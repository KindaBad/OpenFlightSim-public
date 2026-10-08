"""Shader sources stay in step with the C++ they mirror, and stay portable.

Three things here cannot be checked by compiling for one backend:

* terrain_fs.glsl carries a copy of ofs::terrainElevation so the per-pixel
  normal, lake shorelines and relief shadows follow the collision surface.
* frame.glsl and FrameConstants describe the same packed uniform array.
* Windows compiles the same sources as HLSL. With the pinned shaderc supplied
  (argument 2 and the bgfx source directory as argument 3) every shader is also
  compiled through the HLSL front end to SPIR-V, which rejects GLSL-only
  constructs on a Linux build machine.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(sys.argv[1])
SHADERS = ROOT / 'client/shaders'
NUMBER = re.compile(r'(?<![\w.])(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?')


def numbers(text):
    return [float(token) for token in NUMBER.findall(text)]


def body(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


# --- Terrain height: identical constants, in the same order -------------------
cpp = body((ROOT / 'core/include/ofs/terrain.hpp').read_text(encoding='utf-8'), 'inline double terrainElevation', 'inline const Vec3& terrainVertex')
glsl = body((SHADERS / 'terrain_fs.glsl').read_text(encoding='utf-8'), 'float terrainHeight(vec2 eastSouth)', '// Sine-free hash')
# The C++ takes (north, east); the shader takes render axes. Only the numeric
# model has to agree: radii, ramps, wavelengths and amplitudes.
expected = [n for n in numbers(cpp) if n not in (0.0, 1.0, 2.0)]
found = [n for n in numbers(glsl) if n not in (0.0, 1.0, 2.0)]
assert expected == found, f'terrain height constants differ:\n C++  {expected}\n GLSL {found}'

# --- Frame constants: one contiguous array, same length on both sides ---------
frame = (SHADERS / 'frame.glsl').read_text(encoding='utf-8')
size = int(re.search(r'uniform vec4 u_frame\[(\d+)\];', frame).group(1))
indices = [int(i) for i in re.findall(r'#define u_\w+\s+u_frame\[(\d+)\]', frame)]
assert indices == sorted(indices) and indices[0] == 0 and indices[-1] == size - 1, indices
assert sorted(set(range(size)) - set(indices)) == [], 'every frame slot is named or part of the ambient block'
header = (ROOT / 'client/src/renderer.hpp').read_text(encoding='utf-8')
assert f'sizeof(FrameConstants) == {size} * sizeof(glm::vec4)' in header, 'FrameConstants and frame.glsl disagree on size'
members = re.findall(r'glm::vec4 (\w+)(?:\[(\d+)\])?;', body(header, 'struct FrameConstants {', '};'))
assert sum(int(count or 1) for _, count in members) == size, members

# --- Battle damage: the shader cuts the mesh where the C++ says it ends --------
pbr = (SHADERS / 'pbr_fs.glsl').read_text(encoding='utf-8')
damage_size = int(re.search(r'uniform vec4 u_damage\[(\d+)\];', pbr).group(1))
source = (ROOT / 'client/src/renderer.cpp').read_text(encoding='utf-8')
assert f'createUniform("u_damage", vec4, {damage_size})' in source, 'u_damage size differs between pbr_fs.glsl and the renderer'
assert source.count(f'damageUniform[{damage_size}]') == 1 and source.count(f'damage[{damage_size}]{{') == 1, 'renderer fills a different u_damage size'
visuals = (ROOT / 'client/src/damage_visuals.hpp').read_text(encoding='utf-8')
# Wing: mix(1.25, 0.08, smoothstep(0.4, 1.0, d)); fin: mix(1.25, 0.1, ...).
for name, stub in (('wingRemaining', '.08'), ('finRemaining', '.1')):
    cpp = body(visuals, f'inline double {name}', '}')
    assert numbers(cpp) == [0.4, 0.6, 0.0, 1.0, 1.25, float(stub), 1.25, 3.0, 2.0], f'{name} changed: {numbers(cpp)}'
    assert re.search(rf'mix\(1\.25, {float(stub)}\d*, smoothstep\(0\.4, 1\.0, \w+\)\)', pbr), f'pbr_fs.glsl no longer cuts where {name} ends'

# --- Sampler stages are fixed per program on Direct3D -------------------------
for path in sorted(SHADERS.glob('*.glsl')):
    stages = {}
    sources = [path.read_text(encoding='utf-8')]
    # Follow includes so a stage clash between a pass and a shared header is caught.
    for include in re.findall(r'#include "(\w+\.glsl)"', sources[0]):
        sources.append((SHADERS / include).read_text(encoding='utf-8'))
        sources += [(SHADERS / nested).read_text(encoding='utf-8') for nested in re.findall(r'#include "(\w+\.glsl)"', sources[-1])]
    for name, stage in re.findall(r'SAMPLER\w+\((\w+),\s*(\d+)\)', '\n'.join(sources)):
        assert stages.setdefault(stage, name) == name, f'{path.name}: stage {stage} is both {stages[stage]} and {name}'

# --- Identifiers HLSL reserves ------------------------------------------------
reserved = re.compile(r'\b(?:float|vec[234]|int|bool)\s+(sample|point|line|linear|half|texture|pass|fixed|vector|matrix|input|output|triangle|precise|export)\b\s*[=;,)]')
for path in sorted(SHADERS.glob('*.glsl')):
    clash = reserved.search(path.read_text(encoding='utf-8'))
    assert not clash, f'{path.name}: "{clash.group(1)}" is reserved in HLSL'

# --- HLSL front end -----------------------------------------------------------
compiled = 0
if len(sys.argv) >= 4:
    shaderc, bgfx = sys.argv[2], Path(sys.argv[3])
    with tempfile.TemporaryDirectory() as scratch:
        for source in sorted(SHADERS.glob('*_?s.glsl')):
            stage = 'vertex' if source.stem.endswith('_vs') else 'fragment'
            result = subprocess.run(
                [shaderc, '-f', str(source), '-o', str(Path(scratch) / (source.stem + '.bin')), '--type', stage,
                 '--platform', 'linux', '-p', 'spirv', '--varyingdef', str(SHADERS / 'varying.def.sc'),
                 '-i', str(bgfx / 'src'), '-i', str(SHADERS), '-O', '3'], capture_output=True, text=True)
            assert result.returncode == 0, f'{source.name} does not compile as HLSL:\n{result.stdout[-2000:]}{result.stderr[-2000:]}'
            compiled += 1

print(f'PASS shader sources: terrain model ({len(found)} constants), {size} frame slots, sampler stages, '
      f'reserved identifiers' + (f', {compiled} shaders through the HLSL front end' if compiled else ''))
