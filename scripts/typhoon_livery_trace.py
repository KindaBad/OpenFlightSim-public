"""Paint the Austrian Air Force 7L-WO display scheme into the Typhoon's UV atlas.

The scheme is black over the wings and spine with white contour lines, red
shapes along the fuselage, a red-white-red band behind the leading edges that
turns up the fuselage side and runs forward to the canopy, eyes on the
foreplanes, the arms on the spine and a tiger on the fin. It is taken from two
photographs the project owner supplied: one from above, which gives everything
seen from there, and one from the port quarter, which gives the fin. Neither
photograph is kept in the repository; what this writes is

  assets/aircraft/typhoon/livery_paint.png

an RGBA map in the donor's own UV layout: the paint where the airframe is
painted, transparent over the nozzles, wells, cockpit and fittings.
scripts/typhoon_donor_import.py lays it over the donor's base colour and keeps
the donor's panel lines.

Two steps. The first runs in Blender and writes the donor's triangles and base
colour; the second needs numpy, Pillow, scipy and scikit-image:

  blender -b --factory-startup --disable-autoexec \
    --python scripts/typhoon_livery_trace.py -- --source /path/Eurofighter.blend --dump /tmp/typhoon
  python3 scripts/typhoon_livery_trace.py --dump /tmp/typhoon \
    --top /path/above.jpg --rear /path/quarter.jpg --preview /tmp/typhoon

The photographs are too coarse to be copied onto the airframe: at this size a
contour line is two pixels wide. So they are read, not copied. A pinhole camera
is fitted to the photograph from above (the pose below) and every texel is
carried through it, which says for each texel whether the photograph shows red,
a white line, grey or black there. Those answers are cleaned up in the atlas,
where the wings and the fuselage halves lie flat, and the paint is then laid
down afresh: flat colours with clean edges, the lines redrawn along their
centres at one width, and the straight things (the band, the code, the
roundels) drawn from measurements instead of traced. Only the eyes, the arms
and the fin, which are pictures, come from the photographs' own pixels.

Surfaces the camera cannot see take the other side's paint mirrored; the
underside and the lower flanks, which neither photograph shows, stay grey.

Donor frame throughout: ten units to the metre, nose +X, port +Y, up +Z.
"""
import argparse
import math
import sys
from pathlib import Path

import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('--project-root', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--source', type=Path, help='Donor .blend; Blender step only')
parser.add_argument('--dump', type=Path, required=True, help='Directory the Blender step writes and the trace reads')
parser.add_argument('--top', type=Path, help='Photograph from above')
parser.add_argument('--rear', type=Path, help='Photograph from the port quarter')
parser.add_argument('--size', type=int, default=4096)
parser.add_argument('--output', type=Path)
parser.add_argument('--preview', type=Path, help='Directory for check renders of the result')
options = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:])

try:
    import bpy
except ImportError:
    bpy = None

if bpy is not None:
    # --- Blender step: the donor's triangles, UVs and base colour ------------
    if not options.source:
        raise SystemExit('--source is required inside Blender')
    bpy.ops.wm.open_mainfile(filepath=str(options.source.resolve()), load_ui=False, use_scripts=False)
    graph = bpy.context.evaluated_depsgraph_get()
    points, coords, normals, owners, names = [], [], [], [], []
    for obj in bpy.context.scene.objects:
        if obj.type != 'MESH':
            continue
        mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(graph), depsgraph=graph)
        mesh.transform(obj.matrix_world)
        mesh.calc_loop_triangles()
        uv = mesh.uv_layers.active.data
        names.append(obj.name)
        for triangle in mesh.loop_triangles:
            points.append([mesh.vertices[i].co[:] for i in triangle.vertices])
            coords.append([uv[i].uv[:] for i in triangle.loops])
            normals.append(triangle.normal[:])
            owners.append(len(names) - 1)
    options.dump.mkdir(parents=True, exist_ok=True)
    np.savez(options.dump / 'geometry.npz', P=np.array(points, np.float32), U=np.array(coords, np.float32),
             N=np.array(normals, np.float32), O=np.array(owners), names=np.array(names))
    base = bpy.data.images['basecolor.png']
    base.filepath_raw = str(options.dump / 'basecolor.png')
    base.file_format = 'PNG'
    base.save()
    print('TYPHOON LIVERY DUMP', len(points), names)
    sys.exit(0)

import scipy.ndimage as ndi  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
from skimage.filters import apply_hysteresis_threshold  # noqa: E402
from skimage.morphology import disk, remove_small_holes, remove_small_objects, skeletonize  # noqa: E402

Image.MAX_IMAGE_PIXELS = None
if not options.top or not options.rear:
    raise SystemExit('--top and --rear are required')
SIZE = options.size
OUTPUT = (options.output or options.project_root / 'assets/aircraft/typhoon/livery_paint.png').resolve()

