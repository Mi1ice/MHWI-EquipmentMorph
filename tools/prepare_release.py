"""Assemble a local, commit-bound release candidate. Never push, tag or publish."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*arguments):
    return subprocess.check_output(['git', *arguments], cwd=ROOT, text=True).strip()


def package(version, mode):
    folder = ROOT / f'.build/packages/MHWI-EquipmentMorph-{version}-{mode}'
    manifest = json.loads((folder / 'manifest.json').read_bytes())
    expected = ['nativePC/plugins/MHWEquipmentMorphConfiguredPrototype.dll']
    if mode == 'imgui':
        expected.append('nativePC/plugins/MHWEquipmentMorphImGui.dll')
    if (manifest.get('schema') != 'mhwi-equipmentmorph-runtime-distribution-v1'
            or manifest.get('version') != version
            or manifest.get('control_panel') != ('imgui' if mode == 'imgui' else 'none')
            or [entry['path'] for entry in manifest['files']] != expected
            or manifest.get('formal_acceptance_complete') is not False):
        raise ValueError('Unexpected component manifest: ' + mode)
    archive = Path(str(folder) + '.zip')
    with zipfile.ZipFile(archive) as source:
        if source.testzip() is not None:
            raise ValueError('Invalid component archive: ' + mode)
        prefix = folder.name + '/'
        allowed = {
            'manifest.json', 'Manage-Installation.ps1', 'runtime.ini', 'README.md', 'QUICK_START.md',
            'INPUT_DIAGNOSTICS.md', 'DRAW_FILTER.md', 'LICENSE', 'LICENSING.md', 'THIRD_PARTY_NOTICES.md',
            'licenses/LICENSE.SafetyHook', 'licenses/LICENSE.Zydis',
            'Tools/EquipmentMorphPackageValidator.exe', 'Tools/Check-EquipmentMorph.ps1',
            'Tools/prepare_mhwi_equipmentmorph_resource_path.py', 'Tools/manage_equipment_morph_mod.py',
            'Tools/author_backend/output_files.py', 'Tools/author_backend/emit_equipment_morph_profile.py',
            'Tools/author_backend/mesh_contract.py',
        } | {'payload/' + name for name in expected}
        if mode == 'imgui':
            allowed |= {'PRESETS.md', 'licenses/LICENSE.ImGui.txt'}
        if sorted(source.namelist()) != sorted(prefix + name for name in allowed):
            raise ValueError('Unexpected or missing component archive member: ' + mode)
        actual = {path.relative_to(folder).as_posix() for path in folder.rglob('*') if path.is_file()}
        if actual != allowed:
            raise ValueError('Unexpected or missing package file: ' + mode)
        for name in allowed:
            if source.read(prefix + name) != (folder / name).read_bytes():
                raise ValueError('Archive differs from package directory: ' + name)
        for entry in manifest['files']:
            if (digest(folder / 'payload' / entry['path']) != entry['sha256']
                    or hashlib.sha256(source.read(prefix + 'payload/' + entry['path'])).hexdigest() != entry['sha256']):
                raise ValueError('Runtime payload digest mismatch: ' + mode)
        if digest(folder / 'runtime.ini') != manifest['empty_config_sha256']:
            raise ValueError('Public configuration template differs')
        if (folder / 'Manage-Installation.ps1').read_bytes() != (ROOT / 'installer/Manage-Installation.ps1').read_bytes():
            raise ValueError('Rebuild packages after changing the installer')
    return folder, archive, manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, help='New output directory inside .build')
    args = parser.parse_args()
    if git('status', '--porcelain', '--untracked-files=all'):
        raise ValueError('Commit the reviewed source before assembling a release candidate')
    config = json.loads((ROOT / 'config/mhwi-equipmentmorph-current.json').read_bytes())
    version = config['runtime_version']
    output = (args.output or ROOT / f'.build/releases/MHWI-EquipmentMorph-{version}').resolve()
    if not output.is_relative_to((ROOT / '.build').resolve()) or output == (ROOT / '.build').resolve():
        raise ValueError('Release output must be a new directory below .build')
    if output.exists():
        raise ValueError('Release output already exists; select a new directory')
    head = git('rev-parse', 'HEAD')
    core, core_zip, core_manifest = package(version, 'core')
    _, imgui_zip, gui_manifest = package(version, 'imgui')
    if core_manifest['files'][0]['sha256'] != gui_manifest['files'][0]['sha256']:
        raise ValueError('Both component packages must use the same core')
    blender = ROOT / f'.build/packages/MHWI-EquipmentMorph-Blender-{config["addon_version"]}.zip'
    with zipfile.ZipFile(blender) as archive:
        if archive.testzip() is not None or 'mhwi_equipmentmorph/LICENSE' not in archive.namelist():
            raise ValueError('Invalid Blender archive or missing license')
    output.mkdir(parents=True)
    for archive in [core_zip, imgui_zip, blender]:
        shutil.copyfile(archive, output / archive.name)
    author_prefix = f'MHWI-EquipmentMorph-AuthorTools-{config["addon_version"]}'
    author_zip = output / (author_prefix + '.zip')
    author_guide = ('# 独立作者工具\n\n版本：' + config['addon_version']
                    + '\n\n运行 Tools/Check-EquipmentMorph.ps1，指定 -ExportDirectory 与 -RuntimeConfig。'
                    + '\nRuntimeConfig 应指向游戏中已安装的公共配置目录或运行时压缩包解压根目录。'
                    + '\n本包不提供 runtime.ini、Blender、游戏或模型素材。'
                    + '\n本次候选附带的源码归档包含工具和格式后端的对应源码。\n')
    with zipfile.ZipFile(author_zip, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted((core / 'Tools').rglob('*')):
            if path.is_file():
                archive.write(path, author_prefix + '/' + path.relative_to(core).as_posix())
        for name in ['LICENSE', 'LICENSING.md', 'THIRD_PARTY_NOTICES.md', 'licenses/LICENSE.SafetyHook', 'licenses/LICENSE.Zydis']:
            archive.write(core / name, author_prefix + '/' + name)
        archive.writestr(author_prefix + '/README.md', author_guide)
    sources = output / f'MHWI-EquipmentMorph-{version}-Source.zip'
    subprocess.run(['git', 'archive', '--format=zip', '--prefix=' + sources.stem + '/', '--output', str(sources), head], cwd=ROOT, check=True)
    shutil.copyfile(ROOT / 'docs/RELEASE_0.6.0.md', output / 'RELEASE_NOTES.md')
    files = sorted(path for path in output.iterdir() if path.is_file())
    metadata = {
        'schema': 'mhwi-local-release-candidate-v1', 'runtime_version': version,
        'planned_release_version': '0.6.0', 'author_tools_version': config['addon_version'],
        'sdk_version': '0.4.0', 'source_commit': head, 'core_payload': core_manifest['files'],
        'imgui_payload': gui_manifest['files'], 'formal_acceptance_complete': False,
        'published': False, 'preset_config_migration': False,
        'files': [{'name': path.name, 'bytes': path.stat().st_size, 'sha256': digest(path)} for path in files],
    }
    record = output / 'release-manifest.json'
    record.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8', newline='\n')
    checksums = ''.join(digest(path) + '  ' + path.name + '\n' for path in sorted([*files, record]))
    (output / 'SHA256SUMS.txt').write_text(checksums, encoding='utf-8', newline='\n')
    print(json.dumps({'release_candidate': str(output), 'version': version, 'source_commit': head, 'published': False}))


if __name__ == '__main__':
    main()
