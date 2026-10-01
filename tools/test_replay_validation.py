"""Synthetic B5 gate checks. No executable or game process is launched."""
import argparse
import contextlib
import copy
import io
import json
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import replay_batch as batch
import replay_compare as compare


def player(y=28.0):
    return dict(state=14, x=0.0, y=y, facing=1.0, percent=0.0,
                shield=60.0, stocks=4, char=9)


def fixture_slp(path, frames=(0, 1), external=2, y_bits=None, raw_length=True):
    start = bytearray(5 + 0x138)
    start[0] = 0x36
    for slot in range(4):
        start[5 + 0x61 + slot * 0x24] = 3
    start[5 + 0x60] = external
    start[5 + 0x61] = 0
    post = []
    for frame in frames:
        event = bytearray(0x22)
        event[0] = 0x38
        struct.pack_into(">i", event, 1, frame)
        event[7] = 9
        struct.pack_into(">Hfffff", event, 8, 14, 0.0, 28.0, 1.0, 0.0, 60.0)
        event[0x21] = 4
        if y_bits is not None:
            struct.pack_into(">I", event, 0xE, y_bits)
        post.append(event)
    sizes = b"\x36" + struct.pack(">H", len(start) - 1) + b"\x38\x00\x21"
    raw = b"\x35" + bytes([1 + len(sizes)]) + sizes + start + b"".join(post)
    path.write_bytes(b"{U\x03raw[$U#l" + struct.pack(">I", len(raw) if raw_length else 0) + raw)
    return path


def passing_result():
    result = compare.compare_posts({0: {(0, 0): player()}}, {0: {(0, 0): player()}})
    return dict(result, status="ok", exit_code=0, game_exit_code=0)


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.a = {0: {(0, 0): player(), (1, 0): player()},
                  1: {(0, 0): player(), (1, 0): player()}}
        self.b = copy.deepcopy(self.a)

    def check(self, allow=False):
        return compare.compare_posts(self.a, self.b, allow)

    def test_exact_and_terminal_output(self):
        self.assertTrue(self.check()["gate_pass"])
        self.b[2] = {(0, 0): player()}
        result = self.check()
        self.assertTrue(result["gate_pass"])
        self.assertEqual(result["outside_reference_frames"], 1)

    def test_missing_in_play_frame_fails_and_counts_each_player(self):
        del self.b[1]
        result = self.check()
        self.assertFalse(result["gate_pass"])
        self.assertEqual(result["in_play_mismatches"], 2)
        self.assertEqual(result["coverage_mismatches"], 2)
        self.assertEqual(result["first_in_play"]["field"], "missing_frame")

    def test_missing_player_and_extra_follower_fail(self):
        del self.b[0][(1, 0)]
        self.b[1][(0, 1)] = player()
        result = self.check()
        self.assertFalse(result["gate_pass"])
        self.assertEqual(result["in_play_mismatches"], 2)

    def test_signed_zero_is_a_bit_difference(self):
        self.b[0][(0, 0)]["x"] = -0.0
        self.assertFalse(self.check()["gate_pass"])

    def test_shield_and_missing_field_fail(self):
        self.b[0][(0, 0)]["shield"] = 59.0
        del self.b[1][(0, 0)]["percent"]
        self.assertEqual(self.check()["in_play_mismatches"], 2)

    def test_empty_and_gapped_reference_fail(self):
        self.assertFalse(compare.compare_posts({}, {})["gate_pass"])
        self.assertFalse(compare.compare_posts({-1: {(0, 0): player()}}, {})["gate_pass"])
        self.a[3] = {(0, 0): player()}
        self.assertFalse(self.check()["gate_pass"])

    def baseline(self, frame=-113, key=(1, 0), ulps=1):
        value = {(-118, (0, 0)): 26.639930725097656,
                 (-108, (2, 0)): 28.054901123046875}.get((frame, key), 28.04242706298828)
        self.a = {f: {key: player(value)} for f in range(frame, 1)}
        self.b = copy.deepcopy(self.a)
        bits = struct.unpack(">I", struct.pack(">f", value))[0]
        direction = 1 if frame == -108 and key == (2, 0) else -1
        self.b[frame][key]["y"] = struct.unpack(">f", struct.pack(">I", bits + direction * ulps))[0]

    def test_known_preframe_ulp_requires_opt_in(self):
        self.baseline()
        self.assertFalse(self.check()["gate_pass"])
        result = self.check(True)
        self.assertTrue(result["gate_pass"])
        self.assertEqual(result["allowed_preframe_mismatches"], 1)
        self.assertEqual(result["mismatches"], 1)

    def test_second_known_preframe_case(self):
        self.baseline(-108, (2, 0))
        self.assertTrue(self.check(True)["gate_pass"])

    def test_measured_0862_frame_minus118_case(self):
        self.baseline(-118, (0, 0))
        self.assertTrue(self.check(True)["gate_pass"])
        self.assertEqual(self.check(True)["allowed_preframe_mismatches"], 1)

    def test_unmeasured_one_ulp_value_is_not_baseline(self):
        self.baseline()
        self.a[-113][(1, 0)]["y"] = 28.0
        self.b[-113][(1, 0)]["y"] = struct.unpack(">f", struct.pack(">I", 0x41E00001))[0]
        self.assertFalse(self.check(True)["gate_pass"])

    def test_unknown_frame_player_larger_ulp_and_other_field_fail(self):
        for frame, key, ulps in ((-117, (0, 0), 1), (-113, (0, 0), 1),
                                 (-113, (1, 0), 2), (0, (1, 0), 1)):
            with self.subTest(frame=frame, key=key, ulps=ulps):
                self.baseline(frame, key, ulps)
                self.assertFalse(self.check(True)["gate_pass"])
        self.baseline()
        self.b[-113][(1, 0)]["state"] = 15
        self.assertFalse(self.check(True)["gate_pass"])

    def test_preframe_missing_coverage_never_allowed(self):
        self.baseline()
        del self.b[-113]
        self.assertFalse(self.check(True)["gate_pass"])


class FileAndCommandTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_parser_accepts_complete_events_and_rejects_truncated_payload(self):
        path = fixture_slp(self.root / "recording.slp")
        _, post, _ = compare.parse_slp(path)
        self.assertEqual(set(post), {0, 1})
        path.write_bytes(path.read_bytes()[:-1])
        with self.assertRaises(ValueError):
            compare.parse_slp(path)

    def test_unclosed_file_trims_a_cut_off_last_frame(self):
        path = fixture_slp(self.root / "recording.slp", raw_length=False)
        self.assertTrue(compare.parse_slp(path)[1])
        path.write_bytes(path.read_bytes()[:-1])
        self.assertEqual(set(compare.parse_slp(path)[1]), {0})
        # Rebuild a complete unclosed recording followed by an unknown event.
        fixture_slp(path, raw_length=False)
        path.write_bytes(path.read_bytes() + b"\xff")
        with self.assertRaises(ValueError):
            compare.parse_slp(path)

    def test_nan_payload_bits_survive_the_parser(self):
        a = fixture_slp(self.root / "a.slp", y_bits=0x7FA00001)
        b = fixture_slp(self.root / "b.slp", y_bits=0x7FE00001)
        self.assertFalse(compare.compare_posts(compare.parse_slp(a)[1],
                                              compare.parse_slp(b)[1])["gate_pass"])

    def test_default_retail_selection_excludes_mod_and_malformed(self):
        retail = fixture_slp(self.root / "retail.slp")
        mod = fixture_slp(self.root / "mod.slp", external=26)
        broken = self.root / "broken.slp"
        broken.write_bytes(b"broken")
        self.assertTrue(batch.is_vanilla(retail))
        self.assertFalse(batch.is_vanilla(mod))
        self.assertFalse(batch.is_vanilla(broken))

    def test_engine_and_base_disc_are_separate_from_graphics(self):
        args = argparse.Namespace(out=self.root, exe=Path("playback.exe"),
            iso=Path("mod.iso"), replay=Path("replay.slp"), engine="static",
            mod_base_iso=Path("vanilla.iso"), visible=False, backend="d3d11")
        with mock.patch.dict(compare.os.environ, {"REPLAY_COMPARE_EXTRA": ""}):
            cmd = compare.game_command(args)
        self.assertEqual(cmd[cmd.index("--backend") + 1], "d3d11")
        self.assertEqual(cmd[cmd.index("--mod-base-iso") + 1], "vanilla.iso")
        self.assertIn("--sys-dir", cmd)
        for flag in ("--hidden", "--no-music", "--user-dir", "--settings-path"):
            self.assertIn(flag, cmd)
        args.engine = "source"
        self.assertNotIn("--sys-dir", compare.game_command(args))

    def test_game_nonzero_exit_cannot_pass_from_a_recording(self):
        reference = fixture_slp(self.root / "reference.slp")
        iso = self.root / "disc.iso"
        iso.write_bytes(b"disc")
        out = self.root / "output"
        with mock.patch.object(compare.subprocess, "run", return_value=
                subprocess.CompletedProcess([], 7, stdout=b"", stderr=b"")), \
                contextlib.redirect_stdout(io.StringIO()):
            code = compare.main([str(reference), "--iso", str(iso), "--out", str(out)])
        self.assertEqual(code, 2)
        result = json.loads((out / "result.json").read_text())
        self.assertFalse(result["gate_pass"])
        self.assertEqual(result["game_exit_code"], 7)


class BatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.slp = fixture_slp(self.root / "input.slp")
        self.out = self.root / "output"
        self.out.mkdir()
        self.args = argparse.Namespace(engine="static", iso=self.root / "disc.iso",
            exe=self.root / "playback.exe", backend="d3d12", mod_base_iso=self.root / "base.iso",
            timeout=1, allow_preframe_ulp=False, no_cache=False)
        self.common = {"engine": "static", "exe_sha256": "first-build"}

    def fake_comparison(self, cmd, **kwargs):
        out = Path(cmd[cmd.index("--out") + 1])
        (out / "result.json").write_text(json.dumps(passing_result()))
        (out / "output.slp").write_bytes(b"recording")
        return subprocess.CompletedProcess(cmd, 0, stdout="comparison passed\n", stderr="")

    def patches(self, effect=None):
        return (mock.patch.object(batch.shutil, "disk_usage", return_value=
                    argparse.Namespace(free=100 * 1024**3)),
                mock.patch.object(batch.subprocess, "run", side_effect=effect or self.fake_comparison))

    def test_batch_gate_requires_count_and_every_exit(self):
        result = passing_result()
        self.assertTrue(batch.batch_passes([result], 1))
        self.assertFalse(batch.batch_passes([], 1))
        self.assertFalse(batch.batch_passes([result], 2))
        for field, value in (("exit_code", 1), ("game_exit_code", 1),
                             ("in_play_mismatches", 1), ("coverage_mismatches", 1),
                             ("unexpected_mismatches", 1), ("gate_pass", False)):
            with self.subTest(field=field):
                self.assertFalse(batch.batch_passes([dict(result, **{field: value})], 1))

    def test_cache_binds_inputs_and_result_artifacts(self):
        disk, process = self.patches()
        with disk, process as proc:
            first = batch.run(self.slp, self.out, self.args, self.common)
            second = batch.run(self.slp, self.out, self.args, self.common)
            self.assertTrue(batch.result_passes(first))
            self.assertTrue(second["cached"])
            self.assertEqual(proc.call_count, 1)
            self.assertIn("--mod-base-iso", proc.call_args.args[0])
            self.assertIn("--engine", proc.call_args.args[0])
            batch.run(self.slp, self.out, self.args, dict(self.common, exe_sha256="new-build"))
            self.assertEqual(proc.call_count, 2)
            self.slp.write_bytes(self.slp.read_bytes() + b"changed")
            batch.run(self.slp, self.out, self.args, self.common)
            self.assertEqual(proc.call_count, 3)
            latest = batch.run(self.slp, self.out, self.args, self.common)
            Path(latest["output"], "result.txt").write_text("tampered")
            self.assertFalse(batch.run(self.slp, self.out, self.args, self.common)["cached"])
            self.assertEqual(proc.call_count, 4)

    def test_legacy_text_cache_and_failed_subprocess_do_not_pass(self):
        legacy = self.out / self.slp.stem
        legacy.mkdir()
        (legacy / "result.txt").write_text("0 mismatches")

        def failed(cmd, **kwargs):
            self.fake_comparison(cmd, **kwargs)
            return subprocess.CompletedProcess(cmd, 1, stdout="0 mismatches", stderr="")

        disk, process = self.patches(failed)
        with disk, process as proc:
            result = batch.run(self.slp, self.out, self.args, self.common)
        self.assertFalse(batch.result_passes(result))
        self.assertEqual(proc.call_count, 1)

    def test_empty_and_short_corpus_fail_before_any_game(self):
        src = self.root / "corpus"
        src.mkdir()
        with mock.patch.object(batch.subprocess, "run") as proc, \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(batch.main(["--src", str(src), "--count", "1",
                                         "--out", str(self.out)]), 1)
            fixture_slp(src / "one.slp")
            self.assertEqual(batch.main(["--src", str(src), "--count", "2",
                                         "--out", str(self.out)]), 1)
        proc.assert_not_called()

    def test_malformed_requested_corpus_is_not_silently_filtered(self):
        src = self.root / "corpus"
        src.mkdir()
        (src / "broken.slp").write_bytes(b"broken")
        with mock.patch.object(batch.subprocess, "run") as proc, \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(batch.main(["--src", str(src), "--count", "1",
                                         "--out", str(self.out)]), 2)
        proc.assert_not_called()

    def test_include_mods_is_an_explicit_selection_opt_in(self):
        src = self.root / "corpus"
        src.mkdir()
        fixture_slp(src / "mod.slp", external=26)
        with mock.patch.object(batch, "run_identity", return_value={}), \
                mock.patch.object(batch, "require_iso", return_value=self.args.iso), \
                mock.patch.object(batch, "run", return_value=dict(passing_result(), replay="mod.slp")) as run, \
                contextlib.redirect_stdout(io.StringIO()):
            opts = ["--src", str(src), "--count", "1", "--out", str(self.out)]
            self.assertEqual(batch.main(opts), 1)
            run.assert_not_called()
            self.assertEqual(batch.main(opts + ["--include-mods", "--engine", "static"]), 0)
            self.assertEqual(run.call_args.args[2].engine, "static")

    def test_hash_changes_for_disc_dll_and_options(self):
        for p in (self.args.exe, self.args.iso, self.args.mod_base_iso,
                  self.root / "melee_game.dll"):
            p.write_bytes(b"initial")
        tools = self.root / "tools"
        tools.mkdir()
        for name in ("replay_compare.py", "melee_iso.py"):
            (tools / name).write_bytes(b"tool")
        self.args.engine = "source"
        with mock.patch.object(batch, "ROOT", self.root), \
                mock.patch.dict(batch.os.environ, {}, clear=True):
            first = batch.run_identity(self.args)
            for path in (self.args.iso, self.args.mod_base_iso, self.root / "melee_game.dll"):
                with self.subTest(path=path.name):
                    path.write_bytes(b"changed")
                    self.assertNotEqual(first, batch.run_identity(self.args))
                    path.write_bytes(b"initial")
            self.args.allow_preframe_ulp = True
            self.assertNotEqual(first, batch.run_identity(self.args))


if __name__ == "__main__":
    unittest.main()
