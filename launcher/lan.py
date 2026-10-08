"""Finding games on the local network. No Qt; the page that lists them is in ui.py.

A hosting server answers a short question broadcast on a fixed UDP port (see
network/include/ofs/net/discovery.hpp, which this mirrors byte for byte). The
answer only describes a game: joining it goes through the simulator's ordinary
connection, so nothing received here is trusted beyond being shown in a list.
"""
from dataclasses import dataclass
import ipaddress
from pathlib import Path
import socket
import subprocess
import sys
import time

DISCOVERY_PORT = 27019
QUERY = b'OFSLAN\x01Q'
REPLY = b'OFSLAN\x01R'
MAX_LOBBY_NAME = 48
MAX_VERSION = 24
# The first simulator version whose server answers discovery and takes --lan-name.
LAN_VERSION = '0.5.0'


@dataclass(frozen=True)
class Lobby:
    name: str
    address: str
    port: int
    protocol: int
    players: int
    max_players: int
    bots: int
    version: str


def _printable(text):
    return all(32 <= ord(c) <= 126 for c in text)


def lobby_name(text, fallback='OpenFlightSim game'):
    """A name typed by a player, made fit to announce: printable ASCII, bounded."""
    name = ''.join(c for c in str(text) if 32 <= ord(c) <= 126)[:MAX_LOBBY_NAME].strip()
    return name or fallback


def parse_reply(data, address):
    """Decode one answer, or return None for anything that is not exactly one."""
    if not isinstance(data, (bytes, bytearray)) or not len(REPLY) + 9 <= len(data) <= 128 or not data.startswith(REPLY):
        return None
    position = len(REPLY)
    protocol = int.from_bytes(data[position:position + 2], 'big')
    port = int.from_bytes(data[position + 2:position + 4], 'big')
    players, max_players, bots = data[position + 4], data[position + 5], data[position + 6]
    position += 7
    fields = []
    for limit in (MAX_LOBBY_NAME, MAX_VERSION):
        if position >= len(data):
            return None
        size = data[position]
        position += 1
        if size > limit or size > len(data) - position:
            return None
        try:
            text = bytes(data[position:position + size]).decode('ascii')
        except UnicodeDecodeError:
            return None
        if not _printable(text):
            return None
        fields.append(text)
        position += size
    name, version = fields
    if position != len(data) or not port or not name or players > max_players:
        return None
    return Lobby(name, address, port, protocol, players, max_players, bots, version)


def encode_reply(lobby):
    """The wire form of an answer; the server's side, here for tests and tools."""
    name, version = lobby.name.encode('ascii')[:MAX_LOBBY_NAME], lobby.version.encode('ascii')[:MAX_VERSION]
    return (REPLY + lobby.protocol.to_bytes(2, 'big') + lobby.port.to_bytes(2, 'big')
            + bytes((lobby.players, lobby.max_players, lobby.bots, len(name))) + name + bytes((len(version),)) + version)


def local_addresses():
    """This computer's IPv4 addresses on its networks, most likely LAN address first."""
    found = []
    # The address the system would use to reach the wider network. Connecting a
    # datagram socket sends nothing; it only asks the system to pick a route.
    for probe in ('192.0.2.1', '10.255.255.255'):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as link:
                link.connect((probe, 9))
                found.append(link.getsockname()[0])
        except OSError:
            pass
    try:
        found += [info[4][0] for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)]
    except OSError:
        pass
    result = []
    for address in found:
        try:
            parsed = ipaddress.ip_address(address)
        except ValueError:
            continue
        if parsed.is_loopback or parsed.is_unspecified or parsed.is_link_local or address in result:
            continue
        result.append(address)
    return result


def _subnet(address):
    # Home networks are almost always /24; a wrong guess only wastes one datagram.
    return '.'.join(address.split('.')[:3] + ['255'])


def broadcast_targets(addresses=None):
    """Where to ask: every network this computer is on, and this computer itself."""
    targets = ['255.255.255.255']
    for address in local_addresses() if addresses is None else addresses:
        if _subnet(address) not in targets:
            targets.append(_subnet(address))
    targets.append('127.0.0.1')
    return targets


def questions(addresses=None):
    """Who asks whom: pairs of the local address a question leaves from and where it goes.

    The first pair lets the system choose the way out, which reaches one network
    only. On a computer with several (a VPN, a virtual machine's adapter, wired
    and wireless together) that is often not the one a game is on, so the
    question is asked again from each address in turn. A broadcast sent from an
    address leaves on that address's own network and reaches every computer on
    it, whatever subnet they have been given.
    """
    addresses = local_addresses() if addresses is None else list(addresses)
    return [(None, broadcast_targets(addresses))] + [(address, ['255.255.255.255', _subnet(address)]) for address in addresses]


