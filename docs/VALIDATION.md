# Validation reports

Current realism, validation and packaging closure:
[M3_68_1_VALIDATION_REPORT.md](M3_68_1_VALIDATION_REPORT.md), including exact
profile counts, sanitizer results, physical/vectoring regressions, production
content measurements and the baseline for M3.7. Public-source agreement,
mathematical checks and plausibility are reported separately.

Current M1 measurements, corrections, flight-cycle scenarios and build results:
[M1_FLIGHT_VALIDATION.md](M1_FLIGHT_VALIDATION.md).
Multiplayer findings: [M2_MULTIPLAYER_VALIDATION.md](M2_MULTIPLAYER_VALIDATION.md).
Combat findings: [M3_COMBAT_VALIDATION.md](M3_COMBAT_VALIDATION.md).
Visual and aircraft asset pass, including the render-frame and CG-anchor
derivation, the screenshot set and the performance measurements:
[M3_5_VALIDATION.md](M3_5_VALIDATION.md).

The following is the retained historical M0 report; its untrimmed-reset and
uncalibrated-model limitations describe the M0 state.

# M0 verification report

Verified 2026-09-29 on Fedora 44 Workstation, GCC 16.2.1, CMake 4.1.0,
Ninja 1.13.2, XWayland on a Wayland desktop, Intel/Mesa accelerated graphics
(Mesa 26.2.3). Native backend reports OpenGL 4.3; driver supports OpenGL 4.6.
Windows/MSVC and Clang results are not claimed.

## Before work and audit

Unborn master, no commits or remote, all project files untracked. No source or
assets discarded, no commit/staging/reset/clean performed. Existing ignored
Blender backups, check renders, logs and Python caches remain. The original asset
README was copied verbatim to ASSETS.md before updating README. Baseline status
and source/asset SHA256 checksums are saved in .cache/audit. Blender, GLB and all
original asset scripts were checked against that baseline after implementation
and match exactly. Delivery images, OCIO configuration and references were not
modified. See AUDIT.md for the full inventory and implementation plan.

Default CMake failed on missing tests/CMakeLists.txt. Headless core compiled
when OFS_BUILD_TESTS was OFF. Godot extension option referred to nonexistent
code; there was no frontend or simulation test implementation to preserve.

## Preserved, changed and added

Preserved existing math/value types, A320 configuration, atmosphere, lift/drag/
thrust/gravity, stalls, angular dynamics, engines, gear/friction, instruments,
deterministic gusts, render origin, C API and the entire visual asset pipeline.

Corrections with regression coverage:

- Nose-up Euler pitch had been negated contrary to FRD geometry and angular
  integration. Helpers now use the standard positive +Y pitch; C API follows it.
- Positive rudder pedal had produced negative yaw. Surface mapping now matches
  the documented positive-right pilot convention.
- Gear and belly damping subtracted sinking velocity and therefore opposed
  compression with less force. Both now add down-positive compression velocity.
- AGL uses ground Z minus aircraft Z. Current zero-height runway behavior is
  unchanged; the general sign is corrected.
- Nonfinite control scalars become neutral, nonfinite dt is ignored, and setState
  normalizes attitude and clears stale force diagnostics. C creation returns null
  on allocation failure rather than allowing bad_alloc to cross the C boundary.

CMake now uses C++23, scoped compiler warnings, optional project sanitizers,
headless tests and pinned client dependencies. Missing Godot build option is
retired with an explanatory error; no existing Godot files were deleted.

Added SDL3 native Platform/Input, hotplug-ready gamepad mapping, free camera,
bgfx static world/placeholder rendering, Dear ImGui diagnostics and controls,
120 Hz fixed accumulator and interpolated rendering, lightweight logging,
optional screenshot/smoke tooling, CMake presets and actual-implementation docs.
No networking, Jolt, audio or deferred gameplay was added.

