"""Versioned local experiment plans and private events (no implicit execution)."""
from datetime import datetime, timezone
import json
import hashlib
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
    except (ValueError, UnicodeError, RecursionError) as exc: raise ValueError('plan is incomplete or invalid JSON') from exc
    if not isinstance(plan, dict) or type(plan.get('schema_version')) is not int or plan['schema_version'] != 1:
        raise ValueError('schema_version must be 1')
    for key in ('plan_id', 'title'):
        if not isinstance(plan.get(key), str) or not plan[key].strip(): raise ValueError(f'{key} must be nonempty')
    if type(plan.get('revision')) is not int or not 1 <= plan['revision'] <= 2**63-1: raise ValueError('revision must be a positive integer')
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
        cwd = task.get('cwd')
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

# The runner never infers device events from arbitrary command output.
import queue
import select
import re
import signal
import subprocess
import threading

RAW_LIMIT = 4 * 1024 * 1024


def _reader(stream, messages, channel):
    try:
        while True:
            line = stream.readline(MAX_LINE + 1)
            if not line: break
            if len(line) > MAX_LINE:
                messages.put(('overflow', channel, b'')); break
            messages.put(('line', channel, line))
    finally:
        stream.close()
        messages.put(('eof', channel, b''))


def _controls(messages, cancel_on_eof, stop):
    # Read the descriptor directly: a daemon thread must not hold Python stdin's
    # buffered-reader lock when a terminal run finishes without more input.
    buffer = bytearray()
    try:
        descriptor = sys.stdin.fileno()
        while not stop.is_set():
            ready, _, _ = select.select([descriptor], [], [], .1)
            if not ready: continue
            chunk = os.read(descriptor, 4096)
            if not chunk:
                if cancel_on_eof: messages.put(('control', 'cancel', b''))
                return
            buffer.extend(chunk)
            while b'\n' in buffer:
                line, _, tail = buffer.partition(b'\n'); buffer = bytearray(tail)
                if len(line) > 4096: continue
                try: value = json.loads(line)
                except (ValueError, UnicodeError): continue
                if isinstance(value, dict) and value.get('type') in ('cancel', 'confirm'):
                    messages.put(('control', value['type'], b''))
            if len(buffer) > 4096: buffer.clear()
    except (OSError, ValueError):
        if cancel_on_eof: messages.put(('control', 'cancel', b''))


def _group_exists(pid):
    try: os.killpg(pid, 0); return True
    except ProcessLookupError: return False


def _stop_group(process):
    forced = False
    for sig, grace in [(signal.SIGINT, 2.0), (signal.SIGTERM, 1.0), (signal.SIGKILL, .1)]:
        if not _group_exists(process.pid): break
        try: os.killpg(process.pid, sig)
        except ProcessLookupError: break
        if sig != signal.SIGINT: forced = True
        try: process.wait(timeout=grace)
        except subprocess.TimeoutExpired: continue
        # A reaped leader can leave children: send remaining signals to the group.
        if not _group_exists(process.pid): break
        time.sleep(.05)
    try: process.wait(timeout=1)
    except subprocess.TimeoutExpired: forced = True
    return forced


def _simulation_event(writer, line):
    try: data = json.loads(line)
    except (ValueError, UnicodeError): return
    if not isinstance(data, dict): return
    kind = data.get('type')
    if kind == 'device_state' and type(data.get('needed')) is bool and type(data.get('present')) is bool:
        writer.emit(kind, 'simulation', needed=data['needed'], present=data['present'])
    elif kind == 'progress' and type(data.get('completed')) is int and type(data.get('total')) is int:
        if 0 <= data['completed'] <= data['total'] <= 100:
            writer.emit(kind, 'simulation', completed=data['completed'], total=data['total'])
    elif kind == 'instruction' and isinstance(data.get('text'), str):
        writer.emit(kind, 'simulation', text=data['text'][:2000])


def _fprint_state(messages, stop, object_path):
    if not shutil.which('gdbus'):
        messages.put(('bridge_missing', '', b'')); return
    previous = None
    reported_unavailable = False
    while not stop.is_set():
        try:
            result = subprocess.run(['gdbus', 'call', '--system', '--dest', 'net.reactivated.Fprint',
                '--object-path', object_path, '--method',
                'org.freedesktop.DBus.Properties.GetAll', 'net.reactivated.Fprint.Device'],
                capture_output=True, text=True, timeout=1)
            values = {}
            for key in ('finger-needed', 'finger-present'):
                match = re.search(r"'"+key+r"': <(true|false)>", result.stdout)
                if match: values[key] = match[1] == 'true'
            stage = re.search(r"'num-enroll-stages': <(\d+)>", result.stdout)
            if len(values) == 2:
                current = (values['finger-needed'], values['finger-present'], int(stage[1]) if stage else 0)
                if current != previous:
                    messages.put(('device', current, b'')); previous = current
            elif not reported_unavailable:
                messages.put(('limitation', 'Proprietà di contatto non disponibili: attendi gli eventi del comando o interrompi la prova', b''))
                reported_unavailable = True
        except (OSError, subprocess.TimeoutExpired):
            if not reported_unavailable:
                messages.put(('limitation', 'Lettura dello stato D-Bus non riuscita', b''))
                reported_unavailable = True
        stop.wait(.5)


