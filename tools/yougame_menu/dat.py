import struct
class Dat:
    def __init__(s, b):
        s.b = b
        s.size, s.data_size, s.nreloc, s.nroot, s.nref = struct.unpack('>5I', b[:20])
        s.base = 0x20
        rel = s.base + s.data_size
        s.relocs = set(struct.unpack('>%dI' % s.nreloc, b[rel:rel + 4 * s.nreloc]))
        roots = rel + 4 * s.nreloc
        strt = roots + 8 * (s.nroot + s.nref)
        s.roots = []
        for i in range(s.nroot + s.nref):
            off, so = struct.unpack('>II', b[roots + 8 * i: roots + 8 * i + 8])
            nm = b[strt + so: b.index(b'\0', strt + so)].decode()
            s.roots.append((nm, off))
    def u32(s, o): return struct.unpack('>I', s.b[s.base + o: s.base + o + 4])[0]
    def u16(s, o): return struct.unpack('>H', s.b[s.base + o: s.base + o + 2])[0]
    def f32(s, o): return struct.unpack('>f', s.b[s.base + o: s.base + o + 4])[0]
    def ptr(s, o):
        return s.u32(o) if o in s.relocs else 0

def texanims(d, root_off):
    """Yield (path, texanim_off) for every TexAnim under a MatAnimJoint tree."""
    seen = set(); out = []
    def maj(o, path):
        while o and o not in seen:
            seen.add(o)
            child, nxt, ma = d.ptr(o), d.ptr(o + 4), d.ptr(o + 8)
            m = ma; k = 0
            while m:
                ta = d.ptr(m + 8); t = 0
                while ta:
                    out.append((f'{path}/m{k}/t{t}', ta)); ta = d.ptr(ta); t += 1
                m = d.ptr(m); k += 1
            if child: maj(child, path + '/c')
            o = nxt; path = path + '+'
    maj(root_off, '')
    return out

def images(d, ta):
    tbl = d.ptr(ta + 12); n = d.u16(ta + 20)
    res = []
    for i in range(n):
        io = d.ptr(tbl + 4 * i)
        if not io: res.append(None); continue
        res.append(dict(desc=io, data=d.ptr(io), w=d.u16(io + 4), h=d.u16(io + 6), fmt=d.u32(io + 8)))
    return res
