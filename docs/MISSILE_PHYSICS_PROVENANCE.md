# M4 weapon and sensor parameter provenance

The **Dev IR-90** and **Dev AR-157** are development weapons. Their dimensions
are inspired by the Sidewinder and AMRAAM classes. They are not named reproductions,
certified performance models, or operational loadouts. Public specifications do
not establish their motor, seeker, guidance or aerodynamic characteristics.

Primary references, checked 2026-10-06:

- [NAVAIR AIM-9X](https://www.navair.navy.mil/product/AIM-9X-Sidewinder): public
  length 3.02 m, diameter 0.13 m, launch mass 84.37 kg. Speed/range are classified.
- [USAF AIM-120](https://www.af.mil/News/Fact-Sheets/Display/Article/104576/aim-120-amraam/):
  public length 3.66 m and diameter 0.1778 m; inertial/active radar architecture.
- [NASA NTRS 20170001430](https://ntrs.nasa.gov/api/citations/20170001430/downloads/20170001430.pdf):
  public proportional-navigation theory. This is a guidance reference, not
  evidence for either development missile's performance.

`REFERENCE` means a public dimension. `DERIVED` means a calculation from the
listed assumptions. `ESTIMATE` means a transparent engineering design choice.
`CALIBRATED_APPROXIMATION` means a coefficient/control choice adjusted against
this simulator's deterministic scenarios; it is not calibration against real
missile flight data. Angles below are degrees for readability; code uses radians.

## Definitions

| Parameter | IR-90 / AR-157 | Units | Category | Source/derivation | Confidence |
|---|---|---|---|---|---|
| Length | 3.00 / 3.66 | m | ESTIMATE / REFERENCE | Rounded Sidewinder-class / USAF dimension | medium |
| Diameter | 0.13 / 0.178 | m | REFERENCE / DERIVED | NAVAIR dimension / rounded USAF 0.1778 | medium |
| Launch mass | 90 / 157 | kg | ESTIMATE | Development mass classes; IR deliberately differs from AIM-9X | low |
| Propellant | 24 / 55 | kg | ESTIMATE | Development motor sizing | low |
| Dry mass | 66 / 102 | kg | DERIVED | Launch mass minus propellant | high within model |
| Boost thrust | 15000 / 22000 | N | ESTIMATE | Development rocket curves | low |
| Boost duration | 2.2 / 3.5 | s | ESTIMATE | Finite boost phase | low |
| Sustain thrust | 4500 / 6500 | N | ESTIMATE | Boost-sustain motors | low |
| Sustain duration | 5 / 7.7 | s | ESTIMATE | Finite sustain phase | low |
| Ignition delay | 0 / 0.3 | s | ESTIMATE | Rail launch / ejected store that lights its motor clear of the aircraft | low |
| Release velocity | 1 / 6 | m/s down | ESTIMATE | Rail launch / ejector push in aircraft axes | low |
| Total impulse | 55500 / 127050 | N s | DERIVED | Integral of configured piecewise thrust | high within model |
| Reference area | 0.01327 / 0.02489 | m2 | DERIVED | pi diameter²/4 | high within model |
| Axial inertia | mass diameter²/8 | kg m2 | DERIVED | Cylinder approximation at current mass | medium |
| Pitch/yaw inertia | mass length²/12 | kg m2 | DERIVED | Slender cylinder approximation | medium |
| Normal-force slope | 20 / 20 | 1/rad | CALIBRATED_APPROXIMATION | Fin/body incidence response, saturates with sin(alpha)cos(alpha) | low |
| Direct control force slope | 2 / 2 | 1/rad | CALIBRATED_APPROXIMATION | Actuated fin contribution | low |
| Base drag coefficient | 0.45 / 0.40 | dimensionless | ESTIMATE | Development axial reference-area drag | low |
| Induced drag factor | 0.07 / 0.07 | dimensionless | ESTIMATE | Adds factor times normal-coefficient magnitude squared | low |
| Transonic rise | 0.35 / 0.35 | dimensionless | ESTIMATE | Gaussian peak at Mach 1.05, width 0.22 | low |
| Wave drag factor | 0.20 / 0.20 | dimensionless | ESTIMATE | 0.20(1-exp(-(Mach-1))) above Mach 1 | low |
| Stability coefficient | 6 / 6 | 1/rad | CALIBRATED_APPROXIMATION | Restoring pitch/yaw moments | low |
| Control moment slope | 12 / 12 | 1/rad | CALIBRATED_APPROXIMATION | Physical control torque | low |
| Angular damping | 20 / 20 | dimensionless | CALIBRATED_APPROXIMATION | q S L damping L/(2V); implicit numerical damping integration | low |
| Seeker full FOV | 24 / 16 | deg | ESTIMATE | Acquisition cone, not real seeker specification | low |
| Mechanical gimbal limit | 60 / 45 | deg from body axis | ESTIMATE | Geometry restriction | low |
| Seeker angular rate | 120 / 80 | deg/s | ESTIMATE | Boresight slewing constraint | low |
| Signal reference range | 8000 / 18000 | m | ESTIMATE | Unit normalized IR/RCS signal, not guaranteed range | low |
| Pre-launch lock time | 0.55 / n/a | s | ESTIMATE | Time the mounted heat seeker must hold its target before release is allowed | low |
| Acquisition dwell | 0.12 / 0.15 | s | ESTIMATE | Persistent detection dwell | low |
| Measurement coast | 0.5 / 0.7 | s | ESTIMATE | Inertial estimate after missed measurement | low |
| Active seeker activation | n/a / 12000 | m estimate | ESTIMATE | Trigger uses predicted target range | low |
| Navigation constant | 3.5 / 3.5 | dimensionless | ESTIMATE | Public PN law with bounded development gain | medium for theory, low for weapon |
| Initial heading capture | 0.6, capped at 8 | 1/s, g | CALIBRATED_APPROXIMATION | Lateral heading-error acceleration through autopilot | low |
| Maximum commanded/structural load | 35 / 30 | g | ESTIMATE | Bounds force demand and actual normal-force magnitude | low |
| Maximum requested incidence | 25 / 25 | deg | ESTIMATE | Autopilot limit; actual attitude emerges from motion | low |
| Fin travel | 25 / 25 | deg | ESTIMATE | Actuator stops | low |
| Fin rate | 180 / 180 | deg/s | ESTIMATE | Actuator rate constraint | low |
| Fuse stand-off | 7 / 9 | m from collision spheres | ESTIMATE | Swept, closing proximity geometry | low |
| Damage radius | 18 / 25 | m from aircraft hull | ESTIMATE | Quadratic radial falloff | low |
| Peak warhead damage | 180 / 230 | health points | CALIBRATED_APPROXIMATION | Existing 100-point combat health scale; not explosive kg | low |
| Arming time | 0.35 / 0.35 | s | ESTIMATE | Combined time and travel safing | low |
| Arming travel | 87.5 / 150 | m | DERIVED | Minimum launch range / 4 | high within model |
| Minimum launch range cue | 350 / 600 | m | ESTIMATE | Initial safe-employment limit | low |
| Maximum lifetime | 45 / 75 | s | ESTIMATE | Expiry bound, not a claimed intercept range | low |

The motor mass-flow approximation distributes propellant in proportion to thrust
impulse. It implies about 236 seconds of specific impulse for both motors, in the
range of reduced-smoke solid propellants; the earlier 153/161 second curves
under-delivered for their propellant mass. No real motor
measurement establishes these curves. Inertia changes with mass, while the
missile CG remains centered; burn-dependent internal CG shift and fin flutter
are omitted.

## Radar, signatures and geometry

All values in this section are `ESTIMATE`, low confidence as real-world military
performance. They are deterministic normalized gameplay coefficients.

| Radar parameter | Value | Units | Derivation |
|---|---|---|---|
| Antenna location | (4,0,0) | body FRD m | Shared development radar position |
| Azimuth / elevation limits | ±60 / ±30 | deg | Search volume |
| Beam width / full scan / revisit | 12 / 2 / 0.1 | deg / s / s | Raster azimuth scan, elevation volume coverage |
| Maximum query range | 90000 | m | Query bound; detection still requires SNR and LOS |
| Unit-RCS reference range | 55000 | m | SNR = RCS (referenceRange/range)^4 |
| Detection threshold | 1 | normalized SNR | Deterministic detection |
| Track initial confidence / increment | 0.4 / 0.25 | normalized | Detection formation and subsequent measurements |
| Confidence decay / track coast / lock coast | 0.06 / 3 / 0.5 | 1/s / s / s | Finite sensor memory |
| Track filter | 0.75 measurement, 0.25 prediction | weights | Position/velocity smoothing |
| Maximum tracks per aircraft | 16 | count | Bounded current multiplayer scope |
| Terrain LOS spacing / maximum samples | about 500 / 256 | m / count | Shared rendered/physics terrain; coarse screening |
| Earth radius in horizon check | 6371000 | m | Spherical Earth approximation (DERIVED horizon geometry) |
| IR signal aspect/power/reheat | (0.12+0.88 rear)(0.25+0.75 power)(1+3 reheat) | normalized | Inverse-square signal/range threshold |

| Aircraft | Front / side / rear RCS | Units | Category | Confidence |
|---|---|---|---|---|
| Typhoon | 3 / 12 / 5 | m2 | ESTIMATE | low |
| Su-57 | 0.15 / 3 / 1 | m2 | ESTIMATE | low |
| A320 | 20 / 60 / 25 | m2 | ESTIMATE | low |
| SR-71 | 2 / 8 / 4 | m2 | ESTIMATE | low |

RCS interpolates using squared axial viewing-direction cosine. These are broad
engineering contrasts; especially the Su-57 values must not be read as measured
stealth performance. External-store/damage RCS effects are deferred.

## Development loadouts

Each fighter has two IR-90 and two AR-157 stations. Station positions are
`ESTIMATE`, body FRD metres in the existing aircraft reference frame:

| Aircraft | IR stations | Radar stations | Confidence |
|---|---|---|---|
| Typhoon | (0,±4.3,0.5) | (-0.5,±1.5,0.8) | low; scale-consistent test geometry |
| Su-57 | (0.5,±3.5,0.4) | (0,±0.6,0.7) | low; external IR and estimated internal-bay test positions |

These are not certified pylon/bay locations or operational compatibility claims.
A320/SR-71 inventories remain empty. Compatibility bits, mounted type and station
position are reusable configuration data in `Inventory::reset`, separate from
flight-control, rendering and networking implementations. Stores use exact
point-mass position contributions to the existing payload CG/tensor architecture;
missile intrinsic carriage inertia and bay door dynamics are omitted.
