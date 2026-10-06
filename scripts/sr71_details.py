"""Detailed SR-71 secondary structures, articulation and cockpit through MCP.

Run once after the corrected silhouette in bpy.app.driver_namespace['sr71'].
Mechanisms/sections are photograph-inspired approximations, not engineering CAD.
"""
from mathutils import Matrix
if bpy.data.objects.get('Nose gear | upper oleo'):
    raise RuntimeError('Detail stage already exists; apply corrections in place')

# All articulation extras use glTF coordinates while authoring geometry stays Z-up.
for o in aircraft.objects:
    if o.type=='EMPTY':o['sr71_axes_converted']=True


def ring(name,center,radius,section,mat=metal,group=None,lod=1,sectors=64):
    pts=[];fs=[];uv=[]
    for j,(dx,dr) in enumerate(section):
        for i in range(sectors+1):
            a=i*2*math.pi/sectors
            pts.append((center[0]+dx,center[1]+(radius+dr)*math.cos(a),center[2]+(radius+dr)*math.sin(a)))
            uv.append((i/sectors,j/max(1,len(section)-1)))
    for j in range(len(section)-1):
        for i in range(sectors):
            a=j*(sectors+1)+i;fs.append((a,a+1,a+sectors+2,a+sectors+1))
    return mesh(name,pts,fs,mat,group,uv,lod=lod)


def wheel(name,loc,r,width,group):
    sectors=96;vs=[];fs=[];uv=[]
    profile=[(-.53*width,.70*r),(-.55*width,.83*r),(-.47*width,.94*r),(-.28*width,.994*r),
             (0,r),(.28*width,.994*r),(.47*width,.94*r),(.55*width,.83*r),(.53*width,.70*r)]
    for j,(dy,rad) in enumerate(profile):
        for i in range(sectors+1):
            a=i*2*math.pi/sectors;vs.append((loc[0]+rad*math.sin(a),loc[1]+dy,loc[2]+rad*math.cos(a)));uv.append((i/sectors,j/8))
    for j in range(len(profile)-1):
        for i in range(sectors):
            a=j*(sectors+1)+i;fs.append((a,a+1,a+sectors+2,a+sectors+1))
    mesh(name+' tire',vs,fs,rubber,group,uv,lod=2)
    # Transverse axle and open concentric hub, brakes, separate rim lip.
    tube(name+' axle',[(loc[0],loc[1]-width*.58,loc[2]),(loc[0],loc[1]+width*.58,loc[2])],r*.24,metal,group,sides=32,lod=2)
    for side in [-1,1]:
        yy=loc[1]+side*width*.52
        v=[];f=[];N=64
        for rad,offset in [(r*.70,0),(r*.65,.012),(r*.44,.022),(r*.32,.024)]:
            for i in range(N):
                a=i*2*math.pi/N;v.append((loc[0]+rad*math.sin(a),yy+side*offset,loc[2]+rad*math.cos(a)))
        for j in range(3):
            for i in range(N):
                a=j*N+i;b=j*N+(i+1)%N;f.append((a,b,b+N,a+N))
        mesh(name+' wheel rim '+str(side),v,f,metal,group,lod=1)
        for i in range(10):
            a=i*2*math.pi/10;xx=loc[0]+r*.47*math.sin(a);zz=loc[2]+r*.47*math.cos(a)
            tube(name+' hub bolt',[(xx,yy+side*.019,zz),(xx,yy+side*.045,zz)],r*.037,chrome,group,sides=6,lod=0)
        # Brake pack behind the hub is darker than polished outer metal.
        tube(name+' brake pack',[(loc[0],yy-side*.03,loc[2]),(loc[0],yy-side*.07,loc[2])],r*.42,heat,group,sides=32,lod=1)
    return group

