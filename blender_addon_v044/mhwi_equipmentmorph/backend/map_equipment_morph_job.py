"""Read-only MOD3 correspondence from a pinned Blender capture, positions and UV."""
import argparse
import itertools
import json
import math
from pathlib import Path
import struct
import numpy as np
from .equipment_morph_export_pipeline import ROOT, PARTS, digest, pinned, read_json, uv_bytes

from .vertex_formats import FORMATS
LAYOUTS = {code: f['stride'] for code,f in FORMATS.items()}
NEIGHBORS = tuple(itertools.product((-1, 0, 1), repeat=3))


def span(raw, start, count):
    if not 0 <= start <= start+count <= len(raw):
        raise ValueError('Model span outside file')
    return raw[start:start+count]


def model_meshes(raw):
    if len(raw) < 96 or raw[:4] != b'MOD\0' or struct.unpack_from('<H', raw, 4)[0] != 237:
        raise ValueError('Unsupported model version')
    mesh_count, material_count = struct.unpack_from('<HH', raw, 8)
    material_start, mesh_start, vertex_start = (struct.unpack_from('<Q', raw, p)[0] for p in (64,72,80))
    names = [span(raw, material_start+i*128, 128).split(b'\0')[0].decode('utf-8') for i in range(material_count)]
    basis = span(raw, vertex_start, struct.unpack_from('<I', raw, 24)[0])
    if not basis or len(basis) > 64*1024*1024:
        raise ValueError('Invalid vertex buffer bound')
    meshes = []
    for i in range(mesh_count):
        row = span(raw, mesh_start+i*80, 80)
        count, mat = struct.unpack_from('<H', row, 2)[0], struct.unpack_from('<H', row, 6)[0]
        if mat >= len(names):
            raise ValueError('Invalid model material')
        stride = row[14]
        start = struct.unpack_from('<I', row, 20)[0]+stride*sum(struct.unpack_from('<I', row, p)[0] for p in (16,36))
        meshes.append({'mesh': i, 'material': names[mat], 'count': count, 'stride': stride,
                       'format': struct.unpack_from('<I', row, 24)[0], 'start': start})
    return basis, meshes


def solve_vertices(source_positions, loop_vertices, source_uv, disk_positions, disk_uv, tolerance):
    """Select the unique exact-UV candidate among tied closest positions inside tolerance."""
    source = np.asarray(source_positions, dtype=np.float64)
    disk = np.asarray(disk_positions, dtype=np.float64)
    if (not math.isfinite(tolerance) or not 0 < tolerance <= .001
            or source.ndim != 2 or source.shape[1] != 3 or not 0 < len(source) <= 200000
            or disk.ndim != 2 or disk.shape[1] != 3 or len(disk) != len(disk_uv)
            or not np.isfinite(source).all() or not np.isfinite(disk).all()
            or np.max(np.abs(source)) > 1e9 or (len(disk) and np.max(np.abs(disk)) > 1e9)):
        raise ValueError('Invalid coordinates or position tolerance')
    if len(loop_vertices) != len(source_uv):
        raise ValueError('Loop/UV count differs')
    uv_sets = {}
    for vi, uv in zip(loop_vertices, source_uv):
        if type(vi) is not int or not 0 <= vi < len(source):
            raise ValueError('Invalid loop vertex index')
        uv_sets.setdefault(vi, set()).add(uv_bytes(uv))
    cells = np.floor(source/tolerance).astype(np.int64)
    grid = {}
    for i, cell in enumerate(cells):
        grid.setdefault(tuple(map(int, cell)), []).append(i)
    mapping, issues, coincidences = [], [], 0
    max_error = 0.
    for i, position in enumerate(disk):
        cell = tuple(map(int, np.floor(position/tolerance)))
        candidates = []
        for shift in NEIGHBORS:
            candidates.extend(grid.get(tuple(a+b for a,b in zip(cell, shift)), ()))
        if candidates:
            distances = np.sum((source[candidates]-position)**2, axis=1)
            minimum = float(distances.min())
        else:
            minimum = math.inf
        if minimum > tolerance*tolerance:
            mapping.append(None)
            issues.append({'disk_vertex': i, 'reason': 'position_outside_tolerance'})
            continue
        max_error = max(max_error, math.sqrt(minimum))
        tied = [vi for vi, distance in zip(candidates, distances)
                if float(distance) <= tolerance*tolerance and abs(float(distance)-minimum) <= 1e-12]
        exact = [vi for vi in tied if disk_uv[i] in uv_sets.get(vi, ())]
        coincidences += len(tied) > 1
        if len(exact) != 1:
            mapping.append(None)
            issues.append({'disk_vertex': i, 'reason': 'ambiguous_exact_uv' if exact else 'no_exact_uv',
                           'position_candidates': tied[:64], 'position_candidate_count': len(tied),
                           'uv_candidates': exact[:64], 'uv_candidate_count': len(exact)})
        else:
            mapping.append(exact[0])
    return {'disk_to_blender_vertex': mapping, 'fully_resolved': not issues,
            'coordinate_max_error_game_units': max_error if len(issues) < len(disk) else None,
            'spatial_coincidences': int(coincidences), 'unresolved_count': len(issues),
            'unresolved_examples': issues[:32]}


