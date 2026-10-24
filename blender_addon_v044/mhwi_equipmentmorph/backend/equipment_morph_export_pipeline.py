"""Configurable mapped-snapshot exporter. No Blender/game mutations or guessed maps."""
import hashlib
import json
from pathlib import Path
import struct

import numpy as np
from .prepare_equipment_morph_catalog import PARTS, unique_object, prepare, encode_catalog
from ..capacity import MAX_DYNAMIC_KEYS

ROOT = Path(__file__).resolve().parents[1]


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def read_json(path):
    return json.loads(Path(path).read_bytes(), object_pairs_hook=unique_object)


def pinned(item):
    path = Path(item['path'])
    if not path.is_absolute():
        path = ROOT / path
    raw = path.read_bytes()
    if digest(raw) != item['sha256']:
        raise ValueError(f'Pinned input changed: {path}')
    return raw


def load_job(path):
    job = read_json(path)
    if job['schema'] != 'mhw-equipment-morph-export-job-v2':
        raise ValueError('Unsupported export job')
    parts = job['parts']
    if not 0 < len(parts) <= 5 or len({p['part'] for p in parts}) != len(parts):
        raise ValueError('Configure one model per equipment part')
    for part in parts:
        if part['part'] not in PARTS or not 0 < len(part['keys']) <= MAX_DYNAMIC_KEYS:
            raise ValueError('Invalid part or key count')
        if len({k['id'] for k in part['keys']}) != len(part['keys']):
            raise ValueError('Duplicate key identifier')
        for key in part['keys']:
            ident = key['id'].encode('ascii')
            if not 0 < len(ident) < 64 or any(c < 32 or c > 126 for c in ident):
                raise ValueError('Invalid public key identifier')
            if type(key['author_shading']) is not bool:
                raise ValueError('Invalid shading flag')
    return job


def source_objects(reference):
    # Only these already observed historical capture formats are accepted.
    if reference['schema'] == 'mhw-equipment-morph-job-capture-v2':
        if not reference['source_state_unchanged'] or reference['scene_mutated']:
            raise ValueError('Reference capture changed source')
        return {o['name']: {'basis': o['basis'], 'matrix': o['matrix_world'],
                'keys': o['selected_keys'], 'fingerprint': o['legacy_fingerprint'],
                'uv': o['uv'], 'loops': o['loop_vertices'],
                'values': dict((v[0], v[1]) for v in o['legacy_fingerprint']['values'])}
                for o in reference['objects']}
    if reference['schema'] == 'mhw-equipment-morph-source-keys-v1':
        if not reference['source_state_unchanged'] or reference['scene_mutated']:
            raise ValueError('Reference capture changed source')
        return {o['name']: {'basis': o['basis'], 'matrix': o['matrix_world'],
                'keys': o['selected_keys'], 'metadata': o['keys'],
                'fingerprint': reference['source_fingerprints'][o['name']]} for o in reference['objects']}
    if reference['schema'] == 'mhw-equipment-arms-morph-source-v1':
        if reference['scene_modified'] or reference['rig_rest_pose_matrix_error'] or reference['bone_constraints']:
            raise ValueError('Reference arm capture is not unchanged/rest pose')
        return {o['name']: {'basis': o['basis_positions'], 'matrix': o['matrix_world'],
                'keys': {o['key_id']: o['target_positions']}, 'uv': o['loop_uvs'],
                'loops': o['loop_vertex_indices'], 'values': o['key_values']} for o in reference['objects']}
    raise ValueError('Unsupported mapping source reference')


def uv_bytes(uv):
    half = np.asarray(uv, dtype='<f2')
    if not np.isfinite(half).all():
        raise ValueError('Invalid UV')
    return np.array([half[0], 1-float(half[1])], dtype='<f2').tobytes()


def records_checked(raw, size, basis, fields, rotation=False):
    if len(raw) % size:
        raise ValueError('Truncated sparse payload')
    previous = -1
    for values in struct.iter_unpack('<I4f' if rotation else '<I3f', raw):
        offset, vector = values[0], values[1:]
        if offset <= previous or offset not in fields or offset + (7 if rotation else 12) > len(basis):
            raise ValueError('Sparse field overlap or unconfigured vertex field')
        if not np.isfinite(vector).all():
            raise ValueError('Nonfinite sparse payload')
        if rotation and (abs(np.linalg.norm(vector[:3])-1) > .001 or not 0 <= vector[3] <= np.pi):
            raise ValueError('Invalid author rotation')
        previous = offset


