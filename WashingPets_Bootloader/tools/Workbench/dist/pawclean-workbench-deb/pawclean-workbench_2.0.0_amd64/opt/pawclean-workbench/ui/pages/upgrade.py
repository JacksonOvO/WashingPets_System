#!/usr/bin/env python3
"""Upgrade page — firmware IAP/OTA upgrade."""

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import GLib, Gtk

import server as core
from ..widgets import card
from ..i18n import tr


class UpgradePage(Gtk.Box):

    def __init__(self):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        self.get_style_context().add_class("page")
        self.set_hexpand(True)
        self.set_vexpand(True)
        self._log_count = 0
        self._log_lines = []
        self._log_mark = None
        self._browse_cb = None
        self._query_cb = None
        self._upgrade_cb = None

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

        # Upgrade controls
        self._fw_card = card(tr("fw.title"))
        self._fw_card_title = _get_card_title(self._fw_card)
        self._fw_card.get_style_context().add_class("upgrade_card")

        row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        row.get_style_context().add_class("upgrade-row")

        self._lbl_version = Gtk.Label(label=tr("fw.version"))
        row.pack_start(self._lbl_version, False, False, 0)
        self.version_entry = Gtk.Entry()
        self.version_entry.set_text("1.0.1")
        self.version_entry.set_width_chars(8)
        row.pack_start(self.version_entry, False, False, 0)

        self._lbl_firmware = Gtk.Label(label=tr("fw.firmware"))
        row.pack_start(self._lbl_firmware, False, False, 0)
        self.firmware_entry = Gtk.Entry()
        self.firmware_entry.set_text(str(core.DEFAULT_BIN))
        self.firmware_entry.set_hexpand(True)
        row.pack_start(self.firmware_entry, True, True, 0)

        self._btn_browse = Gtk.Button(label=tr("fw.browse"))
        self._btn_browse.connect("clicked", lambda _: self._browse_cb and self._browse_cb())
        row.pack_start(self._btn_browse, False, False, 0)

        self._btn_query = Gtk.Button(label=tr("fw.query_device"))
        self._btn_query.connect("clicked", lambda _: self._query_cb and self._query_cb())
        row.pack_start(self._btn_query, False, False, 0)

        self.upgrade_btn = Gtk.Button(label=tr("fw.start_upgrade"))
        self.upgrade_btn.get_style_context().add_class("start")
        self.upgrade_btn.connect(
            "clicked", lambda _: self._upgrade_cb and self._upgrade_cb())
        row.pack_start(self.upgrade_btn, False, False, 0)

        self._fw_card.pack_start(row, False, False, 0)

        self.status_label = Gtk.Label(label=tr("fw.ready"), xalign=0)
        self.status_label.get_style_context().add_class("status-line")
        self._fw_card.pack_start(self.status_label, False, False, 0)

        # Device info
        self._info_keys = [
            ("hardwareVersion", "fw.hardware"),
            ("bootVersion", "fw.bootloader"),
            ("appVersion", "fw.app_version"),
            ("targetId", "fw.target_id"),
            ("appStart", "fw.app_start"),
            ("appCapacity", "fw.capacity"),
            ("maxChunk", "fw.max_chunk"),
            ("runMode", "fw.run_mode"),
        ]
        self.info_labels = {}
        self._info_name_labels = {}
        ig = Gtk.Grid(column_spacing=16, row_spacing=8)
        ig.set_margin_top(12)
        for i, (k, label_key) in enumerate(self._info_keys):
            lbl = Gtk.Label(label=f"{tr(label_key)}:", xalign=0)
            lbl.get_style_context().add_class("hint")
            ig.attach(lbl, 0, i, 1, 1)
            self._info_name_labels[k] = lbl
            vl = Gtk.Label(label="--", xalign=0)
            vl.set_selectable(True)
            self.info_labels[k] = vl
            ig.attach(vl, 1, i, 1, 1)
        self._fw_card.pack_start(ig, False, False, 0)
        c.pack_start(self._fw_card, False, False, 0)

        # Log
        self._log_card = card(tr("fw.upgrade_log"))
        self._log_card_title = _get_card_title(self._log_card)
        ls = Gtk.ScrolledWindow()
        ls.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        ls.set_min_content_height(300)
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

    def set_callbacks(self, browse, query, upgrade):
        self._browse_cb = browse
        self._query_cb = query
        self._upgrade_cb = upgrade

    def set_device_info(self, info):
        for k, l in self.info_labels.items():
            l.set_text(str(info.get(k, "--")))

    def set_upgrade_running(self, running):
        self.upgrade_btn.set_sensitive(not running)
        if running:
            self.status_label.set_text(tr("fw.upgrading"))

    def set_status(self, text):
        self.status_label.set_text(text)

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

    def refresh_language(self):
        self._fw_card_title.set_text(tr("fw.title"))
        self._log_card_title.set_text(tr("fw.upgrade_log"))
        self._lbl_version.set_text(tr("fw.version"))
        self._lbl_firmware.set_text(tr("fw.firmware"))
        self._btn_browse.set_label(tr("fw.browse"))
        self._btn_query.set_label(tr("fw.query_device"))
        self.upgrade_btn.set_label(tr("fw.start_upgrade"))
        for k, label_key in self._info_keys:
            lbl = self._info_name_labels.get(k)
            if lbl:
                lbl.set_text(f"{tr(label_key)}:")


def _get_card_title(card_box):
    for child in card_box.get_children():
        if isinstance(child, Gtk.Box):
            for sub in child.get_children():
                if isinstance(sub, Gtk.Label):
                    ctx = sub.get_style_context()
                    if "card-title" in ctx.list_classes():
                        return sub
    return None
