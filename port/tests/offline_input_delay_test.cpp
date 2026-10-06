// Exact delay and queue isolation, including repeated reads and scene changes.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "offline_input_delay.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
  using host::PadState;
  for (int delay = 1; delay <= 9; ++delay) {
    host::offline_delay::Queue q;
    for (uint32_t tick = 1; tick <= 24; ++tick) {
      PadState pads[4]{};
      for (int p = 0; p < 4; ++p) { pads[p].button = uint16_t(tick + p * 100); pads[p].trig_r = uint8_t(tick); }
      q.apply(pads, true, delay, tick, 2);
      for (int p = 0; p < 4; ++p) {
        CHECK(pads[p].button == (tick > uint32_t(delay) ? tick - delay + p * 100 : 0));
        CHECK(pads[p].trig_r == (tick > uint32_t(delay) ? tick - delay : 0));
      }
      PadState again[4]{}; again[0].button = 999;
      q.apply(again, true, delay, tick, 2);
      CHECK(again[0].button == pads[0].button);
    }
    PadState pads[4]{}; pads[0].button = 777;
    q.apply(pads, false, delay, 25, 2); CHECK(pads[0].button == 777);
    q.apply(pads, true, delay, 26, 2); CHECK(pads[0].button == 0);
    pads[0].button = 888; q.apply(pads, true, delay, 27, 3); CHECK(pads[0].button == 0);
    pads[0].err = -1; q.apply(pads, true, delay, 28, 3); CHECK(pads[0].button == 0 && pads[0].err == -1);
    pads[0] = {}; q.apply(pads, true, delay, 29, 3); CHECK(pads[0].button == 0 && pads[0].err == 0);
    pads[0].button = 999; q.apply(pads, true, delay, 1, 3); CHECK(pads[0].button == 0);
  }
  std::puts("1 through 9 frames, four ports, repeated reads, disabled, scene and controller changes passed");
}
