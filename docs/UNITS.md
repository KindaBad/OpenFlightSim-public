# Units, coordinates and physical scale

Core and wire mechanics use SI. One world, renderer, Blender (metric scale 1)
or evaluated glTF unit equals **one metre**. Asset node transforms are evaluated
before converting to body coordinates. Scaling geometry to match references is
applied to mesh data; camera offsets do not compensate for wrong model sizes.

| Quantity | Internal unit |
|---|---|
| Position, length, radius, CG, suspension travel | m |
| Time, fixed timestep, actuator/spool time | s |
| Velocity, wind, projectile muzzle velocity | m/s |
| Acceleration, gravity | m/s² (`kG0 = 9.80665`) |
| Empty/fuel/payload/total mass | kg |
| Inertia tensor | kg·m² |
| Force, engine thrust, contact force | N |
| Moment/torque | N·m |
| Pressure, dynamic pressure | Pa |
| Density | kg/m³ |
| Absolute temperature | K |
| Angles, angular velocity | rad, rad/s |
| Engine TSFC | kg/(N·s) |
| Spring, damper coefficients | N/m, N·s/m |
| Normal load, Mach, coefficients, normalized controls/spool | dimensionless |

`State::pos_ned` is the actual CG position. Body-definition surface/engine/gear/
camera/gun/hitbox coordinates use the reference loaded CG; subtract `loadedCg`
before applying forces or attaching geometry. The world is flat Earth and the
runway plane is NED Z=0. The ISA altitude is approximated as geopotential height;
the atmosphere is bounded to −500..47,000 m. Frames and attitude signs are in
[COORDINATES.md](COORDINATES.md): world NED, body forward/right/down, renderer
east/up/south. Quaternions rotate body to world. Positive pilot stick means pull,
right roll or right yaw. Geometry in canonical Blender space is aft/right/up;
glTF is aft/up/port, with a separate documented asset CG.

Explicit compatibility/presentation boundaries:

- `Instruments::*_deg` and plain C `OfsState` Euler angles are degrees; conversion
  happens while forming/reading those interfaces. Core angles remain radians.
- `Weather::temp_offset_c` is a **temperature difference** in Celsius, identical
  numerically to a Kelvin difference; absolute `AirData::temp` is always Kelvin.
- Graphics sun/camera UI angles are degrees converted before trigonometry.
- Legacy `flap_max_deg` describes a deflection limit in degrees; runtime device
  inputs/actuators are normalized. It does not change the core angle convention.
- HUD knots/feet and imperial reference inputs use named helpers in
  `ofs/units.hpp`. Telemetry records metres, m/s, N, kg, rad/s, and explicitly
  named instrument degree fields; G and Mach are dimensionless.
- Lighting radiance/exposure are a relative renderer scale, not calibrated
  photometric lux. Texture color factors and lighting computations are linear;
  base/emission artwork is sRGB, normal/MR/occlusion maps are linear data.

Each asset's measured bounds, gear contacts, tyre radii, pivots and CG anchor
must be compared with its physics/visual definition. Public dimensions are
reference anchors; section polars, hidden mass distributions and proprietary
flight-control laws remain estimates or calibrated approximations. The
[M3.68 validation record](M3_68_SU57_REALISM_GRAPHICS_VALIDATION.md) tracks that
audit and does not claim fully realistic or classified precision.


Measured GLB rest bounds, including extended gear (m, no runtime scale):

| Aircraft | Length | Span | Height | Classification |
|---|---:|---:|---:|---|
| A320 ceo / wingtip fences | 37.5700 | 35.8000 | 11.7700 | REFERENCE dimension anchor |
| Typhoon | 15.9600 | 10.9500 | 5.2854 | REFERENCE; includes 5.4 mm gear mesh lip |
| SR-71A | 32.7406 | 16.9418 | 5.6388 | REFERENCE dimension anchor |
| Su-57 | 20.1000 | 14.1000 | 4.6000 | Public CAD REFERENCE anchor, not OEM certification |

A320's nose contact was 9.5 m ahead of reference CG; it now uses 10.44 m,
matching a 12.64 m main-to-nose wheelbase in the measured visual asset. Su-57
contacts give a 9.25 m estimated wheelbase, 4.5 m main track and 2.45 m
reference CG height; main tyre radius .515 m, paired nose tyre radius .33 m.
Its eye point is 6.70 m ahead and .98 m above reference CG, validated against
seat/canopy geometry. First-person clipping uses at most .08 m; default vertical
FOV is 70 degrees, configurable 40–100 degrees.

The fictional flat airfield runway is 2600 × 45 m. Centreline paint is .90 m
wide (previously 1.8 m), with 30 m dashes and 60 m repetition. Airport scenery is
measured in metres. The pavement/paint have 5 cm depth-separation offsets;
these are a rendering approximation, not physical steps in the ground plane.
Near/far defaults .5/15000 m, shadow half-extent 160 m, medium map 2048 pixels;
shadow snapping uses light-space metres/texel and double-precision absolute
origin coordinates. Effects use metre sizes and second lifetimes. No model
scale, camera multiplier or effect-scale factor compensates for incorrect units.

Legacy aircraft configurations with empty_mass=0 infer basic mass as reference
mass minus reference fuel/payload (A320 42,000 kg, Typhoon
11,000 kg). Su-57 and SR-71 state those masses explicitly. Runtime total mass
includes actual fuel/payload, changing CG and positive inertia through the
existing shared parallel-axis model; kg·m² values must not be confused with
mass or nondimensional tuning gains.

Current A320/Su57 geometry, loading and provenance: [aircraft physical audit](AIRCRAFT_PHYSICS_AUDIT.md). Falcon is retired.
