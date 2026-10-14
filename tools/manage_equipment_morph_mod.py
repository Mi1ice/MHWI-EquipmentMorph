"""Plan or transactionally install paired MOD3/sidecars; never install runtime DLLs."""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid
from prepare_mhwi_equipmentmorph_resource_path import backend,verify

def sha(data):return hashlib.sha256(data).hexdigest()

def target(game,relative):
    relative=Path(relative)
    if relative.is_absolute() or '..' in relative.parts:raise ValueError('Target escapes game directory')
    value=game/relative
    for path in [value,*value.parents]:
        if path==game.parent:break
        if path.is_symlink() or (hasattr(path,'is_junction') and path.is_junction()):raise ValueError('Linked game destination: '+str(path))
        if path.exists() and (path==value and not path.is_file() or path!=value and not path.is_dir()):raise ValueError('Destination file/directory collision: '+str(path))
    return value

def plan(source,game,validator):
    source=Path(source).absolute();game=Path(game).absolute()
    if not (game/'MonsterHunterWorld.exe').is_file():raise ValueError('Chosen directory has no MHW executable')
    config=target(game,'nativePC/plugins/MHWEquipmentMorph/runtime.ini')
    if not config.is_file():raise ValueError('请先安装游戏端形态键运行时；MOD不再提供公共runtime.ini')
    report=verify(validator,source,config.parent);_,encode,_=backend();files=[]
    for entry in report['parts']:
        model=entry['model_id'];folder=source/'nativePC'/(model+'.morph')
        asset=(folder/'asset.mhmpack').read_bytes();profile=(folder/'model.mhmprofile').read_bytes()
        description=json.loads((folder/'profile.json').read_bytes())
        if (description['model_id']!=model or description['part']!=entry['part']
            or description['model_sha256']!=entry['model_sha256'] or encode(description,asset)!=profile):
            raise ValueError('Readable profile differs from native model/package: '+model)
        for relative in ['nativePC/'+model+'.mod3',*['nativePC/'+model+'.morph/'+name for name in ('asset.mhmpack','model.mhmprofile','profile.json')]]:
            src=source/relative;dest=target(game,relative);digest=sha(src.read_bytes())
            old=sha(dest.read_bytes()) if dest.exists() else None
            files.append(dict(relative=relative,sha256=digest,previous_sha256=old,
                              status='identical' if old==digest else 'conflict' if old else 'new'))
    return dict(source=str(source),game=str(game),native_check=report,files=files,
                runtime_config={'path':str(config),'sha256':sha(config.read_bytes()),'owned_by_mod':False},
                conflicts=[entry['relative'] for entry in files if entry['status']=='conflict'],files_changed=False)

def closed(game):
    command="foreach($taskProcess in @(Get-CimInstance Win32_Process -Filter \"Name='MonsterHunterWorld.exe'\")){if(-not $taskProcess.ExecutablePath -or [IO.Path]::GetFullPath($taskProcess.ExecutablePath).Equals($env:MHWI_MORPH_GAME_EXE,[StringComparison]::OrdinalIgnoreCase)){exit 1}}"
    process=subprocess.run(['powershell','-NoProfile','-NonInteractive','-Command',command],
        env=dict(os.environ,MHWI_MORPH_GAME_EXE=str(game/'MonsterHunterWorld.exe')),capture_output=True)
    if process.returncode:raise ValueError('Normally close the selected game before installation')

def install(source,game,validator,replace=False):
    report=plan(source,game,validator);source=Path(report['source']);game=Path(report['game'])
    if report['conflicts'] and not replace:raise ValueError('Existing MOD/configuration differs; inspect plan and explicitly use --replace')
    closed(game)
    state=game/'nativePC/plugins/MHWEquipmentMorph/.mod-installation'
    target(game,'nativePC/plugins/MHWEquipmentMorph/.mod-installation/lock')
    state.mkdir(parents=True,exist_ok=True)
    lock=state/'lock';handle=os.open(lock,os.O_CREAT|os.O_EXCL|os.O_WRONLY)
    receipt=state/uuid.uuid4().hex
    changed=[entry for entry in report['files'] if entry['status']!='identical'];applied=[];directories=set()
    try:
        receipt.mkdir()
        with tempfile.TemporaryDirectory(prefix='paired-install-',dir=state) as temporary:
            stage=Path(temporary)
            for entry in changed:
                relative=entry['relative'];dest=target(game,relative)
                if (sha(dest.read_bytes()) if dest.exists() else None)!=entry['previous_sha256']:raise ValueError('Target changed after preflight: '+relative)
                data=(source/relative).read_bytes()
                if sha(data)!=entry['sha256']:raise ValueError('Source changed after preflight: '+relative)
                if dest.exists():
                    backup=receipt/'previous'/relative;backup.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(dest,backup)
                staged=stage/relative;staged.parent.mkdir(parents=True,exist_ok=True);staged.write_bytes(data)
                for parent in dest.parents:
                    if parent==game or parent.exists():break
                    directories.add(parent)
            publish=importlib.import_module('_mhm_prepare.output_files').publish
            runtime=report['runtime_config']
            if sha(Path(runtime['path']).read_bytes())!=runtime['sha256']:raise ValueError('Runtime configuration changed during installation')
            publish(stage,game);applied=changed
            checked=verify(validator,game,Path(runtime['path']).parent)
            if sha(Path(runtime['path']).read_bytes())!=runtime['sha256']:raise ValueError('Runtime configuration changed during installation')
            report.update(installed=True,files_changed=bool(changed),installed_check=checked)
            (receipt/'receipt.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            report['receipt']=str(receipt/'receipt.json');return report
    except Exception:
        for entry in reversed(applied):
            dest=target(game,entry['relative'])
            if sha(dest.read_bytes())!=entry['sha256']:raise RuntimeError('Concurrent change prevents rollback; previous files retained at '+str(receipt))
            backup=receipt/'previous'/entry['relative']
            if backup.exists():shutil.copyfile(backup,dest)
            else:dest.unlink()
        for directory in sorted(directories,key=lambda path:len(path.parts),reverse=True):
            try:directory.rmdir()
            except OSError:pass
        raise
    finally:os.close(handle);lock.unlink()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True);parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--install',action='store_true');parser.add_argument('--replace',action='store_true')
    local=Path(__file__).with_name('EquipmentMorphPackageValidator.exe')
    parser.add_argument('--validator',type=Path,default=local if local.exists() else Path(__file__).resolve().parents[1]/'.build/current-validation/package-validator/EquipmentMorphPackageValidator.exe')
    args=parser.parse_args()
    result=install(args.source,args.game,args.validator,args.replace) if args.install else plan(args.source,args.game,args.validator)
    print(json.dumps(result,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
