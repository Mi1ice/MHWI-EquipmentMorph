"""Read-only, conservative C06 group evidence checker. Never supplies visual results."""
import argparse
import hashlib
import json
import math
from pathlib import Path


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'),parse_constant=lambda s: (_ for _ in ()).throw(ValueError(s)))


def read_segment(manifest,case,markers,paths):
    chosen=[m for m in markers if m.get('case_id')==case['case_id']]
    if not chosen:return {},[],['not_run']
    if len(chosen)!=2 or [m.get('phase') for m in chosen]!=['begin','end']:return {},[],['missing_or_duplicate_markers']
    begin,end=chosen;session=manifest.get('session') or {};issues=[]
    for m in chosen:
        if m.get('pid')!=session.get('pid') or m.get('runtime_sha256')!=session.get('runtime_sha256'):issues.append('marker_process_or_runtime_mismatch')
    try:
        path=Path(begin['log_file']).resolve()
        if path!=Path(end['log_file']).resolve() or path not in {Path(p).resolve() for p in paths}:raise ValueError('log_file_mismatch')
        raw=path.read_bytes();lines=raw.splitlines(keepends=True)
        first,last=begin['next_complete_line'],end['next_complete_line']
        if not isinstance(first,int) or not isinstance(last,int) or not 1<=first<last<=len(lines)+1:raise ValueError('invalid_or_empty_line_range')
        for m in chosen:
            prefix=b''.join(lines[:m['next_complete_line']-1])
            if prefix and not prefix.endswith(b'\n'):raise ValueError('incomplete_log_line')
            if hashlib.sha256(prefix).hexdigest()!=m.get('prefix_sha256'):raise ValueError('log_prefix_missing_changed_or_rotated')
        all_records=[json.loads(line) for line in lines[:last-1]]
        headers=[s for s in all_records if 'pid' in s and 'process_creation_filetime' in s and 'exe_sha256' in s]
        if len(headers)!=1:raise ValueError('missing_or_multiple_session_headers')
        if any(s.get('event')=='bounded_log_rotated' for s in all_records[first-1:last-1]):raise ValueError('bounded_log_rotated')
        return headers[0],all_records[first-1:last-1],issues
    except (OSError,ValueError,KeyError,TypeError) as error:return {},[],issues+[str(error)]


def close(a,b):return abs(a-b)<=1e-6


def settled(state):
    requested,applied=state.get('requested'),state.get('applied')
    if not state.get('live') or not state.get('ready') or not state.get('target') or not requested or not isinstance(applied,list) or len(requested)!=len(applied):return False
    if not all(isinstance(v,(int,float)) and math.isfinite(v) and 0<=v<=1 for v in requested+applied):return False
    if not all(close(a,b) for a,b in zip(requested,applied)):return False
    if state.get('requested_revision')!=state.get('applied_revision'):return False
    if any(requested) and (not state.get('applied_frame') or state.get('requested_frame')!=state.get('applied_frame')):return False
    return True


def sequence(states,weights,index):
    """A sequence belongs to one target generation, never to global revisions."""
    progress={}
    for s in states:
        if not settled(s) or index>=len(s['applied']):continue
        key=(s['part'],s['epoch'],s['target']);at=progress.get(key,0)
        if close(s['applied'][index],weights[at]):at+=1
        if at==len(weights):return True
        progress[key]=at
    return False


def retirement_proof(records,retired):
    related=[r for r in records if r.get('part')==retired.get('part') and r.get('epoch')==retired.get('epoch')]
    for r in related:
        if r.get('event')=='part_retirement_complete':
            required=['covered_signal','draw_completed','upload_floor','upload_completed']
            if any(not isinstance(r.get(k),int) or r[k]<0 or r[k]>=2**64-1 for k in required):return 'failed'
            return 'passed' if r.get('successor_joined') is True and r.get('part_buffers')==3 and r['covered_signal']>0 and r['draw_completed']>=r['covered_signal'] and r['upload_completed']>=r['upload_floor'] else 'failed'
        if r.get('event')=='part_unlaunched_reusable':
            # This alternate path has no GPU draw references, but still needs its finite upload proof.
            return 'passed' if isinstance(r.get('upload_completed'),int) and 0<=r['upload_completed']<2**64-1 else 'failed'
    return 'not_observed'