Important sources: CMakeLists.txt, CMakePresets.json, cmake/Dependencies.cmake,
core/include/ofs/fixed_step.hpp, core/include/ofs/math.hpp,
core/include/ofs/simulator.hpp, core/src/simulator.cpp, core/src/c_api.cpp,
client/CMakeLists.txt, client/src/{main,platform,input,renderer,debug_ui}.cpp,
client/src/{camera,coordinates}.hpp, tests/* and docs/*.md.

## Build and test results

| Configuration | Compilation | Automated result |
|---|---|---|
| GCC Debug, native client | Pass | 11/11 CTest tests, including graphical smoke; 3.39 s |
| GCC Release, native client | Pass | 11/11 CTest tests, including graphical smoke; 3.36 s |
| GCC Debug, headless | Pass | 9/9 core tests |
| GCC Debug, ASan + UBSan headless | Pass | 9/9 core tests; 0.31 s; no sanitizer diagnostics |
| Fresh dependency fetch configuration | Pass | Default dependency fetching, no source overrides; 105.9 s |

Debug used pinned pre-fetched sources. Release fetched SHA256-verified SDL,
ImGui and GLM archives and reused pinned bgfx.cmake. A fresh build/fetch-check
configuration separately fetched all default dependencies, including the full
bgfx.cmake/submodule chain, and configured successfully without source overrides.
The already-built Debug/Release paths were sufficient; a third full compile of
identical sources was not required. Local system-header prefix overrides are
recorded in BUILDING.md. Headless tests fetch nothing.

Tests use explicit runtime checks in Release as well as Debug (not disabled
assert macros). Coverage includes geometric attitude signs/round trips, ISA
layer continuity/reference values, lift/drag/control signs/post-stall behavior,
flaps, 120-second finite/repeatable flight evolution, quaternion normalization,
spool convergence, gravity, invalid input/time, contact damping and 60-second
gear settling, fixed clock/render-frequency-independent physics, bounded stall
catch-up, billion-meter coordinate offsets, C/C++ parity and real C header use.
Client math tests check handedness, local transforms and quaternion hemisphere
interpolation. Same-build repeatability does not certify cross-compiler determinism.

Own targets compiled without warnings in the final incremental builds. The full
Release dependency build emits one SDL const-qualification warning under GCC 16;
GLM emits CMake compatibility deprecation warnings. Upstream code was not patched
merely to hide them. Initial missing system development/sanitizer libraries were
resolved through local extracted RPMs, without changing installed packages.
Compiler/API/backend configuration errors found during development were corrected.
Two concurrently launched graphical suites interfered through desktop focus and
failed; rerunning sequentially passed both. Graphical tests are marked RUN_SERIAL
and documentation forbids simultaneous runs from separate build directories.

## Runtime evidence

Debug and Release each passed the graphical smoke test sequentially:

- Native resizable SDL3 window and real OpenGL initialization.
- Free camera translation and right-mouse relative look via SDL events.
- Arrow-key aircraft control sampling, focus-loss state clearing.
- Window resize, two successful fullscreen switches, minimize/restore.
- Mouse click on the actual ImGui airborne reset button; final altitude about
  997.8 m, finite state, and 338 executed 120 Hz simulation ticks.
- GPU screenshot callback writes a PPM; queued SDL quit and clean teardown.

Both configurations also ran an ordinary real-clock 180-frame application session
and shut down cleanly. Captured frames were visually inspected: visible perspective
transport-aircraft placeholder, sky, ground/grid/runway and readable ImGui UI.
A normal Debug frame reported about 119.7 measured ticks/s at about 59 FPS,
interpolation alpha 0.459 and no dropped simulation time; this is one observation,
not a performance benchmark. The smoke time source is synthetic; the ordinary
run confirms the real steady-clock path.

Local screenshots: build/debug/client/{smoke,normal}.ppm and
build/release/client/{smoke,normal}.ppm. PNG conversions were used for inspection;
a representative final Release frame is written to
docs/images/m0-native-client.png, which is a generated artifact and is not
version controlled.
These show renderer/UI output, not physical controller or Windows certification.

Debug startup logs include a missing optional RenderDoc library, a successful
EGL context retry without debug flags, and unsupported compressed-texture format
capability probes on Mesa. The M0 renderer uses supported vertex colors/RGBA font
textures; these probes did not prevent rendering. Release startup is clean.

No known critical crash remains in the tested path. Unsupported/missing GPU
backends fail startup; unrecoverable bgfx failures log and abort. Device loss and
bad external State/Weather/config values are not comprehensively handled.

## Architecture, controls and remaining limits

ofs_core has no SDL, GPU, ImGui or GLM dependency. The native client alone owns
platform input, camera and GPU resources. Simulation State is plain numeric data,
advanced at 120 Hz with retained 240 Hz internal substeps. Client rendering uses
previous/current interpolation and double origin subtraction before GPU floats.
This supports a future headless authoritative server but includes no wire protocol.

Controls: WASD camera, R/F vertical, Shift faster, hold right mouse to look,
Home frames aircraft; arrows pitch/roll, Z/C rudder and low-speed nosewheel,
Page Up/Down throttle, G simulated gear, B wheel brakes; ImGui parking brake,
pause/resets/throttle/flaps/spoilers; F11 fullscreen, Escape/window close quits.
Optional gamepad left X/Y maps roll/pitch, right X rudder with deadzone. Actual
physical mouse/keyboard feel and controller hotplug still need player testing;
scripted SDL event-path testing is not a hardware-input certification.

Flight model remains a functional, approximate configurable A320-style model,
not calibrated handling. Airborne reset is explicitly untrimmed. ISA clamps
altitude to -500..20,000 m; CAS is effectively IAS; high-alpha/reverse flow,
contact attitude/friction and belly safety-plane behavior are simplified. No
collision world, crash response, fuel, autopilot or aircraft systems.

Other limitations/debt: Windows and Clang validation, native Wayland, GLB import/
LOD/material/node transform/CG alignment, animated gear/control surfaces, world
chunking, smooth horizon grid appearance, device-loss recovery, explicit serial
state format and external-state validation. Blender scripts retain workstation
paths/fonts and some manual refinements; historical asset validation was read,
not rerun or regenerated. Static ground is finite, not terrain or Earth content.

Core and world rendering add no per-frame dynamic allocations or locks; world
buffers are uploaded once, small transforms submitted each frame. ImGui retains
allocations and uses transient buffers; screenshot conversion allocates only on
request. No ECS/inheritance framework or performance benchmark was added.

Recommended M1: establish measured trim and control-response acceptance at
64,000 kg, 1,000 m and 110 m/s, then add sustained taxi/takeoff/landing scenarios.
Verify MSVC/D3D11 and physical controllers early. Preserve/tune existing dynamics
based on those measurements before starting M2 networking.
