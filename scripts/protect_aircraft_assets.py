#!/usr/bin/env python3
"""Create local-only authenticated aircraft packs using the keyed CMake tool.

No keys or models are added to Git. This command does not approve publication.
"""
import argparse
import json
from pathlib import Path
import secrets
import struct
import subprocess
import tempfile


def protect(tool, source, output):
    source, output = Path(source), Path(output)
    data = source.read_bytes()
    if len(data) < 20 or struct.unpack_from('<III', data) != (0x46546c67, 2, len(data)):
        raise ValueError('Expected a complete GLB 2.0 file')
    size, kind = struct.unpack_from('<II', data, 12)
    if kind != 0x4e4f534a or size > len(data) - 20:
        raise ValueError('GLB must start with a valid JSON chunk')
    document = json.loads(data[20:20+size])
    for section in ('buffers', 'images'):
        if any('uri' in item for item in document.get(section, [])):
            raise ValueError('Protected GLB must embed all buffers and textures')
    del data
    output.parent.mkdir(parents=True, exist_ok=True)
    # Never leave a partial destination on encoder/validation failure.
    with tempfile.TemporaryDirectory(dir=output.parent) as temporary:
        packed = Path(temporary) / 'asset.ofspack'
        subprocess.run([str(tool), str(source), str(packed), secrets.token_hex(24)], check=True)
        subprocess.run([str(tool), '--verify', str(packed)], check=True)
        packed.replace(output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('models', nargs='+', help='Paths relative to source root')
    args = parser.parse_args()
    source_root, output_root = args.source_root.resolve(), args.output_root.resolve()
    for model in args.models:
        relative = Path(model)
        if relative.is_absolute() or '..' in relative.parts or relative.suffix != '.glb':
            parser.error('Models must be relative .glb paths without traversal')
        protect(args.tool.resolve(), source_root / relative, output_root / (model + '.ofspack'))


if __name__ == '__main__':
    main()
