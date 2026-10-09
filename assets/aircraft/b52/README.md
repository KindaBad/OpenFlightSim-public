# B-52 runtime asset

Donor: [Boeing B-52 Stratofortress by bohmerang](https://sketchfab.com/3d-models/boeing-b-52-stratofortress-38b0c64bd552431394efa8625d7f5144),
Sketchfab model `38b0c64bd552431394efa8625d7f5144`, published 2023-09-12 under
CC BY 4.0. The project owner supplied the download on 2026-10-09. The listing's
license and its 16,616-triangle count were read the same day through
Sketchfab's API; the count matches the archive. The archive (SHA-256
`0ec8bddb…252131`) holds one Blender file and twenty textures and no license
text, so the listing is the license record. Credit and terms ship in
`licenses/assets/B52.md`.

`b52_lod0.glb` through `b52_lod3.glb` are generated and not version
controlled. Rebuild them headless from the unmodified donor:

```sh
blender -b --factory-startup --disable-autoexec \
  --python scripts/b52_donor_import.py -- --source /path/to/source/B-52.blend
```

The archive's `textures` folder must lie beside `source`. The donor file is
opened with its scripts disabled. The script writes the four GLBs and
`lod_stats.json` here and an editable `output/B52_Stratofortress_donor.blend`.
It is deterministic and safe to repeat.

## What the import changes

- Scale is one uniform factor, 0.9942 m per donor unit, set by the published
  48.5 m length. Span then measures 55.66 m against the published 56.4 m; the
  flight model uses the published span and wing area.
- The ground is placed so the fin stands at the published 12.4 m.
- The donor is modelled in flight and has no undercarriage. Four two-wheel
  main trucks and two outrigger wheels are built from plain cylinders. The
  trucks stand 1.05 m either side of the centreline, not the real 1.25 m,
  because that is what fits inside this fuselage when they are stowed. They
  fold fore and aft into the fuselage, which has no doors or wells modelled;
  the outriggers swing out to lie against the underside of the wing.
- The flaps are separate shells in the donor and are hinged at their forward
  edges, 30 degrees at full travel.
- The B-52H rolls with spoilers and has no ailerons. A strip of each outer
  wing, the aft 24% of the chord between 19.6 and 24.2 m from the centreline,
  is cut out and hinged as a roll surface so the model answers the stick.
- The whole tailplane turns about a lateral shaft by 0.35 of the commanded
  elevator angle. The rudder turns about its leading edge.
- Each of the eight fans turns about its own centre, four to an engine slot.
- The donor's twenty textures and its materials are kept. Only the glazing is
  remade, as glass.

LOD triangles: **19,242 / 11,777 / 4,733 / 1,976**. Reduced levels are Blender collapse decimation and
reuse LOD0 materials by name.

## Known approximations

- The simulation has two engine slots; each stands for the four engines under
  one wing, at the point midway between that wing's two pods.
- The simulated aft gear contacts are 3 m either side of the centreline and
  the struts are stiffly damped, in place of modelling the outriggers as
  load-bearing gear. The outriggers only catch a dropped wing.
- The wing is set 3.5 degrees nose-up on the fuselage in the flight model,
  against the real 6, so the fast end of the envelope stays trimmable.
- There is no bomb bay: bombs appear under the fuselage as they are released.
- The wings do not flex or droop.
