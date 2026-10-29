// The PhobGCC section of the Controls tab: PhobVision's menus for a controller on the adapter.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace gx {
// Draws the section for adapter socket `port` (0..3). Returns true when a setting changed.
bool draw_phob_menu(int port);
}  // namespace gx