# --- What was read off the photographs -----------------------------------------
# Camera of the photograph from above, fitted to the airframe's outline: a
# rotation vector applied before BASE, the airframe's place in camera space in
# donor units, and the focal length in pixels of the 1600 px wide photograph.
BASE = np.array([[-.89, .46, 0], [.46, .89, 0], [0, 0, -1.]])
POSE = np.array([-.6745, -.1847, -.1503, 22.6022, -2.5489, 271.1799, 2489.7053])
TOP_WIDTH = 1600
# The band's white centre on the wings, as X = BAND[0] + BAND[1] |Y|: a line
# fitted to it on each wing, averaged. Outboard it ends at the tip, where a
# thin arm (HOOK, in X and |Y|) turns forward along the tip; inboard it meets
# the fuselage side at ROOT.
BAND = (48.55, -1.24)
HOOK = ((-14.69, 51.), (-8.49, 49.9))
ROOT = 9.
# On the fuselage side, in X and Z: the band climbs aft from the wing root at
# JOINT to VERTEX, and from there a thin stripe runs forward along SPIKE (Z at
# X = 36 and its slope) to a point at SPIKE_END beside the canopy. Black above
# the stripe, grey in the notch between the two.
JOINT_Z = -1.75
VERTEX = (29.4, 3.43)
SPIKE = (4.2, .118)
SPIKE_END = 64.5
# Half widths in donor units, from profiles across the band: its white, its
# red toward the grey, and where the red has faded into the black.
BAND_WHITE, BAND_RED, BAND_FADE = .48, 1.4, 2.5
# The code on the port side: where it is centred, its baseline, its cap height
# and half its stroke, and each letter's extent along the reading direction
# from the centre. The roundel sits between the halves.
CODE_CENTRE, CODE_BASE, CODE_CAP, CODE_STROKE = 39.03, 1.3, 2.8, .235
CODE_BOXES = {'7': (-7.58, -5.31), 'L': (-4.97, -2.81), 'W': (2.42, 5.37), 'O': (5.60, 7.58)}
CODE_ROUNDEL = (-.31, 1.27, 1.8)   # along, above the baseline, radius
# The roundel on the starboard wing is found in the photograph near here.
WING_ROUNDEL = (-11.5, -40.2)
# The arms on the spine, as a box in the photograph from above.
ARMS = (700, 525, 835, 615)
# Fin corners in the quarter photograph (1400 px wide) against the donor's fin
# in (x, z): leading and trailing edge at the cap and at the root.
FIN_MODEL = np.array([(-43.8, 35.), (-53., 35.), (-8.9, 10.), (-40.6, 10.)])
FIN_PHOTO = np.array([(846.7, 340.8), (871., 364.), (721.7, 433.3), (800., 513.)])
REAR_WIDTH = 1400
# The lettering down the fin lies this far ahead of the trailing edge, below
# the stripe under the cap. The other face mirrors the fin, which would turn
# the letters over, so there the strip is turned back about its middle.
FIN_TEXT = (1.7, 5.3)
FIN_TEXT_TOP = 30.8
# Below this height the quarter photograph's fin is behind the fuselage.
FIN_FLOOR = (13.6, 15.2)

BLACK = np.array((.011, .011, .012), np.float32)
RED = np.array((.56, .012, .028), np.float32)
BAND_RED_PAINT = np.array((.60, .012, .026), np.float32)
WHITE = np.array((.86, .86, .84), np.float32)
INK = np.array((.012, .012, .014), np.float32)
LINE_HALF_WIDTH = .165
WEIGHTS = np.array((.2126, .7152, .0722), np.float32)


def linear(colour):
    colour = colour / 255.
    return np.where(colour <= .04045, colour / 12.92, ((colour + .055) / 1.055) ** 2.4).astype(np.float32)


def encoded(colour):
    colour = np.clip(colour, 0, 1)
    return (np.where(colour <= .0031308, colour * 12.92, 1.055 * colour ** (1 / 2.4) - .055) * 255 + .5).astype(np.uint8)


def smoothstep(low, high, value):
    t = np.clip((value - low) / (high - low), 0, 1)
    return t * t * (3 - 2 * t)


def rotation(vector):
    angle = np.linalg.norm(vector)
    if angle < 1e-12:
        return np.eye(3)
    k = vector / angle
    cross = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return np.eye(3) + math.sin(angle) * cross + (1 - math.cos(angle)) * cross @ cross


def rasterise(corners, depth_of, width, height, tolerance=0.):
    """Nearest triangle and its depth under every cell. `corners` is (n, 3, 2)."""
    depth = np.full((height, width), np.inf, np.float32)
    owner = np.full((height, width), -1, np.int32)
    for index in range(len(corners)):
        t, d = corners[index].astype(np.float64), depth_of[index]
        if (d <= 0).any():
            continue
        x0, x1 = int(max(0, math.floor(t[:, 0].min()))), int(min(width - 1, math.ceil(t[:, 0].max())))
        y0, y1 = int(max(0, math.floor(t[:, 1].min()))), int(min(height - 1, math.ceil(t[:, 1].max())))
        area = (t[1, 1] - t[2, 1]) * (t[0, 0] - t[2, 0]) + (t[2, 0] - t[1, 0]) * (t[0, 1] - t[2, 1])
        if x1 < x0 or y1 < y0 or abs(area) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + .5, np.arange(y0, y1 + 1) + .5)
        a = ((t[1, 1] - t[2, 1]) * (gx - t[2, 0]) + (t[2, 0] - t[1, 0]) * (gy - t[2, 1])) / area
        b = ((t[2, 1] - t[0, 1]) * (gx - t[2, 0]) + (t[0, 0] - t[2, 0]) * (gy - t[2, 1])) / area
        c = 1 - a - b
        hit = (a >= -tolerance) & (b >= -tolerance) & (c >= -tolerance)
        if not hit.any():
            continue
        at = a * d[0] + b * d[1] + c * d[2]
        window, owners = depth[y0:y1 + 1, x0:x1 + 1], owner[y0:y1 + 1, x0:x1 + 1]
        nearer = hit & (at < window)
        window[nearer] = at[nearer]
        owners[nearer] = index
    return depth, owner


def sampled(image, x, y, order=1):
    """`image` at pixel positions (x, y), channel by channel."""
    at = [y - .5, x - .5]
    if image.ndim == 2:
        return ndi.map_coordinates(image, at, order=order, mode='nearest')
    return np.stack([ndi.map_coordinates(image[..., c], at, order=order, mode='nearest')
                     for c in range(image.shape[2])], -1)


def over(canvas, colour, cover):
    return canvas * (1 - cover[..., None]) + colour * cover[..., None]


# --- The donor -------------------------------------------------------------------
geometry = np.load(options.dump / 'geometry.npz')
P, U, N, O = geometry['P'], geometry['U'], geometry['N'], geometry['O']
names = list(geometry['names'])
AIRFRAME, CANOPY = names.index('Airframe'), names.index('Canopy')
air = np.nonzero(O == AIRFRAME)[0]

# Position, facing and triangle of every airframe texel. Rows run up, as V does.
flat = np.concatenate([U[air] * SIZE, np.zeros((len(air), 3, 1), np.float32)], axis=2)
_, texel_triangle = rasterise(flat[:, :, :2], np.ones((len(air), 3), np.float32) + np.arange(len(air))[:, None] * 0,
                              SIZE, SIZE, tolerance=.02)
