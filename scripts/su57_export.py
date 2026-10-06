"""Export native GLBs with physical rig hierarchy and progressively reduced geometry."""
from collections import defaultdict
from contextlib import redirect_stdout
import io
destination_dir=EXPORT_DIR;destination_dir.mkdir(parents=True,exist_ok=True)
validation_pose({})
if not assembly.get('su57_detail_consolidated'):
 for obj in list(aircraft.objects):
  if obj.type!='MESH':continue
  bpy.context.view_layer.objects.active=obj
  for modifier in list(obj.modifiers):bpy.ops.object.modifier_apply(modifier=modifier.name)
 groups=defaultdict(list)
 for obj in aircraft.objects:
  if obj.type=='MESH' and obj.name.startswith('Su57 detail |') and len(obj.data.materials)==1:
   groups[(obj.parent.name,int(obj.get('ofs_last_lod',3)),obj.data.materials[0].name)].append(obj)
 for key,objects in groups.items():
  if len(objects)<2:continue
  bpy.ops.object.select_all(action='DESELECT')
  for obj in objects:obj.select_set(True)
  active=objects[0];bpy.context.view_layer.objects.active=active;bpy.ops.object.join()
  active.name='Su57 detail | '+key[0]+' | '+key[2]+' | tier'+str(key[1]);active['ofs_last_lod']=key[1]
 assembly['su57_detail_consolidated']=True
 print('Gear detail consolidated',len(aircraft.objects),'nodes')
def export_su57_lod(level):
 visibility=[];modifiers=[];links=[];selected=[];transmission=[]
 ratio=[1,.33,.080,.016][level]
 path=destination_dir/f'su57_lod{level}.glb'
 try:
  bpy.ops.object.select_all(action='DESELECT')
  for obj in aircraft.objects:
   visibility.append((obj,obj.hide_render,obj.hide_get()))
   keep=obj.type=='EMPTY' or int(obj.get('ofs_last_lod',3))>=level
   obj.hide_render=not keep;obj.hide_set(not keep)
   if not keep:continue
   obj.select_set(True);selected.append(obj)
   if level and obj.type=='MESH' and len(obj.data.polygons)>100:
    d=obj.modifiers.new('Su57 controlled LOD reduction','DECIMATE');d.ratio=ratio
    if 'Canopy' in obj.name:d.ratio=max(ratio,.5 if level==1 else .10)
    d.use_collapse_triangulate=True;modifiers.append((obj,d))
  bpy.context.view_layer.objects.active=assembly;bpy.context.view_layer.update()
  dg=bpy.context.evaluated_depsgraph_get();triangles=0;primitives=0
  for obj in selected:
   if obj.type!='MESH':continue
   e=obj.evaluated_get(dg);data=e.to_mesh();data.calc_loop_triangles()
   triangles+=len(data.loop_triangles);primitives+=len({p.material_index for p in data.polygons});e.to_mesh_clear()
  for mat in {m for obj in selected if obj.type=='MESH' for m in obj.data.materials if m}:
   p=next((n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
   if p:
    transmission.append((p,p.inputs['Transmission Weight'].default_value));p.inputs['Transmission Weight'].default_value=0
   if level:
    for link in list(mat.node_tree.links):
     if link.from_node.type=='TEX_IMAGE':links.append((mat,link.from_socket,link.to_socket));mat.node_tree.links.remove(link)
  export_log=io.StringIO()
  with redirect_stdout(export_log):
   bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,export_apply=True,export_extras=True,export_cameras=False,export_lights=False,export_materials='EXPORT',export_yup=True)
  (REPORT_DIR/f'blender_export_lod{level}.log').write_text(export_log.getvalue())
  result={'level':level,'triangles':triangles,'primitives':primitives,'nodes':len(selected),'bytes':path.stat().st_size}
  (destination_dir/f'lod{level}_stats.json').write_text(json.dumps(result,indent=2)+'\n')
  print('Native Su57 export',json.dumps(result));return result
 finally:
  for mat,src,dst in links:mat.node_tree.links.new(src,dst)
  for p,value in transmission:p.inputs['Transmission Weight'].default_value=value
  for obj,d in modifiers:obj.modifiers.remove(d)
  for obj,render,hidden in visibility:obj.hide_render=render;obj.hide_set(hidden)
remember_rest();bpy.ops.wm.save_as_mainfile(filepath=str(WORKING_PATH))
print('Native Su57 export helpers ready')
