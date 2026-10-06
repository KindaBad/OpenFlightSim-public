#!/usr/bin/env python3
"""Run sequential, warmed-up native mixed-aircraft render measurements."""
import argparse
import csv
import re
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument("--build",default="build/release")
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
out=root/"docs/images/m3_6"
out.mkdir(parents=True,exist_ok=True)
rows=[]
with tempfile.TemporaryDirectory(prefix="ofs-bench-") as temp:
    config=Path(temp)/"bench.cfg"
    config.write_text("vsync=0\nmsaa=4\nshadows=2\neffects=3\nshadowMapSize=2048\ndevOverlay=0\n",encoding="ascii")
    for count in (1,2,8,16,32):
        result=subprocess.run([str(root/args.build/"client/ofs_client"),"--visual-bench",str(count),
                               "--config",str(config),"--width","1600","--height","900"],
                              cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                              check=True,timeout=90)
        (out/f"benchmark_{count}.log").write_text(result.stdout,encoding="utf-8")
        line=next(line for line in result.stdout.splitlines() if "[VISUAL] BENCH" in line)
        row=dict(re.findall(r"(\w+)=([^\s]+)",line))
        row["wallFrameMs"]=f"{1000/float(row['fps']):.2f}"
        rows.append(row)
        print(line,flush=True)
with (out/"render_performance.csv").open("w",newline="") as file:
    writer=csv.DictWriter(file,fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
