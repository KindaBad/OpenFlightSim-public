# M3.5 Validation - Visual and Aircraft Asset Pass

Historical milestone report. The temporary Falcon has since been retired; current aircraft physics and evidence are documented in [AIRCRAFT_PHYSICS_AUDIT.md](AIRCRAFT_PHYSICS_AUDIT.md).
This records what M3.5 changed, how each item was verified, and the defects
found while bringing the renderer up. Flight dynamics, server authority,
prediction and reconciliation, hit detection, health/damage, respawn rules and
the network protocol are untouched; the only physics-adjacent change is a
camera offset and the visual model anchor, both listed below.

## Summary

The renderer now draws a lit world: a graded sky dome with a sun disc and halo,
a grass airfield with an asphalt runway and painted markings, the real A320
from `output/Airbus_A320.glb`, shadows, combat effects, a gameplay HUD and
developer panels. All four camera modes were inspected frame by frame.

Screenshots: `images/m3_5/` - `chase.png`, `close-chase.png`,
`cockpit.png`, `orbit.png`, all 1600x900, captured through the client's own
`--screenshot` path with the developer overlay hidden. These are generated
captures and are not version controlled.

## Coordinate work

The asset is authored nose-at-X=0 and tail-at-X=37.57 in Blender Z-up, which
the glTF export turns into Y-up. `kAssetToBody` maps asset space to body FRD as
a pure rotation, and the CG anchor translates it so the model origin lands on
the CG:

```
body.x = -(asset.x - cg.x)    body.y = -(asset.z - cg.z)    body.z = -(asset.y - cg.y)
```

Measured from the asset: main wheels centred at asset X 17.71, track
|Z| 3.39..4.49, nose wheels at X 5.07, wing MAC 3.577 m, quarter chord at
X 17.74, bounds X 0..37.57, Y 0..11.77, Z +/-17.9.

`cg.x = 15.51` is derived from the sim's validated gear geometry: main gear
2.2 m **aft** of the CG, and aft is body -X, so
`cg.x = 17.71 - 2.2 = 15.51`. `cg.y = 3.55` matches `AircraftConfig::gear_nose.z`
and puts the tyres on the ground plane; `cg.z = 0` is lateral symmetry.

The nose wheels then land at body +10.44 m against the sim's +9.5 m. That
0.94 m difference is the sim's simplified 11.7 m wheelbase versus the model's
real 12.64 m. It is documented, not "fixed" by altering flight physics.

**No flight parameter, trim solution or aerodynamic coefficient was changed.**
The renderer's `viewProj_` and the camera offsets are new; the physics is
untouched and the full M1/M2/M3 suite still passes.

## Defects found and fixed

Every one of these produced a wrong image rather than an error, which is why
they are listed individually. Each was found by capturing a frame and measuring
it, not by reading the code.

1. **Graphics config was silently ignored.** `read()` scanned a shared
   `istringstream` and returned on the first key it matched, leaving the stream
   positioned mid-file, so every later key lookup started from there and missed.
   Only keys ordered after the first hit in the file were applied - which is why
   `--config` appeared to do nothing. Now parsed once into a key/value table.

2. **Clear colour packed as `0xAABBGGRR`.** bgfx's `setViewClear` takes
   `0xRRGGBBAA`. Packing it the other way round swapped red and blue, which made
   the whole background read as a channel-swapped image. Caught by setting the
   fog colour to a known `(0.2, 0.4, 0.6)` and observing the read-back.

3. **`sky_vs.glsl` declared the wrong attribute name.** bgfx resolves vertex
   attributes by its own reserved names (`a_position`, `a_normal`, ...) after
   `glGetAttribLocation`. The shader used `in vec2 v_position`, which returned
   -1, so the attribute was never bound and the sky triangle collapsed. Confirmed
   by making the sky fragment output a constant colour and seeing no change.

4. **Uniforms declared `vec3`/`vec2`/`float` in GLSL.** bgfx has no such
   uniform types and always uploads with `glUniform4fv`, so every upload to a
   `vec3` was a GL error and the value silently never arrived. All shader
   uniforms are now `vec4`, matching both bgfx's descriptor type and the
   `glm::vec4` the C++ side already writes. The packer also gained patterns for
   these types, since a declaration with no descriptor is dropped without a word.

5. **`u_viewProj` was created but never assigned.** The surface shaders read it
   themselves instead of using bgfx's built-in constants, so every vertex
   collapsed to the origin. The symptom was deceptive: the sky kept rendering
   (it reconstructs rays from an inverse matrix) while the entire world
   vanished, which reads like missing terrain rather than a missing uniform.

6. **No view matrix.** `render()` built a projection and called it
   `viewProj_`. With no view transform the world sat outside the frustum and only
   the sky drew. Found by submitting a hard-coded NDC position in the vertex
   shader and still getting no pixels.

