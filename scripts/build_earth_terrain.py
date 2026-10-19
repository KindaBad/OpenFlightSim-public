#!/usr/bin/env python3
"""Phase 0 prototype: turn NOAA ETOPO 2022 15 arcsec heights into an Earth pack.

See docs/WHOLE_EARTH_PLAN.md. Two steps, both writing under build/earth/:

  ingest  the 288 source GeoTIFFs -> one global int16 grid in half metres
          (86400 x 43200, row 0 at 90N, column 0 at 180W), memory-mapped
  build   the grid -> a cube-sphere tile pack, with every quadtree level, each
          tile delta-coded and zstd-compressed; prints the pack's size

The cube is equal-angle: on face f a point (u, v) in [-1, 1] lies along
normal + tan(u pi/4) right + tan(v pi/4) up. That direction is taken as the
WGS84 surface normal, so its geodetic latitude is atan2(z, hypot(x, y)).
Needs numpy, tifffile, imagecodecs and zstandard (a venv in build/earth/).
"""
import argparse
import math
import multiprocessing
import struct
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'build' / 'earth'
PER_DEGREE = 240                      # 15 arcsec
COLS, ROWS = 360 * PER_DEGREE, 180 * PER_DEGREE
GRID = WORK / 'etopo15s.i16'

TILE = 224                            # intervals per tile edge; 225 samples, edges shared
ROOTS = 3                             # root tiles per face edge
LEVELS = 6                            # 3, 6, ... 96 tiles per face edge
FACE_INTERVALS = ROOTS * TILE << (LEVELS - 1)    # 21504: ~465 m at a face centre
SEA_BELOW = -10.0                     # a tile wholly below this is open sea
SEA_STEP = 4                          # sea tiles keep every fourth sample (~1.9 km)

# Face normal, right and up, as (x toward 0N 0E, y toward 0N 90E, z toward 90N).
FACES = (
    ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
    ((0, 1, 0), (-1, 0, 0), (0, 0, 1)),
    ((-1, 0, 0), (0, -1, 0), (0, 0, 1)),
    ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ((0, 0, 1), (0, 1, 0), (-1, 0, 0)),
    ((0, 0, -1), (0, 1, 0), (1, 0, 0)),
)


def ingest(source):
    import tifffile
    files = sorted(Path(source).glob('ETOPO_2022_v1_15s_*_surface.tif'))
    if len(files) != 288:
        sys.exit(f'expected 288 tiles in {source}, found {len(files)}')
    WORK.mkdir(parents=True, exist_ok=True)
    grid = np.lib.format.open_memmap(GRID, mode='w+', dtype=np.int16, shape=(ROWS, COLS))
    lowest, highest = 1e9, -1e9
    for i, path in enumerate(files):
        with tifffile.TiffFile(path) as tif:
            page = tif.pages[0]
            scale = page.tags['ModelPixelScaleTag'].value
            tie = page.tags['ModelTiepointTag'].value
            if abs(scale[0] - 1 / PER_DEGREE) > 1e-12 or page.shape != (3600, 3600):
                sys.exit(f'{path.name}: unexpected grid {page.shape} {scale}')
            heights = page.asarray()
        west, north = tie[3], tie[4]
        row, col = round((90 - north) * PER_DEGREE), round((west + 180) * PER_DEGREE)
        heights = np.where(heights < -20000, 0, heights)
        lowest, highest = min(lowest, float(heights.min())), max(highest, float(heights.max()))
        grid[row:row + 3600, col:col + 3600] = np.clip(np.rint(heights * 2), -32767, 32767)
        print(f'\r{i + 1}/288 {path.name}', end='', flush=True)
    grid.flush()
    print(f'\nheights {lowest:.1f} .. {highest:.1f} m -> {GRID}')


def directions(face, level, x, y, samples, step):
    """Unit directions of a tile's samples, shape (samples, samples, 3)."""
    normal, right, up = (np.array(a, float) for a in FACES[face])
    span = FACE_INTERVALS >> (LEVELS - 1 - level)     # this level's intervals per face edge
    per_tile = TILE                                    # every level's tile is TILE intervals
    index = np.arange(samples) * step
    u = -1 + 2 * (x * per_tile + index) / span
    v = -1 + 2 * (y * per_tile + index) / span
    tu, tv = np.tan(u * math.pi / 4), np.tan(v * math.pi / 4)
    d = normal + tu[None, :, None] * right + tv[:, None, None] * up
    return d / np.linalg.norm(d, axis=2, keepdims=True)


def sample(grid, d):
    """Bilinear height in half metres from the global grid at unit directions."""
    lat = np.degrees(np.arctan2(d[..., 2], np.hypot(d[..., 0], d[..., 1])))
    lon = np.degrees(np.arctan2(d[..., 1], d[..., 0]))
    r = np.clip((90 - lat) * PER_DEGREE - .5, 0, ROWS - 1.000001)
    c = (lon + 180) * PER_DEGREE - .5
    r0, c0 = np.floor(r).astype(np.int64), np.floor(c).astype(np.int64)
    fr, fc = r - r0, c - c0
    c0 %= COLS
    c1 = (c0 + 1) % COLS
    rows = np.concatenate([r0.ravel(), r0.ravel(), r0.ravel() + 1, r0.ravel() + 1])
    cols = np.concatenate([c0.ravel(), c1.ravel(), c0.ravel(), c1.ravel()])
    values = grid[rows, cols].astype(np.float64).reshape(4, *r.shape)
    top = values[0] * (1 - fc) + values[1] * fc
    bottom = values[2] * (1 - fc) + values[3] * fc
    return top * (1 - fr) + bottom * fr


