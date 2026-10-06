"""Attach authoring helpers to an existing SR-71 scene without rebuilding it.

MCP execute calls use fresh globals. Keep the explicit authoring namespace in
bpy.app.driver_namespace['sr71']; all stage scripts execute in that namespace.
Reopening the blend and running this script restores the same helpers.
"""
import bpy, bmesh, math, json, ast
from pathlib import Path
from mathutils import Vector
import os
# Blender MCP executes without __file__; set OFS_ROOT or start Blender in the checkout.
ROOT=Path(os.environ.get('OFS_ROOT') or (Path(__file__).resolve().parents[1] if '__file__' in globals() else Path.cwd()))
scene=bpy.data.scenes['OFS SR-71A 61-7972'];bpy.context.window.scene=scene
aircraft=bpy.data.collections['SR71 | aircraft and articulation']
studio=bpy.data.collections['SR71 | validation studio']
assembly=bpy.data.objects['SR71 | 61-7972']
source=ast.parse((ROOT/'scripts/sr71_airframe.py').read_text())
for stmt in source.body:
    if isinstance(stmt,ast.Assign) and isinstance(stmt.value,ast.Call) and isinstance(stmt.value.func,ast.Name) and stmt.value.func.id=='material':
        globals()[stmt.targets[0].id]=bpy.data.materials['SR71 | '+ast.literal_eval(stmt.value.args[0])]
    if isinstance(stmt,ast.Assign) and any(isinstance(t,ast.Name) and t.id in ['body_stations','canopy_stations'] for t in stmt.targets):
        exec(compile(ast.Module(body=[stmt],type_ignores=[]),'sr71 constants','exec'))
for stmt in source.body:
    if isinstance(stmt,ast.FunctionDef):exec(compile(ast.Module(body=[stmt],type_ignores=[]),'sr71 helpers','exec'))
body=bpy.data.objects['Airframe | continuous curved chine fuselage']
print('Attached SR71 authoring session')
