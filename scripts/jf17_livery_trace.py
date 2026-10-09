"""Trace the JF-17 display livery from a reference photograph into a plan map.

The Pakistan Air Force's navy, grey and green JF-17 display scheme is traced
from one overhead photograph. The photograph is not part of the repository; the
flat-colour plan this writes, assets/aircraft/jf17/livery_plan.png, is what
scripts/jf17_donor_import.py paints from, and is all that is kept.

Run after a first import has written the working model:

  blender -b --factory-startup --disable-autoexec \
    --python scripts/jf17_livery_trace.py -- --photo /path/photo.png

The photograph's camera is recovered from points that can be found on both the
aircraft and the model (launch-rail ends, flap and tailplane corners, the nose).
Each centimetre of the model's plan, at the height of its upper skin, is then
looked up in the photograph and sorted into one of the scheme's five paints.
Only the port side is read, which the photograph shows unobstructed, and the
scheme is mirrored.
"""
import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

parser = argparse.ArgumentParser()
parser.add_argument('--project-root', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--photo', type=Path, required=True)
parser.add_argument('--model', type=Path)
parser.add_argument('--output', type=Path)
options = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
ROOT = options.project_root.resolve()
MODEL = (options.model or ROOT / 'output/JF17_Thunder_donor.blend').resolve()
OUTPUT = (options.output or ROOT / 'assets/aircraft/jf17/livery_plan.png').resolve()

# Model point in authoring metres (x aft of the nose, y starboard, z up) and
# the pixel where the same point lies in the 1567 x 1254 reference photograph.
LANDMARKS = (
    ((8.892, -4.786, 1.52), (267, 510)), ((11.594, -4.637, 1.63), (183, 760)),      # Port launch rail.
    ((8.892, 4.786, 1.52), (1303, 510)), ((11.594, 4.637, 1.63), (1410, 750)),      # Starboard launch rail.
    ((12.303, -1.285, 1.63), (623, 840)), ((12.303, 1.285, 1.63), (990, 843)),      # Tailplane roots, leading edge.
    ((14.591, -1.285, 1.63), (607, 1180)), ((14.591, 1.285, 1.63), (1033, 1170)),   # Tailplane roots, trailing edge.
    ((11.622, -3.076, 1.74), (403, 743)), ((11.622, 3.076, 1.74), (1193, 737)),     # Flap and aileron meet.
    ((11.668, -1.294, 1.76), (630, 743)), ((11.668, 1.294, 1.76), (967, 740)),      # Flap roots.
    ((0, 0, 1.41), (763, 63)))                                                       # Nose.
PHOTO_SIZE = (1567, 1254)
CELL = .01                      # Metres per plan pixel.
LENGTH, HALF_SPAN = 15.0, 5.0   # Plan covered: nose to tail, centreline to tip.
# The scheme's paints as the photograph shows them, sRGB, and their index.
PAINTS = {'navy': (38, 54, 90), 'grey': (172, 203, 212), 'cream': (238, 236, 214), 'green': (62, 160, 84)}
NAMES = list(PAINTS)
UNKNOWN = 255


def rotation(vector):
    angle = np.linalg.norm(vector)
    if angle < 1e-12:
        return np.eye(3)
    k = vector / angle
    cross = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return np.eye(3) + math.sin(angle) * cross + (1 - math.cos(angle)) * cross @ cross


def project(camera, points):
    """Pinhole camera: focal length, rotation vector, translation, centre."""
    seen = points @ rotation(camera[1:4]).T + camera[4:7]
    return np.stack([camera[0] * seen[..., 0] / seen[..., 2] + camera[7],
                     camera[0] * seen[..., 1] / seen[..., 2] + camera[8]], -1)


def fit_camera():
    world = np.array([p for p, _ in LANDMARKS], float)
    pixel = np.array([q for _, q in LANDMARKS], float)

    def residual(camera):
        centre = (camera[7:9] - np.array(PHOTO_SIZE) * .5) * .05  # The centre stays near the middle.
        return np.concatenate([(project(camera, world) - pixel).ravel(), centre])

    def solve(start):  # Levenberg-Marquardt with a numerical Jacobian.
        x, damping = np.array(start, float), 1e-2
        r = residual(x)
        cost = .5 * r @ r
        for _ in range(300):
            jacobian = np.empty((len(r), len(x)))
            for i in range(len(x)):
                step = 1e-6 * max(1, abs(x[i]))
                moved = x.copy()
                moved[i] += step
                jacobian[:, i] = (residual(moved) - r) / step
            normal = jacobian.T @ jacobian
            try:
                delta = np.linalg.solve(normal + damping * np.diag(np.diag(normal) + 1e-9), -jacobian.T @ r)
            except np.linalg.LinAlgError:
                damping *= 10
                continue
            trial = residual(x + delta)
            trial_cost = .5 * trial @ trial
            if np.isfinite(trial_cost) and trial_cost < cost:
                finished = cost - trial_cost < 1e-10 * cost
                x, r, cost, damping = x + delta, trial, trial_cost, max(damping / 3, 1e-9)
                if finished:
                    break
            else:
                damping *= 4
                if damping > 1e12:
                    break
        return x, cost

    best = None
    for behind in (14, 18, 22, 28):          # Starting points: above the tail, looking forward and down.
        for above in (8, 12, 18, 30):
            eye, target = np.array([behind, 0, above], float), np.array([8.5, 0, 1.6])
            forward = (target - eye) / np.linalg.norm(target - eye)
            right = np.array([0, 1., 0])
            frame = np.stack([right, np.cross(forward, right), forward])
            angle = math.acos(max(-1, min(1, (np.trace(frame) - 1) / 2)))
            axis = np.array([frame[2, 1] - frame[1, 2], frame[0, 2] - frame[2, 0], frame[1, 0] - frame[0, 1]])
            for focal in (700, 1200, 2000):
                start = np.concatenate([[focal], axis / (2 * math.sin(angle)) * angle, -frame @ eye,
                                        np.array(PHOTO_SIZE) * .5])
                found = solve(start)
                if best is None or found[1] < best[1]:
                    best = found
    camera = best[0]
    error = np.linalg.norm(project(camera, world) - pixel, axis=1)
    print('Camera fit: worst landmark %.1f px, mean %.1f px' % (error.max(), error.mean()))
    if error.max() > 20:
        raise RuntimeError('Reference photograph does not fit the model landmarks')
    return camera


# --- Upper skin height over the port half of the plan ------------------------
bpy.ops.wm.open_mainfile(filepath=str(MODEL))
shell = bmesh.new()
for obj in bpy.data.objects:
    if obj.type == 'MESH' and obj.name.startswith('JF-17 | ') and 'wheel' not in obj.name and 'leg' not in obj.name \
            and 'door' not in obj.name and 'canopy' not in obj.name and 'windscreen' not in obj.name:
        mesh = obj.data.copy()
        mesh.transform(obj.matrix_world)
        shell.from_mesh(mesh)
        bpy.data.meshes.remove(mesh)
tree = BVHTree.FromBMesh(shell)
ROWS, COLUMNS = int(HALF_SPAN / CELL), int(LENGTH / CELL)
STEP = 2  # Heights are cast every other cell and repeated.
height = np.full((ROWS, COLUMNS), np.nan, np.float32)
down = Vector((0, 0, -1))
for row in range(0, ROWS, STEP):
    y = -(row + .5 * STEP) * CELL
    for column in range(0, COLUMNS, STEP):
        hit = tree.ray_cast(Vector(((column + .5 * STEP) * CELL, y, 30)), down)[0]
        if hit:
            height[row:row + STEP, column:column + STEP] = hit.z
shell.free()

# --- Read the photograph ------------------------------------------------------
photo = bpy.data.images.load(str(options.photo.resolve()))
if tuple(photo.size) != PHOTO_SIZE:
    raise RuntimeError('The landmarks are measured on the 1567 x 1254 reference photograph')
photo.colorspace_settings.name = 'Non-Color'  # Keep the stored sRGB values.
data = np.empty(PHOTO_SIZE[0] * PHOTO_SIZE[1] * 4, np.float32)
photo.pixels.foreach_get(data)
rgb = data.reshape(PHOTO_SIZE[1], PHOTO_SIZE[0], 4)[::-1, :, :3] * 255  # Top row first.
camera = fit_camera()
columns, rows = np.meshgrid(np.arange(COLUMNS), np.arange(ROWS))
known = np.isfinite(height)
world = np.stack([(columns + .5) * CELL, -(rows + .5) * CELL, np.where(known, height, 0)], -1).astype(np.float64)
where = project(camera, world)
u = np.clip(np.rint(where[..., 0]).astype(int), 0, PHOTO_SIZE[0] - 1)
v = np.clip(np.rint(where[..., 1]).astype(int), 0, PHOTO_SIZE[1] - 1)
inside = known & (where[..., 0] >= 0) & (where[..., 0] < PHOTO_SIZE[0]) & (where[..., 1] >= 0) & (where[..., 1] < PHOTO_SIZE[1])
# A 3 x 3 mean takes out stencils and rivets before the paints are told apart.
soft = sum(np.roll(np.roll(rgb, dy, 0), dx, 1) for dy in (-1, 0, 1) for dx in (-1, 0, 1)) / 9
sample = soft[v, u]
red, green, blue = sample[..., 0], sample[..., 1], sample[..., 2]
value = sample.max(-1)
paint = np.full((ROWS, COLUMNS), UNKNOWN, np.uint8)
distance = np.stack([np.linalg.norm(sample - np.array(PAINTS[name], np.float32), axis=-1) for name in NAMES], -1)
nearest = distance.argmin(-1)
# Concrete and shadow are warm or neutral; every paint but cream is cool.
cool = blue > red + 8
is_green = (green > red + 30) & (green > blue + 12)
# Cream is bright and neutral to warm; the grey is as bright in places but always bluer.
is_cream = (value > 212) & (green > 205) & (blue - red < 14) & (red - blue < 32)
valid = inside & (is_green | is_cream | (cool & (distance.min(-1) < 75)))
paint[valid] = nearest[valid]
paint[inside & is_green] = NAMES.index('green')
paint[inside & is_cream & ~is_green] = NAMES.index('cream')
paint[valid & ~is_cream & ~is_green & (nearest == NAMES.index('cream'))] = NAMES.index('grey')
x_plan, y_plan = (columns + .5) * CELL, (rows + .5) * CELL
# The fin and its fairing stand over the spine in the photograph.
paint[(y_plan < .16) & (x_plan > 9.6)] = NAMES.index('navy')
# The canopy glazing reads as dark as the navy; the skin beside it is grey.
paint[(x_plan > 1.6) & (x_plan < 5.25) & (y_plan < .8)] = NAMES.index('grey')
paint[~known] = UNKNOWN


def box(values, radius):
    out = values.astype(np.float32)
    for axis in (0, 1):
        total = np.cumsum(np.concatenate([np.zeros_like(out.take([0], axis=axis)), out], axis=axis), axis=axis)
        index = np.arange(out.shape[axis])
        upper, lower = np.clip(index + radius + 1, 0, out.shape[axis]), np.clip(index - radius, 0, out.shape[axis])
        out = total.take(upper, axis=axis) - total.take(lower, axis=axis)
    return out


def majority(classes, radius):
    votes = np.stack([box(classes == index, radius) for index in range(len(NAMES))], -1)
    result = votes.argmax(-1).astype(np.uint8)
    result[votes.sum(-1) == 0] = UNKNOWN
    return result


cleaned = majority(paint, 2)           # Specks go; unknown cells next to paint take it.
for _ in range(400):                     # Then paint spreads into everything still unknown.
    unknown = cleaned == UNKNOWN
    if not unknown.any():
        break
    grown = majority(cleaned, 3)
    cleaned[unknown] = grown[unknown]
cleaned[cleaned == UNKNOWN] = NAMES.index('grey')
# The roundel's white disc reads as grey in the photograph: grey shut in by
# green on all four sides within 30 cm is the disc.
ring = cleaned == NAMES.index('green')
reach = int(.3 / CELL)
shut = np.ones_like(ring)
for axis, direction in ((0, 1), (0, -1), (1, 1), (1, -1)):
    near = np.zeros_like(ring)
    for shift in range(1, reach + 1):
        near |= np.roll(ring, direction * shift, axis=axis)
    shut &= near
cleaned[shut & (cleaned == NAMES.index('grey'))] = NAMES.index('cream')
print('Plan share: ' + ', '.join('%s %.0f%%' % (name, 100 * (cleaned[known] == i).mean()) for i, name in enumerate(NAMES)))

# One row per centimetre of span from the centreline, one column per centimetre aft.
image = bpy.data.images.new('JF-17 livery plan', COLUMNS, ROWS, alpha=False)
image.colorspace_settings.name = 'Non-Color'
palette = np.array([PAINTS[name] for name in NAMES], np.float32) / 255
flat = np.concatenate([palette[cleaned], np.ones((ROWS, COLUMNS, 1), np.float32)], -1)[::-1]
image.pixels.foreach_set(flat.reshape(-1))
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
image.filepath_raw = str(OUTPUT)
image.file_format = 'PNG'
image.save()
print('Wrote', OUTPUT)
