// The launcher's looks: Classic (its own), "Classic, Melee font" (the same launcher with Melee's menu font and button
// frame, and Melee's hand as the pointer) and "Brawl Online by sarahkittyy". The Melee art comes from the player's
// disc (launcher_melee_assets.h); without it everything falls back to the launcher's own drawing.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <string>

namespace launcher::melee_look {

enum Look { kClassic = 0, kMeleeFont = 1, kBrawlOnline = 2 };
void set_look(int look);
int look();
bool melee_art();   // a Melee look is chosen and the disc's art is there

// Melee's font for one line of text in `r`, as big as `size_from` (DT_LEFT/DT_CENTER/DT_RIGHT, DT_VCENTER/DT_BOTTOM,
// DT_END_ELLIPSIS). False when it cannot (no art, more than one line, characters the font does not have): draw
// with the launcher's font then.
bool text(HDC dc, const std::wstring& s, RECT r, HFONT size_from, COLORREF color, UINT fmt);
int text_width(const std::wstring& s, HFONT size_from);   // -1 when text() would refuse it
// The same for a button's caption: Melee's font whatever the weight of `size_from`.
bool caption(HDC dc, const std::wstring& s, RECT r, HFONT size_from, COLORREF color, UINT fmt);
// A button in Melee's frame: its fill (top to bottom) and its rim. False without the art.
// css_mask: the rim as the Brawl Online mockups draw it (the art's alpha only, flat colour); otherwise a soft outline
// shaded from the art and blended into the button.
bool button(HDC dc, RECT r, COLORREF fill_top, COLORREF fill_bottom, COLORREF rim, bool css_mask = false);
HCURSOR hand_cursor(int pose = 0);   // Melee's hand as a pointer: 0 pointing, 1 the grab, 2 open (nullptr without the art)
// The pointer the Melee looks use (the Look menu, launcher.ini pointer= / pointer_left=, owner 11-25): kPointer3d the
// shaded posed hand, kPointerFlat the first flat white hand, kPointerWindows the normal Windows pointer (hand_now is
// then nullptr). Left: the hand mirrored, pointing up and left as a mouse pointer does.
enum Pointer { kPointer3d = 0, kPointerFlat = 1, kPointerWindows = 2 };
void set_pointer(int style, bool left);
int pointer();
bool pointer_left();
// The hand for this moment: grabbing while the button is held (mouse_msg: the message WM_SETCURSOR names), open over
// something clickable, otherwise pointing.
HCURSOR hand_now(bool over_clickable, UINT mouse_msg);
int hand_pose_now();   // the pose hand_now last picked (0 pointing, 1 grab, 2 open): the controller's hand follows it
// Melee's font at a given cell size in pixels (the skin's own sizes); -1 / false when it cannot.
int width_cell(const std::wstring& s, int cell);
bool text_cell(HDC dc, const std::wstring& s, RECT r, int cell, COLORREF color, UINT fmt, double skew = 0);   // skew: degrees of slant (titles)

}  // namespace launcher::melee_look
