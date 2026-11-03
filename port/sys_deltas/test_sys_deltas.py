"""Synthetic GCM tests: no retail disc or retail data is used."""
import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_sys_deltas import check
from make_sys_deltas import close_sources, select_sources
from sys_delta_common import (BUILD_SYS, Disc, FORMAT, SYS, TARGETS, blocks, compliance,
                              load_manifest, sha1, source_concat, validate_manifest,
                              vcdiff_stats, scan_disc)


def varint(value):
    encoded = [value & 127]
    value >>= 7
    while value:
        encoded.append(128 | (value & 127))
        value >>= 7
    return bytes(reversed(encoded))


def delta_for(source, extra):
    instructions = b"\x13" + varint(len(source)) + b"\x01" + varint(len(extra))
    window = (varint(len(source) + len(extra)) + b"\0" + varint(len(extra))
              + varint(len(instructions)) + b"\x01" + extra + instructions + b"\0")
    return b"\xd6\xc3\xc4\0\0\x01" + varint(len(source)) + b"\0" + varint(len(window)) + window


def synthetic_gcm(path, third=None):
    # Locally synthesized pseudorandom bytes; the names only resemble disc names.
    one = b"".join(hashlib.sha256(str(i).encode()).digest() for i in range(128))
    two = b"".join(hashlib.sha256(('two' + str(i)).encode()).digest() for i in range(256))
    strings = b"MnOne.dat\0menu\0MnTwo.dat\0"
    entries = [(0x01000000, 0, 4 if third is None else 5), (0, 0x1000, len(one)),
               (0x01000000 | 10, 0, 4), (15, 0x2000, len(two))]
    if third is not None:
        entries.append((len(strings), 0x4000, len(third)))
        strings += b"assets.bin\0"
    fst = b"".join(struct.pack(">III", *entry) for entry in entries) + strings
    image = bytearray(0x2000 + len(two) + (len(third) if third is not None else 0))
    image[:8] = b"GALE01\0\x02"
    image[0x1C:0x20] = b"\xc2\x33\x9f\x3d"
    struct.pack_into(">II", image, 0x424, 0x500, len(fst))
    image[0x500:0x500 + len(fst)] = fst
    image[0x1000:0x1000 + len(one)] = one
    image[0x2000:0x2000 + len(two)] = two
    if third is not None:
        image[0x4000:0x4000 + len(third)] = third
    path.write_bytes(image)
    return one, two


class SyntheticTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="test-sys-deltas-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.iso = self.root / "tiny.iso"
        self.one, self.two = synthetic_gcm(self.iso)
        self.disc = Disc(self.iso)
        sources = [{"disc": name, "sha1": sha1(data), "size": len(data)} for name, data in
                   [("MnOne.dat", self.one), ("menu/MnTwo.dat", self.two)]]
        self.manifest = {"format": FORMAT, "disc": {"id": "GALE01", "revision": 2}, "files": []}
        for name in TARGETS:
            extra = ("original synthetic " + name).encode()
            source = self.one + self.two
            delta = delta_for(source, extra)
            entry = {"target": name, "sys": BUILD_SYS[:], "target_sha1": sha1(source + extra),
                     "target_size": len(source + extra), "sources": copy.deepcopy(sources),
                     "delta": name + ".vcdiff", "delta_sha1": sha1(delta), "delta_size": len(delta)}
            self.manifest["files"].append(entry)
            (self.root / entry["delta"]).write_bytes(delta)
        self.save_manifest()

    def save_manifest(self):
        (self.root / "manifest.json").write_text(json.dumps(self.manifest))

    def run_check(self):
        self.save_manifest()
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = check(self.iso, self.root)
        return result, output.getvalue()

    def mutate_iso(self, offset, data):
        image = bytearray(self.iso.read_bytes())
        image[offset:offset + len(data)] = data
        self.iso.write_bytes(image)

    def test_fst_nested_paths_and_contents(self):
        self.assertEqual((self.disc.id, self.disc.revision), ("GALE01", 2))
        self.assertEqual(set(self.disc.files), {"MnOne.dat", "menu/MnTwo.dat"})
        self.assertEqual(b"".join(self.disc.chunks("menu/MnTwo.dat")), self.two)

    def test_fst_rejects_wrong_magic(self):
        self.mutate_iso(0x1C, b"NOPE")
        with self.assertRaisesRegex(ValueError, "GCM"):
            Disc(self.iso)

    def test_fst_rejects_out_of_bounds_table(self):
        self.mutate_iso(0x424, struct.pack(">II", 0xFFFFFF00, 1000))
        with self.assertRaisesRegex(ValueError, "FST outside"):
            Disc(self.iso)

    def test_fst_rejects_out_of_bounds_file(self):
        self.mutate_iso(0x500 + 12 + 4, struct.pack(">I", 0xFFFFFF00))
        with self.assertRaisesRegex(ValueError, "file outside"):
            Disc(self.iso)

    def test_fst_rejects_invalid_directory_parent(self):
        self.mutate_iso(0x500 + 24 + 4, struct.pack(">I", 1))
        with self.assertRaisesRegex(ValueError, "directory"):
            Disc(self.iso)

    def test_fst_rejects_name_offset(self):
        self.mutate_iso(0x500 + 12, struct.pack(">I", 0xFFFFFF))
        with self.assertRaisesRegex(ValueError, "name outside"):
            Disc(self.iso)

    def test_block_scan_includes_last_and_crosses_chunks(self):
        self.assertEqual(list(self.disc.blocks("MnOne.dat")),
                         [self.one[i:i + 64] for i in range(0, len(self.one) - 63, 32)])
        with mock.patch.object(self.disc, "chunks", return_value=iter([self.one[:32], self.one[32:80], self.one[80:]])):
            self.assertEqual(list(self.disc.blocks("MnOne.dat")),
                             [self.one[i:i + 64] for i in range(0, len(self.one) - 63, 32)])
        self.assertEqual(list(blocks(b"x" * 64)), [])
        self.assertEqual(list(blocks(self.one[:64])), [(0, self.one[:64])])

    def test_selection_orders_by_marginal_coverage_and_64_byte_threshold(self):
        with contextlib.redirect_stdout(io.StringIO()):
            result = select_sources(self.disc, {"example": self.one + self.two})
        self.assertEqual([s["disc"] for s in result["example"]], ["menu/MnTwo.dat", "MnOne.dat"])
        with contextlib.redirect_stdout(io.StringIO()):
            tiny = select_sources(self.disc, {"tiny": self.one[:64]})
        self.assertEqual([s["disc"] for s in tiny["tiny"]], ["MnOne.dat"])
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, "64"):
            select_sources(self.disc, {"tiny": self.one[:63]})

    def test_selection_ties_by_name_and_ignores_redundant_source(self):
        original = self.disc.blocks
        with mock.patch.object(self.disc, "blocks", side_effect=lambda name: original("MnOne.dat")):
            with contextlib.redirect_stdout(io.StringIO()):
                result = select_sources(self.disc, {"example": self.one})
        self.assertEqual([s["disc"] for s in result["example"]], ["MnOne.dat"])

    def test_valid_manifest_and_two_real_decodes(self):
        self.assertEqual(load_manifest(self.root), self.manifest)
        result, output = self.run_check()
        self.assertEqual(result, 0, output)
        self.assertEqual(output.count(": PASS"), 2)

    def test_wrong_source_sha1_fails_closed_without_decoder(self):
        self.manifest["files"][0]["sources"][0]["sha1"] = "0" * 40
        self.manifest["files"][1]["sources"][0]["sha1"] = "0" * 40
        with mock.patch("check_sys_deltas.require_xdelta"), mock.patch(
                "sys_delta_common.run_xdelta", side_effect=AssertionError("decoder must not run")):
            result, output = self.run_check()
        self.assertEqual(result, 1)
        self.assertIn("source SHA-1 mismatch: MnOne.dat", output)
        self.assertEqual(output.count(": FAIL"), 2)

    def test_wrong_source_size_and_missing_source(self):
        for field, value, message in [("size", 1, "source size mismatch"), ("disc", "missing.dat", "missing source")]:
            with self.subTest(field=field):
                original = copy.deepcopy(self.manifest)
                self.manifest["files"][0]["sources"][0][field] = value
                result, output = self.run_check()
                self.assertEqual(result, 1)
                self.assertIn(message, output)
                self.manifest = original

    def test_source_order_is_significant(self):
        self.manifest["files"][0]["sources"].reverse()
        result, output = self.run_check()
        self.assertEqual(result, 1)
        self.assertIn("target size/SHA-1 mismatch", output)

    def test_delta_and_target_corruption_fail(self):
        entry = self.manifest["files"][0]
        delta = self.root / entry["delta"]
        original = delta.read_bytes()
        delta.write_bytes(original + b"x")
        result, output = self.run_check()
        self.assertEqual(result, 1)
        self.assertIn("delta size/SHA-1 mismatch", output)
        delta.write_bytes(original)
        entry["target_sha1"] = "0" * 40
        result, output = self.run_check()
        self.assertEqual(result, 1)
        self.assertIn("target size/SHA-1 mismatch", output)

    def test_wrong_disc_revision(self):
        self.mutate_iso(7, b"\x01")
        result, output = self.run_check()
        self.assertEqual(result, 1)
        self.assertIn("identity/revision mismatch", output)

    def test_manifest_rejects_unsafe_paths_and_duplicate_targets(self):
        for mutate in [lambda m: m["files"][0].update(delta="../evil"),
                       lambda m: m["files"][0]["sources"][0].update(disc="../evil"),
                       lambda m: m["files"].__setitem__(1, m["files"][0]),
                       lambda m: m.update(format="secondary"),
                       lambda m: m["files"][0].update(target_size=True)]:
            manifest = copy.deepcopy(self.manifest)
            mutate(manifest)
            with self.assertRaises(ValueError):
                validate_manifest(manifest)

    def test_manifest_duplicate_json_keys_rejected(self):
        (self.root / "manifest.json").write_text('{"format": "x", "format": "y"}')
        with self.assertRaisesRegex(ValueError, "duplicate JSON key"):
            load_manifest(self.root)

    def test_source_concat_checks_hash_and_preserves_order(self):
        sources = self.manifest["files"][0]["sources"]
        output = self.root / "concat"
        source_concat(self.disc, sources, output)
        self.assertEqual(output.read_bytes(), self.one + self.two)
        sources[0]["sha1"] = "0" * 40
        with self.assertRaisesRegex(ValueError, "MnOne.dat"):
            source_concat(self.disc, sources, output)

    def test_vcdiff_add_statistics_and_malformed_headers(self):
        self.assertEqual(vcdiff_stats(delta_for(self.one, b"extra")), (5, len(self.one) + 5))
        for data in [b"", b"\xd6\xc3\xc4\0\x01", delta_for(self.one, b"extra")[:-1]]:
            with self.assertRaises(ValueError):
                vcdiff_stats(data)

    def test_compliance_accepts_instruction_delta(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(compliance(self.disc, self.manifest["files"], self.root))

    def test_compliance_rejects_retail_add_payload(self):
        entry = self.manifest["files"][0]
        # Put the ADD payload at an arbitrary, explicitly unaligned delta offset.
        extra = b"!" + self.one
        data = delta_for(self.two, extra)
        self.assertNotEqual(data.index(self.one) % 32, 0)
        entry.update(delta_size=len(data), delta_sha1=sha1(data), target_size=len(self.two + extra))
        (self.root / entry["delta"]).write_bytes(data)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertFalse(compliance(self.disc, [entry], self.root))
        self.assertIn("FAIL", output.getvalue())
        self.assertIn("MnOne.dat +0", output.getvalue())
        self.assertIn("delta offset", output.getvalue())

    def test_scan_whole_disc_any_delta_offset_and_chunk_boundaries(self):
        # Non-file system-area bytes must be scanned too. Each disc block is
        # counted once even with odd chunk sizes and windows crossing chunks.
        payload = self.one[:160]
        self.mutate_iso(0x800, payload)
        deltas = {"first": b"abc" + payload, "second": b"12345" + payload,
                  "uniform": b"x" * 256}
        reference = scan_disc(self.disc, deltas)
        self.assertEqual(scan_disc(self.disc, deltas, chunk_size=79), reference)
        self.assertIn((0x800, 3), reference["first"])
        self.assertIn((0x800, 5), reference["second"])
        self.assertEqual(reference["uniform"], [])
        self.assertEqual(len(reference["first"]), len(set(reference["first"])))

    def test_closure_adds_small_third_file_missed_by_aligned_coverage(self):
        third = b"".join(hashlib.sha256(("third" + str(i)).encode()).digest() for i in range(8))
        synthetic_gcm(self.iso, third)
        disc = Disc(self.iso)
        target_data = {"example": self.one + self.two + b"!" + third}
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            selected = select_sources(disc, target_data)
            self.assertEqual([s["disc"] for s in selected["example"]],
                             ["menu/MnTwo.dat", "MnOne.dat"])
            entries = close_sources(disc, target_data, selected, self.root)
        self.assertEqual([s["disc"] for s in entries[0]["sources"]],
                         ["menu/MnTwo.dat", "MnOne.dat", "assets.bin"])
        self.assertIn("round 2", output.getvalue())
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(compliance(disc, entries, self.root))

    def test_closure_stops_after_eight_rounds(self):
        # A persistent hit that implicates a fresh file each round must stop.
        target_data = {"example": self.one}
        selected = {"example": [copy.deepcopy(self.manifest["files"][0]["sources"][0])]}
        fake_hits = {"example": [(0x1000, 0)]}
        owners = [[("extra{}.bin".format(i), 0)] for i in range(7)]
        record = lambda disc, name: dict(selected["example"][0], disc=name)
        with contextlib.redirect_stdout(io.StringIO()), mock.patch(
                "make_sys_deltas.scan_disc", return_value=fake_hits), mock.patch(
                "make_sys_deltas.hit_files", side_effect=owners), mock.patch(
                "make_sys_deltas.report_scan"), mock.patch(
                "make_sys_deltas.source_record", side_effect=record), mock.patch(
                "make_sys_deltas.source_concat"), mock.patch("make_sys_deltas.rebuild"):
            # Provide the concatenation once; synthetic encodes still use xdelta.
            (self.root / "source.bin").write_bytes(self.one)
            with self.assertRaisesRegex(ValueError, "after 8 rounds"):
                close_sources(self.disc, target_data, selected, self.root)


if __name__ == "__main__":
    unittest.main()
