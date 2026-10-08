"""Launcher preferences are JSON; renderer settings remain graphics.cfg."""
from dataclasses import asdict, dataclass
import math
import os
from pathlib import Path
from .storage import LauncherError, atomic_write, read_json, write_json

# key -> (default, minimum, maximum); keys mirror GraphicsSettings::load.
# Options whose meaning changed with the physically based renderer use new key
# names there (drawDistance, glare, shadowDistance), so values written by an
# older launcher are ignored by the simulator rather than misread.
GRAPHICS = {
    'preset': (2, 0, 4), 'vsync': (1, 0, 1), 'msaa': (4, 1, 16), 'fxaa': (1, 0, 1), 'fullscreen': (0, 0, 1),
    'width': (1280, 320, 16384), 'height': (800, 240, 16384),
    'drawDistance': (160000, 40000, 250000), 'textureMaxSize': (2048, 512, 8192),
    'anisotropic': (1, 0, 1), 'lodBias': (0.0, -2, 2), 'clouds': (3, 0, 3),
    'cloudShadows': (1, 0, 1), 'terrain': (2, 0, 2), 'water': (1, 0, 1), 'terrainShadows': (1, 0, 1),
    'vegetation': (1, 0, 1), 'sceneryDistance': (7000, 1000, 15000), 'treeDensity': (650, 50, 1500),
    'shadows': (3, 0, 3), 'shadowDistance': (1600, 200, 5000), 'effects': (3, 0, 3),
    'heatDistortion': (1, 0, 1), 'bloom': (1, 0, 1), 'glare': (0.045, 0, .2),
    'autoExposure': (1, 0, 1), 'exposureCompensation': (0.0, -4, 4), 'visibilityKm': (70.0, 2, 300),
    'cockpitFov': (70, 40, 100), 'hud': (1, 0, 1), 'playerLabels': (1, 0, 1),
    'contrails': (1, 0, 1), 'wingVapor': (1, 0, 1), 'engineHeat': (1, 0, 1),
}
# Mirrors GraphicsSettings::applyPreset; `preset` is the simulator's own index.
PRESETS = {
    'Low': dict(preset=0, msaa=1, fxaa=1, textureMaxSize=1024, lodBias=1.5, shadows=0, shadowDistance=800,
                effects=1, heatDistortion=0, bloom=0, clouds=1, cloudShadows=0, terrain=0, terrainShadows=0,
                water=1, vegetation=1, treeDensity=250, sceneryDistance=3500, drawDistance=80000),
    'Medium': dict(preset=1, msaa=2, fxaa=1, textureMaxSize=2048, lodBias=0.5, shadows=2, shadowDistance=1200,
                   effects=2, heatDistortion=1, bloom=1, clouds=2, cloudShadows=1, terrain=1, terrainShadows=0,
                   water=1, vegetation=1, treeDensity=450, sceneryDistance=5500, drawDistance=120000),
    'High': dict(preset=2, msaa=4, fxaa=1, textureMaxSize=2048, lodBias=0.0, shadows=3, shadowDistance=1600,
                 effects=3, heatDistortion=1, bloom=1, clouds=3, cloudShadows=1, terrain=2, terrainShadows=1,
                 water=1, vegetation=1, treeDensity=650, sceneryDistance=7000, drawDistance=160000),
    'Ultra': dict(preset=3, msaa=8, fxaa=1, textureMaxSize=4096, lodBias=-0.5, shadows=3, shadowDistance=2400,
                  effects=3, heatDistortion=1, bloom=1, clouds=3, cloudShadows=1, terrain=2, terrainShadows=1,
                  water=1, vegetation=1, treeDensity=900, sceneryDistance=10000, drawDistance=220000),
}
CUSTOM_PRESET = 4
CAMERAS = ('pursuit', 'chase', 'close-chase', 'cockpit', 'orbit', 'free')
# The first simulator version that accepts --camera pursuit.
PURSUIT_CAMERA_VERSION = '0.4.5'


def user_directory():
    base = Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local')) if os.name == 'nt' else Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
    return base / 'OpenFlightSim'


@dataclass
class Preferences:
    schema: int = 1
    installation: str = ''
    aircraft: str = 'a320'
    mode: str = 'free'
    camera: str = 'pursuit'
    # 1 once a saved 'chase' has been moved to the pursuit default it predates.
    camera_default: int = 1
    airborne: bool = True
    server: str = '127.0.0.1'
    port: int = 27020
    name: str = 'pilot'
    host: bool = False
    bots: int = 2
    # A game hosted for the local network: its announced name ('' uses the
    # pilot's) and how many AI opponents fly in it.
    lobby: str = ''
    lan_bots: int = 0
    channel: str = 'stable'
    manifest_url: str = ''  # Publisher configures a public HTTPS host; no guessed endpoint.
    auto_check: bool = True
    auto_install: bool = False
    preset: str = 'Custom'

    @classmethod
    def load(cls, path):
        if not Path(path).exists():
            return cls()
        data = read_json(path)
        if not isinstance(data, dict) or data.get('schema') != 1:
            raise LauncherError('Unsupported launcher settings schema')
        prefs = cls(**{k: v for k, v in data.items() if k in cls.__dataclass_fields__})
        if 'camera_default' not in data and data.get('camera') == 'chase':
            # Chase was the default before the pursuit camera existed, so a saved
            # 'chase' from then is the old default rather than a choice.
            prefs.camera = 'pursuit'
        defaults = cls()
        for key, value in asdict(prefs).items():
            if type(value) is not type(getattr(defaults, key)):
                raise LauncherError(f'Invalid launcher setting: {key}')
        if prefs.channel not in ('stable', 'development') or prefs.camera not in CAMERAS:
            raise LauncherError('Invalid channel or camera')
        if not 1 <= prefs.port <= 65535 or not 1 <= prefs.bots <= 8 or not 0 <= prefs.lan_bots <= 8:
            raise LauncherError('Invalid port or bot count')
        return prefs

    def save(self, path):
        write_json(path, asdict(self))


class Graphics:
    def __init__(self, path, load=True):
        self.path = Path(path)
        self.values = {key: str(rule[0]) for key, rule in GRAPHICS.items()}
        if load and self.path.exists():
            try:
                lines = self.path.read_text(encoding='utf-8').splitlines()
            except UnicodeError as exc:
                raise LauncherError('Graphics configuration must be UTF-8 text') from exc
            for line in lines:
                if '=' in line and not line.lstrip().startswith('#'):
                    key, value = line.split('=', 1)
                    self.values[key.strip()] = value.strip()
        self.validate()

    def validate(self):
        for key, (default, low, high) in GRAPHICS.items():
            try:
                raw = self.values[key]
                raw = {'true': '1', 'false': '0'}.get(raw, raw)
                number = float(raw)
                if not math.isfinite(number) or not low <= number <= high:
                    raise ValueError()
                if isinstance(default, int) and not number.is_integer():
                    raise ValueError()
                self.values[key] = str(int(number)) if isinstance(default, int) else str(number)
            except (TypeError, ValueError) as exc:
                raise LauncherError(f'Invalid graphics setting {key}; expected {low}..{high}') from exc
        if int(self.values['msaa']) not in (1, 2, 4, 8, 16):
            raise LauncherError('MSAA must be 1, 2, 4, 8 or 16')

    def preset(self, name):
        self.values.update({k: str(v) for k, v in PRESETS[name].items()})

    def save(self):
        self.validate()
        # Retain keys the simulator added, even when this launcher predates them.
        atomic_write(self.path, ('# OpenFlightSim graphics settings\n' + ''.join(f'{k}={v}\n' for k, v in self.values.items())).encode())
