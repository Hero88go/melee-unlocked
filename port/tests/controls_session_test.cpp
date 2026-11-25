// Tap jump off and tilt stick: the production exchange (runtime/host/controls.cpp) as one machine, with
// the opponent played by hand through controls_wire.h. Every flow ends with both machines holding the
// same decision. No window, game or network; the remembered answers go to a temporary file.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "controls.h"
#include "controls_policy.h"
#include "controls_wire.h"
#include "gameplay_policy.h"

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {
int g_mode = -1;
bool g_auto = false;
std::vector<std::string> g_logs;
uint64_t fake_now = 0;
uint64_t fake_clock() { return fake_now; }
}  // namespace
namespace host {
void log(const char* fmt, ...) {
  char line[512];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof line, fmt, args);
  va_end(args);
  g_logs.push_back(line);
}
}  // namespace host
namespace lcancel { bool automatic_enabled() { return g_auto; } }
namespace slippi::online { int session_mode() { return g_mode; } }

using namespace controls;

namespace {
constexpr unsigned TJ = MU_CONTROL_TAPJUMP_OFF, TS = MU_CONTROL_TILT_STICK, DA = MU_CONTROL_TILT_DASH_ATTACK,
                   LC = MU_CONTROL_AUTO_LCANCEL;

// The opponent: port 1, its own view of every message.
struct Remote {
  wire::State s;
  uint8_t seq = 0;
  Remote() { s.player[0] = s.player[1] = true; }
  // Takes what this machine sent; returns how many answers about the remote's own offer were in it.
  std::vector<wire::Message> take(const std::vector<std::vector<uint8_t>>& out) {
    std::vector<wire::Message> got;
    for (const auto& bytes : out) {
      wire::Message m;
      if (wire::decode(bytes.data(), bytes.size(), &m)) { wire::apply(s, m); got.push_back(m); }
    }
    return got;
  }
  void offer(unsigned bits) {
    const auto bytes = wire::encode_offer(1, ++seq, bits);
    wire::Message m; wire::decode(bytes.data(), bytes.size(), &m); wire::apply(s, m);
    receive(bytes.data(), bytes.size());
  }
  void answer(bool yes) {
    const auto bytes = wire::encode_answer(1, 0, s.seq[0], yes);
    wire::Message m; wire::decode(bytes.data(), bytes.size(), &m); wire::apply(s, m);
    receive(bytes.data(), bytes.size());
  }
};

void set(bool tapjump, bool tilt, bool dash = true) {
  Settings c;
  c.tapjump_off = tapjump;
  c.tilt_stick = tilt;
  c.tilt_dash_attack = dash;
  set_settings(c);
}
std::vector<Player> opponent(const char* key) { return {Player{1, key, "Opponent"}}; }
const int kIdentity[4] = {0, 1, -1, -1};
bool logged(const char* text) {
  for (const auto& l : g_logs) if (l.find(text) != std::string::npos) return true;
  return false;
}
}  // namespace

