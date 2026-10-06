import bpy,json,os,math
from mathutils import Vector
root=bpy.data.collections['A320 • Airbus demonstrator']
bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get();rows=[];bounds=[]
for ob in root.all_objects:
 if ob.type not in {'MESH','CURVE'} or any(c.name.startswith('08 Presentation') for c in ob.users_collection):continue
 e=ob.evaluated_get(dg);me=e.to_mesh()
 if me is None or not me.vertices:continue
 pts=[e.matrix_world@v.co for v in me.vertices]
 b=[min(p[i] for p in pts) for i in range(3)]+[max(p[i] for p in pts) for i in range(3)];bounds.append(b);rows.append((ob.name,b))
 e.to_mesh_clear()
lo=[min(b[i] for b in bounds) for i in range(3)];hi=[max(b[i+3] for b in bounds) for i in range(3)]
print('ACTUAL_GEOMETRY',lo,hi,[hi[i]-lo[i] for i in range(3)])
for i in range(3):print('EXTREME',i, sorted(rows,key=lambda t:t[1][i])[:2],sorted(rows,key=lambda t:-t[1][i+3])[:2])
path=os.path.join(os.path.dirname(bpy.data.filepath),'model_report.json');r=json.load(open(path));r['dimensions_metres']={'length':round(hi[0]-lo[0],4),'span':round(hi[1]-lo[1],4),'height_from_ground':round(hi[2],4)};r['measurement']='Evaluated mesh vertices after dependency graph update; antenna/wick extents included.';json.dump(r,open(path,'w'),indent=2)
assert abs(hi[0]-lo[0]-37.57)<.10
assert abs(hi[1]-lo[1]-35.80)<.10
assert abs(hi[2]-11.76)<.1
print('VALIDATION PASSED')
