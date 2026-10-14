"""Mapped source captures -> configured runtime packages, without scene/install changes."""
import argparse
import json
import os
import re
import shutil
import struct
import tempfile
import time
from pathlib import Path
import numpy as np

from .equipment_morph_export_pipeline import ROOT, build_part, digest, load_job, pinned, read_json
from .prepare_equipment_morph_catalog import PARTS, prepare
from .emit_equipment_morph_profile import ADAPTER, encode
from .mesh_contract import normalized_rows, MASK

def package_id(value):
    if not isinstance(value,str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,63}',value):
        raise ValueError('Invalid package directory identifier')
    upper=value.upper()
    if upper in ('CON','PRN','AUX','NUL') or re.fullmatch(r'(COM|LPT)[0-9]',upper):
        raise ValueError('Reserved package directory identifier')
    return value

def capture_checked(raw,job_sha):
    capture=json.loads(raw)
    if (capture['schema']!='mhw-equipment-morph-job-capture-v2' or capture['job_sha256']!=job_sha
            or not capture['source_state_unchanged'] or capture['scene_mutated'] or capture['blend_saved']):
        raise ValueError('Capture must be unchanged, unsaved and bound to its export job')
    if len({o['name'] for o in capture['objects']})!=len(capture['objects']):
        raise ValueError('Duplicate captured source object')
    return capture

def source_baseline(part,capture,mapping):
    objects={o['name']:o for o in capture['objects']};result=[]
    source_names=[k['source_key'] for k in part['keys']]
    if len(set(source_names))!=len(source_names):raise ValueError('Two public IDs alias the same source key')
    for mapped in mapping['objects']:
        obj=objects[mapped['object']];fingerprint=obj['legacy_fingerprint']
        values=[];dynamic={k['source_key']:k['id'] for k in part['keys'] if k['source_key'] in obj['selected_keys']}
        seen=set()
        for name,weight,mute in fingerprint['values']:
            public=dynamic.get(name,name)
            if public in seen:raise ValueError('Public alias collides with another baseline key')
            seen.add(public)
            if name in dynamic:
                metadata=next(k for k in obj['keys'] if k['name']==name)
                if weight!=0 or mute or metadata['value']!=weight or metadata['mute']!=mute:
                    raise ValueError('Dynamic source reference must be zero and unmuted')
            # Full key values and geometry remain in the pinned capture. The
            # bounded runtime profile needs only controlled and nonzero keys.
            if name in dynamic or weight!=0:
                values.append({'name':public,'source_name':name,'weight':weight,'muted':mute,'dynamic':name in dynamic})
        if not set(dynamic).issubset({v['source_name'] for v in values}):raise ValueError('Selected source key missing from fingerprint')
        if not values:continue  # Uncontrolled zero-only objects remain in the full capture.
        result.append({'name':obj['name'],'geometry_sha256':fingerprint['geometry_and_encoding_sha256'],
                       'matrix_world':obj['matrix_world'],'values':values})
    return result

def model_description(part,model,contract):
    mesh_count=struct.unpack_from('<H',model,8)[0];vertex_bytes=struct.unpack_from('<I',model,24)[0]
    mesh_start=struct.unpack_from('<Q',model,72)[0]
    if not 0<mesh_count<=4096 or mesh_start+mesh_count*80>len(model):raise ValueError('Mesh description bounds')
    rows=bytearray(model[mesh_start:mesh_start+mesh_count*80]);index=PARTS[part['part']]
    mask=MASK
    for i in range(mesh_count):
        for at in mask:rows[i*80+at]=0
    serialized=digest(normalized_rows(model))
    if contract['mode']=='serialized':
        runtime_sha=serialized;provenance='adapter-v2 disk-derived mesh identity; independent runtime format validation'
    elif contract['mode']=='recorded':
        evidence=json.loads(pinned(contract['reference']))
        records=[p for p in evidence['profiles'] if p['part']==index]
        if len(records)!=1:raise ValueError('Native contract part coverage')
        record=records[0]
        reference_mask=list(range(72,80)) if index==1 and sorted(record['normalization'])==list(range(72,80)) else mask
        reference_rows=bytearray(model[mesh_start:mesh_start+mesh_count*80])
        for i in range(mesh_count):
            for at in reference_mask:reference_rows[i*80+at]=0
        if (record['model_id']!=part['model_id'] or record['model_sha256']!=digest(model)
                or record['model_bytes']!=len(model) or record['vertex_bytes']!=vertex_bytes
                or record['mesh_count']!=mesh_count or sorted(record['normalization'])!=sorted(reference_mask)
                or record['serialized_normalized_mesh_sha256']!=digest(reference_rows)):
            raise ValueError('Recorded native contract belongs to a different model/layout')
        runtime_sha=serialized;provenance='adapter-v2 disk-derived identity; supplied reference model/layout verified'
    else:raise ValueError('Unsupported native mesh contract mode')
    return {'model_bytes':len(model),'vertex_bytes':vertex_bytes,'mesh_count':mesh_count,
            'runtime_mesh_sha256':runtime_sha,'mesh_contract_source':provenance,
            'serialized_normalized_mesh_sha256':serialized,'normalization_adapter':ADAPTER}

