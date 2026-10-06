COL=cols['07']
# Tiny physical fasteners and service markings reward close inspection.
def dotbatch(name,centers,r,m):
    v=[];f=[]
    for x,y,z in centers:
        k=len(v);v.extend([(x+r,y,z),(x-r,y,z),(x,y+r,z),(x,y-r,z),(x,y,z+r),(x,y,z-r)])
        f.extend(tuple(k+i for i in a) for a in [(0,2,4),(2,1,4),(1,3,4),(3,0,4),(2,0,5),(1,2,5),(3,1,5),(0,3,5)])
    return mesh(name,v,f,m)
for side in [-1,1]:
    centers=[]
    for x,z,w,h in [(5.03,4.21,1.01,1.94),(29.63,4.22,1.01,1.94),(14.72,4.08,.68,1.17),(15.53,4.08,.68,1.17)]:
        p=rounded(x,z,w,h,.17)
        centers.extend(surfz(a,b,side,.018) for a,b in p[::3])
    dotbatch('Flush door surround fasteners '+str(side),centers,.005,metal)
    centers=[]
    for j in range(1,36):
        x=1.8+j*.93
        for a in [-.73,0.02,.84]:centers.append(surface(x,a,side,.008))
    dotbatch('Sparse fuselage fastener detail '+str(side),centers,.004,seam)
    # Wing root evacuation markings and warning stencil geometry.
    COL=cols['02']
    for ya,yb in [(2.25,4.9),(5.1,8.0),(8.2,11.5)]:
        pts=[tuple(Vector(wp(y,u,side))+Vector((0,0,.02))) for y,u in [(ya,.20),(yb,.20),(yb,.43),(ya,.43)]]
        curve('Wing walk boundary',pts,.009,seam,True)
    for y in [4.2,7.2,10.6,14.6]:
        le,c,z,t=wparams(y)
        for dx in [0,.10,.20]:
            p=wp(y,.52+dx/c,side);cylinder('Spoiler hinge',tuple(Vector(p)+Vector((0,-.08,.025))),tuple(Vector(p)+Vector((0,.08,.025))),.018,metal,16)
    for y in [14.3,15.1,15.9]:
        p=Vector(wp(y,1,side));curve('Wing static discharger',[p,p+Vector((.32,0,-.025))],.010,dark)
    COL=cols['07']
    nav=mat('Navigation lens '+str(side),(.5,.008,.015) if side<0 else (.006,.30,.065),.18,.17)
    p=nav.node_tree.nodes.get('Principled BSDF');p.inputs['Emission Color'].default_value=(*((1,.01,.01) if side<0 else (.01,.8,.12)),1);p.inputs['Emission Strength'].default_value=.7
    uv('Wingtip navigation lens',(21.25,side*16.89,4.08),(.16,.08,.07),nav,32,16)
    uv('Wingtip strobe housing',(22.39,side*16.82,4.03),(.11,.065,.07),chrome,32,16)
    # Landing light housings in the inboard wing leading edge.
    p=Vector(wp(2.62,.025,side));uv('Wing landing light recess',p,(.12,.27,.12),dark)
    uv('Wing landing light glazing',p+Vector((-.035,0,0)),(.095,.21,.086),glass)
    # Rudder static wicks.
    for z in [7.8,8.8,9.8,10.7]:
        p=Vector(fp(z,.998,side,.001));curve('Rudder static discharger',[p,p+Vector((.24,0,0))],.008,dark)
# Intake anti-ice joins and ring fasteners.
COL=cols['04']
for side in [-1,1]:
    y=side*5.75;z=1.84
    dotbatch('Inlet lip fasteners '+str(side),[(11.53,y+1.04*cos(a),z+1.04*sin(a)) for a in [2*pi*j/64 for j in range(64)]],.006,metal)
    # Side stencils projected to the engine cowling.
    for sy in [-1,1]:
        for name,body,xx,zz,size,material in [('Engine service stencil','CFM56',12.47,1.97,.15,seam),('Inlet warning','DANGER  INTAKE',11.83,1.36,.061,red),('Cowl maintenance','NO STEP',13.13,2.43,.053,seam)]:
            cu=bpy.data.curves.new(name,'FONT');cu.body=body;cu.font=font;cu.size=size;cu.resolution_u=6
            o=bpy.data.objects.new(name,cu);COL.objects.link(o);cu.materials.append(material);bpy.context.view_layer.objects.active=o;o.select_set(True);bpy.ops.object.convert(target='MESH');o=bpy.context.object
            for v in o.data.vertices:
                x=xx+v.co.x if sy<0 else xx+.62-v.co.x;zv=zz+v.co.y
                rr=1.089 if x<13.0 else 1.054
                v.co=(x,y+sy*(sqrt(max(.01,rr*rr-(zv-1.84)**2))+.008),zv)
            o.select_set(False)
        # Latch identification and maintenance access panels.
        pts=[]
        for x,zv in rounded(13.42,1.82,.30,.24,.045):pts.append((x,y+sy*(sqrt(1.044**2-(zv-1.84)**2)+.009),zv))
        curve('Nacelle access panel',pts,.0035,seam,True)
# Gear door rear stiffeners and fasteners.
COL=cols['05']
for side in [-1,1]:
    y=side*3.795
    for z in [1.68,1.97,2.27]:box('Main door inner reinforcing rib',(17.35,y+side*.275,z),(.86,.06,.045),metal,.009)
    for x in [17.00,17.70]:
        cylinder('Main door hinge pivot',(x,y+side*.27,2.56),(x,y+side*.44,2.56),.048,metal,20)
# Subtle paint roughness and a faint warm belly tint.
nt=white.node_tree;p=nt.nodes.get('Principled BSDF')
geom=nt.nodes.new('ShaderNodeNewGeometry');sep=nt.nodes.new('ShaderNodeSeparateXYZ');nt.links.new(geom.outputs['Normal'],sep.inputs[0])
ramp=nt.nodes.new('ShaderNodeValToRGB');ramp.color_ramp.elements[0].position=.13;ramp.color_ramp.elements[0].color=(.68,.71,.72,1);ramp.color_ramp.elements[1].position=.65;ramp.color_ramp.elements[1].color=(.84,.875,.89,1)
mathn=nt.nodes.new('ShaderNodeMath');mathn.operation='MULTIPLY_ADD';mathn.inputs[1].default_value=.5;mathn.inputs[2].default_value=.5
nt.links.new(sep.outputs['Z'],mathn.inputs[0]);nt.links.new(mathn.outputs[0],ramp.inputs[0]);nt.links.new(ramp.outputs[0],p.inputs['Base Color'])
# The aircraft remains a single selectable assembly with separately editable parts.
COL=cols['01'];assembly=bpy.data.objects.new('AIRBUS A320 | assembly',None);COL.objects.link(assembly);assembly.empty_display_type='PLAIN_AXES';assembly.empty_display_size=2
for col in [cols[k] for k in ['01','02','03','04','05','06','07']]:
    for ob in col.objects:
        if ob!=assembly:ob.parent=assembly
assembly['Length (m)']=37.57;assembly['Wingspan (m)']=35.8;assembly['Main gear track (m)']=7.59;assembly['Wheelbase (m)']=12.64
assembly['Variant']='A320ceo with CFM56 engines and Sharklets'
print('FINISH COMPLETE',len(root.all_objects))
