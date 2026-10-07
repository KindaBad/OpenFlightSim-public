# Building

CMake >=3.24, Python 3, Ninja, Git, C++23 compiler. Project targets disable compiler
extensions, use -Wall/-Wextra/-Wpedantic or /W4 /permissive-, and never global
-Werror. Dependency warning policies remain upstream. MSVC needs a recent
Visual Studio 2022 toolset with C++23 support (/std:c++latest where CMake chooses
it); GCC 13+ or a recent Clang is a practical starting point.

## Fedora native client

Install the compiler/build tools and X11/OpenGL development packages:

```sh
sudo dnf install gcc-c++ cmake ninja-build git python3 \
  libX11-devel libXext-devel libXcursor-devel libXi-devel \
  libXrandr-devel libXfixes-devel libglvnd-devel libasan libubsan openssl-devel protobuf-devel protobuf-compiler
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/client/ofs_client

cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/client/ofs_client
```

Three compile jobs are the preset default to keep dependency compilation memory
bounded; adjust --parallel for your machine. Linux M0 selects SDL's X11 driver,
so a Wayland session needs XWayland. bgfx owns an OpenGL 4.3+ context through EGL.
An accelerated driver is recommended. No SDL Renderer/SDL GPU/audio module is
used. Native Wayland and alternate bgfx backends are not enabled.

To use Clang, choose a fresh build directory:

```sh
cmake -S . -B build/clang -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/clang --parallel 3
ctest --test-dir build/clang --output-on-failure
```

Clang instructions are provided for compatibility; Clang was not installed on
the M0 verification host and that compiler has not been tested.

## Windows 11 / MSVC

Install Visual Studio 2022 Build Tools with Desktop development with C++, a
Windows SDK (including D3DCompiler), Python 3, CMake, Git and optionally Ninja. From an x64 Native Tools Command
Prompt, the Ninja presets above work after supplying OpenSSL and protobuf through the vcpkg
toolchain described below; those dependencies must match architecture/runtime.
The executable is `build\debug\client\ofs_client.exe`.
Alternatively use the Visual Studio generator:

```bat
vcpkg install openssl:x64-windows protobuf:x64-windows
cmake -S . -B build\msvc -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build\msvc --config Debug --parallel 3
ctest --test-dir build\msvc -C Debug --output-on-failure
build\msvc\client\Debug\ofs_client.exe
cmake --build build\msvc --config Release --parallel 3
ctest --test-dir build\msvc -C Release --output-on-failure
```

The Win32 HWND is obtained through SDL3 properties; bgfx owns D3D11. SDL3,
bgfx/bx/bimg and ImGui link statically; a separate shader folder is unnecessary.
Windows builds run pinned shaderc to produce DXBC shader-model-5 binaries and
select them for D3D11, including matching upstream ImGui shaders. Windows
compilation, fullscreen, controllers and D3D11 runtime remain unverified on this
Linux host. `.github/workflows/windows.yml` has run on GitHub's MSVC runner but has
not yet passed: its last executed build stopped on an MSVC-only `std::filesystem::path`
conversion in a test and on HLSL-reserved identifiers in `common.glsl`. Both are
fixed in source, but the fix has not been re-run on Windows. The workflow is
automatic for pushes and pull requests in the clean public source repository,
and manual-only for the private archive. It caches vcpkg binaries; a cold run
takes about 1.5 hours of Windows runner time. No passing Windows result is implied.

## Headless tests and sanitizers

