// Generic HID gamepads. See hid_pad.h for why this path exists and what it deliberately does not do.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hid_pad.h"
#include "host.h"
#include "controller_relay.h"
#include "input_bindings.h"
#define NOMINMAX
#include <windows.h>
#include <hidsdi.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <array>
#include <cstring>
#include <mutex>
#include <vector>

#pragma comment(lib, "hid.lib")

namespace host {
namespace {

// Generic Desktop usages. A gamepad publishes its axes under these, which is what makes a descriptor
// worth reading rather than guessing: the device says "this 16-bit field is X" and we believe it.
constexpr USAGE kUsagePageGeneric = 0x01;
constexpr USAGE kUsageX = 0x30, kUsageY = 0x31, kUsageZ = 0x32;
constexpr USAGE kUsageRx = 0x33, kUsageRy = 0x34, kUsageRz = 0x35;
constexpr USAGE kUsageSlider = 0x36, kUsageHat = 0x39;
constexpr USAGE kUsagePageButton = 0x09;
// Simulation Controls: where the Arduino Joystick library, which many Leonardo-based boxes are built on,
// puts everything past the two sticks (rudder, throttle, accelerator, brake).
constexpr USAGE kUsagePageSimulation = 0x02;
constexpr USAGE kUsageRudder = 0xBA, kUsageThrottle = 0xBB, kUsageAccelerator = 0xC4, kUsageBrake = 0xC5;

const char* axis_name(USAGE usage) {
  switch (usage) {
    case kUsageX: return "X"; case kUsageY: return "Y"; case kUsageZ: return "Z";
    case kUsageRx: return "Rx"; case kUsageRy: return "Ry"; case kUsageRz: return "Rz";
    case kUsageSlider: return "Slider"; case kUsageHat: return "Hat";
    case kUsageRudder: return "Rudder"; case kUsageThrottle: return "Throttle";
    case kUsageAccelerator: return "Accelerator"; case kUsageBrake: return "Brake";
    default: return "?";
  }
}

struct Axis {
  USAGE usage = 0;
  LONG logical_min = 0, logical_max = 0;
  bool present = false;
  USHORT bits = 0;   // the field's size in the report
  USAGE page = kUsagePageGeneric;
};

struct Slot {
  HANDLE device = nullptr;
  std::vector<uint8_t> preparsed;      // RIDI_PREPARSEDDATA blob, owned here
  std::array<Axis, 8> axes{};          // in the order of kAxisOrder
  int axis_count = 0;
  USAGE button_min = 0, button_max = 0;
  bool haybox = false;                 // HayBox DInput (haybox_dinput_shape): triggers on Z and Rz
  bool sim_triggers = false;           // its triggers are Simulation Controls fields (describe)
  bool frame1 = false;                 // Frame1 firmware in DInput mode (frame1_dinput_name in hid_pad.h)
  // Every Slider field by its data index, in report order: a Frame1 has two (L, R), which one usage
  // lookup cannot tell apart.
  std::array<USHORT, 2> slider_index{};
  int slider_count = 0;
  int8_t f1_enc[8] = {-1, -1, -1, -1, -1, -1, -1, -1};   // per axis, from the first report
  int8_t stick_enc[8] = {-1, -1, -1, -1, -1, -1, -1, -1};   // other pads' stick axes (hid_stick_encoding)
  int8_t f1_trig_enc[2] = {-1, -1};
  std::string name;
  bool ready = false;
  // Latest decoded state.
  PadState pad{};
  uint32_t buttons = 0;
  int log_reports = 0;   // first few reports after a device appears, for diagnosing a pad that will not bind
  int log_changes = 0;   // and the first presses after that: which bit or trigger each button moves
  // Trigger values on their own count, each settled value once: a box's light and mid shield (79, 124)
  // move only the trigger, and a trigger resting at 30 never crossed the old "above 20" test, so the
  // logs a player sent for those buttons held no trigger line at all (11-23).
  int log_triggers = 0;
  uint8_t logged_l = 0, logged_r = 0;
  std::string fields;    // every value field the device declares, for the log
  int32_t raw[8]{};
  int32_t val[8]{};        // the same, as the signed number a signed field carries
  int8_t dec[8]{};         // every axis as a stick value, for choosing one by moving it
  int32_t base_val[8]{};   // both, where hidpad_axis_capture_begin found them
  int8_t base_dec[8]{};
  bool fresh = false;
  // Where each axis rests when nothing touches it, decided from the first report (see hid_trigger_rest).
  // Only the axes read as triggers use it.
  int8_t rest[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  uint8_t origin[8] = {255, 255, 255, 255, 255, 255, 255, 255};   // hid_trigger_from_origin, per axis
};

constexpr USAGE kAxisOrder[8] = {kUsageX, kUsageY, kUsageZ, kUsageRx, kUsageRy, kUsageRz, kUsageSlider, kUsageHat};

std::mutex g_mutex;
std::array<Slot, kHidPadSlots> g_slots;

PHIDP_PREPARSED_DATA preparsed_of(Slot& s) {
  return s.preparsed.empty() ? nullptr : reinterpret_cast<PHIDP_PREPARSED_DATA>(s.preparsed.data());
}

std::string device_path(HANDLE device) {
  UINT size = 0;
  if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &size) != 0 || !size) return {};
  std::wstring wide(size + 1, L'\0');
  if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, wide.data(), &size) == (UINT)-1) return {};
  wide.resize(wcslen(wide.c_str()));
  return std::string(wide.begin(), wide.end());
}

