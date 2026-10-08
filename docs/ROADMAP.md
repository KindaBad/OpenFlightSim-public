# Roadmap

| Milestone | Scope |
|---|---|
| M0 | Foundation / Native Client Pivot: C++23, headless existing core, SDL3/bgfx/ImGui client, free camera, placeholder aircraft, fixed 120 Hz clock, tests and documentation |
| M1 (complete) | Measured trim, controls/stall/contact corrections, taxi/takeoff/cruise/landing and continuous flight-cycle regressions; physical controller feel remains unverified |
| M2 (complete) | Multiplayer foundation: GameNetworkingSockets, authoritative dedicated simulation, input messages, snapshots, prediction/reconciliation and remote interpolation |
| M3 (complete) | Authoritative gun, swept moving hitboxes, damage/destruction, life-safe respawn, replicated visual feedback, real GNS impairments/soak and performance measurements |
| M3.5 (complete) | Visual and aircraft asset pass: PBR lighting, sky/sun/ground/shadows, four camera modes, glTF pipeline with per-material LOD batching, graphics settings, gameplay HUD and developer panels; see [M3_5_VALIDATION.md](M3_5_VALIDATION.md) |
| M3.68.1 (complete) | Realism, validation and packaging closure: provenance, load/engine/nozzle regressions, measured local assets/texture costs and exact test reporting; see [M3_68_1_VALIDATION_REPORT.md](M3_68_1_VALIDATION_REPORT.md) |
| M3.7 (complete) | Protocol v9: spatial interest and update tiers, compact inputs/remote state, acknowledged deltas, bounded keyframe recovery and MTU-aware chunks; see [M3_7_NETWORKING_VALIDATION.md](M3_7_NETWORKING_VALIDATION.md) |
| M4 (complete) | Reusable radar/tracks, IR and active-radar 6-DOF missiles, stores mass/CG/inertia, authoritative launch/fuse/damage, compact AOI replication; [measured 16-player validation](M4_RADAR_MISSILES_VALIDATION.md) |
| M4.5 (in part, 0.5.1) | Flares, chaff and a missile warning, decided by the server's seekers; see [COUNTERMEASURES.md](COUNTERMEASURES.md). A radar warning receiver and jamming remain |
| Later terrain | Terrain streaming and local chunks; asset import/LOD and origin integration |
| M5 | Improved aerodynamics and aircraft handling |
| M7 | Multiple configurable aircraft |
| M8 | Airports and world content; audio through miniaudio when needed |
| M9 | Large-world improvements |
| M10 | Earth-scale experimentation |

M0 implements no multiplayer, weapons, damage, radar, respawn gameplay, terrain,
airports, lobbies, voice, scripting, mods, editor or advanced weather. Multicrew
is a later design question, not an existing capability. M0's airborne reset is
developer tooling, not respawn gameplay. The preserved A320 GLB integration can
be scheduled alongside asset import once its runtime budget is defined.

M1 results are in [M1_FLIGHT_VALIDATION.md](M1_FLIGHT_VALIDATION.md).
M2 delivers versioned GameNetworkingSockets IP networking, server-owned ticks,
input validation, prediction/reconciliation and remote interpolation. Results and
limits are in [M2_MULTIPLAYER_VALIDATION.md](M2_MULTIPLAYER_VALIDATION.md).
M3 gun combat is complete; results and the measured recommendation to optimize
networking before missiles/radar are in [M3_COMBAT_VALIDATION.md](M3_COMBAT_VALIDATION.md).
Windows/MSVC, WAN and physical controller verification remain open. Later numbering
is provisional.
