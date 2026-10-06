"""Original SR71 textures/UV refinement, authored ONLY through Blender MCP.

No reference photograph is copied into these maps. Panel station detail, tiny
stencils and emblem linework are reconstructed approximations. Museum serial
and marking arrangement are photograph verified.
"""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import numpy as np
import random

if assembly.get('sr71_textured'):
    raise RuntimeError('Materials already authored; correct in place')
texdir=ROOT/'assets/aircraft/sr71/textures';texdir.mkdir(parents=True,exist_ok=True)
fontpath='/usr/share/fonts/adwaita-mono-fonts/AdwaitaMono-Bold.ttf'
font=lambda size:ImageFont.truetype(fontpath,max(8,int(size)))
rng=np.random.default_rng(7972)

def base_canvas(w,h,rgb=(44,48,53)):
    coarse=Image.fromarray(rng.integers(0,255,(64,128),dtype=np.uint8)).resize((w,h),Image.Resampling.BICUBIC).filter(ImageFilter.GaussianBlur(w/100))
    noise=(np.asarray(coarse,dtype=np.float32)-128)/34
    grain=rng.normal(0,.45,(h,w)).astype(np.float32)
    a=np.clip(np.array(rgb,dtype=np.float32)[None,None,:]+noise[:,:,None]+grain[:,:,None],0,255).astype(np.uint8)
    return Image.fromarray(a),Image.new('L',(w,h),128),Image.new('L',(w,h),160)

def line(base,height,rough,points,width=2):
    ImageDraw.Draw(base).line(points,fill=(28,31,36),width=width)
    ImageDraw.Draw(height).line(points,fill=110,width=width)
    ImageDraw.Draw(rough).line(points,fill=190,width=width+1)

def hatch(base,height,rough,rect):
    ImageDraw.Draw(base).rounded_rectangle(rect,radius=5,outline=(30,34,38),width=2)
    ImageDraw.Draw(height).rounded_rectangle(rect,radius=5,outline=108,width=2)
    d=ImageDraw.Draw(base)
    x0,y0,x1,y1=rect
    for x in range(int(x0)+7,int(x1)-4,20):
        for y in [y0+4,y1-4]:d.ellipse((x-1,y-1,x+1,y+1),fill=(71,72,72))
    for y in range(int(y0)+7,int(y1)-4,20):
        for x in [x0+4,x1-4]:d.ellipse((x-1,y-1,x+1,y+1),fill=(71,72,72))

def texture_set(label,base,height,rough,metalness=.25,normal_scale=.30):
    # Tangent-space normal map from subtle grooves; normal scale is a visual
    # approximation, not micrometre-accurate titanium corrugation.
    a=np.asarray(height,dtype=np.float32)/255
    gy,gx=np.gradient(a);nx=-gx*normal_scale*20;ny=gy*normal_scale*20
    norm=np.sqrt(nx*nx+ny*ny+1)
    normal=np.stack((nx/norm*.5+.5,ny/norm*.5+.5,1/norm*.5+.5),axis=2)
    orm=np.zeros((*a.shape,3),dtype=np.uint8);orm[:,:,0]=255;orm[:,:,1]=np.asarray(rough);orm[:,:,2]=int(metalness*255)
    paths=[]
    for suffix,im in [('basecolor',base),('orm',Image.fromarray(orm)),('normal',Image.fromarray(np.clip(normal*255,0,255).astype(np.uint8)))]:
        path=texdir/(label+'_'+suffix+'.png');im.save(path,optimize=True);paths.append(path)
    mat=material(label+' PBR',(.03,.03,.03),.62,metalness)
    nodes=mat.node_tree.nodes;links=mat.node_tree.links;p=next(n for n in nodes if n.type=='BSDF_PRINCIPLED')
    images=[]
    for path in paths:
        im=bpy.data.images.load(str(path),check_existing=True)
        if not path.stem.endswith('basecolor'):im.colorspace_settings.name='Non-Color'
        im.pack();n=nodes.new('ShaderNodeTexImage');n.image=im;images.append(n)
    links.new(images[0].outputs['Color'],p.inputs['Base Color'])
    separate=nodes.new('ShaderNodeSeparateColor');links.new(images[1].outputs['Color'],separate.inputs['Color'])
    links.new(separate.outputs['Green'],p.inputs['Roughness']);links.new(separate.outputs['Blue'],p.inputs['Metallic'])
    n=nodes.new('ShaderNodeNormalMap');n.inputs['Strength'].default_value=.55
    links.new(images[2].outputs['Color'],n.inputs['Color']);links.new(n.outputs['Normal'],p.inputs['Normal'])
    return mat

# Continuous forebody cylindrical UVs. The mapping preserves the real chine
# shell rather than projecting separate flat triangular decals onto it.
W,H=4096,2048;b,h,r=base_canvas(W,H)
for x in [1.7,3.15,4.55,6.75,8.55,10.1,12.3,14.6,16.9,19.4,21.8,24.0,26.25,28.7,30.6,31.8]:
    px=x/32.7406*W;line(b,h,r,[(px,0),(px,H)],2)
