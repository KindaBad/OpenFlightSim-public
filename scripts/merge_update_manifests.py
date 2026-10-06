#!/usr/bin/env python3
"""Merge previous and new release manifests; never silently replace a version."""
import argparse
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from launcher.manifest import Release
from launcher.storage import LauncherError, read_json, write_json


def merge(paths):
    releases = {}
    for path in paths:
        data = read_json(path)
        if not isinstance(data, dict) or data.get('schema') != 1 or not isinstance(data.get('releases'), list):
            raise LauncherError('Invalid update index')
        for entry in data['releases']:
            release = Release.parse(entry)
            key = (entry['platform'], entry['channel'], entry['version'])
            if key in releases and releases[key] != entry:
                raise LauncherError('Conflicting release descriptors; published versions are immutable')
            releases[key] = release.data
    if len(releases) > 100:
        raise LauncherError('Index exceeds 100 releases; archive old channels with care')
    return {'schema': 1, 'releases': list(releases.values())}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('manifests', nargs='+', type=Path)
    args = parser.parse_args()
    write_json(args.output, merge(args.manifests))


if __name__ == '__main__':
    main()
