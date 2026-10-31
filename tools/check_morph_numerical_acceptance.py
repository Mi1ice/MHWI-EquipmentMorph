"""Evaluate the approved C03 absolute-position rule on hash-bound measurements.

This offline tool never composes, exports, runs a command from data, or creates
visual evidence. An invalid approval/input is refused; an excessive position
error is a failed numerical result. The original measurement reports stay intact.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys

UNIT='MOD3 exported coordinate unit'
METRIC='max over every serialized vertex of Euclidean xyz distance'

def require(condition,message):
    if not condition:raise ValueError(message)

def finite_number(value):
    return type(value) in (int,float) and math.isfinite(value)

def read_json(path):
    def unique(pairs):
        result={}
        for key,value in pairs:
            require(key not in result,'Duplicate JSON field');result[key]=value
        return result
    def number(text):
        value=float(text);require(math.isfinite(value),'Nonfinite JSON number');return value
    def constant(_):raise ValueError('Nonfinite JSON constant')
    require(path.stat().st_size<=128*1024*1024,'JSON file bound')
    return json.loads(path.read_text(encoding='utf-8-sig'),object_pairs_hook=unique,parse_float=number,parse_constant=constant)

def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def local(root,name):
    require(isinstance(name,str) and bool(name),'Missing bound path')
    part=Path(name)
    require(not part.is_absolute() and not part.drive and '..' not in part.parts,'Bound path must remain inside root')
    path=(root/part).resolve()
    require(path.is_relative_to(root),'Bound path escaped root')
    return path

def evaluate(root,rule_path):
    root=Path(root).resolve();rule_path=Path(rule_path).resolve();rule=read_json(rule_path)
    require(rule['schema']=='mhwi-c03-approved-rule-v1' and rule['status']=='approved','Missing approved rule')
    bindings={}
    for name,expected in rule['bindings'].items():
        path=local(root,name)
        require(path not in bindings and isinstance(expected,str) and re.fullmatch('[0-9a-f]{64}',expected),'Invalid/duplicate digest binding')
        require(digest(path)==expected,'Bound file digest mismatch: '+name)
        bindings[path]=expected
    def bound_path(name):
        path=local(root,name);require(path in bindings,'Missing digest binding: '+name);return path
    def bound(name):return read_json(bound_path(name))
    def sha(name):return bindings[bound_path(name)]

    approval=bound(rule['approval']);proposal=bound(rule['proposal'])
    require(approval['schema']=='mhwi-c03-numerical-approval-v1' and approval['status']=='approved'
            and approval['source']=='direct_user_message' and isinstance(approval['user_reply'],str) and bool(approval['user_reply'].strip()),'Missing direct user approval')
    require(approval['proposal_sha256']==sha(rule['proposal']),'User approval proposal digest mismatch')
    require(proposal['schema']=='mhwi-c03-tolerance-proposal-v1','Proposal schema mismatch')
    require(proposal['unit']==UNIT,'Unapproved/unknown position unit')
    require(proposal['metric']==METRIC and proposal['comparison']=='<=','Unapproved position metric')
    limit=proposal['absolute_limit'];require(finite_number(limit) and limit>0,'Invalid absolute limit')
    require(approval['accepted_rule']=={k:proposal[k] for k in ('metric','unit','absolute_limit','comparison')},'Approval rule differs from proposal')
    app=proposal['applicability']
    inputs=bound(rule['inputs']);source=bound(rule['source_audit']);replay=bound(rule['replay'])
    require(inputs['schema']=='mhwi-c03-input-audit-v1' and source['schema']=='mhwi-c03-source-audit-v1'
            and replay['schema']=='mhwi-c03-current-replay-v1','Audit schema mismatch')
    for field,name in [('frozen_input_audit_sha256','inputs'),('current_source_audit_sha256','source_audit'),('replay_sha256','replay')]:
        require(app[field]==sha(rule[name]),'Approved audit digest mismatch')
    require(isinstance(inputs['artifacts'],list) and bool(inputs['artifacts']),'Missing frozen input identities')
    for artifact in inputs['artifacts']:
        require(sha(artifact['path'])==artifact['sha256'],'Frozen input digest differs from approved audit')
        require(bound_path(artifact['path']).stat().st_size==artifact['bytes'],'Frozen input length differs from approved audit')
    require(replay['inputs_sha256']==sha(rule['inputs']) and replay['source_audit_sha256']==sha(rule['source_audit']),'Replay audit digest mismatch')
    require(source['product_head']==replay['product_head'],'Source/replay identity mismatch')
    require(replay['executable_sha256']==sha(rule['comparator']),'Comparator digest mismatch')
    for name,expected in source['current_comparator_dependencies'].items():
        require(sha(str(Path(rule['source_root'])/name))==expected,'Current source digest mismatch')
    require(set(rule['modes'])==set(replay['modes'])==set(inputs['modes'])=={'BASELINE','GEOMETRIC'},'Two-mode coverage required')
    for field in ('keys_per_asset','cases_per_mode','serialized_vertices'):
        require(type(app[field]) is int and app[field]>0,'Invalid approved coverage')
    require(app['weights']==[0,.25,.5,.75,1],'Unexpected single-key sample grid')
    results={};all_passed=True
    for mode,paths in rule['modes'].items():
        ref=bound(paths['reference']);report=bound(paths['comparison']);measured=replay['modes'][mode];audit=inputs['modes'][mode]
        approved_reference=Path(audit['reference'])
        if approved_reference.is_absolute():approved_reference=approved_reference.resolve().relative_to(root)
        require(sha(paths['reference'])==sha(str(approved_reference)),'Reference differs from approved mode reference')
        require(ref['schema']=='mhwi-morph-position-reference-v1' and report['schema']=='mhwi-morph-position-comparison-v1','Reference/report schema mismatch')
        require(report['status']=='measurement_completed_threshold_not_set' and report['tolerance_set'] is False,'Expected original measurement-only report')
        require(measured['report_sha256']==sha(paths['comparison']),'Replay report digest mismatch')
        require(ref['pack_sha256']==report['pack_sha256']==sha(paths['pack']),'Reference/report pack identity mismatch')
        folder=bound_path(paths['reference']).parent
        for stem in ('model','profile','trace','positions'):
            name=ref[stem+'_file']
            require(isinstance(name,str) and Path(name).name==name,'Reference filename must be local')
            path=(folder/name).relative_to(root)
            require(sha(str(path))==ref[stem+'_sha256'],'Reference '+stem+' digest mismatch')
        require(ref['source_restored'] is True and ref['blend_saved'] is False,'Reference source-state evidence failed')
        require(report['zero_restored'] is True and measured['zero_basis_bytes_exact'] is True,'Exact zero basis restoration failed')
        require(report['non_position_valid'] is True and measured['unrelated_bytes_preserved'] is True,'Unrelated field preservation failed')
        require(report['key_count']==audit['keys']==app['keys_per_asset'],'Key coverage mismatch')
        require(report['position_count']==audit['vertices']==app['serialized_vertices'],'Position coverage mismatch')
        offsets=ref['position_offsets'];mapping=ref['key_mapping'];cases=ref['cases'];rows=report['cases']
        require(len(offsets)==app['serialized_vertices'] and all(type(v) is int and v>=0 for v in offsets)
                and offsets==sorted(set(offsets)),'Position offset coverage mismatch')
        ids=[key['id'] for key in mapping]
        require(len(ids)==len(set(ids))==app['keys_per_asset'] and all(isinstance(v,str) and v for v in ids),'Key ID coverage mismatch')
        require(all(isinstance(key['source_key'],str) and key['source_key'] for key in mapping),'Source key trace missing')
        require(len(cases)==len(rows)==measured['cases']==app['cases_per_mode'],'Reference/report case count mismatch')
        require([c['id'] for c in cases]==[r['id'] for r in rows] and len(set(c['id'] for c in cases))==len(cases),'Reference/report case identity mismatch')
        require(cases[0]['kind']=='baseline' and cases[0]['weights']=={},'Missing baseline case')
        for i,key in enumerate(ids):
            samples=cases[1+5*i:1+5*(i+1)]
            require(len(samples)==5 and all(c['kind']=='single' and c['key_id']==key and c['weights']=={key:w}
                    for c,w in zip(samples,app['weights'])),'Incomplete single-key coverage')
        zero=0;case_results=[];largest=0.
        for index,(case,row) in enumerate(zip(cases,rows)):
            require(case['kind'] in ('baseline','single','combination'),'Unknown case kind')
            require(case['byte_offset']==index*len(offsets)*24,'Reference segment coverage mismatch')
            require(isinstance(case['weights'],dict) and all(k in ids and finite_number(w) and 0<=w<=1 for k,w in case['weights'].items()),'Invalid reference weight')
            zero+=not any(case['weights'].values())
            stats=row['composition_error']
            for field in ('max','mean','rms','p50','p95','p99'):
                require(finite_number(stats[field]) and stats[field]>=0,'Invalid/nonfinite position statistic')
            error=stats['max'];largest=max(largest,error)
            require(all(stats[k]<=error for k in ('mean','rms','p50','p95','p99')) and stats['p50']<=stats['p95']<=stats['p99'],'Inconsistent position statistics')
            vertex=stats['worst_serialized_vertex']
            require(type(vertex) is int and 0<=vertex<len(offsets) and stats['worst_position_offset']==offsets[vertex],'Worst vertex trace mismatch')
            passed=error<=limit
            case_results.append(dict(id=case['id'],kind=case['kind'],max_position_error=error,position_passed=passed))
            all_passed=all_passed and passed
        require(zero==measured['zero_cases']==audit['zero_cases'],'Zero case coverage mismatch')
        require(report['max_composition_error']==largest==measured['max_composition_error'],'Position summary max mismatch')
        results[mode]=dict(cases=len(rows),failed_cases=sum(not r['position_passed'] for r in case_results),max_position_error=largest,
                           zero_cases=zero,zero_basis_bytes_exact=True,unrelated_fields_preserved=True,case_results=case_results)
    total=sum(mode['cases'] for mode in results.values())
    require(total==replay['total_mode_cases'],'Total case coverage mismatch')
    return dict(schema='mhwi-c03-numerical-acceptance-result-v1',status='passed' if all_passed else 'failed',
                scope='Only the approved frozen paired inputs, source and measured cases',unit=UNIT,metric=METRIC,absolute_limit=limit,
                rule_sha256=digest(rule_path),approval_sha256=sha(rule['approval']),proposal_sha256=sha(rule['proposal']),
                evaluator_sha256=digest(Path(__file__)),product_measurement_base=replay['product_head'],total_cases=total,modes=results,
                finite_inputs_and_bound_identity_verified=True,visual_result_generated=False,game_tested=False)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True);parser.add_argument('--rule',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    options=parser.parse_args()
    try:
        result=evaluate(options.root,options.rule)
        with options.output.open('x',encoding='utf-8') as stream:
            json.dump(result,stream,ensure_ascii=False,indent=2,allow_nan=False);stream.write('\n')
        print(f"{result['status']}: {result['total_cases']} approved cases; limit {result['absolute_limit']} {UNIT}")
        return 0 if result['status']=='passed' else 1
    except (ValueError,KeyError,TypeError,OSError,OverflowError) as error:
        print('refused: '+str(error),file=sys.stderr);return 2

if __name__=='__main__':raise SystemExit(main())
