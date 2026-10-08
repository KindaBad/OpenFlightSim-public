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
from launcher.config import Preferences
from launcher.setup import bootstrap_name
from launcher.manifest import platform_id


@unittest.skipUnless(QT, 'PySide6 is optional for headless engine tests')
class AircraftSummary(unittest.TestCase):
    def test_data_sheet_uses_catalogue_and_published_figures(self):
        from launcher.ui import aircraft_summary
        fighter = {'manufacturer': 'PAC / CAC', 'type': 'Fighter', 'role': 'Lightweight multirole', 'engines': 1,
                   'engine_type': 'RD-93 afterburning turbofan', 'thrust_dry_kn': 49.4, 'thrust_reheat_kn': 84.4,
                   'empty_mass_kg': 6586, 'fuel_capacity_kg': 2330, 'span_m': 9.44, 'wing_area_m2': 24.43,
                   'max_speed': 'Mach 1.6 at altitude', 'armament': '23 mm GSh-23-2, 200 rounds', 'armed': True}
        text = aircraft_summary(fighter)
        self.assertIn('1 engine · RD-93', text)
        self.assertIn('Thrust: 49.4 kN dry · 84.4 kN with reheat', text)
        self.assertIn('Empty: 6,586 kg · internal fuel: 2,330 kg', text)
        self.assertIn('Top speed: Mach 1.6 at altitude', text)
        self.assertIn('Armament: 23 mm GSh-23-2, 200 rounds', text)

    def test_older_catalogue_and_unarmed_aircraft(self):
        from launcher.ui import aircraft_summary
        # A catalogue from before 0.5.7 has no thrust or mass breakdown.
        text = aircraft_summary({'engines': 2, 'span_m': 35.8, 'reference_mass_kg': 64000, 'armed': False,
                                 'fuel_capacity_kg': 0, 'thrust_reheat_kn': 0, 'armament': 'ignored'})
        self.assertIn('2 engines · Engine type not provided', text)
        self.assertIn('Reference mass: 64000 kg', text)
        self.assertNotIn('Thrust', text)
        self.assertNotIn('Armament', text)
        self.assertTrue(text.endswith('Unarmed aircraft'))


