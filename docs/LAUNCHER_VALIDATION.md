# Launcher validation — 6 October 2026

Implementation was tested locally on Linux. Windows runners are configured in
the release workflow but no Windows runtime/CI success is claimed.

## Results

- All **55 launcher tests pass** using the isolated PySide6 Essentials environment.
  The system-Python run passes the same security/release tests, with five optional
  Qt widget tests skipped. The final CTest `launcher.security_config` registration
  passes. The first broad run detected an empty Python executable in that CMake
  test registration; root-level Python discovery fixed it, and it was rerun.
- The complete Release build succeeds, including simulator, server, compiled
  catalogue, embedded shaders and existing test targets. `ofs_client --version`
  reports 0.3.0 with the CMake-generated commit identity.
- A client/network-disabled core build exports all four registry aircraft and
  **only Free Flight**, proving capabilities are tied to the compiled build.
- Across the broad run and serial follow-up, **211 of 212 CTest checks pass**.
  Physics, networking, production asset checks, combat, live multiplayer, timing/
  soak checks, graphical missile combat, graphical dogfight and shader conformance
  pass. The optional `client.graphical_smoke` hits its 45-second desktop timeout.
- To investigate that optional failure, the original `HEAD` client main.cpp was
  compiled separately with the unchanged current renderer/core/dependency
  libraries. Its smoke also fails: focus and UI-reset checks are false and the
  screenshot is not captured. The current client with a reduced graphics config
  reports a window restore failure. This desktop's focus/minimize/restore
  behavior is not certified by this run; no simulator logic/test assertions or
  timeouts were weakened to turn that failure into a pass.
- Source and frozen Qt GUI applications run and produce launcher screenshots.
  Optional Qt tests exercise all ten sections, registry aircraft selection,
  presets/custom persistence, recommendations, mode-dependent controls, launch
  handoff and asynchronous worker completion.
- Real local-development release archives are produced; public packaging is
  separately tested to refuse missing or incorrect hash-bound asset approval.
  Initial/update ZIPs, individual repair files and update descriptors validate.
- The actual **frozen bootstrap executable** activates a downloaded-cache package
  with a verified file tree and then rolls back successfully. Both retained trees
  match all expected hashes. Unit tests additionally exercise a failed pointer
  write, journal recovery, damaged-candidate recovery and failed selective repair.
- Release-workflow YAML and both native matrix entries parse successfully.
  `git diff --check` and Python compile checks pass.

The local validation bundles are deliberately labelled LOCAL-DEVELOPMENT-ONLY;
they contain the existing local assets and are not public release authorization.
All generated bundles, models, ZIPs, logs, screenshots and baseline objects remain
in ignored `build/` or `.cache/` directories. The unrelated audit ZIP is preserved.

## Ease-of-use follow-up — 6 October 2026

Tested on Fedora 44 (GNOME, Wayland). Nothing here was run on Windows.

- All **60 launcher tests pass** in the PySide6 environment; the system-Python
  run passes with six optional Qt tests skipped.
- `./play.sh` opens the launcher from this checkout in under a second once
  prepared. In a clean copy without a build it created the environment,
  installed PySide6 and attempted the release build, then stopped with the
  package list because this host has no system development headers.
- A source-build simulator started outside the checkout root could not find its
  aircraft models after the checkout was moved; the launcher now starts
  source builds from their asset root, and a 20-frame run loads all models.
- A freshly frozen, local-development install ZIP was extracted into a folder
  with a space in its name and started with isolated XDG directories: the
  bootstrap wrote a `desktop-file-validate`-clean menu entry and started the
  packaged launcher, which discovered the installation.
- A separate server-only build installed `lib/libprotobuf.so.30` and an
  `$ORIGIN/lib` run path; `ldd` resolves protobuf from the package. The full
  client install and the Ubuntu-built package on Fedora were not run here.
- The Windows Start Menu shortcut (PowerShell `WScript.Shell`), `play.bat` and
  the Visual Studio build path in `scripts/play.py` are untested.

## Commands

The preconfigured Release tree uses the workstation's existing pinned graphics/
network dependencies and local header/runtime paths documented in BUILDING.md.
No dependencies or models were added to Git.

