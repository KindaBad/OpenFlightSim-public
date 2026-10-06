# M3.6 Visual Aircraft Validation

Historical milestone report. The temporary Falcon has since been retired; current aircraft physics and evidence are documented in [AIRCRAFT_PHYSICS_AUDIT.md](AIRCRAFT_PHYSICS_AUDIT.md).
Date: 2026-10-01. Continued the existing M0-M3.5 implementation; no engine,
network transport, integration scheme or authoritative combat rewrite. Linux
GCC Debug/Release and headless verification are recorded below. Windows is
prepared, not compiled or runtime-validated on this workstation.

## Audit and implementation plan

Read the M1, M2, M3 and M3.5 validation reports before editing. Inspected bgfx,
shader packaging/backend selection, GLB primitives/materials/nodes, owned Blender
source, simulation controls, spawning, protocol/prediction, gun/HUD/cameras,
effects, LOD and regression suites. Findings:

- The custom packer manufactured GLSL-oriented containers; Windows selected
  D3D11 without corresponding project or ImGui shader selection.
- Custom uniforms used reserved bgfx names, and the pinned bgfx API expects
  MSAA on the swap-chain surface rather than the global reset mask.
- `assets/a320.glb` was a symlink to the actual owned `output/Airbus_A320.glb`.
- Material merging discarded articulation identity. The original A320 had
  1,066,898 triangles, 1,134 primitives, 21 materials and no decoded images.
- Every aircraft inherited a gun/ammo. Neither selection nor wire state carried
  an aircraft type; cameras and render resources assumed the A320.
- Particle age/motion were persisted only during compaction, allowing particles
  to remain alive indefinitely. Wingtip vapor's old unit-vector condition could
  never trigger. Native main used crude line feedback rather than rich events.

The plan delivered before edits was to repair shader/asset portability first,
retain node hierarchy and add a small shared capability registry, articulate the
owned A320/create an owned fighter, wire state-driven animation and bounded soft
effects, then preserve regressions, measure mixed scenes and inspect captures.

## Renderer, shaders and assets

Pinned bgfx shaderc now compiles all ten stages with the portable bgfx language
and a shared varying definition. `pack_shaders.py` embeds real compiler output.
Linux builds GLSL 430; Windows builds GLSL plus DXBC shader model 5. Renderer and
upstream ImGui select binaries for the active backend. Missing/unsupported shader
backends and failed programs produce backend/program diagnostics. Vulkan was
not added. All ten GL stages compiled and ran; all ten Windows HLSL stages
preprocessed successfully on Linux. HLSL preprocessing is **not** DXBC compilation.

Reserved custom uniforms were renamed, per-draw uniform lifetime fixed for the
pinned bgfx API, and MSAA moved to surface flags. Linux OpenGL rendered actual
geometry, UI, shadows and effects. Existing harmless driver capability-probe
warnings and a fallback EGL-context attempt remain in Debug logs.

The sole airliner runtime path is `output/Airbus_A320.glb`, a regular Git file.
The symlink was removed rather than duplicating 58 MB of geometry. Fighter is
`assets/fighter.glb`. Executable/checkout/source-root lookup and CMake install use
the same canonical paths. No Blender or Windows symlink developer mode is needed
for a normal clone. Source regeneration retains the owned Blender scene and
exports rig extras, parents and evaluated curve details.

`cmake --install` was checked at `/tmp/ofs-m36-package`. Both GLBs are regular
files with matching checkout/install SHA256 hashes. The installed Release
executable launched from unrelated working directory `/`, loaded both models
and produced the inspected `package_fighter.png` without a symlink.

CPU glTF representation retains names, local/rest-world matrices, parents,
children, mesh references and material references. Rest-pose vertices remain
baked for existing coordinate regressions. Articulated batches use animated
world multiplied by inverse rest-world; compatible static geometry still batches
by material. The loader therefore no longer treats an aircraft as one rigid
anonymous mesh.

| Asset | Nodes | Primitives | Materials | Rig channels | Batches | LOD0 / LOD1 / LOD2 triangles |
|---|---:|---:|---:|---:|---:|---:|
| A320 | 1189 | 1155 | 21 | 12 | 91 | 1,117,326 / 90,361 / 11,867 |
| Falcon | 94 | 79 | 7 | 9 | 29 | 19,926 / 3,488 / 843 |

## Aircraft definitions and capabilities

The small immutable registry in `core/aircraft_definition` contains stable type
ID/key/display name, canonical model path, flight configuration, CG/camera/effect
reference points, transition timings, hitbox scale and optional gun configuration.
It is shared by server spawn, prediction, rendering, cameras and HUD. No modding
framework, arbitrary asset paths or client-selected flight parameters were added.

