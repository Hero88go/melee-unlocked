"""Synthetic COFF fixtures for bounded extraction, relocation holes and fail-closed errors."""
# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
import re
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("extract_ppc_stencils", ROOT / "tools/extract_ppc_stencils.py")
extractor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extractor)

OPERAND_HOLES = ("Destination", "Source", "Immediate", "Source2", "Immediate2")


def fixture():
    """One section per stencil, shaped as its declared hole set requires.

    Operand holes are RIP-relative loads; a linear stencil ends in one tail jump to Next, a
    branch stencil in a conditional jump to Taken and a tail jump to Next. Only the parser is
    under test; these synthetic bytes are never executed or used as instruction semantics.
    """
    functions = list(extractor.FUNCTIONS)
    dependencies = list(extractor.HOLES)
    index = {hole: len(functions) + i for i, hole in enumerate(extractor.HOLES.values())}
    sections = []
    for i, name in enumerate(functions):
        operation = extractor.FUNCTIONS[name]
        required = extractor.REQUIRED[operation]
        code = bytearray()
        relocations = []
        if operation == "Return":
            code += b"\x33\xC0\xC3"
        elif operation == "Exit":
            code += b"\x8B\x05\0\0\0\0\xC3"
            relocations.append((2, index["Immediate"], 4))
        else:
            for hole in OPERAND_HOLES:
                if hole in required:
                    code += b"\x8B\x05\0\0\0\0"
                    relocations.append((len(code) - 4, index[hole], 4))
            if "Taken" in required:
                code += b"\x0F\x85\0\0\0\0"
                relocations.append((len(code) - 4, index["Taken"], 4))
            code += b"\xE9\0\0\0\0"
            relocations.append((len(code) - 4, index["Next"], 4))
        sections.append({"name": ".text$%d" % i, "code": bytes(code), "relocations": relocations})
    return {"functions": functions, "dependencies": dependencies, "sections": sections}


