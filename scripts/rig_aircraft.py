"""Shared owned-model export helpers. Run inside Blender, not at runtime."""
import bpy
import math
from mathutils import Matrix, Vector


def parent_keep_world(obj, parent):
    world = obj.matrix_world.copy()
    obj.parent = parent
    obj.matrix_world = world


def pivot(name, location, channel, axis=(0, 0, 1), gain=1, slide=(0, 0, 0), parent=None):
    obj = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(obj)
    obj.location = location
    obj["ofs_channel"] = channel
    obj["ofs_axis"] = axis  # Already in glTF Y-up axes.
    obj["ofs_gain"] = gain
    obj["ofs_slide"] = slide
    bpy.context.view_layer.update()
    if parent:
        parent_keep_world(obj, parent)
    return obj


def mesh_object(name, vertices, faces, material):
    data = bpy.data.meshes.new(name)
    data.from_pydata(vertices, [], faces)
    data.materials.append(material)
    data.update()
    obj = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(obj)
    for polygon in data.polygons:
        polygon.use_smooth = True
    return obj


def export_aircraft(path, objects):
    bpy.ops.object.select_all(action="DESELECT")
    deps = bpy.context.evaluated_depsgraph_get()
    temporary = []
    for obj in objects:
        if obj.type in {"MESH", "EMPTY"}:
            obj.select_set(True)
        elif obj.type == "CURVE":
            data = bpy.data.meshes.new_from_object(obj.evaluated_get(deps))
            duplicate = bpy.data.objects.new(obj.name + " | export", data)
            bpy.context.scene.collection.objects.link(duplicate)
            duplicate.matrix_world = obj.matrix_world
            if obj.parent:
                parent_keep_world(duplicate, obj.parent)
            duplicate.select_set(True)
            temporary.append(duplicate)
    bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", use_selection=True,
                             export_apply=True, export_materials="EXPORT", export_extras=True,
                             export_cameras=False, export_lights=False)
    for obj in temporary:
        bpy.data.objects.remove(obj, do_unlink=True)
