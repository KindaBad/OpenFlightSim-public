# Production-content closure and release provenance

The source repository excludes generated GLBs, Blender files, large runtime
textures, caches and test output. A source/core test run is insufficient evidence
that a production aircraft model was inspected. Content suites now return **77**
for unavailable optional content, with CTest `SKIP_RETURN_CODE=77`; partial packs
are reported as incomplete. Malformed content still fails even in optional mode.
Synthetic loader/batching tests run separately from production model acceptance.

`OFS_REQUIRE_PRODUCTION_ASSETS=ON` requires every registered aircraft model and
authored LOD. `OFS_PRODUCTION_ASSET_ROOT` selects the installed pack root, defaulting
to the repository root. Required mode never skips an absent model. Tests reject
malformed GLBs, omitted/unreadable primitive or image content, missing material
references, expected rig channels, invalid hierarchy/transform data, bad LOD
reduction/identities, and physical dimension errors. Supported optional clearcoat
and emissive-strength omissions remain documented renderer limitations.

Bounds are calculated from transformed GLB vertex positions, in asset coordinates
X aft, Y up, Z port. Length/span/extended-gear height are compared in metres and
percent, with the pre-existing **0.15 m** geometry/export acceptance unchanged.
The rest-pose pack contains aircraft only; if a future pack contains stands,
weapons or other outlying geometry, add explicit airframe measurement metadata
and independently test those anchors rather than relaxing tolerances. Passing
imposed dimensions proves export scale, not accurate aerodynamic planform.

## Su-57 replacement and normalization

On 2026-10-06 the user supplied the official Sketchfab ZIP for
[Sukhoi Su-57 Felon - Fighter Jet - Free by bohmerang](https://sketchfab.com/3d-models/sukhoi-su-57-felon-fighter-jet-free-59995d6f34ba4bb7990195be3a745fc5).
The public Sketchfab model API reports **CC BY-NC-SA 4.0**. The supplied ZIP and
embedded Blender file are hash-recorded in
`data/geometry/su57-sketchfab-source.json`. This is the active donor.

[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/)
allows sharing and adaptations for noncommercial purposes, with attribution,
license links, indication of changes and the same license for adaptations.
`licenses/assets/SU57.md` records the creator/source, our modifications and the
adaptation license. CMake installs this notice under `licenses/assets/`.
Loose model files may be distributed under these terms; asset encryption is
unnecessary and must not restrict the rights granted by this license.

`scripts/import_su57_sketchfab.py` opens only the supplied replacement, preserves
its UVs/packed images, separates controls, rigs gear and exports four native GLBs.
The working scene is `output/Su57-Sketchfab.blend`; runtime paths stay
`assets/aircraft/su57/su57_lod0.glb` through `su57_lod3.glb`. Generated assets stay
local under the source-only policy and belong in reviewed game/asset releases.
The pipeline writes `output/su57-sketchfab/asset-approval-su57.json` with exact
export hashes; this approves only these four Su-57 derivatives for noncommercial
packaging. The other registry models still require their own approval records.

Measured source dimensions are 195.896271 / 141.704758 / 45.673218 in the donor's
authoring units. Scale factors in source longitudinal/span/vertical order are
0.102605321 / 0.099502660 / 0.100715481 metres per source unit. The imposed metre
bounds are 20.1 / 14.1 / 4.6. Span is -3.023878% and vertical -1.841854% relative
to uniform length conversion. `data/geometry/su57-normalization.json` records this
active baseline. Gear contact/radius corrections are additional local edits.

Existing physics contacts and hinge anchors are preserved. The donor nose gear
moves forward 2.482 m and down .303 m; main gear moves aft .562 m and inward
.099 m. Wheel radii are fitted to .33/.515 m. The original mesh's bay positions,
surface cuts, pivots and gear folding/compression remain engineering visual
approximations. Passing dimension/hinge checks is not OEM geometry validation.
See `assets/aircraft/su57/README.md` for reproduction and limitations.

### Retired donor

The former CGTrader donor was identified as
[SU57-Felon by lullabie, model 5684228](https://www.cgtrader.com/free-3d-models/aircraft/military-aircraft/su57-felon).
Its original/modified Blender scenes and archived exports remain local/private;
none of its geometry or textures are used by the replacement pipeline.
[CGTrader's terms](https://www.cgtrader.com/pages/terms-and-conditions)
forbid standalone redistribution of that donor. Its older asset-bearing Git
history remains private. The previous deformation audit is preserved in
`data/geometry/su57-cgtrader-normalization-historical.json`; older validation
documents describe that historical artwork rather than the new model.

## Texture costs and renderer terminology

Every uploaded aircraft texture now logs its source/embedded image identifier,
source and uploaded dimensions, RGBA8 runtime format, mip count, and estimated
GPU bytes. Logs include aircraft totals, all loaded image-resource totals
(including white/font/cloud-noise), and the largest aircraft uploads. Framebuffer
attachments, geometry and driver allocation/alignment are excluded and identified.
Authored LODs reuse LOD0 material/texture handles. CPU diagnostics use the same
role and mip-cost functions, preserve sampler distinctions, and apply the default
2K cap; texture quality and content are unchanged.

Block-aware cost accounting supports BC7/BC5 (16-byte 4x4 blocks) and BC4 (8-byte
4x4 blocks), including small/odd mip tails. This is compression preparation,
**not** a compressed asset decoder/upload implementation. A future path should
supply authored offline mip chains, select formats by texture role and GPU caps,
and transcode KTX2/Basis containers before estimating resident format cost.
RGBA8 uploads, color/normal filtering, and current quality settings remain active.

Exhaust “heat haze” remains translucent animated **schlieren bands**. It does not
sample/refract the scene framebuffer. True scene-refraction distortion belongs to
a later renderer milestone; no framebuffer architecture was changed here.
