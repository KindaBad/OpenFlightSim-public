COL=cols['06']
# Rounded rectangles sampled onto the actual compound fuselage curvature.
def rounded(cx,cz,w,h,r,n=10):
    pts=[]
    for dx,dz,a in [(w/2-r,h/2-r,0),(-w/2+r,h/2-r,pi/2),(-w/2+r,-h/2+r,pi),(w/2-r,-h/2+r,3*pi/2)]:
        for k in range(n):
            ang=a+pi/2*k/(n-1);pts.append((cx+dx+r*cos(ang),cz+dz+r*sin(ang)))
    dense=[]
    for a,b in zip(pts,pts[1:]+pts[:1]):
        nseg=max(1,int(math.dist(a,b)/.035))
        dense.extend([(a[0]+(b[0]-a[0])*j/nseg,a[1]+(b[1]-a[1])*j/nseg) for j in range(nseg)])
    return dense

def patch(name,outline,side,m,off=.013):
    # Triangular fan follows curvature rather than one planar n-gon.
    cx=sum(p[0] for p in outline)/len(outline);cz=sum(p[1] for p in outline)/len(outline)
    v=[surfz(cx,cz,side,off)]+[surfz(x,z,side,off) for x,z in outline]
    f=[(0,i+1,(i+1)%len(outline)+1) for i in range(len(outline))]
    return mesh(name,v,f,m)

for side in [-1,1]:
    tag='Port' if side<0 else 'Starboard'
    # Precisely six angular flight-deck panes, no eyebrow windows.
    cockpit=[[(1.64,4.38),(2.29,4.48),(2.86,5.09),(2.32,5.01)],[(2.41,4.49),(3.11,4.51),(3.27,5.13),(2.98,5.11)],[(3.23,4.52),(3.94,4.52),(3.90,5.06),(3.40,5.14)]]
    for idx,p in enumerate(cockpit):
        # Subdivide boundary for flush projection.
        pts=[]
        for a,b in zip(p,p[1:]+p[:1]):pts.extend([(a[0]+(b[0]-a[0])*j/12,a[1]+(b[1]-a[1])*j/12) for j in range(12)])
        patch(tag+f' cockpit glass {idx+1}',pts,side,glass)
        curve(tag+' cockpit rubber seal',[surfz(x,z,side,.017) for x,z in pts],.019,dark,True)
        curve(tag+' cockpit aluminium surround',[surfz(x,z,side,.02) for x,z in pts],.009,metal,True)
    curve(tag+' windshield wiper',[surfz(1.88,4.43,side,.035),surfz(2.14,4.64,side,.038),surfz(2.50,4.76,side,.035)],.016,dark)
    xs=[6.02+i*.535 for i in range(14)]+[14.76,15.90]+[17.0+i*.535 for i in range(24)]
    for idx,x in enumerate(xs):
        if abs(x-14.75)<.2 or abs(x-15.9)<.2:continue
        z=4.56
        p=rounded(x,z,.25,.37,.108)
        patch(tag+f' cabin window {idx+1:02} surround',rounded(x,z,.29,.41,.123),side,metal,.011)
        patch(tag+f' cabin window {idx+1:02} seal',rounded(x,z,.265,.389,.114),side,dark,.014)
        patch(tag+f' cabin window {idx+1:02} glass',p,side,glass,.020)
        curve(tag+' cabin lower glass glint',[surfz(px,pz,side,.025) for px,pz in p[20:30]],.005,blue4)
    for name,x,z,w,h in [('L1' if side<0 else 'R1',5.03,4.21,.89,1.82),('L2' if side<0 else 'R2',29.63,4.22,.89,1.82),('Overwing exit 1',14.72,4.08,.56,1.05),('Overwing exit 2',15.53,4.08,.56,1.05)]:
        p=rounded(x,z,w,h,.14 if h>1.5 else .08)
        curve(tag+' '+name+' panel seam',[surfz(px,pz,side,.013) for px,pz in p],.012,seam,True)
        curve(tag+' '+name+' inner seal',[surfz(px,pz,side,.017) for px,pz in rounded(x,z,w-.045,h-.045,.12 if h>1.5 else .07)],.005,white,True)
        patch(tag+' '+name+' handle recess',rounded(x+.20,z+.1,.19,.06,.025),side,seam,.019)
        curve(tag+' '+name+' handle',[surfz(x+.125,z+.1,side,.027),surfz(x+.265,z+.1,side,.027)],.013,metal)
        patch(tag+' '+name+' viewing port',rounded(x,z+.37,.16,.21,.07),side,glass,.021)
        for dz in [-.56,.56] if h>1.5 else [-.27,.27]:
            patch(tag+' door hinge cover',rounded(x-w/2-.02,z+dz,.075,.16,.025),side,white,.025)
    # Nose avionics ports and small sensor plates.
    for x,z,w,h in [(2.7,3.62,.34,.28),(3.88,2.65,.42,.33),(8.25,2.20,.45,.33)]:
        curve(tag+' service hatch',[surfz(a,b,side,.013) for a,b in rounded(x,z,w,h,.075)],.007,seam,True)
    for x,z in [(1.97,3.10),(3.42,3.30),(4.15,3.36)]:
        p=surfz(x,z,side,.02);cylinder(tag+' pitot probe',p,(p[0]-.30,p[1]+side*.08,p[2]),.018,metal,16)
    # Very restrained large skin joints, kept subordinate to paint finish.
    for x in [6.25,10.20,22.8,27.65,31.55]:
        curve(tag+' circumferential skin joint',[surface(x,-1.40+j*2.9/130,side,.004) for j in range(131)],.0025,seam)
