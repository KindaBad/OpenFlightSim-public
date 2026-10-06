# Su-57 local asset integration

Starting asset: supplied `.blend` specified by the `--source` pipeline argument, preserved intact.
On 2026-10-06 the user identified the donor as
[SU57-Felon by lullabie, CGTrader model 5684228](https://www.cgtrader.com/free-3d-models/aircraft/military-aircraft/su57-felon)
and supplied the listing's license screenshot. The listing displays Royalty Free
License (no AI). [CGTrader's terms](https://www.cgtrader.com/pages/terms-and-conditions)
allow incorporated game use subject to protecting the asset, and prohibit
standalone model redistribution. This identifies the supplied source/license;
it does not approve publishing the Blender source or loose derivative GLBs.

The current runtime loads ordinary GLB files. Public game packaging still needs
asset safeguarding and recorded approval for that distribution format. Public
source distributions exclude donor geometry/textures. Older private Git history
contains donor models and must not be included in the clean public baseline.

Working source: `output/Su57-Felon.blend`, edited through live Blender MCP.
Metric geometry measures 20.1 m length, 14.1 m span, 4.6 m extended-gear height.
Dimensions use public CAD research anchors; the rig, gear, close-up mechanical
reconstruction and hidden structures are engineering visual estimates.

LOD0 retains the supplied six exterior body/engine PBR images. Imported cockpit
artwork named for an F-14 is not used by the runtime model; owned materials and
static display geometry replace it. The original source collections remain in
the working .blend for recoverability and retain their original unknown status.

1,551 additional real geometry components are grouped into 51 rig-aware meshes:
hollow spoked hubs, brake stacks/calipers, clevis plates/pins, seals/collars,
actuator barrels/pistons, hydraulic fittings/hoses, door hinges/ribs, skin seams,
inspection-cover rims and millimetre flush fasteners. Camouflage and microscopic
surface variation remain textures. Fine parts are excluded beyond LOD0/1.

GLBs 1–3 reuse LOD0 textures by material name. The four `lod*_stats.json` files
record evaluated geometry; runtime mesh merging can reduce the draw count.
See `docs/M3_68_SU57_REALISM_GRAPHICS_VALIDATION.md` for physics, provenance,
approximation labels, measured validation and remaining limitations.

Normalization is anisotropic: the source length/span anchors (17.1699963 m /
12.3671279 m) are scaled to 20.1 m / 14.1 m. This imposes approximately +17.06%
length and vertical scaling, +14.01% span scaling, and -2.61% span relative to
uniform length scaling. The generated `normalization_deformation.json` records
exact factors and volume deformation. The existing baseline calculation is also
recorded in `data/geometry/su57-normalization.json`. These are visual dimension targets, not
an OEM geometry validation.

Pipeline paths are arguments: `--source`, `--working-output`, `--output-dir`,
`--report-dir`, and `--project-root`. Invoke Blender with arguments after `--`.
Follow-on rig/detail/export stages share the resolved pipeline namespace.
