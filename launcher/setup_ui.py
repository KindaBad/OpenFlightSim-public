"""Single-file download -> Install -> Open launcher, with no developer tools."""
import argparse
import logging
from pathlib import Path
import sys

from PySide6.QtCore import QTimer, Signal
from PySide6.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLineEdit, QProgressBar,
    QFileDialog, QMessageBox, QFrame)

from .config import user_directory
from .diagnostics import configure_logs
from .platform_process import spawn
from .presentation import STYLE, Banner, icon
from .setup import bootstrap_name, configuration, default_directory, install
from .shortcuts import install as install_shortcut
from .storage import read_json, write_json
from .ui import Job, button, label


def free_space(folder):
    """Free bytes on the disk that would hold `folder`, which need not exist yet."""
    import shutil
    try:
        path = Path(folder).expanduser().resolve()
        while not path.exists() and path != path.parent:
            path = path.parent
        return shutil.disk_usage(path).free
    except (OSError, ValueError, RuntimeError):
        return None


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
        self.setWindowTitle('OpenFlightSim Setup')
        self.setWindowIcon(icon('wing', '#269cff', 64))
        self.setStyleSheet(STYLE)
        self.resize(680, 560)
        self.setMinimumSize(560, 520)
        box = QVBoxLayout(self)
        box.setContentsMargins(24, 24, 24, 20)
        box.setSpacing(16)
        box.addWidget(Banner('Real planes, real places, an open sky.'))
        card = QFrame()
        card.setObjectName('card')
        folder = QVBoxLayout(card)
        folder.setContentsMargins(18, 16, 18, 16)
        folder.setSpacing(8)
        folder.addWidget(label('Install to', 'sectionTitle'))
        row = QHBoxLayout()
        row.setSpacing(10)
        self.path = QLineEdit(str(root or default_directory(settings['channel'])))
        self.path.setAccessibleName('Game folder')
        self.path.textChanged.connect(self.show_space)
        row.addWidget(self.path, 1)
        self.browse = button('Browse…', self.choose_folder)
        self.browse.setIcon(icon('folder', '#cfe4fb', 18))
        row.addWidget(self.browse)
        folder.addLayout(row)
        self.space = label('', 'factKey')
        folder.addWidget(self.space)
        box.addWidget(card)
        self.status = label('Ready to install. No administrator access is needed.', 'muted')
        box.addWidget(self.status)
        meter = QHBoxLayout()
        meter.setSpacing(12)
        self.progress = QProgressBar()
        self.progress.setTextVisible(False)
        self.progress.hide()
        meter.addWidget(self.progress, 1)
        self.percent = label('', 'percent', False)
        self.percent.hide()
        meter.addWidget(self.percent)
        box.addLayout(meter)
        box.addStretch()
        actions = QHBoxLayout()
        actions.setSpacing(10)
        self.cancel = button('Cancel', self.cancel_install)
        self.cancel.setToolTip('Stop safely. What has been downloaded is kept, so you can resume.')
        self.cancel.hide()
        actions.addWidget(self.cancel)
        self.action = button('Install', self.start)
        self.action.setObjectName('play')
        self.action.setIcon(icon('download', '#ffffff', 22))
        actions.addWidget(self.action, 1)
        box.addLayout(actions)
        box.addWidget(label(f'{settings["channel"].capitalize()} channel · version {settings["version"]} · an internet connection is needed to install', 'factKey'))
        self.show_space()
        if (Path(self.path.text()) / 'current.json').is_file():
            self.action.setText('Open launcher')
            self.action.setIcon(icon('play', '#ffffff', 22))
            self.status.setText('The game is already installed here. Open the launcher to play.')

    def show_status(self, text, kind='muted'):
        self.status.setText(text)
        if self.status.objectName() != kind:
            self.status.setObjectName(kind)
            self.status.style().unpolish(self.status)
            self.status.style().polish(self.status)

    def show_space(self):
        free = free_space(self.path.text()) if self.path.text().strip() else None
        self.space.setText(f'{free / 1024**3:.1f} GiB free on this disk' if free is not None else 'Choose a folder you can write to')

    def choose_folder(self):
        selected = QFileDialog.getExistingDirectory(self, 'Choose game folder', self.path.text())
        if selected:
            self.path.setText(selected)

    def start(self):
        if self.installed:
            self.open_launcher()
            return
        if not self.path.text().strip():
            self.show_status('Choose a game folder first.', 'error')
            return
        self.path.setEnabled(False)
        self.browse.setEnabled(False)
        self.action.setEnabled(False)
        self.action.setText('Installing…')
        self.show_status('Getting ready…')
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
        self.show_status(message)
        self.progress.setRange(0, 0)
        self.percent.hide()

    def download_progress(self, metric):
        done, total, speed = metric
        self.progress.setRange(0, 1000)
        self.progress.setValue(int(done * 1000 / max(1, total)))
        self.percent.setText(f'{int(done * 100 / max(1, total))}%')
        self.percent.show()
        if speed:
            seconds = max(0, int((total - done) / speed))
            remaining = f'{seconds // 60} min left' if seconds >= 60 else f'{seconds} sec left'
            self.show_status(f'Downloading: {done / 1024**2:.0f} of {total / 1024**2:.0f} MiB · {speed / 1024**2:.1f} MiB/s · {remaining}')

    def complete(self, root):
        self.installed = Path(root)
        try:
            saved = read_json(self.state) if self.state.is_file() else {}
            saved[self.settings['channel']] = str(root)
            write_json(self.state, saved)
        except (OSError, ValueError, TypeError):
            logging.getLogger('ofs.setup').warning('Could not save setup folder')
        install_shortcut(self.installed / bootstrap_name(), state=self.data)
        self.show_status('Ready to fly. Opening the launcher…', 'success')

    def failed(self, message):
        self.show_status(message, 'error')
        self.action.setText('Retry installation')

    def finished(self):
        self.job.deleteLater()
        self.job = None
        self.progress.hide()
        self.percent.hide()
        self.cancel.hide()
        self.path.setEnabled(not bool(self.installed))
        self.browse.setEnabled(not bool(self.installed))
        self.action.setEnabled(True)
        if self.action.text() == 'Installing…':
            self.action.setText('Install')
        if self.pending_close:
            self.close()
        elif self.installed:
            self.action.setText('Open launcher')
            self.action.setIcon(icon('play', '#ffffff', 22))
            self.open_launcher()

    def open_launcher(self):
        try:
            spawn([str(self.installed / bootstrap_name()), '--installation', str(self.installed), '--user-data', str(self.data)], cwd=self.installed)
            self.close()
        except OSError as exc:
            self.show_status(f'Could not open the launcher: {exc}. Retry or open it from the Start Menu.', 'error')

    def cancel_install(self):
        if self.job:
            self.job.cancel.set()
            self.show_status('Cancelling… Your download can be resumed.')

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
