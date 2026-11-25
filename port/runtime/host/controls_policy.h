// Where tap jump off and tilt stick may run, and where automatic L-cancel needs the opponent's OK.
// Window-free and constexpr, like lcancel_policy.h, so the whole table is unit tested.
//
// Owner's rules (2026-11-24):
//  - offline: both, always (this PC's choice, every person playing on it);
//  - online, only in YouGame matches and only when every player agrees for that match: Unranked,
//    Friends (a Direct friend room), and the Friends lobby or Doubles (Teams);
//  - YouGame Ranked: tilt stick only, with the same agreement; never tap jump off;
//  - never in a Slippi match (Unranked, Direct, Teams, Party), whoever is on the other side;
//  - automatic L-cancel: as before in Direct (Slippi Direct and YouGame Friends) and offline, and
//    now also in YouGame Unranked when the opponent agrees.
//  - 20XX Melee 1.03 (owner 11-25): one ruleset, agreed like the others, in the same YouGame modes
//    (Unranked, Friends, lobby and Doubles, Arena rooms) and offline; never Ranked, never Slippi.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../abi/mu_controls.h"

namespace controls::policy {

// Slippi mode ids: Ranked 0, Unranked 1, Direct 2, Teams 3, Party 4; offline < 0. `yougame`: this
// connection was made through YouGame (both machines know it the same way).
constexpr unsigned allowed_bits(int mode, bool yougame) {
  if (mode < 0) return MU_CONTROL_GAME_BITS;
  if (!yougame) return 0u;
  switch (mode) {
    case 0: return MU_CONTROL_TILT_STICK | MU_CONTROL_TILT_DASH_ATTACK | MU_CONTROL_TILT_ITEM_TILT_THROW;   // no 20XX
    case 1: case 2: case 3: return MU_CONTROL_GAME_BITS;
    default: return 0u;   // Party is only ever Slippi's
  }
}

// Automatic L-cancel needs the opponent's OK here (it is allowed outright offline and in Direct,
// lcancel_policy.h, and never anywhere else).
constexpr bool lcancel_needs_consent(int mode, bool yougame) { return yougame && mode == 1; }

// A network match runs an exchange at all: something here can be agreed.
constexpr bool exchange(int mode, bool yougame) {
  return mode >= 0 && (allowed_bits(mode, yougame) != 0 || lcancel_needs_consent(mode, yougame));
}

// What this player offers in a match: their own choices the mode allows, normalized.
inline unsigned offer_bits(unsigned own_game_bits, bool own_auto_lcancel, int mode, bool yougame) {
  unsigned bits = own_game_bits & allowed_bits(mode, yougame);
  if (own_auto_lcancel && lcancel_needs_consent(mode, yougame)) bits |= MU_CONTROL_AUTO_LCANCEL;
  return mu_controls_normalize(bits);
}

// The choices that need an answer: the sub-choices of tilt stick ride with it.
constexpr unsigned main_bits(unsigned bits) {
  return bits & (MU_CONTROL_TAPJUMP_OFF | MU_CONTROL_TILT_STICK | MU_CONTROL_AUTO_LCANCEL | MU_CONTROL_20XX103);
}

// How this player answers an offer before asking anyone (owner: "your own options on counts as
// yes, with a notice and a way to turn it off for the match"; remembered answers are never asked
// twice). Ask means the character select asks. A player who has none of the options on is never
// asked and never waits (owner 11-25: "it only needs a short pause if both players have either option
// enabled"): that is a no, and the match plays the default controls.
enum class Auto { Yes, YesOwn, No, NoUnused, Ask };
struct Remembered { unsigned allowed = 0, declined = 0; };
constexpr Auto auto_answer(unsigned offered, unsigned own_offered, Remembered remembered, bool not_this_match) {
  const unsigned want = main_bits(offered);
  if (want == 0) return Auto::Yes;                       // nothing to agree to
  if (not_this_match) return Auto::No;
  if ((want & ~remembered.allowed) == 0) return Auto::Yes;
  if (want & remembered.declined) return Auto::No;
  if ((want & ~main_bits(own_offered)) == 0) return Auto::YesOwn;
  if (main_bits(own_offered) == 0) return Auto::NoUnused;
  return Auto::Ask;
}

// A prompt answered: what is remembered for that opponent from now on.
constexpr Remembered remember(Remembered was, unsigned offered, bool allow) {
  const unsigned want = main_bits(offered);
  if (allow) return Remembered{was.allowed | want, was.declined & ~want};
  // Declined: the choices not yet allowed are declined; what was allowed before stays allowed.
  return Remembered{was.allowed, was.declined | (want & ~was.allowed)};
}

}  // namespace controls::policy
