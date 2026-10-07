"""Import, rig and export the Su-57 donor model as four native GLBs.

Donor: "Sukhoi Su-57 Felon - Fighter Jet - Free" by bohmerang (Sketchfab
59995d6f34ba4bb7990195be3a745fc5), CC BY-NC-SA 4.0. The derived GLBs remain
under that licence; see licenses/assets/SU57.md.

Run headless, never from inside the extracted donor directory:

  blender -b --factory-startup --disable-autoexec \
    --python scripts/su57_donor_import.py -- --source /path/su57.blend

The donor is roughly 10 units per metre with nose +X, port +Y and up +Z. Its
control surfaces, nozzles and undercarriage are already separate shells, so
this stage only groups them under OpenFlightSim pivots; the single welded
exception is each fin blade, which is cut from its fixed root fairing.
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
EXPORT_DIR = (options.output_dir or ROOT / 'assets/aircraft/su57').resolve()
WORKING_PATH = (options.working_output or ROOT / 'output/Su57_Felon_donor.blend').resolve()
EXPORT_DIR.mkdir(parents=True, exist_ok=True)
WORKING_PATH.parent.mkdir(parents=True, exist_ok=True)

bpy.ops.wm.open_mainfile(filepath=str(options.source.resolve()), load_ui=False, use_scripts=False)
source_scene = bpy.context.scene
depsgraph = bpy.context.evaluated_depsgraph_get()

# --- Targets shared with data/physics/su57.json -----------------------------
LENGTH_M, SPAN_M = 20.1, 14.1       # su57Geometry() length and wing_span.
ASSET_CG = Vector((10.95, 0, 2.45))  # Metres aft of the nose, starboard, above ground.
THRUST_LINE_M = 2.45 - .16          # Engine positions sit 0.16 m below the CG.
VECTOR_AXIS_FRD = {'L': (0, .8660254037844387, .5), 'R': (0, .8660254037844387, -.5)}

# --- Donor frame constants (donor units, before the span correction) --------
ENGINE_Y, ENGINE_Z = 14.19, -1.10   # Measured nozzle ring centres.
NOZZLE_FRONT_X = -60.8
STAB_PIVOT = (-62.0, 30.0, -1.65)
FIN_ROOT, FIN_TIP = (-55.1, 25.5, 3.8), (-55.7, 33.7, 21.35)
LEVCON_HINGE = ((36.6, 11.35, 1.35), (27.8, 26.6, -.1))
SLAT_HINGE = ((4.5, 31.9, -.2), (-31.2, 63.8, -.9))
FLAP_HINGE = ((-43.6, 32.1, -1.1), (-39.5, 49.9, -.25))
AILERON_HINGE = ((-39.5, 49.9, -.25), (-42.0, 63.85, -.5))
NOSE_TRUNNION = (46.2, 0, -1.4)
NOSE_STEER_X = 47.6
NOSE_BRACE_ANCHOR = (35.2, 0, -3.75)
NOSE_DOOR_HINGE_Y, NOSE_DOOR_HINGE_Z = 3.5, -4.8
NOSE_OLEO_TOP, NOSE_OLEO_BOTTOM = -11.0, -13.8  # Stretch zone of the nose oleo.
MAIN_TRUNNION = (-15.6, 21.3, -1.0)
PILOT_EYE = (65.0, 0, 11.4)


def donor_bmesh(name):
    obj = source_scene.objects[name]
    # Evaluate the donor's Auto Smooth node group so shading splits survive.
    data = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph), preserve_all_data_layers=True,
                                           depsgraph=depsgraph)
    bm = bmesh.new()
    bm.from_mesh(data)
    bm.transform(obj.matrix_world)
    bpy.data.meshes.remove(data)
    bm.faces.layers.int.new('ofs_part')  # Before any face reference is taken.
    bm.verts.ensure_lookup_table()
    bm.faces.ensure_lookup_table()
    return bm


class Shell:
    def __init__(self, verts):
        self.verts = verts
        self.faces = list({f for v in verts for f in v.link_faces})
        self.measure()

    def measure(self):
        self.lo = Vector([min(v.co[i] for v in self.verts) for i in range(3)])
        self.hi = Vector([max(v.co[i] for v in self.verts) for i in range(3)])
        self.center = (self.lo + self.hi) * .5
        self.size = self.hi - self.lo

    def inside(self, lo, hi, axes=(0, 1, 2), margin=.05):
        return all(self.lo[i] >= lo[i] - margin and self.hi[i] <= hi[i] + margin for i in axes)


def shells(bm):
    seen, result = set(), []
    for seed in bm.verts:
        if seed.index in seen:
            continue
        stack, verts = [seed], []
        seen.add(seed.index)
        while stack:
            vert = stack.pop()
            verts.append(vert)
            for edge in vert.link_edges:
                other = edge.other_vert(vert)
                if other.index not in seen:
                    seen.add(other.index)
                    stack.append(other)
        result.append(Shell(verts))
    return result


# --- Classification ---------------------------------------------------------
PARTS = {}  # name -> (donor bmesh, part id)
_next = [1]


def part(bm, name, faces):
    faces = list(faces)
    if not faces:
        raise RuntimeError(f'Donor layout changed: no geometry for {name}')
    layer = bm.faces.layers.int['ofs_part']
    for face in faces:
        face[layer] = _next[0]
    PARTS[name] = (bm, _next[0])
    _next[0] += 1


def one(candidates, name):
    candidates = list(candidates)
    if len(candidates) != 1:
        raise RuntimeError(f'Donor layout changed: {len(candidates)} candidates for {name}')
    return candidates[0]


def faces_of(group):
    return [f for s in group for f in s.faces]


airframe = donor_bmesh('SU57-airframe')
air_shells = shells(airframe)
main_shell = max(air_shells, key=lambda s: len(s.verts))
loose = [s for s in air_shells if s is not main_shell]
lenses = donor_bmesh('SU57-instrGlass')
lens_shells = shells(lenses)

for side, key in ((1, 'L'), (-1, 'R')):  # Donor +Y is port.
    def near(shell, x, y, z):  # Bounds within a box; y is given for the port side.
        lo = Vector((x[0], y[0], z[0]))
        hi = Vector((x[1], y[1], z[1]))
        mirrored = Shell.__new__(Shell)
        mirrored.lo = Vector((shell.lo.x, min(side * shell.lo.y, side * shell.hi.y), shell.lo.z))
        mirrored.hi = Vector((shell.hi.x, max(side * shell.lo.y, side * shell.hi.y), shell.hi.z))
        return mirrored.inside(lo, hi)

    wicks = lambda x, y, z: [s for s in loose if len(s.verts) < 25 and near(s, x, y, z)]
    levcon = one((s for s in loose if near(s, (26, 52), (9, 28), (-2, 4)) and s.size.x > 15), 'levcon')
    part(airframe, 'levcon_' + key, levcon.faces)
    slat = one((s for s in loose if near(s, (-33, 12), (31, 64.5), (-3, 2)) and s.size.y > 25), 'leading-edge flap')
    part(airframe, 'slat_' + key, slat.faces)
    flap = one((s for s in loose if near(s, (-53, -38), (31.5, 50.2), (-3, 1)) and s.size.y > 12), 'flaperon')
    canoe = one((s for s in loose if near(s, (-52.5, -41), (36, 40.6), (-3, 0)) and len(s.verts) > 30), 'flap canoe')
    part(airframe, 'flap_' + key, flap.faces + canoe.faces)
    aileron = one((s for s in loose if near(s, (-50, -38.5), (49.5, 64.3), (-2, 1)) and s.size.y > 10), 'aileron')
    canoe = one((s for s in loose if near(s, (-48.5, -39.5), (51.4, 55.1), (-2, 0)) and len(s.verts) > 30),
                'aileron canoe')
    part(airframe, 'aileron_' + key, aileron.faces + canoe.faces +
         faces_of(wicks((-49, -45.5), (57.5, 64.5), (-1, 0))))
    stab = one((s for s in loose if near(s, (-78.5, -43), (20.4, 50.7), (-3, 0)) and s.size.y > 25), 'stabilator')
    # The boom tail cone shares the stabilator's inboard trailing tip.
    cone = one((s for s in loose if near(s, (-78, -57), (23.5, 27), (-2.6, 3.2)) and len(s.verts) > 30), 'tail cone')
    part(airframe, 'stabilator_' + key, stab.faces + cone.faces +
         faces_of(wicks((-79, -73), (24.5, 48), (-2.7, -1.7))))
    # Each fin blade is welded to its fixed root fairing; only the blade moves.
    fin = one((s for s in loose if near(s, (-65, -26), (22, 35), (-2, 22)) and len(s.verts) > 100), 'fin')
    blade = [f for f in fin.faces if any(v.co.z > 10 for v in f.verts)]
    fittings = [s for s in loose if s is not fin and near(s, (-63, -53), (30, 32.5), (14, 17.5))]
    part(airframe, 'fin_' + key, blade + faces_of(fittings))
    fin_lens = [s for s in lens_shells if near(s, (-63, -59), (30, 32.5), (14, 17.5))]
    if fin_lens:
        part(lenses, 'fin_lens_' + key, faces_of(fin_lens))
    ring = one((s for s in loose if near(s, (-69, -60), (8, 20.5), (-7.5, 5.5)) and len(s.verts) > 100), 'nozzle')
    petals = [s for s in loose if near(s, (-74.5, -65.9), (8, 20.5), (-7.5, 5.5))]
    if len(petals) < 48:
        raise RuntimeError(f'Expected the nozzle petal rings, found {len(petals)} shells')
    part(airframe, 'nozzle_' + key, ring.faces + faces_of(petals))

gear = donor_bmesh('SU57-landingOn')
gear_shells = shells(gear)
lights = donor_bmesh('SU57-landingOnLight')
closed = donor_bmesh('SU57-landingOff')
closed_shells = shells(closed)

nose = [s for s in gear_shells if s.center.x > 30]
nose_tires = sorted(nose, key=lambda s: len(s.verts))[-2:]
nose_tire_lo = Vector([min(t.lo[i] for t in nose_tires) for i in range(3)])
nose_tire_hi = Vector([max(t.hi[i] for t in nose_tires) for i in range(3)])
# The forward doors are modelled shut with the gear down, as on the aircraft.
nose_forward_doors = [s for s in nose if s.lo.x > 49.9 and s.lo.z > -4.9]
nose_side_doors = {key: [s for s in nose if side * s.center.y > 2.2 and -8.2 < s.lo.z and s.hi.z < -3.4 and
                         s.lo.x > 44 and s.hi.x < 51 and s not in nose_forward_doors]
                   for side, key in ((1, 'L'), (-1, 'R'))}
taken = nose_forward_doors + nose_side_doors['L'] + nose_side_doors['R']
# The brace and the lines clipped along it reach back to the bay's aft wall.
nose_brace = [s for s in nose if s not in taken and s.lo.z > -8 and (s.hi.x < 45 or s.lo.x < 42)]
nose_wheel = [s for s in nose if s.inside(nose_tire_lo, nose_tire_hi, axes=(0, 2)) and abs(s.center.y) < 2.6]
taken += nose_brace + nose_wheel
nose_mudguard = [s for s in nose if s not in taken and s.hi.z < -17.9 and s.hi.x < 46.2]
taken += nose_mudguard
nose_steer = [s for s in nose if s not in taken and s.hi.z < -12]
nose_leg = [s for s in nose if s not in taken and s not in nose_steer]
part(gear, 'nose_forward_doors', faces_of(nose_forward_doors))
for key in 'LR':
    part(gear, 'nose_door_' + key, faces_of(nose_side_doors[key]))
part(gear, 'nose_brace', faces_of(nose_brace))
part(gear, 'nose_wheel', faces_of(nose_wheel))
part(gear, 'nose_steer', faces_of(nose_steer))
part(gear, 'nose_mudguard', faces_of(nose_mudguard))
part(gear, 'nose_leg', faces_of(nose_leg))
MAIN = {}
for side, key in ((1, 'L'), (-1, 'R')):
    group = [s for s in gear_shells if s.center.x <= 30 and side * s.center.y > 0]
    tire = max(group, key=lambda s: len(s.verts))
    wheel = [s for s in group if s.inside(tire.lo, tire.hi, axes=(0, 2)) and
             min(side * s.lo.y, side * s.hi.y) >= 22.3]
    door = [s for s in group if s not in wheel and max(side * s.lo.y, side * s.hi.y) > 23.8 and s.lo.z > -12]
    # Retraction jack anchored to the well; it stays behind the closed door.
    fittings = [s for s in group if s not in wheel and s not in door and s.center.x > -5]
    leg = [s for s in group if s not in wheel and s not in door and s not in fittings]
    MAIN[key] = (tire, door)
    part(gear, 'main_door_' + key, faces_of(door))
    part(gear, 'main_wheel_' + key, faces_of(wheel))
    part(gear, 'main_fittings_' + key, faces_of(fittings))
    part(gear, 'main_leg_' + key, faces_of(leg))

# --- Metric conversion ------------------------------------------------------
# The donor is 3 % wider for its length than the published 20.1 m by 14.1 m.
# Span is corrected first so that every later hinge, fold and fit is rigid.
SPAN_CORRECTION = (SPAN_M / main_shell.size.y) / (LENGTH_M / main_shell.size.x)
narrow = Matrix.Diagonal((1, SPAN_CORRECTION, 1, 1))
canopy = donor_bmesh('SU57-canopy')
cockpit = donor_bmesh('SU57-cockpit')
hud = donor_bmesh('SU57-hud')
for bm in (airframe, lenses, gear, lights, closed, canopy, cockpit, hud):
    bm.transform(narrow)
for shell in air_shells + lens_shells + gear_shells + closed_shells:
    shell.measure()


def P(point):  # A measured donor point after the span correction.
    return Vector((point[0], point[1] * SPAN_CORRECTION, point[2]))


nose_x = main_shell.hi.x
SCALE = LENGTH_M / main_shell.size.x
# Stand the aircraft so the nozzle centres lie on the simulated thrust line.
# The main legs rise slightly into their wells; the nose oleo extends until
# its tyres rest on the same ground plane.
GROUND = ENGINE_Z - THRUST_LINE_M / SCALE
MAIN_LIFT = GROUND - MAIN['L'][0].lo.z
NOSE_DROP = GROUND - min(t.lo.z for t in nose_tires)
CONVERT = Matrix(((-SCALE, 0, 0, nose_x * SCALE), (0, -SCALE, 0, 0), (0, 0, SCALE, -GROUND * SCALE), (0, 0, 0, 1)))


def point(donor):
    return CONVERT @ Vector(donor)


def direction(donor):
    donor = Vector(donor)
    return Vector((-donor.x, -donor.y, donor.z))


def gltf(vector):  # Blender Z-up to exported Y-up.
    return (vector.x, vector.z, -vector.y)


# --- Scene ------------------------------------------------------------------
scene = bpy.data.scenes.new('Su57 | donor rig')
if bpy.context.window:
    bpy.context.window.scene = scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1
aircraft = bpy.data.collections.new('Su57 | aircraft')
scene.collection.children.link(aircraft)
assembly = bpy.data.objects.new('Su57 | assembly', None)
aircraft.objects.link(assembly)
assembly['source'] = 'https://sketchfab.com/3d-models/sukhoi-su-57-felon-fighter-jet-free-59995d6f34ba4bb7990195be3a745fc5'
assembly['author'] = 'bohmerang'
assembly['license'] = 'CC-BY-NC-SA-4.0'
assembly['scale'] = SCALE
assembly['span_correction'] = SPAN_CORRECTION


def pivot(name, donor_location, channel='', donor_axis=(0, 1, 0), gain=1.0, donor_slide=(0, 0, 0), group=None,
          slide_metres=None):
    obj = bpy.data.objects.new('ofs_' + name, None)
    aircraft.objects.link(obj)
    obj.empty_display_size = .2
    obj.parent = group or assembly
    obj.location = point(donor_location) - (obj.parent.matrix_world.translation if group else Vector())
    obj['ofs_channel'] = channel
    obj['ofs_axis'] = gltf(direction(donor_axis).normalized())
    obj['ofs_gain'] = gain
    slide = Vector(slide_metres) if slide_metres else direction(donor_slide) * SCALE
    obj['ofs_slide'] = gltf(slide)
    bpy.context.view_layer.update()
    return obj


airframe_material = bpy.data.materials['airframe']
airframe_material.name = 'Su57 | airframe'
light_material = bpy.data.materials['lights']
light_material.name = 'Su57 | landing lights'


def glazing(name, color, alpha):
    # The donor's Glass BSDF and transmissive HUD have no runtime equivalent.
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    shader = next(n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    shader.inputs['Base Color'].default_value = (*color, 1)
    shader.inputs['Alpha'].default_value = alpha
    shader.inputs['Metallic'].default_value = .05
    shader.inputs['Roughness'].default_value = .08
    material.diffuse_color = (*color, alpha)
    material.use_backface_culling = False
    if 'BLENDED' in [i.identifier for i in material.bl_rna.properties['surface_render_method'].enum_items]:
        material.surface_render_method = 'BLENDED'
    return material


glass = glazing('Su57 | canopy glass', (.30, .24, .12), .22)  # Gold-tinted canopy coating.
glass['ofs_environment_reflection'] = .75
hud_material = glazing('Su57 | HUD combiner', (.30, .55, .38), .07)
for material in (airframe_material, light_material):
    material.use_backface_culling = False
    shader = next(n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    # Neutral dielectric defaults keep unsupported glTF extensions out of the export.
    shader.inputs['Transmission Weight'].default_value = 0
    shader.inputs['Specular IOR Level'].default_value = .5
    shader.inputs['Specular Tint'].default_value = (1, 1, 1, 1)
    shader.inputs['IOR'].default_value = 1.5


def mesh_object(name, bm, material, group=None, lod=3, edit=None, part_id=None):
    copy = bm.copy()
    layer = copy.faces.layers.int['ofs_part']
    if part_id is not None:
        bmesh.ops.delete(copy, geom=[f for f in copy.faces if f[layer] != part_id], context='FACES')
    copy.faces.layers.int.remove(layer)
    if edit:
        edit(copy)
    copy.transform(CONVERT)
    data = bpy.data.meshes.new('Su57 ' + name)
    copy.to_mesh(data)
    copy.free()
    data.materials.clear()
    data.materials.append(material)
    obj = bpy.data.objects.new('Su57 | ' + name, data)
    aircraft.objects.link(obj)
    obj['ofs_last_lod'] = lod
    obj.parent = group or assembly
    obj.matrix_parent_inverse = obj.parent.matrix_world.inverted()
    return obj


def named(name, material=None, group=None, lod=3, edit=None, label=None):
    bm, part_id = PARTS[name]
    return mesh_object(label or name.replace('_', ' '), bm, material or airframe_material, group, lod, edit, part_id)


def lift(amount):
    return lambda bm: bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, amount))


def extend_nose_oleo(bm):
    # Everything below the oleo follows the tyres; the piston between stretches.
    for vert in bm.verts:
        travel = (NOSE_OLEO_TOP - vert.co.z) / (NOSE_OLEO_TOP - NOSE_OLEO_BOTTOM)
        vert.co.z += NOSE_DROP * min(max(travel, 0), 1)


def fit_door(door_shells, cover_shells, edge_sign):
    """Hinge on one long edge of the closed skin that lays the open door flush.

    The donor models each door open; its closed counterpart is the matching
    strip of the flush gear-up skin. edge_sign picks that skin's +Y or -Y edge.
    """
    cover_verts = [v.co for s in cover_shells for v in s.verts]
    edge = max(edge_sign * v.y for v in cover_verts)
    rim = [v for v in cover_verts if edge_sign * v.y > edge - .4]
    y0, z0 = edge_sign * edge, sum(v.z for v in rim) / len(rim)
    skin = bmesh.new()
    lookup = {}
    for face in {f for s in cover_shells for f in s.faces}:
        skin.faces.new([lookup.setdefault(v, skin.verts.new(v.co)) for v in face.verts])
    cover = BVHTree.FromBMesh(skin)
    panel = max(door_shells, key=lambda s: len(s.faces))
    sample = [v.co.copy() for v in panel.verts]

    def cost(parameters):
        y, z, phi = parameters
        c, s, total = math.cos(phi), math.sin(phi), 0
        for q in sample:
            dy, dz = q.y - y, q.z - z
            moved = Vector((q.x, y + dy * c - dz * s, z + dy * s + dz * c))
            total += (cover.find_nearest(moved)[0] - moved).length_squared
        return total / len(sample)

    best = min(([y0, z0, math.radians(a)] for a in range(-180, 180, 2)), key=cost)
    error, step = cost(best), [.15, .15, .02]
    while max(step) > 1e-3:
        improved = False
        for axis in range(3):
            for sign in (1, -1):
                trial = list(best)
                trial[axis] += sign * step[axis]
                value = cost(trial)
                # The hinge stays near the skin edge; only fine seating is free.
                if value < error and abs(trial[0] - y0) < 1.5 and abs(trial[1] - z0) < 1.5:
                    best, error, improved = trial, value, True
        if not improved:
            step = [v * .5 for v in step]
    skin.free()
    xs = [v.x for v in sample]
    return Vector(((min(xs) + max(xs)) * .5, best[0], best[1])), best[2], math.sqrt(error)


report = {'source': assembly['source'], 'author': 'bohmerang', 'license': 'CC BY-NC-SA 4.0',
          'scale_m_per_unit': SCALE, 'span_correction': SPAN_CORRECTION,
          'main_gear_lift_m': MAIN_LIFT * SCALE, 'nose_oleo_extension_m': -NOSE_DROP * SCALE,
          'door_fit_rms_m': {}, 'door_hinge_deg': {}}

# Static airframe, cockpit, glazing and the closed forward nose doors.
mesh_object('airframe', airframe, airframe_material, part_id=0)
named('nose_forward_doors', lod=2)
mesh_object('cockpit', cockpit, airframe_material, lod=1)
mesh_object('HUD combiner', hud, hud_material, lod=0)
mesh_object('sensor lenses', lenses, glass, lod=1, part_id=0)
mesh_object('canopy', canopy, glass)

# Flight controls. Each hinge axis points to port, so a positive angle lowers
# the leading edge and raises the trailing edge on both sides.
for side, key in ((1, 'L'), (-1, 'R')):
    def hinge(name, channel, line, gain, lod=3):
        root, tip = (P((p[0], side * p[1], p[2])) for p in line)
        return named(name + '_' + key, group=pivot(name + '_' + key, root, channel, (tip - root) * side, gain), lod=lod)

    hinge('levcon', 'levcon', LEVCON_HINGE, -1)
    hinge('slat', 'slat', SLAT_HINGE, -1)
    hinge('flap', 'flap', FLAP_HINGE, -math.radians(20))
    hinge('aileron', 'aileron_' + key, AILERON_HINGE, 1)
    shaft = P((STAB_PIVOT[0], side * STAB_PIVOT[1], STAB_PIVOT[2]))
    named('stabilator_' + key, group=pivot('stabilator_' + key, shaft, 'elevator', (0, 1, 0), 1))
    root, tip = (P((p[0], side * p[1], p[2])) for p in (FIN_ROOT, FIN_TIP))
    fin_pivot = pivot('fin_' + key, root, 'rudder', tip - root, 1)
    named('fin_' + key, group=fin_pivot)
    if 'fin_lens_' + key in PARTS:
        named('fin_lens_' + key, glass, fin_pivot, lod=1)
    # Body forward/right/down to donor forward/port/up; the axis is already metric.
    frd = VECTOR_AXIS_FRD[key]
    centre = P((NOZZLE_FRONT_X, side * ENGINE_Y, ENGINE_Z))
    named('nozzle_' + key, group=pivot('nozzle_' + key, centre, 'vector_' + key, (frd[0], -frd[1], -frd[2]), 1))

# Undercarriage. Oleo travel slides each assembly along body-up, in metres.
trunnion = P(NOSE_TRUNNION)
suspension = pivot('suspension_nose', trunnion, 'compression_nose', gain=0, slide_metres=(0, 0, 1))
fold = pivot('nose_gear', trunnion, 'gear_fold', (0, 1, 0), -math.pi / 2, (0, 0, -.25), suspension)
named('nose_leg', group=fold, lod=2, edit=extend_nose_oleo)
landing_lights = mesh_object('landing lights', lights, light_material, fold, lod=1)
nose_axle = Vector(((nose_tire_lo.x + nose_tire_hi.x) * .5, 0, (nose_tire_lo.z + nose_tire_hi.z) * .5 + NOSE_DROP))
steer = pivot('nose_steering', Vector((NOSE_STEER_X, 0, nose_axle.z)), 'steering', (0, 0, 1), 1, group=fold)
named('nose_steer', group=steer, lod=2, edit=extend_nose_oleo)
spin = pivot('nose_wheel', nose_axle, 'nose_wheel', (0, 1, 0), 1, group=steer)
named('nose_wheel', group=spin, edit=extend_nose_oleo, label='nose tire')
# Stowed, the debris guard stays behind the wheel instead of under the belly.
guard = pivot('nose_mudguard', nose_axle, 'gear_fold', (0, 1, 0), math.pi / 2, group=steer)
named('nose_mudguard', group=guard, lod=1, edit=extend_nose_oleo)
# The drag brace swings up through its slot instead of following the leg.
named('nose_brace', group=pivot('nose_brace', P(NOSE_BRACE_ANCHOR), 'gear_fold', (0, 1, 0), -math.radians(25)), lod=2)
for side, key in ((1, 'L'), (-1, 'R')):
    xs = [v.co.x for s in nose_side_doors[key] for v in s.verts]
    door_hinge = Vector(((min(xs) + max(xs)) * .5, side * NOSE_DOOR_HINGE_Y, NOSE_DOOR_HINGE_Z))
    named('nose_door_' + key, group=pivot('nose_door_' + key, door_hinge, 'gear_door', (1, 0, 0), -side * math.pi / 2),
          lod=2)

    tire, door = MAIN[key]
    trunnion = P((MAIN_TRUNNION[0], side * MAIN_TRUNNION[1], MAIN_TRUNNION[2] + MAIN_LIFT))
    suspension = pivot('suspension_' + key, trunnion, 'compression_' + key, gain=0, slide_metres=(0, 0, 1))
    # The wheel swings forward into the well, then moves inboard behind the
    # blanked intake duct where the closed door hides it completely.
    fold = pivot('main_gear_' + key, trunnion, 'gear_fold', (0, 1, 0), -math.pi / 2, (0, -side * 9.0, -2.0),
                 suspension)
    named('main_leg_' + key, group=fold, lod=2, edit=lift(MAIN_LIFT))
    spin = pivot('main_wheel_' + key, tire.center + Vector((0, 0, MAIN_LIFT)), 'wheel', (0, 1, 0), 1, group=fold)
    named('main_wheel_' + key, group=spin, edit=lift(MAIN_LIFT), label='main tire ' + key)
    named('main_fittings_' + key, lod=1)
    main_skin = [s for s in closed_shells if s.center.x <= 30 and side * s.center.y > 0]
    door_hinge, angle, rms = fit_door(door, main_skin, side)
    report['door_hinge_deg']['main_' + key] = round(math.degrees(angle), 1)
    report['door_fit_rms_m']['main_' + key] = rms * SCALE
    named('main_door_' + key, group=pivot('main_door_' + key, door_hinge, 'gear_door', (1, 0, 0), angle), lod=2)

bpy.context.view_layer.update()


# --- Anchors for core/src/aircraft_definition.cpp ---------------------------
def body(donor):  # Donor units to body forward/right/down about the simulated CG.
    p = point(donor)
    return [round(ASSET_CG.x - p.x, 3), round(p.y - ASSET_CG.y, 3), round(ASSET_CG.z - p.z, 3)]


bounds = [Vector((1e9,) * 3), Vector((-1e9,) * 3)]
for obj in aircraft.objects:
    if obj.type == 'MESH':
        for vert in obj.data.vertices:
            world = obj.matrix_world @ vert.co
            for i in range(3):
                bounds[0][i] = min(bounds[0][i], world[i])
                bounds[1][i] = max(bounds[1][i], world[i])
tip = max(main_shell.verts, key=lambda v: v.co.y).co
main_tire = MAIN['L'][0]
report.update({
    'dimensions_m': {'length': bounds[1].x - bounds[0].x, 'span': bounds[1].y - bounds[0].y,
                     'height': bounds[1].z - bounds[0].z, 'ground': bounds[0].z, 'nose': bounds[0].x},
    'body_frd': {
        'pilot_eye': body(P(PILOT_EYE)),
        'nozzle_exit_L': body(P((-73.7, ENGINE_Y, ENGINE_Z))), 'nozzle_pivot_L': body(P((NOZZLE_FRONT_X, ENGINE_Y, ENGINE_Z))),
        'wingtip_L': body(tip),
        'nose_wheel': body(nose_axle),
        'main_wheel_L': body(main_tire.center + Vector((0, 0, MAIN_LIFT))),
        'fin_tip_L': body(P(FIN_TIP)),
        'nose_tip': body((nose_x, 0, 0))},
    'wheel_radius_m': {'main': main_tire.size.z * .5 * SCALE,
                       'nose': (nose_tire_hi.z - nose_tire_lo.z) * .5 * SCALE}})
fuselage = BVHTree.FromBMesh(airframe)
stations = {}
for station in (9.2, 7.6, 5.8, 3.8, 1.6, -.6, -2.8, -5.8):  # Body X of the fuselage hit spheres.
    x = nose_x - (ASSET_CG.x - station) / SCALE
    up = fuselage.ray_cast(Vector((x, 0, 80)), Vector((0, 0, -1)))[0]
    down = fuselage.ray_cast(Vector((x, 0, -80)), Vector((0, 0, 1)))[0]
    if up and down:
        stations[station] = [round(ASSET_CG.z - point(up).z, 3), round(ASSET_CG.z - point(down).z, 3)]
report['fuselage_top_bottom_body_z'] = stations


# --- Export -----------------------------------------------------------------
def export_lod(level):
    visibility, modifiers, links, selected = [], [], [], []
    ratio = [1, .6, .3, .1][level]
    path = EXPORT_DIR / f'su57_lod{level}.glb'
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
                reduce.ratio = max(ratio, .5 if 'canopy' in obj.name else ratio)
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
for scene_to_remove in [s for s in bpy.data.scenes if s is not scene]:
    bpy.data.scenes.remove(scene_to_remove)
bpy.ops.wm.save_as_mainfile(filepath=str(WORKING_PATH))
print('SU57 DONOR REPORT', json.dumps(report))
