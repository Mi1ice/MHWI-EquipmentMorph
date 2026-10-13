bl_info={'name':'MHWI-EquipmentMorph','author':'MHWI-EquipmentMorph','version':(0,4,0),'blender':(4,2,0),'location':'3D View > Sidebar > MHWI-EquipmentMorph','description':'从同一集合联合导出MOD3和装备形态键数据','category':'Import-Export'}
import json
from pathlib import Path
import bpy
from bpy.props import BoolProperty,CollectionProperty,EnumProperty,IntProperty,PointerProperty,StringProperty
from bpy.types import Operator,Panel,PropertyGroup,UIList
from bpy_extras.io_utils import ImportHelper
from . import authoring

PARTS=[('head','头部',''),('body','身体',''),('arms','腕部',''),('waist','腰部',''),('legs','腿部','')]
_MESH_OPTIONS={};_EMPTY_OPTIONS=[('-1','请选择模型网格','')]
def settings(context):return context.window_manager.mhwi_equipmentmorph
def active(context):
    state=settings(context)
    return state.assets[state.active_asset] if 0<=state.active_asset<len(state.assets) else None
def mesh_items(self,context):
    if context:
        for asset in settings(context).assets:
            if any(b.as_pointer()==self.as_pointer() for b in asset.bindings):return _MESH_OPTIONS.get(asset.model,_EMPTY_OPTIONS)
    return _EMPTY_OPTIONS
def load_meshes(asset):
    _,meshes=authoring.model_meshes(Path(bpy.path.abspath(asset.model)).read_bytes())
    _MESH_OPTIONS[asset.model]=list(_EMPTY_OPTIONS)+[(str(m['mesh']),m['material']+' · '+str(m['count'])+'顶点','模型网格 '+str(m['mesh'])) for m in meshes]
    return meshes
def refresh_keys(asset):
    previous={k.source:(k.enabled,k.public_id,k.proof_json,k.author_shading) for k in asset.keys};sources={}
    direct=settings(bpy.context).source_mode=='DIRECT'
    if direct and not asset.collection:raise ValueError('请选择MOD3集合')
    objects=[o for o in asset.collection.all_objects if o.type=='MESH'] if direct else [bpy.data.objects.get(b.object_name) for b in asset.bindings]
    for obj in objects:
        if not obj or obj.type!='MESH':raise ValueError('参与网格不存在')
        if obj.data.shape_keys:
            for key in obj.data.shape_keys.key_blocks:
                if key==obj.data.shape_keys.reference_key:continue
                sources.setdefault(key.name,[]).append(key)
    asset.keys.clear()
    for index,(name,keys) in enumerate(sorted(sources.items())):
        item=asset.keys.add();item.source=name
        item.can_control=all(k.value==0 and not k.mute and not k.vertex_group and k.relative_key==k.id_data.reference_key for k in keys)
        item.participants=len(keys)
        item.public_id=name if name.isascii() and len(name)<64 else 'morph_'+str(index+1)
        if name in previous:
            enabled,item.public_id,item.proof_json,item.author_shading=previous[name];item.enabled=enabled and item.can_control

def key_status(asset,source,context):
    objects=list(asset.collection.all_objects) if settings(context).source_mode=='DIRECT' and asset.collection else [bpy.data.objects.get(b.object_name) for b in asset.bindings]
    for obj in objects:
        if obj and obj.type=='MESH' and obj.data.shape_keys:
            key=obj.data.shape_keys.key_blocks.get(source)
            if key and key.value!=0:return '键值非0：固定'
    return '固定/不支持'

class MHWI_EM_Binding(PropertyGroup):
    object_name:StringProperty(name='源网格')
    mesh:EnumProperty(name='模型网格',items=mesh_items)
