"""Application-menu entry for the current user. Best effort; never needs admin."""
import logging
import os
from pathlib import Path
import subprocess
from .config import user_directory
from .storage import atomic_write

log = logging.getLogger('ofs.shortcuts')
NAME = 'OpenFlightSim'
DESKTOP_ID = 'openflightsim'


def _quote(value):
    # Desktop Entry Exec quoting: reserved characters are escaped inside double
    # quotes, and a literal percent sign is doubled.
    text = str(value)
    for character in ('\\', '"', '`', '$'):
        text = text.replace(character, '\\' + character)
    return '"' + text.replace('%', '%%') + '"'


def desktop_entry(target, arguments=(), icon=None):
    icon = str(icon) if icon and Path(icon).is_file() else 'applications-games'
    return ('[Desktop Entry]\nType=Application\nName=' + NAME + '\n'
            'Comment=Flight simulator\n'
            'Exec=' + ' '.join(_quote(part) for part in (target, *arguments)) + '\n'
            'Path=' + str(Path(target).parent) + '\n'
            'Icon=' + icon + '\n'
            'Terminal=false\nCategories=Game;Simulation;\n'
            'StartupWMClass=' + DESKTOP_ID + '\n')


def _linux(target, arguments, icon):
    base = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')
    path = base / 'applications' / (DESKTOP_ID + '.desktop')
    content = desktop_entry(target, arguments, icon).encode()
    if any(c in str(target) for c in '\n\r'):
        raise OSError('Unsupported installation path')
    if path.is_file() and path.read_bytes() == content:
        return path
    atomic_write(path, content)
    return path


def _windows(target, arguments, icon, state):
    programs = Path(os.environ['APPDATA']) / 'Microsoft/Windows/Start Menu/Programs'
    path = programs / (NAME + '.lnk')
    # PowerShell start-up is slow; only run it when the shortcut is absent or the
    # installation moved. The marker lives with the user's launcher settings.
    marker = Path(state or user_directory()) / 'shortcut-target.txt'
    wanted = '\n'.join((str(target), *map(str, arguments)))
    if path.is_file() and marker.is_file() and marker.read_text(encoding='utf-8') == wanted:
        return path
    programs.mkdir(parents=True, exist_ok=True)
    # Values travel in the environment, never inside the command text.
    script = ('$s=(New-Object -ComObject WScript.Shell).CreateShortcut($env:OFS_SHORTCUT);'
              '$s.TargetPath=$env:OFS_TARGET;$s.Arguments=$env:OFS_ARGUMENTS;'
              '$s.WorkingDirectory=$env:OFS_DIRECTORY;$s.Description="Flight simulator";'
              'if($env:OFS_ICON){$s.IconLocation=$env:OFS_ICON};$s.Save()')
    env = {**os.environ, 'OFS_SHORTCUT': str(path), 'OFS_TARGET': str(target),
           'OFS_ARGUMENTS': subprocess.list2cmdline([str(a) for a in arguments]),
           'OFS_DIRECTORY': str(Path(target).parent),
           'OFS_ICON': str(icon) if icon and Path(icon).suffix.lower() in ('.ico', '.exe') else ''}
    subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script], env=env,
                   capture_output=True, timeout=20, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    atomic_write(marker, wanted.encode('utf-8'))
    return path


def install(target, arguments=(), icon=None, state=None):
    """Create or refresh the menu entry; returns its path, or None on failure."""
    try:
        target = Path(target).resolve()
        path = _windows(target, arguments, icon, state) if os.name == 'nt' else _linux(target, arguments, icon)
        log.info('Application menu entry: %s', path)
        return path
    except (OSError, KeyError, subprocess.SubprocessError) as exc:
        # A missing menu entry must never stop the game from starting.
        log.warning('Could not create application menu entry: %s', exc)
        return None
