"""Security and failure recovery tests; no Qt, desktop or network required."""
import hashlib
import io
import json
import os
from pathlib import Path
import stat
import tempfile
import threading
import unittest
from unittest.mock import patch
import zipfile

from launcher.config import Graphics, Preferences
from launcher.download import download, fetch_manifest, Cancelled, SecureRedirect
from launcher.game import Installation, arguments, Session
from launcher.hardware import Hardware, recommendation
from launcher.installation import (Lease, activate, active_directory, extract_package,
    pointer, recover, repair_stage, rollback, verify)
from launcher.manifest import Release, select_release, https_url, platform_id
from launcher import shortcuts
from launcher.storage import (LauncherError, read_json, write_json, safe_path,
    sha256, parse_json, relative_name)
from launcher.version import Version


def hashed(data, mode=0o644, url=False):
    record = {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'mode': mode}
    if url:
        record['url'] = 'https://releases.example.org/file'
    return record


def fixture(directory, version='0.3.0'):
    suffix = '.exe' if os.name == 'nt' else ''
    catalog = {'schema': 1, 'version': version, 'modes': ['free', 'multiplayer', 'dogfight'], 'aircraft': [
        {'id': 'a320', 'name': 'Airbus A320', 'model': 'assets/a320.glb', 'armed': False, 'lods': [], 'engines': 2},
        {'id': 'typhoon', 'name': 'Eurofighter Typhoon', 'model': 'assets/typhoon.glb', 'armed': True, 'lods': []}]}
    payload = {'ofs_client' + suffix: b'client', 'launcher/ofs_launcher' + suffix: b'launcher',
               'launcher-catalog.json': json.dumps(catalog).encode(),
               'build-info.json': json.dumps({'schema': 1, 'version': version, 'channel': 'stable'}).encode(),
               'assets/a320.glb': b'model a', 'assets/typhoon.glb': b'model t'}
    files = {}
    for name, data in payload.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        mode = 0o755 if name.startswith(('ofs_client', 'launcher/ofs_launcher')) else 0o644
        path.chmod(mode)
        files[name] = hashed(data, mode)
    metadata = {'schema': 1, 'version': version, 'platform': platform_id(), 'channel': 'stable', 'files': files}
    write_json(directory / 'release.json', metadata)
    return metadata


def release_for(metadata, package=b'x'):
    return Release.parse({**metadata, 'files': {k: {**v, 'url': 'https://releases.example.org/' + k} for k, v in metadata['files'].items()},
                          'minimum_launcher_version': '0.3.0', 'minimum_bootstrap_protocol': 1,
                          'notes': 'Flight update', 'package': {**hashed(package), 'url': 'https://releases.example.org/update.zip'}})


class Response(io.BytesIO):
    def __init__(self, body, status=200, headers=None):
        super().__init__(body)
        self.status = status
        self.headers = {'Content-Length': str(len(body))} if headers is None else headers


class Versions(unittest.TestCase):
    def test_semver_order(self):
        versions = ['1.0.0-alpha', '1.0.0-alpha.1', '1.0.0-alpha.beta', '1.0.0-beta', '1.0.0-beta.2', '1.0.0-beta.11', '1.0.0-rc.1', '1.0.0', '1.1.0', '2.0.0']
        self.assertEqual(sorted(versions, key=Version), versions)
        self.assertEqual(Version('1.0.0+a'), Version('1.0.0+b'))
        self.assertLess(Version('0.9.0'), Version('0.10.0'))

    def test_reject_invalid(self):
        for version in ('v1.0.0', '1.0', '01.0.0', '1.0.0-01', '1.0.0-a..b', '1.0.0+'):
            with self.subTest(version=version), self.assertRaises(LauncherError):
                Version(version)


