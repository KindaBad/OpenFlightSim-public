import bpy, math, os, random
from mathutils import Vector
from math import sin, cos, pi, sqrt
# Blender MCP executes without __file__; set OFS_ROOT or start Blender in the checkout.
ROOT=os.environ.get('OFS_ROOT') or (os.path.dirname(os.path.dirname(os.path.abspath(__file__))) if '__file__' in globals() else os.getcwd())
random.seed(320)
scene=bpy.context.scene
# Preserve any existing user objects outside our own collection.
old=bpy.data.collections.get('A320 • Airbus demonstrator')
if old:
    for ob in list(old.all_objects): bpy.data.objects.remove(ob,do_unlink=True)
    bpy.data.collections.remove(old)
root=bpy.data.collections.new('A320 • Airbus demonstrator'); scene.collection.children.link(root)
cols={}
for n in ['01 Fuselage','02 Wings & control surfaces','03 Empennage','04 CFM56 engines','05 Landing gear','06 Glazing & doors','07 Markings & small details','08 Presentation']:
    c=bpy.data.collections.new(n);root.children.link(c);cols[n[:2]]=c
COL=cols['01']
def link(ob):
    for c in list(ob.users_collection): c.objects.unlink(ob)
    COL.objects.link(ob);return ob

def mat(name,color,metal=0,rough=.3):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
    return m
white=mat('Airframe | warm white polyurethane',(.82,.855,.88),.23,.265)
wingmat=mat('Wings | light grey aerospace coating',(.55,.60,.65),.32,.30)
metal=mat('Brushed aluminium',(.51,.56,.61),.85,.24)
chrome=mat('Polished stainless steel',(.72,.78,.84),.95,.16)
dark=mat('Recesses | charcoal',(.013,.019,.025),.25,.38)
seam=mat('Panel joints | soft grey',(.20,.24,.28),.25,.44)
blue=mat('Airbus | deep blue',(.014,.055,.24),.3,.27)
blue2=mat('Airbus | mid blue',(.035,.19,.51),.3,.28)
blue3=mat('Airbus | sky blue',(.18,.43,.73),.23,.29)
blue4=mat('Airbus | pale blue',(.42,.65,.88),.22,.28)
rubber=mat('Tires | worn rubber',(.016,.019,.021),0,.73)
glass=mat('Glazing | smoked blue grey',(.025,.068,.095),.68,.13)
red=mat('Safety red',(.48,.012,.018),.25,.25)
# Microscopic paint variation; physically subtle at full scale.
for m in [white,wingmat,blue,blue2,rubber]:
    nt=m.node_tree;p=nt.nodes.get('Principled BSDF')
    n=nt.nodes.new('ShaderNodeTexNoise');n.inputs['Scale'].default_value=190 if m!=rubber else 95;n.inputs['Detail'].default_value=2
    b=nt.nodes.new('ShaderNodeBump');b.inputs['Strength'].default_value=.13;b.inputs['Distance'].default_value=.0007 if m!=rubber else .0015
    nt.links.new(n.outputs['Fac'],b.inputs['Height']);nt.links.new(b.outputs['Normal'],p.inputs['Normal'])
    if m!=rubber:p.inputs['Coat Weight'].default_value=.24;p.inputs['Coat Roughness'].default_value=.21

def mesh(name,vs,fs,m,smooth=True):
    me=bpy.data.meshes.new(name);me.from_pydata(vs,[],fs);me.update();o=bpy.data.objects.new(name,me);COL.objects.link(o)
    if m:o.data.materials.append(m)
    for p in me.polygons:p.use_smooth=smooth
    return o

def curve(name,pts,r,m,closed=False):
    cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';cu.resolution_u=2;cu.bevel_depth=r;cu.bevel_resolution=2
    s=cu.splines.new('POLY');s.points.add(len(pts)-1)
    for p,co in zip(s.points,pts):p.co=(*co,1)
    s.use_cyclic_u=closed;o=bpy.data.objects.new(name,cu);COL.objects.link(o);o.data.materials.append(m);return o

