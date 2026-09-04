#!/usr/bin/env python3
"""Design System — Apple/macOS inspired theme for PawClean Workbench."""

# ─── Color Tokens ────────────────────────────────────────────────────────────

class Colors:
    # Background layers
    BG_PRIMARY = "#F5F5F7"
    BG_CARD = "#FFFFFF"
    BG_ELEVATED = "#FFFFFF"
    BG_SIDEBAR = "#F0F0F2"
    BG_HOVER = "#E8E8EA"
    BG_GROUP = "#F0F0F2"
    BG_INPUT = "#FFFFFF"

    # Text
    TEXT_PRIMARY = "#1D1D1F"
    TEXT_SECONDARY = "#86868B"
    TEXT_TERTIARY = "#AEAEB2"
    TEXT_DISABLED = "#C7C7CC"

    # Brand
    ACCENT = "#0071E3"
    ACCENT_HOVER = "#0077ED"

    # Status
    SUCCESS = "#34C759"
    WARNING = "#FF9500"
    ERROR = "#FF3B30"
    INFO = "#5AC8FA"

    # Borders
    BORDER = "#D2D2D7"
    BORDER_LIGHT = "#E5E5EA"

    # Dark mode
    DARK_BG_PRIMARY = "#1C1C1E"
    DARK_BG_CARD = "#2C2C2E"
    DARK_BG_SIDEBAR = "#1C1C1E"
    DARK_BG_HOVER = "#3A3A3C"
    DARK_BG_GROUP = "#3A3A3C"
    DARK_BG_INPUT = "#1C1C1E"
    DARK_TEXT_PRIMARY = "#F5F5F7"
    DARK_TEXT_SECONDARY = "#98989D"
    DARK_TEXT_TERTIARY = "#636366"
    DARK_BORDER = "#38383A"
    DARK_BORDER_LIGHT = "#48484A"


# ─── Spacing ─────────────────────────────────────────────────────────────────

class Spacing:
    XS = 4
    SM = 8
    MD = 12
    LG = 16
    XL = 24
    XXL = 32


# ─── Border Radius ───────────────────────────────────────────────────────────

class Radius:
    SM = 6
    MD = 10
    LG = 14
    XL = 20


# ─── Font Sizes ──────────────────────────────────────────────────────────────

class FontSize:
    XS = 11
    SM = 13
    BASE = 15
    LG = 17
    XL = 22
    XXL = 28
    XXXL = 34


# ─── CSS Generation ──────────────────────────────────────────────────────────

