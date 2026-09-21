"""Static contract checks shared by the launcher, updater and release packager."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ReleaseIntegrationTests(unittest.TestCase):
    def read(self, relative):
        return (ROOT / relative).read_text(encoding="utf-8-sig")

    def test_all_components_use_separate_archives(self):
        package = self.read("tools/package_release.py")
        updater = self.read("port/runtime/host/updater.cpp")
        readme = self.read("README.md")
        self.assertIn("Stable-Recomp-Legacy", package)
        self.assertIn('zip_folder(folder, args.out / f"{name}-win64.zip")', package)
        self.assertIn('zip_folder(experimental_folder, args.out / f"MeleeUnlocked-{args.version}-DLSS5-Experimental.zip")', package)
        self.assertIn('Stable-Recomp-Legacy-win64.zip', updater)
        self.assertIn('DLSS5-Experimental.zip', updater)
        self.assertIn("Stable-Recomp-Legacy-win64.zip", readme)

    def test_source_port_is_hidden_from_release_launcher(self):
        package = self.read("tools/package_release.py")
        launcher = self.read("port/app/launcher.cpp")
        self.assertIn("--source-exe", package)
        self.assertIn("--source-dll", package)
        self.assertNotIn('"Build: Source Port"', launcher)
        self.assertIn("ShowWindow(g_engine_btn, SW_HIDE)", launcher)


if __name__ == "__main__":
    unittest.main()
