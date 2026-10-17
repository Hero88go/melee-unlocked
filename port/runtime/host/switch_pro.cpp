// Nintendo Switch Pro Controller (and the Joy-Cons / SNES Online pad, which speak the same
// protocol) over USB and Bluetooth HID.
//
// Raw Input can only read, and over USB a Pro Controller says nothing at all until it has been told
// to talk: the 0x80 0x02 handshake, 0x80 0x04 so the USB timeout does not mute it again a moment
// later, then subcommand 0x03 with argument 0x30 to turn on "standard full" 0x30 input reports.
// That is why this cannot be a VID/PID line in window.cpp's Raw Input path: a controller that has
// not been initialised never sends a report, so waiting for WM_INPUT to discover it waits for ever.
// Instead the Raw Input device list is enumerated directly, the device is opened a second time with
// CreateFile for output, and the handshake runs on a worker thread. Reads still arrive as WM_INPUT
// and land in switchpro_raw_input().
//
// Over Bluetooth the controller streams coarse 0x3F reports with no handshake at all, so those are
// decoded too, and the same 0x03 0x30 subcommand moves it to full reports there as well.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.h"
#define NOMINMAX
#include <windows.h>
#include <hidsdi.h>
#include "input_bindings.h"
#include <libusb.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "hid.lib")

