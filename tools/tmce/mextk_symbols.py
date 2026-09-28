"""MexTK names -> decomp symbols, by console address.

functions(): {mextk name: (address, decomp symbol or None)} from MexTK/melee.link and the decomp's
symbols.txt. globals(model_text): {stc name: (type text, address, decomp symbol, offset)} from the
`static T *stc_x = (void *)0x80...;` / R13_OFFSET(...) declarations in the MexTK headers.
"""
import bisect
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SYMBOLS = os.path.join(ROOT, 'sourceport', 'extern', 'melee', 'config', 'GALE01', 'symbols.txt')
MEXTK = os.path.join(ROOT, 'run-source', 'tmce-src', 'MexTK')
R13 = 0x804DB6A0


class Symbols:
    def __init__(self, path=SYMBOLS):
        rows = []
        for line in open(path, encoding='utf-8'):
            m = re.match(r'(\S+)\s*=\s*\.(\w+):0x([0-9A-Fa-f]+);\s*//(.*)', line)
            if not m:
                continue
            size = re.search(r'size:0x([0-9A-Fa-f]+)', m.group(4))
            local = 'scope:local' in m.group(4)
            rows.append((int(m.group(3), 16), int(size.group(1), 16) if size else 0, m.group(1), m.group(2), local))
        rows.sort()
        self.rows = rows
        self.starts = [r[0] for r in rows]
        self.by_addr = {}
        for r in rows:
            self.by_addr.setdefault(r[0], r)

    def at(self, addr):
        """(symbol, offset, section, local) of the object containing addr, or None."""
        i = bisect.bisect_right(self.starts, addr) - 1
        best = None
        while i >= 0:
            a, size, name, sect, local = self.rows[i]
            if a <= addr < a + max(size, 1) and not name.startswith('...'):
                if best is None or a > best[0] or (a == best[0] and size < best[1]):
                    best = self.rows[i]
            if addr - a > 0x100000:
                break
            i -= 1
        if best is None:
            return None
        a, size, name, sect, local = best
        return name, addr - a, sect, local


def functions(sym=None):
    sym = sym or Symbols()
    res = {}
    for line in open(os.path.join(MEXTK, 'melee.link'), encoding='utf-8'):
        line = line.strip()
        if not line or ':' not in line:
            continue
        a, name = line.split(':', 1)
        addr = int(a, 16)
        hit = sym.at(addr)
        res[name] = (addr, hit[0] if hit and hit[1] == 0 else None, hit)
    return res


GLOBAL_RE = re.compile(
    r'static\s+([\w\s\*]+?)\s*\*\s*(\w+)\s*=\s*(?:\([^)]*\)\s*)?'
    r'(0x[0-9A-Fa-f]{8}|R13_OFFSET\(\s*(-?0x[0-9A-Fa-f]+)\s*\))\s*;')


def globals_(sym=None):
    """{name: dict(type, addr, symbol, offset, local, file, line)} for every stc_ style global."""
    sym = sym or Symbols()
    res = {}
    inc = os.path.join(MEXTK, 'include')
    for fn in sorted(os.listdir(inc)):
        if not fn.endswith('.h'):
            continue
        for ln, line in enumerate(open(os.path.join(inc, fn), encoding='utf-8', errors='replace'), 1):
            m = GLOBAL_RE.search(line)
            if not m:
                continue
            if m.group(4) is not None:
                addr = R13 + int(m.group(4), 16)
            else:
                addr = int(m.group(3), 16)
            hit = sym.at(addr)
            res[m.group(2)] = dict(type=m.group(1).strip(), addr=addr, symbol=hit[0] if hit else None,
                                   offset=hit[1] if hit else None, local=hit[3] if hit else None,
                                   file=fn, line=ln, text=line.rstrip('\n'))
    return res


if __name__ == '__main__':
    import sys
    s = Symbols()
    f = functions(s)
    bad = {k: v for k, v in f.items() if v[1] is None}
    print('functions', len(f), 'unmapped', len(bad))
    for k, v in sorted(bad.items()):
        print('  %s %08X %s' % (k, v[0], v[2]))
    g = globals_(s)
    print('globals', len(g))
    if len(sys.argv) > 1:
        for k, v in sorted(g.items()):
            print('  %-40s %08X %s+%s %s' % (k, v['addr'], v['symbol'], v['offset'], 'LOCAL' if v['local'] else ''))
