"""Import, repaint, rig and export the JF-17 Thunder donor model as four native GLBs.

Donor: "JF-17" by dimal965 (Sketchfab b57660f346314df1877e15b85d6e74be),
CC BY 4.0. Credit and the list of changes ship in licenses/assets/JF17.md.

Run headless, never from inside the extracted donor directory:

  blender -b --factory-startup --disable-autoexec \
    --python scripts/jf17_donor_import.py -- --source /path/source/JF-17_4.fbx

The textures are read from the archive's `textures` folder beside `source`.
The donor is close to metric with nose +X, port +Y and up +Z, stands on its
undercarriage with the canopy raised, and keeps its control surfaces, doors
and legs as separate objects. This stage closes the canopy, rounds the low
polygon skin, paints the Pakistan Air Force display livery over the donor's
grey scheme and groups the moving parts under OpenFlightSim pivots.
"""
import argparse
import json
import math
import sys
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

parser = argparse.ArgumentParser()
parser.add_argument('--project-root', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--working-output', type=Path)
parser.add_argument('--texture-size', type=int, default=4096)
options = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
ROOT = options.project_root.resolve()
SOURCE = options.source.resolve()
TEXTURES = SOURCE.parent.parent / 'textures'
EXPORT_DIR = (options.output_dir or ROOT / 'assets/aircraft/jf17').resolve()
WORKING_PATH = (options.working_output or ROOT / 'output/JF17_Thunder_donor.blend').resolve()
EXPORT_DIR.mkdir(parents=True, exist_ok=True)
WORKING_PATH.parent.mkdir(parents=True, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(SOURCE))
scene = bpy.context.scene
scene.name = 'JF-17 | donor rig'
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1
bpy.context.view_layer.update()

LENGTH_M = 14.93  # Published overall length; the only scale anchor.
EXPECTED = {'body', 'glass'} | {f'Object{n:03d}' for n in (23, 24, *range(26, 45))}
DONOR = {o.name: o for o in bpy.data.objects if o.type == 'MESH'}
if set(DONOR) != EXPECTED:
    raise RuntimeError(f'Donor layout changed: {sorted(set(DONOR) ^ EXPECTED)}')
# Every donor object rests at a half turn about the vertical; whatever a door
# or the canopy has beyond that is how far it stands open about its origin.
REST = Matrix.Rotation(math.pi, 3, 'Z')
OPENING = {}
for name, obj in DONOR.items():
    hinge_object = obj.parent if obj.parent and obj.parent.type == 'MESH' else obj
    world = hinge_object.matrix_world
    OPENING[name] = (world.translation.copy(), world.to_3x3().normalized() @ REST.inverted())
for obj in DONOR.values():  # Bake to world coordinates, donor units.
    world = obj.matrix_world.copy()
    obj.parent = None
    obj.animation_data_clear()
    obj.data.transform(world)
    obj.matrix_world = Matrix.Identity(4)
    if world.determinant() < 0:
        obj.data.flip_normals()
bpy.context.view_layer.update()


def closing(name):  # Donor-space transform that shuts a door or the canopy.
    origin, opened = OPENING[name]
    return Matrix.Translation(origin) @ opened.inverted().to_4x4() @ Matrix.Translation(-origin)


for name in ('Object026', 'Object033'):  # Canopy frame and glazing.
    DONOR[name].data.transform(closing(name))


def points(name):
    return [v.co.copy() for v in DONOR[name].data.vertices]


def bounds(pts):
    return (Vector([min(p[i] for p in pts) for i in range(3)]), Vector([max(p[i] for p in pts) for i in range(3)]))


everything = [p for name in DONOR for p in points(name)]
low, high = bounds(everything)
SCALE = LENGTH_M / (high.x - low.x)
# Donor to authoring metres: X aft from the nose, Y starboard, Z up from the ground.
FRAME = Matrix(((-SCALE, 0, 0, high.x * SCALE), (0, -SCALE, 0, 0), (0, 0, SCALE, -low.z * SCALE), (0, 0, 0, 1)))


def D(vector):  # Donor direction to authoring direction.
    v = Vector(vector)
    return Vector((-v.x, -v.y, v.z))


def gltf(vector):  # Blender Z-up to exported Y-up.
    return (vector.x, vector.z, -vector.y)


for obj in DONOR.values():
    obj.data.transform(FRAME)
    obj.data.update()


def shells(bm):
    seen = set()
    for seed in bm.verts:
        if seed.index in seen:
            continue
        stack, shell = [seed], []
        seen.add(seed.index)
        while stack:
            vert = stack.pop()
            shell.append(vert)
            for edge in vert.link_edges:
                other = edge.other_vert(vert)
                if other.index not in seen:
                    seen.add(other.index)
                    stack.append(other)
        yield shell


def split(name, pick, label):
    """Move the loose shells that `pick` accepts into a new object."""
    obj = DONOR[name]
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.verts.ensure_lookup_table()
    indices = set()
    for shell in shells(bm):
        lo, hi = bounds([v.co for v in shell])
        if pick(lo, hi):
            indices |= {v.index for v in shell}
    if not indices or len(indices) == len(bm.verts):
        raise RuntimeError(f'Donor layout changed: cannot split {label} from {name}')
    keep = bm.copy()
    keep.verts.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.index in indices], context='VERTS')
    bmesh.ops.delete(keep, geom=[v for v in keep.verts if v.index not in indices], context='VERTS')
    bm.to_mesh(obj.data)
    bm.free()
    data = obj.data.copy()
    keep.to_mesh(data)
    keep.free()
    part = bpy.data.objects.new(label, data)
    scene.collection.objects.link(part)
    DONOR[label] = part
    return part


