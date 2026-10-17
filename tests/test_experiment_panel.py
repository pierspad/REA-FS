import contextlib
import copy
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from tools.experiment_panel import load_plan, EventWriter

class PanelModelTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.path = self.root/'plan.json'
        self.plan = {'schema_version':1,'plan_id':'test','revision':1,'title':'Local test','experiments':[
            {'id':'one','title':'One','instructions':'Wait','argv':[sys.executable,'-c','print(1)'],
             'cwd':'.','timeout_seconds':3,'adapter':'plain','category':'offline'}]}
    def write(self, plan=None):
        self.path.write_text(json.dumps(plan or self.plan))
        return load_plan(self.path, self.root)
    def test_validates_and_rejects_ambiguous_or_escaping_plan(self):
        self.assertEqual(self.write()['experiments'][0]['id'],'one')
        for field,value in [('argv','echo hello'),('argv',['sudo','true']),('cwd','../escape'),
                            ('timeout_seconds',0),('timeout_seconds',True),('adapter','unknown')]:
            plan=copy.deepcopy(self.plan);plan['experiments'][0][field]=value
            with self.subTest(field=field,value=value),self.assertRaises(ValueError):self.write(plan)
        duplicate=copy.deepcopy(self.plan);duplicate['experiments']*=2
        with self.assertRaises(ValueError):self.write(duplicate)
        plan=copy.deepcopy(self.plan);plan['experiments'][0]['argv']=['/no/such/program']
        with self.assertRaises(ValueError):self.write(plan)
    def test_next_steps_is_optional_bounded_text(self):
        self.plan['experiments'][0]['next_steps'] = 'Chiedi all’agente di leggere i log.'
        self.assertEqual(self.write()['experiments'][0]['next_steps'], self.plan['experiments'][0]['next_steps'])
        for value in [123, 'x' * 2001]:
            self.plan['experiments'][0]['next_steps'] = value
            with self.assertRaises(ValueError): self.write()
    def test_summary_and_latest_allow_agent_to_resume_without_clipboard(self):
        plan=self.write()
        with contextlib.redirect_stdout(io.StringIO()):
            writer=EventWriter(self.root,plan,plan['experiments'][0])
            writer.emit('started',adapter='simulation')
            writer.emit('completed',exit_code=0)
            writer.close()
        summary=json.loads((writer.run_dir/'summary.json').read_text())
        latest=json.loads((self.root/'latest-run.json').read_text())
        self.assertEqual(summary['last_event']['type'],'completed')
        self.assertEqual(summary['started_event']['adapter'],'simulation')
        self.assertEqual(latest['run_id'],writer.run_id)
        self.assertEqual(Path(latest['events_path']),writer.run_dir/'events.jsonl')
        self.assertIn('events.jsonl',(writer.run_dir/'handoff.txt').read_text())
        for path in [writer.run_dir/'summary.json', self.root/'latest-run.json', writer.run_dir/'handoff.txt']:
            self.assertEqual(path.stat().st_mode&0o777,0o600)
    def test_partial_and_oversized_plan(self):
        for data in ['{',' '*262145]:
            self.path.write_text(data)
            with self.assertRaises(ValueError):load_plan(self.path,self.root)
    def test_private_runs_keep_revisions_and_distinguish_origins(self):
        plan=self.write();session=self.root/'session'
        with contextlib.redirect_stdout(io.StringIO()):
            first=EventWriter(session,plan,plan['experiments'][0]);first.emit('started');first.close()
            plan['revision']=2
            second=EventWriter(session,plan,plan['experiments'][0]);second.emit('device_state',origin='simulation',needed=True);second.close()
        self.assertNotEqual(first.run_dir,second.run_dir)
        self.assertEqual(first.run_dir.stat().st_mode&0o777,0o700)
        self.assertEqual((first.run_dir/'events.jsonl').stat().st_mode&0o777,0o600)
        event=json.loads((first.run_dir/'events.jsonl').read_text())
        self.assertEqual(event['revision'],1)
        event=json.loads((second.run_dir/'events.jsonl').read_text())
        self.assertEqual(event['origin'],'simulation')

import subprocess
import time

CLI = Path(__file__).resolve().parents[1]/'scripts/experiment-panel.py'