covered = texel_triangle >= 0
rows, columns = np.nonzero(covered)
triangle = air[texel_triangle[rows, columns]]
t = U[triangle].astype(np.float64) * SIZE
gx, gy = columns + .5, rows + .5
area = (t[:, 1, 1] - t[:, 2, 1]) * (t[:, 0, 0] - t[:, 2, 0]) + (t[:, 2, 0] - t[:, 1, 0]) * (t[:, 0, 1] - t[:, 2, 1])
wa = ((t[:, 1, 1] - t[:, 2, 1]) * (gx - t[:, 2, 0]) + (t[:, 2, 0] - t[:, 1, 0]) * (gy - t[:, 2, 1])) / area
wb = ((t[:, 2, 1] - t[:, 0, 1]) * (gx - t[:, 2, 0]) + (t[:, 0, 0] - t[:, 2, 0]) * (gy - t[:, 2, 1])) / area
position = (wa[:, None] * P[triangle, 0] + wb[:, None] * P[triangle, 1] +
            (1 - wa - wb)[:, None] * P[triangle, 2]).astype(np.float32)
facing = N[triangle]
del t, gx, gy, area, wa, wb

# UV islands: triangles joined across edges whose UVs agree.
parent = {int(i): int(i) for i in air}


def find(i):
    while parent[i] != i:
        parent[i] = parent[parent[i]]
        i = parent[i]
    return i


edges = {}
quantised = np.round(U * 65536).astype(np.int64)
for i in air:
    for e in range(3):
        a, b = tuple(quantised[i, e]), tuple(quantised[i, (e + 1) % 3])
        other = edges.setdefault((a, b) if a < b else (b, a), int(i))
        if other != i:
            parent[find(int(i))] = find(other)
island = np.array([find(int(i)) for i in triangle])

# Painted skin is the donor's grey or a marking laid over it; whole islands are
# repainted, so its Luftwaffe crosses, serials and badges go. Darker, browner
# or metallic islands (nozzles, wells, intakes, cockpit) are left alone.
donor = Image.open(options.dump / 'basecolor.png').convert('RGB')
if donor.size != (SIZE, SIZE):
    donor = donor.resize((SIZE, SIZE), Image.LANCZOS)
donor = linear(np.asarray(donor).astype(np.float32))[::-1][rows, columns]
luminance = donor @ WEIGHTS
level = float(np.median(luminance))
GREY = np.median(donor[np.abs(luminance - level) < .1 * level], axis=0)
greyish = (np.abs(luminance - level) < .35 * level) & (donor.max(1) - donor.min(1) < .08)
ids, inverse = np.unique(island, return_inverse=True)
painted = (np.bincount(inverse, weights=greyish) > .5 * np.bincount(inverse))[inverse]
del donor, luminance, greyish, island, inverse

def canvas_of(values, fill=0, dtype=np.float32):
    """Per-texel values laid out as the atlas, rows running up."""
    out = np.full((SIZE, SIZE) + np.shape(values)[1:], fill, dtype)
    out[rows, columns] = values
    return out


# Donor units a texel spans, so that widths can be given on the aircraft.
edge_a, edge_b = P[air, 1] - P[air, 0], P[air, 2] - P[air, 0]
flat_uv = U[air] * SIZE
uv_area = np.abs((flat_uv[:, 1, 0] - flat_uv[:, 0, 0]) * (flat_uv[:, 2, 1] - flat_uv[:, 0, 1]) -
                 (flat_uv[:, 2, 0] - flat_uv[:, 0, 0]) * (flat_uv[:, 1, 1] - flat_uv[:, 0, 1])) / 2
span = np.sqrt(np.linalg.norm(np.cross(edge_a, edge_b), axis=1) / 2 / np.maximum(uv_area, 1e-9))
SPAN = canvas_of(span[texel_triangle[rows, columns]], .05)
SOFT = np.maximum(SPAN, .03)   # An edge is softened over a texel either way.
PAINTED = canvas_of(painted, False, bool)
X, Y, Z = (canvas_of(position[:, axis]) for axis in range(3))
NY, NZ = canvas_of(facing[:, 1]), canvas_of(facing[:, 2])
AY = np.abs(Y)


def inside(value, limit):
    """1 where value < limit, softened over a texel."""
    return 1 - smoothstep(-1, 1, (value - limit) / SOFT)


# --- The photograph from above, read for what it shows -------------------------
photo = Image.open(options.top).convert('RGB')
if photo.width != TOP_WIDTH:
    photo = photo.resize((TOP_WIDTH, round(photo.height * TOP_WIDTH / photo.width)), Image.LANCZOS)
WIDTH, HEIGHT = photo.size
raw = np.asarray(photo).astype(np.float32)
seen = linear(raw)
seen_luminance = seen @ WEIGHTS
# An opening takes the thin white contour lines off and leaves what they cross.
opened_image = photo.filter(ImageFilter.MinFilter(7)).filter(ImageFilter.MaxFilter(7))
opened = linear(np.asarray(opened_image.filter(ImageFilter.GaussianBlur(1))).astype(np.float32))
opened_luminance = opened @ WEIGHTS
soft_luminance = linear(np.asarray(opened_image.filter(ImageFilter.GaussianBlur(5))).astype(np.float32)) @ WEIGHTS
redness = (seen[..., 0] - np.maximum(seen[..., 1], seen[..., 2])) / np.maximum(seen[..., 0], 1e-3)
opened_redness = (opened[..., 0] - np.maximum(opened[..., 1], opened[..., 2])) / np.maximum(opened[..., 0], 1e-3)
# The lines are white; the streaks of sky on the gloss black are not quite.
neutral = (seen[..., 0] - seen[..., 2]) < .09 * seen[..., 0] + .02
line_seen = smoothstep(.15, .45, (seen_luminance - opened_luminance) / np.maximum(.95 - opened_luminance, .05)) * \
    (redness < .15) * neutral * smoothstep(.25, .5, seen_luminance)
