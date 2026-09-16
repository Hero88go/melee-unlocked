// L-cancel helpers: a display-only "you missed it" indicator, and an automatic L-cancel that works
// by injecting an analog trigger press into the local pad before the game reads it.
//
// Both are off by default and neither one patches the simulation. The automatic press is a real
// input: it goes through HLE(PADRead) like any other button, so Slippi transmits it and both
// clients compute the same landing lag from it. See lcancel.cpp for why it is an analog press and
// not a digital L bit.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include "host.h"

namespace lcancel {

// ---- settings (owned here; the panel and the command line both write through these) ----
void set_indicator(bool on);
void set_automatic(bool on);
bool indicator_enabled();
bool automatic_enabled();
// Diagnostics: per-frame CSV of every observed local fighter. Empty = off.
void set_log_path(const std::string& path);

// Called from HLE(PADRead) with the freshly polled pads, before the guest sees them. Observes the
// local fighters, raises the analog L trigger when the automatic press is enabled and allowed, and
// records a missed L-cancel for the indicator.
void apply(host::PadState pads[4]);

// ---- indicator, read by the renderer thread ----
struct Flash {
  bool active = false;       // something to draw
  int port = 0;              // 1..4
  int frames_since_press = 0;  // the fighter's "frames since any trigger press" at the landing frame
  float alpha = 0.f;         // 1 at the landing frame, fading to 0
};
Flash flash();

// ---- gating, for the panel and the character-select notice ----
// nullptr when the automatic press is allowed right now (offline, or an online Direct session);
// otherwise the name of the online mode that suppresses it ("Ranked", "Unranked", "Teams", "Party").
const char* auto_suppressed_mode();
// True while an online session exists and its match has not started: matchmaking and the online
// character select screen. The notice about the suppressed setting belongs here.
bool online_session_pending();

}  // namespace lcancel