def generate(job_path, output):
    if output.exists():
        raise ValueError('Refusing to overwrite correspondence output')
    job = read_json(job_path)
    if job['schema'] != 'mhw-equipment-morph-mapping-job-v1':
        raise ValueError('Unsupported mapping job')
    tolerance = job['position_tolerance_game_units']
    if not isinstance(tolerance, (int, float)) or isinstance(tolerance, bool) or not math.isfinite(tolerance) or not 0 < tolerance <= .001:
        raise ValueError('Tolerance must not exceed observed 0.001 limit')
    capture = json.loads(pinned(job['capture']))
    if (capture['schema'] != 'mhw-equipment-morph-job-capture-v2' or not capture['source_state_unchanged']
            or capture['scene_mutated']):
        raise ValueError('Invalid source capture')
    objects = {o['name']: o for o in capture['objects']}
    if len(objects) != len(capture['objects']):
        raise ValueError('Duplicate source objects')
    export = np.asarray(json.loads(pinned(job['export_reference']))['export_matrix'], dtype=np.float64)
    if export.shape != (4,4) or not np.isfinite(export).all() or not np.array_equal(export[3], [0,0,0,1]):
        raise ValueError('Invalid export transform')
    if not 0 < len(job['parts']) <= 5 or len({p['part'] for p in job['parts']}) != len(job['parts']):
        raise ValueError('Duplicate or invalid equipment parts')
    pending = []
    for part in job['parts']:
        if part['part'] not in PARTS or not part['objects']:
            raise ValueError('Invalid configured part/object list')
        basis, meshes = model_meshes(pinned(part['model']))
        used, spans, records, selection_issues = set(), [], [], []
        for config in part['objects']:
            obj = objects[config['object']]
            candidates = [m for m in meshes if m['material'] == config['material']]
            if 'mesh' in config:
                if type(config['mesh']) is not int:
                    raise ValueError('Explicit mesh index must be integer')
                candidates = [m for m in candidates if m['mesh'] == config['mesh']]
            if len(candidates) != 1:
                selection_issues.append({'object': obj['name'], 'material': config['material'],
                                         'reason': 'mesh_selection_not_unique', 'candidate_meshes': [m['mesh'] for m in candidates]})
                continue
            mesh = candidates[0]
            if mesh['mesh'] in used:
                raise ValueError('Two configured objects claim same mesh')
            used.add(mesh['mesh'])
            if LAYOUTS.get(mesh['format']) != mesh['stride'] or not mesh['count']:
                raise ValueError('Unsupported selected vertex layout')
            start, end = mesh['start'], mesh['start']+mesh['count']*mesh['stride']
            span(basis, start, end-start)
            if any(start < b and a < end for a,b in spans):
                raise ValueError('Selected mesh vertex spans overlap')
            spans.append((start,end))
            transform = export @ np.asarray(obj['matrix_world'], dtype=np.float64)
            if transform.shape != (4,4) or not np.isfinite(transform).all() or abs(np.linalg.det(transform[:3,:3])) < 1e-12:
                raise ValueError('Invalid source transform')
            points = np.asarray(obj['evaluated_positions']) @ transform[:3,:3].T+transform[:3,3]
            positions = np.ndarray((mesh['count'],3), dtype='<f4', buffer=basis, offset=start, strides=(mesh['stride'],4))
            uvs = [basis[start+i*mesh['stride']+20:start+i*mesh['stride']+24] for i in range(mesh['count'])]
            solved = solve_vertices(points, obj['loop_vertices'], obj['uv'], positions, uvs, tolerance)
            records.append({'object': obj['name'], 'mesh': mesh['mesh'], 'material': mesh['material'],
                            'disk_vertices': mesh['count'], 'stride': mesh['stride'], 'vertex_buffer_offset': start,
                            'position_tolerance_game_units': tolerance, **solved})
        # Older mapped exporters inspect each record, so selection failure must
        # also prevent them from accepting only the successfully selected subset.
        if selection_issues:
            for record in records:
                record['vertex_mapping_resolved'] = record['fully_resolved']
                record['fully_resolved'] = False
        pending.append({'schema': 'mhw-equipment-morph-correspondence-v1', 'part': part['part'],
                        'model_id': part['model_id'], 'model_sha256': part['model']['sha256'],
                        'capture_sha256': job['capture']['sha256'], 'export_reference_sha256': job['export_reference']['sha256'],
                        'objects': records, 'selection_issues': selection_issues,
                        'fully_resolved': not selection_issues and all(r['fully_resolved'] for r in records),
                        'method': 'Nearest position inside tolerance; equal squared distances within 1e-12; exactly one candidate with legacy half(u),half(1-half(v)). No guessed tie-breaking.',
                        'scene_mutated': False, 'blend_saved': False, 'runtime_binding_verified': False})
    success = all(p['fully_resolved'] for p in pending)
    output.mkdir(parents=True, exist_ok=False)
    for part in pending:
        (output/f'{part["part"]}.mapping.json').write_text(json.dumps(part, ensure_ascii=False, indent=2), encoding='utf-8')
    report = {'schema': 'mhw-equipment-morph-mapping-results-v1', 'success': success,
              'job_sha256': digest(job_path.read_bytes()), 'source_state_unchanged': True,
              'game_installed': False, 'parts': [{'part': p['part'], 'fully_resolved': p['fully_resolved'],
                'objects': len(p['objects']), 'unresolved_vertices': sum(o['unresolved_count'] for o in p['objects']),
                'selection_issues': p['selection_issues']} for p in pending]}
    (output/'results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--job', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = generate(args.job, args.output)
    print(json.dumps(result, ensure_ascii=False))
    raise SystemExit(0 if result['success'] else 2)
