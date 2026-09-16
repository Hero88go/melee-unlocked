// State and body of the PC settings panel, shared by the D3D12 and D3D11 renderer wrappers.
// The panel itself is pure ImGui; only the platform/renderer bindings differ per backend.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gx_d3d12.h"
#include "host.h"            // host::PadState, which input_bindings.h uses without declaring
#include "input_bindings.h"
#include <array>

namespace gx {

struct SettingsState {
  bool open = false, saved = false;
  int volume = 0;
  std::array<float, 180> intervals{};
  unsigned cursor = 0;
  int rebind_action = -1;                                       // BindAction index while a "press a button" capture runs, -1 = none
  host::CaptureDevice rebind_kind = host::CaptureDevice::None;  // which device tab that capture belongs to
  int rebind_index = 0;                                         // pad / adapter port index for that tab
};

// ImGui context plus the Win32 platform backend; the renderer backend is set up by the caller.
void settings_context_create(void* window, bool open_at_startup);
void settings_context_destroy();
// Everything between ImGui::NewFrame() and ImGui::Render(); true when render configuration changed.
bool settings_frame(SettingsState& state, D3D12Options& options);

}  // namespace gx
