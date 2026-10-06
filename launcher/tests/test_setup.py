"""First installation/retry/failure regressions with real archives and hashing."""
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

from test_engine import fixture, release_for, Response
from launcher.download import Cancelled, download
from launcher.game import Installation
from launcher.installation import Lease, pointer, verify
from launcher.manifest import platform_id
from launcher.setup import bootstrap_name, configuration, install
from launcher.storage import LauncherError, write_json
from scripts.package_release import archive_tree, records_for


class Setup(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.root = self.base / 'game with spaces'
        payload = self.base / 'payload'
        fixture(payload)
        self.settings = {'schema': 1, 'version': '0.3.0', 'channel': 'stable',
                         'platform': platform_id(), 'manifest_url': 'https://updates.example.org/manifest.json'}
        write_json(payload / 'publisher.json', {'schema': 1, 'manifest_url': self.settings['manifest_url']})
        (payload / 'release.json').unlink()
        metadata = {'schema': 1, 'version': '0.3.0', 'platform': platform_id(), 'channel': 'stable', 'files': records_for(payload)}
        write_json(payload / 'release.json', metadata)
        self.archive = self.base / 'release.zip'
        archive_tree(payload, self.archive)
        self.body = self.archive.read_bytes()
        self.release = release_for(metadata, self.body)
        self.bootstrap = self.base / bootstrap_name()
        self.bootstrap.write_bytes(b'trusted bootstrap')

    def tearDown(self):
        self.temp.cleanup()

    def perform(self, cancel=None, status=None, body=None):
        def transfer(record, target, **kwargs):
            return download(record, target, **kwargs, opener=lambda u, h: Response(self.body if body is None else body))
        with patch('launcher.setup.fetch_manifest', return_value={'schema': 1, 'releases': [self.release.data]}), patch('launcher.setup.download', side_effect=transfer):
            return install(self.root, self.settings, self.bootstrap, cancel=cancel, status=status)

    def test_fresh_install_then_offline_reopen(self):
        phases = []
        self.assertEqual(self.perform(status=phases.append), self.root)
        self.assertTrue(any('Downloading' in phase for phase in phases))
        self.assertIsNone(pointer(self.root)['previous'])
        installed = Installation.discover(self.root)
        self.assertTrue(installed.managed)
        self.assertEqual(verify(installed.directory), {})
        self.assertEqual(installed.missing_assets(), [])
        self.assertEqual((self.root / bootstrap_name()).read_bytes(), b'trusted bootstrap')
        self.assertEqual(list((self.root / '.downloads').iterdir()), [])
        with patch('launcher.setup.fetch_manifest', side_effect=AssertionError('must work offline')):
            self.assertEqual(install(self.root, self.settings, self.bootstrap), self.root)
        with Lease(self.root):
            pass  # no lock left behind for the launcher

    def test_corrupt_package_never_commits_or_runs(self):
        corrupt = bytes([self.body[0] ^ 1]) + self.body[1:]
        with patch('launcher.platform_process.spawn') as spawn:
            with self.assertRaisesRegex(LauncherError, 'hash mismatch'):
                self.perform(body=corrupt)
            spawn.assert_not_called()
        self.assertFalse((self.root / 'current.json').exists())
        self.assertFalse((self.root / bootstrap_name()).exists())
        self.perform()  # retry completes

    def test_cancelled_download_resumes_same_archive(self):
        cancel = threading.Event()
        def phase(message):
            if message.startswith('Downloading'):
                cancel.set()
        with self.assertRaises(Cancelled):
            self.perform(cancel=cancel, status=phase)
        self.assertFalse((self.root / 'current.json').exists())
        self.perform()
        self.assertTrue((self.root / 'current.json').exists())

    def test_partial_download_uses_http_range_on_retry(self):
        partial = self.root / '.downloads' / (self.release.package['sha256'] + '.zip')
        partial.parent.mkdir(parents=True)
        offset = 32
        partial.write_bytes(self.body[:offset])
        requests = []
        def response(url, headers):
            requests.append(headers)
            return Response(self.body[offset:], status=206, headers={
                'Content-Length': str(len(self.body) - offset),
                'Content-Range': f'bytes {offset}-{len(self.body)-1}/{len(self.body)}'})
        def transfer(record, target, **kwargs):
            return download(record, target, **kwargs, opener=response)
        with patch('launcher.setup.fetch_manifest', return_value={'schema': 1, 'releases': [self.release.data]}), patch('launcher.setup.download', side_effect=transfer):
            install(self.root, self.settings, self.bootstrap)
        self.assertEqual(requests, [{'Range': 'bytes=32-'}])
        self.assertEqual(verify(Installation.discover(self.root).directory), {})

    def test_cancel_during_extraction_discards_stage(self):
        cancel = threading.Event()
        def phase(message):
            if message.startswith('Unpacking'):
                cancel.set()
        with self.assertRaises(Cancelled):
            self.perform(cancel=cancel, status=phase)
        self.assertFalse((self.root / 'current.json').exists())
        self.assertEqual(list((self.root / '.staging').iterdir()), [])
        self.perform()

    def test_failed_first_pointer_write_can_retry(self):
        original = write_json
        def write(path, data):
            if Path(path) == self.root / 'current.json':
                raise OSError('simulated interruption')
            return original(path, data)
        with patch('launcher.installation.write_json', side_effect=write), self.assertRaises(OSError):
            self.perform()
        self.assertFalse((self.root / 'current.json').exists())
        self.perform()
        self.assertEqual(len(list((self.root / 'releases').iterdir())), 1)

    def test_insufficient_space_rejected_before_download(self):
        from collections import namedtuple
        Usage = namedtuple('Usage', 'total used free')
        with patch('launcher.setup.shutil.disk_usage', return_value=Usage(10, 9, 1)), self.assertRaisesRegex(LauncherError, 'free space'):
            self.perform()
        self.assertFalse((self.root / 'current.json').exists())

    def test_installer_obeys_running_game_lease(self):
        with Lease(self.root), self.assertRaisesRegex(LauncherError, 'Another launcher'):
            self.perform()

    def test_wrong_platform_channel_and_old_launcher(self):
        with self.assertRaises(LauncherError):
            configuration({**self.settings, 'platform': 'windows-x86_64' if platform_id().startswith('linux') else 'linux-x86_64'})
        with self.assertRaises(LauncherError):
            configuration({**self.settings, 'channel': 'invalid'})
        self.release.data['minimum_launcher_version'] = '0.4.0'
        with self.assertRaisesRegex(LauncherError, 'latest launcher'):
            self.perform()

    def test_existing_install_other_channel_and_damage(self):
        self.perform()
        with self.assertRaisesRegex(LauncherError, 'separate folder'):
            install(self.root, {**self.settings, 'channel': 'development'}, self.bootstrap)
        installed = Installation.discover(self.root)
        installed.executable.write_bytes(b'corrupt')
        with self.assertRaisesRegex(LauncherError, 'needs repair'):
            self.perform()

    def test_publisher_endpoint_mismatch_rejected(self):
        self.settings['manifest_url'] = 'https://another.example.org/manifest.json'
        with self.assertRaisesRegex(LauncherError, 'addresses disagree'):
            self.perform()
        self.assertFalse((self.root / 'current.json').exists())


if __name__ == '__main__':
    unittest.main()
