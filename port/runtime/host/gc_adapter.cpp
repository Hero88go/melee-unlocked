#include "test_env.h"
#ifdef __APPLE__
#include "host.h"
#include "gc_adapter_mirror_mac.h"
#else
// Official / Mayflash / Hand Held Legend "GameCube Controller Adapter for Wii U", through libusb as
// Dolphin uses it. Same protocol as Dolphin's GCAdapter: one 0x13 byte starts the 37-byte report
// stream on endpoint 0x81 (status + 9 bytes per port), 0x11 + 4 bytes sets rumble.
//
// This used to talk to WinUSB directly, which meant an adapter installed with libusbK or
// libusb-win32 was invisible here while Dolphin reported it detected at 1 kHz, and players were told
// to replace a driver that already worked for them. libusb's Windows backend speaks all three.
//
// When libusb cannot reach the adapter, the same reports are read through the HID interface instead.
// That is the case under Wine and Proton, which have no WinUSB for a real USB device but can pass the
// adapter through as a HID device, and on Windows for an adapter still on its stock HID driver.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.h"
#include "phob.h"
#include "yougame_link.h"
#ifndef _WIN32
#include "pad_mirror_posix.h"
#endif
#define NOMINMAX
#include <windows.h>
#ifdef _WIN32
#include <timeapi.h>
#include <setupapi.h>
#endif
#include <tlhelp32.h>
#include <hidsdi.h>
#include <libusb.h>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include <mutex>
#include <string>
#include <thread>

