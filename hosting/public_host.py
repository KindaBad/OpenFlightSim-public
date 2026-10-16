#!/usr/bin/env python3
"""Public games on one forwarded UDP port. Standard library only.

A home connection usually forwards a single port to the computer that hosts.
This service owns that port and does three things on it:

  * answers launchers: which games are running, start one, let me into one;
  * starts an `ofs_server` per game on a loopback port and stops it once the
    last pilot has been gone for a while;
  * relays game traffic between each player and the game they asked for, so
    every game is reached through the same public port.

A launcher's datagrams begin with `OFSPUB\\x01`; everything else is game
traffic. A player is matched to a game by address: `join` records which game
the asking address wants, and the next new conversation from that address is
relayed to it. Nothing here is trusted by the game: a relayed player goes
through the same connection checks as one who connected directly.

The launcher's side of the conversation is launcher/public.py.
"""
import argparse
from dataclasses import dataclass, field
import hashlib
import hmac
import json
import logging
import os
from pathlib import Path
import re
import secrets
import selectors
import signal
import socket
import subprocess
import time

log = logging.getLogger('ofs.public')

MAGIC = b'OFSPUB\x01'
# A question is padded to this size and no answer is longer, so a forged
# source address cannot turn the service into a larger stream of traffic.
REQUEST_SIZE = 1200
LAN_QUERY = b'OFSLAN\x01Q'
LAN_REPLY = b'OFSLAN\x01R'
MAX_NAME = 48
VERSION = re.compile(r'\d{1,9}(?:\.\d{1,9}){2,3}')
GAME_ID = re.compile(r'[0-9a-f]{8}')
# Games on the round Earth (docs/WHOLE_EARTH_PLAN.md): the theatres a server
# plays, and the first game version whose server does. Launchers that do not
# say they know the Earth map are offered valley games only.
THEATRES = ('austria', 'alps', 'gulf')
EARTH_VERSION = '0.7.0'


@dataclass
class Limits:
    max_games: int = 6
    max_players: int = 16
    games_per_address: int = 2      # running games one address may have started
    create_interval: float = 15     # seconds between games started by one address
    empty_seconds: float = 300      # a game with no pilots is stopped after this
    start_seconds: float = 20       # a game that never answers is stopped after this
    route_seconds: float = 300      # how long a `join` waits for the game client
    flow_seconds: float = 20        # a silent player's relay is dropped after this
    max_flows: int = 256
    flows_per_address: int = 32
    requests_per_second: float = 10  # per address, with a burst of twice that
    token_seconds: int = 600


@dataclass
class Game:
    id: str
    name: str
    version: str
    port: int
    discovery: int
    process: object
    log: Path
    creator: str
    created: float
    teams: bool
    occupied: float                 # when a pilot was last seen in it
    map: str = 'valley'             # or 'earth:<theatre>'
    seen: bool = False              # has answered for itself at least once
    players: int = 0
    max_players: int = 0
    bots: int = 0
    protocol: int = 0


@dataclass
class Flow:
    address: tuple
    game: Game
    link: socket.socket
    last: float


@dataclass
class Route:
    game: str
    expires: float
    used: bool = False


@dataclass
class Bucket:
    tokens: float
    at: float


def clean_name(text, fallback='OpenFlightSim game'):
    name = ''.join(c for c in str(text) if 32 <= ord(c) <= 126)[:MAX_NAME].strip()
    return name or fallback


def parse_lobby(data):
    """A game's answer about itself (network/include/ofs/net/discovery.hpp), or None."""
    if not len(LAN_REPLY) + 9 <= len(data) <= 128 or not data.startswith(LAN_REPLY):
        return None
    position = len(LAN_REPLY)
    protocol = int.from_bytes(data[position:position + 2], 'big')
    port = int.from_bytes(data[position + 2:position + 4], 'big')
    players, max_players, bots = data[position + 4], data[position + 5], data[position + 6]
    return dict(protocol=protocol, port=port, players=players, max_players=max_players, bots=bots)


def version_key(text):
    return tuple(int(part) for part in text.split('.'))


