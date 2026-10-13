"""Prepare an independently validated copy with a corrected resource path.

No source, Blender, installed package or game file is changed. Geometry and all
sparse payload bytes are preserved; only the pack/profile model identity changes.
"""
import argparse
import copy
import hashlib
import importlib
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import types

ROOT=Path(__file__).resolve().parents[1]
PARTS={'head':0,'body':1,'arms':2,'waist':3,'legs':4}

def backend():
    config=ROOT/'config/mhwi-equipmentmorph-current.json'
    if config.exists():
        folder=ROOT/json.loads(config.read_bytes())['addon_directory']/'mhwi_equipmentmorph';base=folder/'backend'
    else:folder=base=Path(__file__).with_name('author_backend')
    for name,path in [('_mhm_prepare',folder),('_mhm_prepare.backend',base)]:
        module=types.ModuleType(name);module.__path__=[str(path)];sys.modules[name]=module
    return (importlib.import_module('_mhm_prepare.output_files').model_resource_id,
            importlib.import_module('_mhm_prepare.backend.emit_equipment_morph_profile').encode,
            importlib.import_module('_mhm_prepare.output_files').retarget_model)

def verify(validator,root):
    process=subprocess.run([str(validator),str(root/'nativePC/plugins/MHWEquipmentMorph'),str(root/'nativePC')],capture_output=True,text=True)
    report=json.loads(process.stdout)
    if process.returncode or not report['native_loader_valid']:raise ValueError('Native package check failed: '+report.get('error','unknown'))
    return report

def prepare(source,output,part,model_id,validator,installed_model=None,equipment_id=''):
    normalize,encode,retarget=backend()
    source=Path(source).resolve();output=Path(output).resolve()
    if output.exists():raise ValueError('Prepared output already exists; choose a new directory')
    verified=verify(validator,source)
    result=json.loads((source/'result.json').read_bytes())
    if not result.get('success'):raise ValueError('Source export was not successful')
    entries=[e for e in result['assets'] if e['part']==part];models=[e for e in result['models'] if e['part']==part]
    if len(entries)!=1 or len(models)!=1:raise ValueError('请选择仅包含一个待迁移模型的导出；同部位多模型请分别迁移')
    entry=entries[0];model_entry=models[0]
    old_id=normalize(models[0]['file'].removeprefix('nativePC/'))
    if entry['package']!=old_id+'.morph':raise ValueError('Source package does not match model resource path')
    model_id=retarget(old_id,equipment_id) if equipment_id else normalize(model_id)
    if not any(p['part']==PARTS[part] for p in verified['parts']):raise ValueError('Part is not selected in source configuration')
    folder=source/'nativePC'/entry['package']
    description=json.loads((folder/'profile.json').read_bytes());raw=(folder/'asset.mhmpack').read_bytes()
    if description['part']!=PARTS[part]:raise ValueError('Source package belongs to another part')
    old_profile=(folder/'model.mhmprofile').read_bytes()
    sha=lambda data:hashlib.sha256(data).hexdigest()
    if sha(raw)!=entry['asset_sha256'] or sha(old_profile)!=entry['profile_sha256'] or encode(description,raw)!=old_profile:raise ValueError('Source description/export receipt differs')
    model=(source/'nativePC'/(description['model_id']+'.mod3')).read_bytes()
    if sha(model)!=description['model_sha256'] or sha(model)!=model_entry['sha256']:raise ValueError('Source model digest differs')
    records=[p for p in verified['parts'] if p['part']==PARTS[part] and p['package']==entry['package']]
    if len(records)!=1 or records[0]['model_id']!=description['model_id'] or records[0]['asset_sha256']!=sha(raw):
        raise ValueError('Source receipt differs from selected native package')
    if installed_model and Path(installed_model).read_bytes()!=model:raise ValueError('Installed target model differs from source export')
    old_length=struct.unpack_from('<I',raw,28)[0];old_id=raw[32:32+old_length].decode('ascii')
    if old_id!=description['model_id']:raise ValueError('Source resource identity differs')
    new_id=model_id.encode('ascii');updated=raw[:28]+struct.pack('<I',len(new_id))+new_id+raw[32+old_length:]
    description=copy.deepcopy(description);description['model_id']=model_id;description['asset_sha256']=sha(updated)
    new_profile=encode(description,updated)
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='morph-resource-stage-',dir=output.parent) as temporary:
        stage=Path(temporary)/'export';stage.mkdir()
        for name in ('nativePC','generated','authoring'):shutil.copytree(source/name,stage/name)
        target=stage/'nativePC'/(model_id+'.mod3');target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(model)
        package=stage/'nativePC'/(model_id+'.morph')
        if old_id!=model_id:
            (stage/'nativePC'/(old_id+'.mod3')).unlink()
            os.rename(stage/'nativePC'/entry['package'],package)
        (package/'asset.mhmpack').write_bytes(updated);(package/'model.mhmprofile').write_bytes(new_profile)
        (package/'profile.json').write_text(json.dumps(description,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        manifest=stage/'generated/export'/old_id/'manifest.json'
        metadata=json.loads(manifest.read_bytes())
        if metadata['model_id']!=old_id or metadata['model_sha256']!=sha(model):raise ValueError('Source manifest identity differs')
        metadata['model_id']=model_id
        manifest.write_text(json.dumps(metadata,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        # The copied authoring receipts retain the original source identity.
        # This derivative is validated from its final model/pack/profile files.
        entry.update(package=model_id+'.morph',asset_sha256=sha(updated),profile_sha256=sha(new_profile))
        model_entry['file']=target.relative_to(stage).as_posix()
        result['resource_path_adjustment']=dict(part=part,from_model_id=old_id,to_model_id=model_id,
            source_result_sha256=sha((source/'result.json').read_bytes()),geometry_and_sparse_payloads_unchanged=True,
            source_asset_sha256=sha(raw),source_profile_sha256=sha(old_profile),
            installed_model_checked=bool(installed_model),source_export_changed=False,game_files_changed=False)
        (stage/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        report=verify(validator,stage)
        os.rename(stage,output)
    return {'output':str(output),'model_id':model_id,'native_check':report,'game_files_changed':False}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--part',choices=PARTS,required=True)
    target=parser.add_mutually_exclusive_group(required=True);target.add_argument('--model-id');target.add_argument('--equipment-id')
    local=Path(__file__).with_name('EquipmentMorphPackageValidator.exe')
    parser.add_argument('--validator',type=Path,default=local if local.exists() else ROOT/'.build/current-validation/package-validator/EquipmentMorphPackageValidator.exe')
    parser.add_argument('--installed-model',type=Path)
    args=parser.parse_args()
    print(json.dumps(prepare(args.source,args.output,args.part,args.model_id,args.validator,args.installed_model,args.equipment_id or ''),ensure_ascii=False))

if __name__=='__main__':main()
