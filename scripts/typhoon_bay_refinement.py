"""Close the bays with proper hinged panels above their recessed interiors."""
for side,tag in [(-1,'L'),(1,'R')]:
    pv=scene.objects['ofs_main_door_'+tag]
    pv.location.y=side*1.2;pv.location.z=1.78
    pv['ofs_gain']=-side*math.pi/2
    o=scene.objects[tag+' gear | bay door']
    coords=[(9.73,side*1.2,1.78),(11.03,side*1.2,1.78),
            (11.03,side*1.2,1.16),(9.73,side*1.2,1.16)]
    for v,p in zip(o.data.vertices,coords):v.co=o.matrix_world.inverted()@Vector(p)
    solid=o.modifiers.new('Door skin thickness','SOLIDIFY');solid.thickness=.012
for o in aircraft.objects:
    if o.type!='MESH':continue
    if 'bay | recessed dark' in o.name or 'bay | rib' in o.name:
        for v in o.data.vertices:v.co.z+=.55
    if 'Nose bay | recessed' in o.name:
        for v in o.data.vertices:v.co.z+=.65
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/Typhoon_7L-WA.blend'))
print('Door footprints fitted and bay ceilings recessed')
