# Graphics refresh — 1 October 2026

The native client now draws the delivered A320 with a compact flight display,
an airfield with taxiways, terminal, hangars, control tower and edge lights,
procedural grass/asphalt detail, distant hills, atmospheric haze and clouds.
The chase camera opens with the aircraft framed in a rear quarter view. F1
opens the diagnostic and graphics windows; they start hidden.

## Correctness fixes

- Quad geometry is expanded to six vertices. Four-vertex triangle lists had
  connected unrelated ground patches, runway paint and particles.
- Aircraft buffers use `BGFX_BUFFER_INDEX32`. Material ranges are bound through
  `setIndexBuffer`, rather than passed as `submit` sorting/discard arguments.
  State is retained within a pass and explicitly discarded at pass boundaries.
- Surface winding uses an explicit front-face convention; quad emission checks
  its geometric normal. Terrain rings share angular subdivisions without cracks.
- Shadows run before the world pass, use comparison sampling and map light clip
  coordinates to texture coordinates. The framebuffer owns its depth texture.
- Environment transforms, fog altitude and cloud coordinates respect origin
  rebasing. The clear color uses bgfx's RGBA byte order.
- The flight-deck camera looks forward. Orbit pitch changes altitude, the orbit
  view remains upright, free camera uses its own orientation, and resets/mode
  changes prime the camera immediately. Exterior cameras remain above ground.
- Streak velocity uses vector magnitude, not GLM's component count. Effects draw
  complete triangles. Replicated combat lines reach the renderer; destroyed
  aircraft are hidden. Contrail/heat toggles control their emissions.
- Fullscreen UI requests, framing, saved HUD preferences and graphics visibility
  work. Aircraft lookup includes the original delivered asset. Shader includes
  are rebuild dependencies. Both Debug and Release executables were rebuilt.

## Use

```sh
./build/release/client/ofs_client
./build/release/client/ofs_client --airborne --camera chase
```

Click **Fly now** or press F2 to start trimmed at 1,000 m. F3 parks on the runway,
P pauses offline flight, Backspace toggles the parking brake, Page Up/Down
changes throttle, arrows control pitch/roll, and Tab cycles camera modes.
V switches between flight deck and chase. Flight readouts use the simulator's
instruments, with airspeed in knots and altitude in feet.

## Validation

The Release suite passed all 44 tests, including the flight scenarios, asset
pipeline, 180-second network and combat soaks and graphical smoke. After the
camera, display and effects changes, the client suite was rerun. Camera
regressions cover forward cockpit direction, orbit elevation and radius,
upright orbit orientation, free-camera switching, ground clearance and zoom.

The graphical smoke exercises keyboard/relative mouse input, focus release,
resizing, fullscreen twice, minimize/restore, a real ImGui reset click and capture.
The dedicated graphical combat fixture and two native clients also passed,
including received shots, hits, destruction and respawn. The fixture additionally
requires at least ten frames that submit visible combat effect geometry.

```sh
ctest --test-dir build/release --output-on-failure --parallel 3
ctest --test-dir build/release -R '^client\.' --output-on-failure
python3 scripts/validate_graphical_combat.py build/release
```

Screenshots are actual native-client captures. Capture images are generated
artifacts and are not version controlled; regenerate them locally with
`scripts/capture_frame.py` (see [BUILDING.md](BUILDING.md)). The set comprises a
chase view, an orbit view, an airborne close chase and a forward flight-deck
view.

Scenery remains a procedural test airfield. Hills and buildings are visual;
collision still uses the existing flat runway plane. The visual landing gear
remains extended when the simulated gear retracts. The flight-deck view is a
forward camera with instruments, without a modeled interior. Linux/OpenGL was
verified on this workstation; Windows shaders/runtime remain unverified.

The UI embeds Karla Regular from the pinned Dear ImGui dependency, by Jonathan
Pinhorn under SIL OFL 1.1. Its notice is in `assets/Karla-LICENSE.txt`.
