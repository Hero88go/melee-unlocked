"""Static contract checks shared by the launcher, updater and release packager."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ReleaseIntegrationTests(unittest.TestCase):
    def read(self, relative):
        return (ROOT / relative).read_text(encoding="utf-8-sig")

    def test_one_archive_per_release(self):
        package = self.read("tools/package_release.py")
        updater = self.read("port/runtime/host/updater.cpp")
        # Since 0.7.0 a release is one -win64.zip. The updater keeps the old DLSS5-Experimental
        # name only as a fallback for a release that has no -win64.zip.
        self.assertIn('zip_folder(args.out / f"{name}-win64.zip")', package)
        self.assertIn('"-win64.zip"', updater)
        self.assertIn("DLSS5-Experimental.zip", updater)

    def test_source_port_is_offered_only_when_installed(self):
        package = self.read("tools/package_release.py")
        launcher = self.read("port/app/launcher.cpp")
        self.assertIn("--source-exe", package)
        self.assertIn("--source-dll", package)
        self.assertIn("melee_source.exe", launcher)
        self.assertIn("Source Port (Beta, not installed)", launcher)
        self.assertIn("melee_game.snapexcl", package)


if __name__ == "__main__":
    unittest.main()