class Host:
    def __init__(self, builds, state, bind='0.0.0.0', port=27020, game_ports=27100, limits=None,
                 clock=time.monotonic, wall=time.time, earth_pack=None):
        self.builds, self.state = Path(builds), Path(state)
        # The Earth terrain pack games on the Earth are played on, if there is one.
        self.earth_pack = Path(earth_pack) if earth_pack else self.state / 'earth' / 'earth.ofse'
        self.limits = limits or Limits()
        self.clock, self.wall = clock, wall
        self.game_ports = game_ports
        (self.state / 'logs').mkdir(parents=True, exist_ok=True)
        self.secret = secrets.token_bytes(32)
        self.selector = selectors.DefaultSelector()
        self.public = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        for option in (socket.SO_RCVBUF, socket.SO_SNDBUF):
            self.public.setsockopt(socket.SOL_SOCKET, option, 4 * 1024 * 1024)
        self.public.bind((bind, port))
        self.public.setblocking(False)
        self.port = self.public.getsockname()[1]
        self.selector.register(self.public, selectors.EVENT_READ, 'public')
        # Each game is asked how full it is over loopback, on a port of its own.
        self.probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.probe.bind(('127.0.0.1', 0))
        self.probe.setblocking(False)
        self.selector.register(self.probe, selectors.EVENT_READ, 'probe')
        self.games, self.flows, self.routes, self.buckets = {}, {}, {}, {}
        self.flow_counts, self.last_create, self.stopping = {}, {}, []
        # A launcher repeats a question whose answer was lost; a repeated
        # `create` gets the first answer again instead of a second game.
        self.created = {}
        self.next_probe = self.next_upkeep = 0.0
        self.version_cache = (0.0, [])
        self.relayed = 0

    # ---- builds -----------------------------------------------------------

    def versions(self):
        """Game versions there is a server for, newest first."""
        now = self.clock()
        if now - self.version_cache[0] > 10 or not self.version_cache[1]:
            found = []
            try:
                for entry in self.builds.iterdir():
                    if VERSION.fullmatch(entry.name) and os.access(entry / 'ofs_server', os.X_OK):
                        found.append(entry.name)
            except OSError:
                pass
            self.version_cache = (now, sorted(found, key=version_key, reverse=True))
        return self.version_cache[1]

    # ---- launcher questions -------------------------------------------------

    def token(self, address, age=0):
        period = int(self.wall() // self.limits.token_seconds) - age
        return hmac.new(self.secret, f'{address}|{period}'.encode(), hashlib.sha256).hexdigest()[:16]

    def allowed(self, address, now):
        rate = self.limits.requests_per_second
        bucket = self.buckets.get(address)
        if bucket is None:
            if len(self.buckets) >= 4096:
                return False
            bucket = self.buckets[address] = Bucket(rate * 2, now)
        bucket.tokens = min(rate * 2, bucket.tokens + (now - bucket.at) * rate)
        bucket.at = now
        if bucket.tokens < 1:
            return False
        bucket.tokens -= 1
        return True

    def listing(self, earth=False):
        """The games running, for a launcher that knows the Earth map or one that does not."""
        games = []
        for g in sorted(self.games.values(), key=lambda g: g.created):
            if not g.seen or (g.map != 'valley' and not earth):
                continue
            game = dict(id=g.id, name=g.name, players=g.players, max=g.max_players, bots=g.bots,
                        version=g.version, protocol=g.protocol, teams=g.teams)
            if earth:
                game['map'] = g.map
            games.append(game)
        return games

    def answer(self, data, address, now):
        if len(data) != REQUEST_SIZE or not self.allowed(address[0], now):
            return None
        try:
            request = json.loads(data[len(MAGIC):].decode('ascii'))
        except (ValueError, UnicodeDecodeError):
            return None
        if not isinstance(request, dict) or type(request.get('nonce')) is not int or not 0 <= request['nonce'] < 2 ** 53:
            return None
        operation = request.get('op')
        repeat = self.created.get((address[0], request['nonce']))
        if repeat:
            return repeat[0]
        if operation == 'list':
            maps = request.get('maps')
            earth = isinstance(maps, list) and 'earth' in maps
            reply = dict(ok=True, token=self.token(address[0]), games=self.listing(earth), versions=self.versions()[:4],
                         capacity=self.limits.max_games)
        elif operation in ('create', 'join'):
            # The token was sent to the address it names, so a question that
            # carries it really came from there and not from a forged source.
            if request.get('token') not in (self.token(address[0]), self.token(address[0], 1)):
                reply = self.refuse('token', 'Ask again.')
            elif operation == 'create':
                reply = self.create(request, address[0], now)
            else:
                reply = self.join(request, address[0], now)
        else:
            return None
        reply['nonce'] = request['nonce']
        encoded = MAGIC + json.dumps(reply, separators=(',', ':')).encode('ascii')
        while len(encoded) > REQUEST_SIZE and reply.get('games'):
            reply['games'].pop()
            encoded = MAGIC + json.dumps(reply, separators=(',', ':')).encode('ascii')
        if len(encoded) > REQUEST_SIZE:
            return None
        if operation == 'create' and len(self.created) < 4096:
            self.created[(address[0], request['nonce'])] = (encoded, now)
        return encoded

    @staticmethod
    def refuse(code, message):
        return dict(ok=False, error=code, message=message)

    def create(self, request, address, now):
        version = request.get('version')
        available = self.versions()
        if not isinstance(version, str) or not VERSION.fullmatch(version):
            return self.refuse('request', 'The request was not understood.')
        if version not in available:
            if available and version_key(version) < version_key(available[-1]):
                return self.refuse('version', f'The public server no longer runs OpenFlightSim {version}. Update your game and try again.')
            return self.refuse('version', f'The public server is still getting ready for OpenFlightSim {version}. Try again in a few minutes.')
        bots, reload_time, teams, score = request.get('bots', 0), request.get('missile_reload', 0), request.get('teams', False), request.get('score_limit', 300)
        if (type(bots) is not int or not 0 <= bots <= 8 or type(reload_time) is not int or not 0 <= reload_time <= 3600
                or type(teams) is not bool or type(score) is not int or not 50 <= score <= 5000):
            return self.refuse('request', 'The game settings were not understood.')
        world, theatre = request.get('map', 'valley'), request.get('theatre')
        if world not in ('valley', 'earth') or (world == 'earth' and theatre not in THEATRES):
            return self.refuse('request', 'The game settings were not understood.')
        if world == 'earth':
            if bots or teams:
                return self.refuse('request', 'A game on the Earth map is free flight for now: no bots or team game.')
            if version_key(version) < version_key(EARTH_VERSION):
                return self.refuse('version', f'Games on the Earth map need OpenFlightSim {EARTH_VERSION} or newer.')
            if not self.earth_pack.is_file():
                return self.refuse('earth', 'The public server does not have the Earth map yet. Try again later, or play in the valley.')
        mine = [g for g in self.games.values() if g.creator == address]
        if len(mine) >= self.limits.games_per_address or now - self.last_create.get(address, -1e9) < self.limits.create_interval:
            return self.refuse('busy', 'You already have a public game running. Join it from the list, or wait a moment before starting another.')
        if len(self.games) >= self.limits.max_games:
            return self.refuse('full', 'The public server is running as many games as it can. Join one from the list, or try again later.')
        slot = next(i for i in range(self.limits.max_games) if self.game_ports + i not in {g.port for g in self.games.values()})
        identity = secrets.token_hex(4)
        name = clean_name(request.get('name', ''))
        port, discovery = self.game_ports + slot, self.game_ports + 100 + slot
        # Every value below was checked above; the name is printable ASCII of
        # bounded length and is passed as one argument, never through a shell.
        command = [str(self.builds / version / 'ofs_server'), '--bind', '127.0.0.1', '--port', str(port),
                   '--max-players', str(self.limits.max_players), '--lan-name', name, '--discovery-port', str(discovery)]
        if bots:
            command += ['--bots', str(bots)]
        if reload_time:
            command += ['--missile-reload', str(reload_time)]
        if teams:
            command += ['--teams', '--score-limit', str(score)]
        if world == 'earth':
            command += ['--map', 'earth', '--theatre', theatre, '--earth-pack', str(self.earth_pack)]
        path = self.state / 'logs' / f'{identity}.log'
        try:
            with open(path, 'wb') as stream:
                process = subprocess.Popen(command, cwd=self.state, stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT)
        except OSError as exc:
            log.error('could not start a game: %s', exc)
            return self.refuse('failed', 'The public server could not start a game. Try again later.')
        self.games[identity] = Game(identity, name, version, port, discovery, process, path, address, now, teams, occupied=now,
                                    map='valley' if world == 'valley' else 'earth:' + theatre)
        self.last_create[address] = now
        self.next_probe = 0
        log.info('game %s "%s" v%s started for %s on loopback port %s', identity, name, version, address, port)
        return dict(ok=True, id=identity)

    def join(self, request, address, now):
        identity = request.get('id')
        game = self.games.get(identity) if isinstance(identity, str) and GAME_ID.fullmatch(identity) else None
        if not game:
            return self.refuse('gone', 'That game has ended.')
        if game.seen and game.players >= game.max_players:
            return self.refuse('full', 'That game is full.')
        routes = [r for r in self.routes.get(address, []) if r.expires > now][-15:]
        waiting = next((r for r in routes if r.game == identity and not r.used), None)
        if waiting:
            waiting.expires = now + self.limits.route_seconds
        else:
            routes.append(Route(identity, now + self.limits.route_seconds))
        self.routes[address] = routes
        return dict(ok=True, id=identity, ready=game.seen)

    # ---- relaying -----------------------------------------------------------

    def route(self, address, now):
        """The game a new conversation from `address` belongs to, or None.

        Players behind one router share an address. Each `join` waits its turn,
        so two of them heading for different games are told apart by order; a
        conversation with no `join` of its own goes where the last one went.
        """
        routes = [r for r in self.routes.get(address, []) if r.expires > now and r.game in self.games]
        chosen = next((r for r in routes if not r.used), routes[-1] if routes else None)
        if not chosen:
            return None
        chosen.used = True
        return self.games[chosen.game]

    def from_player(self, data, address, now):
        flow = self.flows.get(address)
        if flow is None:
            if len(self.flows) >= self.limits.max_flows or self.flow_counts.get(address[0], 0) >= self.limits.flows_per_address:
                return
            game = self.route(address[0], now)
            if not game:
                return
            link = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            link.setblocking(False)
            link.connect(('127.0.0.1', game.port))
            flow = self.flows[address] = Flow(address, game, link, now)
            self.flow_counts[address[0]] = self.flow_counts.get(address[0], 0) + 1
            self.selector.register(link, selectors.EVENT_READ, flow)
            log.info('relay %s:%s -> game %s', address[0], address[1], game.id)
        flow.last = now
        try:
            flow.link.send(data)
            self.relayed += 1
        except BlockingIOError:
            pass
        except OSError:
            self.drop(flow)  # the game has gone

    def from_game(self, flow):
        for _ in range(64):
            try:
                data = flow.link.recv(2048)
            except BlockingIOError:
                return
            except OSError:
                self.drop(flow)
                return
            try:
                self.public.sendto(data, flow.address)
                self.relayed += 1
            except OSError:
                pass  # a full buffer or an unreachable player loses one datagram

    def drop(self, flow):
        if self.flows.pop(flow.address, None) is None:
            return
        count = self.flow_counts.get(flow.address[0], 1) - 1
        if count > 0:
            self.flow_counts[flow.address[0]] = count
        else:
            self.flow_counts.pop(flow.address[0], None)
        try:
            self.selector.unregister(flow.link)
        except (KeyError, ValueError):
            pass
        flow.link.close()

    # ---- games --------------------------------------------------------------

    def heard(self, data):
        lobby = parse_lobby(data)
        game = lobby and next((g for g in self.games.values() if g.port == lobby['port']), None)
        if not game:
            return
        game.players, game.max_players, game.bots, game.protocol = lobby['players'], lobby['max_players'], lobby['bots'], lobby['protocol']
        game.seen = True

    def end(self, game, reason):
        if self.games.pop(game.id, None) is None:
            return
        for flow in [f for f in self.flows.values() if f.game is game]:
            self.drop(flow)
        code = game.process.poll()
        if code is None:
            game.process.terminate()
            self.stopping.append((game.process, self.clock() + 5))
        log.info('game %s "%s" ended: %s', game.id, game.name, reason)
        if code in (None, 0):
            game.log.unlink(missing_ok=True)
        else:
            log.warning('game %s exited with %s; its log is kept at %s', game.id, code, game.log)

    def upkeep(self, now):
        for game in list(self.games.values()):
            if game.process.poll() is not None:
                self.end(game, f'server exited with {game.process.returncode}')
            elif not game.seen and now - game.created > self.limits.start_seconds:
                self.end(game, 'never answered')
            else:
                if game.players or any(f.game is game for f in self.flows.values()):
                    game.occupied = now
                if now - game.occupied > self.limits.empty_seconds:
                    self.end(game, 'empty')
        for flow in [f for f in self.flows.values() if now - f.last > self.limits.flow_seconds]:
            self.drop(flow)
        for address in [a for a, routes in self.routes.items() if all(r.expires <= now for r in routes)]:
            del self.routes[address]
        for address in [a for a, b in self.buckets.items() if now - b.at > 60]:
            del self.buckets[address]
        for address in [a for a, at in self.last_create.items() if now - at > 3600]:
            del self.last_create[address]
        for key in [k for k, (_, at) in self.created.items() if now - at > 60]:
            del self.created[key]
        waiting = []
        for process, deadline in self.stopping:
            if process.poll() is None:
                if now > deadline:
                    process.kill()
                waiting.append((process, deadline))
        self.stopping = waiting
        # Logs of games that crashed are kept for a look, the newest few only.
        logs = sorted((self.state / 'logs').glob('*.log'), key=lambda p: p.stat().st_mtime)
        running = {g.log for g in self.games.values()}
        for path in [p for p in logs if p not in running][:-20]:
            path.unlink(missing_ok=True)

    # ---- the loop -----------------------------------------------------------

    def step(self, timeout=.25):
        for key, _ in self.selector.select(timeout):
            now = self.clock()
            if key.data == 'public':
                for _ in range(256):
                    try:
                        data, address = self.public.recvfrom(2048)
                    except BlockingIOError:
                        break
                    except OSError:
                        continue  # an earlier datagram could not be delivered
                    if data.startswith(MAGIC):
                        reply = self.answer(data, address, now)
                        if reply:
                            try:
                                self.public.sendto(reply, address)
                            except OSError:
                                pass
                    else:
                        self.from_player(data, address, now)
            elif key.data == 'probe':
                for _ in range(64):
                    try:
                        self.heard(self.probe.recv(256))
                    except BlockingIOError:
                        break
                    except OSError:
                        continue
            elif key.data.address in self.flows:
                self.from_game(key.data)
        now = self.clock()
        if now >= self.next_probe:
            self.next_probe = now + (.25 if any(not g.seen for g in self.games.values()) else 2)
            for game in self.games.values():
                try:
                    self.probe.sendto(LAN_QUERY, ('127.0.0.1', game.discovery))
                except OSError:
                    pass
        if now >= self.next_upkeep:
            self.next_upkeep = now + 1
            self.upkeep(now)

    def close(self):
        for game in list(self.games.values()):
            self.end(game, 'service stopping')
        deadline = time.monotonic() + 5
        for process, _ in self.stopping:
            try:
                process.wait(timeout=max(0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                process.kill()
        self.stopping = []
        self.selector.close()
        self.public.close()
        self.probe.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--builds', required=True, help='folder holding <version>/ofs_server')
    parser.add_argument('--state', required=True, help='folder for game logs')
    parser.add_argument('--bind', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=27020, help='the forwarded UDP port')
    parser.add_argument('--game-ports', type=int, default=27100, help='first loopback port for games')
    parser.add_argument('--max-games', type=int, default=6)
    parser.add_argument('--earth-pack', help='the Earth terrain pack for games on the Earth map (default: <state>/earth/earth.ofse)')
    options = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format='%(levelname)s %(message)s')
    host = Host(options.builds, options.state, options.bind, options.port, options.game_ports, Limits(max_games=options.max_games),
                earth_pack=options.earth_pack)
    stop = []
    for number in (signal.SIGINT, signal.SIGTERM):
        signal.signal(number, lambda *_: stop.append(True))
    log.info('public games on UDP %s:%s, up to %s at once, versions %s', options.bind, host.port, options.max_games,
             ', '.join(host.versions()) or 'none yet')
    report = time.monotonic()
    try:
        while not stop:
            host.step()
            if time.monotonic() - report >= 300:
                report = time.monotonic()
                log.info('games=%s players=%s relays=%s datagrams=%s versions=%s', len(host.games),
                         sum(g.players for g in host.games.values()), len(host.flows), host.relayed, ','.join(host.versions()))
    finally:
        host.close()


if __name__ == '__main__':
    main()
