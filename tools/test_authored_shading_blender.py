"""Independent source-corner shading oracles; no private assets or game calls."""
import bpy
import json
from pathlib import Path
import struct
import sys
import tempfile
import ctypes
import numpy as np
from mathutils import Matrix

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'blender_addon_v044'))
from mhwi_equipmentmorph.direct_export import export_model
from mhwi_equipmentmorph.backend.capture_equipment_morph_job import fingerprint
from mhwi_equipmentmorph.backend.map_equipment_morph_job import model_meshes

fixture=ROOT/'tools/blender_export_fixture.py';namespace={'__file__':str(fixture)}
exec(compile(fixture.read_text(encoding='utf-8').split('collection,obj=fixture()')[0],str(fixture),'exec'),namespace)
collection,obj=namespace['fixture']()
for sibling in list(collection.all_objects):
    if sibling.type=='MESH' and sibling!=obj:bpy.data.objects.remove(sibling,do_unlink=True)
mesh=bpy.data.meshes.new('AuthoredCornerOracle')
# Two flat surfaces share vertices and UVs. A separate UV seam forces the
# editor's UV split path, which historically smooths away authored flat edges.
mesh.from_pydata([(0,0,0),(2,0,0),(2,2,0),(0,2,0),(0,2,2),(0,0,2)],[],[(0,1,2,3),(0,3,4,5)])
obj.data=mesh;mesh.materials.append(bpy.data.materials.new('AuthoredCornerOracle'))
uv=mesh.uv_layers.new(name='UVMap')
for face in mesh.polygons:
    for j,loop in enumerate(face.loop_indices):uv.data[loop].uv=[(0,0),(1,0),(1,1),(0,1)][j]
mesh.polygons.foreach_set('use_smooth',[False]*len(mesh.polygons))
color=mesh.color_attributes.new(name='Color',type='BYTE_COLOR',domain='CORNER')
for i,item in enumerate(color.data):item.color=(1,.5,.25,1)
obj.vertex_groups.new(name='MhBone_000').add(list(range(6)),1.,'REPLACE')
obj.shape_key_add(name='Basis');key=obj.shape_key_add(name='Smile');key.data[2].co.z=.2;key.value=0
asset=dict(part='body',collection=collection,model_id='pl/f_equip/pl001_0000/body/mod/f_body001_0000',
    package='f_body001_0000',shading='BASELINE',keys=[dict(id='smile',source_key='Smile',author_shading=False)])
folder=Path(tempfile.mkdtemp(prefix='authored-shading-',dir=ROOT/'.build/capacity-validation'))
checks=[]
def check(value,label):checks.append(dict(check=label,passed=bool(value)))

for label,matrix in [('identity',Matrix.Identity(4)),('nonuniform',Matrix.Rotation(.37,4,'Z')@Matrix.Diagonal((2.,.7,1.3,1.))),
                     ('mirror',Matrix.Diagonal((-1.2,.8,1.4,1.)))]:
    obj.matrix_world=matrix;bpy.context.view_layer.update()
    before=fingerprint(obj);source_normals=np.asarray([tuple(n.vector) for n in mesh.corner_normals],np.float64)
    world=np.asarray(namespace['exporter'].exportMatrix@obj.matrix_world,dtype=np.float64)[:3,:3]
    normals=source_normals@np.linalg.inv(world);normals/=np.linalg.norm(normals,axis=1)[:,None]
    # Match only source vertex and encoded first UV. Expected directions come
    # from the independently read flat source corners, never the export trace.
    expected=set()
    for loop in mesh.loops:
        tex=struct.pack('<2e',float(uv.data[loop.index].uv.x),1-float(uv.data[loop.index].uv.y))
        expected.add((loop.vertex_index,tex,tuple((np.floor(normals[loop.index]*127).astype(int)&255).tolist())))
    path=folder/(label+'.mod3');trace,_=export_model(namespace['exporter'],asset,path)
    basis,rows=model_meshes(path.read_bytes());seen=set();valid_codec=True
    for mapped,row in zip(trace['serialized_objects'],rows):
        for i,vertex in enumerate(mapped['disk_to_blender_vertex']):
            at=row['start']+i*row['stride'];normal=tuple(basis[at+12:at+15]);tex=basis[at+20:at+24]
            seen.add((vertex,tex,normal))
            n=np.asarray(struct.unpack_from('<3b',basis,at+12),np.float64)/127
            t=np.asarray(struct.unpack_from('<3b',basis,at+16),np.float64)/127
            codec_error=np.sqrt(3)/127
            valid_codec &= abs(np.linalg.norm(n)-1)<=codec_error and abs(np.linalg.norm(t)-1)<=codec_error+.001
            valid_codec &= abs(np.dot(n,t))<=2*codec_error+3/127**2+.001
            valid_codec &= basis[at+19] in (127,129)
    check(expected==seen,label+' every source corner retains its independently encoded authored normal and UV')
    check(valid_codec,label+' encoded normal tangent unit orthogonal handedness within analytical codec rounding bounds')
    check(before==fingerprint(obj),label+' source geometry keys UV colors and binding unchanged')
    trace2,_=export_model(namespace['exporter'],asset,folder/(label+'-repeat.mod3'))
    other,_=model_meshes((folder/(label+'-repeat.mod3')).read_bytes())
    check(basis==other,label+' repeated baseline vertex buffer exact')
    check(not bpy.data.collections.get('clonedMod3Meshes'),label+' temporary collection removed')

try:
    from mhwi_equipmentmorph import authored_shading as shading
except ImportError:
    check(False,'authored shading validation module available')
