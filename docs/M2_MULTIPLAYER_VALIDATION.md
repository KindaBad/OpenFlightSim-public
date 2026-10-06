# M2 — Multiplayer Foundation

## Audit and implementation plan

Audited M1 validation, architecture/coordinate/build documentation, complete
Simulator/State/Controls, fixed-step scheduler, C API, native input/render loop,
renderer interpolation and regression suites before implementation. The core
contains complete value state (including spool and time), 120 Hz public ticks
with two 240 Hz substeps, NED/FRD doubles and an unchanged C ABI. There was no
serialization, socket code or multiplayer state. Existing render interpolation
was client-only and unsuitable for timestamped remote histories. The repository
was still unborn/untracked; no reset, asset regeneration or history rewrite.

Implementation plan: isolate GNS behind a small transport; define bounded explicit
serialization; implement reusable authoritative world, standalone server and bot;
add shared prediction/reconciliation/interpolation; connect the existing native
client to that path; exercise actual IP sockets plus impairment/soak/security
and profile increasing aircraft counts; preserve every M1 regression.

Measurements and final validation results are recorded below after execution.

## Architecture delivered

`ofs_core` remains byte-for-byte unchanged (all source/header SHA256 values checked
against the initial audit). `ofs_net` wraps Valve GameNetworkingSockets v1.6.0 and
explicit protocol v1. `ofs_server` is standalone and headless, with one shared-core
Simulator per player at 120 Hz. `ofs_client --server IP` uses that same simulator
for immediate prediction, authoritative rollback and target-tick replay; the
existing offline M1 mode remains. `ofs_bot` is a scripted network probe, not AI.

Inputs contain sequence, target server tick and all eleven M1 controls, including
trim/gear/steering. The connection owns the aircraft ID. Server ingress validates
finite/range values, ID ownership, immutable redundant commands, ordering, size,
future horizon and rates before fixed-tick simulation. Clients cannot supply
physics transforms. Full snapshots carry explicit double pose/velocities,
spool/time, applied controls and retired input sequence. Default 24 Hz snapshots
are separate from 120 Hz physics; 20/24/25/30/60 Hz scheduling was tested.

Remote histories cap at 32 states, render ~100 ms behind estimated server time,
interpolate position and same-hemisphere quaternions, and extrapolate at most
50 ms before freezing. Local history/pending controls cap at 512 ticks; every
fresh snapshot restores authority and replays the remaining target ticks through
`Simulator`. Display-only corrections decay over 120 ms. No cross-platform
bitwise determinism assumption. Full technical details:
[NETWORK_PROTOCOL.md](NETWORK_PROTOCOL.md), [ARCHITECTURE.md](ARCHITECTURE.md).

## Exercised behavior and trust boundaries

The automated connection tests use real GNS **IP loopback sockets**, not a fake
transport or CreateSocketPair. They exercise connection encryption/handshake,
two stable IDs and separate authoritative aircraft, opposite control responses,
snapshot observation by the other client, live joining and removal after leave.
The same tests check client prediction responds before receiving server response.
A deliberate 5 m local state perturbation is corrected over the network.

Protocol tests round-trip all nine message types; check a golden big-endian
header and preservation of billion-meter fractional double positions; reject
every truncation, trailing bytes, bad version, oversized message/counts, duplicate
entities and nonfinite state. All eleven serialized controls are individually
tested against NaN, infinity and out-of-range values. 50,000 malformed random
packets are parsed defensively. World tests cover immutable duplicates, atomic
batch rejection, reordered inputs, future/sequence abuse, tick application,
64-player admission, reusable nonoverlapping spawn slots and stable IDs.

A hostile **connected client** sends NaN, infinity, positive/negative control
range violations, another player's ID, a huge sequence jump, a far-future tick
and a malformed message. It is removed after eight invalid messages, while two
valid clients continue flying. Additional real-socket tests verify server full,
immediate version rejection, 5-second missing-Hello timeout, 300-message burst
rate rejection, reconnection with a new ID and server shutdown cleanup. Ground
spawns settle through existing M1 contact physics. This establishes trust/limits,
not account authentication, anti-cheat, volumetric DoS resilience or hostile
Internet deployment hardening.

## Impairments and convergence

