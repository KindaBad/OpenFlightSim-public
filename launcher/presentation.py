"""Launcher presentation widgets. Artwork is optional; game data drives the cards."""
from PySide6.QtCore import Qt, QRectF, QSize, Signal
from PySide6.QtGui import QColor, QIcon, QLinearGradient, QPainter, QPainterPath, QPixmap, QPen
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtWidgets import (QWidget, QLabel, QPushButton, QVBoxLayout, QBoxLayout,
                             QGridLayout, QSizePolicy, QComboBox, QCheckBox, QSpinBox,
                             QDoubleSpinBox)

from .storage import LauncherError, safe_path


STYLE = """
QWidget { background: #0b141e; color: #edf3fc; font-family: 'Segoe UI', 'DejaVu Sans'; font-size: 14px; }
QMainWindow, QWidget#surface { background: #0b141e; }
QLabel, QWidget#page, QWidget#gallery, QWidget#heroCopy { background: transparent; }
QLabel#eyebrow { color: #a7c5e7; font-size: 10px; font-weight: 600; letter-spacing: 3px; }
QLabel#title { font-size: 28px; font-weight: 600; }
QLabel#detailTitle { font-size: 21px; font-weight: 600; }
QLabel#factKey { color: #8ea5c0; font-size: 12px; }
QLabel#factValue { color: #dde8f5; }
QLabel#error { color: #ff9d8a; }
QLabel#success { color: #7fdca4; }
QLabel#percent { font-size: 13px; font-weight: 600; color: #cfe4fb; }
QLabel#sectionTitle { font-size: 17px; font-weight: 600; }
QLabel#muted, QLabel#cardRole { color: #9db2cc; }
QLabel#cardRole { font-size: 12px; }
QLabel#aircraftName { font-size: 14px; font-weight: 600; }
QLabel#flightAircraft { font-size: 15px; font-weight: 600; }
QLabel#flightSummary { color: #b6c9e1; font-size: 12px; }
QLabel#operationTitle { font-size: 15px; font-weight: 600; }
QLabel#transfer { color: #a9bed9; font-size: 12px; }
QFrame#header { background: #0d1824; border-bottom: 1px solid #172838; }
QFrame#sidebar { background: #09121b; }
QFrame#card, QFrame#downloadBar { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #101e2b, stop:1 #0e1923); border: 1px solid #1e3041; border-radius: 10px; }
QListWidget { background: transparent; border: none; outline: none; padding: 0; }
QListWidget::item { padding: 17px 16px; color: #a7bdd7; border-radius: 6px; margin: 3px 0; border-left: 3px solid transparent; }
QListWidget::item:hover { background: #11263a; color: white; }
QListWidget::item:selected { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0c345a, stop:1 #102944); color: #f7fbff; border-left: 3px solid #168bff; }
QPushButton { background: #192737; border: 1px solid #2c4157; border-radius: 7px; padding: 10px 18px; font-weight: 600; }
QPushButton:hover { background: #223a52; border-color: #438acf; }
QPushButton:focus { border-color: #58acff; }
QPushButton:pressed { background: #0f2337; }
QPushButton:disabled { color: #687d95; background: #14212e; border-color: #233241; }
QPushButton#play { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #078eff, stop:1 #0055ed); color: white; border: 1px solid #37a4ff; font-size: 23px; padding: 14px 30px; }
QPushButton#play:hover { background: #168fff; border-color: #80c5ff; }
QPushButton#play:disabled { background: #223b54; color: #879eb8; border-color: #35516c; }
QPushButton#heroSettings { background: rgba(19, 33, 48, 210); font-size: 16px; padding: 17px 22px; }
QPushButton#updateAction { background: #0d365d; border-color: #247fd2; }
QPushButton#primary { background: #0a62d0; border-color: #37a4ff; color: white; padding: 12px 22px; }
QPushButton#primary:hover { background: #168fff; border-color: #80c5ff; }
QPushButton#primary:disabled { background: #223b54; color: #879eb8; border-color: #35516c; }
QListWidget#lobbies { background: #0d1924; border: 1px solid #1e3041; border-radius: 6px; padding: 4px; }
QListWidget#lobbies::item { padding: 11px 12px; margin: 1px 0; border-left: 3px solid transparent; }
QListWidget#lobbies::item:disabled { color: #62788e; }
QPushButton#link, QPushButton#profile, QPushButton#sideLink { background: transparent; border-color: transparent; color: #b4c9e2; padding: 8px 4px; font-weight: 400; }
QPushButton#link:hover, QPushButton#profile:hover, QPushButton#sideLink:hover { color: #55abff; }
QPushButton#sideLink { padding: 8px 18px; text-align: left; }
QPushButton#settingsTab { background: transparent; border: none; border-bottom: 2px solid transparent; border-radius: 0; color: #9db2cc; padding: 11px 13px; }
QPushButton#settingsTab:checked { color: #63b3ff; border-bottom-color: #168bff; }
QPushButton#settingsTab:hover { color: white; background: #112438; }
QPushButton#aircraftCard { background: #14212e; border: 2px solid #223546; border-radius: 8px; padding: 0; text-align: left; }
QPushButton#aircraftCard:hover { border-color: #4c83b4; background: #1a2b3b; }
QPushButton#aircraftCard:checked { border: 2px solid #168bff; background: #162b40; }
QPushButton#aircraftCard:focus { border-color: #8dcaff; }
QPushButton#aircraftCard QLabel { background: transparent; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QPlainTextEdit { background: #182635; border: 1px solid #2b4055; border-radius: 6px; padding: 8px; selection-background-color: #1268ac; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #2699ff; }
QComboBox { padding-right: 24px; min-height: 20px; }
QSpinBox, QDoubleSpinBox { padding-right: 28px; min-height: 20px; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 24px; border: none; border-left: 1px solid #2b4055; border-top-right-radius: 6px; background: transparent; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 24px; border: none; border-left: 1px solid #2b4055; border-bottom-right-radius: 6px; background: transparent; }
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: #223a52; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow, QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { width: 0; height: 0; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { color: #687d95; background: #14212e; border-color: #233241; }
QPlainTextEdit { background: #0d1924; border-color: #1e3041; padding: 10px; }
QComboBox::drop-down { border: none; width: 25px; }
QComboBox::down-arrow { width: 0; height: 0; }
QComboBox QAbstractItemView { background: #142638; selection-background-color: #145687; color: #edf3fc; padding: 4px; }
QCheckBox { background: transparent; spacing: 10px; padding: 5px; }
QCheckBox::indicator { width: 17px; height: 17px; border: 1px solid #426079; border-radius: 4px; background: #0b1723; }
QCheckBox::indicator:checked { background: #168bff; border: 2px solid #85c6ff; }
QProgressBar { background: #223447; border: none; border-radius: 4px; min-height: 8px; max-height: 8px; }
QProgressBar::chunk { background: #168bff; border-radius: 4px; }
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: #0d1924; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: #29445e; border-radius: 4px; min-height: 32px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QToolTip { background: #193149; color: #eef7ff; border: 1px solid #438acf; padding: 8px; }
"""


