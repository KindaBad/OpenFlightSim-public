"""Catalogue discovery, validated arguments and child process management."""
from dataclasses import dataclass
import ipaddress
import logging
import os
from pathlib import Path
import subprocess
import time
from .config import PURSUIT_CAMERA_VERSION
from .installation import active_directory
from .lan import LAN_VERSION, lobby_name
from .platform_process import spawn
from .storage import LauncherError, read_json, safe_path, relative_name
from .version import Version

log = logging.getLogger('ofs.game')
MODES = {'free': 'Free Flight', 'multiplayer': 'Multiplayer', 'dogfight': 'Local Dogfight'}


@dataclass
class Installation:
    root: Path
    directory: Path
    executable: Path
    catalog: dict
    aircraft: list
    build: dict
    managed: bool

    @classmethod
    def discover(cls, root):
        root = Path(root).expanduser().resolve()
        managed = (root / 'current.json').exists()
        directory = active_directory(root)
        suffix = '.exe' if os.name == 'nt' else ''
        candidates = [(directory / ('ofs_client' + suffix), directory)]
        if not managed and (root / 'CMakeLists.txt').exists():
            for build in ('release', 'debug', 'offline-client', 'msvc'):
                base = root / 'build' / build
                for executable in (base / 'client' / ('ofs_client' + suffix), base / 'client/Release' / ('ofs_client' + suffix)):
                    candidates.append((executable, base))
        selected = next(((exe, cat) for exe, cat in candidates if exe.is_file() and (cat / 'launcher-catalog.json').is_file()), None)
        if not selected:
            raise LauncherError('Simulator executable/catalogue not found. Select a packaged installation or build ofs_client and ofs_launcher_catalog.')
        executable, catalog_root = selected
        catalog = read_json(catalog_root / 'launcher-catalog.json')
        if not isinstance(catalog, dict) or catalog.get('schema') != 1 or not isinstance(catalog.get('aircraft'), list) or not catalog['aircraft']:
            raise LauncherError('Invalid simulator aircraft catalogue')
        Version(catalog.get('version'))
        if not isinstance(catalog.get('modes'), list) or 'free' not in catalog['modes'] or any(not isinstance(mode, str) or mode not in MODES for mode in catalog['modes']):
            raise LauncherError('Invalid simulator modes')
        info_path = directory / 'data/launcher/aircraft-info.json'
        info = read_json(info_path) if info_path.is_file() else {}
        if not isinstance(info, dict):
            raise LauncherError('Invalid aircraft presentation file')
        aircraft = []
        seen = set()
        for entry in catalog['aircraft']:
            if not isinstance(entry, dict) or not isinstance(entry.get('id'), str) or entry['id'] in seen:
                raise LauncherError('Invalid/duplicate aircraft')
            if not entry['id'].isascii() or not entry['id'].isalnum() or type(entry.get('armed')) is not bool:
                raise LauncherError('Invalid aircraft identity/capabilities')
            seen.add(entry['id'])
            if not isinstance(entry.get('name'), str) or not isinstance(entry.get('model'), str) or not isinstance(entry.get('lods'), list):
                raise LauncherError('Invalid aircraft metadata')
            for name in (entry['model'], *entry['lods']):
                relative_name(name)
            presentation = info.get(entry['id'], {})
            if not isinstance(presentation, dict):
                raise LauncherError('Invalid optional aircraft presentation metadata')
            aircraft.append({**entry, **{k: v for k, v in presentation.items()
                if k in ('manufacturer', 'type', 'role', 'engine_type', 'thumbnail') and isinstance(v, str)}})
        build = read_json(catalog_root / 'build-info.json')
        if not isinstance(build, dict) or build.get('schema') != 1 or build.get('channel') not in ('stable', 'development'):
            raise LauncherError('Invalid simulator build metadata')
        if build.get('version') != catalog['version']:
            raise LauncherError('Build metadata and catalogue versions disagree')
        log.info('Discovered installation %s version %s', root, catalog['version'])
        return cls(root, directory, executable, catalog, aircraft, build, managed)

    def missing_assets(self):
        return [name for a in self.aircraft for name in (a['model'], *a['lods']) if not (safe_path(self.directory, name).is_file() or safe_path(self.directory, name + '.ofspack').is_file())]

    def server_executable(self):
        suffix = '.exe' if os.name == 'nt' else ''
        candidates = [self.directory / ('ofs_server' + suffix), self.executable.parent.parent / 'network' / ('ofs_server' + suffix), self.executable.parent.parent.parent / 'network/Release' / ('ofs_server' + suffix)]
        found = next((p for p in candidates if p.is_file()), None)
        if not found:
            raise LauncherError('Dedicated server executable is missing from this installation')
        return found


