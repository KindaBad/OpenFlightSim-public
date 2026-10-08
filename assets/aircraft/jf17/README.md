# JF-17 runtime asset

Donor: [JF-17 Thunder with LS-6 by Jeyhun1985](https://sketchfab.com/3d-models/jf-17-thunder-with-ls-6-10ee4421a360468ebc21c65bdb780c06),
Sketchfab model `10ee4421a360468ebc21c65bdb780c06`, published 2026-08-03 and
listed as CC BY 4.0. The project owner supplied the download on 2026-10-08. The
listing's license, its 316,220-triangle count and its description, "JF-17
Thunder with LS-6 gps-guided bomb from War Thunder", were read the same day
through Sketchfab's API. The archive (SHA-256 `6da5c8ba…9987d7`) holds an OBJ,
its material file and loose textures, and no license text. Credit, terms and
the open question about the artwork's origin are in `licenses/assets/JF17.md`
and `docs/ASSET_RELEASE_PROVENANCE.md`.

`jf17_lod0.glb` through `jf17_lod3.glb` are generated and not version
controlled. Rebuild them headless from the unmodified donor:

```sh
blender -b --factory-startup --disable-autoexec \
  --python scripts/jf17_donor_import.py -- --source /path/to/JF-17.obj
```

The textures must lie beside the OBJ, as they do in the inner `source/JF-17.zip`.
The script writes the four GLBs and `lod_stats.json` here and an editable
`output/JF17_Thunder_donor.blend`. It is deterministic and safe to repeat.

## What the import changes

- The donor is already in metres and is not rescaled. It measures 15.19 m over
  the pitot boom against the published 14.93 m, and 9.41 m across the wingtip
  rails against 9.44 m.
- The listing's four LS-6 bombs, its targeting pod, the two inboard and the
  centreline pylons and two placeholder meshes are left out. The outer pylons
  stay, for the radar missiles.
- The donor has no rig and is modelled with the undercarriage stowed and every
  door shut. Each main leg carries two pins that give its trunnion axis; the
  leg is swung 81.8 degrees about them until the wheel is at the bottom of its
  arc. That alone brings the stowed, canted wheel upright with its axle within
  1.6 degrees of lateral, which is why the single swing is taken to be the real
  mechanism. The nose leg swings 97.8 degrees forward and down about the clevis
  at the front of its barrel and is lengthened 4.9 cm so all three tyres share
  one plane. Retracting plays the same swings backwards, so the stowed pose in
  flight is exactly the donor's.
- Wheel track is then 2.34 m and wheelbase 5.15 m. The aircraft stands 5.37 m
  high on fully extended oleos; the published height is 4.77 m. The airframe is
  not squashed to hide that difference.
- Nose and main doors are opened about their long edges, 88 and 115 degrees.
- Tailplanes, rudder, ailerons, flaps, leading-edge flaps, four airbrake panels
  and 48 of the 84 nozzle parts are grouped under pivots with the channel names
  the renderer animates. No surface is re-modelled.
- Glass and lens materials have no runtime equivalent and are replaced by
  plain ones. The 2048 px colour and normal maps are embedded unchanged in LOD0.

LOD triangles: **93,977 / 40,199 / 16,668 / 8,491**, from the donor's 260,814
without stores. Every level, the nearest included, is Blender collapse
decimation of the donor meshes; reduced levels reuse LOD0 materials by name.

## Known approximations

- The simulated CG is placed 0.55 m ahead of the main axles on the thrust line.
  Nothing published fixes it.
- The retraction jack in the nose bay stays where the donor has it, so it does
  not follow the leg.
- The main wheel doors hang from their inboard edge; the donor gives no hinge.
- Airbrake actuator links stay inside the fuselage when the panels open.
- Markings are the donor's Pakistan Air Force 11-134 "Black Panthers" scheme.
