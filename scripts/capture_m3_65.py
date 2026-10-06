#!/usr/bin/env python3
"""Native renderer evidence; controlled visual fixtures and real physics demos."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--build',default='build/debug');p.add_argument('--only');a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=root/'docs/images/m3_65_typhoon';out.mkdir(exist_ok=True,parents=True)
# name, scenario, camera, frames, yaw, pitch, distance, dark, physics-demo
cases=[
 ('runtime_parked','parked','orbit',95,2.3,.23,24,False,''),
 ('runtime_surfaces','surfaces','orbit',95,2.3,.23,24,False,''),
 ('runtime_chase','flight','chase',95,0,.25,0,False,''),
 ('runtime_close_chase','flight','close-chase',95,0,.25,0,False,''),
 ('runtime_cockpit','flight','cockpit',95,0,.25,0,False,''),
 ('runtime_gear_transition','gear','orbit',150,2.3,-.12,26,False,''),
 ('runtime_gear_up','gear','orbit',350,2.3,-.12,26,False,''),
 ('runtime_underside','flight','orbit',95,2.3,-.58,26,False,''),
 ('runtime_contrail','contrail','chase',360,0,.25,0,False,''),
 ('runtime_gun','gun','orbit',98,1.57,.16,27,False,''),
 ('runtime_mixed_fixture','mixed','orbit',95,2.3,.40,64,False,''),
 ('ab_rear_off','idle','orbit',95,0,.02,14,False,''),
 ('ab_rear_military','military','orbit',95,0,.02,14,False,''),
 ('ab_rear_on','afterburner','orbit',95,0,.02,14,False,''),
 ('ab_three_quarter','afterburner','orbit',95,.75,.12,19,False,''),
 ('ab_dark','afterburner','orbit',95,.60,.12,19,True,''),
 ('ab_flight','afterburner','chase',95,0,.25,0,False,''),
 ('ab_multiple','afterburner-multiple','orbit',95,.75,.28,60,False,''),
 ('ab_ignition','afterburner-transition','orbit',96,.60,.12,19,True,''),
 ('ab_extinction','afterburner-transition','orbit',225,.60,.12,19,True,''),
 ('runtime_taxi','','orbit',600,2.3,.23,24,False,'taxi'),
 ('runtime_takeoff','','orbit',880,2.3,.23,26,False,'takeoff'),
 ('runtime_climb_ab','','orbit',1800,.60,.14,20,False,'takeoff'),
]
if a.only:
    chosen=set(a.only.split(','));assert chosen.issubset({c[0]for c in cases});cases=[c for c in cases if c[0]in chosen]
manifest=[]
env=os.environ.copy();env['LD_LIBRARY_PATH']=str(root/'.cache/sysroot/usr/lib64')+':'+env.get('LD_LIBRARY_PATH','')
with tempfile.TemporaryDirectory(prefix='ofs-m365-')as temp:
    for name,scenario,camera,frames,yaw,pitch,distance,dark,demo in cases:
        cfg=Path(temp)/(name+'.cfg')
        settings='vsync=0\ndevOverlay=0\nhud=0\nplayerLabels=0\nfogDensity=0.000025\n'
        if dark:settings+='sunIntensity=0.12\nzenithR=0.002\nzenithG=0.005\nzenithB=0.016\nhorizonR=0.012\nhorizonG=0.018\nhorizonB=0.036\nskyAmbientR=0.015\nskyAmbientG=0.019\nskyAmbientB=0.029\nfogDensity=0\n'
        cfg.write_text(settings);ppm=Path(temp)/(name+'.ppm')
        cmd=[str(root/a.build/'client/ofs_client'),'--aircraft','typhoon','--camera',camera,'--frames',str(frames),
             '--orbit-yaw',str(yaw),'--orbit-pitch',str(pitch),'--config',str(cfg),'--width','1440','--height','900','--screenshot',str(ppm)]
        if distance:cmd+=['--orbit-distance',str(distance)]
        if scenario:cmd+=['--visual-scenario',scenario]
        if demo:cmd+=['--flight-demo',demo]
        with (out/(name+'.log')).open('w')as log:subprocess.run(cmd,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
        Image.open(ppm).save(out/(name+'.png'))
        manifest.append(dict(name=name,scenario=scenario,physics_demo=demo,frames=frames,yaw=yaw,pitch=pitch,distance=distance,dark=dark))
        print(name,flush=True)
if a.only and (out/'runtime_manifest.json').exists():
    manifest=[x for x in json.loads((out/'runtime_manifest.json').read_text())if x['name']not in {c[0]for c in cases}]+manifest
(out/'runtime_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
