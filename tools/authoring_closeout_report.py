"""Run isolated authoring checks and bind results to exact current inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def write(path,value):
    path=Path(path);temporary=path.with_suffix('.tmp');temporary.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');temporary.replace(path)
def inputs():
    files=list((ROOT/'blender_addon_v044/mhwi_equipmentmorph').rglob('*'))
    files += list((ROOT/'tools').glob('*authoring*'))
    files += [ROOT/'tools/package_source.py',ROOT/'tools/desktop_notices.py',ROOT/'tools/blender_export_fixture.py',ROOT/'LICENSE',ROOT/'LICENSING.md',ROOT/'docs/AUTHOR_TOOLS.md',ROOT/'docs/USAGE.md']
    return {p.relative_to(ROOT).as_posix():sha(p) for p in sorted(set(files)) if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc'}
def run(command,log):
    with Path(log).open('w',encoding='utf-8') as stream:
        result=subprocess.run([str(x) for x in command],cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT)
    print(Path(log).read_text(encoding='utf-8',errors='replace')[-2500:])
    if result.returncode: raise RuntimeError(f'Command failed ({result.returncode}); {log}')
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--stage',choices=['Entrypoints','Preflight','Export','All'],default='All');parser.add_argument('--blender',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    report=dict(schema='mhwi-authoring-closeout-v1',stage=args.stage,software_checks_passed=False,checks=[],source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),dirty=bool(subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT,text=True).strip()),inputs=inputs(),blender=dict(path=str(args.blender.resolve()),sha256=sha(args.blender)),python=dict(path=sys.executable,sha256=sha(sys.executable)),background_blender_tested=False,gui_tested=False,game_tested=False,formal_acceptance_complete=False,reused_reports=[])
    write(output/'report.json',report) # Invalidate any previous All before work starts.
    try:
        run([args.blender,'--version'],output/'blender-version.log')
        version=(output/'blender-version.log').read_text(encoding='utf-8')
        if not version.startswith('Blender 5.1.2'): raise ValueError('Only certified Blender 5.1.2 is supported')
        report['blender']['version_output']=version
        stages=['Entrypoints','Preflight','Export'] if args.stage=='All' else [args.stage]
        if 'Export' in stages:
            from package_source import addon_files
            addon_files(ROOT/'blender_addon_v044/mhwi_equipmentmorph')
            run(['powershell','-NoProfile','-File',ROOT/'tools/build_equipment_morph_package_validator.ps1','-OutputDirectory',output/'validator'],output/'validator-build.log')
            report['validator']=dict(path=str(output/'validator/EquipmentMorphPackageValidator.exe'),sha256=sha(output/'validator/EquipmentMorphPackageValidator.exe'))
        for stage in stages:
            script={'Entrypoints':'test_authoring_entrypoints_blender.py','Preflight':'test_authoring_preflight_blender.py','Export':'test_authoring_export_blender.py'}[stage]
            cmd=[args.blender,'--background','--factory-startup','--python-exit-code','1','--python',ROOT/'tools'/script,'--','--output',output/stage.lower()]
            if 'validator' in report: cmd += ['--validator',report['validator']['path']]
            run(cmd,output/(stage.lower()+'.log'))
            result=json.loads((output/stage.lower()/(stage.lower()+'.json')).read_bytes());report['checks']+=result['checks']
        report['background_blender_tested']=True;report['software_checks_passed']=True
        report['outputs']={p.relative_to(output).as_posix():sha(p) for p in output.rglob('*') if p.is_file() and p.name not in ('report.json','report.tmp')}
    except Exception as error:
        report['error']=str(error);write(output/'report.json',report);raise
    write(output/'report.json',report);print('PASS:',len(report['checks']),'authoring checks')
if __name__=='__main__': main()
