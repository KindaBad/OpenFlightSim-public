# M3.67 SR-71 validation

Historical milestone report. The temporary Falcon has since been retired; current aircraft physics and evidence are documented in [AIRCRAFT_PHYSICS_AUDIT.md](AIRCRAFT_PHYSICS_AUDIT.md).
Status: implementation complete for the playable M3.67 SR-71 milestone. The
aircraft is a reference-driven reconstruction with documented approximations;
it is not claimed to be a 1:1 classified flight or inlet model.

## Pre-change audit and implementation plan (2026-10-02)

Starting from the clean completed M3.66 checkout. Preserve the fixed 120 Hz
authoritative simulation, internal 240 Hz substeps, NED/FRD coordinates,
component/derivative aerodynamics, mass/CG/inertia, actuator/FCS state, independent
engines, registry, GameNetworkingSockets protocol, GLB hierarchy and native
SDL3/bgfx renderer. Reviewed M3.66 and M3.65 validation, architecture/build/coordinate
documentation, definitions, simulator, atmosphere, renderer, pose/effect system,
network authority and test registration.

Integration findings:

- Registry, renderer model storage and world spawn maps already derive capacity
  from definitions. Type 4 can be added without a client-supplied asset path.
- Gun capability is optional; HUD and server fire already consult it. SR-71 must
  use no gun and needs a fourth-type/unarmed regression.
- Six aerodynamic contributors are a hybrid model, not a general panel solver.
  SR-71 needs delta/elevon allocation without invented canards or flaps.
- ISA clamps at 20 km; extend public standard-atmosphere layers to cover cruise.
- Existing generic engine lapse is inadequate for Mach 3 cruise. Add an optional,
  bounded public-data-inspired inlet/ram model, retaining other aircraft behavior.
- Hierarchy extras support rotating/sliding channels and nested gear/wheels.
  Add independent inlet channels using the same schedule as thrust evaluation.
- Four authored GLBs and material-name reuse are supported. Renderer already has
  layered translucent reheat shells; parameterize their dimensions for the J58.
- Cameras and 17 lightweight collision spheres are definition-owned. Combat
  remains authoritative and independent of visual mesh topology.
- Acceptance includes existing timing-sensitive network tests without suppression.

Order: (1) references/dimensions and Blender silhouette; (2) orthographic inspection
and corrections; (3) secondary geometry, rig, cockpit/gear/intakes, UV/PBR/livery;
(4) four LODs and Blender renders; (5) dedicated physics/registry/renderer integration;
(6) native captures, reference comparison and a deliberate correction; (7) measured
flight/network/performance/build/sanitizer acceptance and final evidence report.

## Target and evidence policy

SR-71A, USAF serial **61-7972**, Smithsonian-preserved USAF appearance as the
visual target, associated with its 6 March 1990 delivery. No NASA research payload
or trainer rear cockpit. Markings must follow photographs of this airframe;
other airframes supply only common structural references.

**A:** published measurements or photograph-supported features.
**B:** reconstructed contours, mechanisms, distributions or engineering estimates.
**C:** simulation handling/effect tuning. No exact engine deck, inlet control law,
operational performance or 1:1 surface fidelity is claimed.

Initial references:

