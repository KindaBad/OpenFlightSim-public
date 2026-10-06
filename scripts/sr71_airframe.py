"""Repository-owned SR-71A 61-7972. Execute ONLY via live Blender MCP.

Authoring metres: X aft from nose; Y starboard; Z up from tire plane.
Public envelope is verified; sampled surface stations are reconstructed estimates.
Creates an isolated scene and retains the user's existing scenes.
"""
import bpy, bmesh, math, json
from pathlib import Path
from mathutils import Vector

import os
# Blender MCP executes without __file__; set OFS_ROOT or start Blender in the checkout.
ROOT = Path(os.environ.get('OFS_ROOT') or (Path(__file__).resolve().parents[1] if '__file__' in globals() else Path.cwd()))
SCENE_NAME = 'OFS SR-71A 61-7972'
if SCENE_NAME in bpy.data.scenes:
    raise RuntimeError('SR-71 scene exists: use a correction script, do not overwrite')
scene = bpy.data.scenes.new(SCENE_NAME)
bpy.context.window.scene = scene
scene.unit_settings.scale_length = 1.0
aircraft = bpy.data.collections.new('SR71 | aircraft and articulation')
studio = bpy.data.collections.new('SR71 | validation studio')
scene.collection.children.link(aircraft); scene.collection.children.link(studio)
assembly = bpy.data.objects.new('SR71 | 61-7972', None)
aircraft.objects.link(assembly)
assembly['reference_length_m']=32.7406
assembly['reference_span_m']=16.9418
assembly['reference_height_m']=5.6388
assembly['evidence']='NASA TM-104330 fig.3; Smithsonian 61-7972; contours reconstructed'


def material(name, color, rough=.6, metallic=0, alpha=1, emission=None):
    m=bpy.data.materials.new('SR71 | '+name); m.use_nodes=True
    n=next(n for n in m.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    for key,val in [('Base Color',(*color,alpha)),('Roughness',rough),('Metallic',metallic),('Alpha',alpha)]:
        n.inputs[key].default_value=val
    if emission:
        n.inputs['Emission Color'].default_value=(*emission,1)
        n.inputs['Emission Strength'].default_value=1
    m.diffuse_color=(*color,alpha)
    return m

paint=material('black titanium coating',(.024,.028,.033),.57,.28)
wingpaint=material('corrugated wing coating',(.023,.027,.031),.59,.25)
finpaint=material('tail coating',(.026,.030,.034),.61,.22)
radome=material('dielectric nose and chine',(.019,.023,.028),.68,.08)
heat=material('heat oxidized ejector titanium',(.11,.087,.073),.54,.75)
metal=material('satin structural metal',(.32,.35,.37),.34,.8)
chrome=material('oleo polished steel',(.50,.55,.58),.2,.93)
dark=material('inlet interior',(.007,.009,.012),.8,.15)
rubber=material('aluminium impregnated tires',(.085,.090,.09),.82,.18)
frame=material('canopy frames',(.02,.024,.029),.46,.35)
glass=material('silica canopy glass',(.10,.17,.18),.14,.03,.18)
interior=material('cockpit charcoal',(.034,.042,.045),.79,.05)
seatmat=material('seat olive cushions',(.068,.082,.064),.91,0)
redpaint=material('red identification paint',(.43,.025,.018),.68,0)
whitepaint=material('off white markings',(.72,.74,.70),.66,0)
navred=material('navigation red',(.2,.005,.002),.3,0,1,(.7,.008,.002))
navgreen=material('navigation green',(.002,.12,.027),.3,0,1,(.003,.45,.035))
lamp=material('lamp lens',(.48,.53,.55),.25,.1,1,(.65,.72,.78))


def parent(o, group=None):
    bpy.context.view_layer.update(); w=o.matrix_world.copy()
    o.parent=group or assembly; o.matrix_world=w
    return o


def mesh(name, verts, faces, mat=None, group=None, uv=None, smooth=True, lod=3):
    d=bpy.data.meshes.new('SR71 '+name); d.from_pydata(verts,[],faces); d.update()
    bm=bmesh.new(); bm.from_mesh(d); bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces)); bm.to_mesh(d); bm.free()
    d.materials.append(mat or paint)
    layer=d.uv_layers.new(name='UVMap')
    for p in d.polygons:
        p.use_smooth=smooth
        for li in p.loop_indices:
            vi=d.loops[li].vertex_index; v=d.vertices[vi].co
            layer.data[li].uv=uv[vi] if uv is not None else (v.x/32.7406,(v.y+8.4709)/16.9418)
    o=bpy.data.objects.new(name,d); aircraft.objects.link(o); parent(o,group)
    o['ofs_last_lod']=lod
    return o


