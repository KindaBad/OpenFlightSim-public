# M3.68.1 — Realism, Validation & Packaging Closure

Measured on the Linux development workstation, 4 October 2026. This report
supersedes earlier milestone counts and the historical realism truth table.
M3.7 replication architecture remains future work. Generated aircraft content,
imported reference data, benchmark output and JUnit logs remain local under the
source-only repository policy.

## Implementation and evidence contract

`tests/validation/` provides reusable scenario metadata, fixed acceptance bounds,
explicit PASS/WARN/FAIL, source locators, absolute/percentage error, authority,
and limitations. Metadata records variant, mass/fuel/payload/CG assumptions,
altitude/ISA/wind, runway/flap/gear/engine conditions and measurement procedure.
A non-comparable reference always produces WARN, including accidental numerical
agreement. Mathematical, plausibility, external-source and content evidence
have separate CTest labels. A successful diagnostic test can contain a WARN;
CTest success means the diagnostic/reporting contract ran, not that the warned
aircraft result became externally validated.

Focused physical fixes are distributed payload inertia under loading, SR-71
product-of-inertia sign and basic-airframe CG translation, and geometry/mass-derived
Typhoon inertia. No aerodynamic/FCS gains were fitted to improve these results.
Renderer changes measure texture allocations and share the tested BGRA/RGBA
conversion. Debug bgfx assertions also exposed duplicate sky/effects uniforms;
redundant submissions were removed, preserving the shared values and render passes. Content tests explicitly distinguish unavailable packs from passes.
The only network behavior fix retains complete elapsed time in presentation
clocks; combat test timing now starts after the real handshake barrier.

## Parameter provenance

All production aircraft export 139 configured parameters, including distributed
payload inertia. Configuration/runtime values and source hashes are checked.
The four categories mean public anchor (REFERENCE), calculation from named inputs
(DERIVED), unsupported engineering assumption (ESTIMATE), and reconstructed curve
or control response (CALIBRATED_APPROXIMATION). None implies an aircraft-wide
accuracy claim. Duplicate global/per-engine anchors are counted as exported
records, not independent pieces of evidence.

| Manifest | REFERENCE | DERIVED | ESTIMATE | CALIBRATED_APPROXIMATION |
|---|---:|---:|---:|---:|
| A320 | 3 | 9 | 127 | 0 |
| Typhoon | 6 | 6 | 121 | 6 |
| SR-71 | 2 | 4 | 133 | 0 |
| Su-57 | 0 | 7 | 132 | 0 |

Shared nonlinear aerodynamic, transport engine and FCS laws are separately marked
CALIBRATED_APPROXIMATION in `data/models/engineering-fallback.json`. Authoring
geometry/source records contain additional public anchors that are not scalar
flight-configuration exports. The optional NASA importer uses the same vocabulary.
See [SU57_PHYSICS_PROVENANCE.md](SU57_PHYSICS_PROVENANCE.md) for important parameter
values, units, sources and confidence, and [MASS_ENGINE_AUDIT.md](MASS_ENGINE_AUDIT.md)
for the four load and propulsion reconstructions.

Su-57 dimensions, area, basic mass, fuel, engine thrust, nozzle axes/limits/rates,
CG, aero/control effectiveness and protection assumptions remain ESTIMATE.
The tensor and payload shape terms are DERIVED from estimated component geometry.
There is no enforced, source-validated MTOW parameter. Shared separation/vortex
and reconstructed control responses are CALIBRATED_APPROXIMATION. No exact OEM
or classified Su-57/Typhoon control-law or post-stall claim is made.

## Public-source agreement and A320 operations

| Case | Simulated | Reference | Absolute error / percent | Status and evidence |
|---|---:|---:|---:|---|
| A320 66 t full-flap approach proxy | 137.661970 kt | 136 kt | 1.661970 kt / 1.2220% | PASS, existing ±5 kt bound; one informative public planning point |
| A320 CFM56-5B4/P static rating | 120,100 N/engine | 120,100 N | 0 / 0% | PASS ±1 N, source/units anchor already used in model |
| Typhoon EJ200 nominal reheat | 90,000.450586 N/engine | 90,000 N | .450586 N / .0005% | PASS ±1 N, nominal anchor, not installed flight validation |
| SR-71 zero-fuel Ixx/Iyy/Izz/Ixz | 299,174.788479 / 1,294,602.767964 / 1,589,071.512344 / −26,031.704608 kg m² | Same published tensor after SI/product-sign conversion | <1e−6 kg m² each | PASS, four source/units checks, not four independent trajectories |
| SR-71 nominal reheat diagnostic | 151,240.443539 N/engine | 144,567.202496 N | 6,673.241043 N / 4.6160% | WARN, public nominal figures differ; no acceptance tolerance assigned |
| A320 stall proxy | 111.920301 kt | 110.569106 kt | 1.351195 kt / 1.2220% | PASS, dependent approach/1.23 calculation; plausibility only |
| A320 64 t Mach .78 / 11 km ISA trim | .875930 throttle | Available [0,1] | Within range | PASS, equilibrium/plausibility only |