grey_seen = ((opened_redness < .08) & (opened_luminance > .42) & (opened_luminance < .86) &
             (opened[..., 2] - opened[..., 0] > -.03)).astype(np.float32)
# The aircraft against the forest, which is hazed to blue-green throughout.
aircraft = Image.fromarray(((raw[..., 1] - raw[..., 0] <= 11) * 255).astype(np.uint8))
aircraft = np.asarray(aircraft.filter(ImageFilter.MedianFilter(7)).filter(ImageFilter.MinFilter(5))
                      .filter(ImageFilter.MaxFilter(5))) > 0
beyond = ndi.distance_transform_edt(~aircraft).astype(np.float32)   # Pixels outside its outline.
well_inside = ndi.binary_erosion(aircraft, iterations=3)

# --- Through the camera ---------------------------------------------------------
turn = rotation(POSE[:3]) @ BASE
eye = -turn.T @ POSE[3:6]


def through(points):
    camera = points @ turn.T + POSE[3:6]
    return (POSE[6] * camera[..., 0] / camera[..., 2] + WIDTH / 2,
            POSE[6] * camera[..., 1] / camera[..., 2] + HEIGHT / 2, camera[..., 2])


outer = np.nonzero((O == AIRFRAME) | (O == CANOPY))[0]
px, py, pz = through(P[outer].astype(np.float64))
depth, _ = rasterise(np.stack([px, py], -1), pz, WIDTH, HEIGHT)


def from_above(points, normals):
    """Where the photograph has each point, how squarely it faces the camera and whether it is in view."""
    p = points.astype(np.float64)
    x, y, z = through(p)
    view = eye - p
    toward = (view * normals).sum(1) / np.linalg.norm(view, axis=1)
    within = (x >= 1) & (x < WIDTH - 2) & (y >= 1) & (y < HEIGHT - 2) & (z > 0)
    xi, yi = np.clip(x, 0, WIDTH - 2).astype(int), np.clip(y, 0, HEIGHT - 2).astype(int)
    # The farthest of the cells around, so an edge does not hide its own surface.
    behind = np.maximum.reduce([depth[yi, xi], depth[yi + 1, xi], depth[yi, xi + 1], depth[yi + 1, xi + 1]])
    return x.astype(np.float32), y.astype(np.float32), toward.astype(np.float32), within & (z < behind + 1.2)


MIRROR = np.array((1, -1, 1), np.float32)
near_x, near_y, near_toward, near_view = from_above(position, facing)
far_x, far_y, far_toward, far_view = from_above(position * MIRROR, facing * MIRROR)
# A texel takes what the photograph shows of it, or of its mirror image where
# that is seen more squarely. The starboard half of the fuselage faces away
# except along its crest, so it is the port half mirrored.
squarely = near_view & (near_toward > .35)
fuselage_far = (position[:, 1] < -1.5) & (np.abs(position[:, 1]) < ROOT + .3) & (position[:, 0] < SPIKE_END) & \
    (near_toward < .5)
mirrored = far_view & (far_toward > .18) & ((~squarely & (far_toward > near_toward)) | fuselage_far)
direct = near_view & (near_toward > .18) & ~mirrored
at_x, at_y = np.where(mirrored, far_x, near_x), np.where(mirrored, far_y, near_y)
shown = (direct | mirrored) & (sampled(beyond, at_x, at_y) < 40)


def read(field, order=3):
    return canvas_of(np.where(shown, sampled(field.astype(np.float32), at_x, at_y, order), 0))


# What the photograph shows, in the atlas, smoothed among the texels it reaches.
SHOWN = canvas_of(shown & painted, False, bool)


def smoothed(field, sigma):
    return ndi.gaussian_filter(np.where(SHOWN, field, 0), sigma) / \
        np.maximum(ndi.gaussian_filter(SHOWN.astype(np.float32), sigma), 1e-3)


away, nearest = ndi.distance_transform_edt(~SHOWN, return_indices=True)
# Upward skin the camera missed takes its neighbours' answers.
known = SHOWN | (PAINTED & (NZ > .3) & (away < SIZE / 37))


def answered(field, sigma, order=3):
    return smoothed(read(field, order), sigma)[nearest[0], nearest[1]]


grey_shown = answered(grey_seen, 4, 1)
red_shown = answered(np.maximum(redness, opened_redness), 2.)
line_shown = answered(line_seen, 1.2)
sheen_shown = answered(soft_luminance, 5)

# --- The band --------------------------------------------------------------------
# Signed distances from its white centre, positive toward the grey: across the
# wing in plan, and on the fuselage side in elevation for the arm that climbs
# aft from the wing root and for the stripe that runs forward from its top.
joint_x = BAND[0] + BAND[1] * ROOT
arm_slope = (joint_x - VERTEX[0]) / (VERTEX[1] - JOINT_Z)
wing = (AY >= ROOT) & (NZ > .5) & (AY < 52.2)
fuselage = (AY < ROOT + .6) & ~wing & (Z > -2.7) & (X > 20) & (X < SPIKE_END + 1) & (NZ > -.2)
across_wing = (X - (BAND[0] + BAND[1] * AY)) / np.hypot(1, BAND[1])
across_arm = (X - (joint_x - arm_slope * (Z - JOINT_Z))) / np.hypot(1, arm_slope)
across_spike = (SPIKE[0] + SPIKE[1] * (X - 36) - Z) / np.hypot(1, SPIKE[1])


def band(across, scale, fade=True):
    """Cover of the band's white and of its red at `across`, its widths scaled."""
    white = inside(np.abs(across), BAND_WHITE * scale)
    toward_grey = inside(across, BAND_RED * scale) * (across > 0)
    toward_black = np.where(across <= 0, 1 - smoothstep(.96 * BAND_RED * scale, BAND_FADE * scale, -across)
                            if fade else inside(-across, BAND_RED * scale), 0)
    return white, np.maximum(toward_grey, toward_black)


