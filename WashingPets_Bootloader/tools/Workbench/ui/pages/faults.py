#!/usr/bin/env python3
"""Fault center — active faults, history, severity levels."""

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Pango

import server as core
from ..widgets import card, badge
from ..i18n import tr, severity_name, severity_badge_style


class FaultsPage(Gtk.Box):

    def __init__(self, send_action):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        self.get_style_context().add_class("page")
        self.set_hexpand(True)
        self.set_vexpand(True)
        self._send = send_action
        self._alarm_count = -1

        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        self.pack_start(scroll, True, True, 0)

        c = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        c.set_margin_start(24)
        c.set_margin_end(24)
        c.set_margin_top(20)
        c.set_margin_bottom(20)
        scroll.add(c)

        # Active fault
        self.active_card = Gtk.Box(
            orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.active_card.get_style_context().add_class("fault-card")

        ar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        self.code_label = Gtk.Label(label="0x0000", xalign=0)
        self.code_label.get_style_context().add_class("fault-code")
        ar.pack_start(self.code_label, False, False, 0)
        self.name_label = Gtk.Label(label=tr("fault.no_fault"), xalign=0)
        self.name_label.get_style_context().add_class("fault-name")
        ar.pack_start(self.name_label, True, True, 0)
        self.sev_badge = badge(tr("fault.normal"), "success")
        ar.pack_start(self.sev_badge, False, False, 0)
        self.active_card.pack_start(ar, False, False, 0)

        self.meta_label = Gtk.Label(label="", xalign=0)
        self.meta_label.get_style_context().add_class("card-meta")
        self.active_card.pack_start(self.meta_label, False, False, 0)

        self._reset_btn = Gtk.Button(label=tr("fault.reset"))
        self._reset_btn.get_style_context().add_class("stop")
        self._reset_btn.connect("clicked", lambda _: self._send("process", value=7))
        self.active_card.pack_start(self._reset_btn, False, False, 0)
        c.pack_start(self.active_card, False, False, 0)

        # History
        self._hist_card = card(tr("fault.history"))
        self._hist_card_title = _get_card_title(self._hist_card)
        self._hist_summary = Gtk.Label(label="", xalign=0)
        self._hist_summary.get_style_context().add_class("card-meta")
        self._hist_summary.set_margin_start(4)
        self._hist_summary.set_margin_bottom(4)
        self._hist_card.pack_start(self._hist_summary, False, False, 0)
        self.hist_box = Gtk.Box(
            orientation=Gtk.Orientation.VERTICAL, spacing=6)
        self.hist_empty = Gtk.Label(label=tr("fault.no_history"), xalign=0)
        self.hist_empty.get_style_context().add_class("hint")
        self.hist_box.pack_start(self.hist_empty, False, False, 0)
        hs = Gtk.ScrolledWindow()
        hs.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        hs.set_min_content_height(350)
        hs.add(self.hist_box)
        self._hist_card.pack_start(hs, True, True, 0)
        c.pack_start(self._hist_card, True, True, 0)

    def update_state(self, state):
        f = state.get("fault", {})
        ac = f.get("active_code", 0)
        sev = f.get("severity", 0)
        eid = f.get("event_id", 0)

        if ac:
            self.code_label.set_text(f"0x{ac:04X}")
            self.code_label.get_style_context().add_class("fault-active")
            self.name_label.set_text(core.FAULT_CODES.get(ac, tr("fault.unknown")))
            sn = core.FAULT_SEVERITIES.get(sev, "?")
            bs = severity_badge_style(sev)
            bt = severity_name(sev)
            _update_badge(self.sev_badge, bt, bs)
            self.meta_label.set_text(
                f"{tr('fault.severity')}: {sn}  |  {tr('fault.event_id')}: {eid}")
            ctx = self.active_card.get_style_context()
            ctx.remove_class("fault-card-resolved")
            ctx.remove_class("fault-card-info")
            ctx.remove_class("fault-card-warning")
            if sev >= 3:
                ctx.add_class("fault-card-active")
            elif sev == 2:
                ctx.add_class("fault-card-warning")
            else:
                ctx.add_class("fault-card-info")
        else:
            self.code_label.set_text("0x0000")
            self.code_label.get_style_context().remove_class("fault-active")
            self.name_label.set_text(tr("fault.no_fault"))
            _update_badge(self.sev_badge, tr("fault.normal"), "success")
            self.meta_label.set_text("")
            ctx = self.active_card.get_style_context()
            ctx.remove_class("fault-card-active")
            ctx.remove_class("fault-card-warning")
            ctx.remove_class("fault-card-info")
            ctx.add_class("fault-card-resolved")

        alarms = state.get("faultAlarms", [])
        if len(alarms) != self._alarm_count:
            self._alarm_count = len(alarms)
            active_n = sum(1 for a in alarms if a.get("status") == "active")
            resolved_n = sum(1 for a in alarms if a.get("status") == "resolved")
            self._hist_summary.set_text(
                f"{tr('fault.total_events', 'Total')}: {len(alarms)}  |  "
                f"{tr('fault.active_label', 'Active')}: {active_n}  |  "
                f"{tr('fault.resolved_label', 'Resolved')}: {resolved_n}")
            for ch in list(self.hist_box.get_children()):
                if ch != self.hist_empty:
                    self.hist_box.remove(ch)
            if alarms:
                self.hist_empty.hide()
                # Filter out 0x0000 "no fault" records — they are not real events
                real_alarms = [a for a in reversed(alarms[:50]) if a.get("fault_code", 0) != 0]
                for a in real_alarms:
                    row = self._alarm_row(a)
                    self.hist_box.pack_start(row, False, False, 0)
                self.hist_box.show_all()
            else:
                self.hist_empty.show()

    def _alarm_row(self, a):
        row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        row.get_style_context().add_class("io-item")
        row.set_margin_start(4)
        row.set_margin_end(4)

        fc = a.get("fault_code", 0)
        sev = a.get("severity", 1)
        et = a.get("event_type", 1)

        bs = severity_badge_style(sev)
        b = badge(core.FAULT_SEVERITIES.get(sev, "?"), bs)
        row.pack_start(b, False, False, 0)

        cl = Gtk.Label(label=f"0x{fc:04X}", xalign=0)
        cl.set_size_request(70, -1)
        cl.get_style_context().add_class("fault-time")
        row.pack_start(cl, False, False, 0)

        nl = Gtk.Label(label=core.FAULT_CODES.get(fc, "?"), xalign=0)
        nl.set_hexpand(True)
        nl.set_ellipsize(Pango.EllipsizeMode.END)
        row.pack_start(nl, True, True, 0)

        tl = Gtk.Label(label=core.FAULT_EVENT_TYPES.get(et, "?"), xalign=0)
        tl.get_style_context().add_class("hint")
        row.pack_start(tl, False, False, 0)

        il = Gtk.Label(label=f"#{a.get('event_id', 0)}", xalign=1)
        il.get_style_context().add_class("fault-time")
        il.set_size_request(60, -1)
        row.pack_start(il, False, False, 0)
        return row

    def refresh_language(self):
        self._reset_btn.set_label(tr("fault.reset"))
        self._hist_card_title.set_text(tr("fault.history"))
        self.hist_empty.set_text(tr("fault.no_history"))


def _update_badge(w, text, style):
    w.set_text(text)
    ctx = w.get_style_context()
    for s in ["success", "warning", "error", "info", "neutral"]:
        ctx.remove_class(f"badge-{s}")
    ctx.add_class(f"badge-{style}")


def _get_card_title(card_box):
    for child in card_box.get_children():
        if isinstance(child, Gtk.Box):
            for sub in child.get_children():
                if isinstance(sub, Gtk.Label):
                    ctx = sub.get_style_context()
                    if "card-title" in ctx.list_classes():
                        return sub
    return None
