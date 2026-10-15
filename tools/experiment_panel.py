"""Versioned local experiment plans and private events (no implicit execution)."""
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import shutil
import sys
import time
import uuid

MAX_PLAN = 256 * 1024
MAX_LINE = 64 * 1024
ROOT = Path(__file__).resolve().parents[1]


def load_plan(path: Path, root: Path = ROOT) -> dict:
    root = root.resolve()
    with path.open('rb') as stream:
        content = stream.read(MAX_PLAN + 1)
    if len(content) > MAX_PLAN: raise ValueError('plan exceeds 256 KiB')
    try: plan = json.loads(content)
    except (ValueError, UnicodeError) as exc: raise ValueError('plan is incomplete or invalid JSON') from exc
    if not isinstance(plan, dict) or type(plan.get('schema_version')) is not int or plan['schema_version'] != 1:
        raise ValueError('schema_version must be 1')
    for key in ('plan_id', 'title'):
        if not isinstance(plan.get(key), str) or not plan[key].strip(): raise ValueError(f'{key} must be nonempty')
    if type(plan.get('revision')) is not int or plan['revision'] < 1: raise ValueError('revision must be a positive integer')
    tasks = plan.get('experiments')
    if not isinstance(tasks, list) or not 1 <= len(tasks) <= 32: raise ValueError('plan needs 1..32 experiments')
    ids = set()
    for task in tasks:
        if not isinstance(task, dict): raise ValueError('experiment must be an object')
        for key in ('id', 'title', 'instructions', 'category'):
            if not isinstance(task.get(key), str) or not task[key].strip(): raise ValueError(f'experiment {key} must be nonempty')
        if task['id'] in ids: raise ValueError('duplicate experiment id')
        ids.add(task['id'])
        argv = task.get('argv')
        if not isinstance(argv, list) or not argv or not all(isinstance(x, str) and x and '\0' not in x for x in argv):
            raise ValueError('argv must be a nonempty string array')
        if Path(argv[0]).name in ('sudo', 'su', 'pkexec'): raise ValueError('run privileged actions in your terminal')
        cwd = task.get('cwd', '.')
        if not isinstance(cwd, str) or Path(cwd).is_absolute(): raise ValueError('cwd must be relative to repository')
        resolved = (root/cwd).resolve()
        if not resolved.is_relative_to(root) or not resolved.is_dir(): raise ValueError('cwd escapes repository or does not exist')
        executable = str(resolved/argv[0]) if '/' in argv[0] and not Path(argv[0]).is_absolute() else argv[0]
        if not shutil.which(executable): raise ValueError(f'executable unavailable: {argv[0]}')
        if type(task.get('timeout_seconds')) is not int or not 1 <= task['timeout_seconds'] <= 3600:
            raise ValueError('timeout_seconds must be 1..3600')
        if task.get('adapter') not in ('plain', 'simulation', 'fprintd'): raise ValueError('unknown adapter')
        if task['adapter'] == 'fprintd' and Path(argv[0]).name not in ('fprintd-enroll', 'fprintd-verify'):
            raise ValueError('fprintd adapter requires fprintd-enroll or fprintd-verify')
        if type(task.get('raw_log', False)) is not bool: raise ValueError('raw_log must be boolean')
    return plan


class EventWriter:
    def __init__(self, session_dir: Path, plan: dict, experiment: dict):
        session_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.run_id = str(uuid.uuid4())
        self.run_dir = session_dir/self.run_id
        self.run_dir.mkdir(mode=0o700)
        self.started = time.monotonic()
        self.base = {'schema_version': 1, 'plan_id': plan['plan_id'], 'revision': plan['revision'],
                     'experiment_id': experiment['id'], 'run_id': self.run_id}
        fd = os.open(self.run_dir/'events.jsonl', os.O_WRONLY|os.O_CREAT|os.O_EXCL, 0o600)
        self.stream = os.fdopen(fd, 'w')
        fd = os.open(self.run_dir/'plan.json', os.O_WRONLY|os.O_CREAT|os.O_EXCL, 0o600)
        with os.fdopen(fd, 'w') as snapshot: json.dump(plan, snapshot, ensure_ascii=False)

    def emit(self, event_type, origin='backend', **fields):
        event = dict(fields, **self.base, type=event_type, origin=origin,
                     timestamp=datetime.now(timezone.utc).isoformat(),
                     elapsed_seconds=round(time.monotonic()-self.started, 3))
        line = json.dumps(event, ensure_ascii=False, allow_nan=False)
        if len(line.encode()) > MAX_LINE: raise ValueError('event exceeds size limit')
        self.stream.write(line+'\n'); self.stream.flush()
        print(line, flush=True)
        return event

    def close(self): self.stream.close()
