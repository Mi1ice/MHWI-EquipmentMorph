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
