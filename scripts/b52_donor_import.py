"""Import, rig and export the B-52 Stratofortress donor model as four native GLBs.

Donor: "Boeing B-52 Stratofortress" by bohmerang (Sketchfab
38b0c64bd552431394efa8625d7f5144), CC BY 4.0. Credit and the list of changes
ship in licenses/assets/B52.md.

Run headless, never from inside the extracted donor directory:

  blender -b --factory-startup --disable-autoexec \
    --python scripts/b52_donor_import.py -- --source /path/source/B-52.blend

The textures are read from the archive's `textures` folder beside `source`.
The donor is metric with the nose toward +Y, starboard +X and up +Z. It is
modelled in flight: the undercarriage is up and no control surface is a part of
its own except the rudder and the tailplane. This stage builds the
undercarriage, cuts the flaps and the outboard roll surfaces out of the wing and
groups the moving parts under OpenFlightSim pivots.
"""
import argparse
import json
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

parser = argparse.ArgumentParser()
parser.add_argument('--project-root', type=Path, default=Path(__file__).resolve().parents[1])
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--working-output', type=Path)
options = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
ROOT = options.project_root.resolve()
SOURCE = options.source.resolve()
TEXTURES = SOURCE.parent.parent / 'textures'
EXPORT_DIR = (options.output_dir or ROOT / 'assets/aircraft/b52').resolve()
WORKING_PATH = (options.working_output or ROOT / 'output/B52_Stratofortress_donor.blend').resolve()
EXPORT_DIR.mkdir(parents=True, exist_ok=True)
WORKING_PATH.parent.mkdir(parents=True, exist_ok=True)

bpy.ops.wm.open_mainfile(filepath=str(SOURCE), load_ui=False, use_scripts=False)
scene = bpy.context.scene
scene.name = 'B-52 | donor rig'
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1
bpy.context.view_layer.update()

LENGTH_M = 48.5        # Published overall length; the only scale anchor.
HEIGHT_M = 12.4        # Published height on the ground, to the top of the fin.
CG_AFT_OF_NOSE_M = 22.0   # b52Config(): a quarter of the mean chord back.
CG_HEIGHT_M = 3.6         # b52Config() gear contact depth below the CG.
EXPECTED = {'EVS', 'EngineBlades', 'Engines', 'Fuselage', 'Glass', 'HorizontalStabilizers', 'RandomStuff',
            'RandomStuff2', 'RedLights', 'Rudder', 'VerticalStabilizer', 'WingLeft', 'WingRight'}
DONOR = {o.name: o for o in bpy.data.objects if o.type == 'MESH'}
if set(DONOR) != EXPECTED:
    raise RuntimeError(f'Donor layout changed: {sorted(set(DONOR) ^ EXPECTED)}')
for obj in list(bpy.data.objects):
    if obj.type != 'MESH':
        bpy.data.objects.remove(obj)

# The archive's texture files have no spaces in their names; the donor's do.
for image in bpy.data.images:
    if not image.filepath:
        continue
    path = TEXTURES / Path(image.filepath.replace('//', '')).name.replace(' ', '')
    if not path.is_file():
        raise RuntimeError(f'Donor texture missing: {path.name}')
    image.filepath = str(path)
    image.reload()
    image.pack()

# Bake every object's own placement into its mesh, then move the whole
# aircraft into authoring metres: X aft from the nose, Y starboard, Z up from
# the ground it will stand on.
for obj in DONOR.values():
    world = obj.matrix_world.copy()
    obj.parent = None
    obj.matrix_world = Matrix.Identity(4)
    obj.data.transform(world)


def bounds(pts):
    return (Vector([min(p[i] for p in pts) for i in range(3)]), Vector([max(p[i] for p in pts) for i in range(3)]))


