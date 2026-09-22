"""Lists every retail function a custom build changed, and where its new branches go.

    python tools\\changed_functions.py --dol build\\ace.dol --vanilla build\\vanilla.dol

Drop this in the repo's tools\\ folder next to mod_report.py. For each function whose bytes
differ it prints how many instructions changed and, for every changed word that is a branch,
where that branch now points: another retail function (named), the mod's own code, or nowhere
in the DOL. Functions this port replaces with host code (hle_list.txt) or watches for sub-frame
animation are flagged, because a change there behaves differently than on console.
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "port/recomp"))

from dol import Dol            # noqa: E402
from symbols import SymbolMap  # noqa: E402
from gekko import decode       # noqa: E402

# Functions port/runtime/gx/render_observer.cpp watches to rebuild poses between frames.
OBSERVED = {"HSD_JObjAlloc", "JObjLoad", "JObjRelease", "HSD_JObjDisp",
            "SetupRigidModelMtx", "SetupSharedVtxModelMtx", "SetupEnvelopeModelMtx"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dol", required=True)
    ap.add_argument("--vanilla", required=True)
    ap.add_argument("--symbols", default=str(ROOT / "port/recomp/GALE01_symbols.txt"))
    ap.add_argument("--hle", default=str(ROOT / "port/recomp/hle_list.txt"))
    ap.add_argument("--words", type=int, default=6, help="changed words to print per function")
    args = ap.parse_args()

    mod, van = Dol(args.dol), Dol(args.vanilla)
    symbols = SymbolMap(args.symbols)
    hle = set()
    for line in open(args.hle, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if line:
            hle.add(line)

    def describe(addr):
        """What lives at `addr` in the modded image."""
        if not mod.in_ram(addr):
            return "outside RAM"
        owner = symbols.containing(addr)
        if owner is not None:
            at = "" if owner.addr == addr else "+0x%X" % (addr - owner.addr)
            return "%s%s" % (owner.name, at)
        if mod.in_text(addr):
            return "unnamed text"
        return "not text (heap/loaded code)"

    changed = []
    for func in symbols.functions:
        if not mod.in_text(func.addr) or not van.in_text(func.addr):
            continue
        a, b = func.addr - 0x80000000, func.end - 0x80000000
        if mod.ram[a:b] != van.ram[a:b]:
            changed.append(func)

    print("%d changed functions\n" % len(changed))
    targets = {}
    for func in changed:
        diffs = [addr for addr in range(func.addr, func.end, 4) if mod.u32(addr) != van.u32(addr)]
        flags = []
        if func.name in hle:
            flags.append("!! REPLACED BY HOST CODE - this change cannot run")
        if func.name in OBSERVED:
            flags.append("!  watched for sub-frame animation")
        print("%08X %-44s %d of %d instructions%s"
              % (func.addr, func.name[:44], len(diffs), func.size // 4,
                 ("   " + "; ".join(flags)) if flags else ""))
        shown = 0
        for addr in diffs:
            old, new = van.u32(addr), mod.u32(addr)
            ins = decode(addr, new)
            if ins is None or ins.op not in ("b", "bc"):
                continue
            target = ins.branch_target
            if target is None or func.addr <= target < func.end:
                continue
            kind = "call" if ins.lk else "jump"
            print("    %08X  %08X -> %08X   %s to %08X  (%s)"
                  % (addr, old, new, kind, target, describe(target)))
            targets[target] = targets.get(target, 0) + 1
            shown += 1
            if shown >= args.words:
                print("    ... (%d more changed words)" % (len(diffs) - shown))
                break

    if targets:
        print("\nbranch targets the mod added, most used first:")
        for target, count in sorted(targets.items(), key=lambda kv: -kv[1]):
            print("  %08X  x%-3d %s" % (target, count, describe(target)))


if __name__ == "__main__":
    main()
