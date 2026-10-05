// The Source Port without the Slippi layer (MELEE_NO_SLIPPI): the few host options that layer used
// to carry, as plain settings, and the entry points of the files this build leaves out. The network
// state itself is in runtime/host/netplay_state.h; a session sets it.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "netplay_state.h"
#include <algorithm>
#include "gecko_data.h"
#include "hackpack_ai.h"
#include "host.h"

namespace host::netplay {
namespace {
std::atomic<int> g_music_volume{100};
std::atomic<int> g_widescreen_request{-1};
}  // namespace

int music_volume() { return g_music_volume.load(std::memory_order_relaxed); }
void set_music_volume(int percent) { g_music_volume.store(std::clamp(percent, 0, 100), std::memory_order_relaxed); }

void request_widescreen(bool on) { g_widescreen_request.store(on ? 1 : 0); }
bool widescreen() { return gecko::option_widescreen; }
// The native game carries Fountain of Dreams as C and decides its reflections once, at boot; there
// is no code table to switch here.
void request_fod_reflections(bool) {}

// Every retrace, on the simulation thread: the game reads the widescreen flag through its options
// word as each camera loads, so the flag only changes here, between two frames.
void poll_options() {
  const int wide = g_widescreen_request.exchange(-1);
  if (wide >= 0 && (wide != 0) != gecko::option_widescreen) {
    gecko::option_widescreen = wide != 0;
    host::log("display: widescreen 16:9 %s (from the next screen)", gecko::option_widescreen ? "on" : "off");
  }
}
}  // namespace host::netplay

// "20XX CPUs" on the Static Recomp loads the pack's own AI block into the translated game
// (hackpack_ai.cpp). The Source Port plays its native version, so the loader is not in this build:
// the panel is told there is no block, as it is when no pack disc is present.
namespace host::hackpack_ai {
void boot() {}
void reload() {}
Status status() { Status s; s.looked = true; return s; }
bool effective() { return false; }
}  // namespace host::hackpack_ai
