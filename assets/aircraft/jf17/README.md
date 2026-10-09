# JF-17 runtime asset

Donor: [JF-17 by dimal965](https://sketchfab.com/3d-models/jf-17-b57660f346314df1877e15b85d6e74be),
Sketchfab model `b57660f346314df1877e15b85d6e74be`, published 2019-04-08 under
CC BY 4.0. The project owner supplied the download on 2026-10-09. The listing's
license, its 8,321-triangle count and its description, "JF-17 for game" with a
link to the author's own game, were read the same day through Sketchfab's API;
the triangle count matches the archive. The archive (SHA-256 `e423710d…32ac70`)
holds one FBX and eight textures and no license text, so the listing is the
license record. Credit and terms ship in `licenses/assets/JF17.md`.

It replaces the model used for the local-only JF-17 of 0.5.7, a War Thunder
extract that was never distributed.

`jf17_lod0.glb` through `jf17_lod3.glb` are generated and not version
controlled. Rebuild them headless from the unmodified donor:

```sh
blender -b --factory-startup --disable-autoexec \
  --python scripts/jf17_donor_import.py -- --source /path/to/source/JF-17_4.fbx
```

The archive's `textures` folder must lie beside `source`. The script writes the
four GLBs and `lod_stats.json` here and an editable
`output/JF17_Thunder_donor.blend`. It is deterministic and safe to repeat.

## What the import changes

- Scale is one uniform factor, 1.0737 m per donor unit, set by the published
  14.93 m length. Span then measures 9.57 m against the published 9.44 m.
- The donor's main tyres hang 7.7 cm above the plane its nose tyre stands on;
  both main legs are lowered onto it. Overall height is 5.15 m against the
  published 4.77 m. The airframe is not squashed to hide that difference.
- The canopy is modelled raised 60 degrees and is closed about its own hinge.
- The airframe, canopy frame and wheels get one level of subdivision with every
  edge sharper than 42 degrees creased, which rounds the nose, intakes and
  spine without softening panel breaks. The donor has 8,321 triangles.
- The livery is repainted; see below.
- The donor's skin is bare, polished metal (metallic 0.7, roughness 0.3). The
  repainted skin is set to a semi-gloss dielectric. The donor's ambient
  occlusion map is multiplied into the colour because the runtime has none.
- Tailplanes, rudder, ailerons, flaps and leading-edge flaps are grouped under
  pivots with the channel names the renderer animates. No surface is re-modelled.
- The six gear doors stand open in the donor; each closes by undoing its own
  rotation about its own origin, 60 or 85 degrees.
- Wheels are split from the legs so they turn. The donor gives no stowed pose:
  each leg folds a quarter turn about a lateral trunnion at its top and slides
  into the fuselage, where the closed doors hide it.

LOD triangles: **34,416 / 17,469 / 8,513 / 3,955**. Reduced levels are Blender collapse decimation of
the refined meshes and reuse LOD0 materials by name.

## Livery

The Pakistan Air Force's navy, pale grey, cream and green JF-17 display scheme,
traced from one overhead photograph the owner supplied. The photograph is not
in the repository. `scripts/jf17_livery_trace.py` recovers the photograph's
camera from thirteen points found on both the aircraft and the model (worst
14 px, mean 7 px), looks up each centimetre of the model's upper skin in it and
sorts it into one of four paints. The result, `livery_plan.png` here, is a
flat-colour plan at one pixel per centimetre that the import paints from.

- Only the port half of the photograph is read, and the scheme is mirrored.
- The fin stands over the spine in the photograph, so the spine aft of the wing
  is plain navy, and the fin's sides, which the photograph does not show, carry
  a navy, cream, grey and green flash that is this project's own.
- Everything below the wing line is the scheme's pale grey.
- The donor's panel lines and serial are carried over; its roundels, badge and
  fin flash are painted out. Stencils in the photograph are not reproduced.
- The donor's planform differs a little from the real aircraft's, most at the
  tailplane tips, so the pattern is cut off there where the model is narrower.

## Known approximations

- The simulated CG is placed 0.55 m ahead of the main axles on the thrust line.
  Nothing published fixes it.
- The nozzle is a fixed part of the airframe and does not open with reheat.
- The donor has no airbrakes.
- Retraction is a single fold per leg and is not the real mechanism.
- The model's own two underwing pylons per side stay bare; missiles hang from
  the pylons the renderer draws.