namespace host {
namespace {

// The app's helper is the sole adapter owner in stdio-bridge mode. Keep this check at
// startup and at the transport entry points, including reset and the I/O workers.
bool direct_adapter_allowed() { return !options.no_gc_adapter && !yougame::stdio_bridge(); }
#ifdef GC_ADAPTER_TEST_HOOK
unsigned g_test_scanner_starts = 0, g_test_libusb_opens = 0, g_test_hid_opens = 0;
#endif

enum : uint16_t {
  PAD_LEFT = 0x0001, PAD_RIGHT = 0x0002, PAD_DOWN = 0x0004, PAD_UP = 0x0008, PAD_Z = 0x0010, PAD_R = 0x0020, PAD_L = 0x0040,
  PAD_A = 0x0100, PAD_B = 0x0200, PAD_X = 0x0400, PAD_Y = 0x0800, PAD_START = 0x1000,
};

libusb_context* g_ctx = nullptr;
libusb_device_handle* g_dev = nullptr;
bool g_claimed = false;
// Nintendo's WUP-028 puts its data on interface 0 with endpoints 0x81 in and 0x02 out, and we used to
// assume that. Third-party adapters (the Hand Held Legend GC Pocket+) report the same USB identity but
// use different numbers, so every transfer failed with LIBUSB_ERROR_NOT_FOUND while the device opened
// perfectly: detected, claimed, and silent. These are discovered from the descriptors instead.
int g_iface = 0;
uint8_t g_ep_in = 0x81, g_ep_out = 0x02;
// The HID route: used only while g_dev is null. A HID report carries its id in the first byte, and
// the adapter's ids are its command bytes (0x13, 0x11, 0x21), so the bytes are the same on both routes.
HANDLE g_hid = INVALID_HANDLE_VALUE;
HANDLE g_hid_read_event = nullptr, g_hid_write_event = nullptr;
DWORD g_hid_in_len = 37, g_hid_out_len = 5;
std::mutex g_hid_write_mutex;   // the reader re-sends start while the writer sends rumble
bool g_logged_hid_fail = false;
std::thread g_thread;
std::atomic<bool> g_running{false};
std::atomic<double> g_poll_rate_hz{0.0};
std::atomic<bool> g_reset_requested{false};
std::atomic<bool> g_resetting{false};
std::mutex g_mutex;
uint8_t g_report[37] = {};
std::chrono::steady_clock::time_point g_report_time;
bool g_have_report = false;
bool g_logged_missing = false;
bool g_logged_restart = false;   // logged once per open when the adapter is silent and we retry
// Why an adapter that is plugged in cannot be read, for the Controls tab (gcadapter_problem). It
// used to reach the log only, so a player whose adapter Steam had taken saw a controller that half
// worked (port 1 fell back to Steam's own Xbox copy of it, with Steam's button layout) and no reason.
std::mutex g_problem_mutex;
std::string g_problem;
void set_problem(const std::string& text) { std::lock_guard<std::mutex> lock(g_problem_mutex); g_problem = text; }
constexpr const char* kFrame1Note = " A box controller plugged in normally, such as a Frame1, counts as an adapter.";
struct Origin { bool set = false; uint8_t sx = 128, sy = 128, cx = 128, cy = 128, tl = 0, tr = 0; } g_origin[4];

// PhobGCC settings per adapter port (phob.h). The firmware's loop runs on the reader thread, once
// per millisecond of adapter reports, so its filters see every report the adapter sends; the poll
// then hands the game the latest result. All of it under g_mutex.
phob::Prepared g_phob[4];
phob::PortState g_phob_state[4];
phob::Output g_phob_out[4];
bool g_phob_live[4] = {};
std::chrono::steady_clock::time_point g_phob_last;
bool g_phob_have_last = false;
// Every report as read and as the Phob settings shaped it, for the Phob scope menus.
constexpr int kScopeLen = 4096;
GCScopeSample g_scope[4][kScopeLen];
uint32_t g_scope_head[4] = {};

phob::HardwareInput phob_input(const uint8_t* c, const Origin& o) {
  phob::HardwareInput in;
  uint16_t b = 0;
  if (c[1] & 0x01) b |= PAD_A; if (c[1] & 0x02) b |= PAD_B; if (c[1] & 0x04) b |= PAD_X; if (c[1] & 0x08) b |= PAD_Y;
  if (c[1] & 0x10) b |= PAD_LEFT; if (c[1] & 0x20) b |= PAD_RIGHT; if (c[1] & 0x40) b |= PAD_DOWN; if (c[1] & 0x80) b |= PAD_UP;
  if (c[2] & 0x01) b |= PAD_START; if (c[2] & 0x02) b |= PAD_Z; if (c[2] & 0x04) b |= PAD_R; if (c[2] & 0x08) b |= PAD_L;
  in.buttons = b;
  in.ax = c[3]; in.ay = c[4]; in.cx = c[5]; in.cy = c[6];
  in.ax0 = o.sx; in.ay0 = o.sy; in.cx0 = o.cx; in.cy0 = o.cy;
  in.la = (uint8_t)(c[7] > o.tl ? c[7] - o.tl : 0);
  in.ra = (uint8_t)(c[8] > o.tr ? c[8] - o.tr : 0);
  return in;
}

// Called with g_mutex held, for each new report.
void phob_process(const uint8_t* report, std::chrono::steady_clock::time_point now) {
  int steps = 1;
  if (g_phob_have_last) {
    const double ms = std::chrono::duration<double, std::milli>(now - g_phob_last).count();
    steps = (int)(ms + 0.5);
    steps = steps < 1 ? 1 : steps > 50 ? 50 : steps;
  }
  g_phob_last = now;
  g_phob_have_last = true;
  const uint32_t t = (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
  for (int port = 0; port < 4; ++port) {
    const uint8_t* c = report + 1 + port * 9;
    if (!(c[0] & 0x30) || !g_origin[port].set) { g_phob_live[port] = false; g_phob_state[port].primed = false; continue; }
    const phob::HardwareInput in = phob_input(c, g_origin[port]);
    GCScopeSample& s = g_scope[port][g_scope_head[port] % kScopeLen];
    s.time_us = t;
    s.raw_ax = (int8_t)((int)in.ax - in.ax0); s.raw_ay = (int8_t)((int)in.ay - in.ay0);
    s.raw_cx = (int8_t)((int)in.cx - in.cx0); s.raw_cy = (int8_t)((int)in.cy - in.cy0);
    s.raw_l = in.la; s.raw_r = in.ra;
    s.raw_buttons = in.buttons; s.hw_ax = in.ax; s.hw_ay = in.ay; s.hw_cx = in.cx; s.hw_cy = in.cy;
    if (g_phob[port].s.enabled) {
      g_phob_out[port] = phob::step(g_phob[port], g_phob_state[port], in, steps);
      g_phob_live[port] = true;
      // The firmware's "unfiltered" stick: through the calibration, before the filters.
      auto unf = [](float v) { const long r = std::lround(v); return (int8_t)(r > 127 ? 127 : r < -128 ? -128 : r); };
      s.raw_ax = unf(g_phob_state[port].ax_unfilt); s.raw_ay = unf(g_phob_state[port].ay_unfilt);
      s.raw_cx = unf(g_phob_state[port].cx_unfilt); s.raw_cy = unf(g_phob_state[port].cy_unfilt);
      s.ax = g_phob_out[port].stick_x; s.ay = g_phob_out[port].stick_y;
      s.cx = g_phob_out[port].sub_x; s.cy = g_phob_out[port].sub_y;
      s.l = g_phob_out[port].trig_l; s.r = g_phob_out[port].trig_r;
      s.buttons = g_phob_out[port].buttons;
    } else {
      g_phob_live[port] = false;
      g_phob_state[port].primed = false;
      s.ax = s.raw_ax; s.ay = s.raw_ay; s.cx = s.raw_cx; s.cy = s.raw_cy;
      s.l = s.raw_l; s.r = s.raw_r; s.buttons = s.raw_buttons;
    }
    ++g_scope_head[port];
  }
}
std::atomic<uint8_t> g_rumble[4]{};
std::atomic<bool> g_rumble_dirty{false};
std::thread g_writer;
std::mutex g_rumble_mutex;
std::condition_variable g_rumble_wake;

// Launched by YouGame, the game never opens the adapter: the YouGame app's controller helper holds
// it (and inside the app's sandbox no USB device can be opened at all). The helper keeps every raw
// report it reads in a shared section, the "pad mirror", whose handle the app passes over the bridge
// (yougame_link.h pad_mirror_handle), and takes the rumble back from it. The reports are the
// adapter's own bytes, so everything below (neutral points, triggers, recalibration) is unchanged.
// The layout is the helper's (desktop/native/src/main.cpp in the YouGame repository).
#ifdef _WIN32
struct PadMirror {
  char magic[4];               // "YGPM"
  uint32_t version;            // 1
  volatile LONG seq;           // even: stable; odd: being written
  uint32_t reports;            // reports written
  double at_ms;                // the helper's clock
  float rate_hz;               // the helper's reads per second
  uint8_t connected;           // the adapter is open and reporting
  uint8_t report[37];          // the raw report
  uint8_t reserved[2];
  volatile LONG rumble_seq;    // ours: bumped after each rumble change
  uint8_t rumble[4];           // ours: motor on per port
};
static_assert(offsetof(PadMirror, report) == 29 && offsetof(PadMirror, rumble_seq) == 68 && sizeof(PadMirror) == 80, "the helper's layout");
PadMirror* g_mirror = nullptr;           // under g_mutex
uintptr_t g_mirror_handle = 0;
uint32_t g_mirror_reports = 0;
std::chrono::steady_clock::time_point g_mirror_moved;

// Maps the section the app named, once per handle (a restarted helper hands over a new one).
void attach_mirror() {
  const uintptr_t handle = yougame::pad_mirror_handle();
  if (!handle || handle == g_mirror_handle) return;
  if (g_mirror) UnmapViewOfFile(g_mirror);
  g_mirror_handle = handle;
  g_mirror = (PadMirror*)MapViewOfFile((HANDLE)handle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(PadMirror));
  if (g_mirror && (std::memcmp(g_mirror->magic, "YGPM", 4) != 0 || g_mirror->version != 1)) {
    UnmapViewOfFile(g_mirror);
    g_mirror = nullptr;
  }
  for (auto& o : g_origin) o.set = false;
  log(g_mirror ? "gc adapter: reading the YouGame app's adapter" : "gc adapter: the YouGame app's adapter could not be read (error %lu)", GetLastError());
}

// The latest report from the mirror; false when there is none, the adapter is gone or it went quiet.
bool read_mirror(uint8_t rep[37]) {
  for (int attempt = 0; attempt < 8; ++attempt) {
    const LONG before = g_mirror->seq;
    if (before & 1) { YieldProcessor(); continue; }
    MemoryBarrier();
    std::memcpy(rep, g_mirror->report, 37);
    const bool connected = g_mirror->connected != 0;
    const uint32_t reports = g_mirror->reports;
    const float rate = g_mirror->rate_hz;
    MemoryBarrier();
    if (g_mirror->seq != before) continue;
    const auto now = std::chrono::steady_clock::now();
    if (reports != g_mirror_reports) { g_mirror_reports = reports; g_mirror_moved = now; }
    // The helper rewrites the report on every read, changed or not, so half a second without a new
    // one means the helper or the adapter stopped.
    const bool live = connected && reports && now - g_mirror_moved < std::chrono::milliseconds(500) && rep[0] == 0x21;
    g_poll_rate_hz.store(live ? rate : 0.0, std::memory_order_relaxed);
    return live;
  }
  g_poll_rate_hz.store(0.0, std::memory_order_relaxed);
  return false;
}

// A live report from an attached mirror (read_mirror itself assumes one is attached).
bool mirror_live(uint8_t rep[37]) { return g_mirror && read_mirror(rep); }
#else
yougame::pad_mirror::Reader g_posix_mirror;
void attach_mirror() {
  g_posix_mirror.attach_once(yougame::pad_mirror_attachment(),
      [] { for (auto& o : g_origin) o.set = false; }, log);
}
bool read_mirror(uint8_t rep[37]) {
  const bool live = g_posix_mirror.read(rep);
  g_poll_rate_hz.store(g_posix_mirror.rate_hz(), std::memory_order_relaxed);
  return live;
}
bool mirror_live(uint8_t rep[37]) { return g_posix_mirror.attached() && read_mirror(rep); }
#endif

// Finds the adapter and records the VID:PID of everything else, so a log from someone whose adapter
// is not recognised says what they actually had plugged in.
libusb_device* find_adapter(libusb_device** list, ssize_t count, std::string* others) {
  libusb_device* adapter = nullptr;
  int listed = 0;
  for (ssize_t i = 0; i < count; ++i) {
    libusb_device_descriptor desc{};
    if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
    if (desc.idVendor == 0x057E && desc.idProduct == 0x0337) { adapter = list[i]; break; }
    if (others && listed < 16) {
      char id[16];
      std::snprintf(id, sizeof id, "%04x:%04x", desc.idVendor, desc.idProduct);
      if (!others->empty()) *others += ", ";
      *others += id;
      ++listed;
    }
  }
  return adapter;
}

// One overlapped read or write on the HID handle, with the result in libusb's terms so both routes
// share the reader's error handling.
#ifndef _WIN32
// Linux reads the adapter through libusb only (no WinUSB problem to work around), so the HID route
// is never opened there.
int hid_transfer(bool, uint8_t*, DWORD, DWORD* done, DWORD) { *done = 0; return LIBUSB_ERROR_IO; }
#else
int hid_transfer(bool write, uint8_t* data, DWORD length, DWORD* done, DWORD timeout_ms) {
  OVERLAPPED ov{};
  ov.hEvent = write ? g_hid_write_event : g_hid_read_event;
  ResetEvent(ov.hEvent);
  *done = 0;
  const BOOL ok = write ? WriteFile(g_hid, data, length, done, &ov) : ReadFile(g_hid, data, length, done, &ov);
  if (ok) return 0;
  if (GetLastError() != ERROR_IO_PENDING) return LIBUSB_ERROR_IO;
  if (WaitForSingleObject(ov.hEvent, timeout_ms) == WAIT_OBJECT_0)
    return GetOverlappedResult(g_hid, &ov, done, FALSE) ? 0 : LIBUSB_ERROR_IO;
  // The request must be finished before `ov` leaves scope; one that completed during the cancel counts.
  CancelIoEx(g_hid, &ov);
  return GetOverlappedResult(g_hid, &ov, done, TRUE) ? 0 : LIBUSB_ERROR_TIMEOUT;
}
#endif

int adapter_write(const uint8_t* data, int length, int* wrote, unsigned timeout_ms) {
  if (!direct_adapter_allowed()) { *wrote = 0; return LIBUSB_ERROR_NO_DEVICE; }
  if (g_dev) return libusb_interrupt_transfer(g_dev, g_ep_out, const_cast<uint8_t*>(data), length, wrote, timeout_ms);
  // Windows wants every output report padded to the longest one the device declares. If that is
  // refused, the exact length is tried: a raw passthrough may hand the bytes to the device as they are.
  std::lock_guard<std::mutex> lock(g_hid_write_mutex);
  uint8_t padded[64] = {};
  std::memcpy(padded, data, (size_t)length);
  const DWORD full = g_hid_out_len > (DWORD)length ? (g_hid_out_len < sizeof padded ? g_hid_out_len : (DWORD)sizeof padded) : (DWORD)length;
  DWORD done = 0;
  int rc = hid_transfer(true, padded, full, &done, timeout_ms);
  if (rc == LIBUSB_ERROR_IO && full != (DWORD)length) rc = hid_transfer(true, padded, (DWORD)length, &done, timeout_ms);
  *wrote = rc == 0 ? length : 0;
  return rc;
}

int adapter_read(uint8_t* data, int length, int* got, unsigned timeout_ms) {
  if (!direct_adapter_allowed()) { *got = 0; return LIBUSB_ERROR_NO_DEVICE; }
  if (g_dev) return libusb_interrupt_transfer(g_dev, g_ep_in, data, length, got, timeout_ms);
  uint8_t report[256];
  const DWORD want = g_hid_in_len < sizeof report ? g_hid_in_len : (DWORD)sizeof report;
  DWORD done = 0;
  const int rc = hid_transfer(false, report, want, &done, timeout_ms);
  *got = 0;
  if (rc != 0) return rc;
  *got = (int)done < length ? (int)done : length;
  std::memcpy(data, report, (size_t)*got);
  return 0;
}

bool g_detached_kernel = false;   // Linux: the kernel driver this game let go of, given back on close

void reader_thread() {
  if (!direct_adapter_allowed()) return;
  // The adapter is polled as fast as this thread asks again. The simulation and render threads run
  // above normal priority, and at normal priority this one was sometimes late with the next request:
  // an adapter that shows its rate dropped from 1000 Hz to 500 or lower. It only sleeps in the USB
  // transfer, so the highest priority costs nothing.
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  auto send_start = [&] {
    uint8_t start = 0x13;
    int wrote = 0;
    int rc = adapter_write(&start, 1, &wrote, 100);
    if (rc == LIBUSB_ERROR_PIPE && g_dev) {
      libusb_clear_halt(g_dev, g_ep_out);
      wrote = 0;
      rc = adapter_write(&start, 1, &wrote, 100);
    }
    if (rc != 0 || wrote != 1) log("gc adapter: start command failed (%s, wrote %d)", libusb_error_name(rc), wrote);
  };
  send_start();
  // Some Wii U-mode adapters need the all-off rumble command after report startup.
  uint8_t rumble_off[5] = {0x11, 0, 0, 0, 0};
  int rumble_wrote = 0;
  const int rumble_rc = adapter_write(rumble_off, sizeof rumble_off, &rumble_wrote, 100);
  if (rumble_rc != 0 || rumble_wrote != sizeof rumble_off)
    log("gc adapter: initial rumble reset failed (%s, wrote %d)", libusb_error_name(rumble_rc), rumble_wrote);
  int failures = 0, silent = 0;
  auto poll_window_start = std::chrono::steady_clock::now();
  auto last_report = poll_window_start;
  unsigned reports_in_window = 0;
  while (g_running.load() && direct_adapter_allowed()) {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_report >= std::chrono::milliseconds(500)) {
      g_poll_rate_hz.store(0.0, std::memory_order_relaxed);
      reports_in_window = 0;
      poll_window_start = now;
      std::lock_guard<std::mutex> lk(g_mutex);
      g_have_report = false;
    }
    uint8_t buf[37];
    int got = 0;
    const int rc = adapter_read(buf, (int)sizeof buf, &got, 100);
    if (rc == 0) {
      failures = 0; silent = 0;
      if (got == 37 && buf[0] == 0x21) {
        const auto received = std::chrono::steady_clock::now();
        last_report = received;
        if (++reports_in_window == 50) {
          const double seconds = std::chrono::duration<double>(received - poll_window_start).count();
          g_poll_rate_hz.store(seconds > 0.0 ? 50.0 / seconds : 0.0, std::memory_order_relaxed);
          poll_window_start = received;
          reports_in_window = 0;
        }
        std::lock_guard<std::mutex> lk(g_mutex);
        std::memcpy(g_report, buf, 37);
        g_report_time = received;
        g_have_report = true;
        phob_process(buf, received);
      }
    } else {
      if (rc == LIBUSB_ERROR_TIMEOUT) {
        // A timeout used to loop forever without counting, so an adapter that was connected but not
        // streaming was never retried and never reported: it stayed dead until it was physically
        // unplugged, which is what made replugging "fix" it. An adapter left mid-stream by a crash
        // does exactly this. Reset the read pipe and ask it to start again about once a second.
        if (++silent >= 10) {
          silent = 0;
          if (g_dev) libusb_clear_halt(g_dev, g_ep_in);
          send_start();
          if (!g_logged_restart) { log("gc adapter: no reports yet, clearing the pipe and re-sending start"); g_logged_restart = true; }
        }
        continue;
      }
      if (++failures > 20) { log("gc adapter: read failed (%s), adapter disconnected", libusb_error_name(rc)); break; }
    }
  }
  g_poll_rate_hz.store(0.0, std::memory_order_relaxed);
  g_running.store(false);
  g_rumble_wake.notify_all();
}

// Rumble commands go out on their own thread, as in Dolphin. They used to be sent from the reader
// between two reads, so every motor change delayed the next read by the length of an output
// transfer and the adapter saw a gap in its polling.
void writer_thread() {
  while (g_running.load() && direct_adapter_allowed()) {
    {
      std::unique_lock<std::mutex> lock(g_rumble_mutex);
      g_rumble_wake.wait_for(lock, std::chrono::milliseconds(100),
                             [] { return g_rumble_dirty.load() || !g_running.load(); });
    }
    if (!g_running.load() || !direct_adapter_allowed()) break;
    if (g_rumble_dirty.exchange(false)) {
      uint8_t cmd[5] = {0x11, g_rumble[0], g_rumble[1], g_rumble[2], g_rumble[3]};
      int wrote = 0;
      adapter_write(cmd, (int)sizeof cmd, &wrote, 32);
    }
  }
}

void close_adapter(bool reset_transport = false) {
  reset_transport = reset_transport && direct_adapter_allowed();
  {
    // Under the writer's mutex: a store between its predicate check and its sleep would be a
    // wakeup it never sees.
    std::lock_guard<std::mutex> lock(g_rumble_mutex);
    g_running.store(false);
  }
  g_rumble_wake.notify_all();
  if (g_thread.joinable()) g_thread.join();
  if (g_writer.joinable()) g_writer.join();
  g_poll_rate_hz.store(0.0, std::memory_order_relaxed);
  for (auto& motor : g_rumble) motor.store(0);
  g_rumble_dirty.store(false);
  if (reset_transport && g_dev) {
    // Both I/O threads have stopped before touching the device or its pipes.
    const int rc = libusb_reset_device(g_dev);
    log("gc adapter: USB reset %s; reopening the connection", libusb_error_name(rc));
  } else if (reset_transport && g_hid != INVALID_HANDLE_VALUE) {
    if (!HidD_FlushQueue(g_hid))
      log("gc adapter: HID queue flush failed (%lu); reopening the connection", GetLastError());
  }
  if (g_dev) {
    if (g_claimed) libusb_release_interface(g_dev, g_iface);
#ifndef _WIN32
    if (g_detached_kernel) libusb_attach_kernel_driver(g_dev, g_iface);   // given back, as before
    g_detached_kernel = false;
#endif
    libusb_close(g_dev);
    g_dev = nullptr;
  }
  g_claimed = false;
  if (g_hid != INVALID_HANDLE_VALUE) { CloseHandle(g_hid); g_hid = INVALID_HANDLE_VALUE; }
  std::lock_guard<std::mutex> lk(g_mutex);
  g_have_report = false;
  for (auto& o : g_origin) o.set = false;
}

bool adapter_open() { return g_dev || g_hid != INVALID_HANDLE_VALUE; }

void start_threads() {
  if (!direct_adapter_allowed()) return;
  g_logged_missing = false;
  g_logged_restart = false;
  g_poll_rate_hz.store(0.0, std::memory_order_relaxed);
  g_running.store(true);
  g_thread = std::thread(reader_thread);
  g_writer = std::thread(writer_thread);
}

// The adapter as a HID device. Only interface paths that name the adapter are opened, so no other
// HID device is touched by the scan.
#ifndef _WIN32
bool open_hid() { return false; }
#else
bool open_hid() {
  if (!direct_adapter_allowed()) return false;
#ifdef GC_ADAPTER_TEST_HOOK
  ++g_test_hid_opens;
  return false;
#endif
  GUID hid_guid;
  HidD_GetHidGuid(&hid_guid);
  HDEVINFO set = SetupDiGetClassDevsA(&hid_guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (set == INVALID_HANDLE_VALUE) return false;
  bool seen = false;
  DWORD open_error = 0;
  SP_DEVICE_INTERFACE_DATA itf{};
  itf.cbSize = sizeof itf;
  for (DWORD i = 0; g_hid == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(set, nullptr, &hid_guid, i, &itf); ++i) {
    union { SP_DEVICE_INTERFACE_DETAIL_DATA_A detail; char bytes[1024]; } buf{};
    buf.detail.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
    if (!SetupDiGetDeviceInterfaceDetailA(set, &itf, &buf.detail, sizeof buf - 1, nullptr, nullptr)) continue;
    std::string path = buf.detail.DevicePath;
    std::string lower = path;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    if (lower.find("vid_057e&pid_0337") == std::string::npos) continue;
    seen = true;
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (file == INVALID_HANDLE_VALUE) { open_error = GetLastError(); continue; }
    g_hid_in_len = 37; g_hid_out_len = 5;
    PHIDP_PREPARSED_DATA parsed = nullptr;
    if (HidD_GetPreparsedData(file, &parsed)) {
      HIDP_CAPS caps{};
      if (HidP_GetCaps(parsed, &caps) == HIDP_STATUS_SUCCESS) {
        if (caps.InputReportByteLength) g_hid_in_len = caps.InputReportByteLength;
        if (caps.OutputReportByteLength) g_hid_out_len = caps.OutputReportByteLength;
      }
      HidD_FreePreparsedData(parsed);
    }
    g_hid = file;
  }
  SetupDiDestroyDeviceInfoList(set);
  if (g_hid == INVALID_HANDLE_VALUE) {
    if (seen && !g_logged_hid_fail) {
      log("gc adapter: present as a HID device but cannot be opened (error %lu)", (unsigned long)open_error);
      g_logged_hid_fail = true;
    }
    return false;
  }
  if (!g_hid_read_event) g_hid_read_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if (!g_hid_write_event) g_hid_write_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  set_problem("");
  log("gc adapter: opened through HID (input report %lu bytes, output report %lu bytes)",
      (unsigned long)g_hid_in_len, (unsigned long)g_hid_out_len);
  g_logged_hid_fail = false;
  start_threads();
  return true;
}
#endif

bool steam_is_running() {
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W entry{}; entry.dwSize = sizeof entry;
  bool found = false;
  for (BOOL more = Process32FirstW(snapshot, &entry); more && !found; more = Process32NextW(snapshot, &entry))
    found = _wcsicmp(entry.szExeFile, L"steam.exe") == 0 || _wcsicmp(entry.szExeFile, L"steam") == 0;
  CloseHandle(snapshot);
  return found;
}

bool open_libusb() {
  if (!direct_adapter_allowed()) return false;
#ifdef GC_ADAPTER_TEST_HOOK
  ++g_test_libusb_opens;
  return false;
#endif
  if (!g_ctx && libusb_init(&g_ctx) != 0) {
    if (!g_logged_missing) { log("gc adapter: libusb could not start; keyboard/XInput stay active"); g_logged_missing = true; }
    g_ctx = nullptr;
    return false;
  }
  libusb_device** list = nullptr;
  const ssize_t count = libusb_get_device_list(g_ctx, &list);
  if (count < 0) return false;
  std::string others;
  libusb_device* adapter = find_adapter(list, count, &others);
  if (!adapter) {
    set_problem("");
    if (!g_logged_missing) {
      log("gc adapter: no WUP-028 adapter found (VID 057E PID 0337); keyboard/XInput stay active");
      if (!others.empty()) log("gc adapter: USB devices present instead: %s", others.c_str());
      g_logged_missing = true;
    }
    libusb_free_device_list(list, 1);
    return false;
  }
  // Read the endpoint layout from the device before the list is freed. An adapter whose interrupt
  // endpoints sit elsewhere still works; one we talk to on the wrong numbers does not.
  g_iface = 0; g_ep_in = 0x81; g_ep_out = 0x02;
  libusb_config_descriptor* cfg = nullptr;
  if (libusb_get_active_config_descriptor(adapter, &cfg) == 0 && cfg) {
    bool found = false;
    for (uint8_t i = 0; i < cfg->bNumInterfaces && !found; ++i) {
      const libusb_interface& itf = cfg->interface[i];
      for (int a = 0; a < itf.num_altsetting && !found; ++a) {
        const libusb_interface_descriptor& alt = itf.altsetting[a];
        uint8_t in = 0, out = 0;
        for (uint8_t e = 0; e < alt.bNumEndpoints; ++e) {
          const libusb_endpoint_descriptor& ep = alt.endpoint[e];
          if ((ep.bmAttributes & 0x03) != LIBUSB_TRANSFER_TYPE_INTERRUPT) continue;
          if (ep.bEndpointAddress & 0x80) { if (!in) in = ep.bEndpointAddress; }
          else if (!out) out = ep.bEndpointAddress;
        }
        if (in && out) { g_iface = alt.bInterfaceNumber; g_ep_in = in; g_ep_out = out; found = true; }
      }
    }
    libusb_free_config_descriptor(cfg);
    if (!found) log("gc adapter: no interrupt endpoint pair in the descriptors; trying the standard interface 0 with 0x81/0x02");
  }
  int rc = libusb_open(adapter, &g_dev);
  libusb_free_device_list(list, 1);
#ifndef _WIN32
  // Linux: no access to the device node means the udev rule that gives the logged-in player the
  // adapter is not installed (or the adapter was plugged in before it was).
  if (rc == LIBUSB_ERROR_ACCESS) {
    g_dev = nullptr;
    set_problem("The game has no permission to read your GameCube adapter. Install the udev rule that comes with the game "
                "(sudo install -m 644 51-melee-unlocked-gc-adapter.rules /etc/udev/rules.d/ && sudo udevadm control --reload-rules), "
                "then unplug the adapter and plug it back in. It is picked up without a restart.");
    if (!g_logged_missing) {
      log("gc adapter: found, but no permission to open it (LIBUSB_ERROR_ACCESS): install 51-melee-unlocked-gc-adapter.rules and replug the adapter");
      g_logged_missing = true;
    }
    return false;
  }
  // The kernel's HID driver binds the adapter; it is detached while the game holds the adapter and
  // given back when the game lets go.
  if (rc == 0) libusb_set_auto_detach_kernel_driver(g_dev, 1);
#endif
  if (rc != 0) {
    g_dev = nullptr;
    // Access denied with a driver in place means another program has the adapter open: Windows gives
    // it to one program at a time. Steam takes GameCube adapters for its own controller support.
    if (rc == LIBUSB_ERROR_ACCESS)
      set_problem(std::string(steam_is_running()
          ? "Your GameCube adapter is in use by Steam, so the game cannot read it. Close Steam, or turn off GameCube controller support in Steam's Controller settings."
          : "Your GameCube adapter is in use by another program, such as Dolphin, so the game cannot read it. Close that program.")
          + kFrame1Note + " It is picked up without a restart.");
    else
#ifdef MELEE_NO_SLIPPI
      set_problem(std::string("A GameCube adapter is plugged in but has no driver the game can use. Install WinUSB on it with Zadig.") + kFrame1Note);
#else
      set_problem(std::string("A GameCube adapter is plugged in but has no driver the game can use. Install WinUSB on it with Zadig, or install Slippi's adapter driver.") + kFrame1Note);
#endif
    if (rc == LIBUSB_ERROR_ACCESS && !g_logged_missing) {
      log("gc adapter: found, but another program has it open (LIBUSB_ERROR_ACCESS).%s Close that program and the adapter is picked up without a restart.",
          steam_is_running() ? " Steam is running and takes GameCube adapters for its own controller support: close Steam, or turn that off in its Controller settings." : "");
      g_logged_missing = true;
    }
    if (!g_logged_missing) {
#ifdef MELEE_NO_SLIPPI
      log("gc adapter: found but cannot open (%s): it may have no usable driver. Install WinUSB, libusbK or libusb-win32 on it with Zadig.", libusb_error_name(rc));
#else
      log("gc adapter: found but cannot open (%s): it may have no usable driver. Install WinUSB, libusbK or libusb-win32 on it with Zadig, as Slippi does.", libusb_error_name(rc));
#endif
      g_logged_missing = true;
    }
    return false;
  }
#ifndef _WIN32
  // The kernel's HID driver is let go first, as Dolphin does: the request below went to an adapter
  // the kernel still held (LIBUSB_ERROR_IO), and claiming detaches it only afterwards.
  if (libusb_kernel_driver_active(g_dev, g_iface) == 1) {
    const int detached = libusb_detach_kernel_driver(g_dev, g_iface);
    g_detached_kernel = detached == 0;
    if (detached != 0) log("gc adapter: could not detach the kernel driver (%s)", libusb_error_name(detached));
  }
#endif
  // The request is optional: Nintendo adapters may reject it while still working normally.
  const int protocol = libusb_control_transfer(g_dev, 0x21, 11, 0x0001, 0, nullptr, 0, 1000);
  if (protocol < 0 && protocol != LIBUSB_ERROR_PIPE)
    log("gc adapter: report protocol request returned %s", libusb_error_name(protocol));
  rc = libusb_claim_interface(g_dev, g_iface);
  if (rc != 0) {
    set_problem(std::string("Your GameCube adapter is in use by another program, such as Dolphin, so the game cannot read it. Close that program.")
                + kFrame1Note + " It is picked up without a restart.");
    if (!g_logged_missing) {
#ifdef MELEE_NO_SLIPPI
      log("gc adapter: cannot claim the adapter (%s): another program (Dolphin?) is using it. Close it and try again.", libusb_error_name(rc));
#else
      log("gc adapter: cannot claim the adapter (%s): another program (Dolphin or Slippi?) is using it. Close it and try again.", libusb_error_name(rc));
#endif
      g_logged_missing = true;
    }
    libusb_close(g_dev); g_dev = nullptr;
    return false;
  }
  g_claimed = true;
  set_problem("");
  // Leave a successfully opened pipe alone. The timeout path recovers a genuinely silent pipe.
  log("gc adapter: opened through libusb (interface %d, endpoints in 0x%02X out 0x%02X)", g_iface, g_ep_in, g_ep_out);
  start_threads();
  return true;
}

// libusb first, so an adapter that works today is opened exactly as before.
bool open_adapter() {
  if (!direct_adapter_allowed()) return false;
  return open_libusb() || open_hid();
}

// Finding and opening the adapter runs here, never on the game's thread. Listing the USB devices
// takes tens of milliseconds, and an adapter that is present but cannot be opened (no WinUSB
// driver) was retried every two seconds from the pad read: a hitch in every other second of play.
std::thread g_scanner;
std::atomic<bool> g_scanner_run{false};
std::mutex g_scanner_mutex;
std::condition_variable g_scanner_wake;
std::once_flag g_scanner_started;

void scanner_thread();

void ensure_scanner_started() {
  if (!direct_adapter_allowed()) return;
  // (direct_adapter_allowed covers --no-gc-adapter: every hidden test run never opens the adapter,
  // whoever asks; the Controls tab's status line used to start the scanner and take a real adapter.)
  std::call_once(g_scanner_started, [] {
    g_scanner_run.store(true);
#ifdef GC_ADAPTER_TEST_HOOK
    ++g_test_scanner_starts;  // Exercise lazy startup without ever scanning real hardware.
#else
    g_scanner = std::thread(scanner_thread);
#endif
  });
}

void scanner_thread() {
  while (g_scanner_run.load() && direct_adapter_allowed()) {
    if (g_reset_requested.exchange(false)) {
      log("gc adapter: resetting the connection and controller origins");
      close_adapter(true);
      g_logged_missing = false;
      g_logged_hid_fail = false;
      open_adapter();
      g_resetting.store(false);
    } else if (!adapter_open() || !g_running.load()) {
      if (adapter_open() && !g_running.load()) close_adapter();
      open_adapter();
    }
    std::unique_lock<std::mutex> lock(g_scanner_mutex);
    g_scanner_wake.wait_for(lock, std::chrono::seconds(2),
                           [] { return !g_scanner_run.load() || g_reset_requested.load(); });
  }
}

}  // namespace

double gcadapter_poll_rate_hz() {
  if (options.no_gc_adapter) return 0.0;
  if (yougame::stdio_bridge()) {
    std::lock_guard<std::mutex> lock(g_mutex);
    attach_mirror();
    uint8_t rep[37];
    if (!mirror_live(rep)) return 0.0;
  }
  return g_poll_rate_hz.load(std::memory_order_relaxed);
}

std::string gcadapter_problem() {
  if (yougame::stdio_bridge()) {
    std::lock_guard<std::mutex> lock(g_mutex);
    attach_mirror();
    uint8_t rep[37];
    return mirror_live(rep) ? "Controllers come from the YouGame app" : "No GameCube adapter from the YouGame app";
  }
  ensure_scanner_started();
  std::lock_guard<std::mutex> lock(g_problem_mutex);
  return g_problem;
}

bool gcadapter_reset_pending() {
  return g_resetting.load();
}

bool gcadapter_request_reset() {
  if (!direct_adapter_allowed()) return false;
  ensure_scanner_started();
  std::lock_guard<std::mutex> scanner_lock(g_scanner_mutex);
  if (!g_scanner_run.load() || g_resetting.exchange(true)) return false;
  {
    std::lock_guard<std::mutex> report_lock(g_mutex);
    g_have_report = false;
    for (auto& origin : g_origin) origin.set = false;
  }
  g_poll_rate_hz.store(0.0, std::memory_order_relaxed);
  g_reset_requested.store(true);
  g_scanner_wake.notify_one();
  return true;
}

// Fills ports that have a controller plugged into the adapter; returns the mask of those ports.
uint32_t gcadapter_poll(PadState out[4]) {
  if (options.no_gc_adapter || g_resetting.load()) return 0;
  uint8_t rep[37];
  // Held for the whole report: the scanner thread resets the origins under it when the adapter closes.
  std::unique_lock<std::mutex> lk(g_mutex, std::defer_lock);
  if (yougame::stdio_bridge()) {
    // YouGame: the app's adapter through the pad mirror, never our own USB.
    lk.lock();
    attach_mirror();
#ifdef _WIN32
    if (!g_mirror || !read_mirror(rep)) return 0;
#else
    if (!read_mirror(rep)) return 0;
#endif
  } else {
    ensure_scanner_started();
    auto now = std::chrono::steady_clock::now();
    if (!g_running.load()) return 0;
    lk.lock();
    if (g_resetting.load() || !g_have_report) return 0;
    std::memcpy(rep, g_report, 37);
    TickTiming& tick = tick_timing();
    if (tick.pad_age < 0) tick.pad_age = std::chrono::duration<double, std::milli>(now - g_report_time).count();
  }
  uint32_t mask = 0;
  for (int port = 0; port < 4; ++port) {
    const uint8_t* c = rep + 1 + port * 9;
    uint8_t status = c[0] & 0x30;
    if (!status) { g_origin[port].set = false; continue; }
    Origin& o = g_origin[port];
    if (!o.set) {
      o.set = true; o.sx = c[3]; o.sy = c[4]; o.cx = c[5]; o.cy = c[6]; o.tl = c[7]; o.tr = c[8];
      // The neutral point is taken from this first report; a stick held while the controller
      // connects shifts every later reading, so record it for input reports.
      log("gc adapter: port %d connected, neutral stick %u,%u c-stick %u,%u triggers %u,%u", port + 1, o.sx, o.sy, o.cx, o.cy, o.tl, o.tr);
    }
    PadState& p = out[port];
    std::memset(&p, 0, sizeof p);
    p.err = 0;
    uint16_t b = 0;
    if (c[1] & 0x01) b |= PAD_A; if (c[1] & 0x02) b |= PAD_B; if (c[1] & 0x04) b |= PAD_X; if (c[1] & 0x08) b |= PAD_Y;
    if (c[1] & 0x10) b |= PAD_LEFT; if (c[1] & 0x20) b |= PAD_RIGHT; if (c[1] & 0x40) b |= PAD_DOWN; if (c[1] & 0x80) b |= PAD_UP;
    if (c[2] & 0x01) b |= PAD_START; if (c[2] & 0x02) b |= PAD_Z; if (c[2] & 0x04) b |= PAD_R; if (c[2] & 0x08) b |= PAD_L;
    p.button = b;
    auto axis = [](uint8_t v, uint8_t origin) { int a = (int)v - (int)origin; return (int8_t)(a > 127 ? 127 : a < -128 ? -128 : a); };
    p.stick_x = axis(c[3], o.sx); p.stick_y = axis(c[4], o.sy);
    p.sub_x = axis(c[5], o.cx); p.sub_y = axis(c[6], o.cy);
    p.trig_l = (uint8_t)(c[7] > o.tl ? c[7] - o.tl : 0);
    p.trig_r = (uint8_t)(c[8] > o.tr ? c[8] - o.tr : 0);
    if (g_phob_live[port] && g_phob[port].s.enabled) {
      const phob::Output& q = g_phob_out[port];
      p.button = q.buttons;
      p.stick_x = q.stick_x; p.stick_y = q.stick_y; p.sub_x = q.sub_x; p.sub_y = q.sub_y;
      p.trig_l = q.trig_l; p.trig_r = q.trig_r;
    }
    mask |= 1u << port;
  }
  return mask;
}

// PADRecalibrate on hardware re-reads the controller's neutral position. Our neutral is taken from
// the first report after a controller connects, so a stick that was deflected at that moment shifts
// every later reading, which makes precise inputs like UCF shield drops work only sometimes.
// Dropping the stored origin makes the next report re-establish it.
void gcadapter_recalibrate(int port) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (port < 0 || port > 3) { for (auto& o : g_origin) o.set = false; return; }
  g_origin[port].set = false;
}

void gcadapter_rumble(int port, bool on) {
  if (options.no_gc_adapter || port < 0 || port > 3) return;
  if (g_resetting.load()) on = false;
  uint8_t v = on ? 1 : 0;
  if (yougame::stdio_bridge()) {
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_mirror && g_mirror->rumble[port] != v) { g_mirror->rumble[port] = v; InterlockedIncrement(&g_mirror->rumble_seq); }
#else
    static std::once_flag said;
    std::call_once(said, [] { log("gc adapter: rumble unavailable through the YouGame app on this system"); });
#endif
    return;
  }
  if (g_rumble[port] != v) { g_rumble[port] = v; g_rumble_dirty = true; g_rumble_wake.notify_one(); }
}

