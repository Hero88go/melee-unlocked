"""Every TM-CE ASM patch: its injection address, the decomp function and offset it lands in, and its
size. usage: python tools/tmce/asm_catalog.py [folder filter]"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mextk_symbols as MS  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
ASM = os.path.join(ROOT, 'run-source', 'tmce-src', 'ASM')


def main():
    sym = MS.Symbols()
    flt = sys.argv[1] if len(sys.argv) > 1 else ''
    for dp, dn, fn in sorted(os.walk(ASM)):
        dn.sort()
        for f in sorted(fn):
            if not f.endswith('.asm'):
                continue
            path = os.path.join(dp, f)
            rel = os.path.relpath(path, ASM).replace(os.sep, '/')
            if flt and flt not in rel:
                continue
            text = open(path, encoding='utf-8', errors='replace').read()
            m = re.search(r'(?:inserted|Inject(?:ed)?|Insert)\s*(?:at|@)?\s*:?\s*(?:0x)?([0-9A-Fa-f]{8})', text[:400])
            addr = int(m.group(1), 16) if m else None
            where = sym.at(addr) if addr else None
            lines = text.count('\n')
            print('%-8s %-44s %5d  %s' % ('%08X' % addr if addr else '?',
                                          '%s+0x%x' % (where[0], where[1]) if where else '-', lines, rel))


if __name__ == '__main__':
    main()
