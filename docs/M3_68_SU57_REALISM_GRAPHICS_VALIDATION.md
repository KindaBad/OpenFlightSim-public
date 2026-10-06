# M3.68 — Su-57 and shared realism/graphics validation

Historical milestone report. The temporary Falcon has since been retired; current aircraft physics and evidence are documented in [AIRCRAFT_PHYSICS_AUDIT.md](AIRCRAFT_PHYSICS_AUDIT.md).
2026-10-02. Continues the existing M3.66/M3.67 repository and preserves the
uncommitted SR-71 work. Source: `/home/kindabad/Downloads/Su57-Felon.blend`.
The original is intact; editable result: `output/Su57-Felon.blend`.
No M3.7 features or excluded systems were started. Native C++/SDL3/bgfx/GNS,
authoritative physics, replay and the existing renderer architecture are retained.

The scientific standard is physical coherence and measured acceptance, **not
fully realistic or classified precision**. REFERENCE denotes a public dimension,
mechanism or specification. ESTIMATE denotes a reconstructed parameter or visual
mechanism. CALIBRATED APPROXIMATION denotes a bounded engineering model whose
handling is validated here. Calculated decimal precision is not measurement
accuracy. War Thunder was the user's requested close-up quality reference; no
commercial-game assets were used and visual parity is not claimed.

## Requested 49-point report

1. **Unit audit findings.** Core mechanics already used SI. Presentation degrees,
   Celsius temperature differences, imperial reference inputs and graphics-angle
   controls needed explicit boundary documentation. Found an unmeasured A320
   nose station, the undersized Su-57 source, over-wide centreline paint, shared
   belly-clearance constants, oversized tail spheres, excessive texture memory
   and single-stage display colour handling. A cockpit seat intersection and
   accidental rotation on compression pivots were found by native inspection.

2. **Final conventions.** One world/renderer/evaluated glTF/metric Blender unit
   is one metre. Time s, velocity m/s, mass/fuel kg, force N, torque N·m,
   inertia kg·m², pressure Pa, density kg/m³, temperature K, angle rad, angular
   rate rad/s. UI knots/feet/degrees use named `ofs/units.hpp` conversions.
   `State::pos_ned` is actual CG; definition points reference the loaded datum
   and subtract moving CG. [UNITS.md](UNITS.md) records exceptions and frames.

3. **Scale fixes.** Su-57 mesh vertices receive corrected dimensions and applied
   transforms, with no camera scale compensation. A320 nose contact moved from
   9.50 to 10.44 m ahead of reference CG, matching its measured visual wheelbase.
   Aircraft-specific nacelle/fuselage/wingtip contacts replace shared belly
   clearance. Su-57/SR-71 tail spheres reduced from 2.0/4.4 to 1.15/1.8 m.
   Centreline width reduced from 1.8 to .90 m. Compression gain is zero so
   oleos translate without rotating about the asset origin.

4. **Aircraft dimensions.** Measured evaluated native GLB rest bounds include
   extended gear; there is no runtime aircraft scale multiplier:

   | Aircraft | Length m | Span m | Height m | Status |
   |---|---:|---:|---:|---|
   | A320 ceo / wingtip fences | 37.5700 | 35.8000 | 11.7700 | REFERENCE anchor |
   | Falcon F-16-style | 15.1500 | 9.8600 | 4.8800 | ESTIMATE generic aircraft |
   | Typhoon | 15.9600 | 10.9500 | 5.2854 | REFERENCE; 5.4 mm gear mesh lip |
   | SR-71A | 32.7406 | 16.9418 | 5.6388 | REFERENCE anchor |
   | Su-57 | 20.1000 | 14.1000 | 4.6000 | Public CAD REFERENCE anchor |

   Full evidence: `output/m3_68/scale-mass-audit.log`. Falcon is a generic
   F-16-style airframe, not a particular certified block/configuration.

5. **Runway/world.** Fictional runway remains 2600 × 45 m, 30 m centreline
   dashes at 60 m repetition. .90 m centreline paint follows the public FAA
   precision-runway width anchor. Taxiway/buildings/effects remain in metres.
   The ground plane is continuous NED Z=0; 5 cm pavement/paint render offsets
   are an acknowledged depth-buffer approximation, not physical runway steps.
   This is not a certified airport replica or Earth terrain.

