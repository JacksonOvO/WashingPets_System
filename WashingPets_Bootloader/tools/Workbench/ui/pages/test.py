#!/usr/bin/env python3
"""Test page — single-flow record, self-test, test curves.

Ported from the legacy workbench test screen (APP_IAP/linux_workbench/gtk_app.py):
test conditions, record items, start/stop single-flow test, quick/deep self-test
and a cairo test-curve chart with CSV/PNG export.
"""

import time

import cairo
import gi

gi.require_version("Gtk", "3.0")
gi.require_foreign("cairo")
from gi.repository import Gtk

import server as core
from ..widgets import ResponsiveGrid, card, group_box, combo, labeled
from ..i18n import tr


# (i18n key, sample field, RGB color)
SERIES = [
    ("test.series.temp",     "cabinTemp",    (0.20, 0.55, 1.00)),
    ("test.series.humidity", "cabinHumidity", (0.94, 0.62, 0.12)),
    ("test.series.oxygen",   "cabinOxygen",  (0.57, 0.43, 1.00)),
    ("test.series.ozone",    "cabinOzone",   (0.62, 0.70, 0.80)),
    ("test.series.fan",      "fan",          (0.20, 0.76, 0.43)),
    ("test.series.heater",   "heater",       (0.95, 0.30, 0.25)),
]
SERIES_DEFAULT_ON = {"test.series.temp", "test.series.humidity"}