// Devices another path in this port already owns, or that Windows exposes twice. Reading a pad on
// two paths at once makes it fight itself, which looks like a broken controller rather than a
// duplicated one.
bool claimed_elsewhere(const RID_DEVICE_INFO_HID& hid, const std::string& path) {
  if (playstation_pad(hid.dwVendorId, hid.dwProductId)) return true;                                      // DS4, DualSense
  if (hid.dwVendorId == 0x057E) return true;                                                              // Switch Pro family

  // An XInput device appears as HID as well, and its interface path carries "IG_". XInput already
  // reads those, so taking them here would double up every Xbox pad.
  if (path.find("IG_") != std::string::npos) return true;
  return false;
}

std::string friendly_name(const std::string& path, const RID_DEVICE_INFO_HID& hid) {
  HANDLE file = CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  std::string name;
  if (file != INVALID_HANDLE_VALUE) {
    wchar_t buffer[256] = {};
    if (HidD_GetProductString(file, buffer, sizeof buffer)) {
      std::wstring wide(buffer);
      name.assign(wide.begin(), wide.end());
    }
    CloseHandle(file);
  }
  while (!name.empty() && (name.back() == ' ' || name.back() == '\0')) name.pop_back();
  if (name.empty()) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "HID pad %04X:%04X", (unsigned)hid.dwVendorId, (unsigned)hid.dwProductId);
    name = buf;
  }
  return name;
}

// Reads the descriptor and records where this device keeps its axes and buttons. Done once per
// device, not per report.
bool describe(Slot& s, HANDLE device) {
  UINT size = 0;
  if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, nullptr, &size) != 0 || !size) return false;
  s.preparsed.assign(size, 0);
  if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, s.preparsed.data(), &size) == (UINT)-1) return false;

  HIDP_CAPS caps{};
  if (HidP_GetCaps(preparsed_of(s), &caps) != HIDP_STATUS_SUCCESS) return false;

  std::vector<HIDP_VALUE_CAPS> values(caps.NumberInputValueCaps);
  USHORT value_count = caps.NumberInputValueCaps;
  if (value_count && HidP_GetValueCaps(HidP_Input, values.data(), &value_count, preparsed_of(s)) != HIDP_STATUS_SUCCESS)
    value_count = 0;
  s.axis_count = 0;
  for (int i = 0; i < 8; ++i) {
    for (USHORT v = 0; v < value_count; ++v) {
      const HIDP_VALUE_CAPS& c = values[v];
      if (c.UsagePage != kUsagePageGeneric) continue;
      const USAGE usage = c.IsRange ? c.Range.UsageMin : c.NotRange.Usage;
      if (usage != kAxisOrder[i]) continue;
      s.axes[i].usage = usage;
      s.axes[i].logical_min = c.LogicalMin;
      s.axes[i].logical_max = c.LogicalMax;
      // A descriptor that declares an empty or inverted range tells us nothing usable; fall back to
      // the bit width, which is what the device is actually sending.
      if (s.axes[i].logical_max <= s.axes[i].logical_min)
        s.axes[i] = {usage, 0, (LONG)((1u << std::min<USHORT>(c.BitSize, 31)) - 1), true};
      s.axes[i].present = true;
      s.axes[i].bits = c.BitSize;
      ++s.axis_count;
      break;
    }
  }

  s.slider_count = 0;
  for (USHORT v = 0; v < value_count; ++v) {
    const HIDP_VALUE_CAPS& c = values[v];
    if (c.UsagePage != kUsagePageGeneric) continue;
    if (c.IsRange) {
      for (USAGE u = c.Range.UsageMin; u <= c.Range.UsageMax && s.slider_count < 2; ++u)
        if (u == kUsageSlider) s.slider_index[s.slider_count++] = (USHORT)(c.Range.DataIndexMin + (u - c.Range.UsageMin));
    } else if (c.NotRange.Usage == kUsageSlider && s.slider_count < 2) {
      s.slider_index[s.slider_count++] = c.NotRange.DataIndex;
    }
  }
  std::sort(s.slider_index.begin(), s.slider_index.begin() + s.slider_count);

  std::vector<HIDP_BUTTON_CAPS> buttons(caps.NumberInputButtonCaps);
  USHORT button_count = caps.NumberInputButtonCaps;
  if (button_count && HidP_GetButtonCaps(HidP_Input, buttons.data(), &button_count, preparsed_of(s)) == HIDP_STATUS_SUCCESS) {
    for (USHORT b = 0; b < button_count; ++b) {
      if (buttons[b].UsagePage != kUsagePageButton) continue;
      s.button_min = buttons[b].IsRange ? buttons[b].Range.UsageMin : buttons[b].NotRange.Usage;
      s.button_max = buttons[b].IsRange ? buttons[b].Range.UsageMax : buttons[b].NotRange.Usage;
      break;
    }
  }
  HidAxisRange ranges[8];
  for (int i = 0; i < 8; ++i) ranges[i] = {s.axes[i].present, (long)s.axes[i].logical_min, (long)s.axes[i].logical_max};
  s.haybox = haybox_dinput_shape(ranges, s.button_min, s.button_max);
  // Two sticks and nothing else among the usual axes, plus exactly two Simulation Controls fields: those
  // are the box's analog triggers (a B0XX r2 in DInput mode: 16 report bytes, of which X Y Rx Ry and the
  // buttons explain 12; its light shield buttons did nothing and could not be bound, 11-23). They take the
  // slider's place and Z's, in report order. On that box both carry the same value, the L trigger's (its
  // light and mid shield buttons modify L), so only the first is read by itself, as L; the other can be
  // given to R under Controls > Box layouts.
  s.fields.clear();
  int sim[2] = {-1, -1}, sim_count = 0;
  for (USHORT v = 0; v < value_count; ++v) {
    const HIDP_VALUE_CAPS& c = values[v];
    const USAGE usage = c.IsRange ? c.Range.UsageMin : c.NotRange.Usage;
    char one[80];
    std::snprintf(one, sizeof one, "%s%02X:%02X %u bits %ld..%ld", s.fields.empty() ? "" : ", ", (unsigned)c.UsagePage,
                  (unsigned)usage, (unsigned)c.BitSize, (long)c.LogicalMin, (long)c.LogicalMax);
    if (s.fields.size() < 600) s.fields += one;
    if (c.UsagePage != kUsagePageSimulation || c.IsRange) continue;
    if (usage != kUsageRudder && usage != kUsageThrottle && usage != kUsageAccelerator && usage != kUsageBrake) continue;
    if (sim_count < 2) sim[sim_count] = v;
    ++sim_count;
  }
  if (sim_count == 2 && s.axes[0].present && s.axes[1].present && s.axes[3].present && s.axes[4].present &&
      !s.axes[2].present && !s.axes[5].present && !s.axes[6].present) {
    if (values[sim[0]].NotRange.DataIndex > values[sim[1]].NotRange.DataIndex) std::swap(sim[0], sim[1]);
    static const int kSlot[2] = {6, 2};
    for (int k = 0; k < 2; ++k) {
      const HIDP_VALUE_CAPS& c = values[sim[k]];
      Axis& a = s.axes[kSlot[k]];
      a = {c.NotRange.Usage, c.LogicalMin, c.LogicalMax, true, c.BitSize, kUsagePageSimulation};
      if (a.logical_max <= a.logical_min) { a.logical_min = 0; a.logical_max = (LONG)((1u << std::min<USHORT>(c.BitSize, 31)) - 1); }
      ++s.axis_count;
    }
    s.sim_triggers = true;
  }
  // Something with no axes and no buttons is not a pad, whatever it says it is.
  return s.axis_count > 0 || s.button_max >= s.button_min;
}

