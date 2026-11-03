"""GameCube disc file reader + VCDIFF decoder (port of port/runtime/host/vcdiff.cpp)."""
import struct

def iso_files(iso_path):
    with open(iso_path, 'rb') as f:
        f.seek(0x424); fst_off, fst_size = struct.unpack('>II', f.read(8))
        f.seek(fst_off); fst = f.read(fst_size)
    n = struct.unpack('>I', fst[8:12])[0]
    strings = fst[n * 12:]
    out = {}
    def name(off): return strings[off:strings.index(b'\0', off)].decode('ascii', 'replace')
    def walk(i, end, prefix):
        while i < end:
            w0, a, b = struct.unpack('>III', fst[i*12:i*12+12])
            nm = name(w0 & 0xFFFFFF)
            if w0 >> 24:
                walk(i + 1, b, prefix + nm + '/'); i = b
            else:
                out[prefix + nm] = (a, b); i += 1
    walk(1, n, '')
    return out

def iso_read(iso_path, name):
    files = iso_files(iso_path)
    off, size = files[name]
    with open(iso_path, 'rb') as f:
        f.seek(off); return f.read(size)

def _table():
    t = [(2, 0, 0, 0, 0, 0)]
    t += [(1, s, 0, 0, 0, 0) for s in range(18)]
    for m in range(9):
        t.append((3, 0, m, 0, 0, 0)); t += [(3, s, m, 0, 0, 0) for s in range(4, 19)]
    for m in range(6):
        for a in range(1, 5):
            for s in range(4, 7): t.append((1, a, 0, 3, s, m))
    for m in range(6, 9):
        for a in range(1, 5): t.append((1, a, 0, 3, 4, m))
    for m in range(9): t.append((3, 4, m, 1, 1, 0))
    assert len(t) == 256
    return t
TABLE = _table()

class R:
    def __init__(s, b, p=0, e=None): s.b, s.p, s.e = b, p, len(b) if e is None else e
    def byte(s):
        if s.p >= s.e: raise ValueError('truncated')
        v = s.b[s.p]; s.p += 1; return v
    def varint(s):
        v = 0
        for _ in range(5):
            x = s.byte(); v = (v << 7) | (x & 0x7F)
            if not x & 0x80: return v
        raise ValueError('varint')

def vcdiff_decode(source, delta):
    r = R(delta)
    assert delta[:4] == b'\xd6\xc3\xc4\x00'; r.p = 4
    hdr = r.byte(); assert not hdr & 3
    if hdr & 4: r.p += r.varint()
    target = bytearray()
    while r.p < r.e:
        win = r.byte(); seg = b''
        if win & 3:
            sl = r.varint(); sp = r.varint()
            assert win & 1, 'target-segment windows not supported'
            seg = source[sp:sp + sl]
        r.varint(); tl = r.varint(); di = r.byte(); assert not di & 7
        dl, il, al = r.varint(), r.varint(), r.varint()
        if win & 4: r.varint()
        data = R(delta, r.p, r.p + dl); inst = R(delta, r.p + dl, r.p + dl + il); addrs = R(delta, r.p + dl + il, r.p + dl + il + al)
        r.p += dl + il + al
        w = bytearray(seg); near = [0] * 4; same = [0] * 768; nn = 0
        while inst.p < inst.e:
            e = TABLE[inst.byte()]
            for half in (0, 1):
                typ, sz, mode = (e[3], e[4], e[5]) if half else (e[0], e[1], e[2])
                if typ == 0: continue
                if not sz: sz = inst.varint()
                if typ == 1:
                    w += delta[data.p:data.p + sz]; data.p += sz
                elif typ == 2:
                    w += bytes([data.byte()]) * sz
                else:
                    here = len(w)
                    if mode == 0: a = addrs.varint()
                    elif mode == 1: a = here - addrs.varint()
                    elif mode < 6: a = near[mode - 2] + addrs.varint()
                    else: a = same[(mode - 6) * 256 + addrs.byte()]
                    near[nn] = a; nn = (nn + 1) % 4; same[a % 768] = a
                    for k in range(sz): w.append(w[a + k])
        assert len(w) - len(seg) == tl
        target += w[len(seg):]
    return bytes(target)

def built(iso_path, sys_dir, name):
    src = iso_read(iso_path, name)
    delta = open(f'{sys_dir}/GameFiles/GALE01/{name}.diff', 'rb').read()
    return vcdiff_decode(src, delta)
