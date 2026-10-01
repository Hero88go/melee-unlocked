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
                                   len(section["relocations"]), 0, 0x60000020))
    symbols = bytearray()
    for i, name in enumerate(model["functions"]):
        symbols.extend(symbol_name(name) + struct.pack("<IhHBB", 0, i + 1, 0x20, 2, 0))
    for name in model["dependencies"]:
        symbols.extend(symbol_name(name) + struct.pack("<IhHBB", 0, 0, 0, 2, 0))
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
        for target in (1, len(self.model["functions"]) - 1):
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


if __name__ == "__main__":
    unittest.main()
