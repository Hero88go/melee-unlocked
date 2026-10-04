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
    whose length cannot be read from the source is refused;
  * a string (char name[] = "...") is accepted when its length in the game's character set, with
    the terminator, is exactly the console symbol's size;
  * a global of a struct or enum type is accepted when a header declares it and the type is defined
    in a header (StructTypes below). A struct made only of the simple types is one row, as an array
    is. Any other struct is walked member by member with the console's layout rules (4-byte
    pointers, 4-byte enums, natural alignment): each member that is itself provable becomes its own
    row, named by its C path (object.member), and a pointer, bitfield, enum or union member becomes
    a refused row. The walk is accepted only when it adds up to the console symbol's size and
    every member named after its offset (x1C, unk_0x20) sits at that offset; the native compiler
    then checks the size of every accepted member. The native layout of such a struct differs
    (8-byte pointers), which is why a write may never cross from one member row to the next.

The same run reads the code set the Source Port carries as C (BUILT_IN below) and writes each of its
patches, as (first word, line count, hash of the exact words), into the host's table, so a code the
player adds that is made only of those patches is shown as built in.

Outputs (both generated, do not edit):
  sourceport/game/shim/mu_gecko_targets.inc   the game library's table: console range, native
                                              address and element widths, for shim/mu_gecko.c
  port/runtime/host/user_gecko_targets.h      the host's table of every console symbol and why a
                                              write there can or cannot run, for user_gecko.cpp

Usage: python tools/gecko_targets.py [--check] [--list]
  --check  write nothing; exit 1 when the files on disk differ from what would be generated
  --list   also print every supported symbol

If the game library stops compiling in shim/mu_gecko.c after a regeneration (a header that does not
stand on its own there, or a size assertion), add the object's name to DENY below and regenerate.
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
# Where struct and enum types are read from: only the game's own headers, which shim/mu_gecko.c can
# include by the same path.
TYPE_HEADER_DIR = DECOMP / "src"
# The code list whose codes the Source Port carries as C, and which of them (ini name -> the label
# of the switch in the PC settings). See built_in_units().
BUILT_IN_INI = REPO / "port" / "slippi_sys" / "GameSettings" / "GALE01r2.ini"
BUILT_IN = [
    ("Required: General Codes", "General Codes"),
    ("Optional: Lagless FoD", "Lagless FoD"),
    ("Optional: Widescreen 16:9", "Widescreen 16:9"),
    ("Optional: Disable Screen Shake", "Disable Screen Shake"),
    ("Optional: Flash Red on Failed L-Cancel", "Flash Red on Failed L-Cancel"),
]
# The port's own codes (port/recomp/gecko.py, PORT_CODES), by their flag.
BUILT_IN_PORT_FLAGS = {"pal_stock_icons": "PAL Stock Icons", "no_screen_shake": "Disable Screen Shake"}

# The headers shim/mu_gecko.c includes to declare struct and enum typed objects (the header that
# declares the object; the one defining its type comes with it). Kept short on purpose: every one
# is compiled into the shim, and each was picked for the state it declares (the pads, the debug
# level, the versus mode's match, character select and results data, the stage, the camera, the
# item spawner). An object declared anywhere else is refused, with the header named, until its
# header is added here.
TYPED_HEADERS = (
    "sysdolphin/baselib/controller.h",
    "melee/db/db.h",
    "melee/gm/gmvsmelee.h",
    "melee/gm/gmmain_lib.h",
    "melee/gr/ground.h",
    "melee/cm/camera.h",
    "melee/mn/mnmain.h",
    "melee/it/it_3F14.h",
    "melee/it/item.h",
)

# Objects never offered, whatever the proof says. Put a name here (the object's, not a member's)
# when its row does not compile in shim/mu_gecko.c, then regenerate.
DENY = set()
# One object may become at most this many rows; a bigger one is refused whole.
MAX_ROWS = 2048

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
    """One row: a console symbol, or one member of a struct-typed one (name is then its C path)."""
    __slots__ = ("name", "section", "addr", "size", "kind", "layout", "ctype", "dims", "note",
                 "decl", "includes", "rows", "lvalue", "parts")

    def __init__(self, name, section, addr, size):
        self.name, self.section, self.addr, self.size = name, section, addr, size
        self.kind, self.layout, self.ctype, self.dims, self.note = ABSENT, "", "", [], ""
        self.decl, self.includes, self.rows = "", [], None
        # The C expression for the native object (the name, or the first member of a run), and for
        # a run of neighbouring members offered as one row, the member rows it is made of.
        self.lvalue, self.parts = name, None


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


