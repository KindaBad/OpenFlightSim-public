#!/usr/bin/env python3
"""Deterministic native-renderer fixtures, not gameplay combat recordings."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from PIL import Image

parser=argparse.ArgumentParser()
parser.add_argument("--build",default="build/debug")
parser.add_argument("--only",help="Comma-separated capture names to refresh")
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
out=root/"docs/images/m3_6"
out.mkdir(parents=True,exist_ok=True)
cases=[
    ("a320_parked","a320","parked","orbit",95,2.25,.30),
    ("a320_surfaces","a320","surfaces","orbit",95,.7,.36),
    ("a320_flaps","a320","flaps","orbit",310,.7,.22),
    ("a320_gear_transition","a320","gear","orbit",170,2.35,.02),
    ("a320_gear_up","a320","gear","orbit",350,2.35,.02),
    ("a320_flight","a320","flight","chase",95,0,.25),
    ("a320_exhaust","a320","exhaust","close-chase",95,0,.25),
    ("a320_contrail","a320","contrail","chase",360,0,.25),
    ("su57_parked","su57","parked","orbit",95,2.25,.30),
    ("su57_surfaces","su57","surfaces","orbit",95,.7,.36),
    ("su57_flight","su57","flight","orbit",95,2.25,.35),
    ("su57_chase","su57","flight","chase",95,0,.25),
    ("su57_gun","su57","gun","orbit",98,1.57,.18),
    ("su57_exhaust","su57","exhaust","orbit",95,.2,.12),
    ("mixed_aircraft_fixture","a320","mixed","orbit",95,2.25,.30),
    ("impact_fixture","su57","impact","orbit",95,.7,.36),
    ("explosion_fixture","su57","destruction","orbit",80,.7,.36),
    ("destruction_fixture","su57","destruction","orbit",105,.7,.36),
]
manifest=[]
if args.only:
    selected=set(args.only.split(","))
    if not selected.issubset({case[0] for case in cases}):parser.error("Unknown capture name")
    cases=[case for case in cases if case[0] in selected]
    previous=out/"manifest.json"
    if previous.exists():manifest=[entry for entry in json.loads(previous.read_text()) if Path(entry["image"]).stem not in selected]
with tempfile.TemporaryDirectory(prefix="ofs-m36-") as temp:
    config=Path(temp)/"capture.cfg"
    config.write_text("vsync=0\ndevOverlay=0\nhud=1\n",encoding="ascii")
    for name,aircraft,scenario,camera,frames,yaw,pitch in cases:
        ppm=Path(temp)/(name+".ppm")
        command=[str(root/args.build/"client/ofs_client"),"--aircraft",aircraft,
                 "--visual-scenario",scenario,"--camera",camera,"--frames",str(frames),
                 "--orbit-yaw",str(yaw),"--orbit-pitch",str(pitch),"--config",str(config),
                 "--width","1280","--height","800","--screenshot",str(ppm)]
        if scenario=="gun":command.extend(["--orbit-distance","65"])
        if scenario=="mixed":command.extend(["--orbit-distance","120"])
        if scenario=="exhaust" and aircraft=="su57":command.extend(["--orbit-distance","18"])
        with (out/(name+".log")).open("w") as log:
            subprocess.run(command,cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=90)
        image=Image.open(ppm)
        image.save(out/(name+".png"))
        manifest.append(dict(image=name+".png",aircraft=aircraft,scenario=scenario,camera=camera,
                             frames=frames,fixture_seconds=(frames-2)/60,width=image.width,height=image.height))
        print(name,flush=True)
(out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="ascii")
