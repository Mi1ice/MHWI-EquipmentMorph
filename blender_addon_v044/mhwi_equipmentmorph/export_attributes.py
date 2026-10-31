"""Check the pinned editor's source capacities without changing source data."""
import numpy as np
from .backend.vertex_formats import FORMATS
from .backend.equipment_morph_export_pipeline import uv_bytes

def preflight(collection,objects):
    rigs=[o for o in collection.all_objects if o.type=='ARMATURE']
    if len(rigs)>1:raise ValueError('固定导出器只支持一个骨架：'+collection.name)
    bones={b.name for b in rigs[0].data.bones} if rigs else set()
    for obj in objects:
        mesh=obj.data;uvs=len(mesh.uv_layers);colors=list(mesh.color_attributes)
        def reject(reason):raise ValueError(obj.name+'：'+reason)
        if not uvs:reject('缺少第0层UV（导出UV1）')
        if len(colors)>1:reject('多个颜色属性无法由固定导出器完整保存')
        if colors and (colors[0].domain!='CORNER' or colors[0].data_type!='BYTE_COLOR' or len(mesh.vertex_colors)!=1):reject('固定导出器仅支持角点 BYTE_COLOR 颜色属性')
        if uvs>2 and (rigs or not colors):reject('第3／4层UV仅支持无骨架且有颜色的布局')
        if uvs>4:reject('固定导出器最多支持4层UV')
        for layer in mesh.uv_layers:
            try:
                for item in layer.data:uv_bytes(item.uv)
            except (ValueError,OverflowError):reject('UV层 '+layer.name+' 无法编码为有限半精度数')
        valid={g.index for g in obj.vertex_groups if g.name in bones}
        orphan={g.index:g.name for g in obj.vertex_groups if g.name.startswith('MhBone_') and g.name not in bones}
        maximum=0
        for vertex in mesh.vertices:
            missing=[orphan[g.group] for g in vertex.groups if g.group in orphan and g.weight>=.001]
            if missing:reject('骨骼顶点组 '+', '.join(missing)+' 没有对应骨骼，固定导出器无法保存其权重')
            count=sum(g.group in valid and g.weight>=.001 for g in vertex.groups)
            if count>8:reject('顶点 '+str(vertex.index)+' 超过8个有效骨骼权重（现有阈值0.001）')
            maximum=max(maximum,count)
        if rigs and not maximum:reject('骨架集合中的网格没有有效骨骼权重')
        slots=(4 if maximum<=4 else 8) if rigs else 0
        if not any(f['uv_count']==uvs and f['weight_slots']==slots and f['color']==bool(colors) for f in FORMATS.values()):reject('UV／颜色／骨骼组合没有可保留的常规布局')

def verify_uv_color(obj,basis,row,corners):
    """Compare every serialized UV/color against its traced source corner."""
    f=FORMATS.get(row['format'])
    if not f or f['stride']!=row['stride'] or f['uv_count']!=len(obj.data.uv_layers) or f['color']!=bool(obj.data.color_attributes):raise ValueError('序列化布局与源属性不一致：'+obj.name)
    fields={item['field']:item for item in f['fields']}
    for i,corner in enumerate(corners):
        at=row['start']+i*row['stride']
        for u,layer in enumerate(obj.data.uv_layers):
            offset=at+fields['UV'+str(u+1)]['offset']
            if basis[offset:offset+4]!=uv_bytes(layer.data[corner].uv):raise ValueError('序列化UV通道不一致：'+obj.name+'／'+layer.name)
        if f['color']:
            offset=at+fields['Color']['offset'];color=obj.data.vertex_colors[0].data[corner].color
            expected=(np.asarray(color,dtype=np.float32)*255).astype('u1').tobytes()
            if basis[offset:offset+4]!=expected:raise ValueError('序列化颜色不一致：'+obj.name)
