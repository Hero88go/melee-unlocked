// Host-side mirror of the X11 helper's accepted rect/show/hide state.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace linux_window {
class EmbedVisibility {
 public:
  // Call only after the helper accepts the command. Attachment starts with show
  // intent, but no valid page rect; a rect update preserves an explicit hide.
  void rect(int w, int h) { valid_rect_ = w >= 32 && h >= 32; }
  void show(bool shown) { shown_ = shown; }
  bool visible() const { return valid_rect_ && shown_; }

 private:
  bool valid_rect_ = false;
  bool shown_ = true;
};
}  // namespace linux_window