def strip_source(text, guard=False):
    """C source with comments and literals blanked, preprocessor lines removed and every line that
    sits inside a conditional marked. With `guard`, a header's include guard (#ifndef X followed at
    once by #define X, before any code) is not counted as a conditional."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    text = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)
    text = re.sub(r"'(?:\\.|[^'\\\n])*'", "0", text)
    lines, depth, continued = [], 0, False
    base, pending, directives, code = 0, None, 0, False   # the guard's depth; its name until confirmed
    for line in text.split("\n"):
        stripped = line.lstrip()
        if continued or stripped.startswith("#"):
            if not continued:
                word = stripped[1:].lstrip()
                directives += 1
                if pending is not None:
                    if re.match(r"define\s+%s\b" % re.escape(pending), word):
                        base = 1
                    pending = None
                if word.startswith("if"):
                    depth += 1
                    opened = re.match(r"ifndef\s+(\w+)", word)
                    if guard and directives == 1 and opened and not code:
                        pending = opened.group(1)
                elif word.startswith("endif"):
                    depth = max(0, depth - 1)
            continued = line.rstrip().endswith("\\")
            continue
        code = code or bool(stripped)
        lines.append((CONDITIONAL if depth > base else "") + line)
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


TYPED = re.compile(r"^(?P<type>(?:struct |enum )?[A-Za-z_]\w*) (?P<name>[A-Za-z_]\w*)"
                   r"(?P<dims>(?: ?\[[^\]]*\])*)(?: ?=.*)?$", re.S)
STRING = re.compile(r'^char (\w+)\[\] ?= ?"((?:\\.|[^"\\\n])*)";', re.M)


def string_size(literal):
    """sizeof a char array initialised with the C string `literal` (the text between the quotes),
    in the character set the game is compiled to, or None when an escape is not understood."""
    simple = {"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, '"': 34, "'": 39, "a": 7, "b": 8, "f": 12, "v": 11}
    size, i = 1, 0
    while i < len(literal):
        ch = literal[i]
        if ch != "\\":
            try:
                size += len(ch.encode("cp932"))
            except UnicodeEncodeError:
                return None
            i += 1
            continue
        nxt = literal[i + 1:i + 2]
        if nxt == "x":
            m = re.match(r"[0-9A-Fa-f]+", literal[i + 2:])
            if not m:
                return None
            i += 2 + len(m.group(0))
        elif nxt.isdigit():
            i += 1 + len(re.match(r"[0-7]{1,3}|\d", literal[i + 1:]).group(0))
        elif nxt in simple:
            i += 2
        else:
            return None
        size += 1
    return size


def scan_sources(dirs=SOURCE_DIRS):
    """name -> list of (kind, type, dims, file, initializer) for every file-scope object the
    sources define. A string's initializer is its size in bytes."""
    found = {}
    for root in dirs:
        for path in sorted(root.rglob("*.c")):
            rel = path.relative_to(DECOMP).as_posix()
            raw = path.read_text(encoding="utf-8", errors="replace")
            strings = {m.group(1): string_size(m.group(2)) for m in STRING.finditer(raw)}
            for stmt in file_scope_statements(raw):
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
                    init = m.group("init")
                    if m.group("type") == "char" and dims == [""] and init == '""':
                        init = strings.get(m.group("name"))   # the literal was blanked: its size
                    found.setdefault(m.group("name"), []).append(
                        (kind, m.group("type"), dims, rel, init))
                    continue
                head = stmt.split("=")[0].strip()
                if "(" in head and "(*" not in head:
                    continue   # a function prototype
                names = ANY_NAME.findall(head + " =")
                if not names:
                    continue
                ctype, dims = "", []
                if stmt.startswith("static ") or " static " in head:
                    kind = PRIVATE
                elif "*" in head:
                    kind = POINTERS
                else:
                    kind = LAYOUT
                    typed = TYPED.match(stmt)
                    if typed and not conditional and typed.group("name") == names[-1]:
                        # A struct or enum typed object: classify() asks StructTypes about it.
                        ctype, dims = typed.group("type"), re.findall(r"\[([^\]]*)\]", typed.group("dims"))
                found.setdefault(names[-1], []).append((kind, ctype, dims, rel, None))
    return found


def classify(symbols, definitions, types=None):
    """Fills in each symbol's kind (and layout for the supported ones)."""
    for s in symbols:
        s.decl = ""
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
        if s.name in DENY:
            s.kind, s.note = LAYOUT, "denied in tools/gecko_targets.py (%s)" % rel
            continue
        if kind == LAYOUT and ctype and types is not None and s.section not in (".sdata2", ".rodata"):
            classify_typed(s, types, ctype, dims, rel)
            continue
        if kind != OK:
            continue
        if s.section in (".sdata2", ".rodata"):
            s.kind = CONSTANT   # the console keeps it read-only; the native compiler may fold it
            continue
        if isinstance(init, int):
            # char name[] = "...": the terminator included, it must be exactly the console's bytes.
            if init != s.size:
                s.kind, s.note = LAYOUT, "string is %d bytes, the console has 0x%X (%s)" % (init, s.size, rel)
                continue
            s.layout, s.ctype, s.dims = "1", ctype, [str(init)]
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


