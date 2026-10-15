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
import tempfile

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
        if not isinstance(task.get('next_steps', ''), str) or len(task.get('next_steps', '')) > 2000:
            raise ValueError('next_steps must be text up to 2000 characters')
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
        params = task.get('parameters', [])
        if not isinstance(params, list) or any(p not in PARAMETERS for p in params):
            raise ValueError('parameters may only contain "finger"')
        if any('{finger}' in arg for arg in argv) != ('finger' in params):
            raise ValueError('{finger} placeholder and parameters ["finger"] must be declared together')
    return plan


PARAMETERS = ('finger',)
FINGERS = tuple(f'{side}-{name}' for side in ('left', 'right')
                for name in ('thumb', 'index-finger', 'middle-finger', 'ring-finger', 'little-finger'))
NOTE_LIMIT = 500


def sanitize_note(text) -> str:
    if not isinstance(text, str): raise ValueError('note must be text')
    clean = ' '.join(''.join(c if c.isprintable() else ' ' for c in text).split())[:NOTE_LIMIT]
    if not clean: raise ValueError('note is empty')
    return clean


def _private_atomic_write(path: Path, content: str):
    descriptor, temporary = tempfile.mkstemp(prefix='.panel-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w') as stream: stream.write(content)
        os.chmod(temporary, 0o600)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)


def _private_append(path: Path, line: str):
    fd = os.open(path, os.O_WRONLY|os.O_CREAT|os.O_APPEND, 0o600)
    with os.fdopen(fd, 'a') as stream: stream.write(line+'\n')
    os.chmod(path, 0o600)


def write_panel_status(session_dir: Path, plan_path: Path, plan=None, error=None):
    """Record the last validation outcome so the agent can check what the GUI loaded."""
    session_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    status = {'schema_version': 1, 'updated_at': datetime.now(timezone.utc).isoformat(),
              'plan_path': str(plan_path), 'valid': plan is not None,
              'plan_id': plan['plan_id'] if plan else None, 'revision': plan['revision'] if plan else None,
              'experiment_ids': [x['id'] for x in plan['experiments']] if plan else [],
              'error': None if plan else str(error)[:2000]}
    _private_atomic_write(session_dir/'panel-status.json', json.dumps(status, ensure_ascii=False, indent=2)+'\n')
    return status


def validate_session(session_dir: Path, root: Path = ROOT) -> dict:
    path = session_dir/'plan.json'
    try: plan = load_plan(path, root)
    except (OSError, ValueError) as exc:
        write_panel_status(session_dir, path, error=exc); raise
    write_panel_status(session_dir, path, plan)
    return plan


class EventWriter:
    def __init__(self, session_dir: Path, plan: dict, experiment: dict, parameters=None):
        session_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.run_id = str(uuid.uuid4())
        self.session_dir = session_dir.resolve()
        self.parameters = dict(parameters or {})
        self.user_notes = []
        self.last_event = None
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
        self.run_dir = self.session_dir/f'{stamp}-{self.run_id}'
        self.started_event = None
        self.event_count = 0
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
        self.event_count += 1
        if event_type == 'started': self.started_event = event
        if event_type == 'user_note': self.user_notes.append({'timestamp': event['timestamp'], 'text': event['text']})
        if event_type in TERMINAL or event_type in ('started', 'user_note'):
            self._write_summary(event if event_type != 'user_note' else (self.last_event or event))
        if event_type in TERMINAL: self._append_index(event)
        print(line, flush=True)
        return event

    def _write_summary(self, last_event):
        self.last_event = last_event
        summary = dict(self.base, started_event=self.started_event, last_event=last_event,
                       event_count=self.event_count, user_notes=self.user_notes,
                       events_path=str(self.run_dir/'events.jsonl'),
                       plan_path=str(self.run_dir/'plan.json'), summary_path=str(self.run_dir/'summary.json'))
        content = json.dumps(summary, ensure_ascii=False, indent=2)+'\n'
        self._atomic_write(self.run_dir/'summary.json', content)
        self._atomic_write(self.session_dir/'latest-run.json', content)
        self._atomic_write(self.run_dir/'handoff.txt',
            f"Leggi {self.session_dir}/runs-index.jsonl, poi {self.run_dir}/plan.json, {self.run_dir}/events.jsonl e {self.run_dir}/summary.json. "
            "Riprendi il checkpoint; distingui simulazione e hardware. Analizza l’esito prima di ripetere la prova.\n")

    def _append_index(self, event):
        started = self.started_event or {}
        entry = dict(self.base, adapter=started.get('adapter'), started_at=started.get('timestamp'),
                     ended_at=event['timestamp'], terminal_type=event['type'], exit_code=event.get('exit_code'),
                     result=event.get('result'), parameters=self.parameters,
                     run_dir=str(self.run_dir), summary_path=str(self.run_dir/'summary.json'))
        _private_append(self.session_dir/'runs-index.jsonl', json.dumps(entry, ensure_ascii=False))

    @staticmethod
    def _atomic_write(path, content):
        _private_atomic_write(path, content)

    def close(self): self.stream.close()


TERMINAL = ('completed', 'failed', 'timeout', 'stopped')