band_white = np.zeros((SIZE, SIZE), np.float32)
band_red = np.zeros((SIZE, SIZE), np.float32)
white, red = band(across_wing, 1.)
band_white, band_red = np.where(wing, white, band_white), np.where(wing, red, band_red)
# The arm along the tip, a third of the band's width.
hook_a, hook_b = np.array(HOOK[0]), np.array(HOOK[1])
along = np.clip(((X - hook_a[0]) * (hook_b - hook_a)[0] + (AY - hook_a[1]) * (hook_b - hook_a)[1]) /
                ((hook_b - hook_a) @ (hook_b - hook_a)), 0, 1)
from_hook = np.hypot(X - (hook_a[0] + along * (hook_b - hook_a)[0]), AY - (hook_a[1] + along * (hook_b - hook_a)[1]))
hook = wing & (across_wing > 0)
band_white = np.maximum(band_white, np.where(hook, inside(from_hook, .17), 0))
band_red = np.maximum(band_red, np.where(hook, inside(from_hook, .55), 0))
# The stripe narrows toward the canopy and ends in a point.
spike_scale = np.clip(.42 - (X - 36) * .0065, .2, .46) * np.clip((SPIKE_END - X) / 3., 0, 1)
under_spike = across_spike > -BAND_RED * spike_scale
arm = fuselage & under_spike & (Z < VERTEX[1] + 1.5) & (X < joint_x + 3)
white, red = band(across_arm, 1.)
band_white, band_red = np.where(arm, white, band_white), np.where(arm, red, band_red)
white, red = band(across_spike, np.maximum(spike_scale, 1e-3), fade=False)
spike = fuselage & (X < SPIKE_END) & (across_arm > -.6)
band_white = np.where(spike, np.maximum(band_white, white), band_white)
band_red = np.where(spike, np.maximum(band_red, red), band_red)

# What the band settles: grey ahead of it and in the notch, black behind it and
# above the stripe. Near it nothing else is drawn; what the photograph shows
# there is the band itself, less cleanly.
above_spike = fuselage & (across_spike <= 0) & (X > VERTEX[0]) & (X < SPIKE_END)
grey_by_band = (wing & (across_wing > 0)) | (fuselage & (across_arm > 0) & (across_spike > 0)) | (X >= SPIKE_END)
black_by_band = (wing & (across_wing <= 0) & (across_wing > -3)) | above_spike | \
    (fuselage & (across_arm <= 0) & (across_arm > -3) & under_spike)
# Aft of the wing the fuselage is black down to the nozzles; the bare panels
# the photograph has there are the photographed model's.
black_by_band |= (AY < 14) & (X < -12) & (X > -60) & (NZ > -.2) & (Z > -3)
quiet = (wing & (across_wing > -2.6)) | (hook & (from_hook < 1.2)) | (fuselage & (across_arm > -2.6) & under_spike) | \
    (above_spike & (np.abs(NY) > .3)) | (fuselage & (np.abs(across_spike) < 1.2) & (X > VERTEX[0] - 1)) | \
    ((AY < 2.6) & (NZ > .5))   # ... and the crest of the spine, where the lines seen are vents and joints.

# --- Grey, black, red and the contour lines ---------------------------------------
# Neither photograph shows the underside or the fuselage below the wing.
unseen = (NZ < -.3) | ((Z < -3.2) & (AY < 14.2) & (NZ < .5)) | ((AY > 14.2) & (NZ < .6))
grey = ndi.gaussian_filter(grey_shown.astype(np.float32), 3) > .5
# Small dark things inside the grey are the photographed model's vents and gaps.
grey = remove_small_holes(grey, max_size=6000) | ~known
grey = remove_small_objects(grey, max_size=3000)
grey = (grey | grey_by_band | unseen) & ~(black_by_band & ~unseen)
settled = grey_by_band | black_by_band
grey_cover = np.where(settled, grey.astype(np.float32), ndi.gaussian_filter(grey.astype(np.float32), 1.2))

canvas = np.broadcast_to(GREY, (SIZE, SIZE, 3)).astype(np.float32).copy()
# The black of the wings is shaded in smoky bands that follow the lines.
# Kept faint: the photograph's wings are lit by the sky as well.
smoke = np.clip(sheen_shown - .09, 0, 1) * smoothstep(11, 14.5, AY) * smoothstep(.8, .95, NZ)
black = BLACK + np.clip(smoke * .2, 0, .05)[..., None] * np.array((1., .98, .92), np.float32)
canvas = canvas * grey_cover[..., None] + black * (1 - grey_cover[..., None])

# Red shapes: whole ones only, their edges rounded, darker toward the rim.
shape = apply_hysteresis_threshold(red_shown, .42, .62) & ~grey & ~quiet & ~(wing & (AY < 11))
shape = ndi.binary_closing(shape, disk(3))
shape = remove_small_holes(remove_small_objects(shape, max_size=1500), max_size=1200)
cover = smoothstep(.3, .7, ndi.gaussian_filter(shape.astype(np.float32), 2.2)) * (1 - grey_cover)
rim = smoothstep(0, 1.6, ndi.distance_transform_edt(shape) * SPAN)
canvas = canvas * (1 - cover[..., None]) + RED * (.5 + .5 * rim)[..., None] * cover[..., None]

# Contour lines: each one thinned to its centre and drawn again at one width.
stroke = apply_hysteresis_threshold(line_shown, .09, .28) & ~grey & ~quiet
centre = skeletonize(remove_small_objects(stroke, max_size=90))
label, count = ndi.label(centre, structure=np.ones((3, 3)))
centre &= ndi.sum(centre, label, np.arange(count + 1))[label] >= 80   # Short ones are glints.
from_line = ndi.distance_transform_edt(~centre) * SPAN
# Every red shape is outlined in white, as the lines run round them.
smooth_shape = ndi.gaussian_filter(shape.astype(np.float32), 2.2) > .5
outline = np.maximum(ndi.distance_transform_edt(smooth_shape), ndi.distance_transform_edt(~smooth_shape)) * SPAN
from_line = np.minimum(from_line, np.where(quiet, 9., outline))
cover = (1 - smoothstep(LINE_HALF_WIDTH - .05, LINE_HALF_WIDTH + .05, from_line)) * (1 - grey_cover)
canvas = over(canvas, WHITE, cover)
canvas = over(over(canvas, BAND_RED_PAINT, band_red), WHITE, band_white)


