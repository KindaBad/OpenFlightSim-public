"""Mechanical, cockpit and surface detail in the retained live MCP namespace."""
import json

# Close the cockpit aperture with a proper interior well below transparent glass.
cockpit=material('cockpit charcoal',(.025,.032,.035),.78)
cushion=material('seat olive textile',(.11,.13,.10),.93)
harness=material('seat harness',(.30,.33,.25),.8)
formation=material('formation light strip',(.32,.35,.26),.55,0,1,(.06,.075,.035))
hudglass=material('HUD combiner',(.12,.30,.24),.19,0,.23)
slab('Cockpit | rear bulkhead',[(5.40,-.40,2.05),(5.40,-.40,2.93),(5.60,-.40,2.93),(5.6,-.4,2.05)],.40,cockpit,lod=1)
mesh('Cockpit | floor',[(2.90,-.43,2.08),(5.51,-.43,2.08),(5.51,.43,2.08),(2.90,.43,2.08)],[(0,1,2,3)],cockpit,lod=1)
for s in [-1,1]:
    slab('Cockpit | sidewall',[(2.87,s*.43,2.1),(2.87,s*.43,2.47),(5.5,s*.43,2.47),(5.5,s*.43,2.1)],.04,cockpit,lod=1)
    ellipsoid('Cockpit | console',(4.25,s*.36,2.25),(.77,.12,.11),cockpit,segments=32,rings=12,lod=1)
    for j in range(11):
        tube('Cockpit | console switch',[(3.65+j*.09,s*.36,2.33),(3.65+j*.09,s*.36,2.36)],.013,metal,sides=8,lod=0)
    tube('Seat | ejection rail',[(4.88,s*.22,2.1),(5.03,s*.22,3.09)],.035,metal,sides=12,lod=1)
    tube('Seat | shoulder harness',[(4.84,s*.14,2.97),(4.62,s*.13,2.48),(4.36,s*.13,2.32)],.030,harness,sides=12,lod=1)
ellipsoid('Seat | back cushion',(4.81,0,2.67),(.14,.24,.36),cushion,segments=40,rings=24,lod=1)
ellipsoid('Seat | seat cushion',(4.51,0,2.29),(.34,.24,.12),cushion,segments=40,rings=24,lod=1)
ellipsoid('Seat | head box',(4.94,0,3.01),(.13,.23,.11),cockpit,lod=1)
tube('Seat | ejection handle',[(4.62,-.11,2.34),(4.58,-.11,2.39),(4.58,.11,2.39),(4.62,.11,2.34)],.018,harness,sides=10,lod=0)
ellipsoid('Panel | glare shield',(3.30,0,2.55),(.32,.43,.13),cockpit,lod=1)
slab('Panel | instrument mass',[(3.35,-.35,2.20),(3.2,-.35,2.56),(3.38,-.35,2.56),(3.58,-.35,2.2)],.35,cockpit,lod=1)
for y in [-.24,0,.24]:
    mesh('Panel | unpowered display',[(3.44,y-.08,2.27),(3.44,y+.08,2.27),(3.30,y+.08,2.44),(3.30,y-.08,2.44)],[(0,1,2,3)],dark,lod=1)
tube('Cockpit | control stick',[(4.19,0,2.15),(4.17,0,2.44),(4.12,0,2.48)],.025,cockpit,sides=16,lod=1)
for y in [-.16,.16]:
    tube('HUD | frame',[(3.04,y,2.59),(3.03,y,2.85),(3.14,y,2.93)],.018,frame,sides=12,lod=1)
mesh('HUD | transparent combiner',[(3.04,-.15,2.74),(3.04,.15,2.74),(3.14,.15,2.93),(3.14,-.15,2.93)],[(0,1,2,3)],hudglass,lod=1)

