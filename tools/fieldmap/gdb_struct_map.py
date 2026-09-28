# Run inside gdb: FIELDMAP_TYPES="Fighter HSD_GObj ..." FIELDMAP_OUT=out.json \
#   gdb -batch -x tools/fieldmap/gdb_struct_map.py build-sourceport-gcc/melee_game.dbg
# For each named struct type: every member (aggregates and leaves, nested, arrays of aggregates
# expanded up to 16 elements) with its console offset and size (32-bit big-endian layout, modelled
# as in gdb_layout.py) and its native offset and size, plus the member's type name. Used to map the
# MexTK headers (console offsets) onto the native decomp layout (tools/tmce/gen_mextk.py).
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ['ALIAS_MAIN'] = '0'
import gdb_layout as L  # noqa: E402
import gdb  # noqa: E402

OUT = os.environ.get('FIELDMAP_OUT', 'struct_map.json')
NAMES = os.environ['FIELDMAP_TYPES'].split()


def members(t, cbase, nbase, path, out, depth=0):
    s = t.strip_typedefs()
    if depth > 12:
        return
    code = s.code
    if code == gdb.TYPE_CODE_STRUCT:
        r = L.struct_layout(s)
        for (f, coff, noff, cb) in r[2]:
            name = f.name or ''
            p = path + '.' + name if name else path
            if f.bitsize:
                out.append(dict(path=p, cbit=cbase + coff, cbits=f.bitsize, nbit=nbase + noff,
                                nbits=f.bitsize, kind='bits', type=str(f.type)))
                continue
            cs = L.cl(f.type)[0] * 8
            out.append(dict(path=p, cbit=cbase + coff, cbits=cs, nbit=nbase + noff,
                            nbits=f.type.strip_typedefs().sizeof * 8, kind=L.kind_of(f.type), type=str(f.type), union=f.type.strip_typedefs().code == gdb.TYPE_CODE_UNION,
                            discptr=L.is_discptr(f.type.strip_typedefs())))
            members(f.type, cbase + coff, nbase + noff, p, out, depth + 1)
        return
    if code == gdb.TYPE_CODE_UNION:
        if L.is_discptr(s):
            return
        for f in s.fields():
            name = f.name or ''
            p = path + '.' + name if name else path
            cs = L.cl(f.type)[0] * 8
            out.append(dict(path=p, cbit=cbase, cbits=cs, nbit=nbase, nbits=f.type.strip_typedefs().sizeof * 8,
                            kind=L.kind_of(f.type), type=str(f.type), discptr=L.is_discptr(f.type.strip_typedefs()),
                            union=f.type.strip_typedefs().code == gdb.TYPE_CODE_UNION))
            members(f.type, cbase, nbase, p, out, depth + 1)
        return
    if code == gdb.TYPE_CODE_ARRAY:
        lo, hi = s.range()
        n = hi - lo + 1
        et = s.target()
        if n <= 0 or L.kind_of(et) != 'agg':
            return
        ecs = L.cl(et)[0] * 8
        ens = et.strip_typedefs().sizeof * 8
        # long arrays: the first two elements are enough to know the element layout and the stride
        for i in range(n if n <= 16 else 2):
            p = '%s[%d]' % (path, i)
            out.append(dict(path=p, cbit=cbase + i * ecs, cbits=ecs, nbit=nbase + i * ens, nbits=ens,
                            kind='agg', type=str(et), discptr=False))
            members(et, cbase + i * ecs, nbase + i * ens, p, out, depth + 1)


result = {}
for name in NAMES:
    try:
        t = gdb.lookup_type(name)
    except gdb.error:
        try:
            t = gdb.lookup_type('struct ' + name)
        except gdb.error:
            print('missing type', name)
            continue
    s = t.strip_typedefs()
    rows = []
    members(t, 0, 0, '', rows)
    result[name] = dict(console_size=L.cl(t)[0], native_size=s.sizeof, members=rows)
    print(name, 'members', len(rows), 'console', hex(L.cl(t)[0]), 'native', hex(s.sizeof))
json.dump(result, open(OUT, 'w'), indent=0)
