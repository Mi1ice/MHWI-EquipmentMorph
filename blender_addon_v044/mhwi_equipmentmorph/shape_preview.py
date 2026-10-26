"""Viewport preview on independent mesh/Key copies; source key values are never written."""
import bpy
import json
from bpy.app.handlers import persistent
from . import key_selection

_SESSION=None
TAG='_mhwi_equipmentmorph_preview'

def running():return _SESSION is not None

def present(store,item):
    try:return item is not None and store.get(item.name)==item
    except ReferenceError:return False

def signature(obj):
    shape=obj.data.shape_keys
    return (obj.name,obj.data.as_pointer(),tuple(tuple(row) for row in obj.matrix_world),
            tuple((k.name,k.value,k.mute,k.vertex_group,k.relative_key.as_pointer()) for k in shape.key_blocks))

def start(asset,context,direct):
    global _SESSION
    if running():raise ValueError('请先结束当前预览')
    if context.mode!='OBJECT':raise ValueError('预览需要对象模式')
    keys=key_selection.confirmed(asset,direct);names={key.source for key in keys}
    source_objects=key_selection.objects(asset,direct)
    participants=[o for o in source_objects if o.data.shape_keys and
                  any(o.data.shape_keys.key_blocks.get(name) for name in names)]
    for obj in participants:
        if obj.name not in context.view_layer.objects:raise ValueError('参与网格不在当前视图层：'+obj.name)
        if obj.library or obj.data.library or obj.animation_data or obj.data.animation_data or obj.data.shape_keys.animation_data or obj.constraints:
            raise ValueError('动画、驱动、约束或链接网格暂不支持预览：'+obj.name)
        if obj.show_only_shape_key:raise ValueError('请关闭仅显示活动形态键：'+obj.name)
        for mod in obj.modifiers:
            if not mod.show_viewport:continue
            if mod.type!='ARMATURE':raise ValueError('活动修改器仅支持静止骨架：'+obj.name)
            rig=mod.object
            if not rig or rig.animation_data or rig.constraints or any(b.constraints for b in rig.pose.bones):
                raise ValueError('骨架动画或约束暂不支持预览：'+obj.name)
    layer=context.view_layer
    _SESSION={'asset':asset,'scene':context.scene,'layer':layer,'frame':context.scene.frame_current,
              'direct':direct,'names':names,'source_pointers':{o.as_pointer() for o in source_objects},
              'originals':[(o,o.hide_get(view_layer=layer)) for o in participants],
              'signatures':[signature(o) for o in participants],
              'selected':list(context.selected_objects),'active':layer.objects.active,
              'copies':[],'meshes':[],'collection':None}
    session=_SESSION
    try:
        collection=bpy.data.collections.new('MHWI 临时形态键预览');session['collection']=collection
        collection[TAG]=True
        collection['snapshot']=json.dumps({'scene':context.scene.name,'layer':layer.name,
            'originals':[(o.name,hidden) for o,hidden in session['originals']],
            'selected':[o.name for o in session['selected']],
            'active':session['active'].name if session['active'] else None})
        context.scene.collection.children.link(collection)
        for obj,hidden in session['originals']:
            visible=obj.visible_get(view_layer=layer)
            clone=obj.copy();session['copies'].append(clone)
            clone[TAG]=True
            mesh=obj.data.copy();session['meshes'].append(mesh);clone.data=mesh
            if mesh.shape_keys==obj.data.shape_keys:raise ValueError('预览副本没有独立的形态键数据')
            collection.objects.link(clone);clone.name='MHWI 预览 · '+obj.name
            clone.hide_render=True;clone.hide_select=True;clone.select_set(False)
            clone.hide_set(not visible,view_layer=layer)
            for window in context.window_manager.windows:
                for area in window.screen.areas:
                    if area.type=='VIEW_3D' and area.spaces.active.local_view:
                        clone.local_view_set(area.spaces.active,obj.local_view_get(area.spaces.active))
            obj.hide_set(True,view_layer=layer)
        for key in asset.keys:key.preview_weight=0
        if not bpy.app.timers.is_registered(watch):bpy.app.timers.register(watch,first_interval=.25)
    except Exception:
        stop();raise

def weight(key):
    if not running():return
    session=_SESSION
    if not any(k.as_pointer()==key.as_pointer() for k in session['asset'].keys):return
    if key.source not in session['names']:return
    for obj in session['copies']:
        if not present(bpy.data.objects,obj):continue
        block=obj.data.shape_keys.key_blocks.get(key.source)
        if block:block.value=key.preview_weight

