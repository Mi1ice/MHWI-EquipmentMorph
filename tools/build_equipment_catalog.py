"""Build the bundled catalogue from the user's unrestricted six-column workbook.

Reads only worksheet cells and extracted MOD3 filenames, never game file contents.
"""
import argparse
from collections import Counter,defaultdict
import hashlib
import json
import os
from pathlib import Path
import re

import openpyxl

PARTS={'head':('head','helm'),'chest':('body','body'),'arms':('arms','arm'),
       'waist':('waist','wst'),'legs':('legs','leg')}
SEXES={'male':'m','female':'f'}


def series_name(names):
    common=os.path.commonprefix(names).rstrip(' ·・-_/')
    if not common:return names[0]
    for suffix in ('阿尔法','贝塔','伽马','α','β','γ'):
        if all(name.endswith(suffix) for name in names) and not common.endswith(suffix):
            return common+suffix
    return common


def build(source,pl_root):
    source=Path(source);pl_root=Path(pl_root)
    digest=hashlib.sha256(source.read_bytes()).hexdigest()
    if pl_root.name.lower()!='pl' or not pl_root.is_dir():raise ValueError('Expected an extracted pl directory')
    files=sorted('pl/'+p.relative_to(pl_root).as_posix() for p in pl_root.rglob('*.mod3') if p.is_file())
    if len({p.casefold() for p in files})!=len(files):raise ValueError('Duplicate model filenames')
    present=set(files)
    workbook=openpyxl.load_workbook(source,read_only=True,data_only=False,keep_links=False)
    try:
        if len(workbook.worksheets)!=1:raise ValueError('Expected one six-column worksheet')
        sheet=workbook.worksheets[0]
        if sheet.max_column!=6:raise ValueError('Expected six columns without a header')
        rows=list(sheet.values);sheet_name=sheet.title
    finally:workbook.close()
    armor=[];groups=defaultdict(list);seen=set();models=defaultdict(list)
    for number,row in enumerate(rows,1):
        part_raw,armor_id,name,series_id,male,female=row
        if part_raw not in PARTS or not isinstance(name,str) or not name.strip():raise ValueError('Invalid part/name at row '+str(number))
        if any(type(n) is not int or n<0 for n in (armor_id,series_id)):raise ValueError('Invalid ID at row '+str(number))
        part,folder=PARTS[part_raw]
        if (series_id,part) in seen:raise ValueError('Repeated series/part at row '+str(number))
        seen.add((series_id,part));paths={};addresses=[]
        for sex,value in [('male',male),('female',female)]:
            if value=='None':paths[sex]={'status':'restricted_by_source'};continue
            prefix=SEXES[sex]
            match=re.fullmatch(r'pl/'+prefix+r'_equip/(pl\d{3}_\d{4})',value) if isinstance(value,str) else None
            if not match:raise ValueError('Invalid gender resource directory at row '+str(number))
            address=match[1];addresses.append(address)
            model=value+'/'+folder+'/mod/'+prefix+'_'+folder+address[2:]
            entry={'status':'missing_in_extraction'}
            if model+'.mod3' in present:
                entry.update(status='available',model_id=model,mod3_path=model+'.mod3',
                             morph_path=model+'.morph',equipment_directory=value)
                models[model].append({'series_id':series_id,'armor_id':armor_id,'part':part,'sex':sex})
            paths[sex]=entry
        if not addresses:raise ValueError('No resource directory at row '+str(number))
        if len(set(addresses))!=1:raise ValueError('Different male/female addresses at row '+str(number))
        address=addresses[0]
        item={'part':part,'armor_id':armor_id,'name':name,'series_id':series_id,'source_row':number,
              'source_part':part_raw,'source_equipment_directories':{'male':male,'female':female},
              'model_address':address,'resource_id':address[2:5],'variant':address[6:],
              'selectable':True,'resource_paths':paths}
        item['available_genders']=[s for s in SEXES if paths[s]['status']=='available']
        item['exportable']=bool(item['available_genders'])
        armor.append(item);groups[series_id].append(item)
    sets=[]
    for ident,items in sorted(groups.items()):
        representative=next((i for i in items if i['part']=='body'),items[0])
        sets.append({'series_id':ident,'name':series_name([i['name'] for i in items]),
                     'name_source':'common_part_name_prefix_with_shared_rank_suffix',
                     'model_address':representative['model_address'],'selectable':True,
                     'gender_restriction':None,
                     'part_records':[{'part':i['part'],'armor_id':i['armor_id']} for i in items],
                     'exportable_parts':{sex:[i['part'] for i in items if i['resource_paths'][sex]['status']=='available'] for sex in SEXES}})
    data={'schema':'mhwi-equipment-catalogue-v2',
          'source':{'workbook_filename':source.name,'sha256':digest,'sheet':sheet_name,
                    'columns':['part','armor_id','armor_name','series_id','male_equipment_directory','female_equipment_directory'],
                    'reuse_permission':'unrestricted use and redistribution confirmed by the supplying user',
                    'source_rows':len(rows),'prior_restricted_workbook_used':False},
          'resource_path_source':{'inventory_mod3_count':len(files),
                     'inventory_paths_sha256':hashlib.sha256(('\n'.join(files)+'\n').encode()).hexdigest(),
                     'matching':'exact source gender directory, part folder and MOD3 filename; no fallback',
                     'game_file_contents_read':False},
          'sets':sets,'armor':armor,'model_resource_index':[{'model_id':m,'candidates':v} for m,v in sorted(models.items())]}
    if hashlib.sha256(source.read_bytes()).hexdigest()!=digest:raise ValueError('Workbook changed during extraction')
    counts=Counter(e['status'] for i in armor for e in i['resource_paths'].values())
    report={'source_rows':len(rows),'series':len(sets),'parts':dict(Counter(i['part'] for i in armor)),
            'path_status_counts':dict(counts),'matched_models':len(models),'source_sha256':digest,'workbook_unchanged':True}
    return data,report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--pl-root',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.resolve()==args.source.resolve() or args.output.resolve().is_relative_to(args.pl_root.resolve()):parser.error('Output must be separate from source files')
    data,report=build(args.source,args.pl_root)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes((json.dumps(data,ensure_ascii=False,indent=2)+'\n').encode())
    print(json.dumps(report))