int slot_for(HANDLE device) {
  for (int i = 0; i < kHidPadSlots; ++i) if (g_slots[i].device == device) return i;
  for (int i = 0; i < kHidPadSlots; ++i) if (!g_slots[i].device) return i;
  return -1;
}

// Scales a declared axis range onto the signed range the GameCube pad uses exactly as Dolphin does:
// from the centre out (centre -> 0, either end -> 127), the axis flipped first where the stick's
// direction is reversed, then ROUNDED DOWN. Box scripts are tuned against that: b0xx-ahk sends
// 10271 * c + 16448 on X and -10271 * c + 16320 on Y, and its +-64 offsets only land on the right
// values (80 at a full press either way, 0 at rest) when the result is floored. Rounding to nearest
// gave 79 for a full left press, and scaling across the whole span gave 79 for a full right one.
// No deadzone: a box reports exact values and rounding them toward centre is precisely wrong.
int8_t to_signed(const Axis& a, LONG raw, bool flip = false) {
  const double half = ((double)a.logical_max - (double)a.logical_min) / 2.0;
  if (half <= 0) return 0;
  const double mid = ((double)a.logical_max + (double)a.logical_min) / 2.0;
  double v = ((double)raw - mid) / half * 127.0;
  if (flip) v = -v;
  return (int8_t)std::clamp((int)std::floor(v), -128, 127);
}

}  // namespace

int8_t hid_axis_signed(long logical_min, long logical_max, long raw, bool flip) {
  Axis a{};
  a.logical_min = logical_min; a.logical_max = logical_max;
  return to_signed(a, raw, flip);
}

int8_t hid_stick_value(long logical_min, long logical_max, int bits, unsigned long raw, int8_t encoding, bool flip) {
  if (bits < 2 || bits > 31) bits = 16;
  const unsigned long mask = (1ul << bits) - 1, u = raw & mask;
  const long half = 1l << (bits - 1);
  // A signed field that spans its whole width carries the GameCube byte in its top eight bits (a B0XX in
  // DInput: +20688 up, -20432 down, 0 at rest): that byte, exactly. Scaling from the middle of the range put
  // rest half a step below zero, which the flipped axis rounded down to -1: a stick at rest read 0.0125
  // (player report 11-22).
  // The declared range may stop one short of the field's lowest value (-32767..32767): the same data.
  const bool whole_signed = logical_max == half - 1 && (logical_min == -half || logical_min == -(half - 1));
  if (bits >= 8 && (encoding == kStickTwos ? logical_min >= 0 || whole_signed : encoding == kStickDeclared && whole_signed)) {
    const long s = u >= (unsigned long)half ? (long)u - (half << 1) : (long)u;
    long v = s >= 0 ? s >> (bits - 8) : -((-s + (1l << (bits - 8)) - 1) >> (bits - 8));   // floor, whatever >> does to a negative
    if (flip) v = -v;
    return (int8_t)std::clamp(v, -128l, 127l);
  }
  if (encoding == kStickTwos) {
    // The field's bits as a signed number, scaled across the declared range (or the field's own when the
    // declared range is unsigned).
    const long s = u >= (unsigned long)half ? (long)u - (half << 1) : (long)u;
    if (logical_min < 0) return hid_axis_signed(logical_min, logical_max, s, flip);
    return hid_axis_signed(-half, half - 1, s, flip);
  }
  if (encoding == kStickOffset) return hid_axis_signed(0, (half << 1) - 1, (long)u, flip);   // centred mid-field
  // As declared. A field declared with a signed range holds a signed number: Windows hands its bits over
  // unsigned, so a negative reading (left, down) came out as a large positive one, and both directions of
  // the stick read as the same one (a B0XX r2 in DInput, 11-22: "left and right both = right").
  if (logical_min < 0) return hid_axis_signed(logical_min, logical_max, u >= (unsigned long)half ? (long)u - (half << 1) : (long)u, flip);
  return hid_axis_signed(logical_min, logical_max, (long)u, flip);
}

