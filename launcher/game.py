"""Catalogue discovery, validated arguments and child process management."""
from dataclasses import dataclass
import ipaddress
import re
import logging
import os
from pathlib import Path
import subprocess
import time
from .config import PURSUIT_CAMERA_VERSION, LIVERIES
from .installation import active_directory
from .lan import LAN_VERSION, LIVERY_VERSION, MISSILE_RELOAD_VERSION, TEAMS_VERSION, lobby_name
from .config import LOADOUTS, TEAMS, user_directory
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
                if k in ('manufacturer', 'type', 'role', 'engine_type', 'max_speed', 'armament', 'thumbnail') and isinstance(v, str)}})
        build = read_json(catalog_root / 'build-info.json')
        if not isinstance(build, dict) or build.get('schema') != 1 or build.get('channel') not in ('stable', 'development'):
            raise LauncherError('Invalid simulator build metadata')
        if build.get('version') != catalog['version']:
            raise LauncherError('Build metadata and catalogue versions disagree')
        log.info('Discovered installation %s version %s', root, catalog['version'])
        return cls(root, directory, executable, catalog, aircraft, build, managed)

    def missing_assets(self):
        return [name for a in self.aircraft for name in aircraft_files(a) if not (safe_path(self.directory, name).is_file() or safe_path(self.directory, name + '.ofspack').is_file())]

    def server_executable(self):
        suffix = '.exe' if os.name == 'nt' else ''
        candidates = [self.directory / ('ofs_server' + suffix), self.executable.parent.parent / 'network' / ('ofs_server' + suffix), self.executable.parent.parent.parent / 'network/Release' / ('ofs_server' + suffix)]
        found = next((p for p in candidates if p.is_file()), None)
        if not found:
            raise LauncherError('Dedicated server executable is missing from this installation')
        return found


def aircraft_files(aircraft):
    """Every model file an aircraft needs: its own and those of each alternate livery."""
    files = [aircraft['model'], *aircraft['lods']]
    for livery in aircraft.get('liveries', []):
        files += [livery['model'], *livery['lods']]
    return files


def earth_theatre(installation, theatre_id):
    """The theatre of the round Earth this build lists under an id."""
    theatres = installation.catalog.get('theatres')
    if not isinstance(theatres, list) or not theatres:
        raise LauncherError('This version of the game has no Earth map. Update it, or choose the valley.')
    theatre = next((t for t in theatres if isinstance(t, dict) and t.get('id') == theatre_id), None)
    if not theatre or not isinstance(theatre.get('airports'), list) or not theatre['airports']:
        raise LauncherError('Choose a theatre of the Earth map from the list')
    return theatre


EARTH_FILES = ('earth.ofse', 'earth-cover.ofsc')


def earth_downloads(installation):
    """The Earth map's data files this build offers to download, checked, or none."""
    from .manifest import MAX_PACKAGE, digest, https_url, integer
    records = installation.catalog.get('earth_data')
    if records is None:
        return []
    if not isinstance(records, list) or sorted(r.get('name') for r in records if isinstance(r, dict)) != sorted(EARTH_FILES):
        raise LauncherError('Invalid Earth map download in the simulator catalogue')
    for record in records:
        https_url(record.get('url'))
        digest(record.get('sha256'))
        integer(record.get('size'), 1, MAX_PACKAGE, 'Earth map download size')
    return records


def earth_detail_downloads(installation):
    """The GLO-90 detail regions this build offers to download, checked, or none."""
    from .manifest import digest, https_url, integer
    records = installation.catalog.get('earth_detail')
    if records is None:
        return []
    if not isinstance(records, list) or len(records) > 1000:
        raise LauncherError('Invalid Earth detail download in the simulator catalogue')
    for record in records:
        if not isinstance(record, dict) or not isinstance(record.get('region'), str) or \
                not re.fullmatch(r'[NS][0-9]{2}[EW][0-9]{3}', record['region']) or record.get('name') != f"earth-detail-{record['region']}.ofsd":
            raise LauncherError('Invalid Earth detail download in the simulator catalogue')
        https_url(record.get('url'))
        digest(record.get('sha256'))
        integer(record.get('size'), 1, 2**31 - 1, 'Earth detail download size')
    return records


def theatre_regions(theatre):
    """The detail regions a theatre's games use (none from builds before them)."""
    regions = theatre.get('regions', [])
    if not isinstance(regions, list) or not all(isinstance(r, str) and re.fullmatch(r'[NS][0-9]{2}[EW][0-9]{3}', r) for r in regions):
        raise LauncherError('Invalid theatre regions in the simulator catalogue')
    return regions


def earth_missing(installation, theatre_id):
    """What a flight in a theatre still needs: the folder the data goes in, and
    the records of the files missing there (the terrain pack and land cover if
    they are not installed or are out of date, and the theatre's detail regions
    this build offers), each with the path it is downloaded to."""
    records = earth_downloads(installation)
    try:
        pack, _ = earth_data(installation)
        folder, base = pack.parent, []
    except LauncherError:
        folder = user_directory() / 'earth'
        base = [(r, folder / r['name']) for r in records]
    regions = set(theatre_regions(earth_theatre(installation, theatre_id))) if theatre_id else set()
    detail = []
    for record in earth_detail_downloads(installation):
        target = folder / 'detail' / record['name']
        if record['region'] in regions and not (target.is_file() and target.stat().st_size == record['size']):
            detail.append((record, target))
    return folder, base + detail


