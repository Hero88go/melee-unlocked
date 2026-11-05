// YouGame's online rows while YouGame is not open yet (MELEE_YOUGAME_COMING_SOON): a rainbow
// COMING SOON! sticker drawn over the end of each of the three greyed-out rows on the online menu's
// first page, in the game's own font, popping in once the menu has settled. The game's menu says when
// that page is showing (slippi_online.cpp, the YouGame query).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace coming_soon_overlay {

// The online menu's first page is on screen this frame. Game thread.
void showing();
// UI thread, once per presented frame: draws the stickers while the page is showing. The game
// picture is the rectangle x0,y0 to x1,y1 in the window. Badges stay below settings and dialogs.
void draw(float x0, float y0, float x1, float y1);

}  // namespace coming_soon_overlay
