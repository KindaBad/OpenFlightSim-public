#!/usr/bin/env python3
"""Compile portable bgfx shaders with the pinned shaderc, then embed its binaries.

Usage: pack_shaders.py OUTPUT_HEADER SOURCE_DIR SHADERC BGFX_SRC BACKENDS
BACKENDS is a comma-separated list of glsl,dx11. DXBC compilation needs Windows.
No shader container is synthesized here: all binary metadata comes from shaderc.
"""
import subprocess
import sys
from pathlib import Path


def main():
    header, sources, compiler, bgfx, backends = sys.argv[1:]
    header, sources, bgfx = Path(header), Path(sources), Path(bgfx)
    header.parent.mkdir(parents=True, exist_ok=True)
    profiles = {"glsl": ("linux", "430"), "dx11": ("windows", "s_5_0")}
    lines = ["// Generated from pinned bgfx shaderc output. Do not edit.",
             "#pragma once", "#include <cstdint>"]
    for backend in filter(None, backends.split(",")):
        platform, profile = profiles[backend]
        for source in sorted(sources.glob("*_?s.glsl")):
            stage = "vertex" if source.stem.endswith("_vs") else "fragment"
            symbol = f"{source.stem}_{backend}"
            binary = header.parent / f"{symbol}.bin"
            command = [compiler, "-f", str(source), "-o", str(binary),
                       "--type", stage, "--platform", platform, "-p", profile,
                       "--varyingdef", str(sources / "varying.def.sc"),
                       "-i", str(bgfx / "src"), "-i", str(sources), "-O", "3"]
            print(f"shaderc: {source.name} -> {backend}/{profile}", flush=True)
            subprocess.run(command, check=True)
            blob = binary.read_bytes()
            lines += [f"inline constexpr std::uint8_t {symbol}[] = {{",
                      ",".join(f"0x{b:02x}" for b in blob), "};"]
    temporary = header.with_suffix(".tmp")
    temporary.write_text("\n".join(lines) + "\n")
    temporary.replace(header)


if __name__ == "__main__":
    main()