```sh
cmake --preset headless
cmake --build --preset headless
ctest --preset headless

cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

OFS_BUILD_CLIENT=OFF fetches no client graphics dependencies; M2 still builds
GameNetworkingSockets. OFS_BUILD_NETWORK=OFF also omits networking. OFS_SANITIZE instruments
project targets with ASan/UBSan on GCC/Clang; it does not instrument third-party
libraries and is not offered for MSVC. Core CTest covers math conventions,
atmosphere, aerodynamics/control signs, integration/repeatability, gear/belly
contact damping, fixed-clock scheduling, origin precision, C API parity and
compilation of the API header as real C. Client builds add render-coordinate/
interpolation tests.

## Optional graphical verification

Run only in an accessible, otherwise idle desktop session. Do not run graphical
tests from separate build directories concurrently: they share desktop focus
and fullscreen state. CTest marks the graphical test RUN_SERIAL within a run:

```sh
cmake -S . -B build/debug -DOFS_CLIENT_SMOKE_TEST=ON
cmake --build build/debug --parallel 3
ctest --test-dir build/debug --output-on-failure
./build/debug/client/ofs_client --smoke-test --screenshot build/debug/client/smoke.ppm
./build/debug/client/ofs_client --frames 180 --screenshot build/debug/client/normal.ppm
```

Smoke injects SDL events to exercise free camera/relative mouse look, flight
controls, focus clearing, window resize, fullscreen twice, minimize/restore, an
actual mouse click on ImGui's airborne reset, screenshot capture and queued quit.
It uses synthetic frame elapsed time and checks movement, control sampling,
altitude after reset, simulation tick count and screenshot completion. It is not
physical input-device certification. Ordinary --frames mode uses real wall time.
Smoke captures at frame 150 after the trimmed airborne reset; ordinary mode
captures two frames before the requested limit (frame 90 without a limit) as
PPM via bgfx. --smoke-test should be used alone with --screenshot; a --frames
limit can interrupt the scripted sequence and fail its assertions.

## Dependency handling

Only graphics-enabled configuration downloads these pinned sources:

| Dependency | Revision |
|---|---|
| SDL3 3.2.20 | 96292a5b464258a2b926e0a3d72f8b98c2a81aa6 |
| bgfx.cmake | de08a6080b39994ab8a9eddb82e79e18bc3df7bd |
| bgfx submodule | 81d81fba72c42d348c589514c774bbfe01e110fa |
| bx submodule | 25315498841259323e18f549d1ad9d9aba6632cc |
| bimg submodule | 87aaad3ac882e741889fdd4263224e5d12c26f99 |
| Dear ImGui 1.91.9b | f5befd2d29e66809cd1110a152e375a7f1981f06 |
| GLM 1.0.1 | 0af55ccecd98d4e5a8d1fad7de25ba429d60e863 |

SDL3/ImGui/GLM use SHA256-verified commit archives; bgfx.cmake is fetched with
its matching Git submodules. First configure requires network access and may
take time. Sources/builds live under each build directory's _deps. bgfx examples
and unused backends are disabled; shaderc is built and its binaries are embedded.
M2 additionally fetches GameNetworkingSockets v1.6.0 at pinned commit
2cb93a06350bb065db53abdb0d87cf297e0bfd34. Jolt and miniaudio remain absent.

Optional OFS_DEPS_SOURCE_ROOT points to pre-fetched Git directories named sdl,
bgfx_cmake, imgui and glm. Their HEAD revisions must match the pins. Submodules
must also match. Generic CMake FETCHCONTENT_SOURCE_DIR_<NAME> overrides are
available for local dependency work; the developer owns any modifications made
through that override. FetchContent does not guarantee an offline initial build.
Dependency licenses remain in their source directories: SDL zlib, bgfx/bx/bimg
BSD-2-Clause, bgfx.cmake CC0, ImGui MIT, GLM MIT or Happy Bunny; transitive code
has additional notices. Bundle those notices before distribution. The vendored
PNG/JPEG decoder's MIT/public-domain notice is in `client/thirdparty/stb_image.h`.

### Shader and runtime-asset pipeline (M3.6)

The custom `ofs_shaders` target invokes `scripts/pack_shaders.py`, compiling all
five vertex/fragment program pairs with `varying.def.sc` and pinned bgfx headers.
The script embeds actual shaderc output, never hand-authored binary metadata:

- Linux: platform linux, profile 430, OpenGL GLSL binaries.
- Windows: the above plus platform windows, profile s_5_0, D3D11 DXBC binaries.
- Vulkan is not prepared; unexpected active backends fail diagnostically.

Shaders are embedded in the executable; no copied shader folder is needed. First
builds include the substantial shaderc toolchain. DXBC compilation must run on
Windows with the SDK. CMake rebuilds on shader/varying/include/script changes.
ImGui selects pinned upstream GLSL or DXBC binaries by active backend too.

An ordinary clone supplies `output/Airbus_A320.glb` and `assets/aircraft/su57/su57_lod0.glb` as
regular files. No `assets/a320.glb` symlink or Blender installation is required.
The client checks executable-relative installed assets, checkout-relative assets,
then its compiled source root. `cmake --install build/release --prefix build/package`
installs the executable and those canonical relative paths; run from any working
directory. Deploy dependency DLLs/licenses separately as required.
See [ASSETS.md](ASSETS.md) for optional Blender regeneration.

## Exact verification setup on this workstation

The host has runtime graphics libraries and a desktop but lacks devel RPMs and
passwordless sudo. No packages were installed. Development RPMs were downloaded
and extracted only into ignored .cache/sysroot. To recreate that local setup:

```sh
./scripts/prepare_fedora_headers.sh
# Optional pre-fetch; these commands create NEW ignored directories.
# Skip them if the named directories already exist.
mkdir -p .cache/deps
git clone https://github.com/bkaradzic/bgfx.cmake.git .cache/deps/bgfx_cmake
git -C .cache/deps/bgfx_cmake checkout de08a6080b39994ab8a9eddb82e79e18bc3df7bd
git -C .cache/deps/bgfx_cmake submodule update --init --recursive
```

Debug was built with all four checked-out sources via OFS_DEPS_SOURCE_ROOT.
Release tested the verified archive downloads for SDL3/ImGui/GLM and reused only
bgfx.cmake. The exact configure/build/test commands for Release were:

```sh
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_BGFX_CMAKE="$PWD/.cache/deps/bgfx_cmake" \
  -DOFS_CLIENT_SMOKE_TEST=ON \
  -DCMAKE_PREFIX_PATH="$PWD/.cache/sysroot/usr" \
  -DCMAKE_C_FLAGS="-I$PWD/.cache/sysroot/usr/include" \
  -DCMAKE_CXX_FLAGS="-I$PWD/.cache/sysroot/usr/include"
