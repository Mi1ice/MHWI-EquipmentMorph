"""Four real paired modes, native validation and transaction failure paths."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import copy
import json
import os
import shutil
import struct
from unittest.mock import patch
from authoring_test_support import *
from mhwi_equipmentmorph import direct_export as direct
from prepare_mhwi_equipmentmorph_resource_path import verify,prepare
from mhwi_equipmentmorph.backend.map_equipment_morph_job import model_meshes
from mathutils import Vector

fixture,exporter=load_fixture();collection,obj=fixture()
addon.register();state=addon.settings(bpy.context)
asset=state.assets.add();asset.collection=collection;asset.target_mode='MANUAL';asset.model_id='pl/f_equip/pl105_0000/body/mod/f_body105_0000'
addon.refresh_keys(asset)
for row in asset.keys:row.selected=row.source in ('Smile','Unused_1')
bpy.ops.mhwi_em.confirm_keys()
baseline=scene();model_id=asset.model_id
files=lambda p:sorted(x.relative_to(p).as_posix() for x in p.rglob('*') if x.is_file())

# A short output root can still exceed the certified embedded-Python limit
# inside the nested package. Rejection must precede exporter datablock changes.
def sized_directory(root,size,letter='中'):
    count=size-len(str(root.resolve()).encode('utf-16-le'))//2-1
    width=len(letter.encode('utf-16-le'))//2
    return root/(letter*(count//width)+'x'*(count%width))

recipe=addon.recipe_from_ui(bpy.context)
destination=sized_directory(ARGS.output,125);destination.mkdir()
(destination/'keep').write_bytes(b'keep')
old_model=destination/'nativePC'/(model_id+'.mod3');old_model.parent.mkdir(parents=True);old_model.write_bytes(b'old model')
before=(config(state),scene(),[(o.name,o.data.uv_layers.active_index) for o in collection.all_objects if o.type=='MESH'])
with patch.object(direct.tempfile,'mkdtemp',side_effect=AssertionError('temporary creation before path rejection')):
    message=rejected(lambda:direct.export_direct(recipe,destination),'deep output production rejection')
print('DEEP_OUTPUT_REJECTION',message)
check('导出路径过长' in message and '缩短输出目录' in message and str(destination) in message,'deep output gives actionable path error')
check((destination/'keep').read_bytes()==b'keep' and old_model.read_bytes()==b'old model' and not list(destination.glob('morph-direct-stage-*')),'deep output preserves old files without staging')
check((config(state),scene(),[(o.name,o.data.uv_layers.active_index) for o in collection.all_objects if o.type=='MESH'])==before,'deep output preserves config source keys and active UV')

# A later, longer model must reject the whole batch before creating output.
destination=sized_directory(ARGS.output,118,'😀')/'new'
long_model='pl/'+('long_'*10)+'model'
batch={'assets':[recipe['assets'][0],dict(recipe['assets'][0],model_id=long_model)]}
before=(config(state),scene())
with patch.object(direct.tempfile,'mkdtemp',side_effect=AssertionError('temporary creation before batch rejection')):
    message=rejected(lambda:direct.export_direct(batch,destination),'later longer model rejected')
check('导出路径过长' in message and long_model.replace('/',os.sep) in message and not destination.exists(),'later model rejects whole batch before output creation')
check((config(state),scene())==before,'later long path preserves author state')

# A new output name belongs to final/backup paths even though stage is in its parent.
destination=sized_directory(ARGS.output,190,'x')
message=rejected(lambda:direct.export_direct(recipe,destination,debug=True),'new long output basename rejected')
check('导出路径过长' in message and not destination.exists(),'new output final and backup paths checked')
destination=sized_directory(ARGS.output,164,'x')
message=rejected(lambda:direct.export_direct(recipe,destination),'backup path capacity rejected')
check('导出路径过长' in message and 'morph-output-backup-' in message and not destination.exists(),'backup space checked even when final profile fits')

# The last writable nested profile path (259 UTF-16 units) must still work.
boundary=sized_directory(ARGS.output,124);boundary.mkdir()
direct.export_direct(recipe,boundary)
check(verify(ARGS.validator,boundary,ROOT/'config')['native_loader_valid'],'259 UTF16 nested profile path really exports')

# Do not replace real short-path IO errors with a guessed length diagnosis.
for error in (FileNotFoundError(2,'missing source'),PermissionError(13,'access denied')):
    with patch.object(direct,'export_model',side_effect=error):
        message=rejected(lambda:direct.export_direct(recipe,ARGS.output/'io-error'),'short IO reason retained')
    check(str(error)==message and '路径过长' not in message,'real IO error not mislabeled '+type(error).__name__)

runs={}
for shading in ('BASELINE','GEOMETRIC'):
    asset.direct_shading=shading
    for debug in (False,True):
        destination=ARGS.output/('中文-'+shading+('-debug' if debug else '-normal'));state.output=str(destination);state.debug=debug
        check(bpy.ops.mhwi_em.export_package()=={'FINISHED'},shading+' UI export '+str(debug))
        check(not list(destination.rglob('runtime.ini')),'no runtime ini '+shading+str(debug))
        if debug:
            check(all((destination/p).exists() for p in ('authoring','generated','result.json')),'Debug evidence retained '+shading)
            check(state.result_path==str(destination/'result.json'),'current Debug report '+shading)
            trace=json.loads((destination/'authoring/0/body.trace.json').read_bytes())
            check(len(trace['objects'])==3 and all(x['disk_to_blender_vertex'] for x in trace['objects']),'vertex provenance '+shading)
        else:
            check(files(destination)==sorted('nativePC/'+model_id+s for s in ('.mod3','.morph/asset.mhmpack','.morph/model.mhmprofile','.morph/profile.json')),'exactly four normal files '+shading)
            check(state.result_path=='','normal has no current report '+shading)
        native=verify(ARGS.validator,destination,ROOT/'config')
        check(native['native_loader_valid'],'native paired validation '+shading+str(debug))
        basis,rows=model_meshes((destination/'nativePC'/(model_id+'.mod3')).read_bytes())
        expected=exporter.exportMatrix @ obj.matrix_world @ Vector((1,0,.04))
        positions=[Vector(struct.unpack_from('<3f',basis,row['start']+i*row['stride'])) for row in rows for i in range(row['count'])]
        check(min((position-expected).length for position in positions)<1e-5,'fixed neck=.4 baked into disk baseline '+shading+str(debug))
        description=json.loads((destination/'nativePC'/(model_id+'.morph/profile.json')).read_bytes())
        check(scene()==baseline,'source scene restored '+shading+str(debug))
        # Both a moved key and an all-zero-offset key survive the final package.
        raw=(destination/'nativePC'/(model_id+'.morph/asset.mhmpack')).read_bytes()
        check(struct.unpack_from('<I',raw,20)[0]==2 and b'Smile' in raw and b'Unused_1' in raw,'only selected key and zero displacement key retained '+shading+str(debug))
        runs[(shading,debug)]=destination
    normal=runs[(shading,False)];debugged=runs[(shading,True)]
    a=(normal/'nativePC'/(model_id+'.mod3')).read_bytes();b=(debugged/'nativePC'/(model_id+'.mod3')).read_bytes()
    check(a[:40]+b'\0'*8+a[48:]==b[:40]+b'\0'*8+b[48:],'normalized MOD3 equal across Debug '+shading)
    check((normal/'nativePC'/(model_id+'.morph/asset.mhmpack')).read_bytes()==(debugged/'nativePC'/(model_id+'.morph/asset.mhmpack')).read_bytes(),'paired pack equal across Debug '+shading)

old=ARGS.output/'reused-debug';shutil.copytree(runs[('BASELINE',True)],old)
kept=(old/'result.json').read_bytes();(old/'keep.txt').write_bytes(b'keep')
state.debug=False;state.output=str(old);asset.direct_shading='BASELINE'
check(bpy.ops.mhwi_em.export_package()=={'FINISHED'},'ordinary re-export into Debug directory')
check((old/'result.json').read_bytes()==kept and (old/'keep.txt').read_bytes()==b'keep' and state.result_path=='','historical Debug and unrelated bytes preserved without current report')

# Failure after model 1 has been staged cannot publish its files early.
recipe=addon.recipe_from_ui(bpy.context);second=dict(recipe['assets'][0],model_id=model_id.replace('105','001'));recipe['assets'].append(second)
destination=ARGS.output/'later-failure';destination.mkdir();(destination/'keep').write_bytes(b'keep')
real=direct.export_model
def fail_later(module,entry,path):
    if entry['model_id']==second['model_id']:raise RuntimeError('second model injected failure')
    return real(module,entry,path)
before=(config(state),scene())
with patch.object(direct,'export_model',side_effect=fail_later):rejected(lambda:direct.export_direct(recipe,destination),'later model failure')
check(files(destination)==['keep'] and (config(state),scene())==before,'later model has no partial publication or source changes')
recipe['assets'].pop()
with patch.object(direct,'export_pipeline',side_effect=RuntimeError('package injected failure')):rejected(lambda:direct.export_direct(recipe,destination),'package generation failure')
check(files(destination)==['keep'] and scene()==baseline,'package failure preserves destination and source')
with patch.object(direct,'publish',side_effect=OSError('publish injected failure')):message=rejected(lambda:direct.export_direct(recipe,destination),'publish failure')
check('publish injected failure' in message,'publication fault injection reached')
check(files(destination)==['keep'] and scene()==baseline,'publish failure preserves destination and source')

# Keep a real nested recovery backup when inner publication fails to restore.
real_publish=direct.publish
def retained_backup(stage,out):
    backup=Path(out)/'morph-output-backup-injected';backup.mkdir(parents=True);(backup/'old').write_bytes(b'original')
    raise RuntimeError('导出失败且自动恢复未完成；原文件备份保留在 '+str(backup))
with patch.object(direct,'publish',side_effect=retained_backup):message=rejected(lambda:direct.export_direct(recipe,destination),'nested recovery failure')
check('自动恢复未完成' in message and '备份保留' in message and '路径过长' not in message,'nested recovery fault reached and keeps priority message')
check(any(p.read_bytes()==b'original' for p in destination.rglob('morph-output-backup-*/old')),'outer cleanup preserves nested recovery bytes')

# Capacity guard belongs to model paths, not the five runtime slots.
with patch.object(direct,'editor_module',side_effect=RuntimeError('passed64')):
    for count in (6,64):
        entries=[dict(recipe['assets'][0],model_id='pl/test/model_'+str(i)) for i in range(count)]
        check('passed64' in rejected(lambda:direct.export_direct({'assets':entries},destination),'author path capacity '+str(count)),'author accepts '+str(count)+' paths before exporter')
    entries=[dict(recipe['assets'][0],model_id='pl/test/model_'+str(i)) for i in range(65)]
    check('64' in rejected(lambda:direct.export_direct({'assets':entries},destination),'65 model paths rejected'),'65 path guard')

source=runs[('BASELINE',False)]
for case in ('model','part','digest','profile'):
    bad=ARGS.output/('bad-'+case);shutil.copytree(source,bad)
    profile=bad/'nativePC'/(model_id+'.morph/profile.json')
    if case=='model':
        path=bad/'nativePC'/(model_id+'.mod3');raw=bytearray(path.read_bytes());raw[-1]^=1;path.write_bytes(raw)
    else:
        raw=json.loads(profile.read_bytes());raw[{'part':'part','digest':'asset_sha256','profile':'model_id'}[case]]={'part':0,'digest':'0'*64,'profile':'pl/forged/body'}[case];profile.write_text(json.dumps(raw),encoding='utf-8')
    rejected(lambda:prepare(bad,ARGS.output/('never-'+case),'body',None,ARGS.validator,equipment_id='002',runtime_config=ROOT/'config'),'wrong pairing refused '+case)

# The committed result and retained workspace are explicit when final cleanup fails.
real_remove=shutil.rmtree
def cleanup_fail(path,*a,**kw):
    if Path(path).name.startswith('morph-direct-stage-'):raise OSError('injected stage cleanup')
    return real_remove(path,*a,**kw)
destination=ARGS.output/'cleanup-committed'
with patch.object(direct.shutil,'rmtree',side_effect=cleanup_fail):
    message=rejected(lambda:direct.export_direct(recipe,destination),'postcommit cleanup failure reported')
check('已发布' in message and verify(ARGS.validator,destination,ROOT/'config')['native_loader_valid'],'cleanup failure explicitly preserves already-published valid result')

def backup_and_stage_cleanup_fail(path,*a,**kw):
    if Path(path).name.startswith(('morph-output-backup-','morph-direct-stage-')):raise OSError('injected nested cleanup')
    return real_remove(path,*a,**kw)
destination=ARGS.output/'cleanup-inner'
with patch.object(direct.shutil,'rmtree',side_effect=backup_and_stage_cleanup_fail):
    message=rejected(lambda:direct.export_direct(recipe,destination),'inner committed cleanup failure reported')
check('未发布最终输出' in message and not (destination/'nativePC').exists(),'inner staging commit must not be reported as final publication')

real_publish=direct.publish
def cleanup_final(stage,out):
    # Let inner transactions finish; fail cleanup only in the public transaction.
    if Path(out)==destination/'nativePC':
        with patch.object(direct.shutil,'rmtree',side_effect=backup_and_stage_cleanup_fail):return real_publish(stage,out)
    return real_publish(stage,out)
destination=ARGS.output/'cleanup-final'
with patch.object(direct,'publish',side_effect=cleanup_final),patch.object(direct.shutil,'rmtree',side_effect=cleanup_fail):
    message=rejected(lambda:direct.export_direct(recipe,destination),'final publication and stage cleanup both fail')
check('已发布' in message and verify(ARGS.validator,destination,ROOT/'config')['native_loader_valid'],'dual cleanup error still reports actual final commit')

# One minimal real 1024-key export; the four-mode matrix above stays small.
keys=[]
for i in range(1024):
    key=obj.shape_key_add(name='Capacity_'+str(i),from_mix=False);key.relative_key=obj.data.shape_keys.reference_key;key.value=0
    key.data[0].co.z=(i+1)/32768
    keys.append(dict(id='K_'+str(i),source_key=key.name,author_shading=False))
entry=dict(recipe['assets'][0],keys=keys)
destination=ARGS.output/'capacity1024';direct.export_direct({'assets':[entry]},destination)
check(verify(ARGS.validator,destination,ROOT/'config')['native_loader_valid'],'1024-key real export accepted by current native loader')
raw=(destination/'nativePC'/(model_id+'.morph/asset.mhmpack')).read_bytes()
check(struct.unpack_from('<I',raw,20)[0]==1024,'1024 keys retained in final binary')
addon.unregister();report('export',exporter_path=str(Path(exporter.__file__).resolve()))
