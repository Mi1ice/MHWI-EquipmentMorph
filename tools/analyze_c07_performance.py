"""C07 measurement-only primitives. Raw inputs are never rewritten or trimmed.

Elapsed QPC is not thread CPU time; SDK observed application is not screen latency.
Budget approval must bind the exact proposal file digest before any judgement.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
from statistics import mean, median

def quantile(values,p):
    x=sorted(values);at=(len(x)-1)*p;i=int(at)
    return x[i]+(x[min(i+1,len(x)-1)]-x[i])*(at-i)

def statistics(values):
    if not values:return None
    if any(not math.isfinite(x) or x<0 for x in values):raise ValueError('nonfinite/negative measurement')
    m=mean(values)
    return dict(count=len(values),mean=m,median=median(values),p95=quantile(values,.95),p99=quantile(values,.99),maximum=max(values),fps=1000/m if m else None)

def equal_segments(segments):
    stats=[statistics(s) for s in segments]
    if not stats or any(s is None for s in stats):raise ValueError('empty segment')
    return {k:mean(s[k] for s in stats) for k in ['mean','median','p95','p99']}

def frames(rows,meta):
    values=[];invalid=foreign=outside=0;previous=None
    for row in rows:
        if row['ProcessID']!=str(meta['pid']) or row['SwapChainAddress']!=meta['swapchain']:
            foreign+=1;continue
        if row['PresentMode']!=meta['present_mode']:raise ValueError('presentation mode changed')
        t=float(row[meta['time_column']])*meta['time_scale']
        if not math.isfinite(t) or (previous is not None and t<previous):raise ValueError('invalid/nonmonotonic frame clock')
        previous=t
        if not meta['start_qpc']<=t<meta['end_qpc']:outside+=1;continue
        try:v=float(row[meta['frame_column']])
        except (ValueError,TypeError):invalid+=1;continue
        if not math.isfinite(v) or v<=0:invalid+=1;continue
        values.append(v)
    return dict(statistics=statistics(values),invalid_rows=invalid,foreign_rows=foreign,outside_window=outside,quality_valid=bool(values) and not invalid and not foreign,slow_frames_retained=True)

def worker(row):
    f=row['qpc_frequency'];n=row['valid'];raw=row['raw_qpc']
    if f<=0 or row['saturated'] or row['invalid'] or not n:return dict(mean_ms=None,p95_ms=None,maximum_ms=None,reason='invalid/empty/saturated')
    if row['calls']!=n or len(raw)>n or any(v<0 for v in raw):raise ValueError('worker counts inconsistent')
    complete=not row['dropped'] and len(raw)==n
    return dict(mean_ms=1000*row['total_qpc']/f/n,p95_ms=1000*quantile(raw,.95)/f if complete else None,maximum_ms=1000*max(raw)/f if complete else None,distribution_complete=complete)

def responses(accepts,applied,frequency):
    if frequency<=0:raise ValueError('QPC frequency missing')
    pending={};latencies=[]
    for a in accepts:
        k=(a['part'],a['revision'])
        if k in pending:raise ValueError('duplicate accepted revision')
        pending[k]=a['qpc']
    seen=set()
    for r in applied:
        k=(r['part'],r['revision'])
        if k in seen or k not in pending:continue
        if r['qpc']<pending[k]:raise ValueError('clock domain/order mismatch')
        seen.add(k);latencies.append(1000*(r['qpc']-pending[k])/frequency)
    return dict(accepted=len(pending),observed=len(seen),unobserved=len(pending)-len(seen),latency_ms=statistics(latencies),scope='SDK application first observed by polling; includes poll delay, not screen latency')

def state_proof(rows,meta):
    scheduled=[r for r in rows if r.get('event')=='run_scheduled']
    if len(scheduled)!=1:raise ValueError('single scheduled run required')
    s=scheduled[0]
    for k in ['pid','process_creation_filetime','qpc_frequency','state','inputs_sha256']:
        if str(s.get(k))!=str(meta[k]):raise ValueError('state proof '+k+' mismatch')
    if s.get('record_start_qpc')!=meta['start_qpc'] or s.get('record_end_qpc')!=meta['end_qpc']:raise ValueError('state proof window mismatch')
    if any(r.get('event','').startswith('run_failed') for r in rows):raise ValueError('failed run cannot establish state')
    opened=[r for r in rows if r.get('event')=='source_opened'];closed=[r for r in rows if r.get('event')=='source_closed']
    if len(opened)!=1 or len(closed)!=1 or closed[0]['result']!=0:raise ValueError('successful source open/close proof missing')
    o,c=opened[0],closed[0]
    if len(set(o['handles']))!=5 or any(not h for h in o['handles']) or o['qpc']>meta['start_qpc'] or c['qpc']<meta['end_qpc']:raise ValueError('state coverage/target handles missing')
    clock=[r['qpc'] for r in rows if 'qpc' in r]
    if any(not math.isfinite(q) for q in clock) or any(b<a for a,b in zip(clock,clock[1:])):raise ValueError('state clock nonmonotonic')
    hearts=[o['qpc']]+[r['qpc'] for r in rows if r.get('event')=='heartbeat']+[c['qpc']]
    if any(b-a>=meta['qpc_frequency']*2 for a,b in zip(hearts,hearts[1:])):raise ValueError('lease coverage gap')
    accepts=[r for r in rows if r.get('event')=='accepted'];applied=[r for r in rows if r.get('event')=='applied_observed']
    by_revision={}
    for r in accepts:by_revision.setdefault(r['revision'],[]).append(r['part'])
    if not by_revision or any(sorted(parts)!=list(range(5)) for parts in by_revision.values()):raise ValueError('incomplete full-batch acceptance proof')
    if meta['state'] in ['Z','S'] and len(by_revision)!=1:raise ValueError('static state resubmitted')
    for part in range(5):
        lower=o['qpc'] if meta['state'] in ['Z','S'] else meta['start_qpc']
        upper=meta['start_qpc'] if meta['state'] in ['Z','S'] else meta['end_qpc']
        if not any(r['part']==part and lower<=r['qpc']<upper and r['revision'] in by_revision for r in applied):raise ValueError('applied state not observed for every part')
    return dict(valid=True,accepted_batches=len(by_revision),accepted_part_revisions=len(accepts),response=responses(accepts,applied,meta['qpc_frequency']))

def judge(metrics,budget,approval,proposal_sha):
    if approval is None:return 'measurement-only'
    if not budget or approval.get('approved') is not True or approval.get('source')!='direct_user_message' or approval.get('proposal_sha256')!=proposal_sha:raise ValueError('approved budget binding required')
    if any(k not in metrics or metrics[k] is None or not math.isfinite(metrics[k]) for k in budget):return 'not-observed'
    if any(not math.isfinite(v) or v<0 for v in budget.values()):raise ValueError('invalid budget')
    return 'within-approved-budget' if all(metrics[k]<=v for k,v in budget.items()) else 'over-approved-budget'

def main():
    p=argparse.ArgumentParser();p.add_argument('manifest',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
    m=json.loads(args.manifest.read_text('utf-8-sig'));required=['run_id','pid','process_creation_filetime','inputs_sha256','candidate_sha256','qpc_frequency','state','start_qpc','end_qpc','csv','csv_sha256','swapchain','time_column','time_scale','frame_column','present_mode','state_evidence','state_evidence_sha256']
    if any(k not in m for k in required):raise ValueError('missing run/window/identity/state binding')
    if m['qpc_frequency']<=0 or m['end_qpc']<=m['start_qpc'] or m['state'] not in ['Z','S','D']:raise ValueError('invalid window/state')
    for name in ['csv','state_evidence']:
        path=Path(m[name]);digest=hashlib.sha256(path.read_bytes()).hexdigest()
        if digest!=m[name+'_sha256']:raise ValueError(name+' identity mismatch')
    proof_rows=[json.loads(line) for line in Path(m['state_evidence']).read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    proof=state_proof(proof_rows,m)
    with Path(m['csv']).open(encoding='utf-8-sig',newline='') as f:result=frames(csv.DictReader(f),m)
    result['state_proof']=proof
    # This CLI deliberately cannot close C07: formal identity/state review and
    # direct approval are separate from extracting frame statistics.
    result.update(status='measurement-only',run_id=m['run_id'],c07_complete=False,manifest_sha256=hashlib.sha256(args.manifest.read_bytes()).hexdigest())
    with args.output.open('x',encoding='utf-8') as f:json.dump(result,f,indent=2,allow_nan=False)
if __name__=='__main__':main()