low, high = bounds([v.co for obj in DONOR.values() for v in obj.data.vertices])
SCALE = LENGTH_M / (high.y - low.y)
GROUND = high.z - HEIGHT_M / SCALE  # Donor height of the ground under the fin's published height.
FRAME = Matrix(((0, -SCALE, 0, high.y * SCALE), (SCALE, 0, 0, 0), (0, 0, SCALE, -GROUND * SCALE), (0, 0, 0, 1)))
for obj in DONOR.values():
    obj.data.transform(FRAME)
    obj.data.update()
bpy.context.view_layer.update()
CG = Vector((CG_AFT_OF_NOSE_M, 0, CG_HEIGHT_M))


def gltf(vector):  # Blender Z-up to exported Y-up.
    return (vector.x, vector.z, -vector.y)


def points(name):
    return [v.co.copy() for v in DONOR[name].data.vertices]


# --- Materials ---------------------------------------------------------------
# The donor's materials are kept as they are: one texture each, or a flat
# colour. Only the glazing is remade, so it is drawn as glass.
def plain(name, colour, rough=.6, metal=0., alpha=None, reflection=None):
    result = bpy.data.materials.new('B-52 | ' + name)
    result.use_nodes = True
    shader = next(n for n in result.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    shader.inputs['Base Color'].default_value = (*colour, 1)
    shader.inputs['Roughness'].default_value = rough
    shader.inputs['Metallic'].default_value = metal
    result.diffuse_color = (*colour, alpha or 1)
    if alpha is not None:
        shader.inputs['Alpha'].default_value = alpha
        if 'BLENDED' in [i.identifier for i in result.bl_rna.properties['surface_render_method'].enum_items]:
            result.surface_render_method = 'BLENDED'
    if reflection:
        result['ofs_environment_reflection'] = reflection
    result.use_backface_culling = False
    return result


glass_material = plain('glazing', (.16, .21, .24), rough=.08, metal=.05, alpha=.30, reflection=.7)
tyre_material = plain('tyre', (.035, .035, .038), rough=.9)
strut_material = plain('undercarriage', (.62, .64, .66), rough=.45, metal=.6)
DONOR['Glass'].data.materials.clear()
DONOR['Glass'].data.materials.append(glass_material)
for material in bpy.data.materials:
    if material.users and not material.name.startswith('B-52 | '):
        material.name = 'B-52 | ' + material.name
        material.use_backface_culling = False

# --- Scene -------------------------------------------------------------------
aircraft = bpy.data.collections.new('B-52 | aircraft')
scene.collection.children.link(aircraft)
assembly = bpy.data.objects.new('B-52 | assembly', None)
aircraft.objects.link(assembly)
assembly['source'] = 'https://sketchfab.com/3d-models/boeing-b-52-stratofortress-38b0c64bd552431394efa8625d7f5144'
assembly['author'] = 'bohmerang'
assembly['license'] = 'CC-BY-4.0'
assembly['scale'] = SCALE


def pivot(name, location, channel='', axis=(0, 1, 0), gain=1.0, slide=(0, 0, 0), group=None):
    obj = bpy.data.objects.new('ofs_' + name, None)
    aircraft.objects.link(obj)
    obj.empty_display_size = .5
    obj.parent = group or assembly
    bpy.context.view_layer.update()
    obj.location = Vector(location) - (obj.parent.matrix_world.translation if group else Vector())
    obj['ofs_channel'] = channel
    obj['ofs_axis'] = gltf(Vector(axis).normalized())
    obj['ofs_gain'] = gain
    obj['ofs_slide'] = gltf(Vector(slide))
    bpy.context.view_layer.update()
    return obj


def adopt(obj, label, group=None, lod=3):
    obj.name = 'B-52 | ' + label
    obj.data.name = obj.name
    for collection in list(obj.users_collection):
        collection.objects.unlink(obj)
    aircraft.objects.link(obj)
    obj['ofs_last_lod'] = lod
    obj.parent = group or assembly
    bpy.context.view_layer.update()
    obj.matrix_parent_inverse = obj.parent.matrix_world.inverted()
    return obj


def split(name, pick, label):
    """Moves the faces `pick` accepts out of a donor object into a new one."""
    source = DONOR[name]
    bm = bmesh.new()
    bm.from_mesh(source.data)
    chosen = [f for f in bm.faces if pick(f)]
    if not chosen:
        raise RuntimeError(f'Donor layout changed: nothing of {name} to make the {label} from')
    part = bmesh.new()
    lookup = {}
    layers = [(bm.loops.layers.uv[i], part.loops.layers.uv.new(bm.loops.layers.uv[i].name)) for i in range(len(bm.loops.layers.uv))]
    for face in chosen:
        verts = []
        for v in face.verts:
            if v.index not in lookup:
                lookup[v.index] = part.verts.new(v.co)
            verts.append(lookup[v.index])
        try:
            made = part.faces.new(verts)
        except ValueError:
            continue
        made.material_index = face.material_index
        made.smooth = face.smooth
        for a, b in zip(face.loops, made.loops):
            for old, new in layers:
                b[new].uv = a[old].uv
    bmesh.ops.delete(bm, geom=chosen, context='FACES')
    bm.to_mesh(source.data)
    bm.free()
    mesh = bpy.data.meshes.new(label)
    part.to_mesh(mesh)
    part.free()
    for material in source.data.materials:
        mesh.materials.append(material)
    obj = bpy.data.objects.new(label, mesh)
    scene.collection.objects.link(obj)
    return obj


def islands(bm):
    seen, result = set(), []
    for face in bm.faces:
        if face.index in seen:
            continue
        stack, group = [face], []
        seen.add(face.index)
        while stack:
            current = stack.pop()
            group.append(current)
            for edge in current.edges:
                for other in edge.link_faces:
                    if other.index not in seen:
                        seen.add(other.index)
                        stack.append(other)
        result.append(group)
    return result


report = {'source': assembly['source'], 'author': 'bohmerang', 'license': 'CC BY 4.0', 'scale_m_per_unit': SCALE}
PORT = Vector((0, -1, 0))  # A positive turn about a port-pointing hinge raises a trailing edge.

# Wings. The donor's flaps are separate shells tucked under the trailing edge;
# they are found by the planform they cover. The outboard roll surfaces are cut
# from the wing skin behind a hinge line 24% of the chord ahead of the trailing
# edge. The B-52H itself rolls with spoilers and has no ailerons: the surface
# here stands in for them, so the model answers the stick visibly.
FLAPS = ((2.2, 11.4), (12.4, 19.0))   # Spanwise extent of each flap, metres from the centreline.
ROLL = (19.6, 24.2)
for name, side, sign in (('WingLeft', 'L', -1), ('WingRight', 'R', 1)):
    wing = DONOR[name]
    bm = bmesh.new()
    bm.from_mesh(wing.data)
    bm.faces.ensure_lookup_table()
    # The wing proper is its few large shells; everything small is a fitting.
    shells = islands(bm)
    flap_faces = {}
    skin = []
    for shell in shells:
        pts = [v.co for f in shell for v in f.verts]
        lo, hi = bounds(pts)
        inner, outer = min(abs(lo.y), abs(hi.y)), max(abs(lo.y), abs(hi.y))
        area = sum(f.calc_area() for f in shell)
        for index, (a, b) in enumerate(FLAPS):
            # A flap shell lies wholly inside its bay and is far bigger than a fitting.
            if inner > a - .2 and outer < b + .2 and outer - inner > 3 and area > 4:
                flap_faces.setdefault(index, []).extend(f.index for f in shell)
                break
        else:
            if outer - inner > 6:
                skin.extend(shell)
    if len(flap_faces) != 2:
        raise RuntimeError(f'Donor layout changed: {len(flap_faces)} flaps found on {name}')
    tree = BVHTree.FromBMesh(bm)

    def trailing(span):  # The trailing edge at a spanwise station: the aftmost skin there.
        best = None
        for face in skin:
            for edge in face.edges:
                a, b = edge.verts[0].co, edge.verts[1].co
                if (a.y - sign * span) * (b.y - sign * span) > 0 or a.y == b.y:
                    continue
                p = a.lerp(b, (sign * span - a.y) / (b.y - a.y))
                if best is None or p.x > best.x:
                    best = p
        return best

    def leading(span):
        best = None
        for face in skin:
            for edge in face.edges:
                a, b = edge.verts[0].co, edge.verts[1].co
                if (a.y - sign * span) * (b.y - sign * span) > 0 or a.y == b.y:
                    continue
                p = a.lerp(b, (sign * span - a.y) / (b.y - a.y))
                if best is None or p.x < best.x:
                    best = p
        return best

    ends = []
    for span in ROLL:
        aft, fore = trailing(span), leading(span)
        ends.append(Vector((aft.x - .24 * (aft.x - fore.x), sign * span, aft.z)))
    hinge = (ends[1] - ends[0]).normalized()
    aft_normal = Vector((0, 0, 1)).cross(hinge) * sign  # Horizontal, pointing aft of the hinge.
    if aft_normal.x < 0:
        aft_normal = -aft_normal
    bm.free()
    flaps = []
    for index in sorted(flap_faces):
        chosen = set(flap_faces[index])
        flaps.append(split(name, lambda f, chosen=chosen: f.index in chosen, f'flap {index} {side}'))
    # Cut the skin along the hinge and at both ends of the roll surface.
    bm = bmesh.new()
    bm.from_mesh(wing.data)
    for origin, normal in ((ends[0], aft_normal), (ends[0], Vector((0, sign, 0))), (ends[1], Vector((0, sign, 0)))):
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=origin, plane_no=normal, dist=1e-5)
    bm.to_mesh(wing.data)
    bm.free()

    def roll_surface(face):
        c = face.calc_center_median()
        return ROLL[0] < sign * c.y < ROLL[1] and (c - ends[0]).dot(aft_normal) > 0 and face.calc_area() > .02

    surface = split(name, roll_surface, f'roll surface {side}')
    mid = (ends[0] + ends[1]) * .5
    adopt(surface, f'roll surface {side}', pivot(f'roll_surface_{side}', mid, f'aileron_{side}', PORT if sign > 0 else PORT, 1))
    for index, flap in enumerate(flaps):
        pts = [v.co for v in flap.data.vertices]
        lo, hi = bounds(pts)
        # The flap turns about its own forward edge, found at both ends of it.
        root = [p for p in pts if abs(p.y) < min(abs(lo.y), abs(hi.y)) + .4]
        tip = [p for p in pts if abs(p.y) > max(abs(lo.y), abs(hi.y)) - .4]
        a, b = min(root, key=lambda p: p.x), min(tip, key=lambda p: p.x)
        axis = (a - b) if sign > 0 else (b - a)  # Port-pointing on both wings.
        adopt(flap, f'flap {index} {side}', pivot(f'flap_{index}_{side}', (a + b) * .5, 'flap', axis, -math.radians(30),
                                                 (.5, 0, -.12)))
    adopt(wing, f'wing {side}')
    report.setdefault('roll_hinge', {})[side] = [[round(v, 3) for v in end] for end in ends]

