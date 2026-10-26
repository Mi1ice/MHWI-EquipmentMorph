bl_info={'name':'MHWI-EquipmentMorph','author':'MHWI-EquipmentMorph','version':(0,4,4),'blender':(4,2,0),'location':'3D View > Sidebar > MHWI-EquipmentMorph','description':'精简导出模型和形态键包，可选Debug资料与独立副本预览','category':'Import-Export'}
import json
from pathlib import Path
import bpy
from bpy.props import BoolProperty,CollectionProperty,EnumProperty,FloatProperty,IntProperty,PointerProperty,StringProperty
from bpy.types import Operator,Panel,PropertyGroup,UIList
from bpy_extras.io_utils import ImportHelper
from . import authoring,equipment_selection,key_selection,shape_preview

PARTS=[('head','头部',''),('body','身体',''),('arms','腕部',''),('waist','腰部',''),('legs','腿部','')]
_MESH_OPTIONS={};_EMPTY_OPTIONS=[('-1','请选择模型网格','')]
def settings(context):return context.window_manager.mhwi_equipmentmorph
def active(context):
    state=settings(context)
    return state.assets[state.active_asset] if 0<=state.active_asset<len(state.assets) else None
def end_preview(self,context):shape_preview.stop()
def collection_changed(self,context):
    pointer=str(self.collection.as_pointer()) if self.collection else '0'
    if self.get('_keys_collection_pointer')==pointer:return
    self['_keys_collection_pointer']=pointer
    shape_preview.stop();self.keys.clear();self.active_key=0;self.active_confirmed=0;self.key_search='';self.show_candidates=True
def source_changed(self,context):
    shape_preview.stop()
    for asset in self.assets:asset.keys.clear();asset.key_search='';asset.show_candidates=True
def preview_weight_changed(self,context):shape_preview.weight(self)
def update_target(self,context):
    if self.target_mode!='CATALOG':return
    try:self.model_id=equipment_selection.target(self);self.package=self.model_id.rsplit('/',1)[-1]
    except ValueError:self.model_id=''
def series_items(self,context):
    asset=active(context) if context else None
    return equipment_selection.series_options(asset.sex if asset else 'female')
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
    shape_preview.stop()
    previous={k.source:(k.enabled,k.selected,k.public_id,k.proof_json,k.author_shading) for k in asset.keys}
    direct=settings(bpy.context).source_mode=='DIRECT'
    sources=key_selection.discover(asset,direct)
    asset.keys.clear()
    for index,(name,keys) in enumerate(sorted(sources.items())):
        item=asset.keys.add();item.source=name
        item.can_control=all(key_selection.controllable(k) for k in keys)
        item.participants=len(keys)
        item.public_id=name if name.isascii() and len(name)<64 else 'morph_'+str(index+1)
        if name in previous:
            item.enabled,selected,item.public_id,item.proof_json,item.author_shading=previous[name]
            item.selected=selected and item.can_control
    for name,old in previous.items():
        if name not in sources and old[0]:
            item=asset.keys.add();item.source=name;item.enabled=True;item.can_control=False
            _,_,item.public_id,item.proof_json,item.author_shading=old

def key_status(asset,source,context):
    item=next((k for k in asset.keys if k.source==source),None)
    if item and not item.participants:return '已缺失'
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
    public_id:StringProperty(name='接口名称',description='1至63个可打印ASCII字符，同一模型内不能重复；例如 vrc.blink 或 smile。中文源键可以保留，接口名称请用英文、数字和符号')
    enabled:BoolProperty(name='导出',default=False)
    selected:BoolProperty(name='勾选',default=False)
    preview_weight:FloatProperty(name='预览权重',default=0,min=0,max=1,options={'SKIP_SAVE'},update=preview_weight_changed)
    can_control:BoolProperty(default=True)
    participants:IntProperty(default=0)
    author_shading:BoolProperty(name='作者法线',default=True)
    proof_json:StringProperty(default='{}')