def encode(heights):
    """2D Lorenzo prediction, zigzagged, as low bytes then high bytes."""
    h = heights.astype(np.int32)
    p = np.zeros_like(h)
    p[1:, 1:] = h[1:, :-1] + h[:-1, 1:] - h[:-1, :-1]
    p[0, 1:] = h[0, :-1]
    p[1:, 0] = h[:-1, 0]
    e = h - p
    z = ((e << 1) ^ (e >> 31)).astype(np.uint16)
    return (z & 255).astype(np.uint8).tobytes() + (z >> 8).astype(np.uint8).tobytes()


def decode(data, side):
    planes = np.frombuffer(data, np.uint8).reshape(2, side, side)
    z = planes[0].astype(np.int32) | planes[1].astype(np.int32) << 8
    e = (z >> 1) ^ -(z & 1)
    # The Lorenzo predictor's inverse is a 2D prefix sum.
    return np.cumsum(np.cumsum(e, axis=0), axis=1).astype(np.int16)


# ---- Water ----------------------------------------------------------------
# One global int16 grid like the heights: the water level in half metres, or
# DRY. The sea is at 0. Lakes come from Natural Earth's lakes; inland seas that
# Natural Earth leaves as holes in the land (the Caspian) are lakes too. A
# lake's level is the median height of its shoreline.
WATER = WORK / 'water15s.i16'
DRY = -32768


def rings(shape):
    """The shape's rings as arrays of (longitude, latitude), and whether each is a hole."""
    out = []
    for start, end in zip(shape.parts, list(shape.parts[1:]) + [len(shape.points)]):
        ring = np.asarray(shape.points[start:end], float)
        if len(ring) < 3:
            continue
        x, y = ring[:, 0], ring[:, 1]
        area = .5 * np.sum(x * np.roll(y, -1) - np.roll(x, -1) * y)
        out.append((ring, area > 0))  # shapefile outer rings run clockwise
    return out


def spans(ring_list):
    """Even-odd fill of the rings on the 15 arcsec grid: arrays of (row, first column, end column)."""
    rows, xs = [], []
    for ring in ring_list:
        x = (ring[:, 0] + 180) * PER_DEGREE
        y = (90 - ring[:, 1]) * PER_DEGREE
        x0, y0, x1, y1 = x, y, np.roll(x, -1), np.roll(y, -1)
        low, high = np.minimum(y0, y1), np.maximum(y0, y1)
        first = np.ceil(low - .5).astype(np.int64)
        last = np.ceil(high - .5).astype(np.int64)  # exclusive
        count = np.maximum(last - first, 0)
        if not count.sum():
            continue
        edge = np.repeat(np.arange(len(x0)), count)
        offset = np.arange(count.sum()) - np.repeat(np.cumsum(count) - count, count)
        r = first[edge] + offset
        t = (r + .5 - y0[edge]) / (y1[edge] - y0[edge])
        rows.append(r)
        xs.append(x0[edge] + t * (x1[edge] - x0[edge]))
    if not rows:
        return np.zeros((0, 3), np.int64)
    rows, xs = np.concatenate(rows), np.concatenate(xs)
    order = np.lexsort((xs, rows))
    rows, xs = rows[order], xs[order]
    # Pair the crossings in each row; a row with an odd count is a degenerate touch, dropped.
    starts = np.flatnonzero(np.r_[True, rows[1:] != rows[:-1]])
    counts = np.diff(np.r_[starts, len(rows)])
    keep = np.repeat(counts % 2 == 0, counts)
    rows, xs = rows[keep], xs[keep]
    a, b = xs[0::2], xs[1::2]
    c0 = np.clip(np.ceil(a - .5), 0, COLS).astype(np.int64)
    c1 = np.clip(np.ceil(b - .5), 0, COLS).astype(np.int64)
    r = rows[0::2]
    ok = (c1 > c0) & (r >= 0) & (r < ROWS)
    return np.stack([r[ok], c0[ok], c1[ok]], axis=1)


def paint(grid, span_array, value):
    for r, c0, c1 in span_array:
        grid[r, c0:c1] = value


def shoreline_level(heights, ring):
    """Median terrain height in half metres at a ring's vertices."""
    r = np.clip(((90 - ring[:, 1]) * PER_DEGREE).astype(np.int64), 0, ROWS - 1)
    c = np.clip(((ring[:, 0] + 180) * PER_DEGREE).astype(np.int64), 0, COLS - 1)
    return int(np.median(heights[r, c]))


