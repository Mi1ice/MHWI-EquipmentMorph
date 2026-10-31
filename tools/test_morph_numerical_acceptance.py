"""Behavioural tests for approved position-rule evaluation, with small synthetic reports."""
import hashlib
import json
import math
from pathlib import Path
import tempfile
import unittest
from check_morph_numerical_acceptance import evaluate

class AcceptanceTests(unittest.TestCase):
    def setUp(self):
        parent=Path(__file__).resolve().parents[1]/'.build/numerical-acceptance-tests'
        parent.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(dir=parent)
        def clean_fixture():
            if not Path(self.temp.name).resolve().is_relative_to(parent.resolve()):
                raise ValueError('Test cleanup escaped its dedicated directory')
            self.temp.cleanup()
        self.addCleanup(clean_fixture)
        self.root=Path(self.temp.name);self.files={}
        self.put('comparator.exe',b'comparator');self.put('product/runtime/core.hpp',b'core')
        self.proposal=dict(schema='mhwi-c03-tolerance-proposal-v1',metric='max over every serialized vertex of Euclidean xyz distance',
            unit='MOD3 exported coordinate unit',absolute_limit=.0005,comparison='<=',applicability=dict(keys_per_asset=1,cases_per_mode=6,serialized_vertices=1,weights=[0,.25,.5,.75,1]))
        self.approval=dict(schema='mhwi-c03-numerical-approval-v1',status='approved',source='direct_user_message',user_reply='批准，继续',
            accepted_rule={k:self.proposal[k] for k in ('metric','unit','absolute_limit','comparison')})
        self.source=dict(schema='mhwi-c03-source-audit-v1',product_head='base',current_comparator_dependencies={'runtime/core.hpp':self.digest('product/runtime/core.hpp')})
        self.inputs=dict(schema='mhwi-c03-input-audit-v1',modes={})
        self.replay=dict(schema='mhwi-c03-current-replay-v1',product_head='base',executable_sha256=self.digest('comparator.exe'),modes={},total_mode_cases=12)
        self.references={};self.reports={}
        for mode in ('BASELINE','GEOMETRIC'):
            for name,data in [('model.mod3',b'model'),('model.mhmprofile',b'profile'),('asset.mhmpack',mode.encode()),('trace.json',b'{}'),('positions.bin',b'positions')]:self.put(mode+'/'+name,data)
            cases=[dict(id='baseline',kind='baseline',weights={})]+[dict(id='key@'+str(w),kind='single',key_id='key',weights={'key':w}) for w in (0,.25,.5,.75,1)]
            for i,case in enumerate(cases):case['byte_offset']=i*24
            ref=dict(schema='mhwi-morph-position-reference-v1',model_id='pl/test',key_mapping=[dict(id='key',source_key='key')],position_offsets=[0],cases=cases,
                     source_restored=True,blend_saved=False)
            for stem,name in [('model','model.mod3'),('profile','model.mhmprofile'),('trace','trace.json'),('positions','positions.bin')]:
                ref[stem+'_file']=name;ref[stem+'_sha256']=self.digest(mode+'/'+name)
            ref['pack_sha256']=self.digest(mode+'/asset.mhmpack')
            stats=dict(max=.0005,mean=0.,rms=0.,p50=0.,p95=0.,p99=0.,worst_serialized_vertex=0,worst_position_offset=0)
            self.references[mode]=ref
            self.reports[mode]=dict(schema='mhwi-morph-position-comparison-v1',status='measurement_completed_threshold_not_set',tolerance_set=False,
                key_count=1,position_count=1,pack_sha256=ref['pack_sha256'],zero_restored=True,non_position_valid=True,
                cases=[dict(id=c['id'],composition_error=dict(stats)) for c in cases],max_composition_error=.0005)
            self.inputs['modes'][mode]=dict(reference=str(self.root/mode/'reference.json'),keys=1,vertices=1,zero_cases=2,kinds=dict(baseline=1,single=5),max_active_keys=1)
            self.replay['modes'][mode]=dict(cases=6,zero_cases=2,zero_basis_bytes_exact=True,unrelated_bytes_preserved=True,max_composition_error=.0005)
        self.rule=dict(schema='mhwi-c03-approved-rule-v1',status='approved',approval='approval.json',proposal='proposal.json',inputs='inputs.json',source_audit='source.json',
            replay='replay.json',comparator='comparator.exe',source_root='product',modes={m:dict(reference=m+'/reference.json',comparison=m+'/comparison.json',pack=m+'/asset.mhmpack') for m in self.references})
        self.seal()

    def put(self,name,value):
        path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(value if isinstance(value,bytes) else (json.dumps(value)+'\n').encode())
        self.files[name]=path

    def digest(self,name):return hashlib.sha256((self.root/name).read_bytes()).hexdigest()

    def seal(self):
        for mode in self.references:
            self.put(mode+'/reference.json',self.references[mode]);self.put(mode+'/comparison.json',self.reports[mode])
            self.replay['modes'][mode]['report_sha256']=self.digest(mode+'/comparison.json')
        self.inputs['artifacts']=[dict(path=name,sha256=self.digest(name),bytes=path.stat().st_size)
                                 for name,path in self.files.items() if name.startswith(('BASELINE/','GEOMETRIC/'))]
        self.put('inputs.json',self.inputs);self.put('source.json',self.source)
        self.replay['inputs_sha256']=self.digest('inputs.json');self.replay['source_audit_sha256']=self.digest('source.json');self.put('replay.json',self.replay)
        for field,name in [('frozen_input_audit_sha256','inputs.json'),('current_source_audit_sha256','source.json'),('replay_sha256','replay.json')]:self.proposal['applicability'][field]=self.digest(name)
        self.put('proposal.json',self.proposal);self.approval['proposal_sha256']=self.digest('proposal.json');self.put('approval.json',self.approval)
        self.rule['bindings']={name:self.digest(name) for name in self.files if name!='rule.json'}
        self.put('rule.json',self.rule)

    def run_rule(self):return evaluate(self.root,self.root/'rule.json')

    def test_exact_absolute_boundary_is_inclusive(self):
        result=self.run_rule();self.assertEqual(result['status'],'passed');self.assertEqual(result['total_cases'],12)

    def test_one_vertex_above_cap_fails_even_with_zero_mean_and_percentiles(self):
        self.reports['BASELINE']['cases'][2]['composition_error']['max']=math.nextafter(.0005,math.inf)
        self.reports['BASELINE']['max_composition_error']=math.nextafter(.0005,math.inf)
        self.replay['modes']['BASELINE']['max_composition_error']=math.nextafter(.0005,math.inf);self.seal()
        result=self.run_rule();self.assertEqual(result['status'],'failed');self.assertEqual(result['modes']['BASELINE']['failed_cases'],1)

    def test_near_zero_has_no_relative_error_denominator(self):
        for report in self.reports.values():
            for case in report['cases']:case['composition_error']['max']=1e-12
            report['max_composition_error']=1e-12
        for row in self.replay['modes'].values():row['max_composition_error']=1e-12
        self.seal();self.assertEqual(self.run_rule()['status'],'passed')

    def test_unapproved_record_cannot_pass(self):
        self.approval['status']='pending';self.seal()
        with self.assertRaisesRegex(ValueError,'approval'):self.run_rule()

    def test_missing_approval_cannot_pass(self):
        (self.root/'approval.json').unlink()
        with self.assertRaises((ValueError,OSError)):self.run_rule()

    def test_wrong_unit_cannot_pass_even_when_approval_repeats_it(self):
        self.proposal['unit']='millimeter';self.approval['accepted_rule']['unit']='millimeter';self.seal()
        with self.assertRaisesRegex(ValueError,'unit'):self.run_rule()

    def test_nonfinite_statistic_cannot_pass(self):
        self.reports['BASELINE']['cases'][0]['composition_error']['max']=float('nan');self.seal()
        with self.assertRaisesRegex(ValueError,'finite'):self.run_rule()

    def test_boolean_is_not_a_position_error(self):
        self.reports['BASELINE']['cases'][0]['composition_error']['max']=False;self.seal()
        with self.assertRaises(ValueError):self.run_rule()

    def test_zero_byte_failure_cannot_be_hidden_by_small_position_error(self):
        self.reports['BASELINE']['zero_restored']=False;self.seal()
        with self.assertRaisesRegex(ValueError,'zero'):self.run_rule()

    def test_unrelated_field_failure_cannot_pass(self):
        self.reports['GEOMETRIC']['non_position_valid']=False;self.seal()
        with self.assertRaisesRegex(ValueError,'field'):self.run_rule()

    def test_missing_case_cannot_pass_after_report_hash_is_rebound(self):
        self.reports['BASELINE']['cases'].pop();self.seal()
        with self.assertRaisesRegex(ValueError,'case'):self.run_rule()

    def test_duplicate_case_cannot_pass(self):
        self.reports['BASELINE']['cases'][1]['id']='baseline';self.seal()
        with self.assertRaisesRegex(ValueError,'case'):self.run_rule()

    def test_summary_max_cannot_hide_case_error(self):
        self.reports['BASELINE']['max_composition_error']=0;self.seal()
        with self.assertRaisesRegex(ValueError,'summary'):self.run_rule()

    def test_reference_weight_coverage_cannot_be_silently_removed(self):
        self.references['BASELINE']['cases'][3]['weights']['key']=.25;self.seal()
        with self.assertRaisesRegex(ValueError,'single'):self.run_rule()

    def test_bound_file_tampering_cannot_pass(self):
        (self.root/'BASELINE/positions.bin').write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError,'digest'):self.run_rule()

    def test_wrong_reference_pack_identity_cannot_pass(self):
        self.references['BASELINE']['pack_sha256']='0'*64;self.seal()
        with self.assertRaisesRegex(ValueError,'pack'):self.run_rule()

    def test_rebinding_changed_reference_cannot_detach_from_approved_input_audit(self):
        # Updating the rule's file hash alone must not authorize another
        # reference while the approved input audit and replay stay unchanged.
        self.references['BASELINE']['model_id']='pl/wrong_model'
        self.put('BASELINE/reference.json',self.references['BASELINE'])
        self.rule['bindings']['BASELINE/reference.json']=self.digest('BASELINE/reference.json')
        self.put('rule.json',self.rule)
        with self.assertRaisesRegex(ValueError,'Frozen input digest'):self.run_rule()

    def test_alternate_reference_cannot_replace_the_approved_mode_reference(self):
        alternate=dict(self.references['BASELINE'],model_id='pl/wrong_model')
        self.put('BASELINE/alternate-reference.json',alternate)
        self.rule['bindings']['BASELINE/alternate-reference.json']=self.digest('BASELINE/alternate-reference.json')
        self.rule['modes']['BASELINE']['reference']='BASELINE/alternate-reference.json'
        self.put('rule.json',self.rule)
        with self.assertRaisesRegex(ValueError,'approved mode reference'):self.run_rule()

if __name__=='__main__':unittest.main()
