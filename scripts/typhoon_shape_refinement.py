"""Substantial second silhouette pass against the Swiss three-view and 7L-WA.

Dimensions and visible contours are public-reference reconstruction. The first
pass incorrectly placed foreplanes aft, enlarged them, exposed excessive engine
case length and made the fin too upright. Replace those meshes, retain the rig.
"""
def delete_prefixes(prefixes):
    for o in list(aircraft.objects):
        if o.name.startswith(tuple(prefixes)):bpy.data.objects.remove(o,do_unlink=True)

delete_prefixes(['Airframe | continuous fuselage','Cockpit | painted shoulder',
 'Canopy |','L | all moving canard','R | all moving canard',
 'L | foreplane root bearing','R | foreplane root bearing',
 'Tail | fixed swept fin','Tail | rudder','Dorsal | spine',
 'Tail | brake chute spine','L | engine nozzle shroud','R | engine nozzle shroud',
 'L | dorsal panel boundary','R | dorsal panel boundary'])

stations=[(0,.004,.004,2.53),(.5,.16,.15,2.55),(1.2,.39,.31,2.61),
 (2.04,.64,.47,2.66),(3.1,.72,.48,2.67),(4.2,.74,.49,2.61),
 (5.4,.77,.50,2.55),(6.4,.87,.54,2.48),(8,1.04,.57,2.35),
 (9.8,1.15,.59,2.25),(11.7,1.19,.59,2.19),(13.3,1.19,.54,2.08),(14.15,1.15,.50,2.02)]
sections=interpolate(stations,12);N=128;verts=[];faces=[];uv=[]
for x,w,h,z in sections:
    for i in range(N):
        a=2*math.pi*i/N;y=w*math.cos(a);zz=z+h*math.sin(a)
        if x>6:zz-=.10*abs(math.cos(a))**2
        verts.append((x,y,zz));uv.append((x/16,i/N))
for j in range(len(sections)-1):
    for i in range(N):
        theta=2*math.pi*(i+.5)/N
        if 2.10<sections[j][0]<6.12 and math.sin(theta)>.72:continue
        a=j*N+i;b=j*N+(i+1)%N;faces.append((a,b,b+N,a+N))
faces.append(tuple(range(N-1,-1,-1)))
body=mesh('Airframe | continuous fuselage',verts,faces,paint,uv=uv)
body.data.materials.append(radome)
for p in body.data.polygons:
    if p.center.x<2.04:p.material_index=1
    ids=[body.data.loops[i].vertex_index%N for i in p.loop_indices]
    if 0 in ids and N-1 in ids:
        for i in p.loop_indices:
            if body.data.loops[i].vertex_index%N==0:body.data.uv_layers.active.data[i].uv.y=1

# Single-seat bubble is broader and longer, and its rear follows the spine.
canopy_rows=interpolate([(2.07,.045,3.07,.045),(2.42,.28,3.075,.31),
 (3.18,.46,3.075,.65),(3.93,.50,3.04,.77),(4.80,.46,3.00,.67),
 (5.58,.31,2.99,.37),(6.13,.04,2.97,.04)],16)
verts=[];faces=[];uv=[];C=64
for x,w,z,h in canopy_rows:
    for i in range(C+1):
        a=math.pi*i/C;verts.append((x,w*math.cos(a),z+h*math.sin(a)));uv.append(((x-2.07)/4.06,i/C))
for j in range(len(canopy_rows)-1):
    for i in range(C):
        a=j*(C+1)+i;faces.append((a,a+1,a+C+2,a+C+1))
