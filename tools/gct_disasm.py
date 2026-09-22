"""Disassembles one C2 cave out of a Gecko code table.

    python tools\\gct_disasm.py --gct build\\codes.gct --base 8065CC80 --hook 803753B0

Put this in the repo's tools\\ folder. The cave's instructions live inside the table, so its
addresses depend on where the table is loaded (--base, the address the build reads the file to).
Branch targets are resolved against the symbol map, and any instruction carrying a constant that
looks like a RAM bound is flagged, since that is usually what a mod assumes about the machine.
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "port/recomp"))

import gecko                   # noqa: E402
from symbols import SymbolMap  # noqa: E402
from gekko import decode       # noqa: E402
sys.path.insert(0, str(ROOT / "tools"))
from disasm import operands    # noqa: E402

# Constants worth noticing in a cave: the top of a GameCube's RAM and its neighbours.
RAM_MARKERS = {0x8180, 0x817F, 0x0180, 0x8000}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gct", required=True)
    ap.add_argument("--base", default="8065CC80", help="address the table is loaded at (hex)")
    ap.add_argument("--hook", help="the hooked address whose cave to disassemble (hex)")
    ap.add_argument("--list", action="store_true", help="list the hooks instead, largest cave first")
    ap.add_argument("--symbols", default=str(ROOT / "port/recomp/GALE01_symbols.txt"))
    args = ap.parse_args()

    patches = gecko.parse_gct(Path(args.gct).read_bytes(), int(args.base, 16))
    symbols = SymbolMap(args.symbols)

    def name_of(addr):
        func = symbols.containing(addr)
        if func is None:
            return None
        return func.name if addr == func.addr else "%s+0x%X" % (func.name, addr - func.addr)

    if args.list or not args.hook:
        for hook in sorted(patches.hooks, key=lambda h: -len(h.words))[:25]:
            print("%08X  %-44s %4d instructions  cave at %08X"
                  % (hook.hook, (name_of(hook.hook) or "?")[:44], len(hook.words), hook.cave_addr))
        return

    want = int(args.hook, 16)
    hook = next((h for h in patches.hooks if h.hook == want), None)
    if hook is None:
        raise SystemExit("no C2 hook at %08X (try --list)" % want)

    print("cave for the hook at %08X (%s), %d instructions, loaded at %08X\n"
          % (hook.hook, name_of(hook.hook) or "?", len(hook.words), hook.cave_addr))
    for i, word in enumerate(hook.words):
        addr = hook.cave_addr + i * 4
        ins = decode(addr, word)
        if ins is None:
            print("%08X  %08X  .word" % (addr, word))
            continue
        mnemonic = ins.op.replace("_rc", ".")
        if ins.lk and ins.op in ("b", "bc", "bclr", "bcctr"):
            mnemonic += "l"
        if ins.rc and not ins.op.endswith("_rc"):
            mnemonic += "."
        note = ""
        if ins.op in ("addis", "ori", "oris", "addi", "cmpi", "cmpli") and \
                (ins.f.get("uimm") in RAM_MARKERS or (ins.f.get("simm", 0) & 0xFFFF) in RAM_MARKERS):
            note = "   <-- looks like a RAM bound"
        print("%08X  %08X  %-9s %s%s" % (addr, word, mnemonic, operands(ins, name_of), note))


if __name__ == "__main__":
    main()
