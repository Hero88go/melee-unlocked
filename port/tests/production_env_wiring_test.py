"""All diagnostic getenv reads must reach the session policy, including C shims."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]

class WiringTest(unittest.TestCase):
    def test_all_reads_use_policy(self):
        sites = 0
        for directory in ('port', 'sourceport/game/shim'):
            for path in (ROOT / directory).rglob('*'):
                if path.suffix not in ('.c', '.cpp', '.h') or 'tests' in path.parts or 'third_party' in path.parts:
                    continue
                source = path.read_text(encoding='utf-8-sig')
                for match in re.finditer(r'([\w:]*getenv)\("(MELEE_TEST_[A-Z0-9_]+|MELEE_SNAPSHOT_EXCLUDE)"\)', source):
                    sites += 1
                    expected = 'host::test_env::getenv' if path.suffix == '.cpp' else 'mu_test_getenv'
                    self.assertEqual(match[1], expected, f'{path.relative_to(ROOT)}: {match[2]}')
        self.assertGreaterEqual(sites, 75)

    def test_parent_and_host_callback_wiring(self):
        main = (ROOT / 'port/app/main.cpp').read_text()
        self.assertIn('mods::identify_vanilla(std::filesystem::u8path(o.iso), check, &verification)', main)
        bridge = (ROOT / 'port/runtime/hle/yougame_link.cpp').read_text()
        self.assertIn('host::test_env::yougame_session = active();', bridge)
        host = (ROOT / 'port/app/source_host.cpp').read_text()
        self.assertIn('h.test_getenv = host::test_env::getenv;', host)
        layer = host.split('std::string iso_layer_fingerprint(', 1)[1].split('std::string dir_layer_fingerprint', 1)[0]
        self.assertIn('layer_fingerprint(g_mod_overlay', layer)
        self.assertNotIn('fingerprints.txt', layer)

if __name__ == '__main__':
    unittest.main()
