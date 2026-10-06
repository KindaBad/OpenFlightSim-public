"""Native geometric close-up detail. Execute only through Blender MCP.
Mechanical layout is an engineering visual estimate, not OEM manufacturing data.
"""
from mathutils.bvhtree import BVHTree
from collections import defaultdict
assert not assembly.get('su57_geometric_detail'), 'Geometric detail already applied'
validation_pose({})
for obj in list(aircraft.objects):
 if obj.get('ofs_geometric_detail'):bpy.data.objects.remove(obj,do_unlink=True)
paint=bpy.data.materials['Su57 | gear painted alloy'];chrome=bpy.data.materials['Su57 | oleo polished steel']
created=[]
def remember(obj):
 created.append(obj);obj['ofs_geometric_detail']=True;return obj
alloy=material('machined gear aluminium',(.38,.43,.47,1),.78,.29)
steel=material('gear brake steel',(.095,.105,.115,1),.86,.40)
hose=material('braided hydraulic hose',(.021,.025,.028,1),.2,.70)
seal=material('strut rubber dust seals',(.022,.026,.029,1),0,.75)
fastener=material('flush fastener heads',(.21,.24,.27,1),.68,.43)
seam=material('panel seam depth',(.033,.040,.046,1),.28,.64)
# Recessed lathed rings, transverse axle. A hollow hub exposes real depth.
def lathe(name,x,y,z,profile,mat,group,lod=1,sides=48):
 vertices=[(x+r*math.sin(i*2*math.pi/sides),y+d,z+r*math.cos(i*2*math.pi/sides)) for d,r in profile for i in range(sides)]
 faces=[(k*sides+i,k*sides+(i+1)%sides,(k+1)*sides+(i+1)%sides,(k+1)*sides+i) for k in range(len(profile)-1) for i in range(sides)]
 return remember(mesh(name,vertices,faces,mat,group,lod=lod))
def line(name,points,radius,mat,group,lod=0,sides=8):
 for i in range(len(points)-1):remember(rod(name+' '+str(i),points[i],points[i+1],radius,mat,group,lod=lod,sides=sides))
def plate(name,points,thickness,mat,group,lod=1):
 verts=[tuple(Vector(p)+Vector((0,dy,0))) for dy in [-thickness/2,thickness/2] for p in points];n=len(points)
 faces=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
 return remember(mesh(name,verts,faces,mat,group,smooth=False,lod=lod))
# Replace the flat solid-disc hub bodies, retaining tyres and actual spin pivots.
for name in ['Su57 detail | L wheel hub -2.25','Su57 detail | R wheel hub 2.25','Su57 detail | Su57 pivot | N wheel spin | Su57 | gear painted alloy | tier3']:
 obj=scene.objects.get(name)
 if obj:bpy.data.objects.remove(obj,do_unlink=True)
