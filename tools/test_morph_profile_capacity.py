"""Profile metadata guard tests approved during paired-capacity implementation."""
import hashlib
import importlib
import json
import struct


def run(package, output):
    emit = importlib.import_module(package + '.backend.emit_equipment_morph_profile')
    mesh = importlib.import_module(package + '.backend.mesh_contract')
    sha = lambda value: hashlib.sha256(value).hexdigest()
    payload = b'profile-fixture'
    checks = 0
    failures = []
    cases = []
    string = lambda text: struct.pack('<I', len(text.encode('utf-8'))) + text.encode('utf-8')
    for dynamic, fixed, valid in [(1, 0, True), (65, 2, True), (489, 2, True), (1024, 2, True),
                                 (1024, 256, True), (0, 256, True), (0, 0, False),
                                 (1025, 0, False), (1, 257, False), (1024, 257, False)]:
        values = [dict(name=f'Key_{i}', weight=0, muted=False, dynamic=True) for i in range(dynamic)]
        values += [dict(name=f'Fixed_{i}', weight=.5, muted=False, dynamic=False) for i in range(fixed)]
        description = dict(adapter=mesh.ADAPTER, baseline_semantics='zero_contribution_restores_exported_mod3',
            part=1, vertex_bytes=24, mesh_count=1, model_bytes=128, model_id='pl/synthetic/body',
            model_sha256='0' * 64, asset_sha256=sha(payload), runtime_mesh_sha256='1' * 64,
            capture_sha256='2' * 64, controlled_keys=[v['name'] for v in values if v['dynamic']],
            source_objects=[dict(name='Synthetic', geometry_sha256='3' * 64, values=values)])
        # Independent wire fixture so native RED covers the decoder even when Python rejects.
        raw = b'MHMPROF1' + struct.pack('<I', 1) + string(mesh.ADAPTER) + struct.pack('<4I', 1, 24, 1, 128)
        raw += string('pl/synthetic/body')
        for name in ('model_sha256', 'asset_sha256', 'runtime_mesh_sha256', 'capture_sha256'):
            raw += string(description[name])
        raw += struct.pack('<I', 1) + string('Synthetic') + string('3' * 64) + struct.pack('<I', len(values))
        for value in values:
            raw += string(value['name']) + struct.pack('<fI', value['weight'], 2 if value['dynamic'] else 0)
        filename = f'profile-{dynamic}-{fixed}.bin'
        (output / filename).write_bytes(raw)
        cases.append(dict(file=filename, dynamic=dynamic, fixed=fixed, valid=valid))
        checks += 1
        try:
            actual = emit.encode(description, payload)
        except ValueError as error:
            if valid:
                failures.append(f'profile {dynamic}+{fixed}: {error}')
        else:
            if not valid or actual != raw:
                failures.append(f'profile {dynamic}+{fixed}: incorrect acceptance/payload')
    # Keep the independent profile byte guard, including on otherwise-valid individual objects.
    description['controlled_keys'] = ['Key_0']
    description['source_objects'] = [dict(name=f'Object_{i}', geometry_sha256='3' * 64,
        values=[dict(name='Key_0', weight=0, muted=False, dynamic=True)] +
               [dict(name=f'Fixed_{j}_' + 'x' * 1000, weight=.5, muted=False, dynamic=False) for j in range(256)]) for i in range(3)]
    checks += 1
    try:
        emit.encode(description, payload)
    except ValueError:
        pass
    else:
        failures.append('profile byte guard bypassed')
    (output / 'profile-cases.json').write_text(json.dumps(cases, indent=2), encoding='utf-8')
    return checks, failures