def load_pipeline(path):
    job=read_json(path)
    if job['schema']!='mhw-equipment-morph-package-job-v1':raise ValueError('Package job schema')
    if not 0<len(job['exports'])<=64:raise ValueError('Export job count')
    chosen=set();names=set();pending=[]
    for entry in job['exports']:
        raw=pinned(entry['job']);export_job=json.loads(raw)
        # Reuse the existing job validator without changing its fixed root rules.
        export_path=Path(entry['job']['path']);export_path=export_path if export_path.is_absolute() else ROOT/export_path
        load_job(export_path)
        capture_raw=pinned(entry['capture']);capture=capture_checked(capture_raw,digest(raw))
        export=np.asarray(json.loads(pinned(export_job['export_reference']))['export_matrix'],dtype=np.float64)
        if export.shape!=(4,4) or not np.isfinite(export).all() or not np.array_equal(export[3],[0,0,0,1]):raise ValueError('Export transform')
        if set(entry['packages'])!=set(p['part'] for p in export_job['parts']):raise ValueError('Package selection differs from export parts')
        if set(entry['mesh_contracts'])!=set(entry['packages']):raise ValueError('Missing native contract selection')
        for part in export_job['parts']:
            name=part['model_id']
            if name.casefold() in names:raise ValueError('Duplicate model resource path')
            names.add(name.casefold())
            model=pinned(part['model']);files,manifest=build_part(part,capture,export)
            mapping=json.loads(pinned(part['mapping']))
            if (mapping.get('part')!=part['part'] or mapping.get('model_id')!=part['model_id']
                    or mapping.get('model_sha256')!=part['model']['sha256']):
                raise ValueError('Mapping model identity differs')
            description={'schema':'mhw-equipment-morph-profile-v1','adapter':ADAPTER,'part':PARTS[part['part']],
                'model_id':part['model_id'],'model_sha256':digest(model),'capture_sha256':digest(capture_raw),
                'controlled_keys':[k['id'] for k in part['keys']],
                'key_bindings':[{'id':k['id'],'source_key':k['source_key']} for k in part['keys']],
                'baseline_semantics':'zero_contribution_restores_exported_mod3','fixed_values_baked_in_model':True,
                'source_objects':source_baseline(part,capture,mapping),
                'provenance':{'export_job_sha256':digest(raw),'mapping_sha256':part['mapping']['sha256'],
                              'author_rotations_sha256':part['author_rotations']['sha256']},
                'formal_status':'仍需进行验证',**model_description(part,model,entry['mesh_contracts'][part['part']])}
            pending.append((part['part'],name,files,manifest,description,model))
    return pending

def export_pipeline(job_path,output,per_key=None):
    output=Path(output).resolve()
    if output.exists():raise ValueError('Refusing to overwrite package output')
    pending=load_pipeline(job_path)  # All source/mapping/contract checks before publishing.
    output.parent.mkdir(parents=True,exist_ok=True)
    temporary=Path(tempfile.mkdtemp(prefix='morph-package-stage-',dir=output.parent))
    try:
        assets=[]
        for part,name,files,manifest,description,model in pending:
            folder=temporary/'export'/name;folder.mkdir(parents=True)
            for filename,raw in files.items():(folder/filename).write_bytes(raw)
            (folder/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            packed,_,_=prepare(folder/'manifest.json')
            if per_key and name in per_key:
                from ..per_key_pack import upgrade
                normals=per_key[name];packed=upgrade(packed,normals)
                metadata={}
                for index,(key,raw) in enumerate(normals.items()):
                    normal_file=f'key-{index}.normals.bin';(folder/normal_file).write_bytes(raw)
                    metadata[key]={'file':normal_file,'records':len(raw)//16,'sha256':digest(raw)}
                manifest['per_key_normals']={'mode':'per-key-direction-deltas','keys':metadata,'asset_format':2}
                manifest['schema']='mhwi-equipmentmorph-package-v2'
                manifest['shading']['mode']='per-key-direction-deltas'
                (folder/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
                description['normal_composition']='per-key-direction-deltas';description['asset_format']=2
                description['per_key_normals']=metadata
            description['asset_sha256']=digest(packed);profile=encode(description,packed)
            package=temporary/'nativePC'/(name+'.morph');package.mkdir(parents=True)
            (package.parent/(package.name[:-6]+'.mod3')).write_bytes(model)
            (package/'asset.mhmpack').write_bytes(packed);(package/'model.mhmprofile').write_bytes(profile)
            (package/'profile.json').write_text(json.dumps(description,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            assets.append({'part':part,'package':name+'.morph','keys':description['controlled_keys'],
                'asset_sha256':digest(packed),'profile_sha256':digest(profile),'source_objects':len(description['source_objects']),
                'asset_format':description.get('asset_format',1)})
        result={'schema':'mhw-equipment-morph-package-export-v1','job_sha256':digest(Path(job_path).read_bytes()),
                'assets':assets,'source_capture_reused':True,'scene_mutated':False,'blend_saved':False,
                'game_installed':False,'formal_acceptance_complete':False}
        (temporary/'report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        # Rename cannot replace an existing destination on Windows. A failed
        # build leaves no published package and never edits an accepted output.
        for attempt in range(6):
            try:
                os.rename(temporary,output);break
            except PermissionError:
                if output.exists() or attempt==5:raise
                time.sleep(.05*(2**attempt))
        return result
    finally:
        if temporary.exists():
            resolved=temporary.resolve()
            if resolved.parent!=output.parent or not resolved.name.startswith('morph-package-stage-'):
                raise ValueError('Temporary cleanup path escaped the chosen output parent')
            shutil.rmtree(resolved)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--job',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();print(json.dumps(export_pipeline(args.job,args.output),ensure_ascii=False))
if __name__=='__main__':main()
