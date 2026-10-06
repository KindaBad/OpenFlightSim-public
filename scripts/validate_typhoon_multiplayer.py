#!/usr/bin/env python3
"""Three real native graphical clients on one authoritative dedicated server."""
import os,subprocess,tempfile,time
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1];out=root/'docs/images/m3_65_typhoon'
env=os.environ.copy();env['LD_LIBRARY_PATH']=str(root/'.cache/sysroot/usr/lib64')
processes=[];handles=[]
with tempfile.TemporaryDirectory(prefix='ofs-mixed-live-')as temp:
    try:
        def launch(label,args):
            f=(out/('network_'+label+'.log')).open('w');handles.append(f)
            p=subprocess.Popen([str(x) for x in args],cwd=root,env=env,stdout=f,stderr=subprocess.STDOUT);processes.append((label,p));return p
        server=launch('server',[root/'build/release/network/ofs_server','--bind','127.0.0.1','--port','27023','--seconds','90'])
        time.sleep(.4)
        for kind in ['a320','su57','typhoon']:
            launch(kind,[root/'build/release/client/ofs_client','--server','127.0.0.1','--port','27023','--aircraft',kind,'--name',kind,
                         '--network-smoke','--seconds','20','--screenshot',Path(temp)/(kind+'.ppm')])
        for label,p in processes[1:]:
            code=p.wait(timeout=90);assert code==0,(label,code)
            Image.open(Path(temp)/(label+'.ppm')).save(out/('network_'+label+'.png'))
            print(label+' graphical network PASS',flush=True)
    finally:
        for label,p in processes:
            if p.poll() is None:p.terminate();p.wait(timeout=10)
        for f in handles:f.close()
