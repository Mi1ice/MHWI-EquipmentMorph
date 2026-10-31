"""Historical direct-register export check; not an installation acceptance test."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from authoring_test_support import *
from prepare_mhwi_equipmentmorph_resource_path import verify
check(Path(addon.__file__).resolve().is_relative_to(ARGS.addon_root.resolve()),'loaded addon comes from candidate ZIP')
fixture,exporter=load_fixture();collection,obj=fixture()
addon.register();state=addon.settings(bpy.context)
asset=state.assets.add();asset.collection=collection;asset.target_mode='MANUAL';asset.model_id='pl/f_equip/pl105_0000/body/mod/f_body105_0000'
addon.refresh_keys(asset);next(k for k in asset.keys if k.source=='Smile').selected=True
check(bpy.ops.mhwi_em.confirm_keys()=={'FINISHED'},'ZIP real key confirmation')
state.output=str(ARGS.output/'export');before=scene()
check(bpy.ops.mhwi_em.export_package()=={'FINISHED'},'ZIP real paired export')
check(verify(ARGS.validator,Path(state.output),ROOT/'config')['native_loader_valid'],'ZIP output accepted by current native loader')
check(len([p for p in Path(state.output).rglob('*') if p.is_file()])==4,'ZIP ordinary export exactly four files')
check(before==scene(),'ZIP export restores source')
addon.unregister();report('candidate-selftest',addon_from_zip=True,addon_path=str(Path(addon.__file__).resolve()))
