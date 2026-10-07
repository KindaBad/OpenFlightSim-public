# OpenFlightSim

The clean public source is [OpenFlightSim-public](https://github.com/KindaBad/OpenFlightSim-public).
Windows builds and tests run automatically there. Aircraft content and the
research-only reference documents are separate from the source; see
[the publication notes](docs/PUBLIC_SOURCE.md).

The separate [OpenFlightSim Launcher](docs/LAUNCHER.md) provides a native
Windows/Linux flight-planning UI, renderer presets, multiplayer launch options,
verified HTTPS updates, selective repair and rollback. The same guide covers
running it from source, packaging releases and publishing update manifests.

Windows player releases include a single **OpenFlightSim Setup executable**:
open it, click **INSTALL GAME**, and then press **PLAY** in the launcher.
It downloads the complete game, installs without administrator access, and adds
a Start Menu shortcut. The publisher must configure the release host and approved
asset pack before this download can be distributed; see [the release guide](docs/LAUNCHER.md).

To open it from this checkout, run `./play.sh` on Linux or `play.bat` on Windows.
The first run prepares the launcher and builds the simulator if needed.

Native C++23 flight-simulation foundation for a future multiplayer flight game.
M0/M1 flight and M2 multiplayer are preserved. M3 adds server-authoritative guns,
swept projectile hits, damage, destruction and four-second respawns. The dedicated
server, prediction/reconciliation and remote interpolation remain in place. M3.6
adds articulated aircraft, an A320 reference implementation and a Su-57 engineering model,
backend-specific shaders, texture support and bounded soft effects. M3.66 adds
component airflow, finite actuators/FBW, variable loading/fuel, Mach effects and
shared wind while preserving 120 Hz authority. M4 adds reusable airborne radar,
filtered tracks, IR and active-radar missiles with physical 6-DOF flight,
server-owned seekers/guidance/damage and bounded compact replication. No game engine.

The preserved headless core implements configurable A320-style 6-DoF dynamics,
ISA atmosphere, flight controls, engines, flat-runway landing gear, instruments
and a C API. The native client uses SDL3, bgfx, Dear ImGui and GLM. It renders a
physically based atmosphere, volumetric clouds, terrain with lakes and forest to
the horizon, the airfield and the aircraft, with chase, orbit, free and
flight-deck cameras; see [docs/RENDERER.md](docs/RENDERER.md).
Physics uses a bounded fixed 120 Hz simulation clock and interpolated rendering.

The existing detailed A320 Blender scene, delivery renders, validation tools
and OCIO files are retained locally. The client preserves GLB nodes/pivots,
batches only compatible static geometry and builds three hysteretic
distance-based detail levels. Runtime models are ordinary files expected at
`output/Airbus_A320.glb` and `assets/aircraft/su57/su57_lod0.glb`, with no symlink requirement.
These are generated binaries and are **not** version controlled; see
[docs/ASSETS.md](docs/ASSETS.md) for how to obtain or regenerate them, and
[docs/BUILDING.md](docs/BUILDING.md) for the build. A fresh clone therefore
builds and runs the physics, network and tooling tests, while the aircraft
asset suites report CTest **SKIPPED / NOT RUN** until the models are supplied.

The data-driven flight-model milestone adds full tensor inertia, pure continuous
evaluation, optional RK4 and a locally imported NASA F-16 reference with independent
checkcases. Production aircraft remain engineering surrogates. See the
[validation contract and realism truth table](docs/FLIGHT_MODEL_VALIDATION.md) and
[measured results](docs/FLIGHT_MODEL_RESULTS.md). The `asset-validation` preset
requires every production GLB; missing files fail instead of skipping.

The A320/Su-57 physical audit adds compiled JSON authoring, a CFM56-5B4/P
transport deck and high-lift schedule, reconstructed transport controls,
shared visual/force geometry and unsteady separation/vortex state. The
held-out Airbus approach point passes; other fidelity limits and the takeoff
discrepancy are recorded in [the aircraft audit](docs/AIRCRAFT_PHYSICS_AUDIT.md).
The temporary Falcon is retired; stable wire ID 2 is rejected. Physics geometry
can be displayed from the Effects panel. Current network peers require protocol 13.

M3.68.1 adds scenario metadata and explicit PASS/WARN/FAIL comparisons, load-dependent
inertia corrections, physical nozzle/energy regressions, required content validation,
and measured texture/network costs. The [closure report](docs/M3_68_1_VALIDATION_REPORT.md)
separates public-source agreement from plausibility and lists exact test counts.
See [Su-57 parameter provenance](docs/SU57_PHYSICS_PROVENANCE.md) and
[asset release restrictions](docs/ASSET_RELEASE_PROVENANCE.md) before making fidelity
or redistribution claims. M3.7 replaces live full-world broadcasts with spatial
interest tiers, compact remote state, acknowledged baseline deltas, reliable lifecycle transitions and
1,100-byte snapshot chunks. Owner prediction retains the shared full precision
simulator. See [network validation](docs/M3_7_NETWORKING_VALIDATION.md) and the
[protocol v13 layout](docs/NETWORK_PROTOCOL.md). M3.7.1 bounds recovery requests,
replicates steering, synchronizes remote presentation and adds permanent loss
regressions; see [networking hardening](docs/M3_7_1_NETWORKING_HARDENING.md).

## Build and run

Requires CMake >=3.24, Python 3, Git, Ninja and a C++23-capable GCC/Clang or MSVC. Client
source dependencies are pinned and fetched at configuration; the first build
requires network access. Fedora needs X11 and OpenGL development packages;
see [docs/BUILDING.md](docs/BUILDING.md) before the first client configure.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/client/ofs_client
./build/debug/client/ofs_client --aircraft su57 --airborne

cmake --preset release
cmake --build --preset release
ctest --preset release
```

A headless build needs OpenSSL/protobuf for GameNetworkingSockets and no desktop:

```sh
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
```

Windows commands, sanitizer checks, optional graphical smoke testing and the
exact workstation verification setup are in [BUILDING.md](docs/BUILDING.md).
Linux currently uses X11/XWayland and OpenGL 4.3+. Windows uses Win32/D3D11;
shaderc builds matching GLSL/DXBC variants rather than passing GLSL to D3D11.
Windows has not yet produced a passing build; see BUILDING.md for the current CI state.

## Controls

| Action | Controls |
|---|---|
| Camera mode | Tab cycles free / chase / close chase / orbit / flight deck |
| Start flying / park | F2 for trimmed airborne flight; F3 to reset on runway |
| Pause / parking brake | P pauses offline flight; Backspace toggles parking brake |
| Settings / HUD | F1 shows diagnostics and graphics; F4 toggles HUD |
| Orbit camera | Hold right mouse to orbit; mouse wheel zooms |
| Free camera | WASD; R/F up/down; Shift faster |
| Look | Hold right mouse and move |
| Frame aircraft | Home or debug UI button |
| Aircraft pitch / roll | W pushes down, S pulls up; A/D bank left/right |
| Mouse aim | X toggles; move the mouse to place the aim ring, hold right mouse to look around |
| Rudder / nosewheel | Q left, E right |
| Throttle | Shift / Ctrl, or debug slider |
| Fire selected weapon | Hold Space / left mouse / gamepad right trigger for guns; press once to launch a missile online |
| Select gun / IR / active radar | 1 / 2 / 3 |
| Next / previous radar target | T / Y |
| Lock / unlock selected target | L |
| Gun camera | V toggles flight-deck camera; armed aircraft have a gun sight |
| Gear / brakes | G toggles simulation gear; B wheel brakes; UI parking brake |
| Flaps / airbrake | F cycles 0/25/50/75/100%; H toggles airbrake/spoilers |
| Su-57 / Typhoon maneuver mode | M toggles; also available in the flight controls panel |
| Fullscreen / quit | F11 / Escape, or window close |
| Simulation tooling | UI pause, reset on runway, reset airborne |

The default chase view follows the aircraft. Use **Fly now** or F2 for a trimmed
airborne start (1,000 m, 110 m/s), or release the parking brake with Backspace
and increase throttle with Shift. You can launch directly in flight with
`./build/release/client/ofs_client --airborne`. The free camera moves independently; its WASD/R/F/Shift keys control the camera.
Switch back with Tab to use aircraft keys. A connected SDL gamepad maps
left X/Y to roll/pitch and right X to rudder with a deadzone. Physical controllers
are not yet tested. Gear retracts visually over five seconds on the A320 and
five seconds on the Su-57. Flaps, spoilers, control surfaces, wheels and A320
fans follow aircraft state; visual interpolation never changes physics.

**Mouse aim** (X, the flight panel checkbox, or F1 > Controls) is pointer flying
in the style of War Thunder. The mouse moves an aim ring that is fixed in the
world, the chase cameras look along it, and an instructor flies the ordinary
flight model until the nose (the small cross) reaches the ring: wings level
with elevator and rudder for small corrections, banking into the turn and
pulling for large ones, within the stall margin and g limit of the aircraft.
It only fills axes left neutral, so W/S, A/D, Q/E and the gamepad sticks
override pitch, roll and yaw individually while held. In the flight deck the
view stays fixed to the airframe and the ring is kept within sight of the
nose. The pointer is released while the game is paused, F1 is open or the
aircraft is destroyed. The setting and its sensitivity are saved in
`graphics.cfg` (`mouseAim`, `mouseAimSensitivity`).

The Su-57 **maneuver mode** sharply increases pitch and roll response and relaxes the
AoA protection for high-incidence maneuvers while retaining G limits, rate feedback,
and the physical thrust-vectoring actuators. It is fully active below 210 m/s,
fades to normal by 300 m/s, and is inhibited with gear down. The HUD shows the
selected mode and standby status; press M again to restore normal flight.
This is an estimated gameplay control law, not the actual Su-57 FCS.

Wing condensation appears automatically during humid high-load or high-AoA flight.
The Su-57 gains thin translucent sheets along the swept wing surfaces and
narrow tip vortex wisps. Opacity follows condensation strength, with a single
layer, soft edges and short lifetimes to keep the aircraft visible. Adjust
**F1 → Effects and overlays → Relative humidity / Wing condensation**; Medium
or High effect quality is required. Vapor dissipates after unloading, with no
emissions while stationary. Cold-altitude exhaust contrails remain a separate effect.
See [maneuver and condensation validation](docs/SU57_MANEUVER_MODE.md).

The Typhoon has the same M-key **maneuver mode** on its canard control law, with
smaller gains because it has no vectoring nozzles. See
[Typhoon maneuver mode and model](docs/TYPHOON_MANEUVER_MODE.md).
Both network peers now require protocol **13**; rebuild client and server together.

The elevator-trim slider keeps a persistent pitch offset when the stick is released.
Airborne reset solves this offset and throttle from actual force/moment balance.

## Dogfights with bots

Press **F5** or click **Fight bots** in Flight Operations to start a local
fight against two enemy Typhoons. Starting from an unarmed aircraft switches
you to the Su-57. You spawn airborne with guns and missiles ready; one opponent
approaches ahead and another pursues from behind. Press F5 again or click
**End dogfight** to return to solo flight.

Bots bank and pitch through the ordinary flight model, pursue live players,
lead their gun shots, fire short bursts, break away when hit, avoid terrain,
and launch radar missiles when their actual radar/seeker is ready. Bots and
players use the same ammunition, collision, damage and four-second respawn
rules. Bots ignore each other and patrol when no live player is available.

Hold Space or left mouse to fire the gun. Use **T/Y** to select a target,
**L** to lock, **2/3** for IR/radar missiles, and press the fire control to
launch. **1** switches back to the gun. The HUD shows enemy labels, radar,
health and ammunition.

Launch directly with a chosen opponent count (1–8):

```sh
./build/release/client/ofs_client --aircraft su57 --bots 2
```

A dedicated multiplayer server can also provide opponents:

```sh
./build/release/network/ofs_server --bots 2
```

The bot count defaults to zero on a dedicated server. Human slots plus bots
must fit within the 64-aircraft world limit. Bots join when the first player
connects; rebuilding the server and client is sufficient, with no protocol change.

## Radar and missiles

Online Typhoon and Su-57 carry two **Dev IR-90** and two **Dev AR-157** development
missiles. A320 and SR-71 remain unarmed. Cycle radar contacts with T/Y, use L to
lock or unlock, choose 2/3, and press the fire control once per launch. The server
validates seeker/support readiness, inventory, minimum range and cooldown. The
HUD distinguishes contacts, selected targets and locks and labels its range cue
**Kinematic estimate**. Being inside that estimate does not guarantee a hit.

Missiles inherit the aircraft's motion; finite rocket motors, drag, actuator
limits, physical forces/moments and proximity/contact sweeps determine their
flight and damage. Mounted stores affect mass, CG and inertia. Respawn restores
inventory and retires stale target generations. Smoke/plumes and detonations are
local effects derived from server state. Solo guns remain available; radar and
missile combat are available in local bot dogfights and on multiplayer servers.

See [M4 validation](docs/M4_RADAR_MISSILES_VALIDATION.md) for measured physics,
16-player impairment/soak results, packet and performance budgets, exact test
counts and limitations, and [weapon provenance](docs/MISSILE_PHYSICS_PROVENANCE.md)
for every model assumption. Countermeasures, warnings and ECM are future M4.5 work.

## Authoritative dogfighting

Run the existing dedicated server and two native clients in separate terminals:

```sh
./build/headless/network/ofs_server --port 27020
./build/debug/client/ofs_client --server 127.0.0.1 --name Alice --aircraft su57
./build/debug/client/ofs_client --server 127.0.0.1 --name Bob --aircraft su57
```

Use V for the gun camera, fly with the existing controls, and hold Space to fire.
The server derives each muzzle transform and round velocity, validates rate/ammo,
and owns hits and health. Tracers/impacts are display feedback. Four 25-damage hits
destroy an aircraft; armed aircraft respawn after four seconds with their configured ammunition.
The default A320 has no gun, ammunition, muzzle or combat reticle. The server
ignores its fire commands before weapon queuing; mixed aircraft sessions are supported.
The network panel shows health, ammo, cooldown, kills/deaths and hit diagnostics.
Gun rate, velocity and ammunition follow the aircraft configuration (Su-57: 1,500 RPM,
860 m/s, 150 rounds; Typhoon: 1,700 RPM, 1,000 m/s, 150 rounds), with gravity and a 2,400 m / 3 s cap. Respawn retains
entity identity and clears old life input/prediction. Solo flight supports the same gun controls, muzzle transforms, gravity,
ammunition and cooldowns without a server. Bullets stop at terrain, and tracers
show a bright core, glow and visible end-on tip. F2/F3 reload solo ammunition.

Violent crashes destroy the aircraft on first contact (body closing speed at
least 18 m/s, or landing-gear impact at least 25 m/s). The intact model disappears
and a large fireball, burning debris and smoke replace it. Ordinary landings and
minor scrapes retain incremental damage. F2/F3 restart solo flight; online
respawns remain server-owned.

[Combat validation](docs/M3_COMBAT_VALIDATION.md) records actual tests, adverse
conditions, graphical runs, soak, performance/bandwidth and limitations. There is
no missile, radar, targeting, team, armor or component damage system.

## Status and documentation

Fedora/GCC Debug and Release verification details, screenshots and limitations
are recorded in [docs/VALIDATION.md](docs/VALIDATION.md).

- [Repository audit](docs/AUDIT.md)
- [Architecture](docs/ARCHITECTURE.md)
- [M3.7.1 networking hardening](docs/M3_7_1_NETWORKING_HARDENING.md)
- [Network protocol v13](docs/NETWORK_PROTOCOL.md)
- [M3.66 advanced physics, measurements and limitations](docs/M3_66_ADVANCED_PHYSICS_VALIDATION.md)
- [M3.6 aircraft, visual validation and performance](docs/M3_6_VISUAL_AIRCRAFT_VALIDATION.md)
- [M2 multiplayer validation and capacity](docs/M2_MULTIPLAYER_VALIDATION.md)
- [M3 combat validation and measured performance](docs/M3_COMBAT_VALIDATION.md)
- [Coordinates and units](docs/COORDINATES.md)
- [Roadmap](docs/ROADMAP.md)
- [Preserved A320 assets](docs/ASSETS.md)

The model has automated trim, control, stall, taxi, takeoff, cruise,
approach and landing regressions, including an uninterrupted 420-second flight
cycle. M1 measurements are historical; current model details and before/after results
are in the M3.66 report.
Measured results and realism limits are in
[docs/M1_FLIGHT_VALIDATION.md](docs/M1_FLIGHT_VALIDATION.md).

Run individual scenarios or emit every measurement without a renderer:

```sh
./build/headless/tests/ofs_flight_scenarios all
./build/headless/tests/ofs_flight_scenarios trim
./build/headless/tests/ofs_flight_scenarios cycle
```

The 0.4.0 renderer derives sunlight, skylight, haze and exposure from one
atmosphere model in real units, and adds volumetric clouds, cascaded shadows,
instanced forest, lakes and a 160 km draw distance. Its design, settings, cost
and verification are in [docs/RENDERER.md](docs/RENDERER.md). The earlier
[graphics refresh notes](docs/GRAPHICS_REFRESH.md) record the fixes it built on.

M3.6 stops at multi-aircraft flight and repeatable gun combat. Missiles, radar,
terrain collision, Jolt and audio remain outside this milestone.

## Fly together

```sh
./build/headless/network/ofs_server --bind 0.0.0.0 --port 27020 --max-players 16
./build/debug/client/ofs_client --server 127.0.0.1 --port 27020 --name Alice
./build/debug/client/ofs_client --server 127.0.0.1 --port 27020 --name Bob
./build/debug/client/ofs_client --server 127.0.0.1 --port 27020 --name Fighter --aircraft su57
```

Replace loopback with the dedicated server's reachable numeric IP address. The
server owns each aircraft at 120 Hz and sends full snapshots at 24 Hz. Each client
predicts through the same simulator, reconciles against authority, and renders
remote aircraft from a bounded interpolation history. Network diagnostics appear
in a separate panel; online simulation reset/pause tools are disabled. Airborne
spawns are trimmed and spaced; `ofs_server --ground` selects runway spawns.

Headless test clients and profiles:

```sh
./build/headless/network/ofs_bot --seconds 60 --pulse
ctest --test-dir build/headless --output-on-failure --parallel 3
./build/release/network/ofs_replication_benchmark all
./build/release/network/ofs_net_load
```

Full tests include real loopback networking, security/limits, impairment presets,
three-minute flight and combat soaks and all prior flight regressions. Exact measured results,
launch instructions, bandwidth costs and remaining limitations are documented in
[docs/M2_MULTIPLAYER_VALIDATION.md](docs/M2_MULTIPLAYER_VALIDATION.md) and
[docs/M3_COMBAT_VALIDATION.md](docs/M3_COMBAT_VALIDATION.md).

The Eurofighter Typhoon is available with `--aircraft typhoon`.
It has an independently configured twin-engine flight model, reheat above 85%
throttle, articulated foreplanes/elevons/airbrake/gear/nozzles, Luftwaffe markings
and four LODs. Its model is by bohmerang (CC BY-NC-SA 4.0); the original
Austrian 7L-WA model described in the M3.65 report is retired from the runtime. M3.6 originally introduced protocol v5; current client/server builds require v13. Rebuild both together. See the inspected Blender
and native captures, measurements and approximations in
[docs/M3_65_TYPHOON_VALIDATION.md](docs/M3_65_TYPHOON_VALIDATION.md).

M3.66 measurement tooling:

```sh
./build/release/tests/ofs_advanced_scenarios supersonic
./build/release/tests/ofs_advanced_scenarios telemetry output/physics.csv
./build/release/tests/ofs_physics_benchmark
```
