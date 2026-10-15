import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / 'scripts' / 'rea-fs.py'

class WorkflowTests(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(CLI), *map(str, args)], capture_output=True, text=True)

    def test_private_session_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / 'session'
            result = self.run_cli('init-session', '--sensor', '06cb:00dc', '--directory', target)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(target.stat().st_mode & 0o777, 0o700)
            self.assertEqual((target / 'checkpoint.md').stat().st_mode & 0o777, 0o600)
            self.assertNotEqual(self.run_cli('init-session', '--sensor', 'other', '--directory', target).returncode, 0)

    def test_summary_counts_errors_separately_and_rejects_bad_numbers(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'attempts.csv'
            source.write_text('kind,score,seconds,status\ngenuine,0.8,2,ok\ngenuine,0.3,4,ok\nimpostor,0.6,3,ok\nimpostor,,20,timeout\n')
            result = self.run_cli('summarize', source, '--threshold', '0.5')
            self.assertEqual(result.returncode, 0, result.stderr)
            data = json.loads(result.stdout)
            self.assertEqual(data['genuine']['false_rejects'], 1)
            self.assertEqual(data['impostor']['false_accepts'], 1)
            self.assertEqual(data['errors'], 1)
            source.write_text('kind,score,seconds,status\ngenuine,nan,2,ok\n')
            self.assertNotEqual(self.run_cli('summarize', source).returncode, 0)