// ---- test runs: a scripted controller on socket 1 ----
// MELEE_TEST_ADAPTER_SCRIPT=<file>, honoured only when the real adapter is off (--no-gc-adapter, as
// every hidden test run is). One line per step: `<ms> <ax> <ay> <cx> <cy> <la> <ra> [buttons...]`,
// raw adapter bytes (128 is centre), buttons A B X Y Z L R S DU DD DL DR. The steps play once at
// 1 kHz through the same report path as a real adapter (phob_process); the last one then holds.
struct ScriptStep { int ms; uint8_t ax, ay, cx, cy, la, ra; uint16_t buttons; };
std::thread g_test_thread;
std::atomic<bool> g_test_run{false};

void test_script_thread(std::vector<ScriptStep> steps) {
  // 1 ms sleeps, so a scripted 9 ms tilt lasts 9 ms (the default timer slice is about 15 ms).
#ifdef _WIN32
  timeBeginPeriod(1);
#endif
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  uint8_t rep[37] = {0x21};
  auto fill = [&](const ScriptStep& s) {
    uint8_t* c = rep + 1;
    c[0] = 0x10;
    const uint16_t b = s.buttons;
    c[1] = (uint8_t)((b & PAD_A ? 0x01 : 0) | (b & PAD_B ? 0x02 : 0) | (b & PAD_X ? 0x04 : 0) | (b & PAD_Y ? 0x08 : 0) |
                     (b & PAD_LEFT ? 0x10 : 0) | (b & PAD_RIGHT ? 0x20 : 0) | (b & PAD_DOWN ? 0x40 : 0) | (b & PAD_UP ? 0x80 : 0));
    c[2] = (uint8_t)((b & PAD_START ? 0x01 : 0) | (b & PAD_Z ? 0x02 : 0) | (b & PAD_R ? 0x04 : 0) | (b & PAD_L ? 0x08 : 0));
    c[3] = s.ax; c[4] = s.ay; c[5] = s.cx; c[6] = s.cy; c[7] = s.la; c[8] = s.ra;
  };
  auto next = std::chrono::steady_clock::now();
  size_t i = 0;
  int left = steps.empty() ? 0 : steps[0].ms;
  while (g_test_run.load()) {
    if (!steps.empty()) fill(steps[i]);
    next += std::chrono::milliseconds(1);
    std::this_thread::sleep_until(next);
    const auto now = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> lk(g_mutex);
      if (!g_origin[0].set) { g_origin[0].set = true; g_origin[0].sx = g_origin[0].sy = g_origin[0].cx = g_origin[0].cy = 128; }
      std::memcpy(g_report, rep, 37);
      g_report_time = now;
      g_have_report = true;
      phob_process(rep, now);
    }
    g_poll_rate_hz.store(1000.0, std::memory_order_relaxed);
    static int ticks = 0; static auto tick_start = now;
    if (++ticks == 5000) { log("gc adapter: test script %.0f reports/s", 5000.0 / std::chrono::duration<double>(now - tick_start).count()); ticks = 0; tick_start = now; }
    // Steps advance by the time that really passed (a sleep can last longer than 1 ms).
    static auto last = now;
    int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
    if (elapsed > 0) last += std::chrono::milliseconds(elapsed);
    while (!steps.empty() && elapsed > 0) {
      const int used = std::min(elapsed, left);
      left -= used; elapsed -= used;
      if (left <= 0) { if (i + 1 >= steps.size()) break; left = steps[++i].ms; fill(steps[i]); if (i % 20 == 0) log("gc adapter: test script step %zu", i); }
    }
    if (std::chrono::steady_clock::now() - next > std::chrono::milliseconds(50)) next = std::chrono::steady_clock::now();
  }
}

