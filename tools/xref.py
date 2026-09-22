"""Finds the code that touches an address, in the DOL and in a mod's Gecko caves.

    python tools\\xref.py --dol build\\ace.dol --addr 80431FA0
    python tools\\xref.py --dol build\\ace.dol --addr 80431FA0 --span 0x100 --gct build\\codes.gct

Put this in the repo's tools\\ folder. Data in .bss has no contents in the executable, so the
only way to learn what ends up there is to read the code that writes it. PowerPC builds a 32-bit
address out of two instructions (`lis` then `addi`/`ori`), which a plain byte search cannot find;
this walks the instruction stream instead, tracks what each register holds across those pairs,
and reports every place the result -- or a load/store offset from it -- lands in the range asked
for. Hits are named from the symbol map, and `--gct` extends the search into a mod's code caves.
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "port/recomp"))

import gecko                        # noqa: E402
from dol import Dol                 # noqa: E402
from symbols import SymbolMap       # noqa: E402
from gekko import decode            # noqa: E402
sys.path.insert(0, str(ROOT / "tools"))
from disasm import operands         # noqa: E402

# Instructions that leave a value we can follow in rD.
WRITES_RD = {"addi", "addic", "addic_rc", "addis", "subfic", "mulli", "lwz", "lwzu", "lbz", "lbzu",
             "lhz", "lhzu", "lha", "lhau", "lwzx", "lwzux", "lbzx", "lbzux", "lhzx", "lhzux",
             "lhax", "lhaux", "lwarx", "lwbrx", "lhbrx", "lswi", "lswx", "mfcr", "mfmsr", "mfspr",
             "mftb", "mfsr", "mfsrin", "add", "subf", "addc", "subfc", "adde", "subfe", "addze",
             "subfze", "addme", "subfme", "neg", "mullw", "mulhw", "mulhwu", "divw", "divwu"}
# ...and in rA.
WRITES_RA = {"ori", "oris", "xori", "xoris", "andi_rc", "andis_rc", "or", "and", "xor", "nor",
             "andc", "orc", "nand", "eqv", "slw", "srw", "sraw", "srawi", "rlwinm", "rlwinm_rc",
             "rlwimi", "rlwnm", "extsb", "extsh", "cntlzw"}
LOAD_STORE = {"lwz": 4, "lwzu": 4, "stw": 4, "stwu": 4, "lbz": 1, "lbzu": 1, "stb": 1, "stbu": 1,
              "lhz": 2, "lhzu": 2, "lha": 2, "lhau": 2, "sth": 2, "sthu": 2,
              "lfs": 4, "lfsu": 4, "stfs": 4, "stfsu": 4, "lfd": 8, "lfdu": 8, "stfd": 8, "stfdu": 8}
STORES = {"stw", "stwu", "stb", "stbu", "sth", "sthu", "stfs", "stfsu", "stfd", "stfdu"}


def scan(insns, lo, hi, name_of, where):
    """insns: list of (addr, Insn|None). Reports references landing in [lo, hi)."""
    regs = {}
    hits = []
    for addr, ins in insns:
        if ins is None:
            regs.clear()
            continue
        f, op = ins.f, ins.op
        # A load or store off a register we are tracking.
        if op in LOAD_STORE and f.get("ra") in regs:
            target = (regs[f["ra"]] + f["simm"]) & 0xFFFFFFFF
            if lo <= target < hi:
                hits.append((addr, ins, target, "store" if op in STORES else "load"))
        value = None
        dest = None
        if op == "addis":
            dest = f["rd"]
            base = 0 if f["ra"] == 0 else regs.get(f["ra"])
            value = None if base is None else (base + (f["uimm"] << 16)) & 0xFFFFFFFF
        elif op == "addi":
            dest = f["rd"]
            base = 0 if f["ra"] == 0 else regs.get(f["ra"])
            value = None if base is None else (base + f["simm"]) & 0xFFFFFFFF
        elif op in ("ori", "oris"):
            dest = f["ra"]
            base = regs.get(f["rs"])
            if base is not None:
                value = (base | (f["uimm"] << (16 if op == "oris" else 0))) & 0xFFFFFFFF
        elif op == "or" and f.get("rs") == f.get("rb"):        # mr
            dest = f["ra"]
            value = regs.get(f["rs"])
        if dest is not None:
            if value is None:
                regs.pop(dest, None)
            else:
                regs[dest] = value
                if lo <= value < hi:
                    hits.append((addr, ins, value, "address"))
            continue
        if op in ("b", "bc", "bclr", "bcctr"):
            if ins.lk:                                          # a call clobbers r3-r12
                for r in range(3, 13):
                    regs.pop(r, None)
            else:
                regs.clear()
            continue
        if op in WRITES_RD and "rd" in f:
            regs.pop(f["rd"], None)
        elif op in WRITES_RA and "ra" in f:
            regs.pop(f["ra"], None)
        elif op.startswith("mt") or op.startswith("dcb"):
            pass
        elif "rd" in f and op not in LOAD_STORE:
            regs.pop(f["rd"], None)

    for addr, ins, target, kind in hits:
        label = name_of(addr)
        mnemonic = ins.op.replace("_rc", ".")
        if ins.lk and ins.op in ("b", "bc", "bclr", "bcctr"):
            mnemonic += "l"
        print("  %08X  %-40s %-7s -> %08X   %-9s %s"
              % (addr, ("%s [%s]" % (label, where)) if label else where, kind, target,
                 mnemonic, operands(ins, name_of)))
    return len(hits)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dol", required=True)
    ap.add_argument("--addr", required=True, help="address of interest (hex)")
    ap.add_argument("--span", default="0x80", help="bytes after --addr to include (hex or decimal)")
    ap.add_argument("--before", default="0x10", help="bytes before --addr to include")
    ap.add_argument("--gct", help="also search a mod's code caves")
    ap.add_argument("--gct-base", default="0x8065CC80")
    ap.add_argument("--symbols", default=str(ROOT / "port/recomp/GALE01_symbols.txt"))
    args = ap.parse_args()

    dol = Dol(args.dol)
    symbols = SymbolMap(args.symbols)
    addr = int(args.addr, 16)
    lo = addr - int(args.before, 0)
    hi = addr + int(args.span, 0)

    def name_of(a):
        func = symbols.containing(a)
        if func is None:
            return None
        return func.name if a == func.addr else "%s+0x%X" % (func.name, a - func.addr)

    print("looking for references into %08X-%08X\n" % (lo, hi))
    total = 0
    for section in dol.sections:
        if section.kind != "text":
            continue
        insns = [(a, decode(a, dol.u32(a))) for a in range(section.addr, section.end, 4)]
        total += scan(insns, lo, hi, name_of, "text%d" % section.index)

    if args.gct:
        patches = gecko.parse_gct(Path(args.gct).read_bytes(), int(args.gct_base, 16))
        for hook in patches.hooks:
            insns = [(hook.cave_addr + i * 4, decode(hook.cave_addr + i * 4, w))
                     for i, w in enumerate(hook.words)]
            total += scan(insns, lo, hi, name_of,
                          "cave for %s" % (name_of(hook.hook) or "%08X" % hook.hook))
        for cave in patches.c0:
            insns = [(cave.cave_addr + i * 4, decode(cave.cave_addr + i * 4, w))
                     for i, w in enumerate(cave.words)]
            total += scan(insns, lo, hi, name_of, "C0 cave at %08X" % cave.cave_addr)
        for waddr, blob in patches.writes:
            if lo <= waddr < hi:
                print("  %08X  gecko write of %d bytes: %s"
                      % (waddr, len(blob), blob[:16].hex()))
                total += 1

    print("\n%d reference%s." % (total, "" if total == 1 else "s"))
    if not total:
        print("Nothing reaches that range through a register pair. If the data is filled in by a"
              "\ncopy or a memset, search for the code that writes the structure it belongs to.")


if __name__ == "__main__":
    main()
