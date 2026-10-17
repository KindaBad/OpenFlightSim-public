# Whole-Earth map: plan

Status: plan only, nothing built. Written 2026-10-14 against 0.6.9.2000.

Players have asked for a second map that is the whole Earth: real coastlines,
mountains and airports, round, and big enough to fly anywhere. This document
says what that takes in this code base, in what order to build it, and what the
owner has to decide first.

The current valley stays as the first map. The Earth is added beside it.

## Where the game is today

- **Flat world.** Every position is `State::pos_ned`, metres north, east and
  down from the airfield ([COORDINATES.md](COORDINATES.md)). Altitude is
  `-pos_ned.z`; "down" is always `(0,0,1)`. About 600 uses of `pos_ned` in 75
  files, and about 300 of `vel_ned`, rely on this.
- **Terrain is a formula.** `terrainElevation()` in
  [terrain.hpp](../core/include/ofs/terrain.hpp) builds the land from integer
  hashing, so the server, every client's prediction and the picture compute the
  same ground with no data files. It is sampled through one polar mesh 64 km in
  radius, built once at start-up.
- **Lakes** are worked out from that formula over a 96 km square
  ([water.hpp](../core/include/ofs/water.hpp)).
- **Three airfields** at fixed offsets (`kAirfieldSites`), with team bases and
  structures placed relative to them ([bases.hpp](../core/include/ofs/bases.hpp)).
- **Renderer** draws the polar mesh out to 262 km and lets the sky continue the
  plane beyond it (`renderer_environment.cpp`). It already rebases its render
  origin around the camera in double precision.
- **Network** sends remote aircraft relative to a binary64 reference point, so
  aircraft positions already scale to Earth size. But missile positions are
  absolute `signed32` at 1/8 m (±268 km), and radar tracks are `signed32` at
  0.5 m (±1,074 km). Both break beyond those ranges.
- **Already round in places:** weapon line of sight uses an Earth-radius
  horizon, and the sun takes a latitude.

## The approach

### One exact frame for the whole planet

Positions stay Cartesian doubles, but the frame becomes **Earth-centred
coordinates rotated so that, at the game's anchor point, x is north, y is east
and z is down**. At the anchor nothing looks different. Everywhere else the
frame is still exact: it is just the planet seen from the anchor. There are no
re-anchoring jumps or seams, and double precision gives sub-millimetre
resolution at Earth radius.

What changes is that "down", "altitude" and "level" are no longer properties of
the axes. They become functions of position:

| Today | Becomes |
|---|---|
| `-pos.z` (altitude) | `world.altitude(pos)`: height above the WGS84 ellipsoid (sea level) |
| `(0,0,1)` (down) | `world.down(pos)` |
| NED axes for HUD, wind, Euler angles | `world.localNed(pos)`: rotation from game frame to the local north-east-down |
| `groundHeightNed(n, e)` | `world.ground(pos)`: terrain height and normal at that point's latitude and longitude |
| gravity `(0,0,g)` | `g · world.down(pos)` (standard gravity is enough) |

