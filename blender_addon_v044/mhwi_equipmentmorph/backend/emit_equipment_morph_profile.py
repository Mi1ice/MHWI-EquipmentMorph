"""Pack an export description into the bounded native profile format.

This tool consumes exporter-generated metadata, not user runtime.ini settings.
Model/mesh identity evidence belongs to the export/adapter preparation stage.
The native loader independently checks the installed model and vertex fields.
"""
import argparse
import hashlib
import json
import math
import re
import shutil
import struct
from pathlib import Path

from .mesh_contract import ADAPTER

def encode(description, asset):
    d = description
    if d['adapter'] != ADAPTER or d['baseline_semantics'] != 'zero_contribution_restores_exported_mod3':
        raise ValueError('Unsupported adapter or baseline semantics')
    if hashlib.sha256(asset).hexdigest() != d['asset_sha256']:
        raise ValueError('Asset changed')
    if not 0 <= d['part'] <= 4 or not 0 < d['vertex_bytes'] <= 64*1024*1024 or not 0 < d['mesh_count'] <= 4096 or not 0 < d['model_bytes'] <= 128*1024*1024:
        raise ValueError('Description bounds')
    model=d['model_id']
    if len(model)>=80 or not re.fullmatch(r'pl/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+',model):
        raise ValueError('Canonical model resource ID')
    u32=lambda value:struct.pack('<I',value)
    def string(value):
        raw=value.encode('utf-8')
        if not raw or len(raw)>1024 or b'\0' in raw:raise ValueError('Profile text bound')
        return u32(len(raw))+raw
    def sha(value):
        if not re.fullmatch('[0-9a-f]{64}',value):raise ValueError('Digest')
        return string(value)
    raw=b'MHMPROF1'+u32(1)+string(ADAPTER)
    raw+=b''.join(u32(d[k]) for k in ['part','vertex_bytes','mesh_count','model_bytes'])+string(model)
    raw+=b''.join(sha(d[k]) for k in ['model_sha256','asset_sha256','runtime_mesh_sha256','capture_sha256'])
    objects=d['source_objects'];names=set();dynamic=set()
    if not 0<len(objects)<=128:raise ValueError('Source object bound')
    raw+=u32(len(objects))
    for obj in objects:
        if obj['name'] in names:raise ValueError('Duplicate object')
        names.add(obj['name']);raw+=string(obj['name'])+sha(obj['geometry_sha256'])
        values=obj['values'];keys=set()
        if not 0<len(values)<=256:raise ValueError(f'源网格 {obj["name"]} 的受控键与非零固定键记录为 {len(values)} 条，必须为1至256条')
        raw+=u32(len(values))
        for value in values:
            if value['name'] in keys:raise ValueError('Duplicate source key')
            keys.add(value['name']);weight=value['weight']
            if not math.isfinite(weight) or abs(weight)>1000000:raise ValueError('Source baseline value')
            if value['dynamic']:
                if weight!=0 or value['muted']:raise ValueError('Dynamic source key must be zero and unmuted')
                dynamic.add(value['name'])
            raw+=string(value['name'])+struct.pack('<fI',weight,int(value['muted'])+int(value['dynamic'])*2)
    if dynamic!=set(d['controlled_keys']) or len(set(d['controlled_keys']))!=len(d['controlled_keys']):raise ValueError('Dynamic key coverage')
    if len(raw)>512*1024:raise ValueError('Profile exceeds byte bound')
    return raw

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--description',type=Path,required=True)
    parser.add_argument('--asset',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True,help='New package directory; existing paths are refused')
    args=parser.parse_args()
    description=json.loads(args.description.read_text(encoding='utf-8-sig'))
    asset=args.asset.read_bytes();raw=encode(description,asset)
    args.output.mkdir(parents=True,exist_ok=False)
    (args.output/'model.mhmprofile').write_bytes(raw)
    shutil.copyfile(args.asset,args.output/'asset.mhmpack')
    (args.output/'profile.json').write_text(json.dumps(description,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'profile_sha256':hashlib.sha256(raw).hexdigest(),'asset_sha256':hashlib.sha256(asset).hexdigest(),'installed':False}))

if __name__=='__main__':main()
