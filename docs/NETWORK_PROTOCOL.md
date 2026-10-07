# OpenFlightSim network protocol v14

GameNetworkingSockets v1.6.0, pinned revision
`2cb93a06350bb065db53abdb0d87cf297e0bfd34`, supplies encrypted direct IP
transport and ordered reliable messages. Physics remains authoritative at 120 Hz.
No networking worker threads, account service, discovery, relay, or lag compensation
were introduced. Numeric IPv4/IPv6 endpoints are supported.

Protocol 14 extends the owner-only RadarState with the selected weapon's target,
its lock progress and the stores visible on nearby aircraft; nothing else on the
wire changed from v13. Protocol 13 retains the v12 Su-57 maneuver-mode input and v10 recovery/AOI
architecture. It adds weapon actions, private radar tracks, missile lifecycle/state
and distributed-store inertia. Earlier peers must rebuild with their server.

## Header and limits

Every application message starts with the following big-endian header. Integers
and IEEE float bit patterns are serialized explicitly; simulation structs, padding,
pointers, renderer handles, particles and debug forces are never copied to packets.

| Offset | Bytes | Meaning |
|---|---:|---|
| 0 | 4 | magic `0x4f46534e` (OFSN) |
| 4 | 2 | version **14**, incompatible versions rejected |
| 6 | 1 | message type |
| 7 | 1 | reserved zero |
| 8 | 8 | authoritative tick or newest input target tick |
| 16 | 8 | snapshot sequence, input sequence, or correlation |

The transport send/receive limit is **1,100 application bytes**. GNS UDP payload
MTU is explicitly **1,200 bytes**; live connections expose their read-only
`MTU_DataSize`, checked by `network.presentation_clock`. This leaves room for
GNS encryption/message headers. Snapshot records are application-chunked; no
snapshot relies on GNS message fragmentation or IP fragmentation. Reliable combat
batches contain at most ten 102-byte events (1,045 bytes including header/count).
GNS can coalesce small messages within its configured UDP MTU.

Maximum players: 64; input commands/packet: 8 (normally 4); chunks/snapshot: 16;
in-flight assemblies: 4; baseline frames/peer: 64; incomplete assembly retention:
120 ticks/1 second, expired actively during client polling even without new packets.
Timeline ticks are limited to `(1<<53)-1024`, keeping binary64 presentation ticks
exact and preventing wraparound replay/cast hazards (over two million years at 120 Hz).
Recovery-only ACKs are throttled to 10 Hz. Names/reasons are 1–64 printable ASCII bytes. GNS send buffers
are 256 KiB/connection; snapshot work pauses at 128 KiB pending. A failed reliable
send terminates the session explicitly; transitions cannot silently disappear.
Readers reject unknown types/versions, reserved values, truncation/trailing bytes,
invalid counts/indices/enums/fields, duplicate entities, future acknowledgements,
missing baselines and conflicting duplicate chunks. Parser allocations are bounded
by these fixed limits. The transport accepts at most 72 connections and drains
at most 256 messages/poll. Sessions enforce 240 messages, 1,920 input command
records and 60 fire messages per 120 ticks; eight malformed-message strikes
terminate a session. Hello deadline: 600 ticks. Ownership is connection-scoped.

## Message classes

Payloads below follow the common 24-byte header.

| Type | Value | Delivery/direction | Payload |
|---|---:|---|---|
| Hello | 1 | reliable client → server | u8 text length, text, u8 requested aircraft type |
| Welcome | 2 | reliable server → owner | u16 physics Hz=120, u16 base snapshot Hz, full owner record, 40-byte Weather |
| Joined | 3 | reliable server → clients | u64 joined entity; diagnostic notification |
| Left | 4 | reliable server → clients | u64 departed entity; remove immediately |
| Input | 5 | unreliable client → server | input format below |
| Snapshot | 6 | **local diagnostic only** | full-world projection retained for historical physics/protocol regression tools; rejected by live client; large full-world messages also exceed the transport cap |
| Reject | 7 | reliable server → client | u8 text length, reason |
| Ping/Pong | 8/9 | unreliable | tick/correlation in header |
| Fire | 10 | unreliable client → server | u64 owner, u64 sequence, u64 target tick, u32 generation, u8 weapon=0, u8 held |
| Combat | 11 | reliable server → interested clients | u8 count, 102-byte events (existing explicit event fields preserved) |
| Spawn | 12 | reliable server → interested clients | 611-byte owner projection; initial existence/static type and life transition state |
| Despawn | 13 | reliable server → interested clients | u64 entity; AOI exit |
| SnapshotChunk | 14 | unreliable server → client | chunk header + field deltas below |
| SnapshotAck | 15 | unreliable client → server | u64 usable baseline sequence, u8 request-recovery boolean |
| WeaponAction | 16 | reliable client → server | owner/generation/action/station; M4 section below |
| RadarState | 17 | unreliable server → owner | private tracks/inventory/envelope |
| MissileSpawn | 18 | reliable server → interested clients | compact bounded missile records |
| MissileState | 19 | unreliable server → interested clients | independently applicable compact records |
| MissileRemove | 20 | reliable server → interested clients | retirement/detonation positions |

