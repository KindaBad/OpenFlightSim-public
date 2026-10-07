# Architecture

M0 foundation, M1 flight calibration, M2 authoritative multiplayer, M3 gun combat
and M3.6 multi-aircraft visuals. Native C++23, no game engine.

```text
ofs_core (no third-party graphics or platform libraries)
  math / AircraftConfig / Controls / Weather / State / Instruments
  ISA atmosphere / 6-DoF Simulator / RenderOrigin / FixedStepClock / C API
       ^                                          ^
       |                                          |
headless CTest suite                       ofs_client executable
                                          SDL3 Platform / Input
                                          double-position Camera
                                          bgfx Renderer / Dear ImGui
```

The existing dynamics remain the shared simulation foundation. The dedicated
server links `ofs_core` through `ofs_net`; configure with OFS_BUILD_CLIENT=OFF
to prove that
no SDL, GPU, ImGui or GLM dependency is fetched or initialized on that path.
State, Controls and Weather are plain values without resource handles. Do not
serialize their raw memory as a wire format: padding, endianness, quaternion
conventions and protocol versioning are explicit in the M2 codec. No sockets,
snapshots, prediction or replication existed before M2 (see the M2 section below).

## Data-driven dynamics foundation

The continuous evaluation API and its contracts are documented in
[FLIGHT_MODEL_VALIDATION.md](FLIGHT_MODEL_VALIDATION.md). Atmosphere/wind, mass,
raw aero, propulsion, derivative evaluation and FCS now occupy separate core
source components. Immutable coefficient/engine grids can coexist with the
engineering fallback. Full symmetric inertia uses Cholesky solves. RK4 advances
free-flight rigid-body state with frozen actuator/engine memory; the existing
stepper owns contact impulses and discrete updates. Euler remains the default.
The split simulation does not inherit the continuous integrator's fourth-order
convergence claim. Wire aircraft IDs and shared NED/FRD conventions are unchanged.

## Offline frame flow

SDL pumps events into client-only input state. Input produces a sanitized Controls
value, never global SDL state inside the core. The client uses steady_clock real
elapsed time; FixedStepClock runs zero or more identical 1/120-second ticks.
Each tick retains previous State, sets controls and advances Simulator. Existing
physics internally uses two 1/240-second substeps per tick, retained for spring
and rotational stability. The renderer interpolates previous/current position
and normalized quaternion on the same hemisphere. This introduces one simulation
tick of visual latency. UI reports the current simulation state.

The accumulator clamps a real frame to 0.25 seconds and caps catch-up at 16 ticks.
Excess whole ticks are dropped, leaving a fractional remainder and incrementing
a visible dropped-time counter. Physics dt never increases after a stall.
Minimized frames pump bgfx and sleep briefly; simulation is paused while minimized.
Explicit UI pause preserves the fractional remainder. Reset clears the clock and
sets both interpolation endpoints. Smoke mode alone supplies synthetic 1/60-second
frame time so regression assertions do not depend on scheduling.

Deterministic means repeatable input/state/tick sequence within a build. Floating
point trigonometry, FMA and compiler differences do not promise bit-identical
Linux/Windows results. The future server must remain authoritative.

## Native client

Platform isolates SDL initialization, native X11/Win32 window properties,
fullscreen switching and shutdown. Linux currently selects X11, including
XWayland on a Wayland desktop; native Wayland is deferred. Windows selects Win32.
Renderer chooses OpenGL on Linux and D3D11 on Windows. Other platforms/backends
are outside M0. No SDL GL context is created: bgfx owns GPU initialization.

Input uses persistent key state with focus-loss clearing and right-mouse relative
mode. Optional SDL gamepad axes map to Controls with deadzones; connection/removal
is handled without renderer involvement. No physical controller verification has
been done yet. ImGui receives SDL events and keyboard/mouse capture is respected.
Window/global shortcuts remain available. The UI edits throttle, flap and spoiler
settings and can reset to runway or a solved steady trim at 1,000 m / 110 m/s.
A persistent elevator offset is added to stick input in core physics; it is not
a runtime pitch controller. Existing C control entry points remain unchanged,
with additive `ofs_set_elevator_trim` for the new offset.