6. **Source audit.** Source Mid: 326,172 triangles; Low: 328,988 triangles,
   a displaced duplicate rather than a useful reduced LOD. Original dimensions
   approximately 17.1700 × 12.3671 × 2.7801 m; 75 nonempty source meshes, no
   useful articulated gear, control rig or animation actions. Inspected text,
   custom properties, materials and packed images. Embedded Geo-Scatter code
   was not executed. Author/redistribution licence remain **UNVERIFIED**.
   Local integration only; no source/model/texture uploaded. Inventory and
   SHA256 records are under `output/m3_68`; see the asset README.

7. **Su-57 dimensions.** 20.1 × 14.1 × 4.6 m anchors come from public CAD
   research, not an OEM metrology certificate. Estimated extended-gear stance:
   2.45 m datum CG height, 9.25 m wheelbase, 4.50 m main track, main tyres
   radius .515 m and paired nose tyres radius .33 m.

8. **Blender corrections.** All substantial work used live Blender MCP.
   Length/vertical factor 1.1706467313 and span factor 1.1401192030 were
   applied to evaluated geometry, rotated to canonical aft/right/up. Retained
   supplied UVs, digital camouflage, exterior airframe/intakes/nozzle geometry;
   authored missing gear, cockpit finishes and rig. Optimized excessive hidden
   compressor/nozzle tessellation. Added **1,551 actual geometry pieces**, joined
   into 51 rig-aware meshes: hollow/spoked hubs, brake stacks/calipers, clevises,
   pins, collars/seals, hydraulic barrels/pistons, fittings/hoses, door ribs/hinges,
   projected skin seams, inspection covers and millimetre flush fasteners.
   Clay views disable all textures to distinguish real geometry from artwork.
   Camouflage/microscopic wear remain textures. The owned cockpit replaces
   unverified F14-named artwork in the runtime; source originals remain saved.

9. **Articulation.** Stabilators, outer ailerons, inboard flaps, split slats,
   LEVCON roots, all-moving canted fins, independent nozzles, three gear legs,
   doors, oleo translation, nose steering and wheel spin. 17 useful channels
   are checked. Main wheels fold inward into the measured body volume; small
   geometry follows its proper retract/compress/steer/spin parent. Kinematics
   and hidden gear mechanisms are ESTIMATES, not hydraulic/OEM replicas.

10. **Physical thrust vectoring.** Each engine independently rotates forward
    thrust about its mirrored canted unit axis using Rodrigues' formula.
    `F = thrust × direction`; `M = (engine − loaded CG) cross F`. Full tensor
    and gyroscopic terms integrate these moments; no scripted attitude change.
    Animation/exhaust exit/direction use the same actual actuator angles.

11. **Limits/rates.** ESTIMATE: mirrored 30° cant, ±15° travel, 60°/s finite
    actuator rate. Angles are state radians, independently bounded, replayed
    and interpolated. Surface actuators also retain finite rates.

12. **FCS/allocation.** A separate VectorFighter law requests angular response
    with soft G/AoA protection. A weighted five-column minimum-motion allocator
    uses elevator/aileron/rudder and the two actual nozzle force Jacobians.
    Surface authority depends on q, Mach and flow separation; nozzle authority
    depends on actual thrust/health/spool. TV is favoured at low q and surfaces
    at speed. No random departure, forced rotation or attitude/rate clipping.
    Regularization and saturation are CALIBRATED APPROXIMATIONS; proprietary
    Su-57 FCS logic is unavailable and is not claimed.

13. **High-AoA changes.** Shared bounded separated-flow tables progressively
    blank aerodynamic controls, fins/tails and rate derivatives. Corrected
    residual wing roll damping so stalled sections do not retain an artificial
    linear derivative. Su-57 adds bounded leading-edge vortex lift/breakdown.
    LEVCONs resolve their own local flow and redistribute wing reference lift
    and drag budgets instead of creating free lift. Positive/negative separation
    are distinct. Local flow includes `velocity + omega cross arm`.

