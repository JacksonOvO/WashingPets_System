#!/usr/bin/env python3
"""Shared GTK maintenance configuration panel for both workbench layouts."""
import threading
import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, GLib


class HardwareConfigPanel(Gtk.Box):
    def __init__(self, state):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.state = state
        self._syncing = False
        self._dirty = False
        self._busy = False
        self._session = None
        self._snapshot = {}
        self.set_hexpand(True)
        title = Gtk.Label(label="工程配置", xalign=0)
        title.get_style_context().add_class("card-title")
        self.pack_start(title, False, False, 0)
        grid = Gtk.Grid(column_spacing=18, row_spacing=10)
        grid.set_hexpand(True)
        self.pack_start(grid, False, False, 0)
        self.fan = Gtk.ComboBoxText()
        self.fan.append_text("独立可控硅")
        self.fan.append_text("四路PWM模块")
        self.fan.set_active(0)
        self.fan.connect("changed", self._changed)
        self.pump = Gtk.ComboBoxText()
        self.pump.append_text("GPIO OUT31")
        self.pump.append_text("RS485 增压泵")
        self.pump.set_active(0)
        self.pump.connect("changed", self._changed)
        self.channels = []
        rows = [("风机驱动", self.fan), ("增压泵控制", self.pump)]
        for index, name in enumerate(("CH1 进水温度", "CH2 水箱温度",
                                      "CH3 管路出水温度", "CH4 混水温度")):
            check = Gtk.CheckButton(label="真实采集")
            check.connect("toggled", self._changed)
            self.channels.append(check)
            rows.append((name, check))
        self.lower = Gtk.Switch()
        self.host = Gtk.Switch()
        for switch in (self.lower, self.host):
            switch.set_halign(Gtk.Align.START)
            switch.connect("notify::active", self._changed)
        rows.extend((("下门门磁旁路", self.lower), ("上位机命令旁路", self.host)))
        for row, (label, widget) in enumerate(rows):
            name = Gtk.Label(label=label, xalign=0)
            name.set_line_wrap(True)
            grid.attach(name, 0, row, 1, 1)
            grid.attach(widget, 1, row, 1, 1)
        self.controls = [widget for _, widget in rows]
        buttons = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        self.read_button = self._button("读取", "view-refresh-symbolic", self._read)
        self.apply_button = self._button("应用并保存", "document-save-symbolic", self._apply)
        buttons.pack_start(self.read_button, False, False, 0)
        buttons.pack_start(self.apply_button, False, False, 0)
        self.pack_start(buttons, False, False, 0)
        self.active_label = Gtk.Label(label="当前配置未读取", xalign=0)
        self.warning = Gtk.Label(xalign=0)
        self.message = Gtk.Label(xalign=0)
        for label in (self.active_label, self.warning, self.message):
            label.set_line_wrap(True)
            label.set_max_width_chars(75)
            self.pack_start(label, False, False, 0)
        self.warning.get_style_context().add_class("warning")
        self.update_state({})

    @staticmethod
    def _button(label, icon, callback):
        button = Gtk.Button(label=label)
        button.set_image(Gtk.Image.new_from_icon_name(icon, Gtk.IconSize.BUTTON))
        button.set_always_show_image(True)
        button.connect("clicked", callback)
        return button

    def _changed(self, *_):
        if not self._syncing:
            self._dirty = True
            self.message.set_text("有未应用的修改")

    def values(self):
        return {
            "fan_pwm": self.fan.get_active(),
            "booster_mode": self.pump.get_active(),
            "temperature_mask": sum(1 << i for i, c in enumerate(self.channels) if c.get_active()),
            "lower_door_bypass": int(self.lower.get_active()),
            "host_command_bypass": int(self.host.get_active()),
        }

    def _read(self, *_):
        self._start(self.state.read_hardware, "正在读取设备配置")

    def _apply(self, *_):
        values = self.values()
        if (values["lower_door_bypass"] or values["host_command_bypass"] or
                values["temperature_mask"] != 15):
            risks = []
            if values["lower_door_bypass"]:
                risks.append("下门打开/关闭判断将被跳过。")
            if values["host_command_bypass"]:
                risks.append("流程命令等待将直接放行，可能提前推进流程。")
            if values["temperature_mask"] != 15:
                risks.append("未勾选温度通道将使用固定模拟温度，不是真实测量。")
            dialog = Gtk.MessageDialog(
                transient_for=self.get_toplevel(), modal=True,
                message_type=Gtk.MessageType.WARNING, buttons=Gtk.ButtonsType.OK_CANCEL,
                text="确认应用维护旁路配置？")
            dialog.format_secondary_text("\n".join(risks) + "\n仅用于无宠物维护调试。")
            result = dialog.run()
            dialog.destroy()
            if result != Gtk.ResponseType.OK:
                return
        self._start(lambda: self.state.apply_hardware(values), "正在应用并保存")

    def _start(self, operation, text):
        if self._busy:
            return
        self._busy = True
        self.message.set_text(text)
        self.update_state(self._snapshot)
        def work():
            try:
                operation()
                GLib.idle_add(self._finished, None)
            except Exception as exc:
                GLib.idle_add(self._finished, str(exc))
        threading.Thread(target=work, daemon=True).start()

    def _finished(self, error):
        self._busy = False
        if error is None:
            self._dirty = False
            self.message.set_text("设备配置已确认")
        else:
            self.message.set_text(error)
        self.update_state(self.state.snapshot(include_logs=False, include_samples=False))
        return False

    def update_state(self, snapshot):
        self._snapshot = snapshot
        if snapshot.get("hardwareSession") != self._session:
            self._session = snapshot.get("hardwareSession")
            self._dirty = False
        connected = snapshot.get("connected", False)
        hardware = snapshot.get("hardware", {}) if connected else {}
        metrics = snapshot.get("metrics", {})
        manual_idle = (metrics.get("work_mode") == 1 and
                       metrics.get("device_state") in (0, 6, 7))
        editable = bool(hardware) and manual_idle and not self._busy and not hardware.get("busy")
        for widget in self.controls:
            widget.set_sensitive(editable)
        self.read_button.set_sensitive(connected and not self._busy)
        self.apply_button.set_sensitive(editable)
        if not hardware:
            self.active_label.set_text("当前配置未读取" if connected else "未连接设备")
            self.warning.set_text("")
            return
        if not self._dirty:
            self._syncing = True
            self.fan.set_active(hardware["fan_pwm"])
            self.pump.set_active(hardware.get("booster_mode", 0))
            for i, check in enumerate(self.channels):
                check.set_active(bool(hardware["temperature_mask"] & (1 << i)))
            self.lower.set_active(bool(hardware["lower_door_bypass"]))
            self.host.set_active(bool(hardware["host_command_bypass"]))
            self._syncing = False
        driver = "四路PWM" if hardware["fan_pwm"] else "独立可控硅"
        storage = "未保存Flash" if hardware["save_pending"] else "已保存Flash"
        phase = "切换中" if hardware["busy"] else "已生效"
        self.active_label.set_text(f"当前：{driver} · 泵{'485' if hardware.get('booster_mode') else 'GPIO OUT31'} · 温度掩码0x{hardware['temperature_mask']:02X} · "
                                  f"版本{hardware['revision']} · {phase} · {storage}")
        risks = []
        if hardware["lower_door_bypass"]:
            risks.append("下门门磁已旁路")
        if hardware["host_command_bypass"]:
            risks.append("上位机命令等待已旁路")
        off = [f"CH{i+1}" for i in range(4) if not hardware["temperature_mask"] & (1 << i)]
        if off:
            risks.append("/".join(off) + "为模拟温度")
        self.warning.set_text("；".join(risks))
