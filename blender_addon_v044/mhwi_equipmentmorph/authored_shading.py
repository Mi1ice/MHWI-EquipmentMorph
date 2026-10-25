"""Preserve evaluated author corner directions through the pinned editor.

Only disposable exporter meshes are tagged or split. MikkTSpace receives author
normals directly, avoiding Blender custom-normal space re-encoding altogether.
"""
import ctypes
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

CORNER='mhwi_em_author_corner'
FINAL='mhwi_em_shading_corner'
VERTEX='mhwi_em_shading_vertex'
NATIVE=Path(__file__).with_name('_native')/'build'
MAX_CORNERS=3000000

def _unit(values):
    values=np.asarray(values,dtype=np.float64)
    lengths=np.linalg.norm(values,axis=1)
    if not np.isfinite(values).all() or np.any(lengths<1e-12):raise ValueError('作者法线或切线无定义')
    return values/lengths[:,None]

@lru_cache(maxsize=1)
def native():
    try:
        if sys.platform!='win32' or ctypes.sizeof(ctypes.c_void_p)!=8:raise ValueError('作者计算组件只支持Windows x64')
        manifest=json.loads((NATIVE/'build.json').read_bytes());name=manifest['file']
        if manifest.get('schema')!='mhwi-author-shading-native-v1' or manifest['abi']!=1 or manifest.get('platform')!='windows-x64' or manifest.get('blender')!='5.1.2':raise ValueError('计算组件标识不匹配')
        digest=manifest['sha256']
        if len(digest)!=64 or any(c not in '0123456789abcdef' for c in digest) or name!='mhm_author_shading-'+digest+'.dll':raise ValueError('计算组件文件名不匹配')
        path=NATIVE/name
        if hashlib.sha256(path.read_bytes()).hexdigest()!=manifest['sha256']:raise ValueError('计算组件摘要不匹配')
        library=ctypes.CDLL(str(path.resolve()),winmode=0x900)
        library.MhmAuthorShadingVersion.restype=ctypes.c_uint
        if library.MhmAuthorShadingVersion()!=1:raise ValueError('计算组件接口不匹配')
        function=library.MhmAuthorTangents;pointer=ctypes.POINTER(ctypes.c_float)
        function.argtypes=[ctypes.c_uint,pointer,pointer,pointer,pointer];function.restype=ctypes.c_int
        return library,function,manifest['sha256']
    except (OSError,KeyError,TypeError,AttributeError,ValueError) as error:
        raise ValueError('作者法线计算组件缺失或不匹配，请重新安装完整作者工具') from error