cmake --build build/release --parallel 3
ctest --test-dir build/release --output-on-failure
```

The same commands with Debug and build/debug work. For exact pre-fetched Debug
reproduction, replace FETCHCONTENT_SOURCE_DIR_BGFX_CMAKE with
OFS_DEPS_SOURCE_ROOT="$PWD/.cache/deps" after checking out all four pins.
Normal system-installed builds need none of these local-header/source overrides.

The rootless sanitizer configuration additionally used:

```sh
cmake -S . -B build/headless-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DOFS_BUILD_CLIENT=OFF -DOFS_SANITIZE=ON \
  -DCMAKE_EXE_LINKER_FLAGS="-L$PWD/.cache/sysroot/usr/lib64"
cmake --build build/headless-sanitize --parallel 3
LD_LIBRARY_PATH="$PWD/.cache/sysroot/usr/lib64" \
  ctest --test-dir build/headless-sanitize --output-on-failure
```

Build/test/runtime logs from this session are in ignored .cache. These are local
verification artifacts, not portable dependencies or source deliverables.

## M1 flight scenarios

All builds with OFS_BUILD_TESTS include the headless `ofs_flight_scenarios` runner.
Use `all`, `trim`, `controls`, `stall`, `taxi`, `takeoff`, `cruise`, `landing`,
`cycle`, `diagnostics` or `robustness` as the argument. Metrics print to stdout;
failures return nonzero. CTest registers each scenario separately, in addition
to every M0 suite. M1 Debug/Release native runs contained 21 tests; headless and
sanitizer runs contained 19. M2 adds twelve network suites, for 33 native tests
(with graphical smoke enabled) and 31 headless/sanitizer tests. See [M1_FLIGHT_VALIDATION.md](M1_FLIGHT_VALIDATION.md).

## M2 server and multiplayer client

Networking is enabled by default (`OFS_BUILD_NETWORK=ON`). GNS is pinned through
CMake FetchContent and links statically; install OpenSSL and protobuf development
libraries plus `protoc`. On Fedora:

```sh
sudo dnf install openssl-devel protobuf-devel protobuf-compiler
cmake --preset headless
cmake --build --preset headless
./build/headless/network/ofs_server --bind 0.0.0.0 --port 27020 \
  --max-players 16 --snapshot-hz 24