for v in [.085,.18,.32,.415,.585,.68,.82,.915]:line(b,h,r,[(0,v*H),(W,v*H)],2)
for x in [7.9,10.8,12.6,15.4,17.7,20.0,23.1,25.2,27.3]:
    for v in [.12,.37,.63,.88]:hatch(b,h,r,(x/32.7406*W,(v-.014)*H,(x+.64)/32.7406*W,(v+.014)*H))
d=ImageDraw.Draw(b)
for x in [9.2,13.2,18.3,24.1]:
    for v in [.13,.87]:d.text((x/32.7406*W,v*H),'ACCESS\nNO STEP',font=font(12),fill=(112,112,103),spacing=1)
skinmat=texture_set('sr71_fuselage_4k',b,h,r,.23,.26)
body.data.materials[0]=skinmat

# Shared wing atlas with planar UVs, fine longitudinal corrugation, restrained
# access panels. The original registration is shared by all four elevons.
W,H=4096,2048;b,h,r=base_canvas(W,H,(43,46,51))
a=np.asarray(h).copy();xx=np.arange(W,dtype=np.float32)/W*32.7406
corrug=np.sin(xx*2*math.pi/.078)*2.7
a=np.clip(a.astype(np.float32)+corrug[None,:],0,255).astype(np.uint8);h=Image.fromarray(a)
for y in [-7.8,-6.45,-5.2,-3.15,-1.9,1.9,3.15,5.2,6.45,7.8]:
    yy=(1-(y+8.4709)/16.9418)*H
    line(b,h,r,[(15.3/32.7406*W,yy),(31.8/32.7406*W,yy)],2)
for x in [20.5,22.9,25.1,27.0,28.85,30.2]:
    line(b,h,r,[(x/32.7406*W,0),(x/32.7406*W,H)],2)
for x,y in [(23.6,-6.0),(26.2,-6.8),(24.4,6),(26.2,6.8),(28,-2.3),(28,2.3)]:
    px=x/32.7406*W;py=(1-(y+8.4709)/16.9418)*H
    hatch(b,h,r,(px-42,py-23,px+42,py+23))
    ImageDraw.Draw(b).text((px+46,py-8),'NO STEP',font=font(11),fill=(105,108,101))
wingmat=texture_set('sr71_wing_4k',b,h,r,.22,.32)
for o in aircraft.objects:
    if o.type=='MESH' and ('delta fixed wing' in o.name or 'elevon' in o.name):o.data.materials[0]=wingmat

# Nacelle-specific longitudinal/circumferential mapping, independent of skin.
W,H=2048,2048;b,h,r=base_canvas(W,H,(45,47,51))
for u in [.03,.135,.32,.49,.68,.87,.98]:line(b,h,r,[(u*W,0),(u*W,H)],2)
for v in [.08,.24,.40,.58,.76,.92]:line(b,h,r,[(0,v*H),(W,v*H)],2)
for u in [.27,.43,.66,.82]:
    for v in [.18,.48,.78]:hatch(b,h,r,((u-.035)*W,(v-.023)*H,(u+.035)*W,(v+.023)*H))
d=ImageDraw.Draw(b)
for v in [.25,.75]:
    d.text((.035*W,v*H),'DANGER\nINTAKE',font=font(18),fill=(135,64,54),spacing=2)
    d.text((.80*W,v*H),'HOT AREA',font=font(14),fill=(123,119,108))
nacellemat=texture_set('sr71_nacelle_2k',b,h,r,.29,.26)
for tag in ['L','R']:bpy.data.objects[tag+' | nacelle outer loft'].data.materials[0]=nacellemat

# Tail atlas: selected museum target has a red AFLC crest, red 17972 and a
# white circular skunk emblem. This is original simplified linework.
W,H=2048,1024;b,h,r=base_canvas(W,H,(44,47,52))
def tailpoint(x,z):return ((x-24)/8*W,(1-(z-3.3)/2.5)*H)
line(b,h,r,[tailpoint(24,3.97),tailpoint(32,3.97)],2)
for x in [25.1,26.3,27.1,28.8,30.1]:line(b,h,r,[tailpoint(x,3.3),tailpoint(x,5.8)],1)
d=ImageDraw.Draw(b);red=(153,48,42);white=(184,192,187)
cx,cy=tailpoint(28.20,5.25);sx,sy=110,74
shield=[(cx-sx*.65,cy-sy*.7),(cx-sx*.76,cy-sy*.95),(cx+sx*.76,cy-sy*.95),(cx+sx*.65,cy-sy*.7),(cx+sx*.70,cy+sy*.30),(cx,cy+sy*.7),(cx-sx*.7,cy+sy*.30)]
d.line(shield+[shield[0]],fill=red,width=3)
for s in [-1,1]:
    for i in range(5):d.line([(cx,cy+20),(cx+s*(18+i*12),cy-30+i*3),(cx+s*(11+i*9),cy+13)],fill=red,width=2)
