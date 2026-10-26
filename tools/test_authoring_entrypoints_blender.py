"""Retired public operators must refuse before touching author state or files."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from types import SimpleNamespace
from unittest.mock import patch
from authoring_test_support import *
from mhwi_equipmentmorph import shape_preview

addon.register()
state=addon.settings(bpy.context)
collection=bpy.data.collections.new('Entrypoints');bpy.context.scene.collection.children.link(collection)
mesh=bpy.data.meshes.new('EntryMesh');mesh.from_pydata([(0,0,0),(1,0,0),(0,1,0)],[],[(0,1,2)])
obj=bpy.data.objects.new('Entry',mesh);collection.objects.link(obj)
obj.shape_key_add(name='Basis');smile=obj.shape_key_add(name='Smile',from_mix=False);smile.relative_key=mesh.shape_keys.reference_key;smile.value=0
asset=state.assets.add();asset.collection=collection;asset.target_mode='MANUAL';asset.model_id='pl/entry/body'
addon.refresh_keys(asset);asset.keys[0].selected=True;bpy.ops.mhwi_em.confirm_keys()
state.output=str(ARGS.output/'untouched');state.result_path='previous-result';state.status='previous-status'

class Layout:
    def __init__(self): self.props=[];self.buttons=[]
    def row(self,**kw): return self
    def box(self): return self
    def column(self,**kw): return self
    def prop(self,owner,name,**kw): self.props.append(name)
    def label(self,**kw): pass
    def operator(self,name,**kw): self.buttons.append(name);return SimpleNamespace()
    def template_list(self,*a,**kw): pass

for mode in ('DIRECT','LEGACY'):
    state.source_mode=mode
    layout=Layout();addon.MHWI_EM_PT_Export.draw(SimpleNamespace(layout=layout),bpy.context)
    check('source_mode' not in layout.props,'UI hides source selector '+mode)
    check(not set(layout.buttons)&{'mhwi_em.import_job','mhwi_em.read_model','mhwi_em.collect_meshes'},'UI hides retired controls '+mode)
    check('collection' in layout.props,'UI offers collection '+mode)
state.source_mode='DIRECT';addon.refresh_keys(asset);asset.keys[0].selected=True;bpy.ops.mhwi_em.confirm_keys()
shape_preview.start(asset,bpy.context,True)
before=(config(state),scene(),id(shape_preview._SESSION))
for op,kw in [('import_job',dict(filepath=str(ARGS.output/'must-not-read.json'))),('read_model',{}),('collect_meshes',{})]:
    with patch.object(addon.authoring,'import_job',side_effect=AssertionError('retired file read')),patch.object(addon,'load_meshes',side_effect=AssertionError('retired model read')):
        cancelled(lambda:getattr(bpy.ops.mhwi_em,op)(**kw),'retired operator rejected '+op)
    check((config(state),scene(),id(shape_preview._SESSION))==before,'retired operator preserves full state '+op)
shape_preview.stop();state.source_mode='LEGACY'
# Set up confirmed entries/preview after mode's intentional update callback.
state.source_mode='DIRECT';addon.refresh_keys(asset);asset.keys[0].selected=True;bpy.ops.mhwi_em.confirm_keys()
# Bypass the UI update callback only to simulate an explicitly supplied legacy state.
state['source_mode']=1
shape_preview.start(asset,bpy.context,True)
before=(config(state),scene(),id(shape_preview._SESSION))
with patch.object(addon.authoring,'export_recipe',side_effect=AssertionError('retired export called')):
    rejected(lambda:addon.recipe_from_ui(bpy.context),'legacy recipe refused')
    cancelled(lambda:bpy.ops.mhwi_em.export_package(),'legacy public export refused')
check((config(state),scene(),id(shape_preview._SESSION))==before,'legacy guards preserve configuration and active preview')
shape_preview.stop();state.source_mode='DIRECT'
cancelled(lambda:bpy.ops.mhwi_em.export_package(),'direct empty confirmation refused')
state.assets.clear();layout=Layout();addon.MHWI_EM_PT_Export.draw(SimpleNamespace(layout=layout),bpy.context)
check('source_mode' not in layout.props and 'mhwi_em.import_job' not in layout.buttons,'empty UI stays direct')
addon.unregister();addon.register();addon.unregister()
check(not hasattr(bpy.types.WindowManager,'mhwi_equipmentmorph'),'register and unregister balanced')
report('entrypoints')