canopy=mesh('Canopy | smoked transparent shell',verts,faces,glass,canopy_group,uv=uv)
node=next(n for n in glass.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
node.inputs['Base Color'].default_value=(.19,.23,.22,.42)
node.inputs['Alpha'].default_value=.42;node.inputs['Roughness'].default_value=.09
node.inputs['Metallic'].default_value=.25
for side in [-1,1]:
    tube('Canopy | painted sill',[(x,side*w,z+.008) for x,w,z,h in canopy_rows],.035,paint,canopy_group,12,2)
    tube('Canopy | black seal',[(x,side*w,z+.025) for x,w,z,h in canopy_rows],.012,frame,canopy_group,10,1)
    verts=[];faces=[]
    for x,w,z,h in [r for r in sections if 2.10<r[0]<6.12]:
        cr=min(canopy_rows,key=lambda r:abs(r[0]-x));_,cw,cz,ch=cr
        for j in range(8):
            t=j/7;verts.append((x,side*((1-t)*w*math.sqrt(1-.72**2)+t*cw),z+h*.72+(cz-z-h*.72)*t))
    for j in range(len(verts)//8-1):
        for k in range(7):
            a=j*8+k;faces.append((a,a+1,a+9,a+8))
    mesh('Cockpit | painted shoulder '+str(side),verts,faces,paint)
row=min(canopy_rows,key=lambda r:abs(r[0]-3.19));x,w,z,h=row
tube('Canopy | windshield arch',[(x,w*math.cos(math.pi*i/64),z+h*math.sin(math.pi*i/64)) for i in range(65)],.024,paint,canopy_group,12,2)

# Existing interior detail moves with the corrected eye height, keeping controls
# beneath the coaming. No new avionics behavior is added.
for o in aircraft.objects:
    if o.type=='MESH' and o.name.startswith(('Cockpit |','Seat |','Panel |','HUD |')) and not o.name.startswith('Cockpit | painted shoulder'):
        for v in o.data.vertices:
            p=o.matrix_world@v.co;p.x+=.15;p.z+=.56;v.co=o.matrix_world.inverted()@p

for side,tag in [(-1,'L'),(1,'R')]:
    pv=scene.objects['ofs_canard_'+tag]
    # Keep world-space authored geometry and correctly reset the pivot location.
    pv.location=(3.47,side*.68,2.65)
    wing_surface(tag+' | all moving canard',[(side*.65,2.70,4.63,2.65),(side*2.10,3.99,4.66,2.68)],pv,thickness=.045,ns=32,nc=40,mat=wingpaint)
    ellipsoid(tag+' | foreplane root fairing',(3.47,side*.71,2.65),(.20,.09,.12),paint,lod=2)
    # The root leading edge blends into the fuselage instead of a naked board.
    ellipsoid(tag+' | wing root blend',(8.99,side*.99,1.96),(3.10,.32,.21),paint,segments=64,rings=24,lod=3)
    for o in aircraft.objects:
        if o.type=='MESH' and ('delta fixed wing' in o.name or 'outer elevon' in o.name or 'aerodynamic wingtip' in o.name):
            for v in o.data.vertices:
                p=o.matrix_world@v.co
                t=max(0,min(1,(abs(p.y)-.82)/4.42));p.x-=.38*t
                v.co=o.matrix_world.inverted()@p

# Curved swept fin cap, thin airfoil cross-section, beveled mechanical trailing
# boundary; the long upper sweep is characteristic of the real Typhoon.
outline=[(10.86,0,2.77),(11.26,0,2.96),(14.93,0,5.12),
 (15.17,0,5.25),(15.36,0,5.28),(15.58,0,5.26),(15.86,0,5.17),
 (14.23,0,2.68)]
fin=slab('Tail | fixed swept fin',outline,.065)
bevel=fin.modifiers.new('Rounded fin edge','BEVEL');bevel.width=.028;bevel.segments=3
rudder.location=(14.24,0,2.68)
rudder['ofs_axis']=(-.45,.893,0)
finrudder=slab('Tail | rudder',[(14.27,0,2.68),(15.89,0,5.17),(15.98,0,5.13),(14.74,0,2.61)],.05,group=rudder)
bevel=finrudder.modifiers.new('Rudder edge finish','BEVEL');bevel.width=.018;bevel.segments=3
ellipsoid('Tail | fin root fairing',(12.55,0,2.75),(1.70,.17,.14),paint,segments=64,rings=24,lod=3)
ellipsoid('Dorsal | spine',(8.20,0,2.64),(2.68,.30,.34),paint,segments=64,rings=32,lod=3)
ellipsoid('Tail | brake chute spine',(14.59,0,2.32),(1.37,.18,.17),paint,segments=64,rings=24,lod=3)

# Short exposed reheat nozzle, not two metres of dark engine case.
for side,tag in [(-1,'L'),(1,'R')]:
    y=side*.63;z=2.00
    for i in range(16):
        pv=scene.objects['ofs_nozzle_'+tag+'_%02d'%i]
        obj=scene.objects[tag+' | nozzle petal %02d'%i]
        world=[obj.matrix_world@v.co for v in obj.data.vertices]
        a=2*math.pi*(i+.48)/16
        pv.location=(14.12,y+.548*math.cos(a),z+.548*math.sin(a))
        bpy.context.view_layer.update()
        for v,p in zip(obj.data.vertices,world):
            p.x=14.12+(p.x-15.02)*(.63/.94);p.z+=.18
            v.co=obj.matrix_world.inverted()@p
        solid=obj.modifiers.new('Petal wall thickness','SOLIDIFY');solid.thickness=.012
        bevel=obj.modifiers.new('Petal edge finish','BEVEL');bevel.width=.004;bevel.segments=2
        obj['ofs_last_lod']=2
    verts=[];faces=[];K=96
    rings=[(13.15,.565),(13.86,.55),(14.12,.545),(14.12,.515),(14.70,.40),(13.25,.45)]
    for x,r in rings:
        for i in range(K):
            a=2*math.pi*i/K;verts.append((x,y+r*math.cos(a),z+r*math.sin(a)))
    for j in range(len(rings)-1):
        for i in range(K):
            a=j*K+i;b=j*K+(i+1)%K;faces.append((a,b,b+K,a+K))
    shell=mesh(tag+' | engine nozzle shroud and interior',verts,faces,paint)
    shell.data.materials.append(heat);shell.data.materials.append(dark)
    for p in shell.data.polygons:p.material_index=0 if p.index<2*K else 1 if p.index<4*K else 2
    for o in aircraft.objects:
        if o.type=='MESH' and o.name.startswith(tag+' nozzle |'):
            for v in o.data.vertices:v.co.x-=.9;v.co.z+=.18
        if o.type=='MESH' and o.name.startswith(tag+' | deep exhaust'):
            for v in o.data.vertices:v.co.x-=.9;v.co.z+=.18

# Intake lips receive a rolled, bevelled wall and a non-flat duct impression.
for side,tag in [(-1,'L'),(1,'R')]:
    lip=scene.objects[tag+' | rolled intake and deep duct']
    solid=lip.modifiers.new('Intake lip wall','SOLIDIFY');solid.thickness=.025
    bevel=lip.modifiers.new('Rolled intake edge','BEVEL');bevel.width=.027;bevel.segments=4
    for o in aircraft.objects:
        if o.type=='MESH' and o.name.startswith(tag+' livery |'):
            kind=next((k for label,k in [('7L roundel WA','serial'),('rescue arrow','rescue'),('ejection warning','eject'),('exhaust warning','exhaust'),('formation strip','formation')] if label in o.name),None)
            if kind:
                # Reconstruct patches on the corrected continuous surface.
                bpy.data.objects.remove(o,do_unlink=True)
    cheek(tag+' livery | 7L roundel WA','serial',6.45,2.62,1.50,.29,side,2)
    cheek(tag+' livery | rescue arrow','rescue',4.18,2.89,.71,.14,side,1)
    cheek(tag+' livery | ejection warning','eject',2.55,2.80,.23,.12,side,1)
    cheek(tag+' livery | formation strip','formation',5.18,2.94,.65,.047,side,1)

bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/Typhoon_7L-WA.blend'))
print('Substantial public-reference silhouette correction complete')