def plan_fingerprint(plan):
    return hashlib.sha256(json.dumps(plan, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()).hexdigest()


def run_experiment(session_dir, experiment_id, cancel_on_eof=False, root=ROOT, expected_fingerprint=None):
    plan = load_plan(session_dir/'plan.json', root)
    if expected_fingerprint and plan_fingerprint(plan) != expected_fingerprint:
        raise ValueError('plan changed after preview; reload and review before running')
    task = next((x for x in plan['experiments'] if x['id'] == experiment_id), None)
    if task is None: raise ValueError('experiment not found')
    writer = EventWriter(session_dir, plan, task)
    argv = [arg.replace('{run_dir}', str(writer.run_dir.resolve())) for arg in task['argv']]
    messages = queue.Queue(maxsize=128)
    bridge_stop = threading.Event()
    process = None
    raw = None
    exit_code = 1
    forced = False
    try:
        process = subprocess.Popen(argv, cwd=(root/task.get('cwd', '.')).resolve(),
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            start_new_session=True)
        writer.emit('started', process_group=process.pid, argv=argv, cwd=str((root/task.get('cwd', '.')).resolve()),
                    timeout_seconds=task['timeout_seconds'], adapter=task['adapter'],
                    instructions=task['instructions'], run_directory=str(writer.run_dir.resolve()))
        for channel, stream in [('stdout', process.stdout), ('stderr', process.stderr)]:
            threading.Thread(target=_reader, args=(stream, messages, channel), daemon=True).start()
        threading.Thread(target=_controls, args=(messages, cancel_on_eof, bridge_stop), daemon=True).start()
        if task.get('raw_log'):
            fd = os.open(writer.run_dir/'output.log', os.O_WRONLY|os.O_CREAT|os.O_EXCL, 0o600)
            raw = os.fdopen(fd, 'wb')
        bridge_started = False
        selected_device_path = None
        deadline = time.monotonic()+task['timeout_seconds']
        reason = None
        raw_size = 0
        streams = 2
        exited_at = None
        completed = 0
        total = 0
        device_result = None
        while True:
            if time.monotonic() >= deadline: reason = 'timeout'; break
            try: kind, channel, payload = messages.get(timeout=.02)
            except queue.Empty: kind = None
            if kind == 'control':
                if channel == 'cancel': reason = 'cancel'; break
                writer.emit('user_confirmation', 'user', confirmed=True)
            elif kind == 'overflow': reason = 'output_limit'; break
            elif kind == 'eof': streams -= 1
            elif kind == 'limitation':
                writer.emit('limitation', text=channel)
            elif kind == 'bridge_missing':
                writer.emit('limitation', text='gdbus unavailable: contact state cannot be observed')
            elif kind == 'device':
                needed, present, total = channel
                writer.emit('device_state','device',needed=needed,present=present)
            elif kind == 'line':
                if raw:
                    raw_size += len(payload)
                    if raw_size > RAW_LIMIT: reason = 'output_limit'; break
                    raw.write(payload); raw.flush()
                if channel == 'stdout' and task['adapter'] == 'simulation':
                    _simulation_event(writer, payload)
                if channel == 'stdout' and task['adapter'] == 'fprintd':
                    text = payload.decode('utf-8','replace').strip()
                    device_path = re.fullmatch(r'Using device (/net/reactivated/Fprint/Device/\d+)', text)
                    if device_path: selected_device_path = device_path[1]
                    # fprintd prints its selected path before open/calibration.
                    # Do not expose stale finger-needed flags from that phase.
                    if not bridge_started and selected_device_path and (text == 'Verify started!' or text.startswith('Enrolling ')):
                        threading.Thread(target=_fprint_state,args=(messages,bridge_stop,selected_device_path),daemon=True).start()
                        bridge_started = True
                    if text == 'Enroll result: enroll-stage-passed':
                        completed += 1
                        writer.emit('progress','device',completed=completed,total=total)
                    elif text == 'Enroll result: enroll-completed':
                        writer.emit('progress','device',completed=total or completed+1,total=total or completed+1)
                        device_result = 'enroll-completed'
                    elif text.startswith('Verify result: '):
                        status = text.split(': ',1)[1].split()[0]
                        if status in ('verify-match','verify-no-match'): device_result = status
            if process.poll() is not None:
                if exited_at is None: exited_at = time.monotonic()
                if streams <= 0 or time.monotonic()-exited_at > .5: break
        bridge_stop.set()
        if reason or _group_exists(process.pid):
            writer.emit('stopping', reason=reason or 'remaining_processes')
            forced = _stop_group(process)
        else: process.wait()
        if reason:
            terminal = 'timeout' if reason == 'timeout' else 'stopped' if reason == 'cancel' else 'failed'
            writer.emit(terminal, reason=reason, exit_code=process.returncode,
                        cleanup_not_confirmed=True, forced=forced)
        else:
            exit_code = process.returncode
            writer.emit('completed' if exit_code == 0 else 'failed', exit_code=exit_code,
                        result=device_result, cleanup_not_confirmed=forced)
        return 0 if not reason and exit_code == 0 else 1
    except KeyboardInterrupt:
        if process and _group_exists(process.pid): _stop_group(process)
        writer.emit('stopped', reason='backend_interrupt', cleanup_not_confirmed=True)
        return 1
    except (OSError, ValueError) as exc:
        if process and _group_exists(process.pid): _stop_group(process)
        writer.emit('failed', reason='backend_error', message=str(exc), cleanup_not_confirmed=True)
        return 1
    finally:
        bridge_stop.set()
        if raw: raw.close()
        writer.close()


def prepare_session(session_dir: Path, template: Path) -> bool:
    """Install a starter plan only when none exists; never launch experiments."""
    session_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    target = session_dir/'plan.json'
    try: fd = os.open(target, os.O_WRONLY|os.O_CREAT|os.O_EXCL, 0o600)
    except FileExistsError: return False
    try:
        with os.fdopen(fd, 'wb') as stream: stream.write(template.read_bytes())
    except OSError:
        target.unlink(missing_ok=True)
        raise
    return True
