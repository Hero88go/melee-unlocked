// SPDX-License-Identifier: GPL-2.0-or-later
#include "signin_overlay.h"

#include "qr_code.h"
#include "yougame_link.h"
#include "yougame_signin.h"

#include <imgui.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <string>

namespace signin_overlay {
namespace {

using yougame::signin::Button;
using yougame::signin::Screen;
namespace words = yougame::signin;

// The YouGame menu's look (the menu YouGame draws over a game), so the box reads the same wherever the
// game was started: #0f0f0f panel, #f1f1f1 words, #aaa secondary words, #3ea6ff focus ring.
const ImVec4 kPanel(15 / 255.0f, 15 / 255.0f, 15 / 255.0f, 1.0f);
const ImVec4 kText(241 / 255.0f, 241 / 255.0f, 241 / 255.0f, 1.0f);
const ImVec4 kSoft(170 / 255.0f, 170 / 255.0f, 170 / 255.0f, 1.0f);
const ImVec4 kFocus(62 / 255.0f, 166 / 255.0f, 1.0f, 1.0f);

// One CSS pixel in window pixels: the box keeps the menu's size whatever the window's size (the
// display's scale setting only, as a browser does). Mac windows already count in points.
float css_px() {
#ifdef _WIN32
  static const float k = std::max(1.0f, (float)GetDpiForSystem() / 96.0f);
  return k;
#else
  return 1.0f;
#endif
}

const char* label(Button b) {
  switch (b) {
    case Button::NewCode: return words::kNewCode;
    case Button::TryAgain: return words::kTryAgain;
    case Button::Browser: return words::kBrowserButton;
    case Button::Back: return words::kBack;
    case Button::OpenApp: return words::kOpenApp;
    case Button::GetApp: return words::kGetApp;
    case Button::Offline: return words::kPlayOffline;
    case Button::Update: return words::kUpdate;
    case Button::Cancel: return words::kCancel;
    case Button::Restart: return words::kRestartNow;
    case Button::Feedback: return words::kFeedback;
    case Button::OpenOnYouGame: return words::kOpenOnYouGame;
    case Button::None: break;
  }
  return "";
}

// ImGui sizes fonts by its base size times the style's scales: undo those so `px` is window pixels.
float font_size(float px) {
  const ImGuiStyle& s = ImGui::GetStyle();
  const float scale = std::max(0.01f, s.FontScaleMain * s.FontScaleDpi);
  return std::max(8.0f, px / scale);
}

void centred_text(const std::string& text, float px, const ImVec4& colour) {
  ImGui::PushFont(nullptr, font_size(px));
  const float w = ImGui::CalcTextSize(text.c_str()).x, avail = ImGui::GetContentRegionAvail().x;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - w) * 0.5f));
  ImGui::TextColored(colour, "%s", text.c_str());
  ImGui::PopFont();
}

void wrapped_text(const std::string& text, float px, const ImVec4& colour) {
  ImGui::PushFont(nullptr, font_size(px));
  ImGui::PushStyleColor(ImGuiCol_Text, colour);
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

// The QR, black on white with a quiet zone of four modules, whole pixels per module so a camera reads
// clean edges. `side` is the most room it may take.
void draw_qr(const qr_code::Matrix& m, float side) {
  if (m.size <= 0) return;
  const int quiet = 4, total = m.size + quiet * 2;
  const float module = std::max(2.0f, std::floor(side / (float)total));
  const float full = module * (float)total;
  const float avail = ImGui::GetContentRegionAvail().x;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail - full) * 0.5f));
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(at, ImVec2(at.x + full, at.y + full), IM_COL32(255, 255, 255, 255));
  for (int y = 0; y < m.size; ++y) {
    for (int x = 0; x < m.size;) {
      if (!m.at(x, y)) { ++x; continue; }
      int run = x;
      while (run < m.size && m.at(run, y)) ++run;   // one rectangle per run of dark modules
      const float x0 = at.x + (float)(x + quiet) * module, y0 = at.y + (float)(y + quiet) * module;
      dl->AddRectFilled(ImVec2(x0, y0), ImVec2(at.x + (float)(run + quiet) * module, y0 + module), IM_COL32(0, 0, 0, 255));
      x = run;
    }
  }
  ImGui::Dummy(ImVec2(full, full));
}

