"""Shared Blender/physics geometry reader, without a Blender dependency."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def physical_parameters(name):
    return {r['parameter']:r['values'] for r in json.loads((ROOT/'data/physics'/f'{name}.json').read_text())['parameters']}
def blender_point(parameters,body):
    cg=parameters['asset_cg']
    return (cg[0]-body[0],body[1],cg[1]-body[2])
def hinge_point(parameters,name):
    return blender_point(parameters,parameters['hinge_'+name])