else:
    bpy.context.view_layer.update()
    temporary=mesh.copy()
    try:
        state=shading.capture(temporary)
        check(len(state['normals'])==len(mesh.loops),'capture owns every source corner')
        try:shading.capture(temporary)
        except ValueError:check(True,'reserved source-corner attribute collision rejected')
        else:check(False,'reserved source-corner attribute collision rejected')
    finally:bpy.data.meshes.remove(temporary)
    for values,label in [(np.array([[float('nan'),0,0]]),'nonfinite'),(np.zeros((1,3)),'zero')]:
        try:shading.tangents(np.zeros((3,3)),np.broadcast_to(values,(3,3)),np.zeros((3,2)))
        except ValueError:check(True,label+' tangent inputs rejected')
        else:check(False,label+' tangent inputs rejected')

    positions=np.array([[0,0,0],[1,0,0],[0,1,0]],np.float32);normals=np.tile([0,0,1],(3,1))
    standard=np.array([[0,0],[1,0],[0,1]],np.float32)
    for sign in (1,-1):
        tex=standard.copy();tex[:,0]*=sign
        tangent,exceptions=shading.tangents(positions,normals,tex)
        check(np.array_equal(tangent,np.tile([sign,0,0,sign],(3,1))) and not exceptions,'known planar U tangent and mirrored UV handedness '+str(sign))
    cancelled_positions=np.array([[0,0,0],[1,0,0],[0,1,0],[1,0,0],[0,0,0],[0,1,0]],np.float32)
    cancelled_uv=np.array([[0,0],[1,1],[0,1],[1,1],[0,0],[1,0]],np.float32)
    directions,exceptions=shading.tangents(cancelled_positions,np.tile([1,0,0],(6,1)),cancelled_uv)
    check({e['corner'] for e in exceptions}=={2,5},'cancelling Mikk groups report exactly the defined triangle differential corners')
    check(np.array_equal(directions[2,:3],[0,-1,0]) and np.array_equal(directions[5,:3],[0,1,0]),'cancelled group uses independently known triangle U derivative without arbitrary axis')
    check(np.array_equal(np.linalg.norm(directions[:,:3],axis=1),np.ones(6)) and np.all(directions[:,0]==0),'cancelled group corner tangents are unit and orthogonal')
    try:shading.tangents(positions,np.tile([1,0,0],(3,1)),np.zeros((3,2)))
    except ValueError:check(True,'undefined UV and undefined tangent direction explicitly rejected')
    else:check(False,'undefined UV and undefined tangent direction explicitly rejected')
    for a,b,c in [(positions[:2],normals[:2],standard[:2]),(positions,normals,standard[:2]),(positions,normals,np.full((3,2),float('inf')))]:
        try:shading.tangents(a,b,c)
        except ValueError:check(True,'invalid corner count UV layout or nonfinite UV rejected')
        else:check(False,'invalid corner count UV layout or nonfinite UV rejected')
    function=shading.native()[1];pointer=ctypes.POINTER(ctypes.c_float)
    check(function(0,None,None,None,None)==1 and function(3000003,None,None,None,None)==1,'native count and null pointers rejected before allocation')
    zero=np.zeros((3,3),np.float32);result=np.empty((3,4),np.float32)
    check(function(3,*[a.ctypes.data_as(pointer) for a in (positions,zero,standard,result)])==1,'native zero normal input rejected independently')
    native_path=shading.NATIVE
    try:
        missing=folder/'missing-native';shading.NATIVE=missing;shading.native.cache_clear()
        try:shading.native()
        except ValueError:check(True,'missing native component explicitly rejected')
        else:check(False,'missing native component explicitly rejected')
        missing.mkdir();manifest=json.loads((native_path/'build.json').read_bytes());manifest['schema']='wrong-schema'
        (missing/'build.json').write_text(json.dumps(manifest),encoding='utf-8')
        # A valid DLL is copied only to let schema validation stand on its own.
        (missing/manifest['file']).write_bytes((native_path/manifest['file']).read_bytes());shading.native.cache_clear()
        try:shading.native()
        except ValueError:check(True,'wrong native component schema rejected even with matching DLL')
        else:check(False,'wrong native component schema rejected even with matching DLL')
    finally:shading.NATIVE=native_path;shading.native.cache_clear()
    original_sharp=namespace['exporter'].splitSharpEdges
    for attribute,kind in [(shading.CORNER,'missing'),(shading.CORNER,'out-of-range'),('UVMap','UV-change')]:
        def corrupt(clone,*args):
            original_sharp(clone,*args)
            if kind=='missing':clone.data.attributes.remove(clone.data.attributes[attribute])
            elif kind=='out-of-range':clone.data.attributes[attribute].data[0].value=2147483647
            else:clone.data.uv_layers[0].data[0].uv=(999,999)
        namespace['exporter'].splitSharpEdges=corrupt;before=fingerprint(obj)
        counts=(len(bpy.data.objects),len(bpy.data.meshes),len(bpy.data.collections))
        try:
            try:export_model(namespace['exporter'],asset,folder/(kind+'.mod3'))
            except ValueError:check(True,kind+' preparation failure cannot be swallowed by editor warning handler')
            else:check(False,kind+' preparation failure cannot be swallowed by editor warning handler')
        finally:namespace['exporter'].splitSharpEdges=original_sharp
        check(before==fingerprint(obj) and counts==(len(bpy.data.objects),len(bpy.data.meshes),len(bpy.data.collections)),kind+' rejection cleans all disposable data and preserves source')

report=dict(passed=sum(x['passed'] for x in checks),total=len(checks),checks=checks,private_model_used=False,game_files_changed=False,blend_saved=False)
(ROOT/'.build/capacity-validation/authored-shading-checks.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
for row in checks:
    if not row['passed']:print('FAIL: '+row['check'])
assert all(row['passed'] for row in checks),report
print('PASS: '+str(len(checks))+' authored corner shading checks')
