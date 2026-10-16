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

DESKTOP_ENTRY_MARKER = '# Managed by REA-FS open-panel.py'


def _desktop_exec_arg(value: str) -> str:
    """Quote one argument for the Desktop Entry Exec key."""
    escaped = str(value)
    escaped = escaped.replace('\\', '\\\\').replace('"', '\\"')
    escaped = escaped.replace('$', '\\$').replace('`', '\\`').replace('%', '%%')
    # Desktop string unescaping happens before Exec argument unquoting.
    return _desktop_string(f'"{escaped}"')


def _desktop_string(value: str) -> str:
    """Escape a value for the Desktop Entry string value type."""
    return str(value).replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t').replace('\r', '\\r')


def install_desktop_entry() -> bool:
    """Install or update the launcher-owned per-user desktop entry."""
    data_home = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    desktop_file = data_home / 'applications/rea-fs-panel.desktop'
    icon = (ROOT / 'docs/rea-fs-icon.svg').resolve()
    executable = _desktop_exec_arg(sys.executable)
    script = _desktop_exec_arg((ROOT / 'scripts/open-panel.py').resolve())
    contents = '\n'.join([
        DESKTOP_ENTRY_MARKER,
        '[Desktop Entry]',
        'Type=Application',
        'Name=REA-FS',
        f'Exec={executable} {script}',
        f'Icon={_desktop_string(icon)}',
        'Terminal=false',
        'Categories=Utility;',
        'StartupWMClass=rea-fs-panel',
        '',
    ])

    if desktop_file.is_symlink():
        return False
    if desktop_file.exists():
        try:
            existing = desktop_file.read_text(encoding='utf-8')
        except (OSError, UnicodeError):
            return False
        if not existing.startswith(DESKTOP_ENTRY_MARKER + '\n'):
            return False
        if existing == contents:
            return False
    desktop_file.parent.mkdir(parents=True, exist_ok=True)
    desktop_file.write_text(contents, encoding='utf-8')
    return True


def is_binary_up_to_date(bin_path: Path, panel_dir: Path) -> bool:
    if not bin_path.is_file():
        return False
    bin_mtime = bin_path.stat().st_mtime
    src_paths = [panel_dir / 'Cargo.toml']
    icon = panel_dir.parent / 'docs/rea-fs-icon.png'
    if icon.is_file():
        src_paths.append(icon)
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
    try:
        install_desktop_entry()
    except OSError as exc:
        print(f'Could not update REA-FS desktop entry: {exc}', file=sys.stderr)
    session = args.session_dir.resolve()
    try:
        if prepare_session(session, ROOT / 'examples/panel-plan.json'):
            print(f'Created starter plan with simulation and offline checks: {session}/plan.json', flush=True)
    except OSError as exc:
        parser.exit(1, f'Could not prepare session: {exc}\n')
    env = os.environ.copy()
    if os.environ.get('WAYLAND_DISPLAY') or os.environ.get('XDG_SESSION_TYPE') == 'wayland':
        env.setdefault('WINIT_UNIX_BACKEND', 'wayland')
    if platform.system() == 'Linux' and platform.machine() == 'x86_64' and shutil.which('cc'):
        env.setdefault('CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER', 'cc')
    bin_path = ROOT / 'panel/target/release/rea-fs-panel'
    try:
        if not is_binary_up_to_date(bin_path, ROOT / 'panel'):
            if not shutil.which('cargo'):
                parser.exit(1, 'Rust/Cargo not found. See docs/experiment-panel.md for prerequisites.\n')
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
    except KeyboardInterrupt:
        return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)