int main() {
  int groups = 0;
  const auto memory = std::filesystem::temp_directory_path() / "mu_controls_session_test.txt";
  std::filesystem::remove(memory);
  set_memory_path(memory.u8string());
  set_engine_supported(true);

  // 1. A Slippi match (Direct, not made by YouGame): no exchange, nothing sent, defaults online.
  set(true, true);
  g_mode = 2;
  session_begin(0, opponent("id:slippi"), 2, false, false);
  CHECK(!session_active());
  CHECK(take_outgoing().empty());
  CHECK(fold_inputs(42) == 42);
  freeze(kIdentity);
  CHECK(match_word() == 0);
  ++groups;

  // 2. YouGame Unranked: this player's offer goes out at once.
  set(false, true);
  g_mode = 1;
  {
    Remote r;
    session_begin(0, opponent("id:B"), 1, true, false);
    CHECK(session_active());
    auto sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && sent[0].kind == wire::kOffer && sent[0].bits == (TS | DA));
    ++groups;

    // 3. Their tap jump off is not this player's own: asked, nothing answered, defaults meanwhile.
    CHECK(!settled());                     // the opponent's offer has not come
    r.offer(TJ);
    CHECK(take_outgoing().empty());
    CHECK(!settled());                     // asked, not answered: the match would wait
    View v = view();
    CHECK(v.peers.size() == 1 && v.peers[0].mine == Answer::Ask && v.peers[0].bits == TJ);
    CHECK(wire::decide(r.s) == 0 && v.word == 0);
    ++groups;

    // 4. Allowed (and remembered); they say yes to this player's tilt stick: both decide the same.
    answer(1, true);
    sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && sent[0].kind == wire::kAnswer && sent[0].yes && sent[0].about == 1);
    CHECK(remembered_count() == 1);
    r.answer(true);
    v = view();
    CHECK(v.word == ((TS | DA) | (TJ << 8)));
    CHECK(wire::decide(r.s) == v.word);
    CHECK(fold_inputs(7) == wire::fold_inputs(7, true, v.word));
    CHECK(settled() && wire::settled(r.s));
    ++groups;

    // 5. Frozen for the match: the game gets the word, nothing else changes it, and it ends with the match.
    freeze(kIdentity);
    match_started();
    CHECK(match_word() == ((TS | DA) | (TJ << 8)));
    answer(1, false);                       // in the match: remembered only, nothing sent
    CHECK(take_outgoing().empty());
    CHECK(match_word() == ((TS | DA) | (TJ << 8)));
    CHECK(!lcancel_agreed());
    match_ended();
    CHECK(match_word() == 0);
    answer(1, true);                        // back to yes for the next groups
    take_outgoing();
    session_end();
  }
  ++groups;

  // 6. The same opponent again: remembered, answered at once, never asked.
  {
    Remote r;
    session_begin(0, opponent("id:B"), 1, true, false);
    r.take(take_outgoing());
    r.offer(TJ);
    auto sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && sent[0].yes);
    CHECK(view().peers[0].mine == Answer::Yes);
    session_end();
  }
  ++groups;

  // 7. Own options count as yes (the notice), "Not this match" says no for one match only.
  set(true, false);
  {
    Remote r;
    session_begin(0, opponent("id:own"), 1, true, false);
    r.take(take_outgoing());
    r.offer(TJ);
    auto sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && sent[0].yes && view().peers[0].mine == Answer::YesOwn);
    r.answer(true);
    CHECK(view().word == (TJ | (TJ << 8)));
    decline_this_match(1);
    sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && !sent[0].yes && view().peers[0].mine == Answer::NoThisMatch);
    CHECK(view().word == 0 && wire::decide(r.s) == 0);
    CHECK(remembered_count() == 1);          // not remembered
    freeze(kIdentity);
    CHECK(match_word() == 0);
    match_ended();
    sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && sent[0].yes);  // the next match is the usual yes again
    CHECK(view().word == (TJ | (TJ << 8)) && wire::decide(r.s) == view().word);
    session_end();
  }
  ++groups;

  // 8. Locked in with a question open (owner 11-25: Unranked asks before game 1): nothing goes out, the
  //    match waits with a countdown, the answer given while locked in is sent and counts for this match;
  //    a second answer while locked in is only remembered.
  set_test_clock(fake_clock);
  set(true, false);
  {
    Remote r;
    session_begin(0, opponent("id:late"), 1, true, false);
    r.take(take_outgoing());
    r.offer(TS);
    CHECK(view().peers[0].mine == Answer::Ask);
    fake_now = 1000;
    auto sent = r.take(before_selection(true));
    CHECK(sent.empty());
    CHECK(css_line().state == Css::Asked && css_line().seconds == kAskSeconds && css_line().theirs == TS);
    fake_now += 3000;
    tick();
    CHECK(take_outgoing().empty() && css_line().seconds == kAskSeconds - 3 && !settled());
    answer(1, true);
    sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && sent[0].yes);
    CHECK(remembered_count() == 2);
    r.answer(true);
    CHECK(settled() && view().word == (TJ | (TS << 8)) && wire::decide(r.s) == view().word);
    CHECK(css_line().state == Css::Agreed && css_line().theirs == (TJ | TS));
    answer(1, false);
    CHECK(take_outgoing().empty());
    session_end();
  }
  ++groups;

  // 8b. Unanswered for kAskSeconds: no for this match only (not remembered), default controls.
  {
    Remote r;
    session_begin(0, opponent("id:slow"), 1, true, true);   // met already locked in, as in Unranked
    r.take(take_outgoing());
    r.offer(TS);
    CHECK(css_line().state == Css::Asked && css_line().seconds == kAskSeconds);
    fake_now += (uint64_t)kAskSeconds * 1000;
    tick();
    auto sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && !sent[0].yes && view().peers[0].mine == Answer::NoLocked);
    r.answer(true);
    CHECK(settled() && view().word == 0 && wire::decide(r.s) == 0 && css_line().state == Css::Defaults);
    CHECK(remembered_count() == 2);
    session_end();
  }
  ++groups;

  // 8c. Own options, locked in: the yes waits kOwnSeconds for "Press Y for default controls", then goes;
  //     pressed in that time, it is no for this match.
  set(false, true);
  {
    Remote r;
    session_begin(0, opponent("id:own2"), 1, true, true);
    r.take(take_outgoing());
    r.offer(TS);
    CHECK(take_outgoing().empty() && css_line().state == Css::AllowedOwn && css_line().seconds == kOwnSeconds);
    CHECK(!settled());
    fake_now += (uint64_t)kOwnSeconds * 1000;
    tick();
    auto sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && sent[0].yes);
    session_end();
  }
  {
    Remote r;
    session_begin(0, opponent("id:own3"), 1, true, true);
    r.take(take_outgoing());
    r.offer(TS);
    decline_this_match(1);
    auto sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && !sent[0].yes && view().peers[0].mine == Answer::NoThisMatch);
    r.answer(true);
    CHECK(settled() && view().word == 0 && css_line().state == Css::Defaults);
    session_end();
  }
  ++groups;

  // 8d. A player with none of the options on is never asked and never waits (owner 11-25).
  set(false, false);
  {
    Remote r;
    session_begin(0, opponent("id:plain"), 1, true, true);
    r.take(take_outgoing());
    r.offer(TS);
    auto sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && !sent[0].yes && view().peers[0].mine == Answer::NoUnused);
    CHECK(settled() && view().word == 0 && css_line().state == Css::Defaults);
    CHECK(remembered_count() == 2);
    session_end();
  }
  set_test_clock(nullptr);
  ++groups;

  // 9. A changed setting is a new offer while not locked in, and waits while locked in.
  {
    Remote r;
    session_begin(0, opponent("id:B"), 1, true, false);
    r.take(take_outgoing());
    set(true, false);
    tick();
    auto sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && sent[0].kind == wire::kOffer && sent[0].bits == TJ && sent[0].seq == 2);
    r.take(before_selection(true));
    set(true, true);
    tick();
    CHECK(take_outgoing().empty());
    sent = r.take(before_selection(false));
    CHECK(!sent.empty() && sent[0].kind == wire::kOffer && sent[0].bits == (TJ | TS | DA));
    session_end();
  }
  ++groups;

  // 10. YouGame Ranked: tilt stick only, never tap jump off.
  set(true, true, false);
  g_mode = 0;
  {
    Remote r;
    session_begin(0, opponent("id:ranked"), 0, true, false);
    auto sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && sent[0].bits == TS);
    session_end();
  }
  ++groups;

  // 11. Auto L-cancel in YouGame Unranked: offered, agreed, on only in that match.
  set(false, false);
  g_auto = true;
  g_mode = 1;
  {
    Remote r;
    session_begin(0, opponent("id:lc"), 1, true, false);
    auto sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && sent[0].bits == LC);
    r.offer(0);
    CHECK(!lcancel_agreed());
    r.answer(true);
    CHECK(view().word == LC && wire::decide(r.s) == LC);
    freeze(kIdentity);
    CHECK(lcancel_agreed());
    g_mode = 2;
    CHECK(!lcancel_agreed());               // only in the mode it was agreed for
    g_mode = 1;
    match_ended();
    CHECK(!lcancel_agreed());
    session_end();
  }
  // In YouGame Friends it is free as before: not offered.
  g_mode = 2;
  {
    Remote r;
    session_begin(0, opponent("id:lc"), 2, true, false);
    auto sent = r.take(take_outgoing());
    CHECK(sent.size() == 1 && sent[0].bits == 0);
    session_end();
  }
  g_auto = false;
  ++groups;

  // 12. Declined: defaults for everyone, both sides agree.
  set(false, true);
  g_mode = 1;
  {
    Remote r;
    session_begin(0, opponent("id:no"), 1, true, false);
    r.take(take_outgoing());
    r.offer(0);
    r.answer(false);
    CHECK(view().word == 0 && wire::decide(r.s) == 0);
    CHECK(view().peers[0].they_agree == false);
    session_end();
  }
  ++groups;

  // 13. Malformed messages and spoofed senders change nothing.
  {
    Remote r;
    session_begin(0, opponent("id:B"), 1, true, false);
    r.take(take_outgoing());
    const uint8_t junk[] = {'M', 'U', 'C', 9, 0, 1, 1, 1};
    receive(junk, sizeof junk);
    CHECK(logged("malformed"));
    const auto spoof = wire::encode_offer(0, 9, TJ);      // claims to be this player
    receive(spoof.data(), spoof.size());
    CHECK(view().mine == (TS | DA));
    const auto stranger = wire::encode_offer(3, 1, TJ);   // not in the connection
    receive(stranger.data(), stranger.size());
    CHECK(view().peers.size() == 1);
    session_end();
  }
  ++groups;

  // 14. Which port plays whose offer: a partner seat plays its owner's, a CPU or Pro AI seat none.
  {
    Remote r;
    set(true, false);
    session_begin(0, opponent("id:B"), 3, true, false);
    r.take(take_outgoing());
    r.offer(TS);
    answer(1, true);
    r.answer(true);
    r.take(take_outgoing());
    CHECK(view().word == (TJ | (TS << 8)) && wire::decide(r.s) == view().word);
    const int owner[4] = {0, 0, -1, 1};
    freeze(owner);
    g_mode = 3;
    CHECK(match_word() == (TJ | (TJ << 8) | (TS << 24)));
    match_ended();
    session_end();
    g_mode = 1;
  }
  ++groups;

  // 15. Offline: this PC's options on every port; a vanilla-only session or the Static Recomp: none.
  g_mode = -1;
  set(true, true, false);
  CHECK(match_word() == (TJ | TS) * 0x01010101u);
  host::gameplay::vanilla_only_session = true;
  CHECK(match_word() == 0);
  CHECK(!advertise());
  host::gameplay::vanilla_only_session = false;
  set_engine_supported(false);
  CHECK(match_word() == 0 && !advertise());
  set_engine_supported(true);
  CHECK(advertise());
  set(false, false);
  CHECK(match_word() == 0);
  ++groups;

  // 16. The remembered answers survive a restart, a damaged file is read around, forgetting clears it.
  {
    set_memory_path(memory.u8string());
    CHECK(remembered_count() == 2);
    {
      std::ofstream out(memory, std::ios::app);
      out << "no tab here\n\tmissing key 1 2\nid:bad\tnot numbers\n";
    }
    set_memory_path(memory.u8string());
    CHECK(remembered_count() == 2);
    forget_all();
    CHECK(remembered_count() == 0);
    set_memory_path(memory.u8string());
    CHECK(remembered_count() == 0);
  }
  ++groups;

  // 17. The descriptions the character select shows.
  CHECK(describe(0) == "default controls");
  CHECK(describe(TJ) == "Tap jump off");
  CHECK(describe(TS | DA) == "Tilt stick (dash attack)");
  CHECK(describe(TS | DA | MU_CONTROL_TILT_ITEM_TILT_THROW | TJ | LC) == "Tap jump off, Tilt stick (dash attack, light throws), Auto L-cancel");
  ++groups;

  std::filesystem::remove(memory);
  std::printf("control options exchange: %d groups passed (Slippi off, ask, remember, own options, not this match, "
              "lock-in, re-offer, Ranked, auto L-cancel, decline, malformed, seats, offline, memory)\n", groups);
  return 0;
}