`--aircraft a320` is the civilian default; `--aircraft fighter` selects Falcon.
Unknown IDs/names and path-like selections are rejected. Protocol v3 appends a
validated u8 type to Hello and the 223-byte Aircraft record. Server and other
clients select the correct simulator/model. Full snapshots, 120 Hz authority,
prediction replay, interpolation, ownership, life generations and gun damage
rules remain intact; rebuild peers together because v1/v2 are rejected.

**A320 has no gun definition, muzzle or ammunition.** Spawn/respawn ammo is zero;
server ignores fire before its fire queue/projectile creation, and clients suppress
fire transmission. Normal HUD has no gunsight/ammo/cooldown. The optional debug
network panel can show the zero-ammo baseline. A320 can still receive existing
authoritative damage; civilian invulnerability/team rules were not requested.

Proof: the capability test attempts A320 fire for 240 authoritative ticks and
requires zero shots, zero rounds, zero ammo and an empty fire queue. Fighter fire
must create one authoritative shot and consume one round. The real mixed GNS
test requires correct remote types, A320 ammo zero, fighter ammo consumption and
fighter-only projectile owners. Invalid wire type and civil ammo records are
rejected. Existing combat tests explicitly select the armed fighter; low-level
sweep/rate/damage assertions were retained.

## A320 visual mapping

`animate_a320.py` creates actual separate airfoil geometry and rigid pivots while
retaining the detailed fuselage, livery, nacelles and mechanical parts. No whole-
aircraft deformation or simulation feedback is used.

| System | State mapping and status |
|---|---|
| Ailerons | Actual roll stick/control authority, opposing left/right deflection |
| Elevators | Actual pitch stick plus persistent trim; both tail elevators |
| Rudder | Actual rudder pedal/control authority and vertical-tail pivot |
| Flaps | Three per wing, gradual 4 s travel, rotation plus small extension |
| Spoilers | Five per wing, actual spoiler command, 0.6 s visual deployment |
| Gear | Nose and both main assemblies; reversible 5 s normalized transition |
| Doors | Existing main-door geometry follows its gear assembly; no independent sequence |
| Steering/wheels | Nested nose steering, speed-limited authority, ground-speed wheel rotation |
| Engines | Both fans/spinner details rotate from actual N1; faint normal exhaust |

Gear/flap smoothing belongs solely to `AircraftPose`; authoritative gear/contact
and simplified flap/spoiler aerodynamics are unchanged. Visual wheel rotation is
near-ground/gear-down only. Unit tests verify signs, timing, reversal, wheels,
steering, fan advancement and no writes to aircraft state. Static screenshots
demonstrate pose endpoints/deflections, not a measured fan RPM or a video of motion.

## Fighter implementation and measurements

Falcon is an original repository-owned F-16-style single-engine model, generated
by Blender tooling. No third-party aircraft asset was downloaded or redistributed.
It has a continuous lofted fuselage, pointed radome, bubble canopy, chin intake,
LERX/swept wings, flaps/flaperons, all-moving stabilators, rudder, ventral fins,
nozzle petals, articulated gear/steering/wheels, simple markings and navigation
lights. Editable source is `output/Falcon.blend`; owned 256x256 paint is embedded
in the GLB. This is recognizable game-level geometry, not exact F-16 CAD.

Independent configuration: 10,500 kg; inertia 12,500/82,000/95,000 kg m2; wing
27.9 m2/span 9.8 m/MAC 3.45 m; one 79 kN dry-thrust engine, 0.7 s spool lag;
independent lift/drag/moment/control coefficients, 19-degree clean stall threshold,
landing gear/oleos and control limits. No afterburner state or flame toggle.
The existing 17-sphere swept hit algorithm uses definition-specific fighter scale.
Gun remains 600 RPM, 850 m/s, 25 damage, 600 rounds, bounded life/range; muzzle is
a fighter reference point and all fire/rate/hits/damage remain authoritative.

Measured deterministic 120 Hz scenarios, also saved in `fighter_metrics.log`:

