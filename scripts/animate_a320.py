"""Split the existing owned A320's skins into articulated parts and export.

blender -b output/Airbus_A320.blend --python scripts/animate_a320.py
Keeps the delivered fuselage, nacelles, glazing, markings and mechanical detail.
"""
import bpy
import math
import sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from rig_aircraft import pivot, parent_keep_world, mesh_object, export_aircraft
from physics_geometry import physical_parameters, hinge_point
physical=physical_parameters("a320")

scene = bpy.context.scene
assembly = bpy.data.objects.get("AIRBUS A320 | assembly")
if not assembly:
    raise RuntimeError("Open the existing owned A320 scene first")

if not assembly.get("ofs_m36_rig"):
    wingmat = bpy.data.objects["L wing | swept airfoil"].data.materials[0]
    blue = bpy.data.objects["Vertical stabilizer | Airbus blue"].data.materials[0]
    stations = [(1.55,12.8,6.08,2.66,.125),(3,13.56,5.25,2.78,.12),
                (5.75,15.04,3.95,3.02,.112),(8.3,16.45,3.28,3.24,.102),
                (11.91,18.43,2.54,3.56,.091),(16.29,20.84,1.64,3.94,.085),
                (16.85,21.31,1.41,4.00,.08)]
    spoilers = [(3,4.4),(4.5,5.7),(5.85,7.3),(7.45,9),(9.15,10.9)]
    flaps = [(2.15,5.5),(5.6,9.25),(9.35,12.65)]

    def params(y):
        for a, b in zip(stations, stations[1:]):
            if y <= b[0]:
                t = (y-a[0])/(b[0]-a[0])
                return [a[i]*(1-t)+b[i]*t for i in range(1,5)]
        return stations[-1][1:]

    def point(y, u, side, upper):
        le,c,z,t = params(y)
        thickness = 5*t*c*(.2969*math.sqrt(max(u,0))-.126*u-.3516*u*u+.2843*u**3-.1036*u**4)
        return (le+c*u, side*y, z+.017*c*math.sin(math.pi*u)+(thickness if upper else -thickness))

    for side, tag in [(-1,"L"),(1,"R")]:
        bpy.data.objects.remove(bpy.data.objects[tag+" wing | swept airfoil"], do_unlink=True)
        # Include every hinge/part boundary so no cell straddles a moving joint.
        ys = sorted(set([1.55+i*(16.85-1.55)/96 for i in range(97)] +
                        [y for panel in spoilers+flaps for y in panel] + [12.8,16.2]))
        us = sorted(set([(1-math.cos(math.pi*j/84))/2 for j in range(85)] + [.46,.68,.72]))
        groups = {"static": ([],[])}
        rigs = {}
        for k,(a,b) in enumerate(flaps):
            key=f"flap_{k}"
            rigs[key]=pivot(f"ofs_flap_{tag}_{k}", point((a+b)/2,.72,side,True), "flap",
                            gain=-35*math.pi/180, slide=(.65,-.08,0), parent=assembly)
        rigs["aileron"]=pivot(f"ofs_aileron_{tag}",hinge_point(physical,"aileron_"+tag),f"aileron_{tag}",parent=assembly)
        for k,(a,b) in enumerate(spoilers):
            rigs[f"spoiler_{k}"]=pivot(f"ofs_spoiler_{tag}_{k}",point((a+b)/2,.46,side,True),"spoiler",
                                      gain=50*math.pi/180,parent=assembly)
        for upper in [True,False]:
            for a,b in zip(ys,ys[1:]):
                middle=(a+b)/2
                for u,v in zip(us,us[1:]):
                    key="static"
                    if u >= .72-1e-8:
                        if 12.8<middle<16.2:
                            key="aileron"
                        for k,(lo,hi) in enumerate(flaps):
                            if lo<middle<hi:key=f"flap_{k}"
                    if upper and u >= .46-1e-8 and v<=.68+1e-8:
                        for k,(lo,hi) in enumerate(spoilers):
                            if lo<middle<hi:key=f"spoiler_{k}"
                    vertices,faces=groups.setdefault(key,([],[]))
                    index=len(vertices)
                    vertices += [point(a,u,side,upper),point(a,v,side,upper),
                                 point(b,v,side,upper),point(b,u,side,upper)]
                    order=(0,1,2,3) if (side==1)==upper else (3,2,1,0)
                    faces.append(tuple(index+i for i in order))
        for key,(vertices,faces) in groups.items():
            obj=mesh_object(f"{tag} wing skin {key}",vertices,faces,wingmat)
            parent_keep_world(obj,rigs.get(key,assembly))
            if key.startswith("spoiler"):
                modifier=obj.modifiers.new("Spoiler panel thickness","SOLIDIFY")
                modifier.thickness=.015

        # Split each horizontal tail at its actual .70-chord hinge.
        original=bpy.data.objects[tag+" horizontal stabilizer"]
        hs=[(1,30.08,5.42,4.6),(2,30.69,4.92,4.77),(4,32.20,3.55,5.13),(6.225,34.02,1.24,5.54)]
        tailmat=original.data.materials[0]
        bpy.data.objects.remove(original,do_unlink=True)
        hinge=pivot(f"ofs_elevator_{tag}",hinge_point(physical,"elevator_"+tag),"elevator",parent=assembly)
        for trailing in [False,True]:
            vertices=[];faces=[]
            for y,x,c,z in hs:
                for upper in [True,False]:
                    for j in range(33):
                        u=(.70+j*.30/32) if trailing else j*.70/32
                        vertices.append((x+c*u,side*y,z+(1 if upper else -1)*.043*c*math.sqrt(max(0,1-(2*u-1)**2))))
            for k in range(len(hs)-1):
                for up in [0,1]:
                    for j in range(32):
                        a=k*66+up*33+j
                        faces.append((a,a+1,a+67,a+66))
            obj=mesh_object(tag+(" elevator" if trailing else " fixed stabilizer"),vertices,faces,tailmat)
            parent_keep_world(obj,hinge if trailing else assembly)

    # Separate rudder geometry using the existing fin's trailing polygons.
    fin=bpy.data.objects["Vertical stabilizer | Airbus blue"]
    import bmesh
    bm=bmesh.new();bm.from_mesh(fin.data)
    # A swept hinge in the X/Z plane, matching the original fin's .77 chord.
    hinge_a=Vector(hinge_point(physical,"rudder"));hinge_b=Vector((35.755,0,11.76))
    axis=(hinge_b-hinge_a).normalized()
    normal=Vector((axis.z,0,-axis.x))
    bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
                          dist=.0001,plane_co=hinge_a,plane_no=normal)
    bm.to_mesh(fin.data);bm.free()
    data=fin.data
    static_faces=[];moving_faces=[]
    for p in data.polygons:
        center=sum((data.vertices[i].co for i in p.vertices),Vector())/len(p.vertices)
        (moving_faces if (center-hinge_a).dot(normal)>.002 else static_faces).append(tuple(p.vertices))
    vertices=[tuple(v.co) for v in data.vertices]
    fixed=mesh_object("Fixed vertical fin",vertices,static_faces,blue)
    moving=mesh_object("Rudder",vertices,moving_faces,blue)
    rudder=pivot("ofs_rudder",hinge_a,"rudder",axis=(axis.x,axis.z,-axis.y),parent=assembly)
    parent_keep_world(fixed,assembly);parent_keep_world(moving,rudder)
    bpy.data.objects.remove(fin,do_unlink=True)
    for obj in list(bpy.data.objects):
        if obj.name.startswith("Rudder static discharger"):
            parent_keep_world(obj,rudder)

    # Mechanical detail stays intact; group assemblies, steering and wheel hubs.
    for side,tag in [(-1,"L"),(1,"R")]:
        gear=pivot(f"ofs_main_gear_{tag}",(17.35,side*3.57,2.88),"gear",axis=(1,0,0),
                   gain=(-side)*1.50,parent=assembly)
        for obj in list(bpy.data.objects):
            if obj.name.startswith(tag+" main"):
                parent_keep_world(obj,gear)
        for name,y in [("inner",side*3.795-side*.465),("outer",side*3.795+side*.465)]:
            wheel=pivot(f"ofs_wheel_{tag}_{name}",(17.71,y,.61),"wheel",parent=gear)
            for obj in list(bpy.data.objects):
                if obj.name.startswith(tag+" main "+name):parent_keep_world(obj,wheel)
        fan=pivot(f"ofs_fan_{tag}",(12.4,side*5.75,1.84),f"fan_{tag}",axis=(1,0,0),parent=assembly)
        for obj in list(bpy.data.objects):
            if obj.name.startswith(tag+" fan blade") or obj.name.startswith(tag+" spinner spiral") or obj.name==tag+" CFM56 | spinner":
                parent_keep_world(obj,fan)
    nose=pivot("ofs_nose_gear",(4.91,0,1.95),"gear",gain=-1.52,parent=assembly)
    steering=pivot("ofs_nose_steering",(4.98,0,1.03),"steering",axis=(0,1,0),parent=nose)
    for obj in list(bpy.data.objects):
        if obj.name.startswith("Nose "):
            parent_keep_world(obj,nose)
            if any(s in obj.name for s in ["Nose L |","Nose R |","Nose axle","Nose fork"]):
                parent_keep_world(obj,steering)
    for tag,y in [("L",-.25),("R",.25)]:
        wheel=pivot(f"ofs_nose_wheel_{tag}",(5.07,y,.39),"nose_wheel",parent=steering)
        for obj in list(bpy.data.objects):
            if obj.name.startswith("Nose "+tag+" |"):parent_keep_world(obj,wheel)
    # Explicit doors already exist. Side ribs share the nearest door's assembly.
    for obj in list(bpy.data.objects):
        if obj.name.startswith("Main door"):
            center=obj.matrix_world.translation
            parent_keep_world(obj,bpy.data.objects["ofs_main_gear_"+("L" if center.y<0 else "R")])
    assembly["ofs_m36_rig"]=True

# Folded wheels clear the outer belly; this remains presentation-only travel.
bpy.data.objects["ofs_nose_gear"]["ofs_slide"]=(0,.75,0)
bpy.context.view_layer.update()
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/"output/Airbus_A320.blend"))
objects=[assembly]+list(assembly.children_recursive)
export_aircraft(ROOT/"output/Airbus_A320.glb",objects)
print("A320 articulated export:",len(objects),"objects")