Spawn is also a reliable full refresh on destruction, respawn and supported type
metadata changes. Current world types are immutable. IDs increase monotonically
and are never reused during a server lifetime. Generation increases on respawn.
Snapshots cannot establish existence. Unknown-entity state fails atomically and
requests a keyframe; reliable spawn then makes recovery usable. Despawn/Left
remove interpolation tracks immediately, and bounded tombstones suppress stale
state. Reliable existence is capped at 64 records/client; receiver and presentation
tombstones are each capped at 128 IDs. Rejected stale or excess Spawn records
cannot create presentation tracks. Combat events always reach the participating owner/target, plus peers
interested in either participant. Projectiles remain authoritative and event-driven;
clients derive visual trajectories from shot events, without projectile snapshots.

## Input format

Header tick/sequence identify the newest command in the packet. Payload:
u64 owner, u32 life generation, u64 usable snapshot ACK, u8 recovery flag,
u8 command count, followed by commands. Each command is u16 backwards sequence
offset, u16 backwards tick offset, and eleven u16 controls and one u8 maneuver-mode flag (27 bytes total).
Offsets are checked before subtraction; commands must be strictly ordered.

Control order: elevator, aileron, rudder, flap, spoiler, gear, left throttle,
right throttle, brake, steering, elevator trim; then maneuver mode (0 or 1, other
values rejected). Signed axes use −32767…32767,
resolution 1/32767; −32768 is rejected. Unit controls use 0…65534, resolution
1/65534; 65535 is rejected. Prediction canonicalizes controls to the same wire
lattice **before** simulating/replaying them. The server preserves full precision
simulation states and validates whole input batches before mutation. Recent four
commands at the existing ~60 Hz input send cadence cover isolated dropped packets;
no continually retransmitted 32-command history remains.

| Commands | v8 bytes | v13 bytes |
|---:|---:|---:|
| 1 | 97 | 73 |
| 2 | 157 | 100 |
| 4 (normal) | 277 | 154 |
| 8 (maximum) | 517 | 262 |

Explicit SnapshotAck permits progress while the owner is dead/not producing inputs.
ACKs only advance to retained frames previously sent to that client. Duplicate
and older ACKs are ignored before history lookup; an expired stale ACK cannot
request recovery. A newer expired ACK requests bounded recovery without advancing
the accepted baseline; future ACKs are rejected. Sequence IDs are strictly
increasing u64 and cannot wrap: exhaustion returns an explicit error requiring
reconnection. Zero is the no-baseline/recovery sentinel and never rewinds an ACK.
ACKs piggyback on input as well. Explicit duplicate/zero recovery flags are handled
separately from ACK progression; old nonzero reordered flags are ignored.

Recovery moves NORMAL → REQUESTED → WAITING_FOR_ACK → NORMAL. One keyframe starts
an episode. Further requests coalesce; missing ACKs allow a retry after 30 ticks
(250 ms) or if the sent frame has fallen out of bounded history. A retained usable
ACK from the episode completes it, including a late ACK of an earlier retry.
A cooldown of 30 ticks also bounds new episodes from a continuously asserted
flag; a request during that cooldown is deferred to the next eligible publication.
Before the first usable ACK, deltas may depend on the sent recovery keyframe;
loss of that frame is repaired by the bounded retry. AOI/lifecycle/reference changes
and periodic 240-tick keyframes keep their existing semantics.

## Spatial interest and scheduling

A uniform 3D grid has 20,000 m cells. Each publication rebuilds the grid and
projects common remote fields once per aircraft. Per-peer construction reuses
those quantized fields and computes only the reference-dependent position.
Queries examine a 7×7×7 neighborhood (55 km including hysteresis), or scan occupied
cells when there are fewer than 343. Thus empty-cell work is bounded and clustered
worlds avoid repeated empty lookups. Euclidean physical distances are meters.
Owner always remains interested and is freshly sampled at every configured publication. Only remotes use the reduced tier schedules.

