// SPDX-License-Identifier: GPL-2.0-or-later
// The Friends lobby's state model (yougame_lobby.h): rev handling, refusals, hover pacing, hello,
// start's optional inputDelay, the keepalive, Start's reasons, directOpen triggers and the setup.
#include "yougame_lobby.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

using namespace yougame::lobby;

static json seat_open(int s) { return {{"seat", s}, {"kind", "open"}}; }
static json seat_human(int s, const std::string& id, bool picked = false, int team = 0) {
  json j = {{"seat", s}, {"kind", "human"}, {"id", id}, {"name", id}};
  if (picked) j["pick"] = {{"char", 2}, {"color", 0}, {"team", team}};
  return j;
}
static json seat_cpu(int s, int level, bool picked = false, int team = 0) {
  json j = {{"seat", s}, {"kind", "cpu"}, {"level", level}};
  if (picked) j["pick"] = {{"char", 9}, {"color", 1}, {"team", team}};
  return j;
}
static json state(int64_t rev, int host, std::vector<json> seats, bool teams = false) {
  return {{"t", "yg.mu.lobby"}, {"v", 1}, {"op", "state"}, {"rev", rev}, {"host", host < 0 ? json() : json(host)},
          {"seats", seats},
          {"rules", {{"teams", teams}, {"friendlyFire", false}, {"mode", "stock"}, {"stocks", 4}, {"minutes", 8}, {"pause", false}}},
          {"setup", nullptr}};
}
static json refusal(json s, const std::string& error, const std::string& action) {
  s["error"] = error; s["action"] = action; return s;
}
static std::vector<json> lobby_frames(const std::vector<json>& out) {
  std::vector<json> v;
  for (const auto& f : out) if (f.value("t", "") == kType) v.push_back(f);
  return v;
}
static Session::Options opts(const std::string& me, bool hello = true, int delay = 0) {
  Session::Options o;
  o.me = me;
  if (hello) { o.settings_digest = "settings1"; o.card_digest = "card1"; }
  o.input_delay = delay;
  return o;
}

