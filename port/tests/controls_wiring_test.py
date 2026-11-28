"""Tap jump off and tilt stick: the decomp patch changes exactly the game's own decisions it should, each
behind MU_NATIVE and one mu_control() test, and leaves everything else that reads the sticks alone (DI,
SDI, the C-stick jump out of shield, the short hop release, Peach's float, air dodges, the jump squat's
up smash). The host side sends the exchange ahead of every selection and folds it into the setup hash."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
PATCH = (ROOT / 'sourceport/patches/melee-native.patch').read_text(encoding='utf-8', errors='replace')
HOST = ROOT / 'port/runtime'


def file_section(path):
    start = PATCH.index('diff --git a/%s b/%s' % (path, path))
    end = PATCH.find('\ndiff --git ', start + 10)
    return PATCH[start:end if end >= 0 else len(PATCH)]


def added(path):
    try:
        section = file_section(path)
    except ValueError:
        return ''
    return '\n'.join(line[1:] for line in section.splitlines() if line.startswith('+') and not line.startswith('+++'))


def patched(path):
    """The hunks' post-image: added and unchanged lines, as the patched file reads."""
    try:
        section = file_section(path)
    except ValueError:
        return ''
    return '\n'.join(line[1:] for line in section.splitlines()
                     if (line.startswith('+') and not line.startswith('+++')) or line.startswith(' '))


FT = 'src/melee/ft/kinds/ftCommon/'


class DecompSites(unittest.TestCase):
    def test_tap_jump_sites(self):
        for path, count in ((FT + 'ftCo_Jump.c', 2), (FT + 'ftCo_JumpAerial.c', 1), (FT + 'ftCo_JumpAerialF1.c', 1),
                            (FT + 'ftCo_PassiveWall.c', 1)):
            self.assertEqual(added(path).count('mu_control(fp, MU_CONTROL_TAPJUMP_OFF)'), count, path)

    def test_tilt_stick_sites(self):
        s4 = added(FT + 'ftCo_AttackS4.c')
        self.assertIn('if (mu_control(fp, MU_CONTROL_TILT_STICK)) {\n            return tiltStickS3(gobj, stick_x_sign, stick_angle);', s4)
        self.assertIn('mu_control(fp, MU_CONTROL_TILT_DASH_ATTACK)', s4)
        self.assertIn('ftCo_AttackDash_EnterTiltStick(gobj);\n            ftCo_AttackDash_SetMv0(gobj);', s4)
        self.assertIn('!checkLStick(fp) && ftCo_800DF2D8(fp) && mu_control(fp, MU_CONTROL_TILT_STICK)', added(FT + 'ftCo_AttackHi4.c'))
        self.assertIn('!checkLStick(fp) && ftCo_800DF3A8(fp) && mu_control(fp, MU_CONTROL_TILT_STICK)', added(FT + 'ftCo_AttackLw4.c'))
        for path, entry in ((FT + 'ftCo_AttackS3.c', 'void ftCo_AttackS3_EnterTiltStick(Fighter_GObj* gobj, float stick_angle)'),
                            (FT + 'ftCo_AttackHi3.c', 'void ftCo_AttackHi3_EnterTiltStick(Fighter_GObj* gobj)'),
                            (FT + 'ftCo_AttackLw3.c', 'void ftCo_AttackLw3_EnterTiltStick(Fighter_GObj* gobj)'),
                            (FT + 'ftCo_AttackDash.c', 'void ftCo_AttackDash_EnterTiltStick(Fighter_GObj* gobj)')):
            self.assertIn(entry, added(path), path)

    def test_jump_squat_up_smash_keeps_its_smash(self):
        source = file_section(FT + 'ftCo_AttackHi4.c')
        hunks = [h for h in source.split('\n@@ ')[1:]
                 if h.split('\n', 1)[0].endswith('ftCo_AttackHi4_CheckInputNoD0(HSD_GObj* gobj)')]
        self.assertEqual(len(hunks), 1)
        self.assertNotIn('MU_CONTROL_TILT_STICK', hunks[0])

    def test_untouched_paths(self):
        # The stick readers that are not a jump or a C-stick smash: never gated.
        for path in ('src/melee/ft/ft_0DF1.c', FT + 'ftCo_KneeBend.c', FT + 'ftCo_Damage.c', FT + 'ftCo_EscapeAir.c',
                     FT + 'ftCo_Guard.c', FT + 'ftCo_CaptureWait.c', FT + 'ftCo_CaptureWaitKirby.c',
                     'src/melee/ft/kinds/ftPeach/ftpeachfloat.c', 'src/melee/ft/fighter.c'):
            self.assertNotIn('mu_control(', added(path), path)

    def test_mid_match_toggle_reads_the_shared_input(self):
        # Start + D-pad: one call in Fighter_procInput, after the pad or the replay's input is in place,
        # and the pause checks skip that Start; nothing else in fighter.c or gmvs.c changes for it.
        fighter = added('src/melee/ft/fighter.c')
        self.assertEqual(fighter.count('mu_controls_pad(fp);'), 1)
        self.assertLess(fighter.index('mu_replay_input(fp);'), fighter.index('mu_controls_pad(fp);'))
        self.assertEqual(added('src/melee/gm/gmvs.c').count('mu_controls_pause_combo('), 2)

    def test_every_site_is_native_only(self):
        sites = 0
        for body in PATCH.split('\ndiff --git ')[1:]:
            name = body.split(' ', 1)[0]
            for hunk in body.split('\n@@ ')[1:]:
                lines = hunk.splitlines()
                for i, line in enumerate(lines):
                    if line.startswith('+') and 'mu_control(' in line:
                        sites += 1
                        before = '\n'.join(lines[:i])
                        self.assertIn('#ifdef MU_NATIVE', before, '%s: %s' % (name, line))
        self.assertEqual(sites, 12)   # 5 tap jump (Jump x2, JumpAerial, JumpAerialF1, PassiveWall), forward 3, up 2, down 2

    def test_plain_branches_are_the_originals(self):
        # Each "#else" keeps the decomp's own text, so a non-native build is the unchanged game.
        jump = patched(FT + 'ftCo_Jump.c')
        self.assertIn('#else\n    if ((fp->input.lstick[0].y >= p_ftCommonData->tap_jump_threshold) &&\n'
                      '        (fp->active_timer.lstick.y < p_ftCommonData->tap_jump_window))\n#endif', jump)
        wall = patched(FT + 'ftCo_PassiveWall.c')
        self.assertIn('#else\n    if (fp->x67E < p_ftCommonData->x250 ||\n        fp->input.lstick[0].y >= p_ftCommonData->tap_jump_threshold)\n#endif', wall)

    def test_match_start_freezes_before_replay_and_online(self):
        gmvs = added('src/melee/gm/gmvs.c')
        self.assertIn('mu_controls_match_start();\n    /* Slippi playback, RestoreGameInfo', gmvs)


