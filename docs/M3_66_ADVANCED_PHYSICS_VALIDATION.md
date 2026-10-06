# M3.66 Advanced physics validation

Historical milestone report. The temporary Falcon has since been retired; current aircraft physics and evidence are documented in [AIRCRAFT_PHYSICS_AUDIT.md](AIRCRAFT_PHYSICS_AUDIT.md).
## Pre-change audit (recorded before physics edits)

The M3.65 repository uses renderer-independent NED/FRD doubles, semi-implicit
translation and normalized body-rate quaternion integration, two internal 240 Hz
substeps per authoritative 120 Hz tick. Euler's rigid-body equation includes the
angular momentum cross product and three principal inertias. There is no product
of inertia. Aerodynamics use one wind-frame whole-aircraft force and empirical
stability derivatives. Engine positions already generate asymmetric thrust moments;
Typhoon reheat is independent per engine. ISA has troposphere/isothermal layers to
20 km. Steady wind and deterministic gust hooks already exist. Tires use bounded
iterative impulses with static braking and speed-dependent nose steering.

Deficiencies: coefficients do not sample angular airflow at surfaces; actuator
commands teleport; mass never changes; control laws are direct stick deflections;
transonic CD grows quadratically without bound (CD increment >4 at Mach 2);
CAS is actually EAS; reported G is reset to 1 outside +/-20; separate visual controls
can disagree with physical deflections. Gear tires saturate each direction
independently instead of sharing a friction ellipse. Engine-out hooks and fuel are
absent from replicated state. A material-free glTF reaches a rejecting mesh builder;
screenshot readback ignores the provided BGRA/RGBA distinction.

Preserve: rigid-body frames and force integration, fixed clock, aircraft registry,
trim Newton solver, scenario helpers, renderer/asset architecture, authoritative
world, replay reconciliation, gun/combat system and Typhoon assets/reheat.

Order: baseline existing scenarios and CPU measurements; flight input bindings;
small component aero model with local airflow and bounded Mach tables; serializable
actuators/FCS; mass/CG/fuel/engine state; tire friction ellipse; diagnostics/telemetry;
physics and networking regressions; full builds/sanitizers and graphical smoke.
Reynolds correction is deferred: all three are large full-scale aircraft, without
reference polars sufficient to distinguish a useful correction from speculation.

Raw pre-change measurements: `output/m3_66/baseline.txt` and
`output/m3_66/baseline_cpu.txt` (Release, local host, warmed 120 Hz mixed fleet).

## Scope, evidence and reproducibility

This report records M3.66 on 2026-10-01, starting from commit `95ae1b1`.
The aircraft registry, native renderer/assets, authoritative world, combat,
prediction/reconciliation and fixed clock remain the existing systems. The model
is a small **hybrid component/derivative model**, not CFD or a manufacturer flight
model. Numerical consistency and scenario behavior are tested; real-aircraft
fidelity is bounded by the available public data.

Evidence classes used throughout:

- **A — public/reference-supported:** published geometry, nominal engine thrust,
  atmosphere constants and general aerodynamic equations. A nominal specification
  does not supply a complete engine deck, polar or maneuver envelope.
- **B — approximate/inferred:** coherent engineering approximations without an
  aircraft-specific measured dataset: surface locations, load sharing, inertia,
  stall curves, thrust lapse, friction, actuator rates and fuel distribution.
- **C — handling-tuned:** response rates, feedback gains and soft protection gains
  selected for controllable simulation behavior. These are not real control laws.

`output/m3_66/baseline.txt` and `baseline_cpu.txt` were captured before model edits.
`after.txt`, `advanced_metrics.txt` and `typhoon_controls.csv` contain the new
measurements. Missing baseline acceleration/supersonic probes were subsequently
replayed against an isolated archive of the original commit: see
`comparison_probe.cpp`, `baseline_replay.txt` and `after_replay.txt`. Those two
probes are retrospective comparisons, not measurements claimed to have preceded
implementation. The archive does not replace the working checkout.

## 1–4. Architecture and rigid-body changes

Before: one aggregate wind-axis force, empirical pitching/rolling/yawing moments,
constant loading and instantaneous surfaces. Existing nonlinear stall behavior,
engine offsets, independent Typhoon reheat, ISA, gusts and gear were useful
foundations. Defects included the unlimited common transonic drag polynomial,
wrong side-force basis sign, misleading indicated airspeed, discarded high G
readouts and incomplete state for future actuators/fuel.

After: six fixed aerodynamic contributors, explicit engines, serializable
control memory and loading, and shared mass/CG/inertia evaluation. Each physical
tick still comprises two 1/240 s substeps at server/client 120 Hz. Translation is
semi-implicit; quaternion integration is normalized. Frames remain right-handed
world NED and body FRD, all SI doubles.

The rotational equation remains `I*omega_dot = M - omega cross (I*omega)`.
A symmetric X/Z product is now supported:

```text
I = [ Ixx   0   Ixz ]
    [  0   Iyy   0  ]
    [ Ixz   0   Izz ]
```

Positive-definite basic inertia is checked; the X/Z block is inverted together.
Force offsets use the current CG. Gravity and acceleration use current total mass.
A ten-second free-body test with nonzero Ixz verifies world angular-momentum
error below 0.5%; quaternion/coordinate and existing integration regressions remain.
There is no velocity steering or global angular-rate clamp. Existing bounded
step-call handling and flat-ground emergency contact handling are retained.

## 5–7. Surface, lift and drag models