GNS's actual packet-level simulator applies send lag/jitter/loss/reordering to
both directions inside each test process. Twelve-second trials cover good
(15 ms send lag, 0.1% loss), moderate (50 ms, 2%) and bad (100 ms, 5%), with jitter
and optional reordering as documented in the protocol. Nominal RTT is roughly
30/100/200 ms plus jitter. GNS randomness varies between runs.

Every configuration verifies both clients remain connected, own finite aircraft,
observe control-driven movement, keep bounded histories/queues, correct an
injected divergence and finish with same-authoritative-tick position error <0.5 m.
Acceptance bounds are <20 m peak correction/error, <50 m remote recovery displacement beyond the elapsed-time motion allowance,
≥12 snapshots/s average, pending ≤512, server future queue ≤120 observed records,
32 history samples per remote, and authoritative drift <24 ticks over each wall
run. These are boundedness/convergence checks, not a claim of imperceptible motion
under every impairment. Latest measured same-tick errors printed as 0.000000 m;
all computations remain tolerant of small floating-point differences.

A missing snapshot is bridged from history; exhausted history extrapolates only
50 ms. Bad-condition trials exhibited visible-sized recovery steps of roughly
8–13 m at ~110 m/s after loss/jitter gaps, despite staying finite and converging.
Moderate soaks typically showed submeter updates but occasional larger recovery
steps. A more adaptive render delay/recovery smoothing policy is a remaining
multiplayer improvement. No indefinite extrapolation or accumulating divergence.

## Soak and memory

The real network soak runs **180 wall-clock seconds** under moderate impairment
with two controlling clients, a transient third player, repeated snapshots,
fixed 120 Hz authority and injected prediction divergence. Debug/Release/headless/
sanitizer configurations each completed 21,600 ticks; all entities disappeared
on disconnect. Snapshot histories and both client/server input queues stayed
bounded; no tick drift accumulated. The core-only accelerated network world test
also runs **420 simulated seconds / 50,400 ticks**, with 42 extra join/leave cycles
and input omissions, checking every aircraft for finite state and bounded queues.

An independent Release 180-second socket soak sampled process RSS once per second:
180 samples, peak **17,396 KiB**; after the first 30 s, min/max/final were all
**17,396 KiB**. No upward memory trend was observed. This is the whole combined
server/two-client test process on Linux, not a per-client allocation guarantee.
ASan leak detection was enabled, with no project memory/undefined-behavior reports.

## Final build and test matrix

All configurations configured and built successfully on 2026-09-30. Final native
validation ran the 32-test non-graphical suite and the one graphical test
separately, to avoid desktop interference; their combined result is 33/33.

| Configuration | Final CTest result | Three-minute network soak | Graphical smoke |
|---|---|---|---|
| GCC Debug native | 32/32 + 1/1 = 33/33 | Passed, 180.10 s | Passed, 3.36 s |
| GCC Release native | 32/32 + 1/1 = 33/33 | Passed, 180.08 s | Passed, 3.18 s |
| GCC Debug headless | 31/31 | Passed, 180.09 s | No graphics dependency |
| GCC Debug headless ASan + UBSan | 31/31 | Passed, 180.37 s | No graphics dependency |
| Core only, network/client disabled | 19/19 | N/A | N/A |
| Release offline native client, networking disabled | 20/20 | N/A | Not registered |

Sanitizer acceptance used `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`. No sanitizer reports in the final runs. Project
incremental builds emitted no warnings/errors. Upstream/system libraries remain
uninstrumented. The headless server's dynamic dependency list contained no SDL,
GLM, bgfx or ImGui. All existing M1 suites/tolerances remain unchanged, including
the uninterrupted 420-second flight cycle and tested extreme-condition matrix.

Final Release impairment measurements (including connection/startup):

| Preset/run | Authoritative ticks | Received snapshots | Peak same-time error/correction m | Final same-tick position error m | Peak pending inputs | Peak server queue | Largest sampled remote displacement m | Longest snapshot gap s |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| local connection, 10 s | 1,200 | 240 | 5.000000 injected | 0.000000 | 24 | 19 | 1.0588 | .0465 |
| good, 12 s | 1,440 | 285 | 5.000000 injected | 0.000000 | 25 | 16 | .4900 | .0513 |
| moderate, 12 s | 1,440 | 274 | 5.000000 injected | 0.000000 | 38 | 12 | 1.6138 | .1000 |
| bad, 12 s | 1,440 | 246 | 5.000000 injected | 0.000000 | 54 | 10 | 12.3096 | .1599 |
| moderate soak, 180 s | 21,600 | 4,217 | 5.000000 injected | 0.000000 | 42 | 12 | 4.0290 | .1568 |