int8_t hid_stick_encoding(long logical_min, long logical_max, int bits, unsigned long rest) {
  auto away = [](int v) { return v < 0 ? -v : v; };
  const int declared = away(hid_stick_value(logical_min, logical_max, bits, rest, kStickDeclared));
  if (declared <= 64) return kStickDeclared;   // a normal pad (or a stick held at plug-in): as declared
  const int twos = away(hid_stick_value(logical_min, logical_max, bits, rest, kStickTwos));
  const int offset = away(hid_stick_value(logical_min, logical_max, bits, rest, kStickOffset));
  if (twos <= 16 && twos <= offset) return kStickTwos;
  if (offset <= 16) return kStickOffset;
  return kStickDeclared;
}

// Where a trigger axis sits untouched. A real analog trigger rests at one end of its range. A vJoy
// axis that its feeder never writes rests in the middle, and reading that from the bottom of the
// range put every shield at half pressed for as long as the device was plugged in. Dolphin binds such
// an axis as a half-axis (Z+), where the middle is released, and this does the same. Decided once,
// from the first report, by which of the three places the value is nearest.
int8_t hid_trigger_rest(long logical_min, long logical_max, long raw) {
  const double lo = (double)logical_min, hi = (double)logical_max, mid = (lo + hi) / 2.0;
  const double v = (double)raw;
  const double to_lo = std::abs(v - lo), to_mid = std::abs(v - mid), to_hi = std::abs(v - hi);
  if (to_mid < to_lo && to_mid < to_hi) return kTriggerRestCentre;
  return to_hi < to_lo ? kTriggerRestMax : kTriggerRestMin;
}

uint8_t hid_trigger_value(long logical_min, long logical_max, long raw, int8_t rest) {
  const double lo = (double)logical_min, hi = (double)logical_max;
  if (hi <= lo) return 0;
  double t;
  switch (rest) {
    case kTriggerRestCentre: t = ((double)raw - (lo + hi) / 2.0) / ((hi - lo) / 2.0); break;   // released at the middle
    case kTriggerRestMax:    t = (hi - (double)raw) / (hi - lo); break;                         // pulled toward the minimum
    default:                 t = ((double)raw - lo) / (hi - lo); break;
  }
  // Cut off, not rounded, as Dolphin does when it turns an axis into the controller's byte: a B0XX r2 in
  // DInput sends 30, 79 and 124 that way (rest, light shield, mid shield; its origin, 30, comes off: 0, 49, 94).
  // Rounded they were 31, 80 and 125, one too many on each (player report 11-23).
  return (uint8_t)std::clamp((int)std::floor(t * 255.0 + 1e-9), 0, 255);
}

void hidpad_device_removed(void* device_handle) {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (auto& s : g_slots)
    if (s.device && s.device == (HANDLE)device_handle) {
      host::log("hid pad: %s disconnected", s.name.c_str());
      s = Slot{};
    }
}

