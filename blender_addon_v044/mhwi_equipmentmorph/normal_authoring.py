"""Preserve authored normal intent; do not re-encode the source or deform keys."""
import hashlib
import importlib
import json
import math
import struct
from pathlib import Path
import numpy as np
from .backend.capture_equipment_morph_job import fingerprint
from .backend.equipment_morph_export_pipeline import uv_bytes
from .backend.map_equipment_morph_job import model_meshes

def unit(values):
    values=np.asarray(values,dtype=np.float64);length=np.linalg.norm(values,axis=-1)
    if not np.isfinite(values).all() or np.any(length<1e-12):raise ValueError('法线为零或包含非有限值')
    return values/length[...,None]
def angle(a,b):return math.acos(float(np.clip(np.dot(unit(a),unit(b)),-1,1)))
def rotate(v,axis,radians):return v*math.cos(radians)+np.cross(axis,v)*math.sin(radians)+axis*np.dot(axis,v)*(1-math.cos(radians))
def rotation_record(offset,normal,tangent,target):
    source=unit(normal);target=unit(target);radians=angle(source,target)
    axis=np.cross(source,target);length=np.linalg.norm(axis)
    if radians<=1e-5:return None,normal,tangent
    if length<1e-8:raise ValueError('作者法线与模型法线完全反向，需要明确的旋转依据')
    record=struct.pack('<I4f',offset,*(axis/length),radians)
    _,x,y,z,theta=struct.unpack('<I4f',record);axis=np.array([x,y,z])
    packed=[np.rint(rotate(v,axis,theta)).clip(-128,127).astype(np.int8) for v in (normal,tangent)]
    changed=not np.array_equal(packed[0],normal.astype(np.int8)) or not np.array_equal(packed[1],tangent.astype(np.int8))
    return record if changed else None,packed[0],packed[1]
def choose_loop(possible,base_normals,author_normals,packed_normal):
    if not possible:raise ValueError('法线角点与已映射顶点/UV没有对应关系')
    errors=[angle(packed_normal,base_normals[i]) for i in possible];best=min(errors)
    tied=[i for i,error in zip(possible,errors) if error<=best+1e-10]
    # A tie is harmless only if every tied corner has the same author target.
    if any(angle(author_normals[tied[0]],author_normals[i])>1e-5 for i in tied[1:]):
        raise ValueError('法线角点有多个不同作者目标，不能自动选择')
    return min(tied)
def toolkit():
    import bpy
    names=[a.module for a in bpy.context.preferences.addons if 'toolkit' in a.module.lower()]
    matches=[]
    for name in names:
        try:
            root=importlib.import_module(name);module=importlib.import_module(name+'.core.normal_utils')
            if all(hasattr(module,n) for n in ['decode_at','_intent_field','INTENT_ATTR']):matches.append((root,module))
        except ImportError:continue
    if len(matches)!=1:raise ValueError('需要唯一启用的兼容 MOD Toolkit 法线模块')
    root,module=matches[0];version=tuple(root.bl_info.get('version',()))
    if version!=(2,8,0):raise ValueError('自动作者法线目前适配 MOD Toolkit 2.8.0')
    return module,{'addon_module':root.__name__,'addon_version':version,'normal_module_sha256':hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest()}
def source_state(obj):
    me=obj.data;raw=np.empty(len(me.vertices)*3,np.float32);me.vertices.foreach_get('co',raw)
    intent=me.attributes.get('mhw_normal_intent');intent_sha=None
    if intent:
        if intent.domain!='CORNER' or intent.data_type!='FLOAT_VECTOR':raise ValueError('作者法线属性类型或域不兼容')
        values=np.empty(len(me.loops)*3,np.float32);intent.data.foreach_get('vector',values);intent_sha=hashlib.sha256(values.tobytes()).hexdigest()
    return {'source':fingerprint(obj),'vertex_sha256':hashlib.sha256(raw.tobytes()).hexdigest(),
        'intent_sha256':intent_sha,'attributes':[(a.name,a.data_type,a.domain) for a in me.attributes],'hidden':obj.hide_get()}