def classify_typed(s, types, ctype, dims, rel):
    """A global of a struct or enum type (see the notes at the top). Leaves the symbol supported
    with its rows in s.rows, or refused with the reason in s.note; check_headers() and build() then
    require a header that declares it."""
    s.kind = LAYOUT
    node = types.node(ctype)
    if node is None:
        s.note = "type %s is not worked out (%s)" % (ctype, rel)
        return
    if types.is_enum(ctype):
        # An enum of the int's size on the console: the native compiler's is an int too, and the
        # shim asserts it. As a member of a struct an enum stays refused (see Node).
        node = Node(node.size, node.align, "4", OK, header=node.header)
    count = types.count(dims)
    if count is None:
        # A named bound (a macro or an enum constant): the console's own size says how many.
        if len(dims) != 1 or node.size == 0 or s.size % node.size:
            s.note = "array length not proven: %s (%s)" % ("".join("[%s]" % d for d in dims), rel)
            return
        count = s.size // node.size
        shape = [str(count)]
    else:
        shape = [d.strip() for d in dims]
    if node.size * count != s.size:
        s.note = "SIZE MISMATCH: %s is %d bytes by its header, the console has 0x%X (%s)" % (
            ctype, node.size * count, s.size, rel)
        return
    try:
        rows = types.rows(node, shape, s.addr, s.name, s.section)
    except ValueError as error:
        s.note = "member offsets not proven: %s (%s)" % (error, rel)
        return
    if len(rows) > MAX_ROWS or not any(r.kind == OK for r in rows):
        s.kind = POINTERS if any(r.kind == POINTERS for r in rows) and len(rows) <= MAX_ROWS else LAYOUT
        s.note = "no member can be offered (%s)" % rel
        return
    decl = "%s %s%s" % (ctype, s.name, "".join("[%s]" % d for d in shape))
    for r in rows:
        r.decl, r.ctype, r.note = decl, ctype, rel
    s.kind, s.ctype, s.rows, s.includes = OK, ctype, rows, [node.header] if node.header else []
    if not node.header:
        s.kind, s.rows, s.note = LAYOUT, None, "type %s is not defined in a header (%s)" % (ctype, rel)


def check_headers(symbols, roots=None):
    """Drops a supported symbol that any header declares as something other than
    `extern <the same type> name`: the decomp sometimes defines as bytes in one file what another
    file uses as a pointer, and the shim's own declaration would not compile against it either."""
    ok = {s.name: s for s in symbols if s.kind == OK}
    declared = {}   # name -> the game headers declaring it
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
                    s.kind, s.rows, s.includes, s.note = LAYOUT, None, [], "declared differently in %s" % path.name
                elif root == TYPE_HEADER_DIR:
                    declared.setdefault(name, []).append(path.relative_to(root).as_posix())
    # A struct or enum typed object is declared in the shim through a header: one must exist, and
    # be one the shim is set to include.
    for s in ok.values():
        if s.rows is None or s.kind != OK:
            continue
        headers = sorted(set(declared.get(s.name, [])))
        usable = [h for h in headers if h in TYPED_HEADERS]
        if not headers:
            s.kind, s.rows, s.includes, s.note = LAYOUT, None, [], "no header declares it (%s)" % s.note
        elif not usable or any(h.endswith(".static.h") for h in s.includes):
            s.kind, s.rows, s.includes = LAYOUT, None, []
            s.note = "declared in %s, which is not in TYPED_HEADERS" % headers[0]
        else:
            s.includes = usable[:1] + s.includes


# ---- struct and enum types, read from the game's headers ----

POINTER_SIZE = 4   # on the console; 8 natively, which is why a struct holding one is walked
ENUM_SIZE = 4      # the console compiler's enums are ints; so are the native compiler's
# Types whose size differs natively, or is not this tool's to assume: a refused 4-byte member.
OPAQUE_WORDS = ("long", "unsigned long", "signed long", "size_t", "uintptr_t", "intptr_t", "ptrdiff_t")
AGGREGATE_HEAD = re.compile(r"(?:\b(typedef) )?\b(struct|union|enum)\b ?([A-Za-z_]\w*)? ?\{")
IDENT = re.compile(r"^[A-Za-z_]\w*$")
OFFSET_NAME = re.compile(r"^(?:x([0-9A-F]+)|unk_?0x([0-9A-Fa-f]+))$")


class Node:
    """A type as the console lays it out. `layout` is the element widths when the whole type is
    made of the simple types (then its bytes are the same natively, apart from byte order), else
    empty; `members` is [(offset, name, node, dims)] for a struct that can be walked, else None;
    `kind` is why a member of this type is refused when it has no layout."""
    __slots__ = ("size", "align", "layout", "kind", "members", "header")

    def __init__(self, size, align, layout="", kind=LAYOUT, members=None, header=""):
        self.size, self.align, self.layout, self.kind = size, align, layout, kind
        self.members, self.header = members, header


