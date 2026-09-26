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

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
}

int main() {
  namespace fs = std::filesystem;
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
  CHECK(options.dlss5_tuning.skin == 8.0f);
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
  if (g_failures == 0) std::printf("settings load: all checks passed\n");
  return g_failures == 0 ? 0 : 1;
}
