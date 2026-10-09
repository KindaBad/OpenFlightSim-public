"""Local-network discovery and LAN hosting; loopback sockets only, no Qt."""
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from launcher import lan
from launcher.config import Graphics, Preferences
from launcher.game import Installation, Session, arguments, server_arguments
from launcher.storage import LauncherError, write_json
from test_engine import fixture

# The same lobby, byte for byte, as tests/lan_tests.cpp encodes with the server's code.
GOLDEN = bytes.fromhex('4f46534c414e0152' '000f' '698c' '03' '10' '02' '0c' + b'Friday night'.hex() + '05' + b'0.5.0'.hex())
FRIDAY = lan.Lobby('Friday night', '192.168.1.20', 27020, 15, 3, 16, 2, '0.5.0')


class Responder(threading.Thread):
    """Stands in for a hosting server: answers each question with `reply`."""
    def __init__(self, reply):
        super().__init__(daemon=True)
        self.reply = reply
        self.link = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.link.bind(('127.0.0.1', 0))
        self.link.settimeout(.05)
        self.port = self.link.getsockname()[1]
        self.questions = []
        self.stop = threading.Event()

    def run(self):
        while not self.stop.is_set():
            try:
                data, sender = self.link.recvfrom(256)
            except OSError:
                continue
            self.questions.append(data)
            for reply in self.reply if isinstance(self.reply, list) else [self.reply]:
                self.link.sendto(reply, sender)

    def close(self):
        self.stop.set()
        self.join(1)
        self.link.close()


class Wire(unittest.TestCase):
    def test_reply_matches_the_server_byte_for_byte(self):
        self.assertEqual(lan.encode_reply(FRIDAY), GOLDEN)
        self.assertEqual(lan.parse_reply(GOLDEN, '192.168.1.20'), FRIDAY)

    def test_malformed_replies_are_refused(self):
        for size in range(len(GOLDEN)):
            self.assertIsNone(lan.parse_reply(GOLDEN[:size], '10.0.0.1'), size)
        self.assertIsNone(lan.parse_reply(GOLDEN + b'\0', '10.0.0.1'))
        self.assertIsNone(lan.parse_reply(lan.QUERY, '10.0.0.1'))
        self.assertIsNone(lan.parse_reply('text', '10.0.0.1'))
        self.assertIsNone(lan.parse_reply(lan.encode_reply(lan.Lobby('bad\nname', '', 27020, 15, 0, 8, 0, '1')), '10.0.0.1'))
        self.assertIsNone(lan.parse_reply(lan.encode_reply(lan.Lobby('Game', '', 0, 15, 0, 8, 0, '1')), '10.0.0.1'))
        self.assertIsNone(lan.parse_reply(lan.encode_reply(lan.Lobby('Game', '', 27020, 15, 9, 8, 0, '1')), '10.0.0.1'))
        self.assertIsNone(lan.parse_reply(lan.REPLY + bytes(7) + b'\x00\x00', '10.0.0.1'))
        self.assertIsNone(lan.parse_reply(GOLDEN[:16] + b'\xff' * (len(GOLDEN) - 16), '10.0.0.1'))
        # Any single corrupted byte is refused or still describes a well-formed game.
        for index in range(len(GOLDEN)):
            for value in range(0, 256, 7):
                lobby = lan.parse_reply(GOLDEN[:index] + bytes((value,)) + GOLDEN[index + 1:], '10.0.0.1')
                if lobby:
                    self.assertTrue(lobby.name and lobby.port and lobby.players <= lobby.max_players)
                    self.assertLessEqual(len(lobby.name), lan.MAX_LOBBY_NAME)

    def test_names_are_made_printable_and_bounded(self):
        self.assertEqual(lan.lobby_name("  Ace's game \t"), "Ace's game")
        self.assertEqual(len(lan.lobby_name('x' * 200)), lan.MAX_LOBBY_NAME)
        self.assertEqual(lan.lobby_name('✈✈', 'Fallback'), 'Fallback')
        self.assertEqual(lan.lobby_name(''), 'OpenFlightSim game')
        self.assertEqual(lan.lobby_name('--bots 8; rm -rf'), '--bots 8; rm -rf')  # one argument, never a shell

    def test_description_and_compatibility(self):
        self.assertIn('3/16 pilots + 2 bots', lan.describe(FRIDAY, 15))
        self.assertIn('192.168.1.20', lan.describe(FRIDAY))
        self.assertTrue(lan.joinable(FRIDAY, 15) and lan.joinable(FRIDAY))
        self.assertFalse(lan.joinable(FRIDAY, 14))
        self.assertIn('different game version', lan.describe(FRIDAY, 14))
        full = lan.Lobby('Full', '10.0.0.2', 27020, 15, 4, 4, 0, '0.5.0')
        self.assertFalse(lan.joinable(full, 15))
        self.assertIn('full', lan.describe(full, 15))

    def test_broadcast_targets_cover_every_network_and_this_computer(self):
        targets = lan.broadcast_targets(['192.168.1.23', '10.0.5.9', '192.168.1.77'])
        self.assertEqual(targets, ['255.255.255.255', '192.168.1.255', '10.0.5.255', '127.0.0.1'])
        for address in lan.local_addresses():
            parts = address.split('.')
            self.assertEqual(len(parts), 4)
            self.assertFalse(address.startswith('127.'))


    def test_the_question_is_asked_from_every_address(self):
        plan = lan.questions(['192.168.1.23', '10.0.5.9'])
        self.assertEqual(plan, [(None, ['255.255.255.255', '192.168.1.255', '10.0.5.255', '127.0.0.1']),
                                ('192.168.1.23', ['255.255.255.255', '192.168.1.255']),
                                ('10.0.5.9', ['255.255.255.255', '10.0.5.255'])])
        self.assertEqual(lan.questions([]), [(None, ['255.255.255.255', '127.0.0.1'])])


