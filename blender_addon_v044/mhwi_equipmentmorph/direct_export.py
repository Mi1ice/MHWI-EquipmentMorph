"""Paired export through the installed, pinned MHW Model Editor 0.12.

Source indices are attached ONLY to exporter clones before UV/sharp splitting.
The mapping is taken from the serializer's mesh order, never geometric matching.
"""
import importlib
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import numpy as np
from .capacity import MAX_DYNAMIC_KEYS
from .authoring import pin,write
from .backend.equipment_morph_export_pipeline import digest,load_job
from .backend.equipment_morph_package_pipeline import export_pipeline,package_id,PARTS
from .backend.map_equipment_morph_job import model_meshes
from .output_files import model_resource_id,retarget_model,publish,checked_output,PublicationCleanupError

TAG='mhwi_em_source_vertex'

def editor_module():
    import bpy
    candidates=[m for n,m in sys.modules.items() if n.endswith('.modules.mod3.blender_mod3')]
    if len(candidates)!=1:raise ValueError('请启用已验证的 MHW Model Editor 0.12')
    module=candidates[0];root=importlib.import_module(module.__name__.split('.addons.')[0])
    if root.bl_info['version']!=(0,12):raise ValueError('MHW Model Editor 版本尚未验证')
    receipt=json.loads(Path(__file__).with_name('direct_export_adapter.json').read_bytes())
    if list(bpy.app.version) not in receipt['blender_versions']:raise ValueError('当前Blender版本尚未通过联合导出验证')
    base=Path(module.__file__).parents[1]
    for name,expected in receipt['sources'].items():
        if digest((base/name).read_bytes())!=expected:raise ValueError('导出器源码已改变，需要重新验证：'+name)
    return module

def preflight(asset):
    import bpy
    from . import shape_preview
    if shape_preview.running():raise ValueError('请先结束形态键预览，再进行联合导出')
    collection=asset['collection']
    if asset['part'] not in PARTS:raise ValueError('无效装备部位')
    if not collection or collection.library:raise ValueError('请选择本地 MOD3 集合')
    if asset['shading'] not in ('BASELINE','GEOMETRIC'):raise ValueError('联合导出支持基准法线或逐键几何法线')
    if asset.get('undefined_normal_policy','REJECT') not in ('REJECT','BASELINE'):raise ValueError('无效的无定义几何法线策略')
    if asset.get('antiparallel_normal_policy','REJECT') not in ('REJECT','BASELINE'):raise ValueError('无效的反向端点法线策略')
    model_resource_id(asset['model_id'])
    package_id(asset['package'])
    load_job_validation={'schema':'mhw-equipment-morph-export-job-v2','parts':[dict(part=asset['part'],keys=asset['keys'])]}
    # Same key/part validator used by the existing package pipeline.
    if not asset['keys'] or len(asset['keys'])>MAX_DYNAMIC_KEYS:raise ValueError(f'请选择1至{MAX_DYNAMIC_KEYS}个动态键')
    names=[k['source_key'] for k in asset['keys']]
    if len(set(names))!=len(names):raise ValueError('源形态键重复')
    if any(k.get('author_shading') for k in asset['keys']):raise ValueError('联合导出不使用共享作者旋转数据')
    objects=[o for o in collection.all_objects if o.type=='MESH'];controlled=[];found=set()
    if not objects:raise ValueError('集合没有网格')
    for obj in objects:
        mesh=obj.data;shape=mesh.shape_keys
        if obj.library or mesh.library or obj.animation_data or mesh.animation_data or obj.constraints or (shape and shape.animation_data):
            raise ValueError('动画、约束或链接对象需要专门适配：'+obj.name)
        if obj.show_only_shape_key or (shape and not shape.use_relative):raise ValueError('需要相对形态键和正常混合显示：'+obj.name)
        if any(m.show_viewport and m.type!='ARMATURE' for m in obj.modifiers):raise ValueError('活动修改器仅支持静止骨架：'+obj.name)
        for mod in obj.modifiers:
            if mod.show_viewport and mod.type=='ARMATURE':
                rig=mod.object
                if not rig or rig.animation_data or rig.constraints or any(b.constraints for b in rig.pose.bones):raise ValueError('骨架动画或约束不受支持')
                if any(max(abs(b.matrix[r][c]-b.bone.matrix_local[r][c]) for r in range(4) for c in range(4))>1e-7 for b in rig.pose.bones):raise ValueError('骨架必须处于静止姿态')
        selected=[shape.key_blocks.get(n) for n in names] if shape else []
        selected=[k for k in selected if k]
        found.update(k.name for k in selected)
        for key in selected:
            if key.value!=0 or key.mute or key.vertex_group or key.relative_key!=shape.reference_key:raise ValueError('动态键须为零、未静音、相对Basis且无顶点组：'+key.name)
        if selected:controlled.append(obj)
    missing=set(names)-found
    if missing:raise ValueError('已选动态键在集合中缺失：'+', '.join(sorted(missing)))
    if not controlled:raise ValueError('集合中没有选定的动态键')
    return objects,controlled,load_job_validation

