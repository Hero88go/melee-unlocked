"""Exercise listing launch policies against the real Windows executables.

python tools/test_listing_launch.py --bin-dir build-avx2/port/Release --iso <retail.iso>
All settings, loose mods, logs and saves are isolated in temporary directories.
"""
import argparse
import csv
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CONFIG = None


class ListingLaunchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mu-listings-")
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)

    def run_game(self, engine, extra, folder=None, timeout=60):
        folder = folder or self.folder
        folder.mkdir(parents=True, exist_ok=True)
        env = dict(os.environ, MELEE_NO_GC_ADAPTER="1")
        env.pop("MELEE_LAG_TEST", None)
        return subprocess.run(
            [str(CONFIG.bin_dir / engine), "--headless", "--volume", "0", "--no-music",
             "--settings-path", str(folder / "settings.ini"),
             "--log-file", str(folder / "game.log"),
             "--sys-dir", str(ROOT / "port/slippi_sys"),
             "--card-dir", str(folder / "CardA"),
             "--user-dir", str(folder / "User"),
             "--replay-dir", str(folder / "Replays"), *extra],
            cwd=folder, env=env, capture_output=True, text=True, timeout=timeout)

    def test_conflicting_mod_flags_in_either_order(self):
        for engine in ("melee_source.exe", "melee_port.exe"):
            for mod in ("--mod-profile", "--mod-iso", "--mod-base-iso", "--gecko-codes"):
                for args in (["--vanilla-only", mod, "missing"], [mod, "missing", "--vanilla-only"]):
                    with self.subTest(engine=engine, args=args):
                        result = self.run_game(engine, args)
                        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                        self.assertIn("--vanilla-only cannot be combined with " + mod, result.stderr)

    def test_vanilla_boot_ignores_saved_and_loose_mods(self):
        for engine in ("melee_source.exe", "melee_port.exe"):
            with self.subTest(engine=engine):
                folder = self.folder / engine
                (folder / "Mods/Files").mkdir(parents=True)
                (folder / "Mods/Files/PlFx.dat").write_bytes(b"invalid loose mod: must not be loaded")
                settings = folder / "settings.ini"
                settings.write_text("mod_profile missing-profile\nmod_te_enabled 1\nmod_tmce_enabled 1\n"
                                    "te_options 30\nte_options2 100\ncpu_20xx 1\ncpu_tech 5\n"
                                    "autolcancel 1\nlosslesslagtest 1\n")
                before = settings.read_bytes()
                result = self.run_game(engine, ["--vanilla-only", "--load-settings", "--iso", str(CONFIG.iso),
                                               "--frames", "120", "--fast"], folder)
                self.assertEqual(result.returncode, 0, (result.stdout + result.stderr)[-6000:])
                log = (folder / "game.log").read_text(errors="replace")
                self.assertIn("verified retail disc", log)
                self.assertNotIn("mods: dir", log)
                self.assertNotIn("saved profile missing-profile could not be loaded", log)
                self.assertEqual(before, settings.read_bytes())
                self.assertTrue((folder / "Mods/Files/PlFx.dat").is_file())

    def test_source_vanilla_gameplay_matches_clean_launch(self):
        rows = []
        for vanilla in (False, True):
            folder = self.folder / ("restricted" if vanilla else "ordinary")
            folder.mkdir()
            digest = folder / "digest.csv"
            args = ["--iso", str(CONFIG.iso), "--frames", "2700", "--fast", "--time-base", "1",
                    "--slippi-menus", "off",
                    "--script", str(ROOT / "port/scripts/native_unlocked_vs.txt"),
                    "--match", "31:2/c9:20/c9", "--rng-seed", "1234", "--state-digest", str(digest)]
            if vanilla:
                args.append("--vanilla-only")
                (folder / "settings.ini").write_text("cpu_20xx 1\ncpu_tech 5\ncpu_sdi 6\nautolcancel 1\n")
                args.append("--load-settings")
            result = self.run_game("melee_source.exe", args, folder)
            self.assertEqual(result.returncode, 0, (result.stdout + result.stderr)[-6000:])
            with digest.open(newline="") as stream:
                rows.append(list(csv.DictReader(stream)))
            self.assertGreater(len(rows[-1]), 100)
            self.assertTrue(any(row["p0_present"] != "00000000" for row in rows[-1]))
        self.assertEqual(rows[0], rows[1], "vanilla-only with saved assists must play exactly like a clean launch")

    def test_explicit_source_mod_survives_discovery_disable(self):
        # A harmless banner replacement exercises the actual native overlay path without
        # requiring a particular third-party mod download. The invalid automatic overlay
        # must not accompany the explicitly selected pack.
        pack = self.folder / "chosen-pack"
        pack.mkdir()
        (pack / "opening.bnr").write_bytes(b"explicit listing banner")
        (self.folder / "Mods/Files").mkdir(parents=True)
        (self.folder / "Mods/Files/PlFx.dat").write_bytes(b"unrelated broken pack")
        result = self.run_game("melee_source.exe", ["--iso", str(CONFIG.iso), "--mod-dir", str(pack),
                                                  "--no-mod-autodetect", "--frames", "120", "--fast"])
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr)[-6000:])
        log = (self.folder / "game.log").read_text(errors="replace")
        self.assertIn("mods: dir " + str(pack), log)
        self.assertNotIn("mods: dir Mods", log)
        self.assertNotIn("unrelated broken pack", log)

    def test_vanilla_listing_rejects_nonretail_image(self):
        invalid = self.folder / "nonretail.iso"
        # A readable disc-shaped file with a retail header but no valid retail content.
        with CONFIG.iso.open("rb") as stream:
            invalid.write_bytes(stream.read(0x10000))
        for engine in ("melee_source.exe", "melee_port.exe"):
            with self.subTest(engine=engine):
                result = self.run_game(engine, ["--vanilla-only", "--iso", str(invalid)])
                self.assertEqual(result.returncode, 2, (result.stdout + result.stderr)[-6000:])
                self.assertIn("--vanilla-only requires an unmodified", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--iso", type=Path, required=True)
    CONFIG = parser.parse_args()
    CONFIG.bin_dir = CONFIG.bin_dir.resolve()
    CONFIG.iso = CONFIG.iso.resolve()
    unittest.main(argv=[__file__], verbosity=2)
