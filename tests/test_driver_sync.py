import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.driver_sync import (
    extract_meson_version,
    extract_bootstrap_version,
    verify_versions_match,
    check_submodule_reachable_on_remote,
    rewrite_native_prototype_id,
    sync_driver,
)


class DriverSyncTests(unittest.TestCase):
    def test_version_match_and_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            meson = tmp_path / "meson.build"
            bootstrap = tmp_path / "bootstrap.sh"

            # 1. Matching versions
            meson.write_text("project('syna_00dc_tod', 'c', version: '0.1.2')\n")
            bootstrap.write_text("#!/bin/bash\nVERSION=v0.1.2\n")
            ok, msg = verify_versions_match(tmp_path)
            self.assertTrue(ok)
            self.assertIn("0.1.2", msg)

            # 2. Mismatching versions
            bootstrap.write_text("#!/bin/bash\nVERSION=v0.1.3\n")
            ok, msg = verify_versions_match(tmp_path)
            self.assertFalse(ok)
            self.assertIn("Version mismatch", msg)

    def test_native_prototype_id_rewrite(self):
        sample = """
static void
syna_device_class_init (SynaDeviceClass *klass)
{
    FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
    dev_class->id = "syna_00dc_tod";
    dev_class->nr_enroll_stages = 8;
}
"""
        rewritten = rewrite_native_prototype_id(sample)
        self.assertIn('dev_class->id = "syna_00dc";', rewritten)
        self.assertNotIn('dev_class->id = "syna_00dc_tod";', rewritten)

    def test_sync_rewrites_native_prototype_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            syna_dir = root / "drivers" / "syna_tod"
            proto_dir = root / "workspace" / "libfprint" / "libfprint" / "drivers" / "syna_00dc"
            (syna_dir / "src").mkdir(parents=True)
            proto_dir.mkdir(parents=True)

            # Create mock git repo in syna_dir
            subprocess.run(["git", "init", "-b", "main", str(syna_dir)], capture_output=True, check=True)
            subprocess.run(["git", "-C", str(syna_dir), "config", "user.name", "tester"], check=True)
            subprocess.run(["git", "-C", str(syna_dir), "config", "user.email", "test@example.com"], check=True)

            # Create a mock remote
            remote_repo = root / "remote_mock"
            subprocess.run(["git", "init", "--bare", "-b", "main", str(remote_repo)], capture_output=True, check=True)
            subprocess.run(["git", "-C", str(syna_dir), "remote", "add", "origin", str(remote_repo)], check=True)

            test_c = syna_dir / "src" / "syna_device.c"
            test_c.write_text('dev_class->id = "syna_00dc_tod";\n')
            subprocess.run(["git", "-C", str(syna_dir), "add", "."], check=True)
            subprocess.run(["git", "-C", str(syna_dir), "commit", "-m", "init"], check=True)
            subprocess.run(["git", "-C", str(syna_dir), "push", "origin", "main"], check=True)

            ret = sync_driver(root=root, synaptics_repo=root / "nonexistent")
            self.assertEqual(ret, 0)
            synced_content = (proto_dir / "syna_device.c").read_text()
            self.assertIn('dev_class->id = "syna_00dc";', synced_content)
            self.assertNotIn('dev_class->id = "syna_00dc_tod";', synced_content)

    def test_unpushed_commit_detected(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            repo = tmp_path / "repo"
            remote = tmp_path / "remote"

            # Init bare remote
            subprocess.run(["git", "init", "--bare", "-b", "main", str(remote)], capture_output=True, check=True)

            # Init repo
            subprocess.run(["git", "init", "-b", "main", str(repo)], capture_output=True, check=True)
            subprocess.run(["git", "-C", str(repo), "config", "user.name", "tester"], check=True)
            subprocess.run(["git", "-C", str(repo), "config", "user.email", "test@example.com"], check=True)
            subprocess.run(["git", "-C", str(repo), "remote", "add", "origin", str(remote)], check=True)

            # Initial commit and push
            (repo / "file.txt").write_text("v1")
            subprocess.run(["git", "-C", str(repo), "add", "."], check=True)
            subprocess.run(["git", "-C", str(repo), "commit", "-m", "v1"], check=True)
            subprocess.run(["git", "-C", str(repo), "push", "-u", "origin", "main"], check=True)

            # Verify reachable
            ok, _ = check_submodule_reachable_on_remote(repo, "origin/main")
            self.assertTrue(ok)

            # Add unpushed commit
            (repo / "file.txt").write_text("v2")
            subprocess.run(["git", "-C", str(repo), "add", "."], check=True)
            subprocess.run(["git", "-C", str(repo), "commit", "-m", "unpushed v2"], check=True)

            # Verify NOT reachable
            ok, msg = check_submodule_reachable_on_remote(repo, "origin/main")
            self.assertFalse(ok)
            self.assertIn("NOT reachable", msg)


if __name__ == "__main__":
    unittest.main()
