"""Restore the saved Su57 authoring namespace; execute through Blender MCP."""
import bpy, bmesh, math, json, ast
from pathlib import Path
from mathutils import Vector, Matrix, Quaternion
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from su57_paths import pipeline_paths
options,ROOT,WORKING_PATH,EXPORT_DIR,REPORT_DIR=pipeline_paths(False)
scene=bpy.data.scenes['M3.68 | Su57 metric working copy'];bpy.context.window.scene=scene
aircraft=bpy.data.collections['Su57 | aircraft'];studio=bpy.data.collections['Su57 | validation studio']
assembly=scene.objects['Su57 | assembly']
source=ast.parse((ROOT/'scripts/su57_normalize.py').read_text())
for statement in source.body:
 if isinstance(statement,ast.FunctionDef):exec(compile(ast.Module(body=[statement],type_ignores=[]),'Su57 helpers','exec'))
def parent(obj,group):
 world=obj.matrix_world.copy();obj.parent=group or assembly;obj.matrix_world=world;return obj
def pivot(name,loc,channel='',axis=(0,1,0),gain=1,slide=(0,0,0),group=None):
 obj=bpy.data.objects.new('Su57 pivot | '+name,None);aircraft.objects.link(obj)
 obj.location=loc;obj['ofs_channel']=channel;obj['ofs_axis']=(axis[0],axis[2],-axis[1])
 obj['ofs_gain']=gain;obj['ofs_slide']=(slide[0],slide[2],-slide[1]);bpy.context.view_layer.update()
 return parent(obj,group)
def material(name,color,metal=0,rough=.5):
 mat=bpy.data.materials.new('Su57 | '+name);mat.use_nodes=True
 shader=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
 shader.inputs['Base Color'].default_value=color;shader.inputs['Metallic'].default_value=metal
 shader.inputs['Alpha'].default_value=color[3]
 shader.inputs['Roughness'].default_value=rough;mat.diffuse_color=color;return mat
def mesh(name,vertices,faces,mat,group=None,smooth=True,lod=3):
 data=bpy.data.meshes.new('Su57 '+name);data.from_pydata(vertices,[],faces);data.update()
 bm=bmesh.new();bm.from_mesh(data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(data);bm.free()
 data.materials.append(mat)
 for p in data.polygons:p.use_smooth=smooth
 obj=bpy.data.objects.new('Su57 detail | '+name,data);aircraft.objects.link(obj)
 obj['ofs_last_lod']=lod;bpy.context.view_layer.update();return parent(obj,group)
def rod(name,a,b,radius,mat,group=None,lod=3,sides=20):
 a=Vector(a);b=Vector(b);axis=(b-a).normalized();u=axis.cross(Vector((0,0,1)))
 if u.length<.01:u=axis.cross(Vector((0,1,0)))
 u.normalize();v=axis.cross(u)
 vertices=[tuple(p+radius*(u*math.cos(i*2*math.pi/sides)+v*math.sin(i*2*math.pi/sides))) for p in (a,b) for i in range(sides)]
 faces=[tuple(range(sides-1,-1,-1)),tuple(range(sides,2*sides))]
 faces.extend((i,(i+1)%sides,(i+1)%sides+sides,i+sides) for i in range(sides))
 return mesh(name,vertices,faces,mat,group,lod=lod)
def box(name,center,size,mat,group=None,lod=3):
 vertices=[tuple(center[i]+sign[i]*size[i]*.5 for i in range(3)) for sign in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
 return mesh(name,vertices,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],mat,group,smooth=False,lod=lod)
def tire(name,center,radius,width,mat,group=None):
 # Profile around a transverse axle, with rounded shoulders and a flat tread.
 profile=[(-width*.50,radius*.60),(-width*.50,radius*.82),(-width*.40,radius*.96),(-width*.25,radius),(width*.25,radius),(width*.40,radius*.96),(width*.50,radius*.82),(width*.50,radius*.60)]
 sides=48;vertices=[]
 for y,r in profile:
  vertices.extend((center[0]+r*math.sin(i*2*math.pi/sides),center[1]+y,center[2]+r*math.cos(i*2*math.pi/sides)) for i in range(sides))
 faces=[(k*sides+i,k*sides+(i+1)%sides,(k+1)*sides+(i+1)%sides,(k+1)*sides+i) for k in range(len(profile)-1) for i in range(sides)]
 return mesh(name,vertices,faces,mat,group)
rest={}
def remember_rest():
 rest.update({o.name:o.matrix_basis.copy() for o in aircraft.objects if o.type=='EMPTY'})
def validation_pose(values):
 for obj in aircraft.objects:
  if obj.name not in rest:continue
  axis=obj.get('ofs_axis',(0,1,0));slide=obj.get('ofs_slide',(0,0,0));value=values.get(obj.get('ofs_channel',''),0)
  axis=Vector((axis[0],-axis[2],axis[1]));slide=Vector((slide[0],-slide[2],slide[1]))
  obj.matrix_basis=rest[obj.name]@Matrix.Translation(slide*value)@Quaternion(axis,value*obj.get('ofs_gain',1)).to_matrix().to_4x4()
 bpy.context.view_layer.update()
remember_rest()
bpy.app.driver_namespace['su57']=globals().copy()
print('Su57 saved authoring session restored',len(aircraft.objects),'nodes')