14. **Post-stall behaviour.** Lift falls after stall/vortex breakdown; broadside
    drag removes mechanical energy and aerodynamic controls weaken. Asymmetric
    local flow/wing damage generates departure moments through physical force
    differences. Powered recovery and unpowered energy loss are measured.
    This remains quasi-steady; dynamic-stall hysteresis and validated deep spins
    are not implemented, and a scripted Cobra is not presented as validation.

15. **Mach/compressibility.** Bounded smooth lift/control tables across transonic
    and supersonic flow replace a simplistic speed-invariant slope without a
    Mach-1 singularity. Existing per-type wave drag, atmosphere/pitot and engine
    lapse remain. No fighter polar/engine deck is called an exact public curve.

16. **Shared aerodynamics.** All five types receive separation/Mach/control
    blanking/damping fixes and actual wing-height ground effect. Fighter flap
    lift follows configured CLmax increments (.45 Falcon, .35 Typhoon/Su-57),
    rather than transport Fowler lift 1.05. A sustained 50 s coordinated-turn
    test checks load against sec(bank), heading rate against g·tan(bank)/TAS,
    altitude and sideslip. It caught a sideslip-feedback sign error missed by
    short axis tests; corrected without loosening acceptance tolerances.

17. **Mass/CG/inertia.** Su-57 ESTIMATE basic mass 18,500 kg, reference fuel
    6,500 kg, crew 100 kg, total 25,100 kg, fuel capacity 10,300 kg. Reference
    inertia derives from structure, two 1,600 kg engines, distributed fuel and
    crew with documented radii of gyration, independently of response tuning.
    Calculated Ixx/Iyy/Izz = 67,071/328,905/388,965 kg·m², Ixz = 4,649 kg·m²
    under the existing tensor sign convention. Fuel/payload changes use shared
    CG/parallel-axis updates. Existing A320/Falcon/Typhoon fallback basic masses
    are 42,000/7,200/11,000 kg. Positive tensors/envelopes remain validated.

18. **Engine realism.** Engines produce Newton-scale forces with independent
    spool/reheat/health, altitude/Mach lapse and kg/(N·s) fuel flow. No vector
    angle increases thrust magnitude or adds free energy. Shared architecture
    and existing engine-out/fuel tests are retained. Static/reference thrusts
    and estimated installed decks are distinguished.

19. **Su-57 engines.** AL-41F1-era ESTIMATE, not the newer engine's classified
    specification: two engines, 93/147 kN dry/wet each, .8 s spool constant,
    dry/wet TSFC 2.2e−5/5.1e−5 kg/(N·s), density exponent .60 and ram gain .23.
    Datum hinge stations (−6.4, ±1.34, .16) m. Public programme identity is
    referenced; these numerical decks/positions remain estimates.

20. **Ground physics.** Aircraft-specific wheel stations, compliant oleos,
    effective-mass tire impulses, friction ellipse and load-dependent braking
    remain. Six distributed belly/nacelle/wingtip contacts replace global
    clearance and a hidden downward-speed clamp. Their forces include actual
    lever-arm moments and dissipative Coulomb slip; debug arrows retain contact
    points. New rolled gear-up drop/skid tests support every airframe above the
    plane and dissipate 20 m/s initial skid to near zero, without pose clipping.
    Core gear contact deployment still follows the gear command; visual travel
    is finite. A fully coupled retracting-contact/hydraulic model is a limitation.

21. **Cameras/FOV.** Corrected Su-57 eye to datum (6.70,0,−.98) m after its
    original eye intersected the retained seat. Native screenshot confirms
    forward canopy visibility. Cockpit near plane at most .08 m; default vertical
    FOV 70°, configurable 40–100°. Chase/orbit anchors use actual dimensions and
    loaded CG; smoothing never feeds physics. No asset-sizing camera workaround.

22. **Hitboxes/projectiles/effects.** Efficient 17-sphere swept gun hit model
    remains. Reduced oversized tail envelopes; Su-57 stations fit the measured
    nose/wing span. Su-57 gun ESTIMATE 1,500 rpm, 860 m/s muzzle velocity, 150
    rounds, metre muzzle station. A320/SR-71 remain unarmed. Gravity, times,
    projectile motion and effect dimensions are SI; damage is gameplay tuning,
    not a lethality claim. No radar/missiles/stealth gameplay added.

