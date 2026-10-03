#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Which console data a Gecko code may write on the Source Port.

A Gecko write names a console address. The Source Port has no console memory image: a global lives
wherever the native linker put it, and anything holding a pointer has a different layout. So a
write can only be honoured when the console symbol it lands in has a native global of the same name
whose layout is byte for byte the console's, apart from byte order. This tool proves that for the
simple cases and refuses the rest:

  * the console side comes from the decomp's config/GALE01/symbols.txt (address, size, section);
  * the native side comes from the file-scope definition in the decomp's C (the same sources the
    game library compiles), accepted only when it is a scalar or an array of fixed-width integers,
    floats or the few all-float/all-byte structs listed in TYPES, is not static, not const, not
    inside a preprocessor conditional, and defined exactly once;
  * the two must agree on size, and the generated table makes the native compiler check it again
    (the table redeclares each object with its full bounds and asserts its sizeof), so an array
    whose length cannot be read from the source is refused.

Outputs (both generated, do not edit):
  sourceport/game/shim/mu_gecko_targets.inc   the game library's table: console range, native
                                              address and element widths, for shim/mu_gecko.c
  port/runtime/host/user_gecko_targets.h      the host's table of every console symbol and why a
                                              write there can or cannot run, for user_gecko.cpp

Usage: python tools/gecko_targets.py [--check] [--list]
  --check  write nothing; exit 1 when the files on disk differ from what would be generated
  --list   also print every supported symbol
"""

import argparse
import bisect
import re
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DECOMP = REPO / "sourceport" / "extern" / "melee"
SYMBOLS = DECOMP / "config" / "GALE01" / "symbols.txt"
# The game sources the library compiles whole (sourceport/game/CMakeLists.txt, GAME_SOURCES). The
# SDK and C library files it adds are left out: no tunable lives there.
SOURCE_DIRS = [DECOMP / "src" / "melee", DECOMP / "src" / "sysdolphin"]
# Every header a game source or the shim can see.
HEADER_DIRS = [DECOMP / "src", DECOMP / "libs" / "dolphin" / "include", REPO / "sourceport" / "game"]
OUT_GAME = REPO / "sourceport" / "game" / "shim" / "mu_gecko_targets.inc"
OUT_HOST = REPO / "port" / "runtime" / "host" / "user_gecko_targets.h"

# Type name -> element widths of one object, in order. Every one is the same size and alignment on
# the console and on the PC, and holds no pointer. `long` is left out on purpose (the decomp's s32
# may be a long on the console), as is every enum (its width is the compiler's choice).
TYPES = {
    "s8": "1", "u8": "1", "char": "1", "signed char": "1", "unsigned char": "1",
    "s16": "2", "u16": "2", "short": "2", "unsigned short": "2",
    "s32": "4", "u32": "4", "int": "4", "unsigned int": "4", "unsigned": "4",
    "BOOL": "4", "bool": "4", "f32": "4", "float": "4",
    "s64": "8", "u64": "8", "f64": "8", "double": "8",
    "Vec2": "44", "Vec3": "444", "GXColor": "1111",
}

DATA_SECTIONS = (".data", ".sdata", ".bss", ".sbss", ".sdata2", ".rodata")
CODE_SECTIONS = (".init", ".text")

# Why a write can or cannot run. The numbers are the host table's `kind` (user_gecko.cpp).
OK, CODE, POINTERS, PRIVATE, CONSTANT, LITERAL, LAYOUT, ABSENT = range(8)
KIND_NAMES = {
    OK: "supported",
    CODE: "game code (needs the Static Recomp)",
    POINTERS: "holds pointers",
    PRIVATE: "static in its source file (the shim cannot name it)",
    CONSTANT: "const (compiled into the native code or read-only)",
    LITERAL: "compiler-generated constant (no native object)",
    LAYOUT: "layout not proven (struct, enum, conditional, size mismatch or defined twice)",
    ABSENT: "no definition found in the compiled game sources",
}

SYMBOL_LINE = re.compile(r"^(\S+) = (\S+):0x([0-9A-Fa-f]{8}); // (.*)$")
TYPE_ALT = "|".join(sorted((re.escape(t) for t in TYPES), key=len, reverse=True))
DEFINITION = re.compile(
    r"^(?P<static>static )?(?P<c1>const )?(?:volatile )?(?P<type>" + TYPE_ALT + r")(?P<c2> const)? "
    r"(?P<name>[A-Za-z_]\w*)(?P<dims>(?: ?\[[^\]]*\])*)(?: ?= ?(?P<init>.*))?$", re.S)
ANY_NAME = re.compile(r"([A-Za-z_]\w*)(?: ?\[[^\]]*\])* ?(?:=|$)")
NUMBER = re.compile(r"^(0[xX][0-9A-Fa-f]+|\d+)[uUlL]*$")
CONDITIONAL = "\x01"   # marks text that sits inside an #if of any kind


class Symbol:
    __slots__ = ("name", "section", "addr", "size", "kind", "layout", "ctype", "dims", "note")

    def __init__(self, name, section, addr, size):
        self.name, self.section, self.addr, self.size = name, section, addr, size
        self.kind, self.layout, self.ctype, self.dims, self.note = ABSENT, "", "", [], ""


def read_symbols(path=SYMBOLS):
    """Every sized object and function of the console image, sorted by address."""
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        m = SYMBOL_LINE.match(line)
        if not m:
            continue
        name, section, addr, attrs = m.group(1), m.group(2), int(m.group(3), 16), m.group(4)
        size = re.search(r"size:0x([0-9A-Fa-f]+)", attrs)
        if not size or int(size.group(1), 16) == 0:
            continue
        if section not in DATA_SECTIONS and section not in CODE_SECTIONS:
            continue
        out.append(Symbol(name, section, addr, int(size.group(1), 16)))
    out.sort(key=lambda s: s.addr)
    return out


def strip_source(text):
    """C source with comments and literals blanked, preprocessor lines removed and every line that
    sits inside a conditional marked."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    text = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)
    text = re.sub(r"'(?:\\.|[^'\\\n])*'", "0", text)
    lines, depth, continued = [], 0, False
    for line in text.split("\n"):
        stripped = line.lstrip()
        if continued or stripped.startswith("#"):
            if not continued:
                word = stripped[1:].lstrip()
                if word.startswith("if"):
                    depth += 1
                elif word.startswith("endif"):
                    depth = max(0, depth - 1)
            continued = line.rstrip().endswith("\\")
            continue
        lines.append((CONDITIONAL if depth else "") + line)
    return "\n".join(lines)


