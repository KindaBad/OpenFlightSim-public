# Data-driven flight-model milestone results

Current A320/Su-57 authoring, source evidence, geometry checks and limits are in [AIRCRAFT_PHYSICS_AUDIT.md](AIRCRAFT_PHYSICS_AUDIT.md). Earlier measured results below are historical; the Falcon is retired and its wire ID is rejected.
Recorded 2026-10-03 on the supplied workspace. All production aircraft remain engineering surrogates; the new NASA path establishes agreement with an independent published computational model. The [validation contract](FLIGHT_MODEL_VALIDATION.md) contains the per-aircraft/subsystem truth table and repeatable commands.

## 1. Architecture summary

The original double-precision Newton–Euler NED/FRD simulator is retained. Pure snapshot force/derivative evaluation now supports immutable multidimensional CX/CY/CZ/Cl/Cm/Cn datasets and altitude/Mach/power engine decks alongside explicitly named engineering fallbacks. Full symmetric inertia uses validated Cholesky solves, including loading-dependent products. RK4 advances free-flight rigid-body variables with normalized stage quaternions; the existing stepper retains actuator/spool/fuel memory and contact impulses. FCS is a separate component, with bounded position/rate allocation for vectoring aircraft. Euler remains the default. Wire aircraft IDs are unchanged; rebuild C++ consumers for the changed config/API types.

## 2. Important files changed or added

All paths below are repository-relative.

| Group | Files |
|---|---|
| New public dynamics/data interfaces | `core/include/ofs/{data_model,inertia,dynamics,control_allocation,f16_reference}.hpp` |
| Public compatibility/evaluation changes | `core/include/ofs/{aircraft,simulator,math}.hpp`, `core/include/ofs/c_api.h` |
| New implementation components | `core/src/{data_model,dynamics,control_allocation,f16_reference,environment_models,aerodynamic_model,propulsion_model,mass_properties,dynamics_evaluation,flight_control_system}.cpp`, `core/src/flight_model_detail.hpp` |
| Stepper and compatibility updates | `core/src/{simulator,c_api,su57}.cpp` |
| Machine-readable provenance | `data/provenance.schema.json`, `data/aircraft/{a320,typhoon,sr71,su57}.json`, `data/models/engineering-fallback.json`, `data/geometry/su57-normalization.json`, `data/reference/f16/README.md` |
| Import/export audit tooling | `scripts/{import_nasa_f16,export_aircraft_provenance}.py` |
| Loader/render corrections | `client/src/{gltf,mesh}.{cpp,hpp}`, `client/src/renderer.cpp`, `client/shaders/{pbr_vs,pbr_fs,shadow_vs,shadow_fs}.glsl`, `client/shaders/varying.def.sc` |
| New verification | `tests/{foundation_tests,f16_validation,gltf_conformance_tests,asset_conformance,shader_conformance}.cpp`, `tests/asset_policy_tests.py` |
| Existing test/benchmark updates | `tests/{scenario.hpp,flight_scenarios.cpp,physics_benchmark.cpp}`; high-q expectations/test-pilot formulas corrected to reflect raw aerodynamic response; pass tolerances retained |
| Portable Su-57 pipeline | `scripts/su57_{paths,normalize,session,rig,cockpit,correction,geometry_detail,export,lod_inspection}.py`, `assets/aircraft/su57/README.md` |
| Configuration | `CMakeLists.txt`, `CMakePresets.json`, `core/CMakeLists.txt`, `client/CMakeLists.txt`, `tests/CMakeLists.txt` |
| Documentation | `README.md`, `docs/{ARCHITECTURE,BUILDING,FLIGHT_MODEL_VALIDATION,FLIGHT_MODEL_RESULTS}.md` |

No source aircraft binary was rewritten. The user's untracked audit ZIP is untouched. Imported NASA source/data/header files stay local and ignored; checked-in code contains no copied table/checkData corpus.

## 3. Exact external sources

