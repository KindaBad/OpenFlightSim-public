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
from scripts.publish_github_release import publication_files, publish


class ReleasePipeline(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
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

    def github_package(self):
        repo = 'publisher/game'
        index = f'https://github.com/{repo}/releases/download/launcher-updates'
        immutable = f'https://github.com/{repo}/releases/download/v0.3.0'
        write_json(self.bundles / 'setup-config.json', {
            **read_json(self.bundles / 'setup-config.json'), 'manifest_url': index + '/manifest.json'})
        approval = self.root / 'approval.json'
        write_json(approval, {'schema': 1, 'assets': {name: {
            'redistributable': True, 'license': 'Original fixture', 'source': 'Fixture',
            'sha256': self.metadata['files'][name]['sha256']}
            for name in ('assets/a320.glb', 'assets/typhoon.glb')}})
        output = self.root / 'github'
        descriptor = package(self.simulator, self.bundles, output, index, approval=approval,
                             download_base_url=immutable, flat_downloads=True)
        write_json(output / 'manifest.json', {'schema': 1, 'releases': [descriptor]})
        return output, descriptor, repo, index, immutable

    def test_github_packages_keep_fixed_index_and_verify_flat_repairs(self):
        output, descriptor, repo, index, immutable = self.github_package()
        self.assertTrue(descriptor['package']['url'].startswith(immutable + '/'))
        self.assertEqual(read_json(next(output.glob('*-work')) / 'payload/publisher.json')['manifest_url'],
                         index + '/manifest.json')
        manifest = {'schema': 1, 'releases': [descriptor]}
        write_json(output / 'manifest.json', manifest)
        published = publication_files(output, manifest, repo, 'v0.3.0')
        self.assertNotIn('asset-approval.json', published)  # Only its hash-named repair record is public.
        for name, record in descriptor['files'].items():
            blob = 'file-' + record['sha256']
            self.assertEqual(record['url'], immutable + '/' + blob)
            self.assertIn(blob, published)
        # A corrupted repair blob must prevent every network publication step.
        (output / 'repair' / ('file-' + descriptor['files']['assets/a320.glb']['sha256'])).write_bytes(b'bad')
        with self.assertRaisesRegex(LauncherError, 'mismatch'):
            publication_files(output, manifest, repo, 'v0.3.0')

    def test_publication_announces_only_verified_complete_packages(self):
        output, descriptor, repo, index, immutable = self.github_package()
        manifest = read_json(output / 'manifest.json')
        files = publication_files(output, manifest, repo, 'v0.3.0')
        from launcher.storage import sha256
        uploaded = {'assets': [{'name': name, 'size': p.stat().st_size, 'digest': 'sha256:' + sha256(p)}
                              for name, p in files.items()]}
        commands = []
        def command(*args):
            commands.append(args)
            return '{"private": false}' if args[0] == 'api' else ''
        with patch('scripts.publish_github_release.gh', side_effect=command), \
             patch('scripts.publish_github_release.release_info', side_effect=[None, None, uploaded]), \
             patch('scripts.publish_github_release.fetch_manifest', return_value=manifest):
            publish(output, repo, 'a' * 40, self.root / 'notes.md')
        version_edit = next(i for i, c in enumerate(commands) if c[:3] == ('release', 'edit', 'v0.3.0'))
        index_upload = next(i for i, c in enumerate(commands) if c[:3] == ('release', 'upload', 'launcher-updates'))
        self.assertLess(version_edit, index_upload)

    def test_conflicting_existing_version_prevents_network_mutations(self):
        output, descriptor, repo, *_ = self.github_package()
        commands = []
        def command(*args):
            commands.append(args)
            if args[0] == 'api':
                return '{"private": false}'
            if args[:2] == ('release', 'download'):
                directory = Path(args[args.index('--dir') + 1])
                write_json(directory / 'manifest.json', {'schema': 1, 'releases': [{**descriptor, 'notes': 'Published notes differ'}]})
            return ''
        with patch('scripts.publish_github_release.gh', side_effect=command), \
             patch('scripts.publish_github_release.release_info', return_value={'draft': False, 'assets': [{'name': 'manifest.json'}]}):
            with self.assertRaisesRegex(LauncherError, 'Conflicting'):
                publish(output, repo, 'a' * 40, self.root / 'notes.md')
        self.assertFalse(any(c[:2] in (('release', 'create'), ('release', 'upload'), ('release', 'edit')) for c in commands))

    def test_github_publication_rejects_wrong_tag_and_host(self):
        output = self.root / 'publish'
        descriptor = package(self.simulator, self.bundles, output, 'https://updates.example.org', local=True)
        # Rejection precedes any attempt to find installer/setup files.
        with self.assertRaisesRegex(LauncherError, 'version/channel'):
            publication_files(output, {'schema': 1, 'releases': [descriptor]}, 'publisher/game', 'v0.4.0')
        output, descriptor, repo, *_ = self.github_package()
        descriptor['package']['url'] = 'https://another.example.org/update.zip'
        with self.assertRaisesRegex(LauncherError, 'immutable version tag'):
            publication_files(output, {'schema': 1, 'releases': [descriptor]}, repo, 'v0.3.0')

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
