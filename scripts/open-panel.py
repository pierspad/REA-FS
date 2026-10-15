#!/usr/bin/env python3
"""Open the local panel; initialize a simulation/offline plan if absent."""
import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.experiment_panel import prepare_session


def is_binary_up_to_date(bin_path: Path, panel_dir: Path) -> bool:
    if not bin_path.is_file():
        return False
    bin_mtime = bin_path.stat().st_mtime
    src_paths = [panel_dir / 'Cargo.toml']
    lock = panel_dir / 'Cargo.lock'
    if lock.is_file():
        src_paths.append(lock)
    src_dir = panel_dir / 'src'
    if src_dir.is_dir():
        src_paths.extend(p for p in src_dir.rglob('*') if p.is_file())
    return all(bin_mtime >= p.stat().st_mtime for p in src_paths)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session-dir', type=Path, default=ROOT / '.rea-fs.local/panel')
    args = parser.parse_args()
    if not shutil.which('cargo'):
        parser.exit(1, 'Rust/Cargo non disponibile. Leggi docs/experiment-panel.md per i prerequisiti.\n')
    session = args.session_dir.resolve()
    try:
        if prepare_session(session, ROOT / 'examples/panel-plan.json'):
            print(f'Creato piano iniziale con simulazione e prove offline: {session}/plan.json', flush=True)
    except OSError as exc:
        parser.exit(1, f'Impossibile preparare la sessione: {exc}\n')
    env = os.environ.copy()
    if os.environ.get('WAYLAND_DISPLAY') or os.environ.get('XDG_SESSION_TYPE') == 'wayland':
        env.setdefault('WINIT_UNIX_BACKEND', 'wayland')
    if platform.system() == 'Linux' and platform.machine() == 'x86_64' and shutil.which('cc'):
        env.setdefault('CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER', 'cc')
    bin_path = ROOT / 'panel/target/release/rea-fs-panel'
    if not is_binary_up_to_date(bin_path, ROOT / 'panel'):
        build_res = subprocess.call(
            ['cargo', 'build', '--release', '--locked', '--manifest-path', str(ROOT / 'panel/Cargo.toml')],
            cwd=ROOT, env=env
        )
        if build_res != 0:
            return build_res
    return subprocess.call(
        [str(bin_path), '--repo', str(ROOT), '--session-dir', str(session)],
        cwd=ROOT, env=env
    )

if __name__=='__main__':sys.exit(main())
