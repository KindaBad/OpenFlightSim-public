"""Native aviation-styled Qt Widgets UI. Long operations run off the UI thread."""
import dataclasses
import logging
import os
from pathlib import Path
import subprocess
import sys
import threading
import uuid

from PySide6.QtCore import Qt, QThread, Signal, QTimer, QUrl, QSize
from PySide6.QtGui import QDesktopServices, QPixmap, QIcon
from PySide6.QtWidgets import (QMainWindow, QWidget, QHBoxLayout, QVBoxLayout, QLabel,
    QPushButton, QListWidget, QStackedWidget, QFrame, QFormLayout, QComboBox,
    QCheckBox, QDoubleSpinBox, QLineEdit, QProgressBar, QMessageBox,
    QFileDialog, QScrollArea, QPlainTextEdit, QListWidgetItem, QSizePolicy)

from . import lan
from .config import Preferences, Graphics, GRAPHICS, PRESETS, CUSTOM_PRESET
from .download import download, fetch_manifest, Cancelled
from .game import Installation, Session, MODES, arguments
from .hardware import probe, recommendation, display_modes
from .installation import Lease, verify, repair_stage, recover, pointer
from .manifest import select_release, https_url
from .platform_process import spawn
from .storage import LauncherError, read_json, write_json, safe_path
from .version import Version
from .presentation import (STYLE, CoverArt, AircraftGallery, ResponsiveRow, DropDown, Toggle, SpinBox,
    DoubleSpinBox, WrapLabel, Facts, icon, wordmark)

log = logging.getLogger('ofs.ui')


def aircraft_facts(a):
    """The aircraft page's data sheet, from the game's own catalogue and the
    published figures in data/launcher/aircraft-info.json."""
    def number(key):
        value = a.get(key)
        return value if isinstance(value, (int, float)) and not isinstance(value, bool) and value > 0 else None
    engines = a.get('engines', '?')
    facts = [('Role', a.get('role', 'Not provided')),
             ('Engines', f'{engines} × {a.get("engine_type", "engine type not provided")}')]
    dry, reheat = number('thrust_dry_kn'), number('thrust_reheat_kn')
    if dry:
        facts.append(('Thrust', f'{dry:g} kN' + (f' dry · {reheat:g} kN with reheat' if reheat else '')))
    empty, fuel = number('empty_mass_kg'), number('fuel_capacity_kg')
    facts.append(('Empty mass', f'{empty:,.0f} kg') if empty else ('Reference mass', f'{a.get("reference_mass_kg", "?")} kg'))
    if fuel:
        facts.append(('Internal fuel', f'{fuel:,.0f} kg'))
    area = number('wing_area_m2')
    facts.append(('Wingspan', f'{a.get("span_m", "?")} m' + (f' · wing area {area:g} m²' if area else '')))
    if isinstance(a.get('max_speed'), str):
        facts.append(('Top speed', a['max_speed']))
    if isinstance(a.get('armament'), str) and (a['armed'] or a.get('bomber')):
        facts.append(('Armament', a['armament']))
    else:
        facts.append(('Armament', 'Armed · local dogfight available' if a['armed'] else 'Unarmed'))
    return facts


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


