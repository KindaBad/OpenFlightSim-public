COL=cols['04']
fanmetal=mat('Fan blades | titanium',(.16,.21,.24),.88,.29)
heat=mat('Exhaust | heat discoloured titanium',(.24,.215,.18),.84,.32)
def lathe(name,profile,y,z,m,n=128,closed=False):
    # Cubic section interpolation rounds the inlet without faceted meridians.
    raw=profile;profile=[]
    for i in range(len(raw)-1):
        p0=Vector(raw[max(0,i-1)]);p1=Vector(raw[i]);p2=Vector(raw[i+1]);p3=Vector(raw[min(len(raw)-1,i+2)])
        for j in range(8):
            t=j/8;v=.5*((2*p1)+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t*t+(-p0+3*p1-3*p2+p3)*t*t*t)
            profile.append((v.x,max(.001,v.y)))
    profile.append(raw[-1])
    v=[];f=[]
    for x,r in profile:
        for j in range(n):
            a=2*pi*j/n;v.append((x,y+r*cos(a),z+r*sin(a)))
    count=len(profile) if closed else len(profile)-1
    for i in range(count):
        for j in range(n):f.append((i*n+j,i*n+(j+1)%n,((i+1)%len(profile))*n+(j+1)%n,((i+1)%len(profile))*n+j))
    return mesh(name,v,f,m)
for side in [-1,1]:
    tag='L' if side<0 else 'R';y=side*5.75;z=1.84
    # CFM56 nacelle is hollow; rolled inlet lip leads into a deep acoustic duct.
    lathe(tag+' CFM56 | fan cowl',[(11.55,1.047),(11.8,1.065),(12.4,1.08),(13.1,1.055),(13.8,.98),(14.3,.86),(14.47,.79),(14.47,.74),(14.12,.76)],y,z,white)
    lathe(tag+' CFM56 | rolled metal intake lip',[(11.55,1.047),(11.31,1.022),(11.19,.965),(11.195,.913),(11.28,.874),(11.48,.867),(11.65,.879)],y,z,chrome)
    lathe(tag+' CFM56 | acoustic intake barrel',[(11.48,.868),(11.9,.875),(12.3,.89),(12.47,.875)],y,z,dark)
    lathe(tag+' CFM56 | aft bypass duct',[(14.08,.785),(14.55,.746),(14.65,.70),(14.64,.66),(14.2,.64)],y,z,metal)
    lathe(tag+' CFM56 | core exhaust',[(13.84,.55),(14.45,.52),(14.97,.41),(15.06,.36),(15.05,.31),(14.9,.32)],y,z,heat)
    lathe(tag+' CFM56 | exhaust cone',[(14.15,.31),(14.8,.29),(15.3,.20),(15.62,.06),(15.68,.003)],y,z,heat)
    cylinder(tag+' fan shadow backing',(12.68,y,z),(12.72,y,z),.87,dark,96)
    # 36 individual swept and twisted titanium fan blades, 1.735 m fan diameter.
    for k in range(36):
        v=[];f=[];base=2*pi*k/36
        for i in range(11):
            t=i/10;r=.225+.635*t
            for j in range(5):
                u=j/4;a=base+.22*t*t+(u-.5)*(.19-.05*t)
                x=12.40+.21*(u-.5)*(1-.25*t)+.055*sin(pi*t)
                v.append((x,y+r*cos(a),z+r*sin(a)))
        for i in range(10):
            for j in range(4):a=i*5+j;f.append((a,a+1,a+6,a+5))
        o=mesh(tag+f' fan blade {k+1:02}',v,f,fanmetal);mod=o.modifiers.new('Blade thickness','SOLIDIFY');mod.thickness=.012
    lathe(tag+' CFM56 | spinner',[(11.91,.008),(11.96,.09),(12.07,.16),(12.23,.215),(12.48,.235)],y,z,metal)
    # White spinner spiral painted on the conical front surface.
    pts=[]
    for i in range(120):
        t=i/119;r=.012+.185*t;a=pi*3.9*t;x=11.91+.30*(r/.215)**1.6
        pts.append((x-.003,y+r*cos(a),z+r*sin(a)))
    curve(tag+' spinner spiral',pts,.012,white)
    for x,r in [(11.66,1.055),(12.98,1.063),(13.7,.996),(14.25,.88)]:
        curve(tag+' nacelle circumferential seam',[(x,y+r*cos(a),z+r*sin(a)) for a in [2*pi*j/160 for j in range(160)]],.006,seam,True)
    for a in [.05,pi-.05,-pi/2]:
        curve(tag+' fan cowl longitudinal split',[(x,y+r*cos(a),z+r*sin(a)) for x,r in [(11.67,1.053),(12.4,1.085),(13.1,1.06),(13.8,.985)]],.0055,seam)
    for x in [11.94,12.37,12.80]:
        for a in [-1.03,-2.11]:
            p=(x,y+1.07*cos(a),z+1.07*sin(a));o=box(tag+' flush cowl latch',p,(.12,.04,.075),metal,.008)
    # Slender mounting pylon with an airfoil cross-section.
    v=[];f=[];sections=[(2.47,12.8,2.7,.16),(2.84,14.40,2.4,.18),(3.10,15.44,1.50,.10)]
    n=64
    for zz,x,c,w in sections:
        for j in range(n):a=2*pi*j/n;v.append((x+c*(1-cos(a))/2,y+w*sin(a),zz))
    for i in range(len(sections)-1):
        for j in range(n):f.append((i*n+j,i*n+(j+1)%n,(i+1)*n+(j+1)%n,(i+1)*n+j))
    f.append(tuple((len(sections)-1)*n+j for j in range(n)))
    mesh(tag+' engine pylon',v,f,wingmat)
    # Acoustic liner segmentation visible in the intake.
    for k in range(24):
        a=2*pi*k/24
        curve(tag+' acoustic liner seam',[(11.62,y+.87*cos(a),z+.87*sin(a)),(12.35,y+.887*cos(a),z+.887*sin(a))],.0025,metal)