Renderer owns procedural airfield/sky resources, aircraft model resources,
shadow maps, programs, material textures and the font atlas. Each aircraft type
shares its GPU model/three LODs; each entity owns an independent visual pose and
hysteretic LOD choice. Shadow casters use the cheapest LOD and a bounded near set.
The delivered A320 is the actual GLB, not a placeholder. Models have factor-based
PBR, base-color/metallic-roughness/emissive textures and directional sun shadows.

CMake builds pinned bgfx shaderc and compiles the portable project shader sources
and varying definitions. Linux embeds GLSL 430; Windows embeds GLSL and DXBC
shader-model-5 binaries. Program creation and upstream ImGui binary selection use
the active bgfx backend. Unsupported backends fail with diagnostics. No synthetic
GLSL container is used for D3D11. Per-draw uniforms use project-owned names rather
than reserved bgfx names; MSAA is set on the swap-chain surface.

UI uses the official ImGui SDL3 platform backend and a small project-owned bgfx
renderer backend (font texture ID, alpha blending, clipping, vertex/index offsets,
framebuffer scaling). No docking/multi-viewports or general UI framework.
Screenshot capture uses a bgfx callback and writes RGB PPM; it is optional tooling.

Resource lifetimes: Input/controller closes, Renderer destroys resources and
shuts down bgfx, ImGui SDL backend/context closes, then SDL window/subsystems close.
Construction errors unwind initialized resources. Fatal GPU errors log and abort;
there is no device-loss recovery. Logging is category-prefixed stdio with no
framework, background queue or per-tick output.

## Performance

Core stepping and clock need no dynamic allocation. World GPU buffers are created
once and submitted with small matrices. Double global positions are subtracted
before conversion to GPU floats. ImGui uses retained allocations and bgfx transient
buffers; exhausted transient buffers skip UI drawing safely. Screenshot conversion
allocates only when requested. No ECS, hot-path locks or object hierarchy is added.
The rendering thread and SDL input remain client-only. No benchmark claims are made.

## M1 calibration and contact

`ofs::solveTrim` is renderer-independent offline initialization/calibration code.
It solves three steady force/moment residuals and returns State/Controls with a
success flag; it never changes the normal stepping path. `evalThrust` shares
engine evaluation with trim and diagnostics. The headless test harness supplies
controls at integer 120 Hz ticks, records metrics, and checks finite state. Test
pilot feedback exists only in tests, not production autopilot logic.

Flat-runway gear uses world normals/projected tire axes and a bounded fixed-size
Coulomb impulse solve for braked/lateral contact. All impulses become forces and
moments in the existing 6-DoF integration; contact does not lock aircraft state.
Suspension stiffness/damping and normal-flight derivatives remain unchanged.
DebugFrame records actual force vectors before its last 240 Hz substep, including
side/belly and exact contact-normal total, with sample time. UI shows these in a
collapsible diagnostics panel. Details/results: [M1_FLIGHT_VALIDATION.md](M1_FLIGHT_VALIDATION.md).

## M2 authoritative multiplayer

M0/M1 physics and the C API are unchanged. M2 adds `ofs_net` on top of `ofs_core`;
only `network/src/transport.cpp` includes GNS headers. The core can still build
alone with `OFS_BUILD_NETWORK=OFF`; normal headless builds include GNS but never
fetch SDL/bgfx/ImGui/GLM. Networking is single-threaded at the application level;
GNS owns its internal socket thread. Each process holds a reference-counted GNS
runtime, and transport callbacks route connections to small owning poll groups.
No network internals enter Simulator, State, Controls or renderer.

```text
ofs_server / headless network tests / ofs_net_load
    Server (connections, handshake, bounded ingress, snapshots, statistics)
      World (stable IDs, separate Simulator per aircraft, queued Controls)
        ofs_core: identical 120 Hz authoritative stepping

ofs_client --server IP / ofs_bot
    Client (version handshake, timing estimate, snapshots, metrics)
      Prediction -> ofs_core: immediate local stepping, bounded rollback/replay
      RemoteTrack: timestamped history, interpolation, bounded extrapolation
      display-only correction -> native renderer with shared double render origin

all network users -> explicit v1 codec -> Transport -> GameNetworkingSockets
```

