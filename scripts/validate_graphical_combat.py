#!/usr/bin/env python3
"""Run a dedicated fixture server and two real native clients on the desktop.
Only the server test fixture arranges deterministic duels. No client supplies
transforms or hits; production launch/spawn behavior is unchanged.
"""
import os
import pathlib
import subprocess
import sys
import time
import shutil
from PIL import Image

root = pathlib.Path(__file__).resolve().parents[1]
build = root / (sys.argv[1] if len(sys.argv) > 1 else 'build/debug')
logs = root / '.cache/m3'
logs.mkdir(parents=True, exist_ok=True)
processes = []
handles = []
try:
    def launch(label, args):
        handle = (logs / f'graphical-{label}.log').open('w')
        handles.append(handle)
        process = subprocess.Popen([str(arg) for arg in args], stdout=handle, stderr=subprocess.STDOUT)
        processes.append((label, process))
    launch('server', [build / 'tests/ofs_combat_tests', 'graphical-server'])
    time.sleep(.4)
    for name in ('A', 'B'):
        launch(name, [build / 'client/ofs_client', '--server', '127.0.0.1', '--port', '27021',
                      '--name', name, '--aircraft', 'su57', '--combat-smoke', '--screenshot', logs / f'combat-{name}.ppm'])
    for label, process in processes:
        code = process.wait(timeout=40)
        print(f'{label}: exit={code}')
    if any(process.returncode for _, process in processes):
        raise SystemExit(1)
    evidence=root/'docs/images/m3_6'
    evidence.mkdir(parents=True,exist_ok=True)
    for name in ('A','B'):
        Image.open(logs/f'combat-{name}.ppm').save(evidence/f'combat_network_{name}.png')
    for label in ('server','A','B'):
        shutil.copyfile(logs/f'graphical-{label}.log',evidence/f'combat_network_{label}.log')
finally:
    for _, process in processes:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
    for handle in handles:
        handle.close()