class MHWI_EM_Asset(PropertyGroup):
    collection:PointerProperty(name='MOD3集合',description='选择MHW Model Editor使用的MOD3集合；切换集合后需重新刷新并确认形态键',type=bpy.types.Collection,update=collection_changed)
    direct_shading:EnumProperty(name='法线',items=[('BASELINE','保留基准法线',''),('GEOMETRIC','逐键几何法线','从导出后的拆点网格生成端点方向；保留作者基准方向')],default='BASELINE')
    use_blender_material_name:BoolProperty(name='使用Blender材质名',default=False)
    part:EnumProperty(name='装备部位',items=PARTS,default='body',update=update_target)
    target_mode:EnumProperty(name='路径设置',items=[('CATALOG','选择套装','根据已核验的装备目录自动生成路径'),('MANUAL','高级：手动路径','仅使用填写的完整路径，不进行套装ID改写')],default='CATALOG',update=update_target)
    sex:EnumProperty(name='性别',items=[('female','女性',''),('male','男性','')],default='female',update=update_target)
    series_id:IntProperty(default=-1,update=update_target)
    package:StringProperty(name='包名',description='已有MOD3模式：1至63个英文、数字、下划线或短横线；例如 equipment-body。集合联合导出会自动使用模型名',default='equipment-body')
    model:StringProperty(name='实际MOD3',description='选择电脑上的.mod3文件，例如 D:/MODExport/MyMod/nativePC/pl/f_equip/pl105_0000/body/mod/f_body105_0000.mod3',subtype='FILE_PATH')
    model_id:StringProperty(name='游戏资源路径',description='填写相对nativePC的完整模型路径，不是电脑上的文件夹路径。推荐不写.mod3；例：pl/f_equip/pl105_0000/body/mod/f_body105_0000。支持正斜杠或反斜杠')
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
    active_confirmed:IntProperty(default=0)
    key_search:StringProperty(name='搜索形态键',description='按源名称搜索；隐藏条目不会取消已有勾选')
    show_candidates:BoolProperty(name='可用形态键',default=True)
class MHWI_EM_Settings(PropertyGroup):
    source_mode:EnumProperty(name='导出来源',items=[('DIRECT','集合联合导出','从同一集合生成MOD3和形态键数据，无需选择模型网格编号'),('LEGACY','已有MOD3','使用已有模型和显式映射')],default='DIRECT',update=source_changed)
    assets:CollectionProperty(type=MHWI_EM_Asset)
    active_asset:IntProperty(default=0,update=end_preview)
    output:StringProperty(name='输出文件夹',description='选择任意可写的导出根文件夹，已有文件夹也可使用；例如 D:/MODExport/MyMod。不必位于游戏目录，自动生成nativePC子目录；同路径导出文件会替换，其它文件保留',subtype='DIR_PATH')
    debug:BoolProperty(name='Debug（保留验证资料）',description='默认只输出模型和形态键包。开启后另保留authoring、generated和result.json；两种模式均不输出runtime.ini。历史文件不会自动删除，发布包建议使用空目录',default=False)
    show_field_help:BoolProperty(name='填写说明与示例',description='展开装备选择、手动路径、接口名称与输出文件夹的填写示例',default=False)
    matrix_json:StringProperty(default='')
    status:StringProperty(default='选择装备模型、网格与动态形态键')
    result_path:StringProperty(default='')

class MHWI_EM_UL_Assets(UIList):
    def draw_item(self,context,layout,data,item,icon,active_data,active_propname,index):
        try:name=equipment_selection.target(item).rsplit('/',1)[-1]
        except ValueError:name='请选择有效导出目标'
        layout.label(text=dict((p[0],p[1]) for p in PARTS)[item.part]+' · '+name,icon='MESH_DATA')
class MHWI_EM_UL_Bindings(UIList):
    def draw_item(self,context,layout,data,item,icon,active_data,active_propname,index):
        row=layout.row();row.label(text=item.object_name);row.prop(item,'mesh',text='')
class MHWI_EM_UL_Keys(UIList):
    def filter_items(self,context,data,propname):
        query=data.key_search.casefold()
        return [self.bitflag_filter_item if k.participants and query in k.source.casefold() else 0 for k in getattr(data,propname)],[]
    def draw_item(self,context,layout,data,item,icon,active_data,active_propname,index):
        row=layout.row();check=row.row();check.enabled=item.can_control and not item.enabled;check.prop(item,'selected',text='')
        asset=active(context)
        row.label(text=item.source+' ('+str(item.participants)+'网格)'+(' · '+key_status(asset,item.source,context) if asset and not item.can_control else ''))
        if item.enabled:row.label(text='已加入')