# Undercarriage: one object holds both main legs, another the nose leg.
split('Object023', lambda lo, hi: (lo.y + hi.y) > 0, 'main_R')
DONOR['main_L'] = DONOR.pop('Object023')
DONOR['nose'] = DONOR.pop('Object024')


def tyre_of(name):
    """The tyre is the shell that reaches the ground and is round in side view."""
    best = None
    bm = bmesh.new()
    bm.from_mesh(DONOR[name].data)
    for shell in shells(bm):
        lo, hi = bounds([v.co for v in shell])
        size = hi - lo
        if lo.z < .2 and abs(size.x - size.z) < .12 * size.z and (best is None or size.z > best[1].z - best[0].z):
            best = (lo, hi)
    bm.free()
    if best is None:
        raise RuntimeError(f'Donor layout changed: no tyre in {name}')
    return best


WHEEL, STANCE = {}, {}
for leg, label in (('main_L', 'wheel_L'), ('main_R', 'wheel_R'), ('nose', 'wheel_nose')):
    lo, hi = tyre_of(leg)
    # The donor's main tyres hang clear of the plane its nose tyre stands on.
    # Each leg is lowered so all three share it.
    STANCE[leg] = lo.z
    DONOR[leg].data.transform(Matrix.Translation((0, 0, -lo.z)))
    lo, hi = lo - Vector((0, 0, lo.z)), hi - Vector((0, 0, lo.z))
    # Hub, brake and tyre: every shell that lies inside the tyre's disc.
    split(leg, lambda a, b: a.x > lo.x - .02 and b.x < hi.x + .02 and a.z > lo.z - .02 and b.z < hi.z + .02 and
          a.y > lo.y - .12 and b.y < hi.y + .12, label)
    WHEEL[label] = ((lo + hi) * .5, (hi.z - lo.z) * .5)

# --- Surface refinement ------------------------------------------------------
SMOOTH = ('body', 'Object026', 'wheel_L', 'wheel_R', 'wheel_nose')
SHARP = math.radians(42)


def refine(obj):
    """Round the faceted skin while holding every hard edge and UV border."""
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    crease = bm.edges.layers.float.get('crease_edge') or bm.edges.layers.float.new('crease_edge')
    for edge in bm.edges:
        hard = len(edge.link_faces) != 2 or edge.calc_face_angle(0) > SHARP
        edge[crease] = 1 if hard else 0
        edge.smooth = not hard
    bm.to_mesh(obj.data)
    bm.free()
    modifier = obj.modifiers.new('OFS skin refinement', 'SUBSURF')
    modifier.levels = modifier.render_levels = 1
    modifier.uv_smooth = 'PRESERVE_BOUNDARIES'
    modifier.boundary_smooth = 'PRESERVE_CORNERS'
    bpy.context.view_layer.objects.active = obj
    with bpy.context.temp_override(object=obj, active_object=obj, selected_objects=[obj]):
        bpy.ops.object.modifier_apply(modifier=modifier.name)


for obj in DONOR.values():
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
for name in SMOOTH:
    refine(DONOR[name])

# --- Livery -------------------------------------------------------------------
SIZE = options.texture_size


def load(name, colour=True):
    path = TEXTURES / name
    if not path.is_file():
        raise RuntimeError(f'Donor texture missing: {name}')
    image = bpy.data.images.load(str(path))
    if not colour:
        image.colorspace_settings.name = 'Non-Color'
    return image


def pixels(image):  # Rows from the bottom, as UV space runs.
    data = np.empty(image.size[0] * image.size[1] * 4, dtype=np.float32)
    image.pixels.foreach_get(data)
    return data.reshape(image.size[1], image.size[0], 4)


