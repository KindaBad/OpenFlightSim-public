"""Native glTF export with deliberate component culling and per-mesh reduction.

Execute through Blender MCP in the retained namespace. All rig names survive;
source meshes and modifiers are restored after each export, even on failure.
"""
import json

def export_lod(level):
    destination=ROOT/'assets/typhoon'/f'typhoon_lod{level}.glb'
    bpy.ops.object.select_all(action='DESELECT')
    selected=[]; modifiers=[]; visibility=[];texture_links=[]
    ratio=[1,.42,.13,.035][level]
    try:
        for o in aircraft.objects:
            visibility.append((o,o.hide_render,o.hide_get()))
            keep=o.type=='EMPTY' or int(o.get('ofs_last_lod',3))>=level
            o.hide_render=not keep;o.hide_set(not keep)
            if not keep:continue
            o.select_set(True);selected.append(o)
            if level and o.type=='MESH' and len(o.data.polygons)>100:
                dec=o.modifiers.new('OFS authored LOD reduction','DECIMATE')
                # Preserve the thin canopy and silhouette at the first tier.
                dec.ratio=max(ratio,.6 if level==1 and 'Canopy' in o.name else ratio)
                dec.use_collapse_triangulate=True;modifiers.append((o,dec))
        bpy.context.view_layer.objects.active=assembly
        bpy.context.view_layer.update()
        ns=bpy.context.evaluated_depsgraph_get();triangles=0
        for o in selected:
            if o.type=='MESH':
                evaluated=o.evaluated_get(ns);data=evaluated.to_mesh()
                data.calc_loop_triangles();triangles+=len(data.loop_triangles)
                evaluated.to_mesh_clear()
        scene.camera=scene.objects['Camera | front_quarter']
        render_view('front_quarter',f'lod{level}')
        if level:
            # Native exporter retains named material slots while omitting maps.
            # Runtime remaps these names to LOD0's shared texture/material table.
            for mat in {m for o in selected if o.type=='MESH' for m in o.data.materials if m}:
                for link in list(mat.node_tree.links):
                    if link.from_node.type=='TEX_IMAGE':
                        texture_links.append((mat,link.from_socket,link.to_socket))
                        mat.node_tree.links.remove(link)
        bpy.ops.export_scene.gltf(filepath=str(destination),export_format='GLB',use_selection=True,
            export_apply=True,export_extras=True,export_cameras=False,export_lights=False,
            export_materials='EXPORT',export_yup=True)
        print(f'LOD{level}: {triangles} triangles, {len(selected)} objects, {destination.stat().st_size} bytes')
        return {'level':level,'triangles':triangles,'objects':len(selected),'bytes':destination.stat().st_size}
    finally:
        for mat,src,dst in texture_links:mat.node_tree.links.new(src,dst)
        for o,m in modifiers:o.modifiers.remove(m)
        for o,render,hidden in visibility:o.hide_render=render;o.hide_set(hidden)

stats=[]
for level in range(4):stats.append(export_lod(level))
(ROOT/'assets/typhoon/lod_stats.json').write_text(json.dumps(stats,indent=2)+'\n')
# Pack original images to keep the Blender source independently portable.
for image in bpy.data.images:
    if image.filepath and 'assets/typhoon/textures' in image.filepath:image.pack()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/Typhoon_7L-WA.blend'))
print('Four native Blender GLBs exported; editable source restored and saved')