def water(source):
    import shapefile
    heights = np.load(GRID, mmap_mode='r')
    grid = np.lib.format.open_memmap(WATER, mode='w+', dtype=np.int16, shape=(ROWS, COLS))
    grid[:] = 0  # the sea, until land is drawn over it
    start = time.time()
    land = shapefile.Reader(str(Path(source) / 'ne_10m_land' / 'ne_10m_land'))
    holes = []
    for shape in land.shapes():
        parts = rings(shape)
        paint(grid, spans([r for r, _ in parts]), DRY)
        holes += [r for r, hole in parts if hole]
    islands = shapefile.Reader(str(Path(source) / 'ne_10m_minor_islands' / 'ne_10m_minor_islands'))
    for shape in islands.shapes():
        paint(grid, spans([r for r, _ in rings(shape)]), DRY)
    print(f'land drawn in {time.time() - start:.0f} s; {len(holes)} inland seas')
    bodies = 0
    for ring in holes:
        paint(grid, spans([ring]), shoreline_level(heights, ring))
        bodies += 1
    lakes = shapefile.Reader(str(Path(source) / 'ne_10m_lakes' / 'ne_10m_lakes'))
    for shape, record in zip(lakes.shapes(), lakes.records()):
        parts = rings(shape)
        outer = [r for r, hole in parts if not hole]
        if not outer:
            continue
        level = shoreline_level(heights, np.concatenate(outer))
        paint(grid, spans([r for r, _ in parts]), level)
        bodies += 1
        if record['name'] in ('Lake Geneva', 'Lake Constance', 'Lake Superior', 'Lake Baikal'):
            print(f'  {record["name"]}: {level / 2:.0f} m')
    grid.flush()
    print(f'{bodies} lakes and inland seas in {time.time() - start:.0f} s -> {WATER}')


# ---- Runways --------------------------------------------------------------
# OurAirports runways with both ends located, at open airports. Each is laid
# level across and straight along between end elevations fitted to the
# terrain under it, so it sits in the land rather than above or below it.
RUNWAY = struct.Struct('<ddddfff')  # latitude, longitude of each end (radians), width m, end altitudes m


def sample_degrees(heights, lat, lon):
    lat, lon = np.radians(lat), np.radians(lon)
    d = np.stack([np.cos(lat) * np.cos(lon), np.cos(lat) * np.sin(lon), np.sin(lat)], axis=-1)
    return sample(heights, d) / 2


def runways(source):
    import csv
    heights = np.load(GRID, mmap_mode='r')
    airports = {row['id']: row for row in csv.DictReader(open(Path(source) / 'airports.csv', encoding='utf-8'))}
    out = []
    for row in csv.DictReader(open(Path(source) / 'runways.csv', encoding='utf-8')):
        airport = airports.get(row['airport_ref'])
        if row['closed'] != '0' or not airport or airport['type'] not in ('large_airport', 'medium_airport', 'small_airport'):
            continue
        try:
            ends = [float(row[k]) for k in ('le_latitude_deg', 'le_longitude_deg', 'he_latitude_deg', 'he_longitude_deg')]
        except ValueError:
            continue
        width = float(row['width_ft'] or 100) * .3048
        width = min(max(width, 15), 90)
        t = np.linspace(0, 1, 11)
        lat = ends[0] + (ends[2] - ends[0]) * t
        lon = ends[1] + (ends[3] - ends[1]) * t
        ground = sample_degrees(heights, lat, lon)
        slope, base = np.polyfit(t, ground, 1)
        # Keep the slope within what a runway may have: 2% of its length.
        length = math.dist((ends[0] * 111320, ends[1] * 111320 * math.cos(math.radians(ends[0]))),
                           (ends[2] * 111320, ends[3] * 111320 * math.cos(math.radians(ends[0]))))
        if not 150 < length < 6000:
            continue
        slope = max(-.02 * length, min(.02 * length, slope))
        middle = float(np.mean(ground))
        out.append(RUNWAY.pack(*[math.radians(v) for v in ends], width, middle - slope / 2, middle + slope / 2))
    print(f'{len(out)} runways')
    return out


# ---- Land cover colours and places ---------------------------------------
# Natural Earth II (public domain): the colour of the land from its cover,
# without shading, 21600 x 10800 from 180W and 90N. Halved to about 2 km a
# pixel, it is a file of its own that only the game client reads, with the
# places the client builds its scenery around:
#   magic 'OFSC', version 2, 0, width, height, the compressed sizes of the
#   colours and of the places (uint64 each), then the zstd-compressed RGB rows
#   and the zstd-compressed places.
# The places are the airports that have a runway in the pack (OurAirports,
# public domain) and the towns and cities of Natural Earth's populated places
# (public domain):
#   airport count, town count (uint32 each); per airport latitude and
#   longitude (radians, float64), kind (1 small, 2 medium, 3 large), ident
#   (8 bytes) and name (40 bytes, UTF-8, zero-padded); per town latitude and
#   longitude (radians, float64) and population (float32).
# Version 1 files (colours only, the blob running to the end) are still read.
COVER = WORK / 'earth-cover.ofsc'
COVER_HEADER = struct.Struct('<4sHHIIQQ')
PLACE_AIRPORT = struct.Struct('<ddB8s40s')
PLACE_TOWN = struct.Struct('<ddf')
AIRPORT_KINDS = {'small_airport': 1, 'medium_airport': 2, 'large_airport': 3}


