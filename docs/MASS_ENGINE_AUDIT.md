# M3.68.1 mass and engine audit

All simulation mass is **kg**, tensor entries **kg m²**, and positions **m** in
body FRD: X forward, Y right, Z down. Positions use the reference loaded-CG
origin. `massProperties(state)` returns the current CG displacement and the
symmetric tensor about that CG. The matrix XZ entry is the **negative** of an
aerospace product of inertia. See [COORDINATES.md](COORDINATES.md) and
[UNITS.md](UNITS.md).

| Aircraft | Basic mass and inertia evidence | Load model | Limitations |
|---|---|---|---|
| A320-214 | 42,000 kg basic mass ESTIMATE; structure + two 2,400 kg installed engine allocations; structure covariance (38,17,1.6) m²; DERIVED tensor | Fuel covariance (35,20,0), payload covariance (30,.8,.25) m², distinct load centroids | Not a manufacturer weight-and-balance sheet; basic mass includes an approximate operational configuration |
| Typhoon | 11,000 kg basic mass ESTIMATE; 9,000 kg structure + two 1,000 kg engines; structure covariance ((.20 length)²,(.16 span)²,(.10 height)²); engine covariance (.64,.09,.09) m²; DERIVED tensor | Fuel covariance (6,2.5,0) m² at X=-.25 m; payload treated as a point | Replaces unsupported fixed 20,500/115,000/132,000 kg m² moments, independently of FCS gains; no certified tank capacity/load envelope enforced |
| SR-71 | NASA 60,728 lb zero-fuel baseline and 220,660/954,850/1,172,039 slug ft² moments; product Ixz=19,200 slug ft²; REFERENCE baseline, DERIVED loaded tensor | Estimated fuel covariance (49,9,0) m², centroid (-.45,0,.05) m; reference fuel 20,000 kg | NASA test-aircraft baseline, not every fleet configuration; fuel-burn tank sequence is not reproduced |
| Su-57 | 18,500 kg basic mass ESTIMATE; structure + two 1,600 kg engine allocations; DERIVED tensor | Estimated fuel covariance (12,5,0), payload covariance (.01,.01,.01) m²; distinct centroids | No OEM inertia or load sheet; see [SU57_PHYSICS_PROVENANCE.md](SU57_PHYSICS_PROVENANCE.md) |

The SR-71 source is [NASA/TP-2002-210718](https://ntrs.nasa.gov/api/citations/20020057965/downloads/20020057965.pdf),
Table 2 baseline flights 37–44. Equations 13/14 establish the product sign.
M3.68.1 fixes both the XZ sign and the missing translation of the basic airframe
from its zero-fuel CG to the reference loaded origin. Four external source/units
checks recover that zero-fuel tensor after removing fuel. These are agreement
with published model anchors, not independent trajectory validation.

The A320/Su-57 payload covariance already contributed to their reference tensors,
but the old load update did not remove/add that distributed contribution as
payload changed. `payload_inertia_per_kg` now applies the three covariance sums,
in addition to point/parallel-axis terms. Fuel already had this mechanism.

`closure.mass` samples 27 load/offset combinations per aircraft, checks tensor
symmetry, positive definiteness, principal-moment triangle inequality, radii of
gyration, smooth 0.1 kg fuel increments, and independent payload shape increments.
Reference CGs, principal axes, and loaded moments remain available in diagnostics.
MTOW and certified flight CG limits are not universal runtime constraints; do not
interpret acceptance of a state as approval of an operational load sheet.

## Propulsion approximation

Engineering engines calculate thrust from density/Mach lapse, spool power,
continuous afterburner state, engine health and fuel availability. Thrust vectors
are rotated about each engine's nozzle axis and applied at their actual exits.
Fuel flow is thrust times interpolated TSFC. The SR-71 additionally models an
approximate inlet-spike target and pressure recovery, not a complete J58 cycle.
A320 uses an engineering CFM56 power/pressure/Mach map; its `n1` is normalized
power, not independently validated compressor RPM.

| Aircraft | Public anchor | Off-design curve and response |
|---|---|---|
| A320 | CFM56-5B4/P rated 120,100 N, REFERENCE | Pressure exponent .75, Mach factor 1-.45M+.10M², power exponent 2.5, TSFC 1.65e-5 kg/(N s), 2 s spool ESTIMATE / CALIBRATED_APPROXIMATION |
| Typhoon | EJ200 60/90 kN dry/reheat, REFERENCE; nominal TSFC bands 21–23/47–49 g/(kN s) | Runtime band midpoints 22/48 g/(kN s), .5 density exponent, .22 ram gain, .65 s spool; approximate curves, no installed engine deck |
| SR-71 | NASA identifies 34,000 lbf thrust-class J58s in the technical report | .60 density exponent, piecewise Mach ram curve and inlet recovery, 1.7 s spool, 2.65e-5/5.4e-5 TSFC; ESTIMATE |
| Su-57 | 93/147 kN dry/reheat ESTIMATE, AL-41F1-era surrogate | .60 density exponent, .23 ram gain, .8 s spool, 2.2e-5/5.1e-5 TSFC; ESTIMATE |

Primary rating sources: [EASA E.003 issue 06, III 6.1 page 12](https://www.easa.europa.eu/sites/default/files/dfu/TCDS%20EASA%20E.003%20issue%2006.pdf),
[EUROJET technical fact sheet](https://www.eurojet.de/wp-content/uploads/EUJ_Factsheet_A4_Ansicht.pdf).
NASA's [FS-030 fact sheet](https://www.nasa.gov/wp-content/uploads/2021/09/495839main_FS-030_SR-71.pdf)
instead quotes 32,500 lbf: the runner retains a **WARN** against that approximate
nominal figure. The technical report supports the existing 34,000 lbf thrust
class; neither source establishes the full installed/off-design curve.

`closure.engines` writes `output/m3_68_1/engine-curves.csv`: 2,020 stabilized
samples per aircraft at 0/5/11/20 km, Mach 0/.5/.9/1.5/3, throttle 0..1.
Supersonic A320 samples are numerical robustness diagnostics outside its envelope.
Tests verify power monotonicity, nonnegative flow, failed-engine zero thrust/flow,
left/right independence, continuous AB activation, and non-instantaneous spool.
The CSV calls these curves CALIBRATED_APPROXIMATION; agreement at a rated anchor
must not be interpreted as validation of every curve point. Integration/fuel-burn
regressions remain in `advanced.fuel` and aircraft-specific engine suites.