bool g_was_open = false;
Screen g_last_screen = Screen::Closed;
std::string g_qr_text;
qr_code::Matrix g_qr;

}  // namespace

bool open() { return yougame::sign_in_showing(); }

void back() { yougame::sign_in_press(Button::Back); }

void draw(float x0, float y0, float x1, float y1) {
  if (!yougame::sign_in_showing()) { g_was_open = false; g_last_screen = Screen::Closed; return; }
  yougame::signin::View v;
  yougame::sign_in_view(v);
  if (v.screen == Screen::Closed) { g_was_open = false; g_last_screen = Screen::Closed; return; }
  const bool appearing = !g_was_open;
  const bool new_screen = appearing || v.screen != g_last_screen;
  g_was_open = true;
  g_last_screen = v.screen;

  const float gw = std::max(1.0f, x1 - x0), u = css_px();
  ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(0, 0, 0, 128));   // game at 50%

  // The menu's frame: 440 px wide, 16 px from the picture's top-left corner, only as tall as its words.
  const float card_w = std::max(200.0f * u, std::min(gw - 32.0f * u, 440.0f * u));
  ImGui::SetNextWindowPos(ImVec2(x0 + 16.0f * u, y0 + 16.0f * u), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(card_w, 0.0f), ImGuiCond_Always);
  // Focused every frame it shows, not only as it appears: the box owns the keyboard and the routed pad
  // while it is up, so A and Enter press its buttons even after another window took the focus.
  ImGui::SetNextWindowFocus();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f * u);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * u, 0.0f));   // the header row starts at the top
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f * u, 15.0f * u));   // 44 px buttons with 14 px words
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 22.0f * u);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * u, 8.0f * u));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, kPanel);
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));   // second buttons: plain words
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.16f));
  ImGui::PushStyleColor(ImGuiCol_NavCursor, kFocus);   // ImGui draws it 2 px wide
  ImGui::PushStyleColor(ImGuiCol_Text, kText);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
  Button pressed = Button::None;
  if (ImGui::Begin("##yougame_sign_in", nullptr, flags)) {
    // Header row, 58 px: "YouGame" in bold at 20 px (the body font drawn twice a hair apart).
    {
      const ImVec2 at = ImGui::GetCursorScreenPos();
      ImGui::PushFont(nullptr, font_size(20.0f * u));
      const float th = ImGui::GetFontSize();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 tp(at.x, at.y + (58.0f * u - th) * 0.5f);
      dl->AddText(ImGui::GetFont(), th, tp, ImGui::GetColorU32(kText), words::kHomeTitle);
      dl->AddText(ImGui::GetFont(), th, ImVec2(tp.x + 0.75f * u, tp.y), ImGui::GetColorU32(kText), words::kHomeTitle);
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(0, 58.0f * u - ImGui::GetStyle().ItemSpacing.y));
    }
    // The screen's own title (Sign in to YouGame, Update to play online) as the first line of the body.
    const std::string title = v.title.empty() ? words::kTitle : v.title.c_str();
    if (title != words::kHomeTitle) {
      ImGui::PushFont(nullptr, font_size(16.0f * u));
      ImGui::TextColored(kText, "%s", title.c_str());
      ImGui::PopFont();
    }

    switch (v.screen) {
      case Screen::Asking: wrapped_text(words::kAsking, 14.0f * u, kSoft); break;
      case Screen::Code: {
        const std::string where = v.verify_display.empty() ? std::string("yougame.co/link") : v.verify_display;
        wrapped_text(std::string(words::kGoTo) + " " + where + " " + words::kAndEnter + " " + v.user_code, 14.0f * u, kText);
        ImGui::Dummy(ImVec2(0, 2.0f * u));
        centred_text(v.user_code, 40.0f * u, kText);
        if (v.qr_text != g_qr_text) { g_qr_text = v.qr_text; g_qr = qr_code::encode(g_qr_text); }
        if (g_qr.size > 0) {
          ImGui::Dummy(ImVec2(0, 2.0f * u));
          draw_qr(g_qr, std::min(card_w * 0.62f, 150.0f * u));
          centred_text(words::kScan, 12.0f * u, kSoft);
        }
        centred_text(std::string(words::kExpiresIn) + " " + words::clock(v.seconds), 12.0f * u, kSoft);
        break;
      }
      case Screen::Busy: wrapped_text(std::string(words::kWait) + " " + std::to_string(v.seconds) + " s", 14.0f * u, kText); break;
      case Screen::Expired: wrapped_text(words::kExpired, 14.0f * u, kText); break;
      case Screen::Failed:
      case Screen::NotYet: wrapped_text(v.message.empty() ? std::string(words::kFailed) : v.message, 14.0f * u, kText); break;
      case Screen::Browser:
        wrapped_text(words::kBrowser, 14.0f * u, kText);
        if (!v.user_code.empty()) centred_text(v.user_code, 22.0f * u, kText);   // the /link page's code
        break;
      case Screen::Window: wrapped_text(v.message.empty() ? std::string(words::kWindow) : v.message, 14.0f * u, kText); break;
      case Screen::SignedIn: centred_text(v.message, 22.0f * u, kText); break;
      case Screen::Looking:
      case Screen::Opening: wrapped_text(v.message, 14.0f * u, kSoft); break;
      case Screen::AppClosed:
      case Screen::NoApp:
      case Screen::Admin: wrapped_text(v.message, 14.0f * u, kText); break;
      // Update to play online (yougame_signin.h Update): the model writes each line whole.
      case Screen::UpdateChecking:
      case Screen::UpdateDownloading:
      case Screen::UpdateRestarting: wrapped_text(v.message, 14.0f * u, kSoft); break;
      case Screen::UpdateOffered:
      case Screen::UpdateReady:
      case Screen::UpdateRestartFailed:
      case Screen::UpdateFailed:
      case Screen::UpdateNotAppCopy:
      case Screen::UpdateOldApp:
      case Screen::UpdateLatest:
      case Screen::UpdateNoCheck:
      case Screen::Home: wrapped_text(v.message, 14.0f * u, kText); break;
      case Screen::Closed: break;
    }

    // The buttons, one row, the first one focused whenever the screen changes (A presses it).
    ImGui::Dummy(ImVec2(0, 4.0f * u));
    ImGui::PushFont(nullptr, font_size(14.0f * u));
    bool first = true;
    // One row while they fit, the next button on a new line when it wouldn't (three on the YouGame box).
    const float row_x2 = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (Button b : v.buttons) {
      if (b == Button::None) continue;
      const float w = ImGui::CalcTextSize(label(b)).x + ImGui::GetStyle().FramePadding.x * 2.0f;
      if (!first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w <= row_x2) ImGui::SameLine();
      if (first && new_screen) { ImGui::SetKeyboardFocusHere(); ImGui::SetNavCursorVisible(true); }
      // The main button (the first) is the menu's light pill with dark words; the rest stay plain words.
      if (first) {
        ImGui::PushStyleColor(ImGuiCol_Button, kText);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, kPanel);
      }
      if (ImGui::Button(label(b))) pressed = b;
      if (first) ImGui::PopStyleColor(4);
      first = false;
    }
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 12.0f * u));   // 20 px under the buttons with the item spacing
    // B on the controller is Back (Esc is Back from the window procedure: yougame::sign_in_escape).
    if (pressed == Button::None && !appearing && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) pressed = Button::Back;
  }
  ImGui::End();
  ImGui::PopStyleColor(7);
  ImGui::PopStyleVar(6);
  if (pressed != Button::None) yougame::sign_in_press(pressed);
}

}  // namespace signin_overlay