def download_earth(installation, theatre_id=None, cancel=None, progress=None, fetch=None):
    """Download what the Earth map still needs for a theatre into the user's
    data folder (or beside a pack already there), each file checked against its
    size and SHA-256, and return the folder."""
    from .download import download
    if not earth_downloads(installation):
        raise LauncherError('This version of the game has no Earth map download.')
    folder, needed = earth_missing(installation, theatre_id)
    total = sum(r['size'] for r, _ in needed)
    done = 0
    for record, target in needed:
        def report(received, size, speed, before=done):
            if progress:
                progress(before + received, total, speed)
        (fetch or download)(record, target, cancel, report)
        done += record['size']
    return folder


def earth_data(installation):
    """The Earth terrain pack and land cover: a source checkout's own build, or
    the copy downloaded beside the game or into the user's data."""
    for folder in (installation.root / 'build' / 'earth', installation.directory / 'earth', user_directory() / 'earth'):
        pack, cover = folder / 'earth.ofse', folder / 'earth-cover.ofsc'
        if pack.is_file() and cover.is_file():
            # A download kept from an older version is fetched again.
            records = {r['name']: r for r in earth_downloads(installation)}
            if folder == user_directory() / 'earth' and records and any(
                    (folder / name).stat().st_size != record['size'] for name, record in records.items()):
                raise LauncherError('The Earth map data has been updated. Download it again on the Flight mode page.')
            return pack, cover
    raise LauncherError('The Earth map data is not installed. Download it on the Flight mode page, or choose the valley.')


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
    if Version(installation.catalog['version']) >= Version(TEAMS_VERSION):
        if preferences.team not in TEAMS or preferences.loadout not in LOADOUTS:
            raise LauncherError('Choose a team and a bomb load from the lists')
        if preferences.mode == 'multiplayer' and preferences.team != 'auto':
            args += ['--team', preferences.team]
        if selected.get('bomber'):
            args += ['--loadout', preferences.loadout]
    if preferences.map == 'earth':
        if preferences.mode == 'dogfight':
            raise LauncherError('Bots fly in the valley only for now. Choose free flight or multiplayer, or the valley.')
        theatre = earth_theatre(installation, preferences.theatre)
        airports = [a.get('ident') for a in theatre['airports'] if isinstance(a, dict)]
        airport = preferences.earth_airport if preferences.earth_airport in airports else airports[0]
        if not isinstance(airport, str) or not airport.isascii() or not airport.isalnum():
            raise LauncherError('Invalid airport in the simulator catalogue')
        pack, cover = earth_data(installation)
        args += ['--theatre', theatre['id'], '--earth-airport', airport, '--earth-pack', str(pack), '--earth-cover', str(cover)]
    if preferences.livery not in LIVERIES:
        raise LauncherError('Choose a livery from the list')
    if preferences.livery == 'luftwaffe' and selected.get('liveries') and Version(installation.catalog['version']) >= Version(LIVERY_VERSION):
        args += ['--livery', 'luftwaffe']
    return args


def server_arguments(installation, preferences, lan=False):
    """Command line of the server behind a hosted game: this computer only, or the local network."""
    command = [str(installation.server_executable()), '--bind', '0.0.0.0' if lan else '127.0.0.1', '--port', str(preferences.port)]
    if preferences.map == 'earth':
        # A game on the round Earth: a theatre, free flight, on this computer's terrain pack.
        if preferences.lan_bots or preferences.lan_teams:
            raise LauncherError('A game on the Earth map is free flight for now: no bots or team game.')
        theatre = earth_theatre(installation, preferences.theatre)
        pack, _ = earth_data(installation)
        command += ['--map', 'earth', '--theatre', theatre['id'], '--earth-pack', str(pack)]
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
        if type(preferences.lan_missile_reload) is not int or not 0 <= preferences.lan_missile_reload <= 3600:
            raise LauncherError('Missile reload time must be 0–3600 seconds')
        if preferences.lan_missile_reload:
            if Version(installation.catalog['version']) < Version(MISSILE_RELOAD_VERSION):
                raise LauncherError(f'A missile reload time needs OpenFlightSim {MISSILE_RELOAD_VERSION} or newer. Update the game or set it to off.')
            command += ['--missile-reload', str(preferences.lan_missile_reload)]
        if preferences.lan_teams:
            if Version(installation.catalog['version']) < Version(TEAMS_VERSION):
                raise LauncherError(f'A team game needs OpenFlightSim {TEAMS_VERSION} or newer. Update the game or choose a free-for-all.')
            if type(preferences.lan_score_limit) is not int or not 50 <= preferences.lan_score_limit <= 5000:
                raise LauncherError('The score to win must be 50–5000 points')
            command += ['--teams', '--score-limit', str(preferences.lan_score_limit)]
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
