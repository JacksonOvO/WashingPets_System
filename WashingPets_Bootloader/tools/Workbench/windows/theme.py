"""Original PawClean palette and component geometry, expressed as Qt CSS."""
from ui.theme import Colors

STYLE = """
QWidget { color: #F5F5F7; font-family: 'Noto Sans', 'Microsoft YaHei', 'Segoe UI'; font-size: 15px; }
QMainWindow, QScrollArea, QStackedWidget { background: #1C1C1E; }
QScrollArea { border: none; }
QWidget#pageContent { background: #1C1C1E; }
QLabel { background: transparent; }
#sidebar { background: #1C1C1E; border-right: 1px solid #38383A; }
#brand { font-size: 18px; font-weight: 700; }
#subtitle, #sectionTitle, #tileTitle { font-size: 11px; color: #98989D; }
#sectionTitle { color: #636366; font-weight: 600; }
#meta { color: #98989D; font-size: 13px; }
#nav { font-size: 14px; text-align: left; border: none; border-radius: 6px; padding: 9px 12px; min-height: 36px; background: transparent; color: #F5F5F7; }
#nav:hover { background: #2C2C2E; }
#nav[active="true"] { background: #0071E3; color: white; font-weight: 600; }
#header { background: #2C2C2E; border-bottom: 1px solid #38383A; }
#connection { background: #3A3A3C; color: #98989D; border-radius: 6px; padding: 8px 14px; font-size: 13px; font-weight: 600; }
#connection[connected="true"] { background: rgba(52,199,89,30); color: #34C759; }
#statusbarFrame { background: #2C2C2E; border-top: 1px solid #38383A; }
#statusValue, #statusSeparator { font-size: 11px; color: #98989D; }
QFrame#card, QFrame#sensor, QFrame#heroCard { background: #2C2C2E; border: 1px solid #48484A; border-radius: 10px; }
QFrame#heroCard { border-radius: 14px; }
QFrame#metric, QFrame#subgroup, QFrame#parameter { background: #3A3A3C; border: 1px solid #48484A; border-radius: 10px; }
QFrame#actuator, QFrame#outputItem, QFrame#inputItem { background: #3A3A3C; border: 1px solid #48484A; border-radius: 6px; }
#cardTitle { font-size: 17px; font-weight: 600; }
#hero { font-size: 34px; font-weight: 700; }
#sensorValue { font-size: 28px; font-weight: 700; }
#metricValue { font-size: 22px; font-weight: 700; }
#actuatorValue { font-size: 17px; font-weight: 700; }
QPushButton { font-size: 14px; background: #3A3A3C; border: 1px solid #38383A; border-radius: 6px; min-height: 36px; padding: 0 16px; }
QPushButton:hover { background: #48484A; }
QPushButton:pressed { background: #636366; }
QPushButton:disabled, QLineEdit:disabled, QComboBox:disabled, QCheckBox:disabled { color: #636366; }
QPushButton[role="primary"] { background: #0071E3; border-color: #0071E3; color: white; }
QPushButton[role="primary"]:hover { background: #0077ED; }
QPushButton[role="start"] { background: rgba(52,199,89,25); border-color: rgba(52,199,89,65); }
QPushButton[role="stop"] { background: rgba(255,59,48,20); border-color: rgba(255,59,48,60); }
QPushButton[role="controlStart"] { background: rgba(0,113,227,18); border-color: rgba(0,113,227,60); }
#segmented { background: #3A3A3C; border-radius: 6px; }
#segmented QPushButton { border: none; background: transparent; min-height: 32px; padding: 0 8px; }
QLineEdit, QComboBox { font-size: 14px; background: #1C1C1E; border: 1px solid #48484A; border-radius: 6px; min-height: 36px; padding: 0 12px; selection-background-color: #0071E3; }
QLineEdit:focus, QComboBox:focus { border-color: #0071E3; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: url(ARROW_ICON); width: 12px; height: 8px; }
QComboBox QAbstractItemView { background: #2C2C2E; selection-background-color: #0071E3; }
QCheckBox { spacing: 6px; }
QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #48484A; border-radius: 3px; background: #3A3A3C; }
QCheckBox::indicator:checked { background: #0071E3; border-color: #5AC8FA; image: url(CHECK_ICON); }
QPushButton#outputToggle { background: #1C1C1E; color: #98989D; min-width: 34px; min-height: 30px; padding: 0 10px; }
QPushButton#outputToggle:checked { background: #34C759; color: white; }
QPlainTextEdit { background: #1C1C1E; color: #34C759; border: 1px solid #48484A; border-radius: 6px; font-family: Consolas, monospace; font-size: 12px; padding: 12px; }
QProgressBar { background: #1C1C1E; border: 1px solid #48484A; border-radius: 4px; text-align: center; min-height: 18px; }
QProgressBar::chunk { background: #0071E3; border-radius: 3px; }
QProgressBar#powerBar { border: none; min-height: 0; }
#pending, #nack { background: rgba(255,149,0,25); color: #FF9500; border-left: 3px solid #FF9500; padding: 10px 14px; }
#nack { background: rgba(255,59,48,25); color: #FF3B30; border-color: #FF3B30; }
#statusLine { background: #3A3A3C; border: 1px solid #48484A; border-radius: 10px; padding: 12px 16px; color: #98989D; }
#faultCode { font-size: 28px; font-family: Consolas, monospace; font-weight: bold; }
#badge { border-radius: 6px; padding: 6px 12px; background: rgba(52,199,89,25); color: #34C759; }
#badge[failed="true"] { background: rgba(255,59,48,25); color: #FF3B30; }
QScrollBar:vertical { background: transparent; width: 6px; }
QScrollBar::handle:vertical { background: #48484A; border-radius: 3px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QMessageBox, QFileDialog { background: #2C2C2E; }
"""
# Keep the key backgrounds tied to the GTK design tokens.
STYLE = STYLE.replace('#1C1C1E', Colors.DARK_BG_PRIMARY).replace('#2C2C2E', Colors.DARK_BG_CARD)

from pathlib import Path
ASSETS = Path(__file__).resolve().parent / 'assets'
STYLE = STYLE.replace('CHECK_ICON', (ASSETS / 'check.svg').as_posix()).replace('ARROW_ICON', (ASSETS / 'arrow.svg').as_posix())