d.polygon([(cx-6,cy-42),(cx+6,cy-42),(cx+11,cy+15),(cx,cy+25),(cx-11,cy+15)],outline=red)
d.text((cx,cy+57),'AIR FORCE LOGISTICS COMMAND',font=font(11),fill=red,anchor='mm')
cx,cy=tailpoint(28.20,4.97)
d.text((cx,cy),'17972',font=font(76),fill=red,anchor='mm',stroke_width=0)
cx,cy=tailpoint(28.22,4.48);rx,ry=106,150
d.ellipse((cx-rx,cy-ry,cx+rx,cy+ry),fill=white)
# Upright skunk silhouette with raised curled tail, white stripe and muzzle.
k=(38,42,44)
d.ellipse((cx+8,cy-108,cx+76,cy+28),fill=k)
d.ellipse((cx+29,cy-78,cx+67,cy-1),fill=white)
d.polygon([(cx+25,cy+7),(cx-4,cy+47),(cx-38,cy+47),(cx-47,cy+18),(cx-36,cy-22),(cx-18,cy-36),(cx-3,cy-20),(cx+1,cy+10)],fill=k)
d.ellipse((cx-48,cy-60,cx-7,cy-14),fill=k)
d.polygon([(cx-35,cy-54),(cx-54,cy-70),(cx-52,cy-38),(cx-65,cy-28),(cx-42,cy-20)],fill=k)
d.ellipse((cx-32,cy-45,cx-24,cy-34),fill=white)
d.line([(cx-21,cy-20),(cx-29,cy+9),(cx-20,cy+36)],fill=white,width=9)
d.line([(cx-20,cy+40),(cx-37,cy+77),(cx-49,cy+77)],fill=k,width=10)
d.line([(cx-6,cy+39),(cx+6,cy+70),(cx+24,cy+77)],fill=k,width=10)
finmat=texture_set('sr71_17972_tail_2k',b,h,r,.18,.22)
for tag in ['L','R']:
    o=bpy.data.objects[tag+' | all-moving canted vertical tail'];o.data.materials[0]=finmat
    # Orient both outward sides so the serial is readable from either side.
    uv=o.data.uv_layers.active
    for p in o.data.polygons:
        for li in p.loop_indices:
            v=o.data.vertices[o.data.loops[li].vertex_index].co
            uv.data[li].uv=((v.x-24)/8,(v.z-3.3)/2.5)

# Titanium ejectors: muted brown/blue heat exposure, axial rubbed grain.
W,H=1024,1024;b,h,r=base_canvas(W,H,(86,77,71));a=np.asarray(b).astype(np.float32)
u=np.arange(W)[None,:]/W;v=np.arange(H)[:,None]/H
a[:,:,0]+=np.broadcast_to(np.sin(u*8)*5,(H,W));a[:,:,2]+=np.broadcast_to(np.cos(u*9)*7,(H,W))
a+=np.sin(v*math.pi*48)[:,:,None]*2;b=Image.fromarray(np.clip(a,0,255).astype(np.uint8))
for v in np.arange(24)/24:line(b,h,r,[(0,v*H),(W,v*H)],2)
heatmat=texture_set('sr71_ejector_heat_1k',b,h,r,.70,.25)
for o in aircraft.objects:
    if o.type=='MESH' and ('ejector nozzle outer' in o.name or 'ejector petal' in o.name):o.data.materials[0]=heatmat

# Canopy warnings are restrained original labels on small side panels.
# These remain separate surfaces for legibility without enormous skin maps.
W,H=512,256;b=Image.new('RGB',(W,H),(38,43,49));d=ImageDraw.Draw(b)
d.polygon([(30,200),(128,27),(226,200)],outline=(180,159,61),width=8)
d.text((128,155),'!',font=font(94),fill=(180,159,61),anchor='mm')
d.text((245,52),'DANGER',font=font(34),fill=(169,154,101))
d.text((245,100),'EJECTION\nSEAT',font=font(26),fill=(169,154,101),spacing=4)
warningmat=texture_set('sr71_canopy_warning_512',b,Image.new('L',(W,H),128),Image.new('L',(W,H),180),.08,.10)
for s in [-1,1]:
    mesh('Pilot canopy | warning placard '+str(s),[(5.56,s*.473,3.03),(5.94,s*.486,3.03),(5.94,s*.476,3.19),(5.56,s*.463,3.19)],[(0,1,2,3)],warningmat,bpy.data.objects['Canopy_Front'],[(0,0),(1,0),(1,1),(0,1)],lod=1,smooth=False)

assembly['sr71_textured']=True
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'output/SR71_61-7972.blend'))
for name in ['front_quarter','rear_quarter','top','markings','nozzle','cockpit','gear']:render_view(name,'pbr1')
print('Original packed PBR maps:',[(p.name,p.stat().st_size) for p in texdir.glob('*.png')])