Each peer has a stable publication phase on the 120 Hz clock. With the default
24 Hz setting, five phases distribute peers across five successive physics ticks.
Every peer still receives 24 publications/second; each publication and each entity
sample carry the actual authoritative tick. This bounds the burst of per-peer work
without introducing application worker threads or a second simulation clock. The
nominal server `snapshotCount` tracks the configured clock; `publicationCount`
counts ticks that actually publish a peer group.

| Tier | Enter radius | Exit margin | Nominal rate |
|---|---:|---:|---:|
| Owner | always | always | 24 Hz reconciliation |
| Near | ≤5 km | 5.5 km | 24 Hz |
| Medium | ≤20 km | 22 km | 10 Hz |
| Far | ≤50 km | 55 km | 2 Hz |
| Outside | beyond retained far range | — | no snapshot state |

Promotions into combat proximity happen immediately. A tick-phase scheduler
preserves the 10 Hz average on the 24 Hz publication clock; individual medium
intervals alternate as needed. Keyframes can add refresh samples. The configurable
server publication rate (1–60 Hz) limits these rates; defaults were measured at 24.
Each entity carries its own sample tick, so retained lower-frequency fields are
never mislabeled with a newer snapshot timestamp. Remote histories retain 32 samples.
Interpolation uses the server timeline, with tier delays of 100/200/500 ms. Buffer delay grows fast enough to cover the
first slower update interval, then releases gently on promotion; transition tests
bound presentation steps. Extrapolation remains bounded to 50 ms, then
freezes presentation. One effective tick after adaptive buffering selects both
physical and visual state: gear, flaps, spoiler, throttle and steering interpolate
with the same bracketing samples as spool, afterburner and nozzle angle. Discrete
life/type/flags/loading use the preceding sample, switching at its endpoint.
Rendering remains independent of network cadence.

## Network projections and quantization

Owner fields retain the complete full precision integration/replay projection.
Remote records contain only kinematics, realized presentation actuators/engines,
gear/flap/spoiler/throttle, and low-frequency life/type metadata. Remote projection
payload is 132 bytes with relative position (147 with absolute position). It is
never fed back into authoritative physics or local-owner prediction.

| Remote group | Bytes | Range / resolution / component error bound |
|---|---:|---|
| Position | 10 or 25 | u8 mode + signed24 XYZ at 1 cm relative to reference: ±83,886.07 m, ≤5 mm/component; absolute fallback 3×binary64 |
| Quaternion | 7 | u8 largest-component index +3×signed16 smallest-three in ±1/√2; normalization reconstructed; randomized angular-error test |
| Linear velocity | 6 | 3×signed16, ±2,047.9375 m/s, 1/16 m/s resolution, ≤1/32 m/s/component |
| Angular velocity | 6 | 3×signed16, ±15.99951171875 rad/s, 1/2048 rad/s, ≤1/4096 rad/s/component |
| Gear/flap/spoiler/throttles/steering/maneuver | 7 | 5×u8 [0,1], 1/255 resolution, ≤1/510; signed8 steering −1…1, 1/127 resolution, ≤1/254 error; −128 reserved; u8 maneuver flag, 0/1 |
| Spool/AB/inlet/nozzles | 10 | per engine 3×u8 [0,1], one signed16 nozzle ±30°, step 60°/65534; clamp reconstructed nozzle to actual aircraft limit |
| Actual control surfaces/integrity | 17 | eight signed16 normalized actuators; step 1/32767, error≤1/65534; physical fuselage integrity u8 at 1/255 |
| Life/type | 33 | type u8 (low 3 bits, bit 3 physical crash, bit 4 alive, bit 7 fuel present; bits 5–6 reserved), generation u32, health u16 at 0.01, ammo u16, ready/respawn ticks 2×u64, kills/deaths 2×u32 |
| Loading | 36 | 1 Hz: two u24 masses at 1/16 kg (0…1e6 kg, reserved 0xffffff=initial config), payload offset 3×signed16 at 1 mm (±32.767 m, vector≤20 m), plus 6×binary32 distributed-payload inertia corrections (diagonal and xy/xz/yz off-diagonal, kg m2) |

Signed minimum codes are reserved/rejected. Kinematic outliers saturate without
wrap; out-of-range relative positions use the explicit absolute fallback. Reference
positions are binary64 in a 1 km grid, refreshed by keyframe on changes. Current
absolute/reference bounds ±1e12 m comfortably cover Earth-scale coordinates; no
floating origin, Earth reference frames or streaming are introduced.

