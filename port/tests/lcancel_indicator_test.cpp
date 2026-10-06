// Exercise the production renderer feedback through both fighter-view paths.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "lcancel.h"
#include "mu_lcancel_flash.h"
#include <cstdio>
#include <cstring>
#include <map>

namespace {
uint32_t tick = 1;
MuLcancelView view{};
std::map<uint32_t, uint8_t> memory;
float red = 1, green = 1, amount = 0;
int network_mode = -1;
int local_slot = 0;
void native_view(MuLcancelView* out) { *out = view; }
void put32(uint32_t address, uint32_t value) {
  for (int i = 0; i < 4; ++i) memory[address + i] = uint8_t(value >> (24 - i * 8));
}
}
namespace host {
uint32_t retrace_count() { return tick; }
uint32_t rd32(uint32_t address) {
  uint32_t value = 0;
  for (int i = 0; i < 4; ++i) value = (value << 8) | memory[address + i];
  return value;
}
uint8_t rd8(uint32_t address) { return memory[address]; }
void log(const char*, ...) {}
}
namespace slippi::online {
int session_mode() { return network_mode; }
int local_player_index() { return local_slot; }
bool in_online_menus() { return false; }
}
namespace gx {
void set_owner_tracking(bool) {}
void clear_player_tints() { amount = 0; }
void set_player_tint(int slot, float r, float g, float, float a) {
  if (slot == 0) { red = r; green = g; amount = a; }
}
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
  constexpr uint32_t fp = 0x80200000u, gobj = 0x80203000u;
  host::PadState pads[4]{};
  pads[0].button = 0x0100; pads[0].trig_l = 12;
  host::PadState original[4]; std::memcpy(original, pads, sizeof pads);
  view.have_common = 1; view.lcancel_window = 7;
  view.port[0].present = 1; view.port[0].slot = 0;
  // Static Recomp's ordinary player and Fighter pointers, in big-endian RAM.
  put32(0x80453080u, 2); memory[0x80453088u] = 0; memory[0x804530C6u] = 0;
  put32(0x80453130u, gobj); put32(gobj + 0x2C, fp); put32(fp, gobj);
  for (bool native : {true, false}) {
    lcancel::set_native_view(native ? native_view : nullptr);
    for (int mode : {MU_LCFLASH_OFF, MU_LCFLASH_MU_MISSED, MU_LCFLASH_MU_SUCCESS, MU_LCFLASH_MU_BOTH}) {
      for (uint32_t since : {6u, 7u}) {
        lcancel::set_flash_mode(MU_LCFLASH_OFF); ++tick; lcancel::apply(pads);
        lcancel::set_flash_mode(mode);
        view.port[0].motion_id = 65; view.port[0].ground_or_air = 1;
        put32(fp + 0x10, 65); put32(fp + 0xE0, 1);
        ++tick; lcancel::apply(pads);
        view.port[0].motion_id = 70; view.port[0].ground_or_air = 0;
        view.port[0].frames_since_trigger = since;
        put32(fp + 0x10, 70); put32(fp + 0xE0, 0); memory[fp + 0x67F] = uint8_t(since);
        ++tick; lcancel::apply(pads);
        const bool success = since < 7;
        const bool expected = mu_lcancel_renderer_reports(mode, success);
        CHECK((amount > 0) == expected);
        if (expected) CHECK(success ? green > .9f && red < .2f : red > .9f && green < .1f);
        CHECK(std::memcmp(original, pads, sizeof pads) == 0);
        // Repeated reads neither restart the flash nor mutate the controller.
        lcancel::apply(pads); CHECK(std::memcmp(original, pads, sizeof pads) == 0);
        tick += 10; lcancel::apply(pads); CHECK(amount == 0);
      }
    }
  }
  // Online feedback remains local: a remote fighter's success raises no tint.
  lcancel::set_native_view(native_view); lcancel::set_flash_mode(MU_LCFLASH_MU_BOTH);
  network_mode = 3; local_slot = 1; view.port[0].motion_id = 65;
  ++tick; lcancel::apply(pads); view.port[0].motion_id = 70; view.port[0].frames_since_trigger = 0;
  ++tick; lcancel::apply(pads); CHECK(amount == 0);
  std::puts("native and Static success/missed feedback, boundary, expiry and unchanged inputs passed");
}