class MHWI_EM_Key(PropertyGroup):
    source:StringProperty(name='源形态键')
    public_id:StringProperty(name='接口名称')
    enabled:BoolProperty(name='导出',default=False)
    can_control:BoolProperty(default=True)
    participants:IntProperty(default=0)
    author_shading:BoolProperty(name='作者法线',default=True)
    proof_json:StringProperty(default='{}')
class MHWI_EM_Asset(PropertyGroup):
    collection:PointerProperty(name='MOD3集合',type=bpy.types.Collection)
    direct_shading:EnumProperty(name='法线',items=[('BASELINE','保留基准法线',''),('GEOMETRIC','逐键几何法线','从导出后的拆点网格生成端点方向；保留作者基准方向')],default='BASELINE')
    use_blender_material_name:BoolProperty(name='使用Blender材质名',default=False)
    part:EnumProperty(name='装备部位',items=PARTS,default='body')
    package:StringProperty(name='包名',default='equipment-body')
    model:StringProperty(name='实际MOD3',subtype='FILE_PATH')
    model_id:StringProperty(name='游戏资源路径')
    equipment_id:StringProperty(name='目标套装 ID',description='可选，三位数字；同步修改套装目录和模型名，例如105改为001')
    mapping:StringProperty(name='已有映射',subtype='FILE_PATH')
    reference_capture:StringProperty(name='映射来源采集',subtype='FILE_PATH')
    shading:EnumProperty(name='法线',items=[('BASELINE','保留基准法线',''),('AUTHOR','使用已有作者法线数据',''),('INTENT','保持作者法线方向（自动生成）','需要兼容MOD Toolkit；不修改源法线编码'),('GEOMETRIC','逐键几何法线','需要支持逐键法线的运行时；保留基准方向，按各键端点变化混合（近似）')],default='BASELINE')
    author_rotations:StringProperty(name='作者法线数据',subtype='FILE_PATH')
    contract_mode:EnumProperty(name='模型身份依据',items=[('serialized','从MOD3生成','需要磁盘契约v2运行时；已证明的顶点格式'),('recorded','已有运行时模型记录（可选）','')],default='serialized')
    contract_file:StringProperty(name='运行时模型记录',subtype='FILE_PATH')
    bindings:CollectionProperty(type=MHWI_EM_Binding)
    active_binding:IntProperty(default=0)
    keys:CollectionProperty(type=MHWI_EM_Key)
    active_key:IntProperty(default=0)
class MHWI_EM_Settings(PropertyGroup):
    source_mode:EnumProperty(name='导出来源',items=[('DIRECT','集合联合导出','从同一集合生成MOD3和形态键数据，无需选择模型网格编号'),('LEGACY','已有MOD3','使用已有模型和显式映射')],default='DIRECT')
    assets:CollectionProperty(type=MHWI_EM_Asset)
    active_asset:IntProperty(default=0)
    output:StringProperty(name='输出文件夹',subtype='DIR_PATH')
    matrix_json:StringProperty(default='')
    status:StringProperty(default='选择装备模型、网格与动态形态键')
    result_path:StringProperty(default='')

class MHWI_EM_UL_Assets(UIList):
    def draw_item(self,context,layout,data,item,icon,active_data,active_propname,index):
        from .output_files import retarget_model
        try:name=retarget_model(item.model_id,item.equipment_id).rsplit('/',1)[-1]
        except ValueError:name='未填写有效模型路径'
        layout.label(text=dict((p[0],p[1]) for p in PARTS)[item.part]+' · '+name,icon='MESH_DATA')
class MHWI_EM_UL_Bindings(UIList):
    def draw_item(self,context,layout,data,item,icon,active_data,active_propname,index):
        row=layout.row();row.label(text=item.object_name);row.prop(item,'mesh',text='')