base_image = load('JF-17_3_JF-17_BaseColor.png')
if tuple(base_image.size) != (SIZE, SIZE):
    base_image.scale(SIZE, SIZE)
original = pixels(base_image)[:, :, :3].copy()  # Linear RGB.

# The scheme's paints, linear RGB, matched to the photograph in daylight.
NAVY = np.array((.042, .080, .190), np.float32)
GREY = np.array((.48, .66, .72), np.float32)
CREAM = np.array((.88, .86, .70), np.float32)
GREEN = np.array((.050, .38, .100), np.float32)


# Plan-view artwork: assets/aircraft/jf17/livery_plan.png, traced from a
# photograph of the Pakistan Air Force's navy, grey and green display scheme by
# scripts/jf17_livery_trace.py. One pixel per centimetre: columns run aft from
# the nose, rows out from the centreline, and both sides are alike.
PLAN_CELL = .01
plan_image = bpy.data.images.load(str(ROOT / 'assets/aircraft/jf17/livery_plan.png'))
plan_image.colorspace_settings.name = 'Non-Color'
plan_pixels = pixels(plan_image)[::-1, :, :3]
PLAN_PAINTS = (((38, 54, 90), NAVY), ((172, 203, 212), GREY), ((238, 236, 214), CREAM), ((62, 160, 84), GREEN))
plan_class = np.stack([np.abs(plan_pixels * 255 - np.array(key, np.float32)).sum(-1) for key, _ in PLAN_PAINTS],
                      -1).argmin(-1)
plan_colour = np.array([value for _, value in PLAN_PAINTS], np.float32)[plan_class]
bpy.data.images.remove(plan_image)


def plan(x, y):
    column = np.clip((x / PLAN_CELL).astype(int), 0, plan_colour.shape[1] - 1)
    row = np.clip((y / PLAN_CELL).astype(int), 0, plan_colour.shape[0] - 1)
    return plan_colour[row, column]


def profile(x, z):  # The fin from the side: navy with a cream, grey and green flash.
    colour = np.broadcast_to(NAVY, x.shape + (3,)).copy()
    band = (x - 12.2) * .64 - (z - 2.9) * .77
    colour[(band > .0) & (band < .34)] = GREY
    colour[(band >= .34) & (band < .44)] = GREEN
    colour[(band > -.10) & (band <= .0)] = CREAM
    return colour


# Rasterise every triangle into UV space with its position and facing.
position = np.zeros((SIZE, SIZE, 3), np.float32)
facing = np.zeros((SIZE, SIZE, 3), np.float32)
covered = np.zeros((SIZE, SIZE), bool)
skin = np.zeros((SIZE, SIZE), bool)
island = np.zeros((SIZE, SIZE), np.int32)  # UV island under each texel, from 1.
islands = 0