void gcadapter_test_script_start() {
  const char* path = host::test_env::getenv("MELEE_TEST_ADAPTER_SCRIPT");
  if (!path || !options.no_gc_adapter || g_test_thread.joinable()) return;
  std::ifstream in(path);
  std::vector<ScriptStep> steps;
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    int ms, v[6];
    if (!(ls >> ms >> v[0] >> v[1] >> v[2] >> v[3] >> v[4] >> v[5])) continue;
    ScriptStep s{ms, (uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3], (uint8_t)v[4], (uint8_t)v[5], 0};
    std::string b;
    while (ls >> b) {
      if (b == "A") s.buttons |= PAD_A; else if (b == "B") s.buttons |= PAD_B; else if (b == "X") s.buttons |= PAD_X;
      else if (b == "Y") s.buttons |= PAD_Y; else if (b == "Z") s.buttons |= PAD_Z; else if (b == "L") s.buttons |= PAD_L;
      else if (b == "R") s.buttons |= PAD_R; else if (b == "S") s.buttons |= PAD_START; else if (b == "DU") s.buttons |= PAD_UP;
      else if (b == "DD") s.buttons |= PAD_DOWN; else if (b == "DL") s.buttons |= PAD_LEFT; else if (b == "DR") s.buttons |= PAD_RIGHT;
    }
    steps.push_back(s);
  }
  log("gc adapter: test script %s, %zu steps", path, steps.size());
  g_test_run.store(true);
  g_test_thread = std::thread(test_script_thread, std::move(steps));
}

