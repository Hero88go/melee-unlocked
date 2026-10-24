// The vertical blank model (gx/vblank_model.h) on wake times shaped like a real waiting thread:
// late wakes, a missed blank, a period that is not a whole number of microseconds.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "vblank_model.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int failures = 0;
static void check(bool ok, const char* what) {
  if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}

int main() {
  using gx::VBlankModel;
  const double period = 1.0 / 143.981, start = 100.0;
  {
    VBlankModel m;
    double p = 0, last = 0;
    for (int i = 0; i < 10; ++i) m.add(start + i * period);
    check(!m.fit(&p, &last), "too few samples gives no fit");
  }
  {
    // Every wake 20 to 300 us late; one blank in four exactly on time.
    VBlankModel m;
    for (int i = 0; i < 64; ++i) m.add(start + i * period + (i % 4 == 0 ? 0.0 : 0.00002 + 0.0001 * (i % 3)));
    double p = 0, last = 0;
    check(m.fit(&p, &last), "late wakes fit");
    check(std::fabs(p - period) < 0.00005, "period from late wakes");
    check(std::fabs(last - (start + 63 * period)) < 0.00002, "position is the earliest wake, not a late one");
  }
  {
    // A missed blank (the thread was held) leaves a double gap; the median ignores it.
    VBlankModel m;
    for (int i = 0; i < 64; ++i) if (i != 30) m.add(start + i * period);
    double p = 0, last = 0;
    check(m.fit(&p, &last) && std::fabs(p - period) < 0.00001, "a missed blank does not change the period");
  }
  {
    // Out-of-order or repeated times are ignored.
    VBlankModel m;
    for (int i = 0; i < 40; ++i) { m.add(start + i * period); m.add(start + i * period - 0.001); }
    double p = 0, last = 0;
    check(m.fit(&p, &last) && std::fabs(p - period) < 0.00001, "stale times ignored");
  }
  {
    // A period outside 23..600 Hz is refused.
    VBlankModel m;
    for (int i = 0; i < 40; ++i) m.add(start + i * 0.1);
    double p = 0, last = 0;
    check(!m.fit(&p, &last), "10 Hz refused");
  }
  if (failures) return 1;
  std::printf("vblank model: all checks passed\n");
  return 0;
}
