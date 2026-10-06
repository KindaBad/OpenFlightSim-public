# M3 combat validation — 30 September 2026

Historical milestone report. The temporary Falcon has since been retired; current aircraft physics and evidence are documented in [AIRCRAFT_PHYSICS_AUDIT.md](AIRCRAFT_PHYSICS_AUDIT.md).
M0/M1 and M2 were extended in place. No core flight equations, transport, live
prediction/replay algorithm, physics rate or existing test thresholds were
replaced. No missiles or radar were implemented. Source audit and initial plan:
[M3_IMPLEMENTATION_PLAN.md](M3_IMPLEMENTATION_PLAN.md).

## Combat architecture and trust

`ofs_net` adds `combat.hpp/combat.cpp`, a reusable headless module. World remains
the 120 Hz authority over one Simulator per stable player ID. Client sends held
fire state with entity ownership, sequence, target tick and life generation;
Server validates the session/direction/rates, World queues due input, and Combat
checks readiness/ammo and derives the muzzle transform and velocity. Clients
cannot supply world muzzle, damage, projectile speed, target/hit, health, death
or respawn location. Rendering has no collision or health authority.

Fire input is immediate on transitions with 10 Hz unreliable heartbeats. Server
accepts at most 60 fire messages/s inside the existing 240 messages/s cap, 120
ticks ahead or old, and 512 sequence indices ahead. Duplicate/reordered commands
at or below high-water cannot refresh or refire the trigger. Fire queues cap at
128; latest state for one tick replaces its earlier state. Unrefreshed held input
expires after one second. Normal dead/previous-life in-flight input is discarded;
invalid future-generation/spoof/direction/tick/rate traffic consumes strikes.
Cooldown/ammo are always server-owned, independent of message frequency.

## Gun, rounds and hitboxes

One GunConfig (server configuration, not network-editable):

| Property | Default |
|---|---|
| Rate | 600 RPM; 12 ticks between shots at 120 Hz |
| Muzzle speed | 850 m/s |
| Dispersion | 0.0015 rad (~0.086°), deterministic server-owned angular pattern |
| Damage | 25 health per hit, every region |
| Lifetime / traveled range | 3 s / 2,400 m, whichever comes first |
| Muzzle / axis | body FRD (19,0,0) m / (1,0,0) |
| Ammunition | 600 rounds; restored on respawn |
| Health / respawn delay | 100 / 480 ticks (4 s) |

This arms the existing transport placeholder, not a calibrated real aircraft
weapon. Config permits different values without a weapon framework; finite/range
bounds are enforced. Rate intervals round up to whole ticks, never faster than
configured RPM. No reload action exists; an empty aircraft must respawn to refill.

Rounds use monotonically increasing uint64 projectile IDs in a separate namespace,
owner/life generation, world position/velocity, age and distance. Initial velocity
is authoritative aircraft velocity plus rotated muzzle velocity. Exact constant-
gravity integration advances at 120 Hz; rounds have no 6-DoF physics. Contiguous
storage reserves 4,096 values once and compacts in place. Rounds disappear on hit,
range/lifetime expiry, nonfinite state, owner death/leave/respawn. No posthumous
rounds persist. Event storage caps at 256; production Server drains every tick.
Peak counts, pool/event drops, shots/hits/kills/respawns and timings are exposed.

Seventeen explicit body-space spheres approximate fuselage (8), left/right wings
(4 each) and tail (1). They do not depend on the visual mesh. Sweeps use projectile
start/end relative to previous/current world sphere centers, so moving targets
also sweep. The earliest intersected aircraft/region receives damage once; the
round is consumed. Final sweeps clip at lifetime/range boundaries. A 24 m broad
sphere rejects distant pairs and centers are cached once per tick. Rotation uses
sphere-center chords over one tick, not exact curved sweeps. Owner is ignored
while age <=0.1 s OR traveled distance <=40 m; immunity is not permanent.

## Damage, destruction and respawn

