"""Exercise candidate packaging against an isolated synthetic source repository."""
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

PATH=Path(__file__).with_name('package_panel_capacity.py')
class PackageTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(PATH.is_file(),'panel packager capability missing')
        spec=importlib.util.spec_from_file_location('panel_package',PATH);self.module=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.module)
        base=PATH.parent.parent/'.build/panel-capacity/package-checks';base.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=base);self.addCleanup(self.temp.cleanup);self.folder=Path(self.temp.name)
        self.source=self.folder/'source';self.source.mkdir();self.module.ROOT=self.source
        for name in ('LICENSE','docs/PRESETS.md','docs/IMGUI_INSTALLATION.md','controls/MHWI-EquipmentMorph-ImGui/README.md'):
            p=self.source/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('[presets](../../docs/PRESETS.md) and [header](../../runtime/header.h)\n' if name.endswith('README.md') else 'synthetic documentation\n',encoding='utf-8')
        def git(*args):return subprocess.check_output(['git','-C',str(self.source),*args],stderr=subprocess.DEVNULL).decode().strip()
        git('init');git('add','.');git('-c','user.name=Fixture','-c','user.email=fixture@example.invalid','commit','-m','synthetic source')
        self.commit=git('rev-parse','HEAD');self.build=self.folder/'build';self.imgui=self.build/'imgui';self.imgui.mkdir(parents=True);self.output=self.folder/'candidate'
        raw=bytearray(1024);raw[:2]=b'MZ';struct.pack_into('<I',raw,0x3c,0x80);raw[0x80:0x84]=b'PE\0\0';struct.pack_into('<H',raw,0x84,0x8664);struct.pack_into('<H',raw,0x96,0x2000);struct.pack_into('<H',raw,0x98,0x20b)
        self.dll=self.imgui/'MHWEquipmentMorphImGui.dll';self.dll.write_bytes(raw)
        names=('MHWEquipmentMorphImGui.dll','EquipmentMorphImGuiFixture.exe','ImGuiInputTests.exe','ImGuiControlTests.exe','ImGuiPresetTests.exe','README.md','LICENSE.ImGui.txt','LICENSE.SafetyHook','LICENSE.Zydis')
        for name in names[1:]:(self.imgui/name).write_bytes(b'synthetic')
        self.manifest={'schema':'mhwi-equipmentmorph-imgui-candidate-v1','panel_revision':'dynamic-panel-v2','snapshot':'process-local-dynamic','preset_schema':2,'preset_root':'MHWEquipmentMorph/panel-v2','gui_tested':False,'game_tested':False,'morph_api_connected':False,'files':[{'path':n,'sha256':self.sha((self.imgui/n).read_bytes())} for n in names]}
        self.sdk={'software_checks_passed':True,'gui_tested':False,'game_tested':False}
        sdk=self.build/'sdk-validation-regression';sdk.mkdir();(sdk/'all-report.json').write_text(json.dumps(self.sdk),encoding='utf-8')
        self.report={'schema':'mhwi-panel-capacity-v1','stage':'All','source_commit':self.commit,'software_checks_passed':True,'gui_tested':False,'game_tested':False,'formal_acceptance_complete':False,'completed_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_sha256':{p.relative_to(self.source).as_posix():self.sha(p.read_bytes()) for p in self.source.rglob('*') if p.is_file() and '.git' not in p.parts},'checks':{n:{'passed':True,'checks':1} for n in ('Session','Control','Preset','Storage','Regression')},'core_sha256':'a'*64,'dll':{'file':'imgui/MHWEquipmentMorphImGui.dll','bytes':len(raw),'sha256':self.sha(raw)}}
        self.report['checks']['integration']={'passed':True,'input_checks':1,'control_checks':1,'preset_checks':1,'fixture_built':True,'fixture_run':False}
        self.report['checks']['sdk_regression']={'passed':True,'report_sha256':self.sha((sdk/'all-report.json').read_bytes())}
        self.save()
    @staticmethod
    def sha(raw):return hashlib.sha256(raw).hexdigest()
    def save(self):
        path=self.imgui/'manifest.json';path.write_text(json.dumps(self.manifest),encoding='utf-8');self.report['imgui_manifest_sha256']=self.sha(path.read_bytes());(self.build/'all-report.json').write_text(json.dumps(self.report),encoding='utf-8')
    def package(self):return self.module.package(self.build,self.output)
    def rejected(self):
        with self.assertRaises((ValueError,OSError)):self.package()
        self.assertFalse(self.output.exists())
    def test_candidate_has_only_panel_docs_licenses_reports(self):
        result=self.package();self.assertEqual([p.name for p in self.output.rglob('*.dll')],['MHWEquipmentMorphImGui.dll']);self.assertFalse(list(self.output.rglob('*.exe')))
        for name,info in result['files'].items():self.assertEqual(self.sha((self.output/name).read_bytes()),info['sha256'])
        self.assertFalse(result['deployed']);self.assertEqual(result['preset_schema'],2)
        self.assertEqual((self.output/'README.md').read_text(encoding='utf-8'),'[presets](PRESETS.md) and header\n')
    def test_missing_dll(self):self.dll.unlink();self.rejected()
    def test_changed_dll(self):self.dll.write_bytes(b'changed');self.rejected()
    def test_failed_report(self):self.report['software_checks_passed']=False;self.save();self.rejected()
    def test_missing_stage(self):del self.report['checks']['Storage'];self.save();self.rejected()
    def test_wrong_commit(self):self.report['source_commit']='0'*40;self.save();self.rejected()
    def test_digest_mismatch(self):self.report['source_sha256']['LICENSE']='0'*64;self.save();self.rejected()
    def test_missing_source(self):del self.report['source_sha256']['LICENSE'];self.save();self.rejected()
    def test_uncommitted_source(self):(self.source/'LICENSE').write_text('changed');self.rejected()
    def test_stale_report(self):os.utime(self.build/'all-report.json',(1,1));self.rejected()
    def test_gui_claim(self):self.report['gui_tested']=True;self.save();self.rejected()
    def test_core_payload_refused(self):(self.imgui/'core.dll').write_bytes(b'core');self.rejected()
    def test_user_data_refused(self):(self.imgui/'presets.json').write_text('{}');self.rejected()
    def test_unexpected_manifest_file(self):self.manifest['files'].append({'path':'../core.dll','sha256':'a'*64});self.save();self.rejected()
    def test_sdk_report_changed(self):(self.build/'sdk-validation-regression/all-report.json').write_text('{}');self.rejected()
    def test_non_x64(self):
        raw=bytearray(self.dll.read_bytes());struct.pack_into('<H',raw,0x84,0x14c);self.dll.write_bytes(raw);self.report['dll']['sha256']=self.sha(raw);self.manifest['files'][0]['sha256']=self.sha(raw);self.save();self.rejected()
    def test_existing_output_preserved(self):
        self.output.mkdir();marker=self.output/'keep';marker.write_text('keep');
        with self.assertRaises(ValueError):self.package()
        self.assertEqual(marker.read_text(),'keep')
if __name__=='__main__':unittest.main()
