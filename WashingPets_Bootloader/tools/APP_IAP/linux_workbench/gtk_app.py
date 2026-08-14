#!/usr/bin/env python3
import threading
import time
from pathlib import Path

import cairo
import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
gi.require_foreign("cairo")
from gi.repository import Gdk, GdkPixbuf, GLib, Gtk, Pango

import server as core


APP_DIR = Path(__file__).resolve().parent
GLib.set_prgname("pawclean-workbench")
GLib.set_application_name("喜宠岛洗宠机工作台")
Gdk.set_program_class("pawclean-workbench")


class WorkbenchWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title="洗宠机 Linux 工作台")
        self.set_role("pawclean-workbench")
        self.set_default_size(1280, 820)
        self.set_position(Gtk.WindowPosition.CENTER)
        self.connect("destroy", self.on_destroy)

        icon_path = APP_DIR / "pawclean-app-icon.svg"
        if icon_path.exists():
            self.set_icon_from_file(str(icon_path))

        self.state = core.WorkbenchState()
        core.STATE = self.state
        self.metric_labels = {}
        self.telemetry_labels = {}
        self.power_labels = {}
        self.output_switches = {}
        self.output_states = {}
        self.io_preview_rows = {}
        self.param_entries = {}
        self.log_mark = None
        self.last_log_text = ""
        self.last_io_signature = None
        self.last_samples_signature = None
        self.chart_samples = []
        self.current_page = None
        self.updating_outputs = False
        settings = Gtk.Settings.get_default()
        if settings:
            settings.set_property("gtk-application-prefer-dark-theme", True)

        self.add(self.build_ui())
        self.apply_css()
        self.refresh_ports()
        GLib.timeout_add(500, self.refresh_state)
        GLib.timeout_add(1000, self.compat_poll)

    def build_ui(self):
        root = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)

        sidebar = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        sidebar.set_size_request(220, -1)
        sidebar.get_style_context().add_class("sidebar")
        root.pack_start(sidebar, False, False, 0)

        logo = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        logo.get_style_context().add_class("brand")
        logo_path = APP_DIR / "pawtopia-logo.svg"
        if logo_path.exists():
            pix = GdkPixbuf.Pixbuf.new_from_file_at_scale(str(logo_path), 112, 44, True)
            logo.pack_start(Gtk.Image.new_from_pixbuf(pix), False, False, 0)
        title = Gtk.Label(label="洗宠机")
        title.get_style_context().add_class("brand-title")
        logo.pack_start(title, False, False, 0)
        sidebar.pack_start(logo, False, False, 8)

        self.stack = Gtk.Stack()
        self.stack.set_transition_type(Gtk.StackTransitionType.NONE)
        self.stack.set_hhomogeneous(True)
        self.stack.set_vhomogeneous(True)

        nav_items = [
            ("main", "总览与交互"),
            ("upgrade", "在线升级"),
            ("param", "参数配置"),
            ("io", "IO 输出控制"),
            ("test", "测试界面"),
        ]
        self.nav_buttons = {}
        for name, label in nav_items:
            btn = Gtk.Button(label=label)
            btn.get_style_context().add_class("nav")
            btn.connect("clicked", self.show_page, name)
            sidebar.pack_start(btn, False, False, 0)
            self.nav_buttons[name] = btn

        content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        root.pack_start(content, True, True, 0)

        header = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        header.get_style_context().add_class("header")
        self.connection_label = Gtk.Label(label="上位机未连接")
        self.connection_label.get_style_context().add_class("connection")
        header.pack_start(self.connection_label, False, False, 0)
        spacer = Gtk.Box()
        header.pack_start(spacer, True, True, 0)
        self.port_combo = Gtk.ComboBoxText()
        self.port_combo.set_size_request(180, 36)
        header.pack_start(self.port_combo, False, False, 0)
        header.pack_start(button("刷新", self.refresh_ports), False, False, 0)
        self.connect_button = button("连接APP", self.toggle_connection, "start")
        header.pack_start(self.connect_button, False, False, 0)
        self.poll_check = Gtk.CheckButton(label="兼容轮询")
        header.pack_start(self.poll_check, False, False, 0)
        content.pack_start(header, False, False, 0)

        stack_scroll = Gtk.ScrolledWindow()
        stack_scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        stack_scroll.add(self.stack)
        content.pack_start(stack_scroll, True, True, 0)
        self.stack.add_titled(self.build_main_page(), "main", "总览与交互")
        self.stack.add_titled(self.build_upgrade_page(), "upgrade", "在线升级")
        self.stack.add_titled(self.build_param_page(), "param", "参数配置")
        self.stack.add_titled(self.build_io_page(), "io", "IO 输出控制")
        self.stack.add_titled(self.build_test_page(), "test", "测试界面")
        self.show_page(None, "main")
        return root

    def build_main_page(self):
        page = page_box()
        top = ResponsiveGrid(min_child_width=520, max_columns=2, column_spacing=14, row_spacing=14)
        page.pack_start(top, False, False, 0)
        top.add_item(self.status_panel())
        top.add_item(self.telemetry_panel())
        page.pack_start(self.flow_panel(), False, False, 0)
        bottom = ResponsiveGrid(min_child_width=520, max_columns=2, column_spacing=14, row_spacing=14)
        bottom.set_vexpand(True)
        bottom.set_valign(Gtk.Align.FILL)
        bottom.add_item(self.power_panel(), expand=True)
        bottom.add_item(self.io_preview_panel(), expand=True)
        page.pack_start(bottom, True, True, 0)
        return page

    def status_panel(self):
        panel = card("APP 状态", expand=False)
        grid = ResponsiveGrid(min_child_width=138, max_columns=4, column_spacing=8, row_spacing=8)
        panel.pack_start(grid, False, False, 0)
        for i, name in enumerate(["模式", "设备状态", "运行类型", "当前流程", "步骤", "步骤状态", "配方", "进度"]):
            box, value = metric_box(name)
            self.metric_labels[name] = value
            grid.add_item(box)
        return panel

    def telemetry_panel(self):
        self.flag_label = Gtk.Label(label="状态标志 --", xalign=1)
        self.flag_label.get_style_context().add_class("card-meta")
        panel = card("实时遥测", expand=False, trailing=self.flag_label)
        grid = ResponsiveGrid(min_child_width=138, max_columns=5, column_spacing=8, row_spacing=8)
        panel.pack_start(grid, False, False, 0)
        names = ["舱外温度", "舱外湿度", "舱内氧气", "进水口水温", "水箱水温", "出水口水温", "备用温度", "舱内温度", "舱内湿度", "舱内臭氧"]
        for i, name in enumerate(names):
            box, value = metric_box(name)
            self.telemetry_labels[name] = value
            grid.add_item(box)
        return panel

    def flow_panel(self):
        panel = card("流程控制", expand=False)
        panel.get_style_context().add_class("flow-card")
        self.pending_label = Gtk.Label(label="", xalign=0)
        self.pending_label.get_style_context().add_class("pending")
        self.pending_label.set_no_show_all(True)
        panel.pack_start(self.pending_label, False, False, 0)

        grid = ResponsiveGrid(min_child_width=320, max_columns=3, column_spacing=12, row_spacing=12)
        panel.pack_start(grid, False, False, 0)

        mode = group_box("运行模式与配方")
        mode.get_style_context().add_class("flow-group")
        row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=0)
        row.get_style_context().add_class("segmented")
        row.pack_start(button("手动", lambda *_: self.send_action("mode", value=1)), True, True, 0)
        row.pack_start(button("自动", lambda *_: self.send_action("mode", value=2)), True, True, 0)
        row.pack_start(button("查询", lambda *_: self.send_action("query")), True, True, 0)
        mode.pack_start(row, False, False, 0)
        recipe_row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        self.recipe_combo = combo([f"配方{i + 1}" for i in range(core.RECIPE_COUNT)])
        self.recipe_combo.set_hexpand(True)
        recipe_row.pack_start(self.recipe_combo, True, True, 0)
        recipe_row.pack_start(button("选择配方", self.select_recipe, "soft-green"), False, False, 0)
        mode.pack_start(recipe_row, False, False, 8)
        grid.add_item(mode, expand=False)

        process = group_box("流程动作")
        process.get_style_context().add_class("flow-group")
        row1 = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        row1.pack_start(button("开始", lambda *_: self.send_action("process", value=1), "start big"), True, True, 0)
        row1.pack_start(button("暂停", lambda *_: self.send_action("process", value=2), "pause big"), True, True, 0)
        row1.pack_start(button("继续", lambda *_: self.send_action("process", value=3), "resume big"), True, True, 0)
        row1.pack_start(button("结束", lambda *_: self.send_action("process", value=4), "stop big"), True, True, 0)
        process.pack_start(row1, False, False, 0)
        row2 = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        self.stage_combo = combo(core.STAGES)
        row2.pack_start(self.stage_combo, False, False, 0)
        row2.pack_start(button("段测", self.stage_test), False, False, 0)
        row2.pack_start(button("自清洁", lambda *_: self.send_action("process", value=6)), False, False, 0)
        row2.pack_start(button("故障复位", lambda *_: self.send_action("process", value=7), "stop"), False, False, 0)
        process.pack_start(row2, False, False, 8)
        grid.add_item(process, expand=False)

        interact = group_box("舱门控制")
        interact.get_style_context().add_class("flow-group")
        door_grid = Gtk.Grid(column_spacing=8, row_spacing=8)
        door_grid.set_column_homogeneous(True)
        for i, (label, kind) in enumerate([("打开上舱门", 1), ("打开下舱门", 2), ("关闭下舱门", 3), ("关闭上舱门", 4), ("自动关门", 7)]):
            door_grid.attach(button(label, lambda _, k=kind: self.send_action("interaction", type=k)), i % 3, i // 3, 1, 1)
        interact.pack_start(door_grid, False, False, 0)
        grid.add_item(interact, expand=False)
        return panel

    def power_panel(self):
        panel = card("功率监控")
        grid = ResponsiveGrid(min_child_width=138, max_columns=3, column_spacing=8, row_spacing=8)
        grid.set_vexpand(True)
        grid.set_valign(Gtk.Align.FILL)
        grid.set_row_homogeneous(True)
        panel.pack_start(grid, True, True, 0)
        names = ["左风机", "右风机", "底部风机", "顶部排风扇", "增压泵压力", "左加热棒", "右加热棒", "底部加热棒", "比例阀角度"]
        for i, name in enumerate(names):
            box, value = metric_box(name)
            self.power_labels[name] = value
            grid.add_item(box)
        return panel

    def io_preview_panel(self):
        panel = card("IO 状态")
        scroll = Gtk.ScrolledWindow()
        scroll.set_min_content_height(260)
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        self.io_preview_grid = ResponsiveGrid(
            min_child_width=210,
            max_columns=4,
            column_spacing=8,
            row_spacing=6,
        )
        self.io_preview_grid.set_margin_end(4)
        self.io_preview_grid.set_margin_bottom(4)
        for name in core.INPUT_NAMES + core.OUTPUT_NAMES:
            item = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
            item.get_style_context().add_class("io-status-item")
            dot = Gtk.Label(label="●")
            dot.get_style_context().add_class("dot-off")
            label = Gtk.Label(label=name, xalign=0)
            label.set_ellipsize(Pango.EllipsizeMode.END)
            item.pack_start(dot, False, False, 0)
            item.pack_start(label, True, True, 0)
            self.io_preview_grid.add_item(item, expand=False)
            self.io_preview_rows[name] = dot
        scroll.add(self.io_preview_grid)
        panel.pack_start(scroll, True, True, 0)
        return panel

    def build_upgrade_page(self):
        page = page_box()
        panel = card("在线升级")
        panel.get_style_context().add_class("upgrade-card")
        row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        row.get_style_context().add_class("upgrade-row")
        self.version_entry = Gtk.Entry(text="1.0.1")
        self.version_entry.set_width_chars(8)
        self.firmware_entry = Gtk.Entry(text=str(core.DEFAULT_BIN))
        self.firmware_entry.set_hexpand(True)
        row.pack_start(Gtk.Label(label="版本"), False, False, 0)
        row.pack_start(self.version_entry, False, False, 0)
        row.pack_start(Gtk.Label(label="固件"), False, False, 0)
        row.pack_start(self.firmware_entry, True, True, 0)
        row.pack_start(button("浏览", self.browse_firmware), False, False, 0)
        row.pack_start(button("查询设备", self.query_device), False, False, 0)
        row.pack_start(button("开始升级", self.upgrade, "start"), False, False, 0)
        panel.pack_start(row, False, False, 0)
        self.upgrade_status = Gtk.Label(label="就绪", xalign=0)
        self.upgrade_status.get_style_context().add_class("status-line")
        panel.pack_start(self.upgrade_status, False, False, 8)
        panel.pack_start(self.log_view(), True, True, 0)
        page.pack_start(panel, True, True, 0)
        return page

    def build_param_page(self):
        page = page_box()
        panel = card("配方参数配置")
        row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        self.param_recipe_combo = combo([f"配方{i + 1}" for i in range(core.RECIPE_COUNT)])
        row.pack_start(self.param_recipe_combo, False, False, 0)
        row.pack_start(button("读取", self.read_params), False, False, 0)
        row.pack_start(button("写入RAM", self.write_params, "start"), False, False, 0)
        row.pack_start(button("保存", lambda *_: self.send_action("saveParam", revision=self.state.status["paramRevision"]), "primary"), False, False, 0)
        row.pack_start(button("恢复默认", self.restore_params, "stop"), False, False, 0)
        panel.pack_start(row, False, False, 0)
        self.param_revision = Gtk.Label(label="参数版本 --", xalign=0)
        panel.pack_start(self.param_revision, False, False, 8)
        scroll = Gtk.ScrolledWindow()
        grid = ResponsiveGrid(min_child_width=300, max_columns=4, column_spacing=10, row_spacing=10)
        grid.set_margin_top(4)
        for i, field in enumerate(core.RECIPE_FIELDS):
            item = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
            item.get_style_context().add_class("param-item")
            label = Gtk.Label(label=field["label"], xalign=0)
            label.set_ellipsize(Pango.EllipsizeMode.END)
            entry = Gtk.Entry(text="0")
            entry.set_hexpand(True)
            self.param_entries[field["key"]] = entry
            item.pack_start(label, False, False, 0)
            item.pack_start(entry, False, False, 0)
            grid.add_item(item, expand=False)
        scroll.add(grid)
        panel.pack_start(scroll, True, True, 0)
        page.pack_start(panel, True, True, 0)
        return page

    def build_io_page(self):
        page = page_box()
        panel = card("485 手动控制", expand=False)
        row = ResponsiveGrid(min_child_width=180, max_columns=6, column_spacing=8, row_spacing=8)
        self.rs485_combo = combo(core.RS485_STATIONS)
        self.rs485_combo.set_hexpand(True)
        self.rs485_entry = Gtk.Entry(text="0")
        self.rs485_entry.set_width_chars(8)
        self.actuator_check = Gtk.CheckButton(label="GPIO执行器(泵仍485)")
        row.add_item(self.rs485_combo, expand=True)
        row.add_item(labeled_inline("数值", self.rs485_entry), expand=True)
        row.add_item(self.actuator_check, expand=True)
        row.add_item(button("启动泵", lambda *_: self.send_action("booster", start=True), "start"), expand=True)
        row.add_item(button("停止泵", lambda *_: self.send_action("booster", start=False), "stop"), expand=True)
        row.add_item(button("设置485", self.set_rs485, "start"), expand=True)
        panel.pack_start(row, False, False, 0)
        hint = Gtk.Label(label="泵压力=0.0~10.0kg，风机/加热=0~100%，比例阀=0~90°", xalign=0)
        hint.get_style_context().add_class("hint")
        panel.pack_start(hint, False, False, 4)
        page.pack_start(panel, False, False, 0)

        output_panel = card("GPIO 输出点", expand=True)
        sub = Gtk.Label(label="点击滑动开关下发手动输出命令", xalign=0)
        sub.get_style_context().add_class("hint")
        output_panel.pack_start(sub, False, False, 0)
        scroll = Gtk.ScrolledWindow()
        self.output_grid = ResponsiveGrid(min_child_width=270, max_columns=4, column_spacing=8, row_spacing=8)
        for i, name in enumerate(core.OUTPUT_NAMES):
            sw = OutputToggle()
            sw.connect("notify::active", self.output_changed, i + 1)
            self.output_switches[i + 1] = sw
            item = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
            item.get_style_context().add_class("output-item")
            label = Gtk.Label(label=name, xalign=0)
            label.set_ellipsize(Pango.EllipsizeMode.END)
            item.pack_start(label, True, True, 0)
            item.pack_start(sw, False, False, 0)
            self.output_grid.add_item(item, expand=False)
        scroll.add(self.output_grid)
        output_panel.pack_start(scroll, True, True, 0)
        page.pack_start(output_panel, True, True, 0)
        return page

    def build_test_page(self):
        page = page_box()
        panel = card("单流程记录", expand=False)
        outer = ResponsiveGrid(min_child_width=300, max_columns=3, column_spacing=14, row_spacing=12)
        self.test_interval = Gtk.Entry(text="30")
        self.test_stage = combo(core.STAGES)
        self.test_stage.set_active(6)

        record_group = group_box("测试条件")
        record_group.pack_start(labeled("采样间隔(秒/点)", self.test_interval), False, False, 0)
        record_group.pack_start(labeled("流程", self.test_stage), False, False, 8)
        info = Gtk.Label(label="开始后执行所选单流程，并记录传感器数据", xalign=0)
        info.get_style_context().add_class("hint")
        record_group.pack_start(info, False, False, 8)
        outer.add_item(record_group)

        series_group = group_box("记录项目")
        self.test_record_checks = []
        for label, active in [("温度", True), ("湿度", True), ("氧气", False), ("臭氧", False), ("风机功率", False), ("加热棒功率", False)]:
            check = Gtk.CheckButton(label=label)
            check.set_active(active)
            check.connect("toggled", lambda *_: self.queue_chart_redraw())
            self.test_record_checks.append(check)
            series_group.pack_start(check, False, False, 0)
        outer.add_item(series_group)

        action_group = group_box("执行")
        self.flow_monitor_check = Gtk.CheckButton(label="流程监控")
        self.flow_monitor_check.set_active(True)
        action_group.pack_start(self.flow_monitor_check, False, False, 0)
        action_group.pack_start(button("开始测试", self.start_test, "start big"), False, False, 8)
        action_group.pack_start(button("结束并保存曲线", self.stop_test, "stop big"), False, False, 8)
        self.test_status = Gtk.Label(label="未开始", xalign=0)
        self.test_status.get_style_context().add_class("hint")
        action_group.pack_start(self.test_status, False, False, 8)
        outer.add_item(action_group)
        panel.pack_start(outer, False, False, 0)
        page.pack_start(panel, False, False, 0)

        chart_panel = card("测试曲线")
        self.chart_summary = Gtk.Label(label="舱内温湿度曲线 · 30秒/点", xalign=0)
        self.chart_summary.get_style_context().add_class("hint")
        chart_panel.pack_start(self.chart_summary, False, False, 0)
        self.chart_area = Gtk.DrawingArea()
        self.chart_area.set_size_request(-1, 360)
        self.chart_area.set_hexpand(True)
        self.chart_area.set_vexpand(True)
        self.chart_area.connect("draw", self.draw_chart)
        self.chart_area.get_style_context().add_class("chart")
        chart_panel.pack_start(self.chart_area, True, True, 0)
        page.pack_start(chart_panel, True, True, 0)
        return page

    def log_view(self):
        self.log_scroll = Gtk.ScrolledWindow()
        self.log_scroll.set_min_content_height(500)
        self.log_buffer = Gtk.TextBuffer()
        self.log_text_view = Gtk.TextView(buffer=self.log_buffer)
        self.log_text_view.set_editable(False)
        self.log_text_view.set_cursor_visible(False)
        self.log_text_view.set_monospace(True)
        self.log_text_view.set_wrap_mode(Gtk.WrapMode.NONE)
        self.log_text_view.get_style_context().add_class("log")
        self.log_scroll.add(self.log_text_view)
        return self.log_scroll

    def apply_css(self):
        css = b"""
        * { color: #d9e2ec; }
        window { background: #0f141a; color: #d9e2ec; }
        scrolledwindow { background: #0f141a; border: 0; }
        viewport { background: #0f141a; }
        .sidebar { background: #121820; border-right: 1px solid #26313d; padding: 10px 12px 16px 12px; }
        .brand { margin-bottom: 22px; }
        .brand-title { font-size: 20px; font-weight: 800; color: #f4f7fb; }
        .nav { margin: 3px 0; padding: 10px 12px; border-radius: 5px; color: #8d9aaa; background: transparent; border: 0; }
        .nav:hover { background: #1a2530; }
        .nav-active { background: #2386c8; color: #ffffff; font-weight: 700; }
        .header { background: #111821; border-bottom: 1px solid #26313d; padding: 12px 18px; }
        .connection { background: #1a2530; border-radius: 5px; padding: 9px 12px; font-weight: 700; color: #9fb0c3; }
        .connected { background: #173825; color: #7de39a; }
        .page { padding: 16px 18px; }
        .card { background: #151d26; border: 1px solid #293643; border-radius: 6px; padding: 14px 16px; }
        .card-header { margin-bottom: 8px; }
        .card-title { font-size: 17px; font-weight: 800; color: #f4f7fb; }
        .card-meta { color: #9fb0c3; }
        .metric { background: #1b2530; border-radius: 8px; padding: 9px; min-width: 115px; min-height: 62px; border: 1px solid #243242; }
        .metric-name { color: #8d9aaa; font-size: 12px; }
        .metric-value { color: #edf4fb; font-size: 16px; font-weight: 800; margin-top: 4px; }
        .group { background: #19232e; border: 1px solid #2c3a48; border-radius: 6px; padding: 10px; }
        .group-title { color: #9fb0c3; font-weight: 800; font-size: 12px; margin-bottom: 5px; }
        .flow-card { padding-top: 13px; padding-bottom: 13px; }
        .flow-group { min-height: 126px; }
        .segmented button { border-radius: 0; }
        button { background: #222c37; color: #d9e2ec; border: 1px solid #334252; border-radius: 4px; min-height: 36px; }
        button:hover { background: #2a3643; }
        button.start { background: #23894b; color: #ffffff; border-color: #2da65d; }
        button.pause { background: #4a361c; color: #ffd08a; border-color: #7d5524; }
        button.resume { background: #173b55; color: #91d6ff; border-color: #2c668b; }
        button.stop { background: #462421; color: #ffaaa2; border-color: #8a4039; }
        button.primary { background: #6254c9; color: #ffffff; border-color: #7b6bea; }
        button.soft-green { background: #173825; color: #93e4a9; border-color: #2b7544; }
        button.big { min-height: 44px; font-size: 15px; font-weight: 800; }
        entry, combobox, combobox box, checkbutton { color: #d9e2ec; }
        entry { background: #0f141a; color: #f2f7fb; border: 1px solid #334252; border-radius: 4px; min-height: 34px; padding: 0 10px; }
        entry selection { background: #2386c8; color: #ffffff; }
        combobox button { background: #0f141a; color: #f2f7fb; border-color: #334252; }
        checkbutton { color: #c8d3df; }
        .pending { color: #ffd08a; margin-bottom: 6px; }
        .hint { color: #8d9aaa; }
        .param-item, .output-item { background: #19232e; border: 1px solid #2c3a48; border-radius: 5px; padding: 9px 10px; }
        .param-item { min-height: 66px; }
        .param-item label { color: #9fb0c3; font-size: 12px; font-weight: 600; }
        .param-item entry { background: #111820; min-height: 32px; }
        .output-item { min-height: 42px; }
        .io-status-item { background: #19232e; border: 1px solid #263544; border-radius: 4px; padding: 6px 8px; min-height: 30px; }
        .io-status-item label { color: #cbd6e2; }
        button.io-toggle { min-width: 52px; min-height: 28px; padding: 0 8px; border-radius: 4px; background: #10171f; color: #8d9aaa; border-color: #3a4958; font-size: 12px; font-weight: 700; }
        button.io-toggle:checked { background: #23894b; color: #ffffff; border-color: #35a962; }
        .upgrade-card { padding: 18px; }
        .upgrade-row { margin-bottom: 4px; }
        .status-line { color: #9fb0c3; background: #19232e; border: 1px solid #2c3a48; border-radius: 7px; padding: 8px 10px; }
        .log { background: #0b1117; color: #bfead0; padding: 10px; font-size: 12px; }
        .chart { background: #0f141a; border: 1px solid #2c3a48; border-radius: 8px; }
        list, row { background: #151d26; color: #d9e2ec; }
        .dot-on { color: #34c26d; font-weight: 800; }
        .dot-off { color: #526273; }
        """
        provider = Gtk.CssProvider()
        provider.load_from_data(css)
        Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

    def show_page(self, _button, name):
        if name == self.current_page and self.stack.get_visible_child_name() == name:
            return
        self.current_page = name
        self.stack.set_visible_child_name(name)
        for key, btn in self.nav_buttons.items():
            ctx = btn.get_style_context()
            if key == name:
                ctx.add_class("nav-active")
            else:
                ctx.remove_class("nav-active")

    def refresh_ports(self, *_):
        current = self.port_combo.get_active_text()
        self.port_combo.remove_all()
        ports = core.list_ports()
        for port in ports:
            self.port_combo.append_text(port)
        if current and current in ports:
            self.port_combo.set_active(ports.index(current))
        elif ports:
            self.port_combo.set_active(0)

    def toggle_connection(self, *_):
        try:
            if self.state.status["connected"]:
                self.state.close()
            else:
                port = self.port_combo.get_active_text()
                if not port:
                    raise RuntimeError("请选择串口")
                self.state.open(port)
        except Exception as exc:
            self.error(str(exc))

    def send_action(self, action, **kwargs):
        try:
            if action == "mode":
                self.state.send(core.CMD_SET_MODE, core.u16_payload(int(kwargs["value"])))
            elif action == "query":
                self.state.poll_once()
            elif action == "process":
                self.state.send(core.CMD_PROCESS, core.process_payload(int(kwargs["value"]), int(kwargs.get("param", 0))))
            elif action == "interaction":
                pending = self.state.pending_interaction
                request_id = pending["id"] if pending else int(time.time()) & 0xFFFFFFFF
                self.state.send(core.CMD_INTERACTION, core.interaction_payload(request_id, int(kwargs["type"]), int(kwargs.get("result", 1)), int(kwargs.get("value", 0))))
            elif action == "saveParam":
                self.state.send(core.CMD_SAVE_PARAM, core.struct.pack("<I", int(kwargs.get("revision", 0))))
            elif action == "restoreParam":
                self.state.send(core.CMD_RESTORE_PARAM, b"DFLT")
            elif action == "booster":
                self.state.send(core.CMD_BOOSTER_CONTROL, core.u16_payload(1 if kwargs.get("start") else 0))
        except Exception as exc:
            self.error(str(exc))

    def stage_test(self, *_):
        self.send_action("process", value=5, param=self.stage_combo.get_active())

    def select_recipe(self, *_):
        self.send_action("interaction", type=6, value=self.recipe_combo.get_active())

    def read_params(self, *_):
        try:
            self.state.request_param(self.param_recipe_combo.get_active())
        except Exception as exc:
            self.error(str(exc))

    def write_params(self, *_):
        try:
            values = {key: entry.get_text() for key, entry in self.param_entries.items()}
            payload = self.state.build_param_payload(self.param_recipe_combo.get_active(), self.state.status["paramRevision"], values)
            self.state.send(core.CMD_SET_PARAM, payload)
        except Exception as exc:
            self.error(str(exc))

    def restore_params(self, *_):
        dialog = Gtk.MessageDialog(self, 0, Gtk.MessageType.QUESTION, Gtk.ButtonsType.YES_NO, "恢复26套默认配方到RAM？")
        dialog.format_secondary_text("恢复后请点击保存。")
        result = dialog.run()
        dialog.destroy()
        if result == Gtk.ResponseType.YES:
            self.send_action("restoreParam")

    def set_rs485(self, *_):
        try:
            text = self.rs485_combo.get_active_text() or "1"
            station = int(text.split(" ", 1)[0])
            raw = float(self.rs485_entry.get_text())
            value = round(raw * 10) if station == 1 else round(raw)
            max_value = 100 if station == 1 else (90 if station == 2 else 100)
            value = max(0, min(max_value, value))
            self.state.send(core.CMD_MANUAL_RS485, core.struct.pack("<HH", station, value))
        except Exception as exc:
            self.error(str(exc))

    def output_changed(self, switch, _param, output_id):
        if self.updating_outputs:
            return
        try:
            payload = core.struct.pack("<HHI", output_id, 1 if switch.get_active() else 0, 0)
            self.state.send(core.CMD_MANUAL_OUTPUT, payload)
        except Exception as exc:
            self.error(str(exc))

    def browse_firmware(self, *_):
        dialog = Gtk.FileChooserDialog("选择 App.bin", self, Gtk.FileChooserAction.OPEN, ("取消", Gtk.ResponseType.CANCEL, "选择", Gtk.ResponseType.OK))
        filt = Gtk.FileFilter()
        filt.set_name("App binary")
        filt.add_pattern("*.bin")
        dialog.add_filter(filt)
        if dialog.run() == Gtk.ResponseType.OK:
            self.firmware_entry.set_text(dialog.get_filename())
        dialog.destroy()

    def query_device(self, *_):
        port = self.port_combo.get_active_text()
        if not port:
            self.error("请选择串口")
            return
        threading.Thread(target=self._query_device_worker, args=(port,), daemon=True).start()

    def _query_device_worker(self, port):
        try:
            info = core.query_iap(port)
            GLib.idle_add(self.upgrade_status.set_text, "\n".join(f"{k}: {v}" for k, v in info.items()))
        except Exception as exc:
            GLib.idle_add(self.error, str(exc))

    def upgrade(self, *_):
        port = self.port_combo.get_active_text()
        if not port:
            self.error("请选择串口")
            return
        args = (port, self.firmware_entry.get_text(), self.version_entry.get_text())
        self.upgrade_status.set_text("升级任务已开始，请看日志")
        threading.Thread(target=self._upgrade_worker, args=args, daemon=True).start()

    def _upgrade_worker(self, port, firmware, version):
        try:
            core.upgrade_iap(port, firmware, version)
            GLib.idle_add(self.upgrade_status.set_text, "升级完成")
        except Exception as exc:
            GLib.idle_add(self.upgrade_status.set_text, "升级失败: " + str(exc))

    def start_test(self, *_):
        try:
            with self.state.lock:
                self.state.samples.clear()
                self.state.test_running = True
                self.state.test_started_at = time.time()
                self.state.last_sample_at = 0
                self.state.sample_interval = max(1, min(3600, float(self.test_interval.get_text())))
            self.state.send(core.CMD_PROCESS, core.process_payload(5, self.test_stage.get_active()))
        except Exception as exc:
            self.error(str(exc))

    def stop_test(self, *_):
        try:
            self.state.send(core.CMD_PROCESS, core.process_payload(4, 0))
            core.save_samples()
            with self.state.lock:
                self.state.test_running = False
        except Exception as exc:
            self.error(str(exc))

    def compat_poll(self):
        if self.poll_check.get_active() and self.state.status["connected"]:
            try:
                self.state.poll_once()
            except Exception as exc:
                self.state.add_log("POLL ERROR: " + str(exc))
        return True

    def refresh_state(self):
        page = self.current_page
        snap = self.state.snapshot(
            include_logs=page == "upgrade",
            include_samples=page == "test",
            sample_limit=600,
        )
        connected = snap["connected"]
        set_text_if_changed(self.connection_label, f"上位机已连接 · {snap['port']}" if connected else "上位机未连接")
        connection_style = self.connection_label.get_style_context()
        if connected:
            connection_style.add_class("connected")
        else:
            connection_style.remove_class("connected")
        set_text_if_changed(self.connect_button, "断开APP" if connected else "连接APP")
        if page == "main":
            for key, label in self.metric_labels.items():
                set_text_if_changed(label, str(snap["metrics"].get(key, "--")))
            set_text_if_changed(self.flag_label, "状态标志 " + str(snap["flags"]))
            for key, label in self.telemetry_labels.items():
                value = snap["telemetry"].get(key)
                set_text_if_changed(label, "--" if not value or value[0] is None else f"{value[0]} {value[1]}")
            for key, label in self.power_labels.items():
                value = snap["power"].get(key)
                set_text_if_changed(label, "--" if not value or value[0] is None else f"{value[0]}{value[1]}")
            pending = snap["pendingInteraction"]
            pending_text = "" if not pending else f"等待交互：{pending['text']} request={pending['id']}"
            set_text_if_changed(self.pending_label, pending_text)
            self.pending_label.set_visible(bool(pending_text))
            self.render_io(snap["io"], preview=True)
        elif page == "param":
            set_text_if_changed(self.param_revision, f"参数版本 {snap['paramRevision'] or '--'}")
            for key, value in snap["parameters"].items():
                entry = self.param_entries.get(key)
                if entry and not entry.has_focus() and entry.get_text() != str(value):
                    entry.set_text(str(value))
        elif page == "io":
            self.render_io(snap["io"], outputs=True)
        elif page == "upgrade":
            self.render_logs(snap["logs"])
        elif page == "test":
            self.render_samples(snap["samples"], snap["testRunning"])
        return True

    def render_io(self, io, preview=False, outputs=False):
        if preview:
            rows = io.get("inputs", []) + io.get("outputs", [])
            signature = tuple((item.get("name"), bool(item.get("state"))) for item in rows[:60])
            if signature != self.last_io_signature:
                self.last_io_signature = signature
                self.update_io_preview(rows[:60])
        if outputs:
            self.updating_outputs = True
            for item in io.get("outputs", []):
                sw = self.output_switches.get(item["id"])
                state = bool(item["state"])
                if sw and self.output_states.get(item["id"]) != state:
                    self.output_states[item["id"]] = state
                    sw.set_active(state)
            self.updating_outputs = False

    def update_io_preview(self, rows):
        for item in rows:
            dot = self.io_preview_rows.get(item["name"])
            if not dot:
                continue
            ctx = dot.get_style_context()
            if item.get("state"):
                ctx.add_class("dot-on")
                ctx.remove_class("dot-off")
            else:
                ctx.add_class("dot-off")
                ctx.remove_class("dot-on")

    def render_logs(self, logs):
        text = "\n".join(logs)
        if text != self.last_log_text:
            self.last_log_text = text
            self.log_buffer.set_text(text)
            end_iter = self.log_buffer.get_end_iter()
            if self.log_mark is None:
                self.log_mark = self.log_buffer.create_mark("log-end", end_iter, False)
            else:
                self.log_buffer.move_mark(self.log_mark, end_iter)
            self.log_text_view.scroll_to_mark(self.log_mark, 0.0, True, 0.0, 1.0)

    def render_samples(self, samples, running):
        set_text_if_changed(self.test_status, ("测试中" if running else "未开始") + f"，已记录 {len(samples)} 个点")
        last_sample = samples[-1] if samples else {}
        signature = (len(samples), last_sample.get("time"), last_sample.get("seconds"), running)
        if signature != self.last_samples_signature:
            self.last_samples_signature = signature
            self.chart_samples = samples
            self.queue_chart_redraw()
        selected = [check.get_label() for check in getattr(self, "test_record_checks", []) if check.get_active()]
        selected_text = "、".join(selected) if selected else "温度"
        if hasattr(self, "chart_summary"):
            set_text_if_changed(self.chart_summary, f"{selected_text}曲线 · {self.state.sample_interval:g}秒/点 · 单流程 {self.test_stage.get_active_text()}")

    def queue_chart_redraw(self):
        if hasattr(self, "chart_area"):
            self.chart_area.queue_draw()

    def draw_chart(self, area, cr):
        alloc = area.get_allocation()
        width, height = max(1, alloc.width), max(1, alloc.height)
        cr.set_source_rgb(0.06, 0.08, 0.11)
        cr.paint()
        left, top, right, bottom = 58, 24, 24, 38
        plot_w = max(1, width - left - right)
        plot_h = max(1, height - top - bottom)
        cr.set_source_rgb(0.17, 0.23, 0.29)
        cr.rectangle(left, top, plot_w, plot_h)
        cr.stroke()
        cr.set_line_width(1)
        for i in range(6):
            y = top + plot_h * i / 5.0
            cr.set_source_rgb(0.14, 0.19, 0.24)
            cr.move_to(left, y)
            cr.line_to(left + plot_w, y)
            cr.stroke()
            cr.set_source_rgb(0.55, 0.62, 0.70)
            cr.move_to(14, y + 4)
            cr.show_text(str(100 - i * 20))

        samples = self.chart_samples
        if not samples:
            cr.set_source_rgb(0.55, 0.62, 0.70)
            cr.move_to(left + 24, top + 36)
            cr.show_text("点击开始测试后，对应遥测曲线会显示在这里")
            return False

        max_sec = max(1.0, samples[-1].get("seconds") or 1.0)
        sample_step = max(1, len(samples) // max(1, int(plot_w)))
        visible_samples = samples[::sample_step]
        if visible_samples[-1] is not samples[-1]:
            visible_samples = visible_samples + [samples[-1]]
        series = [
            ("温度", "cabinTemp", (0.20, 0.55, 1.00)),
            ("湿度", "cabinHumidity", (0.94, 0.62, 0.12)),
            ("氧气", "cabinOxygen", (0.57, 0.43, 1.00)),
            ("臭氧", "cabinOzone", (0.62, 0.70, 0.80)),
            ("风机功率", "fan", (0.20, 0.76, 0.43)),
            ("加热棒功率", "heater", (0.95, 0.30, 0.25)),
        ]
        enabled = {check.get_label() for check in getattr(self, "test_record_checks", []) if check.get_active()}
        if not enabled:
            enabled = {"温度"}
        legend_x = left + 10
        for label, key, color in series:
            if label not in enabled:
                continue
            cr.set_source_rgb(*color)
            cr.set_line_width(2.2)
            started = False
            for sample in visible_samples:
                value = sample.get(key)
                if value is None:
                    continue
                x = left + ((sample.get("seconds") or 0) / max_sec) * plot_w
                y = top + (100 - max(0, min(100, float(value)))) / 100.0 * plot_h
                if not started:
                    cr.move_to(x, y)
                    started = True
                else:
                    cr.line_to(x, y)
            cr.stroke()
            cr.rectangle(legend_x, top + 10, 12, 8)
            cr.fill()
            cr.set_source_rgb(0.85, 0.89, 0.93)
            cr.move_to(legend_x + 18, top + 18)
            cr.show_text(label)
            legend_x += 92
        return False

    def error(self, message):
        dialog = Gtk.MessageDialog(self, 0, Gtk.MessageType.ERROR, Gtk.ButtonsType.OK, message)
        dialog.run()
        dialog.destroy()

    def on_destroy(self, *_):
        self.state.close()
        Gtk.main_quit()

def page_box():
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
    box.get_style_context().add_class("page")
    box.set_hexpand(True)
    box.set_vexpand(True)
    return box


def card(title, expand=True, trailing=None):
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
    box.get_style_context().add_class("card")
    box.set_hexpand(True)
    box.set_vexpand(expand)
    header = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
    header.get_style_context().add_class("card-header")
    label = Gtk.Label(label=title, xalign=0)
    label.get_style_context().add_class("card-title")
    header.pack_start(label, True, True, 0)
    if trailing is not None:
        header.pack_start(trailing, False, False, 0)
    box.pack_start(header, False, False, 0)
    return box


def group_box(title):
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
    box.get_style_context().add_class("group")
    box.set_hexpand(True)
    box.set_vexpand(False)
    box.set_valign(Gtk.Align.START)
    label = Gtk.Label(label=title, xalign=0)
    label.get_style_context().add_class("group-title")
    box.pack_start(label, False, False, 0)
    return box


def metric_box(name):
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
    box.get_style_context().add_class("metric")
    box.set_hexpand(True)
    label = Gtk.Label(label=name, xalign=0)
    label.set_ellipsize(Pango.EllipsizeMode.END)
    label.get_style_context().add_class("metric-name")
    value = Gtk.Label(label="--", xalign=0)
    value.get_style_context().add_class("metric-value")
    box.pack_start(label, False, False, 0)
    box.pack_start(value, False, False, 0)
    return box, value


def button(label, callback, classes=""):
    btn = Gtk.Button(label=label)
    btn.set_hexpand(False)
    for cls in classes.split():
        btn.get_style_context().add_class(cls)
    btn.connect("clicked", callback)
    return btn


class OutputToggle(Gtk.ToggleButton):
    def __init__(self):
        super().__init__(label="关")
        self.set_size_request(54, 30)
        self.set_hexpand(False)
        self.set_vexpand(False)
        self.set_valign(Gtk.Align.CENTER)
        self.get_style_context().add_class("io-toggle")
        self.connect("toggled", self._sync_label)

    def _sync_label(self, *_):
        self.set_label("开" if self.get_active() else "关")


def combo(items):
    box = Gtk.ComboBoxText()
    box.set_hexpand(False)
    for item in items:
        box.append_text(item)
    if items:
        box.set_active(0)
    return box


def labeled(text, widget):
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
    box.pack_start(Gtk.Label(label=text, xalign=0), False, False, 0)
    box.pack_start(widget, False, False, 0)
    return box


def labeled_inline(text, widget):
    box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
    box.set_hexpand(True)
    label = Gtk.Label(label=text, xalign=0)
    label.get_style_context().add_class("hint")
    box.pack_start(label, False, False, 0)
    widget.set_hexpand(True)
    box.pack_start(widget, True, True, 0)
    return box


def fmt(value):
    return "--" if value is None else str(value)


def set_text_if_changed(widget, text):
    current = widget.get_label() if isinstance(widget, Gtk.Button) else widget.get_text()
    if current == text:
        return
    if isinstance(widget, Gtk.Button):
        widget.set_label(text)
    else:
        widget.set_text(text)


class ResponsiveGrid(Gtk.Grid):
    def __init__(self, min_child_width=240, max_columns=4, column_spacing=8, row_spacing=8):
        super().__init__(column_spacing=column_spacing, row_spacing=row_spacing)
        self.min_child_width = min_child_width
        self.max_columns = max_columns
        self.items = []
        self.expand_items = set()
        self.current_columns = max_columns
        self.set_hexpand(True)
        self.set_vexpand(False)
        self.set_valign(Gtk.Align.START)
        self.set_column_homogeneous(True)
        self.connect("size-allocate", self._on_size_allocate)

    def add_item(self, widget, expand=True):
        widget.set_hexpand(True)
        widget.set_vexpand(expand)
        self.items.append(widget)
        if expand:
            self.expand_items.add(widget)
        index = len(self.items) - 1
        self.attach(widget, index % self.current_columns, index // self.current_columns, 1, 1)

    def _on_size_allocate(self, _widget, allocation):
        self.reflow_for_width(allocation.width)

    def reflow_for_width(self, width):
        if width <= 1:
            return
        columns = max(1, min(self.max_columns, width // self.min_child_width))
        self.reflow(int(columns))

    def reflow(self, columns):
        if not self.items:
            return
        columns = max(1, min(self.max_columns, columns))
        if columns == self.current_columns and all(item.get_parent() is self for item in self.items):
            return
        for child in list(self.get_children()):
            self.remove(child)
        for index, item in enumerate(self.items):
            self.attach(item, index % columns, index // columns, 1, 1)
            item.set_hexpand(True)
            item.set_vexpand(item in self.expand_items)
        self.current_columns = columns


def main():
    WorkbenchWindow().show_all()
    Gtk.main()


if __name__ == "__main__":
    main()
