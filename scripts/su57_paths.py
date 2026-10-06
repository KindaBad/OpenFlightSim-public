"""Shared Blender pipeline paths; invoke Blender scripts after '--' arguments."""
import argparse
from pathlib import Path
import sys

def pipeline_paths(require_source=False):
    p=argparse.ArgumentParser()
    p.add_argument('--project-root',type=Path,default=Path(__file__).resolve().parents[1])
    p.add_argument('--source',type=Path,required=require_source)
    p.add_argument('--working-output',type=Path)
    p.add_argument('--output-dir',type=Path)
    p.add_argument('--report-dir',type=Path)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    root=args.project_root.resolve()
    working=(args.working_output or root/'output/Su57-Felon.blend').resolve()
    output=(args.output_dir or root/'assets/aircraft/su57').resolve()
    reports=(args.report_dir or root/'output/m3_68').resolve()
    working.parent.mkdir(parents=True,exist_ok=True);output.mkdir(parents=True,exist_ok=True);reports.mkdir(parents=True,exist_ok=True)
    return args,root,working,output,reports
