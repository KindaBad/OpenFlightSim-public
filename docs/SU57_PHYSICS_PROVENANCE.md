# Su-57 physics provenance — M3.68.1

This is an AL-41F1-era **engineering surrogate**, not a reconstruction of a
classified Su-57 aerodynamic deck or control law. SI values below are runtime
values (dimensions from the shared compiled geometry dataset); vectors are body
FRD metres unless dimensionless. Rounded display values link to full precision
in [the runtime manifest](../data/aircraft/su57.json) and [authoring records](../data/physics/su57.json).

- **REFERENCE**: verified public source anchor. No Su-57 numerical parameter here currently meets that standard.
- **DERIVED**: calculated from explicitly estimated component mass/geometry or SI mathematics; does not imply measured aircraft accuracy.
- **ESTIMATE**: chosen engineering assumption without a verified parameter-specific external source.
- **CALIBRATED_APPROXIMATION**: reconstructed curve/control response qualified by internal behavior checks, not OEM flight-data calibration. Shared nonlinear curves are annotated in [engineering-fallback.json](../data/models/engineering-fallback.json).

The [UAC aircraft page](https://www.uacrussia.ru/en/aircraft/lineup/military/su-57/)
identifies the aircraft and program; no retrieved parameter-specific OEM numerical
deck is attributed to it. Popular-looking dimensions/thrust remain estimates.
No classified data, internet-video maneuver target, or manufactured citation is used.

| Parameter | Runtime value | Units | Classification | Source/derivation | Confidence |
|---|---|---|---|---|---|
| `length` | 20.1 | m | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `height` | 4.6 | m | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `wing_area` | 78.8 | m2 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `wing_span` | 14.1 | m | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `mac` | 5.5 | m | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `empty_mass` | 18500 | kg | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `mass` | 25100 | kg | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `fuel_capacity` | 10300 | kg | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `initial_fuel` | 6500 | kg | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `initial_payload` | 100 | kg | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `ixx` | 67071.137 | kg m2 | DERIVED | Component masses + variances + parallel-axis theorem | Derived from estimates |
| `iyy` | 328905.38 | kg m2 | DERIVED | Component masses + variances + parallel-axis theorem | Derived from estimates |
| `izz` | 388964.88 | kg m2 | DERIVED | Component masses + variances + parallel-axis theorem | Derived from estimates |
| `ixz` | 4648.6961 | kg m2 | DERIVED | Component masses + variances + parallel-axis theorem | Derived from estimates |
| `fuel_position` | -0.25, 0, 0.05 | m | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `payload_position` | 4, 0, -0.4 | m | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `fuel_inertia_per_kg` | 5, 12, 17 | m2 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `payload_inertia_per_kg` | 0.02, 0.02, 0.02 | m2 | DERIVED | Component masses + variances + parallel-axis theorem | Derived from estimates |
| `cl_alpha` | 3.8 | 1/rad | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `cl_max_clean` | 1.5 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `cl_max_full_flap` | 1.85 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `alpha_crit_clean` | 0.41887902 | rad | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `cd0_clean` | 0.022 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `oswald_e` | 0.75 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `mach_drag_onset` | 0.88 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `mach_drag_peak` | 0.04 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `mach_drag_supersonic` | 0.02 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `cm_alpha` | -0.22 | 1/rad | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `cm_de` | -1.1 | 1/rad | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `cm_q` | -16 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `cl_da` | 0.22 | 1/rad | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `cn_beta` | 0.1 | 1/rad | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `cn_dr` | -0.12 | 1/rad | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `cy_beta` | -0.6 | 1/rad | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `pitch_arm` | -6.6 | m | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `pitch_span` | 3.15 | m | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `levcon_lift_share` | 0.048 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `levcon_control_share` | 0.25 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `levcon_max` | 0.34906585 | rad | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `vortex_lift` | 1.15 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `unsteady_alpha_tau` | 0.12 | s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `unsteady_detach_tau` | 0.18 | s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `unsteady_attach_tau` | 0.55 | s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `unsteady_vortex_tau` | 0.25 | s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `unsteady_alpha_dot_gain` | 0.12 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `unsteady_beta_gain` | 0.35 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `response_time` | 0.38 | s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `max_pitch_rate` | 0.75 | rad/s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `max_roll_rate` | 3.1 | rad/s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `alpha_limit` | 0.61086524 | rad | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `g_positive` | 9 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `g_negative` | -3 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `actuator_rate` | 4 | 1/s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `flap_rate` | 0.25 | 1/s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `spoiler_rate` | 1.7 | 1/s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[0].position` | -7.85, -1.34, 0.16 | m | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[0].direction` | 1, 0, 0 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[0].nozzle_pivot` | -6.4, -1.34, 0.16 | m | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[0].dry_thrust` | 93000 | N | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[0].reheat_thrust` | 147000 | N | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[0].vector_axis` | 0, 0.8660254, 0.5 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[0].vector_limit` | 0.26179939 | rad | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[0].vector_rate` | 1.0471976 | rad/s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[0].spool_seconds` | 0.8 | s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[0].dry_tsfc` | 2.2e-05 | kg/(N s) | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[0].reheat_tsfc` | 5.1e-05 | kg/(N s) | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[1].position` | -7.85, 1.34, 0.16 | m | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[1].direction` | 1, 0, 0 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[1].nozzle_pivot` | -6.4, 1.34, 0.16 | m | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[1].dry_thrust` | 93000 | N | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[1].reheat_thrust` | 147000 | N | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[1].vector_axis` | 0, 0.8660254, -0.5 | 1 | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[1].vector_limit` | 0.26179939 | rad | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[1].vector_rate` | 1.0471976 | rad/s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[1].spool_seconds` | 0.8 | s | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[1].dry_tsfc` | 2.2e-05 | kg/(N s) | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| `engines[1].reheat_tsfc` | 5.1e-05 | kg/(N s) | ESTIMATE | Engineering configuration; no retrieved OEM value | Low / unverified externally |
| Maximum takeoff mass | No enforced runtime MTOW parameter | kg | ESTIMATE / unavailable | No validated operational load envelope | Unknown |

Engine exit spacing is **2.68 m**, with application stations X=-7.85 m,
pivot stations X=-6.4 m, Z=.16 m. These are shared visual/physical surrogate
geometry estimates. L/R vector axes are canted by +/-30 degrees; nozzle limits
15 degrees, rates 60 degrees/s. Shared leading/trailing surface hinges and force
sites come from the authored geometry reconstruction, not measured OEM pressure
centres or drawings. Every hinge is individually recorded in the authoring JSON.

The reference 25,100 kg tensor uses 15,300 kg structure, two 1,600 kg engines,
6,500 kg fuel, 100 kg payload. Structure covariance is (5.29,1.6384,.2025) m²;
engine covariance (.81,.0784,.0784), fuel (12,5,0), payload (.01,.01,.01).
The structure centroid balances component first moments at the reference origin.
Tensor symmetry zeros are consequences of the assumed distribution, not measured
zeros. Fuel and payload change mass, CG, and tensor about actual CG.

FCS desired pitch/roll/yaw response and load/AoA soft limits request aerodynamic
surface and finite-rate nozzle movement through the allocator. Fixed allocator
gain/coupling formulas in `flight_control_system.cpp` remain reconstructed
approximations: no claim of proprietary Su-57 control-law accuracy. The 35-degree
AoA and +9/-3 g settings are soft command protections, not rigid rate/state clamps
or an externally validated operational envelope.

`closure.vectoring` verifies force geometry, moments `r × F`, engine failure/starvation,
load/CG changes, nozzle rendering state, and the Euler moment equation. Existing
trim/control/turn/recovery/landing suites and the new high-G/post-stall/low-speed
traces prove numerical behavior and energy accounting; **none validates an exact
Su-57 Cobra, departure boundary, or flight envelope**.

See [mass/engine audit](MASS_ENGINE_AUDIT.md), [asset provenance](ASSET_RELEASE_PROVENANCE.md),
and [closure measurements](M3_68_1_VALIDATION_REPORT.md).
