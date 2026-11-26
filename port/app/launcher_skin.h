// The "Brawl Online by sarahkittyy" look: the launcher laid out as that launcher is (a sidebar, the page, an
// Online/Friends rail on the right), with Melee in its place: Melee's menu font and button frame from the player's
// disc, Melee blue by default. Its Play page and Spectate page show the YouGame lobby (launcher_yglobby.h, the same
// live data and actions); its Game, Replays, Mods and Build items open those launcher pages as they are.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <functional>
#include <string>

namespace launcher::skin {

struct Callbacks {
  std::function<void()> play_offline;      // start the game (PLAY)
  std::function<void(int page)> open_page; // 0 Game (today's Play page), 1 Replays, 2 Mods, 3 Build; -1 the whole skin
  std::function<void()> settings, look_menu, send_logs, language;
  std::function<void()> rail_picture;   // clicking Bailey: pick another picture (or bring her back)
  // The status line under Play and Spectate, and what a click on each part does (0 Slippi, 1 YouGame, 2 Mods,
  // 3 the version, 4 Update, 5 the language).
  struct Status { std::wstring slippi, yougame, mods, version, update, language; bool slippi_ok = false, yougame_ok = false, mods_on = false, extension = false; };
  std::function<Status()> status;
  std::function<void(int part)> status_click;
  std::function<std::wstring()> build_name;   // the game build Play Melee starts
  std::function<void()> build_menu;           // pick another
};
// Bailey, or the player's own rail picture (32bpp premultiplied), drawn in the sidebar above the card.
void set_rail_art(HBITMAP bitmap, int w, int h);
// The look's parts for the launcher's own pages beside the sidebar (Mods), in that window's pixels.
struct Look { COLORREF page_top, page, raised, line, text, text2, dim, accent, ok, warn; };
Look look();
void page_background(HDC dc, HWND w, RECT client);   // the page's gradient, as behind Play
void heading(HDC dc, HWND w, const std::wstring& s, int x, int y);   // a page title: Melee's font, white, slanted
void melee_text(HDC dc, HWND w, const std::wstring& s, int x, int y, double size, COLORREF c, int max_w);
void card(HDC dc, HWND w, RECT r);   // a raised box
int melee_width(HDC dc, HWND w, const std::wstring& s, double size);   // how wide melee_text draws it, in pixels
void button(HDC dc, HWND w, RECT r, const std::wstring& label, bool accent, bool down, bool disabled);
void init(HWND main_window, const Callbacks& callbacks);
void show();   // over the whole window (the launcher sizes it to 1264x711 first)
void hide();
bool shown();
bool is_compact();   // only the sidebar, beside a launcher page
void fit();
void compact(int nav);   // only the sidebar, over the Classic rail, item `nav` selected (2 Game .. 5 Build)     // the launcher window changed size: cover it again
void set_color(COLORREF theme);   // the theme colour the palette is made from

}  // namespace launcher::skin
