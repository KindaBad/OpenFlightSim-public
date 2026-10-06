#!/usr/bin/env python3
"""Freeze native launcher and Qt-free bootstrap with the same CMake version."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from launcher.manifest import https_url, platform_id
from launcher.storage import read_json, write_json


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--base-url', help='Public HTTPS release host; also build a standalone setup executable')
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    if not (build / 'build-info.json').is_file():
        parser.error('Configure and build CMake first')
    if args.base_url:
        https_url(args.base_url)
    qt_options = ['--windowed', '--exclude-module', 'PySide6.QtNetwork', '--exclude-module', 'PySide6.QtQml',
                  '--exclude-module', 'PySide6.QtQuick', '--exclude-module', 'PySide6.QtWebEngineCore']
    for name, entry, extra in [
        ('ofs_launcher', 'launcher/entry.py', [*qt_options, '--add-data', str(build / 'build-info.json') + ':.']),
        ('OpenFlightSim', 'launcher/bootstrap_entry.py', ['--onefile', '--exclude-module', 'PySide6', *(['--windowed'] if sys.platform == 'win32' else [])]),
    ]:
        subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--noupx',
                        '--name', name, '--paths', str(ROOT), '--distpath', str(output),
                        '--workpath', str(build / 'freeze' / name), '--specpath', str(build / 'freeze'),
                        *extra, str(ROOT / entry)], cwd=ROOT, check=True)
    if args.base_url:
        info = read_json(build / 'build-info.json')
        settings = {'schema': 1, 'version': info['version'], 'channel': info['channel'],
                    'platform': platform_id(), 'manifest_url': args.base_url.rstrip('/') + '/manifest.json'}
        config = build / 'freeze/setup-config.json'
        write_json(config, settings)
        # Keep a copy for package_release to reject a mismatched endpoint/channel.
        write_json(output / 'setup-config.json', settings)
        suffix = '.exe' if sys.platform == 'win32' else ''
        subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--noupx',
                        '--onefile', '--name', 'OpenFlightSim-Setup', '--paths', str(ROOT),
                        '--distpath', str(output), '--workpath', str(build / 'freeze/setup'),
                        '--specpath', str(build / 'freeze'), *qt_options,
                        '--add-data', str(config) + ':.',
                        '--add-data', str(output / ('OpenFlightSim' + suffix)) + ':bootstrap',
                        str(ROOT / 'launcher/setup_entry.py')], cwd=ROOT, check=True)
    print(f'Launcher and updater/bootstrap bundles: {output}')


if __name__ == '__main__':
    main()
