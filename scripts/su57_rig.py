from physics_geometry import physical_parameters, hinge_point, blender_point
physical=physical_parameters("su57")
"""Preserve supplied airframe/detail geometry, add its missing undercarriage and rig."""
assert not assembly.get('su57_rig_complete'),'Rig stage already applied'
paint=material('gear painted alloy',(.32,.38,.41,1),.35,.40)
chrome=material('oleo polished steel',(.64,.68,.72,1),.92,.18)
rubber=material('tire rubber',(.014,.017,.019,1),0,.82)
dark=material('engine internal dark metal',(.055,.060,.065,1),.80,.55)
well=material('gear bay shadow',(.045,.051,.05,1),.2,.75)
red=material('gear hydraulic line',(.19,.023,.013,1),.25,.45)
glass=material('canopy glass',(.20,.28,.31,.14),.05,.10)
glass['ofs_environment_reflection']=.75
glass.use_backface_culling=False
shader=next(n for n in glass.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
shader.inputs['IOR'].default_value=1.46
shader.inputs['Transmission Weight'].default_value=.9
# Use an inspected valid enum, selecting the alpha-blended method for export.
valid=[i.identifier for i in glass.bl_rna.properties['surface_render_method'].enum_items]
if 'BLENDED' in valid:glass.surface_render_method='BLENDED'
for name in ['Canopy01','Canopy02']:
 obj=scene.objects['Su57 | '+name];obj.data.materials.clear();obj.data.materials.append(glass)
for obj in aircraft.objects:
 if obj.type=='MESH' and not len(obj.data.materials):obj.data.materials.append(dark)
 # Compressor internals have more geometry than their visible contribution warrants.
 if obj.type=='MESH' and any(n in obj.name for n in ['Cube.036','Cube.037','Cylinder.004','Cylinder.005']):obj['ofs_last_lod']=0
def hinge(source,name,loc,channel,axis=(0,1,0),gain=1):
 obj=scene.objects['Su57 | '+source];h=pivot(name,loc,channel,axis,gain)
 parent(obj,h);return h
sx=assembly['scale_correction'][0];sy=assembly['scale_correction'][1]
def original_point(x,y,z):return ((y+7.955928802490234)*sx,-x*sy,z*sx+2.30927877406)
# Fixed-axis controls retain the source's visible hinge line and finite physical angles.
for source,tag,original in [('Stab.001','R',(-2.0,5.5,.05)),('Stab.002','L',(2.0,5.5,.05))]:
 hinge(source,tag+' all-moving stabilator',hinge_point(physical,'elevator_'+tag),'elevator',gain=-1)
for source,tag,original in [('Air.001','R',(-4.5,5.08,.05)),('Air.002','L',(4.5,5.08,.05))]:
 hinge(source,tag+' outer aileron',hinge_point(physical,'aileron_'+tag),'aileron_'+tag)
for source,tag,original in [('Flaps.002','R',(-3,5.06,.04)),('Flaps.003','L',(3,5.06,.04))]:
 hinge(source,tag+' inboard flap',original_point(*original),'flap',gain=-20*math.pi/180)
for source,tag,x in [('L-vertical','L',2.3),('R-vertical','R',-2.3)]:
 # Entire canted fin is all-moving; no fictional split rudder.
 sign=-1 if tag=='L' else 1
 hinge(source,tag+' canted all-moving fin',hinge_point(physical,'fin_'+tag),'rudder',axis=(0,sign*.30,.954),gain=-1)
for source,tag,index in [('L-engine','L',0),('R-engine','R',1)]:
 sign=1 if index==0 else -1
 hinge(source,tag+' independent vector nozzle',hinge_point(physical,'nozzle_'+tag),'vector_'+tag,axis=(0,physical[f"engines[{index}].vector_axis"][1],-physical[f"engines[{index}].vector_axis"][2]))
# Add estimated gear geometry at the simulation contact coordinates, in metres.
# One main wheel per side and a paired nose axle. Dimensions/kinematics are estimates.
for tag,x,y,radius,width,nose in [('N',4.0,0,.33,.16,True),('L',13.25,-2.25,.515,.32,False),('R',13.25,2.25,.515,.32,False)]:
 contact=blender_point(physical,physical['gear_nose' if nose else 'gear_main_'+tag.lower()])
 x,y=contact[0],contact[1]
 top=(x,y,2.15 if nose else 2.12);bottom=(x,y,radius+.10)
 fold=pivot(tag+' gear retract',top,'gear_fold',axis=(0,1,0) if nose else (1,0,0),gain=-math.pi/2 if nose else (1 if tag=='L' else -1)*math.pi/2,slide=(0,0,.22 if nose else .28))
 rod(tag+' upper shock strut',top,(x+.06,y,1.13 if nose else 1.27),.065 if nose else .095,paint,fold)
 compress=pivot(tag+' oleo compression',(0,0,0),'compression_nose' if nose else 'compression_'+tag,gain=0,slide=(0,0,1),group=fold)
 rod(tag+' chrome oleo',(x+.06,y,1.25),bottom,.041 if nose else .062,chrome,compress)
 rod(tag+' diagonal drag brace',(x+.65,y,2.03),(x+.04,y,1.32),.037,paint,fold)
 rod(tag+' torque link upper',(x+.06,y+.10,1.35),(x+.26,y+.10,1.09),.027,paint,compress,lod=1)
 rod(tag+' torque link lower',(x+.26,y+.10,1.09),(x+.14,y+.10,.91),.027,paint,compress,lod=1)
 rod(tag+' hydraulic pipe',(x-.035,y+.12,2.02),(x+.08,y+.12,.92),.008,red,fold,lod=0,sides=8)
 steer=pivot(tag+' axle steering',(x,y,radius),'steering' if nose else '',axis=(0,0,1),group=compress)
 wheel=pivot(tag+' wheel spin',(x,y,radius),'nose_wheel' if nose else 'wheel',axis=(0,1,0),group=steer)
 for axleY in ([y-.20,y+.20] if nose else [y]):
  center=(x,axleY,radius)
  tire(tag+' tire '+str(axleY),center,radius,width,rubber,wheel)
  rod(tag+' wheel hub '+str(axleY),(center[0],axleY-width*.55,radius),(center[0],axleY+width*.55,radius),radius*.60,paint,wheel,sides=32)
  for outer in [-1,1]:
   sideY=axleY+outer*width*.57
   rod(tag+' brake disc '+str(axleY)+str(outer),(center[0],sideY-.015,radius),(center[0],sideY+.015,radius),radius*.35,dark,wheel,lod=1,sides=24)
   for bolt in range(8):
    a=bolt*math.pi/4;bc=(center[0]+radius*.44*math.sin(a),sideY,radius+radius*.44*math.cos(a))
    rod(tag+' hub bolt '+str(axleY)+str(outer)+str(bolt),(bc[0],bc[1]-.01,bc[2]),(bc[0],bc[1]+.01,bc[2]),.013,chrome,wheel,lod=0,sides=6)
 box(tag+' upper gear bay liner',(x-.1,y,2.09 if nose else 2.33),(1.35,.52 if nose else .72,.07),well)
 door=pivot(tag+' gear door hinge',(x-.42,y-(.32 if nose else .43),1.96),'gear_door',axis=(1,0,0),gain=math.pi/2)
 box(tag+' inner door',(x-.03,y-.32,1.53),(1.15,.045,.86),paint,door)
 box(tag+' door actuator fairing',(x-.08,y-.29,1.62),(.7,.07,.04),dark,door,lod=1)
 if nose:
  # Su57-style FOD mudguard over the paired nose tires.
  box('nose FOD guard',(x,0,.72),(.60,.62,.045),paint,steer,lod=1)
remember_rest();assembly['su57_rig_complete']=True
assembly['gear_geometry']='ESTIMATE: scale, contacts and oleo strokes matched to simulation; not OEM drawings'
assembly['vector_axis_model']='ESTIMATE: mirrored 30deg cant, +-15deg, 60deg/s, independent physical engine axes'
bpy.ops.wm.save_as_mainfile(filepath=str(WORKING_PATH))
print('Su57 rig saved',len(aircraft.objects),'aircraft nodes')
