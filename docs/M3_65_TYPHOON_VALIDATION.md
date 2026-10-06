# M3.65 Austrian Eurofighter Typhoon — implementation and validation

Historical milestone report. The temporary Falcon has since been retired; current aircraft physics and evidence are documented in [AIRCRAFT_PHYSICS_AUDIT.md](AIRCRAFT_PHYSICS_AUDIT.md).
Implemented and visually inspected on 2026-10-01. Native integration, flight,
combat, mixed multiplayer and bounded twin afterburner are validated below.
Publicly verified dimensions/configuration are distinguished from reconstructed
surface contours and gameplay physics. This is not a manufacturing-accurate or
certified Typhoon model. No radar/missile milestone was started.

## Audit and plan (2026-10-01)

Read M3.6 validation, architecture, assets, coordinates and build documentation.
Inspected registry, physics/thrust, server spawn, protocol validation, gun and
hitbox authority, GLB hierarchy/material/image loading, rig extras and pose
evaluation, mesh batching/LOD, shaders, cameras, effects, fixture captures,
CMake installation and CPU/network/combat/flight tests. Clean starting checkout.

Preserve the six-DoF simulator, 120 Hz server, existing protocol framing, prediction,
bounded particles and swept sphere collision. Concrete extension requirements:

- World spawn caches and renderer models are fixed arrays of two indexed by
  wire ID minus one. Replace with type-keyed storage before adding type 3.
- Three runtime LODs currently come from vertex clustering. Allow per-definition
  authored LOD assets, retaining the current three tiers for A320/Falcon.
- Normal textures are reported unsupported. Add a guarded normal-map path with
  regression coverage; keep existing factors/textures and environment rendering.
- Combat currently uses one global gun for every armed aircraft. Keep fixture
  overrides, but use definition gun settings and retain settings on each round.
- Existing pose channels need canards, mixed elevons and independent gear doors.

Sequence: reference research; scaled silhouette in live Blender MCP; orthographic
inspection/correction; mechanical/cockpit/texture/livery detail; authored LODs
and Blender renders; runtime integration; flight and mixed multiplayer tests;
in-game inspection and correction; full regressions/performance/documentation.

## Selected visual target and references

**7L-WA**, Austrian single-seat EF2000, standard gray delivery-era configuration
as photographed at Zeltweg on 12 July 2007. Austria's official acquisition notice
identifies Tranche I/Block 5. This pins a historical configuration, avoiding
unverified later upgrades. Clean external stores configuration is a deliberate
game asset choice; launcher attachment geometry may remain, no missiles/tanks.

Primary references (reference use only; photographs are not shipped as textures):

