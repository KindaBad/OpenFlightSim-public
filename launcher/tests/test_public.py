"""Public games: the directory file, the server's answers and the Multiplayer page's use of them."""
import json
from pathlib import Path
import socket
import tempfile
import threading
import unittest

from launcher import lan, public
from launcher.config import Preferences
from launcher.storage import LauncherError, read_json, write_json


class Server(threading.Thread):
    """Stands in for the public server: answers each question with what `answer` returns."""
    def __init__(self, answer):
        super().__init__(daemon=True)
        self.answer, self.requests = answer, []
        self.link = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.link.bind(('127.0.0.1', 0))
        self.link.settimeout(.05)
        self.port = self.link.getsockname()[1]
        self.stopped = False
        self.start()

    def run(self):
        while not self.stopped:
            try:
                data, address = self.link.recvfrom(2048)
            except OSError:
                continue  # nothing yet, or on Windows an earlier answer that found nobody listening
            request = json.loads(data[len(public.MAGIC):])
            self.requests.append((len(data), request))
            reply = self.answer(request)
            if reply is not None:
                self.link.sendto(reply if isinstance(reply, bytes) else public.MAGIC + json.dumps({'nonce': request['nonce'], **reply}).encode(), address)

    def close(self):
        self.stopped = True
        self.join()
        self.link.close()


GAME = dict(id='0a1b2c3d', name='Friday night', players=3, max=16, bots=2, version='0.6.3', protocol=16, teams=True)


