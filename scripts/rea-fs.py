#!/usr/bin/env python3
"""Local workflow helpers. No USB writes, sudo, network access or publication."""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]


def command(args, cwd=ROOT, timeout=30):
    try:
        result = subprocess.run(args, cwd=cwd, capture_output=True, text=True, timeout=timeout)
        return {'command': args, 'exit_code': result.returncode,
                'stdout': result.stdout.strip(), 'stderr': result.stderr.strip()}
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {'command': args, 'exit_code': None, 'error': str(exc)}


def private_write(path, text):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w') as stream:
        stream.write(text)


def preflight():
    repos = {}
    for name, path in [('REA-FS', ROOT), ('libfprint', ROOT/'workspace/libfprint'),
                       ('rea', ROOT/'workspace/rea'), ('synaTudor-fork', ROOT/'workspace/synaTudor-fork')]:
        if not path.is_dir():
            repos[name] = {'available': False}
            continue
        repos[name] = {label: command(args, path) for label, args in [
            ('revision', ['git', 'rev-parse', 'HEAD']),
            ('changes', ['git', 'status', '--short']),
            ('author_name', ['git', 'config', '--show-origin', '--get', 'user.name']),
            ('author_email', ['git', 'config', '--show-origin', '--get', 'user.email']),
        ]}
    # Do not emit remotes: arbitrary URLs can contain embedded credentials.
    return {'time_utc': datetime.now(timezone.utc).isoformat(),
            'kernel': platform.release(), 'repositories': repos,
            'usb': command(['lsusb']),
            'tod': command(['pkg-config', '--modversion', 'libfprint-2-tod-1']),
            'note': 'Private metadata; review before sharing. No device was opened.'}


def summarize(path, threshold):
    groups = {'genuine': [], 'impostor': []}
    times = []
    errors = 0
    with path.open(newline='') as stream:
        reader = csv.DictReader(stream)
        if not {'kind', 'score', 'seconds', 'status'} <= set(reader.fieldnames or []):
            raise ValueError('CSV requires kind,score,seconds,status')
        for row in reader:
            if row['kind'] not in groups: raise ValueError('kind must be genuine or impostor')
            seconds = float(row['seconds'])
            if not math.isfinite(seconds) or seconds < 0: raise ValueError('invalid seconds')
            if row['status'] in ('timeout', 'error'):
                errors += 1
                continue
            if row['status'] != 'ok': raise ValueError('status must be ok, timeout or error')
            score = float(row['score'])
            if not math.isfinite(score) or not -1.001 <= score <= 1.001:
                raise ValueError('invalid correlation score')
            groups[row['kind']].append(score)
            times.append(seconds)
    def stats(kind):
        values = groups[kind]
        wrong = sum(s < threshold if kind == 'genuine' else s >= threshold for s in values)
        return {'attempts': len(values),
                'false_rejects' if kind == 'genuine' else 'false_accepts': wrong,
                'observed_error_rate': wrong / len(values) if values else None,
                'score_min': min(values) if values else None,
                'score_max': max(values) if values else None}
    return {'threshold': threshold, 'genuine': stats('genuine'), 'impostor': stats('impostor'),
            'errors': errors, 'median_completed_seconds': statistics.median(times) if times else None,
            'limitation': 'Descriptive rates on these attempts only; no population FAR/FRR or security certification. Timeouts excluded from comparison rates and counted separately.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='action', required=True)
    init = subs.add_parser('init-session', help='Create a private checkpoint; never overwrite')
    init.add_argument('--sensor', required=True, help='Observed bus/ID; not a driver selector')
    init.add_argument('--directory', type=Path, required=True)
    pre = subs.add_parser('preflight', help='Read local repository and dependency metadata')
    pre.add_argument('--output', type=Path, required=True)
    subs.add_parser('check', help='Run offline Python tests, build and C tests; no hardware')
    summary = subs.add_parser('summarize', help='Summarize a manually labeled attempt CSV')
    summary.add_argument('csv', type=Path)
    summary.add_argument('--threshold', type=float, default=0.5)
    args = parser.parse_args()
    try:
        if args.action == 'init-session':
            if any(c in args.sensor for c in '\r\n'): raise ValueError('sensor must be one line')
            args.directory.mkdir(mode=0o700, parents=False, exist_ok=False)
            private_write(args.directory/'checkpoint.md', f'''# Checkpoint\n\nSensore dichiarato: {args.sensor}\nFase: ATTENDI — discovery da eseguire\nFatti verificati: nessuno\nIpotesi: nessuna\nAutorizzazioni: leggere preferenze e consenso in chat; nessun consenso implicito\nDati: privati locali; niente upload\nEsperimenti conclusi: nessuno\nProcessi attivi: nessuno\nAzione fisica richiesta: nessuna\nProssimo passo: preflight, poi identificazione e ricerca del supporto\n''')
            print(args.directory)
        elif args.action == 'preflight':
            private_write(args.output, json.dumps(preflight(), indent=2)+'\n')
            print(args.output)
        elif args.action == 'summarize':
            if not math.isfinite(args.threshold) or not -1 <= args.threshold <= 1:
                raise ValueError('threshold must be finite and between -1 and 1')
            print(json.dumps(summarize(args.csv, args.threshold), indent=2, allow_nan=False))
        else:
            commands = [[sys.executable, '-m', 'unittest', 'discover', '-v']]
            build = ROOT/'drivers/syna_tod/build'
            if not (build/'build.ninja').exists():
                commands.append(['meson', 'setup', str(build), str(ROOT/'drivers/syna_tod')])
            commands += [['ninja', '-C', str(build)], ['meson', 'test', '-C', str(build), '--print-errorlogs']]
            for argv in commands:
                result = command(argv, timeout=180)
                print(json.dumps(result, indent=2))
                if result['exit_code'] != 0: return 1
    except (OSError, ValueError, csv.Error) as exc:
        parser.exit(1, f'error: {exc}\n')
    return 0

if __name__ == '__main__':
    sys.exit(main())
