#!/usr/bin/env python3
"""Control page — mode, recipe, process actions, door control.
Compact, restrained layout — Apple-like uniformity."""

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk

import server as core
from ..widgets import ResponsiveGrid, card, group_box, button, combo
from ..i18n import tr, recipe_name, state_name, process_name


class ControlPage(Gtk.Box):

    def __init__(self, send_action):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.get_style_context().add_class("page")
        self.set_hexpand(True)
        self.set_vexpand(True)
        self._send = send_action

        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroll.set_overlay_scrolling(True)
        self.pack_start(scroll, True, True, 0)

        c = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        c.set_margin_start(20)
        c.set_margin_end(20)
        c.set_margin_top(16)
        c.set_margin_bottom(16)
        scroll.add(c)

        # ── Pending / NACK alerts (top, prominent) ──────────────────────
        self.pending_label = Gtk.Label(label="", xalign=0)
        self.pending_label.get_style_context().add_class("pending")
        self.pending_label.set_line_wrap(True)
        self.pending_label.set_no_show_all(True)
        self.pending_label.hide()
        c.pack_start(self.pending_label, False, False, 0)

        self.nack_label = Gtk.Label(label="", xalign=0)
        self.nack_label.get_style_context().add_class("nack")
        self.nack_label.set_line_wrap(True)
        self.nack_label.set_no_show_all(True)
        self.nack_label.hide()
        c.pack_start(self.nack_label, False, False, 0)

        # ── Card 1: Mode & Recipe + Process Control (merged) ────────────
        ops_card = card(tr("ctrl.mode_recipe"))
        self._ops_card_title = _get_card_title(ops_card)

        # Row 1: Mode segmented + Recipe selector (horizontal)
        mr_row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        mr_row.set_hexpand(True)

        # Left: mode segmented control
        mode_col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        mode_lbl = Gtk.Label(label=tr("ctrl.work_mode"), xalign=0)
        mode_lbl.get_style_context().add_class("hint")
        self._mode_hint = mode_lbl
        mode_col.pack_start(mode_lbl, False, False, 0)
        seg = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=0)
        seg.get_style_context().add_class("segmented")
        seg.set_hexpand(True)
        self._btn_manual = Gtk.Button(label=tr("ctrl.manual"))
        self._btn_manual.connect("clicked", lambda _: self._send("mode", value=1))
        seg.pack_start(self._btn_manual, True, True, 0)
        self._btn_auto = Gtk.Button(label=tr("ctrl.auto"))
        self._btn_auto.connect("clicked", lambda _: self._send("mode", value=2))
        seg.pack_start(self._btn_auto, True, True, 0)
        self._btn_query = Gtk.Button(label=tr("ctrl.query"))
        self._btn_query.connect("clicked", lambda _: self._send("query"))
        seg.pack_start(self._btn_query, True, True, 0)
        mode_col.pack_start(seg, False, False, 0)
        mr_row.pack_start(mode_col, True, True, 0)

        # Divider
        sep = Gtk.Separator(orientation=Gtk.Orientation.VERTICAL)
        sep.get_style_context().add_class("ctrl-sep")
        mr_row.pack_start(sep, False, False, 0)

        # Right: recipe selector
        rec_col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        rec_lbl = Gtk.Label(label=tr("ctrl.recipe"), xalign=0)
        rec_lbl.get_style_context().add_class("hint")
        self._rec_hint = rec_lbl
        rec_col.pack_start(rec_lbl, False, False, 0)
        self._rec_inner = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        self.recipe_combo = combo([recipe_name(i) for i in range(26)])
        self._rec_inner.pack_start(self.recipe_combo, True, True, 0)
        self._btn_select = Gtk.Button(label=tr("ctrl.select"))
        self._btn_select.get_style_context().add_class("primary")
        self._btn_select.connect("clicked", self._select_recipe)
        self._rec_inner.pack_start(self._btn_select, False, False, 0)
        rec_col.pack_start(self._rec_inner, False, False, 0)
        mr_row.pack_start(rec_col, True, True, 0)

        ops_card.pack_start(mr_row, False, False, 6)

        # Separator inside card
        inner_sep = Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL)
        inner_sep.get_style_context().add_class("ctrl-info-sep")
        ops_card.pack_start(inner_sep, False, False, 4)

        # Row 2: Process control buttons — all uniform neutral
        flow_label = Gtk.Label(label=tr("ctrl.process_actions"), xalign=0)
        flow_label.get_style_context().add_class("hint")
        self._flow_section_hint = flow_label
        ops_card.pack_start(flow_label, False, False, 4)

        flow_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        flow_box.set_hexpand(True)
        self._flow_btns = {}
        flow_layout = [
            (1, "ctrl.start",    "ctrl-start"),
            (2, "ctrl.pause",    ""),
            (3, "ctrl.continue", ""),
            (4, "ctrl.end",      "ctrl-end"),
        ]
        for v, key, cl in flow_layout:
            b = Gtk.Button(label=tr(key))
            if cl:
                b.get_style_context().add_class(cl)
            b.set_hexpand(True)
            b.set_size_request(-1, 36)
            b.connect("clicked", lambda _, v=v: self._send("process", value=v))
            flow_box.pack_start(b, True, True, 0)
            self._flow_btns[v] = (b, key)
        ops_card.pack_start(flow_box, False, False, 0)

        c.pack_start(ops_card, False, False, 0)

        # ── Card 2: Door + Stage/Maint + Status (merged, 2 columns) ─────
        row2 = Gtk.Grid(column_spacing=8, row_spacing=8)
        row2.set_hexpand(True)

        # Left column: Door Control
        door_card = card(tr("ctrl.door_control"))
        self._door_card_title = _get_card_title(door_card)
        door_grid = Gtk.Grid(column_spacing=8, row_spacing=6)
        door_grid.set_hexpand(True)
        self._door_btns = {}
        door_items = [
            ("ctrl.open_top",    1, 0, 0),
            ("ctrl.close_top",   4, 1, 0),
            ("ctrl.open_bottom", 2, 0, 1),
            ("ctrl.close_bottom",3, 1, 1),
        ]
        for key, k, col, row in door_items:
            b = Gtk.Button(label=tr(key))
            b.set_hexpand(True)
            b.set_size_request(-1, 34)
            b.connect("clicked", lambda _, k=k:
                      self._send("interaction", type=k))
            door_grid.attach(b, col, row, 1, 1)
            self._door_btns[k] = (b, key)
        auto_btn = Gtk.Button(label=tr("ctrl.auto_close"))
        auto_btn.set_hexpand(True)
        auto_btn.set_size_request(-1, 34)
        auto_btn.connect("clicked", lambda _: self._send("interaction", type=7))
        door_grid.attach(auto_btn, 0, 2, 2, 1)
        self._door_btns[7] = (auto_btn, "ctrl.auto_close")
        door_card.pack_start(door_grid, False, False, 6)
        row2.attach(door_card, 0, 0, 1, 1)

        # Right column: Stage/Maint + Status Info
        stage_card = card(tr("ctrl.stage_maint"))
        self._stage_card_title = _get_card_title(stage_card)

        # Stage test row
        stb = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        self.stage_combo = combo(core.STAGES)
        stb.pack_start(self.stage_combo, True, True, 0)
        self._btn_stage_test = Gtk.Button(label=tr("ctrl.stage_test"))
        self._btn_stage_test.connect("clicked", lambda _: self._send("stageTest"))
        stb.pack_start(self._btn_stage_test, False, False, 0)
        stage_card.pack_start(stb, False, False, 6)

        # Maint buttons row — uniform neutral (fault_reset is subtle)
        maint_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        self._btn_self_clean = Gtk.Button(label=tr("ctrl.self_clean"))
        self._btn_self_clean.set_hexpand(True)
        self._btn_self_clean.set_size_request(-1, 34)
        self._btn_self_clean.connect("clicked", lambda _: self._send("process", value=6))
        maint_box.pack_start(self._btn_self_clean, True, True, 0)
        self._btn_fault_reset = Gtk.Button(label=tr("ctrl.fault_reset"))
        self._btn_fault_reset.get_style_context().add_class("ctrl-end")
        self._btn_fault_reset.set_hexpand(True)
        self._btn_fault_reset.set_size_request(-1, 34)
        self._btn_fault_reset.connect("clicked", lambda _: self._send("process", value=7))
        maint_box.pack_start(self._btn_fault_reset, True, True, 0)
        stage_card.pack_start(maint_box, False, False, 6)

        # Status info (compact, inline)
        info_sep = Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL)
        info_sep.get_style_context().add_class("ctrl-info-sep")
        stage_card.pack_start(info_sep, False, False, 4)

        self._info_grid = Gtk.Grid(column_spacing=10, row_spacing=4)
        self._info_grid.set_hexpand(True)
        self._info_labels = {}
        info_items = [
            ("ctrl.current_mode",   "mode"),
            ("ctrl.current_recipe", "recipe"),
            ("ctrl.current_stage",  "stage"),
            ("ctrl.current_step",   "step"),
            ("ctrl.run_state",      "state"),
        ]
        for i, (label_key, field) in enumerate(info_items):
            col = (i % 2) * 2
            row = i // 2
            lbl = Gtk.Label(label=tr(label_key), xalign=0)
            lbl.get_style_context().add_class("hint")
            self._info_grid.attach(lbl, col, row, 1, 1)
            val = Gtk.Label(label="--", xalign=0)
            val.get_style_context().add_class("info-value")
            self._info_grid.attach(val, col + 1, row, 1, 1)
            self._info_labels[field] = val
        stage_card.pack_start(self._info_grid, False, False, 4)

        row2.attach(stage_card, 1, 0, 1, 1)
        c.pack_start(row2, False, False, 0)

    def _select_recipe(self, *_):
        self._send("recipe", index=self.recipe_combo.get_active())

    def update_state(self, state):
        ia = state.get("pendingInteraction")
        if ia:
            desc = core.describe_interaction(
                ia.get("stage", 0), ia.get("type", 0))
            self.pending_label.set_text(f"  {tr('ctrl.request')}: {desc}  ")
            self.pending_label.show()
        else:
            self.pending_label.hide()

        nack = state.get("last_nack")
        if nack:
            req = nack.get("request_cmd", 0)
            err = nack.get("error", 0)
            self.nack_label.set_text(
                f"  NACK: cmd=0x{req:02X}  {core.ERROR_NAMES.get(err, f'0x{err:02X}')}  ")
            self.nack_label.show()
        else:
            self.nack_label.hide()

        # Update info labels
        m = state.get("metrics", {})
        # "当前模式" shows process_type (自动流程/单流程测试/自清洁/手动操作)
        pt_val = m.get("process_type")
        if pt_val is not None:
            self._info_labels["mode"].set_text(process_name(pt_val))

        recipe_val = m.get("recipe")
        if recipe_val is not None:
            self._info_labels["recipe"].set_text(recipe_name(recipe_val))

        stage_val = m.get("stage")
        if stage_val is not None and stage_val == -1:
            # Self-clean: stage is -1, show process_type name
            self._info_labels["stage"].set_text(process_name(pt_val))
        elif stage_val is not None and 0 <= stage_val < len(core.STAGES):
            self._info_labels["stage"].set_text(core.STAGES[stage_val])
        else:
            self._info_labels["stage"].set_text("--")

        step_val = m.get("step")
        if step_val is not None:
            self._info_labels["step"].set_text(str(step_val) if step_val else "--")

        run_state = m.get("device_state")
        if run_state is not None:
            self._info_labels["state"].set_text(state_name(run_state))

    def refresh_language(self):
        self._btn_manual.set_label(tr("ctrl.manual"))
        self._btn_auto.set_label(tr("ctrl.auto"))
        self._btn_query.set_label(tr("ctrl.query"))
        self._btn_select.set_label(tr("ctrl.select"))
        self._btn_stage_test.set_label(tr("ctrl.stage_test"))
        self._btn_self_clean.set_label(tr("ctrl.self_clean"))
        self._btn_fault_reset.set_label(tr("ctrl.fault_reset"))
        for v, (b, key) in self._flow_btns.items():
            b.set_label(tr(key))
        for k, (b, key) in self._door_btns.items():
            b.set_label(tr(key))

        # Card titles
        if hasattr(self, '_ops_card_title') and self._ops_card_title:
            self._ops_card_title.set_text(tr("ctrl.mode_recipe"))
        if self._stage_card_title:
            self._stage_card_title.set_text(tr("ctrl.stage_maint"))
        if self._door_card_title:
            self._door_card_title.set_text(tr("ctrl.door_control"))

        # Section hint
        if hasattr(self, '_flow_section_hint'):
            self._flow_section_hint.set_text(tr("ctrl.process_actions"))

        # Hint labels
        if hasattr(self, '_mode_hint'):
            self._mode_hint.set_text(tr("ctrl.work_mode"))
        if hasattr(self, '_rec_hint'):
            self._rec_hint.set_text(tr("ctrl.recipe"))

        # Rebuild recipe combo
        self.recipe_combo.destroy()
        self.recipe_combo = combo([recipe_name(i) for i in range(26)])
        if hasattr(self, '_rec_box') and self._rec_box.get_parent():
            self._rec_box.destroy()
        rb = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        rb.pack_start(self.recipe_combo, True, True, 0)
        rb.pack_start(self._btn_select, False, False, 0)
        self._rec_inner = rb
        rec_col = self._rec_inner.get_parent()
        if rec_col:
            children = rec_col.get_children()
            if len(children) >= 2:
                children[1].destroy()
            rec_col.pack_start(rb, False, False, 0)
            rb.show_all()
        self._rec_box = rb

        # Info labels
        info_keys = [
            ("ctrl.current_mode",   "mode"),
            ("ctrl.current_recipe", "recipe"),
            ("ctrl.current_stage",  "stage"),
            ("ctrl.current_step",   "step"),
            ("ctrl.run_state",      "state"),
        ]
        for i, (label_key, field) in enumerate(info_keys):
            col = (i % 2) * 2
            row = i // 2
            child = self._info_grid.get_child_at(col, row)
            if child:
                child.set_text(tr(label_key))


def _get_card_title(card_box):
    for child in card_box.get_children():
        if isinstance(child, Gtk.Box):
            for sub in child.get_children():
                if isinstance(sub, Gtk.Label):
                    ctx = sub.get_style_context()
                    if "card-title" in ctx.list_classes():
                        return sub
    return None
