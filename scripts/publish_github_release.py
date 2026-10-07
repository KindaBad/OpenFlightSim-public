#!/usr/bin/env python3
"""Publish verified flat release assets, then advance the public launcher index.

Only CI needs GitHub credentials. Never upload payload/work directories or
replace files in an already published version. A failed index upload can be
retried with the exact same version manifest.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from launcher.download import fetch_manifest
from launcher.manifest import Release
from launcher.storage import LauncherError, read_json, sha256, write_json
from scripts.merge_update_manifests import merge

INDEX_TAG = 'launcher-updates'
MAX_ASSETS = 1000
MAX_ASSET_SIZE = 2 * 1024**3


def gh(*args):
    result = subprocess.run(['gh', *map(str, args)], capture_output=True, text=True)
    if result.returncode:
        raise LauncherError(result.stderr.strip() or 'GitHub command failed')
    return result.stdout


def release_info(repo, tag):
    result = subprocess.run(['gh', 'api', f'repos/{repo}/releases/tags/{tag}'],
                            capture_output=True, text=True)
    if result.returncode:
        if '(HTTP 404)' in result.stderr:
            # The tag endpoint only returns published releases. Draft uploads
            # are visible to the authenticated releases-list endpoint instead.
            pages = json.loads(gh('api', '--paginate', '--slurp', f'repos/{repo}/releases?per_page=100'))
            matches = [release for page in pages for release in page if release['tag_name'] == tag]
            if len(matches) > 1:
                raise LauncherError('Multiple release drafts share this tag; review them before publishing')
            return matches[0] if matches else None
        raise LauncherError(result.stderr.strip() or 'Cannot read GitHub release')
    return json.loads(result.stdout)


def release_tag(entry):
    return 'v' + entry['version'] + ('-development' if entry['channel'] == 'development' else '')


def publication_files(directory, manifest, repo, tag):
    """Map exactly the manifest's public URLs to verified local files."""
    directory = Path(directory)
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo):
        raise LauncherError('Invalid GitHub repository')
    if not isinstance(manifest, dict) or manifest.get('schema') != 1 or not manifest.get('releases'):
        raise LauncherError('Empty or invalid publication manifest')
    prefix = f'https://github.com/{repo}/releases/download/{tag}/'
    files = {}
    platforms = set()
    for entry in manifest['releases']:
        Release.parse(entry)
        if tag != release_tag(entry) or entry['platform'] in platforms:
            raise LauncherError('Publication must contain one version/channel and one entry per platform')
        platforms.add(entry['platform'])
        records = [entry['package'], entry['installer'], entry['setup'], *entry['files'].values()]
        for record in records:
            if not record['url'].startswith(prefix):
                raise LauncherError('Release URL does not point to the immutable version tag')
            name = unquote(record['url'][len(prefix):])
            if not re.fullmatch(r'[A-Za-z0-9_.-]+', name) or name in ('.', '..', 'manifest.json'):
                raise LauncherError('GitHub release assets must have safe flat names')
            path = directory / ('repair' if name.startswith('file-') else '') / name
            if path.is_symlink() or not path.is_file():
                raise LauncherError(f'Missing publication file: {name}')
            if path.stat().st_size != record['size'] or sha256(path) != record['sha256']:
                raise LauncherError(f'Publication digest/size mismatch: {name}')
            files[name] = path
    for path in directory.glob('*-SHA256SUMS.txt'):
        files[path.name] = path
    files['manifest.json'] = directory / 'manifest.json'
    # Corresponding library sources can live in a persistent source release,
    # so they do not count against short-lived Actions artifact storage.
    if len(files) > MAX_ASSETS or any(path.stat().st_size >= MAX_ASSET_SIZE for path in files.values()):
        raise LauncherError('Publication exceeds GitHub release asset count/size limits')
    return files


