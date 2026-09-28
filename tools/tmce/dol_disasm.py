"""Disassemble the retail main.dol around an address, with branch targets named from the decomp's
symbols.txt, to pin a TM-CE patch site to its C statement.

usage: py -3.12 tools/tmce/dol_disasm.py <Start.dol> <address hex> [before] [after]
       py -3.12 tools/tmce/dol_disasm.py <Start.dol> <function name> (whole function)
"""
import os
import struct
import sys

import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mextk_symbols as MS  # noqa: E402


def load_dol(path):
    b = open(path, 'rb').read()
    offs = struct.unpack('>18I', b[0:0x48])
    addrs = struct.unpack('>18I', b[0x48:0x90])
    sizes = struct.unpack('>18I', b[0x90:0xD8])
    return b, [(offs[i], addrs[i], sizes[i]) for i in range(18) if sizes[i]]


def read(dol, secs, addr, n):
    for off, a, size in secs:
        if a <= addr < a + size:
            return dol[off + addr - a:off + addr - a + n]
    return None


def main():
    dol, secs = load_dol(sys.argv[1])
    sym = MS.Symbols()
    target = sys.argv[2]
    if all(c in '0123456789abcdefABCDEFx' for c in target):
        addr = int(target, 16)
        before = int(sys.argv[3]) if len(sys.argv) > 3 else 16
        after = int(sys.argv[4]) if len(sys.argv) > 4 else 16
        start, end = addr - before * 4, addr + after * 4
    else:
        row = [r for r in sym.rows if r[2] == target]
        if not row:
            print('no symbol', target)
            return
        start, end = row[0][0], row[0][0] + row[0][1]
        addr = None
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    code = read(dol, secs, start, end - start)
    for ins in md.disasm(code, start):
        note = ''
        if ins.mnemonic.startswith('b') and ins.op_str.startswith('0x'):
            t = int(ins.op_str.split(',')[-1].strip(), 16)
            hit = sym.at(t)
            if hit:
                note = '  ; %s+0x%x' % (hit[0], hit[1]) if hit[1] else '  ; ' + hit[0]
        mark = '>>' if addr is not None and ins.address == addr else '  '
        where = sym.at(ins.address)
        print('%s %08X +%-5x %-8s %s%s' % (mark, ins.address, ins.address - (where and ins.address - where[1] or 0),
                                          ins.mnemonic, ins.op_str, note))


if __name__ == '__main__':
    main()