class Firewall(unittest.TestCase):
    """What a Linux host is told; the firewall is stood in for, never asked."""
    def firewalld(self, open_ports=(), failure=None):
        asked = []

        def run(command, **_):
            asked.append(command)
            if failure:
                if isinstance(failure, Exception):
                    raise failure
                return subprocess.CompletedProcess(command, 1, stdout=failure)
            self.assertEqual(command[:8], ['busctl', '--system', 'call', 'org.fedoraproject.FirewallD1', '/org/fedoraproject/FirewallD1',
                                           'org.fedoraproject.FirewallD1.zone', 'queryPort', 'sss'])
            self.assertEqual(command[8::2], ['', 'udp'])
            return subprocess.CompletedProcess(command, 0, stdout='b true\n' if int(command[9]) in open_ports else 'b false\n')
        return run, asked

    def test_other_systems_ask_the_player_themselves(self):
        run, asked = self.firewalld()
        self.assertIsNone(lan.firewall_advice(27020, run=run, platform='win32'))
        self.assertIsNone(lan.firewall_advice(27020, run=run, platform='darwin'))
        self.assertEqual(asked, [])

    def test_open_firewalld_needs_nothing(self):
        run, asked = self.firewalld(open_ports=(27020, 27019))
        self.assertIsNone(lan.firewall_advice(27020, run=run, platform='linux'))
        self.assertEqual([command[9] for command in asked], ['27020', '27019'])

    def test_closed_firewalld_names_the_ports_to_open(self):
        run, _ = self.firewalld(open_ports=(27019,))
        advice = lan.firewall_advice(27055, run=run, platform='linux')
        self.assertIn('sudo firewall-cmd --add-port=27055/udp', advice)
        self.assertNotIn('27019', advice)
        run, _ = self.firewalld()
        advice = lan.firewall_advice(27020, run=run, platform='linux')
        self.assertIn('--add-port=27020/udp --add-port=27019/udp', advice)

    def test_ufw_is_only_known_to_be_on(self):
        with tempfile.TemporaryDirectory() as folder:
            conf = Path(folder) / 'ufw.conf'
            # No firewalld: no message bus tool, the service not running, or an answer that makes no sense.
            for failure in (FileNotFoundError('busctl'), 'Call failed: The name is not activatable\n',
                            subprocess.TimeoutExpired('busctl', 3)):
                run, _ = self.firewalld(failure=failure)
                self.assertIsNone(lan.firewall_advice(27020, run=run, platform='linux', ufw=conf))
            conf.write_text('# comment\nENABLED=no\n')
            self.assertIsNone(lan.firewall_advice(27020, run=run, platform='linux', ufw=conf))
            conf.write_text('LOGLEVEL=low\nENABLED=yes\n')
            advice = lan.firewall_advice(27020, run=run, platform='linux', ufw=conf)
            self.assertIn('sudo ufw allow 27020/udp && sudo ufw allow 27019/udp', advice)
            # An open firewalld settles the matter whatever ufw's file says.
            run, _ = self.firewalld(open_ports=(27020, 27019))
            self.assertIsNone(lan.firewall_advice(27020, run=run, platform='linux', ufw=conf))

        def garbled(command, **_):
            return subprocess.CompletedProcess(command, 0, stdout='s "yes"\n')
        self.assertIsNone(lan.firewall_advice(27020, run=garbled, platform='linux', ufw=Path('/nonexistent/ufw.conf')))


