"""Native aviation-styled Qt Widgets UI. Long operations run off the UI thread."""
import logging
import os
from pathlib import Path
import subprocess
import sys
import threading
import uuid

from PySide6.QtCore import Qt, QThread, Signal, QTimer, QUrl, QPointF
from PySide6.QtGui import QColor, QPainter, QPainterPath, QLinearGradient, QFont, QDesktopServices, QPixmap, QIcon
from PySide6.QtWidgets import (QMainWindow, QWidget, QHBoxLayout, QVBoxLayout, QLabel,
    QPushButton, QListWidget, QStackedWidget, QFrame, QFormLayout, QComboBox,
    QCheckBox, QSpinBox, QDoubleSpinBox, QLineEdit, QProgressBar, QMessageBox,
    QFileDialog, QScrollArea, QPlainTextEdit)

from .config import Preferences, Graphics, GRAPHICS, PRESETS
from .download import download, fetch_manifest, Cancelled
from .game import Installation, Session, MODES, arguments
from .hardware import probe, recommendation, display_modes
from .installation import Lease, verify, repair_stage, recover, pointer
from .manifest import select_release, https_url
from .platform_process import spawn
from .storage import LauncherError, read_json, write_json, safe_path
from .version import Version

log = logging.getLogger('ofs.ui')

STYLE = """
QWidget { background: #101b2a; color: #e9f0f7; font-family: 'Segoe UI', 'DejaVu Sans'; font-size: 14px; }
QMainWindow { background: #101b2a; }
QLabel { background: transparent; }
QLabel#eyebrow { color: #64c7ee; font-size: 11px; font-weight: 600; letter-spacing: 2px; }
QLabel#title { font-size: 30px; font-weight: 600; }
QLabel#muted { color: #9cafc3; }
QLabel#brand { font-size: 21px; font-weight: 700; }
QFrame#sidebar { background: #0b1420; border-right: 1px solid #27374a; }
QFrame#card { background: #182638; border: 1px solid #2b4055; border-radius: 12px; }
QListWidget { background: transparent; border: none; outline: none; padding: 6px; }
QListWidget::item { padding: 12px 16px; color: #a4b6ca; border-radius: 7px; margin-bottom: 3px; }
QListWidget::item:hover { background: #192d40; color: white; }
QListWidget::item:selected { background: #213b50; color: #7ed9ff; border-left: 3px solid #55c8f2; }
QPushButton { background: #23384c; border: 1px solid #35526a; border-radius: 7px; padding: 10px 18px; font-weight: 600; }
QPushButton:hover { background: #304b64; border-color: #66cbed; }
QPushButton:pressed { background: #172c3f; }
QPushButton:disabled { color: #667c90; background: #1a2939; border-color: #26394b; }
QPushButton#play { background: #69d3f4; color: #092032; border: none; font-size: 23px; padding: 17px 40px; }
QPushButton#play:hover { background: #9ae6ff; }
QPushButton#play:disabled { background: #294357; color: #7d98ad; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QPlainTextEdit { background: #0c1724; border: 1px solid #32475d; border-radius: 6px; padding: 8px; selection-background-color: #28617f; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #69d3f4; }
QComboBox QAbstractItemView { background: #152b3e; selection-background-color: #2c536b; }
QCheckBox { spacing: 10px; padding: 5px; }
QCheckBox::indicator { width: 17px; height: 17px; border: 1px solid #4a6b83; border-radius: 4px; background: #0b1623; }
QCheckBox::indicator:checked { background: #69d3f4; border: 2px solid #a8e8ff; }
QProgressBar { background: #0b1724; border: 1px solid #2d4256; border-radius: 5px; height: 12px; text-align: center; }
QProgressBar::chunk { background: #65d0ef; border-radius: 4px; }
QScrollArea { border: none; }
QToolTip { background: #23394c; color: #eefaff; border: 1px solid #6dcdef; padding: 8px; }
"""


