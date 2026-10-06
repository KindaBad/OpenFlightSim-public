"""Single-file download -> Install -> Open launcher, with no developer tools."""
import argparse
import logging
from pathlib import Path
import sys

from PySide6.QtCore import QTimer, Signal
from PySide6.QtWidgets import QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLineEdit, QProgressBar, QFileDialog, QMessageBox

from .config import user_directory
from .diagnostics import configure_logs
from .platform_process import spawn
from .setup import bootstrap_name, configuration, default_directory, install
from .shortcuts import install as install_shortcut
from .storage import read_json, write_json
from .ui import STYLE, FlightArt, Job, button, label


class InstallJob(Job):
    phase = Signal(str)

    def __init__(self, root, settings, bootstrap):
        super().__init__(lambda cancel, progress: install(root, settings, bootstrap, cancel, progress, self.phase.emit))


class SetupWindow(QWidget):
    def __init__(self, settings, bootstrap, data, root=None):
        super().__init__()
        self.settings, self.bootstrap, self.data = configuration(settings), Path(bootstrap), Path(data)
        self.job = None
        self.installed = None
        self.pending_close = False
        self.state = self.data / 'setup.json'
        if root is None and self.state.is_file():
            try:
                root = read_json(self.state).get(settings['channel'])
            except (ValueError, OSError):
                pass
        self.setWindowTitle('OpenFlightSim')
        self.setStyleSheet(STYLE)
        self.resize(660, 560)
        box = QVBoxLayout(self)
        box.setContentsMargins(28, 24, 28, 24)
        box.addWidget(FlightArt(compact=True))
        box.addWidget(label('Your next flight starts here', 'title'))
        box.addWidget(label('Install the game, then choose your aircraft and press PLAY.'))
        box.addWidget(label('Game folder', 'muted'))
        row = QHBoxLayout()
        self.path = QLineEdit(str(root or default_directory(settings['channel'])))
        row.addWidget(self.path, 1)
        self.browse = button('Browse…', self.choose_folder)
        row.addWidget(self.browse)
        box.addLayout(row)
        self.status = label('Ready to install. No administrator access is needed.', 'muted')
        box.addWidget(self.status)
        self.progress = QProgressBar()
        self.progress.hide()
        box.addWidget(self.progress)
        self.action = button('INSTALL GAME', self.start)
        self.action.setObjectName('play')
        box.addWidget(self.action)
        self.cancel = button('Cancel', self.cancel_install)
        self.cancel.hide()
        box.addWidget(self.cancel)
        box.addWidget(label(f'{settings["channel"].capitalize()} channel · Windows / Linux x64 · Internet required for installation', 'muted'))
        if (Path(self.path.text()) / 'current.json').is_file():
            self.action.setText('OPEN LAUNCHER')
            self.status.setText('Game already installed. Open the launcher to play.')

    def choose_folder(self):
        selected = QFileDialog.getExistingDirectory(self, 'Choose game folder', self.path.text())
        if selected:
            self.path.setText(selected)

    def start(self):
        if self.installed:
            self.open_launcher()
            return
        if not self.path.text().strip():
            self.status.setText('Choose a game folder first.')
            return
        self.path.setEnabled(False)
        self.browse.setEnabled(False)
        self.action.setEnabled(False)
        self.progress.setRange(0, 0)
        self.progress.show()
        self.cancel.show()
        self.job = InstallJob(self.path.text(), self.settings, self.bootstrap)
        self.job.phase.connect(self.phase)
        self.job.metric.connect(self.download_progress)
        self.job.result.connect(self.complete)
        self.job.failed.connect(self.failed)
        self.job.finished.connect(self.finished)
        self.job.start()

    def phase(self, message):
        self.status.setText(message)
        self.progress.setRange(0, 0)

    def download_progress(self, metric):
        done, total, speed = metric
        self.progress.setRange(0, 1000)
        self.progress.setValue(int(done * 1000 / max(1, total)))
        if speed:
            self.status.setText(f'Downloading game: {done / 1024**2:.0f} / {total / 1024**2:.0f} MiB · {speed / 1024**2:.1f} MiB/s')

    def complete(self, root):
        self.installed = Path(root)
        try:
            saved = read_json(self.state) if self.state.is_file() else {}
            saved[self.settings['channel']] = str(root)
            write_json(self.state, saved)
        except (OSError, ValueError, TypeError):
            logging.getLogger('ofs.setup').warning('Could not save setup folder')
        install_shortcut(self.installed / bootstrap_name(), state=self.data)
        self.status.setText('Ready to fly. Opening the launcher…')

    def failed(self, message):
        self.status.setText(message)
        self.action.setText('RETRY INSTALLATION')

    def finished(self):
        self.job.deleteLater()
        self.job = None
        self.progress.hide()
        self.cancel.hide()
        self.path.setEnabled(not bool(self.installed))
        self.browse.setEnabled(not bool(self.installed))
        self.action.setEnabled(True)
        if self.pending_close:
            self.close()
        elif self.installed:
            self.action.setText('OPEN LAUNCHER')
            self.open_launcher()

    def open_launcher(self):
        try:
            spawn([str(self.installed / bootstrap_name()), '--installation', str(self.installed), '--user-data', str(self.data)], cwd=self.installed)
            self.close()
        except OSError as exc:
            self.status.setText(f'Could not open the launcher: {exc}. Retry or open it from the Start Menu.')

    def cancel_install(self):
        if self.job:
            self.job.cancel.set()
            self.status.setText('Cancelling… Your download can be resumed.')

    def closeEvent(self, event):
        if self.job:
            self.pending_close = True
            self.cancel_install()
            event.ignore()
        else:
            event.accept()


def main():
    parser = argparse.ArgumentParser(description='OpenFlightSim standalone installer')
    parser.add_argument('--installation', type=Path)
    parser.add_argument('--user-data', type=Path)
    parser.add_argument('--screenshot', type=Path)
    # Only development runs accept a config path. Distributed setup is self-contained.
    if not getattr(sys, 'frozen', False):
        parser.add_argument('--config', type=Path, required=True)
        parser.add_argument('--bootstrap', type=Path, required=True)
    args = parser.parse_args()
    data = (args.user_data or user_directory()).expanduser().resolve()
    configure_logs(data / 'logs', name='setup')
    app = QApplication(sys.argv)
    app.setApplicationName('OpenFlightSim')
    try:
        bundle = Path(sys._MEIPASS) if getattr(sys, 'frozen', False) else None
        settings = read_json(bundle / 'setup-config.json' if bundle else args.config)
        bootstrap = bundle / 'bootstrap' / bootstrap_name() if bundle else args.bootstrap
        window = SetupWindow(settings, bootstrap, data, args.installation)
        window.show()
        if args.screenshot:
            def capture():
                args.screenshot.parent.mkdir(parents=True, exist_ok=True)
                window.grab().save(str(args.screenshot))
                window.close()
            QTimer.singleShot(1200, capture)
        return app.exec()
    except (OSError, ValueError) as exc:
        QMessageBox.critical(None, 'OpenFlightSim', f'Could not start installation: {exc}')
        return 1