namespace host {
namespace {

constexpr int kSlots = 4;
constexpr uint16_t kNintendoVid = 0x057E;

// A Pro Controller stick reads 0..4095 with an as-built centre near 2048 and full deflection about
// 1300 counts away from it. The exact per-unit figures live in SPI flash, which we deliberately do
// not stop to read: 1150 is short enough that every unit reaches the edge of the gate at full tilt,
// and the small dead band covers the jitter left over after the neutral has been taken from the
// controller's first report the way the GameCube adapter takes its origin.
constexpr int kStickRange = 1150;
constexpr int kStickDead = 60;

constexpr int kMaxInitAttempts = 6;

// The GameCube controller for Nintendo Switch Online (the Switch 2 one). It does not speak the Pro
// Controller's protocol: see gamecube_start and decode_gamecube.
constexpr uint32_t kGameCubePid = 0x2073;
// The Switch 2 Pro Controller: the same start-up as the GameCube controller above and the same
// report shape, with its own buttons and no analog triggers (see decode_pro2).
constexpr uint32_t kPro2Pid = 0x2069;
// The Switch 2 controllers, which share that protocol.
bool switch2_pid(uint32_t pid) { return pid == kGameCubePid || pid == kPro2Pid; }

bool supported_pid(uint32_t pid) {
  return pid == 0x2009      // Pro Controller (the reported case)
      || pid == 0x2017      // SNES controller for Nintendo Switch Online
      || pid == 0x2006      // Joy-Con (L)
      || pid == 0x2007      // Joy-Con (R)
      || switch2_pid(pid);
}

const char* device_name(uint32_t pid) {
  switch (pid) {
    case 0x2009: return "Pro Controller";
    case 0x2017: return "SNES controller";
    case 0x2006: return "Joy-Con (L)";
    case 0x2007: return "Joy-Con (R)";
    case kGameCubePid: return "GameCube controller (Switch Online)";
    case kPro2Pid: return "Switch 2 Pro Controller";
    default: return "controller";
  }
}

using Clock = std::chrono::steady_clock;

struct Device {
  HANDLE raw = nullptr;                  // Raw Input handle: what a WM_INPUT report is tagged with
  HANDLE file = INVALID_HANDLE_VALUE;    // second, writable handle, for output reports
  std::wstring path;
  uint32_t pid = 0;
  bool bluetooth = false;
  USHORT out_len = 64;                   // exactly how many bytes an output report write must be
  uint8_t counter = 0;                   // rolling packet counter carried by report 0x01
  int slot = 0;
  int attempts = 0;
  bool streaming = false;                // at least one report of any kind has been decoded
  bool full = false;                     // reports are the 0x30 "standard full" kind
  bool origin = false;                   // stick neutrals have been taken
  bool failed = false;                   // gave up initialising; logged once
  int nx = 2048, ny = 2048, cx = 2048, cy = 2048;
  Clock::time_point next_try;
  Clock::time_point last_report;
  PadState pad{};
  uint16_t buttons = 0;
};

Device g_dev[kSlots];
std::mutex g_mutex;
// Why a Switch Online GameCube controller on USB is not streaming, for the Controls tab: Steam takes
// these controllers for its own support, and the game then sees only Steam's Xbox copy of it with
// Steam's button layout ("the default binds are all wrong", fixed by a restart in two reports).
std::mutex g_problem_mutex;
std::string g_problem;
void set_problem(const std::string& text) { std::lock_guard<std::mutex> lock(g_problem_mutex); g_problem = text; }
std::thread g_thread;
std::atomic<bool> g_running{false};
// Every HID report that is not a DualShock's reaches switchpro_raw_input, including the ones an
// Xbox pad or a DualSense sends. This lets that path return without taking the lock at all while no
// Nintendo controller is plugged in, which is the usual case.
std::atomic<bool> g_any_device{false};
std::once_flag g_start_once;
bool g_logged_missing = false;

// ---- output reports ----------------------------------------------------------------------------

// WriteFile puts the report on the interrupt OUT endpoint, which is where the controller listens;
// HidD_SetOutputReport goes over the control channel and is only a fallback for stacks that refuse
// the write. Windows wants exactly OutputReportByteLength bytes either way, so every packet is
// zero padded up to it.
bool send_report(HANDLE file, USHORT out_len, const uint8_t* data, size_t len) {
  if (file == INVALID_HANDLE_VALUE || len > out_len) return false;
  std::vector<uint8_t> buf(out_len, 0);
  std::memcpy(buf.data(), data, len);
  DWORD wrote = 0;
  if (WriteFile(file, buf.data(), (DWORD)buf.size(), &wrote, nullptr)) return true;
  return HidD_SetOutputReport(file, buf.data(), (ULONG)buf.size()) != FALSE;
}

// 0x80 commands exist only on USB. Bluetooth has no handshake and rejects the report id, which is
// expected and not worth a line in the log.
bool send_usb_command(const Device& d, uint8_t command) {
  const uint8_t packet[2] = {0x80, command};
  return send_report(d.file, d.out_len, packet, sizeof packet);
}

// Report 0x01 carries a subcommand behind a rolling counter and one neutral rumble frame.
bool send_subcommand(Device& d, uint8_t id, uint8_t argument) {
  const uint8_t packet[12] = {0x01, (uint8_t)(d.counter++ & 0x0F),
                              0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40,
                              id, argument};
  return send_report(d.file, d.out_len, packet, sizeof packet);
}

// ---- discovery ---------------------------------------------------------------------------------

bool open_device(Device& d, HANDLE raw, uint32_t pid, int slot) {
  UINT chars = 0;
  if (GetRawInputDeviceInfoW(raw, RIDI_DEVICENAME, nullptr, &chars) == (UINT)-1 || !chars) return false;
  std::wstring path(chars + 1, L'\0');
  if (GetRawInputDeviceInfoW(raw, RIDI_DEVICENAME, &path[0], &chars) == (UINT)-1) return false;
  path.resize(std::wcslen(path.c_str()));

  // A Bluetooth HID interface path carries the Bluetooth HID service GUID 00001124 (or comes from
  // BTHENUM); a USB one names the USB vendor and product directly. Only USB needs, and only USB
  // accepts, the 0x80 handshake.
  std::wstring lower = path;
  for (auto& c : lower) c = (wchar_t)towlower(c);
  const bool bluetooth = lower.find(L"00001124") != std::wstring::npos ||
                         lower.find(L"bthenum") != std::wstring::npos;

  // Shared read/write: nothing here should take the controller away from anything else, and the
  // reads still come through Raw Input.
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       nullptr, OPEN_EXISTING, 0, nullptr);
  // The GameCube controller takes no output reports on this interface (its commands go to a second
  // USB interface), so a handle with no access rights is enough to keep the slot.
  if (file == INVALID_HANDLE_VALUE && switch2_pid(pid))
    file = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    log("switch pro: found a %s (VID 057E PID %04X) but cannot open it for output (error %lu): "
        "Steam Input, BetterJoy or another Switch driver may be holding it. Close it, or turn off "
        "Steam's Nintendo controller support, and replug the pad.",
        device_name(pid), pid, (unsigned long)GetLastError());
    return false;
  }

  USHORT out_len = bluetooth ? 49 : 64;
  PHIDP_PREPARSED_DATA preparsed = nullptr;
  if (HidD_GetPreparsedData(file, &preparsed) && preparsed) {
    HIDP_CAPS caps{};
    if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS && caps.OutputReportByteLength)
      out_len = caps.OutputReportByteLength;
    HidD_FreePreparsedData(preparsed);
  }

  d = Device{};
  d.raw = raw; d.file = file; d.path = path; d.pid = pid; d.bluetooth = bluetooth;
  d.out_len = out_len; d.slot = slot; d.next_try = Clock::now();
  log("switch pro: %s detected on %s as slot %d (PID %04X, output reports %u bytes)",
      device_name(pid), bluetooth ? "Bluetooth" : "USB", slot + 1, pid, (unsigned)out_len);
  return true;
}

