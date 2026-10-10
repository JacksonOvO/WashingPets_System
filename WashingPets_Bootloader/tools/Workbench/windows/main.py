"""PawClean Windows desktop entry point (run with python -m windows.main)."""

from __future__ import annotations

import csv
import os
import struct
import sys
import threading
import time
from pathlib import Path

# The development entry point lives under Workbench/windows. PyInstaller also
# uses Workbench as an import path so the existing GTK-free backend is bundled.
WORKBENCH = Path(__file__).resolve().parents[1]
if str(WORKBENCH) not in sys.path:
    sys.path.insert(0, str(WORKBENCH))
if sys.stdout is None:
    sys.stdout = open(os.devnull, "w", encoding="utf-8")
if sys.stderr is None:
    log_dir = Path.home() / "AppData" / "Local" / "PawClean"
    log_dir.mkdir(parents=True, exist_ok=True)
    sys.stderr = (log_dir / "pawclean-errors.log").open("a", encoding="utf-8")

from PySide6.QtCore import QObject, QStandardPaths, QTimer, Signal
from PySide6.QtGui import QIcon
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QHBoxLayout, QLabel, QMainWindow,
    QMessageBox, QPushButton, QStackedWidget, QVBoxLayout, QWidget,
)

import server as core
from ui.i18n import get_lang, set_lang, tr
from windows.theme import STYLE
from windows.widgets import label, StatusBar
from windows.iap import IapClient, IapError, inspect_image, parse_version, port_names
from windows.pages import (L, ControlPage, DashboardPage, FaultsPage, IOPage,
                           ParamsPage, TelemetryPage, TestPage)
from windows.upgrade_page import UpgradePage


def desktop_dir() -> Path:
    path = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.DesktopLocation)
    return Path(path) if path else Path.home() / "Desktop"


def save_samples() -> Path | None:
    with core.STATE.lock:
        rows = list(core.STATE.samples)
    if not rows:
        core.STATE.add_log("TEST STOP: 没有收到遥测点")
        return None
    directory = desktop_dir() / "测试曲线"
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / (time.strftime("PawClean_%Y%m%d_%H%M%S") + ".csv")
    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=[
            "time", "seconds", "cabinTemp", "cabinHumidity", "cabinOxygen",
            "cabinOzone", "fan", "heater"])
        writer.writeheader()
        writer.writerows(rows)
    core.STATE.add_log("TEST SAVED: " + str(path))
    return path


core.list_ports = port_names
core.desktop_dir = desktop_dir
core.save_samples = save_samples


class WorkerSignals(QObject):
    done = Signal(str, object, object)
    log = Signal(str)
    progress = Signal(int, str)


