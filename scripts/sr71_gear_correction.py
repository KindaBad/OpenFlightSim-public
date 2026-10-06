"""Correct inward/forward gear stowage through Blender MCP.

Published retraction directions; reconstructed linkage and bay dimensions.
Run once in the existing SR71 driver namespace, at the rest pose.
"""
validation_pose({})
if assembly.get('gear_bays_corrected'):
    raise RuntimeError('Gear correction already applied')
for s,tag in [(-1,'L'),(1,'R')]:
    # The aft wing formerly continued behind the ejector mouth: clear the full
    # nacelle corridor through the trailing edge, not only the engine length.
    cutter=lathe('temporary aft engine corridor',[(27.7,1.01),(32.8,1.01)],(s*4.14,2.70),dark,sectors=96,steps=0,caps=True,lod=0)
    o=bpy.data.objects[tag+' | delta fixed wing'];bpy.context.view_layer.objects.active=o
    m=o.modifiers.new('Clear aft nozzle sightline','BOOLEAN');m.operation='DIFFERENCE';m.solver='EXACT';m.object=cutter
    bpy.ops.object.modifier_apply(modifier=m.name);bpy.data.objects.remove(cutter,do_unlink=True)

# Remove only the previous SR71 bay/door structures, retaining all gear detail.
owned=[o for o in aircraft.objects if ('main well |' in o.name or 'main door |' in o.name or 'main gear | door' in o.name or 'Nose gear | well' in o.name or 'Nose gear | open door' in o.name or 'Nose gear | door inner' in o.name or o.name.startswith('Gear_door_'))]
for o in owned:bpy.data.objects.remove(o,do_unlink=True)

# Widen the underbody openings for the actual swept gear path. The fuselage is
# a shell with deliberate apertures, so face removal is cleaner than a Boolean.
bm=bmesh.new();bm.from_mesh(body.data)
removed=[]
for f in bm.faces:
    c=f.calc_center_median()
    nose=6.65<c.x<10.88 and abs(c.y)<.40 and c.z<2.42
    main=20.84<c.x<23.38 and abs(c.y)<1.85 and c.z<2.49
    if nose or main:removed.append(f)
bmesh.ops.delete(bm,geom=removed,context='FACES');bm.to_mesh(body.data);bm.free()

