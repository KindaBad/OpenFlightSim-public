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

## Weapon effects (0.5.0)

- **Gun.** Each round throws a rayed muzzle flash, a short tongue of flame, a
  glow, a puff of grey gun gas and, every other round, a spent case, all
  carried along with the firing aircraft so they stay at the muzzle. Tracers
  are longer and brighter, are not drawn behind the muzzle they have just
  left, and leave a faint smoke line near the camera.
- **Hits.** A flash, twelve sparks and five dark fragments, carried along with
  the struck aircraft, and a puff of smoke.
- **Missiles.** The plume is drawn as a volume with the afterburner's flame
  mesh in a rocket palette, scaled from the missile's length and diameter, with
  a glow at the nozzle and a lit nozzle on the airframe. The smoke trail is
  unchanged in construction. A warhead has its own effect: a white flash, a
  small fireball, a shock ring, 36 fragments and a knot of grey smoke.
- **Destruction.** A flash, fireball, shock ring, burning debris that trails
  smoke as it falls, and the aircraft's own wings, fin and wreck leaving as
  pieces; see [the damage model](DAMAGE_MODEL.md).
- Smoke, flame and rings fade out within 14 m of the eye, so a camera following
  an aircraft through its own trail is not filled by one sprite.

`battle.effects` counts what each event emits and checks that everything
expires and stays inside the pool. Visual scenarios `gun`, `missile` and
`detonation` render them. The flame shader takes its palette per draw because
bgfx uniforms keep the value an earlier draw left in them.

## Reheat, shot origins and decoys (0.5.1)

- **Reheat.** The afterburner is redrawn as a nearly parallel jet instead of a
  tapering cone (`client/shaders/flame.glsl`, `flame_vs.glsl`, `flame_fs.glsl`).
  Three shells share the one tube mesh: a chain of shock diamonds whose radius
  follows a triangle wave so each cell widens to its Mach disc and pinches
  again, a pale blue flame that is orange for its first tenth and violet at
  its tail, and a faint blue sheath that runs on past it. Cells stand 0.72
  model metres apart whatever the reheat setting, so the plume grows by adding
  diamonds, from 0.8 to 6.4 model metres. A disc just inside the nozzle lip is
  the white-hot throat of the jet pipe, and the glow at the nozzle is orange.
  Each shell is weighted in the vertex shader by the length of the sight line
  through the volume it encloses, which gives a bright middle and soft edges
  from the side and a deep glow seen along the jet. Reheat is scaled by
  `flameAdaptation(exposure)`, so at dusk it is brighter against the scene
  than at noon but keeps its colour. A rocket motor keeps its cone and palette.
- **Shot origins.** The server reports where a round left the gun on its own
  clock. The pilot's own aircraft is drawn 0.15 s or more ahead of that clock
  and every other aircraft 0.1 s or more behind it, so at fighting speeds
  tracers and muzzle effects appeared tens of metres behind the pilot's
  aircraft and ahead of the others. `shotOrigin` (`client/src/weapon_visuals.hpp`)
  now starts a reported round at the muzzle of the aircraft as it is drawn,
  with the velocity the server gave it. A report more than 600 m from the
  aircraft belongs to an earlier life and is left alone. Hit sparks are moved
  the same way, along the struck aircraft's velocity by the time between the
  server's tick and the tick that aircraft is drawn at (`hitOrigin`), and an
  aircraft blows up where it is seen. Solo guns were never affected. Which
  rounds hit is unchanged: that is the server's.
- **Decoys.** A flare is one particle with the decoy's own drag and fall, drawn
  as a white-hot point in a flickering glare that never shrinks below a few
  pixels, laying smoke in lengths between where it was and where it is. Chaff
  is a burst of bright strips and a thin haze that stops in the air. Both are
  started where the aircraft is drawn, as rounds are.

`battle.weapons` checks the origins and what each decoy emits; `client.shader_conformance`
draws the shock-diamond shell and checks its ends and its interior. Visual
scenarios `afterburner`, `decoys` and `warning` render them.

## Explosions, bombs and the nuclear cloud (0.6.1)

- **Fire and smoke.** Smoke, dust and fire particles are shaded as lumpy
  balls (`client/shaders/effect_fs.glsl`): a normal is built across the quad
  and pushed about by noise that belongs to the particle, so each has a
  sunlit side, a shaded side and hollows between its lumps, and thins toward
  its edge so neighbours run together. Fire reads its age: it cools from the
  outside in, from white through orange to red, and soot closes over it. The
  particle's age and its own random number ride in a second texture
  coordinate of the effect vertex (13 floats a vertex, from 9).
- **Warheads and bombs.** `CombatEffects::onDetonation` builds a fireball
  from a core and lobes, a knot of dark smoke, fragments and a few burning
  pieces that trail smoke. Within a few metres of the ground it becomes a
  ground burst: the fireball is a dome, fingers of earth are thrown up and
  leave dust where they went, a ring of dust is driven out along the ground
  under a blast ring that lies flat on it (`EffectKind::GroundRing`), and the
  smoke stands as a column. An aircraft blowing up uses the same fireball
  with burning fuel and black smoke. Every part is emitted at every effects
  setting; the lower ones use fewer particles, never none.
- **The nuclear cloud** is a volume, not particles and not a mesh
  (`client/shaders/nuke_fs.glsl`, `client/src/nuclear_cloud.hpp`). Four
  bodies are described analytically about the vertical through the burst: a
  vortex ring under a dome (the fireball at first), a flared stem, a ring of
  dust on the ground and a collar of condensation. A fullscreen pass,
  scissored to the cloud's bounds, marches each view ray through them. The
  fields give a lower bound on the distance to anything dense, so empty air
  is skipped; inside, the cloud noise volume breaks the bodies into billows
  in coordinates that rise with the cap and climb the stem. Each sample is
  lit by the sun through two further density taps and the cap's own shadow,
  by the sky, and by its own heat, which is high through the body and low in
  the crust of the billows once the cloud has cooled, so fire shows in the
  creases. The same pass adds the fireball's light on the terrain and a
  glow round it in the air. `nuclearVolume(age)` gives every number from the
  age of the burst alone, so all clients draw the same cloud; it lasts 190 s.
  Steps a ray: 44, 60 or 84 by effects setting. On the reference laptop on
  battery at 1920x1200, low preset, a burst 11 km ahead costs about 5 ms a
  frame averaged over its first 75 s.
- **Bombs.** `buildBombMesh` (`client/src/missile_mesh.hpp`) gives the Mk 82
  and Mk 84 an ogive nose with a fuze, a tapering body, a conical fin
  assembly with four fins, a yellow band and two suspension lugs, and the B83
  a flat-fronted crushable nose, a long cylinder with red bands and a short
  finned tail.
- **Looking at them.** `--visual-scenario bombs` hangs the three bombs behind
  a parked aircraft and sets a Mk 84 and a Mk 82 off on the ground ahead;
  `--visual-scenario nuclear` sets the weapon off 11 km ahead. `--frame-step
  SECONDS` (at most 0.1) advances a scripted scene by a fixed time each
  frame, so frame N of a capture is always the same moment. A new shader file
  needs CMake to be run again before the build sees it.

`battle.effects` counts what a warhead, a ground burst at high and low
settings, an aircraft and a nuclear burst emit, and checks the cloud's shape
over its life. `client.shader_sources` checks that the renderer fills as many
`u_nuke` slots as the shader reads. Captures were made on Linux/OpenGL;
Windows/D3D11 output is unverified beyond the HLSL compile.