def uv(name,loc,scale,m,seg=48,rings=24):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg,ring_count=rings,location=loc);o=link(bpy.context.object);o.name=name;o.scale=scale;o.data.materials.append(m)
    for p in o.data.polygons:p.use_smooth=True
    return o

def cylinder(name,a,b,r,m,verts=32,r2=None):
    d=Vector(b)-Vector(a);mid=(Vector(a)+Vector(b))/2
    bpy.ops.mesh.primitive_cone_add(vertices=verts,radius1=r,radius2=r if r2 is None else r2,depth=d.length,location=mid)
    o=link(bpy.context.object);o.name=name;o.rotation_euler=d.to_track_quat('Z','Y').to_euler();o.data.materials.append(m)
    for p in o.data.polygons:p.use_smooth=True
    bevel=o.modifiers.new('Machined edge','BEVEL');bevel.width=.008;bevel.segments=2
    return o

def box(name,loc,scale,m,bevel=.02):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=link(bpy.context.object);o.name=name;o.dimensions=scale;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(m)
    if bevel:mod=o.modifiers.new('Rounded edges','BEVEL');mod.width=bevel;mod.segments=3
    return o

stations=[(0,.006,.018,3.25),(.025,.18,.15,3.25),(.08,.34,.28,3.25),(.20,.56,.46,3.26),(.40,.77,.63,3.29),(.8,.97,.79,3.35),(1.3,1.22,1.04,3.43),(1.8,1.42,1.28,3.53),(2.4,1.61,1.56,3.66),(3.1,1.77,1.82,3.77),(4.1,1.92,2.02,3.82),(5.4,1.975,2.07,3.82),(7,1.975,2.07,3.82),(24.8,1.975,2.07,3.82),(26.5,1.94,2.035,3.86),(28,1.82,1.94,3.94),(30,1.59,1.72,4.10),(32,1.24,1.40,4.29),(34,.84,1.01,4.48),(35.5,.51,.66,4.59),(36.8,.23,.34,4.65),(37.57,.005,.09,4.68)]
# Shape-preserving cubic interpolation avoids ripples along the straight barrel.
def interp(x,j):
    xs=[s[0] for s in stations];ys=[s[j] for s in stations];ds=[(ys[i+1]-ys[i])/(xs[i+1]-xs[i]) for i in range(len(xs)-1)]
    ms=[ds[0]]+[0 if ds[i-1]*ds[i]<=0 else 2/(1/ds[i-1]+1/ds[i]) for i in range(1,len(xs)-1)]+[ds[-1]]
    for i in range(len(xs)-1):
        if x<=xs[i+1]:
            h=xs[i+1]-xs[i];t=(x-xs[i])/h
            return (2*t**3-3*t*t+1)*ys[i]+(t**3-2*t*t+t)*h*ms[i]+(-2*t**3+3*t*t)*ys[i+1]+(t**3-t*t)*h*ms[i+1]
    return ys[-1]
def surface(x,a,side=1,off=0):return (x,side*(interp(x,1)+off)*cos(a),interp(x,3)+(interp(x,2)+off)*sin(a))
def surfz(x,z,side=1,off=.009):
    a=math.asin(max(-.999,min(.999,(z-interp(x,3))/interp(x,2))))
    return surface(x,a,side,off)
vs=[];fs=[];nx=340;nr=128
for i in range(nx+1):
    x=37.57*i/nx
    for j in range(nr):vs.append(surface(x,2*pi*j/nr))
for i in range(nx):
    for j in range(nr):a=i*nr+j;b=i*nr+(j+1)%nr;fs.append((a,b,b+nr,a+nr))
