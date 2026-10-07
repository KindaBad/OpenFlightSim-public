"""Frozen setup's CI check: public downloads using only its bundled CA roots."""
import argparse
from functools import partial
from pathlib import Path
import ssl
import sys
import tempfile

import certifi

from .download import download, fetch_manifest, open_https
from .manifest import select_release
from .storage import read_json, sha256, write_json


def check_downloads(manifest_url, channel, report, platform=None):
    # Deliberately do not load Windows/system roots in this check. It reproduces
    # a PC with no suitable local issuer and catches omitted frozen PEM data.
    context = ssl.create_default_context(cafile=certifi.where())
    opener = partial(open_https, context=context)
    release = select_release(fetch_manifest(manifest_url, opener=opener), channel, platform)
    record = min(release.data['files'].values(), key=lambda entry: entry['size'])
    with tempfile.TemporaryDirectory() as temporary:
        downloaded = download(record, Path(temporary) / 'verified-file', opener=opener)
        write_json(report, {'version': release.version, 'certificate_verification': True,
                           'hostname_verification': context.check_hostname,
                           'bundled_ca_count': context.cert_store_stats()['x509_ca'],
                           'download_sha256': sha256(downloaded), 'download_size': record['size']})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check-downloads', type=Path, required=True)
    parser.add_argument('--manifest')
    parser.add_argument('--platform')
    args = parser.parse_args()
    if getattr(sys, 'frozen', False):
        settings = read_json(Path(sys._MEIPASS) / 'setup-config.json')
    else:
        if not args.manifest:
            parser.error('--manifest is required outside the frozen setup')
        settings = {'manifest_url': args.manifest, 'channel': 'stable', 'platform': args.platform}
    check_downloads(settings['manifest_url'], settings['channel'], args.check_downloads, settings['platform'])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
