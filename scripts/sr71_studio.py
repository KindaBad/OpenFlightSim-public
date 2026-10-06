"""Blender MCP validation studio. Run after sr71_airframe.py in same namespace."""
def studio_obj(name,data):
    o=bpy.data.objects.new(name,data);studio.objects.link(o);return o
world=bpy.data.worlds.new('SR71 | neutral validation world');world.use_nodes=True
bg=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND')
bg.inputs['Color'].default_value=(.19,.22,.26,1);bg.inputs['Strength'].default_value=.7
scene.world=world
for name,loc,energy,size in [('Key',(4,-14,20),4800,16),('Fill',(18,16,13),6400,18),('Rim',(36,-8,12),6000,12)]:
    d=bpy.data.lights.new('SR71 '+name,'AREA');d.energy=energy;d.shape='DISK';d.size=size
    o=studio_obj('SR71 studio '+name,d);o.location=loc;o.rotation_euler=(Vector((16,0,2.4))-o.location).to_track_quat('-Z','Y').to_euler()
d=bpy.data.lights.new('SR71 sun','SUN');d.energy=2.5;d.angle=.3
sun=studio_obj('SR71 studio sun',d);sun.rotation_euler=(.35,-.4,-.5)
views={
 'front':((-45,0,2.8),(18,0,2.8),20), 'rear':((64,0,2.8),(20,0,2.8),20),
 'side':((16,-52,3),(16,0,3),36),'top':((16,0,52),(16,0,2.4),37),
 'underside':((16,0,-52),(16,0,2.4),37),
 'front_quarter':((-27,-30,21),(16,0,2.3),37),
 'rear_quarter':((53,-30,18),(17,0,2.4),35),
 'intake':((7,-10,5),(16,-4.72,2.7),7),
 'spike':((10,-8,3.5),(15.8,-4.72,2.7),5),
 'nozzle':((39,-8,5.8),(30.3,-4.72,2.8),5.5),
 'cockpit':((1,-6,6.8),(7.5,0,3),6.8),
 'gear':((17,-7,1.9),(21,-2.2,1.2),5),
 'markings':((30,-12,6),(28.5,-4.25,4.7),5.8)}
for name,(loc,target,scale) in views.items():
    d=bpy.data.cameras.new('SR71 '+name);d.type='ORTHO';d.ortho_scale=scale
    o=studio_obj('SR71 camera '+name,d);o.location=loc;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=1600;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.film_transparent=False
# Keep the current valid view transform rather than assuming an OCIO enum.
scene.view_settings.exposure=.7

def render_view(name,prefix='blender'):
    scene.camera=scene.objects['SR71 camera '+name]
    scene.render.filepath=str(ROOT/'docs/images/m3_67_sr71'/f'{prefix}_{name}.png')
    bpy.ops.render.render(write_still=True)
    print('Rendered',scene.render.filepath)

scene.camera=scene.objects['SR71 camera front_quarter']
for screen in bpy.data.screens:
    for a in screen.areas:
        if a.type=='VIEW_3D':
            a.spaces.active.overlay.show_overlays=False
            a.spaces.active.region_3d.view_distance=40
            a.spaces.active.region_3d.view_location=(16,0,2.5)
            a.spaces.active.region_3d.view_rotation=scene.camera.rotation_euler.to_quaternion()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/SR71_61-7972.blend'))
print('Validation studio saved')
