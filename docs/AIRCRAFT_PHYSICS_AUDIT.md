# A320 and Su-57 physical-model audit

Audit date: 3 October 2026. M3.68.1 follow-up: 4 October 2026; see
[M3_68_1_VALIDATION_REPORT.md](M3_68_1_VALIDATION_REPORT.md) for current closure results,
[MASS_ENGINE_AUDIT.md](MASS_ENGINE_AUDIT.md) for load corrections, and
[SU57_PHYSICS_PROVENANCE.md](SU57_PHYSICS_PROVENANCE.md) for the parameter inventory. Production priorities are A320, Su-57, and the
NASA/public F-16 as an optional independent validation aircraft. Typhoon and
SR-71 remain available. The temporary Falcon is removed; wire ID 2 is reserved
and rejected. Historical milestone reports describe earlier implementations.

This milestone establishes traceable physical geometry and an engineering
architecture. It does not establish Airbus control-law fidelity, certified
A320 performance, or OEM/classified Su-57 aerodynamics.

## Authoring and evidence

`data/physics/a320.json` and `su57.json` are the reviewed authoring sources.
CMake compiles their numerical assignments, shared geometry, high-lift schedule
and component mass distributions into the build directory. The Blender helpers
read these same files. `data/aircraft/*.json` are generated exports of the actual
configured registry; `regression.provenance` detects stale exports and source
hashes. `data/models/engineering-fallback.json` records shared/reconstructed
laws, including the transport engine deck and FCS constants.

Every numeric authoring record includes units, source, explanation, and one of
REFERENCE, DERIVED, ESTIMATE or CALIBRATED_APPROXIMATION. REFERENCE is a retrieved
primary-source anchor. DERIVED inherits the uncertainty of its inputs. A
numerical model measurement is evidence about that model, not evidence about
the real aircraft. No new coefficient was identified from a Cobra video or
fitted to make a reference acceptance test pass.

Primary sources reviewed:

