# Su-57 runtime asset

Donor: [Sukhoi Su-57 Felon - Fighter Jet - Free by bohmerang](https://sketchfab.com/3d-models/sukhoi-su-57-felon-fighter-jet-free-59995d6f34ba4bb7990195be3a745fc5),
Sketchfab model `59995d6f34ba4bb7990195be3a745fc5`, published 2023-07-20 under
CC BY-NC-SA 4.0. The project owner supplied the download on 2026-10-07; the
listing's license and its 31,929-triangle count were read the same day and match
the archive. The archive contains one packed Blender file (SHA-256 `bf5ee985…bfcfcb`),
loose copies of its textures and no license text, so the listing is the license record.

It replaces the earlier CGTrader donor (SU57-Felon by lullabie), whose authoring
stages remain in `scripts/su57_{normalize,rig,cockpit,correction,geometry_detail,export}.py`
and are no longer part of the runtime model.

`su57_lod0.glb` through `su57_lod3.glb` are generated and not version
controlled. Rebuild them headless from the unmodified donor:

```sh
blender -b --factory-startup --disable-autoexec \
  --python scripts/su57_donor_import.py -- --source /path/to/su57.blend
```

The script writes the four GLBs and `lod_stats.json` here and an editable
`output/Su57_Felon_donor.blend`. It is deterministic and safe to repeat.

## What the import changes

- Length is scaled to the published 20.1 m (0.10261 m per donor unit). The donor
  is 3.0 % wider for that length than the published 14.1 m span, so its lateral
  axis alone is narrowed by 0.9698. Span, wingtips and engine spacing then agree
  with `data/physics/su57.json`; every hinge and fold is rigged after that
  correction, so moving parts stay rigid.
- The aircraft stands with its nozzle centres on the simulated thrust line,
  2.29 m above the ground. That raises the main legs 0.08 m into their wells and
  extends the nose oleo 0.23 m (a vertical stretch of the piston only) so all
  tyres rest on one plane. Overall height measures 4.61 m against the 4.6 m target.
- The donor already separates its LEVCONs, leading-edge flaps, flaperons,
  ailerons, stabilators, nozzles, gear and doors. They are grouped under pivots
  with the channel names the renderer animates. The one cut is each fin blade,
  which the donor welds to its fixed root fairing.
- Thrust-vectoring pivots use the same canted axes as the flight model.
- Each main door hinge is fitted so the open door lands on the donor's own
  flush gear-up skin; the residual is 3.4 cm RMS.
- Glass BSDF and transmissive HUD materials have no runtime equivalent and are
  replaced by alpha-blended materials. The 4096 px base colour, normal and
  metallic/roughness atlas is embedded unchanged in LOD0.

LOD triangles: **31,553 / 18,960 / 8,395 / 2,284**. Reduced levels are Blender
collapse decimation of the same meshes and reuse LOD0 materials by name.

## Known approximations

- `data/physics/su57.json` takes its gear contact points and its stabilator,
  aileron, fin and LEVCON hinge stations from this model, so the simulated
  wheelbase is the donor's 6.25 m.
- Nozzle centres are 1.41 m from the centreline; the simulated engines and the
  exhaust plumes are at 1.34 m.
- Retraction is one 90 degree forward fold per leg. The stowed main wheels also
  slide 0.9 m inboard, into space the donor leaves empty behind its blanked
  intake ducts. The forward nose doors stay shut, as the donor models them, so
  the nose wheels pass through them while the gear is travelling.
- The boom tail cone beside each nozzle moves with its stabilator, because the
  donor's stabilator extends beneath it.
- Nozzle petals do not open with reheat.
- Markings are the donor's Russian Aerospace Forces "051" scheme.
