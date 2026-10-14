"""Package local build outputs; never install them or claim game acceptance."""
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CONFIG = json.loads((ROOT / 'config/mhwi-equipmentmorph-current.json').read_bytes())
VERSION = CONFIG['runtime_version']
PACKAGE = ROOT / CONFIG['release_directory']

def copy(source, relative):
    destination = PACKAGE / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    return destination

def record(path):
    data = path.read_bytes()
    return {'path': path.relative_to(PACKAGE).as_posix(), 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest()}

def main():
    # All outputs are under the ignored .build folder and must be rebuilt locally.
    if not PACKAGE.resolve().is_relative_to((ROOT / '.build').resolve()):
        raise ValueError('Package destination must stay inside .build')
    PACKAGE.mkdir(parents=True, exist_ok=True)
    payload = []
    for source, relative in [
        ('.build/cleanup-validation/native/MHWEquipmentMorphConfiguredPrototype.dll', 'nativePC/plugins/MHWEquipmentMorphConfiguredPrototype.dll'),
        ('.build/current-validation/control-panel/MHWEquipmentMorphControlBridge.dll', 'nativePC/plugins/MHWEquipmentMorphControlBridge.dll'),
        ('.build/current-validation/control-panel/MHWI-EquipmentMorph-ControlPanel.exe', 'MHWI-EquipmentMorph/MHWI-EquipmentMorph-ControlPanel.exe')]:
        path = copy(ROOT / source, 'payload/' + relative)
        entry = record(path); entry['path'] = relative
        payload.append(entry)
    copy(ROOT / 'installer/Manage-Installation.ps1', 'Manage-Installation.ps1')
    copy(ROOT / 'config/runtime.ini', 'runtime.ini')
    copy(ROOT / 'docs/USAGE.md', 'README.md')
    copy(ROOT / 'docs/USAGE.md', 'QUICK_START.md')
    copy(ROOT / 'LICENSE', 'LICENSE')
    copy(ROOT / 'LICENSING.md', 'LICENSING.md')
    copy(ROOT / 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md')
    for name in ('LICENSE.SafetyHook', 'LICENSE.Zydis'):
        copy(ROOT / 'research/safetyhook-local-v0.7.0' / name, 'licenses/' + name)
    copy(ROOT / '.build/current-validation/package-validator/EquipmentMorphPackageValidator.exe', 'Tools/EquipmentMorphPackageValidator.exe')
    for name in ('Check-EquipmentMorph.ps1', 'prepare_mhwi_equipmentmorph_resource_path.py', 'manage_equipment_morph_mod.py'):
        copy(ROOT / 'tools' / name, 'Tools/' + name)
    addon = ROOT / CONFIG['addon_directory'] / 'mhwi_equipmentmorph'
    for name, source in [('output_files.py', addon / 'output_files.py'),
                         ('emit_equipment_morph_profile.py', addon / 'backend/emit_equipment_morph_profile.py'),
                         ('mesh_contract.py', addon / 'backend/mesh_contract.py')]:
        copy(source, 'Tools/author_backend/' + name)
    manifest = {'schema': 'mhwi-equipmentmorph-runtime-distribution-v1', 'project_name': 'MHWI-EquipmentMorph',
                'version': VERSION, 'files': payload,
                'empty_config_sha256': hashlib.sha256((PACKAGE / 'runtime.ini').read_bytes()).hexdigest(),
                'includes_mod_assets': False, 'formal_acceptance_complete': False,
                'project_license': 'GPL-3.0-or-later',
                'build_origin': 'local source build; distinct from byte-verified accepted release'}
    (PACKAGE / 'manifest.json').write_bytes((json.dumps(manifest, indent=2) + '\n').encode())
    with zipfile.ZipFile(PACKAGE.with_suffix('.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(PACKAGE.rglob('*')):
            if path.is_file(): archive.write(path, PACKAGE.name + '/' + path.relative_to(PACKAGE).as_posix())
    output = ROOT / ('.build/packages/MHWI-EquipmentMorph-Blender-' + CONFIG['addon_version'] + '.zip')
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(addon.rglob('*')):
            if path.is_file() and '__pycache__' not in path.parts and path.suffix != '.pyc' and path.name not in ('LICENSE', 'LICENSING.md'):
                archive.write(path, 'mhwi_equipmentmorph/' + path.relative_to(addon).as_posix())
        for name in ('LICENSE', 'LICENSING.md'):
            archive.write(ROOT / name, 'mhwi_equipmentmorph/' + name)
    print('PASS: packaged local runtime and Blender addon under .build/packages; no installation')

if __name__ == '__main__': main()