def tire(name,loc,r,width,group):
    verts=[];faces=[];uv=[];segments=64;rings=20
    # Flattened cross section with shoulder and sidewall, instead of a donut rim.
    for j in range(rings):
        b=2*math.pi*j/rings
        rr=r*.76+r*.24*math.cos(b)
        yy=width*.5*math.sin(b)
        for i in range(segments):
            a=2*math.pi*i/segments
            verts.append((loc[0]+rr*math.cos(a),loc[1]+yy,loc[2]+rr*math.sin(a)))
            uv.append((i/segments,j/rings))
    for j in range(rings):
        for i in range(segments):
            a=j*segments+i;b=j*segments+(i+1)%segments;c=((j+1)%rings)*segments+(i+1)%segments;d=((j+1)%rings)*segments+i
            faces.append((a,b,c,d))
    mesh(name+' | tire shoulder and sidewall',verts,faces,rubber,group,uv=uv,lod=2)
    for y in [-width*.42,0,width*.42]:
        tube(name+' | circumferential tread',[(loc[0]+r*.998*math.cos(2*math.pi*i/96),loc[1]+y,loc[2]+r*.998*math.sin(2*math.pi*i/96)) for i in range(97)],.006,dark,group,8,0)
    tube(name+' | rim barrel',[(loc[0],loc[1]-width*.48,loc[2]),(loc[0],loc[1]+width*.48,loc[2])],r*.50,metal,group,48,2)
    for s in [-1,1]:
        yy=loc[1]+s*width*.5
        tube(name+' | axle cap',[(loc[0],yy,loc[2]),(loc[0],yy+s*.025,loc[2])],r*.15,metal,group,24,1)
        for k in range(8):
            a=2*math.pi*k/8;x=loc[0]+r*.35*math.cos(a);z=loc[2]+r*.35*math.sin(a)
            ellipsoid(name+' | hub recess',(x,yy+s*.003,z),(.043,.006,.043),dark,group,16,8,1)
            ellipsoid(name+' | hub fastener',(x,yy+s*.011,z),(.012,.005,.012),metal,group,12,6,0)

# Clear openings for gear bays, preserving UVs/normals on the surrounding shell.
bm=bmesh.new();bm.from_mesh(body.data)
remove=[]
for f in bm.faces:
    c=f.calc_center_median()
    if (3.28<c.x<4.04 and abs(c.y)<.24 and c.z<1.60) or (8.88<c.x<10.18 and abs(c.y)>.61 and c.z<1.55):remove.append(f)
bmesh.ops.delete(bm,geom=remove,context='FACES');bm.to_mesh(body.data);bm.free()

for s,tag in [(-1,'L'),(1,'R')]:
    pv=pivot('ofs_main_gear_'+tag,(9.20,s*.89,1.60),'gear_fold',axis=(1,0,0),gain=-s*1.36,slide=(0,.83,0))
    tube(tag+' gear | upper housing',[(9.20,s*.89,1.6),(9.38,s*1.45,.87)],.083,white,pv,32,2)
    tube(tag+' gear | chrome oleo',[(9.38,s*1.45,.98),(9.5,s*1.45,.49)],.051,metal,pv,32,2)
    tube(tag+' gear | brace',[(8.97,s*.76,1.54),(9.34,s*1.34,.96),(9.5,s*1.45,.70)],.039,white,pv,20,1)
    tube(tag+' gear | actuator',[(9.83,s*.85,1.40),(9.4,s*1.40,.8)],.027,metal,pv,16,1)
    for y in [s*1.45-.09,s*1.45+.09]:
        tube(tag+' gear | torque link',[(9.34,y,.95),(9.23,y,.76),(9.45,y,.65)],.020,metal,pv,12,1)
        tube(tag+' gear | brake hose',[(9.21,y,1.48),(9.33,y,1.12),(9.35,y,.86),(9.52,y,.53)],.011,dark,pv,10,0)
    wheel=pivot('ofs_main_wheel_'+tag,(9.5,s*1.45,.46),'wheel',group=pv)
    tire(tag+' main wheel',(9.5,s*1.45,.46),.46,.25,wheel)
    # Inboard brake stack, concentric rings visible at close range.
    for rr in [.19,.16,.125]:
        tube(tag+' brake | rotor ring',[(9.5+rr*math.cos(i*2*math.pi/40),s*1.31,.46+rr*math.sin(i*2*math.pi/40)) for i in range(41)],.012,heat,wheel,10,0)
    door=pivot('ofs_main_door_'+tag,(9.21,s*.84,1.25),'gear_door',axis=(1,0,0),gain=s*1.12)
    mesh(tag+' gear | bay door',[(8.90,s*.86,1.22),(10.17,s*.86,1.22),(10.17,s*1.39,1.2),(8.90,s*1.39,1.2)],[(0,1,2,3)],paint,door,smooth=False,lod=2)
    for x in [9.00,9.48,9.95]:
        tube(tag+' bay | rib',[(x,s*.69,1.63),(x,s*1.01,1.62),(x,s*1.11,1.32)],.018,white,sides=10,lod=1)
    mesh(tag+' bay | recessed dark ceiling',[(8.88,s*.65,1.69),(10.19,s*.65,1.69),(10.19,s*1.12,1.62),(8.88,s*1.12,1.62)],[(0,1,2,3)],dark,lod=1)
    # Actuator fairing under the delta, a shaped hull rather than a box.
    for y in [2.0,3.7]:
        ellipsoid(tag+' | elevon actuator canoe',(12.45,s*y,1.77),(.61,.115,.16),paint,segments=40,rings=16,lod=2)
    tube(tag+' | bare outer station rail',[(11.94,s*4.76,1.76),(13.38,s*4.76,1.76)],.035,metal,sides=16,lod=2)

