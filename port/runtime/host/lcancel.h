// L-cancel helpers: display-only red/green feedback, and an automatic L-cancel that works
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
#include "../abi/mu_lcancel_view.h"

namespace lcancel {

// ---- settings (owned here; the panel and the command line both write through these) ----
void set_indicator(bool on);
// Renderer feedback on either engine, independent of TE: missed, success or both.
void set_flash_mode(int mode);
int flash_mode();
void set_automatic(bool on);
bool indicator_enabled();
bool automatic_enabled();
// Diagnostics: per-frame CSV of every observed local fighter. Empty = off.
void set_log_path(const std::string& path);

// Called from HLE(PADRead) with the freshly polled pads, before the guest sees them. Observes the
// local fighters, raises the analog L trigger when the automatic press is enabled and allowed, and
// flashes red on a missed L-cancel or green on success, according to the chosen feedback mode.
void apply(host::PadState pads[4]);
// The native game keeps its fighters in its own layout, so it hands the values this helper reads
// over itself (MuGameApi.lcancel_view). Unset, they are read from guest memory (recompiled build).
void set_native_view(void (*fill)(MuLcancelView* out));

// The indicator itself needs nothing from the UI: feedback tints the fighter through
// gx::set_player_tint, which is a renderer-side colour and never a write into the game.

// ---- gating, for the panel and the character-select notice ----
// nullptr when the automatic press is allowed right now (offline, or an online Direct session);
// otherwise the name of the online mode that suppresses it ("Unranked", "Teams", "Party").
const char* auto_suppressed_mode();
// True while an online session exists and its match has not started: matchmaking and the online
// character select screen. The notice about the suppressed setting belongs here.
bool online_session_pending();

}  // namespace lcancel