class MHWI_EM_UL_Keys(UIList):
    def draw_item(self,context,layout,data,item,icon,active_data,active_propname,index):
        row=layout.row();check=row.row();check.enabled=item.can_control;check.prop(item,'enabled',text='')
        asset=active(context)
        row.label(text=item.source+' ('+str(item.participants)+'网格)'+(' · '+key_status(asset,item.source,context) if asset and not item.can_control else ''))
        if asset and settings(context).source_mode=='LEGACY' and asset.shading in ('AUTHOR','INTENT'):row.prop(item,'author_shading',text='法线')
        row.prop(item,'public_id',text='')

class MHWI_EM_OT_Add(Operator):
    bl_idname='mhwi_em.add_asset';bl_label='添加装备模型'
    def execute(self,context):
        state=settings(context)
        if len(state.assets)>=64:self.report({'ERROR'},'最多64个装备模型');return {'CANCELLED'}
        asset=state.assets.add();state.active_asset=len(state.assets)-1
        chosen={a.part for a in state.assets[:-1]}
        asset.part=next((p[0] for p in PARTS if p[0] not in chosen),'body');asset.package='equipment-'+asset.part
        asset.contract_mode='serialized';return {'FINISHED'}
class MHWI_EM_OT_Remove(Operator):
    bl_idname='mhwi_em.remove_asset';bl_label='移除部位配置'
    def execute(self,context):
        state=settings(context)
        if active(context):state.assets.remove(state.active_asset);state.active_asset=max(0,state.active_asset-1)
        return {'FINISHED'}
class MHWI_EM_OT_Model(Operator):
    bl_idname='mhwi_em.read_model';bl_label='读取模型网格'
    def execute(self,context):
        try:
            asset=active(context);load_meshes(asset)
            if not asset.model_id:asset.model_id=authoring.resource_id(bpy.path.abspath(asset.model))
            settings(context).status='模型网格已读取';return {'FINISHED'}
        except Exception as error:self.report({'ERROR'},str(error));return {'CANCELLED'}
class MHWI_EM_OT_Collect(Operator):
    bl_idname='mhwi_em.collect_meshes';bl_label='使用选中的网格'
    def execute(self,context):
        try:
            asset=active(context);meshes=load_meshes(asset);objects=[o for o in context.selected_objects if o.type=='MESH']
            if not objects:raise ValueError('请先选择参与变形的网格')
            asset.bindings.clear();asset.mapping='';asset.reference_capture=''
            for obj in objects:
                binding=asset.bindings.add();binding.object_name=obj.name
                materials={m.name for m in obj.data.materials if m};matches=[m for m in meshes if m['material'] in materials]
                if len(matches)==1:binding.mesh=str(matches[0]['mesh'])
            for key in asset.keys:key.proof_json='{}'
            refresh_keys(asset);settings(context).status='已收集网格；请选择目标模型网格及动态键';return {'FINISHED'}
        except Exception as error:self.report({'ERROR'},str(error));return {'CANCELLED'}
class MHWI_EM_OT_Keys(Operator):
    bl_idname='mhwi_em.refresh_keys';bl_label='刷新可用形态键'
    def execute(self,context):
        try:refresh_keys(active(context));return {'FINISHED'}
        except Exception as error:self.report({'ERROR'},str(error));return {'CANCELLED'}
class MHWI_EM_OT_Import(Operator,ImportHelper):
    bl_idname='mhwi_em.import_job';bl_label='导入已验证作业'
    filename_ext='.json'
    filter_glob:StringProperty(default='*.json',options={'HIDDEN'})
    def execute(self,context):
        try:
            recipe=authoring.import_job(self.filepath)
            for entry in recipe['assets']:
                for binding in entry['bindings']:
                    if binding['object'] not in bpy.data.objects:raise ValueError('工程缺少网格：'+binding['object'])
            state=settings(context);state.source_mode='LEGACY';state.assets.clear();state.matrix_json=json.dumps(recipe['export_matrix'])
            for entry in recipe['assets']:
                asset=state.assets.add()
                for field in ['part','package','model','model_id','mapping','reference_capture','shading','author_rotations','contract_mode','contract_file']:setattr(asset,field,entry[field])
                load_meshes(asset)
                for binding in entry['bindings']:item=asset.bindings.add();item.object_name=binding['object'];item.mesh=str(binding['mesh'])
                for key in entry['keys']:
                    item=asset.keys.add();item.source=key['source_key'];item.public_id=key['id'];item.enabled=True;item.author_shading=key['author_shading'];item.proof_json=json.dumps(key)
                refresh_keys(asset)
            state.active_asset=0;state.status='已导入；固定修正保留，动态键已选择';return {'FINISHED'}
        except Exception as error:self.report({'ERROR'},str(error));return {'CANCELLED'}