nose=pivot('ofs_nose_gear',(3.7,0,1.50),'gear_fold',gain=-1.62,slide=(0,.26,0))
tube('Nose gear | upper housing',[(3.7,0,1.50),(3.7,0,.77)],.064,white,nose,32,2)
tube('Nose gear | sliding oleo',[(3.7,0,.91),(3.7,0,.36)],.040,metal,nose,32,2)
tube('Nose gear | drag brace',[(4.05,0,1.46),(3.88,0,.96),(3.7,0,.77)],.027,white,nose,20,1)
steer=pivot('ofs_nose_steering',(3.7,0,.54),'steering',axis=(0,1,0),group=nose)
for s in [-1,1]:
    tube('Nose gear | fork',[(3.7,s*.13,.61),(3.72,s*.13,.32)],.024,white,steer,16,1)
wheel=pivot('ofs_nose_wheel',(3.7,0,.31),'nose_wheel',group=steer)
tire('Nose wheel',(3.7,0,.31),.31,.20,wheel)
tube('Nose gear | hydraulic hose',[(3.67,-.08,1.44),(3.63,-.08,1.0),(3.69,-.08,.55)],.010,dark,nose,10,0)
ellipsoid('Nose gear | landing lamp',(3.65,0,.84),(.047,.073,.06),lamp,nose,24,12,1)
for s in [-1,1]:
    door=pivot('ofs_nose_door_'+str(s),(3.25,s*.23,1.50),'gear_door',axis=(1,0,0),gain=s*1.35)
    mesh('Nose bay | door '+str(s),[(3.27,s*.23,1.5),(4.04,s*.23,1.5),(4.04,s*.04,1.5),(3.27,s*.04,1.5)],[(0,1,2,3)],paint,door,smooth=False,lod=2)
mesh('Nose bay | recessed ceiling',[(3.28,-.23,1.84),(4.04,-.23,1.84),(4.04,.23,1.84),(3.28,.23,1.84)],[(0,1,2,3)],dark,lod=1)

# Proper individual petal pivots: dry opening -> reheat opening, four-degree
# outward rotation around tangential root hinges. Geometry never scales whole jet.
for s,tag in [(-1,'L'),(1,'R')]:
    for i in range(16):
        a=2*math.pi*(i+.48)/16
        pv=pivot('ofs_nozzle_'+tag+'_%02d'%i,(15.02,s*.63+.548*math.cos(a),1.82+.548*math.sin(a)),
                 'nozzle_'+tag,axis=(0,math.cos(a),math.sin(a)),gain=.075)
        parent(scene.objects[tag+' | nozzle petal %02d'%i],pv)
        # Hinged mechanical overlap strips and root fasteners.
        tube(tag+' nozzle | actuator link',[(14.70,s*.63+.57*math.cos(a),1.82+.57*math.sin(a)),
                                           (15.05,s*.63+.555*math.cos(a),1.82+.555*math.sin(a))],.018,metal,sides=10,lod=0)
        ellipsoid(tag+' nozzle | root screw',(15.0,s*.63+.554*math.cos(a),1.82+.554*math.sin(a)),(.016,.016,.016),metal,segments=12,rings=8,lod=0)

