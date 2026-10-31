"""Convert a private approved case proposal and paired references; no game I/O."""
import argparse,hashlib,json,math,pathlib

STEPS=[0,.25,.5,.75,1,.75,.5,.25,0]
def require(value,message):
    if not value:raise ValueError(message)
def unique(pairs):
    result={}
    for key,value in pairs:
        require(key not in result,'Duplicate JSON property');result[key]=value
    return result
def load(path,limit=16*1024*1024):
    raw=path.read_bytes();require(len(raw)<=limit,'Input too large')
    return json.loads(raw.decode('utf-8'),object_pairs_hook=unique,parse_constant=lambda x:(_ for _ in ()).throw(ValueError('Nonfinite JSON')))
def identifier(value,bound=63):return isinstance(value,str) and 0<len(value)<=bound and all(32<=ord(c)<=126 for c in value)
def text(value):return isinstance(value,str) and 0<len(value.encode('utf-8'))<=4096 and all(ord(c)>=32 for c in value)
def digest(value):return isinstance(value,str) and len(value)==64 and all(c in '0123456789abcdef' for c in value)
def validate(c):
    require(set(c)=={'schema','part','model_id','expected_key_count','modes','labels','cases','steps'},'Unknown catalog fields')
    require(c['schema']=='mhwi-sdk-validation-catalog-v1' and c['part']==1 and identifier(c['model_id'],127),'Invalid target')
    require(type(c['expected_key_count']) is int and 0<c['expected_key_count']<=1024,'Invalid actual count')
    require(c['steps']==STEPS and all(type(v) in (int,float) and math.isfinite(v) for v in c['steps']),'Invalid steps')
    require(len(c['modes'])==2 and {m['name'] for m in c['modes']}=={'BASELINE','GEOMETRIC'},'Both modes required')
    for m in c['modes']:require(set(m)=={'name','model_sha256','pack_sha256'} and digest(m['model_sha256']) and digest(m['pack_sha256']),'Invalid mode identity')
    require(len({(m['model_sha256'],m['pack_sha256']) for m in c['modes']})==2,'Distinct identities required')
    labels={}
    for label in c['labels']:
        require(set(label)=={'id','source'} and identifier(label['id']) and text(label['source']) and label['id'] not in labels,'Invalid label');labels[label['id']]=label['source']
    case_ids=set();used=set()
    for case in c['cases']:
        require(set(case)=={'id','title','fixed','varying'} and identifier(case['id']) and text(case['title']) and case['id'] not in case_ids,'Invalid case');case_ids.add(case['id']);keys=set()
        for item in case['fixed']:
            require(set(item)=={'id','weight'} and item['id'] in labels and item['id'] not in keys and type(item['weight']) in (int,float) and item['weight']==.5,'Invalid fixed key');keys.add(item['id'])
        for key in case['varying']:require(key in labels and key not in keys,'Invalid varying key');keys.add(key)
        require((len(case['fixed']),len(case['varying'])) in ((0,1),(25,5)),'Expected single or 25+5');used.update(keys)
    require(case_ids and used==set(labels) and len(c['cases'])<=320 and len(labels)<=320,'Case labels incomplete')
    require(len(json.dumps(c,ensure_ascii=False).encode('utf-8'))<=256*1024,'Catalog too large')
    return c
def prepare(proposal,baseline,geometric):
    require(proposal['schema']=='mhwi-sdk-visual-case-proposal-v1','Unsupported proposal')
    require(proposal['single_weight_curve_proposed']==STEPS,'Unsupported weight curve')
    require(baseline['key_mapping']==geometric['key_mapping'],'Paired mappings differ')
    mapping={k['id']:k['source_key'] for k in baseline['key_mapping']}
    require(len(mapping)==len(baseline['key_mapping']) and all(identifier(k) and text(v) for k,v in mapping.items()),'Invalid reference mapping')
    modes=[]
    for name,reference in [('BASELINE',baseline),('GEOMETRIC',geometric)]:
        expected=proposal['asset_identity_by_mode'][name]
        require(all(reference[k]==expected[k] for k in ('model_id','model_sha256','pack_sha256')),'Resource identity mismatch')
        require(reference['model_id']==baseline['model_id'],'Models differ');modes.append(dict(name=name,model_sha256=reference['model_sha256'],pack_sha256=reference['pack_sha256']))
    labels={};cases=[]
    def key(item):
        name=item['key_id'];require(name in mapping and mapping[name]==item['source_key'],'Source label does not match runtime ID');labels[name]=mapping[name];return name
    for item in proposal['single_cases']:
        name=key(item);cases.append(dict(id=item['case_id'],title=item['case_id']+' · '+mapping[name],fixed=[],varying=[name]))
    for item in proposal['combination_cases']:
        require(item['varying_curve_proposed']==STEPS and item['fixed_weight']==.5 and item['final_all_dynamic_weights']==0,'Combination contract differs')
        fixed=[dict(id=key(k),weight=.5) for k in item['fixed_keys']];varying=[key(k) for k in item['varying_keys']]
        for reference in (baseline,geometric):
            lookup={r['id']:r for r in reference['cases']}
            for index,weight in enumerate(STEPS):
                expected={**{k['id']:.5 for k in fixed},**{k:weight for k in varying}}
                actual=lookup.get('combo-'+item['scheme']+'-'+str(index),{}).get('weights')
                require(actual==expected,'Combination reference weights differ')
        cases.append(dict(id=item['case_id'],title=item['case_id']+' · '+item['scheme'],fixed=fixed,varying=varying))
    return validate(dict(schema='mhwi-sdk-validation-catalog-v1',part=1,model_id=baseline['model_id'],expected_key_count=len(mapping),modes=modes,labels=[dict(id=k,source=v) for k,v in sorted(labels.items())],cases=cases,steps=STEPS))
def main():
    p=argparse.ArgumentParser();p.add_argument('--cases',type=pathlib.Path,required=True);p.add_argument('--baseline-reference',type=pathlib.Path,required=True);p.add_argument('--geometric-reference',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
    require(not a.output.exists(),'Refusing to overwrite catalog');result=prepare(load(a.cases),load(a.baseline_reference),load(a.geometric_reference));raw=(json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode('utf-8');require(len(raw)<=256*1024,'Catalog too large');a.output.parent.mkdir(parents=True,exist_ok=True)
    with a.output.open('xb') as stream:stream.write(raw)
    print(json.dumps(dict(cases=len(result['cases']),labels=len(result['labels']),expected_key_count=result['expected_key_count'],sha256=hashlib.sha256(raw).hexdigest())))
if __name__=='__main__':main()
