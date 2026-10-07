"""Import, rig and export the Luftwaffe Typhoon donor model as four native GLBs.

Donor: "Eurofighter Typhoon - Fighter Jet - Free" by bohmerang (Sketchfab
992bcc8987964ca09d55410330aa8579), CC BY-NC-SA 4.0. The derived GLBs remain
under that licence; see licenses/assets/TYPHOON.md.

Run headless, never from inside the extracted donor directory:

  blender -b --factory-startup --disable-autoexec \
    --python scripts/typhoon_donor_import.py -- --source /path/Eurofighter.blend

The donor is 10 units per metre with nose +X, port +Y and up +Z. Its control
surfaces, nozzle petals and undercarriage are already separate shells, so this
stage only groups them under OpenFlightSim pivots; no surface is re-modelled.
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
EXPORT_DIR = (options.output_dir or ROOT / 'assets/aircraft/typhoon').resolve()
WORKING_PATH = (options.working_output or ROOT / 'output/Typhoon_Luftwaffe.blend').resolve()
EXPORT_DIR.mkdir(parents=True, exist_ok=True)
WORKING_PATH.parent.mkdir(parents=True, exist_ok=True)

bpy.ops.wm.open_mainfile(filepath=str(options.source.resolve()), load_ui=False, use_scripts=False)
source_scene = bpy.context.scene
depsgraph = bpy.context.evaluated_depsgraph_get()

# --- Donor frame constants (donor units) ----------------------------------
LENGTH_M = 15.96            # Published overall length; the only scale anchor.
CG_HEIGHT_M = 2.05          # typhoonConfig() gear contact below the CG.
MAIN_GEAR_AFT_OF_CG_M = 1.0 # typhoonConfig() gear_main_*.x
ENGINE_Y, ENGINE_Z = 4.95, -.47  # Measured nozzle ring centres.
HINGE_ROOT, HINGE_TIP = Vector((-15.5, 12.2, -3.45)), Vector((-18.2, 51.3, -2.8))
TRAILING_ROOT, TRAILING_TIP = -24.5, -22.4
RUDDER_LOW, RUDDER_HIGH = Vector((-30.4, 0, 8.2)), Vector((-46.3, 0, 31.8))


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
        self.lo = Vector([min(v.co[i] for v in verts) for i in range(3)])
        self.hi = Vector([max(v.co[i] for v in verts) for i in range(3)])
        self.center = (self.lo + self.hi) * .5

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


def tag(bm, faces, part):
    layer = bm.faces.layers.int['ofs_part']
    for face in faces:
        face[layer] = part


# --- Classification ---------------------------------------------------------
PARTS = {}  # name -> (donor bmesh, part id)
_next = [1]


def part(bm, name, faces):
    faces = list(faces)
    if not faces:
        raise RuntimeError(f'Donor layout changed: no geometry for {name}')
    tag(bm, faces, _next[0])
    PARTS[name] = (bm, _next[0])
    _next[0] += 1


airframe = donor_bmesh('Airframe')
air_shells = shells(airframe)
main_shell = max(air_shells, key=lambda s: len(s.verts))
main_faces = set(main_shell.faces)


def hinge_x(y):
    return HINGE_ROOT.x + (abs(y) - HINGE_ROOT.y) * (HINGE_TIP.x - HINGE_ROOT.x) / (HINGE_TIP.y - HINGE_ROOT.y)


def trailing_x(y):
    return TRAILING_ROOT + (abs(y) - HINGE_ROOT.y) * (TRAILING_TIP - TRAILING_ROOT) / (HINGE_TIP.y - HINGE_ROOT.y)


def elevon_face(face, side):
    # The flaperons are closed bodies welded to the rear spar wall at a few
    # coincident points. Spar-wall faces have every vertex on the hinge line.
    moving = False
    for vert in face.verts:
        x, y, z = vert.co
        if not (12.1 <= side * y <= 51.45 and -4.25 <= z <= -2.3):
            return False
        if not (trailing_x(y) - .3 <= x <= hinge_x(y) + .4):
            return False
        moving |= x < hinge_x(y) - .5 or x > hinge_x(y) + .1
    return moving


for side, key in ((1, 'L'), (-1, 'R')):  # Donor +Y is port.
    faces = [f for f in main_faces if elevon_face(f, side)]
    for shell in air_shells:
        # Aft halves of the two flap-track canoes travel with the surface.
        if shell is not main_shell and shell.hi.x < -14.9 and shell.lo.x > -23 and \
                19 < side * shell.center.y < 38 and shell.inside((-99, -99, -5.5), (99, 99, -2.9), axes=(2,)):
            faces += shell.faces
    part(airframe, 'elevon_' + key, faces)
    canard = [s for s in air_shells if s.lo.x > 66 and s.hi.x < 84.5 and 5 < side * s.center.y < 19 and
              s.hi.y - s.lo.y > 10]
    part(airframe, 'canard_' + key, canard[0].faces)
    petals = [s for s in air_shells if len(s.faces) == 5 and s.hi.x < -34 and s.lo.x > -41.8 and
              side * s.center.y > 0]
    if len(petals) != 12:
        raise RuntimeError(f'Expected twelve nozzle petals, found {len(petals)}')
    petals.sort(key=lambda s: math.atan2(s.center.z - ENGINE_Z, -s.center.y + side * ENGINE_Y))
    for index, shell in enumerate(petals):
        part(airframe, f'nozzle_{key}_{index:02d}', shell.faces)
rudder = [s for s in air_shells if s is not main_shell and abs(s.center.y) < .1 and s.hi.z - s.lo.z > 20]
part(airframe, 'rudder', rudder[0].faces)
airbrake = [s for s in air_shells if s is not main_shell and abs(s.center.y) < .1 and
            s.lo.x > 22 and s.hi.x < 45 and s.lo.z > 6.5 and s.hi.y - s.lo.y > 3]
part(airframe, 'airbrake', [f for s in airbrake for f in s.faces])

gear = donor_bmesh('LandingOn')
gear_shells = shells(gear)
lights = donor_bmesh('LandingLight')
light_shells = shells(lights)
closed = donor_bmesh('LandingOff')
closed_shells = shells(closed)

nose = [s for s in gear_shells if s.center.x > 30]
nose_tire = max(nose, key=lambda s: len(s.faces))
nose_door = [s for s in nose if s.lo.y >= 1.0 and s.hi.x <= 52.7]
nose_wheel = [s for s in nose if s.inside(nose_tire.lo, nose_tire.hi, axes=(0, 2))]
nose_steer = [s for s in nose if s not in nose_wheel and s not in nose_door and
              (s.hi.z <= -18.25 or (s.lo.z < -21 and s.hi.z < -18))]
nose_leg = [s for s in nose if s not in nose_door and s not in nose_wheel and s not in nose_steer]
part(gear, 'nose_door', [f for s in nose_door for f in s.faces])
part(gear, 'nose_wheel', [f for s in nose_wheel for f in s.faces])
part(gear, 'nose_steer', [f for s in nose_steer for f in s.faces])
part(gear, 'nose_leg', [f for s in nose_leg for f in s.faces])
MAIN = {}
for side, key in ((1, 'L'), (-1, 'R')):
    group = [s for s in gear_shells if s.center.x <= 30 and side * s.center.y > 0]
    tire = max(group, key=lambda s: len(s.faces))
    inner = [s for s in group if side * s.center.y < 6 and max(abs(s.lo.y), abs(s.hi.y)) <= 6.0]
    outer = [s for s in group if max(abs(s.lo.y), abs(s.hi.y)) > 22]
    wheel = [s for s in group if s.inside(tire.lo, tire.hi, axes=(0, 2)) and
             min(abs(s.lo.y), abs(s.hi.y)) >= 18.2]
    leg = [s for s in group if s not in inner and s not in outer and s not in wheel]
    MAIN[key] = (tire, inner, outer)
    part(gear, 'main_inner_door_' + key, [f for s in inner for f in s.faces])
    part(gear, 'main_outer_door_' + key, [f for s in outer for f in s.faces])
    part(gear, 'main_wheel_' + key, [f for s in wheel for f in s.faces])
    part(gear, 'main_leg_' + key, [f for s in leg for f in s.faces])
    part(lights, 'landing_light_' + key, [f for s in light_shells if side * s.center.y > 0 for f in s.faces])

# --- Metric conversion ------------------------------------------------------
nose_x = main_shell.hi.x
SCALE = LENGTH_M / (main_shell.hi.x - main_shell.lo.x)
# Stand the aircraft so the configured CG sits on the thrust line. The donor
# gear is modelled fully extended; both legs are raised into their wells until
# the tyres rest on that ground plane.
GROUND = ENGINE_Z - CG_HEIGHT_M / SCALE
NOSE_LIFT = GROUND - nose_tire.lo.z
MAIN_LIFT = GROUND - MAIN['L'][0].lo.z
CONVERT = Matrix(((-SCALE, 0, 0, nose_x * SCALE), (0, -SCALE, 0, 0), (0, 0, SCALE, -GROUND * SCALE), (0, 0, 0, 1)))


def point(donor):
    return CONVERT @ Vector(donor)


def direction(donor):
    donor = Vector(donor)
    return Vector((-donor.x, -donor.y, donor.z))


def gltf(vector):  # Blender Z-up to exported Y-up.
    return (vector.x, vector.z, -vector.y)


# --- Scene ------------------------------------------------------------------
scene = bpy.data.scenes.new('Typhoon | Luftwaffe donor rig')
if bpy.context.window:
    bpy.context.window.scene = scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1
aircraft = bpy.data.collections.new('Typhoon | aircraft')
scene.collection.children.link(aircraft)
assembly = bpy.data.objects.new('Typhoon | assembly', None)
aircraft.objects.link(assembly)
assembly['source'] = 'https://sketchfab.com/3d-models/eurofighter-typhoon-fighter-jet-free-992bcc8987964ca09d55410330aa8579'
assembly['author'] = 'bohmerang'
assembly['license'] = 'CC-BY-NC-SA-4.0'
assembly['scale'] = SCALE


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


airframe_material = bpy.data.materials['Airframe']
airframe_material.name = 'Typhoon | airframe'
light_material = bpy.data.materials['Lights']
light_material.name = 'Typhoon | landing lights'


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


glass = glazing('Typhoon | canopy glass', (.20, .26, .29), .16)
glass['ofs_environment_reflection'] = .75
hud_material = glazing('Typhoon | HUD combiner', (.30, .55, .38), .07)
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
    data = bpy.data.meshes.new('Typhoon ' + name)
    copy.to_mesh(data)
    copy.free()
    data.materials.clear()
    data.materials.append(material)
    obj = bpy.data.objects.new('Typhoon | ' + name, data)
    aircraft.objects.link(obj)
    obj['ofs_last_lod'] = lod
    obj.parent = group or assembly
    obj.matrix_parent_inverse = obj.parent.matrix_world.inverted()
    return obj


def named(name, material=None, group=None, lod=3, edit=None):
    bm, part_id = PARTS[name]
    return mesh_object(name.replace('_', ' '), bm, material or airframe_material, group, lod, edit, part_id)


def lift(amount, ceiling=None):
    def apply(bm):
        bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, amount))
        if ceiling is not None:  # Trim trunnions that would pierce the upper wing skin.
            geometry = list(bm.verts) + list(bm.edges) + list(bm.faces)
            bmesh.ops.bisect_plane(bm, geom=geometry, plane_co=(0, 0, ceiling), plane_no=(0, 0, 1),
                                   clear_outer=True, dist=1e-4)
    return apply


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
                # The hinge stays on the skin edge; only fine seating is free.
                if value < error and abs(trial[0] - y0) < .5 and abs(trial[1] - z0) < .5:
                    best, error, improved = trial, value, True
        if not improved:
            step = [v * .5 for v in step]
    skin.free()
    xs = [v.x for v in sample]
    return Vector(((min(xs) + max(xs)) * .5, best[0], best[1])), best[2], math.sqrt(error)


report = {'source': assembly['source'], 'author': 'bohmerang', 'license': 'CC BY-NC-SA 4.0',
          'scale_m_per_unit': SCALE, 'nose_gear_lift_m': NOSE_LIFT * SCALE, 'main_gear_lift_m': MAIN_LIFT * SCALE,
          'door_fit_rms_m': {}, 'door_hinge_deg': {}}

# Static airframe, cockpit, glazing and bare launch rails.
mesh_object('airframe', airframe, airframe_material, part_id=0)
mesh_object('launch rails', donor_bmesh('WeaponRails'), airframe_material, lod=1)
mesh_object('cockpit', donor_bmesh('Cockpit'), airframe_material, lod=1)
mesh_object('HUD combiner', donor_bmesh('HUD'), hud_material, lod=0)
mesh_object('lamp lenses', donor_bmesh('InstrGlass'), glass, lod=1)
mesh_object('canopy', donor_bmesh('Canopy'), glass)

# Flight controls. Axis and gain conventions match the retired original rig.
for side, key in ((1, 'L'), (-1, 'R')):
    root = Vector((HINGE_ROOT.x, side * HINGE_ROOT.y, HINGE_ROOT.z))
    tip = Vector((HINGE_TIP.x, side * HINGE_TIP.y, HINGE_TIP.z))
    named('elevon_' + key, group=pivot('elevon_' + key, root, 'elevon_' + key, tip - root, -side))
    shell = Shell([v for f in airframe.faces if f[airframe.faces.layers.int['ofs_part']] == PARTS['canard_' + key][1]
                   for v in f.verts])
    anhedral = math.atan2(1.9 + .9, shell.hi.y - shell.lo.y)
    # Both shafts point to port so one positive command pitches both the same way.
    shaft = Vector((0, math.cos(anhedral), -side * math.sin(anhedral)))
    named('canard_' + key, group=pivot('canard_' + key, (78.2, side * 5.6, .9), 'canard', shaft, -1))
    for index in range(12):
        name = f'nozzle_{key}_{index:02d}'
        verts = {v for f in airframe.faces if f[airframe.faces.layers.int['ofs_part']] == PARTS[name][1]
                 for v in f.verts}
        front = max(v.co.x for v in verts)
        root = sum((v.co for v in verts if v.co.x > front - .2), Vector()) / sum(v.co.x > front - .2 for v in verts)
        radial = Vector((0, root.y - side * ENGINE_Y, root.z - ENGINE_Z)).normalized()
        # Tangential root hinge; positive reheat opens the exit outward.
        tangent = Vector((1, 0, 0)).cross(radial)
        named(name, group=pivot(name, root, 'nozzle_' + key, tangent, -.075), lod=2)
axis = RUDDER_HIGH - RUDDER_LOW
named('rudder', group=pivot('rudder', RUDDER_LOW, 'rudder', axis, 1))
brake = Shell([v for f in airframe.faces if f[airframe.faces.layers.int['ofs_part']] == PARTS['airbrake'][1]
               for v in f.verts])
named('airbrake', group=pivot('airbrake', (brake.hi.x, 0, brake.lo.z + .2), 'spoiler', (0, 1, 0), 50 * math.pi / 180),
      lod=2)

# Undercarriage. Oleo travel slides each assembly along body-up, in metres.
# Flush gear-up skins guide the door hinges; the stowed nose wheel does not.
nose_skin = [s for s in closed_shells if s.center.x > 30 and s.hi.x - s.lo.x > 10]
suspension = pivot('suspension_nose', (52.25, 0, -8.3 + NOSE_LIFT), 'compression_nose', gain=0, slide_metres=(0, 0, 1))
fold = pivot('nose_gear', (52.25, 0, -8.3 + NOSE_LIFT), 'gear_fold', (0, 1, 0), math.pi / 2, (0, 0, 1.0), suspension)
named('nose_leg', group=fold, lod=2, edit=lift(NOSE_LIFT))
steer = pivot('nose_steering', (53.0, 0, nose_tire.center.z + NOSE_LIFT), 'steering', (0, 0, 1), 1, group=fold)
named('nose_steer', group=steer, lod=2, edit=lift(NOSE_LIFT))
spin = pivot('nose_wheel', nose_tire.center + Vector((0, 0, NOSE_LIFT)), 'nose_wheel', (0, 1, 0), 1, group=steer)
named('nose_wheel', group=spin, edit=lift(NOSE_LIFT))
hinge, angle, rms = fit_door(nose_door, nose_skin, 1)
report['door_fit_rms_m']['nose'] = rms * SCALE
named('nose_door', group=pivot('nose_door', hinge, 'gear_door', (1, 0, 0), angle), lod=2)
for side, key in ((1, 'L'), (-1, 'R')):
    tire, inner, outer = MAIN[key]
    trunnion = Vector((11.7, side * 18.5, -5.5 + MAIN_LIFT))
    suspension = pivot('suspension_' + key, trunnion, 'compression_' + key, gain=0, slide_metres=(0, 0, 1))
    # The wing is too thin for the folded side brace, so the stowed leg also
    # slides inboard under the fuselage where the closed doors hide it.
    fold = pivot('main_gear_' + key, trunnion, 'gear_fold', (1, 0, 0), -side * math.pi / 2, (0, -side * 9, .2),
                 suspension)
    named('main_leg_' + key, group=fold, lod=2, edit=lift(MAIN_LIFT, -2.6))
    named('landing_light_' + key, light_material, fold, lod=1, edit=lift(MAIN_LIFT))
    spin = pivot('main_wheel_' + key, tire.center + Vector((0, 0, MAIN_LIFT)), 'wheel', (0, 1, 0), 1, group=fold)
    named('main_wheel_' + key, group=spin, edit=lift(MAIN_LIFT))
    main_skin = [s for s in closed_shells if s.center.x <= 30 and side * s.center.y > 0]
    for label, door, edge in (('inner', inner, -side), ('outer', outer, side)):
        hinge, angle, rms = fit_door(door, main_skin, edge)
        report['door_hinge_deg'][f'main_{label}_{key}'] = round(math.degrees(angle), 1)
        report['door_fit_rms_m'][f'main_{label}_{key}'] = rms * SCALE
        named(f'main_{label}_door_{key}', group=pivot(f'main_{label}_door_{key}', hinge, 'gear_door', (1, 0, 0), angle),
              lod=2)

bpy.context.view_layer.update()


# --- Anchors for core/src/aircraft_definition.cpp ---------------------------
def body(donor, cg):  # Blender metres to body forward/right/down about the CG.
    p = point(donor)
    return [round(cg.x - p.x, 3), round(p.y - cg.y, 3), round(cg.z - p.z, 3)]


main_axle = point(MAIN['L'][0].center)
cg = Vector((main_axle.x - MAIN_GEAR_AFT_OF_CG_M, 0, CG_HEIGHT_M))
bounds = [Vector((1e9,) * 3), Vector((-1e9,) * 3)]
for obj in aircraft.objects:
    if obj.type == 'MESH':
        for vert in obj.data.vertices:
            world = obj.matrix_world @ vert.co
            for i in range(3):
                bounds[0][i] = min(bounds[0][i], world[i])
                bounds[1][i] = max(bounds[1][i], world[i])
tip_pod = max((s for s in air_shells if s is not main_shell), key=lambda s: s.hi.y)
report.update({
    'dimensions_m': {'length': bounds[1].x - bounds[0].x, 'span': bounds[1].y - bounds[0].y,
                     'height': bounds[1].z - bounds[0].z, 'ground': bounds[0].z, 'nose': bounds[0].x},
    'asset_cg_gltf': [round(cg.x, 3), round(cg.z, 3), 0],
    'body_frd': {
        'pilot_eye': body((67.0, 0, 11.4), cg),
        'exhaust_L': body((-41.7, ENGINE_Y, ENGINE_Z), cg), 'exhaust_R': body((-41.7, -ENGINE_Y, ENGINE_Z), cg),
        'wingtip_L': body((tip_pod.lo.x, tip_pod.center.y, tip_pod.center.z), cg),
        'nose_wheel': body(nose_tire.center + Vector((0, 0, NOSE_LIFT)), cg),
        'main_wheel_L': body(MAIN['L'][0].center + Vector((0, 0, MAIN_LIFT)), cg),
        'fin_tip': body((main_shell.lo.x, 0, main_shell.hi.z), cg),
        'nose_tip': body((nose_x, 0, 0), cg)},
    'wheel_radius_m': {'main': (MAIN['L'][0].hi.z - MAIN['L'][0].lo.z) * .5 * SCALE,
                       'nose': (nose_tire.hi.z - nose_tire.lo.z) * .5 * SCALE}})
fuselage = BVHTree.FromBMesh(airframe)
stations = {}
for station in (8.0, 6.5, 5.0, 3.5, 1.5, -.5, -2.5, -4.5):  # Body X of the fuselage hit spheres.
    x = nose_x - (cg.x - station) / SCALE
    up = fuselage.ray_cast(Vector((x, 0, 80)), Vector((0, 0, -1)))[0]
    down = fuselage.ray_cast(Vector((x, 0, -80)), Vector((0, 0, 1)))[0]
    if up and down:
        stations[station] = [round(cg.z - point(up).z, 3), round(cg.z - point(down).z, 3)]
report['fuselage_top_bottom_body_z'] = stations


# --- Export -----------------------------------------------------------------
def export_lod(level):
    visibility, modifiers, links, selected = [], [], [], []
    ratio = [1, .6, .3, .1][level]
    path = EXPORT_DIR / f'typhoon_lod{level}.glb'
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
print('TYPHOON DONOR REPORT', json.dumps(report))