Code that only uses differences between positions (guidance, closing speeds,
ranges, collisions between aircraft, the network's interest grid) does not change.

The Earth does not rotate in the simulation, so there is no Coriolis effect. That
is normal for games and keeps the flight model unchanged.

### The valley keeps its exact behaviour

A `World` interface carries those functions. `FlatWorld` implements them exactly
as the code does today: `altitude = -z`, `down = (0,0,1)`, and the procedural
valley. `EarthWorld` implements the round planet. Moving the code onto the
interface is a pure refactor: valley results, scenario tests and aircraft
provenance hashes must not change.

### Elevation data

| Layer | Source | Licence | Resolution | Size (estimate) |
|---|---|---|---|---|
| Base heights on land | NOAA ETOPO 2022 | public domain | 15 arcsec (~460 m) | 7.5 GB raw for the whole grid; land is ~29% of it (~2.2 GB raw), so roughly 0.7–1.2 GB compressed as a pack |
| Sea floor and shallows | NOAA ETOPO 2022 | public domain | 60 arcsec (~1.8 km); only sea level and the coast are visible | tens of MB |
| Fine heights (later stage) | Copernicus DEM GLO-90 | free, attribution required | 3 arcsec (~90 m), land only | ~1 MB per 1° tile compressed, ~25 GB for all land |
| Land cover (colour and material) | ESA WorldCover, resampled to ~250 m | CC BY 4.0 | 10 m source | a few hundred MB after resampling |
| Airports and runways | OurAirports | public domain | — | a few MB |
| Lakes and rivers | Natural Earth | public domain | — | tens of MB |

Licence terms are to be re-checked in phase 0. Sizes are estimates until the
converter has been run.

Heights are stored as 16-bit values on a **cube-sphere quadtree** (six faces,
each split into tiles). That avoids squashed tiles at the poles, and the same
tiles are used for collision and drawing. Below the data's resolution, the
existing deterministic noise adds detail, scaled by slope and land-cover class,
so a ~500 m grid still looks like mountains up close.

**Collision and the picture use the same heights.** The server, the client's
prediction and the renderer all sample the same pack through one core function,
so a plane can never hit hills that aren't drawn or fly through hills that are.
The pack carries a content hash, and a client whose pack does not match the
server's is refused at join, just as the protocol version is today.

Under every airport runway the terrain is flattened by a deterministic blend, so
runways are level.

### Drawing the planet

- Chunked level-of-detail terrain on the cube-sphere quadtree. Each chunk's
  vertices are stored as floats relative to the chunk centre, and each frame is
  rotated so the camera's local up is render +Y. The existing double-precision
  render-origin rebasing carries over.
- A background thread loads tiles from the memory-mapped pack, decompresses them
  and builds meshes, with a per-frame budget for uploads to the GPU. Tiles are
  prefetched ahead of the aircraft: at 1 km/s (the SR-71), a 300 km lookahead
  gives five minutes of warning.
- A curved horizon. At 25 km altitude the horizon is about 565 km away, so the
  far quadtree levels must be cheap.
- A global ocean at sea level, replacing lakes-only water outside the valley.
- Ground materials chosen by land-cover class, reusing the existing terrain
  layer textures.

### Where players play

The Earth is 40,000 km around, so players spread over it would never meet. A
game on the Earth map is started at a **theatre**: an anchor latitude and
longitude, and a set of real airports to spawn at. Players spawn there and can
fly anywhere. The first release is free flight only. Team games on the Earth map
come in a later update. The valley's team-game rules will carry over then, with
team bases and structures placed per theatre.

The first theatres are **Austria**, **the Alps** and **the Persian Gulf**. Austria
lies in the eastern Alps, so the Alps theatre is anchored on the western Alps
(Switzerland, northern Italy and south-east France) to keep the two apart.
Exact anchors and spawn airports are chosen in phase 5 from OurAirports.

## Phases

Each phase is a mergeable stage on a feature branch. Nothing is released until
the owner says so.

### Phase 0: decisions and a data test
- Owner decisions (see below).
- Re-check the data licences, and add credit entries to `licenses/`.
- Prototype `scripts/build_earth_terrain.py`: convert ETOPO into the tile pack,
  then measure its size and how fast a tile loads.
- Done when the pack's size and format are known. (Hosting is decided: GitHub.)

#### Phase 0 results (2026-10-14)

The prototype is [scripts/build_earth_terrain.py](../scripts/build_earth_terrain.py).
It works under `build/earth/` with a local venv holding numpy, tifffile,
imagecodecs and zstandard.

- **Source:** NOAA ETOPO 2022, 15 arcsec, surface elevation. 288 GeoTIFFs of
  15° × 15°, 4.9 GB, downloaded from NCEI in about 45 minutes. Each tile's
  corner tie point is its north-west edge, with pixels as areas. They are
  ingested in 50 s into one 86400 × 43200 int16 grid of half metres (7.5 GB, kept
  local). Heights run from −10,907 to 8,586 m. Summits are averaged down by
  the ~460 m cells: Everest shows as 8,586 m, Mont Blanc as 4,655 m.
- **Tiles:** an equal-angle cube, 3 × 3 root tiles per face and 6 levels, down
  to 96 × 96 leaf tiles per face. Each tile has 225 × 225 samples with shared
  edges. Leaf spacing is ~465 m at a face centre. That's 73,710 tiles in all,
  and a full build takes about 4.5 minutes on 6 cores.
- **Encoding:** 2D Lorenzo prediction, zigzag, low/high byte planes, then
  zstd 19. Measured on 26 Alpine leaves (the hardest terrain):

  | Encoding | KiB per leaf |
  |---|---|
  | raw int16 + zstd | 69.4 |
  | Lorenzo + zstd | 56.9 |
  | + byte planes | 48.4 (xz: 47.6) |
  | + 1 m steps | 41.9 |
  | + 2 m steps | 35.7 |

  zstd 22 and xz gain under 2%, and xz decodes much slower, so zstd 19 stays.
- **Whole pack:**
  - First build (0.5 m steps, no byte planes, coarse sea floor kept): **1,286 MiB**.
  - Chosen settings (1 m steps, byte planes, open sea stored as a flag only):
    **799 MiB**. That's 505 MiB of leaves and 294 MiB of coarser levels, for
    20,250 land leaves and 35,046 open-sea leaves. It's one file, under
    GitHub's 2 GiB limit, so no split is needed.
  - "Open sea" means a tile whose every sample is below −10 m.
  - 1 m steps are below ETOPO's own vertical accuracy (several metres).
- **Load speed:** 0.43 ms per land tile to decompress and decode, in Python and
  numpy. C++ will be faster. At that rate, streaming is not the bottleneck.
- **Check:** a hillshade of the Alps built from the pack shows the Alpine arc,
  the Po valley, the Jura, Istria and the Croatian islands in the right places.
  Inland lakes are not water in ETOPO (Lake Geneva is land at its surface
  height), so they need the Natural Earth lakes in phase 3.
- **Further savings, not taken:**
  - The coarser levels could be rebuilt from the leaves on the player's machine
    after download. That's a ~505 MiB download, and the build would be about a
    minute of work once.
  - 2 m steps would cut a further ~15%.
- **Still to decide in the format:**
  - Lakes and land below sea level: a land/water mask, rather than "height ≤ 0".
  - A proper filtered downsample for the coarse levels. The prototype samples
    the source directly at each level's spacing.
  - Final cube orientation, so tile edges match exactly across faces.


### Phase 1: the `World` refactor, with no change in behaviour
- Add the `World` interface and `FlatWorld`.
- Route the `pos_ned` altitude uses, "down" vectors, NED-for-display and
  terrain/water calls through it: core flight, ground service, weapons and
  ballistics, damage and ejection, bots, client camera, HUD, effects, renderer.
- Done when every test, `regression.provenance` and the valley scenarios pass
  unchanged. This is the riskiest phase because it touches the most files. It
  should not be mixed with any feature work.

#### Phase 1 results (2026-10-14)

Done on `feature/whole-earth`. The interface is `ofs::WorldShape`, reached
through `ofs::world()`, in [world.hpp](../core/include/ofs/world.hpp); see
[COORDINATES.md](COORDINATES.md). (`World` was taken by the network game
state, so it was renamed.)

- **Moved onto it:**
  - Core: gravity, gear/body/water contact, air data, engines, wind, the
    instruments, flight-control ground and attitude terms, trim, the ground
    service check, and the weapons (bombs, decoys, missiles, line of sight).
  - Network: bots, spawns, air defences, and server bomb/missile ground checks.
  - Client: camera ground clearance and horizon, mouse aim, HUD heading and
    markers, effects, ejection, debris, the local gun, sound, gear animation.
- **Not moved, on purpose:** the valley's own terrain mesh, lakes, airfield
  geometry, minimap and screenshot fixtures still use north/east, because they
  only draw the valley. The render transform (NED to render axes) is untouched;
  phase 4 adds the planet's rotation there. The Earth's equivalents come in
  phases 4 and 5.
- **Checks:**
  - A comparison program flew all seven aircraft through ground runs, trimmed
    manoeuvres in wind and turbulence, a crash into the mountains, a
    flat-runway run, a lake landing, bomb drops, flares and line-of-sight
    tests. It hashed every state, instrument and impact bit. Old and new builds
    gave identical hashes.
  - One signed-zero difference (a flat-runway impact height of −0 against 0)
    was found this way and fixed.
  - The full CTest suite matched the baseline. `regression.provenance` needed
    the source fingerprints in `data/models/engineering-fallback.json`
    refreshed for the edited flight-model files; no values changed.

### Phase 2: round-Earth physics on a smooth planet
- Add `EarthWorld` with WGS84 geodesy (game frame ↔ Earth-centred ↔ latitude,
  longitude, altitude), gravity toward the local down, and atmosphere by
  geodetic altitude.
- Add per-position wind rotation, HUD heading and pitch, and the attitude
  indicator.
- Update weapons: line of sight against the real curved surface, missile
  guidance unchanged, ground impact.
- Update bots: altitude and terrain avoidance through `World`.
- Add the sun from latitude and longitude, plus a time of day per theatre.
- Test on a planet that is all ocean (sea level everywhere).
- Tests: geodesy round trip; near the anchor the Earth and flat results agree
  within tolerance over short flights; a long great-circle flight keeps altitude;
  a dropped bomb falls along the local down far from the anchor.

#### Phase 2 results (2026-10-14)

Done on `feature/whole-earth`: `ofs::EarthWorld` in
[earth.hpp](../core/include/ofs/earth.hpp), tested by
[earth_tests.cpp](../tests/earth_tests.cpp) (`earth.*` in CTest).

- **Frame:** WGS84. The game frame is Earth-centred coordinates rotated so
  that at the anchor (sea level) x is north, y east and z down. The Earth does
  not turn. Gravity is standard g along the local geodetic down.
- **Conversions:** Bowring's method, two passes, giving the sines and cosines
  of latitude and longitude directly.
  - Round trip over 2,000 points from −400 m to 40 km: within 3 nm and
    2×10⁻¹⁶ rad.
  - The last point asked about is cached per thread, because a step asks
    about the aircraft's own position many times.
  - A physics step costs 19 µs, against 8 µs in the valley over flat ground and
    20 µs over valley terrain (A320, this laptop). The first, untuned version
    cost 97 µs.
- **Surface:** smooth for now: solid ground at sea level everywhere, and no
  water, so aircraft can stand and roll on it. Ocean water and terrain come in
  phase 3.
- **Tests:**
  - At the anchor: down is +z and the local axes are the game axes.
  - 100 km north: the sea is 785 m below the anchor plane and the vertical has
    turned 0.90°.
  - Every aircraft flies 10 s near the anchor within 5 cm of its valley flight.
  - An A320 holding altitude and wings level for 600 km (30N 10E to 32.6N
    15.6E) stays within 1.5 m of its altitude and 80 m of a true great circle.
  - Bombs dropped at 10N, 60S and 89N land exactly below.
  - Every aircraft parked at 41S 174E rests at the same height as in the
    valley (to 0.1 mm), on its heading, undamaged.
  - The Earth's bulge hides 10 m-high objects 50 km apart and 10 km-high ones
    800 km apart, but not 300 km apart.
  - The instruments read the local heading, altitude, roll and climb.
- **Moved to later phases:**
  - The sun's position from latitude, longitude and time belongs with the
    client's sky, so it moves to phase 4.
  - Running a network game on the Earth moves to phase 6.
  - Missile and radar-track positions on the wire still need making relative
    (phase 6) before a game can range more than a few hundred kilometres.
- **Valley unchanged:** the bit-for-bit comparison program gives the same
  hashes as before phase 1.

### Phase 3: real terrain in core
- Load the pack in core (shared by the server and the client), and sample height
  and normal with deterministic noise detail.
- Ocean at sea level, lakes from Natural Earth, runway flattening.
- Add a pack hash to the join handshake.
- Tests: identical heights on Linux and Windows builds for a fixed set of points
  (tolerating mm-level `libm` differences, since the server is the authority);
  gear contact on a sloped real runway.

#### Phase 3 results (2026-10-15)

Done on `feature/whole-earth`. `ofs::EarthTerrain`
([earth_terrain.hpp](../core/include/ofs/earth_terrain.hpp)) reads the pack;
`EarthWorld` takes it as an optional third argument.

- **Inputs** (downloaded to `build/earth/dl/`, kept local):
  - Natural Earth 1:10m land, minor islands and lakes (public domain).
  - OurAirports runways and airports (public domain).
  - Zstandard 1.5.7's single-file decoder, vendored in `core/thirdparty/zstd`
    (BSD; credited in the release notices).
- **Water:** `build_earth_terrain.py water` paints a global 15 arcsec level
  map, taking about 30 s.
  - The sea is at 0.
  - Each of the 1,355 lakes, and the one inland sea Natural Earth leaves as a
    hole in the land (the Caspian), gets the median height of its shoreline as
    its level.
  - Results: Superior 184 m, Baikal 451 m, Geneva 380 m, Constance 394 m,
    Caspian −28 m, Dead Sea −414 m. The Qattara Depression and the Dutch
    polders stay dry below sea level.
  - Water mapped over ground more than 15 m above it counts as dry. A water
    bed is lowered to at least 2 m under its level.
- **Runways:** 14,323 open runways at airports, with both ends located and
  150–6,000 m long.
  - Each runway's end elevations are fitted to the terrain along it, with the
    slope capped at 2 %.
  - At run time it lies level across and straight along within 40 m of its
    edges, blending into the land over another 250 m and 120 m past each end.
- **Pack, version 2:** 807 MiB, built in about 5 minutes. Each tile is one of:
  - land: heights only;
  - shore: heights plus water levels, stored raw and zstd-compressed;
  - open sea: a flag only, read as bed −50 m and water 0.

  The header and index layout are documented in the script.
- **Ground at a point:** the pack's heights, bilinear, plus five octaves of
  3D value noise from 360 m down to 22 m wavelengths.
  - The noise runs about ±1.5 m in lowlands up to ±23 m above 2,200 m, and
    fades out near water and on runways.
  - The slope comes from two samples 2 m north and east. It is skipped more
    than 50 m above the ground, where nothing uses it.
  - Heights are taken as metres above the ellipsoid: the source's geoid is
    the ellipsoid here, so the sea is at 0 everywhere.
- **Tests** (`earth.places`, `seams`, `runway`, `taxi`, `ditch`, `determinism`,
  `speed`; skipped with exit 77 when the pack is absent, as in CI):
  - Known heights and waters come out right (Mont Blanc 4,620 m and Everest
    8,413 m, both averaged down by the 460 m source cells).
  - 444,528 one-metre steps over 4° of latitude, through tile edges and a cube
    face edge, change by at most 0.96 m.
  - Vienna's 3,497 m runway matches its profile and is level across, to the
    millimetre.
  - All seven aircraft park still and taxi on it without bouncing.
  - An aircraft ditching in the Atlantic goes into the water.
  - Two loads of the pack agree bit for bit at 4,000 points.
  - An A320 step over the Alps costs 35 µs.
- **Valley unchanged:** the bit-for-bit comparison still matches the
  pre-phase-1 build.

### Phase 4: drawing the planet
- Quadtree LOD terrain, background streaming, a curved horizon, the global
  ocean, land-cover materials, and haze tuned for long ranges.
- Performance budget: frame time on Low and High settings stays at today's
  valley level, and streaming stalls stay under one frame.
- Compare against the War Thunder reference screenshots, as for the valley terrain.

#### Phase 4 results (2026-10-15)

Done on `feature/whole-earth`. Single player only for now.

**To try it:**
- `build/release/client/ofs_client --earth` starts on Vienna's runway 11/29.
- `--earth-at <lat>,<lon>` starts on the runway nearest a place;
  `--earth-height <m>` with `--airborne` starts that high above the ground.
- `--earth-pack` and `--earth-cover` override the default files in
  `build/earth/`.

**What was built:**
- **Land cover:** `build_earth_terrain.py cover` turns Natural Earth II
  (public domain, 21,600 × 10,800, from land cover, without shading) into
  `earth-cover.ofsc`: 10,800 × 5,400 RGB, about 2 km a pixel, 28 MiB.
- **Render frame:** the render axes follow the local vertical at the render
  origin (`RenderBasis` in `coordinates.hpp`), set whenever the origin moves.
  - In the valley it is the identity, and every mapping is unchanged.
  - World-fixed shader patterns take the origin as distances along the
    surface, so clouds do not jump when the origin moves.
  - A frame constant (`u_planet`) tells shaders they are on the round Earth.
    The sky then puts the ground below the real, curved horizon instead of
    the level line.
- **Terrain:** a quadtree of 33 × 33-vertex chunks on the pack's cube
  ([earth_scenery.hpp](../client/src/earth_scenery.hpp),
  [renderer_earth.cpp](../client/src/renderer_earth.cpp)).
  - Down to level 5 (~100 km nodes) a chunk is the pack's own samples, 7 apart.
    Below that, down to level 13 (~13 m between vertices), it is the
    simulation's ground function, so the drawn ground is the ground the
    aircraft stands on.
  - Chunks are built on background threads, nearest-and-coarsest first by
    distance over size, and drawn coarser until all four children are ready.
    Skirts hide cracks.
  - Culling is by horizon and frustum. The far plane reaches the horizon plus
    360 km of mountains.
  - A chunk takes about 5 ms to build. A parked start settles at about 650
    chunks.
- **Shading** ([earth_terrain_fs.glsl](../client/shaders/earth_terrain_fs.glsl)):
  - Natural Earth's colour classifies the land: plants by greenness, ice pale
    and cool, the rest warm arid ground. Farmland and woods use the valley's
    own pasture, crop, grass and woodland textures, means far off and textures
    close to. Deserts take Natural Earth's sand tones.
  - Steep ground is rock, and ground over about 3,000 m is snow.
  - Sea and lakes are wherever the water level stands above the ground, per
    pixel, shaded as the valley's lakes.
  - The 8 nearest runways are painted per pixel: asphalt, edge lines and a
    dashed centre line.
- **Map:** the valley minimap and map are hidden on the Earth.

**Seen** (screenshots at 1280 × 720, clouds thinned):
- the A320 parked on Innsbruck's runway in the Inn valley, mountains on both
  sides;
- Vienna's 16/34 from 600 m;
- Lake Geneva's shore and the Jura;
- the Gulf coast desert at Dubai;
- the Earth's curve, Lake Geneva and the Alps from 24 km in the SR-71.

**Speed:** about 13 ms a frame against the valley's 20 ms on this laptop
(Intel Arc, owner's settings, vsync off).

**Still to do** (phase 4 follow-ups and phase 5):
- Terrain shadows and relief self-shadowing.
- Trees and towns (done: see Earth scenery below).
- The sun from latitude, longitude and time.
- A land-cover field pattern like the valley's (done: see Earth scenery below).
- Rock and snow tuned against War Thunder references (a first pass is below).
- Network play (phase 6).
- The minimap and map for the Earth.

#### Earth scenery (2026-10-15)

Done on `feature/whole-earth`: trees, towns, tall structures and airport
buildings, all placed procedurally, so every machine sees the same thing.
Nothing here is collided with; it is scenery, like the valley's trees and
villages.

- **Places:** `build_earth_terrain.py cover` now also writes the places the
  scenery is built around into `earth-cover.ofsc` (version 2, 28.4 MiB): the
  10,876 airports with a runway in the pack (OurAirports: position, size,
  ident and name) and 7,332 towns and cities with their populations (Natural
  Earth 1:10m populated places, public domain, a 0.6 MiB download). Version 1
  files still load, without places.
- **Patterns shared by the shader and the objects**
  ([earth_pattern.hpp](../client/src/earth_pattern.hpp), mirrored in
  [earth_terrain_fs.glsl](../client/shaders/earth_terrain_fs.glsl) and
  [earth_pattern.glsl](../client/shaders/earth_pattern.glsl)): fields, hedges,
  lanes, the clearings in woods, village streets and town districts are
  worked out from hashes of a point's position along the cube face, so the
  trees of a hedgerow stand on the hedge the shader paints.
  - Coordinates wrap every 1,048,576 m, which leaves a seam in the pattern
    every thousand kilometres or so.
  - Texture tiles are powers of two up to 16,384 m, and each chunk's texture
    coordinates start from a multiple of that, so textures run on unbroken
    from one chunk to the next (in phase 4 they jumped at chunk edges).
- **Ground** (the shader):
  - Farmland (plants in the cover, not woods) is the valley's patchwork:
    512 m fields of pasture, crops, stubble and plough with the valley's tints,
    hedgerows and tree belts on most boundaries, one field in twelve wooded,
    and lanes on a 2 km network. Lanes and hedges fade out from 3.5–5 km on
    Low and 6–8 km on High, the fields themselves from 9–15 km.
  - Steep farmland is wooded instead (over about 18° of slope), so the Alps'
    slopes are forest, not fields.
  - Woods have clearings and ragged edges from value noise.
  - Rock above about 40° of slope, grey and mottled; above the tree line
    (about 3,700 m at the equator falling to 1,200 m at 62°, so about 2,000 m
    in the Alps) alpine grass, then scree; snow from 600 m above the tree
    line, on all but the steepest faces.
  - Towns: streets, verges and yards in a grid of 24 m lots in blocks of four,
    each 768 m district of a town turned to its own angle; villages have one
    street and a lane across it. Beyond where buildings are drawn the lots
    show roof colours instead.
  - Airports: a parallel taxiway with links, an apron of concrete and a
    yellow centre line, on the side of the longest runway away from the
    airport's other runways. Every runway is painted over that.
- **Objects** ([earth_objects.hpp](../client/src/earth_objects.hpp)), built a
  tile at a time on a background thread, a tile being a node of the pack's
  cube at level 11 (about 1.6 km):
  - Trees: the valley's two species and meshes, instanced. Woods are dense,
    hedgerows and belts carry lines of trees, and a few stand alone. Conifers
    take over with height and toward the poles; none grow above the tree
    line, on rock, in water, on runways and airport ground, or in streets.
    Candidates per square kilometre follow the tree density setting.
  - Towns: Natural Earth's towns spread `350 m × (population / 1000)^0.42`
    (Innsbruck 2.9 km, Vienna 9 km) with a ragged edge; villages stand on
    about four in ten 2.4 km squares of farmland, away from towns and
    airports. Houses use the valley's house and roof (now shared, in
    [mesher.hpp](../client/src/mesher.hpp)) with clay or slate roofs; towns
    have blocks of flats, cities offices, and the largest glass towers in the
    middle; a sector of each town's outskirts is sheds and warehouses, with
    the odd works chimney. Each village has a church spire.
  - Airports: along the longest runway, a terminal with jet bridges and a
    control tower at medium and large airports, then arched hangars (one or
    two sheds at small strips).
  - Masts: one tile in thirty carries a banded lattice mast with stays and a
    lamp, on its highest point, away from airports.
  - Buildings are drawn to 1.8 times the scenery distance (5–16 km), trees to
    the scenery distance, both culled by tile; they cast sun shadows.
- **Speed** (this laptop on battery, Low preset, SU-57 at 500 m, bench
  numbers): Vienna 100 fps against 124 fps for the phase 4 build without
  scenery; Innsbruck 98. The valley gives 160–180 fps on the same settings.
  A tile takes 8–22 ms to build in the background (cities are the dearest).
- **Speed work (2026-10-16).** Measured on mains power, Low preset, vsync
  off, over Vienna airport (median of three bench runs; this laptop varies by
  ±10 fps from run to run): 122 → 128–137 fps at 500 m, 130 → 150–153 fps at
  1,500 m. The valley gives 160–180 on the same settings.
  - Where the time went: the frame is bound by the GPU, and on it by the
    ground's fragment shader (a plain-coloured ground gives about 200 fps).
    Draw calls were not the cost: 293 of the 374 are ground chunks.
  - A tile's buildings are one draw instead of up to twelve: each vertex
    carries its part's colour and look (`EarthBuildingVertex`, 40 bytes, the
    `earth_building` shader); the shadow pass draws only the leading range
    that casts shadows. They look as before.
  - The ground shader works out woodland noise, the canopy and rock only
    where there are woods or rock (same picture, read with explicit gradients
    in those branches).
  - Trees and buildings are drawn before the ground, and the ground's chunks
    nearest first, so hidden ground is not shaded.
  - The minimap passed over ten thousand airports a frame, about 1.2 ms of
    CPU; only those within its band of latitude are projected now.
  - Tried and dropped: fading the fields out sooner on Low (6–10 km instead
    of 9–15 km) made no measurable difference; dropping field detail entirely
    only gains 6–11 fps, so far fields were not the cost.
