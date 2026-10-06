"""Import bohmerang's CC BY-NC-SA Su-57 using Blender's native glTF exporter.

blender --background --disable-autoexec --python scripts/import_su57_sketchfab.py -- \
  --source /path/to/extracted/source/su57.blend

The supplied archive is never edited. This pipeline uses only the replacement's
meshes and packed images; it does not open the retired CGTrader scene. Rigging,
dimension fitting and gear contact corrections are visual engineering estimates.
"""
import argparse
from collections import defaultdict
from contextlib import redirect_stdout
import hashlib
import io
import json
import math
from pathlib import Path
import sys

import bpy
import bmesh
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from physics_geometry import blender_point, hinge_point, physical_parameters
from rig_aircraft import parent_keep_world, pivot

ROOT = Path(__file__).resolve().parents[1]
SOURCE_URL = 'https://sketchfab.com/3d-models/sukhoi-su-57-felon-fighter-jet-free-59995d6f34ba4bb7990195be3a745fc5'
LICENSE_URL = 'https://creativecommons.org/licenses/by-nc-sa/4.0/'
SOURCE_OBJECTS = ('SU57-airframe', 'SU57-canopy', 'SU57-cockpit', 'SU57-hud',
                  'SU57-instrGlass', 'SU57-landingOn', 'SU57-landingOnLight')


def bounds(points):
    return Vector(tuple(min(p[i] for p in points) for i in range(3))), Vector(tuple(max(p[i] for p in points) for i in range(3)))


def components(mesh):
    """Identify authored disconnected shells without changing topology or UVs."""
    owners = list(range(len(mesh.vertices)))
    def find(index):
        while owners[index] != index:
            owners[index] = owners[owners[index]]
            index = owners[index]
        return index
    for edge in mesh.edges:
        a, b = map(find, edge.vertices)
        owners[a] = b
    groups = defaultdict(list)
    for polygon in mesh.polygons:
        groups[find(polygon.vertices[0])].append(polygon.index)
    return list(groups.values())


