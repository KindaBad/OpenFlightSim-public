#!/usr/bin/env python3
"""Describe the Earth map's data for players and the public server.

The terrain pack and land cover (scripts/build_earth_terrain.py) are too large
for the game's own download, so they are published as files of a GitHub
pre-release on the public repository and fetched on their own: by the launcher
when a player asks for the Earth map, and by the public server's build service.
So are the GLO-90 detail regions (build/earth/detail/earth-detail-*.ofsd), listed
under `detail`; the launcher fetches a theatre's regions and the server all it
plays on.
Both check each file against the SHA-256 and size in data/earth/earth-data.json,
which this writes. The release workflow passes the same values into the build.

  describe  write data/earth/earth-data.json for files about to be published
  upload    publish them as a pre-release with gh (the owner's go-ahead first)

Standard library only, apart from gh for uploading.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DESCRIPTOR = ROOT / 'data' / 'earth' / 'earth-data.json'
REPOSITORY = 'KindaBad/OpenFlightSim-public'
FILES = ('earth.ofse', 'earth-cover.ofsc')
LIMIT = 2 * 1024**3 - 1  # GitHub's limit on one release file
ASSETS = 1000            # and on the files of one release
REGION = re.compile(r'earth-detail-([NS][0-9]{2}[EW][0-9]{3})\.ofsd')


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def record(path, tag, repository):
    size = path.stat().st_size
    if size > LIMIT:
        raise SystemExit(f'{path} is over GitHub\'s 2 GiB limit for a release file; split it first')
    return dict(name=path.name, url=f'https://github.com/{repository}/releases/download/{tag}/{path.name}',
                sha256=sha256(path), size=size)


def describe(folder, tag, repository):
    files = []
    for name in FILES:
        files.append(record(Path(folder) / name, tag, repository))
        print(f'{name}: {files[-1]["size"] / 2**20:.1f} MiB')
    detail = []
    for path in sorted((Path(folder) / 'detail').glob('earth-detail-*.ofsd')):
        match = REGION.fullmatch(path.name)
        if not match:
            raise SystemExit(f'{path.name} is not a detail region pack')
        detail.append(dict(region=match[1], **record(path, tag, repository)))
    if len(files) + len(detail) > ASSETS:
        raise SystemExit('Too many files for one GitHub release')
    if detail:
        print(f'{len(detail)} detail regions: {sum(r["size"] for r in detail) / 2**20:.1f} MiB')
    DESCRIPTOR.parent.mkdir(parents=True, exist_ok=True)
    DESCRIPTOR.write_text(json.dumps(dict(schema=1, release=tag, files=files, detail=detail), indent=2) + '\n')
    print(f'Wrote {DESCRIPTOR.relative_to(ROOT)}')


def upload(folder, repository):
    data = json.loads(DESCRIPTOR.read_text())
    tag = data['release']
    paths = [Path(folder) / r['name'] for r in data['files']] + [Path(folder) / 'detail' / r['name'] for r in data.get('detail', [])]
    for path, described in zip(paths, data['files'] + data.get('detail', [])):
        if path.stat().st_size != described['size'] or sha256(path) != described['sha256']:
            raise SystemExit(f'{path} is not the file described; run describe again')
    notes = ('Data for the Earth map: the terrain pack, the land cover and the GLO-90 detail regions. The launcher '
             'downloads these when a player chooses the Earth map, and checks each against the SHA-256 in '
             'data/earth/earth-data.json. Detail regions are produced using Copernicus WorldDEM-90 (c) DLR e.V. '
             '2010-2014 and (c) Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union '
             'and ESA; all rights reserved. The organisations in charge of the Copernicus programme by law or by '
             'delegation do not incur any liability for any use of the Copernicus WorldDEM-90.')
    # A release is created with the first files and the rest are added, so a
    # long list never runs past the command line's length.
    subprocess.run(['gh', 'release', 'create', tag, '--repo', repository, '--prerelease', '--title', f'Earth map data ({tag})',
                    '--notes', notes, *[str(p) for p in paths[:20]]], check=True)
    for start in range(20, len(paths), 20):
        subprocess.run(['gh', 'release', 'upload', tag, '--repo', repository, *[str(p) for p in paths[start:start + 20]]], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('describe')
    p.add_argument('--folder', type=Path, default=ROOT / 'build' / 'earth')
    p.add_argument('--tag', required=True, help='the pre-release to publish the files under, e.g. earth-data-v2')
    p.add_argument('--repository', default=REPOSITORY)
    p = sub.add_parser('upload')
    p.add_argument('--folder', type=Path, default=ROOT / 'build' / 'earth')
    p.add_argument('--repository', default=REPOSITORY)
    options = parser.parse_args()
    if options.command == 'describe':
        describe(options.folder, options.tag, options.repository)
    else:
        upload(options.folder, options.repository)


if __name__ == '__main__':
    main()