COL=cols['05']
def wheel(name,x,y,z,r,width):
    # Lathed radial tire profile across axle, with rounded sidewalls.
    prof=[(-width*.50,r*.53),(-width*.54,r*.69),(-width*.49,r*.85),(-width*.34,r*.97),(-width*.20,r),(width*.20,r),(width*.34,r*.97),(width*.49,r*.85),(width*.54,r*.69),(width*.50,r*.53)]
    v=[];f=[];n=80
    for dy,rr in prof:
        for k in range(n):a=2*pi*k/n;v.append((x+rr*sin(a),y+dy,z+rr*cos(a)))
    for i in range(len(prof)):
        for k in range(n):f.append((i*n+k,i*n+(k+1)%n,((i+1)%len(prof))*n+(k+1)%n,((i+1)%len(prof))*n+k))
    mesh(name+' | tire',v,f,rubber)
    for dy in [-.19,-.065,.065,.19]:
        if abs(dy)>width*.40:continue
        curve(name+' | circumferential tread groove',[(x+(r+.001)*sin(a),y+dy,z+(r+.001)*cos(a)) for a in [2*pi*k/180 for k in range(180)]],.009,dark,True)
    cylinder(name+' | wheel rim',(x,y-width*.48,z),(x,y+width*.48,z),r*.55,metal,64)
    for s in [-1,1]:
        yy=y+s*width*.51
        cylinder(name+' | recessed hub',(x,yy,z),(x,yy+s*.012,z),r*.42,dark,48)
        cylinder(name+' | hubcap',(x,yy+s*.014,z),(x,yy+s*.035,z),r*.23,metal,48)
        # Rim lip and cast spokes / bolt circle.
        curve(name+' | rim bead',[(x+r*.51*sin(a),yy,z+r*.51*cos(a)) for a in [2*pi*k/100 for k in range(100)]],.014,chrome,True)
        for k in range(10):
            a=2*pi*k/10;rr=r*.35
            cylinder(name+' | wheel bolt',(x+rr*sin(a),yy,z+rr*cos(a)),(x+rr*sin(a),yy+s*.035,z+rr*cos(a)),.019,chrome,12)
            cylinder(name+' | cast spoke',(x+r*.24*sin(a),yy,z+r*.24*cos(a)),(x+r*.45*sin(a),yy,z+r*.45*cos(a)),.025,metal,12)
