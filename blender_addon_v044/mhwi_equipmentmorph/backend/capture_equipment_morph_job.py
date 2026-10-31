"""Run capture(job_path, output) in Blender. Read-only meshes, no operators/save."""
import json
from pathlib import Path
import bpy
import numpy as np
from .equipment_morph_export_pipeline import digest, load_job, pinned


def coords(block):
    result = np.empty(len(block.data)*3, dtype=np.float32)
    block.data.foreach_get('co', result)
    return result.reshape(-1, 3)


def fingerprint(obj):
    mesh = obj.data
    blocks = mesh.shape_keys.key_blocks if mesh.shape_keys else []
    chunks = [coords(k).tobytes() for k in blocks]
    if mesh.attributes.get('custom_normal'):
        data = np.empty(len(mesh.loops)*2, dtype=np.int16)
        mesh.attributes['custom_normal'].data.foreach_get('value', data)
        chunks.append(data.tobytes())
    return {'geometry_and_encoding_sha256': digest(b''.join(chunks)),
            'values': [[k.name, k.value, k.mute] for k in blocks],
            'matrix': [list(row) for row in obj.matrix_world]}


def capture(job_path, output):
    job_path, output = Path(job_path), Path(output)
    if output.exists() or bpy.context.mode != 'OBJECT':
        raise ValueError('Capture requires object mode and fresh output')
    job = load_job(job_path)
    keys_by_object = {}
    for part in job['parts']:
        for item in json.loads(pinned(part['mapping']))['objects']:
            keys_by_object.setdefault(item['object'], set()).update(k['source_key'] for k in part['keys'])
    objects = [bpy.data.objects[name] for name in keys_by_object]
    def state():
        return (bpy.data.filepath, bpy.data.is_dirty, bpy.context.scene.frame_current,
                bpy.context.mode, bpy.context.active_object.name if bpy.context.active_object else None,
                sorted(o.name for o in bpy.context.selected_objects),
                len(bpy.data.objects), len(bpy.data.meshes), len(bpy.data.shape_keys), len(bpy.data.scenes))
    before = {o.name: fingerprint(o) for o in objects}
    ui_before = state()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    rows = []
    for obj in objects:
        mesh = obj.data
        if obj.type != 'MESH' or obj.animation_data or mesh.animation_data or (mesh.shape_keys and mesh.shape_keys.animation_data):
            raise ValueError('Animated/non-mesh source requires separate capture adapter')
        for modifier in obj.modifiers:
            if modifier.show_viewport and modifier.type != 'ARMATURE':
                raise ValueError('Active topology/deformation modifier unsupported')
            if modifier.type == 'ARMATURE' and modifier.show_viewport:
                rig = modifier.object
                if not rig or rig.animation_data or any(b.constraints for b in rig.pose.bones):
                    raise ValueError('Animated/constrained armature unsupported')
                if any(max(abs(b.matrix[r][c]-b.bone.matrix_local[r][c]) for r in range(4) for c in range(4)) > 1e-7 for b in rig.pose.bones):
                    raise ValueError('Armature is not in rest pose')
        blocks = mesh.shape_keys.key_blocks if mesh.shape_keys else []
        if mesh.shape_keys and not mesh.shape_keys.use_relative:
            raise ValueError('Absolute keys unsupported')
        basis_block = mesh.shape_keys.reference_key if mesh.shape_keys else None
        basis = coords(basis_block).tolist() if basis_block else [list(v.co) for v in mesh.vertices]
        selected, metadata = {}, []
        for name in sorted(keys_by_object[obj.name]):
            key = mesh.shape_keys.key_blocks.get(name) if mesh.shape_keys else None
            if key:
                selected[name] = coords(key).tolist()
                metadata.append({'name': name, 'relative': key.relative_key.name, 'vertex_group': key.vertex_group,
                                 'value': key.value, 'mute': key.mute})
        if not mesh.uv_layers:
            raise ValueError('Mapped mesh has no UV1')
        evaluated = obj.evaluated_get(depsgraph)
        temporary = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=depsgraph)
        try:
            if len(temporary.vertices) != len(mesh.vertices) or len(temporary.loops) != len(mesh.loops):
                raise ValueError('Evaluated topology changed')
            positions = [list(v.co) for v in temporary.vertices]
        finally:
            evaluated.to_mesh_clear()
        rows.append({'name': obj.name, 'basis_key': basis_block.name if basis_block else None,
                     'basis': basis, 'selected_keys': selected, 'keys': metadata,
                     'matrix_world': [list(row) for row in obj.matrix_world], 'evaluated_positions': positions,
                     'loop_vertices': [loop.vertex_index for loop in mesh.loops],
                     'uv': [list(uv.uv) for uv in mesh.uv_layers[0].data], 'legacy_fingerprint': before[obj.name]})
    if ui_before != state() or before != {o.name: fingerprint(o) for o in objects}:
        raise ValueError('Source/UI state changed during capture')
    report = {'schema': 'mhw-equipment-morph-job-capture-v2', 'job_sha256': digest(job_path.read_bytes()),
              'blender_version': bpy.app.version_string, 'blend_filepath': bpy.data.filepath,
              'source_state_unchanged': True, 'scene_mutated': False, 'blend_saved': False,
              'objects': rows}
    with output.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, ensure_ascii=False)
    return {'output': str(output), 'objects': len(rows), 'source_state_unchanged': True, 'blend_saved': False}