- **Compared** with the valley's `farmland` and `mountains` scenarios: the
  farmland reads alike; the Alps have forested slopes, grey rock and snow on
  the ridges, though they are paler and hazier than the valley's mountains.
  Expect tuning once the owner has flown it.
- **Known limits:** towns are grids, not real street plans; buildings stand
  on their lots without checking the slope beyond 3 m; airports are laid out
  procedurally, not from their real aprons and terminals.

### Phase 5: airports, theatres and the map screen
- Import OurAirports into a compact table, and generate runway and taxiway
  meshes from its runway ends and headings.
- Add the theatre list: Austria, the Alps and the Persian Gulf, each with an
  anchor and its spawn airports. Free flight only. Team bases and structures per
  theatre come later, with Earth team games.
- The map screen (`client/src/map.cpp`) shows a zoomable world map with latitude
  and longitude.

#### Phase 5 results (2026-10-15)

Done on `feature/whole-earth`; still single player until phase 6.

- **Theatres** ([theatres.hpp](../core/include/ofs/theatres.hpp)), each an
  anchor (the game frame's origin) and the airports a flight starts at, on the
  longest runway of the pack within 5 km of OurAirports' reference point,
  300 m in from its end:

  | Theatre | Anchor | Airports |
  |---|---|---|
  | `austria` Austria | 47.6N 13.8E | Vienna LOWW, Salzburg LOWS, Innsbruck LOWI, Graz LOWG, Linz LOWL, Klagenfurt LOWK |
  | `alps` Western Alps | 46.1N 7.4E | Geneva LSGG, Zurich LSZH, Sion LSGS, Milan Malpensa LIMC, Turin LIMF, Grenoble LFLS, Chambery LFLB |
  | `gulf` Persian Gulf | 26.2N 53.0E | Dubai OMDB, Abu Dhabi OMAA, Sharjah OMSJ, Doha OTHH, Bahrain OBBI, Dammam OEDF, Bandar Abbas OIKB, Bushehr OIBB |

  Every start runway is 2,000–4,860 m long and lies 15–374 km from its
  anchor (`earth.theatres`). The airport lists are in the source, so the
  server and the client agree without reading the cover file.
- **Client:** `ofs_client --theatre <id> [--earth-airport <ident>]` starts at
  that airport (the theatre's first by default). `--earth-at` still works as
  before, with the start as the anchor.
- **Launcher:** the flight mode page has a map choice (the valley, or Earth ·
  each theatre the installed build lists in `launcher-catalog.json`) and the
  airport to start at. An Earth flight passes `--theatre`, `--earth-airport`,
  `--earth-pack` and `--earth-cover`, the data being found in a source
  checkout's `build/earth/`, an `earth/` folder beside the game, or the
  user's data folder. A build without theatres offers only the valley, and an
  Earth flight is free flight only for now.
- **Map** ([earth_map.hpp](../client/src/earth_map.hpp)): the minimap and the
  full map (N) show the land about the aircraft, north up, painted on a
  background thread from the land cover (deepened a little), the sea and
  lakes, and relief shading from the pack level nearest the map's scale.
  - Lines of latitude and longitude, labelled on the full map in degrees and
    minutes; the airports of the cover file, the larger ones only when zoomed
    out; the theatre's airports picked out by name.
  - The full map zooms with + and − or the mouse wheel, from 25 km across to
    12,800 km. The minimap scales with speed as the valley's does.
  - The minimap's picture covers more than it shows and is painted again as
    the aircraft nears its edge.
- **Sun:** with real time on, the sun stands where it really does over the
  aircraft, from the UTC time, latitude and longitude (NOAA's equation of time
  and declination). Checked against noon at the solstices in Vienna (65.2°)
  and Dubai (41.3°) and sunrise at the equinox in Geneva (`scenery.sun`).
  The valley keeps its local-clock sun at 45°N.
- **Tests:** `earth.theatres`; `scenery.sun`, `scenery.patterns`,
  `scenery.determinism` (two builds of the same tiles in Vienna, Innsbruck,
  farmland and Dubai agree byte for byte), `scenery.airports` (no tree on
  Vienna's runway strip, no building on the runway) and `scenery.map` (Lake
  Geneva comes out blue). Those that need the pack and cover are skipped
  without them.

### Phase 6: network, launcher and public host
- Add a map ID and theatre to `Hello`/`Welcome`, plus the pack hash, and bump the
  protocol version.
- Missile and radar-track positions become relative to the reference point,
  like aircraft positions.
- Keep older launchers working. They are only offered valley games.
- Launcher: a map choice, a theatre picker, and the Earth pack as a download
  with hash, size and URL checked, like the aircraft content pack.
- Public host: map choice per game, and the pack installed on the mini server.
- Backup download copy on the mini server: a static file server added under
  `hosting/`, with the owner forwarding one TCP port, and a launcher fallback
  from GitHub to the server with the same hash checks.
  Keep `launcher/public.py` and `hosting/public_host.py` in step.

#### Phase 6 results (2026-10-15)

Done on `feature/whole-earth`, except what has to wait for a release (below).
Free flight only on the Earth: no bots or team games there yet.

- **Protocol 25.** `Hello` and `Welcome` carry the map: valley or Earth, the
  theatre and the terrain pack's content hash (`EarthTerrain::contentHash`,
  FNV-1a over the pack's header, index and runways, which hold every tile's
  compressed size). `Hello` also names the airport the pilot would start at.
  A server turns away a client on another map, theatre or pack with the reason
  (`This game is on the Earth map, in alps. Choose it to join.`), at most 64
  characters as a `Reject` allows. Older games cannot join version 25 servers,
  as with any protocol change; their launchers are handled below.
- **Relative positions.** Radar and missile messages carry a reference point,
  the whole kilometre below the viewer in each axis, and their positions
  relative to it: missiles to 1/8 m within 260 km, radar tracks to 1/2 m within
  1,000 km. Anything beyond is left out (a missile) or kept to the edge (where a
  removed one went), so nothing far away can overflow the fields. Checked 2,600
  km from the origin (`earthnet.relative`).
- **Server:** `ofs_server --map earth --theatre <id> --earth-pack <path>` plays a
  theatre. Each pilot starts at the airport they asked for, or the theatre's
  airports in turn, on its longest runway or above it, later arrivals spaced
  along it, and respawns there.
- **Client:** `ofs_client --server ... --theatre <id> [--earth-airport <ident>]`;
  a bomber or a game alone on the Earth gets a private Earth server, as in the
  valley. The F5 bot fight stays in the valley.
- **LAN:** a game on the Earth adds `earth:<theatre>` after the version in its
  discovery answer. A valley game's answer is byte for byte as before. Older
  launchers refuse an answer with anything after the version, so they list
  valley games only.
- **Launcher:** lists Earth games with their theatre, joinable only by an
  installation that has that theatre; joining one flies the game's map,
  whatever was chosen for flying alone. Hosting (LAN or public) uses the map
  chosen on the flight mode page; the server is given this computer's pack.
- **Public host** ([PUBLIC_HOSTING.md](PUBLIC_HOSTING.md)): `list` with `maps`
  shows Earth games and their `map`; without it, only valley games. `create`
  takes `map` and `theatre`, refuses bots and teams on the Earth, a server
  older than `EARTH_VERSION` (set to 0.7.0, to be matched to the version that
  ships) and a host without the pack. The build service fetches the server's
  own copy of the pack into `/var/lib/ofs-public/earth/`.
- **Downloads:** `scripts/publish_earth_data.py describe --tag earth-data-v2`
  writes `data/earth/earth-data.json` (each file's GitHub pre-release URL,
  SHA-256 and size), and `upload` publishes the pre-release with `gh`. The
  release workflow passes the descriptor into the build, the catalogue lists
  the files as `earth_data`, and the launcher's flight mode page offers them
  for download into the user's data folder, each checked against its size and
  SHA-256; data of another size is fetched again. Credits are in
  `licenses/data/EARTH.md`, shipped with the game.
- **Tests:** `earthnet.handshake`, `relative`, `lobby` and `game` (a real
  server in the western Alps: a pilot asking for Zurich starts on its runway's
  centre line; a valley client is turned away with the reason);
  `lan.public_host` gains Earth games and the listing for older launchers; the
  launcher tests cover lobbies with a map, hosting and downloads.
- **Valley unchanged:** a trajectory-hash program flew all seven aircraft
  through scripted manoeuvres in wind and turbulence, a takeoff run and a
  crash into the western mountains, hashing every state, instrument and impact
  value; the core built from this branch and from b880856 (before this work)
  gave identical hashes.

**Waiting for "release":**
1. Build every region (`build_earth_terrain.py detail` once the world's
   GLO-90 is downloaded), then `python3 scripts/publish_earth_data.py describe
   --tag earth-data-v2` and `upload` (the 835 MiB base files and roughly 7 GB of
   regions, as one pre-release on `KindaBad/OpenFlightSim-public`), and commit
   `data/earth/earth-data.json`. Check the mini server has room for the regions
   it fetches.
2. The version bump, to 0.7.0 (the owner's choice, 2026-10-15), which is
   also `EARTH_VERSION` in `hosting/public_host.py`. It must have three
   numbers: launchers before 0.6.9 read only the index's `releases` list,
   which holds versions of three numbers; versions of four numbers such as
   0.6.9.2000 and later 0.7.0.1 are listed under `extended_releases`
   (`launcher/manifest.py`). Publishing 0.7.0 rewrites the live index that
   way, so launchers that now fail on "Invalid semantic version: 0.6.9.1"
   update to 0.7.0, and from it to 0.7.0.1 and on.
3. Merge to `main`, deliver the public source, and on the mini server copy
   `hosting/` and run `sudo sh ./install.sh` (it ends the games running), then
   `systemctl start ofs-public-build` to build the server and fetch the pack.

### Phase 7 (optional): more detail
- Stream the GLO-90 90 m heights by region on demand. The server holds them all,
  about 25 GB. It is hosted as region packs on GitHub (for example 10° squares,
  each under 2 GiB, spread across pre-releases), with the mini server as the
  backup copy (see decision 1).
- Add towns from built-up land cover, then roads and rivers.

#### Phase 7 results (2026-10-16)

Done on `feature/whole-earth` for the three theatres; the rest of the world is
downloading. Owner's decisions: go ahead with the download (the full source is
66 GiB, not the ~25 GB first estimated, which was the packed size; players may
download up to 100 GB), theatres first, and region packs fetched by the
launcher per theatre.

- **Source:** Copernicus DEM GLO-90 from the AWS Open Data copy (no account),
  1° Cloud Optimized GeoTIFFs of float32 metres, points 3 arcsec apart (wider in
  longitude above 50°), sea without tiles. `build_earth_terrain.py fetch-glo90
  [--theatres ...]` downloads them, resumably, into `build/earth/dl/glo90/`.
  - Licence (checked 2026-10-16, the ESA licence PDF linked from the Copernicus
    Data Space page): free reproduction, distribution and adaptation, with an
    attribution notice and a no-liability sentence; both are in
    `licenses/data/EARTH.md` and the pre-release notes.
  - It is a surface model: forests and buildings are in the heights, averaged
    over 90 m cells. Sharp peaks are blunted: GLO-90 has the Matterhorn at
    4,316 m (true 4,478 m), Mont Blanc 4,792 m (4,806 m).
- **Region packs:** `build_earth_terrain.py detail --theatres ...` (or
  `--regions`, or all) writes `build/earth/detail/earth-detail-<region>.ofsd`,
  one for each 10° square named by its south-west corner (`N40E010`).
  - Each holds cube level 7, two below the base pack's leaves: the 16 tiles
    under every base leaf that is not open sea, 225 × 225 samples ~116 m apart
    at a face centre, encoded as the base pack's tiles (1 m steps). The header
    and index are documented in the script.
  - Heights are GLO-90 bilinear across its tiles (continuous across tile and
    latitude-band edges); where GLO-90 has no tile, the ETOPO grid. Water
    mapped over a sample lowers it below the water as in the base pack; the
    water itself stays the base pack's.
  - The ten theatre regions: 300 MB, 10–40 MB each, about 15 s each to build
    on 6 cores. All the world's land should come to roughly 7 GB.
- **Core** ([earth_terrain.hpp](../core/include/ofs/earth_terrain.hpp)):
  `addDetail` loads a region; `loadEarthDetail` (in `theatres.hpp`) loads a
  theatre's regions from `detail/` beside the pack, or all of them in free
  flight alone.
  - Where a region covers the point, the ground is its heights with only the
    two finest noise octaves (45 and 22 m) instead of all five, blended back to
    the base over 48 samples (~5.6 km) toward any neighbouring tile without
    detail, found across face edges too. Runways are laid level over it as
    before. With no region loaded the ground is computed exactly as before.
  - The content hash covers the regions loaded (the base hash, then each
    region's header and index hash in order of name), so a player without a
    theatre's regions is turned away by a server that has them, with "Your
    Earth map data is not this game's. Download it again."
  - The client's chunks above the pack's levels already sample the ground, so
    they show the detail; distant chunks still come from the base pack's
    coarse tiles.
- **Delivery:**
  - `publish_earth_data.py describe` lists the regions under `detail` in
    `earth-data.json`; `upload` adds them to the pre-release, 20 files at a
    time, with the Copernicus notices in its notes.
  - The release workflow checks each record and passes the list to CMake
    (`OFS_EARTH_DETAIL_JSON`); the catalogue lists them as `earth_detail`, and
    each theatre's `regions`.
  - The launcher's Flight mode page shows the chosen theatre's 90 m detail and
    downloads what is missing (the base files if needed, then the theatre's
    regions) into `detail/` beside the pack, each checked by size and SHA-256.
  - The public host's build service fetches every region into
    `/var/lib/ofs-public/earth/detail/`.
- **Tests:** `earth.detail` (skipped without the Alps' regions): the regions
  and hash, Mont Blanc 4,784 m and the Matterhorn 4,279 m against 4,649 m and
  4,000 m on the base, Lake Geneva wet, one-metre walks across region edges and
  out of the detail (steepest 1.47 m), Zurich's runway level to the millimetre,
  two loads bit for bit, an A320 step 43.5 µs (35.8 µs on the base). Launcher:
  a theatre's regions are fetched beside the pack, once, and again when their
  size changes. By hand: a client and server in the Alps both load the same
  four regions and the client joins; a client without them is turned away.
- **Valley unchanged:** the trajectory-hash program was rewritten (kept
  locally as `build/earth/valley_hash.cpp`): all seven aircraft through
  scripted manoeuvres in wind and turbulence, a takeoff run and a low run west
  into the mountains, hashing every state, instrument, force and impact value.
  Cores built from 7bad300 and from this work give identical hashes.
- **Look:** around Innsbruck the hills show ridges and gullies where the base
  was smooth. From high up the difference is small, since the distance is
  drawn from the base pack.

## Decisions for the owner

Every choice has to cost nothing. The owner has no budget, only this laptop and
the mini server, so paid hosting such as Cloudflare R2 is ruled out, even its
free tier, because it needs a card.

1. **Where the Earth pack is hosted: decided 2026-10-14.** It goes on GitHub, as
   pre-releases on `KindaBad/OpenFlightSim-public`, the same way as the aircraft
   content pack. Its URL, SHA-256 and size are given to CI and the launcher the
   same way too. GitHub's release docs (checked 2026-10-14) say each file must
   be under 2 GiB, and that there is no limit on a release's total size or on
   bandwidth. So the base pack goes in numbered parts if needed, and the 90 m
   detail goes as region packs under 2 GiB each.

   **The owner's mini server is the backup copy.** It has 800 Mbit/s upload, a
   static IP and about 98.9% uptime, and the owner can forward a TCP port for it.
   It serves the same files as static HTTPS downloads. The launcher tries GitHub
   first and falls back to the server. Either way it checks every file against
   the SHA-256 published by CI, so a tampered or partial download from either
   source is rejected. Server hardening: a read-only static file server (e.g.
   Caddy) running as its own user, serving one directory, with no uploads, no
   directory listing and no scripts, and a download rate cap so live games on
   UDP 27020 keep their bandwidth. If GitHub ever objects to the volume, the
   server becomes the main source with no change to the launcher's checks.
2. **Base resolution: decided 2026-10-14.** ETOPO 2022 at 15 arcsec (~460 m,
   "~500 m") on land, plus noise detail. That's sharper than the 30 arcsec
   (~1 km) first proposed, at about four times the size. The sea floor is kept
   at 60 arcsec, because only the coastline and sea level are visible. Expected
   pack: roughly 0.7–1.2 GB, to be measured in phase 0. It is split into parts
   if it passes GitHub's 2 GB per file.
3. **Theatres: decided 2026-10-14.** The first theatres are Austria, the Alps
   (western Alps) and the Persian Gulf. The Earth map ships with free flight
   only. Team games on it come in a later update.
4. **Earth rotation.** The plan ignores it (no Coriolis). Real orbital or
   ballistic accuracy would need it, but nothing in the game does.

## Risks

- **Phase 1 breadth.** About 600 position uses. A missed "z is altitude" is a
  bug that only appears far from the anchor. Phase 2's long-flight tests are
  there to catch those.
- **Prediction across platforms.** Windows and Linux `libm` give slightly
  different trigonometry. The server is authoritative, so tiny prediction
  differences are corrected as they are today. Each machine must still load the
  same pack.
- **Download size.** It must stay optional: players who only fly the valley
  should not need the Earth pack.
- **Rendering cost.** Showing a 565 km horizon at 60 fps on the owner's Low
  setting is the hardest performance target in the plan.

## After release: fights on the Earth (0.7.0.1)

Players asked to fight on the Earth map. From 0.7.0.1 (protocol 26) Local
Dogfight, bots in hosted games and team games run in every theatre. A team
game puts Red at the theatre's first airport and Blue at the one farthest from
it (`teamAirport`, `Start::home`); each side is serviced within 4.5 km of its
start. The valley's structures are not placed on the Earth, so a team game
there is scored by kills. A bot with no pilot to chase starts 1 km above its
side's airport (or over a start in turn) and patrols at least 900 m over the
ground. The launcher and the public host refuse bots and teams on the Earth
for versions before 0.7.0.1.

The same release fixed the hard line across the view at eye level reported at
Innsbruck: the renderer took the eye's altitude from the game frame's z,
which away from the theatre's anchor is not the vertical, so the aerial
perspective clipped every downward ray at a few metres.

