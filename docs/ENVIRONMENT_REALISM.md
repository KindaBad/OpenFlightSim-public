# Environment and ground impacts

The native client now renders moving volumetric clouds, cloud self-shadowing and
sun scattering, soft projected cloud shadows, smoother hills, grass/field/rock
materials, mixed trees, rocks, service roads, fences and utility buildings.
Scenery is built once into spatial batches, culled against the camera and drawn
with simpler crowns at distance. Runway and taxiway clearances stay open.

## Controls and performance limits

Press **F1** and open **Clouds and scenery** in Graphics. Settings persist through
the existing Save settings action.

| Setting | Default | Purpose |
| --- | --- | --- |
| `clouds` | `2` | 0 off, 1 low, 2 medium, 3 high |
| `cloudCoverage` | `0.48` | Fractional weather coverage, 0–1 |
| `cloudBase` | `1800` | Cloud base, metres MSL |
| `cloudThickness` | `950` | Cloud layer thickness, metres |
| `cloudShadows` | `1` | Projected sun attenuation on surfaces |
| `vegetation` | `1` | Trees, rocks and additional scenery batches |
| `sceneryDistance` | `9000` | Maximum scenery distance, metres |

Low marches 12 samples at one quarter of the display dimensions. Medium uses
24 samples and High 36, at half the display dimensions. All tiers cap the
cloud target at 960 × 540. Rays stop at the nearest scene surface and exit early
when opaque. Two short sun probes provide internal lighting. A compact R16F
scene-range attachment and depth-aware upsampling keep clouds off nearby
cockpit/aircraft geometry. Clouds and particles share the HDR display transform.
Off/zero coverage removes the extra scene-range attachment and cloud passes.

No temporal accumulation is required. Weather uses a reusable 64³ noise texture;
wind displacement stays in world coordinates across render-origin rebases.
Particles remain limited to the existing 4096-entry pool. Smoke/fire emissions
are timed and expire; scrape effects are throttled and debris hits the terrain.

## Collision and crash behavior

`ofs/terrain.hpp` supplies the same polar-grid vertices to rendering and physics.
Physics locates the actual triangle, interpolates its height and uses its face
normal for gear, tire friction and distributed fuselage/wing/upper-body contacts. Lighting
uses smoother normals without changing the collision mesh. Camera clearance,
AGL, ground effect and ground-mode control logic all use the terrain height.
The original airfield remains level; the terrain mesh has a 64 km radius
(its present shape is described in [TERRAIN.md](TERRAIN.md)). Scenery
objects are visual additions, not individual solid-object collision bodies.

Impact work above safe normal closing speeds damages the existing replicated
body/surface health fields: gear contacts tolerate 6 m/s, body contacts 3 m/s.
Wing strikes can reduce lift on the affected side. Heavy structural damage
reduces engine output; a destroyed airframe stops engines/reheat. Deep contacts
use inelastic impulses and position correction to prevent tunneling and
spring-driven launch of the wreck. Ordinary touchdown and parked loads cause no
damage. The aircraft retains rigid-body tumble/skid motion; particles represent
fragment debris, rather than a separately simulated fracture mesh.

Offline wrecks remain visible, darken and emit smoke/fire while the simulation
continues. **F2** restarts airborne and **F3** resets to the runway. Multiplayer
terrain damage and destruction are server-authoritative, count a death without a
self-kill and use the existing respawn lifecycle. Rebuild client and server
together so prediction uses the same terrain and damage physics; no new wire
fields are required.

## Verification

The Release client and portable GLSL shaders build successfully. The final
physics/flight/contact/particle/camera selection passed **79/79** checks. Seven
ASan/UBSan checks passed with leak detection, including terrain interpolation,
all five aircraft crashes (including inverted wreck support), normal touchdowns, deterministic replay, bounded wreck
effects, server destruction/respawn, the original flight cycle and body contacts.
The native graphical smoke passed resize, two fullscreen switches, minimize and
restore, and additional Off/Low/High/zero-coverage/Medium transitions.

The broader 120-test run completed including both 180-second network/combat
soaks. Its initial particle and flat-reference flight-cycle failures were fixed
and rerun successfully. The impaired-network combat test initially reached four
kills but only three respawns within its deadline; its isolated rerun passed.
The legacy continuous flight cycle now explicitly uses a flat reference surface;
all gameplay and the new hill collision tests use terrain by default.

Native captures exercise the airfield/hills, inside/above the cloud layer and a
real physics crash. Reproduce from the repository with the Release binary:

```sh
build/release/client/ofs_client --aircraft su57 --flight-demo crash \
  --camera orbit --orbit-distance 60 --frames 220 --screenshot crash.ppm
build/release/client/ofs_client --aircraft su57 --visual-scenario above-clouds \
  --camera orbit --orbit-distance 150 --frames 45 --screenshot clouds.ppm
ctest --test-dir build/release -R '^environment\.' --output-on-failure
```

Visual scenarios freeze physics; `--flight-demo crash` integrates the ordinary
simulator. Rootless host library overrides are described in `BUILDING.md`.
Local captures, build/test logs and performance results are in
`.cache/environment`. Native OpenGL was verified on this workstation; the
Windows/D3D11 runtime was not exercised.

## Measured native performance

1280 × 800, VSync off, four-sample MSAA, fighter orbit camera, native client.
These are camera-specific workstation measurements, not minimum hardware
requirements. Aircraft models/LODs and effects use the production path.

| Case | FPS | GPU frame time |
| --- | ---: | ---: |
| Before this change, one fighter | 226.0 | 2.74 ms |
| Clouds/scenery off, new renderer | 231.0 | 2.69 ms |
| Low clouds and scenery, one fighter | 150.4 | 4.54 ms |
| Default Medium clouds and scenery, one fighter | 148.8 | 4.61 ms |
| Default Medium, 16 mixed aircraft | 78.6 | 6.69 ms |

The default adds about 1.9 ms of GPU time in the single-aircraft camera tested.
Cloud-disabled rendering avoids that cost. Tree/scenery batches use 35 additional
draws in this view, rather than one submission per object. Earlier transient
profiles of the prototype range/composite path are superseded by
`.cache/environment/performance-final.json`.
