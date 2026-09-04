#!/usr/bin/env python3
"""Dashboard page — device control center homepage."""

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Pango

import server as core
from ..widgets import (
    ResponsiveGrid, card, metric_box, sensor_tile, actuator_item, badge,
)
from ..i18n import tr, sensor_name, actuator_name, mode_name, state_name, process_name


TELEMETRY_KEYS = [
    "cabin_ext_temp", "cabin_ext_humi", "cabin_oxygen",
    "inlet_water_temp", "tank_water_temp", "outlet_water_temp",
    "reserve_temp", "cabin_temp", "cabin_humidity", "cabin_ozone",
]

SENSOR_UNITS = {
    "cabin_ext_temp": "\u2103", "cabin_ext_humi": "%",
    "cabin_oxygen": "%", "inlet_water_temp": "\u2103",
    "tank_water_temp": "\u2103", "outlet_water_temp": "\u2103",
    "reserve_temp": "\u2103", "cabin_temp": "\u2103",
    "cabin_humidity": "%", "cabin_ozone": "ppm",
}

ACTUATOR_KEYS = [
    "left_fan_pct", "right_fan_pct", "bottom_fan_pct",
    "left_heater_pct", "right_heater_pct", "bottom_heater_pct",
    "booster_pressure", "valve_angle",
]

ACTUATOR_UNITS = {
    "left_fan_pct": "%", "right_fan_pct": "%", "bottom_fan_pct": "%",
    "left_heater_pct": "%", "right_heater_pct": "%", "bottom_heater_pct": "%",
    "booster_pressure": "kg", "valve_angle": "\u00b0",
}

STATE_MAP = {
    0: ("state.idle", "hero-status-idle"),
    1: ("state.start", "hero-status-idle"),
    2: ("state.running", "hero-status-normal"),
    3: ("state.waiting", "hero-status-warning"),
    4: ("state.paused", "hero-status-warning"),
    5: ("state.stopping", "hero-status-warning"),
    6: ("state.done", "hero-status-normal"),
    7: ("state.fault", "hero-status-error"),
}

ALL_HERO_CLASSES = [
    "hero-status-normal", "hero-status-idle",
    "hero-status-warning", "hero-status-error",
]


