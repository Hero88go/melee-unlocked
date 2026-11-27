"""A replay recorded with a Mods folder disc the session did not load (an Akaneia Play match opened
from the launcher's Replays tab) plays with that disc instead of stopping. Proven end to end with
hidden runs; this keeps the wiring in place."""
from pathlib import Path
import hashlib
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
HOST = (ROOT / 'port/app/source_host.cpp').read_text(encoding='utf-8')


def function(text, signature, last=False):
    start = text.rindex(signature) if last else text.index(signature)
    body = text.index('{', start)
    depth = 0
    for i in range(body, len(text)):
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        if depth == 0:
            return text[start:i + 1]
    raise AssertionError(signature + ' has no end')


class ReplayModsFolderWiring(unittest.TestCase):
    def test_identity_matches_the_load_loop(self):
        # The trial identity must be built exactly as load_mod_overlay builds g_mod_fingerprint.
        load = function(HOST, 'void load_mod_overlay()')
        self.assertIn('identity.update(std::string(kind) + "\\t" + fingerprint + "\\n")', load)
        pick = function(HOST, 'bool replay_layers_from_mods_folder(')
        self.assertIn('identity.update("iso\\t" + fingerprint + "\\n")', pick)
        # Akaneia 1.0.1 alone: the content a player's Akaneia replays record (f4bb2d7096633d5e...).
        known = re.search(r'\{"([0-9a-f]{64})", "Akaneia 1\.0\.1"\}', HOST).group(1)
        self.assertTrue(hashlib.sha256(f'iso\t{known}\n'.encode()).hexdigest().startswith('f4bb2d7096633d5e'))

    def test_only_mods_folder_sessions_are_redirected(self):
        pick = function(HOST, 'bool replay_layers_from_mods_folder(')
        for guard in ('g_replay_recorded_mods.empty()', 'g_replay_recorded_mods == g_mod_fingerprint',
                      '!g_explicit_mod_layers.empty()', '!g_mod_profile_name.empty()', '!mods::status().detected',
                      'g_replay_layers_chosen'):
            self.assertIn(guard, pick)
        self.assertIn('mods::runs_on(d, true)', pick)
        self.assertIn('d.card', pick)

    def test_reload_and_unchanged_failure(self):
        load = function(HOST, 'void load_mod_overlay()')
        call = load.index('replay_layers_from_mods_folder(base_disc, lookup, read_base, folder_layer_ids)')
        self.assertLess(load.index('g_mod_fingerprint = identity.hex();'), call)
        self.assertIn('load_mod_overlay();', load[call:call + 200])
        # No disc recorded it: the check still stops with the same message.
        check = function(HOST, 'void check_replay_content()', last=True)
        self.assertIn('enable the same mods to play it', check)
        # The second pass does not scan the Mods folder again.
        self.assertIn('if (g_replay_layers_chosen) return;', function(HOST, 'void apply_detected_mods()'))
        self.assertIn('g_replay_recorded_mods = g_replay_mod_profile;', function(HOST, 'bool set_replay(const char* path)', last=True))


if __name__ == '__main__':
    unittest.main()
