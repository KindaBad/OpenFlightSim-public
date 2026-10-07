#!/usr/bin/env python3
"""CI asset provisioning from a hash-pinned, explicitly approved private pack."""
import os
from pathlib import Path
import shutil
import stat
import sys
import zipfile
import argparse

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from launcher.download import download
from launcher.manifest import MAX_EXPANDED, MAX_FILES
from launcher.storage import LauncherError, read_json, safe_path, relative_name
from scripts.package_release import check_asset_approval


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--cache', type=Path, required=True)
    args = parser.parse_args()
    record = {'url': os.environ['OFS_ASSET_PACK_URL'], 'sha256': os.environ['OFS_ASSET_PACK_SHA256'], 'size': int(os.environ['OFS_ASSET_PACK_SIZE'])}
    catalog = read_json(args.catalog)
    required = {name for a in catalog['aircraft'] for name in (a['model'], *a['lods'])}
    allowed = required | {name + '.ofspack' for name in required}
    package = download(record, args.cache)
    seen = set()
    with zipfile.ZipFile(package) as archive:
        entries = archive.infolist()
        if len(entries) > MAX_FILES or sum(e.file_size for e in entries) > MAX_EXPANDED:
            raise LauncherError('Asset pack exceeds size limit')
        for entry in entries:
            name = relative_name(entry.filename)
            if name.casefold() in seen:
                raise LauncherError('Duplicate asset pack entry')
            seen.add(name.casefold())
            if name not in allowed and name != 'asset-approval.json' and not name.startswith('licenses/assets/'):
                raise LauncherError('Asset pack contains an unexpected file')
            if stat.S_IFMT(entry.external_attr >> 16) not in (0, stat.S_IFREG) or entry.is_dir() or entry.flag_bits & 1:
                raise LauncherError('Asset archive requires regular files without ZIP-level encryption')
            target = safe_path(args.root, name)
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(entry) as source, target.open('wb') as destination:
                shutil.copyfileobj(source, destination, 1024 * 1024)
    if any(name.casefold() not in seen and (name + '.ofspack').casefold() not in seen for name in required):
        raise LauncherError('Asset pack is incomplete')
    # Approval covers exact delivered bytes; encrypted assets also require
    # documented credit, reviewed rights evidence and the protected format.
    shutil.copy2(args.catalog, args.root / 'launcher-catalog.json')
    check_asset_approval(args.root, args.root / 'asset-approval.json')


if __name__ == '__main__':
    main()