class Scan(QThread):
    """One look for games on the local network, off the UI thread."""
    found = Signal(object, object, object)

    def __init__(self, parent, firewall_port=None):
        super().__init__(parent)
        # Asking the firewall starts other programs, so it is done when the
        # caller wants it and not on every look.
        self.firewall_port = firewall_port

    def run(self):
        advice = False  # not asked
        try:
            if self.firewall_port:
                advice = lan.firewall_advice(self.firewall_port)
            self.found.emit(lan.discover(), lan.local_addresses(), advice)
        except OSError as exc:
            log.info('LAN discovery unavailable: %s', exc)
            self.found.emit([], [], advice)


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
        self.scan = None
        self.lobbies = []
        self.updating = True
        self.pending_close = False
        self.setWindowTitle('OpenFlightSim Launcher')
        self.resize(1440, 900)
        self.setMinimumSize(1020, 720)
        self.setStyleSheet(STYLE)
        self.graphic_widgets = {}
        self.home_graphics = {}
        self.status_buttons = []
        self.make_ui()
        root = installation or self.prefs.installation or Path(__file__).resolve().parent.parent
        self.set_installation(root)
        self.updating = False
        self.poll = QTimer(self)
        self.poll.timeout.connect(self.poll_session)
        self.poll.start(500)
        # The list of games keeps itself current while its page is open.
        self.firewall, self.firewall_port = None, None
        self.lan_timer = QTimer(self)
        self.lan_timer.timeout.connect(self.scan_lan)
        self.lan_timer.start(3000)
        QTimer.singleShot(0, self.start_background)

    def make_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        central.setObjectName('surface')
        layout = QVBoxLayout(central)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        header = QFrame()
        header.setObjectName('header')
        header.setFixedHeight(78)
        top = QHBoxLayout(header)
        top.setContentsMargins(26, 12, 28, 12)
        mark = QLabel()
        mark.setPixmap(icon('wing', '#269cff', 42).pixmap(QSize(42, 42)))
        top.addWidget(mark)
        top.addSpacing(6)
        top.addWidget(wordmark(27))
        top.addStretch()
        self.connection_label = label('●  Local flight', 'muted', False)
        self.connection_label.setToolTip('Free flight works offline. No account is required.')
        top.addWidget(self.connection_label)
        top.addSpacing(25)
        self.pilot_button = button(self.prefs.name, lambda: self.show_page(10))
        self.pilot_button.setObjectName('profile')
        self.pilot_button.setIcon(icon('pilot', '#95b6da', 22))
        self.pilot_button.setIconSize(QSize(22, 22))
        self.pilot_button.setToolTip('Edit your multiplayer pilot name')
        top.addWidget(self.pilot_button)
        layout.addWidget(header)
        body = QHBoxLayout()
        body.setContentsMargins(0, 0, 0, 0)
        body.setSpacing(0)
        sidebar = QFrame()
        sidebar.setObjectName('sidebar')
        sidebar.setFixedWidth(202)
        side = QVBoxLayout(sidebar)
        side.setContentsMargins(8, 18, 12, 24)
        self.navigation = QListWidget()
        self.navigation.setIconSize(QSize(24, 24))
        self.navigation.setSpacing(2)
        self.nav_pages = [0, 1, 10, 2, 8, 9]
        for title, symbol in [('Home', 'home'), ('Aircraft', 'aircraft'), ('Multiplayer', 'network'),
                              ('Settings', 'settings'), ('Updates', 'download'), ('Installation', 'repair')]:
            self.navigation.addItem(QListWidgetItem(icon(symbol), '  ' + title))
        self.navigation.currentRowChanged.connect(self.navigate)
        side.addWidget(self.navigation, 1)
        divider = QFrame()
        divider.setFixedHeight(1)
        divider.setStyleSheet('background: #203243;')
        side.addWidget(divider)
        side.addSpacing(12)
        for text, symbol, action in (('  Support  ↗', 'support', self.open_support),
                                     ('  Release notes', 'notes', lambda: self.show_page(8))):
            link = button(text, action)
            link.setObjectName('sideLink')
            link.setIcon(icon(symbol))
            link.setIconSize(QSize(20, 20))
            side.addWidget(link)
        side.addSpacing(14)
        self.version_label = label(f'Launcher {self.build["version"]}', 'muted')
        self.version_label.setContentsMargins(20, 0, 0, 0)
        side.addWidget(self.version_label)
        body.addWidget(sidebar)
        main = QWidget()
        main.setObjectName('surface')
        outer = QVBoxLayout(main)
        outer.setContentsMargins(22, 14, 22, 18)
        outer.setSpacing(16)
        self.settings_tabs = QWidget()
        self.settings_tabs.setObjectName('gallery')
        tabs = QHBoxLayout(self.settings_tabs)
        tabs.setContentsMargins(0, 0, 0, 0)
        tabs.setSpacing(4)
        self.tab_buttons = {}
        for index, text in enumerate(('Flight mode', 'Graphics', 'Display', 'Controls', 'Network', 'Advanced'), 2):
            tab = button(text, lambda checked=False, page=index: self.show_page(page))
            tab.setObjectName('settingsTab')
            tab.setCheckable(True)
            self.tab_buttons[index] = tab
            tabs.addWidget(tab)
        tabs.addStretch()
        self.settings_tabs.hide()
        outer.addWidget(self.settings_tabs)
        self.stack = QStackedWidget()
        outer.addWidget(self.stack, 1)
        footer = QFrame()
        footer.setObjectName('downloadBar')
        footer.setMinimumHeight(70)
        bottom = QHBoxLayout(footer)
        bottom.setContentsMargins(20, 10, 20, 10)
        bottom.setSpacing(16)
        self.operation_icon = QLabel()
        self.show_operation('check')
        bottom.addWidget(self.operation_icon)
        state = QVBoxLayout()
        state.setSpacing(3)
        self.operation_label = label('Ready for departure', 'operationTitle')
        state.addWidget(self.operation_label)
        self.status = label('Preparing flight operations…', 'muted')
        self.status.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Preferred)
        state.addWidget(self.status)
        bottom.addLayout(state, 3)
        transfer = QVBoxLayout()
        transfer.setSpacing(8)
        self.transfer_label = label('', 'transfer', False)
        self.transfer_label.hide()
        transfer.addWidget(self.transfer_label)
        self.progress = QProgressBar()
        self.progress.setTextVisible(False)
        self.progress.hide()
        transfer.addWidget(self.progress)
        bottom.addLayout(transfer, 2)
        self.update_button = button('Update now', self.install_update)
        self.update_button.setObjectName('updateAction')
        self.update_button.setIcon(icon('download', '#b8dcff', 18))
        self.update_button.hide()
        bottom.addWidget(self.update_button)
        self.cancel_button = button('Cancel', self.cancel_job)
        self.cancel_button.setIcon(icon('close', '#b8d2ef', 18))
        self.cancel_button.setToolTip('Cancel safely. Downloaded data is kept so you can resume.')
        self.cancel_button.hide()
        bottom.addWidget(self.cancel_button)
        outer.addWidget(footer)
        body.addWidget(main, 1)
        layout.addLayout(body, 1)
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
        self.make_lan()
        self.show_page(0)

    def show_operation(self, symbol):
        self.operation_icon.setPixmap(icon(symbol, '#259eff', 26).pixmap(QSize(26, 26)))

    def navigate(self, row):
        if 0 <= row < len(self.nav_pages):
            self.show_page(self.nav_pages[row])

    def show_page(self, index):
        self.stack.setCurrentIndex(index)
        settings = 2 <= index <= 7
        self.settings_tabs.setVisible(settings)
        for page, tab in self.tab_buttons.items():
            tab.setChecked(page == index)
        self.navigation.blockSignals(True)
        self.navigation.setCurrentRow(self.nav_pages.index(2 if settings else index))
        self.navigation.blockSignals(False)
        if index == 10:
            self.scan_lan()

    def open_support(self):
        if not QDesktopServices.openUrl(QUrl('https://github.com/KindaBad/OpenFlightSim-public/issues')):
            self.error('Could not open support in your browser.')

    def page(self, eyebrow, title, description):
        content = QWidget()
        content.setObjectName('page')
        box = QVBoxLayout(content)
        box.setContentsMargins(0, 0, 8, 0)
        box.setSpacing(16)
        heading = QVBoxLayout()
        heading.setSpacing(5)
        heading.addWidget(label(eyebrow.upper(), 'eyebrow'))
        heading.addWidget(label(title, 'title'))
        heading.addWidget(label(description, 'muted'))
        box.addLayout(heading)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(content)
        self.stack.addWidget(scroll)
        return box

    def form(self, box):
        card = QFrame()
        card.setObjectName('card')
        form = QFormLayout(card)
        form.setContentsMargins(20, 18, 20, 18)
        form.setHorizontalSpacing(22)
        form.setVerticalSpacing(12)
        form.setLabelAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.ExpandingFieldsGrow)
        box.addWidget(card)
        return form

    def combo(self, items, value, callback):
        widget = DropDown()
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
        content = QWidget()
        content.setObjectName('page')
        box = QVBoxLayout(content)
        box.setContentsMargins(0, 0, 0, 0)
        box.setSpacing(16)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(content)
        self.stack.addWidget(scroll)
        self.art = CoverArt(hero=True)
        hero = QVBoxLayout(self.art)
        hero.setContentsMargins(32, 32, 30, 26)
        hero.setSpacing(12)
        hero.addStretch()
        hero.addWidget(label('REAL PLANES  ·  REAL PLACES  ·  AN OPEN SKY', 'eyebrow'))
        hero.addWidget(wordmark(46))
        description = label('Your next flight starts here.\nChoose your aircraft, make it your own,\nand take to the skies.')
        description.setStyleSheet('font-size: 16px; color: #d6e1ee;')
        hero.addWidget(description)
        self.aircraft_label = label('Select an installation', 'flightAircraft')
        self.summary_label = label('', 'flightSummary')
        hero.addSpacing(4)
        hero.addWidget(self.aircraft_label)
        hero.addWidget(self.summary_label)
        actions = QHBoxLayout()
        actions.setSpacing(14)
        self.play_button = button('Play', self.play)
        self.play_button.setObjectName('play')
        self.play_button.setIcon(icon('play', '#ffffff', 24))
        self.play_button.setIconSize(QSize(24, 24))
        self.play_button.setMinimumWidth(195)
        actions.addWidget(self.play_button)
        settings = button('Settings', lambda: self.show_page(3))
        settings.setObjectName('heroSettings')
        settings.setIcon(icon('settings', '#bcd4ef', 22))
        settings.setIconSize(QSize(22, 22))
        actions.addWidget(settings)
        self.stop_button = button('End flight', self.stop_flight)
        self.stop_button.hide()
        actions.addWidget(self.stop_button)
        actions.addStretch()
        hero.addSpacing(5)
        hero.addLayout(actions)
        hero.addStretch()
        # Keep copy in the left side of the banner at large window sizes.
        for widget in (description, self.aircraft_label, self.summary_label):
            widget.setMaximumWidth(470)
        box.addWidget(self.art, 3)
        row = ResponsiveRow()
        hangar = QFrame()
        hangar.setObjectName('card')
        aircraft_box = QVBoxLayout(hangar)
        aircraft_box.setContentsMargins(18, 16, 18, 16)
        aircraft_box.setSpacing(14)
        heading = QHBoxLayout()
        symbol = QLabel()
        symbol.setPixmap(icon('aircraft', '#9bbde2', 24).pixmap(QSize(24, 24)))
        heading.addWidget(symbol)
        heading.addWidget(label('Aircraft', 'sectionTitle'))
        heading.addStretch()
        view_all = button('View all  ›', lambda: self.show_page(1))
        view_all.setObjectName('link')
        heading.addWidget(view_all)
        aircraft_box.addLayout(heading)
        self.home_aircraft = AircraftGallery(height=116)
        self.home_aircraft.selected.connect(self.select_card)
        aircraft_box.addWidget(self.home_aircraft)
        aircraft_box.addStretch()
        row.box.addWidget(hangar, 3)
        quick = QFrame()
        quick.setObjectName('card')
        quick.setMinimumWidth(330)
        quick_box = QVBoxLayout(quick)
        quick_box.setContentsMargins(18, 16, 18, 16)
        quick_box.setSpacing(16)
        heading = QHBoxLayout()
        symbol = QLabel()
        symbol.setPixmap(icon('monitor', '#9bbde2', 24).pixmap(QSize(24, 24)))
        heading.addWidget(symbol)
        heading.addWidget(label('Graphics & performance', 'sectionTitle'))
        heading.addStretch()
        quick_box.addLayout(heading)
        form = QFormLayout()
        form.setSpacing(9)
        form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.ExpandingFieldsGrow)
        self.home_preset = DropDown()
        self.home_preset.addItems(['Auto / Recommended', *PRESETS, 'Custom'])
        self.home_preset.currentTextChanged.connect(self.apply_preset)
        form.addRow(label('Graphics preset', 'muted'), self.home_preset)
        self.home_resolution = DropDown()
        self.home_resolution.currentIndexChanged.connect(lambda _: self.change_resolution(self.home_resolution.currentData()))
        self.home_resolution.setToolTip('Window size for the next flight. Fullscreen follows your desktop resolution.')
        form.addRow(label('Window resolution', 'muted'), self.home_resolution)
        self.home_graphics['msaa'] = self.combo([(n, 'Off' if n == 1 else f'{n}× MSAA') for n in (1, 2, 4, 8, 16)],
                                               int(self.graphics.values['msaa']), lambda v: self.graphic_change('msaa', v))
        form.addRow(label('Anti-aliasing', 'muted'), self.home_graphics['msaa'])
        self.home_graphics['vsync'] = self.combo([(1, 'On'), (0, 'Off')], int(self.graphics.values['vsync']),
                                                lambda v: self.graphic_change('vsync', v))
        form.addRow(label('VSync', 'muted'), self.home_graphics['vsync'])
        for widget in (self.home_preset, self.home_resolution, *self.home_graphics.values()):
            widget.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        quick_box.addLayout(form)
        quick_box.addStretch()
        self.quick_settings = quick
        row.box.addWidget(quick, 2)
        box.addWidget(row, 2)
        self.update_label = label('Updates: publisher endpoint has not been configured', 'muted')

    def select_card(self, aircraft_id):
        if self.job or self.session.running():
            return
        index = self.aircraft_combo.findData(aircraft_id)
        if index >= 0:
            self.aircraft_combo.setCurrentIndex(index)

    def make_aircraft(self):
        box = self.page('Your hangar', 'Choose your aircraft', 'Pick the aircraft for your next flight. Your choice is saved automatically.')
        # The catalogue lives in this unseen list; the cards and the data sheet follow it.
        self.aircraft_combo = DropDown(self)
        self.aircraft_combo.hide()
        self.aircraft_combo.currentIndexChanged.connect(self.select_aircraft)
        row = ResponsiveRow(narrow=700)
        hangar = QWidget()
        hangar.setObjectName('gallery')
        hangar_box = QVBoxLayout(hangar)
        hangar_box.setContentsMargins(0, 0, 0, 0)
        self.hangar_aircraft = AircraftGallery(height=132, card_width=185)
        self.hangar_aircraft.selected.connect(self.select_card)
        hangar_box.addWidget(self.hangar_aircraft)
        hangar_box.addStretch()
        row.box.addWidget(hangar, 3)
        sheet = QFrame()
        sheet.setObjectName('card')
        sheet.setMinimumWidth(360)
        sheet_box = QVBoxLayout(sheet)
        sheet_box.setContentsMargins(18, 18, 18, 18)
        sheet_box.setSpacing(12)
        self.aircraft_art = CoverArt()
        self.aircraft_art.setFixedHeight(164)
        sheet_box.addWidget(self.aircraft_art)
        self.aircraft_title = label('', 'detailTitle')
        sheet_box.addWidget(self.aircraft_title)
        self.aircraft_maker = label('', 'muted')
        sheet_box.addWidget(self.aircraft_maker)
        self.aircraft_detail = Facts()
        sheet_box.addWidget(self.aircraft_detail)
        # Only a bomber has a load to choose; the row is hidden for the rest.
        self.loadout_row = QWidget()
        self.loadout_row.setObjectName('gallery')
        loadout_box = QVBoxLayout(self.loadout_row)
        loadout_box.setContentsMargins(0, 4, 0, 0)
        loadout_box.setSpacing(6)
        loadout_box.addWidget(label('Bomb load', 'factKey'))
        self.loadout_combo = self.combo([('mk82', '51 × Mk 82 (500 lb) · carpet bombing'), ('mk84', '18 × Mk 84 (2,000 lb) · hard targets'),
                                         ('nuke', '1 × B83 nuclear · levels a whole base')],
                                        self.prefs.loadout, lambda v: self.set_pref('loadout', v))
        self.loadout_combo.setToolTip('What the bomber takes off with. In flight, O asks for another load, fitted the next time you land and rearm. Space drops.')
        loadout_box.addWidget(self.loadout_combo)
        sheet_box.addWidget(self.loadout_row)
        sheet_box.addSpacing(4)
        self.hangar_play = button('Play', self.play)
        self.hangar_play.setObjectName('primary')
        self.hangar_play.setIcon(icon('play', '#ffffff', 18))
        sheet_box.addWidget(self.hangar_play)
        sheet_box.addWidget(label('Figures are the values the simulator flies with.', 'factKey'))
        sheet_box.addStretch()
        row.box.addWidget(sheet, 2)
        box.addWidget(row)
        box.addStretch()

    def make_mode(self):
        box = self.page('Mission planning', 'Flight mode', 'Choose a mode supported by the installed simulator.')
        form = self.form(box)
        self.mode_combo = DropDown()
        self.mode_combo.currentIndexChanged.connect(self.select_mode)
        form.addRow('Flight mode', self.mode_combo)
        self.airborne = Toggle('Start airborne in trimmed flight')
        self.airborne.setChecked(self.prefs.airborne)
        self.airborne.toggled.connect(lambda v: self.set_pref('airborne', v))
        form.addRow('Initial state', self.airborne)
        self.bots = SpinBox()
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
            widget = Toggle()
            widget.setChecked(bool(value))
            widget.toggled.connect(lambda v: self.graphic_change(key, int(v)))
        else:
            widget = DoubleSpinBox() if isinstance(default, float) else SpinBox()
            widget.setRange(low, high)
            if isinstance(default, float):
                # Fractions such as the glare amount need finer steps than distances or stops.
                widget.setDecimals(3 if high <= 1 else 2)
                widget.setSingleStep(.005 if high <= 1 else .1)
            widget.setValue(value if isinstance(default, float) else int(value))
            widget.valueChanged.connect(lambda v: self.graphic_change(key, v))
        if not isinstance(widget, QCheckBox):
            widget.setMinimumWidth(210)
        widget.setToolTip(tip or 'Saved to the simulator graphics.cfg. Takes effect on the next flight.')
        form.addRow(title, widget)
        self.graphic_widgets[key] = widget

    def graphic_change(self, key, value):
        if self.updating:
            return
        self.graphics.values[key] = str(value)
        self.graphics.values['preset'] = str(CUSTOM_PRESET)
        self.prefs.preset = 'Custom'
        self.sync_graphics()
        self.graphics_error = None
        try:
            self.graphics.save()
            self.save()
            self.refresh_summary()
        except (LauncherError, OSError) as exc:
            self.error(str(exc))

    def make_graphics(self):
        box = self.page('Visual systems', 'Graphics quality', 'Presets set every cost-related renderer option at once. Texture and terrain detail changes apply on the next launch.')
        form = self.form(box)
        self.preset_combo = DropDown()
        self.preset_combo.addItems(['Auto / Recommended', *PRESETS, 'Custom'])
        self.preset_combo.setCurrentText(self.prefs.preset if self.prefs.preset in (*PRESETS, 'Auto / Recommended') else 'Custom')
        self.preset_combo.currentTextChanged.connect(self.apply_preset)
        form.addRow('Quality preset', self.preset_combo)
        self.recommendation_label = WrapLabel('Detecting hardware…', 'muted')
        form.addRow('Recommendation', self.recommendation_label)
        levels = list(enumerate(['Off', 'Low', 'Medium', 'High']))
        self.graphic(form, 'msaa', 'Anti-aliasing', [(n, 'Off' if n == 1 else f'{n}× MSAA') for n in (1, 2, 4, 8, 16)])
        self.graphic(form, 'textureMaxSize', 'Texture limit', [(n, f'{n} px') for n in (512, 1024, 2048, 4096, 8192)])
        for key, title in [('shadows', 'Sun shadows'), ('effects', 'Effects / particles'), ('clouds', 'Volumetric clouds')]:
            self.graphic(form, key, title, levels)
        self.graphic(form, 'terrain', 'Terrain detail', list(enumerate(['Low', 'Medium', 'High'])))
        for key, title in [('fxaa', 'Edge filter (FXAA)'), ('anisotropic', 'Anisotropic filtering'), ('bloom', 'Glare'),
                           ('cloudShadows', 'Cloud shadows'), ('terrainShadows', 'Relief shadows'), ('water', 'Lakes'),
                           ('vegetation', 'Trees'), ('heatDistortion', 'Exhaust heat refraction')]:
            self.graphic(form, key, title)
        box.addStretch()

    def apply_preset(self, name):
        if self.updating:
            return
        if name == 'Custom':
            self.graphic_change('preset', CUSTOM_PRESET)
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
        for widget in (self.preset_combo, self.home_preset):
            widget.setCurrentText(self.prefs.preset)
        for key, widget in self.graphic_widgets.items():
            value = float(self.graphics.values[key])
            if isinstance(widget, QCheckBox):
                widget.setChecked(bool(value))
            elif isinstance(widget, QComboBox):
                widget.setCurrentIndex(widget.findData(int(value)))
            else:
                widget.setValue(value if isinstance(widget, QDoubleSpinBox) else int(value))
        for key, widget in self.home_graphics.items():
            widget.setCurrentIndex(widget.findData(int(self.graphics.values[key])))
        wanted = (int(self.graphics.values['width']), int(self.graphics.values['height']))
        for widget in (self.resolution, self.home_resolution):
            index = widget.findData(wanted)
            if index < 0:
                widget.addItem(f'{wanted[0]} × {wanted[1]} (custom)', wanted)
                index = widget.count() - 1
            widget.setCurrentIndex(index)
        self.updating = before

    def make_display(self):
        box = self.page('Cockpit view', 'Display', 'SDL fullscreen uses the desktop mode. Window dimensions apply in windowed mode.')
        form = self.form(box)
        self.resolution = DropDown()
        self.resolution.currentIndexChanged.connect(lambda _: self.change_resolution(self.resolution.currentData()))
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

    def change_resolution(self, size):
        if self.updating or not size:
            return
        width, height = size
        self.graphics.values.update(width=str(width), height=str(height))
        self.sync_graphics()
        self.graphic_change('width', width)

    def make_controls(self):
        box = self.page('Flight deck', 'Controls / Input', 'The simulator detects SDL gamepads automatically. These are the current built-in bindings.')
        form = self.form(box)
        for title, text in [('Pitch / roll', 'W / S · A / D'), ('Rudder', 'Q / E'), ('Throttle', 'Shift / Ctrl'),
                            ('Brake', 'Hold Ctrl with the throttle at idle: wheel brakes on the ground, airbrake in the air'),
                            ('Gear, flaps, airbrake', 'G · F · H'),
                            ('Camera', 'Tab cycles camera · V flight deck · hold C or the right mouse button to look around · wheel zooms · hold U to follow your missile or bomb'),
                            ('Bomber', 'Hold Space to drop bombs · O changes the load fitted at the next rearm'),
                            ('Map', 'N opens and closes the full map'),
                            ('Weapons', 'Space / left mouse / gamepad right trigger · 1 gun · 2 heat seeker · 3 radar missile'),
                            ('Targets', 'L locks or breaks lock · T / Y next and previous target'),
                            ('Countermeasures', 'R flare against heat seekers (come out of reheat first) · Z chaff against radar missiles (turn them onto your wingtip first)'),
                            ('Repair and rearm', 'Land, stop and wait ten seconds'),
                            ('Chat', '/ or Enter opens chat in multiplayer · Enter sends · Esc cancels'),
                            ('Pilots and scores', 'Hold K in multiplayer'),
                            ('Menu', 'Esc opens the in-flight menu: settings, controls, quit'),
                            ('Gamepad', 'SDL compatible gamepads are discovered in flight')]:
            form.addRow(title, WrapLabel(text, 'factValue'))
        box.addWidget(label('Binding remapping and sensitivity profiles are not yet supported by the simulator.', 'muted'))
        box.addStretch()

    def make_multiplayer(self):
        box = self.page('Flight network', 'Network', 'These settings are used by Play in Multiplayer flight mode. To play with friends on your own network, use the Multiplayer page instead.')
        form = self.form(box)
        self.server_field = QLineEdit(self.prefs.server)
        self.server_field.setPlaceholderText('Numeric IPv4 or IPv6 address')
        self.server_field.editingFinished.connect(lambda: self.set_network(server=self.server_field.text()))
        form.addRow('Server address', self.server_field)
        self.port_field = SpinBox()
        self.port_field.setRange(1, 65535)
        self.port_field.setValue(self.prefs.port)
        self.port_field.valueChanged.connect(lambda v: self.set_network(port=v))
        form.addRow('UDP port', self.port_field)
        self.pilot_field = QLineEdit(self.prefs.name)
        self.pilot_field.setMaxLength(64)
        self.pilot_field.editingFinished.connect(lambda: self.set_network(name=self.pilot_field.text()))
        form.addRow('Pilot name', self.pilot_field)
        self.host = Toggle('Start a dedicated server on this computer (loopback)')
        self.host.setChecked(self.prefs.host)
        self.host.toggled.connect(lambda v: (self.server_field.setEnabled(not v), self.set_pref('host', v)))
        self.server_field.setEnabled(not self.prefs.host)
        form.addRow('Local host', self.host)
        box.addWidget(label('This loopback server is for flights on this computer only. The Multiplayer page hosts a game the whole local network can join; a public server uses the standalone ofs_server, see the networking documentation.', 'muted'))
        box.addStretch()

    def set_network(self, **changes):
        """Pilot name, address and port are each edited in two places; keep them the same.

        Only what changed is copied across, so text still being typed in another field is left alone.
        """
        if self.updating:
            return
        for key, value in changes.items():
            setattr(self.prefs, key, value)
        before, self.updating = self.updating, True
        if 'name' in changes:
            for field in (self.pilot_field, self.lan_pilot):
                if field.text() != self.prefs.name:
                    field.setText(self.prefs.name)
            self.lobby_field.setPlaceholderText(lan.lobby_name(self.prefs.name + "'s game"))
        if 'server' in changes:
            for field in (self.server_field, self.direct_address):
                if field.text() != self.prefs.server:
                    field.setText(self.prefs.server)
        if 'port' in changes:
            for field in (self.port_field, self.direct_port):
                if field.value() != self.prefs.port:
                    field.setValue(self.prefs.port)
        self.updating = before
        self.save()
        self.refresh_summary()

    def card(self, title, symbol):
        frame = QFrame()
        frame.setObjectName('card')
        box = QVBoxLayout(frame)
        box.setContentsMargins(18, 16, 18, 18)
        box.setSpacing(12)
        heading = QHBoxLayout()
        mark = QLabel()
        mark.setPixmap(icon(symbol, '#9bbde2', 24).pixmap(QSize(24, 24)))
        heading.addWidget(mark)
        heading.addWidget(label(title, 'sectionTitle'))
        heading.addStretch()
        box.addLayout(heading)
        return frame, box, heading

    def make_lan(self):
        box = self.page('Fly together', 'Multiplayer', 'Play with friends on the same Wi-Fi or network. One of you hosts a game; everyone else picks it from the list. No addresses to type.')
        form = self.form(box)
        self.lan_pilot = QLineEdit(self.prefs.name)
        self.lan_pilot.setMaxLength(64)
        self.lan_pilot.setToolTip('Shown to other pilots in chat, on labels and on the scoreboard.')
        self.lan_pilot.editingFinished.connect(lambda: self.set_network(name=self.lan_pilot.text()))
        form.addRow('Pilot name', self.lan_pilot)
        self.team_combo = self.combo([('auto', 'Whichever side has fewer pilots'), ('red', 'Red'), ('blue', 'Blue')],
                                     self.prefs.team, lambda v: self.set_pref('team', v))
        self.team_combo.setToolTip('Your side in a team game. Each side has its own airfield to take off from and to land at for repairs and weapons. In the game, type /red or /blue in chat to change. A free-for-all ignores this.')
        form.addRow('Team', self.team_combo)
        row = ResponsiveRow()
        host, host_box, _ = self.card('Host a game', 'play')
        host_form = QFormLayout()
        host_form.setVerticalSpacing(10)
        host_form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.ExpandingFieldsGrow)
        self.lobby_field = QLineEdit(self.prefs.lobby)
        self.lobby_field.setMaxLength(lan.MAX_LOBBY_NAME)
        self.lobby_field.setPlaceholderText(lan.lobby_name(self.prefs.name + "'s game"))
        self.lobby_field.editingFinished.connect(lambda: self.set_pref('lobby', self.lobby_field.text()))
        host_form.addRow(label('Game name', 'muted'), self.lobby_field)
        self.lan_bots = SpinBox()
        self.lan_bots.setRange(0, 8)
        self.lan_bots.setValue(self.prefs.lan_bots)
        self.lan_bots.setToolTip('AI opponents that fly in your game. They need an armed aircraft to fight.')
        self.lan_bots.valueChanged.connect(lambda v: self.set_pref('lan_bots', v))
        host_form.addRow(label('AI opponents', 'muted'), self.lan_bots)
        self.lan_missile_reload = SpinBox()
        self.lan_missile_reload.setRange(0, 3600)
        self.lan_missile_reload.setSingleStep(10)
        self.lan_missile_reload.setSuffix(' s')
        self.lan_missile_reload.setSpecialValueText('Off · rearm by landing')
        self.lan_missile_reload.setValue(self.prefs.lan_missile_reload)
        self.lan_missile_reload.setToolTip('Every pilot gets one missile back after this many seconds, in the air or on the ground, with a quarter of their flares, chaff and gun rounds. At 60, that is one missile a minute.')
        self.lan_missile_reload.valueChanged.connect(lambda v: self.set_pref('lan_missile_reload', v))
        host_form.addRow(label('Weapon reload', 'muted'), self.lan_missile_reload)
        self.lan_teams = self.combo([(False, 'Free-for-all'), (True, 'Team battle · Red against Blue')], self.prefs.lan_teams,
                                    lambda v: (self.set_pref('lan_teams', bool(v)), self.lan_score_limit.setEnabled(bool(v))))
        self.lan_teams.setToolTip('In a team battle each side has an airfield, a depot and three outposts, defended by guns and missiles. Bomb the other side\'s to score, shoot its aircraft down, and land at your own airfield to repair and rearm.')
        host_form.addRow(label('Game type', 'muted'), self.lan_teams)
        self.lan_score_limit = SpinBox()
        self.lan_score_limit.setRange(50, 5000)
        self.lan_score_limit.setSingleStep(50)
        self.lan_score_limit.setSuffix(' points')
        self.lan_score_limit.setValue(self.prefs.lan_score_limit)
        self.lan_score_limit.setEnabled(self.prefs.lan_teams)
        self.lan_score_limit.setToolTip('The first side to reach this wins the round. A building is worth 10 to 40 points and an aircraft shot down 10.')
        self.lan_score_limit.valueChanged.connect(lambda v: self.set_pref('lan_score_limit', v))
        host_form.addRow(label('Score to win', 'muted'), self.lan_score_limit)
        host_box.addLayout(host_form)
        self.host_button = button('Host and fly', self.host_lan)
        self.host_button.setObjectName('primary')
        self.host_button.setIcon(icon('play', '#ffffff', 18))
        host_box.addWidget(self.host_button)
        self.lan_address = label('Your game appears on other computers on this network as soon as you take off.', 'muted')
        host_box.addWidget(self.lan_address)
        host_box.addStretch()
        row.box.addWidget(host, 2)
        join, join_box, heading = self.card('Games on your network', 'network')
        refresh = button('Refresh', self.scan_lan)
        refresh.setObjectName('link')
        refresh.setIcon(icon('refresh'))
        heading.addWidget(refresh)
        self.lobby_list = QListWidget()
        self.lobby_list.setObjectName('lobbies')
        self.lobby_list.setMinimumHeight(150)
        self.lobby_list.itemSelectionChanged.connect(self.lobby_selected)
        self.lobby_list.itemDoubleClicked.connect(lambda _: self.join_selected())
        join_box.addWidget(self.lobby_list, 1)
        self.lan_status = label('Looking for games…', 'muted')
        join_box.addWidget(self.lan_status)
        self.join_button = button('Join', self.join_selected)
        self.join_button.setObjectName('primary')
        self.join_button.setEnabled(False)
        join_box.addWidget(self.join_button)
        row.box.addWidget(join, 3)
        box.addWidget(row)
        direct, direct_box, _ = self.card('Join by address', 'chevron')
        line = QHBoxLayout()
        line.setSpacing(10)
        self.direct_address = QLineEdit(self.prefs.server)
        self.direct_address.setPlaceholderText('Address shown on the host, such as 192.168.1.20')
        self.direct_address.editingFinished.connect(lambda: self.set_network(server=self.direct_address.text()))
        line.addWidget(self.direct_address, 3)
        self.direct_port = SpinBox()
        self.direct_port.setRange(1, 65535)
        self.direct_port.setValue(self.prefs.port)
        self.direct_port.setToolTip('UDP port of the game; 27020 unless the host changed it.')
        self.direct_port.valueChanged.connect(lambda v: self.set_network(port=v))
        line.addWidget(self.direct_port, 1)
        self.direct_button = button('Connect', self.join_address)
        line.addWidget(self.direct_button)
        direct_box.addLayout(line)
        direct_box.addWidget(label('Use this if a game does not show in the list, as happens on large networks such as a school or an office. The host\'s address is shown on their Multiplayer page. A Windows host must allow OpenFlightSim through the firewall on private networks when asked; a Linux host is told on that page if its firewall is in the way.', 'muted'))
        box.addWidget(direct)
        box.addStretch()

    def scan_lan(self):
        """Look for games, unless a look is already under way or nobody is watching."""
        if self.scan or self.stack.currentIndex() != 10 or not self.isVisible() or self.session.running():
            return
        # The firewall is asked about once, and again if the game's port changes.
        ask = self.prefs.port if self.firewall_port != self.prefs.port else None
        self.firewall_port = self.prefs.port
        self.scan = Scan(self, ask)
        self.scan.found.connect(self.show_lobbies)
        self.scan.finished.connect(self.scan_finished)
        self.scan.start()

    def scan_finished(self):
        self.scan.deleteLater()
        self.scan = None

    def protocol(self):
        value = self.installation.catalog.get('protocol') if self.installation else None
        return value if type(value) is int else None

    def show_lobbies(self, lobbies, addresses=(), firewall=False):
        if firewall is not False:
            self.firewall = firewall  # None when nothing is in the way
        chosen = self.selected_lobby()
        self.lobbies = list(lobbies)
        protocol = self.protocol()
        self.lobby_list.blockSignals(True)
        self.lobby_list.clear()
        for lobby in self.lobbies:
            item = QListWidgetItem(lan.describe(lobby, protocol))
            item.setData(Qt.ItemDataRole.UserRole, lobby)
            if not lan.joinable(lobby, protocol):
                item.setFlags(item.flags() & ~Qt.ItemFlag.ItemIsEnabled)
            self.lobby_list.addItem(item)
            if chosen and (lobby.address, lobby.port) == (chosen.address, chosen.port):
                self.lobby_list.setCurrentItem(item)
        if self.lobby_list.currentRow() < 0:
            first = next((i for i, lobby in enumerate(self.lobbies) if lan.joinable(lobby, protocol)), -1)
            self.lobby_list.setCurrentRow(first)
        self.lobby_list.blockSignals(False)
        count = len(self.lobbies)
        self.lan_status.setText(f'{count} game{"" if count == 1 else "s"} found. Select one and press Join.' if count else
                                'No games found yet. Ask the host to press Host and fly, and check that you are on the same network.')
        if addresses:
            self.lan_address.setText('Your game appears on other computers on this network as soon as you take off. '
                                     f'If someone has to join by address, yours is {addresses[0]}, port {self.prefs.port}.'
                                     + (f'\n\n{self.firewall}' if self.firewall else ''))
        self.lobby_selected()

    def selected_lobby(self):
        item = self.lobby_list.currentItem()
        return item.data(Qt.ItemDataRole.UserRole) if item else None

    def lobby_selected(self):
        lobby = self.selected_lobby()
        busy = bool(self.job) or self.session.running()
        self.join_button.setEnabled(bool(lobby) and lan.joinable(lobby, self.protocol()) and not busy)

    def host_lan(self):
        self.fly(dataclasses.replace(self.prefs, mode='multiplayer', host=True), lan=True,
                 message='Hosting your game · friends on this network can join from their Multiplayer page')

    def join_selected(self):
        lobby = self.selected_lobby()
        if not lobby or not lan.joinable(lobby, self.protocol()):
            self.error('Select a game you can join first.')
            return
        self.fly(dataclasses.replace(self.prefs, mode='multiplayer', host=False, server=lobby.address, port=lobby.port),
                 message=f'Joining {lobby.name}')

    def join_address(self):
        self.set_network(server=self.direct_address.text().strip(), port=self.direct_port.value())
        self.fly(dataclasses.replace(self.prefs, mode='multiplayer', host=False),
                 message=f'Connecting to {self.prefs.server}')

    def make_advanced(self):
        box = self.page('Fine tuning', 'Advanced', 'Adjust renderer settings with clear units. Longer distances and denser forest increase GPU cost.')
        form = self.form(box)
        cameras = [(v, t) for v, t in [('pursuit', 'Pursuit'), ('chase', 'Chase'), ('close-chase', 'Close chase'), ('cockpit', 'Cockpit'), ('orbit', 'Orbit'), ('free', 'Free camera')]]
        form.addRow('Starting camera', self.combo(cameras, self.prefs.camera, lambda v: self.set_pref('camera', v)))
        for key, title, tip in [('drawDistance', 'Draw distance (m)', 'How far terrain and haze are drawn, 40–250 km.'),
                                ('sceneryDistance', 'Tree distance (m)', 'Individual trees are drawn to this range, 1–15 km. Forest beyond it is shaded into the terrain.'),
                                ('treeDensity', 'Tree density (per km²)', 'Candidate trees per square kilometre of woodland.'),
                                ('lodBias', 'Model LOD bias', 'Log₂ distance bias. Positive values choose cheaper geometry sooner.'),
                                ('shadowDistance', 'Shadow distance (m)', 'Sun shadows are drawn out to this range from the camera.'),
                                ('glare', 'Glare amount', 'Fraction of light scattered around bright sources, 0–0.2.'),
                                ('realTime', 'Follow real-world time', 'The sun follows this computer\'s clock: morning, noon and dusk when they are outside.'),
                                ('visibilityKm', 'Visibility (km)', 'Meteorological visual range at sea level; sets the haze.'),
                                ('autoExposure', 'Automatic exposure', 'Meters the scene like a camera as the sun and weather change.'),
                                ('exposureCompensation', 'Exposure compensation (EV)', 'Photographic stops added to the metered exposure.'),
                                ('contrails', 'Contrails', ''), ('wingVapor', 'Wing vapour', ''),
                                ('engineHeat', 'Engine exhaust heat', ''), ('hud', 'Flight HUD', ''), ('playerLabels', 'Player labels', '')]:
            self.graphic(form, key, title, tip=tip)
        self.hardware_label = label('Detecting hardware…', 'muted')
        box.addWidget(self.hardware_label)
        box.addStretch()

    def make_updates(self):
        box = self.page('Release control', 'Updates', 'Updates are downloaded over HTTPS and verified before they are installed. Your settings and the previous version are kept.')
        state, state_box, _ = self.card('OpenFlightSim', 'download')
        self.update_label.setObjectName('factValue')
        state_box.addWidget(self.update_label)
        controls = QHBoxLayout()
        controls.setSpacing(10)
        check = button('Check for updates', self.check_updates)
        check.setIcon(icon('refresh', '#cfe4fb', 18))
        controls.addWidget(check)
        self.install_update_button = button('Download && install', self.install_update)
        self.install_update_button.setObjectName('primary')
        self.install_update_button.setIcon(icon('download', '#ffffff', 18))
        self.install_update_button.setEnabled(False)
        controls.addWidget(self.install_update_button)
        self.rollback_button = button('Roll back', self.do_rollback)
        self.rollback_button.setIcon(icon('undo', '#cfe4fb', 18))
        self.rollback_button.setToolTip('Return to the version that was installed before the last update.')
        controls.addWidget(self.rollback_button)
        controls.addStretch()
        state_box.addLayout(controls)
        box.addWidget(state)
        notes, notes_box, _ = self.card('Release notes', 'notes')
        self.notes = QPlainTextEdit()
        self.notes.setReadOnly(True)
        self.notes.setMinimumHeight(150)
        self.notes.setPlaceholderText('Release notes appear here after a check for updates.')
        notes_box.addWidget(self.notes, 1)
        box.addWidget(notes, 1)
        form = self.form(box)
        form.addRow('Release channel', self.combo([('stable', 'Stable'), ('development', 'Development / Experimental')], self.prefs.channel, self.change_channel))
        self.url = QLineEdit(self.prefs.manifest_url)
        self.url.setPlaceholderText('https://your-update-host/manifest.json')
        self.url.setToolTip('Where this launcher looks for new versions. An installed game sets this for you.')
        self.url.editingFinished.connect(self.change_endpoint)
        form.addRow('Update address', self.url)
        for key, title, text in [('auto_check', 'On start', 'Check for updates when the launcher opens'),
                                 ('auto_install', 'When idle', 'Install updates automatically while no flight is running')]:
            check = Toggle(text)
            check.setChecked(getattr(self.prefs, key))
            check.toggled.connect(lambda v, k=key: self.set_pref(k, v))
            form.addRow(title, check)

    def make_installation(self):
        box = self.page('Ground services', 'Installation / Repair', 'See where the game is installed, check its files and repair them. Your settings are never touched.')
        place, place_box, _ = self.card('This installation', 'folder')
        self.installation_label = Facts()
        place_box.addWidget(self.installation_label)
        folders = QHBoxLayout()
        folders.setSpacing(10)
        for text, symbol, action in (('Change folder…', 'folder', self.choose_installation),
                                     ('Open game folder', 'folder', lambda: self.open_folder(self.installation.directory if self.installation else self.prefs.installation)),
                                     ('Open logs folder', 'notes', lambda: self.open_folder(self.logs))):
            control = button(text, action)
            control.setIcon(icon(symbol, '#cfe4fb', 18))
            folders.addWidget(control)
        folders.addStretch()
        place_box.addLayout(folders)
        box.addWidget(place)
        files, files_box, _ = self.card('Verify and repair', 'shield')
        files_box.addWidget(label('Verifying compares every installed file with the published release. Repairing downloads only the files that differ.', 'muted'))
        services = QHBoxLayout()
        services.setSpacing(10)
        verify_button = button('Verify installation', self.verify_installation)
        verify_button.setObjectName('primary')
        verify_button.setIcon(icon('shield', '#ffffff', 18))
        services.addWidget(verify_button)
        repair_button = button('Repair affected files', self.repair_installation)
        repair_button.setIcon(icon('repair', '#cfe4fb', 18))
        services.addWidget(repair_button)
        services.addStretch()
        files_box.addLayout(services)
        self.verify_report = QPlainTextEdit()
        self.verify_report.setReadOnly(True)
        self.verify_report.setMinimumHeight(120)
        self.verify_report.setPlaceholderText('Verification results will appear here.')
        files_box.addWidget(self.verify_report, 1)
        box.addWidget(files, 1)
        box.addWidget(label(f'Your settings are kept in {self.data}', 'factKey'))

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
            for gallery, limit in ((self.home_aircraft, 4), (self.hangar_aircraft, None)):
                gallery.populate(self.installation.aircraft, self.installation.directory, limit)
            self.art.thumbnail = QPixmap(str(self.installation.directory / 'data/launcher/previews/hero.jpg'))
            self.art.update()
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
            build = self.installation.build
            self.installation_label.show_facts([
                ('Folder', self.installation.root),
                ('Version', f'OpenFlightSim {build["version"]} · {build.get("commit", "unknown")}'),
                ('Channel', str(build.get('channel', 'unknown')).capitalize()),
                ('Kind', 'Managed release' if self.installation.managed else 'Developer checkout / unpacked simulator'),
                *self.disk_usage()])
        except (LauncherError, OSError) as exc:
            if self.lease:
                self.lease.close()
                self.lease = None
            self.status.setText(str(exc))
            self.installation_label.show_facts([('Folder', root), ('Problem', exc)])
        self.select_aircraft()
        self.select_mode()
        self.sync_graphics()
        self.refresh_summary()

    def disk_usage(self):
        # Installed manifests already carry sizes; avoid recursively scanning a source checkout.
        if self.installation.managed:
            try:
                metadata = read_json(self.installation.directory / 'release.json')
                total = sum(r['size'] for r in metadata['files'].values())
                import shutil
                free = shutil.disk_usage(self.installation.root).free
                return [('Size', f'{total / 1024**3:.2f} GiB'), ('Free disk space', f'{free / 1024**3:.1f} GiB')]
            except (OSError, LauncherError, KeyError, TypeError):
                pass
        return []

    def select_aircraft(self):
        if self.installation:
            if not self.updating:
                self.prefs.aircraft = self.aircraft_combo.currentData()
                self.save()
            a = next((a for a in self.installation.aircraft if a['id'] == self.aircraft_combo.currentData()), None)
            self.loadout_row.setVisible(bool(a and a.get('bomber')))
            if a:
                self.aircraft_title.setText(a['name'].split(' | ')[0])
                self.aircraft_maker.setText(f'{a.get("manufacturer", "Manufacturer not provided")} · {a.get("type", "Aircraft")}')
                self.aircraft_detail.show_facts(aircraft_facts(a))
                preview = None
                if a.get('thumbnail'):
                    try:
                        preview = QPixmap(str(safe_path(self.installation.directory, a['thumbnail'])))
                    except LauncherError:
                        pass
                self.aircraft_art.thumbnail = preview
                self.aircraft_art.update()
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
        self.home_aircraft.setEnabled(not busy)
        self.quick_settings.setEnabled(not busy)
        self.pilot_button.setText(self.prefs.name)
        for index in range(1, self.stack.count()):
            self.stack.widget(index).setEnabled(not busy)
        self.stop_button.setVisible(self.session.running())
        self.play_button.setEnabled(bool(self.installation) and not busy and not self.graphics_error)
        self.hangar_play.setEnabled(self.play_button.isEnabled())
        if hasattr(self, 'host_button'):
            ready = bool(self.installation) and not busy and not self.graphics_error and 'multiplayer' in self.installation.catalog['modes']
            self.host_button.setEnabled(ready)
            self.direct_button.setEnabled(ready)
            self.lobby_selected()
        self.play_button.setText('In flight' if self.session.running() else 'Play')
        update = bool(self.release and self.installation and self.installation.managed) and not busy and Version(self.release.version) > Version(self.installation.build['version'])
        self.install_update_button.setEnabled(update)
        # Players should not have to find the Updates page to get a new version.
        self.update_button.setVisible(update)
        if update:
            self.update_button.setText(f'Update to {self.release.version}')
        self.rollback_button.setEnabled(bool(self.installation and self.installation.managed) and not busy)
        if self.installation:
            a = next((a for a in self.installation.aircraft if a['id'] == self.prefs.aircraft), None)
            self.version_label.setText(f'OpenFlightSim {self.installation.build["version"]}')
            self.version_label.setToolTip(f'Launcher {self.build["version"]} · {self.prefs.channel} channel')
            self.aircraft_label.setText(a['name'].split(' | ')[0] if a else 'Choose aircraft')
            self.home_aircraft.select(self.prefs.aircraft)
            self.hangar_aircraft.select(self.prefs.aircraft)
            g = self.graphics.values
            self.summary_label.setText(f'{MODES.get(self.prefs.mode, self.prefs.mode)}  ·  {self.prefs.camera.replace("-", " ").title()} camera')
        if not busy:
            self.operation_label.setText('Update available' if update else 'Ready for departure')
        elif not self.job:
            self.operation_label.setText('In flight')

    def start_background(self):
        if self.initial_error:
            self.error('Preferences could not be loaded: ' + self.initial_error)
        if self.graphics_error:
            self.error(self.graphics_error + '. Choose a graphics preset to restore valid settings; your original file is preserved until then.')
        screen = self.screen()
        self.updating = True
        modes = display_modes(screen)
        for widget in (self.resolution, self.home_resolution):
            widget.clear()
            for mode in modes:
                widget.addItem(f'{mode[0]} × {mode[1]}', mode)
        self.sync_graphics()
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
        self.operation_label.setText(title)
        self.transfer_label.setText('Preparing…')
        self.transfer_label.show()
        self.progress.setRange(0, 0)
        self.progress.show()
        self.cancel_button.show()
        self.show_operation('refresh')
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
        percent = max(0, min(100, int(done / max(total, 1) * 100)))
        self.progress.setValue(percent)
        if speed:
            seconds = max(0, int((total - done) / speed))
            remaining = f'{seconds // 60} min remaining' if seconds >= 60 else f'{seconds} sec remaining'
            self.transfer_label.setText(f'{done / 1024**2:.1f} / {total / 1024**2:.1f} MiB ({percent}%)')
            self.status.setText(f'{speed / 1024**2:.1f} MiB/s  ·  {remaining}')
        else:
            self.transfer_label.setText(f'{percent}% complete')
            self.status.setText(f'Verified / processed {done} of {total}')

    def job_finished(self):
        self.job.deleteLater()
        self.job = None
        self.progress.hide()
        self.transfer_label.hide()
        self.cancel_button.hide()
        self.show_operation('check')
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
        self.fly(self.prefs)

    def fly(self, preferences, lan=False, message='Simulator started · launcher remains open to manage this flight'):
        """Start a flight with `preferences`, which a multiplayer action may have adjusted for one flight."""
        if not self.installation:
            self.error('Choose a valid installation first.')
            return
        if self.job or self.session.running():
            self.error('Finish the current flight or operation first.')
            return
        try:
            self.session.start(self.installation, preferences, self.graphics, self.logs, self.data / 'runtime', lan=lan)
            self.status.setText(message)
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
            self.error('The game server on this computer stopped unexpectedly. Review server.log.')
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
            self.connection_label.setText('●  Update feed available')
            self.connection_label.setToolTip('Connected to the publisher update feed. Free flight also works offline.')
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
            self.show_page(9)
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
        self.lan_timer.stop()
        if self.scan:
            # A look for games takes well under a second; let it finish cleanly.
            self.scan.wait(3000)
        self.session.cleanup()
        if self.lease:
            self.lease.close()
        self.save()
        event.accept()
