"""Import, place and export the Boeing 747-206B donor model as four native GLBs.

Donor: "Royal Dutch Airlines Boeing 747-206B" by Nobilis 2 (Sketchfab
829cd4d3ccb6404fb503cb4c9115f3da), CC BY 4.0. Credit and the list of changes
ship in licenses/assets/B747.md.

Run headless, never from inside the extracted donor directory:

  blender -b --factory-startup --disable-autoexec \
    --python scripts/b747_donor_import.py -- --source /path/source/747klm.fbx

The livery texture is read from the archive's `textures` folder beside
`source`. The donor is metric with the nose toward -Y, starboard -X and up +Z.
It is modelled in flight: there is no undercarriage and no interior, and of the
control surfaces only the flaps are separate panels. This stage places the donor
in the authoring frame (nose at x = 0, aft +x, starboard +y, up +z from the
ground), anchors its scale to the published length, measures the wing, fuselage,
engines and fin from the mesh, hinges the flaps, cuts out and hinges the other
control surfaces, builds a retracting undercarriage from plain cylinders and
exports the four GLBs with their report.
"""
import argparse
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform

parser = argparse.ArgumentParser()
parser.add_argument('--project-root', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--working-output', type=Path)
options = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
ROOT = options.project_root.resolve()
SOURCE = options.source.resolve()
TEXTURES = SOURCE.parent.parent / 'textures'
EXPORT_DIR = (options.output_dir or ROOT / 'assets/aircraft/b747').resolve()
WORKING_PATH = (options.working_output or ROOT / 'output/B747_donor.blend').resolve()
EXPORT_DIR.mkdir(parents=True, exist_ok=True)
WORKING_PATH.parent.mkdir(parents=True, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(SOURCE))
bpy.context.view_layer.update()
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1

LENGTH_M = 70.66    # Published overall length, 231 ft 10 in: the only scale anchor.
HEIGHT_M = 19.33    # Published height on the ground, 63 ft 5 in, to the top of the fin.
SPAN_M = 59.64      # Published span, 195 ft 8 in: checked against the donor, not used.
AREA_M2 = 511.0     # Published wing area, 5,500 ft2: checked against the donor, not used.
EXPECTED = {'Object_9', 'Object_12', 'Object_15'}
DONOR = {o.name: o for o in bpy.data.objects if o.type == 'MESH'}
if set(DONOR) != EXPECTED:
    raise RuntimeError(f'Donor layout changed: {sorted(set(DONOR) ^ EXPECTED)}')


def base_colour(material):
    shader = next((n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None) if material.use_nodes else None
    return tuple(shader.inputs['Base Color'].default_value)[:3] if shader else (.6, .6, .6)


PLAIN_COLOUR = base_colour(DONOR['Object_12'].data.materials[0])

# Bake each mesh into world space, then detach it from the imported hierarchy.
for obj in DONOR.values():
    obj.data.transform(obj.matrix_world)
    obj.parent = None
    obj.matrix_world = Matrix.Identity(4)
for obj in list(bpy.data.objects):
    if obj.name not in DONOR:
        bpy.data.objects.remove(obj, do_unlink=True)

points = [v.co for obj in DONOR.values() for v in obj.data.vertices]
low = Vector(tuple(min(p[i] for p in points) for i in range(3)))
high = Vector(tuple(max(p[i] for p in points) for i in range(3)))
SCALE = LENGTH_M / (high.y - low.y)
GROUND = high.z - HEIGHT_M / SCALE  # Donor height of the ground: the fin stands the published height above it.
# Donor (X, Y, Z) to authoring (aft, starboard, up): nose at x = 0, starboard = -X, up = Z.
MAP = Matrix(((0, SCALE, 0, -SCALE * low.y), (-SCALE, 0, 0, 0), (0, 0, SCALE, -SCALE * GROUND), (0, 0, 0, 1)))
for obj in DONOR.values():
    obj.data.transform(MAP)
points = [v.co for obj in DONOR.values() for v in obj.data.vertices]
lo = Vector(tuple(min(p[i] for p in points) for i in range(3)))
hi = Vector(tuple(max(p[i] for p in points) for i in range(3)))
if abs(hi.z - HEIGHT_M) > .02 or abs(lo.x) > .02 or abs(hi.x - LENGTH_M) > .02:
    raise RuntimeError(f'Placement failed: {lo} {hi}')


def parts(obj):
    """Loose parts of a mesh, as lists of vertex indices."""
    me = obj.data
    parent = list(range(len(me.vertices)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for poly in me.polygons:
        first = poly.vertices[0]
        for other in poly.vertices[1:]:
            a, b = find(first), find(other)
            if a != b:
                parent[a] = b
    groups = defaultdict(list)
    for i in range(len(me.vertices)):
        groups[find(i)].append(i)
    return list(groups.values())


def extent(obj, indices):
    co = [obj.data.vertices[i].co for i in indices]
    return (Vector(tuple(min(c[k] for c in co) for k in range(3))),
            Vector(tuple(max(c[k] for c in co) for k in range(3))))


airframe = DONOR['Object_15']
wing_points, engine_groups = [], []
for indices in parts(airframe):
    low_p, high_p = extent(airframe, indices)
    size = high_p - low_p
    if size.y >= 20 and not (low_p.y < 2 and high_p.y > -2) and size.z <= 4 and len(indices) >= 20:
        wing_points += [airframe.data.vertices[i].co.copy() for i in indices]
    if max(size) <= 3.0 and len(indices) >= 100:
        engine_groups.append([airframe.data.vertices[i].co.copy() for i in indices])
if len(engine_groups) != 4:
    raise RuntimeError(f'Expected four engine nacelles, found {len(engine_groups)}')

# Ray casts through the airframe's own faces. The mesh has few vertices between
# its ribs, so sampling vertices misses the planform and the fuselage sections.
shell = bmesh.new()
shell.from_mesh(airframe.data)
tree = BVHTree.FromBMesh(shell)


def crossings(origin, direction):
    """Every surface a ray passes through, nearest first."""
    found, point = [], Vector(origin)
    for _ in range(400):
        location = tree.ray_cast(point, direction)[0]
        if location is None:
            break
        found.append(location)
        point = location + direction * 1e-3
    return found


CG_Z = sum(p.z for p in wing_points) / len(wing_points)  # the wing plane's height above the ground.
# Planform: the wing's leading and trailing edge at each spanwise station, every
# 0.4 m from 4 m, which keeps clear of the fuselage shell, integrated by the
# trapezoid rule.
rows = []
for k in range(10, 75):
    y = k * .4
    near = [p.z for p in wing_points if abs(abs(p.y) - y) <= 1.0]
    z = sum(near) / len(near) if near else CG_Z
    xs = [h.x for h in crossings((-5, y, z), Vector((1, 0, 0))) if h.x < 58]
    if len(xs) >= 2:
        rows.append((y, min(xs), max(xs)))  # |y|, leading edge x, trailing edge x
rows.insert(0, (0., rows[0][1], rows[0][2]))  # The wing carries on inside the fuselage at its root chord.


def integrate(values):
    return sum((values[i] + values[i + 1]) / 2 * (rows[i + 1][0] - rows[i][0]) for i in range(len(rows) - 1))


def edges(y):  # Leading and trailing edge x at spanwise station y, interpolated.
    for (y0, le0, te0), (y1, le1, te1) in zip(rows, rows[1:]):
        if y0 <= y <= y1:
            t = (y - y0) / (y1 - y0)
            return le0 + t * (le1 - le0), te0 + t * (te1 - te0)
    raise RuntimeError(f'No planform at {y} m')


chords = [te - le for _, le, te in rows]
half_area = integrate(chords)
MAC = integrate([c * c for c in chords]) / half_area
Y_MAC = integrate([y * c for (y, _, _), c in zip(rows, chords)]) / half_area
LE_MAC = edges(Y_MAC)[0]
WING_AREA_CHECK = 2 * half_area  # both halves; compared with the published area, not used.
CG_X = LE_MAC + .25 * MAC          # 25 % MAC, aft of the nose.
CG = Vector((CG_X, 0., CG_Z))


def body(point):  # Authoring metres to body forward/right/down about the CG.
    return [round(CG.x - point[0], 3), round(point[1] - CG.y, 3), round(CG.z - point[2], 3)]


# Fuselage sections, every 8 m of length, measured by rays: centre height,
# radius and belly.
fuselage = []
for station in (4, 12, 20, 28, 36, 44, 50, 54):
    top = crossings((station, 0, 30), Vector((0, 0, -1)))
    bottom = crossings((station, 0, -5), Vector((0, 0, 1)))
    if not top or not bottom:
        raise RuntimeError(f'No fuselage at station {station}')
    z_hi, z_lo = top[0].z, bottom[0].z
    z_mid = (z_hi + z_lo) / 2
    port = crossings((station, -20, z_mid), Vector((0, 1, 0)))
    starboard = crossings((station, 20, z_mid), Vector((0, -1, 0)))
    half_width = (starboard[0].y - port[0].y) / 2 if port and starboard else 0.
    fuselage.append({'x': station, 'z_centre': z_mid, 'top': z_hi, 'bottom': z_lo,
                     'radius': .85 * min(half_width, (z_hi - z_lo) / 2)})

# Engines: two per side, the inner nearer the centreline.
engines = []
for group in engine_groups:
    centre = sum(group, Vector()) / len(group)
    engines.append({'centre': centre, 'aft': max(p.x for p in group), 'side': 1 if centre.y > 0 else -1})
engines.sort(key=lambda e: (e['side'], abs(e['centre'].y)))
inner = {e['side']: e for e in engines[0::2]}
outer = {e['side']: e for e in engines[1::2]}

# Tailplane and fin.
tail_points = [p.co.copy() for p in airframe.data.vertices if p.co.x >= 55 and abs(p.co.y) >= 3.5 and p.co.z < 11]
fin_points = [p.co.copy() for p in airframe.data.vertices if p.co.x >= 55 and p.co.z >= 11.5]
fin_centre = sum(fin_points, Vector()) / len(fin_points)
fin_fore = min(p.x for p in fin_points)
# Outer wing for the damage geometry and the wingtip anchors.
root_stub = abs(inner[1]['centre'].y) + 1.0
outer_wing = [p for p in wing_points if abs(p.y) >= root_stub]
tip = {side: max((p for p in wing_points if p.y * side > 0), key=lambda p: abs(p.y)) for side in (-1, 1)}

# Collision spheres: 8 fuselage, 4 per wing, 1 fin, all body FRD about the CG.
spheres = []
for section in fuselage:
    spheres.append({'centre': body((section['x'], 0, section['z_centre'])), 'radius': round(section['radius'], 3)})
for side in (-1, 1):
    for y in (8, 15, 22, 27):
        leading, trailing = edges(y)
        spheres.append({'centre': body(((leading + trailing) / 2, side * y, CG.z)),
                        'radius': round(min(3.0, .45 * (trailing - leading)), 3)})
spheres.append({'centre': body((fin_centre.x, 0, fin_centre.z)), 'radius': round(min(4.5, .5 * (max(p.x for p in fin_points) - fin_fore)), 3)})

def material(name, colour, rough=.5, metal=0., image=None):
    mat = bpy.data.materials.new('B-747 | ' + name)
    mat.use_nodes = True
    shader = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    shader.inputs['Roughness'].default_value = rough
    shader.inputs['Metallic'].default_value = metal
    if image is not None:
        node = mat.node_tree.nodes.new('ShaderNodeTexImage')
        node.image = image
        mat.node_tree.links.new(node.outputs['Color'], shader.inputs['Base Color'])
    else:
        shader.inputs['Base Color'].default_value = (*colour, 1)
    mat.use_backface_culling = False
    return mat


livery_image = bpy.data.images.load(str(TEXTURES / 'FAIB_B7471_T.jpg'), check_existing=True)
livery_material = material('livery', (1, 1, 1), rough=.45, image=livery_image)
plain_material = material('plain', PLAIN_COLOUR, rough=.5)
strut_material = material('undercarriage', (.58, .60, .62), rough=.5, metal=.5)
chrome_material = material('oleo', (.86, .87, .88), rough=.12, metal=1.)
wheel_material = material('wheel', (.70, .71, .72), rough=.35, metal=.8)
tyre_material = material('tyre', (.035, .035, .038), rough=.9)

aircraft = bpy.data.collections.new('B-747 | aircraft')
scene.collection.children.link(aircraft)
assembly = bpy.data.objects.new('B-747 | assembly', None)
aircraft.objects.link(assembly)
assembly['source'] = 'https://sketchfab.com/3d-models/royal-dutch-airlines-boeing-747-206b-829cd4d3ccb6404fb503cb4c9115f3da'
assembly['author'] = 'Nobilis 2'
assembly['license'] = 'CC-BY-4.0'
assembly['scale'] = SCALE


PORT = Vector((0, -1, 0))  # A positive turn about a port-pointing hinge raises a trailing edge.


def gltf(vector):  # Blender Z-up to exported Y-up.
    return (vector.x, vector.z, -vector.y)


def attach(obj, parent):
    """Parent an object to a pivot or the assembly, keeping where it stands."""
    obj.parent = parent
    obj.matrix_parent_inverse = parent.matrix_world.inverted()


def pivot(name, location, channel='', axis=(0, 1, 0), gain=1.0, slide=(0, 0, 0), parent=None):
    """An empty that carries a control channel: its children turn about `axis` by `gain` times the channel
    and move by `slide` times it."""
    obj = bpy.data.objects.new('ofs_' + name, None)
    aircraft.objects.link(obj)
    obj.empty_display_size = .5
    attach(obj, parent or assembly)
    obj.matrix_world = Matrix.Translation(Vector(location))
    obj['ofs_channel'] = channel
    obj['ofs_axis'] = gltf(Vector(axis).normalized())
    obj['ofs_gain'] = gain
    obj['ofs_slide'] = gltf(Vector(slide))
    bpy.context.view_layer.update()
    return obj


def finish(bm, label, mat, last_lod=3, parent=None):
    mesh = bpy.data.meshes.new('B-747 | ' + label)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(mat)
    obj = bpy.data.objects.new('B-747 | ' + label, mesh)
    aircraft.objects.link(obj)
    obj['ofs_last_lod'] = last_lod
    attach(obj, parent or assembly)
    return obj


def cylinder(label, a, b, radius, mat, segments=12, parent=None, last_lod=3):
    a, b = Vector(a), Vector(b)
    axis = b - a
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments, radius1=radius,
                          radius2=radius, depth=axis.length)
    turn = Vector((0, 0, 1)).rotation_difference(axis.normalized()).to_matrix().to_4x4()
    bmesh.ops.transform(bm, matrix=Matrix.Translation((a + b) / 2) @ turn, verts=bm.verts)
    return finish(bm, label, mat, last_lod, parent)


# Control surfaces. The donor models its triple-slotted flaps as their own
# panels at the wing's trailing edge, inboard and outboard of the inboard engine;
# they are kept whole and hinged along their forward edge. It has no separate
# aileron, elevator or rudder. The wing and tail skins carry only a few dozen
# vertices each, so a single face can run from the trailing edge almost to the
# leading edge: those surfaces are cut out of their skin with planes, along the
# hinge line and across both ends, and turn about that hinge line, which follows
# the sweep and dihedral.
WING_SKIN, TAIL_SKIN, FIN_SKIN, INBOARD_FLAP, OUTBOARD_FLAP, TRACK_FAIRING = 1, 2, 3, 4, 5, 6
# Plan-view boxes, (x, |y|) corners, that hold each set of donor flap panels.
FLAP_BOXES = {INBOARD_FLAP: ((31.9, 3.2), (38.0, 11.3)), OUTBOARD_FLAP: ((35.6, 12.8), (43.2, 20.9))}
skin_of_vertex, part_of_vertex, wing_parts = {}, {}, []
for number, indices in enumerate(parts(airframe)):
    low_p, high_p = extent(airframe, indices)
    size = high_p - low_p
    kind = 0
    if size.y >= 20 and not (low_p.y < 2 and high_p.y > -2) and size.z <= 4 and len(indices) >= 20:
        kind = WING_SKIN
        wing_parts.append((sum(airframe.data.vertices[i].co.z for i in indices) / len(indices),
                           1 if low_p.y > 0 else -1, number))
    elif (low_p.x >= 30 and size.y <= .8 and 1.5 <= size.x <= 3.6 and low_p.z >= 3.0 and low_p.y * high_p.y > 0
          and 5 <= min(abs(low_p.y), abs(high_p.y)) and max(abs(low_p.y), abs(high_p.y)) <= 20):
        kind = TRACK_FAIRING  # The canoe fairings over the flap tracks, each modelled in two halves.
    elif low_p.x >= 58 and size.y >= 8 and size.z <= 2.5:
        kind = TAIL_SKIN
    elif low_p.x >= 53 and high_p.z >= HEIGHT_M - .1 and size.y <= 1:
        kind = FIN_SKIN
    for flap_kind, ((x0, y0), (x1, y1)) in FLAP_BOXES.items():
        if (low_p.y * high_p.y > 0 and size.y >= 1 and low_p.x >= x0 and high_p.x <= x1
                and min(abs(low_p.y), abs(high_p.y)) >= y0 and max(abs(low_p.y), abs(high_p.y)) <= y1):
            kind = flap_kind
    for i in indices:
        skin_of_vertex[i] = kind
        part_of_vertex[i] = number
# Each wing is an upper and a lower skin; the upper one stands higher.
upper_parts = {max((p for p in wing_parts if p[1] == side))[2] for side in (-1, 1)}
skin = bmesh.new()
skin.from_mesh(airframe.data)
skin_layer = skin.faces.layers.int.new('ofs_skin')
upper_layer = skin.faces.layers.int.new('ofs_upper')
for face in skin.faces:
    face[skin_layer] = skin_of_vertex[face.verts[0].index]
    face[upper_layer] = part_of_vertex[face.verts[0].index] in upper_parts
counts = {kind: sum(f[skin_layer] == kind for f in skin.faces)
          for kind in (WING_SKIN, TAIL_SKIN, FIN_SKIN, INBOARD_FLAP, OUTBOARD_FLAP, TRACK_FAIRING)}
if not all(counts.values()) or len(wing_parts) != 4:
    raise RuntimeError(f'Donor layout changed: skin faces {counts}, {len(wing_parts)} wing skins')


def skin_tree(kind, upper_only=False):
    """A ray-cast tree over one skin only, clear of nacelles, pylons and fairings."""
    copy = skin.copy()
    layer, upper = copy.faces.layers.int['ofs_skin'], copy.faces.layers.int['ofs_upper']
    bmesh.ops.delete(copy, geom=[f for f in copy.faces if f[layer] != kind or (upper_only and not f[upper])],
                     context='FACES')
    tree_of = BVHTree.FromBMesh(copy)
    copy.free()
    return tree_of


def chord_line(tree_of, fixed, across, start, stop):
    """Leading and trailing edge of a surface: the first and last of rays `across` it, swept along x every 5 cm."""
    covered = [x / 20 for x in range(int(start * 20), int(stop * 20))
               if tree_of.ray_cast(fixed(x / 20) - across * 30, across)[0] is not None]
    if len(covered) < 2:
        raise RuntimeError(f'No chord at {tuple(fixed(start))}')
    return fixed(covered[0]), fixed(covered[-1])


def mid_surface(tree_of, point, normal):
    """The point halfway through the skin at `point`, along `normal`."""
    above = tree_of.ray_cast(point + normal * 30, -normal)[0]
    below = tree_of.ray_cast(point - normal * 30, normal)[0]
    if above is None or below is None:
        raise RuntimeError(f'No skin at {tuple(point)}')
    return (above + below) / 2


wing_tree, tail_tree, fin_tree = skin_tree(WING_SKIN), skin_tree(TAIL_SKIN), skin_tree(FIN_SKIN)
upper_tree = skin_tree(WING_SKIN, upper_only=True)
UP, PORT_SIDE = Vector((0, 0, 1)), Vector((0, 1, 0))
# Spoilers, |y| in metres: two inboard of the inboard engine and four outboard
# of it, as on the 747, each a sixth of the upper skin's local chord deep.
SPOILERS = ((5.0, 7.7), (7.7, 10.6), (13.0, 14.9), (14.9, 16.8), (16.8, 18.7), (18.7, 20.6))
SPOILER_DEPTH = 1 / 6
# The flaps turn about a line this far under their forward edge, so they run a
# little aft as they droop and open a narrow slot, and stay on the wing.
FLAP_HINGE_DEPTH = .35


def wing_hinge(y, fraction):
    """The hinge point `fraction` of the local chord ahead of the wing's trailing edge, at span y."""
    leading, trailing = chord_line(wing_tree, lambda x: Vector((x, y, 0)), UP, 15, 55)
    return mid_surface(wing_tree, trailing.lerp(leading, fraction), UP)


def spoiler_chord(y):
    """Forward and aft x of the spoiler at span y: the aft part of the upper skin's chord."""
    leading, trailing = chord_line(upper_tree, lambda x: Vector((x, y, 0)), UP, 15, 55)
    return trailing.lerp(leading, SPOILER_DEPTH).x, trailing.x - .05


def upper_surface(x, y):
    """Height of the upper skin at (x, y), or just ahead of it where the trailing edge is ragged."""
    for back in [k / 10 for k in range(16)]:
        hit = upper_tree.ray_cast(Vector((x - back, y, 30)), -UP)[0]
        if hit is not None:
            return hit.z
    raise RuntimeError(f'No upper skin at ({x:.2f}, {y:.2f})')


def tail_hinge(y, fraction):
    leading, trailing = chord_line(tail_tree, lambda x: Vector((x, y, 0)), UP, 55, 72)
    return mid_surface(tail_tree, trailing.lerp(leading, fraction), UP)


def fin_hinge(z, fraction):
    leading, trailing = chord_line(fin_tree, lambda x: Vector((x, 0, z)), PORT_SIDE, 50, 72)
    return trailing.lerp(leading, fraction)


def cut(kind, planes, nearby):
    """Split the faces of one skin that lie `nearby` along each plane."""
    for origin, normal in planes:
        faces = [f for f in skin.faces if (f[skin_layer] == kind if isinstance(kind, int) else kind(f)) and nearby(f)]
        geom = list({v for f in faces for v in f.verts}) + list({e for f in faces for e in f.edges}) + faces
        bmesh.ops.bisect_plane(skin, geom=geom, plane_co=origin, plane_no=normal, dist=1e-5)


def split_off(keep, label):
    """A new object holding the skin faces `keep` accepts; they leave the airframe."""
    skin.faces.index_update()
    chosen = [f for f in skin.faces if keep(f)]
    if not chosen:
        raise RuntimeError(f'No faces for {label}')
    part = skin.copy()
    part.faces.index_update()
    keep_index = {f.index for f in chosen}
    bmesh.ops.delete(part, geom=[f for f in part.faces if f.index not in keep_index], context='FACES')
    bmesh.ops.delete(skin, geom=chosen, context='FACES')
    return finish(part, label, livery_material)


def aft_normal(a, b, up):
    """The normal of the plane through hinge line a-b that contains `up`, pointing aft."""
    normal = (b - a).cross(up).normalized()
    return normal if normal.x > 0 else -normal


surfaces, riders = [], []
for side, name in ((-1, 'L'), (1, 'R')):
    for kind, label in ((INBOARD_FLAP, 'inboard flap'), (OUTBOARD_FLAP, 'outboard flap')):
        flap = split_off(lambda f, kind=kind, side=side: f[skin_layer] == kind and f.calc_center_median().y * side > 0,
                         f'{label} {name}')
        points_of = [v.co for v in flap.data.vertices]
        inner_y, outer_y = min(abs(p.y) for p in points_of), max(abs(p.y) for p in points_of)
        # The flap turns about its forward edge, found at both of its ends.
        a = min((p for p in points_of if abs(p.y) < inner_y + .4), key=lambda p: p.x).copy()
        b = min((p for p in points_of if abs(p.y) > outer_y - .4), key=lambda p: p.x).copy()
        surfaces.append((side, name, 'flap', label, a, b, flap))
        # The aft part of each track fairing under this flap rides with it.
        normal = aft_normal(a, b, UP)

        def under(f, side=side, a=a, b=b):
            c = f.calc_center_median()
            return f[skin_layer] == TRACK_FAIRING and c.y * side > 0 and min(abs(a.y), abs(b.y)) < abs(c.y) < max(abs(a.y), abs(b.y))
        cut(under, ((a, normal),), lambda f: True)
        fairings = split_off(lambda f, under=under, a=a, normal=normal: under(f) and (f.calc_center_median() - a).dot(normal) > 0,
                             f'{label} track fairings {name}')
        riders.append((label.replace(' ', '_') + '_' + name, fairings))
    # The low-speed aileron: the aft fifth of the outer wing's chord, |y| in metres.
    for channel, label, first, last, fraction in (('aileron', 'aileron', 21.8, 28.6, .22),):
        a, b = wing_hinge(side * first, fraction), wing_hinge(side * last, fraction)
        normal = aft_normal(a, b, Vector((0, 0, 1)))

        def nearby(f, side=side, first=first, last=last):
            ys = [v.co.y * side for v in f.verts]
            return max(ys) >= first - .5 and min(ys) <= last + .5

        cut(WING_SKIN, ((a, normal), (Vector((0, side * first, 0)), Vector((0, 1, 0))),
                        (Vector((0, side * last, 0)), Vector((0, 1, 0)))), nearby)

        def keep(f, a=a, normal=normal, side=side, first=first, last=last):
            c = f.calc_center_median()
            return f[skin_layer] == WING_SKIN and first < side * c.y < last and (c - a).dot(normal) > 0
        surfaces.append((side, name, channel, label, a, b, split_off(keep, f'{label} {name}')))

# Elevators: a third of the tailplane chord, out to just short of the tip.
for side, name in ((-1, 'L'), (1, 'R')):
    first, last = 1.6, 10.7
    a, b = tail_hinge(side * first, .32), tail_hinge(side * last, .32)
    normal = aft_normal(a, b, Vector((0, 0, 1)))
    cut(TAIL_SKIN, ((a, normal), (Vector((0, side * first, 0)), Vector((0, 1, 0))),
                    (Vector((0, side * last, 0)), Vector((0, 1, 0)))), lambda f, side=side: f.verts[0].co.y * side > 0)

    def keep(f, a=a, normal=normal, side=side, first=first, last=last):
        c = f.calc_center_median()
        return f[skin_layer] == TAIL_SKIN and first < side * c.y < last and (c - a).dot(normal) > 0
    surfaces.append((side, name, 'elevator', 'elevator', a, b, split_off(keep, f'elevator {name}')))

# Rudder: the aft 30 % of the fin, from just above the fuselage to below the tip.
RUDDER_LOW, RUDDER_HIGH = 10.4, 18.9
rudder_low, rudder_high = fin_hinge(RUDDER_LOW, .30), fin_hinge(RUDDER_HIGH, .30)
normal = aft_normal(rudder_low, rudder_high, Vector((0, 1, 0)))
cut(FIN_SKIN, ((rudder_low, normal), (Vector((0, 0, RUDDER_LOW)), Vector((0, 0, 1))),
               (Vector((0, 0, RUDDER_HIGH)), Vector((0, 0, 1)))), lambda f: True)
rudder = split_off(lambda f: f[skin_layer] == FIN_SKIN and RUDDER_LOW < f.calc_center_median().z < RUDDER_HIGH
                   and (f.calc_center_median() - rudder_low).dot(normal) > 0, 'rudder')
skin.faces.layers.int.remove(skin_layer)
skin.faces.layers.int.remove(upper_layer)
skin.to_mesh(airframe.data)
skin.free()
attach(rudder, pivot('rudder', rudder_low, 'rudder', rudder_high - rudder_low))
hinges, flap_pivots = {}, {}
for side, name, channel, label, a, b, obj in surfaces:
    axis = (b - a) if side < 0 else (a - b)  # Port-pointing on both sides: a positive turn raises the trailing edge.
    key = label.replace(' ', '_') + '_' + name
    if channel == 'flap':
        flap_pivots[key] = pivot(key, (a + b) / 2 - UP * FLAP_HINGE_DEPTH, 'flap', axis, -math.radians(30))
        attach(obj, flap_pivots[key])
    elif channel == 'aileron':
        attach(obj, pivot(key, (a + b) / 2, f'aileron_{name}', axis))
    else:
        attach(obj, pivot(key, (a + b) / 2, 'elevator', axis))
    hinges[key] = [[round(v, 3) for v in a], [round(v, 3) for v in b]]
for key, fairings in riders:
    attach(fairings, flap_pivots[key])

# Undercarriage. The donor is modelled in flight and has none. The 747 stands
# on a two-wheel nose gear and four four-wheel main trucks: two wing gears under
# the wing roots, which fold inboard, and two body gears behind them. Here the
# body and nose gears fold aft, where the fuselage is deep enough to hold them
# clear of the wing gears' wells; the real ones fold forward. Each leg rises as
# it folds until every part of it lies inside the skin, and doors close over
# the wells once the gear is up. The simulation's single main contact each side
# stands midway between a wing gear and a body gear.
NOSE_GEAR_X = 6.0        # Nose wheels, aft of the nose: an estimate under the flight deck.
MAIN_AFT_OF_CG = 2.0     # The simulated main contacts, aft of the CG.
MAIN_TRACK = 5.8         # Half track of the simulated main contacts.
WING_GEAR = (CG.x + .6, 5.5)    # Station and half track; the real track is 11.0 m.
BODY_GEAR = (CG.x + 3.4, 1.92)  # The real body gear track is 3.84 m.
TYRE_RADIUS, TYRE_WIDTH = .62, .43  # 49 x 17 tyres on every wheel.
NOSE_WHEEL = MAIN_WHEEL = TYRE_RADIUS
NOSE_AHEAD_OF_CG = CG.x - NOSE_GEAR_X
DOOR_DROP = .03          # Closed doors sit this far under the skin they cover.


def belly(x, y):
    """Height of the airframe's underside above the ground at (x, y)."""
    hit = tree.ray_cast(Vector((x, y, -5)), Vector((0, 0, 1)))[0]
    if hit is None:
        raise RuntimeError(f'No airframe over ({x:.2f}, {y:.2f})')
    return hit.z


def roof(x, y):
    """Height of the airframe's upper surface at (x, y)."""
    hit = tree.ray_cast(Vector((x, y, 40)), Vector((0, 0, -1)))[0]
    if hit is None:
        raise RuntimeError(f'No airframe over ({x:.2f}, {y:.2f})')
    return hit.z


def lathe(label, centre, profile, mat, parent, segments=24, last_lod=3, first_lod=0):
    """A solid of revolution about the lateral axis through `centre`; `profile` holds (radius, lateral offset) pairs."""
    bm = bmesh.new()
    rings = [[bm.verts.new(centre + Vector((r * math.cos(2 * math.pi * k / segments), offset,
                                            r * math.sin(2 * math.pi * k / segments)))) for k in range(segments)]
             for r, offset in profile]
    for first, second in zip(rings, rings[1:]):
        for k in range(segments):
            bm.faces.new((first[k], first[(k + 1) % segments], second[(k + 1) % segments], second[k])).smooth = True
    bm.faces.new(rings[0])
    bm.faces.new(list(reversed(rings[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    obj = finish(bm, label, mat, last_lod, parent)
    obj['ofs_first_lod'] = first_lod
    return obj


# Tyre and wheel sections, (radius, offset across the wheel) in metres.
TYRE_PROFILE = [(.41, -.195), (.52, -.212), (.590, -.192), (.620, -.120), (.626, 0), (.620, .120), (.590, .192),
                (.52, .212), (.41, .195)]
WHEEL_PROFILE = [(.07, -.175), (.21, -.175), (.30, -.135), (.41, -.200), (.41, .200), (.30, .135), (.21, .175),
                 (.07, .175)]
# The distant levels of detail carry coarse tyres and wheels and none of the small fittings.
COARSE_TYRE_PROFILE = [(.41, -.20), (.60, -.20), (.626, 0), (.60, .20), (.41, .20)]
COARSE_WHEEL_PROFILE = [(.30, -.15), (.41, -.20), (.41, .20), (.30, .15)]


def wheels(label, axle, offsets, parent, channel, parts_of):
    """Tyres and wheels on axles across the aircraft, spinning on `channel`."""
    for dx, dy in offsets:
        centre = axle + Vector((dx, dy, 0))
        spin = pivot(f'{label}_wheel_{dx:+.2f}_{dy:+.2f}', centre, channel, PORT, parent=parent)
        parts_of.append(lathe(f'{label} tyre', centre, TYRE_PROFILE, tyre_material, spin, 24, 1))
        parts_of.append(lathe(f'{label} wheel', centre, WHEEL_PROFILE, wheel_material, spin, 16, 1))
        parts_of.append(lathe(f'{label} coarse tyre', centre, COARSE_TYRE_PROFILE, tyre_material, spin, 10, 3, 2))
        parts_of.append(lathe(f'{label} coarse wheel', centre, COARSE_WHEEL_PROFILE, wheel_material, spin, 8, 2, 2))


def leg(label, top, axle, fold, brace_to, parts_of):
    """Shock strut, chrome oleo piston, torque links and a brace to the airframe side of the fold."""
    length = top.z - axle.z
    cylinder_bottom = top - Vector((0, 0, .55 * length))
    piston_bottom = axle + Vector((0, 0, .22))
    parts_of.append(cylinder(f'{label} strut', top, cylinder_bottom, .21, strut_material, 16, parent=fold))
    parts_of.append(cylinder(f'{label} gland', cylinder_bottom + Vector((0, 0, .06)), cylinder_bottom - Vector((0, 0, .06)),
                             .235, strut_material, 16, parent=fold, last_lod=1))
    parts_of.append(cylinder(f'{label} piston', cylinder_bottom, piston_bottom, .14, chrome_material, 16, parent=fold))
    # Torque links: an upper and a lower arm meeting at a knee ahead of the piston.
    knee = (cylinder_bottom + piston_bottom) / 2 + Vector((-.36, 0, 0))
    for a, b in ((cylinder_bottom + Vector((-.18, 0, .08)), knee), (knee, piston_bottom + Vector((-.15, 0, .08)))):
        parts_of.append(cylinder(f'{label} torque link', a, b, .045, strut_material, 8, parent=fold, last_lod=1))
    # The brace runs from the strut to the airframe on the side the leg folds
    # away from, so it swings up into the airframe with the leg.
    parts_of.append(cylinder(f'{label} brace', top - Vector((0, 0, .4 * length)), top + brace_to, .085, strut_material, 10,
                             parent=fold, last_lod=2))


def fold_matrix(fold_pivot, value=1.0):
    g = fold_pivot['ofs_axis']
    axis = Vector((g[0], -g[2], g[1]))
    s = fold_pivot['ofs_slide']
    slide = Vector((s[0], -s[2], s[1]))
    centre = fold_pivot.matrix_world.translation.copy()
    return (Matrix.Translation(centre + slide * value) @ Matrix.Rotation(value * fold_pivot['ofs_gain'], 4, axis)
            @ Matrix.Translation(-centre))


def folded_points(fold_pivot, parts_of):
    m = fold_matrix(fold_pivot)
    return [m @ v.co for obj in parts_of for v in obj.data.vertices]


def stow(label, fold_pivot, parts_of, margin=.06):
    """Raise the leg as it folds until all of it lies `margin` inside the skin, and report the folded footprint."""
    points = folded_points(fold_pivot, parts_of)
    lift = max(0., max(belly(p.x, p.y) + margin - p.z for p in points))
    s = fold_pivot['ofs_slide']
    fold_pivot['ofs_slide'] = (s[0], s[1] + lift, s[2])  # glTF Y is up.
    points = folded_points(fold_pivot, parts_of)
    high = max(p.z - roof(p.x, p.y) + margin for p in points)
    if high > 0:
        raise RuntimeError(f'The folded {label} gear stands {high:.2f} m out of the top of the airframe')
    low = Vector(tuple(min(p[i] for p in points) for i in range(3)))
    top = Vector(tuple(max(p[i] for p in points) for i in range(3)))
    return low, top, lift


def door(label, x0, x1, y0, y1, hinge, angle, lowest=.25, cells=8):
    """A door flush under the skin over the plan rectangle, hinged along its `hinge` edge ('y0' or 'y1'),
    textured from the skin it covers and authored open by `angle`; it closes on `gear_door`."""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new('UVMap')
    grid = [[None] * (cells + 1) for _ in range(cells + 1)]
    for i in range(cells + 1):
        for j in range(cells + 1):
            x, y = x0 + (x1 - x0) * i / cells, y0 + (y1 - y0) * j / cells
            grid[i][j] = bm.verts.new((x, y, belly(x, y) - DOOR_DROP))
    for i in range(cells):
        for j in range(cells):
            face = bm.faces.new((grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]))
            lifted = [loop.vert.co + Vector((0, 0, DOOR_DROP)) for loop in face.loops]
            for loop, value in zip(face.loops, skin_uvs(sum(lifted, Vector()) / 4, lifted)):
                loop[uv].uv = value
    edge_y = y0 if hinge == 'y0' else y1
    a = Vector((x0, edge_y, belly(x0, edge_y) - DOOR_DROP))
    b = Vector((x1, edge_y, belly(x1, edge_y) - DOOR_DROP))
    centre = sum((v.co for v in bm.verts), Vector()) / len(bm.verts)
    # Open the door the way that swings its free edge down.
    for sign in (1, -1):
        turn = (Matrix.Translation(a) @ Matrix.Rotation(sign * angle, 4, (b - a).normalized()) @ Matrix.Translation(-a))
        if (turn @ centre).z < centre.z:
            break
    bmesh.ops.transform(bm, matrix=turn, verts=bm.verts)
    if min(v.co.z for v in bm.verts) < lowest:
        raise RuntimeError(f'The open {label} door reaches {min(v.co.z for v in bm.verts):.2f} m from the ground')
    hinge_pivot = pivot(label.replace(' ', '_') + '_door', a, 'gear_door', b - a, -sign * angle)
    return finish(bm, label + ' door', livery_material, 3, hinge_pivot)


# Texture for the doors: each door vertex takes the UV of the skin just above it.
uv_shell = bmesh.new()
uv_shell.from_mesh(airframe.data)
bmesh.ops.triangulate(uv_shell, faces=uv_shell.faces)
uv_shell.faces.ensure_lookup_table()
uv_layer = uv_shell.loops.layers.uv.active
uv_tree = BVHTree.FromBMesh(uv_shell)


def skin_uvs(centre, points, from_above=False):
    """UVs of `points` carried on the texture mapping of the outermost skin triangle straight below or above
    `centre`, so a door or panel cell never straddles a seam between texture islands."""
    for back in [k / 10 for k in range(16)]:  # Step forward off a ragged trailing edge.
        if from_above:
            index = uv_tree.ray_cast(Vector((centre.x - back, centre.y, 30)), -UP)[2]
        else:
            index = uv_tree.ray_cast(Vector((centre.x - back, centre.y, -5)), UP)[2]
        if index is not None:
            break
    else:
        raise RuntimeError(f'No skin over ({centre.x:.2f}, {centre.y:.2f})')
    face = uv_shell.faces[index]
    corners = [loop.vert.co for loop in face.loops]
    uvs = [loop[uv_layer].uv.to_3d() for loop in face.loops]
    return [barycentric_transform(point, *corners, *uvs).to_2d() for point in points]


# Spoilers: plates laid on the upper skin over the panels its livery already
# shows, textured from it, hinged at their forward edge and raised by up to 45
# degrees on `spoiler`.
for side, name in ((-1, 'L'), (1, 'R')):
    for number, (first, last) in enumerate(SPOILERS):
        bm = bmesh.new()
        uv = bm.loops.layers.uv.new('UVMap')
        span_cells, chord_cells = 6, 3
        grid = []
        for i in range(span_cells + 1):
            y = side * (first + (last - first) * i / span_cells)
            fore, aft = spoiler_chord(y)
            grid.append([])
            for j in range(chord_cells + 1):
                x = fore + (aft - fore) * j / chord_cells
                grid[-1].append(bm.verts.new((x, y, upper_surface(x, y) + .012)))
        for i in range(span_cells):
            for j in range(chord_cells):
                face = bm.faces.new((grid[i][j], grid[i][j + 1], grid[i + 1][j + 1], grid[i + 1][j]))
                points = [loop.vert.co for loop in face.loops]
                for loop, value in zip(face.loops, skin_uvs(sum(points, Vector()) / 4, points, from_above=True)):
                    loop[uv].uv = value
        a, b = grid[0][0].co.copy(), grid[-1][0].co.copy()
        axis = (b - a) if side < 0 else (a - b)  # Port-pointing: a positive turn raises the trailing edge.
        hinge = pivot(f'spoiler_{number + 1}_{name}', (a + b) / 2, 'spoiler', axis, math.radians(45))
        finish(bm, f'spoiler {number + 1} {name}', livery_material, 2, hinge)

report_gear = {}
for side, name in ((-1, 'L'), (1, 'R')):
    # Wing gears swing inboard about a fore-and-aft hinge into the wing-root fairing.
    station, half_track = WING_GEAR
    # The folded strut lies across the wing-root fairing, which is under a metre
    # deep: the trunnion stands midway through it.
    top = Vector((station, side * half_track, (belly(station, side * 3.6) + roof(station, side * 3.6)) / 2))
    axle = Vector((station, side * half_track, MAIN_WHEEL))
    fold = pivot(f'wing_{name}_gear', top, 'gear_fold', (1, 0, 0), -side * math.pi / 2)
    parts_of = []
    leg(f'wing {name}', top, axle, fold, Vector((-1.4, 0, .1)), parts_of)  # a drag brace, level as the leg swings
    parts_of.append(cylinder(f'wing {name} bogie', axle + Vector((-1.0, 0, .05)), axle + Vector((1.0, 0, .05)), .15,
                             strut_material, 12, parent=fold))
    for dx in (-.74, .74):
        parts_of.append(cylinder(f'wing {name} axle', axle + Vector((dx, -.8, 0)), axle + Vector((dx, .8, 0)), .08,
                                 strut_material, 8, parent=fold, last_lod=2))
    wheels(f'wing_{name}', axle, [(dx, dy) for dx in (-.74, .74) for dy in (-.56, .56)], fold, 'wheel', parts_of)
    low, high, lift = stow(f'wing {name}', fold, parts_of)
    inboard = high.y if side < 0 else low.y
    # The well door covers the folded truck; it hangs from its inboard edge.
    # It stays inside the fuselage's width, clear of the inboard flap.
    span = (inboard - .08 * side, side * min(abs(inboard) + 1.75, 3.05))
    door(f'wing gear {name}', low.x - .1, high.x + .1, min(span), max(span), 'y1' if side < 0 else 'y0',
         math.radians(95))
    report_gear[f'wing_{name}'] = {'lift_m': round(lift, 3), 'folded_low': [round(v, 3) for v in low],
                                   'folded_high': [round(v, 3) for v in high]}

    # Body gears swing aft about a lateral hinge.
    station, half_track = BODY_GEAR
    top = Vector((station, side * half_track, belly(station, side * half_track) + 1.0))
    axle = Vector((station, side * half_track, MAIN_WHEEL))
    fold = pivot(f'body_{name}_gear', top, 'gear_fold', PORT, math.pi / 2)
    parts_of = []
    leg(f'body {name}', top, axle, fold, Vector((1.6, 0, 0)), parts_of)
    parts_of.append(cylinder(f'body {name} bogie', axle + Vector((-1.0, 0, .05)), axle + Vector((1.0, 0, .05)), .15,
                             strut_material, 12, parent=fold))
    for dx in (-.74, .74):
        parts_of.append(cylinder(f'body {name} axle', axle + Vector((dx, -.8, 0)), axle + Vector((dx, .8, 0)), .08,
                                 strut_material, 8, parent=fold, last_lod=2))
    wheels(f'body_{name}', axle, [(dx, dy) for dx in (-.74, .74) for dy in (-.56, .56)], fold, 'wheel', parts_of)
    low, high, lift = stow(f'body {name}', fold, parts_of)
    # Hung from its outboard edge and swung past the vertical, clear of the truck.
    door(f'body gear {name}', low.x - .1, high.x + .1, low.y - .08, high.y + .08,
         'y0' if side < 0 else 'y1', math.radians(112))
    report_gear[f'body_{name}'] = {'lift_m': round(lift, 3), 'folded_low': [round(v, 3) for v in low],
                                   'folded_high': [round(v, 3) for v in high]}

nose_x = CG.x - NOSE_AHEAD_OF_CG
nose_top = Vector((nose_x, 0, belly(nose_x, 0) + 1.1))
nose_axle = Vector((nose_x, 0, NOSE_WHEEL))
nose_fold = pivot('nose_gear', nose_top, 'gear_fold', PORT, math.pi / 2)
parts_of = []
leg('nose', nose_top, nose_axle, nose_fold, Vector((1.4, 0, 0)), parts_of)
# Nose steering turns about the strut's axis; each wheel spins on its own axle.
nose_steer = pivot('nose_steering', nose_axle, 'steering', (0, 0, 1), parent=nose_fold)
parts_of.append(cylinder('nose axle', nose_axle + Vector((0, -.62, 0)), nose_axle + Vector((0, .62, 0)), .07,
                         strut_material, 8, parent=nose_steer))
wheels('nose', nose_axle, [(0, -.39), (0, .39)], nose_steer, 'nose_wheel', parts_of)
low, high, lift = stow('nose', nose_fold, parts_of)
for side, name in ((-1, 'L'), (1, 'R')):
    # Two doors meet on the centreline and hang from their outer edges.
    edge = max(abs(low.y), abs(high.y)) + .08
    door(f'nose gear {name}', low.x - .1, high.x + .1, min(0, side * edge), max(0, side * edge),
         'y0' if side < 0 else 'y1', math.radians(95))
report_gear['nose'] = {'lift_m': round(lift, 3), 'folded_low': [round(v, 3) for v in low],
                       'folded_high': [round(v, 3) for v in high]}
uv_shell.free()


# Donor airframe: livery on the skin, the small plain-coloured details on their own material.
for name, obj in DONOR.items():
    for collection in list(obj.users_collection):
        collection.objects.unlink(obj)
    aircraft.objects.link(obj)
    obj.name = 'B-747 | ' + ('details' if name == 'Object_12' else 'airframe ' + name)
    obj.data.name = obj.name
    obj.data.materials.clear()
    obj.data.materials.append(plain_material if name == 'Object_12' else livery_material)
    obj['ofs_last_lod'] = 3
    obj.parent = assembly
    obj.matrix_parent_inverse = assembly.matrix_world.inverted()
bpy.context.view_layer.update()


def export_lod(level):
    visibility, modifiers, links, selected = [], [], [], []
    ratio = [1, .6, .32, .16][level]
    path = EXPORT_DIR / f'b747_lod{level}.glb'
    try:
        bpy.ops.object.select_all(action='DESELECT')
        for obj in aircraft.objects:
            visibility.append((obj, obj.hide_render, obj.hide_get()))
            keep = obj.type == 'EMPTY' or int(obj.get('ofs_first_lod', 0)) <= level <= int(obj.get('ofs_last_lod', 3))
            obj.hide_render = not keep
            obj.hide_set(not keep)
            if not keep:
                continue
            obj.select_set(True)
            selected.append(obj)
            if level and obj.type == 'MESH' and len(obj.data.polygons) > 400 and obj.name.startswith('B-747 | airframe'):
                reduce = obj.modifiers.new('OFS authored LOD reduction', 'DECIMATE')
                reduce.ratio = ratio
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


engine_slots = {side: {'x': body(((inner[side]['centre'].x + outer[side]['centre'].x) / 2, 0, 0))[0],
                       'y': round(side * (abs(inner[side]['centre'].y) + abs(outer[side]['centre'].y)) / 2, 3)}
                for side in (-1, 1)}
report = {
    'scale_m_per_donor_unit': round(SCALE, 6),
    'dimensions_m': {'length': round(hi.x - lo.x, 3), 'span': round(hi.y - lo.y, 3),
                     'height': round(hi.z - lo.z, 3), 'ground': 0},
    'published': {'length': LENGTH_M, 'span': SPAN_M, 'height': HEIGHT_M, 'wing_area': AREA_M2},
    'wing_check': {'area_from_donor_m2': round(WING_AREA_CHECK, 2), 'mac_m': round(MAC, 3),
                   'mac_spanwise_m': round(Y_MAC, 3), 'mac_leading_edge_aft_of_nose_m': round(LE_MAC, 3)},
    'asset_cg_gltf': [round(CG.x, 3), round(CG.z, 3), 0],
    'cg_aft_of_nose_m': round(CG.x, 3), 'cg_height_m': round(CG.z, 3),
    'fuselage_sections': [{k: round(v, 3) for k, v in s.items()} for s in fuselage],
    'engines_body_frd': {'inner': [body(inner[s]['centre']) for s in (-1, 1)],
                         'outer': [body(outer[s]['centre']) for s in (-1, 1)],
                         'exhaust_aft_body': [body((max(inner[s]['aft'], outer[s]['aft']), 0, 0))[0] for s in (-1, 1)]},
    'engine_slots_body_frd': {str(side): engine_slots[side] for side in (-1, 1)},
    'wingtips_body_frd': {'left': body(tip[-1]), 'right': body(tip[1])},
    'tailplane_body_frd': {'centre_x': round(CG.x - sum(p.x for p in tail_points) / len(tail_points), 3),
                           'half_span': round(max(abs(p.y) for p in tail_points), 3)},
    'fin_body_frd': {'top_above_cg': round(hi.z - CG.z, 3), 'leading_edge_x': round(CG.x - fin_fore, 3),
                     'centre': body((fin_centre.x, 0, fin_centre.z))},
    'damage_geometry': {'wing_root_abs_y': round(root_stub, 3), 'wing_tip_abs_y': round(max(abs(p.y) for p in wing_points), 3),
                        'outer_wing_aft_x': round(min(CG.x - p.x for p in outer_wing), 3),
                        'outer_wing_fore_x': round(max(CG.x - p.x for p in outer_wing), 3),
                        'fin_base_above_cg': round(max(s['top'] for s in fuselage if s['x'] >= 52) - CG.z, 3),
                        'fin_top_above_cg': round(hi.z - CG.z, 3), 'fin_fore_x': round(CG.x - fin_fore, 3)},
    'collision_spheres_body_frd': spheres,
    'gear_body_frd': {'nose': [NOSE_AHEAD_OF_CG, 0, round(CG.z, 3)],
                      'main_left': [-MAIN_AFT_OF_CG, -MAIN_TRACK, round(CG.z, 3)],
                      'main_right': [-MAIN_AFT_OF_CG, MAIN_TRACK, round(CG.z, 3)],
                      'wheel_radius_m': {'main': MAIN_WHEEL, 'nose': NOSE_WHEEL},
                      'wing_trucks': [round(WING_GEAR[0], 3), WING_GEAR[1]],
                      'body_trucks': [round(BODY_GEAR[0], 3), BODY_GEAR[1]], 'stowed': report_gear},
    'hinges_authoring': hinges,
    'channels': sorted({o.get('ofs_channel') for o in aircraft.objects if o.type == 'EMPTY' and o.get('ofs_channel')}),
}
report['lods'] = [export_lod(level) for level in range(4)]
(EXPORT_DIR / 'lod_stats.json').write_text(json.dumps(report, indent=2) + '\n')
bpy.ops.wm.save_as_mainfile(filepath=str(WORKING_PATH))
print('B-747 DONOR REPORT', json.dumps(report))
