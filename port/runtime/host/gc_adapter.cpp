// Official / Mayflash / Hand Held Legend "GameCube Controller Adapter for Wii U", through libusb as
// Dolphin uses it. Same protocol as Dolphin's GCAdapter: one 0x13 byte starts the 37-byte report
// stream on endpoint 0x81 (status + 9 bytes per port), 0x11 + 4 bytes sets rumble.
//
// This used to talk to WinUSB directly, which meant an adapter installed with libusbK or
// libusb-win32 was invisible here while Dolphin reported it detected at 1 kHz, and players were told
// to replace a driver that already worked for them. libusb's Windows backend speaks all three.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.h"
#define NOMINMAX
#include <windows.h>
#include <libusb.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace host {
namespace {

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
std::thread g_thread;
std::atomic<bool> g_running{false};
std::mutex g_mutex;
uint8_t g_report[37] = {};
bool g_have_report = false;
std::chrono::steady_clock::time_point g_next_scan;
bool g_logged_missing = false;
bool g_logged_restart = false;   // logged once per open when the adapter is silent and we retry
struct Origin { bool set = false; uint8_t sx = 128, sy = 128, cx = 128, cy = 128, tl = 0, tr = 0; } g_origin[4];
std::atomic<uint8_t> g_rumble[4]{};
std::atomic<bool> g_rumble_dirty{false};

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

void reader_thread() {
  auto send_start = [&] {
    uint8_t start = 0x13;
    int wrote = 0;
    const int rc = libusb_interrupt_transfer(g_dev, g_ep_out, &start, 1, &wrote, 100);
    if (rc != 0) log("gc adapter: start command failed (%s)", libusb_error_name(rc));
  };
  send_start();
  int failures = 0, silent = 0;
  while (g_running.load()) {
    uint8_t buf[37];
    int got = 0;
    const int rc = libusb_interrupt_transfer(g_dev, g_ep_in, buf, (int)sizeof buf, &got, 100);
    if (rc == 0) {
      failures = 0; silent = 0;
      if (got == 37 && buf[0] == 0x21) { std::lock_guard<std::mutex> lk(g_mutex); std::memcpy(g_report, buf, 37); g_have_report = true; }
    } else {
      if (rc == LIBUSB_ERROR_TIMEOUT) {
        // A timeout used to loop forever without counting, so an adapter that was connected but not
        // streaming was never retried and never reported: it stayed dead until it was physically
        // unplugged, which is what made replugging "fix" it. An adapter left mid-stream by a crash
        // does exactly this. Reset the read pipe and ask it to start again about once a second.
        if (++silent >= 10) {
          silent = 0;
          libusb_clear_halt(g_dev, g_ep_in);
          send_start();
          if (!g_logged_restart) { log("gc adapter: no reports yet, clearing the pipe and re-sending start"); g_logged_restart = true; }
        }
        continue;
      }
      if (++failures > 20) { log("gc adapter: read failed (%s), adapter disconnected", libusb_error_name(rc)); break; }
    }
    if (g_rumble_dirty.exchange(false)) {
      uint8_t cmd[5] = {0x11, g_rumble[0], g_rumble[1], g_rumble[2], g_rumble[3]};
      int wrote = 0;
      libusb_interrupt_transfer(g_dev, g_ep_out, cmd, (int)sizeof cmd, &wrote, 100);
    }
  }
  g_running.store(false);
}

void close_adapter() {
  g_running.store(false);
  if (g_thread.joinable()) g_thread.join();
  if (g_dev) {
    if (g_claimed) libusb_release_interface(g_dev, g_iface);
    libusb_close(g_dev);
    g_dev = nullptr;
  }
  g_claimed = false;
  std::lock_guard<std::mutex> lk(g_mutex);
  g_have_report = false;
  for (auto& o : g_origin) o.set = false;
}

bool open_adapter() {
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
  if (rc != 0) {
    g_dev = nullptr;
    if (!g_logged_missing) {
      log("gc adapter: found but cannot open (%s): it may have no usable driver. Install WinUSB, libusbK or libusb-win32 on it with Zadig, as Slippi does.", libusb_error_name(rc));
      g_logged_missing = true;
    }
    return false;
  }
  rc = libusb_claim_interface(g_dev, g_iface);
  if (rc != 0) {
    if (!g_logged_missing) {
      log("gc adapter: cannot claim the adapter (%s): another program (Dolphin or Slippi?) is using it. Close it and try again.", libusb_error_name(rc));
      g_logged_missing = true;
    }
    libusb_close(g_dev); g_dev = nullptr;
    return false;
  }
  g_claimed = true;
  // A run that exited without closing the adapter (a crash) leaves it mid-stream: the next open
  // succeeds but the read pipe delivers nothing, so the adapter looks absent until it is physically
  // unplugged. Clearing both pipes does by software what a replug was doing by hand.
  libusb_clear_halt(g_dev, g_ep_in);
  libusb_clear_halt(g_dev, g_ep_out);
  log("gc adapter: opened through libusb (interface %d, endpoints in 0x%02X out 0x%02X)", g_iface, g_ep_in, g_ep_out);
  g_logged_missing = false;
  g_logged_restart = false;
  g_running.store(true);
  g_thread = std::thread(reader_thread);
  return true;
}

}  // namespace

// Fills ports that have a controller plugged into the adapter; returns the mask of those ports.
uint32_t gcadapter_poll(PadState out[4]) {
  if (options.no_gc_adapter) return 0;
  auto now = std::chrono::steady_clock::now();
  if (!g_dev || !g_running.load()) {
    if (g_dev && !g_running.load()) close_adapter();
    if (now < g_next_scan) return 0;
    g_next_scan = now + std::chrono::seconds(2);
    if (!open_adapter()) return 0;
  }
  uint8_t rep[37];
  {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_have_report) return 0;
    std::memcpy(rep, g_report, 37);
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
  if (port < 0 || port > 3) return;
  uint8_t v = on ? 1 : 0;
  if (g_rumble[port] != v) { g_rumble[port] = v; g_rumble_dirty = true; }
}

void gcadapter_shutdown() {
  close_adapter();
  if (g_ctx) { libusb_exit(g_ctx); g_ctx = nullptr; }
}

}  // namespace host
