"""Source-only skips are distinct from success; malformed content cannot skip."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

def run(root, policy):
    return subprocess.run([sys.argv[1],str(root),policy],capture_output=True,text=True)

def fixture(path, length=37.57):
    positions=struct.pack('<9f',0,0,-17.9,length,0,17.9,20,11.76,0)
    doc=dict(asset={'version':'2.0'},buffers=[{'byteLength':len(positions)}],
             bufferViews=[{'buffer':0,'byteLength':len(positions)}],
             accessors=[{'bufferView':0,'componentType':5126,'count':3,'type':'VEC3'}],
             meshes=[{'primitives':[{'attributes':{'POSITION':0}}]}],
             nodes=[{'mesh':0}],scenes=[{'nodes':[0]}],scene=0)
    text=json.dumps(doc).encode();text+=b' '*(-len(text)%4)
    data=struct.pack('<III',0x46546c67,2,12+8+len(text)+8+len(positions))
    data+=struct.pack('<II',len(text),0x4e4f534a)+text+struct.pack('<II',len(positions),0x004e4942)+positions
    path.write_bytes(data)

with tempfile.TemporaryDirectory() as empty:
    root=Path(empty)
    for policy,code in [('required',1),('optional',77)]:
        p=run(root,policy)
        assert p.returncode==code,(policy,p.stdout,p.stderr)
        assert ('Required production model absent' in p.stderr if code==1 else '[NOT RUN asset-conformance]' in p.stdout)
    path=root/'output/Airbus_A320.glb';path.parent.mkdir()
    path.write_bytes(b'broken GLB')
    for policy in ['required','optional']:
        p=run(root,policy);assert p.returncode==1,(p.stdout,p.stderr)
    fixture(path,length=20)
    p=run(root,'required');assert p.returncode==1 and 'Physical length mismatch' in p.stderr,(p.stdout,p.stderr)
    fixture(path)
    p=run(root,'required');assert p.returncode==1 and 'Missing channel' in p.stderr,(p.stdout,p.stderr)
print('PASS missing required/optional, malformed GLB, wrong scale, missing articulation policies')
