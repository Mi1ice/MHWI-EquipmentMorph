"""Package local build outputs; never install them or claim game acceptance."""
import hashlib
import json
from pathlib import Path
import shutil
import zipfile
import argparse
from desktop_notices import collect as desktop_notices

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

def addon_files(addon):
    """Select exactly the current generated author component, never stale DLLs."""
    native=addon/'_native/build'
    try:
        manifest=json.loads((native/'build.json').read_bytes());digest=manifest['sha256'];name=manifest['file']
        if manifest['schema']!='mhwi-author-shading-native-v1' or manifest['abi']!=1 or manifest['platform']!='windows-x64' or manifest['blender']!='5.1.2':raise ValueError('Unexpected author component metadata')
        if len(digest)!=64 or any(c not in '0123456789abcdef' for c in digest) or name!='mhm_author_shading-'+digest+'.dll':raise ValueError('Unexpected author component filename')
        if hashlib.sha256((native/name).read_bytes()).hexdigest()!=digest:raise ValueError('Author component digest mismatch')
        for license in ('NOTICE.txt','LICENSE.Apache-2.0.txt'):
            raw=(native/license).read_bytes()
            if not raw or hashlib.sha256(raw).hexdigest()!=manifest['license_sha256'][license]:raise ValueError('Author component license mismatch')
    except (OSError,KeyError,TypeError,ValueError) as error:
        raise ValueError('Build the author shading component before packaging; missing, mismatched or incomplete component') from error
    generated={native/entry for entry in (name,'build.json','NOTICE.txt','LICENSE.Apache-2.0.txt')}
    return [path for path in sorted(addon.rglob('*')) if path.is_file() and '__pycache__' not in path.parts and path.suffix!='.pyc'
            and path.name not in ('LICENSE','LICENSING.md') and (native not in path.parents or path in generated)]

def runtime_author_helpers(addon):
    return {'output_files.py':addon/'output_files.py',
            'capacity.py':addon/'capacity.py',
            'emit_equipment_morph_profile.py':addon/'backend/emit_equipment_morph_profile.py',
            'mesh_contract.py':addon/'backend/mesh_contract.py',
            'vertex_formats.py':addon/'backend/vertex_formats.py'}

