#!/usr/bin/env python3
"""Telemetry page — sensor details, process time, chart."""

import gi

gi.require_version("Gtk", "3.0")
gi.require_foreign("cairo")
from gi.repository import Gtk

from ..widgets import ResponsiveGrid, card, sensor_tile
from ..i18n import tr, sensor_name


SENSORS = [
    ("cabin_ext_temp", "\u2103"),
    ("cabin_ext_humi", "%"),
    ("cabin_oxygen", "%"),
    ("inlet_water_temp", "\u2103"),
    ("tank_water_temp", "\u2103"),
    ("outlet_water_temp", "\u2103"),
    ("reserve_temp", "\u2103"),
    ("cabin_temp", "\u2103"),
    ("cabin_humidity", "%"),
    ("cabin_ozone", "ppm"),
]


class TelemetryPage(Gtk.Box):

    def __init__(self):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        self.get_style_context().add_class("page")
        self.set_hexpand(True)
        self.set_vexpand(True)

        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        self.pack_start(scroll, True, True, 0)

        c = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        c.set_margin_start(24)
        c.set_margin_end(24)
        c.set_margin_top(20)
        c.set_margin_bottom(20)
        scroll.add(c)

        # Sensors
        sc = card(tr("tel.sensor_data"))
        self._sensor_card_title = _get_card_title(sc)
        self.sensors = {}
        self._sensor_name_labels = {}
        sg = ResponsiveGrid(min_child_width=150, max_columns=5,
                            column_spacing=10, row_spacing=10)
        for key, unit in SENSORS:
            name = sensor_name(key)
            _, vl = sensor_tile(name, value_text="--", unit=unit)
            self.sensors[key] = vl
            self._sensor_name_labels[key] = _find_name_label(vl)
            sg.add_item(vl.get_parent().get_parent())
        sc.pack_start(sg, False, False, 0)

        self.flag_label = Gtk.Label(
            label=f"{tr('tel.status_flags')} --", xalign=0)
        self.flag_label.get_style_context().add_class("card-meta")
        sc.pack_start(self.flag_label, False, False, 0)
        c.pack_start(sc, False, False, 0)

        # Process time
        tc = card(tr("tel.process_time"))
        self._time_card_title = _get_card_title(tc)
        tb = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=24)
        self.total_label = Gtk.Label(label=f"{tr('tel.total')}: --")
        self.total_label.get_style_context().add_class("metric-value")
        self.stage_label = Gtk.Label(label=f"{tr('tel.stage')}: --")
        self.stage_label.get_style_context().add_class("metric-value")
        tb.pack_start(self.total_label, False, False, 0)
        tb.pack_start(self.stage_label, False, False, 0)
        tc.pack_start(tb, False, False, 0)
        c.pack_start(tc, False, False, 0)

        # Chart
        cc = card(tr("tel.realtime_curves"))
        self._chart_card_title = _get_card_title(cc)
        self.chart_summary = Gtk.Label(
            label=tr("tel.curves_hint"), xalign=0)
        self.chart_summary.get_style_context().add_class("hint")
        cc.pack_start(self.chart_summary, False, False, 0)
        self.chart_area = Gtk.DrawingArea()
        self.chart_area.set_size_request(-1, 360)
        self.chart_area.get_style_context().add_class("chart")
        self.chart_area.set_hexpand(True)
        self.chart_area.set_vexpand(True)
        cc.pack_start(self.chart_area, True, True, 0)
        c.pack_start(cc, True, True, 0)

        self.last_sig = None

    def update_state(self, state):
        t = state.get("telemetry_ui", {})
        for key, _ in SENSORS:
            v = t.get(key)
            self.sensors[key].set_text(f"{v:.1f}" if v is not None else "--")

        self.flag_label.set_text(
            f"{tr('tel.status_flags')} {state.get('flags', '--')}")

        pt = state.get("process_time", {})
        tr_val = pt.get("total_remaining_ms", 0)
        sr = pt.get("stage_remaining_ms", 0)
        if tr_val:
            m, s = divmod(tr_val // 1000, 60)
            self.total_label.set_text(f"{tr('tel.total')}: {m}:{s:02d}")
        else:
            self.total_label.set_text(f"{tr('tel.total')}: --")
        if sr:
            m, s = divmod(sr // 1000, 60)
            self.stage_label.set_text(f"{tr('tel.stage')}: {m}:{s:02d}")
        else:
            self.stage_label.set_text(f"{tr('tel.stage')}: --")

        sig = len(state.get("samples", []))
        if sig != self.last_sig:
            self.last_sig = sig
            self.chart_area.queue_draw()

    def refresh_language(self):
        self._sensor_card_title.set_text(tr("tel.sensor_data"))
        self._time_card_title.set_text(tr("tel.process_time"))
        self._chart_card_title.set_text(tr("tel.realtime_curves"))
        self.chart_summary.set_text(tr("tel.curves_hint"))
        for key, _ in SENSORS:
            lbl = self._sensor_name_labels.get(key)
            if lbl:
                lbl.set_text(sensor_name(key))


def _get_card_title(card_box):
    for child in card_box.get_children():
        if isinstance(child, Gtk.Box):
            for sub in child.get_children():
                if isinstance(sub, Gtk.Label):
                    ctx = sub.get_style_context()
                    if "card-title" in ctx.list_classes():
                        return sub
    return None


def _find_name_label(value_widget):
    parent = value_widget.get_parent()
    if parent:
        grandparent = parent.get_parent()
        if grandparent:
            for child in grandparent.get_children():
                if child != parent and isinstance(child, Gtk.Label):
                    return child
    return value_widget
