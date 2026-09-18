// Generic HID gamepads. See hid_pad.h for why this path exists and what it deliberately does not do.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hid_pad.h"
#include "host.h"
#include "input_bindings.h"
#define NOMINMAX
#include <windows.h>
#include <hidsdi.h>
#include <algorithm>
#include <cmath>
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

const char* axis_name(USAGE usage) {
  switch (usage) {
    case kUsageX: return "X"; case kUsageY: return "Y"; case kUsageZ: return "Z";
    case kUsageRx: return "Rx"; case kUsageRy: return "Ry"; case kUsageRz: return "Rz";
    case kUsageSlider: return "Slider"; case kUsageHat: return "Hat";
    default: return "?";
  }
}

struct Axis {
  USAGE usage = 0;
  LONG logical_min = 0, logical_max = 0;
  bool present = false;
};

struct Slot {
  HANDLE device = nullptr;
  std::vector<uint8_t> preparsed;      // RIDI_PREPARSEDDATA blob, owned here
  std::array<Axis, 8> axes{};          // in the order of kAxisOrder
  int axis_count = 0;
  USAGE button_min = 0, button_max = 0;
  std::string name;
  bool ready = false;
  // Latest decoded state.
  PadState pad{};
  uint32_t buttons = 0;
  int log_reports = 0;   // first few reports after a device appears, for diagnosing a pad that will not bind
  int32_t raw[8]{};
  bool fresh = false;
  // Where each axis rests when nothing touches it, decided from the first report (see hid_trigger_rest).
  // Only the axes read as triggers use it.
  int8_t rest[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
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
  if (hid.dwVendorId == 0x057E || hid.dwVendorId == 0x0079) {}
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
      ++s.axis_count;
      break;
    }
  }

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
  // Something with no axes and no buttons is not a pad, whatever it says it is.
  return s.axis_count > 0 || s.button_max >= s.button_min;
}

int slot_for(HANDLE device) {
  for (int i = 0; i < kHidPadSlots; ++i) if (g_slots[i].device == device) return i;
  for (int i = 0; i < kHidPadSlots; ++i) if (!g_slots[i].device) return i;
  return -1;
}

// Scales a declared axis range onto the signed range the GameCube pad uses, from the centre out,
// the way Dolphin does: centre -> 0, either end -> 127. No deadzone: a box reports exact values and
// rounding them toward centre is precisely the wrong thing to do. Scaling across the whole span
// instead (t * 255 - 128) put b0xx-ahk's full push (vJoy 16384 + 10271) at 79 rather than the 80
// Dolphin gives it, so a B0XX on vJoy never reached the edge of the stick.
int8_t to_signed(const Axis& a, LONG raw) {
  const double half = ((double)a.logical_max - (double)a.logical_min) / 2.0;
  if (half <= 0) return 0;
  const double mid = ((double)a.logical_max + (double)a.logical_min) / 2.0;
  const int scaled = (int)std::lround(((double)raw - mid) / half * 127.0);
  return (int8_t)std::clamp(scaled, -128, 127);
}

}  // namespace

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
  return (uint8_t)std::clamp((int)std::lround(t * 255.0), 0, 255);
}

bool hidpad_raw_input(void* device_handle, const uint8_t* report, uint32_t size, uint32_t count) {
  HANDLE device = (HANDLE)device_handle;
  if (!report || !size || !count) return false;

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
    if (claimed_elsewhere(info.hid, path)) return false;
    if (!describe(s, device)) return false;
    s.device = device;
    s.name = friendly_name(path, info.hid);
    s.ready = true;
    // Everything needed to tell, from a log alone, why a pad does not bind: how the device
    // described itself and what the first report actually contained. A box controller reports its
    // buttons in ways that differ from a normal pad, and without this the only symptom is a rebind
    // that waits forever.
    HIDP_CAPS caps{};
    const unsigned collections = HidP_GetCaps(preparsed_of(s), &caps) == HIDP_STATUS_SUCCESS ? caps.NumberLinkCollectionNodes : 0;
    host::log("hid pad %d: %s (%d axes, buttons %u..%u, %u collections, report %u bytes)", index + 1, s.name.c_str(),
              s.axis_count, (unsigned)s.button_min, (unsigned)s.button_max, collections, caps.InputReportByteLength);
    s.log_reports = 3;
  }
  if (!s.ready) return false;

  PadState pad{};
  pad.err = 0;
  LONG value = 0;
  auto read_axis = [&](int i, LONG& out) {
    if (!s.axes[i].present) return false;
    ULONG raw = 0;
    if (HidP_GetUsageValue(HidP_Input, kUsagePageGeneric, 0, s.axes[i].usage, &raw, preparsed_of(s),
                           (PCHAR)report, size) != HIDP_STATUS_SUCCESS)
      return false;
    out = (LONG)raw;
    s.raw[i] = (int32_t)raw;
    return true;
  };

  // X/Y are the control stick. Which pair carries the C-stick differs between devices, so both of
  // the common choices are accepted: Rx/Ry when present, otherwise Z/Rz, which is what most cheap
  // pads and most vJoy feeder configurations use.
  if (read_axis(0, value)) pad.stick_x = to_signed(s.axes[0], value);
  if (read_axis(1, value)) pad.stick_y = (int8_t)-std::max<int>(-127, to_signed(s.axes[1], value));
  if (s.axes[3].present && s.axes[4].present) {
    if (read_axis(3, value)) pad.sub_x = to_signed(s.axes[3], value);
    if (read_axis(4, value)) pad.sub_y = (int8_t)-std::max<int>(-127, to_signed(s.axes[4], value));
  } else {
    if (read_axis(2, value)) pad.sub_x = to_signed(s.axes[2], value);
    if (read_axis(5, value)) pad.sub_y = (int8_t)-std::max<int>(-127, to_signed(s.axes[5], value));
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
    return hid_trigger_value(s.axes[i].logical_min, s.axes[i].logical_max, raw, s.rest[i]);
  };
  if (s.axes[6].present && read_axis(6, value)) pad.trig_l = trigger(6, value);
  if (s.axes[3].present && s.axes[4].present && s.axes[2].present && read_axis(2, value))
    pad.trig_r = trigger(2, value);

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

std::string hidpad_name(int slot) {
  if (slot < 0 || slot >= kHidPadSlots) return {};
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_slots[slot].ready ? g_slots[slot].name : std::string();
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