Health is server-owned and clamped at zero. A hit records attacker, target, region,
impact and resulting health. First lethal hit increments deaths/kills once,
marks destroyed, schedules respawn and emits Destroyed. Dead bodies freeze and
normal flight/fire input cannot alter them. World clears input/fire queues,
retires received flight sequences and removes owner rounds.

After four seconds World restores full health/ammo, existing solved trim/controls
and the same stable ID/slot, increments life generation and emits Respawn. Spawn
candidates exclude live aircraft within 60 m; 150 m candidate spacing means each
other aircraft can exclude at most one candidate, so the bounded 65-candidate
search succeeds for the 64-player cap. An occupied-spawn regression verifies the
fallback. Scores persist; no entity duplication is introduced.

Input and Fire carry life generations. Delayed previous-life packets cannot
control a new life. Local alive/generation transitions clear prediction pending
commands/history and visual correction; remote generation transitions clear
interpolation history. Dead prediction freezes. Authoritative snapshots recover
health/lifecycle after packet loss; visual events cannot overwrite these values.
Stale-life visual spawns/effects are discarded and old destruction/respawn events
cannot remove current-life rounds.

## Protocol and client feedback

Explicit big-endian protocol is **v2**; v1 peers are rejected. Existing flight
Command fields stay unchanged; Input appends u32 generation. Aircraft records
increase from 188 to 222 bytes for health/ammo/generation/ready/respawn/kills/deaths.
Fire packets are 54 bytes. Combat batches contain 1..64 explicit 102-byte events
plus a 25-byte envelope, encoded once and fanned out via reliable ordered GNS.
Shot, Hit, Destroyed and Respawn have unique event IDs and authoritative ticks.
Separate combat sequence/event high-water marks prevent duplicate feedback and
avoid snapshot-channel reordering suppressing combat events. No per-round state
snapshots at 120 Hz. Exact layouts/limits: [NETWORK_PROTOCOL.md](NETWORK_PROTOCOL.md).

Clients reconstruct gravity tracers from authoritative Shot events; Hit consumes
visual rounds. Muzzle crosses, yellow tracer lines, white impact crosses and
expanding orange destruction crosses give basic feedback; destroyed aircraft
are hidden. These lines never participate in gameplay. A projected gun-axis
reticle and V nose camera help aim. Space / left mouse / right trigger fire.
Held input predicts reticle color only; health/hits remain authoritative.
Diagnostics show health, ammo, cooldown, life, scores, shots observed, received
hits, visual rounds and last target/region. Server prints rounds, shots/rate,
rejections, hits/kills/respawns, timings and combat bytes.

## Automated coverage and build totals

Actual Fedora/GCC builds and CTest acceptance (Intel Core Ultra 7 255H,
16 logical CPUs, GCC 16.2.1; rootless dependency setup from BUILDING.md):

| Configuration | Registered tests passed | Execution |
|---|---:|---|
| Debug native | 43 / 43 | 42 automated suites plus separately serialized desktop smoke |
| Release native | 43 / 43 | 42 automated suites plus separately serialized desktop smoke |
| Debug headless | 41 / 41 | final complete verbose run |
| ASan + UBSan headless | 41 / 41 | leak detection and halt-on-error enabled |
| Total across configurations | 168 / 168 | includes the preserved M0/M1/M2 suites |

The final occupied-spawn change additionally passed World/unit/lifecycle/protocol
checks in Debug, Release and sanitizer builds (4 each), then a complete headless
41-test run. Builds produced no project compiler warnings in the final build logs.
Existing network tests changed only the expected protocol version and default
Life aggregate initialization, not their assertions/tolerances. Flight/core files
are untouched. Native desktop smoke retained its original interactions/assertions.

