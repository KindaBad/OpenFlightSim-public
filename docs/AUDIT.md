# Foundation / Native Client Pivot audit

Audit date: 2026-09-29, before implementation. Fedora 44, GCC 16.2.1,
CMake 4.1.0, Ninja 1.13.2. No applicable AGENTS.md.

## Repository baseline

Git is on unborn `master`: no commits, no remote, every source/asset untracked.
No changes were discarded, staged or committed. Ignored files include Blender
backup, render logs, check images and Python caches; these are preserved.
No existing C++ build directories were present. `godot/`, `extern/`, `docs/`
and `tests/` contain no files. Baseline status and source checksums plus the
original asset README are saved locally in ignored `.cache/audit/`.

Default CMake configuration fails because tests/CMakeLists.txt is absent.
With OFS_BUILD_TESTS=OFF the entire static C++17 core compiles under GCC.
OFS_BUILD_GDEXT points at a nonexistent extension. Global Unix warning and
sanitizer flags are unsuitable for MSVC. No application or simulation tests exist.

## Existing architecture and quality

`ofs_core` is already headless: double precision vector/quaternion math, NED
world/FRD body frames, configurable A320, pilot Controls, Weather, State,
Instruments, debug forces, ISA atmosphere and plain C wrapper. State contains no
GPU, SDL or UI data. RenderOrigin already supports subtraction in doubles.

Simulator integrates translation semi-implicitly and normalized attitude with
body angular velocity and diagonal inertia/gyroscopic coupling. step(dt) splits
into <=1/240-second substeps. Aerodynamics includes lift, induced/configuration/
wave drag, angle of attack, sideslip, longitudinal/lateral stability, damping,
control moments, positive/negative stall approximation and ground effect.
Engines have throttle/spool lag, density/Mach thrust lapse and application-point
moments. Gear has spring/damper, steering and tire/brake friction. Instruments
include TAS/IAS, approximate CAS, Mach, attitude, altitude, vertical speed, stall
warning and load factor. Gusts are deterministic sine sums, not a true Dryden model.

Concrete defects: quaternion helpers negate nose-up pitch despite +Y rotation
in FRD rotating forward toward -Z; rudder mapping produces left yaw for positive
pedal; spring forces subtract down-positive contact speed, creating anti-damping
on compression (gear and belly); AGL subtracts ground Z instead of altitude;
NaN controls survive clamp; infinite dt is accepted as 0.5 seconds.
Flight coefficients and landing behavior have no calibration evidence; high-alpha
and reverse flight are approximate. No trim/autopilot, collision world, fuel,
systems, multiplayer or gameplay exists. Determinism across compilers is unproven.

## Assets and tooling

Editable Blender scene, aircraft-only GLB, four delivery PNGs, dimension report,
reference images/PDF, seven modeling/export/validation scripts and shell launchers
exist. Read all scripts and report/validation logs. Report says 37.57 m length,
35.80 m span, 11.76 m height. Logged evaluated span is 35.762 m, within the
validator's 0.1 m tolerance. Validation log reports PASS; this is historical
validation, not a rerun. Some modeling scripts hardcode this workstation path
and fonts; sequential scripts share a Blender namespace and the final scene
includes manual refinements. Export lacks a runtime LOD/material/animation pipeline.
OCIO compatibility files and original report are preserved. Blender axes and
nose-origin differ from simulation; see COORDINATES.md. Binary art and references
were inventoried; no new art export or image/reference revalidation is required
for the native client pivot. Original asset documentation is preserved in ASSETS.md.

## Implementation plan

1. Preserve core and complete assets. Deprecate nonexistent Godot CMake option.
2. C++23, target-scoped portable warnings/sanitizers, meaningful dependency-free
   core CTest suite and explicit headless configuration.
3. Correct proven convention/contact bugs with regressions; retain force model.
4. Add reusable bounded 120 Hz accumulator, exposing interpolation and dropped time.
5. Separate client: SDL3 native handles/input/controller lifetime, bgfx world and
   ImGui SDL platform/custom bgfx backend, free camera, CPU state interpolation.
6. Reuse existing origin helper; subtract global doubles before GPU floats. Start
   with inexpensive identifiable aircraft geometry; defer 52 MB GLB integration.
7. Pin client dependencies, document Linux/Windows builds and architecture.
8. Build Debug/Release, run headless regressions, sanitizer checks and native
   graphical smoke/visual checks. Record unverified paths and remaining debt.
