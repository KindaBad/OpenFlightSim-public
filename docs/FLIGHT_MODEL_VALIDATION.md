# Flight-model validation contract

Current closure measurements and evidence limits are in [M3_68_1_VALIDATION_REPORT.md](M3_68_1_VALIDATION_REPORT.md). Current A320/Su-57 authoring, source evidence, geometry checks and limits are in [AIRCRAFT_PHYSICS_AUDIT.md](AIRCRAFT_PHYSICS_AUDIT.md). Earlier measured results below are historical; the Falcon is retired and its wire ID is rejected.
This milestone adds a data-driven, independently checkable flight-model path to the existing double-precision 6-DOF simulator. The four production registry aircraft remain engineering surrogates. NASA reference-model agreement is computational model validation, not evidence of agreement with flight tests of a real F-16. Passing a maneuver regression does not establish aircraft fidelity.

## Evidence categories

| CTest label | What a pass establishes | What it does not establish |
|---|---|---|
| `mathematical-invariant` | Analytic inertia, interpolation, force purity, conservation and numerical convergence properties | Correct aircraft data |
| `standards-reference` | Existing ISA checkpoints and focused glTF conformance fixtures | Complete certification against every provision of a standard |
| `external-aircraft-validation` | Agreement with published aircraft/engine anchors or independently published NASA DAVE-ML checkData; case metadata identifies dependent anchors | A validated real-aircraft flight envelope |
| `regression-plausibility` | Determinism, stability, maneuver behavior, networking, combat, existing bounds and provenance consistency | Independent aerodynamic/engine/FCS validation |
| `asset-conformance` | Asset dimensions, coordinate transforms, rig channels, materials and LODs satisfy the registry contract | OEM geometry accuracy or redistribution permission |

`client.shader_conformance` additionally renders the shipped shaders into an offscreen target. Its pixel assertions check masked shadows and front/back lighting, including an authored tilted normal map. It needs a working GPU/display and is optional in headless configurations.

## Provenance

`data/provenance.schema.json` defines records with parameter, values, SI unit, source, revision, configuration, validity envelope, confidence, validation status and notes. Origins mean:

- `REFERENCE`: traceable public data or a published computational model; publication alone does not establish flight-test validity.
- `DERIVED`: an identified computation from identified inputs; uncertainty follows those inputs.
- `ESTIMATE`: an engineering assumption without independent validation, including unavailable data represented by a surrogate.
- `CALIBRATED_APPROXIMATION`: a reconstructed law or curve selected against behavioral targets rather than independent physical evidence.

`data/physics/{a320,su57}.json` now authors the reviewed geometry, high-lift
schedule, mass distribution and configuration assignments. CMake generates the
compiled C++ in the build tree. `data/aircraft/{a320,typhoon,sr71,su57}.json`
exports 139 configured parameters per aircraft, including engine/force sites and
unsteady constants. `scripts/export_aircraft_provenance.py --check` detects
stale values/source hashes. The A320 has parameter-specific published anchors;
other values remain derived/estimated as recorded. Shared engine, aero and
reconstructed FCS constants live in `data/models/engineering-fallback.json`.
See the current audit before interpreting the historical truth table below.

The NASA importer writes the same record schema locally, preserves original source notices, extracts the independent checkData separately, and records import integrity hashes. No runtime metadata lookup occurs in a force evaluation.

## Continuous and discrete responsibilities

World axes remain NED; body axes remain FRD; the normalized attitude quaternion rotates body to world. The shared core still serves client, server, trim and C API.

| Component | Location and responsibility |
|---|---|
| Atmosphere/wind | `atmosphere.cpp`, `environment_models.cpp`: existing ISA and deterministic harmonic gust approximation |
| Mass properties | `mass_properties.cpp`, `inertia.hpp`: consumables/payload CG and full symmetric tensor, including parallel-axis products |
| Raw airframe | `aerodynamic_model.cpp`: named `EngineeringAeroModel` fallback or immutable `AerodynamicModel` dataset |
| Propulsion | `propulsion_model.cpp`: named `EngineeringJetEngineModel` fallback or `PropulsionModel`/`EngineDeckModel` |
| Continuous derivatives | `dynamics_evaluation.cpp`: `evaluateContinuous(snapshot, controls, weather)` computes free-flight forces/moments and derivatives without advancing simulator memory |
| FCS/allocation | `flight_control_system.cpp`, `control_allocation.cpp`: commanded surfaces, protection, pressure-dependent authority/allocation, position/rate bounds |
| Integration | `dynamics.cpp`: continuous Euler/RK4; `simulator.cpp`: stepping, actuator/spool memory, fuel updates, gear/contact impulses and diagnostics |

These are source boundaries and callable evaluation interfaces; atmosphere, mass, gear and FCS have not all been turned into polymorphic plug-ins. Landing-gear/contact handling stays in the existing stepping layer. Pure evaluations read actual surfaces/power from the supplied state. They never update actuators, fuel, damage, failures, timers or contact impulses. Environmental callbacks must themselves be deterministic/read-only.

