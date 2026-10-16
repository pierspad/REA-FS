#!/usr/bin/env python3
"""Local experiment runner for the egui panel. Plans never autorun."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.experiment_panel import validate_session, run_experiment, plan_fingerprint, append_note, FINGERS


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    subs=parser.add_subparsers(dest='action',required=True)
    for name in ('validate','run','note'):
        sub=subs.add_parser(name)
        sub.add_argument('--session-dir',type=Path,required=True)
        if name=='run':
            sub.add_argument('--experiment',required=True)
            sub.add_argument('--cancel-on-eof',action='store_true')
            sub.add_argument('--expected-fingerprint')
            sub.add_argument('--finger',choices=FINGERS)
        if name=='note':
            sub.add_argument('--text',required=True)
    subs.add_parser('simulate')
    args=parser.parse_args()
    try:
        if args.action=='validate':
            # Also writes panel-status.json (valid or error) for the agent.
            plan=validate_session(args.session_dir)
            print(json.dumps({'plan':plan,'fingerprint':plan_fingerprint(plan)},ensure_ascii=False));return 0
        if args.action=='run':return run_experiment(args.session_dir.resolve(),args.experiment,args.cancel_on_eof,
                                                    expected_fingerprint=args.expected_fingerprint,finger=args.finger)
        if args.action=='note':
            print(json.dumps(append_note(args.session_dir,args.text),ensure_ascii=False));return 0
        def emit(**fields):print(json.dumps(fields),flush=True)
        for stage in range(1,4):
            emit(type='instruction',text='Simulated event: waiting for touch. Do not touch the reader.')
            emit(type='device_state',needed=True,present=False);time.sleep(1)
            emit(type='device_state',needed=False,present=True);time.sleep(1)
            emit(type='progress',completed=stage,total=3)
            emit(type='instruction',text='Simulated event: release. This is not a hardware measurement.')
            emit(type='device_state',needed=False,present=False);time.sleep(.5)
        return 0
    except (OSError,ValueError,KeyError) as exc:
        print(str(exc),file=sys.stderr);return 2

if __name__=='__main__':sys.exit(main())
