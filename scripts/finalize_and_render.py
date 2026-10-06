import bpy,os,json,math,sys
from mathutils import Vector
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT=os.path.join(ROOT,'output');s=bpy.context.scene
s.view_settings.view_transform='AgX';s.view_settings.look='AgX - Medium High Contrast'
s.camera=bpy.data.objects['Camera | hero front port']
s.render.resolution_x=2400;s.render.resolution_y=1500;s.render.resolution_percentage=100
s.cycles.samples=96;s.cycles.use_denoising=True;s.render.filepath=OUT+'/A320_Hero.png'
s.render.image_settings.file_format='PNG';s.render.image_settings.color_mode='RGBA';s.render.image_settings.color_depth='8'
bpy.ops.file.pack_all()
# Document measured bounds of evaluated render geometry, excluding the presentation.
root=bpy.data.collections.get('A320 • Airbus demonstrator')
aircraft=[o for o in root.all_objects if not any(c.name.startswith('08 Presentation') for c in o.users_collection)]
assembly=bpy.data.objects.get('AIRBUS A320 | assembly')
if assembly:aircraft=list(dict.fromkeys(aircraft+[assembly]+list(assembly.children_recursive)))
bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get();pts=[]
for o in aircraft:
 if o.type in {'MESH','CURVE'}:
  ev=o.evaluated_get(deps);me=ev.to_mesh()
  if me:pts.extend(ev.matrix_world@v.co for v in me.vertices)
  ev.to_mesh_clear()
lo=[min(v[i] for v in pts) for i in range(3)];hi=[max(v[i] for v in pts) for i in range(3)]
report={'variant':'Airbus A320ceo, CFM56-5B, Sharklets','dimensions_metres':{'length':hi[0]-lo[0],'span':hi[1]-lo[1],'height_from_ground':hi[2]},'aircraft_objects':len(aircraft),'base_mesh_vertices':sum(len(o.data.vertices) for o in aircraft if o.type=='MESH'),'reference_length_m':37.57,'reference_span_m':35.80,'wheelbase_m':12.64,'main_gear_track_m':7.59,'configuration':'Gear extended; neutral control surfaces; exterior only','accuracy_note':'Visual reconstruction from public dimensions and photography. Local contours, livery and mechanical details are approximations; not manufacturer CAD.'}
open(OUT+'/model_report.json','w').write(json.dumps(report,indent=2))
bpy.ops.wm.save_as_mainfile(filepath=OUT+'/Airbus_A320.blend')
# Export evaluated curves without losing their articulated parents.
sys.path.insert(0,os.path.join(ROOT,'scripts'))
from rig_aircraft import export_aircraft
export_aircraft(OUT+'/Airbus_A320.glb',aircraft)
# Render inspected views at delivery resolution.
for cam,name,w,h,samples in [('Camera | hero front port','A320_Hero',2400,1500,96),('Camera | CFM56 detail','A320_Engine_Detail',1800,1200,80),('Camera | port profile','A320_Profile',2400,1200,64),('Camera | main gear detail','A320_Landing_Gear',1600,1200,64)]:
 s.camera=bpy.data.objects[cam];s.render.resolution_x=w;s.render.resolution_y=h;s.cycles.samples=samples;s.render.filepath=OUT+'/'+name+'.png';bpy.ops.render.render(write_still=True)
print('DELIVERABLES COMPLETE',json.dumps(report))
