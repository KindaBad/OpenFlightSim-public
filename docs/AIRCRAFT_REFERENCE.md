# Aircraft reference figures

What each aircraft is meant to be, the published figures it was set from, and
what the simulator reaches. Speeds in the last column are the highest steady
level speed found by balancing thrust against drag at full power, clean, at the
reference mass, scanning up from low speed and stopping at the first speed the
aircraft cannot hold. Published figures are the commonly quoted manufacturer
and service numbers; where sources disagree the one used is named. Nothing here
is a flight-test validation.

| | A320-214 | Typhoon | SR-71A | Su-57 | JF-17 Block II |
|---|---|---|---|---|---|
| Engines | 2 × CFM56-5B4/P | 2 × EJ200 | 2 × J58 | 2 × AL-41F1 | 1 × RD-93 |
| Thrust each, dry / reheat | 120.1 kN / none | 60 / 90 kN | 106 / 151 kN | 93 / 147 kN | 49.4 / 84.4 kN |
| Empty mass | 42,000 kg | 11,000 kg | 27,546 kg | 18,500 kg | 6,586 kg |
| Internal fuel | 19,004 kg | 4,996 kg | 36,287 kg | 10,300 kg | 2,330 kg |
| Mass at spawn | 64,000 kg | 14,000 kg | 47,546 kg | 25,100 kg | 8,086 kg |
| Wing area | 122.6 m² | 51.2 m² | 149.1 m² | 78.8 m² | 24.43 m² |
| Span / length | 35.8 / 37.57 m | 10.95 / 15.96 m | 16.94 / 32.74 m | 14.1 / 20.1 m | 9.44 / 14.93 m |
| Load limit | +2.5 / −1 g | +9 / −3 g | +2.5 / −0.5 g | +9 / −3 g | +8 / −3 g |
| Published top speed | Mach 0.82 | Mach 2.0; 1.25 at sea level | Mach 3.2 at 24 km | Mach 2.0; 1.1 at sea level | Mach 1.6 |
| Simulated, 11 km | Mach 0.81 | Mach 1.95 | see below | Mach 1.97 | Mach 1.66 |
| Simulated, sea level | Mach 0.78 | Mach 1.22 | Mach 0.92 | Mach 1.08 | Mach 0.97 |
| Simulated without reheat, 11 km | | Mach 1.44 | Mach 0.93 | Mach 1.45 | Mach 1.00 |

Notes on the figures:

- **Typhoon.** Wing area was 50 m² and is now the published 51.2 m². Fuel was
  unlimited above the 3,000 kg it spawns with and is now capped at the
  published 4,996 kg. The BK-27's muzzle velocity is 1,025 m/s. Eurofighter
  quotes supercruise at Mach 1.5.
- **Su-57.** 93 and 147 kN are the figures quoted for the AL-41F1 (izdeliye
  117); 86 to 88 and 142 kN, also in circulation, belong to the Su-35's
  AL-41F1S. Empty mass is quoted as 18,000 or 18,500 kg; 18,500 kg is kept.
  The published supercruise figure is Mach 1.3, so the simulated aircraft is
  about Mach 0.15 fast without reheat.
- **SR-71.** Mass and inertia are NASA's 60,728 lb zero-fuel test aircraft, not
  the 67,500 lb usually quoted as empty weight. In level flight it will not go
  through Mach 1 below about 16 km; above that it reaches Mach 3.4 at 18 km and
  3.6 at 20 km, where the real limit was inlet temperature and not thrust.
- **JF-17.** Empty mass is quoted as 6,411 or 6,586 kg and internal fuel as
  2,268 or 2,330 kg; the later figures are used. The published height is
  4.77 m. No sea-level speed is published. It cannot hold supersonic speed
  without reheat, which is right for the type.
- **A320.** Top speed is the Mach 0.82 operating limit; nothing changed.

## Why the fighters were slow

Until 0.5.7 the Typhoon reached Mach 1.56 and the Su-57 Mach 1.50 at 11 km, and
neither went supersonic at sea level. Two things were wrong. Wave drag was
added on top of a subsonic drag coefficient that already included most of it,
and the thrust lapse gave no credit for intake ram recovery above Mach 1.
`mach_drag_peak` and `mach_drag_supersonic` are now fitted, with the new
`thrust_ram_supersonic` and the density exponent, to the published speeds at
sea level and at altitude together. Subsonic drag is untouched.

Thrust also fell too slowly with height above the tropopause, so top speed kept
rising with altitude and no aircraft had a ceiling. Engineering engines now
lose thrust in proportion to density above 11 km. The level-flight ceilings
that result are about 19 km for the Typhoon and 18 km for the Su-57 and JF-17,
against published service ceilings of 19.8, 20 and 16.9 km.

## Guns

| | Typhoon | Su-57 | JF-17 |
|---|---|---|---|
| Gun | Mauser BK-27, 27 mm | GSh-30-1, 30 mm | GSh-23-2, 23 mm twin barrel |
| Rate of fire | 1,700 rpm | 1,500 rpm | 3,400 rpm |
| Muzzle velocity | 1,025 m/s | 860 m/s | 715 m/s |
| Rounds | 150 | 150 | 200 |

The JF-17's round count is the 200 usually given for the GSh-23 installation;
no primary source was found. Damage per round is a gameplay value: 34 for the
27 and 30 mm guns, 20 for the lighter 23 mm shell.

## Missiles

Stations follow each aircraft's usual air-to-air load. The missiles themselves
remain the two development weapons described in
[MISSILE_PHYSICS_PROVENANCE.md](MISSILE_PHYSICS_PROVENANCE.md): every heat
seeker flies as the Dev IR-90 and every radar missile as the Dev AR-157,
whatever the station is named for.

| | Heat seekers | Radar missiles |
|---|---|---|
| Typhoon | 2 × IRIS-T, outer wing pylons | 4 × Meteor, half sunk into the fuselage |
| Su-57 | 2 × R-74M2, wing-root bays | 4 × R-77M, two tandem bays between the engines |
| JF-17 | 2 × PL-5EII, wingtip rails | 2 × SD-10A, outer wing pylons |

The Su-57 carries all six inside, so none is visible until it is launched. The
model has no bay doors; a missile leaves through the skin. Until 0.5.7 the
Typhoon and Su-57 each carried two of each kind under the wings.
