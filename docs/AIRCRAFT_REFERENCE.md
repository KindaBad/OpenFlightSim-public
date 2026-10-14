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
| Simulated, 11 km | Mach 0.81 | Mach 1.97 | see below | Mach 1.97 | Mach 1.66 |
| Simulated, sea level | Mach 0.78 | Mach 1.26 | Mach 0.92 | Mach 1.08 | Mach 0.97 |
| Simulated without reheat, 11 km | | Mach 1.46 | Mach 0.93 | Mach 1.45 | Mach 1.00 |

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
  4.77 m; the model stands 5.15 m. No sea-level speed is published. It cannot hold supersonic speed
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

## Handling since 0.5.9

The Typhoon and JF-17 were made a little stronger in 0.5.9 through figures
that are estimates, not published ones: maximum lift, induced drag, a small
cut in the Typhoon's subsonic drag, and the rates the control laws allow.
Thrust, mass, load limits and top speeds stay as above. Best turn rates at
500 m with reheat, clean, at spawn mass, from the same force balance as the
speeds:

| | Typhoon | Su-57 | JF-17 |
|---|---|---|---|
| Instantaneous, before / after | 26.9 / 27.5 °/s | 25.2 °/s | 22.0 / 23.0 °/s |
| Sustained, before / after | 20.6 / 21.4 °/s | 19.2 °/s | 20.3 / 21.2 °/s |
| Pitch rate limit, before / after | 0.75 / 0.80 rad/s | 0.75 rad/s | 0.65 / 0.70 rad/s |
| Roll rate limit, before / after | 3.5 / 3.7 rad/s | 3.1 rad/s | 3.1 / 3.3 rad/s |

The Su-57 figures leave out thrust vectoring and its post-stall flight.

### Typhoon since 0.6.5

The Typhoon was strengthened again in 0.6.5, after it proved the weakest
fighter in a turning fight. A stick held fully back took it several degrees
past the angle of best lift, where it lost speed until it wallowed at about
100 m/s. Its canard law now holds the angle-of-attack limit firmly, the limit
(23°) sits below a later stall (27°, as a close-coupled canard delta's vortex
lift allows), maximum lift is 1.85 and induced drag is lower (span efficiency
0.90). Pitch and roll rate limits are 0.95 and 4.4 rad/s. All of these are
estimates; thrust, mass, the load limit and the top speeds are as above.

Flown rather than computed: reheat, clean, spawn mass, 1,000 m, rolled to 80°
of bank and the stick held back for 30 s.

| Entry speed | 360° before / after | Turn rate at the end, before / after |
|---|---|---|
| 120 m/s | not completed / 19.9 s | 9.9 / 17.5 °/s |
| 180 m/s | 28.3 / 18.0 s | 9.9 / 17.7 °/s |
| 250 m/s | 23.3 / 18.1 s | 16.2 / 20.2 °/s |

## Guns

| | Typhoon | Su-57 | JF-17 |
|---|---|---|---|
| Gun | Mauser BK-27, 27 mm | GSh-30-1, 30 mm | GSh-23-2, 23 mm twin barrel |
| Rate of fire | 1,700 rpm | 1,500 rpm | 3,400 rpm |
| Muzzle velocity | 1,025 m/s | 860 m/s | 715 m/s |
| Rounds | 150 | 150 | 200 |

The JF-17's round count is the 200 usually given for the GSh-23 installation;
no primary source was found. Damage per round is a gameplay value: 34 for the
27 and 30 mm guns, 24 for the lighter 23 mm shell (20 until 0.5.9).

## Missiles

Stations follow each aircraft's usual air-to-air load, and since 0.6.6 each
carries its own type's missiles. Lengths, diameters and launch masses are the
commonly published ones, entered from general references and not checked
against a primary source for this release. Little is published about the
R-74M2 and R-77M themselves, so those two use the R-74M's and R-77-1's
sizes and masses. Motors, seekers and manoeuvre limits are estimates set from
the published range, off-boresight angle and load limit of each; none is a
manufacturer's figure. See
[MISSILE_PHYSICS_PROVENANCE.md](MISSILE_PHYSICS_PROVENANCE.md). Until 0.6.6 every heat seeker flew as the Dev IR-90 and
every radar missile as the Dev AR-157; those two remain as what the ground
defences fire.

| | Heat seekers | Radar missiles |
|---|---|---|
| Typhoon | 2 × IRIS-T, outer wing pylons | 4 × Meteor, half sunk into the fuselage |
| Su-57 | 2 × R-74M2, wing-root bays | 4 × R-77M, two tandem bays between the engines |
| JF-17 | 2 × PL-5EII, wingtip rails | 4 × SD-10A, outer and inner wing pylons |