def uv_islands(mesh):
    """Island number of every polygon: faces joined across edges whose UVs agree."""
    bm = bmesh.new()
    bm.from_mesh(mesh)
    layer = bm.loops.layers.uv.active
    parent = list(range(len(bm.faces)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for edge in bm.edges:
        if len(edge.link_loops) != 2:
            continue
        a, b = edge.link_loops
        # Loops on either side of an edge run in opposite directions.
        if (a[layer].uv - b.link_loop_next[layer].uv).length < 1e-5 and \
                (a.link_loop_next[layer].uv - b[layer].uv).length < 1e-5:
            parent[find(a.face.index)] = find(b.face.index)
    roots = [find(i) for i in range(len(bm.faces))]
    bm.free()
    return roots


SKIN_OBJECTS = {'body', 'Object026', *(f'Object{n:03d}' for n in range(27, 33)), *(f'Object{n:03d}' for n in range(34, 45))}
for name, obj in DONOR.items():
    mesh = obj.data
    if not mesh.uv_layers or obj.material_slots[0].material.name != 'JF-17':
        continue
    mesh.calc_loop_triangles()
    uv = mesh.uv_layers.active.data
    roots = uv_islands(mesh)
    numbers = {root: islands + 1 + index for index, root in enumerate(sorted(set(roots)))}
    islands += len(numbers)
    for triangle in mesh.loop_triangles:
        t = np.array([uv[i].uv[:] for i in triangle.loops], np.float64) * SIZE
        p = np.array([mesh.vertices[i].co[:] for i in triangle.vertices], np.float32)
        x0, x1 = int(max(0, math.floor(t[:, 0].min()))), int(min(SIZE - 1, math.ceil(t[:, 0].max())))
        y0, y1 = int(max(0, math.floor(t[:, 1].min()))), int(min(SIZE - 1, math.ceil(t[:, 1].max())))
        d = (t[1, 1] - t[2, 1]) * (t[0, 0] - t[2, 0]) + (t[2, 0] - t[1, 0]) * (t[0, 1] - t[2, 1])
        if x1 < x0 or y1 < y0 or abs(d) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + .5, np.arange(y0, y1 + 1) + .5)
        a = ((t[1, 1] - t[2, 1]) * (gx - t[2, 0]) + (t[2, 0] - t[1, 0]) * (gy - t[2, 1])) / d
        b = ((t[2, 1] - t[0, 1]) * (gx - t[2, 0]) + (t[0, 0] - t[2, 0]) * (gy - t[2, 1])) / d
        c = 1 - a - b
        hit = (a >= -.02) & (b >= -.02) & (c >= -.02)
        if not hit.any():
            continue
        rows, cols = np.nonzero(hit)
        where = (rows + y0, cols + x0)
        position[where] = (a[hit, None] * p[0] + b[hit, None] * p[1] + c[hit, None] * p[2]).astype(np.float32)
        facing[where] = np.array(triangle.normal[:], np.float32)
        covered[where] = True
        island[where] = numbers[roots[triangle.polygon_index]]
        if name in SKIN_OBJECTS:
            skin[where] = True


def blur(values, radius):
    out = values.astype(np.float32)
    for axis in (0, 1):
        padded = np.concatenate([np.zeros_like(out.take([0], axis=axis)), np.cumsum(out, axis=axis, dtype=np.float64)],
                                axis=axis)
        index = np.arange(out.shape[axis])
        upper = np.clip(index + radius + 1, 0, out.shape[axis])
        lower = np.clip(index - radius, 0, out.shape[axis])
        out = ((padded.take(upper, axis=axis) - padded.take(lower, axis=axis)) /
               (upper - lower).reshape([-1 if i == axis else 1 for i in range(2)])).astype(np.float32)
    return out


def extreme(values, radius, largest):
    out = values
    for axis in (0, 1):
        stack = [np.roll(out, shift, axis=axis) for shift in range(-radius, radius + 1)]
        out = np.maximum.reduce(stack) if largest else np.minimum.reduce(stack)
    return out


WEIGHTS = np.array((.2126, .7152, .0722), np.float32)
luminance = original @ WEIGHTS
chroma = original.max(axis=2) - original.min(axis=2)
grey_level = float(np.median(luminance[skin]))
# Painted skin is the donor's grey, or one of the markings laid over it: the
# interior, nozzle, wells and fittings are darker, browner or metallic.
# Whole UV islands are repainted, so the donor's roundels, serials and badge go.
greyish = (np.abs(luminance - grey_level) < .35 * grey_level) & (chroma < .08)
texels = np.bincount(island[skin], minlength=islands + 1)
grey_texels = np.bincount(island[skin & greyish], minlength=islands + 1)
painted_islands = (grey_texels > .5 * np.maximum(texels, 1)) & (texels > 0)
painted_islands[0] = False
paint = skin & painted_islands[island]

x, y, z = position[:, :, 0], np.abs(position[:, :, 1]), position[:, :, 2]
# The plan artwork covers what is seen from above: surfaces that face up, and
# the flanks from the wing line upwards. Everything beneath is grey.
WING_LINE = 1.52
seen_from_above = (facing[:, :, 2] > .25) | ((facing[:, :, 2] > -.30) & (z > WING_LINE))
livery = np.where(seen_from_above[:, :, None], plan(x, y), GREY[None, None, :])
fin = (np.abs(facing[:, :, 1]) > .75) & (z > 2.75) & (x > 10.6) & (y < .35)
livery[fin] = profile(x, z)[fin]
livery[(z < 1.15) & (x > 11) & (np.abs(facing[:, :, 1]) > .6)] = GREY   # Ventral fins.

# Carry the donor's panel lines across: thin dark strokes against their
# surroundings. Its own markings are broad and are left behind.
RADIUS = max(2, SIZE // 512)
closed = extreme(extreme(luminance, RADIUS, True), RADIUS, False)
lines = np.clip(luminance / np.maximum(closed, 1e-4), .35, 1)
occlusion = pixels(load('JF-17_3_JF-17_AO.png', colour=False))[:, :, 0]
if occlusion.shape != (SIZE, SIZE):
    raise RuntimeError('Donor texture sizes differ')
result = original.copy()
result[paint] = (livery * lines[:, :, None])[paint]
result *= (.55 + .45 * occlusion)[:, :, None]       # The runtime has no separate occlusion map.
# Bleed the paint a few texels into the unused gaps between UV islands.
filled = paint.copy()
for _ in range(6):
    for shift, axis in ((1, 0), (-1, 0), (1, 1), (-1, 1)):
        grow = np.roll(filled, shift, axis=axis) & ~filled & ~covered
        result[grow] = np.roll(result, shift, axis=axis)[grow]
        filled |= grow
rgba = np.concatenate([np.clip(result, 0, 1), np.ones((SIZE, SIZE, 1), np.float32)], axis=2)
livery_image = bpy.data.images.new('JF-17 display livery', SIZE, SIZE, alpha=False)
livery_image.pixels.foreach_set(rgba.reshape(-1))
livery_path = WORKING_PATH.parent / 'JF17_display_livery.png'
livery_image.filepath_raw = str(livery_path)
livery_image.file_format = 'PNG'
livery_image.save()
bpy.data.images.remove(livery_image)
livery_image = bpy.data.images.load(str(livery_path))


def repainted(name, value, label):
    """A donor surface map with the painted skin set to `value`."""
    source = load(name, colour=False)
    data = pixels(source)
    if data.shape[:2] != (SIZE, SIZE):
        raise RuntimeError('Donor texture sizes differ')
    data[filled, :3] = value
    image = bpy.data.images.new(label, SIZE, SIZE, alpha=False)
    image.colorspace_settings.name = 'Non-Color'
    image.pixels.foreach_set(data.reshape(-1))
    path = WORKING_PATH.parent / (label.replace(' ', '_') + '.png')
    image.filepath_raw = str(path)
    image.file_format = 'PNG'
    image.save()
    bpy.data.images.remove(image)
    bpy.data.images.remove(source)
    image = bpy.data.images.load(str(path))
    image.colorspace_settings.name = 'Non-Color'
    return image


# The donor's skin is bare, polished metal. Display paint is a semi-gloss
# dielectric, so the repainted texels lose their metal and most of their shine.
metal_image = repainted('JF-17_3_JF-17_Metallic.png', 0.0, 'JF17 painted metallic')
rough_image = repainted('JF-17_3_JF-17_Roughness.png', .42, 'JF17 painted roughness')

# --- Materials ---------------------------------------------------------------


def material(name, base, normal=None, rough=None, metal=None, alpha=None, reflection=None):
    result = bpy.data.materials.new('JF-17 | ' + name)
    result.use_nodes = True
    nodes, links = result.node_tree.nodes, result.node_tree.links
    shader = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')

    def texture(image, socket):
        node = nodes.new('ShaderNodeTexImage')
        node.image = image
        links.new(node.outputs['Color'], socket)
    if isinstance(base, tuple):
        shader.inputs['Base Color'].default_value = (*base, 1)
        result.diffuse_color = (*base, alpha or 1)
    else:
        texture(base, shader.inputs['Base Color'])
    if normal:
        bump = nodes.new('ShaderNodeNormalMap')
        texture(normal, bump.inputs['Color'])
        links.new(bump.outputs['Normal'], shader.inputs['Normal'])
    if rough:
        texture(rough, shader.inputs['Roughness'])
    if metal:
        texture(metal, shader.inputs['Metallic'])
    if alpha is not None:
        shader.inputs['Alpha'].default_value = alpha
        shader.inputs['Roughness'].default_value = .08
        shader.inputs['Metallic'].default_value = .05
        if 'BLENDED' in [i.identifier for i in result.bl_rna.properties['surface_render_method'].enum_items]:
            result.surface_render_method = 'BLENDED'
    if reflection:
        result['ofs_environment_reflection'] = reflection
    result.use_backface_culling = False
    return result


airframe_material = material('airframe', livery_image, load('JF-17_3_JF-17_Normal.png', False), rough_image, metal_image)
glass_material = material('canopy glass', (.20, .26, .29), alpha=.16, reflection=.75)
for image in bpy.data.images:
    if image.users and image.packed_file is None and image.filepath:
        image.pack()

# --- Scene -------------------------------------------------------------------
aircraft = bpy.data.collections.new('JF-17 | aircraft')
scene.collection.children.link(aircraft)
assembly = bpy.data.objects.new('JF-17 | assembly', None)
aircraft.objects.link(assembly)
assembly['source'] = 'https://sketchfab.com/3d-models/jf-17-b57660f346314df1877e15b85d6e74be'
assembly['author'] = 'dimal965'
assembly['license'] = 'CC-BY-4.0'
assembly['scale'] = SCALE


def pivot(name, location, channel='', axis=(0, 1, 0), gain=1.0, slide=(0, 0, 0), group=None):
    obj = bpy.data.objects.new('ofs_' + name, None)
    aircraft.objects.link(obj)
    obj.empty_display_size = .2
    obj.parent = group or assembly
    bpy.context.view_layer.update()
    obj.location = Vector(location) - (obj.parent.matrix_world.translation if group else Vector())
    obj['ofs_channel'] = channel
    obj['ofs_axis'] = gltf(Vector(axis).normalized())
    obj['ofs_gain'] = gain
    obj['ofs_slide'] = gltf(Vector(slide))
    bpy.context.view_layer.update()
    return obj


def place(name, label, group=None, lod=3):
    obj = DONOR.pop(name)
    glass = bool(obj.material_slots) and obj.material_slots[0].material.name.startswith('JF-17_glass')
    obj.name = 'JF-17 | ' + label
    obj.data.name = obj.name
    obj.data.materials.clear()
    obj.data.materials.append(glass_material if glass else airframe_material)
    for collection in list(obj.users_collection):
        collection.objects.unlink(obj)
    aircraft.objects.link(obj)
    obj['ofs_last_lod'] = lod
    obj.parent = group or assembly
    bpy.context.view_layer.update()
    obj.matrix_parent_inverse = obj.parent.matrix_world.inverted()
    return obj


report = {'source': assembly['source'], 'author': 'dimal965', 'license': 'CC BY 4.0', 'scale_m_per_unit': SCALE,
          'leg_lowered_m': {k: round(v, 4) for k, v in STANCE.items()}}
place('body', 'airframe')
place('glass', 'windscreen')
place('Object026', 'canopy frame')
place('Object033', 'canopy')

PORT = Vector((0, -1, 0))  # A positive turn about a port-pointing hinge raises a trailing edge.


def edge_line(name, forward):
    pts = points(name)
    lo, hi = bounds(pts)
    span = hi.y - lo.y
    ends = []
    for y0, y1 in ((lo.y, lo.y + .1 * span), (hi.y - .1 * span, hi.y)):
        strip = [p for p in pts if y0 <= p.y <= y1]
        pick = (min if forward else max)(strip, key=lambda p: p.x)
        ends.append(Vector((pick.x, (y0 + y1) * .5, sum(p.z for p in strip) / len(strip))))
    return ends  # Port end first.


SURFACES = (('aileron', 'aileron_', True, 1, (37, 38)), ('flap', 'flap', True, -math.radians(25), (39, 40)),
            ('inner leading flap', 'slat', False, -1.5, (41, 42)), ('outer leading flap', 'slat', False, -1.5, (43, 44)))
for label, channel, forward, gain, numbers in SURFACES:
    for number in numbers:
        name = f'Object{number:03d}'
        side = 'L' if bounds(points(name))[0].y < 0 else 'R'
        a, b = edge_line(name, forward)
        full = f'{label} {side}'
        place(name, full, pivot(full.replace(' ', '_'), (a + b) * .5, channel + side if channel.endswith('_') else channel,
                                a - b, gain))
for name in ('Object034', 'Object035'):  # All-moving tailplanes on a shaft at mid root chord.
    pts = points(name)
    lo, hi = bounds(pts)
    side = 'L' if lo.y < 0 else 'R'
    inboard = min(pts, key=lambda p: abs(p.y))
    root = [p for p in pts if abs(p.y - inboard.y) < .1]
    shaft = Vector(((min(p.x for p in root) + max(p.x for p in root)) * .5, inboard.y, sum(p.z for p in root) / len(root)))
    place(name, 'stabilator ' + side, pivot('stabilator_' + side, shaft, 'elevator', PORT, 1))
pts = points('Object036')
lo, hi = bounds(pts)
low_end = min((p for p in pts if p.z < lo.z + .12), key=lambda p: p.x)
high_end = min((p for p in pts if p.z > hi.z - .12), key=lambda p: p.x)
place('Object036', 'rudder', pivot('rudder', low_end, 'rudder', high_end - low_end, 1))

# Doors stand open in the donor; each closes by undoing its own rotation.
report['door_swing_deg'] = {}
for name, label in (('Object027', 'nose door A'), ('Object028', 'nose door B'), ('Object029', 'main door A'),
                    ('Object031', 'main door B'), ('Object030', 'main door C'), ('Object032', 'main door D')):
    origin, opened = OPENING[name]
    axis, angle = opened.inverted().to_quaternion().to_axis_angle()
    report['door_swing_deg'][label] = round(math.degrees(angle), 1)
    place(name, label, pivot(label.replace(' ', '_'), FRAME @ origin, 'gear_door', D(axis), angle), lod=2)

# Undercarriage. The donor gives no stowed pose: each leg folds a quarter turn
# about a lateral trunnion at its top and slides into the fuselage, where the
# closed doors hide it.
nose_lo, nose_hi = bounds(points('nose'))
nose_axle, nose_radius = WHEEL['wheel_nose']
trunnion = Vector((nose_axle.x, 0, nose_hi.z - .05))
suspension = pivot('suspension_nose', trunnion, 'compression_nose', gain=0, slide=(0, 0, 1))
fold = pivot('nose_gear', trunnion, 'gear_fold', PORT, math.pi / 2, (.25, 0, .12), suspension)  # Wheel swings aft.
place('nose', 'nose leg', fold, lod=2)
steer = pivot('nose_steering', Vector((nose_axle.x, 0, nose_axle.z)), 'steering', (0, 0, 1), 1, group=fold)
place('wheel_nose', 'nose wheel', pivot('nose_wheel', nose_axle, 'nose_wheel', PORT, 1, group=steer))
MAIN_AXLE = {}
for side, sign in (('L', -1), ('R', 1)):
    lo, hi = bounds(points('main_' + side))
    axle, radius = WHEEL['wheel_' + side]
    MAIN_AXLE[side] = (axle, radius)
    trunnion = Vector((axle.x, axle.y, hi.z - .05))
    suspension = pivot('suspension_' + side, trunnion, 'compression_' + side, gain=0, slide=(0, 0, 1))
    # Wheel swings forward and the leg moves inboard under the intake duct.
    fold = pivot('main_gear_' + side, trunnion, 'gear_fold', PORT, -math.pi / 2, (.1, -sign * .42, .10), suspension)
    place('main_' + side, 'main leg ' + side, fold, lod=2)
    place('wheel_' + side, 'main wheel ' + side, pivot('main_wheel_' + side, axle, 'wheel', PORT, 1, group=fold))
if DONOR:
    raise RuntimeError(f'Donor layout changed: unplaced objects {sorted(DONOR)}')
bpy.context.view_layer.update()

# --- Anchors for core/src/aircraft_definition.cpp ---------------------------
MAIN_GEAR_AFT_OF_CG_M = .55  # jf17Config() gear_main_*.x


def placed(label):
    obj = bpy.data.objects['JF-17 | ' + label]
    return [obj.matrix_world @ v.co for v in obj.data.vertices]


skin_points = placed('airframe')
skin_lo, skin_hi = bounds(skin_points)
ring = [p for p in skin_points if p.x > skin_hi.x - .05 and abs(p.y) < 1]  # Nozzle lip.
nozzle = Vector((skin_hi.x, 0, (min(p.z for p in ring) + max(p.z for p in ring)) * .5))
main_axle = MAIN_AXLE['L'][0]
cg = Vector((main_axle.x - MAIN_GEAR_AFT_OF_CG_M, 0, nozzle.z))  # On the thrust line.


def body(point):  # Authoring metres to body forward/right/down about the CG.
    return [round(cg.x - point.x, 3), round(point.y - cg.y, 3), round(cg.z - point.z, 3)]


every = [obj.matrix_world @ v.co for obj in aircraft.objects if obj.type == 'MESH' for v in obj.data.vertices]
all_lo, all_hi = bounds(every)
canopy_lo, canopy_hi = bounds(placed('canopy'))
tip = min(skin_points, key=lambda p: p.y)
rail = [p for p in skin_points if p.y < tip.y + .12]
report.update({
    'dimensions_m': {'length': all_hi.x - all_lo.x, 'span': all_hi.y - all_lo.y, 'height': all_hi.z - all_lo.z,
                     'ground': all_lo.z, 'nose': all_lo.x},
    'asset_cg_gltf': [round(cg.x, 3), round(cg.z, 3), 0],
    'body_frd': {
        'exhaust': body(nozzle), 'nozzle_radius': round((max(p.z for p in ring) - min(p.z for p in ring)) * .5, 3),
        'canopy': [body(canopy_lo), body(canopy_hi)],
        'wingtip_rail_L': [body(Vector((min(p.x for p in rail), tip.y, min(p.z for p in rail)))),
                           body(Vector((max(p.x for p in rail), tip.y, min(p.z for p in rail))))],
        'nose_wheel': body(nose_axle), 'main_wheel_L': body(main_axle),
        'nose_contact': body(Vector((nose_axle.x, 0, 0))), 'main_contact_L': body(Vector((main_axle.x, main_axle.y, 0))),
        'fin_tip': body(Vector((all_hi.x, 0, all_hi.z))), 'nose_tip': body(Vector((all_lo.x, 0, cg.z)))},
    'wheel_radius_m': {'main': MAIN_AXLE['L'][1], 'nose': nose_radius},
    'wheel_track_m': 2 * abs(main_axle.y), 'wheelbase_m': main_axle.x - nose_axle.x})
shell = bmesh.new()
shell.from_mesh(bpy.data.objects['JF-17 | airframe'].data)
tree = BVHTree.FromBMesh(shell)
sections, undersides = {}, {}
for station in (7.5, 6.0, 4.5, 3.0, 1.5, 0.0, -1.5, -3.0, -4.5, -5.5):
    at = cg.x - station
    up = tree.ray_cast(Vector((at, .03, 20)), Vector((0, 0, -1)))[0]
    down = tree.ray_cast(Vector((at, .03, -20)), Vector((0, 0, 1)))[0]
    flank = tree.ray_cast(Vector((at, -20, cg.z)), Vector((0, 1, 0)))[0]
    if up and down:
        sections[station] = [round(cg.z - up.z, 3), round(cg.z - down.z, 3), round(abs(flank.y), 3) if flank else None]
for forward in (1.0, .5, 0, -.5, -1.0, -1.5, -2.0, -2.5):
    for out in (1.9, 2.3, 2.7, 3.1, 3.5, 3.9, 4.3, 4.75):
        hit = tree.ray_cast(Vector((cg.x - forward, -out, -20)), Vector((0, 0, 1)))[0]
        if hit:
            undersides[f'{forward},{-out}'] = round(cg.z - hit.z, 3)
# Underwing pylons: the lowest skin along two chordwise cuts, every 10 cm of span.
pylons = {}
for forward in (-.6, -1.2):
    row = []
    for step in range(15, 47):
        hit = tree.ray_cast(Vector((cg.x - forward, -step * .1, -20)), Vector((0, 0, 1)))[0]
        row.append(round(cg.z - hit.z, 2) if hit else None)
    pylons[str(forward)] = row
report['lowest_skin_by_span_from_1.5m'] = pylons
shell.free()
report['fuselage_top_bottom_halfwidth_body'] = sections
report['lowest_skin_body_z'] = undersides


# --- Export ------------------------------------------------------------------
def export_lod(level):
    visibility, modifiers, links, selected = [], [], [], []
    ratio = [1, .5, .22, .09][level]
    path = EXPORT_DIR / f'jf17_lod{level}.glb'
    try:
        bpy.ops.object.select_all(action='DESELECT')
        for obj in aircraft.objects:
            visibility.append((obj, obj.hide_render, obj.hide_get()))
            keep = obj.type == 'EMPTY' or int(obj.get('ofs_last_lod', 3)) >= level
            obj.hide_render = not keep
            obj.hide_set(not keep)
            if not keep:
                continue
            obj.select_set(True)
            selected.append(obj)
            if level and obj.type == 'MESH' and len(obj.data.polygons) > 40:
                reduce = obj.modifiers.new('OFS authored LOD reduction', 'DECIMATE')
                reduce.ratio = max(ratio, .5 if 'canopy' in obj.name or 'windscreen' in obj.name else ratio)
                reduce.use_collapse_triangulate = True
                modifiers.append((obj, reduce))
        bpy.context.view_layer.objects.active = assembly
        bpy.context.view_layer.update()
        graph = bpy.context.evaluated_depsgraph_get()
        triangles = 0
        for obj in selected:
            if obj.type == 'MESH':
                evaluated = obj.evaluated_get(graph)
                data = evaluated.to_mesh()
                data.calc_loop_triangles()
                triangles += len(data.loop_triangles)
                evaluated.to_mesh_clear()
        if level:
            # Reduced tiers keep material names only; runtime shares LOD0's maps.
            for item in {m for o in selected if o.type == 'MESH' for m in o.data.materials if m}:
                for link in list(item.node_tree.links):
                    if link.from_node.type == 'TEX_IMAGE':
                        links.append((item, link.from_socket, link.to_socket))
                        item.node_tree.links.remove(link)
        bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_selection=True, export_apply=True,
                                  export_extras=True, export_cameras=False, export_lights=False,
                                  export_materials='EXPORT', export_yup=True, export_image_format='JPEG',
                                  export_image_quality=92)
        return {'level': level, 'triangles': triangles, 'nodes': len(selected), 'bytes': path.stat().st_size}
    finally:
        for item, source, target in links:
            item.node_tree.links.new(source, target)
        for obj, modifier in modifiers:
            obj.modifiers.remove(modifier)
        for obj, render, hidden in visibility:
            obj.hide_render = render
            obj.hide_set(hidden)


report['lods'] = [export_lod(level) for level in range(4)]
(EXPORT_DIR / 'lod_stats.json').write_text(json.dumps(report, indent=2) + '\n')
bpy.ops.wm.save_as_mainfile(filepath=str(WORKING_PATH))
print('JF-17 DONOR REPORT', json.dumps(report))
