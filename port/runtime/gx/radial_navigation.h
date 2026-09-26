// Pure category selection rules for the radial settings wheel.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace gx::radial_navigation {

constexpr int kCategoryCount = 7;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kStep = 2.0f * kPi / kCategoryCount;
constexpr int kStickDeadzone = 58;

inline int wrap(int selection) {
  return (selection % kCategoryCount + kCategoryCount) % kCategoryCount;
}

inline int update(int selection, bool up, bool right, bool down, bool left,
                  int stick_x, int stick_y) {
  selection = std::clamp(selection, 0, kCategoryCount - 1);
  if (std::abs(stick_x) > kStickDeadzone || std::abs(stick_y) > kStickDeadzone) {
    const float angle = std::atan2(-static_cast<float>(stick_y),
                                   static_cast<float>(stick_x));
    const float sector_position = (angle + kPi * 0.5f) / kStep;
    // At the exact bottom the seven equal wedges straddle the down axis. Bias that
    // single tie toward Controls (sector 3), matching D-pad Down and the wheel label.
    const int target = wrap(static_cast<int>(std::floor(sector_position + 0.5f - 1.0e-5f)));
    if (target == selection) return selection;

    // Keep the current slice through its narrow boundary band so stick noise cannot
    // chatter the highlight between adjacent categories.
    const float current_angle = -kPi * 0.5f + selection * kStep;
    const float delta = std::remainder(angle - current_angle, 2.0f * kPi);
    return std::abs(delta) > kStep * 0.58f ? target : selection;
  }

  if (left != right) return wrap(selection + (right ? 1 : -1));
  if (up) return 0;
  if (down) return 3;
  return selection;
}

}  // namespace gx::radial_navigation
