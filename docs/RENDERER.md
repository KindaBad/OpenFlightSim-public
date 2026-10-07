# Renderer

OpenFlightSim 0.4.0 replaces the lighting, atmosphere, cloud, terrain and
display stages of the native client. This document describes how the renderer
works, the physical units it uses, what each graphics setting costs, and how the
result was verified.

Nothing here touches the simulation. The renderer reads aircraft state and the
shared terrain function and never writes to either; flight physics, networking
and combat are unchanged.

## Units

Everything is drawn at real scale, in SI units.

- Distances are metres. The atmosphere is 100 km deep on a 6360 km planet, the
  default cloud base is 1500 m, cirrus sits at 9800 m and trees are 10 to 26 m tall.
- Radiance uses one scene unit = 10 000 cd/m². The sun above the atmosphere
  delivers 128 klx; a clear high sun reaches the ground at about 100 klx, the
  blue zenith is 2000 to 4500 cd/m², and a sunlit cloud is about 30 000 cd/m².
  These are outputs of the atmosphere model, checked by `visual.atmosphere`,
  not tuned colours.
- Haze is set by meteorological visibility in kilometres (Koschmieder's relation,
  2 % contrast), not by a fog colour and density.
- Exposure is metered from the scene's illuminance like an incident-light meter,
  with compensation in photographic stops.

## Frame graph

`client/src/renderer.hpp` lists the passes in execution order.

| Pass | Target | Purpose |
|---|---|---|
| Sky-view table | 192 x 108 RGBA16F | Radiance of the whole sky from the camera's altitude |
| Aerial perspective | 256 x 128 RGBA16F | In-scattered light at 32 depths across the view frustum |
| Shadow cascades | up to 6144 x 2048 D32F | Sun shadows, two or three cascades in one atlas |
| World | HDR RGBA16F + range R16F + depth, MSAA | Sky, terrain, airfield, forest, aircraft |
| Cloud march | at most 960 x 540 | Volumetric cumulus and cirrus, jittered per frame |
| Cloud accumulation | same | Reprojection and blend with the previous frames |
| Atmosphere | HDR colour | Cloud composite, particles, afterburner plumes, rain |
| Flight deck | HDR colour, depth cleared | The pilot's own aircraft in its own depth range |
| Heat refraction | half resolution RG16F | Image offsets from hot exhaust |
| Glare pyramid | five levels | Lens and eye scatter around bright sources |
| Display transform | back buffer or RGBA8 | White balance, exposure, tone curve, sRGB, dither |
| Edge filter | back buffer | FXAA on the finished image |

The implementation is split by subsystem: `renderer.cpp` (frame, aircraft,
effects), `renderer_atmosphere.cpp` (sky, clouds, shadows, display chain) and
`renderer_environment.cpp` (terrain, airfield, forest). Per-frame constants are
one packed uniform array, described once in `client/shaders/frame.glsl` and
mirrored by `FrameConstants`.

## Atmosphere and lighting

`client/src/atmosphere_model.cpp` describes the air: Rayleigh scattering,
aerosols with a 1.2 km scale height, an optional shallow fog layer and ozone
absorption. It builds a transmittance table (Bruneton and Neyret 2008) and a
multiple-scattering table (Hillaire 2020) on the CPU in about 30 ms. Each frame
the GPU integrates the sky-view table and the aerial-perspective atlas from them.

Every lighting quantity comes from this one description:

- **Sunlight** is the solar irradiance times the transmittance along the path
  from the surface to space, looked up per pixel at the surface's own altitude.
  Peaks stay lit after the valleys are in shadow, and a low sun reddens.
- **Skylight** is the sky radiance integrated into first-order spherical
  harmonics, including light bounced from the ground.
- **Haze** on terrain, aircraft, trees, particles and clouds is the same
  in-scattering integral that produces the sky behind them, so distant objects
  fade into the sky without a seam. Transmittance is per colour channel, which
  is what turns far ridges blue.
- **Reflections** on aircraft skin, canopies and water read the sky-view table.
- **Exposure** follows the metered illuminance with a 0.45 s time constant.

Below the horizon the sky pass continues the simulated ground plane with the
scene's mean albedo and the same haze, so the land has no visible edge at any
altitude.

## Clouds

