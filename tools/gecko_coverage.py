#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""How much of the Gecko codes found in this tree the Source Port can run.

The Source Port is the game compiled from C: there is no PowerPC in memory for a code to patch and
no console memory image. A code runs there only when everything it does comes down to reading and
writing game variables that the native game keeps with the console's layout (tools/gecko_targets.py
proves which). This file holds the rules, in the form the unit test runs:

  compile_code()  what port/runtime/host/user_gecko.cpp does when it reads a code: every line is
                  checked and turned into a step with its address worked out, or the code is refused
                  with the reason the settings panel shows;
  run_plan()      what it does once per frame with an enabled code: the Gecko handler's own rules
                  for conditions, registers and writes.

and a read-only report over the code lists in the tree (SOURCES): for every code whether it runs,
is built in (the Source Port carries it as C, behind a switch of its own) or cannot run, with the
reason and its class; then every patch that still needs a native rewrite, by function, ranked by
how many lists carry it.

Usage: python tools/gecko_coverage.py [--out FILE] [--legacy]
  --out FILE  also write the report there
  --legacy    judge with the rules before 0.8.8 (write types 00 to 06 only, no built-in table), to
              measure what changed; the table of variables is whatever tools/gecko_targets.py
              proves now
"""

import argparse
import glob
import hashlib
import json
import struct
import sys
from collections import Counter, OrderedDict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gecko_targets  # noqa: E402

REPO = gecko_targets.REPO

# Where code lists are looked for (globs from the repository root). A file whose bytes were already
# seen under another path is listed once.
SOURCES = [
    "port/slippi_sys/GameSettings/GALE01r2.ini",
    "port/slippi_sys_general/GameSettings/GALE01r2.ini",
    "port/slippi_sys_playback/GameSettings/GALE01r2.ini",
    "port/slippi_sys*/bootloader.gct",
    "run-source/rel0871a/pkg/*/Sys*/GameSettings/GALE01*.ini",
    "run-source/rel0871a/pkg/*/Sys*/bootloader.gct",
    "run-source/rel0871a/pkg/*/Sys*/Slippi/InjectionLists/*.json",
    "run-source/m-ex-official/asm/codes.ini",
    "run-source/rel0871a/gk/codes.ini",
    "run-source/rel09-b4-ace/*.ini",
    "run-source/*/GeckoCodes.ini",
    "run-source/*/*/GeckoCodes.ini",
    "run-source/*/*/*/GeckoCodes.ini",
    "run-source/*/*/*/*/GeckoCodes.ini",
    "run-source/*/*/*/*/*/GeckoCodes.ini",
]

BASE = 0x80000000       # the base address and the pointer both start here
MEM_LO, MEM_HI = 0x80003100, 0x81800000

# Why a code cannot run: the classes of the report.
CLASSES = OrderedDict([
    ("code-write", "overwrites instructions of a game function (00/02/04/06 into code)"),
    ("code-inject", "injects PowerPC into a game function (C2)"),
    ("code-run", "runs or branches to PowerPC of its own (C0, C6)"),
    ("pointer", "follows a pointer read from game memory (40, 48, or a register used as an address)"),
    ("type", "uses a code type with no data-only meaning here (loops, jumps, register compares, switches)"),
    ("no-symbol", "touches memory that is no game variable (the heap, the stack, the handler)"),
    ("symbol", "touches a variable the Source Port cannot offer (pointers, private, constant, layout)"),
    ("past-end", "runs past the end of one variable"),
    ("outside", "addresses outside game memory"),
    ("base", "uses a base address whose value depends on a condition that has ended"),
    ("malformed", "is cut short or empty"),
])


class Refused(Exception):
    """A code that cannot run: its class (a key of CLASSES) and the reason shown to the player."""

    def __init__(self, cls, reason):
        Exception.__init__(self, reason)
        self.cls, self.reason = cls, reason


def hex8(value):
    return "%08X" % (value & 0xFFFFFFFF)


def type_name(word):
    return "%02X" % (word >> 24)


class Compiler:
    """One code being read on the Source Port. `symbols` is the table tools/gecko_targets.py
    builds. Every rule is the console handler's own (port/slippi_sys/codehandler.bin, loaded at
    80001800), with one difference: the handler runs every code as one list, so a base address, a
    pointer or an open if left behind by one code is still there for the next and an F0 line ends
    the whole list; here each code starts with both at 80000000 and no condition, and an F0 ends
    that code only (the note above Compiler in user_gecko.cpp)."""

    def __init__(self, symbols):
        self.symbols = symbols
        self.plan = []
        self.reads = False
        # The base address and the pointer, when their value is known here, and the if-depth each
        # was last set at (0: outside every if). A value set inside an if holds only until that if
        # ends: after it the register is one of two values, so it is not known.
        self.base = {"ba": BASE, "po": BASE}
        self.set_at = {"ba": 0, "po": 0}
        self.depth = 0

    # -- addresses --

    def register(self, word):
        return "po" if (word >> 24) & 0x10 else "ba"

    def base_of(self, word):
        """What a line counts from, the handler's r12: the pointer in full for the types with bit
        0x10, else only the top seven bits of the base address (rlwinm r12,r6,0,0,6 at 80001FCC)."""
        name = self.register(word)
        if self.base[name] is None:
            raise Refused("base", "uses the %s after a condition that changed it has ended"
                          % ("pointer" if name == "po" else "base address"))
        return self.base[name] if name == "po" else self.base[name] & 0xFE000000

    def address(self, word):
        return (self.base_of(word) + (word & 0x01FFFFFF)) & 0xFFFFFFFF

    def check(self, addr, length, reading=False):
        verb = "reads" if reading else "writes"
        if addr < MEM_LO or addr > MEM_HI or length > MEM_HI - addr:
            raise Refused("outside", "%s outside game memory (%s)" % (verb, hex8(addr)))
        if length == 0:
            return
        reason = gecko_targets.check_write(self.symbols, addr, length)
        if not reason:
            return
        symbol = gecko_targets.find(self.symbols, addr)
        if symbol is None:
            cls = "no-symbol"
        elif symbol.kind == gecko_targets.CODE:
            cls = "code-write"
        elif symbol.kind == gecko_targets.OK:
            cls = "past-end"
        else:
            cls = "symbol"
        if reading and reason.startswith("writes"):
            reason = "reads" + reason[len("writes"):]
        raise Refused(cls, reason)

    def write(self, addr, data):
        self.check(addr, len(data))
        if data:
            self.plan.append(("write", addr, bytes(data)))

    def read(self, addr, length):
        self.check(addr, length, reading=True)
        self.reads = True

    # -- conditions --

    def close_to(self, open_ifs, otherwise=False):
        """Ifs end until `open_ifs` stay open (and, with `otherwise`, the innermost one turns into
        its else). A base set inside a block that has ended is no longer known."""
        for name in ("ba", "po"):
            if self.set_at[name] > open_ifs or (otherwise and self.set_at[name] >= max(open_ifs, 1)):
                self.base[name], self.set_at[name] = None, 0
        self.depth = open_ifs

    def reset_bases(self, value):
        """The second word of a terminator: a nonzero half sets the base address (high) or the
        pointer (low) to that half times 0x10000, whatever the conditions were."""
        if value >> 16:
            self.base["ba"], self.set_at["ba"] = (value >> 16) << 16, 0
        if value & 0xFFFF:
            self.base["po"], self.set_at["po"] = (value & 0xFFFF) << 16, 0

    # -- the lines --

    def compile(self, lines):
        if not lines:
            raise Refused("malformed", "has no code lines")
        i = 0
        while i < len(lines):
            word, value = lines[i]
            kind = word >> 24
            family, sub = kind >> 5, (kind >> 1) & 7
            if kind == 0xF0:
                break   # the end of a code list
            if family == 0:
                i = self.write_line(lines, i, sub)
            elif family == 1:
                self.if_line(word, value, sub)
            elif family == 2:
                self.base_line(word, value, sub)
            elif family == 3:
                raise Refused("type", "uses a loop or a jump (code type %s)" % type_name(word))
            elif family == 4:
                self.register_line(word, value, sub)
            elif family == 5:
                raise Refused("type", "compares registers or counts frames (code type %s)" % type_name(word))
            elif family == 6:
                self.code_line(word, sub)
            elif kind == 0xE0:
                self.plan.append(("end",))
                self.close_to(0)
                self.reset_bases(value)
            elif kind == 0xE2:
                count, otherwise = word & 0x1F, (word >> 20) & 1   # five bits of count (80002740)
                self.plan.append(("endif", count, otherwise))
                self.close_to(max(0, self.depth - count), bool(otherwise))
                self.reset_bases(value)
            else:
                raise Refused("type", "uses code type %s, which has no meaning here" % type_name(word))
            i += 1
        return self.plan

    def write_line(self, lines, i, sub):
        word, value = lines[i]
        addr = self.address(word)
        if sub == 0:
            self.write(addr, bytes([value & 0xFF]) * ((value >> 16) + 1))
        elif sub == 1:
            self.write(addr, struct.pack(">H", value & 0xFFFF) * ((value >> 16) + 1))
        elif sub == 2:
            self.write(addr & ~3, struct.pack(">I", value))   # rounded down to a word (80002050)
        elif sub == 3:
            rows = (value + 7) // 8
            if i + rows >= len(lines) and rows:
                raise Refused("malformed", "has a string write cut short")
            data = b"".join(struct.pack(">II", *lines[i + 1 + k]) for k in range(rows))[:value]
            self.write(addr, data)
            i += rows
        elif sub == 4:
            if i + 1 >= len(lines):
                raise Refused("malformed", "has a serial write cut short")
            second, step = lines[i + 1]
            width = {0: 1, 1: 2, 2: 4}.get(second >> 28)
            if width is None:
                raise Refused("malformed", "has a serial write of an unknown size")
            for k in range(((second >> 16) & 0xFFF) + 1):
                item = (value + k * step) & (0xFFFFFFFF >> (32 - 8 * width))
                self.write((addr + k * (second & 0xFFFF)) & 0xFFFFFFFF, item.to_bytes(width, "big"))
            i += 1
        else:
            raise Refused("type", "uses code type %s, which has no meaning here" % type_name(word))
        return i

    def if_line(self, word, value, sub):
        if word & 1:   # an address ending in 1: one endif first
            self.plan.append(("endif", 1, 0))
            self.close_to(max(0, self.depth - 1))
        # The line's 25 bits, the 1 included, then rounded down to what is read (80002104, 80002110).
        width = 4 if sub < 4 else 2
        addr = (self.base_of(word) + (word & 0x01FFFFFF)) & 0xFFFFFFFF & ~(width - 1)
        self.read(addr, width)
        if width == 4:
            self.plan.append(("if", addr, 4, sub & 3, value, 0xFFFFFFFF))
        else:
            self.plan.append(("if", addr, 2, sub & 3, value & 0xFFFF, ~(value >> 16) & 0xFFFF))
        self.depth += 1

    def base_line(self, word, value, sub):
        target = "po" if sub & 4 else "ba"
        called = "pointer" if target == "po" else "base address"
        # The handler tests one bit of each flag digit (80002198 to 800021F0).
        add, relative, by_register = (word >> 20) & 1, (word >> 16) & 1, (word >> 12) & 1
        if sub & 3 == 0:
            raise Refused("pointer", "loads the %s from game memory (code type %s): a pointer there is a PC address here"
                          % (called, type_name(word)))
        if sub & 3 == 3:
            raise Refused("pointer", "takes the address of its own lines (code type %s)" % type_name(word))
        if by_register:
            raise Refused("pointer", "adds a register to an address (code type %s)" % type_name(word))
        if sub & 3 == 1:   # set: the number, plus what a line counts from, plus the register in full
            operand = (value + (self.base_of(word) if relative else 0)) & 0xFFFFFFFF
            if add:
                if self.base[target] is None:
                    raise Refused("base", "uses the %s after a condition that changed it has ended" % called)
                operand = (self.base[target] + operand) & 0xFFFFFFFF
            self.base[target], self.set_at[target] = operand, self.depth
            return
        # store: the register's value in full, a console address, written as a number. The handler
        # counts the address from the base whether or not the relative bit is set (stwx r6,r12,r4
        # at 8000221C): 44000000 00400034 writes at 80400034.
        operand = (self.base_of(word) + value) & 0xFFFFFFFF
        if self.base[target] is None:
            raise Refused("base", "uses the %s after a condition that changed it has ended" % called)
        self.write(operand, struct.pack(">I", self.base[target]))

    def register_line(self, word, value, sub):
        high, relative, n = (word >> 20) & 0xF, (word >> 16) & 0xF, word & 0xF
        if sub == 0:     # 80: set or add a number (relative: bit 16 alone; add: bit 20 alone)
            operand = (value + (self.base_of(word) if relative & 1 else 0)) & 0xFFFFFFFF
            self.plan.append(("grset", n, operand, high & 1))
        elif sub in (1, 2):   # 82 load, 84 store
            width = {0: 1, 1: 2, 2: 4}.get(high)
            if width is None:
                raise Refused("malformed", "has a register %s of an unknown size" % ("load" if sub == 1 else "store"))
            addr = (value + (self.base_of(word) if relative & 1 else 0)) & 0xFFFFFFFF
            if sub == 1:
                self.read(addr, width)
                self.plan.append(("grload", n, addr, width))
            else:
                count = ((word >> 4) & 0xFFF) + 1
                self.check(addr, width * count)
                self.plan.append(("grstore", n, addr, width, count))
        elif sub in (3, 4):   # 86: register and number or memory; 88: two registers
            if high > 0xA:
                raise Refused("malformed", "has a register operation of an unknown kind")
            if relative == 0:
                self.plan.append(("grop", n, high, value) if sub == 3 else ("gropgr", n, high, value & 0xF))
            elif relative == 2 and sub == 3:
                # The number is an address, always counted from the base (add r19,r12,r19 at
                # 800023B8): 86020002 00400034 reads 80400034.
                addr = (self.base_of(word) + value) & 0xFFFFFFFF
                self.read(addr, 4)
                self.plan.append(("gropmem", n, high, addr))
            else:
                raise Refused("pointer", "uses a register as an address (code type %s)" % type_name(word))
        else:
            raise Refused("pointer", "copies memory between addresses held in registers (code type %s)" % type_name(word))

    def code_line(self, word, sub):
        if sub == 1:
            target = None
            if self.base[self.register(word)] is not None:
                target = gecko_targets.find(self.symbols, self.address(word) & ~3)
            if target is not None and target.kind == gecko_targets.CODE:
                raise Refused("code-inject", "injects PowerPC code into %s (C2), which this build cannot run" % target.name)
            raise Refused("code-inject", "injects PowerPC code (C2), which this build cannot run")
        if sub in (0, 3):
            raise Refused("code-run", "runs PowerPC code of its own (code type %s)" % type_name(word))
        raise Refused("type", "uses code type %s, which has no meaning here" % type_name(word))


def compile_code(lines, symbols):
    """The plan for a code's lines [(word, value)], and whether it reads game variables. Raises
    Refused."""
    compiler = Compiler(symbols)
    return compiler.compile(lines), compiler.reads


def operate(a, b, op):
    """One Gecko register operation on 32-bit values (code types 86 and 88). `a` is the register
    the result goes to, `b` the number, the memory or the other register. The handler's shifts and
    its rotate are "slw r4,r9,r4" and the like (800023F4 to 8000240C) with a in r4 and b in r9: the
    OPERAND is shifted, BY the register."""
    if op == 0:
        r = a + b
    elif op == 1:
        r = a * b
    elif op == 2:
        r = a | b
    elif op == 3:
        r = a & b
    elif op == 4:
        r = a ^ b
    elif op == 5:
        r = 0 if a & 0x20 else b << (a & 0x1F)
    elif op == 6:
        r = 0 if a & 0x20 else b >> (a & 0x1F)
    elif op == 7:
        n = a & 0x1F
        r = (b << n) | (b >> (32 - n)) if n else b
    elif op == 8:
        signed = b - 0x100000000 if b & 0x80000000 else b
        r = signed >> min(a & 0x3F, 31)
    else:
        fa, fb = (struct.unpack(">f", struct.pack(">I", x))[0] for x in (a, b))
        try:
            r = struct.unpack(">I", struct.pack(">f", fa + fb if op == 9 else fa * fb))[0]
        except OverflowError:
            sign = (a ^ b) & 0x80000000 if op == 10 else a & 0x80000000
            r = sign | 0x7F800000
    return r & 0xFFFFFFFF


def run_plan(plan, read, write, registers):
    """Runs one code for one frame. `read(addr, length)` returns big-endian bytes, `write(addr,
    bytes)` stores them, `registers` is the 16 Gecko registers (kept between frames)."""
    skipping = 0   # 0: running; else how many ifs deep, counted from the first that failed
    for op in plan:
        kind = op[0]
        if kind == "if":
            if skipping:
                skipping += 1
                continue
            _, addr, width, compare, value, mask = op
            memory = int.from_bytes(read(addr, width), "big") & mask
            passed = (memory == value, memory != value, memory > value, memory < value)[compare]
            if not passed:
                skipping = 1
        elif kind == "endif":
            # The handler keeps one bit per open if; an else flips the innermost bit only when the
            # one above it is clear (8000274C to 80002754), so inside an outer block that is being
            # skipped it changes nothing: the last case below.
            skipping = max(0, skipping - op[1])
            if op[2]:
                skipping = 1 if skipping == 0 else 0 if skipping == 1 else skipping
        elif kind == "end":
            skipping = 0
        elif skipping:
            continue
        elif kind == "write":
            write(op[1], op[2])
        elif kind == "grset":
            registers[op[1]] = (op[2] + (registers[op[1]] if op[3] else 0)) & 0xFFFFFFFF
        elif kind == "grload":
            registers[op[1]] = int.from_bytes(read(op[2], op[3]), "big")
        elif kind == "grstore":
            _, n, addr, width, count = op
            item = (registers[n] & (0xFFFFFFFF >> (32 - 8 * width))).to_bytes(width, "big")
            write(addr, item * count)
        elif kind == "grop":
            registers[op[1]] = operate(registers[op[1]], op[3], op[2])
        elif kind == "gropgr":
            registers[op[1]] = operate(registers[op[1]], registers[op[3]], op[2])
        elif kind == "gropmem":
            registers[op[1]] = operate(registers[op[1]], int.from_bytes(read(op[3], 4), "big"), op[2])


def legacy_refusal(lines, symbols):
    """The rules before 0.8.8 (write types 00 to 06 on the base address only), as (class, reason),
    or None when the code ran."""
    if not lines:
        return "malformed", "has no code lines"
    i = 0
    while i < len(lines):
        word, value = lines[i]
        kind = word >> 24
        i += 1
        if kind in (0xE0, 0xF0):
            continue
        if kind & 0x10:
            return "type", "writes through the pointer register (code type %s)" % type_name(word)
        if kind > 0x07:
            if kind in (0xC2, 0xC3):
                return "code-inject", "injects PowerPC code (C2), which this build cannot run"
            return "type", "uses code type %s, which needs the Gecko handler" % type_name(word)
        addr = BASE | (word & 0x01FFFFFF)
        sub = (kind >> 1) & 3
        length = ((value >> 16) + 1) * (1 if sub == 0 else 2) if sub < 2 else 4 if sub == 2 else value
        if sub == 3:
            i += (value + 7) // 8
            if i > len(lines):
                return "malformed", "has a string write cut short"
        try:
            Compiler(symbols).check(addr, length)
        except Refused as refused:
            return refused.cls, refused.reason
    return None


# ---- the report ----

def read_gct(data):
    """A .gct file as one code's lines (the 8-byte header and the F0 terminator left out)."""
    words = struct.unpack(">%dI" % (len(data) // 4), data[:len(data) // 4 * 4])
    lines = list(zip(words[0::2], words[1::2]))
    if lines and lines[0] == (0x00D0C0DE, 0x00D0C0DE):
        lines = lines[1:]
    while lines and lines[-1][0] >> 24 == 0xF0:
        lines = lines[:-1]
    return lines


def collect():
    """[(path, kind, payload)] for every distinct code list: kind "codes" with [(name, lines)], or
    "sites" with [(name, address, code type)] for a list that names injection addresses only."""
    seen, out = {}, []
    for pattern in SOURCES:
        for name in sorted(glob.glob(str(REPO / pattern))):
            path = Path(name)
            rel = path.relative_to(REPO).as_posix()
            data = path.read_bytes()
            digest = hashlib.sha1(data).hexdigest()
            if rel in seen.values():
                continue
            if digest in seen:
                out.append((rel, "same", seen[digest]))
                continue
            seen[digest] = rel
            if path.suffix == ".gct":
                out.append((rel, "codes", [(path.name, read_gct(data))]))
            elif path.suffix == ".json":
                try:
                    details = json.loads(data.decode("utf-8", errors="replace")).get("Details", [])
                except ValueError:
                    details = []
                out.append((rel, "sites", [(d.get("Name", "").split("[")[0].strip(),
                                            int(d.get("InjectionAddress", "0"), 16), d.get("Codetype", ""))
                                           for d in details]))
            else:
                out.append((rel, "codes", gecko_targets.read_code_list(data.decode("utf-8", errors="replace"))))
    return out


def judge(lines, symbols, built_in, legacy=False):
    """("runs" | "built-in" | "cannot", class, detail) for one code or one patch."""
    if legacy:
        refusal = legacy_refusal(lines, symbols)
        return ("runs", "", "") if refusal is None else ("cannot",) + refusal
    label = gecko_targets.built_in_label(lines, *built_in) if lines else None
    if label:
        return "built-in", "", label
    try:
        plan, reads = compile_code(lines, symbols)
    except Refused as refused:
        return "cannot", refused.cls, refused.reason
    return "runs", "", "reads and writes game variables" if reads else "writes game variables"


def site_name(symbols, addr):
    symbol = gecko_targets.find(symbols, addr)
    return symbol.name if symbol is not None else "(no symbol at %s)" % hex8(addr)


def report(legacy=False):
    symbols = gecko_targets.build()
    built_in = gecko_targets.built_in_units()
    built_in_words = {key[0] & 0x01FFFFFF for key in built_in[1]}
    out = []
    say = out.append
    say("Gecko code coverage on the Source Port%s" % (" (rules before 0.8.8)" if legacy else ""))
    say("variables a code may write or read: %d rows, %d bytes"
        % (sum(s.kind == gecko_targets.OK for s in symbols),
           sum(s.size for s in symbols if s.kind == gecko_targets.OK)))
    say("")
    codes = Counter()
    patches = Counter()
    classes = Counter()
    needed = {}   # function -> {"lists": set, "names": set, "kinds": Counter}
    for rel, kind, payload in collect():
        if kind == "same":
            say("== %s: the same bytes as %s" % (rel, payload))
            say("")
            continue
        if kind == "sites":
            # Addresses only: no instruction words to recognise, so each site is judged by where it is.
            by_name = OrderedDict()
            for name, addr, codetype in payload:
                by_name.setdefault(name, []).append((addr, codetype))
            say("== %s: %d injection sites (addresses only, no code words)" % (rel, len(payload)))
            for name, sites in by_name.items():
                known = sum((addr & 0x01FFFFFF) in built_in_words for addr, _ in sites)
                say("  %-52s %3d sites, %d at an address a built-in patch uses" % (name[:52], len(sites), known))
                for addr, codetype in sites:
                    if (addr & 0x01FFFFFF) in built_in_words:
                        continue
                    entry = needed.setdefault(site_name(symbols, addr), {"lists": set(), "names": set(), "kinds": Counter()})
                    entry["lists"].add(rel)
                    entry["names"].add(name)
                    entry["kinds"][codetype or "?"] += 1
            say("")
            continue
        say("== %s: %d codes" % (rel, len(payload)))
        for name, lines in payload:
            verdict, cls, detail = judge(lines, symbols, built_in, legacy)
            codes[verdict] += 1
            units = gecko_targets.code_units(lines)
            tally = Counter()
            for unit in units:
                if len(unit) == 1 and unit[0][0] >> 24 in (0xE0, 0xF0):
                    continue
                unit_verdict, unit_cls, unit_detail = judge(unit, symbols, built_in, legacy)
                tally[unit_verdict if unit_verdict != "cannot" else unit_cls] += 1
                patches[unit_verdict] += 1
                if unit_verdict == "cannot":
                    classes[unit_cls] += 1
                    if unit_cls in ("code-write", "code-inject"):
                        addr = BASE | (unit[0][0] & 0x01FFFFFF)
                        entry = needed.setdefault(site_name(symbols, addr), {"lists": set(), "names": set(), "kinds": Counter()})
                        entry["lists"].add(rel)
                        entry["names"].add(name)
                        entry["kinds"]["%02X" % (unit[0][0] >> 24)] += 1
            if verdict == "runs":
                line = "RUNS        %s" % detail
            elif verdict == "built-in":
                line = "BUILT IN    switch \"%s\"" % detail
            else:
                line = "CANNOT RUN  [%s] %s" % (cls, detail)
            say("  %-52s %5d lines  %s" % (name[:52], len(lines), line))
            if verdict == "cannot" and len(units) > 1:
                say("  %-52s        patches: %s" % ("", ", ".join("%d %s" % (n, k) for k, n in tally.most_common())))
        say("")
    say("TOTALS")
    total = sum(codes.values())
    say("  codes:   %d   runs %d   built in %d   cannot run %d" % (total, codes["runs"], codes["built-in"], codes["cannot"]))
    total = sum(patches.values())
    say("  patches: %d   runs %d   built in %d   cannot run %d" % (total, patches["runs"], patches["built-in"], patches["cannot"]))
    say("  patches that cannot run, by reason:")
    for cls, text in CLASSES.items():
        if classes[cls]:
            say("    %5d  %-12s %s" % (classes[cls], cls, text))
    say("")
    say("FUNCTIONS PATCHED BY CODES THAT CANNOT RUN (each needs a native rewrite), most lists first")
    ranked = sorted(needed.items(), key=lambda item: (-len(item[1]["lists"]), -sum(item[1]["kinds"].values()), item[0]))
    for function, entry in ranked:
        say("  %2d lists  %-40s %-14s %s" % (len(entry["lists"]), function[:40],
                                           " ".join("%s x%d" % (k, n) for k, n in sorted(entry["kinds"].items())),
                                           "; ".join(sorted(entry["names"]))[:110]))
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out")
    parser.add_argument("--legacy", action="store_true")
    args = parser.parse_args()
    if not gecko_targets.SYMBOLS.exists():
        sys.exit("gecko coverage: the decomp is not checked out")
    text = report(args.legacy)
    sys.stdout.write(text)
    if args.out:
        path = Path(args.out)
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as out:
            out.write(text)


if __name__ == "__main__":
    main()
