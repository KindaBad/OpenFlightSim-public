# M1 — Flight model calibration and stable full flight cycle

Measured on 2026-09-29, Fedora 44 / GCC 16.2.1 / CMake 4.1.0 / Ninja 1.13.2.
M0 architecture, assets, C API entry points, fixed 120 Hz ticks and 240 Hz
internal substeps are retained. No multiplayer or M2 implementation is present.
These are acceptance measurements of this approximate aircraft, not A320 flight
manual values or certification of real-world accuracy.

## Audit and implementation plan

Read the complete aircraft/configuration, Simulator, atmosphere, math, controls,
instruments, C API, input/reset paths, existing tests, coordinate documentation,
M0 audit and validation before changing the model. Initial repository remains
unborn with untracked files, as documented in M0; no reset, staging, commit or
asset regeneration was performed. Baseline source copies and measurements are
retained locally under ignored `.cache/m1`.

| Item | Existing implementation and audit conclusion |
|---|---|
| Mass / inertia | 64,000 kg; diagonal Ixx/Iyy/Izz = 1.35/3.55/4.60 million kg m². Constant mass, no fuel or inertia products. Retained. |
| CG | Body origin; gear/engine positions relative to it. No separate CG travel or visual mesh alignment. Aero moment coefficients describe moments about CG. |
| Geometry | S = 122.6 m², b = 35.8 m, MAC = 4.19 m; retained. Values are approximate source defaults without an authoritative aerodynamic dataset. |
| Lift | 5.05/rad slope, zero lift −2°, CLmax clean/full flap 1.42/2.25; flap +1.05, spoiler −0.55, elevator +0.32/rad. Normal range retained. |
| Drag | CD0 = .022; induced CL²/(π AR .82), flap/gear/spoiler increments .028/.016/.028; ground effect and wave drag above Mach .72. Retained before stall. |
| Pitch | Cm = .06 − .85 alpha − 1.45 elevator − 12 qhat − .10 flap + .02 spoiler, plus stall nose-down break. Retained. |
| Lateral | Cy_beta = −.55; Cl_beta/p/aileron = −.14/−.50/.16; Cn_beta/r/rudder = .13/−.16/−.085. M0 signs were already correct. |
| Thrust | Two 120 kN sea-level static engines, 1.2 s spool lag, 5% idle floor plus .95 N1^2.5; density^.85 and (1 − .20 Mach) lapse. Each at y = ±5.3 m, z = +1.1 m. Retained. |
| Atmosphere | ISA lapse to 11 km, isothermal to 20 km; altitude clamped −500..20,000 m. Deterministic gust functions; calibration uses calm standard atmosphere. |
| Gear | Nose (9.5,0,3.55), mains (−2.2,±3.8,3.55) m; stroke .45 m; k = 1.35 MN/m, c = 90 kN s/m per gear. Retained. |
| Tires | Rolling/brake/lateral coefficients .025/.55/.60; speed-dependent 70°..6° nosewheel limit. Retained, force calculation corrected. |
| Integration | Semi-implicit velocity/position and angular-rate/quaternion integration; rigid-body gyroscopic term and normalized quaternion. No flight-state locks. |
| Coordinates | World NED; body FRD; +pitch nose up, +roll right wing down, +yaw east/right. Verified M0 convention and control fixes. |

The measured plan was: (1) extract shared thrust evaluation and solve force/
moment balance, (2) preserve coefficients if trim succeeds, (3) correct stalled
flow and contact defects with focused regressions, (4) add headless scripted
flight-cycle tests and diagnostics, (5) run all build/runtime paths.

Baseline measurements showed **normal-flight coefficients already admit a stable
trim**. The untrimmed client reset and absence of measured scenarios had hidden
that capability. No broad retuning of lift slope, stability derivatives, inertia,
thrust or suspension was justified.

## Defects and evidence for changes

