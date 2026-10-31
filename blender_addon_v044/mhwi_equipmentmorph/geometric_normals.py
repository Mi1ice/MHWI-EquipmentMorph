"""Endpoint geometric direction transport on disposable evaluated mesh copies.

Fixed nonzero source keys remain baked in evaluated positions. Authored baseline
normal directions are transported by each key's geometric change, not replaced
with raw geometry normals. No source datablock, key value or file is modified.
"""
import hashlib
import math
import struct
from pathlib import Path
import numpy as np
from .normal_authoring import angle,rotate,source_state,unit
from .backend.equipment_morph_export_pipeline import uv_bytes
from .backend.map_equipment_morph_job import model_meshes

class AntiparallelDirection(ValueError):
    """The endpoint directions do not define a unique transport axis."""

def transported(normal,base,target):
    base=unit(base);target=unit(target);cosine=float(np.clip(np.dot(base,target),-1,1))
    if cosine<-.999999:raise AntiparallelDirection('几何法线端点反向；请使用作者法线模式或修正端点')
    axis=np.cross(base,target);sine=np.linalg.norm(axis)
    if sine<1e-12:return unit(normal)
    return unit(rotate(unit(normal),axis/sine,math.atan2(sine,cosine)))

def capture_object(obj,keys,export):
    import bpy
    if bpy.context.mode!='OBJECT':raise ValueError('逐键法线采集需要物体模式')
    depsgraph=bpy.context.evaluated_depsgraph_get();evaluated=obj.evaluated_get(depsgraph)
    temporary=evaluated.to_mesh(preserve_all_data_layers=True,depsgraph=depsgraph);copy=None
    try:
        source=obj.data
        if len(temporary.vertices)!=len(source.vertices) or len(temporary.loops)!=len(source.loops):raise ValueError('求值拓扑改变')
        transform=np.asarray(export)@np.asarray(obj.matrix_world);normal_matrix=np.linalg.inv(transform[:3,:3]).T
        baseline_authored=unit(np.asarray([tuple(n.vector) for n in temporary.corner_normals])@normal_matrix.T)
        coordinates=np.empty(len(temporary.vertices)*3,np.float32);temporary.vertices.foreach_get('co',coordinates)
        copy=temporary.copy()
        copy.normals_split_custom_set([(0.,0.,0.)]*len(copy.loops));copy.update()
        geometric=unit(np.asarray([tuple(n.vector) for n in copy.corner_normals])@normal_matrix.T)
        endpoints={}
        blocks=source.shape_keys.key_blocks if source.shape_keys else {}
        for key in keys:
            block=blocks.get(key['source_key'])
            if block is None:continue
            if block.value!=0 or block.mute or block.vertex_group or block.relative_key!=source.shape_keys.reference_key:raise ValueError('动态键必须为零、未静音、相对基准且无顶点组')
            endpoint=np.empty(len(block.data)*3,np.float32);block.data.foreach_get('co',endpoint)
            basis=np.empty_like(endpoint);block.relative_key.data.foreach_get('co',basis)
            copy.vertices.foreach_set('co',coordinates+endpoint-basis);copy.update()
            endpoints[key['id']]=unit(np.asarray([tuple(n.vector) for n in copy.corner_normals])@normal_matrix.T)
        return {'authored':baseline_authored,'geometric':geometric,'endpoints':endpoints,
            'loops':[l.vertex_index for l in source.loops],'uv':[list(u.uv) for u in source.uv_layers.active.data]}
    finally:
        if copy is not None:bpy.data.meshes.remove(copy)
        evaluated.to_mesh_clear()

def generate(asset,mapping,export):
    import bpy
    if mapping.get('position_equivalence'):raise ValueError('位置重合证明不能用于逐键法线')
    objects=[bpy.data.objects[b['object']] for b in asset['bindings']]
    before={o.name:source_state(o) for o in objects}
    state=lambda:(bpy.data.filepath,bpy.data.is_dirty,bpy.context.mode,bpy.context.scene.frame_current,
        bpy.context.active_object.name if bpy.context.active_object else None,sorted(o.name for o in bpy.context.selected_objects),
        len(bpy.data.scenes),len(bpy.data.objects),len(bpy.data.meshes),len(bpy.data.shape_keys))
    initial=state()
    try:captured={o.name:capture_object(o,asset['keys'],export) for o in objects}
    finally:
        if initial!=state() or before!={o.name:source_state(o) for o in objects}:raise ValueError('采集后源数据、界面状态或临时数量改变')
    basis,meshes=model_meshes(Path(asset['model']).read_bytes());records={k['id']:{} for k in asset['keys']};reports=[]
    for mapped in mapping['objects']:
        row=captured[mapped['object']];mesh=meshes[mapped['mesh']];loops={}
        for i,(vertex,uv) in enumerate(zip(row['loops'],row['uv'])):loops.setdefault((vertex,uv_bytes(uv)),[]).append(i)
        counts={key:0 for key in records};maximum=0.
        for disk,source_vertex in enumerate(mapped['disk_to_blender_vertex']):
            position=mesh['start']+disk*mesh['stride'];offset=position+12
            normal=unit(struct.unpack_from('<3b',basis,offset));possible=loops.get((source_vertex,basis[position+20:position+24]),[])
            if not possible:raise ValueError('逐键法线角点缺少已证明的顶点/UV对应')
            errors=[angle(normal,row['authored'][i]) for i in possible];best=min(errors)
            tied=[i for i,error in zip(possible,errors) if error<=best+1e-10];chosen=min(tied)
            for key,target in row['endpoints'].items():
                candidates=[transported(normal,row['geometric'][i],target[i]) for i in tied]
                if any(angle(candidates[0],v)>1e-5 for v in candidates[1:]):raise ValueError('角点存在不同逐键法线目标，无法自动选择')
                target=transported(normal,row['geometric'][chosen],target[chosen]);radians=angle(normal,target);maximum=max(maximum,math.degrees(radians))
                if radians<=1e-5:continue
                if offset in records[key]:raise ValueError('逐键法线字段重叠')
                records[key][offset]=struct.pack('<I3f',offset,*(target-normal));counts[key]+=1
        reports.append({'object':mapped['object'],'mesh':mapped['mesh'],'mapped_vertices':len(mapped['disk_to_blender_vertex']),
            'normal_records':counts,'maximum_endpoint_rotation_degrees':maximum})
    raw={key:b''.join(value for _,value in sorted(items.items())) for key,items in records.items()}
    report={'project_name':'MHWI-EquipmentMorph','schema':'mhwi-per-key-normal-endpoints-v1',
        'mode':'per-key-direction-deltas','endpoint_method':'transport-authored-baseline-by-geometric-change',
        'keys':{key:{'records':len(value)//16,'sha256':hashlib.sha256(value).hexdigest()} for key,value in raw.items()},
        'objects':reports,'source_fingerprints':before,'source_unchanged':True,'temporary_data_removed':True,
        'blend_saved':False,'game_installed':False,'formal_acceptance_complete':False,
        'limitations':'Normalized endpoint blend approximation; tangent direction transport, no UV tangent regeneration.'}
    return raw,report
