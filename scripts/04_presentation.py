COL=cols['08']
scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
floor=mat('Studio apron | cool concrete',(.105,.135,.17),.18,.50)
box('Ground | seamless studio apron',(18,0,-.125),(4000,4000,.25),floor,.03)
# Seamless curved studio background.
v=[];f=[]
for j in range(65):
    a=pi*.5*j/64;y=50+30*sin(a);z=30*(1-cos(a));v.extend([(-250,y,z),(250,y,z)])
v.extend([(-250,80,160),(250,80,160)])
for j in range(65):f.append((2*j,2*j+1,2*j+3,2*j+2))
mesh('Studio cyclorama | continuous curved backdrop',v,f,floor)
world=bpy.data.worlds.new('Soft blue daylight') if not bpy.data.worlds.get('Soft blue daylight') else bpy.data.worlds['Soft blue daylight'];scene.world=world;world.use_nodes=True
world.node_tree.nodes['Background'].inputs[0].default_value=(.47,.59,.74,1);world.node_tree.nodes['Background'].inputs[1].default_value=.38

def area(name,loc,target,power,size,color,shape='DISK',size_y=None):
    da=bpy.data.lights.new(name,'AREA');da.energy=power;da.shape=shape;da.size=size;da.color=color
    if size_y and shape=='RECTANGLE':da.size_y=size_y
    ob=bpy.data.objects.new(name,da);COL.objects.link(ob);ob.location=loc;ob.rotation_euler=(Vector(target)-ob.location).to_track_quat('-Z','Y').to_euler();return ob
area('Key | giant softbox',(-3,-18,29),(14,0,3),9500,19,(1,.91,.80),'RECTANGLE',26)
area('Rim | long strip',(27,12,24),(17,0,4),12500,21,(.72,.84,1),'RECTANGLE',8)
area('Front intake fill',(-14,-4,10),(12,0,2),2300,12,(.78,.87,1))
area('Port fuselage reflection',(14,-25,12),(18,0,4),5500,24,(1,1,1),'RECTANGLE',7)

def camera(name,loc,target,lens):
    da=bpy.data.cameras.new(name);ob=bpy.data.objects.new(name,da);COL.objects.link(ob);ob.location=loc;ob.rotation_euler=(Vector(target)-ob.location).to_track_quat('-Z','Y').to_euler();da.lens=lens;da.clip_end=1000;return ob
hero=camera('Camera | hero front port',(-14,-64,12),(17.6,0,4.2),58)
profile=camera('Camera | port profile',(18,-160,18),(18,0,4.9),50);profile.data.type='ORTHO';profile.data.ortho_scale=43.5
front=camera('Camera | nose on',(-53,-.1,7),(15,0,4),48)
enginecam=camera('Camera | CFM56 detail',(3,-12,4.1),(12.3,-5.75,1.9),57)
gearcam=camera('Camera | main gear detail',(13,-9,2.7),(17.7,-3.8,1.35),52)
topcam=camera('Camera | planform',(18,0,70),(18,0,0),48);topcam.data.type='ORTHO';topcam.data.ortho_scale=43
scene.camera=hero
scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=48;scene.cycles.use_denoising=True
scene.cycles.max_bounces=7;scene.cycles.diffuse_bounces=3;scene.cycles.glossy_bounces=4
scene.render.resolution_x=1600;scene.render.resolution_y=1000;scene.render.resolution_percentage=65
scene.render.image_settings.file_format='PNG';scene.render.filepath=ROOT+'/output/A320_preview.png'
try:
    scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast'
except: pass
# Disable the startup scene's camera and light in the presentation.
for ob in scene.objects:
    if ob.name in ['Light','Camera'] and ob.name not in root.all_objects:ob.hide_render=True
# Useful opening view with material colours, and clean object organization.
bpy.ops.object.select_all(action='DESELECT');fuse.select_set(True);bpy.context.view_layer.objects.active=fuse
for screen in bpy.data.screens:
    for a in screen.areas:
        if a.type=='VIEW_3D':
            a.spaces.active.region_3d.view_perspective='CAMERA';a.spaces.active.overlay.show_overlays=False;a.spaces.active.shading.type='MATERIAL'
# Recalculate closed mesh normals consistently, including mirrored surfaces.
import bmesh
for ob in root.all_objects:
    if ob.type=='MESH':
        bm=bmesh.new();bm.from_mesh(ob.data);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(ob.data);bm.free()
scene['Aircraft']='Airbus A320-214 / CFM56-5B / Sharklets, Airbus demonstrator F-WWIO'
scene['Reference']='Airbus AC_A320_20250715, pp. 46-47; supplied photograph; exterior photo references'
scene['Configuration']='Landing gear extended; control surfaces neutral'
scene['Accuracy scope']='Full-scale visual exterior reconstruction. Public general arrangement dimensions, estimated local surface contours. Not manufacturer CAD.'
bpy.ops.wm.save_as_mainfile(filepath=ROOT+'/output/Airbus_A320.blend')
print('SCENE READY',len(root.all_objects))