- [Austrian EF2000 dimensions and single-seat/twin-engine description](https://www.bundesheer.at/unser-heer/waffen-und-geraet/eurofighter-ef-2000):
  length 15.96 m, span 10.95 m, height 5.28 m.
- [Bundesheer 7L-WA first landing photo sequence](https://www.bmlv.gv.at/download_archiv/photos/flieger/galerie.php?currRubrik=55&id=1141):
  nose/canopy, gray radome, serial separated by national roundel, plain fin,
  foreplanes, intake, gear stance and delta planform.
- [Bundesheer acquisition configuration, 28 June 2007](https://www.bmlv.gv.at/cms/artikel.php?ID=3475):
  Tranche I/Block 5 fleet configuration.
- [Manufacturer aircraft features](https://world.eurofighter.com/the-aircraft/features):
  airframe and propulsion context. Raw aircraft is unstable; this game's stable
  derivative model will approximate closed-loop handling, not implement real FCS.
- [Swiss defence department three-view and public specifications, p.5](https://www.newsd.admin.ch/newsd/message/attachments/25030.pdf):
  proportions and EJ200 60/90 kN per engine. Not an Austrian configuration source.
- [Burkhard Domke's original Typhoon walkaround photography](https://b-domke.de/AviationImages/Typhoon.html):
  generic mechanical references for gear, canopy, intake and nozzles only.
  Foreign trainer/RAF-specific sensors and markings must not be copied.
- [Rheinmetall medium-calibre cannon portfolio](https://www.rheinmetall.com/Rheinmetall%20Group/brochure-download/Weapon-Ammmunition/B226e0424-Rheinmetall-Leadership-in-cannon-design-medium-calibre-portfolio.pdf):
  BK27 27 mm, 1,700 RPM, 1,000 m/s full-calibre muzzle velocity.

Surface contours, hidden duct/bay/cockpit structure, exact hinges, stencil density
and flight derivatives require visual/gameplay approximation. No manufacturing,
classified internal accuracy or 1:1 claim is made.

## Blender connection

MCP status and scene queried before code. Live Blender reports 5.2.2 LTS;
scene initially contained default Cube/Camera/Light, no saved filepath. Addon
1.6/protocol 5 is older than server expectation 13; core scene/code/screenshot
capabilities are available. Preserve the initial scene by modeling in a separate
named scene. Model construction/export must run through MCP in this instance.

## Final report

### 1. Exact Austrian target

Single-seat Austrian **7L-WA, Tranche I/Block 5, July 2007 delivery appearance**.
Clean stores, gray radome/airframe, bare outer attachment rails, wingtip fairings,
plain fin and no PIRATE turret. Later fleet upgrades and foreign markings were
not mixed into this historical target. Small antenna shapes remain approximated.

### 2. Public references

The seven linked sources above establish configuration, dimensions, photographed
exterior proportions, generic mechanical detail and public EJ200/BK27 ratings.
Official Austrian photographs take precedence for livery/configuration; foreign
walkarounds support mechanical interpretation only. Reference photographs and
the Swiss diagram are not incorporated into distributed textures.

### 3. Modeling approach

Repository-owned station lofts for the fuselage, radome and canopy; sampled
airfoil surfaces for delta wings/canards/elevons; contoured fin and wing roots;
separate mechanical parts, cockpit and ducts. Smooth curved surfaces and hard
mechanical boundaries use mesh normals. UVs follow cylindrical fuselage and
planform wing mapping, with dedicated fin/nozzle sheets. Original panel lines,
hatches, restrained fasteners and wear use texture/normal detail. Markings are
projected onto the actual surface rather than floating above it.

The source has **403 mesh objects, 54 channel nodes and 20 exported materials**.
Intended open cockpit, intake, nozzle and bay apertures are not sealed solid
objects. The scene's 474 objects include rig and validation studio; its 23
materials include unused authoring materials, not 23 runtime materials.

### 4. Actual Blender MCP workflow and correction passes

All substantial modeling, rigging, material assignment, UV construction,
decimation, scene rendering and GLB export ran in the live Blender instance
through MCP. Scene information and viewport screenshots were read before work
and after changes. The default scene was retained; the new scene is
`OFS Typhoon 7L-WA`. Export used Blender's native glTF exporter, never a custom
binary GLB writer. Packed source: `output/Typhoon_7L-WA.blend`.

After initial native inspection, deliberate correction passes changed the nose
and radome transition, canopy profile, forward canard placement, wing sweep,
vertical fin outline, nozzle length, gear stance and wing-root blend. Further
passes fixed opaque canopy reflections from inside, texture shimmer, floating
serial glyphs, rear canopy gaps, nose-gear folding direction, main doors and
ground suspension. Afterburner was repeatedly captured and adjusted for
translucency, color, flicker and the end-on hot core.

The retained authoring scripts form a sequential pipeline, not independent
idempotent scene patches. From a fresh Blender session, execute these through
MCP in one retained namespace, in order (adjust `ROOT` in the first script if the
checkout moves):

1. `typhoon_airframe.py`, `typhoon_studio.py`.
2. Generate original PNGs with `typhoon_textures.py` (Python/Pillow/NumPy).
3. `typhoon_details.py`, `typhoon_corrections.py`, `typhoon_shape_refinement.py`,
   `typhoon_finish_refinement.py`.
4. Generate remaining PNGs with `typhoon_finish_textures.py`.
5. `typhoon_polish.py`, `typhoon_gear_glass_refinement.py`,
   `typhoon_bay_refinement.py`, `typhoon_cockpit_suspension.py`,
   `typhoon_skin_refinement.py`, `typhoon_export.py`.

Scripts live in `scripts/`. Reopening the packed `.blend` needs none of these
steps. Re-exporting restores source visibility, temporary modifiers and texture
links, including on an export exception. Source textures are original seeded
procedural artwork; lettering uses rasterized Liberation Sans Italic. No game
assets, external aircraft meshes or photographic textures were redistributed.

### 5. Dimensions

Public reference envelope: **15.96 m long, 10.95 m span, 5.28 m high**. Measured
source envelope: **15.960000 × 10.950000 × 5.285380 m**; the 5 mm height excess
comes from detailed geometry. Blender metres use X aft from the nose, Y
starboard, Z up; native glTF converts to Y up and the renderer then uses body
FRD relative to the definition's CG. Matching the bounding box does not prove
every contour matches real engineering drawings.

### 6–8. LOD geometry, source and runtime sizes

| Tier | Triangles | Exported objects, including rig | GLB bytes |
|---|---:|---:|---:|
| LOD0 | 220,472 | 458 | 18,123,876 |
| LOD1 | 95,833 | 271 | 2,479,648 |
| LOD2 | 38,479 | 153 | 1,017,828 |
| LOD3 | 7,281 | 86 | 360,996 |

Packed editable Blender source: **15,656,768 bytes (14.93 MiB)**. All four runtime
GLBs: **21,982,348 bytes (20.96 MiB)**. LOD0 embeds the texture payload once;
lower tiers retain named materials and reuse the canonical texture table.
Source + GLBs + loose original textures + asset metadata total **50,855,933
bytes (48.50 MiB)**, excluding documentation and validation images.

Component visibility and decimation are deliberate per tier; tiny cockpit,
gear and nozzle detail disappears at distance. All four Blender tier images
were inspected. Runtime thresholds are 5/55/180 visual radii (roughly
47.5/522.5/1710 m) with 15% hysteresis. A320/Falcon keep their existing tier path.

### 9. Textures and memory

**13 PNGs, 13,215,778 bytes (12.60 MiB)**:

- Fuselage and wings: each three 4096×2048 base-color/metallic-roughness/normal maps.
- Fin: three 2048×2048 maps; markings: one 2048×2048 RGBA atlas.
- Nozzles: three 1024×1024 maps, including heat discoloration.

Decoded RGBA base levels occupy **281,018,368 bytes (268 MiB)**; complete mip
chains occupy **374,691,156 bytes (357.33 MiB)** before driver overhead. This
is a significant current memory cost, shared per model across aircraft/LODs.
PNG disk compression does not reduce GPU RGBA memory. BC/KTX2/Basis ingestion
would improve this; it is not implemented. CPU-generated mipmaps and
renormalized normal-map mip levels removed visible close-range shimmer.

### 10. Materials

Twenty exported materials separate air-defense gray paint, warmer radome,
wing/canard paint, fin paint, Austrian markings, satin aluminum, light-gray gear,
tire rubber, titanium nozzle case, heat-stained petals, dark ducts, charcoal
cockpit, olive seat textile, harness, graphite canopy frame, tinted canopy, HUD
combiner and port/starboard/landing-strobe lenses. PBR base color,
metallic/roughness and normal maps are supported. Emission uses material factors.
Canopy alpha is 0.12 with restrained roughness/reflection; an opt-in procedural
sky reflection is used for this canopy. Opaque batches precede transparent ones.

### 11. Austrian livery

Reference-based gray finish and `7L [Austrian roundel] WA` serial on both cheeks;
roundels on upper/lower wings; plain fin in the selected photos. Individually
oriented lettering avoids mirrored text. Atlas warnings cover ejection/rescue,
no-step and intake/exhaust areas; panel/hatch detail is restrained. Tiny stencils
and exact paint chips are reconstructed approximations, not a claimed exhaustive
maintenance decal transcription. Livery close-ups were inspected after surface
projection removed floating shadows.

### 12. Articulated parts

`ofs_canard_L/R`; `ofs_elevon_inner_L/R`, `ofs_elevon_outer_L/R`; `ofs_rudder`;
`ofs_main_gear_L/R`, `ofs_nose_gear`; four gear-door roots;
`ofs_nose_steering`, `ofs_nose_wheel`, `ofs_main_wheel_L/R`;
`ofs_suspension_nose_gear`, `ofs_suspension_main_gear_L/R`;
`ofs_nozzle_L_00..15` and `ofs_nozzle_R_00..15`. `ofs_canopy` reserves a future
opening hierarchy and currently has zero animation gain.

Canards follow pitch; paired elevons mix pitch, roll and flap state, bounded
to ±25 degrees; rudder follows yaw input. glTF extras store channels, local
axes, gains and optional translations. Pose tests verify independent nozzle,
control, gear and suspension changes. Simulation/control state drives all poses.

### 13. Landing gear

Struts, actuators, tire/hub/brake hints, bay recesses/ribs and separate doors;
four-second extension/retraction. Main assemblies fold inward, nose folds aft;
door timing opens around the gear movement and closes when stowed. Root slides
approximate the compound mechanism rather than pretending to be an exact
four-bar linkage. Wheels rotate from ground speed, nose steering decreases
with speed, and contact-derived oleo compression keeps tires on the runway.
Stowed underside and transition images were checked for major clipping.

### 14. Cockpit-visible detail

Ejection-seat silhouette, head box, harness, instrument-panel massing, side
consoles, stick, coaming, HUD frame/combiner, canopy framing/seals and rear
bulkhead. Native pilot view now renders this aircraft's own cockpit geometry.
Displays are unpowered and simplified; there are no clickable controls or
functional avionics. Transparent canopy remains separate for future opening.

### 15. Engines, nozzles and required afterburner

Two independently modeled EJ200-level engines: public nominal **60 kN dry /
90 kN reheated per engine**, sea-level static. At idle the existing 5% thrust
floor remains. Throttle 0..0.85 covers dry power; the upper 0.85..1 regime
proportionally commands reheat once actual spool reaches the detent. N1 uses
0.65 s spool response; afterburner follows a bounded 0.16 s response, giving
clean ignition/extinction and no full-flame chatter around the threshold.
Extra reheat thrust ramps with state; the unchanged atmospheric lapse remains.

Six translucent procedural 3D flame layers total across both engines, plus two
soft nozzle glow discs. Blue/violet near-nozzle shells, bright inner hot core,
yellow/orange downstream colors, longitudinal animated structure, soft shock
cell impressions, phase-offset flicker, taper and opacity falloff. Full core
length is about 4.15 m; state controls length, intensity and glow. Each engine
attaches at its own exhaust anchor, 6 cm behind the modeled nozzle exit.
Sixteen individually hinged petals per engine open about 0.075 rad in reheat.
No black smoke during normal operation or afterburner.

Effect budget: **6,916 triangles and eight draw submissions per visible reheat
aircraft**, shared static geometry, no flame particles or streamed particle
positions. Effects stop past 1200 m or for inactive/destroyed aircraft. The
existing global 4096-particle/64-emitter pool remains bounded; ordinary dry
exhaust is faint and yields to reheat. Other bounded vapor/lights may still
contribute particles. **Refractive heat haze, volumetric lighting and bloom are
not implemented**; nozzle glow does not cast dynamic light onto the runway.

### 16. Aircraft lights and vapor

Port red, starboard green and landing/strobe lens geometry/materials; restrained
existing local effect flashes. Small formation-light housing details are present,
but the lighting pattern is not a certified aircraft electrical simulation.
Existing contrails emit from the two exhausts only above 7000 m at TAS >90 m/s;
short wingtip vapor requires |load| >2.4, 300..4500 m altitude and TAS >65 m/s.
Native contrail and aircraft-light evidence was inspected.

### 17. Independent flight configuration

Mass **14,000 kg** representing a fueled clean aircraft; inertia
**20,500/115,000/132,000 kg·m²**; wing **50 m² / 10.95 m span / 4.3 m MAC**.
Lift slope 3.8/rad, CLmax 1.50 clean / 1.85 configured flap, critical alpha
21 degrees, zero-lift alpha −0.5 degrees; CD0 0.024, Oswald e 0.72.
Pitch stability −0.48, pitch damping −18, pitch-control coefficient −1.10;
roll damping −0.55, roll control 0.24; yaw stability 0.11, yaw damping −0.26,
yaw control −0.115. These are authored gameplay derivatives, not published FCS.

Engine lever arms body FRD: {−4.45, ±0.63, 0.05} m. Gear contacts
{3.97,0,2.05} and {−1,±1.45,2.05}; oleo stroke 0.32 m,
stiffness 560 kN/m, damping 33 kN·s/m; maximum brake coefficient 0.58.
Pitch range −23/+18 degrees, roll 20, rudder 25, configured flap 15.
A320 and Falcon configurations and legacy dry-thrust calculations are retained.

### 18. Measured headless flight behavior

Results in `images/m3_65_typhoon/flight_measurements.log`, measured by
`tests/typhoon_scenarios.cpp`, not predictions of real operational performance:

| Check | Measured result |
|---|---|
| Engines | 120,001 N total dry, 180,002 N total reheat; ratio 1.5; independent left/right states |
| Cruise, 120 s | 110/180/250 m/s; throttle 0.389765/0.464812/0.629256; pitch 6.01629/1.89015/0.69229° |
| Trim drift | Maximum height drift 6.88e−8 m, speed drift 7.88e−10 m/s in these equilibrium fixtures |
| Taxi/braking | 7.7266 m/s; right heading 35.6092°; stopped at 1.52e−8 m/s |
| Takeoff | Liftoff 9.3417 s after release, 414.2607 m, 95.9434 m/s; 80 s fixture ends at 3197.23 m, 271.57 m/s |
| Roll/pitch/yaw | Primary rates after 0.5 s: 0.949836/0.180817/0.220623 rad/s |
| Sustained turn, 20 s | 30.0472° bank, 32.5558° heading change, 1.16261 g, 181.1897 m/s |
| Stall/recovery, 25 s | Initial warning at 32° alpha; final alpha 0.7053°, TAS 263.09 m/s; final altitude 14.36 m above start |
| Landing | Touchdown 29.2167 s, TAS 78.7441 m/s, sink 1.28454 m/s; rollout 1092.57 m; peak contact 201.884 kN; stopped |
| Extremes | 42 finite-state cases, speeds 0..600 m/s and angles −180..180° |

The negative logged stall `altitudeLoss` is a final altitude gain, not proof of
zero transient altitude loss. Separate native real-physics captures cover taxi
(10 s, TAS 4.726 m/s), takeoff (14.67 s, TAS 116.676 m/s, height 23.057 m),
and reheat climb (30 s, TAS 233.047 m/s, height 502.637 m).

### 19. Gun

Existing authoritative projectile/combat path, per-definition BK27-style
**1700 RPM, 1000 m/s, 150 rounds**. Starboard intake shoulder muzzle
body {4,0.82,0.29} m, forward direction; 0.0015 rad dispersion, 3 s lifetime,
2400 m configured range. **34 damage** and four-second respawn are game rules.
Native gun fixture uses this definition's velocity/lifetime and was inspected.
Server owns fire validation, muzzle transform, projectile creation, swept hits,
damage/destruction. A320 remains unarmed; Falcon keeps its existing gun settings.

### 20. Hitboxes

Seventeen Typhoon-specific lightweight collision spheres: eight nose/fuselage/
engine-body, four per wing and one fin. Radii 0.40..1.16 m; authoritative
centers are transformed/cached once per physics step. Existing swept collision
is preserved; render mesh is never used for authoritative collision. This is
an approximate whole-aircraft damage envelope, not per-component failure logic.

### 21. Cameras

Body FRD metre offsets: pilot **{5.12,0,−1.40}**, chase **{−29,5,−8}**,
close chase **{−20,0,−5}**, orbit/gun-camera center **{−1,0,0}**. Asset CG
glTF **{9.35,2.05,0}**. Pilot source position is approximately x=4.23,
z=3.45 m, within the canopy/HUD area. All native camera captures were inspected;
existing A320/Falcon offsets are retained.

### 22. Registry and real mixed multiplayer

Type ID **3**, selection `--aircraft typhoon`. Type-keyed world spawn and
renderer model storage replace fixed two-aircraft arrays; registry size is
derived. Invalid wire types are rejected, asset paths come only from the registry.
Coverage spawns all three types to capacity and tests independent engine state.

Protocol **v4** appends two bounded float32 afterburner values: **231 bytes per
snapshot aircraft record**, eight bytes more than v3. Other framing/authority is
preserved. Both server and clients must be rebuilt together. N1, controls and
afterburner interpolate on clients; flame/nozzle poses are generated locally.

Automated real GameNetworkingSockets mixed test: three clients, all see both
remote types; Typhoon left engine reheats while right remains dry, correctly
received by A320/Falcon. Additional **three native graphical clients** ran
against one authoritative server for 20 seconds (`validate_typhoon_multiplayer.py`).
All three printed `GRAPHICAL PASS`; each observed two remotes. A320/Falcon logs
confirm remote Typhoon afterburner; their own states stay zero. Typhoon has both
engine states 1.0. Server reports three clients and no invalid controls,
disconnects, overload or send failures. Screenshots/logs are stored as
`network_{a320,fighter,typhoon,server}` evidence. This is real networking;
`runtime_mixed_fixture.png` is separately labeled synthetic visual evidence.

### 23. Rendering performance

Release, **1280×800**, VSync off, default medium effects/shadows, Intel integrated
ARL graphics, Mesa 26.2.3, native bgfx OpenGL. One-second stable warm-up and
three-second measurement per count. Synthetic presentation workloads exercise
normal LOD selection; these are rendering measurements, not 32 network users.
Mixed entries cycle the registered fleet at near/mid/far distances; reheat
entries are all Typhoons within flame range. The first aircraft is close LOD0.
All counts retain shared textures and bounded effects.

| Fleet | Count | FPS | Frame ms | GPU ms | Prep ms | Draws | Triangles | Particles | LOD0/1/2/3 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| Mixed | 1 | 332.9 | 3.00 | 1.38 | 0.176 | 110 | 227,753 | 10 | 1/0/0/0 |
| Mixed | 2 | 332.5 | 3.01 | 1.24 | 0.332 | 278 | 329,981 | 14 | 1/1/0/0 |
| Mixed | 8 | 203.8 | 4.91 | 1.96 | 0.878 | 796 | 733,972 | 24 | 1/6/1/0 |
| Mixed | 16 | 150.1 | 6.66 | 2.69 | 1.606 | 1,259 | 1,053,735 | 42 | 1/12/2/1 |
| Mixed | 32 | 116.1 | 8.61 | 3.29 | 2.581 | 2,258 | 1,922,815 | 84 | 1/25/4/2 |
| Reheat | 1 | 339.0 | 2.95 | 1.10 | 0.174 | 118 | 234,669 | 2 | 1/0/0/0 |
| Reheat | 2 | 315.8 | 3.17 | 1.36 | 0.293 | 221 | 344,699 | 6 | 1/1/0/0 |
| Reheat | 8 | 150.0 | 6.67 | 2.71 | 1.007 | 825 | 997,598 | 16 | 1/7/0/0 |
| Reheat | 16 | 119.1 | 8.39 | 3.42 | 1.920 | 1,551 | 1,826,871 | 38 | 1/15/0/0 |

Numbers include terrain, shadows, UI submission and other effects, not aircraft
mesh alone. Reheat flame itself emits zero particles; listed particles include
other effect types. CPU frame and GPU timings overlap rather than add.
Machine/driver/load dependent. Reproduce with `scripts/profile_m3_65.py`;
raw records are in `performance.json` and per-count logs.

### 24. Builds, tests and sanitizers

Native Debug and Release, headless, and headless ASan/UBSan builds succeeded.
Eight new flight scenarios and three Typhoon integration tests extend the
baseline to **67 native / 64 headless tests**. Old tolerances were not loosened.

| Configuration | Complete-suite result | Final relevant rechecks |
|---|---|---|
| Debug | 67/67 pass, 194.66 s | 5/5 visual/asset/multiplayer pass, 17.01 s |
| Release | First run 65/67, 185.03 s; both failures pass separately | Latest 5/5 visual/asset/multiplayer pass; graphical smoke separately 1/1, 5.75 s |
| Headless | 64/64 pass, 180.12 s | 4/4 visual/asset pass, 10.67 s |
| ASan + UBSan | 64/64 pass, 180.20 s | 5/5 final visual/asset/multiplayer pass, 47.58 s |

Release's first `network.bad` run hit the existing timing-sensitive impaired
network case; a separate unchanged rerun passed (12.25 s). The first graphical
smoke run overlapped desktop visual inspection and failed relative mouse
capture; a separate rerun passed. A final six-test Release recheck likewise
passed the five CPU/network tests but encountered desktop mouse-capture failure;
the final isolated graphical smoke passed. These failures are retained in logs,
not represented as one uninterrupted all-green Release suite.

The first sanitizer attempt used default allocator quarantine and hit the RSS
soak budget plus a timing-sensitive combat case. The full successful rerun used
the existing documented `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:quarantine_size_mb=16`
and UBSan halt-on-error. No sanitizer findings, leaks or suppressions in that
complete run. Optional network-disabled client `main.cpp` also passed a real
compiler syntax check; a complete additional offline graphical build was not
run. `git diff --check` passes. Build/test logs are retained in
`images/m3_65_typhoon/validation_logs/`.

### 25. Captures created and inspected

All canonical files are generated into `docs/images/m3_65_typhoon/` by
`scripts/capture_m3_65.py` and are not version controlled; earlier correction
images are produced as work history. Blender final renders, 1440×960:

- `final_front`, `final_rear`, `final_left`, `final_right`, `final_top`,
  `final_underside`, `final_front_quarter`, `final_rear_quarter`.
- `final_gear`, `final_cockpit`, `final_nozzles`, `final_livery`.
- `lod0..3_front_quarter`; stowed underside/rear-quarter inspection images.

Native captures, 1440×900, described in `runtime_manifest.json`:

- `runtime_parked`, `runtime_taxi`, `runtime_takeoff`, `runtime_climb_ab`.
- `runtime_chase`, `runtime_close_chase`, `runtime_cockpit`, `runtime_surfaces`,
  `runtime_gear_transition`, `runtime_gear_up`, `runtime_underside`.
- `runtime_contrail`, `runtime_gun`, `runtime_mixed_fixture`.
- `ab_rear_off`, `ab_rear_military`, `ab_rear_on`, `ab_three_quarter`, `ab_dark`,
  `ab_flight`, `ab_multiple`, `ab_ignition`, `ab_extinction`.
- Three real-network client screenshots listed in item 22.

Images were actually inspected individually and in comparison sheets, including
the final source viewport and all canonical angles. Taxi/takeoff/climb are
actual simulated trajectories; other captures are explicitly controlled visual
fixtures. No post-processing was used to paint in aircraft or exhaust.

The two reference captures for this section are the final native twin-afterburner
view and the final Blender front-quarter aircraft view. Capture images are
generated artifacts and are not version controlled; regenerate them with
`scripts/capture_m3_65.py`.

### 26. Known visual inaccuracies

Surface sections, wing-root blend, intake/duct shape, nozzle mechanisms and
small aerials were reconstructed from public views rather than manufacturer
CAD. Minor mechanical seams and gear linkage approximations remain. Cockpit
panel/seat details are simplified, displays unpowered; tiny stencils, fasteners
and panel arrangement are not a photographic transcription. Nozzle opening is
restrained visual interpretation. Glass uses procedural sky reflection, not an
environment cubemap. Transparent sorting and automatic LOD decimation retain
the renderer's general limits. LOD3 intentionally omits close-up detail. Exhaust
has no refractive haze, volumetric scattering, bloom or dynamic light cast.
The aircraft is substantially refined, but neither 1:1 nor proprietary combat
game visual parity is claimed.

### 27. Known flight-model inaccuracies

The real unstable airframe/FCS is approximated by a stable augmented derivative
model. No full Eurofighter control law, Mach-dependent aerodynamics, detailed
canard-wing interference, fuel consumption, engine compressor map or real
post-stall envelope. Thrust/geometry public ratings are anchored; inertia,
coefficients, contact mechanics and measured trajectories are gameplay values.
Trim fixtures demonstrate numerical stability, not operational certification.

### 28. Remaining technical debt

Texture compression/transcoding and lower GPU memory; cheaper articulated
batching and broader culling for dense scenes; improved canopy/environment
lighting, refractive heat haze and HDR/bloom; more photographic cockpit and
mechanical refinement; better deterministic timing for `network.bad` and combat
timing fixtures. Graphical smoke needs isolated desktop focus. Protocol v4
requires coordinated binary updates. Asset scripts retain an explicit authoring
root and sequential correction order; packed source is portable. No bandwidth
redesign was undertaken. A320's pre-existing heavy asset is retained.

### 29. Future missile/radar foundation

**Ready as an aircraft/visual/authority foundation**, with stable type/registry,
per-aircraft capabilities, weapon muzzle, lightweight collision, pilot cameras,
hierarchy and independent replicated engine state. Missile stations, radar/sensor
definitions, seeker/guidance, avionics and weapon UI still require their own
design and validation. None was implemented here. M3.65 stops at this milestone.