```

The standalone server initializes neither SDL nor graphics. Defaults: UDP port
27020, 16 clients, airborne solved trim, 120 Hz physics, 24 Hz snapshots.
Use `--ground` for braked flat-runway spawns, `--seconds N` for timed test runs.
SIGINT/SIGTERM or Ctrl+C shuts down cleanly. Physics rate is deliberately fixed.
Server addresses must be numeric IPv4/IPv6 literals. Make the chosen UDP port
reachable for remote clients; M2 does not automate port forwarding or discovery.

In separate terminals:

```sh
./build/debug/client/ofs_client --server 127.0.0.1 --port 27020 --name Alice
./build/debug/client/ofs_client --server 127.0.0.1 --port 27020 --name Bob
# Bot uses no SDL; trim controls unless an initial roll pulse is requested.
./build/headless/network/ofs_bot --server 127.0.0.1 --port 27020 --name bot \
  --seconds 60 --pulse
```

Omitting `--server` retains M1 offline mode. Online pause/reset tools are disabled;
controls/throttle/trim and free camera remain. The native network panel shows
connection, local ID, ticks, RTT, rates, bandwidth, prediction and interpolation
metrics. `--seconds N` is useful for timed graphical runs. Names are 1..64
printable ASCII bytes. No launcher, accounts, chat or matchmaking.

For Windows, an ordinary x64 vcpkg setup can provide required binary libraries:

```bat
vcpkg install openssl:x64-windows protobuf:x64-windows
cmake -S . -B build\msvc -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build\msvc --config Debug --parallel 3
ctest --test-dir build\msvc -C Debug --output-on-failure
build\msvc\network\Debug\ofs_server.exe --port 27020
build\msvc\client\Debug\ofs_client.exe --server 127.0.0.1 --name Alice
```

Follow dependency DLL deployment requirements from that toolchain. This is a
compatibility recipe, not a claim that Windows/MSVC was executed. Network source
uses standard C++/GNS APIs, no POSIX socket types, byte order helpers or OS-specific
clock calls. Upstream GNS has its own MSVC path. Windows runtime remains unverified.

## M2 automated validation and profiles

All normal CTest configurations add protocol/world/interpolation/prediction,
connection/security/limits, good/moderate/bad impairment, accelerated 420-second
world and real 180-second network soak tests. Full suites take approximately
three minutes; every existing M0/M1 regression remains registered. Active runtime
checks also execute in Release. The impairment tests exercise actual GNS loopback
IP connections, including encryption, reliability and packet fragmentation.

```sh
ctest --preset debug
ctest --preset release
ctest --preset headless
ctest --preset sanitize
# Useful short development subset, not full milestone acceptance:
ctest --test-dir build/headless -E network.soak --output-on-failure --parallel 3
./build/headless/tests/ofs_network_tests soak
./build/release/network/ofs_net_benchmark
./build/release/network/ofs_net_load
python3 scripts/profile_network_soak.py ./build/release/tests/ofs_network_tests
```

The optional RSS sampler is Linux-only tooling; the tests and game remain
portable. Capacity tools print CSV. Pure benchmark runs 7,200 ticks per aircraft
count; load tool runs 8 wall-clock seconds each with 2/8/16/32/64 actual clients.
Bot `--conditions good|moderate|bad` opts into GNS process-global development
packet impairment; it only affects sends in that bot process. Native clients and
the production server have no implicit impairment. See exact preset/rate meanings
in [NETWORK_PROTOCOL.md](NETWORK_PROTOCOL.md).

For graphical multiplayer verification, run a dedicated server and two clients
with `--network-smoke --name A` / `--network-smoke --name B`, optionally with
separate `--screenshot` paths. Each runs eight seconds with opposite scripted roll
pulses, requires live snapshots and remote rendered frames, and checks finite
state. This differs from the original offline `--smoke-test`, which stays intact.
Do not overlap graphical tests that manipulate focus/fullscreen.

Sanitizers instrument project targets, including protocol, server, client logic,
world and tests. GNS itself and system libraries are not instrumented. Sanitizer
builds turn RTTI back on for GNS C++ code so UBSan virtual-pointer checks can verify
objects across the library boundary; no project sanitizer check is suppressed.
Leak detection and halt-on-error were enabled during M2 acceptance.

On this rootless workstation the existing ignored sysroot was extended with
OpenSSL/protobuf devel/compiler RPMs (no system packages installed). Configure all
four builds with `CMAKE_PREFIX_PATH="$PWD/.cache/sysroot/usr"`, and use
`LD_LIBRARY_PATH="$PWD/.cache/sysroot/usr/lib64"` during configure, build and test
so the local protoc can load libprotoc/libprotobuf. The sanitizer linker override
from M1 still applies. `FETCHCONTENT_SOURCE_DIR_GNS="$PWD/.cache/deps/gns"` used a
local checkout at the exact documented pin. Ordinary system-installed builds
need none of these overrides. GNS is BSD-3-Clause; carry its and transitive
OpenSSL/protobuf notices when distributing binaries.


## M3 combat validation

Online fighter fire is Space / left mouse / right gamepad trigger; V toggles gun
camera. Choose `--aircraft su57`; default A320 has no weapon. Headless bots
add `--aircraft su57 --fire` for held gun input. Optional `GunConfig` lives in
the immutable aircraft definition; World/ServerConfig retain test overrides.
There is one gun, no weapon-selection UI. The default four-second
respawn is automatic and server-owned.

```sh
./build/headless/network/ofs_bot --server 127.0.0.1 --seconds 60 --aircraft su57 --fire
./build/headless/tests/ofs_combat_tests unit
./build/headless/tests/ofs_combat_tests lifecycle
./build/headless/tests/ofs_combat_tests protocol
./build/headless/tests/ofs_combat_tests abuse
./build/headless/tests/ofs_combat_tests bad
./build/headless/tests/ofs_combat_tests soak
./build/release/tests/ofs_combat_tests profile
./build/release/network/ofs_net_load --combat
python3 scripts/profile_network_soak.py ./build/release/tests/ofs_combat_tests
python3 scripts/validate_graphical_combat.py build/debug
```

CTest adds ten combat suites: native builds total 43 tests with graphical smoke
enabled; headless/sanitizer builds total 41. The real four-client combat soak runs
180 wall seconds. It alternates shooter/target roles after respawns in separated
pairs using authoritative server scenario fixtures and actual Client/Server/GNS.
No game-facing client transform assignment or claimed-hit API exists.
The fixture sets initial conditions after each life; normal 120 Hz physics runs
between them. `--combat` load profiles 2/8/16 real clients firing continuously for
eight wall seconds each; pure combat profiles run 60 simulated seconds per count.

`validate_graphical_combat.py` starts a dedicated test-fixture server on UDP 27021
and two native clients for twenty seconds. The server arranges deterministic
trimmed gun-axis encounters with alternating shooter roles after respawn;
production spawn rules are separately tested. Clients use `--combat-smoke`,
render accepted server combat events as soft effects and assert received shots,
hits, destruction, respawn and finite state. Server asserts authoritative hits/
kills/respawns. Logs/screenshots go to ignored `.cache/m3`. This requires an
accessible desktop; ordinary `--network-smoke` and offline smoke remain available.
Run desktop smoke tests sequentially across build directories to preserve focus.

M3 sanitizer acceptance uses ASan/UBSan with leak detection and halt-on-error:

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:quarantine_size_mb=16 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --preset sanitize --parallel 4
```