# --- The code and the roundels ------------------------------------------------
def from_strokes(u, v, points):
    out = np.full(u.shape, 9., np.float32)
    for a, b in zip(points[:-1], points[1:]):
        a, b = np.array(a, float), np.array(b, float)
        t = np.clip(((u - a[0]) * (b - a)[0] + (v - a[1]) * (b - a)[1]) / ((b - a) @ (b - a)), 0, 1)
        out = np.minimum(out, np.hypot(u - (a[0] + t * (b - a)[0]), v - (a[1] + t * (b - a)[1])))
    return out


def from_code(u, v):
    """Distance to the middle of the code's strokes; u runs as it reads, v up from the baseline."""
    k, top = CODE_STROKE, CODE_CAP - CODE_STROKE
    left, right = CODE_BOXES['7'][0] + k, CODE_BOXES['7'][1] - k
    out = from_strokes(u, v, [(left, top), (right, top), (left + .35 * (right - left), 0)])
    left, right = CODE_BOXES['L'][0] + k, CODE_BOXES['L'][1] - k
    out = np.minimum(out, from_strokes(u, v, [(left, CODE_CAP), (left, k), (right, k)]))
    left, right = CODE_BOXES['W'][0] + k, CODE_BOXES['W'][1] - k
    w = right - left
    out = np.minimum(out, from_strokes(u, v, [(left, CODE_CAP), (left + .25 * w, .4 * k), (left + .5 * w, .82 * top),
                                              (left + .75 * w, .4 * k), (right, CODE_CAP)]))
    left, right = CODE_BOXES['O'][0] + k, CODE_BOXES['O'][1] - k
    radius = (right - left) / 2
    ring = np.hypot(u - (left + right) / 2, np.maximum(np.abs(v - CODE_CAP / 2) - ((top - k) / 2 - radius), 0))
    return np.minimum(out, np.abs(ring - radius))


def roundel(canvas, where, u, v, radius, clear=0.):
    """The Austrian roundel about (0, 0): a white rim, the red disc, a white triangle with its apex along -v."""
    r = np.hypot(u, v)
    a, b = -v / radius, u / radius
    triangle = np.minimum(a + .43, (.86 - a) / 2 - np.abs(b) * math.sqrt(3) / 2) * radius
    if clear:
        canvas = over(canvas, BLACK, np.where(where, inside(r, radius + clear), 0))
    disc = np.where(where, inside(r, radius * .93), 0)
    canvas = over(over(canvas, WHITE, np.where(where, inside(r, radius), 0)), BAND_RED_PAINT, disc)
    return over(canvas, WHITE, np.where(where, inside(-triangle, 0), 0) * disc)


# The code reads from the nose on the port side and toward it on the starboard.
side = (np.abs(NY) > .25) & (AY > 4) & (X > 28) & (X < 50) & (Z > 0) & (Z < 5.6) & fuselage
reads = np.where(Y > 0, CODE_CENTRE - X, X - CODE_CENTRE)
canvas = roundel(canvas, side, reads - CODE_ROUNDEL[0], Z - CODE_BASE - CODE_ROUNDEL[1], CODE_ROUNDEL[2])
canvas = over(canvas, INK, np.where(side, inside(from_code(reads, Z - CODE_BASE), CODE_STROKE), 0))

# The wing roundel, where its red is in the photograph.
is_red = canvas_of(direct & (np.maximum(sampled(redness, near_x, near_y), sampled(opened_redness, near_x, near_y)) > .5),
                   False, bool) & (Y < 0) & (NZ > .9) & (across_wing < -3.5 / np.hypot(1, BAND[1]))
cx, cy = WING_ROUNDEL
for _ in range(3):
    found = is_red & (np.hypot(X - cx, Y - cy) < 3.4)
    (x0, x1), (y0, y1) = np.percentile(X[found], [1, 99]), np.percentile(Y[found], [1, 99])
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
radius = 1.03 * ((x1 - x0) + (y1 - y0)) / 4
canvas = roundel(canvas, (Y < 0) & (NZ > .8) & (np.hypot(X - cx, Y - cy) < radius + 3), Y - cy, X - cx, radius, clear=.3)

# --- The pictures: eyes, arms and fin ---------------------------------------------
enlarged = photo.resize((WIDTH * 3, HEIGHT * 3), Image.LANCZOS).filter(ImageFilter.UnsharpMask(4, 60, 2))
enlarged = linear(np.asarray(enlarged).astype(np.float32))
NEAR_X, NEAR_Y, FAR_X, FAR_Y = canvas_of(near_x, -1), canvas_of(near_y, -1), canvas_of(far_x, -1), canvas_of(far_y, -1)
NEAR_VIEW, FAR_VIEW = canvas_of(near_view, False, bool), canvas_of(far_view, False, bool)
NEAR_TOWARD, FAR_TOWARD = canvas_of(near_toward), canvas_of(far_toward)
# Both pictures are symmetrical and are seen whole only to port, so the port
# half serves both sides.
PORT = Y >= -.25
SEEN_X, SEEN_Y = np.where(PORT, NEAR_X, FAR_X), np.where(PORT, NEAR_Y, FAR_Y)


def copied(zone):
    """The photograph over a zone, with its haze taken off."""
    out = np.zeros((SIZE, SIZE, 3), np.float32)
    out[zone] = sampled(enlarged, SEEN_X[zone] * 3, SEEN_Y[zone] * 3)
    return np.clip((out - .035) / (.93 - .035), 0, 1)


# Eyes on the foreplanes, which run in to the fuselage side: their upper skins
# are picked by triangle, since no box around them misses the fuselage.
upper = (O == AIRFRAME) & (N[:, 2] > .15) & (P[:, :, 0].min(1) > 62) & (P[:, :, 0].max(1) < 84) & \
    (np.abs(P[:, :, 1]).max(1) > 10.5) & (np.abs(P[:, :, 1]).min(1) > 5) & (P[:, :, 2].min(1) > -3.5) & \
    (P[:, :, 2].max(1) < 1.8)