def pivot(name, loc, channel='', axis=(0,0,1), gain=1, slide=(0,0,0), group=None):
    o=bpy.data.objects.new(name,None); aircraft.objects.link(o); o.location=loc
    # Extras are read verbatim by the runtime. Convert Blender Z-up vectors to
    # glTF Y-up here, just as the exporter converts the actual node matrices.
    o['ofs_channel']=channel; o['ofs_axis']=(axis[0],axis[2],-axis[1])
    o['ofs_gain']=gain; o['ofs_slide']=(slide[0],slide[2],-slide[1])
    return parent(o,group)


def sample(stations, steps=8):
    # Cubic Hermite smooth sections; endpoint slopes limited by secants.
    rows=[]
    for j in range(len(stations)-1):
        a=stations[j]; b=stations[j+1]; before=stations[max(0,j-1)]; after=stations[min(len(stations)-1,j+2)]
        for k in range(steps):
            t=k/steps; h=b[0]-a[0]; vals=[a[0]+t*h]
            for c in range(1,len(a)):
                sec=(b[c]-a[c])/h
                m0=(b[c]-before[c])/(b[0]-before[0]); m1=(after[c]-a[c])/(after[0]-a[0])
                m0=math.copysign(min(abs(m0),3*abs(sec)),sec) if m0*sec>0 else 0
                m1=math.copysign(min(abs(m1),3*abs(sec)),sec) if m1*sec>0 else 0
                vals.append((2*t**3-3*t*t+1)*a[c]+(t**3-2*t*t+t)*h*m0+(-2*t**3+3*t*t)*b[c]+(t**3-t*t)*h*m1)
            rows.append(tuple(vals))
    return rows+[stations[-1]]


def tube(name, points, radius, mat=None, group=None, sides=12, lod=1):
    vs=[]; fs=[]
    for j,p in enumerate(points):
        tangent=(Vector(points[min(j+1,len(points)-1)])-Vector(points[max(0,j-1)])).normalized()
        q=Vector((0,0,1)).rotation_difference(tangent)
        for i in range(sides):
            a=i*2*math.pi/sides; vs.append(tuple(Vector(p)+q@Vector((radius*math.cos(a),radius*math.sin(a),0))))
    for j in range(len(points)-1):
        for i in range(sides):
            a=j*sides+i; b=j*sides+(i+1)%sides; fs.append((a,b,b+sides,a+sides))
    fs.extend([tuple(range(sides-1,-1,-1)),tuple((len(points)-1)*sides+i for i in range(sides))])
    return mesh(name,vs,fs,mat or metal,group,lod=lod)


def box(name, loc, size, mat=None, group=None, lod=1, bevel=0):
    vs=[(loc[0]+i*size[0]/2,loc[1]+j*size[1]/2,loc[2]+k*size[2]/2) for i,j,k in [(-1,-1,-1),(-1,-1,1),(-1,1,-1),(-1,1,1),(1,-1,-1),(1,-1,1),(1,1,-1),(1,1,1)]]
    o=mesh(name,vs,[(0,4,6,2),(1,3,7,5),(0,1,5,4),(2,6,7,3),(0,2,3,1),(4,5,7,6)],mat,group,smooth=False,lod=lod)
    if bevel:
        m=o.modifiers.new('Machined corner radius','BEVEL');m.width=bevel;m.segments=3
    return o


def lathe(name, sections, center=(0,0), mat=None, group=None, sectors=128, steps=6, caps=False, lod=3):
    # sections are x/radius; axis is longitudinal. UVs are longitudinal/circumferential.
    rows=sample(sections,steps) if steps else sections; vs=[]; fs=[]; uv=[]
    for j,(x,r) in enumerate(rows):
        for i in range(sectors+1):
            a=i*2*math.pi/sectors
            vs.append((x,center[0]+r*math.cos(a),center[1]+r*math.sin(a)))
            uv.append(((x-sections[0][0])/(sections[-1][0]-sections[0][0]),i/sectors))
    n=sectors+1
    for j in range(len(rows)-1):
        for i in range(sectors):
            a=j*n+i;fs.append((a,a+1,a+n+1,a+n))
    if caps:fs.extend([tuple(range(n-1,-1,-1)),tuple((len(rows)-1)*n+i for i in range(n))])
    return mesh(name,vs,fs,mat,group,uv,lod=lod)