for tag,x,y,r,w,nose in [('N',4.,0,.33,.16,True),('L',13.25,-2.25,.515,.32,False),('R',13.25,2.25,.515,.32,False)]:
 fold=scene.objects['Su57 pivot | '+tag+' gear retract'];compress=scene.objects['Su57 pivot | '+tag+' oleo compression'];steer=scene.objects['Su57 pivot | '+tag+' axle steering'];wheel=scene.objects['Su57 pivot | '+tag+' wheel spin']
 # Seal collars, stepped cylinder glands and circumferential mounting bands.
 for z,rr in [(1.24,.076 if nose else .108),(1.12,.051 if nose else .075),(1.32,.077 if nose else .110),(1.72,.074 if nose else .105)]:
  remember(rod(tag+' strut machined collar',(x+.06,y,z-.020),(x+.06,y,z+.020),rr,alloy,compress if z<1.25 else fold,lod=1,sides=24))
 remember(rod(tag+' oleo wiper seal',(x+.06,y,1.185),(x+.06,y,1.208),.054 if nose else .078,seal,compress,lod=0,sides=24))
 # Real clevis plates and hinge pins instead of cylinder-only scissor links.
 for side in [-1,1]:
  yy=y+side*(.086 if nose else .118)
  for i,(a,b) in enumerate([((x+.06,yy,1.34),(x+.30,yy,1.08)),((x+.30,yy,1.08),(x+.13,yy,.89))]):
   v=Vector(b)-Vector(a);off=Vector((v.z,0,-v.x)).normalized()*.038
   plate(tag+' scissor clevis '+str(side)+str(i),[Vector(a)-off,Vector(a)+off,Vector(b)+off,Vector(b)-off],.023,paint,compress)
  for j,(xx,zz) in enumerate([(x+.06,1.34),(x+.30,1.08),(x+.13,.89)]):
   remember(rod(tag+' scissor pin '+str(side)+str(j),(xx,yy-.026,zz),(xx,yy+.026,zz),.030,alloy,compress,lod=0,sides=12))
   remember(rod(tag+' scissor pin hex '+str(side)+str(j),(xx,yy+.026,zz),(xx,yy+.038,zz),.023,chrome,compress,lod=0,sides=6))
 # Hydraulic actuator piston, barrel, end-eye pins and flex hose routing.
 a=(x+.54,y+.07,1.94);b=(x+.15,y+.07,1.30);mid=Vector(a).lerp(Vector(b),.63)
 remember(rod(tag+' retract hydraulic barrel',a,mid,.052,alloy,fold,lod=1,sides=20))
 remember(rod(tag+' retract hydraulic piston',mid,b,.022,chrome,fold,lod=1,sides=16))
 for i,p in enumerate([a,b]):remember(rod(tag+' actuator eye pin '+str(i),(p[0],p[1]-.07,p[2]),(p[0],p[1]+.07,p[2]),.031,steel,fold,lod=1,sides=12))
 path=[(x-.07,y-.13,1.95),(x-.13,y-.18,1.68),(x-.12,y-.20,1.30),(x-.20,y-.21,1.08),(x-.23,y-.17,.87),(x-.12,y-.15,.72)]
 line(tag+' flexible brake hose',path,.010,hose,compress,lod=0,sides=10)
 for i,p in enumerate(path[::2]):
  remember(rod(tag+' hose ferrule '+str(i),(p[0]-.017,p[1],p[2]),(p[0]+.017,p[1],p[2]),.017,alloy,compress,lod=0,sides=6))
 # Door sheet thickness, stamped ribs and a visible hinge/actuator.
 door=scene.objects['Su57 pivot | '+tag+' gear door hinge'];side=y-(.33 if nose else .44)
 for j in range(3):
  z=1.43+j*.18
  remember(box(tag+' door stamped rib '+str(j),(x+.01,side+.075,z),(.67,.030,.028),alloy,door,lod=1))
 for j in range(2):remember(rod(tag+' door hinge knuckle '+str(j),(x-.43+j*.50,side,1.97),(x-.29+j*.50,side,1.97),.038,steel,door,lod=1,sides=12))
 for axleY in ([y-.20,y+.20] if nose else [y]):
  # Hollow rim lip with a recessed well, hub boss and ventilated spoke mesh.
  for sideSign in [-1,1]:
   outer=axleY+sideSign*w*.51
   profile=[(sideSign*d,rr*r) for d,rr in [(0,.59),(.015,.57),(.015,.49),(-.023,.46),(-.045,.38),(-.045,.30)]]
   lathe(tag+' recessed rim '+str(axleY)+str(sideSign),x,outer,r,profile,alloy,wheel,lod=1)
   remember(rod(tag+' axle cap',(x,outer-sideSign*.047,r),(x,outer+sideSign*.024,r),r*.19,alloy,wheel,lod=1,sides=24))
   for i in range(10 if not nose else 8):
    a=i*2*math.pi/(10 if not nose else 8);da=.075
    verts=[]
    for yy in [outer-sideSign*.032,outer-sideSign*.017]:
     for rr,angle in [(r*.19,a-da),(r*.49,a-da),(r*.49,a+da),(r*.19,a+da)]:verts.append((x+rr*math.sin(angle),yy,r+rr*math.cos(angle)))
    remember(mesh(tag+' ventilated rim spoke '+str(axleY)+str(sideSign)+str(i),verts,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],alloy,wheel,smooth=False,lod=1))
   for i in range(8):
    a=i*math.pi/4;xx=x+r*.26*math.sin(a);zz=r+r*.26*math.cos(a)
    remember(rod(tag+' rim retention nut '+str(axleY)+str(sideSign)+str(i),(xx,outer-sideSign*.006,zz),(xx,outer+sideSign*.006,zz),.010 if nose else .015,chrome,wheel,lod=0,sides=6))
  if not nose:
   # Carbon/steel brake stack and stationary caliper supported by axle/steer.
   inner=axleY+(-1 if y>0 else 1)*w*.60
   for i in range(4):lathe(tag+' brake pack '+str(i),x,inner+i*.012,r,[(-.005,r*.21),(-.005,r*.43),(.005,r*.43),(.005,r*.21)],steel,wheel,lod=0,sides=32)
   remember(box(tag+' brake caliper',(x-.21,inner+.026,r+.15),(.16,.11,.17),paint,steer,lod=1))
   # Torque reaction arm and hydraulic union.
   line(tag+' brake torque arm',[(x-.21,inner,r+.15),(x-.07,y,1.01)],.023,steel,steer,lod=1,sides=12)
 # A stepped axle housing and steering cylinders remain fixed to gear.
 remember(rod(tag+' axle housing',(x,y-(.39 if nose else .24),r),(x,y+(.39 if nose else .24),r),.052 if nose else .073,steel,steer,lod=1,sides=24))
 if nose:
  for sign in [-1,1]:
   remember(rod('N steering damper '+str(sign),(x-.21,sign*.09,.69),(x-.03,sign*.24,.48),.029,alloy,steer,lod=1,sides=12))
