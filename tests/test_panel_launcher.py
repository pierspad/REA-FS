import importlib.util
from pathlib import Path
import tempfile
import shutil
import subprocess
import unittest
from unittest import mock


LAUNCHER = Path(__file__).resolve().parents[1] / 'scripts/open-panel.py'
SPEC = importlib.util.spec_from_file_location('open_panel', LAUNCHER)
open_panel = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(open_panel)


class PanelLauncherTests(unittest.TestCase):
    def test_installs_owned_entry_with_quoted_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'repo % back\\slash "quoted" $cash'
            (root / 'scripts').mkdir(parents=True)
            (root / 'docs').mkdir()
            data_home = Path(tmp) / 'xdg-data'
            python = Path(tmp) / 'python % back\\slash "quoted" $cash'
            with (
                mock.patch.object(open_panel, 'ROOT', root),
                mock.patch.object(open_panel.sys, 'executable', str(python)),
                mock.patch.dict(open_panel.os.environ, {'XDG_DATA_HOME': str(data_home)}),
            ):
                self.assertTrue(open_panel.install_desktop_entry())
                desktop_file = data_home / 'applications/rea-fs-panel.desktop'
                contents = desktop_file.read_text(encoding='utf-8')
                self.assertIn('# Managed by REA-FS open-panel.py', contents)
                self.assertIn('Name=REA-FS\n', contents)
                self.assertIn('Icon=' + str((root / 'docs/rea-fs-icon.svg').resolve()).replace('\\', '\\\\'), contents)
                self.assertIn('StartupWMClass=rea-fs-panel\n', contents)
                self.assertIn('%%', contents)
                self.assertIn('\\\\', contents)
                self.assertIn('\\"', contents)
                self.assertIn('\\$', contents)
                self.assertFalse(open_panel.install_desktop_entry())
                if shutil.which('desktop-file-validate'):
                    result = subprocess.run(['desktop-file-validate', str(desktop_file)],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_does_not_replace_unowned_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            data_home = Path(tmp) / 'xdg-data'
            desktop_file = data_home / 'applications/rea-fs-panel.desktop'
            desktop_file.parent.mkdir(parents=True)
            original = '[Desktop Entry]\nName=Another application\n'
            desktop_file.write_text(original, encoding='utf-8')
            with mock.patch.dict(open_panel.os.environ, {'XDG_DATA_HOME': str(data_home)}):
                self.assertFalse(open_panel.install_desktop_entry())
            self.assertEqual(desktop_file.read_text(encoding='utf-8'), original)

    def test_current_binary_launches_without_cargo(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            panel = root / 'panel'
            sources = panel / 'src'
            target = panel / 'target/release'
            sources.mkdir(parents=True)
            target.mkdir(parents=True)
            (panel / 'Cargo.toml').write_text('[package]\n')
            binary = target / 'rea-fs-panel'
            binary.write_text('fake executable')

            session = root / 'session'
            with (
                mock.patch.object(open_panel, 'ROOT', root),
                mock.patch.object(open_panel.shutil, 'which', return_value=None),
                mock.patch.object(open_panel, 'prepare_session', return_value=False),
                mock.patch.object(open_panel.subprocess, 'call', return_value=0) as call,
                mock.patch.dict(open_panel.os.environ, {'XDG_DATA_HOME': str(root / 'xdg-data')}, clear=True),
                mock.patch.object(open_panel.sys, 'argv', ['open-panel.py', '--session-dir', str(session)]),
            ):
                self.assertEqual(open_panel.main(), 0)

            self.assertEqual(call.call_count, 1)
            self.assertEqual(call.call_args.args[0][0], str(binary))
            self.assertTrue((root / 'xdg-data/applications/rea-fs-panel.desktop').is_file())


if __name__ == '__main__':
    unittest.main()