The six contributors are left/right wing, left/right pitch surface, fin and body.
Pitch surfaces are aft horizontal surfaces for A320/Falcon and forward canards
for Typhoon; Typhoon wing trailing control supplies additional pitch and roll.
Definitions use fixed arrays with no per-substep allocation. Engine arrays also
have fixed two-slot storage; Falcon activates one slot.

At each surface:

```text
r       = surface_reference_position - CG
Vlocal  = attitude.inverseRotate(Vworld - wind) + omega cross r
q       = 0.5 * rho * |Vlocal|^2
alpha   = atan2(Vlocal.z, Vlocal.x)
beta    = atan2(Vlocal.y, hypot(Vlocal.x, Vlocal.z))
Mlocal  = r cross Flocal
```

Angles are defined as zero at negligible flow; q then approaches zero. Lift and
drag use orthogonal axes even for transverse/reverse flow. The side-force axis
is `flow_direction cross lift_axis`, fixing the prior sign error. Local AoA,
sideslip, q and forces are inspectable per component.

Attached lift retains the calibrated finite-wing slope, flap/spoiler/elevator
increments and configurable clean/full-flap CL maxima. Positive stall uses a
smooth one-degree transition to falling lift; negative stall approaches bounded
negative lift. Between |AoA| 25° and 60°, a smooth blend transitions to
`sin(2*alpha)` flat-plate lift, including reverse flow. Separated drag approaches
`1.8*sin(alpha)^2` through a smooth ten-degree onset. These empirical curves are B,
not measured A320 or Typhoon polars.

The drag accounting exposes profile, induced, wave and device coefficients.
Profile is distributed 75% to wings and 25% to body. Wing induced drag is
`k*CL_section^2`, with `k = S/(pi*b^2*e)` and a ground-effect factor. Devices add
`0.028*flap + 0.016*gear + 0.028*spoiler`, plus an optional payload drag term.
Each surface has a drag modifier. The relationships follow ordinary finite-wing
physics; the aircraft-specific numbers remain approximations. See
[NASA induced drag](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/induced-drag-coefficient/).

Pitch force sharing translates the calibrated static/control Cm into forces at
surface locations, balancing the wing's reference lift moment. Thus the canard
forces really contribute, but they are not independently researched canard
polars. Residual whole-airframe derivatives remain deliberately present. Surface
positions/areas are a configurable foundation; this is not a general arbitrary
surface topology or panel solver. Tail profile drag is included in the aggregate
profile calibration rather than a separate tail polar.

## 8–9. Mach effects, stability and damping

Smooth bounded interpolation replaces the universal `2.5*(Mach-.72)^2` rise.
The tables below hold their end values outside the tabulated interval.

| Quantity | Tabulated Mach/value pairs |
|---|---|
| Lift multiplier | 0/1, .6/1, .85/1.08, 1/1.04, 1.4/.90, 2/.75, 3/.60 |
| Control multiplier | 0/1, .8/1, 1/.85, 2/.65, 3/.50 |
| Wave CD, A320 | .72/0, 1.05/.080, 1.4/.045, 3/.0315 |
| Wave CD, Falcon | .88/0, 1.05/.035, 1.4/.018, 3/.0126 |
| Wave CD, Typhoon | .90/0, 1.05/.035, 1.4/.018, 3/.0126 |

Control effectiveness is additionally divided by `sqrt(1+q/45000)` to represent
increasing aerodynamic load. These B tables avoid a Mach-one singularity and
separate transport/fighter drag behavior; they are not wind-tunnel datasets.

Local wing flow supplies substantial roll damping. Only the residual needed to
retain the configured nominal Cl_p is added, avoiding double-counting. Residual
Cm_q/Cn_r, sideslip-to-roll/yaw and small yaw-rate-to-roll / roll-rate-to-yaw terms
remain scaled by dynamic pressure and nondimensional `rate*length/(2V)`.
Static pitching stability uses bounded `sin(alpha)` instead of unlimited alpha.
Different definitions retain different longitudinal/directional stability.
There are no arbitrary world-velocity damping forces.

## 10–11. Surfaces, devices and ground effect

Pilot commands pass through configurable deadzone/exponent shaping and a 35 ms
exponential input lag, then FCS targets, normalized deflection bounds and finite
actuator rates. These run at fixed physics substeps. A320/Falcon/Typhoon main
actuators move at 2.5/5/6 normalized units per second; flaps at .25 and spoilers
at 1.7. Positive pull maps to negative elevator deflection. Trim remains a
persistent command offset, with a saved equilibrium reference.

Flaps increase CL0/CLmax, modify stall onset, drag and pitching moment. Spoilers/
airbrake reduce lift and add drag and a small pitching term. The common scalar
is deliberately inexpensive; aircraft-specific split spoiler/airbrake panels,
slats and automatic A320 ground deployment are not implemented. Component state
and modifier hooks allow later extension.

Ground effect continuously scales induced drag with
`1 - .28*exp(-max(0,wing_height)/(.18*span))`. It approaches unity at altitude
without the previous cutoff discontinuity. Wing height uses the nominal local
wing elevation and CG altitude, not a terrain mesh or separately tilted wing
height. Approach, flare, contact and continuous flight-cycle tests validate
behavior; no ground-effect lift boost is claimed.

## 12. Atmosphere and airspeed

