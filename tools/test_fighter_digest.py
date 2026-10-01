"""Synthetic dump/schema gates, without game processes or real debug artifacts."""
import contextlib
import copy
import io
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import fighter_digest as digest


def schema():
    return dict(format=digest.FORMAT, version=1, mode="core", scope="three synthetic scalars only",
                provenance=dict(native_debug_sha256="a" * 64, native_field_map_sha256="b" * 64),
                record_sizes=dict(console=24, native=32),
                kind_offsets=dict(console=0, native=0), player_offsets=dict(console=4, native=4),
                fields=[dict(path="fp.player_idx", type="u8", console_offset=4, native_offset=4),
                        dict(path="fp.motion_id", type="u32", console_offset=8, native_offset=8),
                        dict(path="fp.cur_pos.x", type="f32", console_offset=12, native_offset=16)],
                exclusions=dict(pointers="different address identities", padding="not typed state",
                                unions="need active kind schemas"))


def payload(spec, layout, port=0, kind=0, motion=0x12345678, float_bits=0x7FA00001,
            endian=None):
    data = bytearray(spec["record_sizes"][layout])
    order = endian or ("big" if layout == "console" else "little")
    values = {"fp.player_idx": port, "fp.motion_id": motion, "fp.cur_pos.x": float_bits}
    for field in spec["fields"]:
        value = values.get(field["path"], 0)
        offset, width = field[layout + "_offset"], digest.WIDTHS[field["type"]]
        data[offset:offset + width] = value.to_bytes(width, order)
    offset = spec["kind_offsets"][layout]
    data[offset:offset + 4] = kind.to_bytes(4, order)
    return bytes(data)


def record(data, frame=0, port=0, follower=0, size=None):
    return struct.pack("<iBBH", frame, port, follower, len(data) if size is None else size) + data


class DigestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.spec = schema()
        self.console = self.root / "console.bin"
        self.native = self.root / "native.bin"
        self.expected = {(0, 0, 0)}
        self.console.write_bytes(record(payload(self.spec, "console")))
        self.native.write_bytes(record(payload(self.spec, "native")))

    def read(self, path=None, layout="console", extensions=()):
        return digest.read_records(path or self.console, layout, self.spec, extensions)

    def compare(self):
        return digest.compare_records(self.read(), self.read(self.native, "native"),
                                      self.expected, "c" * 64)

    def test_exact_declared_endian_normalization_preserves_nan_bits(self):
        result = self.compare()
        self.assertTrue(result["gate_pass"])
        self.assertEqual(result["per_frame_digests"][0]["console"],
                         result["per_frame_digests"][0]["native"])
        self.assertEqual(self.read()[(0, 0, 0)]["core:fp.cur_pos.x"].hex(), "7fa00001")

    def test_opposite_endian_is_not_permissively_accepted(self):
        self.native.write_bytes(record(payload(self.spec, "native", endian="big")))
        result = self.compare()
        self.assertFalse(result["gate_pass"])
        self.assertEqual(result["first_difference"]["field"], "core:fp.motion_id")

    def test_float_signed_zero_and_nan_payload_differences_fail(self):
        for left, right in ((0x00000000, 0x80000000), (0x7FA00001, 0x7FE00001)):
            with self.subTest(left=left, right=right):
                self.console.write_bytes(record(payload(self.spec, "console", float_bits=left)))
                self.native.write_bytes(record(payload(self.spec, "native", float_bits=right)))
                self.assertFalse(self.compare()["gate_pass"])

    def test_truncated_header_and_payload_fail(self):
        for raw in (b"\x00", record(payload(self.spec, "console"))[:-1]):
            self.console.write_bytes(raw)
            with self.assertRaises(ValueError):
                self.read()

    def test_wrong_declared_size_and_duplicate_key_fail(self):
        for raw in (record(payload(self.spec, "console"), size=23),
                    record(payload(self.spec, "console")) * 2):
            self.console.write_bytes(raw)
            with self.assertRaises(ValueError):
                self.read()

    def test_unknown_port_follower_and_header_identity_fail(self):
        for raw in (record(payload(self.spec, "console"), port=4),
                    record(payload(self.spec, "console"), follower=2),
                    record(payload(self.spec, "console", port=1), port=0)):
            self.console.write_bytes(raw)
            with self.assertRaises(ValueError):
                self.read()

    def test_empty_and_no_frame_zero_fail(self):
        for raw in (b"", record(payload(self.spec, "console"), frame=1)):
            self.console.write_bytes(raw)
            with self.assertRaises(ValueError):
                self.read()

    def test_missing_and_unknown_expected_keys_fail(self):
        console, native = self.read(), self.read(self.native, "native")
        for expected in (set(), {(1, 0, 0)}, self.expected | {(1, 0, 0)},
                         self.expected | {(0, 0, 1)}):
            with self.subTest(expected=expected), self.assertRaises(ValueError):
                digest.compare_records(console, native, expected, "c" * 64)
        native[(0, 1, 0)] = copy.deepcopy(native[(0, 0, 0)])
        with self.assertRaises(ValueError):
            digest.compare_records(console, native, self.expected, "c" * 64)

    def test_known_bone_companion_is_structural_only(self):
        self.console.write_bytes(self.console.read_bytes() + record(bytes(88), port=0x80))
        self.assertEqual(len(self.read()), 1)
        self.assertTrue(self.compare()["gate_pass"])

    def test_orphan_duplicate_and_wrong_size_bones_fail(self):
        fighter = record(payload(self.spec, "console"))
        bone = record(bytes(88), port=0x80)
        for raw in (bone + fighter, fighter + bone * 2,
                    fighter + record(bytes(87), port=0x80)):
            self.console.write_bytes(raw)
            with self.assertRaises(ValueError):
                self.read()

    def test_pointer_padding_and_union_bytes_are_not_compared(self):
        raw = bytearray(self.native.read_bytes())
        raw[8 + 24:8 + 32] = b"changed!"
        self.native.write_bytes(raw)
        self.assertTrue(self.compare()["gate_pass"])

    def test_expected_replay_interval_requires_complete_frame_zero_coverage(self):
        posts = {0: {(0, 0): {}}, 1: {(0, 0): {}, (0, 1): {}}}
        with mock.patch.object(digest, "parse_slp", return_value=(None, posts, {})):
            self.assertEqual(digest.expected_keys("unused"),
                             {(0, 0, 0), (1, 0, 0), (1, 0, 1)})
            for first, last in ((1, 1), (0, 2), (-1, 1), (0, -1)):
                with self.assertRaises(ValueError):
                    digest.expected_keys("unused", first, last)

    def extension(self):
        extension = copy.deepcopy(self.spec)
        extension.update(mode="active_variables", name="synthetic-fighter", scope="one active variable",
                         kinds=dict(console=[27], native=[34]),
                         fields=[dict(path="fp.u.fx.variable", type="u32", console_offset=16,
                                      native_offset=24)])
        return extension

    def test_explicit_active_kind_extension_hook(self):
        extension = self.extension()
        digest.validate_extensions(self.spec, [extension])
        for layout, path, kind in (("console", self.console, 27), ("native", self.native, 34)):
            raw = bytearray(payload(self.spec, layout, kind=kind))
            offset = extension["fields"][0][layout + "_offset"]
            raw[offset:offset + 4] = (17).to_bytes(4, "big" if layout == "console" else "little")
            path.write_bytes(record(raw))
        console = self.read(extensions=[extension])
        native = self.read(self.native, "native", [extension])
        self.assertTrue(digest.compare_records(console, native, self.expected, "c" * 64)["gate_pass"])
        native[(0, 0, 0)]["synthetic-fighter:fp.u.fx.variable"] = b"\x00\x00\x00\x12"
        self.assertFalse(digest.compare_records(console, native, self.expected, "c" * 64)["gate_pass"])

    def test_mismatched_active_extension_selection_fails(self):
        extension = self.extension()
        self.console.write_bytes(record(payload(self.spec, "console", kind=27)))
        result = digest.compare_records(self.read(extensions=[extension]),
            self.read(self.native, "native", [extension]), self.expected, "c" * 64)
        self.assertFalse(result["gate_pass"])
        self.assertEqual(result["first_difference"]["field"], "active_schema_fields")


class SchemaTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.spec = schema()
        self.debug = self.root / "current.dbg"
        self.debug.write_bytes(b"synthetic debugger artifact")
        self.mapping = self.root / "map.json"
        self.mapping.write_text(json.dumps(dict(native_size=32, fields=[
            dict(path="fp.kind", kind="int", noff=0, nsize=4),
            dict(path="fp.player_idx", kind="int", noff=4, nsize=1),
            dict(path="fp.motion_id", kind="int", noff=8, nsize=4),
            dict(path="fp.cur_pos.x", kind="flt", noff=16, nsize=4)])))
        self.spec["provenance"] = dict(native_debug_sha256=digest.sha256_file(self.debug),
            native_field_map_sha256=digest.sha256_file(self.mapping))
        self.path = self.root / "schema.json"
        self.write()

    def write(self):
        self.path.write_text(json.dumps(self.spec))

    def load(self):
        return digest.load_schema(self.path, self.debug, self.mapping)

    def test_debug_and_map_content_binding(self):
        self.assertEqual(self.load(), self.spec)
        self.debug.write_bytes(b"another debugger artifact")
        with self.assertRaisesRegex(ValueError, "native debug"):
            self.load()
        self.debug.write_bytes(b"synthetic debugger artifact")
        self.mapping.write_text("{}")
        with self.assertRaisesRegex(ValueError, "field-map"):
            self.load()

    def test_native_offset_and_scalar_type_need_debugger_proof(self):
        for field, value in (("native_offset", 12), ("type", "u32")):
            with self.subTest(field=field):
                self.spec["fields"][2][field] = value
                self.write()
                with self.assertRaises(ValueError):
                    self.load()
                self.spec["fields"][2].update(native_offset=16, type="f32")

    def test_bad_width_overlap_union_and_missing_provenance_fail(self):
        for mutate in (lambda s: s["fields"][0].update(type="ptr"),
                       lambda s: s["fields"][1].update(console_offset=4),
                       lambda s: s["fields"][2].update(path="fp.u.fx.variable"),
                       lambda s: s["fields"][2].update(native_offset=32),
                       lambda s: s["provenance"].pop("native_debug_sha256")):
            altered = copy.deepcopy(self.spec)
            mutate(altered)
            with self.assertRaises(ValueError):
                digest.validate_schema(altered)

    def test_core_schema_size_is_bound_to_current_native_map(self):
        self.spec["record_sizes"]["native"] = 33
        self.write()
        with self.assertRaisesRegex(ValueError, "record size"):
            self.load()

    def test_malformed_schema_shapes_fail_validation(self):
        for field, value in (("fields", [1]), ("record_sizes", 1),
                             ("provenance", None), ("kind_offsets", [])):
            with self.subTest(field=field):
                altered = copy.deepcopy(self.spec)
                altered[field] = value
                with self.assertRaises(ValueError):
                    digest.validate_schema(altered)

    def test_cli_pass_mismatch_and_provenance_error_write_evidence(self):
        console, native = self.root / "console.bin", self.root / "native.bin"
        console.write_bytes(record(payload(self.spec, "console")))
        native.write_bytes(record(payload(self.spec, "native")))
        expected = self.root / "expected.slp"
        expected.write_bytes(b"synthetic expected-replay identity")
        out = self.root / "result.json"
        opts = [str(console), str(native), "--schema", str(self.path),
                "--native-debug", str(self.debug), "--field-map", str(self.mapping),
                "--expected-replay", str(expected), "--last", "0", "--out", str(out)]
        with mock.patch.object(digest, "parse_slp", return_value=(None, {0: {(0, 0): {}}}, {})), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(digest.main(opts), 0)
            result = json.loads(out.read_text())
            self.assertTrue(result["gate_pass"])
            self.assertEqual(result["provenance"], self.spec["provenance"])
            self.assertEqual(result["player_frames"], 1)
            native.write_bytes(record(payload(self.spec, "native", motion=15)))
            self.assertEqual(digest.main(opts), 1)
            self.assertFalse(json.loads(out.read_text())["gate_pass"])
            self.debug.write_bytes(b"new debug")
            self.assertEqual(digest.main(opts), 2)
            self.assertEqual(json.loads(out.read_text())["status"], "error")

    def test_cli_cannot_overwrite_an_input_artifact(self):
        opts = [str(self.debug), str(self.mapping), "--schema", str(self.path),
                "--native-debug", str(self.debug), "--field-map", str(self.mapping),
                "--expected-replay", str(self.debug), "--out", str(self.debug)]
        before = self.debug.read_bytes()
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            digest.main(opts)
        self.assertEqual(self.debug.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
