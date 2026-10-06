"""Inspection correction: articulated root controls, shaped gear doors and tread."""
assert not assembly.get('su57_correction_complete'),'Correction already applied'
for tag,sources,x in [('R',['Cube.003','Cube.044'],-1.7),('L',['Cube.054'],1.7)]:
 from physics_geometry import physical_parameters, hinge_point
 h=pivot(tag+' LEVCON root',hinge_point(physical_parameters('su57'),'levcon_'+tag),'levcon')
 for source in sources:parent(scene.objects['Su57 | '+source],h)
source=scene.objects['Su57 | Slat.001']
for tag,sign in [('L',-1),('R',1)]:
 data=source.data.copy();bm=bmesh.new();bm.from_mesh(data)
 bmesh.ops.delete(bm,geom=[v for v in bm.verts if v.co.y*sign<0],context='VERTS')
 bm.to_mesh(data);bm.free()
 obj=bpy.data.objects.new('Su57 | '+tag+' source slat',data);aircraft.objects.link(obj)
 h=pivot(tag+' leading-edge flap',(11.4,sign*4.8,2.4),'slat')
 bpy.context.view_layer.update();parent(obj,h)
 if tag=='L':parent(scene.objects['Su57 | Slat.002'],h)
bpy.data.objects.remove(source,do_unlink=True)
for tag,x,y,nose in [('N',4,0,True),('L',13.25,-2.25,False),('R',13.25,2.25,False)]:
 old=scene.objects['Su57 detail | '+tag+' inner door'];group=old.parent;bpy.data.objects.remove(old,do_unlink=True)
 side=y-(.33 if nose else .44);top=1.98;bottom=1.28 if nose else 1.35
 points=[(x-.55,side,top),(x+.60,side,top),(x+.46,side,bottom+.09),(x-.40,side,bottom)]
 vertices=points+[(a,b+.035,c) for a,b,c in points]
 door=mesh(tag+' tapered gear door',vertices,[(0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],paint,group,smooth=False)
 bevel=door.modifiers.new('Rounded panel edge','BEVEL');bevel.width=.012;bevel.segments=3
 if not nose:group.location.z+=.34
 for z in [bottom+.14,top-.14]:
  rod(tag+' door reinforcement '+str(z),(x-.29,side+.055,z),(x+.35,side+.055,z),.024,dark,group,lod=1)
for tag,x,y,r,w,nose in [('N',4.,0,.33,.16,True),('L',13.25,-2.25,.515,.32,False),('R',13.25,2.25,.515,.32,False)]:
 group=scene.objects['Su57 pivot | '+tag+' wheel spin']
 for axleY in ([y-.2,y+.2] if nose else [y]):
  for dy in [-.18*w,0,.18*w]:
   sides=64;vertices=[]
   for ry in [-.006,.006]:
    vertices.extend((x+(r-.002)*math.sin(i*2*math.pi/sides),axleY+dy+ry,r+(r-.002)*math.cos(i*2*math.pi/sides)) for i in range(sides))
   mesh(tag+' tread groove '+str(axleY)+str(dy),vertices,[(i,(i+1)%sides,(i+1)%sides+sides,i+sides) for i in range(sides)],dark,group,lod=1)
camera=scene.objects['Su57 camera gear'];camera.location=(10.5,-6.5,.95)
camera.rotation_euler=(Vector((13.25,-2.25,1.10))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=3.3
remember_rest();assembly['su57_correction_complete']=True
validation_pose({})
bpy.ops.wm.save_as_mainfile(filepath=str(WORKING_PATH))
print('Shaped gear panels, tread and root/slat articulation saved')