# Replace a uniform canopy material with glazing bounded by opaque roof/frame.
glass.surface_render_method='BLENDED'
node=next(n for n in glass.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
node.inputs['IOR'].default_value=1.46;node.inputs['Roughness'].default_value=.13
for label,lo,hi in [('Front',4.53,6.87),('Rear',6.87,8.56)]:
    shell=bpy.data.objects[label+' | canopy silhouette shell'];shell.data.materials.append(glass)
    for p in shell.data.polygons:
        c=p.center;xx=c.x;yy=abs(c.y)
        # Windshield and small lateral windows; keep the central opaque roof.
        windshield=label=='Front' and 4.85<xx<5.31 and c.z>3.11
        sidewindow=(5.37<xx<6.34 if label=='Front' else 7.06<xx<7.79) and yy>.25 and c.z>3.10
        if windshield or sidewindow:p.material_index=1
    group=bpy.data.objects['Canopy_'+label]
    rows=[r for r in sample(canopy_stations,12) if lo<=r[0]<=hi]
    for side in [-1,1]:
        tube(label+' canopy | sill seal',[(x,side*w,z+.01) for x,w,z,h in rows],.022,frame,group,lod=2)
        # A narrow backing skirt fills the reconstructed fuselage aperture edge.
        v=[];f=[]
        for x,w,z,h in rows:v.extend([(x,side*w,z),(x,side*w*.94,z-.19)])
        for i in range(len(rows)-1):f.append((i*2,i*2+1,i*2+3,i*2+2))
        mesh(label+' canopy | lower skirt '+str(side),v,f,paint,group,lod=3)
    for xx in ([5.31,6.39,6.82] if label=='Front' else [6.92,7.04,7.82]):
        x,w,z,h=min(rows,key=lambda r:abs(r[0]-xx))
        tube(label+' canopy | structural arch',[(x,w*math.cos(math.pi*i/48),z+(h-z)*math.sin(math.pi*i/48)**.84+.009) for i in range(49)],.018,frame,group,sides=10,lod=2)
    # Small seals at the upper side glazing borders.
    for side in [-1,1]:
        a=.99 if side==1 else math.pi-.99
        windowrows=[r for r in rows if (5.32<r[0]<6.41 if label=='Front' else 7.0<r[0]<7.84)]
        tube(label+' canopy | window roof seal',[(x,w*math.cos(a),z+(h-z)*math.sin(a)**.84+.008) for x,w,z,h in windowrows],.012,frame,group,sides=8,lod=1)

# Tandem cockpits with two seats, analog instrument massing, control sticks and
# side consoles. No invented modern HUD or working avionics.
for label,xpilot,xpanel,width in [('Front',5.95,5.25,.72),('Rear',7.63,7.04,.66)]:
    box(label+' cockpit | floor',(xpilot,0,2.34),(1.33,width,.075),interior,lod=1,bevel=.018)
    box(label+' cockpit | aft bulkhead',(xpilot+.44,0,2.95),(.055,width,1.12),interior,lod=1,bevel=.014)
    box(label+' seat | bucket',(xpilot+.05,0,2.63),(.46,.43,.15),metal,lod=1,bevel=.045)
    box(label+' seat | cushion',(xpilot+.04,0,2.73),(.41,.38,.12),seatmat,lod=1,bevel=.045)
    back=box(label+' seat | backrest',(xpilot+.31,0,3.02),(.105,.41,.52),seatmat,lod=1,bevel=.040)
    box(label+' seat | headrest',(xpilot+.30,0,3.38),(.14,.32,.17),seatmat,lod=1,bevel=.028)
    for side in [-1,1]:
        tube(label+' seat | ejection rail',[(xpilot+.40,side*.24,2.45),(xpilot+.42,side*.24,3.39)],.025,metal,sides=12,lod=1)
        tube(label+' seat | harness',[(xpilot+.245,side*.11,3.30),(xpilot+.245,side*.08,2.91),(xpilot+.1,side*.15,2.78)],.016,whitepaint,sides=8,lod=0)
        box(label+' cockpit | console '+str(side),(xpilot-.04,side*.35,2.86),(.98,.18,.20),interior,lod=1,bevel=.025)
        for k in range(7):
            box(label+' console | switch',(xpilot-.43+k*.095,side*.35,2.982),(.023,.044,.012),metal,lod=0,bevel=.003)
        tube(label+' cockpit | sill duct',[(xpilot-.55,side*.39,3.005),(xpilot+.43,side*.36,3.005)],.025,interior,lod=1)
    box(label+' cockpit | analog instrument panel',(xpanel,0,3.075),(.11,width*.98,.40),interior,lod=1,bevel=.025)
    box(label+' cockpit | glare shield',(xpanel+.05,0,3.285),(.35,width+.10,.045),frame,lod=1,bevel=.012)
    for row in range(3):
        for col in range(5 if label=='Front' else 4):
            y=(col-(2 if label=='Front' else 1.5))*.119;z=2.96+row*.113
            # Gauges face aft toward the crew. The index marks and needles are
            # original geometry, not copied photographic cockpit textures.
            rad=.043 if row else .036
            tube(label+' panel | gauge bezel',[(xpanel+.059,y,z),(xpanel+.068,y,z)],rad,metal,sides=32,lod=0)
            tube(label+' panel | gauge face',[(xpanel+.069,y,z),(xpanel+.071,y,z)],rad*.89,dark,sides=32,lod=0)
            for tick in range(12):
                a=tick*math.pi/6;rr=rad*.70
                tube(label+' gauge | index',[(xpanel+.073,y+rr*math.cos(a),z+rr*math.sin(a)),(xpanel+.073,y+(rr+.005)*math.cos(a),z+(rr+.005)*math.sin(a))],.0015,whitepaint,sides=4,lod=0)
            a=.7+col*.3+row*.4
            tube(label+' gauge | needle',[(xpanel+.074,y,z),(xpanel+.074,y+rad*.64*math.cos(a),z+rad*.64*math.sin(a))],.0016,whitepaint,sides=4,lod=0)
    if label=='Front':
        tube('Pilot | control stick',[(5.61,0,2.52),(5.62,0,2.86),(5.56,0,3.00)],.023,interior,sides=16,lod=1)
        box('Pilot | stick grip',(5.55,0,2.99),(.06,.055,.12),frame,lod=1,bevel=.018)
        for s in [-1,1]:box('Pilot | rudder pedal',(5.35,s*.13,2.50),(.11,.10,.05),metal,lod=1,bevel=.008)
        tube('Pilot | throttle levers',[(5.78,-.35,2.98),(5.77,-.35,3.04)],.019,metal,lod=0)
    else:
        box('RSO | navigation display',(xpanel+.061,0,3.09),(.021,.21,.14),dark,lod=1,bevel=.015)

# Nose well side walls and ceiling. Only exposed cavity surfaces are retained.
box('Nose gear | well ceiling',(9.77,0,2.33),(1.95,.68,.07),interior,lod=2)
for s in [-1,1]:box('Nose gear | well side',(9.77,s*.37,1.91),(1.95,.05,.75),interior,lod=2)
for x in [8.83,10.73]:box('Nose gear | well end',(x,0,1.92),(.06,.75,.75),interior,lod=2)
nose=pivot('Gear_nose',(9.26,0,2.13),'gear_fold',axis=(0,1,0),gain=-1.50,slide=(.36,0,.16))
tube('Nose gear | upper oleo',[(9.42,0,2.03),(9.80,0,1.34)],.105,metal,nose,sides=32,lod=2)
compression=pivot('Compression_nose',(9.80,0,1.34),'compression_nose',slide=(0,0,1),group=nose)
tube('Nose gear | chrome piston',[(9.80,0,1.40),(10.02,0,.62)],.066,chrome,compression,sides=32,lod=2)
steer=pivot('Steering_nose',(10.04,0,.55),'steering',group=compression)
tube('Nose gear | fork',[(9.98,-.23,.73),(10.20,-.23,.42),(10.20,.23,.42),(9.98,.23,.73)],.046,metal,steer,sides=16,lod=2)
for s in [-1,1]:
    w=pivot('Wheel_nose_'+str(s),(10.20,s*.19,.355),'nose_wheel',axis=(0,1,0),gain=-1,group=steer)
    wheel('Nose '+str(s),(10.20,s*.19,.355),.355,.17,w)
tube('Nose gear | drag brace',[(8.96,0,2.10),(9.18,0,1.57),(9.84,0,.98)],.052,metal,nose,lod=1)
tube('Nose gear | actuator',[(8.97,.12,1.97),(9.62,.12,1.42)],.037,chrome,nose,lod=1)
tube('Nose gear | brake hose',[(9.48,.11,1.99),(9.91,.12,1.44),(10.08,.15,.53)],.012,dark,nose,sides=8,lod=1)
for s in [-1,1]:
    # Baseline authored doors hang open; channel=1 closes them onto the belly.
    door=pivot('Gear_door_nose_'+str(s),(9.77,s*.38,1.57),'gear_door',axis=(1,0,0),gain=-s*math.pi/2)
    box('Nose gear | open door '+str(s),(9.77,s*.395,1.19),(1.93,.045,.75),paint,door,lod=2,bevel=.012)
    box('Nose gear | door inner rib '+str(s),(9.77,s*.43,1.21),(1.79,.032,.045),metal,door,lod=1)

# Main wells cut through the actual wing solids. Three wheels per main axle.
for s,tag in [(-1,'L'),(1,'R')]:
    y=s*2.78
    cutter=box('temporary main bay cutter',(22.10,y,2.10),(2.42,1.31,1.03),dark,lod=0)
    wing=bpy.data.objects[tag+' | delta fixed wing']
    bpy.context.view_layer.objects.active=wing;wing.select_set(True)
    mod=wing.modifiers.new('Actual main wheel well aperture','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter,do_unlink=True)
    box(tag+' main well | ceiling',(22.1,y,2.58),(2.40,1.30,.065),interior,lod=2)
    for sy in [-1,1]:box(tag+' main well | side',(22.1,y+sy*.655,2.12),(2.40,.045,.84),interior,lod=2)
    for x in [20.92,23.28]:box(tag+' main well | end',(x,y,2.12),(.05,1.30,.84),interior,lod=2)
    for x in [21.10,21.53,21.96,22.39,22.82,23.15]:
        tube(tag+' main well | structure',[(x,y-.60,2.54),(x,y+.60,2.54)],.024,metal,lod=1)
    gear=pivot('Gear_main_'+tag,(21.25,y,2.20),'gear_fold',axis=(1,0,0),gain=-s*1.46,slide=(.12,0,.09))
    tube(tag+' main gear | upper oleo',[(21.45,y,2.09),(21.83,y,1.45)],.143,metal,gear,sides=32,lod=2)
    comp=pivot('Compression_'+tag,(21.83,y,1.45),'compression_'+tag,slide=(0,0,1),group=gear)
    tube(tag+' main gear | piston',[(21.83,y,1.51),(22.21,y,.76)],.09,chrome,comp,sides=32,lod=2)
    tube(tag+' main gear | axle',[(22.37,y-.57,.55),(22.37,y+.57,.55)],.089,metal,comp,sides=32,lod=2)
    for j in [-1,0,1]:
        yy=y+j*.345;w=pivot('Wheel_main_'+tag+'_'+str(j),(22.37,yy,.55),'wheel',axis=(0,1,0),gain=-1,group=comp)
        wheel(tag+' main '+str(j),(22.37,yy,.55),.55,.26,w)
    tube(tag+' main gear | drag brace',[(20.93,y-.20,2.11),(21.24,y-.14,1.40),(22.09,y-.12,.98)],.064,metal,gear,lod=1)
    for sy in [-1,1]:
        tube(tag+' main gear | torque scissors',[(21.79,y+sy*.13,1.48),(22.08,y+sy*.13,1.32),(22.09,y+sy*.13,1.06)],.032,metal,comp,sides=12,lod=1)
    tube(tag+' main gear | hydraulic line',[(21.48,y+.16,2.05),(21.98,y+.16,1.35),(22.35,y+.14,.59)],.013,dark,gear,sides=8,lod=1)
    for sy in [-1,1]:
        yy=y+sy*.67
        door=pivot('Gear_door_'+tag+'_'+str(sy),(22.1,yy,1.72),'gear_door',axis=(1,0,0),gain=-sy*math.pi/2)
        box(tag+' main gear | door',(22.1,yy,1.39),(2.40,.055,.65),paint,door,lod=2,bevel=.014)
        for x in [21.14,21.78,22.42,23.04]:box(tag+' main door | stiffener',(x,yy+sy*.035,1.41),(.045,.038,.54),metal,door,lod=1)

# Detailed dual inlet and ejector assemblies.
for s,tag in [(-1,'L'),(1,'R')]:
    cy=s*4.14;cz=2.70
    ring(tag+' inlet | capture lip',(18.17,cy,cz),.920,[(-.017,0),(0,.019),(.024,.027),(.061,.017),(.075,0)],metal,lod=3,sectors=128)
    ring(tag+' inlet | shock trap',(19.07,cy,cz),.855,[(0,0),(.036,.012),(.076,0)],dark,lod=2,sectors=96)
    spike=bpy.data.objects['Inlet_spike_'+tag]
    for x,r in [(18.35,.765),(18.51,.786),(18.68,.790)]:
        ring(tag+' spike | bleed ring',(x,cy,cz),r,[(0,.004),(.012,.007),(.021,.004)],dark,spike,lod=1,sectors=96)
    for i in range(24):
        a=i*math.pi/12
        yy=cy+.787*math.cos(a);zz=cz+.787*math.sin(a)
        tube(tag+' spike | bleed slit',[(18.42,yy,zz),(18.62,yy,zz)],.007,dark,spike,sides=6,lod=0)
    # Recessed bypass/louver impressions wrapped onto the curved nacelle surface.
    for x,rad,nt in [(19.38,1.09,5),(20.12,1.117,6),(27.22,.996,5)]:
        for a in [math.pi*.25,math.pi*.5,math.pi*.75,math.pi*1.25,math.pi*1.5,math.pi*1.75]:
            yy=cy+rad*math.cos(a);zz=cz+rad*math.sin(a)
            for j in range(nt):
                dx=j*.09
                p0=(x+dx,yy-.125*math.sin(a),zz+.125*math.cos(a));p1=(x+dx,yy+.125*math.sin(a),zz-.125*math.cos(a))
                tube(tag+' nacelle | bypass louver',[p0,p1],.011,dark,sides=8,lod=1)
    # Compressor suggestion well behind the cone base, never a flat mouth wall.
    ring(tag+' intake | compressor surround',(22.03,cy,cz),.65,[(0,0),(.055,0)],metal,lod=1)
    for i in range(32):
        a=i*math.pi/16;b=a+.055
        v=[(22.04,cy+.42*math.cos(a),cz+.42*math.sin(a)),(22.10,cy+.62*math.cos(b),cz+.62*math.sin(b)),(22.10,cy+.62*math.cos(b+.075),cz+.62*math.sin(b+.075)),(22.04,cy+.42*math.cos(a+.075),cz+.42*math.sin(a+.075))]
        mesh(tag+' intake | guide vane',v,[(0,1,2,3)],metal,lod=1)
    # Ejector blow-in door band and overlapping high-temperature petals.
    ring(tag+' nozzle | actuator band',(28.75,cy,cz),.956,[(-.028,0),(0,.014),(.035,.014),(.07,0)],heat,lod=2,sectors=96)
    for i in range(24):
        a0=i*2*math.pi/24
        v=[];f=[];uv=[]
        for j in range(11):
            t=j/10;x=28.77+t*1.28;r=.958-.092*t+.008*math.sin(math.pi*t)
            for k in range(9):
                a=a0+(k/8)*2*math.pi/24*.94
                v.append((x,cy+r*math.cos(a),cz+r*math.sin(a)));uv.append((t,k/8))
        for j in range(10):
            for k in range(8):
                a=j*9+k;f.append((a,a+1,a+10,a+9))
        petal=pivot('Nozzle_'+tag+'_'+str(i),(28.77,cy,cz),'nozzle_'+tag,axis=(0,0,1),gain=0,slide=(0,.030*math.cos(a0),.030*math.sin(a0)))
        mesh(tag+' nozzle | ejector petal %02d'%i,v,f,heat,petal,uv,lod=1)
        tube(tag+' nozzle | petal seam',[(28.80,cy+.960*math.cos(a0),cz+.960*math.sin(a0)),(30.04,cy+.870*math.cos(a0),cz+.870*math.sin(a0))],.010,dark,petal,sides=6,lod=1)
        # Small actuating link is worth retaining in hero close-up only.
        tube(tag+' nozzle | linkage',[(28.61,cy+.975*math.cos(a0),cz+.975*math.sin(a0)),(28.92,cy+.982*math.cos(a0),cz+.982*math.sin(a0))],.018,metal,sides=8,lod=0)
    ring(tag+' exhaust | flameholder',(27.64,cy,cz),.49,[(0,0),(.044,.025),(.075,0)],heat,lod=1)
    for i in range(8):
        a=i*math.pi/4
        tube(tag+' exhaust | internal brace',[(27.72,cy+.16*math.cos(a),cz+.16*math.sin(a)),(27.72,cy+.57*math.cos(a),cz+.57*math.sin(a))],.023,heat,lod=1)

# Exterior features supported at common-arrangement level. Tiny stencils/panels
# will be textures; visible structures are kept as geometry.
tube('Nose | pitot probe',[(.015,0,2.28),(-.12,0,2.28)],.009,metal,sides=12,lod=2)
box('Refuel | recessed door surround',(11.65,0,3.148),(1.12,.68,.018),frame,lod=2,bevel=.04)
box('Refuel | access door',(11.65,0,3.160),(1.01,.57,.014),paint,lod=2,bevel=.035)
box('ANS | astro tracker window',(10.70,0,3.150),(.31,.27,.025),glass,lod=2,bevel=.03)
for s in [-1,1]:
    # Under-chine dark sensor windows are reconstructed from common bay layouts.
    box('Sensor bay | window surround',(6.73,s*1.00,2.10),(.79,.47,.032),frame,lod=2,bevel=.018)
    box('Sensor bay | optical window',(6.73,s*1.00,2.083),(.69,.37,.018),glass,lod=1,bevel=.015)
    tube('Wingtip | subtle navigation lens',[(28.94,s*8.38,2.48),(29.08,s*8.37,2.48)],.025,navred if s<0 else navgreen,sides=16,lod=1)
box('Rear | drag chute housing',(31.54,0,2.49),(.64,.49,.22),paint,lod=2,bevel=.08)

bpy.context.view_layer.update()
for o in aircraft.objects:
    if o.type=='EMPTY':o['sr71_axes_converted']=True
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/SR71_61-7972.blend'))
print('Detail stage:',len(aircraft.objects),'objects')
