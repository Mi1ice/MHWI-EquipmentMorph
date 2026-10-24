"""Background-only paired export, independent split-vertex and reference checks."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import numpy as np
import bpy

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'blender_addon_v044')]
import mhwi_equipmentmorph as addon
from mhwi_equipmentmorph.backend.map_equipment_morph_job import model_meshes
from mhwi_equipmentmorph.backend.capture_equipment_morph_job import fingerprint
from blender_export_fixture import fixture, exporter
from capture_morph_position_reference import capture_reference


def main(counts=(65, 1024)):
    addon.register()
    output = Path(tempfile.mkdtemp(prefix='paired-', dir=ROOT / '.build/capacity-validation'))
    comparator = ROOT / '.build/capacity-validation/CompareMorphPositions.exe'
    checks = []

    def check(ok, label):
        assert ok, label
        checks.append(label)

    def compare(pack, reference, name, success=True):
        report = reference.parent / (name + '.json')
        result = subprocess.run([str(comparator), '--pack', str(pack), '--reference', str(reference),
                                 '--output', str(report)], capture_output=True, text=True)
        check((result.returncode == 0) == success, name + ': ' + result.stdout + result.stderr)
        return json.loads(report.read_bytes()) if success else None

    for count in counts:
        collection, obj = fixture()
        keys = []
        for i in range(count):
            name = f'源鍵_表情_{i:04}'
            block = obj.shape_key_add(name=name, from_mix=False)
            block.value = 0
            block.data[0].co.z = (i + 1) / 32768
            block.data[4].co.z = (i + 1) / 16384
            keys.append(dict(id=f'Key_{i}', source_key=name, author_shading=False))
        bpy.context.view_layer.update()
        before = {o.name: fingerprint(o) for o in collection.all_objects if o.type == 'MESH'}
        # Independently known, literal source displacements distinguish the two coincident vertices.
        known = np.asarray([tuple(v.co) for v in obj.data.vertices], dtype=np.float64)
        known[1, 2] = .04  # fixed neck=.4, endpoint displacement=.1
        matrix = np.asarray(exporter.exportMatrix, dtype=np.float64)
        for shading in ('BASELINE', 'GEOMETRIC'):
            payloads = []
            for debug in (False, True):
                asset = dict(part='body', collection=collection.name, model_id='pl/synthetic/body',
                    package='body', shading=shading, keys=keys)
                recipe = dict(assets=[asset], export_matrix=matrix.tolist(), debug=debug)
                destination = output / f'{count}-{shading}-{debug}'
                result = capture_reference(recipe, destination)
                reference = destination / 'references/0/reference.json'
                meta = json.loads(reference.read_bytes())
                pack = destination / 'export/nativePC/pl/synthetic/body.morph/asset.mhmpack'
                model = destination / 'export/nativePC/pl/synthetic/body.mod3'
                report = compare(pack, reference, 'comparison')
                check(report['status'] == 'measurement_completed_threshold_not_set', 'no invented tolerance')
                check(report['key_count'] == count and report['zero_restored'] and report['non_position_valid'], 'coverage and exact restoration')
                check(meta['schema'] == 'mhwi-morph-position-reference-v1' and len(meta['key_mapping']) == count, 'reference schema and mapping')
                check(len(meta['serialized_objects']) == 3, 'uncontrolled fixed meshes retained')
                endpoint = next(c for c in meta['cases'] if c.get('key_id') == 'Key_0' and c['weights'] == {'Key_0': 1})
                binary = (reference.parent / 'positions.bin').read_bytes()
                positions = np.frombuffer(binary, dtype='<f8', count=len(meta['position_offsets']) * 3,
                                          offset=endpoint['byte_offset']).reshape((-1, 3))
                basis, rows = model_meshes(model.read_bytes())
                by_offset = {offset: i for i, offset in enumerate(meta['position_offsets'])}
                verified = set()
                for row in rows:
                    for i in range(row['count']):
                        offset = row['start'] + i * row['stride']
                        disk = np.array(struct.unpack_from('<3f', basis, offset))
                        local = np.linalg.solve(matrix, np.r_[disk, 1.])[:3]
                        if np.min(np.linalg.norm(known - local, axis=1)) > 1e-5:
                            continue  # siblings have separate translations, not this known source mesh
                        uv = basis[offset + 20:offset + 24]
                        candidates = []
                        for vi in range(8):
                            for loop in obj.data.loops:
                                if loop.vertex_index != vi:
                                    continue
                                source_uv = obj.data.uv_layers.active.data[loop.index].uv
                                source_half = np.array(source_uv, dtype='<f2')
                                encoded = np.array([source_half[0], 1 - float(source_half[1])], dtype='<f2').tobytes()
                                if uv == encoded and np.linalg.norm(known[vi] - local) < 1e-5:
                                    candidates.append(vi)
                        check(len(set(candidates)) == 1, f'independent position/UV identifies source: {offset} {local} {uv.hex()} {candidates}')
                        vi = candidates[0]
                        target = known[vi].copy()
                        if vi == 0:
                            target[2] += 1 / 32768
                        elif vi == 4:
                            target[2] += 1 / 16384
                        expected = (matrix @ np.r_[target, 1.])[:3]
                        # Fixture oracle only; this is not a product/game acceptance tolerance.
                        check(np.linalg.norm(positions[by_offset[offset]] - expected) < 1e-5,
                              'known split/coincident endpoint independent of trace')
                        verified.add(vi)
                check({0, 4}.issubset(verified), 'both coincident vertices independently exercised')
                raw = model.read_bytes()
                payloads.append((raw[:40] + b'\0' * 8 + raw[48:], pack.read_bytes()))
                check(before == {o.name: fingerprint(o) for o in collection.all_objects if o.type == 'MESH'}, 'source and fixed values restored')
                if count == 65 and shading == 'BASELINE' and not debug:
                    for label, mutate in [
                        ('bad-offset', lambda m: m['position_offsets'].__setitem__(0, 0xffffffff)),
                        ('duplicate-offset', lambda m: m['position_offsets'].__setitem__(1, m['position_offsets'][0])),
                        ('bad-byte-offset', lambda m: m['cases'][0].__setitem__('byte_offset', -1)),
                        ('huge-byte-offset', lambda m: m['cases'][0].__setitem__('byte_offset', 2**64)),
                        ('wrong-sha', lambda m: m.__setitem__('positions_sha256', '0' * 64)),
                        ('missing-key', lambda m: m['key_mapping'].pop()),
                        ('missing-case', lambda m: m['cases'].pop()),
                        ('duplicate-weight', lambda m: m['cases'][1]['weights'].__setitem__('missing', .5)),
                    ]:
                        bad = copy.deepcopy(meta)
                        mutate(bad)
                        path = reference.parent / (label + '-reference.json')
                        path.write_text(json.dumps(bad, ensure_ascii=False), encoding='utf-8')
                        compare(pack, path, label, False)
                    path = reference.parent / 'nonfinite-reference.json'
                    bad = copy.deepcopy(meta)
                    altered = bytearray(binary);struct.pack_into('<d', altered, 0, float('nan'))
                    (reference.parent / 'bad-positions.bin').write_bytes(altered)
                    bad['positions_file'] = 'bad-positions.bin';bad['positions_sha256'] = hashlib.sha256(altered).hexdigest()
                    path.write_text(json.dumps(bad, ensure_ascii=False), encoding='utf-8')
                    compare(pack, path, 'nonfinite', False)
                check(bool(result['references']), 'reference receipt returned')
            check(payloads[0] == payloads[1], shading + ' ordinary/Debug payload parity')
        bad = copy.deepcopy(recipe)
        bad['assets'][0]['keys'][1]['id'] = bad['assets'][0]['keys'][0]['id']
        try:
            capture_reference(bad, output / f'{count}-collision')
        except ValueError:
            check(True, 'public ID collision rejected')
        else:
            raise AssertionError('public ID collision accepted')
    report = dict(passed=len(checks), checks=checks, output=str(output), tested_counts=list(counts), synthetic_only=True,
                  game_tested=False, tolerance_set=False)
    (ROOT / '.build/capacity-validation/paired-checks.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f'PASS: {len(checks)} paired capacity checks')


if __name__ == '__main__':
    main()