A server tick is uint64, monotonically increasing; it owns integration order and
time. CLI scheduling uses steady_clock deadlines, at most 16 catch-up ticks per
poll, and reports severe overload; every actual simulation step is 1/120 second.
An overload beyond one second rebases the wall-clock deadline, without enlarging
physics dt or pretending the dropped wall time was simulated. Normal observed
tests retain 120 ticks per second. Connection callbacks cannot advance physics.

`World` is reusable by server, tests and benchmarks, rather than a second physics
implementation. Each connection owns exactly one stable network ID and aircraft.
Independent spawn slots in a 100 m lateral / 150 m longitudinal grid are reclaimed
on leave; entity IDs are not. Default spawn is M1 solved 1,000 m / 110 m/s trim;
`--ground` uses braked runway-level spawns. No collision gameplay, aircraft-to-
aircraft contact or respawn mechanic is introduced.

Each client generates tick-labelled controls, predicts with the shared core,
and sends redundant input batches at about 60 Hz. Server queues validated future
commands, retires late commands and applies due commands only at simulation tick
boundaries. Ownership, range/finite values, sequence/tick consistency, message
sizes and rates are checked. Missing inputs hold last controls briefly and then
neutralize stick/pedal/steering; server position/velocity/attitude cannot be set
by clients. Physics state/weather/configuration are server responsibilities.

Full snapshots default to 24 Hz, configurable independently of physics. Explicit
records include double NED/FRD pose/velocities, quaternion, spool/time, applied
controls and retired-input sequence. Local clients restore snapshots and replay
pending target ticks with `Simulator`, bounded to 512 ticks. There is no portable
bit-identical simulation assumption. Prediction history lets diagnostics compare
states at the same authoritative tick; meaningful corrections are counted, while
every fresh baseline still gets exact replay. Large stale baselines rebase.

Renderer display corrections decay over 120 ms independently of prediction.
Remote histories cap at 32 samples; a slewed clock renders ~100 ms behind the
estimated server timeline, interpolates doubles and hemisphere-aware quaternions,
and extrapolates at most 50 ms before freezing. Long loss gaps can still produce
a visible recovery jump. Full snapshots repair world membership and reliable
leave events immediately remove remote aircraft; tombstones guard reordered
older snapshots. Sampled type/controls/life drive independent articulated models.

Online SDL input maps to the same Controls. Reset/pause tooling is disabled while
connected; controls remain usable. Simulation/network polling continues while an
online window is minimized so client time does not stop. Offline mode retains M1
reset/pause/clock/smoke behavior. Renderer accepts remote State values and has no
network dependencies. The network diagnostic panel exposes IDs/ticks, GNS RTT,
snapshot/input rates, pending count, corrections/error, payload and wire bandwidth,
and remote history. Server logs include clients/ticks, ingress/egress counters,
invalid/disconnected/late inputs, queue/timing and overload counters.

`ofs_net_benchmark` measures pure World tick/snapshot/encoding cost for 2/8/16/
32/64 aircraft. `ofs_net_load` connects that many headless clients over real GNS IP
sockets and measures simulation and encoding/send cost plus bandwidth. Neither
claims an Internet capacity guarantee. Full-world fanout and input redundancy are
intentionally uncompressed; interest management, delta snapshots, precise clock
synchronization and scalable server admission belong to later networking work.
See [NETWORK_PROTOCOL.md](NETWORK_PROTOCOL.md) and
[M2_MULTIPLAYER_VALIDATION.md](M2_MULTIPLAYER_VALIDATION.md) for exact limits/results.


## M3 authoritative combat

`network/combat.cpp` is a headless value-based combat module with no transport or
renderer dependencies. `World` retains the M2 player map/Simulator/tick/spawn path
and adds Life, queued FireCommand state and Combat. Server retains GNS ownership,
validation/ingress, lifecycle and snapshots. It drains bounded CombatEvents each
tick and encodes each reliable batch once for all clients. No physics equations,
120 Hz rate, prediction replay rules for a live aircraft, or transport are replaced.

