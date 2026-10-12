"""Public games: played over the internet on the OpenFlightSim public server. No Qt.

The public server runs each game and relays its traffic through one UDP port
(hosting/public_host.py, which this mirrors). The launcher asks it which games
are running, asks it to start one, and tells it which game this computer is
about to join; the simulator then connects to that one address and port like
any other game. What the server says is only ever shown in a list or used as
the address of a game, and joining goes through the simulator's own checks.

Where the server is comes from a small file fetched over HTTPS, because a home
connection's address changes; the last good answer is kept for when the file
cannot be reached.
"""
from dataclasses import dataclass
import ipaddress
import json
import logging
from pathlib import Path
import re
import secrets
import socket
import time

from .lan import Lobby, MAX_LOBBY_NAME, MAX_VERSION, lobby_name
from .storage import LauncherError, read_json, write_json

log = logging.getLogger('ofs.public')

# The first simulator version the public server runs.
PUBLIC_VERSION = '0.6.3'
DIRECTORY_URL = 'https://raw.githubusercontent.com/KindaBad/OpenFlightSim-servers/main/public.json'
# Used until the directory has been read once on this computer.
DEFAULT_ADDRESS, DEFAULT_PORT = '92.98.24.248', 27020
DIRECTORY_SECONDS = 600
MAGIC = b'OFSPUB\x01'
REQUEST_SIZE = 1200
GAME_ID = re.compile(r'[0-9a-f]{8}')
UNREACHABLE = 'The public server cannot be reached right now. Check your internet connection, or try again later.'


@dataclass(frozen=True)
class Listing:
    lobbies: tuple = ()
    versions: tuple = ()
    reachable: bool = False


def endpoint(data):
    """The address and port in a directory or cache file, or None if it is not exactly that."""
    if not isinstance(data, dict) or data.get('schema') != 1:
        return None
    address, port = data.get('address'), data.get('port')
    if not isinstance(address, str) or type(port) is not int or not 1 <= port <= 65535:
        return None
    try:
        parsed = ipaddress.IPv4Address(address)
    except ValueError:
        return None
    # A numeric address only: the simulator's transport takes nothing else.
    if parsed.is_unspecified or parsed.is_multicast or parsed.is_link_local or parsed == ipaddress.IPv4Address('255.255.255.255'):
        return None
    return str(parsed), port


def _fetch(url):
    from .download import fetch_manifest  # imports the TLS stack; not needed to read a cache
    return fetch_manifest(url)


def locate(cache, *, fetch=_fetch, now=time.time, refresh=False, url=DIRECTORY_URL):
    """Where the public server is: (address, port).

    The directory is read at most every ten minutes, or at once with `refresh`
    after the server did not answer. Without it the last good answer stands.
    """
    cache = Path(cache)
    known, age = None, None
    try:
        saved = read_json(cache) if cache.is_file() else None
        known = endpoint(saved)
        if known and isinstance(saved.get('checked'), (int, float)):
            age = now() - saved['checked']
    except (LauncherError, OSError):
        pass
    if known and age is not None and 0 <= age < (30 if refresh else DIRECTORY_SECONDS):
        return known
    try:
        found = endpoint(fetch(url))
        if not found:
            raise LauncherError('Public server directory is not in the expected form')
        known = found
    except Exception as exc:  # no network, a captive portal, a filtered school connection
        log.info('Public server directory unavailable: %s', type(exc).__name__)
        if not known:
            known = (DEFAULT_ADDRESS, DEFAULT_PORT)
    try:
        # A failed look is remembered too, so a computer without access does not ask on every refresh.
        write_json(cache, dict(schema=1, address=known[0], port=known[1], checked=now()))
    except (LauncherError, OSError):
        pass
    return known


def _text(value, limit):
    return isinstance(value, str) and 0 < len(value) <= limit and all(32 <= ord(c) <= 126 for c in value)


