# NASA NESC F-16 local reference import

Authoritative entrypoints: [2015 NASA NESC checkcases](https://nescacademy.nasa.gov/flightsim/2015), [body/model definitions](https://nescacademy.nasa.gov/flightsim/2015/bodies), [original F16_package.zip](https://nescacademy.nasa.gov/workshop/FlightSim/2015/models/F16_package.zip).

The retrieved archive contains Eugene Morelli copyright notices; a general redistribution grant was not established. This repository includes import tooling and equations, **not copied tables or checkData**. Review the original notices for your intended use. All imported originals, compiled tables and independent expected values stay under the ignored local `.cache/reference/f16` directory.

| Source | Revision | SHA-256 |
|---|---|---|
| F16_package.zip | NASA 2015 checkcase archive, retrieved 2026-10-03 | `20c60f615ae8e87d81c9d98b54fff45a2832840201499cbcfe3f45a60ef3e5b2` |
| F16_aero.dml | Mod P, 2013-10-21 | `82796577b989b5f3becb7d13430ff6c619b3ecd2e642a6427b743658fb177e51` |
| F16_prop.dml | Initial version, 2012-08-07 | `e5fd7772e5c05cdac666c94febbe4cb50139418b26045affd92dbd198f2215c6` |
| F16_inertia.dml | As supplied in pinned archive | `e11d4eba5b158d24db7034bd85682ad0b896dd2f3172f007ca3d6d6d4e693c4a` |

The importer rejects a changed archive hash. Use `python3 scripts/import_nasa_f16.py` or supply `--archive /path/to/F16_package.zip --output /path/to/local/import`. The import contains original DML notices, 35 parameter/table provenance records, 16 aerodynamic staticShot cases, nine propulsion staticShot cases and a compiled header. Tables and checkData are extracted independently; no OpenFlightSim evaluation generates expected values. Source angular/dimensional units are converted to SI. Original checkData also remains in its original units for review. `--verify` checks generated/original files against the import manifest; a changed local import must be regenerated/reviewed. Hashes detect accidental changes, not authenticity against an attacker who also edits the manifest.

Source conflicts are resolved explicitly: the web overview gives CG=25% MAC and mass=637.26 slug, while DML defaults give CG=35% MAC and mass=637.1595 slug. This implementation uses the DML configuration and the source moment reference of 35% MAC; it does not mix the web overview with the source checks. Off-diagonal aerospace products are negated into symmetric matrix entries.

The reference is computational validation only. Aero is the source subsonic nonlinear/static model, without invented Mach or device corrections. Gear, fuel flow, real FCS, unsteady aero, production geometry and source-validated spool timing are unavailable here. Runtime contacts are disabled; no A320 contact geometry is treated as F-16 data. See `docs/FLIGHT_MODEL_VALIDATION.md` for the complete model boundary contract and test commands.