def replacement(records,at,end,neutral=True):
    old=records[at]
    if old.get('old_handle_stale') is not True:return 'failed',None,None
    found=[(i,s) for i,s in enumerate(records[at+1:end],at+1) if s.get('event')=='part_ready_original_inputs' and s.get('part')==old['part']]
    if not found:return 'not_observed',None,None
    index,new=found[0]
    proof=retirement_proof(records[at+1:index],old)
    if proof!='passed':return proof,None,None
    if new.get('target')==old['target'] or not new.get('target') or new.get('epoch',0)<=old['epoch']:return 'failed',None,None
    if neutral and (any(new.get('requested',[])) or any(new.get('applied',[]))):return 'failed',None,None
    return 'passed',index,new


def switch_check(records,case,states):
    part=case['part'];rounds=case['rounds'];b=case['kind']=='switch_B';need=rounds*(2 if b else 1)
    retire=[(i,r) for i,r in enumerate(records) if r.get('event')=='public_part_retired' and r.get('part')==part and r.get('target')]
    if len(retire)<need:return 'not_observed',['retirement_rounds_missing']
    for n,(at,old) in enumerate(retire[:need]):
        before=[s for s in records[:at] if s.get('event') in ('part_state','part_ready_original_inputs') and s.get('target')==old['target'] and s.get('epoch')==old['epoch']]
        expected=case.get('leave_weights',[.5]*rounds)[n//2 if b else n]
        index=case.get('key_index',0)
        if not before or not settled(before[-1]) or index>=len(before[-1]['applied']) or not close(before[-1]['applied'][index],expected):return 'not_observed',['leave_weight_not_applied']
        end=retire[n+1][0] if n+1<len(retire) else len(records)
        proof,ready_at,new=replacement(records,at,end)
        if proof!='passed':return proof,['chronological_replacement_'+proof]
        if new.get('_role')!=('B' if b and n%2==0 else 'A'):return 'not_observed',['expected_replacement_model_missing']
        fresh=[s for s in records[ready_at:end] if s.get('target')==new['target'] and s.get('epoch')==new['epoch'] and s.get('part')==part and 'applied' in s]
        wanted=[.5,0] if not b or n==need-1 else [.5]
        if not sequence(fresh,wanted,index):return 'not_observed',['renewed_control_or_release_missing']
    return 'passed',[]


def coexist_edits(states,case,after):
    expected=list(case['weights']);phases=[]
    for part,weight in enumerate(case['edit_weights']):
        for value in (weight,0):
            expected[part]=value;phases.append(list(expected))
    latest={};progress=0
    for s in states:
        latest[s['part']]=s
        if s.get('observed_tick',0)<=after or len(latest)!=5 or not all(settled(v) for v in latest.values()):continue
        ticks=[v['observed_tick'] for v in latest.values()]
        if max(ticks)-min(ticks)>1500:continue
        if all(close(latest[p]['applied'][case.get('key_index',0)],w) for p,w in enumerate(phases[progress])):
            progress+=1
            if progress==len(phases):return True
    return False


def evaluate(manifest,case,records,observations,header):
    session=manifest.get('session') or {};issues=[];failures=[];states=[]
    software=manifest.get('software',{'status':'not_run'})
    visual=observations.get(case['case_id'],{'status':'not_observed'})
    for name in ('pid','process_creation_filetime','exe_sha256'):
        if session.get(name) is None:issues.append('session_'+name+'_missing')
        elif header.get(name)!=session[name]:failures.append('session_'+name+'_mismatch')
    if session.get('loaded_identity_verified') is not True or not session.get('runtime_sha256'):issues.append('loaded_runtime_not_verified')
    assets={(a['part'],a['model_id']):a for a in manifest['assets']};annotated=[]
    for original in records:
        event=original.get('event','')
        if event in ('failed_overrides_disabled_resources_retained','launch_identity_rejected','input_cycle_rejected','part_retirement_buffer_rejected','stopped_original_inputs_resources_retained'):failures.append(event)
        if event not in ('part_state','part_ready_original_inputs'):
            annotated.append(original);continue
        s=dict(original);asset=assets.get((s.get('part'),s.get('model_id')))
        if not asset:failures.append('unfrozen_asset_identity');continue
        if s.get('model_sha256')!=asset['model_sha256'] or s.get('pack_sha256')!=asset['asset_sha256']:failures.append('asset_digest_mismatch');continue
        if len(s.get('requested',[]))!=len(asset['keys']) or len(s.get('applied',[]))!=len(asset['keys']):failures.append('weight_count_mismatch');continue
        if not all(isinstance(v,(int,float)) and math.isfinite(v) and 0<=v<=1 for v in s.get('requested',[])+s.get('applied',[])):failures.append('invalid_weight');continue
        s['_role']=asset.get('role');states.append(s);annotated.append(s)
    kind=case['kind'];live='not_observed';reasons=[]
    selected=[s for s in states if s['part']==case.get('part')]
    if kind=='basic':
        if sequence(selected,case['sequence'],case.get('key_index',0)):live='passed'
        else:reasons.append('same_generation_applied_sequence_missing')
    elif kind=='coexist':
        intervals=[]
        for part,weight in enumerate(case['weights']):
            samples=[s for s in states if s['part']==part and settled(s) and close(s['applied'][case.get('key_index',0)],weight)]
            groups={}
            for s in samples:groups.setdefault((s['epoch'],s['target']),[]).append(s['observed_tick'])
            spans=[(min(ts),max(ts)) for ts in groups.values() if len(ts)>=2]
            if not spans:break
            intervals.append(max(spans,key=lambda span:span[1]-span[0]))
        start=max((a for a,b in intervals),default=0);end=min((b for a,b in intervals),default=0)
        uninterrupted=all(settled(s) and close(s['applied'][case.get('key_index',0)],case['weights'][s['part']]) for s in states if start<=s.get('observed_tick',-1)<=end)
        if len(intervals)==5 and end-start>=case['hold_ms'] and uninterrupted:
            if coexist_edits(states,case,end):live='passed'
            else:reasons.append('independent_part_edits_and_final_zero_missing')
        else:reasons.append('five_part_30s_overlap_missing')
    elif kind in ('switch_U','switch_B'):
        live,reasons=switch_check(annotated,case,selected)
        attest=['equipment_sequence_confirmed']+(['unpackaged_interval_observed'] if kind=='switch_U' else [])
        if any(visual.get(k) is not True for k in attest):visual=dict(visual,status='not_observed',reason='equipment_sequence_attestation_missing')
    elif kind in ('title','history'):
        removed=next((i for i,r in enumerate(annotated) if r.get('event')=='automatic_main_player_changed' and r.get('waiting') is True),None)
        # PlayerGone clears the anchor in the removal callback. Refresh can
        # therefore see zero unchanged and omit the worker's waiting event.
        # Its per-part retirement reason is direct evidence of that same path.
        passed=True
        for part in case['parts']:
            all_retire=[i for i,r in enumerate(annotated) if r.get('event')=='public_part_retired' and r.get('part')==part and r.get('target')]
            first=next((n for n,i in enumerate(all_retire) if (removed is not None and i>removed) or
                        annotated[i].get('reason') in ('equipment_player_removed','equipment_player_destroyed')),None)
            if first is None:passed=False;continue
            # Once removal is established, ordinary equipment changes also end
            # a generation; controls from that retired target cannot finish this case.
            retire=all_retire[first:]
            for n,at in enumerate(retire):
                end=retire[n+1] if n+1<len(retire) else len(annotated)
                proof,ready_at,new=replacement(annotated,at,end)
                if proof=='failed':failures.append('retirement_completion_failed')
                if proof!='passed':passed=False;break
            if proof!='passed':continue
            # Every intermediate generation must retire safely and start neutral.
            # The requested renewed control belongs to the final loaded character.
            fresh=[s for s in annotated[ready_at:] if s.get('part')==part and s.get('epoch')==new['epoch'] and s.get('target')==new['target'] and 'applied' in s]
            if not sequence(fresh,[.5,0],0):passed=False
        if passed:live='passed'
        else:reasons.append('player_removal_five_part_rebind_control_missing')
    elif kind=='restore':
        retired=next((i for i,r in enumerate(annotated) if r.get('event')=='public_part_retired' and r.get('part')==case['part'] and r.get('target')),None)
        proof,ready_at,new=replacement(annotated,retired,len(annotated),neutral=False) if retired is not None else ('not_observed',None,None)
        if proof=='failed':failures.append('daily_source_retirement_failed')
        newstates=[s for s in annotated[ready_at:] if new and s.get('part')==case['part'] and s.get('epoch')==new['epoch'] and s.get('target')==new['target'] and settled(s)] if ready_at is not None else []
        changes=[]
        for s in newstates:
            w=s['applied'][case.get('key_index',0)]
            if not changes or not close(changes[-1],w):changes.append(w)
        observed=all(visual.get(k) is True for k in ('sources_restored','source_attribution_confirmed','manual_override_observed','release_to_daily_source_observed'))
        if observed and proof=='passed' and len(changes)>=3:live='passed'
        else:reasons.append('restored_source_attribution_missing')
    else:reasons.append('unsupported_case_kind')
    closed=[s for s in states if settled(s) and s.get('started') and s.get('cycle_open') is False and s.get('completed_signals',0)>0 and s.get('joined_boundaries',0)>=s['completed_signals'] and 1<=s.get('native_workers',0)<=8]
    transitions=[];previous={}
    for second in closed:
        first=previous.get(second['part']);previous[second['part']]=second
        if first and first['native_workers']!=second['native_workers']:
            if first['epoch']!=second['epoch'] or second['completed_signals']>first['completed_signals']:
                transitions.append({'part':first['part'],'from':first['native_workers'],'to':second['native_workers'],'from_epoch':first['epoch'],'to_epoch':second['epoch']})
    if failures:live='failed'
    elif issues:live='not_observed'
    status='failed' if live=='failed' or visual.get('status')=='failed' or software.get('status')=='failed' else 'passed' if live=='passed' and visual.get('status')=='passed' and software.get('status')=='passed' else 'not_observed'
    return {'case_id':case['case_id'],'status':status,'software':software,'live_log':{'status':live,'issues':sorted(set(issues+failures+reasons))},'user_observation':visual,'thread_transition':{'status':'passed' if transitions and not failures and not issues else 'not_observed','transitions':transitions,'meaning':'sampled closed successful cycles; no inference about unsampled counts'}}


def summarize(cases,results):
    definitions={c['case_id']:c for c in cases}
    success=[definitions[r['case_id']].get('bounded_attempt',1) for r in results if definitions[r['case_id']]['kind']=='history' and r['status']=='passed' and r['thread_transition']['status']=='passed']
    thread_seen=bool(success)
    def acceptable(r):
        if r['status']=='passed':return True
        c=definitions[r['case_id']]
        return r['status']=='not_run' and c.get('optional_after_thread_observed') is True and success and c.get('bounded_attempt',0)>min(success)
    passed=bool(results) and all(acceptable(r) for r in results)
    return {'matrix_passed':passed,'historical_thread_transition_observed':thread_seen,'C06_complete':passed and thread_seen}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for arg in ('manifest','cases','markers','observations','output'):parser.add_argument('--'+arg,required=True)
    parser.add_argument('--runtime-log',action='append',default=[])
    args=parser.parse_args();manifest=read_json(args.manifest);cases=read_json(args.cases)['cases'];observations=read_json(args.observations)
    markers=[json.loads(line) for line in Path(args.markers).read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    results=[]
    for case in cases:
        header,records,issues=read_segment(manifest,case,markers,args.runtime_log)
        if issues:
            status='not_run' if issues==['not_run'] else 'not_observed'
            results.append({'case_id':case['case_id'],'status':status,'software':manifest.get('software',{'status':'not_run'}),'live_log':{'status':status,'issues':issues},'user_observation':observations.get('cases',{}).get(case['case_id'],{'status':'not_observed'}),'thread_transition':{'status':'not_observed','transitions':[]}})
        else:
            result=evaluate(manifest,case,records,observations.get('cases',{}),header)
            if observations.get('session')!=manifest.get('session'):
                result['user_observation']={'status':'not_observed','reason':'observation_session_not_bound'}
                if result['status']=='passed':result['status']='not_observed'
            results.append(result)
    report={'schema':'mhwi-c06-report-v1','cases':results,**summarize(cases,results),'files_changed':'new output report only'}
    output=Path(args.output).resolve();inputs=[args.manifest,args.cases,args.markers,args.observations]+args.runtime_log
    if output in {Path(p).resolve() for p in inputs}:parser.error('Output must be distinct from every input')
    if output.exists():parser.error('Output already exists; choose a new report filename')
    output.write_text(json.dumps(report,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps({'cases':len(results),'C06_complete':report['C06_complete'],'output':str(output)}))


if __name__=='__main__':main()
