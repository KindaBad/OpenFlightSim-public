"""Close-up defects corrected in the retained live Blender namespace."""
for o in list(aircraft.objects):
    if o.name.startswith(('Cockpit | rear bulkhead','Cockpit | sidewall')):
        bpy.data.objects.remove(o,do_unlink=True)
slab('Cockpit | tapered rear bulkhead',[(5.34,0,2.05),(5.34,0,2.68),(5.49,0,2.62),(5.56,0,2.05)],.24,cockpit,lod=1)
for s in [-1,1]:
    mesh('Cockpit | inset sidewall '+str(s),[(3.1,s*.29,2.08),(3.1,s*.29,2.37),(4.4,s*.40,2.40),(5.31,s*.26,2.40),(5.31,s*.26,2.08)],[(0,1,2,3,4)],cockpit,smooth=False,lod=1)
panel=scene.objects['Panel | instrument mass']
for v in panel.data.vertices:v.co.y=(v.co.y+.35)*.46
# A continuous painted cockpit shoulder joins the aperture to the sill.
for side in [-1,1]:
    verts=[];faces=[]
    for row in [r for r in sections if 2.85<r[0]<5.55]:
        x,w,h,z=row
        cr=min(canopy_rows,key=lambda r:abs(r[0]-x))
        _,cw,cz,ch=cr
        for j in range(5):
            t=j/4
            verts.append((x,side*((1-t)*w*math.sqrt(1-.64**2)+t*cw),z+h*.64+(cz-z-h*.64)*t))
    for j in range(len(verts)//5-1):
        for k in range(4):
            a=j*5+k;faces.append((a,a+1,a+6,a+5))
    mesh('Cockpit | painted shoulder '+str(side),verts,faces,paint,lod=3)
node=next(n for n in heat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
node.inputs['Base Color'].default_value=(.075,.069,.061,1)
node.inputs['Roughness'].default_value=.63
node.inputs['Metallic'].default_value=.55
for o in aircraft.objects:
    if 'Seat | ejection rail' in o.name:o.data.materials[0]=cockpit
    if 'foreplane root bearing' in o.name:o.data.materials[0]=frame
    if 'nozzle petal' in o.name:
        for p in o.data.polygons:
            for li in p.loop_indices:
                v=o.data.loops[li].vertex_index
                o.data.uv_layers.active.data[li].uv=(v%7/6,v//7/8)
    if 'bay door' in o.name or 'Nose bay | door' in o.name:
        pv=o.parent
        for v in o.data.vertices:
            world=o.matrix_world@v.co
            world.z=(1.50 if 'Nose bay' in o.name else 1.22)-abs(world.y-pv.location.y)*1.15
            world.y=pv.location.y
            v.co=o.matrix_world.inverted()@world
        side=1 if pv.location.y>0 else -1
        pv['ofs_gain']=(-side if 'nose' in pv.name else side)*math.pi/2
    if 'NO STEP' in o.name:
        center=sum((o.matrix_world@v.co for v in o.data.vertices),Vector())/len(o.data.vertices)
        if center.x<6:
            parent(o,scene.objects['ofs_canard_L' if center.y<0 else 'ofs_canard_R'])
            for v in o.data.vertices:
                world=o.matrix_world@v.co;world.z+=.22;v.co=o.matrix_world.inverted()@world
    if 'wing | Austrian roundel' in o.name:
        top='False' in o.name
        for v in o.data.vertices:
            world=o.matrix_world@v.co
            yy=abs(world.y);le=8.55+(yy-2.5)*3.53/2.74;te=12.55+(yy-2.5)*.28/2.74
            t=max(0,min(1,(world.x-le)/(te-le)))
            h=5*.04*(te-le)*(.2969*math.sqrt(t)-.126*t-.3516*t*t+.2843*t**3-.1036*t**4)
            world.z=1.95+(yy-2.5)*.04/2.74+(h+.006 if top else -h-.006)
            v.co=o.matrix_world.inverted()@world
scene.view_settings.exposure=-.85
scene.objects['Studio | sun'].data.energy=1.15
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/Typhoon_7L-WA.blend'))
print('Corrected cockpit intrusion, doors, nozzle metal and decal placement')