class Directory(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.cache = Path(self.temp.name) / 'public-server.json'

    def tearDown(self):
        self.temp.cleanup()

    def test_only_a_numeric_address_and_port_are_accepted(self):
        self.assertEqual(public.endpoint(dict(schema=1, address='203.0.113.7', port=27020)), ('203.0.113.7', 27020))
        for wrong in (None, [], dict(schema=2, address='203.0.113.7', port=27020), dict(schema=1, address='example.org', port=27020),
                      dict(schema=1, address='203.0.113.7', port=0), dict(schema=1, address='203.0.113.7', port='27020'),
                      dict(schema=1, address='203.0.113.7', port=True), dict(schema=1, address='0.0.0.0', port=27020),
                      dict(schema=1, address='255.255.255.255', port=27020), dict(schema=1, address='2001:db8::1', port=27020),
                      dict(schema=1, address='203.0.113.7 --bots 8', port=27020)):
            self.assertIsNone(public.endpoint(wrong))

    def test_the_address_is_fetched_remembered_and_survives_an_unreachable_directory(self):
        calls = []

        def fetch(url):
            calls.append(url)
            return dict(schema=1, address='203.0.113.7', port=27021)
        self.assertEqual(public.locate(self.cache, fetch=fetch, now=lambda: 1000), ('203.0.113.7', 27021))
        self.assertEqual(calls, [public.DIRECTORY_URL])
        self.assertTrue(public.DIRECTORY_URL.startswith('https://'))
        # Fresh enough: not asked again, unless the server did not answer.
        self.assertEqual(public.locate(self.cache, fetch=fetch, now=lambda: 1500), ('203.0.113.7', 27021))
        self.assertEqual(len(calls), 1)
        public.locate(self.cache, fetch=fetch, now=lambda: 1500, refresh=True)
        self.assertEqual(len(calls), 2)
        public.locate(self.cache, fetch=fetch, now=lambda: 1510, refresh=True)
        self.assertEqual(len(calls), 2)

        def broken(url):
            raise OSError('no route')
        self.assertEqual(public.locate(self.cache, fetch=broken, now=lambda: 9000), ('203.0.113.7', 27021))
        self.assertEqual(read_json(self.cache)['checked'], 9000)
        # A directory that says something else entirely changes nothing.
        self.assertEqual(public.locate(self.cache, fetch=lambda url: {'address': 'evil.example'}, now=lambda: 20000), ('203.0.113.7', 27021))

    def test_a_computer_that_never_reached_the_directory_uses_the_built_in_address(self):
        def broken(url):
            raise LauncherError('offline')
        self.assertEqual(public.locate(self.cache, fetch=broken), (public.DEFAULT_ADDRESS, public.DEFAULT_PORT))
        self.assertEqual(public.endpoint(dict(schema=1, address=public.DEFAULT_ADDRESS, port=public.DEFAULT_PORT)),
                         (public.DEFAULT_ADDRESS, public.DEFAULT_PORT))
        self.cache.write_text('not json')
        self.assertEqual(public.locate(self.cache, fetch=broken), (public.DEFAULT_ADDRESS, public.DEFAULT_PORT))


class Conversation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.cache = Path(self.temp.name) / 'public-server.json'
        self.server = None

    def tearDown(self):
        if self.server:
            self.server.close()
        self.temp.cleanup()

    def serve(self, answer):
        import time
        self.server = Server(answer)
        write_json(self.cache, dict(schema=1, address='127.0.0.1', port=self.server.port, checked=time.time()))
        return self.server

    def test_games_are_listed_and_anything_malformed_is_left_out(self):
        games = [GAME, {**GAME, 'id': '../etc'}, {**GAME, 'name': 'bell\x07'}, {**GAME, 'players': 17}, {**GAME, 'protocol': '16'},
                 {**GAME, 'name': 'x' * 49}, 'text', {**GAME, 'id': 'ffffffff', 'name': 'Second'}]
        server = self.serve(lambda request: dict(ok=True, token='t' * 16, games=games, versions=['0.6.3', 7, 'bad\n']))
        listing = public.browse(self.cache)
        self.assertTrue(listing.reachable)
        self.assertEqual(listing.versions, ('0.6.3',))
        self.assertEqual(listing.lobbies, (lan.Lobby('Friday night', '127.0.0.1', server.port, 16, 3, 16, 2, '0.6.3', '0a1b2c3d'),
                                           lan.Lobby('Second', '127.0.0.1', server.port, 16, 3, 16, 2, '0.6.3', 'ffffffff')))
        # Every question is padded to full size, so the server never answers with more than it was sent.
        self.assertEqual({size for size, _ in server.requests}, {public.REQUEST_SIZE})
        line = lan.describe(listing.lobbies[0], 16)
        self.assertIn('Public', line)
        self.assertNotIn('127.0.0.1', line)

    def test_an_answer_to_another_question_or_from_a_silent_server_is_not_used(self):
        self.serve(lambda request: public.MAGIC + json.dumps(dict(ok=True, nonce=request['nonce'] + 1, games=[GAME])).encode())
        self.assertEqual(public.browse(self.cache), public.Listing())
        self.assertIsNone(public.ask('127.0.0.1', self.server.port, {'op': 'list'}, timeout=.1, attempts=2))
        self.assertEqual(len(self.server.requests), 3)

    def test_hosting_sends_the_host_settings_then_joins(self):
        def answer(request):
            if request['op'] == 'list':
                return dict(ok=True, token='secret-token', games=[], versions=['0.6.3'])
            if request['op'] == 'create':
                return dict(ok=True, id='0a1b2c3d')
            return dict(ok=True, id=request['id'], ready=True)
        server = self.serve(answer)
        prefs = Preferences(name='Ada', lobby='', lan_bots=3, lan_missile_reload=60, lan_teams=True, lan_score_limit=500)
        self.assertEqual(public.host(self.cache, prefs, '0.6.3'), ('127.0.0.1', server.port, '0a1b2c3d'))
        create, join = server.requests[1][1], server.requests[2][1]
        del create['nonce'], join['nonce']
        self.assertEqual(create, dict(op='create', token='secret-token', version='0.6.3', name="Ada's game", bots=3,
                                      missile_reload=60, teams=True, score_limit=500))
        self.assertEqual(join, dict(op='join', token='secret-token', id='0a1b2c3d'))

    def test_refusals_are_shown_in_the_servers_words_when_they_are_plain_text(self):
        replies = {'create': dict(ok=False, error='full', message='The public server is running as many games as it can.'),
                   'join': dict(ok=False, error='gone', message='bad\x1b[31m')}
        self.serve(lambda request: dict(ok=True, token='t' * 16, games=[]) if request['op'] == 'list' else replies[request['op']])
        with self.assertRaisesRegex(LauncherError, 'as many games as it can'):
            public.host(self.cache, Preferences(), '0.6.3')
        with self.assertRaisesRegex(LauncherError, 'refused the request'):
            public.join(self.cache, '0a1b2c3d')
        with self.assertRaisesRegex(LauncherError, 'cannot be joined'):
            public.join(self.cache, 'not-an-id')

    def test_a_server_that_does_not_answer_is_reported_plainly(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as silent:
            silent.bind(('127.0.0.1', 0))
            import time
            write_json(self.cache, dict(schema=1, address='127.0.0.1', port=silent.getsockname()[1], checked=time.time()))
            original = public.ask
            try:
                public.ask = lambda *a, **k: original(*a, **{**k, 'timeout': .05, 'attempts': 1})
                with self.assertRaisesRegex(LauncherError, 'cannot be reached'):
                    public.host(self.cache, Preferences(), '0.6.3', fetch=lambda url: dict(schema=1, address='127.0.0.1', port=silent.getsockname()[1]))
            finally:
                public.ask = original
        self.assertEqual(public.browse(self.cache), public.Listing())


if __name__ == '__main__':
    unittest.main()
