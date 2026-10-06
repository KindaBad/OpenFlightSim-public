"""Fit the articulated stowed gear to the final fuselage, through Blender MCP."""
from mathutils import Quaternion

for side,tag in [(-1,'L'),(1,'R')]:
    gear=scene.objects['ofs_main_gear_'+tag]
    gear['ofs_slide']=(0,1.18,-side*.83)
    scene.objects['ofs_main_door_'+tag].location.z+=.45
nose=scene.objects['ofs_nose_gear']
nose['ofs_gain']=1.62
nose['ofs_slide']=(0,.80,0)
for side in [-1,1]:scene.objects['ofs_nose_door_'+str(side)].location.z+=.58
glass=bpy.data.materials.get('Canopy | restrained blue glass')
if glass is None:
    glass=next(m for m in bpy.data.materials if m.get('ofs_environment_reflection',0)>0)
bsdf=next(n for n in glass.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
bsdf.inputs['Alpha'].default_value=.12
bsdf.inputs['Metallic'].default_value=.05
glass.diffuse_color=(*glass.diffuse_color[:3],.12)

# Inspect the actual runtime hinge/slide poses in Blender, then restore rest.
rest=[]
for o in aircraft.objects:
    if o.type!='EMPTY' or o.get('ofs_channel') not in ['gear_fold','gear_door']:continue
    rest.append((o,o.location.copy(),o.rotation_mode,o.rotation_quaternion.copy()))
    a=o['ofs_axis'];slide=o['ofs_slide']
    o.rotation_mode='QUATERNION'
    o.rotation_quaternion=Quaternion(Vector((a[0],-a[2],a[1])),o['ofs_gain'])
    o.location+=Vector((slide[0],-slide[2],slide[1]))
bpy.context.view_layer.update()
render_view('underside','stowed')
render_view('front_quarter','stowed')
for o,location,mode,rotation in rest:
    o.location=location;o.rotation_quaternion=rotation;o.rotation_mode=mode
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/Typhoon_7L-WA.blend'))
print('Articulated stow poses inspected and source rest restored')
