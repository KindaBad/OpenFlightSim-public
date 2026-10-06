"""Launcher preferences are JSON; renderer settings remain graphics.cfg."""
from dataclasses import asdict, dataclass
import math
import os
from pathlib import Path
from .storage import LauncherError, atomic_write, read_json, write_json

# key -> (default, minimum, maximum); keys mirror GraphicsSettings::load.
GRAPHICS = {
    'vsync': (1, 0, 1), 'msaa': (4, 1, 16), 'fullscreen': (0, 0, 1),
    'width': (1280, 320, 16384), 'height': (800, 240, 16384),
    'renderDistance': (24000, 500, 120000), 'textureMaxSize': (2048, 512, 8192),
    'anisotropic': (1, 0, 1), 'lodBias': (0.0, -2, 2), 'clouds': (3, 0, 3),
    'cloudShadows': (1, 0, 1), 'vegetation': (1, 0, 1), 'sceneryDistance': (9000, 1000, 15000),
    'shadows': (3, 0, 3), 'effects': (3, 0, 3), 'shadowMapSize': (2048, 512, 4096),
    'shadowExtent': (160, 30, 2000), 'bloom': (1, 0, 1), 'bloomStrength': (0.14, 0, .4),
    'cockpitFov': (70, 40, 100), 'hud': (1, 0, 1), 'playerLabels': (1, 0, 1),
    'contrails': (1, 0, 1), 'wingVapor': (1, 0, 1), 'engineHeat': (1, 0, 1), 'fog': (1, 0, 1),
}
PRESETS = {
    'Low': dict(msaa=1, textureMaxSize=1024, shadows=0, effects=1, clouds=1,
                shadowMapSize=512, cloudShadows=0, vegetation=0, sceneryDistance=3000,
                renderDistance=12000, lodBias=1.5, bloom=0),
    'Medium': dict(msaa=2, textureMaxSize=2048, shadows=1, effects=2, clouds=2,
                   shadowMapSize=1024, cloudShadows=0, vegetation=1, sceneryDistance=6000,
                   renderDistance=20000, lodBias=1.0, bloom=1),
    'High': dict(msaa=4, textureMaxSize=2048, shadows=2, effects=3, clouds=3,
                 shadowMapSize=2048, cloudShadows=1, vegetation=1, sceneryDistance=9000,
                 renderDistance=24000, lodBias=0.0, bloom=1),
    'Ultra': dict(msaa=8, textureMaxSize=4096, shadows=3, effects=3, clouds=3,
                  shadowMapSize=4096, cloudShadows=1, vegetation=1, sceneryDistance=15000,
                  renderDistance=40000, lodBias=-0.5, bloom=1),
}


def user_directory():
    base = Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local')) if os.name == 'nt' else Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
    return base / 'OpenFlightSim'


@dataclass
class Preferences:
    schema: int = 1
    installation: str = ''
    aircraft: str = 'a320'
    mode: str = 'free'
    camera: str = 'chase'
    airborne: bool = True
    server: str = '127.0.0.1'
    port: int = 27020
    name: str = 'pilot'
    host: bool = False
    bots: int = 2
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
        defaults = cls()
        for key, value in asdict(prefs).items():
            if type(value) is not type(getattr(defaults, key)):
                raise LauncherError(f'Invalid launcher setting: {key}')
        if prefs.channel not in ('stable', 'development') or prefs.camera not in ('chase', 'close-chase', 'cockpit', 'orbit', 'free'):
            raise LauncherError('Invalid channel or camera')
        if not 1 <= prefs.port <= 65535 or not 1 <= prefs.bots <= 8:
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