class MHWI_EM_UL_Confirmed(UIList):
    def filter_items(self,context,data,propname):
        return [self.bitflag_filter_item if k.enabled else 0 for k in getattr(data,propname)],[]
    def draw_item(self,context,layout,data,item,icon,active_data,active_propname,index):
        row=layout.row();row.alert=not item.can_control
        row.label(text=item.source+' ('+str(item.participants)+'网格)')
        asset=active(context)
        if asset and not item.can_control:row.label(text=key_status(asset,item.source,context))
        row.prop(item,'public_id',text='')
        if asset and settings(context).source_mode=='LEGACY' and asset.shading in ('AUTHOR','INTENT'):row.prop(item,'author_shading',text='法线')
        row.operator('mhwi_em.remove_key',text='',icon='X').source=item.source

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
        shape_preview.stop()
        state=settings(context)
        if active(context):state.assets.remove(state.active_asset);state.active_asset=max(0,state.active_asset-1)
        return {'FINISHED'}
class MHWI_EM_OT_Suit(Operator):
    bl_idname='mhwi_em.choose_suit';bl_label='搜索并选择套装';bl_property='series'
    series:EnumProperty(name='套装',items=series_items)
    def invoke(self,context,event):
        asset=active(context)
        if not asset:return {'CANCELLED'}
        if any(item[0]==str(asset.series_id) for item in series_items(self,context)):self.series=str(asset.series_id)
        context.window_manager.invoke_search_popup(self);return {'FINISHED'}
    def execute(self,context):
        asset=active(context)
        if not asset:return {'CANCELLED'}
        asset.series_id=int(self.series)
        try:equipment_selection.target(asset);settings(context).status='目标已更新：'+asset.model_id
        except ValueError as error:settings(context).status=str(error)
        return {'FINISHED'}
class MHWI_EM_OT_Model(Operator):
    bl_idname='mhwi_em.read_model';bl_label='读取模型网格'
    def execute(self,context):
        self.report({'ERROR'},RETIRED_REASON);return {'CANCELLED'}

class MHWI_EM_OT_Collect(Operator):
    bl_idname='mhwi_em.collect_meshes';bl_label='使用选中的网格'
    def execute(self,context):
        self.report({'ERROR'},RETIRED_REASON);return {'CANCELLED'}

class MHWI_EM_OT_Keys(Operator):
    bl_idname='mhwi_em.refresh_keys';bl_label='刷新可用形态键'
    def execute(self,context):
        try:refresh_keys(active(context));return {'FINISHED'}
        except Exception as error:self.report({'ERROR'},str(error));return {'CANCELLED'}
class MHWI_EM_OT_Confirm(Operator):
    bl_idname='mhwi_em.confirm_keys';bl_label='使用选中形态键'
    def execute(self,context):
        try:
            shape_preview.stop();asset=active(context)
            added=key_selection.confirm(asset,settings(context).source_mode=='DIRECT')
            refresh_keys(asset)
            asset.show_candidates=False;settings(context).status='已加入'+str(added)+'条形态键；请在导出区域配置接口名称'
            return {'FINISHED'}
        except Exception as error:self.report({'ERROR'},str(error));return {'CANCELLED'}
class MHWI_EM_OT_RemoveKey(Operator):
    bl_idname='mhwi_em.remove_key';bl_label='移除导出形态键'
    source:StringProperty()
    def execute(self,context):
        shape_preview.stop();asset=active(context)
        key=next((k for k in asset.keys if k.source==self.source),None) if asset else None
        if key:key.enabled=False;key.selected=False
        return {'FINISHED'}
class MHWI_EM_OT_Preview(Operator):
    bl_idname='mhwi_em.preview_keys';bl_label='进入预览模式'
    def execute(self,context):
        try:
            shape_preview.start(active(context),context,settings(context).source_mode=='DIRECT');return {'FINISHED'}
        except Exception as error:self.report({'ERROR'},str(error));return {'CANCELLED'}
    def invoke(self,context,event):
        result=self.execute(context)
        if result!={'FINISHED'}:return result
        context.window_manager.modal_handler_add(self);return {'RUNNING_MODAL'}
    def modal(self,context,event):
        if not shape_preview.running():return {'FINISHED'}
        if event.type=='ESC':shape_preview.stop();return {'FINISHED'}
        return {'PASS_THROUGH'}
    def cancel(self,context):shape_preview.stop()
