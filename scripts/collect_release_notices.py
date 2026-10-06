#!/usr/bin/env python3
"""Collect dependency notices from the actual build and Python distributions."""
import argparse
import importlib.metadata
from pathlib import Path
import shutil
import os

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--deps', type=Path, action='append', default=[])
    args = parser.parse_args()
    output = args.output / 'licenses'
    output.mkdir(parents=True, exist_ok=True)
    roots = [args.build / '_deps', *args.deps]
    if os.environ.get('VCPKG_INSTALLATION_ROOT'):
        roots.append(Path(os.environ['VCPKG_INSTALLATION_ROOT']) / 'installed/x64-windows/share')
    for index, root in enumerate(roots):
        for path in root.rglob('*'):
            if path.is_file() and path.name.lower().startswith(('license', 'copying', 'copyright', 'notice')) and path.stat().st_size < 1024 * 1024:
                destination = output / f'build-{index}' / path.relative_to(root)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, destination)
    for package in ('PySide6-Essentials', 'shiboken6', 'PyInstaller'):
        dist = importlib.metadata.distribution(package)
        (output / (package + '-metadata.txt')).write_text(dist.read_text('METADATA') or '', encoding='utf-8')
        for name in dist.files or []:
            if 'license' in str(name).lower() or 'copying' in str(name).lower():
                source = Path(dist.locate_file(name))
                if source.is_file():
                    shutil.copy2(source, output / (package + '-' + source.name))
    shutil.copy2(ROOT / 'assets/Karla-LICENSE.txt', output)
    shutil.copy2(ROOT / 'client/thirdparty/stb_image.h', output / 'stb_image-notice-and-source.h')
    shutil.copy2(ROOT / 'docs/LAUNCHER_DEPENDENCIES.md', output)


if __name__ == '__main__':
    main()