class TestPage(Gtk.Box):

    def __init__(self, send_action):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.get_style_context().add_class("page")
        self.set_hexpand(True)
        self.set_vexpand(True)
        self._send = send_action
        self._chart_samples = []
        self._last_samples_sig = None
        self._selftest_sig = None
        self._cmd_sent_at = None
        self._device_confirmed = False
        self._last_seen_nack_sig = None
        self._status_override = None

        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroll.set_overlay_scrolling(True)
        self.pack_start(scroll, True, True, 0)

        c = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        c.set_margin_start(20)
        c.set_margin_end(20)
        c.set_margin_top(16)
        c.set_margin_bottom(16)
        scroll.add(c)

        c.pack_start(self._build_record_card(), False, False, 0)
        c.pack_start(self._build_selftest_card(), False, False, 0)
        c.pack_start(self._build_curve_card(), True, True, 0)

    # ── Card builders ────────────────────────────────────────────────────────

    def _build_record_card(self):
        panel = card(tr("test.card_record"), expand=False)
        self._record_card_title = _get_card_title(panel)

        outer = ResponsiveGrid(min_child_width=300, max_columns=3,
                               column_spacing=14, row_spacing=12)

        cond = group_box(tr("test.conditions"))
        self._cond_title = _get_group_title(cond)
        self.interval_entry = Gtk.Entry(text="30")
        self.stage_combo = combo(core.STAGES)
        self.stage_combo.set_active(6)
        cond.pack_start(labeled(tr("test.interval"), self.interval_entry), False, False, 0)
        cond.pack_start(labeled(tr("test.stage"), self.stage_combo), False, False, 8)
        self._interval_label = _first_label_of(cond, 0)
        self._stage_label = _first_label_of(cond, 2)
        self._cond_hint = Gtk.Label(label=tr("test.cond_hint"), xalign=0)
        self._cond_hint.get_style_context().add_class("hint")
        self._cond_hint.set_line_wrap(True)
        cond.pack_start(self._cond_hint, False, False, 8)
        outer.add_item(cond)

        series = group_box(tr("test.series"))
        self._series_title = _get_group_title(series)
        self._series_checks = []
        for key, _field, _color in SERIES:
            check = Gtk.CheckButton(label=tr(key))
            check.set_active(key in SERIES_DEFAULT_ON)
            check.connect("toggled", lambda *_: self._queue_chart_redraw())
            self._series_checks.append((key, check))
            series.pack_start(check, False, False, 0)
        outer.add_item(series)

        action = group_box(tr("test.exec"))
        self._exec_title = _get_group_title(action)
        self._btn_start = Gtk.Button(label=tr("test.start"))
        self._btn_start.get_style_context().add_class("start")
        self._btn_start.set_size_request(-1, 42)
        self._btn_start.connect("clicked", self._start_test)
        action.pack_start(self._btn_start, False, False, 0)
        self._btn_stop = Gtk.Button(label=tr("test.stop_save"))
        self._btn_stop.get_style_context().add_class("stop")
        self._btn_stop.set_size_request(-1, 42)
        self._btn_stop.connect("clicked", self._stop_test)
        action.pack_start(self._btn_stop, False, False, 8)
        self.status_label = Gtk.Label(label=tr("test.status_idle"), xalign=0)
        self.status_label.get_style_context().add_class("hint")
        self.status_label.set_line_wrap(True)
        action.pack_start(self.status_label, False, False, 8)
        self.device_label = Gtk.Label(label=tr("test.device"), xalign=0)
        self.device_label.get_style_context().add_class("hint")
        self.device_label.set_line_wrap(True)
        action.pack_start(self.device_label, False, False, 2)
        outer.add_item(action)

        panel.pack_start(outer, False, False, 0)
        return panel

    def _build_selftest_card(self):
        panel = card(tr("test.card_selftest"), expand=False)
        self._selftest_card_title = _get_card_title(panel)

        row = ResponsiveGrid(min_child_width=170, max_columns=4,
                             column_spacing=8, row_spacing=8)
        self._btn_selftest_quick = Gtk.Button(label=tr("test.selftest_quick"))
        self._btn_selftest_quick.get_style_context().add_class("start")
        self._btn_selftest_quick.connect("clicked", lambda *_: self._send("selfTest", mode=0))
        row.add_item(self._btn_selftest_quick, expand=True)
        self._btn_selftest_deep = Gtk.Button(label=tr("test.selftest_deep"))
        self._btn_selftest_deep.get_style_context().add_class("start")
        self._btn_selftest_deep.connect("clicked", lambda *_: self._send("selfTest", mode=1))
        row.add_item(self._btn_selftest_deep, expand=True)
        self._btn_selftest_stop = Gtk.Button(label=tr("test.selftest_stop"))
        self._btn_selftest_stop.get_style_context().add_class("stop")
        self._btn_selftest_stop.connect("clicked", lambda *_: self._send("process", value=4))
        row.add_item(self._btn_selftest_stop, expand=True)
        self._selftest_hint = Gtk.Label(label=tr("test.selftest_hint"), xalign=0)
        self._selftest_hint.get_style_context().add_class("hint")
        self._selftest_hint.set_line_wrap(True)
        row.add_item(self._selftest_hint, expand=True)
        panel.pack_start(row, False, False, 0)

        self.selftest_summary = Gtk.Label(label=tr("test.selftest_no_result"), xalign=0)
        self.selftest_summary.get_style_context().add_class("hint")
        panel.pack_start(self.selftest_summary, False, False, 8)

        self.selftest_flow = Gtk.FlowBox()
        self.selftest_flow.set_selection_mode(Gtk.SelectionMode.NONE)
        self.selftest_flow.set_max_children_per_line(6)
        panel.pack_start(self.selftest_flow, False, False, 0)
        return panel

    def _build_curve_card(self):
        panel = card(tr("test.card_curve"))
        self._curve_card_title = _get_card_title(panel)
        self.chart_summary = Gtk.Label(label="", xalign=0)
        self.chart_summary.get_style_context().add_class("hint")
        panel.pack_start(self.chart_summary, False, False, 0)
        self.chart_area = Gtk.DrawingArea()
        self.chart_area.set_size_request(-1, 360)
        self.chart_area.set_hexpand(True)
        self.chart_area.set_vexpand(True)
        self.chart_area.get_style_context().add_class("chart")
        self.chart_area.connect("draw", self._on_draw)
        panel.pack_start(self.chart_area, True, True, 0)
        self._update_chart_summary()
        return panel

    # ── Test actions ─────────────────────────────────────────────────────────

    def _start_test(self, *_):
        # 单阶段测试仅手动模式接受：先本地预检，避免进入"在记录但流程没跑"的状态。
        with core.STATE.lock:
            mode = core.STATE.status.get("metrics", {}).get("work_mode")
        if mode is not None and mode != 1:
            self.status_label.set_text(tr("err.stage_test_mode"))
            return
        try:
            interval = max(1.0, min(3600.0, float(self.interval_entry.get_text())))
        except ValueError:
            self.status_label.set_text(tr("test.interval") + " ?")
            return
        with core.STATE.lock:
            core.STATE.samples.clear()
            core.STATE.test_running = True
            core.STATE.test_started_at = time.time()
            core.STATE.last_sample_at = 0
            core.STATE.sample_interval = interval
        self._chart_samples = []
        self._queue_chart_redraw()
        self._cmd_sent_at = time.time()
        self._device_confirmed = False
        self._status_override = ("test.cmd_sent", {})
        self.status_label.set_text(tr("test.cmd_sent"))
        self._send("process", value=5, param=self.stage_combo.get_active())

    def _stop_test(self, *_):
        try:
            self._send("process", value=4)
            csv_path = core.save_samples()
            with core.STATE.lock:
                core.STATE.test_running = False
            self._cmd_sent_at = None
            self._device_confirmed = False
            self._status_override = None
            if csv_path:
                png_path = self._save_chart_png()
                self.status_label.set_text(tr("test.saved").format(
                    dir=csv_path.parent, csv=csv_path.name, png=png_path.name))
            else:
                self.status_label.set_text(tr("test.status_idle"))
        except Exception as exc:
            self.status_label.set_text(str(exc))

    def _save_chart_png(self):
        alloc = self.chart_area.get_allocation()
        width, height = max(640, alloc.width), max(360, alloc.height)
        surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, width, height)
        self._render_chart(cairo.Context(surface), width, height)
        out_dir = core.desktop_dir() / "测试曲线"
        out_dir.mkdir(parents=True, exist_ok=True)
        path = out_dir / (time.strftime("linux_workbench_%Y%m%d_%H%M%S") + ".png")
        surface.write_to_png(str(path))
        core.STATE.add_log("CHART SAVED: " + str(path))
        return path

    # ── State refresh ────────────────────────────────────────────────────────

    def update_state(self, state):
        samples = state.get("samples", [])
        running = state.get("testRunning", False)
        metrics = state.get("metrics", {})
        self._update_device_label(metrics, state.get("connected"))
        self._update_cmd_feedback(metrics, state.get("last_nack"))

        state_text = tr("test.status_running") if running else tr("test.status_idle")
        if samples:
            self._status_override = None
            text = tr("test.status").format(state=state_text, n=len(samples))
        elif self._status_override is not None:
            key, kwargs = self._status_override
            text = tr(key).format(**kwargs)
        else:
            current = self.status_label.get_text()
            if current in (tr("test.status_idle"), tr("test.status_running"),
                           tr("test.status").format(state=tr("test.status_idle"), n=0)):
                text = state_text
            else:
                text = current
        if self.status_label.get_text() != text:
            self.status_label.set_text(text)

        last = samples[-1] if samples else {}
        sig = (len(samples), last.get("time"), last.get("seconds"), running)
        if sig != self._last_samples_sig:
            self._last_samples_sig = sig
            self._chart_samples = samples
            self._queue_chart_redraw()
        self._update_chart_summary()
        self._render_self_test(state.get("self_test"))

    def _update_device_label(self, metrics, connected):
        if not connected or not metrics:
            text = tr("test.device")
        else:
            parts = [str(metrics.get(k, "--"))
                     for k in ("模式", "设备状态", "运行类型", "当前流程")]
            text = tr("test.device_prefix") + " · ".join(parts)
        if self.device_label.get_text() != text:
            self.device_label.set_text(text)

    def _update_cmd_feedback(self, metrics, nack):
        nack_sig = None
        if nack:
            nack_sig = (nack.get("request_cmd"), nack.get("error"),
                        nack.get("detail"))
        if self._cmd_sent_at is None or self._device_confirmed:
            return
        # 设备拒绝了流程指令：显示原因并停止记录，避免"在记录但流程没跑"。
        if (nack and nack.get("request_cmd") == core.CMD_PROCESS
                and nack_sig != self._last_seen_nack_sig):
            self._last_seen_nack_sig = nack_sig
            self._cmd_sent_at = None
            self._status_override = ("test.nack", {"text": nack.get("text", "")})
            with core.STATE.lock:
                core.STATE.test_running = False
            return
        # 固件进入单阶段测试后上报模式 3。
        if metrics.get("work_mode") == 3:
            self._device_confirmed = True
            if not self._status_override or self._status_override[0] == "test.cmd_sent":
                self._status_override = ("test.device_entered", {})
            return
        if time.time() - self._cmd_sent_at > 8.0:
            self._status_override = ("test.no_response", {})

    def _render_self_test(self, result):
        if not result:
            return
        summary = result.get("summary") or tr("test.card_selftest")
        self.selftest_summary.set_text(summary)
        items = result.get("items", [])
        sig = (result.get("result_id"),
               tuple((it["name"], it["passed"]) for it in items))
        if sig == self._selftest_sig:
            return
        self._selftest_sig = sig
        for child in self.selftest_flow.get_children():
            self.selftest_flow.remove(child)
        for it in items:
            chip = Gtk.Label(label=it["name"])
            verdict = tr("test.selftest_passed") if it["passed"] else tr("test.selftest_failed")
            chip.set_tooltip_text(f"{verdict}：{it['name']}")
            chip.get_style_context().add_class(
                "selftest-pass" if it["passed"] else "selftest-fail")
            self.selftest_flow.add(chip)
        self.selftest_flow.show_all()

    # ── Chart ────────────────────────────────────────────────────────────────

    def _enabled_series(self):
        enabled = {key for key, check in self._series_checks if check.get_active()}
        return enabled or {"test.series.temp"}

    def _update_chart_summary(self):
        selected = "、".join(tr(key) for key, _f, _c in SERIES
                             if key in self._enabled_series())
        self.chart_summary.set_text(tr("test.curve_summary").format(
            series=selected,
            interval=f"{core.STATE.sample_interval:g}",
            stage=self.stage_combo.get_active_text() or "--"))

    def _queue_chart_redraw(self):
        self.chart_area.queue_draw()

    def _on_draw(self, area, cr):
        alloc = area.get_allocation()
        self._render_chart(cr, max(1, alloc.width), max(1, alloc.height))
        return False

    def _render_chart(self, cr, width, height):
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

        samples = self._chart_samples
        if not samples:
            cr.set_source_rgb(0.55, 0.62, 0.70)
            cr.move_to(left + 24, top + 36)
            cr.show_text(tr("test.curve_empty"))
            return

        max_sec = max(1.0, samples[-1].get("seconds") or 1.0)
        sample_step = max(1, len(samples) // max(1, int(plot_w)))
        visible = samples[::sample_step]
        if visible[-1] is not samples[-1]:
            visible = visible + [samples[-1]]
        enabled = self._enabled_series()
        legend_x = left + 10
        for key, field, color in SERIES:
            if key not in enabled:
                continue
            cr.set_source_rgb(*color)
            cr.set_line_width(2.2)
            started = False
            for sample in visible:
                value = sample.get(field)
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
            cr.show_text(tr(key))
            legend_x += 110

    # ── Language ─────────────────────────────────────────────────────────────

    def refresh_language(self):
        self._record_card_title.set_text(tr("test.card_record"))
        self._selftest_card_title.set_text(tr("test.card_selftest"))
        self._curve_card_title.set_text(tr("test.card_curve"))
        self._cond_title.set_text(tr("test.conditions"))
        self._series_title.set_text(tr("test.series"))
        self._exec_title.set_text(tr("test.exec"))
        if self._interval_label:
            self._interval_label.set_text(tr("test.interval"))
        if self._stage_label:
            self._stage_label.set_text(tr("test.stage"))
        self._cond_hint.set_text(tr("test.cond_hint"))
        for key, check in self._series_checks:
            check.set_label(tr(key))
        self._btn_start.set_label(tr("test.start"))
        self._btn_stop.set_label(tr("test.stop_save"))
        if self.device_label.get_text() in (tr("test.device"), "设备：未收到状态", "Device: no status received"):
            self.device_label.set_text(tr("test.device"))
        self._btn_selftest_quick.set_label(tr("test.selftest_quick"))
        self._btn_selftest_deep.set_label(tr("test.selftest_deep"))
        self._btn_selftest_stop.set_label(tr("test.selftest_stop"))
        self._selftest_hint.set_text(tr("test.selftest_hint"))
        if not core.STATE.status.get("self_test"):
            self.selftest_summary.set_text(tr("test.selftest_no_result"))
        self._update_chart_summary()
        self._queue_chart_redraw()


def _get_card_title(card_box):
    for child in card_box.get_children():
        if isinstance(child, Gtk.Box):
            for sub in child.get_children():
                if isinstance(sub, Gtk.Label):
                    ctx = sub.get_style_context()
                    if "card-title" in ctx.list_classes():
                        return sub
    return Gtk.Label(label="")


def _get_group_title(group):
    children = group.get_children()
    if children and isinstance(children[0], Gtk.Label):
        return children[0]
    return Gtk.Label(label="")


def _first_label_of(container, index):
    """Return the label at position `index` inside a vertical box, if any."""
    children = container.get_children()
    if index < len(children) and isinstance(children[index], Gtk.Box):
        for sub in children[index].get_children():
            if isinstance(sub, Gtk.Label):
                return sub
    return None
