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