# Cargo holds are on starboard only.
for x,z,w,h in [(8.5,2.94,1.86,1.19),(24.86,2.86,1.85,1.18),(28.0,3.02,.91,.83)]:
    curve('Starboard cargo door seal',[surfz(a,b,1,.013) for a,b in rounded(x,z,w,h,.13)],.010,seam,True)
    patch('Cargo door handle',rounded(x+.5,z,.18,.10,.025),1,metal,.018)

COL=cols['07']
fontpath='/usr/share/fonts/rsms-inter-fonts/InterDisplay-Regular.ttf'
boldpath='/usr/share/fonts/rsms-inter-fonts/InterDisplay-Bold.ttf'
font=bpy.data.fonts.load(fontpath);bold=bpy.data.fonts.load(boldpath)
def text_mesh(name,body,x,z,size,side,m,fontobj=font,off=.003,maxwidth=None):
    # Convert type to mesh, then wrap every vertex onto the fuselage skin.
    cu=bpy.data.curves.new(name,'FONT');cu.body=body;cu.font=fontobj;cu.size=size;cu.resolution_u=10
    o=bpy.data.objects.new(name,cu);COL.objects.link(o);cu.materials.append(m)
    bpy.context.view_layer.objects.active=o;o.select_set(True);bpy.ops.object.convert(target='MESH');o=bpy.context.object
    w=max(v.co.x for v in o.data.vertices)-min(v.co.x for v in o.data.vertices) if o.data.vertices else 1
    sc=maxwidth/w if maxwidth else 1
    h=max(v.co.y for v in o.data.vertices) if o.data.vertices else 1
    cap={'AIRBUS logotype':.78,'A320 fuselage designation':.67,'Sharklets campaign title':.31}.get(name)
    sy=cap/h if cap else sc
    import bmesh
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.triangulate(bm,faces=list(bm.faces));bmesh.ops.subdivide_edges(bm,edges=list(bm.edges),cuts=4,use_grid_fill=True);bm.to_mesh(o.data);bm.free()
    for v in o.data.vertices:
        xx=x+v.co.x*sc if side<0 else x-v.co.x*sc
        v.co=surfz(xx,z+v.co.y*sy,side,off)
    o.select_set(False);return o
for side in [-1,1]:
    text_mesh('AIRBUS logotype','AIRBUS',6.0 if side<0 else 10.7,3.51,.87,side,blue2,bold,maxwidth=4.5)
    text_mesh('A320 fuselage designation','A320',10.66 if side<0 else 12.94,3.51,.75,side,blue,font,maxwidth=2.45)
    text_mesh('Sharklets campaign title','Sharklets - hunting down fuel burn',5.98 if side<0 else 16.9,5.00,.39,side,blue,font,maxwidth=11.0)
    text_mesh('Aircraft registration','F-WWIO',26.50 if side<0 else 28.3,3.76,.35,side,dark,font,maxwidth=1.65)
    text_mesh('Manufacturer serial','5098',24.55 if side<0 else 25.3,3.40,.30,side,dark,font,maxwidth=.75)
    text_mesh('Nose Airbus signature','AIRBUS',2.20 if side<0 else 3.18,3.76,.115,side,blue,bold)
    for x in [5.03,29.63]:
        text_mesh('Door exit stencil','EXIT',x-.15 if side<0 else x+.15,5.18,.08,side,red,bold)
        text_mesh('Door operating instructions','OPEN',x-.04 if side<0 else x+.04,4.34,.05,side,seam,font)
    # Circular green engine programme emblem as visible in supplied reference.
    y=side*5.75
    for sy in [-1,1]:
        cylinder('CFM badge',(12.03,y+sy*1.071,2.02),(12.03,y+sy*1.081,2.02),.105,mat('CFM green '+str(side)+str(sy),(.035,.32,.23),.15,.33),40)
