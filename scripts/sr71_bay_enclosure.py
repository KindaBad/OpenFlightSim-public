"""Enclose gear bays below intact upper wing and corrected fuselage section.

Execute through Blender MCP in the existing sr71 namespace. Wider aft core
and bay sections are visual reconstructions, not reference-verified station CAD.
"""
validation_pose({})
if assembly.get('sr71_bays_enclosed'):raise RuntimeError('Enclosure already applied')

# Correct the rear centerbody cross section. Chines keep their verified envelope.
new_stations=[]
for x,w,core,z,up,down in body_stations:
    t=math.exp(-((x-21.6)/6.7)**4)
    new_stations.append((x,w,core*(1+.49*t),z,up+.16*t,down+.07*t))
new_rows=sample(new_stations,10)
def skin_height(x,y,upper=True):
    row=min(new_rows,key=lambda row:abs(row[0]-x));xx,w,core,z,up,down=row
    rim=math.exp(-1.8*(w/core)**2);centre=max(0,(math.exp(-1.8*(abs(y)/core)**2)-rim)/(1-rim));edge=max(0,1-(abs(y)/w)**2)
    return z+(.78*centre+.22*edge)*(up if upper else -down)
for v in body.data.vertices:
    row=min(body_rows,key=lambda row:abs(row[0]-v.co.x))
    v.co.z=skin_height(v.co.x,v.co.y,v.co.z>=row[3])
body.data.update()

sections=[(.9,15.50,30.72,2.46),(1.9,15.80,30.70,2.46),(2.4,16.05,30.68,2.45),(3.2,17.50,30.55,2.43),(4.14,19.05,30.40,2.43),(5.3,21.10,30.17,2.43),(5.9,23.30,30.10,2.45),(7.8,26.65,30.00,2.46),(8.28,27.78,29.61,2.46),(8.4709,28.60,29.12,2.46)]
wing_rows=sample(sections,10)
def wing_top(x,y):
    yy,le,te,z=min(wing_rows,key=lambda row:abs(row[0]-abs(y)));u=max(0,min(1,(x-le)/(te-le)))
    return z+5*.031*(te-le)*(.2969*math.sqrt(u)-.126*u-.3516*u*u+.2843*u**3-.1036*u**4)

for s,tag in [(-1,'L'),(1,'R')]:
    # Restore the full upper skin before making only an underside bay aperture.
    old=bpy.data.objects[tag+' | delta fixed wing'];bpy.data.objects.remove(old,do_unlink=True)
    signed=[(s*y,le,te,z) for y,le,te,z in sections]
    if s<0:signed.reverse()
    wing=wing_surface(tag+' | delta fixed wing',signed,mat=wingmat)
    for cutter in [lathe('temporary nacelle corridor',[(16.03,1.01),(32.8,1.01)],(s*4.14,2.70),dark,sectors=96,steps=0,caps=True,lod=0),box('temporary underside bay',(22.10,s*1.80,1.84),(2.54,3.30,1.42),dark,lod=0)]:
        bpy.context.view_layer.objects.active=wing;m=wing.modifiers.new('Underbody apertures','BOOLEAN');m.operation='DIFFERENCE';m.solver='EXACT';m.object=cutter
        bpy.ops.object.modifier_apply(modifier=m.name);bpy.data.objects.remove(cutter,do_unlink=True)
    for o in list(aircraft.objects):
        if o.name.startswith(tag+' main well |'):bpy.data.objects.remove(o,do_unlink=True)
    ys=[.15,.45,.75,1.05,1.35,1.65,1.85,2.2,2.6,3.0,3.45]
    def roof(xx,yy):return skin_height(xx,yy)-.035 if yy<1.85 else wing_top(xx,yy)-.035
    v=[];f=[]
    for xx in [20.84,23.37]:
        for yy in ys:v.append((xx,s*yy,roof(xx,yy)))
    for j in range(len(ys)-1):f.append((j,j+1,j+1+len(ys),j+len(ys)))
    mesh(tag+' main well | fitted roof',v,f,interior,lod=2)
    for xx in [20.84,23.37]:
        verts=[(xx,s*yy,roof(xx,yy)) for yy in ys]+[(xx,s*yy,1.84 if yy<1.4 else 2.17) for yy in reversed(ys)]
        mesh(tag+' main well | shaped end',verts,[tuple(range(len(verts)))],interior,lod=2,smooth=False)
    for yy in [.15,3.45]:
        mesh(tag+' main well | side',[(20.84,s*yy,1.83),(23.37,s*yy,1.83),(23.37,s*yy,roof(23.37,yy)),(20.84,s*yy,roof(20.84,yy))],[(0,1,2,3)],interior,lod=2,smooth=False)
    for xx in [21.04,21.57,22.10,22.63,23.16]:tube(tag+' main well | roof rib',[(xx,s*yy,roof(xx,yy)-.03) for yy in ys],.015,metal,sides=8,lod=1)
    bpy.data.objects['Gear_main_'+tag]['ofs_slide']=(.12,.30,s*.50)

# Fit the inner closed-door profile below the stowed outer tire. The door is
# deliberately curved into the lower chine, with a small reconstructed fairing.
validation_pose({'gear_door':1})
for o in aircraft.objects:
    if o.type!='MESH' or not o.parent or not o.parent.name.startswith('Gear_door_') or 'nose' in o.parent.name:continue
    if not o.parent.name.endswith('_0'):continue
    inv=o.matrix_world.inverted()
    for v in o.data.vertices:
        c=o.matrix_world@v.co
        if .40<abs(c.y)<1.4:c.z-=.12
        v.co=inv@c
validation_pose({})
assembly['sr71_bays_enclosed']=True
for name in ['front_quarter','rear_quarter','gear','top']:render_view(name,'enclosure')
validation_pose({'gear_fold':1,'gear_door':1});render_view('underside','enclosure_stowed');render_view('top','enclosure_stowed');validation_pose({})
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/SR71_61-7972.blend'))
print('Upper surfaces continuous; fitted bay roofs and inward stow pose saved')
