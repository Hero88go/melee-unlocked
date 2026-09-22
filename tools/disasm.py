"""Disassembles a range of a DOL, for looking at what a mod put in it.

    python tools\\disasm.py --dol build\\ace.dol --start 8032C540 --end 8032C670
    python tools\\disasm.py --dol build\\ace.dol --func HSD_OSInit

Put this in the repo's tools\\ folder. Branch targets are resolved against the symbol map, so a
jump out of a mod's code into the retail game is named. Operand formatting covers what the game
and hand-written mod code use; anything else prints its decoded fields.
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "port/recomp"))

from dol import Dol            # noqa: E402
from symbols import SymbolMap  # noqa: E402
from gekko import decode       # noqa: E402

D_FORM_LOAD_STORE = {"lwz", "lwzu", "lbz", "lbzu", "lhz", "lhzu", "lha", "lhau", "stw", "stwu",
                     "stb", "stbu", "sth", "sthu", "lmw", "stmw", "lfs", "lfsu", "lfd", "lfdu",
                     "stfs", "stfsu", "stfd", "stfdu"}
FLOAT_LS = {"lfs", "lfsu", "lfd", "lfdu", "stfs", "stfsu", "stfd", "stfdu"}


def operands(ins, name_of):
    f = ins.f
    op = ins.op
    if op in ("b", "bc"):
        target = ins.branch_target
        label = name_of(target)
        suffix = ("  # %s" % label) if label else ""
        if op == "b":
            return "0x%08X%s" % (target, suffix)
        return "%d, %d, 0x%08X%s" % (f["bo"], f["bi"], target, suffix)
    if op in ("bclr", "bcctr"):
        return "" if (f["bo"] & 0x14) == 0x14 else "%d, %d" % (f["bo"], f["bi"])
    if op in D_FORM_LOAD_STORE:
        reg = "f%d" % f["fd"] if op in FLOAT_LS else "r%d" % f["rd"]
        return "%s, %d(r%d)" % (reg, f["simm"], f["ra"])
    if op in ("addi", "addic", "addic_rc", "subfic", "mulli"):
        return "r%d, r%d, %d" % (f["rd"], f["ra"], f["simm"])
    if op == "addis":
        return "r%d, r%d, 0x%04X" % (f["rd"], f["ra"], f["uimm"])
    if op in ("ori", "oris", "xori", "xoris", "andi_rc", "andis_rc"):
        return "r%d, r%d, 0x%04X" % (f["ra"], f["rs"], f["uimm"])
    if op in ("cmpi", "cmpli"):
        return "cr%d, r%d, %d" % (f["crfd"], f["ra"], f["simm"] if op == "cmpi" else f["uimm"])
    if op in ("cmp", "cmpl"):
        return "cr%d, r%d, r%d" % (f["crfd"], f["ra"], f["rb"])
    if op in ("or", "and", "xor", "nor", "andc", "orc", "nand", "eqv"):
        if op == "or" and f["rs"] == f["rb"]:
            return "r%d, r%d       # mr" % (f["ra"], f["rs"])
        return "r%d, r%d, r%d" % (f["ra"], f["rs"], f["rb"])
    if op in ("add", "subf", "addc", "subfc", "adde", "subfe", "mullw", "divw", "divwu",
              "mulhw", "mulhwu"):
        return "r%d, r%d, r%d" % (f["rd"], f["ra"], f["rb"])
    if op in ("neg", "extsb", "extsh", "cntlzw"):
        return "r%d, r%d" % (f["ra"], f["rs"])
    if op in ("rlwinm", "rlwimi"):
        return "r%d, r%d, %d, %d, %d" % (f["ra"], f["rs"], f["sh"], f["mb"], f["me"])
    if op in ("slw", "srw", "sraw"):
        return "r%d, r%d, r%d" % (f["ra"], f["rs"], f["rb"])
    if op == "srawi":
        return "r%d, r%d, %d" % (f["ra"], f["rs"], f["sh"])
    if op in ("mfspr", "mtspr"):
        spr = {1: "xer", 8: "lr", 9: "ctr"}.get(f["spr"], "spr%d" % f["spr"])
        return ("r%d, %s" % (f["rd"], spr)) if op == "mfspr" else ("%s, r%d" % (spr, f["rs"]))
    if op in ("lwzx", "lbzx", "lhzx", "lhax", "stwx", "stbx", "sthx", "lwzux", "stwux"):
        return "r%d, r%d, r%d" % (f["rd"], f["ra"], f["rb"])
    if op == "mfcr":
        return "r%d" % f["rd"]
    if op == "mtcrf":
        return "0x%02X, r%d" % (f["crm"], f["rs"])
    return " ".join("%s=%d" % (k, v) for k, v in sorted(f.items()) if k in ("rd", "ra", "rb", "simm"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dol", required=True)
    ap.add_argument("--start", help="first address (hex)")
    ap.add_argument("--end", help="last address, exclusive (hex)")
    ap.add_argument("--func", help="a symbol name to disassemble instead")
    ap.add_argument("--symbols", default=str(ROOT / "port/recomp/GALE01_symbols.txt"))
    args = ap.parse_args()

    dol = Dol(args.dol)
    symbols = SymbolMap(args.symbols)

    if args.func:
        func = symbols.by_name.get(args.func)
        if func is None:
            raise SystemExit("%s: not in the symbol map" % args.func)
        start, end = func.addr, func.end
    else:
        if not args.start or not args.end:
            raise SystemExit("give --start and --end, or --func")
        start, end = int(args.start, 16), int(args.end, 16)

    def name_of(addr):
        func = symbols.containing(addr)
        if func is None:
            return None
        return func.name if addr == func.addr else "%s+0x%X" % (func.name, addr - func.addr)

    for addr in range(start, end, 4):
        word = dol.u32(addr)
        ins = decode(addr, word)
        label = name_of(addr)
        marker = "  <- %s" % label if label and (addr == start or label.endswith("+0x0") or
                                                 (symbols.containing(addr) and symbols.containing(addr).addr == addr)) else ""
        if ins is None:
            print("%08X  %08X  .word%s" % (addr, word, marker))
            continue
        mnemonic = ins.op.replace("_rc", ".")
        if ins.lk and ins.op in ("b", "bc", "bclr", "bcctr"):
            mnemonic += "l"
        if ins.rc and not ins.op.endswith("_rc"):
            mnemonic += "."
        print("%08X  %08X  %-9s %s%s" % (addr, word, mnemonic, operands(ins, name_of), marker))


if __name__ == "__main__":
    main()
