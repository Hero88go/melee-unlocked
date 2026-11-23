"""Writes the two generated headers the Source Port host needs, without a disc.

Usage: python port/recomp/host_headers.py --out DIR [--symbols PATH] [--hle PATH]

recomp.py writes hle_decls.h and guest_symbols.h next to the translation, and it needs main.dol for
that. With --no-slippi both headers depend only on the symbol map and the HLE list in this
repository, so this script writes the same two files from those alone (CI has no disc). Nothing
else from the translation is produced. Output is byte-identical to `recomp.py --no-slippi`.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from recomp import ROOT, c_ident, write_if_changed  # noqa: E402
from symbols import SymbolMap  # noqa: E402


def hle_names(path):
    names = set()
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if line:
            names.add(line)
    return names


def hle_decls(symbols, hle):
    text = ["// Generated. HLE overrides referenced by generated code.\n#pragma once\n#include \"ppc.h\"\nnamespace hle {\n"]
    for name in sorted(n for n in hle if n in symbols.by_name):
        text.append("void %s(ppc::Context& c, uint8_t* m);\n" % name)
    text.append("}\n")
    return "".join(text)


def guest_symbols(symbols):
    text = ["// Generated guest symbol addresses.\n#pragma once\n#include <cstdint>\nnamespace gs {\n"]
    used = set()
    for addr, name in sorted(symbols.names.items()):
        ident = c_ident(name)
        if ident in used:
            ident = "%s_%08X" % (ident, addr)
        used.add(ident)
        text.append("constexpr uint32_t %s = 0x%08Xu;\n" % (ident, addr))
    text.append("}\n")
    return "".join(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--symbols", default=str(ROOT / "port/recomp/GALE01_symbols.txt"))
    ap.add_argument("--hle", default=str(ROOT / "port/recomp/hle_list.txt"))
    args = ap.parse_args()
    symbols = SymbolMap(args.symbols)
    args.out.mkdir(parents=True, exist_ok=True)
    write_if_changed(args.out / "hle_decls.h", hle_decls(symbols, hle_names(args.hle)))
    write_if_changed(args.out / "guest_symbols.h", guest_symbols(symbols))
    print("host headers:", args.out / "hle_decls.h", args.out / "guest_symbols.h")


if __name__ == "__main__":
    main()
