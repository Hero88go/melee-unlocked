"""The native-Akaneia probe reads the library the loader maps (no app or game launch)."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class AkaneiaProbeWiringTest(unittest.TestCase):
    def test_probe_resolves_bare_names_beside_the_program(self):
        host = source("port/app/source_host.cpp")
        probe = host.split('static const char kName[] = "mu_ak_native_build";', 1)[0].rsplit("#ifndef _WIN32", 1)[1]
        # Same rule as LoadLibraryA in port/linux/compat: a bare name is taken from beside the program,
        # never from the working folder (the app's Linux jail may start the game in its data folder).
        self.assertIn('read_symlink("/proc/self/exe", ec)', probe)
        self.assertIn("library = self.parent_path() / library;", probe)
        self.assertIn("std::ifstream file(library, std::ios::binary);", probe)
        self.assertNotIn("std::ifstream file(named ? named : g_dll.c_str()", probe)

    def test_loader_uses_the_same_rule(self):
        compat = source("port/linux/compat/compat.cpp")
        loader = compat.split("HMODULE LoadLibraryA(LPCSTR name) {", 1)[1].split("\n}\n", 1)[0]
        self.assertIn('fs::read_symlink("/proc/self/exe", ec)', loader)
        self.assertIn("self.parent_path() / lib", loader)


if __name__ == "__main__":
    unittest.main()
