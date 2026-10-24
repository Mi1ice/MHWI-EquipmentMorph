"""Synthetic capacity fixtures and behavioral authoring checks; no private assets."""
import hashlib
import importlib
import json
from pathlib import Path
import struct
import sys
import types

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT / 'blender_addon_v044/mhwi_equipmentmorph'
NAME = 'capacity_authoring_test'
for name, folder in [(NAME, FOLDER), (NAME + '.backend', FOLDER / 'backend')]:
    module = types.ModuleType(name)
    module.__path__ = [str(folder)]
    sys.modules[name] = module
catalog = importlib.import_module(NAME + '.backend.prepare_equipment_morph_catalog')
pipeline = importlib.import_module(NAME + '.backend.equipment_morph_export_pipeline')
pack = importlib.import_module(NAME + '.per_key_pack')
direct = importlib.import_module(NAME + '.direct_export')


def main():
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    failures = []
    checks = 0

    def check(label, action, accepts=True):
        nonlocal checks
        checks += 1
        try:
            action()
        except (ValueError, AssertionError) as error:
            if accepts:
                failures.append(f'{label}: {error}')
        else:
            if not accepts:
                failures.append(label + ': unexpectedly accepted')

    def equal(actual, expected):
        if actual != expected:
            raise AssertionError('serialized payload differs')

    basis = struct.pack('<3f4b8x', 1., 2., 3., 127, 0, 0, 0)
    delta = struct.pack('<I3f', 0, .25, .5, .75)
    sha = lambda data: hashlib.sha256(data).hexdigest()
    (output / 'basis.bin').write_bytes(basis)
    (output / 'delta.bin').write_bytes(delta)
    (output / 'rotations.bin').write_bytes(b'')
    # Only bpy's scene access is replaced; all three production validators run.
    sys.modules['bpy'] = types.ModuleType('bpy')
    preview = types.ModuleType(NAME + '.shape_preview')
    preview.running = lambda: False
    sys.modules[preview.__name__] = preview
    key = types.SimpleNamespace(value=0, mute=False, vertex_group='', relative_key=None)
    shape = types.SimpleNamespace(use_relative=True, reference_key=None, animation_data=None,
                                  key_blocks=types.SimpleNamespace(get=lambda name: key))
    obj = types.SimpleNamespace(type='MESH', name='Fixture', library=None, animation_data=None,
        constraints=[], modifiers=[], show_only_shape_key=False,
        data=types.SimpleNamespace(library=None, animation_data=None, shape_keys=shape))
    collection = types.SimpleNamespace(library=None, all_objects=[obj])
    for count in (0, 1, 64, 65, 489, 512, 1024, 1025):
        keys = [dict(id=f'Key_{i:04}', source_key=f'Source_{i:04}', minimum=0, maximum=1,
                     author_shading=False, exclusive_group=0, positions='delta.bin', position_count=1,
                     positions_sha256=sha(delta)) for i in range(count)]
        manifest = dict(schema='mhw-equipment-morph-package-v1', abi_version=65536,
            model_id='pl/synthetic/body', part='body', basis='basis.bin', basis_bytes=len(basis),
            basis_sha256=sha(basis), keys=keys,
            shading=dict(mode='shared-author-intent-max', rotation_count=0,
                         rotations='rotations.bin', rotations_sha256=sha(b'')))
        path = output / 'manifest.json'
        path.write_text(json.dumps(manifest), encoding='utf-8')
        # Independent wire fixture also lets native rejection tests run during RED.
        model = manifest['model_id'].encode('ascii')
        legacy = b'MHMPACK1' + struct.pack('<6I', 1, 1, len(basis), count, 0, len(model)) + model + basis
        for item in keys:
            ident = item['id'].encode('ascii')
            legacy += struct.pack('<6I', len(ident), 0, 0x3f800000, 0, 0, 1) + ident + delta
        advanced = pack.upgrade(legacy, {item['id']: b'' for item in keys})
        (output / f'count-{count}.mhm1').write_bytes(legacy)
        (output / f'count-{count}.mhm2').write_bytes(advanced)
        valid = 1 <= count <= 1024
        check(f'manifest count {count}', lambda: equal(catalog.prepare(path)[0], legacy), valid)
        job = dict(schema='mhw-equipment-morph-export-job-v2', parts=[dict(part='body', keys=keys)])
        job_path = output / 'job.json'
        job_path.write_text(json.dumps(job), encoding='utf-8')
        check(f'job count {count}', lambda: pipeline.load_job(job_path), valid)
        asset = dict(part='body', collection=collection, shading='BASELINE',
                     model_id='pl/synthetic/body', package='capacity-fixture', keys=keys)
        check(f'direct count {count}', lambda: direct.preflight(asset), valid)
        if valid:
            check(f'per-key prepare count {count}', lambda: equal(pack.prepare(path)[0], legacy))
            advanced_manifest = dict(manifest, schema='mhwi-equipmentmorph-package-v2',
                shading=dict(manifest['shading'], mode='per-key-direction-deltas'),
                per_key_normals=dict(mode='per-key-direction-deltas', asset_format=2,
                    keys={item['id']: dict(file='rotations.bin', records=0, sha256=sha(b'')) for item in keys}))
            path.write_text(json.dumps(advanced_manifest), encoding='utf-8')
            check(f'advanced prepare count {count}', lambda: equal(pack.prepare(path)[0], advanced))
    # Retain identifier, record and digest protections independently of key count.
    manifest['keys'] = keys[:1]
    for field, value in [('id', 'x' * 64), ('position_count', 1000001), ('positions_sha256', '0' * 64)]:
        original = manifest['keys'][0][field]
        manifest['keys'][0][field] = value
        path.write_text(json.dumps(manifest), encoding='utf-8')
        check('reject ' + field, lambda: catalog.prepare(path), False)
        manifest['keys'][0][field] = original
    manifest['keys'] *= 2
    path.write_text(json.dumps(manifest), encoding='utf-8')
    check('duplicate key ID', lambda: catalog.prepare(path), False)
    report = dict(checks=checks, failures=failures, synthetic_only=True)
    (output / 'authoring.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    for failure in failures:
        print('FAIL:', failure)
    print(f'Authoring capacity: {checks - len(failures)}/{checks}')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