def append_note(session_dir: Path, text: str) -> dict:
    """Attach a user note to the latest run after it ended (GUI note button)."""
    session_dir = session_dir.resolve()
    text = sanitize_note(text)
    latest = json.loads((session_dir/'latest-run.json').read_text())
    run_dir = Path(latest['summary_path']).resolve().parent
    if not run_dir.is_relative_to(session_dir) or not run_dir.is_dir(): raise ValueError('latest run outside session')
    summary_path = run_dir/'summary.json'
    summary = json.loads(summary_path.read_text())
    if summary.get('run_id') != latest.get('run_id'): raise ValueError('latest run mismatch')
    event = {k: summary[k] for k in ('schema_version', 'plan_id', 'revision', 'experiment_id', 'run_id')}
    event.update(type='user_note', origin='user', text=text, after_end=True,
                 timestamp=datetime.now(timezone.utc).isoformat())
    _private_append(run_dir/'events.jsonl', json.dumps(event, ensure_ascii=False))
    summary.setdefault('user_notes', []).append({'timestamp': event['timestamp'], 'text': text})
    summary['event_count'] = summary.get('event_count', 0)+1
    content = json.dumps(summary, ensure_ascii=False, indent=2)+'\n'
    _private_atomic_write(summary_path, content)
    _private_atomic_write(session_dir/'latest-run.json', content)
    return event

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
                elif isinstance(value, dict) and value.get('type') == 'note':
                    try: messages.put(('control', 'note', sanitize_note(value.get('text'))))
                    except ValueError: pass
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


RETRY_CODES = ('enroll-retry-scan', 'enroll-swipe-too-short', 'enroll-finger-not-centered', 'enroll-remove-and-retry',
               'verify-retry-scan', 'verify-swipe-too-short', 'verify-finger-not-centered', 'verify-remove-and-retry')


def run_experiment(session_dir, experiment_id, cancel_on_eof=False, root=ROOT, expected_fingerprint=None, finger=None):
    plan = load_plan(session_dir/'plan.json', root)
    if expected_fingerprint and plan_fingerprint(plan) != expected_fingerprint:
        raise ValueError('plan changed after preview; reload and review before running')
    task = next((x for x in plan['experiments'] if x['id'] == experiment_id), None)
    if task is None: raise ValueError('experiment not found')
    parameters = {}
    if 'finger' in task.get('parameters', []):
        if finger not in FINGERS: raise ValueError('experiment requires --finger with an fprintd finger name')
        parameters['finger'] = finger
    elif finger is not None: raise ValueError('experiment does not accept --finger')
    writer = EventWriter(session_dir, plan, task, parameters)
    import getpass
    argv = [arg.replace('{run_dir}', str(writer.run_dir.resolve())).replace('{user}', getpass.getuser())
               .replace('{finger}', parameters.get('finger', '{finger}')) for arg in task['argv']]
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
                    instructions=task['instructions'], next_steps=task.get('next_steps', ''),
                    parameters=parameters, run_directory=str(writer.run_dir.resolve()))
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
        listing = Path(argv[0]).name == 'fprintd-list' and task['adapter'] == 'plain'
        enrolled = []
        while True:
            if time.monotonic() >= deadline: reason = 'timeout'; break
            try: kind, channel, payload = messages.get(timeout=.02)
            except queue.Empty: kind = None
            if kind == 'control':
                if channel == 'cancel': reason = 'cancel'; break
                if channel == 'note': writer.emit('user_note', 'user', text=payload)
                else: writer.emit('user_confirmation', 'user', confirmed=True)
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
                if channel == 'stdout' and listing:
                    found = re.fullmatch(r'\s*-\s*#\d+:\s*([a-z-]+)\s*', payload.decode('utf-8', 'replace'))
                    if found and found[1] in FINGERS and found[1] not in enrolled: enrolled.append(found[1])
                if channel == 'stdout' and task['adapter'] == 'fprintd':
                    text = payload.decode('utf-8','replace').strip()
                    device_path = re.fullmatch(r'Using device (/net/reactivated/Fprint/Device/\d+)', text)
                    if device_path: selected_device_path = device_path[1]
                    # fprintd prints its selected path before open/calibration.
                    # Do not expose stale finger-needed flags from that phase.
                    if not bridge_started and selected_device_path and (text == 'Verify started!' or text.startswith('Enrolling ')):
                        threading.Thread(target=_fprint_state,args=(messages,bridge_stop,selected_device_path),daemon=True).start()
                        bridge_started = True
                        # Marks the end of claim/open/calibration: lets the GUI split latency.
                        writer.emit('operation_started', 'device')
                    selected = re.fullmatch(r'Verifying: ([a-z-]+)', text)
                    if selected: writer.emit('verify_finger', 'device', finger=selected[1])
                    if text == 'Enroll result: enroll-stage-passed':
                        completed += 1
                        writer.emit('progress','device',completed=completed,total=total)
                    elif text == 'Enroll result: enroll-completed':
                        writer.emit('progress','device',completed=total or completed+1,total=total or completed+1)
                        device_result = 'enroll-completed'
                    elif text.startswith('Enroll result: '):
                        code = text.split(': ',1)[1].split()[0]
                        if code in RETRY_CODES: writer.emit('retry_hint', 'device', code=code)
                    elif text.startswith('Verify result: '):
                        status = text.split(': ',1)[1].split()[0]
                        if status in ('verify-match','verify-no-match'):
                            device_result = status
                            writer.emit('verify_result', 'device', result=status)
                        elif status in RETRY_CODES: writer.emit('retry_hint', 'device', code=status)
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
            if listing and exit_code == 0: writer.emit('enrolled_fingers', fingers=enrolled)
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