23. **PBR/materials.** Base/metallic/roughness/normal/emission retained; glTF
    occlusion strength/UV0 supported. AO affects ambient light, not direct sun.
    Colour maps decode exact sRGB, data maps remain linear. Material factors
    remain linear. Working opaque body and glass use separate alpha handling.

24. **Normals.** Existing tangent generation/consumption preserved and checked;
    normal texture mips renormalize vectors instead of weakening them. Retained
    supplied panel/nozzle normals; new geometric detail is actual mesh depth.

25. **Mips/filtering.** Full colour-role-aware RGBA mip chains, linear-light
    averaging for colour, linear alpha/data, normalized normals, odd-size area
    coverage and sensible sampler mip modes. Texture cache keys include roles
    and sampler flags; anisotropic sampling is configurable. Numerical tests
    distinguish correct 188 grey colour averaging from incorrect byte-space 128.

26. **Lighting.** Shared directional sun, ambient sky/ground and PBR response
    operate in linear radiance before display. Reasonable defaults and developer
    controls retained. Intensity is a relative renderer scale, not calibrated lux.

27. **Atmosphere/sky.** Analytical altitude-dependent optical air column with
    8 km approximate scale height, longer horizon path, sun direction/halo and
    horizon brightening. Sun radius approximately .266°, derivative antialiasing.
    Haze integrates mean exponential density from eye to surface rather than
    using only surface altitude or a constant high-altitude fog floor. Fixed
    cloud layer is a visual approximation; no full radiative transfer/weather.

28. **HDR/tone map/bloom.** RGBA16F scene with depth/MSAA, quarter-resolution
    thresholded separable bloom and one final ACES-like exposure/sRGB composite.
    UI follows the scene composite. Bloom defaults restrained .08 strength,
    configurable/off. Resize/fullscreen recreation passed native smoke. Unsupported
    float-MSAA falls back to one sample with a diagnostic; no silent HDR downgrade.

29. **Shadows.** Stable light-space texel snapping uses double-precision absolute
    origin coordinates. Quality selects an appropriate shadow LOD instead of
    always the cheapest mesh; alpha-blended glass is excluded from solid shadows.
    Existing filtering/bias controls retained. Single local map remains, with
    finite coverage; distant cascades and temporal filtering are limitations.

30. **Canopy/glass.** Thin alpha-blended glass with approximate Fresnel environment
    reflection, tint/roughness, two-sided rendering and separate painted frame.
    Working canopy alpha .14, IOR 1.46; retained seat geometry with owned fabric,
    bezels and static display/HUD geometry. No clickable/operational avionics.
    Transmission/refraction and transparency sorting remain approximations.

31. **Heat haze.** Shared, cheap, moving transparent schlieren bands plus soft
    exhaust particles. Scales with engine state, air density, actual canted nozzle
    angle and distance presentation. **It does not refract the scene**; this is
    the expressly allowed lightweight approximation, not screen-space refraction.

32. **Afterburners.** Shared layered core/sheath, turbulent variation, shock-cell
    impression, nozzle illumination and independent smooth reheat transitions.
    HDR preserves bright cores before restrained bloom. Actual actuator geometry
    drives plume orientation/exit; state is replicated for remote aircraft.

33. **Particles/vapour/contrails.** Bounded pool, soft radial edges, finite fade/
    growth/expiry and fixed emitter cadence. Contrails follow cold ISA layers
    (<233 K), altitude/speed and distance emission, staying in air. Wing vapour
    uses actual supplied aerodynamic load, altitude/speed, not visual bank.
    Heat/nozzle particles follow physical nozzle rotation. Humidity, condensation
    microphysics and Schmidt–Appleman formation remain unmodeled.
    Paused aircraft do not emit stationary condensation; visual inspection caught
    and corrected nozzle particle stacking. New trails form gradually behind the
    exhaust, then expand and fade. A moving-versus-paused regression passes in
    all four build configurations.

