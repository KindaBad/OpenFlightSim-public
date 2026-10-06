"""Post-native-capture correction. Run directly through Blender MCP."""
validation_pose({})
if not assembly.get('sr71_runtime_corrected'):
    # Flatten the roof and windshield cross sections into an SR71-like faceted
    # section, retaining the recorded low A-model tandem cockpit stations.
    for o in aircraft.objects:
        if o.type!='MESH' or not o.parent or not o.parent.name.startswith('Canopy_'):continue
        if 'warning' in o.name:continue
        for v in o.data.vertices:
            p=o.matrix_world@v.co
            x,w,z,h=min(canopy_stations,key=lambda r:abs(r[0]-p.x))
            if p.z>z+.015 and abs(p.y)<w*1.15:
                height=(h-z)
                curved=(max(0,1-(p.y/max(w,.01))**2))**.42
                faceted=min(1.,max(0,(1-abs(p.y)/max(w,.01))/.62))
                p.z+=height*(faceted-curved)*.75
                v.co=o.matrix_world.inverted()@p
        o.data.update()
    # Public NASA table1 reports outboard elevons rigged 3 degrees up.
    # Rotate child meshes around their authored hinge, preserving the runtime
    # channel's zero and the entire independent actuator hierarchy.
    from mathutils import Matrix
    for o in aircraft.objects:
        if o.type=='MESH' and o.parent and o.parent.name.startswith('Elevon_') and 'outboard' in o.parent.name:
            par=o.parent;axis=Vector(par['ofs_axis']);axis=Vector((axis.x,-axis.z,axis.y))
            v=Matrix.Rotation(-3*math.pi/180*par['ofs_gain'],4,axis)
            for vertex in o.data.vertices:vertex.co=v@vertex.co
            o.data.update()
    # Petal seam rods need not persist into normal gameplay LOD1.
    for o in aircraft.objects:
        if o.type=='MESH' and 'petal seam' in o.name:o['ofs_last_lod']=0
    assembly['sr71_runtime_corrected']=True
    assembly['sr71_runtime_correction_note']='Flattened canopy after native cockpit capture; public outboard elevon rig; LOD1 nozzle seam removal'
render_view('cockpit','corrected');render_view('front_quarter','corrected');render_view('top','corrected')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/SR71_61-7972.blend'))
print('Post-native correction saved')
