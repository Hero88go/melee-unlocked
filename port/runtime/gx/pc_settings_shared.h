// State and body of the PC settings panel, shared by the D3D12 and D3D11 renderer wrappers.
// The panel itself is pure ImGui; only the platform/renderer bindings differ per backend.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gx_d3d12.h"
#include "host.h"            // host::PadState, which input_bindings.h uses without declaring
#include "input_bindings.h"
#include <array>
#include <cstdint>

namespace gx {

struct SettingsState {
  bool open = false, saved = false;
  bool legacy_presentation = false;
  int legacy_saved_appearance = 0;
  // Panel movement follows GD's Melee hub_motion.py slide curves. Keep the
  // surface visible during its exit track instead of dropping it on toggle.
  bool panel_anim_target_open = false;
  bool panel_anim_initialized = false;
  float panel_anim_frame = 0.0f;
  float panel_slide_x = 720.0f;
  float panel_slide_start_x = 720.0f;
  bool dirty = false;   // a setting changed since the last write; saved once the control is released
  int volume = 0;
  std::array<float, 180> intervals{};
  unsigned cursor = 0;
  // Reflex's measured render latency, same rolling-buffer shape as intervals, for the performance
  // graph. Filled whether or not Reflex's low-latency mode is on (see gx_streamline.h).
  std::array<float, 180> latencies{};
  unsigned latency_cursor = 0;
  int rebind_action = -1;                                       // BindAction index while a "press a button" capture runs, -1 = none
  host::CaptureDevice rebind_kind = host::CaptureDevice::None;  // which device tab that capture belongs to
  int rebind_index = 0;                                         // pad / adapter port index for that tab
  bool running_d3d11 = false;                                   // which backend owns this panel, so it can say when a backend change needs a restart
  // Restart and Quit confirm rather than acting on the click: both end the current match, and the
  // panel is reachable mid-game.
  enum class Confirm { None, Restart, Quit };
  Confirm confirm = Confirm::None;
  // The Esc menu (back to game, settings, quit), for keyboards without an F1 key and so quitting
  // does not need Alt-Tab. menu_quit: showing "Quit Melee Unlocked?" instead of the buttons.
  bool menu_open = false, menu_quit = false;
  // A texture pack was switched on or off: the backend drops what it has uploaded so the next
  // draw rebuilds it with (or without) its replacement.
  bool textures_dirty = false;
  // Standalone settings window: the panel fills the OS window instead of floating inside one, so
  // what opens is the settings box itself rather than a box inside an empty frame.
  bool fill_window = false;
  int active_tab = 0;             // shared across overlay styles A and B
  bool home_focus_reset = false;  // discard stale ImGui navigation focus when reopening the menu
  int settings_controller_port = 0;  // most recently used controller while the settings panel is open
  int radial_selection = 0;       // sector chosen by the GameCube d-pad/left stick
  int8_t settings_stick_x = 0, settings_stick_y = 0;
  int content_anim_tab = -1;
  float content_anim_frame = 0.0f;
  bool clean_detail_open = false;
  float clean_home_frames = 0.0f;
  float clean_detail_frames = 0.0f;
  std::array<float, 7> clean_hover{};
  bool dashboard_detail_open = false;
  float dashboard_home_frames = 0.0f;
  std::array<float, 7> dashboard_hover{};
  std::array<float, 2> footer_hover{};
  bool radial_detail_open = false;
  bool wide_detail_open = false;
  std::array<float, 7> radial_hover{};
  bool gd_detail_open = false;
  float gd_screen_frames = 0.0f;
  // Native practice matchmaking popup. Its network state lives on the simulation thread; these
  // fields are render-only input/focus state.
  bool practice_open = false;
  bool practice_focus_code = false;
  bool practice_nav_active = false;
  bool practice_pad_armed = false;
  bool practice_a_was_down = false;
  bool practice_release_capture = false;
  uint32_t practice_generation = 0;
  char practice_code[19]{};
  char practice_error[96]{};
};

// ImGui context plus the Win32 platform backend; the renderer backend is set up by the caller.
void settings_context_create(void* window, bool open_at_startup);
void settings_context_destroy();
// Everything between ImGui::NewFrame() and ImGui::Render(); true when render configuration changed.
bool settings_frame(SettingsState& state, D3D12Options& options);
// Guest Settings menu slot 3 is the original game's unused row, made selectable
// during translation. Only a completed guest frame publishes its state here.
void settings_guest_options_frame(uint8_t menu, uint16_t selection, uint32_t buttons);

}  // namespace gx
