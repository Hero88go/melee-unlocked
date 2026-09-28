// port-settings.ini is read back exactly: a setting whose value has several words (the Custom video
// preset, a texture pack folder with a space) must not shift the settings after it.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx/pc_settings.h"
#include "host/window.h"
#include "host/host.h"
#include "host/input_bindings.h"
#include "gx/texture_pack.h"
#ifdef GX_DLSS5
#include "gx/gx_dlss5.h"
#endif
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <string>

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
}

int main() {
  namespace fs = std::filesystem;
  gx::RenderOptions aspect_options;
  aspect_options.widescreen = true;
  CHECK(gx::presented_aspect(aspect_options, 1920, 1080) == 16.0f / 9.0f);
  aspect_options.native_source = true;
  CHECK(gx::presented_aspect(aspect_options, 1920, 1080) == 73.0f / 60.0f);
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
  }
  gx::D3D12Options options;
  options.settings_path = path.string();
  int volume = 0;
  gx::load_pc_settings(options, volume);

  CHECK(options.efb_scale == 2);
  CHECK(!options.fod_reflections);
  CHECK(options.screen_space_ao == 0.65f);
  CHECK(options.api == gx::RenderApi::D3D11);                       // after both multi-word lines
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

  if (g_failures == 0) std::printf("settings load: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
