// Offline regression: the hook waits for UI samples, including hidden/stalled rendering.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_agent_pad.h"
#include "settings_chord.h"
#include "settings_nav.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame::agent_test;

void acknowledgement() {
  PadBack script;
  const PadBack::Clock::time_point epoch{};
  CHECK(!script.sample_for_ui(epoch + std::chrono::hours(1)).active);
  script.arm(10, epoch);
  CHECK(!script.sample_for_ui(epoch - std::chrono::seconds(1)).active);
  CHECK(!script.sample_for_ui(epoch + std::chrono::milliseconds(9999)).active);
  CHECK(script.sample_for_ui(epoch + std::chrono::seconds(10)).step == 0);
  for (int i = 0; i < 1000; ++i)
    CHECK(script.sample_for_ui(epoch + std::chrono::hours(1)).step == 0);
  CHECK(!script.consume_ui(-1) && !script.consume_ui(1));
  CHECK(script.consume_ui(0));
  CHECK(script.sample_for_ui(epoch + std::chrono::hours(2)).step == 0);
  CHECK(script.consume_ui(0));
  CHECK(!script.consume_ui(0));
  CHECK(script.sample_for_ui(epoch + std::chrono::hours(2)).step == 1);
  constexpr unsigned counts[] = {2, 1, 3, 1, 3, 1, 3, 1, 3, 1, 3};
  constexpr uint16_t buttons[] = {0, 0x1014, 0, 2, 0, 2, 0, 0x100, 0, 0x100, 0};
  for (int step = 1; step < 11; ++step) {
    // Wall time and any number of input reads cannot skip a press or release.
    for (unsigned ui = 0; ui < counts[step]; ++ui) {
      for (int read = 0; read < 50; ++read) {
        const auto pad = script.sample_for_ui(epoch + std::chrono::hours(24));
        CHECK(pad.active && pad.step == step && pad.pad.err == 0);
        CHECK(pad.pad.button == buttons[step]);
      }
      CHECK(script.consume_ui(step));
    }
    CHECK(!script.consume_ui(step));
  }
  CHECK(!script.sample_for_ui(epoch + std::chrono::hours(48)).active);
  CHECK(!script.consume_ui(10));
  for (int seconds : {0, 9, 901}) {
    PadBack invalid;
    invalid.arm(seconds, epoch);
    CHECK(!invalid.sample_for_ui(epoch + std::chrono::hours(48)).active);
  }
}