`cloud_fs.glsl` marches a cumulus layer whose shape comes from a 96³
Perlin-Worley volume eroded by a 32³ Worley volume and carved by a 512² weather
map (Schneider 2015). Coverage in the weather map is rank-equalised, so a
coverage setting of 0.4 covers 40 % of the sky. Cells are tallest at their
centres and their tops follow the noise. The layer curves with the Earth, so it
meets the horizon correctly although the world is flat for navigation.

Lighting uses energy-conserving integration, a secondary march toward the sun,
and two broader, less attenuated terms for the higher scattering orders that
make dense cloud white. Cirrus is a thin sheet at 9800 m. Clouds receive aerial
perspective and cast shadows on the terrain, trees, aircraft and particles.

The march runs at a quarter of the display pixels (a ninth on Low), with a
different sample offset each frame. `cloud_resolve_fs.glsl` reprojects the
previous result by the cloud's mean depth, clamps it to what the current frame
sees nearby and blends 10 % of the new samples in. History is rejected where
geometry has moved in front of the cloud.

## Terrain, water and forest

The terrain mesh **is** the simulation's collision surface: the inner 32 km
reproduces `ofs::terrainVertex` vertex for vertex with the same triangle
diagonals, and it is never displaced. Outside that radius the simulation uses
the analytic height function directly, so the mesh continues the same function
in widening rings to 262 km.

Detail is shading only:

- A per-pixel normal from a copy of the height function in `terrain_fs.glsl`.
  `regression.shader_sources` fails if its constants drift from `terrain.hpp`.
- Six tiling material layers (grass, soil, rock, forest canopy, snow, asphalt)
  synthesised at start-up, each with albedo, height, normal, roughness and
  cavity. They are blended by slope, altitude and a baked land-cover map, at two
  scales, and hand over to their mean colour beyond a few kilometres so no tile
  repeat shows from altitude.
- Farmland as an irregular patchwork with a crop, a lay and a boundary hedge per
  field.
- Long shadows cast by the relief when the sun is below 42°, found by walking
  the height function toward the sun.

`client/src/landscape.cpp` classifies the surface. Lakes are found by a
priority flood (Barnes et al. 2014): each closed basin deeper than 45 m is
filled part-way to its spill point. The airfield is treated as a drain so the
basin it sits in stays dry. The lake level is constant per basin and the
shoreline is decided per pixel. Water reflects the sky-view table with Fresnel
weighting, shows sun glitter, and reveals the bed where it is shallow. Lakes are
presentation only: the lake bed remains the collision surface.

Trees are instanced. Each square kilometre is a static buffer of 32 bytes per
tree, generated on demand around the camera from the same land-cover map that
shades the ground, and drawn with one of four shared meshes. The previous
renderer stored unique vertices for every tree, about 50 kB each, and only
within 9 km of the airfield; the forest now follows the aircraft anywhere for a
few megabytes.

Paved surfaces are coplanar with the ground. Each layer is separated from the
one below by a constant offset in depth-buffer units, which holds at every
range where a geometric lift does not.

## Shadows and depth

Sun shadows use two or three stable cascades fitted to spheres around slices of
the view frustum and snapped to whole texels in absolute coordinates, so edges
do not shimmer as the camera turns or the origin rebases. The first cascade is
sized for the subject: about 1 cm per texel around the pilot in the cockpit, 5 cm
around the aircraft from a chase camera. Receivers are offset along their normal
rather than by a large depth bias.

Seen from the flight deck the airframe is centimetres from the eye and the
horizon is a hundred kilometres away. The pilot's own aircraft is therefore
drawn last, after clearing depth, with a 5 cm near plane; the world keeps a
1.5 m near plane. This removes the depth flicker the single 8 cm near plane
caused on distant terrain and buildings.

## Materials and display

Surfaces use GGX with height-correlated Smith visibility, an analytic split-sum
environment term, multiple-scattering energy compensation and geometric
specular antialiasing. Canopy glass is a thin dielectric whose opacity rises
toward grazing angles as its reflection takes over.

The display transform applies a daylight white balance, the metered exposure and
a filmic curve in a slightly desaturated space, in the manner of AgX: bright
colours roll off toward white instead of clipping to a saturated primary, and
18 % grey maps to 18 % grey. `client.shader_conformance` pins those anchors.
One least-significant bit of dither removes banding in sky gradients.

## Effects

