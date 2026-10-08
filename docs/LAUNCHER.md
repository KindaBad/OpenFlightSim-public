# OpenFlightSim Launcher

OpenFlightSim now has a separate native Windows/Linux launcher. Qt Widgets via
PySide6 Essentials gives a styled, responsive native UI without Electron,
WebEngine or a browser process. The update engine is Python standard library
code and has no Qt dependency. Production users do not need Python installed:
PyInstaller freezes a one-directory GUI and a small standalone bootstrap/helper.
The simulator remains usable directly; only its version reporting/build metadata
and packaging were changed.

## Open the launcher

**From this checkout (developers):** one command prepares everything and opens
the launcher.

```sh
./play.sh          # Fedora / Linux
play.bat           # Windows (or double-click it)
```

The first run creates the private interface environment in `.cache/launcher-venv`
and, when no simulator build exists, configures and builds the release preset
(`build/msvc` with the Visual Studio generator on Windows outside a developer
prompt; `VCPKG_ROOT` supplies the vcpkg toolchain). Later runs start in under a
second. Nothing is installed system-wide and no administrator rights are used.
If build tools are missing, the script prints the exact packages to install.
`./play.sh --shortcut` also adds OpenFlightSim to the application menu,
`--rebuild` forces a simulator build and `--no-launch` only prepares. Other
arguments are passed to the launcher. The manual equivalent is:

```sh
python3 -m venv .cache/launcher-venv
.cache/launcher-venv/bin/pip install -r launcher/requirements.txt
cmake --preset release
cmake --build --preset release
.cache/launcher-venv/bin/python -m launcher.app --installation "$PWD"
```

**From the standalone setup (players):** download the platform's `*-setup.exe`
on Windows (or `*-setup` on Linux), then double-click that one file. Click
**INSTALL GAME**; the setup downloads the complete simulator, aircraft, launcher
and runtime dependencies, checks their hashes, installs them, adds a Start Menu /
application menu shortcut, and opens the full launcher. Choose your aircraft and
press **PLAY**. Python, Qt, compilers, repository access and administrator rights
are not required on the player's machine.

Windows defaults to `%LOCALAPPDATA%\Programs\OpenFlightSim`; the development
channel uses `OpenFlightSim-Development`. Browse allows another writable folder.
The setup shows download progress and speed, checks free disk space, and supports
cancel/retry with HTTP resume. It does not activate incomplete or unverified
downloads. Closing during installation cancels the worker before exiting.
After installation, use **OpenFlightSim** in the Start Menu. You can delete the
downloaded setup file; the shortcut points to the installed bootstrap. Running
the setup again checks and opens an existing installation without a network
request; normal updates and repairs are available in the full launcher.

**From a release ZIP (alternative):** extract the install ZIP into any folder in
the home directory and start `OpenFlightSim` (`OpenFlightSim.exe` on Windows),
then press PLAY. Python, Qt, a compiler, administrator rights and a repository
checkout are not needed. The first start adds OpenFlightSim to the application
menu (a `.desktop` entry on Linux, a Start Menu shortcut on Windows) and
refreshes it if the folder is moved. The publisher's update address is built into
the package; when a newer version exists, an **Update to …** button appears on
the persistent status bar. Linux packages carry their own protobuf runtime in `lib/`, so a
package built on the Ubuntu release runner also starts on Fedora. The simulator
and server can still be started directly with their existing arguments.

The source launcher searches standard release/debug/offline-client/MSVC build
layouts, matching an executable to its build-generated catalogue. For a custom
build layout, install it with `cmake --install ... --prefix ...` and select that
folder. An incomplete installation remains visible, with actionable errors and
folder selection available; Play performs a full registry-asset preflight.

## UI and connected options

The dark launcher has blue accents, a branded header, a persistent navigation
rail, a wide flight banner and a large Play button. Home includes selectable
aircraft cards and quick controls for preset, window resolution, MSAA and VSync.
These controls share the same saved settings as the detailed pages. Settings
groups flight mode, graphics, display, controls, multiplayer and advanced options
into tabs. The bottom bar remains visible on every page and shows operation
status, download percentage, speed, remaining time and safe cancellation.