# Small source SVGs keep icons crisp at every desktop scale without a font dependency.
ICON_PATHS = {
    'wing': '<path fill="currentColor" stroke="none" d="M2 9 23 2 18 11 7 15 5 12 17 7 4 11Z M7 17 17 13 14 18 10 20Z M10 22 14 20 12 23Z"/>',
    'home': '<path d="m3 11 9-8 9 8M5 10v11h5v-7h4v7h5V10"/>',
    'aircraft': '<path d="m12 2 2 8 8 5v3l-8-2-1 5h-2l-1-5-8 2v-3l8-5Z"/>',
    'settings': '<path d="M3 6h18M3 12h18M3 18h18"/><circle cx="8" cy="6" r="2.5"/><circle cx="16" cy="12" r="2.5"/><circle cx="9" cy="18" r="2.5"/>',
    'download': '<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',
    'repair': '<path d="m14 5 5-2a6 6 0 0 1-7 8L4 21l-3-3 9-8a6 6 0 0 1 1-7l-1 5Z"/>',
    'support': '<circle cx="12" cy="12" r="9"/><path d="M9 9a3 3 0 1 1 5 2c-2 1-2 2-2 3m0 3v.1"/>',
    'notes': '<path d="M5 2h9l5 5v15H5ZM14 2v6h5M8 12h8M8 16h8"/>',
    'monitor': '<rect x="2" y="3" width="20" height="14" rx="1"/><path d="M12 17v4m-5 0h10"/>',
    'refresh': '<path d="M20 9a8 8 0 0 0-14-4L3 8m0-6v6h6M4 15a8 8 0 0 0 14 4l3-3m0 6v-6h-6"/>',
    'play': '<path fill="currentColor" stroke="none" d="M6 3v18l15-9Z"/>',
    'pilot': '<circle cx="12" cy="8" r="4"/><path d="M4 22v-3a8 8 0 0 1 16 0v3"/>',
    'chevron': '<path d="m9 4 8 8-8 8"/>',
    'close': '<path d="m5 5 14 14M5 19 19 5"/>',
    'check': '<circle cx="12" cy="12" r="9"/><path d="m8 12 3 3 5-6"/>',
    'folder': '<path d="M3 5h6l2 3h10v11H3Z"/>',
    'shield': '<path d="M12 3 4 6v6c0 5 3.5 8 8 9 4.5-1 8-4 8-9V6Z"/><path d="m9 12 2 2 4-4"/>',
    'undo': '<path d="M8 5 3 10l5 5M3 10h12a5 5 0 0 1 0 10h-4"/>',
    'network': '<circle cx="12" cy="5" r="2.5"/><circle cx="5" cy="19" r="2.5"/><circle cx="19" cy="19" r="2.5"/><path d="M12 7.5v4.5l-5.3 5M12 12l5.3 5"/>',
}