# Tail. The whole tailplane is one donor object: it turns about a lateral shaft
# at 40% of its root chord, by a third of the commanded elevator angle.
pts = points('HorizontalStabilizers')
lo, hi = bounds(pts)
root = [p for p in pts if abs(p.y) < 1.0]
shaft = Vector((min(p.x for p in root) + .4 * (max(p.x for p in root) - min(p.x for p in root)), 0,
                sum(p.z for p in root) / len(root)))
adopt(DONOR['HorizontalStabilizers'], 'tailplane', pivot('tailplane', shaft, 'elevator', PORT, .35))
pts = points('Rudder')
lo, hi = bounds(pts)
low_end = min((p for p in pts if p.z < lo.z + .3), key=lambda p: p.x)
high_end = min((p for p in pts if p.z > hi.z - .3), key=lambda p: p.x)
adopt(DONOR['Rudder'], 'rudder', pivot('rudder', low_end, 'rudder', high_end - low_end, 1))

# Fans. Every engine's blades and spinner disc are shells of one donor object;
# each engine's turn about its own centre, four to an engine slot.
bm = bmesh.new()
bm.from_mesh(DONOR['EngineBlades'].data)
centres = []
for shell in islands(bm):
    lo, hi = bounds([v.co for f in shell for v in f.verts])
    if (hi.y - lo.y) > .9 and (hi.z - lo.z) > .9:  # A fan disc, not a single blade.
        centres.append((lo + hi) * .5)