Aircraft previews are actual native simulator captures. Release builds run
`scripts/capture_launcher_previews.py` against the installed game and approved
content, then include the resulting JPEGs in the verified update payload. The
images stay out of source Git history. Source checkouts can generate their own
previews after building the game:

```sh
.cache/launcher-venv/bin/python scripts/capture_launcher_previews.py \
  --client build/release/client/ofs_client --catalog build/release/launcher-catalog.json \
  --asset-root . --output data/launcher/previews
```

Captures use an isolated graphics configuration and do not change the player's
settings. Missing optional previews use the launcher's aircraft icon. Cards are
derived from the compiled registry, and layouts scroll/stack on smaller windows.
The header's pilot name opens the Multiplayer page; local flight requires no
account. Support opens the public project's issue page.

| Section | Working controls / information |
| --- | --- |
| Home | Start simulator, end flight, flight summary, aircraft cards, quick graphics controls |
| Aircraft | Generated registry, name, optional manufacturer/type/role/engine metadata, engine count, configuration span/mass, thumbnail |
| Flight Mode | Free Flight, direct-connect Multiplayer, Local Dogfight (only in network-enabled builds), airborne start, 1–8 bots |
| Graphics | Low/Medium/High/Ultra/Custom/Auto presets, MSAA, texture cap, shadows, effects/particles, clouds, anisotropic sampling, bloom, cloud shadows, vegetation |
| Display | Detected resolutions, window dimensions, desktop fullscreen, VSync, cockpit vertical FOV, desktop refresh information |
| Controls / Input | Current built-in keyboard/mouse/gamepad bindings; automatic SDL gamepad detection in the simulator |
| Multiplayer (navigation) | Pilot name; Host and fly with a game name and 0–8 AI opponents; a live list of games on the local network with Join; join by address |
| Network (settings tab) | Numeric IPv4/IPv6, UDP port, 1–64 printable ASCII pilot name, locally managed loopback dedicated server, as used by Play in Multiplayer flight mode |
| Advanced | Starting camera, draw/scenery distance, model LOD bias, shadow resolution/extent, bloom strength, fog, contrails, wing vapour, exhaust bands, HUD/player labels, hardware report |
| Downloads | Stable/development channel, HTTPS publisher endpoint, startup check, opt-in automatic installation, release notes, progress/speed/cancel/retry/resume, rollback |
| Installation / Repair | Location, version/commit/channel, active release size/free disk, folder links, file verification, staged selective repair |

Local Dogfight requires an armed registry aircraft. The launcher rejects an
unarmed choice instead of silently switching it. Multiplayer spawn state comes
from the server. Direct connect launches the existing transport, and the
simulator shows why a connection failed. DNS names, NAT traversal and public
hosting setup are outside the current transport's capabilities.

### Playing on the local network

**Host and fly** starts the installed `ofs_server` bound to every interface
with `--lan-name` (the game name, or "<pilot>'s game"; printable ASCII, at most
48 characters, passed as one argument and never through a shell) and the chosen
number of bots, then joins it over loopback. A missile reload time, when set,
is passed as `--missile-reload SECONDS` (0.5.5 or newer): every aircraft gets
one missile back on an empty pylon after that long, in the air as well. The server stops when the host's
flight ends. Hosting needs a simulator of version 0.5.0 or newer; an older
installed or rolled-back game reports that instead of starting.

While the Multiplayer page is open and no flight is running, the launcher asks
the network for games every three seconds on a worker thread
(`launcher/lan.py`, mirroring `network/include/ofs/net/discovery.hpp`): one
broadcast question on UDP 27019 to each local network and to this computer,
answers collected for 0.6 s. A system sends a broadcast out through one
network only, which on a computer with a VPN, a virtual machine's adapter or
both wired and wireless connections is often not the network the game is on.
The question is therefore asked again from each of the computer's own
addresses, which sends it out on that address's network and reaches every
computer there whatever subnet it has been given. A game is listed with its pilots, bots, address
and version. Games built for another network protocol, compared with the
`protocol` in the installed `launcher-catalog.json`, and full games are shown
but cannot be joined. **Join** starts the simulator against the listed address
and port. Hosting and joining are for one flight: the saved flight mode is not
changed.