class RunnerTests(PanelModelTests):
    def start(self, code, timeout=5, adapter='plain', extra=()):
        self.plan['experiments'][0].update(argv=[sys.executable,'-c',code],timeout_seconds=timeout,adapter=adapter)
        self.write()
        return subprocess.Popen([sys.executable,str(CLI),'run','--session-dir',str(self.root),
                                 '--experiment','one',*extra],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,text=True)
    def events(self, process, input=''):
        out,err=process.communicate(input=input,timeout=10)
        self.assertFalse(err,err)
        return [json.loads(line) for line in out.splitlines()]
    def test_success_does_not_trust_device_looking_stdout(self):
        self.plan['experiments'][0]['next_steps'] = 'Continua dai log.'
        process=self.start('print(\'{"type":"device_state","needed":true}\')')
        events=self.events(process)
        self.assertEqual(events[0]['next_steps'], 'Continua dai log.')
        self.assertEqual(events[-1]['type'],'completed')
        self.assertFalse(any(e['type']=='device_state' for e in events))
    def test_failure_and_timeout(self):
        events=self.events(self.start('raise SystemExit(7)'))
        self.assertEqual(events[-1]['type'],'failed');self.assertEqual(events[-1]['exit_code'],7)
        events=self.events(self.start('import time;time.sleep(20)',timeout=1))
        self.assertEqual(events[-1]['type'],'timeout')
    def test_cancel_and_plan_revision_are_explicit(self):
        process=self.start('import time;time.sleep(20)')
        first=json.loads(process.stdout.readline())
        self.assertEqual(first['type'],'started')
        self.plan['revision']=2;self.write()
        events=self.events(process,'{"type":"cancel"}\n')
        self.assertEqual(events[-1]['type'],'stopped')
        self.assertTrue(all(e['revision']==1 for e in events))
    def test_output_cap_and_eof_cancellation(self):
        events=self.events(self.start('print("x"*70000)'))
        self.assertEqual(events[-1]['type'],'failed')
        self.assertEqual(events[-1]['reason'],'output_limit')
        events=self.events(self.start('import time;time.sleep(20)',extra=('--cancel-on-eof',)))
        self.assertEqual(events[-1]['type'],'stopped')
    def test_cancel_stops_descendant_process(self):
        marker=self.root/'child.pid'
        code=f'import subprocess,time;from pathlib import Path;p=subprocess.Popen([{sys.executable!r},"-c","import time;time.sleep(60)"]);Path({str(marker)!r}).write_text(str(p.pid));time.sleep(60)'
        process=self.start(code)
        deadline=time.monotonic()+3
        while not marker.exists() and time.monotonic()<deadline:time.sleep(.01)
        self.assertTrue(marker.exists())
        events=self.events(process,'{"type":"cancel"}\n')
        self.assertEqual(events[-1]['type'],'stopped')
        pid=int(marker.read_text())
        stat=Path(f'/proc/{pid}/stat')
        self.assertTrue(not stat.exists() or stat.read_text().split()[2]=='Z')

    def test_backend_interrupt_stops_child_group(self):
        import signal
        process=self.start('import time;time.sleep(30)')
        first=json.loads(process.stdout.readline())
        self.assertEqual(first['type'],'started')
        process.send_signal(signal.SIGINT)
        events=self.events(process)
        self.assertEqual(events[-1]['type'],'stopped')

    def test_changed_plan_fingerprint_prevents_launch(self):
        marker=self.root/'must-not-exist'
        process=self.start(f'from pathlib import Path;Path({str(marker)!r}).touch()',extra=('--expected-fingerprint','incorrect'))
        out,err=process.communicate(input='',timeout=5)
        self.assertNotEqual(process.returncode,0)
        self.assertFalse(marker.exists())
        self.assertEqual(out,'')
        self.assertIn('plan changed',err)

    def test_missing_cwd_and_large_revision_rejected_for_gui(self):
        plan=copy.deepcopy(self.plan);del plan['experiments'][0]['cwd']
        with self.assertRaises(ValueError):self.write(plan)
        plan=copy.deepcopy(self.plan);plan['revision']=2**80
        with self.assertRaises(ValueError):self.write(plan)


class LauncherTests(unittest.TestCase):
    def test_starter_plan_is_private_and_never_replaces_agent_plan(self):
        from tools.experiment_panel import prepare_session
        with tempfile.TemporaryDirectory() as tmp:
            session=Path(tmp)/'panel'
            template=Path(tmp)/'example.json';template.write_text('{"example":true}')
            self.assertTrue(prepare_session(session,template))
            self.assertEqual((session/'plan.json').stat().st_mode&0o777,0o600)
            (session/'plan.json').write_text('keep my plan')
            self.assertFalse(prepare_session(session,template))
            self.assertEqual((session/'plan.json').read_text(),'keep my plan')


