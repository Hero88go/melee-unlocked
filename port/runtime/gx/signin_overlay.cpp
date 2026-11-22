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
const ImVec4 kBody(217 / 255.0f, 217 / 255.0f, 217 / 255.0f, 1.0f);   // the line under a heading
const ImU32 kRed = IM_COL32(255, 0, 0, 255);
// The header's close pill: Back (Esc, B and Shift+Tab are Back too), named with its key.
const char* const kBackToGame = "Back to game";
const char* const kCloseKey = "Shift+Tab";

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

// Bold the way the header word is: the body font drawn twice a hair apart. Advances the cursor.
void bold_text(const char* text, float px, const ImVec4& colour, float u) {
  ImGui::PushFont(nullptr, font_size(px));
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const float fs = ImGui::GetFontSize();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddText(ImGui::GetFont(), fs, at, ImGui::GetColorU32(colour), text);
  dl->AddText(ImGui::GetFont(), fs, ImVec2(at.x + 0.75f * u, at.y), ImGui::GetColorU32(colour), text);
  ImGui::Dummy(ImVec2(ImGui::CalcTextSize(text).x + 0.75f * u, fs));
  ImGui::PopFont();
}

// YouGame's logo, `side` px square at `at`: the red tile with the white pad (the menu's 28 px mark).
void draw_logo(ImDrawList* dl, ImVec2 at, float side) {
  const float k = side / 28.0f;
  auto P = [&](float x, float y) { return ImVec2(at.x + x * k, at.y + y * k); };
  dl->AddRectFilled(P(0, 0), P(28, 28), kRed, 7.0f * k);
  dl->AddRectFilled(P(5, 9.5f), P(23, 18.5f), IM_COL32(255, 255, 255, 255), 4.5f * k);
  dl->AddRectFilled(P(9.25f, 11.5f), P(10.75f, 16.5f), kRed, 0.6f * k);
  dl->AddRectFilled(P(7.5f, 13.25f), P(12.5f, 14.75f), kRed, 0.6f * k);
  dl->AddCircleFilled(P(17.4f, 12.7f), 1.1f * k, kRed);
  dl->AddCircleFilled(P(19.8f, 15.3f), 1.1f * k, kRed);
}

// The back arrow (24 px grid drawn at `side`): a turn-back arrow, 2 px strokes.
void draw_back_icon(ImDrawList* dl, ImVec2 at, float side, ImU32 col) {
  const float k = side / 24.0f, t = 2.0f * k;
  auto P = [&](float x, float y) { return ImVec2(at.x + x * k, at.y + y * k); };
  const ImVec2 head[3] = {P(9, 7), P(4, 12), P(9, 17)};
  dl->AddPolyline(head, 3, col, ImDrawFlags_None, t);
  dl->PathLineTo(P(4, 12));
  dl->PathLineTo(P(15, 12));
  dl->PathArcTo(P(15, 17), 5.0f * k, -1.5707963f, 1.5707963f);
  dl->PathLineTo(P(13, 22));
  dl->PathStroke(col, ImDrawFlags_None, t);
}