Discovery needs broadcast UDP between the computers. It does not cross
routers, guest-network isolation or a firewall that blocks UDP 27019/27020; a
Windows host has to allow OpenFlightSim on private networks when first asked.
A Linux firewall turns players away without asking anyone, so the page looks
(`lan.firewall_advice`): firewalld is asked whether the two ports are open,
which needs no privileges, and ufw can only be seen to be switched on. When
either would be in the way the page says so with the command that opens the
ports. Nothing is changed for the player. The page shows this computer's
address and port so others can **join by address** instead. Nothing received by discovery is trusted beyond being displayed; the
game connection does its own version and identity checks.

Settings pages are disabled while a flight or background operation is active,
so they cannot race simulator saves or installation changes. Closing during a
flight waits for that flight to end; End current flight explicitly terminates
the managed simulator. Updates wait until the game is idle. Opening installation
and log folders uses the platform's file manager.

No fake controls are provided for frame caps, render scaling, exclusive display
modes, selectable refresh rates, reflections, input rebinding or arbitrary
combat rules. Extend simulator GraphicsSettings and the launcher's central
GRAPHICS specification when implementing another renderer setting. Add a mode
to the catalogue exporter only after its simulator launch path works.

## Configuration and discovery

Preferences are stored in `launcher.json`, separately from the simulator's
existing key/value `graphics.cfg`. Both live under `%LOCALAPPDATA%\OpenFlightSim`
on Windows or `$XDG_CONFIG_HOME/OpenFlightSim` (default `~/.config/OpenFlightSim`)
on Linux. `--user-data PATH` provides an isolated profile for tests/development.
The simulator receives the absolute graphics path through its existing
`--config` argument. Graphics edits preserve unknown keys written by newer
simulators. Simulator in-flight saves are re-read after a flight. Malformed
configurations are reported and left intact until a deliberate preset/edit
restores valid settings. Writes use temporary files, fsync and atomic replace.

`ofs_catalog` links the real `ofs_core` registry and exports
`launcher-catalog.json` during every normal build. It contains capabilities,
aircraft keys, names, models, LOD paths, armed status, engines and configuration
specifications. The launcher never executes arbitrary aircraft discovery code
or parses C++ source at runtime. Optional presentation fields in
`data/launcher/aircraft-info.json` augment the exported entries; they cannot
change registry keys, assets or armed status. New compiled registry aircraft
appear without launcher code changes, and missing presentation fields have
explicit fallback text. Aircraft parameters are simulator configuration values,
not claims of certified aircraft performance.

`project(OpenFlightSim VERSION ...)` in the root CMake file is the application
version source. CMake generates `build-info.json` and a C++ header; `ofs_client
--version`, catalogue, GUI bundle, release directories and release descriptors
use it. Metadata includes commit, UTC build date and selected channel.
`OFS_RELEASE_CHANNEL` selects stable/development; `OFS_MIN_LAUNCHER_VERSION`
(default 0.3.0) is the oldest launcher allowed to handle the release format,
independently of the new simulator version. Increase it only when necessary.
Use SOURCE_DATE_EPOCH for reproducible build timestamps. Development builds from
an uncommitted checkout report the base commit; release CI builds committed tags.

Hardware detection is isolated in `hardware.py`. It uses Linux `/proc`, `lspci`
and accessible DRM VRAM information, or best-effort Windows CIM data. Display
modes come from xrandr/Win32 with Qt's current screen as fallback. RAM, logical
CPU count and known VRAM capacity drive conservative recommendations; GPU vendor
names do not. Unknown VRAM recommends Medium, small memory/CPU recommends Low,
and sufficient measured capacity permits High. Auto never assumes Ultra, and
capacity is not a GPU throughput benchmark. Windows WMI VRAM is limited/legacy;
the UI labels it reported and policy never promotes based on it alone.

