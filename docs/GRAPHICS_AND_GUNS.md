# Graphics, solo guns and crash destruction

The renderer now adds roughness-dependent sky and ground reflections to PBR
materials, warmer sunward atmospheric haze, three-probe internal cloud shadows,
finer cloud erosion and a soft bloom threshold. Terrain includes distant ridges,
snow at high elevations, varied meadows, crop furrows and rock strata. Airfield
asphalt has filtered seams and repairs; nearby tree crowns have more geometry
and scattered farm buildings add scale. Defaults use high clouds and shadows,
a 24 km view distance and lighter height fog. Existing saved settings still apply.

Solo gun simulation runs on the fixed simulation clock, including in builds
with networking disabled. It uses aircraft-specific ammunition, rate, dispersion,
loaded-CG muzzle placement and inherited aircraft velocity. Pause stops bullets;
F2/F3 clear rounds, effects and reload ammunition. Multiplayer retains server
ownership of shots, damage and ammo. Both paths use shared ballistic displacement,
deterministic dispersion and terrain sweeps, so bullets cannot hit through hills.
Solo flight currently has no spawned combat targets; multiplayer aircraft damage
continues to use the existing swept hitboxes.

Tracers have an HDR glow, a narrow hot core, a camera-facing tip and a minimum
angular width. Hit events retire their projectile's tracer. Ground contact removes
visual rounds and emits an impact flash and dust. Muzzle flashes are spawned after
advancing older effects, so long frames do not erase them before their first draw.
Hits produce a radial spark spray; explosions use overlapping fire puffs, smoke
and terrain-colliding debris. Particle and transient vertex budgets remain bounded.

A severe ground strike is an immediate total loss: 18 m/s normal closing speed
for body contact, 25 m/s for landing gear. The shared simulator sets structural
and engine health to zero, preserving prediction/replay agreement. Rendering
omits the intact wreck, its shadow and afterburners, and emits one fireball per
life with lingering fire and smoke. Minor contacts keep the existing impulse-based
damage. Online destruction and respawning retain the existing authority path.

## Verification

- Release client, server, portable shaders and tests build successfully.
- `combat.solo_gun` checks configured cadence/ammo, muzzle/velocity, exhaustion,
  reset/pause, unarmed aircraft, terrain collision and tracer retirement.
- `combat.unit` includes terrain occlusion before aircraft hits.
- Environment tests cover immediate crash destruction, deterministic replay,
  bounded particles, one fireball per life and one online destruction/respawn.
- Native `--gun-smoke` sends Space through the real SDL input path in solo flight.
  Su-57 consumes 24 of 150 rounds during one second of held input, consistent with
  the configured rate rounded to the 120 Hz simulation clock.
- Native gun, environment and actual physics crash captures are retained locally
  under `output/graphics-refresh/`; generated images and aircraft models are not
  committed. GPU shader pixel regressions verify alpha masks and normal mapping.

The delivered source was also isolated from pre-existing missile changes:
its headless build and 12 targeted combat/visual/environment tests pass, and its
native client compiles with and without networking. The broader 170-test run
passed 168 tests initially; the mountain-dependent Typhoon equilibrium check
passes after selecting flat reference terrain. The remaining
`regression.provenance` failure is pre-existing: the unchanged transport control
law source fingerprint and aircraft manifests are stale.

The graphical captures use Linux/OpenGL. Windows/D3D11 runtime output is unverified.
