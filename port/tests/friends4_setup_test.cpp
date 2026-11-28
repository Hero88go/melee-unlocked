// SPDX-License-Identifier: GPL-2.0-or-later
// The 4-player Friends lobby's match from the room's frozen setup (yougame_lobby_setup.h): checks on
// the setup, the game-info block it writes (ports, CPUs, teams, stocks or time, stage, pause, friendly
// fire), the same block from any earlier block, the CPU-aware costume indices, the compatibility key
// and the build capability.
#include "yougame_lobby_setup.h"
#include "mu_friends_lobby.h"
#include "yougame_friends.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

using namespace yougame::lobby;

static const size_t kBlock = 0x138;

static json setup_frame(bool teams = false, const char* mode = "stock") {
  json seats = json::array();
  seats.push_back({{"seat", 2}, {"port", 0}, {"kind", "human"}, {"id", "human-a"}, {"name", "A"}, {"char", 2}, {"color", 1}, {"team", teams ? 1 : 0}});
  seats.push_back({{"seat", 0}, {"port", 1}, {"kind", "human"}, {"id", "human-b"}, {"name", "B"}, {"char", 2}, {"color", 1}, {"team", teams ? 1 : 0}});
  seats.push_back({{"seat", 1}, {"port", 2}, {"kind", "cpu"}, {"id", "cpu:1"}, {"name", "CPU 2"}, {"char", 9}, {"color", 0}, {"team", teams ? 2 : 0}, {"level", 9}});
  return {{"t", "yg.mu.lobby"}, {"v", 1}, {"op", "setup"}, {"rev", 12}, {"matchId", "match-1"}, {"seed", 7},
          {"seats", seats},
          {"rules", {{"teams", teams}, {"friendlyFire", true}, {"mode", mode}, {"stocks", 3}, {"minutes", 5}, {"pause", false}}},
          {"stage", 31}};
}
static Setup setup_of(const json& f) {
  Incoming in = decode(f);
  CHECK(in.op == Op::Setup && !in.why);
  return in.setup;
}
static const uint8_t* rec(const std::vector<uint8_t>& b, int port) { return b.data() + 0x60 + port * 0x24; }