| | Length | Diameter | Mass | Load limit | Seeker off the nose | Published range |
|---|---|---|---|---|---|---|
| IRIS-T | 2.94 m | 127 mm | 87.4 kg | 60 g | 90° | 25 km |
| R-74M2 | 2.90 m | 170 mm | 106 kg | 50 g | 75° | 40 km |
| PL-5EII | 2.89 m | 127 mm | 83 kg | 40 g | 40° | 16 km |
| Meteor | 3.65 m | 178 mm | 190 kg | 36 g (estimate) | 50° (estimate) | over 100 km |
| R-77M | 3.71 m | 200 mm | 190 kg | 35 g (estimate) | 55° (estimate) | about 190 km |
| SD-10A | 3.93 m | 203 mm | 199 kg | 38 g | 45° (estimate) | 70 km |

Published ranges are for a launch at height and speed against a target that
does not turn; the game's battles are far closer, and its missiles are sized
to them. What is kept is the order and the character. Since 0.6.8, flown at
8 km against a target that does not turn, the farthest hit head-on is 91 km
for the Meteor, 70 km for the R-77M and 63 km for the SD-10A, and in a tail
chase 48, 26 and 21 km: the Meteor's ramjet is throttled to hold about
Mach 3.6 and is still burning when it arrives, where the rockets are
coasting. A radar missile fired from beyond 12 km climbs on the way and comes
down on its target.

The IRIS-T and R-74M2 turn their exhaust as well as their fins, so they can
be fired at a target far off the nose: at 2.5 km the IRIS-T hits one crossing
70 degrees off the nose and the R-74M2 one at 60, where the PL-5EII's seeker
looks no further than 40 and its fins cannot make the turn from even 30. A heat seeker is shown the radar's locked
target wherever it is, as far round as its seeker can look; without a radar
lock it finds its own target in the ring at the nose. Every missile but the
PL-5EII also allows for its target's turn instead of following it.

Each is drawn to its own layout: the IRIS-T's long strakes, the R-74M2's
nose vanes and canards, the PL-5EII's Sidewinder canards and rollerons, the
Meteor's two intake ducts and no wings, the R-77M's strakes and plain fins,
the SD-10A's four mid-body wings. Close to, each also shows the joints
between its sections, the windows of its proximity fuze, its hangers and its
nozzle, with jet vanes in the two that have them.

The Su-57 carries all six inside, so none is visible until it is launched. The
model has no bay doors; a missile leaves through the skin. Until 0.5.7 the
Typhoon and Su-57 each carried two of each kind under the wings. Until 0.5.9
the JF-17 carried two SD-10A; four is the type's full radar-missile load.

## B-52H Stratofortress

Added in 0.6.0 as the game's bomber. It has no gun and no missiles.

| | Published | Simulator |
|---|---|---|
| Engines | 8 × TF33-P-3/103, 75.6 kN each | two engine slots of 302.5 kN, one per wing |
| Empty mass | 83,250 kg | 83,250 kg |
| Internal fuel | 141,600 kg | capacity 141,600 kg; 45,000 kg at spawn |
| Wing area / span / length | 370 m² / 56.4 m / 48.5 m | the same; the model spans 55.66 m |
| Top speed | 1,047 km/h, Mach 0.86 at altitude | Mach 0.87 at 11 km, Mach 0.84 at sea level |
| Load limit | about +2 g | +2 / −0.5 g |

The real aircraft is limited well below Mach 0.84 at sea level by its
structure; the simulator has no such limit. Roll and pitch response, drag,
inertia and the thrust lapse are engineering estimates. Eight engines are
flown as two because the simulation and its network state carry two.

| Load | Count | Each | Notes |
|---|---|---|---|
| Mk 82 | 51 | 227 kg | 27 in the bay and 24 under the wings on the real aircraft |
| Mk 84 | 18 | 925 kg | |
| B83 | 1 | 1,100 kg | a 1.2 megaton weapon in reality |

Bombs fall ballistically with drag and wind; nothing steers them. Blast
radii and damage are gameplay values, not weapon effects data: an Mk 82
damages aircraft within 70 m and buildings within 50 m, an Mk 84 within 150 m
and 120 m, and the nuclear weapon destroys aircraft within about 5 km,
damages them out to 8 km and levels buildings within 6 km. The nuclear weapon
is given enough drag to take about a minute to fall from 9 km, which is what
lets the bomber get clear; the real one is slowed by a parachute.

## Boeing 747-206B

Added in 0.6.9.2000 as the game's airliner. It has no gun, no missiles and no
bombs. The figures are the commonly quoted ones and were not checked against a
primary source for this release.