34. **LOD statistics.** Native evaluated geometry; reduced GLBs reuse LOD0 maps
    by material name and include no duplicate image payload:

    | LOD | Triangles | Source primitives | Nodes | GLB bytes |
    |---|---:|---:|---:|---:|
    | 0 | 206,748 | 145 | 175 | 89,599,824 |
    | 1 | 48,985 | 94 | 124 | 1,914,984 |
    | 2 | 11,109 | 47 | 77 | 570,512 |
    | 3 | 4,264 | 47 | 77 | 257,328 |

    Runtime LOD0 merges to 91 draw batches, ~15 MiB mesh storage. Fine detail
    disappears after LOD0/1; distance hysteresis and configurable LOD bias remain.
    Twelve final Blender views, three actual reduced-geometry views and clay/
    compressed/retracted gear views were inspected. Native poses caught and
    corrected seat clipping and compression rotation missed by rest-pose views.

35. **Su-57 flight validation.** 60 s equilibrium checks at 130/180 m/s @1,500 m,
    250 m/s @5,000 m and Mach 1.5 @11,000 m converge; altitude/speed drift below
    numerical test limits. Automated test pilot: liftoff TAS 101.41 m/s after
    493.51 m/10.78 s brake-release roll; landing 75.22 m/s, sink 1.101 m/s,
    686.01 m rollout, peak contact 493 kN. Taxi 6.45 m/s, correct right steering,
    stopped to numerical zero. Powered 32° entry recovery finishes at .62° AoA;
    42 extreme finite-state cases pass. These are measured model scenarios,
    not OEM performance. Logs `su57-*-measurement.log` record setups/output.

36. **TV validation.** Force norms exactly retain engine thrust; summed physical
    F/M match evaluations. Symmetric −10° travel raises pitch moment from
    39.3 to 275.3 kN·m with mirrored yaw/roll cancellation. Differential travel
    produces −46.1/38.7/136.6 kN·m roll/pitch/yaw, demonstrating physical coupling.
    Actual vectoring changes integrated motion. Low-speed pitch demand uses
    4.19° peak TV at 25 m/s; at 300 m/s it uses .00256°, prioritizing surfaces.
    Limits and finite rates remain asserted independently for both engines.

37. **High-AoA energy validation.** Unpowered 55° pitch / 140 m/s / 6,000 m entry,
    two simulated seconds: minimum speed 124.87 m/s, +10.34 m altitude, total
    mechanical energy loss **47.742 MJ**. Translation/gravity/rotational energy
    are included; no fake speed clamp. All-type polar tests verify drag opposes
    velocity, lift drops post-stall and control authority at 45° is below 30%
    of the 5° value. Departure tests use actual local asymmetric flow/damage.

38. **Existing aircraft.** All old trim, taxi, takeoff, cruise, landing, engine-out,
    fuel/mass/tensor, control, wind, damage and envelope regressions pass.
    Sustained-turn results (average final 20 s of a 50 s run):

    | Type | Bank ° | TAS m/s | Sideslip ° | G | Expected G | Heading rate / expected rad/s |
    |---|---:|---:|---:|---:|---:|---|
    | A320 | 29.63 | 129.29 | .575 | 1.1385 | 1.1504 | .04230 / .04314 |
    | Falcon | 28.72 | 179.47 | .188 | 1.1353 | 1.1403 | .02950 / .02994 |
    | Typhoon | 28.41 | 179.54 | .192 | 1.1310 | 1.1369 | .02892 / .02955 |
    | SR-71 | 28.49 | 219.54 | .174 | 1.1315 | 1.1378 | .02374 / .02425 |
    | Su-57 | 30.10 | 179.42 | .328 | 1.1621 | 1.1559 | .03256 / .03168 |

    Typhoon landing pilot was corrected to flare at 18 m rather than 12 m and
    hold 85 m/s with throttle after its physically incorrect transport flap lift
    was removed. Touchdown sink .966 m/s; the <2 m/s tolerance was retained.
    Protocol-size and generic-gear fixture assertions were updated for the
    intentionally larger v7 state and actual Su-57 channels, not weakened.