The fixed 16 MiB ASan quarantine bounds allocator retention for the RSS soak;
no sanitizer checks or leak detection are suppressed. The RSS stability check
starts after thirty seconds and applies to long soaks, not twenty-second tests
before allocation histories warm up. On the rootless workstation, retain the
previously documented LD_LIBRARY_PATH sysroot setting for build/test/runtime.

## M3.6 verification

Current totals are 56 native tests (including opt-in graphical smoke) and 53
headless/sanitizer tests. All historical flight/network/combat regressions remain;
new suites cover registry/civil weapon rejection, v3 type replication, animation
mapping, bounded particle cleanup, preserved GLB hierarchy and seven fighter
flight scenarios. A real five-second mixed A320/fighter GNS test verifies correct
type sampling, zero civil ammo and fighter-only authoritative shots.

```sh
./build/headless/tests/ofs_su57_scenarios all
python3 scripts/capture_m3_6.py --build build/debug
./build/release/client/ofs_client --visual-bench 16 --seconds 20
```

Capture tooling additionally requires Pillow. `--visual-scenario` fixtures freeze
physics and drive presentation at 60 Hz; they are not authoritative gameplay
evidence. `validate_graphical_combat.py` remains the separate real-server combat
check. `--visual-bench` ramps to the selected total aircraft count, mixes types/
distances, warms up one second then measures three seconds at stable count.
Keep graphical runs sequential and the desktop idle. Full measured results and
inspected evidence are in [M3_6_VISUAL_AIRCRAFT_VALIDATION.md](M3_6_VISUAL_AIRCRAFT_VALIDATION.md).

