# Typhoon runtime asset

Donor: [Eurofighter Typhoon - Fighter Jet - Free by bohmerang](https://sketchfab.com/3d-models/eurofighter-typhoon-fighter-jet-free-992bcc8987964ca09d55410330aa8579),
Sketchfab model `992bcc8987964ca09d55410330aa8579`, published 2024-06-14 under
CC BY-NC-SA 4.0. The project owner supplied the download on 2026-10-07; the
listing and license were read the same day. The archive contains one packed
Blender file and no license text, so the listing is the license record. Credit
and terms ship in `licenses/assets/TYPHOON.md`.

`typhoon_lod0.glb` through `typhoon_lod3.glb` are generated and not version
controlled. Rebuild them headless from the unmodified donor:

```sh
blender -b --factory-startup --disable-autoexec \
  --python scripts/typhoon_donor_import.py -- --source /path/to/Eurofighter.blend
```

The script writes the four GLBs and `lod_stats.json` here and an editable
`output/Typhoon_Luftwaffe.blend`. It is deterministic and safe to repeat.

## What the import changes

- Scale is one uniform factor, 0.09989 m per donor unit, set by the published
  15.96 m length. Span then measures 10.82 m against the published 10.95 m.
- The donor undercarriage is modelled fully extended. Both legs are raised into
  their wells (nose 0.31 m, main 0.23 m) so the configured CG, 2.05 m above the
  ground, lies on the thrust line. Overall height is 5.62 m; the published
  figure is 5.28 m. The airframe is not squashed to hide that difference.
- The donor already separates its foreplanes, rudder, flaperons, twelve nozzle
  petals per engine, airbrake, gear and doors. They are grouped under pivots
  with the channel names the renderer animates; no surface is re-modelled.
- Door hinges are fitted so each open door lands on the donor's own flush
  gear-up skin; residuals are 0.8 to 2.4 cm RMS.
- Glass BSDF and transmissive HUD materials have no runtime equivalent and are
  replaced by alpha-blended materials. The 4096 px base colour, normal and
  metallic/roughness atlas is embedded unchanged in LOD0.

LOD triangles: **27,769 / 16,716 / 6,972 / 1,872**. Reduced levels are Blender
collapse decimation of the same meshes and reuse LOD0 materials by name.

## Known approximations

- The simulated wheel track and wheelbase come from `typhoonConfig()` and are
  narrower than the model's. The model is placed so the main axles sit at the
  simulated station; the nose wheel stands 0.8 m ahead of its contact point.
- Retraction is a single 90 degree fold per leg. The stowed main legs also slide
  inboard because the wing is too thin for the folded side brace.
- Nozzle petals open 4.3 degrees with reheat around the donor's inner ring.
- Markings are the donor's Luftwaffe 30+47 scheme.
