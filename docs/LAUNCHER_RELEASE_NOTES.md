OpenFlightSim 0.3.0 Launcher

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
