// Requested display settings survive a temporary DXGI exclusive-mode refusal.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace gx {
struct FullscreenState {
  bool exclusive_active = false;

  template<class TryExclusive>
  bool enter_exclusive(TryExclusive attempt) {
    exclusive_active = attempt();
    return exclusive_active;
  }

  // Apply the driver's result to session state, never to the saved preference.
  // A refused exclusive request still needs a fullscreen-sized borderless window.
  template<class Options>
  bool borderless(const Options& requested, bool exclusive_deferred = false) const {
    return !exclusive_active &&
        (requested.fullscreen || requested.exclusive_fullscreen || exclusive_deferred);
  }
};
} // namespace gx
