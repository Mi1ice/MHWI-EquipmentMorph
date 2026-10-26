"""Standalone packaged tools must import without an installed/source addon."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from package_source import ROOT,runtime_author_helpers

class RuntimePackageHelpers(unittest.TestCase):
    def test_standalone_backend_imports_all_profile_and_layout_dependencies(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'.build') as temporary:
            tools=Path(temporary)/'Tools';backend=tools/'author_backend';backend.mkdir(parents=True)
            shutil.copyfile(ROOT/'tools/prepare_mhwi_equipmentmorph_resource_path.py',tools/'prepare.py')
            for name,source in runtime_author_helpers(ROOT/'blender_addon_v044/mhwi_equipmentmorph').items():shutil.copyfile(source,backend/name)
            result=subprocess.run([sys.executable,'-I','-c',"import runpy; m=runpy.run_path('Tools/prepare.py'); normalize,encode,retarget=m['backend'](); assert normalize('pl/test/body')=='pl/test/body'; assert retarget('pl/f_equip/pl105_0000/body/mod/f_body105_0000','002')=='pl/f_equip/pl002_0000/body/mod/f_body002_0000'"],cwd=temporary,text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
if __name__=='__main__':unittest.main()
