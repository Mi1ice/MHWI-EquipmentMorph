"""Blender --background --factory-startup --python-exit-code 1 --python this file."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
from unittest.mock import patch
import bpy

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'blender_addon_v044'))
sys.path.insert(0,str(ROOT/'tools'))
import mhwi_equipmentmorph as addon
from mhwi_equipmentmorph.direct_export import export_direct
from mhwi_equipmentmorph import authoring
from prepare_mhwi_equipmentmorph_resource_path import verify,prepare
import manage_equipment_morph_mod as management
addon.register()
state=addon.settings(bpy.context);checks=[]
def check(ok,label):assert ok,label;checks.append(label)
def rejected(call,label):
    try:call()
    except (ValueError,RuntimeError,OSError):check(True,label)
    else:raise AssertionError(label)
fixture=ROOT/'tools/blender_export_fixture.py'
namespace={'__file__':str(fixture)}
exec(compile(fixture.read_text(encoding='utf-8').split('collection,obj=fixture()')[0],str(fixture),'exec'),namespace)
collection,obj=namespace['fixture']()
from mhwi_equipmentmorph.backend.capture_equipment_morph_job import fingerprint
baseline=fingerprint(obj)
asset=state.assets.add();asset.part='body';asset.collection=collection;asset.series_id=250
addon.refresh_keys(asset)
key=next(k for k in asset.keys if k.source=='Smile');key.selected=True;key.public_id='smile';bpy.ops.mhwi_em.confirm_keys()
check(not state.debug,'Debug defaults off')
validator=ROOT/'.build/current-validation/package-validator/EquipmentMorphPackageValidator.exe'
runtime=ROOT/'config'
parent=ROOT/'.build/current-validation/debug-export-tests';parent.mkdir(parents=True,exist_ok=True)
folder=Path(tempfile.mkdtemp(prefix='mhwi-debug-'));runs={}
model_id='pl/f_equip/pl105_0000/body/mod/f_body105_0000'
def files(root):return sorted(p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file())
def no_ini(root):return not list(root.rglob('runtime.ini'))
for shading in ('BASELINE','GEOMETRIC'):
    asset.direct_shading=shading
    for debug in (False,True):
        destination=folder/(shading+('-debug' if debug else '-release'))
        state.output=str(destination);state.debug=debug
        check(bpy.ops.mhwi_em.export_package()=={'FINISHED'},shading+' UI export '+str(debug))
        check(no_ini(destination),shading+' neither mode exports runtime.ini '+str(debug))
        expected=['nativePC/'+model_id+'.mod3',*['nativePC/'+model_id+'.morph/'+f for f in ('asset.mhmpack','model.mhmprofile','profile.json')]]
        if not debug:
            check(files(destination)==sorted(expected),shading+' minimal export contains exactly four required files')
            check(state.result_path=='',shading+' result path cleared in normal mode')
        else:
            check(all((destination/p).exists() for p in ('authoring','generated','result.json')),shading+' Debug retains all authoring records')
            check(state.result_path==str(destination/'result.json'),shading+' Debug result path points to report')
        check(verify(validator,destination,runtime)['native_loader_valid'],shading+' external runtime validates export '+str(debug))
        runs[(shading,debug)]=destination
    normal=runs[(shading,False)];debugged=runs[(shading,True)]
    a=(normal/'nativePC'/(model_id+'.mod3')).read_bytes();b=(debugged/'nativePC'/(model_id+'.mod3')).read_bytes()
    check(a[:40]+b'\0'*8+a[48:]==b[:40]+b'\0'*8+b[48:],shading+' Debug changes no model payload')
    check((normal/'nativePC'/(model_id+'.morph/asset.mhmpack')).read_bytes()==(debugged/'nativePC'/(model_id+'.morph/asset.mhmpack')).read_bytes(),shading+' Debug changes no shape-key pack bytes')

# Reusing a folder does not delete historical Debug records or unrelated files.
old=folder/'existing-debug';shutil.copytree(runs[('BASELINE',True)],old)
kept=(old/'result.json').read_bytes();(old/'keep.txt').write_bytes(b'keep')
state.debug=False;state.output=str(old);asset.direct_shading='BASELINE';bpy.ops.mhwi_em.export_package()
check((old/'result.json').read_bytes()==kept and (old/'keep.txt').read_bytes()==b'keep','normal re-export preserves historical records and unrelated files')
check(not list(folder.rglob('morph-direct-stage-*')),'direct export cleans every staging directory')

# Existing MOD3 workflow must obey the same minimal/Debug contract.
debugged=runs[('BASELINE',True)]
trace=json.loads((debugged/'authoring/0/body.trace.json').read_bytes())
legacy=dict(part='body',package='legacy-body',model=str(debugged/'nativePC'/(model_id+'.mod3')),model_id=model_id,
    bindings=[{'object':o['object'],'mesh':o['mesh'],'material':o['material']} for o in trace['objects']],
    keys=[{'id':'smile','source_key':'Smile','author_shading':False}],mapping=str(debugged/'authoring/0/body.trace.json'),
    reference_capture=str(debugged/'authoring/0/body.reference.json'),shading='BASELINE',contract_mode='serialized',contract_file='',author_rotations='')
recipe={'assets':[legacy],'export_matrix':[list(row) for row in namespace['exporter'].exportMatrix]}
for debug in (False,True):
    destination=folder/('legacy-'+str(debug));destination.mkdir()
    (destination/'keep.txt').write_bytes(b'keep')
    result=authoring.export_recipe(recipe,destination,debug=debug)
    check(result['assets'] and verify(validator,destination,runtime)['native_loader_valid'],'existing MOD3 export validates '+str(debug))
    check(no_ini(destination),'existing MOD3 emits no runtime config '+str(debug))
    check((destination/'authoring').exists()==debug and (destination/'result.json').exists()==debug,'existing MOD3 Debug flag controls auxiliary output '+str(debug))
    check((destination/'keep.txt').read_bytes()==b'keep','existing MOD3 preserves unrelated files '+str(debug))
check(not list(folder.rglob('morph-author-stage-*')),'existing MOD3 staging cleaned')

# Minimal migration uses final validated files, not any Debug result/manifest.
source=runs[('GEOMETRIC',False)];moved=folder/'moved'
result=prepare(source,moved,'body',None,validator,equipment_id='001',runtime_config=runtime)
new_id=model_id.replace('105','001')
check(files(moved)==sorted('nativePC/'+new_id+s for s in ('.mod3','.morph/asset.mhmpack','.morph/model.mhmprofile','.morph/profile.json')),'migration produces minimal model/sidecar only')
before=(source/'nativePC'/(model_id+'.morph/asset.mhmpack')).read_bytes();after=(moved/'nativePC'/(new_id+'.morph/asset.mhmpack')).read_bytes()
old_length=struct.unpack_from('<I',before,28)[0];new_length=struct.unpack_from('<I',after,28)[0]
check(before[32+old_length:]==after[32+new_length:],'migration preserves all geometric normal and position payloads')
check((source/'nativePC'/(model_id+'.mod3')).read_bytes()==(moved/'nativePC'/(new_id+'.mod3')).read_bytes(),'migration preserves MOD3 bytes')
check(no_ini(moved) and result['native_check']['native_loader_valid'],'migrated package uses external runtime config')
bad=folder/'bad-profile';shutil.copytree(source,bad)
profile=bad/'nativePC'/(model_id+'.morph/profile.json');raw=json.loads(profile.read_bytes());raw['model_id']='pl/forged/body';profile.write_text(json.dumps(raw),encoding='utf-8')
rejected(lambda:prepare(bad,folder/'bad-move','body',None,validator,equipment_id='001',runtime_config=runtime),'forged human-readable profile rejected')
rejected(lambda:prepare(source,folder/'wrong-part','head',None,validator,equipment_id='001',runtime_config=runtime),'wrong part migration rejected')
rejected(lambda:verify(validator,source,folder/'missing-runtime'),'missing external runtime config rejected')

# MOD installers only own model and sidecar files; no public config copies or conflicts.
game=folder/'game';game.mkdir();(game/'MonsterHunterWorld.exe').write_bytes(b'fixture')
rejected(lambda:management.plan(source,game,validator),'runtime installation required before MOD plan')
config=game/'nativePC/plugins/MHWEquipmentMorph';config.mkdir(parents=True);ini=config/'runtime.ini';ini.write_bytes((runtime/'runtime.ini').read_bytes())
config_before=ini.read_bytes()
with patch.object(management,'closed'):
    plan=management.plan(source,game,validator)
    check(len(plan['files'])==4 and not any(f['relative'].endswith('runtime.ini') for f in plan['files']),'installation plan excludes global runtime config')
    management.install(source,game,validator)
    management.install(moved,game,validator)
    checked=verify(validator,game,config)
    check(len(checked['parts'])==2 and ini.read_bytes()==config_before,'two MODs coexist without rewriting shared config')
    check(all(f['status']=='identical' for f in management.plan(source,game,validator)['files']),'repeat installation checks model files only')
    real_verify=management.verify
    def fail_final(v,root,configuration=None):
        if Path(root)==game:raise ValueError('injected final validation failure')
        return real_verify(v,root,configuration)
    fresh=folder/'moved-second';prepare(source,fresh,'body',None,validator,equipment_id='002',runtime_config=runtime)
    with patch.object(management,'verify',side_effect=fail_final):
        rejected(lambda:management.install(fresh,game,validator),'failed final verification reports error')
    check(not (game/'nativePC'/new_id.replace('001','002')).with_suffix('.mod3').exists() and ini.read_bytes()==config_before,'rollback removes new MOD files while preserving runtime config')

check(fingerprint(obj)==baseline,'all exports preserve synthetic source data')
addon.unregister()
report={'version':'0.4.4','passed':len(checks),'checks':checks,'blender':list(bpy.app.version),
        'test_directory':str(folder),'normal_and_debug_no_runtime_ini':True,'global_runtime_config_unchanged':True,
        'real_mod_project_used':False,'user_project_saved':False,'game_directory_modified':False}
(ROOT/'.build/current-validation/addon-debug-export.json').write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
print('PASS: '+str(len(checks))+' Debug, minimal export, migration and runtime ownership checks')
