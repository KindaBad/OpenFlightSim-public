#!/usr/bin/env python3
"""Render the client with the developer windows hidden and a chosen camera.

Used to produce the M3.5 screenshot set. It drives the existing client binary
through its documented command line rather than adding a rendering backdoor, so
the captured frames are exactly what a player sees.
"""
import argparse
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", default=os.path.join(ROOT, "build/release/client/ofs_client"))
    parser.add_argument("--asset", default=os.path.join(ROOT, "output/Airbus_A320.glb"))
    parser.add_argument("--out", required=True, help="PPM output path")
    parser.add_argument("--camera", default="chase",
                        choices=["free", "chase", "close-chase", "orbit", "cockpit"])
    parser.add_argument("--config", default=None,
                        help="graphics config to use; a temporary one hides the dev overlay")
    parser.add_argument("--frames", type=int, default=90)
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=900)
    parser.add_argument("--bench", type=int, default=0)
    parser.add_argument("--seconds", type=int, default=0)
    args = parser.parse_args()

    config = args.config
    temporary = None
    if config is None:
        # A private config with the developer overlay off, so the capture shows
        # the game rather than the diagnostics. The HUD stays on.
        temporary = os.path.join("/tmp", f"ofs_capture_{os.getpid()}.cfg")
        with open(temporary, "w") as handle:
            handle.write("# capture config\n")
            handle.write("devOverlay=0\n")
            handle.write("hud=1\n")
            handle.write(f"msaa={4}\n")
            handle.write("vsync=0\n")
        config = temporary

    command = [args.client, "--asset", args.asset, "--config", config,
               "--camera", args.camera, "--frames", str(args.frames),
               "--width", str(args.width), "--height", str(args.height),
               "--screenshot", args.out]
    if args.bench:
        command += ["--visual-bench", str(args.bench)]
    if args.seconds:
        command += ["--seconds", str(args.seconds)]
    print("run:", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT)
    if temporary:
        os.unlink(temporary)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