| Scenario | Measurement |
|---|---|
| Trim/cruise, 120 s | TAS 110/180/250 m/s; pitch 7.185950/2.022143/0.521959 deg; throttle 0.424212/0.472332/0.641499; altitude drift <=6.64e-7 m |
| Taxi | 6.320368 m/s, heading response 29.455881 deg; parked height 1.715464 m, stopped speed <9e-10 m/s |
| Takeoff | Liftoff 15.883333 s, 98.470565 m/s, 760.072564 m run; 80 s altitude 2310.139559 m, TAS 262.277973 m/s |
| Sustained turn, 20 s | Heading change 32.559281 deg, bank 30.072477 deg, load 1.161453 g, height 1486.774124 m |
| Stall/recovery | Final AoA 2.241605 deg, TAS 183.611360 m/s; altitude loss 162.965426 m; warning clears |
| Landing | Touchdown 29.583333 s, 79.820141 m/s, sink 1.183068 m/s; rollout 1149.296402 m; peak contact 152,762.920472 N; stable stop |
| Finite states | 42 extreme speed/angle cases, 2 simulated seconds each; finite state/unit quaternion |

These tests use simple test-pilot feedback where stated, not new game autopilot.
Fighter realism is intentionally approximate and not a flight-certification claim.
All original A320 M1 scenario thresholds/calibration remain unchanged.

## Effects and materials

The 4,096-value reserved particle pool has no per-particle heap allocation. It
compacts living entries in place, persists age/motion every update and replaces
round-robin at capacity. Finite bounds, velocity, drag, gravity, lifetime, size,
opacity and variation are supported; emitter clocks cap at 64 aircraft. A test
emits 10,000 particles into capacity 32, requires exactly 9,968 replacements and
complete expiry. Invalid/nonfinite particles are rejected. Diagnostics expose
current/peak particle count and CPU render-preparation time.

Procedural UV radial falloff replaces opaque rectangles. Alpha-blended billboards
fade and expand; tracers/debris are velocity-oriented soft streaks. Depth testing
is retained without depth writes. There is no sampled-depth intersection softening,
refractive heat distortion or heavy VFX framework.

Normal airliner exhaust is deliberately very faint, not black smoke; fan motion
is N1-driven. Fighter dry exhaust scales with N1 squared and uses a stronger,
outside-nozzle emitter. Damaged aircraft emit smoke; destruction produces a warm
expanding flash, rising/expanding smoke and bounded gravity/drag debris. Reliable
server events drive muzzle flash, ballistic tracers, impact flash/sparks/smoke and
destruction once; hit projectile IDs retire tracers. Hit detection was not moved
for visual alignment. Color packing is explicitly ABGR, with a warm-flash test.

Contrails require altitude >7,000 m and speed >90 m/s, stay in the atmosphere,
overlap at roughly 2 m emission spacing, expand and expire after 8 s. High-load
local wingtip vapor requires |g|>2.4, 300-4,500 m and speed >65 m/s, and is short/
restrained. This is an atmospheric humidity approximation, not complex weather;
remote vapor has no independent aerodynamic-load reconstruction. Navigation
lights and A320 strobe puffs are present; dedicated beacon/landing-light switching
and volumetric beams were not added.

PNG/JPEG decoding supports external relative files, data URIs and embedded GLB
buffer-view images. Dimensions cap at 4096 and encoded/decoded images at 64 MiB.
Base-color, metallic/roughness and emissive textures/factors, UV0, wrap/filter and
alpha mask/blend are supported. Fighter's embedded PNG is tested headlessly and
rendered. Textures cache by image/sampler within each shared aircraft model;
materials reference shared textures, GPU handles are freed once on teardown.
Normal maps, mip generation, skins, general animation tracks and most glTF
extensions remain unsupported; clearcoat factors are reported/ignored.

## Captured and inspected evidence

Native 1280x800 OpenGL screenshots are generated into `docs/images/m3_6/` by
`scripts/capture_m3_6.py` and are not version controlled. These files were
opened and visually inspected, not inferred from successful compilation.
There are 25 PNG captures: 18 deterministic fixtures, four actual network/combat
views, one installed-package view and two final graphical-smoke screenshots.
Smoke screenshots reflect the test's resized window, rather than 1280x800.
`manifest.json` records deterministic fixture settings and capture frame times.
Fixture physics is frozen, visual time advances at 60 Hz; fixture shots/impacts/
destruction are explicitly **not** proof of server gameplay.

