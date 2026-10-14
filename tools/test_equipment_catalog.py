"""Check the redistributable catalogue and selector without Blender or game assets."""
import hashlib
import importlib
import json
from pathlib import Path
import re
import sys
import types

ROOT=Path(__file__).resolve().parents[1]
folder=ROOT/'blender_addon_v044/mhwi_equipmentmorph'
name='_mhm_catalog_test'
module=types.ModuleType(name);module.__path__=[str(folder)];sys.modules[name]=module
selection=importlib.import_module(name+'.equipment_selection')
data=json.loads((folder/'equipment_catalog.json').read_bytes())
assert data['source']['workbook_filename']=='Book.xlsx'
assert data['source']['prior_restricted_workbook_used'] is False
assert data['source']['source_rows']==len(data['armor'])==2896
assert len(data['sets'])==641
sets={i['series_id']:i for i in data['sets']}
seen=set();models={};checks=0
folders={'head':'helm','body':'body','arms':'arm','waist':'wst','legs':'leg'}
for item in data['armor']:
    key=(item['series_id'],item['part']);assert key not in seen;seen.add(key)
    assert item['name'] and item['series_id'] in sets
    assert item['part']==('body' if item['source_part']=='chest' else item['source_part'])
    for sex,prefix in [('male','m'),('female','f')]:
        directory=item['source_equipment_directories'][sex];entry=item['resource_paths'][sex]
        if directory=='None':assert entry=={'status':'restricted_by_source'}
        else:
            assert re.fullmatch('pl/'+prefix+r'_equip/pl\d{3}_\d{4}',directory)
            assert directory.rsplit('/',1)[-1]==item['model_address']
        if entry['status']=='available':
            expected=directory+'/'+folders[item['part']]+'/mod/'+prefix+'_'+folders[item['part']]+item['model_address'][2:]
            assert entry=={'status':'available','model_id':expected,'mod3_path':expected+'.mod3',
                          'morph_path':expected+'.morph','equipment_directory':directory}
            assert selection.resolve(item['series_id'],sex,item['part'])==entry
            models.setdefault(expected,[]).append({'series_id':item['series_id'],'armor_id':item['armor_id'],'part':item['part'],'sex':sex})
        else:
            try:selection.resolve(item['series_id'],sex,item['part'])
            except ValueError:pass
            else:raise AssertionError('Unavailable target accepted')
        checks+=1
assert data['model_resource_index']==[{'model_id':m,'candidates':v} for m,v in sorted(models.items())]
for sex in ('female','male'):
    options=selection.series_options(sex)
    assert selection.series_options(sex) is options
    assert {int(o[0]) for o in options}=={n for n,i in sets.items() if i['exportable_parts'][sex]}
    for n,item in sets.items():
        assert set(item['exportable_parts'][sex])=={i['part'] for i in data['armor'] if i['series_id']==n and i['resource_paths'][sex]['status']=='available'}
assert selection.resolve(250,'female','body')['model_id']=='pl/f_equip/pl105_0000/body/mod/f_body105_0000'
assert selection.resolve(1,'female','body')['model_id']=='pl/f_equip/pl001_0000/body/mod/f_body001_0000'
assert selection.resolve(674,'female','head')['model_id']=='pl/f_equip/pl104_0010/helm/mod/f_helm104_0010'
assert selection.resolve(674,'female','body')['model_id']=='pl/f_equip/pl104_0000/body/mod/f_body104_0000'
manual=types.SimpleNamespace(target_mode='MANUAL',model_id='pl/f_equip/pl001_0000/body/mod/f_body001_0000')
assert selection.target(manual)==manual.model_id
output=ROOT/'.build/current-validation/equipment-catalog.json';output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps({'path_combinations_checked':checks,'series':len(sets),'matched_models':len(models),
                             'catalog_sha256':hashlib.sha256((folder/'equipment_catalog.json').read_bytes()).hexdigest(),
                             'real_mod_or_workbook_required':False},indent=2)+'\n',encoding='utf-8')
print(f'PASS: {checks} equipment path combinations, {len(sets)} series, selector rejection and special variants')