Final Debug/headless/sanitizer soaks received 4,213/4,208/4,214 snapshots and had
peak pending histories 41/40/43. Every final soak had zero tick error and zero
remaining entities after leave. The independently sampled final Release soak
received 4,224 snapshots, with a flat 17,396 KiB post-warm-up RSS. Exact console
logs/CSV/profiles are retained locally in ignored `.cache/m2`; these documented
results and reproducible tools are the source deliverables.

## Bandwidth and capacity measurements

Fedora 44, GCC 16.2.1, Intel Core Ultra 7 255H, x86-64; Release measurements on the
development workstation, with other validation running. Timings are not controlled
hardware guarantees. Pure world benchmark uses 7,200 ticks (60 simulated seconds)
per count and one snapshot every five ticks, with M1 trimmed airborne aircraft.

| Aircraft | Physics mean µs/tick | p95 µs | Snapshot creation mean µs | Encoding mean µs | Snapshot bytes | Full fanout payload MB/s at 24 Hz |
|---:|---:|---:|---:|---:|---:|---:|
| 2 | 2.522 | 3.473 | 0.145 | 1.332 | 402 | 0.019 |
| 8 | 3.500 | 4.080 | 0.159 | 1.537 | 1,530 | 0.294 |
| 16 | 6.873 | 6.850 | 0.326 | 3.194 | 3,034 | 1.165 |
| 32 | 13.766 | 13.727 | 0.656 | 5.156 | 6,042 | 4.640 |
| 64 | 27.491 | 27.558 | 1.280 | 11.658 | 12,058 | 18.521 |

Pure physics/snapshot/encoding values exclude transport, client simulation and
OS scheduling. Whole pure benchmark peak RSS was 8,044 KiB. Player value storage
at 64 aircraft was 140,288 bytes before map/input allocations; not total memory.

The real GNS load tool connected 2/8/16/32/64 clients for **8 wall seconds per
count**. Every client received all other aircraft; every run reached 960 server
ticks, retained finite state and removed every entity on leave. Zero send failures.
Timings below include the server invocation amid prediction, packet work and
other processes; the physics measurement includes scheduler preemption.

| Clients | Server physics mean / p95 µs | Snapshot encode/send mean µs | Server payload in B/s | Server payload out B/s | GNS wire in B/s | GNS wire out B/s |
|---:|---:|---:|---:|---:|---:|---:|
| 2 | 13.556 / 30.932 | 44.109 | 153,401 | 19,404 | 160,647 | 21,287 |
| 8 | 25.872 / 58.083 | 67.047 | 584,292 | 294,288 | 610,074 | 308,827 |
| 16 | 38.458 / 51.260 | 91.521 | 1,224,505 | 1,166,368 | 1,284,474 | 1,208,266 |
| 32 | 71.481 / 95.951 | 180.851 | 2,338,569 | 4,619,736 | 2,451,212 | 4,757,362 |
| 64 | 136.497 / 183.276 | 754.030 | 4,985,101 | 18,436,016 | 5,245,814 | 18,964,364 |

Payload figures include startup/lifecycle; GNS wire figures are end-window rates
summed from client links (exclude IP/UDP framing). At two clients each downstream
is ~10–11 kB/s wire. Input redundancy costs ~73–110 kB/s upstream **per client**
in the measured scenarios, increasing with pending history/RTT. This is a clear
future optimization opportunity; no compression/delta/interest policy is claimed.
The 64-client tool, including all clients, peaked at **52,136 KiB RSS**. Queue peak
was 17–18 commands in the local load trials. Tested transport capacity is 64
loopback peers for eight seconds, not 64 Internet players for hours. Immediate
product target remains small friend groups, with default admission at 16.

## Graphical and standalone execution