Particles are lit by the sun and sky, fade where they meet geometry, receive
haze and are hidden behind cloud. Hot exhaust refracts the scene behind it
through an offset buffer instead of drawing translucent bands. Rain is drawn in
the frame of the drops' velocity relative to the camera, so it falls vertically
when parked and streams from the heading point in flight; it also wets and
darkens paving and closes in the visibility.

## Settings

F1 opens the graphics panel. Presets set every cost-related option at once;
changing any of them individually selects Custom. The launcher exposes the same
presets and options.

| Option | Low | Medium | High | Ultra |
|---|---|---|---|---|
| Multisampling | off | 2x | 4x | 8x |
| Edge filter (FXAA) | on | on | on | on |
| Sun shadows | off | 3 x 1536 | 3 x 2048 | 3 x 2048 |
| Shadow distance | – | 1.2 km | 1.6 km | 2.4 km |
| Clouds | 28 steps, 1/9 res | 44 steps | 64 steps | 64 steps |
| Cloud shadows | off | on | on | on |
| Terrain detail | vertex normals, one scale | full | full | full |
| Relief shadows | off | off | on | on |
| Tree density and distance | 250 /km², 3.5 km | 450, 5.5 km | 650, 7 km | 900, 10 km |
| Draw distance | 80 km | 120 km | 160 km | 220 km |
| Glare, heat refraction | off | on | on | on |
| Aircraft texture limit | 1024 | 2048 | 2048 | 4096 |

Time of day, visibility, fog, rain and cloud cover are scene choices and are
left alone by presets.

`graphics.cfg` keys whose meaning changed have new names (`drawDistance`,
`glare`, `shadowDistance`), so a file written by an earlier build or launcher
keeps its still-valid choices and falls back to defaults for the rest.

## Cost

Measured on Linux/OpenGL with an Intel Arc integrated GPU (Arrow Lake), Typhoon
airborne in chase view, default weather:

| Configuration at 1920 x 1080 | Frames per second | GPU frame |
|---|---|---|
| Previous renderer (0.3.3 defaults, 24 km view) | 96 | 8.5 ms |
| Low | 201 | 3.8 ms |
| Medium | 77 | 11.5 ms |
| High (default) | 69 | 12.9 ms |
| Ultra | 33 | 20.5 ms |

High costs about a quarter more than the renderer it replaces while drawing
seven times the distance; Low is twice as fast as the old defaults.

Procedural environment textures total 26 MB. The High shadow atlas is 48 MB
(the previous single 4096² map was 64 MB). The atmosphere tables, noise
volumes, material layers and land-cover maps are synthesised in about 0.7 s on
16 threads, on a background thread that overlaps aircraft loading, so start-up
is no longer than before.

Anisotropic filtering was requested by earlier builds but never took effect,
because bgfx leaves the maximum anisotropy at zero unless the reset flag is set.
It is now applied, including to the terrain layers.

## Verification

- `visual.atmosphere`, `visual.procedural`, `visual.landscape` and
  `visual.scenery` run headless: physical ranges of sun and sky light, the
  visibility relation, tiling of every noise primitive, lake and tree placement
  against the collision surface.
- `regression.shader_sources` checks the shader copy of the terrain function,
  the frame-constant layout and sampler stage assignments.
  `client.shader_portability` additionally compiles every shader through
  shaderc's HLSL front end.
- `client.shader_conformance` renders the shipped shaders on the GPU: alpha
  masks, double-sided and normal-mapped lighting, plume boundaries and the
  display transform's anchors.
- Native captures were inspected for clear, dusk, fog and rain weather from the
  ground, the cockpit, low level and above the cloud layer, using
  `--visual-scenario` (including the new `lake` and `mountains` viewpoints).

Windows/Direct3D 11 was not run on the development machine. The shaders are
written in bgfx's portable language and pass the HLSL front end; the Windows
build is produced and its shaders compiled with fxc by the release workflow.

## Limits

- Terrain shape is the simulation's analytic function; there is no elevation or
  imagery data. Silhouettes of distant ridges follow the collision mesh.
- Clouds are one cumulus layer plus cirrus. There are no cumulonimbus towers,
  no precipitation shafts and no lightning.
- Aircraft do not reflect clouds or each other; reflections are sky and ground.
- There is no night sky: below about -6° sun elevation the scene is dark apart
  from emissive lights.
- Lakes do not affect physics. An aircraft that descends into one meets the bed.
