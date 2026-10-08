"""Import, rig and export the JF-17 Thunder donor model as four native GLBs.

Donor: "JF-17 Thunder with LS-6" by Jeyhun1985 (Sketchfab
10ee4421a360468ebc21c65bdb780c06), listed as CC BY 4.0. The listing describes
the model as taken from War Thunder; see licenses/assets/JF17.md and
docs/ASSET_RELEASE_PROVENANCE.md before distributing the derived GLBs.

Run headless, never from inside the extracted donor directory:

  blender -b --factory-startup --disable-autoexec \
    --python scripts/jf17_donor_import.py -- --source /path/JF-17.obj

The donor is in metres with port +X, up +Y and nose +Z. Its control surfaces,
nozzle petals, doors and undercarriage are separate objects, but it carries no
rig and is modelled with the gear retracted and the doors shut. This stage
swings each leg down about its own trunnion pins and opens the doors, so the
retracted pose the renderer animates back to is the donor's own.
"""
import argparse
import json
import math
import re
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

parser = argparse.ArgumentParser()
parser.add_argument('--project-root', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--working-output', type=Path)
options = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
ROOT = options.project_root.resolve()
SOURCE = options.source.resolve()
EXPORT_DIR = (options.output_dir or ROOT / 'assets/aircraft/jf17').resolve()
WORKING_PATH = (options.working_output or ROOT / 'output/JF17_Thunder_donor.blend').resolve()
EXPORT_DIR.mkdir(parents=True, exist_ok=True)
WORKING_PATH.parent.mkdir(parents=True, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=str(SOURCE), forward_axis='NEGATIVE_Z', up_axis='Y')
scene = bpy.context.scene
scene.name = 'JF-17 | donor rig'
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1

# --- Donor inventory --------------------------------------------------------
DONOR = {}
for obj in list(bpy.data.objects):
    match = re.fullmatch(r'(?:vehicle#)?(.+?)_(\d+)(\.\d+)?', obj.name)
    if obj.type != 'MESH' or not match:
        raise RuntimeError(f'Donor layout changed: unexpected object {obj.name}')
    DONOR.setdefault(match[1], []).append(obj)
# The listing's LS-6 bombs, targeting pod, their inboard and centreline pylons
# and two placeholder meshes are not part of the air-to-air aircraft.
DROPPED = ('ch_lgb_ls_6_500_na', 'optic1_turret', 'optic1_gun', 'optic2_turret', 'optic2_gun', 'empty_mesh',
           'bomb_pylon_2', 'bomb_pylon_3', 'bomb_pylon_4')
for name in DROPPED:
    for obj in DONOR.pop(name):
        bpy.data.objects.remove(obj, do_unlink=True)


def donor(name):
    found = DONOR.get(name, [])
    if len(found) != 1:
        raise RuntimeError(f'Donor layout changed: expected one {name}, found {len(found)}')
    return found[0]


def donor_points(name):  # Donor coordinates: port, up, nose.
    return [v.co.copy() for v in donor(name).data.vertices]


def bounds(points):
    return (Vector([min(p[i] for p in points) for i in range(3)]),
            Vector([max(p[i] for p in points) for i in range(3)]))


def centre(points):
    lo, hi = bounds(points)
    return (lo + hi) * .5


def loose_shells(name):
    mesh = donor(name).data
    parent = list(range(len(mesh.vertices)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for edge in mesh.edges:
        a, b = find(edge.vertices[0]), find(edge.vertices[1])
        if a != b:
            parent[a] = b
    groups = {}
    for index, vert in enumerate(mesh.vertices):
        groups.setdefault(find(index), []).append(vert.co.copy())
    return list(groups.values())


def principal_axis(points, smallest=False):
    c = sum(points, Vector()) / len(points)
    cov = Matrix(((0,) * 3,) * 3)
    for p in points:
        d = p - c
        for i in range(3):
            for j in range(3):
                cov[i][j] += d[i] * d[j]
    # Power iteration; the inverse gives the thin axis of a wheel.
    m = cov.inverted() if smallest else cov
    axis = Vector((.577, .577, .577))
    for _ in range(200):
        axis = (m @ axis).normalized()
    return axis


# --- Frame ------------------------------------------------------------------
nose_z = max(p.z for p in donor_points('fuse'))

# Main legs: two pins on each leg casting share the trunnion axis.
MAIN = {}
for side, key in ((1, 'l'), (-1, 'r')):
    pins = sorted((s for s in loose_shells('gear_' + key + '1') if len(s) == 68 and centre(s).z < -.3),
                  key=lambda s: centre(s).x * side)
    if len(pins) != 2:
        raise RuntimeError('Donor layout changed: main trunnion pins')
    inner, outer = centre(pins[0]), centre(pins[1])
    axis = (outer - inner).normalized()
    pivot_point = (inner + outer) * .5
    wheel_points = donor_points('wheel_' + key)
    hub = sum(wheel_points, Vector()) / len(wheel_points)
    arm = hub - pivot_point
    radial = arm - axis * arm.dot(axis)
    down = Vector((0, -1, 0))
    down = (down - axis * down.dot(axis)).normalized()
    # Signed swing about the pins that puts the wheel at the bottom of its arc.
    angle = math.atan2(axis.dot(radial.cross(down)), radial.dot(down))
    axle = principal_axis(wheel_points, smallest=True)
    radius = max((p - hub - axle * (p - hub).dot(axle)).length for p in wheel_points)
    MAIN[key] = {'axis': axis, 'pivot': pivot_point, 'angle': angle, 'hub': hub, 'axle': axle, 'radius': radius}

# Nose leg: retracts aft about a lateral trunnion at the front of its barrel.
leg_points = donor_points('gear_c1')
front = max(p.z for p in leg_points)
nose_wheel_points = donor_points('wheel_c')
nose_hub = centre(nose_wheel_points)
nose_radius = (bounds(nose_wheel_points)[1].z - bounds(nose_wheel_points)[0].z) * .5
NOSE_TRUNNION_INSET = .055  # Pin bore centre behind the clevis face, measured on the donor.
clevis = [p for p in leg_points if p.z > front - 2 * NOSE_TRUNNION_INSET]
nose_pivot = Vector((0, (min(p.y for p in clevis) + max(p.y for p in clevis)) * .5, front - NOSE_TRUNNION_INSET))
nose_arm = nose_hub - nose_pivot
# About donor +X: the swing that carries the wheel to the bottom of its arc.
NOSE_ANGLE = (2 * math.pi - math.atan2(nose_arm.z, nose_arm.y)) % (2 * math.pi) - math.pi


def swing(pivot_point, axis, angle):
    return Matrix.Translation(pivot_point) @ Matrix.Rotation(angle, 4, axis) @ Matrix.Translation(-pivot_point)


main_ground = min((swing(m['pivot'], m['axis'], m['angle']) @ m['hub']).y - m['radius'] for m in MAIN.values())
nose_ground = (swing(nose_pivot, Vector((1, 0, 0)), NOSE_ANGLE) @ nose_hub).y - nose_radius
GROUND = main_ground
NOSE_OLEO = nose_ground - main_ground  # The nose leg is extended by this to share the plane.
# Donor to authoring metres: X aft from the nose, Y starboard, Z up from the ground.
FRAME = Matrix(((0, 0, -1, nose_z), (-1, 0, 0, 0), (0, 1, 0, -GROUND), (0, 0, 0, 1)))


def P(point):
    return FRAME @ Vector(point)


def D(vector):
    return (FRAME.to_3x3() @ Vector(vector))


def gltf(vector):  # Blender Z-up to exported Y-up.
    return (vector.x, vector.z, -vector.y)


# --- Materials ---------------------------------------------------------------
TEXTURES = SOURCE.parent


def image(name, colour=True):
    path = TEXTURES / name
    if not path.is_file():
        raise RuntimeError(f'Donor texture missing: {name}')
    loaded = bpy.data.images.load(str(path), check_existing=True)
    if not colour:
        loaded.colorspace_settings.name = 'Non-Color'
    loaded.pack()
    return loaded


def surface(name, base=None, normal=None, metal=None, alpha=None, colour=(1, 1, 1), opacity=1, roughness=.55,
            metallic=0, emission=None):
    material = bpy.data.materials.new('JF-17 | ' + name)
    material.use_nodes = True
    nodes, links = material.node_tree.nodes, material.node_tree.links
    shader = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
    shader.inputs['Base Color'].default_value = (*colour, 1)
    shader.inputs['Alpha'].default_value = opacity
    shader.inputs['Roughness'].default_value = roughness
    shader.inputs['Metallic'].default_value = metallic
    if base:
        node = nodes.new('ShaderNodeTexImage')
        node.image = image(base)
        links.new(node.outputs['Color'], shader.inputs['Base Color'])
    if alpha:
        node = nodes.new('ShaderNodeTexImage')
        node.image = image(alpha, colour=False)
        links.new(node.outputs['Color'], shader.inputs['Alpha'])
    if normal:
        node = nodes.new('ShaderNodeTexImage')
        node.image = image(normal, colour=False)
        bump = nodes.new('ShaderNodeNormalMap')
        links.new(node.outputs['Color'], bump.inputs['Color'])
        links.new(bump.outputs['Normal'], shader.inputs['Normal'])
    if metal:
        node = nodes.new('ShaderNodeTexImage')
        node.image = image(metal, colour=False)
        links.new(node.outputs['Color'], shader.inputs['Metallic'])
    if emission:
        shader.inputs['Emission Color'].default_value = (*emission, 1)
        shader.inputs['Emission Strength'].default_value = 1
    material.diffuse_color = (*colour, opacity)
    material.use_backface_culling = False
    blended = opacity < 1 or alpha
    if blended and 'BLENDED' in [i.identifier for i in material.bl_rna.properties['surface_render_method'].enum_items]:
        material.surface_render_method = 'BLENDED'
    return material


glass = surface('canopy glass', colour=(.20, .26, .29), opacity=.16, roughness=.08, metallic=.05)
glass['ofs_environment_reflection'] = .75
lens = surface('lamp lenses', colour=(.03, .03, .035), roughness=.12)
# Donor material identifiers are stable across its objects.
MATERIALS = {
    '9ce83077': surface('airframe', 'jf_17_c.jpg', 'jf_17_n_n.jpg', 'jf_17_n_s.jpg'),
    '563e74fb': surface('cockpit', 'jf_17_interior_c.jpg', 'jf_17_interior_n_n.jpg'),
    'e38aa096': surface('cutout details', 'jf_17_inside_c.jpg', alpha='jf_17_inside_c_a.png'),
    '312c1b96': glass,
    'abbb09df': lens, '3fd3b212': lens, '04bdc19a': lens,
    '3b7a533d': surface('port navigation lens', colour=(.55, .02, .02), roughness=.15, emission=(.5, 0, 0)),
    'd25dae78': surface('starboard navigation lens', colour=(.02, .45, .05), roughness=.15, emission=(0, .4, .03)),
    '80dd5a65': surface('formation strips', colour=(.55, .50, .25), roughness=.3, emission=(.25, .22, .08)),
    '3ff4ee8f': surface('landing lamp glass', colour=(.75, .78, .80), roughness=.1),
    '3c3917cc': surface('missile pylon', 'jf_17_pylon1_c.jpg', 'jf_17_pylon1_n_n.jpg'),
}

# --- Scene -------------------------------------------------------------------
aircraft = bpy.data.collections.new('JF-17 | aircraft')
scene.collection.children.link(aircraft)
assembly = bpy.data.objects.new('JF-17 | assembly', None)
aircraft.objects.link(assembly)
assembly['source'] = 'https://sketchfab.com/3d-models/jf-17-thunder-with-ls-6-10ee4421a360468ebc21c65bdb780c06'
assembly['author'] = 'Jeyhun1985'
assembly['license'] = 'CC-BY-4.0 as listed; see licenses/assets/JF17.md'


def pivot(name, location, channel='', axis=(0, 1, 0), gain=1.0, slide=(0, 0, 0), group=None):
    """An animation node at `location` (authoring metres) turning about `axis`."""
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


def place(name, group=None, lod=3, rest=None, label=None):
    """Move a donor object into the authoring frame, optionally posed by `rest`."""
    obj = donor(name)
    obj.data.transform(FRAME @ rest if rest else FRAME)
    obj.matrix_world = Matrix.Identity(4)
    obj.name = 'JF-17 | ' + (label or name.replace('_', ' '))
    obj.data.name = obj.name
    for slot in obj.material_slots:
        slot.material = MATERIALS[slot.material.name[:8]]
    for collection in list(obj.users_collection):
        collection.objects.unlink(obj)
    aircraft.objects.link(obj)
    obj['ofs_last_lod'] = lod
    obj.parent = group or assembly
    bpy.context.view_layer.update()
    obj.matrix_parent_inverse = obj.parent.matrix_world.inverted()
    del DONOR[name]
    return obj


SKIN = {'JF-17 | ' + n for n in ('fuse', 'tail', 'wing l', 'wing r', 'blister1', 'stabilator L', 'stabilator R',
                                 'rudder', 'aileron l', 'aileron r', 'flap l', 'flap r', 'slat l', 'slat r')}
report = {'source': assembly['source'], 'author': 'Jeyhun1985', 'license': 'CC BY 4.0 as listed',
          'scale_m_per_unit': 1.0, 'nose_oleo_extension_m': -NOSE_OLEO,
          'main_gear_swing_deg': round(math.degrees(MAIN['l']['angle']), 2),
          'nose_gear_swing_deg': round(math.degrees(NOSE_ANGLE), 2)}

# Static airframe.
for name, lod in (('fuse', 3), ('tail', 3), ('wing_l', 3), ('wing_r', 3), ('blister1', 3), ('chute1', 2),
                  ('blister2', 0), ('blister3', 0), ('seat_01', 1), ('bomb_pylon_1', 2), ('bomb_pylon_5', 2),
                  ('aileron1_l', 1), ('aileron1_r', 1), ('gear_c6', 1), ('gear_c7', 1)):
    place(name, lod=lod)
for name in [n for n in DONOR if re.fullmatch(r'airbrake_[lr][2-5]', n)]:
    place(name, lod=0)  # Actuator links behind the closed panels.


# --- Flight controls ---------------------------------------------------------
PORT = Vector((0, -1, 0))  # Positive turn about a port-pointing hinge raises a trailing edge.


def edge_line(name, forward):
    """Root and tip of a surface's leading or trailing edge, in authoring metres."""
    points = [P(p) for p in donor_points(name)]
    lo, hi = bounds(points)
    span = hi.y - lo.y
    ends = []
    for y0, y1 in ((lo.y, lo.y + .08 * span), (hi.y - .08 * span, hi.y)):
        strip = [p for p in points if y0 <= p.y <= y1]
        pick = (min if forward else max)(strip, key=lambda p: p.x)
        ends.append(Vector((pick.x, (y0 + y1) * .5, sum(p.z for p in strip) / len(strip))))
    return ends  # Starboard-most last.


def hinged(name, channel, forward, gain, lod=3, inset=0.0):
    a, b = edge_line(name, forward)
    axis = (a - b).normalized()  # Towards port.
    shift = Vector((inset if forward else -inset, 0, 0))
    place(name, group=pivot(name, (a + b) * .5 + shift, channel, axis, gain), lod=lod)


for key, side in (('l', 'L'), ('r', 'R')):
    hinged('aileron_' + key, 'aileron_' + side, True, 1, inset=.02)
    hinged('flap_' + key, 'flap', True, -math.radians(25), inset=.02)
    # Leading-edge manoeuvring flaps droop about their rear edge.
    hinged('slat_' + key, 'slat', False, -1.5, inset=.02)
# All-moving tailplanes turn on a lateral shaft at mid root chord.
for name in ('elevator0', 'elevator1'):
    points = [P(p) for p in donor_points(name)]
    lo, hi = bounds(points)
    inboard = min(points, key=lambda p: abs(p.y))
    root = [p for p in points if abs(p.y - inboard.y) < .1]
    shaft = Vector(((min(p.x for p in root) + max(p.x for p in root)) * .5, inboard.y,
                    sum(p.z for p in root) / len(root)))
    place(name, group=pivot('stabilator_' + ('L' if lo.y < 0 else 'R'), shaft, 'elevator', PORT, 1),
          label='stabilator ' + ('L' if lo.y < 0 else 'R'))
points = [P(p) for p in donor_points('rudder')]
lo, hi = bounds(points)
low = min((p for p in points if p.z < lo.z + .1), key=lambda p: p.x)
high = min((p for p in points if p.z > hi.z - .1), key=lambda p: p.x)
place('rudder', group=pivot('rudder', low, 'rudder', high - low, 1))
# Four airbrake panels open outwards about their forward edges.
THRUST_Y = 0.0
for name in ('airbrake_l', 'airbrake_l1', 'airbrake_r', 'airbrake_r1'):
    points = [P(p) for p in donor_points(name)]
    lo, hi = bounds(points)
    lead = [p for p in points if p.x < lo.x + .03]
    hinge = sum(lead, Vector()) / len(lead)
    middle = (lo + hi) * .5
    outward = Vector((0, middle.y, middle.z - P((0, THRUST_Y, 0)).z)).normalized()
    place(name, group=pivot(name, hinge, 'spoiler', Vector((1, 0, 0)).cross(outward), math.radians(50)), lod=2)

# Nozzle: the outer flaps and the divergent petals open with reheat.
nozzle_names = sorted(n for n in DONOR if n.startswith('nozzle1_'))
if len(nozzle_names) != 84:
    raise RuntimeError(f'Expected 84 nozzle parts, found {len(nozzle_names)}')
ring = [P(centre(donor_points(n))) for n in nozzle_names]
nozzle_centre = sum(ring, Vector()) / len(ring)
exit_x = max(P(p).x for n in nozzle_names for p in donor_points(n))
for name in nozzle_names:
    points = [P(p) for p in donor_points(name)]
    lo, hi = bounds(points)
    length = hi.x - lo.x
    moving = length > .45 and len(points) > 250
    if not moving:
        place(name, lod=1)
        continue
    lead = [p for p in points if p.x < lo.x + .03]
    root = sum(lead, Vector()) / len(lead)
    radial = Vector((0, root.y - nozzle_centre.y, root.z - nozzle_centre.z)).normalized()
    # Positive reheat turns the trailing edge outwards.
    place(name, group=pivot(name, root, 'nozzle_L', Vector((1, 0, 0)).cross(radial), .07), lod=2)

# --- Undercarriage -----------------------------------------------------------
# Rest pose is gear down. Each fold pivot turns back through the swing found
# above, which returns its parts to the donor's stowed pose exactly.
X_AXIS = Vector((1, 0, 0))
nose_rest = Matrix.Translation(Vector((0, -NOSE_OLEO, 0))) @ swing(nose_pivot, X_AXIS, NOSE_ANGLE)
nose_trunnion = P(nose_pivot) - Vector((0, 0, NOSE_OLEO))
suspension = pivot('suspension_nose', nose_trunnion, 'compression_nose', gain=0, slide=(0, 0, 1))
fold = pivot('nose_gear', nose_trunnion, 'gear_fold', D(X_AXIS), -NOSE_ANGLE, (0, 0, NOSE_OLEO), suspension)
nose_axle = FRAME @ nose_rest @ nose_hub
steer = pivot('nose_steering', Vector((nose_axle.x, 0, nose_axle.z)), 'steering', (0, 0, 1), 1, group=fold)
spin = pivot('nose_wheel', nose_axle, 'nose_wheel', PORT, 1, group=steer)
place('wheel_c', group=spin, rest=nose_rest, label='nose wheel')
for name in ('gear_c', 'gear_c2', 'gear_c3'):
    place(name, group=steer, rest=nose_rest, lod=2)
for name in sorted(n for n in DONOR if re.fullmatch(r'gear_c(1|4|5|1[2-9]|2[0-2])', n)):
    place(name, group=fold, rest=nose_rest, lod=2 if name == 'gear_c1' else 1)
# Four nose doors, each hinged along its outboard edge.
DOOR_OPEN = math.radians(88)
MAIN_DOOR_OPEN = math.radians(115)
for name in ('gear_c8', 'gear_c9', 'gear_c10', 'gear_c11'):
    points = donor_points(name)
    lo, hi = bounds(points)
    side = 1 if lo.x + hi.x > 0 else -1
    edge = [p for p in points if side * p.x > max(abs(lo.x), abs(hi.x)) - .02]
    hinge = Vector((side * max(abs(lo.x), abs(hi.x)), sum(p.y for p in edge) / len(edge), (lo.z + hi.z) * .5))
    axis = Vector((0, 0, 1))
    angle = side * DOOR_OPEN  # Inboard edge swings down.
    place(name, group=pivot(name.replace('gear_c', 'nose_door_'), P(hinge), 'gear_door', D(axis), -angle),
          rest=swing(hinge, axis, angle), lod=2, label=name.replace('gear_c', 'nose door '))

MAIN_REST = {}
for key, side in (('l', 'L'), ('r', 'R')):
    m = MAIN[key]
    rest = swing(m['pivot'], m['axis'], m['angle'])
    MAIN_REST[side] = rest
    trunnion = P(m['pivot'])
    suspension = pivot('suspension_' + side, trunnion, 'compression_' + side, gain=0, slide=(0, 0, 1))
    fold = pivot('main_gear_' + side, trunnion, 'gear_fold', D(m['axis']), -m['angle'], group=suspension)
    axle = FRAME @ rest @ m['hub']
    axle_axis = D(rest.to_3x3() @ m['axle'])
    if axle_axis.y > 0:
        axle_axis.negate()
    m['axle_rest'] = axle_axis
    spin = pivot('main_wheel_' + side, axle, 'wheel', axle_axis, 1, group=fold)
    place('wheel_' + key, group=spin, rest=rest, label='main wheel ' + side)
    for name in sorted(n for n in DONOR if re.fullmatch(r'gear_' + key + r'(|[1-9]|10)', n)):
        place(name, group=fold, rest=rest, lod=2 if name in ('gear_' + key, 'gear_' + key + '1') else 1,
              label=name.replace('gear_' + key, 'main leg ' + side + ' '))
    # The wheel door and its two hinge arms hang from the inboard edge.
    points = donor_points('gear_' + key + '11')
    lo, hi = bounds(points)
    sign = 1 if key == 'l' else -1
    inner = min(abs(lo.x), abs(hi.x))
    edge = [p for p in points if sign * p.x < inner + .03]
    hinge = Vector((sign * inner, sum(p.y for p in edge) / len(edge), (lo.z + hi.z) * .5))
    axis = Vector((0, 0, 1))
    angle = -sign * MAIN_DOOR_OPEN  # Outboard edge swings down and under.
    door = pivot('main_door_' + side, P(hinge), 'gear_door', D(axis), -angle)
    for name in ('gear_' + key + '11', 'gear_' + key + '12', 'gear_' + key + '13'):
        place(name, group=door, rest=swing(hinge, axis, angle), lod=2,
              label=name.replace('gear_' + key + '1', 'main door ' + side + ' '))
if DONOR:
    raise RuntimeError(f'Donor layout changed: unplaced objects {sorted(DONOR)}')
bpy.context.view_layer.update()

# --- Anchors for core/src/aircraft_definition.cpp ---------------------------
MAIN_GEAR_AFT_OF_CG_M = .55  # jf17Config() gear_main_*.x
low = Vector((1e9,) * 3)
high = Vector((-1e9,) * 3)
for obj in aircraft.objects:
    if obj.type == 'MESH':
        for vert in obj.data.vertices:
            world = obj.matrix_world @ vert.co
            for i in range(3):
                low[i] = min(low[i], world[i])
                high[i] = max(high[i], world[i])
main_axle = FRAME @ MAIN_REST['L'] @ MAIN['l']['hub']
cg = Vector((main_axle.x - MAIN_GEAR_AFT_OF_CG_M, 0, nozzle_centre.z))  # On the thrust line.


def body(point):  # Authoring metres to body forward/right/down about the CG.
    return [round(cg.x - point.x, 3), round(point.y - cg.y, 3), round(cg.z - point.z, 3)]


def placed(name):
    obj = bpy.data.objects['JF-17 | ' + name]
    return [obj.matrix_world @ v.co for v in obj.data.vertices]


pylon = min((placed('bomb pylon 1'), placed('bomb pylon 5')), key=lambda points: points[0].y)  # Port.
pylon_lo, pylon_hi = bounds(pylon)


wing = placed('wing l')
tip = min(wing, key=lambda p: p.y)
rail = [p for p in wing if p.y < tip.y + .12]
canopy, seat = placed('blister1'), placed('seat 01')
report.update({
    'dimensions_m': {'length': high.x - low.x, 'span': high.y - low.y, 'height': high.z - low.z,
                     'ground': low.z, 'nose': low.x, 'fuselage_nose_to_nozzle': exit_x},
    'asset_cg_gltf': [round(cg.x, 3), round(cg.z, 3), 0],
    'body_frd': {
        'pilot_eye': body(Vector((bounds(seat)[0].x + .28, 0, bounds(seat)[1].z - .12))),
        'exhaust': body(Vector((exit_x, nozzle_centre.y, nozzle_centre.z))),
        'wingtip_L': body(Vector((max(p.x for p in rail), tip.y, tip.z))),
        'wingtip_rail_L': [body(Vector((min(p.x for p in rail), tip.y, min(p.z for p in rail)))),
                           body(Vector((max(p.x for p in rail), tip.y, min(p.z for p in rail))))],
        'outer_pylon_L': [body(Vector((pylon_lo.x, (pylon_lo.y + pylon_hi.y) * .5, pylon_lo.z))),
                          body(Vector((pylon_hi.x, (pylon_lo.y + pylon_hi.y) * .5, pylon_lo.z)))],
        'nose_wheel': body(nose_axle), 'main_wheel_L': body(main_axle),
        'nose_contact': body(Vector((nose_axle.x, 0, 0))), 'main_contact_L': body(Vector((main_axle.x, main_axle.y, 0))),
        'fin_tip': body(Vector((high.x, 0, high.z))), 'nose_tip': body(Vector((low.x, 0, P((0, 0, nose_z)).z))),
        'canopy_top': body(Vector(((bounds(canopy)[0].x + bounds(canopy)[1].x) * .5, 0, bounds(canopy)[1].z)))},
    'wheel_radius_m': {'main': MAIN['l']['radius'], 'nose': nose_radius},
    'wheel_track_m': 2 * abs(main_axle.y), 'wheelbase_m': main_axle.x - nose_axle.x,
    'main_axle_axis': [round(v, 4) for v in MAIN['l']['axle_rest']]})
# Fuselage top and bottom at the body stations of the hit spheres.
import bmesh
from mathutils.bvhtree import BVHTree
shell = bmesh.new()
for name in ('fuse', 'tail'):
    shell.from_mesh(bpy.data.objects['JF-17 | ' + name].data)
tree = BVHTree.FromBMesh(shell)
stations = {}
for station in (7.5, 6.0, 4.5, 3.0, 1.5, 0.0, -1.5, -3.0, -4.5, -5.5):
    x = cg.x - station
    up = tree.ray_cast(Vector((x, 0, 20)), Vector((0, 0, -1)))[0]
    down = tree.ray_cast(Vector((x, 0, -20)), Vector((0, 0, 1)))[0]
    side = tree.ray_cast(Vector((x, -20, cg.z)), Vector((0, 1, 0)))[0]
    if up and down:
        stations[station] = [round(cg.z - up.z, 3), round(cg.z - down.z, 3), round(abs(side.y), 3) if side else None]
shell.free()
report['fuselage_top_bottom_halfwidth_body'] = stations


# --- Export ------------------------------------------------------------------
def export_lod(level):
    visibility, modifiers, links, selected = [], [], [], []
    # The donor carries 261,000 triangles, a third of them in wheels, nozzle
    # petals and gear fittings, so even the nearest level is reduced.
    skin, fittings = [.45, .2, .07, .012][level], [.25, .1, .03, .008][level]
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
            if obj.type == 'MESH' and len(obj.data.polygons) > 40:
                reduce = obj.modifiers.new('OFS authored LOD reduction', 'DECIMATE')
                reduce.ratio = skin if obj.name in SKIN else fittings
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
            for material in {m for o in selected if o.type == 'MESH' for m in o.data.materials if m}:
                for link in list(material.node_tree.links):
                    if link.from_node.type == 'TEX_IMAGE':
                        links.append((material, link.from_socket, link.to_socket))
                        material.node_tree.links.remove(link)
        bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_selection=True, export_apply=True,
                                  export_extras=True, export_cameras=False, export_lights=False,
                                  export_materials='EXPORT', export_yup=True)
        return {'level': level, 'triangles': triangles, 'nodes': len(selected), 'bytes': path.stat().st_size}
    finally:
        for material, source, target in links:
            material.node_tree.links.new(source, target)
        for obj, modifier in modifiers:
            obj.modifiers.remove(modifier)
        for obj, render, hidden in visibility:
            obj.hide_render = render
            obj.hide_set(hidden)


report['lods'] = [export_lod(level) for level in range(4)]
(EXPORT_DIR / 'lod_stats.json').write_text(json.dumps(report, indent=2) + '\n')
bpy.ops.wm.save_as_mainfile(filepath=str(WORKING_PATH))
print('JF-17 DONOR REPORT', json.dumps(report))