# Continuous upper/lower forebody surface: rounded center blends smoothly into
# knife-edge chines. Chine width, core width and upper/lower depth are independent.
# x, chine half-width, center half-width, chine height, upper height, lower depth.
body_stations=[
 (0,.004,.004,2.28,.004,.004),(.55,.29,.12,2.29,.06,.04),(1.6,.73,.29,2.31,.17,.12),
 (3.1,1.29,.49,2.34,.32,.21),(4.7,1.70,.67,2.38,.48,.32),(6.2,1.91,.79,2.42,.64,.43),
 (8,2.03,.87,2.45,.72,.53),(10,2.08,.91,2.47,.69,.61),(12.5,2.08,.97,2.48,.68,.68),
 (15,2.00,1.02,2.49,.72,.76),(18,1.92,1.06,2.49,.76,.79),(21,1.83,1.09,2.49,.74,.76),
 (24,1.70,1.02,2.48,.66,.68),(27,1.48,.91,2.47,.49,.52),(29.5,1.16,.72,2.45,.30,.31),
 (31.5,.68,.49,2.43,.16,.17),(32.7406,.015,.013,2.42,.015,.015)]
body_rows=sample(body_stations,10)
verts=[]; uv=[]; faces=[]; N=128
for x,w,core,z,up,down in body_rows:
    for i in range(N+1):
        a=2*math.pi*i/N; y=w*math.cos(a); f=abs(y)/max(core,.001)
        # Gently convex chine shelf, rounded core, continuous at shelf/core junction.
        rim=math.exp(-1.8*(w/max(core,.001))**2)
        centre=max(0,(math.exp(-1.8*f*f)-rim)/(1-rim)); edge=max(0,1-(abs(y)/w)**2)
        height=(.78*centre+.22*edge)*(up if math.sin(a)>=0 else down)
        zz=z+math.copysign(height,math.sin(a))
        verts.append((x,y,zz));uv.append((x/32.7406,i/N))
for j in range(len(body_rows)-1):
    for i in range(N):
        x=(body_rows[j][0]+body_rows[j+1][0])/2; a=2*math.pi*(i+.5)/N
        # Two real apertures under the front/rear canopy; the separating deck remains.
        opening=((4.95<x<6.72) or (6.95<x<8.05)) and math.sin(a)>0 and abs(verts[j*(N+1)+i][1])<.36
        # Underbody openings for nose and main gear, later enclosed by wells.
        bottom=math.sin(a)<-.94
        bay=bottom and (8.9<x<10.8)
        if not opening and not bay:
            k=j*(N+1)+i;faces.append((k,k+1,k+N+2,k+N+1))
body=mesh('Airframe | continuous curved chine fuselage',verts,faces,paint,uv=uv)
body.data.materials.append(radome)
for p in body.data.polygons:
    if p.center.x<4.3:p.material_index=1


def wing_surface(name, sections, mat=wingpaint, group=None, thickness=.031, nc=48, steps=10, lod=3):
    # sections: spanwise positive distance, leading x, trailing x, z.
    rows=sample(sections,steps); vs=[];fs=[];uv=[]
    for side in (1,-1):
        for y,le,te,z in rows:
            for k in range(nc+1):
                u=(1-math.cos(math.pi*k/nc))/2
                h=5*thickness*(te-le)*(.2969*math.sqrt(u)-.126*u-.3516*u*u+.2843*u**3-.1036*u**4)
                vs.append((le+(te-le)*u,y,z+side*h));uv.append(((le+(te-le)*u)/32.7406,(y+8.4709)/16.9418))
    W=nc+1;H=len(rows);off=W*H
    for s in range(2):
        for j in range(H-1):
            for k in range(nc):
                a=s*off+j*W+k;fs.append((a,a+1,a+W+1,a+W))
    for j in range(H-1):
        for k in (0,nc):
            a=j*W+k;fs.append((a,a+W,a+W+off,a+off))
    for j in (0,H-1):
        for k in range(nc):
            a=j*W+k;fs.append((a,a+1,a+1+off,a+off))
    return mesh(name,vs,fs,mat,group,uv,lod=lod)

