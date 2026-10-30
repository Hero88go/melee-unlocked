// Optional app-owned controllers keep the same families and bindings as native devices.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "host.h"
#include "input_bindings.h"
#include <atomic>
#include <array>
#include <string>
namespace host::controller_relay {
struct Sample {
  CaptureDevice family = CaptureDevice::None;
  int index = 0; // Stable index within the family, not its position in a mixed app list.
  uint32_t buttons = 0; // Original family mask, including HID hat/modifier bits.
  int16_t axes[4]{}; // Standard full-range axes; Y increases upwards.
  uint8_t left = 0, right = 0;
  std::string name;
};
struct Snapshot { std::array<Sample, 4> devices{}; };
// nullptr restores standalone ownership. An active provider owns these families even when
// its snapshot is empty: disconnection must not fall through to duplicate physical devices.
using Read = Snapshot (*)();
using Rumble = void (*)(CaptureDevice, int, bool); // Must enqueue; never wait for app/USB I/O.
inline std::atomic<Rumble> rumble_writer{nullptr};
inline void install_rumble(Rumble write) { rumble_writer.store(write,std::memory_order_release); }
inline std::atomic<Read> reader{nullptr};
inline void install(Read read) { reader.store(read, std::memory_order_release); }
inline bool active() { return reader.load(std::memory_order_acquire) != nullptr; }
inline bool sample(CaptureDevice family, int index, Sample& out) {
  const auto read = reader.load(std::memory_order_acquire);
  if (!read || index < 0 || index >= 4) return false;
  for (const auto& device : read().devices) if (device.family == family && device.index == index) {
    out = device; return true;
  }
  return false;
}
inline CaptureDevice family_of(DeviceKind kind) {
  switch(kind) {
    case DeviceKind::XInputPad:return CaptureDevice::XInputPad;
    case DeviceKind::DS4Pad:return CaptureDevice::DS4Pad;
    case DeviceKind::SwitchPro:return CaptureDevice::SwitchPro;
    case DeviceKind::HidPad:return CaptureDevice::HidPad;
    default:return CaptureDevice::None;
  }
}
inline bool rumble(DeviceKind kind,int index,bool on) {
  if (!active() || kind==DeviceKind::GCAdapter || kind==DeviceKind::Keyboard) return false;
  if (const auto write=rumble_writer.load(std::memory_order_acquire)) write(family_of(kind),index,on);
  return true; // App owns output too, even when it does not implement rumble yet.
}
inline PadState raw_pad(const Sample& s) {
  PadState p{}; p.err = 0;
  p.stick_x = xinput_axis(s.axes[0]); p.stick_y = xinput_axis(s.axes[1]);
  p.sub_x = xinput_axis(s.axes[2]); p.sub_y = xinput_axis(s.axes[3]);
  p.trig_l = s.left; p.trig_r = s.right;
  return p;
}
// App standard layouts use XInput button positions. Convert before applying PS/Switch
// profiles; their native masks are deliberately different. Raw HID keeps its original mask.
inline uint16_t native_buttons(uint16_t standard, CaptureDevice family, uint8_t left=0, uint8_t right=0) {
  if (family == CaptureDevice::XInputPad) return standard;
  uint16_t result = 0;
  const uint16_t source[] = {0x1,0x2,0x4,0x8,0x4000,0x1000,0x2000,0x8000,0x100,0x200,0x20,0x10,0x40,0x80};
  const uint16_t ps[] = {1,2,4,8,16,32,64,128,256,512,4096,8192,16384,32768};
  const uint16_t sw[] = {1,2,4,8,64,16,32,128,256,512,4096,8192,16384,32768};
  const auto* target = family == CaptureDevice::SwitchPro ? sw : ps;
  for (unsigned i=0;i<14;++i) if (standard & source[i]) result |= target[i];
  if (left > (family==CaptureDevice::DS4Pad ? kPlayStationTriggerPress : 127)) result |= 0x400;
  if (right > (family==CaptureDevice::DS4Pad ? kPlayStationTriggerPress : 127)) result |= 0x800;
  return result;
}
inline uint32_t poll(CaptureDevice family, PadState out[4], uint32_t buttons[4]) {
  uint32_t mask = 0;
  const auto read = reader.load(std::memory_order_acquire);
  if (!read) return 0;
  for (auto& s : read().devices) if (s.family == family && s.index >= 0 && s.index < 4) {
    out[s.index] = raw_pad(s); buttons[s.index] = s.buttons; mask |= 1u << s.index;
  }
  return mask;
}
} // namespace host::controller_relay