int main() {
  uint64_t now = 100000;

  // ---- welcome.muLobby, my seat, host, hello once on taking a seat ----
  {
    Session s(opts("a"));
    CHECK(!s.known() && s.my_seat() == -1 && s.can_start() == StartBlock::NotSeated);
    json welcome = {{"t", "welcome"}, {"me", "conn-a"},
                    {"muLobby", state(1, 0, {seat_human(0, "a"), seat_open(1), seat_open(2), seat_open(3)})}};
    CHECK(s.receive(welcome));
    CHECK(s.known() && s.my_seat() == 0 && s.is_host());
    auto out = lobby_frames(s.due(now));
    CHECK(out.size() == 1 && out[0]["op"] == "hello" && out[0]["settingsDigest"] == "settings1" && !out[0].count("rev"));
    // The same seat again: no second hello.
    CHECK(s.receive(state(2, 0, {seat_human(0, "a"), seat_open(1), seat_open(2), seat_open(3)})));
    CHECK(lobby_frames(s.due(now)).empty());
    // Rooms has no hello yet: a shape refusal stops it for good.
    CHECK(s.receive(refusal(state(2, 0, {seat_human(0, "a"), seat_open(1), seat_open(2), seat_open(3)}), "shape", "hello")));
    CHECK(!s.hello_supported());
    CHECK(s.refusal() == "shape" && s.refusal_action() == "hello");
    // Moved to another seat (a reconnect): no hello, since Rooms refused it.
    CHECK(s.receive(state(3, 2, {seat_open(0), seat_open(1), seat_human(2, "a"), seat_open(3)})));
    CHECK(s.my_seat() == 2 && lobby_frames(s.due(now)).empty());
  }
  {
    // Hello again on another seat when Rooms takes it.
    Session s(opts("a"));
    s.receive(state(1, 0, {seat_human(0, "a"), seat_open(1), seat_open(2), seat_open(3)}));
    CHECK(lobby_frames(s.due(now)).size() == 1);
    s.receive(state(2, 1, {seat_open(0), seat_human(1, "a"), seat_open(2), seat_open(3)}));
    auto out = lobby_frames(s.due(now));
    CHECK(out.size() == 1 && out[0]["op"] == "hello");
    // No digests given: never a hello.
    Session quiet(opts("b", false));
    quiet.receive(state(1, 0, {seat_human(0, "b"), seat_open(1), seat_open(2), seat_open(3)}));
    CHECK(lobby_frames(quiet.due(now)).empty());
  }

  // ---- own picks carry no rev; CPU picks, rules, seats and start carry the rev the host saw ----
  {
    Session s(opts("a", false));
    s.receive(state(4, 0, {seat_human(0, "a"), seat_cpu(1, 5), seat_open(2), seat_human(3, "b")}));
    CHECK(s.pick(Pick{2, 1, 0}));
    CHECK(s.unpick());
    CHECK(s.cpu_pick(1, Pick{9, 1, 0}));
    CHECK(!s.cpu_pick(3, Pick{9, 1, 0}));      // seat 3 is a human: not the host's to pick
    CHECK(!s.cpu_pick(2, Pick{9, 1, 0}));      // nor an open seat
    Rules r; r.stocks = 3;
    CHECK(s.set_rules(r));
    CHECK(s.set_cpu(2, 7));
    CHECK(!s.set_cpu(3, 7));                   // a human's seat: seat_occupied, never sent
    CHECK(s.open_seat(1));
    CHECK(!s.open_seat(2));                    // already open
    CHECK(!s.set_cpu(1, 10));                  // level out of range
    auto out = lobby_frames(s.due(now));
    CHECK(out.size() == 6);
    CHECK(out[0]["op"] == "pick" && !out[0].count("rev") && out[0]["seat"] == 0);
    CHECK(out[1]["op"] == "unpick" && !out[1].count("rev"));
    CHECK(out[2]["op"] == "pick" && out[2]["rev"] == 4 && out[2]["seat"] == 1);
    CHECK(out[3]["op"] == "rules" && out[3]["rev"] == 4 && out[3]["rules"]["stocks"] == 3);
    CHECK(out[4]["op"] == "seat" && out[4]["kind"] == "cpu" && out[4]["level"] == 7 && out[4]["rev"] == 4);
    CHECK(out[5]["op"] == "seat" && out[5]["kind"] == "open" && !out[5].count("level"));
    for (const auto& f : out) CHECK(!frame_text(f).empty());
  }

  // ---- a guest: no shared ops, Start greyed out as NotHost ----
  {
    Session s(opts("b", false));
    s.receive(state(4, 0, {seat_human(0, "a", true), seat_human(1, "b", true), seat_open(2), seat_open(3)}));
    CHECK(s.my_seat() == 1 && !s.is_host());
    CHECK(!s.set_rules(Rules{}) && !s.set_cpu(2, 3) && !s.start(0));
    CHECK(s.can_start() == StartBlock::NotHost);
    CHECK(s.pick(Pick{1, 1, 0}));               // own picks are anyone's
    CHECK(lobby_frames(s.due(now)).size() == 1);
  }

  // ---- revision: rules, seat and CPU picks go again on the new rev; start never does ----
  {
    Session s(opts("a", false));
    s.receive(state(4, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_open(2), seat_open(3)}));
    Rules r; r.stocks = 2;
    CHECK(s.set_rules(r));
    CHECK(lobby_frames(s.due(now)).size() == 1);
    // Someone joined meanwhile (rev 5): Rooms refuses rules on rev 4.
    s.receive(refusal(state(5, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_human(2, "c"), seat_open(3)}),
                      "revision", "rules"));
    auto out = lobby_frames(s.due(now));
    CHECK(out.size() == 1 && out[0]["op"] == "rules" && out[0]["rev"] == 5 && out[0]["rules"]["stocks"] == 2);
    // Bounded: three resends at most.
    for (int i = 0; i < 5; ++i)
      s.receive(refusal(state(6 + i, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_human(2, "c"), seat_open(3)}),
                        "revision", "rules"));
    CHECK(lobby_frames(s.due(now)).size() == 2);
    // Start on a stale rev: refused, not resent (the host presses again on the new roster).
    s.receive(state(20, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_human(2, "c", true), seat_open(3)}));
    CHECK(s.start(0));
    out = lobby_frames(s.due(now));
    CHECK(out.size() == 1 && out[0]["op"] == "start" && out[0]["rev"] == 20 && out[0]["stage"] == "random");
    CHECK(!out[0].count("inputDelay"));
    s.receive(refusal(state(21, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_human(2, "c"), seat_open(3)}),
                      "revision", "start"));
    CHECK(lobby_frames(s.due(now)).empty());
    CHECK(s.refusal() == "revision" && s.refusal_action() == "start");
    // A seat the host wanted for a CPU that a human took meanwhile is not resent either.
    s.receive(state(22, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_open(2), seat_open(3)}));
    CHECK(s.set_cpu(2, 4));
    CHECK(lobby_frames(s.due(now)).size() == 1);
    s.receive(refusal(state(23, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_human(2, "c"), seat_open(3)}),
                      "revision", "seat"));
    CHECK(lobby_frames(s.due(now)).empty());
  }

  // ---- Start's reasons ----
  {
    Session s(opts("a", false));
    s.receive(state(1, 0, {seat_human(0, "a"), seat_open(1), seat_open(2), seat_open(3)}));
    CHECK(s.can_start() == StartBlock::PickRequired);
    s.receive(state(2, 0, {seat_human(0, "a", true), seat_open(1), seat_open(2), seat_open(3)}));
    CHECK(s.can_start() == StartBlock::PlayersRequired);
    s.receive(state(3, 0, {seat_human(0, "a", true), seat_cpu(1, 3), seat_open(2), seat_open(3)}));
    CHECK(s.can_start() == StartBlock::PickRequired);   // the CPU has no pick yet
    s.receive(state(4, 0, {seat_human(0, "a", true), seat_cpu(1, 3, true), seat_open(2), seat_open(3)}));
    CHECK(s.can_start() == StartBlock::HumansRequired);  // one human and CPUs: Rooms says humans_required
    CHECK(!s.start(31));
    CHECK(known_refusal("humans_required"));
    s.receive(state(5, 0, {seat_human(0, "a", true), seat_cpu(1, 3, true), seat_human(2, "b", true), seat_open(3)}));
    CHECK(s.can_start() == StartBlock::None);            // two humans and a CPU may start
    CHECK(!s.start(21) && !s.start(26) && !s.start(1));
    // Teams on, everyone on one team: greyed out (Rooms: teams_required).
    s.receive(state(6, 0, {seat_human(0, "a", true, 1), seat_cpu(1, 3, true, 1), seat_human(2, "b", true, 1), seat_open(3)}, true));
    CHECK(s.can_start() == StartBlock::TeamsRequired);
    CHECK(!s.start(0));
    s.receive(state(7, 0, {seat_human(0, "a", true, 1), seat_cpu(1, 3, true, 0), seat_human(2, "b", true, 1), seat_open(3)}, true));
    CHECK(s.can_start() == StartBlock::None);
    CHECK(s.start(31));
    auto out = lobby_frames(s.due(now));
    CHECK(out.size() == 1 && out[0]["stage"] == 31);
  }

  // ---- start's inputDelay: sent when asked, dropped once if Rooms has none yet ----
  {
    Session s(opts("a", false, 2));
    s.receive(state(4, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_human(2, "c", true), seat_open(3)}));
    CHECK(s.start(0));
    auto out = lobby_frames(s.due(now));
    CHECK(out.size() == 1 && out[0]["inputDelay"] == 2);
    s.receive(refusal(state(4, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_human(2, "c", true), seat_open(3)}), "shape", "start"));
    out = lobby_frames(s.due(now));
    CHECK(out.size() == 1 && out[0]["op"] == "start" && !out[0].count("inputDelay") && out[0]["rev"] == 4);
    CHECK(!s.input_delay_supported());
    CHECK(s.start(0));
    out = lobby_frames(s.due(now));
    CHECK(out.size() == 1 && !out[0].count("inputDelay"));
  }

  // ---- digest_missing: hello again ----
  {
    Session s(opts("a"));
    s.receive(state(4, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_human(2, "c", true), seat_open(3)}));
    CHECK(lobby_frames(s.due(now)).size() == 1);   // the first hello
    CHECK(s.start(0));
    s.due(now);
    s.receive(refusal(state(4, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_human(2, "c", true), seat_open(3)}), "digest_missing", "start"));
    auto out = lobby_frames(s.due(now));
    CHECK(out.size() == 1 && out[0]["op"] == "hello");
    CHECK(known_refusal("digest_mismatch") && known_refusal("teams_required") && !known_refusal("other"));
  }

  // ---- Friends: the guests follow the host to the stage select and back ----
  {
    const auto seats = std::vector<json>{seat_human(0, "a", true), seat_cpu(1, 5, true), seat_human(2, "c", true), seat_open(3)};
    Session host(opts("a", false));
    host.receive(state(4, 0, seats));
    host.due(now);
    CHECK(host.stage_select(true));
    auto out = lobby_frames(host.due(now));
    CHECK(out.size() == 1 && out[0]["op"] == "stageSelect" && out[0]["open"] == true && out[0]["rev"] == 4);
    json on = state(5, 0, seats);
    on["stageSelect"] = true;
    host.receive(on);
    CHECK(host.state().stage_select);
    // A refused Start leaves the host on the select, so the guests must be let go too.
    CHECK(host.start(0));
    host.due(now);
    host.receive(refusal(on, "player_away", "start"));
    out = lobby_frames(host.due(now));
    CHECK(out.size() == 1 && out[0]["op"] == "stageSelect" && out[0]["open"] == false && out[0]["rev"] == 5);
    host.receive(state(6, 0, seats));   // the key is gone
    CHECK(!host.state().stage_select);
    // Start that can't go out (someone sat down unpicked meanwhile) lets the guests go as well.
    json late = state(7, 0, {seat_human(0, "a", true), seat_human(1, "b", false), seat_human(2, "c", true), seat_open(3)});
    late["stageSelect"] = true;
    host.receive(late);
    host.due(now);
    CHECK(host.can_start() != StartBlock::None && !host.start(0));
    out = lobby_frames(host.due(now));
    CHECK(out.size() == 1 && out[0]["op"] == "stageSelect" && out[0]["open"] == false && out[0]["rev"] == 7);
    Session guest(opts("c", false));
    guest.receive(on);
    guest.due(now);
    CHECK(!guest.stage_select(false) && lobby_frames(guest.due(now)).empty());   // only the host says
    json bad = on;
    bad["stageSelect"] = 1;
    Session strict(opts("c", false));
    strict.receive(bad);
    CHECK(strict.take_bad_frame() != nullptr);
  }

  // ---- ready: once Rooms shows it on any seat, Start waits for every human's hello ----
  {
    Session s(opts("a", false));
    json st = state(4, 0, {seat_human(0, "a", true), seat_human(1, "b", true), seat_open(2), seat_open(3)});
    s.receive(st);
    CHECK(s.can_start() == StartBlock::None);         // a room before hello shows no ready at all
    st["rev"] = 5; st["seats"][0]["ready"] = true;
    s.receive(st);
    CHECK(s.state().seats[0].ready && s.state().seats[0].ready_shown && !s.state().seats[1].ready);
    CHECK(s.can_start() == StartBlock::NotReady);
    CHECK(!s.start(0));
    st["rev"] = 6; st["seats"][1]["ready"] = true;
    s.receive(st);
    CHECK(s.can_start() == StartBlock::None);
    st["rev"] = 7; st["seats"][1].erase("ready");     // b's game took the seat again: no hello yet
    s.receive(st);
    CHECK(s.can_start() == StartBlock::NotReady);
    json bad = st; bad["seats"][0]["ready"] = "yes";
    Session other(opts("a", false));
    other.receive(bad);
    CHECK(!other.known());
    // player_away: someone is inside their 5 s lobby grace. Carried through, not resent.
    st["seats"][1]["ready"] = true;
    s.receive(st);
    CHECK(s.start(0));
    s.due(now);
    s.receive(refusal(st, "player_away", "start"));
    CHECK(s.refusal() == "player_away" && known_refusal("player_away"));
    CHECK(lobby_frames(s.due(now)).empty());
  }

  // ---- hover: on change only, at most one per 100 ms, the newest flushed ----
  {
    Session s(opts("a", false));
    s.receive(state(1, 0, {seat_human(0, "a"), seat_open(1), seat_open(2), seat_open(3)}));
    uint64_t t = now;
    CHECK(s.hover(Pick{1, 0, 0}));
    auto out = lobby_frames(s.due(t));
    CHECK(out.size() == 1 && out[0]["op"] == "hover" && out[0]["char"] == 1 && !out[0].count("rev"));
    CHECK(s.hover(Pick{1, 0, 0}));                  // unchanged: nothing
    CHECK(lobby_frames(s.due(t + 200)).empty());
    s.hover(Pick{2, 0, 0});
    s.hover(Pick{3, 0, 0});
    CHECK(lobby_frames(s.due(t + 250)).size() == 1);   // 250 ms since the last: goes now, the newest
    s.hover(Pick{4, 0, 0});
    s.hover(Pick{5, 0, 0});
    CHECK(lobby_frames(s.due(t + 300)).empty());     // 50 ms: held
    out = lobby_frames(s.due(t + 350));              // the trailing flush, with the newest
    CHECK(out.size() == 1 && out[0]["char"] == 5);
    s.hover(Pick{6, 0, 0});
    s.hover(Pick{5, 0, 0});                          // back to what Rooms already has: nothing to send
    CHECK(lobby_frames(s.due(t + 1000)).empty());
    CHECK(!s.hover(Pick{26, 0, 0}));
    // A reconnect brings picks and rules, not hovers: the state simply has none.
    json st = state(2, 0, {seat_human(0, "a", true), seat_open(1), seat_open(2), seat_open(3)});
    s.receive(st);
    CHECK(!s.state().seats[0].hovering && s.state().seats[0].picked);
  }

  // ---- keepalive: a ping when nothing went out for 5 s ----
  {
    Session s(opts("a", false));
    s.receive(state(1, 0, {seat_human(0, "a"), seat_open(1), seat_open(2), seat_open(3)}));
    auto out = s.due(now);
    CHECK(out.size() == 1 && out[0]["t"] == "ping");
    CHECK(s.due(now + 4999).empty());
    out = s.due(now + 5000);
    CHECK(out.size() == 1 && out[0]["t"] == "ping" && out[0]["at"] == now + 5000);
    s.note_sent(now + 9000);
    CHECK(s.due(now + 13999).empty());
    CHECK(s.due(now + 14000).size() == 1);
    CHECK(kKeepaliveMs * 3 <= 15000);   // two missed pings still under Rooms' eviction
  }

  // ---- directOpen triggers: each other human who takes a seat, once; again after a replacement ----
  {
    Session s(opts("a", false));
    s.receive(state(1, 0, {seat_human(0, "a"), seat_open(1), seat_open(2), seat_open(3)}));
    CHECK(s.take_seated().empty());
    s.receive(state(2, 0, {seat_human(0, "a"), seat_human(1, "b"), seat_cpu(2, 3), seat_open(3)}));
    auto seated = s.take_seated();
    CHECK(seated.size() == 1 && seated[0].seat == 1 && seated[0].id == "b");
    s.receive(state(3, 0, {seat_human(0, "a"), seat_human(1, "b"), seat_cpu(2, 3), seat_human(3, "d")}));
    seated = s.take_seated();
    CHECK(seated.size() == 1 && seated[0].id == "d");
    s.receive(state(4, 0, {seat_human(0, "a"), seat_human(1, "e"), seat_cpu(2, 3), seat_human(3, "d")}));
    seated = s.take_seated();
    CHECK(seated.size() == 1 && seated[0].seat == 1 && seated[0].id == "e");
  }

  // ---- host handover: the state names the host; the old host loses its controls ----
  {
    Session s(opts("b", false));
    s.receive(state(1, 0, {seat_human(0, "a", true), seat_human(1, "b", true), seat_open(2), seat_open(3)}));
    CHECK(!s.is_host());
    s.receive(state(2, 1, {seat_open(0), seat_human(1, "b", true), seat_cpu(2, 4, true), seat_open(3)}));
    CHECK(s.is_host() && s.can_start() == StartBlock::HumansRequired);   // host now, but alone with a CPU
  }

  // ---- the setup: from its broadcast (or a state carrying it); the lobby closes ----
  {
    Session s(opts("a", false));
    s.receive(state(4, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_open(2), seat_open(3)}));
    json setup = {{"t", "yg.mu.lobby"}, {"v", 1}, {"op", "setup"}, {"rev", 4}, {"matchId", "m1"}, {"seed", 99},
                  {"seats", {{{"seat", 0}, {"port", 0}, {"kind", "human"}, {"id", "a"}, {"name", "a"}, {"char", 2}, {"color", 0}, {"team", 0}},
                             {{"seat", 1}, {"port", 1}, {"kind", "cpu"}, {"id", "cpu:1"}, {"name", "CPU 2"}, {"char", 9}, {"color", 1}, {"team", 0}, {"level", 5}}}},
                  {"rules", {{"teams", false}, {"friendlyFire", false}, {"mode", "stock"}, {"stocks", 4}, {"minutes", 8}, {"pause", false}}},
                  {"stage", 28}};
    CHECK(s.receive(setup));
    CHECK(s.has_setup() && s.setup().match_id == "m1" && s.setup().seed == 99 && s.setup().stage == 28);
    CHECK(!s.lobby_open() && s.can_start() == StartBlock::MatchActive && !s.pick(Pick{1, 1, 0}));
    // A refusal of a late frame (match_active) changes nothing; Rooms' state then carries the frozen setup.
    json st = state(4, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_open(2), seat_open(3)});
    json frozen = setup;
    for (const char* k : {"t", "v", "op", "rev", "matchId"}) frozen.erase(k);
    json active = st; active["setup"] = frozen;
    CHECK(s.receive(refusal(active, "match_active", "pick")));
    CHECK(s.has_setup() && s.setup().match_id == "m1");
    // A reconnecting game reads it from the state's frozen setup.
    Session late(opts("a", false));
    json mid = st;
    mid["setup"] = frozen;
    late.receive(mid);
    CHECK(late.has_setup() && late.setup().seed == 99 && late.setup().match_id.empty());
    // A bad lobby frame is reported once, never adopted.
    Session bad(opts("a", false));
    json broken = st; broken["seats"][1]["level"] = 12;
    CHECK(bad.receive(broken));
    CHECK(!bad.known());
    CHECK(bad.take_bad_frame() != nullptr && bad.take_bad_frame() == nullptr);
  }

  // ---- a refused setup: a guest ends that match in the room once; the host never sends it here ----
  {
    json st = state(4, 0, {seat_human(0, "a", true), seat_human(1, "b", true), seat_open(2), seat_open(3)});
    json setup = {{"t", "yg.mu.lobby"}, {"v", 1}, {"op", "setup"}, {"rev", 4}, {"matchId", "m1"}, {"seed", 5},
                  {"seats", {{{"seat", 0}, {"port", 0}, {"kind", "human"}, {"id", "a"}, {"name", "a"}, {"char", 2}, {"color", 0}, {"team", 0}},
                             {{"seat", 1}, {"port", 1}, {"kind", "human"}, {"id", "b"}, {"name", "b"}, {"char", 2}, {"color", 0}, {"team", 0}}}},
                  {"rules", {{"teams", false}, {"friendlyFire", false}, {"mode", "stock"}, {"stocks", 4}, {"minutes", 8}, {"pause", false}}},
                  {"stage", 28}};
    const auto ends = [&](Session& x) {
      int n = 0;
      for (const auto& f : x.due(now)) if (f.value("t", "") == "end-session") { ++n; CHECK(f.value("sessionId", "") == "m1"); }
      return n;
    };
    Session guest(opts("b", false));
    guest.receive(st); guest.receive(setup); guest.due(now);
    CHECK(guest.has_setup() && !guest.is_host());
    guest.refuse_setup();
    CHECK(ends(guest) == 1);
    guest.refuse_setup();
    CHECK(ends(guest) == 0);                            // once per setup
    Session host(opts("a", false));
    host.receive(st); host.receive(setup); host.due(now);
    CHECK(host.is_host());
    host.refuse_setup();
    CHECK(ends(host) == 0);                             // the host calls it off through next_match
    Session played(opts("b", false));
    played.receive(st); played.receive(setup); played.due(now);
    played.match_began();
    played.refuse_setup();
    CHECK(ends(played) == 0);                           // a match that began is never refused
  }

  // ---- next match: the room stays; the old setup is never taken again, the next one is ----
  {
    Session s(opts("a", false));
    json st = state(4, 0, {seat_human(0, "a", true), seat_human(1, "b", true), seat_open(2), seat_open(3)});
    s.receive(st);
    json setup = {{"t", "yg.mu.lobby"}, {"v", 1}, {"op", "setup"}, {"rev", 4}, {"matchId", "m1"}, {"seed", 5},
                  {"seats", {{{"seat", 0}, {"port", 0}, {"kind", "human"}, {"id", "a"}, {"name", "a"}, {"char", 2}, {"color", 0}, {"team", 0}},
                             {{"seat", 1}, {"port", 1}, {"kind", "human"}, {"id", "b"}, {"name", "b"}, {"char", 2}, {"color", 0}, {"team", 0}}}},
                  {"rules", {{"teams", false}, {"friendlyFire", false}, {"mode", "stock"}, {"stocks", 4}, {"minutes", 8}, {"pause", false}}},
                  {"stage", 28}};
    s.receive(setup);
    json frozen = setup;
    for (const char* k : {"t", "v", "op", "rev", "matchId"}) frozen.erase(k);
    json mid = st; mid["rev"] = 5; mid["setup"] = frozen;
    s.receive(mid);
    CHECK(s.has_setup() && s.setup().match_id == "m1");
    s.match_began();                                    // the match was played
    s.due(now);
    s.next_match();
    for (const auto& f : s.due(now)) CHECK(f.value("t", "") != "end-session");   // nothing to call off
    CHECK(!s.has_setup() && s.lobby_open() == false);   // Rooms still shows the old frozen setup
    s.receive(setup);                                   // a late copy of the old broadcast
    s.receive(mid);                                     // and the old frozen setup
    CHECK(!s.has_setup());
    json back = st; back["rev"] = 6;                    // Rooms clears it: the lobby is open again
    s.receive(back);
    CHECK(!s.has_setup() && s.lobby_open() && s.state().seats[1].picked && s.can_start() == StartBlock::None);
    CHECK(s.start(0));
    json next = setup; next["matchId"] = "m2"; next["rev"] = 6; next["stage"] = 31;
    s.receive(next);
    CHECK(s.has_setup() && s.setup().match_id == "m2" && s.setup().stage == 31);
    // A reconnecting game that only sees a frozen setup after an open lobby takes it.
    s.next_match();
    s.receive(back);
    json frozen2 = next;
    for (const char* k : {"t", "v", "op", "rev", "matchId"}) frozen2.erase(k);
    json mid2 = st; mid2["rev"] = 7; mid2["setup"] = frozen2;
    s.receive(mid2);
    CHECK(s.has_setup() && s.setup().stage == 31);
  }

  // ---- a match dropped before its splash: a state with no setup lets the setup go (the select is back);
  //      once the match began (game_started), only next_match does ----
  {
    Session s(opts("a", false));
    json st = state(4, 0, {seat_human(0, "a", true), seat_human(1, "b", true), seat_open(2), seat_open(3)});
    s.receive(st);
    json setup = {{"t", "yg.mu.lobby"}, {"v", 1}, {"op", "setup"}, {"rev", 4}, {"matchId", "m1"}, {"seed", 5},
                  {"seats", {{{"seat", 0}, {"port", 0}, {"kind", "human"}, {"id", "a"}, {"name", "a"}, {"char", 2}, {"color", 0}, {"team", 0}},
                             {{"seat", 1}, {"port", 1}, {"kind", "human"}, {"id", "b"}, {"name", "b"}, {"char", 2}, {"color", 0}, {"team", 0}}}},
                  {"rules", {{"teams", false}, {"friendlyFire", false}, {"mode", "stock"}, {"stocks", 4}, {"minutes", 0}, {"pause", false}}},
                  {"stage", 28}};
    s.match_began();                                   // nothing to begin yet: no effect
    CHECK(!s.began());
    CHECK(s.receive(setup));
    CHECK(s.has_setup() && s.setup().rules.minutes == 0 && !s.began());
    json dropped = st; dropped["rev"] = 5;             // Rooms called the match off before anyone's splash
    s.receive(dropped);
    CHECK(!s.has_setup() && s.lobby_open() && s.can_start() == StartBlock::None);
    CHECK(s.start(0));
    json again = setup; again["matchId"] = "m2"; again["rev"] = 5;
    s.receive(again);
    CHECK(s.has_setup() && s.setup().match_id == "m2");
    s.match_began();
    CHECK(s.began());
    json after = st; after["rev"] = 6;                 // a state without the setup mid-match keeps it
    s.receive(after);
    CHECK(s.has_setup() && s.setup().match_id == "m2");
    s.next_match();
    CHECK(!s.has_setup() && !s.began());
  }

  // ---- a pre-match drop the room still holds (a link dropped or the setup hash disagreed before frame 1;
  //      Rooms keeps the match and its frozen setup): after next_match the select is still MatchActive,
  //      the host calls the match off once, a guest only waits; a state without the setup opens it ----
  for (const bool host : {true, false}) {
    Session s(opts(host ? "a" : "b", false));
    json st = state(4, 0, {seat_human(0, "a", true), seat_human(1, "b", true), seat_open(2), seat_open(3)});
    s.receive(st);
    json setup = {{"t", "yg.mu.lobby"}, {"v", 1}, {"op", "setup"}, {"rev", 4}, {"matchId", "m1"}, {"seed", 5},
                  {"seats", {{{"seat", 0}, {"port", 0}, {"kind", "human"}, {"id", "a"}, {"name", "a"}, {"char", 2}, {"color", 0}, {"team", 0}},
                             {{"seat", 1}, {"port", 1}, {"kind", "human"}, {"id", "b"}, {"name", "b"}, {"char", 2}, {"color", 0}, {"team", 0}}}},
                  {"rules", {{"teams", false}, {"friendlyFire", false}, {"mode", "stock"}, {"stocks", 4}, {"minutes", 8}, {"pause", false}}},
                  {"stage", 28}};
    s.receive(setup);
    json frozen = setup;
    for (const char* k : {"t", "v", "op", "rev", "matchId"}) frozen.erase(k);
    json held = st; held["rev"] = 5; held["setup"] = frozen;
    s.receive(held);
    CHECK(s.has_setup() && !s.began());
    s.due(now);
    s.next_match();                                    // the drop's recovery: re-adopt, then next_match
    CHECK(!s.has_setup());
    CHECK(s.can_start() == (host ? StartBlock::MatchActive : StartBlock::NotHost));
    CHECK(!s.lobby_open());
    int call_offs = 0;
    for (const auto& f : s.due(now))
      if (f.value("t", "") == "end-session") { ++call_offs; CHECK(f.value("sessionId", "") == "m1"); }
    CHECK(call_offs == (host ? 1 : 0));
    s.receive(held);                                   // Rooms still holds it: the old setup stays off
    CHECK(!s.has_setup() && !s.lobby_open());
    if (host) CHECK(s.can_start() == StartBlock::MatchActive);
    s.next_match();                                    // a second recovery has nothing left to call off
    for (const auto& f : s.due(now)) CHECK(f.value("t", "") != "end-session");
    json open = st; open["rev"] = 6;                   // Rooms dropped it: the select is open again
    s.receive(open);
    CHECK(s.lobby_open() && !s.has_setup());
    CHECK(s.can_start() == (host ? StartBlock::None : StartBlock::NotHost));
  }
  // ---- the call-off survives a host who moves before frame 1, and a re-joined socket ----
  {
    json st = state(4, 0, {seat_human(0, "a", true), seat_human(1, "b", true), seat_open(2), seat_open(3)});
    json setup = {{"t", "yg.mu.lobby"}, {"v", 1}, {"op", "setup"}, {"rev", 4}, {"matchId", "m1"}, {"seed", 5},
                  {"seats", {{{"seat", 0}, {"port", 0}, {"kind", "human"}, {"id", "a"}, {"name", "a"}, {"char", 2}, {"color", 0}, {"team", 0}},
                             {{"seat", 1}, {"port", 1}, {"kind", "human"}, {"id", "b"}, {"name", "b"}, {"char", 2}, {"color", 0}, {"team", 0}}}},
                  {"rules", {{"teams", false}, {"friendlyFire", false}, {"mode", "stock"}, {"stocks", 4}, {"minutes", 8}, {"pause", false}}},
                  {"stage", 28}};
    json frozen = setup;
    for (const char* k : {"t", "v", "op", "rev", "matchId"}) frozen.erase(k);
    json held = st; held["rev"] = 5; held["setup"] = frozen;
    const auto call_offs = [&](Session& s) {
      int n = 0;
      for (const auto& f : s.due(now))
        if (f.value("t", "") == "end-session") { ++n; CHECK(f.value("sessionId", "") == "m1"); }
      return n;
    };

    // The host moves to the guest before frame 1: the guest's pending call-off goes out once it is host.
    Session g(opts("b", false));
    g.receive(st); g.receive(setup); g.receive(held);
    g.due(now);
    g.next_match();
    CHECK(g.pending_call_off() == "m1" && call_offs(g) == 0);   // a guest only waits
    json moved = held; moved["rev"] = 6; moved["host"] = 1;
    g.receive(moved);
    CHECK(g.is_host() && !g.has_setup() && g.can_start() == StartBlock::MatchActive);
    CHECK(call_offs(g) == 1);
    json moved2 = moved; moved2["rev"] = 7;
    g.receive(moved2);
    CHECK(call_offs(g) == 0);                                    // once
    json open = st; open["rev"] = 8; open["host"] = 1;
    g.receive(open);                                             // Rooms let it go: cleared
    CHECK(g.pending_call_off().empty() && g.lobby_open() && g.can_start() == StartBlock::None);
    json next = setup; next["matchId"] = "m2"; next["rev"] = 8;
    json frozen2 = next;
    for (const char* k : {"t", "v", "op", "rev", "matchId"}) frozen2.erase(k);
    json held2 = open; held2["rev"] = 9; held2["setup"] = frozen2;
    g.receive(next); g.receive(held2);
    CHECK(g.has_setup() && g.setup().match_id == "m2" && call_offs(g) == 0);   // the next match is untouched

    // The host's room socket closed after the setup: the session that replaces it on the re-join carries
    // the call-off, treats the room's frozen setup (no matchId) as stale instead of unusable, and calls it
    // off as host. A guest re-joining the same way waits, and calls it off if it becomes the host.
    Session old(opts("a", false));
    old.receive(st); old.receive(setup); old.receive(held);
    CHECK(old.has_setup() && !old.began());
    const Session::CallOff carried = old.call_off();
    CHECK(carried.id == "m1" && carried.setup == frozen);
    for (const bool host : {true, false}) {
      Session::Options o = opts(host ? "a" : "b", false);
      o.call_off_id = carried.id; o.call_off_setup = carried.setup;
      Session fresh(o);
      fresh.receive(held);
      CHECK(!fresh.has_setup() && !fresh.lobby_open());         // stale, never planned (no kSetupUnusable)
      CHECK(call_offs(fresh) == (host ? 1 : 0));
      fresh.receive(setup);                                     // a late broadcast of the called-off match
      CHECK(!fresh.has_setup());
      if (!host) {
        fresh.receive(moved);
        CHECK(fresh.is_host() && !fresh.has_setup() && call_offs(fresh) == 1);
      }
      json back = st; back["rev"] = 8;
      fresh.receive(back);
      CHECK(fresh.lobby_open() && fresh.pending_call_off().empty() && call_offs(fresh) == 0);
    }
    Session played(opts("a", false));                           // a match that began has nothing to carry
    played.receive(st); played.receive(setup); played.match_began();
    CHECK(played.call_off().id.empty());
    Session fresh(opts("a", false));                            // and nothing carried: the frozen setup is taken
    fresh.receive(held);
    CHECK(fresh.has_setup() && call_offs(fresh) == 0);
  }

  // ---- refusal_count: once per refusal, never for a plain state ----
  {
    Session s(opts("a", false));
    json st = state(1, 0, {seat_human(0, "a"), seat_open(1), seat_open(2), seat_open(3)});
    s.receive(st);
    CHECK(s.refusal_count() == 0);
    s.receive(refusal(st, "players_required", "start"));
    s.receive(st);
    s.receive(refusal(st, "players_required", "start"));
    CHECK(s.refusal_count() == 2);
  }

  // ---- leave: the lobby's, then the room's; a room without the lobby ----
  {
    Session s(opts("a", false));
    s.receive(state(1, 0, {seat_human(0, "a"), seat_open(1), seat_open(2), seat_open(3)}));
    s.leave();
    auto out = s.due(now);
    CHECK(out.size() == 2 && out[0]["op"] == "leave" && out[1] == json({{"t", "leave"}}));
    CHECK(!s.pick(Pick{1, 1, 0}));
    Session none(opts("a", false));
    CHECK(none.receive(json{{"t", "yg.mu.lobby"}, {"v", 1}, {"op", "state"}, {"error", "unsupported"}}));
    CHECK(none.unsupported() && !none.known());
  }

  // ---- the screen's handle: intents run on the search thread's turn, then the view ----
  {
    Session s(opts("a", false));
    Control c;
    s.receive(state(4, 0, {seat_human(0, "a", true), seat_cpu(1, 5, true), seat_human(2, "c", true), seat_open(3)}));
    c.post([](Session& x) { x.start(0); });
    CHECK(!c.view().known);
    c.run(s);
    CHECK(c.view().known && c.view().host && c.view().my_seat == 0 && c.view().start == StartBlock::None);
    auto out = lobby_frames(s.due(now));
    CHECK(out.size() == 1 && out[0]["op"] == "start");
  }

  // ---- early links: directOpen per seated peer, kept, closed when they leave ----
  {
    std::vector<json> calls;
    unsigned next = 7;
    auto request = [&](const json& q, int) -> json {
      calls.push_back(q);
      if (q["type"] == "directOpen") return {{"link", next++}};
      return json::object();
    };
    EarlyLinks links;
    CHECK(links.open(3, "conn-b", request));
    CHECK(links.open(3, "conn-b", request));   // once per peer
    CHECK(calls.size() == 1 && calls[0] == json({{"type", "directOpen"}, {"conn", 3}, {"peer", "conn-b"}}));
    auto ids = links.for_peers(3, {"conn-d", "conn-b"}, request);
    CHECK(ids.size() == 2 && ids[0] == 8 && ids[1] == 7);
    links.forget("conn-b", request);
    CHECK(calls.back() == json({{"type", "directClose"}, {"link", 7}}));
    {  // A link that died in the lobby (the friend's game couldn't answer yet) is closed and opened again.
      EarlyLinks waited;
      CHECK(waited.open(3, "conn-dead", request));
      CHECK(waited.open(3, "conn-live", request));
      const unsigned dead = waited.by_peer["conn-dead"], live = waited.by_peer["conn-live"];
      waited.forget_dead([&](unsigned id) { return id == dead ? 3 : 2; }, request);
      CHECK(calls.back() == json({{"type", "directClose"}, {"link", dead}}));
      CHECK(!waited.by_peer.count("conn-dead") && waited.by_peer["conn-live"] == live);
      auto again = waited.for_peers(3, {"conn-dead", "conn-live"}, request);
      CHECK(again.size() == 2 && again[0] != dead && again[1] == live);
    }
    auto refuse = [](const json&, int) -> json { throw std::runtime_error("no_direct"); };
    CHECK(!links.open(3, "conn-x", refuse));
    CHECK(links.for_peers(3, {"conn-x"}, refuse).empty());
    // The forced update is never swallowed as "no link".
    struct Update : std::runtime_error { Update() : std::runtime_error("update") {} };
    auto update = [](const json&, int) -> json { throw Update(); };
    bool thrown = false;
    try { links.open<Update>(3, "conn-y", update); } catch (const Update&) { thrown = true; }
    CHECK(thrown && !links.by_peer.count("conn-y"));
    thrown = false;
    try { links.for_peers<Update>(3, {"conn-y"}, update); } catch (const Update&) { thrown = true; }
    CHECK(thrown);
    // Match start: a seated friend who isn't in the match (conn-d) has their link closed; the match's
    // links are opened where missing and handed over, so nothing is left here to close twice.
    std::vector<json> closes;
    auto close = [&](const json& q, int) -> json { closes.push_back(q); return json::object(); };
    CHECK(links.open(3, "conn-b", request));
    const unsigned b_link = links.by_peer["conn-b"];
    auto handed = links.hand_over(3, {"conn-b", "conn-e"}, request, close);
    CHECK(handed.size() == 2 && handed[0] == b_link && links.by_peer.empty());
    CHECK(closes.size() == 1 && closes[0] == json({{"type", "directClose"}, {"link", 8}}));
    // One that can't open: nothing handed over, the links stay to be closed with the lobby.
    CHECK(links.open(3, "conn-b", request));
    CHECK(links.hand_over(3, {"conn-b", "conn-x"}, refuse, close).empty() && links.by_peer.count("conn-b"));
    links.forget_all(close);
    CHECK(links.by_peer.empty() && closes.back()["type"] == "directClose");
  }
  // ---- six seats (Triples): the room's seats, not four, bound every seat the session acts on ----
  {
    Session s(opts("e"));
    auto six = [](int64_t rev, int host, std::vector<json> seats) {
      return state(rev, host, std::move(seats));
    };
    CHECK(s.receive(six(1, 4, {seat_human(0, "a", true), seat_human(1, "b", true), seat_human(2, "c", true),
                               seat_human(3, "d", true), seat_human(4, "e"), seat_open(5)})));
    CHECK(s.known() && s.size() == kSeatsMax && s.my_seat() == 4 && s.is_host());
    (void) s.due(now);
    CHECK(s.hover(Pick{2, 0, 0}));
    auto out = lobby_frames(s.due(now + 1000));
    CHECK(out.size() == 1 && out[0]["op"] == "hover" && out[0]["seat"] == 4);
    CHECK(s.pick(Pick{2, 0, 0}));
    out = lobby_frames(s.due(now + 2000));
    CHECK(out.size() == 1 && out[0]["op"] == "pick" && out[0]["seat"] == 4);
    CHECK(s.set_cpu(5, 7));                                    // the host's sixth seat
    out = lobby_frames(s.due(now + 3000));
    CHECK(out.size() == 1 && out[0]["op"] == "seat" && out[0]["seat"] == 5 && out[0]["level"] == 7);
    CHECK(!s.set_cpu(6, 7));
  }
  {
    // A four-seat room: seats 4 and 5 are nobody's, as before.
    Session s(opts("a"));
    CHECK(s.receive(state(1, 0, {seat_human(0, "a"), seat_open(1), seat_open(2), seat_open(3)})));
    CHECK(s.size() == kSeats && s.is_host());
    CHECK(!s.set_cpu(4, 3) && !s.set_cpu(5, 3) && s.set_cpu(3, 3));
  }
  {
    // Six players picked, Teams on with three on each team: Start is offered; one team: not.
    Session s(opts("a"));
    std::vector<json> seats;
    for (int k = 0; k < 6; ++k) seats.push_back(seat_human(k, std::string(1, char('a' + k)), true, k < 3 ? 0 : 1));
    CHECK(s.receive(state(1, 0, seats, true)));
    CHECK(s.can_start() == StartBlock::None);
    for (auto& j : seats) j["pick"]["team"] = 0;
    CHECK(s.receive(state(2, 0, seats, true)));
    CHECK(s.can_start() == StartBlock::TeamsRequired);
  }
  std::printf("ok\n");
  return 0;
}