def file_scope_statements(text):
    """The declarations and definitions at file scope, each as one line, initializer included.
    Function bodies are skipped."""
    out, cur, depth, keep = [], [], 0, False
    for ch in strip_source(text):
        if ch == "{":
            if depth == 0:
                keep = "".join(cur).rstrip().endswith("=")   # an initializer, not a body
            depth += 1
            if keep:
                cur.append(ch)
        elif ch == "}":
            depth = max(0, depth - 1)
            if keep:
                cur.append(ch)
            elif depth == 0:
                if "(" in "".join(cur):
                    cur = []   # a function body just ended
                else:
                    cur.append("{}")   # a struct, union or enum body
        elif depth == 0 or keep:
            if ch == ";" and depth == 0:
                out.append(" ".join("".join(cur).split()))
                cur, keep = [], False
            else:
                cur.append(ch)
    return out


def initializer_count(init, per_element):
    """How many outer elements a braced initializer gives an array whose outer bound is left
    empty, or None when that cannot be read off the text. `per_element` is the number of scalars
    in one outer element."""
    init = init.strip()
    if not (init.startswith("{") and init.endswith("}")):
        return None   # a string literal, or something else entirely
    body = init[1:-1]
    if "[" in body or '"' in body or re.search(r"\.[A-Za-z_]", body):
        return None   # designators, member access or strings: not counted here
    groups, items, depth, token = 0, 0, 0, ""
    for ch in body + ",":
        if ch in "{(":
            if ch == "{" and depth == 0:
                groups += 1
            depth += 1
        elif ch in "})":
            depth -= 1
        elif ch == "," and depth == 0:
            if token.strip():
                items += 1
            token = ""
            continue
        if depth == 0 and ch not in "{}()":
            token += ch
    if depth != 0:
        return None
    if groups:
        return groups if items == 0 else None   # every outer element braced, or none of them
    if items == 0 or items % per_element:
        return None
    return items // per_element


