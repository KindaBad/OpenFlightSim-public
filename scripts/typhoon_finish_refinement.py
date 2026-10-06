"""Finish the second silhouette pass: intake, gear stance and conforming livery."""
from mathutils.bvhtree import BVHTree

for side,tag in [(-1,'L'),(1,'R')]:
    for o in list(aircraft.objects):
        if o.name.startswith((tag+' | rolled intake',tag+' | hidden engine face',tag+' | splitter plate',tag+' intake |',tag+' wing | Austrian roundel',tag+' | NO STEP')):
            bpy.data.objects.remove(o,do_unlink=True)
    rings=[(4.53,.06,1.07,1.48,2.21),(4.59,.045,1.09,1.455,2.235),
      (4.73,.08,1.03,1.51,2.20),(5.5,.14,.97,1.61,2.33),
      (6.6,.27,.91,1.71,2.48),(8.4,.41,.90,1.72,2.43)]
    verts=[];faces=[]
    for x,inner,outer,bottom,top in rings:
        verts.extend([(x,side*inner,top),(x,side*outer,top),(x+.09,side*outer,bottom),(x+.09,side*inner,bottom)])
    for j in range(len(rings)-1):
        for i in range(4):
            a=j*4+i;b=j*4+(i+1)%4;faces.append((a,b,b+4,a+4))
    intake=mesh(tag+' | rolled intake and deep duct',verts,faces,paint,smooth=False)
    solid=intake.modifiers.new('Intake wall','SOLIDIFY');solid.thickness=.025
    bevel=intake.modifiers.new('Rolled intake edge','BEVEL');bevel.width=.026;bevel.segments=4
    # The inside is dark; the exterior follows the airframe's gray paint.
    verts=[];faces=[]
    for x,inner,outer,bottom,top in [(4.67,.075,1.045,1.51,2.20),(5.0,.14,.98,1.61,2.19),(6.55,.38,.83,1.78,2.30)]:
        verts.extend([(x,side*inner,top),(x,side*outer,top),(x+.07,side*outer,bottom),(x+.07,side*inner,bottom)])
    for j in range(2):
        for i in range(4):
            a=j*4+i;b=j*4+(i+1)%4;faces.append((a,b,b+4,a+4))
    faces.append((8,9,10,11))
    mesh(tag+' intake | internal duct',verts,faces,dark,smooth=False)
    slab(tag+' | splitter plate',[(4.47,side*.047,2.26),(6.40,side*.047,2.43),(6.4,side*.047,1.71),(4.63,side*.047,1.48)],.025,paint)
    decal(tag+' intake | danger stencil','intake',[(4.88,side*1.028,1.80),(5.64,side*.97,1.80),(5.64,side*.97,1.91),(4.88,side*1.028,1.91)],lod=1)
    inner=scene.objects[tag+' | inner elevon']
    for v in inner.data.vertices:
        p=inner.matrix_world@v.co;p.x-=.38*max(0,min(1,(abs(p.y)-.82)/4.42));v.co=inner.matrix_world.inverted()@p
    # BVH projection makes every vertex of the decal follow the actual wing.
    wing=scene.objects[tag+' | delta fixed wing'];tree=BVHTree.FromObject(wing,bpy.context.evaluated_depsgraph_get())
    for top in [True,False]:
        verts=[];uv=[];faces=[];K=24
        x0,y0,x1,y1=rects['roundel']
        for j in range(K+1):
            for i in range(K+1):
                x=11.65+(i/K-.5)*.72;y=side*3.62+(j/K-.5)*.72
                origin=Vector((x,y,4 if top else .5));direction=Vector((0,0,-1 if top else 1))
                hit,n,index,distance=tree.ray_cast(origin,direction)
                z=(hit.z if hit else 1.96)+(.006 if top else -.006)
                verts.append((x,y,z));uv.append(((x0+(x1-x0)*i/K)/2048,1-(y1-(y1-y0)*j/K)/2048))
        for j in range(K):
            for i in range(K):
                a=j*(K+1)+i;faces.append((a,a+1,a+K+2,a+K+1))
        mesh(tag+' wing | Austrian roundel '+str(top),verts,faces,markings,uv=uv,lod=2)
    # Corrected articulated foreplane stencil sits on the foreplane itself.
    decal(tag+' | canard NO STEP','no_step',[(3.95,side*1.4,2.711),(4.25,side*1.4,2.711),(4.25,side*1.46,2.711),(3.95,side*1.46,2.711)],scene.objects['ofs_canard_'+tag])

def shift_root(obj,delta):
    obj.location+=Vector(delta)
for tag in ['L','R']:
    shift_root(scene.objects['ofs_main_gear_'+tag],(.85,0,0))
    shift_root(scene.objects['ofs_main_door_'+tag],(.85,0,0))
shift_root(scene.objects['ofs_nose_gear'],(1.68,0,0))
for tag in ['-1','1']:shift_root(scene.objects['ofs_nose_door_'+tag],(1.68,0,0))
for o in aircraft.objects:
    if o.type!='MESH':continue
    if 'Nose bay | recessed' in o.name:
        for v in o.data.vertices:v.co.x+=1.68
    if 'bay | rib' in o.name or 'bay | recessed dark' in o.name:
        for v in o.data.vertices:v.co.x+=.85
    if 'air data probe' in o.name:
        for v in o.data.vertices:v.co.z+=.63
    if 'cheek vent slat' in o.name:
        for v in o.data.vertices:v.co.z+=.62
    if o.name.startswith('Dorsal | VHF') or o.name.startswith('Dorsal | anticollision'):
        for v in o.data.vertices:v.co.z+=.27
    if o.name.startswith('Gun |'):
        for v in o.data.vertices:v.co.z+=.54
    if o.name.startswith('Tail | spine outlet'):
        for v in o.data.vertices:v.co.x+=1.03;v.co.z+=.10

# Bay apertures are cut after the final fuselage shape, not into a discarded mesh.
bm=bmesh.new();bm.from_mesh(body.data);remove=[]
for f in bm.faces:
    c=f.calc_center_median()
    if (4.96<c.x<5.72 and abs(c.y)<.24 and c.z<2.12) or (9.73<c.x<11.03 and abs(c.y)>.61 and c.z<1.95):remove.append(f)
bmesh.ops.delete(bm,geom=remove,context='FACES');bm.to_mesh(body.data);bm.free()
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/Typhoon_7L-WA.blend'))
print('Intake, gear stance, probe height and curved decals corrected')
