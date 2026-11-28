"""An opponent who hasn't shared their name with the game shows their connect-code tag, not "Player"."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class OpponentNameTag(unittest.TestCase):
    def test_unshared_opponent_name_falls_back_to_code_tag(self):
        src = (ROOT / 'port/runtime/hle/yougame_link.cpp').read_text()
        code_at = src.index('player.code = player.local ? me.code')
        name_at = src.index('player.name = player.local ? me.name : p.value("name", "");')
        self.assertLess(code_at, name_at, 'the code must be known before the name falls back to it')
        self.assertIn('(player.name.empty() || player.name == "Player") && !player.code.empty()', src)
        self.assertIn("player.name = player.code.substr(0, player.code.find('#'));", src)
        self.assertNotIn('p.value("name", "Player")', src)


if __name__ == '__main__':
    unittest.main()
