"""First-run installation engine for the standalone, frozen setup application."""
import os
from pathlib import Path
import shutil
import threading
import uuid

from .download import Cancelled, download, fetch_manifest
from .game import Installation
from .installation import Lease, activate, extract_package, prune, recover, verify
from .manifest import https_url, platform_id, select_release
from .storage import LauncherError, atomic_write, read_json, safe_path
from .version import Version


def default_directory(channel='stable'):
    name = 'OpenFlightSim' if channel == 'stable' else 'OpenFlightSim-Development'
    if os.name == 'nt':
        return Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local')) / 'Programs' / name
    return Path.home() / '.local/share' / name


def configuration(data):
    if not isinstance(data, dict) or data.get('schema') != 1:
        raise LauncherError('Invalid setup configuration')
    https_url(data.get('manifest_url'))
    if data.get('channel') not in ('stable', 'development'):
        raise LauncherError('Invalid setup release channel')
    Version(data.get('version'))
    if data.get('platform') != platform_id():
        raise LauncherError('This launcher download is for a different operating system')
    return data


def bootstrap_name():
    return 'OpenFlightSim' + ('.exe' if os.name == 'nt' else '')


def install(root, settings, bootstrap, cancel=None, progress=None, status=None):
    """Download, verify and atomically commit a complete per-user installation.

    Nothing downloaded is executed here. The caller starts the stable bootstrap
    after the installation lease is released. Partials survive failure for retry.
    """
    settings = configuration(settings)
    root = Path(root).expanduser().resolve()
    bootstrap = Path(bootstrap)
    cancel = cancel or threading.Event()
    status = status or (lambda message: None)

    def checkpoint():
        if cancel.is_set():
            raise Cancelled('Installation cancelled. Run the launcher again to resume.')

    if not bootstrap.is_file():
        raise LauncherError('The downloaded launcher is incomplete; download it again')
    checkpoint()
    with Lease(root):
        recover(root)
        target = root / bootstrap_name()
        if target.is_symlink() or (target.exists() and not target.is_file()):
            raise LauncherError('Invalid installed launcher file')
        if (root / 'current.json').exists():
            status('Checking the installed game…')
            installed = Installation.discover(root)
            if installed.build['channel'] != settings['channel']:
                raise LauncherError('Choose a separate folder for this release channel')
            if verify(installed.directory, cancel=cancel, progress=progress) or installed.missing_assets():
                raise LauncherError('The installed game needs repair. Open OpenFlightSim in the Start Menu and use Installation / Repair.')
            checkpoint()
            if not target.exists():
                atomic_write(target, bootstrap.read_bytes())
                target.chmod(0o755)
            return root
        # Never treat a loose developer/distribution folder as a fresh install.
        if (root / 'CMakeLists.txt').exists() or (root / ('ofs_client' + ('.exe' if os.name == 'nt' else ''))).exists():
            raise LauncherError('Choose a new folder for the game installation')
        status('Finding the latest game release…')
        release = select_release(fetch_manifest(settings['manifest_url']), settings['channel'])
        if Version(settings['version']) < Version(release.data['minimum_launcher_version']):
            raise LauncherError('Download the latest launcher to install this game release')
        checkpoint()
        required = release.package['size'] + sum(r['size'] for r in release.data['files'].values()) + bootstrap.stat().st_size + 256 * 1024**2
        if shutil.disk_usage(root).free < required:
            raise LauncherError(f'Not enough free space. This install needs {required / 1024**3:.1f} GiB; choose another folder or free space and retry.')
        archive = safe_path(root, '.downloads/' + release.package['sha256'] + '.zip')
        status(f'Downloading OpenFlightSim {release.version}…')
        download(release.package, archive, cancel=cancel, progress=progress)
        checkpoint()
        stage = safe_path(root, '.staging/' + uuid.uuid4().hex)
        try:
            status('Unpacking and verifying the game…')
            extract_package(archive, stage, release, cancel=cancel, progress=progress)
            candidate = Installation.discover(stage)
            if candidate.missing_assets():
                raise LauncherError('The published game package is missing aircraft models')
            publisher = stage / 'publisher.json'
            if not publisher.is_file() or read_json(publisher).get('manifest_url') != settings['manifest_url']:
                raise LauncherError('Game package and launcher update addresses disagree')
            checkpoint()
            status('Finishing installation…')
            # The stable updater is embedded in the trusted setup distribution.
            # Copy it out of the temporary onefile bundle, never link a shortcut
            # to Downloads or the bundle's temporary extraction directory.
            if not target.exists():
                atomic_write(target, bootstrap.read_bytes())
                target.chmod(0o755)
            activate(root, stage, f'releases/{release.version}-{uuid.uuid4().hex[:12]}')
            prune(root)
            return root
        finally:
            if stage.exists():
                shutil.rmtree(stage)
