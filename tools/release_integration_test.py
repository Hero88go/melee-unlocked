"""Static contract checks shared by the launcher, updater and release packager."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ReleaseIntegrationTests(unittest.TestCase):
    def read(self, relative):
        return (ROOT / relative).read_text(encoding="utf-8-sig")

    def test_all_components_use_combined_archive(self):
        package = self.read("tools/package_release.py")
        updater = self.read("port/runtime/host/updater.cpp")
        readme = self.read("README.md")
        self.assertIn("Stable-Recomp-Legacy", package)
        self.assertIn('zip_folder(args.out / f"{name}-win64.zip")', package)
        self.assertIn('Stable-Recomp-Legacy-win64.zip', updater)
        self.assertNotIn("DLSS5-Experimental.zip", updater)
        self.assertIn("Stable-Recomp-Legacy-win64.zip", readme)

    def test_source_port_contract_is_visible_and_opt_in(self):
        package = self.read("tools/package_release.py")
        launcher = self.read("port/app/launcher.cpp")
        readme = self.read("tools/package_release.py")
        self.assertIn("--source-exe", package)
        self.assertIn("--source-dll", package)
        self.assertIn('"Build: Source Port"', launcher)
        self.assertIn('"Build: Stable Recomp Legacy"', launcher)
        self.assertIn("offline modes only", launcher)
        self.assertIn("Source Port does not include Slippi yet", readme)


if __name__ == "__main__":
    unittest.main()
