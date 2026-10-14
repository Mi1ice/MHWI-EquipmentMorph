"""MHMPACK2 encoder; existing MHMPACK1 inputs remain byte-for-byte intact."""
import math
import struct
import copy
import hashlib
import json
from pathlib import Path

def prepare(path):
    from .backend.prepare_equipment_morph_catalog import prepare as legacy_prepare
    from .backend.prepare_equipment_morph_catalog import unique_object
    path=Path(path);raw=path.read_bytes()
    if len(raw)>1024*1024:raise ValueError('Manifest exceeds bound')
    manifest=json.loads(raw,object_pairs_hook=unique_object)
    if manifest['schema']=='mhw-equipment-morph-package-v1':return legacy_prepare(path)
    if manifest['schema']!='mhwi-equipmentmorph-package-v2' or manifest['shading']['mode']!='per-key-direction-deltas':raise ValueError('Unsupported normal manifest')
    normals=manifest['per_key_normals']
    if normals['mode']!='per-key-direction-deltas' or normals['asset_format']!=2:raise ValueError('Normal mode/version mismatch')
    local=copy.deepcopy(manifest);local['schema']='mhw-equipment-morph-package-v1';local['shading']['mode']='shared-author-intent-max'
    packed,_,inputs=legacy_prepare(path,local);data={}
    for key,item in normals['keys'].items():
        name=item['file'];count=item['records']
        if not isinstance(name,str) or Path(name).name!=name or any(c in name for c in '/\\:') or type(count) is not int or not 0<=count<=1000000:raise ValueError('Normal filename/count bound')
        source=(path.parent/name).resolve()
        if not source.is_relative_to(path.parent.resolve()):raise ValueError('Normal file escapes package')
        if source.stat().st_size!=count*16:raise ValueError('Normal file size')
        value=source.read_bytes()
        if hashlib.sha256(value).hexdigest()!=item['sha256']:raise ValueError('Normal payload changed')
        data[key]=value;inputs.append({'path':str(source),'sha256':item['sha256']})
    return upgrade(packed,data),manifest,inputs

def upgrade(legacy,normal_data):
    if legacy[:8]!=b'MHMPACK1':raise ValueError('Expected position-only MHMPACK1')
    version,part,size,count,rotations,model_bytes=struct.unpack_from('<6I',legacy,8)
    if version!=1 or rotations:raise ValueError('Cannot mix author rotations and per-key normal deltas')
    at=32;model=legacy[at:at+model_bytes];at+=model_bytes;basis=legacy[at:at+size];at+=size
    output=bytearray(b'MHMPACK2'+struct.pack('<6I',2,part,size,count,1,model_bytes)+model+basis)
    selected=set();total=0
    for _ in range(count):
        id_bytes,lo,hi,flags,group,positions=struct.unpack_from('<6I',legacy,at);at+=24
        ident=legacy[at:at+id_bytes];at+=id_bytes;name=ident.decode('ascii');selected.add(name)
        raw_positions=legacy[at:at+positions*16];at+=positions*16
        if flags:raise ValueError('Legacy author flag cannot enter MHMPACK2')
        normals=normal_data[name]
        if len(normals)%16:raise ValueError('Truncated per-key normal records')
        total+=len(normals)//16
        if total>1000000:raise ValueError('Per-key normal count exceeds bound')
        previous=-8
        for offset,x,y,z in struct.iter_unpack('<I3f',normals):
            if offset<previous+8 or offset+8>len(basis) or not all(map(math.isfinite,(x,y,z))):raise ValueError('Per-key normal field bounds/order')
            base=struct.unpack_from('<3b',basis,offset);length=math.sqrt(sum(v*v for v in base))
            if length<1e-8:raise ValueError('Zero packed basis normal')
            unit=[v/length for v in base];target=[a+b for a,b in zip(unit,(x,y,z))]
            magnitude=sum(v*v for v in target)
            if not .9999<=magnitude<=1.0001 or sum(a*b for a,b in zip(unit,target))<-.999999:raise ValueError('Invalid normal endpoint')
            previous=offset
        output.extend(struct.pack('<7I',id_bytes,lo,hi,2 if normals else 0,group,positions,len(normals)//16))
        output.extend(ident+raw_positions+normals)
    if at!=len(legacy) or selected!=set(normal_data) or len(output)>128*1024*1024:raise ValueError('Per-key pack coverage/trailing/bounds')
    return bytes(output)