Ten combat suites: unit, lifecycle, protocol, abuse, local, good, moderate, bad,
180-second soak and performance profile. Coverage includes rate/ammo/dead gating;
server muzzle/velocity rotation; exact gravity; high-speed endpoints-outside hit;
high-speed moving-target crossing; near miss; damage once; multiple attackers and
one lethal transition; region preservation; bounded owner grace; terminal range
clipping; invalid state/pool/event caps; expiry/leave cleanup; safe timed same-ID
respawn; dead freeze; old-life input retirement; prediction/interpolation reset;
codec roundtrips/truncation/trailing/enums/nonfinite fields and 10,000 mutations;
real GNS duplicate/reordered input, spoofing/future/sequence/generation/weapon/bool
abuse, malformed messages, flood limiting and isolated offender removal.

Instrumented scope is project core/network/client logic/tests. GNS and system/GPU
libraries are not instrumented. ASan quarantine is fixed at 16 MiB for bounded
allocator retention during RSS sampling; leak detection, ASan checks and UBSan
checks are not suppressed. No sanitizer findings in accepted runs.

Validation repairs: a reciprocal fixture initially reversed attitude without
velocity; fixing velocity restored meaningful head-on fire. A bad-network head-on
duel need not kill both aircraft symmetrically, so the soak scenario alternates
shooter/target roles after each respawn to exercise both clients' resets. Twenty-
second RSS checks initially failed as the ASan allocation quarantine warmed up;
RSS stability now applies to the long soak after 30 s. One concurrent headless M2
bad-preset run missed its short roll-pulse observation; its unchanged test passed
an isolated rerun and the subsequent complete headless run. GNS impairment
randomness remains unseeded; these results do not establish zero test flakiness.

## Impairments and lag-compensation decision

Actual two-client GNS IP combat runs, 20 wall seconds each; final headless run:

| Preset | Shots | Hits | Kills | Respawns | Peak rounds | First fire admission s | Max fresh hit delivery s |
|---|---:|---:|---:|---:|---:|---:|---:|
| local | 216 | 20 | 5 | 4 | 26 | 0.142 | 0.000 |
| good | 215 | 20 | 5 | 4 | 26 | 0.108 | 0.025 |
| moderate | 214 | 20 | 5 | 4 | 26 | 0.075 | 0.100 |
| bad | 201 | 18 | 4 | 4 | 26 | 0.108 | 0.533 |

These use the existing nominal 30/100/200 ms round-trip impairment presets with
jitter/loss/reordering. Both clients receive damage/life events and respawn
baselines, reset prediction, remain connected and keep bounded histories/queues.
The scenario arranges same-heading, moving aircraft after each life and alternates
the aimed role; between fixtures, normal physics/controls run. It is not AI or
manual pilot aim certification. Unit tests independently cover crossing targets.
Fresh delivery delay samples only newly observed hit events, not age of stale
last-hit HUD data. First admission measures one initial held-trigger request to
the first authoritative shot; it is not a full trigger-delay percentile study.

**No lag compensation added.** The tested aimed combat loop remains functional
under all presets, so results do not justify introducing rewind into the existing
physics/network path. Commands execute at validated due/current server time;
current moving hitboxes decide hits. Input lead adds ~0.1–0.15 s to first local
trigger admission, and bad-preset reliable hit feedback reached 0.533 s. Moving
human opponents, WAN aim quality and subjective responsiveness remain unverified;
bounded compensation may deserve a separate measured investigation later.

## Three-minute combat soak

Four real headless Client connections to the production Server over moderate-
impaired GNS for **180 wall seconds**. Two separated scripted pairs alternate
shooter/target roles, hold fire and send continuous trim controls; authoritative
fixtures arrange each new life without adding a client transform/hit API.

