// Window-free histogram, exclusive stage and resimulation accounting checks.
#include "sim_work.h"
#include <cstdio>
#include <cstdlib>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace host::perf;
static bool near(double a, double b) { return std::abs(a - b) < 0.000001; }
int main() {
  CHECK(active == nullptr);
  {
    Scope inactive(Stage::Decode);
    CHECK(sim_timer.stage == Stage::Step && sim_timer.last == Timer::Clock::time_point{});
  }
  CHECK(sim_timer.last == Timer::Clock::time_point{} && sim_timer.work.count == 0);
  Histogram h;
  CHECK(h.mean() == 0 && h.p99() == 0 && h.maximum == 0);
  for (int i = 0; i < 98; ++i) h.add(1.001);
  h.add(4.001); h.add(20);
  CHECK(h.count == 100 && near(h.mean(), (98 * 1.001 + 4.001 + 20) / 100));
  CHECK(near(h.p99(), 4.05) && h.maximum == 20);
  Histogram edge;
  edge.add(0); CHECK(edge.p99() == 0);
  edge.add(0.05); CHECK(near(edge.p99(), 0.05));
  edge.add(100); CHECK(edge.p99() == 100);
  edge.add(100.001); CHECK(edge.p99() == edge.maximum && edge.maximum > 100);
  Histogram many;
  for (int i = 0; i < 9900; ++i) many.add(2);
  for (int i = 0; i < 100; ++i) many.add(200);
  CHECK(many.p99() == 2);
  many.add(200); CHECK(many.p99() == 200); // overflow rank, no clipping

  Timer t;
  const auto at = [](int ms) { return Timer::Clock::time_point{} + std::chrono::milliseconds(ms); };
  t.finish(at(0)); CHECK(t.work.count == 0); // no invented startup sample
  t.begin(at(0));
  const auto step = t.enter(Stage::Decode, at(2));
  const auto decode = t.enter(Stage::Submit, at(3));
  const auto submit = t.enter(Stage::Wait, at(4));
  t.enter(submit, at(14));
  t.enter(decode, at(15));
  t.enter(step, at(16));
  t.enter(Stage::Input, at(17));
  t.enter(Stage::Step, at(18));
  t.set_resim(true, 100, true, at(19));
  t.set_resim(true, 101, true, at(20)); // duplicate does not lose the first resim frame
  t.enter(Stage::Decode, at(20));
  t.enter(Stage::Step, at(25));
  t.set_resim(false, 103, true, at(29));
  const auto frame = t.finish(at(31));
  CHECK(near(frame.get(Stage::Step), 6) && near(frame.get(Stage::Decode), 2));
  CHECK(near(frame.get(Stage::Submit), 2) && near(frame.get(Stage::Input), 1));
  CHECK(near(frame.get(Stage::Wait), 10) && near(frame.work_ms(), 11));
  CHECK(t.work.count == 1 && near(t.work.mean(), 11) && t.resim_frames == 3);
  t.finish(at(500)); CHECK(t.work.count == 1); // repeated finish never samples shutdown
  t.begin(at(500)); t.finish(at(502));
  CHECK(t.work.count == 2 && near(t.work.mean(), 6.5));
  CHECK(near(t.mean(Stage::Wait), 5));
  t.set_resim(true, 100, false, at(503));
  t.set_resim(false, 150, true, at(504));
  CHECK(t.resim_frames == 3); // failed query cannot invent frames
  t.set_resim(true, 100, true, at(505));
  t.set_resim(false, 0, true, at(506));
  CHECK(t.resim_frames == 3); // a scene reset cannot underflow
  Timer only_resim;
  only_resim.set_resim(true, 10, true, at(0));
  only_resim.begin(at(0));
  const auto skipped = only_resim.finish(at(100));
  CHECK(only_resim.work.count == 0 && skipped.work_ms() == 0);
  only_resim.set_resim(false, 12, true, at(100));
  only_resim.set_resim(false, 12, true, at(101));
  CHECK(only_resim.resim_frames == 2);
  only_resim.begin(at(102)); only_resim.finish(at(103));
  CHECK(only_resim.work.count == 1 && only_resim.work.mean() == 1);
  std::thread simulation([] {
    active = &sim_timer;
    active->begin();
    {
      Scope decode_cost(Stage::Decode);
      CHECK(sim_timer.stage == Stage::Decode);
      { Scope submit_cost(Stage::Submit);
        { Scope wait_cost(Stage::Wait); CHECK(sim_timer.stage == Stage::Wait); }
        CHECK(sim_timer.stage == Stage::Submit);
      }
      CHECK(sim_timer.stage == Stage::Decode);
      const auto last = sim_timer.last;
      const auto frame = sim_timer.frame.ms;
      // A renderer/worker stays inactive even while the simulation timer is running.
      std::thread worker([] {
        CHECK(active == nullptr);
        Scope decode(Stage::Decode);
        Scope wait(Stage::Wait);
        CHECK(active == nullptr);
      });
      worker.join();
      CHECK(sim_timer.stage == Stage::Decode && sim_timer.last == last);
      CHECK(sim_timer.frame.ms == frame && sim_timer.work.count == 0);
    }
    CHECK(sim_timer.stage == Stage::Step);
    active->finish();
  });
  simulation.join();
  // Shutdown/reporting runs on an inactive thread after the simulation thread is gone.
  CHECK(active == nullptr);
  const auto& report = sim_timer;
  CHECK(!report.running && report.work.count == 1 && report.work.mean() > 0);
  const auto last = report.last;
  const auto totals = report.totals;
  { Scope inactive(Stage::Input); }
  CHECK(report.last == last && report.totals == totals && report.work.count == 1);
  std::puts("sim_work: histogram bounds/rank/overflow, exclusive stages, waits and resim ok");
}