def capture_object(obj,export,module):
    import bpy
    me=obj.data
    intent=module._intent_field(me)
    if intent is not None:local=intent;origin='stored_author_intent'
    else:
        if me.attributes.get('custom_normal') is None:raise ValueError('网格没有作者自定义法线：'+obj.name)
        keys=me.shape_keys
        block=keys.key_blocks.get('TK_Basis') if keys else None
        block=block or (keys.reference_key if keys else None)
        data=block.data if block else me.vertices;coords=np.empty(len(data)*3,np.float32);data.foreach_get('co',coords)
        local=module.decode_at(me,coords.reshape(-1,3));origin='frozen_basis' if block and block.name=='TK_Basis' else 'source_basis'
    transform=np.asarray(export)@np.asarray(obj.matrix_world);normal_matrix=np.linalg.inv(transform[:3,:3]).T
    author=unit(np.asarray(local)@normal_matrix.T)
    evaluated=obj.evaluated_get(bpy.context.evaluated_depsgraph_get());temporary=evaluated.to_mesh(preserve_all_data_layers=True,depsgraph=bpy.context.evaluated_depsgraph_get())
    try:
        if len(temporary.loops)!=len(me.loops) or len(temporary.vertices)!=len(me.vertices):raise ValueError('法线采集的求值拓扑发生变化')
        base=unit(np.asarray([tuple(n.vector) for n in temporary.corner_normals])@normal_matrix.T)
    finally:evaluated.to_mesh_clear()
    if author.shape!=(len(me.loops),3) or base.shape!=author.shape:raise ValueError('作者法线角点数量不符')
    if not me.uv_layers.active:raise ValueError('作者法线采集需要UV')
    return {'object':obj.name,'origin':origin,'author_normals':author.tolist(),'baseline_normals':base.tolist(),
            'loop_vertices':[l.vertex_index for l in me.loops],'uv':[list(u.uv) for u in me.uv_layers.active.data]}
def generate(asset,mapping,export):
    import bpy
    wanted={b['object'] for b in asset['bindings'] if bpy.data.objects[b['object']].data.shape_keys
            and any(k['author_shading'] and k['source_key'] in bpy.data.objects[b['object']].data.shape_keys.key_blocks for k in asset['keys'])}
    if not wanted:raise ValueError('请选择参与作者法线的动态键')
    if mapping.get('position_equivalence'):raise ValueError('位置字段重合证明不能用于生成法线')
    objects=[bpy.data.objects[n] for n in sorted(wanted)];before={o.name:source_state(o) for o in objects}
    counts=(len(bpy.data.scenes),len(bpy.data.objects),len(bpy.data.meshes),len(bpy.data.shape_keys));dirty=bpy.data.is_dirty
    module,provenance=toolkit()
    try:captured={o.name:capture_object(o,export,module) for o in objects}
    finally:
        if before!={o.name:source_state(o) for o in objects} or counts!=(len(bpy.data.scenes),len(bpy.data.objects),len(bpy.data.meshes),len(bpy.data.shape_keys)) or dirty!=bpy.data.is_dirty:
            raise ValueError('作者法线采集后源数据或临时数据数量不一致')
    model=Path(asset['model']).read_bytes();basis,meshes=model_meshes(model);records=[];reports=[];offsets=set()
    for mapped in mapping['objects']:
        if mapped['object'] not in wanted:continue
        row=captured[mapped['object']];mesh=meshes[mapped['mesh']];base=np.asarray(row['baseline_normals']);author=np.asarray(row['author_normals'])
        loops={}
        for i,(vertex,uv) in enumerate(zip(row['loop_vertices'],row['uv'])):loops.setdefault((vertex,uv_bytes(uv)),[]).append(i)
        errors=[];chosen=[];changed=0
        for disk,source_vertex in enumerate(mapped['disk_to_blender_vertex']):
            position=mesh['start']+disk*mesh['stride'];offset=position+12
            if offset in offsets:raise ValueError('作者法线字段重叠')
            offsets.add(offset);normal=np.array(struct.unpack_from('<3b',basis,offset),float);tangent=np.array(struct.unpack_from('<3b',basis,offset+4),float)
            loop=choose_loop(loops.get((source_vertex,basis[position+20:position+24]),[]),base,author,normal);chosen.append(loop)
            record,packed_normal,_=rotation_record(offset,normal,tangent,author[loop])
            error=math.degrees(angle(packed_normal,author[loop]))
            if error>1:raise ValueError('作者法线量化误差超过1度：'+mapped['object'])
            errors.append(error)
            if record:records.append((offset,record));changed+=1
        reports.append({'object':mapped['object'],'mesh':mapped['mesh'],'vertices':len(chosen),'records':changed,
            'maximum_target_error_degrees':max(errors),'source_loops':chosen,'target_origin':row['origin']})
    raw=b''.join(value for _,value in sorted(records))
    report={'project_name':'MHWI-EquipmentMorph','schema':'mhwi-author-normal-intent-v1','mode':'shared-author-intent-max',
        'rotation_sha256':hashlib.sha256(raw).hexdigest(),'rotation_records':len(records),'objects':reports,
        'toolkit':provenance,'source_fingerprints':before,'source_unchanged':True,'temporary_data_removed':True,
        'blend_saved':False,'game_installed':False,'normal_padding_and_tangent_sign_preserved':True,
        'limitations':'Static authored direction transport; no per-key geometric normal recalculation or tangent regeneration.'}
    return raw,report,{'objects':list(captured.values()),'source_unchanged':True}