@unittest.skipUnless(QT, 'PySide6 is optional for headless engine tests')
class UI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name).resolve()
        self.directory = root / 'game'
        fixture(self.directory)
        self.data = root / 'user'
        self.data.mkdir()
        # No test asks this computer's real firewall anything.
        self.firewall = patch('launcher.ui.lan.firewall_advice', return_value=None)
        self.firewall_advice = self.firewall.start()
        with patch.object(Window, 'start_background', lambda self: None):
            self.window = Window(self.directory, self.data, self.data / 'logs', {'version': '0.3.0'})
            self.window.show()
            self.app.processEvents()

    def tearDown(self):
        self.window.close()
        self.app.processEvents()
        self.firewall.stop()
        self.temp.cleanup()

    def test_all_sections_render_and_aircraft_discovered(self):
        self.assertEqual(self.window.stack.count(), 11)
        self.assertEqual(self.window.aircraft_combo.count(), 2)
        for row, index in enumerate((0, 1, 10, 2, 8, 9)):
            self.window.navigation.setCurrentRow(row)
            self.app.processEvents()
            self.assertEqual(self.window.stack.currentIndex(), index)
        for index, tab in self.window.tab_buttons.items():
            self.window.show_page(2)
            tab.click()
            self.app.processEvents()
            self.assertEqual(self.window.stack.currentIndex(), index)
            self.assertEqual(self.window.navigation.currentRow(), 3)
        self.window.show_page(0)
        self.assertTrue(self.window.play_button.isEnabled())

    def test_home_cards_select_and_persist_actual_catalogue_aircraft(self):
        self.assertEqual(set(self.window.home_aircraft.cards), {'a320', 'typhoon'})
        self.window.home_aircraft.cards['typhoon'].click()
        self.assertEqual(self.window.prefs.aircraft, 'typhoon')
        self.assertEqual(self.window.aircraft_combo.currentData(), 'typhoon')
        self.assertTrue(self.window.hangar_aircraft.cards['typhoon'].isChecked())
        self.assertFalse(self.window.home_aircraft.cards['a320'].isChecked())
        self.assertEqual(read_json(self.data / 'launcher.json')['aircraft'], 'typhoon')
        self.window.show_page(1)
        self.window.hangar_aircraft.cards['a320'].click()
        self.assertEqual(self.window.prefs.aircraft, 'a320')
        self.assertTrue(self.window.home_aircraft.cards['a320'].isChecked())

    def test_home_graphics_and_detail_pages_stay_in_sync(self):
        self.window.home_preset.setCurrentText('Ultra')
        self.assertEqual(self.window.preset_combo.currentText(), 'Ultra')
        self.assertEqual(self.window.home_graphics['msaa'].currentData(), 8)
        self.window.home_preset.setCurrentText('Custom')
        self.assertEqual(self.window.preset_combo.currentText(), 'Custom')
        self.assertEqual(read_json(self.data / 'launcher.json')['preset'], 'Custom')
        self.window.home_graphics['msaa'].setCurrentIndex(1)
        self.assertEqual(self.window.graphic_widgets['msaa'].currentData(), 2)
        self.assertEqual(self.window.preset_combo.currentText(), 'Custom')
        self.assertEqual(self.window.home_preset.currentText(), 'Custom')
        self.window.graphic_widgets['width'].setValue(1600)
        self.window.graphic_widgets['height'].setValue(900)
        self.assertEqual(self.window.home_resolution.currentData(), (1600, 900))
        self.window.home_graphics['vsync'].setCurrentIndex(1)
        self.assertFalse(self.window.graphic_widgets['vsync'].isChecked())
        config = (self.data / 'graphics.cfg').read_text()
        for expected in ('width=1600', 'height=900', 'msaa=2', 'vsync=0'):
            self.assertIn(expected, config)

    def test_existing_settings_load_without_migration_or_reset(self):
        self.window.close()
        Preferences(installation=str(self.directory), aircraft='typhoon', mode='dogfight',
                    camera='orbit', name='Returning pilot', preset='High',
                    manifest_url='https://updates.example.org/manifest.json',
                    auto_check=False, auto_install=True).save(self.data / 'launcher.json')
        (self.data / 'graphics.cfg').write_text('width=1712\nheight=964\nmsaa=8\nvsync=0\nfutureSetting=keep\n')
        with patch.object(Window, 'start_background', lambda self: None):
            self.window = Window(None, self.data, self.data / 'logs', {'version': '0.4.2'})
            self.window.show()
            self.app.processEvents()
        self.assertEqual(self.window.prefs.aircraft, 'typhoon')
        self.assertEqual(self.window.prefs.mode, 'dogfight')
        self.assertEqual(self.window.prefs.name, 'Returning pilot')
        self.assertTrue(self.window.prefs.auto_install)
        self.assertEqual(self.window.home_preset.currentText(), 'High')
        self.assertEqual(self.window.home_resolution.currentData(), (1712, 964))
        self.assertEqual(self.window.home_graphics['msaa'].currentData(), 8)
        self.window.home_graphics['vsync'].setCurrentIndex(0)
        self.assertIn('futureSetting=keep', (self.data / 'graphics.cfg').read_text())

    def test_busy_home_controls_cannot_change_the_active_flight(self):
        with patch.object(self.window.session, 'running', return_value=True):
            self.window.refresh_summary()
            self.assertFalse(self.window.home_aircraft.isEnabled())
            self.assertFalse(self.window.quick_settings.isEnabled())
            self.assertFalse(self.window.play_button.isEnabled())
            self.window.select_card('typhoon')
            self.assertEqual(self.window.prefs.aircraft, 'a320')
        self.window.refresh_summary()
        self.assertTrue(self.window.home_aircraft.isEnabled())
        self.assertTrue(self.window.quick_settings.isEnabled())

    def test_download_bar_reports_progress_and_safe_cancellation(self):
        import threading
        finished = threading.Event()
        def work(cancel, progress):
            progress(1024**2, 4 * 1024**2, 1024**2)
            cancel.wait(2)
            finished.set()
        self.window.start_job('Downloading update', work, lambda result: None)
        deadline = time.monotonic() + 1
        while self.window.progress.value() != 25 and time.monotonic() < deadline:
            self.app.processEvents()
            time.sleep(.01)
        self.assertEqual(self.window.operation_label.text(), 'Downloading update')
        self.assertEqual(self.window.progress.value(), 25)
        self.assertIn('(25%)', self.window.transfer_label.text())
        self.assertIn('remaining', self.window.status.text())
        self.assertFalse(self.window.quick_settings.isEnabled())
        self.window.cancel_button.click()
        deadline = time.monotonic() + 2
        while self.window.job and time.monotonic() < deadline:
            self.app.processEvents()
            time.sleep(.01)
        self.assertTrue(finished.is_set())
        self.assertIsNone(self.window.job)
        self.assertFalse(self.window.progress.isVisible())
        self.assertTrue(self.window.quick_settings.isEnabled())

    def test_available_update_is_offered_on_play_page(self):
        self.assertFalse(self.window.update_button.isVisible())
        self.window.installation.managed = True
        self.window.release = release_for(fixture(Path(self.temp.name).resolve() / 'next', '0.4.0'))
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

    def lobby(self, **changes):
        from launcher.lan import Lobby
        return Lobby(**{**dict(name='Friday night', address='192.168.1.20', port=27025, protocol=15, players=1,
                               max_players=16, bots=2, version='0.5.0'), **changes})

    def test_multiplayer_page_lists_games_and_joins_the_selected_one(self):
        self.window.installation.catalog['protocol'] = 15
        self.window.show_page(10)
        self.assertEqual(self.window.navigation.currentRow(), 2)
        self.assertFalse(self.window.join_button.isEnabled())
        self.window.show_lobbies([self.lobby(name='Old build', address='192.168.1.9', protocol=14), self.lobby()], ['192.168.1.23'])
        self.assertEqual(self.window.lobby_list.count(), 2)
        self.assertIn('different game version', self.window.lobby_list.item(0).text())
        # The first game that can be joined is selected; the incompatible one cannot be.
        self.assertEqual(self.window.selected_lobby().name, 'Friday night')
        self.assertTrue(self.window.join_button.isEnabled())
        self.assertIn('192.168.1.23', self.window.lan_address.text())
        self.assertIn('2 games found', self.window.lan_status.text())
        with patch.object(self.window.session, 'start') as start:
            self.window.join_button.click()
            prefs = start.call_args.args[1]
            self.assertEqual((prefs.mode, prefs.host, prefs.server, prefs.port), ('multiplayer', False, '192.168.1.20', 27025))
            self.assertFalse(start.call_args.kwargs['lan'])
        # Joining a game is for one flight; the saved flight mode is untouched.
        self.assertEqual(self.window.prefs.mode, 'free')
        # A refresh keeps the selection, and an empty network says what to do.
        self.window.show_lobbies([self.lobby(name='Another', address='192.168.1.30'), self.lobby()])
        self.assertEqual(self.window.selected_lobby().address, '192.168.1.20')
        self.window.show_lobbies([])
        self.assertFalse(self.window.join_button.isEnabled())
        self.assertIn('No games found', self.window.lan_status.text())
        with patch.object(self.window.session, 'start') as start, patch.object(self.window, 'error') as error:
            self.window.join_selected()
            start.assert_not_called()
            error.assert_called_once()

    def test_host_and_fly_opens_the_game_to_the_network(self):
        self.window.show_page(10)
        self.window.lan_pilot.setText('Ace')
        self.window.lan_pilot.editingFinished.emit()
        self.assertEqual(self.window.prefs.name, 'Ace')
        self.assertEqual(self.window.pilot_field.text(), 'Ace')
        self.assertEqual(self.window.pilot_button.text(), 'Ace')
        self.assertEqual(self.window.lobby_field.placeholderText(), "Ace's game")
        self.window.lobby_field.setText('Squadron night')
        self.window.lobby_field.editingFinished.emit()
        self.window.lan_bots.setValue(3)
        with patch.object(self.window.session, 'start') as start:
            self.window.host_button.click()
            prefs = start.call_args.args[1]
            self.assertEqual((prefs.mode, prefs.host, prefs.lobby, prefs.lan_bots), ('multiplayer', True, 'Squadron night', 3))
            self.assertTrue(start.call_args.kwargs['lan'])
        saved = read_json(self.data / 'launcher.json')
        self.assertEqual((saved['name'], saved['lobby'], saved['lan_bots'], saved['mode']), ('Ace', 'Squadron night', 3, 'free'))

    def test_join_by_address_and_shared_network_fields(self):
        self.window.show_page(10)
        self.window.direct_address.setText(' 192.168.1.44 ')
        self.window.direct_port.setValue(27033)
        with patch.object(self.window.session, 'start') as start:
            self.window.direct_button.click()
            prefs = start.call_args.args[1]
            self.assertEqual((prefs.mode, prefs.host, prefs.server, prefs.port), ('multiplayer', False, '192.168.1.44', 27033))
        self.assertEqual(self.window.server_field.text(), '192.168.1.44')
        self.assertEqual(self.window.port_field.value(), 27033)
        self.window.port_field.setValue(27020)
        self.assertEqual(self.window.direct_port.value(), 27020)

    def test_games_are_not_looked_for_behind_the_players_back(self):
        with patch('launcher.ui.lan.discover', return_value=[]) as discover:
            self.window.show_page(0)
            self.window.scan_lan()
            self.assertIsNone(self.window.scan)
            self.window.show_page(10)
            deadline = time.monotonic() + 3
            while self.window.scan and time.monotonic() < deadline:
                self.app.processEvents()
                time.sleep(.01)
            self.assertIsNone(self.window.scan)
            discover.assert_called_once()
            with patch.object(self.window.session, 'running', return_value=True):
                self.window.scan_lan()
                self.assertIsNone(self.window.scan)

    def wait_for_scan(self):
        deadline = time.monotonic() + 3
        while self.window.scan and time.monotonic() < deadline:
            self.app.processEvents()
            time.sleep(.01)
        self.assertIsNone(self.window.scan)

    def test_a_linux_host_is_told_when_its_firewall_is_in_the_way(self):
        advice = 'This computer\'s firewall will turn other players away. To let them in until the next restart, run:  sudo firewall-cmd --add-port=27020/udp'
        self.firewall_advice.return_value = advice
        with patch('launcher.ui.lan.discover', return_value=[]), patch('launcher.ui.lan.local_addresses', return_value=['192.168.1.23']):
            self.window.show_page(10)
            self.wait_for_scan()
            self.assertIn('192.168.1.23, port 27020', self.window.lan_address.text())
            self.assertIn('sudo firewall-cmd --add-port=27020/udp', self.window.lan_address.text())
            # The firewall is asked once, not on every look, and the advice stays up.
            self.window.scan_lan()
            self.wait_for_scan()
            self.firewall_advice.assert_called_once_with(27020)
            self.assertIn('sudo firewall-cmd', self.window.lan_address.text())
            # A different port is a different question; an open firewall clears the advice.
            self.firewall_advice.return_value = None
            self.window.set_network(port=27031)
            self.window.scan_lan()
            self.wait_for_scan()
            self.assertEqual(self.firewall_advice.call_count, 2)
            self.assertNotIn('firewall', self.window.lan_address.text())
            self.assertIn('port 27031', self.window.lan_address.text())

    def test_multiplayer_actions_wait_for_a_flight_in_progress(self):
        self.window.show_page(10)
        self.window.show_lobbies([self.lobby()])
        with patch.object(self.window.session, 'running', return_value=True):
            self.window.refresh_summary()
            self.assertFalse(self.window.host_button.isEnabled())
            self.assertFalse(self.window.join_button.isEnabled())
            with patch.object(self.window.session, 'start') as start, patch.object(self.window, 'error'):
                self.window.host_lan()
                start.assert_not_called()
        self.window.refresh_summary()
        self.assertTrue(self.window.host_button.isEnabled())

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
        self.root = Path(self.temp.name).resolve()
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