1. [Smithsonian collection, 61-7972](https://airandspace.si.edu/collection-objects/lockheed-sr-71-blackbird/nasm_A19920072000)
   — identity and dimensions (collection page access via search; direct access
   currently returns 403). Photographs still require visual inspection.
2. [NASA TM-104330, Conners, 1997](https://ntrs.nasa.gov/api/citations/19970019923/downloads/19970019923.pdf)
   — fig. 3 arrangement, 107 ft 5 in length, 55 ft 7 in span; inboard/outboard
   elevons, inward-canted all-moving rudders, inlet and ejector description,
   nominal 34,000 lbf J58 class, Mach 3.2/85,000 ft envelope. Its proposed thrust
   enhancement is NOT part of this aircraft.
3. [NASA SR-71 facts FS-2008-6-030-DFRC](https://www.nasa.gov/wp-content/uploads/2021/09/495839main_FS-030_SR-71.pdf)
   — broad geometry, operating envelope and 80,000 lb fuel. The PDF's parenthetical
   kilogram conversions for gross/fuel weight are inconsistent with pounds;
   use SI conversion of the stated pounds, not those erroneous parentheticals.
4. [NASA/AIAA Blackbird design history, Merlin, 2009](https://ntrs.nasa.gov/api/citations/20090007797/downloads/20090007797.pdf)
   — structural/chine/thermal/nacelle references; no mesh or texture incorporation.
5. [SR-71 inlet engineering presentation](https://www.enginehistory.org/Convention/2014/SR-71Inlts/SR-71Inlts.shtml)
   — 26 in travel between Mach 1.6 and 3.2 (schedule shape is an approximation).

Dimension anchors: length 32.7406 m; span 16.9418 m; parked height about 5.6388 m.
Fuel capacity anchor: 36,287.3896 kg (80,000 lb); representative load is a separate
configuration decision. Wheelbase, sections and detailed stations are pending
photographic/arrangement reconstruction and will be classified B.

## Blender MCP evidence

Live connection checked: Blender 5.2.2 LTS, addon 1.6/protocol 5. MCP reports older
addon protocol but working execution/screenshot fallbacks. Initial scene contains
the startup cube, camera and light. Substantial asset work will run directly via
Blender MCP; retained Python scripts are authoring sources, not hand-built GLBs.

## Results pending

Geometry, textures, LOD counts, package sizes, measurements, captures and acceptance
results will be recorded only after execution and inspection.

### Additional primary reference and first inspection

[NASA/TP-2002-210718, Moes and Iliff, 2002](https://ntrs.nasa.gov/api/citations/20020057965/downloads/20020057965.pdf)
provides actual baseline flight-test mass/inertia and control data (not necessarily
identical to serial 61-7972's mission loading). Table 2: zero-fuel 60,728 lb;
Ix/Iy/Iz = 220,660/954,850/1,172,039 slug-ft², Ixz=19,200 slug-ft².
Reference aerodynamic area 1,605 ft² and chord 37.7 ft differ from some popular
geometric specifications; coefficient normalization must follow the selected
reference. Table 1 gives elevon and rudder limits/rates; text identifies a 3°
trailing-edge-up rig offset for outboard elevons. Figure 7 supplies loading trends.
Its LASRE geometry is used only for the underlying airframe outline, with all
experimental payloads excluded. Figure 4 distinguishes the fuselage-station
reference from the nose: FS102 body nose and FS1355 tail.

[NASA/TM-2000-209020 fig.6](https://ntrs.nasa.gov/api/citations/20000052206/downloads/20000052206.pdf)
provides a related YF-12C three-view for cross-checking general geometry. It is not
claimed as an exact drawing of 61-7972. Photographic views of the chosen airframe
remain the livery authority:

- [Udvar-Hazy frontal photograph, 2008](https://commons.wikimedia.org/wiki/File:SR-71_Blackbird_at_the_Udvar-Hazy_Center.jpg)
- [Orlando Suarez, 61-7972 photograph](https://www.jetphotos.com/photo/8033247)
- [Target aft-quarter photograph](https://aircrafttotal.nl/assets/images/Nieuwe%20map/SR71BlackbirdUsafusa1.jpg)
- [Target tail detail photograph](https://assets.hiwars.com/assets/4e31b56a-c331-4d53-aec9-89616fbc9d92.jpg)

These images were inspected in the browser; no photograph is incorporated in
runtime textures or redistributed as model artwork. Red 17972, red crest and
white circular skunk tail emblem are visible on the preserved target. Narrative
claims on third-party image-hosting pages are not used as performance evidence.

Baseline before code changes: **81/81 headless tests passed in 182.45 s**,
including network/combat impairment and both 180-second soaks. Starting commit
`ba1fb53`; raw log `output/m3_67/baseline_headless.log`.

Blender MCP created the separate aircraft scene and saved
`output/SR71_61-7972.blend`. Inspected initial top/side/quarter renders and corrected
oversized canopy, nacelles too far forward/too long, excessive engine spacing and
forward wing-root stations. `blockout_*` and `shape1_*` images preserve this first
silhouette correction. The post-engine correction pass then flattened the canopy roof/windshield toward the faceted A-model profile, applied the public 3-degree outboard-elevon rig and removed low-detail nozzle seams from LOD1. The corrected source was saved and all four GLBs were exported again through Blender MCP.

## Final acceptance record

Target: SR-71A USAF 61-7972/17972. Reference-supported features include the pointed nose and chines, broad delta wing, twin nacelles, canted rudders, tandem A-model cockpit, inward main/nose gear, ejector nozzles, heat-darkened materials and serial/tail emblem. Reconstructed or gameplay approximations are exact hidden cross-sections, wheel-door linkage, fuel-tank distribution, inlet schedule, aerodynamic coefficients, J58 engine deck, contrail humidity and thermal response. Heat haze is a shared animated transparent schlieren impression; it does not refract the backbuffer.

Measurements: 32.7406 m length, 16.9418 m span, about 5.6388 m parked height; 80,000 lb fuel capacity; LOD triangles 291,530 / 116,811 / 27,355 / 5,435; packed Blender source 11,687,195 bytes; four GLBs 18,939,436 bytes total; editable PNG maps 7,761,522 bytes. Maps are 4K fuselage/wing, 2K nacelle/tail, 1K heat and 512 warning.

Flight evidence: takeoff ground roll 1,369.7 m and liftoff 119.24 m/s; 25 km high-Mach cruise settles at Mach 3.18 with 38–75 m altitude range over ten minutes; engine-out yaw moments are ±203,930 Nm with bounded max rate 0.285 rad/s; landing touchdown is 92.89 m/s at 1.31 m/s sink with 1,155.7 m rollout. The dedicated Delta/elevon model has independent twin dry/reheat engines, inlet recovery, fuel burn, CG/inertia variation, contrails and altitude/Mach effects. These are public-data-based gameplay approximations, not an exact engine deck or classified inlet controller.

The SR-71 is unarmed: no gun, ammo or hardpoints. Server fire input is ignored and counted as rejected. Registry type 4, protocol v6, prediction/replay and a live four-aircraft A320/Falcon/Typhoon/SR-71 session pass. Seventeen lightweight hit spheres and chase, close, orbit and cockpit cameras are definition-owned. Native renderer measurements are 223/180/160/108/73 FPS for 1/2/8/16/32 SR-71s; physics is 4.42 microseconds per step. Debug, release, headless and the SR-71 sanitizer-focused subset pass. The full graphical smoke harness remains environment-sensitive and reported only its existing focus-restore failure.

Final Blender views and native captures are generated into
`docs/images/m3_67_sr71/` by `scripts/capture_m3_67.py`, and focused logs are
written to `output/m3_67/`. Both are generated artifacts and are not version
controlled.

Play from the repository root:

```sh
LD_LIBRARY_PATH="$PWD/.cache/sysroot/usr/lib64" ./build/release/client/ofs_client --aircraft sr71 --airborne --camera chase
```

Useful options are `--camera cockpit`, `--camera close-chase`, `--visual-scenario afterburner` and `--flight-demo takeoff`. The server and client resolve `sr71` through the immutable registry and never accept arbitrary asset paths.