def recipe_from_ui(context):
    state=settings(context);assets=[]
    if state.source_mode=='DIRECT':
        for asset in state.assets:
            assets.append(dict(part=asset.part,package=asset.package,collection=asset.collection,model_id=asset.model_id,
                equipment_id=asset.equipment_id,keys=[dict(id=k.public_id,source_key=k.source,author_shading=False) for k in asset.keys if k.enabled],
                shading=asset.direct_shading,use_blender_material_name=asset.use_blender_material_name))
        return {'assets':assets}
    matrix=json.loads(state.matrix_json) if state.matrix_json else json.loads((Path(__file__).parent/'coordinate_reference.json').read_text())['export_matrix']
    for asset in state.assets:
        meshes=load_meshes(asset);bindings=[]
        for binding in asset.bindings:
            match=next((m for m in meshes if str(m['mesh'])==binding.mesh),None)
            if match is None:raise ValueError('请选择 '+binding.object_name+' 的目标模型网格')
            bindings.append({'object':binding.object_name,'mesh':match['mesh'],'material':match['material']})
        keys=[]
        for key in asset.keys:
            if key.enabled:
                proof=json.loads(key.proof_json);proof.update(id=key.public_id,source_key=key.source,author_shading=asset.shading in ('AUTHOR','INTENT') and key.author_shading);keys.append(proof)
        assets.append({'part':asset.part,'package':asset.package,'model':bpy.path.abspath(asset.model),'model_id':asset.model_id,
            'mapping':bpy.path.abspath(asset.mapping) if asset.mapping else '',
            'reference_capture':bpy.path.abspath(asset.reference_capture) if asset.reference_capture else '',
            'bindings':bindings,'keys':keys,'shading':asset.shading,
            'author_rotations':bpy.path.abspath(asset.author_rotations) if asset.author_rotations else '',
            'contract_mode':asset.contract_mode,'contract_file':bpy.path.abspath(asset.contract_file) if asset.contract_file else ''})
    return {'project_name':authoring.NAME,'assets':assets,'export_matrix':matrix}
class MHWI_EM_OT_Export(Operator):
    bl_idname='mhwi_em.export_package';bl_label='生成装备形态键数据包'
    def execute(self,context):
        state=settings(context)
        try:
            if context.mode!='OBJECT':raise ValueError('请切换到对象模式')
            if not state.output:raise ValueError('请选择输出文件夹')
            if state.source_mode=='DIRECT':
                from .direct_export import export_direct
                result=export_direct(recipe_from_ui(context),bpy.path.abspath(state.output))
            else:result=authoring.export_recipe(recipe_from_ui(context),bpy.path.abspath(state.output))
            state.result_path=str(Path(bpy.path.abspath(state.output))/'result.json');state.status='生成完成：'+str(len(result['assets']))+'部位；未安装到游戏'
            self.report({'INFO'},state.status);return {'FINISHED'}
        except Exception as error:
            state.status='生成暂停：'+str(error);self.report({'ERROR'},state.status);return {'CANCELLED'}
