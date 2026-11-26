// Tap jump off and tilt stick in a network match: the messages and the one decision every machine
// takes from them. Pure, no host state: the same messages give the same bytes everywhere.
//
// Each machine offers its player's choices (MU_CONTROL_* bits, controls_policy::offer_bits) and
// answers every other player's offer, yes or no. The match runs every player's offered bits when
// every player said yes to every other player's latest offer that asks for anything; one "no", or an
// answer that has not arrived, and the match runs everyone's default controls (all bits 0). The owner's
// rule: both players consent every match, and without consent it is the default options.
//
// Ordering: a machine sends its offer and its answers before its lock-in selection, on the same
// ordered channel, and changes neither while locked in. A match is set up only when every player is
// locked in, so by then each machine holds every message that decides it. The decided word is folded
// into the match setup hash inputs (mu_setup_hash.h): two machines that decided differently wait for
// each other's next message, and a match never starts on two different decisions.
//
//   offer:  'M' 'U' 'C' 1, kind 0, sender port, offer sequence, bits
//   answer: 'M' 'U' 'C' 1, kind 1, sender port, about port, that offer's sequence, 1 yes / 0 no
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../abi/mu_controls.h"
#include "../abi/mu_setup_hash.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace controls::wire {

constexpr uint8_t kVersion = 1;
constexpr uint8_t kOffer = 0, kAnswer = 1;
constexpr size_t kOfferSize = 8, kAnswerSize = 9;
constexpr int kPorts = 4;

struct Message {
  uint8_t kind = 0xFF;
  uint8_t sender = 0;   // the sender's port (0-3)
  uint8_t about = 0;    // answer: whose offer it answers
  uint8_t seq = 0;      // the offer's sequence (offer: its own; answer: the one answered)
  uint8_t bits = 0;     // offer: MU_CONTROL_* bits, normalized
  bool yes = false;     // answer
};

inline std::vector<uint8_t> encode_offer(uint8_t sender, uint8_t seq, unsigned bits) {
  return {'M', 'U', 'C', kVersion, kOffer, sender, seq, (uint8_t)mu_controls_normalize(bits)};
}
inline std::vector<uint8_t> encode_answer(uint8_t sender, uint8_t about, uint8_t seq, bool yes) {
  return {'M', 'U', 'C', kVersion, kAnswer, sender, about, seq, (uint8_t)(yes ? 1 : 0)};
}

// Exactly one of the two layouts, this version, ports 0-3, an answer never about its own sender,
// and only known bits (unnormalized bits are refused, not repaired). False: nothing is used.
inline bool decode(const uint8_t* data, size_t size, Message* out) {
  if (!data || !out || size < 6 || data[0] != 'M' || data[1] != 'U' || data[2] != 'C' || data[3] != kVersion) return false;
  Message m;
  m.kind = data[4];
  m.sender = data[5];
  if (m.sender >= kPorts) return false;
  if (m.kind == kOffer) {
    if (size != kOfferSize) return false;
    m.seq = data[6];
    m.bits = data[7];
    if (m.bits != mu_controls_normalize(m.bits)) return false;
  } else if (m.kind == kAnswer) {
    if (size != kAnswerSize) return false;
    m.about = data[6];
    m.seq = data[7];
    if (m.about >= kPorts || m.about == m.sender || data[8] > 1) return false;
    m.yes = data[8] == 1;
  } else {
    return false;
  }
  *out = m;
  return true;
}

// Everything every machine has heard (its own messages included), by port.
struct State {
  bool player[kPorts] = {};                 // this port is a person's machine in the connection
  bool have_offer[kPorts] = {};
  uint8_t seq[kPorts] = {}, bits[kPorts] = {};
  bool have_answer[kPorts][kPorts] = {};    // [who answered][whose offer]
  uint8_t answer_seq[kPorts][kPorts] = {};
  bool yes[kPorts][kPorts] = {};
};

// Folds one message into the state. Offers from a port that is not a player are ignored; a newer
// offer replaces the old one (answers to the old sequence stop counting by themselves).
inline void apply(State& s, const Message& m) {
  if (m.sender >= kPorts || !s.player[m.sender]) return;
  if (m.kind == kOffer) {
    s.have_offer[m.sender] = true;
    s.seq[m.sender] = m.seq;
    s.bits[m.sender] = m.bits;
  } else if (m.kind == kAnswer && m.about < kPorts && s.player[m.about]) {
    s.have_answer[m.sender][m.about] = true;
    s.answer_seq[m.sender][m.about] = m.seq;
    s.yes[m.sender][m.about] = m.yes;
  }
}

// The match word (one byte per port, mu_controls.h): every player's offer when all is agreed, else 0.
inline uint32_t decide(const State& s) {
  int players = 0;
  bool anything = false;
  for (int p = 0; p < kPorts; ++p) {
    if (!s.player[p]) continue;
    ++players;
    if (!s.have_offer[p]) return 0;   // not heard from yet: defaults until it is
    if (s.bits[p]) anything = true;
  }
  if (players < 2 || !anything) return 0;
  for (int t = 0; t < kPorts; ++t) {
    if (!s.player[t] || !(s.bits[t] & (MU_CONTROL_TAPJUMP_OFF | MU_CONTROL_TILT_STICK | MU_CONTROL_AUTO_LCANCEL | MU_CONTROL_20XX103))) continue;
    for (int p = 0; p < kPorts; ++p) {
      if (p == t || !s.player[p]) continue;
      if (!s.have_answer[p][t] || s.answer_seq[p][t] != s.seq[t] || !s.yes[p][t]) return 0;
    }
  }
  uint32_t word = 0;
  for (int p = 0; p < kPorts; ++p)
    if (s.player[p]) word = (uint32_t)mu_controls_with_slot(word, p, s.bits[p]);
  return word;
}

// Every message that decides the match is here: every player's offer, and an answer from every other
// player to each offer that asks for something, for that offer's latest sequence. Until then a machine
// that is ready holds the match back (slippi_online.cpp), so no machine starts on a partial decision.
inline bool settled(const State& s) {
  for (int p = 0; p < kPorts; ++p)
    if (s.player[p] && !s.have_offer[p]) return false;
  for (int t = 0; t < kPorts; ++t) {
    if (!s.player[t] || !(s.bits[t] & (MU_CONTROL_TAPJUMP_OFF | MU_CONTROL_TILT_STICK | MU_CONTROL_AUTO_LCANCEL | MU_CONTROL_20XX103))) continue;
    for (int p = 0; p < kPorts; ++p)
      if (p != t && s.player[p] && (!s.have_answer[p][t] || s.answer_seq[p][t] != s.seq[t])) return false;
  }
  return true;
}

// The setup hash inputs with this exchange folded in. Every machine of an exchanging connection folds
// the same tag and the word it decided; a connection with no exchange folds nothing, so its hash is
// exactly what it was before this existed.
inline uint64_t fold_inputs(uint64_t inputs, bool exchanging, uint32_t word) {
  if (!exchanging) return inputs;
  return mu_fnv64_u32(mu_fnv64_bytes(inputs, "mu-controls-1", 13), word);
}

}  // namespace controls::wire
