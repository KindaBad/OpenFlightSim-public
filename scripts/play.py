#!/usr/bin/env python3
"""Open the launcher from a source checkout, preparing whatever is missing.

Standard library only, so the system Python can run it. It creates the private
launcher environment, builds the simulator when no build exists, and starts the
launcher. Nothing is installed system-wide and administrator rights are not used.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from launcher.game import Installation
from launcher.shortcuts import install as install_shortcut
from launcher.storage import LauncherError

WINDOWS = os.name == 'nt'
VENV = ROOT / '.cache' / 'launcher-venv'
FEDORA = ('sudo dnf install gcc-c++ cmake ninja-build git python3 libX11-devel libXext-devel '
          'libXcursor-devel libXi-devel libXrandr-devel libXfixes-devel libglvnd-devel '
          'openssl-devel protobuf-devel protobuf-compiler')
WINDOWS_TOOLS = ('Install Visual Studio 2022 Build Tools (Desktop development with C++), CMake and Git,\n'
                 'then vcpkg with:  vcpkg install openssl:x64-windows protobuf:x64-windows\n'
                 'and set VCPKG_ROOT to the vcpkg folder. Details: docs/BUILDING.md')


class Setup(Exception):
    pass


def say(text):
    print(f'[OpenFlightSim] {text}', flush=True)


def run(command, **kwargs):
    try:
        subprocess.run([str(part) for part in command], cwd=ROOT, check=True, **kwargs)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise Setup(f'{Path(str(command[0])).name} failed: {exc}') from exc


def venv_python(windowed=False):
    if WINDOWS:
        return VENV / 'Scripts' / ('pythonw.exe' if windowed else 'python.exe')
    return VENV / 'bin' / 'python'


def has_qt():
    python = venv_python()
    return python.is_file() and subprocess.run([str(python), '-c', 'import PySide6.QtWidgets'],
                                               capture_output=True).returncode == 0


def prepare_environment():
    if has_qt():
        return
    say('Preparing the launcher interface (first run only)…')
    if not venv_python().is_file():
        run([sys.executable, '-m', 'venv', VENV])
    # Only the interface toolkit is needed to run; packaging tools stay optional.
    pins = [line.strip() for line in (ROOT / 'launcher/requirements.txt').read_text().splitlines()
            if line.strip().lower().startswith('pyside6')]
    run([venv_python(), '-m', 'pip', 'install', '--disable-pip-version-check', *pins])
    if not has_qt():
        raise Setup('The launcher interface could not be installed. Check the internet connection and retry.')


def simulator_ready():
    try:
        Installation.discover(ROOT)
        return True
    except (LauncherError, OSError):
        return False


def stale(build):
    """A build tree configured before the checkout moved cannot be reused."""
    cache = build / 'CMakeCache.txt'
    if not cache.is_file():
        return False
    for line in cache.read_text(errors='replace').splitlines():
        if line.startswith('CMAKE_HOME_DIRECTORY:'):
            return Path(line.split('=', 1)[1]).resolve() != ROOT
    return False


def build_simulator():
    if not shutil.which('cmake'):
        raise Setup('CMake is required to build the simulator.\n' + (WINDOWS_TOOLS if WINDOWS else FEDORA))
    say('Building the simulator. The first build takes several minutes…')
    if WINDOWS and not shutil.which('cl'):
        # Outside a developer prompt, the Visual Studio generator finds MSVC itself.
        build = ROOT / 'build' / 'msvc'
        configure = ['cmake', '-S', ROOT, '-B', build, '-G', 'Visual Studio 17 2022', '-A', 'x64']
        vcpkg = os.environ.get('VCPKG_ROOT') or os.environ.get('VCPKG_INSTALLATION_ROOT')
        if vcpkg:
            configure.append('-DCMAKE_TOOLCHAIN_FILE=' + str(Path(vcpkg) / 'scripts/buildsystems/vcpkg.cmake'))
        compile_step = ['cmake', '--build', build, '--config', 'Release', '--parallel', '3']
    else:
        build = ROOT / 'build' / 'release'
        configure = ['cmake', '--preset', 'release']
        compile_step = ['cmake', '--build', '--preset', 'release']
    if stale(build):
        configure.append('--fresh')
    try:
        run(configure)
        run(compile_step)
    except Setup as exc:
        raise Setup(f'{exc}\nThe simulator build needs these tools and libraries:\n'
                    + (WINDOWS_TOOLS if WINDOWS else FEDORA)) from exc
    if not simulator_ready():
        raise Setup('The build finished without a usable simulator. See docs/BUILDING.md.')


def main():
    parser = argparse.ArgumentParser(description='Open the OpenFlightSim launcher from this checkout')
    parser.add_argument('--shortcut', action='store_true', help='also add OpenFlightSim to the application menu')
    parser.add_argument('--rebuild', action='store_true', help='build the simulator even when a build exists')
    parser.add_argument('--no-launch', action='store_true', help='prepare everything, then exit')
    args, passthrough = parser.parse_known_args()
    try:
        prepare_environment()
        if args.rebuild or not simulator_ready():
            build_simulator()
    except Setup as exc:
        say(str(exc))
        return 1
    if args.shortcut:
        script = ROOT / ('play.bat' if WINDOWS else 'play.sh')
        entry = install_shortcut(script, icon=ROOT / 'data/launcher/icon.svg')
        say(f'Added to the application menu: {entry}' if entry else 'Could not add an application-menu entry.')
    if args.no_launch:
        say('Ready.')
        return 0
    command = [str(venv_python(windowed=True)), '-m', 'launcher.app', '--installation', str(ROOT), *passthrough]
    if WINDOWS:
        # Let the console that ran play.bat close while the launcher stays open.
        subprocess.Popen(command, cwd=ROOT, creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP)
        return 0
    os.chdir(ROOT)
    os.execv(command[0], command)


if __name__ == '__main__':
    raise SystemExit(main())