## Modules and update transaction

| Module | Responsibility |
| --- | --- |
| `ui.py`, `presentation.py`, `app.py` | Native interface, responsive cards/banner, icons, background jobs, guarded operation states |
| `config.py` | Preferences, renderer schema, presets, user paths |
| `game.py`, `catalog.cpp` | Discovery, capability validation, safe argument arrays, managed processes |
| `hardware.py` | Platform probes and independent recommendation policy |
| `version.py`, `manifest.py` | SemVer, bounded schema validation, release selection |
| `download.py` | TLS/redirect policy, resume, size bounds, cancellation, SHA-256 |
| `storage.py` | Strict JSON, hashes, atomic writes, portable safe paths |
| `installation.py` | OS lease, verification, extraction, repair, activation/recovery/rollback |
| `bootstrap.py` | Stable external helper and active launcher startup |
| `platform_process.py` | Frozen-app DLL/library environment isolation for subprocesses |
| `diagnostics.py` | Rotating launcher/updater logs and secret/URL redaction |
| `shortcuts.py` | Per-user application-menu entry, created by the bootstrap |
| `scripts/play.py` | One-command source-checkout setup, build and start |
| `scripts/capture_launcher_previews.py` | Real game captures for release banner and aircraft cards |

A managed installation looks like:

```text
OpenFlightSim[.exe]             stable bootstrap / updater, outside release slots
current.json                   atomic active/previous slot pointer
releases/0.3.0-initial/          initial complete game + launcher bundle
releases/0.4.0-<unique-id>/      separately verified replacement
  ofs_client[.exe]
  ofs_server[.exe]
  launcher/ofs_launcher[.exe]
  launcher/_internal/...        Qt/Python/runtime libraries
  launcher-catalog.json
  build-info.json
  release.json                 expected files/sizes/hashes/modes
  publisher.json               initial public manifest endpoint (public packages)
  assets/... and output/...    canonical registry model paths
.downloads/<sha256>.zip         retryable verified/partial package cache
.staging/<unique-id>/           candidate tree
.installation.lock             OS lease; stale files do not imply stale locks
.transaction.json              pending pointer transaction / crash recovery
```

The 0.4.2 interface refresh retains preference schema 1, update manifest schema 1,
bootstrap protocol 1 and minimum launcher version 0.3.0. An existing launcher
downloads a complete replacement release, closes, and lets its existing stable
bootstrap activate and open the new interface. The stable helper and user-data
directory are preserved; no reinstall or shortcut replacement is required.

1. The bootstrap resolves the pointer and starts that release's launcher. The
   launcher holds the installation OS lease throughout its own/game lifetime.
2. Startup optionally fetches the configured HTTPS manifest, validates every
   release descriptor, selects platform/channel and compares SemVer and minimum
   launcher version. No endpoint or credentials are invented. Public packaging
   embeds the publisher endpoint for first-run automatic checks; a saved user
   override remains authoritative. Automatic installation is explicitly opt-in.
3. A cancellable worker downloads to a content-addressed partial cache. Range
   responses must match offset/end/total; a host ignoring Range triggers restart.
   Size and SHA-256 must match before the candidate is eligible for installation.
   Retry reuses verified downloads and resumes partials; a bad digest is deleted.
4. The GUI launches the stable bootstrap with an update request, then exits.
   The helper waits for the OS lease, rechecks the archive's size/hash, validates
   extraction paths/types/limits, checks the internal manifest against the HTTPS
   descriptor, and verifies every extracted file.
5. A same-volume directory rename creates a new immutable release slot. A durable
   journal records old/new pointers; atomic current.json replacement activates
   the candidate. Existing files are never overwritten, and the previous tree
   remains available. A crash leaves the old or new complete release active;
   the next start completes the verified transaction or restores the old pointer.
