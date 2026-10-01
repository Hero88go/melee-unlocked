// port-settings.ini is read back exactly: a setting whose value has several words (the Custom video
// preset, a texture pack folder with a space) must not shift the settings after it.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx/pc_settings.h"
#include "host/window.h"
#include "host/host.h"
#include "host/input_bindings.h"
#include "host/audio_buffer_policy.h"
#include "host/audio_sample_conversion.h"
#include "host/lcancel.h"
#include "abi/mu_lcancel_flash.h"
#include "gx/gx_core.h"
#include "gx/texture_pack.h"
#include "host/mod_scan.h"
#include "hle/gecko_data.h"
#ifdef GX_DLSS5
#include "gx/gx_dlss5.h"
#endif
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <chrono>
#include <sstream>
#include <string>

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

std::string read_text(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}
// A "key value" line for this key, anywhere in a settings file.
bool has_key_line(const std::string& text, const std::string& key) {
  return text.rfind(key + " ", 0) == 0 || text.find("\n" + key + " ") != std::string::npos;
}
}

int main() {
  namespace fs = std::filesystem;
  host::AudioBufferPolicy audio;
  audio.configure(0, 40);
  CHECK(audio.gap(160, 32000) && audio.target_ms == 44);
  CHECK(!audio.gap(32000, 32000) && audio.target_ms == 44);
  audio.clean(320000, 32000);
  CHECK(audio.target_ms == 43);
  audio.configure(1, 5);
  CHECK(!audio.gap(160, 32000) && audio.target_ms == 5);
  audio.configure(0, -100);
  CHECK(audio.target_ms == 10);   // Auto's floor
  audio.configure(2, 8);
  CHECK(!audio.adaptive && !audio.gap(160, 32000) && audio.target_ms == 8);   // Exclusive stays fixed
  audio.configure(0, 10000);
  CHECK(!audio.gap(160, 32000) && audio.target_ms == 120);
  // Exclusive has no buffer control. Neither a saved 120 ms Low target nor the default
  // 40 ms may override its device-derived 18 ms floor. Low still respects the saved target.
  audio.configure_for_output(2, 40, 18);
  CHECK(!audio.adaptive && audio.target_ms == 18);
  audio.configure_for_output(2, 120, 18);
  CHECK(audio.target_ms == 18);
  audio.configure_for_output(2, 5, 18);
  CHECK(audio.target_ms == 18);
  audio.configure_for_output(1, 40, 18);
  CHECK(audio.target_ms == 40);
  audio.configure_for_output(1, 5, 18);
  CHECK(audio.target_ms == 18);
  // The driver receives signed packed PCM: silence, both polarities and both full-scale limits.
  const int16_t samples24[] = {0, 1, -1, INT16_MAX, INT16_MIN};
  const uint32_t expected24[] = {0x000000u, 0x000100u, 0xFFFF00u, 0x7FFF00u, 0x800000u};
  for (size_t i = 0; i < sizeof(samples24) / sizeof(samples24[0]); ++i) {
    uint8_t guarded[] = {0xA5, 0, 0, 0, 0x5A};
    host::audio_store_pcm24_le(guarded + 1, samples24[i]);
    CHECK(guarded[0] == 0xA5 && guarded[4] == 0x5A);
    CHECK((static_cast<uint32_t>(guarded[1]) | (static_cast<uint32_t>(guarded[2]) << 8) |
           (static_cast<uint32_t>(guarded[3]) << 16)) == expected24[i]);
  }
  gx::Frame event_frame{};
  CHECK(gx::frame_scene_draw(event_frame) == nullptr);
  event_frame.draws.resize(3);
  event_frame.draws[0].xf_regs[0x26] = 1; // orthographic UI
  CHECK(gx::frame_scene_draw(event_frame) == &event_frame.draws[1]);
  event_frame.draws[2].owner_player = 0;
  event_frame.draws[2].skinned = true;
  CHECK(gx::frame_scene_draw(event_frame) == &event_frame.draws[2]);
  event_frame.scene_major = 0x2B;
  CHECK(!gx::frame_in_match(event_frame) && gx::frame_has_widenable_scene(event_frame));
  event_frame.scene_minor = 2;
  CHECK(!gx::frame_in_match(event_frame) && gx::frame_has_widenable_scene(event_frame));
  event_frame.scene_minor = 1;
  CHECK(gx::frame_in_match(event_frame) && gx::frame_has_widenable_scene(event_frame));
  event_frame.scene_major = 0x0E;
  event_frame.scene_minor = 0;
  CHECK(!gx::frame_in_match(event_frame) && gx::frame_has_widenable_scene(event_frame));
  event_frame.scene_minor = 1;
  CHECK(gx::frame_in_match(event_frame) && gx::frame_has_widenable_scene(event_frame));
  event_frame.scene_minor = 3;
  CHECK(!gx::frame_in_match(event_frame) && gx::frame_has_widenable_scene(event_frame));
  // Widescreen presents every scene wide (the menus too: their cameras widen), except the
  // opening movie, a video on the 2D layer that would only be stretched.
  event_frame.scene_major = 0x01; event_frame.scene_minor = 0;
  CHECK(!gx::frame_in_match(event_frame) && gx::frame_has_widenable_scene(event_frame));
  event_frame.scene_major = 0x20;   // Home-Run Contest
  CHECK(gx::frame_has_widenable_scene(event_frame));
  event_frame.scene_major = 0x18;
  CHECK(!gx::frame_has_widenable_scene(event_frame));
  // "Visual effects": only decoration drawn by the world camera is skipped. A HUD drawn in
  // perspective by a camera of its own always draws (a mod's song title text neither tests nor
  // writes depth, and Minimal used to remove it).
  {
    gx::Frame match{};
    match.scene_major = 2; match.scene_minor = 2;
    match.draws.resize(4);
    const float world[6] = {3.066f, 0, 3.732f, 0, -6.1e-06f, -0.1f}, hud[6] = {2.167f, 0, 2.637f, 0, -0.00029f, -1.0f};
    for (int i = 0; i < 3; ++i) std::memcpy(&match.draws[i].xf_regs[0x20], world, sizeof world);
    std::memcpy(&match.draws[3].xf_regs[0x20], hud, sizeof hud);
    match.draws[0].owner_player = 0; match.draws[0].skinned = true;                                   // a fighter: the world camera
    match.draws[1].bp.reg[gx::BP_BLENDMODE] = 0x0429; match.draws[1].bp.reg[gx::BP_ZMODE] = 0x07;     // additive spark, depth tested
    match.draws[2].bp.reg[gx::BP_BLENDMODE] = 0x04A9; match.draws[2].bp.reg[gx::BP_ZMODE] = 0x00;     // overlay in the world
    match.draws[3].bp.reg[gx::BP_BLENDMODE] = 0x04A9; match.draws[3].bp.reg[gx::BP_ZMODE] = 0x00;     // HUD text
    const gx::EffectsFilter full = gx::effects_filter(match, 0), reduced = gx::effects_filter(match, 1),
                            minimal = gx::effects_filter(match, 2);
    for (const auto& d : match.draws) CHECK(!gx::skip_for_effects(full, d));
    CHECK(!gx::skip_for_effects(reduced, match.draws[0]) && gx::skip_for_effects(reduced, match.draws[1]));
    CHECK(!gx::skip_for_effects(reduced, match.draws[2]) && !gx::skip_for_effects(reduced, match.draws[3]));
    CHECK(!gx::skip_for_effects(minimal, match.draws[0]) && gx::skip_for_effects(minimal, match.draws[1]));
    CHECK(gx::skip_for_effects(minimal, match.draws[2]) && !gx::skip_for_effects(minimal, match.draws[3]));
    match.scene_minor = 0;   // character select: nothing is filtered outside a match
    CHECK(!gx::skip_for_effects(gx::effects_filter(match, 2), match.draws[1]));
  }
  gx::RenderOptions aspect_options;
  CHECK(aspect_options.te_options == 0x10u); // recognized TE save is enabled by default
  aspect_options.widescreen = true;
  CHECK(gx::presented_aspect(aspect_options, 1920, 1080) == 16.0f / 9.0f);
  aspect_options.native_source = true;
  CHECK(gx::presented_aspect(aspect_options, 1920, 1080) == 16.0f / 9.0f);
  aspect_options.true_widescreen = true;
  CHECK(gx::presented_aspect(aspect_options, 1920, 1080) == 16.0f / 9.0f);

  const fs::path path = fs::temp_directory_path() / "melee_unlocked_settings_load_test.ini";
  {
    std::ofstream f(path);
    // The order the game writes: the preset line sits above the controls.
    f << "fps -1\nscale 2\n"
         "custompreset 2 1 16 0 -1.000000 1\n"
         "texpackoff HD Textures Pack\n"
         "fodreflections 0\n"
         "ssao 0.65\n"
         "discord 0\n"
         "backend d3d11\n"
         "audio_mode 1\naudio_buffer_ms 17\n"
         "overlaystyle 2\nlegacymenu 1\noverlaypalette0 1\noverlaypalette2 3\noverlaypalette4 99\n"
#ifdef GX_DLSS5
         "dlss5intensity 10\n"
         "dlss5detail 7.5\n"
         "dlss5tone 10\n"
         "dlss5skin 8\n"
         "dlss5resolution 73\ndlss5passes 3\ndlss5downsample 2\ndlss5upsample 1\ndlss5reconstruction 1\n"
         "dlss5intensity nan\ndlss5detail inf\ndlss5tone -inf\ndlss5skin nan\n"
#endif
         "key_A 88\n"
         "port0 2\nport1 18\n"
         "portname1 GRAM_Slim\n";
    f << "mod_te_enabled 0\nmod_tmce_enabled 1\nmod_enabled abcdef0123456789 0\n";
  }
  gx::D3D12Options options;
  options.settings_path = path.string();
  int volume = 0;
  gx::load_pc_settings(options, volume);

  CHECK(options.mod_choices.at("te") == 0 && options.mod_choices.at("tmce") == 1);
  CHECK(source_port::mods::choices() == options.mod_choices);
  CHECK(gx::save_pc_settings(options, volume));
  gx::RenderOptions reloaded;
  reloaded.settings_path = path.string();
  gx::load_pc_settings(reloaded, volume);
  CHECK(reloaded.mod_choices == options.mod_choices);
  CHECK(reloaded.mod_choices.at("abcdef0123456789") == 0);

  CHECK(options.efb_scale == 2);
  CHECK(!options.fod_reflections);
  CHECK(options.screen_space_ao == 0.65f);
  CHECK(options.api == gx::RenderApi::D3D11);                       // after both multi-word lines
  CHECK(options.audio_mode == 1 && options.audio_buffer_ms == 17);
  CHECK(options.audio_asio_driver.empty() && options.audio_asio_buffer == 0);   // files without the ASIO keys
  CHECK(options.overlay_style == 2);
  CHECK(options.legacy_menu_enabled);
  CHECK(options.legacy_menu_style == 7); // existing preferences default to original layout
  CHECK(options.overlay_palettes[0] == 1 && options.overlay_palettes[2] == 3);
  CHECK(options.overlay_palettes[4] == 3);                           // invalid saved values clamp
#ifdef GX_DLSS5
  CHECK(options.dlss5_tuning.intensity == 10.0f);
  CHECK(options.dlss5_tuning.detail == 7.5f);
  CHECK(options.dlss5_tuning.tone == 10.0f);
  CHECK(options.dlss5_tuning.skin == 8.0f);                         // non-finite later lines ignored
  CHECK(options.dlss5_tuning.resolution_scale == 73 && options.dlss5_tuning.passes == 3);
  CHECK(options.dlss5_tuning.downsample_filter == 2 && options.dlss5_tuning.upsample_filter == 1);
  CHECK(options.dlss5_tuning.reconstruction == 1);
  {
    // Profiles: every field round-trips, old profiles get the default processing, bounds and bad
    // records are handled without touching the caller's tuning.
    const auto folder = fs::temp_directory_path() / ("melee_dlss5_profiles_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    gx::dlss5::profile_set_folder((folder / "settings.ini").string());
    CHECK(gx::dlss5::profile_save("roundtrip", options.dlss5_tuning));
    gx::dlss5::Tuning loaded;
    CHECK(gx::dlss5::profile_load("roundtrip", loaded));
    CHECK(!(loaded != options.dlss5_tuning));
    { std::ofstream f(folder / "Dlss5Profiles" / "legacy.txt"); f << "# Melee Unlocked DLSS 5 profile\nintensity 0.75\nstyle 2\n"; }
    CHECK(gx::dlss5::profile_load("legacy", loaded));
    CHECK(loaded.intensity == 0.75f && loaded.style == 2 && loaded.passes == 1 && loaded.resolution_scale == 100);
    { std::ofstream f(folder / "Dlss5Profiles" / "bounds.txt"); f << "resolution -20\npasses 999\ndownsample -1\nupsample 99\nreconstruction 99\n"; }
    CHECK(gx::dlss5::profile_load("bounds", loaded));
    CHECK(loaded.resolution_scale == 25 && loaded.passes == 4 && loaded.downsample_filter == 0 &&
          loaded.upsample_filter == 2 && loaded.reconstruction == 1);
    { std::ofstream f(folder / "Dlss5Profiles" / "invalid.txt"); f << "intensity nan\n"; }
    const auto before_invalid = loaded;
    CHECK(!gx::dlss5::profile_load("invalid", loaded));
    CHECK(!(before_invalid != loaded));
    { std::ofstream f(folder / "Dlss5Profiles" / "comments.txt");
      f << "# Custom look\npasses 3 # repeat evaluations\n# short\nresolution 67\nfuture_key anything goes here\nupsample 1\n"; }
    CHECK(gx::dlss5::profile_load("comments", loaded));
    CHECK(loaded.passes == 3 && loaded.resolution_scale == 67 && loaded.upsample_filter == 1);
    for (const char* bad : {"passes\nresolution 50\n", "passes 3junk\n", "automask maybe\n", "detail 1.5 other\n"}) {
      { std::ofstream f(folder / "Dlss5Profiles" / "malformed.txt"); f << bad; }
      const auto before = loaded;
      CHECK(!gx::dlss5::profile_load("malformed", loaded));
      CHECK(!(loaded != before));
    }
    loaded.detail = 1.23456776f;
    CHECK(gx::dlss5::profile_save("precision", loaded));
    const auto precise = loaded;
    CHECK(gx::dlss5::profile_load("precision", loaded));
    CHECK(!(precise != loaded));
    for (const char* name : {"roundtrip", "legacy", "bounds", "invalid", "comments", "malformed", "precision"})
      CHECK(gx::dlss5::profile_delete(name));
    std::error_code ec;
    fs::remove(folder / "Dlss5Profiles", ec); fs::remove(folder, ec);
  }
#endif
  CHECK(host::g_key_bindings.vk[(int)host::BindAction::A] == 88);
  CHECK(host::g_port_sources[0].kind == host::DeviceKind::XInputPad && host::g_port_sources[0].index == 0);
  CHECK(host::g_port_sources[1].kind == host::DeviceKind::HidPad && host::g_port_sources[1].index == 0);
  CHECK(host::g_port_device_names[1] == "GRAM_Slim");
  const auto off = gx::texpack::disabled_packs();
  CHECK(std::find(off.begin(), off.end(), "HD Textures Pack") != off.end());

  for (int saved : {6, 7, -1, 99}) {
    { std::ofstream f(path); f << "legacymenustyle " << saved << "\n"; }
    gx::load_pc_settings(options, volume);
    CHECK(options.legacy_menu_style == (saved == 6 ? 6 : 7));
    CHECK(options.overlay_style == 2); // legacy choice never replaces modern selection
  }

  { std::ofstream f(path); f << "te_options 0\nte_options2 0\n"; }
  gx::load_pc_settings(options, volume);
  CHECK(options.te_options == 0 && gx::RenderOptions::live_te_options() == 0);

  std::error_code ec;
  fs::remove(path, ec);
  // Low spec: on keeps the player's settings, off puts exactly those back.
  {
    gx::RenderOptions o;
    o.api = gx::RenderApi::D3D12; o.fps_cap = 144; o.efb_scale = 3; o.ssaa = 2; o.anisotropy = 8;
    o.effects_level = 1; o.dlss_mode = 4; o.subframe = gx::SubFrameMode::Authored;
    const gx::RenderOptions before = o;
    gx::apply_low_spec(o, true);
    CHECK(o.low_spec && o.api == gx::RenderApi::D3D11 && o.fps_cap == 60 && o.efb_scale == 1 && o.ssaa == 1 &&
          o.anisotropy == 1 && o.dlss_mode == 0 && o.subframe == gx::SubFrameMode::Off);
    gx::restore_low_spec(o);
    CHECK(!o.low_spec && o.api == before.api && o.fps_cap == before.fps_cap && o.efb_scale == before.efb_scale &&
          o.ssaa == before.ssaa && o.anisotropy == before.anisotropy && o.effects_level == before.effects_level &&
          o.dlss_mode == before.dlss_mode && o.subframe == before.subframe);
  }
  // ... and across a restart: the kept values come back from the settings file.
  {
    const fs::path low_path = fs::temp_directory_path() / "melee_unlocked_settings_lowspec_test.ini";
    {
      std::ofstream f(low_path);
      f << "lowspec 1\nbackend d3d11\nfps 60\nscale 1\nssaa 1\nanisotropy 1\n"
        << "lowspec_prev_backend d3d12\nlowspec_prev_fps 240\nlowspec_prev_scale 4\nlowspec_prev_ssaa 2\n"
        << "lowspec_prev_anisotropy 16\nlowspec_prev_effects 2\nlowspec_prev_dlss 3\nlowspec_prev_subframe 2\n";
    }
    gx::RenderOptions o;
    o.settings_path = low_path.string();
    int volume = 100;
    gx::load_pc_settings(o, volume);
    CHECK(o.low_spec);
    gx::restore_low_spec(o);
    CHECK(o.api == gx::RenderApi::D3D12 && o.fps_cap == 240 && o.efb_scale == 4 && o.ssaa == 2 &&
          o.anisotropy == 16 && o.effects_level == 2 && o.dlss_mode == 3 &&
          o.subframe == gx::SubFrameMode::AuthoredInterpolate);
    std::error_code ec;
    fs::remove(low_path, ec);
  }

  // Settings audit (0.8.5): Video and Overlays were regrouped (one Display choice, one Widescreen
  // choice, an Advanced group on each) and 20XX TE's screen rumble and input display were linked with
  // ours, all with the settings file's keys unchanged. A file in 0.8.1's format loads the same values,
  // saving writes every key it had, and the two new keys (the Advanced groups) default to closed.
  {
    const fs::path old_path = fs::temp_directory_path() / "melee_unlocked_settings_081_test.ini";
    {
      std::ofstream f(old_path);
      f << "fps 144\nscale 3\nfullscreen 1\nexclusivefullscreen 0\nvsync 0\nwidescreen 0\n"
           "fodreflections 0\ntruewidescreen 1\naspect 4\nwindow 1440x1080\nvolume 80\n"
           "performance 1\nshowfps 1\nshowvram 0\nshowping 1\nreflex 1\nreflexstats 1\nreflexflash 1\n"
           "pathtracing 1\nrayreconstruction 1\ninputoverlay 1\ninputoverlayports 3\nlabview 1\nlabskipscene 0\n"
           "dumptextures 1\nnoscreenshake 1\nte_options 30\nte_options2 2100\noverlaystyle 3\n";
    }
    gx::RenderOptions o;
    o.settings_path = old_path.string();
    int vol = 0;
    gx::load_pc_settings(o, vol);
    CHECK(o.fps_cap == 144 && o.efb_scale == 3 && o.fullscreen && !o.exclusive_fullscreen && !o.vsync);
    CHECK(!o.widescreen && o.true_widescreen && o.aspect == gx::AspectMode::Stretch && !o.fod_reflections);
    CHECK(o.window_pinned && o.window_w == 1440 && o.window_h == 1080 && vol == 80);
    CHECK(o.performance_overlay && o.show_fps && o.show_ping && o.reflex_mode == 1 && o.reflex_stats && o.reflex_flash);
    CHECK(o.path_tracing == gx::RenderOptions::kPathTracingAvailable &&
          o.ray_reconstruction == gx::RenderOptions::kPathTracingAvailable);   // loaded as before
    CHECK(o.input_overlay && o.input_overlay_ports == 3 && o.lab_view && !o.lab_skip_scene && o.dump_textures);
    CHECK(gecko::option_no_screen_shake && o.te_options == 0x30u && o.te_options2 == 0x2100u && o.overlay_style == 3);
    CHECK(gx::save_pc_settings(o, vol));
    const std::string written = read_text(old_path);
    for (const char* key : {"fps", "scale", "fullscreen", "exclusivefullscreen", "vsync", "widescreen", "fodreflections",
                            "truewidescreen", "aspect", "window", "volume", "performance", "showfps", "showvram",
                            "showping", "reflex", "reflexstats", "reflexflash", "pathtracing", "rayreconstruction",
                            "inputoverlay", "inputoverlayports", "labview", "labskipscene", "dumptextures",
                            "noscreenshake", "te_options", "te_options2", "overlaystyle"})
      CHECK(has_key_line(written, key));
    CHECK(written.find("\nadvancedvideo 0\n") != std::string::npos);
    CHECK(written.find("\nadvancedoverlays 0\n") != std::string::npos);
    gx::RenderOptions again;
    again.settings_path = old_path.string();
    gx::load_pc_settings(again, vol);
    CHECK(again.fullscreen == o.fullscreen && again.exclusive_fullscreen == o.exclusive_fullscreen &&
          again.widescreen == o.widescreen && again.true_widescreen == o.true_widescreen && again.aspect == o.aspect);
    CHECK(again.reflex_flash == o.reflex_flash && again.reflex_stats == o.reflex_stats &&
          again.dump_textures == o.dump_textures && again.lab_skip_scene == o.lab_skip_scene &&
          again.input_overlay == o.input_overlay && again.fod_reflections == o.fod_reflections &&
          again.te_options == o.te_options && again.te_options2 == o.te_options2);
    std::error_code ec;
    fs::remove(old_path, ec);
  }
  // One Display choice and one Widescreen choice: a file with both of a pair on (possible only by
  // hand) loads as the one the renderers already let win, exclusive and the Slippi code.
  {
    const fs::path pair_path = fs::temp_directory_path() / "melee_unlocked_settings_pairs_test.ini";
    { std::ofstream f(pair_path); f << "fullscreen 1\nexclusivefullscreen 1\nwidescreen 1\ntruewidescreen 1\n"; }
    gx::RenderOptions o;
    o.settings_path = pair_path.string();
    int vol = 0;
    gx::load_pc_settings(o, vol);
    CHECK(o.exclusive_fullscreen && !o.fullscreen);
    CHECK(o.widescreen && !o.true_widescreen);
    CHECK(gx::presented_aspect(o, 1920, 1080) == 16.0f / 9.0f);
    std::error_code ec;
    fs::remove(pair_path, ec);
  }
  // The Advanced groups remember being opened, and a file without their keys reads as closed again.
  {
    const fs::path adv_path = fs::temp_directory_path() / "melee_unlocked_settings_advanced_test.ini";
    { std::ofstream f(adv_path); f << "advancedvideo 1\nadvancedoverlays 0\n"; }
    gx::RenderOptions o;
    o.settings_path = adv_path.string();
    int vol = 0;
    gx::load_pc_settings(o, vol);
    CHECK(gx::save_pc_settings(o, vol));
    std::string written = read_text(adv_path);
    CHECK(written.find("\nadvancedvideo 1\n") != std::string::npos);
    CHECK(written.find("\nadvancedoverlays 0\n") != std::string::npos);
    { std::ofstream f(adv_path); f << "advancedoverlays 1\n"; }
    gx::load_pc_settings(o, vol);
    CHECK(gx::save_pc_settings(o, vol));
    written = read_text(adv_path);
    CHECK(written.find("\nadvancedvideo 0\n") != std::string::npos);
    CHECK(written.find("\nadvancedoverlays 1\n") != std::string::npos);
    std::error_code ec;
    fs::remove(adv_path, ec);
  }
  // Reloading preferences replaces the texture-pack choice list rather than doubling it.
  {
    const fs::path path = fs::temp_directory_path() / "melee_texture_reload_test.ini";
    { std::ofstream out(path); out << "texpackoff HD Textures\ntexpackoff HD Textures\n"; }
    gx::RenderOptions o;
    o.settings_path = path.string();
    int volume = 0;
    for (int i = 0; i < 4; ++i) {
      gx::load_pc_settings(o, volume);
      CHECK(gx::texpack::disabled_packs() == std::vector<std::string>{"HD Textures"});
      CHECK(gx::save_pc_settings(o, volume));
    }
    { std::ofstream out(path); out << "volume 0\n"; }
    gx::load_pc_settings(o, volume);
    CHECK(gx::texpack::disabled_packs().empty());
    std::error_code ec;
    fs::remove(path, ec);
  }
  // Trigger values per family: saved only when changed, read back, clamped; a file without them
  // leaves every trigger at Full (255) and saving writes no trigger line.
  {
    const fs::path trig_path = fs::temp_directory_path() / "melee_unlocked_settings_trigger_test.ini";
    { std::ofstream f(trig_path); f << "trigger_switch_l 100\ntrigger_xbox_r 999\n"; }
    gx::RenderOptions o;
    o.settings_path = trig_path.string();
    int vol = 0;
    gx::load_pc_settings(o, vol);
    CHECK(host::g_deadzones[(size_t)host::PadFamily::Switch].trig_l == 100);
    CHECK(host::g_deadzones[(size_t)host::PadFamily::Switch].trig_r == 255);
    CHECK(host::g_deadzones[(size_t)host::PadFamily::Xbox].trig_r == 255);   // clamped
    CHECK(gx::save_pc_settings(o, vol));
    host::g_deadzones[(size_t)host::PadFamily::Switch].trig_l = 255;
    gx::RenderOptions again;
    again.settings_path = trig_path.string();
    gx::load_pc_settings(again, vol);
    CHECK(host::g_deadzones[(size_t)host::PadFamily::Switch].trig_l == 100);
    host::g_deadzones[(size_t)host::PadFamily::Switch].trig_l = 255;
    CHECK(gx::save_pc_settings(again, vol));
    std::string text;
    { std::ifstream f(trig_path); text.assign(std::istreambuf_iterator<char>(f), {}); }
    CHECK(text.find("trigger_") == std::string::npos);
    std::error_code ec;
    fs::remove(trig_path, ec);
  }
  // Audio mode 3 (ASIO) with its driver name (spaces) and buffer round-trips; out-of-range modes clamp.
  {
    const fs::path asio_path = fs::temp_directory_path() / "melee_unlocked_settings_asio_test.ini";
    { std::ofstream f(asio_path); f << "audio_mode 3\naudio_asio_driver MOTU M Series\naudio_asio_buffer 64\n"; }
    gx::RenderOptions o;
    o.settings_path = asio_path.string();
    int vol = 0;
    gx::load_pc_settings(o, vol);
    CHECK(o.audio_mode == 3 && o.audio_asio_driver == "MOTU M Series" && o.audio_asio_buffer == 64);
    CHECK(gx::save_pc_settings(o, vol));
    gx::RenderOptions again;
    again.settings_path = asio_path.string();
    gx::load_pc_settings(again, vol);
    CHECK(again.audio_mode == 3 && again.audio_asio_driver == "MOTU M Series" && again.audio_asio_buffer == 64);
    { std::ofstream f(asio_path); f << "audio_mode 7\n"; }
    gx::RenderOptions high;
    high.settings_path = asio_path.string();
    gx::load_pc_settings(high, vol);
    CHECK(high.audio_mode == 3);
    std::error_code ec;
    fs::remove(asio_path, ec);
  }

  // Each mode keeps its Windows endpoint and ASIO preference independently. Opening a
  // missing driver later must not erase the requested driver or the selected buffer.
  {
    const fs::path path = fs::temp_directory_path() /
      ("melee_audio_choice_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
    const int choices[] = {0, 64, 96, 128, 192, 256, 384, 512};
    const std::string endpoint = "{0.0.0.00000000}.{12345678-1234-1234-1234-123456789abc}";
    const std::string driver = "Unavailable Test ASIO Driver";
    int volume = 0;
    for (int mode = 0; mode < 4; ++mode) for (int buffer : choices) {
      { std::ofstream out(path); out << "audio_mode " << mode << "\naudio_buffer_ms 17\naudio_device "
        << endpoint << "\naudio_asio_driver " << driver << "\naudio_asio_buffer " << buffer << "\nvolume 0\n"; }
      gx::RenderOptions first;
      first.settings_path = path.string();
      gx::load_pc_settings(first, volume);
      CHECK(first.audio_mode == mode && first.audio_buffer_ms == 17);
      CHECK(first.audio_device == endpoint && first.audio_asio_driver == driver && first.audio_asio_buffer == buffer);
      CHECK(gx::save_pc_settings(first, volume));
      gx::RenderOptions restarted;
      restarted.settings_path = path.string();
      gx::load_pc_settings(restarted, volume);
      CHECK(restarted.audio_mode == mode && restarted.audio_buffer_ms == 17);
      CHECK(restarted.audio_device == endpoint && restarted.audio_asio_driver == driver && restarted.audio_asio_buffer == buffer);
    }
    const int requested_buffers[] = {-4, 1, 0, 33, 2049};
    const int expected_buffers[] = {32, 32, 0, 33, 2048};
    for (size_t i = 0; i < sizeof(requested_buffers) / sizeof(requested_buffers[0]); ++i) {
      { std::ofstream out(path); out << "audio_asio_buffer " << requested_buffers[i] << "\n"; }
      gx::RenderOptions o;
      o.settings_path = path.string();
      gx::load_pc_settings(o, volume);
      CHECK(o.audio_asio_buffer == expected_buffers[i]);
    }
    { std::ofstream out(path); out << "audio_mode -1\naudio_asio_buffer nonsense\naudio_asio_driver "
                                  << driver << "\naudio_buffer_ms 23\n"; }
    gx::RenderOptions malformed;
    malformed.settings_path = path.string();
    gx::load_pc_settings(malformed, volume);
    CHECK(malformed.audio_mode == 0 && malformed.audio_asio_buffer == 0);
    CHECK(malformed.audio_asio_driver == driver && malformed.audio_buffer_ms == 23);
    std::error_code ec;
    fs::remove(path, ec);
  }

  // Old flash keys keep their behavior; the explicit chooser is independent of file key order.
  {
    const fs::path flash_path = fs::temp_directory_path() / "melee_unlocked_lcancel_flash_test.ini";
    int vol = 0;
    for (int mode = 0; mode < 5; ++mode) for (int color = 0; color < 3; ++color) {
      for (int order = 0; order < 2; ++order) {
        const std::string legacy = "te_options2 40200\nlcancelindicator 1\n";
        const std::string explicit_choice = "lcancel_flash_mode " + std::to_string(mode) +
                                           "\nlcancel_success_flash " + std::to_string(color) + "\n";
        { std::ofstream f(flash_path); f << (order ? legacy + explicit_choice : explicit_choice + legacy); }
        gx::RenderOptions o;
        o.settings_path = flash_path.string();
        gx::load_pc_settings(o, vol);
        CHECK(mu_lcancel_flash_mode(o.te_options2, lcancel::indicator_enabled()) == mode);
        CHECK(mu_lcancel_success_color(o.te_options2) == color);
        CHECK((o.te_options2 & 0x40000u) != 0);   // the stored lock is not discarded at load
        CHECK(lcancel::indicator_enabled() == (mode == MU_LCFLASH_MU_MISSED));
        CHECK(gx::save_pc_settings(o, vol));
        gx::RenderOptions again;
        again.settings_path = flash_path.string();
        gx::load_pc_settings(again, vol);
        CHECK(again.te_options2 == o.te_options2);
        CHECK(mu_lcancel_flash_mode(again.te_options2, lcancel::indicator_enabled()) == mode);
      }
    }
    { std::ofstream f(flash_path); f << "te_options2 200\nlcancelindicator 1\n"; }
    gx::RenderOptions legacy_te;
    legacy_te.settings_path = flash_path.string();
    gx::load_pc_settings(legacy_te, vol);
    CHECK(legacy_te.te_options2 == 0x200u && !lcancel::indicator_enabled());
    CHECK(mu_lcancel_flash_mode(legacy_te.te_options2, 0) == MU_LCFLASH_TE_BOTH);
    CHECK(mu_lcancel_success_color(legacy_te.te_options2) == MU_LCFLASH_SUCCESS_WHITE);
    { std::ofstream f(flash_path); f << "lcancelindicator 1\n"; }
    gx::RenderOptions legacy_mu;
    legacy_mu.settings_path = flash_path.string();
    gx::load_pc_settings(legacy_mu, vol);
    CHECK(lcancel::indicator_enabled() && legacy_mu.te_options2 == 0);
    std::error_code ec;
    fs::remove(flash_path, ec);
  }

  if (g_failures == 0) std::printf("settings load: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
