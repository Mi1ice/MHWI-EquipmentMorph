"""Run with Blender --background --factory-startup --python-exit-code 1."""
import importlib
import json
from pathlib import Path
import sys
import struct
import numpy as np
import bpy

ROOT=Path(__file__).resolve().parents[1]
CONFIG=json.loads((ROOT/'config/mhwi-equipmentmorph-current.json').read_bytes())
sys.path.insert(0,str(ROOT/CONFIG['addon_directory']))
editor=importlib.import_module('MHW_Model_Editor-main')
editor.register()
exporter=importlib.import_module('MHW_Model_Editor-main.addons.MHW_Model_Editor.modules.mod3.blender_mod3')

def fixture():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    collection=bpy.data.collections.new('Synthetic.mod3');bpy.context.scene.collection.children.link(collection)
    data=bpy.data.armatures.new('SyntheticRig');rig=bpy.data.objects.new('SyntheticRig',data);collection.objects.link(rig)
    bpy.context.view_layer.objects.active=rig;rig.select_set(True);bpy.ops.object.mode_set(mode='EDIT')
    for i in range(5):
        bone=data.edit_bones.new('MhBone_'+str(i).zfill(3));bone.head=(0,0,0);bone.tail=(0,0,1)
    bpy.ops.object.mode_set(mode='OBJECT');rig.select_set(False)
    mesh=bpy.data.meshes.new('SyntheticMesh')
    mesh.from_pydata([(0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,0),(-1,0,0),(-1,-1,0),(0,-1,0)],[],[(0,1,2,3),(4,5,6,7),(0,7,1)])
    obj=bpy.data.objects.new('Group_0_Sub_0__Synthetic',mesh);collection.objects.link(obj)
    mesh.materials.append(bpy.data.materials.new('Synthetic'))
    uv=mesh.uv_layers.new(name='UVMap')
    for i,entry in enumerate(uv.data):entry.uv=((i%4)/3,(i//4)/3)
    for edge in mesh.edges:edge.use_edge_sharp=True
    group=obj.vertex_groups.new(name='MhBone_000');group.add(list(range(8)),1.,'REPLACE')
    mod=obj.modifiers.new('RestRig','ARMATURE');mod.object=rig
    obj.shape_key_add(name='Basis');fixed=obj.shape_key_add(name='neck');fixed.data[1].co.z=.1;fixed.value=.4
    key=obj.shape_key_add(name='Smile',from_mix=False);key.relative_key=mesh.shape_keys.reference_key;key.value=0;key.data[0].co.z=.1;key.data[4].co.z=.2
    for index in range(501):
        unused=obj.shape_key_add(name='Unused_'+str(index),from_mix=False);unused.relative_key=mesh.shape_keys.reference_key;unused.value=0
    mesh.shape_keys.key_blocks['Unused_0'].mute=True
    obj.select_set(True);bpy.context.view_layer.objects.active=obj
    for group_id in (1,2):
        sibling=obj.copy();sibling.data=mesh.copy();sibling.name='Group_'+str(group_id)+'_Sub_0__Synthetic'
        collection.objects.link(sibling)
        color=sibling.data.color_attributes.new(name='Color',type='BYTE_COLOR',domain='CORNER')
        for entry in color.data:entry.color=(1,1,1,1)
        if group_id==2:
            sibling.vertex_groups[0].add(list(range(8)),.2,'REPLACE')
            for i in range(1,5):sibling.vertex_groups.new(name='MhBone_'+str(i).zfill(3)).add(list(range(8)),.2,'REPLACE')
        sibling.location.x=group_id*3;sibling.rotation_euler.z=.25*group_id;sibling.scale=(1.1,1.2,1.3)
    return collection,obj

collection,obj=fixture()
output=ROOT/'.build/direct-export';output.mkdir(parents=True,exist_ok=True)
options=dict(targetCollection=collection,selectedOnly=False,visibleOnly=False,exportAllLODs=False,
    useBlenderMaterialName=False,exportBoundingBoxes=False,autoSolveRepeatedUVs=True,preserveSharpEdges=True,invisibleMantlesModFix=True)
assert exporter.exportMHWMod3File(str(output/'plain.mod3'),options)
print('PLAIN EXPORT PASS')
if '--plain-only' not in sys.argv:
    from mhwi_equipmentmorph.direct_export import export_direct
    from mhwi_equipmentmorph.backend.map_equipment_morph_job import model_meshes
    from mhwi_equipmentmorph.backend.capture_equipment_morph_job import fingerprint,coords
    from mhwi_equipmentmorph.per_key_pack import prepare
    from mhwi_equipmentmorph.output_files import model_resource_id,publish
    from mhwi_equipmentmorph.backend.equipment_morph_package_pipeline import source_baseline
    import tempfile
    checks=[]
    def check(ok,label):
        assert ok,label
        checks.append(label)
    def without_timestamp(model):
        return model[:40]+b'\0'*8+model[48:]
    asset=dict(part='body',package='synthetic-body',collection=collection,model_id='pl/synthetic/body',
        keys=[dict(id='smile',source_key='Smile',author_shading=False)],shading='BASELINE')
    import mhwi_equipmentmorph as addon
    addon.register()
    state=addon.settings(bpy.context);item=state.assets.add();item.collection=collection;item.model_id=asset['model_id']
    addon.refresh_keys(item)
    key=next(k for k in item.keys if k.source=='Smile');key.enabled=True;key.public_id='smile'
    check(len(obj.data.shape_keys.key_blocks)==504,'fixture has 504 source keys')
    check(addon.key_status(item,'neck',bpy.context)=='键值非0：固定','nonzero key shows requested fixed label')
    check(addon.key_status(item,'Unused_0',bpy.context)=='固定/不支持','muted zero key is not mislabeled as nonzero')
    captured_example=dict(name='Controlled',selected_keys={'Smile':[]},keys=[dict(name='Smile',value=0,mute=False)],matrix_world=[],
        legacy_fingerprint=dict(values=[['Basis',0,False],['Smile',0,False]],geometry_and_encoding_sha256='0'*64))
    zero_only=dict(captured_example,name='ZeroOnly',selected_keys={},keys=[],legacy_fingerprint=dict(values=[['Basis',0,False]],geometry_and_encoding_sha256='0'*64))
    example_capture={'objects':[captured_example,zero_only]}
    example_mapping={'objects':[{'object':o['name']} for o in example_capture['objects']]}
    example=source_baseline(asset,example_capture,example_mapping)
    check([o['name'] for o in example]==['Controlled'],'legacy zero-only source objects do not create empty profile records')
    check(len(example_capture['objects'])==2 and zero_only['legacy_fingerprint']['values']==[['Basis',0,False]],'zero-only source capture preserved')
    check(addon.recipe_from_ui(bpy.context)['assets'][0]['collection']==collection,'UI builds collection recipe without MOD3 mapping')
    for typed in ('pl/synthetic/body','pl\\synthetic\\body',' nativePC\\pl\\synthetic\\body.mod3 '):
        check(model_resource_id(typed)=='pl/synthetic/body','resource path normalized '+typed)
    for typed in ('','../pl/synthetic/body','pl/synthetic/../body','C:\\pl\\synthetic\\body'):
        try:model_resource_id(typed)
        except ValueError:check(True,'invalid resource path rejected '+typed)
        else:raise AssertionError('unsafe resource path accepted')
    source=fingerprint(obj);selected=list(bpy.context.selected_objects);active=bpy.context.active_object
    counts=lambda:tuple(len(getattr(bpy.data,k)) for k in ('objects','meshes','collections','armatures','shape_keys'))
    before=counts()
    # Keep nested installer fixtures below Windows' legacy path-length limit.
    with tempfile.TemporaryDirectory(prefix='mhwi-') as folder:
        folder=Path(folder)
        for shading in ('BASELINE','GEOMETRIC'):
            destination=folder/shading;destination.mkdir();sentinel=destination/'keep.txt';sentinel.write_bytes(b'keep unrelated')
            result=export_direct({'assets':[dict(asset,shading=shading,model_id='pl\\synthetic\\body.mod3')]},destination)
            check(sentinel.read_bytes()==b'keep unrelated',shading+' existing selected folder preserves unrelated files')
            check(result['success'],shading+' paired export')
            model=(destination/result['models'][0]['file']).read_bytes();basis,rows=model_meshes(model)
            plain=(output/'plain.mod3').read_bytes()
            check(without_timestamp(model)==without_timestamp(plain),shading+' MOD3 matches unwrapped exporter except its export timestamp')
            traces=json.loads((destination/'authoring/0/body.trace.json').read_bytes())['objects']
            trace=traces[0];indices=trace['disk_to_blender_vertex']
            check([r['stride'] for r in rows]==[32,36,44],'three runtime-proven formats exported in one model '+shading)
            check(len(indices)>8 and {0,4}.issubset(indices),shading+' split and coincident source IDs retained')
            row=rows[0];positions=np.ndarray((row['count'],3),dtype='<f4',buffer=basis,offset=row['start'],strides=(row['stride'],4))
            transform=np.asarray(exporter.exportMatrix@obj.matrix_world)
            expected=np.asarray([tuple(v.co) for v in obj.data.vertices]);expected[1,2]+=.04
            check(np.allclose(positions,expected[indices]@transform[:3,:3].T+transform[:3,3],atol=1e-5),shading+' fixed neck correction baked')
            manifest=destination/'generated/export/pl/synthetic/body/manifest.json';packed,_,_=prepare(manifest)
            check(packed==(destination/'nativePC/pl/synthetic/body.morph/asset.mhmpack').read_bytes(),shading+' pack re-encodes exactly')
            raw=(manifest.parent/'positions-0.bin').read_bytes();records={v[0]:v[1:] for v in struct.iter_unpack('<I3f',raw)}
            for traced,mesh_row in zip(traces,rows):
                source_obj=bpy.data.objects[traced['object']];source_transform=np.asarray(exporter.exportMatrix@source_obj.matrix_world)
                delta=(coords(source_obj.data.shape_keys.key_blocks['Smile'])-coords(source_obj.data.shape_keys.reference_key))@source_transform[:3,:3].T
                for i,source_index in enumerate(traced['disk_to_blender_vertex']):
                    check(np.allclose(records.get(mesh_row['start']+i*mesh_row['stride'],(0,0,0)),delta[source_index],atol=1e-6),shading+' exact delta mesh '+str(mesh_row['mesh'])+' vertex '+str(i))
            description=json.loads((destination/'nativePC/pl/synthetic/body.morph/profile.json').read_bytes())
            check(description['model_sha256']==result['models'][0]['sha256'],shading+' profile bound to paired model')
            values=description['source_objects'][0]['values']
            check(all(len(o['values'])==3 and {v['source_name'] for v in o['values']}=={'Basis','Smile','neck'} for o in description['source_objects']),shading+' runtime profiles retain controlled and nonzero keys only')
            captured=json.loads((destination/'authoring/0/body.capture.json').read_bytes())
            check(all(len(o['legacy_fingerprint']['values'])==504 for o in captured['objects']),shading+' full 504-key capture retained')
            check(all(o['geometry_sha256']==next(c['legacy_fingerprint']['geometry_and_encoding_sha256'] for c in captured['objects'] if c['name']==o['name']) for o in description['source_objects']),shading+' full source fingerprint retained')
            check(any(v['name']=='neck' and v['weight']>.39 and not v['dynamic'] for v in values),shading+' fixed key declared')
            check(any(v['name']=='smile' and v['weight']==0 and v['dynamic'] for v in values),shading+' dynamic baseline declared')
            if shading=='GEOMETRIC':check(packed[:8]==b'MHMPACK2' and description['per_key_normals']['smile']['records']>0,'per-key normals generated on final split topology')
            sys.path.insert(0,str(ROOT/'tools'))
            from prepare_mhwi_equipmentmorph_resource_path import prepare as prepare_resource
            import subprocess
            validator=ROOT/'.build/current-validation/package-validator/EquipmentMorphPackageValidator.exe'
            adjusted=folder/(shading+'-adjusted')
            prepared=prepare_resource(destination,adjusted,'body','pl/rebound/body',validator)
            corrected=(adjusted/'nativePC/pl/rebound/body.morph/asset.mhmpack').read_bytes()
            old_length=struct.unpack_from('<I',packed,28)[0];new_length=struct.unpack_from('<I',corrected,28)[0]
            check(packed[32+old_length:]==corrected[32+new_length:],shading+' path correction preserves complete basis and sparse payloads')
            check((adjusted/'nativePC/pl/rebound/body.mod3').read_bytes()==model,shading+' path correction preserves model bytes')
            repacked,_,_=prepare(adjusted/'generated/export/pl/synthetic/body/manifest.json')
            check(repacked==corrected and prepared['native_check']['native_loader_valid'],shading+' corrected manifest and native profile agree')
            check((destination/result['models'][0]['file']).read_bytes()==model,shading+' correction keeps original export unchanged')
            wrong=folder/(shading+'-wrong.mod3');wrong.write_bytes(b'wrong model')
            try:prepare_resource(destination,folder/(shading+'-rejected'),'body','pl/rebound/body',validator,wrong)
            except ValueError:check(not (folder/(shading+'-rejected')).exists(),shading+' wrong installed model rejects correction')
            else:raise AssertionError('wrong installed target accepted')
            mutated=bytearray(model);mutated[-1]^=1;(adjusted/'nativePC/pl/rebound/body.mod3').write_bytes(mutated)
            rejected_native=subprocess.run([str(validator),str(adjusted/'nativePC/plugins/MHWEquipmentMorph'),str(adjusted/'nativePC')],capture_output=True,text=True)
            check(rejected_native.returncode!=0 and not json.loads(rejected_native.stdout)['native_loader_valid'],shading+' altered model rejected by native checker')
            (adjusted/'nativePC/pl/rebound/body.mod3').write_bytes(model)
            receipt_path=destination/'result.json';receipt_bytes=receipt_path.read_bytes()
            bad_receipt=json.loads(receipt_bytes);bad_receipt['assets'][0]['package']='../../escape'
            receipt_path.write_text(json.dumps(bad_receipt),encoding='utf-8')
            try:
                try:prepare_resource(destination,folder/(shading+'-unsafe-package'),'body','pl/rebound/body',validator)
                except ValueError:check(not (folder/(shading+'-unsafe-package')).exists(),shading+' escaped package receipt rejected')
                else:raise AssertionError('unsafe package accepted')
            finally:receipt_path.write_bytes(receipt_bytes)
            description_path=destination/'nativePC/pl/synthetic/body.morph/profile.json';description_bytes=description_path.read_bytes()
            wrong_part=json.loads(description_bytes);wrong_part['part']=0;description_path.write_text(json.dumps(wrong_part),encoding='utf-8')
            try:
                try:prepare_resource(destination,folder/(shading+'-wrong-part'),'body','pl/rebound/body',validator)
                except ValueError:check(not (folder/(shading+'-wrong-part')).exists(),shading+' inconsistent part receipt rejected')
                else:raise AssertionError('wrong part accepted')
            finally:description_path.write_bytes(description_bytes)
            check(source==fingerprint(obj) and before==counts(),'source and datablock counts restored '+shading)
            check(selected==list(bpy.context.selected_objects) and active==bpy.context.active_object,'selection restored '+shading)
        def rejected(label,action):
            try:action()
            except (ValueError,RuntimeError,OSError):check(True,label)
            else:raise AssertionError(label+' unexpectedly accepted')
            check(before==counts() and source==fingerprint(obj),'failure cleanup '+label)
        previous=(folder/'BASELINE/nativePC/pl/synthetic/body.mod3').read_bytes()
        export_direct({'assets':[asset]},folder/'BASELINE')
        check(without_timestamp((folder/'BASELINE/nativePC/pl/synthetic/body.mod3').read_bytes())==without_timestamp(previous) and (folder/'BASELINE/keep.txt').read_bytes()==b'keep unrelated','repeat export updates selected folder')
        staging=folder/'publish-stage';staging.mkdir();(staging/'a.txt').write_bytes(b'new a');(staging/'b.txt').write_bytes(b'new b')
        target=folder/'publish-target';target.mkdir();(target/'a.txt').write_bytes(b'old a');(target/'keep.txt').write_bytes(b'keep')
        import mhwi_equipmentmorph.output_files as publication
        original_replace=publication.os.replace;calls=[]
        def failing_replace(source,dest):
            calls.append(str(source))
            if len(calls)==3:raise PermissionError('injected publication fault')
            return original_replace(source,dest)
        publication.os.replace=failing_replace
        try:rejected('publication failure rolled back',lambda:publish(staging,target))
        finally:publication.os.replace=original_replace
        check((target/'a.txt').read_bytes()==b'old a' and (target/'keep.txt').read_bytes()==b'keep' and not (target/'b.txt').exists(),'publication restores original files')
        (staging/'a.txt').write_bytes(b'new a')
        blocked=folder/'blocked';blocked.mkdir();(blocked/'a.txt').mkdir()
        rejected('file versus folder collision rejected',lambda:publish(staging,blocked))
        obj.data.shape_keys.key_blocks['Smile'].value=.5
        try:
            try:export_direct({'assets':[asset]},folder/'nonzero')
            except ValueError:check(not (folder/'nonzero').exists(),'nonzero dynamic key rejected before publish')
            else:raise AssertionError('nonzero accepted')
        finally:obj.data.shape_keys.key_blocks['Smile'].value=0
        modifier=obj.modifiers.new('Unsupported','MIRROR')
        try:
            try:export_direct({'assets':[asset]},folder/'mirror')
            except ValueError:check(not (folder/'mirror').exists(),'topology modifier rejected')
            else:raise AssertionError('mirror accepted')
        finally:obj.modifiers.remove(modifier)
        sibling=bpy.data.objects['Group_1_Sub_0__Synthetic'];extra=sibling.data.uv_layers.new(name='UnsupportedUV2')
        for target,original_uv in zip(extra.data,sibling.data.uv_layers[0].data):target.uv=original_uv.uv
        try:rejected('unknown runtime format rejected',lambda:export_direct({'assets':[asset]},folder/'unknown-format'))
        finally:sibling.data.uv_layers.remove(extra)
        original=exporter.parseVertexDict
        def fault(*args):raise RuntimeError('injected serializer fault')
        exporter.parseVertexDict=fault
        try:rejected('mid-export failure cleans clones',lambda:export_direct({'assets':[asset]},folder/'fault'))
        finally:exporter.parseVertexDict=original
        check(not (folder/'fault').exists() and not list(folder.glob('morph-direct-stage-*')),'failed export leaves no partial output')
        empty=folder/'empty-config';empty.mkdir()
        (empty/'runtime.ini').write_text('[runtime]\nformat=2\n')
        empty_check=subprocess.run([str(validator),str(empty),str(folder)],capture_output=True,text=True)
        check(empty_check.returncode!=0 and 'No model sidecars found' in json.loads(empty_check.stdout)['error'],'empty configuration cannot falsely pass native checker')
        from mhwi_equipmentmorph.output_files import retarget_model
        source_id='pl/f_equip/pl105_0000/body/mod/f_body105_0000'
        target_id='pl/f_equip/pl001_0000/body/mod/f_body001_0000'
        check(retarget_model(source_id,'001')==target_id,'single equipment ID changes directory and model basename')
        rejected('ambiguous equipment identity rejected',lambda:retarget_model('pl/f_equip/pl105_0000/body/mod/f_body001_0000','001'))
        rejected('invalid equipment ID rejected',lambda:retarget_model(source_id,'1'))
        bpy.data.collections.remove(collection)
        collection,obj=fixture();asset=dict(asset,collection=collection)
        exporter.exportMHWMod3File(str(folder/'prime.mod3'),dict(options,targetCollection=collection))
        other=bpy.data.collections.new('SecondSynthetic.mod3');bpy.context.scene.collection.children.link(other)
        other.objects.link(obj);other.objects.link(next(o for o in collection.all_objects if o.type=='ARMATURE'))
        multi=folder/'multiple-equipment'
        export_direct({'assets':[dict(asset,model_id=source_id,equipment_id='001'),dict(asset,collection=other,model_id=source_id)]},multi)
        report=subprocess.run([str(validator),str(multi/'nativePC/plugins/MHWEquipmentMorph'),str(multi/'nativePC')],capture_output=True,text=True)
        native=json.loads(report.stdout)
        check(report.returncode==0 and len(native['parts'])==2 and all(p['part']==1 for p in native['parts']),'two same-part equipment models discovered without manual selection')
        check(len({p['vertex_bytes'] for p in native['parts']})==2,'multiple equipment fixture has different buffer sizes')
        check((multi/'nativePC'/(target_id+'.morph/profile.json')).is_file(),'package basename follows single-ID export')
        moved=folder/'single-id-copy'
        single=folder/'single-id-source';export_direct({'assets':[dict(asset,model_id=source_id)]},single)
        translated=prepare_resource(single,moved,'body',None,validator,equipment_id='001')
        check(translated['model_id']==target_id and (moved/'nativePC'/(target_id+'.morph/asset.mhmpack')).is_file(),'single-ID migration updates model and sidecar together')
        check((single/'nativePC'/(source_id+'.mod3')).read_bytes()==(moved/'nativePC'/(target_id+'.mod3')).read_bytes(),'equipment migration leaves MOD3 bytes unchanged')
        mismatch=moved/'nativePC'/(target_id+'.morph');mismatch.rename(mismatch.with_name('wrong.morph'))
        report=subprocess.run([str(validator),str(moved/'nativePC/plugins/MHWEquipmentMorph'),str(moved/'nativePC')],capture_output=True,text=True)
        check(report.returncode!=0 and 'Sidecar directory differs' in json.loads(report.stdout)['error'],'misnamed sidecar cannot bind by hash alone')
        import manage_equipment_morph_mod as management
        from unittest.mock import patch
        game=folder/'game';game.mkdir();(game/'MonsterHunterWorld.exe').write_bytes(b'isolated directory marker')
        planned=management.plan(multi,game,validator)
        check(not planned['conflicts'] and not (game/'nativePC').exists(),'installation plan is read-only')
        with patch.object(management,'closed'):
            installed=management.install(multi,game,validator)
            check(installed['installed_check']['native_loader_valid'] and len(installed['installed_check']['parts'])==2,'paired installation checks complete multi-model catalogue')
            check(all(f['status']=='identical' for f in management.plan(multi,game,validator)['files']),'identical paired installation detected')
            conflicting=management.plan(single,game,validator)
            check(len(conflicting['conflicts'])==4,'different MOD at same resource path identifies all four paired conflicts')
            original={f['relative']:(game/f['relative']).read_bytes() for f in planned['files']}
            try:management.install(single,game,validator)
            except ValueError:check(all((game/p).read_bytes()==data for p,data in original.items()),'unapproved replacement preserves existing MOD and packages')
            else:raise AssertionError('conflicting MOD overwritten without replace')
            real_verify=management.verify
            def fail_after_write(v,root):
                if Path(root)==game:raise ValueError('injected post-install validation failure')
                return real_verify(v,root)
            with patch.object(management,'verify',fail_after_write):
                try:management.install(single,game,validator,replace=True)
                except ValueError:check(all((game/p).read_bytes()==data for p,data in original.items()),'failed final check rolls back all paired replacements')
                else:raise AssertionError('injected final check failure ignored')
            installed=management.install(single,game,validator,replace=True)
            check(installed['installed_check']['native_loader_valid'] and Path(installed['receipt']).is_file(),'explicit pair replacement retains backup receipt')
            check(len(installed['installed_check']['parts'])==2,'replacement preserves other equipment package')
        with patch.object(management,'closed',side_effect=ValueError('game is running')):
            try:management.install(single,game,validator,replace=True)
            except ValueError:check(True,'running game blocks MOD installation')
            else:raise AssertionError('running game accepted')
        fresh=folder/'failed-fresh-game';fresh.mkdir();(fresh/'MonsterHunterWorld.exe').write_bytes(b'isolated directory marker')
        def reject_fresh(v,root):
            if Path(root)==fresh:raise ValueError('injected new installation failure')
            return real_verify(v,root)
        with patch.object(management,'closed'),patch.object(management,'verify',reject_fresh):
            try:management.install(single,fresh,validator)
            except ValueError:check(not (fresh/'nativePC/pl').exists(),'failed new installation removes created sidecar directories')
            else:raise AssertionError('new installation failure ignored')
        linked=folder/'linked-game';linked.mkdir();(linked/'MonsterHunterWorld.exe').write_bytes(b'isolated directory marker');(linked/'nativePC').mkdir()
        import os
        junction=subprocess.run(['powershell','-NoProfile','-NonInteractive','-Command',
            'New-Item -ItemType Junction -Path $env:MHWI_TEST_LINK -Target $env:MHWI_TEST_TARGET | Out-Null'],
            env=dict(os.environ,MHWI_TEST_LINK=str(linked/'nativePC/pl'),MHWI_TEST_TARGET=str(single/'nativePC/pl')),capture_output=True)
        check(junction.returncode==0,'real linked MOD directory fixture created')
        try:management.plan(single,linked,validator)
        except ValueError:check(True,'linked MOD destination rejected before writing')
        else:raise AssertionError('linked destination accepted')
        metadata=linked/'nativePC/plugins/MHWEquipmentMorph';metadata.mkdir(parents=True);(metadata/'runtime.ini').write_text('[runtime]\nformat=2\n')
        report=subprocess.run([str(validator),str(metadata),str(linked/'nativePC')],capture_output=True,text=True)
        check(report.returncode!=0 and 'linked' in json.loads(report.stdout)['error'].lower(),'native catalogue also rejects linked model roots')
        bpy.data.collections.remove(other)
    addon.unregister()
    report=dict(schema='mhwi-direct-export-checks-v1',passed=len(checks),checks=checks,blender=bpy.app.version_string,
        real_mod_project_used=False,blend_saved=False,formal_acceptance_complete=False)
    (output/'checks.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('PASS: '+str(len(checks))+' direct Blender export checks')
