"""Reference cameras and lighting; run with the retained typhoon namespace."""
def studio_obj(name, data):
    o=bpy.data.objects.new(name,data); studio.objects.link(o); return o

world=bpy.data.worlds.new('Typhoon | neutral studio')
world.use_nodes=True
next(n for n in world.node_tree.nodes if n.type=='BACKGROUND').inputs['Color'].default_value=(.13,.16,.20,1)
next(n for n in world.node_tree.nodes if n.type=='BACKGROUND').inputs['Strength'].default_value=.5
scene.world=world
for name,loc,energy,size in [('Key',(0,-9,12),1800,9),('Fill',(7,8,10),2300,10),('Rim',(18,-3,9),2000,7)]:
    data=bpy.data.lights.new('Typhoon '+name,'AREA');data.energy=energy;data.shape='DISK';data.size=size
    o=studio_obj('Studio | '+name,data);o.location=loc;o.rotation_euler=(Vector((8,0,2))-o.location).to_track_quat('-Z','Y').to_euler()
sun=bpy.data.lights.new('Typhoon sun','SUN');sun.energy=1.8;sun.angle=.25
o=studio_obj('Studio | sun',sun);o.rotation_euler=(.3,-.4,-.4)

views={
 'front':((-22,0,4),(7,0,2.2),13), 'rear':((32,0,4),(8,0,2.3),13),
 'left':((8,-27,3.0),(8,0,2.4),18), 'right':((8,27,3.0),(8,0,2.4),18),
 'top':((8,0,30),(8,0,2),19), 'underside':((8,0,-28),(8,0,2),19),
 'front_quarter':((-12,-17,10),(8,0,2),19), 'rear_quarter':((25,17,11),(8,0,2),19),
 'gear':((7,-6,2.2),(9.4,-1.45,.95),4),
 'cockpit':((.5,-4.8,5.3),(4.0,0,2.75),4.8),
 'nozzles':((20,-3,3.5),(15.1,0,1.9),3.8),
 'livery':((5.9,-6,3.2),(5.9,-.8,2.05),5.6)}
for name,(loc,target,scale) in views.items():
    data=bpy.data.cameras.new('Typhoon '+name);data.type='ORTHO';data.ortho_scale=scale
    o=studio_obj('Camera | '+name,data);o.location=loc
    o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()

scene.render.resolution_x=1440;scene.render.resolution_y=960;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
scene.render.film_transparent=False
scene.view_settings.view_transform='Standard'
scene.view_settings.look='None'
scene.view_settings.exposure=-.35
scene.render.image_settings.color_mode='RGBA'

def render_view(name, prefix='blender'):
    scene.camera=scene.objects['Camera | '+name]
    scene.render.filepath=str(ROOT/'docs/images/m3_65_typhoon'/f'{prefix}_{name}.png')
    bpy.ops.render.render(write_still=True)
    print('Rendered',name)

for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D': area.spaces.active.overlay.show_overlays=False
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/Typhoon_7L-WA.blend'))
print('Validation studio ready; source saved')
