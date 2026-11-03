// The settings overlay's device tabs and complete profile binding snapshots.
// Kept independent of the UI and OS headers so all platforms test the same accessors.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../host/controller_profiles.h"

namespace gx {

constexpr int kDeviceTabs = 21;

inline uint32_t binding_get(host::CaptureDevice kind, int index, int action) {
  switch (kind) {
    case host::CaptureDevice::Keyboard:  return (uint32_t)host::g_key_bindings.vk[action];
    case host::CaptureDevice::XInputPad: return host::g_pad_bindings[index].mask[action];
    case host::CaptureDevice::DS4Pad:    return host::g_ds4_bindings[index].mask[action];
    case host::CaptureDevice::SwitchPro: return host::g_swpro_bindings[index].mask[action];
    case host::CaptureDevice::HidPad:    return host::g_hid_bindings[index].mask[action];
    case host::CaptureDevice::GCAdapter: return host::g_gc_bindings[index].mask[action];
    default: return 0;
  }
}

inline void binding_set(host::CaptureDevice kind, int index, int action, uint32_t value) {
  switch (kind) {
    case host::CaptureDevice::Keyboard:  host::g_key_bindings.vk[action] = (int)value; break;
    case host::CaptureDevice::XInputPad: host::g_pad_bindings[index].mask[action] = (unsigned short)value; break;
    case host::CaptureDevice::DS4Pad:    host::g_ds4_bindings[index].mask[action] = (unsigned short)value; break;
    case host::CaptureDevice::SwitchPro: host::g_swpro_bindings[index].mask[action] = (unsigned short)value; break;
    case host::CaptureDevice::HidPad:    host::g_hid_bindings[index].mask[action] = value; break;
    case host::CaptureDevice::GCAdapter: host::g_gc_bindings[index].mask[action] = (unsigned short)value; break;
    default: break;
  }
}

inline host::CaptureDevice tab_kind_of(int tab, int* index) {
  *index = 0;
  if (tab == 0) return host::CaptureDevice::Keyboard;
  if (tab <= 4) { *index = tab - 1; return host::CaptureDevice::XInputPad; }
  if (tab <= 8) { *index = tab - 5; return host::CaptureDevice::DS4Pad; }
  if (tab <= 12) { *index = tab - 9; return host::CaptureDevice::GCAdapter; }
  if (tab <= 16) { *index = tab - 13; return host::CaptureDevice::SwitchPro; }
  *index = tab - 17; return host::CaptureDevice::HidPad;
}
inline host::ProfileBindings bindings_of(int tab) {
  int index; const host::CaptureDevice kind = tab_kind_of(tab, &index);
  host::ProfileBindings b{};
  for (int i = 0; i < (int)host::BindAction::Count; ++i) b[i] = binding_get(kind, index, i);
  return b;
}
inline void set_bindings_of(int tab, const host::ProfileBindings& b) {
  int index; const host::CaptureDevice kind = tab_kind_of(tab, &index);
  for (int i = 0; i < (int)host::BindAction::Count; ++i) binding_set(kind, index, i, b[i]);
}
}  // namespace gx
