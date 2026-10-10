"""Qt Widgets pages for the Windows PawClean workbench."""

from __future__ import annotations

import threading
import time

from PySide6.QtCore import Qt, Signal, QObject
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QFormLayout, QGridLayout,
    QHBoxLayout, QLabel, QLineEdit, QMessageBox,
    QPushButton, QScrollArea,
    QVBoxLayout, QWidget,
)

from windows.widgets import Card, ResponsiveGrid, Tile, Toggle, Switch, label

import server as core
from ui.i18n import (tr, recipe_name, mode_name, state_name, process_name,
                     sensor_name, actuator_name, get_lang)

SENSORS = [
    ("cabin_ext_temp", "℃"), ("cabin_ext_humi", "%"),
    ("cabin_oxygen", "%"), ("inlet_water_temp", "℃"),
    ("tank_water_temp", "℃"), ("outlet_water_temp", "℃"),
    ("reserve_temp", "℃"), ("cabin_temp", "℃"),
    ("cabin_humidity", "%"), ("cabin_ozone", "ppm"),
]
ACTUATORS = [
    ("left_fan_pct", "%"), ("right_fan_pct", "%"),
    ("bottom_fan_pct", "%"), ("left_heater_pct", "%"),
    ("right_heater_pct", "%"), ("bottom_heater_pct", "%"),
    ("booster_pressure", "kg"), ("valve_angle", "°"),
]
SERIES = [
    ("test.series.temp", "cabinTemp", "#3290ff"),
    ("test.series.humidity", "cabinHumidity", "#ef9f20"),
    ("test.series.oxygen", "cabinOxygen", "#9670ff"),
    ("test.series.ozone", "cabinOzone", "#a0b3cc"),
    ("test.series.fan", "fan", "#30c26e"),
    ("test.series.heater", "heater", "#f34c42"),
]


def L(zh: str, en: str) -> str:
    return zh if get_lang() == "zh" else en


def button(label: str, callback) -> QPushButton:
    widget = QPushButton(label)
    widget.clicked.connect(callback)
    return widget


def group(title, parent_layout):
    box = Card(title)
    parent_layout.addWidget(box)
    return box, box.body


def tiles(specs, name, role='sensor', minimum=140, columns=5):
    grid = ResponsiveGrid(minimum, columns)
    for key, unit in specs:
        maximum = (100 if unit == '%' else 90 if unit == '°' else 10) if role == 'actuator' else None
        grid.addWidget(Tile(name(key), unit, role, maximum))
    return grid


def tile_value(grid, row, value):
    grid.items[row].set_value(value)


def divider():
    from PySide6.QtWidgets import QFrame
    line = QFrame()
    line.setFrameShape(QFrame.Shape.HLine)
    line.setStyleSheet('color: #48484A;')
    return line


class Page(QWidget):
    def __init__(self):
        super().__init__()
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        outer.addWidget(scroll)
        content = QWidget()
        content.setObjectName("pageContent")
        scroll.setWidget(content)
        self.body = QVBoxLayout(content)
        self.body.setContentsMargins(48, 40, 42, 40)
        self.body.setSpacing(16)

    def finish(self):
        self.body.addStretch(1)

    def update_state(self, state):
        pass