def build_part(part, capture, export):
    model = pinned(part['model'])
    if len(model) < 96 or model[:4] != b'MOD\0' or struct.unpack_from('<H', model, 4)[0] != 237:
        raise ValueError('Unsupported MOD3 model')
    vertex_start = struct.unpack_from('<Q', model, 80)[0]
    length = struct.unpack_from('<I', model, 24)[0]
    basis = model[vertex_start:vertex_start+length]
    if len(basis) != length or not basis or length > 64*1024*1024:
        raise ValueError('Invalid model vertex buffer')
    mapping = json.loads(pinned(part['mapping']))
    references = source_objects(json.loads(pinned(part['reference_capture'])))
    objects = {o['name']: o for o in capture['objects']}
    spans, reports = [], []
    position_fields, normal_fields = set(), set()
    used_meshes = set()
    mesh_start = struct.unpack_from('<Q', model, 72)[0]
    mesh_count, material_count = struct.unpack_from('<HH', model, 8)
    materials = struct.unpack_from('<Q', model, 64)[0]
    for mapped in mapping['objects']:
        if not mapped['fully_resolved'] or mapped['mesh'] in used_meshes:
            raise ValueError('Mapping unresolved or duplicate mesh')
        used_meshes.add(mapped['mesh'])
        obj, old = objects[mapped['object']], references[mapped['object']]
        for actual, expected in ((obj['basis'], old['basis']), (obj['matrix_world'], old['matrix'])):
            if not np.array_equal(np.asarray(actual), np.asarray(expected)):
                raise ValueError(f'Mapped source geometry/transform changed: {obj["name"]}')
        if 'fingerprint' in old and obj['legacy_fingerprint'] != old['fingerprint']:
            raise ValueError('Mapped source keys/normal encoding or baseline values changed')
        if 'uv' in old and (obj['uv'] != old['uv'] or obj['loop_vertices'] != old['loops']
                            or dict((v[0], v[1]) for v in obj['legacy_fingerprint']['values']) != old['values']):
            raise ValueError('Mapped arm topology/UV or baseline values changed')
        for key in part['keys']:
            name = key['source_key']
            if name in old['keys']:
                if name not in obj['selected_keys'] or not np.array_equal(obj['selected_keys'][name], old['keys'][name]):
                    raise ValueError('Mapped key endpoint changed')
        index = mapped['mesh']
        if type(index) is not int or not 0 <= index < mesh_count:
            raise ValueError('Mesh index outside model')
        row = model[mesh_start+index*80:mesh_start+(index+1)*80]
        if len(row) != 80:
            raise ValueError('Truncated mesh metadata')
        count, material = struct.unpack_from('<H', row, 2)[0], struct.unpack_from('<H', row, 6)[0]
        if material >= material_count:
            raise ValueError('Invalid material index')
        matname = model[materials+material*128:materials+(material+1)*128].split(b'\0')[0].decode('utf-8')
        if matname != mapped['material']:
            raise ValueError('Mapping material changed')
        stride = row[14]
        # Supported observed MOD3 layouts all place xyz/normal/tangent/first UV at 0/12/16/20.
        layouts = {0xa756f2f9:24, 0xa5104ca0:28, 0xc3ac03a1:36, 0xc9690ab8:40,
                   0x818904dc:28, 0x0f06033f:32, 0xf637401c:32, 0xf471fe45:36,
                   0x3c730760:36, 0xb2fc0083:40, 0x81f58067:40, 0x83b33e3e:44,
                   0x366995a7:44, 0xb8e69244:48}
        if layouts.get(struct.unpack_from('<I', row, 24)[0]) != stride:
            raise ValueError('Unsupported vertex layout')
        start = struct.unpack_from('<I', row, 20)[0] + stride*sum(struct.unpack_from('<I', row, o)[0] for o in (16, 36))
        indices = np.asarray(mapped['disk_to_blender_vertex'])
        if indices.dtype.kind not in 'iu' or len(indices) != count or np.any(indices < 0) or np.any(indices >= len(obj['basis'])):
            raise ValueError('Mapping vertex index invalid')
        if start+count*stride > len(basis) or any(start < b and a < start+count*stride for a, b in spans):
            raise ValueError('Mesh vertex buffer span overlaps or exceeds basis')
        spans.append((start, start+count*stride))
        transform = export @ np.asarray(obj['matrix_world'])
        points = np.asarray(obj['evaluated_positions']) @ transform[:3, :3].T + transform[:3, 3]
        disk = np.ndarray((count, 3), dtype='<f4', buffer=basis, offset=start, strides=(stride, 4))
        error = np.linalg.norm(points[indices]-disk, axis=1)
        if not np.isfinite(error).all() or error.max() > .001:
            raise ValueError('Evaluated rest baseline no longer matches model')
        uv_sets = {}
        for vi, uv in zip(obj['loop_vertices'], obj['uv']):
            uv_sets.setdefault(vi, set()).add(uv_bytes(uv))
        for i, vi in enumerate(indices):
            offset = start+i*stride
            if basis[offset+20:offset+24] not in uv_sets.get(int(vi), set()):
                raise ValueError('Mapped UV changed')
            position_fields.add(offset)
            normal_fields.add(offset+12)
        reports.append({'object': obj['name'], 'mesh': index, 'vertices': count,
                        'position_max_error_game_units': float(error.max()), 'exact_mapped_uv': True})
    rotations = pinned(part['author_rotations'])
    records_checked(rotations, 20, basis, normal_fields, True)
    files = {'basis.bin': basis, 'author-rotations.bin': rotations}
    keys = []
    for key_index, key in enumerate(part['keys']):
        records = []
        for mapped in mapping['objects']:
            obj = objects[mapped['object']]
            name = key['source_key']
            if name not in obj['selected_keys']:
                continue
            metadata = next(k for k in obj['keys'] if k['name'] == name)
            if metadata['relative'] != obj['basis_key'] or metadata['vertex_group'] or metadata['mute']:
                raise ValueError('Only unmasked relative-to-basis keys supported')
            if metadata['value'] != 0:
                raise ValueError('Export key currently nonzero')
            delta = (np.asarray(obj['selected_keys'][name])-np.asarray(obj['basis'])) @ (export @ np.asarray(obj['matrix_world']))[:3, :3].T
            delta = delta[np.asarray(mapped['disk_to_blender_vertex'])].astype('<f4')
            row = model[mesh_start+mapped['mesh']*80:mesh_start+(mapped['mesh']+1)*80]
            stride = row[14]
            start = struct.unpack_from('<I', row, 20)[0]+stride*sum(struct.unpack_from('<I', row, o)[0] for o in (16, 36))
            for i in np.flatnonzero(np.any(delta != 0, axis=1)):
                records.append((start+int(i)*stride, *map(float, delta[i])))
        raw = b''.join(struct.pack('<I3f', *r) for r in sorted(records))
        if not raw:
            raise ValueError('Key has no mapped deformation')
        if key.get('source_delta_reference') and raw != pinned(key['source_delta_reference']):
            raise ValueError('Source delta differs from companion/shading reference')
        if key.get('validated_positions'):
            if not key.get('source_delta_reference'):
                raise ValueError('Companion requires exact source delta reference')
            raw = pinned(key['validated_positions'])
        records_checked(raw, 16, basis, position_fields)
        filename = f'positions-{key_index}.bin'
        files[filename] = raw
        keys.append({'id': key['id'], 'minimum': 0, 'maximum': 1, 'exclusive_group': 0,
                     'author_shading': key['author_shading'], 'positions': filename,
                     'position_count': len(raw)//16, 'positions_sha256': digest(raw)})
    manifest = {'schema': 'mhw-equipment-morph-package-v1', 'abi_version': 65536,
                'part': part['part'], 'model_id': part['model_id'], 'model_sha256': digest(model),
                'basis': 'basis.bin', 'basis_bytes': len(basis), 'basis_sha256': digest(basis),
                'shading': {'mode': 'shared-author-intent-max', 'rotations': 'author-rotations.bin',
                            'rotation_count': len(rotations)//20, 'rotations_sha256': digest(rotations)},
                'keys': keys, 'mapping_validation': reports,
                'validation': {'source_unchanged': True, 'game_runtime_validated_keys': [],
                               'combined_runtime_validated': False},
                'notes': 'Explicit pinned mapping/author rotations; no new visual acceptance inferred by export.'}
    return files, manifest


def export_job(job_path, capture_path, output):
    output = Path(output)
    if output.exists():
        raise ValueError('Refusing to overwrite output')
    job = load_job(job_path)
    capture = read_json(capture_path)
    if (capture['schema'] != 'mhw-equipment-morph-job-capture-v2' or not capture['source_state_unchanged']
            or capture['scene_mutated'] or capture['job_sha256'] != digest(Path(job_path).read_bytes())):
        raise ValueError('Invalid job capture')
    export = np.asarray(json.loads(pinned(job['export_reference']))['export_matrix'], dtype=np.float64)
    if export.shape != (4, 4) or not np.isfinite(export).all() or not np.allclose(export[3], [0, 0, 0, 1]):
        raise ValueError('Invalid export transform')
    pending = [(part, *build_part(part, capture, export)) for part in job['parts']]
    output.mkdir(parents=True, exist_ok=False)
    catalog = {'schema': 'mhw-equipment-morph-native-catalog-v1', 'abi_version': 65536,
               'game_installed': False, 'native_bindings_generalized': False, 'assets': []}
    for index, (part, files, manifest) in enumerate(pending):
        folder = output / part['part']
        folder.mkdir()
        for name, raw in files.items():
            (folder/name).write_bytes(raw)
        manifest['provenance'] = {'job_sha256': capture['job_sha256'], 'capture_sha256': digest(Path(capture_path).read_bytes()),
                                  'mapping_sha256': part['mapping']['sha256'], 'author_rotations_sha256': part['author_rotations']['sha256']}
        path = folder/'manifest.json'
        path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
        packed, _, inputs = prepare(path)
        name = f'asset-{index}.mhmpack'
        (output/name).write_bytes(packed)
        catalog['assets'].append({'part': part['part'], 'model_id': part['model_id'], 'model_sha256': part['model']['sha256'],
                                  'pack': name, 'bytes': len(packed), 'sha256': digest(packed),
                                  'keys': [k['id'] for k in manifest['keys']], 'inputs': inputs})
    (output/'catalog.mhmcatalog').write_bytes(encode_catalog(catalog))
    (output/'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2), encoding='utf-8')
    return catalog