fs.extend([tuple(range(nr-1,-1,-1)),tuple(nx*nr+j for j in range(nr))])
fuse=mesh('A320 | continuous lofted pressure shell',vs,fs,white)
fuse['length_m']=37.57;fuse['fuselage_width_m']=3.95;fuse['basis']='Airbus AC A320 Jul 2025; exterior reference reconstruction'
# Nose radome join and lightning strips.
curve('Radome perimeter', [surface(1.24+.30*sin(a),a,1,.007) for a in [2*pi*j/180 for j in range(180)]],.007,seam,True)
for a in [-1.0,-.4,.2,.8,2.34,2.94,3.54,4.14]:
    curve('Radome lightning diverter',[surface(.24+i*.08,a,1,.006) for i in range(15)],.008,white)
# Belly wing-body canoe fairing.
uv('Wing body fairing', (16.0,0,2.06),(5.65,2.18,.77),wingmat,96,32)

COL=cols['02']
wingstations=[(1.55,12.8,6.08,2.66,.125),(3,13.56,5.25,2.78,.12),(5.75,15.04,3.95,3.02,.112),(8.3,16.45,3.28,3.24,.102),(11.91,18.43,2.54,3.56,.091),(16.29,20.84,1.64,3.94,.085),(16.85,21.31,1.41,4.00,.08)]
def wparams(y):
    for a,b in zip(wingstations,wingstations[1:]):
        if y<=b[0]:
            t=(y-a[0])/(b[0]-a[0]);return [a[i]*(1-t)+b[i]*t for i in range(1,5)]
    return list(wingstations[-1][1:])
def wp(y,u,side=1,upper=True):
    le,c,z,t=wparams(y);th=5*t*c*(.2969*sqrt(max(u,0))-.126*u-.3516*u*u+.2843*u**3-.1036*u**4)
    cam=.017*c*sin(pi*u);return (le+c*u,side*y,z+cam+(th if upper else -th))
for side in [-1,1]:
    tag='L' if side<0 else 'R';verts=[];faces=[];N=84;S=64
    us=[(1-cos(pi*j/N))/2 for j in range(N+1)]
    profile=[(u,True) for u in us]+[(u,False) for u in us[-2:0:-1]];n=len(profile)
    for i in range(S+1):
        y=1.55+(16.85-1.55)*i/S
        verts.extend(wp(y,u,side,up) for u,up in profile)
    for i in range(S):
        for j in range(n):faces.append((i*n+j,i*n+(j+1)%n,(i+1)*n+(j+1)%n,(i+1)*n+j))
    faces.extend([tuple(range(n-1,-1,-1)),tuple(S*n+j for j in range(n))])
    mesh(tag+' wing | swept airfoil',verts,faces,wingmat)
    for u in [.14,.62,.76]:
        curve(tag+' wing chordwise panel joint',[tuple(Vector(wp(1.95+i*.18,u,side))+Vector((0,0,.009))) for i in range(82)],.006,seam)
    # Separate leading-edge slat skin and individual trailing-edge panels.
    for ya,yb in [(2.15,5.4),(5.55,8.15),(8.3,11.75),(11.9,14.2),(14.35,16.65)]:
        for y in [ya,yb]:curve(tag+' slat end joint',[tuple(Vector(wp(y,u,side))+Vector((0,0,.007))) for u in [j*.14/25 for j in range(26)]],.007,seam)
    for ya,yb in [(2.15,5.5),(5.6,9.25),(9.35,12.65),(12.8,16.2)]:
        for y in [ya,yb]:curve(tag+' flap & aileron end joint',[tuple(Vector(wp(y,.63+j*.37/25,side))+Vector((0,0,.009))) for j in range(26)],.008,seam)
    # Five spoiler panels per side, flush with upper wing skin.
    for ya,yb in [(3.0,4.4),(4.5,5.7),(5.85,7.3),(7.45,9.0),(9.15,10.9)]:
        pts=[tuple(Vector(wp(y,u,side))+Vector((0,0,.012))) for y,u in [(ya,.46),(yb,.46),(yb,.68),(ya,.68)]]
        curve(tag+' spoiler perimeter',pts,.006,seam,True)
    for y in [3.3,8.3,11.91]:
        le,c,z,t=wparams(y);uv(tag+' flap track canoe',(le+c-.18,side*y,z-.28),(1.24,.17,.23),wingmat)
    # Tangent blended sharklet sweep, full span 35.8 m.
    sh=[(16.65,21.12,1.5,3.98),(17.08,21.52,1.37,4.07),(17.39,21.78,1.20,4.35),(17.55,22.03,1.05,4.81),(17.67,22.31,.89,5.43),(17.80,22.69,.68,6.13),(17.9,23.06,.39,6.56)]
    verts=[];faces=[];np=64
    for y,x,c,z in sh:
        for j in range(np):
            a=2*pi*j/np;u=(1-cos(a))/2;th=.05*c*sin(a)
            verts.append((x+c*u,side*(y+th),z))
    for i in range(len(sh)-1):
        for j in range(np):faces.append((i*np+j,i*np+(j+1)%np,(i+1)*np+(j+1)%np,(i+1)*np+j))
    faces.append(tuple((len(sh)-1)*np+j for j in range(np)))
    o=mesh(tag+' sharklet | blended upward tip',verts,faces,blue);mod=o.modifiers.new('Smooth composite curvature','SUBSURF');mod.levels=2