int main() {
  // ---- the plan ----
  Plan p = plan(setup_of(setup_frame()), "human-b");
  CHECK(p.ok);
  CHECK(p.match_id == "match-1" && p.seed == 7 && p.stage == 31 && p.ports == 3 && p.humans == 2);
  CHECK(p.local_port == 1);
  CHECK(p.input_delay == kInputDelayDefault);
  CHECK((p.ids == std::vector<std::string>{"human-a", "human-b", "cpu:1"}));
  CHECK(p.port[2].kind == Kind::Cpu && p.port[2].level == 9);
  CHECK(!p.frozen.count("matchId") && p.frozen["seed"] == 7);
  CHECK(plan(setup_of(setup_frame()), "human-a").local_port == 0);
  CHECK(!plan(setup_of(setup_frame()), "human-z").ok);    // not this player's match
  CHECK(!plan(setup_of(setup_frame()), "cpu:1").ok);      // a CPU is nobody's seat
  json f = setup_frame(); f["inputDelay"] = 4;
  CHECK(plan(setup_of(f), "human-a").input_delay == 4);

  // Refused setups: the game never builds a match the room could not have frozen.
  f = setup_frame(); f["seats"].erase(1); f["seats"].erase(1);
  CHECK(decode(f).why || !plan(setup_of(f), "human-a").ok);      // one player
  f = setup_frame(); f["seats"][1]["port"] = 2; f["seats"][2]["port"] = 1;
  CHECK(!plan(setup_of(f), "human-a").ok);                         // ports out of order
  f = setup_frame(); f["seats"][1]["seat"] = 2;
  CHECK(!plan(setup_of(f), "human-a").ok);                         // a seat twice
  f = setup_frame(); f["seats"][2]["id"] = "cpu:3";
  CHECK(!plan(setup_of(f), "human-a").ok);                         // a CPU named for another seat
  f = setup_frame(); std::swap(f["seats"][1], f["seats"][2]); f["seats"][1]["port"] = 1; f["seats"][2]["port"] = 2;
  CHECK(!plan(setup_of(f), "human-a").ok);                         // a human after a CPU
  f = setup_frame(); f["seats"][1]["id"] = "human-a";
  CHECK(!plan(setup_of(f), "human-a").ok);                         // one account twice
  f = setup_frame(); f["seats"][1]["id"] = "cpu:0";
  CHECK(!plan(setup_of(f), "human-a").ok);                         // a human posing as a CPU
  f = setup_frame(); f["seats"][0]["team"] = 1;
  CHECK(!plan(setup_of(f), "human-a").ok);                         // a team with teams off
  f = setup_frame(true); f["seats"][2]["team"] = 1;
  CHECK(!plan(setup_of(f), "human-a").ok);                         // teams with one team
  CHECK(plan(setup_of(setup_frame(true)), "human-a").ok);
  // Digests: the setup's for this seat must be this game's (when both exist).
  f = setup_frame(); f["seats"][0]["settingsDigest"] = "set1"; f["seats"][0]["cardDigest"] = "card1";
  CHECK(plan(setup_of(f), "human-a", "set1", "card1").ok);
  CHECK(!plan(setup_of(f), "human-a", "set2", "card1").ok);
  CHECK(!plan(setup_of(f), "human-a", "set1", "card2").ok);
  CHECK(plan(setup_of(f), "human-a").ok);                          // a game with no digest yet
  CHECK(plan(setup_of(setup_frame()), "human-a", "set1", "card1").ok);   // a room with none

  // ---- the block ----
  std::vector<uint8_t> block(kBlock, 0);
  CHECK(apply(p, block.data(), block.size()));
  CHECK(block[0x0] == 0x32 && block[0x3] == 0x4C);   // stock
  CHECK((block[0x1] & 0x01) == 0x01);                // friendly fire on
  CHECK((block[0x2] & 0x08) == 0x08);                // pause off
  CHECK(block[0x8] == 0);
  CHECK(block[0xE] == 0 && block[0xF] == 31);
  CHECK(block[0x10] == 0 && block[0x11] == 0 && block[0x12] == 0x01 && block[0x13] == 0x2C);   // 5 minutes
  CHECK(rec(block, 0)[0] == 2 && rec(block, 0)[1] == 0 && rec(block, 0)[2] == 3 && rec(block, 0)[3] == 1);
  CHECK(rec(block, 1)[1] == 0 && rec(block, 2)[1] == 1 && rec(block, 3)[1] == 3);
  CHECK(rec(block, 2)[0] == 9 && rec(block, 2)[0xF] == 9 && rec(block, 2)[0xE] == kCpuKindDefault);
  CHECK(rec(block, 0)[0xF] == kCpuLevelIdle && rec(block, 1)[0xF] == kCpuLevelIdle);
  // Same character and colour on ports 0 and 1: the second gets costume index 1; the CPU 0.
  CHECK(rec(block, 0)[7] == 0 && rec(block, 1)[7] == 1 && rec(block, 2)[7] == 0);
  CHECK(!apply(p, block.data(), 0x60));
  CHECK(!apply(Plan{}, block.data(), block.size()));

  // Stock with minutes 0: no timer (the timer bit clear, no time), as Melee's own stock with no limit.
  f = setup_frame(); f["rules"]["minutes"] = 0;
  Plan s0 = plan(setup_of(f), "human-a");
  CHECK(s0.ok && s0.rules.minutes == 0);
  std::vector<uint8_t> sb(kBlock, 0xFF);
  CHECK(apply(s0, sb.data(), sb.size()));
  CHECK(sb[0x0] == 0x30 && sb[0x3] == 0x4C);
  CHECK(sb[0x10] == 0 && sb[0x11] == 0 && sb[0x12] == 0 && sb[0x13] == 0);
  CHECK(apply(s0, sb.data(), sb.size(), 30) && sb[0x0] == 0x32 && sb[0x13] == 30);   // the tests' timer still runs
  f = setup_frame(false, "time"); f["rules"]["minutes"] = 0;
  CHECK(decode(f).op == Op::None);                     // Time with 0 is refused, as Rooms refuses it

  // Colours: clamped to the build's costume count before the hash (every machine the same), else 0.
  {
    std::vector<uint8_t> cb(kBlock, 0);
    uint8_t counts[MU_LOBBY_CHARS];
    for (unsigned ch = 0; ch < MU_LOBBY_CHARS; ++ch) counts[ch] = (uint8_t)mu_lobby_costumes_melee(ch);
    CHECK(apply(p, cb.data(), cb.size(), 0, counts) && rec(cb, 0)[3] == 1 && rec(cb, 2)[3] == 0);
    counts[2] = 1;                                     // this build's Fox has one costume
    CHECK(apply(p, cb.data(), cb.size(), 0, counts) && rec(cb, 0)[3] == 0 && rec(cb, 1)[3] == 0);
    CHECK(rec(cb, 0)[7] == 0 && rec(cb, 1)[7] == 1);   // one colour for both: the second port is index 1
    f = setup_frame(); f["seats"][0]["color"] = 5;     // Melee's Fox has 4 (Captain Falcon 6)
    Plan c5 = plan(setup_of(f), "human-a");
    CHECK(c5.ok && apply(c5, cb.data(), cb.size()) && rec(cb, 0)[3] == 3);   // over the count: its last
    f = setup_frame(); f["seats"][0]["char"] = 0; f["seats"][0]["color"] = 5;
    Plan falcon = plan(setup_of(f), "human-a");
    CHECK(falcon.ok && apply(falcon, cb.data(), cb.size()) && rec(cb, 0)[3] == 5);
    CHECK(costume_for(2, 3, nullptr) == 3 && costume_for(2, 4, nullptr) == 3 && costume_for(26, 0, nullptr) == 0);
    CHECK(costume_for(2, -1, nullptr) == 0 && costume_for(2, 5, counts) == 0);   // counts[2] is 1 here
  }

  // Time, teams, pause, no friendly fire, a test timer.
  f = setup_frame(true, "time");
  f["rules"]["friendlyFire"] = false; f["rules"]["pause"] = true;
  Plan t = plan(setup_of(f), "human-a");
  CHECK(t.ok);
  std::vector<uint8_t> tb(kBlock, 0xFF);
  CHECK(apply(t, tb.data(), tb.size(), 30));
  CHECK(tb[0x0] == 0x12 && tb[0x3] == 0xCC && (tb[0x1] & 0x01) == 0 && (tb[0x2] & 0x08) == 0 && tb[0x8] == 1);
  CHECK(tb[0x10] == 0 && tb[0x11] == 0 && tb[0x12] == 0 && tb[0x13] == 30);
  CHECK(rec(tb, 0)[2] == 4 && rec(tb, 0)[9] == 1 && rec(tb, 2)[9] == 2);
  CHECK(rec(tb, 0)[7] == 0 && rec(tb, 1)[7] == 1);   // same team: indices count by team
  CHECK(rec(tb, 3)[1] == 3);

  // ---- the same block from any earlier block (it is kept between matches) ----
  std::vector<uint8_t> a(kBlock, 0), b(kBlock, 0);
  for (size_t i = 0; i < kBlock; ++i) { a[i] = (uint8_t)(i * 7); b[i] = (uint8_t)(255 - i * 3); }
  // The bytes apply() leaves to the caller are the same (items, the rest of the rules); the ports and
  // the rules it owns must not depend on what was there.
  for (size_t i = 0x14; i < 0x60; ++i) b[i] = a[i];
  b[0x1] = a[0x1]; b[0x2] = a[0x2];
  for (size_t i = 0x4; i < 0x8; ++i) b[i] = a[i];
  for (size_t i = 0x9; i < 0xE; ++i) b[i] = a[i];
  CHECK(apply(p, a.data(), a.size()) && apply(p, b.data(), b.size()));
  for (int port = 0; port < p.ports; ++port) {
    const uint8_t* x = rec(a, port);
    const uint8_t* y = rec(b, port);
    for (int k : {0x0, 0x1, 0x2, 0x3, 0x7, 0x9, 0xE, 0xF, 0x10, 0x11}) CHECK(x[k] == y[k]);
  }
  CHECK(rec(a, 3)[1] == 3 && rec(b, 3)[1] == 3);
  for (int k : {0x0, 0x1, 0x2, 0x3, 0x8, 0xE, 0xF, 0x10, 0x11, 0x12, 0x13}) CHECK(a[k] == b[k]);

  // ---- the costume indices with CPUs, and which records the setup hash keeps ----
  {
    std::vector<uint8_t> k(kBlock, 0);
    const unsigned char kinds[4] = {0, 1, 1, 7};
    for (int i = 0; i < 4; ++i) { k[0x60 + i * 0x24] = 0x13; k[0x60 + i * 0x24 + 3] = 2; }
    k[0x60 + 2 * 0x24] = 0x12;   // Ice Climbers count as Popo
    mu_setup_ports_kinds(k.data(), k.size(), kinds, 0);
    CHECK(rec(k, 0)[1] == 0 && rec(k, 1)[1] == 1 && rec(k, 2)[1] == 1 && rec(k, 3)[1] == 3);
    CHECK(rec(k, 0)[7] == 0 && rec(k, 1)[7] == 1 && rec(k, 2)[7] == 2);
    CHECK(mu_setup_record_hashed(0) && mu_setup_record_hashed(1));
    CHECK(!mu_setup_record_hashed(2) && !mu_setup_record_hashed(3));
    // The 1v1 path is unchanged: two humans, P3/P4 off.
    std::vector<uint8_t> o(kBlock, 0);
    mu_setup_ports(o.data(), o.size(), 1, 0);
    CHECK(rec(o, 2)[1] == 3 && rec(o, 3)[1] == 3 && rec(o, 1)[7] == 1);
  }

  // ---- the hash halves ----
  CHECK(inputs_hash(p) == inputs_hash(plan(setup_of(setup_frame()), "human-a")));   // every seat agrees
  CHECK(setup_digest(p) == setup_digest(plan(setup_of(setup_frame()), "human-a")));
  f = setup_frame(); f["matchId"] = "match-2";
  CHECK(inputs_hash(p) != inputs_hash(plan(setup_of(f), "human-a")));
  f = setup_frame(); f["seats"][2]["level"] = 8;
  CHECK(setup_digest(p) != setup_digest(plan(setup_of(f), "human-a")));
  f = setup_frame(); f["seed"] = 8;
  CHECK(setup_digest(p) != setup_digest(plan(setup_of(f), "human-a")));
  // Key order on the wire never matters (Rooms compares canonically too).
  json shuffled = json::parse(R"({"stage":31,"seed":7,"rules":{"pause":false,"minutes":5,"stocks":3,"mode":"stock","friendlyFire":true,"teams":false},
    "seats":[{"team":0,"color":1,"char":2,"name":"A","id":"human-a","kind":"human","port":0,"seat":2},
             {"team":0,"color":1,"char":2,"name":"B","id":"human-b","kind":"human","port":1,"seat":0},
             {"level":9,"team":0,"color":0,"char":9,"name":"CPU 2","id":"cpu:1","kind":"cpu","port":2,"seat":1}],
    "matchId":"match-1","rev":12,"op":"setup","v":1,"t":"yg.mu.lobby"})");
  CHECK(setup_digest(p) == setup_digest(plan(setup_of(shuffled), "human-a")));

  // ---- the compatibility key: inside 64, friends-lobby-1 first, versions never share a room ----
  auto make = [](const std::string& v) { return "mu-" + v + "-mod-0123456789abcdef"; };
  std::string c = compatibility("0.9.105", make);
  CHECK(c == "friends-lobby-1:mu-0.9.105-mod-0123456789abcdef");
  const std::string long_a(40, 'a');
  std::string long_b = long_a; long_b.back() = 'b';
  const std::string ca = compatibility(long_a, make), cb = compatibility(long_b, make);
  CHECK(ca.size() <= 64 && cb.size() <= 64 && ca != cb);
  CHECK(ca.rfind("friends-lobby-1:mu-h", 0) == 0);
  CHECK(ca.size() > 20 && ca.substr(ca.size() - 21) == "-mod-0123456789abcdef");
  auto huge = [](const std::string& v) { return "mu-" + v + std::string(60, 'x'); };
  const std::string ch = compatibility("1", huge);
  CHECK(ch.size() <= 64 && ch.rfind("friends-lobby-1:mu-k", 0) == 0);
  CHECK(compatibility("1", huge) != compatibility("2", huge));
  CHECK(compatibility("1", [](const std::string&) { return std::string(); }).empty());
  for (const std::string& k : {c, ca, cb, ch}) {
    CHECK(yougame::friends::lobby_compatibility(k));
  }

  // ---- the match roster: setup humans, by port, from the room's participants ----
  {
    Plan q = plan(setup_of(setup_frame()), "human-a");
    json room = json::parse(R"([{"id":"human-b","connectionId":"c-b","slot":0,"localIndex":0,"name":"B"},
      {"id":"watcher","connectionId":"c-w","slot":1,"localIndex":0},
      {"id":"human-a","connectionId":"c-a","slot":2,"localIndex":0,"name":"A"}])");
    json r = match_roster(room, q);
    CHECK(r.size() == 2);
    CHECK(r[0]["id"] == "human-a" && r[0]["slot"] == 0 && r[0]["connectionId"] == "c-a");
    CHECK(r[1]["id"] == "human-b" && r[1]["slot"] == 1 && r[1]["name"] == "B");
    json missing = room; missing.erase(2);
    CHECK(match_roster(missing, q).empty());              // a setup human not in the room
    json twice = room; twice.push_back(room[0]);
    CHECK(match_roster(twice, q).empty());
    CHECK(match_roster(room, Plan{}).empty());
  }

  // ---- the finish report: full ranking by setup id, the frozen setup, a vanished player last ----
  {
    Plan q = plan(setup_of(setup_frame()), "human-a");   // ports: human-a, human-b, cpu:1
    Outcome o;
    o.end_method = 2; o.has_placements = true;
    o.placements[0] = 2; o.placements[1] = 0; o.placements[2] = 1; o.placements[3] = 0xFF;
    json r = finish_report(q, o);
    CHECK(r["t"] == "finish" && r["matchId"] == "match-1" && !r.count("void") && !r.count("draw"));
    CHECK(r["ranking"] == json({"human-b", "cpu:1", "human-a"}));
    CHECK(r["setup"] == q.frozen && !r["setup"].count("matchId"));
    // The netcode dropped human-b mid-match: ranked last, never omitted, whatever its placement said.
    o.gone = 1u << 1;
    CHECK(finish_report(q, o)["ranking"] == json({"cpu:1", "human-a", "human-b"}));
    // A CPU is never dropped (it runs on every machine).
    o.gone = 1u << 2;
    CHECK(finish_report(q, o)["ranking"] == json({"human-b", "cpu:1", "human-a"}));
    // A No Contest naming a quitter (LRA-Start) where the game still placed everyone: the quitter last.
    o.gone = 0; o.end_method = 7; o.lras = 0;
    CHECK(finish_report(q, o)["ranking"] == json({"human-b", "cpu:1", "human-a"}));
    o.lras = 1;
    CHECK(finish_report(q, o)["ranking"] == json({"cpu:1", "human-a", "human-b"}));
    // A No Contest with a disconnect and no order from the game, two sides still in (human-b, the CPU):
    // void, no ranking. Never an order the game didn't decide.
    Outcome d; d.end_method = 7; d.gone = 1u << 0;
    r = finish_report(q, d);
    CHECK(r["void"] == true && !r.count("ranking") && r["setup"] == q.frozen && r["matchId"] == "match-1");
    Outcome dq; dq.end_method = 7; dq.lras = 1;   // an LRA quit, no order: the same
    CHECK(finish_report(q, dq)["void"] == true && !finish_report(q, dq).count("ranking"));
    // A No Contest naming nobody (a desync): void, even with placements in the record.
    Outcome v; v.end_method = 7; v.has_placements = true; v.placements[0] = 0;
    r = finish_report(q, v);
    CHECK(r["void"] == true && !r.count("ranking") && r["setup"] == q.frozen);
    CHECK(finish_report(q, Outcome{})["void"] == true);   // no result at all
    CHECK(!finish_report(q, Outcome{}).count("ranking"));
    // Only a winner: the winner first, the rest by port.
    Outcome w; w.end_method = 2; w.winner = 2;
    CHECK(finish_report(q, w)["ranking"] == json({"cpu:1", "human-a", "human-b"}));
    w.gone = 1u << 0;   // ...and a leaver after the winner was decided: last
    CHECK(finish_report(q, w)["ranking"] == json({"cpu:1", "human-b", "human-a"}));
    Outcome dr; dr.end_method = 1; dr.draw = true;
    r = finish_report(q, dr);
    CHECK(r["draw"] == true && !r.count("ranking") && !r.count("void"));
    // One-on-one, no CPUs: a quitter leaves exactly one side, which wins.
    json duel = setup_frame(); duel["seats"].erase(2);
    Plan dp = plan(setup_of(duel), "human-a");
    CHECK(dp.ok && dp.ports == 2);
    Outcome q1; q1.end_method = 7; q1.lras = 0;
    CHECK(finish_report(dp, q1)["ranking"] == json({"human-b", "human-a"}));
    Outcome g1; g1.end_method = 7; g1.gone = 1u << 1;
    CHECK(finish_report(dp, g1)["ranking"] == json({"human-a", "human-b"}));
    // Teams: the winning team's ports share placement 0 and stay together, in port order.
    Plan tq = plan(setup_of(setup_frame(true)), "human-a");   // team 1: human-a, human-b; team 2: cpu:1
    Outcome to; to.end_method = 2; to.has_placements = true;
    to.placements[0] = 1; to.placements[1] = 1; to.placements[2] = 0;
    CHECK(finish_report(tq, to)["ranking"] == json({"cpu:1", "human-a", "human-b"}));
    // Teams, the game placed human-a's team first but human-b left: their whole team goes last.
    to.placements[0] = 0; to.placements[1] = 0; to.placements[2] = 1; to.gone = 1u << 1;
    CHECK(finish_report(tq, to)["ranking"] == json({"cpu:1", "human-a", "human-b"}));
    // Teams, no order, human-b dropped: one side (the CPU's team) is left, and it wins.
    Outcome tg; tg.end_method = 7; tg.gone = 1u << 1;
    CHECK(finish_report(tq, tg)["ranking"] == json({"cpu:1", "human-a", "human-b"}));
    CHECK(finish_report(Plan{}, o)["void"] == true);
  }

  // ---- hello's digests: valid on the wire, one per build ----
  {
    const std::string d = settings_digest("friends-lobby-1:mu-0.9.105");
    CHECK(d.size() == 22 && d.rfind("mu-s1-", 0) == 0);
    CHECK(d == settings_digest("friends-lobby-1:mu-0.9.105") && d != settings_digest("friends-lobby-1:mu-0.9.106"));
    CHECK(!frame_text(hello_frame(d, kCardDigestNative)).empty());
  }

  // ---- the build capability: old builds can't join a lobby match ----
  CHECK(mu_friends_lobby_verdict(1, 1, MU_BUILD_CAPS2_FRIENDS_LOBBY, 0) == 1);
  CHECK(mu_friends_lobby_verdict(1, 1, 0, 0) == -1);              // no second byte: an older build
  CHECK(mu_friends_lobby_verdict(1, 0, MU_BUILD_CAPS2_FRIENDS_LOBBY, 0) == -1);   // no setup hash
  CHECK(mu_friends_lobby_verdict(0, 0, 0, 0) == 0);               // still waiting
  CHECK(mu_friends_lobby_verdict(0, 0, 0, 1) == -1);              // never sent one
  CHECK(std::strlen(MU_FRIENDS_LOBBY_OLD_BUILD_TEXT) < 100);

  // ---- six seats (Triples): a six-port plan, its block (players 5 and 6) and its finish ----
  {
    json s6 = setup_frame(true);
    s6["seats"] = json::array();
    const int chars[6] = {2, 20, 12, 19, 0, 15};
    for (int k = 0; k < 4; ++k)
      s6["seats"].push_back({{"seat", k}, {"port", k}, {"kind", "human"}, {"id", "h" + std::to_string(k)}, {"name", "P"},
                             {"char", chars[k]}, {"color", 0}, {"team", k % 2}});
    for (int k = 4; k < 6; ++k)
      s6["seats"].push_back({{"seat", k}, {"port", k}, {"kind", "cpu"}, {"id", "cpu:" + std::to_string(k)}, {"name", "CPU"},
                             {"char", chars[k]}, {"color", 0}, {"team", k % 2}, {"level", 9}});
    Plan p6 = plan(setup_of(s6), "h3");
    CHECK(p6.ok && p6.ports == 6 && p6.humans == 4 && p6.local_port == 3 && p6.port[5].kind == Kind::Cpu);
    CHECK(!plan(setup_of(s6), "h3", {}, {}, Profile::Doubles).ok);   // Doubles is four
    std::vector<uint8_t> block(kBlock, 0x5A);
    CHECK(apply(p6, block.data(), block.size()));
    CHECK(rec(block, 4)[1] == 1 && rec(block, 5)[1] == 1 && rec(block, 5)[0] == 15 && rec(block, 4)[0xF] == 9);
    CHECK(rec(block, 4)[0x9] == 0 && rec(block, 5)[0x9] == 1 && rec(block, 3)[1] == 0);
    std::vector<uint8_t> small(0x60 + 4 * 0x24, 0);
    CHECK(!apply(p6, small.data(), small.size()));             // six ports need the six records
    // Then a three-port match on the same block: players 5 and 6 are nobody again.
    Plan p3 = plan(setup_of(setup_frame()), "human-b");
    CHECK(apply(p3, block.data(), block.size()) && rec(block, 4)[1] == 3 && rec(block, 5)[1] == 3);
    // The finish: six placements rank all six; Teams with only a winner put the winner's team first.
    Outcome o;
    o.has_placements = true;
    const int places[6] = {3, 0, 2, 1, 5, 4};
    for (int k = 0; k < 6; ++k) o.placements[k] = places[k];
    json fin = finish_report(p6, o);
    CHECK(!fin.value("void", false) && fin["ranking"] == json({"h1", "h3", "h2", "h0", "cpu:5", "cpu:4"}));
    Outcome w;
    w.winner = 3; w.end_method = 2;
    fin = finish_report(p6, w);
    CHECK(fin["ranking"] == json({"h1", "h3", "cpu:5", "h0", "h2", "cpu:4"}));
  }
  std::printf("ok\n");
  return 0;
}