def publish(directory, repo, commit, notes):
    directory = Path(directory).resolve()
    current = read_json(directory / 'manifest.json')
    tag = release_tag(current['releases'][0])
    files = publication_files(directory, current, repo, tag)
    repository = json.loads(gh('api', f'repos/{repo}'))
    if repository.get('private'):
        raise LauncherError('Player releases require a public repository')
    with tempfile.TemporaryDirectory() as temporary:
        temporary = Path(temporary)
        old_index = release_info(repo, INDEX_TAG)
        manifests = [directory / 'manifest.json']
        if old_index and not old_index['draft']:
            if any(a['name'] == 'manifest.json' for a in old_index['assets']):
                gh('release', 'download', INDEX_TAG, '--repo', repo, '--pattern', 'manifest.json', '--dir', temporary / 'old')
                manifests.insert(0, temporary / 'old/manifest.json')
            else:
                # A failed --clobber may have deleted the index asset. Rebuild
                # its history from immutable manifests, rather than dropping
                # older versions or needing credentials in the launcher.
                pages = json.loads(gh('api', '--paginate', '--slurp', f'repos/{repo}/releases?per_page=100'))
                for page in pages:
                    for release in page:
                        old_tag = release['tag_name']
                        if release['draft'] or not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-development)?', old_tag):
                            continue
                        if not any(a['name'] == 'manifest.json' for a in release['assets']):
                            continue
                        gh('release', 'download', old_tag, '--repo', repo, '--pattern', 'manifest.json', '--dir', temporary / old_tag)
                        manifests.insert(0, temporary / old_tag / 'manifest.json')
        merged = merge(manifests)  # Reject conflicting already announced versions before any upload.
        if len((json.dumps(merged, indent=2) + '\n').encode()) > 4 * 1024 * 1024:
            raise LauncherError('Launcher index would exceed its 4 MiB limit')
        existing = release_info(repo, tag)
        if existing and not existing['draft']:
            gh('release', 'download', tag, '--repo', repo, '--pattern', 'manifest.json', '--dir', temporary / 'version')
            if read_json(temporary / 'version/manifest.json') != current:
                raise LauncherError('Published versions are immutable; increment the CMake version')
        else:
            if not existing:
                args = ['release', 'create', tag, '--repo', repo, '--target', commit,
                        '--draft', '--title', f'OpenFlightSim {current["releases"][0]["version"]}', '--notes-file', notes]
                if current['releases'][0]['channel'] == 'development':
                    args.append('--prerelease')
                gh(*args)
            paths = list(files.values())
            for start in range(0, len(paths), 20):
                gh('release', 'upload', tag, '--repo', repo, '--clobber', *paths[start:start + 20])
            # Check the server's uploaded file list before making a version public.
            uploaded = {asset['name']: asset for asset in release_info(repo, tag)['assets']}
            if set(uploaded) != set(files):
                raise LauncherError('Draft contains missing or unexpected assets; review it before retrying')
            for name, path in files.items():
                asset = uploaded[name]
                if asset['size'] != path.stat().st_size or (
                        asset.get('digest') and asset['digest'] != 'sha256:' + sha256(path)):
                    raise LauncherError(f'GitHub asset verification failed: {name}')
            gh('release', 'edit', tag, '--repo', repo, '--draft=false', '--latest=false')
        # Download the public descriptor before advertising it to installed launchers.
        version_url = f'https://github.com/{repo}/releases/download/{tag}/manifest.json'
        if fetch_manifest(version_url) != current:
            raise LauncherError('Public version manifest verification failed')
        index_path = temporary / 'manifest.json'
        write_json(index_path, merged)
        if not old_index:
            gh('release', 'create', INDEX_TAG, '--repo', repo, '--target', commit, '--draft',
               '--title', 'OpenFlightSim launcher updates',
               '--notes', 'Launcher update index. Download the setup executable from a version release to install the game.')
        # GitHub replaces a release asset by deleting/uploading it. A brief missing
        # index leaves installed games intact; rerunning recovers a failed upload.
        gh('release', 'upload', INDEX_TAG, '--repo', repo, '--clobber', index_path)
        gh('release', 'edit', INDEX_TAG, '--repo', repo, '--draft=false', '--latest=false')
        index_url = f'https://github.com/{repo}/releases/download/{INDEX_TAG}/manifest.json'
        if fetch_manifest(index_url) != merged:
            raise LauncherError('Public launcher index verification failed; retry this publication')
    print(f'Published {tag}; launcher index: {index_url}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--repo', required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--notes', type=Path, required=True)
    args = parser.parse_args()
    publish(args.directory, args.repo, args.commit, args.notes)


if __name__ == '__main__':
    main()