| Defect / measurement | Implemented change and reason | Regression |
|---|---|---|
| CD fell from .095848 at 14° to .048948 at 25°, .042887 at 90°; CL stayed .75 even at 180°. Broadside flow received almost clean-airframe drag. | Add separated-flow CD = 1.8 sin²(alpha), smoothly introduced over 10° beyond stall; blend CL toward sin(2 alpha) between absolute AoA 25° and 60°. 1.8 is an engineering flat-plate approximation, not measured A320 data. Existing onset and normal coefficients retained. | `flight.stall`: bounded/continuous coefficient sweep −180°..180°, rising post-stall drag, dissipation, broadside limits, actual recovery. |
| Negative stall blended from the linear curve toward roughly −1 immediately below −8°, increasing lift magnitude rather than beginning from the onset value. | Start the existing exponential approximation at the actual negative-stall onset CL, approaching −.6 continuously. | Clean and full-flap continuity sweep in `flight.stall`. |
| Independently projected lift/side axes were not orthogonal at simultaneous alpha/beta; vertical-flow fallback could point lift along airflow. | Use orthonormal wind axes: drag along flow, lift (sin alpha,0,−cos alpha), side = lift_axis cross flow_axis. | `flight.diagnostics` combined-alpha/beta orthogonality; extreme-angle dissipation. |
| Runway normal stored as body −Z rotated with pitch/roll. Friction used body velocities, including vertical motion. | World-vertical normals and horizontal projected tire directions. Normal damping sign preserved. | `flight.diagnostics` tilted contact has zero world horizontal normal; taxi/takeoff/landing/cycle. |
| Braked idle creep .033581 m/s after 60 s because tanh friction is zero at zero speed. | Bounded Coulomb tire impulses with effective point mass, predicted external-force velocity and 8 fixed sweeps. Braked longitudinal and lateral tires can hold statically; rolling tanh retained while brakes are applied. No velocity/position clamp added. | `flight.taxi` parked stability, powered taxi, both steering directions, brake distance and held stop; landing/cycle. |
| High-altitude warning compared IAS against density-corrected stall TAS. At 7 km, TAS 160, IAS 110.993, stall TAS 110.595, the old comparison warned falsely. | Compare TAS to stall TAS; retain AoA warning. | `flight.cruise` explicitly exercises this mismatch. |
| Airborne reset at pitch 3°, throttle .65 was untrimmed; input sampling overwrote any equilibrium elevator stick. | Solved reset and persistent normalized elevator trim offset, added to pilot stick before saturation. No force/rate controller in runtime. | `flight.trim`, `flight.controls`, strengthened graphical smoke after UI reset. |
| Debug lift/drag reconstructed using post-integration airflow, missing side/belly and misleading lift application position. | Store exactly the aero vectors and thrust used in integration, side/belly entries, total contact normal and sample time. Aero application point shown at CG. | `flight.diagnostics` force entries sum to integrated total. |
| Nonfinite external state/weather and invalid mass/inertia/reference geometry could poison calculations. | Reject nonfinite State, normalize attitude/clamp spool; sanitize Weather; reject invalid/nonfinite config values at construction. Preserve invalid-control/dt handling. | `flight.robustness` and existing M0 tests. |

Engine constants and spool behavior were **not changed**. `evalThrust()` is now
shared by integration, trim and diagnostics, avoiding duplicated formulas.
Suspension stiffness, damping and stroke were **not changed**. The old contact
lower-force condition for high positive compression speed was unreachable and
removed with the contact rewrite. The existing emergency belly spring/clamp is
retained, but normal scenario CG altitude stays above 3 m and never reaches it.

## Trim utility and standard condition

`ofs::solveTrim(config, TrimRequest)` is a small, offline Newton solver with
finite-difference 3×3 Jacobian, pivoted elimination, bounds and backtracking.
Unknowns are alpha, normalized elevator offset and symmetric throttle. Residuals
are world forward/vertical force and body pitching moment, scaled by weight and
MAC. It supports calm wings-level steady climb/descent and existing flap/gear
settings. It initializes spool to equilibrium; subsequent engine evolution is
ordinary runtime physics. Failed/unflyable requests return `converged=false`;
contacting gear is not accepted as an airborne trim. This is not a general
optimizer, banked/windy trim solver, runtime autopilot or physics constraint.

At **64,000 kg / 1,000 m MSL / TAS 110 m/s**, clean, gear up, wings level,
heading north, calm ISA:

