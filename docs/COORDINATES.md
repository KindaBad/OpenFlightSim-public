# Coordinates and units

All simulation values use SI: meters, seconds, kilograms, Newtons, radians.
Global positions, velocities and attitudes use doubles. M0 is flat Earth; there
is no geodetic/ECEF transform, curvature, geographic origin or terrain streaming.

| Frame | +X | +Y | +Z | Handedness |
|---|---|---|---|---|
| Simulation world NED | north | east | down | right |
| Aircraft body FRD | forward | right/starboard | down/belly | right |
| Client render | east | up | south | right |

`State::att` is a unit quaternion `(w,x,y,z)` rotating body to world. Rotation is
`q * (0,v) * conjugate(q)`. Euler sequence is yaw-pitch-roll, aerospace 3-2-1:
`q = qYaw * qPitch * qRoll`. Positive roll lowers the right wing; positive pitch
raises the nose; positive yaw turns north toward east. In FRD, a positive
right-handed +Y rotation maps +X toward -Z, so pitch is **not negated**. The old
helper negated pitch incorrectly; M0 corrects both C++ and C API interpretation.
Any externally saved state using the former sign must convert its pitch.

Controls: positive elevator stick pulls/nose-up; positive aileron rolls right;
positive rudder pedal yaws right. Surface elevator deflection has the opposite
sign from pull. Existing negative rudder effectiveness is retained; the surface
mapping reverses positive pedal to produce the documented right-yaw moment.

Altitude MSL is `-pos_ned.z`. Ground height returns down-positive NED Z. AGL is
`ground_z - aircraft_z`. Current runway plane is ground_z = 0. Gear contact
compression is positive down; spring/damper normal magnitude adds the
compression velocity term, providing upward force on sinking contact points.
M1 applies normal force along world −Z and tire friction along horizontal
projected wheel axes, independent of body pitch/roll. Contact remains a flat
runway approximation.

## Render origin

Reuse existing RenderOrigin, rebased around the double-precision free camera
when distance exceeds 5 km. First compute `delta = position_ned - origin_ned` in
double, then convert:

```text
render.x = float(delta.east)
render.y = float(-delta.down)
render.z = float(-delta.north)
```

This is a proper rotation (determinant +1), preserving handedness. Camera and
all objects use the same origin. Body geometry is FRD relative to aircraft CG;
model matrix columns transform body basis vectors through the attitude quaternion
and then through the NED/render rotation. Ground geometry is centered at field
origin, translated relative to the same render origin. Static field spans 12 km;
rebasing does not create infinite world content or guarantee precision for distant
meshes. Large-map chunking remains future work.

## Preserved Blender asset

The existing asset is authored in meters in Blender Z-up, nose at X=0 and tail
near X=37.57. Its nose points toward -X; starboard is +Y, up is +Z. The original
asset README calls +X its longitudinal axis (increasing toward tail), not body
forward. glTF exports normally convert Blender Z-up to glTF Y-up and apply node
transforms; do not simply copy vertex axes into the simulation.

For a later loader, evaluate glTF node transforms first, recover the authored
space and choose a documented CG anchor `cg_blender`. The CG location is not
supplied by the visual model and must be calibrated against gear/configuration;
do not assume nose-origin equals simulator origin. No existing asset or export
axes were changed.

## M3.5 render frame and the A320 anchor

The render frame is right-handed with +X east, +Y up and +Z south. The mapping
from the simulation's body frame is `localPosition`, which is pure rotation and
never translation-bearing:

```
render = (d.y, -d.z, -d.x)          for positions, after subtracting the origin
```

The consequence worth stating plainly, because several defects in M3.5 came from
missing it: **the vertical axis is body -Z, not body -Y.** `renderDirection({0,0,-1})`
is render +Y, and the coordinate tests pin this down. Anything that needs "up"
must use that axis - camera offsets, the orbit sphere, the chase up vector. Using
the body Y slot places cameras under the terrain.

The A320 asset arrives through the glTF exporter, so in asset space +X runs aft
from the nose, +Y is up and +Z is to port. Mapping to body FRD is again a pure
rotation plus the CG anchor:

```
body.x = -(asset.x - cg.x)
body.y = -(asset.z - cg.z)
body.z = -(asset.y - cg.y)
```

Measured from `output/Airbus_A320.glb`: main wheels centred at asset X 17.71 with
track |Z| 3.39..4.49, nose wheels at X 5.07, wing MAC 3.577 m, quarter chord at
X 17.74, bounds X 0..37.57, Y 0..11.77, Z +/-17.9. That gives the anchor:

| component | value | derivation |
|---|---|---|
| `cg.x` | 15.51 m | main gear sits 2.2 m **aft** of the CG (M1's validated `AircraftConfig`), and aft is body -X, so `cg.x = 17.71 - 2.2` |
| `cg.y` | 3.55 m | matches `AircraftConfig::gear_nose.z`, the sim's contact height below the CG; the model's tyres touch asset Y=0, so this puts them on the ground plane |
| `cg.z` | 0 | the asset is laterally symmetric about its centreline |

Adding 2.2 instead of subtracting it mirrors the airframe about the gear, which
is why `cg.x` is 15.51 and not 19.91; the coordinate test asserts the wheel lands
at body -2.2.

The nose wheels then land at body +10.44 m against the sim's +9.5 m. That 0.94 m
difference is the sim's simplified 11.7 m wheelbase versus the model's real
12.64 m. It is documented rather than "fixed" by changing flight physics, which
M1 validated. No flight parameter was changed by M3.5.

## M2 wire coordinates

Network aircraft records preserve global NED position, NED velocity, body FRD
angular velocity and body-to-world quaternion `(w,x,y,z)` as explicit big-endian
binary64 values. Render origin and renderer-local floats are never transmitted.
Clients interpolate remote double values first and subtract the same local render
origin as their predicted aircraft. Control scalars use binary32 with the same
M1 signs/units/ranges, including elevator trim. Snapshot authority corrects small
cross-platform floating-point divergence; no bit-identical lockstep is assumed.

## M3.66 moving CG

`State::pos_ned` denotes actual CG. Definition surface/engine/gear/gun and visual
attachment positions use the reference loaded body frame. Subtract
`loadedCg(config,state)` before rotating and adding actual CG position. Forces,
wheel contact, render/camera, particles and combat sphere offsets share this rule.
Surface flow is body air-relative velocity plus `omega cross (point-CG)`. The
positive side-force basis is `flow_direction cross lift_axis`, aligned with body
+Y in forward flow. Pilot signs remain positive pull/right roll/right yaw.