// The header's "Back to game  Shift+Tab" pill, right-aligned on the 58 px row at `row_y`. True when pressed.
bool back_pill(float row_y, float right, float u, bool focus) {
  ImGui::PushFont(nullptr, font_size(13.0f * u));
  const float icon = 16.0f * u, gap = 7.0f * u, pad = 14.0f * u, h = 44.0f * u;
  const float w_words = ImGui::CalcTextSize(kBackToGame).x, w_key = ImGui::CalcTextSize(kCloseKey).x;
  const float w = pad + icon + gap + w_words + gap + w_key + pad;
  const ImVec2 at(right - w, row_y + (58.0f * u - h) * 0.5f);
  ImGui::SetCursorScreenPos(at);
  if (focus) { ImGui::SetKeyboardFocusHere(); ImGui::SetNavCursorVisible(true); }
  const bool pressed = ImGui::InvisibleButton("##yougame_back_to_game", ImVec2(w, h));
  const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), IM_COL32(255, 255, 255, hot ? 51 : 26), h * 0.5f);
  const float fs = ImGui::GetFontSize(), ty = at.y + (h - fs) * 0.5f;
  float x = at.x + pad;
  draw_back_icon(dl, ImVec2(x, at.y + (h - icon) * 0.5f), icon, ImGui::GetColorU32(kText));
  x += icon + gap;
  dl->AddText(ImGui::GetFont(), fs, ImVec2(x, ty), ImGui::GetColorU32(kText), kBackToGame);
  dl->AddText(ImGui::GetFont(), fs, ImVec2(x + 0.5f * u, ty), ImGui::GetColorU32(kText), kBackToGame);   // 500 weight
  x += w_words + gap;
  dl->AddText(ImGui::GetFont(), fs, ImVec2(x, ty), ImGui::GetColorU32(kSoft), kCloseKey);
  ImGui::PopFont();
  return pressed;
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
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * u, 0.0f));   // the header row starts at the top
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
    // Header row, 58 px: the 28 px logo, "YouGame" in bold at 20 px, and on the right the close pill
    // (Back to game, Shift+Tab). The pill is Back on every screen, so the rows below carry no Back.
    // Shown buttons: the screen's own, less Back and Play offline (both close the box, as the pill does).
    int shown = 0;
    for (Button b : v.buttons) if (b != Button::None && b != Button::Back && b != Button::Offline) ++shown;
    {
      const ImVec2 at = ImGui::GetCursorScreenPos();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float logo = 28.0f * u;
      draw_logo(dl, ImVec2(at.x, at.y + (58.0f * u - logo) * 0.5f), logo);
      ImGui::PushFont(nullptr, font_size(20.0f * u));
      const float th = ImGui::GetFontSize();
      const ImVec2 tp(at.x + logo + 8.0f * u, at.y + (58.0f * u - th) * 0.5f);
      dl->AddText(ImGui::GetFont(), th, tp, ImGui::GetColorU32(kText), words::kHomeTitle);
      dl->AddText(ImGui::GetFont(), th, ImVec2(tp.x + 0.75f * u, tp.y), ImGui::GetColorU32(kText), words::kHomeTitle);
      ImGui::PopFont();
      // 10 px from the panel's right edge, as the menu's bar is (6 px into the 16 px padding).
      const float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 10.0f * u;
      if (back_pill(at.y, right, u, new_screen && shown == 0)) pressed = Button::Back;
      ImGui::SetCursorScreenPos(at);
      ImGui::Dummy(ImVec2(0, 58.0f * u - ImGui::GetStyle().ItemSpacing.y));
    }
    // The screen's own title (Sign in to YouGame, Update to play online) as the first line of the body.
    const std::string title = v.title.empty() ? words::kTitle : v.title.c_str();
    if (title != words::kHomeTitle) bold_text(title.c_str(), 16.0f * u, kText, u);   // the pane's heading

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
      case Screen::Window: wrapped_text(v.message.empty() ? std::string(words::kWindow) : v.message, 14.0f * u, kBody); break;
      case Screen::SignedIn: centred_text(v.message, 22.0f * u, kText); break;
      case Screen::Looking:
      case Screen::Opening: wrapped_text(v.message, 14.0f * u, kSoft); break;
      case Screen::AppClosed:
      case Screen::NoApp:
      case Screen::Admin: wrapped_text(v.message, 14.0f * u, kBody); break;
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
      case Screen::Home: wrapped_text(v.message, 14.0f * u, kBody); break;
      case Screen::Closed: break;
    }

    // The buttons, one row, the first one focused whenever the screen changes (A presses it).
    if (shown > 0) ImGui::Dummy(ImVec2(0, 4.0f * u));
    ImGui::PushFont(nullptr, font_size(14.0f * u));
    bool first = true;
    // One row while they fit, the next button on a new line when it wouldn't (three on the YouGame box).
    const float row_x2 = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (Button b : v.buttons) {
      if (b == Button::None || b == Button::Back || b == Button::Offline) continue;   // the header's pill
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
    ImGui::Dummy(ImVec2(0, (shown > 0 ? 12.0f : 10.0f) * u));   // 18-20 px under the last row with the item spacing
    // B on the controller is Back (Esc is Back from the window procedure: yougame::sign_in_escape).
    if (pressed == Button::None && !appearing && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) pressed = Button::Back;
  }
  ImGui::End();
  ImGui::PopStyleColor(7);
  ImGui::PopStyleVar(6);
  if (pressed != Button::None) yougame::sign_in_press(pressed);
}

}  // namespace signin_overlay
