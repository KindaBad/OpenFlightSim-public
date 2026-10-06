"""Start external executables without leaking the frozen launcher's DLL paths."""
import os
from pathlib import Path
import subprocess
import sys
import threading

_spawn_lock = threading.Lock()


def child_environment():
    env = os.environ.copy()
    if getattr(sys, 'frozen', False):
        original = env.pop('LD_LIBRARY_PATH_ORIG', None)
        if original is None:
            env.pop('LD_LIBRARY_PATH', None)
        else:
            env['LD_LIBRARY_PATH'] = original
        bundle = Path(sys._MEIPASS).resolve()
        env['PATH'] = os.pathsep.join(p for p in env.get('PATH', '').split(os.pathsep)
                                     if p and not Path(p).resolve().is_relative_to(bundle))
        # New onefile helper must create its own extraction directory. It outlives
        # the current launcher, so inheriting PyInstaller parent state is unsafe.
        env['PYINSTALLER_RESET_ENVIRONMENT'] = '1'
    return env


def spawn(command, **kwargs):
    kwargs.setdefault('env', child_environment())
    if os.name == 'nt':
        kwargs.setdefault('creationflags', subprocess.CREATE_NO_WINDOW)
    with _spawn_lock:
        if os.name == 'nt' and getattr(sys, 'frozen', False):
            import ctypes
            ctypes.windll.kernel32.SetDllDirectoryW(None)
            try:
                return subprocess.Popen(command, **kwargs)
            finally:
                ctypes.windll.kernel32.SetDllDirectoryW(str(sys._MEIPASS))
        return subprocess.Popen(command, **kwargs)