Field audit: pose/velocity are required frequent state; engines and actual surfaces
are delta-compressible; life/type are low-frequency/reliable transition state;
fuel/load/CG inputs are compact low-frequency remote state for presentation and exact
owner state for integration; input acknowledgements, damage effectiveness, transient pilot/FCS and
unsteady aerodynamic memory are **owner only**; simulation time is derived from
entity tick; particles, render resources, debug forces and static flight config are
not replicated. Both endpoints resolve type-specific immutable configuration from
the shared registry. Weather is authoritative in Welcome/chunk headers.

### Owner projection (611 bytes, explicit serializer)

| Field | Representation |
|---|---|
| entity ID | u64 |
| highest retired/applied input sequence | u64 |
| global NED position N/E/D | 3×binary64 meters |
| world NED linear velocity N/E/D | 3×binary64 m/s |
| body→world quaternion w/x/y/z | 4×binary64, unit length |
| body FRD angular velocity p/q/r | 3×binary64 rad/s |
| engine spool left/right | 2×binary64 [0,1] |
| simulation time | binary64 seconds |
| applied Controls | 11×binary32 in Command order + u8 maneuver-mode flag |
| health | binary32 [0,100]; zero means destroyed |
| ammo | u16 [0,600] |
| life generation | u32; increases on respawn, aircraft ID unchanged |
| next gun-ready tick | u64 |
| scheduled respawn tick | u64; zero while alive |
| kills / deaths | 2×u32 |
| aircraft type | u8: A320=1, reserved/rejected=2, Typhoon=3, SR71=4, Su57=5 |
| afterburner left/right | 2×binary32 [0,1], finite, server-simulated |
| inlet-spike left/right | 2×binary64 [0,1], finite |
| actual independent nozzle angle left/right | 2×binary64 radians; finite, within type-specific limits |
| aerodynamic memory initialized | u8 boolean |
| lagged incidence left/right | 2×binary64 radians, finite, [−π,π] |
| separation fraction left/right | 2×binary64 [0,1], finite |
| vortex state left/right | 2×binary64 [0,1], finite |
| trim reference / fuel mass / payload mass | 3×binary64 |
| payload offset X/Y/Z in reference body frame | 3×binary64 meters |
| distributed payload inertia diagonal / xy,xz,yz correction | 6×binary64 kg m2 |
| engine health/output modifiers | 2×binary64 [0,1] |
| surface effectiveness / drag modifiers | 6×binary64 [0,1], then 6×binary64 [0,10] |
| actuator initialization / FCS enabled | 2×u8 boolean |
| pilot pitch/roll/yaw shaping memory | 3×binary64 [-1,1] |
| actual elevator/aileron/rudder | 3×binary64 [-1,1] |
| actual flap / spoiler | 2×binary64 [0,1] |
| actual canard / left elevon / right elevon | 3×binary64 [-1,1] |


The receiver requires exactly one owner, checks the connection's owner ID when known, and rejects a stale owner sample tick.

Owner deltas use sixteen groups with lengths
`16,24,24,32,24,24,45,35,40,49,8,8,32,112,2,136` bytes, respectively:
identity/ACK, position, velocity, attitude, body rates, spool/time, applied controls,
life/type, AB/inlet/nozzles, aero memory, trim, fuel, payload/offset,
engine/surface damage modifiers, initialization/FCS flags, pilot/surface memory and distributed payload inertia.
Time is zero in the network comparison lattice and derived from sampled tick on
expansion. Applied controls retain binary32 as in v8; all integration state,
fuel/CG/inertia inputs and unsteady/FCS memory retain binary64. The owner path
preserves the shared Simulator and exact authoritative reset/replay semantics.

## Baselines, chunks and recovery

Snapshot sequences are per client, uint64, monotonic. Each peer retains64 usable
frames (2.67 seconds at 24 Hz), and the server encodes against that peer's latest
acknowledged retained frame. While waiting for the first usable ACK, deltas may
depend on the most recent sent recovery keyframe until the bounded retry; this
bootstrap dependency is never treated as an accepted ACK. Tiered state sampled
in an unacknowledged frame remains available for retransmission in another delta.

Chunk header after the common header:
u64 baseline sequence (zero=keyframe), u8 index, u8 chunk count, u8 entity-record
count, 3×binary64 reference position, and 40-byte Weather. Total header99 bytes.
Record: u64 ID, u8 owner flag, u8 tier, u64 sampled tick, u16 changed-group mask;
for each set bit in ascending order: u16 field length, explicit field bytes.
A new entity/keyframe uses mask0xffff, including zero lengths for unused remote
groups. Existing unchanged groups are omitted; a timestamp-only sample is legal.
All chunks are individually≤1,100 bytes and each record fits within one chunk.
The transport uses GNS `SendMessages` for each bounded chunk batch, allowing one
connection scheduling pass. GNS owns those allocated message buffers, including
failed sends; per-message results remain part of send-failure accounting. The
configured unfragmented payload is read once per connection and cached.

