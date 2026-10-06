"""Native Blender export and authored SR71 LODs. Execute only through MCP.

Merge small static details within the same parent/material/detail tier. Preserve
major named surfaces and every useful animated pivot. Reduced GLBs retain names
and reuse the runtime LOD0 material/texture table, without duplicating maps.
"""
from collections import defaultdict
validation_pose({})
destination_dir=ROOT/'assets/aircraft/sr71';destination_dir.mkdir(parents=True,exist_ok=True)

if not assembly.get('sr71_detail_consolidated'):
    # Apply detail bevels before joining, so secondary geometry is preserved.
    for o in list(aircraft.objects):
        if o.type!='MESH':continue
        bpy.context.view_layer.objects.active=o
        for mod in list(o.modifiers):bpy.ops.object.modifier_apply(modifier=mod.name)
    groups=defaultdict(list)
    for o in list(aircraft.objects):
        if o.type!='MESH' or len(o.data.polygons)>1000 or len(o.data.materials)!=1:continue
        groups[(o.parent.name,int(o.get('ofs_last_lod',3)),o.data.materials[0].name)].append(o)
    merged=0
    for key,objects in groups.items():
        if len(objects)<2:continue
        bpy.ops.object.select_all(action='DESELECT')
        for o in objects:o.select_set(True)
        active=objects[0];sources=[o.name for o in objects]
        bpy.context.view_layer.objects.active=active;bpy.ops.object.join()
        active['sr71_sources']=json.dumps(sources);active['ofs_last_lod']=key[1]
        active.name='SR71 detail | '+key[0]+' | '+key[2]+' | tier'+str(key[1])
        merged+=len(objects)-1
    assembly['sr71_detail_consolidated']=True
    print('Consolidated',merged,'small meshes;',len(aircraft.objects),'aircraft nodes remain')

def export_sr71_lod(level):
    path=destination_dir/f'sr71_lod{level}.glb'
    bpy.ops.object.select_all(action='DESELECT')
    visibility=[];modifiers=[];links=[];selected=[]
    ratio=[1,.40,.105,.025][level]
    try:
        for o in aircraft.objects:
            visibility.append((o,o.hide_render,o.hide_get()))
            keep=o.type=='EMPTY' or int(o.get('ofs_last_lod',3))>=level
            o.hide_render=not keep;o.hide_set(not keep)
            if not keep:continue
            o.select_set(True);selected.append(o)
            if level and o.type=='MESH' and len(o.data.polygons)>100:
                d=o.modifiers.new('SR71 authored reduction','DECIMATE')
                d.ratio=max(ratio,.60 if level==1 and 'canopy silhouette' in o.name else ratio)
                d.use_collapse_triangulate=True;modifiers.append((o,d))
        bpy.context.view_layer.objects.active=assembly;bpy.context.view_layer.update()
        dg=bpy.context.evaluated_depsgraph_get();triangles=0;primitives=0;meshes=0
        for o in selected:
            if o.type!='MESH':continue
            e=o.evaluated_get(dg);data=e.to_mesh();data.calc_loop_triangles()
            triangles+=len(data.loop_triangles);primitives+=len({p.material_index for p in data.polygons});meshes+=1;e.to_mesh_clear()
        render_view('front_quarter',f'lod{level}');render_view('rear_quarter',f'lod{level}')
        if level:
            for mat in {m for o in selected if o.type=='MESH' for m in o.data.materials if m}:
                for link in list(mat.node_tree.links):
                    if link.from_node.type=='TEX_IMAGE':
                        links.append((mat,link.from_socket,link.to_socket));mat.node_tree.links.remove(link)
        bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,export_apply=True,export_extras=True,export_cameras=False,export_lights=False,export_materials='EXPORT',export_yup=True)
        result={'level':level,'triangles':triangles,'meshes':meshes,'primitives':primitives,'nodes':len(selected),'bytes':path.stat().st_size}
        print('Native SR71 export',json.dumps(result));return result
    finally:
        for mat,src,dst in links:mat.node_tree.links.new(src,dst)
        for o,m in modifiers:o.modifiers.remove(m)
        for o,render,hidden in visibility:o.hide_render=render;o.hide_set(hidden)

stats=[export_sr71_lod(i) for i in range(4)]
(destination_dir/'lod_stats.json').write_text(json.dumps(stats,indent=2)+'\n')
for im in bpy.data.images:
    if '/sr71/textures/' in im.filepath:im.pack()
validation_pose({})
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/SR71_61-7972.blend'))
print('Four GLBs exported natively; source restored, packed and saved')
