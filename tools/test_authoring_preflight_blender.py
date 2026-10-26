"""Production preparation, selection and exporter restoration regressions."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from unittest.mock import patch
from types import SimpleNamespace
from authoring_test_support import *
from mhwi_equipmentmorph import direct_export as direct, key_selection, shape_preview

fixture,exporter=load_fixture();collection,obj=fixture()
addon.register();state=addon.settings(bpy.context)
asset=state.assets.add();asset.collection=collection;asset.target_mode='MANUAL';asset.model_id='pl/f_equip/pl105_0000/body/mod/f_body105_0000'
addon.refresh_keys(asset)
def choose(source='Smile'):
    for row in asset.keys:row.selected=row.source==source
    bpy.ops.mhwi_em.confirm_keys()
choose()
recipe=lambda:addon.recipe_from_ui(bpy.context)['assets'][0]
entry=recipe();baseline=(config(state),scene())
objects,controlled,_=direct.preflight(entry)
check(len(objects)==3 and len(controlled)==3,'rest rig and shared source key supported on three meshes')
check((config(state),scene())==baseline,'preflight preserves full configuration and source')
check(abs(obj.data.shape_keys.key_blocks['neck'].value-.4)<1e-7,'fixed nonzero baseline preserved')
smile=obj.data.shape_keys.key_blocks['Smile'];shape=obj.data.shape_keys
def invalid(label,change,restore):
    change();bpy.context.view_layer.update();before=(config(state),scene())
    rejected(lambda:direct.preflight(entry),label)
    check((config(state),scene())==before,label+' has no side effects')
    restore();bpy.context.view_layer.update()
invalid('nonzero dynamic',lambda:setattr(smile,'value',.2),lambda:setattr(smile,'value',0))
invalid('muted dynamic',lambda:setattr(smile,'mute',True),lambda:setattr(smile,'mute',False))
invalid('nonBasis relative',lambda:setattr(smile,'relative_key',shape.key_blocks['neck']),lambda:setattr(smile,'relative_key',shape.reference_key))
invalid('vertex group mask',lambda:setattr(smile,'vertex_group','MhBone_000'),lambda:setattr(smile,'vertex_group',''))
invalid('absolute keys',lambda:setattr(shape,'use_relative',False),lambda:setattr(shape,'use_relative',True))
invalid('show only key',lambda:setattr(obj,'show_only_shape_key',True),lambda:setattr(obj,'show_only_shape_key',False))
invalid('object animation',lambda:obj.animation_data_create(),lambda:obj.animation_data_clear())
invalid('mesh animation',lambda:obj.data.animation_data_create(),lambda:obj.data.animation_data_clear())
invalid('key animation',lambda:shape.animation_data_create(),lambda:shape.animation_data_clear())
constraint=obj.constraints.new('COPY_LOCATION')
invalid('object constraint',lambda:None,lambda:obj.constraints.remove(constraint))
modifier=obj.modifiers.new('Unsupported','SUBSURF')
invalid('active non-armature modifier',lambda:None,lambda:obj.modifiers.remove(modifier))
rig=obj.modifiers[0].object
invalid('rig animation',lambda:rig.animation_data_create(),lambda:rig.animation_data_clear())
constraint=rig.constraints.new('COPY_LOCATION')
invalid('rig constraint',lambda:None,lambda:rig.constraints.remove(constraint))
bone=rig.pose.bones[0];constraint=bone.constraints.new('COPY_LOCATION')
invalid('bone constraint',lambda:None,lambda:bone.constraints.remove(constraint))
invalid('non-rest rig',lambda:setattr(bone,'location',(0,0,.1)),lambda:setattr(bone,'location',(0,0,0)))
rejected(lambda:direct.preflight(dict(entry,collection=SimpleNamespace(library=object()))),'linked collection guard (input proxy; no library file IO)')
rejected(lambda:direct.preflight(dict(entry,collection=None)),'missing collection')
rejected(lambda:direct.preflight(dict(entry,keys=entry['keys']+[dict(id='missing',source_key='missing')])),'missing selected source cannot silently disappear')

# Selected rows remain bound by source, independent of filtering and shared meshes.
unicode=obj.shape_key_add(name='微笑',from_mix=False);unicode.relative_key=shape.reference_key;unicode.value=0
addon.refresh_keys(asset);row=next(k for k in asset.keys if k.source=='微笑');row.selected=True
asset.key_search='not-visible';bpy.ops.mhwi_em.confirm_keys();row=next(k for k in asset.keys if k.source=='微笑');row.public_id='unicode_smile'
chosen=key_selection.confirmed(asset,True)
check([k.source for k in chosen]==['Smile','微笑'],'hidden selection and Unicode source preserved')
check(next(k for k in asset.keys if k.source=='Smile').participants==3,'same source on multiple meshes is one key')
row.public_id='Smile';rejected(lambda:key_selection.confirmed(asset,True),'duplicate public ID refused')
row.public_id='x'*64;rejected(lambda:key_selection.confirmed(asset,True),'overlength public ID refused without truncation')
row.public_id='中文';rejected(lambda:key_selection.confirmed(asset,True),'nonASCII public ID refused')
row.public_id='unicode_smile';obj.shape_key_remove(unicode)
rejected(lambda:key_selection.confirmed(asset,True),'missing confirmed source refused')
asset.keys.clear()
# All-zero offsets remain legitimate dynamic keys. Counts test actual production selection.
for index in range(1025):
    key=obj.shape_key_add(name='Boundary_'+str(index),from_mix=False);key.relative_key=shape.reference_key;key.value=0
addon.refresh_keys(asset)
for count in (1,65,1024,1025):
    for row in asset.keys:row.enabled=row.source.startswith('Boundary_') and int(row.source.split('_')[1])<count;row.public_id=row.source
    if count==1025:rejected(lambda:key_selection.confirmed(asset,True),'1025 keys refused')
    else:
        selected=key_selection.confirmed(asset,True)
        check(len(selected)==count,'confirmed key capacity '+str(count))
        check(len(direct.preflight(recipe())[1])==1,'zero displacement keys retained '+str(count))
for row in asset.keys:row.enabled=row.source=='Smile'
asset.key_search='';before=(config(state),scene())
shape_preview.start(asset,bpy.context,True);next(k for k in asset.keys if k.source=='Smile').preview_weight=.7
shape_preview.stop()
check(scene()==before[1],'normal preview exit restores source and selection')

# Real serializer exceptions must restore monkey patches, active/selection and temporary data.
functions={n:getattr(exporter,n) for n in ('createCloneMesh','parseVertexDict','buildMod3File','splitSharpEdges')}
last=bpy.context.scene.mhw_mod3_toolpanel.lastExportCollection
before=(config(state),scene())
with patch.object(exporter,'parseVertexDict',side_effect=RuntimeError('injected serializer failure')):
    rejected(lambda:direct.export_model(exporter,recipe(),ARGS.output/'never.mod3'),'serializer failure reported')
check(all(getattr(exporter,n) is f for n,f in functions.items()),'all exporter functions restored')
check((config(state),scene())==before,'serializer failure restores data blocks source and author state')
check(bpy.context.scene.mhw_mod3_toolpanel.lastExportCollection==last,'last export collection restored')
addon.unregister();report('preflight',exporter_path=str(Path(exporter.__file__).resolve()))