class MHWI_EM_OT_EndPreview(Operator):
    bl_idname='mhwi_em.end_preview';bl_label='结束预览'
    def execute(self,context):shape_preview.stop();return {'FINISHED'}
class MHWI_EM_OT_ResetPreview(Operator):
    bl_idname='mhwi_em.reset_preview';bl_label='全部归零'
    def execute(self,context):
        asset=active(context)
        if asset:
            for key in asset.keys:key.preview_weight=0
        return {'FINISHED'}
class MHWI_EM_OT_Import(Operator,ImportHelper):
    bl_idname='mhwi_em.import_job';bl_label='导入已验证作业'
    filename_ext='.json'
    filter_glob:StringProperty(default='*.json',options={'HIDDEN'})
    def execute(self,context):
        self.report({'ERROR'},RETIRED_REASON);return {'CANCELLED'}

RETIRED_REASON='旧已有MOD3／导入作业入口已退役；请使用集合联合导出'
def require_direct(state):
    if state.source_mode!='DIRECT':raise ValueError(RETIRED_REASON)

def recipe_from_ui(context):
    state=settings(context);require_direct(state)
    shape_preview.stop();assets=[]
    for asset in state.assets:
        model_id=equipment_selection.target(asset)
        assets.append(dict(part=asset.part,package=model_id.rsplit('/',1)[-1],collection=asset.collection,model_id=model_id,
            keys=[dict(id=k.public_id,source_key=k.source,author_shading=False) for k in key_selection.confirmed(asset,True)],
            shading=asset.direct_shading,use_blender_material_name=asset.use_blender_material_name))
    return {'assets':assets}
class MHWI_EM_OT_Export(Operator):
    bl_idname='mhwi_em.export_package';bl_label='生成装备形态键数据包'
    def execute(self,context):
        state=settings(context)
        try:require_direct(state)
        except ValueError as error:
            self.report({'ERROR'},str(error));return {'CANCELLED'}
        try:
            shape_preview.stop()
            if context.mode!='OBJECT':raise ValueError('请切换到对象模式')
            if not state.output:raise ValueError('请选择输出文件夹')
            from .direct_export import export_direct
            result=export_direct(recipe_from_ui(context),bpy.path.abspath(state.output),debug=state.debug)
            state.result_path=str(Path(bpy.path.abspath(state.output))/'result.json') if state.debug else ''
            state.status='生成完成：'+str(len(result['assets']))+'部位；'+('Debug资料已保留' if state.debug else '仅输出模型与形态键包')
            self.report({'INFO'},state.status);return {'FINISHED'}
        except Exception as error:
            state.result_path=''
            state.status='生成暂停：'+str(error);self.report({'ERROR'},state.status);return {'CANCELLED'}
class MHWI_EM_PT_Export(Panel):
    bl_label='MHWI-EquipmentMorph';bl_idname='MHWI_EM_PT_export';bl_space_type='VIEW_3D';bl_region_type='UI';bl_category='MHWI-EquipmentMorph'
    def draw(self,context):
        layout=self.layout;state=settings(context)
        layout.template_list('MHWI_EM_UL_Assets','',state,'assets',state,'active_asset',rows=3)
        row=layout.row(align=True);row.operator('mhwi_em.add_asset',icon='ADD');row.operator('mhwi_em.remove_asset',icon='REMOVE')
        asset=active(context)
        if asset:
            box=layout.box();draw_target(box,asset);box.prop(asset,'collection')
            box.prop(asset,'use_blender_material_name');draw_keys(box,asset,context)
            box.prop(asset,'direct_shading')
            field_help(layout,state)
            layout.prop(state,'output',text='输出文件夹');layout.prop(state,'debug');layout.operator('mhwi_em.export_package',text='导出 MOD3＋形态键数据',icon='EXPORT')
            layout.label(text=state.status)
            return
        field_help(layout,state)
        layout.prop(state,'output',text='输出文件夹');layout.prop(state,'debug')
        layout.operator('mhwi_em.export_package',text='导出 MOD3＋形态键数据',icon='EXPORT');layout.label(text=state.status)

