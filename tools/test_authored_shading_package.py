"""Validate the author-only native component and isolated addon archive."""
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import zipfile
import package_source

ROOT=Path(__file__).resolve().parents[1]
ADDON=ROOT/'blender_addon_v044/mhwi_equipmentmorph'
parent=ROOT/'.build/capacity-validation'
parent.mkdir(parents=True,exist_ok=True)
folder=Path(tempfile.mkdtemp(prefix='author-package-',dir=parent))
checks=0
def check(value,label):
    global checks
    assert value,label
    checks+=1
def refused(action,label):
    try:action()
    except ValueError:check(True,label)
    else:check(False,label)

files=package_source.addon_files(ADDON)
native=ADDON/'_native/build';manifest=json.loads((native/'build.json').read_bytes())
expected={native/name for name in [manifest['file'],'build.json','NOTICE.txt','LICENSE.Apache-2.0.txt']}
check(expected.issubset(set(files)),'component manifest binary and licenses included')
archive=folder/'author-candidate.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for path in files:z.write(path,'mhwi_equipmentmorph/'+path.relative_to(ADDON).as_posix())
    for name in ['LICENSE','LICENSING.md']:z.write(ROOT/name,'mhwi_equipmentmorph/'+name)
with zipfile.ZipFile(archive) as z:
    check(z.testzip() is None,'isolated author archive CRC')
    check(z.read('mhwi_equipmentmorph/_native/build/'+manifest['file'])==(native/manifest['file']).read_bytes(),'packaged helper byte exact')
    check('mhwi_equipmentmorph/LICENSE' in z.namelist() and 'mhwi_equipmentmorph/_native/build/LICENSE.Apache-2.0.txt' in z.namelist(),'project and dependency licenses retained')
    check(not any('__pycache__' in name or name.endswith('.pyc') for name in z.namelist()),'no interpreter cache in addon')
fixture=folder/'fixture';target=fixture/'_native/build';shutil.copytree(native,target)
(fixture/'__init__.py').write_text('# synthetic package fixture\n',encoding='utf-8')
stale=target/('mhm_author_shading-'+'f'*64+'.dll');stale.write_bytes(b'old generated helper')
check(stale not in package_source.addon_files(fixture),'stale generated helper excluded')
binary=target/manifest['file'];original=binary.read_bytes();binary.unlink()
refused(lambda:package_source.addon_files(fixture),'missing helper rejects packaging')
binary.write_bytes(original+b'corrupt')
refused(lambda:package_source.addon_files(fixture),'corrupt helper rejects packaging')
binary.write_bytes(original);(target/'NOTICE.txt').unlink()
refused(lambda:package_source.addon_files(fixture),'missing attribution rejects packaging')
report=dict(passed=checks,archive=str(archive),sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),published=False,installed=False)
(ROOT/'.build/capacity-validation/authored-package-checks.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('PASS: '+str(checks)+' author native packaging checks; isolated candidate only')
