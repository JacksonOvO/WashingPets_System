#!/usr/bin/env python3
"""Header bar — connection controls, status, and language toggle."""

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import GObject, Gtk

from .i18n import tr, get_lang, set_lang


class Header(Gtk.Box):
    __gtype_name__ = "PawHeader"
    __gsignals__ = {
        "language-changed": (GObject.SignalFlags.RUN_LAST, None, ()),
    }

    def __init__(self):
        super().__init__(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        self.get_style_context().add_class("header")
        self._connected = False

        self.connection_label = Gtk.Label(label=tr("header.disconnected"))
        self.connection_label.get_style_context().add_class("connection")
        self.pack_start(self.connection_label, False, False, 0)

        sep = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        self.pack_start(sep, False, False, 0)

        self.port_combo = Gtk.ComboBoxText()
        self.port_combo.set_size_request(180, -1)
        self.pack_start(self.port_combo, False, False, 0)

        self.refresh_btn = Gtk.Button(label=tr("header.refresh"))
        self.refresh_btn.connect("clicked", lambda _: self._refresh_ports())
        self.pack_start(self.refresh_btn, False, False, 0)

        self.connect_button = Gtk.Button(label=tr("header.connect"))
        self.connect_button.get_style_context().add_class("primary")
        self.pack_start(self.connect_button, False, False, 0)

        spacer = Gtk.Box()
        spacer.set_hexpand(True)
        self.pack_start(spacer, True, True, 0)

        self.poll_check = Gtk.CheckButton(label=tr("header.poll"))
        self.pack_start(self.poll_check, False, False, 0)

        self.lang_button = Gtk.Button(label=tr("header.lang"))
        self.lang_button.set_size_request(60, -1)
        self.lang_button.connect("clicked", self._toggle_lang)
        self.pack_start(self.lang_button, False, False, 0)

        self._refresh_ports()

    def _toggle_lang(self, *_):
        new_lang = "zh" if get_lang() == "en" else "en"
        set_lang(new_lang)
        self.refresh_language()
        self.emit("language-changed")

    def _refresh_ports(self):
        import server as core
        active = self.port_combo.get_active_text()
        self.port_combo.remove_all()
        for port in core.list_ports():
            self.port_combo.append_text(port)
        if active:
            model = self.port_combo.get_model()
            for i, row in enumerate(model):
                if row[0] == active:
                    self.port_combo.set_active(i)
                    return
        self.port_combo.set_active(0)

    def set_connected(self, connected, port=""):
        self._connected = connected
        if connected:
            self.connection_label.set_text(
                f"{tr('header.connected')}  {port}")
            self.connection_label.get_style_context().add_class("connected")
            self.connect_button.set_label(tr("header.disconnect"))
            self.connect_button.get_style_context().remove_class("primary")
            self.connect_button.get_style_context().add_class("stop")
        else:
            self.connection_label.set_text(tr("header.disconnected"))
            self.connection_label.get_style_context().remove_class("connected")
            self.connect_button.set_label(tr("header.connect"))
            self.connect_button.get_style_context().remove_class("stop")
            self.connect_button.get_style_context().add_class("primary")

    def refresh_language(self):
        self.refresh_btn.set_label(tr("header.refresh"))
        self.poll_check.set_label(tr("header.poll"))
        self.lang_button.set_label(tr("header.lang"))
        self.set_connected(self._connected,
                           self.port_combo.get_active_text() or "")