# Reference-observed probes, lights, grills and longitudinal seams.
for s,tag in [(-1,'L'),(1,'R')]:
    tube(tag+' | air data probe',[(2.4,s*.51,1.86),(2.02,s*.62,1.86),(1.83,s*.64,1.86)],.017,metal,sides=12,lod=1)
    for j in range(16):
        x=6.54+j*.055
        tube(tag+' | cheek vent slat',[(x,s*.845,1.62),(x+.026,s*.81,1.79)],.008,dark,sides=6,lod=0)
    for x in [7.1,8.9,11.8]:
        # Short curved seams conform to body cross section, restrained size.
        row=min(sections,key=lambda r:abs(r[0]-x))
        xx,w,h,z=row
        tube(tag+' | dorsal panel boundary',[(xx,s*w*math.cos(a),z+h*math.sin(a)+.004) for a in [.2+i*.04 for i in range(26)]],.0025,frame,sides=6,lod=0)
slab('Dorsal | VHF blade',[(7.1,0,2.70),(7.35,0,3.00),(7.60,0,2.98),(7.6,0,2.71)],.024,paint,lod=2)
slab('Ventral | antenna',[(10.2,0,1.18),(10.38,0,.98),(10.60,0,1.01),(10.62,0,1.18)],.021,paint,lod=1)
ellipsoid('Dorsal | anticollision lens',(7.30,0,2.91),(.07,.055,.046),red,segments=24,rings=12,lod=1)
tube('Tail | spine outlet',[(11.15,0,2.76),(11.43,0,2.76)],.115,heat,sides=40,lod=2)
ellipsoid('Tail | spine outlet interior',(11.43,0,2.76),(.007,.086,.086),dark,lod=1)

# Gun port is starboard, aligned with registry muzzle (x=8.3-2.95=5.35).
tube('Gun | recessed starboard muzzle',[(5.35,.82,1.22),(5.67,.82,1.22)],.048,heat,sides=32,lod=1)
ellipsoid('Gun | bore darkness',(5.348,.82,1.22),(.008,.033,.033),dark,lod=1)

# Import original texture sets into PBR nodes; data maps use linear data space.
def texture_set(mat,basename):
    n=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    n.inputs['Base Color'].default_value=(1,1,1,1)
    for suffix in ['base','mr','normal']:
        image=bpy.data.images.load(str(ROOT/'assets/typhoon/textures'/f'{basename}_{suffix}.png'),check_existing=True)
        if suffix!='base':image.colorspace_settings.name='Non-Color'
        tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=image
        if suffix=='base':mat.node_tree.links.new(tex.outputs['Color'],n.inputs['Base Color'])
        elif suffix=='mr':
            sep=mat.node_tree.nodes.new('ShaderNodeSeparateColor')
            mat.node_tree.links.new(tex.outputs['Color'],sep.inputs[0])
            mat.node_tree.links.new(sep.outputs[1],n.inputs['Roughness'])
            mat.node_tree.links.new(sep.outputs[2],n.inputs['Metallic'])
        else:
            norm=mat.node_tree.nodes.new('ShaderNodeNormalMap');norm.inputs['Strength'].default_value=.55
            mat.node_tree.links.new(tex.outputs['Color'],norm.inputs['Color'])
            mat.node_tree.links.new(norm.outputs['Normal'],n.inputs['Normal'])

texture_set(paint,'fuselage')
wingpaint=material('delta and foreplane paint',(.4,.43,.44),.6)
texture_set(wingpaint,'wings')
for o in aircraft.objects:
    if o.type=='MESH' and any(s in o.name for s in ['delta fixed wing','elevon','all moving canard']):o.data.materials[0]=wingpaint