class FlightArt(QWidget):
    """Small procedural vector art; optional aircraft thumbnail takes precedence."""
    def __init__(self, compact=False):
        super().__init__()
        self.compact = compact
        self.thumbnail = None
        self.setMinimumHeight(135 if compact else 210)
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, False)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        gradient = QLinearGradient(0, 0, w, h)
        gradient.setColorAt(0, QColor('#1b4f68'))
        gradient.setColorAt(.6, QColor('#1a354e'))
        gradient.setColorAt(1, QColor('#142234'))
        p.fillRect(self.rect(), gradient)
        p.setPen(QColor('#2d5369'))
        for i in range(-4, 14):
            p.drawLine(QPointF(i * 80, h), QPointF(i * 80 + 150, 0))
        p.setPen(QColor('#50798b'))
        p.drawLine(0, int(h * .73), w, int(h * .73))
        p.setPen(QColor('#6fa1b7'))
        p.setFont(QFont('DejaVu Sans', 9))
        p.drawText(22, 27, 'OPENFLIGHTSIM   /   FLIGHT OPERATIONS')
        if self.thumbnail and not self.thumbnail.isNull():
            image = self.thumbnail.scaled(self.size(), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
            p.drawPixmap((w - image.width()) // 2, (h - image.height()) // 2, image)
        else:
            p.save()
            p.translate(w * .64, h * .47)
            p.rotate(-18)
            scale = min(w / 400, h / 130)
            p.scale(scale, scale)
            path = QPainterPath()
            points = [(0, -42), (5, -30), (6, -8), (65, 22), (65, 30), (5, 15),
                      (4, 40), (22, 52), (22, 58), (0, 50), (-22, 58), (-22, 52),
                      (-4, 40), (-5, 15), (-65, 30), (-65, 22), (-6, -8), (-5, -30)]
            path.moveTo(*points[0])
            for point in points[1:]:
                path.lineTo(*point)
            path.closeSubpath()
            p.setPen(QColor('#acd4e6'))
            p.setBrush(QColor('#76a9c1'))
            p.drawPath(path)
            p.restore()
            p.setPen(QColor('#80a8ba'))
            p.drawText(22, h - 17, 'ILLUSTRATIVE AIRCRAFT SILHOUETTE')
        p.end()


class Job(QThread):
    metric = Signal(object)
    result = Signal(object)
    failed = Signal(str)

    def __init__(self, function):
        super().__init__()
        self.function = function
        self.cancel = threading.Event()

    def report(self, done, total, speed):
        self.metric.emit((done, total, speed))

    def run(self):
        try:
            self.result.emit(self.function(self.cancel, self.report))
        except Exception as exc:
            log.error('Operation failed: %s', exc)
            # Network exception text can include a signed CDN URL. Keep it out of dialogs.
            import re
            self.failed.emit(re.sub(r'https?://[^\s]+', '[HTTPS endpoint]', str(exc)))


def label(text, name=None, wrap=True):
    widget = QLabel(text)
    if name:
        widget.setObjectName(name)
    widget.setWordWrap(wrap)
    widget.setTextFormat(Qt.TextFormat.PlainText)
    return widget


def button(text, callback):
    widget = QPushButton(text)
    widget.clicked.connect(callback)
    return widget


class Window(QMainWindow):
    def __init__(self, installation, data, logs, build):
        super().__init__()
        self.data, self.logs, self.build = Path(data).resolve(), Path(logs).resolve(), build
        self.preferences_path = self.data / 'launcher.json'
        self.initial_error = None
        try:
            self.prefs = Preferences.load(self.preferences_path)
        except (LauncherError, OSError) as exc:
            # Preserve the invalid file for diagnosis, and permit correction in UI.
            self.prefs = Preferences()
            self.initial_error = str(exc)
            if self.preferences_path.is_file():
                import shutil
                backup = self.data / ('launcher.invalid-' + uuid.uuid4().hex[:8] + '.json')
                shutil.copy2(self.preferences_path, backup)
                self.initial_error += f' · original saved to {backup}'
        self.graphics_error = None
        try:
            self.graphics = Graphics(self.data / 'graphics.cfg')
        except (LauncherError, OSError) as exc:
            self.graphics_error = str(exc)
            self.graphics = Graphics(self.data / 'graphics.cfg', load=False)
        self.session = Session()
        self.lease = None
        self.installation = None
        self.release = None
        self.job = None
        self.updating = True
        self.pending_close = False
        self.setWindowTitle('OpenFlightSim Launcher')
        self.resize(1100, 790)
        self.setMinimumSize(880, 680)
        self.setStyleSheet(STYLE)
        self.graphic_widgets = {}
        self.status_buttons = []
        self.make_ui()
        root = installation or self.prefs.installation or Path(__file__).resolve().parent.parent
        self.set_installation(root)
        self.updating = False
        self.poll = QTimer(self)
        self.poll.timeout.connect(self.poll_session)
        self.poll.start(500)
        QTimer.singleShot(0, self.start_background)

    def make_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QHBoxLayout(central)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        sidebar = QFrame()
        sidebar.setObjectName('sidebar')
        sidebar.setFixedWidth(222)
        side = QVBoxLayout(sidebar)
        side.setContentsMargins(16, 28, 14, 22)
        side.addWidget(label('OFS  /  FLIGHT', 'brand'))
        side.addWidget(label('SIMULATOR LAUNCHER', 'eyebrow'))
        side.addSpacing(25)
        self.navigation = QListWidget()
        self.navigation.addItems(['Play', 'Aircraft', 'Flight Mode', 'Graphics', 'Display', 'Controls / Input', 'Multiplayer', 'Advanced', 'Updates', 'Installation / Repair'])
        side.addWidget(self.navigation)
        side.addWidget(label('YOUR NEXT FLIGHT\nSTARTS HERE', 'muted'))
        side.addSpacing(14)
        side.addWidget(label(f'Launcher {self.build["version"]}', 'muted'))
        layout.addWidget(sidebar)
        main = QWidget()
        outer = QVBoxLayout(main)
        outer.setContentsMargins(32, 28, 32, 20)
        self.stack = QStackedWidget()
        outer.addWidget(self.stack, 1)
        self.status = label('Preparing flight operations…', 'muted')
        outer.addWidget(self.status)
        self.progress = QProgressBar()
        self.progress.hide()
        outer.addWidget(self.progress)
        self.cancel_button = button('Cancel operation', self.cancel_job)
        self.cancel_button.hide()
        outer.addWidget(self.cancel_button)
        layout.addWidget(main, 1)
        self.make_play()
        self.make_aircraft()
        self.make_mode()
        self.make_graphics()
        self.make_display()
        self.make_controls()
        self.make_multiplayer()
        self.make_advanced()
        self.make_updates()
        self.make_installation()
        self.navigation.currentRowChanged.connect(self.stack.setCurrentIndex)
        self.navigation.setCurrentRow(0)

    def page(self, eyebrow, title, description):
        content = QWidget()
        box = QVBoxLayout(content)
        box.setContentsMargins(0, 0, 8, 0)
        box.setSpacing(16)
        box.addWidget(label(eyebrow.upper(), 'eyebrow'))
        box.addWidget(label(title, 'title'))
        box.addWidget(label(description, 'muted'))
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(content)
        self.stack.addWidget(scroll)
        return box

    def form(self, box):
        card = QFrame()
        card.setObjectName('card')
        form = QFormLayout(card)
        form.setContentsMargins(20, 20, 20, 20)
        form.setVerticalSpacing(12)
        form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.ExpandingFieldsGrow)
        box.addWidget(card)
        return form

    def combo(self, items, value, callback):
        widget = QComboBox()
        for key, text in items:
            widget.addItem(text, key)
        index = widget.findData(value)
        if index < 0 and value is not None:
            widget.addItem(f'Custom ({value})', value)
            index = widget.findData(value)
        widget.setCurrentIndex(max(0, index))
        widget.currentIndexChanged.connect(lambda _: callback(widget.currentData()))
        return widget

    def set_pref(self, key, value):
        if self.updating:
            return
        setattr(self.prefs, key, value)
        self.save()
        self.refresh_summary()

    def save(self):
        if self.updating:
            return
        try:
            self.prefs.save(self.preferences_path)
        except OSError as exc:
            self.error(str(exc))

    def make_play(self):
        box = self.page('Flight operations', 'Ready for departure', 'Configure your aircraft and flight, then take to the skies.')
        self.art = FlightArt()
        box.addWidget(self.art)
        card = QFrame()
        card.setObjectName('card')
        summary = QVBoxLayout(card)
        summary.setContentsMargins(22, 19, 22, 22)
        self.version_label = label('OpenFlightSim', 'eyebrow')
        self.aircraft_label = label('Select an installation', 'title')
        self.summary_label = label('', 'muted')
        self.update_label = label('Updates: publisher endpoint has not been configured', 'muted')
        for widget in (self.version_label, self.aircraft_label, self.summary_label, self.update_label):
            summary.addWidget(widget)
        box.addWidget(card)
        self.play_button = button('PLAY   →', self.play)
        self.play_button.setObjectName('play')
        box.addWidget(self.play_button)
        self.update_button = button('Update now', self.install_update)
        self.update_button.hide()
        box.addWidget(self.update_button)
        self.stop_button = button('End current flight', self.stop_flight)
        self.stop_button.hide()
        box.addWidget(self.stop_button)
        box.addWidget(label('Settings are saved automatically. Simulator logs are available in Installation / Repair.', 'muted'))
        box.addStretch()

    def make_aircraft(self):
        box = self.page('Hangar', 'Choose your aircraft', 'Aircraft are discovered from this simulator build’s compiled registry.')
        self.aircraft_combo = QComboBox()
        self.aircraft_combo.currentIndexChanged.connect(self.select_aircraft)
        box.addWidget(self.aircraft_combo)
        self.aircraft_art = FlightArt(compact=True)
        box.addWidget(self.aircraft_art)
        self.aircraft_detail = label('', 'muted')
        box.addWidget(self.aircraft_detail)
        box.addWidget(label('Specifications below are simulator configuration values. Aircraft presentation metadata is optional.', 'muted'))
        box.addStretch()

    def make_mode(self):
        box = self.page('Mission planning', 'Flight mode', 'Choose a mode supported by the installed simulator.')
        form = self.form(box)
        self.mode_combo = QComboBox()
        self.mode_combo.currentIndexChanged.connect(self.select_mode)
        form.addRow('Flight mode', self.mode_combo)
        self.airborne = QCheckBox('Start airborne in trimmed flight')
        self.airborne.setChecked(self.prefs.airborne)
        self.airborne.toggled.connect(lambda v: self.set_pref('airborne', v))
        form.addRow('Initial state', self.airborne)
        self.bots = QSpinBox()
        self.bots.setRange(1, 8)
        self.bots.setValue(self.prefs.bots)
        self.bots.valueChanged.connect(lambda v: self.set_pref('bots', v))
        form.addRow('Dogfight opponents', self.bots)
        self.mode_detail = label('', 'muted')
        box.addWidget(self.mode_detail)
        box.addStretch()

    def graphic(self, form, key, title, choices=None, tip=''):
        default, low, high = GRAPHICS[key]
        value = float(self.graphics.values[key])
        if choices:
            widget = self.combo(choices, int(value), lambda v: self.graphic_change(key, v))
        elif low == 0 and high == 1 and isinstance(default, int):
            widget = QCheckBox('Enabled')
            widget.setChecked(bool(value))
            widget.toggled.connect(lambda v: self.graphic_change(key, int(v)))
        else:
            widget = QDoubleSpinBox() if isinstance(default, float) else QSpinBox()
            widget.setRange(low, high)
            if isinstance(default, float):
                widget.setDecimals(2)
                widget.setSingleStep(.1)
            widget.setValue(value if isinstance(default, float) else int(value))
            widget.valueChanged.connect(lambda v: self.graphic_change(key, v))
        widget.setToolTip(tip or 'Saved to the simulator graphics.cfg. Takes effect on the next flight.')
        form.addRow(title, widget)
        self.graphic_widgets[key] = widget

    def graphic_change(self, key, value):
        if self.updating:
            return
        self.graphics.values[key] = str(value)
        self.prefs.preset = 'Custom'
        self.preset_combo.blockSignals(True)
        self.preset_combo.setCurrentText('Custom')
        self.preset_combo.blockSignals(False)
        self.graphics_error = None
        try:
            self.graphics.save()
            self.save()
            self.refresh_summary()
        except (LauncherError, OSError) as exc:
            self.error(str(exc))

    def make_graphics(self):
        box = self.page('Visual systems', 'Graphics quality', 'Presets tune the existing renderer. Texture and sampling changes apply on the next launch.')
        form = self.form(box)
        self.preset_combo = QComboBox()
        self.preset_combo.addItems(['Auto / Recommended', *PRESETS, 'Custom'])
        self.preset_combo.setCurrentText(self.prefs.preset if self.prefs.preset in (*PRESETS, 'Auto / Recommended') else 'Custom')
        self.preset_combo.currentTextChanged.connect(self.apply_preset)
        form.addRow('Quality preset', self.preset_combo)
        self.recommendation_label = label('Detecting hardware…', 'muted')
        form.addRow('Recommendation', self.recommendation_label)
        levels = list(enumerate(['Off', 'Low', 'Medium', 'High']))
        self.graphic(form, 'msaa', 'Anti-aliasing', [(n, 'Off' if n == 1 else f'{n}× MSAA') for n in (1, 2, 4, 8, 16)])
        self.graphic(form, 'textureMaxSize', 'Texture limit', [(n, f'{n} px') for n in (512, 1024, 2048, 4096, 8192)])
        for key, title in [('shadows', 'Shadows'), ('effects', 'Effects / particles'), ('clouds', 'Clouds')]:
            self.graphic(form, key, title, levels)
        for key, title in [('anisotropic', 'Anisotropic filtering'), ('bloom', 'Bloom'), ('cloudShadows', 'Cloud shadows'), ('vegetation', 'Vegetation')]:
            self.graphic(form, key, title)
        box.addStretch()

    def apply_preset(self, name):
        if self.updating or name == 'Custom':
            return
        chosen = name
        if name == 'Auto / Recommended':
            chosen, reason = recommendation(self.hardware) if hasattr(self, 'hardware') else ('Medium', 'Hardware detection is pending; using conservative Medium.')
            self.recommendation_label.setText(f'{chosen}: {reason}')
        self.graphics.preset(chosen)
        self.prefs.preset = name
        self.graphics_error = None
        self.sync_graphics()
        try:
            self.graphics.save()
            self.save()
            self.refresh_summary()
        except (LauncherError, OSError) as exc:
            self.error(str(exc))

    def sync_graphics(self):
        before = self.updating
        self.updating = True
        for key, widget in self.graphic_widgets.items():
            value = float(self.graphics.values[key])
            if isinstance(widget, QCheckBox):
                widget.setChecked(bool(value))
            elif isinstance(widget, QComboBox):
                widget.setCurrentIndex(widget.findData(int(value)))
            else:
                widget.setValue(value if isinstance(widget, QDoubleSpinBox) else int(value))
        self.updating = before

    def make_display(self):
        box = self.page('Cockpit view', 'Display', 'SDL fullscreen uses the desktop mode. Window dimensions apply in windowed mode.')
        form = self.form(box)
        self.resolution = QComboBox()
        self.resolution.currentIndexChanged.connect(self.change_resolution)
        form.addRow('Detected resolutions', self.resolution)
        self.graphic(form, 'width', 'Window width')
        self.graphic(form, 'height', 'Window height')
        self.graphic(form, 'fullscreen', 'Desktop fullscreen')
        self.graphic(form, 'vsync', 'VSync')
        self.graphic(form, 'cockpitFov', 'Cockpit vertical FOV', tip='Vertical field of view in degrees, 40–100. This affects the cockpit camera only.')
        self.display_detail = label('', 'muted')
        box.addWidget(self.display_detail)
        box.addWidget(label('Refresh rate follows the desktop. Exclusive fullscreen, a frame cap and render scaling are not implemented by the current simulator.', 'muted'))
        box.addStretch()

    def change_resolution(self):
        if self.updating or not self.resolution.currentData():
            return
        width, height = self.resolution.currentData()
        self.graphics.values.update(width=str(width), height=str(height))
        self.sync_graphics()
        self.graphic_change('width', width)

    def make_controls(self):
        box = self.page('Flight deck', 'Controls / Input', 'The simulator detects SDL gamepads automatically. These are the current built-in bindings.')
        form = self.form(box)
        for title, text in [('Pitch / roll', 'W / S · A / D'), ('Rudder', 'Q / E'), ('Throttle', 'Shift / Ctrl'),
                            ('Camera', 'Tab cycles camera · right mouse looks · wheel zooms'),
                            ('Weapons', 'Space / left mouse / gamepad right trigger'),
                            ('Missiles', 'Use the in-flight combat HUD controls'), ('Gamepad', 'SDL compatible gamepads are discovered in flight')]:
            form.addRow(title, label(text, 'muted'))
        box.addWidget(label('Binding remapping and sensitivity profiles are not yet supported by the simulator.', 'muted'))
        box.addStretch()

    def make_multiplayer(self):
        box = self.page('Flight network', 'Multiplayer', 'Direct connect uses GameNetworkingSockets. Configure your flight mode before connecting.')
        form = self.form(box)
        self.server_field = QLineEdit(self.prefs.server)
        self.server_field.setPlaceholderText('Numeric IPv4 or IPv6 address')
        self.server_field.editingFinished.connect(lambda: self.set_pref('server', self.server_field.text()))
        form.addRow('Server address', self.server_field)
        port = QSpinBox()
        port.setRange(1, 65535)
        port.setValue(self.prefs.port)
        port.valueChanged.connect(lambda v: self.set_pref('port', v))
        form.addRow('UDP port', port)
        pilot = QLineEdit(self.prefs.name)
        pilot.setMaxLength(64)
        pilot.editingFinished.connect(lambda: self.set_pref('name', pilot.text()))
        form.addRow('Pilot name', pilot)
        self.host = QCheckBox('Start a dedicated server on this computer (loopback)')
        self.host.setChecked(self.prefs.host)
        self.host.toggled.connect(lambda v: (self.server_field.setEnabled(not v), self.set_pref('host', v)))
        self.server_field.setEnabled(not self.prefs.host)
        form.addRow('Local host', self.host)
        box.addWidget(label('Local hosting binds 127.0.0.1 for flights on this computer. LAN/public hosting uses the standalone ofs_server; see networking documentation for binding and firewall setup.', 'muted'))
        box.addStretch()

    def make_advanced(self):
        box = self.page('Fine tuning', 'Advanced', 'Adjust renderer settings with clear units. Higher distances and shadow sizes increase GPU cost.')
        form = self.form(box)
        cameras = [(v, t) for v, t in [('chase', 'Chase'), ('close-chase', 'Close chase'), ('cockpit', 'Cockpit'), ('orbit', 'Orbit'), ('free', 'Free camera')]]
        form.addRow('Starting camera', self.combo(cameras, self.prefs.camera, lambda v: self.set_pref('camera', v)))
        for key, title, tip in [('renderDistance', 'Draw distance (m)', 'Far clipping distance in metres. Large values reduce depth precision.'),
                                ('sceneryDistance', 'Scenery distance (m)', 'Terrain vegetation visibility distance, 1–15 km.'),
                                ('lodBias', 'Model LOD bias', 'Log₂ distance bias. Positive values choose cheaper geometry sooner.'),
                                ('shadowMapSize', 'Shadow map size', 'Shadow-map resolution per side. Larger maps cost GPU memory.'),
                                ('shadowExtent', 'Shadow half extent (m)', 'Size of the shadow box around the camera; increasing range reduces detail.'),
                                ('bloomStrength', 'Bloom strength', 'Post-processing bloom contribution, 0–0.4.'),
                                ('fog', 'Atmospheric fog', ''), ('contrails', 'Contrails', ''), ('wingVapor', 'Wing vapour', ''),
                                ('engineHeat', 'Engine exhaust bands', ''), ('hud', 'Flight HUD', ''), ('playerLabels', 'Player labels', '')]:
            self.graphic(form, key, title, tip=tip)
        self.hardware_label = label('Detecting hardware…', 'muted')
        box.addWidget(self.hardware_label)
        box.addStretch()

    def make_updates(self):
        box = self.page('Release control', 'Updates', 'Verified releases arrive over HTTPS. Updates preserve settings and the previous release.')
        form = self.form(box)
        form.addRow('Release channel', self.combo([('stable', 'Stable'), ('development', 'Development / Experimental')], self.prefs.channel, self.change_channel))
        self.url = QLineEdit(self.prefs.manifest_url)
        self.url.setPlaceholderText('https://your-update-host/manifest.json')
        self.url.editingFinished.connect(self.change_endpoint)
        form.addRow('Publisher manifest', self.url)
        for key, text in [('auto_check', 'Check automatically on launcher start'), ('auto_install', 'Install available updates automatically when the game is idle')]:
            check = QCheckBox(text)
            check.setChecked(getattr(self.prefs, key))
            check.toggled.connect(lambda v, k=key: self.set_pref(k, v))
            form.addRow('', check)
        controls = QHBoxLayout()
        controls.addWidget(button('Check for updates', self.check_updates))
        self.install_update_button = button('Download & install', self.install_update)
        self.install_update_button.setEnabled(False)
        controls.addWidget(self.install_update_button)
        self.rollback_button = button('Roll back', self.do_rollback)
        controls.addWidget(self.rollback_button)
        box.addLayout(controls)
        self.notes = QPlainTextEdit()
        self.notes.setReadOnly(True)
        self.notes.setPlaceholderText('Release notes will appear here after checking the publisher manifest.')
        box.addWidget(self.notes, 1)
        box.addWidget(label('Publishing requires an HTTPS update host. The launcher contains no repository credentials. Automatic updates begin after the publisher endpoint is configured.', 'muted'))

    def make_installation(self):
        box = self.page('Ground services', 'Installation / Repair', 'Inspect your installation, verify files, and repair a staged copy without touching user settings.')
        self.installation_label = label('', 'muted')
        box.addWidget(self.installation_label)
        box.addWidget(button('Choose installation folder', self.choose_installation))
        controls = QHBoxLayout()
        controls.addWidget(button('Open game folder', lambda: self.open_folder(self.installation.directory if self.installation else self.prefs.installation)))
        controls.addWidget(button('Open logs folder', lambda: self.open_folder(self.logs)))
        box.addLayout(controls)
        services = QHBoxLayout()
        services.addWidget(button('Verify installation', self.verify_installation))
        services.addWidget(button('Repair affected files', self.repair_installation))
        box.addLayout(services)
        self.verify_report = QPlainTextEdit()
        self.verify_report.setReadOnly(True)
        self.verify_report.setPlaceholderText('Verification results will appear here.')
        box.addWidget(self.verify_report, 1)
        box.addWidget(label(f'User settings: {self.data}\nRepairs require the publisher’s manifest for the installed version. Source checkouts use asset preflight checks.', 'muted'))

    def set_installation(self, root):
        if self.session.running() or self.job:
            self.error('Finish the current flight or operation before changing installation.')
            return
        if self.lease:
            self.lease.close()
            self.lease = None
        self.installation = None
        self.release = None
        try:
            self.lease = Lease(root).acquire()
            if (Path(root) / 'current.json').exists():
                recover(root)
            self.installation = Installation.discover(root)
            self.prefs.installation = str(self.installation.root)
            icon = self.installation.directory / 'data/launcher/icon.svg'
            if icon.is_file():
                self.setWindowIcon(QIcon(str(icon)))
            if not self.installation.managed:
                self.update_label.setText('Updates: source build · pull and rebuild to update')
            publisher = self.installation.directory / 'publisher.json'
            if not self.preferences_path.exists() and publisher.is_file():
                defaults = read_json(publisher)
                self.prefs.manifest_url = https_url(defaults['manifest_url'])
                self.prefs.channel = self.installation.build['channel']
                self.url.setText(self.prefs.manifest_url)
                self.update_label.setText('Updates: automatic startup check configured')
            before = self.updating
            self.updating = True
            self.aircraft_combo.clear()
            for a in self.installation.aircraft:
                self.aircraft_combo.addItem(a['name'], a['id'])
            index = self.aircraft_combo.findData(self.prefs.aircraft)
            self.aircraft_combo.setCurrentIndex(max(index, 0))
            self.prefs.aircraft = self.aircraft_combo.currentData()
            self.mode_combo.clear()
            for mode in self.installation.catalog['modes']:
                self.mode_combo.addItem(MODES[mode], mode)
            index = self.mode_combo.findData(self.prefs.mode)
            self.mode_combo.setCurrentIndex(max(index, 0))
            self.prefs.mode = self.mode_combo.currentData()
            self.updating = before
            self.save()
            missing = self.installation.missing_assets()
            self.status.setText('Installation ready' if not missing else f'{len(missing)} required assets are missing. See Installation / Repair.')
            self.installation_label.setText(f'{self.installation.root}\nOpenFlightSim {self.installation.build["version"]} · {self.installation.build.get("commit", "unknown")}\nInstalled channel: {self.installation.build.get("channel", "unknown")} · {"Managed release" if self.installation.managed else "Developer checkout / unpacked simulator"}')
            self.disk_usage()
        except (LauncherError, OSError) as exc:
            if self.lease:
                self.lease.close()
                self.lease = None
            self.status.setText(str(exc))
            self.installation_label.setText(f'{root}\n{exc}')
        self.select_aircraft()
        self.select_mode()
        self.refresh_summary()

    def disk_usage(self):
        # Installed manifests already carry sizes; avoid recursively scanning a source checkout.
        if self.installation.managed:
            try:
                metadata = read_json(self.installation.directory / 'release.json')
                total = sum(r['size'] for r in metadata['files'].values())
                import shutil
                free = shutil.disk_usage(self.installation.root).free
                self.installation_label.setText(self.installation_label.text() + f'\nActive release: {total / 1024**3:.2f} GiB · free disk: {free / 1024**3:.1f} GiB')
            except (OSError, LauncherError, KeyError, TypeError):
                pass

    def select_aircraft(self):
        if self.installation:
            if not self.updating:
                self.prefs.aircraft = self.aircraft_combo.currentData()
                self.save()
            a = next((a for a in self.installation.aircraft if a['id'] == self.aircraft_combo.currentData()), None)
            if a:
                self.aircraft_detail.setText(f'{a.get("manufacturer", "Manufacturer not provided")} · {a.get("type", "Aircraft")}\n{a.get("role", "Role not provided")}\n{a.get("engines", "?")} engines · {a.get("engine_type", "Engine type not provided")}\nSpan: {a.get("span_m", "?")} m · simulator reference mass: {a.get("reference_mass_kg", "?")} kg\n{"Armed aircraft · local dogfight available" if a["armed"] else "Unarmed aircraft"}')
                preview = None
                if a.get('thumbnail'):
                    try:
                        preview = QPixmap(str(safe_path(self.installation.directory, a['thumbnail'])))
                    except LauncherError:
                        pass
                for art in (self.art, self.aircraft_art):
                    art.thumbnail = preview
                    art.update()
        self.refresh_summary()

    def select_mode(self):
        mode = self.mode_combo.currentData() or 'free'
        if not self.updating:
            self.prefs.mode = mode
            self.save()
        self.bots.setEnabled(mode == 'dogfight')
        self.airborne.setEnabled(mode == 'free')
        self.mode_detail.setText({'free': 'Fly offline from the runway or start airborne. No network connection is required.', 'multiplayer': 'Configure server address, pilot name and local hosting in Multiplayer. Spawn state is managed by the server.', 'dogfight': 'Fight 1–8 AI opponents on the local authoritative server. Choose an armed aircraft; the simulator starts airborne.'}[mode])
        self.refresh_summary()

    def refresh_summary(self):
        if not hasattr(self, 'rollback_button'):
            return
        busy = bool(self.job) or self.session.running()
        for index in range(1, self.stack.count()):
            self.stack.widget(index).setEnabled(not busy)
        self.stop_button.setVisible(self.session.running())
        self.play_button.setEnabled(bool(self.installation) and not busy and not self.graphics_error)
        self.play_button.setText('IN FLIGHT' if self.session.running() else 'PLAY   →')
        update = bool(self.release and self.installation and self.installation.managed) and not busy and Version(self.release.version) > Version(self.installation.build['version'])
        self.install_update_button.setEnabled(update)
        # Players should not have to find the Updates page to get a new version.
        self.update_button.setVisible(update)
        if update:
            self.update_button.setText(f'Update to {self.release.version}')
        self.rollback_button.setEnabled(bool(self.installation and self.installation.managed) and not busy)
        if self.installation:
            a = next((a for a in self.installation.aircraft if a['id'] == self.prefs.aircraft), None)
            self.version_label.setText(f'OPENFLIGHTSIM {self.installation.build["version"]}   /   {self.prefs.channel.upper()}')
            self.aircraft_label.setText(a['name'].split(' | ')[0] if a else 'Choose aircraft')
            g = self.graphics.values
            self.summary_label.setText(f'{MODES.get(self.prefs.mode, self.prefs.mode)}  ·  {self.prefs.preset}  ·  {g["width"]} × {g["height"]}\n{"Desktop fullscreen" if g["fullscreen"] == "1" else "Windowed"}  ·  VSync {"on" if g["vsync"] == "1" else "off"}  ·  {self.prefs.camera.replace("-", " ").title()} camera')

    def start_background(self):
        if self.initial_error:
            self.error('Preferences could not be loaded: ' + self.initial_error)
        if self.graphics_error:
            self.error(self.graphics_error + '. Choose a graphics preset to restore valid settings; your original file is preserved until then.')
        screen = self.screen()
        self.updating = True
        for mode in display_modes(screen):
            self.resolution.addItem(f'{mode[0]} × {mode[1]}', mode)
        wanted = (int(self.graphics.values['width']), int(self.graphics.values['height']))
        self.resolution.setCurrentIndex(self.resolution.findData(wanted))
        self.updating = False
        self.display_detail.setText(f'{screen.name()} · desktop {screen.size().width()} × {screen.size().height()} · {screen.refreshRate():.0f} Hz')
        def detected(info):
            self.hardware = info
            name, reason = recommendation(info)
            self.recommendation_label.setText(f'{name}: {reason}')
            self.hardware_label.setText(f'{info.os}\n{info.cpu} · {info.cores} logical CPUs · {info.ram_gib:.1f} GiB RAM\n{info.gpu} · {f"{info.vram_gib:.1f} GiB reported VRAM" if info.vram_gib else "VRAM unavailable"}')
            self.status.setText('Hardware detected · settings ready')
            if self.prefs.auto_check and self.prefs.manifest_url:
                QTimer.singleShot(50, self.check_updates)
        self.start_job('Detecting hardware', lambda c, p: probe(), detected)
        if self.installation and (self.installation.root / 'last-update-result.json').exists():
            try:
                result = read_json(self.installation.root / 'last-update-result.json')
                self.error('The updater could not finish: ' + result['message'])
                (self.installation.root / 'last-update-result.json').unlink()
            except (LauncherError, OSError, KeyError):
                pass

    def start_job(self, title, function, success):
        if self.job:
            self.error('Another operation is in progress.')
            return
        self.status.setText(title + '…')
        self.progress.setRange(0, 0)
        self.progress.show()
        self.cancel_button.show()
        self.job = Job(function)
        self.job.metric.connect(self.job_progress)
        def receive(value):
            try:
                success(value)
            except Exception as exc:
                log.error('Operation completion failed: %s', exc)
                self.error(str(exc))
        self.job.result.connect(receive)
        self.job.failed.connect(self.error)
        self.job.finished.connect(self.job_finished)
        self.job.start()
        self.refresh_summary()

    def job_progress(self, metric):
        done, total, speed = metric
        self.progress.setRange(0, 100)
        self.progress.setValue(int(done / max(total, 1) * 100))
        if speed:
            self.status.setText(f'{done / 1024**2:.1f} / {total / 1024**2:.1f} MiB · {speed / 1024**2:.1f} MiB/s')
        else:
            self.status.setText(f'Verified / processed {done} of {total}')

    def job_finished(self):
        self.job.deleteLater()
        self.job = None
        self.progress.hide()
        self.cancel_button.hide()
        self.refresh_summary()
        if self.pending_close:
            self.close()

    def cancel_job(self):
        if self.job:
            self.job.cancel.set()
            self.status.setText('Cancelling safely…')

    def error(self, text):
        self.status.setText(text)
        if not text.startswith(('Download cancelled', 'Verification cancelled', 'Repair cancelled')):
            QMessageBox.warning(self, 'OpenFlightSim', text)

    def play(self):
        if not self.installation:
            self.error('Choose a valid installation first.')
            return
        try:
            self.session.start(self.installation, self.prefs, self.graphics, self.logs, self.data / 'runtime')
            self.status.setText('Simulator started · launcher remains open to manage this flight')
            self.refresh_summary()
        except (LauncherError, OSError) as exc:
            self.error(str(exc))

    def poll_session(self):
        if self.session.client and self.session.client.poll() is not None:
            code = self.session.client.returncode
            self.session.cleanup()
            self.session.client = None
            self.status.setText(f'Flight ended (exit {code})')
            try:
                self.graphics = Graphics(self.graphics.path)
                self.sync_graphics()
            except (LauncherError, OSError) as exc:
                self.graphics_error = str(exc)
            self.refresh_summary()
            if code and not getattr(self, 'stopped_by_user', False):
                self.error(f'Simulator exited with code {code}. Open logs from Installation / Repair for startup or connection details.')
            self.stopped_by_user = False
            if self.pending_close:
                self.close()
        if self.session.running() and self.session.server and self.session.server.poll() is not None:
            self.error('Local server stopped unexpectedly. Review server.log.')
            self.session.server = None

    def stop_flight(self):
        if self.session.running():
            self.stopped_by_user = True
            self.session.client.terminate()
            self.status.setText('Ending flight…')

    def choose_installation(self):
        root = QFileDialog.getExistingDirectory(self, 'Choose OpenFlightSim installation')
        if root:
            self.set_installation(root)

    def open_folder(self, path):
        if path and not QDesktopServices.openUrl(QUrl.fromLocalFile(str(Path(path).resolve()))):
            self.error('Could not open folder with the system file manager.')

    def change_channel(self, value):
        self.release = None
        self.notes.clear()
        self.set_pref('channel', value)
        self.update_label.setText('Update channel changed · check for updates')

    def change_endpoint(self):
        value = self.url.text().strip()
        try:
            if value:
                https_url(value)
            self.release = None
            self.set_pref('manifest_url', value)
        except LauncherError as exc:
            self.error(str(exc))

    def check_updates(self):
        if not self.prefs.manifest_url:
            self.error('Configure the publisher’s HTTPS manifest URL in Updates.')
            return
        channel, url = self.prefs.channel, self.prefs.manifest_url
        def checked(release):
            if channel != self.prefs.channel or url != self.prefs.manifest_url:
                self.status.setText('Update settings changed. Check again.')
                return
            self.release = release
            installed = self.installation.build['version'] if self.installation else '0.0.0'
            available = Version(release.version) > Version(installed)
            compatible = Version(self.build['version']) >= Version(release.data['minimum_launcher_version'])
            text = f'OpenFlightSim {release.version} available' if available else f'OpenFlightSim {installed} is up to date for {channel}'
            if not compatible:
                text += ' · a newer launcher distribution is required'
                self.release = None
            self.update_label.setText(text)
            self.status.setText(text)
            self.notes.setPlainText(f'{release.version} · {release.data["channel"]}\nDownload: {release.package["size"] / 1024**2:.1f} MiB\n\n{release.data.get("notes", "No release notes provided.")}')
            self.refresh_summary()
            if available and compatible and self.prefs.auto_install and self.installation and self.installation.managed and not self.session.running():
                QTimer.singleShot(50, self.install_update)
        self.start_job('Checking publisher manifest', lambda c, p: select_release(fetch_manifest(url), channel), checked)

    def managed_idle(self):
        if not self.installation or not self.installation.managed:
            raise LauncherError('Updates/repair require a managed release distribution. Developer checkouts can still launch and verify assets.')
        if self.session.running():
            raise LauncherError('Finish the current flight before updating or repairing.')
        if self.job:
            raise LauncherError('Wait for the current operation to finish.')

    def install_update(self):
        try:
            self.managed_idle()
            if not self.release or Version(self.release.version) <= Version(self.installation.build['version']):
                raise LauncherError('Check for a newer compatible release first.')
            release, root = self.release, self.installation.root
            import shutil
            needed = release.package['size'] + sum(r['size'] for r in release.data['files'].values()) + 256 * 1024**2
            if shutil.disk_usage(root).free < needed:
                raise LauncherError('Insufficient space for download and a separate verified release.')
            destination = safe_path(root, '.downloads/' + release.package['sha256'] + '.zip')
            def ready(path):
                request = {'kind': 'update', 'release': release.data, 'package': path.relative_to(root).as_posix()}
                self.dispatch_helper(request)
            self.start_job('Downloading verified update', lambda c, p: download(release.package, destination, c, p), ready)
        except (LauncherError, OSError) as exc:
            self.error(str(exc))

    def dispatch_helper(self, request=None, rollback=False):
        root = self.installation.root
        # Stable bootstrap sits outside every replaceable release slot.
        helper = root / ('OpenFlightSim.exe' if os.name == 'nt' else 'OpenFlightSim')
        if not helper.is_file():
            self.error('Stable bootstrap is missing; restore it from the distribution package.')
            return
        args = [str(helper), '--installation', str(root), '--user-data', str(self.data)]
        if rollback:
            args += ['--rollback']
        else:
            pending = root / '.pending-update.json'
            write_json(pending, request)
            args += ['--apply', str(pending)]
        try:
            spawn(args, cwd=root)
        except OSError as exc:
            self.error(f'Could not start updater: {exc}')
            return
        self.pending_close = True
        self.status.setText('Verified update ready · restarting through updater')
        # The helper waits for the OS lease, including until this worker is done.
        if not self.job:
            self.close()

    def do_rollback(self):
        try:
            self.managed_idle()
            if not pointer(self.installation.root)['previous']:
                raise LauncherError('No previous release is available.')
            self.dispatch_helper(rollback=True)
        except (LauncherError, OSError) as exc:
            self.error(str(exc))

    def verify_installation(self):
        if not self.installation:
            self.error('Choose an installation first.')
            return
        if not self.installation.managed:
            missing = self.installation.missing_assets()
            self.verify_report.setPlainText('Source checkout asset preflight:\n' + ('\n'.join('Missing: ' + name for name in missing) if missing else 'All registry models/LODs are present. Hash verification requires a release manifest.'))
            self.navigation.setCurrentRow(9)
            return
        def done(issues):
            self.verify_report.setPlainText('\n'.join(f'{status}: {name}' for name, status in issues.items()) or 'All installed release files match their expected SHA-256 hashes and sizes.')
            self.status.setText(f'Verification complete · {len(issues)} affected files')
        self.start_job('Verifying installation', lambda c, p: verify(self.installation.directory, cancel=c, progress=p), done)

    def repair_installation(self):
        try:
            self.managed_idle()
            if not self.prefs.manifest_url:
                raise LauncherError('Configure a publisher manifest URL before repairing.')
            installation, url = self.installation, self.prefs.manifest_url
            def repair(cancel, progress):
                manifest = fetch_manifest(url)
                # Repair older installed releases too, even if the latest is newer.
                matches = [r for r in manifest.get('releases', []) if r.get('version') == installation.build['version'] and r.get('channel') == installation.build['channel']]
                release = select_release({'schema': 1, 'releases': matches}, installation.build['channel'])
                stage = safe_path(installation.root, '.staging/repair-' + release.package['sha256'][:16])
                repair_stage(installation.directory, stage, release, cancel, progress)
                return {'kind': 'repair', 'release': release.data, 'stage': stage.relative_to(installation.root).as_posix()}
            self.start_job('Repairing staged release', repair, self.dispatch_helper)
        except (LauncherError, OSError) as exc:
            self.error(str(exc))

    def closeEvent(self, event):
        if self.job:
            self.pending_close = True
            self.cancel_job()
            event.ignore()
            return
        if self.session.running():
            self.pending_close = True
            self.status.setText('The launcher will close when your flight ends.')
            event.ignore()
            return
        self.session.cleanup()
        if self.lease:
            self.lease.close()
        self.save()
        event.accept()
