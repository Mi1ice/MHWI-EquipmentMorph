"""Stream evaluated Blender reference cases beside a paired export. Never saves .blend."""
import argparse
import hashlib
import importlib
import json
import math
from pathlib import Path
import shutil
import sys
import numpy as np
import bpy

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'blender_addon_v044'))
from mhwi_equipmentmorph import direct_export
from mhwi_equipmentmorph.backend.capture_equipment_morph_job import fingerprint
from mhwi_equipmentmorph.backend.map_equipment_morph_job import model_meshes
from mhwi_equipmentmorph.backend.prepare_equipment_morph_catalog import unique_object


def sha_file(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def capture_reference(recipe, output):
    output = Path(output).resolve()
    if output.exists():
        raise ValueError('Refusing to overwrite reference output')
    assets = []
    for source in recipe['assets']:
        asset = dict(source)
        if not isinstance(asset['collection'], str) or not bpy.data.collections.get(asset['collection']):
            raise ValueError('Missing named collection')
        asset['collection'] = bpy.data.collections[asset['collection']]
        direct_export.preflight(asset)
        ids = [k['id'] for k in asset['keys']]
        if len(set(ids)) != len(ids):
            raise ValueError('Duplicate public key identifier')
        assets.append(asset)
    output.mkdir(parents=True)
    write(output / 'recipe.json', recipe)
    traces = {}
    exported = direct_export.export_direct(dict(recipe, assets=assets), output / 'export',
        debug=recipe.get('debug', False), trace_callback=lambda index, asset, trace: traces.__setitem__(index, (asset, trace)))
    receipts = []
    for index in range(len(assets)):
        asset, trace = traces[index]
        folder = output / 'references' / str(index)
        folder.mkdir(parents=True)
        model = output / 'export/nativePC' / (asset['model_id'] + '.mod3')
        pack = output / 'export/nativePC' / (asset['model_id'] + '.morph/asset.mhmpack')
        profile = output / 'export/nativePC' / (asset['model_id'] + '.morph/model.mhmprofile')
        shutil.copyfile(model, folder / 'model.mod3')
        shutil.copyfile(profile, folder / 'model.mhmprofile')
        write(folder / 'trace.json', trace)
        basis, rows = model_meshes(model.read_bytes())
        offsets = sorted(row['start'] + i * row['stride'] for row in rows for i in range(row['count']))
        if not offsets or len(offsets) != len(set(offsets)) or any(p + 12 > len(basis) for p in offsets):
            raise ValueError('Serialized position coverage invalid')
        objects = {entry['object']: bpy.data.objects[entry['object']] for entry in trace['serialized_objects']}
        before = {name: fingerprint(obj) for name, obj in objects.items()}
        dynamic = []
        for obj in objects.values():
            if obj.data.shape_keys:
                for key in asset['keys']:
                    block = obj.data.shape_keys.key_blocks.get(key['source_key'])
                    if block is not None:
                        dynamic.append((block, key['id'], float(block.value)))
        cases = [dict(id='baseline', kind='baseline', weights={})]
        for key in asset['keys']:
            for weight in (0, .25, .5, .75, 1):
                cases.append(dict(id=key['id'] + '@' + str(weight), kind='single', key_id=key['id'],
                                  weights={key['id']: weight}))
        cases.extend(recipe.get('cases', []))
        case_ids = set()
        public_ids = {k['id'] for k in asset['keys']}
        for case in cases:
            if not isinstance(case['id'], str) or case['id'] in case_ids or not isinstance(case['weights'], dict):
                raise ValueError('Invalid or duplicate reference case')
            case_ids.add(case['id'])
            for name, weight in case['weights'].items():
                if name not in public_ids or type(weight) not in (int, float) or not math.isfinite(weight) or not 0 <= weight <= 1:
                    raise ValueError('Invalid reference case weight')
        export = np.asarray(trace['export_matrix'], dtype=np.float64)
        if export.shape != (4, 4) or not np.isfinite(export).all():
            raise ValueError('Invalid export matrix')
        matrices = {name: export @ np.asarray(obj.matrix_world, dtype=np.float64) for name, obj in objects.items()}
        try:
            with (folder / 'positions.bin').open('xb') as stream:
                for case in cases:
                    for block, name, _ in dynamic:
                        block.value = case['weights'].get(name, 0)
                    bpy.context.view_layer.update()
                    graph = bpy.context.evaluated_depsgraph_get();graph.update()
                    evaluated = {}
                    for name, obj in objects.items():
                        value = obj.evaluated_get(graph)
                        mesh = value.to_mesh()
                        try:
                            if len(mesh.vertices) != len(obj.data.vertices):
                                raise ValueError('Evaluated topology changed')
                            points = np.empty(len(mesh.vertices) * 3, dtype=np.float64)
                            mesh.vertices.foreach_get('co', points)
                            transform = matrices[name]
                            evaluated[name] = points.reshape((-1, 3)) @ transform[:3, :3].T + transform[:3, 3]
                        finally:
                            value.to_mesh_clear()
                    aligned = {}
                    for entry in trace['serialized_objects']:
                        points = evaluated[entry['object']]
                        for i, source_index in enumerate(entry['disk_to_blender_vertex']):
                            if type(source_index) is not int or not 0 <= source_index < len(points):
                                raise ValueError('Source trace index out of bounds')
                            aligned[entry['start'] + i * entry['stride']] = points[source_index]
                    values = np.asarray([aligned[p] for p in offsets], dtype='<f8')
                    if not np.isfinite(values).all():
                        raise ValueError('Nonfinite evaluated position')
                    case['byte_offset'] = stream.tell()
                    stream.write(values.tobytes())
        finally:
            for block, _, original in dynamic:
                block.value = original
            bpy.context.view_layer.update()
        if before != {name: fingerprint(obj) for name, obj in objects.items()}:
            raise ValueError('Reference capture did not restore source')
        reference = dict(schema='mhwi-morph-position-reference-v1', model_id=asset['model_id'],
            model_file='model.mod3', model_sha256=sha_file(model), pack_sha256=sha_file(pack),
            profile_file='model.mhmprofile', profile_sha256=sha_file(profile),
            trace_file='trace.json', trace_sha256=sha_file(folder / 'trace.json'),
            positions_file='positions.bin', positions_sha256=sha_file(folder / 'positions.bin'),
            export_matrix=trace['export_matrix'], source_matrices={name: matrix.tolist() for name, matrix in matrices.items()},
            serialized_objects=trace['serialized_objects'], key_mapping=asset['keys'], position_offsets=offsets,
            cases=cases, source_restored=True, blend_saved=False, tolerance_set=False)
        write(folder / 'reference.json', reference)
        receipts.append(dict(reference=str(folder / 'reference.json'), sha256=sha_file(folder / 'reference.json'),
                             pack=str(pack), keys=len(asset['keys']), positions=len(offsets), cases=len(cases)))
    result = dict(schema='mhwi-paired-reference-capture-v1', references=receipts, export=exported,
                  source_restored=True, blend_saved=False, game_tested=False)
    write(output / 'capture-receipt.json', result)
    return result


def main():
    args = sys.argv[sys.argv.index('--') + 1:]
    parser = argparse.ArgumentParser()
    parser.add_argument('--recipe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    options = parser.parse_args(args)
    # Use the same installed and hash-pinned exporter as the established fixture.
    if not any(name.endswith('.modules.mod3.blender_mod3') for name in sys.modules):
        editor = importlib.import_module('MHW_Model_Editor-main')
        editor.register()
        importlib.import_module('MHW_Model_Editor-main.addons.MHW_Model_Editor.modules.mod3.blender_mod3')
    recipe = json.loads(options.recipe.read_bytes(), object_pairs_hook=unique_object)
    result = capture_reference(recipe, options.output)
    print(json.dumps(dict(references=result['references']), ensure_ascii=False))


if __name__ == '__main__':
    main()
