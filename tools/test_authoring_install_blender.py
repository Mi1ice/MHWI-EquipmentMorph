"""Actual legacy ZIP install/enable in isolated factory-startup user resources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import traceback
import bpy
import addon_utils

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--workspace',type=Path,required=True)
parser.add_argument('--zip',type=Path,required=True)
parser.add_argument('--validator',type=Path,required=True)
parser.add_argument('--exporter-addons',type=Path,required=True)
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
work=args.workspace.resolve();checks=[]
def check(value,label):
    assert value,label
    checks.append(label)
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    scripts=Path(bpy.utils.user_resource('SCRIPTS')).resolve()
    configuration=Path(bpy.utils.user_resource('CONFIG')).resolve()
    check(scripts==work/'scripts' and configuration==work/'config','installer resources isolated from user scripts and preferences')
    check('mhwi_equipmentmorph' not in sys.modules,'no source import before actual installation')
    check(not (scripts/'addons/mhwi_equipmentmorph').exists(),'fresh isolated addon destination')
    args.zip=args.zip.resolve()
    check(bpy.ops.preferences.addon_install(filepath=str(args.zip),enable_on_install=True,overwrite=False)=={'FINISHED'},'real Blender ZIP installation and automatic restricted enable')
    import mhwi_equipmentmorph as addon
    from mhwi_equipmentmorph import shape_preview as preview
    startup_recovery=getattr(preview,'recover_after_register',preview.recover)
    installed=scripts/'addons/mhwi_equipmentmorph'
    check(Path(addon.__file__).resolve()==installed/'__init__.py','enabled module loaded from ZIP installation')
    check(addon_utils.check('mhwi_equipmentmorph')==(True,True),'Blender records the installed addon as enabled')
    check(bpy.app.timers.is_registered(startup_recovery),'initial recovery is deferred until data access is available')
    # Run the registered callback in restored context; no GUI loop or user data.
    startup_recovery()
    handlers=lambda:{name:len(getattr(bpy.app.handlers,name)) for name in ('save_pre','load_pre','undo_pre','redo_pre','render_pre','load_post','undo_post','redo_post')}
    expected=handlers()
    check(all(getattr(bpy.app.handlers,name).count(preview.before_operation)==1 for name in ('save_pre','load_pre','undo_pre','redo_pre','render_pre')),'pre-operation cleanup handlers registered once')
    addon_utils.disable('mhwi_equipmentmorph',default_set=True)
    check(not hasattr(bpy.types.WindowManager,'mhwi_equipmentmorph') and not bpy.app.timers.is_registered(startup_recovery),'disable removes RNA property and pending recovery timer')
    errors=[]
    check(addon_utils.enable('mhwi_equipmentmorph',default_set=True,handle_error=errors.append) is addon and not errors,'actual restricted re-enable succeeds')
    check(handlers()==expected,'disable and re-enable do not duplicate handlers')
    sys.path[:0]=[str(args.exporter_addons.resolve()),str(ROOT/'tools')]
    from blender_export_fixture import fixture,exporter
    from prepare_mhwi_equipmentmorph_resource_path import verify
    from mhwi_equipmentmorph.backend.capture_equipment_morph_job import fingerprint
    collection,obj=fixture();state=addon.settings(bpy.context)
    asset=state.assets.add();asset.collection=collection;asset.target_mode='MANUAL';asset.model_id='pl/f_equip/pl105_0000/body/mod/f_body105_0000'
    addon.refresh_keys(asset);next(key for key in asset.keys if key.source=='Smile').selected=True
    check(bpy.ops.mhwi_em.confirm_keys()=={'FINISHED'},'installed addon confirms real shape key')
    preview.start(asset,bpy.context,True)
    preview_objects={o.name for o in bpy.data.objects}
    startup_recovery()
    check(preview.running() and {o.name for o in bpy.data.objects}==preview_objects,'deferred startup recovery does not delete a newly active preview')
    preview.stop()
    before={o.name:fingerprint(o) for o in collection.all_objects if o.type=='MESH'}
    state.output=str(work/'export')
    check(bpy.ops.mhwi_em.export_package()=={'FINISHED'},'installed addon performs real paired export')
    check(verify(args.validator,Path(state.output),ROOT/'config')['native_loader_valid'],'installed ZIP output accepted by current native loader')
    check(len([p for p in Path(state.output).rglob('*') if p.is_file()])==4,'installed ZIP ordinary export contains exactly four files')
    check(before=={o.name:fingerprint(o) for o in collection.all_objects if o.type=='MESH'},'installed addon preserves synthetic source data')
    # Exercise the installed public operator, including its actual error report.
    count=125-len(str(work).encode('utf-16-le'))//2-1
    deep=work/('中'*count);deep.mkdir();(deep/'keep').write_bytes(b'old output')
    state.output=str(deep)
    # Capture RNA without importing source addon code or another test parser.
    configuration_snapshot=lambda:(state.output,state.debug,asset.model_id,[(k.source,k.public_id,k.enabled,k.selected) for k in asset.keys])
    configuration_before=configuration_snapshot()
    try:bpy.ops.mhwi_em.export_package()
    except RuntimeError as error:message=str(error)
    else:raise AssertionError('installed ZIP did not reject deep output')
    check('导出路径过长' in message and '缩短输出目录' in message and str(deep) in message,'installed ZIP reports actionable deep output error')
    check(configuration_before==configuration_snapshot(),'installed ZIP path rejection preserves author configuration')
    check(before=={o.name:fingerprint(o) for o in collection.all_objects if o.type=='MESH'} and (deep/'keep').read_bytes()==b'old output' and list(deep.iterdir())==[deep/'keep'],'installed ZIP path rejection preserves source and old output without staging')
    addon_utils.disable('mhwi_equipmentmorph',default_set=True)
    check(not bpy.app.timers.is_registered(startup_recovery) and not bpy.app.timers.is_registered(preview.watch),'final disable removes pending timers')
    check(not (configuration/'userpref.blend').exists(),'no preferences saved')
    return dict(addon_path=str(installed/'__init__.py'),zip_sha256=sha(args.zip),installed_files={p.relative_to(installed).as_posix():sha(p) for p in installed.rglob('*') if p.is_file() and '__pycache__' not in p.parts})

result=dict(installation_tested=False,enable_via_addon_utils=False,addon_from_zip=False,background_blender_tested=True,gui_tested=False,game_tested=False,blender=list(bpy.app.version),blend_saved=False,user_configuration_modified=False)
try:
    result.update(main(),installation_tested=True,enable_via_addon_utils=True,addon_from_zip=True,passed=len(checks),checks=checks)
except Exception:
    result.update(passed=0,completed_checks=checks,error=traceback.format_exc())
    (work/'candidate-install.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    raise
(work/'candidate-install.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('PASS:',len(checks),'actual isolated ZIP install/enable/export checks')