39. **Multiplayer/prediction.** Stable type ID 5, protocol v7. Independent nozzle
    actuator doubles accompany existing propulsion/loading/control memory.
    Native 10 s replay is exact to acceptance precision; packet replay position
    error <.02 m with existing binary32 command rounding. Invalid nonfinite/
    over-limit nozzle states rejected. Five real GNS clients observe all four
    other types, independent Su-57 propulsion/angles and server gun capabilities.
    180 s network/combat loss/lifecycle/replay soaks pass. No bandwidth redesign.

40. **Physics performance.** Release, Intel Core Ultra 7 255H, 120 Hz tick with
    240 Hz internal subdivisions; mean/p95/p99 microseconds per mixed-world step:

    | Count | Mean µs | p95 µs | p99 µs |
    |---:|---:|---:|---:|
    | 1 | 5.252 | 5.852 | 7.122 |
    | 8 | 46.709 | 53.703 | 58.124 |
    | 16 | 99.032 | 112.627 | 121.669 |
    | 32 | 184.590 | 210.526 | 236.430 |
    | 64 | 400.680 | 618.391 | 910.888 |

    Each type individually measured ~3.65–4.96 µs mean. Warm trimmed physics,
    2,900 measured samples, not full server networking/render time. 64-aircraft
    mean uses ~4.8% of an 8.33 ms tick budget; actual worst-case scheduling differs.

41. **Graphics performance.** Native Release OpenGL/bgfx, Mesa Intel Graphics
    ARL, 1440×900, default 2K maps/MSAA4/medium shadows/bloom, VSync off. Counts
    1/2 Su-57 and 8/16/32 mixed types are measured by the native benchmark;
    warm measurements are:

    | Rendered count | FPS | Frame ms | GPU ms | Draws | Triangles | Particles |
    |---:|---:|---:|---:|---:|---:|---:|
    | 1 | 202.1 | 4.95 | 3.27 | 196 | 407,610 | 10 |
    | 2 | 196.5 | 5.09 | 3.38 | 336 | 501,482 | 12 |
    | 8 | 172.0 | 5.81 | 3.96 | 1,075 | 1,204,498 | 26 |
    | 16 | 145.4 | 6.88 | 4.55 | 1,879 | 1,951,719 | 44 |
    | 32 | 113.3 | 8.83 | 5.61 | 3,034 | 2,994,886 | 82 |

    GPU time is bgfx's reported GPU-frame statistic; CPU-frame timing is elapsed
    frame time, not isolated CPU execution. The scene includes terrain, shadows,
    HDR/bloom and UI. Counts include submitted aircraft, including off-screen
    instances; distant aircraft use actual LOD reductions. Texture residency is
    shared across instances (~470 MiB eagerly loaded aircraft textures); Su-57
    mesh ~15 MiB, all aircraft meshes ~105 MiB. Cold asset loading is excluded.
    Logs: `render_{1,2,8,16,32}.log`, `render_performance.json`, `gpu-device.log`.
    This is one warm Linux/iGPU/backend scene, not every combat/platform scenario.

42. **Texture memory.** Default 2K cap: Su-57 ~127 MiB, Typhoon ~165 MiB, SR-71
    ~178 MiB, A320/Falcon use factor materials. Source 4K maps are preserved;
    a 2K map plus complete mips costs one quarter of its 4K equivalent. Role/
    sampler-aware sharing prevents incorrect reuse. Loading all five definitions
    still costs hundreds of MiB. Future BC7 colour/BC5 normals/BC4 masks or
    KTX2/Basis would materially reduce RGBA expansion; compression/transcoding
    support was not added without a validated decoder/backend path.

43. **Build/test/sanitizers.** Debug/Release/native, headless and ASan+UBSan builds
    succeed. Expanded suites: 112 headless/sanitizer and 116 native tests, including
    new Su-57/handling/rig/material checks. Acceptance logs are listed below.
    Leak detection and halt-on-error enabled; existing GNS/system libraries are
    not instrumented. Initial combined graphical runs lost window focus, and
    simultaneous long workloads failed two timing-sensitive soaks. Separate
    graphical runs and later full headless/separate soaks pass. An initial missing
    sanitizer library path was repaired using the existing extracted runtime;
    no sanitizer check or test tolerance was disabled. Windows/D3D11 are not
    runtime tested on this Linux host. Old SR-71 modifications are preserved.