def ask(address, port, request, timeout=1.2, attempts=3):
    """One question to the public server and its answer, or None if none came."""
    nonce = secrets.randbits(48)
    body = MAGIC + json.dumps({**request, 'nonce': nonce}, separators=(',', ':')).encode('ascii')
    if len(body) > REQUEST_SIZE:
        raise LauncherError('Request to the public server is too long')
    body = body.ljust(REQUEST_SIZE, b' ')
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as link:
            link.connect((address, port))
            for _ in range(attempts):
                link.settimeout(timeout)
                try:
                    link.send(body)
                    deadline = time.monotonic() + timeout
                    while True:
                        remaining = deadline - time.monotonic()
                        if remaining <= 0:
                            break
                        link.settimeout(remaining)
                        data = link.recv(2048)
                        if not data.startswith(MAGIC) or len(data) > REQUEST_SIZE:
                            continue
                        try:
                            reply = json.loads(data[len(MAGIC):].decode('ascii'))
                        except (ValueError, UnicodeDecodeError):
                            continue
                        if isinstance(reply, dict) and reply.get('nonce') == nonce and type(reply.get('ok')) is bool:
                            return reply
                except (socket.timeout, ConnectionError):
                    continue  # lost on the way, or nothing listening yet
    except OSError as exc:
        log.info('Public server unreachable: %s', type(exc).__name__)
    return None


def lobbies(reply, address, port):
    """The games in a `list` answer; anything not exactly a game is left out."""
    found = []
    games = reply.get('games')
    for game in games if isinstance(games, list) else []:
        if not isinstance(game, dict) or not isinstance(game.get('id'), str) or not GAME_ID.fullmatch(game['id']):
            continue
        numbers = [game.get(key) for key in ('players', 'max', 'bots', 'protocol')]
        if any(type(n) is not int for n in numbers) or not _text(game.get('name'), MAX_LOBBY_NAME) or not _text(game.get('version'), MAX_VERSION):
            continue
        players, limit, bots, protocol = numbers
        if not 0 <= players <= limit <= 64 or not 0 <= bots <= 8 or not 0 < protocol < 65536:
            continue
        found.append(Lobby(game['name'], address, port, protocol, players, limit, bots, game['version'], game['id']))
    return found[:32]


def browse(cache, **options):
    """The public games running now. Never raises: an unreachable server is an empty list."""
    try:
        address, port = locate(cache, **options)
        reply = ask(address, port, {'op': 'list'}, attempts=1)
        if not reply or not reply['ok']:
            return Listing()
        versions = tuple(v for v in reply.get('versions', []) if _text(v, MAX_VERSION)) if isinstance(reply.get('versions'), list) else ()
        return Listing(tuple(lobbies(reply, address, port)), versions, True)
    except (LauncherError, OSError, ValueError) as exc:
        log.info('Public games unavailable: %s', exc)
        return Listing()


def _refusal(reply):
    message = reply.get('message')
    return LauncherError(message if _text(message, 200) else 'The public server refused the request.')


def _session(cache, **options):
    """The server's address and a token from it, looking the address up again if it did not answer."""
    for refresh in (False, True):
        address, port = locate(cache, refresh=refresh, **options)
        reply = ask(address, port, {'op': 'list'})
        if reply and reply['ok'] and _text(reply.get('token'), 64):
            return address, port, reply
    raise LauncherError(UNREACHABLE)


def join(cache, game, **options):
    """Tell the public server this computer is about to join `game`; returns where to connect."""
    if not isinstance(game, str) or not GAME_ID.fullmatch(game):
        raise LauncherError('That game cannot be joined.')
    address, port, session = _session(cache, **options)
    return _enter(address, port, session['token'], game)


def _enter(address, port, token, game, wait=0):
    deadline = time.monotonic() + wait
    while True:
        reply = ask(address, port, {'op': 'join', 'token': token, 'id': game})
        if not reply:
            raise LauncherError(UNREACHABLE)
        if not reply['ok']:
            raise _refusal(reply)
        # A game just started takes a moment to open its port.
        if reply.get('ready') is True or time.monotonic() >= deadline:
            return address, port
        time.sleep(.25)


def host(cache, preferences, version, **options):
    """Start a public game with the host settings in `preferences` and return (address, port, id)."""
    address, port, session = _session(cache, **options)
    request = {'op': 'create', 'token': session['token'], 'version': version,
               'name': lobby_name(preferences.lobby, lobby_name(preferences.name + "'s game")),
               'bots': preferences.lan_bots, 'missile_reload': preferences.lan_missile_reload,
               'teams': bool(preferences.lan_teams), 'score_limit': preferences.lan_score_limit}
    reply = ask(address, port, request)
    if not reply:
        raise LauncherError(UNREACHABLE)
    if not reply['ok']:
        raise _refusal(reply)
    game = reply.get('id')
    if not isinstance(game, str) or not GAME_ID.fullmatch(game):
        raise LauncherError('The public server gave an answer that was not understood.')
    _enter(address, port, session['token'], game, wait=8)
    return address, port, game