def discover(timeout=.6, port=DISCOVERY_PORT, targets=None):
    """Ask the local network once and return the games that answer within `timeout` seconds.

    With `targets`, only those addresses are asked, the system choosing the way out.
    """
    lobbies = {}
    mine = set(local_addresses())
    links = []
    try:
        for source, wanted in [(None, targets)] if targets is not None else questions(sorted(mine)):
            link = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                link.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
                link.setblocking(False)
                if source:
                    link.bind((source, 0))
            except OSError:
                link.close()  # an address that has just gone away
                continue
            links.append(link)
            for target in wanted:
                try:
                    # Numeric addresses only: a name would be looked up, and stall the search.
                    link.sendto(QUERY, (str(ipaddress.IPv4Address(target)), port))
                except (OSError, ValueError):
                    pass  # not an address, a network that is down, or one that refuses broadcasts
        deadline = time.monotonic() + timeout
        while links and time.monotonic() < deadline and len(lobbies) < 64:
            heard = False
            for link in links:
                try:
                    data, (address, _) = link.recvfrom(256)
                except OSError:
                    # Nothing waiting; on Windows also an earlier unreachable target. Keep listening.
                    continue
                heard = True
                lobby = parse_reply(data, address)
                if lobby:
                    # A game on this computer answers on every address it was asked on.
                    own = address in mine or address.startswith('127.')
                    lobbies.setdefault((lobby.port, lobby.name) if own else (address, lobby.port), lobby)
            if not heard:
                time.sleep(.01)
    finally:
        for link in links:
            link.close()
    return sorted(lobbies.values(), key=lambda lobby: (lobby.name.lower(), lobby.address, lobby.port))


def _firewalld_open(port, run):
    """Whether firewalld lets UDP `port` in: True, False, or None when firewalld is not there to ask."""
    # The firewall's own message bus answers in a few milliseconds and needs no
    # privileges; its command-line tool takes seconds to start.
    question = ['busctl', '--system', 'call', 'org.fedoraproject.FirewallD1', '/org/fedoraproject/FirewallD1',
                'org.fedoraproject.FirewallD1.zone', 'queryPort', 'sss', '', str(port), 'udp']
    try:
        answer = run(question, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL, timeout=3, text=True)
    except (OSError, subprocess.SubprocessError):
        return None
    reply = (answer.stdout or '').split()
    if answer.returncode != 0 or len(reply) != 2 or reply[0] != 'b' or reply[1] not in ('true', 'false'):
        return None
    return reply[1] == 'true'


def firewall_advice(port, discovery=DISCOVERY_PORT, run=subprocess.run, platform=sys.platform, ufw=Path('/etc/ufw/ufw.conf')):
    """What a Linux host has to do before others can reach a game on `port`, or None when nothing is in the way.

    Windows asks the player itself the first time a game is hosted. Linux
    firewalls turn the players away silently, so the launcher looks: firewalld
    can be asked without privileges; ufw can only be seen to be switched on.
    Nothing is changed, and a firewall that cannot be asked is left alone.
    """
    if not platform.startswith('linux'):
        return None
    answers = {number: _firewalld_open(number, run) for number in (port, discovery)}
    if None not in answers.values():
        closed = [number for number, is_open in answers.items() if not is_open]
        if not closed:
            return None
        ports = ' '.join(f'--add-port={number}/udp' for number in closed)
        return ('This computer\'s firewall will turn other players away. To let them in until the next restart, run:  '
                f'sudo firewall-cmd {ports}')
    try:
        enabled = any(line.strip().lower() == 'enabled=yes' for line in ufw.read_text(encoding='utf-8', errors='replace').splitlines())
    except OSError:
        enabled = False
    if enabled:
        return ('If other players cannot join, this computer\'s firewall is turning them away. Let them in with:  '
                f'sudo ufw allow {port}/udp && sudo ufw allow {discovery}/udp')
    return None


def describe(lobby, protocol=None):
    """One line for a list: how full the game is and whether this installation can join it."""
    pilots = f'{lobby.players}/{lobby.max_players} pilots'
    if lobby.bots:
        pilots += f' + {lobby.bots} bots'
    line = f'{lobby.name}   ·   {pilots}   ·   {lobby.address}'
    if lobby.version:
        line += f'   ·   v{lobby.version}'
    if protocol is not None and lobby.protocol != protocol:
        line += '   ·   different game version'
    elif lobby.players >= lobby.max_players:
        line += '   ·   full'
    return line


def joinable(lobby, protocol=None):
    return (protocol is None or lobby.protocol == protocol) and lobby.players < lobby.max_players