Launched a standalone Release `ofs_server` and two separate native client
processes on real GNS IP connections. Both displayed the other aircraft and
received 192 snapshots during eight-second sessions. Opposite scripted roll
pulses produced approximately +7.34° and −7.30° final roll; 448/449 frames had a
remote aircraft render submission. The server recorded two independent players,
no invalid inputs/send failures/overload, and two clean disconnects. Screenshot
capture was inspected visually: both transport placeholders and live diagnostic
rates/IDs are visible. Screenshots: `images/m2-client-A.png` and
`images/m2-client-B.png`, generated captures that are not version controlled.

The graphical test camera follows the formation only in `--network-smoke`, so the
captured aircraft remain visible; normal free-camera behavior is unchanged.
This demonstrates native network/render integration, not physical controller
certification or WAN play. An additional standalone `--ground --snapshot-hz 30`
server and two separate `ofs_bot` processes ran successfully for ten seconds;
both saw the other entity and 30 Hz snapshots. One bot explicitly enabled moderate
one-direction send impairment and reported approximately 50 ms RTT.

## Validation repair notes

Initial GNS IP tests attempted port zero, which upstream rejects. Test-only
random high-port binding with retries now avoids fixed-port conflicts. Initial
network divergence injection was corrected by exact replay before its historical
tick reached authority, so the diagnostic counter missed it; counters now also
measure the actual current-state correction after replay at the same tick;
catching up to a newer authoritative tick is elapsed motion, not divergence. The latest error
still compares the same authoritative tick. Initial UBSan vptr checks crossed an
upstream RTTI-disabled library boundary; sanitizer GNS C++ builds now enable RTTI
without disabling project sanitizer checks.

A final adverse test could finish its wall-time control pulse before a delayed
handshake; its script now starts relative to completed two-client connection.
A repeated soak encountered long process scheduling gaps; the continuity check
now allows physical displacement `speed × elapsed` while retaining the same
50 m maximum unexplained recovery bound. It does not weaken M1 flight tolerances.
Desktop focus also caused an offline smoke ImGui click to be missed. Smoke now
raises the window and submits explicit mouse motion before the actual click,
then separate sequential graphical reruns pass. No physics/control assertions
were bypassed. Severe packet gaps still permit the recovery limitations above.

## Windows and remaining weaknesses

Windows 11/MSVC compilation and execution were unavailable and remain unverified.
GNS and application source use portable APIs, standard C++ timing and deliberate
endianness; Windows dependency/toolchain instructions are provided. Fedora/GCC is
the verified path. IPv6 uses GNS address parsing but only IPv4 runtime was tested.
Clang, physical controllers and real inter-machine/WAN conditions are unverified.

Current limits: 64 aircraft hard bound, 16 players by default, full-world quadratic
fanout, expensive repeated input batches, approximate clock synchronization,
fixed 100 ms interpolation delay and visible freeze/recovery under severe gaps,
no adaptive packet-gap smoothing/interest management/compression, numeric IP only,
manual reachable server port, no discovery/relay/account authentication, fixed
M1 aircraft/calm atmosphere, static placeholder gear, and no automatic reconnect
UI. Transport and histories are bounded; long-stall recovery may rebase prediction
or snap a display correction at ≥50 m. Same-tick convergence does not guarantee
identical trajectories on different CPUs. Pure model/capacity tests remain small
and short enough that no broad production scalability claim is justified.

## Recommended M3 — Combat Foundation (not implemented)

Keep M2 authority and add exactly one initial gun, with rate/ammo/cooldown
validation and a tick/sequence-labelled fire command in a versioned protocol
extension. Allocate projectile IDs from the stable entity namespace. Advance
simple authoritative projectile ballistics on the fixed clock and perform swept
segment tests against explicit aircraft hitboxes; avoid importing a general
collision engine unless measurements justify it. Let the server own hits,
damage and a minimal destroyed-aircraft lifecycle. Replicate projectile/combat
creation/removal and damage events, with display-only muzzle/impact effects.

Add bounded authoritative history/lag compensation only for the chosen hit model,
with strict rewind limits and no client-reported hits. Write headless tests for
rate/ownership validation, high-speed hit/miss geometry, damage replication,
destruction cleanup and adverse-network/soak regression while preserving M1/M2.
Defer missiles, radar, teams, scoring, matchmaking/accounts, voice, terrain, AI,
complex damage systems and respawn gameplay. M2 ends here; no combat code exists.
