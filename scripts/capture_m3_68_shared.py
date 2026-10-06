"""Shared native graphics inspection, including old aircraft and combat effects."""
import os,subprocess,tempfile
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1];out=root/'docs/images/m3_68_su57'
env=os.environ.copy();env['LD_LIBRARY_PATH']=str(root/'.cache/sysroot/usr/lib64')
cases=[('shared_a320','a320','parked',''),('shared_su57','su57','flight',''),('shared_typhoon','typhoon','afterburner',''),('shared_sr71','sr71','high-altitude',''),('shared_su57_gun','su57','gun',''),('shared_su57_hit','su57','impact',''),('shared_su57_destruction','su57','destruction',''),('shared_bloom_off','su57','afterburner','bloom=0\n'),('shared_1024_textures','su57','flight','textureMaxSize=1024\n')]
with tempfile.TemporaryDirectory(prefix='ofs-m368-shared-')as tmp:
 for name,aircraft,scenario,extra in cases:
  cfg=Path(tmp)/(name+'.cfg');cfg.write_text('vsync=0\ndevOverlay=0\nhud=0\nplayerLabels=0\n'+extra)
  ppm=Path(tmp)/(name+'.ppm');distance='48'if aircraft in ['a320','sr71']else'30'
  cmd=[str(root/'build/release/client/ofs_client'),'--aircraft',aircraft,'--visual-scenario',scenario,'--camera','orbit','--orbit-yaw','2.3','--orbit-pitch','.22','--orbit-distance',distance,'--frames','110','--width','1440','--height','900','--config',str(cfg),'--screenshot',str(ppm)]
  if name=='shared_bloom_off':cmd[cmd.index('--orbit-yaw')+1]='.65'
  with (out/(name+'.log')).open('w')as log:subprocess.run(cmd,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=120,check=True)
  Image.open(ppm).save(out/(name+'.png'));print(name,flush=True)