- [Airbus A320 Aircraft Characteristics, 15 July 2025](https://www.aircraft.airbus.com/sites/g/files/jlcbta126/files/2025-07/AC_A320_20250715.pdf): 2-2-0 pp4–5 sharklet dimensions; 2-1-1 p2 WV017 masses and p5 fuel; 3-5-0 p1 approach; 3-3-1 p2 and 3-4-1 p2 CFM56 ISA runway curves. Planning information is not an airline AFM.
- [Airbus A320ceo product data](https://www.aircraft.airbus.com/en/aircraft/a320-family/a320ceo): height and Mmo. Its maximum fuel figure includes options; the selected standard-tank configuration is identified below.
- [EASA A.064](https://www.easa.europa.eu/en/downloads/16507/en): A320 section1 items15/16, datum and MAC. Verified through the indexed primary document; the full current PDF fetch was unavailable during this audit.
- [EASA E.003, historical issue06](https://www.easa.europa.eu/sites/default/files/dfu/TCDS%20EASA%20E.003%20issue%2006.pdf): III6.1 CFM56-5B4/P takeoff thrust. Engine ratings from this historical issue are not claimed to reproduce a current installed performance deck.
- [FAA A320 accident lessons](https://www.faa.gov/lessons_learned/transport_airplane/accidents/VT-EPN) and [Airbus protection concepts](https://www.airbus.com/en/newsroom/stories/2023-02-safety-innovation-7-flight-envelope-protection): public control concepts. The FAA aircraft was A320-231; its incident-specific numerical speeds are not used as A320-214 data.
- [Sukhoi patent RU2440916C1](https://patents.google.com/patent/RU2440916C1/en): integrated configuration, separated nacelles, moving leading-edge extensions, all-moving tails and nozzle arrangement. The patent supplies qualitative mechanism evidence, not a production dimension drawing or coefficient database.
- [Rostec/UEC first-stage engine statement, 2018](https://www.rostec.ru/media/news/dvigatel-al-41f-1-zavershil-gosudarstvennye-stendovye-ispytaniya/): AL-41F1 / izdeliye117 identification. The numerical engine estimates below are not specifications extracted from this statement.

Downloaded drawings/reference imports and generated assets remain local. The
Supplied Su-57 artwork's redistribution permission remains unverified.

## A320 configuration and geometry

Selected engineering variant: **A320-214 ceo, CFM56-5B4/P, sharklets, WV017**.
No A319/A321 or neo engine map is blended into the model.

| Quantity | Selected value | Evidence / qualification |
|---|---:|---|
| Length / span / height | 37.57 / 35.80 / 11.76 m | REFERENCE Airbus; 11.91 m on the drawing is a lateral wing station, not height |
| Wing area / quarter-chord sweep | 122.6 m² / 25° | ESTIMATE retained engineering anchors; no verified variant-specific primary area/sweep entry retrieved |
| MAC | 4.1935 m | REFERENCE EASA |
| Horizontal-tail span | 12.45 m | REFERENCE Airbus; tail planform/pressure partition remains ESTIMATE |
| Wheelbase / main track | 12.64 / 7.59 m | REFERENCE Airbus |
| Nose axle / main axle aft of nose | 5.07 / 17.71 m | REFERENCE nose station; DERIVED main station from wheelbase |
| Engine lateral centre | ±5.75 m | REFERENCE drawing; fore/aft and vertical force stations from owned artwork remain ESTIMATE |
| Certification datum | 2.540 m forward of nose | REFERENCE EASA; distinct from the simulator CG reference |
| Reference CG in asset coordinates | aft15.51, up3.55, port0 m | ESTIMATE; the model is not a measured loading diagram |
| Aerodynamic reference | body (0,0,0) m | ESTIMATE reference-CG choice, not an Airbus pressure-centre measurement |
| Tail force sites | body X−16.4, Y±3.7 m | ESTIMATE aligned aft-tail partition; separate from elevator hinges |

Body coordinates are forward/right/down relative to the configured reference
CG. glTF is aft/up/port; Blender authoring is aft/starboard/up. Shared conversion
functions and their inverse are checked automatically. Pressure application
sites are engineering partitions and do not claim measured pressure centroids.

The local A320 mesh measures 37.570/35.800/11.770 m: height error +0.010 m.
Elevator and rudder pivots match the shared records; existing upper-skin
aileron pivots differ vertically by 0.0765 m from the shared geometric station,
inside the explicit 0.15 m conformance tolerance. All three wheel-contact
longitudinal/vertical stations match. Paired-tire lateral offsets around the
published axle track are expected. The current asset does not contain a
separate animated A320 slat mesh; physics and pose channels have a distinct
slat schedule, but this is not a claim that missing artwork has been supplied.

### Mass, CG and inertia

| Quantity | Value | Provenance |
|---|---:|---|
| Basic/operating mass | 42,000 kg | ESTIMATE including crew/basic equipment; not manufacturer's universal empty mass |
| Default fuel / payload / total | 8,000 / 14,000 / 64,000 kg | ESTIMATE scenario load; total DERIVED |
| MTOW / MLW / MZFW | 78,000 / 66,000 / 62,500 kg | REFERENCE WV017 planning limits |
| Standard ceo CFM usable fuel | 24,209 L | REFERENCE standard tanks, excluding optional ACT |
| Nominal fuel mass capacity | 19,004.065 kg | DERIVED using published planning density 0.785 kg/L |
| Fuel / payload centroid | X−0.4 / +0.6 m | ESTIMATE loaded-reference centroids |
| CG planning bounds | 17–36.8% MAC | ESTIMATE advisory bounds; the reviewed planning jacking diagram was WV015, not a certified WV017 flight envelope |
| Reference CG | 25% MAC | ESTIMATE convention |

Published maximum weights are audit metadata, not dispatcher-enforced limits.
No certified CG envelope is enforced. Payload offsets and fuel depletion change
the actual CG and full inertia tensor through first moments and parallel-axis
mechanics. Unknown loaded reference position prevents converting the planning
MAC bounds into a certified constraint.

The 64t reference inertia is derived from a 37.2t structure, two estimated
2.4t installed engine components, fuel and payload. The structure centroid
balances the reference first moment. Each component contributes
`m (|r|² I − r rᵀ)` plus its distributed covariance inertia. Authoring records
contain the diagonal variances in m²: structure(38,17,1.6), engine(1,.25,.25),
fuel(35,20,0), payload(30,.8,.25). Derived Ixx/Iyy/Izz are approximately
1.044/2.205/3.089 million kg m²; Ixz≈706 kg m² is a matrix entry, not the
oppositely signed aerospace product. Ixy/Iyz assume symmetry. None is measured
Airbus inertia. Principal axes are calculated from the actual symmetric tensor.

### Named propulsion

CFM56-5B4/P reference takeoff thrust is **12,010 daN = 120,100 N per engine**
(REFERENCE). The normalized power variable is not literal compressor N1.
Installed/off-design corrections, engine component mass, spool lag2s, fuel
flow and TSFC are ESTIMATE.

The deck uses ISA pressure ratio δ and temperature ratio θ:

`T = 120100 δ^.75 / sqrt(θ) max(.25, 1 − .45 M + .10 M²) (.04 + .96 power^2.5)`.

`fuel = .12 + T × 1.65e−5 × (1 + .20 (1−power))` kg/s per engine.

All map exponents, floors and off-design constants are explicitly estimated.
This is a high-bypass engineering surrogate anchored to one static rating,
not a thermodynamic/manufacturer deck. Non-ISA temperature is not supplied to
this deck interface. Engine health and fuel exhaustion independently stop
thrust and flow; actuators/spool remain distinct from aerodynamic evaluation.

### Transport aerodynamics and controls

Finite-wing lift slope is DERIVED with AR=b²/S and estimated quarter-chord sweep:
`a = 2π AR / (2 + sqrt(4 + AR² (1 + tan²Λ))) = 4.7921/rad`.
Inputs and method are approximate; this is not measured Airbus lift data.
Clean zero-lift incidence−2°, CLmax1.42, full CLmax2.6, CD0.022, e.82 and
stability/control derivatives remain ESTIMATE.

The transport path removes the generic elevator-to-main-wing lift shortcut.
Tail force generates elevator pitching moment at the aft physical site. Lift,
induced/profile/device/wave drag, local sideslip forces and roll/yaw/pitch
moments remain physically summed around actual CG. The polar uses
`k=S/(π b² e)`; ground effect multiplies induced drag by
`1−.28 exp(−height/(.18 span))`. Bounded transport compressibility increases
lift by up to8% over Mach.55–.82. These effects, static stall progression,
separation drag and near-stall control blanking are engineering estimates.
There is no Airbus coefficient database hidden behind the transport label.

| Setting / normalized travel | Flap / slat degrees | ΔCL / CLmax | ΔCD / ΔCm |
|---|---|---|---|
| CLEAN / 0 | 0 / 0 | 0 / 1.42 | 0 / 0 |
| 1 / .2 | 0 / 18 | .20 / 1.75 | .004 / −.015 |
| 1+F / .4 | 10 / 18 | .45 / 1.90 | .015 / −.030 |
| 2 / .6 | 15 / 22 | .70 / 2.15 | .027 / −.050 |
| 3 / .8 | 20 / 22 | 1.00 / 2.40 | .045 / −.080 |
| FULL / 1 | 35 / 27 | 1.20 / 2.60 | .070 / −.100 |

All angles, increments and transit interpolation are ESTIMATE. Detent1
explicitly represents slats alone; 1+F is separately commanded, without the
real aircraft's automatic speed-dependent retraction logic. Gear ΔCD.025,
spoilers ΔCD.060 / ΔCL−.55 and spoiler ΔCm+.02 are estimates. Elevator−25/+20°,
aileron±25°, rudder±30°, normalized actuator2.5/s, flap.25/s and spoiler1.7/s
are also estimates, not published variant-specific limits/rates.

The raw airframe is testable with `fcs_enabled=false`. Actuator rate/lag lives
in the simulator; the OpenFlightSim transport FCS commands those actuators.
It reconstructs load-factor demand (clean−1/+2.5, high-lift0/+2), an alpha-command
transition, neutral bank return above33°, outward bank demand fading to67°,
and nose-up high-speed demand above Mmo.82 or estimated Vmo350kt. Gains and
alpha thresholds are estimates; the public concepts do not reveal Airbus
ELAC algorithms. Ground rotation uses direct surface authority. Protections
never clamp the rigid-body attitude or angular rate.

### Independent performance and discrepancies

`data/reference/a320/performance.json` is separate from coefficient generation.
The approach point is held out. `aircraft_audit.performance` scans the raw
full-flap polar and predicts1.23VS at66t: **137.66 kt**, versus Airbus's
**136 kt**, error+1.66kt inside the declared±5kt engineering tolerance. The
ratio1.23 is a public A320 control concept; using it to estimate this planning
approach figure is an approximation, not proof that Vapp always equals VLS.

`aircraft_audit.performance_report` prints comparisons without turning
non-equivalent runway scenarios into external-validation assertions:

| Diagnostic | Simulated result | Independently documented comparator / limit |
|---|---|---|
| 64t, ISA, all-engine takeoff to CG15.24m | 2046.5 m; rotation at1012.6m using78m/s test-pilot trigger | Planning runway curve≈1330±100m; difference+716.5m. Certification/engine-out/accelerate-stop/configuration schedules differ; accuracy remains unvalidated |
| 66t,136kt entry at CG15.24m to stop | 933.3m; flare281.8m, rollout651.6m, sink1.69m/s | CFM56 landing field≈1590±80m; unfactored0.6×=954±48m, difference−20.7m. Pilot, braking, reverse and screen-height assumptions are not certified |
| Clean3000m/150m/s glide polar | peak L/D17.49 at7.2°, approximate sink8.56m/s | No independently retrieved variant polar/glide point; engineering diagnostic |
| 1000m/100m/s all-engine excess thrust | grade.213, approximate climb21.33m/s | Not a solved climb equilibrium or engine-out gradient; no primary matching point retrieved |
| 66t,11000m,M.78 equilibrium | drag47.26kN, thrust47.38kN, fuel1.040kg/s | Engineering equilibrium; Mmo.82 is published but is not evidence validating drag/fuel consumption |

The takeoff discrepancy is retained. No coefficients were adjusted to erase
it. Clean stall speeds, rotation speeds, climb curves, cruise drag/TSFC and
certified field lengths need independent variant/condition-specific data
before being claimed validated.

## Su-57 configuration, geometry and propulsion

The selected engine era is **AL-41F1 / izdeliye117**, excluding AL-41F1S and
izdeliye30/future engine specifications. Primary sources establish the engine
identity and qualitative lifting/vectoring arrangement. Verified primary
numerical dimensions/maps were not retrieved. Frequently quoted dimension
anchors are therefore retained as ESTIMATE, not promoted to REFERENCE.

| Quantity | Selected estimate |
|---|---|
| Length / span / gear-down height | 20.1 / 14.1 / 4.6m |
| Reference area / MAC / sweep anchor | 78.8m² / 5.5m / 48°; blended area and sweep convention uncertain |
| Basic / initial fuel / internal capacity / payload | 18500 / 6500 / 10300 / 100kg; default total25100kg |
| Reference CG asset / aerodynamic reference body | (10.95,2.45,0)m / (0,0,0)m |
| Advisory CG bounds | 17–35% MAC; estimated metadata, not a published/enforced flight envelope |
| Nozzle force exits | body(−7.85,±1.34,+.16)m, separation2.68m |
| Nozzle pivots / engine mass centroids | body(−6.4,±1.34,+.16)m; centroid proxy uncertain |
| Neutral thrust direction | body+X; true outward engine-axis cant not numerically established |
| Mirrored hinge axes | (0,cos30°,±sin30°), estimated single-axis cant |
| TV limit / rate | ±15° /60°/s, estimated |
| Dry / reheat thrust | 93 /147kN each, estimated installed anchors |
| Engine component mass / spool | 1600kg each / .8s, estimated |
| Dry / reheat TSFC | 2.2e−5 /5.1e−5 kg/(N s), estimated |
| Elevator / aileron / all-moving fin limits | −25/+20° /±22° /±25°, estimated |
| LEVCON limit / flap limit / actuator rate | ±20° /20° /4 normalized/s, estimated |
| Nose / main contact points | (6.95,0,2.45)m / (−2.3,±2.25,2.45)m, estimated |

The engine uses the existing estimated engineering deck: density-ratio
exponent.60, Mach lapse `1−.2 min(M,2.5)+.23 M²/(1+M²)`, dry power curve
and continuous reheat blend. Inlet recovery is implicitly ideal; no Su-57
inlet map or separate thermodynamic cycle is invented. Installed losses and
mass are uncertain. Positive idle thrust requires a running healthy engine
and fuel; shutdown is separate from throttle idle.

Component inertia uses a15.3t structure plus engines/fuel/payload. Structure
variances(5.29,1.6384,.2025)m², engine(.81,.0784,.0784), fuel(12,5,0),
payload(.01,.01,.01) are explicit estimates. The first moment is balanced at
the reference load. Derived Ixx/Iyy/Izz≈67071/328905/388965kg m²,
Ixz≈4649kg m²; zero Ixy/Iyz assumes symmetry. These are not measured Su-57
inertias or tuning values inferred from turn-rate targets.

### Force-vector thrust mechanics

For each nozzle Rodrigues rotation changes both its direction and its exit
position about the shared hinge. `F=T direction` and
`M=(exit−actualCG)×F` are the only TV force/moment contributions. The control
allocator uses the articulated derivative
`dM/dθ = (exit−CG)×(axis×F) + (axis×(exit−pivot))×F`.
There is no TV/Cobra pitch/yaw torque constant.

Tests cover left/right/both engines, neutral/symmetric/differential deflection,
two power levels and three CG load offsets, preservation of force magnitude,
moment sums/signs/coupling and finite-difference Jacobians. Symmetric negative
angles pitch nose up with roll/yaw cancellation; differential angles couple
yaw right and roll left. Single neutral left/right engines yaw right/left.
Console diagnostics print both three-component forces and total moments.
This validates mechanics of the estimated geometry, not the OEM hinge angles.

### Unsteady aerodynamic architecture

The separated wing/body surrogate retains estimated finite lift/polar/Mach
curves, forebody/vortex augmentation, local tail and fin forces, nonlinear
pitch moment, LEVCON load redistribution and local control blanking. The
fin pair remains one equivalent physical force site; differential all-moving
fin aerodynamics is not resolved as two independent surfaces. The body/wing
blend and LEVCON force shares are estimates, not a pressure database.

Each wing has independently integrated lagged incidence, separation fraction
and vortex strength. Local incidence includes angular airflow and an estimated
opposite sideslip perturbation±.35β. Alpha lagτ=.12s; alpha-dot correction
.12s is bounded±8°; detachment/reattachment use differentτ=.18/.55s;
vortex persistenceτ=.25s. Separation changes lift/drag/control authority;
vortex strength follows the existing growth/breakdown curve with lag. Unequal
states cause actual unequal forces and roll/yaw coupling. Autorotation/spin
can emerge from forces and local airflow; no spin or recovery capability is
claimed validated. The nonlinear parameters and the allocator/FCS remain
ESTIMATE. No Su-57-specific unsteady identification data was available.

Both semi-implicit integration and RK4 advance aerodynamic memory; pure
`evalAero` never advances it. Complete memory is accepted/rejected with finite
range checks, replicated in protocol8 and restored for prediction, C API
snapshot and replay. Remote interpolation handles wrapped incidence angles.

`aircraft_audit.unsteady` runs a20s raw45°-entry pitch/yaw/release trajectory,
checks bounded states, asymmetric incidence, detachment/reattachment hysteresis,
pure evaluation and deterministic snapshot continuation for both integrators.
The ignored CSV `output/aircraft-audit/su57-unsteady.csv` includes alpha/beta,
p/q/r, speed/altitude, normal acceleration, specific energy, both nozzle
angles, surface deflections and all lag/separation/vortex states. It is a
mechanism/regression history, not a Cobra or flight-envelope acceptance target.

### Supplied model measurements and anisotropic error

The original pipeline baseline is17.1700×12.3671×2.7801m. Applied X/Z scale
1.17065 and lateral scale1.14012 stretch length17.06% and span14.01%; span is
2.6077% narrower than uniform length scaling would produce, and volume scales
by1.56244. Gear is subsequently authored and vertical origin shifted to fit
4.6m total height. This is not a measurement establishing real Su-57 shape.
That describes the donor retired in 0.4.3. The current donor is scaled uniformly
by 0.10261 m per unit to 20.1 m and narrowed laterally by 0.9698 to 14.1 m;
`data/geometry/su57-normalization.json` records those factors.

The local assembled GLB measures20.1000×14.1000×4.6000m. Agreement is imposed
by normalization. Independent real planform/area/tail/gear geometry error
remains **unknown**, since no dimensioned production drawing was verified.
Further nonuniform corrections would not establish accuracy.

| Local measurement | Value / physics alignment |
|---|---|
| Engine mesh rear exits | aft18.8053m; bodyX−7.8553m, 0.0053m from force-exit station |
| Engine mesh lateral centres | ±1.3378m; spacing2.6756m, 0.0044m from configured spacing |
| Wing station at semispan3.525m | leading/trailing aft10.625/14.697m |
| Wing station at semispan5.640m | leading/trailing aft12.922/15.265m |
| Stabilator hinges | bodyX−4.8021, Y±2.2802, Z+.0822m |
| Aileron hinges | bodyX−4.3105, Y±5.1305, Z+.0822m |
| All-moving fin hinges | bodyX−4.2168, Y±2.6223, Z−.3275m |
| LEVCON hinges | bodyX+4.9100, Y±1.9382, Z+.0500m |
| Nozzle pivots | bodyX−6.4000, Y±1.3400, Z+.1600m |
| Wheel contacts | all three longitudinal/vertical stations agree within numerical precision |

These controls/gear are mesh measurements or authored estimates, not
independent aircraft evidence. Force sites differ from hinge sites: a hinge
is not a pressure centroid. Missing/unreliable licensing keeps all supplied
and derived Su-57 model binaries local.

## Geometry debugging and verification

The Effects panel's **Physics geometry** option draws actual CG, reference
point, principal inertia axes, aerodynamic force sites/vectors, articulated
nozzle exits/vectors, contact points and hinges. Magenta marks CG/hinges,
cyan reference, green aero, orange thrust, white contacts, RGB principal axes.
Markers inside the mesh remain visible through it. The default is off; marker
positions use the same loaded-CG transform as the physics. No asset is needed
to draw the diagnostics.

Reproduce the new checks:

```sh
cmake --preset headless
cmake --build --preset headless
python3 scripts/export_aircraft_provenance.py --binary build/headless/tests/ofs_provenance_export
ctest --test-dir build/headless --output-on-failure
cmake --preset flight-validation
cmake --build --preset flight-validation
ctest --test-dir build/flight-validation --output-on-failure -R external.f16
```

The F-16 importer integrity hashes and independent NASA checkcases remain
separate from production registry models. Ordinary asset tests may explicitly
skip missing local models; asset-validation requires them. No restricted
reference inputs, generated models, build products or unrelated audit ZIPs are
included in the source repository.

## Workstation verification status

Debug and Release native builds passed, along with all 124 CPU/network tests,
all four required local aircraft asset checks, NASA independent static shots,
13 focused ASan/UBSan tests and four native client tests. Native A320/Su57 geometry captures are
local under `output/aircraft-audit`. The existing optional desktop smoke harness
cannot complete minimize/restore on this GNOME/XWayland session; its assertion
is retained, and this failure is not counted as a passing graphical check.
Blender MCP was unavailable, so no claim is made of a fresh live source-scene
measurement or regenerated supplied artwork. Geometry evidence above comes
from the existing local GLB and the audited normalization source.
