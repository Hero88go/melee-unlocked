// --online-test ranked: leave once the requested game's room receipt has arrived.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <charconv>
#include <string_view>

namespace yougame::online_test {
class QuitAfter {
 public:
  QuitAfter() = default;
  QuitAfter(int mode, const char* value) {
    if (mode != 0 || !value) return;   // only an explicitly selected Ranked harness
    const std::string_view text(value);
    int n = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), n);
    if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && n >= 1) game_ = n;
  }
  template<class Leave>
  void poll(int received_games, Leave leave) {
    if (!game_ || triggered_ || received_games < game_) return;
    triggered_ = true;   // repeated menu/pad polls must never clean up twice
    leave();
  }
  int game() const { return game_; }
  bool triggered() const { return triggered_; }

 private:
  int game_ = 0;
  bool triggered_ = false;
};
}  // namespace yougame::online_test