def places(airports_dir, towns_dir):
    import csv
    import shapefile
    airports = {row['id']: row for row in csv.DictReader(open(Path(airports_dir) / 'airports.csv', encoding='utf-8'))}
    # The airports with a runway the pack keeps (see runways()).
    kept = set()
    for row in csv.DictReader(open(Path(airports_dir) / 'runways.csv', encoding='utf-8')):
        airport = airports.get(row['airport_ref'])
        if row['closed'] != '0' or not airport or airport['type'] not in AIRPORT_KINDS:
            continue
        try:
            ends = [float(row[k]) for k in ('le_latitude_deg', 'le_longitude_deg', 'he_latitude_deg', 'he_longitude_deg')]
        except ValueError:
            continue
        length = math.dist((ends[0] * 111320, ends[1] * 111320 * math.cos(math.radians(ends[0]))),
                           (ends[2] * 111320, ends[3] * 111320 * math.cos(math.radians(ends[0]))))
        if 150 < length < 6000:
            kept.add(row['airport_ref'])
    out = []
    for ref in sorted(kept, key=int):
        a = airports[ref]
        name = a['name'].encode('utf-8')[:40]
        while True:  # never cut a character in two
            try:
                name.decode('utf-8')
                break
            except UnicodeDecodeError:
                name = name[:-1]
        out.append(PLACE_AIRPORT.pack(math.radians(float(a['latitude_deg'])), math.radians(float(a['longitude_deg'])),
                                      AIRPORT_KINDS[a['type']], a['ident'].encode('ascii', 'replace')[:8], name))
    towns = []
    reader = shapefile.Reader(str(Path(towns_dir) / 'ne_10m_populated_places_simple'))
    for rec in reader.records():
        population = max(float(rec['pop_max'] or 0), float(rec['pop_min'] or 0))
        if population <= 0:
            continue
        towns.append(PLACE_TOWN.pack(math.radians(rec['latitude']), math.radians(rec['longitude']), population))
    print(f'{len(out)} airports, {len(towns)} towns')
    return struct.pack('<II', len(out), len(towns)) + b''.join(out) + b''.join(towns)