| Evidence | Inspected result |
|---|---|
| `a320_parked.png` | Real marked A320 on runway, detailed model and all gear down |
| `a320_surfaces.png` | Raised spoilers, opposing aileron/tail/rudder deflection, nose steering |
| `a320_flaps.png` | Actual separate trailing-edge flap geometry deployed |
| `a320_gear_transition.png`, `a320_gear_up.png` | Different intermediate/retracted poses; wheels fold clear of the nose belly |
| `a320_flight.png`, `a320_exhaust.png` | Clean chase/rear view, no thick normal smoke; exhaust is deliberately difficult to see |
| `a320_contrail.png` | Continuous soft paired trails, not opaque rectangles/dotted chains |
| `fighter_parked.png`, `fighter_flight.png`, `fighter_chase.png` | Modern fighter silhouette, canopy/intake/nozzle and clean gear-up flight |
| `fighter_surfaces.png` | Independent stabilators/flaperons/rudder and nose steering |
| `fighter_gun.png` | Warm muzzle and long soft tracer streak (fixture) |
| `fighter_exhaust.png` | Rear nozzle with restrained dry exhaust; no afterburner flame |
| `mixed_aircraft_fixture.png` | Both model types together, at correct relative scale (fixture) |
| `impact_fixture.png`, `explosion_fixture.png`, `destruction_fixture.png` | Warm soft impact/flash, fading expanding smoke and debris (fixtures) |

Live evidence is separate: `mixed_network.png` and `mixed_network_fighter.png`
use an ordinary server and two native clients (A320 and fighter), with the real
v3 sampling/rendering path and per-type HUD. The dedicated
graphical combat fixture uses two actual fighter clients and the authoritative
server; `combat_network_A.png`/`combat_network_B.png` capture that path. It only
arranges initial gun-axis encounters/alternating roles after respawn, not client
hits. Assertions require shots, received hits, destruction, respawn, finite
state and rendered effect frames. Exact logs accompany the images.
Final mixed run: 192 snapshots per client, 1,859/1,771 remote-rendered frames.
Final combat run: 217 server shots, 20 hits, five destructions and four respawns;
clients received 8/12 hits and both passed their original graphical assertions.

Capture iteration caught and corrected overwritten fixture controls, dotted
contrails, reversed flash colors, incomplete nose-wheel clearance, inverted
mirrored fighter foil normals/zero-thickness fins, and unrelated
desktop F1 events obscuring test images. Test modes ignore untagged keyboard
events; ordinary user input remains unchanged. Graphical smoke uses actual ImGui
item bounds for its click, retaining all original reset/control assertions.
Final smoke reruns also exposed asynchronous window-restore timing: screenshot
frame 150 could occur while minimized. The same event order now synchronizes
SDL minimize/restore and checks restoration before advancing. Both original
Debug/Release smoke assertions pass after this fix; no screenshot or flight
assertion was removed.

## Performance

Release OpenGL on the existing Intel Core Ultra 7 255H / Intel integrated GPU
workstation. Native mixed-type 1600x900 scene, vsync off, MSAA x4, medium shadows
2048, high effects. Each run ramps to the requested **total** aircraft count,
warms up one second, then samples three seconds at stable count. Distances mix
medium/far aircraft. Counts include submitted shadow geometry; older reports
did not always include shadow triangles and are not directly comparable.

Final regenerated-asset measurements (CSV and per-count logs accompany this report):

| Aircraft | FPS | Frame ms | GPU ms | Draws | Submitted triangles | Particles | CPU preparation ms | LOD0/1/2 aircraft |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 157.3 | 6.36 | 4.46 | 179 | 1,129,193 | 4 | 0.193 | 1/0/0 |
| 2 | 151.9 | 6.58 | 4.56 | 347 | 1,231,421 | 6 | 0.347 | 1/1/0 |
| 8 | 118.2 | 8.46 | 5.41 | 830 | 1,448,870 | 18 | 0.834 | 1/6/1 |
| 16 | 116.3 | 8.60 | 4.58 | 1439 | 1,766,861 | 36 | 1.435 | 1/12/3 |
| 32 | 88.2 | 11.34 | 5.53 | 2357 | 2,433,869 | 68 | 2.466 | 1/25/6 |

These short desktop samples have driver/compositor/thermal variation; an earlier
pre-final-foil run measured 76.1 FPS at 32 aircraft. This is an airfield benchmark,
not 32 overlapping near-LOD0 airliners or all aircraft emitting maximum-altitude
contrails. No frame-rate guarantee for different GPUs follows from these samples.

LOD selection uses 15% hysteresis around radius*5/radius*55, with unit coverage
for entering/exiting both thresholds. Shadow rendering uses LOD2 and caps at
eight near casters. Distant airliners therefore use 11,867 triangles, not 1.12M.

Actual eight-second GNS flight load, unchanged full-world snapshot/input scheme:

| Clients | Mean server tick us | p95 tick us | Snapshot encode/send us | Payload in B/s | Payload out B/s | Wire out B/s | Send failures |
|---|---:|---:|---:|---:|---:|---:|---:|
| 2 | 25.550 | 53.246 | 26.454 | 153,880.5 | 22,772.8 | 24,656.6 | 0 |
| 8 | 45.041 | 85.021 | 55.372 | 586,205.0 | 348,083.0 | 362,431.3 | 0 |
| 16 | 63.165 | 121.479 | 96.002 | 1,172,170.8 | 1,381,478.0 | 1,423,354.4 | 0 |
| 32 | 94.360 | 179.764 | 185.089 | 2,359,012.8 | 5,504,204.0 | 5,662,860.5 | 0 |
| 64 | 153.808 | 244.284 | 438.175 | 4,991,880.2 | 21,859,016.0 | 22,455,261.4 | 0 |

Raw CSV: `network_load.csv`. Queue peaks 17-18. No Internet capacity guarantee
follows from loopback measurements; full snapshot fanout still scales quadratically.

## Builds, tests and Windows status

All four configurations build. Every historical M1/M2/M3 suite remains registered;
combat world fixtures select fighter rather than weakening assertions. Native
graphical smoke is run separately/serially to preserve desktop focus. Headless
builds test hierarchy/particles/animation without SDL/bgfx/GLM.

| Configuration | Registered cases | Observed result |
|---|---:|---|
| Debug native | 56 | 55/55 full CPU/network plus 1/1 final graphical smoke; final regenerated-asset subset 13/13 |
| Release native | 56 | 55/55 full CPU/network plus 1/1 final graphical smoke; final regenerated-asset subset 13/13 |
| Debug headless | 53 | 53/53 full suite; final regenerated-asset subset 12/12 |
| ASan/UBSan headless | 53 | Earlier full 53/53; final full rerun 52/53 (`network.bad`); original-assertion isolated recheck 3/3 |

Sanitizers use leak detection/halt-on-error and the existing documented 16 MiB
ASan quarantine, without suppressing checks. An initial run without that setting
failed the unchanged 16 MiB RSS growth assertion due to allocator quarantine;
the documented setting passed both 180-second soaks. Concurrent-load reruns also
exposed nondeterministic adverse-network fixture deadlines (one final combat
respawn, one short opposite-roll observation); these failures are recorded, not
erased by lowering assertions. Separate original-assertion rechecks are recorded
alongside full-suite results. The final sanitizer run completed in 274.56 s,
with both soaks passing (180.17/180.18 s); its unchanged 0.8 s opposite-roll
probe failed in `network.bad`. Three isolated repeats then passed in 36.78 s.
`sanitizer_full_pass.log`, `sanitizer_closure.log` and
`sanitizer_closure_bad_recheck.log` preserve these distinct outcomes. Do not read
the isolated recheck as a clean final full-suite run. No ASan/UBSan/leak finding
was reported.

Windows source uses standard C++/SDL/GNS APIs; Linux-only RSS tooling is guarded.
No runtime symlink or POSIX path dependency remains. A VS2022/vcpkg workflow is
prepared in `.github/workflows/windows.yml`, including DXBC shader compilation.
Actual MSVC build, D3D11 initialization/drawing, fullscreen and controllers
**have not been executed**. The user's Windows 11 friend must run those checks;
the workflow's existence is not a successful Windows result.

## Known weaknesses and next milestone

Remaining limits: game-level fighter coefficients/mesh, simplistic stall/contact
and flap aerodynamics inherited from the core, approximate hinges/gear doors,
no detailed cockpit, no structural breakup, no depth-aware soft intersections,
no refraction, very subtle dry exhaust, simple conditional humidity, no remote
load-based vapor, no mipmaps/normal mapping, and the expensive A320 LOD0 if many
airliners intentionally crowd the camera. Windows runtime is unverified. Adverse
impairment tests retain stochastic timing sensitivity; do not hide this as green
determinism. Generated model sources and runtime geometry remain repository-owned.

Recommended exact next milestone: **M3.7 - Network/World Bandwidth Optimization
(option A)**. Preserve v3 aircraft/weapon authority and regression semantics;
measure/reduce repeated input payload, add versioned snapshot delta/quantization
and suitable interest management, with loss recovery and bounded queues. At 64
clients, ~21.9 MB/s outgoing payload and ~5 MB/s incoming payload dominate the
technical scale problem while simulation ticks are sub-millisecond. Complete the
prepared Windows build/runtime checks as a prerequisite to distributing that work.

Option B (missiles/radar) should wait for transport/world bandwidth headroom and
Windows acceptance. Option C (another focused visual/content pass) would improve
gear sequencing, normal maps/cockpit/lighting and close-up polish, but is secondary
to the measured fanout cost. No missiles, radar or optimization work was started
in M3.6. Stop at this milestone.