```sh
python3 -m venv .cache/launcher-venv
.cache/launcher-venv/bin/pip install -r launcher/requirements.txt
cmake --build build/release --parallel 3
build/release/client/ofs_client --version

.cache/launcher-venv/bin/python -m unittest discover -s launcher/tests -v
python3 -m unittest discover -s launcher/tests -v
ctest --test-dir build/release -R '^launcher.security_config$' --output-on-failure

ctest --test-dir build/release --output-on-failure --parallel 3 \
  -E 'graphical|shader_conformance|dogfight_smoke|network.bad|combat.bad'
ctest --test-dir build/release --output-on-failure --parallel 1 \
  -R 'graphical|shader_conformance|dogfight_smoke|network.bad|combat.bad|^launcher.security_config$'

cmake -S . -B build/launcher-core-only -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DOFS_BUILD_CLIENT=OFF -DOFS_BUILD_NETWORK=OFF -DOFS_BUILD_TESTS=OFF
cmake --build build/launcher-core-only --target ofs_launcher_catalog --parallel 3

cmake --install build/release --prefix build/launcher-stage
.cache/launcher-venv/bin/python scripts/collect_release_notices.py \
  --build build/release --output build/launcher-stage --deps .cache/deps
.cache/launcher-venv/bin/python scripts/package_launcher.py \
  --build build/release --output build/launcher-final-bundles
.cache/launcher-venv/bin/python scripts/package_release.py \
  --simulator build/launcher-stage --bundles build/launcher-final-bundles \
  --output build/launcher-delivery-release \
  --base-url https://updates.example.org/openflightsim --local-development \
  --notes docs/LAUNCHER_RELEASE_NOTES.md

.cache/launcher-venv/bin/python -m launcher.app --installation "$PWD" \
  --user-data .cache/launcher-ui-data --screenshot .cache/launcher-ui.png
```

Frozen GUI validation runs the release's `launcher/ofs_launcher` with its managed
installation root, isolated `--user-data`, and `--screenshot`. Frozen-helper
validation supplies a `.pending-update.json` generated from that release's actual
descriptor and its matching `.downloads/` ZIP, then runs `OpenFlightSim
--installation ROOT --apply REQUEST --no-restart` and `--rollback --no-restart`.
The updates.example.org URL in local packaging is never used as a live update
endpoint: local-development packages leave automatic publisher defaults blank.

## Publication prerequisites

An actual publisher HTTPS host, approved model/LOD pack, reviewed dependency
notices/matching source offers, Windows testing and optional code/release signing
remain external release prerequisites. No public CDN, Windows runtime result,
cryptographic release signatures or permission to redistribute the current
unverified Su-57 donor is implied. See LAUNCHER.md and LAUNCHER_DEPENDENCIES.md.

# Standalone setup validation — 2026-10-06

The new setup has a single Install Game action, a writable per-user default
folder, download progress/speed, cancellation/resume and a persistent installed
bootstrap/Start Menu target. It opens the full launcher after installation.
Release packaging emits a standalone setup executable and verifies that its
embedded publisher configuration matches the packaged game.

Local Linux verification:

- `.cache/launcher-venv/bin/python -m unittest discover -s launcher/tests -v`:
  **75 tests passed**, including all nine Qt widget tests. New tests cover first
  install, offline reopen, corrupt downloads, actual HTTP Range resume, cancelled
  download/extraction, interrupted first pointer commit/retry, insufficient disk
  space, installation leases, wrong platforms/channels, minimum launcher version,
  publisher mismatch, persistent shortcut/spawn paths, retry and closing during
  background work. Existing update, repair, rollback and archive safety tests pass.
- The system Python run passes the 66 engine tests and skips the nine optional
  Qt tests. Fresh CMake configuration at `build/setup-ctest` passes
  `launcher.security_config` through CTest with the launcher venv Python.
  The pre-existing `build/release` CTest cache references the checkout's previous
  location; it was not used as acceptance evidence.
- Frozen launcher, bootstrap and one-file setup built successfully with pinned
  PyInstaller 6.19.0 / PySide6 6.10.2 using `scripts/package_launcher.py`.
  The frozen setup exits successfully after rendering an offscreen screenshot
  at `build/setup-smoke/setup.png`, with no Python CLI needed to start it.
  This verification bundle uses an example HTTPS endpoint and is local only.
- Release YAML parses successfully. CI now requires the frozen setup to render
  on each native release runner before uploading its setup executable/checksums.

Windows execution is still unverified. A manual Windows Preparation run was
requested for implementation commit `9477813` after pushing it to `main`:
[run 37475609947](https://github.com/KindaBad/OpenFlightSim/actions/runs/37475609947).
It failed before executing any build steps. GitHub's check annotation
reported: “The job was not started because recent account payments have failed
or your spending limit needs to be increased.” No Windows steps ran. Repository
release inputs (`OFS_UPDATE_BASE_URL`, asset-pack URL/hash/size) are not configured,
so there is no published player download. Windows build/runtime testing, a public
HTTPS host and a redistribution-approved asset pack remain required before
announcing a working Windows release. Generated bundles and screenshots stay
under ignored `build/` and `.cache/` directories.
