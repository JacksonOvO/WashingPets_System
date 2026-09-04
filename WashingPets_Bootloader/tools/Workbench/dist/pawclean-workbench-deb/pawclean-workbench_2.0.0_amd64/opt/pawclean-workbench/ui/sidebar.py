#!/usr/bin/env python3
"""Sidebar navigation — grouped nav with section headers."""

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import GObject, Gtk

from .i18n import tr


class Sidebar(Gtk.Box):
    __gtype_name__ = "PawSidebar"
    __gsignals__ = {
        "page-changed": (GObject.SignalFlags.RUN_LAST, None, (str,)),
    }

    def __init__(self):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.get_style_context().add_class("sidebar")
        self.set_size_request(210, -1)
        self._buttons = {}
        self._active_page = None
        self._section_labels = []
        self._brand_title = None
        self._brand_sub = None
        self._nav_labels = {}

        self._build_brand()
        self._build_section("nav.section.monitor", [
            ("dashboard", "nav.dashboard"),
            ("telemetry", "nav.telemetry"),
            ("faults", "nav.faults"),
        ])
        self._build_section("nav.section.control", [
            ("control", "nav.control"),
            ("io", "nav.io"),
            ("params", "nav.params"),
            ("test", "nav.test"),
        ])
        self._build_section("nav.section.engineer", [
            ("upgrade", "nav.upgrade"),
        ])

    def _build_brand(self):
        brand = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        brand.get_style_context().add_class("brand")
        brand.set_margin_top(8)
        brand.set_margin_bottom(16)
        brand.set_margin_start(8)

        self._brand_title = Gtk.Label(label=tr("brand.title"), xalign=0)
        self._brand_title.get_style_context().add_class("brand-title")
        brand.pack_start(self._brand_title, False, False, 0)

        self._brand_sub = Gtk.Label(label=tr("brand.subtitle"), xalign=0)
        self._brand_sub.get_style_context().add_class("brand-subtitle")
        brand.pack_start(self._brand_sub, False, False, 0)

        self.pack_start(brand, False, False, 0)

    def _build_section(self, title_key, items):
        sec = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        sec.set_margin_top(12)
        sec.set_margin_start(4)
        sec.set_margin_end(4)

        lbl = Gtk.Label(label=tr(title_key), xalign=0)
        lbl.get_style_context().add_class("nav-section-title")
        lbl.set_margin_start(8)
        lbl.set_margin_bottom(4)
        sec.pack_start(lbl, False, False, 0)
        self._section_labels.append(lbl)

        for page_name, label_key in items:
            btn = Gtk.Button()
            btn.set_relief(Gtk.ReliefStyle.NONE)
            btn.get_style_context().add_class("nav")

            row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
            dot = Gtk.Label(label="\u25cb")
            dot.set_size_request(16, -1)
            dot.get_style_context().add_class("nav-dot")
            row.pack_start(dot, False, False, 0)
            name_lbl = Gtk.Label(label=tr(label_key), xalign=0)
            row.pack_start(name_lbl, True, True, 0)

            btn.add(row)
            btn.connect("clicked", self._on_nav_clicked, page_name)
            sec.pack_start(btn, False, False, 0)
            self._buttons[page_name] = (btn, dot)
            self._nav_labels[page_name] = name_lbl

        self.pack_start(sec, False, False, 0)

    def _on_nav_clicked(self, _button, page_name):
        if self._active_page == page_name:
            return
        self.set_active(page_name)
        self.emit("page-changed", page_name)

    def set_active(self, page_name):
        if self._active_page and self._active_page in self._buttons:
            btn, dot = self._buttons[self._active_page]
            btn.get_style_context().remove_class("nav-active")
            dot.set_text("\u25cb")
        if page_name in self._buttons:
            btn, dot = self._buttons[page_name]
            btn.get_style_context().add_class("nav-active")
            dot.set_text("\u25cf")
        self._active_page = page_name

    def refresh_language(self):
        self._brand_title.set_text(tr("brand.title"))
        self._brand_sub.set_text(tr("brand.subtitle"))
        for lbl in self._section_labels:
            idx = self._section_labels.index(lbl)
            keys = ["nav.section.monitor", "nav.section.control", "nav.section.engineer"]
            if idx < len(keys):
                lbl.set_text(tr(keys[idx]))
        for page_name, lbl in self._nav_labels.items():
            key_map = {
                "dashboard": "nav.dashboard",
                "telemetry": "nav.telemetry",
                "faults": "nav.faults",
                "control": "nav.control",
                "io": "nav.io",
                "params": "nav.params",
                "test": "nav.test",
                "upgrade": "nav.upgrade",
            }
            lbl.set_text(tr(key_map.get(page_name, page_name)))
