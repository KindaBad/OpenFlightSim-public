# Regional damage

A hit damages the part of the aircraft it struck. The part's condition is the
surface and engine health the flight model already integrates, so there is one
damage state: it flies, replicates, predicts and replays like the rest of
`ofs::State`. Rules are in `core/include/ofs/damage.hpp`; values are gameplay
choices, not lethality claims about real aircraft.

## Parts

| Part | State it changes | Strength (hit points) | Share passed to the airframe |
|---|---|---:|---:|
| Fuselage | body drag only | - | 100 % |
| Left / right wing | `surface_health[0/1]`, `surface_drag[0/1]` | 100 | 50 % |
| Tail | fin `surface_health[4]`; tailplanes `[2/3]` at 60 %, floored at 0.35 | 80 | 50 % |
| Left / right engine | `engine_health[0/1]`; below 0.3 the engine stops | 60 | 60 % |

An aircraft has 100 hit points. A hit of `d` on an intact part takes `d /
strength` off the part and `d x share` off the hit points; a part that is
already destroyed protects nothing, so further hits there count in full. With
the fighters' 34-point cannon rounds: three hits remove a wing, two stop an
engine, three in the fuselage destroy the aircraft. A missile's blast damages
the part it went off beside and is still taken in full by the hit points.

Hits are classified on the server. The 17 collision spheres give fuselage,
wing or tail; a hit within 0.75 m of an engine bay (a capsule running forward
from the exhaust, sized from the aircraft) is that engine's, whichever sphere
it entered. Combat events carry the part struck.

## What damage does in flight

- **Wing.** Lift on that side falls with health, and drag rises (the stored
  factor compensates for the flight model scaling a surface's whole force by
  its health). Measured with a simple level-hold pilot: the Typhoon needs 13 %
  roll stick after one hit and about 55 % after two, and loses 20 to 30 m/s;
  with the wing gone no aircraft stays up. The Su-57's control law hides one
  hit almost completely in level flight and departs sooner when it runs out.
- **Overstress.** A wing below half health has a load limit of `3 + 14 x
  health` g. Pulling past it breaks the wing off (`applyOverstress`), checked
  on the server each tick for damaged wings only.
- **Tail.** A destroyed fin removes rudder authority and weathercock
  stability. Tailplanes mounted with it (A320, SR-71, Su-57) keep 35 %, so
  pitch control weakens but is never lost outright; the Typhoon's canards are
  untouched.
- **Engines.** Thrust scales with health down to 0.3, then the engine flames
  out for good. A burnt-out engine with fuel still counts as a strong heat
  source for infrared seekers.
- **Stores.** A destroyed wing takes the missiles under it; the payload and
  inertia are recomputed.
- An aircraft with no wings is destroyed at once. A crash within 20 seconds of
  being hit is credited to the attacker (`killCreditTicks`).

Respawning restores every part. Ground impacts keep their existing behaviour
and use the same state, so a wing scraped on landing shows and flies the same
way as one that was shot.

## Replication

The owner's full state already carried every health value. Other viewers
previously received only structural integrity; protocol 15 adds seven bytes to
the surfaces field (five surface healths and two engine healths at 1/255), so
everyone sees the same damage. See `NETWORK_PROTOCOL.md`.

## What the player sees

`client/src/damage_visuals.hpp` says where each part is on each model
(measured from the delivered meshes). The surface shader works in body axes
derived from the asset position, so damage stays on the part through control
surface and gear animation:

- Wings and fins lose their outer part once damage passes 40 %, down to a
  ragged stub when destroyed. The cut is `wingRemaining` / `finRemaining` in
  C++ and the same expression in `pbr_fs.glsl`; `regression.shader_sources`
  holds the two together.
- Holes are cut in the skin facing the viewer with the blackened inside of the
  wing behind them, soot trails aft of each hole, the fuselage shows blackened
  pits, and soot spreads around a hit engine. The wall of a burnt-out jet pipe
  glows.
- The piece that left is drawn from the aircraft's own mesh as a separate
  object (`client/src/breakaway.hpp`): it carries on at the aircraft's speed,
  tumbles, slows and falls, trailing smoke. An aircraft that is destroyed
  throws off whatever wings and fin it still had, and its fuselage falls as a
  burning wreck that goes up when it reaches the ground.
- Smoke is laid in continuous lengths from the damaged part: white vapour from
  a holed wing, grey from the tail or fuselage, oily black with flame from a
  dead engine. A wingtip that is gone takes its navigation light with it.
- The flight display shows an airframe diagram coloured by part, the hit
  points, a line for each part in trouble, a red flash at the screen edges when
  hit, and markers for your own hits and kills.

Sun shadows are still cast by the whole airframe: a torn wing keeps its full
shadow.

## Tests

- `damage.parts`, `damage.flight`, `damage.combat`, `damage.world`: the rules,
  their direction of effect on all four aircraft, rounds fired into each wing,
  each engine and the cockpit, authoritative application, replication to
  viewers, snapping under load, loss of stores, crash credit and respawn.
- `battle.view`, `battle.breakaway`, `battle.effects`: display mapping and
  model geometry, piece release and kinematics, and the effects.
- `combat.unit`, `bots.dogfight`: updated for part-dependent hit points.
- Visual scenarios `damage`, `damage-heavy` and `breakup`
  (`ofs_client --visual-scenario ...`) render the states for inspection.