def scan_sources(dirs=SOURCE_DIRS):
    """name -> list of (kind, type, dims, file, initializer) for every file-scope object the
    sources define."""
    found = {}
    for root in dirs:
        for path in sorted(root.rglob("*.c")):
            rel = path.relative_to(DECOMP).as_posix()
            for stmt in file_scope_statements(path.read_text(encoding="utf-8", errors="replace")):
                conditional = CONDITIONAL in stmt
                stmt = " ".join(stmt.replace(CONDITIONAL, " ").split())
                if not stmt or stmt.startswith(("extern ", "typedef ")):
                    continue
                m = DEFINITION.match(stmt)
                if m:
                    dims = re.findall(r"\[([^\]]*)\]", m.group("dims"))
                    if m.group("static"):
                        kind = PRIVATE
                    elif m.group("c1") or m.group("c2"):
                        kind = CONSTANT
                    elif conditional:
                        kind = LAYOUT
                    else:
                        kind = OK
                    found.setdefault(m.group("name"), []).append(
                        (kind, m.group("type"), dims, rel, m.group("init")))
                    continue
                head = stmt.split("=")[0]
                if "(" in head and "(*" not in head:
                    continue   # a function prototype
                names = ANY_NAME.findall(head + " =")
                if not names:
                    continue
                if stmt.startswith("static ") or " static " in head:
                    kind = PRIVATE
                elif "*" in head:
                    kind = POINTERS
                else:
                    kind = LAYOUT
                found.setdefault(names[-1], []).append((kind, "", [], rel, None))
    return found


def classify(symbols, definitions):
    """Fills in each symbol's kind (and layout for the supported ones)."""
    for s in symbols:
        if s.section in CODE_SECTIONS:
            s.kind = CODE
            continue
        if s.name.startswith("@") or s.name.startswith("..."):
            s.kind = LITERAL
            continue
        defs = definitions.get(s.name)
        if not defs:
            s.kind = ABSENT
            continue
        if len(defs) > 1:
            kinds = {d[0] for d in defs}
            s.kind = PRIVATE if kinds == {PRIVATE} else LAYOUT
            s.note = "defined %d times" % len(defs)
            continue
        kind, ctype, dims, rel, init = defs[0]
        s.kind, s.note = kind, rel
        if kind != OK:
            continue
        if s.section in (".sdata2", ".rodata"):
            s.kind = CONSTANT   # the console keeps it read-only; the native compiler may fold it
            continue
        layout = TYPES[ctype]
        unit = sum(int(c) for c in layout)
        dims = [d.strip() for d in dims]
        counts = [int(d.rstrip("uUlL"), 0) if NUMBER.match(d) else None for d in dims]
        # Inner bounds must be plain numbers: the shim redeclares the object with them.
        if any(c is None for c in counts[1:]):
            s.kind, s.note = LAYOUT, "array bound is not a number (%s)" % rel
            continue
        inner = 1
        for c in counts[1:]:
            inner *= c
        if counts and counts[0] is None:
            # The outer bound must be known too, or a write near the end of the console object
            # could land past the end of a shorter native one. Empty: count the initializer. A
            # macro or a string: not proven here.
            if dims[0] == "" and init is not None:
                counts[0] = initializer_count(init, inner * len(layout))
            if counts[0] is None:
                s.kind, s.note = LAYOUT, "array length not proven: [%s] (%s)" % (dims[0], rel)
                continue
        total = unit * inner * (counts[0] if counts else 1)
        if total != s.size:
            s.kind = LAYOUT
            s.note = "SIZE MISMATCH: %s%s is %d bytes, the console has 0x%X (%s)" % (
                ctype, "".join("[%s]" % d for d in dims), total, s.size, rel)
            continue
        s.layout, s.ctype, s.dims = layout, ctype, [str(c) for c in counts]


