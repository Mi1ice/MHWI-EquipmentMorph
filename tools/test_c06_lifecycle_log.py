"""Evidence gates for C06; fixture records model real worker JSON fields."""
import json
import hashlib
import tempfile
import unittest
from pathlib import Path
from check_c06_lifecycle_log import evaluate, read_segment, summarize


class C06Tests(unittest.TestCase):
    def setUp(self):
        self.asset={'part':1,'model_id':'pl/A','model_sha256':'a'*64,'asset_sha256':'b'*64,'keys':['same'],'test_key_index':0,'role':'A'}
        self.manifest={'assets':[self.asset], 'session':{'pid':42,'process_creation_filetime':123,'exe_sha256':'c'*64,'runtime_sha256':'d'*64,'loaded_identity_verified':True},'software':{'status':'passed'}}
        self.header={'pid':42,'process_creation_filetime':123,'exe_sha256':'c'*64}
        self.case={'case_id':'basic','kind':'basic','part':1,'sequence':[0,.5,1,.5,0]}
        self.obs={'basic':{'status':'passed'}}
        self.records=[self.state(w,tick=i*1000) for i,w in enumerate([0,.5,1,.5,0])]

    def state(self,w,**extra):
        return dict(event='part_state',part=1,epoch=1,target=10,live=True,ready=True,started=True,
                    model_id='pl/A',model_sha256='a'*64,pack_sha256='b'*64,requested=[w],applied=[w],
                    requested_frame=7,applied_frame=7,requested_revision=8,applied_revision=8,
                    native_workers=8,cycle_open=False,completed_signals=5,joined_boundaries=5,observed_tick=1000,**extra)

    def run_case(self):return evaluate(self.manifest,self.case,self.records,self.obs,self.header)
    def test_normal_basic(self):self.assertEqual(self.run_case()['status'],'passed')
    def test_unobserved_visual(self):
        self.obs={};result=self.run_case();self.assertEqual(result['status'],'not_observed');self.assertEqual(result['live_log']['status'],'passed')
    def test_failed_visual(self):self.obs['basic']['status']='failed';self.assertEqual(self.run_case()['status'],'failed')
    def test_cross_pid(self):self.header['pid']=43;self.assertEqual(self.run_case()['status'],'failed')
    def test_cross_process_creation(self):self.header['process_creation_filetime']=124;self.assertEqual(self.run_case()['status'],'failed')
    def test_cross_version(self):self.header['exe_sha256']='e'*64;self.assertEqual(self.run_case()['status'],'failed')
    def test_unverified_loaded_module(self):
        self.manifest['session']['loaded_identity_verified']=False;self.assertEqual(self.run_case()['status'],'not_observed')
    def test_missing_runtime_evidence(self):self.records=[];self.assertEqual(self.run_case()['status'],'not_observed')
    def test_accepted_is_not_applied(self):
        for s in self.records:s['applied']=[0];s['applied_revision']=0
        self.assertEqual(self.run_case()['status'],'not_observed')
    def test_no_cross_target_sequence(self):
        self.records[2]['target']=99;self.assertEqual(self.run_case()['status'],'not_observed')
    def test_wrong_pack(self):self.records[0]['pack_sha256']='e'*64;self.assertEqual(self.run_case()['status'],'failed')
    def test_nan_rejected(self):self.records[1]['requested']=[float('nan')];self.assertEqual(self.run_case()['status'],'failed')
    def test_cycle_failure(self):
        self.records.append({'event':'launch_identity_rejected','part':1,'observed_workers':1,'expected_workers':8})
        self.assertEqual(self.run_case()['status'],'failed')
    def switch_fixture(self):
        self.case={'case_id':'basic','kind':'switch_U','part':1,'rounds':1,'leave_weights':[.5]}
        self.obs['basic'].update(equipment_sequence_confirmed=True,unpackaged_interval_observed=True)
        self.records=[self.state(.5),{'event':'public_part_retired','part':1,'epoch':1,'target':10,'old_handle_stale':True},
          {'event':'part_retirement_complete','part':1,'epoch':1,'successor_joined':True,'covered_signal':5,'draw_completed':5,'upload_floor':20,'upload_completed':20,'part_buffers':3},
          dict(self.state(0),target=11,epoch=2,event='part_ready_original_inputs'),dict(self.state(.5),target=11,epoch=2),dict(self.state(0),target=11,epoch=2)]
    def test_stale_and_gpu_retirement(self):self.switch_fixture();self.assertEqual(self.run_case()['status'],'passed')
    def test_missing_retirement(self):
        self.switch_fixture();self.records.pop(2);self.assertEqual(self.run_case()['status'],'not_observed')
    def test_missing_stale(self):
        self.switch_fixture();self.records[1]['old_handle_stale']=False;self.assertEqual(self.run_case()['status'],'failed')
    def test_incomplete_gpu_proof(self):
        self.switch_fixture();self.records[2]['draw_completed']=4;self.assertEqual(self.run_case()['status'],'failed')
    def test_nonzero_inherited(self):
        self.switch_fixture();self.records[3]['requested']=[.5];self.assertEqual(self.run_case()['status'],'failed')
    def test_rounds_not_collapsed(self):
        self.switch_fixture();self.case.update(rounds=3,leave_weights=[.5,0,.5]);self.assertEqual(self.run_case()['status'],'not_observed')
    def test_same_name_B_requires_distinct_identity(self):
        self.switch_fixture();self.case['kind']='switch_B';self.manifest['assets'].append(dict(self.asset,model_id='pl/B',model_sha256='e'*64,asset_sha256='f'*64,role='B'))
        self.assertEqual(self.run_case()['status'],'not_observed')
    def test_U_readiness_does_not_prove_renewed_control(self):
        self.switch_fixture();self.records=self.records[:4];self.assertEqual(self.run_case()['status'],'not_observed')
    def test_U_interval_requires_user_equipment_attestation(self):
        self.switch_fixture();del self.obs['basic']['unpackaged_interval_observed'];self.assertEqual(self.run_case()['status'],'not_observed')
    def title_fixture(self):
        self.manifest['assets']=[dict(self.asset,part=p) for p in range(5)]
        self.case={'case_id':'basic','kind':'title','parts':list(range(5))}
        self.records=[{'event':'automatic_main_player_changed','waiting':True}]
        for p in range(5):
            self.records += [dict(self.state(.5),part=p),
                {'event':'public_part_retired','part':p,'epoch':1,'target':10,'old_handle_stale':True},
                {'event':'part_retirement_complete','part':p,'epoch':1,'successor_joined':True,'covered_signal':5,'draw_completed':5,'upload_floor':20,'upload_completed':20,'part_buffers':3},
                dict(self.state(0),part=p,target=20+p,epoch=2,event='part_ready_original_inputs'),
                dict(self.state(.5),part=p,target=20+p,epoch=2),dict(self.state(0),part=p,target=20+p,epoch=2)]
    def test_title_requires_controls_after_fresh_readiness(self):
        self.title_fixture();self.assertEqual(self.run_case()['status'],'passed')
        old=[];retire=[]
        for p in range(5):
            old += [dict(self.state(w),part=p,event='part_ready_original_inputs' if w==0 else 'part_state') for w in (0,.5,0)]
        retire=[r for r in self.records if r.get('event') in ('public_part_retired','part_retirement_complete')]
        self.records=old+[{'event':'automatic_main_player_changed','waiting':True}]+retire
        self.assertEqual(self.run_case()['status'],'not_observed')
    def test_title_reused_target_or_epoch_fails(self):
        self.title_fixture()
        for r in self.records:
            if r.get('event')=='part_ready_original_inputs':r['target']=10
        self.assertEqual(self.run_case()['status'],'failed')
    def test_restore_rejects_pre_retirement_controls(self):
        self.switch_fixture();self.case['kind']='restore'
        self.obs['basic'].update(sources_restored=True,source_attribution_confirmed=True,manual_override_observed=True,release_to_daily_source_observed=True)
        self.records=[dict(self.state(0),event='part_ready_original_inputs'),self.state(.5),self.state(0)]+self.records[1:3]
        self.assertEqual(self.run_case()['status'],'not_observed')
    def test_optional_history_stops_only_after_observed_success(self):
        cases=[{'case_id':'h1','kind':'history','bounded_attempt':1},{'case_id':'h2','kind':'history','bounded_attempt':2,'optional_after_thread_observed':True}]
        results=[{'case_id':'h1','status':'passed','thread_transition':{'status':'passed'}},{'case_id':'h2','status':'not_run','thread_transition':{'status':'not_observed'}}]
        self.assertTrue(summarize(cases,results)['C06_complete'])
        results[1]['status']='failed';self.assertFalse(summarize(cases,results)['C06_complete'])
        results[1]['status']='not_run';results[0]['thread_transition']['status']='not_observed'
        self.assertFalse(summarize(cases,results)['matrix_passed'])
    def test_five_parts_independently(self):
        for part in range(5):
            self.asset['part']=part;self.case['part']=part
            for s in self.records:s['part']=part
            self.assertEqual(self.run_case()['status'],'passed')
    def test_coexist_window(self):
        self.manifest['assets']=[dict(self.asset,part=p) for p in range(5)]
        self.case={'case_id':'basic','kind':'coexist','weights':[.25,.5,.75,1,.25],'edit_weights':[.5,.75,.5,.5,.5],'hold_ms':30000}
        self.records=[dict(self.state(w,tick=t),part=p,observed_tick=t) for t in (1000,32000) for p,w in enumerate(self.case['weights'])]
        held=list(self.records)
        current=list(self.case['weights']);tick=33000
        for part,edit in enumerate(self.case['edit_weights']):
            for value in (edit,0):
                current[part]=value
                self.records += [dict(self.state(w),part=p,observed_tick=tick) for p,w in enumerate(current)];tick+=2000
        self.assertEqual(self.run_case()['status'],'passed')
        self.records=held;self.assertEqual(self.run_case()['status'],'not_observed')
    def test_restore_requires_replacement_and_release(self):
        self.case={'case_id':'basic','kind':'restore','part':1}
        self.obs['basic'].update(sources_restored=True,source_attribution_confirmed=True)
        self.assertEqual(self.run_case()['status'],'not_observed')
    def test_legal_default_restore(self):
        self.switch_fixture();self.case={'case_id':'basic','kind':'restore','part':1}
        self.obs['basic'].update(sources_restored=True,source_attribution_confirmed=True)
        self.obs['basic'].update(manual_override_observed=True,release_to_daily_source_observed=True)
        self.records += [dict(self.state(w),target=11,epoch=2) for w in (.25,.75,.25)]
        self.assertEqual(self.run_case()['status'],'passed')
        del self.obs['basic']['source_attribution_confirmed'];self.assertEqual(self.run_case()['status'],'not_observed')
    def test_thread_not_triggered(self):
        self.case={'case_id':'basic','kind':'history','parts':[1]};self.records=[self.state(0)]
        result=self.run_case();self.assertEqual(result['thread_transition']['status'],'not_observed')
    def test_closed_cycles_only_for_thread_transition(self):
        self.case={'case_id':'basic','kind':'history','parts':[1]}
        self.records=[self.state(0),dict(self.state(.5),native_workers=1,completed_signals=9,joined_boundaries=9,cycle_open=True)]
        self.assertEqual(self.run_case()['thread_transition']['status'],'not_observed')
        self.records[-1]['cycle_open']=False
        self.assertEqual(self.run_case()['thread_transition']['status'],'passed')
    def test_no_software_proof(self):
        self.manifest['software']={'status':'not_run'};self.assertEqual(self.run_case()['status'],'not_observed')
    def test_log_prefix_and_pid_marker(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]/'.build') as td:
            p=Path(td)/'runtime.ndjson';p.write_text(json.dumps(self.header)+'\n'+json.dumps(self.records[0])+'\n',encoding='utf-8')
            marks=[{'case_id':'basic','phase':phase,'pid':42,'log_file':str(p),'next_complete_line':line,'runtime_sha256':'d'*64} for phase,line in [('begin',2),('end',3)]]
            for m in marks:m['prefix_sha256']=hashlib.sha256(b''.join(p.read_bytes().splitlines(keepends=True)[:m['next_complete_line']-1])).hexdigest()
            header,records,issues=read_segment(self.manifest,self.case,marks,[p]);self.assertFalse(issues);self.assertEqual(len(records),1)
            marks[1]['pid']=43;self.assertTrue(read_segment(self.manifest,self.case,marks,[p])[2])
    def test_rotation_rejected(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]/'.build') as td:
            p=Path(td)/'runtime.ndjson';p.write_text(json.dumps(self.header)+'\n'+json.dumps({'event':'bounded_log_rotated'})+'\n',encoding='utf-8')
            marks=[{'case_id':'basic','phase':phase,'pid':42,'log_file':str(p),'next_complete_line':line,'runtime_sha256':'d'*64} for phase,line in [('begin',2),('end',3)]]
            for m in marks:m['prefix_sha256']=hashlib.sha256(b''.join(p.read_bytes().splitlines(keepends=True)[:m['next_complete_line']-1])).hexdigest()
            self.assertTrue(read_segment(self.manifest,self.case,marks,[p])[2])


if __name__=='__main__':unittest.main()