| | Published | Simulator |
|---|---|---|
| Engines | 4 × JT9D-7A, 46,950 lbf (208.9 kN) each, as quoted | two engine slots of 417.7 kN, one per wing, two engines each |
| Empty mass | 174,000 kg (quoted operating empty weight, estimate) | 174,000 kg |
| Internal fuel | 146,700 kg (183,380 L at 0.8 kg/L) | capacity 146,700 kg; 60,000 kg at spawn |
| Mass at spawn | | 234,000 kg |
| Wing area / span / mean chord | 511 m² / 59.64 m / 8.3 m | the same; the donor's planform measures 413 m² |
| Length / height | 70.66 m / 19.33 m | the model measures 70.66 m and 19.33 m |
| Top speed | Mmo 0.92 | steady top 290 m/s at sea level, Mach 0.88 at 35,000 ft |
| Load limit | +2.5 / −1 g | +2.5 / −1 g |

The real aircraft is held to its structural limit of about 375 knots indicated
at low altitude, well below 290 m/s at sea level; the simulator has no such
limit. Drag, inertia, thrust lapse, control response and the wave-drag fit are
engineering estimates set against the commonly quoted performance below.

The engines lose thrust with speed as a bypass-ratio-5 fan does, about 79 % of
static at Mach 0.5 and 62 % at Mach 0.8, and the wave drag rises from Mach 0.84,
just short of the 747's drag-divergence Mach. Measured at the 234 t spawn mass
against commonly quoted figures for a 747-200 of about that weight:

| | Commonly quoted | Simulator |
|---|---|---|
| Cruise, Mach 0.85 at 35,000 ft | about 2° nose up, 9 to 11 t of fuel an hour | 1.4° nose up, 9.5 t an hour |
| Climb at full thrust | | 5,200 ft/min at 3 km, 2,100 ft/min at 9 km |
| Ceiling | 45,000 ft when light | about 13 km (42,600 ft) |
| Stall, clean and flaps 30 | about 145 and 110 knots | 148 and 115 knots |
| Roll from wings level, full wheel, 130 m/s | 10 to 15°/s | 31° in 3 s, 13°/s peak |
| Lift-off, flaps 30, rotated at 136 knots | about 145 knots | 146 knots after about 1 km |

Before 0.6.9.2000 shipped, the engines kept 83 % of their thrust at Mach 0.86
and the wave drag began at Mach 0.80, which cost 100 drag counts at cruise: the
747 burnt 11.4 t an hour at Mach 0.85 and reached Mach 0.86 at sea level.

The scenarios in `tests/b747_scenarios.cpp` measure:

- Static thrust of 835,487 N, the four engines at the quoted rating.
- Level trim at 3 km with throttle at 53 %, 62 % and 76 % for 120, 180 and
  230 m/s, with pitch of 5.7°, 0.8° and −0.8°.
- A takeoff held to 7° of pitch from 70 m/s, which lifts off at 90 m/s after
  1.49 km; pulled through to 10°, it lifts off at 75 m/s.
- A landing from 73 m/s at 0.9 m/s sink, peaking at 5.0 MN (about 2.2 g), with a
  420 m roll on the shared gameplay braking described below. A real 747 needs
  a longer roll than this; the simulator does not model reverse thrust.
- A stall recovery that loses 339 m of height.

The 747 flies the B-52's transport control law, which eases the nose down in
proportion to the load factor. With the B-52's 0.12 rad/s pitch rate a full
pull then held only about 1.25 g in a 58-degree bank, the bank the mouse-aim
instructor flies to the 747's 2.5 g limit, so every steep turn sank: the
instructor's 175-degree reversal at 6 km and 210 m/s lost 765 m. The 747 now
asks for 0.22 rad/s, and that reversal loses 7 m. Its ailerons and roll
spoilers together are given a roll power of 0.18 per radian, so a held roll key
banks it 11 degrees in the first 1.5 seconds; at 0.12 it managed 8.

Wheels up, the 747 comes down on the bottoms of its inboard nacelles, which
hang 0.8 m below the belly, as a real one does, and on its belly aft of them.

## Brakes and landing, all aircraft

From 0.6.0 every wheel is braked, with each aircraft's own friction figures
multiplied by 1.35 for braking and 1.5 sideways. The tyres keep their full
sideways grip under braking, and the brakes never fall below half their force
while cornering. While the wings still carry weight the brakes act as if 80%
of the aircraft's weight were on the wheels, standing in for lift dumpers.
Measured landing rolls fell from 904 m to 330 m for the Typhoon and from 745 m
to 398 m for the JF-17. The undercarriage takes a closing speed of 9 m/s
before damage, up from 6 m/s, and the fuselage 4.5 m/s, up from 3 m/s. These
are gameplay settings, chosen to make landing forgiving.
