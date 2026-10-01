import json
import tempfile
import unittest
from pathlib import Path
import zipfile

from crash_report_privacy import private_text, report_names, scrub_line
from crash_report_text import markdown

VECTORS = Path(__file__).resolve().parent / "crash_relay" / "test" / "privacy_vectors.json"


class CrashReportPrivacyVectors(unittest.TestCase):
    def test_shared_vectors(self):
        vectors = json.loads(VECTORS.read_text(encoding="utf-8"))
        self.assertGreaterEqual(len(vectors["cases"]), 30)
        for case in vectors["cases"]:
            with self.subTest(line=case["in"][:120]):
                self.assertEqual(scrub_line(case["in"], report_names(case["in"], vectors["names"])), case["out"])

    def test_file_counts_names_and_lobby(self):
        log = (b"mods: on:  Mods\\Discs\\Some Pack.iso\r\r\n\r\nslippi: logged in as someone (ABCD#123)\n"
               b"card: mounted from C:\\Users\\Someone\\Games\\profile-1 (1 files)\nhost someone ready\n")
        self.assertEqual(private_text("melee_port.log", log),
                         "mods: on:  Mods\\Discs\\Some Pack.iso\ncard: mounted from profile-1 (1 files)\n"
                         "host [user] ready\n[1 lines omitted for privacy]\n")
        self.assertEqual(private_text("lobby.log", log), "[4 lines omitted for privacy]\n")
        self.assertEqual(private_text("melee_port.log", b"tail of a cut line\nhello PlayerOne\n", ["PlayerOne"], True),
                         "hello [user]\n[1 lines omitted for privacy]\n")
        once = private_text("melee_port.log", log)
        self.assertEqual(private_text("melee_port.log", once.encode()), once)


class CrashReportTextTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "report.zip"

    def tearDown(self):
        self.temp.cleanup()

    def write(self, entries):
        with zipfile.ZipFile(self.path, "w", compression=zipfile.ZIP_DEFLATED) as out:
            for name, data in entries:
                out.writestr(name, data)

    def test_readable_error_and_log_without_binary_or_other_files(self):
        self.write([("melee_port_crash.txt", "FATAL: game stopped at C:/Users/PrivateTesterAlpha/NEW project/lbarchive.c:94: , version 0.8.62\r\r\n"
                     "last guest function 8006B7F8 ftCo_800693AC, lr 8006B80C\r\n"),
                    ("melee_port.log", "```\n  stack: melee_game.dll+0x14982F\n[game] Cannot find symbol itPublicData.\n\x00"),
                    ("melee_port_crash.dmp", b"DUMP_PRIVATE_BYTES"),
                    ("../settings.ini", "UNEXPECTED_PRIVATE_BYTES")])
        report = markdown(self.path)
        self.assertIn("FATAL: game stopped at lbarchive.c:94: , version 0.8.62\n"
                      "last guest function 8006B7F8 ftCo_800693AC, lr 8006B80C\n", report)
        self.assertIn("Cannot find symbol itPublicData.", report)
        self.assertIn("\n  stack: melee_game.dll+0x14982F\n", report)
        self.assertNotIn("DUMP_PRIVATE_BYTES", report)
        self.assertNotIn("UNEXPECTED_PRIVATE_BYTES", report)
        self.assertNotIn("\x00", report)
        self.assertNotIn("PrivateTester", report)
        self.assertNotIn("C:/Users/", report)

    def test_log_tail_and_invalid_utf8(self):
        self.write([("melee_port.log", b"EARLY_MARKER" + b"x" * (512 << 10) + b"\xff\nFATAL: game stopped at lbarchive.c:94, version 0.8.62")])
        report = markdown(self.path)
        self.assertIn("Earlier bytes omitted", report)
        self.assertNotIn("EARLY_MARKER", report)
        self.assertIn("FATAL: game stopped at lbarchive.c:94, version 0.8.62", report)
        self.assertNotIn("\ufffd", report)

    def test_cut_first_line_is_dropped(self):
        # The 512 KiB cap lands inside the path, leaving "PrivateFolder\x.iso" with no root to recognize.
        filler = "".join(f"scene: major 08 minor 00 (frame {i:07d})\n" for i in range(12786)) + "y" * 41 + "\n"
        self.assertEqual(len(filler), (512 << 10) - 20)
        self.write([("melee_port.log", "open C:\\Users\\PrivateTesterAlpha\\Games\\Melee\\PrivateFolder\\x.iso\n" + filler)])
        report = markdown(self.path)
        self.assertIn("Earlier bytes omitted", report)
        self.assertNotIn("Private", report)
        self.assertIn("scene: major 08 minor 00 (frame 0012785)", report)

    def test_identity_lines_paths_and_lobby_removed_and_diagnostics_kept(self):
        self.write([("melee_port.log", "slippi: logged in as PrivateTesterAlpha (CODE#123)\n"
                    "mods: on: C:/Users/PrivateTesterBeta/Mods/Discs/Some Pack.iso | Detected: Some Pack\n"
                    "config /home/PrivateTesterGamma/profile/melee.ini\n"
                    "http://private.invalid/?token=PRIVATE_AUTH\n"
                    "native practice: opponent WXYZ#987 at 192.168.1.10:51413, host PrivateTesterBeta\n"
                    "FATAL: guest fault: call to unmapped guest address (0000D899) in ftCo_800C0658 (800C0658); lr=8035E3F8 r1=804EE740, version 0.8.62\n"
                    "scene: major 08 minor 00 (frame 3919)\n"),
                    ("lobby.log", "PrivateTesterAlpha joined from 192.168.1.10\n")])
        report = markdown(self.path)
        for private in ["PrivateTester", "CODE#123", "WXYZ#987", "C:/Users", "/home/", "192.168.1.10", "PRIVATE_AUTH"]:
            self.assertNotIn(private, report)
        self.assertIn("mods: on: Some Pack.iso | Detected: Some Pack\n", report)
        self.assertIn("native practice: opponent [code] at [ip], host [user]\n", report)
        self.assertIn("FATAL: guest fault: call to unmapped guest address (0000D899) in ftCo_800C0658 (800C0658); "
                      "lr=8035E3F8 r1=804EE740, version 0.8.62\n", report)
        self.assertIn("scene: major 08 minor 00 (frame 3919)\n[3 lines omitted for privacy]", report)
        self.assertIn("## lobby.log\n\n```text\n[1 lines omitted for privacy]\n```", report)

    def test_source_file_named_after_the_user_folder(self):
        self.write([("melee_port_crash.txt", "FATAL: game stopped at C:/Users/PrivateTesterAlpha/src/PrivateTesterAlpha.cpp:94: , version 0.8.62")])
        report = markdown(self.path)
        self.assertNotIn("PrivateTester", report)
        self.assertIn("FATAL: game stopped at [user].cpp:94: , version 0.8.62", report)

    def test_duplicate_names_rejected(self):
        self.write([("melee_port.log", "one"), ("melee_port.log", "two")])
        with self.assertRaisesRegex(ValueError, "duplicate"):
            markdown(self.path)

    def test_oversized_compressed_text_rejected_before_inflation(self):
        self.write([("melee_port.log", b"x" * ((2 << 20) + 1))])
        with self.assertRaisesRegex(ValueError, "oversized"):
            markdown(self.path)


if __name__ == "__main__":
    unittest.main()
