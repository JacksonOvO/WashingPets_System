#!/usr/bin/env python3
"""Debug page — TX/RX logs, self-test controls."""

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import GLib, Gtk

import server as core
from ..widgets import card, ResponsiveGrid
from ..i18n import tr


class DebugPage(Gtk.Box):

    def __init__(self, send_action):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        self.get_style_context().add_class("page")
        self.set_hexpand(True)
        self.set_vexpand(True)
        self._send = send_action
        self._log_count = 0
        self._log_lines = []
        self._log_mark = None
        self._last_st = None

        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroll.set_overlay_scrolling(True)
        self.pack_start(scroll, True, True, 0)

        c = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        c.set_margin_start(24)
        c.set_margin_end(24)
        c.set_margin_top(20)
        c.set_margin_bottom(20)
        scroll.add(c)

        # Log
        self._log_card = card(tr("debug.comm_log"))
        self._log_card_title = _get_card_title(self._log_card)
        cb = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        self._btn_clear = Gtk.Button(label=tr("debug.clear"))
        self._btn_clear.connect("clicked", self._clear)
        cb.pack_start(self._btn_clear, False, False, 0)
        self.count_label = Gtk.Label(
            label=tr("debug.entries").format(n=0), xalign=0)
        self.count_label.get_style_context().add_class("hint")
        cb.pack_start(self.count_label, False, False, 0)
        self._log_card.pack_start(cb, False, False, 0)

        ls = Gtk.ScrolledWindow()
        ls.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        ls.set_min_content_height(400)
        self.log_view = Gtk.TextView()
        self.log_view.set_editable(False)
        self.log_view.set_cursor_visible(False)
        self.log_view.set_monospace(True)
        self.log_view.set_wrap_mode(Gtk.WrapMode.NONE)
        self.log_view.get_style_context().add_class("log")
        self.log_buf = self.log_view.get_buffer()
        ls.add(self.log_view)
        self._log_card.pack_start(ls, True, True, 0)
        c.pack_start(self._log_card, True, True, 0)

        # Self-test
        self._test_card = card(tr("debug.self_test"))
        self._test_card_title = _get_card_title(self._test_card)
        self._test_hint = Gtk.Label(
            label=tr("debug.self_test_hint"), xalign=0)
        self._test_hint.get_style_context().add_class("hint")
        self._test_card.pack_start(self._test_hint, False, False, 0)

        tg = ResponsiveGrid(min_child_width=160, max_columns=4,
                            column_spacing=10, row_spacing=10)
        self._btn_quick = Gtk.Button(label=tr("debug.quick_test"))
        self._btn_quick.get_style_context().add_class("start")
        self._btn_quick.connect("clicked", lambda _: self._send("selfTest", mode=0))
        tg.add_item(self._btn_quick)
        self._btn_deep = Gtk.Button(label=tr("debug.deep_test"))
        self._btn_deep.get_style_context().add_class("start")
        self._btn_deep.connect("clicked", lambda _: self._send("selfTest", mode=1))
        tg.add_item(self._btn_deep)
        self._btn_safe_stop = Gtk.Button(label=tr("debug.safe_stop"))
        self._btn_safe_stop.get_style_context().add_class("stop")
        self._btn_safe_stop.connect("clicked", lambda _: self._send("process", value=4))
        tg.add_item(self._btn_safe_stop)
        self._test_card.pack_start(tg, False, False, 0)

        self.st_summary = Gtk.Label(
            label=tr("debug.no_result"), xalign=0)
        self.st_summary.get_style_context().add_class("hint")
        self._test_card.pack_start(self.st_summary, False, False, 0)

        self.st_flow = Gtk.FlowBox()
        self.st_flow.set_selection_mode(Gtk.SelectionMode.NONE)
        self.st_flow.set_max_children_per_line(6)
        self.st_flow.set_homogeneous(True)
        self._test_card.pack_start(self.st_flow, False, False, 0)
        c.pack_start(self._test_card, False, False, 0)

    def _clear(self, *_):
        self.log_buf.set_text("")
        self._log_count = 0
        self._log_lines = []
        self._log_mark = None

    def _is_at_bottom(self):
        """Check if user is scrolled near the bottom of the log view."""
        adj = self.log_view.get_vadjustment()
        if not adj:
            return True
        return adj.get_value() >= adj.get_upper() - adj.get_page_size() - 30

    def _render_logs(self, logs):
        logs = list(logs)
        if logs == self._log_lines:
            return
        old_logs = self._log_lines
        reset_buffer = False
        if len(logs) >= len(old_logs) and old_logs and logs[:len(old_logs)] == old_logs:
            new_logs = logs[len(old_logs):]
        else:
            overlap = 0
            for count in range(min(len(old_logs), len(logs)), 0, -1):
                if old_logs[-count:] == logs[:count]:
                    overlap = count
                    break
            if overlap:
                new_logs = logs[overlap:]
            else:
                reset_buffer = True
                new_logs = logs
        if reset_buffer:
            self.log_buf.set_text("")
            self._log_mark = None
            self._log_lines = []
        end_iter = self.log_buf.get_end_iter()
        for line in new_logs:
            if self.log_buf.get_char_count() > 0:
                self.log_buf.insert(end_iter, "\n")
            self.log_buf.insert(end_iter, line)
        self._log_lines = self._log_lines + new_logs
        self._log_count = len(self._log_lines)
        if new_logs:
            GLib.idle_add(self._scroll_to_end)

    def _scroll_to_end(self):
        end_iter = self.log_buf.get_end_iter()
        if self._log_mark is None:
            self._log_mark = self.log_buf.create_mark("log-end", end_iter, False)
        else:
            self.log_buf.move_mark(self._log_mark, end_iter)
        self.log_view.scroll_to_mark(self._log_mark, 0.05, True, 0.0, 1.0)
        return False

    def update_state(self, state):
        self._render_logs(state.get("logs", []))
        self.count_label.set_text(
            tr("debug.entries").format(n=len(self._log_lines)))

        st = state.get("self_test")
        if st != self._last_st:
            self._last_st = st
            self._render_st(st)

    def _render_st(self, result):
        for ch in self.st_flow.get_children():
            self.st_flow.remove(ch)
        if not result:
            self.st_summary.set_text(tr("debug.no_result"))
            return
        mode = result.get("mode", 0)
        state = result.get("state", 0)
        mt = tr("debug.deep") if mode else tr("debug.quick")
        st2 = tr("debug.aborted") if state else tr("debug.done")
        self.st_summary.set_text(f"{mt}{tr('debug.test_colon')}{st2}")

        tested = result.get("tested", 0)
        passed = result.get("passed", 0)
        items = core.SELF_TEST_ITEMS
        for i, name in enumerate(items):
            bit = 1 << i
            if not (tested & bit):
                continue
            ok = bool(passed & bit)
            lbl = Gtk.Label(label=name)
            lbl.get_style_context().add_class(
                "selftest-pass" if ok else "selftest-fail")
            self.st_flow.add(lbl)
        self.st_flow.show_all()

    def refresh_language(self):
        self._log_card_title.set_text(tr("debug.comm_log"))
        self._test_card_title.set_text(tr("debug.self_test"))
        self._test_hint.set_text(tr("debug.self_test_hint"))
        self._btn_clear.set_label(tr("debug.clear"))
        self._btn_quick.set_label(tr("debug.quick_test"))
        self._btn_deep.set_label(tr("debug.deep_test"))
        self._btn_safe_stop.set_label(tr("debug.safe_stop"))
        self.count_label.set_text(
            tr("debug.entries").format(n=self._log_count))


def _get_card_title(card_box):
    for child in card_box.get_children():
        if isinstance(child, Gtk.Box):
            for sub in child.get_children():
                if isinstance(sub, Gtk.Label):
                    ctx = sub.get_style_context()
                    if "card-title" in ctx.list_classes():
                        return sub
    return None
