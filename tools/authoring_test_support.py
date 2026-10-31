"""Utilities for isolated factory-startup authoring checks; no user project IO."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import bpy

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--validator', type=Path)
parser.add_argument('--addon-root', type=Path, default=ROOT/'blender_addon_v044')
ARGS = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
ARGS.output.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ARGS.addon_root))
import mhwi_equipmentmorph as addon
checks = []
def check(ok, label):
    assert ok, label
    checks.append(label)
def rejected(call, label):
    try: call()
    except (ValueError, RuntimeError, OSError) as error:
        check(bool(str(error)), label)
        return str(error)
    raise AssertionError('Not rejected: '+label)
def cancelled(call, label):
    try: result = call()
    except RuntimeError as error:
        check(bool(str(error)), label)
    else: check(result == {'CANCELLED'}, label)
def config(value):
    result = {}
    for prop in value.bl_rna.properties:
        if prop.identifier == 'rna_type': continue
        item = getattr(value, prop.identifier)
        if prop.type == 'COLLECTION': item = [config(x) for x in item]
        elif prop.type == 'POINTER': item = item.as_pointer() if item else 0
        elif getattr(prop,'is_array',False): item = list(item)
        result[prop.identifier] = item
    return result
def scene():
    from mhwi_equipmentmorph.backend.capture_equipment_morph_job import fingerprint
    return dict(objects=[(o.name, o.data.as_pointer() if o.data else 0,
                         fingerprint(o) if o.type=='MESH' else None,
                         o.select_get(), o.hide_get()) for o in bpy.data.objects],
                active=bpy.context.view_layer.objects.active.name if bpy.context.view_layer.objects.active else None,
                counts=[len(getattr(bpy.data,k)) for k in ('objects','meshes','armatures','shape_keys','collections')])
def report(name, **extra):
    result=dict(passed=len(checks), checks=checks, blender=list(bpy.app.version),
                background_blender_tested=True, gui_tested=False, game_tested=False,
                user_project_opened=False, blend_saved=False, **extra)
    (ARGS.output/(name+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n', encoding='utf-8')
    print('PASS:', name, len(checks))
def load_fixture():
    # Explicitly register the installed pinned exporter, without loading user preferences.
    import importlib
    folder=Path(bpy.utils.user_resource('SCRIPTS'))/'addons'
    sys.path.insert(0,str(folder))
    from blender_export_fixture import fixture, exporter
    return fixture, exporter