class MHWI_EM_PT_Export(Panel):
    bl_label='MHWI-EquipmentMorph';bl_idname='MHWI_EM_PT_export';bl_space_type='VIEW_3D';bl_region_type='UI';bl_category='MHWI-EquipmentMorph'
    def draw(self,context):
        layout=self.layout;state=settings(context);layout.prop(state,'source_mode')
        if state.source_mode=='LEGACY':layout.operator('mhwi_em.import_job')
        layout.template_list('MHWI_EM_UL_Assets','',state,'assets',state,'active_asset',rows=3)
        row=layout.row(align=True);row.operator('mhwi_em.add_asset',icon='ADD');row.operator('mhwi_em.remove_asset',icon='REMOVE')
        asset=active(context)
        if asset and state.source_mode=='DIRECT':
            box=layout.box();box.prop(asset,'part');box.prop(asset,'collection');box.prop(asset,'model_id');box.prop(asset,'equipment_id')
            box.prop(asset,'use_blender_material_name');box.operator('mhwi_em.refresh_keys')
            box.template_list('MHWI_EM_UL_Keys','',asset,'keys',asset,'active_key',rows=6)
            box.prop(asset,'direct_shading');box.label(text='动态键须为0；固定修正保留');box.label(text='形态键包与模型同名，随换装自动选择')
            box.label(text='自动拆UV/锐边，导出整个集合')
            layout.prop(state,'output',text='输出文件夹');layout.operator('mhwi_em.export_package',text='导出 MOD3＋形态键数据',icon='EXPORT')
            layout.label(text=state.status);layout.label(text='不保存工程；同名导出文件会更新')
            return
        if asset:
            box=layout.box();box.prop(asset,'part');box.prop(asset,'package');box.prop(asset,'model');box.operator('mhwi_em.read_model');box.prop(asset,'model_id')
            box.operator('mhwi_em.collect_meshes');box.template_list('MHWI_EM_UL_Bindings','',asset,'bindings',asset,'active_binding',rows=3)
            box.operator('mhwi_em.refresh_keys');box.template_list('MHWI_EM_UL_Keys','',asset,'keys',asset,'active_key',rows=6)
            box.label(text='动态键源值须为0；neck等固定修正保留')
            box.prop(asset,'shading')
            if asset.shading=='AUTHOR':box.prop(asset,'author_rotations')
            if asset.shading=='INTENT':box.label(text='共享作者方向；参与键取最高权重')
            if asset.shading=='GEOMETRIC':
                box.label(text='各键独立采集并叠加；端点混合近似')
                box.label(text='需要支持逐键法线的运行时')
            box.prop(asset,'contract_mode')
            box.label(text='新生成包需要磁盘契约v2运行时')
            if asset.contract_mode=='recorded':box.prop(asset,'contract_file')
            if asset.mapping:box.label(text='沿用准确来源的既有映射')
        layout.prop(state,'output');layout.operator('mhwi_em.export_package',icon='EXPORT');layout.label(text=state.status)
        layout.label(text='只读采集，不保存工程；新模型仍需最终检查')

classes=(MHWI_EM_Binding,MHWI_EM_Key,MHWI_EM_Asset,MHWI_EM_Settings,MHWI_EM_UL_Assets,MHWI_EM_UL_Bindings,MHWI_EM_UL_Keys,MHWI_EM_OT_Add,MHWI_EM_OT_Remove,MHWI_EM_OT_Model,MHWI_EM_OT_Collect,MHWI_EM_OT_Keys,MHWI_EM_OT_Import,MHWI_EM_OT_Export,MHWI_EM_PT_Export)
def register():
    for cls in classes:bpy.utils.register_class(cls)
    bpy.types.WindowManager.mhwi_equipmentmorph=PointerProperty(type=MHWI_EM_Settings)
def unregister():
    if hasattr(bpy.types.WindowManager,'mhwi_equipmentmorph'):del bpy.types.WindowManager.mhwi_equipmentmorph
    for cls in reversed(classes):bpy.utils.unregister_class(cls)
    _MESH_OPTIONS.clear()
