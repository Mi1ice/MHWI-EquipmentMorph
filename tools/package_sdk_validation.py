"""Package a verified local SDK validation app. Never deploy or launch anything."""
import argparse,datetime,hashlib,json,pathlib,re,struct,subprocess
from prepare_sdk_validation_catalog import load,require,validate

ROOT=pathlib.Path(__file__).resolve().parent.parent
APP=pathlib.Path('controls/MHWI-EquipmentMorph-SdkValidation')
DLL='MHWEquipmentMorphSdkValidation.dll'
def sha(raw):return hashlib.sha256(raw).hexdigest()
def git(*args):
    result=subprocess.run(['git','-C',str(ROOT),*args],capture_output=True)
    require(result.returncode==0,'Source commit cannot be verified');return result.stdout.decode('utf-8').strip()
def required_sources():
    paths=set()
    def add(path):
        path=path.resolve()
        if not path.is_relative_to(ROOT) or not path.is_file():return
        name=path.relative_to(ROOT).as_posix()
        if name in paths:return
        paths.add(name)
        for local in re.findall(r'^\s*#\s*include\s+"([^"]+)"',path.read_text(encoding='utf-8-sig'),re.M):add(path.parent/local)
    for p in (ROOT/APP).iterdir():
        if p.suffix in ('.cpp','.hpp'):add(p)
    for name in ('check_sdk_validation.ps1','windows-toolchain.ps1','build_mhwi_equipmentmorph_sdk_v04.ps1','prepare_sdk_validation_catalog.py','test_prepare_sdk_validation_catalog.py'):add(ROOT/'tools'/name)
    for name in ('tests.cpp','examples/usage.cpp'):add(ROOT/'sdk/MHWI-EquipmentMorph-0.4.0'/name)
    add(ROOT/'docs/SDK_VALIDATION.md')
    return sorted(paths)
def verify_report(build):
    path=build/'all-report.json';report=load(path,4*1024*1024)
    require(report.get('schema')=='mhwi-sdk-validation-build-v1' and report.get('stage')=='All' and report.get('software_checks_passed') is True,'A successful All report is required')
    require(all(report.get(k) is False for k in ('game_tested','gui_tested','visual_acceptance_complete','formal_acceptance_complete')),'Unexpected acceptance claims in software report')
    checks=report.get('checks',{})
    for name in ('case_tests','catalog_conversion','sdk_session_tests','controller_tests','window_model_tests','sdk_regression','dll_build'):
        require(name in checks and checks[name].get('passed') is True,'Missing or failed check stage')
        if name!='dll_build':require(type(checks[name].get('checks')) is int and checks[name]['checks']>0,'Missing actual check count')
    require(checks['sdk_regression'].get('capacity_groups')==4,'Existing SDK checks incomplete')
    require(checks['dll_build'].get('loaded') is False and checks['dll_build'].get('window_created') is False,'Software build must not claim GUI execution')
    commit=report.get('source_commit');require(commit==git('rev-parse','HEAD'),'Report belongs to a different source commit')
    sources=report.get('source_sha256',{});required=set(required_sources());allowed=required|{'tools/package_sdk_validation.py','tools/test_package_sdk_validation.py'}
    require(required.issubset(sources) and set(sources).issubset(allowed),'Build source manifest incomplete or unexpected')
    tracked=set(git('ls-tree','-r','--name-only',commit,'--',*sorted(sources)).splitlines());require(tracked==set(sources),'Report includes uncommitted sources')
    git('diff','--exit-code',commit,'--',*sorted(sources))
    for name,digest in sources.items():
        source=ROOT/name;require(source.resolve().is_relative_to(ROOT) and source.is_file() and sha(source.read_bytes())==digest,'Build source digest mismatch')
        require(source.stat().st_mtime_ns<=path.stat().st_mtime_ns,'Report predates source files')
    artifact=report.get('dll',{});require(artifact.get('file')==DLL,'Unexpected DLL artifact')
    binary=(build/DLL).read_bytes();require(len(binary)==artifact.get('bytes') and sha(binary)==artifact.get('sha256'),'DLL differs from successful build')
    require(len(binary)>=256 and len(binary)<=16*1024*1024 and binary[:2]==b'MZ','Invalid DLL header')
    pe=struct.unpack_from('<I',binary,0x3c)[0];require(0x40<=pe and pe+26<=len(binary),'Invalid PE offset')
    require(binary[pe:pe+4]==b'PE\0\0' and struct.unpack_from('<H',binary,pe+4)[0]==0x8664 and struct.unpack_from('<H',binary,pe+22)[0]&0x2000 and struct.unpack_from('<H',binary,pe+24)[0]==0x20b,'Expected Windows x64 DLL')
    require((build/DLL).stat().st_mtime_ns<=path.stat().st_mtime_ns,'Report predates DLL build')
    require(bool(datetime.datetime.fromisoformat(report['completed_utc'].replace('Z','+00:00')).tzinfo),'Build completion time missing')
    return report,binary,path.read_bytes()
def package(build,catalog_path,output):
    build=pathlib.Path(build).resolve();catalog_path=pathlib.Path(catalog_path).resolve();output=pathlib.Path(output).resolve()
    require(not output.exists(),'Refusing to overwrite candidate output')
    report,binary,report_raw=verify_report(build);catalog=validate(load(catalog_path,256*1024));catalog_raw=catalog_path.read_bytes()
    files={DLL:binary,'MHWEquipmentMorphSdkValidation.cases.json':catalog_raw,'README.md':(ROOT/'docs/SDK_VALIDATION.md').read_bytes(),'all-report.json':report_raw}
    manifest=dict(schema='mhwi-sdk-validation-candidate-v1',source_commit=report['source_commit'],software_checks_passed=True,
        game_tested=False,gui_tested=False,visual_acceptance_complete=False,formal_acceptance_complete=False,deployed=False,
        cases=len(catalog['cases']),case_keys=len(catalog['labels']),expected_target_keys=catalog['expected_key_count'],
        external_dependencies=dict(core='Already-loaded compatible core with public SDK and asset identity extension',model_id=catalog['model_id'],modes=catalog['modes']),
        checks=report['checks'],files={name:dict(bytes=len(raw),sha256=sha(raw)) for name,raw in files.items()})
    output.mkdir(parents=True)
    for name,raw in files.items():
        with (output/name).open('xb') as stream:stream.write(raw)
    with (output/'manifest.json').open('x',encoding='utf-8') as stream:json.dump(manifest,stream,ensure_ascii=False,indent=2);stream.write('\n')
    return manifest
def main():
    p=argparse.ArgumentParser();p.add_argument('--build-dir',type=pathlib.Path,required=True);p.add_argument('--catalog',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
    m=package(a.build_dir,a.catalog,a.output);print(json.dumps(dict(source_commit=m['source_commit'],files=len(m['files'])+1,cases=m['cases'],case_keys=m['case_keys'],expected_target_keys=m['expected_target_keys'],deployed=False)))
if __name__=='__main__':main()
