import bpy,os
# Blender MCP executes without __file__; set OFS_ROOT or start Blender in the checkout.
root=os.path.join(os.environ.get('OFS_ROOT') or (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) if '__file__' in globals() else os.getcwd()),'output')
s=bpy.context.scene
try:
    s.view_settings.view_transform='AgX';s.view_settings.look='AgX - Medium High Contrast'
except Exception as e:print('COLOR',e)
s.render.resolution_x=1400;s.render.resolution_y=900;s.render.resolution_percentage=75;s.cycles.samples=32
for cam,name in [('Camera | port profile','check_profile'),('Camera | CFM56 detail','check_engine'),('Camera | hero front port','check_hero')]:
    s.camera=bpy.data.objects[cam];s.render.filepath=root+'/'+name+'.png';bpy.ops.render.render(write_still=True)
