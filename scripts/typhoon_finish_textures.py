"""Owned close-up artwork: correctly spaced serial, fin seams and nozzle wear."""
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw,ImageFont,ImageFilter
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'assets/typhoon/textures'
atlas=Image.open(OUT/'markings.png');draw=ImageDraw.Draw(atlas)
draw.rectangle((0,0,1024,180),fill=(0,0,0,0))
font=ImageFont.truetype('/usr/share/fonts/liberation-sans-fonts/LiberationSans-Italic.ttf',164)
draw.text((8,-4),'7L',font=font,fill=(28,30,31,255))
draw.ellipse((212,20,350,158),fill=(205,32,39,255))
draw.polygon([(231,50),(331,50),(281,142)],fill=(245,243,234,255))
draw.text((365,-4),'WA',font=font,fill=(28,30,31,255))
# The atlas region remains fixed so the existing material/UV references survive.
atlas.save(OUT/'markings.png',optimize=True)
rng=np.random.default_rng(730)
for name,w,h in [('tail',2048,2048),('nozzle',1024,1024)]:
    yy,xx=np.mgrid[:h,:w];u=xx/w;v=yy/h
    if name=='tail':
        broad=2.0*np.sin(u*5)*np.sin(v*7)
        rgb=np.clip(np.array([164,171,174])+broad[:,:,None]+rng.normal(0,.45,(h,w,1)),0,255).astype('uint8')
    else:
        heat=14*np.exp(-((v-.82)/.22)**2)
        streak=3*np.sin(u*63)+2*np.sin(u*137)
        rgb=np.clip(np.array([79,76,72])+streak[:,:,None]-heat[:,:,None]+rng.normal(0,.65,(h,w,1)),0,255).astype('uint8')
    base=Image.fromarray(rgb);d=ImageDraw.Draw(base);height=Image.new('L',(w,h),128);hd=ImageDraw.Draw(height)
    if name=='tail':
        def pt(x,z):return ((x-10.6)/5.5*w,(5.30-z)/2.8*h)
        for a,b in [((14.9,5.05),(15.86,5.05)),((12.50,3.00),(14.18,2.85)),((12.35,3.57),(14.55,3.55)),((13.27,4.15),(14.95,4.12))]:
            p=[pt(*a),pt(*b)];d.line(p,fill=(132,139,143),width=2);hd.line(p,fill=102,width=2)
        box=(*pt(12.88,3.18),*pt(13.40,2.94));d.rectangle(box,outline=(135,143,147),width=2);hd.rectangle(box,outline=101,width=2)
        for x in np.arange(12.85,14.05,.13):
            a,b=pt(x,2.83);d.ellipse((a-1,b-1,a+1,b+1),fill=(130,138,140))
    else:
        for x in [.04,.96]:
            d.line((int(x*w),0,int(x*w),h),fill=(38,40,41),width=5)
            hd.line((int(x*w),0,int(x*w),h),fill=96,width=5)
        for y in [.16,.35,.78]:
            d.line((0,int(y*h),w,int(y*h)),fill=(65,63,59),width=3);hd.line((0,int(y*h),w,int(y*h)),fill=105,width=3)
    base.save(OUT/(name+'_base.png'),optimize=True)
    a=np.asarray(height,dtype=float);gx=np.gradient(a,axis=1)*.014;gy=-np.gradient(a,axis=0)*.014
    normal=np.stack((-gx,-gy,np.ones_like(a)),axis=2);normal/=np.linalg.norm(normal,axis=2)[:,:,None]
    Image.fromarray(((normal*.5+.5)*255).astype('uint8')).save(OUT/(name+'_normal.png'),optimize=True)
    mr=np.zeros((h,w,3),dtype='uint8');mr[:,:,0]=255;mr[:,:,1]=175 if name=='tail' else 154;mr[:,:,2]=0 if name=='tail' else 150
    Image.fromarray(mr).save(OUT/(name+'_mr.png'),optimize=True)
print('Corrected serial atlas, 2K fin and 1K heat-stained nozzle maps')