// Exercise the bundled ImGui navigation engine and the production arming/chord rules. No OS
// window, renderer, physical input or exit action. Layout/IDs match the small YouGame menu.
void menu_navigation(float dt, bool render_faster_than_input,
                     ImGuiKey activate = ImGuiKey_GamepadFaceDown, bool hide_confirmation_cursor = false) {
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  io.DisplaySize = ImVec2(1280, 960);
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigInputTrickleEventQueue = false;
  unsigned char* pixels;
  int w, h;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  PadBack script;
  const PadBack::Clock::time_point epoch{};
  script.arm(10, epoch);
  bool open = false, quit = false, exited = false, chord = false, toggle = false;
  bool focus_pending = false;
  bool was_active = false, armed = false, rest[4]{};
  int port = 0, right_edges = 0, left_edges = 0, a_edges = 0, quit_actions = 0, confirm_actions = 0, focus_requests = 0;
  const bool keyboard = activate != ImGuiKey_GamepadFaceDown;
  const ImGuiKey right = keyboard ? ImGuiKey_RightArrow : ImGuiKey_GamepadDpadRight;
  PadFrame published;
  for (int frame = 0; frame < 100 && !exited; ++frame) {
    // Let input time run arbitrarily far ahead, then sometimes render the same snapshot twice.
    if (!render_faster_than_input || frame % 2 == 0) {
      published = script.sample_for_ui(epoch + std::chrono::hours(frame + 1));
      if (!published.active) break;
      if (host::settings_shortcut::poll_z_start(true, published.pad.button, chord)) toggle = true;
    }
    auto pad = published.pad;
    if (!script.consume_ui(published.step)) pad = {}; // window_ui_pads' stale-snapshot release
    host::settings_nav::Pad pads[4] = {{true, pad.button, pad.stick_x, pad.stick_y}, {}, {}, {}};
    const bool active = open;
    port = host::settings_nav::follow(pads, rest, port, active);
    CHECK(port == 0);
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    if (active && !was_active) {
      io.ClearEventsQueue();
      io.ClearInputKeys();
      armed = false;
    }
    was_active = active;
    if (!active) armed = false;
    else if (!armed) armed = pad.button == 0;
    if (active && armed) {
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
      io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
      io.AddKeyEvent(activate, (pad.button & 0x100) != 0);
      io.AddKeyEvent(right, (pad.button & 2) != 0);
      io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (pad.button & 1) != 0);
    }
    io.DeltaTime = dt;
    ImGui::NewFrame();
    if (ImGui::IsKeyPressed(right, true)) ++right_edges;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true)) ++left_edges;
    if (ImGui::IsKeyPressed(activate, false)) ++a_edges;
    if (toggle) { open = !open; quit = false; toggle = false; }
    if (open) {
      ImGui::SetNextWindowPos(ImVec2(260, 770), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(760, 172), ImGuiCond_Always);
      ImGui::Begin("##esc_menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
      const int count = quit ? 2 : 3;
      const float width = (760.f - 40.f - 10.f * (count - 1)) / count;
      const auto action = [&](const char* id, int index) {
        ImGui::SetCursorPos(ImVec2(20.f + index * (width + 10.f), 66));
        const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, 58), ImGuiButtonFlags_EnableNav);
        // Mirror the production one-shot focus transfer, including the gamepad cursor requirement.
        if (quit && index == 0 && focus_pending) {
          CHECK(!ImGui::IsWindowAppearing()); // the regression: this is an in-place view change
          if (hide_confirmation_cursor) ImGui::SetNavCursorVisible(false);
          ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
          if (ImGui::GetCurrentContext()->NavInputSource == ImGuiInputSource_Gamepad)
            ImGui::SetNavCursorVisible(true);
          focus_pending = false;
          ++focus_requests;
          CHECK(ImGui::IsItemFocused()); // focused on the confirmation's very first frame
          CHECK(!pressed && !exited);   // transferring focus must not activate the confirmation
          if (!keyboard) CHECK(ImGui::GetCurrentContext()->NavCursorVisible);
        }
        return pressed;
      };
      if (!quit) {
        CHECK(!action("##resume", 0));
        ImGui::SetItemDefaultFocus();
        CHECK(!action("##settings", 1));
        if (action("##quit", 2)) { quit = true; focus_pending = true; ++quit_actions; }
      } else {
        const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        if (action("##confirm_quit", 0) || enter) { exited = true; ++confirm_actions; }
        ImGui::SetItemDefaultFocus();
        CHECK(!action("##cancel_quit", 1));
      }
      ImGui::End();
    }
    ImGui::Render();
  }
  CHECK(exited && quit_actions == 1 && confirm_actions == 1 && focus_requests == 1);
  CHECK(right_edges == 2 && a_edges == 2);
  CHECK(left_edges == 0);
  ImGui::DestroyContext();
}

int main() {
  acknowledgement();
  for (float dt : {1.f / 60.f, 0.7f, 5.f}) {
    menu_navigation(dt, false);
    menu_navigation(dt, true);
    menu_navigation(dt, false, ImGuiKey_Enter);
    menu_navigation(dt, true, ImGuiKey_Enter);
    menu_navigation(dt, false, ImGuiKey_KeypadEnter);
    menu_navigation(dt, true, ImGuiKey_KeypadEnter);
  }
  menu_navigation(1.f / 60.f, false, ImGuiKey_GamepadFaceDown, true);
  std::puts("yougame_agent_pad_ui: stalls, acknowledgements, stale snapshots, arming, A/A, Enter/Enter and keypad Enter exit navigation ok");
}