def icon(name, color='#9bb9db', size=24):
    body = ICON_PATHS[name].replace('currentColor', color)
    svg = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">{body}</svg>'
    renderer = QSvgRenderer(svg.encode())
    pixmap = QPixmap(size * 2, size * 2)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    renderer.render(painter)
    painter.end()
    pixmap.setDevicePixelRatio(2)
    return QIcon(pixmap)


def wordmark(size):
    widget = QLabel(f'<span style="font-size:{size}px; font-weight:700;">Open<span style="color:#359fff; font-style:italic;">FlightSim</span></span>')
    widget.setTextFormat(Qt.TextFormat.RichText)
    widget.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)
    return widget


class CoverArt(QWidget):
    """Paint a cached image with a focal point and an optional readable text overlay."""
    def __init__(self, hero=False, focal=.5):
        super().__init__()
        self.hero, self.focal = hero, focal
        self.thumbnail = None
        self.setMinimumWidth(0)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        if hero:
            self.setMinimumHeight(350)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        rect = QRectF(self.rect())
        clip = QPainterPath()
        clip.addRoundedRect(rect, 10 if self.hero else 5, 10 if self.hero else 5)
        painter.setClipPath(clip)
        painter.fillRect(rect, QColor('#173750'))
        if self.thumbnail and not self.thumbnail.isNull():
            pixmap = self.thumbnail
            scale = max(self.width() / pixmap.width(), self.height() / pixmap.height())
            width, height = self.width() / scale, self.height() / scale
            left = max(0, min(pixmap.width() - width, pixmap.width() * self.focal - width / 2))
            focal_y = .32 if self.hero else .5
            top = max(0, min(pixmap.height() - height, pixmap.height() * focal_y - height / 2))
            source = QRectF(left, top, width, height)
            painter.drawPixmap(rect, pixmap, source)
        else:
            gradient = QLinearGradient(0, 0, self.width(), self.height())
            gradient.setColorAt(0, QColor('#163b59'))
            gradient.setColorAt(1, QColor('#274966'))
            painter.fillRect(rect, gradient)
            # Older installations and third-party catalogue entries need no artwork.
            plane = icon('aircraft', '#7ca1c4', 64)
            plane.paint(painter, int(self.width() * .7) - 32, self.height() // 2 - 32, 64, 64)
        if self.hero:
            shade = QLinearGradient(0, 0, self.width(), 0)
            shade.setColorAt(0, QColor(4, 12, 21, 230))
            shade.setColorAt(.35, QColor(5, 15, 26, 175))
            shade.setColorAt(.65, QColor(7, 17, 26, 30))
            shade.setColorAt(1, QColor(5, 15, 26, 0))
            painter.fillRect(rect, shade)
            floor = QLinearGradient(0, 0, 0, self.height())
            floor.setColorAt(0, QColor(4, 12, 21, 0))
            floor.setColorAt(1, QColor(4, 12, 21, 65))
            painter.fillRect(rect, floor)
        painter.end()


class AircraftCard(QPushButton):
    def __init__(self, aircraft, root, height):
        super().__init__()
        self.aircraft_id = aircraft['id']
        self.setObjectName('aircraftCard')
        self.setCheckable(True)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.setFixedHeight(height + 92)
        self.setMinimumWidth(130)
        name = aircraft['name'].split(' | ')[0]
        self.setAccessibleName(f'Select {name}')
        self.setToolTip(name)
        box = QVBoxLayout(self)
        box.setContentsMargins(3, 3, 3, 12)
        box.setSpacing(6)
        self.art = CoverArt()
        self.art.setFixedHeight(height)
        try:
            if aircraft.get('thumbnail'):
                self.art.thumbnail = QPixmap(str(safe_path(root, aircraft['thumbnail'])))
        except LauncherError:
            pass
        box.addWidget(self.art)
        title = QLabel(name)
        title.setObjectName('aircraftName')
        title.setWordWrap(True)
        title.setFixedHeight(40)
        title.setContentsMargins(9, 0, 6, 0)
        box.addWidget(title)
        role = QLabel(aircraft.get('type', 'Military' if aircraft['armed'] else 'Aircraft'))
        role.setObjectName('cardRole')
        role.setContentsMargins(9, 0, 6, 0)
        box.addWidget(role)
        for child in (self.art, title, role):
            child.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)


