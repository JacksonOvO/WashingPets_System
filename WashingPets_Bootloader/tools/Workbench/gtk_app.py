#!/usr/bin/env python3
"""PawClean Workbench — main entry point.

Apple/macOS inspired UI for STM32 pet washing machine control.
"""

import sys
import threading
import time
from pathlib import Path

import cairo
import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
gi.require_foreign("cairo")
from gi.repository import Gdk, GdkPixbuf, GLib, Gtk, Pango

APP_DIR = Path(__file__).resolve().parent
if str(APP_DIR) not in sys.path:
    sys.path.insert(0, str(APP_DIR))

import server as core
from ui.theme import get_css
from ui.sidebar import Sidebar
from ui.header import Header
from ui.statusbar import StatusBar
from ui.pages.dashboard import DashboardPage
from ui.pages.control import ControlPage
from ui.pages.telemetry import TelemetryPage
from ui.pages.faults import FaultsPage
from ui.pages.params import ParamsPage
from ui.pages.io_control import IOControlPage
from ui.pages.upgrade import UpgradePage
from ui.pages.test import TestPage
from ui.i18n import tr

GLib.set_prgname("pawclean-workbench")
GLib.set_application_name("PawClean Workbench")
Gdk.set_program_class("pawclean-workbench")


class WorkbenchWindow(Gtk.Window):

    def __init__(self):
        super().__init__(title="PawClean Workbench")
        self.set_role("pawclean-workbench")
        self.set_default_size(1400, 900)
        self.set_position(Gtk.WindowPosition.CENTER)

        # Prefer PNG for window icon (GTK3 handles PNG reliably; SVG often fails)
        for icon_name in ("pawclean-app-icon.png", "pawclean-app-icon.svg"):
            icon_path = APP_DIR / icon_name
            if icon_path.exists():
                try:
                    self.set_icon_from_file(str(icon_path))
                    break
                except Exception:
                    continue

        settings = Gtk.Settings.get_default()
        if settings:
            settings.set_property("gtk-application-prefer-dark-theme", True)

        self.state = core.STATE
        self.upgrade_running = False
        self._pages = {}
        self._current_page = None

        self._build_ui()
        self._apply_css()

        GLib.timeout_add(500, self._refresh)
        GLib.timeout_add(1000, self._poll)
        # Defer initial page show until after window is realized
        GLib.idle_add(self._show_initial_page)

    # ── UI Construction ────────────────────────────────────────────────────

    def _build_ui(self):
        root = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)

        self.sidebar = Sidebar()
        self.sidebar.connect("page-changed", self._on_nav)
        root.pack_start(self.sidebar, False, False, 0)

        right = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        right.set_hexpand(True)
        right.set_vexpand(True)

        self.header = Header()
        self.header.connect_button.connect("clicked", self._toggle_conn)
        self.header.connect("language-changed", self._on_language_changed)
        right.pack_start(self.header, False, False, 0)

        self.stack = Gtk.Stack()
        self.stack.set_transition_type(Gtk.StackTransitionType.CROSSFADE)
        self.stack.set_transition_duration(200)
        self.stack.set_hexpand(True)
        self.stack.set_vexpand(True)
        self._page_widgets = {}
        right.pack_start(self.stack, True, True, 0)

        self.statusbar = StatusBar()
        right.pack_start(self.statusbar, False, False, 0)

        root.pack_start(right, True, True, 0)
        self.add(root)

        sa = self._send_action
        self._page_defs = {
            "dashboard": lambda: DashboardPage(),
            "control": lambda: ControlPage(sa),
            "telemetry": lambda: TelemetryPage(),
            "faults": lambda: FaultsPage(sa),
            "params": lambda: ParamsPage(sa),
            "io": lambda: IOControlPage(sa),
            "test": lambda: TestPage(sa),
            "upgrade": lambda: UpgradePage(),
        }

        self.connect("destroy", self._on_destroy)

    def _show_initial_page(self):
        """Show the initial dashboard page after the window is realized."""
        self._show_page("dashboard")
        self.show_all()
        return False  # Don't repeat the idle callback

    def _apply_css(self):
        provider = Gtk.CssProvider()
        provider.load_from_data(get_css(dark=True))
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(),
            provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION,
        )

    def _ensure_page(self, name):
        if name in self._pages:
            return
        factory = self._page_defs.get(name)
        if not factory:
            return
        page = factory()
        self._pages[name] = page
        self._page_widgets[name] = page
        self.stack.add_named(page, name)
        page.show_all()
        if name == "upgrade":
            page.set_callbacks(
                self._browse_firmware, self._query_device, self._upgrade)

    def _show_page(self, name):
        self._ensure_page(name)
        if name not in self._page_widgets:
            return
        page_widget = self._page_widgets[name]
        self.stack.set_visible_child(page_widget)
        self._current_page = name
        self.sidebar.set_active(name)

    # ── Language ───────────────────────────────────────────────────────────

    def _on_language_changed(self, *_):
        self._refresh_all_language()

    def _refresh_all_language(self):
        self.sidebar.refresh_language()
        self.header.refresh_language()
        self.statusbar.refresh_language()
        for page in self._pages.values():
            if hasattr(page, "refresh_language"):
                page.refresh_language()

    # ── Navigation ─────────────────────────────────────────────────────────

    def _on_nav(self, _sidebar, page_name):
        self._show_page(page_name)

    # ── Connection ─────────────────────────────────────────────────────────

    def _toggle_conn(self, *_):
        if self.state.running:
            self.state.close()
            self.header.set_connected(False)
        else:
            port = self.header.port_combo.get_active_text()
            if not port:
                self.header._refresh_ports()
                port = self.header.port_combo.get_active_text()
            if not port:
                self._error(tr("err.no_serial"))
                return
            try:
                self.state.open(port)
                self.header.set_connected(True, port)
            except Exception as e:
                self._error(str(e))

    # ── State Refresh ──────────────────────────────────────────────────────

    def _refresh(self):
        page = self._current_page
        snap = self.state.snapshot(
            include_logs=page == "upgrade",
            include_samples=page in ("telemetry", "test"),
        )
        self.header.set_connected(snap["connected"], snap.get("port", ""))
        self.statusbar.update(snap)
        if page and page in self._pages:
            self._pages[page].update_state(snap)
        return True

    def _poll(self):
        if self.state.running and self.header.poll_check.get_active():
            self.state.poll_once()
        return True

    # ── Actions ────────────────────────────────────────────────────────────

    def _send_action(self, action, **kwargs):
        try:
            if action == "mode":
                self.state.send(core.CMD_SET_MODE,
                                core.u16_payload(kwargs["value"]))
            elif action == "query":
                self.state.poll_once()
            elif action == "process":
                val = kwargs["value"]
                param = kwargs.get("param", 0)
                if val == 5:
                    if not self._check_stage_test_mode():
                        return
                if val in (1, 6):
                    threading.Thread(
                        target=self._process_worker,
                        args=(val, param), daemon=True).start()
                else:
                    self.state.send(core.CMD_PROCESS,
                                    core.process_payload(val, param))
            elif action == "interaction":
                itype = kwargs.get("type", 0)
                result_val = kwargs.get("result", 1)
                value_val = kwargs.get("value", 0)
                pi = self.state.pending_interaction
                if pi:
                    req = pi.get("id", 0)
                else:
                    import time as _time
                    req = int(_time.time()) & 0xFFFFFFFF
                self.state.send(core.CMD_INTERACTION,
                                core.interaction_payload(req, itype, result_val, value_val))
            elif action == "recipe":
                idx = kwargs.get("index", 0)
                pi = self.state.pending_interaction
                if pi:
                    req = pi.get("id", 0)
                else:
                    import time as _time
                    req = int(_time.time()) & 0xFFFFFFFF
                self.state.send(core.CMD_INTERACTION,
                                core.interaction_payload(req, 6, 1, idx))
            elif action == "saveParam":
                rev = 0
                pp = self._pages.get("params")
                if pp:
                    rev = pp.get_revision()
                self.state.send(core.CMD_SAVE_PARAM,
                                core.struct.pack("<I", rev))
            elif action == "restoreParam":
                self.state.send(core.CMD_RESTORE_PARAM, b"DFLT")
            elif action == "readParam":
                pp = self._pages.get("params")
                idx = pp.get_recipe_index() if pp else 0
                self.state.request_param(idx)
            elif action == "writeParam":
                pp = self._pages.get("params")
                if pp:
                    dirty = pp.get_dirty_params()
                    if dirty:
                        rev = pp.get_revision()
                        idx = pp.get_recipe_index()
                        self.state.send_param_update(idx, rev, dirty)
                        pp.mark_clean()
            elif action == "booster":
                val = 1 if kwargs.get("start") else 0
                self.state.send(core.CMD_BOOSTER_CONTROL,
                                core.u16_payload(val))
            elif action == "rs485":
                station_index = int(kwargs.get("station", 0) or 0)
                if 0 <= station_index < len(core.RS485_STATIONS):
                    station_text = core.RS485_STATIONS[station_index]
                    try:
                        station = int(station_text.split(" ", 1)[0])
                    except (ValueError, TypeError):
                        station = station_index + 1
                else:
                    station = station_index + 1
                value = kwargs.get("value", 0)
                self.state.send(core.CMD_MANUAL_RS485,
                                core.struct.pack("<HH", station, value))
            elif action == "actuatorMode":
                self.state.send(core.CMD_SET_ACTUATOR_MODE,
                                core.u16_payload(kwargs.get("mode", 1)))
            elif action == "output":
                oid = kwargs.get("output_id", 0)
                val = 1 if kwargs.get("state") else 0
                self.state.send(core.CMD_MANUAL_OUTPUT,
                                core.struct.pack("<HBBI", oid, val, 0, 0))
            elif action == "stageTest":
                pp = self._pages.get("control")
                stage_idx = 0
                if pp and hasattr(pp, "stage_combo"):
                    stage_idx = pp.stage_combo.get_active()
                self.state.send(core.CMD_PROCESS,
                                core.process_payload(5, stage_idx))
            elif action == "selfTest":
                try:
                    self.state.set_mode_wait(2)
                except Exception:
                    pass
                mode = kwargs.get("mode", 0)
                self.state.send(core.CMD_PROCESS,
                                core.process_payload(8 + mode))
        except Exception as e:
            self._error(str(e))

    def _process_worker(self, action, param):
        try:
            if action in (1, 6):
                self.state.set_mode_wait(2)
            self.state.send(core.CMD_PROCESS,
                            core.process_payload(action, param))
        except Exception as e:
            GLib.idle_add(self._error, str(e))

    def _check_stage_test_mode(self):
        with self.state.lock:
            m = self.state.status.get("metrics", {})
            mode = m.get("work_mode")
        if mode is not None and mode != 1:
            self._error(tr("err.stage_test_mode"))
            return False
        return True

    # ── Upgrade ────────────────────────────────────────────────────────────

    def _browse_firmware(self):
        dialog = Gtk.FileChooserDialog(
            tr("fw.select_firmware"), self, Gtk.FileChooserAction.OPEN,
            (tr("fw.cancel"), Gtk.ResponseType.CANCEL,
             tr("fw.open"), Gtk.ResponseType.ACCEPT))
        filt = Gtk.FileFilter()
        filt.set_name(tr("fw.binary_files"))
        filt.add_pattern("*.bin")
        dialog.add_filter(filt)
        if dialog.run() == Gtk.ResponseType.ACCEPT:
            up = self._pages.get("upgrade")
            if up:
                up.firmware_entry.set_text(dialog.get_filename())
        dialog.destroy()

    def _query_device(self):
        up = self._pages.get("upgrade")
        if up:
            up.upgrade_btn.set_sensitive(False)
        port = self.header.port_combo.get_active_text()
        if not port:
            if up:
                up.set_status(tr("fw.no_port"))
                up.upgrade_btn.set_sensitive(True)
            return
        threading.Thread(
            target=self._query_worker, args=(port,), daemon=True).start()

    def _query_worker(self, port):
        try:
            info = core.query_iap(port)
            GLib.idle_add(self._query_done, info, None)
        except Exception as e:
            GLib.idle_add(self._query_done, None, str(e))

    def _query_done(self, info, error):
        up = self._pages.get("upgrade")
        if not up:
            return
        up.upgrade_btn.set_sensitive(True)
        if error:
            up.set_status(f"{tr('fw.query_failed')}: {error}")
        elif info:
            up.set_device_info(info)
            parts = []
            for k in ("hardwareVersion", "bootVersion", "appVersion"):
                v = info.get(k)
                if v is not None:
                    parts.append(f"{k}={v}")
            up.set_status(
                "  |  ".join(parts) if parts else tr("fw.device_queried"))

    def _upgrade(self):
        up = self._pages.get("upgrade")
        if not up or self.upgrade_running:
            return
        port = self.header.port_combo.get_active_text()
        if not port:
            up.set_status(tr("fw.no_port"))
            return
        self.upgrade_running = True
        up.set_upgrade_running(True)
        fw = up.firmware_entry.get_text()
        ver = up.version_entry.get_text()
        threading.Thread(
            target=self._upgrade_worker, args=(port, fw, ver),
            daemon=True).start()

    def _upgrade_worker(self, port, firmware, version):
        try:
            core.upgrade_iap(port, firmware, version)
            GLib.idle_add(self._upgrade_done, tr("fw.upgrade_complete"), False)
        except Exception as e:
            GLib.idle_add(self._upgrade_done, f"{tr('fw.failed')}: {e}", True)

    def _upgrade_done(self, text, is_error):
        self.upgrade_running = False
        up = self._pages.get("upgrade")
        if up:
            up.upgrade_btn.set_sensitive(True)
            up.set_status(text)

    # ── Utilities ──────────────────────────────────────────────────────────

    def _error(self, message):
        dialog = Gtk.MessageDialog(
            self, 0, Gtk.MessageType.ERROR, Gtk.ButtonsType.OK, message)
        dialog.run()
        dialog.destroy()

    def _on_destroy(self, *_):
        self.state.close()
        Gtk.main_quit()


def main():
    win = WorkbenchWindow()
    win.show_all()
    Gtk.main()


if __name__ == "__main__":
    main()
