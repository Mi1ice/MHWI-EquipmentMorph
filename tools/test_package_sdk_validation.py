"""Package checks with a non-executable synthetic PE header and synthetic catalog."""
import copy,datetime,hashlib,importlib.util,json,os,pathlib,struct,subprocess,tempfile,unittest
PATH=pathlib.Path(__file__).with_name('package_sdk_validation.py');ROOT=PATH.parent.parent
class PackageTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(PATH.is_file(),'SDK validation packager capability missing')
        spec=importlib.util.spec_from_file_location('package_validation',PATH);self.module=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.module)
        self.base=ROOT/'.build/sdk-validation/package-checks';self.base.mkdir(parents=True,exist_ok=True)
        self.temporary=tempfile.TemporaryDirectory(dir=self.base);self.folder=pathlib.Path(self.temporary.name);self.assertTrue(self.folder.resolve().is_relative_to(self.base.resolve()));self.addCleanup(self.temporary.cleanup)
        self.build=self.folder/'build';self.build.mkdir();self.output=self.folder/'candidate';self.catalog=self.folder/'catalog.json'
        labels=[dict(id='key_'+str(i),source='完整な源名_'+str(i)) for i in range(30)]
        self.data=dict(schema='mhwi-sdk-validation-catalog-v1',part=1,model_id='pl/synthetic/body',expected_key_count=489,
            modes=[dict(name=n,model_sha256=a*64,pack_sha256=b*64) for n,a,b in [('BASELINE','a','b'),('GEOMETRIC','c','d')]],labels=labels,
            cases=[dict(id='S01',title='单键',fixed=[],varying=['key_0']),dict(id='C01',title='组合',fixed=[dict(id=l['id'],weight=.5) for l in labels[:25]],varying=[l['id'] for l in labels[25:]])],steps=[0,.25,.5,.75,1,.75,.5,.25,0])
        self.catalog.write_text(json.dumps(self.data,ensure_ascii=False),encoding='utf-8')
        payload=bytearray(1024);payload[:2]=b'MZ';struct.pack_into('<I',payload,0x3c,0x80);payload[0x80:0x84]=b'PE\0\0';struct.pack_into('<H',payload,0x84,0x8664);struct.pack_into('<H',payload,0x94,0xf0);struct.pack_into('<H',payload,0x96,0x2000);struct.pack_into('<H',payload,0x98,0x20b)
        self.dll=self.build/'MHWEquipmentMorphSdkValidation.dll';self.dll.write_bytes(payload)
        source_hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in self.module.required_sources()}
        commit=subprocess.run(['git','-C',str(ROOT),'rev-parse','HEAD'],capture_output=True,text=True,check=True).stdout.strip()
        checks={n:dict(passed=True,checks=count) for n,count in [('case_tests',31),('catalog_conversion',6),('sdk_session_tests',633),('controller_tests',171),('window_model_tests',61)]}
        checks['sdk_regression']=dict(passed=True,checks=14,capacity_groups=4);checks['dll_build']=dict(passed=True,loaded=False,window_created=False)
        self.report=dict(schema='mhwi-sdk-validation-build-v1',stage='All',software_checks_passed=True,completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),source_commit=commit,source_sha256=source_hashes,checks=checks,
            game_tested=False,gui_tested=False,visual_acceptance_complete=False,formal_acceptance_complete=False,dll=dict(file=self.dll.name,bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest()))
        self.report_path=self.build/'all-report.json';self.save_report()
    def save_report(self):self.report_path.write_text(json.dumps(self.report),encoding='utf-8')
    def package(self):return self.module.package(self.build,self.catalog,self.output)
    def rejected(self):
        with self.assertRaises((ValueError,OSError)):self.package()
        self.assertFalse(self.output.exists(),'failed input must not produce a partial candidate')
    def test_complete_candidate_payload_and_hashes(self):
        manifest=self.package();self.assertEqual(set(p.name for p in self.output.iterdir()),{'MHWEquipmentMorphSdkValidation.dll','MHWEquipmentMorphSdkValidation.cases.json','README.md','all-report.json','manifest.json'})
        for name,value in manifest['files'].items():self.assertEqual(hashlib.sha256((self.output/name).read_bytes()).hexdigest(),value['sha256'])
        self.assertEqual((self.output/'MHWEquipmentMorphSdkValidation.cases.json').read_bytes(),self.catalog.read_bytes());self.assertFalse(manifest['game_tested']);self.assertFalse(manifest['visual_acceptance_complete'])
    def test_missing_dll(self):self.dll.unlink();self.rejected()
    def test_changed_dll(self):self.dll.write_bytes(b'wrong');self.rejected()
    def test_non_x64_dll(self):
        payload=bytearray(self.dll.read_bytes());struct.pack_into('<H',payload,0x84,0x14c);self.dll.write_bytes(payload);self.report['dll']['sha256']=hashlib.sha256(payload).hexdigest();self.save_report();self.rejected()
    def test_failed_build(self):self.report['software_checks_passed']=False;self.save_report();self.rejected()
    def test_missing_stage(self):del self.report['checks']['controller_tests'];self.save_report();self.rejected()
    def test_source_mismatch(self):self.report['source_sha256']['controls/MHWI-EquipmentMorph-SdkValidation/SdkSession.hpp']='0'*64;self.save_report();self.rejected()
    def test_incomplete_source_manifest(self):del self.report['source_sha256']['controls/MHWI-EquipmentMorph-SdkValidation/SdkSession.hpp'];self.save_report();self.rejected()
    def test_wrong_commit(self):self.report['source_commit']='f'*40;self.save_report();self.rejected()
    def test_stale_report(self):os.utime(self.report_path,(1,1));self.rejected()
    def test_missing_identity(self):self.data['modes'][0]['pack_sha256']='';self.catalog.write_text(json.dumps(self.data),encoding='utf-8');self.rejected()
    def test_duplicate_cases(self):self.data['cases'].append(copy.deepcopy(self.data['cases'][0]));self.catalog.write_text(json.dumps(self.data),encoding='utf-8');self.rejected()
    def test_existing_output_preserved(self):
        self.output.mkdir();marker=self.output/'keep.txt';marker.write_text('keep',encoding='utf-8')
        with self.assertRaises(ValueError):self.package()
        self.assertEqual(marker.read_text(encoding='utf-8'),'keep');self.assertEqual(len(list(self.output.iterdir())),1)
if __name__=='__main__':unittest.main()