Tick order: increment server tick; respawn due players safely; apply queued flight
and held-fire state; create ready rounds from authoritative starting transforms;
advance live aircraft; advance ballistic rounds; sweep against moving explicit
sphere-region hitboxes; apply one damage per consumed round; emit lethal
transitions; clear dead input/fire and owner rounds. Dead flight is frozen.
Aircraft hitbox centers are cached once per tick. Relative segment/bounding-sphere
checks reject most projectile/aircraft pairs before 17 region-sphere sweeps. The
moving-center chord approximates rotation over one 1/120-second tick. Collision
clips the final segment at lifetime/range expiry and selects the earliest target
and region along each segment. No triangle mesh or subsystem simulation exists.

Rounds are pre-reserved contiguous values (4,096 cap), compacted in place without
per-round allocations. Event buffers cap at 256 and drain each server tick.
Clients retain at most 4,096 visual rounds and 256 impacts/explosions; display
positions use analytic gravity from reliable Shot events. A separate bounded
256-event drain dispatches accepted shots/hits/destruction once to renderer
effects. Projectile IDs retire tracers on impact. The renderer receives no
Combat, World or hit-detection API. A projected gun-axis
sight and optional nose gun camera (V) help aim. Input predicts the held-trigger
sight color; damage and hit markers always require server events/snapshots.

Life generations extend Input/Fire and Aircraft records, retained in protocol v3, rejecting
previous-life actions. Snapshots own health/ammo/life/cooldown/respawn baselines;
events own descriptive feedback. Local alive/generation transitions clear
prediction and display corrections; remote generation transitions clear their
interpolation history. Respawn uses the same ID/slot and excludes live aircraft
within 60 m, searching a bounded sequence of spaced candidates. Full health/ammo
and trim flight controls return after four seconds. Scores persist. Projectiles
are removed on owner death/leave/respawn; there are no posthumous rounds.

Test fixtures can assign authoritative trajectories to create repeatable duels;
this is deliberately absent from game-facing network messages. The soak uses real
GNS clients and the production combat path. M1/M2 tests still independently verify
normal flight, prediction, interpolation, membership and impairments. Combat
profiles distinguish cached transforms/motion from narrow-phase collision and
serialization; this remains O(rounds × aircraft) with cheap rejection, bounded to
small friend groups. See protocol and M3 validation documents for measurements.

## M3.6 aircraft and visuals

`core/aircraft_definition` contains immutable A320, Su-57, Typhoon and SR-71 definitions. The Falcon is retired. A320/Su-57 geometry and numerical authoring come from `data/physics`; see [the current audit](AIRCRAFT_PHYSICS_AUDIT.md).
Each has stable ID/name, canonical model path, flight configuration, camera/CG/
effect reference points, transition timings, collision scale and optional gun.
Civil aircraft simply have no gun. Server spawning, prediction, cameras, HUD and
model selection resolve the same registry. A320 calibration remains unchanged;
armed definitions have independent mass/inertia/aerodynamics, named engine assumptions and
gear geometry. No afterburner, missile, radar or arbitrary client asset selection.

`ofs_visual_core` is CPU-only and testable without SDL/bgfx/GLM. Its glTF loader
retains named nodes, parents/children, local/rest-world transforms, mesh and
material references. Primitives retain node identity. Rest-pose vertices remain
baked for compatibility, but batches split at articulated ancestors; animated
world times inverse rest-world transforms move those batches rigidly. Static
geometry still merges by material. Authored pivots use small `ofs_channel`, axis,
gain and optional slide extras; general skeletal/animation-track playback is not
implemented. Nested gear/steering/wheels compose through the hierarchy.

`AircraftPose` reads controls/state only: opposite ailerons, elevators plus trim,
rudder, gradually moving flaps/spoilers/gear, speed-dependent steering, wheel
rotation and N1-driven A320 fans. Gear interpolation is visual only; the existing
authoritative gear command/contact model is unchanged. Standard gear doors move
with the assemblies; there is no independent sequenced door controller.

PNG/JPEG images load from GLB buffer views, data URIs or relative files through
the vendored stb_image decoder. Texture dimensions/decoded bytes are bounded.
GPU textures cache by image/sampler within each shared model and are destroyed
once, along with owned buffers/programs. Base color, metallic/roughness and
emissive maps are supported. M3.65 adds guarded derivative normal mapping,
renormalized normal-map mip chains and an opt-in canopy environment reflection
factor. Skins and most glTF extensions remain unsupported.