class LogProtocolTests(RunnerTests):
    def test_runs_index_accumulates_private_entries(self):
        self.events(self.start('print(1)'))
        self.events(self.start('raise SystemExit(3)'))
        index=self.root/'runs-index.jsonl'
        self.assertEqual(index.stat().st_mode&0o777,0o600)
        entries=[json.loads(x) for x in index.read_text().splitlines()]
        self.assertEqual([e['terminal_type'] for e in entries],['completed','failed'])
        self.assertEqual(entries[1]['exit_code'],3)
        for key in ('schema_version','run_id','plan_id','revision','experiment_id','adapter','started_at',
                    'ended_at','terminal_type','exit_code','run_dir','summary_path'):
            self.assertIn(key,entries[0])
        self.assertTrue(Path(entries[0]['summary_path']).exists())

    def test_validate_writes_panel_status_for_valid_and_invalid_plans(self):
        from tools.experiment_panel import validate_session
        self.write()
        validate_session(self.root,self.root)
        status_path=self.root/'panel-status.json'
        status=json.loads(status_path.read_text())
        self.assertEqual(status_path.stat().st_mode&0o777,0o600)
        self.assertTrue(status['valid']);self.assertEqual(status['experiment_ids'],['one']);self.assertIsNone(status['error'])
        self.path.write_text('{')
        with self.assertRaises(ValueError):validate_session(self.root,self.root)
        status=json.loads(status_path.read_text())
        self.assertFalse(status['valid']);self.assertTrue(status['error'])

    def test_user_note_during_and_after_run(self):
        process=self.start('import time;time.sleep(20)')
        self.assertEqual(json.loads(process.stdout.readline())['type'],'started')
        long='dito\nsbagliato '+'x'*600
        events=self.events(process,json.dumps({'type':'note','text':long})+'\n{"type":"cancel"}\n')
        note=next(e for e in events if e['type']=='user_note')
        self.assertEqual(note['origin'],'user');self.assertNotIn('\n',note['text']);self.assertLessEqual(len(note['text']),500)
        latest=json.loads((self.root/'latest-run.json').read_text())
        self.assertEqual(latest['last_event']['type'],'stopped')
        self.assertEqual(len(latest['user_notes']),1)
        out=subprocess.run([sys.executable,str(CLI),'note','--session-dir',str(self.root),'--text','nessun segnale'],
                           capture_output=True,text=True,timeout=10)
        self.assertEqual(out.returncode,0,out.stderr)
        summary=json.loads(Path(latest['summary_path']).read_text())
        self.assertEqual([n['text'] for n in summary['user_notes']][-1],'nessun segnale')
        self.assertEqual(Path(latest['summary_path']).stat().st_mode&0o777,0o600)
        last=json.loads(Path(latest['events_path']).read_text().splitlines()[-1])
        self.assertEqual((last['type'],last['origin']),('user_note','user'))

    def test_finger_parameter_is_whitelisted(self):
        self.plan['experiments'][0].update(argv=[sys.executable,'-c','import sys;print(sys.argv[1])','{finger}'])
        with self.assertRaises(ValueError):self.write()
        self.plan['experiments'][0]['parameters']=['finger'];self.write()
        def run(*extra):
            return subprocess.run([sys.executable,str(CLI),'run','--session-dir',str(self.root),'--experiment','one',*extra],
                                  capture_output=True,text=True,timeout=10,stdin=subprocess.DEVNULL)
        self.assertNotEqual(run('--finger','$(id)').returncode,0)
        self.assertNotEqual(run().returncode,0)
        ok=run('--finger','left-thumb')
        self.assertEqual(ok.returncode,0,ok.stderr)
        self.assertEqual(json.loads(ok.stdout.splitlines()[0])['argv'][-1],'left-thumb')

    def test_transient_already_in_use_retries_automatically(self):
        counter = self.root / 'attempts.txt'
        code = (
            f"from pathlib import Path; import sys; p = Path({str(counter)!r}); "
            "n = int(p.read_text()) if p.exists() else 0; p.write_text(str(n + 1)); "
            "sys.stderr.write('GDBus.Error:net.reactivated.Fprint.Error.AlreadyInUse: Device was already claimed\\n') if n == 0 else print('ok'); "
            "raise SystemExit(1 if n == 0 else 0)"
        )
        events = self.events(self.start(code, timeout=5))
        self.assertEqual(events[-1]['type'], 'completed')
        self.assertEqual(int(counter.read_text()), 2)

    def test_fprintd_verify_defaults_to_any_finger(self):
        fake_verify = self.root / 'fprintd-verify'
        fake_verify.write_text(f"#!{sys.executable}\nimport sys\nprint('ARGS:', ' '.join(sys.argv[1:]))\n")
        fake_verify.chmod(0o755)
        self.plan['experiments'][0].update(
            argv=[str(fake_verify)],
            timeout_seconds=5,
            adapter='fprintd',
            raw_log=True,
        )
        self.write()
        out = subprocess.run(
            [sys.executable, str(CLI), 'run', '--session-dir', str(self.root), '--experiment', 'one'],
            capture_output=True, text=True, timeout=10, stdin=subprocess.DEVNULL,
        )
        self.assertEqual(out.returncode, 0, out.stderr)
        events = [json.loads(line) for line in out.stdout.splitlines()]
        self.assertEqual(events[0]['argv'][-2:], ['-f', 'any'])
