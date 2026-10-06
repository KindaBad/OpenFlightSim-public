import argparse
import logging
from pathlib import Path
import sys
from .config import user_directory
from .diagnostics import configure_logs
from .storage import LauncherError, read_json


def launcher_build():
    if getattr(sys, 'frozen', False):
        return read_json(Path(sys._MEIPASS) / 'build-info.json')
    # Development launches use the same CMake version source.
    import re
    source = Path(__file__).resolve().parent.parent
    version = re.search(r'project\(OpenFlightSim VERSION ([0-9.]+)', (source / 'CMakeLists.txt').read_text())[1]
    return {'version': version, 'commit': 'source', 'channel': 'development'}


def main():
    parser = argparse.ArgumentParser(description='OpenFlightSim Launcher')
    parser.add_argument('--installation', type=Path)
    parser.add_argument('--screenshot', type=Path, help='Capture the launcher UI, then exit (development verification)')
    parser.add_argument('--user-data', type=Path, help='Use an isolated preference/log directory')
    args = parser.parse_args()
    data = (args.user_data or user_directory()).expanduser().resolve()
    data.mkdir(parents=True, exist_ok=True)
    logs = configure_logs(data / 'logs')
    logging.getLogger('ofs.app').info('Launcher %s started', launcher_build()['version'])
    from PySide6.QtCore import QTimer
    from PySide6.QtWidgets import QApplication, QMessageBox
    from .ui import Window
    application = QApplication(sys.argv)
    application.setApplicationName('OpenFlightSim Launcher')
    application.setOrganizationName('OpenFlightSim')
    # Lets the desktop match this window to the application-menu entry and icon.
    application.setDesktopFileName('openflightsim')
    try:
        window = Window(args.installation, data, logs, launcher_build())
    except (LauncherError, OSError) as exc:
        QMessageBox.critical(None, 'OpenFlightSim — Startup', str(exc))
        return 1
    window.show()
    if args.screenshot:
        def capture():
            args.screenshot.parent.mkdir(parents=True, exist_ok=True)
            window.grab().save(str(args.screenshot))
            window.close()
        QTimer.singleShot(1500, capture)
    return application.exec()


if __name__ == '__main__':
    raise SystemExit(main())