## M3.65 Typhoon and afterburner

Launch `ofs_client --aircraft typhoon`. The upper throttle range above 85% uses
reheat; dry power ends at 85%. Both engines spool independently. Multiplayer
requires protocol v4 client/server builds together. Runtime assets are now the four
`assets/aircraft/typhoon/typhoon_lod*.glb` files generated by
`scripts/typhoon_donor_import.py`; launch from the repository or installed
asset directory. The M3.65 model and its ordered modeling scripts are retired
from the runtime and kept for reference.

New native/headless totals: **67 / 64**. All original tests remain, with eight
Typhoon flight tests and three registry/asset/real multiplayer tests added.

```sh
./build/release/client/ofs_client --aircraft typhoon --airborne
python3 scripts/capture_m3_65.py --build build/release
python3 scripts/validate_typhoon_multiplayer.py
python3 scripts/profile_m3_65.py
./build/release/network/ofs_bot --aircraft typhoon --afterburner --seconds 20
```

Visual fixtures freeze simulation; `--flight-demo taxi|takeoff` instead advances
real fixed-step physics with scripted controls for reproducible native captures.
Profiles are synthetic presentation workloads, separate from real multiplayer
validation. Run graphical tools and the fullscreen smoke test sequentially.
Use the rootless LD_LIBRARY_PATH setting above where applicable. Sanitizer runs
use the documented ASAN_OPTIONS quarantine, leak detection and halt-on-error.
Measured outcomes and limitations: [M3_65_TYPHOON_VALIDATION.md](M3_65_TYPHOON_VALIDATION.md).

## M3.66 advanced physics

Current totals are85 native tests including graphical smoke,81 headless/sanitizer.
Historical milestone counts above describe their respective versions. Protocol v6
requires rebuilding both client and server. Aircraft controls are Shift/Ctrl
throttle, W/S down/up, A/D bank, Q/E rudder, G gear, F flap cycle and H airbrake.
F4 toggles HUD; Free camera retains camera movement keys.

```sh
cmake --build --preset headless
ctest --test-dir build/headless --output-on-failure --parallel 3
./build/release/tests/ofs_advanced_scenarios supersonic
./build/release/tests/ofs_advanced_scenarios engine_out
./build/release/tests/ofs_advanced_scenarios telemetry output/physics.csv
./build/release/tests/ofs_physics_benchmark
```

On this rootless host the sanitizer runtime needs the bundled sysroot:

```sh
LD_LIBRARY_PATH="$PWD/.cache/sysroot/usr/lib64" \
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:quarantine_size_mb=16 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build/sanitize --output-on-failure --parallel 3
```

Run the Debug and Release graphical smoke tests sequentially. Complete baseline,
model equations, evidence classes, aircraft measurements, CPU timing and actual
acceptance outcomes are in
[M3.66 physics validation](M3_66_ADVANCED_PHYSICS_VALIDATION.md).

## M3.67 SR-71 integration

Select with `--aircraft sr71` on the client or bot. The server selects its
immutable type4 definition; it has no gun or ammunition. Protocol v6 requires
rebuilding both endpoints. The source is `output/SR71_61-7972.blend`; four
Blender-authored GLBs live in `assets/aircraft/sr71`. No Blender installation is
needed by the runtime. `scripts/capture_m3_67.py` records native visual fixtures
and real taxi/takeoff demonstrations; `scripts/benchmark_m3_67.py` measures the
production renderer. Details and acceptance status are recorded in
[M3_67_SR71_VALIDATION.md](M3_67_SR71_VALIDATION.md).


## M3.68 Su-57 and shared realism/rendering

Choose `--aircraft su57`. No Blender is needed to run the four exported GLBs.
Since 0.4.3 they come from a Creative Commons donor through the headless
`scripts/su57_donor_import.py`; see `assets/aircraft/su57/README.md`. The other
`scripts/su57_*.py` stages belong to the retired M3.68 donor and ran inside a
live Blender MCP session.

Protocol v7 adds two authoritative nozzle actuator angles and stable type ID 5.
Rebuild client, server, tests and C API consumers together. Supplemental
`OfsPhysicsMemory` adds inlet/nozzle arrays; the legacy OfsState/Controls layout
is preserved. Core/wire SI conventions are in `docs/UNITS.md`.

