#!/usr/bin/env python3
"""Extract the translator stencils from a standard AMD64 MSVC COFF object.

No third-party parser or player-side compiler is needed. Unknown dependencies,
unwind metadata, overlapping relocations, a hole set other than the declared one
and a missing final tail jump reject the object. A failed extraction never
overwrites an existing generated table.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import hashlib
import json
import struct
from pathlib import Path

# (function suffix, Operation, holes) in table order. port/runtime/ppc/stencil_format.h carries
# the same rows; the extraction test compares the two lists.
OPERATIONS = [
    ("add", "Add", "DSIN"), ("or", "Or", "DSIN"), ("xor", "Xor", "DSIN"),
    ("and_record", "AndRecord", "DSIN"), ("return", "Return", ""),
    ("add_reg", "AddReg", "DSBN"), ("subf", "Subf", "DSBN"), ("mullw", "Mullw", "DSBN"),
    ("mulhw", "Mulhw", "DSBN"), ("mulhwu", "Mulhwu", "DSBN"), ("divw", "Divw", "DSBN"),
    ("divwu", "Divwu", "DSBN"), ("neg", "Neg", "DSN"),
    ("addc", "Addc", "DSBN"), ("adde", "Adde", "DSBN"), ("addze", "Addze", "DSN"),
    ("addme", "Addme", "DSN"), ("subfc", "Subfc", "DSBN"),
    ("subfze", "Subfze", "DSN"), ("subfme", "Subfme", "DSN"),
    ("addic", "Addic", "DSIN"), ("subfic", "Subfic", "DSIN"), ("mulli", "Mulli", "DSIN"),
    ("and_reg", "AndReg", "DSBN"), ("or_reg", "OrReg", "DSBN"), ("xor_reg", "XorReg", "DSBN"),
    ("nand", "Nand", "DSBN"), ("nor", "Nor", "DSBN"), ("eqv", "Eqv", "DSBN"),
    ("andc", "Andc", "DSBN"), ("orc", "Orc", "DSBN"),
    ("extsb", "Extsb", "DSN"), ("extsh", "Extsh", "DSN"), ("cntlzw", "Cntlzw", "DSN"),
    ("slw", "Slw", "DSBN"), ("srw", "Srw", "DSBN"), ("sraw", "Sraw", "DSBN"),
    ("srawi", "Srawi", "DSIN"), ("rlwinm", "Rlwinm", "DSIJN"), ("rlwnm", "Rlwnm", "DSBJN"),
    ("rlwimi", "Rlwimi", "DSIJN"), ("record", "Record", "SN"),
    ("cmpw", "Cmpw", "DSBN"), ("cmplw", "Cmplw", "DSBN"), ("cmpwi", "Cmpwi", "DSIN"),
    ("cmplwi", "Cmplwi", "DSIN"), ("mfcr", "Mfcr", "DN"), ("mtcrf", "Mtcrf", "SIN"),
    ("mcrf", "Mcrf", "DSN"),
    ("mflr", "Mflr", "DN"), ("mtlr", "Mtlr", "SN"), ("mfctr", "Mfctr", "DN"),
    ("mtctr", "Mtctr", "SN"), ("mfxer", "Mfxer", "DN"), ("mtxer", "Mtxer", "SN"),
    ("jump", "Jump", "N"), ("branch_cr_set", "BranchCrSet", "SINT"),
    ("branch_cr_clear", "BranchCrClear", "SINT"), ("branch_ctr_nonzero", "BranchCtrNonzero", "NT"),
    ("branch_ctr_zero", "BranchCtrZero", "NT"), ("backedge", "Backedge", "NT"), ("exit", "Exit", "I"),
]
HOLE_LETTERS = {"D": "Destination", "S": "Source", "I": "Immediate", "N": "Next",
                "B": "Source2", "J": "Immediate2", "T": "Taken"}
FUNCTIONS = {"mu_stencil_" + suffix: operation for suffix, operation, _ in OPERATIONS}
REQUIRED = {operation: {HOLE_LETTERS[x] for x in letters} for _, operation, letters in OPERATIONS}
HOLES = {
    "mu_stencil_destination": "Destination", "mu_stencil_source": "Source",
    "mu_stencil_immediate": "Immediate", "mu_stencil_next": "Next",
    "mu_stencil_source2": "Source2", "mu_stencil_immediate2": "Immediate2",
    "mu_stencil_taken": "Taken",
}
JUMPS = ("Next", "Taken")
# xor eax,eax then RET or RET imm16=0: the guest return, result 0.
RETURN_SHAPES = (b"\x33\xC0\xC3", b"\x33\xC0\xC2\0\0")
# mov eax,[rip+Immediate] then RET or RET imm16=0: leave the chain with a nonzero exit number.
EXIT_SHAPES = (b"\x8B\x05\0\0\0\0\xC3", b"\x8B\x05\0\0\0\0\xC2\0\0")


class InvalidObject(ValueError):
    pass


def span(data, at, size):
    if at < 0 or size < 0 or at > len(data) or size > len(data) - at:
        raise InvalidObject("COFF field outside object")
    return data[at:at + size]


def unpack(fmt, data, at):
    return struct.unpack(fmt, span(data, at, struct.calcsize(fmt)))


def jump_kind(code, offset):
    """The transfer whose rel32 starts at offset: 'direct' (E9), 'conditional' (0F 8x) or None."""
    if offset >= 1 and code[offset - 1] == 0xE9:
        return "direct"
    if offset >= 2 and code[offset - 2] == 0x0F and 0x80 <= code[offset - 1] <= 0x8F:
        return "conditional"
    return None


def extract(data):
    machine, count, _, symbol_at, symbol_count, optional, _ = unpack("<HHIIIHH", data, 0)
    if machine != 0x8664 or optional or not count or count > 1024 or symbol_count > 100000:
        raise InvalidObject("expected a standard AMD64 COFF object, without optional header")
    span(data, 20, count * 40)
    span(data, symbol_at, symbol_count * 18)
    string_at = symbol_at + symbol_count * 18
    string_size, = unpack("<I", data, string_at)
    if string_size < 4:
        raise InvalidObject("invalid COFF string table")
    strings = span(data, string_at, string_size)

    def string(offset):
        if offset < 4 or offset >= len(strings):
            raise InvalidObject("invalid COFF string offset")
        end = strings.find(b"\0", offset)
        if end < 0:
            raise InvalidObject("unterminated COFF string")
        try:
            return strings[offset:end].decode("ascii")
        except UnicodeDecodeError as exc:
            raise InvalidObject("non-ASCII COFF symbol") from exc

    sections = []
    for index in range(count):
        header = span(data, 20 + index * 40, 40)
        raw_name = header[:8].split(b"\0", 1)[0]
        try:
            name = string(int(raw_name[1:])) if raw_name.startswith(b"/") else raw_name.decode("ascii")
        except (ValueError, UnicodeDecodeError) as exc:
            raise InvalidObject("invalid COFF section name") from exc
        size, raw_at, reloc_at = unpack("<III", header, 16)
        reloc_count, = unpack("<H", header, 32)
        flags, = unpack("<I", header, 36)
        if flags & 0x01000000: # IMAGE_SCN_LNK_NRELOC_OVFL requires a different record layout.
            raise InvalidObject("overflow relocation tables are unsupported")
        raw = span(data, raw_at, size) if size else b""
        relocations = [unpack("<IIH", data, reloc_at + i * 10) for i in range(reloc_count)]
        sections.append({"name": name, "code": raw, "flags": flags, "relocations": relocations})

    symbols = {}
    index = 0
    while index < symbol_count:
        record = span(data, symbol_at + index * 18, 18)
        zero, offset = struct.unpack("<II", record[:8])
        try:
            name = string(offset) if zero == 0 else record[:8].split(b"\0", 1)[0].decode("ascii")
        except UnicodeDecodeError as exc:
            raise InvalidObject("non-ASCII COFF symbol") from exc
        value, section, kind, storage, auxiliary = struct.unpack("<IhHBB", record[8:])
        if auxiliary > symbol_count - index - 1:
            raise InvalidObject("truncated COFF auxiliary symbols")
        symbols[index] = {"name": name, "value": value, "section": section, "kind": kind, "storage": storage}
        index += auxiliary + 1

    selected = {}
    for symbol in symbols.values():
        if symbol["name"] not in FUNCTIONS:
            continue
        if symbol["name"] in selected:
            raise InvalidObject("duplicate stencil function")
        if not (0 < symbol["section"] <= count) or symbol["value"] or symbol["kind"] != 0x20 or symbol["storage"] != 2:
            raise InvalidObject("stencils require one external function per /Gy section")
        selected[symbol["name"]] = symbol
    if set(selected) != set(FUNCTIONS):
        raise InvalidObject("object does not contain exactly the required stencil functions")
    selected_sections = {item["section"] for item in selected.values()}
    if len(selected_sections) != len(FUNCTIONS):
        raise InvalidObject("compile the stencil object with /Gy")
    for section in sections:
        if section["name"].startswith(".pdata"):
            for _, target, _ in section["relocations"]:
                symbol = symbols.get(target)
                if symbol is None:
                    raise InvalidObject("relocation targets an auxiliary or missing symbol")
                if symbol["section"] in selected_sections:
                    owner = [name for name, item in selected.items() if item["section"] == symbol["section"]]
                    raise InvalidObject("leaf stencils must have no unwind metadata or stack frame: " + owner[0])

    result = []
    for name, operation in FUNCTIONS.items():
        symbol = selected[name]
        section = sections[symbol["section"] - 1]
        code = section["code"]
        if not section["flags"] & 0x20 or not code or len(code) > 4096:
            raise InvalidObject("invalid stencil code section: " + name)
        relocations = []
        used = set()
        for offset, target, kind in section["relocations"]:
            dependency = symbols.get(target)
            if dependency is None or dependency["name"] not in HOLES or dependency["section"] != 0 or dependency["value"]:
                raise InvalidObject("unexpected stencil dependency in %s: %s" % (
                    name, dependency["name"] if dependency else "missing symbol"))
            if kind < 4 or kind > 9:
                raise InvalidObject("only AMD64 REL32_0..5 holes are supported: " + name)
            span(code, offset, 4)
            locations = set(range(offset, offset + 4))
            if locations & used:
                raise InvalidObject("overlapping stencil relocations: " + name)
            used |= locations
            addend, = unpack("<i", code, offset)
            if addend:
                raise InvalidObject("operand and jump holes must have zero addends: " + name)
            hole = HOLES[dependency["name"]]
            if hole in JUMPS:
                # Every Next or Taken reference is a jump out of the stencil, never a call or a
                # data reference. MSVC duplicates the tail of a stencil whose last statement
                # branches (adde, subfc), so several direct jumps to Next are admitted; only a
                # branch stencil may jump conditionally.
                transfer = jump_kind(code, offset)
                branch = "Taken" in REQUIRED[operation]
                if kind != 4 or transfer is None or (transfer == "conditional" and not branch):
                    raise InvalidObject("Next must be a direct tail jump: " + name)
            relocations.append({"offset": offset, "hole": hole, "addend": addend, "bias": kind - 4})
        present = [item["hole"] for item in relocations]
        if operation == "Return":
            # No nonzero stack pop and no extra bytes are admitted.
            if code not in RETURN_SHAPES or relocations:
                raise InvalidObject("return stencil must be xor eax,eax and a single ret with zero stack pop")
        elif operation == "Exit":
            if code not in EXIT_SHAPES or relocations != [
                    {"offset": 2, "hole": "Immediate", "addend": 0, "bias": 0}]:
                raise InvalidObject("exit stencil must load its number and return with zero stack pop")
        else:
            if set(present) != REQUIRED[operation]:
                raise InvalidObject("stencil does not reference exactly its required holes: %s has %s" % (
                    name, ",".join(sorted(set(present))) or "none"))
            jumps = [item for item in relocations if item["hole"] in JUMPS]
            # The last instruction is always an unconditional jump: no padding, ret or fallthrough.
            if not any(item["offset"] + 4 == len(code) and jump_kind(code, item["offset"]) == "direct"
                       for item in jumps):
                raise InvalidObject("stencil must end in a direct tail jump: " + name)
        result.append({"operation": operation, "code": code.hex(), "relocations": relocations})
    canonical = json.dumps(result, sort_keys=True, separators=(",", ":")).encode()
    return {"version": 2, "sha256": hashlib.sha256(canonical).hexdigest(), "stencils": result}


def render_header(table):
    lines = ["// Generated from leaf_stencils.cpp; do not edit.",
             "// SPDX-License-Identifier: GPL-2.0-or-later", "#pragma once",
             '#include "stencil_format.h"', "namespace ppc::stencil::generated {"]
    for item in table["stencils"]:
        name = item["operation"]
        code = bytes.fromhex(item["code"])
        lines.append(f"inline constexpr uint8_t code_{name}[] = {{" + ", ".join(f"0x{x:02X}" for x in code) + "};")
        if item["relocations"]:
            lines.append(f"inline constexpr Relocation holes_{name}[] = {{")
            lines += [f"  {{{r['offset']}u, Hole::{r['hole']}, {r['addend']}, {r['bias']}}}," for r in item["relocations"]]
            lines.append("};")
    lines.append("inline constexpr Stencil stencils[] = {")
    for item in table["stencils"]:
        name = item["operation"]
        holes = f"holes_{name}" if item["relocations"] else "nullptr"
        lines.append(f"  {{Operation::{name}, code_{name}, sizeof(code_{name}), {holes}, {len(item['relocations'])}}},")
    lines += ["};", f'inline constexpr Table table{{{table["version"]}u, "{table["sha256"]}", stencils, sizeof(stencils)/sizeof(stencils[0])}};', "}", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("object", type=Path)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    table = extract(args.object.read_bytes())
    rendered = render_header(table)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(rendered, encoding="utf-8")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(table, indent=2) + "\n", encoding="utf-8")
    print(f"leaf stencils: {len(table['stencils'])} extracted, sha256 {table['sha256']}")


if __name__ == "__main__":
    main()