def draw_keys(box,asset,context):
    box.operator('mhwi_em.refresh_keys')
    box.prop(asset,'show_candidates',icon='TRIA_DOWN' if asset.show_candidates else 'TRIA_RIGHT')
    if asset.show_candidates:
        candidates=box.box();candidates.prop(asset,'key_search',icon='VIEWZOOM')
        candidates.template_list('MHWI_EM_UL_Keys','candidates',asset,'keys',asset,'active_key',rows=6)
        candidates.operator('mhwi_em.confirm_keys')
    confirmed=box.box();count=sum(k.enabled for k in asset.keys)
    confirmed.label(text='导出形态键 ('+str(count)+')')
    confirmed.template_list('MHWI_EM_UL_Confirmed','confirmed',asset,'keys',asset,'active_confirmed',rows=min(max(count,2),6))
    if count:
        row=confirmed.row(align=True)
        if shape_preview.running():
            row.operator('mhwi_em.reset_preview');row.operator('mhwi_em.end_preview',icon='X')
            for key in asset.keys:
                if key.enabled:confirmed.prop(key,'preview_weight',text=key.public_id or key.source,slider=True)
        else:row.operator('mhwi_em.preview_keys',icon='PLAY')
        try:key_selection.confirmed(asset,settings(context).source_mode=='DIRECT')
        except ValueError as error:confirmed.label(text=str(error),icon='ERROR')

def draw_target(box,asset):
    box.prop(asset,'target_mode')
    if asset.target_mode=='CATALOG':
        box.prop(asset,'sex')
        try:
            sets,_=equipment_selection.catalogue();selected=sets.get(asset.series_id)
            box.operator('mhwi_em.choose_suit',text=selected['name'] if selected else '搜索并选择套装',icon='VIEWZOOM')
        except ValueError as error:box.label(text=str(error),icon='ERROR')
    box.prop(asset,'part')
    if asset.target_mode=='MANUAL':box.prop(asset,'model_id');return
    try:
        resolved=equipment_selection.resolve(asset.series_id,asset.sex,asset.part)
        box.label(text='资源编号：'+resolved['equipment_directory'].rsplit('/',1)[-1])
        box.label(text=resolved['equipment_directory']+'/')
        box.label(text=resolved['model_id'][len(resolved['equipment_directory'])+1:])
    except ValueError as error:box.label(text=str(error),icon='ERROR')

def field_help(layout,state):
    row=layout.row();row.prop(state,'show_field_help',icon='INFO')
    if not state.show_field_help:return
    box=layout.box()
    box.label(text='选择性别、套装和部位，自动确定路径')
    box.label(text='缺少对应资源的组合不能导出')
    box.label(text='高级手动路径：相对 nativePC')
    box.label(text='例：pl/f_equip/pl105_0000/')
    box.label(text='     body/mod/f_body105_0000')
    box.label(text='接口名称：1至63个ASCII字符')
    box.label(text='例：vrc.blink；同一模型内不重复')
    box.label(text='输出：任意可写文件夹（可以已存在）')
    box.label(text='例：D:/MODExport/MyMod')
    box.label(text='自动生成 nativePC 子目录')

classes=(MHWI_EM_Binding,MHWI_EM_Key,MHWI_EM_Asset,MHWI_EM_Settings,MHWI_EM_UL_Assets,MHWI_EM_UL_Bindings,MHWI_EM_UL_Keys,MHWI_EM_UL_Confirmed,MHWI_EM_OT_Add,MHWI_EM_OT_Remove,MHWI_EM_OT_Suit,MHWI_EM_OT_Model,MHWI_EM_OT_Collect,MHWI_EM_OT_Keys,MHWI_EM_OT_Confirm,MHWI_EM_OT_RemoveKey,MHWI_EM_OT_Preview,MHWI_EM_OT_EndPreview,MHWI_EM_OT_ResetPreview,MHWI_EM_OT_Import,MHWI_EM_OT_Export,MHWI_EM_PT_Export)
def register():
    for cls in classes:bpy.utils.register_class(cls)
    bpy.types.WindowManager.mhwi_equipmentmorph=PointerProperty(type=MHWI_EM_Settings)
    shape_preview.register()
def unregister():
    shape_preview.unregister()
    if hasattr(bpy.types.WindowManager,'mhwi_equipmentmorph'):del bpy.types.WindowManager.mhwi_equipmentmorph
    for cls in reversed(classes):bpy.utils.unregister_class(cls)
    _MESH_OPTIONS.clear()
    equipment_selection.clear_cache()
