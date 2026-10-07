#!/usr/bin/env python3
"""Publish hash-pinned matching LGPL library sources outside Actions artifacts."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from launcher.download import download
from launcher.storage import LauncherError, read_json
from scripts.publish_github_release import gh, release_info


def publish(repo, commit, directory):
    settings = read_json(ROOT / 'scripts/dependency-sources.json')
    existing = release_info(repo, settings['tag'])
    if json.loads(gh('api', f'repos/{repo}')).get('private'):
        raise LauncherError('Library sources must be publicly accessible')
    if existing:
        assets = {a['name']: a for a in existing['assets']}
        complete = all(assets.get(record['name'], {}).get('size') == record['size']
                       and assets.get(record['name'], {}).get('digest') == 'sha256:' + record['sha256']
                       for record in settings['sources'])
        if complete:
            if existing['draft']:
                gh('release', 'edit', settings['tag'], '--repo', repo, '--draft=false', '--latest=false')
            print('Matching library source archives verified and published')
            return
        if not existing['draft']:
            raise LauncherError('Published source archive differs from the pinned dependency')
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    paths = [download(record, directory / record['name']) for record in settings['sources']]
    if not existing:
        gh('release', 'create', settings['tag'], '--repo', repo, '--target', commit,
           '--draft', '--prerelease', '--title', 'Qt / PySide / shiboken 6.10.2 corresponding source',
           '--notes', 'Unmodified corresponding library sources for the dynamically linked Qt/PySide launcher. '
           'License texts and copyright notices are included in these upstream source archives and the game package. '
           'The launcher source and rebuild instructions are in this repository.')
    gh('release', 'upload', settings['tag'], '--repo', repo, '--clobber', *paths)
    assets = {a['name']: a for a in release_info(repo, settings['tag'])['assets']}
    for record in settings['sources']:
        asset = assets.get(record['name'], {})
        if asset.get('size') != record['size'] or asset.get('digest') != 'sha256:' + record['sha256']:
            raise LauncherError('Uploaded library source verification failed')
    gh('release', 'edit', settings['tag'], '--repo', repo, '--draft=false', '--latest=false')
    print('Published matching library source archives')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--directory', type=Path, required=True)
    args = parser.parse_args()
    publish(args.repo, args.commit, args.directory)


if __name__ == '__main__':
    main()