def get_css(dark: bool = True) -> bytes:
    """Generate complete CSS stylesheet."""

    if dark:
        bg_primary = Colors.DARK_BG_PRIMARY
        bg_card = Colors.DARK_BG_CARD
        bg_sidebar = Colors.DARK_BG_SIDEBAR
        bg_hover = Colors.DARK_BG_HOVER
        bg_group = Colors.DARK_BG_GROUP
        bg_input = Colors.DARK_BG_INPUT
        text_primary = Colors.DARK_TEXT_PRIMARY
        text_secondary = Colors.DARK_TEXT_SECONDARY
        text_tertiary = Colors.DARK_TEXT_TERTIARY
        border = Colors.DARK_BORDER
        border_light = Colors.DARK_BORDER_LIGHT
    else:
        bg_primary = Colors.BG_PRIMARY
        bg_card = Colors.BG_CARD
        bg_sidebar = Colors.BG_SIDEBAR
        bg_hover = Colors.BG_HOVER
        bg_group = Colors.BG_GROUP
        bg_input = Colors.BG_INPUT
        text_primary = Colors.TEXT_PRIMARY
        text_secondary = Colors.TEXT_SECONDARY
        text_tertiary = Colors.TEXT_TERTIARY
        border = Colors.BORDER
        border_light = Colors.BORDER_LIGHT

    css = f"""
* {{
    color: {text_primary};
    font-family: -apple-system, "SF Pro Display", "PingFang SC", "Helvetica Neue", "Noto Sans SC", sans-serif;
}}

window {{
    background: {bg_primary};
    color: {text_primary};
}}

scrolledwindow {{
    background: {bg_primary};
    border: 0;
}}

viewport {{
    background: {bg_primary};
}}

/* ─── Sidebar ──────────────────────────────────────────────────────────── */

.sidebar {{
    background: {bg_sidebar};
    border-right: 1px solid {border};
    padding: 16px 12px 20px 12px;
}}

.brand {{
    margin-bottom: 24px;
    padding: 4px 8px;
}}

.brand-title {{
    font-size: 18px;
    font-weight: 700;
    color: {text_primary};
    letter-spacing: -0.3px;
}}

.brand-subtitle {{
    font-size: 11px;
    color: {text_secondary};
    margin-top: 2px;
}}

.nav-section {{
    margin-top: 16px;
    margin-bottom: 8px;
    padding: 0 12px;
}}

.nav-section-title {{
    font-size: 11px;
    font-weight: 600;
    color: {text_tertiary};
    letter-spacing: 0.5px;
}}

.nav {{
    margin: 2px 0;
    padding: 9px 12px;
    border-radius: {Radius.SM}px;
    color: {text_secondary};
    background: transparent;
    border: 0;
    font-size: 14px;
    font-weight: 500;
    transition: all 150ms ease;
}}

.nav:hover {{
    background: {bg_hover};
    color: {text_primary};
}}

.nav-active {{
    background: {Colors.ACCENT};
    color: #FFFFFF;
    font-weight: 600;
}}

.nav-active:hover {{
    background: {Colors.ACCENT_HOVER};
}}

/* ─── Header ───────────────────────────────────────────────────────────── */

.header {{
    background: {bg_card};
    border-bottom: 1px solid {border};
    padding: 12px 20px;
}}

.connection {{
    background: {bg_group};
    border-radius: {Radius.SM}px;
    padding: 8px 14px;
    font-weight: 600;
    font-size: 13px;
    color: {text_secondary};
}}

.connected {{
    background: rgba(52, 199, 89, 0.12);
    color: {Colors.SUCCESS};
}}

/* ─── Page ─────────────────────────────────────────────────────────────── */

.page {{
    padding: 20px 24px;
}}

.page-title {{
    font-size: {FontSize.XL}px;
    font-weight: 700;
    color: {text_primary};
    margin-bottom: 4px;
    letter-spacing: -0.3px;
}}

.page-subtitle {{
    font-size: {FontSize.SM}px;
    color: {text_secondary};
    margin-bottom: 20px;
}}

/* ─── Cards ────────────────────────────────────────────────────────────── */

.card {{
    background: {bg_card};
    border: 1px solid {border_light};
    border-radius: {Radius.MD}px;
    padding: 18px 20px;
    box-shadow: 0 1px 3px rgba(0,0,0,0.04);
}}

.card-header {{
    margin-bottom: 12px;
}}

.card-title {{
    font-size: {FontSize.LG}px;
    font-weight: 600;
    color: {text_primary};
    letter-spacing: -0.2px;
}}

.card-meta {{
    color: {text_secondary};
    font-size: {FontSize.SM}px;
}}

/* ─── Hero Status ──────────────────────────────────────────────────────── */

.hero-status {{
    background: {bg_card};
    border: 1px solid {border_light};
    border-radius: {Radius.LG}px;
    padding: 24px 28px;
    box-shadow: 0 2px 8px rgba(0,0,0,0.06);
}}

.hero-status-text {{
    font-size: {FontSize.XXXL}px;
    font-weight: 700;
    letter-spacing: -0.5px;
}}

.hero-status-normal {{
    color: {Colors.SUCCESS};
}}

.hero-status-idle {{
    color: {Colors.ACCENT};
}}

.hero-status-warning {{
    color: {Colors.WARNING};
}}

.hero-status-error {{
    color: {Colors.ERROR};
}}

.hero-meta {{
    font-size: {FontSize.BASE}px;
    color: {text_secondary};
    margin-top: 8px;
}}

/* ─── Metric Cards ─────────────────────────────────────────────────────── */

.metric {{
    background: {bg_group};
    border-radius: {Radius.MD}px;
    padding: 14px 16px;
    min-width: 120px;
    min-height: 72px;
    border: 1px solid {border_light};
    transition: all 150ms ease;
}}

.metric:hover {{
    box-shadow: 0 2px 8px rgba(0,0,0,0.06);
}}

.metric-name {{
    color: {text_secondary};
    font-size: {FontSize.XS}px;
    font-weight: 500;
    letter-spacing: 0.3px;
}}

.metric-value {{
    color: {text_primary};
    font-size: 22px;
    font-weight: 700;
    margin-top: 6px;
    letter-spacing: -0.3px;
}}

.metric-unit {{
    font-size: {FontSize.SM}px;
    font-weight: 500;
    color: {text_secondary};
}}

/* ─── Sensor Tile ──────────────────────────────────────────────────────── */

.sensor-tile {{
    background: {bg_card};
    border: 1px solid {border_light};
    border-radius: {Radius.MD}px;
    padding: 16px 18px;
    min-width: 140px;
    min-height: 88px;
    box-shadow: 0 1px 3px rgba(0,0,0,0.04);
}}

.sensor-tile-icon {{
    font-size: 20px;
    margin-bottom: 6px;
}}

.sensor-tile-name {{
    color: {text_secondary};
    font-size: {FontSize.XS}px;
    font-weight: 500;
}}

.sensor-tile-value {{
    color: {text_primary};
    font-size: {FontSize.XXL}px;
    font-weight: 700;
    margin-top: 4px;
    letter-spacing: -0.5px;
}}

.sensor-tile-invalid {{
    border-color: {Colors.WARNING};
    opacity: 0.7;
}}

/* ─── Actuator Bar ─────────────────────────────────────────────────────── */

.actuator-item {{
    background: {bg_group};
    border-radius: {Radius.SM}px;
    padding: 12px 14px;
    border: 1px solid {border_light};
}}

.actuator-name {{
    color: {text_secondary};
    font-size: {FontSize.XS}px;
    font-weight: 500;
}}

.actuator-value {{
    color: {text_primary};
    font-size: {FontSize.LG}px;
    font-weight: 700;
    margin-top: 4px;
}}

/* ─── Progress Bar ─────────────────────────────────────────────────────── */

.progress-bar {{
    min-height: 6px;
    background: {bg_hover};
    border-radius: 3px;
}}

.progress-bar trough {{
    background: {bg_hover};
    border-radius: 3px;
    min-height: 6px;
}}

.progress-bar progress {{
    background: {Colors.ACCENT};
    border-radius: 3px;
    min-height: 6px;
}}

.progress-bar-success progress {{
    background: {Colors.SUCCESS};
}}

.progress-bar-warning progress {{
    background: {Colors.WARNING};
}}

.progress-bar-error progress {{
    background: {Colors.ERROR};
}}

/* ─── Buttons ──────────────────────────────────────────────────────────── */

button {{
    background: {bg_group};
    color: {text_primary};
    border: 1px solid {border};
    border-radius: {Radius.SM}px;
    min-height: 36px;
    padding: 0 16px;
    font-size: 14px;
    font-weight: 500;
    transition: all 100ms ease;
}}

button:hover {{
    background: {bg_hover};
}}

button:active {{
    opacity: 0.85;
}}

button:disabled {{
    opacity: 0.5;
}}

button.primary {{
    background: {Colors.ACCENT};
    color: #FFFFFF;
    border-color: {Colors.ACCENT};
}}

button.primary:hover {{
    background: {Colors.ACCENT_HOVER};
}}

button.danger {{
    background: rgba(255, 59, 48, 0.08);
    color: {Colors.ERROR};
    border-color: rgba(255, 59, 48, 0.2);
}}

button.danger:hover {{
    background: rgba(255, 59, 48, 0.14);
}}

button.start {{
    background: rgba(52, 199, 89, 0.08);
    color: {Colors.SUCCESS};
    border-color: rgba(52, 199, 89, 0.2);
}}

button.start:hover {{
    background: rgba(52, 199, 89, 0.14);
}}

button.stop {{
    background: rgba(255, 59, 48, 0.08);
    color: {Colors.ERROR};
    border-color: rgba(255, 59, 48, 0.2);
}}

button.stop:hover {{
    background: rgba(255, 59, 48, 0.14);
}}

button.ctrl-start {{
    background: rgba(0, 113, 227, 0.06);
    color: {Colors.ACCENT};
    border-color: rgba(0, 113, 227, 0.15);
}}

button.ctrl-start:hover {{
    background: rgba(0, 113, 227, 0.12);
}}

button.ctrl-end {{
    background: rgba(255, 59, 48, 0.05);
    color: {Colors.ERROR};
    border-color: rgba(255, 59, 48, 0.12);
}}

button.ctrl-end:hover {{
    background: rgba(255, 59, 48, 0.10);
}}

button.big {{
    min-height: 44px;
    font-size: 15px;
    font-weight: 600;
    padding: 0 24px;
}}

/* ─── Segmented Control ────────────────────────────────────────────────── */

.segmented {{
    background: {bg_group};
    border-radius: {Radius.SM}px;
    padding: 2px;
}}

.segmented button {{
    border-radius: {Radius.SM - 2}px;
    border: 0;
    background: transparent;
    min-height: 32px;
    padding: 0 16px;
    font-size: 13px;
    font-weight: 500;
    color: {text_secondary};
}}

.segmented button:checked,
.segmented button.seg-active {{
    background: {bg_card};
    color: {text_primary};
    font-weight: 600;
    box-shadow: 0 1px 3px rgba(0,0,0,0.08);
}}

/* ─── Inputs ───────────────────────────────────────────────────────────── */

entry {{
    background: {bg_input};
    color: {text_primary};
    border: 1px solid {border};
    border-radius: {Radius.SM}px;
    min-height: 36px;
    padding: 0 12px;
    font-size: 14px;
    caret-color: {Colors.ACCENT};
}}

entry:focus {{
    border-color: {Colors.ACCENT};
    box-shadow: 0 0 0 3px rgba(0, 113, 227, 0.15);
}}

entry selection {{
    background: {Colors.ACCENT};
    color: #FFFFFF;
}}

combobox, combobox box {{
    color: {text_primary};
}}

combobox button {{
    background: {bg_input};
    color: {text_primary};
    border-color: {border};
    border-radius: {Radius.SM}px;
    padding: 0 12px;
    min-height: 36px;
}}

checkbutton {{
    color: {text_primary};
    font-size: 14px;
}}

checkbutton check {{
    border-radius: 4px;
    border-color: {border};
}}

/* ─── Switch (iOS-style) ───────────────────────────────────────────────── */

switch {{
    background: {bg_hover};
    border-radius: 13px;
    min-width: 44px;
    min-height: 26px;
    border: 0;
    padding: 2px;
}}

switch slider {{
    background: #FFFFFF;
    border-radius: 11px;
    min-width: 22px;
    min-height: 22px;
    box-shadow: 0 1px 3px rgba(0,0,0,0.2);
}}

switch:checked {{
    background: {Colors.SUCCESS};
}}

/* ─── Badges ───────────────────────────────────────────────────────────── */

.badge {{
    border-radius: 10px;
    padding: 3px 10px;
    font-size: {FontSize.XS}px;
    font-weight: 600;
}}

.badge-success {{
    background: rgba(52, 199, 89, 0.12);
    color: {Colors.SUCCESS};
}}

.badge-warning {{
    background: rgba(255, 159, 10, 0.12);
    color: {Colors.WARNING};
}}

.badge-error {{
    background: rgba(255, 59, 48, 0.12);
    color: {Colors.ERROR};
}}

.badge-info {{
    background: rgba(90, 200, 250, 0.12);
    color: {Colors.INFO};
}}

.badge-neutral {{
    background: {bg_hover};
    color: {text_secondary};
}}

/* ─── Fault Cards ──────────────────────────────────────────────────────── */

.fault-card {{
    background: {bg_card};
    border: 1px solid {border_light};
    border-radius: {Radius.MD}px;
    padding: 16px 20px;
    border-left: 4px solid {border};
}}

.fault-card-active {{
    border-left-color: {Colors.ERROR};
}}

.fault-card-warning {{
    border-left-color: {Colors.WARNING};
}}

.fault-card-info {{
    border-left-color: {Colors.INFO};
}}

.fault-card-resolved {{
    border-left-color: {Colors.SUCCESS};
    opacity: 0.7;
}}

.fault-code {{
    font-size: {FontSize.XXL}px;
    font-weight: 800;
    color: {text_secondary};
    font-family: "SF Mono", "JetBrains Mono", "Fira Code", monospace;
}}

.fault-code.fault-active {{
    color: {Colors.ERROR};
}}

.fault-name {{
    font-size: {FontSize.BASE}px;
    font-weight: 600;
    color: {text_primary};
}}

.fault-time {{
    font-size: {FontSize.XS}px;
    color: {text_tertiary};
    font-family: "SF Mono", monospace;
}}

/* ─── IO Items ─────────────────────────────────────────────────────────── */

.io-item {{
    background: {bg_group};
    border: 1px solid {border_light};
    border-radius: {Radius.SM}px;
    padding: 8px 12px;
    min-height: 36px;
}}

.io-item label {{
    color: {text_primary};
    font-size: 13px;
}}

.dot-on {{
    color: {Colors.SUCCESS};
    font-weight: 800;
}}

.dot-off {{
    color: {text_tertiary};
}}

button.io-toggle {{
    min-width: 54px;
    min-height: 30px;
    padding: 0 10px;
    border-radius: {Radius.SM}px;
    background: {bg_primary};
    color: {text_secondary};
    border-color: {border};
    font-size: 12px;
    font-weight: 600;
}}

button.io-toggle:checked {{
    background: {Colors.SUCCESS};
    color: #FFFFFF;
    border-color: {Colors.SUCCESS};
}}

/* ─── Parameter Items ──────────────────────────────────────────────────── */

.param-item {{
    background: {bg_group};
    border: 1px solid {border_light};
    border-radius: {Radius.SM}px;
    padding: 12px 14px;
    min-height: 70px;
}}

.param-item label {{
    color: {text_secondary};
    font-size: {FontSize.XS}px;
    font-weight: 600;
}}

.param-item entry {{
    background: {bg_input};
    min-height: 32px;
    font-size: 14px;
}}

.param-dirty {{
    border-color: {Colors.WARNING};
}}

/* ─── Groups ───────────────────────────────────────────────────────────── */

.group {{
    background: {bg_group};
    border: 1px solid {border_light};
    border-radius: {Radius.MD}px;
    padding: 14px 16px;
}}

.group-title {{
    color: {text_secondary};
    font-weight: 600;
    font-size: {FontSize.XS}px;
    letter-spacing: 0.5px;
    margin-bottom: 10px;
}}

/* ─── Status Bar ───────────────────────────────────────────────────────── */

.statusbar {{
    background: {bg_card};
    border-top: 1px solid {border};
    padding: 8px 20px;
    font-size: {FontSize.XS}px;
    color: {text_secondary};
}}

/* ─── Logs ─────────────────────────────────────────────────────────────── */

.log {{
    background: {bg_primary};
    color: {Colors.SUCCESS};
    padding: 12px;
    font-size: 12px;
    font-family: "SF Mono", "JetBrains Mono", "Fira Code", monospace;
    border-radius: {Radius.SM}px;
    border: 1px solid {border_light};
}}

/* ─── Chart ────────────────────────────────────────────────────────────── */

.chart {{
    background: {bg_primary};
    border: 1px solid {border_light};
    border-radius: {Radius.MD}px;
}}

/* ─── Pending / NACK ───────────────────────────────────────────────────── */

.pending {{
    color: {Colors.WARNING};
    font-weight: 600;
    font-size: 14px;
    background: rgba(255, 159, 10, 0.08);
    border-radius: {Radius.SM}px;
    padding: 10px 14px;
    border-left: 3px solid {Colors.WARNING};
}}

.nack {{
    color: {Colors.ERROR};
    font-weight: 600;
    font-size: 14px;
    background: rgba(255, 59, 48, 0.08);
    border-radius: {Radius.SM}px;
    padding: 10px 14px;
    border-left: 3px solid {Colors.ERROR};
}}

.info-value {{
    color: {text_primary};
    font-weight: 600;
    font-size: {FontSize.BASE}px;
}}

.hint {{
    color: {text_secondary};
    font-size: {FontSize.SM}px;
}}

/* ─── Upgrade ──────────────────────────────────────────────────────────── */

.upgrade-card {{
    padding: 20px;
}}

.upgrade-row {{
    margin-bottom: 8px;
}}

.status-line {{
    color: {text_secondary};
    background: {bg_group};
    border: 1px solid {border_light};
    border-radius: {Radius.MD}px;
    padding: 12px 16px;
    font-size: 14px;
}}

/* ─── Self-test ────────────────────────────────────────────────────────── */

.selftest-pass {{
    background: rgba(52, 199, 89, 0.12);
    color: {Colors.SUCCESS};
    border: 1px solid rgba(52, 199, 89, 0.3);
    border-radius: {Radius.SM}px;
    padding: 6px 12px;
    margin: 3px;
    font-size: 13px;
    font-weight: 500;
}}

.selftest-fail {{
    background: rgba(255, 59, 48, 0.12);
    color: {Colors.ERROR};
    border: 1px solid rgba(255, 59, 48, 0.3);
    border-radius: {Radius.SM}px;
    padding: 6px 12px;
    margin: 3px;
    font-size: 13px;
    font-weight: 500;
}}

/* ─── Output Item ──────────────────────────────────────────────────────── */

.output-item {{
    background: {bg_group};
    border: 1px solid {border_light};
    border-radius: {Radius.SM}px;
    padding: 10px 14px;
    min-height: 44px;
}}

/* ─── Scrollbar ────────────────────────────────────────────────────────── */

scrollbar {{
    background: transparent;
}}

scrollbar slider {{
    background: {border};
    border-radius: 4px;
    min-width: 6px;
    min-height: 6px;
}}

scrollbar slider:hover {{
    background: {text_tertiary};
}}

/* ─── List ─────────────────────────────────────────────────────────────── */

list, row {{
    background: {bg_card};
    color: {text_primary};
}}

/* ─── Tooltip ──────────────────────────────────────────────────────────── */

tooltip {{
    background: {bg_card};
    color: {text_primary};
    border: 1px solid {border};
    border-radius: {Radius.SM}px;
    padding: 8px 12px;
    box-shadow: 0 4px 12px rgba(0,0,0,0.1);
    font-size: 13px;
}}

/* ─── Flow Control ─────────────────────────────────────────────────────── */

.flow-card {{
    padding: 16px 20px;
}}

.flow-group {{
    min-height: 130px;
}}

/* ─── Interaction Request Card ─────────────────────────────────────────── */

.interaction-card {{
    background: rgba(255, 159, 10, 0.06);
    border: 1px solid rgba(255, 159, 10, 0.2);
    border-radius: {Radius.MD}px;
    padding: 16px 20px;
}}

.interaction-card-title {{
    font-size: {FontSize.LG}px;
    font-weight: 600;
    color: {Colors.WARNING};
}}

/* ─── Status Bar Items ─────────────────────────────────────────────────── */

.statusbar-item {{
    color: {text_secondary};
    font-size: {FontSize.XS}px;
    font-weight: 500;
}}

.statusbar-sep {{
    color: {text_tertiary};
    font-size: {FontSize.XS}px;
}}

/* ─── Nav Dot ────────────────────────────────────────────────────────────── */

.nav-dot {{
    font-size: 8px;
    color: {text_tertiary};
}}

.nav-active .nav-dot {{
    color: #FFFFFF;
}}

/* ─── Metric Value Fault ─────────────────────────────────────────────────── */

.metric-value-fault {{
    color: {Colors.ERROR};
}}

/* ─── Status Error/Success ───────────────────────────────────────────────── */

.status-error {{
    color: {Colors.ERROR};
}}

.status-success {{
    color: {Colors.SUCCESS};
}}

/* ─── Badge Success (for statusbar) ──────────────────────────────────────── */

.badge-success {{
    color: {Colors.SUCCESS};
}}

/* ─── Control Page Separators ─────────────────────────────────────────────── */

.ctrl-sep {{
    min-width: 1px;
    background: {border_light};
    margin: 0 4px;
}}

.ctrl-info-sep {{
    min-height: 1px;
    background: {border_light};
    margin: 4px 0;
}}
"""

    return css.encode("utf-8")