# Fix cylindrical UV seam explicitly; the wrap edge must not interpolate across
# the whole sheet. Gear aperture operations preserve the surviving loop UVs.
for p in body.data.polygons:
    ids=[body.data.loops[i].vertex_index%96 for i in p.loop_indices]
    if 0 in ids and 95 in ids:
        for i in p.loop_indices:
            if body.data.loops[i].vertex_index%96==0:body.data.uv_layers.active.data[i].uv.y=1

markings=material('Austrian markings atlas',(1,1,1),.64)
node=next(n for n in markings.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
tex=markings.node_tree.nodes.new('ShaderNodeTexImage')
tex.image=bpy.data.images.load(str(ROOT/'assets/typhoon/textures/markings.png'),check_existing=True)
markings.node_tree.links.new(tex.outputs['Color'],node.inputs['Base Color'])
markings.node_tree.links.new(tex.outputs['Alpha'],node.inputs['Alpha'])
rects=json.loads((ROOT/'assets/typhoon/textures/markings_layout.json').read_text())

def decal(name,kind,verts,group=assembly,lod=1):
    x0,y0,x1,y1=rects[kind]
    uv=[(x0/2048,1-y1/2048),(x1/2048,1-y1/2048),(x1/2048,1-y0/2048),(x0/2048,1-y0/2048)]
    return mesh(name,verts,[(0,1,2,3)],markings,group,uv=uv,smooth=False,lod=lod)

def cheek(name,kind,x,z,w,h,s,lod=1):
    # Sample the fuselage cross-section so decals follow its curvature. Subdivide
    # as a patch to eliminate coplanar bleed and lifting at wide serial corners.
    x0,y0,x1,y1=rects[kind];verts=[];faces=[];uv=[];nx=20;ny=6
    for j in range(ny+1):
        zz=z+(j/ny-.5)*h
        for i in range(nx+1):
            xx=x+((-1 if s>0 else 1)*(i/nx-.5)*w)
            idx=min(range(len(sections)),key=lambda k:abs(sections[k][0]-xx))
            _,ww,hh,zc=sections[idx]
            yy=ww*math.sqrt(max(.06,1-((zz-zc)/hh)**2))+.009
            verts.append((xx,s*yy,zz));uv.append(((x0+(x1-x0)*i/nx)/2048,1-(y1-(y1-y0)*j/ny)/2048))
    for j in range(ny):
        for i in range(nx):
            a=j*(nx+1)+i;faces.append((a,a+1,a+nx+2,a+nx+1))
    return mesh(name,verts,faces,markings,uv=uv,smooth=False,lod=lod)

for s,tag in [(-1,'L'),(1,'R')]:
    cheek(tag+' livery | 7L roundel WA','serial',6.28,2.12,1.46,.28,s,2)
    cheek(tag+' livery | rescue arrow','rescue',3.85,2.32,.82,.18,s,1)
    cheek(tag+' livery | ejection warning','eject',3.13,2.21,.30,.12,s,1)
    cheek(tag+' livery | exhaust warning','exhaust',13.52,2.12,.54,.064,s,0)
    cheek(tag+' livery | formation strip','formation',5.24,2.38,.65,.044,s,1)
    for underside in [False,True]:
        zz=2.055 if not underside else 1.83
        x=11.50;y=s*3.82;r=.42
        decal(tag+' wing | Austrian roundel '+str(underside),'roundel',[(x-r,y-r,zz),(x+r,y-r,zz),(x+r,y+r,zz),(x-r,y+r,zz)],lod=2)
    decal(tag+' intake | danger stencil','intake',[(4.9,s*1.085,1.09),(5.65,s*1.00,1.09),(5.65,s*1.00,1.20),(4.9,s*1.085,1.20)],lod=1)
    for x,y in [(4.83,s*1.42),(10.25,s*2.13),(11.33,s*3.5)]:
        decal(tag+' | NO STEP','no_step',[(x-.14,y-.032,2.04),(x+.14,y-.032,2.04),(x+.14,y+.032,2.04),(x-.14,y+.032,2.04)],lod=1)

bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/Typhoon_7L-WA.blend'))
print('TYPHOON detail/UV/material stage:',len(aircraft.objects),'objects')