Complete frames apply atomically. Chunks can duplicate/reorder; conflicting copies
are rejected. At most four assemblies of sixteen chunks coexist, with 1 s expiry;
newer complete state discards older incomplete frames. Lost chunks do not become
ACK baselines. Repeated deltas based on a usable ACK remain independently decodable.
Periodic keyframes every 240 ticks/2 s, AOI entry/life transition, reference changes,
expired baseline, unknown entity, invalid decode and explicit recovery requests all
refresh full state. No snapshot is made reliable.

Network histories use inline bounded field storage rather than heap allocations
per field. The fixed memory cost is explicit in the validation report. GNS owns
transport retransmission queues; application code owns authoritative world state,
per-peer baseline histories, bounded assembly/interpolation buffers and prediction
histories on its existing single simulation/poll thread.

## Measurement and limitations

`ofs_replication_benchmark` runs production sender/receiver, explicit wire parsers,
shared physics/prediction, tier interpolation and deterministic impaired delivery
without sleeps. `ofs_net_load` measures actual GNS connections and transport rates.
`ofs_server` prints headless replication totals, per-client rates, packet percentiles,
interest counts, keyframe/delta/recovery counters, timings and bounded memory.
See `M3_7_NETWORKING_VALIDATION.md` for measured results and exact test outcomes.
M4 radar/missile state, bandwidth prioritization, lag compensation, Earth frame work
and user/account authentication remain outside this protocol milestone.


## M4 weapon messages (v14)

All messages use the same 24-byte header and the 1100-byte application ceiling.
Weapon messages use a separate explicit codec; they never serialize physics
structs. Guidance and launch-radar support are server internal, with no extra
client support stream.

| Message | Reliability / cadence | Body and maximum size including header |
|---|---|---|
| WeaponAction | Ordered reliable, ≤30 actions/s | Owner u64, generation u32, action u8, station u8; 38 bytes |
| RadarState | Unreliable 10 Hz, owner only | Generation u32, mode u8, selected/locked references 2×12, selected weapon/readiness u8, weapon target reference 12 and lock progress u8, station count u8, ≤8 station types, 3×u32 range cues, signed16 closure, inside/count u8, ≤16 tracks, then count u8 and ≤16 nearby loadouts (entity reference 12, mounted-station bits u8); 814 bytes at every bound |
| MissileSpawn | Ordered reliable, AOI entry | Count u8, ≤16×63-byte records; 1033 bytes |
| MissileState | Independently applicable unreliable 20 Hz | Same bounded batch, 1033 bytes |
| MissileRemove | Ordered reliable, AOI exit/expiry/detonation | Count u8, ≤16×(id u64, detonation u8, position 3×signed32); 361 bytes |

A missile record has ID u64, owner and target references (u64 id/u32 generation),
type u8, position 3×signed32 at 1/8 m, velocity 3×signed16 at 1/4 m/s, normalized
quaternion 4×signed16 at 1/32767, motor/seeker u8 and age u16 at 0.01 s. Signed
minimum values are reserved. Target identity is exposed only to the launch owner;
other observers receive zero. Physics mass, propellant, angular rates, forces,
telemetry and particles are not transmitted.

A radar track is 32 bytes: reference 12, position 12 at 0.5 m, velocity 6 at
0.5 m/s, quality u8 and detection age u8 at 0.02 s. The owner receives only its
server-derived detections/tracks, not a world entity list.

Missile authority is bounded to 128 entities. Each viewer's known set and receive
pool are bounded to 128, so a state publication needs at most eight separate
packets; lifecycle classes are bounded similarly. Launch owner and intended
target have unconditional interest; other viewers need to be within 15 km.
Reliable spawn/retirement handles AOI changes. State can reconstruct a watchdog
expired visualization. Tick ordering, life generations, bounded 256-entry
retirement tombstones and a two-second watchdog prevent stale resurrection.
Four presentation samples interpolate position/attitude; extrapolation stops at
50 ms. No client launches a physical missile or claims a hit.

Invalid owners, future/expired ticks, excessive sequences, invalid stations,
nonfinite/out-of-range quantization, duplicate IDs, invalid enum/boolean fields,
truncation/trailing data and over-budget packets are rejected. The existing
connection-wide rate/strike policy applies. Aircraft baselines remain independently
bounded to 64 and aircraft assembly/recovery limits remain unchanged.
