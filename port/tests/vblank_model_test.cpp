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
  using gx::WakeFilter;
  {
    // A display that refreshes: every return is a blank, at 60 Hz and at 500 Hz.
    for (double hz : {60.0, 500.0}) {
      WakeFilter f;
      int blanks = 0;
      for (int i = 0; i < 200; ++i) blanks += f.add(start + i / hz) == WakeFilter::kBlank;
      check(blanks == 200, "every refresh is a blank");
    }
  }
  {
    // 360 Hz, the thread woke 2.2 ms late once: the blank right after it is skipped, nothing sleeps,
    // and the waits after it count again.
    const double p = 1.0 / 360.0;
    WakeFilter f;
    int blanks = 0, skips = 0, sleeps = 0;
    for (int i = 0; i < 100; ++i) {
      const double t = start + i * p + (i == 50 ? 0.0022 : 0.0);
      const WakeFilter::Verdict v = f.add(t);
      blanks += v == WakeFilter::kBlank; skips += v == WakeFilter::kSkip; sleeps += v == WakeFilter::kSleep;
    }
    check(sleeps == 0, "a late wake never sleeps the thread");
    check(skips == 1 && blanks == 99, "only the return right after the late wake is skipped");
  }
  {
    // A display asleep: the wait returns every few microseconds. Within a few returns the thread
    // sleeps, and after a sleep the next immediate return sleeps it again (no spinning).
    WakeFilter f;
    double t = start;
    int counted = 0, before_sleep = 0;
    WakeFilter::Verdict v = WakeFilter::kBlank;
    while ((v = f.add(t)) != WakeFilter::kSleep && before_sleep < 100) { counted += v == WakeFilter::kBlank; ++before_sleep; t += 0.000003; }
    check(v == WakeFilter::kSleep && before_sleep <= 4, "an asleep display sleeps the thread within a few returns");
    check(counted <= 1, "immediate returns are not counted as blanks");
    t += 0.010; f.slept(t);
    check(f.add(t + 0.000003) == WakeFilter::kSleep, "after a sleep, the next immediate return sleeps again");
    // The display wakes: blanks at 144 Hz resume from a moment unrelated to the sleep's end.
    t += 0.0004; f.slept(t);
    int resumed_at = -1;
    for (int i = 0; i < 20; ++i) if (f.add(t + 0.0003 + i * period) == WakeFilter::kBlank && resumed_at < 0) resumed_at = i;
    check(resumed_at >= 0 && resumed_at <= 1, "blanks count again once the display refreshes");
    int later = 0;
    for (int i = 20; i < 60; ++i) later += f.add(t + 0.0003 + i * period) == WakeFilter::kBlank;
    check(later == 40, "and every blank after that counts");
  }
  if (failures) return 1;
  std::printf("vblank model: all checks passed\n");
  return 0;
}
