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

// `others`, when given, collects the VID/PID of every USB device that was not a match. Third-party
// adapters (the Hand Held Legend GC Pocket+ and similar) use their own USB identity rather than
// Nintendo's, so "no adapter found" told a user nothing about what they actually had plugged in.
// Listing what was there makes the log enough to add support without asking them to run commands.
std::string find_adapter_path(std::string* others = nullptr) {
  HDEVINFO devs = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_USB_DEVICE, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (devs == INVALID_HANDLE_VALUE) return "";
  std::string found;
  int listed = 0;
  SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof iface;
  for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devs, nullptr, &GUID_DEVINTERFACE_USB_DEVICE, i, &iface); ++i) {
    DWORD needed = 0;
    SetupDiGetDeviceInterfaceDetailA(devs, &iface, nullptr, 0, &needed, nullptr);
    if (!needed) continue;
    std::string buf(needed, '\0');
    auto* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)buf.data();
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
    if (!SetupDiGetDeviceInterfaceDetailA(devs, &iface, detail, needed, nullptr, nullptr)) continue;
    std::string path(detail->DevicePath);
    std::string lower(path);
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    if (lower.find("vid_057e&pid_0337") != std::string::npos) { found = path; break; }
    if (others && listed < 20) {
      const size_t vid = lower.find("vid_");
      // "vid_xxxx&pid_xxxx" is 17 characters; anything shorter is not a VID/PID pair.
      if (vid != std::string::npos && lower.size() >= vid + 17) {
        if (!others->empty()) *others += ", ";
        *others += lower.substr(vid, 17);
        ++listed;
      }
    }
  }
  SetupDiDestroyDeviceInfoList(devs);
  return found;
}

// True when a WUP-028 is plugged in but not reachable through WinUSB. Dolphin talks to adapters with
// libusb, which also drives libusbK and libusb-win32, and libusbK is a common Zadig choice for Melee.
// Those register a different device interface, so the WinUSB enumeration above finds nothing while
// Dolphin reports the adapter detected: saying "no adapter found" then sends people hunting for
// hardware faults instead of changing the driver.
bool adapter_present_on_another_driver() {
  HDEVINFO devs = SetupDiGetClassDevsA(nullptr, "USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (devs == INVALID_HANDLE_VALUE) return false;
  SP_DEVINFO_DATA info{}; info.cbSize = sizeof info;
  bool present = false;
  for (DWORD i = 0; !present && SetupDiEnumDeviceInfo(devs, i, &info); ++i) {
    char id[512]{};
    if (!SetupDiGetDeviceInstanceIdA(devs, &info, id, sizeof id, nullptr)) continue;
    std::string lower(id);
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    if (lower.find("vid_057e&pid_0337") != std::string::npos) present = true;
  }
  SetupDiDestroyDeviceInfoList(devs);
  return present;
}

void reader_thread() {
  auto send_start = [&] {
    uint8_t start = 0x13;
    int wrote = 0;
    const int rc = libusb_interrupt_transfer(g_dev, 0x02, &start, 1, &wrote, 100);
    if (rc != 0) log("gc adapter: start command failed (%s)", libusb_error_name(rc));
  };
  send_start();
  int failures = 0, silent = 0;
  while (g_running.load()) {
    uint8_t buf[37];
    int got = 0;
    const int rc = libusb_interrupt_transfer(g_dev, 0x81, buf, (int)sizeof buf, &got, 100);
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
          libusb_clear_halt(g_dev, 0x81);
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
      libusb_interrupt_transfer(g_dev, 0x02, cmd, (int)sizeof cmd, &wrote, 100);
    }
  }
  g_running.store(false);
}

void close_adapter() {
  g_running.store(false);
  if (g_thread.joinable()) g_thread.join();
  if (g_usb) { WinUsb_Free(g_usb); g_usb = nullptr; }
  if (g_file != INVALID_HANDLE_VALUE) { CloseHandle(g_file); g_file = INVALID_HANDLE_VALUE; }
  std::lock_guard<std::mutex> lk(g_mutex);
  g_have_report = false;
  for (auto& o : g_origin) o.set = false;
}

bool open_adapter() {
  std::string others;
  std::string path = find_adapter_path(&others);
  if (path.empty()) {
    if (!g_logged_missing) {
      if (adapter_present_on_another_driver()) {
        log("gc adapter: a WUP-028 adapter (VID 057E PID 0337) is plugged in but is not on the WinUSB driver.");
        log("gc adapter: run Zadig, select the adapter, choose WinUSB and click Replace Driver. Dolphin also accepts libusbK, which this port does not read yet.");
      } else {
        log("gc adapter: no WUP-028 adapter found (VID 057E PID 0337 with the WinUSB driver); keyboard/XInput stay active");
        if (!others.empty()) log("gc adapter: USB devices present instead: %s", others.c_str());
      }
      g_logged_missing = true;
    }
    return false;
  }
  g_file = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
  if (g_file == INVALID_HANDLE_VALUE) {
    if (!g_logged_missing) { log("gc adapter: found but cannot open (%lu): another program (Dolphin?) may hold it, or the driver is not WinUSB", GetLastError()); g_logged_missing = true; }
    return false;
  }
  if (!WinUsb_Initialize(g_file, &g_usb)) {
    if (!g_logged_missing) { log("gc adapter: WinUsb_Initialize failed (%lu): install the WinUSB driver with Zadig as for Slippi", GetLastError()); g_logged_missing = true; }
    CloseHandle(g_file); g_file = INVALID_HANDLE_VALUE;
    return false;
  }
  // A run that exited without closing the adapter (a crash) leaves it mid-stream: the next open
  // succeeds but the read pipe delivers nothing, so the adapter looks absent until it is physically
  // unplugged. Resetting both pipes clears that state, which is what a replug was doing by hand.
  WinUsb_ResetPipe(g_usb, 0x81);
  WinUsb_ResetPipe(g_usb, 0x02);
  log("gc adapter: opened %s", path.c_str());
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
  if (!g_usb || !g_running.load()) {
    if (g_usb && !g_running.load()) close_adapter();
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

void gcadapter_rumble(int port, bool on) {
  if (port < 0 || port > 3) return;
  uint8_t v = on ? 1 : 0;
  if (g_rumble[port] != v) { g_rumble[port] = v; g_rumble_dirty = true; }
}

void gcadapter_shutdown() { close_adapter(); }

}  // namespace host