def subset(source, faces, name):
    """Copy a face subset with its original UVs and materials."""
    data = source.data.copy()
    bm = bmesh.new()
    bm.from_mesh(data)
    bm.faces.ensure_lookup_table()
    keep = set(faces)
    bmesh.ops.delete(bm, geom=[face for face in bm.faces if face.index not in keep], context='FACES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    bm.to_mesh(data)
    bm.free()
    data.update()
    obj = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def cut_surface_boundaries(mesh):
    """Split welded polygons at every control-region boundary before selection.

    Selecting a long triangle by its centroid leaves a jagged control seam.
    Bisecting first interpolates the original UV data at a straight hinge cut.
    """
    bm = bmesh.new()
    bm.from_mesh(mesh)
    planes = []
    for axis, stations in enumerate(((-65, -60, -43, -31, -29, -26, -25, 26, 51),
                                     (-40, -36, -30, -27, -24, -20.5, -20, -9.7, -8,
                                      8, 9.7, 20, 20.5, 24, 27, 30, 36, 40),
                                     (-2, 3, 3.5, 4, 5))):
        for station in stations:
            point = Vector((0, 0, 0))
            normal = Vector((0, 0, 0))
            point[axis] = station
            normal[axis] = 1
            planes.append((point, normal))
    # Leading-edge slat boundaries are swept in the source planform.
    planes.extend(((Vector((38, 0, 0)), Vector((1, 1.06, 0))),
                   (Vector((38, 0, 0)), Vector((1, -1.06, 0)))))
    for point, normal in planes:
        bmesh.ops.bisect_plane(bm, geom=list(bm.verts) + list(bm.edges) + list(bm.faces),
                              plane_co=point, plane_no=normal, dist=1e-6)
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()


def airframe_group(point, whole_bounds):
    """Separate visible surface regions in the verified donor's source axes.

    The original main airframe is welded across several surfaces. Cuts follow
    its visible panel regions; they are an animation approximation, not OEM
    hinge measurements. Small disconnected detail shells move with their panel.
    """
    x, y, z = point
    side = 'L' if y > 0 else 'R'
    lateral = abs(y)
    lo, hi = whole_bounds
    if lo.y * hi.y > 0 and hi.z > 18 and lo.x < -25:
        return 'fin_' + side
    if -65 < x < -25 and 20 < lateral < 36 and z > 3.5:
        return 'fin_' + side
    if x < -43 and lateral > 20 and z < 4:
        return 'elevator_' + side
    if x < -60 and 8 < lateral < 20.5 and z < 5:
        return 'nozzle_' + side
    if 26 < x < 51 and 9.7 < lateral < 27 and z > -2:
        return 'levcon_' + side
    if x < -31 and lateral > 40 and z < 4:
        return 'aileron_' + side
    if x < -29 and 24 < lateral <= 40 and z < 3:
        return 'flap_' + side
    if lateral > 30 and x > 38 - 1.06 * lateral and z < 3:
        return 'slat_' + side
    return 'airframe'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'assets/aircraft/su57')
    parser.add_argument('--working-output', type=Path, default=ROOT / 'output/Su57-Sketchfab.blend')
    parser.add_argument('--report-dir', type=Path, default=ROOT / 'output/su57-sketchfab')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    if args.source.resolve() == args.working_output.resolve():
        raise ValueError('The working copy must not overwrite the original source')
    for folder in (args.output_dir, args.working_output.parent, args.report_dir):
        folder.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(args.source.resolve()))
    source_scene = bpy.context.scene
    sources = {name: source_scene.objects.get(name) for name in SOURCE_OBJECTS}
    if any(obj is None or obj.type != 'MESH' for obj in sources.values()):
        raise ValueError('Expected the downloaded bohmerang Su-57 scene and named meshes')
    # Bake original object transforms before measuring or separating geometry.
    for obj in sources.values():
        obj.data = obj.data.copy()
        obj.data.transform(obj.matrix_world)
        obj.matrix_world = Matrix.Identity(4)
    source_lo, source_hi = bounds([v.co for o in sources.values() for v in o.data.vertices])
    physical = physical_parameters('su57')
    size = source_hi - source_lo
    scale = Vector((physical['length'] / size.x, physical['wing_span'] / size.y, physical['height'] / size.z))
    # Source nose +X, port +Y, up +Z -> authoring aft +X, starboard +Y, up +Z.
    conversion = Matrix(((-scale.x, 0, 0, source_hi.x * scale.x),
                         (0, -scale.y, 0, (source_hi.y + source_lo.y) * scale.y / 2),
                         (0, 0, scale.z, -source_lo.z * scale.z), (0, 0, 0, 1)))
    scene = bpy.data.scenes.new('Su57 | Sketchfab replacement')
    bpy.context.window.scene = scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 1
    assembly = bpy.data.objects.new('Su57 | assembly', None)
    scene.collection.objects.link(assembly)
    assembly['asset_author'] = 'bohmerang'
    assembly['asset_source'] = SOURCE_URL
    assembly['asset_license'] = 'CC BY-NC-SA 4.0'
    assembly['asset_license_url'] = LICENSE_URL
    assembly['asset_changes'] = 'Dimension fitting, control-surface separation, rigid animation rig, gear contact fitting, PBR glass conversion, LOD export'
    assembly['source_sha256'] = hashlib.sha256(args.source.read_bytes()).hexdigest()
    exported = [assembly]
    groups = {}
    # Runtime extras use glTF axes, while pivot locations use Blender axes.
    for side in ('L', 'R'):
        groups['elevator_' + side] = pivot('Su57 pivot | ' + side + ' stabilator', hinge_point(physical, 'elevator_' + side), 'elevator', (0, 0, -1), -1, parent=assembly)
        groups['aileron_' + side] = pivot('Su57 pivot | ' + side + ' aileron', hinge_point(physical, 'aileron_' + side), 'aileron_' + side, (0, 0, -1), parent=assembly)
        sign = -1 if side == 'L' else 1
        groups['fin_' + side] = pivot('Su57 pivot | ' + side + ' fin', hinge_point(physical, 'fin_' + side), 'rudder', (0, .954, -sign * .30), -1, parent=assembly)
        groups['nozzle_' + side] = pivot('Su57 pivot | ' + side + ' nozzle', hinge_point(physical, 'nozzle_' + side), 'vector_' + side, (0, -physical[f'engines[{0 if side == "L" else 1}].vector_axis'][2], -physical[f'engines[{0 if side == "L" else 1}].vector_axis'][1]), parent=assembly)
        groups['levcon_' + side] = pivot('Su57 pivot | ' + side + ' LEVCON', hinge_point(physical, 'levcon_' + side), 'levcon', (0, 0, -1), parent=assembly)
        groups['flap_' + side] = pivot('Su57 pivot | ' + side + ' flap', (14.9, sign * 3.35, 2.25), 'flap', (0, 0, -1), -20 * math.pi / 180, parent=assembly)
        groups['slat_' + side] = pivot('Su57 pivot | ' + side + ' slat', (11.4, sign * 4.8, 2.4), 'slat', (0, 0, -1), parent=assembly)
    exported.extend(groups.values())
    buckets = defaultdict(list)
    source = sources['SU57-airframe']
    cut_surface_boundaries(source.data)
    for faces in components(source.data):
        indices = {i for face in faces for i in source.data.polygons[face].vertices}
        lo, hi = bounds([source.data.vertices[i].co for i in indices])
        if len(faces) < 1000:
            buckets[airframe_group((lo + hi) / 2, (lo, hi))].extend(faces)
        else:
            for face in faces:
                buckets[airframe_group(source.data.polygons[face].center, (lo, hi))].append(face)
    for group, faces in buckets.items():
        obj = subset(source, faces, 'Su57 | ' + group)
        obj.data.transform(conversion)
        parent_keep_world(obj, groups.get(group, assembly))
        exported.append(obj)
    if any(key not in buckets for key in groups):
        raise ValueError('A control surface has no real geometry: ' + str(set(groups) - set(buckets)))
    for name in ('SU57-canopy', 'SU57-cockpit', 'SU57-hud', 'SU57-instrGlass'):
        obj = subset(sources[name], range(len(sources[name].data.polygons)), 'Su57 | ' + name)
        obj.data.transform(conversion)
        parent_keep_world(obj, assembly)
        exported.append(obj)
    gear_source = sources['SU57-landingOn']
    gear_buckets = defaultdict(list)
    for faces in components(gear_source.data):
        indices = {i for face in faces for i in gear_source.data.polygons[face].vertices}
        lo, hi = bounds([gear_source.data.vertices[i].co for i in indices])
        middle = (lo + hi) / 2
        tag = 'N' if abs(middle.y) < 10 else 'L' if middle.y > 0 else 'R'
        if hi.z < -14 and middle.z < -16:
            role = 'wheel'
        elif (hi.x - lo.x > 12 and hi.y - lo.y > 3) or (tag == 'N' and hi.x > 60):
            # Thin nose-door skins and their hinge brackets are separate shells;
            # they follow the bay doors, not the folding shock strut.
            role = 'door'
        else:
            role = 'leg'
        gear_buckets[(tag, role)].extend(faces)
    gear_fitting = {}
    for tag in ('N', 'L', 'R'):
        nose = tag == 'N'
        contact = Vector(blender_point(physical, physical['gear_nose' if nose else 'gear_main_' + tag.lower()]))
        radius = .33 if nose else .515
        wheels = subset(gear_source, gear_buckets[(tag, 'wheel')], 'Su57 | ' + tag + ' tire and hub')
        wheels.data.transform(conversion)
        lo, hi = bounds([v.co for v in wheels.data.vertices])
        centre = (lo + hi) / 2
        vertical_scale = 2 * radius / (hi.z - lo.z)
        offset = Vector((contact.x - centre.x, contact.y - centre.y, -lo.z))
        # Preserve donor strut shape, then fit the axle to the simulator's contacts.
        top = contact + Vector((0, 0, 2.12))
        fold = pivot('Su57 pivot | ' + tag + ' gear', top, 'gear_fold',
                     (0, 0, -1) if nose else (1, 0, 0),
                     -math.pi / 2 if nose else (1 if tag == 'L' else -1) * math.pi / 2,
                     slide=(0, .55 if nose else 0, 0), parent=assembly)
        compress = pivot('Su57 pivot | ' + tag + ' compression', (0, 0, 0), 'compression_nose' if nose else 'compression_' + tag, gain=0, slide=(0, 1, 0), parent=fold)
        steer = pivot('Su57 pivot | ' + tag + ' steering', contact + Vector((0, 0, radius)), 'steering' if nose else '', (0, 1, 0), parent=compress)
        wheel = pivot('Su57 pivot | ' + tag + ' wheel', contact + Vector((0, 0, radius)), 'nose_wheel' if nose else 'wheel', (0, 0, -1), parent=steer)
        for v in wheels.data.vertices:
            v.co.x = contact.x + (v.co.x - centre.x) * vertical_scale
            v.co.y += offset.y
            v.co.z = (v.co.z - lo.z) * vertical_scale
        parent_keep_world(wheels, wheel)
        exported.extend((fold, compress, steer, wheel, wheels))
        for role in ('leg', 'door'):
            faces = gear_buckets[(tag, role)]
            if not faces:
                raise ValueError('Missing replacement gear geometry: ' + tag + ' ' + role)
            obj = subset(gear_source, faces, 'Su57 | ' + tag + ' ' + role)
            obj.data.transform(conversion)
            obj.data.transform(Matrix.Translation(offset))
            if role == 'door':
                parent = pivot('Su57 pivot | ' + tag + ' door', top, 'gear_door', (1, 0, 0), math.pi / 2, parent=assembly)
                exported.append(parent)
            else:
                parent = compress
            parent_keep_world(obj, parent)
            exported.append(obj)
        gear_fitting[tag] = {'translation_m': list(offset), 'wheel_radius_scale': vertical_scale}
    # The landing-light housings move with the new nose strut.
    obj = subset(sources['SU57-landingOnLight'], range(len(sources['SU57-landingOnLight'].data.polygons)), 'Su57 | landing light')
    obj.data.transform(conversion)
    obj.data.transform(Matrix.Translation(Vector(gear_fitting['N']['translation_m'])))
    parent_keep_world(obj, next(o for o in exported if o.name == 'Su57 pivot | N compression'))
    exported.append(obj)
    # No source camera, alternate retracted-gear mesh or original scene survives
    # in the saved working file. Packed replacement images remain self-contained.
    bpy.data.scenes.remove(source_scene)
    for obj in list(bpy.data.objects):
        if obj not in exported:
            bpy.data.objects.remove(obj, do_unlink=True)
    for material in {m for obj in exported if obj.type == 'MESH' for m in obj.data.materials if m}:
        if material.use_nodes:
            # Blender's Glass BSDF has no core-glTF equivalent. Use the native
            # renderer's supported alpha-blended PBR glass instead.
            if material.name == 'Glass':
                material.node_tree.nodes.clear()
                output = material.node_tree.nodes.new('ShaderNodeOutputMaterial')
                shader = material.node_tree.nodes.new('ShaderNodeBsdfPrincipled')
                shader.inputs['Base Color'].default_value = (.14, .22, .28, 1)
                shader.inputs['Alpha'].default_value = .20
                shader.inputs['Metallic'].default_value = .05
                shader.inputs['Roughness'].default_value = .15
                material.node_tree.links.new(shader.outputs['BSDF'], output.inputs['Surface'])
                material.surface_render_method = 'DITHERED'
            shader = next((n for n in material.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
            if shader and 'Transmission Weight' in shader.inputs:
                shader.inputs['Transmission Weight'].default_value = 0
                # Restore glTF dielectric defaults to avoid unsupported
                # KHR_materials_specular / KHR_materials_ior on light housings.
                shader.inputs['IOR'].default_value = 1.5
                shader.inputs['Specular IOR Level'].default_value = .5
    bpy.context.view_layer.update()
    normalization = {'source': SOURCE_URL, 'author': 'bohmerang', 'license': 'CC BY-NC-SA 4.0',
                     'source_sha256': assembly['source_sha256'], 'source_dimensions_authoring_units': list(size),
                     'target_dimensions_m': [physical['length'], physical['wing_span'], physical['height']],
                     'scale': list(scale), 'span_relative_to_length_change_percent': (scale.y / scale.x - 1) * 100,
                     'vertical_relative_to_length_change_percent': (scale.z / scale.x - 1) * 100,
                     'gear_fitting': gear_fitting, 'geometry_status': 'Dimension-fitted visual surrogate; hinge/contact/gear poses are estimates, not OEM validation'}
    (args.report_dir / 'normalization.json').write_text(json.dumps(normalization, indent=2) + '\n')
    bpy.ops.wm.save_as_mainfile(filepath=str(args.working_output.resolve()))
    all_stats = []
    for level, ratio in enumerate((1., .5, .2, .06)):
        mods, links = [], []
        try:
            bpy.ops.object.select_all(action='DESELECT')
            for obj in exported:
                obj.select_set(True)
                if level and obj.type == 'MESH' and len(obj.data.polygons) > 24:
                    modifier = obj.modifiers.new('Su57 replacement LOD', 'DECIMATE')
                    modifier.ratio = ratio
                    modifier.use_collapse_triangulate = True
                    mods.append((obj, modifier))
            if level:
                for material in {m for obj in exported if obj.type == 'MESH' for m in obj.data.materials if m and m.use_nodes}:
                    for link in list(material.node_tree.links):
                        if link.from_node.type == 'TEX_IMAGE':
                            links.append((material, link.from_socket, link.to_socket))
                            material.node_tree.links.remove(link)
            bpy.context.view_layer.objects.active = assembly
            path = args.output_dir / f'su57_lod{level}.glb'
            log = io.StringIO()
            with redirect_stdout(log):
                bpy.ops.export_scene.gltf(filepath=str(path.resolve()), export_format='GLB', use_selection=True,
                                         export_apply=True, export_extras=True, export_cameras=False, export_lights=False)
            (args.report_dir / f'export-lod{level}.log').write_text(log.getvalue())
            graph = bpy.context.evaluated_depsgraph_get()
            triangles = 0
            for obj in exported:
                if obj.type == 'MESH':
                    evaluated = obj.evaluated_get(graph)
                    mesh = evaluated.to_mesh()
                    mesh.calc_loop_triangles()
                    triangles += len(mesh.loop_triangles)
                    evaluated.to_mesh_clear()
            record = {'level': level, 'triangles': triangles, 'nodes': len(exported),
                      'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                      'source': SOURCE_URL, 'license': 'CC BY-NC-SA 4.0'}
            (args.output_dir / f'lod{level}_stats.json').write_text(json.dumps(record, indent=2) + '\n')
            all_stats.append(record)
            print('SU57_REPLACEMENT_EXPORT', json.dumps(record), flush=True)
        finally:
            for material, source, target in links:
                material.node_tree.links.new(source, target)
            for obj, modifier in mods:
                obj.modifiers.remove(modifier)
    (args.report_dir / 'exports.json').write_text(json.dumps(all_stats, indent=2) + '\n')
    approval = {'schema': 1, 'assets': {
        f'assets/aircraft/su57/su57_lod{record["level"]}.glb': {
            'redistributable': True, 'license': 'CC BY-NC-SA 4.0 (noncommercial)',
            'source': 'bohmerang; ' + SOURCE_URL, 'sha256': record['sha256'],
            'notice': 'licenses/assets/SU57.md',
        } for record in all_stats
    }}
    (args.report_dir / 'asset-approval-su57.json').write_text(json.dumps(approval, indent=2) + '\n')


if __name__ == '__main__':
    main()
