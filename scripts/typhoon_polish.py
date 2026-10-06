"""Final close-up finish after the second reference-driven shape pass."""
for image in bpy.data.images:
    if image.filepath.endswith('markings.png'):
        if image.packed_file:image.unpack(method='USE_ORIGINAL')
        image.reload()
finpaint=material('fin paint',(.4,.43,.44),.63);texture_set(finpaint,'tail')
for o in aircraft.objects:
    if o.name in ['Tail | fixed swept fin','Tail | rudder']:
        o.data.materials[0]=finpaint
        for p in o.data.polygons:
            for li in p.loop_indices:
                v=o.matrix_world@o.data.vertices[o.data.loops[li].vertex_index].co
                o.data.uv_layers.active.data[li].uv=((v.x-10.6)/5.5,(v.z-2.5)/2.8)
nozzlepaint=material('heat stained nozzle petals',(.1,.1,.1),.6,.6);texture_set(nozzlepaint,'nozzle')
for o in aircraft.objects:
    if 'nozzle petal' in o.name:o.data.materials[0]=nozzlepaint
glass.surface_render_method='BLENDED';glass['ofs_environment_reflection']=1.0
markings.surface_render_method='BLENDED'

# The aft fuselage skin must end around the cases, never inside the exhaust.
bm=bmesh.new();bm.from_mesh(body.data);remove=[]
for f in bm.faces:
    c=f.calc_center_median()
    if c.x>13.2 and min((c.y-.63)**2+(c.z-2)**2,(c.y+.63)**2+(c.z-2)**2)<.565**2:remove.append(f)
bmesh.ops.delete(bm,geom=remove,context='FACES');bm.to_mesh(body.data);bm.free()

for o in aircraft.objects:
    if o.type=='MESH' and o.name.startswith('Seat |'):
        for v in o.data.vertices:v.co.x-=.25;v.co.z-=.10
    if 'nozzle | actuator link' in o.name:o.data.materials[0]=heat
for o in list(aircraft.objects):
    if o.name.startswith(('Seat | back cushion','Seat | head box','L | wing root blend','R | wing root blend')):
        bpy.data.objects.remove(o,do_unlink=True)
# Shaped cushion with chamfered shoulder corners, not an oval car seat.
verts=[];faces=[]
for j in range(18):
    t=j/17;z=2.81+.65*t;width=.235*(1-.22*t**6)
    for i in range(17):
        u=2*i/16-1;verts.append((4.70+.18*t-.025*(1-u*u),width*u,z))
for j in range(17):
    for i in range(16):
        a=j*17+i;faces.append((a,a+1,a+18,a+17))
seat=mesh('Seat | shaped back cushion',verts,faces,cushion,lod=1)
solid=seat.modifiers.new('Cushion thickness','SOLIDIFY');solid.thickness=.07
head=slab('Seat | shaped head box',[(4.80,0,3.36),(4.79,0,3.54),(4.98,0,3.56),(5.07,0,3.39)],.19,cockpit,lod=1)
bevel=head.modifiers.new('Head box rounded corners','BEVEL');bevel.width=.027;bevel.segments=3
for s,tag in [(-1,'L'),(1,'R')]:
    wing_surface(tag+' | wing root blended fillet',[(s*.91,6.30,12.31,2.18),(s*1.35,6.83,12.38,1.995)],thickness=.048,ns=20,nc=48,mat=paint)
    # Canard root fairing and fin cap use quiet satin paint.
for o in aircraft.objects:
    if o.name=='Tail | rudder':
        for v in o.data.vertices:
            p=o.matrix_world@v.co;p.x=min(p.x,15.96);v.co=o.matrix_world.inverted()@p
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/Typhoon_7L-WA.blend'))
print('Glass, fin/nozzle textures, seat, wing fillet and exhaust aperture polished')
