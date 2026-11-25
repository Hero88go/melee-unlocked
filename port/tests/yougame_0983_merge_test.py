"""v0.9.83 merge boundaries: CLI compatibility, graphics lifecycle, and patch metadata."""
from pathlib import Path
import re
import subprocess
import shutil
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
GIT = '/Library/Developer/CommandLineTools/usr/bin/git' if sys.platform == 'darwin' else (shutil.which('git') or 'git')
CONFLICTED = ('port/app/main.cpp', 'port/linux/renderer_gl.cpp', 'port/linux/window_sdl.inl',
              'port/runtime/gx/gx_d3d11.cpp', 'port/runtime/gx/gx_d3d12.cpp',
              'port/runtime/gx/pc_settings.cpp', 'port/runtime/host/window.cpp', 'port/runtime/host/window.h')


def source(path):
    return (ROOT / path).read_text(encoding='utf-8-sig')


def revision(ref, path):
    if not (ROOT / '.git').exists():
        raise unittest.SkipTest('history checks require a git checkout')
    available = subprocess.run([GIT, 'show', '--format=', '--no-patch', ref + '^{commit}'], cwd=ROOT,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if available.returncode:
        raise unittest.SkipTest('history checks require revision ' + ref)
    return subprocess.check_output([GIT, 'show', ref + ':' + path], cwd=ROOT).decode('utf-8-sig')


def flags(text):
    return set(re.findall(r'a == "(--[a-z0-9-]+)"', text))


class YouGame0983MergeTest(unittest.TestCase):
    def test_all_conflict_markers_are_removed(self):
        for path in CONFLICTED:
            self.assertIsNone(re.search(r'^(?:<{7}|={7}|>{7}|\|{7})(?: |$)', source(path), re.M), path)

    def test_upstream_cli_delta_and_existing_options_survive(self):
        path = 'port/app/main.cpp'
        before, upstream, merged = (flags(revision('v0.9.82', path)),
                                     flags(revision('986f971e', path)), flags(source(path)))
        self.assertEqual(upstream - before, {'--msaa'})
        self.assertEqual(before - upstream, set())
        self.assertTrue(flags(revision('HEAD', path)) <= merged)
        self.assertTrue(upstream <= merged)
        main = source(path)
        self.assertIn('v == "vulkan" || v == "vk"', main)
        self.assertIn('gfx.ssaa = std::clamp(n, 1, 4)', main)
        # The launch spec in mu-ci and mu-ci-console has these ten switches.
        for flag in ('--iso', '--threaded-renderer', '--settings-path', '--sys-dir', '--user-dir',
                     '--replay-dir', '--card-dir', '--log-file', '--shader-cache', '--vanilla-only'):
            self.assertIn('"' + flag + '"', main)

    def test_vulkan_window_selection_precedes_creation_in_both_paths(self):
        for path, prefix in (('port/app/main.cpp', 'gfx'), ('port/runtime/gx/threaded_backend.cpp', 'options')):
            text = source(path)
            prefer = text.index('host::window_prefer_vulkan(' + prefix + '.api')
            # The availability check sits just above the window choice (0.9.96 added a failed-starts check between;
            # 0.9.99 made its limit per-OS).
            self.assertGreater(text.rfind('vulkan_available()', 0, prefer), prefer - 1000)
            self.assertLess(prefer, text.index('host::window_create(', prefer))
        window = source('port/linux/window_sdl.inl')
        self.assertIn('(g_vulkan_window ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL)', window)
        self.assertIn('flags |= boxed ? SDL_WINDOW_HIDDEN', window)
        header = source('port/runtime/host/window.h')
        self.assertLess(header.index('#endif', header.index('void* window_create')),
                        header.index('void window_prefer_vulkan'))

    def test_mac_keeps_gl_and_vulkan_free_build(self):
        vulkan = source('port/runtime/gx/gx_vulkan.cpp')
        mac = vulkan.split('#ifdef __APPLE__', 1)[1].split('#else', 1)[0]
        self.assertIn('bool vulkan_available() { return false; }', mac)
        self.assertNotIn('#include <vulkan/', mac)
        backend = source('port/linux/full/gx_backend_linux.cpp')
        self.assertIn('#ifndef __APPLE__\n#include <SDL_vulkan.h>\n#endif', backend)
        self.assertIn('#ifndef __APPLE__\n  // Vulkan', backend)
        self.assertIn('if (host::window_embedded()) host::window_client_size(w, h);', backend)

    def test_imgui_chooses_backend_and_preserves_manual_gamepads(self):
        settings = source('port/runtime/gx/pc_settings.cpp')
        start = settings.index('if (SDL_GetWindowFlags(')
        end = settings.index('io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;', start)
        block = settings[start:end]
        for call in ('ImGui_ImplSDL2_InitForVulkan', 'ImGui_ImplSDL2_InitForOpenGL',
                     'ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0)'):
            self.assertIn(call, block)

    def test_gl_msaa_preserves_state_cache_and_embed_evidence(self):
        from yougame_console_wiring_test import body
        gl = source('port/linux/renderer_gl.cpp')
        self.assertIn('invalidate_state();', body(gl, 'apply_msaa'))
        draw = body(gl, 'draw')
        self.assertIn('use_program(p); attributes(p); glBindFramebuffer(GL_FRAMEBUFFER, draw_fbo());', draw)
        present = body(gl, 'present')
        self.assertLess(present.index('resolve_msaa();'), present.index('picture(c,'))
        for token in ('hooks_.drawable_size', 'test_shot(w, h)', 'evidence.after_swap(',
                      'hooks_.after_swap(presented_)', 'yougame::parity::signals.presented(test_fill < 0)'):
            self.assertIn(token, present)
        self.assertIn('bind_vertex_array(vao_);', gl)

    def test_dxgi_probes_cannot_release_cover_or_pollute_present_metrics(self):
        for api in (11, 12):
            text = source(f'port/runtime/gx/gx_d3d{api}.cpp')
            self.assertIn('&& !yougame::embedded();', text)
            self.assertIn('if (test_only) return swapchain_->Present(0, DXGI_PRESENT_TEST);', text)
            self.assertIn('signals.presented(!test_only && SUCCEEDED(present_result) && frame.test_fill < 0)', text)
            self.assertIn('if (!test_only) pacing_diagnostics_.present(', text)
            self.assertIn('return swapchain_->Present(present.interval, present.flags);', text)
        self.assertIn('occluded_ && fg_applied_ <= 0 && !yougame::embedded()', source('port/runtime/gx/gx_d3d12.cpp'))

    def test_vulkan_ready_requires_successful_real_present(self):
        text = source('port/runtime/gx/gx_vulkan.cpp')
        self.assertIn('end_frame(presented, frame.test_fill < 0)', text)
        # v0.9.104 times the present call (host::perf Wait scope) around the same vkQueuePresentKHR.
        start = text.index('const VkResult r = [&] { host::perf::Scope wait_cost(host::perf::Stage::Wait); return vkQueuePresentKHR(queue_, &p); }();')
        tail = text[start:text.index('acquired_ = false;', start)]
        self.assertIn('if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)', tail)
        self.assertIn('yougame::parity::signals.presented(real_frame)', tail)
        self.assertIn('host::window_presented(presented_)', tail)
        self.assertIn('#ifdef MELEE_NO_SLIPPI\n#include "netplay_state.h"', text)

    def test_patch_new_offsets_follow_hunk_deltas(self):
        patch = source('sourceport/patches/melee-native.patch')
        count = 0
        for section in patch.split('diff --git ')[1:]:
            delta = 0
            for m in re.finditer(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@', section, re.M):
                old, old_n, new, new_n = (int(m[1]), int(m[2] or 1), int(m[3]), int(m[4] or 1))
                self.assertEqual(new, old + delta + (old_n == 0) - (new_n == 0), section.splitlines()[0])
                delta += new_n - old_n
                count += 1
        # Build 28 + v0.9.87: 10067; Phase B adds 6; Build 49.1 GX layouts add 2.
        # Optional Squad Strike adds 11 guarded hook hunks.
        # Build 53: upstream v0.9.97's community codes add 4 (V-cancel sound 1, full FoD reflections 3).
        # Build 56: upstream v0.9.99 adds 6 (stage skins in mnstagesw 4, Training fixes in gmtrainingmode 2).
        # Build 57: upstream v0.9.101 adds 3 net (lbcollision +3, itfreeze +1, hsd_3A76 -1).
        # Build 58: upstream v0.9.102 adds 1 (grizumi, the live Fountain of Dreams scenery).
        # Build 65: upstream v0.9.104 adds 9 (render semantics in cobj/dobj/lobj/tobj 6, Kirby hats in ftparts 2, gm_1601 1).
        # The 4-player Friends select adds 2 (the host-only Rules and Teams guards in mncharsel).
        # Build 95: upstream v0.9.112 adds 1 (gricemt: the Ice Climbers come on Icicle Mountain again).
        self.assertEqual(count, 10168)  # 20XX Melee 1.03 and the mid-match toggle: 2 more; tap jump off and tilt stick: 13 more (gmvs.c match start, ftCo_Jump/JumpAerial/JumpAerialF1/PassiveWall, ftCo_AttackS3/S4/Hi3/Hi4/Lw3/Lw4/Dash and their headers); 1.0.15: one more (a fighter's swinging parts with a bone missing, lb_00F9.c); 1.0.3: one more (added items start with no command script, item.c); 1.0.1: one more (the Training HUD switch, gm_1884.c, and the item script guard, itanimlist.c); 0.9.992: three more (Final Destination's background guard for skins without its animations, grlast.c); 0.9.990: one more for the rogueMelee Disable fix (main becad61b), one more (single player stock icon lists on an m-ex disc's icon table); 8 more (an m-ex disc's own menu under VS Mode: mnmain 7, gmvsmode 1); one more (TObj palette index past 255); 19 more (the 0.9.216 merge and the online menu description hook); build 103: two more (results screen and main menu); build 101: v0.9.120's two new patch hunks (97: v0.9.117's itzako hunk)


if __name__ == '__main__':
    unittest.main()
