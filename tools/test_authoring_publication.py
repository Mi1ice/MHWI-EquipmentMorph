"""Real file transactions with narrowly injected OS failures."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('publication',ROOT/'blender_addon_v044/mhwi_equipmentmorph/output_files.py')
publication=importlib.util.module_from_spec(spec);spec.loader.exec_module(publication)

class PublicationTests(unittest.TestCase):
    def setUp(self):
        parent=ROOT/'.build/authoring-publication-tests';parent.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=parent);self.root=Path(self.temp.name)
        self.stage=self.root/'stage';self.output=self.root/'output';self.stage.mkdir();self.output.mkdir()
    def tearDown(self): self.temp.cleanup()
    def put(self,root,name,data):
        path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);return path
    def test_success_only_replaces_owned_files(self):
        self.put(self.stage,'a',b'new');self.put(self.output,'a',b'old');self.put(self.output,'unrelated',b'keep')
        publication.publish(self.stage,self.output)
        self.assertEqual((self.output/'a').read_bytes(),b'new');self.assertEqual((self.output/'unrelated').read_bytes(),b'keep')
    def test_move_failure_restores_old_and_removes_new(self):
        for n in ('a','b','c'):self.put(self.stage,n,b'new')
        self.put(self.output,'b',b'old');self.put(self.output,'unrelated',b'keep')
        real=os.replace
        def move(source,target):
            if Path(source)==self.stage/'c':raise OSError('injected third move')
            return real(source,target)
        with patch.object(publication.os,'replace',side_effect=move),self.assertRaises(OSError):publication.publish(self.stage,self.output)
        self.assertEqual({p.name:p.read_bytes() for p in self.output.iterdir()},{'b':b'old','unrelated':b'keep'})
    def test_rollback_failure_keeps_real_old_bytes(self):
        self.put(self.stage,'a',b'new');self.put(self.stage,'b',b'new');self.put(self.output,'a',b'old')
        real=os.replace
        def move(source,target):
            if Path(source)==self.stage/'b' or 'morph-output-backup-' in str(source):raise OSError('injected failure')
            return real(source,target)
        with patch.object(publication.os,'replace',side_effect=move),self.assertRaisesRegex(RuntimeError,'备份保留'):publication.publish(self.stage,self.output)
        self.assertEqual(list(self.output.glob('morph-output-backup-*/a'))[0].read_bytes(),b'old')
    def test_conflicts_fail_before_any_write(self):
        self.put(self.stage,'a',b'new');self.put(self.stage,'b/nested',b'new');self.put(self.output,'a',b'old');self.put(self.output,'b',b'keep')
        with self.assertRaises(ValueError):publication.publish(self.stage,self.output)
        self.assertEqual((self.output/'a').read_bytes(),b'old');self.assertEqual((self.stage/'a').read_bytes(),b'new')
        (self.output/'b').unlink();(self.output/'a').unlink();(self.output/'a').mkdir()
        with self.assertRaises(ValueError):publication.publish(self.stage,self.output)
    def test_output_junction_rejected_before_resolution(self):
        self.put(self.stage,'a',b'new');link=self.root/'junction'
        if os.name=='nt':
            subprocess.run(['cmd','/c','mklink','/J',str(link),str(self.output)],check=True,capture_output=True)
        else:link.symlink_to(self.output,target_is_directory=True)
        try:
            with self.assertRaises(ValueError):publication.publish(self.stage,link)
            self.assertFalse((self.output/'a').exists())
        finally:link.rmdir() if os.name=='nt' else link.unlink()
    def test_post_commit_cleanup_error_is_explicit(self):
        self.put(self.stage,'a',b'new');self.put(self.output,'a',b'old')
        with patch.object(publication.shutil,'rmtree',side_effect=OSError('injected cleanup')),self.assertRaisesRegex(RuntimeError,'已发布.*清理'):
            publication.publish(self.stage,self.output)
        self.assertEqual((self.output/'a').read_bytes(),b'new')
        self.assertEqual(list(self.output.glob('morph-output-backup-*/a'))[0].read_bytes(),b'old')

if __name__=='__main__': unittest.main(verbosity=2)
