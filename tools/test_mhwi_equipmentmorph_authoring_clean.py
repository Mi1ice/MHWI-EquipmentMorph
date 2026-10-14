"""Maintained synthetic authoring checks; no historical MOD or Blender dependency."""
import hashlib
import importlib
import json
import struct
import sys
import types
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
NAME = 'mhwi_clean_authoring_test'
FOLDER = ROOT / (sys.argv[1] if len(sys.argv)==2 else 'blender_addon_v044/mhwi_equipmentmorph')
for name, path in [(NAME, FOLDER), (NAME + '.backend', FOLDER / 'backend')]:
    module = types.ModuleType(name)
    module.__path__ = [str(path)]
    sys.modules[name] = module
mesh = importlib.import_module(NAME + '.backend.mesh_contract')
pipeline = importlib.import_module(NAME + '.backend.equipment_morph_package_pipeline')
emit = importlib.import_module(NAME + '.backend.emit_equipment_morph_profile')
mapping = importlib.import_module(NAME + '.backend.map_equipment_morph_job')
export = importlib.import_module(NAME + '.backend.equipment_morph_export_pipeline')


def main():
    checks = 0
    def check(ok, label):
        nonlocal checks
        if not ok:
            raise AssertionError(label)
        checks += 1
    # Retain the prior solver's synthetic ambiguity/UV cases after retiring its MOD fixtures.
    a, b = [0., 0.], [.5, .5]
    uv = export.uv_bytes(a)
    def solve(positions, uvs, point=(0., 0., 0.)):
        return mapping.solve_vertices(positions, list(range(len(positions))), uvs, [point], [uv], .001)
    check(solve([[0, 0, 0], [0, 0, 0]], [b, a])['disk_to_blender_vertex'] == [1], 'UV disambiguates coincident positions')
    check(not solve([[0, 0, 0], [0, 0, 0]], [a, a])['fully_resolved'], 'identical position/UV refuses ambiguity')
    check(not solve([[0, 0, 0]], [b])['fully_resolved'], 'wrong UV refuses mapping')
    check(not solve([[.002, 0, 0]], [a])['fully_resolved'], 'distant position refuses mapping')
    check(not solve([[0, 0, 0], [.0005, 0, 0]], [b, a])['fully_resolved'], 'farther UV match cannot replace nearest position')
    check(solve([[.00101, 0, 0]], [a], (.00099, 0, 0))['fully_resolved'], 'neighboring spatial cells match')
    for label, action in [
        ('nonfinite coordinates', lambda: solve([[float('nan'), 0, 0]], [a])),
        ('invalid loop', lambda: mapping.solve_vertices([[0, 0, 0]], [1], [a], [[0, 0, 0]], [uv], .001)),
        ('unsafe tolerance', lambda: mapping.solve_vertices([[0, 0, 0]], [0], [a], [[0, 0, 0]], [uv], 1.)),
        ('truncated MOD3', lambda: mapping.model_meshes(b'MOD\0')),
    ]:
        try:
            action()
        except ValueError:
            check(True, label + ' rejected')
        else:
            raise AssertionError(label + ' accepted')
    model = bytearray(128 + 3 * 80 + 112)
    model[:4] = b'MOD\0'
    for at, value, fmt in [(4, 237, '<H'), (8, 3, '<H'), (24, 112, '<I'),
                           (72, 128, '<Q'), (80, 368, '<Q')]:
        struct.pack_into(fmt, model, at, value)
    start = 0
    for i, (disk, (_, stride)) in enumerate(mesh.FORMATS.items()):
        row = 128 + i * 80
        struct.pack_into('<H', model, row + 2, 1)
        model[row + 14] = stride
        struct.pack_into('<I', model, row + 20, start)
        struct.pack_into('<I', model, row + 24, disk)
        start += stride
    part = {'part': 'body', 'model_id': 'pl/example/body'}
    result = pipeline.model_description(part, model, {'mode': 'serialized'})
    check(result['runtime_mesh_sha256'] == result['serialized_normalized_mesh_sha256'], 'body disk generation')
    check(result['normalization_adapter'] == mesh.ADAPTER and mesh.ADAPTER.endswith('-v2'), 'v2 marker')
    for name in ('head', 'arms', 'waist', 'legs'):
        other = pipeline.model_description(dict(part, part=name), model, {'mode': 'serialized'})
        check(other['runtime_mesh_sha256'] == result['runtime_mesh_sha256'], name + ' normalization')
    for changed, label in [('format', 'unknown disk format'), ('stride', 'incorrect stride')]:
        bad = bytearray(model)
        if changed == 'format':
            struct.pack_into('<I', bad, 152, 0xa756f2f9)
        else:
            bad[142] += 1
        try:
            mesh.normalized_rows(bad)
        except ValueError:
            check(True, label + ' rejected')
        else:
            raise AssertionError(label + ' accepted')
    disk = bytes(model[128:368])
    runtime = bytearray(disk)
    for i, (_, (native, _)) in enumerate(mesh.FORMATS.items()):
        struct.pack_into('<I', runtime, i * 80 + 24, native)
        runtime[i * 80 + 42] = i + 1
        struct.pack_into('<Q', runtime, i * 80 + 72, 0x12340000 + i * 4096)
    normalized_runtime = bytearray(runtime)
    for i in range(3):
        for at in mesh.MASK:
            normalized_runtime[i * 80 + at] = 0
    normalized = mesh.normalized_rows(model)
    check(normalized == normalized_runtime, 'synthetic initialized table equals normalized disk')
    for at in (14, 20, 28):
        changed = bytearray(normalized_runtime)
        changed[at] ^= 1
        check(changed != normalized, 'unmasked field retained ' + str(at))
    proof = json.loads((ROOT / 'config/format_provenance.json').read_bytes())
    observed = {int(e['disk_format'], 16): (e['native_format'], e['stride']) for e in proof['format_pairs']}
    check(observed == mesh.FORMATS, 'implementation still agrees with retained measured format evidence')
    description = {**result, 'adapter': mesh.ADAPTER, 'model_sha256': hashlib.sha256(model).hexdigest(),
                   'baseline_semantics': 'zero_contribution_restores_exported_mod3',
                   'asset_sha256': hashlib.sha256(b'fixture').hexdigest(), 'part': 1, 'mesh_count': 3,
                   'model_id': 'pl/example/body', 'capture_sha256': '0' * 64,
                   'controlled_keys': ['Example_A'], 'source_objects': [
                       {'name': 'Example', 'geometry_sha256': '0' * 64, 'values': [
                           {'name': 'Example_A', 'weight': 0, 'muted': False, 'dynamic': True},
                           {'name': 'fixed', 'weight': .6, 'muted': False, 'dynamic': False}]}]}
    encoded = emit.encode(description, b'fixture')
    check(mesh.ADAPTER.encode() in encoded and encoded.startswith(b'MHMPROF1'), 'binary profile contract')
    check(struct.pack('<fI', .6, 0) in encoded, 'nonzero fixed correction retained')
    output = ROOT / '.build/cleanup-validation'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'synthetic.mhmprofile').write_bytes(encoded)
    description['source_objects'][0]['values'][0]['weight'] = .2
    try:
        emit.encode(description, b'fixture')
    except ValueError:
        check(True, 'nonzero dynamic baseline rejected')
    else:
        raise AssertionError('nonzero dynamic baseline accepted')
    report = {'schema': 'mhwi-clean-authoring-checks-v1', 'passed': checks,
              'historical_mod_regression_reexecuted': False, 'uses_geometry_from_user_mod': False,
              'game_or_blender_changed': False, 'native_fixture': '.build/cleanup-validation/synthetic.mhmprofile'}
    (output / 'authoring-checks.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'PASS: {checks} synthetic authoring checks; not a repeat of historical MOD validation')


if __name__ == '__main__':
    main()
