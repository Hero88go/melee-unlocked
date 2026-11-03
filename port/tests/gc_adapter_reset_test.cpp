// Exercise the actual reset request and I/O shutdown without opening any USB device.
#define GC_ADAPTER_TEST_HOOK
#include "../runtime/host/gc_adapter.cpp"
#include <cstdio>

namespace yougame {
bool g_test_stdio_bridge = false;
bool stdio_bridge() { return g_test_stdio_bridge; }
uintptr_t pad_mirror_handle() { return 0; }
}

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
  // The hook counts attempted starts/opens without accessing hardware, even on a regression.
  for (bool bridge : {true, false}) {
    yougame::g_test_stdio_bridge = bridge;
    options.no_gc_adapter = !bridge;
    ensure_scanner_started();
    check(gcadapter_problem() == (bridge ? "No GameCube adapter from the YouGame app" : ""),
          "disabled status query does not start scanner");
    check(!gcadapter_request_reset(), "disabled reset is a no-op");
    check(!open_adapter() && !open_libusb() && !open_hid(), "all open routes refuse disabled mode");
    start_threads();
    reader_thread();
    writer_thread();
    g_scanner_run = true;
    g_reset_requested = true;
    scanner_thread();  // Must return before handling even a queued reset.
    check(g_reset_requested, "disabled scanner leaves reset untouched");
    g_reset_requested = false;
    g_scanner_run = false;
    check(!g_test_scanner_starts && !g_test_libusb_opens && !g_test_hid_opens,
          "disabled entry points never reach scanner or transport opens");
    check(!g_scanner.joinable() && !g_thread.joinable() && !g_writer.joinable(),
          "disabled entry points launch no workers");
    check(!g_resetting && !g_ctx && !g_dev && g_hid == INVALID_HANDLE_VALUE,
          "disabled entry points leave transport and reset idle");
    g_poll_rate_hz = 999.0;
    check(gcadapter_poll_rate_hz() == 0.0, "missing mirror or disabled adapter has zero rate");
    int transferred = 99;
    uint8_t report[37]{};
    check(adapter_read(report, sizeof report, &transferred, 0) == LIBUSB_ERROR_NO_DEVICE && !transferred,
          "disabled transport cannot read");
    transferred = 99;
    check(adapter_write(report, sizeof report, &transferred, 0) == LIBUSB_ERROR_NO_DEVICE && !transferred,
          "disabled transport cannot write rumble");
  }
  yougame::g_test_stdio_bridge = false;
  options.no_gc_adapter = false;
  ensure_scanner_started();
  check(g_test_scanner_starts == 1 && g_scanner_run, "standalone lazy startup is still enabled");
  ensure_scanner_started();
  check(g_test_scanner_starts == 1, "standalone scanner starts only once");
  check(!open_adapter() && g_test_libusb_opens == 1 && g_test_hid_opens == 1,
        "standalone still tries libusb then HID");
  g_scanner_run = false;
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
