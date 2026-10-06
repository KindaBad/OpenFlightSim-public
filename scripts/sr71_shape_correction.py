"""First reference correction, via Blender MCP. Rebuild only owned blockout parts.

NASA plan/side views reveal the first blockout had nacelles too far forward,
excessively long tandem canopy, overly forward wing roots and wide engine spacing.
Retains the scene, studio and repository-owned assembly; replaces its geometry.
"""
for o in list(aircraft.objects):
    if o is not assembly:bpy.data.objects.remove(o,do_unlink=True)
source=(ROOT/'scripts/sr71_airframe.py').read_text()
exec(compile(source[source.index('# Continuous upper/lower forebody'):],'sr71 corrected shape','exec'))
scene.camera=scene.objects['SR71 camera front_quarter'];scene.camera.data.ortho_scale=43
for name in ['top','side','front_quarter']:render_view(name,'shape1')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/SR71_61-7972.blend'))
