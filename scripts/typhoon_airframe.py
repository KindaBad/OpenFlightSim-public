"""Owned Austrian 7L-WA mesh. Execute in the live Blender through Blender MCP.

Metres: X aft from nose, Y starboard, Z up. Measured envelope 15.96/10.95/5.28.
Station/planform contours reconstructed from cited public views, not CAD.
This stage creates a separate scene and preserves all pre-existing user scenes.
"""
import bpy
import bmesh
import math
from pathlib import Path
from mathutils import Vector, Quaternion

import os
# Blender MCP executes without __file__; set OFS_ROOT or start Blender in the checkout.
ROOT = Path(os.environ.get('OFS_ROOT') or (Path(__file__).resolve().parents[1] if '__file__' in globals() else Path.cwd()))
SCENE_NAME = 'OFS Typhoon 7L-WA'
if SCENE_NAME in bpy.data.scenes:
    raise RuntimeError('Typhoon scene already exists; use correction scripts rather than overwrite')
scene = bpy.data.scenes.new(SCENE_NAME)
bpy.context.window.scene = scene
scene.unit_settings.scale_length = 1
aircraft = bpy.data.collections.new('Typhoon | airframe and rig')
scene.collection.children.link(aircraft)
studio = bpy.data.collections.new('Typhoon | validation studio')
scene.collection.children.link(studio)
assembly = bpy.data.objects.new('TYPHOON | 7L-WA assembly', None)
aircraft.objects.link(assembly)
assembly['reference_length_m'] = 15.96
assembly['reference_span_m'] = 10.95
assembly['reference_height_m'] = 5.28

def material(name, color, roughness=.55, metallic=0, alpha=1, emission=None):
    m = bpy.data.materials.new('Typhoon | '+name)
    m.use_nodes = True
    n = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    n.inputs['Base Color'].default_value = (*color, alpha)
    n.inputs['Metallic'].default_value = metallic
    n.inputs['Roughness'].default_value = roughness
    n.inputs['Alpha'].default_value = alpha
    if emission:
        n.inputs['Emission Color'].default_value = (*emission, 1)
        n.inputs['Emission Strength'].default_value = 1
    m.diffuse_color = (*color,alpha)
    return m

paint=material('air defence gray',(.40,.43,.445),.61)
radome=material('radome warm gray',(.32,.335,.33),.69)
metal=material('satin aluminium',(.55,.59,.61),.31,.8)
heat=material('nozzle titanium',(.18,.17,.15),.46,.85)
dark=material('duct darkness',(.012,.014,.016),.86)
rubber=material('tire rubber',(.019,.020,.022),.9)
frame=material('canopy graphite frame',(.075,.085,.09),.44,.18)
glass=material('smoke tinted canopy',(.17,.24,.27),.16,0,.19)
white=material('gear light gray',(.69,.72,.73),.48,.12)
red=material('port nav',(.42,.012,.008),.24,0,1,(.9,.018,.009))
green=material('starboard nav',(.012,.27,.07),.24,0,1,(.015,.8,.1))
lamp=material('landing and strobe lens',(.76,.8,.83),.22,0,1,(1,1,1))

def parent(obj, group=assembly):
    # Geometry in aircraft coordinates; maintain world position at the pivot.
    bpy.context.view_layer.update()
    world=obj.matrix_world.copy()
    obj.parent=group
    obj.matrix_world=world
    return obj

def mesh(name, verts, faces, mat=paint, group=assembly, uv=None, smooth=True, lod=3):
    data=bpy.data.meshes.new(name)
    data.from_pydata(verts,[],faces)
    data.materials.append(mat)
    data.update()
    bm=bmesh.new(); bm.from_mesh(data)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(data); bm.free()
    for p in data.polygons: p.use_smooth=smooth
    layer=data.uv_layers.new(name='UVMap')
    for p in data.polygons:
        for li in p.loop_indices:
            vi=data.loops[li].vertex_index
            v=data.vertices[vi].co
            layer.data[li].uv=uv[vi] if uv else (v.x/16, (v.y+5.5)/11)
    o=bpy.data.objects.new(name,data); aircraft.objects.link(o)
    parent(o,group); o['ofs_last_lod']=lod
    return o

def pivot(name, location, channel, axis=(0,0,1), gain=1, slide=(0,0,0), group=assembly):
    o=bpy.data.objects.new(name,None); aircraft.objects.link(o); o.location=location
    o['ofs_channel']=channel; o['ofs_axis']=axis; o['ofs_gain']=gain; o['ofs_slide']=slide
    return parent(o,group)