def simple_node(layout):
    widths = [int(c) for c in layout]
    return Node(sum(widths), max(widths), layout, OK)


def match_brace(text, at):
    """Index of the brace closing the one at `at`, or -1."""
    depth = 0
    for i in range(at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return -1


def split_top(text, sep):
    """`text` cut at each `sep` that is not inside braces, brackets or parentheses."""
    out, depth, cur = [], 0, []
    for ch in text:
        if ch in "{[(":
            depth += 1
        elif ch in "}])":
            depth -= 1
        if ch == sep and depth == 0:
            out.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    out.append("".join(cur))
    return [x.strip() for x in out if x.strip()]


class StructTypes:
    """Every struct, union, enum and typedef the game's headers define, and the console layout of
    the ones that can be worked out."""

    def __init__(self, root=None, texts=None):
        self.defs = {}       # "struct X" or a typedef name -> (keyword, body, disc, header)
        self.typedefs = {}   # name -> ("enum", header) | ("ptr",) | ("alias", type text)
        self.enums = {}      # "enum X" -> header
        self.bad = set()     # defined twice, or under a conditional
        self.constants = {}  # #define NAME number
        self.cache = {}
        if texts is None:
            texts = {}
            for path in sorted((root or TYPE_HEADER_DIR).rglob("*.h")):
                texts[path.relative_to(root or TYPE_HEADER_DIR).as_posix()] = path.read_text(
                    encoding="utf-8", errors="replace")
        for rel in sorted(texts):
            self.read(rel, texts[rel])

    def note(self, table, key, value):
        """Records a definition; the same name defined two different ways is not used at all."""
        same = 1 if value[0] == "enum" else 3   # an enum typedef: the header may differ
        if key in table and table[key][:same] != value[:same]:
            self.bad.add(key)
        table.setdefault(key, value)

    def read(self, rel, raw):
        for m in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+(0[xX][0-9A-Fa-f]+|\d+)[uUlL]*[ \t]*(?://.*|/\*.*\*/[ \t]*)?$",
                             raw, re.M):
            value = int(m.group(2), 0)
            if self.constants.setdefault(m.group(1), value) != value:
                self.constants[m.group(1)] = None
        text = " ".join(strip_source(raw, guard=True).replace(CONDITIONAL, " " + CONDITIONAL + " ").split())
        rest, pos = [], 0
        while True:
            m = AGGREGATE_HEAD.search(text, pos)
            if not m:
                break
            close = match_brace(text, m.end() - 1)
            semi = text.find(";", close) if close >= 0 else -1
            if semi < 0:
                break
            rest.append(text[pos:m.start()])
            typedef, keyword, tag = m.group(1), m.group(2), m.group(3)
            body, tail = text[m.end():close], text[close + 1:semi].strip()
            conditional = CONDITIONAL in text[m.start():semi]
            disc = "DISC_STRUCT" in tail.split()
            names = [n.strip() for n in " ".join(w for w in tail.split() if w != "DISC_STRUCT").split(",")]
            names = [n for n in names if n] if typedef else []
            keys = (["%s %s" % (keyword, tag)] if tag else []) + [n for n in names if IDENT.match(n)]
            for key in keys:
                if conditional:
                    self.bad.add(key)
                if keyword == "enum":
                    if key.startswith("enum "):
                        self.enums.setdefault(key, rel)
                    else:
                        self.note(self.typedefs, key, ("enum", rel))
                else:
                    self.note(self.defs, key, (keyword, body, disc, rel))
            for n in names:
                if n.startswith("*") and IDENT.match(n.lstrip("* ")):
                    self.note(self.typedefs, n.lstrip("* "), ("ptr",))
                elif not IDENT.match(n):
                    self.bad.update(keys)   # an attribute or something else this tool cannot read
            pos = semi + 1
        rest.append(text[pos:])
        for stmt in "".join(rest).split(";"):
            stmt = stmt.strip()
            conditional = CONDITIONAL in stmt
            stmt = " ".join(stmt.replace(CONDITIONAL, " ").split())
            if not stmt.startswith("typedef "):
                continue
            pointer = re.search(r"\(\s*\*+\s*(\w+)\s*\)", stmt)
            if pointer:
                key, value = pointer.group(1), ("ptr",)
            elif "(" in stmt:
                continue   # a function type: only ever used through a pointer
            else:
                m = re.match(r"^typedef (.*?)(\w+)((?: ?\[[^\]]*\])*)$", stmt)
                if not m or m.group(3):
                    continue
                key, target = m.group(2), m.group(1).strip()
                value = ("ptr",) if "*" in target else ("alias", target)
            if conditional:
                self.bad.add(key)
            self.note(self.typedefs, key, value)

    # -- layout --

    def node(self, text, seen=()):
        """The console layout of the type written `text`, or None when it cannot be worked out."""
        text = " ".join(w for w in text.split() if w not in ("const", "volatile"))
        if text in TYPES:
            return simple_node(TYPES[text])
        if text in OPAQUE_WORDS:
            return Node(4, 4)
        if text in self.bad or text in seen:
            return None
        if text.startswith("enum "):
            return Node(ENUM_SIZE, ENUM_SIZE, header=self.enums.get(text, ""))
        if text in self.defs:
            if text not in self.cache:
                keyword, body, disc, rel = self.defs[text]
                self.cache[text] = self.aggregate(keyword, body, disc, rel, seen + (text,))
            return self.cache[text]
        entry = self.typedefs.get(text)
        if not entry:
            return None
        if entry[0] == "enum":
            return Node(ENUM_SIZE, ENUM_SIZE, header=entry[1])
        if entry[0] == "ptr":
            return Node(POINTER_SIZE, POINTER_SIZE, kind=POINTERS)
        return self.node(entry[1], seen + (text,))

    def is_enum(self, text):
        """Whether the type written `text` is an enum, through any typedef."""
        for _ in range(8):
            if text.startswith("enum "):
                return text in self.enums and text not in self.bad
            entry = self.typedefs.get(text)
            if text in self.bad or not entry or entry[0] not in ("enum", "alias"):
                return False
            if entry[0] == "enum":
                return True
            text = entry[1]
        return False

    def count(self, dims):
        """The element count of `dims` (the texts between the brackets), or None."""
        total = 1
        for d in dims:
            d = d.strip()
            if NUMBER.match(d):
                total *= int(d.rstrip("uUlL"), 0)
            elif self.constants.get(d) is not None:
                total *= self.constants[d]
            elif re.match(r"^[0-9A-Fa-fxX+\-*/() ]+$", d) and re.search(r"\d", d):
                try:   # plain arithmetic on numbers, as in pad[0x40 - 0x1C]
                    total *= int(eval(d.replace("/", "//"), {"__builtins__": {}}))
                except Exception:
                    return None
            else:
                return None
        return total

    def members(self, body, seen):
        """[(name or None, node, dims, bitfield width or None)] for a struct body, or None."""
        out = []
        for text in split_top(body, ";"):
            if CONDITIONAL in text:
                return None
            if "{" in text:
                m = re.match(r"^(struct|union|enum) ?(\w+)? ?\{(.*)\} ?(.*)$", text)
                if not m:
                    return None
                words = m.group(4).split()
                disc = "DISC_STRUCT" in words
                if m.group(1) == "enum":
                    node = Node(ENUM_SIZE, ENUM_SIZE)
                else:
                    node = self.aggregate(m.group(1), m.group(3), disc, "", seen)
                if node is None:
                    return None
                declarators = split_top(" ".join(w for w in words if w != "DISC_STRUCT"), ",") or [None]
                for d in declarators:
                    if d is None:
                        out.append((None, node, [], None))
                        continue
                    dm = re.match(r"^(\**) ?(\w+)((?: ?\[[^\]]*\])*)$", d)
                    if not dm:
                        return None
                    out.append((dm.group(2), Node(POINTER_SIZE, POINTER_SIZE, kind=POINTERS) if dm.group(1) else node,
                                re.findall(r"\[([^\]]*)\]", dm.group(3)), None))
                continue
            if "(" in re.sub(r"\[[^\]]*\]", "", text):   # outside any array bound
                m = re.search(r"\(\s*\*+\s*(?:const\s+)?(\w+)\s*((?:\[[^\]]*\])*)\s*\)\s*\(", text)
                if not m:
                    m = re.match(r"^DISC_PTR ?\(.*\) ?\** ?(\w+)((?: ?\[[^\]]*\])*)$", text)
                if not m:
                    return None
                out.append((m.group(1), Node(POINTER_SIZE, POINTER_SIZE, kind=POINTERS),
                            re.findall(r"\[([^\]]*)\]", m.group(2)), None))
                continue
            parts = split_top(text, ",")
            first = re.match(r"^(.*[^\w])(\w+) ?((?: ?\[[^\]]*\])*) ?(?:: ?(\w+))?$", parts[0])
            if ":" in parts[0]:   # a bitfield: the type and name are left of the colon
                first = re.match(r"^(.*[^\w])(\w+) ?() ?: ?(\w+)$", parts[0])
            if not first:
                return None
            pointer = Node(POINTER_SIZE, POINTER_SIZE, kind=POINTERS)
            decls = [("*" in first.group(1), first.group(2), first.group(3), first.group(4))]
            for extra in parts[1:]:
                m = re.match(r"^(\**) ?(\w+) ?((?: ?\[[^\]]*\])*) ?(?:: ?(\w+))?$", extra)
                if not m:
                    return None
                decls.append((bool(m.group(1)), m.group(2), m.group(3), m.group(4)))
            node = None
            if not all(d[0] for d in decls):   # a pointer's size does not depend on what it points at
                node = self.node(first.group(1).replace("*", " ").strip(), seen)
                if node is None:
                    return None
            for is_pointer, name, dims, bits in decls:
                if bits is not None:
                    if is_pointer or dims or not NUMBER.match(bits) or not node.layout or len(node.layout) != 1:
                        return None
                    out.append((name, node, [], int(bits, 0)))
                else:
                    out.append((name, pointer if is_pointer else node, re.findall(r"\[([^\]]*)\]", dims), None))
        return out

    def aggregate(self, keyword, body, disc, rel, seen):
        members = self.members(body, seen)
        if members is None:
            return None
        placed, offset, align, provable = [], 0, 1, not disc and keyword == "struct"
        run = None   # the open run of bitfields: [first byte, next free bit, name]

        def close_run():
            # A run of bitfields is one refused member, from its first byte to its last.
            end = -(-run[1] // 8)
            placed.append((run[0], run[2], Node(end - run[0], 1), []))
            return end

        for name, node, dims, bits in members:
            if bits is not None:
                # Bitfields are packed bit by bit; one never straddles a unit of its own type. That
                # is the native compiler's rule (-mno-ms-bitfields) and, for every struct whose
                # size the caller can check against the console's, the console compiler's too.
                if keyword != "struct":
                    return None
                if run is None:
                    run = [offset, offset * 8, name]
                unit = node.size * 8
                if run[1] % unit + bits > unit:
                    run[1] = -(-run[1] // unit) * unit
                run[1] += bits
                align, provable = max(align, node.align), False
                continue
            if run is not None:
                offset, run = close_run(), None
            count = self.count(dims)
            if count is None:
                return None
            align = max(align, node.align)
            start = -(-offset // node.align) * node.align if keyword == "struct" else 0
            placed.append((start, name, node, dims))
            offset = start + node.size * count if keyword == "struct" else max(offset, node.size * count)
            provable = provable and bool(node.layout)
        if run is not None:
            offset = close_run()
        size = -(-offset // align) * align
        if keyword == "union" or disc:
            # A union's members share their bytes, and disc data keeps the console's byte order
            # natively: neither is a plain list of host-order numbers. The size is all that is used.
            return Node(size, align, header=rel)
        layout = ""
        if provable:
            at = 0
            for start, name, node, dims in placed:
                layout += "1" * (start - at) + node.layout * self.count(dims)
                at = start + node.size * self.count(dims)
            layout += "1" * (size - at)
        return Node(size, align, layout, OK if layout else LAYOUT, placed, rel)

    # -- rows --

    def rows(self, node, dims, addr, path, section, bases=()):
        """The rows of an object of type `node` at console `addr`: one for a provable or refused
        type, one per member for a struct that has to be walked. Raises ValueError when a member
        named after its offset is somewhere else."""
        count = self.count(dims)
        if node.layout or node.members is None:
            row = Symbol(path, section, addr, node.size * count)
            row.kind, row.layout = (OK, node.layout) if node.layout else (node.kind, "")
            return [row]
        out = []
        shape = [self.count([d]) for d in dims]
        for index in range(count):
            where, left = path, index
            subscripts = []
            for n in reversed(shape):
                subscripts.append(left % n)
                left //= n
            where += "".join("[%d]" % i for i in reversed(subscripts))
            out += self.walk(node, addr + index * node.size, where, section, bases)
        return out

    def walk(self, node, addr, path, section, bases):
        out, bases, run = [], bases + (addr,), []

        def flush():
            # Neighbouring provable members are one row, so a write may cover several of them
            # (four byte fields set by one 32-bit write). They are neighbours natively too: what
            # moves a member natively is a pointer before it, by a multiple of 4 bytes, which
            # changes no padding between members aligned to 4 or less. The shim asserts the run's
            # native extent all the same.
            if len(run) > 1:
                row = Symbol("%s to %s" % (run[0].name, run[-1].name.rsplit(".", 1)[-1]), section, run[0].addr,
                             run[-1].addr + run[-1].size - run[0].addr)
                row.kind, row.lvalue, row.parts, at = OK, run[0].name, list(run), run[0].addr
                for part in run:
                    period = sum(int(c) for c in part.layout)
                    row.layout += "1" * (part.addr - at) + part.layout * (part.size // period)
                    at = part.addr + part.size
                out.append(row)
            else:
                out.extend(run)
            del run[:]

        for offset, name, sub, dims in node.members:
            named = OFFSET_NAME.match(name or "")
            if named and all(addr + offset - b != int(named.group(1) or named.group(2), 16) for b in bases):
                raise ValueError("%s.%s is at offset 0x%X" % (path, name, offset))
            if name is None:
                # An anonymous struct or union: its members are the parent's, so there is no C path
                # for the whole of it.
                flush()
                if sub.members is not None and self.count(dims) == 1:
                    out += self.walk(sub, addr + offset, path, section, bases)
                else:
                    row = Symbol(path + ".(unnamed)", section, addr + offset, sub.size * self.count(dims))
                    row.kind = sub.kind if sub.kind != OK else LAYOUT
                    out.append(row)
                continue
            rows = self.rows(sub, dims, addr + offset, "%s.%s" % (path, name), section, bases)
            joins = len(rows) == 1 and rows[0].kind == OK and rows[0].size and "8" not in rows[0].layout
            if not joins or (run and not 0 <= rows[0].addr - (run[-1].addr + run[-1].size) < 4):
                flush()
            if joins:
                run.append(rows[0])
            else:
                out += rows
        flush()
        return out


def build():
    """Every row of the console image, sorted by address: one per symbol, or one per member for a
    struct-typed object that had to be walked."""
    symbols = read_symbols()
    classify(symbols, scan_sources(), StructTypes())
    check_headers(symbols)
    # Overlapping console ranges cannot be told apart by address: keep neither.
    for a, b in zip(symbols, symbols[1:]):
        if a.addr + a.size > b.addr:
            for s in (a, b):
                if s.kind == OK:
                    s.kind, s.rows, s.includes, s.note = LAYOUT, None, [], "overlaps its neighbour in symbols.txt"
    out = []
    for s in symbols:
        if s.kind == OK and s.rows is not None:
            for r in s.rows:
                r.includes = s.includes
            out += s.rows
            continue
        if s.kind == OK:
            s.decl = "%s %s%s" % (s.ctype, s.name, "".join("[%s]" % d for d in s.dims))
        out.append(s)
    return out


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


def read_bytes(symbols, memory, addr, length):
    """The `length` big-endian bytes at console `addr`, from `memory` as apply_write() keeps it."""
    s = find(symbols, addr)
    return bytes(memory[s.name][native_offset(s.layout, addr - s.addr + k)] for k in range(length))


# ---- the codes the Source Port carries as C ----

CODE_LINE = re.compile(r"^\s*([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})")


def read_code_list(text):
    """[(name, [(word, value)])] for a code list in Dolphin's format: "$Name [authors]" headers and
    "XXXXXXXX YYYYYYYY" lines. A [Gecko] section is honoured when the text has one; a list without
    sections is read whole. The names are returned without the bracketed part."""
    sectioned = re.search(r"^\[Gecko\]", text, re.M) is not None
    out, section = [], ""
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("[") and sectioned:
            section = line
            continue
        if sectioned and section != "[Gecko]":
            continue
        if line.startswith("$"):
            out.append((line[1:].split("[")[0].strip(), []))
            continue
        m = CODE_LINE.match(line)
        if m and out:
            out[-1][1].append((int(m.group(1), 16), int(m.group(2), 16)))
    return out


def code_units(lines):
    """A code cut into its patches, each a list of (word, value) lines: one write, one string or
    serial write with its data lines, or one block of PowerPC (C0, C2) with its instructions."""
    units, i = [], 0
    while i < len(lines):
        word, value = lines[i]
        kind = (word >> 24) & 0xEE
        if kind in (0xC0, 0xC2):
            count = 1 + value
        elif kind == 0x06:
            count = 1 + (value + 7) // 8
        elif kind == 0x08:
            count = 2
        else:
            count = 1
        units.append(lines[i:i + count])
        i += count
    return units


def unit_hash(unit):
    """FNV-1a, 64 bits, over the unit's words as big-endian bytes (user_gecko.cpp computes the
    same)."""
    h = 0xCBF29CE484222325
    for word, value in unit:
        for byte in word.to_bytes(4, "big") + value.to_bytes(4, "big"):
            h = ((h ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def unit_key(unit):
    """What a patch is recognised by: (first word, line count, hash). An injection of a single
    instruction that is not a relative branch (C2 at A: the instruction, then the branch back) does
    exactly what a 32-bit write of that instruction at A does, so it is keyed as that write."""
    word, value = unit[0]
    if word >> 24 in (0xC2, 0xC3) and value == 1 and len(unit) == 2 and unit[1][1] == 0 \
            and unit[1][0] >> 26 not in (16, 18):
        unit = [(0x04000000 | (word & 0x01FFFFFF), unit[1][0])]
    return unit[0][0], len(unit), unit_hash(unit)


def built_in_units(ini=BUILT_IN_INI):
    """(labels, {(first word, line count, hash): label index}) for every patch of the codes the
    Source Port carries as C. Empty when the code list is not in this tree."""
    labels, units = [], {}
    if not ini.exists():
        return labels, units

    def add(label, lines):
        if label not in labels:
            labels.append(label)
        for unit in code_units(lines):
            units.setdefault(unit_key(unit), labels.index(label))

    codes = dict(read_code_list(ini.read_text(encoding="utf-8", errors="replace")))
    for name, label in BUILT_IN:
        if name in codes:
            add(label, codes[name])
    recomp = str(REPO / "port" / "recomp")
    if recomp not in sys.path:
        sys.path.insert(0, recomp)
    import gecko   # port/recomp/gecko.py: the port's own codes
    for name, flag, lines in gecko.PORT_CODES:
        if flag in BUILT_IN_PORT_FLAGS:
            add(BUILT_IN_PORT_FLAGS[flag], list(lines))
    return labels, units


def built_in_label(lines, labels, units):
    """The label of the built-in switch when every patch of a code is one the Source Port carries
    as C, else None. Terminator lines (E0, F0) are not patches."""
    found = None
    for unit in code_units(lines):
        if len(unit) == 1 and unit[0][0] >> 24 in (0xE0, 0xF0):
            continue
        index = units.get(unit_key(unit))
        if index is None:
            return None
        if found is None:
            found = labels[index]
    return found


# ---- output ----

HEADER = "generated by tools/gecko_targets.py from the decomp's symbols.txt and C sources: do not edit"


def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def member_designator(path):
    """`object[i].a.b[2]` -> `a.b[2]`: the member as offsetof() names it."""
    return path.split(".", 1)[1]


def game_table(symbols):
    ok = [s for s in symbols if s.kind == OK]
    lines = ["/* " + HEADER + ".",
             " * MU_GECKO_OBJECT declares the native object with its full bounds, so the compiler checks both",
             " * the declaration against the game's own and its size against the console's. MU_GECKO_TARGET",
             " * is one table row: console address, size, native object, element widths of one period.",
             " * Sorted by console address. */",
             "#ifdef MU_GECKO_OBJECT"]
    # The headers that declare the struct and enum typed objects, and define their types.
    for header in sorted({h for s in ok for h in s.includes}):
        lines.append("#include <%s>" % header)
    for s in ok:
        if s.parts is None:
            lines.append("MU_GECKO_OBJECT(%s, %s, 0x%X)" % (s.decl, s.lvalue, s.size))
            continue
        # A run of neighbouring members: each member's size, then the run's extent in the native
        # struct (first member's offset to the end of the last).
        for part in s.parts:
            lines.append("MU_GECKO_OBJECT(%s, %s, 0x%X)" % (s.decl, part.name, part.size))
        first, last = (member_designator(part.name) for part in (s.parts[0], s.parts[-1]))
        lines.append("_Static_assert(__builtin_offsetof(%s, %s) + sizeof(%s) - __builtin_offsetof(%s, %s) == 0x%X, "
                     "\"console and native extent differ: %s\");"
                     % (s.ctype, last, s.parts[-1].name, s.ctype, first, s.size, s.lvalue))
    lines.append("#endif")
    lines.append("#ifdef MU_GECKO_TARGET")
    for s in ok:
        lines.append("MU_GECKO_TARGET(0x%08Xu, 0x%Xu, %s, \"%s\")" % (s.addr, s.size, s.lvalue, s.layout))
    lines.append("#endif")
    return "\n".join(lines) + "\n"


def host_table(symbols, built_in=None):
    labels, units = built_in if built_in is not None else built_in_units()
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
    lines += ["};",
              "",
              "// Every patch of the codes the Source Port carries as C: the first word of the patch, its",
              "// number of lines, the FNV-1a hash of its exact words and the built-in switch it belongs to.",
              "// Sorted by word, then line count, then hash.",
              "static const char* const kBuiltInLabels[] = {"]
    lines += ["  %s," % c_string(label) for label in labels] or ['  "",']
    lines += ["};",
              "struct BuiltInUnit { uint32_t word, lines; uint64_t hash; uint8_t label; };",
              "static const BuiltInUnit kBuiltInUnits[] = {"]
    lines += ["  {0x%08Xu, %du, 0x%016XULL, %d}," % (word, count, h, label)
              for (word, count, h), label in sorted(units.items())] or ["  {0u, 0u, 0ULL, 0},"]
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
    labels, units = built_in_units()
    print("  built-in code patches recognised: %d, under %d switches" % (len(units), len(labels)))
    mismatches = [s for s in symbols if s.note.startswith("SIZE MISMATCH")]
    for s in mismatches:
        print("  WARNING %s: %s" % (s.name, s.note))
    if list_ok:
        for s in symbols:
            if s.kind == OK:
                print("  %08X %5X %-6s %s  (%s)" % (s.addr, s.size, s.section, s.decl, s.name))


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
