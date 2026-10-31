"""Blender background regression for confirmed keys and non-destructive viewport preview."""
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch
import bpy

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'blender_addon_v044'))
import mhwi_equipmentmorph as addon
from mhwi_equipmentmorph import key_selection,shape_preview as preview
addon.register();state=addon.settings(bpy.context)
collection=bpy.data.collections.new('KeyPreviewFixture');bpy.context.scene.collection.children.link(collection)
objects=[]
for index,name in enumerate(('A','B','C','D')):
    mesh=bpy.data.meshes.new(name);mesh.from_pydata([(0,0,0),(1,0,0),(0,1,0)],[],[(0,1,2)])
    obj=bpy.data.objects.new(name,mesh);collection.objects.link(obj);objects.append(obj)
    obj.shape_key_add(name='Basis')
    fixed=obj.shape_key_add(name='fixed');fixed.data[2].co.z=.2;fixed.value=.4
    if name!='D':
        for source,vertex,delta in [('key1',0,index+1),('key2',1,.5*(index+1))]:
            k=obj.shape_key_add(name=source,from_mix=False);k.relative_key=mesh.shape_keys.reference_key
            for point,basis in zip(k.data,mesh.shape_keys.reference_key.data):point.co=basis.co
            k.data[vertex].co.z=delta;k.value=0
        k=obj.shape_key_add(name='muted');k.mute=True;k.value=0
        for n in range(250):
            k=obj.shape_key_add(name='Unused_'+str(n));k.value=0
asset=state.assets.add();asset.collection=collection;asset.part='body';asset.target_mode='MANUAL';asset.model_id='pl/f_equip/pl105_0000/body/mod/f_body105_0000'
addon.refresh_keys(asset)
checks=[]
def check(ok,label):assert ok,label;checks.append(label)
def rejected(call,label):
    try:call()
    except (ValueError,RuntimeError):check(True,label)
    else:raise AssertionError(label)
def key(name):return next(k for k in asset.keys if k.source==name)
def counts():return [len(bpy.data.objects),len(bpy.data.meshes),len(bpy.data.shape_keys),len(bpy.data.collections)]
def source_snapshot():
    return [(o.name,tuple(tuple(v.co) for v in o.data.vertices),
             tuple((k.name,k.value,k.mute,tuple(tuple(v.co) for v in k.data)) for k in o.data.shape_keys.key_blocks)) for o in objects]
def viewport():return ([o.hide_get() for o in objects],sorted(o.name for o in bpy.context.selected_objects),bpy.context.view_layer.objects.active)

check(len(asset.keys)==254,'hundreds of candidates grouped by source name')
check(key('key1').participants==3 and key('fixed').participants==4,'same-name participation counts remain correct')
key('key1').selected=True;asset.key_search='KEY2'
fake=SimpleNamespace(bitflag_filter_item=1)
flags,_=addon.MHWI_EM_UL_Keys.filter_items(fake,bpy.context,asset,'keys')
check([k.source for k,f in zip(asset.keys,flags) if f]==['key2'],'candidate search is case-insensitive and exact substring')
check(key('key1').selected,'hidden candidate remains checked')
key('key2').selected=True
rejected(lambda:addon.recipe_from_ui(bpy.context),'draft checks alone cannot export')
check(bpy.ops.mhwi_em.confirm_keys()=={'FINISHED'},'use selected keys confirms hidden and visible checks')
check({k.source for k in asset.keys if k.enabled}=={'key1','key2'},'only confirmed sources selected')
check(not any(k.selected for k in asset.keys),'confirmation clears draft checks')
key('key1').public_id='alias_one';key('key2').public_id='alias_two'
addon.refresh_keys(asset)
check(key('key1').enabled and key('key1').public_id=='alias_one','refresh preserves confirmation and alias')
key('key1').selected=True;bpy.ops.mhwi_em.confirm_keys()
check(sum(k.enabled for k in asset.keys)==2,'repeat confirmation does not duplicate entries')
key('fixed').selected=True
rejected(lambda:bpy.ops.mhwi_em.confirm_keys(),'nonzero fixed key cannot be confirmed')
key('fixed').selected=False
key('key2').public_id='alias_one'
rejected(lambda:addon.recipe_from_ui(bpy.context),'duplicate alias blocked before export')
key('key2').public_id='中文'
rejected(lambda:preview.start(asset,bpy.context,True),'non-ASCII alias rejected by preview preflight')
key('key2').public_id='alias_two'
recipe=addon.recipe_from_ui(bpy.context)
check([k['source_key'] for k in recipe['assets'][0]['keys']]==['key1','key2'],'export reads confirmation list only')
check([k['id'] for k in recipe['assets'][0]['keys']]==['alias_one','alias_two'],'export preserves separate aliases')

