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

def runtime_directory(value=None):
    if value is None:
        config=ROOT/'config/mhwi-equipmentmorph-current.json'
        if not config.is_file():raise ValueError('请用 --runtime-config 指定已安装运行时的配置目录或运行时解压目录')
        value=ROOT/json.loads(config.read_bytes())['release_directory']
    path=Path(value).absolute()
    if path.name.lower()=='runtime.ini':path=path.parent
    if not (path/'runtime.ini').is_file():raise ValueError('未找到运行时配置，请先安装运行时或指定正确的 --runtime-config')
    return path

def verify(validator,root,runtime_config=None):
    root=Path(root);config=runtime_directory(runtime_config)
    process=subprocess.run([str(validator),str(config),str(root/'nativePC')],capture_output=True,text=True)
    report=json.loads(process.stdout)
    if process.returncode or not report['native_loader_valid']:raise ValueError('Native package check failed: '+report.get('error','unknown'))
    return report

def prepare(source,output,part,model_id,validator,installed_model=None,equipment_id='',runtime_config=None):
    normalize,encode,retarget=backend()
    source=Path(source).resolve();output=Path(output).resolve()
    if output.exists():raise ValueError('Prepared output already exists; choose a new directory')
    verified=verify(validator,source,runtime_config)
    records=[p for p in verified['parts'] if p['part']==PARTS[part]]
    if len(records)!=1:raise ValueError('请选择仅包含一个待迁移部位模型的包；同部位多模型请分别迁移')
    record=records[0];old_id=normalize(record['model_id'])
    model_id=retarget(old_id,equipment_id) if equipment_id else normalize(model_id)
    folder=source/'nativePC'/record['package']
    description=json.loads((folder/'profile.json').read_bytes());raw=(folder/'asset.mhmpack').read_bytes()
    if description['part']!=PARTS[part]:raise ValueError('Source package belongs to another part')
    old_profile=(folder/'model.mhmprofile').read_bytes()
    sha=lambda data:hashlib.sha256(data).hexdigest()
    if (description['model_id']!=old_id or sha(raw)!=record['asset_sha256']
            or description['asset_sha256']!=sha(raw) or encode(description,raw)!=old_profile):
        raise ValueError('Readable profile differs from native model/package')
    model=(source/'nativePC'/(description['model_id']+'.mod3')).read_bytes()
    if sha(model)!=description['model_sha256'] or sha(model)!=record['model_sha256']:raise ValueError('Source model digest differs')
    if installed_model and Path(installed_model).read_bytes()!=model:raise ValueError('Installed target model differs from source export')
    old_length=struct.unpack_from('<I',raw,28)[0];old_id=raw[32:32+old_length].decode('ascii')
    if old_id!=description['model_id']:raise ValueError('Source resource identity differs')
    new_id=model_id.encode('ascii');updated=raw[:28]+struct.pack('<I',len(new_id))+new_id+raw[32+old_length:]
    description=copy.deepcopy(description);description['model_id']=model_id;description['asset_sha256']=sha(updated)
    new_profile=encode(description,updated)
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='morph-resource-stage-',dir=output.parent) as temporary:
        stage=Path(temporary)/'export';stage.mkdir()
        if not Path(temporary).resolve().is_relative_to(output.parent):raise ValueError('Temporary migration escaped output parent')
        shutil.copytree(source/'nativePC',stage/'nativePC')
        # A MOD never owns the runtime configuration, even if old files are present.
        ini=stage/'nativePC/plugins/MHWEquipmentMorph/runtime.ini'
        if ini.exists():ini.unlink()
        for directory in (ini.parent,ini.parent.parent,ini.parent.parent.parent):
            if directory.exists() and not any(directory.iterdir()):directory.rmdir()
        if model_id!=old_id and ((stage/'nativePC'/(model_id+'.mod3')).exists() or (stage/'nativePC'/(model_id+'.morph')).exists()):
            raise ValueError('Target model/package already exists; migration would overwrite another MOD')
        target=stage/'nativePC'/(model_id+'.mod3');target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(model)
        package=stage/'nativePC'/(model_id+'.morph')
        if old_id!=model_id:
            (stage/'nativePC'/(old_id+'.mod3')).unlink()
            old_package=stage/'nativePC'/record['package']
            if not old_package.resolve().is_relative_to(stage.resolve()) or not package.resolve().is_relative_to(stage.resolve()):
                raise ValueError('Migration package escaped staging root')
            os.rename(old_package,package)
        (package/'asset.mhmpack').write_bytes(updated);(package/'model.mhmprofile').write_bytes(new_profile)
        (package/'profile.json').write_text(json.dumps(description,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        report=verify(validator,stage,runtime_config)
        os.rename(stage,output)
    return {'output':str(output),'model_id':model_id,'from_model_id':old_id,'native_check':report,
            'geometry_and_sparse_payloads_unchanged':True,'source_export_changed':False,'game_files_changed':False}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--part',choices=PARTS,required=True)
    target=parser.add_mutually_exclusive_group(required=True);target.add_argument('--model-id');target.add_argument('--equipment-id')
    local=Path(__file__).with_name('EquipmentMorphPackageValidator.exe')
    parser.add_argument('--validator',type=Path,default=local if local.exists() else ROOT/'.build/current-validation/package-validator/EquipmentMorphPackageValidator.exe')
    parser.add_argument('--installed-model',type=Path)
    parser.add_argument('--runtime-config',type=Path,help='Runtime configuration directory or runtime.ini; never read from the MOD')
    args=parser.parse_args()
    print(json.dumps(prepare(args.source,args.output,args.part,args.model_id,args.validator,args.installed_model,args.equipment_id or '',args.runtime_config),ensure_ascii=False))

if __name__=='__main__':main()
