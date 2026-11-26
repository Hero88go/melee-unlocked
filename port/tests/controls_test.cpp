// Tap jump off and tilt stick: the mode table, the messages and the one decision every machine takes.
// No window, game, network or file: only controls_policy.h, controls_wire.h and mu_controls.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "controls_policy.h"
#include "controls_wire.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

using namespace controls;

namespace {
constexpr unsigned TJ = MU_CONTROL_TAPJUMP_OFF, TS = MU_CONTROL_TILT_STICK, DA = MU_CONTROL_TILT_DASH_ATTACK,
                   IT = MU_CONTROL_TILT_ITEM_TILT_THROW, LC = MU_CONTROL_AUTO_LCANCEL;

wire::State players(std::initializer_list<int> ports) {
  wire::State s;
  for (int p : ports) s.player[p] = true;
  return s;
}
void offer(wire::State& s, int port, uint8_t seq, unsigned bits) {
  wire::Message m;
  const auto bytes = wire::encode_offer((uint8_t)port, seq, bits);
  if (wire::decode(bytes.data(), bytes.size(), &m)) wire::apply(s, m);
}
void answer(wire::State& s, int from, int about, uint8_t seq, bool yes) {
  wire::Message m;
  const auto bytes = wire::encode_answer((uint8_t)from, (uint8_t)about, seq, yes);
  if (wire::decode(bytes.data(), bytes.size(), &m)) wire::apply(s, m);
}
}  // namespace