`EffectPool` reserves 4,096 contiguous values and compacts living particles in
place, always persisting age/movement. Emission clocks cap at 64 aircraft. Effects
have velocity/drag/gravity, finite lifetime, variation, age-based size/opacity and
a procedural soft radial shader. Tracers/debris orient along velocity; other
particles billboard with alpha blending and depth testing, no depth writes.
Contrails use overlapping, distance-spaced soft puffs only above 7 km at speed;
local high-load vapor is restrained. Normal engines have faint spool-driven
exhaust, damage produces smoke, and server events drive flash/sparks/explosions.
This is not depth-aware softness or refractive heat haze. Counts/peak/LOD/draws/
triangles and CPU preparation time are visible in diagnostics. Performance and
captured evidence are in `M3_6_VISUAL_AIRCRAFT_VALIDATION.md`.

## M3.65 registry, authored LODs and engine extension

Aircraft ID 3 is the Eurofighter Typhoon. Type-keyed maps replace the
two-aircraft spawn/model arrays. Legacy configuration history is superseded by the current aircraft audit; production configurations use
their thrust curves, gun capabilities and generated three-tier LODs; authored
four-tier assets are optional definition data. Per-type collision spheres and
gun configurations drive server authority; legacy explicit gun fixtures remain.

Typhoon has two independent engine slots. At normalized throttle 0.85 dry power
reaches its rated 60 kN per engine; the upper 15% requests smoothly spooled reheat,
adding up to 30 kN per engine. N1 and afterburner state interpolate with the
aircraft state, and protocol v4 adds only two floats to each snapshot aircraft.
The server runs the state transitions and thrust calculation. Unused reheat
defaults to zero for legacy configurations. Fuel and real engine/FCS scheduling
are outside this approximate flight model.

Canard/elevon/door/nozzle/suspension channels extend the existing extras rig.
Simulation controls drive pose, never the reverse. Oleo translation keeps tires
at the ground plane. Reheat uses one shared fixed flame mesh: three animated,
translucent tapered shells and a soft hot-core glow per engine, eight draws and
6,916 triangles per visible Typhoon. No flame particles or particle coordinates
are networked. Effects cull at 1,200 m and disappear with destroyed aircraft.
Scene-color refraction, physical volumetrics and dynamic exhaust lighting remain
future rendering work. See the M3.65 report for inspected evidence and budgets.

## M3.7 replication architecture

Protocol 9 replaces the historical full-world live broadcasts described above.
`InterestGrid` indexes authoritative snapshots in 20 km cells and caches common
remote quantized fields once per publication. Each `ReplicationSender` maintains
per-client AOI tiers, reliable existence/life metadata, a 64-frame history and the
latest usable ACK. It emits 24/10/2 Hz entity samples as field-group deltas, refreshes
keyframes periodically and on recovery/entry/reference changes, and chunks them
into at most 1,100 application bytes. Owner state preserves full precision flight
memory; remote presentation uses a 106-byte quantized projection.

Per-peer publication phases distribute the 24 Hz work across five 120 Hz ticks.
Each group builds a common grid/projection once, then uses bounded GNS batch sends
for its peers. Authoritative timestamps remain explicit, and no application
networking worker threads are introduced. Profiling separates the complete server
tick from physics alone and includes real transport dispatch.

`ReplicationReceiver` bounds assembly/history allocation, expires incomplete
chunks actively after 1 s, applies complete frames atomically, and requires reliable
spawn before accepting entity state. Unknown/missing/invalid state requests refresh;
recovery ACKs are throttled and piggyback on compact input. Sorted contiguous entity
baselines and inline field storage bound allocation costs. Shared `Simulator`
prediction/replay remains on the existing thread; there is no second flight model.
Remote presentation keeps 32 samples, adapts tier buffer delays, and caps
extrapolation at 50 ms. Reliable owner life transitions apply immediately.

The full-world serializer remains a local state/protocol regression diagnostic;
the live client rejects full-world snapshots and transport enforces the packet cap. Existing gun events remain
ordered/reliable and are filtered to involved/interested peers. No projectile
snapshot stream or combat expansion was added. Detailed layout and measured limits
are in [NETWORK_PROTOCOL.md](NETWORK_PROTOCOL.md) and
[M3_7_NETWORKING_VALIDATION.md](M3_7_NETWORKING_VALIDATION.md).