# Preserve hidden participants, unrelated selection, active object and fixed corrections.
for o in bpy.context.view_layer.objects:o.select_set(False)
objects[1].hide_set(True);objects[0].select_set(True);bpy.context.view_layer.objects.active=objects[0]
baseline=counts();before=source_snapshot();view_before=viewport()
check(bpy.ops.mhwi_em.preview_keys()=={'FINISHED'},'preview entry succeeds in object mode')
check(len(preview._SESSION['copies'])==3,'only A B C copied; D without selected keys retained')
check(all(c.data.shape_keys!=o.data.shape_keys for (o,_),c in zip(preview._SESSION['originals'],preview._SESSION['copies'])),'preview uses independent Key datablocks')
key('key1').preview_weight=.75;key('key2').preview_weight=.5;bpy.context.view_layer.update()
for index,clone in enumerate(preview._SESSION['copies']):
    evaluated=clone.evaluated_get(bpy.context.evaluated_depsgraph_get());mesh=evaluated.to_mesh()
    check(abs(mesh.vertices[0].co.z-.75*(index+1))<1e-6,'key1 deformation synchronized '+str(index))
    check(abs(mesh.vertices[1].co.z-.25*(index+1))<1e-6,'key2 combines independently '+str(index))
    check(abs(mesh.vertices[2].co.z-.08)<1e-6,'fixed correction retained '+str(index))
    evaluated.to_mesh_clear()
check(source_snapshot()==before,'all original key values and coordinates unchanged during multi-key preview')
check(preview._SESSION['copies'][1].hide_get(),'originally hidden participant stays hidden in preview')
from mhwi_equipmentmorph.direct_export import preflight
rejected(lambda:preflight(recipe['assets'][0]),'direct backend blocks export while preview is active')
bpy.ops.mhwi_em.reset_preview()
check(all(k.preview_weight==0 for k in asset.keys),'reset zeros preview controls')
check(all(c.data.shape_keys.key_blocks['key1'].value==0 for c in preview._SESSION['copies']),'reset synchronizes clone values')
bpy.ops.mhwi_em.end_preview()
check(counts()==baseline and viewport()==view_before,'exit removes all preview data and restores viewport state')
check(source_snapshot()==before,'original mesh and key state unchanged after exit')

with patch.object(bpy.app.timers,'is_registered',return_value=False),patch.object(bpy.app.timers,'register',side_effect=RuntimeError('injected guard failure')):
    rejected(lambda:preview.start(asset,bpy.context,True),'injected late-start failure reported')
check(not preview.running() and counts()==baseline and viewport()==view_before,'failed start rolls back clones and visibility')
preview.start(asset,bpy.context,True);key('key1').preview_weight=1
addon.recipe_from_ui(bpy.context)
check(not preview.running() and counts()==baseline,'recipe ends preview before collecting export inputs')
preview.start(asset,bpy.context,True);bpy.context.scene.frame_set(bpy.context.scene.frame_current+1);preview.watch()
check(not preview.running() and counts()==baseline,'frame change ends preview cleanly')
preview.start(asset,bpy.context,True);objects[0].data.shape_keys.key_blocks['key1'].value=.2;preview.watch()
check(not preview.running() and abs(objects[0].data.shape_keys.key_blocks['key1'].value-.2)<1e-6,'external source edit ends preview without overwriting user value')
objects[0].data.shape_keys.key_blocks['key1'].value=0
preview.start(asset,bpy.context,True)
event=SimpleNamespace(type='ESC')
check(addon.MHWI_EM_OT_Preview.modal(SimpleNamespace(),bpy.context,event)=={'FINISHED'} and not preview.running(),'Escape ends preview')

# Removed and newly invalid confirmed keys remain explicit errors after refresh.
for obj in objects[:3]:obj.shape_key_remove(obj.data.shape_keys.key_blocks['key2'])
addon.refresh_keys(asset)
check(key('key2').enabled and not key('key2').can_control and key('key2').participants==0,'missing confirmed key retained for explicit removal')
rejected(lambda:addon.recipe_from_ui(bpy.context),'missing confirmed key blocks export')
bpy.ops.mhwi_em.remove_key(source='key2')
check(not key('key2').enabled and len(addon.recipe_from_ui(bpy.context)['assets'][0]['keys'])==1,'remove button updates export selection')
objects[0].data.shape_keys.key_blocks['key1'].value=.2;addon.refresh_keys(asset)
check(key('key1').enabled and not key('key1').can_control,'invalidated confirmed key stays visible')
rejected(lambda:addon.recipe_from_ui(bpy.context),'nonzero confirmed key cannot export')
objects[0].data.shape_keys.key_blocks['key1'].value=0;addon.refresh_keys(asset)