Primary flight reference: [NASA NESC 2015 checkcases](https://nescacademy.nasa.gov/flightsim/2015), [NASA model definitions](https://nescacademy.nasa.gov/flightsim/2015/bodies), and [F16_package.zip](https://nescacademy.nasa.gov/workshop/FlightSim/2015/models/F16_package.zip).

Archive SHA-256: `20c60f615ae8e87d81c9d98b54fff45a2832840201499cbcfe3f45a60ef3e5b2`. Aero DML is Mod P dated 2013-10-21; propulsion is the initial version dated 2012-08-07. Individual file hashes are in `data/reference/f16/README.md`. Local importer output contains 35 source parameter/table records, the original DML files, separately extracted source checks/tolerances and an integrity manifest. Source DML defaults resolve the web/DML CG and mass discrepancy.

Rendering contract: [Khronos glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html). Backface normal-map basis behavior was checked against [Khronos Sample Renderer material_info.glsl](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/main/source/Renderer/shaders/material_info.glsl). GPU fixtures exercise the shipped shaders rather than copying expected pixels from the implementation.

## 4. Validation errors and convergence

| Independent reference check | Count | Maximum absolute error | Maximum percentage error | Tolerance |
|---|---:|---:|---:|---|
| NASA aero staticShot coefficients | 96 across 16 shots | 4.21884749e-15 coefficient units | 1.60315561e-11% for nonzero references | Original 1e-6 per coefficient |
| NASA propulsion staticShot thrust | 9 shots | 0.00253088 N | 8.1415e-6% | Unchanged per-source tolerances: 4.44822e-5 N at exact corners; 0.00444822 / 0.00266893 N for the two rounded interior reference outputs |

The interior engine residuals are consistent with the rounded source expected outputs and pass the source tolerances. The case with maximum absolute error has 6.11863e-6% error; the other interior case has the maximum percentage error. Zero-reference coefficient percentages are explicitly undefined. Individual tests print every comparison, source tolerance and absolute/percentage error.

Local mathematical trim at 150 m/s and 3048 m converged: alpha approximately 3.57093 degrees, normalized elevator trim 0.0266416 and power 0.108093. Translational/angular derivative residuals are below 1e-7 in their SI derivative units. These trim results are solver verification, not independent trim data.

| Smooth convergence case | Steps refined | Observed order | Finest-step errors |
|---|---|---|---|
| Full-tensor torque-free attitude/rates plus analytic ballistic translation, 2 s | 0.1, 0.05, 0.025, 0.0125 s | 3.9972, 3.9980, 3.9988 | omega 2.40154e-12 rad/s; quaternion difference 2.63110e-11; rotational energy drift 5.21627e-12 J; world angular momentum drift 1.46411e-10 kg m2/s; ballistic position/velocity within 1e-11 SI |
| Frozen engineering airframe, 0.64 s, forces reevaluated at each stage | 0.08, 0.04, 0.02, 0.01 s | 4.03218, 4.01595, 4.00565 | position 1.06329e-11 m; velocity 1.09189e-10 m/s; quaternion difference 4.07338e-12; omega 9.39170e-12 rad/s; reference energy difference 1.14441e-5 J |

Fine numerical references use 1/8192 s and 0.64/16384 s respectively. They are convergence comparators, never external aircraft evidence. The forced airframe does not conserve energy; its energy is compared with the fine trajectory. Coupled actuator/fuel/contact stepping is not claimed to converge at fourth order.

## 5. Benchmark before/after

Same Intel Core Ultra 7 255H host, Linux x86-64, GCC Release builds, existing benchmark with 3000 120-Hz steps and the first 100 samples discarded for production/mixed cases. Single-run host scheduling and background build/sanitizer activity affect these measurements; no cross-aircraft speedup claim is made.

All values are mean microseconds per aircraft step unless labeled otherwise. Baseline measured before this milestone; final values follow source refactoring and constrained allocation.

| Case | Baseline Euler | Final Euler | Final RK4 |
|---|---:|---:|---:|
| A320 | 8.378 | 5.347 | 10.982 |
| Fighter | 5.456 | 6.640 | 12.032 |
| Typhoon | 4.831 | 6.235 | 11.851 |
| SR-71 | 4.910 | 6.276 | 11.893 |
| Su-57 | 4.553 | 17.209 | 22.693 |
| Mixed 64 aircraft, total | 303.684 | 517.191 | 881.396 |
| Mixed 64 aircraft, per aircraft | 4.745 | 8.081 | 13.772 |
| NASA table aero + engine path | Unavailable | 1.884 | 6.970 |

Mixed-64 p95/p99 totals: baseline 388.880/394.237 us; final Euler 679.979/691.526 us; final RK4 1028.816/1046.525 us. The deterministic bounded allocator accounts for much of Su-57's added work. Mixed-64 mean remains below 1 ms per 120-Hz simulation tick on this host, though this is not a server end-to-end capacity measurement. No physics was simplified to meet a speculative performance target.

An earlier post-change sample yielded 7.678 us Euler and 13.727 us RK4 per aircraft in the mixed-64 case; final measurements illustrate host/run variance. The NASA benchmark measures the full reference stepping path, not just an isolated lookup.

## 6. Tests executed

| Configuration/check | Result |
|---|---|
| Original dependency-free Release baseline | 76/76 passed, 7.43 s |
| Final dependency-free Release with NASA reference | 87/87 passed, 9.51 s |
| Network-enabled headless Release with NASA reference | 127/127 passed, 212.70 s, including network/combat 180 s soaks |
| Final finite-sum guard, glTF tangent import and provenance checks after last incremental edits | 3/3 passed |
| Mandatory asset-validation Release, core-only | 3/3 asset-category tests passed, 5.54 s; all five production GLBs and required LODs checked |
| ASan/UBSan Debug, network + NASA reference | 127/127 passed, 363.48 s; no ASan/UBSan/leak reports |
| Actual GPU shader fixtures, OpenGL | Shadow transparent/opaque pixels 0/255, excessive cutoff 0; front/back 79/79; tilted normal-map front/back 79/79; passed |
| Native client coordinate/CG tests | Passed |
| Native client/shader compilation | Passed |
| Su-57 authoring Python syntax and whitespace checks | Passed; Blender pipeline not rerun |

Measured mandatory assets (length/span/height, meters): A320 37.570000/35.799995/11.770000; fighter 15.150000/9.860001/4.880000; Typhoon 15.960000/10.950000/5.285380; SR-71 32.740601/16.941799/5.638800; Su-57 20.100002/14.100000/4.600000. These agree with project dimension targets within the declared 0.15 m tolerance; they are not OEM measurements.

Early concurrent runs exposed fixed-wall-clock lossy network/combat flakes and provenance mismatches while manifests were still changing. The lossy `network.bad`/`combat.bad` cases now run alone under CTest, without changing counts, deadlines or physics tolerances. The final full Release run passes. An initial sanitizer soak exceeded the pre-existing 16 MiB RSS limit with ASan's default large quarantine; the final run uses 4 MiB global/64 KiB thread-local quarantine while preserving leak detection, halt-on-error and the existing RSS assertion. No test tolerance was relaxed.

The source-command invalid-state and debug-name fixes are covered directly. Sparse-only zero bases, view bounds, required-extension rejection, explicit UV handling and tangent import/mesh preservation have independent hand-built glTF fixtures. GPU fixtures check mask cutoff and complete double-sided mapped normals. Matrix, interpolation, bounded allocation, purity and long-duration tests use analytic properties, not simulator-generated golden constants.

## 7. Remaining approximations

Production airframes retain generic engineering separation/stall/vortex, Mach/control-effectiveness, wave-drag and engine density/Mach/inlet laws. Gravity, gear compliance/friction, damage, actuator/spool dynamics and split contact stepping remain approximations. Gusts are deterministic harmonic sums rather than Dryden turbulence. The source F-16 computational model has no supplied fuel-flow, gear or real FCS dataset; local source agreement does not supply these missing models. Boundary clamping is explicit but does not validate behavior outside an envelope.

## 8. Su-57 unknowns

Independent aerodynamic polars/control derivatives, high-AoA unsteady behavior, authoritative mass distribution/CG/inertia products, validated AL-41F1 deck/fuel-flow/spool behavior, real FCS/allocation/protection laws, nozzle hinge/deflection/rate limits, structural/actuator loads, landing-gear geometry/compliance and OEM geometry remain unknown. Existing mechanics remain, labeled as engineering estimates. Art redistribution permission remains unverified.

Normalization of the supplied visual geometry is anisotropic: original length/span 17.1699962616/12.3671278954 m, target 20.1/14.1 m. Longitudinal/vertical scale 1.1706467313, transverse scale 1.1401192030; +17.064673% length/height, +14.011920% span, -2.607749% span relative to uniform length scaling, volume factor 1.562435 (approximately +56.24%). This quantifies imposed art deformation, not OEM accuracy. `data/geometry/su57-normalization.json` records the current baseline calculation; future authoring runs also write the exact deformation JSON.

## 9. Deliberately unavailable data

No Su-57 measured tensor products, engine maps, classified FCS or internet-maneuver targets were invented. No NASA Mach/device/high-AoA extrapolation correction, fuel-flow estimate, real F-16 FCS or borrowed contact geometry was presented as published reference data. No historic production value was promoted to published status merely because an old code comment called it published. No redistribution permission was assumed for either source archive or supplied Su-57 art. Missing reference data stays missing, with explicit operational/configuration limits.

## 10. Recommended next milestone

Acquire a redistributable reference corpus with independent trim and time-history checkcases, define the full atmospheric/configuration envelope and source uncertainty, and validate trajectory/linearization outputs from the pure evaluator. Improve coupled actuator/spool integration with convergence evidence before changing the default integrator. Extend aircraft datasets only as traceable primary evidence becomes available; do not tune the Su-57 to arbitrary maneuver videos or unsourced claims.
