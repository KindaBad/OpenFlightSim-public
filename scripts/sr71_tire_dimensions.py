"""Manufacturer-based tire correction executed through Blender MCP.

Goodyear historical application chart 7.3 (printed page35) lists SR71:
main27.5x7.5-16,22TL; nose25x6.75,16TL. Nominal sizes, not loaded radii.
"""
validation_pose({})
if assembly.get('sr71_tires_verified'):raise RuntimeError('Tire correction already applied')
main_r=27.5*.0254/2;nose_r=25*.0254/2
for s,tag in [(-1,'L'),(1,'R')]:
    cy=s*2.78
    for j in [-1,0,1]:
        w=bpy.data.objects['Wheel_main_'+tag+'_'+str(j)]
        old=Vector((22.37,cy+j*.345,.55));new=Vector((22.37,cy+j*.235,main_r))
        children=list(w.children)
        worlds={o.name:o.matrix_world.copy() for o in children}
        world=w.matrix_world.copy();world.translation=new;w.matrix_world=world
        bpy.context.view_layer.update()
        for o in children:
            inv=o.matrix_world.inverted()
            for v in o.data.vertices:
                c=worlds[o.name]@v.co-old
                c.x*=main_r/.55;c.z*=main_r/.55;c.y*=(7.5*.0254)/.26
                v.co=inv@(new+c)
    # Extend the lower fork/piston to the now smaller ground-level tire.
    for name in ['piston','axle','torque scissors','hydraulic line']:
        for o in aircraft.objects:
            if o.type!='MESH' or not o.name.startswith(tag+' main gear | '+name):continue
            inv=o.matrix_world.inverted()
            for v in o.data.vertices:
                c=o.matrix_world@v.co
                if c.z<1.20:c.z-=(.55-main_r)*min(1,(1.20-c.z)/.55)
                if name=='axle':c.y=cy+(c.y-cy)*(.235/.345)
                v.co=inv@c
    # Retraction slide now accounts for smaller wheels and a shorter packing
    # height, rather than distorting the tires to hide the original fit error.
    bpy.data.objects['Gear_main_'+tag]['ofs_slide']=(.12,.30,s*.42)

for s in [-1,1]:
    w=bpy.data.objects['Wheel_nose_'+str(s)];old=Vector((10.20,s*.19,.355));new=Vector((10.20,s*.19,nose_r))
    children=list(w.children);worlds={o.name:o.matrix_world.copy() for o in children}
    world=w.matrix_world.copy();world.translation=new;w.matrix_world=world;bpy.context.view_layer.update()
    for o in children:
        inv=o.matrix_world.inverted()
        for v in o.data.vertices:
            c=worlds[o.name]@v.co-old;c.x*=nose_r/.355;c.z*=nose_r/.355;c.y*=(6.75*.0254)/.17
            v.co=inv@(new+c)
rest={o.name:o.matrix_basis.copy() for o in aircraft.objects if o.type=='EMPTY'}
assembly['sr71_tires_verified']=True
assembly['main_tire_nominal_diameter_m']=main_r*2
assembly['nose_tire_nominal_diameter_m']=nose_r*2
for amount in [0,.5,1]:
    validation_pose({'gear_fold':amount,'gear_door':1 if amount==1 else 0})
    render_view('underside','tires_'+str(amount));render_view('top','tires_'+str(amount))
validation_pose({})
render_view('gear','tires');render_view('front_quarter','tires')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/SR71_61-7972.blend'))
print('Nominal tire diameters:',main_r*2,nose_r*2,'m; stow/ground poses saved')
