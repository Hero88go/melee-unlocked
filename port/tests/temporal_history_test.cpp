// Displayed motion must be independent of simulation frames drained without presentation.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "temporal_history.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
  gx::TemporalMotionHistory h;
  float position[256]{}, projection[16]{};
  projection[0] = 1;
  CHECK(!h.previous(7, 1, true));
  h.record(7, 1, true, position, projection);
  // A camera moves through ten unshown frames while the renderer catches up. They neither
  // become the previous displayed pose nor consume a presentation number.
  for (int i = 1; i <= 10; ++i) {
    position[3] = (float)i;
    projection[0] = (float)i + 1;
    CHECK(!h.previous(7, 2, false));
    h.record(7, 2, false, position, projection);
  }
  const auto* previous = h.previous(7, 2, true);
  CHECK(previous && previous->position[3] == 0 && previous->projection[0] == 1);
  position[3] = 20; projection[0] = 3;
  h.record(7, 2, true, position, projection);
  // A fighter drawn again in a second pass still compares against the preceding displayed
  // frame, not against its first pass from the current one.
  previous = h.previous(7, 2, true);
  CHECK(previous && previous->position[3] == 0 && previous->projection[0] == 1);
  position[3] = 21;
  h.record(7, 2, true, position, projection);
  previous = h.previous(7, 3, true);
  CHECK(previous && previous->position[3] == 21 && previous->projection[0] == 3);
  h.record(7, 3, true, position, projection);
  // A new/reappearing draw and a restored timeline must not reuse unrelated history.
  CHECK(!h.previous(8, 3, true));
  CHECK(!h.previous(7, 5, true));
  h.clear();
  CHECK(!h.previous(7, 6, true));
  h.record(7, 6, true, position, projection);
  h.prune(11);
  CHECK(!h.previous(7, 12, true));
  std::puts("unshown frames, multipass draws, missing draws, timeline resets and retirement passed");
}