asset.collection=collection
check(key('key1').enabled,'reassigning same collection preserves confirmation')
other=bpy.data.collections.new('Other');bpy.context.scene.collection.children.link(other)
preview.start(asset,bpy.context,True);asset.collection=other
check(not preview.running() and not asset.keys and counts()[0:3]==baseline[0:3],'collection switch clears confirmation and cleans preview')
asset.collection=collection;addon.refresh_keys(asset);key('key1').selected=True;bpy.ops.mhwi_em.confirm_keys()
preview.start(asset,bpy.context,True);state.source_mode='LEGACY'
check(not preview.running() and not asset.keys,'source-mode switch cleans preview and invalidates candidates')
state.source_mode='DIRECT';addon.refresh_keys(asset);key('key1').selected=True;bpy.ops.mhwi_em.confirm_keys()

# Recover copies that an undo snapshot or autosave recovery resurrects without a live session.
baseline_with_other=counts();view_before=viewport()
preview.start(asset,bpy.context,True);preview._SESSION=None
preview.after_operation()
check(counts()==baseline_with_other and viewport()==view_before,'resurrected orphan copies cleaned with visibility and selection restored')

class Layout:
    def __init__(self):self.props=[];self.labels=[];self.buttons=[];self.lists=[]
    def row(self,**kwargs):return self
    def box(self):return self
    def prop(self,owner,name,**kwargs):self.props.append(name)
    def label(self,*,text,**kwargs):self.labels.append(text)
    def operator(self,name,**kwargs):self.buttons.append(name);return SimpleNamespace()
    def template_list(self,name,*args,**kwargs):self.lists.append(name)
asset.show_candidates=True
layout=Layout();addon.draw_keys(layout,asset,bpy.context)
check('key_search' in layout.props and 'mhwi_em.confirm_keys' in layout.buttons,'candidate search and confirmation shown together')
check(set(layout.lists)=={'MHWI_EM_UL_Keys','MHWI_EM_UL_Confirmed'},'candidate and confirmed lists have separate UI entries')
candidate=Layout();addon.MHWI_EM_UL_Keys.draw_item(SimpleNamespace(),bpy.context,candidate,None,key('key1'),0,None,None,0)
check('selected' in candidate.props and 'public_id' not in candidate.props,'candidate rows have selection without alias fields')
confirmed=Layout();addon.MHWI_EM_UL_Confirmed.draw_item(SimpleNamespace(),bpy.context,confirmed,None,key('key1'),0,None,None,0)
check('public_id' in confirmed.props and 'mhwi_em.remove_key' in confirmed.buttons,'confirmed rows expose alias and removal')
preview.start(asset,bpy.context,True);layout=Layout();addon.draw_keys(layout,asset,bpy.context)
check('preview_weight' in layout.props and 'mhwi_em.reset_preview' in layout.buttons and 'mhwi_em.end_preview' in layout.buttons,'preview uses separate full-width sliders and reset/exit controls')
preview.stop()

# Closeout runs explicitly skip file IO; default retains the historical save test.
saved=None
if '--no-save' not in sys.argv:
    folder=ROOT/'.build/current-validation/key-preview-tests';folder.mkdir(parents=True,exist_ok=True)
    saved=Path(tempfile.mkdtemp(dir=folder))/'synthetic.blend'
    preview.start(asset,bpy.context,True);key('key1').preview_weight=1
    bpy.ops.wm.save_as_mainfile(filepath=str(saved),copy=True)
    check(not preview.running() and not any(c.name.startswith('MHWI 临时形态键预览') for c in bpy.data.collections),'save_pre ends preview before saving synthetic fixture')
    with bpy.data.libraries.load(str(saved)) as (source,destination):
        check(not any(n.startswith('MHWI 预览') for n in source.objects),'saved fixture contains no preview objects')
check(all(preview.before_operation in getattr(bpy.app.handlers,n) for n in ('load_pre','undo_pre','redo_pre','render_pre')),'file/undo/render cleanup handlers registered')
preview.start(asset,bpy.context,True);addon.unregister()
check(not preview.running() and counts()[0:3]==baseline[0:3],'unregister removes preview and independent meshes')
check(not bpy.app.timers.is_registered(preview.watch),'unregister removes timer')
check(all(preview.before_operation not in getattr(bpy.app.handlers,n) for n in ('save_pre','load_pre','undo_pre','redo_pre','render_pre')),'unregister removes handlers')
check(all(preview.after_operation not in getattr(bpy.app.handlers,n) for n in ('load_post','undo_post','redo_post')),'unregister removes recovery handlers')
addon.register();addon.unregister()
check(True,'registration lifecycle repeats cleanly')
report={'version':'0.4.4','passed':len(checks),'checks':checks,'blender':list(bpy.app.version),
        'original_key_values_never_written_by_preview':True,'real_mod_project_used':False,
        'user_project_saved':False,'synthetic_fixture_saved_for_cleanup_test':str(saved)}
(Path(sys.argv[sys.argv.index('--output')+1]) if '--output' in sys.argv else ROOT/'.build/current-validation/addon-key-preview.json').write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
print('PASS: '+str(len(checks))+' key confirmation and preview checks')