_, foreplane = rasterise(U[upper] * SIZE, np.ones((int(upper.sum()), 3), np.float32), SIZE, SIZE, tolerance=.02)
foreplane = PAINTED & (foreplane >= 0)
eyes = copied(foreplane)
# Texels that land beyond the photographed outline take their neighbours' paint.
on_aircraft = np.zeros((SIZE, SIZE), bool)
on_aircraft[foreplane] = well_inside[np.clip(SEEN_Y[foreplane].astype(int), 0, HEIGHT - 1),
                                     np.clip(SEEN_X[foreplane].astype(int), 0, WIDTH - 1)]
_, fill = ndi.distance_transform_edt(~on_aircraft, return_indices=True)
eyes = np.where((foreplane & ~on_aircraft)[..., None], eyes[fill[0], fill[1]], eyes)
# The photograph's highlights wash the iris out to pink: its red is put back
# and the whites and the dark outlines are kept.
level = (eyes @ WEIGHTS)[..., None]
warm = smoothstep(.12, .35, (eyes[..., 0] - np.maximum(eyes[..., 1], eyes[..., 2])) / np.maximum(eyes[..., 0], 1e-3))
iris = np.clip(eyes[..., :1] * .95, 0, .8) * np.array((1., .06, .08), np.float32)
eyes = np.clip(level + (eyes - level) * 1.15, 0, 1)
eyes = np.clip(over(eyes, iris, warm) * 1.02 - .015, 0, 1)
canvas = np.where(foreplane[..., None], eyes, canvas)

# The arms on the spine: a torn red-white-red flag. Too small in the photograph
# to copy, so it is redrawn in its three paints.
zone = PAINTED & np.where(PORT, NEAR_VIEW & (NEAR_TOWARD > .06), FAR_VIEW & (FAR_TOWARD > .06)) & \
    (SEEN_X > ARMS[0]) & (SEEN_X < ARMS[2]) & (SEEN_Y > ARMS[1]) & (SEEN_Y < ARMS[3]) & (AY < 6) & (NZ > .3)
arms = copied(zone)


def within_zone(field, sigma):
    return ndi.gaussian_filter(np.where(zone, field, 0), sigma) / \
        np.maximum(ndi.gaussian_filter(zone.astype(np.float32), sigma), 1e-3)


arms_light = within_zone(arms @ WEIGHTS, 2.)
arms_red = within_zone((arms[..., 0] - np.maximum(arms[..., 1], arms[..., 2])) / np.maximum(arms[..., 0], 1e-3), 2.)
flag = ndi.binary_opening(zone & ((arms_light > .42) | (arms_red > .35)), disk(4))
label, count = ndi.label(flag)
flag = label == ndi.sum(flag, label, np.arange(count + 1)).argmax()
flag = remove_small_holes(ndi.binary_closing(flag, disk(7)), max_size=6000)
is_red = smoothstep(.22, .4, arms_red)
paints = over(over(np.broadcast_to(BLACK, (SIZE, SIZE, 3)), BAND_RED_PAINT, is_red),
              WHITE, smoothstep(.38, .55, arms_light) * (1 - is_red))
x0, x1 = np.percentile(X[flag], [.5, 99.5])
around = PAINTED & (AY < 6.2) & (NZ > .3) & (X > x0 - 1.5) & (X < x1 + 1.5)
canvas = over(canvas, BLACK, ndi.gaussian_filter(around.astype(np.float32), 2) * around)
canvas = over(canvas, paints, smoothstep(.35, .65, ndi.gaussian_filter(flag.astype(np.float32), 2)))

# The fin, from the port quarter: both faces take the port one.
quarter = Image.open(options.rear).convert('RGB')
if quarter.width != REAR_WIDTH:
    quarter = quarter.resize((REAR_WIDTH, round(quarter.height * REAR_WIDTH / quarter.width)), Image.LANCZOS)
low_x, low_y = (int(v) - 60 for v in FIN_PHOTO.min(0))
high_x, high_y = (int(v) + 60 for v in FIN_PHOTO.max(0))
quarter = quarter.crop((low_x, low_y, high_x, high_y))
quarter = quarter.resize((quarter.width * 4, quarter.height * 4), Image.BICUBIC) \
    .filter(ImageFilter.GaussianBlur(1.2)).filter(ImageFilter.UnsharpMask(6, 90, 2))
quarter = np.asarray(quarter).astype(np.float32)
system, target = [], []
for (mx, mz), (qx, qy) in zip(FIN_MODEL, FIN_PHOTO):
    system += [[mx, mz, 1, 0, 0, 0, -qx * mx, -qx * mz], [0, 0, 0, mx, mz, 1, -qy * mx, -qy * mz]]
    target += [qx, qy]
h = np.append(np.linalg.solve(np.array(system), np.array(target)), 1).reshape(3, 3)
leading = -11.7 - 1.395 * (Z - 12)
trailing = -41.6 - .495 * (Z - 12)
fin = PAINTED & (np.abs(NY) > .6) & (AY < 2.5) & (Z > 11.5) & (X < leading + 1) & (X > trailing - 1)
# On the starboard face the lettering is turned back about the strip's middle.
from_trailing = X - trailing
lettering = (Y < 0) & (from_trailing > FIN_TEXT[0]) & (from_trailing < FIN_TEXT[1]) & (Z < FIN_TEXT_TOP)
fin_x = np.where(lettering, trailing + FIN_TEXT[0] + FIN_TEXT[1] - from_trailing, X)[fin]
mapped = np.stack([fin_x, Z[fin], np.ones(int(fin.sum()))], 1).astype(np.float64) @ h.T
fin_seen = sampled(quarter, (mapped[:, 0] / mapped[:, 2] - low_x) * 4, (mapped[:, 1] / mapped[:, 2] - low_y) * 4)
# Levels from the fin itself: its black is hazed blue, its white is the tiger's.
low = np.percentile(fin_seen, 1.5, axis=0)
brightness = fin_seen @ WEIGHTS
high = fin_seen[brightness >= np.percentile(brightness, 99.3)].mean(0)
fin_seen = linear(np.clip((fin_seen - low) / (high - low), 0, 1) * 255)
# Paint, not a photograph of it: the greys go neutral and the reds to the scheme's red.
fin_red = smoothstep(.18, .45, np.clip((fin_seen[:, 0] - np.maximum(fin_seen[:, 1], fin_seen[:, 2])) /
                                       np.maximum(fin_seen[:, 0], 1e-3), 0, 1))[:, None]
