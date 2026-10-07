"""Exercise the production encoder/loader, authentication and disk policy."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.protect_aircraft_assets import protect


def fixture(path, external=False):
    positions = struct.pack('<9f', 0, 0, 0, 2, 0, 0, 0, 1, 0)
    doc = dict(asset={'version': '2.0'}, buffers=[{'byteLength': len(positions)}],
        bufferViews=[{'buffer': 0, 'byteLength': len(positions)}],
        accessors=[{'bufferView': 0, 'componentType': 5126, 'count': 3, 'type': 'VEC3'}],
        meshes=[{'primitives': [{'attributes': {'POSITION': 0}}]}],
        nodes=[{'mesh': 0}], scenes=[{'nodes': [0]}], scene=0)
    if external:
        doc['images'] = [{'uri': 'texture.png'}]
    text = json.dumps(doc).encode(); text += b' ' * (-len(text) % 4)
    path.write_bytes(struct.pack('<III', 0x46546c67, 2, 28 + len(text) + len(positions)) +
        struct.pack('<II', len(text), 0x4e4f534a) + text +
        struct.pack('<II', len(positions), 0x004e4942) + positions)


def main():
    tool, keyed = Path(sys.argv[1]).resolve(), '--keyed' in sys.argv
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, packed = root / 'aircraft.glb', root / 'aircraft.glb.ofspack'
        fixture(source)
        def verify(path):
            return subprocess.run([str(tool), '--verify', str(path)], capture_output=True, text=True)
        if not keyed:
            packed.write_bytes(b'OFSPACK1' + bytes(48))
            result = verify(source)
            assert result.returncode and 'no content key' in result.stderr, result
            packed.unlink()
            assert verify(source).returncode == 0
            print('PASS unkeyed builds reject protected packs and retain loose GLB support')
            return
        expected = verify(source).stdout
        protect(tool, source, packed)
        data = packed.read_bytes()
        assert b'glTF' not in data[:40]
        assert verify(source).stdout == expected
        # No loose file exists when the renderer requests its canonical GLB name.
        source.unlink()
        assert verify(source).stdout == expected
        assert sorted(p.name for p in root.iterdir()) == [packed.name]
        for offset in (0, 8, 32, 40, len(data)-1):
            mutated = bytearray(data); mutated[offset] ^= 1; packed.write_bytes(mutated)
            assert verify(source).returncode != 0, offset
        for mutation in (data[:-1], data + b'x'):
            packed.write_bytes(mutation)
            assert verify(source).returncode != 0
        # A broken protected file must never fall back to a loose copy.
        fixture(source)
        assert verify(source).returncode != 0
        packed.unlink()
        fixture(source, external=True)
        try:
            protect(tool, source, packed)
            raise AssertionError('external URI accepted')
        except ValueError:
            assert not packed.exists()
        # The loader also rejects a malicious authenticated GLB with external URIs.
        subprocess.run([str(tool), str(source), str(packed), '01' * 24], check=True)
        result = verify(packed)
        assert result.returncode and 'external resources' in result.stderr, result
    print('PASS encrypted round trip, canonical path, tampering, truncation, no loose output, no fallback, embedded resources')

if __name__ == '__main__':
    main()