| Metric | Measured value |
|---|---:|
| Pitch = AoA | 6.646995671° |
| Pilot elevator stick | 0 |
| Elevator trim / effective normalized elevator | +.042966320 |
| Elevator surface deflection | −1.074157992° (trailing edge up) |
| Throttle = equilibrium normalized spool, each | .442522978 |
| Lift | 623,471.619938 N |
| Drag | 35,645.699787 N |
| Total thrust | 35,886.926082 N |
| Weight | 627,625.600000 N |
| Aero pitching moment / thrust pitching moment | −39,475.618690 / +39,475.618690 Nm |
| Net force norm | 7.89e−9 N |
| Net pitching moment | 8.69e−9 Nm |
| Initial vertical speed / angular rates | 0 / 0 |

Lift is slightly below weight because pitched thrust supplies the remaining
vertical force. Body thrust also supplies a nose-up moment through the existing
1.1 m engine vertical offset. Both balances are solved, not approximated by
setting lift equal to weight alone.

## Measured scenario results and tolerances

Every sample is checked for finite state and force/moment totals. The harness
uses integer tick indexes and `FixedStepClock::tick`; scripts can only produce
Controls during a run. Takeoff and landing each replay twice with a digest of
**all state scalars at every tick**, plus event/metric comparisons. These prove
same-build repeatability, not cross-compiler or network synchronization.

| Scenario | Measured result | Acceptance envelope |
|---|---|---|
| Straight level, 120 s / 14,400 ticks, no feedback | Max altitude drift 4.36e−10 m; TAS drift 0 at printed precision; pitch drift 7.56e−12°; roll/heading drift 0; peak rate 2.54e−15 rad/s; final VS 8.93e−12 m/s, ax −6.83e−13 m/s² | Altitude <1 m, TAS <.1 m/s, pitch <.1°, roll/heading <.01°, rates <1e−4 rad/s, no stall warning |
| +1° pitch perturbation, fixed trim controls, 120 s | Altitude 997.359..1002.604 m, TAS 109.764..110.233 m/s, peak rate .008742 rad/s | Altitude 980..1020, TAS 108..112, rate <.05; bounded natural response, not complete return to identical energy |
| ±.2 control pulse, .25 s | Right/left roll rates ±.061555; pitch pull/push +.029635/−.024979; yaw ±.013907 rad/s | Correct sign, .001.. .2 rad/s magnitude; input assignment cannot change rate, one-tick full pull <.02 rad/s |
| Dynamic pressure | Control moment increments at 120 vs 60 m/s = exactly 4 to test tolerance | Ratio 4 within 1e−12 at identical conditions |
| Stall recovery from 24° AoA, 80 m/s, 2,000 m | Push −.3 for 2 s gives AoA 3.228°; test pilot then targets 2° pitch. At 25 s: AoA 5.784°, TAS 121.102, altitude loss 340.178 m; warning cleared | AoA after push <5°, final <8°, no final warning, altitude >1,500 m |
| Park and taxi | Settled CG 3.380351 m, pitch .491235°, speed ~2e−11 m/s; zero altitude range in last 10 s. Released idle after 60 s: 1.493518 m/s. .5 throttle for 15 s: 8.930955 m/s | Park speed/rate <1e−5/1e−6; idle <3, powered taxi <15 m/s with positive acceleration |
| Nosewheel / brakes | ±.2 steering for 3 s gives −24.506°/+24.504° heading; idle-throttle full brake stop from 8.931 m/s in 11.033 m, then held | Correct sign, gear altitude >2.8 m; brake distance 0..100 m, final speed <1e−5 and position drift <1e−4 m |
| Takeoff | Rotation 78.010944 m/s; qualified liftoff 88.331997 m/s, 29.108333 s after brake release, 1,244.751 m from initial position; pitch 8.241329°, AoA 7.674838°, VS .873339 m/s. Gear retracted above 10 m | Liftoff 75..105 m/s, distance 500..2,200 m, total time <50 s, max pitch <17°, heading <5° and roll <1° |
| Sustained climb at 120 s | Altitude 1,799.756 m; TAS 190.696 m/s; VS 30.185 m/s; pitch 10°, AoA .893° | Altitude >1,000, VS >10; speed <220; finite/controlled |
| Cruise, 120 s, 5 km / 160 m/s | Throttle .558690289, pitch 4.118545°, altitude range 1.16e−9 m, speed range 6.61e−11 m/s | Altitude range <1 m, speed range <.1, rates <1e−4 |
| Cruise, 120 s, 7 km / 200 m/s | Throttle .649429674, pitch 2.846478°, altitude range 7.73e−8 m, speed range 1.54e−9 m/s | Same cruise tolerances |
| Energy trade, +.1 pull for 5 s at trim power | Height +29.711 m, speed −3.815 m/s, VS +15.681 m/s relative to level reference | Height +5 m or more, speed decreases >.2 m/s |
| Full-power level test pilot, 360 s | Terminal TAS 268.457853 m/s; last 30 s speed range 6.24e−8 m/s; altitude 1,002.902 m | Max TAS <300, tail range <.1; physical drag limits acceleration |
| Approach | Existing full flaps and gear down, 120 m / 78 m/s, gamma −2.5°: pitch −.568780°, AoA 1.931220°, VS −3.402312 m/s, throttle .410936 | Trim converges; descent reaches flare and normal landing |
| Landing, 100 s including stopped hold | Contact at 38.216667 s; forward speed 75.199071 m/s, pre-contact sink .807271 m/s; pitch 2.233472°, AoA 2.834320°; peak total normal 649,806.072 N (1.035 g weight equivalent); max compression .225020 m; max CG height after contact 3.625831 m; rollout 881.493845 m, stop time 22.225000 s | Sink 0..2 m/s, speed 65..85, peak <2× weight, compression <.45 m, CG after contact <4.5 m, rollout 300..1,300 m, stop <40 s, final speed/rates <1e−5 |
| Continuous full cycle, 420 s / 50,400 ticks | Cruise begins 90.041667 s near 380 m; after first 20 s, max error to 400 m is 3.341894 m and to TAS 110 is .061051 m/s. Touchdown 288.083333 s at 75.453931 m/s, .910416 m/s sink; rollout 886.368035 m, stop 22.283333 s later; final north distance 26,611.167 m, final speed ~2e−11 | Every phase reached on one State without reset; cruise errors <5 m/.2 m/s; normal landing, max altitude <410, CG >3 m, peak contact <2× weight; final stable stop |

