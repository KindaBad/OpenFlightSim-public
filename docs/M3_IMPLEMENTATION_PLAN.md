# M3 audit and implementation plan

Audit: World owns stable monotonic aircraft IDs, reusable spaced spawn slots and
120 Hz Simulator stepping. Server binds each GNS connection to one World player,
limits ingress and publishes 24 Hz full snapshots. Explicit v1 encoding projects
State/Controls. Prediction has 512-entry queues/history; RemoteTrack has 32 samples.
Renderer takes State values and never owns simulation. Bots and real GNS tests
exercise shared World/Client/Server paths. No combat/life state exists yet.
The working tree contains the existing project as untracked files; preserve it.

1. Add an independent headless combat module: one configurable gun, pre-reserved
   bounded rounds, moving sphere-region sweeps, health and authoritative events.
2. World validates queued fire-state commands; server checks connection ownership
   and ingress rate. World schedules destruction and safe spaced respawns using
   existing slots. Life generation rejects delayed input from previous lives.
3. Version protocol to v2: fire state, bounded combat event batches, health/ammo/
   generation/respawn/cooldown snapshots. Reconstruct visual rounds from spawns;
   never publish projectile state every tick. Encode once per broadcast batch.
4. Reset local prediction and remote interpolation on life changes. Add held-fire
   input, gun-axis sight, tracers, impacts/explosion indication and diagnostics.
5. Add deterministic combat scenarios, real GNS local/good/moderate/bad scenarios,
   abuse tests, multi-minute repeat-cycle soak and 2/8/16-player profiling.
6. Preserve all existing assertions (update only changed version/wire layout), run
   Debug/Release/headless/sanitizers and graphical checks where available. Record
   actual results, performance/bandwidth and the evidence for lag compensation
   and next-milestone decisions in M3_COMBAT_VALIDATION.md.

No missiles, radar, subsystem damage or networking redesign.