for side in [-1,1]:
    y=side*3.795;x=17.71;tag='L main' if side<0 else 'R main'
    for dy in [-.465,.465]:wheel(tag+(' inner' if dy*side<0 else ' outer'),x,y+dy,.61,.61,.43)
    cylinder(tag+' axle',(x,y-.68,.65),(x,y+.68,.65),.12,metal)
    cylinder(tag+' chrome oleo',(x,y,.69),(x-.18,y,1.63),.115,chrome)
    cylinder(tag+' upper shock strut',(x-.18,y,1.36),(x-.36,y*.94,2.88),.18,wingmat)
    cylinder(tag+' upper collar',(x-.19,y,1.38),(x-.23,y,1.58),.215,metal)
    cylinder(tag+' drag brace',(x-.13,y,1.27),(x-1.46,y*.83,2.71),.095,wingmat)
    cylinder(tag+' side stay',(x-.15,y,1.1),(x-.50,side*2.22,2.56),.09,metal)
    cylinder(tag+' torque link top',(x-.24,y-.17,1.6),(x+.22,y-.17,1.14),.045,metal)
    cylinder(tag+' torque link bottom',(x+.22,y-.17,1.14),(x,y-.17,.88),.045,metal)
    curve(tag+' brake hydraulic hose',[(x-.36,y+.21,2.6),(x-.26,y+.22,1.8),(x+.13,y+.2,1.28),(x+.15,y+.23,.82),(x,y+.35,.66)],.023,rubber)
    o=box(tag+' gear door',(17.35,y+side*.32,2.04),(1.08,.065,1.25),white,.04);o.rotation_euler[0]=side*.15
    for dy in [-.37,.37]:cylinder(tag+' carbon brake pack',(x,y+dy-.06,.61),(x,y+dy+.06,.61),.28,heat)
# Nose gear at the Airbus 5.07 m station; wheelbase 12.64 m.
x=5.07
for y in [-.25,.25]:wheel('Nose '+('L' if y<0 else 'R'),x,y,.39,.39,.19)
cylinder('Nose axle',(x,-.43,.39),(x,.43,.39),.075,metal)
cylinder('Nose chrome oleo',(x,0,.49),(4.98,0,1.03),.08,chrome)
cylinder('Nose steering strut',(4.98,0,.96),(4.91,0,1.95),.125,wingmat)
cylinder('Nose drag brace',(4.99,0,.92),(5.74,0,1.90),.063,metal)
for y in [-.12,.12]:
    cylinder('Nose fork',(5.0,y,.8),(5.07,y,.4),.06,metal)
    cylinder('Nose torque scissors',(4.88,y,1.21),(4.69,y,.87),.035,metal)
    cylinder('Nose torque scissors',(4.69,y,.87),(5.0,y,.67),.035,metal)
curve('Nose hydraulic line',[(4.81,-.13,1.87),(4.81,-.15,1.3),(4.84,-.16,.86),(5.04,-.16,.56)],.016,rubber)
for side in [-1,1]:
    o=box('Nose gear forward door',(4.56,side*.41,1.44),(.83,.045,.68),white,.035);o.rotation_euler[0]=side*.15
    o=box('Nose gear aft door',(5.38,side*.38,1.79),(.65,.045,.30),white,.025)
    cylinder('Nose taxi light housing',(4.80,side*.18,1.26),(4.63,side*.18,1.26),.095,dark)
    cylinder('Nose taxi light glass',(4.62,side*.18,1.26),(4.61,side*.18,1.26),.077,chrome)
print('MECHANICAL COMPLETE',len(root.all_objects))
