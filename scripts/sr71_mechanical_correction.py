"""Blender inspection correction: clear engine bores and cockpit interference."""
from mathutils import Quaternion, Matrix
for s,tag in [(-1,'L'),(1,'R')]:
    cutter=lathe('temporary clear engine bore',[(16.03,.94),(30.18,.94)],(s*4.14,2.70),dark,sectors=96,steps=0,caps=True,lod=0)
    wing=bpy.data.objects[tag+' | delta fixed wing'];bpy.context.view_layer.objects.active=wing
    mod=wing.modifiers.new('Clear nacelle internal volume','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
    bpy.ops.object.modifier_apply(modifier=mod.name);bpy.data.objects.remove(cutter,do_unlink=True)
# Lower the rear separation bulkhead; keep its bottom against the cockpit floor.
o=bpy.data.objects['Rear cockpit | aft bulkhead']
for v in o.data.vertices:
    world=o.matrix_world@v.co
    if world.z>3.24:v.co=o.matrix_world.inverted()@Vector((world.x,world.y,3.24))
# Keep the pitot within the published nose-to-tail envelope.
o=bpy.data.objects['Nose | pitot probe']
for v in o.data.vertices:v.co.x+=.12
# Put gear cameras beneath the airframe so the nacelle does not occlude the struts.
o=scene.objects['SR71 camera gear'];o.location=(18,-5.5,.25)
o.rotation_euler=(Vector((22.0,-2.78,1.10))-o.location).to_track_quat('-Z','Y').to_euler();o.data.ortho_scale=4.6
for name,loc,target,scale in [('nose_gear',(6.8,-3.0,.8),(9.8,0,1.0),3.2),('pilot',(5.82,0,3.35),(5.12,0,3.17),1.45),('rear_pilot',(7.55,0,3.30),(7.00,0,3.12),1.30)]:
    d=bpy.data.cameras.new('SR71 '+name);d.type='PERSP' if 'pilot' in name else 'ORTHO'
    d.ortho_scale=scale;d.lens=25
    o=bpy.data.objects.new('SR71 camera '+name,d);studio.objects.link(o);o.location=loc;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
# Apply extras in Blender for validation, reversing the exporter axis conversion.
rest={o.name:(o.matrix_basis.copy()) for o in aircraft.objects if o.type=='EMPTY'}
def validation_pose(values):
    for o in aircraft.objects:
        if o.name not in rest:continue
        value=values.get(o.get('ofs_channel',''),0)
        axis=o.get('ofs_axis',(0,1,0));slide=o.get('ofs_slide',(0,0,0))
        a=Vector((axis[0],-axis[2],axis[1]));delta=Vector((slide[0],-slide[2],slide[1]))*value
        movement=Matrix.Translation(delta)@Quaternion(a,value*o.get('ofs_gain',1)).to_matrix().to_4x4()
        o.matrix_basis=rest[o.name]@movement
    bpy.context.view_layer.update()
for name in ['cockpit','gear','nose_gear','nozzle','intake','pilot']:render_view(name,'mechanical2')
for amount in [.5,1]:
    validation_pose({'gear_fold':amount,'gear_door':amount})
    render_view('underside','gear_'+str(amount))
validation_pose({})
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/SR71_61-7972.blend'))
print('Engine bores clear; cockpit corrected; gear validation rendered')
