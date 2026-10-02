// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gx_core.h"
#include "render_options.h"
#include <memory>
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D11Device;
struct ID3D11DeviceContext;
namespace gx {
void load_pc_settings(RenderOptions& options, int& volume);
// Same atomic writer used by the panel, for startup choices and settings validation.
bool save_pc_settings(const RenderOptions& options, int volume);
// Current letterboxed gameplay viewport aspect, used to keep overlays out of the black bars.
void settings_set_game_aspect(float aspect);
// Copy the same simulation frame's HUD anchors used by the presented image.
void settings_set_hud_snapshot(const Frame& frame);
// True once after a texture pack is switched on or off, so the backend can drop the textures it
// uploaded under the old set. Reading it clears it.
bool settings_textures_dirty();
// Standalone settings window: the panel fills the OS window instead of floating inside it.
void settings_fill_window(bool on);
// True in the launcher's Settings window: the panel is the window and is always open there,
// whatever the saved "open the overlay at startup" choice is.
bool settings_fills_window();
// True in the launcher's Settings window: the panel is the window and is always open there,
// whatever the saved "open the overlay at startup" choice is.
bool settings_fills_window();
// Standalone settings window on a scaled display: its monitor's DPI over 96 (2 at 200 percent). The
// panel is laid out in 96 DPI units and drawn this much larger, with its text rasterised at the
// final size. Stays 1 in the game window, which is never asked to scale this way.
void settings_set_window_scale(float scale);
// The tab the panel opens on the first time it is drawn (0 Video to 7 Mods), for --settings-tab.
// Negative, the default, leaves the saved behaviour.
void settings_set_initial_tab(int tab);
// True once after the panel asks to close. The standalone window has nothing to return to, so that
// is its cue to exit. Reading it clears it.
bool settings_close_requested();
class PcSettingsUI {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  PcSettingsUI(void* window, ID3D12Device* device, ID3D12CommandQueue* queue, const RenderOptions& options);
  ~PcSettingsUI();
  bool begin(RenderOptions& options); // returns true when render configuration changed
  void draw(ID3D12GraphicsCommandList* list);
};
// The same panel on the D3D11 backend (its own small ImGui renderer; the panel body is shared).
class PcSettingsUID3D11 {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  PcSettingsUID3D11(void* window, ID3D11Device* device, ID3D11DeviceContext* context, const RenderOptions& options);
  ~PcSettingsUID3D11();
  bool begin(RenderOptions& options);
  void draw();   // into whatever render target the caller has bound
};
}
