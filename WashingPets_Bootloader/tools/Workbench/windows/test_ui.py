"""Regression checks for the Linux-compatible Qt views, without serial hardware."""
import copy
import os
import struct
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
from PySide6.QtWidgets import QApplication, QPushButton
from PySide6.QtGui import QImage
import server as core
from ui.i18n import set_lang, tr, process_name
from windows.main import WorkbenchWindow
from windows.pages import IOPage, ControlPage, ParamsPage, TestPage as RecordingPage
from windows.theme import STYLE, ASSETS


class Device:
    def __init__(self):
        self.lock = threading.RLock()
        self.running = True
        self.pending_interaction = None
        self.samples = []
        self.sample_interval = 30
        self.test_running = False
        self.sent = []
        self.status = {'connected': True, 'port': 'COM7',
                       'metrics': {'work_mode': 1, 'device_state': 0, 'process_type': 3,
                                   'stage': 6, 'recipe': 2, 'step': 4, 'progress': 500},
                       'hardwareSession': 1,
                       'hardware': {'fan_pwm': 0, 'temperature_mask': 15,
                                    'lower_door_bypass': 0, 'host_command_bypass': 0,
                                    'booster_mode': 0, 'revision': 3, 'save_pending': 0, 'busy': 0},
                       'telemetry_ui': {'cabin_temp': 25.5},
                       'process_time': {'total_remaining_ms': 123000, 'stage_remaining_ms': 45000},
                       'io_bools': {'inputs': [True] * 21, 'outputs': [False] * 45}}

    def snapshot(self, **kwargs):
        return dict(copy.deepcopy(self.status), samples=list(self.samples), testRunning=self.test_running)

    def send(self, cmd, payload):
        self.sent.append((cmd, payload))

    def set_mode_wait(self, mode):
        self.sent.append(('mode', mode))

    def close(self):
        self.running = False

    def add_log(self, line):
        pass


class UiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])
        cls.app.setStyle('Fusion')
        cls.app.setStyleSheet(STYLE)

    def setUp(self):
        set_lang('zh')
        self.device = Device()
        self.widgets = []

    def tearDown(self):
        for widget in self.widgets:
            widget.close()
            widget.deleteLater()
        self.app.processEvents()
        set_lang('zh')

    def window(self):
        with patch('windows.main.core.STATE', self.device), patch('windows.main.port_names', return_value=['COM7']), patch('windows.upgrade_page.port_names', return_value=['COM7']):
            window = WorkbenchWindow()
        window.timer.stop()
        window.poll_timer.stop()
        window._background = lambda op: op()
        self.widgets.append(window)
        return window

    def test_all_pages_render_both_languages_and_resize(self):
        w = self.window()
        w.show()
        for language in range(2):
            for width, height in [(1400, 900), (1280, 800)]:
                w.resize(width, height)
                for i, key in enumerate(w.PAGE_KEYS):
                    w.show_page(i)
                    w.refresh()
                    for _ in range(10):
                        self.app.processEvents()
                    self.assertFalse(w.grab().isNull(), key)
                    self.assertLessEqual(w.width(), width, key)
            w.toggle_language()
        self.assertTrue((ASSETS / 'arrow.svg').is_file())
        self.assertTrue((ASSETS / 'check.svg').is_file())

    def test_io_refresh_never_sends_commands_and_toggle_uses_output_id(self):
        sent = []
        page = IOPage(lambda action, **kw: sent.append((action, kw)))
        self.widgets.append(page)
        state = self.device.snapshot()
        state['io_bools']['outputs'][39] = True
        page.update_state(state)
        self.assertEqual(sent, [])
        self.assertEqual(page.outputs[39].text(), 'ON')
        page.outputs[39].click()
        self.assertEqual(sent, [('output', {'output_id': 40, 'state': False})])
        state['io_bools']['outputs'][44] = True
        page.update_state(state)
        self.assertEqual(len(page.outputs), 45)
        self.assertEqual(len(page.inputs), 21)
        page.outputs[44].click()
        self.assertEqual(sent[-1], ('output', {'output_id': 45, 'state': False}))
        self.assertIn('MAGLOCK6', core.OUTPUT_NAMES[44])
        page.station.setCurrentIndex(0)
        page.value.setText('2.3')
        page.set_rs485()
        self.assertEqual(sent[-1], ('rs485', {'station': 0, 'value': 23}))
        count = len(sent)
        page.value.setText('10.1')
        page.set_rs485()
        self.assertEqual(len(sent), count)

    def test_control_actions_and_reported_process_type(self):
        w = self.window()
        page = w.pages['control']
        page.update_state(self.device.snapshot())
        self.assertEqual(page.info[0].text(), process_name(3))
        def click(key):
            buttons = [b for b in page.findChildren(QPushButton) if b.text() == tr(key)]
            self.assertEqual(len(buttons), 1, key)
            buttons[0].click()
        for key, value in [('ctrl.start', 1), ('ctrl.pause', 2), ('ctrl.continue', 3), ('ctrl.end', 4), ('ctrl.self_clean', 6), ('ctrl.fault_reset', 7)]:
            click(key)
            self.assertEqual(self.device.sent[-1], (core.CMD_PROCESS, core.process_payload(value, 0)))
        page.stage.setCurrentIndex(6)
        click('ctrl.stage_test')
        self.assertEqual(self.device.sent[-1], (core.CMD_PROCESS, core.process_payload(5, 6)))
        for key, kind in [('ctrl.open_top', 1), ('ctrl.close_top', 4), ('ctrl.open_bottom', 2), ('ctrl.close_bottom', 3), ('ctrl.auto_close', 7)]:
            self.device.pending_interaction = {'id': 12}
            click(key)
            self.assertEqual(self.device.sent[-1], (core.CMD_INTERACTION, core.interaction_payload(12, kind, 1, 0)))

    def test_hardware_refresh_preserves_unsaved_choices_and_shows_device_risks(self):
        page = ParamsPage(lambda *a, **kw: None, self.device)
        self.widgets.append(page)
        state = self.device.snapshot()
        page.update_state(state)
        self.assertTrue(page.hw_apply.isEnabled())
        page.lower.setChecked(True)
        page.pump.setCurrentIndex(1)
        self.assertTrue(page.hw_dirty)
        state['hardware']['revision'] = 4
        page.update_state(state)
        self.assertTrue(page.lower.isChecked())
        self.assertEqual(page.hw_values()['booster_mode'], 1)
        state['hardware']['temperature_mask'] = 7
        state['hardware']['host_command_bypass'] = 1
        page.update_state(state)
        self.assertIn('CH4', page.hw_warning.text())
        self.assertIn('上位机', page.hw_warning.text())
        state['hardwareSession'] = 2
        page.update_state(state)
        self.assertFalse(page.lower.isChecked())
        self.assertEqual(page.pump.currentIndex(), 0)
        self.assertFalse(page.hw_dirty)
        state['metrics']['work_mode'] = 2
        page.update_state(state)
        self.assertFalse(page.hw_apply.isEnabled())

    def test_language_switch_preserves_pending_forms_and_upgrade_log(self):
        w = self.window()
        w.refresh()
        p = w.pages['params']
        p.update_state(self.device.snapshot())
        key = next(iter(p.entries))
        p.entries[key].setText('37.5')
        p.dirty.add(key)
        p.lower.setChecked(True)
        p.pump.setCurrentIndex(1)
        p.recipe.setCurrentIndex(5)
        w.pages['test'].interval.setText('12')
        w.pages['test'].series_checks['fan'].setChecked(True)
        up = w.pages['upgrade']
        up.file.setText('C:/固件/App.bin')
        up.profile.setCurrentText('Store')
        up.append_log('ACK 1024')
        up.set_progress(42, 'Uploading')
        w.toggle_language()
        p = w.pages['params']
        p.update_state(self.device.snapshot())
        self.assertEqual(p.entries[key].text(), '37.5')
        self.assertIn(key, p.dirty)
        self.assertTrue(p.lower.isChecked())
        self.assertEqual(p.pump.currentIndex(), 1)
        self.assertTrue(p.hw_dirty)
        self.assertEqual(p.recipe.currentIndex(), 5)
        self.assertEqual(w.pages['test'].interval.text(), '12')
        self.assertTrue(w.pages['test'].series_checks['fan'].isChecked())
        self.assertEqual(w.pages['upgrade'].file.text(), 'C:/固件/App.bin')
        self.assertEqual(w.pages['upgrade'].profile.currentText(), 'Store')
        self.assertEqual(w.pages['upgrade'].log.toPlainText(), 'ACK 1024')
        self.assertEqual(w.pages['upgrade'].progress.value(), 42)

    def test_recording_export_uses_latest_samples_and_reports_rejection(self):
        sent = []
        page = RecordingPage(lambda action, **kw: sent.append((action, kw)), self.device)
        self.widgets.append(page)
        page.resize(1000, 900)
        page.show()
        self.app.processEvents()
        page.start_test()
        self.assertTrue(self.device.test_running)
        self.assertEqual(sent[-1], ('process', {'value': 5, 'param': 6}))
        state = self.device.snapshot()
        state['last_nack'] = {'request_cmd': core.CMD_PROCESS, 'error': 2, 'detail': 3, 'text': 'Door open'}
        page.update_state(state)
        self.assertFalse(self.device.test_running)
        self.assertEqual(page.status.text(), 'Door open')
        self.device.samples = [dict(time='12:00:00', seconds=0, cabinTemp=25, cabinHumidity=50,
                                    cabinOxygen=20, cabinOzone=0, fan=30, heater=40),
                               dict(time='12:00:01', seconds=1, cabinTemp=26, cabinHumidity=51,
                                    cabinOxygen=20, cabinOzone=0, fan=30, heater=40)]
        with tempfile.TemporaryDirectory() as folder, patch('windows.main.core.STATE', self.device), patch('windows.main.desktop_dir', return_value=Path(folder)):
            page.stop_test()
            csv = next(Path(folder).rglob('*.csv'))
            self.assertIn('26', csv.read_text(encoding='utf-8-sig'))
            self.assertFalse(QImage(str(csv.with_suffix('.png'))).isNull())
            self.assertEqual(page.curve.samples, self.device.samples)
            self.assertFalse(self.device.test_running)


if __name__ == '__main__':
    unittest.main()
