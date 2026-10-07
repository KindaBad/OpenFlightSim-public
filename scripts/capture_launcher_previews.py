#!/usr/bin/env python3
"""Capture real in-game launcher previews from the release's compiled catalogue.

Images are generated build products, never public source assets or AI artwork.
Run after installing the approved game content. No models are exported or copied.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from launcher.storage import LauncherError, read_json
from launcher.config import PRESETS


def capture(client, catalog, asset_root, output, frames=45, logs=None,
            width=1600, height=900, preset='High'):
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QImage

    client, asset_root, output = Path(client).resolve(), Path(asset_root).resolve(), Path(output).resolve()
    entries = read_json(catalog)['aircraft']
    output.mkdir(parents=True, exist_ok=True)
    logs = Path(logs or ROOT / 'build/launcher-preview-logs').resolve()
    logs.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='launcher-previews-', dir=output.parent) as temporary:
        temporary = Path(temporary)
        config = temporary / 'capture.cfg'
        settings = {**PRESETS[preset], 'vsync': 0, 'fullscreen': 0, 'devOverlay': 0,
                    'hud': 0, 'playerLabels': 0, 'clouds': 2, 'sunElevation': 28,
                    'sunAzimuth': 125, 'autoExposure': 0}
        config.write_text(''.join(f'{key}={value}\n' for key, value in settings.items()))
        for entry in entries:
            key = entry['id']
            if not isinstance(key, str) or not key.isascii() or not key.isalnum():
                raise LauncherError('Invalid preview aircraft identity')
            ppm = temporary / (key + '.ppm')
            distance = {'a320': 45, 'typhoon': 24, 'sr71': 42, 'su57': 28}.get(key, 40)
            command = [str(client), '--aircraft', key, '--visual-scenario', 'flight',
                       '--camera', 'orbit', '--orbit-yaw', '2.3', '--orbit-pitch', '.12',
                       '--orbit-distance', str(distance), '--frames', str(frames),
                       '--width', str(width), '--height', str(height), '--config', str(config),
                       '--screenshot', str(ppm)]
            log_path = logs / (key + '.log')
            with log_path.open('w') as log:
                try:
                    subprocess.run(command, cwd=asset_root, stdout=log, stderr=subprocess.STDOUT,
                                   check=True, timeout=120)
                except subprocess.SubprocessError:
                    print(log_path.read_text(errors='replace')[-6000:], file=sys.stderr)
                    raise
            image = QImage(str(ppm))
            if image.isNull() or image.width() != width or image.height() != height:
                raise LauncherError(f'The simulator did not capture a valid {key} preview')
            # The normal flight controls occupy the top right. Use the unobstructed
            # scene below them, leaving the aircraft and scenery intact.
            top = image.height() // 5
            image = image.copy(0, top, image.width(), image.height() - top)
            thumbnail = image.scaled(640, 360, Qt.AspectRatioMode.KeepAspectRatio,
                                     Qt.TransformationMode.SmoothTransformation)
            if not thumbnail.save(str(output / (key + '.jpg')), 'JPG', 90):
                raise LauncherError(f'Could not save the {key} preview')
            if key == 'a320' and not image.save(str(output / 'hero.jpg'), 'JPG', 92):
                raise LauncherError('Could not save the launcher banner')
            print(f'Captured {key} from the game', flush=True)
    if not any(entry['id'] == 'a320' for entry in entries) and entries:
        shutil.copy2(output / (entries[0]['id'] + '.jpg'), output / 'hero.jpg')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--asset-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--logs', type=Path, help='Native capture logs (defaults to build/launcher-preview-logs)')
    parser.add_argument('--frames', type=int, default=45, help='Frames to render; CI uses six to flush readback')
    parser.add_argument('--width', type=int, default=1600)
    parser.add_argument('--height', type=int, default=900)
    parser.add_argument('--preset', choices=tuple(PRESETS), default='High')
    args = parser.parse_args()
    if args.frames < 6 or not 640 <= args.width <= 3840 or not 360 <= args.height <= 2160:
        parser.error('Captures require at least six frames and dimensions within 640–3840 × 360–2160')
    capture(args.client, args.catalog, args.asset_root, args.output, frames=args.frames,
            logs=args.logs, width=args.width, height=args.height, preset=args.preset)


if __name__ == '__main__':
    main()
