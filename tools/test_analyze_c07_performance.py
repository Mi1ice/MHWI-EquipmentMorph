import unittest
try:
    import analyze_c07_performance as a
except ImportError:
    a=None
class AnalysisTests(unittest.TestCase):
    def test_metrics_required(self):
        self.assertIsNotNone(a,'C07 analyzer unavailable')
    def test_slow_frames_and_fps(self):
        if a is None:self.skipTest('missing implementation')
        s=a.statistics([1,2,3,100]);self.assertEqual(s['mean'],26.5);self.assertAlmostEqual(s['p95'],85.45);self.assertAlmostEqual(s['p99'],97.09);self.assertAlmostEqual(s['fps'],1000/26.5)
    def test_missing_is_not_zero(self):
        if a is None:self.skipTest('missing implementation')
        self.assertIsNone(a.statistics([]));self.assertRaises(ValueError,a.statistics,[1,float('nan')])
    def test_segments_equal_weight(self):
        if a is None:self.skipTest('missing implementation')
        self.assertEqual(a.equal_segments([[1]*100,[3]])['mean'],2)
    def test_budget_requires_bound_direct_approval(self):
        if a is None:self.skipTest('missing implementation')
        self.assertEqual(a.judge({'mean':1},None,None,'abc'),'measurement-only')
        budget={'mean':2};approval={'approved':True,'proposal_sha256':'wrong','source':'direct_user_message'}
        self.assertRaises(ValueError,a.judge,{'mean':1},budget,approval,'abc')
    def test_invalid_and_mixed_identity_gate(self):
        if a is None:self.skipTest('missing implementation')
        meta={'pid':'42','swapchain':'0x1','time_column':'QPC','time_scale':1,'start_qpc':1000,'end_qpc':5000,'frame_column':'MsBetweenPresents','present_mode':'Independent Flip'}
        def row(t,ms,pid='42',sc='0x1'):return {'ProcessID':pid,'SwapChainAddress':sc,'QPC':str(t),'MsBetweenPresents':str(ms),'PresentMode':'Independent Flip'}
        result=a.frames([row(1000,1),row(2000,100),row(3000,'NaN'),row(4000,2,'41'),row(4500,2,'42','0x2')],meta)
        self.assertEqual(result['statistics']['mean'],50.5);self.assertEqual(result['invalid_rows'],1);self.assertEqual(result['foreign_rows'],2);self.assertFalse(result['quality_valid'])
        self.assertRaises(ValueError,a.frames,[row(2000,1),row(1000,2)],meta)
    def test_response_never_interpolates_missing_or_superseded(self):
        if a is None:self.skipTest('missing implementation')
        accepts=[{'revision':10,'part':0,'qpc':100},{'revision':11,'part':0,'qpc':110},{'revision':12,'part':1,'qpc':120}]
        applied=[{'revision':11,'part':0,'qpc':150}]
        r=a.responses(accepts,applied,1000);self.assertEqual(r['observed'],1);self.assertEqual(r['unobserved'],2);self.assertEqual(r['latency_ms']['mean'],40)
        self.assertRaises(ValueError,a.responses,accepts,[{'revision':11,'part':0,'qpc':90}],1000)
        self.assertRaises(ValueError,a.responses,accepts,applied,0)
    def test_raw_distribution_incomplete_has_no_percentiles(self):
        if a is None:self.skipTest('missing implementation')
        x=a.worker({'calls':300,'valid':300,'invalid':0,'saturated':False,'total_qpc':300,'dropped':44,'raw_qpc':[1]*256,'qpc_frequency':1000})
        self.assertEqual(x['mean_ms'],1);self.assertIsNone(x['p95_ms'])
    def test_state_proof_is_bound_to_process_clock_window_and_assets(self):
        if a is None:self.skipTest('missing implementation')
        self.assertTrue(callable(getattr(a,'state_proof',None)),'state proof validator missing')
        meta=dict(pid=42,process_creation_filetime=123,qpc_frequency=1000,state='S',inputs_sha256='frozen',start_qpc=16000,end_qpc=36000)
        scheduled=dict(event='run_scheduled',pid=42,process_creation_filetime=123,qpc_frequency=1000,state='S',inputs_sha256='frozen',record_start_qpc=16000,record_end_qpc=36000)
        rows=[scheduled,dict(event='source_opened',qpc=1000,handles=[1,2,3,4,5])]
        rows += [dict(event='accepted',part=p,revision=10,qpc=1001) for p in range(5)]
        rows += [dict(event='applied_observed',part=p,revision=10,qpc=1200) for p in range(5)]
        rows += [dict(event='heartbeat',qpc=t) for t in range(1500,36000,500)]
        rows += [dict(event='source_closed',qpc=36001,result=0)]
        self.assertTrue(a.state_proof(rows,meta)['valid'])
        for field,value in [('pid',41),('qpc_frequency',999),('inputs_sha256','other'),('process_creation_filetime',124)]:
            changed=[dict(scheduled,**{field:value})]+rows[1:]
            self.assertRaises(ValueError,a.state_proof,changed,meta)
        self.assertRaises(ValueError,a.state_proof,rows[:-1],meta)
        self.assertRaises(ValueError,a.state_proof,rows[:-1]+[dict(event='run_failed_source_close_attempted_lease_backstop',qpc=36001)],meta)
if __name__=='__main__':unittest.main()
