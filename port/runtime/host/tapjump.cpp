// Tap jump off. See tapjump.h for why this is done to the pad and not to the game.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "tapjump.h"
#include "host.h"
#include "input_bindings.h"
#include <atomic>
#include <cstring>

namespace tapjump {
namespace {

// p_ftCommonData is a pointer (GALE01_symbols.txt, .sbss:0x804D6554) to the table of the constants
// every fighter shares; tap_jump_threshold is the float at +0x70 of it. HSD_PadLibData (.bss:
// 0x804C1F78) holds scale_stick at +0x26, the divisor that turns the byte in PADStatus into the
// -1..1 value the fighter code compares against. Both are read from the running game rather than
// written down here, because a number copied into this file is a number that can quietly stop
// being true.
constexpr uint32_t kFtCommonDataPtr = 0x804D6554;
constexpr uint32_t kTapJumpThresholdOffset = 0x70;
constexpr uint32_t kPadLibData = 0x804C1F78;
constexpr uint32_t kScaleStickOffset = 0x26;

std::atomic<bool> g_enabled{false};
std::atomic<int> g_limit{0};            // highest stick_y that cannot tap jump, in PADStatus units
std::atomic<float> g_threshold{0.0f};   // the same thing as the game states it

bool read_limit() {
  const uint32_t table = host::rd32(kFtCommonDataPtr);
  if (table < 0x80000000u || table >= 0x81800000u) return false;
  const uint32_t bits = host::rd32(table + kTapJumpThresholdOffset);
  float threshold;
  std::memcpy(&threshold, &bits, sizeof threshold);
  const int scale = (int8_t)host::rd8(kPadLibData + kScaleStickOffset);
  if (!(threshold > 0.05f && threshold < 1.5f) || scale < 20 || scale > 127) return false;

  // The game's test is >=, so one unit below the threshold is the highest value that cannot jump.
  // Rounding up before subtracting keeps this on the safe side of a threshold that is not a whole
  // number of units: 0.6625 * 80 is exactly 53, but nothing guarantees that stays true.
  const int raw = (int)(threshold * (float)scale + 0.999f) - 1;
  g_threshold.store(threshold, std::memory_order_relaxed);
  g_limit.store(raw < 1 ? 1 : raw, std::memory_order_relaxed);
  return true;
}

}  // namespace

void set_enabled(bool on) {
  if (on == g_enabled.exchange(on, std::memory_order_relaxed)) return;
  host::log("tap jump off: %s", on ? "on" : "off");
}

bool enabled() { return g_enabled.load(std::memory_order_relaxed); }
float threshold_normalised() { return g_threshold.load(std::memory_order_relaxed); }
int threshold_raw() { return g_limit.load(std::memory_order_relaxed); }

void apply(host::PadState pads[4]) {
  if (!g_enabled.load(std::memory_order_relaxed)) return;
  // The tables only exist once the game has loaded them, and they do not move afterwards, so this
  // keeps trying until it gets a sane answer and then stops.
  int limit = g_limit.load(std::memory_order_relaxed);
  if (limit <= 0) {
    if (!read_limit()) return;
    limit = g_limit.load(std::memory_order_relaxed);
  }
  const uint16_t a_bit = host::kActionPadBit[(size_t)host::BindAction::A];
  for (int port = 0; port < 4; ++port) {
    host::PadState& pad = pads[port];
    if (pad.err != 0) continue;
    // A pressed this frame is the one case the game can tell apart from a tap jump, so it is the one
    // case the stick is left alone: that is what keeps up-smash working.
    if (pad.button & a_bit) continue;
    if (pad.stick_y > limit) pad.stick_y = (int8_t)limit;
  }
}

}  // namespace tapjump
