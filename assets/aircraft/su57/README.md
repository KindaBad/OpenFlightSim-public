# Su-57 Sketchfab replacement

Current artwork: **Sukhoi Su-57 Felon - Fighter Jet - Free** by **bohmerang**.
Original: https://sketchfab.com/3d-models/sukhoi-su-57-felon-fighter-jet-free-59995d6f34ba4bb7990195be3a745fc5
Creator: https://sketchfab.com/bohmerang
License: **CC BY-NC-SA 4.0**, https://creativecommons.org/licenses/by-nc-sa/4.0/
The adapted aircraft artwork is distributed under the same license.
See `licenses/assets/SU57.md` for attribution, modifications and terms.

On 2026-10-06 the user supplied the official downloaded archive. Its SHA-256 and
Blender-file SHA-256 are recorded in `data/geometry/su57-sketchfab-source.json`.
The Sketchfab API independently reports bohmerang and CC BY-NC-SA 4.0.

## Rebuild

Extract the downloaded ZIP locally. The Blender source has packed images; its
separate texture directory can remain beside the source. From the project root:

```sh
blender --background --disable-autoexec --python scripts/import_su57_sketchfab.py -- \
  --source /path/to/extracted/source/su57.blend
```

The original source stays intact. The pipeline writes a new working copy to
`output/Su57-Sketchfab.blend`, GLBs to this directory, and normalization/export
reports plus hash-bound Su-57 release approval to `output/su57-sketchfab/`.
Optional arguments: `--output-dir`, `--working-output`, `--report-dir`.
Blender and the official source archive are only needed to regenerate assets;
players receive the generated GLBs through game releases.

## Runtime adaptation

The aircraft is fitted to 20.1 m length, 14.1 m span and 4.6 m extended-gear height,
with nose at asset X=0 and ground at asset Y=0. Length/span/vertical normalization
is nonuniform: span is 3.024% smaller and vertical 1.842% smaller than uniform
length scaling. This matches the simulator registry, not independent OEM geometry.

The donor's welded airframe is separated into stabilators, canted fins, ailerons,
flaps, slats, LEVCON surfaces and nozzle shells. Original UVs and packed images
are retained. Rigid pivots use the existing engineering physics anchors. Donor
gear and wheel radii are fitted to the existing simulation contacts; the nose
assembly moves forward about 2.482 m and down 0.303 m, and the main assemblies
move aft 0.562 m. Panel cuts, hinges, bay alignment and folding/compression poses
remain approximations. Nose retraction adds an estimated .55 m upward stow
translation. The flight model is unchanged. Glass is converted to
renderer-supported alpha-blended PBR, without unsupported material extensions.

LOD triangles: **52,185 / 26,084 / 10,422 / 3,360**, with 59 exported nodes.
LOD0 embeds body color, normal and packed metallic/roughness images. LOD1–3
retain rig/material identities and share LOD0 textures in the renderer.

The old CGTrader airframe, cockpit, geometry details and textures are not opened,
loaded or copied by this pipeline. Their former exports are preserved locally
under `.cache/su57-sketchfab/retired-local-exports/`; the historical working scene
remains local. The older `su57_normalize`, `su57_session`, `su57_rig` and related
scripts describe the retired donor and must not be used to rebuild this model.

Generated models and Blender sources remain outside the source-only Git
repositories. Noncommercial releases may distribute these loose GLBs with the
credit/license notice; preserve CC BY-NC-SA rights and license model adaptations
likewise. Other aircraft need their own provenance and approval.
