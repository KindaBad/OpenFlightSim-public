#!/usr/bin/env python3
"""Merge previous and new release manifests; never silently replace a version."""
import argparse
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from launcher.manifest import Release, index_entries, index_list
from launcher.storage import LauncherError, read_json, write_json


def merge(paths):
    releases = {}
    for path in paths:
        try:
            entries = index_entries(read_json(path))
        except LauncherError:
            raise LauncherError('Invalid update index') from None
        for entry in entries:
            release = Release.parse(entry)
            key = (entry['platform'], entry['channel'], entry['version'])
            if key in releases and releases[key] != entry:
                raise LauncherError('Conflicting release descriptors; published versions are immutable')
            releases[key] = release.data
    # Versions of four numbers go in their own list, which launchers older
    # than 0.6.9 do not read (see launcher/manifest.py).
    index = {'schema': 1, 'releases': []}
    for entry in releases.values():
        index.setdefault(index_list(entry['version']), []).append(entry)
    if any(len(entries) > 100 for key, entries in index.items() if key != 'schema'):
        raise LauncherError('Index exceeds 100 releases; archive old channels with care')
    return index


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('manifests', nargs='+', type=Path)
    args = parser.parse_args()
    write_json(args.output, merge(args.manifests))


if __name__ == '__main__':
    main()