// `restore` puts the USB timeout back on, which is how a driver should hand the controller back:
// it goes quiet again and a Switch (or the next run of the game) takes it over without a replug.
// Only worth doing on the way out, since it is a blocking write and a controller that was unplugged
// cannot hear it anyway.
void close_device(Device& d, const char* why, bool restore) {
  if (d.file != INVALID_HANDLE_VALUE) {
    if (restore && !d.bluetooth) send_usb_command(d, 0x05);
    CloseHandle(d.file);
  }
  if (d.raw && why) log("switch pro: slot %d %s", d.slot + 1, why);
  const int slot = d.slot;
  d = Device{};
  d.slot = slot;
}

// Walks the Raw Input device list for Nintendo controllers. New ones take a free slot; ones that
// have gone away free theirs. Runs on the worker thread only.
void scan() {
  UINT count = 0;
  if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1) return;
  std::vector<RAWINPUTDEVICELIST> list(count ? count : 1);
  count = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
  if (count == (UINT)-1) return;

  bool present[kSlots] = {};
  int found = 0;
  for (UINT i = 0; i < count; ++i) {
    if (list[i].dwType != RIM_TYPEHID) continue;
    RID_DEVICE_INFO info{}; info.cbSize = sizeof info;
    UINT size = sizeof info;
    if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) == (UINT)-1) continue;
    if (info.dwType != RIM_TYPEHID) continue;
    if (info.hid.dwVendorId != kNintendoVid || !supported_pid(info.hid.dwProductId)) continue;
    ++found;

    std::lock_guard<std::mutex> lock(g_mutex);
    int slot = -1;
    for (int s = 0; s < kSlots; ++s) if (g_dev[s].raw == list[i].hDevice) { slot = s; break; }
    if (slot >= 0) { present[slot] = true; continue; }
    for (int s = 0; s < kSlots && slot < 0; ++s) if (!g_dev[s].raw) slot = s;
    if (slot < 0) continue;   // four already, which is every port Melee has
    if (open_device(g_dev[slot], list[i].hDevice, info.hid.dwProductId, slot)) present[slot] = true;
  }

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    bool any = false;
    for (int s = 0; s < kSlots; ++s) {
      if (g_dev[s].raw && !present[s]) close_device(g_dev[s], "disconnected", false);
      any = any || g_dev[s].raw != nullptr;
    }
    g_any_device.store(any);
  }

  if (!found) {
    if (!g_logged_missing) {
      log("switch pro: no Nintendo controller found (VID 057E); keyboard/XInput/DS4 stay active");
      g_logged_missing = true;
    }
  } else {
    g_logged_missing = false;
  }
}

// ---- initialisation ----------------------------------------------------------------------------