Qualified takeoff liftoff requires 24 consecutive ticks with no gear normal load,
CG AGL >4 m and positive VS; this includes a deliberate .2 s confirmation delay.
It is not the exact first tire-separation instant. The takeoff script applies a
small early steering pulse and rudder/roll feedback, full power, a rotation
command, pitch hold and flap retraction over 20 s. Climb figures are aggressive
full-power model results, not claimed operational climb targets.

Landing flare starts below 15 m CG AGL, targets about −.9 m/s and reduces throttle
to .25; after contact it commands idle, full brakes and spoilers. Contact speed/
sink are captured immediately before the contact tick. Forward speed is the horizontal ground-velocity magnitude in calm air.
Peak contact is the sum
of the three exact normal forces at sampled final 240 Hz substeps; it is not a
per-tire structural load or a guaranteed maximum over unsampled substeps.
The continuous cycle uses test-only trim feedforward and pitch/vertical-speed/
speed feedback; approach target speed reduces with flap deployment. It does not
install an autopilot in the core or client. Separate cruise regressions use
fixed trim controls and no feedback at all.

## Stall coefficient measurements

| AoA | CL | CD | Baseline CD |
|---:|---:|---:|---:|
| 10° | 1.057670 | .063539 | .063539 |
| 14° | 1.410226 | .095848 | .095848 |
| 20° | 1.122421 | .189801 | .068781 |
| 25° | .851895 | .370440 | .048948 |
| 45° | .901603 | .952185 | .042887 |
| 90° | ~0 | 1.822000 | .042887 |

The deep-stall approximation transitions to plate-like behavior; it need not
continue decreasing monotonically from 25° to 45°. No separated-flow, tail-blanking,
spin or stall hysteresis accuracy is claimed.

Full thrust falls from 206,537.375 N at 1 km / 110 m/s to 119,806.314 N at
7 km / 110 m/s; at 1 km / 220 m/s it is 192,086.498 N. Altitude and speed
lapse signs are tested; the existing engine formula is otherwise unchanged.

## Build, automated and graphical validation

| Configuration | Configure / build | CTest |
|---|---|---|
| GCC Debug native | Passed | 21/21, includes real graphical smoke; 5.55 s |
| GCC Release native | Passed | 21/21, includes real graphical smoke; 4.00 s |
| GCC Debug headless | Passed | 19/19, no graphics dependencies; 2.63 s |
| GCC Debug headless ASan + UBSan | Passed | 19/19, no sanitizer diagnostics; 6.89 s |