6. The helper restarts the new launcher. Failures retain/recover the previous
   pointer, log diagnostics, and restart the previous launcher where possible,
   with a failure message shown on startup. Manual Roll back verifies the previous
   tree before swapping pointers. No automatic deletion of old releases occurs.

The stable bootstrap does **not** update itself. Protocol-incompatible bootstrap
changes require a new install distribution. The launcher itself lives in the
replaceable release slot and is updated safely along with the simulator. No
running application is overwritten. SHA-256 through a trusted HTTPS manifest is
the v1 authenticity boundary: a compromised publisher can replace both hashes
and packages. Manifest authentication is isolated for adding pinned Ed25519 keys,
key rotation and canonical signed manifests later; v1 does not claim signatures.
Code signing for Windows is also a publisher/CI follow-up.

The extractor rejects absolute/dot/parent paths, backslashes, Windows device
names/alternate stream syntax, trailing spaces/dots, case aliases, duplicate
entries, symlinks, special files, file-directory collisions, unexpected files,
encrypted entries and excessive counts/expanded sizes. Only regular files listed
in the manifest plus release.json are permitted. HTTPS redirects cannot downgrade
to HTTP; system certificate verification is enabled. No downloaded binary runs
before package/file verification. Local settings/metadata are writable by the
same user; this protects release delivery, not against an attacker already able
to modify that user's applications. Logs redact URL query credentials and common
secret forms; no private repository token is used.

Verify reads the installed file manifest and reports missing, size/hash mismatch,
unsafe paths and missing executable permission. Repair fetches the exact
installed version/channel descriptor, copies intact files to a separate candidate
and downloads **only affected files**. It verifies the whole candidate before
using the same activation transaction. Keep old version descriptors and their
per-file URLs available for repair. Developer checkouts have presence-based asset
preflight rather than a fabricated hash manifest; they cannot use release updates.

## Manifest formats

Update host `manifest.json` has a bounded schema and can retain multiple releases:

```json
{
  "schema": 1,
  "releases": [{
    "schema": 1,
    "version": "0.4.0",
    "channel": "stable",
    "platform": "linux-x86_64",
    "minimum_launcher_version": "0.3.0",
    "minimum_bootstrap_protocol": 1,
    "notes": "Release notes as plain text",
    "package": {
      "url": "https://updates.example.org/OpenFlightSim-0.4.0-stable-linux-x86_64-update.zip",
      "size": 123456789,
      "sha256": "<64 lowercase hexadecimal characters>"
    },
    "files": {
      "ofs_client": {
        "size": 1234567,
        "sha256": "<64 lowercase hexadecimal characters>",
        "mode": 493,
        "url": "https://updates.example.org/files/OpenFlightSim-0.4.0-stable-linux-x86_64/ofs_client"
      }
    }
  }]
}
```

This abbreviated example illustrates fields; a valid descriptor lists **every**
file, including launcher executable/runtime, catalogue and build metadata. The
packager generates real sizes/hashes/URLs. Platforms are windows-x86_64 or
linux-x86_64; channels are stable or development. Files use portable mode 420
(0644) or 493 (0755). `release.json` inside the archive carries schema, version,
channel, platform and the same file table without URLs. It does not hash itself;
the HTTPS descriptor authenticates the archive including that internal manifest.

## Produce Windows and Linux releases

`.github/workflows/release.yml` currently publishes Windows x64 on a native runner.
It runs engine/widget tests, builds client/server/catalogue and embedded shaders,
provisions an approved hash-pinned content pack, runs existing CTest regressions,
installs the simulator/runtime, collects notices, freezes the GUI/bootstrap and
produces standalone setup executables, install ZIP, update ZIP, SHA256SUMS,
per-file repair content and manifest descriptors. It also runs the frozen setup
on each native runner and requires it to render a screenshot successfully.
The runner publishes verified packages directly to GitHub Releases, then advances
the fixed `launcher-updates/manifest.json` endpoint. Release packages and matching
library sources do not consume Actions artifact storage.
It does not publish unapproved local donor assets or embed GitHub credentials.
The existing Windows push/PR compilation workflow is preserved.