def coff(model):
    strings = bytearray(b"\0\0\0\0")

    def symbol_name(name):
        encoded = name.encode("ascii")
        if len(encoded) <= 8:
            return encoded.ljust(8, b"\0")
        offset = len(strings)
        strings.extend(encoded + b"\0")
        return struct.pack("<II", 0, offset)

    section_count = len(model["sections"])
    payload = bytearray()
    headers = bytearray()
    for section in model["sections"]:
        raw_at = 20 + section_count * 40 + len(payload)
        payload.extend(section["code"])
        reloc_at = 20 + section_count * 40 + len(payload)
        payload.extend(b"".join(struct.pack("<IIH", *r) for r in section["relocations"]))
        headers.extend(struct.pack("<8sIIIIIIHHI", section["name"].encode("ascii"), 0, 0,
                                   len(section["code"]), raw_at, reloc_at, 0,
                                   len(section["relocations"]), 0, section.get("flags", 0x60000020)))
    symbols = bytearray()
    for i, name in enumerate(model["functions"]):
        symbols.extend(symbol_name(name) + struct.pack("<IhHBB", 0, i + 1, 0x20, 2, 0))
    for name in model["dependencies"]:
        symbols.extend(symbol_name(name) + struct.pack("<IhHBB", 0, 0, 0, 2, 0))
    for name, value, section in model.get("defined", []):  # static symbols inside a section
        symbols.extend(symbol_name(name) + struct.pack("<IhHBB", value, section, 0, 3, 0))
    struct.pack_into("<I", strings, 0, len(strings))
    symbol_at = 20 + len(headers) + len(payload)
    header = struct.pack("<HHIIIHH", 0x8664, section_count, 0, symbol_at, len(symbols) // 18, 0, 0)
    return bytes(header + headers + payload + symbols + strings)


class ExtractTest(unittest.TestCase):
    def setUp(self):
        self.model = fixture()
        self.names = [extractor.FUNCTIONS[name] for name in self.model["functions"]]

    def section(self, operation):
        return self.model["sections"][self.names.index(operation)]

    def symbol(self, hole):
        return len(self.model["functions"]) + list(extractor.HOLES.values()).index(hole)

    def rejected(self, message):
        with self.assertRaisesRegex(extractor.InvalidObject, message):
            extractor.extract(coff(self.model))

    def test_extract_and_render_are_deterministic(self):
        table = extractor.extract(coff(self.model))
        self.assertEqual(len(table["stencils"]), len(extractor.OPERATIONS))
        self.assertEqual([item["operation"] for item in table["stencils"]], self.names)
        self.assertEqual(table, extractor.extract(coff(self.model)))
        header = extractor.render_header(table)
        self.assertIn("Hole::Destination", header)
        self.assertIn("nullptr, 0", header)
        self.assertIn(table["sha256"], header)
        for hole in set().union(*extractor.REQUIRED.values()):
            self.assertIn("Hole::" + hole, header)

    def test_operation_list_matches_the_runtime_header(self):
        text = (ROOT / "port/runtime/ppc/stencil_format.h").read_text(encoding="utf-8")
        body = text.split("#define MU_STENCIL_OPERATIONS(X)", 1)[1].split("enum class Operation", 1)[0]
        rows = [(name, "".join(letters.replace("0", "").split("|")))
                for name, letters in re.findall(r"X\((\w+),\s*([A-Z0-9|]+)\)", body)]
        expected = [(operation, letters) for _, operation, letters in extractor.OPERATIONS]
        self.assertEqual([name for name, _ in rows], [name for name, _ in expected])
        self.assertEqual([(name, set(letters)) for name, letters in rows],
                         [(name, set(letters)) for name, letters in expected])
        version = re.search(r"kFormatVersion = (\d+);", text)
        self.assertEqual(int(version.group(1)), extractor.extract(coff(self.model))["version"])

    def test_function_names_are_unique_and_prefixed(self):
        self.assertEqual(len(extractor.FUNCTIONS), len(extractor.OPERATIONS))
        self.assertEqual(len(set(extractor.FUNCTIONS.values())), len(extractor.OPERATIONS))
        self.assertTrue(all(name.startswith("mu_stencil_") for name in extractor.FUNCTIONS))
        self.assertFalse(set(extractor.FUNCTIONS) & set(extractor.HOLES))

    def test_rel32_biases_are_retained(self):
        offset, target, _ = self.section("Add")["relocations"][0]
        self.section("Add")["relocations"][0] = (offset, target, 9)
        table = extractor.extract(coff(self.model))
        self.assertEqual(table["stencils"][0]["relocations"][0]["bias"], 5)

    def test_truncated_object_fields(self):
        blob = coff(self.model)
        for size in (0, 19, 45, len(blob) - 1):
            with self.subTest(size=size), self.assertRaises(extractor.InvalidObject):
                extractor.extract(blob[:size])

    def test_wrong_architecture(self):
        blob = bytearray(coff(self.model))
        struct.pack_into("<H", blob, 0, 0x14C)
        with self.assertRaisesRegex(extractor.InvalidObject, "AMD64"):
            extractor.extract(bytes(blob))

    def test_bigobj_header_is_rejected(self):
        blob = bytearray(coff(self.model))
        struct.pack_into("<HH", blob, 0, 0, 0xFFFF)
        with self.assertRaisesRegex(extractor.InvalidObject, "AMD64"):
            extractor.extract(bytes(blob))

    def test_overflow_relocation_layout_is_rejected(self):
        blob = bytearray(coff(self.model))
        struct.pack_into("<I", blob, 20 + 36, 0x61000020)
        with self.assertRaisesRegex(extractor.InvalidObject, "overflow relocation"):
            extractor.extract(bytes(blob))

    def test_truncated_auxiliary_symbol_records(self):
        blob = bytearray(coff(self.model))
        symbol_at, symbol_count = struct.unpack_from("<II", blob, 8)
        blob[symbol_at + (symbol_count - 1) * 18 + 17] = 1
        with self.assertRaisesRegex(extractor.InvalidObject, "auxiliary"):
            extractor.extract(bytes(blob))

    def test_invalid_long_symbol_offset(self):
        blob = bytearray(coff(self.model))
        symbol_at, = struct.unpack_from("<I", blob, 8)
        struct.pack_into("<II", blob, symbol_at, 0, 0xFFFFFFFF)
        with self.assertRaisesRegex(extractor.InvalidObject, "string offset"):
            extractor.extract(bytes(blob))

    def test_function_must_start_at_section_zero(self):
        blob = bytearray(coff(self.model))
        symbol_at, = struct.unpack_from("<I", blob, 8)
        struct.pack_into("<I", blob, symbol_at + 8, 1)
        with self.assertRaisesRegex(extractor.InvalidObject, "/Gy section"):
            extractor.extract(bytes(blob))

    def test_unknown_dependency(self):
        self.model["dependencies"][0] = "__security_cookie"
        self.rejected("unexpected stencil dependency")

    def test_helper_call_is_an_unknown_dependency(self):
        # A stencil that calls out (a memory slow path, ppc::loop_poll) names the helper here.
        self.model["dependencies"].append("?mmio_read@ppc@@YAIAEAUContext@1@IH@Z")
        section = self.section("AddReg")
        section["code"] = b"\xE8\0\0\0\0" + section["code"]
        section["relocations"] = [(1, len(self.model["functions"]) + len(self.model["dependencies"]) - 1, 4)] + [
            (offset + 5, target, kind) for offset, target, kind in section["relocations"]]
        self.rejected("unexpected stencil dependency in mu_stencil_add_reg: \\?mmio_read")

    def test_missing_function(self):
        self.model["functions"][1] = "other_function"
        self.rejected("required stencil functions")

    def test_overlapping_relocations(self):
        self.section("Add")["relocations"].append((3, self.symbol("Destination"), 4))
        self.rejected("overlapping")

    def test_out_of_bounds_relocation(self):
        self.section("Add")["relocations"][0] = (1000, self.symbol("Destination"), 4)
        self.rejected("outside object")

    def test_absolute_relocation(self):
        offset, target, _ = self.section("Add")["relocations"][0]
        self.section("Add")["relocations"][0] = (offset, target, 1)
        self.rejected("REL32")

    def test_nonzero_addend(self):
        code = bytearray(self.section("Add")["code"])
        struct.pack_into("<i", code, 2, 1)
        self.section("Add")["code"] = bytes(code)
        self.rejected("zero addends")

    def test_next_must_be_a_jump_not_a_call(self):
        for operation in ("Add", "Record", "Rlwinm"):
            with self.subTest(operation=operation):
                self.setUp()
                code = bytearray(self.section(operation)["code"])
                code[-5] = 0xE8
                self.section(operation)["code"] = bytes(code)
                self.rejected("tail jump")

    def test_next_must_be_last(self):
        self.section("Add")["code"] += b"\xC3"
        self.rejected("tail jump")

    def test_padding_after_the_tail_jump_is_rejected(self):
        for padding in (b"\xCC", b"\x90", b"\xCC" * 11):
            with self.subTest(padding=padding):
                self.setUp()
                self.section("Subf")["code"] += padding
                self.rejected("tail jump")

    def test_linear_stencil_with_a_conditional_next_is_rejected(self):
        section = self.section("Add")
        section["code"] = section["code"][:-5] + b"\x0F\x84\0\0\0\0"
        offset, target, kind = section["relocations"][-1]
        section["relocations"][-1] = (offset + 1, target, kind)
        self.rejected("tail jump")

    def test_duplicated_direct_tail_jump_is_admitted(self):
        # MSVC ends both arms of a final branch with their own jmp Next (adde, subfc).
        section = self.section("Add")
        at = len(section["code"])
        section["code"] += b"\x33\xC0\xE9\0\0\0\0"
        section["relocations"].append((at + 3, self.symbol("Next"), 4))
        table = extractor.extract(coff(self.model))
        self.assertEqual([r["hole"] for r in table["stencils"][0]["relocations"]].count("Next"), 2)

    def test_duplicated_tail_jump_must_still_end_the_stencil(self):
        section = self.section("Add")
        at = len(section["code"])
        section["code"] += b"\xE9\0\0\0\0\x33\xC0"
        section["relocations"].append((at + 1, self.symbol("Next"), 4))
        self.rejected("tail jump")

    def test_next_as_data_or_call_in_the_middle_is_rejected(self):
        for prefix in (b"\xE8", b"\x48\x8D\x05", b"\x8B\x05"):
            with self.subTest(prefix=prefix):
                self.setUp()
                section = self.section("Add")
                shift = len(prefix) + 4
                section["relocations"] = [(len(prefix), self.symbol("Next"), 4)] + [
                    (offset + shift, target, kind) for offset, target, kind in section["relocations"]]
                section["code"] = prefix + b"\0\0\0\0" + section["code"]
                self.rejected("tail jump")

    def test_branch_stencil_layouts(self):
        # MSVC writes either jcc Taken; jmp Next, or a short jcc over jmp Taken and then jmp Next.
        table = extractor.extract(coff(self.model))
        holes = [r["hole"] for r in table["stencils"][self.names.index("Backedge")]["relocations"]]
        self.assertEqual(holes, ["Taken", "Next"])
        section = self.section("BranchCtrNonzero")
        section["code"] = b"\x74\x05\xE9\0\0\0\0\xE9\0\0\0\0"
        section["relocations"] = [(3, self.symbol("Taken"), 4), (8, self.symbol("Next"), 4)]
        extractor.extract(coff(self.model))
        section["relocations"] = [(3, self.symbol("Next"), 4), (8, self.symbol("Taken"), 4)]
        extractor.extract(coff(self.model))

    def test_branch_stencil_must_end_in_a_direct_jump(self):
        for code, relocations in (
                (b"\xE9\0\0\0\0\x0F\x85\0\0\0\0", ((1, "Taken"), (7, "Next"))),      # ends conditional
                (b"\x0F\x85\0\0\0\0\xE9\0\0\0\0\xCC", ((2, "Taken"), (7, "Next"))),  # padding
                (b"\x0F\x85\0\0\0\0\xE9\0\0\0\0\xC3", ((2, "Taken"), (7, "Next"))),  # trailing ret
        ):
            with self.subTest(code=code):
                self.setUp()
                section = self.section("BranchCtrZero")
                section["code"] = code
                section["relocations"] = [(offset, self.symbol(hole), 4) for offset, hole in relocations]
                self.rejected("end in a direct tail jump")

    def test_taken_must_be_a_jump(self):
        for prefix in (b"\xE8", b"\x48\x8D\x05", b"\xFF\x25"):
            with self.subTest(prefix=prefix):
                self.setUp()
                section = self.section("Backedge")
                section["code"] = prefix + b"\0\0\0\0\xE9\0\0\0\0"
                section["relocations"] = [(len(prefix), self.symbol("Taken"), 4),
                                          (len(prefix) + 5, self.symbol("Next"), 4)]
                self.rejected("tail jump")

    def test_branch_stencil_needs_both_exits(self):
        for missing in ("Taken", "Next"):
            with self.subTest(missing=missing):
                self.setUp()
                section = self.section("BranchCrSet")
                kept = "Next" if missing == "Taken" else "Taken"
                section["relocations"] = [
                    (offset, self.symbol(kept) if target == self.symbol(missing) else target, kind)
                    for offset, target, kind in section["relocations"]]
                self.rejected("required holes")

    def test_linear_stencil_may_not_reference_taken(self):
        section = self.section("Add")
        offset, _, kind = section["relocations"][-1]
        section["relocations"][-1] = (offset, self.symbol("Taken"), kind)
        self.rejected("required holes")

    def test_jump_hole_bias_is_rejected(self):
        section = self.section("Jump")
        offset, target, _ = section["relocations"][-1]
        section["relocations"][-1] = (offset, target, 5)
        self.rejected("tail jump")

    def test_jump_stencil_is_one_tail_jump(self):
        table = extractor.extract(coff(self.model))
        self.assertEqual(table["stencils"][self.names.index("Jump")]["code"], "e900000000")

    def test_exit_shapes(self):
        position = self.names.index("Exit")
        for code in extractor.EXIT_SHAPES:
            self.section("Exit")["code"] = code
            self.assertEqual(extractor.extract(coff(self.model))["stencils"][position]["code"], code.hex())
        for code in (b"\x8B\x05\0\0\0\0\xC2\x08\0", b"\x8B\x05\0\0\0\0\xC3\x90", b"\x8B\x0D\0\0\0\0\xC3",
                     b"\x8B\x05\0\0\0\0\x90\xC3", b"\x8B\x05\0\0\0\0"):
            with self.subTest(code=code):
                self.section("Exit")["code"] = code
                self.rejected("exit stencil")

    def test_exit_needs_exactly_its_number(self):
        section = self.section("Exit")
        section["relocations"] = []
        self.rejected("exit stencil")
        section["relocations"] = [(2, self.symbol("Source"), 4)]
        self.rejected("exit stencil")
        section["relocations"] = [(2, self.symbol("Immediate"), 5)]
        self.rejected("exit stencil")
        section["relocations"] = [(2, self.symbol("Next"), 4)]
        self.rejected("tail jump")

    def test_missing_operand_hole(self):
        del self.section("Add")["relocations"][2]
        self.rejected("required holes")

    def test_extra_operand_hole(self):
        # Record declares Source and Next only; a read of the destination cell is not expected.
        section = self.section("Record")
        section["code"] = b"\x8B\x05\0\0\0\0" + section["code"]
        section["relocations"] = [(2, self.symbol("Destination"), 4)] + [
            (offset + 6, target, kind) for offset, target, kind in section["relocations"]]
        self.rejected("required holes: mu_stencil_record")

    def test_every_declared_hole_is_required(self):
        for operation in self.names:
            required = extractor.REQUIRED[operation] - {"Next", "Taken"}
            for hole in sorted(required):
                if operation == "Exit":
                    continue
                with self.subTest(operation=operation, hole=hole):
                    self.setUp()
                    section = self.section(operation)
                    section["relocations"] = [r for r in section["relocations"] if r[1] != self.symbol(hole)]
                    self.rejected("required holes")

    def test_nontrivial_return(self):
        self.section("Return")["code"] = b"\x90\x33\xC0\xC3"
        self.rejected("single ret")

    def test_return_without_a_zero_result_is_rejected(self):
        for code in (b"\xC3", b"\xC2\0\0", b"\x33\xC9\xC3", b"\xB8\x01\0\0\0\xC3"):
            with self.subTest(code=code):
                self.section("Return")["code"] = code
                self.rejected("single ret")

    def test_msvc_zero_pop_return(self):
        self.section("Return")["code"] = b"\x33\xC0\xC2\0\0"
        table = extractor.extract(coff(self.model))
        self.assertEqual(table["stencils"][self.names.index("Return")]["code"], "33c0c20000")

    def test_return_with_stack_pop_is_rejected(self):
        for code in (b"\x33\xC0\xC2\x08\0", b"\x33\xC0\xC2\0\x01"):
            with self.subTest(code=code):
                self.section("Return")["code"] = code
                self.rejected("zero stack pop")

    def test_return_with_trailing_code_is_rejected(self):
        for code in (b"\x33\xC0\xC3\x90", b"\x33\xC0\xC2\0\0\x90"):
            with self.subTest(code=code):
                self.section("Return")["code"] = code
                self.rejected("single ret")

    def test_return_with_a_relocation_is_rejected(self):
        self.section("Return")["code"] = b"\x33\xC0\xC3"
        self.section("Return")["relocations"] = [(0, self.symbol("Immediate"), 4)]
        with self.assertRaises(extractor.InvalidObject):
            extractor.extract(coff(self.model))

    def test_unwind_metadata(self):
        self.model["sections"].append({"name": ".pdata", "code": b"\0" * 12,
                                       "relocations": [(0, 0, 3)]})
        self.rejected("unwind metadata")

    def test_unwind_metadata_on_any_stencil(self):
        for target in (1, self.model["functions"].index("mu_stencil_exit")):
            with self.subTest(target=target):
                self.setUp()
                self.model["sections"].append({"name": ".pdata", "code": b"\0" * 12,
                                               "relocations": [(0, target, 3)]})
                self.rejected("unwind metadata or stack frame: " + self.model["functions"][target])

    def test_duplicate_stencil(self):
        self.model["functions"][1] = self.model["functions"][0]
        self.rejected("duplicate stencil")

    def test_bad_relocation_symbol_index(self):
        offset, _, kind = self.section("Add")["relocations"][0]
        self.section("Add")["relocations"][0] = (offset, 999999, kind)
        self.rejected("unexpected stencil dependency")

    def test_content_hash_tracks_code_and_holes(self):
        original = extractor.extract(coff(self.model))["sha256"]
        offset, target, _ = self.section("Add")["relocations"][0]
        self.section("Add")["relocations"][0] = (offset, target, 5)
        self.assertNotEqual(original, extractor.extract(coff(self.model))["sha256"])

    # ---- call-capable stencils: unwind data, host symbols, constants ----
    def add_symbol(self, name):
        self.model["dependencies"].append(name)
        return len(self.model["functions"]) + len(self.model["dependencies"]) - 1

    def add_defined(self, name, value, section_number):
        # Defined symbols follow every undefined one, so add all host symbols first.
        self.model.setdefault("defined", []).append((name, value, section_number))
        return len(self.model["functions"]) + len(self.model["dependencies"]) + len(self.model["defined"]) - 1

    def add_section(self, name, code, relocations=(), flags=0x40300040):
        self.model["sections"].append({"name": name, "code": code, "relocations": list(relocations), "flags": flags})
        return len(self.model["sections"])  # 1-based section number

    def add_unwind(self, operation, info, begin=0, end=None, extra=()):
        """A .pdata record and its .xdata for one stencil."""
        function = self.names.index(operation)
        size = len(self.section(operation)["code"]) if end is None else end
        xdata_number = self.add_section(".xdata", info, extra)
        unwind_symbol = self.add_defined("$unwind$%d" % xdata_number, 0, xdata_number)
        self.add_section(".pdata", struct.pack("<III", begin, size, 0),
                         [(0, function, 3), (4, function, 3), (8, unwind_symbol, 3)])
        return unwind_symbol

    PLAIN_UNWIND = bytes([1, 4, 1, 0, 4, 0x42, 0, 0])  # version 1, prolog 4, one code: sub rsp,28h

    def test_call_capable_stencil_keeps_its_unwind_data(self):
        self.add_unwind("Stw", self.PLAIN_UNWIND)
        table = extractor.extract(coff(self.model))
        item = table["stencils"][self.names.index("Stw")]
        size = len(self.section("Stw")["code"])
        self.assertEqual(item["unwind"], [{"begin": 0, "end": size, "info": self.PLAIN_UNWIND.hex(), "parent": -1}])
        header = extractor.render_header(table)
        self.assertIn("UnwindRecord unwind_Stw[]", header)
        self.assertIn("unwind_Stw, 1}", header)
        self.assertNotIn("unwind", table["stencils"][self.names.index("Add")])

    def test_unwind_data_changes_the_hash(self):
        original = extractor.extract(coff(self.model))["sha256"]
        self.add_unwind("Stw", self.PLAIN_UNWIND)
        self.assertNotEqual(original, extractor.extract(coff(self.model))["sha256"])

    def test_unsupported_unwind_data_is_rejected(self):
        for info, message in (
                (bytes([2, 4, 1, 0, 4, 0x42, 0, 0]), "version or flags"),             # version 2
                (bytes([1 | (1 << 3), 4, 1, 0, 4, 0x42, 0, 0]), "version or flags"),  # exception handler
                (bytes([1 | (2 << 3), 4, 1, 0, 4, 0x42, 0, 0]), "version or flags"),  # termination handler
                (bytes([1, 4, 1, 5, 4, 0x42, 0, 0]), "frame register"),
                (bytes([1, 4, 1, 0, 4, 0x03, 0, 0]), "unwind code"),                  # UWOP_SET_FPREG
                (bytes([1, 4, 1, 0, 4, 0x0A, 0, 0]), "unwind code"),                  # UWOP_PUSH_MACHFRAME
                (bytes([1, 4, 1, 0, 9, 0x42, 0, 0]), "unwind code"),                  # a code after the prolog
                (bytes([1, 200, 1, 0, 4, 0x42, 0, 0]), "prolog longer"),
                (bytes([1, 4, 3, 0, 4, 0x42, 0, 0]), "truncated"),
                (bytes([1, 4, 2, 0, 4, 0x42, 4, 0x01]), "truncated unwind codes"),    # ALLOC_LARGE cut short
                (bytes([1, 4]), "truncated"),
        ):
            with self.subTest(info=info):
                self.setUp()
                self.add_unwind("Stw", info)
                self.rejected(message)

    def test_unwind_record_must_cover_the_stencil(self):
        size = len(self.section("Stw")["code"])
        for begin, end in ((1, size), (0, size - 1), (0, size + 4)):
            with self.subTest(begin=begin, end=end):
                self.setUp()
                self.add_unwind("Stw", self.PLAIN_UNWIND, begin, end)
                self.rejected("does not cover the whole stencil")

    def test_unwind_data_with_a_handler_relocation_is_rejected(self):
        handler = self.add_symbol("__CxxFrameHandler4")
        self.add_unwind("Stw", self.PLAIN_UNWIND + b"\0" * 4, extra=[(4, handler, 3)])
        self.rejected("relocation")

    def test_unwind_data_must_live_in_xdata(self):
        function = self.names.index("Stw")
        size = len(self.section("Stw")["code"])
        number = self.add_section(".rdata", self.PLAIN_UNWIND)
        symbol = self.add_defined("$unwind$x", 0, number)
        self.add_section(".pdata", struct.pack("<III", 0, size, 0), [(0, function, 3), (4, function, 3), (8, symbol, 3)])
        self.rejected("not in .xdata")

    def chained(self, parent_begin=0, parent_end=None):
        """Two records for Stmw: [0, split) plain and [split, size) chained to the first."""
        function = self.names.index("Stmw")
        size = len(self.section("Stmw")["code"])
        split = size // 2
        first_unwind = self.add_unwind("Stmw", self.PLAIN_UNWIND, 0, split)
        chained = bytes([1 | (4 << 3), 0, 0, 0]) + struct.pack(
            "<III", parent_begin, split if parent_end is None else parent_end, 0)
        number = self.add_section(".xdata", chained, [(4, function, 3), (8, function, 3), (12, first_unwind, 3)])
        symbol = self.add_defined("$chain$%d" % number, 0, number)
        self.add_section(".pdata", struct.pack("<III", split, size, 0), [(0, function, 3), (4, function, 3), (8, symbol, 3)])
        return split, size

    def test_chained_unwind_records_are_kept_in_order(self):
        split, size = self.chained()
        table = extractor.extract(coff(self.model))
        records = table["stencils"][self.names.index("Stmw")]["unwind"]
        self.assertEqual([(r["begin"], r["end"], r["parent"]) for r in records], [(0, split, -1), (split, size, 0)])
        self.assertEqual(records[1]["info"], (bytes([1 | (4 << 3), 0, 0, 0]) + b"\0" * 12).hex())
        self.assertIn("unwind_Stmw, 2}", extractor.render_header(table))

    def test_chained_unwind_record_needs_its_parent(self):
        self.chained(parent_begin=4)
        self.rejected("without its parent")

    def test_unwind_records_must_not_leave_a_gap(self):
        function = self.names.index("Stmw")
        size = len(self.section("Stmw")["code"])
        self.add_unwind("Stmw", self.PLAIN_UNWIND, 0, 8)
        number = self.add_section(".xdata", self.PLAIN_UNWIND)
        symbol = self.add_defined("$unwind$b", 0, number)
        self.add_section(".pdata", struct.pack("<III", 12, size, 0), [(0, function, 3), (4, function, 3), (8, symbol, 3)])
        self.rejected("does not cover the whole stencil")

    def test_chained_unwind_record_without_its_relocations_is_rejected(self):
        function = self.names.index("Stmw")
        size = len(self.section("Stmw")["code"])
        self.add_unwind("Stmw", self.PLAIN_UNWIND, 0, 8)
        number = self.add_section(".xdata", bytes([1 | (4 << 3), 0, 0, 0]) + b"\0" * 12)
        symbol = self.add_defined("$chain$b", 0, number)
        self.add_section(".pdata", struct.pack("<III", 8, size, 0), [(0, function, 3), (4, function, 3), (8, symbol, 3)])
        self.rejected("chained unwind relocations")

    def refer(self, operation, symbol, kind=4, prefix=b"\xE8", addend=0):
        """Prepends a reference to a symbol to a stencil."""
        section = self.section(operation)
        shift = len(prefix) + 4
        section["relocations"] = [(len(prefix), symbol, kind)] + [
            (offset + shift, target, k) for offset, target, k in section["relocations"]]
        section["code"] = prefix + struct.pack("<i", addend) + section["code"]

    MMIO_WRITE = "?mmio_write@ppc@@YAXAEAUContext@1@IIH@Z"

    def test_listed_host_call_in_a_call_capable_stencil(self):
        self.refer("Stw", self.add_symbol(self.MMIO_WRITE))
        table = extractor.extract(coff(self.model))
        self.assertEqual(table["externals"], [{"name": self.MMIO_WRITE, "kind": "Direct"}])
        first = table["stencils"][self.names.index("Stw")]["relocations"][0]
        self.assertEqual((first["hole"], first["index"], first["offset"], first["bias"]), ("External", 0, 1, 0))
        header = extractor.render_header(table)
        self.assertIn("reinterpret_cast<const void*>(&ppc::mmio_write), ExternalKind::Direct", header)
        self.assertIn("externals, 1", header)

    def test_host_symbols_are_numbered_once(self):
        write = self.add_symbol(self.MMIO_WRITE)
        self.refer("Stw", write)
        self.refer("Stwu", write)
        self.refer("Lwz", self.add_symbol("?mmio_read@ppc@@YAIAEAUContext@1@IH@Z"))
        table = extractor.extract(coff(self.model))
        self.assertEqual(sorted(item["name"].split("@")[0] for item in table["externals"]), ["?mmio_read", "?mmio_write"])
        indices = {table["stencils"][self.names.index(name)]["relocations"][0]["index"] for name in ("Stw", "Stwu")}
        self.assertEqual(len(indices), 1)

    def test_unlisted_host_call_in_a_call_capable_stencil_is_rejected(self):
        self.refer("Stw", self.add_symbol("?fatal@ppc@@YAXAEAUContext@1@PEBDI@Z"))
        self.rejected(r"unexpected stencil dependency in mu_stencil_stw: \?fatal")

    def test_listed_host_call_in_a_leaf_stencil_is_rejected(self):
        for operation in ("Add", "Fadd", "Backedge"):
            with self.subTest(operation=operation):
                self.setUp()
                self.refer(operation, self.add_symbol("?call@ppc@@YAXAEAUContext@1@PEAEI@Z"))
                self.rejected("unexpected stencil dependency")

    def test_host_call_with_an_addend_is_rejected(self):
        self.refer("Stw", self.add_symbol(self.MMIO_WRITE), addend=8)
        self.rejected("with an addend")

    def test_image_relative_host_data(self):
        versions = self.add_symbol("?g_ram_versions@ppc@@3PAU?$atomic@I@std@@A")
        image = self.add_symbol("__ImageBase")
        self.refer("Stw", versions, kind=3, prefix=b"\x8B\x84\x81", addend=4)
        self.refer("Stw", image, prefix=b"\x4C\x8D\x0D")
        table = extractor.extract(coff(self.model))
        holes = table["stencils"][self.names.index("Stw")]["relocations"][:2]
        self.assertEqual((holes[0]["hole"], holes[0]["addend"]), ("External", 0))
        self.assertEqual((holes[1]["hole"], holes[1]["addend"], holes[1]["bias"]), ("ExternalRva", 4, 0))
        self.assertEqual([item["kind"] for item in table["externals"]], ["ImageBase", "Direct"])
        # The addend lives in the table, not in the copied bytes.
        code = bytes.fromhex(table["stencils"][self.names.index("Stw")]["code"])
        self.assertEqual(code[holes[1]["offset"]:holes[1]["offset"] + 4], b"\0\0\0\0")
        self.assertIn('{"__ImageBase", nullptr, ExternalKind::ImageBase}', extractor.render_header(table))

    def test_image_relative_reference_to_the_image_base_or_a_slot_is_rejected(self):
        for name in ("__ImageBase", "__imp_trunc"):
            with self.subTest(name=name):
                self.setUp()
                self.refer("Stw", self.add_symbol(name), kind=3, prefix=b"\x8B\x84\x81")
                self.rejected("image-relative reference")

    def test_absolute_relocation_to_a_host_symbol_is_rejected(self):
        self.refer("Stw", self.add_symbol(self.MMIO_WRITE), kind=1)
        self.rejected("unsupported relocation")

    def test_import_pointer_becomes_a_slot(self):
        self.refer("Fctiwz", self.add_symbol("__imp_trunc"), prefix=b"\xFF\x15")
        table = extractor.extract(coff(self.model))
        self.assertEqual(table["externals"], [{"name": "__imp_trunc", "kind": "Slot"}])
        self.assertIn("ExternalKind::Slot", extractor.render_header(table))

    def constant(self, operation, data, flags=0x40400040, name=".rdata", relocations=(), addend=0):
        number = self.add_section(name, data, relocations, flags)
        self.refer(operation, self.add_defined("__real@%d" % number, 0, number), prefix=b"\xF2\x0F\x10\x05", addend=addend)

    def test_compiler_constant_travels_with_the_table(self):
        self.constant("Fctiwz", struct.pack("<d", 2147483647.0))
        table = extractor.extract(coff(self.model))
        self.assertEqual(table["constants"], [{"bytes": struct.pack("<d", 2147483647.0).hex(), "alignment": 8}])
        first = table["stencils"][self.names.index("Fctiwz")]["relocations"][0]
        self.assertEqual((first["hole"], first["index"], first["addend"]), ("Constant", 0, 0))
        header = extractor.render_header(table)
        self.assertIn("Constant constants[]", header)
        self.assertIn("constants, 1", header)

    def test_equal_constants_are_stored_once(self):
        self.constant("Fctiwz", struct.pack("<d", 1.0))
        self.constant("Fctiw", struct.pack("<d", 1.0))
        self.constant("Fsel", struct.pack("<d", -0.0))
        self.assertEqual(len(extractor.extract(coff(self.model))["constants"]), 2)

    def test_constant_offset_is_kept_and_bounded(self):
        self.constant("Fctiwz", b"\0" * 16, addend=8)
        table = extractor.extract(coff(self.model))
        self.assertEqual(table["stencils"][self.names.index("Fctiwz")]["relocations"][0]["addend"], 8)
        self.setUp()
        self.constant("Fctiwz", b"\0" * 16, addend=16)
        self.rejected("outside its section")

    def test_data_that_is_not_a_constant_is_rejected(self):
        for kwargs in ({"flags": 0xC0400040},                       # writable
                       {"flags": 0x60400020},                       # code
                       {"name": ".data"},
                       {"relocations": [(0, 0, 1)]},                # a constant holding an address
                       {"data": b"\0" * 80}):                       # too large to be a constant
            with self.subTest(kwargs=kwargs):
                self.setUp()
                kwargs = dict(kwargs)
                self.constant("Fctiwz", kwargs.pop("data", b"\0" * 8), **kwargs)
                self.rejected("unexpected stencil dependency in mu_stencil_fctiwz")

    def test_constant_in_a_leaf_stencil_is_rejected(self):
        self.constant("Fadd", struct.pack("<d", 1.0))
        self.rejected("unexpected stencil dependency in mu_stencil_fadd")

    def test_call_capable_rows_are_the_same_in_the_runtime_header(self):
        text = (ROOT / "port/runtime/ppc/stencil_format.h").read_text(encoding="utf-8")
        body = text.split("#define MU_STENCIL_OPERATIONS(X)", 1)[1].split("enum class Operation", 1)[0]
        marked = {name for name, letters in re.findall(r"X\((\w+),\s*([A-Z0-9|]+)\)", body) if "C" in letters.split("|")}
        self.assertEqual(marked, extractor.MAY_CALL)
        self.assertNotIn("Add", marked)
        self.assertIn("Stw", marked)


if __name__ == "__main__":
    unittest.main()
