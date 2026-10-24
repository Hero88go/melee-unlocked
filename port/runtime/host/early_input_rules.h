// Early inputs: the rules shared by every transport. Which controller changes are worth telling the
// other player about before the next frame, the pad form they travel in, and the small buffer the
// receiving side predicts from. Header only, no host services: the unit test includes it alone.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace host::early_input {

// The pad as it travels: Slippi's eight data bytes (buttons big-endian, main stick, C-stick, the
// two analog triggers). The four bytes the game adds (analog A/B, error, padding) are zero online.
constexpr int kPadBytes = 8;
// Entries kept per remote player. A rollback window is seven frames; more early pads than that for
// frames still to confirm would mean the confirmed pads have stopped arriving.
constexpr int kMaxEntries = 8;
// What travels, whichever network carries it: the pad frame the change will first appear in (big
// endian), the sender's in-game port, the pad.
constexpr int kMessageBytes = 4 + 1 + kPadBytes;

// Resting sticks read as exactly zero, as the online pad code does before a pad is sent, so stick
// noise around neutral never counts as a change.
inline void clamp_rest(uint8_t pad[kPadBytes]) {
  const int8_t sx = (int8_t)pad[2], sy = (int8_t)pad[3], cx = (int8_t)pad[4], cy = (int8_t)pad[5];
  if (sx >= -2 && sx <= 2 && sy >= -2 && sy <= 2) pad[2] = pad[3] = 0;
  if (cx >= -2 && cx <= 2 && cy >= -2 && cy <= 2) pad[4] = pad[5] = 0;
}

// The lines a stick axis crosses that the game acts on, in the controller's own units (the game
// divides by 80): walk and tilt at 0.2875 (23), dash and smash at 0.8 (64), tap jump at 0.6625
// (53), crouch at 0.7 (56). A change inside one zone is noise or a slow drift the next frame's pad
// carries anyway; a change across a line is a press the other side will mispredict without.
// Lines are sorted; a negative line is crossed at or below it, a positive one at or above it.
struct AxisLines { int8_t v[6]; int n; };
constexpr AxisLines kMainX{{-64, -23, 23, 64}, 4};
constexpr AxisLines kMainY{{-56, -23, 23, 53}, 4};
constexpr AxisLines kCStickX{{-64, -23, 23, 64}, 4};
constexpr AxisLines kCStickY{{-53, -23, 23, 53}, 4};
// Shield drop: main stick Y at -0.6625 (-53) counts while a shield input is held.
constexpr int8_t kShieldDropLine = -53;
// The analog trigger value the game reads as a light shield (the online code treats everything at
// or below 42 as released).
constexpr uint8_t kTriggerLine = 43;

// Buttons the game acts on when pressed (A B Start) and buttons held for a state (X Y, Z R L).
// The D-pad is left out.
constexpr uint16_t kPressButtons = 0x1300;
constexpr uint16_t kHeldButtons = 0x0C70;

// Two rule sets, chosen per sampler for measurement:
//   kAllEdges   every line both ways, every button both ways (the confirmed pad differs either way).
//   kPresses    only moves outward across a line and button presses (plus X Y Z R L releases);
//               fewer sends, each one the start of an action.
enum class Rules : uint8_t { kAllEdges = 0, kPresses = 1 };

inline bool crossed(int8_t now, int8_t before, int8_t line, bool outward_only) {
  const bool in_now = line < 0 ? now <= line : now >= line;
  const bool in_before = line < 0 ? before <= line : before >= line;
  return outward_only ? (in_now && !in_before) : (in_now != in_before);
}
inline bool axis_changed(int8_t now, int8_t before, const AxisLines& z, bool outward_only) {
  for (int i = 0; i < z.n; ++i)
    if (crossed(now, before, z.v[i], outward_only)) return true;
  return false;
}

