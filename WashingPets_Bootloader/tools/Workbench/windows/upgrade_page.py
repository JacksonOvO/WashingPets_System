"""Integrated PawClean/Store serial upgrade page."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QComboBox, QFileDialog, QFormLayout, QHBoxLayout, QLabel,
    QLineEdit, QPlainTextEdit, QProgressBar, QPushButton,
)

from .iap import PROFILES, port_names, version_text
from .pages import L, Page, group
from .widgets import label
from ui.i18n import tr


class UpgradePage(Page):
    query_requested = Signal()
    upgrade_requested = Signal()

    def __init__(self):
        super().__init__()
        _, content = group(tr('fw.title'), self.body)
        self.profile = QComboBox()
        self.profile.addItems(PROFILES)
        self.port = QComboBox()
        self.port.setEditable(True)
        self.baud = QLineEdit('115200')
        self.baud.setMaximumWidth(100)
        self.target = QLineEdit()
        self.target.setPlaceholderText(L('查询后自动填入', 'Filled after query'))
        self.version = QLineEdit('1.0.1')
        self.version.setMaximumWidth(90)
        self.file = QLineEdit()
        self.file.setPlaceholderText(L('请选择 APP .bin 固件', 'Choose an APP .bin file'))
        self.browse_btn = QPushButton(tr('fw.browse'))
        self.browse_btn.clicked.connect(self.browse)
        self.refresh_btn = QPushButton(L('刷新串口', 'Refresh ports'))
        self.query_btn = QPushButton(tr('fw.query_device'))
        self.upgrade_btn = QPushButton(tr('fw.start_upgrade'))
        self.upgrade_btn.setProperty('role', 'start')
        self.clear_btn = QPushButton(L('清空日志', 'Clear log'))
        self.refresh_btn.clicked.connect(self.refresh_ports)
        self.query_btn.clicked.connect(self.query_requested.emit)
        self.upgrade_btn.clicked.connect(self.upgrade_requested.emit)
        self.clear_btn.clicked.connect(lambda: self.log.clear())
        row = QHBoxLayout()
        for name, widget in [(L('设备类型', 'Device'), self.profile), (L('串口', 'Port'), self.port), (L('波特率', 'Baud'), self.baud), (L('目标 ID', 'Target ID'), self.target)]:
            row.addWidget(label(name))
            row.addWidget(widget)
        row.addWidget(self.refresh_btn)
        content.addLayout(row)
        row = QHBoxLayout()
        row.addWidget(label(tr('fw.version')))
        row.addWidget(self.version)
        row.addWidget(label(tr('fw.firmware')))
        row.addWidget(self.file, 1)
        for widget in (self.browse_btn, self.query_btn, self.upgrade_btn):
            row.addWidget(widget)
        content.addLayout(row)
        self.status = label(tr('fw.ready'), 'statusLine')
        content.addWidget(self.status)
        device_layout = QFormLayout()
        self.device_labels = {}
        fields = (('hardware', tr('fw.hardware')), ('boot_version', tr('fw.bootloader')),
                  ('app_version', tr('fw.app_version')), ('target_id', tr('fw.target_id')),
                  ('app_start', tr('fw.app_start')), ('capacity', tr('fw.capacity')),
                  ('max_chunk', tr('fw.max_chunk')), ('mode', tr('fw.run_mode')),
                  ('protocol', L('协议', 'Protocol')))
        for key, title in fields:
            value = QLabel('--')
            value.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            self.device_labels[key] = value
            device_layout.addRow(label(title), value)
        content.addLayout(device_layout)
        self.progress = QProgressBar()
        self.progress.setValue(0)
        self.progress_text = label(L('等待操作', 'Waiting'))
        content.addWidget(self.progress)
        content.addWidget(self.progress_text)
        log_box, content = group('', self.body)
        row = QHBoxLayout()
        row.addWidget(label(tr('fw.upgrade_log'), 'cardTitle'), 1)
        row.addWidget(self.clear_btn)
        content.addLayout(row)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMinimumHeight(300)
        self.log.setMaximumBlockCount(1000)
        content.addWidget(self.log)
        self.finish()
        self.refresh_ports()

    def update_state(self, _state):
        pass

    def refresh_ports(self):
        current = self.port.currentText()
        self.port.clear()
        self.port.addItems(port_names())
        self.port.setCurrentText(current or (self.port.itemText(0) if self.port.count() else ""))

    def browse(self):
        path, _ = QFileDialog.getOpenFileName(self, L("选择固件", "Choose firmware"),
                                              str(Path.home()), "Binary (*.bin)")
        if path:
            self.file.setText(path)

    def settings(self):
        return {"profile": PROFILES[self.profile.currentText()],
                "port": self.port.currentText().strip(),
                "baudrate": int(self.baud.text().strip()),
                "target_id": int(self.target.text().strip(), 0) if self.target.text().strip() else None,
                "version": self.version.text().strip(),
                "file": self.file.text().strip()}

    def show_info(self, info):
        values = dict(info)
        values["mode"] = "APP" if info["mode"] == 0 else "Bootloader" if info["mode"] == 1 else str(info["mode"])
        values["target_id"] = f"0x{info['target_id']:08X}"
        values["app_start"] = f"0x{info['app_start']:08X}"
        values["boot_version"] = version_text(info["boot_version"])
        values["app_version"] = version_text(info["app_version"])
        for key, label in self.device_labels.items():
            label.setText(str(values.get(key, "--")))
        if not self.target.text().strip():
            self.target.setText(values["target_id"])
        self.status.setText(L("查询成功", "Query complete"))

    def set_busy(self, busy):
        for widget in (self.profile, self.port, self.baud, self.target,
                       self.version, self.file, self.browse_btn, self.refresh_btn,
                       self.query_btn, self.upgrade_btn):
            widget.setEnabled(not busy)

    def append_log(self, line):
        self.log.appendPlainText(line)
        if self.log.blockCount() > 1000:
            cursor = self.log.textCursor()
            cursor.movePosition(cursor.MoveOperation.Start)
            cursor.select(cursor.SelectionType.BlockUnderCursor)
            cursor.removeSelectedText()

    def set_progress(self, percent, label):
        self.progress.setValue(percent)
        self.progress_text.setText(label)
