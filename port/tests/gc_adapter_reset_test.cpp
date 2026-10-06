// Exercise the actual reset request and I/O shutdown without opening any USB device.
#include "../runtime/host/gc_adapter.cpp"
#include <cstdio>

namespace host {
Options options;
TickTiming& tick_timing() { static TickTiming timing; return timing; }
void log(const char*, ...) {}
}

int main() {
  using namespace host;
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
  };
  // Prime lazy startup so this fixture can never enumerate or claim real hardware.
  std::call_once(g_scanner_started, [] {});
  options.no_gc_adapter = true;
  check(!gcadapter_request_reset(), "automated runs do not open adapters");
  check(!gcadapter_reset_pending(), "disabled request stays idle");
  options.no_gc_adapter = false;
  check(!gcadapter_request_reset(), "inactive scanner rejects reset");
  g_scanner_run = true;
  g_running = true;
  g_have_report = true;
  g_report[0] = 0x21;
  g_poll_rate_hz = 1000.0;
  for (auto& origin : g_origin) origin.set = true;
  check(gcadapter_request_reset(), "active scanner accepts reset");
  check(gcadapter_reset_pending() && g_reset_requested, "reset is queued");
  check(!g_have_report && gcadapter_poll_rate_hz() == 0.0, "stale report and rate cleared");
  for (const auto& origin : g_origin) check(!origin.set, "all sockets recalibrate");
  check(!gcadapter_request_reset(), "duplicate request coalesced");
  // A late USB report must also stay out of the simulation while reconnecting.
  g_have_report = true;
  PadState pads[4]{};
  check(gcadapter_poll(pads) == 0, "pending reset suppresses late reports");
  gcadapter_rumble(0, true);
  check(g_rumble[0] == 0, "reset suppresses new rumble");
  std::atomic<int> ready{0}, stopped{0};
  g_thread = std::thread([&] {
    ++ready;
    while (g_running.load()) std::this_thread::yield();
    ++stopped;
  });
  g_writer = std::thread([&] {
    ++ready;
    std::unique_lock<std::mutex> lock(g_rumble_mutex);
    g_rumble_wake.wait(lock, [] { return !g_running.load(); });
    ++stopped;
  });
  while (ready != 2) std::this_thread::yield();
  close_adapter(true);  // Handles are null: only the real lifecycle runs, no USB calls.
  check(stopped == 2 && !g_thread.joinable() && !g_writer.joinable(), "both I/O workers joined");
  check(!g_have_report && !g_running, "connection state cleared");
  g_scanner_run = false;
  gcadapter_shutdown();
  check(!gcadapter_reset_pending() && !g_reset_requested, "shutdown clears pending reset");
  std::printf("adapter reset lifecycle: %s\n", failures ? "FAIL" : "PASS");
  return failures != 0;
}
