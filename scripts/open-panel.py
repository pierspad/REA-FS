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


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session-dir',type=Path,default=ROOT/'.rea-fs.local/panel')
    args=parser.parse_args()
    if not shutil.which('cargo'):
        parser.exit(1,'Rust/Cargo non disponibile. Leggi docs/experiment-panel.md per i prerequisiti.\n')
    session=args.session_dir.resolve()
    try:
        if prepare_session(session,ROOT/'examples/panel-plan.json'):
            print(f'Creato piano iniziale con simulazione e prove offline: {session}/plan.json',flush=True)
    except OSError as exc:parser.exit(1,f'Impossibile preparare la sessione: {exc}\n')
    env=os.environ.copy()
    if platform.system()=='Linux' and platform.machine()=='x86_64' and shutil.which('cc'):
        env.setdefault('CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER','cc')
    return subprocess.call(['cargo','run','--release','--locked','--manifest-path',str(ROOT/'panel/Cargo.toml'),
                            '--','--repo',str(ROOT),'--session-dir',str(session)],cwd=ROOT,env=env)

if __name__=='__main__':sys.exit(main())
