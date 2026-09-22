"""Dumps the heap region table a build reserves memory from, straight out of the DOL.

    python tools\\region_table.py --dol build\\ace.dol
    python tools\\region_table.py --dol build\\ace.dol --table 80431FE8 --count 8

Put this in the repo's tools\\ folder. No game run needed: if the table lives in an initialised
section of the executable, its contents are already in the file, and this prints them. If it
lives in .bss it is filled in at boot instead, and the tool says so -- dump it at run time with
melee_port.exe --regions ADDR then.

Each entry is 28 bytes. The fields this cares about are the region's start (+8), its size (+0xC),
a kind tag (+0x10) and a skip flag (+0x14); the rest is printed raw so nothing is hidden. The
summary at the end is the point: how much of RAM each reservation takes, and what is left over
for the match heap.
"""
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "port/recomp"))

from dol import Dol, RAM_BASE, RAM_SIZE  # noqa: E402

ENTRY = 0x1C
RAM_TOP = RAM_BASE + RAM_SIZE


def mb(n):
    return "%.2f MB" % (n / (1024.0 * 1024.0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dol", required=True)
    ap.add_argument("--table", default="80431FE8", help="address of the first entry (hex)")
    ap.add_argument("--count", type=int, default=8, help="entries to print")
    ap.add_argument("--raw", type=int, default=0, help="also dump this many words around the table")
    args = ap.parse_args()

    dol = Dol(args.dol)
    table = int(args.table, 16)

    section = next((s for s in dol.sections if s.addr <= table < s.end), None)
    if section is None:
        print("%08X is not inside any section of %s." % (table, args.dol))
        if dol.bss_addr <= table < dol.bss_addr + dol.bss_size:
            print("It is in .bss (%08X-%08X), so the table is built at boot and the file holds"
                  % (dol.bss_addr, dol.bss_addr + dol.bss_size))
            print("nothing to read. Dump it at run time instead:")
            print('  melee_port.exe --iso "<iso>" --regions %08X '
                  "--trace-func HSD_CreateMainHeap:10 --log-file regions.log" % table)
        return 1
    print("table at %08X, inside %s%d (%08X-%08X) -- initialised data, read from the file\n"
          % (table, section.kind, section.index, section.addr, section.end))

    if args.raw:
        for i in range(args.raw):
            addr = table + i * 4
            print("  %08X  %08X" % (addr, dol.u32(addr)))
        print()

    regions = []
    for i in range(args.count):
        base = table + i * ENTRY
        if base + ENTRY > section.end:
            print("entry %d runs past the end of the section; stopping." % i)
            break
        words = [dol.u32(base + o) for o in range(0, ENTRY, 4)]
        start, size, kind, skip = words[2], words[3], words[4], words[5]
        print("entry %d @ %08X" % (i, base))
        print("  raw      %s" % " ".join("%08X" % w for w in words))
        print("  start    %08X" % start)
        print("  size     %08X  (%s)" % (size, mb(size)))
        print("  kind     %08X" % kind)
        print("  skip     %08X%s" % (skip, "   <-- not reserved" if skip else ""))
        plausible = RAM_BASE <= start < RAM_TOP and 0 < size <= RAM_SIZE and start + size <= RAM_TOP
        if plausible:
            print("  range    %08X-%08X%s" % (start, start + size,
                                              "   (reaches the top of RAM)"
                                              if start + size >= RAM_TOP - 0x20 else ""))
            regions.append((start, size, kind, skip, i))
        else:
            print("  range    implausible -- probably past the end of the table")
        print()

    if not regions:
        return 0
    print("--- what RAM is spoken for ---")
    reserved = 0
    for start, size, kind, skip, i in sorted(regions):
        tag = "skipped" if skip else "reserved"
        if not skip:
            reserved += size
        print("  %08X-%08X  %10s  kind %X  entry %d  %s"
              % (start, start + size, mb(size), kind, i, tag))
    print("\n  reserved total   %s of %s" % (mb(reserved), mb(RAM_SIZE)))
    top = max(s + z for s, z, _, _, _ in regions)
    if top < RAM_TOP:
        print("  above the table  %08X-%08X  %s unaccounted for"
              % (top, RAM_TOP, mb(RAM_TOP - top)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