bm.free()
if len(centres) != 8:
    raise RuntimeError(f'Donor layout changed: {len(centres)} engine fans found')
for number, centre in enumerate(sorted(centres, key=lambda c: c.y)):
    side = 'L' if centre.y < 0 else 'R'

    def of_engine(face, centre=centre):
        c = face.calc_center_median()
        return abs(c.y - centre.y) < .68 and abs(c.z - centre.z) < .68

    fan = split('EngineBlades', of_engine, f'fan {number + 1}')
    adopt(fan, f'fan {number + 1}', pivot(f'fan_{number + 1}', centre, 'fan_' + side, (1, 0, 0), 1), lod=1)
if DONOR['EngineBlades'].data.polygons:
    raise RuntimeError('Donor layout changed: fan blades left over')
bpy.data.objects.remove(DONOR.pop('EngineBlades'))
report['fans_authoring'] = [[round(v, 3) for v in c] for c in sorted(centres, key=lambda c: c.y)]

for name, label, lod in (('Fuselage', 'fuselage', 3), ('Engines', 'engine pods', 3), ('VerticalStabilizer', 'fin', 3),
                         ('Glass', 'glazing', 3), ('EVS', 'sensor blisters', 2), ('RandomStuff', 'fittings', 1),
                         ('RandomStuff2', 'antennas', 1), ('RedLights', 'beacons', 2)):
    adopt(DONOR[name], label, lod=lod)