// The Switch Online GameCube controller over USB says nothing until it has been sent a start-up
// sequence, and that sequence does not go to the HID interface: it goes to the bulk OUT endpoint of
// the controller's second interface (number 1). Windows gives that interface no driver of its own,
// so it can only be reached where a WinUSB driver has been put on it (Zadig, "Interface 1"), the
// same step the GameCube adapter needs. The commands are the ones the console sends, as published
// by the community tools that enable these controllers on a PC. After them the HID interface
// streams 64-byte reports, which arrive as WM_INPUT like every other pad's.
// Returns nullptr when every command was written, otherwise what stopped it.
const char* gamecube_start(int slot, uint32_t pid) {
  static const uint8_t kCommands[][28] = {
      {0x03, 0x91, 0x00, 0x0D, 0x00, 0x08, 0x00, 0x00, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
      {0x07, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00},
      {0x16, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00},
      {0x15, 0x91, 0x00, 0x01, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
      {0x15, 0x91, 0x00, 0x02, 0x00, 0x11, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
      {0x15, 0x91, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00},
      {0x09, 0x91, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      {0x0C, 0x91, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00},
      {0x11, 0x91, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00},
      {0x0A, 0x91, 0x00, 0x08, 0x00, 0x14, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x35, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      {0x0C, 0x91, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00},
      {0x03, 0x91, 0x00, 0x0A, 0x00, 0x04, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00},
      {0x10, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00},
      {0x01, 0x91, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00},
      {0x03, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00},
      {0x0A, 0x91, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0x00},
      {0x09, 0x91, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // player light
  };
  static const uint8_t kLengths[] = {16, 8, 8, 22, 25, 9, 16, 12, 8, 28, 12, 12, 8, 8, 7, 11, 16};
  static libusb_context* context = nullptr;
  if (!context && libusb_init(&context) != 0) { context = nullptr; return "libusb could not start"; }
  libusb_device** list = nullptr;
  const ssize_t count = libusb_get_device_list(context, &list);
  if (count < 0) return "the USB device list could not be read";
  // The slot-th controller of this kind when there is one, so two of them each get their start-up.
  libusb_device* found = nullptr;
  libusb_device* first = nullptr;
  int seen = 0;
  for (ssize_t i = 0; i < count && !found; ++i) {
    libusb_device_descriptor desc{};
    if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
    if (desc.idVendor != kNintendoVid || desc.idProduct != pid) continue;
    if (!first) first = list[i];
    if (seen++ == slot) found = list[i];
  }
  if (!found) found = first;
  const char* why = nullptr;
  libusb_device_handle* handle = nullptr;
  uint8_t out = 0, in = 0;
  if (!found) why = "it is not on USB";
  else {
    libusb_config_descriptor* config = nullptr;
    if (libusb_get_active_config_descriptor(found, &config) == 0 && config) {
      for (uint8_t i = 0; i < config->bNumInterfaces; ++i) {
        const libusb_interface& itf = config->interface[i];
        if (!itf.num_altsetting || itf.altsetting[0].bInterfaceNumber != 1) continue;
        for (uint8_t e = 0; e < itf.altsetting[0].bNumEndpoints; ++e) {
          const libusb_endpoint_descriptor& ep = itf.altsetting[0].endpoint[e];
          if ((ep.bmAttributes & 3) != LIBUSB_TRANSFER_TYPE_BULK) continue;
          if (ep.bEndpointAddress & 0x80) in = ep.bEndpointAddress; else out = ep.bEndpointAddress;
        }
      }
      libusb_free_config_descriptor(config);
    }
    if (!out) why = "its command interface (interface 1) shows no bulk endpoint to write to";
    else if (libusb_open(found, &handle) != 0 || !handle) {
      handle = nullptr;
      why = "its command interface has no WinUSB driver: install one on \"Interface 1\" with Zadig, then replug";
    } else if (libusb_claim_interface(handle, 1) != 0) {
      why = "its command interface could not be claimed: install a WinUSB driver on \"Interface 1\" with Zadig, "
            "or close the program that holds it, then replug";
    }
  }
  if (!why) {
    for (size_t c = 0; c < sizeof kLengths; ++c) {
      uint8_t packet[28];
      std::memcpy(packet, kCommands[c], kLengths[c]);
      if (c + 1 == sizeof kLengths) packet[8] = (uint8_t)(1u << (slot & 3));   // the light for this slot
      int wrote = 0;
      if (libusb_bulk_transfer(handle, out, packet, kLengths[c], &wrote, 1000) != 0) { why = "a start-up command was refused"; break; }
      if (in) { uint8_t reply[64]; int got = 0; libusb_bulk_transfer(handle, in, reply, sizeof reply, &got, 100); }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    libusb_release_interface(handle, 1);
  }
  if (handle) libusb_close(handle);
  libusb_free_device_list(list, 1);
  return why;
}

void initialise(int slot) {
  // Everything below blocks, so work from a copy and put the results back afterwards. Only this
  // thread opens or closes the handle, so it stays valid for the duration.
  HANDLE file; USHORT out_len; bool bluetooth; uint32_t pid; uint8_t counter; int attempts;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    Device& d = g_dev[slot];
    file = d.file; out_len = d.out_len; bluetooth = d.bluetooth; pid = d.pid;
    counter = d.counter; attempts = ++d.attempts;
    d.next_try = Clock::now() + std::chrono::milliseconds(700);
  }

  if (switch2_pid(pid)) {
    // Over Bluetooth it streams by itself once paired; over USB it needs its start-up sequence.
    const char* why = bluetooth ? nullptr : gamecube_start(slot, pid);
    if (why && !bluetooth)
      set_problem(steam_running()
          ? std::string("Your ") + device_name(pid) + " is held by Steam, so the game cannot start it and only sees Steam's copy of it, "
            "with Steam's button layout. Close Steam, or turn off Switch controller support in Steam's Controller settings, then plug it in again."
          : std::string("Your ") + device_name(pid) + " cannot be started: " + why + ".");
    if (attempts == 1) {
      if (why) log("switch pro: slot %d %s over USB cannot be started: %s", slot + 1, device_name(pid), why);
      else log("switch pro: slot %d %s %s", slot + 1, device_name(pid),
               bluetooth ? "on Bluetooth, waiting for its reports" : "start-up sequence sent over USB");
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dev[slot].next_try = Clock::now() + std::chrono::milliseconds(1500);
    return;
  }

  Device scratch{};
  scratch.file = file; scratch.out_len = out_len; scratch.bluetooth = bluetooth; scratch.counter = counter;

  bool handshake = true;
  if (!bluetooth) {
    handshake = send_usb_command(scratch, 0x02);          // handshake: start talking
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    send_usb_command(scratch, 0x04);                      // no USB timeout, so it keeps talking
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
  }
  const bool mode = send_subcommand(scratch, 0x03, 0x30); // input report mode: standard full
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  send_subcommand(scratch, 0x30, (uint8_t)(1u << slot));  // player LED, so the pad shows its slot

  if (attempts == 1)
    log("switch pro: slot %d initialising over %s (%s handshake %s, report mode %s)", slot + 1,
        bluetooth ? "Bluetooth" : "USB", device_name(pid),
        bluetooth ? "not needed" : (handshake ? "sent" : "refused"), mode ? "sent" : "refused");

  std::lock_guard<std::mutex> lock(g_mutex);
  g_dev[slot].counter = scratch.counter;
}

void worker() {
  auto next_scan = Clock::now();
  while (g_running.load()) {
    const auto now = Clock::now();
    if (now >= next_scan) { next_scan = now + std::chrono::seconds(1); scan(); }

    for (int slot = 0; slot < kSlots; ++slot) {
      bool needs_init = false, give_up = false;
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        Device& d = g_dev[slot];
        if (!d.raw || d.file == INVALID_HANDLE_VALUE) continue;
        const auto at = Clock::now();
        if (at < d.next_try) continue;
        // Reports stopping is what happens when something else re-enabled the USB timeout or put
        // the pad back into simple mode, so treat silence the same as never having started.
        const bool quiet = !d.streaming || at - d.last_report > std::chrono::seconds(3);
        if (d.full && !quiet) continue;
        if (quiet) { d.full = false; d.streaming = false; }
        if (d.attempts >= kMaxInitAttempts) {
          give_up = !d.failed;
          d.failed = true;
          // Start the sequence over in a few seconds rather than giving up for the whole run:
          // whatever was holding the controller may have been closed since.
          d.attempts = 0;
          d.next_try = at + std::chrono::seconds(5);
        } else {
          needs_init = true;
        }
      }
      if (give_up)
        log("switch pro: slot %d sent no input after %d attempts. Check that no other program "
            "(Steam Input with Nintendo support, BetterJoy, a vJoy driver) is already using the "
            "controller, and that the cable is a data cable rather than charge only.",
            slot + 1, kMaxInitAttempts);
      if (needs_init) initialise(slot);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

void start_worker() {
  std::call_once(g_start_once, [] {
    for (int s = 0; s < kSlots; ++s) g_dev[s].slot = s;
    g_running.store(true);
    g_thread = std::thread(worker);
  });
}

// ---- input reports -----------------------------------------------------------------------------

void unpack_stick(const uint8_t* d, int* x, int* y) {
  *x = d[0] | ((d[1] & 0x0F) << 8);
  *y = (d[1] >> 4) | (d[2] << 4);
}

// Nintendo's sticks already grow upward, unlike a DualShock's, so there is nothing to invert here.
int8_t axis(int raw, int centre) {
  const int delta = raw - centre;
  const int sign = delta < 0 ? -1 : 1;
  const int magnitude = std::abs(delta) - kStickDead;
  if (magnitude <= 0) return 0;
  int value = magnitude * 127 / (kStickRange - kStickDead);
  if (value > 127) value = 127;
  return (int8_t)(sign * value);
}

// 0x30 "standard full": timer, battery, three button bytes, then two sticks of two 12-bit values.
void decode_full(Device& d, const uint8_t* r, size_t size) {
  if (size < 12) return;
  const uint8_t right = r[3], shared = r[4], left = r[5];
  uint16_t b = 0;
  if (right & 0x01) b |= SWPRO_Y;   if (right & 0x02) b |= SWPRO_X;
  if (right & 0x04) b |= SWPRO_B;   if (right & 0x08) b |= SWPRO_A;
  if (right & 0x40) b |= SWPRO_R;   if (right & 0x80) b |= SWPRO_ZR;
  if (shared & 0x01) b |= SWPRO_MINUS; if (shared & 0x02) b |= SWPRO_PLUS;
  if (shared & 0x04) b |= SWPRO_R3;    if (shared & 0x08) b |= SWPRO_L3;
  if (left & 0x01) b |= SWPRO_DPAD_DOWN;  if (left & 0x02) b |= SWPRO_DPAD_UP;
  if (left & 0x04) b |= SWPRO_DPAD_RIGHT; if (left & 0x08) b |= SWPRO_DPAD_LEFT;
  if (left & 0x40) b |= SWPRO_L;   if (left & 0x80) b |= SWPRO_ZL;

  int nx, ny, cx, cy;
  unpack_stick(r + 6, &nx, &ny);
  unpack_stick(r + 9, &cx, &cy);
  // A right Joy-Con on its own reports its one stick in the right-hand field; give it to the game
  // as the main stick rather than the C-stick.
  if (d.pid == 0x2007) { std::swap(nx, cx); std::swap(ny, cy); }

  if (!d.origin) {
    d.origin = true;
    d.nx = nx; d.ny = ny; d.cx = cx; d.cy = cy;
    // Same idea as the GameCube adapter's origin: the neutral comes from the first report, so a
    // stick held at that moment offsets everything afterwards and the log says what was recorded.
    log("switch pro: slot %d streaming full reports, neutral stick %d,%d c-stick %d,%d",
        d.slot + 1, nx, ny, cx, cy);
  }

  PadState pad{};
  pad.err = 0;
  pad.stick_x = axis(nx, d.nx); pad.stick_y = axis(ny, d.ny);
  pad.sub_x = axis(cx, d.cx);   pad.sub_y = axis(cy, d.cy);
  d.pad = pad; d.buttons = b;
  d.full = true; d.streaming = true; d.attempts = 0; d.failed = false;
}

// The Switch Online GameCube controller's report: after the report id and two counter bytes come
// three button bytes, the two sticks as two 12-bit values each, a spare byte, and the two analog
// triggers. The buttons are handed on in SWPRO_* bits placed so that the default Switch bindings
// give each GameCube button its own name: the controller's A lands on the game's A.
void decode_gamecube(Device& d, const uint8_t* r, size_t size) {
  if (size < 15) return;
  const uint8_t right = r[3], left = r[4];
  uint16_t b = 0;
  if (right & 0x01) b |= SWPRO_A;   if (right & 0x02) b |= SWPRO_B;   // B and A: the defaults bind the game's A to SWPRO_B
  if (right & 0x04) b |= SWPRO_Y;   if (right & 0x08) b |= SWPRO_X;
  if (right & 0x10) b |= SWPRO_R;   if (right & 0x20) b |= SWPRO_ZR;  // R click, Z
  if (right & 0x40) b |= SWPRO_PLUS;                                  // Start
  if (left & 0x01) b |= SWPRO_DPAD_DOWN;  if (left & 0x02) b |= SWPRO_DPAD_RIGHT;
  if (left & 0x04) b |= SWPRO_DPAD_LEFT;  if (left & 0x08) b |= SWPRO_DPAD_UP;
  if (left & 0x10) b |= SWPRO_L;    if (left & 0x20) b |= SWPRO_ZL;   // L click, the small ZL
  if (left & 0x40) b |= SWPRO_MINUS;

  int nx, ny, cx, cy;
  unpack_stick(r + 6, &nx, &ny);
  unpack_stick(r + 9, &cx, &cy);
  if (!d.origin) {
    d.origin = true;
    d.nx = nx; d.ny = ny; d.cx = cx; d.cy = cy;
    set_problem("");
    log("switch pro: slot %d GameCube controller streaming, neutral stick %d,%d c-stick %d,%d, triggers %u,%u",
        d.slot + 1, nx, ny, cx, cy, (unsigned)r[13], (unsigned)r[14]);
  }
  // Full tilt is about 1500 counts from the centre on the main stick and a little less on the C
  // stick; the ranges are short of that so every unit reaches the edge of the gate.
  const auto stick = [](int raw, int centre, int range) {
    const int delta = raw - centre, sign = delta < 0 ? -1 : 1, magnitude = std::abs(delta) - kStickDead;
    if (magnitude <= 0) return (int8_t)0;
    const int value = magnitude * 127 / (range - kStickDead);
    return (int8_t)(sign * (value > 127 ? 127 : value));
  };
  // The triggers rest near 36 and bottom out near 240.
  const auto trigger = [](uint8_t raw) {
    const int value = ((int)raw - 36) * 255 / (240 - 36);
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
  };
  PadState pad{};
  pad.err = 0;
  pad.stick_x = stick(nx, d.nx, 1400); pad.stick_y = stick(ny, d.ny, 1400);
  pad.sub_x = stick(cx, d.cx, 1300);   pad.sub_y = stick(cy, d.cy, 1300);
  pad.trig_l = trigger(r[13]); pad.trig_r = trigger(r[14]);
  d.pad = pad; d.buttons = b;
  d.full = true; d.streaming = true; d.attempts = 0; d.failed = false;
}

// The Switch 2 Pro Controller's report has the GameCube controller's shape: report id, two counter
// bytes, three button bytes, then the two sticks as two 12-bit values each. Its buttons carry
// their own names, so they go to the same SWPRO_* bits a first Pro Controller uses and the
// default Switch bindings apply as they are. ZL and ZR are plain buttons: there is no travel.
void decode_pro2(Device& d, const uint8_t* r, size_t size) {
  if (size < 12) return;
  const uint8_t right = r[3], left = r[4];
  uint16_t b = 0;
  if (right & 0x01) b |= SWPRO_B;   if (right & 0x02) b |= SWPRO_A;
  if (right & 0x04) b |= SWPRO_Y;   if (right & 0x08) b |= SWPRO_X;
  if (right & 0x10) b |= SWPRO_R;   if (right & 0x20) b |= SWPRO_ZR;
  if (right & 0x40) b |= SWPRO_PLUS; if (right & 0x80) b |= SWPRO_R3;
  if (left & 0x01) b |= SWPRO_DPAD_DOWN;  if (left & 0x02) b |= SWPRO_DPAD_RIGHT;
  if (left & 0x04) b |= SWPRO_DPAD_LEFT;  if (left & 0x08) b |= SWPRO_DPAD_UP;
  if (left & 0x10) b |= SWPRO_L;    if (left & 0x20) b |= SWPRO_ZL;
  if (left & 0x40) b |= SWPRO_MINUS; if (left & 0x80) b |= SWPRO_L3;

  int nx, ny, cx, cy;
  unpack_stick(r + 6, &nx, &ny);
  unpack_stick(r + 9, &cx, &cy);
  if (!d.origin) {
    d.origin = true;
    d.nx = nx; d.ny = ny; d.cx = cx; d.cy = cy;
    log("switch pro: slot %d Switch 2 Pro Controller streaming, neutral stick %d,%d right stick %d,%d (report %02X, %u bytes)",
        d.slot + 1, nx, ny, cx, cy, (unsigned)r[0], (unsigned)size);
  }
  const auto axis = [](int raw, int centre) {
    const int delta = raw - centre, sign = delta < 0 ? -1 : 1, magnitude = std::abs(delta) - kStickDead;
    if (magnitude <= 0) return (int8_t)0;
    const int value = magnitude * 127 / (kStickRange - kStickDead);
    return (int8_t)(sign * (value > 127 ? 127 : value));
  };
  PadState pad{};
  pad.err = 0;
  pad.stick_x = axis(nx, d.nx); pad.stick_y = axis(ny, d.ny);
  pad.sub_x = axis(cx, d.cx);   pad.sub_y = axis(cy, d.cy);
  d.pad = pad; d.buttons = b;
  d.full = true; d.streaming = true; d.attempts = 0; d.failed = false;
}

// 0x3F "simple": what a Bluetooth pad sends before it has been asked for anything better. The
// sticks in this mode only report their extremes, so it is a stopgap until 0x30 takes over.
void decode_simple(Device& d, const uint8_t* r, size_t size) {
  if (size < 12) return;
  const uint8_t low = r[1], high = r[2];
  uint16_t b = 0;
  if (low & 0x01) b |= SWPRO_B;  if (low & 0x02) b |= SWPRO_A;
  if (low & 0x04) b |= SWPRO_Y;  if (low & 0x08) b |= SWPRO_X;
  if (low & 0x10) b |= SWPRO_L;  if (low & 0x20) b |= SWPRO_R;
  if (low & 0x40) b |= SWPRO_ZL; if (low & 0x80) b |= SWPRO_ZR;
  if (high & 0x01) b |= SWPRO_MINUS; if (high & 0x02) b |= SWPRO_PLUS;
  if (high & 0x04) b |= SWPRO_L3;    if (high & 0x08) b |= SWPRO_R3;
  switch (r[3]) {   // hat: 0 = up, clockwise, 8 = centred
    case 0: b |= SWPRO_DPAD_UP; break;
    case 1: b |= SWPRO_DPAD_UP | SWPRO_DPAD_RIGHT; break;
    case 2: b |= SWPRO_DPAD_RIGHT; break;
    case 3: b |= SWPRO_DPAD_DOWN | SWPRO_DPAD_RIGHT; break;
    case 4: b |= SWPRO_DPAD_DOWN; break;
    case 5: b |= SWPRO_DPAD_DOWN | SWPRO_DPAD_LEFT; break;
    case 6: b |= SWPRO_DPAD_LEFT; break;
    case 7: b |= SWPRO_DPAD_UP | SWPRO_DPAD_LEFT; break;
    default: break;
  }
  auto simple_axis = [](const uint8_t* p) {
    const int v = (int)(p[0] | (p[1] << 8)) - 0x8000;
    const int a = v * 127 / 0x7FFF;
    return (int8_t)(a > 127 ? 127 : a < -127 ? -127 : a);
  };
  PadState pad{};
  pad.err = 0;
  pad.stick_x = simple_axis(r + 4); pad.stick_y = simple_axis(r + 6);
  pad.sub_x = simple_axis(r + 8);   pad.sub_y = simple_axis(r + 10);
  d.pad = pad; d.buttons = b;
  d.full = false; d.streaming = true;
}

}  // namespace

// Called from the window's WM_INPUT handler for every HID report that was not a DualShock's.
// Returns true when the report belonged to a controller this file manages.
bool switchpro_raw_input(void* device, const uint8_t* report, size_t size, size_t count) {
  if (!g_any_device.load(std::memory_order_relaxed) || !report || !size) return false;
  if (options.no_gc_adapter) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  int slot = -1;
  for (int s = 0; s < kSlots; ++s) if (g_dev[s].raw == (HANDLE)device) { slot = s; break; }
  if (slot < 0) return false;
  Device& d = g_dev[slot];
  // Raw Input can batch several reports of the same size into one message; the last one is the
  // current state, but walking them all keeps a button that was tapped inside the batch.
  for (size_t i = 0; i < count; ++i) {
    const uint8_t* r = report + i * size;
    if (d.pid == kGameCubePid) { if (size < 15) continue; decode_gamecube(d, r, size); }
    else if (d.pid == kPro2Pid) { if (size < 12) continue; decode_pro2(d, r, size); }
    else if (r[0] == 0x30 || r[0] == 0x21 || r[0] == 0x31) decode_full(d, r, size);
    else if (r[0] == 0x3F) decode_simple(d, r, size);
    else continue;
    d.last_report = Clock::now();
  }
  return true;
}

// Fills the slots that have a Switch controller sending input; returns the mask of those slots and
// their raw SWPRO_* button bits, which window.cpp then runs through the remappable binding table.
uint32_t switchpro_poll(PadState out[4], uint16_t buttons[4]) {
  if (options.no_gc_adapter) return 0;   // a hidden test run must not reconfigure a player's pad
  start_worker();
  uint32_t mask = 0;
  std::lock_guard<std::mutex> lock(g_mutex);
  for (int s = 0; s < kSlots; ++s) {
    if (!g_dev[s].raw || !g_dev[s].streaming) continue;
    out[s] = g_dev[s].pad;
    buttons[s] = g_dev[s].buttons;
    mask |= 1u << s;
  }
  return mask;
}

// PADRecalibrate: forget the stored neutral so the next report establishes it again.
void switchpro_recalibrate(int slot) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (slot < 0 || slot > 3) { for (auto& d : g_dev) d.origin = false; return; }
  g_dev[slot].origin = false;
}

void switchpro_shutdown() {
  if (!g_running.exchange(false)) return;
  // Nothing here blocks for long, but a write to a controller that was yanked mid-transfer could,
  // so cancel any I/O in flight before waiting for the thread.
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& d : g_dev) if (d.file != INVALID_HANDLE_VALUE) CancelIoEx(d.file, nullptr);
  }
  if (g_thread.joinable()) g_thread.join();
  std::lock_guard<std::mutex> lock(g_mutex);
  for (auto& d : g_dev) if (d.raw) close_device(d, nullptr, true);
  g_any_device.store(false);
}

std::string switch_pad_problem() {
  std::lock_guard<std::mutex> lock(g_problem_mutex);
  return g_problem;
}

}  // namespace host