Final headless acceptance: **3,827 shots, 320 hits, 80 destructions, 78 respawns,
52 peak active rounds**, peak pending events 4, peak fire queue 1. Two aircraft
were still awaiting respawn near the timed stop, explaining kills versus respawns.
Combat fanout averaged 11,927.4 B/s across four clients. Maximum fresh hit event
delivery was 0.450 s. RSS at 30 s was **13,176 KiB**, subsequent sampled peak
**13,200 KiB** (+24 KiB). Every per-tick aircraft state remained finite; player
count, server inputs (256), fire inputs (128), local prediction (512), remote
histories (32 each), visual rounds (4,096) and pending events stayed bounded.
No pool/event overflow or send failures. All entities/rounds were removed on
client disconnect. The same long combat suite passed Debug, Release and ASan/UBSan;
the existing 180-second M2 network soak also passed all four configurations.

## Performance and bandwidth

Release pure World profile: 60 simulated seconds / 7,200 ticks for each count,
all aircraft continuously firing/missing (maximum normal lifetime occupancy).
Motion column includes once-per-tick cached hitbox transforms and round integration.
Collision column includes broad/narrow sweeps, damage/removal and compaction.

| Aircraft | Shots | Peak rounds | World tick mean µs | Cache/motion µs | Collision µs | Event encode µs/tick | Combat B/s per receiver | Snapshot B/s per receiver |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 2 | 1,200 | 52 | 4.740 | 2.926 | 0.525 | 0.024 | 2,290 | 11,280 |
| 8 | 4,800 | 208 | 12.831 | 4.179 | 4.716 | 0.052 | 8,410 | 43,248 |
| 16 | 9,600 | 416 | 29.593 | 6.013 | 16.029 | 0.100 | 16,570 | 85,872 |

There were zero hits in these deliberate-miss load cases. Region correctness and
damage work are measured by the integration/soak cases, not inferred from this
profile. Collision remains O(rounds × aircraft) but broad rejection/cached centers
keep the 16-player pure tick at ~0.36% of the 8,333 µs budget. No spatial database
was justified. This is small-group profiling, not a 64-player combat claim.

Actual Release GNS load, eight wall seconds/count, continuous firing, 960 server
ticks each, zero send failures, queue peak 17 and finite states:

| Clients | Tick mean / p95 µs | Snapshot encode/send µs | Combat encode µs/tick | Server input B/s | Server output B/s | Combat fanout B/s | Peak rounds |
|---|---:|---:|---:|---:|---:|---:|---:|
| 2 | 37.595 / 79.266 | 42.032 | 0.403 | 147,910 | 27,199 | 4,523 | 52 |
| 8 | 73.928 / 126.498 | 74.156 | 0.671 | 590,524 | 412,985 | 66,439 | 208 |
| 16 | 150.792 / 225.205 | 128.273 | 1.000 | 1,180,809 | 1,637,138 | 261,806 | 416 |

Real tick timing includes process scheduling amid clients/packet work; it is not
an isolated collision benchmark. Approximate combat collision means in this real
run were 2.405 / 19.527 / 67.082 µs at 2/8/16 clients. Pure figures above are more
useful for collision scaling. GNS measured aggregate wire out was 29,713 / 429,896 /
1,689,749 B/s respectively; these exclude IP/UDP framing. No Internet link claim.

Same v2 load without firing measured server outputs 22,677 / 346,546 / 1,375,332
B/s at 2/8/16 clients; firing adds approximately **20.0% / 19.2% / 19.0%**, almost
entirely the separately counted events. Client fire heartbeats add ~540 B/s while
alive and flight packets append four bytes. Life snapshot fields add 34 bytes per
aircraft (+18.1% record size over M2). No projectile-state snapshot fanout exists.

Additional non-combat v2 baseline: 32 clients produced **5,451,135 B/s** server
output; 64 produced **21,761,216 B/s**, still 960 ticks and no send failures. This
measures existing quadratic full-world fanout plus Life, not large combat capacity.
Upstream redundancy remains ~74 kB/s per client in local load and ~109 kB/s in the
M2 bad impairment test; M3 did not redesign that system. Pure event payload size
and real fanout measurements are recorded separately to avoid hiding growth.

## Graphical execution