# --- Undercarriage -----------------------------------------------------------
# The donor has none. The B-52 stands on four two-wheel trucks in line under
# the fuselage, the forward pair steering, and on an outrigger near each
# wingtip. They are built here as plain struts and wheels.
def cylinder(label, a, b, radius, material, segments=12, caps=True):
    mesh = bpy.data.meshes.new(label)
    bm = bmesh.new()
    axis = (Vector(b) - Vector(a))
    made = bmesh.ops.create_cone(bm, cap_ends=caps, segments=segments, radius1=radius, radius2=radius, depth=axis.length)
    bmesh.ops.transform(bm, matrix=Matrix.Translation((Vector(a) + Vector(b)) * .5) @ axis.to_track_quat('Z', 'Y').to_matrix().to_4x4(),
                        verts=made['verts'])
    for face in bm.faces:
        face.smooth = len(face.verts) == 4
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(material)
    obj = bpy.data.objects.new(label, mesh)
    scene.collection.objects.link(obj)
    return obj


def join(label, parts):
    bpy.ops.object.select_all(action='DESELECT')
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    parts[0].name = label
    return parts[0]


fuselage_bm = bmesh.new()
fuselage_bm.from_mesh(bpy.data.objects['B-52 | fuselage'].data)
fuselage_tree = BVHTree.FromBMesh(fuselage_bm)
MAIN_RADIUS, MAIN_WIDTH, OUTRIGGER_RADIUS = .70, .40, .40
TRACK = 1.05      # Truck centres from the centreline; the real 1.25 m does not fit this fuselage.
WHEEL_OFFSET = .36
TRUCKS = (('forward', CG.x - 10.2, True), ('aft', CG.x + 5.0, False))
for label, station, steered in TRUCKS:
    for side, sign in (('L', -1), ('R', 1)):
        axle = Vector((station, sign * TRACK, MAIN_RADIUS))
        belly = fuselage_tree.ray_cast(Vector((station, sign * TRACK, -5)), Vector((0, 0, 1)))[0]
        if belly is None:
            raise RuntimeError('Donor layout changed: no fuselage over a main truck')
        trunnion = Vector((station, sign * TRACK, belly.z + .55))
        # The truck swings up fore and aft into the fuselage: forward trucks
        # aft, aft trucks forward, as the real ones lie.
        fold = pivot(f'{label}_gear_{side}', trunnion, 'gear_fold', PORT, (-1 if steered else 1) * math.pi / 2,
                     (0, -sign * .38, .25))
        leg = join(f'{label} leg {side}', [
            cylinder('strut', trunnion, axle + Vector((0, 0, .05)), .13, strut_material),
            cylinder('axle', axle - Vector((0, WHEEL_OFFSET + .05, 0)), axle + Vector((0, WHEEL_OFFSET + .05, 0)), .09, strut_material, 8)])
        adopt(leg, f'{label} leg {side}', fold, lod=2)
        group = fold
        if steered:
            group = pivot(f'{label}_steering_{side}', axle, 'steering', (0, 0, 1), 1, group=fold)
        spin = pivot(f'{label}_wheel_{side}', axle, 'nose_wheel' if steered else 'wheel', PORT, 1, group=group)
        wheels = []
        for offset in (-WHEEL_OFFSET, WHEEL_OFFSET):
            centre = axle + Vector((0, offset, 0))
            half = Vector((0, MAIN_WIDTH / 2, 0))
            wheels.append(cylinder('tyre', centre - half, centre + half, MAIN_RADIUS, tyre_material, 20))
            wheels.append(cylinder('hub', centre - half * 1.04, centre + half * 1.04, MAIN_RADIUS * .52, strut_material, 12))
        adopt(join(f'{label} wheels {side}', wheels), f'{label} wheels {side}', spin, lod=2)