bool hidpad_raw_input(void* device_handle, const uint8_t* report, uint32_t size, uint32_t count) {
  HANDLE device = (HANDLE)device_handle;
  if (!report || !size || !count) return false;
  // Raw Input packs the reports that arrived since the window last read into one message, oldest first:
  // the newest is the pad's state now. Reading the first one dropped the rest, so the last change before
  // the pad went quiet was lost until its next report: b0xx-ahk writes each vJoy axis on its own, and a
  // released C-stick stayed up or down "until I press another button" (TutukaSSBM, 1.0.0 to 1.0.8).
  report += (size_t)(count - 1) * size;

  RID_DEVICE_INFO info{}; info.cbSize = sizeof info;
  UINT info_size = sizeof info;
  if (GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, &info, &info_size) == (UINT)-1 || info.dwType != RIM_TYPEHID)
    return false;
  // Gamepad (0x05) and joystick (0x04) on the generic desktop page. A box controller presents as one
  // of these; a flight stick does too, and is welcome to be used as one.
  if (info.hid.usUsagePage != kUsagePageGeneric || (info.hid.usUsage != 0x04 && info.hid.usUsage != 0x05))
    return false;

  std::lock_guard<std::mutex> lock(g_mutex);
  int index = slot_for(device);
  if (index < 0) return false;
  Slot& s = g_slots[index];
  if (!s.device) {
    const std::string path = device_path(device);
    if (claimed_elsewhere(info.hid, path)) {
      // Named once in the log: which pad is being read through XInput (or another reader) says what a
      // box in its XInput mode calls itself, which a report about its stick values needs.
      static std::vector<HANDLE> named;
      if (path.find("IG_") != std::string::npos && std::find(named.begin(), named.end(), device) == named.end() && named.size() < 16) {
        named.push_back(device);
        host::log("hid pad: %s (%04X:%04X) is read through XInput", friendly_name(path, info.hid).c_str(),
                  (unsigned)info.hid.dwVendorId, (unsigned)info.hid.dwProductId);
      }
      return false;
    }
    if (!describe(s, device)) return false;
    s.device = device;
    s.name = friendly_name(path, info.hid);
    s.ready = true;
    s.frame1 = !s.haybox && frame1_dinput_name(s.name) && s.axes[0].present && s.axes[1].present;
    // Everything needed to tell, from a log alone, why a pad does not bind: how the device
    // described itself and what the first report actually contained. A box controller reports its
    // buttons in ways that differ from a normal pad, and without this the only symptom is a rebind
    // that waits forever.
    HIDP_CAPS caps{};
    const unsigned collections = HidP_GetCaps(preparsed_of(s), &caps) == HIDP_STATUS_SUCCESS ? caps.NumberLinkCollectionNodes : 0;
    host::log("hid pad %d: %s (%d axes, buttons %u..%u, %u collections, report %u bytes)%s", index + 1, s.name.c_str(),
              s.axis_count, (unsigned)s.button_min, (unsigned)s.button_max, collections, caps.InputReportByteLength,
              s.haybox ? ", a HayBox box in DInput mode (triggers on Z and Rz)"
              : s.frame1 ? ", a Frame1 in DInput mode (C-stick on Z and Rx, triggers on the sliders)" : "");
    for (int i = 0; i < 8; ++i)
      if (s.axes[i].present)
        host::log("hid pad %d: axis %s range %ld..%ld, %u bits", index + 1, axis_name(s.axes[i].usage),
                  (long)s.axes[i].logical_min, (long)s.axes[i].logical_max, (unsigned)s.axes[i].bits);
    if (s.slider_count) host::log("hid pad %d: %d slider fields", index + 1, s.slider_count);
    if (!s.fields.empty()) host::log("hid pad %d: value fields (page:usage): %s", index + 1, s.fields.c_str());
    s.log_reports = 3;
    s.log_changes = 40;
    s.log_triggers = 60;
    s.logged_l = s.logged_r = 0;
  }
  if (!s.ready) return false;

  PadState pad{};
  pad.err = 0;
  LONG value = 0;
  auto read_axis = [&](int i, LONG& out) {
    if (!s.axes[i].present) return false;
    ULONG raw = 0;
    if (HidP_GetUsageValue(HidP_Input, s.axes[i].page, 0, s.axes[i].usage, &raw, preparsed_of(s),
                           (PCHAR)report, size) != HIDP_STATUS_SUCCESS)
      return false;
    out = (LONG)raw;
    s.raw[i] = (int32_t)raw;
    // A field declared with a signed range: its bits as the signed number they are (see hid_stick_value).
    if (s.axes[i].logical_min < 0 && s.axes[i].bits >= 2 && s.axes[i].bits <= 31 && (raw >> (s.axes[i].bits - 1)) & 1u)
      out = (LONG)((long long)(raw & ((1ull << s.axes[i].bits) - 1)) - (1ll << s.axes[i].bits));
    s.val[i] = (int32_t)out;
    return true;
  };
  unsigned present = 0;
  for (int i = 0; i < 7; ++i) if (s.axes[i].present) present |= 1u << i;
  static const uint8_t kNoRoles[HID_ROLE_COUNT] = {kHidAxisAuto, kHidAxisAuto, kHidAxisAuto, kHidAxisAuto, kHidAxisAuto, kHidAxisAuto};
  const uint8_t* roles = index >= 0 && index < 4 ? g_hid_axis_role[index].data() : kNoRoles;

  if (s.frame1) {
    // Frame1 DInput (hid_pad.h): each stick field read as the GameCube byte it carries, exactly.
    auto stick = [&](int i, bool flip, int8_t& out) {
      if (!read_axis(i, value)) return;
      const unsigned long raw = (unsigned long)value;
      if (s.f1_enc[i] < 0) s.f1_enc[i] = frame1_stick_encoding(raw, s.axes[i].bits);
      out = frame1_stick(raw, s.axes[i].bits, s.f1_enc[i], flip);
    };
    stick(0, false, pad.stick_x);
    stick(1, true, pad.stick_y);
    stick(2, false, pad.sub_x);
    stick(3, true, pad.sub_y);
    ULONG count = HidP_MaxDataListLength(HidP_Input, preparsed_of(s));
    if (count && s.slider_count) {
      std::vector<HIDP_DATA> data(count);
      if (HidP_GetData(HidP_Input, data.data(), &count, preparsed_of(s), (PCHAR)report, size) == HIDP_STATUS_SUCCESS) {
        for (ULONG d = 0; d < count; ++d)
          for (int t = 0; t < s.slider_count; ++t) {
            if (data[d].DataIndex != s.slider_index[t]) continue;
            const unsigned long raw = data[d].RawValue;
            const int bits = s.axes[6].bits ? s.axes[6].bits : 16;
            if (s.f1_trig_enc[t] < 0) s.f1_trig_enc[t] = frame1_trigger_encoding(raw, bits);
            (t == 0 ? pad.trig_l : pad.trig_r) = frame1_trigger(raw, bits, s.f1_trig_enc[t]);
          }
      }
    }
    if (s.log_reports == 3)
      host::log("hid pad %d: Frame1 fields at rest: X %08lX Y %08lX Z %08lX Rx %08lX, read as %s", index + 1,
                (unsigned long)s.raw[0], (unsigned long)s.raw[1], (unsigned long)s.raw[2], (unsigned long)s.raw[3],
                s.f1_enc[0] == kFrame1Twos ? "centred on 0" : "centred mid-range");
  } else {
  // X/Y are the control stick. Which pair carries the C-stick differs between devices, so both of
  // the common choices are accepted: Rx/Ry when present, otherwise Z/Rz, which is what most cheap
  // pads and most vJoy feeder configurations use.
  // The stick axes this pad uses: X/Y, then Rx/Ry when present, otherwise Z/Rz.
  const bool rxry = s.axes[3].present && s.axes[4].present;
  // Each one is the player's own choice when they made one (Controls, by moving it).
  const int used[4] = {hid_role_axis(roles, HID_ROLE_STICK_X, 0, present), hid_role_axis(roles, HID_ROLE_STICK_Y, 1, present),
                       hid_role_axis(roles, HID_ROLE_CSTICK_X, rxry ? 3 : 2, present),
                       hid_role_axis(roles, HID_ROLE_CSTICK_Y, rxry ? 4 : 5, present)};
  // Their encoding, decided once from the first report (hid_stick_encoding), for all of them together: another
  // reading only when every stick axis the pad has is off-centre as declared and centred in that same reading,
  // as a box whose descriptor and data disagree reads at rest. One stick held at plug-in keeps the declared reading.
  if (s.stick_enc[0] < 0) {
    int8_t agreed = -1; int seen = 0; bool same = true, all_zero = true;
    for (const int i : used) {
      LONG rest = 0;
      if (i < 0 || !read_axis(i, rest)) continue;
      if (rest != 0) all_zero = false;
      const int8_t e = hid_stick_encoding(s.axes[i].logical_min, s.axes[i].logical_max, s.axes[i].bits, (unsigned long)rest);
      if (seen++ == 0) agreed = e; else if (e != agreed) same = false;
    }
    const int8_t chosen = seen >= 2 && same && agreed > kStickDeclared ? agreed : kStickDeclared;
    // Every stick axis at exactly 0 is a vJoy device whose feeder (b0xx-ahk) has not written its axes yet,
    // not a stick at rest: deciding there read it as two's complement for good, and its C-stick stuck up or
    // down (1.0.0 report). Read as declared and decide again on a later report.
    if (!(all_zero && chosen == kStickTwos)) {
      for (int i = 0; i < 8; ++i) s.stick_enc[i] = chosen;
      if (chosen != kStickDeclared)
        host::log("hid pad %d: its sticks read as %s (their declared range puts them off-centre at rest)", index + 1,
                  chosen == kStickTwos ? "two's complement" : "offset binary");
    }
  }
  // A B0XX on its own firmware 4.x names itself "Arduino Leonardo" and declares its axes signed,
  // -32767..32767, with buttons 1-20 (HayBox on the same board declares 0..65535 and buttons 1-16). It
  // sends the GameCube's direction, up positive, as firmware v3 did below; flipped as HID's Y, a player's
  // up and down were swapped on both sticks (11-25, v2 box on 4.01).
  const bool b0xx_firmware4 = s.button_min == 1 && s.button_max == 20 && s.axes[0].logical_min == -32767 &&
                              s.axes[0].logical_max == 32767 && s.name == "Arduino Leonardo";
  auto stick = [&](int i, bool flip) -> int8_t {
    const Axis& ax = s.axes[i];
    if (b0xx_firmware4) flip = false;
    // Two's-complement data in a field declared unsigned (a B0XX r2 on firmware v3) is the GameCube's own
    // signed reading, up positive (its raw values: up +20688, down -20432): not flipped as HID's Y is.
    if (s.stick_enc[i] == kStickTwos && ax.logical_min >= 0) flip = false;
    return hid_stick_value(ax.logical_min, ax.logical_max, ax.bits, (unsigned long)value, s.stick_enc[i] < 0 ? kStickDeclared : s.stick_enc[i], flip);
  };
  if (used[0] >= 0 && read_axis(used[0], value)) pad.stick_x = stick(used[0], false);
  if (used[1] >= 0 && read_axis(used[1], value)) pad.stick_y = stick(used[1], true);
  if (used[2] >= 0 && read_axis(used[2], value)) pad.sub_x = stick(used[2], false);
  if (used[3] >= 0 && read_axis(used[3], value)) pad.sub_y = stick(used[3], true);
  // Controls > Box layouts: a direction the pad reports reversed, turned round by the player.
  if (index >= 0 && index < 4 && g_hid_stick_invert[index]) {
    const uint8_t inv = g_hid_stick_invert[index];
    auto flip = [](int8_t v) { return (int8_t)std::clamp(-(int)v, -128, 127); };
    if (inv & HID_INVERT_STICK_X) pad.stick_x = flip(pad.stick_x);
    if (inv & HID_INVERT_STICK_Y) pad.stick_y = flip(pad.stick_y);
    if (inv & HID_INVERT_CSTICK_X) pad.sub_x = flip(pad.sub_x);
    if (inv & HID_INVERT_CSTICK_Y) pad.sub_y = flip(pad.sub_y);
  }
  // Triggers, when the device has spare analog axes for them. A box reports these as buttons
  // instead, which the binding table picks up.
  auto trigger = [&](int i, LONG raw) {
    if (s.rest[i] < 0) {
      s.rest[i] = hid_trigger_rest(s.axes[i].logical_min, s.axes[i].logical_max, raw);
      if (s.rest[i] != kTriggerRestMin)
        host::log("hid pad %d: trigger axis %s rests %s", index + 1, axis_name(s.axes[i].usage),
                  s.rest[i] == kTriggerRestCentre ? "at the centre, read as a half-axis" : "at the top, read reversed");
    }
    // The origin off, as a console's pad library takes it (a box resting at 30: its light shield 79 is 49).
    const uint8_t before = s.origin[i];
    const uint8_t v = hid_trigger_from_origin(hid_trigger_value(s.axes[i].logical_min, s.axes[i].logical_max, raw, s.rest[i]),
                                              &s.origin[i]);
    if (s.origin[i] != before && s.origin[i])
      host::log("hid pad %d: trigger axis %s origin %u", index + 1, axis_name(s.axes[i].usage), (unsigned)s.origin[i]);
    return v;
  };
  // HayBox DInput: Z is the left trigger, Rz the right (TUGamepad's report). Any other pad: the slider
  // is the left one, and Z the right one when Rx/Ry carry the C-stick. The player's own choice comes first.
  const int trig_l = hid_role_axis(roles, HID_ROLE_TRIGGER_L, s.haybox ? 2 : s.axes[6].present ? 6 : -1, present);
  const int trig_r = hid_role_axis(roles, HID_ROLE_TRIGGER_R, s.haybox ? 5 : rxry && s.axes[2].present && !s.sim_triggers ? 2 : -1, present);
  if (trig_l >= 0 && read_axis(trig_l, value)) pad.trig_l = trigger(trig_l, value);
  if (trig_r >= 0 && read_axis(trig_r, value)) pad.trig_r = trigger(trig_r, value);
  }
  // Every axis as a stick value, whatever it is used for: what choosing one by moving it compares.
  for (int i = 0; i < 7; ++i)
    if (read_axis(i, value))
      s.dec[i] = hid_stick_value(s.axes[i].logical_min, s.axes[i].logical_max, s.axes[i].bits, (unsigned long)value,
                                 s.stick_enc[i] < 0 ? kStickDeclared : s.stick_enc[i], false);

  uint32_t mask = 0;
  {
    // Every button pressed anywhere in the report, whatever collection it sits in. Asking for the
    // Button page in link collection 0 only missed buttons a device keeps in a nested collection,
    // and a vJoy device fed by a box controller can be laid out that way: its axes read (they are
    // top level) while not one of its buttons ever registered, so a rebind waited forever.
    ULONG usage_count = HidP_MaxUsageListLength(HidP_Input, 0, preparsed_of(s));
    if (usage_count) {
      std::vector<USAGE_AND_PAGE> pressed(usage_count);
      if (HidP_GetUsagesEx(HidP_Input, 0, pressed.data(), &usage_count, preparsed_of(s), (PCHAR)report, size) == HIDP_STATUS_SUCCESS) {
        for (ULONG i = 0; i < usage_count; ++i) {
          if (pressed[i].UsagePage != kUsagePageButton) continue;
          const int bit = (int)pressed[i].Usage - (int)s.button_min;
          if (bit >= 0 && bit < 32) mask |= 1u << bit;
        }
      }
    }
  }

  // The hat switch, as four buttons above the device's own (HID_HAT_*): box controllers on HayBox
  // report their D-pad there. Eight positions from the top clockwise; anything else is centred.
  if (s.axes[7].present && read_axis(7, value)) {
    const LONG v = value - s.axes[7].logical_min;
    if (v >= 0 && v < 8) {
      if (v == 7 || v <= 1) mask |= host::HID_HAT_UP;
      if (v >= 1 && v <= 3) mask |= host::HID_HAT_RIGHT;
      if (v >= 3 && v <= 5) mask |= host::HID_HAT_DOWN;
      if (v >= 5) mask |= host::HID_HAT_LEFT;
    }
  }

  if (s.log_reports) {
    --s.log_reports;
    host::log("hid pad %d: report buttons %08X, stick %d,%d c-stick %d,%d, triggers %u/%u",
              index + 1, mask, pad.stick_x, pad.stick_y, pad.sub_x, pad.sub_y, pad.trig_l, pad.trig_r);
  }
  else if (s.log_changes && (mask != s.buttons || (pad.trig_l > 20) != (s.pad.trig_l > 20) || (pad.trig_r > 20) != (s.pad.trig_r > 20))) {
    --s.log_changes;
    host::log("hid pad %d: buttons %08X, triggers %u/%u", index + 1, mask, pad.trig_l, pad.trig_r);
  }
  // Each new trigger value (a step of 4 or more from the last one logged; a box jumps straight there).
  if (!s.log_reports && s.log_triggers && (std::abs((int)pad.trig_l - (int)s.logged_l) >= 4 ||
                                          std::abs((int)pad.trig_r - (int)s.logged_r) >= 4)) {
    --s.log_triggers;
    s.logged_l = pad.trig_l; s.logged_r = pad.trig_r;
    host::log("hid pad %d: triggers now %u/%u (buttons %08X)", index + 1, pad.trig_l, pad.trig_r, mask);
  }
  s.pad = pad;
  s.buttons = mask;
  s.fresh = true;
  return true;
}

