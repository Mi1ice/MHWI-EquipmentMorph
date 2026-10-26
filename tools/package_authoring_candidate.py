"""Standalone author ZIP; requires a matching complete All and external ZIP self-test."""
import argparse
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import tempfile
import zipfile
from authoring_closeout_report import ROOT,inputs,sha,write,run
from package_source import addon_files

def validate_report(build):
    build=Path(build).resolve();report=json.loads((build/'report.json').read_bytes())
    if (report.get('schema')!='mhwi-authoring-closeout-v1' or report.get('stage')!='All'
        or report.get('software_checks_passed') is not True or report.get('background_blender_tested') is not True
        or report.get('dirty') is not False or not report.get('python_suites')):
        raise ValueError('Complete clean-source All authoring report required')
    current=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    if report.get('source_commit')!=current or report.get('inputs')!=inputs():raise ValueError('All source inputs are stale or incomplete')
    blender=report.get('blender',{})
    if not blender.get('version_output','').startswith('Blender 5.1.2'):raise ValueError('Certified Blender 5.1.2 report required')
    for name in ('blender','validator'):
        entry=report.get(name,{})
        if not entry.get('path') or sha(entry['path'])!=entry.get('sha256'):raise ValueError('Changed or missing '+name)
    addon=ROOT/'blender_addon_v044/mhwi_equipmentmorph';addon_files(addon)
    native=json.loads((addon/'_native/build/build.json').read_bytes())
    if any(report.get('native_component',{}).get(k)!=v for k,v in native.items()):raise ValueError('Changed native author component')
    for name,expected in native['source_sha256'].items():
        if sha(ROOT/name)!=expected:raise ValueError('Changed native component source')
    sources=report.get('exporter',{}).get('sources',{})
    if len(sources)!=8 or any(sha(p)!=digest for p,digest in sources.items()):raise ValueError('Changed pinned exporter')
    outputs=report.get('outputs',{})
    for relative,expected in outputs.items():
        path=build/relative
        if not path.resolve().is_relative_to(build) or sha(path)!=expected:raise ValueError('Changed check output '+relative)
    checks=[]
    for relative in ('entrypoints/entrypoints.json','preflight/preflight.json','key-preview.json','export/export.json'):
        if relative not in outputs:raise ValueError('Missing stage report '+relative)
        result=json.loads((build/relative).read_bytes())
        if not result.get('checks') or result.get('passed')!=len(result['checks']):raise ValueError('Incomplete stage '+relative)
        checks+=result['checks']
    if checks!=report.get('checks'):raise ValueError('Check accounting differs')
    return report

def zip_sources():
    addon=ROOT/'blender_addon_v044/mhwi_equipmentmorph'
    selected=addon_files(addon)
    tracked=set(subprocess.check_output(['git','ls-files','--','blender_addon_v044/mhwi_equipmentmorph'],cwd=ROOT,text=True).splitlines())
    native=json.loads((addon/'_native/build/build.json').read_bytes())
    generated={'_native/build/'+name for name in (native['file'],'build.json','NOTICE.txt','LICENSE.Apache-2.0.txt')}
    for path in selected:
        relative=path.relative_to(addon).as_posix()
        if path.is_symlink() or (hasattr(path,'is_junction') and path.is_junction()):raise ValueError('Linked addon input')
        if path.relative_to(ROOT).as_posix() not in tracked and relative not in generated:raise ValueError('Untracked addon payload '+relative)
        if path.suffix.lower() in ('.blend','.exe','.ini','.mod3','.mhmpack','.mhmprofile') or (path.suffix.lower()=='.dll' and relative not in generated):raise ValueError('Unexpected addon payload '+relative)
    result={'mhwi_equipmentmorph/'+p.relative_to(addon).as_posix():p for p in selected}
    result.update({'mhwi_equipmentmorph/'+name:ROOT/name for name in ('LICENSE','LICENSING.md')})
    return result

def validate_zip(path,expected):
    with zipfile.ZipFile(path) as archive:
        names=archive.namelist()
        if len(names)!=len(set(names)) or set(names)!=set(expected):raise ValueError('Unexpected or missing ZIP members')
        for entry in archive.infolist():
            name=entry.filename;pure=PurePosixPath(name)
            if pure.is_absolute() or '..' in pure.parts or '\\' in name or ':' in name or stat.S_ISLNK(entry.external_attr>>16):raise ValueError('Unsafe ZIP member')
            import hashlib
            if hashlib.sha256(archive.read(name)).hexdigest()!=expected[name]:raise ValueError('Changed ZIP member '+name)