44. **Public references.** Primary links are listed below. The CAD geometry paper
    supports model dimensions; UAC/Rosoboronexport/Rostec support programme and
    engine-era identity; NASA supports aerodynamic mechanisms, atmosphere and
    solar apparent size; Airbus/Eurofighter/SR-71 sources remain in earlier
    milestone records. No generic conceptual fighter paper is treated as an
    Su-57 polar source.

45. **Reference-supported values.** SI/frames, published dimension anchors,
    twin-engine Su-57 identity, AL-41F1-era association, atmospheric trends,
    vortex/separation/vector-force mechanisms, approximate solar angular size
    and runway-marking dimension. Public source support does not establish
    hidden gear/CG/FCS/manufacturing geometry or an installed engine deck.

46. **Estimated values.** Su-57 basic/crew/fuel loading, area/MAC, engine thrust/
    TSFC/spool/lapse, tank/structure distribution, vector cant/travel/rate,
    hinge/gear/cockpit/gun points and hidden visual mechanisms. Geometry details
    are reconstructed at sensible millimetre/metre scale, not claimed exact.

47. **Calibrated approximations.** Section lift/drag/Mach/separation/vortex and
    blanking tables, FCS weights/response/soft protection, contact damping/
    friction, lighting/exposure/environment glass, cloud/sky/haze, bloom and
    exhaust turbulence/shock-cell impression. Calibrated consistency and
    regression success are narrower claims than real aircraft fidelity.

48. **Remaining realism weaknesses.** No measured proprietary Su-57 polars,
    dynamic-stall history/deep-spin validation, aeroelasticity, hydraulic/electrical
    systems or engine maps. Fuel slosh/tank routing simplified; reference CG and
    component inertias are estimates. Gear-contact deployment is command-driven;
    visual retraction is finite and reconstructed. Wind/atmosphere are bounded
    flat-Earth models; no humidity/condensation microphysics or Earth terrain.
    Efficient gun spheres are approximate volumes and gameplay damage is not
    lethality. Generic Falcon is not a particular real F-16 configuration.

49. **Remaining graphical weaknesses.** Supplied licence unverified; don't publish
    it without permission. Reconstructed gear/cockpit/fastener locations are not
    OEM drawings. Texture atlas resolution and close-up artwork remain finite.
    Single shadow map, simple environment glass, no true heat refraction,
    analytical clouds/sky, RGBA texture expansion and costly eager asset loading
    remain. No TAA/motion vectors or validated D3D11 render captures. This is a
    substantial local quality improvement, not a claim of commercial-game parity.

## Final acceptance evidence

The final full headless run passes **112/112** tests in 180.12 s. Native suites
were split to give graphical smoke exclusive window focus; the combined first
attempts recorded one graphical failure each. Every named native test has a
subsequent passing result: **116/116 Debug and 116/116 Release across split runs**.
This is not a claim that the initial combined logs were all green.

| Configuration | Evidence under `output/m3_68/` | Result |
|---|---|---|
| Headless | `tests-headless-final.log`, `final-rig-regressions.log`, `headless-particle-final.log` | 112/112 full suite; final gear and contrail regressions pass |
| Debug | `tests-debug.log`, `tests-debug-soaks.log`, `debug-graphical-smoke.log`, `debug-particle-final.log` | 113 non-graphical tests + two soaks + standalone smoke pass; smoke 44.73 s |
| Release | `tests-release.log`, `tests-release-soaks.log`, `release-graphical-smoke.log`, `release-final-regressions.log`, `release-particle-final.log` | Same 116-test coverage; final smoke 13.90 s; final rig/input/interpolation/asset checks pass |
| ASan + UBSan | `tests-sanitize.log`, `tests-sanitize-soaks.log`, `sanitize-final-rig.log`, `sanitize-particle-final.log` | 110 ordinary tests + two 180 s soaks pass; final rig and contrail checks pass; leak detection enabled |
| Builds | `build-debug.log`, `build-release.log`, `build-headless.log`, `build-sanitize.log` | All succeed |

