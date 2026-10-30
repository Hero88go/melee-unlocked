// The PhobGCC section of the Controls tab: PhobVision's menus for a controller on the adapter.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace gx {
// Draws the section for adapter socket `port` (0..3). Returns true when a setting changed.
bool draw_phob_menu(int port);
// True while a Phob calibration or remap is listening to the controller. The panel then takes no
// navigation from the controller, so A, B, X, Y, Z, L and R mean what PhobVision makes them mean.
bool phob_menu_capturing();
// True while a PhobScope screen that records on Start is showing: Start then belongs to it, while
// A, B and the D-pad still move around the panel.
bool phob_menu_owns_start();
// Call on a frame the section is not drawn: ends any calibration or remap in progress.
void phob_menu_not_drawn();
}  // namespace gx
