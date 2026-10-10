#!/usr/bin/env python3
"""IO control page — RS485 manual control and GPIO output switches."""

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Pango

import server as core
from ..widgets import (
    ResponsiveGrid, card, group_box, combo, labeled, OutputToggle,
)
from ..i18n import tr


class IOControlPage(Gtk.Box):

    def __init__(self, send_action):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        self.get_style_context().add_class("page")
        self.set_hexpand(True)
        self.set_vexpand(True)
        self._send = send_action
        self.switches = {}
        self.out_states = {}
        self._updating_out = False

        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        self.pack_start(scroll, True, True, 0)

        c = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        c.set_margin_start(24)
        c.set_margin_end(24)
        c.set_margin_top(20)
        c.set_margin_bottom(20)
        scroll.add(c)

        # RS485
        self._rs485_card = card(tr("io.rs485_manual"))
        self._rs485_card_title = _get_card_title(self._rs485_card)
        rg = ResponsiveGrid(min_child_width=180, max_columns=6,
                            column_spacing=10, row_spacing=10)
        self.rs485_combo = combo(core.RS485_STATIONS)
        rg.add_item(labeled(tr("io.station"), self.rs485_combo))
        self.rs485_entry = Gtk.Entry()
        self.rs485_entry.set_text("0")
        rg.add_item(labeled(tr("io.value"), self.rs485_entry))
        self.act_check = Gtk.CheckButton(label=tr("io.gpio_actuator"))
        self.act_check.connect("toggled", self._act_changed)
        self._btn_start_pump = Gtk.Button(label=tr("io.start_pump"))
        self._btn_start_pump.get_style_context().add_class("start")
        self._btn_start_pump.connect("clicked", lambda _: self._send("booster", start=True))
        rg.add_item(labeled(tr("io.pump"), self._btn_start_pump))
        self._btn_stop_pump = Gtk.Button(label=tr("io.stop_pump"))
        self._btn_stop_pump.get_style_context().add_class("stop")
        self._btn_stop_pump.connect("clicked", lambda _: self._send("booster", start=False))
        rg.add_item(labeled("", self._btn_stop_pump))
        self._btn_set_rs485 = Gtk.Button(label=tr("io.set_rs485"))
        self._btn_set_rs485.get_style_context().add_class("primary")
        self._btn_set_rs485.connect("clicked", self._set_rs485)
        rg.add_item(labeled("", self._btn_set_rs485))
        self._rs485_card.pack_start(rg, False, False, 0)

        self._rs485_hint = Gtk.Label(
            label=tr("io.rs485_hint"), xalign=0)
        self._rs485_hint.get_style_context().add_class("hint")
        self._rs485_hint.set_line_wrap(True)
        self._rs485_card.pack_start(self._rs485_hint, False, False, 0)
        c.pack_start(self._rs485_card, False, False, 0)

        # GPIO Outputs
        self._out_card = card(tr("io.gpio_outputs"))
        self._out_card_title = _get_card_title(self._out_card)
        self._out_hint = Gtk.Label(
            label=tr("io.toggle_hint"), xalign=0)
        self._out_hint.get_style_context().add_class("hint")
        self._out_card.pack_start(self._out_hint, False, False, 0)
        self.io_status = Gtk.Label(label=tr("io.no_status"), xalign=0)
        self.io_status.get_style_context().add_class("card-meta")
        self._out_card.pack_start(self.io_status, False, False, 0)

        og = ResponsiveGrid(min_child_width=250, max_columns=4,
                            column_spacing=8, row_spacing=8)
        for oid in range(1, len(core.OUTPUT_NAMES) + 1):
            if oid == 40:
                self._out_card.pack_start(og, False, False, 0)
                self._out_card.pack_start(Gtk.Label(label="MAGLOCK1–6", xalign=0), False, False, 0)
                og = ResponsiveGrid(min_child_width=250, max_columns=4,
                                    column_spacing=8, row_spacing=8)
            name = core.map_name(core.OUTPUT_NAMES, oid - 1)
            item = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
            item.get_style_context().add_class("output-item")
            item.set_hexpand(True)
            nl = Gtk.Label(label=name, xalign=0)
            nl.set_hexpand(True)
            nl.set_ellipsize(Pango.EllipsizeMode.END)
            item.pack_start(nl, True, True, 0)
            sw = OutputToggle()
            sw.connect("notify::active", self._out_changed, oid)
            self.switches[oid] = sw
            item.pack_start(sw, False, False, 0)
            og.add_item(item)
        self._out_card.pack_start(og, True, True, 0)
        c.pack_start(self._out_card, True, True, 0)

        # GPIO Inputs
        self._in_card = card(tr("io.gpio_inputs"))
        self._in_card_title = _get_card_title(self._in_card)
        self.input_dots = {}
        ig = ResponsiveGrid(min_child_width=200, max_columns=4,
                            column_spacing=8, row_spacing=8)
        for iid in range(1, len(core.INPUT_NAMES) + 1):
            if iid == core.INPUT_GPIO_COUNT + 1:
                self._in_card.pack_start(ig, False, False, 0)
                self._in_card.pack_start(Gtk.Label(label="独立关门到位 PF15 / PG0", xalign=0), False, False, 8)
                ig = ResponsiveGrid(min_child_width=200, max_columns=4,
                                    column_spacing=8, row_spacing=8)
            name = core.map_name(core.INPUT_NAMES, iid - 1)
            item = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
            item.get_style_context().add_class("io-item")
            dot = Gtk.Label(label="\u25cb", xalign=0)
            dot.set_size_request(20, -1)
            dot.get_style_context().add_class("dot-off")
            self.input_dots[iid] = dot
            item.pack_start(dot, False, False, 0)
            nl = Gtk.Label(label=name, xalign=0)
            nl.set_ellipsize(Pango.EllipsizeMode.END)
            item.pack_start(nl, True, True, 0)
            ig.add_item(item)
        self._in_card.pack_start(ig, False, False, 0)
        c.pack_start(self._in_card, False, False, 0)

    def _set_rs485(self, *_):
        station_index = self.rs485_combo.get_active()
        text = self.rs485_entry.get_text().strip()

        try:
            station_text = core.RS485_STATIONS[station_index]
            station = int(station_text.split(" ", 1)[0])

            if station == 1:
                pressure = float(text)
                scaled_pressure = pressure * 10.0
                value = int(round(scaled_pressure))
                if (pressure < 0.0 or pressure > 10.0 or
                        abs(scaled_pressure - value) > 1e-6):
                    raise ValueError
            else:
                value = int(text)
                if station == 2:
                    if value < 0 or value > 90:
                        raise ValueError
                elif value < 0 or value > 100:
                    raise ValueError
        except (ValueError, IndexError):
            self._rs485_hint.set_text(tr("io.rs485_invalid"))
            return

        self._rs485_hint.set_text(tr("io.rs485_hint"))
        self._send("rs485", station=station_index, value=value)

    def _act_changed(self, cb, *_):
        self._send("actuatorMode", mode=2 if cb.get_active() else 1)

    def _out_changed(self, sw, _p, oid):
        if self._updating_out:
            return
        self._send("output", output_id=oid, state=sw.get_active())

    def update_state(self, state):
        io = state.get("io_bools", {})
        inputs = io.get("inputs", [])
        outputs = io.get("outputs", [])

        sig = (tuple(inputs), tuple(outputs))
        if sig == (tuple(self.out_states.get("_in", [])),
                   tuple(self.out_states.get("_out", []))):
            return
        self.out_states["_in"] = list(inputs)
        self.out_states["_out"] = list(outputs)

        if inputs:
            self.io_status.set_text(
                f"{len(inputs)} {tr('io.in')}  \u00b7  {len(outputs)} {tr('io.out')}")
        else:
            self.io_status.set_text(tr("io.no_status"))

        for iid in range(1, len(core.INPUT_NAMES) + 1):
            dot = self.input_dots.get(iid)
            if dot:
                on = (iid - 1) < len(inputs) and inputs[iid - 1]
                dot.set_text("\u25cf" if on else "\u25cb")
                ctx = dot.get_style_context()
                ctx.remove_class("dot-on")
                ctx.remove_class("dot-off")
                ctx.add_class("dot-on" if on else "dot-off")

        self._updating_out = True
        for oid in range(1, len(core.OUTPUT_NAMES) + 1):
            sw = self.switches.get(oid)
            if sw:
                on = (oid - 1) < len(outputs) and outputs[oid - 1]
                sw.set_active(on)
        self._updating_out = False

    def refresh_language(self):
        self._rs485_card_title.set_text(tr("io.rs485_manual"))
        self._out_card_title.set_text(tr("io.gpio_outputs"))
        self._in_card_title.set_text(tr("io.gpio_inputs"))
        self._rs485_hint.set_text(tr("io.rs485_hint"))
        self._out_hint.set_text(tr("io.toggle_hint"))
        self._btn_start_pump.set_label(tr("io.start_pump"))
        self._btn_stop_pump.set_label(tr("io.stop_pump"))
        self._btn_set_rs485.set_label(tr("io.set_rs485"))
        self.act_check.set_label(tr("io.gpio_actuator"))


def _get_card_title(card_box):
    for child in card_box.get_children():
        if isinstance(child, Gtk.Box):
            for sub in child.get_children():
                if isinstance(sub, Gtk.Label):
                    ctx = sub.get_style_context()
                    if "card-title" in ctx.list_classes():
                        return sub
    return None
