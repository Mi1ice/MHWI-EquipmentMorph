"""Real pinned exporter: fourteen layouts, UV/color seams and source preservation."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import struct
import json
from authoring_test_support import *
from mhwi_equipmentmorph import direct_export as direct
from mhwi_equipmentmorph.backend.map_equipment_morph_job import model_meshes
from mhwi_equipmentmorph.backend.vertex_formats import FORMATS

fixture,exporter=load_fixture()
addon.register()
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
layouts=[(0xa756f2f9,1,0,False),(0xa5104ca0,2,0,False),(0x818904dc,1,0,True),
 (0x0f06033f,2,0,True),(0xc3ac03a1,3,0,True),(0xc9690ab8,4,0,True),
 (0xf637401c,1,4,False),(0xf471fe45,2,4,False),(0x3c730760,1,4,True),
 (0xb2fc0083,2,4,True),(0x81f58067,1,8,False),(0x83b33e3e,2,8,False),
 (0x366995a7,1,8,True),(0xb8e69244,2,8,True)]
collections=[];objects={};expected={}
for skin in (False,True):
    collection=bpy.data.collections.new('Skin.mod3' if skin else 'Plain.mod3');bpy.context.scene.collection.children.link(collection);collections.append(collection)
    rig=None
    if skin:
        data=bpy.data.armatures.new('Rig');rig=bpy.data.objects.new('Rig',data);collection.objects.link(rig)
        bpy.context.view_layer.objects.active=rig;rig.select_set(True);bpy.ops.object.mode_set(mode='EDIT')
        for i in range(9):
            bone=data.edit_bones.new('MhBone_'+str(i).zfill(3));bone.head=(0,0,0);bone.tail=(0,0,1)
        bpy.ops.object.mode_set(mode='OBJECT');rig.select_set(False)
    for index,(code,uvs,weights,color) in enumerate(layouts):
        if bool(weights)!=skin:continue
        mesh=bpy.data.meshes.new('Mesh');mesh.from_pydata([(0,0,0),(1,0,0),(1,1,0),(0,1,0)],[],[(0,1,2),(0,2,3)])
        obj=bpy.data.objects.new('Group_'+str(index)+'_Sub_0__F'+hex(code),mesh);collection.objects.link(obj);obj.location.x=index*2
        mesh.materials.append(bpy.data.materials.new('F'+hex(code)))
        for u in range(uvs):
            layer=mesh.uv_layers.new(name='UV'+str(u))
            for i,entry in enumerate(layer.data):
                x,y=mesh.vertices[mesh.loops[i].vertex_index].co[:2]
                entry.uv=(x*.5+u*.125,y*.5+u*.0625)
            if u>0:layer.data[3].uv.x+=.125 # seam absent from UV1
        if uvs>1:mesh.uv_layers.active_index=1
        if color:
            attr=mesh.color_attributes.new(name='Color',type='BYTE_COLOR',domain='CORNER')
            for i,entry in enumerate(attr.data):entry.color=(1,0,0,1) if i<3 else (0,1,0,1)
        if weights:
            values=[.4,.3,.2,.1] if weights==4 else [.2,.18,.16,.14,.12,.1,.06,.04]
            for i,value in enumerate(values):obj.vertex_groups.new(name='MhBone_'+str(i).zfill(3)).add(list(range(4)),value,'REPLACE')
            mod=obj.modifiers.new('Rest','ARMATURE');mod.object=rig
        obj.shape_key_add(name='Basis');key=obj.shape_key_add(name='Smile',from_mix=False);key.relative_key=mesh.shape_keys.reference_key;key.value=0;key.data[1].co.z=.1
        objects[obj.name]=obj;expected[obj.name]=code
    # A genuinely fixed mesh participates in whole-model format checking.
    src=next(o for o in collection.objects if o.type=='MESH');fixed=src.copy();fixed.data=src.data.copy();fixed.shape_key_clear();fixed.name='Group_30_Sub_0__Fixed';fixed.location.y=3;collection.objects.link(fixed)
    objects[fixed.name]=fixed;expected[fixed.name]=expected[src.name]

def snapshot():
    return dict(scene=scene(),attributes={name:dict(uvs=[[(tuple(v.uv)) for v in layer.data] for layer in obj.data.uv_layers],active=obj.data.uv_layers.active_index,
        colors=[(a.name,a.data_type,a.domain,[tuple(v.color) for v in a.data]) for a in obj.data.color_attributes],groups=[[(g.group,g.weight) for g in v.groups] for v in obj.data.vertices]) for name,obj in objects.items()})
bpy.context.view_layer.update()
before=snapshot();runs=[]
for shading in ('BASELINE','GEOMETRIC'):
    for debug in (False,True):
        assets=[]
        for i,c in enumerate(collections):assets.append(dict(collection=c,part='body',model_id='pl/test/body'+str(i),package='test',shading=shading,keys=[dict(id='Smile',source_key='Smile',author_shading=False)]))
        out=ARGS.output/(shading+str(debug));traces={}
        direct.export_direct(dict(assets=assets),out,debug,lambda i,a,t:traces.update({i:t}))
        seen=set()
        for i,trace in traces.items():
            raw=(out/'nativePC'/('pl/test/body'+str(i)+'.mod3')).read_bytes();basis,rows=model_meshes(raw)
            check(len(trace['serialized_objects'])==len(rows),'all dynamic and fixed meshes traced '+str((shading,debug,i)))
            for serial,row in zip(trace['serialized_objects'],rows):
                obj=objects[serial['object']];code=expected[obj.name];seen.add(code)
                check(row['format']==code,'real target format '+hex(code)+str((shading,debug)))
                f=FORMATS[code];fields={x['field']:x for x in f['fields']}
                for vi,corner in enumerate(serial['disk_to_source_corner']):
                    at=row['start']+vi*row['stride']
                    for u,layer in enumerate(obj.data.uv_layers):
                        x,y=layer.data[corner].uv;half_y=struct.unpack('<e',struct.pack('<e',y))[0]
                        want=struct.pack('<ee',x,1-half_y);offset=fields['UV'+str(u+1)]['offset']
                        check(basis[at+offset:at+offset+4]==want,'encoded UV'+str(u+1)+' '+obj.name)
                    if f['color']:
                        offset=fields['Color']['offset'];want=bytes(int(v*255) for v in obj.data.vertex_colors[0].data[corner].color)
                        check(basis[at+offset:at+offset+4]==want,'encoded color '+obj.name)
                    if f['weight_slots']:
                        offset=fields['Bone']['offset'];n=f['weight_slots'];check(basis[at+offset:at+offset+n]==bytes(range(n)),'bone indices '+obj.name)
        check(seen=={v[0] for v in layouts},'all fourteen produced '+str((shading,debug)))
        after=snapshot()
        if after!=before:
            (ARGS.output/'state-difference.json').write_text(json.dumps(dict(before=before,after=after),indent=2),encoding='utf-8')
        check(after==before,'full source attributes and active UV unchanged '+str((shading,debug)))
        runs.append(str(out))

# Active layer 1 all zero while layer 0 remains valid: the original failure.
obj=next(o for o in objects.values() if len(o.data.uv_layers)==2 and o.data.shape_keys)
for item in obj.data.uv_layers[1].data:item.uv=(0,0)
asset=dict(collection=obj.users_collection[0],part='body',model_id='pl/test/uvzero',package='uvzero',shading='BASELINE',keys=[dict(id='Smile',source_key='Smile',author_shading=False)])
active=obj.data.uv_layers.active_index;direct.export_direct(dict(assets=[asset]),ARGS.output/'active-zero')
check(obj.data.uv_layers.active_index==active,'active zero UV layer preserved')

# Invalid attributes must be rejected before staging and keep old destination bytes.
out=ARGS.output/'refused';out.mkdir();(out/'keep').write_bytes(b'old')
for case in ('extra-color','point-color','float-color','extra-uv','ninth-weight'):
    target=next(o for o in objects.values() if o.data.shape_keys and (len(o.vertex_groups)==8 if case=='ninth-weight' else len(o.vertex_groups)==4))
    asset=dict(collection=target.users_collection[0],part='body',model_id='pl/test/refused',package='refused',shading='BASELINE',keys=[dict(id='Smile',source_key='Smile',author_shading=False)])
    attribute=None;layer=None;group=None
    if 'color' in case:
        if case=='extra-color' and not target.data.color_attributes:target.data.color_attributes.new(name='First',type='BYTE_COLOR',domain='CORNER')
        attribute=target.data.color_attributes.new(name='Unsaveable',type='FLOAT_COLOR' if case=='float-color' else 'BYTE_COLOR',domain='POINT' if case=='point-color' else 'CORNER')
    elif case=='extra-uv':
        while len(target.data.uv_layers)<3:layer=target.data.uv_layers.new(name='Extra')
    else:group=target.vertex_groups.new(name='MhBone_008');group.add(list(range(4)),.01,'REPLACE')
    changed=snapshot();message=rejected(lambda:direct.export_direct(dict(assets=[asset]),out),case)
    check(target.name in message,'object named '+case)
    check(snapshot()==changed and (out/'keep').read_bytes()==b'old' and not list(out.glob('morph-direct-stage-*')),'refusal restores data and output '+case)
    if attribute:target.data.color_attributes.remove(attribute)
    if layer:target.data.uv_layers.remove(layer)
    if group:target.vertex_groups.remove(group)
addon.unregister();report('vertex-formats',runs=runs,formats=14,exporter_path=str(Path(exporter.__file__).resolve()))
