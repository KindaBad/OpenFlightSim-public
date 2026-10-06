#!/usr/bin/env python3
"""Verify M4 using an authoritative fixture and a real SDL/bgfx client.
Generated evidence remains local. Run: python3 scripts/validate_graphical_missiles.py build/release
"""
import os
import pathlib
import subprocess
import sys
import time

root = pathlib.Path(__file__).resolve().parents[1]
build = root / (sys.argv[1] if len(sys.argv) > 1 else "build/release")
output = root / "output/m4"
output.mkdir(parents=True, exist_ok=True)
env = dict(os.environ)
if sys.platform.startswith("linux") and env.get("DISPLAY"):
    env["SDL_VIDEODRIVER"] = "x11"
config = output / "graphical.cfg"
config.write_text("vsync=0\nmsaa=0\nshadows=0\nclouds=0\neffects=2\ndevOverlay=0\nfullscreen=0\n")
processes = []
handles = []
try:
    def launch(name, arguments):
        handle = (output / f"graphical-{name}.log").open("w")
        handles.append(handle)
        process = subprocess.Popen([str(x) for x in arguments], cwd=root,
                                   env=env, stdout=handle, stderr=subprocess.STDOUT)
        processes.append((name, process))
    launch("server", [build / "tests/ofs_m4_live", "graphical-server"])
    time.sleep(.3)
    launch("missiles", [build / "client/ofs_client", "--server", "127.0.0.1",
                         "--port", "27022", "--aircraft", "su57", "--missile-smoke",
                         "--config", config, "--width", "1280", "--height", "720", "--screenshot",
                         output / "missiles.ppm"])
    for name, process in processes:
        code = process.wait(timeout=60)
        print(f"{name}: exit={code}")
        if code:
            print((output / f"graphical-{name}.log").read_text()[-5000:])
    if any(process.returncode for _, process in processes):
        raise SystemExit(1)
finally:
    for _, process in processes:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
    for handle in handles:
        handle.close()