def stop():
    global _SESSION
    if not running():return
    session=_SESSION;_SESSION=None
    if bpy.context.mode!='OBJECT' and bpy.context.object:
        bpy.ops.object.mode_set(mode='OBJECT')
    for obj in session['copies']:
        if present(bpy.data.objects,obj):bpy.data.objects.remove(obj,do_unlink=True)
    for mesh in session['meshes']:
        if present(bpy.data.meshes,mesh):bpy.data.meshes.remove(mesh)
    if present(bpy.data.collections,session['collection']):bpy.data.collections.remove(session['collection'])
    layer=session['layer']
    for obj,hidden in session['originals']:
        if present(bpy.data.objects,obj) and obj.name in layer.objects:obj.hide_set(hidden,view_layer=layer)
    for obj in layer.objects:
        if obj.select_get(view_layer=layer):obj.select_set(False,view_layer=layer)
    for obj in session['selected']:
        if present(bpy.data.objects,obj) and obj.name in layer.objects:obj.select_set(True,view_layer=layer)
    layer.objects.active=session['active'] if present(bpy.data.objects,session['active']) else None
    try:
        for key in session['asset'].keys:key.preview_weight=0
    except ReferenceError:pass

def watch():
    if not running():return None
    session=_SESSION
    try:
        state=bpy.context.window_manager.mhwi_equipmentmorph
        active=state.assets[state.active_asset] if 0<=state.active_asset<len(state.assets) else None
        changed=(active!=session['asset'] or bpy.context.scene!=session['scene'] or bpy.context.view_layer!=session['layer']
                 or bpy.context.mode!='OBJECT' or session['scene'].frame_current!=session['frame']
                 or (state.source_mode=='DIRECT')!=session['direct']
                 or {k.source for k in session['asset'].keys if k.enabled}!=session['names'])
        changed=changed or any(not present(bpy.data.objects,o) or signature(o)!=saved
                               for (o,_),saved in zip(session['originals'],session['signatures']))
        changed=changed or any(not present(bpy.data.objects,o) for o in session['copies'])
        copies={o.as_pointer() for o in session['copies'] if present(bpy.data.objects,o)}
        changed=changed or {o.as_pointer() for o in key_selection.objects(session['asset'],session['direct'])
                           if o.as_pointer() not in copies}!=session['source_pointers']
        if changed:stop();return None
    except (ReferenceError,AttributeError,IndexError,ValueError):stop();return None
    return .25

@persistent
def before_operation(*args):stop();recover()

def recover():
    """Undo or a recovered file can resurrect copies after the live session was ended."""
    for collection in list(bpy.data.collections):
        if not collection.get(TAG):continue
        saved=json.loads(collection['snapshot']);scene=bpy.data.scenes.get(saved['scene'])
        layer=scene.view_layers.get(saved['layer']) if scene else None
        for obj in list(collection.objects):
            if obj.get(TAG):
                mesh=obj.data;bpy.data.objects.remove(obj,do_unlink=True)
                if not mesh.users:bpy.data.meshes.remove(mesh)
            elif scene and len(obj.users_collection)==1:scene.collection.objects.link(obj)
        bpy.data.collections.remove(collection)
        if layer:
            for name,hidden in saved['originals']:
                obj=layer.objects.get(name)
                if obj:obj.hide_set(hidden,view_layer=layer)
            for obj in layer.objects:obj.select_set(obj.name in saved['selected'],view_layer=layer)
            layer.objects.active=layer.objects.get(saved['active']) if saved['active'] else None

@persistent
def after_operation(*args):recover()

def recover_after_register():
    # A script can start preview before the first timer tick after enable.
    # Only stale copies belong to startup recovery, never that live session.
    if not running():recover()
    return None

def register():
    # addon_utils.enable() runs register under RestrictBlend: scene data is not
    # available until registration finishes. Recover disposable copies later.
    if not bpy.app.timers.is_registered(recover_after_register):bpy.app.timers.register(recover_after_register,first_interval=0)
    for name in ('save_pre','load_pre','undo_pre','redo_pre','render_pre'):
        handlers=getattr(bpy.app.handlers,name)
        if before_operation not in handlers:handlers.append(before_operation)
    for name in ('load_post','undo_post','redo_post'):
        handlers=getattr(bpy.app.handlers,name)
        if after_operation not in handlers:handlers.append(after_operation)

def unregister():
    if bpy.app.timers.is_registered(recover_after_register):bpy.app.timers.unregister(recover_after_register)
    stop();recover()
    if bpy.app.timers.is_registered(watch):bpy.app.timers.unregister(watch)
    for name in ('save_pre','load_pre','undo_pre','redo_pre','render_pre'):
        handlers=getattr(bpy.app.handlers,name)
        if before_operation in handlers:handlers.remove(before_operation)
    for name in ('load_post','undo_post','redo_post'):
        handlers=getattr(bpy.app.handlers,name)
        if after_operation in handlers:handlers.remove(after_operation)
