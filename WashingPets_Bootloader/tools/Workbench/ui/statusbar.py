#!/usr/bin/env python3
"""Bottom status bar — mode, process, faults, IO summary."""

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk

import server as core
from .i18n import tr, mode_name, state_name, process_name


class StatusBar(Gtk.Box):
    __gtype_name__ = "PawStatusBar"

    def __init__(self):
        super().__init__(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        self.get_style_context().add_class("statusbar")
        self._last_state = None

        self.mode_label = Gtk.Label(label="--")
        self.mode_label.get_style_context().add_class("statusbar-item")
        self.pack_start(self.mode_label, False, False, 0)

        sep1 = Gtk.Label(label="|")
        sep1.get_style_context().add_class("statusbar-sep")
        self.pack_start(sep1, False, False, 0)

        self.recipe_label = Gtk.Label(label="--")
        self.recipe_label.get_style_context().add_class("statusbar-item")
        self.pack_start(self.recipe_label, False, False, 0)

        sep2 = Gtk.Label(label="|")
        sep2.get_style_context().add_class("statusbar-sep")
        self.pack_start(sep2, False, False, 0)

        self.process_label = Gtk.Label(label="--")
        self.process_label.set_ellipsize(3)
        self.pack_start(self.process_label, False, False, 0)

        sep3 = Gtk.Label(label="|")
        sep3.get_style_context().add_class("statusbar-sep")
        self.pack_start(sep3, False, False, 0)

        self.time_label = Gtk.Label(label="--")
        self.time_label.get_style_context().add_class("statusbar-item")
        self.pack_start(self.time_label, False, False, 0)

        sep4 = Gtk.Label(label="|")
        sep4.get_style_context().add_class("statusbar-sep")
        self.pack_start(sep4, False, False, 0)

        self.stage_time_label = Gtk.Label(label="--")
        self.stage_time_label.get_style_context().add_class("statusbar-item")
        self.pack_start(self.stage_time_label, False, False, 0)

        sep5 = Gtk.Label(label="|")
        sep5.get_style_context().add_class("statusbar-sep")
        self.pack_start(sep5, False, False, 0)

        self.step_label = Gtk.Label(label="--")
        self.step_label.get_style_context().add_class("statusbar-item")
        self.pack_start(self.step_label, False, False, 0)

        sep6 = Gtk.Label(label="|")
        sep6.get_style_context().add_class("statusbar-sep")
        self.pack_start(sep6, False, False, 0)

        self.fault_label = Gtk.Label(label=tr("statusbar.no_faults"))
        self.fault_label.get_style_context().add_class("statusbar-item")
        self.pack_start(self.fault_label, False, False, 0)

        spacer = Gtk.Box()
        spacer.set_hexpand(True)
        self.pack_start(spacer, True, True, 0)

        self.io_label = Gtk.Label(label="--")
        self.io_label.get_style_context().add_class("statusbar-item")
        self.pack_start(self.io_label, False, False, 0)

    def update(self, state):
        self._last_state = state
        connected = state.get("connected", False)
        if not connected:
            self.mode_label.set_text(tr("statusbar.offline"))
            self.recipe_label.set_text("--")
            self.process_label.set_text("--")
            self.time_label.set_text("--")
            self.stage_time_label.set_text("--")
            self.step_label.set_text("--")
            self.fault_label.set_text("--")
            self.io_label.set_text("--")
            return

        metrics = state.get("metrics", {})
        wm = metrics.get("work_mode", 0)
        ds = metrics.get("device_state", 0)
        mode_text = mode_name(wm)
        state_text = state_name(ds)
        self.mode_label.set_text(f"{mode_text} / {state_text}")

        recipe_idx = metrics.get("recipe", 0)
        recipe_tpl = tr("statusbar.recipe", "Recipe {n}")
        self.recipe_label.set_text(recipe_tpl.format(n=recipe_idx + 1))

        stage_idx = metrics.get("stage", 0)
        if stage_idx == -1:
            proc_type = metrics.get("process_type", 0)
            stage_name = process_name(proc_type)
        else:
            stage_name = core.map_name(core.STAGES, stage_idx)
        progress = metrics.get("progress", 0)
        self.process_label.set_text(f"{stage_name}  {progress / 10.0:.1f}%")

        pt = state.get("process_time", {})
        total_rem = pt.get("total_remaining_ms", 0)
        if total_rem and total_rem > 0:
            mins = total_rem // 60000
            secs = (total_rem % 60000) // 1000
            time_tpl = tr("statusbar.flow_time", "Total {time}")
            self.time_label.set_text(time_tpl.format(time=f"{mins}:{secs:02d}"))
        else:
            self.time_label.set_text("--")

        stage_rem = pt.get("stage_remaining_ms", 0)
        if stage_rem and stage_rem > 0:
            s_mins = stage_rem // 60000
            s_secs = (stage_rem % 60000) // 1000
            stage_tpl = tr("statusbar.stage_time", "Stage {time}")
            self.stage_time_label.set_text(stage_tpl.format(time=f"{s_mins}:{s_secs:02d}"))
        else:
            self.stage_time_label.set_text("--")

        step_num = metrics.get("step", 0)
        if step_num:
            step_tpl = tr("statusbar.step", "Step {n}")
            self.step_label.set_text(step_tpl.format(n=step_num))
        else:
            self.step_label.set_text("--")

        fault = state.get("fault", {})
        active_code = fault.get("active_code", 0)
        alarms = state.get("faultAlarms", [])
        active_count = sum(1 for a in alarms if a.get("status") == "active")
        total_count = len(alarms)

        if active_code:
            fault_tpl = tr("statusbar.fault_count", "{active} Active / {total} Total")
            self.fault_label.set_text(fault_tpl.format(active=active_count, total=total_count))
            self.fault_label.get_style_context().add_class("badge-error")
            self.fault_label.get_style_context().remove_class("badge-success")
        elif total_count > 0:
            fault_tpl = tr("statusbar.fault_count", "{active} Active / {total} Total")
            self.fault_label.set_text(fault_tpl.format(active=0, total=total_count))
            self.fault_label.get_style_context().remove_class("badge-error")
            self.fault_label.get_style_context().add_class("badge-success")
        else:
            self.fault_label.set_text(tr("statusbar.no_faults"))
            self.fault_label.get_style_context().remove_class("badge-error")
            self.fault_label.get_style_context().add_class("badge-success")

        self.io_label.set_text(state.get("ioSummary", "--"))

    def refresh_language(self):
        """Update all labels when language changes."""
        if self._last_state:
            # Re-run update with stored state to refresh all translated strings
            self.update(self._last_state)
        else:
            # No state yet, just update the default labels
            self.fault_label.set_text(tr("statusbar.no_faults"))
            self.mode_label.set_text(tr("statusbar.offline"))