class DashboardPage(Gtk.Box):

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

        # Hero
        self.hero = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        self.hero.get_style_context().add_class("hero-status")
        self.hero.set_hexpand(True)

        self.hero_status = Gtk.Label(label=tr("dash.offline"), xalign=0)
        self.hero_status.get_style_context().add_class("hero-status-text")
        self.hero.pack_start(self.hero_status, False, False, 0)

        self.hero_meta = Gtk.Label(
            label=tr("dash.offline_hint"), xalign=0)
        self.hero_meta.get_style_context().add_class("hero-meta")
        self.hero_meta.set_ellipsize(Pango.EllipsizeMode.END)
        self.hero.pack_start(self.hero_meta, False, False, 0)

        c.pack_start(self.hero, False, False, 0)

        # Metrics
        mg = ResponsiveGrid(min_child_width=170, max_columns=4,
                            column_spacing=12, row_spacing=12)
        self._total_name_lbl, self.total_val, total_box = metric_box(tr("dash.total_remaining"))
        mg.add_item(total_box)
        self._stage_name_lbl, self.stage_val, stage_box = metric_box(tr("dash.stage_remaining"))
        mg.add_item(stage_box)
        self._step_name_lbl, self.step_val, step_box = metric_box(tr("dash.current_step"))
        mg.add_item(step_box)
        self._fault_name_lbl, self.fault_val, fault_box = metric_box(tr("dash.active_faults"))
        mg.add_item(fault_box)
        c.pack_start(mg, False, False, 0)

        # Sensors
        self._sensor_card, self._sensor_card_title = card_with_title_ref(tr("dash.sensor_data"))
        self.sensors = {}
        self._sensor_name_labels = {}
        sg = ResponsiveGrid(min_child_width=140, max_columns=5,
                            column_spacing=10, row_spacing=10)
        for key in TELEMETRY_KEYS:
            name = sensor_name(key)
            unit = SENSOR_UNITS[key]
            _, vl = sensor_tile(name, value_text="--", unit=unit)
            self.sensors[key] = vl
            self._sensor_name_labels[key] = _find_name_label(vl)
            sg.add_item(vl.get_parent().get_parent())
        self._sensor_card.pack_start(sg, False, False, 0)
        c.pack_start(self._sensor_card, False, False, 0)

        # Actuators
        self._act_card, self._act_card_title = card_with_title_ref(tr("dash.actuator_status"))
        self.actuators = {}
        self._act_name_labels = {}
        ag = ResponsiveGrid(min_child_width=160, max_columns=4,
                            column_spacing=10, row_spacing=10)
        for key in ACTUATOR_KEYS:
            name = actuator_name(key)
            _, vl, prog = actuator_item(name)
            self.actuators[key] = (vl, prog, ACTUATOR_UNITS[key])
            self._act_name_labels[key] = _find_actuator_name_label(vl)
            ag.add_item(vl.get_parent())
        self._act_card.pack_start(ag, False, False, 0)
        c.pack_start(self._act_card, False, False, 0)

        # Bottom: interaction + fault preview
        bot = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=16)
        bot.set_hexpand(True)

        self.int_card = Gtk.Box(
            orientation=Gtk.Orientation.VERTICAL, spacing=8)
        self.int_card.get_style_context().add_class("interaction-card")
        self.int_card.set_size_request(300, -1)
        self.int_card.set_no_show_all(True)
        self.int_card.hide()
        self._int_title = Gtk.Label(label=tr("dash.interaction_req"), xalign=0)
        self._int_title.get_style_context().add_class("interaction-card-title")
        self.int_card.pack_start(self._int_title, False, False, 0)
        self.int_detail = Gtk.Label(label="", xalign=0)
        self.int_detail.get_style_context().add_class("hint")
        self.int_detail.set_line_wrap(True)
        self.int_card.pack_start(self.int_detail, False, False, 0)
        bot.pack_start(self.int_card, False, False, 0)

        self._fault_card, self._fault_card_title = card_with_title_ref(tr("dash.recent_faults"))
        self.fault_box = Gtk.Box(
            orientation=Gtk.Orientation.VERTICAL, spacing=6)
        self.fault_empty = Gtk.Label(label=tr("dash.no_recent_faults"), xalign=0)
        self.fault_empty.get_style_context().add_class("hint")
        self.fault_box.pack_start(self.fault_empty, False, False, 0)
        self.fault_prev = []
        for _ in range(3):
            lbl = Gtk.Label(label="", xalign=0)
            lbl.get_style_context().add_class("card-meta")
            lbl.set_ellipsize(Pango.EllipsizeMode.END)
            lbl.set_no_show_all(True)
            lbl.hide()
            self.fault_box.pack_start(lbl, False, False, 0)
            self.fault_prev.append(lbl)
        self._fault_card.pack_start(self.fault_box, False, False, 0)
        bot.pack_start(self._fault_card, True, True, 0)
        c.pack_start(bot, False, False, 0)

    def update_state(self, state):
        if not state.get("connected"):
            self.hero_status.set_text(tr("dash.offline"))
            _swap(self.hero_status, ALL_HERO_CLASSES, None)
            self.hero_meta.set_text(tr("dash.offline_hint"))
            return

        m = state.get("metrics", {})
        ds = m.get("device_state", 0)
        wm = m.get("work_mode", 0)
        text_key, cls = STATE_MAP.get(ds, ("state.idle", "hero-status-idle"))
        self.hero_status.set_text(state_name(ds) if ds in STATE_MAP else tr(text_key))
        _swap(self.hero_status, ALL_HERO_CLASSES, cls)

        stage_idx = m.get("stage", 0)
        if stage_idx == -1:
            stage = process_name(m.get("process_type", 0))
        else:
            stage = core.map_name(core.STAGES, stage_idx)
        prog = m.get("progress", 0)
        self.hero_meta.set_text(
            f"{mode_name(wm)}  \u00b7  {stage}  \u00b7  "
            f"{tr('dash.recipe')} {m.get('recipe', 0) + 1}  \u00b7  {prog / 10.0:.1f}%")

        # Metrics
        pt = state.get("process_time", {})
        total_rem = pt.get("total_remaining_ms", 0)
        if total_rem and total_rem > 0:
            self.total_val.set_text(f"{total_rem // 60000}:{(total_rem % 60000) // 1000:02d}")
        else:
            self.total_val.set_text("--")

        stage_rem = pt.get("stage_remaining_ms", 0)
        if stage_rem and stage_rem > 0:
            self.stage_val.set_text(f"{stage_rem // 60000}:{(stage_rem % 60000) // 1000:02d}")
        else:
            self.stage_val.set_text("--")

        step_num = m.get("step", 0)
        self.step_val.set_text(str(step_num) if step_num else "--")

        f = state.get("fault", {})
        ac = f.get("active_code", 0)
        alarms = state.get("faultAlarms", [])
        active_count = sum(1 for a in alarms if a.get("status") == "active")
        self.fault_val.set_text(str(active_count))

        # Sensors
        t = state.get("telemetry_ui", {})
        for key in TELEMETRY_KEYS:
            v = t.get(key)
            self.sensors[key].set_text(f"{v:.1f}" if v is not None else "--")

        # Actuators
        p = state.get("power_ui", {})
        for key in ACTUATOR_KEYS:
            vl, prog, unit = self.actuators[key]
            v = p.get(key)
            if v is not None:
                vl.set_text(f"{v:.1f}{unit}")
                prog.set_fraction(min(v / (100 if "%" in unit else 10), 1.0))
            else:
                vl.set_text(f"--{unit}")
                prog.set_fraction(0)

        # Interaction
        ia = state.get("pendingInteraction")
        if ia:
            self.int_detail.set_text(
                core.describe_interaction(ia.get("stage", 0), ia.get("type", 0)))
            self.int_card.show_all()
        else:
            self.int_card.hide()

        # Fault preview
        alarms = state.get("faultAlarms", [])
        for i, lbl in enumerate(self.fault_prev):
            if i < len(alarms):
                a = alarms[i]
                fc2 = a.get("fault_code", 0)
                lbl.set_text(
                    f"0x{fc2:04X}  {core.FAULT_CODES.get(fc2, '?')}  "
                    f"(#{a.get('event_id', 0)})")
                lbl.show()
            else:
                lbl.hide()
        self.fault_empty.set_visible(not alarms)

    def refresh_language(self):
        self._total_name_lbl.set_text(tr("dash.total_remaining"))
        self._stage_name_lbl.set_text(tr("dash.stage_remaining"))
        self._step_name_lbl.set_text(tr("dash.current_step"))
        self._fault_name_lbl.set_text(tr("dash.active_faults"))
        self._sensor_card_title.set_text(tr("dash.sensor_data"))
        self._act_card_title.set_text(tr("dash.actuator_status"))
        self._int_title.set_text(tr("dash.interaction_req"))
        self._fault_card_title.set_text(tr("dash.recent_faults"))
        self.fault_empty.set_text(tr("dash.no_recent_faults"))
        for key in TELEMETRY_KEYS:
            lbl = self._sensor_name_labels.get(key)
            if lbl:
                lbl.set_text(sensor_name(key))
        for key in ACTUATOR_KEYS:
            lbl = self._act_name_labels.get(key)
            if lbl:
                lbl.set_text(actuator_name(key))


def _swap(w, classes, target):
    ctx = w.get_style_context()
    for c in classes:
        ctx.remove_class(c)
    if target:
        ctx.add_class(target)


def card_with_title_ref(title_text):
    """Create a card and return (card_box, title_label) for later updates."""
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
    box.get_style_context().add_class("card")
    box.set_hexpand(True)
    box.set_vexpand(True)

    header = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
    header.get_style_context().add_class("card-header")

    label = Gtk.Label(label=title_text, xalign=0)
    label.get_style_context().add_class("card-title")
    header.pack_start(label, True, True, 0)

    box.pack_start(header, False, False, 0)
    return box, label


def _find_name_label(value_widget):
    """Walk up from value widget to find the name label in a sensor tile."""
    parent = value_widget.get_parent()
    if parent:
        grandparent = parent.get_parent()
        if grandparent:
            for child in grandparent.get_children():
                if child != parent and isinstance(child, Gtk.Label):
                    return child
    return value_widget


def _find_actuator_name_label(value_widget):
    """Walk up from value widget to find the name label in an actuator item."""
    parent = value_widget.get_parent()
    if parent:
        for child in parent.get_children():
            if child != value_widget and isinstance(child, Gtk.Label):
                return child
    return value_widget