for s,tag in [(-1,'L'),(1,'R')]:
    # Signed station progression must stay increasing for smooth interpolation.
    sections=[(.9,15.50,30.72,2.46),(1.9,15.80,30.70,2.46),(2.4,16.05,30.68,2.45),
              (3.2,17.50,30.55,2.43),(4.14,19.05,30.40,2.43),(5.3,21.10,30.17,2.43),
              (5.9,23.30,30.10,2.45),(7.8,26.65,30.00,2.46),
              (8.28,27.78,29.61,2.46),(8.4709,28.60,29.12,2.46)]
    signed=[(s*y,le,te,z) for y,le,te,z in sections]
    if s<0:signed.reverse()
    wing_surface(tag+' | delta fixed wing',signed)
    for label,y0,y1,x0,x1,t0,t1 in [('inboard',1.18,3.08,30.74,30.59,31.72,31.48),('outboard',5.20,8.10,30.22,29.78,31.14,30.52)]:
        axis=Vector((x1-x0,s*(y1-y0),0)).normalized()
        group=pivot('Elevon_'+tag+'_'+label,(x0,s*y0,2.45),'elevon_'+tag,tuple(axis),s)
        sec=[(s*y0,x0,t0,2.45),(s*y1,x1,t1,2.45)]
        if s<0:sec.reverse()
        wing_surface(tag+' | '+label+' elevon',sec,group=group,thickness=.037,nc=32,steps=16)
    cy=s*4.14;cz=2.70
    # Curved area distribution, circular capture lip, aft compression and ejector.
    lathe(tag+' | nacelle outer loft',[(18.15,.91),(18.20,.955),(18.48,1.025),(19.5,1.095),(21.0,1.12),(23,1.10),(25.3,1.04),(27.7,.98),(28.48,.965)],(cy,cz),sectors=128,steps=10)
    # Inlet duct extends several metres; no close-up flat closure.
    lathe(tag+' | intake lined tunnel',[(18.16,.904),(18.28,.900),(18.8,.875),(19.6,.81),(20.6,.74),(22.0,.69)],(cy,cz),dark,sectors=128,steps=8)
    spike=pivot('Inlet_spike_'+tag,(18.15,cy,cz),'inlet_'+tag,slide=(.6604,0,0))
    lathe(tag+' | translating inlet cone',[(16.16,.005),(16.42,.09),(17.12,.33),(17.82,.58),(18.32,.76),(18.8,.79),(19.5,.59),(20.1,.40)],(cy,cz),radome,spike,sectors=128,steps=10,caps=True)
    # Whole fin pivots about its inward-canted spar, not a trailing rudder strip.
    base=(27.05,cy,3.45); finaxis=(0,-s*math.sin(math.radians(15)),math.cos(math.radians(15)))
    tail=pivot('Rudder_'+tag,base,'rudder',finaxis,1)
    outline=[(25.12,3.57),(27.25,5.6388),(29.88,5.6388),(30.35,3.69),(29.82,3.40),(26.3,3.38)]
    vs=[];fs=[];uv=[]
    for side in (-1,1):
        for x,z in outline:
            y=cy-s*(z-3.45)*math.tan(math.radians(15))+side*.048
            vs.append((x,y,z));uv.append(((x-24)/8,(z-3.3)/2.5))
    fs=[tuple(range(5,-1,-1)),tuple(range(6,12))]+[(i,(i+1)%6,(i+1)%6+6,i+6) for i in range(6)]
    fin=mesh(tag+' | all-moving canted vertical tail',vs,fs,finpaint,tail,uv,smooth=False)
    m=fin.modifiers.new('Fin edge bevel','BEVEL');m.width=.022;m.segments=3
    # Ejector outer body and dark internal depth; detailed petals in next stage.
    lathe(tag+' | ejector nozzle outer',[(28.48,.965),(28.75,.96),(29.50,.90),(30.05,.88)],(cy,cz),heat,sectors=128,steps=8)
    lathe(tag+' | deep exhaust liner',[(27.2,.46),(28.0,.58),(28.8,.69),(30.05,.84)],(cy,cz),dark,sectors=96,steps=8)

# The A model has two low separate cockpits, no raised trainer position.
# Longitudinal cross sections: x / half-width / sill z / crown z.
canopy_stations=[(4.53,.06,2.93,3.00),(4.86,.35,2.95,3.35),(5.26,.48,2.96,3.59),
 (5.90,.50,2.98,3.68),(6.52,.46,3.00,3.63),(6.78,.43,3.02,3.58),
 (6.94,.43,3.04,3.58),(7.35,.45,3.04,3.64),(7.79,.41,3.05,3.56),
 (8.16,.30,3.07,3.33),(8.56,.06,3.10,3.14)]
# Silhouette shell is opaque frame skin; window panels cut into it in detail pass.
canopies=[]
for label,xlo,xhi in [('Front',4.53,6.87),('Rear',6.87,8.56)]:
    group=pivot('Canopy_'+label,(xhi,0,3.13))
    rows=[r for r in sample(canopy_stations,8) if xlo<=r[0]<=xhi]
    vs=[];fs=[];uv=[];N=32
    for x,w,z,h in rows:
        for i in range(N+1):
            a=math.pi*i/N;vs.append((x,w*math.cos(a),z+(h-z)*math.sin(a)**.84));uv.append((x/32.7406,i/N))
    for j in range(len(rows)-1):
        for i in range(N):
            a=j*(N+1)+i;fs.append((a,a+1,a+N+2,a+N+1))
    shell=mesh(label+' | canopy silhouette shell',vs,fs,frame,group,uv)
    canopies.append(shell)

print('SR-71 silhouette generated:',len(aircraft.objects),'objects')
bpy.app.driver_namespace['sr71']=globals()