fin_grey = (np.clip((fin_seen @ WEIGHTS - .02) * 1.25, 0, 1) ** 1.15)[:, None] * np.array((1., 1., .98), np.float32)
fin_paint = fin_grey * (1 - fin_red) + np.clip(fin_seen[:, :1] * 1.25, 0, .75) * np.array((1., .03, .055)) * fin_red
fin_paint *= smoothstep(FIN_FLOOR[0], FIN_FLOOR[1], Z[fin])[:, None]
canvas[fin] = np.maximum(fin_paint, BLACK)

# --- Written out ------------------------------------------------------------------
alpha = PAINTED.copy()
# A few texels of bleed into the unused gaps between islands.
gap, nearest = ndi.distance_transform_edt(~alpha, return_indices=True)
bleed = ~covered & (gap <= max(3, SIZE // 700))
canvas[bleed] = canvas[nearest[0][bleed], nearest[1][bleed]]
alpha |= bleed

OUTPUT.parent.mkdir(parents=True, exist_ok=True)
rgba = np.concatenate([encoded(canvas), (alpha * 255).astype(np.uint8)[..., None]], axis=2)
rgba[~alpha] = 0
Image.fromarray(rgba[::-1], 'RGBA').save(OUTPUT, optimize=True)
print(f'TYPHOON LIVERY {OUTPUT} painted={alpha.mean():.3f} from_above={(direct & painted).sum() / painted.sum():.3f} '
      f'mirrored={(mirrored & painted).sum() / painted.sum():.3f} fin={int(fin.sum())} '
      f'wing_roundel=({cx:.2f}, {cy:.2f}) r={radius:.2f}')

if options.preview:
    # Check renders: the donor in the new paint through the photograph's camera
    # beside the photograph, and from four plain directions.
    options.preview.mkdir(parents=True, exist_ok=True)
    base = Image.open(options.dump / 'basecolor.png').convert('RGB')
    if base.size != (SIZE, SIZE):
        base = base.resize((SIZE, SIZE), Image.LANCZOS)
    texture = linear(np.asarray(base).astype(np.float32))[::-1].copy()
    texture[alpha] = canvas[alpha]
    drawn = np.nonzero(np.isin(O, [AIRFRAME, CANOPY, names.index('Cockpit'), names.index('WeaponRails')]))[0]

    def render(turn_, shift, focal, width, height):
        camera = P[drawn].astype(np.float64) @ turn_.T + shift
        corners = np.stack([focal * camera[..., 0] / camera[..., 2] + width / 2,
                            focal * camera[..., 1] / camera[..., 2] + height / 2], -1)
        _, owner = rasterise(corners, camera[..., 2], width, height)
        out = np.broadcast_to(np.array((.25, .33, .42), np.float32), (height, width, 3)).copy()
        ys, xs = np.nonzero(owner >= 0)
        k = owner[ys, xs]
        c = corners[k]
        sx, sy = xs + .5, ys + .5
        d = (c[:, 1, 1] - c[:, 2, 1]) * (c[:, 0, 0] - c[:, 2, 0]) + (c[:, 2, 0] - c[:, 1, 0]) * (c[:, 0, 1] - c[:, 2, 1])
        a = ((c[:, 1, 1] - c[:, 2, 1]) * (sx - c[:, 2, 0]) + (c[:, 2, 0] - c[:, 1, 0]) * (sy - c[:, 2, 1])) / d
        b = ((c[:, 2, 1] - c[:, 0, 1]) * (sx - c[:, 2, 0]) + (c[:, 0, 0] - c[:, 2, 0]) * (sy - c[:, 2, 1])) / d
        inverse_depth = 1 / camera[k][..., 2]
        weights = np.stack([a, b, 1 - a - b], 1) * inverse_depth
        uv = (weights[:, :, None] * U[drawn][k]).sum(1) / weights.sum(1)[:, None]
        shade = texture[np.clip((uv[:, 1] * SIZE).astype(int), 0, SIZE - 1),
                        np.clip((uv[:, 0] * SIZE).astype(int), 0, SIZE - 1)].copy()
        shade[O[drawn][k] == CANOPY] = (.03, .04, .05)
        sun = np.array((.3, .2, .93)) / np.linalg.norm((.3, .2, .93))
        out[ys, xs] = shade * (.55 + .45 * np.abs(N[drawn][k] @ sun))[:, None]
        return encoded(out)

    def look(eye_, target, up):
        forward = np.array(target, float) - np.array(eye_, float)
        forward /= np.linalg.norm(forward)
        right = np.cross(forward, up)
        right /= np.linalg.norm(right)
        frame = np.stack([right, np.cross(forward, right), forward])
        return frame, -frame @ np.array(eye_, float)

    matched = render(turn, POSE[3:6], POSE[6], WIDTH, HEIGHT)
    Image.fromarray(np.concatenate([matched, raw.astype(np.uint8)], 1)).save(options.preview / 'check_matched.png')
    Image.fromarray(matched).save(options.preview / 'check_matched_only.png')
    views = {'above': ((27, 0, 900), (27, 0, 0), (1, 0, 0), 900), 'below': ((27, 0, -900), (27, 0, 0), (1, 0, 0), 900),
             'port': ((27, 900, 10), (27, 0, 10), (0, 0, 1), 560), 'starboard': ((27, -900, 10), (27, 0, 10), (0, 0, 1), 560)}
    for name, (eye_, target, up, height) in views.items():
        frame, shift = look(eye_, target, up)
        Image.fromarray(render(frame, shift, 900 * 8.2, 1500, height)).save(options.preview / f'check_{name}.png')
