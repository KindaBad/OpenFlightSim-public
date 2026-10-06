"""Stitch the canopy shoulder ends and project serials onto the actual skin."""
from mathutils.bvhtree import BVHTree
for o in list(aircraft.objects):
    if o.name.startswith('Cockpit | painted shoulder'):bpy.data.objects.remove(o,do_unlink=True)
indices=[i for i,r in enumerate(sections) if 2.10<r[0]<6.12]
rows=sections[max(0,indices[0]-1):indices[-1]+2]
for side in [-1,1]:
    verts=[];faces=[]
    for x,w,h,z in rows:
        cr=min(canopy_rows,key=lambda r:abs(r[0]-x));_,cw,cz,ch=cr
        if x<2.07 or x>6.13:cw=0;cz=z+h
        for j in range(12):
            t=j/11;y=side*((1-t)*w*math.sqrt(1-.72**2)+t*cw)
            low=z+h*.72-(.10*(1-.72**2) if x>6 else 0)
            verts.append((x,y,low+(cz-low)*t))
    for j in range(len(rows)-1):
        for k in range(11):
            a=j*12+k;faces.append((a,a+1,a+13,a+12))
    mesh('Cockpit | painted shoulder '+str(side),verts,faces,paint)
tree=BVHTree.FromObject(body,bpy.context.evaluated_depsgraph_get())
for o in aircraft.objects:
    if o.type!='MESH' or 'livery |' not in o.name:continue
    side=-1 if o.name.startswith('L') else 1
    for v in o.data.vertices:
        p=o.matrix_world@v.co
        hit,n,index,distance=tree.ray_cast(Vector((p.x,side*3,p.z)),Vector((0,-side,0)))
        if hit is not None:v.co=o.matrix_world.inverted()@(hit+n*.003)
bpy.context.view_layer.update()
render_view('front_quarter','final');render_view('cockpit','final');render_view('livery','final')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/Typhoon_7L-WA.blend'))
print('Canopy shoulders stitched; glyph meshes conform to actual fuselage')