def main():
    global PACKAGE
    parser = argparse.ArgumentParser()
    controls = parser.add_mutually_exclusive_group()
    controls.add_argument('--desktop', action='store_true', help='Include the retired Tauri control center and automatic launch bridge')
    controls.add_argument('--core-only', action='store_true', help='Package the independent runtime without a control panel')
    controls.add_argument('--imgui', action='store_true', help='Package the runtime and optional in-game Dear ImGui panel')
    args = parser.parse_args()
    if args.desktop:
        PACKAGE = ROOT / '.build/packages/MHWI-EquipmentMorph-0.5.1-dev12-controlcenter-dev1'
    elif args.core_only or args.imgui:
        PACKAGE = PACKAGE.with_name(PACKAGE.name + ('-imgui' if args.imgui else '-core'))
    addon = ROOT / CONFIG['addon_directory'] / 'mhwi_equipmentmorph'
    author_files=addon_files(addon)
    # All outputs are under the ignored .build folder and must be rebuilt locally.
    if not PACKAGE.resolve().is_relative_to((ROOT / '.build').resolve()):
        raise ValueError('Package destination must stay inside .build')
    PACKAGE.mkdir(parents=True, exist_ok=True)
    payload = []
    components = [('.build/cleanup-validation/native/MHWEquipmentMorphConfiguredPrototype.dll', 'nativePC/plugins/MHWEquipmentMorphConfiguredPrototype.dll')]
    if args.imgui:
        components.append(('.build/current-validation/imgui/MHWEquipmentMorphImGui.dll', 'nativePC/plugins/MHWEquipmentMorphImGui.dll'))
    elif not args.core_only:
        components.extend([
        ('.build/current-validation/control-panel/MHWEquipmentMorphControlBridge.dll', 'nativePC/plugins/MHWEquipmentMorphControlBridge.dll'),
        ('.build/current-validation/control-panel/MHWI-EquipmentMorph-ControlPanel.exe', 'MHWI-EquipmentMorph/MHWI-EquipmentMorph-ControlPanel.exe')])
    if args.core_only or args.imgui:
        expected = {relative for _, relative in components}
        for path in (PACKAGE / 'payload').rglob('*'):
            if path.is_file() and path.relative_to(PACKAGE / 'payload').as_posix() not in expected:
                raise ValueError('Unexpected payload in package directory; use a clean build output')
    for source, relative in components:
        path = copy(ROOT / source, 'payload/' + relative)
        entry = record(path); entry['path'] = relative
        payload.append(entry)
    if args.desktop:
        predecessors = json.loads((ROOT / 'config/control-center-predecessors.json').read_text(encoding='utf-8'))
        for entry in payload:
            if entry['path'] in predecessors['files']:
                entry['accepted_predecessor_sha256'] = predecessors['files'][entry['path']]
    if args.desktop:
        path = copy(ROOT / '.build/current-validation/desktop/MHWI-EquipmentMorph-ControlCenter.exe',
                    'payload/MHWI-EquipmentMorph/MHWI-EquipmentMorph-ControlCenter.exe')
        entry = record(path); entry['path'] = 'MHWI-EquipmentMorph/MHWI-EquipmentMorph-ControlCenter.exe'
        payload.append(entry)
    copy(ROOT / 'installer/Manage-Installation.ps1', 'Manage-Installation.ps1')
    copy(ROOT / 'config/runtime.ini', 'runtime.ini')
    guide = 'docs/IMGUI_INSTALLATION.md' if args.core_only or args.imgui else 'docs/USAGE.md'
    copy(ROOT / guide, 'README.md')
    copy(ROOT / guide, 'QUICK_START.md')
    if args.imgui:
        copy(ROOT / 'docs/PRESETS.md', 'PRESETS.md')
        copy(ROOT / 'research/imgui-v1.93.0/LICENSE.txt', 'licenses/LICENSE.ImGui.txt')
    copy(ROOT / 'docs/INPUT_DIAGNOSTICS.md', 'INPUT_DIAGNOSTICS.md')
    copy(ROOT / 'docs/DRAW_FILTER.md', 'DRAW_FILTER.md')
    if args.desktop:
        copy(ROOT / 'controls/MHWI-EquipmentMorph-Desktop/README.md', 'CONTROL_CENTER.md')
        (PACKAGE / 'CONTROL_CENTER_THIRD_PARTY_NOTICES.txt').write_text(desktop_notices(ROOT), encoding='utf-8')
    copy(ROOT / 'LICENSE', 'LICENSE')
    copy(ROOT / 'LICENSING.md', 'LICENSING.md')
    copy(ROOT / 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md')
    for name in ('LICENSE.SafetyHook', 'LICENSE.Zydis'):
        copy(ROOT / 'research/safetyhook-local-v0.7.0' / name, 'licenses/' + name)
    copy(ROOT / '.build/current-validation/package-validator/EquipmentMorphPackageValidator.exe', 'Tools/EquipmentMorphPackageValidator.exe')
    for name in ('Check-EquipmentMorph.ps1', 'prepare_mhwi_equipmentmorph_resource_path.py', 'manage_equipment_morph_mod.py'):
        copy(ROOT / 'tools' / name, 'Tools/' + name)
    for name, source in runtime_author_helpers(addon).items():
        copy(source, 'Tools/author_backend/' + name)
    manifest = {'schema': 'mhwi-equipmentmorph-runtime-distribution-v1', 'project_name': 'MHWI-EquipmentMorph',
                'version': PACKAGE.name.removeprefix('MHWI-EquipmentMorph-') if args.desktop else VERSION, 'files': payload,
                'empty_config_sha256': hashlib.sha256((PACKAGE / 'runtime.ini').read_bytes()).hexdigest(),
                'includes_mod_assets': False, 'formal_acceptance_complete': False,
                'project_license': 'GPL-3.0-or-later',
                'build_origin': 'local source build; distinct from byte-verified accepted release'}
    if args.desktop:
        manifest['runtime_version'] = VERSION
        manifest['control_center_version'] = '0.2.0'
        manifest['hotkey'] = 'Ctrl+Shift+M'
    if args.core_only or args.imgui:
        manifest['control_panel'] = 'imgui' if args.imgui else 'none'
        manifest['preset_config_policy'] = 'current-schema-only; no previous-test-format migration'
        if args.imgui: manifest['hotkey'] = 'Del'
    (PACKAGE / 'manifest.json').write_bytes((json.dumps(manifest, indent=2) + '\n').encode())
    with zipfile.ZipFile(PACKAGE.parent / (PACKAGE.name + '.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(PACKAGE.rglob('*')):
            if path.is_file(): archive.write(path, PACKAGE.name + '/' + path.relative_to(PACKAGE).as_posix())
    output = ROOT / ('.build/packages/MHWI-EquipmentMorph-Blender-' + CONFIG['addon_version'] + '.zip')
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in author_files:
            archive.write(path, 'mhwi_equipmentmorph/' + path.relative_to(addon).as_posix())
        for name in ('LICENSE', 'LICENSING.md'):
            archive.write(ROOT / name, 'mhwi_equipmentmorph/' + name)
    print('PASS: packaged local runtime and Blender addon under .build/packages; no installation')

if __name__ == '__main__': main()
