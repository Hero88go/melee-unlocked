// Offline, exhaustive frame checks for the test-only GameCube port-1 sequence.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_agent_pad.h"
#include "settings_chord.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame::agent_test;

void neutral_axes(const MuPadStatus& pad) {
  CHECK(pad.stick_x == 0 && pad.stick_y == 0);
  CHECK(pad.sub_x == 0 && pad.sub_y == 0);
  CHECK(pad.trigger_l == 0 && pad.trigger_r == 0);
  CHECK(pad.analog_a == 0 && pad.analog_b == 0);
}

void no_override(const PadFrame& state) {
  CHECK(!state.active && state.step == -1);
  CHECK(state.pad.err == -1 && state.pad.button == 0);
  neutral_axes(state.pad);
}

void sequence(int seconds) {
  const uint64_t start = static_cast<uint64_t>(seconds) * 60;
  // Check each frame, rather than deriving expectations from the production step table.
  for (uint64_t frame = 0; frame < start; ++frame) no_override(pad_back_frame(frame, seconds));
  bool chord_held = false;
  int chord_edges = 0, right_edges = 0, a_edges = 0;
  uint16_t last_buttons = 0;
  for (uint64_t f = 0; f < 152; ++f) {
    const auto state = pad_back_frame(start + f, seconds);
    CHECK(state.active && state.pad.err == 0);
    neutral_axes(state.pad);
    uint16_t expected = 0;
    if (f >= 30 && f < 40) expected = 0x1014;
    if ((f >= 60 && f < 66) || (f >= 80 && f < 86)) expected = 0x0002;
    if ((f >= 100 && f < 106) || (f >= 126 && f < 132)) expected = 0x0100;
    CHECK(state.pad.button == expected);
    const int expected_step = f < 30 ? 0 : f < 40 ? 1 : f < 60 ? 2 : f < 66 ? 3 :
        f < 80 ? 4 : f < 86 ? 5 : f < 100 ? 6 : f < 106 ? 7 : f < 126 ? 8 : f < 132 ? 9 : 10;
    CHECK(state.step == expected_step);
    // Exercise the actual chord edge/suppression policy on the same state the menu snapshot gets.
    auto buttons = state.pad.button;
    if (host::settings_shortcut::poll_z_start(true, buttons, chord_held)) ++chord_edges;
    CHECK(buttons == (expected == 0x1014 ? 0 : expected));
    if ((buttons & 0x0002) && !(last_buttons & 0x0002)) ++right_edges;
    if ((buttons & 0x0100) && !(last_buttons & 0x0100)) ++a_edges;
    last_buttons = buttons;
  }
  CHECK(chord_edges == 1 && right_edges == 2 && a_edges == 2);
  CHECK(!chord_held && last_buttons == 0);
  for (uint64_t f : {start + 152, start + 153, start + 304, uint64_t(1000000),
                     std::numeric_limits<uint64_t>::max()}) no_override(pad_back_frame(f, seconds));
}

void clock_gating() {
  PadBack script;
  const PadBack::Clock::time_point epoch{};
  no_override(script.sample(epoch + std::chrono::hours(1)));
  script.arm(10, epoch);
  no_override(script.sample(epoch - std::chrono::seconds(1)));
  no_override(script.sample(epoch + std::chrono::milliseconds(9999)));
  CHECK(script.sample(epoch + std::chrono::seconds(10)).active);
  // The same instant has the same input, regardless of the number of input reads or render frames.
  for (int i = 0; i < 200; ++i) {
    const auto state = script.sample(epoch + std::chrono::milliseconds(10500));
    CHECK(state.active && state.step == 1 && state.pad.button == 0x1014);
  }
  no_override(script.sample(epoch + std::chrono::milliseconds(12534)));
  no_override(script.sample(epoch + std::chrono::hours(1)));
}

int main() {
  sequence(10);
  sequence(900);
  for (int seconds : {0, 9, 901}) no_override(pad_back_frame(60000, seconds));
  clock_gating();
  std::puts("yougame_agent_pad: all 152 frames, releases, connected state, chord edges, timing and no repeat ok");
}
