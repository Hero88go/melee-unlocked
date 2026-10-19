// Decode synthetic raw USB reports with the production native adapter code.
// This fixture never calls initialize() or opens USB hardware.
#include "../gc_adapter.cpp"
#include <stdexcept>

int main() {
  using namespace linux_gc;
  auto check = [](bool value) { if (!value) throw std::runtime_error("native USB report check failed"); };
  MuPadStatus pads[4]{};
  check(poll(pads) == 0);
  report = {}; report[0] = 0x21;
  for (unsigned index = 0; index < 4; ++index) {
    auto* raw = report.data() + 1 + index * 9;
    raw[0] = 0x10; raw[1] = 0xff; raw[2] = 0x0f;
    raw[3] = raw[4] = raw[5] = raw[6] = 100;
    raw[7] = 10; raw[8] = 20;
  }
  have_report = true; received = Clock::now(); frequency = 1000;
  check(poll(pads) == 15);
  for (const auto& pad : pads) {
    check(pad.err == 0 && pad.button == 0x1f7f);
    check(pad.stick_x == 0 && pad.stick_y == 0 && pad.sub_x == 0 && pad.sub_y == 0);
    check(pad.trigger_l == 0 && pad.trigger_r == 0);
  }
  report[4] = 255; report[5] = 0; report[8] = 110; report[9] = 220;
  check(poll(pads) == 15 && pads[0].stick_x == 127 && pads[0].stick_y == -100);
  check(pads[0].trigger_l == 100 && pads[0].trigger_r == 200);
  report[10] = 0;
  check(poll(pads) == 13 && !origins[1].set);
  received = Clock::now() - std::chrono::milliseconds(600);
  check(poll(pads) == 0 && poll_rate() == 0 && !have_report);
  for (const auto& origin : origins) check(!origin.set);
  shutdown();
  std::puts("native Linux USB decoding: PASS");
}