Dedicated test-fixture server process and two separate native Debug client
processes using real GNS IP connections; clients ran twenty seconds. Production
Server owns all shots/hits/damage/respawns. The fixture arranges initial head-on
encounters, preserving normal physics between lives. Server recorded **208 shots,
22 hits, 5 destructions, 4 respawns, 26 peak rounds**. Both clients passed:

| Client | Frames rendering a live remote | Frames submitting combat visuals | Shots observed | Hits received | Destruction / respawn events | Final life |
|---|---:|---:|---:|---:|---:|---:|
| A | 712 | 1,180 | 208 | 12 | 5 / 4 | 2 |
| B | 576 | 1,177 | 208 | 10 | 5 / 4 | 2 |

Both took damage, were destroyed, respawned and continued firing. Finite state and
received authoritative counters are runtime assertions, not inferred from images.
Both retained actual 120 Hz flight stepping. Fixture repositioning after life
changes can produce large diagnostic reconciliation errors; these scripted setup
corrections are not normal production respawn behavior or evidence of flight
prediction accuracy. The existing M2 graphical flight
smoke remains available; offline desktop smoke passed Debug and Release. Visual
inspection of captures confirmed yellow tracers, impact/destruction crosses,
health/respawn feedback and visible live aircraft. Captures:
`images/m3-client-A.png` and `images/m3-client-B.png`, generated captures that
are not version controlled.

A separate **production Release `ofs_server` executable** also ran ten seconds
with two Release native `--network-smoke` clients in the ordinary spaced spawns.
Both received 192 snapshots and rendered live remotes in 474/475 frames; opposite
scripted roll ended at +7.30° / -7.34°. Both held fire: server recorded 156 shots,
zero hits (these trajectories intentionally did not aim at each other), zero
invalid inputs/send failures/overloads and clean disconnects with zero rounds.
This separately verifies standalone launch and preserved M2 flight; aimed
hit/destruction/respawn verification above uses the dedicated scenario fixture.

No physical gamepad, manual dual-pilot aiming, WAN, Windows/MSVC or IPv6 combat run
was performed. Driver capability-probe warnings remained the existing bgfx
warnings; both clients exited cleanly. Native renderer is basic line feedback,
not realistic explosion/smoke/structural breakup.

## Remaining weaknesses and recommended next milestone

Simplified sphere regions and one-tick rotational chords; no terrain/aircraft-
aircraft collision gameplay; frozen/hidden destruction; coarse expanding-line
explosions; fixed gun-axis sight with no lead; visual shots use current estimated server time
while remotes interpolate behind it and the local aircraft predicts ahead, so
muzzle/impact alignment is approximate; tracer range can visually exceed
the authoritative range until its advertised lifetime; old rounds are removed on
owner death; no manual reload; one default transport/gun/calm environment; reliable
combat events can suffer head-of-line delay (0.533 s observed); no spawn immunity,
rewind, precise clock synchronization, discovery/authentication or WAN verification.
Full snapshots recover health but missed event sends can lose cosmetic feedback.
New joiners do not receive historical active-round visuals. Existing adverse-test
randomness occasionally misses the short M2 control observation; assertions stayed
unchanged. Memory/performance results are this host and these durations only.

**Recommend B: a networking/world optimization pass before A: M4 missiles + radar.**
CPU collision is cheap at 16 players, while local 16-player continuous combat
already needs ~1.64 MB/s (~13.1 Mbit/s) server output and ~1.18 MB/s input. At 64,
even without firing, full snapshots cost ~21.76 MB/s. Redundant inputs remain
~74–109 kB/s/client. Optimize input redundancy, encode/snapshot bandwidth and
bounded interest/delta policies while preserving prediction/authority; do not
build a massive world database. Then reassess missiles/radar against a measured
small-group bandwidth budget. M3 ends here.

Reproducible commands: [BUILDING.md](BUILDING.md). Local build/test/CSV/desktop logs
are retained under ignored `.cache/m3` and `/tmp/ofs-m3-*-build.log`; documentation
above records the accepted results without requiring those local artifacts.