# Project seams and 2-mm fasteners onto actual evaluated airframe surfaces.
# No fictitious raised plates: seam strips penetrate the skin with just 0.3 mm
# of exposed edge; access-cover rims are 0.7 mm proud. Geometry follows normals.
bodyObjects=[o for o in aircraft.objects if o.type=='MESH' and o.name.startswith('Su57 | ') and not any(s in o.name for s in ['Canopy','engine','Cylinder','Eject','Cube.02','Cube.03'])]
vertices=[];faces=[];owners=[]
for obj in bodyObjects:
 data=obj.data;start=len(vertices);vertices.extend(tuple(obj.matrix_world@v.co) for v in data.vertices)
 for p in data.polygons:faces.append(tuple(start+i for i in p.vertices));owners.append(obj.parent)
bvh=BVHTree.FromPolygons(vertices,faces)
def sample(x,y):
 hit,n,index,d=bvh.ray_cast(Vector((x,y,8)),Vector((0,0,-1)),8)
 if hit is None or n.z<.35 or hit.z<1.9:return None
 return hit,n,owners[index]
def surface_strip(name,coords,width,mat,lod=0):
 points=[sample(x,y) for x,y in coords]
 if any(p is None for p in points):return
 # Split at moving surface parent boundaries, preventing glued articulation.
 for i in range(len(points)-1):
  a,na,pa=points[i];b,nb,pb=points[i+1]
  if pa!=pb or (b-a).length>.35:continue
  tangent=(b-a).normalized();sa=na.cross(tangent).normalized()*width/2;sb=nb.cross(tangent).normalized()*width/2
  verts=[a-sa+na*.0003,a+sa+na*.0003,b+sb+nb*.0003,b-sb+nb*.0003,a-sa-na*.002,a+sa-na*.002,b+sb-nb*.002,b-sb-nb*.002]
  remember(mesh(name+' '+str(i),verts,[(0,1,2,3),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],mat,pa,smooth=False,lod=lod))
# Transverse fuselage service joints and symmetric removable wing skin panels.
for x in [6.7,8.2,10.1,12.0,14.0,15.55,16.75]:
 surface_strip('fuselage service seam '+str(x),[(x,-1.65+i*.055) for i in range(61)],.004,seam)
for sign in [-1,1]:
 for y in [2.0,3.1,4.2,5.25]:
  surface_strip('wing spar seam '+str(sign)+str(y),[(9.1+i*.07,sign*y) for i in range(82)],.003,seam)
 for cx,cy,wx,wy in [(8.4,1.0,.70,.52),(10.9,1.15,.90,.48),(13.15,1.20,.70,.43),(12.9,3.6,.80,.60),(14.1,4.8,.60,.42)]:
  cy*=sign;coords=[]
  for a,b in [((cx-wx/2,cy-wy/2),(cx+wx/2,cy-wy/2)),((cx+wx/2,cy-wy/2),(cx+wx/2,cy+wy/2)),((cx+wx/2,cy+wy/2),(cx-wx/2,cy+wy/2)),((cx-wx/2,cy+wy/2),(cx-wx/2,cy-wy/2))]:
   coords.extend([(a[0]+(b[0]-a[0])*t/10,a[1]+(b[1]-a[1])*t/10) for t in range(11)])
  surface_strip('flush inspection cover '+str(cx)+str(cy),coords,.003,seam)
  for k,(x,y) in enumerate(coords[::3]):
   p=sample(x,y)
   if not p:continue
   pos,n,group=p
   remember(rod('inspection fastener '+str(cx)+str(cy)+str(k),pos-n*.001,pos+n*.0015,.0045,fastener,group,lod=0,sides=8))
 # Hundreds of correctly small perimeter fasteners, projected onto skin.
 for y in [2.3,3.65,5.0]:
  for i in range(32):
   p=sample(10.2+i*.16,sign*y)
   if not p:continue
   pos,n,group=p;remember(rod('wing flush fastener '+str(sign)+str(y)+str(i),pos-n*.0005,pos+n*.0015,.0035,fastener,group,lod=0,sides=8))
# Group per parent/material/LOD, retaining mechanical pivots and bounded draws.
groups=defaultdict(list)
for obj in created:groups[(obj.parent.name,int(obj.get('ofs_last_lod',3)),obj.data.materials[0].name)].append(obj)
for key,objects in groups.items():
 bpy.ops.object.select_all(action='DESELECT')
 for obj in objects:obj.select_set(True)
 bpy.context.view_layer.objects.active=objects[0]
 if len(objects)>1:bpy.ops.object.join()
 obj=objects[0];obj.name='Su57 geometry | '+key[0]+' | '+key[2]+' | tier'+str(key[1]);obj['ofs_last_lod']=key[1];obj['ofs_geometric_detail']=True
assembly['su57_geometric_detail']=True
assembly['geometric_detail_note']='ESTIMATE: hollow/spoked hubs, brakes, clevis pins, hydraulic actuators/hoses, collars/seals, door hinges/ribs, projected seams and millimetre flush fasteners; not OEM drawings'
assembly['geometric_detail_parts']=len(created);assembly['geometric_detail_batches']=len(groups)
remember_rest();validation_pose({})
bpy.ops.wm.save_as_mainfile(filepath=str(WORKING_PATH))
print('ACTUAL GEOMETRY',len(created),'parts joined into',len(groups),'rig-aware batches')