7. **Quad winding reversed for ground and runway.** Listed corners gave a
   geometric normal of -Y, so `BGFX_STATE_CULL_CCW` culled them. The aircraft
   survived because all 21 glTF materials are `doubleSided`.

8. **Runway paint wound the same wrong way.** Fixing ground and runway exposed
   this: the paint disappeared, leaving broad green bands where the markings
   should be.

9. **Camera up vector picked the wrong body axis.** This frame has -Z up
   (`renderDirection({0,0,-1})` is render +Y, as the coordinate tests pin
   down), but the camera used the attitude's Y axis, tipping every view ~90
   degrees.

10. **`Camera::orientation()` was the free-camera yaw/pitch**, used regardless
    of mode. The renderer now reads `renderOrientation()`, which returns the
    smoothed flight attitude.

11. **Camera offsets used the wrong component order.** Chase height went into
    the second body slot, putting the camera 9 m under the terrain. Height is
    the third slot in this frame.

12. **Orbit camera pitched about a lateral axis**, putting it below ground.
    It now uses an explicit `axisAngle` about the vertical axis.

13. **Model transform had no CG translation**, leaving the model in asset space
    with the body origin 19.91 m ahead of the CG, inside the forward fuselage.

14. **`cg.x` sign.** The doc said `17.71 + 2.2`; main gear is 2.2 m *aft*, so it
    is `17.71 - 2.2 = 15.51`. `client.coordinates_interpolation` caught this.

15. **Two orphaned renderer files** (`renderer_passes.cpp`, `renderer_frame.cpp`)
    contained a second, conflicting implementation of the same pass methods,
    bound to a different uniform view. Neither was referenced by CMake. Reading
    one while debugging the other wasted a lot of time; both are removed.

16. **Coplanar runway surfaces.** Asphalt, paint and grass were within
    centimetres of each other, z-fighting into wide shimmering bands at range.
    The near plane was also raised from 0.2 m to 0.5 m, which still resolves
    the cockpit view (eye sits 0.55 m above the model origin).

17. **Scissor state leaked between frames.** ImGui narrows the scissor per draw
    command and it persisted into the next frame's world passes. Now reset at
    the start of `ui()`.

## Verification

### Screenshots

Each of the four cameras was captured and inspected. Checks applied to every
frame: ImGui panels render in their intended dark palette (not tinted), the sky
gradient runs lighter at the horizon, the grass is green, the runway is asphalt
with white edge and centre lines, the horizon is level, and the A320 reads as an
A320 (blue Airbus tail, white fuselage, wings and engines) sitting on its
wheels on the runway.

### Performance

`--visual-bench N`, 1600x900, MSAA x4, shadow map 2048, Intel integrated GPU
via Mesa:

| aircraft | fps | cpu ms | gpu ms | draws | triangles | LOD |
|---|---|---|---|---|---|---|
| 1 | 61.8 | 15.56 | 14.56 | 80 | 2 133 796 | LOD0 |
| 2 | 48.4 | 20.52 | 19.16 | 118 | 3 200 694 | LOD0 |
| 8 | 27.2 | 34.87 | 33.60 | 312 | 7 635 202 | LOD1 |
| 16 | 23.9 | 45.82 | 43.92 | 497 | 10 269 746 | LOD1 |
| 28 | 14.6 | 73.09 | 70.62 | 749 | 20 122 202 | LOD1 |

Triangle counts are the main pass plus the shadow pass. LOD drops from LOD0 to
LOD1 as the scene grows, which is the intended behaviour.

### Asset pipeline

`client.asset_pipeline` loads the real A320 and reports 1134 primitives, 21
materials, 1 066 898 triangles, 21 batches, LOD
1066898/83458/11769, 64.5 MB. Unsupported glTF features
(`KHR_materials_clearcoat` on four materials) are reported per material rather
than silently dropped, and the client continues.

### Tests

| preset | result |
|---|---|
| `release` | 43/44 passed (the one failure was defect 14, now fixed) |
| `debug` | 43/43 passed |
| `headless` | 41/41 passed |
| `sanitize` | not runnable here: `libasan.so.8` is absent from this system |

M1 flight, M2 multiplayer and M3 combat suites all pass unchanged. The sanitize
gap is an environment limitation, not a code regression; the same preset needs
re-running on a machine with the sanitizer runtime installed.

## Known limitations

- No textures: the asset ships 0 images, so materials use flat base colour and
  metallic/roughness factors only.
- The sky shader does not apply `u_exposure`, so exposure affects surfaces but
  not the sky. Harmless at the default 1.0, wrong if exposure is changed.
- Cockpit view clips the nose (near plane 0.5 m); there is no interior model.
- Earth terrain, satellite imagery and weather are out of scope for M3.5.
