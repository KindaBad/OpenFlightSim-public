"""Original reference-informed texture artwork, no redistributed photographs.

Run with Python/Pillow/NumPy. Panel layout is visual reconstruction, not a
maintenance drawing. Fonts only rasterized; no font binary is redistributed.
"""
from pathlib import Path
import json
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/typhoon/textures';OUT.mkdir(parents=True,exist_ok=True)
FONT='/usr/share/fonts/abattis-cantarell-fonts/Cantarell-Regular.otf'
W,H=4096,2048
rng=np.random.default_rng(365)

def sheet(name, wing=False):
    noise=rng.normal(0,.62,(H,W,1))
    base=np.clip(np.array([169,175,178])+noise,0,255).astype(np.uint8)
    im=Image.fromarray(base,'RGB');d=ImageDraw.Draw(im)
    height=Image.new('L',(W,H),128);hd=ImageDraw.Draw(height)
    def line(points,width=2):
        d.line(points,fill=(132,140,143),width=width)
        hd.line(points,fill=102,width=width)
    def panel(box):
        d.rounded_rectangle(box,radius=9,outline=(144,151,154),width=2)
        hd.rounded_rectangle(box,radius=9,outline=103,width=2)
        x0,y0,x1,y1=box
        for x in np.arange(x0+14,x1-10,36):
            for y in [y0+8,y1-8]:
                d.ellipse((x-1,y-1,x+1,y+1),fill=(121,130,134));hd.point((int(x),int(y)),100)
    if not wing:
        for x in [2.65,6.35,8.55,10.75,12.8,14.45]:
            xx=int(x/16*W);line([(xx,0),(xx,H)],2)
        for v in [.04,.22,.51,.77,.94]:
            yy=int(v*H);line([(int(5.8/16*W),yy),(int(14.45/16*W),yy)],2)
        for v in [.05,.36,.56,.86]:
            for x in [6.9,9.6,11.5]:
                xx=int(x/16*W);yy=int(v*H)
                panel((xx,yy,xx+150,yy+95))
        for v in [.04,.46,.55,.96]:
            xx=int(6.45/16*W);yy=int(v*H)
            for k in range(14):
                line([(xx+7*k,yy),(xx+7*k+5,yy+45)],3)
    else:
        def pt(x,y):return (x/16*W,(.5-y/11)*H)
        for s in [-1,1]:
            for y in [1.6,2.7,3.8,4.65]:
                le=6.2+(abs(y)-.82)*1.33
                line([pt(le,s*y),pt(12.60,s*y)])
            line([pt(7.4,s*1.65),pt(11.9,s*4.9),pt(12.5,s*4.9)])
            line([pt(12.4,s*.85),pt(12.8,s*5.2)],3)
            for x,y in [(9.4,2.1),(10.6,3.0),(11.7,4.1)]:
                xx,yy=pt(x,s*y);panel((xx-45,yy-24,xx+45,yy+24))
            # Walkway dashes stay around root; no fabricated maintenance text.
            for x in np.arange(8.0,11.8,.14):
                line([pt(x,s*1.3),pt(x+.06,s*1.3)],2)
    im.save(OUT/(name+'_base.png'),optimize=True)
    a=np.asarray(height,dtype=np.float32)
    gx=np.gradient(a,axis=1)*.015;gy=-np.gradient(a,axis=0)*.015
    normal=np.stack([-gx,-gy,np.ones_like(a)],axis=2)
    normal/=np.linalg.norm(normal,axis=2,keepdims=True)
    Image.fromarray(((normal*.5+.5)*255).astype(np.uint8),'RGB').save(OUT/(name+'_normal.png'),optimize=True)
    # G roughness, B metallic. Paint remains dielectric, subtle wear in roughness.
    mr=np.zeros((H,W,3),dtype=np.uint8);mr[:,:,0]=255
    mr[:,:,1]=np.clip(173+(a-128)*.20+rng.normal(0,.5,(H,W)),0,255)
    Image.fromarray(mr,'RGB').save(OUT/(name+'_mr.png'),optimize=True)

sheet('fuselage');sheet('wings',True)
atlas=Image.new('RGBA',(2048,2048),(0,0,0,0));d=ImageDraw.Draw(atlas)
rects={}
def text(label,box,words,color=(25,29,31,255),size=62):
    rects[label]=box
    x,y,_,_=box;d.text((x+10,y+8),words,fill=color,font=ImageFont.truetype(FONT,size))

text('serial',(0,0,1024,180),'7L     WA',size=148)
d.ellipse((307,22,440,155),fill=(213,28,35,255))
d.polygon([(321,50),(426,50),(374,141)],fill=(247,246,241,255))
rects['roundel']=(1100,0,1400,300)
d.ellipse((1105,5,1395,295),fill=(213,28,35,255))
d.polygon([(1132,65),(1368,65),(1250,271)],fill=(249,248,244,255))
text('no_step',(0,230,700,350),'NO STEP',size=80)
text('rescue',(0,400,1000,540),'NOTFALL  RESCUE',(226,189,69,255),66)
d.polygon([(30,545),(880,545),(880,528),(960,566),(880,604),(880,586),(30,586)],fill=(226,189,69,255))
rects['rescue']=(0,400,1000,610)
text('intake',(0,690,1200,840),'DANGER  AIR INTAKE',(197,167,75,255),66)
text('exhaust',(0,900,1200,1040),'DANGER  EXHAUST',(172,72,58,255),66)
text('eject',(0,1120,1000,1320),'',size=58)
d.polygon([(32,1288),(126,1132),(220,1288)],outline=(206,93,73,255),width=9)
d.text((55,1200),'E',fill=(40,40,39,255),font=ImageFont.truetype(FONT,64))
text('formation',(0,1420,1024,1490),'',size=40)
d.rounded_rectangle((8,1428,1016,1484),radius=15,fill=(138,144,127,255),outline=(60,70,69,255),width=6)
text('walkway',(0,1550,950,1650),'NO STEP',size=48)
atlas.save(OUT/'markings.png',optimize=True)
(OUT/'markings_layout.json').write_text(json.dumps(rects,indent=2)+'\n')
print('Original 4096x2048 fuselage/wing base, MR and normal; 2048x2048 markings atlas')
