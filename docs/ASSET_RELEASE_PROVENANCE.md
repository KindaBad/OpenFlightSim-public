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

## Su-57 normalization

The original local donor is `/home/kindabad/Downloads/Su57-Felon.blend`, recorded
in the existing source inventory. Working copy: `output/Su57-Felon.blend`;
exports: `assets/aircraft/su57/su57_lod0.glb` through `su57_lod3.glb`.
The original audit could not identify the donor's author or license from embedded
metadata. On 2026-10-06 the user supplied the original listing and license screenshot:
[SU57-Felon, model 5684228, by lullabie](https://www.cgtrader.com/free-3d-models/aircraft/military-aircraft/su57-felon).
The listing currently displays Royalty Free License (no AI).

[CGTrader's terms](https://www.cgtrader.com/pages/terms-and-conditions) permit use
inside an incorporated game while requiring reasonable asset safeguarding. They
do not permit giving away the standalone model file. Modifications and a free
game do not remove that distinction. The user-supplied listing identifies the
intended donor; the source has not been re-downloaded and byte-matched against
the local original.

**PUBLIC SOURCE EXCLUDED / CURRENT LOOSE-GLB RELEASE NOT APPROVED.** The runtime
and release packager currently store ordinary derivative GLBs, so they need a
protected asset packaging/loading path before approving this donor for game
distribution. Keep Blender sources and loose GLBs out of the public repository
and public repair/download file indexes. Preserve the license/provenance evidence
and review the incorporated package format when completing asset approval.
Original cockpit artwork with F-14 names is preserved locally but is not used in
runtime exports. Other pack assets and library notices still require review.

The existing normalization baselines are 17.1699963 m length, 12.3671279 m span,
2.78005594 m donor height. They are recorded authoring measurements, **not a fresh
measurement of the original donor in this milestone**. The length/span targets
are 20.1/14.1 m; assembled extended-gear height is 4.6 m. Source axes (+X port,
-Y nose, +Z up) differ from canonical authoring axes; corrections in canonical
length/span/vertical order are **1.1706467313 / 1.1401192030 / 1.1706467313**.

Thus donor length/vertical increase 17.0647%, span 14.0119%; span is compressed
**2.6077% relative to uniform length scaling**, with volume scaling 1.5624351.
Uniform length scaling would produce about 14.4775 m span, approximately .3775 m
too wide for the configured target. This alters sweep/aspect ratios and is a
meaningful surrogate-shape uncertainty. The donor height is not directly scaled
to 4.6 m: gear/reconstruction and vertical positioning produce the final height.

The exporter already bakes this normalization into the working geometry. Runtime
registry model scales are (1,1,1), not an additional anisotropic correction.
For a licensed replacement/full-content pipeline, measure source airframe bounds
and gear pose, choose true metre units, prefer a uniform unit conversion, correct
planform/gear once in Blender against justified anchors, apply transforms, and
export all LODs consistently. Preserve donor provenance and compare local
cross-sections/hinges as well as bounds; dimension-fitting alone cannot establish
OEM fidelity. No artwork was changed by this milestone.

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
