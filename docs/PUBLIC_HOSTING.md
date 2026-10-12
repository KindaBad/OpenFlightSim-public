# Public games

From 0.6.3 a player can host a game that anyone can join over the internet.
The game does not run on the player's computer: it runs on the public host, a
Linux computer on a home connection with one UDP port forwarded to it. This
describes that host, the conversation the launcher has with it, and how to run
one. The player's side is in [LAUNCHER.md](LAUNCHER.md).

## How it fits together

```
launcher ──HTTPS──> public.json (GitHub)          where is the host?
launcher ──UDP 27020, OFSPUB──> public_host.py    list / create / join
ofs_client ──UDP 27020──> public_host.py ──loopback──> ofs_server (one per game)
```

`hosting/public_host.py` owns the forwarded port. A datagram that begins with
`OFSPUB\x01` is a question from a launcher; anything else is game traffic,
which it relays to and from the game that the sending address asked to join.
Each game is an ordinary `ofs_server` bound to a loopback port
(`--bind 127.0.0.1 --port 27100+n --lan-name NAME --discovery-port 27200+n`),
so no game is reachable except through the relay. The host asks every game how
full it is over its discovery port once every two seconds and stops a game
that has had no pilot and no relayed traffic for five minutes, that exits, or
that never answers.

A player is matched to a game by address. `join` records that the asking
address is about to enter a game, for five minutes; the next new conversation
from that address (a source address and port not seen before) is relayed to
it, and stays with it until twenty seconds of silence. Players behind one
router share an address: each `join` waits its turn, so two of them heading
for different games at the same moment are told apart by order, and a
conversation that arrives with no `join` waiting goes where the last one went.
The game sees every player as a loopback address and applies its own
connection, version and identity checks as it would to anyone.

## The launcher's questions

Every question is one datagram of exactly 1200 bytes: `OFSPUB\x01`, a JSON
object, then spaces. Every answer is `OFSPUB\x01` and a JSON object of at most
1200 bytes that repeats the question's `nonce`. Questions of any other size
are not answered, so a forged source address never receives more than was
sent.

| `op` | Fields | Answer |
|---|---|---|
| `list` | | `token`, `games` (`id`, `name`, `players`, `max`, `bots`, `version`, `protocol`, `teams`), `versions` the host can run, `capacity` |
| `create` | `token`, `version`, `name`, `bots` 0–8, `missile_reload` 0–3600, `teams`, `score_limit` 50–5000 | `id` |
| `join` | `token`, `id` | `id`, `ready` once the game has opened its port |

A refusal is `{"ok": false, "error": CODE, "message": TEXT}`; the launcher
shows the text if it is plain printable ASCII. The `token` is derived from the
asking address and a secret made when the host starts, and lasts ten to twenty
minutes. `create` and `join` need it, so they cannot be made in the name of an
address that did not receive it. A repeated `create` (same address and nonce)
gets the first answer again instead of a second game.

Limits: six games at once, sixteen pilots a game, two running games started
from one address and fifteen seconds between them, ten questions a second from
one address, 256 relayed players in all and 32 from one address.

## Versions

A game is started with the server built for the version of the player who
asks, so a player who has not updated yet can still host for others on the
same version. `hosting/build_server.py`, run every few minutes, looks at the
main branch of the public source repository and builds `ofs_server` when the
version there is new or the server's sources have changed. That happens when
the source is pushed, which is before the players' update has finished
publishing. The newest three versions are kept.

## Where the host is

`hosting/publish_address.py`, run every five minutes, asks independent
services for this computer's public address and, when two agree on one that
differs from the published one, commits it to `public.json` in
`KindaBad/OpenFlightSim-servers` with a deploy key that can write to that
repository only. The launcher reads that file.

## Running the host

On a Debian or Ubuntu computer, from a copy of `hosting/`:

```sh
sudo sh ./install.sh
```

This installs the build tools, creates the unprivileged `ofs-public` user,
puts the three scripts in `/opt/ofs-public`, keeps everything they write under
`/var/lib/ofs-public`, opens UDP 27020 in ufw and enables
`ofs-public-host.service` with the `ofs-public-build` and
`ofs-public-address` timers. It prints a deploy key to add, with write access,
to the address repository. The router has to forward UDP 27020 to the
computer. Running the installer again updates the scripts; it restarts the
host, which ends the games in progress.

```sh
journalctl -u ofs-public-host -f        # games started, relays, a line every five minutes
systemctl start ofs-public-build        # look for a new version now
ls /var/lib/ofs-public/builds           # versions it can run
```

A game that exits with an error leaves its log in
`/var/lib/ofs-public/host/logs`.

## Tests

`tests/public_host_tests.py` (CTest `lan.public_host`) runs the host on
loopback with the real `ofs_server` and `ofs_bot`: games are started, listed
and joined through the one port, pilots heading for different games arrive in
the right ones, limits and malformed questions are refused, and an emptied
game is closed. `launcher/tests/test_public.py` covers the directory file and
the launcher's reading of answers; `launcher/tests/test_ui.py` covers the
page.
