#!/usr/bin/env python3
"""Measure production native rendering after capture completion."""
import os,subprocess,tempfile,json,re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
env=os.environ.copy();env['LD_LIBRARY_PATH']=str(root/'.cache/sysroot/usr/lib64')+':'+env.get('LD_LIBRARY_PATH','')
out=root/'output/m3_67';results=[]
with tempfile.TemporaryDirectory(prefix='ofs-sr71-bench-') as tmp:
 cfg=Path(tmp)/'bench.cfg';cfg.write_text('vsync=0\ndevOverlay=0\nhud=0\nplayerLabels=0\n')
 for count in (1,2,8,16,32):
  p=subprocess.run([str(root/'build/release/client/ofs_client'),'--aircraft','sr71','--airborne','--visual-bench',str(count),'--camera','orbit','--orbit-distance','44','--width','1440','--height','900','--config',str(cfg)],cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120)
  (out/f'render_{count}.log').write_text(p.stdout);assert p.returncode==0
  line=next(x for x in p.stdout.splitlines()if '[VISUAL] BENCH' in x);results.append({'count':count,'native_measurement':line});print(line,flush=True)
(out/'render_performance.json').write_text(json.dumps(results,indent=2)+'\n')
