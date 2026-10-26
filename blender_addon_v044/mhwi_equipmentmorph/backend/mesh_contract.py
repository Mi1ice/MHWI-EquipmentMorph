"""Disk normalization; loaded declaration identity is checked by the host."""
import struct
from .vertex_formats import FORMATS

ADAPTER='mhw-c2ebbbd2-equipment-v2'
MASK=list(range(72,80))+[24,25,26,27,42]
def normalized_rows(model):
    count=struct.unpack_from('<H',model,8)[0];start=struct.unpack_from('<Q',model,72)[0]
    if not 0<count<=4096 or start+count*80>len(model):raise ValueError('模型网格描述范围无效')
    rows=bytearray(model[start:start+count*80])
    for i in range(count):
        code=struct.unpack_from('<I',rows,i*80+24)[0]
        if code not in FORMATS:raise ValueError(f'当前磁盘契约尚未覆盖顶点格式 0x{code:08x}；不能猜测运行时编号')
        if rows[i*80+14]!=FORMATS[code]['stride']:raise ValueError('模型顶点格式与步长不一致')
        for offset in MASK:rows[i*80+offset]=0
    return rows