The inertia representation uses actual symmetric matrix entries: `[Ixx Ixy Ixz; Ixy Iyy Iyz; Ixz Iyz Izz]`. Aerospace positive *products of inertia* therefore require negation when placed in off-diagonal entries. Positive definiteness uses normalized Sylvester checks; solves use Cholesky with finite positive pivots. Existing Ixy=Iyz=0 models remain supported. Payload/fuel translation applies all parallel-axis terms.

## Data-model contracts

`GridTable` accepts 1–13 strictly increasing, finite tensor-grid axes with shape-checked finite values. The final axis varies fastest. Multilinear interpolation is deterministic. Boundary policy is explicitly clamp or reject; there is no extrapolation. `TableAeroModel` can compose CX/CY/CZ/Cl/Cm/Cn terms over alpha, beta, Mach, normalized rates, elevator, aileron, rudder, leading/trailing devices, speed brake and configuration. Symmetry is a model-specific decision, never a generic assumption.

Physical control angles are radians. Body rates are normalized as `p*b/(2V)`, `q*c/(2V)`, `r*b/(2V)`. Coefficients become forces with qbar*S and moments with qbar*S*(b,c,b); moments shift from the dataset reference to current CG. The engineering path retains distributed sites and local `omega cross r` airflow. A global reference coefficient model uses the supplied aerodynamic reference moments directly.

`EngineDeckModel` interpolates thrust over altitude, Mach and normalized power. A separate `running` flag defines stopped (zero thrust/flow), running idle, dry/intermediate, and afterburning regimes. Optional fuel-flow tables use the same axes and SI units. The legacy state name `n1` is retained for compatibility: in the engineering model zero is explicitly running idle, with health/fuel availability determining stoppage; in the deck path it is normalized power, not measured compressor RPM. NASA's idle table can contain negative net thrust; these source values are retained.

Raw engineering control response no longer contains the generic high-q square-root softening. FCS response uses qbar-dependent authority to command actual surfaces, so protection/scheduling is distinct from the airframe force law. Su-57 allocation solves bounded regularized least squares by deterministic projected coordinate descent, with position and per-substep rate bounds in the solve. Its columns remain local linear estimates, and the solver is not a reconstruction of the real aircraft's FCS.

## Reference aircraft and reproducibility