The shared renderer now uses a linear RGBA16F scene and quarter-resolution bloom,
followed by one ACES-like exposure/tone-map/sRGB composite. Graphics settings
include bloom/strength, MSAA, shadows, effects, texture maximum resolution and
anisotropic sampling, plus configurable 40–100 degree vertical cockpit FOV
(default 70). Texture/anisotropy changes apply on next launch; GLB source images
are preserved. The default 2K texture cap includes complete role-aware mip chains.

Native capture script: `python3 scripts/capture_m3_68.py`; benchmark script:
`python3 scripts/benchmark_m3_68.py`. Use the existing rootless library path here.
On this workstation the extracted sanitizer .so files lacked version symlinks;
local links under `.cache/sanitizer-runtime` point to the existing sysroot files.
ASan/UBSan use that directory plus `.cache/sysroot/usr/lib64` in LD_LIBRARY_PATH,
with leak detection, halt-on-error and a 16 MiB quarantine.

The live Blender build linked OCIO 2.4 against an incompatible packaged 2.5 config.
A local validated official Blender 4.3.2 OCIO config and installed LUTs under
`.cache/blender_ocio` restore AgX. This affects authoring/inspection only, without
altering system configuration or runtime colour management.

Offline native validation uses `--flight-demo taxi|takeoff|landing|stall` with
`--aircraft su57 --frames N --screenshot result.ppm`. Demos integrate the normal
physics at fixed 60 Hz presentation; landing and stall start from solved trim.
`scripts/capture_m3_68.py` records the poses and physics demos;
`scripts/capture_m3_68_shared.py` checks the existing aircraft and shared effects.
`scripts/benchmark_m3_68.py` measures 1/2 Su-57 and 8/16/32 mixed aircraft.


## Flight and release asset validation

Import the pinned NASA archive locally before enabling the reference:

```sh
python3 scripts/import_nasa_f16.py
cmake --preset flight-validation
cmake --build --preset flight-validation
ctest --preset flight-validation
cmake --preset asset-validation
cmake --build --preset asset-validation
ctest --preset asset-validation
```

`OFS_ENABLE_F16_REFERENCE=ON` requires an intact `OFS_F16_DATA_DIR` import. Source
notices and redistribution limits are in `data/reference/f16/README.md`. The
asset preset requires all registry GLBs and authored LODs; ordinary builds retain
optional asset skips. Add `-DOFS_BUILD_NETWORK=OFF` for a dependency-free core
configuration, understanding that it excludes networking/combat tests.

For sanitizer testing use the supported `sanitize` preset plus
`-DOFS_ENABLE_F16_REFERENCE=ON`. Run with leak detection and halt-on-error. In this
milestone the existing 16 MiB combat RSS limit requires a bounded ASan quarantine:
`ASAN_OPTIONS=quarantine_size_mb=4:thread_local_quarantine_size_kb=64:detect_leaks=1:halt_on_error=1`
and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. This changes instrumentation
retention, not the RSS assertion or sanitizer error checks. The lossy
`network.bad`/`combat.bad` fixed-duration tests run alone to avoid competing CTest workloads.

`ofs_physics_benchmark --rk4` measures the optional integrator; the default run
measures Euler. Filter convergence with `ctest -L mathematical-invariant`; filter
source comparisons with `ctest -L external-aircraft-validation`. Enable
`OFS_CLIENT_SMOKE_TEST=ON` in a client build to register GPU shader conformance.
Headless checks never initialize SDL/bgfx.

## A320/Su-57 physical audit

See [AIRCRAFT_PHYSICS_AUDIT.md](AIRCRAFT_PHYSICS_AUDIT.md) for reviewed JSON
authoring, held-out performance checks and the retained takeoff discrepancy.
`ctest -R aircraft_audit` runs mechanics, geometry, approach reference, operational
diagnostics and unsteady trajectory tests. Effects → Physics geometry shows
force sites, CG, hinges, contacts and inertia axes. The Falcon is retired.
M3.68 introduced protocol 8 with aerodynamic memory; current peers require
protocol 9. Rebuild both peers together.


## M3.68.1 validation and content profiles