def check_headers(symbols, roots=None):
    """Drops a supported symbol that any header declares as something other than
    `extern <the same type> name`: the decomp sometimes defines as bytes in one file what another
    file uses as a pointer, and the shim's own declaration would not compile against it either."""
    ok = {s.name: s for s in symbols if s.kind == OK}
    word = re.compile(r"[A-Za-z_]\w*")
    for root in roots or HEADER_DIRS:
        for path in sorted(root.rglob("*.h")):
            text = path.read_text(encoding="utf-8", errors="replace")
            mentions = []
            for stmt in file_scope_statements(text):
                stmt = " ".join(stmt.replace(CONDITIONAL, " ").split())
                mentions += [(w, stmt) for w in set(word.findall(stmt.split("=")[0])) if w in ok]
            mentions += [(m.group(1), "#define") for m in re.finditer(r"^\s*#\s*define\s+(\w+)", text, re.M)
                         if m.group(1) in ok]
            for name, stmt in mentions:
                s = ok[name]
                if re.sub(r" ?\[[^\]]*\]", "", stmt).strip() != "extern %s %s" % (s.ctype, name):
                    s.kind, s.note = LAYOUT, "declared differently in %s" % path.name


def build():
    symbols = read_symbols()
    classify(symbols, scan_sources())
    check_headers(symbols)
    # Overlapping console ranges cannot be told apart by address: keep neither.
    for a, b in zip(symbols, symbols[1:]):
        if a.addr + a.size > b.addr:
            for s in (a, b):
                if s.kind == OK:
                    s.kind, s.note = LAYOUT, "overlaps its neighbour in symbols.txt"
    return symbols


# ---- the model the game library and the host implement (the unit test runs these) ----

def native_offset(layout, offset):
    """Where console byte `offset` of an object sits in the native object: the same element, the
    mirrored byte within it."""
    period = sum(int(c) for c in layout)
    base, pos = offset - offset % period, offset % period
    start = 0
    for c in layout:
        width = int(c)
        if pos < start + width:
            return base + start + (width - 1 - (pos - start))
        start += width
    raise AssertionError("unreachable")


def find(symbols, addr):
    """The symbol holding console address `addr`, or None."""
    i = bisect.bisect_right([s.addr for s in symbols], addr) - 1
    if i >= 0 and addr < symbols[i].addr + symbols[i].size:
        return symbols[i]
    return None


def check_write(symbols, addr, length):
    """Empty when a write of `length` bytes at `addr` can run on the Source Port, else the reason
    (the same wording user_gecko.cpp gives)."""
    s = find(symbols, addr)
    if s is None:
        return "writes memory at %08X, which is not a game variable the Source Port can find" % addr
    if s.kind == CODE:
        return "writes game code at %s: needs the Static Recomp engine" % s.name
    if s.kind == POINTERS:
        return "writes %s, which holds pointers" % s.name
    if s.kind == PRIVATE:
        return "writes %s, which the Source Port keeps private to one source file" % s.name
    if s.kind == CONSTANT:
        return "writes %s, a constant the Source Port compiles into its code" % s.name
    if s.kind == LITERAL:
        return "writes a constant at %08X that the Source Port compiles into its code" % addr
    if s.kind == LAYOUT:
        return "writes %s, whose layout is not proven to match the console's" % s.name
    if s.kind == ABSENT:
        return "writes %s, which has no counterpart the Source Port can name" % s.name
    if addr + length > s.addr + s.size:
        return "writes past the end of %s" % s.name
    return ""


def apply_write(symbols, memory, addr, data):
    """Stores big-endian `data` at console `addr` into `memory` (name -> bytearray, host order)."""
    s = find(symbols, addr)
    for k, byte in enumerate(data):
        memory[s.name][native_offset(s.layout, addr - s.addr + k)] = byte


# ---- output ----

HEADER = "generated by tools/gecko_targets.py from the decomp's symbols.txt and C sources: do not edit"


