"""Optional native widget integration tests. Core/security tests require no Qt."""
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
try:
    from PySide6.QtWidgets import QApplication, QComboBox
    from launcher.ui import Window
    from launcher.setup_ui import SetupWindow
    QT = True
except ImportError:
    QT = False
from test_engine import fixture, release_for
from launcher.hardware import Hardware
from launcher.storage import read_json, write_json
from launcher.setup import bootstrap_name
from launcher.manifest import platform_id


@unittest.skipUnless(QT, 'PySide6 is optional for headless engine tests')
class UI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.directory = root / 'game'
        fixture(self.directory)
        self.data = root / 'user'
        self.data.mkdir()
        with patch.object(Window, 'start_background', lambda self: None):
            self.window = Window(self.directory, self.data, self.data / 'logs', {'version': '0.3.0'})
            self.window.show()
            self.app.processEvents()

    def tearDown(self):
        self.window.close()
        self.app.processEvents()
        self.temp.cleanup()

    def test_all_sections_render_and_aircraft_discovered(self):
        self.assertEqual(self.window.stack.count(), 10)
        self.assertEqual(self.window.aircraft_combo.count(), 2)
        for index in range(10):
            self.window.navigation.setCurrentRow(index)
            self.app.processEvents()
            self.assertEqual(self.window.stack.currentIndex(), index)
        self.assertTrue(self.window.play_button.isEnabled())

    def test_available_update_is_offered_on_play_page(self):
        self.assertFalse(self.window.update_button.isVisible())
        self.window.installation.managed = True
        self.window.release = release_for(fixture(Path(self.temp.name) / 'next', '0.4.0'))
        self.window.refresh_summary()
        self.assertTrue(self.window.update_button.isVisible())
        self.assertEqual(self.window.update_button.text(), 'Update to 0.4.0')

    def test_presets_and_custom_values_persist(self):
        self.window.preset_combo.setCurrentText('Low')
        self.assertEqual(self.window.graphics.values['shadows'], '0')
        self.window.graphic_widgets['width'].setValue(1600)
        self.assertEqual(self.window.prefs.preset, 'Custom')
        self.assertIn('width=1600', (self.data / 'graphics.cfg').read_text())
        self.assertEqual(read_json(self.data / 'launcher.json')['preset'], 'Custom')

    def test_auto_recommendation_and_selected_aircraft(self):
        self.window.hardware = Hardware(cores=8, ram_gib=16, vram_gib=0)
        self.window.preset_combo.setCurrentText('Auto / Recommended')
        self.assertEqual(self.window.graphics.values['msaa'], '2')
        self.window.aircraft_combo.setCurrentIndex(1)
        self.assertEqual(self.window.prefs.aircraft, 'typhoon')
        self.assertIn('Eurofighter', self.window.aircraft_label.text())
        self.window.mode_combo.setCurrentIndex(2)
        self.assertTrue(self.window.bots.isEnabled())
        self.assertFalse(self.window.airborne.isEnabled())

    def test_async_worker_completes_without_blocking(self):
        results = []
        self.window.start_job('Test operation', lambda c, p: (p(1, 2, 1024), 'complete')[1], results.append)
        deadline = time.monotonic() + 3
        while self.window.job and time.monotonic() < deadline:
            self.app.processEvents()
            time.sleep(.01)
        self.assertEqual(results, ['complete'])
        self.assertIsNone(self.window.job)
        self.assertFalse(self.window.progress.isVisible())

    def test_play_hands_real_settings_to_session(self):
        self.window.aircraft_combo.setCurrentIndex(1)
        with patch.object(self.window.session, 'start') as start:
            self.window.play()
            args = start.call_args.args
            self.assertEqual(args[1].aircraft, 'typhoon')
            self.assertEqual(args[2].path, self.data / 'graphics.cfg')


@unittest.skipUnless(QT, 'PySide6 is optional for headless engine tests')
class SetupUI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.game = self.root / 'installed game'
        fixture(self.game / 'releases/initial')
        write_json(self.game / 'current.json', {'schema': 1, 'active': 'releases/initial', 'previous': None})
        self.bootstrap = self.root / bootstrap_name()
        self.bootstrap.write_bytes(b'bootstrap')
        settings = {'schema': 1, 'version': '0.3.0', 'platform': platform_id(),
                    'channel': 'stable', 'manifest_url': 'https://updates.example.org/manifest.json'}
        self.window = SetupWindow(settings, self.bootstrap, self.root / 'user', root=self.game)
        self.window.show()
        self.app.processEvents()

    def tearDown(self):
        self.window.close()
        self.app.processEvents()
        self.temp.cleanup()

    def wait_job(self):
        deadline = time.monotonic() + 3
        while self.window.job and time.monotonic() < deadline:
            self.app.processEvents()
            time.sleep(.01)
        self.assertIsNone(self.window.job)

    def test_open_installed_game_uses_persistent_bootstrap(self):
        self.assertEqual(self.window.action.text(), 'OPEN LAUNCHER')
        with patch('launcher.setup_ui.spawn') as spawn, patch('launcher.setup_ui.install_shortcut') as shortcut:
            self.window.action.click()
            self.wait_job()
        self.assertEqual(spawn.call_args.args[0][0], str(self.game / bootstrap_name()))
        self.assertEqual(shortcut.call_args.args[0], self.game / bootstrap_name())
        self.assertEqual(read_json(self.root / 'user/setup.json')['stable'], str(self.game))
        self.assertFalse(self.window.isVisible())

    def test_failure_offers_retry_without_starting_game(self):
        self.window.settings['channel'] = 'development'
        with patch('launcher.setup_ui.spawn') as spawn:
            self.window.action.click()
            self.wait_job()
            spawn.assert_not_called()
        self.assertIn('separate folder', self.window.status.text())
        self.assertEqual(self.window.action.text(), 'RETRY INSTALLATION')
        self.assertTrue(self.window.path.isEnabled())

    def test_close_cancels_worker_before_destroying_window(self):
        from launcher.download import Cancelled
        def wait_for_cancel(root, settings, bootstrap, cancel, progress, status):
            cancel.wait(2)
            raise Cancelled('Cancelled')
        with patch('launcher.setup_ui.install', side_effect=wait_for_cancel), patch('launcher.setup_ui.spawn') as spawn:
            self.window.action.click()
            self.assertFalse(self.window.path.isEnabled())
            self.window.close()
            self.wait_job()
            spawn.assert_not_called()
        self.assertFalse(self.window.isVisible())


if __name__ == '__main__':
    unittest.main()