Primary source: [NASA NESC 2015 flight-simulation checkcases](https://nescacademy.nasa.gov/flightsim/2015), [model overview](https://nescacademy.nasa.gov/flightsim/2015/bodies), and the [F-16 model archive](https://nescacademy.nasa.gov/workshop/FlightSim/2015/models/F16_package.zip). See `data/reference/f16/README.md` for exact revisions/hashes and redistribution policy.

```sh
python3 scripts/import_nasa_f16.py
# Or use --archive /path/to/F16_package.zip; the same SHA-256 is mandatory.
python3 scripts/import_nasa_f16.py --verify
cmake -S . -B build/flight-validation -DOFS_BUILD_CLIENT=OFF \
  -DOFS_ENABLE_F16_REFERENCE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/flight-validation -j3
ctest --test-dir build/flight-validation --output-on-failure
```

Network-enabled headless builds require OpenSSL/protobuf and GameNetworkingSockets. For an independent core-only build add `-DOFS_BUILD_NETWORK=OFF`; this deliberately excludes network tests. The `flight-validation` preset also enables the reference. `OFS_F16_DATA_DIR` can point to another local import directory. Enabling the reference without an intact local import fails configuration.

The reference config is available as `f16ReferenceConfig()` rather than as a new network aircraft ID. It has no production GLB or source gear/contact model. It exercises source static aero, sideslip, controls, damping/rate terms, full inertia, thrust deck, SI force reconstruction, trim, and RK4. The 16 original aerodynamic shots yield 96 independent coefficient comparisons; nine original propulsion shots compare thrust. Source expected values and source tolerances are copied, never generated by OpenFlightSim. Each comparison prints expected/actual, absolute/percentage error and tolerance; percentage error is undefined at a zero reference.

The source aerodynamic tables cover alpha -10 to 45 degrees and beta -30 to 30 degrees; elevator table boundaries are +/-24 degrees. Table boundaries clamp explicitly. Source control normalization uses 20-degree aileron and 30-degree rudder anchors, and linear control terms remain source-defined (one check intentionally exceeds the aileron anchor). Only static Cl/Cn use source-described odd beta symmetry. The runtime reference rejects non-clean configurations and Mach outside 0–1; the source aero has no explicit Mach dependence, so this admission range does not establish a validated transonic envelope. Engine deck bounds are 0–15,240 m, Mach 0–1, power 0–1, with military power at 0.5. Outside deck limits, table clamp policy applies.

No high-AoA vortex additions, supersonic correction, fuel-flow estimates or real F-16 FCS are added. Source-model checks validate equations, not real unsteady flight physics. Spool/actuator timing remains estimated; inventory uses an explicit 1 kg bookkeeping marker with zero consumption to preserve the source fixed mass. Gear contact is disabled. Local trim at 150 m/s, 3048 m is a derivative-residual check, not an independent flight-validation point.

## Integration accuracy and limits

`Simulator::setIntegrator(ContinuousIntegrator::RungeKutta4)` selects RK4 for free-flight rigid-body variables. Euler remains the default for compatibility. RK stages normalize the quaternion and reevaluate continuous forces at their stage state/time. Actuator, engine and fuel memory advances once per substep and is held fixed during RK stages. Contact proximity/active compliance selects the original contact stepping path. Contact impulses are never fed to RK4 as smooth derivatives.

Fourth-order accuracy is established for the smooth, frozen-memory continuous problem, **not** for the entire actuator/fuel/contact split simulation. The full model still has first-order split/discrete components and a maximum internal substep of 1/240 s. The convergence suite compares torque-free motion with analytic invariants, analytic ballistic position/velocity, and a fine-step numerical reference; a second smooth frozen-airframe case uses all continuous engineering forces. A fine-step numerical trajectory establishes numerical convergence only, not independent aircraft correctness. Energy is conserved in the torque-free case; the forced airframe energy is compared with the fine reference because thrust/drag do work.

`step(dt)` consumes the whole valid requested duration rather than capping it at 0.5 s; durations needing more than INT_MAX substeps throw. C++ `setState` returns a rejection flag, and `ofs_try_set_state`/`ofs_try_step` expose detectable C results while legacy void wrappers remain. Invalid state rejection is atomic. Nonfinite generic clamps throw instead of hiding corrupted physics; external controls/weather have a separate sanitization boundary.

## Historical realism truth table (pre-M3.68.1)

The current parameter inventory and corrections are in the closure report and mass/engine audit. The following table describes the earlier model.

E = engineering estimate (may include historic tuning); D(E) = derived from estimates; P = primary published source; X(P) = checked against independent published *computational* source cases. None of these production aircraft has independent flight-test validation in this milestone. Dimension/rig checks are conformance to our own targets. Original 'published' code comments have not been upgraded to verified evidence.

| Aircraft | Geometry | Mass | CG | Inertia | Aero | Engine | FCS | Gear | High-AoA | Thrust vectoring |
|---|---|---|---|---|---|---|---|---|---|---|
| A320 surrogate | E; asset conformance | E | E | E | E | E | E | E | E heuristic | Absent; no claim |
| Retired temporary Falcon | Removed from registry/assets/source | — | — | — | — | — | — | — | — | Not a production aircraft |
| Typhoon surrogate | E; asset conformance | E | E | E | E | E | E | E | E heuristic | Absent; no claim |
| SR-71 surrogate | E; asset conformance | E | E | E | E | E | E | E | E heuristic | Absent; no claim |
| Su-57 AL-41F1-era surrogate | E; anisotropic art normalization | E | D(E) | D(E); assumed zero products | E | E | E bounded allocation | E | E heuristic | Force mechanics verified; limits/axes/rates E |
| NASA F-16 reference config | P reference area/span/chord; no asset | P source fixed mass | P source MRC/default | P source tensor | X(P) static/rate checkData | X(P) thrust checkData; spool E | Direct estimated actuators; real FCS absent | Unavailable; contacts disabled | P finite alpha table; no unsteady-flight claim | Absent; no claim |

Shared ISA has standards checkpoints. Gravity is the existing constant-g approximation. Gusts are sums of deterministic sines, **not a Dryden stochastic process**. Flat/local terrain contacts, damage-to-health mappings, fuel/engine dynamics, component aero separation, transonic wave drag and vortex breakdown remain engineering approximations unless a parameter-specific primary source says otherwise.

## Assets and contained correctness

Ordinary developer builds report absent generated production assets as CTest SKIPPED with an explicit NOT RUN message, not passed. `OFS_PRODUCTION_ASSET_ROOT` selects a separate content root. `cmake --preset asset-validation` enables `OFS_REQUIRE_PRODUCTION_ASSETS`; every registry model and authored LOD becomes mandatory. The conformance executable validates all four aircraft's length/span/height targets, nose orientation, model/body convention, CG anchors, parked gear plane, rig channel names, material references, finite transforms and authored/generated LODs. A policy test proves missing models fail required mode and skip optional mode. These checks do not assert OEM surface shape accuracy.

The Su-57 authoring scripts accept project/source/working/output/report arguments. The normalization report records independent length/span factors and induced deformation. Licence status remains unresolved. This milestone checked Python syntax and existing exported GLBs, but did not rerun the Blender modeling/export process.

Small corrections are covered by direct fixtures: sparse-only accessors have zero bases; accessor reads obey buffer-view bounds; unsupported required extensions and UV sets fail explicitly; authored tangents reach the render mesh; alpha-mask shadow cutoff and double-sided backface normals have GPU pixel checks; long steps, invalid state, nonfinite clamp and bounded debug-force formatting have core regressions.

## Next milestone

Obtain permission for a redistributable reference corpus and add independently published trim/trajectory checkcases across an explicitly bounded envelope, including source atmospheric/integrator conventions. Add finite-difference linearization and system-identification outputs to the pure evaluator. Then integrate actuator/spool ODEs with a documented split scheme and convergence tests for the coupled model. Require source-backed uncertainty and error budgets before upgrading aircraft claims. Su-57 data collection should precede additional maneuver tuning.