# Broad interlaced globe bands reproduce the visual language of the reference tail.
for side in [-1,1]:
    for k,m in enumerate([blue2,blue3,blue4,white,blue3]):
        v=[];f=[]
        for i in range(101):
            z=7.55+i*(4.18/100);t=(z-7.55)/4.18
            center=-.29+k*.23+.77*max(0,1-t)**1.75
            for u in [center,center+.16]:v.append(fp(z,max(.006,min(.994,u)),side,.018))
        for i in range(100):f.append((i*2,i*2+1,i*2+3,i*2+2))
        mesh('Airbus tail broad ribbon '+str(k)+' '+str(side),v,f,m)
    # Crossing crescent gives the Airbus globe its layered curved pattern.
    for k,m in enumerate([blue2,blue4,blue3]):
        v=[];f=[]
        for i in range(101):
            z=7.65+i*4.05/100;t=(z-7.65)/4.05
            center=-.8+k*.29+1.37*t**.6
            for u in [center,center+.10]:v.append(fp(z,max(.008,min(.992,u)),side,.025))
        for i in range(100):f.append((i*2,i*2+1,i*2+3,i*2+2))
        mesh('Airbus tail cross ribbon '+str(k)+' '+str(side),v,f,m)
    cu=bpy.data.curves.new('Tail A320','FONT');cu.body='A320';cu.font=bold;cu.size=1.27;cu.resolution_u=12
    o=bpy.data.objects.new('Tail A320 '+str(side),cu);COL.objects.link(o);cu.materials.append(white);bpy.context.view_layer.objects.active=o;o.select_set(True);bpy.ops.object.convert(target='MESH');o=bpy.context.object
    width=max(v.co.x for v in o.data.vertices);factor=3.9/width
    # Tessellate the lettering so it conforms to the tail curvature.
    import bmesh
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.triangulate(bm,faces=list(bm.faces));bmesh.ops.subdivide_edges(bm,edges=list(bm.edges),cuts=5,use_grid_fill=True);bm.to_mesh(o.data);bm.free()
    for v in o.data.vertices:
        xx=30.49+v.co.x*factor if side<0 else 34.65-v.co.x*factor;zz=6.58+v.co.y*factor;le,c,t=finparam(zz);u=max(.015,min(.99,(xx-le)/c));v.co=fp(zz,u,side,.004)
    o.select_set(False)
# Dorsal / ventral antennas and red beacons.
for x,length,height in [(9.5,.65,.37),(20.7,.72,.46),(26.3,.56,.31)]:
    zz=interp(x,3)+interp(x,2)
    mesh('VHF blade antenna',[(x-.2,-.035,zz),(x+length,-.035,zz),(x+.34,-.023,zz+height),(x+.12,-.023,zz+height),(x-.2,.035,zz),(x+length,.035,zz),(x+.34,.023,zz+height),(x+.12,.023,zz+height)],[(0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],white)
for x,z in [(13.3,5.91),(17.0,1.28)]:
    uv('Anti collision beacon base',(x,0,z),(.17,.13,.065),metal)
    uv('Anti collision red lens',(x,0,z+.04 if z>3 else z-.04),(.11,.085,.07),red)
for x in [7.3,23.0]:
    z=interp(x,3)-interp(x,2)
    mesh('Ventral blade antenna',[(x,0,z),(x+.6,0,z),(x+.34,0,z-.30),(x+.15,0,z-.31)],[(0,1,2,3)],white)
# APU exhaust recessed into tail cone.
COL=cols['01']
lathe('APU exhaust surround',[(37.35,.12),(37.58,.095),(37.60,.069),(37.41,.068)],0,4.68,metal,64)
cylinder('APU exhaust darkness',(37.36,0,4.68),(37.37,0,4.68),.071,dark,48)
print('DETAILS COMPLETE',len(root.all_objects))
