"""Bounded TLS downloads, strict redirects, retryable partials and final hashes."""
import logging
from pathlib import Path
import re
import threading
import time
from urllib.error import HTTPError
from urllib.request import HTTPRedirectHandler, HTTPSHandler, Request, build_opener
import ssl
import certifi
from .manifest import https_url, digest, integer, MAX_PACKAGE
from .storage import LauncherError, parse_json, sha256

log = logging.getLogger('ofs.download')


class Cancelled(LauncherError):
    pass


class SecureRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        https_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def https_context():
    # Frozen Python has no portable CA bundle of its own. Seed trust with
    # Mozilla's roots, then retain OS roots (including managed Windows roots).
    # Passing cafile keeps hostname and certificate verification enabled.
    context = ssl.create_default_context(cafile=certifi.where())
    context.load_default_certs()
    return context


def open_https(url, headers=None, *, context=None):
    https_url(url)
    opener = build_opener(HTTPSHandler(context=context or https_context()), SecureRedirect())
    return opener.open(Request(url, headers={'User-Agent': 'OpenFlightSim-Launcher/1', 'Accept-Encoding': 'identity', **(headers or {})}), timeout=20)


def fetch_manifest(url, *, opener=None):
    with (opener or open_https)(url) as response:
        data = response.read(4 * 1024 * 1024 + 1)
    if len(data) > 4 * 1024 * 1024:
        raise LauncherError('Update manifest exceeds 4 MiB')
    log.info('Fetched HTTPS update manifest')
    return parse_json(data)


def download(record, destination, cancel=None, progress=None, opener=open_https):
    https_url(record.get('url'))
    expected_hash = digest(record.get('sha256'))
    size = integer(record.get('size'), 0, MAX_PACKAGE, 'download size')
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_symlink():
        raise LauncherError('Download target is a symlink')
    if destination.exists() and destination.stat().st_size == size and sha256(destination) == expected_hash:
        return destination
    cancel = cancel or threading.Event()
    offset = destination.stat().st_size if destination.exists() else 0
    if offset >= size:
        destination.unlink(missing_ok=True)
        offset = 0
    headers = {'Range': f'bytes={offset}-'} if offset else {}
    try:
        response = opener(record['url'], headers)
    except HTTPError as exc:
        if exc.code == 416 and offset:
            destination.unlink(missing_ok=True)
        raise LauncherError(f'Download request failed (HTTP {exc.code}); retry is available') from None
    started = time.monotonic()
    received = 0
    with response:
        status = response.status
        if status == 206:
            match = re.fullmatch(r'bytes (\d+)-(\d+)/(\d+)', response.headers.get('Content-Range', ''))
            if not match or tuple(map(int, match.groups())) != (offset, size - 1, size):
                raise LauncherError('Invalid resumed download range')
        elif status == 200:
            offset = 0  # Host ignored Range. Restart rather than append.
        else:
            raise LauncherError(f'Unexpected download status: {status}')
        content_length = response.headers.get('Content-Length')
        if content_length and int(content_length) != size - offset:
            raise LauncherError('Download size differs from manifest')
        with destination.open('ab' if offset else 'wb') as stream:
            while True:
                if cancel.is_set():
                    raise Cancelled('Download cancelled; retry will resume the partial file')
                block = response.read(256 * 1024)
                if not block:
                    break
                received += len(block)
                if offset + received > size:
                    stream.close()
                    destination.unlink(missing_ok=True)
                    raise LauncherError('Download exceeds expected size')
                stream.write(block)
                if progress:
                    progress(offset + received, size, received / max(.001, time.monotonic() - started))
    if destination.stat().st_size != size:
        raise LauncherError('Download interrupted; retry will resume')
    if sha256(destination) != expected_hash:
        destination.unlink(missing_ok=True)
        log.error('SHA-256 verification rejected download')
        raise LauncherError('Download hash mismatch; discarded unsafe file')
    log.info('Verified download: %s bytes, SHA-256 %s', size, expected_hash)
    return destination