All original nine core suites and client coordinate/interpolation/smoke tests
remain. Ten new suites: trim, controls, stall, taxi, takeoff, cruise, landing,
cycle, diagnostics and robustness. Tests use active runtime checks in Release.
Native graphical suites run sequentially to avoid shared-desktop focus failures.
The actual client was also run for 180 real-clock frames in Debug and Release,
with screenshots and clean shutdown. Smoke clicks the real airborne reset,
checks trim altitude/TAS/pitch, and captures after the reset at frame 150.
Screenshots were inspected; the representative M1 Release image is
`images/m1-native-client.png`, a generated capture that is not version
controlled.
Initial final Debug smoke attempts missed mouse-look (look/turn flags false)
while the other checks passed. The test now raises its window and sends explicit
absolute mouse motion outside ImGui before requesting relative mode, removing
dependence on asynchronous native warp timing. Full Debug/Release runs then
passed sequentially. Smoke also reframes the aircraft with Home before capture;
no runtime physics or input assertions were bypassed. Actual desktop interaction
can still interfere with this optional test, as documented in M0.
Final project incremental builds emitted no compiler warnings or errors.
Physical controllers, Windows/MSVC, Clang and native Wayland remain unverified.

Reproduction (existing workstation development-library overrides described in
[BUILDING.md](BUILDING.md) remain necessary on this host):

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
./build/headless/tests/ofs_flight_scenarios all
./build/headless/tests/ofs_flight_scenarios cycle
```

Rootless sanitizer builds on this host retain `CMAKE_EXE_LINKER_FLAGS=-L.../.cache/sysroot/usr/lib64`
and run with that directory in `LD_LIBRARY_PATH`, as in M0. Full configure/build/
test/runtime logs and emitted metrics are retained in ignored `.cache/m1`.
Debug/headless and Release printed scenario metrics matched to displayed
precision in this run; that is not a cross-compiler bit-identity guarantee.

## Numerical findings, performance and remaining realism limits

The robustness matrix covers 56 combinations of TAS 0, .01, .49, .51, 5, 40,
200 and 600 m/s with AoA −180°, −90°, −30°, 0°, 30°, 90°, 180°, two seconds
each with extreme pitch/roll/rudder, full flaps/spoilers and asymmetric thrust.
All remain finite and unit-normalized. Stalled clean/full-flap coefficient
sweeps also remain bounded and dissipative. Zero airflow is guarded; wind-axis
construction has no normalization singularity at vertical airflow. External
nonfinite state is rejected; controls/time retain M0 sanitization. Zero wing area in the disabled-aero test
configuration returns a finite stall-speed diagnostic sentinel (0). These checks
do not certify every physically impossible finite double-valued input.

Core stepping still has no allocations, locks, graphics dependency or ECS.
The friction solve uses fixed stack arrays and fixed iteration count only while
gear contacts are active. Trim/scenario feedback is separate from runtime;
cruise stepping never runs a trim solve. No many-aircraft performance benchmark
or physical controller feel certification is claimed.

Remaining approximations: fixed mass/CG/inertia, linear normal-flow moment
coefficients, algebraic surfaces without actuator rates, normalized elevator
offset rather than a separate stabilizer/trim-tab model, simple flaps/spoilers,
rough wave drag/engine lapse, IAS≈CAS, flat infinite contact plane, capped oleo
spring stroke and emergency belly clamp, separate lateral/longitudinal friction
limits rather than a tire friction ellipse, no rolling wheel inertia or tire/
anti-skid/suspension detail. Continuous tests fly beyond the client's finite
visual runway/grid; the headless plane remains valid there. The placeholder
exterior has no runtime GLB/animated gear. Crosswind landing, spin recovery,
very high Mach fidelity and certification against authoritative aircraft data
are outside these measurements.

M1 supplies a stable, measurable renderer-independent baseline for an eventual
**authoritative** multiplayer server. It does not establish portable deterministic
lockstep or a capacity target. Recommended M2 scope: explicit versioned State/
Controls serialization (including elevator trim), server-owned 120 Hz ticks,
input ordering/validation, snapshot timing, remote interpolation, bounded local
prediction/reconciliation, headless replay/network-loss tests and many-aircraft
profiling. Keep gameplay, weapons, terrain and avionics outside that foundation.
M1 stops here; no M2 code was started.
