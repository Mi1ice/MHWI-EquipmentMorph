"""Synthetic conversion checks; no private assets, game or Blender calls."""
import copy,importlib.util,json,pathlib,subprocess,sys,tempfile,unittest
PATH=pathlib.Path(__file__).with_name('prepare_sdk_validation_catalog.py')
class ConversionTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(PATH.is_file(),'catalog conversion capability missing')
        spec=importlib.util.spec_from_file_location('prepare_validation',PATH);self.module=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.module)
        self.mapping=[dict(id='key_'+str(i),source_key='源_目_'+str(i),author_shading=False) for i in range(489)]
        ids=[k['id'] for k in self.mapping]
        self.proposal=dict(schema='mhwi-sdk-visual-case-proposal-v1',single_weight_curve_proposed=[0,.25,.5,.75,1,.75,.5,.25,0],
            single_cases=[dict(case_id='S01',source_key=self.mapping[0]['source_key'],key_id=ids[0])],
            combination_cases=[dict(case_id='C01',scheme='synthetic',fixed_weight=.5,fixed_keys=[dict(key_id=k['id'],source_key=k['source_key']) for k in self.mapping[:25]],varying_keys=[dict(key_id=k['id'],source_key=k['source_key']) for k in self.mapping[25:30]],varying_curve_proposed=[0,.25,.5,.75,1,.75,.5,.25,0],final_all_dynamic_weights=0)],
            asset_identity_by_mode={name:dict(model_id='pl/synthetic/body',model_sha256=a*64,pack_sha256=b*64) for name,a,b in [('BASELINE','a','b'),('GEOMETRIC','c','d')]})
        self.refs={}
        for name in ('BASELINE','GEOMETRIC'):
            identity=self.proposal['asset_identity_by_mode'][name]
            self.refs[name]=dict(identity,key_mapping=copy.deepcopy(self.mapping),cases=[dict(id='combo-synthetic-'+str(i),weights={**{k:.5 for k in ids[:25]},**{k:w for k in ids[25:30]}}) for i,w in enumerate([0,.25,.5,.75,1,.75,.5,.25,0])])
    def test_unicode_source_mapping_and_combinations(self):
        result=self.module.prepare(self.proposal,self.refs['BASELINE'],self.refs['GEOMETRIC'])
        self.assertEqual(result['expected_key_count'],489);self.assertEqual(len(result['labels']),30)
        self.assertEqual(result['labels'][0],dict(id='key_0',source='源_目_0'));self.assertEqual(len(result['cases'][1]['fixed']),25)
    def test_mismatched_mapping_rejected(self):
        self.refs['GEOMETRIC']['key_mapping'][0]['source_key']='different'
        with self.assertRaises(ValueError):self.module.prepare(self.proposal,self.refs['BASELINE'],self.refs['GEOMETRIC'])
    def test_combination_weights_rejected(self):
        self.refs['GEOMETRIC']['cases'][3]['weights']['key_0']=.75
        with self.assertRaises(ValueError):self.module.prepare(self.proposal,self.refs['BASELINE'],self.refs['GEOMETRIC'])
    def test_wrong_identity_and_missing_label_rejected(self):
        self.proposal['asset_identity_by_mode']['BASELINE']['pack_sha256']='f'*64
        with self.assertRaises(ValueError):self.module.prepare(self.proposal,self.refs['BASELINE'],self.refs['GEOMETRIC'])
    def test_source_label_and_duplicate_case_rejected(self):
        p=copy.deepcopy(self.proposal);p['single_cases'][0]['source_key']='wrong'
        with self.assertRaises(ValueError):self.module.prepare(p,self.refs['BASELINE'],self.refs['GEOMETRIC'])
        p=copy.deepcopy(self.proposal);p['single_cases'].append(copy.deepcopy(p['single_cases'][0]))
        with self.assertRaises(ValueError):self.module.prepare(p,self.refs['BASELINE'],self.refs['GEOMETRIC'])
    def test_cli_failure_leaves_no_catalog(self):
        temporary_root=PATH.parent.parent/'.build/sdk-validation/catalog-checks';temporary_root.mkdir(parents=True,exist_ok=True)
        for fault in ('mapping','weights'):
            with tempfile.TemporaryDirectory(dir=temporary_root) as temporary:
                p=pathlib.Path(temporary);g=copy.deepcopy(self.refs['GEOMETRIC'])
                self.assertTrue(p.resolve().is_relative_to(temporary_root.resolve()))
                if fault=='mapping':g['key_mapping'][0]['source_key']='wrong'
                else:g['cases'][0]['weights']['key_0']=.25
                for name,value in [('cases',self.proposal),('baseline',self.refs['BASELINE']),('geometric',g)]:
                    (p/(name+'.json')).write_text(json.dumps(value),encoding='utf-8')
                output=p/'out.json';result=subprocess.run([sys.executable,str(PATH),'--cases',str(p/'cases.json'),'--baseline-reference',str(p/'baseline.json'),'--geometric-reference',str(p/'geometric.json'),'--output',str(output)],capture_output=True)
                self.assertNotEqual(result.returncode,0);self.assertFalse(output.exists())
if __name__=='__main__':unittest.main()
