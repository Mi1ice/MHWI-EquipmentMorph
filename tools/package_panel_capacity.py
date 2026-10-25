"""Package a verified panel candidate, without installation or user configuration."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT=Path(__file__).resolve().parent.parent
DLL='MHWEquipmentMorphImGui.dll'
BUILD_FILES={DLL,'EquipmentMorphImGuiFixture.exe','ImGuiInputTests.exe','ImGuiControlTests.exe','ImGuiPresetTests.exe','README.md','LICENSE.ImGui.txt','LICENSE.SafetyHook','LICENSE.Zydis'}
def require(ok,message):
    if not ok:raise ValueError(message)
def sha(raw):return hashlib.sha256(raw).hexdigest()
def git(*args):
    result=subprocess.run(['git','-C',str(ROOT),*args],capture_output=True)
    require(result.returncode==0,'Source repository cannot be verified')
    return result.stdout.decode('utf-8').strip()
def load(path):
    raw=path.read_bytes();require(len(raw)<=4*1024*1024,'Software report too large')
    return json.loads(raw.decode('utf-8-sig'))
def portable_document(source):
    names={(ROOT/'controls/MHWI-EquipmentMorph-ImGui/README.md').resolve():'README.md',(ROOT/'docs/PRESETS.md').resolve():'PRESETS.md',(ROOT/'docs/IMGUI_INSTALLATION.md').resolve():'IMGUI_INSTALLATION.md'}
    def link(match):
        label,target=match.groups()
        if '://' in target or target.startswith('#'):return match.group(0)
        path,separator,anchor=target.partition('#');name=names.get((source.parent/path).resolve())
        return '['+label+']('+name+(separator+anchor if separator else '')+')' if name else label
    return re.sub(r'\[([^\]]+)\]\(([^)]+)\)',link,source.read_text(encoding='utf-8-sig')).encode('utf-8')
def verify(build):
    report_path=build/'all-report.json';report=load(report_path)
    require(report.get('schema')=='mhwi-panel-capacity-v1' and report.get('stage')=='All' and report.get('software_checks_passed') is True,'Successful All report required')
    require(all(report.get(k) is False for k in ('gui_tested','game_tested','formal_acceptance_complete')),'Invalid software acceptance claim')
    checks=report.get('checks',{})
    for name in ('Session','Control','Preset','Storage','Regression'):
        entry=checks.get(name,{});require(entry.get('passed') is True and type(entry.get('checks')) is int and entry['checks']>0,'Missing capacity stage result')
    integration=checks.get('integration',{})
    require(integration.get('passed') is True and integration.get('fixture_built') is True and integration.get('fixture_run') is False,'Missing no-window integration build')
    require(all(type(integration.get(k)) is int and integration[k]>0 for k in ('input_checks','control_checks','preset_checks')),'Missing integration assertion counts')
    sdk_path=build/'sdk-validation-regression/all-report.json';sdk=checks.get('sdk_regression',{})
    require(sdk.get('passed') is True and sha(sdk_path.read_bytes())==sdk.get('report_sha256'),'SDK regression report mismatch')
    sdk_report=load(sdk_path);require(sdk_report.get('software_checks_passed') is True and sdk_report.get('gui_tested') is False and sdk_report.get('game_tested') is False,'Invalid SDK software result')
    commit=report.get('source_commit');require(commit==git('rev-parse','HEAD'),'Stale source commit')
    tracked=set(git('-c','core.quotepath=false','ls-tree','-r','--name-only',commit).splitlines());sources=report.get('source_sha256',{})
    require(set(sources)==tracked,'Incomplete or unexpected source inventory');git('diff','--exit-code',commit,'--')
    for name,digest in sources.items():
        path=ROOT/name;require(path.resolve().is_relative_to(ROOT) and path.is_file() and sha(path.read_bytes())==digest,'Source digest mismatch')
        require(path.stat().st_mtime_ns<=report_path.stat().st_mtime_ns,'Report predates source')
    require(bool(datetime.datetime.fromisoformat(report['completed_utc'].replace('Z','+00:00')).tzinfo),'Missing completion time')
    require(isinstance(report.get('core_sha256'),str) and len(report['core_sha256'])==64 and all(c in '0123456789abcdef' for c in report['core_sha256']),'Missing external core digest')
    folder=build/'imgui';manifest_path=folder/'manifest.json'
    require(sha(manifest_path.read_bytes())==report.get('imgui_manifest_sha256'),'Build manifest mismatch');manifest=load(manifest_path)
    require(manifest.get('panel_revision')=='dynamic-panel-v2' and manifest.get('snapshot')=='process-local-dynamic' and manifest.get('preset_schema')==2 and manifest.get('preset_root')=='MHWEquipmentMorph/panel-v2','Incorrect panel build identity')
    require(manifest.get('gui_tested') is False and manifest.get('game_tested') is False and manifest.get('morph_api_connected') is False,'Build must not claim live connection')
    entries=manifest.get('files',[]);require(len(entries)==len(BUILD_FILES) and {e['path'] for e in entries}==BUILD_FILES,'Unexpected build payload')
    for path in folder.iterdir():
        # The no-window preset suite creates isolated synthetic cases here.
        # It is never a candidate payload and is deliberately not traversed.
        if path.name=='preset-fixtures' and path.is_dir() and not path.is_symlink():continue
        require(path.is_file() and (path.name in BUILD_FILES|{'manifest.json','EquipmentLabels.generated.hpp'} or path.suffix in {'.obj','.lib','.exp','.rsp'}),'Unexpected core or user data in panel build')
    for entry in entries:require(sha((folder/entry['path']).read_bytes())==entry['sha256'],'Build payload digest mismatch')
    artifact=report.get('dll',{});require(artifact.get('file')=='imgui/'+DLL,'Unexpected DLL path');binary=(folder/DLL).read_bytes()
    require(len(binary)==artifact.get('bytes') and sha(binary)==artifact.get('sha256'),'DLL differs from verified report')
    require(256<=len(binary)<=16*1024*1024 and binary[:2]==b'MZ','Invalid DLL')
    offset=struct.unpack_from('<I',binary,0x3c)[0];require(0x40<=offset and offset+26<=len(binary),'Invalid PE offset')
    require(binary[offset:offset+4]==b'PE\0\0' and struct.unpack_from('<H',binary,offset+4)[0]==0x8664 and struct.unpack_from('<H',binary,offset+22)[0]&0x2000 and struct.unpack_from('<H',binary,offset+24)[0]==0x20b,'Expected x64 DLL')
    require((folder/DLL).stat().st_mtime_ns<=report_path.stat().st_mtime_ns and manifest_path.stat().st_mtime_ns<=report_path.stat().st_mtime_ns,'Report predates build')
    return report
def package(build,output):
    build=Path(build).resolve();output=Path(output).resolve();require(not output.exists(),'Refusing to overwrite existing output')
    report=verify(build);folder=build/'imgui'
    files={DLL:(folder/DLL).read_bytes(),'LICENSE':(ROOT/'LICENSE').read_bytes(),'README.md':portable_document(ROOT/'controls/MHWI-EquipmentMorph-ImGui/README.md'),'PRESETS.md':portable_document(ROOT/'docs/PRESETS.md'),'IMGUI_INSTALLATION.md':portable_document(ROOT/'docs/IMGUI_INSTALLATION.md'),'all-report.json':(build/'all-report.json').read_bytes(),'imgui-build-manifest.json':(folder/'manifest.json').read_bytes(),'sdk-validation-report.json':(build/'sdk-validation-regression/all-report.json').read_bytes()}
    for name in ('LICENSE.ImGui.txt','LICENSE.SafetyHook','LICENSE.Zydis'):files[name]=(folder/name).read_bytes()
    manifest=dict(schema='mhwi-panel-capacity-candidate-v1',source_commit=report['source_commit'],panel_revision='dynamic-panel-v2',preset_schema=2,preset_root='MHWEquipmentMorph/panel-v2',experimental_limits=dict(keys_per_target=1024,targets=5,batch_updates=5120,file_bytes=1048576),software_checks_passed=True,gui_tested=False,game_tested=False,formal_acceptance_complete=False,deployed=False,external_dependencies=dict(core_sha256=report['core_sha256'],assets='Existing paired model and pack; no asset payload included',native_blink='Independent optional plugin, not upgraded'),checks=report['checks'],files={name:dict(bytes=len(raw),sha256=sha(raw)) for name,raw in files.items()})
    output.mkdir(parents=True)
    for name,raw in files.items():
        with (output/name).open('xb') as stream:stream.write(raw)
    with (output/'manifest.json').open('x',encoding='utf-8') as stream:json.dump(manifest,stream,ensure_ascii=False,indent=2);stream.write('\n')
    return manifest
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build-dir',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    result=package(args.build_dir,args.output);print(json.dumps(dict(source_commit=result['source_commit'],files=len(result['files'])+1,deployed=False)))
if __name__=='__main__':main()
