#!/usr/bin/env python3
"""Keep the public host's game servers in step with the published game. Standard library only.

Looks at the public source repository's main branch and, when the game there
is a version this computer has no server for (or its server code has changed),
builds `ofs_server` and puts it in builds/<version>/. The source is built as
soon as it is pushed, which is before the players' update finishes publishing,
so a server is waiting when they have it. The newest few versions are kept,
for players who have not updated yet.

With --source, builds a source tree already on this computer instead.

It also keeps the server's copy of the Earth map's data (the terrain pack that
games on the Earth are played on) in step with the one data/earth/earth-data.json
in the source describes, fetching it from its GitHub pre-release and checking
its SHA-256 and size.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import urllib.request

REPOSITORY = 'https://github.com/KindaBad/OpenFlightSim-public.git'
FIRST_VERSION = (0, 6, 3)
VERSION = re.compile(r'project\(OpenFlightSim VERSION (\d{1,9}(?:\.\d{1,9}){2,3}) ')


def run(*command, cwd=None, capture=False):
    result = subprocess.run(command, cwd=cwd, check=True, text=True, stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else None


def version_key(text):
    return tuple(int(part) for part in text.split('.'))


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def earth_data(source, folder):
    """Fetch the Earth map's data the source describes into `folder`, if it is not there yet."""
    descriptor = Path(source) / 'data' / 'earth' / 'earth-data.json'
    if not descriptor.is_file():
        return
    data = json.loads(descriptor.read_text(encoding='utf-8'))
    folder.mkdir(parents=True, exist_ok=True)
    # The pack and land cover, then every GLO-90 detail region into detail/
    # beside the pack, where the server looks for a theatre's.
    records = [(r, folder) for r in data.get('files', [])] + [(r, folder / 'detail') for r in data.get('detail', [])]
    for record, place in records:
        name, url, digest, size = record['name'], record['url'], record['sha256'], record['size']
        known = name in ('earth.ofse', 'earth-cover.ofsc') if place == folder else \
            re.fullmatch(r'earth-detail-[NS][0-9]{2}[EW][0-9]{3}\.ofsd', name) and name == f"earth-detail-{record.get('region')}.ofsd"
        if not known or not url.startswith('https://github.com/'):
            raise SystemExit(f'Unexpected Earth data record {name}')
        place.mkdir(parents=True, exist_ok=True)
        target = place / name
        if target.is_file() and target.stat().st_size == size and sha256(target) == digest:
            continue
        partial = place / (name + '.part')
        with urllib.request.urlopen(url, timeout=60) as response, open(partial, 'wb') as stream:
            shutil.copyfileobj(response, stream, 1 << 20)
        if partial.stat().st_size != size or sha256(partial) != digest:
            partial.unlink()
            raise SystemExit(f'Earth data {name} did not match its SHA-256; discarded')
        # Games already running keep the pack they opened.
        os.replace(partial, target)
        print(f'Fetched Earth data {name} ({size / 2**20:.0f} MiB)', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--root', required=True, help='folder holding builds/ and work/')
    parser.add_argument('--source', help='build this source tree instead of the public repository')
    parser.add_argument('--repository', default=REPOSITORY)
    parser.add_argument('--keep', type=int, default=3, help='versions to keep')
    parser.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    options = parser.parse_args()
    root = Path(options.root).resolve()
    builds, work = root / 'builds', root / 'work'
    builds.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    if options.source:
        source, fingerprint = Path(options.source).resolve(), 'local'
    else:
        head = run('git', 'ls-remote', options.repository, 'refs/heads/main', capture=True).split()[0]
        seen = work / 'head'
        if seen.is_file() and seen.read_text().strip() == head:
            return
        source = work / 'src'
        if not (source / '.git').is_dir():
            run('git', 'init', '--quiet', str(source))
        run('git', 'fetch', '--quiet', '--depth', '1', options.repository, head, cwd=source)
        run('git', 'checkout', '--quiet', '--force', '--detach', 'FETCH_HEAD', cwd=source)
        # What the server is made of; a change elsewhere needs no rebuild.
        fingerprint = run('git', 'rev-parse', 'HEAD:core', 'HEAD:network', 'HEAD:cmake', 'HEAD:CMakeLists.txt', cwd=source, capture=True)
    match = VERSION.search((source / 'CMakeLists.txt').read_text(encoding='utf-8'))
    if not match:
        raise SystemExit('No game version found in CMakeLists.txt')
    version = match[1]
    if version_key(version) < FIRST_VERSION:
        # Servers before this cannot be run several to a computer by the host.
        if not options.source:
            (work / 'head').write_text(head)
        return
    target = builds / version
    record = target / 'source'
    if not (options.source is None and record.is_file() and record.read_text() == fingerprint and (target / 'ofs_server').is_file()):
        tree = work / ('build-local' if options.source else 'build')
        configure = ('cmake', '-S', str(source), '-B', str(tree), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                     '-DOFS_BUILD_CLIENT=OFF', '-DOFS_BUILD_TESTS=OFF')
        try:
            run(*configure)
        except subprocess.CalledProcessError:
            # A build folder left by something else, or by an older CMake: start it again.
            shutil.rmtree(tree, ignore_errors=True)
            run(*configure)
        run('cmake', '--build', str(tree), '--target', 'ofs_server', '--parallel', str(options.jobs))
        target.mkdir(exist_ok=True)
        # Games already running keep the file they started from; new ones get this.
        fresh = target / 'ofs_server.new'
        shutil.copy2(tree / 'network' / 'ofs_server', fresh)
        os.replace(fresh, target / 'ofs_server')
        record.write_text(fingerprint)
        print(f'Built ofs_server {version}', flush=True)
    earth_data(source, root / 'earth')
    if not options.source:
        (work / 'head').write_text(head)
    versions = sorted((entry.name for entry in builds.iterdir() if re.fullmatch(r'\d{1,9}(?:\.\d{1,9}){2,3}', entry.name)), key=version_key)
    for old in versions[:-options.keep]:
        shutil.rmtree(builds / old)
        print(f'Removed ofs_server {old}', flush=True)


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as exc:
        sys.exit(f'{exc.cmd[0]} failed with {exc.returncode}')
