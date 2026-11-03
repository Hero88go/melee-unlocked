// Exercise the overlay's actual snapshot/apply helpers without a window or input devices.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings_profile_bindings.h"
#include <cstdio>

namespace host {
KeyBindings g_key_bindings{};
std::array<PadBindings, 4> g_pad_bindings{}, g_ds4_bindings{}, g_swpro_bindings{};
std::array<GCBindings, 4> g_gc_bindings{};
std::array<HidBindings, 4> g_hid_bindings{};
}

static_assert(host::kProfileActions == static_cast<int>(host::BindAction::Count));
static_assert(host::kProfileActions == 27, "Update coverage when actions are appended");

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

template <class T, size_t N>
void check_tab(int tab, T (&raw)[N], uint8_t (&levels)[N], uint32_t high_bit) {
  static_assert(N == static_cast<size_t>(host::BindAction::Count));
  host::ProfileBindings expected{};
  for (size_t i = 0; i < N; ++i) {
    expected[i] = high_bit | static_cast<uint32_t>(tab * N + i + 1);
    raw[i] = static_cast<T>(expected[i]);
    levels[i] = 123;
  }
  // Reads must include all five appended actions, for every controller slot and key/mask width.
  CHECK(gx::bindings_of(tab) == expected);

  for (size_t i = 0; i < N; ++i) expected[i] = i == 0 ? 0 : high_bit | static_cast<uint32_t>(2000 + tab * N + i);
  gx::set_bindings_of(tab, expected);
  for (size_t i = 0; i < N; ++i) {
    CHECK(static_cast<uint32_t>(raw[i]) == expected[i]);
    CHECK(levels[i] == 123);  // profile snapshots contain bindings, never analog levels
  }
  CHECK(gx::bindings_of(tab) == expected);
}
}

int main() {
  check_tab(0, host::g_key_bindings.vk, host::g_key_bindings.level, 0x40000000u);
  for (int i = 0; i < 4; ++i) {
    check_tab(1 + i, host::g_pad_bindings[i].mask, host::g_pad_bindings[i].level, 0x8000u);
    check_tab(5 + i, host::g_ds4_bindings[i].mask, host::g_ds4_bindings[i].level, 0x8000u);
    check_tab(9 + i, host::g_gc_bindings[i].mask, host::g_gc_bindings[i].level, 0x8000u);
    check_tab(13 + i, host::g_swpro_bindings[i].mask, host::g_swpro_bindings[i].level, 0x8000u);
    check_tab(17 + i, host::g_hid_bindings[i].mask, host::g_hid_bindings[i].level, 0x80000000u);
  }
  std::printf("settings profile bindings: %d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
