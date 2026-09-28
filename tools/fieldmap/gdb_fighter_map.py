# Run inside gdb: gdb -batch -x tools/fieldmap/gdb_fighter_map.py <melee_game.dbg>
# Writes FIELDMAP_OUT (default fighter_map.json): every scalar leaf of `struct Fighter` with its console
# (32-bit big-endian) offset and size and its native offset and size, so a native Fighter dump and a
# console-layout Fighter dump can be compared field by field. Console layout is modelled the same way
# the layout audit models it (4-byte pointers, console alignment, MSB-first bitfields).
import json
import os
import sys

sys.path.insert(0, os.environ['FIELDMAP_HELPER_DIR'])
os.environ['ALIAS_MAIN'] = '0'
import gdb_layout as L  # noqa: E402
import gdb  # noqa: E402

OUT = os.environ.get('FIELDMAP_OUT', 'fighter_map.json')
t = gdb.parse_and_eval('(struct Fighter*)0').type.target()
leaves = []
L.flatten(t, 0, 0, 'fp', leaves)
rows = []
seen = set()
for path, cbit, cbits, nbit, nbits, kind, tname in leaves:
    if kind not in ('flt', 'int', 'bits', 'arr', 'ptr', 'discptr'):
        continue
    if cbit % 8 or nbit % 8:
        continue                      # bitfields inside a byte are compared through their byte
    key = (cbit, cbits, kind == 'ptr')
    if key in seen:
        continue                      # union views of the same console bytes
    seen.add(key)
    rows.append(dict(path=path, coff=cbit // 8, csize=max(1, cbits // 8), noff=nbit // 8,
                     nsize=max(1, nbits // 8), kind=kind, type=tname))
json.dump({'console_size': L.cl(t)[0], 'native_size': t.sizeof, 'fields': rows}, open(OUT, 'w'), indent=0)
print('fields', len(rows), 'console size', hex(L.cl(t)[0]), 'native size', hex(t.sizeof))
