import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('credentials', ROOT/'scripts/setup-tod-credentials.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class CredentialsTests(unittest.TestCase):
    def fixture(self, root):
        pairing = root/'pairing-candidate'
        pairing.mkdir()
        artifacts = root/'artifacts'
        artifacts.mkdir()
        (pairing/'host-key.pem').write_bytes(b'-----BEGIN PRIVATE KEY-----\nsynthetic-not-a-key\n')
        (pairing/'ownership-reply.bin').write_bytes(bytes(190))
        (pairing/'public-material-after-ownership.bin').write_bytes(bytes(926))
        (artifacts/'acer-capture-init-rom6-20-group2.bin').write_bytes(bytes(4676))
        (artifacts/'type2449-raw-capture-request.bin').write_bytes(bytes(1917))

    def test_validate_all_before_writing_and_refuse_existing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            target = root/'output'
            packet = root/'artifacts/type2449-raw-capture-request.bin'
            packet.unlink()
            self.assertFalse(module.extract_credentials(root, target))
            self.assertFalse(target.exists(), 'Invalid input must not leave partial credentials')
            packet.write_bytes(bytes(1917))
            self.assertTrue(module.extract_credentials(root, target))
            self.assertEqual(target.stat().st_mode & 0o777, 0o700)
            self.assertTrue(all(p.stat().st_mode & 0o777 == 0o600 for p in target.iterdir()))
            sentinel = target/'host_key.pem'
            sentinel.write_bytes(b'keep')
            self.assertFalse(module.extract_credentials(root, target))
            self.assertEqual(sentinel.read_bytes(), b'keep')