// True when `now` differs from `before` in a way that changes what the game does this frame. Both
// pads must already be rest-clamped.
inline bool matters(const uint8_t now[kPadBytes], const uint8_t before[kPadBytes], Rules rules = Rules::kAllEdges) {
  const bool out = rules == Rules::kPresses;
  const uint16_t bn = (uint16_t)((now[0] << 8) | now[1]), bb = (uint16_t)((before[0] << 8) | before[1]);
  if ((bn ^ bb) & kHeldButtons) return true;
  if (out ? (bn & ~bb & kPressButtons) : ((bn ^ bb) & kPressButtons)) return true;
  if (axis_changed((int8_t)now[2], (int8_t)before[2], kMainX, out)) return true;
  if (axis_changed((int8_t)now[3], (int8_t)before[3], kMainY, out)) return true;
  if (axis_changed((int8_t)now[4], (int8_t)before[4], kCStickX, out)) return true;
  if (axis_changed((int8_t)now[5], (int8_t)before[5], kCStickY, out)) return true;
  const bool shielding = (bn & 0x0070) || now[6] >= kTriggerLine || now[7] >= kTriggerLine;
  if (shielding && crossed((int8_t)now[3], (int8_t)before[3], kShieldDropLine, out)) return true;
  if ((now[6] >= kTriggerLine) != (before[6] >= kTriggerLine)) return true;
  if ((now[7] >= kTriggerLine) != (before[7] >= kTriggerLine)) return true;
  return false;
}

// A pad that would match every idle controller: no button, sticks at rest, triggers released.
inline bool neutral(const uint8_t pad[kPadBytes]) {
  return (pad[0] & 0x1F) == 0 && (pad[1] & 0x7F) == 0 && pad[2] == 0 && pad[3] == 0 && pad[4] == 0 && pad[5] == 0 &&
         pad[6] < kTriggerLine && pad[7] < kTriggerLine;
}

inline void encode(int32_t target, uint8_t port, const uint8_t pad[kPadBytes], uint8_t out[kMessageBytes]) {
  const uint32_t t = (uint32_t)target;
  out[0] = (uint8_t)(t >> 24); out[1] = (uint8_t)(t >> 16); out[2] = (uint8_t)(t >> 8); out[3] = (uint8_t)t;
  out[4] = port;
  std::memcpy(out + 5, pad, kPadBytes);
}
inline void decode(const uint8_t in[kMessageBytes], int32_t& target, uint8_t& port, uint8_t pad[kPadBytes]) {
  target = (int32_t)(((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | in[3]);
  port = in[4];
  std::memcpy(pad, in + 5, kPadBytes);
}

struct Entry {
  int32_t target = 0;          // the pad frame this change first appears in
  uint8_t pad[kPadBytes] = {};
};

// The early pads held for one remote player, oldest target first, at most kMaxEntries. Only
// prediction reads it: a confirmed pad for a frame always replaces whatever was guessed for it.
class Buffer {
 public:
  // Adds or replaces (the newest word about one target frame stands). A full buffer drops its
  // oldest entry for a newer target and ignores a target older than everything it holds.
  void add(int32_t target, const uint8_t pad[kPadBytes]) {
    for (Entry& e : entries_) {
      if (e.target == target) { std::memcpy(e.pad, pad, kPadBytes); return; }
    }
    if ((int)entries_.size() >= kMaxEntries) {
      if (target < entries_.front().target) return;
      entries_.erase(entries_.begin());
    }
    size_t at = entries_.size();
    while (at > 0 && entries_[at - 1].target > target) --at;
    Entry e;
    e.target = target;
    std::memcpy(e.pad, pad, kPadBytes);
    entries_.insert(entries_.begin() + (std::ptrdiff_t)at, e);
  }
  // The confirmed pads now reach `latest`: every early pad for a frame at or before it is spent.
  void drop_through(int32_t latest) {
    size_t n = 0;
    while (n < entries_.size() && entries_[n].target <= latest) ++n;
    if (n) entries_.erase(entries_.begin(), entries_.begin() + (std::ptrdiff_t)n);
  }
  // The pad to predict `frame` with when the confirmed pads only reach `latest` (latest < frame):
  // the newest early pad for a frame after `latest` and not after `frame`, or none.
  const Entry* best(int32_t frame, int32_t latest) const {
    const Entry* found = nullptr;
    for (const Entry& e : entries_) {
      if (e.target > latest && e.target <= frame) found = &e;
      if (e.target > frame) break;
    }
    return found;
  }
  const std::vector<Entry>& entries() const { return entries_; }
  void clear() { entries_.clear(); }

 private:
  std::vector<Entry> entries_;
};

}  // namespace host::early_input