for s,tag in [(-1,'L'),(1,'R')]:
    y=s*1.80
    cutter=box('temporary swept main bay',(22.10,y,2.10),(2.54,3.30,1.06),dark,lod=0)
    o=bpy.data.objects[tag+' | delta fixed wing'];bpy.context.view_layer.objects.active=o
    m=o.modifiers.new('Swept main gear bay','BOOLEAN');m.operation='DIFFERENCE';m.solver='EXACT';m.object=cutter
    bpy.ops.object.modifier_apply(modifier=m.name);bpy.data.objects.remove(cutter,do_unlink=True)
    # Reconstructed stepped ceiling: the stowed wheels occupy the deep center
    # body; the upper oleo remains in the shallow wing root.
    box(tag+' main well | inner ceiling',(22.10,s*.83,3.00),(2.54,1.35,.06),interior,lod=2)
    box(tag+' main well | outer ceiling',(22.10,s*2.46,2.58),(2.54,1.94,.06),interior,lod=2)
    for yy,z,h in [(s*.15,2.41,1.16),(s*3.45,2.37,.45)]:
        box(tag+' main well | side',(22.10,yy,z),(2.54,.04,h),interior,lod=2)
    for xx in [20.84,23.37]:
        mesh(tag+' main well | shaped end',[(xx,s*.15,1.84),(xx,s*1.8,2.20),(xx,s*3.45,2.19),(xx,s*3.45,2.58),(xx,s*1.5,2.58),(xx,s*1.5,3.00),(xx,s*.15,3.00)],[(0,1,2,3,4,5,6)],interior,lod=2,smooth=False)
    for xx in [21.05,21.57,22.09,22.61,23.14]:
        tube(tag+' main well | structure',[(xx,s*.20,2.94),(xx,s*1.40,2.94),(xx,s*1.55,2.52),(xx,s*3.36,2.52)],.022,metal,lod=1)
    gear=bpy.data.objects['Gear_main_'+tag]
    gear['ofs_gain']=-s*math.pi/2
    gear['ofs_slide']=(.12,.40,s*.50) # glTF X/Y/Z -> Blender X/Z/-Y
    # Closed doors follow the reconstructed underside rather than a single
    # flat panel below the belly. Author the open pose by undoing closure.
    profile=[(.15,1.84),(.55,1.96),(.90,2.11),(1.30,2.25),(1.80,2.31),(2.40,2.20),(2.95,2.19),(3.45,2.19)]
    for index,rows in enumerate([profile[:5],profile[4:]]):
        hinge=rows[0] if index==0 else rows[-1]
        loc=Vector((22.10,s*hinge[0],hinge[1]))
        gain=(-s if index==0 else s)*math.pi/2
        door=pivot('Gear_door_'+tag+'_'+str(index),loc,'gear_door',axis=(1,0,0),gain=gain)
        undo=Quaternion(Vector((1,0,0)),-gain)
        v=[];f=[]
        for xx in [20.84,23.37]:
            for yy,zz in rows:v.append(tuple(loc+undo@(Vector((xx,s*yy,zz)) -loc)))
        n=len(rows)
        for j in range(n-1):f.append((j,j+1,j+1+n,j+n))
        o=mesh(tag+' main gear | curved door '+str(index),v,f,paint,door,lod=2,smooth=False)
        sol=o.modifiers.new('Door sheet thickness','SOLIDIFY');sol.thickness=.026
        for xx in [21.04,22.08,23.16]:
            pts=[tuple(loc+undo@(Vector((xx,s*yy,zz+.035))-loc)) for yy,zz in rows]
            tube(tag+' main door | rib',pts,.015,metal,door,sides=8,lod=1)

# Forward folding nose leg; extra translation represents the omitted linkage.
nose=bpy.data.objects['Gear_nose'];nose['ofs_gain']=2.05;nose['ofs_slide']=(-.05,.38,0)
box('Nose gear | well ceiling',(8.77,0,2.87),(4.26,.76,.06),interior,lod=2)
for s in [-1,1]:box('Nose gear | well side',(8.77,s*.42,2.39),(4.26,.04,.92),interior,lod=2)
for xx in [6.65,10.88]:box('Nose gear | well end',(xx,0,2.39),(.045,.84,.92),interior,lod=2)
for s in [-1,1]:
    door=pivot('Gear_door_nose_'+str(s),(8.77,s*.42,1.95),'gear_door',axis=(1,0,0),gain=-s*math.pi/2)
    box('Nose gear | open door '+str(s),(8.77,s*.42,1.74),(4.23,.028,.42),paint,door,lod=2,bevel=.012)
    for xx in [6.88,7.89,8.90,9.91,10.64]:
        box('Nose gear | door inner rib',(xx,s*.44,1.74),(.025,.024,.34),metal,door,lod=1)

# Tone down the intake lip: it is dark coated metal, not a bright chrome ring.
for tag in ['L','R']:
    o=bpy.data.objects[tag+' inlet | capture lip'];o.data.materials[0]=frame
o=scene.objects['SR71 camera pilot'];o.location=(5.94,0,3.47)
o.rotation_euler=(Vector((3.5,0,3.31))-o.location).to_track_quat('-Z','Y').to_euler();o.data.lens=21
rest={o.name:o.matrix_basis.copy() for o in aircraft.objects if o.type=='EMPTY'}
assembly['gear_bays_corrected']=True
for amount in [0,.5,1]:
    validation_pose({'gear_fold':amount,'gear_door':1 if amount==1 else 0})
    render_view('underside','gear3_'+str(amount));render_view('gear','gear3_'+str(amount))
validation_pose({})
render_view('nozzle','mechanical3');render_view('pilot','mechanical3')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/SR71_61-7972.blend'))
print('Corrected forward/inward gear paths and shaped doors; saved source')
