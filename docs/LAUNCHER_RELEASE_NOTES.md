OpenFlightSim 0.4.5 Launcher

- Pursuit camera, now the default view: the camera sits behind and above the
  aircraft and looks where you are going, so the aircraft banks and pulls in
  front of you instead of the world rolling with it. With mouse aim it looks
  along the aim ring. The view widens as you gain speed and narrows as you
  slow down; that can be turned off in F1 > Controls. Tab still cycles to the
  earlier chase, close chase, orbit and flight-deck views, and the launcher's
  starting camera now defaults to Pursuit.
- Minimap in the lower right corner showing the terrain, lakes, the runway,
  other aircraft and your heading. It opens out as you fly faster. Press N for
  the full map with a distance and bearing back to the airfield.

- Mouse aim: press X in flight (or tick Mouse aim in the flight panel or the F1
  settings) to fly with the mouse. Move the aim ring where you want to go and
  the aircraft banks, pulls and rolls out onto it; the view follows the ring.
  W/A/S/D, Q/E and the gamepad still work and take over their own axis while
  held. Hold the right mouse button to look around. Sensitivity is in F1.

- New Su-57 model: bohmerang's Su-57 Felon in Russian Aerospace Forces "051"
  camouflage, with moving LEVCONs, leading-edge flaps, flaperons, ailerons,
  all-moving tails, vectoring nozzles and retracting gear. It is Creative Commons
  artwork (CC BY-NC-SA 4.0) and is installed unencrypted; the earlier encrypted
  Su-57 model is removed by the update.

- Redesigned launcher: a wide flight banner, blue Play button, selectable aircraft
  cards, quick graphics controls, and a persistent download/status bar.
- Aircraft previews are captured from the simulator. The aircraft cards show the
  aircraft included in your installed build.
- Settings now share one section with tabs for flight mode, graphics, display,
  controls, multiplayer, and advanced options. Changes on Home and in Settings
  stay in sync and save automatically.
- Existing launchers can install this update through their normal update button.
  Your aircraft selection, graphics settings, pilot name, and installation folder
  are preserved, with the previous version available for rollback.

- Faster start. Aircraft now load on several processor cores at once, which cuts
  the wait before the first frame to well under half, and the window shows a
  loading message and stays responsive instead of being reported as not
  responding.
- New renderer. Sunlight, sky colour, haze and exposure now come from one
  physically based atmosphere, so the time of day and the visibility you set are
  what light the scene. Volumetric clouds with a cirrus layer cast shadows on
  the ground; terrain is drawn to 160 km with textured land cover, lakes, relief
  shadows and forest that follows the aircraft.
- Sharper, steadier image: cascaded sun shadows, a separate depth range for the
  cockpit, anisotropic filtering that now takes effect, and an edge filter on
  top of multisampling.
- Rain, ground fog and visibility are new weather options. Exhaust heat refracts
  the scene behind it, and particles are lit and fade into geometry and cloud.
- Graphics presets (Low to Ultra) now cover terrain detail, tree density, draw
  distance and shadow range. Open the launcher's Graphics page, or press F1 in
  flight. Settings saved by earlier versions are kept where they still apply.


- Typhoon maneuver mode: press M for sharper pitch and roll below 300 m/s, with
  the gear up. G limits and control-surface limits still apply.
- New Typhoon model in Luftwaffe markings with a detailed cockpit and a working
  airbrake. Model by bohmerang, CC BY-NC-SA 4.0; see `licenses/assets/TYPHOON.md`.

- Fixed installation and update downloads on PCs missing GitHub's certificate
  issuer in their local trust store. Setup and launcher now bundle Mozilla's
  certificate authorities while retaining certificate and hostname verification.

- Native launcher with aircraft discovery, flight planning and renderer settings.
- Persistent presets, display selection, hardware recommendations and diagnostics.
- Direct connect, local hosting, and armed-aircraft bot dogfights.
- HTTPS updates with SHA-256 verification, resumable downloads and cancellation.
- Separate release slots, repair of affected files, crash recovery and rollback.
- Windows/Linux packaging and build-generated version information.
- One-step start: application-menu entry on first run and an update button on the Play page.
- Linux packages include the protobuf runtime, so they also start on Fedora.

# Standalone player setup

- One downloadable Windows setup executable installs the complete game and opens
  the launcher, with no compiler, Python, repository checkout or administrator access.
- Progress, free-space checks, cancellation/resume and verified atomic first install.
- Persistent Start Menu shortcut; later setup runs open the existing game offline.
- Release packaging includes setup executables, checksums and a native UI smoke check.
# First Windows player release

Download the Windows `*-setup.exe`, run it and click **INSTALL GAME**, then **PLAY**.
The game and Qt/Python runtime are installed for your user account; no compiler
or separately installed Python is required. Enable automatic updates in the
launcher's Updates page once to install subsequent published versions when idle.

This release includes A320, Typhoon, SR-71 and Su-57. The Su-57 and
Typhoon models are by bohmerang (Sketchfab, CC BY-NC-SA 4.0) and are licensed
separately from the game; see `licenses/assets`. Existing installations receive the
new aircraft through the normal launcher update.

The launcher uses dynamically linked Qt/PySide/shiboken 6.10.2. License notices
are included in the installed `licenses/` folder. Matching library sources:
[Qt / PySide / shiboken sources](https://github.com/KindaBad/OpenFlightSim-public/releases/tag/qt-sources-6.10.2).
Launcher source and reproducible freezing instructions are in this repository's
`launcher/`, `scripts/package_launcher.py` and `docs/LAUNCHER_DEPENDENCIES.md`.
The installed main launcher uses a one-directory bundle whose Qt libraries can
be replaced; the setup executable extracts its runtime to a temporary folder.
