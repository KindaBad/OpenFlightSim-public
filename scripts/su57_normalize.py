"""Normalize the supplied asset; execute through live Blender MCP only."""
import bpy, json, math
from pathlib import Path
from mathutils import Vector, Matrix
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from su57_paths import pipeline_paths
from physics_geometry import physical_parameters
physical=physical_parameters("su57")
options,ROOT,WORKING_PATH,EXPORT_DIR,REPORT_DIR=pipeline_paths(True)
bpy.ops.wm.open_mainfile(filepath=str(options.source.resolve()))
source_scene=bpy.context.scene
scene=bpy.data.scenes.new('M3.68 | Su57 metric working copy')
bpy.context.window.scene=scene
scene.unit_settings.system=source_scene.unit_settings.system
scene.unit_settings.scale_length=1
scene.unit_settings.length_unit=source_scene.unit_settings.length_unit
aircraft=bpy.data.collections.new('Su57 | aircraft');scene.collection.children.link(aircraft)
studio=bpy.data.collections.new('Su57 | validation studio');scene.collection.children.link(studio)
assembly=bpy.data.objects.new('Su57 | assembly',None);aircraft.objects.link(assembly)
assembly['source_file']=str(options.source.resolve())
assembly['redistribution']='UNVERIFIED: no supplied model or cockpit artwork licence found'
# Source: nose -Y, port +X, up +Z. Canonical: aft +X, starboard +Y, up +Z.
span0=12.367127895355224; length0=17.16999626159668
sy=physical["wing_span"]/span0; sx=physical["length"]/length0; sz=sx
z_shift=physical["height"]-1.9567997455596924*sz
conversion=Matrix(((0,sx,0,7.955928802490234*sx),(-sy,0,0,0),(0,0,sz,z_shift),(0,0,0,1)))
# Evaluate copied modifiers in the original scene's dependency graph.
bpy.context.window.scene=source_scene
dg=bpy.context.evaluated_depsgraph_get()
records=[]
for old in source_scene.objects:
 if old.type!='MESH' or not any(c.name in ['Su57_Mid','Cockpit'] for c in old.users_collection): continue
 if not len(old.data.vertices): continue
 data=bpy.data.meshes.new_from_object(old.evaluated_get(dg),preserve_all_data_layers=True,depsgraph=dg)
 data.transform(conversion@old.matrix_world)
 obj=bpy.data.objects.new('Su57 | '+old.name,data);aircraft.objects.link(obj);obj.parent=assembly
 obj['su57_source_object']=old.name
 records.append({'name':obj.name,'source':old.name,'triangles':sum(len(p.vertices)-2 for p in data.polygons)})
bpy.context.window.scene=scene
assembly['source_dimensions_m']=[length0,span0,2.78005594]
assembly['scale_correction']=[sx,sy,sz]
# Quantify imposed deformation; fitting dimensions does not recover OEM geometry.
deformation={'method':'anisotropic length/span normalization','source_dimensions_m':[length0,span0,2.78005594],
 'scale':[sx,sy,sz],'axial_change_percent':[(v-1)*100 for v in [sx,sy,sz]],
 'span_relative_to_length_change_percent':(sy/sx-1)*100,
 'max_min_scale_ratio':max(sx,sy,sz)/min(sx,sy,sz),'volume_scale':sx*sy*sz,
 'redistribution':'UNVERIFIED','geometry_status':'engineering surrogate; no OEM accuracy claim'}
(REPORT_DIR/'normalization_deformation.json').write_text(json.dumps(deformation,indent=2)+'\n')
print('ANISOTROPIC DEFORMATION',json.dumps(deformation))
assembly['dimension_target_m']=[physical['length'],physical['wing_span'],physical['height']]
# Lighting and orthographic reference views.
world=bpy.data.worlds.new('Su57 | neutral world');world.use_nodes=True
background=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND')
background.inputs['Color'].default_value=(.20,.24,.30,1);background.inputs['Strength'].default_value=.6
scene.world=world
for name,location,energy,size in [('key',(-5,-10,17),3300,12),('fill',(10,12,12),4200,14),('rim',(25,-10,10),3800,10)]:
 data=bpy.data.lights.new('Su57 '+name,'AREA');data.energy=energy;data.size=size
 obj=bpy.data.objects.new(data.name,data);studio.objects.link(obj);obj.location=location
 obj.rotation_euler=(Vector((10,0,2.5))-obj.location).to_track_quat('-Z','Y').to_euler()
data=bpy.data.lights.new('Su57 sun','SUN');data.energy=2;data.angle=.10
obj=bpy.data.objects.new(data.name,data);studio.objects.link(obj);obj.rotation_euler=(.3,-.4,-.7)
views={
 'front':((-32,0,2.6),(10,0,2.6),16), 'rear':((43,0,2.6),(10,0,2.6),16),
 'side':((10,-35,2.6),(10,0,2.6),23),'top':((10,0,40),(10,0,2.6),23),
 'underside':((10,0,-35),(10,0,2.6),23),
 'front_quarter':((-14,-22,15),(10,0,2.5),25),
 'rear_quarter':((34,-22,12),(11,0,2.5),25),
 'canopy':((1,-5,5.8),(4.7,0,3.1),5),
 'intakes':((3,-5,.6),(7.5,-1.35,1.95),5),
 'nozzles':((24,-5,4),(18,-1.34,2.3),5),
 'gear':((8,-8,.7),(12,-2,1),6),
 'surfaces':((21,-15,8),(15,-4.2,2.4),9)}
for name,(location,target,scale) in views.items():
 data=bpy.data.cameras.new('Su57 camera '+name);data.type='ORTHO';data.ortho_scale=scale
 obj=bpy.data.objects.new(data.name,data);studio.objects.link(obj);obj.location=location
 obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=1280;scene.render.resolution_y=800;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
try: scene.render.engine='CYCLES'
except TypeError as e: print(e)
scene.cycles.samples=24;scene.cycles.use_denoising=True
scene.view_settings.exposure=.3

def render_view(name,prefix='blender'):
 scene.camera=scene.objects['Su57 camera '+name]
 scene.render.filepath=str(ROOT/'docs/images/m3_68_su57'/f'{prefix}_{name}.png')
 bpy.ops.render.render(write_still=True)
 print('RENDER',scene.render.filepath)
scene.camera=scene.objects['Su57 camera front_quarter']
for screen in bpy.data.screens:
 for area in screen.areas:
  if area.type=='VIEW_3D':
   area.spaces.active.overlay.show_overlays=False
   area.spaces.active.region_3d.view_distance=28
   area.spaces.active.region_3d.view_location=(10,0,2.5)
   area.spaces.active.region_3d.view_rotation=scene.camera.rotation_euler.to_quaternion()
(ROOT/'docs/images/m3_68_su57').mkdir(parents=True,exist_ok=True)
(REPORT_DIR/'normalized_inventory.json').write_text(json.dumps(records,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=str(WORKING_PATH))
bpy.app.driver_namespace['su57']=globals().copy()
print('NORMALIZED',len(records),'objects','scale',sx,sy,sz,'vertical origin',z_shift)
