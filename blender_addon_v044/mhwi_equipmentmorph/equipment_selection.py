"""Resolve an export target from the locally verified equipment catalogue."""
from functools import lru_cache
import json
from pathlib import Path
from .output_files import model_resource_id

PART_NAMES={'head':'头部','body':'身体','arms':'腕部','waist':'腰部','legs':'腿部'}

@lru_cache(maxsize=1)
def catalogue():
    path=Path(__file__).with_name('equipment_catalog.json')
    if not path.is_file():raise ValueError('缺少本地装备目录，请安装含装备数据的插件包')
    data=json.loads(path.read_bytes())
    if data.get('schema')!='mhwi-equipment-catalogue-v2':raise ValueError('装备目录格式不正确')
    armor={}
    for item in data['armor']:
        if not item['selectable']:continue
        key=(item['series_id'],item['part'])
        if key in armor:raise ValueError('装备目录存在重复的套装部位记录')
        armor[key]=item
    return {s['series_id']:s for s in data['sets']},armor

@lru_cache(maxsize=2)
def series_options(sex):
    sets,_=catalogue()
    return [(str(s['series_id']),s['name']+' · '+s['model_address'][2:],
             '可用部位：'+'、'.join(PART_NAMES[p] for p in s['exportable_parts'][sex]),0,s['series_id']+1)
            for s in sets.values() if s['exportable_parts'][sex]]

def resolve(series_id,sex,part):
    sets,armor=catalogue()
    selected=sets.get(series_id)
    if not selected:raise ValueError('请选择目标套装')
    if sex not in ('female','male') or part not in PART_NAMES:raise ValueError('无效的性别或装备部位')
    if not selected['selectable']:raise ValueError('该套装没有可用装备记录')
    if selected['gender_restriction'] not in (None,sex):raise ValueError('该套装不支持所选性别')
    item=armor.get((series_id,part))
    if not item or not item['selectable']:raise ValueError('该套装没有可用的'+PART_NAMES[part]+'记录')
    entry=item['resource_paths'][sex]
    if entry['status']=='restricted_by_source':raise ValueError('该部位不支持所选性别')
    if entry['status']!='available':raise ValueError('解包目录未找到该性别的'+PART_NAMES[part]+'资源，请重新选择')
    return dict(entry)

def target(asset):
    if asset.target_mode=='MANUAL':return model_resource_id(asset.model_id)
    return resolve(asset.series_id,asset.sex,asset.part)['model_id']

def clear_cache():
    series_options.cache_clear();catalogue.cache_clear()
