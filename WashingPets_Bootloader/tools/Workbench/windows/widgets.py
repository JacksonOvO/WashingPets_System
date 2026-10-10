"""Qt equivalents of the original ui/widgets.py components."""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QWidget, QFrame, QLabel, QVBoxLayout, QHBoxLayout, QGridLayout, QPushButton, QProgressBar, QSizePolicy


def label(text, role='meta'):
    item = QLabel(text)
    item.setObjectName(role)
    return item


class Card(QFrame):
    def __init__(self, title='', role='card'):
        super().__init__()
        self.setObjectName(role)
        self.body = QVBoxLayout(self)
        self.body.setContentsMargins(20, 18, 20, 18)
        self.body.setSpacing(12)
        if title:
            self.heading = label(title, 'cardTitle' if role == 'card' else 'meta')
            self.body.addWidget(self.heading)
            if role == "card":
                self.body.addSpacing(12)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)


class ResponsiveGrid(QWidget):
    def __init__(self, minimum=140, columns=5, spacing=10, weights=None):
        super().__init__()
        self.minimum, self.columns = minimum, columns
        self.items = []
        self.weights = weights or [1] * columns
        self.grid = QGridLayout(self)
        self.grid.setContentsMargins(0, 0, 0, 0)
        self.grid.setSpacing(spacing)
        self.count = 0
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Maximum)

    def addWidget(self, item, *unused):
        self.items.append(item)
        self.reflow()

    def reflow(self):
        count = max(1, min(self.columns, (self.width() + self.grid.spacing()) // (self.minimum + self.grid.spacing())))
        if count == self.count and self.grid.count() == len(self.items):
            return
        for col in range(self.columns):
            self.grid.setColumnStretch(col, 0)
        while self.grid.count():
            self.grid.takeAt(0)
        for n, item in enumerate(self.items):
            self.grid.addWidget(item, n // count, n % count)
        for col in range(count):
            self.grid.setColumnStretch(col, self.weights[col] if count == self.columns else 1)
        self.count = count
        self.updateGeometry()

    def resizeEvent(self, event):
        self.reflow()
        super().resizeEvent(event)


class Tile(Card):
    def __init__(self, title, unit='', role='sensor', maximum=None):
        super().__init__('', role)
        self.body.setContentsMargins(18, 16, 18, 16)
        self.body.setSpacing(6)
        self.body.addWidget(label(title, 'tileTitle'))
        row = QHBoxLayout()
        self.value = label('--', 'sensorValue' if role == 'sensor' else 'metricValue' if role == 'metric' else 'actuatorValue')
        row.addWidget(self.value)
        if unit:
            row.addWidget(label(unit))
        row.addStretch()
        self.body.addLayout(row)
        self.setMinimumHeight(122 if role == 'sensor' else 102 if role == 'metric' else 88)
        self.maximum = maximum
        if maximum:
            self.bar = QProgressBar()
            self.bar.setObjectName('powerBar')
            self.bar.setRange(0, 1000)
            self.bar.setValue(0)
            self.bar.setTextVisible(False)
            self.bar.setFixedHeight(6)
            self.body.addWidget(self.bar)

    def set_value(self, value):
        self.value.setText(str(value))


class Toggle(QPushButton):
    def __init__(self, name, callback):
        super().__init__('OFF')
        self.setObjectName('outputToggle')
        self.setCheckable(True)
        self.setAccessibleName(name)
        self.toggled.connect(callback)
        self.toggled.connect(self.refresh_text)

    def refresh_text(self, checked):
        self.setText('ON' if checked else 'OFF')


class Switch(QPushButton):
    """Maintenance toggle with the same pill geometry as Gtk.Switch."""
    def __init__(self, name):
        super().__init__()
        self.setCheckable(True)
        self.setAccessibleName(name)
        self.setFixedSize(54, 30)

    def paintEvent(self, event):
        from PySide6.QtGui import QPainter, QColor
        from PySide6.QtCore import QRectF
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        if not self.isEnabled():
            p.setOpacity(.55)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor('#0071E3' if self.isChecked() else '#3A3A3C'))
        p.drawRoundedRect(QRectF(0, 0, 54, 30), 15, 15)
        p.setBrush(QColor('#F5F5F7'))
        p.drawEllipse(QRectF(27 if self.isChecked() else 3, 3, 24, 24))


class StatusBar(QFrame):
    def __init__(self):
        super().__init__()
        self.setObjectName('statusbarFrame')
        row = QHBoxLayout(self)
        row.setContentsMargins(20, 8, 20, 8)
        row.setSpacing(12)
        self.values = []
        for index in range(7):
            if index:
                row.addWidget(label('|', 'statusSeparator'))
            item = label('--', 'statusValue')
            row.addWidget(item)
            self.values.append(item)
        row.addStretch()
        self.io = label('--', 'statusValue')
        row.addWidget(self.io)

    def update_state(self, snapshot):
        import server as core
        from ui.i18n import tr, mode_name, state_name, process_name, recipe_name
        if not snapshot.get('connected'):
            parts = [tr('statusbar.offline')] + ['--'] * 6
            self.io.setText('--')
            self.values[-1].setStyleSheet('')
        else:
            m = snapshot.get('metrics', {})
            stage = m.get('stage', 0)
            stage_text = process_name(m.get('process_type', 0)) if stage == -1 else core.map_name(core.STAGES, stage)
            parts = [f"{mode_name(m.get('work_mode', 0))} / {state_name(m.get('device_state', 0))}",
                     recipe_name(m.get('recipe', 0)), f"{stage_text} {m.get('progress', 0) / 10:.1f}%"]
            for field, translation_key in [('total_remaining_ms', 'statusbar.flow_time'), ('stage_remaining_ms', 'statusbar.stage_time')]:
                ms = snapshot.get('process_time', {}).get(field, 0)
                parts.append(tr(translation_key).format(time=f'{ms // 60000}:{ms // 1000 % 60:02d}') if ms else '--')
            parts.append(tr('statusbar.step').format(n=m['step']) if m.get('step') else '--')
            alarms = snapshot.get('faultAlarms', [])
            parts.append(tr('statusbar.fault_count').format(active=sum(a.get('status') == 'active' for a in alarms), total=len(alarms)) if alarms else tr('statusbar.no_faults'))
            color = '#FF3B30' if snapshot.get('fault', {}).get('active_code') else '#34C759'
            self.values[-1].setStyleSheet(f'color: {color};')
            self.io.setText(snapshot.get('ioSummary', '--'))
        for item, value in zip(self.values, parts):
            item.setText(value)