Configure these CI-only secrets: OFS_ASSET_PACK_URL, OFS_ASSET_PACK_SHA256 and
OFS_ASSET_PACK_SIZE. Configure public repository variable OFS_UPDATE_BASE_URL,
`https://github.com/KindaBad/OpenFlightSim-public/releases/download/launcher-updates`.
Versioned downloads use their immutable `v<VERSION>` release tag; no GitHub
credentials are embedded in player downloads. The asset ZIP
contains exactly the registry GLBs/LODs, `asset-approval.json`, and optional
`licenses/assets/` notices. Approval schema:

```json
{"schema":1,"assets":{"assets/aircraft/su57/su57_lod0.glb":{
  "redistributable":true,"license":"Publisher-verified license or original work",
  "source":"Documented author/source and permission record",
  "sha256":"<matching asset SHA-256>"
}}}
```

List **every** model/LOD, not only Su-57. A publisher must actually establish
those rights; a JSON assertion cannot grant them. The Su-57 and Typhoon donors' Creative
Commons listings are recorded in ASSET_RELEASE_PROVENANCE.md; both ship as
ordinary GLBs. `OFS_INCLUDE_SU57=OFF` still excludes the Su-57 from the compiled
registry, launcher catalogue, installed models and content pack.
CI runs the complete source regression suite, then reconfigures the player
binaries to require every registry model and LOD and runs their content checks. CI fails when release inputs or
approval are missing. See LAUNCHER_DEPENDENCIES.md for runtime/license/source
publication requirements.

For the configured public repository, increment the root CMake version, commit
and push both source snapshots. The public `main` push automatically builds and
publishes the new version. A published version is skipped; unfinished drafts can
be retried. Matching `v<VERSION>` tags and dispatching **Launcher and Simulator
Release** on public `main` can also start a build. Published versions cannot be replaced.
The Windows Preparation workflow continues to build/test ordinary source pushes.
Only version releases advance installed launchers. Development releases use
`v<VERSION>-development` and the development channel.

The publisher uploads hash-named flat repair files, validates GitHub's upload
sizes/digests, publishes the complete version, verifies its public descriptor,
and updates the launcher index last. Older versions remain available. GitHub's
index asset replacement briefly deletes/reuploads that file, so a startup check
during that interval can fail and be retried without affecting the installed game.
Do not delete historical version packages referenced by the index.

Manual packaging after the platform-specific CMake build:

```sh
cmake --install build/release --config Release --prefix build/stage
python scripts/collect_release_notices.py --build build/release --output build/stage
python scripts/package_launcher.py --build build/release --output build/bundles \
  --base-url https://updates.example.org/openflightsim
python scripts/package_release.py --simulator build/stage --bundles build/bundles \
  --output build/publish --base-url https://updates.example.org/openflightsim \
  --asset-approval /path/to/approved-asset-record.json \
  --notes docs/LAUNCHER_RELEASE_NOTES.md
```

Use the venv Python above, or its Windows Scripts equivalent. On Windows,
configure MSVC/vcpkg as in BUILDING.md. CMake collects non-system runtime DLLs and
MSVC redistributables. On Linux, build against the oldest supported distro ABI;
Qt/Python travel in the GUI bundle, and the simulator's driver/system runtime
requirements are documented in LAUNCHER_DEPENDENCIES.md. CMake install supplies
canonical asset paths and metadata. The packager rejects missing assets and
symlinks; copied PyInstaller bundles intentionally materialize internal symlinks
as regular files, preserving the strict updater policy (with some size overhead).

`--base-url` freezes the publisher address, CMake version, release channel and
platform into the standalone setup. It embeds the stable bootstrap as a resource;
Python/Qt travel inside the one-file application. Public packaging checks that
the setup's address/version/channel/platform match the game package. The generated
descriptor's optional `setup` record contains the executable's HTTPS URL, size
and SHA-256, while `installer` continues to describe the offline install ZIP.
Both are listed in SHA256SUMS. Omitting `--base-url` only builds the older
launcher/bootstrap bundles for local use. Local unapproved packaging does not
emit a distributable standalone setup.