uint32_t hidpad_poll(PadState out[kHidPadSlots], uint32_t buttons[kHidPadSlots]) {
  std::lock_guard<std::mutex> lock(g_mutex);
  uint32_t present = 0;
  for (int i = 0; i < kHidPadSlots; ++i) {
    if (!g_slots[i].ready) { out[i].err = -1; buttons[i] = 0; continue; }
    out[i] = g_slots[i].pad;
    buttons[i] = g_slots[i].buttons;
    present |= 1u << i;
  }
  return present;
}

bool hidpad_haybox_dinput(int slot) {
  if (slot < 0 || slot >= kHidPadSlots) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_slots[slot].ready && g_slots[slot].haybox;
}

bool hidpad_frame1_dinput(int slot) {
  if (slot < 0 || slot >= kHidPadSlots) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_slots[slot].ready && g_slots[slot].frame1;
}

std::string hidpad_name(int slot) {
  if (controller_relay::active()) {
    controller_relay::Sample s;
    if (controller_relay::sample(CaptureDevice::HidPad,slot,s)) return s.name;
    // YouGame: HID pads stay with this reader until the app sends raw HID ones (window.cpp
    // relay_hid_poll). A name is only asked for a slot the reader in charge says is connected.
  }
  if (slot < 0 || slot >= kHidPadSlots) return {};
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_slots[slot].ready ? g_slots[slot].name : std::string();
}

