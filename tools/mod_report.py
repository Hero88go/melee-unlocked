"""Pre-flight report for recompiling a custom Melee build (m-ex / ACE, 20XX, Akaneia, ...).

    python tools/mod_report.py --dol build/ace.dol [--vanilla build/main.dol]

Answers, before spending a build on it, what a custom DOL does to the assumptions this port
makes: how much code the decomp symbol map does not describe, how much of that the discovery
pass can reach, which functions the mod changed that the port replaces with host code (those
changes cannot take effect), and where the mod hooks into the retail game.

Nothing here modifies anything; it only reads the two DOL files.
"""
import argparse
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "port/recomp"))

from dol import Dol            # noqa: E402
from symbols import SymbolMap  # noqa: E402
from gekko import decode       # noqa: E402
import discover                # noqa: E402

VANILLA_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"


def section_table(dol):
    return sorted(((s.kind, s.index, s.addr, s.end, s.size) for s in dol.sections), key=lambda r: r[2])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dol", required=True, help="the custom build's main.dol (tools/extract_dol.py --any)")
    ap.add_argument("--vanilla", help="the vanilla NTSC 1.02 main.dol, for a byte-level comparison")
    ap.add_argument("--symbols", default=str(ROOT / "port/recomp/GALE01_symbols.txt"))
    ap.add_argument("--hle", default=str(ROOT / "port/recomp/hle_list.txt"))
    ap.add_argument("--no-pointer-scan", action="store_true")
    args = ap.parse_args()

    raw = Path(args.dol).read_bytes()
    digest = hashlib.sha1(raw).hexdigest()
    dol = Dol(args.dol)
    symbols = SymbolMap(args.symbols)
    hle = set()
    for line in open(args.hle, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if line:
            hle.add(line)

    print("=== image ===")
    print("file        %s" % args.dol)
    print("sha1        %s%s" % (digest, "  (vanilla NTSC 1.02)" if digest == VANILLA_SHA1 else "  (custom build)"))
    print("size        %d bytes" % len(raw))
    print("entry       %08X" % dol.entry)
    print("sections")
    for kind, index, addr, end, size in section_table(dol):
        print("  %-5s%-2d %08X-%08X  %8d" % (kind, index, addr, end, size))
    text_bytes = sum(e - a for a, e in dol.text_ranges)

    vanilla = None
    if args.vanilla:
        vanilla = Dol(args.vanilla)
        print("\n=== compared with vanilla ===")
        van_ranges = {(a, e) for a, e in vanilla.text_ranges}
        for a, e in dol.text_ranges:
            if (a, e) not in van_ranges:
                print("  new or resized text %08X-%08X (%d bytes)" % (a, e, e - a))
        changed = []
        for func in symbols.functions:
            if not dol.in_text(func.addr) or not vanilla.in_text(func.addr):
                continue
            a, b = func.addr - 0x80000000, func.end - 0x80000000
            if dol.ram[a:b] != vanilla.ram[a:b]:
                changed.append(func)
        print("  %d of %d retail functions have different bytes" % (len(changed), len(symbols.functions)))
        hit = [f for f in changed if f.name in hle]
        if hit:
            print("  !! %d of them are replaced by host code in this port, so the mod's changes there do NOT run:" % len(hit))
            for f in hit:
                print("     %08X %s" % (f.addr, f.name))
        else:
            print("  none of them is a function this port replaces with host code (good)")

    print("\n=== code the symbol map does not describe ===")
    retail = [f for f in symbols.functions if dol.in_text(f.addr)]
    covered = sum(f.size for f in retail)
    found = discover.discover(dol, symbols, scan_pointers=not args.no_pointer_scan, log=lambda s: print("  " + s))
    if found:
        lo = min(f.addr for f in found)
        hi = max(f.end for f in found)
        print("  discovered code spans %08X-%08X" % (lo, hi))

    print("\n=== hooks from retail code into the mod ===")
    new_starts = {f.addr for f in found}
    hooks = []
    for func in retail:
        for a in range(func.addr, min(func.end, 0x81800000), 4):
            ins = decode(a, dol.u32(a))
            if ins is None or ins.op not in ("b", "bc"):
                continue
            target = ins.branch_target
            if target is not None and target in new_starts:
                hooks.append((a, func.name, target))
    print("  %d branches from retail functions into discovered code" % len(hooks))
    for a, name, target in hooks[:15]:
        print("    %08X in %-34s -> %08X" % (a, name[:34], target))
    hooked_hle = sorted({name for _, name, _ in hooks if name in hle})
    if hooked_hle:
        print("  !! hooks inside functions this port replaces with host code (they will not run):")
        for name in hooked_hle:
            print("     %s" % name)

    print("\n=== verdict ===")
    left = text_bytes - covered - sum(f.size for f in found)
    print("  text %d bytes: %d retail (translated), %d discovered (translated), %d unclaimed"
          % (text_bytes, covered, sum(f.size for f in found), left))
    print("  unclaimed bytes are data, padding or code only reached indirectly; reached indirectly,")
    print("  it still runs, in the interpreter, and shows up in melee_port.log as interpreter calls.")
    if args.vanilla is None:
        print("  pass --vanilla to also see which retail functions this build changed")


if __name__ == "__main__":
    main()
