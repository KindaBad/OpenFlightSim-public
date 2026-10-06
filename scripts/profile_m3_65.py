#!/usr/bin/env python3
"""Bounded visual cost at mixed-fleet and all-reheat fighter counts."""
import os,subprocess,json,re,tempfile,argparse
from pathlib import Path
root=Path(__file__).resolve().parents[1];out=root/'docs/images/m3_65_typhoon'
env=os.environ.copy();env['LD_LIBRARY_PATH']=str(root/'.cache/sysroot/usr/lib64')
parser=argparse.ArgumentParser();parser.add_argument('--afterburner-only',action='store_true');args=parser.parse_args()
results=[r for r in json.loads((out/'performance.json').read_text()) if r['profile']=='mixed'] if args.afterburner_only and (out/'performance.json').exists() else []
with tempfile.TemporaryDirectory(prefix='ofs-profile-') as temp:
    config=Path(temp)/'profile.cfg';config.write_text('vsync=0\ndevOverlay=0\nhud=0\nplayerLabels=0\n')
    cases=[(True,[1,2,8,16])] if args.afterburner_only else [(False,[1,2,8,16,32]),(True,[1,2,8,16])]
    for afterburner,counts in cases:
        for count in counts:
            name=('afterburner' if afterburner else 'mixed')+'_'+str(count)
            cmd=[str(root/'build/release/client/ofs_client'),'--aircraft','typhoon','--airborne',
                 '--afterburner-bench' if afterburner else '--visual-bench',str(count),
                 '--config',str(config),'--width','1280','--height','800']
            run=subprocess.run(cmd,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,check=True,timeout=180)
            (out/(name+'.log')).write_text(run.stdout)
            line=next(s for s in run.stdout.splitlines() if 'BENCH aircraft=' in s)
            record=dict(re.findall(r'(\w+)=([^ ]+)',line));record['profile']='afterburner' if afterburner else 'mixed'
            results.append(record);print(line,flush=True)
            (out/'performance.json').write_text(json.dumps(results,indent=2)+'\n')
