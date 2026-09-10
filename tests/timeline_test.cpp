#include "Timeline.h"
#include <cstdlib>
#include <iostream>
#include <limits>

static void Check(bool condition, const char* message)
{
  if (!condition)
  {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

int main()
{
  using namespace melee_unlocked;
  Timeline<int> timeline;
  const auto a = std::make_shared<const int>(10);
  const auto b = std::make_shared<const int>(20);
  Check(!timeline.At(0, 0).current, "empty history");
  Check(timeline.Push({1, 10, 1.0, a}), "first snapshot");
  Check(timeline.Push({1, 11, 1.0 + 1.0 / 60, b}), "second snapshot");
  auto sample = timeline.At(1.0 + 1.5 / 60, 1.0 / 60);
  Check(sample.interpolate && std::abs(sample.alpha - 0.5) < 1e-10, "midpoint");
  Check(!timeline.At(2, 0).interpolate, "never extrapolate stale history");
  Check(!timeline.Push({1, 9, 1.04, a}), "reject untagged rollback");
  timeline.Invalidate(2);
  Check(!timeline.At(2, 0).current, "rollback clears both snapshots");
  Check(!timeline.Push({1, 12, 1.04, b}), "reject stale queued generation");
  Check(timeline.Push({2, 8, 1.05, a}), "accept corrected earlier game frame");
  Check(!timeline.At(1.05, 0).interpolate, "first corrected snapshot holds");
  Check(timeline.Push({2, 10, 1.06, b}), "accept missing frame");
  Check(!timeline.At(1.065, 0.01).interpolate, "do not interpolate across missing frame");
  Check(timeline.Push({2, 11, 2.0, a}), "accept resumed frame");
  Check(!timeline.At(2.01, 0.02).interpolate, "do not interpolate across pause");
  Check(!timeline.Push({2, 12, std::numeric_limits<double>::quiet_NaN(), b}), "reject NaN");
  for (double fps : {60.0, 120.0, 144.0, 165.0, 240.0, 360.0})
  {
    PresentClock clock(fps);
    int count = 0;
    for (int i = 0; i < 100000; ++i)
      count += clock.Due(i / 10000.0) ? 1 : 0;
    Check(std::abs(count - fps * 10) <= 1, "arbitrary display cadence");
    Check(clock.Due(100), "recover after stall");
    Check(!clock.Due(100), "no catch-up burst");
  }
  PresentClock uncapped(0);
  Check(uncapped.Due(0) && uncapped.Due(0.00001), "uncapped readiness");
  Check(*a == 10 && *b == 20, "immutable snapshot inputs");
  std::cout << "Presentation timeline tests passed. Emulator integration is NOT tested.\n";
}
