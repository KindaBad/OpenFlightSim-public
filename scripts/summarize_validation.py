#!/usr/bin/env python3
"""Print exact CTest JUnit passed/failed/unavailable counts, keeping skips separate."""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET

def summarize(path):
    root=ET.parse(path).getroot()
    result=dict(profile=str(path),passed=0,failed=0,skipped_not_run=0,failures=[],unavailable=[])
    for test in root.iter('testcase'):
        name=test.get('name','unnamed')
        if test.find('skipped') is not None or test.get('status') in ('notrun','disabled'):
            result['skipped_not_run']+=1;result['unavailable'].append(name)
        elif test.find('failure') is not None or test.find('error') is not None:
            result['failed']+=1;result['failures'].append(name)
        else:
            result['passed']+=1
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reports',nargs='+',type=Path)
    args=parser.parse_args()
    results=[summarize(p) for p in args.reports]
    print(json.dumps(results,indent=2))
    raise SystemExit(1 if any(r['failed'] for r in results) else 0)