def cover(source, out, airports_dir, towns_dir):
    import tifffile
    import zstandard
    image = tifffile.imread(source)
    if image.shape != (10800, 21600, 3):
        sys.exit(f'{source}: expected 21600 x 10800 RGB, found {image.shape}')
    half = image.reshape(5400, 2, 10800, 2, 3).astype(np.uint16).sum(axis=(1, 3))
    rgb = ((half + 2) // 4).astype(np.uint8)
    compressor = zstandard.ZstdCompressor(level=19, threads=-1)
    blob = compressor.compress(rgb.tobytes())
    extra = compressor.compress(places(airports_dir, towns_dir))
    with open(out, 'wb') as fh:
        fh.write(COVER_HEADER.pack(b'OFSC', 2, 0, 10800, 5400, len(blob), len(extra)))
        fh.write(blob)
        fh.write(extra)
    print(f'{out}: 10800 x 5400, {Path(out).stat().st_size / 2**20:.1f} MiB')


# ---- The pack -------------------------------------------------------------
# Header: magic, version 2, tile, roots, levels, tile count, metres per height
# step, runway count, then byte offsets of the tile index, the runways and the
# tile data. Each index entry: face, level, x, y, kind, then the offset and
# size of the heights and of the water levels within the tile data.
HEADER = struct.Struct('<4sHHHHIfIQQQ')
ENTRY = struct.Struct('<BBHHBQIQI')
LAND, SEA, SHORE = 0, 1, 2  # kinds: heights only; nothing (open sea); heights and water levels
SHALLOWEST = 2.0  # metres: water stands at least this deep over its bed
HIGHEST_WET = 15.0  # metres: water mapped over ground higher than this above it is taken as dry

_grid = None
_water = None


def nearest(grid, d):
    lat = np.degrees(np.arctan2(d[..., 2], np.hypot(d[..., 0], d[..., 1])))
    lon = np.degrees(np.arctan2(d[..., 1], d[..., 0]))
    r = np.clip(((90 - lat) * PER_DEGREE).astype(np.int64), 0, ROWS - 1)
    c = (((lon + 180) * PER_DEGREE).astype(np.int64)) % COLS
    return grid[r.ravel(), c.ravel()].reshape(r.shape)


def build_tile(job):
    global _grid, _water
    import zstandard
    if _grid is None:
        _grid = np.load(GRID, mmap_mode='r')
        _water = np.load(WATER, mmap_mode='r')
    face, level, x, y, zlevel, step = job
    d = directions(face, level, x, y, TILE + 1, 1)
    heights = sample(_grid, d) / 2
    levels = nearest(_water, d).astype(np.float64) / 2
    wet = (levels > DRY / 2) & (heights <= levels + HIGHEST_WET)
    if not wet.any():
        kind, levels = LAND, None
    else:
        heights = np.where(wet, np.minimum(heights, levels - SHALLOWEST), heights)
        kind = SEA if wet.all() and not levels.any() and heights.max() < SEA_BELOW else SHORE
    compressor = zstandard.ZstdCompressor(level=zlevel)
    blobs = [b'', b'']
    if kind != SEA:
        blobs[0] = compressor.compress(encode(np.rint(heights / step).astype(np.int16)))
    if kind == SHORE:
        codes = np.where(wet, np.rint(levels / step), DRY).astype(np.int16)
        blobs[1] = compressor.compress(codes.tobytes())
    return face, level, x, y, kind, blobs


def build(out, workers, zlevel, limit, step, source):
    if not GRID.exists() or not WATER.exists():
        sys.exit('run ingest and water first')
    table = runways(source)
    jobs = [(f, l, x, y, zlevel, step) for f in range(6) for l in range(LEVELS)
            for y in range(ROOTS << l) for x in range(ROOTS << l)]
    if limit:
        jobs = jobs[::max(1, len(jobs) // limit)]
    start = time.time()
    index, data = [], bytearray()
    stats = {}
    with multiprocessing.Pool(workers) as pool:
        for n, (f, l, x, y, kind, blobs) in enumerate(pool.imap(build_tile, jobs, chunksize=8)):
            entry = [f, l, x, y, kind]
            for blob in blobs:
                entry += [len(data), len(blob)]
                data += blob
            index.append(entry)
            s = stats.setdefault((l, kind), [0, 0])
            s[0] += 1
            s[1] += len(blobs[0]) + len(blobs[1])
            if n % 1000 == 0:
                print(f'\r{n}/{len(jobs)} tiles, {len(data) / 2**20:.0f} MiB, {time.time() - start:.0f} s', end='', flush=True)
    index_offset = HEADER.size
    runway_offset = index_offset + ENTRY.size * len(index)
    data_offset = runway_offset + RUNWAY.size * len(table)
    with open(out, 'wb') as fh:
        fh.write(HEADER.pack(b'OFSE', 2, TILE, ROOTS, LEVELS, len(index), step, len(table),
                             index_offset, runway_offset, data_offset))
        for entry in index:
            fh.write(ENTRY.pack(*entry))
        for runway in table:
            fh.write(runway)
        fh.write(data)
    total = Path(out).stat().st_size
    print(f'\n{len(jobs)} tiles in {time.time() - start:.0f} s -> {out}: {total / 2**20:.1f} MiB')
    names = {LAND: 'land ', SEA: 'sea  ', SHORE: 'shore'}
    for (l, kind), (count, size) in sorted(stats.items()):
        print(f'  level {l} {names[kind]}: {count:6d} tiles, {size / 2**20:8.1f} MiB')


def timing(pack):
    import zstandard
    data = Path(pack).read_bytes()
    header = HEADER.unpack_from(data, 0)
    count, index_offset, data_offset = header[5], header[8], header[10]
    entries = [ENTRY.unpack_from(data, index_offset + i * ENTRY.size) for i in range(count)]
    land = [e for e in entries if e[4] != SEA and e[1] == LEVELS - 1][:2000]
    d = zstandard.ZstdDecompressor()
    start = time.perf_counter()
    for e in land:
        raw = d.decompress(data[data_offset + e[5]:data_offset + e[5] + e[6]])
        decode(raw, TILE + 1)
    each = (time.perf_counter() - start) / max(1, len(land))
    print(f'{len(land)} leaf tiles: {each * 1000:.2f} ms each to decompress and decode (Python/numpy)')


# Detail regions: squares of 10 degrees, named by their south-west corner
# (N40E010). A theatre needs every region its box touches: 4.5 degrees of
# latitude (500 km) either side of its anchor, and as many kilometres east and
# west. Mirrors theatreRegions in core/src/theatres.cpp.
THEATRES = {'austria': (47.6, 13.8), 'alps': (46.1, 7.4), 'gulf': (26.2, 53.0), 'moscow': (55.75, 37.6)}
THEATRE_REACH = 4.5                   # degrees of latitude


def region_name(lat, lon):
    """The region holding a point, from its corner in whole tens of degrees."""
    south, west = math.floor(lat / 10) * 10, math.floor(lon / 10) * 10
    return f"{'N' if south >= 0 else 'S'}{abs(south):02d}{'E' if west >= 0 else 'W'}{abs(west):03d}"


def theatre_regions(theatre):
    lat, lon = THEATRES[theatre]
    east = THEATRE_REACH / math.cos(math.radians(lat))
    south, north = math.floor((lat - THEATRE_REACH) / 10), math.floor((lat + THEATRE_REACH) / 10)
    west, far = math.floor((lon - east) / 10), math.floor((lon + east) / 10)
    return sorted({region_name(10 * a, 10 * b) for a in range(south, north + 1) for b in range(west, far + 1)})


def glo90_corner(name):
    """South-west corner of a GLO-90 tile from its name, in whole degrees."""
    parts = name.split('_')
    lat = int(parts[4][1:]) * (1 if parts[4][0] == 'N' else -1)
    lon = int(parts[6][1:]) * (1 if parts[6][0] == 'E' else -1)
    return lat, lon


GLO90_BUCKET = 'https://copernicus-dem-90m.s3.amazonaws.com/'
GLO90_DIR = WORK / 'dl' / 'glo90'


def fetch_glo90(workers, regions=None):
    """Downloads Copernicus GLO-90 tiles (AWS Open Data, no account) into
    build/earth/dl/glo90/, resuming where an earlier run stopped: those in
    `regions`, or all ~26,500 Cloud Optimized GeoTIFFs of 1 degree (66 GiB).
    The sea has no tiles."""
    import concurrent.futures
    import urllib.request
    GLO90_DIR.mkdir(parents=True, exist_ok=True)
    listing = GLO90_DIR / 'tileList.txt'
    if not listing.exists():
        urllib.request.urlretrieve(GLO90_BUCKET + 'tileList.txt', listing)
    names = [n.strip() for n in listing.read_text().split() if n.strip()]
    if regions:
        names = [n for n in names if region_name(*glo90_corner(n)) in regions]
        print(f'{len(names)} tiles in {len(regions)} regions: {" ".join(sorted(regions))}')
    tiles = GLO90_DIR / 'tiles'
    tiles.mkdir(exist_ok=True)

    def one(name):
        target = tiles / (name + '.tif')
        if target.exists():
            return 0
        part = target.with_suffix('.part')
        for attempt in range(5):
            try:
                with urllib.request.urlopen(f'{GLO90_BUCKET}{name}/{name}.tif', timeout=120) as response, open(part, 'wb') as out:
                    expected = int(response.headers['Content-Length'])
                    while chunk := response.read(1 << 20):
                        out.write(chunk)
                if part.stat().st_size != expected:
                    raise OSError('short download')
                part.rename(target)
                return expected
            except OSError as error:
                if attempt == 4:
                    print(f'\n{name}: {error}', flush=True)
                time.sleep(2 + 4 * attempt)
        return 0

    start, total, done = time.time(), 0, 0
    with concurrent.futures.ThreadPoolExecutor(workers) as pool:
        for size in pool.map(one, names):
            done += 1
            total += size
            if done % 100 == 0 or done == len(names):
                rate = total / max(time.time() - start, 1e-3) / 2**20
                print(f'\r{done}/{len(names)} tiles, {total / 2**30:.2f} GiB this run, {rate:.1f} MiB/s', end='', flush=True)
    missing = [n for n in names if not (tiles / (n + '.tif')).exists()]
    print(f'\n{len(names) - len(missing)} of {len(names)} tiles present' + (f'; missing {missing[:5]}...' if missing else ''))


# ---- Detail: GLO-90 by region ---------------------------------------------
# Region packs (version 1) hold land at DETAIL_LEVEL, two levels below the base
# pack's leaves: 16 tiles under each base leaf that is not open sea, ~116 m
# between samples at a face centre, from Copernicus GLO-90 (90 m). Heights only;
# the water stays the base pack's, and water mapped over a sample lowers it
# below the water's level as in the base pack. Where GLO-90 has no tile (the
# sea) the base grid fills in. Layout, little-endian:
#   header  "OFSD", u16 version, u16 tile intervals, u16 roots, u16 level,
#           u32 tiles, f32 metres per step, 8-byte region name, u64 index
#           offset, u64 data offset
#   index   per tile, sorted by face, row, column: u8 face, u32 column, u32 row,
#           u64 offset into the data, u32 size
#   data    each tile's heights as the base pack's (Lorenzo, zigzag, byte
#           planes, zstd)
DETAIL_LEVEL = LEVELS + 1             # index 7: 3 << 7 = 384 tiles per face edge
DETAIL_HEADER = struct.Struct('<4sHHHHIf8sQQ')
DETAIL_ENTRY = struct.Struct('<BIIQI')
DETAIL_DIR = WORK / 'detail'


def level_directions(face, level, x, y):
    """As directions(), for any level, including those below the base pack's."""
    normal, right, up = (np.array(a, float) for a in FACES[face])
    span = (ROOTS * TILE) << level
    index = np.arange(TILE + 1)
    u = -1 + 2 * (x * TILE + index) / span
    v = -1 + 2 * (y * TILE + index) / span
    tu, tv = np.tan(u * math.pi / 4), np.tan(v * math.pi / 4)
    d = normal + tu[None, :, None] * right + tv[:, None, None] * up
    return d / np.linalg.norm(d, axis=2, keepdims=True)


class Glo90:
    """GLO-90 heights by bilinear interpolation across its 1 degree tiles.
    Samples are points: row 0 is the tile's north edge, column 0 its west."""

    def __init__(self):
        self.tiles = GLO90_DIR / 'tiles'
        self.cache = {}

    def raw(self, lat, lon):
        lon = (lon + 180) % 360 - 180
        key = ('raw', lat, lon)
        if key not in self.cache:
            name = (f"Copernicus_DSM_COG_30_{'N' if lat >= 0 else 'S'}{abs(lat):02d}_00_"
                    f"{'E' if lon >= 0 else 'W'}{abs(lon):03d}_00_DEM.tif")
            path = self.tiles / name
            if path.exists():
                import tifffile
                with tifffile.TiffFile(path) as tif:
                    self.cache[key] = tif.pages[0].asarray().astype(np.float32)
            else:
                self.cache[key] = None
        return self.cache[key]

    def padded(self, lat, lon):
        """The tile with one more row and column, from its south and east
        neighbours, NaN where there is none."""
        key = ('pad', lat, lon)
        if key in self.cache:
            return self.cache[key]
        if len(self.cache) > 200:
            self.cache.clear()
        own = self.raw(lat, lon)
        if own is None:
            self.cache[key] = None
            return None
        rows, cols = own.shape
        out = np.full((rows + 1, cols + 1), np.nan, np.float32)
        out[:rows, :cols] = own
        east = self.raw(lat, lon + 1)
        if east is not None:
            out[:rows, cols] = east[:, 0]
        if lat - 1 >= -90:
            south = self.raw(lat - 1, lon)
            if south is not None:
                # The tile below may have another width: read its top row at our columns.
                width = south.shape[1]
                out[rows, :cols] = np.interp(np.arange(cols) * width / cols, np.arange(width), south[0])
            corner = self.raw(lat - 1, lon + 1)
            if corner is not None:
                out[rows, cols] = corner[0, 0]
        self.cache[key] = out
        return out

    def heights(self, lat, lon):
        """Metres at points given in degrees; NaN where GLO-90 has no tile."""
        result = np.full(lat.shape, np.nan)
        south, west = np.floor(lat).astype(np.int64), np.floor(lon).astype(np.int64)
        for a, b in set(zip(south.ravel().tolist(), west.ravel().tolist())):
            tile = self.padded(a, b)
            if tile is None:
                continue
            here = (south == a) & (west == b)
            cols = tile.shape[1] - 1
            r = (a + 1 - lat[here]) * 1200
            c = (lon[here] - b) * cols
            r0 = np.clip(np.floor(r).astype(np.int64), 0, 1199)
            c0 = np.clip(np.floor(c).astype(np.int64), 0, cols - 1)
            fr, fc = r - r0, c - c0
            top = tile[r0, c0] * (1 - fc) + tile[r0, c0 + 1] * fc
            bottom = tile[r0 + 1, c0] * (1 - fc) + tile[r0 + 1, c0 + 1] * fc
            result[here] = top * (1 - fr) + bottom * fr
        return result


def base_leaves(pack):
    """(face, x, y, kind) of every leaf of a base pack."""
    with open(pack, 'rb') as fh:
        header = HEADER.unpack(fh.read(HEADER.size))
        count, index_offset = header[5], header[8]
        fh.seek(index_offset)
        raw = fh.read(ENTRY.size * count)
    leaves = []
    for k in range(count):
        f, l, x, y, kind = ENTRY.unpack_from(raw, k * ENTRY.size)[:5]
        if l == LEVELS - 1:
            leaves.append((f, x, y, kind))
    return leaves


def leaf_region(face, x, y):
    d = level_directions(face, LEVELS - 1, x, y)[TILE // 2, TILE // 2]
    lat = math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
    return region_name(lat, math.degrees(math.atan2(d[1], d[0])))


_glo = None


def build_detail_tile(job):
    global _grid, _water, _glo
    import zstandard
    if _grid is None:
        _grid = np.load(GRID, mmap_mode='r')
        _water = np.load(WATER, mmap_mode='r')
    if _glo is None:
        _glo = Glo90()
    face, x, y, zlevel, step = job
    d = level_directions(face, DETAIL_LEVEL, x, y)
    lat = np.degrees(np.arctan2(d[..., 2], np.hypot(d[..., 0], d[..., 1])))
    lon = np.degrees(np.arctan2(d[..., 1], d[..., 0]))
    heights = _glo.heights(lat, lon)
    base = sample(_grid, d) / 2
    # GLO-90 records the sea as exactly 0 inside tiles that also cover land, so
    # those samples, and any it has no tile for, take the base grid's height:
    # the sea's real depth, as in the base pack.
    sea = np.isnan(heights) | (heights == 0)
    heights = np.where(sea, base, heights)
    levels = nearest(_water, d).astype(np.float64) / 2
    wet = (levels > DRY / 2) & (heights <= levels + HIGHEST_WET)
    # Water's bed is the base pack's, as there: GLO-90 measures a lake's or
    # sea's surface, not its bed, so it would leave the Caspian two metres deep.
    heights = np.where(wet, np.minimum(base, levels - SHALLOWEST), heights)
    blob = zstandard.ZstdCompressor(level=zlevel).compress(encode(np.rint(heights / step).astype(np.int16)))
    return face, x, y, blob, int(sea.sum())


def detail(regions, pack, workers, zlevel, step):
    """Builds build/earth/detail/earth-detail-<region>.ofsd for each region."""
    if not GRID.exists() or not WATER.exists():
        sys.exit('run ingest and water first')
    leaves = [(f, x, y) for f, x, y, kind in base_leaves(pack) if kind != SEA]
    by_region = {}
    for f, x, y in leaves:
        by_region.setdefault(leaf_region(f, x, y), []).append((f, x, y))
    DETAIL_DIR.mkdir(parents=True, exist_ok=True)
    for region in sorted(regions or by_region):
        jobs = sorted((f, 4 * x + i, 4 * y + j, zlevel, step)
                      for f, x, y in by_region.get(region, []) for j in range(4) for i in range(4))
        if not jobs:
            print(f'{region}: no land')
            continue
        start, index, data, filled = time.time(), [], bytearray(), 0
        # Grouped by face and row so each worker reads neighbouring GLO-90 tiles.
        with multiprocessing.Pool(workers) as pool:
            for n, (f, x, y, blob, missing) in enumerate(pool.imap(build_detail_tile, jobs, chunksize=4)):
                index.append((f, x, y, len(data), len(blob)))
                data += blob
                filled += missing
                if n % 200 == 0:
                    print(f'\r{region}: {n}/{len(jobs)} tiles, {len(data) / 2**20:.0f} MiB, {time.time() - start:.0f} s', end='', flush=True)
        index.sort(key=lambda e: (e[0], e[2], e[1]))
        out = DETAIL_DIR / f'earth-detail-{region}.ofsd'
        index_offset = DETAIL_HEADER.size
        data_offset = index_offset + DETAIL_ENTRY.size * len(index)
        with open(out, 'wb') as fh:
            fh.write(DETAIL_HEADER.pack(b'OFSD', 1, TILE, ROOTS, DETAIL_LEVEL, len(index), step,
                                        region.encode().ljust(8, b'\0'), index_offset, data_offset))
            for f, x, y, offset, size in index:
                fh.write(DETAIL_ENTRY.pack(f, x, y, offset, size))
            fh.write(data)
        share = filled / (len(jobs) * (TILE + 1) ** 2)
        print(f'\r{region}: {len(jobs)} tiles in {time.time() - start:.0f} s -> {out.name}: '
              f'{out.stat().st_size / 2**20:.1f} MiB, {100 * share:.1f}% of samples from the base grid')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('ingest')
    p.add_argument('source', type=Path, help='folder of the 288 ETOPO 2022 15s surface GeoTIFFs')
    p = sub.add_parser('water')
    p.add_argument('source', type=Path, help='folder holding ne_10m_land, ne_10m_minor_islands and ne_10m_lakes')
    p = sub.add_parser('build')
    p.add_argument('runways', type=Path, help='folder holding OurAirports runways.csv and airports.csv')
    p.add_argument('--out', type=Path, default=WORK / 'earth.ofse')
    p.add_argument('--workers', type=int, default=6)
    p.add_argument('--zstd', type=int, default=19)
    p.add_argument('--sample', type=int, default=0, help='build only about this many tiles')
    p.add_argument('--step', type=float, default=1.0, help='metres per stored height step')
    p = sub.add_parser('cover')
    p.add_argument('source', type=Path, help='Natural Earth II NE2_HR_LC.tif')
    p.add_argument('--airports', type=Path, default=WORK / 'dl' / 'oa', help='folder holding OurAirports airports.csv and runways.csv')
    p.add_argument('--towns', type=Path, default=WORK / 'dl' / 'ne' / 'ne_10m_populated_places_simple',
                   help='folder holding Natural Earth ne_10m_populated_places_simple')
    p.add_argument('--out', type=Path, default=COVER)
    p = sub.add_parser('fetch-glo90')
    p.add_argument('--workers', type=int, default=12)
    p.add_argument('--theatres', default='', help='comma-separated theatres whose regions to fetch; all when empty')
    p = sub.add_parser('detail')
    p.add_argument('--theatres', default='', help='comma-separated theatres whose regions to build')
    p.add_argument('--regions', default='', help='comma-separated regions (N40E010); all with land when both are empty')
    p.add_argument('--pack', type=Path, default=WORK / 'earth.ofse')
    p.add_argument('--workers', type=int, default=6)
    p.add_argument('--zstd', type=int, default=19)
    p.add_argument('--step', type=float, default=1.0)
    p = sub.add_parser('timing')
    p.add_argument('pack', type=Path)
    args = parser.parse_args()
    if args.command == 'ingest':
        ingest(args.source)
    elif args.command == 'water':
        water(args.source)
    elif args.command == 'cover':
        cover(args.source, args.out, args.airports, args.towns)
    elif args.command == 'fetch-glo90':
        regions = {r for t in args.theatres.split(',') if t for r in theatre_regions(t)}
        fetch_glo90(args.workers, regions or None)
    elif args.command == 'detail':
        regions = {r for t in args.theatres.split(',') if t for r in theatre_regions(t)}
        regions |= {r for r in args.regions.split(',') if r}
        detail(regions, args.pack, args.workers, args.zstd, args.step)
    elif args.command == 'build':
        build(args.out, args.workers, args.zstd, args.sample, args.step, args.runways)
    else:
        timing(args.pack)


if __name__ == '__main__':
    main()
