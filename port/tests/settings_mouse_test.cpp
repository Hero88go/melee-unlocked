// Real ImGui input processing, no platform backend, renderer, adapter or game process.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings_imgui_input.h"
#include "imgui_internal.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

namespace {
struct Menu {
  ImGuiID ids[3]{};
  int clicks[3]{};
  bool hovered[3]{};
  float dt;
  bool keyboard;

  Menu(bool old_input, float frame_time, bool use_keyboard = false)
      : dt(frame_time), keyboard(use_keyboard) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(640, 240);
    gx::settings_input::configure(io); // The production configuration, not a test substitute.
    if (old_input) io.ConfigInputTrickleEventQueue = false;
    io.Fonts->Build();
    io.AddMousePosEvent(120, 105);
    frame(); frame(); frame();
  }
  ~Menu() { ImGui::DestroyContext(); }

  void frame(unsigned buttons = 0, int stick_x = 0, bool nav_active = true) {
    auto& io = ImGui::GetIO();
    // settings_frame's backend boundary and armed, routed GameCube pad mapping. The platform
    // backend has no gamepad producer now. Mouse events queued before this call are untouched.
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    if (nav_active) {
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
      io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
      io.AddKeyEvent(ImGuiKey_GamepadStart, (buttons & 0x1000) != 0);
      io.AddKeyEvent(ImGuiKey_GamepadBack, (buttons & 0x10) != 0);
      io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (buttons & 0x100) != 0);
      io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (buttons & 0x200) != 0);
      io.AddKeyEvent(ImGuiKey_GamepadDpadUp, (buttons & 8) != 0);
      io.AddKeyEvent(ImGuiKey_GamepadDpadDown, (buttons & 4) != 0);
      io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (buttons & 1) || stick_x < -40);
      io.AddKeyEvent(keyboard ? ImGuiKey_RightArrow : ImGuiKey_GamepadDpadRight,
                     (buttons & 2) || stick_x > 40);
    }
    io.DeltaTime = dt;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(600, 180), ImGuiCond_Always);
    ImGui::Begin("##menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    for (int i = 0; i < 3; ++i) {
      ImGui::PushID(i);
      ImGui::SetCursorPos(ImVec2(20.f + i * 180.f, 60));
      // Same default click/release behavior and explicit navigation as the F1 menu.
      if (ImGui::InvisibleButton("##action", ImVec2(160, 50), ImGuiButtonFlags_EnableNav))
        ++clicks[i];
      ids[i] = ImGui::GetItemID();
      hovered[i] = ImGui::IsItemHovered();
      if (i == 0) ImGui::SetItemDefaultFocus();
      ImGui::PopID();
    }
    ImGui::End();
    ImGui::Render();
    CHECK(ImGui::GetCurrentContext()->NavWindowingTarget == nullptr);
  }

  void navigate() {
    CHECK(hovered[0]);
    frame(2); frame(); frame();
    const auto& g = *ImGui::GetCurrentContext();
    CHECK(g.NavId == ids[1] && g.NavCursorVisible && g.NavHighlightItemUnderNav);
    CHECK(!hovered[0]);
  }
};

void handoff(bool old_input, bool move, bool fast_click, float dt, bool keyboard = false) {
  Menu menu(old_input, dt, keyboard);
  menu.navigate();
  auto& io = ImGui::GetIO();
  auto& g = *ImGui::GetCurrentContext();
  if (move) {
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMousePosEvent(123, 105);
    menu.frame();
    // This already works before the fix: do not misreport a permanent core hover lockout.
    CHECK(!g.NavHighlightItemUnderNav && menu.hovered[0]);
  }
  io.AddMouseButtonEvent(0, true);
  if (fast_click) io.AddMouseButtonEvent(0, false);
  menu.frame();
  const bool lost = old_input && fast_click;
  if (lost) {
    CHECK(!io.MouseClicked[0] && !io.MouseDown[0]);
    if (!move) CHECK(g.NavHighlightItemUnderNav && !menu.hovered[0]);
  } else {
    // A stationary click, as well as movement, hands the first frame back to the mouse.
    CHECK(io.MouseClicked[0] && !g.NavHighlightItemUnderNav && menu.hovered[0]);
  }
  if (!fast_click) io.AddMouseButtonEvent(0, false);
  menu.frame(); menu.frame();
  CHECK(menu.clicks[0] == (lost ? 0 : 1));
  CHECK(menu.clicks[1] == 0 && menu.clicks[2] == 0);
  if (!old_input) {
    // Mouse ownership does not disarm the pad: stick navigation and A still work afterward.
    menu.frame(0, 80); menu.frame(); menu.frame();
    CHECK(g.NavId == menu.ids[1] && g.NavHighlightItemUnderNav);
    menu.frame(0x100); menu.frame();
    CHECK(menu.clicks[1] == 1);
    menu.frame(0, 80);
    menu.frame(0, 0, false); // Closed/disconnected/unarmed must release previous pad keys.
    CHECK(!ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight));
    CHECK(!ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown));
  }
}

void held_stick_handoff() {
  Menu menu(false, 1.f / 60.f);
  menu.navigate();
  menu.frame(0, 80);
  auto& io = ImGui::GetIO();
  io.AddMousePosEvent(125, 105);
  io.AddMouseButtonEvent(0, true);
  menu.frame(0, 80);
  CHECK(!ImGui::GetCurrentContext()->NavHighlightItemUnderNav && menu.hovered[0]);
  io.AddMouseButtonEvent(0, false);
  menu.frame(0, 80);
  CHECK(menu.clicks[0] == 1);
}
} // namespace

int main() {
  for (float dt : {1.f / 60.f, 0.25f})
    for (bool move : {false, true})
      for (bool fast : {false, true}) {
        handoff(true, move, fast, dt);  // Verify the old lost-click failure explicitly.
        handoff(false, move, fast, dt);
        handoff(false, move, fast, dt, true);
      }
  held_stick_handoff();
  std::puts("settings_mouse: old fast clicks lost; fixed move/click handoff, keyboard, stick and A pass");
}
