// The PhobGCC menu: PhobVision's menus for a controller on the adapter, drawn full screen in the
// game's own gold Main Menu look and driven by the controller.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace gx {

// The Controls tab's entry for adapter socket `port` (0..3): what is set, and the button that opens
// the menu. Returns true when a setting changed.
bool draw_phob_menu(int port);

// The full screen menu. While it is open the settings panel stays open underneath (so the game
// keeps its controllers away) but is not drawn, and the panel gives the controller to the menu.
struct PhobNativeIn {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // the game picture in the window
  uint16_t buttons = 0;                   // the menu's controller, GameCube button bits
  int8_t stick_x = 0, stick_y = 0;
  bool armed = false;                     // the controller was let go since the menu opened
  double time = 0;                        // seconds
};
struct PhobNativeOut {
  bool back_to_panel = false;   // B on the top screen: back to the Controls tab
  bool close_all = false;       // Start (outside the screens that record with it): close everything
  bool changed = false;
};
bool phob_native_open();
void phob_native_request(int port);   // open on this adapter socket
void phob_native_close();
PhobNativeOut phob_native_frame(const PhobNativeIn& in);

}  // namespace gx
