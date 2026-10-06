"""Refine visible cockpit massing and retain ground contact through oleo travel."""
for key,channel in [('ofs_nose_gear','compression_nose'),('ofs_main_gear_L','compression_L'),('ofs_main_gear_R','compression_R')]:
    o=scene.objects[key]
    pv=pivot('ofs_suspension_'+key.removeprefix('ofs_'),tuple(o.location),channel,gain=0,slide=(0,1,0))
    parent(o,pv)
for o in list(aircraft.objects):
    if o.name.startswith(('Panel | glare shield','HUD | frame','HUD | transparent combiner')):
        bpy.data.objects.remove(o,do_unlink=True)
# Contoured glare shield follows the instrument-panel envelope, with a blunt
# leading edge and chamfered pilot-facing corners rather than an oval cushion.
outline=[(3.11,-.28,3.09),(3.11,.28,3.09),(3.35,.39,3.12),
         (3.65,.38,3.10),(3.73,.28,3.10),(3.73,-.28,3.10),(3.65,-.38,3.10),(3.35,-.39,3.12)]
o=mesh('Panel | contoured glare shield',outline,[(0,1,2,3,4,5,6,7)],cockpit,lod=1,smooth=False)
solid=o.modifiers.new('Coaming thickness','SOLIDIFY');solid.thickness=.085
bevel=o.modifiers.new('Soft coaming edge','BEVEL');bevel.width=.022;bevel.segments=4
for side in [-1,1]:
    tube('HUD | shaped combiner support',[(3.23,side*.155,3.12),(3.21,side*.155,3.29),(3.16,side*.145,3.40)],.012,frame,sides=16,lod=1)
mesh('HUD | transparent combiner',[(3.21,-.145,3.26),(3.21,.145,3.26),(3.15,.145,3.41),(3.15,-.145,3.41)],[(0,1,2,3)],hudglass,lod=1)
# Material remains an unpowered optical combiner, with no invented avionics.
bpy.context.view_layer.update()
render_view('cockpit','final')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/Typhoon_7L-WA.blend'))
print('Cockpit shapes refined and three oleo suspension pivots added')
