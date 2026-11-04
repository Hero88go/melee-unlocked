// YouGame's online rows while YouGame is not open yet (MELEE_YOUGAME_COMING_SOON): a sticker drawn
// over the end of each of the three rows on the online menu's first page, in the game's own font,
// that pops in once the menu has settled and shakes when a player picks a row that is not open.
// The game's menu says when its first page is showing (slippi_online.cpp, the YouGame query).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace coming_soon_overlay {

// The online menu's first page is on screen this frame, the cursor on `row` (0-3). Game thread.
void showing(int row);
// A player picked Coming soon row `row` (0-2). Game thread.
void refused(int row);
// UI thread, once per presented frame: draws the stickers while the page is showing. The game
// picture is the rectangle x0,y0 to x1,y1 in the window.
void draw(float x0, float y0, float x1, float y1);
// The sticker style (0-4), for the look options; MELEE_TEST_SOON_STYLE picks one in hidden runs.
void set_style(int style);

}  // namespace coming_soon_overlay