class Discovery(unittest.TestCase):
    def test_every_question_is_sent_and_one_game_is_listed_once(self):
        responder = Responder(lan.encode_reply(FRIDAY))
        responder.start()
        try:
            # The second question leaves from a chosen address, as it does on a real network.
            with patch('launcher.lan.questions', return_value=[(None, ['127.0.0.1']), ('127.0.0.1', ['127.0.0.1']),
                                                               ('192.0.2.77', ['127.0.0.1'])]):
                found = lan.discover(timeout=.4, port=responder.port)
        finally:
            responder.close()
        self.assertEqual(responder.questions, [lan.QUERY, lan.QUERY])
        self.assertEqual([lobby.name for lobby in found], ['Friday night'])

    def test_finds_a_hosted_game_and_ignores_noise(self):
        responder = Responder([b'not a lobby', lan.encode_reply(FRIDAY), lan.encode_reply(FRIDAY)])
        responder.start()
        try:
            found = lan.discover(timeout=.4, port=responder.port, targets=['127.0.0.1'])
        finally:
            responder.close()
        self.assertEqual(responder.questions, [lan.QUERY])
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0], lan.Lobby('Friday night', '127.0.0.1', 27020, 15, 3, 16, 2, '0.5.0'))

    def test_nothing_hosted_finds_nothing(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as unused:
            unused.bind(('127.0.0.1', 0))
            port = unused.getsockname()[1]
        start = time.monotonic()
        self.assertEqual(lan.discover(timeout=.15, port=port, targets=['127.0.0.1', 'not an address']), [])
        self.assertLess(time.monotonic() - start, 2)

    def test_real_server_answers_when_built(self):
        root = Path(__file__).resolve().parents[2]
        suffix = '.exe' if os.name == 'nt' else ''
        server = next((p for p in (root / 'build/release/network' / ('ofs_server' + suffix),
                                   root / 'build/release/network/Release' / ('ofs_server' + suffix)) if p.is_file()), None)
        if not server:
            self.skipTest('ofs_server is not built here')
        # Discovery is on a fixed port, so the real answer is only checked when nothing else holds it.
        process = subprocess.Popen([str(server), '--bind', '127.0.0.1', '--port', '27991', '--lan-name', 'Launcher test',
                                    '--seconds', '4'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            found = []
            deadline = time.monotonic() + 3
            while not found and time.monotonic() < deadline:
                found = [lobby for lobby in lan.discover(timeout=.3, targets=['127.0.0.1']) if lobby.port == 27991]
        finally:
            process.terminate()
            output = process.communicate(timeout=5)[0]
        if 'could not open discovery port' in output or 'Launcher test' not in output:
            self.skipTest('this ofs_server predates LAN games, or the discovery port is in use')
        self.assertEqual([(lobby.name, lobby.players, lobby.bots) for lobby in found], [('Launcher test', 0, 0)])


class Hosting(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve()
        fixture(self.root, '0.5.0')
        suffix = '.exe' if os.name == 'nt' else ''
        (self.root / ('ofs_server' + suffix)).write_bytes(b'server')
        self.installation = Installation.discover(self.root)

    def tearDown(self):
        self.temp.cleanup()

    def test_lan_server_is_open_to_the_network_and_named(self):
        prefs = Preferences(name='Ace', lan_bots=3, port=27100)
        command = server_arguments(self.installation, prefs, lan=True)
        self.assertEqual(command[1:], ['--bind', '0.0.0.0', '--port', '27100', '--lan-name', "Ace's game", '--bots', '3'])
        prefs.lobby, prefs.lan_bots = 'Squadron night ✈ $(reboot)', 0
        command = server_arguments(self.installation, prefs, lan=True)
        self.assertEqual(command[-2:], ['--lan-name', 'Squadron night  $(reboot)'])
        self.assertNotIn('--bots', command)
        self.assertNotIn('--missile-reload', command)

    def test_missile_reload_reaches_a_server_that_takes_it(self):
        prefs = Preferences(name='Ace', lan_missile_reload=60)
        with self.assertRaises(LauncherError):
            server_arguments(self.installation, prefs, lan=True)
        self.installation.catalog['version'] = '0.5.6'
        self.assertEqual(server_arguments(self.installation, prefs, lan=True)[-2:], ['--missile-reload', '60'])
        prefs.lan_missile_reload = 3601
        with self.assertRaises(LauncherError):
            server_arguments(self.installation, prefs, lan=True)

    def test_team_battle_needs_a_game_that_has_it(self):
        prefs = Preferences(lan_teams=True, lan_score_limit=450)
        with self.assertRaises(LauncherError):
            server_arguments(self.installation, prefs, lan=True)
        self.installation.catalog['version'] = '0.6.0'
        self.assertEqual(server_arguments(self.installation, prefs, lan=True)[-3:], ['--teams', '--score-limit', '450'])
        self.assertNotIn('--teams', server_arguments(self.installation, Preferences(), lan=True))
        with self.assertRaises(LauncherError):
            server_arguments(self.installation, Preferences(lan_teams=True, lan_score_limit=10), lan=True)

    def test_side_and_bomb_load_are_passed_to_a_game_that_knows_them(self):
        graphics = Graphics(self.root / 'user/graphics.cfg')
        prefs = Preferences(mode='multiplayer', team='blue', loadout='nuke', aircraft=self.installation.aircraft[0]['id'])
        # An older game is given neither argument.
        command = arguments(self.installation, prefs, graphics)
        self.assertNotIn('--team', command)
        self.assertNotIn('--loadout', command)
        self.installation.catalog['version'] = '0.6.0'
        command = arguments(self.installation, prefs, graphics)
        self.assertEqual(command[command.index('--team') + 1], 'blue')
        self.assertNotIn('--loadout', command)  # not a bomber
        self.installation.aircraft[0]['bomber'] = True
        command = arguments(self.installation, prefs, graphics)
        self.assertEqual(command[command.index('--loadout') + 1], 'nuke')
        self.assertNotIn('--team', arguments(self.installation, Preferences(mode='multiplayer', aircraft=prefs.aircraft), graphics))
        with self.assertRaises(LauncherError):
            arguments(self.installation, Preferences(mode='multiplayer', team='green', aircraft=prefs.aircraft), graphics)

    def test_private_server_stays_on_this_computer(self):
        command = server_arguments(self.installation, Preferences(lobby='ignored', lan_bots=4))
        self.assertEqual(command[1:], ['--bind', '127.0.0.1', '--port', '27020'])

    def test_older_game_cannot_host_for_the_network(self):
        self.installation.catalog['version'] = '0.4.7'
        with self.assertRaises(LauncherError):
            server_arguments(self.installation, Preferences(), lan=True)
        self.assertIn('127.0.0.1', server_arguments(self.installation, Preferences()))
        self.installation.catalog['version'] = '0.5.0'
        with self.assertRaises(LauncherError):
            server_arguments(self.installation, Preferences(lan_bots=9), lan=True)

    def test_hosting_starts_the_server_then_joins_it_locally(self):
        graphics = Graphics(self.root / 'user/graphics.cfg')
        prefs = Preferences(aircraft='typhoon', mode='multiplayer', host=True, name='Ace', server='203.0.113.9')
        session = Session()
        with patch('launcher.game.spawn') as spawn:
            spawn.return_value.wait.side_effect = subprocess.TimeoutExpired('server', .5)
            session.start(self.installation, prefs, graphics, self.root / 'user', self.root / 'user/runtime', lan=True)
        server, client = (call.args[0] for call in spawn.call_args_list)
        self.assertEqual(server[1:5], ['--bind', '0.0.0.0', '--port', '27020'])
        self.assertIn("Ace's game", server)
        self.assertEqual(client[client.index('--server') + 1], '127.0.0.1')
        self.assertEqual(client[client.index('--name') + 1], 'Ace')
        session.client = None
        session.server = None
        session.cleanup()

    def test_joining_uses_the_lobby_address(self):
        prefs = Preferences(aircraft='a320', mode='multiplayer', server='192.168.1.20', port=27025, name='Wingman')
        args = arguments(self.installation, prefs, Graphics(self.root / 'graphics.cfg'))
        self.assertEqual(args[args.index('--server') + 1], '192.168.1.20')
        self.assertEqual(args[args.index('--port') + 1], '27025')

    def test_saved_settings_from_before_lan_games_still_load(self):
        path = self.root / 'launcher.json'
        write_json(path, {'schema': 1, 'name': 'Old pilot', 'port': 27020, 'bots': 2})
        prefs = Preferences.load(path)
        self.assertEqual((prefs.lobby, prefs.lan_bots, prefs.name), ('', 0, 'Old pilot'))
        write_json(path, {'schema': 1, 'lan_bots': 12})
        with self.assertRaises(LauncherError):
            Preferences.load(path)


if __name__ == '__main__':
    unittest.main()