def export_model(module,asset,path):
    import bpy
    from .backend.capture_equipment_morph_job import fingerprint,coords
    from .geometric_normals import transported,AntiparallelDirection
    from .normal_authoring import unit
    from . import authored_shading
    authored_shading.native()
    objects,controlled,_=preflight(asset)
    normal_policy=asset.get('undefined_normal_policy','REJECT')
    antiparallel_policy=asset.get('antiparallel_normal_policy','REJECT')
    if bpy.context.mode!='OBJECT':raise ValueError('联合导出需要对象模式')
    if bpy.data.collections.get('clonedMod3Meshes'):raise ValueError('导出器临时集合已存在，请先处理该集合')
    source_before={o.name:fingerprint(o) for o in objects}
    selection=list(bpy.context.selected_objects);active=bpy.context.view_layer.objects.active
    last=bpy.context.scene.mhw_mod3_toolpanel.lastExportCollection
    existing={kind:set(getattr(bpy.data,kind)) for kind in ('meshes','armatures')}
    clones=[];traces={};current=[];ordered=[];shading_states={}
    originals={n:getattr(module,n) for n in ('createCloneMesh','parseVertexDict','buildMod3File','splitSharpEdges')}
    def clone(obj,*args):
        result=originals['createCloneMesh'](obj,*args);clones.append(result);current[:]=[obj,result]
        data=result.data
        if len(data.vertices)!=len(obj.data.vertices) or [tuple(p.vertices) for p in data.polygons]!=[tuple(p.vertices) for p in obj.data.polygons]:raise ValueError('求值拓扑或顶点次序改变：'+obj.name)
        if data.attributes.get(TAG):raise ValueError('源网格包含保留的追溯属性：'+obj.name)
        attr=data.attributes.new(TAG,'INT','POINT');attr.data.foreach_set('value',np.arange(len(data.vertices),dtype=np.int32))
        state=authored_shading.capture(data);state['source_object']=obj;shading_states[result.as_pointer()]=state
        return result
    def sharp(clone_obj,*args):
        state=shading_states[clone_obj.as_pointer()]
        try:
            originals['splitSharpEdges'](clone_obj,*args)
            if any(len(face.vertices)!=3 for face in clone_obj.data.polygons):module.triangulateMesh(clone_obj)
            authored_shading.prepare(clone_obj,state,module.exportMatrix @ state['source_object'].matrix_world,TAG)
        except Exception as error:
            # The pinned editor catches sharp-edge errors. Retain the failure
            # so parse cannot continue with an incomplete shading preparation.
            state['error']=error
            raise

    def parse(vertex_dict,info,*args):
        obj,clone_obj=current;data=clone_obj.data;attr=data.attributes.get(TAG)
        if not attr or attr.domain!='POINT' or attr.data_type!='INT':raise ValueError('拆点丢失来源属性')
        indices=np.empty(len(data.vertices),np.int32);attr.data.foreach_get('value',indices)
        if np.any(indices<0) or np.any(indices>=len(obj.data.vertices)):raise ValueError('拆点来源越界')
        state=shading_states[clone_obj.as_pointer()]
        if 'error' in state:raise ValueError('作者角点准备失败：'+str(state['error'])) from state['error']
        if 'desired' not in state or len(args)!=7:raise ValueError('作者角点未准备或序列化接口不匹配')
        authored,tangent,geometry_corners=authored_shading.serialize(data,state)
        adjusted=list(args);adjusted[2]=authored;adjusted[3]=tangent
        original_result=originals['parseVertexDict'](vertex_dict,info,*adjusted)
        if len(vertex_dict['Position'])!=len(indices):raise ValueError('导出顶点次序不完整')
        trace=dict(object=obj.name,indices=indices.tolist(),positions=np.asarray(vertex_dict['Position']).tolist(),normals={},
                   source_corners=state['source_corner_indices'][geometry_corners].tolist(),authored_shading=state['report'])
        if asset['shading']=='GEOMETRIC' and obj in controlled:
            trace['normal_mesh']=state['geometry_mesh'].copy()
            trace['normal_mesh'].transform(module.exportMatrix @ obj.matrix_world)
            trace['geometry_source_indices']=state['geometry_source_indices']
            trace['geometry_corners']=geometry_corners
            trace['source_object']=obj
        traces[id(info)]=trace
        return original_result
    def capture_normals(trace):
        obj=trace.pop('source_object');copy=trace.pop('normal_mesh');indices=trace.pop('geometry_source_indices')
        chosen=trace.pop('geometry_corners').tolist()
        try:
            # Clearing the storage, rather than setting zero custom vectors, avoids
            # decoding authored normal-space data as endpoint geometric normals.
            custom=copy.attributes.get('custom_normal')
            if custom:copy.attributes.remove(custom)
            copy.update()
            def geometric():
                values=np.asarray([tuple(n.vector) for n in copy.corner_normals],dtype=np.float64)
                if not np.isfinite(values).all():raise ValueError('几何法线包含非有限值：'+obj.name)
                return unit(values)[chosen] if normal_policy=='REJECT' else values[chosen]
            base=geometric();trace['normal_corners']=chosen
            positions=np.asarray([tuple(v.co) for v in copy.vertices])
            transform=np.asarray(module.exportMatrix @ obj.matrix_world)[:3,:3]
            for key in asset['keys']:
                block=obj.data.shape_keys.key_blocks.get(key['source_key'])
                if block is None:continue
                delta=(coords(block)-coords(obj.data.shape_keys.reference_key)).astype(np.float64)@transform.T
                copy.vertices.foreach_set('co',(positions+delta[indices]).astype(np.float32).ravel());copy.update()
                endpoint=geometric()
                trace['normals'][key['id']]={'base':base.tolist(),'endpoint':endpoint.tolist()}
        finally:bpy.data.meshes.remove(copy)
    def build(model,*args):
        result=originals['buildMod3File'](model,*args)
        for meshes in result.meshDict.values():
            for mesh in meshes:ordered.append(traces[id(mesh.meshInfo)])
        return result
    try:
        module.createCloneMesh=clone;module.parseVertexDict=parse;module.buildMod3File=build;module.splitSharpEdges=sharp
        options=dict(targetCollection=asset['collection'],selectedOnly=False,visibleOnly=False,exportAllLODs=False,
            useBlenderMaterialName=asset.get('use_blender_material_name',False),exportBoundingBoxes=False,
            autoSolveRepeatedUVs=True,preserveSharpEdges=True,invisibleMantlesModFix=True)
        if not module.exportMHWMod3File(str(path),options):raise ValueError('MOD3 导出器报告失败')
        for trace in ordered:
            if 'normal_mesh' in trace:capture_normals(trace)
    finally:
        for name,original in originals.items():setattr(module,name,original)
        if bpy.context.mode!='OBJECT':bpy.ops.object.mode_set(mode='OBJECT')
        for clone_obj in clones:
            if clone_obj in bpy.data.objects.values():bpy.data.objects.remove(clone_obj,do_unlink=True)
        temporary=bpy.data.collections.get('clonedMod3Meshes')
        if temporary:bpy.data.collections.remove(temporary)
        for kind,before in existing.items():
            for data in set(getattr(bpy.data,kind))-before:
                if data.users==0:getattr(bpy.data,kind).remove(data)
        bpy.ops.object.select_all(action='DESELECT')
        for obj in selection:obj.select_set(True)
        bpy.context.view_layer.objects.active=active;bpy.context.scene.mhw_mod3_toolpanel.lastExportCollection=last
        if source_before!={o.name:fingerprint(o) for o in objects}:raise ValueError('导出期间源网格数据改变')
    model=path.read_bytes();basis,rows=model_meshes(model)
    if len(rows)!=len(ordered):raise ValueError('序列化网格数量与追溯数量不同')
    mapping=[];serialized=[];normal_records={k['id']:[] for k in asset['keys']};seen=set();fallbacks=[]
    for trace,row in zip(ordered,rows):
        count=row['count'];disk=np.ndarray((count,3),dtype='<f4',buffer=basis,offset=row['start'],strides=(row['stride'],4))
        if count!=len(trace['indices']) or not np.array_equal(disk,np.asarray(trace['positions'],dtype='<f4')):raise ValueError('序列化顶点与追溯次序不同')
        serialized.append(dict(object=trace['object'],mesh=row['mesh'],start=row['start'],stride=row['stride'],disk_to_blender_vertex=trace['indices'],disk_to_source_corner=trace['source_corners'],authored_shading=trace['authored_shading']))
        if trace['object'] not in {o.name for o in controlled}:continue
        if trace['object'] in seen:raise ValueError('同一源对象被多个LOD重复导出，当前不支持')
        seen.add(trace['object'])
        mapping.append(dict(object=trace['object'],mesh=row['mesh'],material=row['material'],fully_resolved=True,disk_to_blender_vertex=trace['indices']))
        for key,values in trace['normals'].items():
            for i,(base,target) in enumerate(zip(values['base'],values['endpoint'])):
                offset=row['start']+i*row['stride']+12
                normal=unit(struct.unpack_from('<3b',basis,offset))
                base_zero=np.linalg.norm(base)<1e-12;target_zero=np.linalg.norm(target)<1e-12
                reason=None
                if base_zero or target_zero:
                    if normal_policy!='BASELINE':raise ValueError('几何法线无定义：'+trace['object'])
                    reason='baseline_and_endpoint_zero' if base_zero and target_zero else ('baseline_zero' if base_zero else 'endpoint_zero')
                else:
                    try:direction=transported(normal,base,target)
                    except AntiparallelDirection:
                        if antiparallel_policy!='BASELINE':raise
                        reason='antiparallel_endpoint'
                if reason:
                    fallbacks.append(dict(object=trace['object'],mesh=row['mesh'],key_id=key,source_vertex=trace['indices'][i],
                                          serialized_vertex=i,corner=trace['normal_corners'][i],normal_offset=offset,reason=reason))
                    continue
                if np.linalg.norm(direction-normal)>1e-5:normal_records[key].append((offset,*(direction-normal)))
    if seen!={o.name for o in controlled}:raise ValueError('部分动态网格未由导出器输出')
    normal_data={key:b''.join(struct.pack('<I3f',*record) for record in sorted(records)) for key,records in normal_records.items()}
    return dict(schema='mhwi-direct-export-trace-v1',part=asset['part'],model_id=asset['model_id'],
        model_sha256=digest(model),fully_resolved=True,objects=mapping,exporter='MHW Model Editor 0.12',
        mapping_method='clone-point-index-through-serializer',authored_shading_method='source-corner-normals-final-UV-MikkTSpace',geometric_corner_domain='before-shading-seam-split',export_matrix=[list(r) for r in module.exportMatrix],serialized_objects=serialized,
        undefined_normal_policy=normal_policy,antiparallel_normal_policy=antiparallel_policy,geometric_normal_fallbacks=fallbacks),normal_data