class HostWiring(unittest.TestCase):
    def test_exchange_rides_ahead_of_every_selection(self):
        net = (HOST / 'hle/slippi_net.cpp').read_text(encoding='utf-8')
        self.assertEqual(net.count('controls::before_selection(match_info_.local.is_character_selected != 0)'), 2)
        sel = net[net.index('void NetplayClient::SetMatchSelections'):]
        self.assertLess(sel.index('controls::before_selection'), sel.index('WriteSelections(*spac'))
        self.assertIn('if (mid == NP_MSG_MU_CONTROLS) {', net)
        self.assertIn('remote_build_[pidx].controls = has_caps4 && (caps4 & MU_BUILD_CAPS4_CONTROLS) != 0;', net)
        # 1.0.33: the arena stage pick bit rides the same byte, whatever the controls say.
        self.assertIn('*p << (uint8_t)((controls::advertise() ? MU_BUILD_CAPS4_CONTROLS | MU_BUILD_CAPS4_20XX103 : 0) | MU_BUILD_CAPS4_ARENA_STAGE_PICK);', net)

    def test_setup_hash_carries_the_decision_and_freezes_it(self):
        online = (HOST / 'hle/slippi_online.cpp').read_text(encoding='utf-8')
        gate = online[online.index('setup_inputs = controls::fold_inputs(setup_inputs);'):]
        self.assertLess(gate.index('setup_hash_gate('), gate.index('controls_expected() && !controls::settled()'))
        self.assertLess(gate.index('controls_expected() && !controls::settled()'), gate.index('controls_freeze(lobby_plan);'))
        self.assertIn('controls::match_started();', online)
        self.assertIn('g_controls_match_running = true;', online)
        self.assertIn('controls::session_end();', online)

    def test_host_api_and_replays(self):
        self.assertIn('#define MU_HOST_API_VERSION 35', (HOST / 'abi/mu_host.h').read_text(encoding='utf-8'))
        host = (ROOT / 'port/app/source_host.cpp').read_text(encoding='utf-8')
        self.assertIn('h.control_flags = h_control_flags;', host)
        self.assertIn('if (g_replaying) word = g_replay_controls;', host)
        self.assertIn('g_replay_stream.set_controls(word);', host)
        stream = (HOST / 'hle/native_replay_stream.cpp').read_text(encoding='utf-8')
        self.assertEqual(stream.count('"muControls"'), 2)

    def test_settings_rows_and_keys(self):
        settings = (HOST / 'gx/pc_settings.cpp').read_text(encoding='utf-8')
        section = settings.index('settings_section("Tap jump and tilt stick");')
        self.assertLess(section, settings.index('settings_section("L-cancel");'))
        for key in ('tapjumpoff', 'tiltstick', 'tiltdash', 'tiltitem'):
            self.assertIn('"\\n%s " << (controls::settings()' % key, settings)
            self.assertIn('key == "%s"' % key, settings)


if __name__ == '__main__':
    unittest.main()
