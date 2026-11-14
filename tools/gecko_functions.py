#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Which game functions a Gecko code may make "return at once" on the Source Port.

The Source Port runs no PowerPC, so a Gecko code that writes instructions into the game's code has
no effect there. A few such codes only change what a function returns: a blr at its start ("do
nothing"), li r3,N + blr at its start ("return N"), or li r3,N just before the epilogue of a
function that only computes a value (waxo's No Character Shadows). Each of those has an exact native
equivalent: the native function, entered, returns at once (with N). The host checks the code's
shape against the disc's main.dol at run time (user_gecko.cpp, function_patch); this tool decides
which native functions may be overwritten that way:

  * the console side: config/GALE01/symbols.txt, functions with their address and size;
  * the native side: the game library's own debug information (DWARF in melee_game.dbg, read with
    the toolchain's objdump), for each function of the same name: an external definition, its
    return type (void, an integer, an enum or a pointer; never a float or a struct) and its
    parameters (at most 6, none passed as a struct or a float, no "...");
  * never one the compiler copied into another function (an inlined instance) or specialised (a
    .constprop / .isra / .part clone): overwriting its entry would not reach those copies. Every
    debug file given must agree, so pass the library of each instruction-set build.

Output (generated, do not edit): sourceport/game/shim/mu_gecko_functions.inc, one line per function:
MU_GECKO_FN(console address, native name, return kind) with return kind 0 void, 1 integer of
1/2/4 bytes (bytes in bits 4-7), 2 pointer. A Linux library is checked the same way (--linux
writes the Linux table: GCC there decides its own inlining).

Usage: python tools/gecko_functions.py [--check] [--linux] DEBUGFILE [DEBUGFILE...]
Takes minutes per file (the debug information is large); run it after a decomp update.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SYMBOLS = REPO / "sourceport" / "extern" / "melee" / "config" / "GALE01" / "symbols.txt"
OUT = {False: REPO / "sourceport" / "game" / "shim" / "mu_gecko_functions.inc",
       True: REPO / "sourceport" / "game" / "shim" / "mu_gecko_functions_linux.inc"}
OBJDUMP_WIN = Path("C:/Users/Chandler/toolchains/winlibs-gcc-15.3/mingw64/bin/objdump.exe")

DIE = re.compile(r"^\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: (\d+)(?: \((DW_TAG_\w+)\))?")
ATTR = re.compile(r"^\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)$")
REF = re.compile(r"<0x([0-9a-f]+)>")

KEEP_ATTRS = {"DW_AT_name", "DW_AT_type", "DW_AT_external", "DW_AT_low_pc", "DW_AT_inline",
              "DW_AT_abstract_origin", "DW_AT_specification", "DW_AT_declaration", "DW_AT_byte_size",
              "DW_AT_encoding", "DW_AT_high_pc"}
TYPE_TAGS = {"DW_TAG_base_type", "DW_TAG_typedef", "DW_TAG_pointer_type", "DW_TAG_const_type",
             "DW_TAG_volatile_type", "DW_TAG_enumeration_type", "DW_TAG_structure_type",
             "DW_TAG_union_type", "DW_TAG_array_type", "DW_TAG_subroutine_type", "DW_TAG_restrict_type",
             "DW_TAG_atomic_type"}


def attr_value(name, raw):
    if name == "DW_AT_name":
        # "(indirect string, offset: 0x1a2b): ftLib_800872BC" or the name itself
        return raw.split("): ", 1)[1].strip() if raw.startswith("(") and "): " in raw else raw.strip()
    m = REF.search(raw)
    if name in ("DW_AT_type", "DW_AT_abstract_origin", "DW_AT_specification") and m:
        return int(m.group(1), 16)
    if name in ("DW_AT_byte_size", "DW_AT_encoding", "DW_AT_inline", "DW_AT_external", "DW_AT_declaration", "DW_AT_high_pc"):
        tok = raw.split()[0] if raw.split() else "0"
        try:
            return int(tok, 0)
        except ValueError:
            return 1
    return raw.strip()


def read_dwarf(path, objdump):
    """Types, subprograms (with parameter counts) and the set of inlined abstract origins."""
    types, subs, inlined = {}, {}, set()
    stack = []          # (depth, offset, tag)
    cur = None          # (offset, tag, attrs)
    proc = subprocess.Popen([str(objdump), "--dwarf=info", str(path)], stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True, errors="replace", bufsize=1 << 20)

    def close(die):
        if die is None:
            return
        off, tag, attrs, depth = die
        if tag in TYPE_TAGS:
            types[off] = (tag, attrs)
        elif tag == "DW_TAG_subprogram":
            attrs.setdefault("_params", 0)
            subs[off] = attrs
        elif tag == "DW_TAG_inlined_subroutine" and "DW_AT_abstract_origin" in attrs:
            inlined.add(attrs["DW_AT_abstract_origin"])

    for line in proc.stdout:
        m = DIE.match(line)
        if m:
            close(cur)
            depth, off, abbrev, tag = int(m.group(1)), int(m.group(2), 16), m.group(3), m.group(4)
            cur = None
            if abbrev == "0" or not tag:
                continue
            while stack and stack[-1][0] >= depth:
                stack.pop()
            parent = stack[-1] if stack else None
            if parent and parent[2] == "DW_TAG_subprogram" and parent[0] == depth - 1:
                sub = parent[3]
                if tag == "DW_TAG_formal_parameter":
                    sub["_params"] = sub.get("_params", 0) + 1
                    sub.setdefault("_ptypes", [])
                    cur = (off, tag, {"_owner": sub}, depth)
                elif tag == "DW_TAG_unspecified_parameters":
                    sub["_varargs"] = 1
            attrs = {}
            if tag == "DW_TAG_subprogram":
                stack.append((depth, off, tag, attrs))
            else:
                stack.append((depth, off, tag, None))
            if cur is None:
                cur = (off, tag, attrs, depth)
            continue
        if cur is None:
            continue
        a = ATTR.match(line)
        if not a:
            continue
        name, raw = a.group(1), a.group(2)
        if cur[1] == "DW_TAG_formal_parameter":
            if name == "DW_AT_type" and "_owner" in cur[2]:
                cur[2]["_owner"].setdefault("_ptypes", []).append(attr_value(name, raw))
            continue
        if name in KEEP_ATTRS:
            cur[2][name] = attr_value(name, raw)
    close(cur)
    if proc.wait() != 0:
        sys.exit(f"objdump failed on {path}")
    return types, subs, inlined


def type_class(types, ref, depth=0):
    """('void'|'int'|'ptr'|'float'|'agg'|'?', byte size)."""
    if ref is None:
        return ("void", 0)
    if depth > 20 or ref not in types:
        return ("?", 0)
    tag, attrs = types[ref]
    if tag in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_restrict_type",
               "DW_TAG_atomic_type"):
        return type_class(types, attrs.get("DW_AT_type"), depth + 1)
    if tag == "DW_TAG_pointer_type":
        return ("ptr", 8)
    if tag == "DW_TAG_enumeration_type":
        return ("int", attrs.get("DW_AT_byte_size", 4))
    if tag == "DW_TAG_base_type":
        enc = attrs.get("DW_AT_encoding", 0)
        size = attrs.get("DW_AT_byte_size", 0)
        if enc == 4:          # DW_ATE_float
            return ("float", size)
        if enc == 2 and size == 1:   # DW_ATE_boolean: the caller trusts it to be 0 or 1
            return ("bool", 1)
        if enc in (2, 5, 6, 7, 8, 16) and size in (1, 2, 4, 8):   # boolean, (un)signed, (un)signed char, UTF
            return ("int", size)
        return ("?", size)
    return ("agg", 0)


def eligible(path, objdump):
    types, subs, inlined = read_dwarf(path, objdump)
    # Names that have an inlined instance anywhere, through the abstract DIE or its declaration.
    def root(off, seen=0):
        a = subs.get(off)
        while a is not None and seen < 10:
            nxt = a.get("DW_AT_abstract_origin") or a.get("DW_AT_specification")
            if nxt is None or nxt not in subs:
                break
            off, a, seen = nxt, subs[nxt], seen + 1
        return off
    inlined_roots = {root(o) for o in inlined}
    out, refused = {}, {}
    names_by_root = {}
    for off, a in subs.items():
        r = root(off)
        name = a.get("DW_AT_name") or subs.get(r, {}).get("DW_AT_name")
        if name:
            names_by_root.setdefault(r, set()).add(name)
    inlined_names = set()
    for r in inlined_roots:
        inlined_names |= names_by_root.get(r, set())
    for off, a in subs.items():
        if "DW_AT_low_pc" not in a:
            continue
        r = root(off)
        info = subs.get(r, a)
        name = a.get("DW_AT_name") or info.get("DW_AT_name")
        if not name:
            continue
        external = a.get("DW_AT_external") or info.get("DW_AT_external")
        ret = type_class(types, info.get("DW_AT_type", a.get("DW_AT_type")))
        params = a.get("_params") or info.get("_params", 0)
        ptypes = a.get("_ptypes") or info.get("_ptypes", [])
        varargs = a.get("_varargs") or info.get("_varargs")
        why = None
        if not external:
            why = "static"
        elif name in inlined_names:
            why = "inlined"
        elif varargs:
            why = "varargs"
        elif params > 6:
            why = "params"
        elif any(type_class(types, t)[0] not in ("int", "bool", "ptr") for t in ptypes):
            why = "param type"
        elif ret[0] not in ("void", "int", "bool", "ptr") or (ret[0] == "int" and ret[1] == 8):
            why = "return type"
        elif not isinstance(a.get("DW_AT_high_pc"), int) or a["DW_AT_high_pc"] < 6:
            why = "too short"
        if why:
            refused[name] = why
            continue
        kind = 0 if ret[0] == "void" else 2 if ret[0] == "ptr" else 0x19 if ret[0] == "bool" else 1 | (ret[1] << 4)
        if name in out and out[name] != kind:
            refused[name] = "two definitions"
            continue
        out[name] = kind
    for n in list(out):
        if n in refused:
            del out[n]
    return out, refused


def clone_bases(path):
    """Functions GCC specialised (name.constprop.N, name.isra.N, name.part.N): from the symbol table."""
    nm = OBJDUMP_WIN.with_name("nm.exe") if OBJDUMP_WIN.exists() else "nm"
    try:
        text = subprocess.run([str(nm), str(path)], capture_output=True, text=True, errors="replace").stdout
    except OSError:
        return set()
    bases = set()
    for line in text.splitlines():
        sym = line.split()[-1] if line.split() else ""
        if "." in sym and any(k in sym for k in (".constprop", ".isra", ".part")):
            bases.add(sym.split(".", 1)[0])
    return bases


def console_functions():
    out = {}
    for line in SYMBOLS.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^(\S+) = \.text:0x([0-9A-Fa-f]{8}); // type:function size:0x([0-9A-Fa-f]+)", line)
        if m:
            out[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return out


def eligible_cached(f, objdump):
    """eligible() and the clones, kept beside the debug file until it changes (the read takes minutes)."""
    import json
    cache = Path(str(f) + ".gecko-functions.json")
    mtime = Path(f).stat().st_mtime
    if cache.exists():
        d = json.loads(cache.read_text())
        if d.get("mtime") == mtime and d.get("format") == 2:
            return d["ok"], d["refused"], set(d["clones"])
    ok, refused = eligible(f, objdump)
    clones = clone_bases(f)
    cache.write_text(json.dumps({"format": 2, "mtime": mtime, "ok": ok, "refused": refused, "clones": sorted(clones)}))
    return ok, refused, clones


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("debug_files", nargs="+")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--linux", action="store_true")
    ap.add_argument("--objdump", default=str(OBJDUMP_WIN) if OBJDUMP_WIN.exists() else "objdump")
    args = ap.parse_args()
    console = console_functions()
    keep = None
    reasons = {}
    for f in args.debug_files:
        ok, refused, clones = eligible_cached(f, args.objdump)
        ok = {n: k for n, k in ok.items() if n not in clones}
        for n in clones:
            reasons.setdefault(n, "clone")
        for n, why in refused.items():
            reasons.setdefault(n, why)
        keep = ok if keep is None else {n: k for n, k in keep.items() if ok.get(n) == k}
        print(f"{f}: {len(ok)} eligible, {len(refused)} refused, {len(clones)} cloned")
    rows = sorted((console[n][0], n, k) for n, k in keep.items() if n in console and console[n][1] >= 8)
    text = ["/* Generated by tools/gecko_functions.py from the game library's debug information. Do not edit.",
            " * Game functions whose native entry the host may overwrite with \"return at once\" for a Gecko code",
            " * (user_gecko.cpp, function patches): MU_GECKO_FN(console address, native name, return kind).",
            " * Return kind: 0x00 void, 0x02 pointer, 0x11/0x21/0x41 an integer of 1, 2 or 4 bytes, 0x19 bool. */"]
    text += [f"MU_GECKO_FN(0x{addr:08X}u, {name}, 0x{kind:02X})" for addr, name, kind in rows]
    body = "\n".join(text) + "\n"
    out = OUT[args.linux]
    if args.check:
        current = out.read_text(encoding="utf-8") if out.exists() else ""
        # A function listed now must still be eligible: the reverse (a new eligible one) is fine.
        listed = set(re.findall(r"MU_GECKO_FN\(0x[0-9A-F]+u, (\w+),", current))
        lost = sorted(listed - set(keep))
        if lost:
            print("no longer safe to overwrite: " + ", ".join(f"{n} ({reasons.get(n, '?')})" for n in lost[:40]))
            return 1
        print(f"check: all {len(listed)} listed functions still eligible")
        return 0
    out.write_text(body, encoding="utf-8", newline="\n")
    print(f"wrote {out} ({len(rows)} functions)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
