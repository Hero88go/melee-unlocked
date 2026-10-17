// The variable refresh verdict (gx/vrr_detect.h) on display counts shaped like each kind of display.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "vrr_detect.h"
#include <cstdio>
#include <cstdlib>

using namespace gx::vrr;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

// Feeds `seconds` one-second windows of `fps` frames shown and `refresh` refreshes a second.
static void feed(Detector& d, Sample& s, int seconds, double fps, double refresh, double hz) {
  for (int i = 0; i < seconds; ++i) {
    s.presents += (uint64_t)fps;
    s.refreshes += (uint64_t)refresh;
    s.seconds += 1.0;
    d.add(s, hz);
  }
}

int main() {
  CHECK(std::abs(under_refresh(240) - 224) < 0.01);
  CHECK(std::abs(under_refresh(144) - 138.24) < 0.01);

  // Single windows.
  CHECK(judge(224, 224, 240) == 1);   // G-Sync at the Reflex limit: one refresh per frame
  CHECK(judge(224, 240, 240) == 0);   // fixed 240 Hz: full-rate refreshes, 16 frames shown twice
  CHECK(judge(60, 60, 240) == 1);     // 60 fps on a variable 240 Hz panel
  CHECK(judge(60, 240, 240) == 0);    // 60 fps on a fixed 240 Hz panel
  CHECK(judge(240, 240, 240) == -1);  // as fast as the refresh: alike on both
  CHECK(judge(300, 240, 240) == -1);  // faster than the refresh, tearing: alike on both
  CHECK(judge(60, 60, 60) == -1);     // a 60 Hz panel is never judged
  CHECK(judge(0, 240, 240) == -1);    // an idle window
  CHECK(judge(40, 80, 144) == -1);    // below the panel's range (frames doubled): no verdict

  // Two agreeing windows make a verdict, one window does not.
  {
    Detector d; Sample s;
    d.add(s, 240);                      // first reading only starts a window
    feed(d, s, 1, 224, 224, 240);
    CHECK(d.state() == -1);
    feed(d, s, 1, 224, 224, 240);
    CHECK(d.state() == 1);
    // A fixed display after a monitor change takes two windows to win.
    feed(d, s, 1, 224, 240, 240);
    CHECK(d.state() == 1);
    feed(d, s, 1, 224, 240, 240);
    CHECK(d.state() == 0);
  }
  // Undecidable windows (frames as fast as the refresh) leave the verdict alone.
  {
    Detector d; Sample s;
    d.add(s, 240);
    feed(d, s, 5, 240, 240, 240);
    CHECK(d.state() == -1);
    feed(d, s, 2, 216, 216, 240);       // the start-up check at 90% of the refresh
    CHECK(d.state() == 1);
    feed(d, s, 5, 240, 240, 240);
    CHECK(d.state() == 1);
  }
  // A window shorter than a second is not judged; counts that go backwards start over.
  {
    Detector d; Sample s;
    d.add(s, 240);
    s.presents += 100; s.refreshes += 100; s.seconds += 0.5;
    d.add(s, 240);
    CHECK(d.shown() == 0);
    feed(d, s, 1, 224, 240, 240);       // one fixed-looking window, counted
    Sample back = s; back.presents = 10; back.seconds += 1.0;   // the counts restarted (a new swap chain)
    d.add(back, 240);
    CHECK(d.state() == -1);
    feed(d, back, 2, 224, 224, 240);
    CHECK(d.state() == 1);
  }
  if (failures) { std::printf("%d failures\n", failures); return 1; }
  std::printf("vrr_detect: ok\n");
  return 0;
}