void gcadapter_shutdown() {
  if (g_test_thread.joinable()) { g_test_run.store(false); g_test_thread.join();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
  }
  if (g_scanner.joinable()) {
    { std::lock_guard<std::mutex> lock(g_scanner_mutex); g_scanner_run.store(false); }
    g_scanner_wake.notify_all();
    g_scanner.join();
  }
  close_adapter();
  g_reset_requested.store(false);
  g_resetting.store(false);
  if (g_ctx) { libusb_exit(g_ctx); g_ctx = nullptr; }
}

bool steam_running() { return steam_is_running(); }

void gcadapter_phob_set(int port, const phob::Settings& settings) {
  if (port < 0 || port > 3) return;
  phob::Prepared prepared;
  phob::prepare(settings, prepared);   // the calibration fit runs outside the lock
  std::lock_guard<std::mutex> lk(g_mutex);
  g_phob[port] = prepared;
  g_phob_state[port].primed = false;
}

phob::Settings gcadapter_phob_get(int port) {
  if (port < 0 || port > 3) return {};
  std::lock_guard<std::mutex> lk(g_mutex);
  return g_phob[port].s;
}

bool gcadapter_phob_raw(int port, phob::HardwareInput& in) {
  if (port < 0 || port > 3) return false;
  std::lock_guard<std::mutex> lk(g_mutex);
  if (!g_have_report || !g_origin[port].set) return false;
  const uint8_t* c = g_report + 1 + port * 9;
  if (!(c[0] & 0x30)) return false;
  in = phob_input(c, g_origin[port]);
  return true;
}

int gcadapter_scope(int port, GCScopeSample* out, int max) {
  if (port < 0 || port > 3 || max <= 0) return 0;
  std::lock_guard<std::mutex> lk(g_mutex);
  const uint32_t head = g_scope_head[port];
  const int n = (int)std::min<uint32_t>((uint32_t)std::min(max, kScopeLen), head);
  for (int i = 0; i < n; ++i) out[i] = g_scope[port][(head - (uint32_t)n + (uint32_t)i) % kScopeLen];
  return n;
}

}  // namespace host
#endif