Soak wall times: Debug 180.13 s, Release 180.11 s, sanitizer 180.17 s. Sanitizer
ordinary suite: 266.11 s. System dependencies/GNS are not instrumented. Final
particle regressions add a paused-emission assertion without reducing prior
moving-trail coverage. Final native inspection covers the 12 Blender views,
all four LODs, texture-free geometric gear/surfaces, compression/retraction,
26 Su-57 render scenarios and nine shared renderer/combat/settings scenarios.
The landing/post-stall/recovery demos integrate real offline physics; other
paused fixtures demonstrate pose/graphics and are labeled accordingly. Landing
starts upstream of the finite runway; starting at its centre would put the
same descent beyond the pavement. Actual measured landing performance remains
the separate 120 s automated scenario in point 35.

Source Downloads hash is unchanged. The final working blend and all four GLBs
have refreshed SHA256 entries in `asset-sha256.json`. The model licence remains
unverified and integration remains local.

## Sources

- [Su-57 public CAD dimension research, Türe/Paker, Eleco 2025, table 2](https://www.eleco.org.tr/ELECO2025/Eleco2025-Papers/29.pdf).
- [UAC Su-57 programme](https://www.uacrussia.ru/en/aircraft/lineup/military/su-57/).
- [Rosoboronexport Su-57E factsheet](https://roe.ru/upload/iblock/09e/tmsmssri09ubvr63gxqe3gg3s9jug5pd.pdf).
- [Rostec Su-57/AL-41F1 programme context](https://elements.rostec.ru/media/news/istrebitel-su-57-pyatoe-pokolenie-na-vzlet/).
- [NASA F-18 HARV](https://www.nasa.gov/reference/f-18-harv/) and [NASA strake/delta interaction study](https://ntrs.nasa.gov/citations/19810017536).
- [NASA atmospheric properties](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/air-properties-definitions/) and [solar apparent size](https://science.nasa.gov/learn/basics-of-space-flight/chapter1-1/).
- [Airbus A320 airport planning dimensions](https://aircraft.airbus.com/sites/g/files/jlcbta126/files/2023-02/Airbus-techdata-AC_A320_0322.pdf).
- [Eurofighter manufacturer](https://www.eurofighter.com/), [Austrian Air Force dimensions](https://www.bundesheer.at/unser-heer/waffen-und-geraet/eurofighter-ef-2000), and [prior SR-71 references](M3_67_SR71_VALIDATION.md).
- [FAA current airport marking AC](https://www.faa.gov/airports/resources/advisory_circulars/index.cfm/go/document.current/documentNumber/150_5340-1) and [official consolidated marking PDF](https://www.faasafety.gov/files/events/SO/SO15/2024/SO15129467/AC150-5340-1M-Chg-1-Airport-Markings.pdf).
- [Official Blender OCIO config used for the local compatibility repair](https://raw.githubusercontent.com/blender/blender/v4.3.2/release/datafiles/colormanagement/config.ocio).

## Evidence locations

`output/m3_68/`: original/evaluated/normalized inventory, asset hashes, scale/mass
measurement, physics measurements, builds, tests, soaks, benchmarks and inspection
contact sheets. `docs/images/m3_68_su57/`: final Blender views, texture-free clay,
actual LOD reductions, compressed/retracted gear, native cameras/gear/high-AoA/
vectoring/high-altitude/contrail/mixed/afterburner/day/dark/physics-demo captures.

Three captures illustrate the gear work specifically: the actual modeled gear as
finally authored, a texture-free geometric inspection of that gear, and the
native runway stance after the compression correction.

Capture images and validation artifacts are generated and are not version
controlled; regenerate them with `scripts/capture_m3_68.py` and
`scripts/benchmark_m3_68.py`.

The source audit's original generic-name interpretation was corrected after
orthographic views: circular internals were compressor/nozzle parts, not wheels.
The source Low collection was not reused as a fake LOD. Initial Blender renders
used an incompatible OCIO fallback; final AgX views use the validated local
configuration. The original supplied .blend was never overwritten.
