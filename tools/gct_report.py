"""Reports what a Gecko code list does, in the terms the recompiler cares about.

    python tools\\gct_report.py --gct build\\codes.gct --base 8065CC80 [--dol build\\ace.dol]

Put this in the repo's tools\\ folder. A GCT is only a list of patches to guest RAM, which a
static recompilation cannot execute; port/recomp/gecko.py can bake those patches into the
translation instead, but only for the code types it understands. This prints what the list is
made of, where it patches, and what could not be decoded.
"""
import argparse
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "port/recomp"))

import gecko                   # noqa: E402
from symbols import SymbolMap  # noqa: E402
from dol import Dol            # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gct", required=True)
    ap.add_argument("--base", default="8065CC80", help="guest address the table is loaded at (hex)")
    ap.add_argument("--dol", help="the build's DOL, to say whether a patch lands in code or data")
    ap.add_argument("--symbols", default=str(ROOT / "port/recomp/GALE01_symbols.txt"))
    ap.add_argument("--list", type=int, default=25, help="how many hooks to print")
    args = ap.parse_args()

    data = Path(args.gct).read_bytes()
    base = int(args.base, 16)
    patches = gecko.parse_gct(data, base)
    symbols = SymbolMap(args.symbols)
    dol = Dol(args.dol) if args.dol else None

    def where(addr):
        func = symbols.containing(addr)
        if func is None:
            if dol and dol.in_text(addr):
                return "unnamed text"
            if dol and dol.in_ram(addr):
                return "not text"
            return "outside the DOL (RAM only)"
        return "%s+0x%X" % (func.name, addr - func.addr) if addr != func.addr else func.name

    print("=== %s ===" % args.gct)
    print("size        %d bytes (%d lines), loaded at %08X" % (len(data), len(data) // 8, base))
    print("writes      %d" % len(patches.writes))
    print("C2 hooks    %d" % len(patches.hooks))
    print("C0 caves    %d" % len(patches.c0))
    print("unsupported %d lines" % len(patches.unsupported))

    if dol:
        in_text = sum(1 for addr, blob in patches.writes if dol.in_text(addr))
        in_data = sum(1 for addr, blob in patches.writes if dol.in_ram(addr) and not dol.in_text(addr))
        outside = len(patches.writes) - in_text - in_data
        written = sum(len(blob) for addr, blob in patches.writes)
        print("            %d patch code, %d patch data, %d land outside the DOL image (%d bytes total)"
              % (in_text, in_data, outside, written))

    cave_words = sum(len(h.words) for h in patches.hooks) + sum(len(c.words) for c in patches.c0)
    print("cave code   %d instructions (%d bytes) living inside the table" % (cave_words, cave_words * 4))

    if patches.hooks:
        print("\nhooks (the instruction replaced -> the cave that replaces it):")
        for hook in patches.hooks[:args.list]:
            print("  %08X  %-44s %3d instructions" % (hook.hook, where(hook.hook)[:44], len(hook.words)))
        if len(patches.hooks) > args.list:
            print("  ... %d more" % (len(patches.hooks) - args.list))
        owners = Counter(where(h.hook).split("+")[0] for h in patches.hooks)
        print("\nmost hooked functions:")
        for name, count in owners.most_common(10):
            print("  %-44s x%d" % (name[:44], count))

    if patches.c0:
        print("\nC0 caves (run once at boot):")
        for cave in patches.c0[:args.list]:
            print("  %08X  %d instructions" % (cave.cave_addr, len(cave.words)))

    if patches.unsupported:
        print("\nunsupported lines (gecko.py cannot express these as ahead-of-time patches):")
        seen = Counter("%02X" % (a >> 24) for _, a, b in patches.unsupported)
        for code_type, count in seen.most_common():
            print("  type %s  x%d" % (code_type, count))
        for index, a, b in patches.unsupported[:10]:
            print("    line %d: %08X %08X" % (index, a, b))


if __name__ == "__main__":
    main()