def export_direct(recipe,output,debug=False,trace_callback=None):
    from .backend.capture_equipment_morph_job import capture
    output=checked_output(output);assets=[dict(a,model_id=retarget_model(a['model_id'],a.get('equipment_id',''))) for a in recipe['assets']]
    for a in assets:a['package']=a['model_id'].rsplit('/',1)[-1]
    if output.exists() and not output.is_dir():raise ValueError('输出位置必须是文件夹')
    if not 0<len(assets)<=64 or len({a['model_id'].casefold() for a in assets})!=len(assets):raise ValueError('模型资源路径重复或超过64个')
    module=editor_module()
    for asset in assets:preflight(asset)
    output.parent.mkdir(parents=True,exist_ok=True)
    stage_parent=output if output.is_dir() else output.parent
    stage=Path(tempfile.mkdtemp(prefix='morph-direct-stage-',dir=stage_parent))
    committed=False;publishing_final=False
    try:
        work=stage/'authoring';work.mkdir();empty=work/'baseline_rotations.bin';empty.write_bytes(b'')
        for asset in assets:
            _,_,validation=preflight(asset)
            check=work/(asset['part']+'.keys.json');write(check,validation);load_job(check)
        parts=[];normal_data={};models=[];fallbacks=[]
        for index,asset in enumerate(assets):
            work=stage/'authoring'/str(index);work.mkdir()
            empty=work/'baseline_rotations.bin';empty.write_bytes(b'')
            model=stage/'nativePC'/(asset['model_id']+'.mod3');model.parent.mkdir(parents=True,exist_ok=True)
            if model.exists():raise ValueError('两个部位使用同一输出模型路径')
            trace,normals=export_model(module,asset,model);mapping=work/(asset['part']+'.trace.json');write(mapping,trace)
            fallbacks.extend(dict(item,model_id=asset['model_id']) for item in trace['geometric_normal_fallbacks'])
            if trace_callback is not None:trace_callback(index,asset,trace)
            matrix=work/(asset['part']+'.coordinates.json');write(matrix,{'export_matrix':trace['export_matrix']})
            part=dict(part=asset['part'],model_id=asset['model_id'],model=pin(model),mapping=pin(mapping),keys=asset['keys'],author_rotations=pin(empty))
            job=dict(schema='mhw-equipment-morph-export-job-v2',export_reference=pin(matrix),parts=[part])
            path=work/(asset['part']+'.job.json');write(path,job);load_job(path)
            reference=work/(asset['part']+'.reference.json');capture(path,reference)
            part['reference_capture']=pin(reference);write(path,job)
            current=work/(asset['part']+'.capture.json');capture(path,current)
            parts.append(dict(job=pin(path),capture=pin(current),packages={asset['part']:asset['package']},mesh_contracts={asset['part']:{'mode':'serialized'}}))
            if asset['shading']=='GEOMETRIC':normal_data[asset['model_id']]=normals
            models.append(dict(part=asset['part'],file=model.relative_to(stage).as_posix(),sha256=trace['model_sha256'],meshes=len(trace['objects']),vertices=sum(len(o['disk_to_blender_vertex']) for o in trace['objects'])))
        job=work/'package_job.json';write(job,{'schema':'mhw-equipment-morph-package-job-v1','exports':parts})
        result=export_pipeline(job,stage/'generated',normal_data)
        publish(stage/'generated/nativePC',stage/'nativePC')
        shutil.rmtree(stage/'generated/nativePC')
        result.update(success=True,models=models,mapping_method='same-export-source-indices',source_data_unchanged=True,
            # Exporter creates/removes datablocks; Blender may mark the scene dirty.
            scene_mutated=True,blend_saved=False,formal_acceptance_complete=False,geometric_normal_fallbacks=fallbacks)
        write(stage/'result.json',result)
        # Preserve every hashed receipt byte. Absolute paths record the transient
        # run location; users repeat the paired export from the Blender collection.
        write(work/'README.json',{'replayable':False,'staging_root':str(stage),
            'reason':'archived paired-run evidence; files are now under the output directory',
            'source_indices_retained':True})
        publishing_final=True
        if debug:publish(stage,output)
        else:publish(stage/'nativePC',output/'nativePC')
        committed=True
        return result
    except PublicationCleanupError as error:
        if publishing_final:committed=error.committed;raise
        raise RuntimeError('阶段文件写入完成但未发布最终输出；清理失败，资料保留在 '+str(stage)) from error
    finally:
        if stage.exists():
            if stage.parent!=stage_parent or not stage.name.startswith('morph-direct-stage-'):raise ValueError('临时输出清理范围错误')
            # Nested publication can retain the only real recovery bytes here.
            # Never erase them in the outer cleanup after rollback failed.
            if not any(stage.rglob('morph-output-backup-*')):
                try:shutil.rmtree(stage)
                except OSError as cleanup:
                    message='导出文件已发布；临时资料清理未完成：' if committed else '导出未完成；临时资料清理未完成：'
                    raise RuntimeError(message+str(stage)) from cleanup
