# Su-57 Sketchfab replacement — 2026-10-06

The runtime Su-57 now uses the user-supplied official Sketchfab download by
bohmerang, with CC BY-NC-SA 4.0 attribution and the same license for the adapted
artwork. The source/ZIP hashes are recorded in
`data/geometry/su57-sketchfab-source.json`. No CGTrader geometry, textures,
cockpit artwork or mechanical meshes are carried into this replacement.

`scripts/import_su57_sketchfab.py` regenerates the working Blender file and four
native GLBs from the new source. UV-preserving planar cuts separate welded
surfaces before rigid animation. The original downloaded file remains intact.
Flight-model parameters and registry paths stay the same.

## Measured results

- Dimensions: **20.099998 × 14.100000 × 4.600000 m** (length/span/gear height).
- LOD triangles: **52,185 / 26,084 / 10,422 / 3,360**.
- Exported hierarchy: **59 nodes**, including every existing Su-57 control,
  gear, wheel, compression and independent vector-nozzle channel.
- LOD0: three embedded PBR images, approximately **23.7 MB** GLB, compared with
  the retired local model's approximately 89.6 MB. Reduced LODs share the LOD0
  texture/material table at runtime.

## Checks

`su57.assets`, `visual.hierarchy`, `assets.production` and
`aircraft_audit.geometry` pass with the actual local content for all registered
aircraft. Su-57 checks cover metre bounds, all required rig channels, actual
PBR texture bindings, progressively reduced LODs, material/control identities,
independent nozzle animation, physical wheel compression and fully retracted
strut clearance. Geometry auditing intersects triangle edges with wing section
planes, so it also measures sparse donor meshes without requiring a vertex to
be near each section station.

All 75 launcher tests pass. A fresh source-only CMake configure/build/install
confirms `licenses/assets/SU57.md` is included in the install. Native renderer
captures inspect parked, deflected-control, gear-up and vectoring poses. Local
captures/logs are under ignored `output/su57-sketchfab/`; validation transcripts
are under `.cache/su57-sketchfab/`.

The first public Windows build (Actions run 37486531313) exposed bare `return;`
statements in the flame shaders' `main`. bgfx translates the vertex entry point
to an HLSL function returning an output structure, so these statements fail
DXBC compilation. Both shader entry points now use complete conditional branches
and finish through bgfx's generated return path. All shipped GLSL shaders compile
with the pinned shaderc, and the GPU shader-conformance fixture passes against
the newly compiled binaries. Native Windows DXBC verification is delegated to
the automatically triggered public Windows build; it is not claimed as a local
Windows test.

The import pipeline generates the exact hashes in
`output/su57-sketchfab/asset-approval-su57.json`. A replacement-only pack is
prepared locally at `build/su57-sketchfab-pack.zip`, containing the four GLBs,
their approval and the credit/license notice. It is not a full game pack.

## Limits

Dimension fitting is anisotropic, and donor gear is repositioned to existing
simulation contacts. Surface cuts, hinge locations, doors/bays, wheel-radius
fitting and folding/compression are visual engineering approximations. The
model has no new OEM geometry or aerodynamic validation. Other aircraft and
runtime dependencies need their own release approval/notices. Generated GLBs,
Blender sources and preview images remain outside source-only Git delivery.
