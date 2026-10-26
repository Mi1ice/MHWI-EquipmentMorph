"""Candidate contract fixtures; these tests do not claim Blender acceptance."""
import copy
import importlib
import json
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import authoring_closeout_report as reports

try: candidate=importlib.import_module('package_authoring_candidate')
except ImportError: candidate=None

ROOT=reports.ROOT
class CandidateTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(candidate,'Dedicated author candidate packager must exist')
        parent=ROOT/'.build/candidate-contract-tests';parent.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=parent);self.root=Path(self.temp.name)
        self.build=self.root/'build';self.build.mkdir();self.output=self.root/'candidate'
        self.report=self.fixture()
    def tearDown(self):
        if hasattr(self,'temp'):self.temp.cleanup()
    def save(self):reports.write(self.build/'report.json',self.report)
    def fixture(self):
        # Synthetic report contract only; external executable bytes are inert fixtures.
        outputs={};checks=[]
        for relative in ('entrypoints/entrypoints.json','preflight/preflight.json','key-preview.json','export/export.json'):
            path=self.build/relative;path.parent.mkdir(parents=True,exist_ok=True)
            reports.write(path,dict(passed=1,checks=[relative]));outputs[relative]=reports.sha(path);checks.append(relative)
        external=self.build/'external';external.mkdir()
        for i in range(8):(external/str(i)).write_bytes(('fixture'+str(i)).encode())
        native=json.loads((ROOT/'blender_addon_v044/mhwi_equipmentmorph/_native/build/build.json').read_bytes())
        report=dict(schema='mhwi-authoring-closeout-v1',stage='All',software_checks_passed=True,checks=checks,source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),dirty=False,inputs=reports.inputs(),outputs=outputs,background_blender_tested=True,blender=dict(path=str(external/'0'),sha256=reports.sha(external/'0'),version_output='Blender 5.1.2 fixture'),validator=dict(path=str(external/'1'),sha256=reports.sha(external/'1')),native_component=native,exporter=dict(sources={str(external/str(i)):reports.sha(external/str(i)) for i in range(8)}),python_suites=['fixture contract'])
        reports.write(self.build/'report.json',report);return report
    def refuse(self,mutate):
        mutate(self.report);self.save()
        with self.assertRaises((ValueError,OSError)):candidate.assemble(self.build,self.output)
        self.assertFalse(self.output.exists(),'reject before creating candidate')
    def test_success_pending_until_matching_external_selftest(self):
        manifest=candidate.assemble(self.build,self.output)
        self.assertFalse(manifest['candidate_complete'])
        with self.assertRaises(ValueError):candidate.finalize(self.output,{})
        receipt=dict(passed=5,checks=['fixture']*5,zip_sha256=reports.sha(self.output/'addon.zip'),validator_sha256=self.report['validator']['sha256'],background_blender_tested=True,addon_from_zip=True,blender_sha256=self.report['blender']['sha256'],installation_tested=True,enable_via_addon_utils=True,installed_files={name.removeprefix('mhwi_equipmentmorph/'):digest for name,digest in manifest['zip_files'].items()})
        candidate.finalize(self.output,receipt)
        self.assertTrue(json.loads((self.output/'manifest.json').read_bytes())['candidate_complete'])
        candidate.validate_zip(self.output/'addon.zip',manifest['zip_files'])
    def test_direct_registration_receipt_cannot_complete(self):
        manifest=candidate.assemble(self.build,self.output)
        receipt=dict(passed=1,checks=['direct-register fixture'],zip_sha256=reports.sha(self.output/'addon.zip'),validator_sha256=self.report['validator']['sha256'],background_blender_tested=True,addon_from_zip=True,blender_sha256=self.report['blender']['sha256'],installed_files={name.removeprefix('mhwi_equipmentmorph/'):digest for name,digest in manifest['zip_files'].items()})
        with self.assertRaises(ValueError):candidate.finalize(self.output,receipt)
    def test_changed_installed_files_cannot_complete(self):
        manifest=candidate.assemble(self.build,self.output)
        receipt=dict(passed=1,checks=['installation fixture'],zip_sha256=reports.sha(self.output/'addon.zip'),validator_sha256=self.report['validator']['sha256'],background_blender_tested=True,addon_from_zip=True,blender_sha256=self.report['blender']['sha256'],installation_tested=True,enable_via_addon_utils=True,installed_files={name.removeprefix('mhwi_equipmentmorph/'):digest for name,digest in manifest['zip_files'].items()})
        receipt['installed_files'][next(iter(receipt['installed_files']))]='0'*64
        with self.assertRaises(ValueError):candidate.finalize(self.output,receipt)
    def test_failed_all(self):self.refuse(lambda r:r.update(software_checks_passed=False))
    def test_partial_stage(self):self.refuse(lambda r:r.update(stage='Export'))
    def test_failed_single_stage_invalidates_old_all(self):
        result=subprocess.run([sys.executable,str(ROOT/'tools/authoring_closeout_report.py'),'--stage','Entrypoints','--blender',sys.executable,'--output',str(self.build)],capture_output=True)
        self.assertNotEqual(result.returncode,0)
        report=json.loads((self.build/'report.json').read_bytes())
        self.assertEqual(report['stage'],'Entrypoints');self.assertFalse(report['software_checks_passed'])
        with self.assertRaises(ValueError):candidate.assemble(self.build,self.output)
    def test_missing_blender_invalidates_old_all_before_hashing(self):
        result=subprocess.run([sys.executable,str(ROOT/'tools/authoring_closeout_report.py'),'--stage','Entrypoints','--blender',str(self.root/'missing.exe'),'--output',str(self.build)],capture_output=True)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(json.loads((self.build/'report.json').read_bytes())['software_checks_passed'])
    def test_wrong_blender(self):self.refuse(lambda r:r['blender'].update(version_output='Blender 4.2'))
    def test_no_background(self):self.refuse(lambda r:r.update(background_blender_tested=False))
    def test_missing_native_validation(self):self.refuse(lambda r:r.pop('validator'))
    def test_missing_check_output(self):
        (self.build/'export/export.json').unlink()
        with self.assertRaises((ValueError,OSError)):candidate.assemble(self.build,self.output)
    def test_changed_source(self):self.refuse(lambda r:r['inputs'].update({'blender_addon_v044/mhwi_equipmentmorph/__init__.py':'0'*64}))
    def test_changed_adapter(self):self.refuse(lambda r:r['inputs'].update({'blender_addon_v044/mhwi_equipmentmorph/direct_export_adapter.json':'0'*64}))
    def test_changed_native_license(self):self.refuse(lambda r:r['inputs'].update({'blender_addon_v044/mhwi_equipmentmorph/_native/build/NOTICE.txt':'0'*64}))
    def test_changed_native_component(self):self.refuse(lambda r:r['native_component'].update(sha256='0'*64))
    def test_changed_exporter(self):self.refuse(lambda r:r['exporter']['sources'].update({next(iter(r['exporter']['sources'])):'0'*64}))
    def test_changed_validator(self):self.refuse(lambda r:r['validator'].update(sha256='0'*64))
    def test_changed_output_digest(self):self.refuse(lambda r:r['outputs'].update({'export/export.json':'0'*64}))
    def test_existing_output(self):
        self.output.mkdir();(self.output/'keep').write_bytes(b'keep')
        with self.assertRaises(ValueError):candidate.assemble(self.build,self.output)
        self.assertEqual((self.output/'keep').read_bytes(),b'keep')
    def test_zip_injected_payloads(self):
        manifest=candidate.assemble(self.build,self.output)
        for index,name in enumerate(('../escape','mhwi_equipmentmorph/old.dll','mhwi_equipmentmorph/private.blend','mhwi_equipmentmorph/runtime.ini')):
            path=self.root/(str(index)+'.zip');path.write_bytes((self.output/'addon.zip').read_bytes())
            with zipfile.ZipFile(path,'a') as archive:archive.writestr(name,b'unwanted')
            with self.assertRaises(ValueError):candidate.validate_zip(path,manifest['zip_files'])
    def test_zip_missing_and_changed_file(self):
        manifest=candidate.assemble(self.build,self.output)
        entries=list(manifest['zip_files'])
        for changed in (False,True):
            path=self.root/(str(changed)+'.zip')
            with zipfile.ZipFile(self.output/'addon.zip') as source,zipfile.ZipFile(path,'w') as target:
                for entry in entries:
                    if entry==entries[0] and not changed:continue
                    target.writestr(entry,b'changed' if entry==entries[0] else source.read(entry))
            with self.assertRaises(ValueError):candidate.validate_zip(path,manifest['zip_files'])
    def test_mismatched_selftest_never_completes(self):
        candidate.assemble(self.build,self.output)
        with self.assertRaises(ValueError):candidate.finalize(self.output,dict(passed=1,zip_sha256='0'*64))
        self.assertFalse(json.loads((self.output/'manifest.json').read_bytes())['candidate_complete'])

if __name__=='__main__':unittest.main(verbosity=2)