COL=cols['03']
# Vertical tail: spanwise loft, swept leading edge and nearly upright trailing edge.
finstations=[(5.32,27.20,8.10,.36),(5.75,28.38,6.86,.32),(6.30,29.57,5.65,.28),(7.1,30.52,4.78,.24),(8.5,31.68,3.89,.18),(10.95,33.75,2.20,.09),(11.76,34.40,1.76,.048)]
def finparam(z):
    for a,b in zip(finstations,finstations[1:]):
        if z<=b[0]:
            t=(z-a[0])/(b[0]-a[0]);return [a[i]*(1-t)+b[i]*t for i in range(1,4)]
    return list(finstations[-1][1:])
def fp(z,u,side=1,off=0):
    x,c,t=finparam(z);return (x+c*u,side*(2*t*sqrt(max(.00001,u*(1-u)))+off),z)
verts=[];faces=[];n=96
for z,x,c,t in finstations:
    for j in range(n):
        a=2*pi*j/n;u=(1-cos(a))/2;verts.append((x+c*u,t*sin(a),z))
for i in range(len(finstations)-1):
    for j in range(n):faces.append((i*n+j,i*n+(j+1)%n,(i+1)*n+(j+1)%n,(i+1)*n+j))
faces.extend([tuple(range(n-1,-1,-1)),tuple((len(finstations)-1)*n+j for j in range(n))])
fin=mesh('Vertical stabilizer | Airbus blue',verts,faces,blue)
for side in [-1,1]:
    curve('Rudder hinge line',[fp(6.15+i*.06,.77,side,.008) for i in range(93)],.009,seam)
    hs=[(1.0,30.08,5.42,4.6),(2.0,30.69,4.92,4.77),(4.0,32.20,3.55,5.13),(6.225,34.02,1.24,5.54)]
    v=[];f=[];n=80
    for y,x,c,z in hs:
        for j in range(n):
            a=2*pi*j/n;u=(1-cos(a))/2;v.append((x+c*u,side*y,z+.043*c*sin(a)))
    for i in range(len(hs)-1):
        for j in range(n):f.append((i*n+j,i*n+(j+1)%n,(i+1)*n+(j+1)%n,(i+1)*n+j))
    f.append(tuple((len(hs)-1)*n+j for j in range(n)))
    mesh(('L' if side<0 else 'R')+' horizontal stabilizer',v,f,wingmat)
    curve('Elevator hinge',[(x+c*.72,side*y,z+.043*c*sin(math.acos(1-2*.72))+.008) for y,x,c,z in hs],.009,seam)
print('AIRFRAME COMPLETE',len(root.all_objects))
bpy.app.driver_namespace['a320']=globals()
