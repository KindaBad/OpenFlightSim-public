#!/usr/bin/env python3
"""Create consistent managed installs, update archives, and public file indexes.

Input is a CMake-installed simulator + frozen launcher bundles. Generated files
remain in ignored build directories. Public assets require hash-bound approval.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import sys
from urllib.parse import quote
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from launcher.storage import LauncherError, read_json, write_json, sha256, relative_name
from launcher.manifest import platform_id, https_url, Release
from launcher.game import Installation


def check_asset_approval(stage, approval_path):
    approval = read_json(approval_path)
    if approval.get('schema') != 1 or not isinstance(approval.get('assets'), dict):
        raise LauncherError('Invalid asset release approval')
    catalog = read_json(stage / 'launcher-catalog.json')
    for aircraft in catalog['aircraft']:
        for name in [aircraft['model'], *aircraft['lods']]:
            record = approval['assets'].get(name, {})
            if record.get('redistributable') is not True or any(not isinstance(record.get(key), str) or not record[key].strip() for key in ('license', 'source')):
                raise LauncherError(f'Public release asset lacks explicit redistribution approval: {name}')
            if sha256(stage / name) != record.get('sha256'):
                raise LauncherError(f'Approved asset digest does not match: {name}')
    if Path(approval_path).resolve() != (stage / 'asset-approval.json').resolve():
        shutil.copy2(approval_path, stage / 'asset-approval.json')


def records_for(directory):
    records = {}
    for path in sorted(directory.rglob('*')):
        if path.is_symlink():
            raise LauncherError(f'Symlink cannot be packaged: {path}')
        if path.is_dir():
            continue
        if not path.is_file():
            raise LauncherError('Special files cannot be packaged')
        name = relative_name(path.relative_to(directory).as_posix())
        executable = bool(path.stat().st_mode & 0o111) if os.name != 'nt' else path.suffix.lower() in ('.exe', '.dll')
        records[name] = {'size': path.stat().st_size, 'sha256': sha256(path), 'mode': 0o755 if executable else 0o644}
    return records


def archive_tree(directory, output):
    # Store only regular files; use explicit portable modes, no OS links/ACLs.
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(directory.rglob('*')):
            if not path.is_file():
                continue
            info = zipfile.ZipInfo(path.relative_to(directory).as_posix(), (2020, 1, 1, 0, 0, 0))
            mode = 0o755 if (path.stat().st_mode & 0o111 or path.suffix.lower() in ('.exe', '.dll')) else 0o644
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            # Stream large models rather than reading the entire pack into RAM.
            with path.open('rb') as source, archive.open(info, 'w', force_zip64=True) as target:
                shutil.copyfileobj(source, target, 1024 * 1024)


def package(simulator, bundles, output, base_url, approval=None, local=False, notes='', platform=None):
    simulator, bundles, output = Path(simulator), Path(bundles), Path(output)
    build = read_json(simulator / 'build-info.json')
    target = platform or platform_id()
    https_url(base_url)
    output.mkdir(parents=True, exist_ok=True)
    name = f'OpenFlightSim-{build["version"]}-{build["channel"]}-{target}'
    suffix = '.exe' if target.startswith('windows') else ''
    setup = bundles / ('OpenFlightSim-Setup' + suffix)
    if not local:
        expected = {'schema': 1, 'version': build['version'], 'channel': build['channel'],
                    'platform': target, 'manifest_url': base_url.rstrip('/') + '/manifest.json'}
        if not setup.is_file() or not (bundles / 'setup-config.json').is_file():
            raise LauncherError('Public packaging requires the standalone setup; freeze with --base-url first')
        if read_json(bundles / 'setup-config.json') != expected:
            raise LauncherError('Standalone setup configuration does not match this release')
    work = output / (name + '-work')
    if work.exists():
        raise LauncherError('Packaging work directory already exists; use a fresh output directory')
    stage = work / 'payload'
    shutil.copytree(simulator, stage)
    shutil.copytree(bundles / 'ofs_launcher', stage / 'launcher')
    if not local:
        if not approval:
            raise LauncherError('Public packaging requires --asset-approval; --local-development does not permit publishing')
        check_asset_approval(stage, approval)
    else:
        (stage / 'LOCAL-DEVELOPMENT-ONLY.txt').write_text('Local use only. Assets have not been approved for redistribution.\n')
    if not local:
        write_json(stage / 'publisher.json', {'schema': 1, 'manifest_url': base_url.rstrip('/') + '/manifest.json'})
    Installation.discover(stage)  # executable/catalogue presence and version coherence
    missing = Installation.discover(stage).missing_assets()
    if missing:
        raise LauncherError('Incomplete runtime asset pack: ' + ', '.join(missing))
    files = records_for(stage)
    metadata = {'schema': 1, 'version': build['version'], 'channel': build['channel'], 'platform': target, 'files': files}
    write_json(stage / 'release.json', metadata)
    package_path = output / (name + '-update.zip')
    archive_tree(stage, package_path)
    content_path = output / 'files' / name
    shutil.copytree(stage, content_path)
    file_records = {path: {**record, 'url': base_url.rstrip('/') + '/files/' + name + '/' + quote(path, safe='/')} for path, record in files.items()}
    descriptor = {**metadata, 'files': file_records, 'minimum_launcher_version': build.get('minimum_launcher_version', '0.3.0'), 'minimum_bootstrap_protocol': 1,
                  'notes': notes, 'package': {'url': base_url.rstrip('/') + '/' + package_path.name,
                    'size': package_path.stat().st_size, 'sha256': sha256(package_path)}}
    Release.parse(descriptor)
    distribution = work / 'distribution'
    active = f'releases/{build["version"]}-initial'
    destination = distribution / active
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(stage, destination)
    shutil.copy2(bundles / ('OpenFlightSim' + suffix), distribution / ('OpenFlightSim' + suffix))
    write_json(distribution / 'current.json', {'schema': 1, 'active': active, 'previous': None})
    start = ('Double-click OpenFlightSim.exe. If Windows SmartScreen appears, choose More info, then Run anyway.'
             if suffix else
             'Double-click OpenFlightSim. If your file manager does not start it, right-click it and choose\n'
             'Run as a Program, or run ./OpenFlightSim in a terminal.')
    (distribution / 'README.txt').write_text(
        'OpenFlightSim\n\n1. Extract this whole folder somewhere in your home folder (for example Games).\n'
        '2. ' + start + '\n3. Press PLAY.\n\n'
        'After the first start, OpenFlightSim is also in your applications menu.\n'
        'Updates are offered inside the launcher; nothing else needs to be installed or configured.\n'
        'Settings and logs are kept separately in your user account.\n', encoding='utf-8')
    # Publisher endpoint is bundled in each payload; consumed only on first launch.
    # The local fixture intentionally leaves a blank endpoint to prevent accidental publishing.
    installer_path = output / (name + '-install.zip')
    archive_tree(distribution, installer_path)
    descriptor['installer'] = {'url': base_url.rstrip('/') + '/' + installer_path.name,
                              'size': installer_path.stat().st_size, 'sha256': sha256(installer_path)}
    setup_sum = ''
    if not local:
        setup_path = output / (name + '-setup' + suffix)
        shutil.copy2(setup, setup_path)
        descriptor['setup'] = {'url': base_url.rstrip('/') + '/' + setup_path.name,
                               'size': setup_path.stat().st_size, 'sha256': sha256(setup_path)}
        setup_sum = f'{descriptor["setup"]["sha256"]}  {setup_path.name}\n'
    Release.parse(descriptor)
    write_json(output / (name + '-manifest.json'), {'schema': 1, 'releases': [descriptor]})
    (output / (name + '-SHA256SUMS.txt')).write_text(
        f'{descriptor["installer"]["sha256"]}  {installer_path.name}\n'
        f'{descriptor["package"]["sha256"]}  {package_path.name}\n' + setup_sum, encoding='ascii')
    return descriptor


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--simulator', type=Path, required=True)
    parser.add_argument('--bundles', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--base-url', required=True)
    parser.add_argument('--asset-approval', type=Path)
    parser.add_argument('--local-development', action='store_true')
    parser.add_argument('--notes', type=Path)
    args = parser.parse_args()
    package(args.simulator, args.bundles, args.output, args.base_url, args.asset_approval,
            args.local_development, args.notes.read_text() if args.notes else '')


if __name__ == '__main__':
    main()