def tangents(positions,normals,uv):
    positions=np.ascontiguousarray(positions,dtype=np.float32)
    normals=np.asarray(normals,dtype=np.float64);uv=np.ascontiguousarray(uv,dtype=np.float32)
    count=len(positions)
    if not count or count%3 or count>MAX_CORNERS or positions.shape!=(count,3) or normals.shape!=(count,3) or uv.shape!=(count,2):raise ValueError('最终三角网格角点数量或布局无效')
    normals=_unit(normals)
    if not np.isfinite(positions).all() or not np.isfinite(uv).all():raise ValueError('切线输入包含非有限值')
    supplied=np.ascontiguousarray(normals,dtype=np.float32);output=np.empty((count,4),np.float32)
    pointer=ctypes.POINTER(ctypes.c_float)
    if native()[1](count,*[x.ctypes.data_as(pointer) for x in (positions,supplied,uv,output)])!=0:raise ValueError('切线生成失败')
    if not np.isfinite(output).all() or not np.isin(output[:,3],(-1.,1.)).all():raise ValueError('切线生成结果无效')
    projected=output[:,:3].astype(np.float64)-normals*np.sum(output[:,:3]*normals,axis=1)[:,None]
    cancelled=np.flatnonzero(np.linalg.norm(projected,axis=1)<1e-12);exceptions=[]
    # A cancelling Mikk group may still have a defined per-triangle U
    # differential. Keep that corner direction as a seam, never invent an axis.
    for corner in cancelled:
        start=(int(corner)//3)*3;p=positions[start:start+3].astype(np.float64);tex=uv[start:start+3].astype(np.float64)
        a=tex[1]-tex[0];b=tex[2]-tex[0];det=a[0]*b[1]-a[1]*b[0]
        if det==0:raise ValueError('UV微分与切线组均无定义')
        derivative=((p[1]-p[0])*b[1]-(p[2]-p[0])*a[1])/det
        projected[corner]=derivative-normals[corner]*np.dot(derivative,normals[corner])
        exceptions.append(dict(corner=int(corner),reason='cancelled_Mikk_group_defined_triangle_U'))
    return np.column_stack((_unit(projected),output[:,3])),exceptions

def _ids(mesh,name,domain,count):
    attribute=mesh.attributes.get(name)
    if not attribute or attribute.domain!=domain or attribute.data_type!='INT' or len(attribute.data)!=count:raise ValueError('作者角点追溯属性丢失或布局改变：'+name)
    values=np.empty(count,np.int32);attribute.data.foreach_get('value',values)
    return values

def capture(mesh):
    if any(mesh.attributes.get(name) for name in (CORNER,FINAL,VERTEX)):raise ValueError('源网格包含保留的作者角点属性')
    if not len(mesh.loops) or len(mesh.loops)>MAX_CORNERS or not mesh.uv_layers:raise ValueError('作者角点数量或UV无效')
    result=dict(normals=_unit([tuple(n.vector) for n in mesh.corner_normals]),
        vertices=np.asarray([loop.vertex_index for loop in mesh.loops],np.int32),
        uv=np.asarray([tuple(item.uv) for item in mesh.uv_layers[0].data],np.float32))
    mesh.attributes.new(CORNER,'INT','CORNER').data.foreach_set('value',np.arange(len(mesh.loops),dtype=np.int32))
    return result

def _first(mesh):
    loops=np.asarray([loop.vertex_index for loop in mesh.loops],np.int32)
    first=np.full(len(mesh.vertices),len(mesh.loops),np.int32)
    np.minimum.at(first,loops,np.arange(len(loops),dtype=np.int32))
    if np.any(first==len(mesh.loops)):raise ValueError('导出网格含松散顶点')
    return loops,first

def prepare(obj,state,transform,vertex_tag):
    """Prepare after the editor's UV/edge splits and existing triangulation."""
    import bpy,bmesh
    mesh=obj.data;count=len(mesh.loops)
    if not count or count>MAX_CORNERS or any(len(face.vertices)!=3 for face in mesh.polygons):raise ValueError('作者切线计算需要最终三角网格')
    corners=_ids(mesh,CORNER,'CORNER',count);source=_ids(mesh,vertex_tag,'POINT',len(mesh.vertices));loops,first=_first(mesh)
    if np.any(corners<0) or np.any(corners>=len(state['normals'])) or not np.array_equal(state['vertices'][corners],source[loops]):raise ValueError('作者角点与源顶点对应失效')
    uv=np.ascontiguousarray([tuple(item.uv) for item in mesh.uv_layers[0].data],np.float32)
    if not np.array_equal(state['uv'][corners],uv):raise ValueError('作者角点与UV对应失效')
    matrix=np.asarray(transform,dtype=np.float64)[:3,:3]
    try:desired=_unit(state['normals'][corners]@np.linalg.inv(matrix))
    except np.linalg.LinAlgError as error:raise ValueError('作者法线坐标变换不可逆') from error
    work=mesh.copy()
    try:
        work.transform(transform)
        positions=np.ascontiguousarray([tuple(work.vertices[i].co) for i in loops],np.float32)
    finally:bpy.data.meshes.remove(work)
    tangent,exceptions=tangents(positions,desired,uv)
    encoded=(np.floor(tangent*np.array([127*1.001]*3+[127])).astype(np.int32)&255).astype(np.uint8)
    codes=np.column_stack(((np.floor(desired*127).astype(np.int32)&255).astype(np.uint8),encoded))
    local=np.asarray([tuple(v.co) for v in mesh.vertices],np.float32)
    groups=[tuple(sorted((g.group,g.weight) for g in v.groups)) for v in mesh.vertices]
    colors={a.name:(a.domain,a.data_type,np.asarray([tuple(v.color) for v in a.data],np.float32)) for a in mesh.color_attributes}
    mesh.attributes.new(FINAL,'INT','CORNER').data.foreach_set('value',np.arange(count,dtype=np.int32))
    mesh.attributes.new(VERTEX,'INT','POINT').data.foreach_set('value',np.arange(len(mesh.vertices),dtype=np.int32))
    geometry=mesh.copy();old_count=len(mesh.vertices)
    needs_split=bool(np.any(codes!=codes[first[loops]]))
    if needs_split:
        bm=bmesh.new()
        try:
            bm.from_mesh(mesh);bmesh.ops.split_edges(bm,edges=list(bm.edges))
            point_layer=bm.verts.layers.int[VERTEX];corner_layer=bm.loops.layers.int[FINAL];representatives={};weld={}
            for vertex in bm.verts:
                linked=[int(loop[corner_layer]) for loop in vertex.link_loops]
                if not linked or any(not np.array_equal(codes[linked[0]],codes[i]) for i in linked):raise ValueError('法线切线接缝无法分离')
                identity=(int(vertex[point_layer]),codes[linked[0]].tobytes())
                if identity in representatives:weld[vertex]=representatives[identity]
                else:representatives[identity]=vertex
            if weld:bmesh.ops.weld_verts(bm,targetmap=weld)
            bm.to_mesh(mesh)
        finally:bm.free()
        mesh.update()
    ids=_ids(mesh,FINAL,'CORNER',len(mesh.loops));old_vertices=_ids(mesh,VERTEX,'POINT',len(mesh.vertices));new_loops,_=_first(mesh)
    if len(ids)!=count or not np.array_equal(np.sort(ids),np.arange(count)):raise ValueError('接缝拆点改变角点集合')
    if np.any(old_vertices<0) or np.any(old_vertices>=old_count):raise ValueError('接缝拆点源顶点越界')
    if not np.array_equal(np.asarray([tuple(v.co) for v in mesh.vertices],np.float32),local[old_vertices]):raise ValueError('接缝拆点改变坐标')
    if not np.array_equal(np.asarray([tuple(v.uv) for v in mesh.uv_layers[0].data],np.float32),uv[ids]):raise ValueError('接缝拆点改变UV')
    if [tuple(sorted((g.group,g.weight) for g in v.groups)) for v in mesh.vertices]!=[groups[i] for i in old_vertices]:raise ValueError('接缝拆点改变绑定')
    if set(colors)!={a.name for a in mesh.color_attributes}:raise ValueError('接缝拆点改变顶点色集合')
    for attribute in mesh.color_attributes:
        domain,data_type,values=colors[attribute.name];selection=ids if domain=='CORNER' else old_vertices
        if domain!=attribute.domain or data_type!=attribute.data_type or not np.array_equal(np.asarray([tuple(v.color) for v in attribute.data],np.float32),values[selection]):raise ValueError('接缝拆点改变顶点色')
    faces=[]
    for face in mesh.polygons:
        triangle=ids[list(face.loop_indices)]
        if len(triangle)!=3 or len(set((triangle//3).tolist()))!=1 or tuple((triangle%3).tolist()) not in ((0,1,2),(1,2,0),(2,0,1)):raise ValueError('接缝拆点改变面或绕序')
        faces.append(int(triangle[0]//3))
    if sorted(faces)!=list(range(count//3)):raise ValueError('接缝拆点改变面集合')
    state.update(desired=desired,tangent_bytes=encoded,codes=codes,positions=positions,geometry_mesh=geometry,
        geometry_source_indices=source,source_corner_indices=corners,
        report=dict(method='author-corners-final-UV-Blender-MikkTSpace',native_sha256=native()[2],
            before_vertices=old_count,after_vertices=len(mesh.vertices),source_corners=len(state['normals']),
            max_tangent_normal_abs_dot=float(np.max(np.abs(np.sum(tangent[:,:3]*desired,axis=1)))),
            triangle_derivative_exceptions=[dict(item,source_corner=int(corners[item['corner']]),source_vertex=int(source[loops[item['corner']]])) for item in exceptions]))

def serialize(mesh,state):
    ids=_ids(mesh,FINAL,'CORNER',len(mesh.loops));loops,first=_first(mesh)
    if np.any(ids<0) or np.any(ids>=len(state['desired'])):raise ValueError('最终作者角点越界')
    chosen=ids[first]
    if np.any(state['codes'][ids]!=state['codes'][chosen[loops]]):raise ValueError('同一导出顶点含不同法线切线接缝')
    current=np.asarray([tuple(mesh.vertices[i].co) for i in loops],np.float32)
    if not np.array_equal(current,state['positions'][ids]):raise ValueError('最终角点坐标改变')
    return state['desired'][chosen],state['tangent_bytes'][chosen].tolist(),chosen
