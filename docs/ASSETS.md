# Airbus A320 — Blender exterior model

A full-scale exterior reconstruction of the sharklet-equipped A320ceo in the supplied photograph, with CFM56-style engines and the F-WWIO Airbus demonstrator markings. Created and refined in the connected Blender scene through Blender MCP.

## Deliverables

- `output/Airbus_A320.blend` — editable Blender scene, organized by aircraft subsystem, with an assembly parent, procedural materials, studio lighting and six cameras.
- `output/Airbus_A320.glb` — portable aircraft-only export with evaluated geometry, including curve-based seams and hoses. Blender procedural shader detail is simplified to glTF materials.
- `output/A320_Hero.png` — main rendered view.
- `output/A320_Engine_Detail.png` — intake, fan and pylon close-up.
- `output/A320_Profile.png` — side view.
- `output/A320_Landing_Gear.png` — mechanical close-up.
- `output/model_report.json` — measured geometry bounds and model statistics.

The model uses metres. Its longitudinal axis is +X, vertical is +Z, and the port side is −Y. The nose is at X=0. Select `AIRBUS A320 | assembly` to move the complete aircraft. Presentation objects are grouped separately.

## Included detail

Lofted fuselage and tapered tail cone; six cockpit panes; paired cabin window rows; passenger, overwing exit and cargo door outlines; radome join and lightning diverters; swept airfoil wings, blended sharklets, spoiler/flap/aileron panel boundaries and flap-track fairings; horizontal and vertical stabilizers; hollow engine intakes, 36 individually modeled fan blades per engine, spinner spirals, acoustic duct segmentation, nacelle joints and exhaust sections; twin-wheel nose and main landing gear, oleos, braces, torque links, brake hoses, cast wheel details and tire grooves; antennas, pitot probes, navigation lights, static discharge wicks, fasteners, service stencils and reconstructed Airbus livery.

Rest configuration: landing gear extended, neutral control surfaces. M3.6 adds
named rigid articulation pivots, not a skeletal animation system. This exterior
model has no passenger cabin, flight-deck interior or manufacturing tolerances.
Primary dimensions are anchored to Airbus's public drawings; local contours,
mechanical details and livery are reconstructed approximations. Window glazing
and door seams are surface geometry rather than pressure-shell apertures.

## References

- Airbus, *A320 Aircraft Characteristics — Airport and Maintenance Planning*, July 2025, pages 46–47 (PDF page numbers): https://www.aircraft.airbus.com/sites/g/files/jlcbta126/files/2025-07/AC_A320_20250715.pdf
- Airbus A320ceo specifications: https://www.aircraft.airbus.com/en/aircraft/a320-family/a320ceo
- GKN Aerospace, CFM56-5B first-stage fan blade data (36 blades per engine): https://www.gknaerospace.com/media/iwxh5uum/cfm56-5b-1st-stage-fan-blade-m.pdf
- CFM International engine family reference: https://www.cfmaeroengines.com/cfm56/CFM56-5B-CFM56-7B
- Supplied F-WWIO photograph; supplementary online image searches for the demonstrator, nacelles and landing gear.

## Color management on this workstation

This workstation's Blender 5.2 package ships an OCIO 2.5 configuration alongside an OCIO 2.4 runtime. A project-local copy of the installed color configuration, with its header set to 2.4 and its lookup tables copied alongside it, is included in `output/color_management`. It was verified by rendering with AgX. System files were not changed.

Use `./open_aircraft.sh` to launch this scene with the compatible configuration. A normally configured Blender installation can open the `.blend` directly. The already-running Blender window may continue to use its fallback Standard transform until relaunched; the delivered PNGs use AgX.

## Reproduction

The modeling scripts in `scripts/01_airframe.py`, `02_mechanical.py`, `03_details.py`, `05_finish.py`, then `04_presentation.py` describe the build. They share a namespace retained in `bpy.app.driver_namespace['a320']` by the first script. The working scene also includes final camera/framing and small boundary refinements made through MCP. `scripts/finalize_and_render.py` exports the existing scene and renders the delivery views. Its paths are relative to this project. Run `./render_aircraft.sh` to regenerate those outputs.

## M3.6 runtime assets and articulation

The sole canonical runtime airliner is the regular Git file
`output/Airbus_A320.glb`. The old `assets/a320.glb` symlink was removed rather than
copying a second large model. CMake install retains the `output/` path. Names and
path case are identical on Linux/Windows; users do not need Blender or symlink
developer mode to run the game.

`scripts/animate_a320.py` opens the owned scene, replaces wing/tail foil geometry
with genuine separate control surfaces and creates useful pivots. Fuselage,
livery, nacelles and detailed mechanical parts are retained. Rigging is marked
idempotently in the scene. From the repository root:

```sh
blender -b output/Airbus_A320.blend --python scripts/animate_a320.py
```

On this workstation retain the existing compatible `OCIO` setting if rendering
Blender presentation images. Native GLB export does not depend on AgX output.
After rebuilding the original airframe scripts, apply `animate_a320.py` last.
The shared `rig_aircraft.py` exporter preserves parents, pivots and extras when
temporarily evaluating curve details for glTF.

A320 supports left/right ailerons, elevators, rudder, three flaps and five spoilers
per wing, all three gear assemblies, nested nose steering/wheels, main wheels,
and two fans. Gear-related doors follow the assemblies rather than separate door
sequencing. Hinge rotations and flap translation are game-level approximations;
the existing aerodynamic flap/spoiler equations are not a new detailed system.
GLB node extras define `ofs_channel`, `ofs_axis`, `ofs_gain` and `ofs_slide`.
The loader preserves names, parent/children, local transforms, meshes and materials.
Only compatible static batches merge; articulated ancestors stay independent.

Current A320 GPU LOD triangle totals are 1,117,326 / 90,361 / 11,867 across 91
batches. These supersede the historical unrigged model report's totals; the
original report/presentation images remain historical rather than new captures.

## Retired temporary aircraft

The generic Falcon and its authoring script were removed. Armed regression fixtures
use production engineering aircraft. The NASA/public F-16 reference remains separate
and is loaded only by the optional scientific validation target.

## Austrian Eurofighter Typhoon

`assets/typhoon/typhoon_lod0.glb` through `typhoon_lod3.glb` are original aircraft
meshes authored and exported in live Blender MCP. The editable, packed source is
`output/Typhoon_7L-WA.blend`. Target: Austrian 7L-WA, single-seat Tranche I/Block 5,
gray July 2007 delivery configuration with clean stores. Photographs were used
as references, never redistributed as textures or geometry.

LOD triangles: **220,472 / 95,833 / 38,479 / 7,281**. Reduced GLBs retain material
and rig names; runtime shares LOD0's maps and materials across all four tiers and
all instances. `lod_stats.json` records exact native export counts and bytes.
Original PNG base-color, metallic/roughness and normal maps live in `textures/`:
4096x2048 fuselage/wing sheets, 2048x2048 tail/markings and 1024x1024 nozzles.
All needed images are embedded in LOD0 and packed into the Blender source.

Run the authored modeling stages through Blender MCP, in the order recorded in
[M3_65_TYPHOON_VALIDATION.md](M3_65_TYPHOON_VALIDATION.md). Correction scripts are
one-time stages, not safe to repeat on an already corrected scene. Export uses
Blender's native glTF exporter, never handcrafted binary GLB. The report contains
sources, native screenshots, exact budgets, checks and visual approximations.

## What is not version controlled

Research-only files in `reference/`, including the Airbus reference PDF and
reference images, are kept locally and excluded from source publications. The
simulator, launcher, CMake configuration and automated source tests do not need
them. See [the public source publication notes](PUBLIC_SOURCE.md) for the clean
baseline and separation from older private history.

Runtime content is generated and is excluded by `.gitignore`, so a clone contains
sources, scripts, definitions, manifests and documentation only:

| Content | Rule | Regenerate with |
|---|---|---|
| Runtime models (`*.glb`) | `*.glb` | `scripts/{typhoon,sr71}_export.py`, `scripts/import_su57_sketchfab.py`; A320 comes from the author's Blender scenes |
| Blender sources (`*.blend`) | `*.blend` | Authored stages in `scripts/*.py` through live Blender MCP |
| Large textures (>= 1 MiB) | explicit paths in `.gitignore` | Exported from the models; runtime ships them embedded in LOD0 |
| Screenshots and contact sheets | `docs/images/` | `scripts/capture_*.py` |
| Validation logs and reports | `*.log`, `output/**/*.txt\|json\|csv` | The `ctest` and benchmark runs recorded in each validation document |

Consequences for a fresh clone:

- The build, the physics, network, protocol and tooling suites run normally.
- The aircraft asset suites (`typhoon.assets`, `sr71.assets`, `su57.assets`,
  `visual.hierarchy`, `client.asset`) print `[SKIP]` and pass rather than fail
  while the models are absent. Restore the models at the paths named by each
  `AircraftDefinition::modelAsset` in `core/src/aircraft_definition.cpp`.
- Validation documents cite evidence by name and location in prose; they do not
  link to files, because those files are regenerated locally.

The active Su-57 uses bohmerang's Sketchfab model under CC BY-NC-SA 4.0. Its
adaptations carry the same license; noncommercial releases must include
`licenses/assets/SU57.md`. See `assets/aircraft/su57/README.md` for importing the
official archive. The retired CGTrader donor stays local/private.