class Curve(QWidget):
    def __init__(self):
        super().__init__()
        self.setMinimumHeight(360)
        self.samples = []
        self.enabled = {"cabinTemp", "cabinHumidity"}

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.fillRect(self.rect(), QColor('#0f141c'))
        plot = self.rect().adjusted(58, 24, -24, -38)
        p.setPen(QPen(QColor('#2b3b4a'), 1))
        p.drawRect(plot)
        for n in range(6):
            y = plot.top() + n * plot.height() / 5
            p.setPen(QColor('#24303d'))
            p.drawLine(plot.left(), int(y), plot.right(), int(y))
            p.setPen(QColor('#8c9eb3'))
            p.drawText(14, int(y) + 4, str(100 - n * 20))
        if not self.samples:
            p.drawText(plot.adjusted(24, 24, 0, 0), Qt.AlignmentFlag.AlignTop, tr('test.curve_empty'))
            return
        maximum = max(1.0, self.samples[-1].get('seconds') or 1.0)
        legend_x = plot.left() + 10
        for key, field, color in SERIES:
            if field not in self.enabled:
                continue
            p.setPen(QPen(QColor(color), 2.2))
            last = None
            stride = max(1, len(self.samples) // max(1, plot.width()))
            visible = self.samples[::stride]
            if visible[-1] is not self.samples[-1]:
                visible = visible + [self.samples[-1]]
            for sample in visible:
                if sample.get(field) is None:
                    continue
                x = plot.left() + (sample.get('seconds') or 0) / maximum * plot.width()
                y = plot.bottom() - max(0, min(100, float(sample[field]))) / 100 * plot.height()
                if last:
                    p.drawLine(int(last[0]), int(last[1]), int(x), int(y))
                last = x, y
            p.fillRect(legend_x, plot.top() + 10, 12, 8, QColor(color))
            p.setPen(QColor('#d9e3ed'))
            p.drawText(legend_x + 18, plot.top() + 20, tr(key))
            legend_x += max(110, p.fontMetrics().horizontalAdvance(tr(key)) + 36)


class DashboardPage(Page):
    def __init__(self):
        super().__init__()
        self.hero = Card(role='heroCard')
        self.hero.body.setContentsMargins(28, 24, 28, 24)
        self.body.addWidget(self.hero)
        self.status = label(tr('dash.offline'), 'hero')
        self.meta = label(tr('dash.offline_hint'))
        self.hero.body.addWidget(self.status)
        self.hero.body.addSpacing(8)
        self.hero.body.addWidget(self.meta)
        self.metrics = ResponsiveGrid(170, 4, 12)
        for key in ('dash.total_remaining', 'dash.stage_remaining', 'dash.current_step', 'dash.active_faults'):
            self.metrics.addWidget(Tile(tr(key), role='metric'))
        self.body.addWidget(self.metrics)
        _, content = group(tr('dash.sensor_data'), self.body)
        self.sensors = tiles(SENSORS, sensor_name)
        content.addWidget(self.sensors)
        _, content = group(tr('dash.actuator_status'), self.body)
        self.actuators = tiles(ACTUATORS, actuator_name, 'actuator', 160, 4)
        content.addWidget(self.actuators)
        row = QHBoxLayout()
        self.interaction_card, content = group(tr('dash.interaction_req'), row)
        self.interaction = QLabel('--')
        self.interaction.setWordWrap(True)
        content.addWidget(self.interaction)
        self.interaction_card.hide()
        _, content = group(tr('dash.recent_faults'), row)
        self.faults = QLabel('--')
        content.addWidget(self.faults)
        self.body.addLayout(row)
        self.finish()

    def update_state(self, state):
        if not state.get("connected"):
            self.status.setText(tr("dash.offline"))
            self.meta.setText(tr("dash.offline_hint"))
            return
        m = state.get("metrics", {})
        self.status.setText(state_name(m.get("device_state", 0)))
        stage = m.get("stage", 0)
        stage_text = (process_name(m.get("process_type", 0)) if stage == -1
                      else core.map_name(core.STAGES, stage))
        self.meta.setText(f"{mode_name(m.get('work_mode', 0))} · {stage_text} · "
                          f"{tr('dash.recipe')} {m.get('recipe', 0) + 1} · "
                          f"{m.get('progress', 0) / 10:.1f}%")
        times = state.get("process_time", {})
        for row, key in enumerate(("total_remaining_ms", "stage_remaining_ms")):
            ms = times.get(key, 0)
            tile_value(self.metrics, row, f"{ms // 60000}:{ms // 1000 % 60:02d}" if ms else "--")
        tile_value(self.metrics, 2, m.get("step") or "--")
        alarms = state.get("faultAlarms", [])
        tile_value(self.metrics, 3, sum(a.get("status") == "active" for a in alarms))
        values = state.get("telemetry_ui", {})
        for row, (key, unit) in enumerate(SENSORS):
            value = values.get(key)
            tile_value(self.sensors, row, f"{value:.1f}" if value is not None else "--")
        values = state.get("power_ui", {})
        for row, (key, unit) in enumerate(ACTUATORS):
            value = values.get(key)
            tile_value(self.actuators, row, f"{value:.1f}" if value is not None else "--")
        for tile, (key, _) in zip(self.actuators.items, ACTUATORS):
            tile.bar.setValue(round((values.get(key) or 0) / tile.maximum * 1000))
        pending = state.get("pendingInteraction")
        self.interaction_card.setVisible(bool(pending))
        self.interaction.setText(core.describe_interaction(pending.get("stage", 0), pending.get("type", 0)) if pending else "--")
        self.faults.setText("\n".join(f"0x{a.get('fault_code', 0):04X}  "
                                      f"{core.FAULT_CODES.get(a.get('fault_code', 0), '?')}"
                                      for a in alarms[-3:]) or "--")


class TelemetryPage(Page):
    def __init__(self):
        super().__init__()
        _, content = group(tr("tel.sensor_data"), self.body)
        self.sensors = tiles(SENSORS, sensor_name, minimum=150)
        content.addWidget(self.sensors)
        self.flags = QLabel("--")
        content.addWidget(self.flags)
        _, content = group(tr("tel.process_time"), self.body)
        self.times = label("--", "metricValue")
        content.addWidget(self.times)
        _, content = group(tr("tel.realtime_curves"), self.body)
        content.addWidget(label(tr("tel.curves_hint")))
        self.curve = Curve()
        content.addWidget(self.curve)
        self.finish()

    def update_state(self, state):
        values = state.get("telemetry_ui", {})
        for row, (key, unit) in enumerate(SENSORS):
            value = values.get(key)
            tile_value(self.sensors, row, f"{value:.1f}" if value is not None else "--")
        self.flags.setText(f"{tr('tel.status_flags')} {state.get('flags', '--')}")
        pt = state.get("process_time", {})
        def remaining(field):
            ms = pt.get(field, 0)
            return f'{ms // 60000}:{ms // 1000 % 60:02d}' if ms else '--'
        self.times.setText(f"{tr('tel.total')}: {remaining('total_remaining_ms')}    {tr('tel.stage')}: {remaining('stage_remaining_ms')}")
        samples = state.get("samples", [])
        if samples != self.curve.samples:
            self.curve.samples = samples
            self.curve.update()


class FaultsPage(Page):
    def __init__(self, send):
        super().__init__()
        self.send = send
        self.active, content = group('', self.body)
        row = QHBoxLayout()
        self.current = label('0x0000', 'faultCode')
        row.addWidget(self.current)
        self.fault_name = QLabel(tr('fault.no_fault'))
        row.addWidget(self.fault_name, 1)
        self.severity = label(tr('fault.normal'), 'badge')
        row.addWidget(self.severity)
        content.addLayout(row)
        self.fault_meta = label('--')
        content.addWidget(self.fault_meta)
        reset = button(tr('fault.reset'), lambda: send('process', value=7))
        reset.setProperty('role', 'stop')
        content.addWidget(reset)
        _, content = group(tr('fault.history'), self.body)
        self.history_summary = label('--')
        content.addWidget(self.history_summary)
        history_box = QWidget()
        history_box.setMinimumHeight(350)
        self.history = QVBoxLayout(history_box)
        self.history.setAlignment(Qt.AlignmentFlag.AlignTop)
        content.addWidget(history_box)
        self.last_alarms = None
        self.finish()

    def update_state(self, state):
        fault = state.get('fault', {})
        code = fault.get('active_code', 0)
        self.current.setText(f"0x{code:04X}")
        self.fault_name.setText(core.FAULT_CODES.get(code, tr("fault.unknown")) if code else tr("fault.no_fault"))
        severity = core.FAULT_SEVERITIES.get(fault.get('severity', 0), '--')
        self.fault_meta.setText(f"{severity} · ID {fault.get('event_id', 0)}")
        self.severity.setText(severity if code else tr('fault.normal'))
        self.severity.setProperty('failed', bool(code))
        self.severity.style().unpolish(self.severity)
        self.severity.style().polish(self.severity)
        if not code:
            self.fault_meta.clear()
        color = ('#FF3B30' if fault.get('severity', 0) >= 3 else '#FF9500' if fault.get('severity') == 2 else '#5AC8FA') if code else '#34C759'
        self.active.setStyleSheet(f'QFrame#card {{ border-left: 4px solid {color}; }}')
        alarms = [a for a in state.get('faultAlarms', []) if a.get('fault_code')]
        self.history_summary.setText(f"{tr('fault.total_events')}: {len(alarms)}  |  {tr('fault.active_label')}: {sum(a.get('status') == 'active' for a in alarms)}  |  {tr('fault.resolved_label')}: {sum(a.get('status') == 'resolved' for a in alarms)}")
        if alarms == self.last_alarms:
            return
        self.last_alarms = [dict(a) for a in alarms]
        while self.history.count():
            self.history.takeAt(0).widget().deleteLater()
        if not alarms:
            self.history.addWidget(label(tr("fault.no_history")))
        for a in reversed(alarms):
            row = QWidget()
            layout = QHBoxLayout(row)
            badge = label(core.FAULT_SEVERITIES.get(a.get('severity', 0), '?'), 'badge')
            badge.setProperty('failed', a.get('status') == 'active')
            layout.addWidget(badge)
            code = a['fault_code']
            layout.addWidget(QLabel(f"0x{code:04X}"))
            layout.addWidget(QLabel(core.FAULT_CODES.get(code, '?')), 1)
            layout.addWidget(label(core.FAULT_EVENT_TYPES.get(a.get('event_type', 0), '?')))
            layout.addWidget(label(f"#{a.get('event_id', 0)}"))
            self.history.addWidget(row)


class ControlPage(Page):
    def __init__(self, send):
        super().__init__()
        self.send = send
        self.body.setContentsMargins(44, 70, 44, 70)
        self.body.setSpacing(76)
        self.pending = label('', 'pending')
        self.nack = label('', 'nack')
        self.pending.hide()
        self.nack.hide()
        self.body.addWidget(self.pending)
        self.body.addWidget(self.nack)
        mode_card, content = group(tr('ctrl.mode_recipe'), self.body)
        mode_card.setMinimumHeight(272)
        top = QHBoxLayout()
        left = QVBoxLayout()
        left.addWidget(label(tr('ctrl.work_mode')))
        segment = QWidget()
        segment.setObjectName('segmented')
        row = QHBoxLayout(segment)
        row.setContentsMargins(2, 2, 2, 2)
        for key, action, values in [('ctrl.manual', 'mode', {'value': 1}), ('ctrl.auto', 'mode', {'value': 2}), ('ctrl.query', 'query', {})]:
            row.addWidget(button(tr(key), lambda _=False, a=action, v=values: send(a, **v)))
        left.addWidget(segment)
        top.addLayout(left, 1)
        right = QVBoxLayout()
        right.addWidget(label(tr('ctrl.recipe')))
        row = QHBoxLayout()
        self.recipe = QComboBox()
        self.recipe.addItems([recipe_name(i) for i in range(26)])
        row.addWidget(self.recipe, 1)
        select = button(tr('ctrl.select'), lambda: send('recipe', index=self.recipe.currentIndex()))
        select.setProperty('role', 'primary')
        row.addWidget(select)
        right.addLayout(row)
        top.addLayout(right, 1)
        content.addLayout(top)
        content.addWidget(divider())
        content.addWidget(label(tr('ctrl.process_actions')))
        row = QHBoxLayout()
        for value, key in ((1, 'ctrl.start'), (2, 'ctrl.pause'), (3, 'ctrl.continue'), (4, 'ctrl.end')):
            btn = button(tr(key), lambda _=False, v=value: send('process', value=v))
            if value in (1, 4):
                btn.setProperty('role', 'controlStart' if value == 1 else 'stop')
            row.addWidget(btn)
        content.addLayout(row)
        bottom = QHBoxLayout()
        door_card, content = group(tr('ctrl.door_control'), bottom)
        door_card.setMinimumHeight(314)
        row = QGridLayout()
        for n, (key, value) in enumerate((('ctrl.open_top', 1), ('ctrl.close_top', 4), ('ctrl.open_bottom', 2), ('ctrl.close_bottom', 3), ('ctrl.auto_close', 7))):
            row.addWidget(button(tr(key), lambda _=False, v=value: send('interaction', type=v)), n // 2, n % 2, 1, 2 if n == 4 else 1)
        content.addLayout(row)
        content.addStretch()
        stage_card, content = group(tr('ctrl.stage_maint'), bottom)
        stage_card.setMinimumHeight(314)
        row = QHBoxLayout()
        self.stage = QComboBox()
        self.stage.addItems(core.STAGES)
        row.addWidget(self.stage, 1)
        row.addWidget(button(tr('ctrl.stage_test'), lambda: send('process', value=5, param=self.stage.currentIndex())))
        content.addLayout(row)
        row = QHBoxLayout()
        row.addWidget(button(tr('ctrl.self_clean'), lambda: send('process', value=6)))
        reset = button(tr('ctrl.fault_reset'), lambda: send('process', value=7))
        reset.setProperty('role', 'stop')
        row.addWidget(reset)
        content.addLayout(row)
        content.addWidget(divider())
        grid = QGridLayout()
        self.info = []
        for n, key in enumerate(('ctrl.current_mode', 'ctrl.current_recipe', 'ctrl.current_stage', 'ctrl.current_step', 'ctrl.run_state')):
            grid.addWidget(label(tr(key)), n // 2, (n % 2) * 2)
            value = QLabel('--')
            self.info.append(value)
            grid.addWidget(value, n // 2, (n % 2) * 2 + 1)
        content.addLayout(grid)
        self.body.addLayout(bottom)
        self.finish()

    def update_state(self, state):
        pending = state.get("pendingInteraction")
        self.pending.setText(core.describe_interaction(pending.get("stage", 0), pending.get("type", 0)) if pending else "")
        nack = state.get("last_nack")
        self.nack.setText(nack.get("text", "") if nack else "")
        self.pending.setVisible(bool(pending))
        self.nack.setVisible(bool(nack))
        m = state.get("metrics", {})
        if not m:
            for item in self.info:
                item.setText("--")
            return
        for row, value in enumerate((process_name(m.get("process_type", 0)),
                                     recipe_name(m.get("recipe", 0)),
                                     process_name(m.get("process_type", 0)) if m.get("stage") == -1 else core.map_name(core.STAGES, m.get("stage", 0)),
                                     m.get("step", "--"),
                                     state_name(m.get("device_state", 0)))):
            self.info[row].setText(str(value))


class IOPage(Page):
    def __init__(self, send):
        super().__init__()
        self.send = send
        _, content = group(tr("io.rs485_manual"), self.body)
        self.station = QComboBox()
        self.station.addItems(core.RS485_STATIONS)
        self.value = QLineEdit('0')
        self.actuator = QCheckBox(tr('io.gpio_actuator'))
        self.actuator.toggled.connect(lambda checked: send('actuatorMode', mode=2 if checked else 1))
        controls = [(tr('io.station'), self.station), (tr('io.value'), self.value),
                    (tr('io.pump'), button(tr('io.start_pump'), lambda: send('booster', start=True))),
                    ('', button(tr('io.stop_pump'), lambda: send('booster', start=False))),
                    ('', button(tr('io.set_rs485'), self.set_rs485))]
        for (_, widget), role in zip(controls[2:], ("start", "stop", "primary")):
            widget.setProperty("role", role)
        row = ResponsiveGrid(180, 6)
        for title, widget in controls:
            box = QWidget()
            layout = QVBoxLayout(box)
            layout.setContentsMargins(0, 0, 0, 0)
            layout.addWidget(label(title or ' '))
            layout.addWidget(widget)
            row.addWidget(box)
        content.addWidget(row)
        self.hint = label(tr('io.rs485_hint'))
        self.hint.setWordWrap(True)
        content.addWidget(self.hint)
        _, content = group(tr('io.gpio_outputs'), self.body)
        content.addWidget(label(tr('io.toggle_hint')))
        self.output_status = label(tr('io.no_status'))
        content.addWidget(self.output_status)
        grid = ResponsiveGrid(250, 4, 8)
        self.outputs = []
        for index, name in enumerate(core.OUTPUT_NAMES):
            if index == 39:
                content.addWidget(grid)
                content.addWidget(label("MAGLOCK1–6", "cardTitle"))
                grid = ResponsiveGrid(250, 4, 8)
            box = Card(role='outputItem')
            box.body.setContentsMargins(14, 10, 14, 10)
            row = QHBoxLayout()
            row.addWidget(QLabel(name), 1)
            check = Toggle(name, lambda checked, oid=index + 1: send('output', output_id=oid, state=checked))
            row.addWidget(check)
            box.body.addLayout(row)
            self.outputs.append(check)
            grid.addWidget(box)
        content.addWidget(grid)
        _, content = group(tr('io.gpio_inputs'), self.body)
        grid = ResponsiveGrid(200, 4)
        self.inputs = []
        for index, name in enumerate(core.INPUT_NAMES):
            if index == core.INPUT_GPIO_COUNT:
                content.addWidget(grid)
                content.addWidget(label(L("独立关门到位 PF15 / PG0", "Independent Door Closed Inputs PF15 / PG0"), "cardTitle"))
                grid = ResponsiveGrid(200, 4)
            box = Card(role='inputItem')
            box.body.setContentsMargins(12, 8, 12, 8)
            item = QLabel('○ ' + name)
            box.body.addWidget(item)
            self.inputs.append(item)
            grid.addWidget(box)
        content.addWidget(grid)
        self.finish()

    def set_rs485(self):
        index = self.station.currentIndex()
        station = int(core.RS485_STATIONS[index].split(" ", 1)[0])
        try:
            if station == 1:
                pressure = float(self.value.text())
                value = round(pressure * 10)
                if not 0 <= pressure <= 10 or abs(pressure * 10 - value) > 1e-6:
                    raise ValueError
            else:
                value = int(self.value.text())
                if not 0 <= value <= (90 if station == 2 else 100):
                    raise ValueError
        except ValueError:
            self.hint.setText(tr("io.rs485_invalid"))
            return
        self.hint.setText(tr("io.rs485_hint"))
        self.send("rs485", station=index, value=value)

    def update_state(self, state):
        io = state.get("io_bools", {})
        inputs, outputs = io.get("inputs", []), io.get("outputs", [])
        self.output_status.setText(f"{len(inputs)} IN · {len(outputs)} OUT" if inputs else tr("io.no_status"))
        for index, check in enumerate(self.outputs):
            check.blockSignals(True)
            check.setChecked(bool(index < len(outputs) and outputs[index]))
            check.refresh_text(check.isChecked())
            check.blockSignals(False)
        for index, label in enumerate(self.inputs):
            on = index < len(inputs) and inputs[index]
            label.setText(("● " if on else "○ ") + core.INPUT_NAMES[index])
            label.setStyleSheet("color: #34C759;" if on else "color: #98989D;")
        self.actuator.blockSignals(True)
        self.actuator.setChecked(state.get("actuator_mode") == 2)
        self.actuator.blockSignals(False)


class TaskSignals(QObject):
    finished = Signal(object, object)


class ParamsPage(Page):
    def __init__(self, send, state):
        super().__init__()
        self.send = send
        self.state = state
        self.dirty = set()
        self.current_hardware = {}
        self.hw_busy = False
        self.hw_session = None
        self.task_signals = TaskSignals()
        self.task_signals.finished.connect(self.hw_done)
        engineering = QWidget()
        content = QVBoxLayout(engineering)
        content.setContentsMargins(0, 0, 0, 0)
        content.setSpacing(10)
        content.addWidget(label(L('工程配置', 'Engineering configuration'), 'cardTitle'))
        self.body.addWidget(engineering)
        form = QFormLayout()
        form.setHorizontalSpacing(18)
        form.setVerticalSpacing(10)
        form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.FieldsStayAtSizeHint)
        self.fan = QComboBox()
        self.fan.addItems([L("独立可控硅", "Independent thyristors"), L("四路 PWM 模块", "Four-channel PWM")])
        form.addRow(L("风机驱动", "Fan drive"), self.fan)
        self.pump = QComboBox()
        self.pump.addItems(["GPIO OUT31", "RS485"])
        form.addRow(L("增压泵控制", "Booster pump drive"), self.pump)
        self.channels = []
        for name in ("CH1 进水温度", "CH2 水箱温度", "CH3 管路出水温度", "CH4 混水温度"):
            check = QCheckBox(L("真实采集", "Real sensor"))
            self.channels.append(check)
            form.addRow(name, check)
        self.lower = Switch(L('下门门磁旁路', 'Lower door bypass'))
        self.host = Switch(L('上位机命令旁路', 'Host command bypass'))
        form.addRow(L('下门门磁旁路', 'Lower door bypass'), self.lower)
        form.addRow(L('上位机命令旁路', 'Host command bypass'), self.host)
        content.addLayout(form)
        row = QHBoxLayout()
        self.hw_read = button(L("读取", "Read"), lambda: self.hw_task(self.state.read_hardware))
        self.hw_apply = button(L("应用并保存", "Apply and save"), self.apply_hardware)
        row.addWidget(self.hw_read)
        row.addWidget(self.hw_apply)
        row.addStretch()
        content.addLayout(row)
        self.hw_active = QLabel(L('未连接设备', 'Disconnected'))
        self.hw_warning = label('', 'pending')
        self.hw_warning.setWordWrap(True)
        self.hw_status = QLabel('')
        content.addWidget(self.hw_active)
        content.addWidget(self.hw_warning)
        content.addWidget(self.hw_status)
        self.hw_dirty = False
        self.hw_syncing = False
        self.fan.currentIndexChanged.connect(self.hardware_changed)
        self.pump.currentIndexChanged.connect(self.hardware_changed)
        for widget in [*self.channels, self.lower, self.host]:
            widget.toggled.connect(self.hardware_changed)
        self.body.addSpacing(28)
        _, content = group(tr("param.recipe_config"), self.body)
        row = QHBoxLayout()
        self.recipe = QComboBox()
        self.recipe.addItems([recipe_name(i) for i in range(26)])
        row.addWidget(self.recipe)
        for key, action in (("param.read", "readParam"), ("param.write_ram", "writeParam"),
                            ("param.save_flash", "saveParam"), ("param.restore_default", "restoreParam")):
            btn = button(tr(key), lambda _=False, a=action: send(a))
            btn.setProperty('role', {'writeParam': 'primary', 'saveParam': 'start', 'restoreParam': 'stop'}.get(action, ''))
            row.addWidget(btn)
        row.addStretch()
        content.addLayout(row)
        self.revision = QLabel("--")
        content.addWidget(self.revision)
        _, content = group(tr("param.parameters"), self.body)
        grid = ResponsiveGrid(220, 4)
        self.entries = {}
        for index, field in enumerate(core.RECIPE_FIELDS):
            box = Card(role="parameter")
            inner = box.body
            inner.setContentsMargins(14, 12, 14, 12)
            inner.addWidget(label(field["label"], "tileTitle"))
            edit = QLineEdit("0")
            edit.textEdited.connect(lambda _, key=field["key"]: self.dirty.add(key))
            inner.addWidget(edit)
            self.entries[field["key"]] = edit
            grid.addWidget(box)
        content.addWidget(grid)
        self.finish()

    def hardware_changed(self, *_):
        if not self.hw_syncing:
            self.hw_dirty = True
            self.hw_status.setText(L("有未应用的修改", "Unapplied changes"))

    def get_recipe_index(self):
        return self.recipe.currentIndex()

    def get_revision(self):
        return getattr(self, "_revision", 0)

    def get_dirty_params(self):
        return {key: float(self.entries[key].text()) for key in self.dirty}

    def mark_clean(self):
        self.dirty.clear()

    def hw_values(self):
        return {"fan_pwm": self.fan.currentIndex(),
                "booster_mode": self.pump.currentIndex(),
                "temperature_mask": sum(1 << i for i, c in enumerate(self.channels) if c.isChecked()),
                "lower_door_bypass": int(self.lower.isChecked()),
                "host_command_bypass": int(self.host.isChecked())}

    def apply_hardware(self):
        values = self.hw_values()
        risks = []
        if values["lower_door_bypass"]:
            risks.append(L("下门开关判断将被跳过", "Lower door check will be bypassed"))
        if values["host_command_bypass"]:
            risks.append(L("流程命令等待将被跳过", "Host command wait will be bypassed"))
        if values["temperature_mask"] != 15:
            risks.append(L("部分温度将使用模拟值", "Some temperatures will be simulated"))
        if risks and QMessageBox.warning(self, L("维护配置", "Maintenance configuration"),
                                         "\n".join(risks), QMessageBox.StandardButton.Ok |
                                         QMessageBox.StandardButton.Cancel) != QMessageBox.StandardButton.Ok:
            return
        self.hw_task(lambda: self.state.apply_hardware(values))

    def hw_task(self, operation):
        if self.hw_busy:
            return
        self.hw_busy = True
        for widget in [self.hw_read, self.hw_apply, self.fan, self.pump, self.lower, self.host, *self.channels]:
            widget.setEnabled(False)
        self.hw_status.setText(L("正在与设备通信...", "Communicating with device..."))
        def run():
            try:
                result = operation()
                self.task_signals.finished.emit(result, None)
            except Exception as exc:
                self.task_signals.finished.emit(None, str(exc))
        threading.Thread(target=run, daemon=True).start()

    def hw_done(self, result, error):
        self.hw_busy = False
        self.hw_status.setText(error or L("配置已确认", "Configuration confirmed"))
        if not error:
            self.hw_dirty = False
        self.update_state(self.state.snapshot(include_logs=False, include_samples=False))

    def update_state(self, state):
        self._revision = state.get("paramRevision", 0)
        self.revision.setText(f"{tr('param.version')} {self._revision}")
        for field in core.RECIPE_FIELDS:
            key = field["key"]
            if key not in self.dirty and key in state.get("parameters", {}):
                self.entries[key].setText(str(state["parameters"][key]))
        hardware = state.get("hardware", {}) if state.get("connected") else {}
        if state.get("hardwareSession") != self.hw_session:
            self.hw_session = state.get("hardwareSession")
            self.current_hardware = {}
            self.hw_dirty = False
        editable = (bool(hardware) and not self.hw_busy and not hardware.get("busy")
                    and state.get("metrics", {}).get("work_mode") == 1
                    and state.get("metrics", {}).get("device_state") in (0, 6, 7))
        self.hw_read.setEnabled(state.get("connected", False) and not self.hw_busy)
        self.hw_apply.setEnabled(editable)
        for widget in [self.fan, self.pump, self.lower, self.host, *self.channels]:
            widget.setEnabled(editable)
        if hardware and not self.hw_dirty and not self.hw_busy:
            self.hw_syncing = True
            self.current_hardware = dict(hardware)
            self.fan.setCurrentIndex(hardware.get("fan_pwm", 0))
            self.pump.setCurrentIndex(hardware.get("booster_mode", 0))
            for index, check in enumerate(self.channels):
                check.setChecked(bool(hardware.get("temperature_mask", 0) & (1 << index)))
            self.lower.setChecked(bool(hardware.get("lower_door_bypass")))
            self.host.setChecked(bool(hardware.get("host_command_bypass")))
            self.hw_syncing = False
        if hardware:
            driver = self.fan.itemText(hardware.get('fan_pwm', 0))
            saved = L('未保存 Flash', 'Flash not saved') if hardware.get('save_pending') else L('已保存 Flash', 'Flash saved')
            phase = L('切换中', 'Switching') if hardware.get('busy') else L('已生效', 'Active')
            self.hw_active.setText(f"{driver} · {self.pump.itemText(hardware.get('booster_mode', 0))} · {L('温度掩码', 'Temperature mask')} 0x{hardware.get('temperature_mask', 0):02X} · {L('版本', 'Revision')} {hardware.get('revision')} · {phase} · {saved}")
            risks = []
            if hardware.get('lower_door_bypass'):
                risks.append(L('下门门磁已旁路', 'Lower door bypass active'))
            if hardware.get('host_command_bypass'):
                risks.append(L('上位机命令等待已旁路', 'Host command bypass active'))
            simulated = [f'CH{i+1}' for i in range(4) if not hardware.get('temperature_mask', 0) & (1 << i)]
            if simulated:
                risks.append('/'.join(simulated) + L(' 为模拟温度', ' simulated temperature'))
            self.hw_warning.setText('；'.join(risks))
        else:
            self.hw_active.setText(L('当前配置未读取', 'Configuration not read') if state.get('connected') else L('未连接设备', 'Disconnected'))
            self.hw_warning.clear()
        self.hw_warning.setVisible(bool(self.hw_warning.text()))


class TestPage(Page):
    def __init__(self, send, state):
        super().__init__()
        self.send = send
        self.state = state
        self.command_at = None
        self.last_nack = None
        self.body.setContentsMargins(44, 36, 38, 36)
        self.body.setSpacing(8)
        _, content = group(tr("test.card_record"), self.body)
        columns = ResponsiveGrid(280, 3, 10, weights=[41, 27, 30])
        conditions = Card(tr('test.conditions'), 'subgroup')
        self.interval = QLineEdit('30')
        self.stage = QComboBox()
        self.stage.addItems(core.STAGES)
        self.stage.setCurrentIndex(6)
        for title, widget in ((tr('test.interval'), self.interval), (tr('test.stage'), self.stage)):
            conditions.body.addWidget(label(title))
            conditions.body.addWidget(widget)
        hint = label(tr('test.cond_hint'))
        hint.setWordWrap(True)
        conditions.body.addWidget(hint)
        conditions.setMinimumHeight(256)
        columns.addWidget(conditions)
        series = Card(tr('test.series'), 'subgroup')
        self.series_checks = {}
        for key, field, _ in SERIES:
            check = QCheckBox(tr(key))
            check.setChecked(field in ('cabinTemp', 'cabinHumidity'))
            check.toggled.connect(self.update_series)
            series.body.addWidget(check)
            self.series_checks[field] = check
        series.setMinimumHeight(256)
        columns.addWidget(series)
        execution = Card(tr('test.exec'), 'subgroup')
        for key, callback, role in [('test.start', self.start_test, 'start'), ('test.stop_save', self.stop_test, 'stop')]:
            btn = button(tr(key), callback)
            btn.setProperty('role', role)
            btn.setMinimumHeight(42)
            execution.body.addWidget(btn)
        self.status = label(tr('test.status_idle'))
        self.status.setWordWrap(True)
        execution.body.addWidget(self.status)
        self.device_label = label(tr('test.device'))
        self.device_label.setWordWrap(True)
        execution.body.addWidget(self.device_label)
        execution.setMinimumHeight(248)
        columns.addWidget(execution)
        content.addWidget(columns)
        _, content = group(tr('test.card_selftest'), self.body)
        row = QHBoxLayout()
        for key, action, args, role in [('test.selftest_quick', 'selfTest', {'mode': 0}, 'start'), ('test.selftest_deep', 'selfTest', {'mode': 1}, 'start'), ('test.selftest_stop', 'process', {'value': 4}, 'stop')]:
            btn = button(tr(key), lambda _=False, a=action, v=args: send(a, **v))
            btn.setProperty('role', role)
            btn.setMinimumWidth(115)
            row.addWidget(btn)
        hint = label(tr('test.selftest_hint'))
        hint.setWordWrap(True)
        row.addWidget(hint, 1)
        content.addLayout(row)
        self.selftest = label(tr('test.selftest_no_result'))
        self.selftest.setWordWrap(True)
        content.addWidget(self.selftest)
        self.selftest_chips = ResponsiveGrid(150, 5)
        content.addWidget(self.selftest_chips)
        self.last_result = None
        _, content = group(tr('test.card_curve'), self.body)
        self.curve_summary = label('')
        content.addWidget(self.curve_summary)
        self.curve = Curve()
        content.addWidget(self.curve)
        self.update_series()
        self.finish()

    def update_series(self):
        if not hasattr(self, "curve"):
            return
        self.curve.enabled = {field for field, check in self.series_checks.items() if check.isChecked()} or {"cabinTemp"}
        self.curve.update()
        self.update_curve_summary()

    def update_curve_summary(self):
        selected = "、".join(tr(key) for key, field, _ in SERIES if field in self.curve.enabled)
        self.curve_summary.setText(tr("test.curve_summary").format(series=selected, interval=f"{self.state.sample_interval:g}", stage=self.stage.currentText()))

    def start_test(self):
        if not self.state.running:
            self.status.setText(L("请先连接 PawClean 设备", "Connect the PawClean device first"))
            return
        with self.state.lock:
            mode = self.state.status.get("metrics", {}).get("work_mode")
        if mode is not None and mode != 1:
            self.status.setText(tr("err.stage_test_mode"))
            return
        try:
            interval = float(self.interval.text())
            if not 1 <= interval <= 3600:
                raise ValueError
        except ValueError:
            self.status.setText(L("记录间隔应为 1–3600 秒", "Interval must be 1–3600 seconds"))
            return
        with self.state.lock:
            self.state.samples.clear()
            self.state.test_running = True
            self.state.test_started_at = time.time()
            self.state.last_sample_at = 0
            self.state.sample_interval = interval
        self.curve.samples = []
        self.curve.update()
        previous = self.state.status.get("last_nack") or {}
        self.last_nack = (previous.get("error"), previous.get("detail")) if previous else None
        self.command_at = time.monotonic()
        self.status.setText(tr("test.cmd_sent"))
        self.send("process", value=5, param=self.stage.currentIndex())

    def stop_test(self):
        self.send("process", value=4)
        with self.state.lock:
            self.state.test_running = False
            self.curve.samples = list(self.state.samples)
        csv_path = core.save_samples()
        if csv_path:
            png = csv_path.with_suffix(".png")
            self.curve.grab().save(str(png), "PNG")
            self.state.add_log("CHART SAVED: " + str(png))
            self.status.setText(f"CSV: {csv_path}\nPNG: {png}")
        else:
            self.status.setText(tr("test.status_idle"))
        self.command_at = None

    def update_state(self, state):
        metrics = state.get('metrics', {})
        if state.get('connected') and metrics:
            stage = metrics.get('stage', 0)
            stage_text = process_name(metrics.get('process_type', 0)) if stage == -1 else core.map_name(core.STAGES, stage)
            self.device_label.setText(tr('test.device_prefix') + ' · '.join([mode_name(metrics.get('work_mode', 0)), state_name(metrics.get('device_state', 0)), process_name(metrics.get('process_type', 0)), stage_text]))
        else:
            self.device_label.setText(tr('test.device'))
        self.update_curve_summary()
        samples = state.get("samples", [])
        if samples != self.curve.samples:
            self.curve.samples = samples
            self.curve.update()
        if samples and state.get("testRunning"):
            self.status.setText(tr("test.status").format(state=tr("test.status_running"), n=len(samples)))
        result = state.get("self_test")
        if result:
            lines = [result.get("summary", "")]
            lines += [f"{'✓' if item.get('passed') else '✗'} {item.get('name', '')}"
                      for item in result.get("items", [])]
            self.selftest.setText(result.get('summary', ''))
            if result != self.last_result:
                self.last_result = result
                for widget in self.selftest_chips.items:
                    widget.deleteLater()
                self.selftest_chips.items.clear()
                self.selftest_chips.count = 0
                for item in result.get('items', []):
                    chip = label(('✓ ' if item.get('passed') else '✗ ') + item.get('name', ''), 'badge')
                    chip.setProperty('failed', not item.get('passed'))
                    self.selftest_chips.addWidget(chip)
        nack = state.get("last_nack")
        if self.command_at and nack and nack.get("request_cmd") == core.CMD_PROCESS:
            sig = (nack.get("error"), nack.get("detail"))
            if sig != self.last_nack:
                self.last_nack = sig
                self.state.test_running = False
                self.status.setText(nack.get("text", L("设备拒绝测试", "Test rejected")))
                self.command_at = None
        if self.command_at and state.get("metrics", {}).get("work_mode") == 3:
            self.status.setText(tr("test.device_entered"))
            self.command_at = None
        if self.command_at and time.monotonic() - self.command_at > 8:
            self.status.setText(tr("test.no_response"))