def assemble(build,output):
    output=Path(output).absolute()
    if output.exists():raise ValueError('Candidate output already exists; use a new directory')
    report=validate_report(build);sources=zip_sources()
    output.mkdir(parents=True)
    archive=output/'addon.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as target:
        for name,path in sorted(sources.items()):target.write(path,name)
    expected={name:sha(path) for name,path in sources.items()};validate_zip(archive,expected)
    for name in ('LICENSE','LICENSING.md'):shutil.copyfile(ROOT/name,output/name)
    shutil.copyfile(ROOT/'docs/AUTHOR_TOOLS.md',output/'README.md')
    shutil.copyfile(Path(build)/'report.json',output/'software-report.json')
    manifest=dict(schema='mhwi-authoring-candidate-v1',candidate_complete=False,source_commit=report['source_commit'],zip_sha256=sha(archive),zip_files=expected,software_report_sha256=sha(output/'software-report.json'),formal_acceptance_complete=False,gui_tested=False,game_tested=False,includes_game_components=False)
    write(output/'manifest.json',manifest);return manifest

def finalize(output,receipt):
    output=Path(output);manifest=json.loads((output/'manifest.json').read_bytes());report=json.loads((output/'software-report.json').read_bytes())
    validate_zip(output/'addon.zip',manifest['zip_files'])
    if sha(output/'software-report.json')!=manifest['software_report_sha256']:raise ValueError('Changed candidate software report')
    if (receipt.get('zip_sha256')!=sha(output/'addon.zip') or receipt.get('zip_sha256')!=manifest['zip_sha256']
        or receipt.get('validator_sha256')!=report['validator']['sha256'] or receipt.get('blender_sha256')!=report['blender']['sha256']
        or not receipt.get('passed') or receipt.get('passed')!=len(receipt.get('checks',[]))
        or receipt.get('background_blender_tested') is not True or receipt.get('addon_from_zip') is not True):raise ValueError('Missing or mismatched candidate ZIP self-test')
    write(output/'self-test-receipt.json',receipt)
    manifest.update(candidate_complete=True,self_test_receipt_sha256=sha(output/'self-test-receipt.json'))
    write(output/'manifest.json',manifest)
    (output/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in sorted(output.iterdir()) if p.is_file() and p.name!='SHA256SUMS'),encoding='utf-8')
    return manifest

def self_test(build,output):
    build=Path(build).resolve();output=Path(output).resolve();report=json.loads((output/'software-report.json').read_bytes())
    # Blender's embedded Windows Python cannot open the DLL at a >260-char
    # nested evidence path. Keep this disposable unpacking under a short root.
    work=Path(tempfile.mkdtemp(prefix='az-',dir=ROOT/'.build'));unpacked=work/'unpacked';unpacked.mkdir()
    manifest=json.loads((output/'manifest.json').read_bytes());validate_zip(output/'addon.zip',manifest['zip_files'])
    with zipfile.ZipFile(output/'addon.zip') as archive:
        for name in archive.namelist():
            path=unpacked/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(archive.read(name))
    run([report['blender']['path'],'--background','--factory-startup','--python-exit-code','1','--python',ROOT/'tools/test_authoring_candidate_blender.py','--','--output',work/'result','--validator',report['validator']['path'],'--addon-root',unpacked],work/'blender.log')
    receipt=json.loads((work/'result/candidate-selftest.json').read_bytes())
    receipt.update(zip_sha256=sha(output/'addon.zip'),validator_sha256=sha(report['validator']['path']),blender_sha256=sha(report['blender']['path']),work_directory=str(work),log_sha256=sha(work/'blender.log'),check_script_sha256=sha(ROOT/'tools/test_authoring_candidate_blender.py'))
    return receipt

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build-dir',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    assemble(args.build_dir,args.output);receipt=self_test(args.build_dir,args.output);validate_report(args.build_dir);finalize(args.output,receipt)
    print('PASS: independent author candidate and external ZIP self-test:',args.output)
if __name__=='__main__':main()