def tube(name, points, radius, mat=metal, group=assembly, sides=16, lod=1):
    verts=[]; faces=[]
    for j,point in enumerate(points):
        p=Vector(point)
        d=Vector(points[min(j+1,len(points)-1)])-Vector(points[max(j-1,0)])
        q=Vector((0,0,1)).rotation_difference(d.normalized())
        for i in range(sides):
            a=2*math.pi*i/sides
            verts.append(tuple(p+q@Vector((radius*math.cos(a),radius*math.sin(a),0))))
    for j in range(len(points)-1):
        for i in range(sides):
            a=j*sides+i; b=j*sides+(i+1)%sides
            faces.append((a,b,b+sides,a+sides))
    faces += [tuple(range(sides-1,-1,-1)),tuple((len(points)-1)*sides+i for i in range(sides))]
    return mesh(name,verts,faces,mat,group,lod=lod)

def ellipsoid(name,loc,size,mat=paint,group=assembly,segments=48,rings=24,lod=2):
    verts=[]; faces=[]
    for j in range(rings+1):
        b=math.pi*(.001+(1-.002)*j/rings)
        for i in range(segments):
            a=2*math.pi*i/segments
            verts.append((loc[0]+size[0]*math.sin(b)*math.cos(a),loc[1]+size[1]*math.sin(b)*math.sin(a),loc[2]+size[2]*math.cos(b)))
    for j in range(rings):
        for i in range(segments):
            a=j*segments+i; b=j*segments+(i+1)%segments
            faces.append((a,b,b+segments,a+segments))
    faces += [tuple(range(segments-1,-1,-1)),tuple(rings*segments+i for i in range(segments))]
    return mesh(name,verts,faces,mat,group,lod=lod)

def interpolate(stations,steps=10):
    result=[]
    for j in range(len(stations)-1):
        p0=stations[max(0,j-1)]; p1=stations[j]; p2=stations[j+1]; p3=stations[min(j+2,len(stations)-1)]
        for k in range(steps):
            t=k/steps
            result.append(tuple(.5*((2*p1[c])+(-p0[c]+p2[c])*t+(2*p0[c]-5*p1[c]+4*p2[c]-p3[c])*t*t+(-p0[c]+3*p1[c]-3*p2[c]+p3[c])*t*t*t) for c in range(len(p1))))
    return result+[stations[-1]]

# Main fuselage loft, with canopy opening and shoulder flattening. Nose droop,
# pinched forebody and twin-engine shoulder width are controlled independently.
stations=[(0,.006,.006,1.92),(.35,.12,.12,1.94),(1,.31,.30,1.99),
          (1.8,.49,.43,2.02),(2.65,.64,.54,2.06),(3.6,.68,.59,2.07),
          (4.7,.73,.57,2.03),(5.7,.81,.58,2.00),(6.8,1.0,.66,1.97),
          (8.2,1.19,.71,1.93),(10,1.30,.70,1.91),(11.8,1.26,.66,1.88),
          (13.5,1.17,.58,1.86),(14.7,1.05,.49,1.83),(15.15,.91,.43,1.82)]
sections=interpolate(stations,9); N=96; verts=[]; uv=[]; faces=[]
for j,(x,w,h,z) in enumerate(sections):
    for i in range(N):
        a=2*math.pi*i/N
        y=w*math.cos(a); zz=z+h*math.sin(a)
        if x>7: zz-=.075*abs(math.cos(a))**2
        verts.append((x,y,zz)); uv.append((x/16,i/N))
for j in range(len(sections)-1):
    for i in range(N):
        a=j*N+i; b=j*N+(i+1)%N
        # Leave a real cockpit aperture rather than put a black seat on a shell.
        theta=2*math.pi*(i+.5)/N
        if 2.85<sections[j][0]<5.55 and math.sin(theta)>.64: continue
        faces.append((a,b,b+N,a+N))
faces += [tuple(range(N-1,-1,-1))]
body=mesh('Airframe | continuous fuselage',verts,faces,paint,uv=uv)
# Radome is the same continuous contour, separate material, no seam overlap.
for p in body.data.polygons:
    if p.center.x<2.65: p.material_index=1
body.data.materials.append(radome)