def arguments(installation, preferences, graphics):
    selected = next((a for a in installation.aircraft if a['id'] == preferences.aircraft), None)
    if not selected:
        raise LauncherError('Selected aircraft is unavailable')
    if preferences.mode not in installation.catalog['modes']:
        raise LauncherError('This simulator build does not support the selected mode')
    graphics.validate()
    camera = preferences.camera
    if camera == 'pursuit' and Version(installation.catalog['version']) < Version(PURSUIT_CAMERA_VERSION):
        camera = 'chase'  # an older build, kept or rolled back to, has no pursuit camera
    args = ['--aircraft', preferences.aircraft, '--config', str(graphics.path.resolve()), '--camera', camera]
    if preferences.airborne:
        args += ['--airborne']
    if preferences.mode == 'dogfight':
        if not selected['armed']:
            raise LauncherError('Local Dogfight requires an armed aircraft')
        if type(preferences.bots) is not int or not 1 <= preferences.bots <= 8:
            raise LauncherError('Choose 1–8 bots')
        args += ['--bots', str(preferences.bots)]
    elif preferences.mode == 'multiplayer':
        address = '127.0.0.1' if preferences.host else preferences.server.strip()
        try:
            ipaddress.ip_address(address)
        except ValueError as exc:
            raise LauncherError('The current networking transport requires a numeric IPv4 or IPv6 address') from exc
        if type(preferences.port) is not int or not 1 <= preferences.port <= 65535:
            raise LauncherError('Server port must be 1–65535')
        if not preferences.name.strip() or len(preferences.name) > 64 or any(ord(c) < 32 or ord(c) > 126 for c in preferences.name):
            raise LauncherError('Pilot name must be 1–64 printable ASCII characters')
        args += ['--server', address, '--port', str(preferences.port), '--name', preferences.name]
    return args


def server_arguments(installation, preferences, lan=False):
    """Command line of the server behind a hosted game: this computer only, or the local network."""
    command = [str(installation.server_executable()), '--bind', '0.0.0.0' if lan else '127.0.0.1', '--port', str(preferences.port)]
    if lan:
        if Version(installation.catalog['version']) < Version(LAN_VERSION):
            raise LauncherError(f'Hosting on the local network needs OpenFlightSim {LAN_VERSION} or newer. Update the game first.')
        if type(preferences.lan_bots) is not int or not 0 <= preferences.lan_bots <= 8:
            raise LauncherError('Choose 0–8 bots')
        # The name is the only text of the player's that reaches the server's
        # command line; lobby_name leaves printable ASCII of bounded length.
        command += ['--lan-name', lobby_name(preferences.lobby, lobby_name(preferences.name + "'s game"))]
        if preferences.lan_bots:
            command += ['--bots', str(preferences.lan_bots)]
    return command


class Session:
    def __init__(self):
        self.client = None
        self.server = None
        self.streams = []

    def running(self):
        return self.client is not None and self.client.poll() is None

    def start(self, installation, preferences, graphics, logs, runtime, lan=False):
        """Start a flight. With `lan`, a hosted game is opened to the local network."""
        if self.running():
            raise LauncherError('The simulator is already running')
        missing = installation.missing_assets()
        if missing:
            raise LauncherError('Required aircraft assets are missing: ' + ', '.join(missing[:5]) + '. Verify/repair the installation or provision the local asset pack.')
        args = arguments(installation, preferences, graphics)
        graphics.save()
        Path(runtime).mkdir(parents=True, exist_ok=True)
        try:
            if preferences.mode == 'multiplayer' and preferences.host:
                stream = open(Path(logs) / 'server.log', 'ab')
                self.streams.append(stream)
                server_args = server_arguments(installation, preferences, lan)
                self.server = spawn(server_args, cwd=runtime, stdout=stream, stderr=subprocess.STDOUT)
                # A busy port or missing library ends the server at once; report that
                # instead of letting the client time out against nothing.
                try:
                    self.server.wait(timeout=.5)
                    raise LauncherError(f'Local server exited at startup (port {preferences.port} may be in use); see server.log')
                except subprocess.TimeoutExpired:
                    pass
                log.info('Started %s server on port %s', 'LAN' if lan else 'local', preferences.port)
            stream = open(Path(logs) / 'simulator.log', 'ab')
            self.streams.append(stream)
            # Player names are personal data; log the launch options with name redacted.
            logged = list(args)
            if '--name' in logged:
                logged[logged.index('--name') + 1] = '[pilot]'
            log.info('Launching %s %s', installation.executable, logged)
            # A build-tree simulator sits outside its asset root; it finds models
            # through the working directory. Packaged releases keep assets beside it.
            directory = runtime if installation.managed else installation.directory
            self.client = spawn([str(installation.executable), *args], cwd=directory, stdout=stream, stderr=subprocess.STDOUT)
        except (OSError, LauncherError) as exc:
            self.cleanup()
            raise LauncherError(f'Could not start simulator: {exc}') from exc

    def cleanup(self):
        if self.server and self.server.poll() is None:
            self.server.terminate()
            try:
                self.server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.server.kill()
                self.server.wait(timeout=5)
        self.server = None
        for stream in self.streams:
            stream.close()
        self.streams.clear()
