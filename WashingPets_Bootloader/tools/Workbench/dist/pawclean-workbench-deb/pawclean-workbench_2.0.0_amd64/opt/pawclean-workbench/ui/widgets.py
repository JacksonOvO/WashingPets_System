#!/usr/bin/env python3
"""Reusable UI components for PawClean Workbench."""

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk, GdkPixbuf, GLib, Gtk, Pango


# ─── Responsive Grid ─────────────────────────────────────────────────────────

class ResponsiveGrid(Gtk.Grid):
    """Auto-reflowing grid that adjusts columns based on available width."""

    def __init__(self, min_child_width=240, max_columns=4, column_spacing=10, row_spacing=10):
        super().__init__(column_spacing=column_spacing, row_spacing=row_spacing)
        self.min_child_width = min_child_width
        self.max_columns = max_columns
        self.items = []
        self.expand_items = set()
        self.current_columns = max_columns
        self.set_hexpand(True)
        self.set_vexpand(False)
        self.set_valign(Gtk.Align.START)
        self.set_column_homogeneous(False)
        self.connect("size-allocate", self._on_size_allocate)

    def add_item(self, widget, expand=True):
        widget.set_hexpand(True)
        widget.set_vexpand(expand)
        self.items.append(widget)
        if expand:
            self.expand_items.add(widget)
        index = len(self.items) - 1
        self.attach(widget, index % self.current_columns, index // self.current_columns, 1, 1)

    def _on_size_allocate(self, _widget, allocation):
        self.reflow_for_width(allocation.width)

    def reflow_for_width(self, width):
        if width <= 1:
            return
        columns = max(1, min(self.max_columns, width // self.min_child_width))
        self.reflow(int(columns))

    def reflow(self, columns):
        if not self.items:
            return
        columns = max(1, min(self.max_columns, columns))
        if columns == self.current_columns and all(item.get_parent() is self for item in self.items):
            return
        for child in list(self.get_children()):
            self.remove(child)
        for index, item in enumerate(self.items):
            self.attach(item, index % columns, index // columns, 1, 1)
            item.set_hexpand(True)
            item.set_vexpand(item in self.expand_items)
        self.current_columns = columns


# ─── Card ────────────────────────────────────────────────────────────────────

def card(title, expand=True, trailing=None):
    """Create a styled card container with title."""
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
    box.get_style_context().add_class("card")
    box.set_hexpand(True)
    box.set_vexpand(expand)

    header = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
    header.get_style_context().add_class("card-header")

    label = Gtk.Label(label=title, xalign=0)
    label.get_style_context().add_class("card-title")
    header.pack_start(label, True, True, 0)

    if trailing is not None:
        header.pack_start(trailing, False, False, 0)

    box.pack_start(header, False, False, 0)
    return box


# ─── Group Box ───────────────────────────────────────────────────────────────

def group_box(title):
    """Create a grouped section with title."""
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
    box.get_style_context().add_class("group")
    box.set_hexpand(True)
    box.set_vexpand(False)
    box.set_valign(Gtk.Align.START)

    label = Gtk.Label(label=title, xalign=0)
    label.get_style_context().add_class("group-title")
    box.pack_start(label, False, False, 0)
    return box


# ─── Metric Box ──────────────────────────────────────────────────────────────

def metric_box(name, value_text="--"):
    """Create a metric display with label and value.
    Returns (name_label, value_label, container_box)."""
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
    box.get_style_context().add_class("metric")
    box.set_hexpand(True)

    label = Gtk.Label(label=name, xalign=0)
    label.set_ellipsize(Pango.EllipsizeMode.END)
    label.get_style_context().add_class("metric-name")

    value = Gtk.Label(label=value_text, xalign=0)
    value.get_style_context().add_class("metric-value")

    box.pack_start(label, False, False, 0)
    box.pack_start(value, False, False, 0)
    return label, value, box


# ─── Sensor Tile ─────────────────────────────────────────────────────────────

def sensor_tile(name, icon="", value_text="--", unit=""):
    """Create a sensor display tile with icon, name, value and unit."""
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
    box.get_style_context().add_class("sensor-tile")
    box.set_hexpand(True)

    if icon:
        icon_label = Gtk.Label(label=icon, xalign=0)
        icon_label.get_style_context().add_class("sensor-tile-icon")
        box.pack_start(icon_label, False, False, 0)

    name_label = Gtk.Label(label=name, xalign=0)
    name_label.get_style_context().add_class("sensor-tile-name")
    name_label.set_ellipsize(Pango.EllipsizeMode.END)

    value_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
    value = Gtk.Label(label=value_text, xalign=0)
    value.get_style_context().add_class("sensor-tile-value")
    value_box.pack_start(value, False, False, 0)

    if unit:
        unit_label = Gtk.Label(label=unit, xalign=0)
        unit_label.get_style_context().add_class("metric-unit")
        value_box.pack_start(unit_label, False, False, 0)

    box.pack_start(name_label, False, False, 0)
    box.pack_start(value_box, False, False, 0)
    return box, value


# ─── Actuator Item ───────────────────────────────────────────────────────────

def actuator_item(name, value_text="--"):
    """Create an actuator display with progress bar."""
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
    box.get_style_context().add_class("actuator-item")
    box.set_hexpand(True)

    name_label = Gtk.Label(label=name, xalign=0)
    name_label.get_style_context().add_class("actuator-name")
    name_label.set_ellipsize(Pango.EllipsizeMode.END)

    value = Gtk.Label(label=value_text, xalign=0)
    value.get_style_context().add_class("actuator-value")

    progress = Gtk.ProgressBar()
    progress.get_style_context().add_class("progress-bar")
    progress.set_fraction(0)

    box.pack_start(name_label, False, False, 0)
    box.pack_start(value, False, False, 0)
    box.pack_start(progress, False, False, 0)
    return box, value, progress


# ─── Button ──────────────────────────────────────────────────────────────────

def button(label, callback, classes=""):
    """Create a styled button."""
    btn = Gtk.Button(label=label)
    btn.set_hexpand(False)
    for cls in classes.split():
        btn.get_style_context().add_class(cls)
    btn.connect("clicked", callback)
    return btn


# ─── Combo Box ───────────────────────────────────────────────────────────────

def combo(items):
    """Create a combo box with items."""
    box = Gtk.ComboBoxText()
    box.set_hexpand(False)
    for item in items:
        box.append_text(item)
    if items:
        box.set_active(0)
    return box


# ─── Labeled Widget ──────────────────────────────────────────────────────────

def labeled(text, widget):
    """Create a vertically labeled widget."""
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
    label = Gtk.Label(label=text, xalign=0)
    label.get_style_context().add_class("hint")
    box.pack_start(label, False, False, 0)
    box.pack_start(widget, False, False, 0)
    return box


def labeled_inline(text, widget):
    """Create a horizontally labeled widget."""
    box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
    box.set_hexpand(True)
    label = Gtk.Label(label=text, xalign=0)
    label.get_style_context().add_class("hint")
    box.pack_start(label, False, False, 0)
    widget.set_hexpand(True)
    box.pack_start(widget, True, True, 0)
    return box


# ─── Output Toggle ───────────────────────────────────────────────────────────

class OutputToggle(Gtk.ToggleButton):
    """iOS-style toggle for IO outputs."""

    def __init__(self):
        super().__init__(label="OFF")
        self.set_size_request(56, 30)
        self.set_hexpand(False)
        self.set_vexpand(False)
        self.set_valign(Gtk.Align.CENTER)
        self.get_style_context().add_class("io-toggle")
        self.connect("toggled", self._sync_label)

    def _sync_label(self, *_):
        from .i18n import tr
        self.set_label(tr("io.on") if self.get_active() else tr("io.off"))

    def refresh_language(self):
        self._sync_label()


# ─── Page Box ────────────────────────────────────────────────────────────────

def page_box(title="", subtitle=""):
    """Create a page container with optional title."""
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
    box.get_style_context().add_class("page")
    box.set_hexpand(True)
    box.set_vexpand(True)

    if title:
        title_label = Gtk.Label(label=title, xalign=0)
        title_label.get_style_context().add_class("page-title")
        box.pack_start(title_label, False, False, 0)

    if subtitle:
        subtitle_label = Gtk.Label(label=subtitle, xalign=0)
        subtitle_label.get_style_context().add_class("page-subtitle")
        box.pack_start(subtitle_label, False, False, 0)

    return box


# ─── Badge ───────────────────────────────────────────────────────────────────

def badge(text, style="neutral"):
    """Create a status badge."""
    label = Gtk.Label(label=text)
    label.get_style_context().add_class("badge")
    label.get_style_context().add_class(f"badge-{style}")
    return label


# ─── Status Dot ──────────────────────────────────────────────────────────────

def status_dot(active=False):
    """Create a status indicator dot."""
    dot = Gtk.Label(label="●" if active else "○")
    dot.get_style_context().add_class("dot-on" if active else "dot-off")
    return dot


# ─── Utility Functions ───────────────────────────────────────────────────────

def set_text_if_changed(widget, text):
    """Set text only if it changed to avoid unnecessary redraws."""
    current = widget.get_label() if isinstance(widget, Gtk.Button) else widget.get_text()
    if current == text:
        return
    if isinstance(widget, Gtk.Button):
        widget.set_label(text)
    else:
        widget.set_text(text)


def fmt(value):
    """Format a value, showing -- for None."""
    return "--" if value is None else str(value)
