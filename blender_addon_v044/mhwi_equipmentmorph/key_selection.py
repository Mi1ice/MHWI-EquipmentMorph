"""Candidate discovery and export-only confirmation, independent of the UI list filter."""
from .capacity import MAX_DYNAMIC_KEYS

def objects(asset,direct):
    import bpy
    if direct:
        if not asset.collection:raise ValueError('请选择MOD3集合')
        return [o for o in asset.collection.all_objects if o.type=='MESH']
    result=[bpy.data.objects.get(binding.object_name) for binding in asset.bindings]
    if any(not o or o.type!='MESH' for o in result):raise ValueError('参与网格不存在')
    return result

def controllable(key):
    return (key.id_data.use_relative and key.value==0 and not key.mute and not key.vertex_group
            and key.relative_key==key.id_data.reference_key)

def discover(asset,direct):
    sources={}
    for obj in objects(asset,direct):
        if obj.data.shape_keys:
            for key in obj.data.shape_keys.key_blocks:
                if key!=obj.data.shape_keys.reference_key:sources.setdefault(key.name,[]).append(key)
    return sources

def confirmed(asset,direct):
    selected=[key for key in asset.keys if key.enabled]
    if not selected:raise ValueError('请先使用选中形态键，确认需要导出的键')
    if len(selected)>MAX_DYNAMIC_KEYS:raise ValueError(f'每个模型最多确认{MAX_DYNAMIC_KEYS}个动态键')
    sources=discover(asset,direct);aliases=set()
    for key in selected:
        if key.source not in sources:raise ValueError('已确认形态键已缺失：'+key.source)
        if not all(controllable(k) for k in sources[key.source]):raise ValueError('已确认形态键当前不能导出：'+key.source)
        if not 0<len(key.public_id)<64 or not all(32<=ord(c)<=126 for c in key.public_id):
            raise ValueError('接口名称须为1至63个可打印ASCII字符：'+key.source)
        if key.public_id in aliases:raise ValueError('接口名称重复：'+key.public_id)
        aliases.add(key.public_id)
    return selected

def confirm(asset,direct):
    chosen=[key for key in asset.keys if key.selected]
    if not chosen:raise ValueError('请勾选需要使用的形态键')
    sources=discover(asset,direct)
    if any(k.source not in sources or not all(controllable(v) for v in sources[k.source]) for k in chosen):
        raise ValueError('选中项含缺失、非零或不支持的形态键，请刷新后重新选择')
    if len({k.source for k in asset.keys if k.enabled or k.selected})>MAX_DYNAMIC_KEYS:raise ValueError(f'每个模型最多确认{MAX_DYNAMIC_KEYS}个动态键')
    added=sum(not key.enabled for key in chosen)
    for key in chosen:key.enabled=True;key.selected=False
    return added
