#!/usr/bin/env python3
"""Capture actual v3 A320/su57 replication through an ordinary server."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import time
from PIL import Image

parser=argparse.ArgumentParser()
parser.add_argument("--build",default="build/debug")
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
build=root/args.build
out=root/"docs/images/m3_6"
out.mkdir(parents=True,exist_ok=True)
processes=[]
handles=[]
try:
    def launch(name,command):
        handle=(out/(name+".log")).open("w")
        handles.append(handle)
        process=subprocess.Popen([str(arg) for arg in command],cwd=root,stdout=handle,stderr=subprocess.STDOUT)
        processes.append(process)
        return process
    with tempfile.TemporaryDirectory(prefix="ofs-mixed-") as temp:
        config=Path(temp)/"capture.cfg"
        config.write_text("devOverlay=0\nvsync=0\n",encoding="ascii")
        ppm=Path(temp)/"mixed.ppm"
        launch("mixed_network_server",[build/"network/ofs_server","--port","27023","--seconds","30"])
        time.sleep(.3)
        client=launch("mixed_network_client",[build/"client/ofs_client","--server","127.0.0.1","--port","27023",
                                              "--aircraft","a320","--name","A","--network-smoke",
                                              "--width","1280","--height","800","--config",config,"--screenshot",ppm])
        su57Ppm=Path(temp)/"su57.ppm"
        su57=launch("mixed_network_su57",[build/"client/ofs_client","--server","127.0.0.1","--port","27023",
                                                "--aircraft","su57","--name","B","--network-smoke",
                                                "--width","1280","--height","800","--config",config,"--screenshot",su57Ppm])
        assert client.wait(timeout=30)==0,"native mixed network smoke failed"
        assert su57.wait(timeout=30)==0,"su57 mixed network smoke failed"
        Image.open(ppm).save(out/"mixed_network.png")
        Image.open(su57Ppm).save(out/"mixed_network_su57.png")
        for process in processes:
            assert process.wait(timeout=35)==0,"mixed peer failed"
        print("actual mixed A320/su57 native capture PASS")
finally:
    for process in processes:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
    for handle in handles:
        handle.close()
