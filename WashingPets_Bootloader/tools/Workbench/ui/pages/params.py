#!/usr/bin/env python3
"""Parameters page — recipe parameter read/write/save/restore."""

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Pango

import server as core
from ..widgets import ResponsiveGrid, card, combo
from ..i18n import tr, recipe_name


class ParamsPage(Gtk.Box):

    def __init__(self, send_action):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        self.get_style_context().add_class("page")
        self.set_hexpand(True)
        self.set_vexpand(True)
        self._send = send_action
        self.entries = {}
        self.dirty = set()
        self._updating = False

        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        self.pack_start(scroll, True, True, 0)

        c = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
        c.set_margin_start(24)
        c.set_margin_end(24)
        c.set_margin_top(20)
        c.set_margin_bottom(20)
        scroll.add(c)

        # Toolbar
        self._config_card = card(tr("param.recipe_config"))
        self._config_card_title = _get_card_title(self._config_card)
        tb = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        self.recipe_combo = combo([recipe_name(i) for i in range(26)])
        tb.pack_start(self.recipe_combo, False, False, 0)

        self._btn_read = Gtk.Button(label=tr("param.read"))
        self._btn_read.connect("clicked", lambda _: self._send("readParam"))
        tb.pack_start(self._btn_read, False, False, 0)

        self._btn_write = Gtk.Button(label=tr("param.write_ram"))
        self._btn_write.get_style_context().add_class("primary")
        self._btn_write.connect("clicked", lambda _: self._send("writeParam"))
        tb.pack_start(self._btn_write, False, False, 0)

        self._btn_save = Gtk.Button(label=tr("param.save_flash"))
        self._btn_save.get_style_context().add_class("start")
        self._btn_save.connect("clicked", lambda _: self._send("saveParam"))
        tb.pack_start(self._btn_save, False, False, 0)

        self._btn_restore = Gtk.Button(label=tr("param.restore_default"))
        self._btn_restore.get_style_context().add_class("stop")
        self._btn_restore.connect("clicked", lambda _: self._send("restoreParam"))
        tb.pack_start(self._btn_restore, False, False, 0)

        self._config_card.pack_start(tb, False, False, 0)
        self.rev_label = Gtk.Label(
            label=f"{tr('param.version')} --", xalign=0)
        self.rev_label.get_style_context().add_class("hint")
        self._config_card.pack_start(self.rev_label, False, False, 0)
        c.pack_start(self._config_card, False, False, 0)

        # Grid
        self._param_card = card(tr("param.parameters"))
        self._param_card_title = _get_card_title(self._param_card)
        self.grid = ResponsiveGrid(min_child_width=220, max_columns=4,
                                   column_spacing=10, row_spacing=10)
        for field in core.RECIPE_FIELDS:
            item = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
            item.get_style_context().add_class("param-item")
            item.set_hexpand(True)

            lbl = Gtk.Label(label=field["label"], xalign=0)
            lbl.set_ellipsize(Pango.EllipsizeMode.END)
            item.pack_start(lbl, False, False, 0)

            entry = Gtk.Entry()
            entry.set_text("0")
            entry.set_hexpand(True)
            entry.connect("changed", self._on_changed, field["key"])
            item.pack_start(entry, False, False, 0)

            self.entries[field["key"]] = entry
            self.grid.add_item(item)

        self._param_card.pack_start(self.grid, True, True, 0)
        c.pack_start(self._param_card, True, True, 0)

    def _on_changed(self, entry, key):
        if self._updating:
            return
        self.dirty.add(key)
        entry.get_style_context().add_class("param-dirty")

    def update_state(self, state):
        params = state.get("parameters", {})
        rev = state.get("paramRevision", 0)
        self.rev_label.set_text(f"{tr('param.version')} {rev}")
        if not params:
            return
        self._updating = True
        for f in core.RECIPE_FIELDS:
            k = f["key"]
            e = self.entries[k]
            if k not in self.dirty:
                e.set_text(str(params.get(k, 0)))
                e.get_style_context().remove_class("param-dirty")
        self._updating = False

    def get_recipe_index(self):
        return self.recipe_combo.get_active()

    def get_revision(self):
        try:
            return int(self.rev_label.get_text().split()[-1])
        except (ValueError, IndexError):
            return 0

    def get_dirty_params(self):
        result = {}
        for k in self.dirty:
            e = self.entries.get(k)
            if e:
                try:
                    result[k] = float(e.get_text())
                except ValueError:
                    pass
        return result

    def mark_clean(self):
        self.dirty.clear()
        for e in self.entries.values():
            e.get_style_context().remove_class("param-dirty")

    def refresh_language(self):
        self._config_card_title.set_text(tr("param.recipe_config"))
        self._param_card_title.set_text(tr("param.parameters"))
        self._btn_read.set_label(tr("param.read"))
        self._btn_write.set_label(tr("param.write_ram"))
        self._btn_save.set_label(tr("param.save_flash"))
        self._btn_restore.set_label(tr("param.restore_default"))


def _get_card_title(card_box):
    for child in card_box.get_children():
        if isinstance(child, Gtk.Box):
            for sub in child.get_children():
                if isinstance(sub, Gtk.Label):
                    ctx = sub.get_style_context()
                    if "card-title" in ctx.list_classes():
                        return sub
    return None
