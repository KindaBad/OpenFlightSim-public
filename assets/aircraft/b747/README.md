# Boeing 747-206B runtime asset

Donor: [Royal Dutch Airlines Boeing 747-206B by Nobilis 2](https://sketchfab.com/3d-models/royal-dutch-airlines-boeing-747-206b-829cd4d3ccb6404fb503cb4c9115f3da),
Sketchfab model `829cd4d3ccb6404fb503cb4c9115f3da`, published 2026-07-20 under
CC BY 4.0. The project owner supplied the download on 2026-10-14. The listing's
licence, author and 9,328-face count were read the same day through Sketchfab's
API. The archive `royal-dutch-airlines-boeing-747-206b.zip` holds one FBX, two
4096 px textures and no licence text, so the listing is the licence record.
Credit and terms ship in `licenses/assets/B747.md`.

`b747_lod0.glb` through `b747_lod3.glb` are generated and not version
controlled. Rebuild them headless from the unmodified donor:

```sh
blender -b --factory-startup --disable-autoexec \
  --python scripts/b747_donor_import.py -- --source /path/to/source/747klm.fbx
```

The archive's `textures` folder must lie beside `source`. The script writes the
four GLBs and `lod_stats.json` here and an editable `output/B747_donor.blend`.
It is deterministic and safe to repeat.

## What the import changes

- The donor is placed with its nose toward -Y, starboard toward -X and up
  toward +Z. It becomes nose at x = 0, aft +x, starboard +y, up +z from the
  ground.
- The scale is one uniform factor, 1.00585 m per donor unit, set by the
  published 70.66 m length. The span then measures 59.38 m against the published
  59.64 m; the flight model uses the published span.
- The ground is placed so the fin stands the published 19.33 m above it.
- The donor is modelled in flight and has no undercarriage. One is built from
  cylinders and turned tyre and wheel sections: a two-wheel nose gear 6.0 m aft
  of the nose, two four-wheel wing gears 0.6 m aft of the centre of gravity and
  5.5 m either side, and two four-wheel body gears 3.4 m aft of it and 1.92 m
  either side, all on 49 x 17 tyres, with oleo pistons, torque links and braces.
  On `gear_fold` the wing gears swing inboard and the body and nose gears aft
  (the real ones fold forward; there is no room for that here), each rising as
  it folds until every part of it lies inside the skin: the import measures this
  and fails if any part would show. Doors under the wells, textured from the
  skin they cover, close on `gear_door` once the gear is up. The wheels spin on
  `wheel` and `nose_wheel` pivots and the nose gear steers on `steering`.
- The donor's triple-slotted flaps are separate panels at the trailing edge,
  inboard and outboard of the inboard engine. They are kept whole and turn on
  `flap` by 30 degrees at full travel about a line 0.35 m under their forward
  edge, so they droop from the wing with a narrow slot, and the aft halves of
  the flap track fairings under them turn with them. The donor has no separate
  aileron, elevator or rudder, and its wing and tail skins have only a few dozen
  vertices, so a single face can span most of the chord. Those surfaces are cut
  out of the skin with planes, along the hinge line and across both ends, and
  each turns about its own hinge line, which follows the sweep and dihedral:
  the ailerons (`aileron_L`, `aileron_R`) 22 % of the chord ahead of the
  trailing edge from 21.8 m to 28.6 m out, the elevators (`elevator`) 32 % of
  the tailplane chord ahead of it, and the rudder (`rudder`) 30 % of the fin's
  chord ahead of it. The hinge lines are estimates from the donor's geometry,
  not the real aircraft's. The leading-edge Krueger flaps stay fixed.
- Six spoilers a wing, two inboard and four outboard of the inboard engine,
  are plates laid 12 mm over the upper skin where its livery already shows
  spoiler panels, textured from it and raised up to 45 degrees on `spoiler`.
- The distant levels of detail carry coarse tyres and wheels and leave out the
  torque links, braces, axles and spoilers.
- The livery atlas is kept as the donor's own base colour. The grey detail map
  is not used. The four engine nacelles are part of the airframe.
- Reduced levels are Blender collapse decimation of the airframe; the
  undercarriage is kept whole in every level.

## Measured from the donor

The wing is measured by casting rays through the donor's own faces, because its
vertices fall only at the ribs. The fuselage sections are measured the same way.

| Quantity | Donor | Published / used |
|---|---|---|
| Length | 70.66 m | 70.66 m |
| Span | 59.38 m | 59.64 m (used) |
| Planform area, both halves | 413 m² | 511 m² (used) |
| Mean aerodynamic chord | 8.28 m | 8.3 m (used) |
| Centre of gravity | 30.9 m aft of the nose, 25 % MAC | from the donor |
| Wing plane above the ground | 5.12 m | the centre of gravity height |
| Fin top above the centre of gravity | 14.21 m | 14.21 m |

The donor's planform comes out about 19 % smaller than the published area. The
flight model uses the published figure; the donor is only used for shape.

LOD triangles: **25,242 / 21,919 / 9,581 / 6,336**.

## Known approximations

- The simulation has two engine slots; each stands for the pair of engines under
  one wing, midway between the inner and outer nacelle.
- The centre of gravity is at 25 % of the mean chord measured from the donor,
  about 30.9 m aft of the nose, which is further aft than a published 747 CG.
  The simulated main contacts are 2 m aft of it and the nose gear 25 m ahead.
- The undercarriage is a visual approximation of the 747's gear, not its
  geometry; the wells inside the doors are not modelled. The simulation has one main contact each side, 2 m aft of the
  centre of gravity and 5.8 m out, between a wing gear and a body gear.
- Wing area, inertia, drag, thrust lapse, control response and the wave-drag
  fit are engineering estimates set against the published speeds. See
  `docs/AIRCRAFT_REFERENCE.md`.
