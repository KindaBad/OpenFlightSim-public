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
`output/Typhoon_7LWO.blend`. It is deterministic and safe to repeat.

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
  replaced by alpha-blended materials. The 4096 px normal and
  metallic/roughness atlas is embedded unchanged in LOD0; the base colour is
  repainted, as below.

LOD triangles: **27,769 / 16,716 / 6,972 / 1,872**. Reduced levels are Blender
collapse decimation of the same meshes and reuse LOD0 materials by name.

## Livery

The donor's Luftwaffe 30+47 markings are painted over with the Austrian Air
Force 7L-WO display scheme: black wings and spine with white contour lines,
red shapes along the fuselage, a red-white-red band behind the leading edges
that turns up the fuselage side and runs forward to the canopy, eyes on the
foreplanes, the arms on the spine and a tiger on the fin.

`livery_paint.png` is that paint in the donor's UV layout, transparent where
the airframe is not painted (nozzles, wells, cockpit, fittings). The import
lays it over the donor's base colour and keeps the donor's panel lines. It is
made by `scripts/typhoon_livery_trace.py` from two photographs the project
owner supplied on 2026-10-12, one from above and one from the port quarter;
the photographs are not in the repository.

The photographs are read rather than copied, because at their size a contour
line is two pixels wide. A camera is fitted to the photograph from above and
every texel is carried through it, which says where the scheme is red, lined,
grey or black. The paint is then laid down afresh: flat colours with clean
edges, each contour line redrawn along its centre at one width, and the band,
the `7L-WO` code and the roundels drawn from measurements. Only the eyes, the
arms and the fin are taken from the photographs' own pixels. To paint it
again (the second step needs numpy, Pillow, scipy and scikit-image):

```sh
blender -b --factory-startup --disable-autoexec \
  --python scripts/typhoon_livery_trace.py -- --source /path/to/Eurofighter.blend --dump /tmp/typhoon
python3 scripts/typhoon_livery_trace.py --dump /tmp/typhoon \
  --top /path/above.jpg --rear /path/quarter.jpg --preview /tmp/typhoon
```

What the photographs do not show is filled in, not known: the starboard side
of the fuselage and the starboard face of the fin mirror the port ones (with
the code and the fin's lettering turned to read the right way), and the
underside and lower flanks are left plain grey.

## Known approximations

- The simulated wheel track and wheelbase come from `typhoonConfig()` and are
  narrower than the model's. The model is placed so the main axles sit at the
  simulated station; the nose wheel stands 0.8 m ahead of its contact point.
- Retraction is a single 90 degree fold per leg. The stowed main legs also slide
  inboard because the wing is too thin for the folded side brace.
- Nozzle petals open 4.3 degrees with reheat around the donor's inner ring.
- The fin's tiger and the foreplanes' eyes are as sharp as the photographs,
  which is not very: about 200 pixels across the fin.
- The arms on the spine are a red-white-red flag without the eagle, which the
  photograph does not resolve.