The preserved ISA-like model uses 288.15 K / 101325 Pa at sea level, a 6.5 K/km
lapse to 11 km, then an isothermal layer to 20 km, with `rho=P/(R*T)` and
`sound=sqrt(1.4*R*T)`, R=287.05. Inputs are finite-checked; altitude is bounded
-500..20000 m. Temperature offset changes temperature/density/sound at the
standard pressure profile. These constants and layers follow the
[NASA-hosted Standard Atmosphere](https://ntrs.nasa.gov/api/citations/19770009539/downloads/19770009539.pdf).
Flat-Earth geometric altitude is used directly; geopotential conversion and upper
layers are outside this implementation.

TAS is air-relative speed, q uses that speed, and Mach is TAS/sound. IAS now
represents ideal calibrated pitot airspeed, equal to CAS because installation
and instrument error are absent. Subsonic total/static pressure uses
`(1+.2*M*M)^3.5`; supersonic flow uses the normal-shock Rayleigh pitot relation.
A bounded 40-iteration sea-level inversion derives CAS. The supersonic distinction
is supported by [NASA pitot-pressure definitions](https://www.grc.nasa.gov/www/winddocs/towne/plotc/plotc_p3d.html).
EAS is no longer mislabeled IAS; no separate EAS instrument is provided.

| Altitude m | Temperature K | Pressure Pa | Density kg/m³ | Sound m/s | TAS at M.78 | IAS/CAS m/s |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 288.15 | 101325.00 | 1.225012 | 340.2923 | 265.4280 | 265.4280 |
| 5000 | 255.65 | 54019.55 | .736118 | 320.5278 | 250.0117 | 200.1084 |
| 11000 | 216.65 | 22631.70 | .363916 | 295.0680 | 230.1531 | 132.6590 |
| 20000 | 216.65 | 5474.72 | .088033 | 295.0680 | 230.1531 | 66.1799 |

Gas-law/sound/pitot consistency and nonstandard-temperature tests pass. This is
not a complete weather or humidity model. A Reynolds correction was evaluated
and deferred: the three large aircraft lack trustworthy section polars to justify
another fitted correction. Future small aircraft should revisit that decision.

## 13–14. Mass, CG, inertia and fuel

Total mass is basic + current fuel + payload. Default reference loading:

| Aircraft | Basic kg | Fuel kg | Payload kg | Total kg |
|---|---:|---:|---:|---:|
| A320 | 42000 | 8000 | 14000 | 64000 |
| Falcon | 7200 | 2500 | 800 | 10500 |
| Typhoon | 11000 | 3000 | 0 | 14000 |

The reference body frame is centered on the original loaded CG. The basic
moment is inferred to balance reference fuel/payload moments. Current CG is
`[fuel_position*(fuel-fuel0) + payload_position*(payload-payload0) + payload*offset]/mass`.
Reference inertia receives point-mass loading deltas and a parallel-axis
correction to actual CG, including Ixz. This is an inexpensive B mass distribution,
not a tank mesh or cabin loading simulation. The loaded A320 test measures
68000 kg, CG .56471/.26471/.07941 m and principal terms
1364426/3646146/4708191 kg m²; trim/acceleration change with load.

State position is actual CG. Aero/engine/gear/gun offsets subtract CG. Rendering,
camera reference, wheel compression, exhaust/wingtip particles and combat sphere
attachments use the same subtraction so geometry and collision stay aligned.
No asset anchors or registry are replaced.

Each engine reports `fuel_flow = thrust * blended_TSFC`; flow is summed and fuel
burned each substep, bounded at zero. Dry/reheat TSFC differ; total mass/CG/inertia
then update. Empty fuel yields zero thrust. There is one fuel mass, no transfer,
imbalance, unusable fuel or multi-tank system. Inertia varies quasi-statically;
exhaust mass momentum flux beyond represented thrust is not modeled. Fixed-mass
comparison scenarios explicitly set `fuel_flow_scale=0`; production defaults
burn fuel and separate tests cover flow, exhaustion, loading and replay.

## 15–18. Engines, lapse, engine-out and reheat

Explicit engine components provide body position, normalized direction, dry/wet
thrust, spool time and TSFC. Legacy definition fields materialize components once
on simulator construction, preserving existing configuration code. Independent
spool, reheat and health values are physical state; absent Falcon slot contributes
no thrust or fuel.

Thrust lapse is
`max(.05, sigma^exponent * [1-.20*min(M,2.5)+ram_gain*M²/(1+M²)])`.
A320/Falcon/Typhoon density exponents are .85/.65/.50, ram gains 0/.18/.22.
The preserved dry nonlinear spool curve and Typhoon .85 reheat detent are used;
reheat adds the nominal wet-minus-dry thrust through the existing finite ramp.
Lapse, ram gain and spool curves are B. They are not compressor/inlet/thermodynamic
models or a manufacturer altitude/Mach engine deck.

Nominal Typhoon engines remain 60 kN dry / 90 kN reheated each at sea level.
Published EJ200 TSFC bands are 21–23 g/(kN s) dry and 47–49 reheated; configured
midpoints are 22e-6 and 48e-6 kg/(N s). Those nominal numbers are A from
[EUROJET technical data](https://www.eurojet.de/innovation/); applying them across
all flight conditions remains B. The new fixture records combined dry/wet flows
1.422039/4.653946 kg/s, ratio 3.273, and dry ten-second burn 14.172334 kg.

| Left engine only, right health=0 | Left thrust N | Yaw moment Nm | Yaw rate after .25 s rad/s |
|---|---:|---:|---:|
| A320 | 58776.84 | +311517.252 | +.01467 |
| Typhoon | 45499.43 | +28664.640 | +.03566 |

Opposite failures reverse yaw; existing independent-throttle/reheat regressions
remain. These are sign/response validations, not certified engine-out procedures
or minimum-control-speed calculations. Reheat visual nozzles/flames read actual
per-engine state; fuel consumption adds a new physical cost without replacing
M3.65 reheat behavior.

## 19–21. FBW approximations and Typhoon allocation

Direct control is selectable through definition and `State::fcs_enabled`.
Above 30 m/s / q>50 Pa, augmented aircraft use dynamic-pressure/inertia-scheduled
pitch/roll rate servos, equilibrium moment feedforward, a small normal-load
feedback and yaw damping. At lower speed they revert to direct actuator targets.
Load is body-up non-gravitational acceleration divided by g; gravity is excluded.
Ground/contact specific force is included in the sampled diagnostic G. The old
+/-20 G replacement is removed. Normal acceleration in m/s² is also exposed.

| Definition | Pitch rate command rad/s | Roll rad/s | Response s | Positive/negative G | AoA soft onset |
|---|---:|---:|---:|---:|---:|
| A320 transport | .22 | .58 | .55 | +2.5 / -1 | 15° |
| Falcon fighter | .65 | 3.2 | .30 | +9 / -3 | 25° |
| Typhoon canard | .75 | 3.5 | .25 | +9 / -3 | 28° |

These are C response parameters. The A320 approximation supports neutral-stick
load feedback and trim but does not reproduce Airbus normal/alternate/direct law,
bank hold, autotrim scheduling, VMO protection or certified envelope guarantees.
Protection reduces requested surface demand above configured AoA/G; it is soft,
not a hard structural cap. Maximum rate is a command limit, not an imposed
angular-rate clamp. Overshoot and disturbances remain possible.

Typhoon pitch authority is apportioned 45% to forward canards and 55% to trailing
wing control; symmetric wing positions are 1.2 m aft of the reference CG.
Allocator outputs are `canard=elevator`, `left=clamp(elevator+aileron)`,
`right=clamp(elevator-aileron)`. Actual allocated deflections drive component
forces and visual surfaces; clipping reduces combined pitch/roll authority.
Rudder supplies yaw. The split and gains are B/C, not Typhoon control-law data.
Open-loop Cm_alpha is reduced from -.48 to -.15 to represent weaker static
stability while retaining a stable numerical airframe; it is not made deliberately
unstable. FCS is needed for the intended rate-command handling, not for preventing
an invented unstable divergence. Closed/open .25-pitch-command peak rate over
2 s is .14693/.32609 rad/s. A five-second .65 pull at 220 m/s with reheat peaks
at 5.41724 G, a physical sanity check rather than a claimed sustained combat turn.

At .4 roll command over the same short fixture, rates at 110/250 m/s are:
A320 .10835/.07154, Falcon .55803/.38847, Typhoon .56911/.36585 rad/s.
Finite actuators, aerodynamic damping and loading produce the measured responses;
no real maximum roll-rate figure is asserted. Exact Typhoon unstable-airframe
behavior, vortex lift, departure and recovery laws require better public datasets.

## 22–24. Ground, wind and damage foundations

Existing per-gear spring/damper compression, eight-iteration tire impulses and
static parking are preserved. Longitudinal braking/rolling and lateral slip
impulses now share a normal-load-dependent friction ellipse, preventing full
braking and full cornering force simultaneously. Wheel-point velocity includes
rotation. Steering rotates nose-wheel tire axes and falls with speed; it does
not apply an arbitrary yaw torque. Friction coefficients remain configurable
runway-condition hooks. There is no separate wheel angular-speed/slip-ratio
state, tire thermal model, brush distribution or new anti-skid algorithm.
This is a bounded impulse tire approximation, not Pacejka.

A global finite wind/temperature/turbulence setting is owned by World and applied
to all simulators. Air-relative flow feeds instruments, surfaces and engines;
airborne spawns add steady wind to their ground velocity. Existing deterministic
space/time sinusoidal gust hooks remain. Weather is now included in welcome and
snapshot messages and prediction initializes with it before leading ticks.
Headwind/tailwind/crosswind and a Galilean air-mass test pass; a 240-tick world /
encoded snapshot / prediction test with wind, gusts and temperature has zero
measured position error. No full stochastic/weather system is added.

Independent engine output modifiers [0,1], surface effectiveness [0,1] and drag
multipliers [0,10] are serializable. A zero surface modifier removes its force;
a zero engine modifier removes output. A Typhoon left-wing effectiveness .3
reduces lift from 136731.94 to 102051.19 N and generates -83889.93 Nm roll moment.
These hooks are separate from the preserved gun/health system; guns do not yet
choose component damage. Stuck actuators/structural breakup are future extensions.

## 25. A320 before/after validation

Reference mass is 64000 kg, calm ISA, flat dry runway, two nominal 120 kN
engines. Equilibrium/comparison scenarios freeze fuel; the production fuel tests
are separate. Takeoff uses .35 flap, full throttle, scripted rotation near
78 m/s and gear retraction. Landing starts at 78 m/s with full flap/gear;
rollout measures touchdown to stop, not certified field length.

| Measurement | M3.65 | M3.66 |
|---|---:|---:|
| Trim, 1000 m / 110 m/s: AoA deg | 6.646996 | 6.654545 |
| Trim elevator normalized | .042966 | .047773 |
| Trim throttle | .442523 | .442525 |
| Trim lift / drag N | 623471.62 / 35645.70 | 623466.89 / 35645.43 |
| +1° open-loop perturbation, altitude range m | 997.359–1002.604 | 996.514–1003.492 |
| Takeoff liftoff TAS m/s | 88.3320 | 85.6001 |
| Takeoff release time s / distance m | 29.1083 / 1244.75 | 28.1417 / 1160.46 |
| Climb at 120 s, altitude m / TAS m/s | 1799.76 / 190.696 | 1805.73 / 190.286 |
| Clean CL at 14° / 20° / 90° | 1.41023 / 1.12242 / 0 | 1.41023 / 1.12242 / 0 |
| Stall recovery final AoA deg / altitude loss m* | 5.78397 / 340.18 | 6.14987 / 314.42 |
| Cruise 5000 m / 160 m/s throttle | .558690 | .558690 |
| Cruise 7000 m / 200 m/s throttle | .649430 | .649426 |
| Ideal IAS at 7000 m / TAS160 m/s | 110.9927 (EAS approximation) | 113.1016 (pitot CAS) |
| Full-power level test, 360 s terminal TAS m/s | 268.4579 | 271.9843 |
| Touchdown speed m/s / sink m/s | 75.1991 / .80727 | 75.6969 / .71895 |
| Rollout m / stopping time s | 881.494 / 22.225 | 824.636 / 21.092 |
| Continuous 420 s cycle rollout m | 886.368 | 798.434 |

*The initial stall push was changed to full rate command; see test changes below.
It is a recovery regression, not a claim of improved recovery for identical
pilot commands. Lift curves remain unchanged in this low-Mach nominal fixture;
control, local force application and tire behavior account for changed dynamics.
Steady trim remains effectively exact over 120 s with frozen fuel. The open-loop
perturbation remains bounded within the old accepted excursion limits.

The reference CL maxima imply sea-level 1 G clean/full-flap stall estimates
76.72/60.95 m/s from `sqrt(2*weight/(rho*S*CLmax))`. These are configuration
estimates, not flight-test stall speeds or Airbus V-speeds. Flap lift/drag/moment
and finite transit are tested, including configured approach, rotation and flare.
New high-altitude trim at 11000 m / Mach .78 yields TAS 230.153 m/s,
IAS 132.659 m/s and throttle .82952. This is below the published A320 MMO .82,
but MMO alone does not validate our required cruise power or drag.

## 26. Falcon before/after validation

The generic single-engine fighter remains a test aircraft, not a 1:1 F-16.
Reference loading is 10500 kg, nominal 79 kN dry only.

| Measurement | M3.65 | M3.66 |
|---|---:|---:|
| 180 m/s cruise throttle / pitch deg | .472332 / 2.02214 | .453782 / 2.02519 |
| 250 m/s cruise throttle | .641499 | .587577 |
| Takeoff TAS m/s / release time s | 98.4706 / 15.8833 | 99.0278 / 15.8833 |
| Takeoff distance m | 760.073 | 762.081 |
| Climb endpoint altitude m / TAS m/s | 2310.14 / 262.278 | 2472.54 / 290.166 |
| 20 s turn heading change deg / load G | 32.5593 / 1.16145 | 28.2765 / 1.13632 |
| Recovery final AoA deg / height loss m | 2.24161 / 162.97 | 2.27120 / 87.08 |
| Touchdown speed m/s / sink m/s | 79.8201 / 1.18307 | 79.1770 / .82899 |
| Landing rollout m | 1149.296 | 1094.641 |

The higher-speed changes mainly reflect the new fighter thrust lapse/Mach
approximation; response/turn differences also reflect rate-command control and
finite actuators. At an initial Mach 1.5 / 11 km, full dry power remains finite and
decelerates to Mach 1.46347 over ten seconds. **Steady Mach 1.5 is not claimed**
for this dry-only definition. Adding afterburner solely to meet that target would
misrepresent the preserved aircraft configuration.

## 27. Typhoon before/after validation

Reference loading is 14000 kg: 11000 basic + 3000 fuel, no payload; two 60/90 kN
engines. Comparisons freeze fuel to isolate the model. Taxi, reheat transient,
turn, low-speed/high-AoA, engine-out, allocation and recovery tests complement
the existing asset/registry/multiplayer tests.

| Measurement | M3.65 | M3.66 |
|---|---:|---:|
| Total static dry / reheat thrust kN | 120.001 / 180.002 | 120.001 / 180.001 |
| Cruise180 m/s throttle / pitch deg | .464812 / 1.89015 | .428415 / 1.38321 |
| Cruise250 m/s throttle | .629256 | .562980 |
| Taxi speed m/s / right heading deg | 7.7266 / 35.6092 | 7.7257 / 35.5932 |
| Liftoff TAS m/s | 95.9434 | 101.2689 |
| Release time s / takeoff distance m | 9.3417 / 414.261 | 9.8250 / 462.970 |
| Climb endpoint altitude m / TAS m/s | 3197.23 / 271.575 | 4023.46 / 323.446 |
| .2 roll input, .5 s p rad/s | .949836 | .226781 |
| .15 pitch input, .5 s q rad/s | .180817 | .087483 |
| .2 rudder input, .5 s r rad/s | .220623 | .161174 |
| 20 s turn heading change deg / G | 32.5558 / 1.16261 | 26.6431 / 1.13147 |
| High-AoA recovery final AoA deg / altitude loss m | .70530 / -14.36 | -.12737 / -149.59 |
| Touchdown speed m/s / sink m/s | 78.7441 / 1.28454 | 77.7030 / 1.16037 |
| Landing rollout m | 1092.571 | 1074.568 |
| Replayed full-reheat acceleration, from TAS180 over10 s: TAS m/s | 245.789 | 261.109 |
| Replayed same acceleration: altitude m | 1111.638 | 1015.445 |
| Replayed M1.5, 11 km, zero-AoA, gear-down CD | 1.561203 | .058100 |
| Replayed M1.5, 11 km level trim | fails at full power | converges, throttle .954941 |

The acceleration probe has no external pitch-hold pilot; altitude/attitude can
change and results are not constant-altitude acceleration curves. The existing
climb endpoint is likewise not a level-flight top-speed measurement. Gear-down
zero-AoA CD is a static identical-input diagnostic; clean trimmed Mach 1.5 CD is
.04222. The advanced clean supersonic case maintains Mach 1.5 at 11 km for ten
seconds, validating bounded drag and physical force/moment balance. It does not
establish maximum Mach or real dry supercruise performance.

Slower initial command response replaces instantaneous deflection and arbitrary
surface authority with finite servos and allocation. Takeoff rotation occurs
later, increasing liftoff speed/distance. Fighter lapse/wave drag produce greater
high-speed acceleration/climb. Canard load sharing and reduced static stability
change trim and recovery; negative altitude loss indicates climb during powered
recovery, not energy creation. The unpowered energy checks separately reject
increasing mechanical energy.

## 28–29. Public reference data and uncertainty

| Source / public fact | Use and evidence class |
|---|---|
| [EUROJET EJ200](https://www.eurojet.de/innovation/): 60/90 kN, TSFC21–23 /47–49 g/(kN s) | A nominal thrust/TSFC; B flight-condition extrapolation |
| [German Air Force Eurofighter data](https://www.bundeswehr.de/en/organization/german-air-force/eurofighter): 11 t empty, 10.95 m span, 50 m² area, +9/-3 structural load | A geometry/basic mass and reference limits; B/C modeled handling/protection |
| Same source: takeoff <700 m, landing <600 m, supersonic capability | Broad context only; unspecified loading, touchdown/braking and distance definitions prevent exact calibration |
| [EASA A.064 A320 TCDS](https://www.easa.europa.eu/en/downloads/16507/en), A320 airspeed limitations: MMO .82 | A envelope context for tested Mach .78; no claim of certified flight-law/operational performance |
| [NASA Standard Atmosphere](https://ntrs.nasa.gov/api/citations/19770009539/downloads/19770009539.pdf) | A constants/ideal layer equations; B flat-Earth implementation and temperature-offset simplification |
| [NASA induced drag](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/induced-drag-coefficient/) | A equation family; B chosen efficiency and component load sharing |
| [NASA pitot-pressure definitions](https://www.grc.nasa.gov/www/winddocs/towne/plotc/plotc_p3d.html) | A ideal compressible/shock relation; B ideal instrument without installation error |

The measured Typhoon takeoff ground run is consistent with a broad published
<700 m statement; it does not validate a particular aircraft operating manual.
Our >1 km fighter braking rollouts do **not** match the published <600 m landing
statement. This remains a known difference, with unknown reference conditions and
simple current tire/brake settings; it is not concealed by retuning a coefficient
to force agreement. A320 operational V-speeds/distances likewise depend on weight,
engine subtype, slats/flaps, atmospheric conditions and runway assumptions.

All inertia tensors, aero derivatives/polars, component force splits, FCS gains,
actuator timing, thrust lapse/ram curves, A320/Falcon TSFC, contact coefficients,
fuel locations and loading assumptions are B/C unless a specific A source is
listed. No forum performance figures, classified data or commercial-simulator
flight models are used. No DCS-level fidelity or exact Typhoon law is claimed.

## 30. Robustness, energy and diagnostics

The added envelope covers 180 inverted/extreme flow/rate cases of two simulated
seconds, supplementing existing 56 A320 and 42 each Falcon/Typhoon cases. It
includes zero/tiny speed, vertical/reverse/transverse flow, negative/high AoA,
large sideslip, high rates, near ground and high altitude. Finite state/forces,
unit quaternion and bounded aerodynamic results are checked; finite input/config
validation rejects invalid state and sanitizes weather. This demonstrates a broad
finite envelope over tested durations, not guaranteed arbitrary-condition flight.

Unpowered two-second mechanical-energy loss is 13.800 MJ A320, 2.594 MJ Falcon,
6.991 MJ Typhoon in the selected high-speed glide fixtures. Drag opposes local
flow; control-component force/moment changes are energy-dissipating in those
cases. Existing climb kinetic/potential exchange and full-power terminal-speed
checks remain. This is a gross energy sanity check, not a complete proof across
all controls, changing mass and ground impacts.

Developer diagnostics expose atmosphere/q/Mach, lift/drag/weight, individual
engine thrust/reheat/flow, fuel/mass/CG, actual controls, wind, ground effect and
per-surface forces/angles/moments. Normal load and acceleration are inspectable
through Instruments/DebugFrame. The normal HUD is kept compact. Optional ostream
CSV provides time, speed/altitude/Mach/angles/rates/G, throttle/actuators, force,
fuel/mass/CG and position. The telemetry case produces 100 samples at 10 Hz over
10 simulated seconds; it has no default runtime logging/allocation overhead.

## 31. Simulation CPU performance

Measured on Linux x86-64, GCC Release, Intel Core Ultra 7 255H, mixed registry
A320/Falcon/Typhoon, single calling thread. Benchmark advances 3000 ticks and
excludes the first 100 from statistics. Initial trim solving, networking, assets,
renderer, logging and allocations outside stepping are excluded. Aircraft step
at 120 Hz with their internal two substeps; these are **whole physics-tick** times.
CPU frequency/scheduling noise remains, particularly in the single-aircraft row.

The baseline is saved in `baseline_cpu.txt`; final values are in `after_cpu.txt`.
The final table was captured after all concurrent acceptance runs finished.
Single-aircraft tails are sensitive to CPU migration/frequency and very short
benchmark duration; the warmup is retained identically to the baseline.

| Aircraft | Before mean / p95 / p99 µs | After mean / p95 / p99 µs | After per aircraft µs | After mean / 120 Hz budget |
|---:|---:|---:|---:|---:|
| 1 | 1.741 / 1.920 / 1.932 | 7.142 / 15.168 / 17.166 | 7.142 | 0.09% |
| 8 | 5.078 / 8.835 / 10.205 | 25.314 / 25.347 / 29.770 | 3.164 | 0.30% |
| 16 | 6.521 / 6.523 / 6.568 | 51.662 / 55.125 / 58.449 | 3.229 | 0.62% |
| 32 | 13.046 / 13.007 / 17.004 | 102.854 / 107.023 / 111.490 | 3.214 | 1.23% |
| 64 | 26.249 / 26.747 / 33.487 | 208.671 / 215.620 / 223.583 | 3.260 | 2.50% |

The 120 Hz budget is 8333.33 µs. This is a local synthetic feasibility measurement,
not a prediction of end-to-end 64-player server/network/render cost. The model is
more expensive than M3.65, as expected from component evaluation and FCS feedback.
Fixed arrays and no substep allocations keep the measured scale practical.

## 32. Multiplayer, serialization, compatibility and cleanups

Protocol **v5** replicates all new fuel/loading/modifier, pilot shaping, trim
reference, actuator, FCS and allocation state, plus world weather. Welcome carries
weather before prediction leading steps are simulated; snapshots refresh it before
reconciliation. Actual controls interpolate only for display. Full snapshots and
120 Hz authority remain; no M3.7 quantization, compression, interest management
or bandwidth redesign is included. Client and server must be rebuilt together.

An aircraft record is now 481 bytes (old 231 + 250 physics memory). Snapshot size
is `66+481*N`, including 40 bytes weather: 547 bytes at one aircraft, 1028 at two,
30850 at 64. Maximum message rises from16384 to65536 bytes to accommodate64 complete
states; GNS already provides fragmentation. At 24 Hz, 64-aircraft full snapshots
cost 740400 bytes/s per client before transport overhead, about47.39 MB/s full
fanout. This deliberate growth preserves complete replay state; bandwidth work
belongs to a later milestone.

Offline trim now uses loaded weight and materialized explicit engine components,
including reheat. Regression cases cover explicit A320 basic mass 47000 kg
(total 69000 kg) holding equilibrium for 30 seconds and a Typhoon component-engine
Mach 1.5 trim even when legacy scalar reheat is unset. A finite-difference probe
steps inward at upper actuator/throttle bounds, avoiding a false singular
Jacobian caused by control sanitation. Trim remains separate from runtime;
priming actual actuators is an offline equilibrium operation.

The supplemental C API `OfsPhysicsMemory` getter/setter pairs with legacy
`OfsState` for a complete snapshot while preserving the original struct ABI.
An exact same-platform memory restore/replay test passes. Cross-compiler/platform
bitwise determinism is neither required nor asserted. Dedicated servers do not
depend on graphics. Selection, original Typhoon assets/reheat and gun combat
remain through existing regression suites.

Valid glTF primitives without materials now receive an implicit rough white
material. Its identity uses an index, so an authored material with the same
internal-looking name cannot hijack it. Material-free/authored-material tests
exercise the actual mesh builder. Screenshot readback selects R/B channel indices
from RGBA8 versus BGRA8 format, retaining pitch/Y-flip handling.

`network.bad` uses a common absolute server tick interval of 96 ticks instead of a
wall-time input window. Response proof requires correct motion from each aircraft
independently: packet delay can make those moments occur on different ticks.
The prior simultaneous-sample condition was observed failing despite successful
remote motion. Snapshot starvation, packet impairment, replication, injected 5 m
reconciliation, error/queue/history limits, remote continuity, cleanup and tick-drift
assertions remain. GNS's packet-loss randomness and real-time scheduler
are still not deterministically seeded; only the pilot script is tick-driven.

## 33. Build/test acceptance and intentional expectation changes

The final acceptance logs are `output/m3_66/tests-*-acceptance.txt`; earlier
iteration logs are not acceptance evidence. Desktop smoke runs sequentially to
preserve focus and covers S flight input, camera navigation, UI, reset, resizing,
fullscreen and finite flight state. The input unit test covers both modifiers,
opposing keys, throttle time scaling, capture/release and physical pitch/roll/yaw
signs. Tab camera changes bypass ImGui navigation when keyboard is not captured,
fixing an observed keyboard-focus conflict.

| Configuration | Complete matrix | Final offline-trim review | Evidence |
|---|---|---|---|
| Debug native | 84/84 + 1/1 graphical smoke | 59/59 affected cases | `tests-debug-acceptance.txt`, `post-review-debug.txt`, `smoke-debug.txt` |
| Release native | 84/84 + 1/1 graphical smoke | 59/59 affected cases | `tests-release-acceptance.txt`, `post-review-release.txt`, `smoke-release.txt` |
| Headless Debug | 80/81; combat.bad passed 1/1 unchanged retry | 57/57 affected cases | `tests-headless-acceptance.txt`, `recheck-combat-headless.txt`, `post-review-headless.txt` |
| ASan + UBSan headless | 81/81, no reported sanitizer error/leak | 57/57 affected cases | `tests-sanitize-acceptance.txt`, `post-review-sanitize.txt` |

The full matrix preceded the final offline trim compatibility correction; all
physics, aircraft scenarios, explicit-loading/engine cases, protocol, world,
interpolation and prediction cases affected by that correction were rerun in
all four configurations. Desktop smoke was also repeated after the correction.
The standard A320, Typhoon cruise and supersonic measurements were rechecked
and unchanged.

The headless full-run combat.bad failure was `repeat combat loop`:198 shots,
16 hits,4 kills,3 respawns in20 wall seconds. Four-second respawn timing and
unseeded packet delay put a lifecycle completion outside the fixture cutoff;
the identical test passed on isolated retry, and all other configurations and
combat soaks passed. No combat assertion, impairment setting or respawn rule was
changed. This remains a real-time test flakiness limitation, not a claim that
that full headless run passed81/81. All81 cases have passing coverage across
that run and its retry. The revised network.bad additionally passed five
consecutive12-second runs (`repeat-network-bad.txt`).


One additional Debug desktop rerun encountered SDL's "No relative mode
implementation available" during fullscreen/minimize restore; the final isolated
rerun passed. This is an observed desktop focus/backend limitation. No mouse
assertion or SDL check was bypassed.

Each complete suite retains real three-minute flight/network and combat soaks,
protocol fuzz/security, predictions/reconciliation, selection, assets, visuals and
all original aircraft regressions, plus 15 advanced cases, material-free glTF,
wind prediction and native input. ASan/UBSan run together with leak detection,
halt-on-error and the existing 16 MiB quarantine; no sanitizer check is suppressed.
Linux desktop/OpenGL is tested. Windows/MSVC and cross-platform matching remain
unverified on this host.

Intentional scenario changes are explicit:

- Fixed-mass trim/parking/comparison cases disable burn. Otherwise a 120-second
  exact equilibrium assertion would measure deliberately changing loading.
  Dedicated burn/exhaustion/CG/replay cases use production behavior.
- External scripted pilots invert the new rate-servo mapping to request the same
  intended pitch/flight path. The runtime demo pilot is updated consistently.
  Approach feedforward uses the current configuration's offline trim solution.
- A320's +1° stability test disables augmentation to keep testing the airframe
  directly. Existing excursion bounds remain.
- A320 initial stall recovery changes -.3 direct surface command to -1 rate
  command. The two-second AoA threshold changes from 5° to 12° to represent finite
  .22 rad/s transport pitch demand and actuator travel. The final<8° recovery,
  no-warning, positive-altitude conditions remain. Both raw responses are reported;
  the criteria are B model expectations, not public recovery timings.
- Pressure-scaling checks use `4*sqrt((1+q60/qLimit)/(1+q120/qLimit))` instead of
  exact4 for a doubled-speed control moment, with .025 tolerance for lift/drag
  coupling. This follows the documented load-softening equation.
- Immediate prediction response checks positive roll acceleration and positive
  actual aileron rather than an instantaneous >.001 rad/s threshold on tick1.
  Exact replay/convergence checks remain.
- The aggregate side-force label becomes the fin component; force sums and frame
  orthogonality checks remain. Wire malformed-count offsets follow v5 weather.
- The network pulse/observation timing changes above remove an accidental
  simultaneous-packet-delivery requirement without reducing impairment settings
  or the physical/replication/error assertions.

## 34–35. Remaining weaknesses and recommended next milestone

Remaining physics weaknesses include hybrid force sharing rather than independent
measured section polars; simplified delta vortex/stall/departure behavior; only
one symmetric product of inertia; quasi-static point-mass fuel/payload inertia;
no fuel tank transfer/imbalance; approximate engine lapse/TSFC; soft FBW limits
without certified aircraft laws; scalar flap/spoiler devices; instant physical
gear command alongside existing visual transit; impulse tires without wheel spin,
slip-ratio/thermal/anti-skid dynamics; flat terrain and nominal ground effect;
20 km atmosphere bound and ideal pitot instruments. Longer-term envelope and
operational performance fidelity need trustworthy polars/engine decks and broader
reference-condition sweeps, not isolated coefficient tuning.

M3.66 stops here. The recommended scheduled next milestone is M3.7, addressing
measured multiplayer capacity and bandwidth now that complete physical replay
state is explicit, with its own acceptance criteria. Future physics calibration
can use the new CSV and component tests. **No M3.7 implementation, missile/radar,
world streaming, structural breakup or detailed systems work is included.**

## Inspected native captures

The Release Typhoon reheat fixture below freezes physics for presentation; it
confirms preserved aircraft/nozzle/flame assets, not flight performance. Its
capture shows the Typhoon with dual reheat.

The separate takeoff demo advances real 120 Hz physics with production fuel burn
and the scripted pilot. At 20 simulated seconds it reports TAS 166.841 m/s,
altitude 130.974 m, x 1416.539 m and both reheat states 1.0. The diagnostics show
changing fuel/mass and force balance; the demo is not an additional automatic
flight-performance assertion. Its capture shows the Typhoon at takeoff with the
new physics diagnostics. A third capture is the Release graphical smoke frame
carrying the same diagnostics.

Capture images are generated artifacts and are not version controlled; regenerate
them with `scripts/capture_m3_6.py`.

## Evidence files

Validation transcripts and reports are generated artifacts and are not version
controlled. They are written to `output/m3_66/` by the test and benchmark runs
described above, and are regenerated locally on demand. The evidence set covers:

- Pre-change aircraft measurements (`baseline.txt`) and post-change
  measurements (`after.txt`).
- Advanced model measurements (`advanced_metrics.txt`) and Typhoon control
  telemetry (`typhoon_controls.csv`).
- Baseline and final CPU timing (`baseline_cpu.txt`, `after_cpu.txt`).
- The full Debug, Release, headless and sanitizer acceptance matrices
  (`tests-*-acceptance.txt`).
- The headless combat retry (`recheck-combat-headless.txt`) and the five
  packet-loss repetitions (`repeat-network-bad.txt`).
- The final Debug and Release smoke runs (`smoke-debug.txt`,
  `smoke-release.txt`).
- The final affected Debug, Release, headless and sanitizer cases
  (`post-review-*.txt`).