int main() {
  int groups = 0;

  // 1. The mode table (owner 11-24): offline all, Slippi none, YouGame Unranked/Friends/Teams all,
  //    YouGame Ranked tilt stick only, Party never.
  for (bool yg : {false, true}) {
    CHECK(policy::allowed_bits(-1, yg) == MU_CONTROL_GAME_BITS);
    for (int mode : {0, 1, 2, 3, 4, 5}) {
      const unsigned bits = policy::allowed_bits(mode, yg);
      if (!yg) CHECK(bits == 0);
      else if (mode == 0) CHECK(bits == (TS | DA | IT));
      else if (mode >= 1 && mode <= 3) CHECK(bits == MU_CONTROL_GAME_BITS);
      else CHECK(bits == 0);
      CHECK(!(bits & LC));
    }
  }
  ++groups;

  // 2. Auto L-cancel asks only in YouGame Unranked; exchanges run only where something can be agreed.
  for (int mode : {-1, 0, 1, 2, 3, 4}) {
    CHECK(policy::lcancel_needs_consent(mode, true) == (mode == 1));
    CHECK(!policy::lcancel_needs_consent(mode, false));
    CHECK(!policy::exchange(mode, false));
    CHECK(policy::exchange(mode, true) == (mode >= 0 && mode <= 3));
  }
  ++groups;

  // 3. Offers: only what the mode allows, the sub-choices only with tilt stick, auto L-cancel only
  //    where it needs consent.
  CHECK(policy::offer_bits(TJ | TS | DA, false, 0, true) == (TS | DA));              // Ranked: no tap jump off
  CHECK(policy::offer_bits(TJ | DA | IT, false, 1, true) == TJ);                     // no tilt stick: no sub-choices
  CHECK(policy::offer_bits(TJ | TS, true, 1, true) == (TJ | TS | LC));               // Unranked: auto L-cancel asks
  CHECK(policy::offer_bits(TJ | TS, true, 2, true) == (TJ | TS));                    // Friends: auto L-cancel is free there
  CHECK(policy::offer_bits(TJ | TS, true, 2, false) == 0);                           // Slippi Direct: nothing
  CHECK(policy::offer_bits(MU_CONTROL_GAME_BITS, true, 4, true) == 0);               // Party: nothing
  ++groups;

  // 4. Normalizing and the per-slot word.
  CHECK(mu_controls_normalize(DA | IT) == 0);
  CHECK(mu_controls_normalize(TS | DA | IT | 0xE0) == (TS | DA | IT));
  unsigned word = 0;
  word = mu_controls_with_slot(word, 0, TJ);
  word = mu_controls_with_slot(word, 3, TS | DA);
  CHECK(mu_controls_slot(word, 0) == TJ && mu_controls_slot(word, 1) == 0 && mu_controls_slot(word, 3) == (TS | DA));
  CHECK(mu_controls_slot(word, 4) == 0 && mu_controls_slot(word, -1) == 0);
  CHECK(mu_controls_with_slot(word, 7, TJ) == word);
  ++groups;

  // 5. Automatic answers: nothing asked, remembered yes, remembered no, own options, not this match.
  {
    policy::Remembered none;
    CHECK(policy::auto_answer(0, 0, none, false) == policy::Auto::Yes);
    CHECK(policy::auto_answer(DA, 0, none, false) == policy::Auto::Yes);                // a sub-choice alone asks nothing
    CHECK(policy::auto_answer(TS, 0, none, false) == policy::Auto::NoUnused);           // no options of its own: never asked
    CHECK(policy::auto_answer(TS, TJ, none, false) == policy::Auto::Ask);                // both use options: asked
    CHECK(policy::auto_answer(TS, MU_CONTROL_AUTO_LCANCEL, none, false) == policy::Auto::Ask);
    CHECK(policy::auto_answer(TS | DA, TS, none, false) == policy::Auto::YesOwn);       // own tilt stick covers theirs
    CHECK(policy::auto_answer(TS | TJ, TS, none, false) == policy::Auto::Ask);          // tap jump off is not covered
    CHECK(policy::auto_answer(TS, TS, none, true) == policy::Auto::No);
    policy::Remembered yes = policy::remember(none, TS | IT, true);
    CHECK(yes.allowed == TS && yes.declined == 0);
    CHECK(policy::auto_answer(TS, 0, yes, false) == policy::Auto::Yes);
    CHECK(policy::auto_answer(TS | TJ, LC, yes, false) == policy::Auto::Ask);           // something new: asked
    CHECK(policy::auto_answer(TS | TJ, 0, yes, false) == policy::Auto::NoUnused);       // ... unless this player uses nothing
    policy::Remembered no = policy::remember(yes, TS | TJ, false);
    CHECK(no.allowed == TS && no.declined == TJ);                                       // the old yes stays
    CHECK(policy::auto_answer(TS | TJ, 0, no, false) == policy::Auto::No);
    CHECK(policy::auto_answer(TS, 0, no, false) == policy::Auto::Yes);
    policy::Remembered again = policy::remember(no, TJ, true);
    CHECK(again.allowed == (TS | TJ) && again.declined == 0);
    CHECK(policy::auto_answer(LC, LC, none, false) == policy::Auto::YesOwn);
  }
  ++groups;

  // 6. The wire: round trips.
  {
    wire::Message m;
    auto o = wire::encode_offer(1, 7, TS | DA);
    CHECK(o.size() == wire::kOfferSize && wire::decode(o.data(), o.size(), &m));
    CHECK(m.kind == wire::kOffer && m.sender == 1 && m.seq == 7 && m.bits == (TS | DA));
    auto a = wire::encode_answer(2, 0, 9, true);
    CHECK(a.size() == wire::kAnswerSize && wire::decode(a.data(), a.size(), &m));
    CHECK(m.kind == wire::kAnswer && m.sender == 2 && m.about == 0 && m.seq == 9 && m.yes);
    auto n = wire::encode_answer(3, 1, 0, false);
    CHECK(wire::decode(n.data(), n.size(), &m) && !m.yes);
    // encode_offer normalizes what it sends
    auto odd = wire::encode_offer(0, 1, DA | IT);
    CHECK(wire::decode(odd.data(), odd.size(), &m) && m.bits == 0);
  }
  ++groups;

  // 7. The wire: everything malformed is refused whole.
  {
    wire::Message m;
    auto o = wire::encode_offer(1, 7, TS);
    std::vector<uint8_t> bad = o; bad[3] = 2;                                CHECK(!wire::decode(bad.data(), bad.size(), &m));   // version
    bad = o; bad[0] = 'X';                                                   CHECK(!wire::decode(bad.data(), bad.size(), &m));   // magic
    bad = o; bad[5] = 4;                                                     CHECK(!wire::decode(bad.data(), bad.size(), &m));   // port
    bad = o; bad[7] = DA;                                                    CHECK(!wire::decode(bad.data(), bad.size(), &m));   // unnormalized bits
    bad = o; bad[7] = 0x40;                                                  CHECK(!wire::decode(bad.data(), bad.size(), &m));   // unknown bit
    bad = o; bad.push_back(0);                                               CHECK(!wire::decode(bad.data(), bad.size(), &m));   // long
    bad = o; bad.pop_back();                                                 CHECK(!wire::decode(bad.data(), bad.size(), &m));   // short
    bad = o; bad[4] = 2;                                                     CHECK(!wire::decode(bad.data(), bad.size(), &m));   // kind
    auto a = wire::encode_answer(2, 0, 9, true);
    bad = a; bad[6] = 2;                                                     CHECK(!wire::decode(bad.data(), bad.size(), &m));   // about self
    bad = a; bad[6] = 5;                                                     CHECK(!wire::decode(bad.data(), bad.size(), &m));   // about port
    bad = a; bad[8] = 2;                                                     CHECK(!wire::decode(bad.data(), bad.size(), &m));   // yes not 0/1
    CHECK(!wire::decode(nullptr, 8, &m));
    CHECK(!wire::decode(o.data(), o.size(), nullptr));
  }
  ++groups;

  // 8. A 1v1: defaults until every offer is in and every ask is answered yes for its latest offer.
  {
    wire::State s = players({0, 1});
    CHECK(wire::decide(s) == 0);
    offer(s, 0, 1, TS | DA);
    CHECK(wire::decide(s) == 0);                    // P2 not heard from
    offer(s, 1, 1, 0);
    CHECK(wire::decide(s) == 0);                    // P2 has not answered P1's ask
    answer(s, 1, 0, 1, true);
    CHECK(wire::decide(s) == (TS | DA));            // P1 plays tilt stick, P2 the defaults
    offer(s, 0, 2, TS | DA | TJ);                   // P1 changed its options: the old answer stops counting
    CHECK(wire::decide(s) == 0);
    answer(s, 1, 0, 2, false);
    CHECK(wire::decide(s) == 0);
    answer(s, 1, 0, 2, true);
    CHECK(wire::decide(s) == (TS | DA | TJ));
    offer(s, 1, 2, TJ);
    CHECK(wire::decide(s) == 0);                    // P2 now asks too
    answer(s, 0, 1, 2, true);
    CHECK(wire::decide(s) == ((TS | DA | TJ) | (TJ << 8)));
  }
  ++groups;

  // 9. Nothing offered by anyone: 0 without any answer; a lone player never agrees with itself.
  {
    wire::State s = players({0, 1});
    offer(s, 0, 1, 0); offer(s, 1, 1, 0);
    CHECK(wire::decide(s) == 0);
    wire::State alone = players({2});
    offer(alone, 2, 1, TS);
    CHECK(wire::decide(alone) == 0);
  }
  ++groups;

  // 10. Four players: everyone must say yes to every ask; one decline and everyone plays defaults.
  {
    wire::State s = players({0, 1, 2, 3});
    offer(s, 0, 1, TS); offer(s, 1, 1, 0); offer(s, 2, 1, TJ); offer(s, 3, 1, 0);
    for (int p : {1, 2, 3}) answer(s, p, 0, 1, true);
    for (int p : {0, 1, 3}) answer(s, p, 2, 1, true);
    CHECK(wire::decide(s) == (TS | (TJ << 16)));
    answer(s, 3, 2, 1, false);
    CHECK(wire::decide(s) == 0);
    answer(s, 3, 2, 1, true);
    CHECK(wire::decide(s) == (TS | (TJ << 16)));
    // a "no" about a player who asks for nothing changes nothing
    answer(s, 1, 3, 1, false);
    CHECK(wire::decide(s) == (TS | (TJ << 16)));
  }
  ++groups;

  // 11. Three players (an arena free-for-all shape), and order does not matter: answers before offers.
  {
    wire::State s = players({0, 2, 3});
    answer(s, 2, 0, 5, true); answer(s, 3, 0, 5, true);
    offer(s, 2, 1, 0); offer(s, 3, 1, 0);
    CHECK(wire::decide(s) == 0);
    offer(s, 0, 5, TS);
    CHECK(wire::decide(s) == TS);
  }
  ++groups;

  // 12. Messages from ports that are not in the connection are ignored.
  {
    wire::State s = players({0, 1});
    offer(s, 2, 1, TJ);
    CHECK(!s.have_offer[2]);
    offer(s, 0, 1, 0); offer(s, 1, 1, 0);
    CHECK(wire::decide(s) == 0);
  }
  ++groups;

  // 13. The setup hash: no exchange folds nothing (the old hash exactly); an exchange always folds,
  //     and two different decisions give two different inputs.
  {
    const uint64_t inputs = 0x0123456789ABCDEFull;
    CHECK(wire::fold_inputs(inputs, false, 0) == inputs);
    CHECK(wire::fold_inputs(inputs, false, 0x0202) == inputs);
    CHECK(wire::fold_inputs(inputs, true, 0) != inputs);
    CHECK(wire::fold_inputs(inputs, true, 0) != wire::fold_inputs(inputs, true, TS));
    CHECK(wire::fold_inputs(inputs, true, TS) == wire::fold_inputs(inputs, true, TS));
    CHECK(wire::fold_inputs(inputs, true, TS) != wire::fold_inputs(inputs, true, TS << 8));
  }
  ++groups;

  // 14. Settled: every offer in, and every ask answered for its latest offer; a "no" settles too.
  {
    wire::State s = players({0, 1});
    CHECK(!wire::settled(s));
    offer(s, 0, 1, TS);
    CHECK(!wire::settled(s));
    offer(s, 1, 1, 0);
    CHECK(!wire::settled(s));                       // P1 asks, P2 has not answered
    answer(s, 1, 0, 1, false);
    CHECK(wire::settled(s) && wire::decide(s) == 0);
    offer(s, 0, 2, TS | TJ);                        // a new offer: the old answer no longer settles it
    CHECK(!wire::settled(s));
    answer(s, 1, 0, 2, true);
    CHECK(wire::settled(s) && wire::decide(s) == (TS | TJ));
    wire::State quiet = players({0, 1, 2});
    offer(quiet, 0, 1, 0); offer(quiet, 1, 1, 0);
    CHECK(!wire::settled(quiet));                   // P3 not heard from
    offer(quiet, 2, 1, 0);
    CHECK(wire::settled(quiet) && wire::decide(quiet) == 0);   // nobody asks for anything: settled at once
  }
  ++groups;

  std::printf("control options: %d groups passed (mode table, offers, answers, wire, decisions for 1-4 players, setup hash)\n", groups);
  return 0;
}