The A320 approach uses a realized static lift scan, 66,000 kg = 42,000 basic +
10,000 fuel + 14,000 payload, full flap/gear, zero wind, ISA, equivalent-speed
proxy. It preserves the existing result without hardcoding a speed. The generic
1.23 multiplier and static-polar procedure do not reproduce exact VAPP/VLS,
instrument error or dynamic stall. `aircraft_audit.performance` and the new
scenario runner repeat the same point; count it once as flight-performance evidence.

Primary sources and scope:

- [Airbus A320 Aircraft Characteristics, July 2025](https://www.aircraft.airbus.com/sites/g/files/jlcbta126/files/2025-07/AC_A320_20250715.pdf), 3-5-0 p1: the informative 66 t full-flap speed. Published runway planning curves do not supply a complete certified test procedure.
- [EASA E.003 issue 06](https://www.easa.europa.eu/sites/default/files/dfu/TCDS%20EASA%20E.003%20issue%2006.pdf), III 6.1 p12: selected CFM56 rating; no installed/off-design deck claim.
- [EUROJET fact sheet](https://www.eurojet.de/wp-content/uploads/EUJ_Factsheet_A4_Ansicht.pdf): EJ200 nominal ratings and TSFC bands, not a full engine deck.
- [NASA/TP-2002-210718](https://ntrs.nasa.gov/api/citations/20020057965/downloads/20020057965.pdf), Table 2 and equations 13/14: baseline SR-71 zero-fuel reconstruction and negative matrix XZ entry. Its 34,000 lbf thrust-class description differs from [NASA FS-030](https://www.nasa.gov/wp-content/uploads/2021/09/495839main_FS-030_SR-71.pdf)'s 32,500 lbf nominal figure. Neither validates the full runtime engine curve.

A320 operational outputs remain diagnostics with explicit mismatches:

| Diagnostic | Measurement | Planning comparison / limitation |
|---|---|---|
| 64 t takeoff, ISA sea level, no wind, 1+F surrogate, no FLEX | 2,046.5 m to CG15.24 m; rotation 1,012.6 m at 78 m/s | Approximate planning 1,330±100 m, +716.5 m numerical difference; rotation, flight-path/screen definition and certified procedure differ |
| 66 t landing, 136 kt threshold, full flap/gear, programmed flare, no reverse | 933.3 m; 281.8 m flare +651.6 m rollout; 1.69 m/s sink | Approximate unfactored planning 954±48 m, −20.7 m; threshold is CG height and braking/flare assumptions are not certified |
| Clean glide 150 m/s /3 km | L/D17.49 at 7.2°, sink8.56 m/s | No independent matched flight reference |
| All-engine climb 100 m/s /1 km | Excess-thrust grade .213 /21.33 m/s | No source-matched climb/engine-out validation |

Reliable public matched stall/climb/idle-descent/takeoff/landing datasets were
not established. These cases are not promoted to external validation. The
unresolved takeoff difference warrants further ground/high-lift/pilot-procedure
work before certified-performance claims, independently of networking work.

## Mass, engines and physical thrust vectoring

`closure.mass` checks 27 load/offset combinations for each production aircraft:
symmetric positive-definite tensors, principal-moment triangle inequality,
radii of gyration, continuous fuel increments and independently calculated
payload covariance plus parallel-axis increments. Body axes are forward/right/down,
world axes north/east/down; kg, metres and kg m² remain internal units.

`closure.engines` writes 2,020 stabilized samples per aircraft: four altitudes,
five Mach points and 101 power points. It checks monotonic power response,
nonnegative fuel flow, continuous AB transition, finite spool response,
independent left/right operation, and failed/starved engine zero thrust/flow.
Supersonic A320 points are numerical robustness samples outside its envelope.
Curves/TSFC remain approximations beyond identified public rating anchors.

`closure.vectoring` exercises **432** fuel/payload/CG/failure/nozzle combinations.
Independent Rodrigues rotation reconstructs directions and moving exits, then
sums `(exit−loaded_CG) × force`. Maximum measured moment disagreement was **0 Nm**;
maximum Euler angular-derivative disagreement was **0 rad/s²**, within declared
1e−7 Nm /1e−10 rad/s² bounds. Neutral, symmetric, differential, left/right/both
failed and fuel-starved cases are covered. Visual pose channels match actual
left/right nozzle angles. Continuous angular acceleration remains the full tensor
Euler equation, with aerodynamic and physical thrust moments; FCS requests
finite actuator motion. No direct attitude or angular-velocity control was added.

## Su-57 maneuver evidence

Existing trim covers subsonic, high-subsonic and supersonic entries; roll/pitch/yaw,
sustained turn, takeoff/landing, low speed, stall/departure recovery and engine
regressions remain active. New traces record speed/height/energy, alpha/beta,
body rates, both nozzles and aerodynamic/propulsive moments. Energy includes
translation, gravitational potential and full-tensor rotational energy; force
and moment work is integrated independently at 120 Hz output/240 Hz integration.
Fuel consumption is disabled in these energy fixtures, avoiding changing-mass work.

Unpowered two-second entries at 140 m/s /6 km:

| Initial AoA | Min/final speed (m/s) | Height change (m) | Mechanical energy change (MJ) | Max pitch rate (rad/s) | Aero pitch-moment magnitude (Nm) |
|---|---:|---:|---:|---:|---:|
| 35° | 129.489 | +17.340 | −31.279377 | .4897 | 882,439.3 |
| 55° | 125.003 | +6.631 | −48.245999 | 1.1669 | 944,662.2 |
| 70° | 114.768 | +3.752 | −79.748856 | 1.2753 | 782,114.1 |

Sideslip/roll/yaw and nozzle/thrust moments remain zero in these symmetric
unpowered entries; max realized alpha is 34.947/54.960/69.985°. Work-accounting
residuals are 70.4/179.7/212.0 kJ, below the fixed 2% plus 1 kJ numerical bound.
These losses establish dissipative behavior, not exact real Su-57 post-stall motion.

At25 m/s with full thrust and .3 pitch input, enabled vectoring reaches 5.957°
nozzles and .2066 rad/s peak pitch rate versus .1833 with vectoring disabled.
Final speeds 34.107/34.138 m/s and energy gains 5.588/5.635 MJ are accounted for
by ~5.733 MJ propulsive work and small aerodynamic loss. Propulsive pitch moments
are 146,723 /31,981 Nm; the neutral thrust offset still produces a moment, so these
are total propulsion moments, not isolated incremental TV moments. This powered
low-speed test is not a Cobra or energy-conserving post-stall assertion.

The 300 m/s high-G entry loses 274.790 MJ and 49.018 m/s while gaining260.54 m;
peak 11.805 g exposes the reconstructed **soft** +9 g command protection's
transient overshoot. It is not proof of real-aircraft protection or a certified
load envelope. Engine-out traces verify opposite yaw signs with physical
asymmetric thrust, small speed changes and independently accounted work.

## Production assets, scale and licensing

All four local aircraft and all 13 registered production GLBs were present and
checked: A320 plus four authored LODs for each fighter/reconnaissance aircraft.
Conformance covers finite transformed geometry, dimensions, CG/contact anchors,
material/rig channels and authored/generated LOD contracts. Policy fixtures prove
missing required models fail, optional missing packs return77/NOT RUN, and broken
GLBs, wrong scale and absent articulation fail. Source/core runs now visibly skip
unavailable production-content tests instead of implying they passed.

| Aircraft | Measured length /span /height (m) | Dimensional errors (m) | Largest percent error |
|---|---|---|---:|
| A320 | 37.570000 /35.799995 /11.770000 | ~0 /−.000005 /+.010000 | +.0850% height |
| Typhoon | 15.960000 /10.950000 /5.285380 | ~0 /~0 /+.005380 | +.1019% height |
| SR-71 | 32.740601 /16.941799 /5.638800 | +.000001 /−.000001 /~0 | <.00001% |
| Su-57 | 20.100002 /14.100000 /4.600000 | +.000002 /~0 /~0 | <.00001% |

The existing .15 m export tolerance was retained. These are conformance to configured
targets, not measurements of real-aircraft planform accuracy.

[ASSET_RELEASE_PROVENANCE.md](ASSET_RELEASE_PROVENANCE.md) records Su-57 donor
identity, unverified license and derivative restrictions. Donor baselines
17.1699963×12.3671279×2.78005594 m are earlier authoring measurements, not a fresh
original-file measurement here. Canonical length/span/vertical corrections are
1.1706467313/1.1401192030/1.1706467313. Span is 2.6077% narrower than uniform
length scaling would produce; uniform scaling gives 14.4775 m instead of 14.1 m.
Normalization is baked into exported geometry; runtime scales are (1,1,1).
No fresh Blender export or artwork modification was performed.

The donor and derivatives remain **LOCAL DEVELOPMENT ONLY / REDISTRIBUTION NOT
VERIFIED**. Public content release requires verified rights or an original/licensed
replacement. This remains a packaging blocker. Exhaust haze is translucent
schlieren animation; it does not refract scene imagery.

## Texture-memory measurement

RGBA8 allocations at the existing default 2K cap, complete mip chains and renderer
sampler deduplication; authored LODs reuse texture handles:

| Aircraft | Estimated image-resource bytes | MiB |
|---|---:|---:|
| A320 | 0 (material colors) | 0 |
| Typhoon | 173,364,564 | 165.333 |
| SR-71 | 186,646,536 | 178.000 |
| Su-57 | 134,217,720 | 128.000 |
| All aircraft | 494,228,820 | 471.333 |

Runtime logs confirm those allocations. White/font/cloud-noise add 393,220 bytes,
for **494,622,040 bytes (~471.708 MiB)** of loaded image resources on this host.
Framebuffer attachments, geometry, allocator padding and driver overhead are
excluded, so this is not total measured VRAM. The largest uploaded 2K square maps
cost22,369,620 bytes each, 12 mips; Su-57 sources are 4K square. Logs name source,
source/upload dimensions, runtime format, mips and bytes per upload.
BC7/BC5/BC4 block-aware accounting prepares cost comparison; compressed
KTX2/Basis decoding/transcoding/uploads are not implemented. Quality is unchanged.

## Network baseline before M3.7

Protocol8 layout and 24 Hz full-snapshot architecture are unchanged. MB/s below
is unicast snapshot application payload to all N clients, `bytes×24×N/1e6`,
excluding transport/encryption/fragmentation overhead, inputs and reliable events.
CPU timings are Release mean per operation on this host.

| Aircraft | Snapshot bytes | Encode µs | Decode µs | Outbound MB/s |
|---:|---:|---:|---:|---:|
| 2 | 1,190 | 1.492 | .640 | .057 |
| 8 | 4,562 | 4.501 | 1.754 | .876 |
| 16 | 9,058 | 9.318 | 3.342 | 3.478 |
| 32 | 18,050 | 17.796 | 6.042 | 13.862 |
| 64 | 36,034 | 34.435 | 11.994 | 55.348 |

Snapshot size remains66+562N bytes. Input packets with 1/2/4/8 commands are
97/157/277/517 bytes, 37+60N; maximum 32-command batch is1,957 bytes by layout.
These sizes are the intended M3.7 comparison baseline; no AOI, deltas,
quantization, baseline/keyframe or MTU architecture was started.

The lossy combat fixture previously consumed respawn-observation time during
connection setup. It now starts the unchanged 20 s observation interval at all-client
readiness, with a separate 5 s handshake deadline and unchanged loss/assertions.
A real presentation-clock bug discarded elapsed time above 250 ms, leaving the
render clock behind bounded history and causing an observed 50.418 m eviction jump.
Full elapsed time now advances clocks; smoothing still uses its existing cap.
A deterministic 2 s frame regression verifies 240 tick advancement without a 2 s
sleep. Existing impairment/loss/long-run/soak and combat regressions remain active.

## Before/after performance

Release baseline commit98740cf versus this closure, same workload, 3000 ticks,
first 100 discarded, fixed 1/120 s steps, default semi-implicit Euler. Runs were
captured without builds or other test workloads. Each timing covers the complete
set of N aircraft on one thread. These are host measurements, not controlled
multi-run confidence intervals.

| Aircraft | Before mean /p95 /p99 µs | After mean /p95 /p99 µs | Mean change |
|---:|---|---|---:|
| 1 | 8.289 /8.391 /10.565 | 8.401 /8.468 /9.209 | +1.35% |
| 8 | 76.718 /91.542 /96.417 | 77.272 /92.116 /96.703 | +.72% |
| 16 | 153.513 /183.939 /188.915 | 151.992 /184.676 /187.691 | −.99% |
| 32 | 306.772 /370.148 /375.733 | 303.756 /358.676 /363.765 | −.98% |
| 64 | 613.693 /736.176 /745.874 | 615.471 /735.888 /744.623 | +.29% |

The largest group remains ~.616 ms mean against the 8.333 ms 120 Hz tick budget
for physics alone. Variation is consistent with ordinary host noise; no
significant runtime regression is demonstrated. Provenance comparison, trajectory
CSV and content measurements are test/startup diagnostics, not force-loop metadata
lookups. The small distributed-payload correction is one vector scale/add per
mass evaluation. The source/core run spends **.072 s across 19 new scenario, closure, reporting
and screenshot/texture-cost tests**, excluding production texture loading.
New validation wall times are recorded in JUnit; diagnostics add test work rather than recurring release frame work.

## Tests, sanitizers and environment

Final verification coverage (each test name once per profile):

| Profile | Passed | Failed | Skipped / NOT RUN | Scope |
|---|---:|---:|---:|---|
| Native Debug | 149 | 0 | 0 | Full profile plus graphics retest after renderer fixes |
| Native Release | 149 | 0 | 0 | 147 non-graphics checks plus two isolated native graphics checks |
| Headless Debug | 145 | 0 | 0 | Full core, network, physics and local production content |
| Source/core Release, empty content root | 101 | 0 | 3 | Network/client disabled; content genuinely absent |
| Required-content Release | 5 | 0 | 0 | Complete asset-conformance label, required pack installed |
| NASA reference Release | 105 | 0 | 0 | Core/content plus pinned independent computational checks, network disabled |
| ASan + UBSan Debug with NASA reference | 146 | 0 | 0 | Complete instrumented headless profile including local NASA checks; 397.89 s |

The three source/core skips are `assets.production`, `aircraft_audit.geometry`
and `assets.texture_memory`. Test profiles overlap, so these rows should not
be summed as independent evidence. NASA's single CTest case contains 96 aerodynamic
coefficient comparisons from 16 published shots and nine propulsion comparisons,
with unchanged source tolerances. It validates source-model implementation, not
flight tests of a real aircraft.

Raw Debug full run: 148 passed, 1 failed, 0 skipped; bgfx rejected duplicate sky
uniforms. The first focused graphics retry was 1 passed, 1 failed, 0 skipped and
exposed the duplicate effects view-projection uniform after fixing the sky.
Both were removed; the final graphics retry is 2 passed, 0 failed, 0 skipped
(smoke 37.86 s, shader .22 s). Release's isolated native graphics result is 2 passed,
0 failed, 0 skipped (13.88 s total). The final coverage rows use the latest result
for each test, while retaining both failed attempts locally. No physics/network
assertion or smoke deadline was weakened.
JUnit reports in `output/m3_68_1/` are summarized by
`scripts/summarize_validation.py`, which keeps skips separate from passes.
Final coverage combines a full profile with focused retests when only the renderer
changed; each test name is counted once using its latest result. Raw failed
attempts are retained and described below. No previously failed run is represented
as a clean original run. Early stale
provenance exports and changed asset-skip expectations were corrected, not ignored.
An earlier Release full attempt was 146 passed, 2 failed, 0 skipped: the real
`network.bad` presentation-clock failure and a graphical deadline under competing
work. The retained initial combat failure motivated the handshake barrier fix;
subsequent Debug/Release/headless impairment and full 180 s soaks passed. Those
failed attempts are not included as successes in final coverage.
Native graphics required SDL's supported relative-mouse warp fallback on this
host; a smoke run competing with heavy sanitizer work hit its 45 s deadline.
Final native graphics are isolated from sanitizer jobs.

**ASan: zero findings; UBSan: zero findings; LeakSanitizer: zero findings.**
The complete 146-test run passed, including both 180 s soaks, lossy regressions,
production-content checks and the optional NASA comparison. ASan/UBSan retain
leak detection and halt-on-error. This rootless workstation uses
`.cache/sanitizer-runtime` and `.cache/sysroot/usr/lib64`; existing combat RSS
checks require a 4 MiB ASan quarantine and 64 KiB thread-local quarantine.
These settings reduce instrumentation retention, not memory checking or assertions.
Windows/D3D11, TSan and a newly authored Blender content export were not run on
this Linux host. GPU allocation estimates are not hardware VRAM queries.

## Remaining uncertainty and M3.7 readiness

No exact military aero/FCS, dynamic stall, separation identification, tank burn
sequence or installed engine deck has been validated. Only one retained A320
informative approach point supplies independent flight-performance agreement;
source anchors and computational checkcases must not be counted as more flight
measurements. The takeoff discrepancy and transient Su-57 soft-limit overshoot
remain visible. Model bounds and mechanics do not establish OEM shape fidelity.
Su-57 redistribution rights remain a public-content release blocker.

M3.7 can begin as networking work: these remaining issues do not require a
replication redesign delay, and the protocol/bandwidth/performance baseline is
now explicit. Preserve all closure regressions and evidence limitations while
changing replication. Serious blockers remain for certified-realism claims and
public Su-57 content packaging, not for starting M3.7.