class Engine(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        # Windows tempfile paths can use 8.3 aliases; production paths are resolved.
        self.root = Path(self.temporary.name).resolve()
        self.active = self.root / 'releases/initial'
        self.metadata = fixture(self.active)
        write_json(self.root / 'current.json', {'schema': 1, 'active': 'releases/initial', 'previous': None})
        self.release = release_for(self.metadata)

    def tearDown(self):
        self.temporary.cleanup()

    def make_archive(self, entries=None):
        path = self.root / 'package.zip'
        with zipfile.ZipFile(path, 'w') as archive:
            if entries is None:
                for file in self.active.rglob('*'):
                    if file.is_file():
                        archive.write(file, file.relative_to(self.active).as_posix())
            else:
                for name, body in entries:
                    archive.writestr(name, body)
        return path, release_for(self.metadata, path.read_bytes())

    def test_manifest_selects_platform_channel_version(self):
        newer = fixture(self.root / 'other', '0.4.0')
        release = select_release({'schema': 1, 'releases': [self.release.data, release_for(newer).data]}, 'stable')
        self.assertEqual(release.version, '0.4.0')
        with self.assertRaises(LauncherError):
            select_release({'schema': 1, 'releases': [self.release.data]}, 'development')

    def test_manifest_rejects_mutations(self):
        changes = [('schema', 2), ('version', 'latest'), ('minimum_launcher_version', 'unknown'),
                   ('minimum_bootstrap_protocol', 2), ('platform', 'macos'), ('channel', 'nightly'), ('files', {})]
        for key, value in changes:
            data = {**self.release.data, key: value}
            with self.subTest(key=key), self.assertRaises(LauncherError):
                Release.parse(data)

    def test_reject_bad_package_specs(self):
        for key, value in [('url', 'http://unsafe/file'), ('sha256', 'a' * 63), ('size', -1), ('size', True)]:
            with self.subTest(key=key), self.assertRaises(LauncherError):
                Release.parse({**self.release.data, 'package': {**self.release.package, key: value}})

    def test_reject_file_collision(self):
        for name in ('assets/A320.glb', 'assets/a320.glb/child'):
            files = {**self.release.data['files'], name: hashed(b'x', url=True)}
            with self.assertRaises(LauncherError):
                Release.parse({**self.release.data, 'files': files})

    def test_https_only(self):
        for url in ('http://host/path', 'file:///tmp/data', 'https://user:pass@host/file', 'https://host:444/path', 'https://host/path#fragment'):
            with self.subTest(url=url), self.assertRaises(LauncherError):
                https_url(url)
        self.assertEqual(https_url('https://cdn.example.org/a?signature=opaque'), 'https://cdn.example.org/a?signature=opaque')

    def test_reject_https_downgrade_redirect(self):
        with self.assertRaises(LauncherError):
            SecureRedirect().redirect_request(None, None, 302, '', {}, 'http://unsafe.example.org')

    def test_json_duplicates_and_nan(self):
        for data in ('{"schema":1,"schema":2}', '{"size":NaN}', '{"size":Infinity}', '{broken'):
            with self.assertRaises(LauncherError):
                parse_json(data)

    def test_path_policy(self):
        for name in ('../escape', '/tmp/escape', 'a/../../b', 'C:/escape', 'a\\b', 'a//b', 'a/./b', 'a/CON.txt', 'a./b', 'a /b', 'a\x00b'):
            with self.subTest(name=name), self.assertRaises(LauncherError):
                safe_path(self.root, name)
        self.assertEqual(safe_path(self.root, 'assets/a320.glb'), self.root / 'assets/a320.glb')

    @unittest.skipIf(os.name == 'nt', 'Symlink privilege varies on Windows')
    def test_reject_symlink_parent(self):
        (self.root / 'link').symlink_to(self.active, target_is_directory=True)
        with self.assertRaises(LauncherError):
            safe_path(self.root, 'link/file')

    def test_known_sha256(self):
        path = self.root / 'hash'
        path.write_bytes(b'abc')
        self.assertEqual(sha256(path), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')

    def test_download_verified(self):
        target = self.root / 'download'
        body = b'flight package'
        metrics = []
        download({**hashed(body), 'url': 'https://host/file'}, target, progress=lambda *args: metrics.append(args), opener=lambda *_: Response(body))
        self.assertEqual(target.read_bytes(), body)
        self.assertTrue(metrics)

    def test_download_corruption_discarded(self):
        target = self.root / 'download'
        with self.assertRaisesRegex(LauncherError, 'hash mismatch'):
            download({**hashed(b'good'), 'url': 'https://host/file'}, target, opener=lambda *_: Response(b'evil'))
        self.assertFalse(target.exists())

    def test_download_oversized_discarded(self):
        target = self.root / 'download'
        with self.assertRaises(LauncherError):
            download({**hashed(b'good'), 'url': 'https://host/file'}, target, opener=lambda *_: Response(b'too much', headers={}))
        self.assertFalse(target.exists())

    def test_resume_download(self):
        target = self.root / 'partial'
        target.write_bytes(b'abc')
        def open_range(url, headers):
            self.assertEqual(headers['Range'], 'bytes=3-')
            return Response(b'def', 206, {'Content-Range': 'bytes 3-5/6', 'Content-Length': '3'})
        download({**hashed(b'abcdef'), 'url': 'https://host/file'}, target, opener=open_range)
        self.assertEqual(target.read_bytes(), b'abcdef')

    def test_resume_ignored_restarts(self):
        target = self.root / 'partial'
        target.write_bytes(b'abc')
        download({**hashed(b'abcdef'), 'url': 'https://host/file'}, target, opener=lambda *_: Response(b'abcdef'))
        self.assertEqual(target.read_bytes(), b'abcdef')

    def test_bad_range_rejected(self):
        target = self.root / 'partial'
        target.write_bytes(b'abc')
        with self.assertRaisesRegex(LauncherError, 'range'):
            download({**hashed(b'abcdef'), 'url': 'https://host/file'}, target, opener=lambda *_: Response(b'abc', 206, {'Content-Range': 'bytes 0-2/6'}))
        self.assertEqual(target.read_bytes(), b'abc')

    def test_cancellation_retains_partial(self):
        target = self.root / 'partial'
        target.write_bytes(b'ab')
        event = threading.Event()
        event.set()
        with self.assertRaises(Cancelled):
            download({**hashed(b'abcdef'), 'url': 'https://host/file'}, target, event,
                     opener=lambda *_: Response(b'cdef', 206, {'Content-Range': 'bytes 2-5/6'}))
        self.assertEqual(target.read_bytes(), b'ab')

    def test_short_download_kept_for_retry(self):
        target = self.root / 'partial'
        with self.assertRaisesRegex(LauncherError, 'interrupted'):
            download({**hashed(b'abcdef'), 'url': 'https://host/file'}, target, opener=lambda *_: Response(b'ab', headers={}))
        self.assertEqual(target.read_bytes(), b'ab')

    def test_extract_verified(self):
        package, release = self.make_archive()
        stage = self.root / '.staging/test'
        extract_package(package, stage, release)
        self.assertEqual(verify(stage), {})

    def test_extract_corrupt_package_never_installed(self):
        package, release = self.make_archive()
        package.write_bytes(b'corrupt')
        with self.assertRaises(LauncherError):
            extract_package(package, self.root / '.staging/bad', release)
        self.assertEqual(active_directory(self.root), self.active)

    def test_extract_traversal_and_unexpected_rejected(self):
        for name in ('../escape', '/outside', 'C:/outside', 'unlisted.txt'):
            package, release = self.make_archive([(name, b'x')])
            stage = self.root / '.staging/bad'
            with self.assertRaises(LauncherError):
                extract_package(package, stage, release)
            self.assertFalse(stage.exists())
        self.assertFalse((self.root / 'escape').exists())

    def test_archive_symlink_rejected(self):
        info = zipfile.ZipInfo('assets/a320.glb')
        info.create_system = 3
        info.external_attr = (stat.S_IFLNK | 0o777) << 16
        package = self.root / 'symlink.zip'
        with zipfile.ZipFile(package, 'w') as archive:
            archive.writestr(info, '/etc/passwd')
        with self.assertRaises(LauncherError):
            extract_package(package, self.root / '.staging/bad', release_for(self.metadata, package.read_bytes()))

    def test_archive_duplicate_rejected(self):
        import warnings
        with warnings.catch_warnings():
            warnings.simplefilter('ignore')
            package, release = self.make_archive([('assets/a320.glb', b'model a'), ('assets/a320.glb', b'model a')])
        with self.assertRaises(LauncherError):
            extract_package(package, self.root / '.staging/bad', release)

    def test_verify_identifies_missing_size_and_hash(self):
        (self.active / 'assets/a320.glb').unlink()
        (self.active / 'assets/typhoon.glb').write_bytes(b'garbled')
        (self.active / 'build-info.json').write_bytes(b'x')
        issues = verify(self.active)
        self.assertEqual(issues['assets/a320.glb'], 'missing')
        self.assertEqual(issues['assets/typhoon.glb'], 'hash mismatch')
        self.assertEqual(issues['build-info.json'], 'size mismatch')

    def test_activate_and_rollback_keep_preferences(self):
        settings = self.root / 'user-settings.json'
        settings.write_text('user')
        stage = self.root / '.staging/new'
        fixture(stage, '0.4.0')
        activate(self.root, stage, 'releases/new')
        self.assertEqual(pointer(self.root)['active'], 'releases/new')
        self.assertEqual(pointer(self.root)['previous'], 'releases/initial')
        self.assertTrue(self.active.exists())
        rollback(self.root)
        self.assertEqual(active_directory(self.root), self.active)
        self.assertEqual(settings.read_text(), 'user')

    def test_activation_rejects_incomplete_or_extra_tree(self):
        stage = self.root / '.staging/new'
        fixture(stage)
        (stage / 'unlisted').write_text('extra')
        with self.assertRaises(LauncherError):
            activate(self.root, stage, 'releases/new')
        self.assertEqual(active_directory(self.root), self.active)
        (stage / 'unlisted').unlink()
        (stage / 'assets/a320.glb').unlink()
        with self.assertRaises(LauncherError):
            activate(self.root, stage, 'releases/new')
        self.assertEqual(active_directory(self.root), self.active)

    def test_recover_failed_pointer_commit(self):
        stage = self.root / '.staging/new'
        fixture(stage, '0.4.0')
        from launcher import installation
        actual = installation.write_json
        def fail_pointer(path, data):
            if Path(path).name == 'current.json':
                raise OSError('simulated power loss before pointer replace')
            actual(path, data)
        with patch('launcher.installation.write_json', side_effect=fail_pointer):
            with self.assertRaises(OSError):
                activate(self.root, stage, 'releases/new')
        self.assertEqual(active_directory(self.root), self.active)
        recover(self.root)
        self.assertEqual(pointer(self.root)['active'], 'releases/new')
        self.assertFalse((self.root / '.transaction.json').exists())

    def test_recovery_reverts_damaged_candidate(self):
        stage = self.root / '.staging/new'
        fixture(stage)
        old = pointer(self.root)
        activate(self.root, stage, 'releases/new')
        new = pointer(self.root)
        write_json(self.root / '.transaction.json', {'old': old, 'new': new})
        (self.root / 'releases/new/assets/a320.glb').unlink()
        recover(self.root)
        self.assertEqual(active_directory(self.root), self.active)

    def test_lease_prevents_concurrent_writer(self):
        with Lease(self.root):
            with self.assertRaises(LauncherError):
                Lease(self.root).acquire()
        with Lease(self.root):
            pass

    def test_repair_only_downloads_bad_files(self):
        (self.active / 'assets/a320.glb').write_bytes(b'bad')
        stage = self.root / '.staging/repair'
        calls = []
        def replace_file(record, target, *_):
            calls.append(target.relative_to(stage).as_posix())
            target.write_bytes(b'model a')
        with patch('launcher.installation.download', side_effect=replace_file):
            repair_stage(self.active, stage, self.release)
        self.assertEqual(calls, ['assets/a320.glb'])
        self.assertEqual((self.active / 'assets/a320.glb').read_bytes(), b'bad')
        self.assertFalse(verify(stage))

    def test_repair_failure_retains_active(self):
        (self.active / 'assets/a320.glb').unlink()
        with patch('launcher.installation.download', side_effect=OSError('network unavailable')):
            with self.assertRaises(OSError):
                repair_stage(self.active, self.root / '.staging/repair', self.release)
        self.assertEqual(active_directory(self.root), self.active)

    def test_preferences_roundtrip(self):
        prefs = Preferences(aircraft='typhoon', port=30000, name='Test Pilot')
        path = self.root / 'prefs.json'
        prefs.save(path)
        self.assertEqual(Preferences.load(path), prefs)

    def test_bad_preferences_rejected(self):
        path = self.root / 'prefs.json'
        for data in ({'schema': 2}, {'schema': 1, 'port': 0}, {'schema': 1, 'auto_check': 'false'}):
            write_json(path, data)
            with self.assertRaises(LauncherError):
                Preferences.load(path)

    def test_graphics_roundtrip_preserves_simulator_extensions(self):
        path = self.root / 'graphics.cfg'
        path.write_text('vsync=false\nsunElevation=38\nwidth=1920\n')
        graphics = Graphics(path)
        graphics.preset('Low')
        graphics.save()
        result = Graphics(path)
        self.assertEqual(result.values['vsync'], '0')
        self.assertEqual(result.values['sunElevation'], '38')
        self.assertEqual(result.values['width'], '1920')
        self.assertEqual(result.values['shadows'], '0')

    def test_graphics_keys_and_presets_match_simulator(self):
        from launcher.config import GRAPHICS, PRESETS
        settings = (Path(__file__).resolve().parents[2] / 'client/src/settings.cpp').read_text(encoding='utf-8')
        for key in GRAPHICS:
            self.assertIn(f'read(table, "{key}"', settings, f'{key} is not a simulator setting')
        for name, values in PRESETS.items():
            for key, value in values.items():
                low, high = GRAPHICS[key][1:]
                self.assertTrue(low <= value <= high, f'{name}.{key}')
        # Every preset sets the same options, so switching presets leaves nothing behind.
        self.assertEqual(len({frozenset(values) for values in PRESETS.values()}), 1)
        self.assertEqual([values['preset'] for values in PRESETS.values()], [0, 1, 2, 3])

    def test_bad_graphics_not_overwritten(self):
        path = self.root / 'graphics.cfg'
        for text in ('width=-1', 'msaa=3', 'lodBias=nan', 'height=800.5'):
            path.write_text(text)
            with self.assertRaises(LauncherError):
                Graphics(path)
            self.assertEqual(path.read_text(), text)

    def test_corrupt_graphics_encoding_reported(self):
        path = self.root / 'graphics.cfg'
        path.write_bytes(b'\xff\x00')
        with self.assertRaisesRegex(LauncherError, 'UTF-8'):
            Graphics(path)
        self.assertEqual(path.read_bytes(), b'\xff\x00')

    def test_corrupt_catalogue_and_build_metadata_reported(self):
        path = self.active / 'launcher-catalog.json'
        original = read_json(path)
        for mutation in ({**original, 'modes': ['free', []]},
                         {**original, 'aircraft': [{'id': 'a320', 'name': 'A320', 'armed': False, 'lods': []}]}):
            write_json(path, mutation)
            with self.assertRaises(LauncherError):
                Installation.discover(self.root)
        write_json(path, original)
        write_json(self.active / 'build-info.json', [])
        with self.assertRaises(LauncherError):
            Installation.discover(self.root)

    def test_optional_metadata_cannot_override_registry_capabilities(self):
        path = self.active / 'data/launcher/aircraft-info.json'
        write_json(path, {'a320': {'armed': True, 'id': 'fake', 'model': '../outside', 'manufacturer': 'Airbus'}})
        entry = Installation.discover(self.root).aircraft[0]
        self.assertEqual(entry['id'], 'a320')
        self.assertFalse(entry['armed'])
        self.assertEqual(entry['manufacturer'], 'Airbus')

    def test_aircraft_discovery_is_registry_driven(self):
        installation = Installation.discover(self.root)
        self.assertEqual([a['id'] for a in installation.aircraft], ['a320', 'typhoon'])
        catalog = installation.catalog
        catalog['aircraft'].append({'id': 'future', 'name': 'Future Aircraft', 'model': 'assets/new.glb', 'armed': False, 'lods': []})
        write_json(self.active / 'launcher-catalog.json', catalog)
        installation = Installation.discover(self.root)
        self.assertEqual(installation.aircraft[-1]['id'], 'future')
        self.assertEqual(installation.missing_assets(), ['assets/new.glb'])

    def test_safe_arguments_and_mode_validation(self):
        installation = Installation.discover(self.root)
        graphics = Graphics(self.root / 'graphics.cfg')
        prefs = Preferences(aircraft='typhoon', mode='dogfight')
        self.assertIn('--bots', arguments(installation, prefs, graphics))
        prefs.aircraft = 'a320'
        with self.assertRaises(LauncherError):
            arguments(installation, prefs, graphics)
        prefs.mode, prefs.server = 'multiplayer', 'example.org'
        with self.assertRaises(LauncherError):
            arguments(installation, prefs, graphics)
        prefs.server = '::1'
        self.assertIn('::1', arguments(installation, prefs, graphics))
        prefs.name = 'non-ascii-✈'
        with self.assertRaises(LauncherError):
            arguments(installation, prefs, graphics)
        prefs.name = 'Pilot ; $(no shell)'
        self.assertIn(prefs.name, arguments(installation, prefs, graphics))

    def test_source_build_runs_from_its_asset_root(self):
        installation = Installation.discover(self.root)
        graphics = Graphics(self.root / 'user/graphics.cfg')
        for managed, expected in ((False, installation.directory), (True, self.root / 'user/runtime')):
            installation.managed = managed
            session = Session()
            with patch('launcher.game.spawn') as spawn:
                session.start(installation, Preferences(), graphics, self.root / 'user', self.root / 'user/runtime')
            self.assertEqual(spawn.call_args.kwargs['cwd'], expected)
            session.cleanup()

    def test_offline_build_rejects_network_modes(self):
        installation = Installation.discover(self.root)
        installation.catalog['modes'] = ['free']
        with self.assertRaises(LauncherError):
            arguments(installation, Preferences(mode='multiplayer'), Graphics(self.root / 'graphics.cfg'))

    def test_hardware_policy_never_uses_vendor_name(self):
        for vendor in ('NVIDIA RTX', 'AMD Radeon', 'Unknown GPU'):
            name, _ = recommendation(Hardware(cores=8, ram_gib=16, gpu=vendor, vram_gib=0))
            self.assertEqual(name, 'Medium')
        self.assertEqual(recommendation(Hardware(cores=2, ram_gib=4))[0], 'Low')
        self.assertEqual(recommendation(Hardware(cores=8, ram_gib=32, vram_gib=12))[0], 'High')


if __name__ == '__main__':
    unittest.main()


@unittest.skipIf(os.name == 'nt', 'Desktop entries are the Linux menu format')
class Shortcuts(unittest.TestCase):
    def test_menu_entry_is_quoted_and_refreshed_only_on_change(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, {'XDG_DATA_HOME': temp}):
            target = Path(temp) / 'My $Games 100%' / 'OpenFlightSim'
            target.parent.mkdir()
            target.write_bytes(b'')
            entry = shortcuts.install(target)
            text = entry.read_text()
            self.assertEqual(entry, Path(temp) / 'applications/openflightsim.desktop')
            self.assertIn('Exec="' + str(target.resolve()).replace('$', '\\$').replace('%', '%%') + '"\n', text)
            self.assertIn('Icon=applications-games\n', text)
            stamp = entry.stat().st_mtime_ns
            self.assertEqual(shortcuts.install(target), entry)
            self.assertEqual(entry.stat().st_mtime_ns, stamp)
            moved = target.parent / 'Moved'
            moved.write_bytes(b'')
            self.assertIn('Moved', shortcuts.install(moved, icon=moved).read_text())

    def test_failure_never_raises(self):
        with tempfile.TemporaryDirectory() as temp:
            blocker = Path(temp) / 'file'
            blocker.write_bytes(b'')
            with patch.dict(os.environ, {'XDG_DATA_HOME': str(blocker)}):
                self.assertIsNone(shortcuts.install(Path(temp) / 'OpenFlightSim'))
