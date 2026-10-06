"""Best-effort hardware probing, isolated from conservative preset policy."""
from dataclasses import dataclass, field
import os
from pathlib import Path
import platform
import subprocess
from .storage import parse_json


@dataclass
class Hardware:
    os: str = field(default_factory=platform.platform)
    cpu: str = field(default_factory=lambda: platform.processor() or 'Unknown CPU')
    cores: int = field(default_factory=lambda: os.cpu_count() or 1)
    ram_gib: float = 0
    gpu: str = 'Unknown GPU'
    vram_gib: float = 0
    resolutions: list = field(default_factory=list)


def probe():
    info = Hardware()
    if os.name == 'nt':
        script = "$c=Get-CimInstance Win32_Processor | Select-Object -First 1; $m=Get-CimInstance Win32_ComputerSystem; $g=Get-CimInstance Win32_VideoController | Select-Object -First 1; @{cpu=$c.Name;ram=$m.TotalPhysicalMemory;gpu=$g.Name;vram=$g.AdapterRAM} | ConvertTo-Json -Compress"
        try:
            result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script], capture_output=True, timeout=8, creationflags=subprocess.CREATE_NO_WINDOW, check=True)
            data = parse_json(result.stdout.decode('utf-8-sig'))
            info.cpu = data['cpu']
            info.ram_gib = int(data['ram']) / 1024**3
            info.gpu = data['gpu'] or info.gpu
            # WMI AdapterRAM is a 32-bit legacy field: never use it to recommend Ultra.
            info.vram_gib = min(int(data['vram'] or 0) / 1024**3, 4)
        except (OSError, ValueError, TypeError, KeyError, subprocess.SubprocessError):
            pass
    else:
        try:
            for line in Path('/proc/cpuinfo').read_text().splitlines():
                if line.startswith('model name'):
                    info.cpu = line.split(':', 1)[1].strip()
                    break
            for line in Path('/proc/meminfo').read_text().splitlines():
                if line.startswith('MemTotal:'):
                    info.ram_gib = int(line.split()[1]) / 1024**2
            result = subprocess.run(['lspci'], capture_output=True, text=True, timeout=3, check=True)
            devices = [line.split(': ', 1)[-1] for line in result.stdout.splitlines() if 'VGA compatible controller' in line or '3D controller' in line]
            if devices:
                info.gpu = ' / '.join(devices)
            sizes = []
            for path in Path('/sys/class/drm').glob('card[0-9]*/device/mem_info_vram_total'):
                sizes.append(int(path.read_text()) / 1024**3)
            info.vram_gib = max(sizes, default=0)
        except (OSError, ValueError, subprocess.SubprocessError):
            pass
    return info


def recommendation(info):
    """Capacity thresholds, not vendor names; conservative when VRAM is unknown."""
    if info.ram_gib and info.ram_gib < 8 or info.cores < 4:
        return 'Low', 'Limited system memory or CPU capacity; start with reduced effects.'
    if info.ram_gib >= 16 and info.cores >= 8 and info.vram_gib >= 8:
        return 'High', 'Memory and CPU capacity support High; confirm performance in flight.'
    return 'Medium', 'Conservative starting point. GPU throughput is not benchmarked; tune after a flight.'


def display_modes(screen):
    modes = {(screen.size().width(), screen.size().height())}
    if os.name == 'nt':
        import ctypes
        # DEVMODEW layout documented by Win32; EnumDisplaySettingsW reports modes.
        class DevMode(ctypes.Structure):
            _fields_ = [('name', ctypes.c_wchar * 32), ('spec', ctypes.c_ushort), ('driver', ctypes.c_ushort),
                        ('size', ctypes.c_ushort), ('extra', ctypes.c_ushort), ('fields', ctypes.c_ulong),
                        ('union', ctypes.c_byte * 16), ('color', ctypes.c_short), ('duplex', ctypes.c_short),
                        ('y', ctypes.c_short), ('tt', ctypes.c_short), ('collate', ctypes.c_short),
                        ('form', ctypes.c_wchar * 32), ('dpi', ctypes.c_ushort), ('bits', ctypes.c_ulong),
                        ('width', ctypes.c_ulong), ('height', ctypes.c_ulong), ('flags', ctypes.c_ulong),
                        ('frequency', ctypes.c_ulong), ('rest', ctypes.c_ulong * 6)]
        mode = DevMode()
        mode.size = ctypes.sizeof(mode)
        index = 0
        while ctypes.windll.user32.EnumDisplaySettingsW(None, index, ctypes.byref(mode)):
            modes.add((mode.width, mode.height))
            index += 1
    else:
        try:
            result = subprocess.run(['xrandr', '--query'], capture_output=True, text=True, timeout=3, check=True)
            import re
            for width, height in re.findall(r'^\s+(\d+)x(\d+)\s', result.stdout, re.M):
                modes.add((int(width), int(height)))
        except (OSError, subprocess.SubprocessError):
            pass
    return sorted((w, h) for w, h in modes if w >= 320 and h >= 240)