For **local verification only**, `--local-development` replaces the approval
requirement and marks the payload LOCAL-DEVELOPMENT-ONLY. Never publish those
archives. Generated bundles, asset copies, ZIPs, file indexes and screenshots
stay under ignored build/.cache directories.

## Publish a new version

1. Increment the root CMake project version and update release notes. Refresh
   aircraft provenance when its tracked source inputs change. If approved art
   changes, rebuild the approved pack and update its CI URL, size and hash.
2. Commit and push the reviewed source to both repositories. A public `main`
   push automatically builds an unpublished version; no Windows PC build or
   manual tag is required. Documentation-only pushes skip already published
   versions. The original repository remains private.
3. Wait for the Windows release workflow to pass. It uploads immutable versioned
   setup executables, ZIPs, checksums and every repair object to GitHub Releases,
   verifies them, and updates the launcher manifest last. Matching library source
   archives are published in their separate source release. Never replace a
   published version with different bytes.
4. Verify the public manifest and download hashes before announcing availability.
   Link the Windows `*-setup.exe` as the player download. Test a fresh setup,
   install and Play on a Windows account without Python or build tools, followed
   by offline reopening and Start Menu launch after deleting setup. Enable
   automatic updates once in the launcher to install future versions when idle.

The public release feed is
`https://github.com/KindaBad/OpenFlightSim-public/releases/download/launcher-updates/manifest.json`.
Signing/publishing credentials stay in CI. Player downloads need no GitHub account.
Multiplayer on one local network needs no setup (see the Multiplayer page);
shared internet multiplayer requires separate server setup.

## Verification and limitations

Run the standard-library security suite without GUI dependencies:

```sh
python3 -m unittest discover -s launcher/tests -v
```

The same command using the launcher venv also runs optional Qt integration tests
with the offscreen platform. CTest registers the suite as launcher.security_config.
Tests cover SemVer ordering, strict JSON/schema/hash validation, HTTPS redirects,
portable path safety, malicious archives, partial/corrupt/oversized downloads,
resume/restart/cancellation, lease exclusion, whole-tree checks, atomic activation,
failed-pointer crash recovery, rollback, selective repair/failure retention,
configuration persistence, dynamic registry discovery, mode/network arguments,
hardware policy, and actual widget interactions/background-job completion.

```sh
cmake --build build/release --parallel 3
ctest --test-dir build/release --output-on-failure --parallel 3
.cache/launcher-venv/bin/python -m launcher.app --installation "$PWD" \
  --user-data .cache/launcher-ui-profile --screenshot .cache/launcher-ui.png
```

Graphical simulator tests require an accessible desktop; do not run them while
another graphical test or launcher UI capture is using focus. Network timing/soak
checks should follow the existing suite's serial scheduling and BUILDING.md.

Current limits: x64 Windows/Linux; Ubuntu 24.04 ABI baseline for CI Linux bundles;
best-effort hardware/display detection; capacity-based presets rather than GPU
benchmarks; launcher-managed hosting on loopback or the local network only; no input rebinding; no signed
manifests/code signatures yet; stable bootstrap changes require a distribution
refresh; previous releases/downloads consume disk until explicitly removed by
an administrator while idle. Full-package updates download the release rather
than binary deltas; repair downloads individual affected files. Hashing is
cancellable between files, and a blocked HTTPS read can take up to its 20-second
network timeout to observe cancellation. True power loss/filesystem durability
varies by OS/filesystem, while ordinary process-interruption recovery is tested.
The local results and exact commands are recorded in [LAUNCHER_VALIDATION.md](LAUNCHER_VALIDATION.md).
Windows runtime/CI execution must be verified on Windows; no Linux test can
certify D3D11, Win32 display probing, DLL deployment or Windows process locks.