void hidpad_axis_capture_begin(int slot) {
  if (slot < 0 || slot >= kHidPadSlots) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  Slot& s = g_slots[slot];
  std::memcpy(s.base_dec, s.dec, sizeof s.dec);
  std::memcpy(s.base_val, s.val, sizeof s.val);
}

int hidpad_axis_capture_poll(int slot) {
  if (slot < 0 || slot >= kHidPadSlots) return -1;
  std::lock_guard<std::mutex> lock(g_mutex);
  const Slot& s = g_slots[slot];
  if (!s.ready || s.frame1) return -1;
  unsigned present = 0;
  for (int i = 0; i < 7; ++i) if (s.axes[i].present) present |= 1u << i;
  return hid_moved_axis(s.base_dec, s.dec, present);
}

void hidpad_assign_axis(int slot, int role, int axis) {
  if (slot < 0 || slot >= kHidPadSlots || slot >= 4 || role < 0 || role >= HID_ROLE_COUNT || axis < 0 || axis >= 7) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  Slot& s = g_slots[slot];
  if (!s.ready || !s.axes[axis].present) return;
  for (uint8_t& other : g_hid_axis_role[slot]) if (other == axis) other = kHidAxisAuto;
  g_hid_axis_role[slot][role] = (uint8_t)axis;
  const Axis& ax = s.axes[axis];
  if (role >= HID_ROLE_TRIGGER_L) {
    // Where it sat before the press, not where the first report of the session happened to find it.
    s.rest[axis] = hid_trigger_rest(ax.logical_min, ax.logical_max, s.base_val[axis]);
    host::log("hid pad %d: %s trigger set to axis %s", slot + 1, role == HID_ROLE_TRIGGER_L ? "L" : "R", axis_name(ax.usage));
    return;
  }
  // The push that chose it was right or up. Read it as the reader will, and turn it round if that comes out
  // left or down.
  const int8_t enc = s.stick_enc[axis] < 0 ? kStickDeclared : s.stick_enc[axis];
  bool flip = role == HID_ROLE_STICK_Y || role == HID_ROLE_CSTICK_Y;
  if (enc == kStickTwos && ax.logical_min >= 0) flip = false;
  const int8_t read = hid_stick_value(ax.logical_min, ax.logical_max, ax.bits, (unsigned long)s.val[axis], enc, flip);
  const uint8_t bit = (uint8_t)(1u << role);   // HID_INVERT_* are in the roles' order
  g_hid_stick_invert[slot] = (uint8_t)((g_hid_stick_invert[slot] & ~bit) | (read < 0 ? bit : 0));
  host::log("hid pad %d: role %d set to axis %s%s", slot + 1, role, axis_name(ax.usage), read < 0 ? ", inverted" : "");
}

const char* hidpad_axis_name(int slot, int axis) {
  if (axis < 0 || axis >= 8) return "";
  std::lock_guard<std::mutex> lock(g_mutex);
  // The field that is really there (a box's Rudder in Z's place), else the usual name of that place.
  if (slot >= 0 && slot < kHidPadSlots && g_slots[slot].ready && g_slots[slot].axes[axis].present) return axis_name(g_slots[slot].axes[axis].usage);
  return axis_name(kAxisOrder[axis]);
}

HidPadAxes hidpad_axes(int slot) {
  HidPadAxes out{};
  if (slot < 0 || slot >= kHidPadSlots) return out;
  std::lock_guard<std::mutex> lock(g_mutex);
  const Slot& s = g_slots[slot];
  if (!s.ready) return out;
  for (int i = 0; i < 8; ++i) {
    if (!s.axes[i].present) continue;
    out.value[out.count] = s.raw[i];
    out.name[out.count] = axis_name(s.axes[i].usage);
    ++out.count;
  }
  return out;
}

}  // namespace host
