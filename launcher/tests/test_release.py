"""Real packaging, helper activation and publication-index consistency."""
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

from test_engine import fixture, release_for
from launcher.bootstrap import run
from launcher.installation import pointer, verify
from launcher.storage import LauncherError, read_json, write_json
from launcher.platform_process import child_environment
from scripts.package_release import package, check_asset_approval
from scripts.merge_update_manifests import merge


class ReleasePipeline(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.simulator = self.root / 'simulator'
        self.metadata = fixture(self.simulator)
        # CMake stage contains no GUI; the packaging step adds that bundle.
        shutil.rmtree(self.simulator / 'launcher')
        (self.simulator / 'release.json').unlink()
        self.bundles = self.root / 'bundles'
        (self.bundles / 'ofs_launcher').mkdir(parents=True)
        suffix = '.exe' if os.name == 'nt' else ''
        (self.bundles / 'ofs_launcher' / ('ofs_launcher' + suffix)).write_bytes(b'launcher')
        (self.bundles / 'ofs_launcher' / ('ofs_launcher' + suffix)).chmod(0o755)
        (self.bundles / ('OpenFlightSim' + suffix)).write_bytes(b'bootstrap')
        (self.bundles / ('OpenFlightSim' + suffix)).chmod(0o755)
        (self.bundles / ('OpenFlightSim-Setup' + suffix)).write_bytes(b'setup')
        write_json(self.bundles / 'setup-config.json', {
            'schema': 1, 'version': '0.3.0', 'channel': 'stable',
            'platform': self.metadata['platform'], 'manifest_url': 'https://updates.example.org/game/manifest.json'})

    def tearDown(self):
        self.temporary.cleanup()

    def test_local_package_helper_update_and_rollback(self):
        output = self.root / 'publish'
        descriptor = package(self.simulator, self.bundles, output, 'https://updates.example.org', local=True)
        work = next(output.glob('*-work'))
        distribution = work / 'distribution'
        installed = distribution / pointer(distribution)['active']
        self.assertEqual(verify(installed), {})
        self.assertTrue((installed / 'LOCAL-DEVELOPMENT-ONLY.txt').exists())
        cache = distribution / '.downloads'
        cache.mkdir()
        archive = next(output.glob('*-update.zip'))
        shutil.copy2(archive, cache / 'update.zip')
        request = distribution / '.pending-update.json'
        write_json(request, {'kind': 'update', 'release': descriptor, 'package': '.downloads/update.zip'})
        run(distribution, 'apply', request, restart=False)
        self.assertNotEqual(pointer(distribution)['active'], pointer(distribution)['previous'])
        self.assertEqual(verify(distribution / pointer(distribution)['active']), {})
        # The consumed package is reclaimed; both rollback slots survive.
        self.assertEqual(list(cache.iterdir()), [])
        run(distribution, 'rollback', restart=False)
        self.assertEqual(pointer(distribution)['active'], 'releases/0.3.0-initial')
        first_update = pointer(distribution)['previous']
        shutil.copy2(archive, cache / 'update.zip')
        write_json(request, {'kind': 'update', 'release': descriptor, 'package': '.downloads/update.zip'})
        run(distribution, 'apply', request, restart=False)
        state = pointer(distribution)
        self.assertEqual(state['previous'], 'releases/0.3.0-initial')
        self.assertFalse((distribution / first_update).exists())
        self.assertEqual({p.name for p in (distribution / 'releases').iterdir()},
                         {state['active'].split('/')[1], '0.3.0-initial'})
        self.assertEqual(list((distribution / '.staging').iterdir()), [])

    @unittest.skipIf(os.name == 'nt', 'Linux runtime libraries are installed as soname links')
    def test_bundled_library_links_become_regular_files(self):
        library = self.simulator / 'lib'
        library.mkdir()
        (library / 'libprotobuf.so.30.0.6').write_bytes(b'protobuf')
        (library / 'libprotobuf.so.30').symlink_to('libprotobuf.so.30.0.6')
        output = self.root / 'publish'
        descriptor = package(self.simulator, self.bundles, output, 'https://updates.example.org', local=True)
        self.assertIn('lib/libprotobuf.so.30', descriptor['files'])
        installed = next(output.glob('*-work')) / 'distribution/releases/0.3.0-initial'
        self.assertFalse((installed / 'lib/libprotobuf.so.30').is_symlink())
        self.assertEqual(verify(installed), {})

    def test_public_packaging_requires_approval(self):
        write_json(self.bundles / 'setup-config.json', {
            **read_json(self.bundles / 'setup-config.json'), 'manifest_url': 'https://updates.example.org/manifest.json'})
        with self.assertRaisesRegex(LauncherError, 'approval'):
            package(self.simulator, self.bundles, self.root / 'bad', 'https://updates.example.org')

    def test_approval_is_hash_bound(self):
        records = {name: {'redistributable': True, 'license': 'Original fixture', 'source': 'Unit test fixture', 'sha256': self.metadata['files'][name]['sha256']} for name in ('assets/a320.glb', 'assets/typhoon.glb')}
        path = self.root / 'approved.json'
        write_json(path, {'schema': 1, 'assets': records})
        check_asset_approval(self.simulator, path)
        # Same source/destination is legal in CI provisioning.
        check_asset_approval(self.simulator, self.simulator / 'asset-approval.json')
        (self.simulator / 'assets/a320.glb').write_bytes(b'changed')
        with self.assertRaises(LauncherError):
            check_asset_approval(self.simulator, path)

    def test_public_bundle_sets_default_publisher(self):
        approval = self.root / 'approval.json'
        write_json(approval, {'schema': 1, 'assets': {name: {'redistributable': True, 'license': 'Original fixture', 'source': 'Fixture', 'sha256': self.metadata['files'][name]['sha256']} for name in ('assets/a320.glb', 'assets/typhoon.glb')}})
        output = self.root / 'public'
        descriptor = package(self.simulator, self.bundles, output, 'https://updates.example.org/game', approval=approval)
        work = next(output.glob('*-work'))
        self.assertEqual(read_json(work / 'payload/publisher.json')['manifest_url'], 'https://updates.example.org/game/manifest.json')
        self.assertEqual(descriptor['minimum_launcher_version'], '0.3.0')
        self.assertIn('publisher.json', descriptor['files'])
        setup = next(output.glob('*-setup*'))
        self.assertEqual(setup.read_bytes(), b'setup')
        from launcher.storage import sha256
        self.assertEqual(descriptor['setup']['sha256'], sha256(setup))
        self.assertIn(setup.name, next(output.glob('*-SHA256SUMS.txt')).read_text())

    def test_setup_endpoint_must_match_publication(self):
        with self.assertRaisesRegex(LauncherError, 'configuration'):
            package(self.simulator, self.bundles, self.root / 'mismatch', 'https://another.example.org')

    def test_merge_preserves_versions_rejects_conflicts(self):
        first = self.root / 'first.json'
        second = self.root / 'second.json'
        descriptor = release_for(self.metadata).data
        write_json(first, {'schema': 1, 'releases': [descriptor]})
        write_json(second, {'schema': 1, 'releases': [descriptor]})
        self.assertEqual(len(merge([first, second])['releases']), 1)
        write_json(second, {'schema': 1, 'releases': [{**descriptor, 'notes': 'Different bytes under same version'}]})
        with self.assertRaises(LauncherError):
            merge([first, second])

    def test_frozen_child_environment_restores_library_path(self):
        import sys
        with patch.object(sys, 'frozen', True, create=True), patch.object(sys, '_MEIPASS', str(self.root), create=True), patch.dict(os.environ, {'LD_LIBRARY_PATH': str(self.root), 'LD_LIBRARY_PATH_ORIG': '/original/runtime', 'PATH': str(self.root / 'bin') + os.pathsep + '/usr/bin'}):
            environment = child_environment()
        self.assertEqual(environment['LD_LIBRARY_PATH'], '/original/runtime')
        self.assertNotIn('LD_LIBRARY_PATH_ORIG', environment)
        self.assertEqual(environment['PYINSTALLER_RESET_ENVIRONMENT'], '1')
        self.assertNotIn(str(self.root), environment['PATH'])


if __name__ == '__main__':
    unittest.main()