[M3_68_1_VALIDATION_REPORT.md](M3_68_1_VALIDATION_REPORT.md) records the measured
closure results and distinctions between reference, plausibility and regression evidence.
Generated production content remains local. `OFS_PRODUCTION_ASSET_ROOT` defaults
to the source root; a separate content pack must preserve its relative paths.

A core-only profile with a deliberately absent content root demonstrates genuine
CTest skips instead of an apparent production-content pass:

```sh
cmake -S . -B build/source-core -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DOFS_BUILD_CLIENT=OFF -DOFS_BUILD_NETWORK=OFF \
  -DOFS_PRODUCTION_ASSET_ROOT=/path/to/unavailable/content
cmake --build build/source-core -j3
ctest --test-dir build/source-core --output-on-failure --output-junit source-core.xml
python3 scripts/summarize_validation.py source-core.xml
```

For full content, configure `-DOFS_REQUIRE_PRODUCTION_ASSETS=ON` and point
`OFS_PRODUCTION_ASSET_ROOT` at the installed pack, or use the `asset-validation`
preset for local content. Missing or malformed registered models/LODs, incorrect
scale/rig contracts and omitted render content fail. Synthetic glTF/default-material
and texture/readback fixtures run without production content. A skipped asset test
is never counted as passed by `scripts/summarize_validation.py`.

The four production aircraft have 139 exported configuration parameters each.
Regenerate after changing configuration with
`python3 scripts/export_aircraft_provenance.py --binary build/headless/tests/ofs_provenance_export`;
CTest checks both runtime values and source hashes. `ctest -R 'validation|closure'`
runs scenario comparisons, load/engine/nozzle checks and Su-57 energy traces.
`ofs_physics_benchmark` and `ofs_net_benchmark` provide the recorded baselines;
CSV traces and local JUnit files are kept under ignored `output/m3_68_1/`.

On this Linux desktop native graphics tests require
`SDL_MOUSE_RELATIVE_MODE_WARP=1` to use SDL's supported relative-input fallback.
Run native graphics checks separately from heavyweight sanitizer/asset jobs to
avoid the fixed smoke-test deadline. Sanitizer settings above retain leak detection
and ASan/UBSan halt-on-error. Windows/D3D11 and a fresh Blender export were not
verified on this Linux host. Exhaust haze remains a translucent schlieren effect;
true scene refraction is a later renderer task.

## M3.7 replication validation

Rebuild both networking peers for protocol 9. Live snapshots are per-client,
quantized/acknowledged deltas in application chunks up to 1,100 bytes. The normal
input redundancy depth is 4 commands (150 bytes). The legacy full-projection
serializer remains a local diagnostic used by historical state regressions.

```sh
./build/release/network/ofs_replication_benchmark all
./build/release/network/ofs_replication_benchmark moderate
./build/release/network/ofs_replication_benchmark poor
./build/release/network/ofs_replication_benchmark soak
ctest --test-dir build/headless -R 'replication\.' --output-on-failure
./build/release/network/ofs_net_load
```

For separate server and peer processes, start the server load driver first and
then the peer driver in another terminal (the server runs eight real seconds):

```sh
./build/release/network/ofs_net_load --server 27020 64
./build/release/network/ofs_net_load --peers 27020 64
```

The load CSV preserves its historical physics timing columns and adds complete
server tick mean/p95/p99/max, per-publication construction/transport stages and
observed pending transport bytes. Complete tick timing includes accumulated input
polling, authoritative simulation, reliable events and phased snapshot dispatch.

The deterministic driver uses production physics, prediction, replication and wire
parsers, advancing simulated time without sleeps. It reports clustered/distributed
bandwidth, packet percentiles, stage timings, bounded memory, impairment recovery
and 180 simulated seconds of mixed-aircraft combat/AOI soak. Existing network and
combat tests retain their separate 180 real-second GNS soaks. See
[M3.7 validation](M3_7_NETWORKING_VALIDATION.md) for actual outcomes and limitations.

## Native launcher and release packaging

The simulator now exports a build-generated launcher catalogue and version
metadata. See [LAUNCHER.md](LAUNCHER.md) for the separate Windows/Linux Qt Widgets
launcher, security tests, release workflow, approved asset-pack requirements,
manifest publication and update/repair/rollback operations. Source launching
requires PySide6 Essentials; frozen releases include their own Qt/Python runtime.