class WorkbenchWindow(QMainWindow):
    PAGE_KEYS = ["dashboard", "telemetry", "faults", "control", "io", "params", "test", "upgrade"]
    PAGE_LABELS = ["nav.dashboard", "nav.telemetry", "nav.faults", "nav.control",
                   "nav.io", "nav.params", "nav.test", "nav.upgrade"]

    def __init__(self):
        super().__init__()
        self.setWindowTitle("PawClean Workbench")
        self.resize(1400, 900)
        icon_root = Path(getattr(sys, "_MEIPASS", WORKBENCH))
        icon = icon_root / "pawclean-app-icon.png"
        if icon.exists():
            self.setWindowIcon(QIcon(str(icon)))
        self.state = core.STATE
        self.busy = False
        self.worker = None
        self.worker_signals = WorkerSignals()
        self.worker_signals.done.connect(self.worker_done)
        self.worker_signals.log.connect(self._upgrade_log)
        self.worker_signals.progress.connect(self._upgrade_progress)
        self.pending_settings = None
        self.was_connected = False
        self.last_snapshot = {}
        self._build()
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh)
        self.timer.start(500)
        self.poll_timer = QTimer(self)
        self.poll_timer.timeout.connect(self.poll)
        self.poll_timer.start(1000)

    def _build(self):
        root = QWidget()
        self.setCentralWidget(root)
        layout = QHBoxLayout(root)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        nav = QWidget()
        nav.setObjectName("sidebar")
        nav.setFixedWidth(210)
        nav_layout = QVBoxLayout(nav)
        nav_layout.setContentsMargins(16, 28, 16, 20)
        nav_layout.setSpacing(4)
        title = QLabel(tr("brand.title"))
        self.brand_title = title
        title.setObjectName("brand")
        title.setContentsMargins(12, 0, 0, 0)
        nav_layout.addWidget(title)
        self.brand_subtitle = label(tr("brand.subtitle"), "subtitle")
        self.brand_subtitle.setContentsMargins(12, 0, 0, 0)
        nav_layout.addWidget(self.brand_subtitle)
        nav_layout.addSpacing(38)
        self.section_labels = []
        self.nav_buttons = []
        for index, key in enumerate(self.PAGE_LABELS):
            if index in (0, 3, 7):
                nav_layout.addSpacing(16)
                section_key = {0: 'nav.section.monitor', 3: 'nav.section.control', 7: 'nav.section.engineer'}[index]
                section = label(tr(section_key), 'sectionTitle')
                section.setContentsMargins(8, 0, 0, 0)
                self.section_labels.append((section, section_key))
                nav_layout.addWidget(section)
            entry = QPushButton(tr(key))
            entry.setObjectName("nav")
            entry.clicked.connect(lambda _=False, i=index: self.show_page(i))
            nav_layout.addWidget(entry)
            self.nav_buttons.append(entry)
        nav_layout.addStretch()
        layout.addWidget(nav)

        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.setSpacing(0)
        header_box = QWidget()
        header_box.setObjectName('header')
        header = QHBoxLayout(header_box)
        header.setContentsMargins(20, 12, 20, 12)
        header.setSpacing(12)
        self.connection = QLabel(tr("header.disconnected"))
        self.connection.setObjectName("connection")
        header.addWidget(self.connection)
        self.port = QComboBox()
        self.port.setEditable(True)
        self.port.setFixedWidth(180)
        header.addWidget(self.port)
        self.refresh_ports_btn = QPushButton(tr("header.refresh"))
        self.refresh_ports_btn.clicked.connect(self.refresh_ports)
        header.addWidget(self.refresh_ports_btn)
        self.connect_btn = QPushButton(tr("header.connect"))
        self.connect_btn.setProperty("role", "primary")
        self.connect_btn.clicked.connect(self.toggle_connection)
        header.addWidget(self.connect_btn)
        header.addStretch()
        self.poll_check = QCheckBox(tr("header.poll"))
        header.addWidget(self.poll_check)
        self.lang_btn = QPushButton(tr("header.lang"))
        self.lang_btn.clicked.connect(self.toggle_language)
        header.addWidget(self.lang_btn)
        right_layout.addWidget(header_box)

        self.stack = QStackedWidget()
        right_layout.addWidget(self.stack, 1)
        self.pages = {
            "dashboard": DashboardPage(),
            "telemetry": TelemetryPage(),
            "faults": FaultsPage(self.action),
            "control": ControlPage(self.action),
            "io": IOPage(self.action),
            "params": ParamsPage(self.action, self.state),
            "test": TestPage(self.action, self.state),
            "upgrade": UpgradePage(),
        }
        for key in self.PAGE_KEYS:
            self.stack.addWidget(self.pages[key])
        self.pages["upgrade"].query_requested.connect(self.query_device)
        self.pages["upgrade"].upgrade_requested.connect(self.prepare_upgrade)
        self.status = StatusBar()
        right_layout.addWidget(self.status)
        layout.addWidget(right, 1)
        self.refresh_ports()
        self.show_page(0)

    def show_page(self, index):
        self.stack.setCurrentIndex(index)
        for n, widget in enumerate(self.nav_buttons):
            widget.setText(("•   " if n == index else "◦   ") + tr(self.PAGE_LABELS[n]))
            widget.setProperty("active", n == index)
            widget.style().unpolish(widget)
            widget.style().polish(widget)

    def refresh_ports(self):
        current = self.port.currentText()
        ports = port_names()
        self.port.clear()
        self.port.addItems(ports)
        self.port.setCurrentText(current or (ports[0] if ports else ""))
        self.pages["upgrade"].refresh_ports()

    def toggle_connection(self):
        if self.busy:
            return
        if self.state.running:
            self.state.close()
            return
        port = self.port.currentText().strip()
        if not port:
            self.refresh_ports()
            port = self.port.currentText().strip()
        if not port:
            self.error(tr("err.no_serial"))
            return
        try:
            self.state.open(port)
        except Exception as exc:
            self.error(str(exc))

    def refresh(self):
        key = self.PAGE_KEYS[self.stack.currentIndex()]
        snapshot = self.state.snapshot(
            include_logs=False, include_samples=key in ("telemetry", "test"))
        self.last_snapshot = snapshot
        connected = snapshot["connected"]
        self.connection.setText(f"{tr('header.connected')} {snapshot['port']}" if connected else tr("header.disconnected"))
        self.connect_btn.setText(tr("header.disconnect") if connected else tr("header.connect"))
        self.connection.setProperty('connected', connected)
        self.connection.style().unpolish(self.connection)
        self.connection.style().polish(self.connection)
        self.lang_btn.setEnabled(not self.busy and not self.pages['params'].hw_busy)
        self.status.update_state(snapshot)
        self.pages[key].update_state(snapshot)

    def poll(self):
        if self.state.running and self.poll_check.isChecked() and not self.busy:
            try:
                self.state.poll_once()
            except Exception as exc:
                self.state.add_log("POLL ERROR: " + str(exc))

    def error(self, message):
        QMessageBox.warning(self, L("操作失败", "Operation failed"), message)

    def _background(self, operation):
        def run():
            try:
                operation()
            except Exception as exc:
                self.worker_signals.done.emit("action", None, str(exc))
        threading.Thread(target=run, daemon=True).start()

    def action(self, action, **kwargs):
        try:
            if action == "mode":
                self.state.send(core.CMD_SET_MODE, core.u16_payload(kwargs["value"]))
            elif action == "query":
                self.state.poll_once()
            elif action == "process":
                value, param = kwargs["value"], kwargs.get("param", 0)
                if value == 5:
                    with self.state.lock:
                        mode = self.state.status.get("metrics", {}).get("work_mode")
                    if mode is not None and mode != 1:
                        raise RuntimeError(tr("err.stage_test_mode"))
                if value in (1, 6):
                    self._background(lambda: (self.state.set_mode_wait(2),
                                              self.state.send(core.CMD_PROCESS, core.process_payload(value, param))))
                else:
                    self.state.send(core.CMD_PROCESS, core.process_payload(value, param))
            elif action in ("interaction", "recipe"):
                pending = self.state.pending_interaction
                request_id = pending.get("id", 0) if pending else int(time.time()) & 0xFFFFFFFF
                kind = kwargs.get("type", 6 if action == "recipe" else 0)
                selected = kwargs.get("index", kwargs.get("value", 0)) if action == "recipe" else kwargs.get("value", 0)
                self.state.send(core.CMD_INTERACTION,
                                core.interaction_payload(request_id, kind, 1, selected))
            elif action == "saveParam":
                revision = self.pages["params"].get_revision()
                self.state.send(core.CMD_SAVE_PARAM, struct.pack("<I", revision))
            elif action == "restoreParam":
                self.state.send(core.CMD_RESTORE_PARAM, b"DFLT")
            elif action == "readParam":
                self.state.request_param(self.pages["params"].get_recipe_index())
            elif action == "writeParam":
                page = self.pages["params"]
                dirty = page.get_dirty_params()
                if dirty:
                    self.state.send_param_update(page.get_recipe_index(), page.get_revision(), dirty)
                    page.mark_clean()
            elif action == "booster":
                self.state.send(core.CMD_BOOSTER_CONTROL, core.u16_payload(1 if kwargs.get("start") else 0))
            elif action == "rs485":
                station = int(core.RS485_STATIONS[kwargs["station"]].split(" ", 1)[0])
                self.state.send(core.CMD_MANUAL_RS485, struct.pack("<HH", station, kwargs["value"]))
            elif action == "actuatorMode":
                self.state.send(core.CMD_SET_ACTUATOR_MODE, core.u16_payload(kwargs["mode"]))
            elif action == "output":
                self.state.send(core.CMD_MANUAL_OUTPUT,
                                struct.pack("<HBBI", kwargs["output_id"], int(bool(kwargs["state"])), 0, 0))
            elif action == "selfTest":
                mode = kwargs.get("mode", 0)
                self._background(lambda: (self.state.set_mode_wait(2),
                                          self.state.send(core.CMD_PROCESS, core.process_payload(8 + mode))))
        except Exception as exc:
            self.error(str(exc))

    def _start_worker(self, kind, operation):
        self.busy = True
        self.pages["upgrade"].set_busy(True)
        self.connect_btn.setEnabled(False)
        self.poll_check.setEnabled(False)
        def run():
            result, error = None, None
            try:
                result = operation()
            except Exception as exc:
                error = str(exc)
            self.worker_signals.done.emit(kind, result, error)
        self.worker = threading.Thread(target=run, daemon=True)
        self.worker.start()

    def _release_business_port(self, port):
        self.was_connected = self.state.running and self.state.status.get("port") == port
        if self.was_connected:
            self.state.close()
            time.sleep(.3)

    def _restore_business_port(self):
        settings = self.pending_settings or {}
        if self.was_connected and settings.get("profile", None) and settings["profile"].name == "PawClean":
            try:
                self.state.open(settings["port"])
            except Exception as exc:
                self.worker_signals.log.emit(L("恢复业务连接失败：", "Business reconnect failed: ") + str(exc))
        self.was_connected = False

    def _upgrade_log(self, line):
        self.pages["upgrade"].append_log(time.strftime("[%H:%M:%S] ") + line)

    def _upgrade_progress(self, percent, message):
        self.pages["upgrade"].set_progress(percent, message)

    def query_device(self):
        if self.busy:
            return
        try:
            settings = self.pages["upgrade"].settings()
            if not settings["port"]:
                raise ValueError(tr("err.no_serial"))
            self.pending_settings = settings
        except Exception as exc:
            self.error(str(exc))
            return
        self.pages["upgrade"].status.setText(L("查询中", "Querying"))
        def operation():
            self._release_business_port(settings["port"])
            client = IapClient(log=self.worker_signals.log.emit)
            return client.query_port(settings["port"], settings["baudrate"], settings["profile"])
        self._start_worker("query", operation)

    def prepare_upgrade(self):
        if self.busy:
            return
        try:
            settings = self.pages["upgrade"].settings()
            if not settings["port"] or not settings["file"]:
                raise ValueError(L("请选择串口和固件文件", "Choose a port and firmware file"))
            parse_version(settings["version"])
            image, crc = inspect_image(settings["file"], settings["profile"])
            settings["size"], settings["crc"] = len(image), crc
            self.pending_settings = settings
        except Exception as exc:
            self.error(str(exc))
            return
        self.pages["upgrade"].status.setText(L("升级前查询设备", "Preflight device query"))
        def operation():
            self._release_business_port(settings["port"])
            client = IapClient(log=self.worker_signals.log.emit)
            return client.query_port(settings["port"], settings["baudrate"], settings["profile"])
        self._start_worker("preflight", operation)

    def worker_done(self, kind, result, error):
        if kind == "action":
            if error:
                self.error(error)
            return
        page = self.pages["upgrade"]
        if kind in ("query", "preflight") and not error:
            page.show_info(result)
        if kind == "preflight" and not error:
            settings = self.pending_settings
            target = settings["target_id"]
            if target is None:
                target = result["target_id"]
                settings["target_id"] = target
            if target != result["target_id"]:
                error = (f"目标 ID 不匹配：设备 0x{result['target_id']:08X}，"
                         f"输入 0x{target:08X}")
            else:
                message = (f"{settings['profile'].name}  ·  {settings['port']}  ·  "
                           f"Target 0x{target:08X}\n"
                           f"{Path(settings['file']).name}\n"
                           f"{settings['size']} bytes  ·  CRC32 0x{settings['crc']:08X}  ·  "
                           f"V{settings['version']}\n\n"
                           + L("原始 BIN 不含设备型号信息。请再次核对固件与设备类型。",
                               "Raw BIN files contain no device model. Verify the file and device type."))
                answer = QMessageBox.question(self, L("确认升级", "Confirm upgrade"), message,
                                              QMessageBox.StandardButton.Yes |
                                              QMessageBox.StandardButton.No,
                                              QMessageBox.StandardButton.No)
                if answer == QMessageBox.StandardButton.Yes:
                    self.busy = False
                    self._start_upgrade()
                    return
        if error:
            page.status.setText(L("操作失败", "Operation failed"))
            self._upgrade_log("ERROR: " + error)
            self.error(error)
        elif kind == "upgrade":
            page.status.setText(L("升级成功", "Upgrade complete"))
        elif kind == "query":
            page.status.setText(L("查询成功", "Query complete"))
        self._restore_business_port()
        self.busy = False
        page.set_busy(False)
        self.connect_btn.setEnabled(True)
        self.poll_check.setEnabled(True)

    def _start_upgrade(self):
        settings = dict(self.pending_settings)
        page = self.pages["upgrade"]
        page.status.setText(L("升级中", "Upgrading"))
        page.set_progress(0, L("开始升级", "Starting upgrade"))
        def operation():
            client = IapClient(log=self.worker_signals.log.emit,
                               progress=self.worker_signals.progress.emit)
            return client.upgrade(settings["port"], settings["baudrate"],
                                  settings["profile"], settings["file"],
                                  settings["version"], settings["target_id"])
        self._start_worker("upgrade", operation)

    def toggle_language(self):
        if self.busy or self.pages["params"].hw_busy:
            return
        # Rebuild labels from the common translation catalog and preserve form state.
        old_index = self.stack.currentIndex()
        old_upgrade = self.pages["upgrade"]
        fields = {key: getattr(old_upgrade, key).text() for key in ("baud", "target", "version", "file")}
        profile, port = old_upgrade.profile.currentText(), old_upgrade.port.currentText()
        upgrade_log = old_upgrade.log.toPlainText()
        upgrade_info = {key: label.text() for key, label in old_upgrade.device_labels.items()}
        upgrade_status = old_upgrade.status.text()
        upgrade_progress = (old_upgrade.progress.value(), old_upgrade.progress_text.text())
        old_control = self.pages["control"]
        control_recipe, control_stage = old_control.recipe.currentIndex(), old_control.stage.currentIndex()
        old_io = self.pages["io"]
        io_station, io_value = old_io.station.currentIndex(), old_io.value.text()
        old_test = self.pages["test"]
        test_interval, test_stage = old_test.interval.text(), old_test.stage.currentIndex()
        test_series = {key: box.isChecked() for key, box in old_test.series_checks.items()}
        test_status, test_command = old_test.status.text(), old_test.command_at
        old_params = self.pages["params"]
        params_recipe = old_params.recipe.currentIndex()
        hardware_values = old_params.hw_values()
        hardware_dirty = old_params.hw_dirty
        param_values = {key: edit.text() for key, edit in old_params.entries.items()}
        param_dirty = set(old_params.dirty)
        set_lang("en" if get_lang() == "zh" else "zh")
        old_stack = self.stack
        new_stack = QStackedWidget()
        old_stack.parentWidget().layout().replaceWidget(old_stack, new_stack)
        self.stack = new_stack
        self.pages = {
            "dashboard": DashboardPage(), "telemetry": TelemetryPage(),
            "faults": FaultsPage(self.action), "control": ControlPage(self.action),
            "io": IOPage(self.action), "params": ParamsPage(self.action, self.state),
            "test": TestPage(self.action, self.state), "upgrade": UpgradePage(),
        }
        for key in self.PAGE_KEYS:
            self.stack.addWidget(self.pages[key])
        self.pages["upgrade"].query_requested.connect(self.query_device)
        self.pages["upgrade"].upgrade_requested.connect(self.prepare_upgrade)
        new_upgrade = self.pages["upgrade"]
        for key, text in fields.items():
            getattr(new_upgrade, key).setText(text)
        new_upgrade.profile.setCurrentText(profile)
        new_upgrade.port.setCurrentText(port)
        new_upgrade.log.setPlainText(upgrade_log)
        new_upgrade.status.setText(upgrade_status)
        new_upgrade.set_progress(*upgrade_progress)
        for key, text in upgrade_info.items():
            new_upgrade.device_labels[key].setText(text)
        self.pages["control"].recipe.setCurrentIndex(control_recipe)
        self.pages["control"].stage.setCurrentIndex(control_stage)
        self.pages["io"].station.setCurrentIndex(io_station)
        self.pages["io"].value.setText(io_value)
        self.pages["test"].interval.setText(test_interval)
        self.pages["test"].stage.setCurrentIndex(test_stage)
        self.pages["test"].status.setText(test_status)
        self.pages["test"].command_at = test_command
        for key, checked in test_series.items():
            self.pages["test"].series_checks[key].setChecked(checked)
        self.pages["params"].recipe.setCurrentIndex(params_recipe)
        self.pages["params"].fan.setCurrentIndex(hardware_values["fan_pwm"])
        self.pages["params"].pump.setCurrentIndex(hardware_values.get("booster_mode", 0))
        for index, check in enumerate(self.pages["params"].channels):
            check.setChecked(bool(hardware_values["temperature_mask"] & (1 << index)))
        self.pages["params"].lower.setChecked(bool(hardware_values["lower_door_bypass"]))
        self.pages["params"].host.setChecked(bool(hardware_values["host_command_bypass"]))
        for key, text in param_values.items():
            self.pages["params"].entries[key].setText(text)
        self.pages["params"].dirty = param_dirty
        self.pages["params"].hw_dirty = hardware_dirty
        self.pages["params"].hw_session = self.last_snapshot.get("hardwareSession")
        self.pages["params"].current_hardware = dict(self.last_snapshot.get("hardware", {}))
        for widget, label in zip(self.nav_buttons, self.PAGE_LABELS):
            widget.setText(tr(label))
        self.brand_title.setText(tr("brand.title"))
        self.brand_subtitle.setText(tr("brand.subtitle"))
        for section, key in self.section_labels:
            section.setText(tr(key))
        self.refresh_ports_btn.setText(tr("header.refresh"))
        self.poll_check.setText(tr("header.poll"))
        self.lang_btn.setText(tr("header.lang"))
        self.show_page(old_index)
        old_stack.deleteLater()

    def closeEvent(self, event):
        if self.busy:
            QMessageBox.information(self, L("升级进行中", "Upgrade in progress"),
                                    L("请等待升级完成后再关闭程序。", "Wait for the upgrade to finish before closing."))
            event.ignore()
            return
        self.state.close()
        event.accept()


def main():
    set_lang("zh")
    app = QApplication(sys.argv)
    app.setApplicationName("PawClean")
    app.setStyle("Fusion")
    app.setStyleSheet(STYLE)
    window = WorkbenchWindow()
    if "--smoke" in sys.argv:
        for index in range(len(window.PAGE_KEYS)):
            window.show_page(index)
            window.refresh()
            app.processEvents()
        window.toggle_language()
        app.processEvents()
        window.close()
        return 0
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
