"""Synthetic cancellation regression; no private model or game installation."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import numpy as np
import bpy

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'blender_addon_v044'))
from mhwi_equipmentmorph.direct_export import export_model
from mhwi_equipmentmorph.backend.capture_equipment_morph_job import fingerprint
from mhwi_equipmentmorph.normal_authoring import unit
from mhwi_equipmentmorph.geometric_normals import transported

fixture=ROOT/'tools/blender_export_fixture.py'
namespace={'__file__':str(fixture)}
exec(compile(fixture.read_text(encoding='utf-8').split('collection,obj=fixture()')[0],str(fixture),'exec'),namespace)
collection,obj=namespace['fixture']()
for sibling in list(collection.all_objects):
    if sibling.type=='MESH' and sibling!=obj:bpy.data.objects.remove(sibling,do_unlink=True)
mesh=bpy.data.meshes.new('OppositeFaces')
mesh.from_pydata([(0,0,0),(1,0,0),(0,1,0),(0,1,.1)],[],[(0,1,2),(0,3,1)])
obj.data=mesh
mesh.materials.append(bpy.data.materials.new('OppositeFaces'))
uv=mesh.uv_layers.new(name='UVMap')
for loop in mesh.loops:uv.data[loop.index].uv=mesh.vertices[loop.vertex_index].co.xy
for polygon in mesh.polygons:polygon.use_smooth=True
mesh.normals_split_custom_set([(0,0,1)]*len(mesh.loops))
obj.vertex_groups.new(name='MhBone_000').add(list(range(4)),1.,'REPLACE')
obj.shape_key_add(name='Basis')
key=obj.shape_key_add(name='Smile',from_mix=False)
key.data[2].co.z=.2;key.data[3].co.z=.2
key.relative_key=mesh.shape_keys.reference_key;key.value=0;key.mute=False;key.vertex_group=''
obj.active_shape_key_index=0
bpy.context.view_layer.update()
before=fingerprint(obj)
source_normals=np.asarray([tuple(n.vector) for n in mesh.corner_normals])
assert np.isfinite(source_normals).all() and np.all(np.linalg.norm(source_normals,axis=1)>0)
source_positions=np.asarray([tuple(v.co) for v in mesh.vertices])
folder=Path(tempfile.mkdtemp(prefix='geometric-cancellation-',dir=ROOT/'.build/capacity-validation'))
asset=dict(part='body',collection=collection,model_id='pl/f_equip/pl001_0000/body/mod/f_body001_0000',
           package='f_body001_0000',shading='GEOMETRIC',keys=[dict(id='smile',source_key='Smile',author_shading=False)])
checks=[]
def check(condition,label):checks.append(dict(check=label,passed=bool(condition)))
def rejected(call,label,expected=''):
    try:call()
    except ValueError as error:check(expected in str(error),label+': '+str(error))
    else:check(False,label)

rejected(lambda:export_model(namespace['exporter'],asset,folder/'strict.mod3'),'default policy rejects cancelling geometric directions','法线')
rejected(lambda:export_model(namespace['exporter'],dict(asset,undefined_normal_policy='TYPO'),folder/'bad-policy.mod3'),'unknown normal policy rejected','策略')
for name,height,reason in [('endpoint',0,'endpoint_zero'),('quantized_baseline',100000000,'baseline_and_endpoint_zero')]:
    obj.location.z=height;bpy.context.view_layer.update()
    try:
        trace,normals=export_model(namespace['exporter'],dict(asset,undefined_normal_policy='BASELINE'),folder/(name+'.mod3'))
    except ValueError as error:
        check(False,name+' explicit baseline policy exports all selected keys: '+str(error))
    else:
        exceptions=trace.get('geometric_normal_fallbacks',[])
        check({item['source_vertex'] for item in exceptions}=={0,1},name+' only the two cancelling source vertices fall back')
        check(bool(exceptions) and all(item['key_id']=='smile' and item['reason']==reason for item in exceptions),name+' fallback report identifies exact key and cause')
        check(trace.get('undefined_normal_policy')=='BASELINE',name+' trace declares explicit normal policy')
        check(all(item['object']==obj.name and item['corner']>=0 and item['mesh']==0 for item in exceptions),name+' fallback report identifies object mesh and corner')
        offsets={struct.unpack_from('<I',normals['smile'],i)[0] for i in range(0,len(normals['smile']),16)}
        check(not offsets.intersection({item['normal_offset'] for item in exceptions}),name+' undefined directions preserve serialized authored normal bytes')
        if name=='endpoint':check(bool(offsets),name+' well-defined vertices still receive geometric normal changes')
        check(all(np.isfinite(struct.unpack_from('<3f',normals['smile'],i+4)).all() for i in range(0,len(normals['smile']),16)),name+' all emitted direction deltas finite')
obj.location.z=0;bpy.context.view_layer.update()

flip_mesh=bpy.data.meshes.new('DefinedAntiparallel')
flip_mesh.from_pydata([(0,0,0),(1,0,0),(0,1,0)],[],[(0,1,2)])
flip=bpy.data.objects.new('Group_0_Sub_1__Flip',flip_mesh);collection.objects.link(flip)
flip_mesh.materials.append(bpy.data.materials.new('Flip'))
flip_uv=flip_mesh.uv_layers.new(name='UVMap')
for loop in flip_mesh.loops:flip_uv.data[loop.index].uv=flip_mesh.vertices[loop.vertex_index].co.xy
flip.vertex_groups.new(name='MhBone_000').add([0,1,2],1.,'REPLACE')
flip.modifiers.new('RestRig','ARMATURE').object=obj.modifiers[0].object
flip.shape_key_add(name='Basis');flip_key=flip.shape_key_add(name='Flip',from_mix=False)
flip_key.relative_key=flip_mesh.shape_keys.reference_key;flip_key.value=0
flip_key.data[1].co.x=-1
bpy.context.view_layer.update();flip_before=fingerprint(flip)
flip_asset=dict(asset,keys=[dict(id='flip',source_key='Flip',author_shading=False)])
rejected(lambda:export_model(namespace['exporter'],dict(flip_asset,undefined_normal_policy='BASELINE'),folder/'flip-strict.mod3'),'zero rule alone does not allow defined antiparallel endpoints','反向')
rejected(lambda:export_model(namespace['exporter'],dict(flip_asset,antiparallel_normal_policy='TYPO'),folder/'flip-typo.mod3'),'unknown antiparallel policy rejected','策略')
try:
    trace,normals=export_model(namespace['exporter'],dict(flip_asset,antiparallel_normal_policy='BASELINE'),folder/'flip-baseline.mod3')
except ValueError as error:
    check(False,'independent antiparallel baseline policy exports: '+str(error))
else:
    exceptions=trace.get('geometric_normal_fallbacks',[])
    check(len(exceptions)==3 and {item['source_vertex'] for item in exceptions}=={0,1,2},'exactly three flipped vertices use baseline')
    check(all(item['reason']=='antiparallel_endpoint' and item['key_id']=='flip' and item['object']==flip.name for item in exceptions),'antiparallel exceptions carry exact reason key and object')
    check(trace.get('antiparallel_normal_policy')=='BASELINE' and trace.get('undefined_normal_policy')=='REJECT','antiparallel option independent of zero-direction option')
    check(normals['flip']==b'','antiparallel policy preserves authored normal bytes')
check(fingerprint(flip)==flip_before,'antiparallel export preserves synthetic source')

rejected(lambda:unit([[float('nan'),0,1]]),'nonfinite normals remain rejected')
rejected(lambda:transported((0,0,1),(0,0,1),(0,0,-1)),'defined antiparallel normals remain rejected')
check(before==fingerprint(obj),'source keys geometry and attributes unchanged')
check(np.array_equal(source_normals,np.asarray([tuple(n.vector) for n in mesh.corner_normals])),'source authored normals unchanged')
check(np.array_equal(source_positions,np.asarray([tuple(v.co) for v in mesh.vertices])),'source vertex coordinates unchanged')
check(not bpy.data.collections.get('clonedMod3Meshes'),'exporter temporary collection cleaned after rejection and success')
report=dict(passed=sum(item['passed'] for item in checks),total=len(checks),checks=checks,
            private_model_used=False,game_files_changed=False,blend_saved=False,test_directory=str(folder))
(ROOT/'.build/capacity-validation/geometric-fallback-checks.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
for item in checks:
    if not item['passed']:print('FAIL: '+item['check'])
assert all(item['passed'] for item in checks),report
print('PASS: '+str(len(checks))+' geometric cancellation and explicit baseline checks')
