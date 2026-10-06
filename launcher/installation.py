"""Immutable release slots, file verification, staged repair and crash recovery."""
import logging
import os
from pathlib import Path
import shutil
import stat
import time
import zipfile
from .download import Cancelled, download
from .manifest import Release, release_metadata, platform_id, MAX_FILES, MAX_EXPANDED
from .storage import LauncherError, read_json, write_json, safe_path, sha256, parse_json, relative_name

log = logging.getLogger('ofs.installation')


class Lease:
    """One OS lease for launcher, simulator lifetime and helper mutations."""
    def __init__(self, root):
        self.path = Path(root) / '.installation.lock'
        self.stream = None

    def acquire(self, wait=0):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if self.path.is_symlink():
            raise LauncherError('Installation lease path is a symlink')
        self.stream = self.path.open('a+b')
        if self.stream.seek(0, os.SEEK_END) == 0:
            self.stream.write(b'0')
            self.stream.flush()
        self.stream.seek(0)
        deadline = time.monotonic() + wait
        while True:
            try:
                if os.name == 'nt':
                    import msvcrt
                    self.stream.seek(0)
                    msvcrt.locking(self.stream.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(self.stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                return self
            except OSError:
                if time.monotonic() >= deadline:
                    self.stream.close()
                    self.stream = None
                    raise LauncherError('Another launcher, game or updater is using this installation') from None
                time.sleep(.1)

    def close(self):
        if self.stream:
            if os.name == 'nt':
                import msvcrt
                self.stream.seek(0)
                msvcrt.locking(self.stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.stream.fileno(), fcntl.LOCK_UN)
            self.stream.close()
            self.stream = None

    def __enter__(self):
        return self.acquire()

    def __exit__(self, *args):
        self.close()


def pointer(root):
    state = read_json(Path(root) / 'current.json')
    if not isinstance(state, dict) or state.get('schema') != 1:
        raise LauncherError('Invalid installation pointer')
    for key in ('active', 'previous'):
        value = state.get(key)
        if key == 'previous' and value is None:
            continue
        if not isinstance(value, str) or not value.startswith('releases/') or len(value.split('/')) != 2:
            raise LauncherError('Invalid release slot')
        safe_path(root, value)
    return state


def active_directory(root):
    root = Path(root).resolve()
    return safe_path(root, pointer(root)['active']) if (root / 'current.json').exists() else root


def verify(directory, metadata=None, cancel=None, progress=None):
    directory = Path(directory)
    metadata = release_metadata(metadata or read_json(directory / 'release.json'))
    issues = {}
    for index, (name, record) in enumerate(metadata['files'].items()):
        if cancel and cancel.is_set():
            raise Cancelled('Verification cancelled')
        try:
            path = safe_path(directory, name)
            if not path.is_file():
                issues[name] = 'missing'
            elif path.stat().st_size != record['size']:
                issues[name] = 'size mismatch'
            elif sha256(path) != record['sha256']:
                issues[name] = 'hash mismatch'
            elif os.name != 'nt' and record['mode'] == 0o755 and not os.access(path, os.X_OK):
                issues[name] = 'not executable'
        except LauncherError:
            issues[name] = 'unsafe path'
        if progress:
            progress(index + 1, len(metadata['files']), 0)
    log.info('Verified %s files; %s issues', len(metadata['files']), len(issues))
    return issues


def exact_tree(directory, metadata):
    expected = set(metadata['files']) | {'release.json'}
    actual = set()
    for path in Path(directory).rglob('*'):
        if path.is_symlink():
            raise LauncherError('Release contains symlink')
        if path.is_file():
            actual.add(path.relative_to(directory).as_posix())
        elif not path.is_dir():
            raise LauncherError('Release contains special file')
    if actual != expected:
        raise LauncherError('Release file set differs from manifest')


def extract_package(package, destination, release, cancel=None, progress=None):
    record = release.package
    if Path(package).stat().st_size != record['size'] or sha256(package) != record['sha256']:
        raise LauncherError('Package integrity check failed before extraction')
    destination = Path(destination)
    if destination.exists():
        raise LauncherError('Extraction destination already exists')
    destination.mkdir(parents=True)
    try:
        with zipfile.ZipFile(package) as archive:
            entries = archive.infolist()
            if len(entries) > MAX_FILES + 1 or sum(e.file_size for e in entries) > MAX_EXPANDED:
                raise LauncherError('Archive exceeds safety limits')
            seen = set()
            for entry in entries:
                if cancel and cancel.is_set():
                    raise Cancelled('Extraction cancelled')
                name = relative_name(entry.filename)
                if name.casefold() in seen:
                    raise LauncherError('Duplicate archive entry')
                seen.add(name.casefold())
                mode = entry.external_attr >> 16
                if stat.S_IFMT(mode) not in (0, stat.S_IFREG) or entry.is_dir() or entry.flag_bits & 1:
                    raise LauncherError('Only unencrypted regular files are allowed in release archives')
                record = release.data['files'].get(name)
                if not record and name != 'release.json':
                    raise LauncherError('Unexpected archive file')
                expected_size = record['size'] if record else 4 * 1024 * 1024
                if entry.file_size > expected_size or (record and entry.file_size != expected_size):
                    raise LauncherError('Archive entry size differs from manifest')
                path = safe_path(destination, name)
                path.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(entry) as source, path.open('xb') as target:
                    for block in iter(lambda: source.read(1024 * 1024), b''):
                        if cancel and cancel.is_set():
                            raise Cancelled('Extraction cancelled')
                        target.write(block)
                os.chmod(path, record['mode'] if record else 0o644)
        internal = release_metadata(read_json(destination / 'release.json'))
        # Remote URLs are deliberately omitted in the installed manifest.
        stripped = {k: {field: value for field, value in v.items() if field != 'url'} for k, v in release.data['files'].items()}
        if internal['files'] != stripped or any(internal[k] != release.data[k] for k in ('version', 'channel', 'platform')):
            raise LauncherError('Package manifest does not match update manifest')
        exact_tree(destination, internal)
        if verify(destination, internal, cancel=cancel, progress=progress):
            raise LauncherError('Extracted release failed verification')
        return internal
    except BaseException:
        shutil.rmtree(destination)
        raise


def repair_stage(active, destination, release, cancel=None, progress=None):
    """Copy good files; fetch only replacements. Never modify the active slot."""
    local = release_metadata(read_json(Path(active) / 'release.json'))
    if any(local[k] != release.data[k] for k in ('version', 'platform', 'channel')):
        raise LauncherError('Repair requires the exact installed version and channel; use Update for a newer release')
    stripped = {k: {f: v for f, v in r.items() if f != 'url'} for k, r in release.data['files'].items()}
    if local['files'] != stripped:
        raise LauncherError('Published repair file list differs from installed manifest')
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    issues = verify(active, local, cancel)
    for index, (name, record) in enumerate(release.data['files'].items()):
        if cancel and cancel.is_set():
            raise Cancelled('Repair cancelled; active release is unchanged')
        target = safe_path(destination, name)
        target.parent.mkdir(parents=True, exist_ok=True)
        if name in issues:
            download(record, target, cancel, progress)
        else:
            shutil.copy2(safe_path(active, name), target)
        os.chmod(target, record['mode'])
        if progress:
            progress(index + 1, len(local['files']), 0)
    write_json(destination / 'release.json', local)
    exact_tree(destination, local)
    if verify(destination, local, cancel):
        raise LauncherError('Repaired release failed verification')
    return local


def recover(root):
    """Caller holds lease. A crash commits either old or fully verified new pointer."""
    root = Path(root)
    journal = root / '.transaction.json'
    if not journal.exists():
        return
    data = read_json(journal)
    old = data.get('old')
    new = data.get('new')
    # Validate both pointer structures without trusting paths in the journal.
    if not isinstance(old, dict) or not isinstance(new, dict):
        raise LauncherError('Invalid update recovery journal')
    for state in (old, new):
        for key in ('active', 'previous'):
            name = state.get(key)
            if name is None and key == 'previous':
                continue
            if not isinstance(name, str) or not name.startswith('releases/') or len(name.split('/')) != 2:
                raise LauncherError('Invalid recovery slot')
            safe_path(root, name)
    candidate = safe_path(root, new['active'])
    try:
        if verify(candidate):
            raise LauncherError('Candidate incomplete')
        write_json(root / 'current.json', new)
        log.info('Recovered verified new release')
    except (OSError, LauncherError):
        write_json(root / 'current.json', old)
        log.warning('Recovered previous release')
    journal.unlink()


def activate(root, stage, slot):
    root = Path(root).resolve()
    stage = Path(stage).resolve()
    if not stage.is_relative_to(root / '.staging') or stage == root / '.staging':
        raise LauncherError('Candidate is outside staging area')
    metadata = release_metadata(read_json(stage / 'release.json'))
    if metadata['platform'] != platform_id():
        raise LauncherError('Release platform mismatch')
    exact_tree(stage, metadata)
    if verify(stage, metadata):
        raise LauncherError('Candidate verification failed; old release retained')
    old = pointer(root) if (root / 'current.json').exists() else None
    target = safe_path(root, slot)
    if not slot.startswith('releases/') or len(slot.split('/')) != 2 or target.exists():
        raise LauncherError('Invalid/new release slot required')
    target.parent.mkdir(parents=True, exist_ok=True)
    # Rename a complete tree on the same volume. Active files are never overwritten.
    os.replace(stage, target)
    new = {'schema': 1, 'active': slot, 'previous': old['active'] if old else None}
    # On first install an absent pointer is the previous state. The atomic pointer
    # write alone commits it: interruption leaves either no install or a complete
    # verified slot. Existing installs retain their recovery/rollback journal.
    if old:
        write_json(root / '.transaction.json', {'old': old, 'new': new})
    write_json(root / 'current.json', new)
    (root / '.transaction.json').unlink(missing_ok=True)
    log.info('Activated %s; previous release retained', slot)


def prune(root):
    """Caller holds lease. Drop superseded slots, consumed packages and stale stages."""
    root = Path(root).resolve()
    state = pointer(root)
    keep = {state['active'], state['previous']}
    for parent, retained in (('releases', keep), ('.staging', set()), ('.downloads', set())):
        directory = root / parent
        if directory.is_symlink() or not directory.is_dir():
            continue
        for entry in directory.iterdir():
            if f'{parent}/{entry.name}' in retained:
                continue
            try:
                if entry.is_dir() and not entry.is_symlink():
                    shutil.rmtree(entry)
                else:
                    entry.unlink()
                log.info('Pruned %s/%s', parent, entry.name)
            except OSError as exc:
                # Reclaiming space is best effort; the activated release is unaffected.
                log.warning('Could not prune %s/%s: %s', parent, entry.name, exc)


def rollback(root):
    root = Path(root)
    old = pointer(root)
    if not old['previous']:
        raise LauncherError('No previous release is available')
    candidate = safe_path(root, old['previous'])
    if verify(candidate):
        raise LauncherError('Previous release is damaged; rollback refused')
    new = {'schema': 1, 'active': old['previous'], 'previous': old['active']}
    write_json(root / '.transaction.json', {'old': old, 'new': new})
    write_json(root / 'current.json', new)
    (root / '.transaction.json').unlink()
    log.info('Rolled back to %s', new['active'])