fuselage_bm.free()
OUTRIGGER_SPAN, OUTRIGGER_AFT = 22.6, 6.5
for side, sign in (('L', -1), ('R', 1)):
    wing = bpy.data.objects[f'B-52 | wing {side}']
    bm = bmesh.new()
    bm.from_mesh(wing.data)
    tree = BVHTree.FromBMesh(bm)
    station = CG.x + OUTRIGGER_AFT
    under = tree.ray_cast(Vector((station, sign * OUTRIGGER_SPAN, -5)), Vector((0, 0, 1)))[0]
    bm.free()
    if under is None:
        raise RuntimeError('Donor layout changed: no wing over an outrigger')
    # Clear of the ground by the height the simulated contact stands above it.
    axle = Vector((station, sign * OUTRIGGER_SPAN, OUTRIGGER_RADIUS + .35))
    trunnion = Vector((station, sign * OUTRIGGER_SPAN, under.z + .05))
    # It swings outward and up, to lie half sunk in the underside of the wing.
    fold = pivot(f'outrigger_{side}', trunnion, 'gear_fold', (1, 0, 0), sign * math.radians(88), (0, 0, .2))
    half = Vector((0, .13, 0))
    leg = join(f'outrigger {side}', [
        cylinder('strut', trunnion, axle, .07, strut_material, 8),
        cylinder('tyre', axle - half, axle + half, OUTRIGGER_RADIUS, tyre_material, 16),
        cylinder('hub', axle - half * 1.05, axle + half * 1.05, OUTRIGGER_RADIUS * .5, strut_material, 10)])
    adopt(leg, f'outrigger {side}', fold, lod=1)
    report.setdefault('outrigger_authoring', {})[side] = [round(v, 3) for v in axle]