class AircraftGallery(QWidget):
    selected = Signal(str)

    def __init__(self, height=120, card_width=158):
        super().__init__()
        self.setObjectName('gallery')
        self.image_height, self.card_width = height, card_width
        self.cards = {}
        self.columns = 0
        self.grid = QGridLayout(self)
        self.grid.setContentsMargins(0, 0, 0, 0)
        self.grid.setSpacing(12)

    def populate(self, aircraft, root, limit=None):
        while self.grid.count():
            self.grid.takeAt(0).widget().deleteLater()
        self.cards = {}
        for entry in aircraft[:limit]:
            card = AircraftCard(entry, root, self.image_height)
            card.clicked.connect(lambda checked=False, key=entry['id']: self.selected.emit(key))
            self.cards[entry['id']] = card
        self.reflow()

    def reflow(self):
        # Allow cards to wrap on smaller displays instead of compressing their labels.
        columns = max(1, min(4, self.width() // self.card_width))
        for column in range(4):
            self.grid.setColumnStretch(column, 0)
        for index, card in enumerate(self.cards.values()):
            self.grid.addWidget(card, index // columns, index % columns)
        for column in range(min(columns, len(self.cards))):
            self.grid.setColumnStretch(column, 1)
        self.columns = columns

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if max(1, min(4, self.width() // self.card_width)) != self.columns:
            self.reflow()

    def select(self, aircraft_id):
        for key, card in self.cards.items():
            card.setChecked(key == aircraft_id)


class ResponsiveRow(QWidget):
    """Stack side-by-side panels when the available content width becomes narrow."""
    def __init__(self, narrow=1010):
        super().__init__()
        self.setObjectName('gallery')
        self.narrow = narrow
        self.box = QBoxLayout(QBoxLayout.Direction.LeftToRight, self)
        self.box.setContentsMargins(0, 0, 0, 0)
        self.box.setSpacing(16)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        direction = QBoxLayout.Direction.TopToBottom if self.width() < self.narrow else QBoxLayout.Direction.LeftToRight
        if self.box.direction() != direction:
            self.box.setDirection(direction)


class DropDown(QComboBox):
    """A clear dropdown indicator independent of the operating system's theme."""
    def paintEvent(self, event):
        super().paintEvent(event)
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setPen(QPen(QColor('#a6c4e6' if self.isEnabled() else '#62788e'), 1.6))
        path = QPainterPath()
        path.moveTo(self.width() - 20, self.height() / 2 - 2)
        path.lineTo(self.width() - 16, self.height() / 2 + 2)
        path.lineTo(self.width() - 12, self.height() / 2 - 2)
        painter.drawPath(path)
        painter.end()


class Toggle(QCheckBox):
    """A switch. The whole control is the click target, not only the track."""
    def __init__(self, text=''):
        super().__init__(text)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

    def sizeHint(self):
        text = self.fontMetrics().horizontalAdvance(self.text()) + 14 if self.text() else 0
        return QSize(44 + text, 30)

    def minimumSizeHint(self):
        return self.sizeHint()

    def hitButton(self, position):
        return self.rect().contains(position)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        on, enabled = self.isChecked(), self.isEnabled()
        track = QRectF(2, (self.height() - 20) / 2, 38, 20)
        if self.hasFocus():
            painter.setPen(QPen(QColor('#8dcaff'), 1.4))
        else:
            painter.setPen(QPen(QColor('#3c5a76' if not on else '#3aa0ff'), 1))
        painter.setBrush(QColor(('#168bff' if on else '#16273a') if enabled else ('#27425c' if on else '#14212e')))
        painter.drawRoundedRect(track, 10, 10)
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(('#ffffff' if on else '#9db2cc') if enabled else '#62788e'))
        painter.drawEllipse(QRectF(track.right() - 17 if on else track.left() + 3, track.top() + 3, 14, 14))
        if self.text():
            painter.setPen(QColor('#edf3fc' if enabled else '#687d95'))
            painter.drawText(self.rect().adjusted(52, 0, 0, 0),
                             Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft, self.text())
        painter.end()


class Stepper:
    """Up and down chevrons for spin boxes, drawn like the dropdown's."""
    def paintEvent(self, event):
        super().paintEvent(event)
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setPen(QPen(QColor('#a6c4e6' if self.isEnabled() else '#62788e'), 1.5))
        x, middle = self.width() - 12, self.height() / 2
        for y, direction in ((middle - 7, -1), (middle + 7, 1)):
            path = QPainterPath()
            path.moveTo(x - 3.5, y - direction * 1.8)
            path.lineTo(x, y + direction * 1.8)
            path.lineTo(x + 3.5, y - direction * 1.8)
            painter.drawPath(path)
        painter.end()


class SpinBox(Stepper, QSpinBox):
    pass


class DoubleSpinBox(Stepper, QDoubleSpinBox):
    pass


class WrapLabel(QLabel):
    """Wrapped text that claims its full height in a form or grid row."""
    def __init__(self, text='', name=None):
        super().__init__(text)
        if name:
            self.setObjectName(name)
        self.setWordWrap(True)
        self.setTextFormat(Qt.TextFormat.PlainText)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)

    def hasHeightForWidth(self):
        # Layouts would otherwise reserve room for a narrower, taller wrapping.
        return False

    def fit(self):
        # Measured here: the label's own answers guess at a width, and never fall
        # below a height it was given earlier.
        height = self.fontMetrics().boundingRect(0, 0, self.width(), 10000,
                                                 int(Qt.TextFlag.TextWordWrap), self.text()).height()
        if height > 0 and (self.minimumHeight(), self.maximumHeight()) != (height, height):
            self.setFixedHeight(height)

    def setText(self, text):
        super().setText(text)
        self.fit()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.fit()

    def showEvent(self, event):
        # The style sheet's font arrives after the first hidden layout.
        super().showEvent(event)
        self.fit()


class Facts(QWidget):
    """A short table of labelled values, such as an aircraft's data sheet."""
    def __init__(self):
        super().__init__()
        self.setObjectName('gallery')
        self.grid = QGridLayout(self)
        self.grid.setContentsMargins(0, 0, 0, 0)
        self.grid.setHorizontalSpacing(18)
        self.grid.setVerticalSpacing(9)
        self.grid.setColumnStretch(1, 1)

    def show_facts(self, pairs):
        while self.grid.count():
            old = self.grid.takeAt(0).widget()
            old.setParent(None)
            old.deleteLater()
        for row, (key, value) in enumerate(pairs):
            name = QLabel(key)
            name.setObjectName('factKey')
            self.grid.addWidget(name, row, 0, Qt.AlignmentFlag.AlignTop)
            self.grid.addWidget(WrapLabel(str(value), 'factValue'), row, 1)

    def text(self):
        return '\n'.join(self.grid.itemAt(i).widget().text() for i in range(self.grid.count()))


class Banner(QWidget):
    """The installer's header. It is painted, as no game artwork exists before installation."""
    def __init__(self, tagline):
        super().__init__()
        self.setFixedHeight(148)
        box = QVBoxLayout(self)
        box.setContentsMargins(26, 0, 26, 0)
        box.setSpacing(6)
        box.addStretch()
        eyebrow = QLabel('FLIGHT SIMULATOR')
        eyebrow.setObjectName('eyebrow')
        box.addWidget(eyebrow)
        box.addWidget(wordmark(34))
        line = QLabel(tagline)
        line.setStyleSheet('color: #c9d9ea;')
        box.addWidget(line)
        box.addStretch()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(self.rect())
        clip = QPainterPath()
        clip.addRoundedRect(rect, 10, 10)
        painter.setClipPath(clip)
        sky = QLinearGradient(0, 0, self.width(), self.height())
        sky.setColorAt(0, QColor('#0c1d2e'))
        sky.setColorAt(.55, QColor('#10385c'))
        sky.setColorAt(1, QColor('#1a5f93'))
        painter.fillRect(rect, sky)
        painter.setPen(QPen(QColor(120, 180, 235, 26), 1))
        for offset in range(-2, 12):
            painter.drawLine(int(self.width() * .45) + offset * 46, self.height(),
                             int(self.width() * .45) + offset * 46 + self.height(), 0)
        size = self.height() - 44
        painter.setOpacity(.55)
        icon('wing', '#5fb6ff', size).paint(painter, self.width() - size - 30, 22, size, size)
        painter.end()
