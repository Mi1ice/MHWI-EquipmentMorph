"""Disk contract acceptance and rejection across every pinned regular layout."""
import sys
from pathlib import Path
import struct
import unittest
import types

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'blender_addon_v044'))
package=types.ModuleType('mhwi_equipmentmorph');package.__path__=[str(ROOT/'blender_addon_v044/mhwi_equipmentmorph')]
sys.modules['mhwi_equipmentmorph']=package
from mhwi_equipmentmorph.backend.mesh_contract import normalized_rows

LAYOUTS=[(0xa756f2f9,24),(0xa5104ca0,28),(0x818904dc,28),(0x0f06033f,32),
         (0xc3ac03a1,36),(0xc9690ab8,40),(0xf637401c,32),(0xf471fe45,36),
         (0x3c730760,36),(0xb2fc0083,40),(0x81f58067,40),(0x83b33e3e,44),
         (0x366995a7,44),(0xb8e69244,48)]
def model(layouts):
    raw=bytearray(96+80*len(layouts));struct.pack_into('<H',raw,8,len(layouts));struct.pack_into('<Q',raw,72,96)
    for i,(code,stride) in enumerate(layouts):
        at=96+i*80;raw[at+14]=stride;struct.pack_into('<I',raw,at+24,code)
        raw[at+42]=7;raw[at+72:at+80]=b'identity'
    return raw
class VertexFormats(unittest.TestCase):
    def test_all_14_and_mixed_static_rows(self):
        result=normalized_rows(model(LAYOUTS))
        self.assertEqual(len(result),1120)
        for i,(_,stride) in enumerate(LAYOUTS):
            self.assertEqual(result[i*80+14],stride)
            self.assertEqual(result[i*80+24:i*80+28],b'\0'*4)
            self.assertEqual(result[i*80+72:i*80+80],b'\0'*8)
    def test_old_three_normalization_unchanged(self):
        self.assertEqual(normalized_rows(model([(0xf637401c,32)])),bytearray(14)+b' '+bytearray(65))
    def test_unknown_and_same_stride_wrong_code_rejected(self):
        for code,stride in [(0,32),(0x81f58067,36),(0xf637401c,40)]:
            with self.assertRaises(ValueError):normalized_rows(model([(code,stride)]))
if __name__=='__main__':unittest.main()
