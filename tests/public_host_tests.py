#!/usr/bin/env python3
"""The public game host end to end: real servers and a real client through the relay.

Usage: public_host_tests.py <ofs_server> <ofs_bot>. Loopback only.
"""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT), str(ROOT / 'hosting')]
import public_host  # noqa: E402
from launcher import public  # noqa: E402
from launcher.config import Preferences  # noqa: E402
from launcher.storage import LauncherError, write_json  # noqa: E402

SERVER, BOT = None, None
VERSION = '9.9.9'


def free_port_block():
    """A first loopback port for games that nothing else is using."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(('127.0.0.1', 0))
        return 20000 + probe.getsockname()[1] % 20000


class VersionFormat(unittest.TestCase):
    def test_three_or_four_numbers_in_any_size_are_accepted(self):
        for version in ('0.4.82', '0.10.0', '1.2.3.4', '12.345.6789.0'):
            with self.subTest(version=version):
                self.assertTrue(public_host.VERSION.fullmatch(version))
        self.assertEqual(sorted(['0.4.9', '0.4.82', '0.4.82.1'], key=public_host.version_key), ['0.4.9', '0.4.82', '0.4.82.1'])

    def test_other_shapes_are_refused(self):
        for version in ('1.2', '1.2.3.4.5', '1.2.3.', '1.2.3-rc.1', '../1.2.3', '1234567890.1.1'):
            with self.subTest(version=version):
                self.assertFalse(public_host.VERSION.fullmatch(version))


class Hosted(unittest.TestCase):
    limits = dict(empty_seconds=300)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        (root / 'builds' / VERSION).mkdir(parents=True)
        (root / 'builds' / VERSION / 'ofs_server').symlink_to(SERVER)
        self.host = public_host.Host(root / 'builds', root / 'state', '127.0.0.1', 0, free_port_block(),
                                     public_host.Limits(**{'create_interval': 0, 'requests_per_second': 1000, **self.limits}))
        self.cache = root / 'public-server.json'
        write_json(self.cache, dict(schema=1, address='127.0.0.1', port=self.host.port, checked=time.time()))
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self.run_host, daemon=True)
        self.thread.start()
        self.children = []

    def run_host(self):
        while not self.stopped.is_set():
            self.host.step(.02)

    def tearDown(self):
        for child in self.children:
            child.kill()
            child.wait()
        self.stopped.set()
        self.thread.join()
        self.host.close()
        self.temp.cleanup()

    def wait(self, condition, seconds=15):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            value = condition()
            if value:
                return value
            time.sleep(.05)
        self.fail('condition not reached in time')

    def bot(self, name):
        child = subprocess.Popen([BOT, '--server', '127.0.0.1', '--port', str(self.host.port), '--name', name, '--seconds', '60'],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.children.append(child)
        return child

    def game(self, identity):
        return next((l for l in public.browse(self.cache).lobbies if l.game == identity), None)


class Games(Hosted):
    def test_a_game_is_started_listed_joined_through_the_one_port_and_kept_apart_from_another(self):
        self.assertEqual(public.browse(self.cache), public.Listing((), (VERSION,), True))
        address, port, first = public.host(self.cache, Preferences(name='Ada', lan_bots=2, lan_teams=True, lan_score_limit=500), VERSION)
        self.assertEqual((address, port), ('127.0.0.1', self.host.port))
        lobby = self.wait(lambda: self.game(first))
        self.assertEqual((lobby.name, lobby.players, lobby.max_players, lobby.version, lobby.address, lobby.port),
                         ("Ada's game", 0, 16, VERSION, '127.0.0.1', self.host.port))
        command = self.host.games[first].process.args
        self.assertEqual(command[1:5], ['--bind', '127.0.0.1', '--port', str(self.host.games[first].port)])
        self.assertEqual(command[-3:], ['--teams', '--score-limit', '500'])
        # host() has already said this address is coming: a client that connects
        # to the public port is relayed to the game and counted in it.
        self.bot('first')
        self.wait(lambda: self.game(first).players == 1)
        # The AI opponents asked for arrive with the first pilot.
        self.assertEqual(self.game(first).bots, 2)
        # A second game; the pilot who joins it lands there and not in the first.
        _, _, second = public.host(self.cache, Preferences(name='Bob', lobby='Second <game>'), VERSION)
        self.assertNotEqual(first, second)
        self.assertEqual(self.wait(lambda: self.game(second)).name, 'Second <game>')
        self.bot('second')
        self.wait(lambda: self.game(second).players == 1)
        self.assertEqual(self.game(first).players, 1)
        # Back into the first by asking for it.
        public.join(self.cache, first)
        self.bot('third')
        self.wait(lambda: self.game(first).players == 2)
        self.assertEqual(self.game(second).players, 1)
        self.assertEqual(len(self.host.flows), 3)

    def test_requests_are_checked_and_limited(self):
        with self.assertRaisesRegex(LauncherError, 'still getting ready for OpenFlightSim 10.0.0'):
            public.host(self.cache, Preferences(), '10.0.0')
        with self.assertRaisesRegex(LauncherError, 'no longer runs OpenFlightSim 0.6.3'):
            public.host(self.cache, Preferences(), '0.6.3')
        with self.assertRaisesRegex(LauncherError, 'has ended'):
            public.join(self.cache, 'deadbeef')
        self.assertEqual(self.host.games, {})
        session = public.ask('127.0.0.1', self.host.port, {'op': 'list'})
        create = {'op': 'create', 'version': VERSION, 'name': 'x', 'bots': 0, 'missile_reload': 0, 'teams': False, 'score_limit': 300}
        # Without the token sent to this address, nothing is started.
        self.assertEqual(public.ask('127.0.0.1', self.host.port, {**create, 'token': '0' * 16})['error'], 'token')
        for wrong in ({'bots': 9}, {'bots': True}, {'missile_reload': -1}, {'score_limit': 10}, {'teams': 1}, {'version': '../../bin'}):
            self.assertEqual(public.ask('127.0.0.1', self.host.port, {**create, 'token': session['token'], **wrong})['ok'], False)
        self.assertEqual(self.host.games, {})
        # One address may run two games, and the whole server a fixed number.
        public.host(self.cache, Preferences(), VERSION)
        public.host(self.cache, Preferences(), VERSION)
        with self.assertRaisesRegex(LauncherError, 'already have a public game'):
            public.host(self.cache, Preferences(), VERSION)
        self.assertEqual(len(self.host.games), 2)

    def test_a_repeated_question_does_not_start_a_second_game_and_answers_never_outgrow_questions(self):
        session = public.ask('127.0.0.1', self.host.port, {'op': 'list'})
        body = public_host.MAGIC + json.dumps({'op': 'create', 'token': session['token'], 'version': VERSION, 'name': 'Once',
                                               'nonce': 77}).encode()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as link:
            link.settimeout(2)
            link.connect(('127.0.0.1', self.host.port))
            # A question that is not padded to full size is not answered at all.
            link.send(body)
            with self.assertRaises(socket.timeout):
                link.settimeout(.3)
                link.recv(2048)
            link.settimeout(2)
            answers = []
            for _ in range(3):
                link.send(body.ljust(public_host.REQUEST_SIZE))
                answers.append(link.recv(2048))
        self.assertEqual(len(set(answers)), 1)
        self.assertLessEqual(len(answers[0]), public_host.REQUEST_SIZE)
        self.assertEqual(len(self.host.games), 1)
        # Game traffic from an address that asked for no game goes nowhere.
        self.host.routes.clear()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as link:
            link.sendto(b'\x20' + bytes(40), ('127.0.0.1', self.host.port))
        time.sleep(.2)
        self.assertEqual(self.host.flows, {})


class EarthGames(Hosted):
    """Games on the Earth map are offered to launchers that know it, and only those."""

    def test_an_earth_game_needs_the_pack(self):
        earth = Preferences(name='Ada', map='earth', theatre='alps')
        with self.assertRaisesRegex(LauncherError, 'does not have the Earth map yet'):
            public.host(self.cache, earth, VERSION)
        session = public.ask('127.0.0.1', self.host.port, {'op': 'list'})
        create = {'op': 'create', 'token': session['token'], 'version': VERSION, 'name': 'x', 'map': 'earth', 'theatre': 'alps'}
        for wrong in ({'theatre': 'moon'}, {'theatre': None}, {'map': 'mars'}, {'bots': 2}, {'teams': True}):
            self.assertEqual(public.ask('127.0.0.1', self.host.port, {**create, **wrong})['ok'], False)
        self.assertEqual(self.host.games, {})

    def test_bots_and_teams_on_the_earth_need_0_7_0_1(self):
        (self.host.state / 'earth').mkdir(parents=True, exist_ok=True)
        (self.host.state / 'earth' / 'earth.ofse').write_bytes(b'pack')
        (self.host.builds / '0.7.0').mkdir(parents=True)
        (self.host.builds / '0.7.0' / 'ofs_server').symlink_to(SERVER)
        session = public.ask('127.0.0.1', self.host.port, {'op': 'list'})
        create = {'op': 'create', 'token': session['token'], 'version': '0.7.0', 'name': 'x', 'map': 'earth', 'theatre': 'alps'}
        for fight in ({'bots': 2}, {'teams': True}):
            reply = public.ask('127.0.0.1', self.host.port, {**create, **fight})
            self.assertEqual((reply['ok'], reply['error']), (False, 'version'))
            self.assertIn('0.7.0.1', reply['message'])
        self.assertEqual(self.host.games, {})

    def test_moscow_needs_0_7_3(self):
        (self.host.state / 'earth').mkdir(parents=True, exist_ok=True)
        (self.host.state / 'earth' / 'earth.ofse').write_bytes(b'pack')
        (self.host.builds / '0.7.2').mkdir(parents=True)
        (self.host.builds / '0.7.2' / 'ofs_server').symlink_to(SERVER)
        session = public.ask('127.0.0.1', self.host.port, {'op': 'list'})
        create = {'op': 'create', 'token': session['token'], 'version': '0.7.2', 'name': 'x', 'map': 'earth', 'theatre': 'moscow'}
        reply = public.ask('127.0.0.1', self.host.port, create)
        self.assertEqual((reply['ok'], reply['error']), (False, 'version'))
        self.assertIn('0.7.3', reply['message'])
        self.assertEqual(self.host.games, {})

    def test_older_launchers_see_only_valley_games(self):
        pack = ROOT / 'build' / 'earth' / 'earth.ofse'
        if not pack.is_file():
            self.skipTest('no Earth terrain pack')
        (self.host.state / 'earth').mkdir(parents=True, exist_ok=True)
        (self.host.state / 'earth' / 'earth.ofse').symlink_to(pack)
        _, _, valley = public.host(self.cache, Preferences(name='Valley'), VERSION)
        _, _, alps = public.host(self.cache, Preferences(name='Alps', map='earth', theatre='alps'), VERSION)
        command = self.host.games[alps].process.args
        self.assertEqual(command[-6:], ['--map', 'earth', '--theatre', 'alps', '--earth-pack', str(self.host.state / 'earth' / 'earth.ofse')])
        # The Earth game loads its pack before it answers.
        lobby = self.wait(lambda: self.game(alps), seconds=30)
        self.assertEqual((lobby.map, lobby.theatre), ('earth:alps', 'alps'))
        self.assertEqual(self.wait(lambda: self.game(valley)).map, '')
        # A launcher that does not say it knows the Earth map is not offered it.
        old = public.ask('127.0.0.1', self.host.port, {'op': 'list'})
        self.assertEqual([g['id'] for g in old['games']], [valley])
        self.assertNotIn('map', old['games'][0])


class Emptied(Hosted):
    limits = dict(empty_seconds=1.5, flow_seconds=1)

    def test_a_game_closes_once_the_last_pilot_has_gone(self):
        _, _, identity = public.host(self.cache, Preferences(name='Ada'), VERSION)
        pilot = self.bot('only')
        self.wait(lambda: self.game(identity) and self.game(identity).players == 1)
        process, log = self.host.games[identity].process, self.host.games[identity].log
        time.sleep(2.5)
        self.assertIn(identity, self.host.games)  # still flown in
        pilot.kill()
        self.wait(lambda: identity not in self.host.games, 30)
        self.wait(lambda: process.poll() is not None)
        self.assertEqual(self.host.flows, {})
        self.assertFalse(log.exists())
        self.assertEqual(public.browse(self.cache).lobbies, ())


if __name__ == '__main__':
    SERVER, BOT = str(Path(sys.argv[1]).resolve()), str(Path(sys.argv[2]).resolve())
    unittest.main(argv=sys.argv[:1], verbosity=2)