def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def game_table(symbols):
    ok = [s for s in symbols if s.kind == OK]
    lines = ["/* " + HEADER + ".",
             " * MU_GECKO_OBJECT declares the native object with its full bounds, so the compiler checks both",
             " * the declaration against the game's own and its size against the console's. MU_GECKO_TARGET",
             " * is one table row: console address, size, native object, element widths of one period.",
             " * Sorted by console address. */",
             "#ifdef MU_GECKO_OBJECT"]
    for s in ok:
        decl = "%s %s%s" % (s.ctype, s.name, "".join("[%s]" % d for d in s.dims))
        lines.append("MU_GECKO_OBJECT(%s, %s, 0x%X)" % (decl, s.name, s.size))
    lines.append("#endif")
    lines.append("#ifdef MU_GECKO_TARGET")
    for s in ok:
        lines.append("MU_GECKO_TARGET(0x%08Xu, 0x%Xu, %s, \"%s\")" % (s.addr, s.size, s.name, s.layout))
    lines.append("#endif")
    return "\n".join(lines) + "\n"


def host_table(symbols):
    lines = ["// " + HEADER + ".",
             "// Every sized symbol of the console image, sorted by address, with why a Gecko write there can",
             "// or cannot run on the Source Port (kind: see user_gecko.cpp). A name is kept only where a",
             "// refusal quotes it.",
             "// SPDX-License-Identifier: GPL-2.0-or-later",
             "#pragma once",
             "#include <cstdint>",
             "",
             "namespace user_gecko {",
             "struct ConsoleSymbol { uint32_t addr, size; uint8_t kind; const char* name; };",
             "static const ConsoleSymbol kConsoleSymbols[] = {"]
    run = None   # neighbouring compiler constants share one row: nothing tells them apart
    for s in symbols:
        if s.kind == LITERAL:
            if run and s.addr - (run[0] + run[1]) < 8:
                run[1] = s.addr + s.size - run[0]
            else:
                run = [s.addr, s.size]
                lines.append(run)
            continue
        run = None
        lines.append("  {0x%08Xu, 0x%Xu, %d, %s}," % (s.addr, s.size, s.kind, c_string(s.name)))
    lines = [x if isinstance(x, str) else "  {0x%08Xu, 0x%Xu, %d, \"\"}," % (x[0], x[1], LITERAL) for x in lines]
    lines += ["};", "}  // namespace user_gecko", ""]
    return "\n".join(lines)


def summary(symbols, list_ok):
    data = [s for s in symbols if s.kind != CODE]
    by_kind, bytes_by_kind = Counter(s.kind for s in data), Counter()
    for s in data:
        bytes_by_kind[s.kind] += s.size
    print("gecko targets: %d console data symbols, %d bytes" % (len(data), sum(s.size for s in data)))
    for kind in sorted(by_kind):
        print("  %-78s %6d symbols %9d bytes" % (KIND_NAMES[kind], by_kind[kind], bytes_by_kind[kind]))
    print("  functions (always refused, named in the reason): %d" % sum(s.kind == CODE for s in symbols))
    mismatches = [s for s in symbols if s.note.startswith("SIZE MISMATCH")]
    for s in mismatches:
        print("  WARNING %s: %s" % (s.name, s.note))
    if list_ok:
        for s in symbols:
            if s.kind == OK:
                print("  %08X %5X %-6s %s%s  (%s)" % (s.addr, s.size, s.section, s.ctype,
                                                     "".join("[%s]" % d for d in s.dims), s.name))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    if not SYMBOLS.exists():
        sys.exit("gecko targets: %s not found (is the decomp checked out?)" % SYMBOLS.relative_to(REPO).as_posix())
    symbols = build()
    summary(symbols, args.list)
    stale = False
    for path, text in ((OUT_GAME, game_table(symbols)), (OUT_HOST, host_table(symbols))):
        current = path.read_bytes().decode("utf-8") if path.exists() else None
        if current == text:
            continue
        if args.check:
            print("STALE: %s" % path.relative_to(REPO).as_posix())
            stale = True
        else:
            with open(path, "w", encoding="utf-8", newline="\n") as out:
                out.write(text)
            print("wrote %s" % path.relative_to(REPO).as_posix())
    if stale:
        sys.exit(1)


if __name__ == "__main__":
    main()
