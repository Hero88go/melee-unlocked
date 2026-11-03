"""Source regressions for v0.9.62's loader, rendering and settings merge boundaries.

No game, graphics context, network or platform build is needed. Behavioral policy
and Windows/runtime validation remain in their existing release stages.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
CONFLICTED = (
    "port/app/main.cpp", "port/app/source_host.cpp",
    "port/linux/full/gx_backend_linux.cpp", "port/linux/renderer_gl.cpp",
    "port/linux/window_sdl.inl", "port/runtime/gx/pc_settings.cpp",
    "port/runtime/host/host.cpp", "port/runtime/host/host.h",
)


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def conditional_source(text, defines):
    """Select the ifdef/ifndef branches used at these specific merge boundaries."""
    stack, output, enabled = [], [], True
    for line in text.splitlines():
        directive = re.match(r"\s*#\s*(ifdef|ifndef|else|endif|if|elif)\b(.*)", line)
        if not directive:
            if enabled:
                output.append(line)
            continue
        kind, argument = directive.groups()
        if kind in ("ifdef", "ifndef"):
            condition = argument.split()[0] in defines
            if kind == "ifndef":
                condition = not condition
            stack.append((enabled, condition))
            enabled = enabled and condition
        elif kind == "else":
            parent, condition = stack[-1]
            enabled = parent and not condition
        elif kind == "endif":
            enabled = stack.pop()[0]
        else:
            raise AssertionError("Extend branch selector for: " + line)
    if stack:
        raise AssertionError("Unclosed preprocessor branch")
    return "\n".join(output)


class YouGame0962MergeTest(unittest.TestCase):
    def test_all_merge_boundaries_have_no_markers(self):
        for path in CONFLICTED:
            with self.subTest(path=path):
                self.assertIsNone(re.search(r"^(?:<{7}|={7}|>{7})(?: |$)", source(path), re.M))

    def test_disc_open_stays_single_before_profile_and_look_scan(self):
        main = source("port/app/main.cpp")
        startup = main.split("if (settings_window_only) {", 1)[1]
        startup = startup.split("remember_iso(o.iso);", 1)[0]
        self.assertEqual(startup.count("if (!host::disc_open(o.iso))"), 1)
        self.assertLess(startup.index("host::disc_open(o.iso)"),
                        startup.index("source_port::set_mod_profile("))
        self.assertLess(startup.index("source_port::set_mod_profile("),
                        startup.index("host::cosmetics::scan_look_packs();"))
        self.assertIn("source_port::mods::identify_vanilla(", startup)
        self.assertIn(' / "VanillaOnly"', startup)

    def test_exact_looks_run_as_upstream_in_yougame(self):
        main = source("port/app/main.cpp")
        # Luis + Hero 10-28: YouGame runs exact looks with upstream's own validators and online eligibility.
        self.assertNotIn("exact looks are off in YouGame", main)
        block = main.split("  // An exact look (Diet Classic, Diet 64, Crystal) serves its pack", 1)[1]
        block = block.split("if (host::gameplay::vanilla_only())", 1)[0]
        self.assertNotIn("yougame::active()", block)
        self.assertLess(block.index("host::cosmetics::scan_look_packs();"),
                        block.index("host::cosmetics::set_active_look_pack(active);"))
        self.assertEqual(main.count("host::cosmetics::set_active_look_pack("), 1)
        self.assertIn('std::getenv("MELEE_BUILD_LOOKS"); looks && *looks) {', main)

    def test_exact_look_overrides_keep_upstream_online_eligibility(self):
        cosmetics = source("port/runtime/host/cosmetic_mods.cpp")
        self.assertNotIn("exact_look_policy", cosmetics)
        self.assertNotIn("offline only in YouGame", cosmetics)
        self.assertNotIn("yougame::", cosmetics)
        exact = cosmetics.split("  // An exact look: the fighter data files", 1)[1]
        exact = exact.split("  // A slot's own portrait", 1)[0]
        self.assertEqual(exact.count("RuntimeAsset{"), 2)
        for kind, value in (("kLookFighterKind", "look_fighter_tables"),
                            ("kLookFileKind", "look_file")):
            with self.subTest(kind=kind):
                self.assertIn('constexpr const char* ' + kind + ' = "' + value + '";', cosmetics)
                self.assertRegex(exact, r"file\.size,\s*true,\s*" + kind + r",")
        read = cosmetics.split("OverrideRead read(", 1)[1].split("std::atomic<OnlineProbe>", 1)[0]
        fallback = read.split("if (!found->second.online_allowed && online_now) {", 1)[1]
        fallback = fallback.split("return OverrideRead::Success;", 1)[0]
        self.assertIn("host::disc_read(vanilla_file_start + file_offset, dst, disc_size)", fallback)
        self.assertIn('found->second.target_path != "nr_name.ssm"', fallback)
        self.assertNotIn("stage_read_pin::pick", fallback)
        self.assertLess(read.index("if (!found->second.online_allowed && online_now)"),
                        read.index("stage_read_pin::pick"))
        scan = cosmetics.split("void scan_look_packs() {", 1)[1].split("void set_active_look_pack", 1)[0]
        self.assertNotIn("yougame::active()", scan)

    def test_new_snapshot_uses_each_platforms_game_export_resolver(self):
        host = source("port/app/source_host.cpp")
        loader = host.split("#ifdef __APPLE__\n  std::string mac_error;", 1)[1]
        loader = "#ifdef __APPLE__\n  std::string mac_error;" + loader.split("  if (!entry)", 1)[0]
        for defines, resolver in (({"__APPLE__"}, "g_mac_image.symbol(name)"),
                                  ({"_WIN32"}, "sandboxed ? image_export(module, name)"),
                                  (set(), "GetProcAddress(module, name)")):
            with self.subTest(defines=defines):
                active = conditional_source(loader, defines)
                self.assertEqual(active.count("auto game_proc ="), 1)
                self.assertIn(resolver, active)
                for symbol in ("mu_game_entry", "mu_look_snapshot_v1", "mu_look_items_v1",
                               "mu_user_gecko_write", "mu_user_gecko_read"):
                    self.assertIn('game_proc("' + symbol + '")', active)
        retrace = host.split("void native_retrace() {", 1)[1].split("void h_gx_fifo", 1)[0]
        self.assertLess(retrace.index("g_game.retrace();"), retrace.index("g_look_snapshot(&snapshot);"))
        self.assertIn("g_look_snapshot && lab::styles_on()", retrace)
        self.assertIn("lab::native_frame(snapshot, g_look_items ? &items : nullptr);", retrace)

    def test_shader_workers_and_gl_headers_stay_platform_specific(self):
        renderer = source("port/linux/renderer_gl.cpp")
        for mac in (False, True):
            for settings in (False, True):
                with self.subTest(mac=mac, settings=settings):
                    defines = {"__APPLE__"} if mac else {"MELEE_LINUX", "MELEE_LINUX_X11_EMBED"}
                    if settings:
                        defines.add("GX_PC_SETTINGS")
                    active = conditional_source(renderer, defines)
                    if mac:
                        self.assertIn("shader_translation::Queue translations_;", active)
                        self.assertIn("translations_.stop();", active)
                        self.assertIn("<OpenGL/gl3ext.h>", active)
                        self.assertNotIn("GLEW_", active)
                        self.assertNotIn("translate_shader(", active)
                        self.assertNotIn("translate_worker()", active)
                    else:
                        self.assertIn("void translate_shader(", active)
                        self.assertIn("void translate_worker()", active)
                        self.assertIn("stop_translators();", active)
                        self.assertNotIn("shader_translation::Queue", active)
                    self.assertIn("if (!p) {", active)  # Pending programs cannot be drawn.
                    self.assertIn("gx::skip_for_lightweight(", active)
                    self.assertIn("captures(capture_sequence_)", active)

    def test_new_render_passes_restore_cached_game_state(self):
        renderer = source("port/linux/renderer_gl.cpp")
        for begin, end in (("GLuint white_texture()", "void clear("),
                           ("GLuint video_texture(", "void copy_quad("),
                           ("void draw_video_layer(", "void border_art(")):
            with self.subTest(begin=begin):
                block = renderer.split(begin, 1)[1].split(end, 1)[0]
                self.assertIn("invalidate_state();", block)
        self.assertIn("capability(emitted_.cull, GL_CULL_FACE, false); glPolygonMode", renderer)
        self.assertIn("capture_sequence_ = frame.sequence; present(c, frame);", renderer)
        active = conditional_source(renderer, {"__APPLE__", "GX_PC_SETTINGS", "YOUGAME_AGENT_TEST_HOOK"})
        self.assertIn("record_present();", active)
        self.assertIn("hooks_.after_swap(presented_)", active)
        self.assertIn("frame.source_retrace, frame.scene_major, frame.scene_minor", active)

    def test_settings_keep_manual_gamepad_focus_and_cursor_ownership(self):
        settings = source("port/runtime/gx/pc_settings.cpp")
        self.assertIn("ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, nullptr, 0);", settings)
        self.assertIn("io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;", settings)
        self.assertIn("state.menu_quit_focus_pending", settings)
        window = source("port/linux/window_sdl.inl")
        gained = window.split("case SDL_WINDOWEVENT_FOCUS_GAINED:", 1)[1].split("return;\n        }", 1)[0]
        self.assertLess(gained.index("if (g_x11_embed && !g_embed_focus_requested) return;"),
                        gained.index("linux_compat::set_window_focused(true);"))
        self.assertIn("g_cursor_reapply = true;", gained)
        self.assertIn("g_activated.store(true);", gained)

    def test_both_simulation_measurement_interfaces_survive(self):
        header = source("port/runtime/host/host.h")
        host = source("port/runtime/host/host.cpp")
        for name in ("last_sim_frame_ms", "last_sim_frame_work_ms", "report_sim_work"):
            with self.subTest(name=name):
                self.assertEqual(header.count(name + "();"), 1)
                self.assertEqual(host.count(name + "() {"), 1)
        self.assertIn("g_last_sim_work_ms = g_last_sim_ms - g_sim_costs[SIM_WAIT] * 1000.0;", host)
        self.assertIn("if (g_sim_times) std::fflush(g_sim_times);", host)


if __name__ == "__main__":
    unittest.main()
