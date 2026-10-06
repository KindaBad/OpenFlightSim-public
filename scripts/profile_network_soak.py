#!/usr/bin/env python3
"""Optional Linux RSS sampling of the real 180-second GNS test (not a dependency)."""
import json
import pathlib
import subprocess
import sys
import time

if len(sys.argv) != 2 or not pathlib.Path('/proc/self/status').exists():
    raise SystemExit('Usage on Linux: profile_network_soak.py /path/to/ofs_network_tests')
process = subprocess.Popen([sys.argv[1], 'soak'])
start = time.monotonic()
samples = []
while process.poll() is None:
    try:
        status = pathlib.Path(f'/proc/{process.pid}/status').read_text()
        rss = next(int(line.split()[1]) for line in status.splitlines()
                   if line.startswith('VmRSS:'))
        samples.append((time.monotonic() - start, rss))
    except (FileNotFoundError, StopIteration):
        pass
    time.sleep(1)
warm = [rss for elapsed, rss in samples if elapsed >= 30]
print(json.dumps({'samples': len(samples), 'rss_peak_KiB': max((x[1] for x in samples), default=0),
                  'rss_after_30s_min_KiB': min(warm, default=0),
                  'rss_after_30s_max_KiB': max(warm, default=0),
                  'rss_final_KiB': samples[-1][1] if samples else 0,
                  'test_exit': process.returncode}))
raise SystemExit(process.returncode)
