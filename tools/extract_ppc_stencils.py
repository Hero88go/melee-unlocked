#!/usr/bin/env python3
"""Extract the translator stencils from a standard AMD64 MSVC COFF object.

No third-party parser or player-side compiler is needed. The extraction fails closed.

A leaf stencil is rejected for unwind metadata (any saved register or stack frame), for any
relocation to a symbol that is not one of its holes, for overlapping relocations, for a hole set
other than the declared one and for a missing final tail jump.

A call-capable stencil (letter C in its row) may also have a stack frame, whose unwind data is
extracted and checked, and may reference the host symbols listed in EXTERNALS and read-only
compiler constants, whose bytes are copied into the table. Any other symbol rejects the object.

A failed extraction never overwrites an existing generated table.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def _snake(name):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


# (function suffix, Operation, holes) in table order. port/runtime/ppc/stencil_format.h carries
# the same rows; the extraction test compares the two lists. C marks a call-capable stencil.
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
# Third set: the function suffix is the operation name in snake case.
_THIRD = """
Subfe DSBNC
Crand DSBNC Cror DSBNC Crxor DSBNC Crnand DSBNC Crnor DSBNC Creqv DSBNC Crandc DSBNC Crorc DSBNC
Mcrxr DN
Lbz DSINC Lhz DSINC Lha DSINC Lwz DSINC Lbzu DSINC Lhzu DSINC Lhau DSINC Lwzu DSINC
Lbzx DSBNC Lhzx DSBNC Lhax DSBNC Lwzx DSBNC Lbzux DSBNC Lhzux DSBNC Lhaux DSBNC Lwzux DSBNC
Stb DSINC Sth DSINC Stw DSINC Stbu DSINC Sthu DSINC Stwu DSINC
Stbx DSBNC Sthx DSBNC Stwx DSBNC Stbux DSBNC Sthux DSBNC Stwux DSBNC
Lmw DSINC Stmw DSINC
Lwbrx DSBNC Lhbrx DSBNC Stwbrx DSBNC Sthbrx DSBNC
Lfs DSINC Lfsu DSINC Lfsx DSBNC Lfsux DSBNC Lfd DSINC Lfdu DSINC Lfdx DSBNC Lfdux DSBNC
Stfs DSINC Stfsu DSINC Stfsx DSBNC Stfsux DSBNC Stfd DSINC Stfdu DSINC Stfdx DSBNC Stfdux DSBNC
Stfiwx DSBNC
Fadd DSBN Fsub DSBN Fmul DSIN Fdiv DSBN
Fmadd DSIBN Fmsub DSIBN Fnmadd DSIBN Fnmsub DSIBN
Fadds DSBN Fsubs DSBN Fmuls DSIN Fdivs DSBN
Fmadds DSIBN Fmsubs DSIBN Fnmadds DSIBN Fnmsubs DSIBN
Fres DBNC Frsqrte DBNC Frsp DBN
Fmr DBN Fneg DBN Fabs DBN Fnabs DBN Fsel DSIBNC
Fcmp DSBN Fctiw DBNC Fctiwz DBNC
Mffs DN Mtfsf BIJNC Mtfsb0 INC Mtfsb1 INC Mtfsfi IJNC
Mcrfs DIN
PsqL DSIJNC PsqLu DSIJNC PsqLx DSBJNC PsqLux DSBJNC
PsqSt DSIJNC PsqStu DSIJNC PsqStx DSBJNC PsqStux DSBJNC
PsAdd DSBN PsSub DSBN PsMul DSIN PsDiv DSBN
PsMuls0 DSIN PsMuls1 DSIN
PsMadd DSIBNC PsMsub DSIBNC PsNmadd DSIBNC PsNmsub DSIBNC
PsMadds0 DSIBN PsMadds1 DSIBN PsSum0 DSIBN PsSum1 DSIBN
PsRes DBNC PsRsqrte DBNC PsSel DSIBNC
PsMr DBN PsNeg DBN PsAbs DBN PsNabs DBN
PsMerge00 DSBN PsMerge01 DSBN PsMerge10 DSBN PsMerge11 DSBN
FcmpPs1 DSBN
Call IJNC CallCtr INC TailCall INC TailCallCtr NC
SetLr IN CallChecked IJNC ResumeTest INTC
""".split()
OPERATIONS += [(_snake(name), name, letters) for name, letters in zip(_THIRD[0::2], _THIRD[1::2])]
HOLE_LETTERS = {"D": "Destination", "S": "Source", "I": "Immediate", "N": "Next",
                "B": "Source2", "J": "Immediate2", "T": "Taken"}
FUNCTIONS = {"mu_stencil_" + suffix: operation for suffix, operation, _ in OPERATIONS}
REQUIRED = {operation: {HOLE_LETTERS[x] for x in letters if x != "C"} for _, operation, letters in OPERATIONS}
MAY_CALL = {operation for _, operation, letters in OPERATIONS if "C" in letters}
HOLES = {
    "mu_stencil_destination": "Destination", "mu_stencil_source": "Source",
    "mu_stencil_immediate": "Immediate", "mu_stencil_next": "Next",
    "mu_stencil_source2": "Source2", "mu_stencil_immediate2": "Immediate2",
    "mu_stencil_taken": "Taken",
}
JUMPS = ("Next", "Taken")
# Host symbols a call-capable stencil may reference: COFF name -> (C++ address expression, kind).
# Direct: a function or object of the host image. Slot: an import pointer, which becomes an 8-byte
# slot in the translation. ImageBase: the linker's image base, found at translation time.
EXTERNALS = {
    "__ImageBase": ("nullptr", "ImageBase"),
    "?g_ram_watched@ppc@@3PAU?$atomic@E@std@@A": ("&ppc::g_ram_watched", "Direct"),
    "?g_ram_versions@ppc@@3PAU?$atomic@I@std@@A": ("&ppc::g_ram_versions", "Direct"),
    "?locked_cache@ppc@@YAPEAEXZ": ("&ppc::locked_cache", "Direct"),
    "?mmio_read@ppc@@YAIAEAUContext@1@IH@Z": ("&ppc::mmio_read", "Direct"),
    "?mmio_write@ppc@@YAXAEAUContext@1@IIH@Z": ("&ppc::mmio_write", "Direct"),
    "?mmio_read64@ppc@@YA_KAEAUContext@1@I@Z": ("&ppc::mmio_read64", "Direct"),
    "?mmio_write64@ppc@@YAXAEAUContext@1@I_K@Z": ("&ppc::mmio_write64", "Direct"),
    # The inline accessors of ppc.h, for the stencils in which MSVC declines to inline them: the
    # stencil then calls the host's own copy of the same function.
    "?ld8@ppc@@YAIAEAUContext@1@PEAEI@Z": ("&ppc::ld8", "Direct"),
    "?ld16@ppc@@YAIAEAUContext@1@PEAEI@Z": ("&ppc::ld16", "Direct"),
    "?ld32@ppc@@YAIAEAUContext@1@PEAEI@Z": ("&ppc::ld32", "Direct"),
    "?ld64@ppc@@YA_KAEAUContext@1@PEAEI@Z": ("&ppc::ld64", "Direct"),
    "?st8@ppc@@YAXAEAUContext@1@PEAEII@Z": ("&ppc::st8", "Direct"),
    "?st16@ppc@@YAXAEAUContext@1@PEAEII@Z": ("&ppc::st16", "Direct"),
    "?st32@ppc@@YAXAEAUContext@1@PEAEII@Z": ("&ppc::st32", "Direct"),
    "?st64@ppc@@YAXAEAUContext@1@PEAEI_K@Z": ("&ppc::st64", "Direct"),
    "?call@ppc@@YAXAEAUContext@1@PEAEI@Z": ("&ppc::call", "Direct"),
    "?update_mxcsr@ppc@@YAXAEAUContext@1@@Z": ("&ppc::update_mxcsr", "Direct"),
    "?g_resumed_returns@ppc@@3_KA": ("&ppc::g_resumed_returns", "Direct"),
    "?g_computed_return_checks@ppc@@3_KA": ("&ppc::g_computed_return_checks", "Direct"),
    "?fres@ppc@@YANN@Z": ("&ppc::fres", "Direct"),
    "?frsqrte@ppc@@YANN@Z": ("&ppc::frsqrte", "Direct"),
    "?psq_load@ppc@@YAXAEAUContext@1@PEAEIIII@Z": ("&ppc::psq_load", "Direct"),
    "?psq_store@ppc@@YAXAEAUContext@1@PEAEIIII@Z": ("&ppc::psq_store", "Direct"),
    "trunc": ("static_cast<double (*)(double)>(&::trunc)", "Direct"),
    "nearbyint": ("static_cast<double (*)(double)>(&::nearbyint)", "Direct"),
    "_dclass": ("&::_dclass", "Direct"),
    "__imp__dclass": ("&::_dclass", "Slot"),
    "__imp_trunc": ("static_cast<double (*)(double)>(&::trunc)", "Slot"),
    "__imp_nearbyint": ("static_cast<double (*)(double)>(&::nearbyint)", "Slot"),
}
# xor eax,eax then RET or RET imm16=0: the guest return, result 0.
RETURN_SHAPES = (b"\x33\xC0\xC3", b"\x33\xC0\xC2\0\0")
# mov eax,[rip+Immediate] then RET or RET imm16=0: leave the chain with a nonzero exit number.
EXIT_SHAPES = (b"\x8B\x05\0\0\0\0\xC3", b"\x8B\x05\0\0\0\0\xC2\0\0")
VERSION = 3
# IMAGE_SCN flags.
SCN_CODE, SCN_INITIALIZED, SCN_WRITE = 0x20, 0x40, 0x80000000
REL_ADDR32NB = 3


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


def check_unwind(info, range_size, name):
    """UNWIND_INFO: version 1, no handler, no frame register, plain codes only.

    Returns (size of the header and codes, chained). A chained record (UNW_FLAG_CHAININFO, which
    MSVC writes when it saves registers in the middle of a function) is followed by the
    RUNTIME_FUNCTION of the record it continues.
    """
    if len(info) < 4:
        raise InvalidObject("truncated unwind data: " + name)
    flags = info[0] >> 3
    if info[0] & 7 != 1 or flags not in (0, 4):
        raise InvalidObject("unsupported unwind version or flags (handler): " + name)
    prolog, count, frame = info[1], info[2], info[3]
    if frame:
        raise InvalidObject("unwind data with a frame register: " + name)
    if prolog > range_size:
        raise InvalidObject("unwind prolog longer than the stencil: " + name)
    size = 4 + 2 * ((count + 1) & ~1)
    if len(info) < size + (12 if flags else 0):
        raise InvalidObject("truncated unwind data: " + name)
    i = 0
    while i < count:
        offset, op, detail = info[4 + 2 * i], info[5 + 2 * i] & 15, info[5 + 2 * i] >> 4
        # PUSH_NONVOL, ALLOC_LARGE, ALLOC_SMALL, SAVE_NONVOL(_FAR), SAVE_XMM128(_FAR).
        slots = {0: 1, 2: 1, 4: 2, 8: 2, 5: 3, 9: 3}.get(op)
        if op == 1:
            slots = {0: 2, 1: 3}.get(detail)
        if not slots or offset > prolog:
            raise InvalidObject("unsupported unwind code: " + name)
        i += slots
    if i != count:
        raise InvalidObject("truncated unwind codes: " + name)
    return size, bool(flags)


def extract(data):
    machine, count, _, symbol_at, symbol_count, optional, _ = unpack("<HHIIIHH", data, 0)
    if machine != 0x8664 or optional or not count or count > 4096 or symbol_count > 100000:
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
        raw = span(data, raw_at, size) if size and raw_at else b""
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
    selected_sections = {item["section"]: name for name, item in selected.items()}
    if len(selected_sections) != len(FUNCTIONS):
        raise InvalidObject("compile the stencil object with /Gy")

    # Unwind records. A leaf stencil must have none. A call-capable stencil may have records that
    # together cover the whole function: one, or several when MSVC saves registers in the middle
    # of the function and chains the later ranges to an earlier one.
    found = {}
    for section in sections:
        if not section["name"].startswith(".pdata"):
            continue
        by_offset = {offset: (target, kind) for offset, target, kind in section["relocations"]}
        for offset, target, kind in section["relocations"]:
            symbol = symbols.get(target)
            if symbol is None:
                raise InvalidObject("relocation targets an auxiliary or missing symbol")
            if symbol["section"] not in selected_sections:
                continue
            name = selected_sections[symbol["section"]]
            operation = FUNCTIONS[name]
            if operation not in MAY_CALL:
                raise InvalidObject("leaf stencils must have no unwind metadata or stack frame: " + name)
            if offset % 12:
                continue # The end address of the record whose begin address was checked.
            begin, end, info_at = unpack("<III", section["code"], offset)
            end_reloc, info_reloc = by_offset.get(offset + 4), by_offset.get(offset + 8)
            if kind != REL_ADDR32NB or end_reloc is None or info_reloc is None or \
                    end_reloc[1] != REL_ADDR32NB or info_reloc[1] != REL_ADDR32NB:
                raise InvalidObject("unexpected unwind record relocations: " + name)
            end_symbol, info_symbol = symbols.get(end_reloc[0]), symbols.get(info_reloc[0])
            if end_symbol is None or info_symbol is None or end_symbol["section"] != symbol["section"]:
                raise InvalidObject("unexpected unwind record relocations: " + name)
            if not (0 < info_symbol["section"] <= count):
                raise InvalidObject("unwind data is not in this object: " + name)
            found.setdefault(name, []).append({
                "begin": symbol["value"] + begin, "end": end_symbol["value"] + end,
                "section": info_symbol["section"], "start": info_symbol["value"] + info_at})
    unwind = {}
    for name, records in found.items():
        function_section = selected[name]["section"]
        size = len(sections[function_section - 1]["code"])
        records.sort(key=lambda record: record["begin"])
        if records[0]["begin"] != 0 or records[-1]["end"] != size or \
                any(record["begin"] >= record["end"] for record in records) or \
                any(records[i]["end"] != records[i + 1]["begin"] for i in range(len(records) - 1)):
            raise InvalidObject("unwind record does not cover the whole stencil: " + name)
        out = []
        for index, record in enumerate(records):
            home = sections[record["section"] - 1]
            if not home["name"].startswith(".xdata"):
                raise InvalidObject("unwind data is not in .xdata: " + name)
            start = record["start"]
            raw = span(home["code"], start, len(home["code"]) - start)
            codes, chained = check_unwind(raw, record["end"] - record["begin"], name)
            extent = codes + (12 if chained else 0)
            inside = {r[0]: (r[1], r[2]) for r in home["relocations"] if start < r[0] + 4 and r[0] < start + extent}
            parent = -1
            if chained:
                # The RUNTIME_FUNCTION of the record this one continues: it must be an earlier
                # record of this stencil, which the translator rebuilds for every copy.
                at = start + codes
                if set(inside) != {at, at + 4, at + 8} or any(k != REL_ADDR32NB for _, k in inside.values()):
                    raise InvalidObject("unexpected chained unwind relocations: " + name)
                first, last = symbols.get(inside[at][0]), symbols.get(inside[at + 4][0])
                if first is None or last is None or first["section"] != function_section or \
                        last["section"] != function_section:
                    raise InvalidObject("chained unwind record of another function: " + name)
                parent_range = (first["value"] + unpack("<I", home["code"], at)[0],
                                last["value"] + unpack("<I", home["code"], at + 4)[0])
                matches = [i for i, other in enumerate(records) if (other["begin"], other["end"]) == parent_range]
                if len(matches) != 1 or matches[0] >= index:
                    raise InvalidObject("chained unwind record without its parent: " + name)
                parent = matches[0]
            elif inside:
                raise InvalidObject("unwind data with a relocation (handler): " + name)
            info = bytes(raw[:codes]) + (b"\0" * 12 if chained else b"")
            out.append({"begin": record["begin"], "end": record["end"], "info": info.hex(), "parent": parent})
        unwind[name] = out

    externals, constants = [], []
    result = []
    for name, operation in FUNCTIONS.items():
        symbol = selected[name]
        section = sections[symbol["section"] - 1]
        code = section["code"]
        may_call = operation in MAY_CALL
        if not section["flags"] & SCN_CODE or not code or len(code) > 4096:
            raise InvalidObject("invalid stencil code section: " + name)
        code = bytearray(code)
        relocations = []
        used = set()
        for offset, target, kind in section["relocations"]:
            dependency = symbols.get(target)
            if dependency is None:
                raise InvalidObject("unexpected stencil dependency in %s: missing symbol" % name)
            span(code, offset, 4)
            locations = set(range(offset, offset + 4))
            if locations & used:
                raise InvalidObject("overlapping stencil relocations: " + name)
            used |= locations
            addend, = unpack("<i", code, offset)
            is_hole = dependency["name"] in HOLES and dependency["section"] == 0 and not dependency["value"]
            if is_hole:
                if kind < 4 or kind > 9:
                    raise InvalidObject("only AMD64 REL32_0..5 holes are supported: " + name)
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
                relocations.append({"offset": offset, "hole": hole, "addend": 0, "bias": kind - 4})
                continue
            if not may_call:
                raise InvalidObject("unexpected stencil dependency in %s: %s" % (name, dependency["name"]))
            # A listed host symbol: undefined here, or an inline function of ppc.h that this
            # object also carries a copy of (the linker of the host keeps one for everybody).
            inline_copy = dependency["section"] > 0 and dependency["kind"] == 0x20 and dependency["storage"] == 2
            if dependency["name"] in EXTERNALS and not dependency["value"] and \
                    (dependency["section"] == 0 or inline_copy):
                host_kind = EXTERNALS[dependency["name"]][1]
                if dependency["name"] not in externals:
                    externals.append(dependency["name"])
                index = externals.index(dependency["name"])
                if kind == REL_ADDR32NB:
                    if host_kind != "Direct":
                        raise InvalidObject("image-relative reference to %s in %s" % (dependency["name"], name))
                    relocations.append({"offset": offset, "hole": "ExternalRva", "addend": addend, "bias": 0,
                                        "symbol": dependency["name"], "index": index})
                elif 4 <= kind <= 9:
                    if addend:
                        raise InvalidObject("host reference with an addend in %s: %s" % (name, dependency["name"]))
                    relocations.append({"offset": offset, "hole": "External", "addend": 0, "bias": kind - 4,
                                        "symbol": dependency["name"], "index": index})
                else:
                    raise InvalidObject("unsupported relocation to %s in %s" % (dependency["name"], name))
            elif 0 < dependency["section"] <= count:
                # A compiler constant: read-only initialized data of this object, without
                # relocations of its own. Its bytes travel with the table.
                home = sections[dependency["section"] - 1]
                flags = home["flags"]
                if flags & (SCN_CODE | SCN_WRITE) or not flags & SCN_INITIALIZED or home["relocations"] or \
                        not home["name"].startswith(".rdata") or not home["code"] or len(home["code"]) > 64:
                    raise InvalidObject("unexpected stencil dependency in %s: %s" % (name, dependency["name"]))
                if kind < 4 or kind > 9:
                    raise InvalidObject("only REL32 references to constants are supported: " + name)
                within = dependency["value"] + addend
                if within < 0 or within >= len(home["code"]):
                    raise InvalidObject("constant reference outside its section: " + name)
                alignment = 1 << (((flags >> 20) & 15) - 1) if (flags >> 20) & 15 else 16
                item = {"bytes": home["code"].hex(), "alignment": min(alignment, 64)}
                if item not in constants:
                    constants.append(item)
                relocations.append({"offset": offset, "hole": "Constant", "addend": within, "bias": kind - 4,
                                    "index": constants.index(item)})
            else:
                raise InvalidObject("unexpected stencil dependency in %s: %s" % (name, dependency["name"]))
            code[offset:offset + 4] = b"\0\0\0\0" # The addend now lives in the table.
        code = bytes(code)
        present = [item["hole"] for item in relocations if item["hole"] in HOLE_LETTERS.values()]
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
        entry = {"operation": operation, "code": code.hex(), "relocations": relocations}
        if name in unwind:
            entry["unwind"] = unwind[name]
        result.append(entry)
    table = {"stencils": result,
             "externals": [{"name": name, "kind": EXTERNALS[name][1]} for name in externals],
             "constants": constants}
    canonical = json.dumps(table, sort_keys=True, separators=(",", ":")).encode()
    return {"version": VERSION, "sha256": hashlib.sha256(canonical).hexdigest(), **table}


def render_header(table):
    lines = ["// Generated from leaf_stencils.cpp; do not edit.",
             "// SPDX-License-Identifier: GPL-2.0-or-later", "#pragma once",
             '#include "stencil_format.h"', '#include "ppc.h"', "#include <cmath>",
             "namespace ppc::stencil::generated {"]

    def array(name, raw):
        lines.append(f"inline constexpr uint8_t {name}[] = {{" + ", ".join(f"0x{x:02X}" for x in raw) + "};")

    for item in table["stencils"]:
        name = item["operation"]
        array(f"code_{name}", bytes.fromhex(item["code"]))
        if item["relocations"]:
            lines.append(f"inline constexpr Relocation holes_{name}[] = {{")
            lines += [f"  {{{r['offset']}u, Hole::{r['hole']}, {r['addend']}, {r['bias']}, {r.get('index', 0)}}},"
                      for r in item["relocations"]]
            lines.append("};")
        if "unwind" in item:
            for index, record in enumerate(item["unwind"]):
                array(f"unwind_{name}_{index}", bytes.fromhex(record["info"]))
            lines.append(f"inline constexpr UnwindRecord unwind_{name}[] = {{")
            lines += [f"  {{{record['begin']}u, {record['end']}u, unwind_{name}_{index}, "
                      f"sizeof(unwind_{name}_{index}), {record['parent']}}},"
                      for index, record in enumerate(item["unwind"])]
            lines.append("};")
    for index, item in enumerate(table["constants"]):
        array(f"constant_{index}", bytes.fromhex(item["bytes"]))
    if table["constants"]:
        lines.append("inline constexpr Constant constants[] = {")
        lines += [f"  {{constant_{index}, sizeof(constant_{index}), {item['alignment']}u}},"
                  for index, item in enumerate(table["constants"])]
        lines.append("};")
    if table["externals"]:
        # Addresses of the host's own functions and objects: resolved by the linker of the program
        # that includes this header, so the table always names the runtime it is linked with.
        lines.append("inline const External externals[] = {")
        for item in table["externals"]:
            expression = EXTERNALS[item["name"]][0]
            address = "nullptr" if item["kind"] == "ImageBase" else f"reinterpret_cast<const void*>({expression})"
            lines.append(f'  {{"{item["name"]}", {address}, ExternalKind::{item["kind"]}}},')
        lines.append("};")
    lines.append("inline constexpr Stencil stencils[] = {")
    for item in table["stencils"]:
        name = item["operation"]
        holes = f"holes_{name}" if item["relocations"] else "nullptr"
        info = f"unwind_{name}, {len(item['unwind'])}" if "unwind" in item else "nullptr, 0"
        lines.append(f"  {{Operation::{name}, code_{name}, sizeof(code_{name}), {holes}, {len(item['relocations'])}, {info}}},")
    externals = f"externals, {len(table['externals'])}" if table["externals"] else "nullptr, 0"
    constants = f"constants, {len(table['constants'])}" if table["constants"] else "nullptr, 0"
    lines += ["};", f'inline const Table table{{{table["version"]}u, "{table["sha256"]}", stencils, '
                    f'sizeof(stencils)/sizeof(stencils[0]), {externals}, {constants}}};', "}", ""]
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
    framed = sum(1 for item in table["stencils"] if "unwind" in item)
    print(f"leaf stencils: {len(table['stencils'])} extracted ({framed} with a stack frame and unwind data, "
          f"{len(table['externals'])} host symbols, {len(table['constants'])} constants), sha256 {table['sha256']}")


if __name__ == "__main__":
    main()
