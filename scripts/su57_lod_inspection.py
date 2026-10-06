"""Render actual export reductions with their original shared materials."""
for level in [1,2,3]:
 changes=[];mods=[]
 try:
  for o in aircraft.objects:
   changes.append((o,o.hide_render))
   o.hide_render=o.type=='MESH' and int(o.get('ofs_last_lod',3))<level
   if o.type=='MESH' and not o.hide_render and len(o.data.polygons)>100:
    m=o.modifiers.new('Validation LOD reduction','DECIMATE');m.ratio=[1,.33,.08,.016][level]
    if 'Canopy' in o.name:m.ratio=max(m.ratio,.5 if level==1 else .1)
    m.use_collapse_triangulate=True;mods.append((o,m))
  render_view('front_quarter','lod'+str(level))
 finally:
  for o,m in mods:o.modifiers.remove(m)
  for o,v in changes:o.hide_render=v
validation_pose({})
clay=material('geometry inspection clay',(.34,.40,.44,1),0,.5)
scene.view_layers[0].material_override=clay
try:
 render_view('gear','clay');render_view('surfaces','clay')
finally:scene.view_layers[0].material_override=None
bpy.ops.wm.save_as_mainfile(filepath=str(WORKING_PATH))