def wing_surface(name,sections,group=assembly,thickness=.04,mat=paint,ns=24,nc=36):
    rows=interpolate(sections,max(1,ns//(len(sections)-1)))
    verts=[]; faces=[]; uv=[]
    # sections: y, leading x, trailing x, z; cosine chord distribution.
    for sign in [1,-1]:
        for y,le,te,z in rows:
            for k in range(nc+1):
                t=(1-math.cos(math.pi*k/nc))/2
                h=5*thickness*(te-le)*(.2969*math.sqrt(t)-.126*t-.3516*t*t+.2843*t**3-.1036*t**4)
                x=le+(te-le)*t
                verts.append((x,y,z+sign*h)); uv.append((x/16,(y+5.5)/11))
    W=nc+1; H=len(rows); off=W*H
    for s in range(2):
        for j in range(H-1):
            for k in range(nc):
                a=s*off+j*W+k; faces.append((a,a+1,a+W+1,a+W))
    for j in range(H-1):
        for k in [0,nc]:
            a=j*W+k; faces.append((a,a+W,a+W+off,a+off))
    for j in [0,H-1]:
        for k in range(nc):
            a=j*W+k; faces.append((a,a+1,a+1+off,a+off))
    return mesh(name,verts,faces,mat,group,uv=uv)

for side,tag in [(-1,'L'),(1,'R')]:
    wing_surface(tag+' | delta fixed wing',[(side*.82,6.20,12.35,1.94),(side*2.5,8.55,12.55,1.95),(side*5.24,12.08,12.83,1.99)])
    # Four independent trailing-edge surfaces, no conventional horizontal tail.
    for part,y0,y1,hinge0,hinge1,te0,te1 in [('inner',.87,2.65,12.38,12.60,13.39,13.26),('outer',2.69,5.15,12.61,12.84,13.26,13.08)]:
        channel='elevon_'+tag
        axis=Vector((hinge1-hinge0,side*(y1-y0),0)).normalized()
        pv=pivot('ofs_elevon_'+part+'_'+tag,(hinge0,side*y0,1.96),channel,axis=(axis.x,0,-axis.y),gain=side)
        wing_surface(tag+' | '+part+' elevon',[(side*y0,hinge0,te0,1.96),(side*y1,hinge1,te1,1.98)],pv,ns=12,nc=16)
    pv=pivot('ofs_canard_'+tag,(4.96,side*.69,2.21),'canard',gain=-1)
    wing_surface(tag+' | all moving canard',[(side*.65,4.28,6.04,2.21),(side*2.75,5.55,6.17,2.23)],pv,thickness=.045,ns=24,nc=28)
    ellipsoid(tag+' | foreplane root bearing',(4.97,side*.70,2.21),(.25,.10,.13),metal)
    ellipsoid(tag+' | aerodynamic wingtip fairing',(12.53,side*5.30,2.00),(1.03,.175,.145),paint,segments=64,rings=24,lod=3)
    ellipsoid(tag+' | navigation lens',(12.65,side*5.445,2.01),(.12,.03,.04),red if side<0 else green,segments=24,rings=12,lod=1)

# Canopy shell, proper arch with steep windshield and long rear taper.
canopy_rows=interpolate([(2.64,.055,2.48,.06),(2.94,.27,2.48,.36),(3.45,.44,2.47,.74),
                         (4.12,.47,2.46,.84),(4.8,.45,2.45,.73),(5.40,.29,2.44,.40),(5.75,.03,2.42,.03)],12)
verts=[];faces=[];uv=[];N=48
for x,w,z,h in canopy_rows:
    for i in range(N+1):
        a=math.pi*i/N
        verts.append((x,w*math.cos(a),z+h*math.sin(a)));uv.append(((x-2.64)/3.11,i/N))
for j in range(len(canopy_rows)-1):
    for i in range(N):
        a=j*(N+1)+i;faces.append((a,a+1,a+N+2,a+N+1))
canopy_group=pivot('ofs_canopy',(5.68,0,2.48),'canopy',gain=0)
canopy=mesh('Canopy | smoked transparent shell',verts,faces,glass,canopy_group,uv=uv)
for side in [-1,1]:
    tube('Canopy | sill seal',[(x,side*w,z+.012) for x,w,z,h in canopy_rows],.022,frame,canopy_group,12,2)
row=min(canopy_rows,key=lambda r:abs(r[0]-3.36))
x,w,z,h=row
tube('Canopy | windshield arch',[(x,w*math.cos(math.pi*i/48),z+h*math.sin(math.pi*i/48)) for i in range(49)],.028,paint,canopy_group,12,2)

# Fin swept profile with real thickness and separately hinged rudder.
def slab(name,outline,thickness=.07,mat=paint,group=assembly,lod=3):
    n=len(outline);verts=[(x,y-thickness,z) for x,y,z in outline]+[(x,y+thickness,z) for x,y,z in outline]
    faces=[tuple(range(n)),tuple(range(2*n-1,n-1,-1))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,verts,faces,mat,group,smooth=False,lod=lod)
slab('Tail | fixed swept fin',[(10.10,0,2.43),(12.05,0,5.28),(12.82,0,5.28),(13.30,0,2.47)])
rudder=pivot('ofs_rudder',(13.30,0,2.48),'rudder',axis=(-.17,.985,0),gain=1)
slab('Tail | rudder',[(13.33,0,2.48),(12.85,0,5.28),(13.38,0,5.28),(14.34,0,2.44)],.06,group=rudder)
ellipsoid('Dorsal | spine',(8.9,0,2.47),(3.37,.34,.25),paint,segments=64,rings=32,lod=3)
ellipsoid('Tail | brake chute spine',(14.30,0,2.24),(1.18,.21,.22),paint,lod=3)

# Twin lower rectangular intake ducts, rolled edge and dark curved interior.
for side,tag in [(-1,'L'),(1,'R')]:
    verts=[];faces=[]
    rings=[(4.56,.055,1.06,.95,1.63),(4.62,.045,1.08,.925,1.65),(4.74,.10,.995,1.00,1.57),
           (5.50,.13,.92,1.08,1.60),(6.6,.25,.89,1.25,1.78),(8.2,.40,.91,1.4,1.91)]
    # Polygonal mouth: these hard intake corners are a recognizable Typhoon feature.
    for x,inner,outer,bottom,top in rings:
        verts += [(x,side*inner,top),(x,side*outer,top),(x+.08,side*outer,bottom),(x+.08,side*inner,bottom)]
    for j in range(len(rings)-1):
        for i in range(4):
            a=j*4+i;b=j*4+(i+1)%4;faces.append((a,b,b+4,a+4))
    intake=mesh(tag+' | rolled intake and deep duct',verts,faces,paint,smooth=False)
    intake.data.materials.append(dark)
    for p in intake.data.polygons:
        if p.index>=8:p.material_index=1
    slab(tag+' | splitter plate',[(4.38,side*.052,1.73),(6.4,side*.052,1.75),(6.4,side*.052,.98),(4.70,side*.052,.96)],.024,paint,lod=3)
    # Dark terminal faces well behind the opening rather than a black exterior patch.
    mesh(tag+' | hidden engine face',[(8.15,side*.40,1.4),(8.15,side*.91,1.4),(8.15,side*.91,1.91),(8.15,side*.40,1.91)],[(0,1,2,3)],dark,smooth=False)

# Engine barrels and 16 genuinely separate tapered overlapping petal strips per nozzle.
for side,tag in [(-1,'L'),(1,'R')]:
    y=side*.63;z=1.82
    verts=[];faces=[];N=96
    rings=[(13.70,.56),(14.72,.55),(15.02,.54),(15.96,.435),(15.96,.395),(14.45,.46)]
    for x,r in rings:
        for i in range(N):
            a=2*math.pi*i/N;verts.append((x,y+r*math.cos(a),z+r*math.sin(a)))
    for j in range(len(rings)-1):
        for i in range(N):
            a=j*N+i;b=j*N+(i+1)%N;faces.append((a,b,b+N,a+N))
    shell=mesh(tag+' | engine nozzle shroud and interior',verts,faces,heat)
    shell.data.materials.append(dark)
    for p in shell.data.polygons:
        if p.index>=4*N:p.material_index=1
    for i in range(16):
        verts=[];faces=[]
        for k in range(9):
            t=k/8;x=15.02+.94*t;r=.548-.105*t
            for j in range(7):
                a=2*math.pi*(i+(j/6)*.96)/16
                verts.append((x,y+r*math.cos(a),z+r*math.sin(a)))
        for k in range(8):
            for j in range(6):
                a=k*7+j;faces.append((a,a+1,a+8,a+7))
        mesh(tag+' | nozzle petal %02d'%i,verts,faces,heat,lod=1)
    ellipsoid(tag+' | deep exhaust darkness',(14.47,y,z),(.014,.455,.455),dark,lod=3)

bpy.context.view_layer.update()
bpy.app.driver_namespace['typhoon']=globals()
print('TYPHOON scaled airframe stage:',len(aircraft.objects),'objects; original scene preserved')