leftover = [o.name for o in scene.collection.objects if o.type == 'MESH']
if leftover:
    raise RuntimeError(f'Unplaced objects: {leftover}')
bpy.context.view_layer.update()


# --- Anchors for core/src/b52.cpp and aircraft_definition.cpp -----------------
def placed(label):
    obj = bpy.data.objects['B-52 | ' + label]
    return [obj.matrix_world @ v.co for v in obj.data.vertices]


def body(point):  # Authoring metres to body forward/right/down about the CG.
    return [round(CG.x - point.x, 3), round(point.y - CG.y, 3), round(CG.z - point.z, 3)]


every = [obj.matrix_world @ v.co for obj in aircraft.objects if obj.type == 'MESH' for v in obj.data.vertices]
all_lo, all_hi = bounds(every)
pods = placed('engine pods')
glazing_lo, glazing_hi = bounds(placed('glazing'))
fuselage_lo, fuselage_hi = bounds(placed('fuselage'))
wing_points = placed('wing L')
tip = min(wing_points, key=lambda p: p.y)
fans = sorted(centres, key=lambda c: c.y)
report.update({
    'dimensions_m': {'length': all_hi.x - all_lo.x, 'span': all_hi.y - all_lo.y, 'height': all_hi.z - all_lo.z,
                     'ground': all_lo.z, 'nose': all_lo.x},
    'asset_cg_gltf': [round(CG.x, 3), round(CG.z, 3), 0],
    'body_frd': {
        'fans': [body(c) for c in fans],
        'pods': [body(p) for p in bounds(pods)],
        'glazing': [body(glazing_lo), body(glazing_hi)],
        'fuselage': [body(fuselage_lo), body(fuselage_hi)],
        'wingtip_L': body(tip),
        'fin_tip': body(Vector((all_hi.x, 0, all_hi.z))),
        'forward_truck': body(Vector((CG.x - 10.2, TRACK, 0))), 'aft_truck': body(Vector((CG.x + 5.0, TRACK, 0)))},
    'wheel_radius_m': {'main': MAIN_RADIUS, 'outrigger': OUTRIGGER_RADIUS}})
shell = bmesh.new()
for label in ('fuselage', 'wing L', 'engine pods'):
    shell.from_mesh(bpy.data.objects['B-52 | ' + label].data)
tree = BVHTree.FromBMesh(shell)
sections = {}
for station in (20, 16, 12, 8, 4, 0, -4, -8, -12, -16, -20, -24):
    at = CG.x - station
    up = tree.ray_cast(Vector((at, .05, 30)), Vector((0, 0, -1)))[0]
    down = tree.ray_cast(Vector((at, .05, -30)), Vector((0, 0, 1)))[0]
    flank = tree.ray_cast(Vector((at, -40, CG.z)), Vector((0, 1, 0)))[0]
    if up and down:
        sections[station] = [round(CG.z - up.z, 3), round(CG.z - down.z, 3), round(abs(flank.y), 3) if flank else None]
# Exhausts: the aft end of each pod, found from behind along each fan's axis.
exhausts = []
for fan in fans:
    hit = tree.ray_cast(Vector((fan.x + 30, fan.y, fan.z)), Vector((-1, 0, 0)))[0]
    exhausts.append(body(hit) if hit else None)
shell.free()
report['fuselage_top_bottom_halfwidth_body'] = sections
report['body_frd']['exhausts'] = exhausts


# --- Export ------------------------------------------------------------------
def export_lod(level):
    visibility, modifiers, links, selected = [], [], [], []
    ratio = [1, .6, .32, .16][level]
    path = EXPORT_DIR / f'b52_lod{level}.glb'
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
                reduce.ratio = max(ratio, .5 if 'glazing' in obj.name else ratio)
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
print('B-52 DONOR REPORT', json.dumps(report))
