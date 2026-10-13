"""Read-only author workflow. Source selection is explicit; unresolved maps stop export."""
import copy
import json
import re
from pathlib import Path
from .backend.equipment_morph_export_pipeline import digest,read_json
from .backend.equipment_morph_package_pipeline import export_pipeline,package_id
from .backend.map_equipment_morph_job import generate,model_meshes

NAME='MHWI-EquipmentMorph'
def pin(path):
    path=Path(path).resolve();return {'path':str(path),'sha256':digest(path.read_bytes())}
def write(path,value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
def resource_id(path):
    normalized=Path(path).as_posix().replace('\\','/');match=re.search(r'(?:^|/)(pl/[A-Za-z0-9_/]+)\.mod3$',normalized)
    return match.group(1) if match else ''
def associated(value,base):
    path=Path(value)
    if path.is_absolute():return path
    for parent in [Path(base).parent,*Path(base).parents]:
        candidate=parent/path
        if candidate.exists():return candidate.resolve()
    raise ValueError('找不到作业关联文件：'+value)
def pinned_from(item,base):
    path=associated(item['path'],base)
    if digest(path.read_bytes())!=item['sha256']:raise ValueError('作业来源已改变：'+str(path))
    return path
def import_job(path):
    path=Path(path);pipeline=read_json(path)
    if pipeline['schema']!='mhw-equipment-morph-package-job-v1':raise ValueError('请选择统一数据包作业')
    assets=[];matrix=None
    for entry in pipeline['exports']:
        source=pinned_from(entry['job'],path);job=read_json(source)
        reference=pinned_from(job['export_reference'],source);current=read_json(reference)['export_matrix']
        if matrix is not None and matrix!=current:raise ValueError('多个部位的导出坐标设置不同')
        matrix=current
        for part in job['parts']:
            model=pinned_from(part['model'],source);mapping=pinned_from(part['mapping'],source)
            capture=pinned_from(part['reference_capture'],source)
            contract=entry['mesh_contracts'][part['part']]
            assets.append({'part':part['part'],'package':entry['packages'][part['part']],
                'model':str(model),'model_id':part['model_id'],'mapping':str(mapping),'reference_capture':str(capture),
                'bindings':[{'object':o['object'],'mesh':o['mesh'],'material':o['material']} for o in read_json(mapping)['objects']],
                'keys':copy.deepcopy(part['keys']),
                'shading':'AUTHOR' if any(k['author_shading'] for k in part['keys']) else 'BASELINE',
                'author_rotations':str(pinned_from(part['author_rotations'],source)),
                'contract_mode':contract['mode'],'contract_file':str(pinned_from(contract['reference'],path)) if contract['mode']=='recorded' else ''})
            # Preserve exact source/companion proofs for this imported key set.
            for key in assets[-1]['keys']:
                for name in ['source_delta_reference','validated_positions']:
                    if name in key:key[name]={'path':str(pinned_from(key[name],source)),'sha256':key[name]['sha256']}
    return {'project_name':NAME,'export_matrix':matrix,'assets':assets}

def check_assets(assets):
    if not 0<len(assets)<=5 or len({a['part'] for a in assets})!=len(assets):raise ValueError('每个装备部位只能配置一次')
    packages=set()
    for asset in assets:
        package_id(asset['package'])
        if asset['package'].casefold() in packages:raise ValueError('数据包名称重复')
        packages.add(asset['package'].casefold())
        if not asset['bindings'] or not asset['keys']:raise ValueError('请选择参与网格和至少一个形态键')
        if len({b['object'] for b in asset['bindings']})!=len(asset['bindings']):raise ValueError('参与网格重复')
        if len({k['id'] for k in asset['keys']})!=len(asset['keys']):raise ValueError('接口名称重复')
        model=Path(asset['model'])
        if not model.is_file() or not asset['model_id']:raise ValueError('请选择实际MOD3，并指定游戏资源路径')

def reused_map(asset,model_sha):
    path=Path(asset['mapping']);mapping=read_json(path)
    if (mapping['part']!=asset['part'] or mapping['model_sha256']!=model_sha or mapping['model_id']!=asset['model_id']
            or not mapping.get('fully_resolved',all(o['fully_resolved'] for o in mapping['objects']))
            or mapping.get('selection_issues') or not all(o['fully_resolved'] for o in mapping['objects'])):
        raise ValueError('已有映射与所选模型不符或尚未解决')
    actual={(o['object'],o['mesh'],o['material']) for o in mapping['objects']}
    desired={(b['object'],b['mesh'],b['material']) for b in asset['bindings']}
    if actual!=desired:raise ValueError('已有映射与参与网格/模型网格选择不一致')
    if 'position_equivalence' in mapping:
        proofs=mapping['position_equivalence']['proofs']
        permitted=set.intersection(*(set(p['equal_fields'])-{'basis','evaluated'} for p in proofs)) if proofs else set()
        if not {k['source_key'] for k in asset['keys']}.issubset(permitted) or asset['shading']!='BASELINE':
            raise ValueError('该映射的重合点证明仅覆盖原选定位置键；新增键或法线需重新建立依据')
    return mapping

def export_recipe(recipe,output):
    from .backend.capture_equipment_morph_job import capture
    output=Path(output).resolve()
    if output.exists():raise ValueError('输出目录已存在，请选择新目录')
    check_assets(recipe['assets']);output.mkdir(parents=True,exist_ok=False);work=output/'authoring';work.mkdir()
    try:
        matrix_file=work/'export_coordinates.json';write(matrix_file,{'export_matrix':recipe['export_matrix']})
        empty=work/'baseline_rotations.bin';empty.write_bytes(b'')
        seed={'schema':'mhw-equipment-morph-export-job-v2','export_reference':pin(matrix_file),'parts':[]}
        for asset in recipe['assets']:
            model_pin=pin(asset['model']);_,meshes=model_meshes(Path(asset['model']).read_bytes())
            for binding in asset['bindings']:
                match=next((m for m in meshes if m['mesh']==binding['mesh']),None)
                if match is None or match['material']!=binding['material']:raise ValueError('参与网格的目标选择已失效')
            if asset.get('mapping'):
                mapping=reused_map(asset,model_pin['sha256'])
            else:
                mapping={'objects':[{'object':b['object'],'mesh':b['mesh'],'material':b['material'],'fully_resolved':False} for b in asset['bindings']]}
            selection=work/(asset['part']+'.selection.json');write(selection,mapping)
            part={'part':asset['part'],'model_id':asset['model_id'],'model':model_pin,'mapping':pin(selection),
                'keys':copy.deepcopy(asset['keys']),
                'author_rotations':pin(asset['author_rotations']) if asset['shading']=='AUTHOR' else pin(empty)}
            if asset.get('reference_capture'):part['reference_capture']=pin(asset['reference_capture'])
            seed['parts'].append(part)
        seed_path=work/'capture_job.json';write(seed_path,seed)
        initial=work/'initial_capture.json';capture(seed_path,initial)
        final=copy.deepcopy(seed);all_assets=recipe['assets'];per_key={}
        for asset,part in zip(all_assets,final['parts']):
            if asset.get('mapping'):
                part['mapping']=pin(asset['mapping'])
            else:
                mapping_job={'schema':'mhw-equipment-morph-mapping-job-v1','capture':pin(initial),
                    'export_reference':seed['export_reference'],'position_tolerance_game_units':.001,
                    'parts':[{'part':asset['part'],'model_id':asset['model_id'],'model':part['model'],
                        'objects':[{'object':b['object'],'material':b['material'],'mesh':b['mesh']} for b in asset['bindings']]}]}
                path=work/(asset['part']+'.mapping_job.json');write(path,mapping_job)
                mapped=work/(asset['part']+'.mapping');result=generate(path,mapped)
                if not result['success']:raise ValueError('映射未解决：'+asset['part']+'，请查看该部位mapping/results.json')
                part['mapping']=pin(mapped/(asset['part']+'.mapping.json'));part['reference_capture']=pin(initial)
            if asset['shading']=='INTENT':
                from .normal_authoring import generate as generate_normals
                raw,report,targets=generate_normals(asset,read_json(part['mapping']['path']),recipe['export_matrix'])
                normal_path=work/(asset['part']+'.author_normals.bin');normal_path.write_bytes(raw)
                write(work/(asset['part']+'.author_normals_report.json'),report)
                write(work/(asset['part']+'.author_normal_targets.json'),targets)
                part['author_rotations']=pin(normal_path)
            if asset['shading']=='GEOMETRIC':
                from .geometric_normals import generate as generate_per_key
                raw,report=generate_per_key(asset,read_json(part['mapping']['path']),recipe['export_matrix'])
                per_key[asset['part']]=raw
                write(work/(asset['part']+'.per_key_normals_report.json'),report)
                for key in part['keys']:key['author_shading']=False
        export_job=work/'export_job.json';write(export_job,final)
        current=work/'export_capture.json';capture(export_job,current)
        contracts={a['part']:{'mode':a['contract_mode'],**({'reference':pin(a['contract_file'])} if a['contract_mode']=='recorded' else {})} for a in all_assets}
        job={'schema':'mhw-equipment-morph-package-job-v1','exports':[{'job':pin(export_job),'capture':pin(current),
            'packages':{a['part']:a['package'] for a in all_assets},'mesh_contracts':contracts}]}
        package_job=work/'package_job.json';write(package_job,job)
        result=export_pipeline(package_job,output/'generated',per_key)
        from .output_files import publish
        publish(output/'generated/nativePC',output/'nativePC')
        result.update(project_name=NAME,live_blender_captured=True,game_installed=False)
        write(output/'result.json',result);return result
    except Exception as error:
        write(output/'result.json',{'project_name':NAME,'success':False,'reason':str(error),'game_installed':False,'blend_saved':False})
        raise
