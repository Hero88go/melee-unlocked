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
        self.assertIn("Source Port (not installed)", launcher)
        self.assertIn("melee_game.snapexcl", package)

    def test_release_carries_yougame_spec(self):
        import json
        spec = json.loads(self.read("port/yougame/yougame-native.json"))
        self.assertTrue(spec["disc"])
        self.assertIn("{disc}", spec["args"])
        self.assertEqual(spec["net"], "yougame")
        for packager in ("tools/package_release.py", "tools/package_linux_release.py"):
            text = self.read(packager)
            self.assertIn("port/yougame/yougame-native.json", text)
            self.assertIn("yougame-native.json')", text.replace('"', "'"))


if __name__ == "__main__":
    unittest.main()
