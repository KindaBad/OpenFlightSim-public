"""Atomic state, strict JSON and portable path policy shared by the helper."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import tempfile


class LauncherError(ValueError):
    pass


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise LauncherError(f'Duplicate JSON key: {key}')
        result[key] = value
    return result


def parse_json(data):
    try:
        return json.loads(data, object_pairs_hook=_unique,
                          parse_constant=lambda s: (_ for _ in ()).throw(LauncherError(f'Invalid number: {s}')))
    except (ValueError, TypeError, UnicodeError) as exc:
        raise LauncherError(f'Invalid JSON: {exc}') from exc


def read_json(path, limit=4 * 1024 * 1024):
    with Path(path).open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise LauncherError('JSON exceeds size limit')
    return parse_json(data)


def atomic_write(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.ofs-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        if os.name != 'nt':
            fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
    finally:
        Path(temporary).unlink(missing_ok=True)


def write_json(path, data):
    atomic_write(path, (json.dumps(data, indent=2, allow_nan=False) + '\n').encode())


def relative_name(name):
    if not isinstance(name, str) or not name or len(name) > 240 or '\\' in name:
        raise LauncherError('Invalid package path')
    parts = name.split('/')
    if any(p in ('', '.', '..') or p.endswith((' ', '.')) or
           not re.fullmatch(r'[A-Za-z0-9_. +\-]+', p) or
           p.split('.')[0].upper() in {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(1, 10)), *(f'LPT{i}' for i in range(1, 10))}
           for p in parts):
        raise LauncherError(f'Unsafe package path: {name}')
    if PurePosixPath(name).is_absolute():
        raise LauncherError('Absolute package path')
    return name


def safe_path(root, name):
    relative_name(name)
    root = Path(root).resolve()
    path = root.joinpath(*name.split('/'))
    cursor = root
    for part in name.split('/'):
        cursor = cursor / part
        if cursor.is_symlink():
            raise LauncherError(f'Symlink in installation: {name}')
    if not path.resolve().is_relative_to(root):
        raise LauncherError('Path leaves installation')
    return path


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()
