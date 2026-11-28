// SPDX-License-Identifier: GPL-2.0-or-later
// The Friends lobby's room frames (yougame_lobby_wire.h) against Rooms' frozen v1 (rooms/src/mu-lobby.ts
// validMuFrame, muState, the setup broadcast) and the optional hello/inputDelay shapes.
#include "yougame_lobby_wire.h"
#include "yougame_friends.h"
#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

using namespace yougame::lobby;

static json state_json() {
  return json::parse(R"({"t":"yg.mu.lobby","v":1,"op":"state","rev":6,"host":0,
    "seats":[{"seat":0,"kind":"human","id":"human-a","name":"Player A","pick":{"char":2,"color":1,"team":0}},
             {"seat":1,"kind":"cpu","level":9,"pick":{"char":5,"color":0,"team":0}},
             {"seat":2,"kind":"open"},{"seat":3,"kind":"open"}],
    "rules":{"teams":false,"friendlyFire":false,"mode":"stock","stocks":4,"minutes":8,"pause":false},
    "setup":null})");
}
static json setup_json() {
  return json::parse(R"({"t":"yg.mu.lobby","v":1,"op":"setup","rev":6,"matchId":"match-example","seed":7,
    "seats":[{"seat":0,"port":0,"kind":"human","id":"human-a","name":"Player A","char":2,"color":1,"team":0},
             {"seat":1,"port":1,"kind":"cpu","id":"cpu:1","name":"CPU 2","char":5,"color":0,"team":0,"level":9}],
    "rules":{"teams":false,"friendlyFire":false,"mode":"stock","stocks":4,"minutes":8,"pause":false},
    "stage":3})");
}

int main() {
  // ---- outgoing: each op's keys, exactly as validMuFrame takes them ----
  const Pick p{2, 1, 0};
  const int64_t rev = 6;
  CHECK(!frame_text(hover_frame(0, p)).empty());
  CHECK(!hover_frame(0, p).count("rev"));                 // hover never carries rev
  CHECK(!pick_frame(0, p, nullptr).count("rev"));          // nor a player's own pick
  CHECK(pick_frame(1, p, &rev)["rev"] == 6);               // a CPU's pick does
  CHECK(!unpick_frame(0, nullptr).count("rev"));
  CHECK(!frame_text(unpick_frame(1, &rev)).empty());
  CHECK(!frame_text(rules_frame(Rules{}, 0)).empty());
  CHECK(!frame_text(cpu_seat_frame(1, 9, 3)).empty());
  CHECK(!frame_text(open_seat_frame(1, 3)).empty());
  CHECK(!frame_text(start_frame(0, 3, 0)).empty());
  CHECK(start_frame(0, 3, 0)["stage"] == "random");
  CHECK(!start_frame(0, 3, 0).count("inputDelay"));
  CHECK(start_frame(31, 3, 2)["inputDelay"] == 2);
  CHECK(!frame_text(leave_frame()).empty());
  CHECK(!frame_text(hello_frame("mu-r1234_abc", "native")).empty());
  CHECK(!hello_frame("a", "b").count("rev"));
  CHECK(frame_text(hover_frame(0, p)) == R"({"char":2,"color":1,"op":"hover","seat":0,"t":"yg.mu.lobby","team":0,"v":1})");

  // Ranges: seat 0-5 (a six-seat room's; a four-seat session never sends 4 or 5), char 0-25, color 0-5,
  // team 0-2, level 1-9, stage 2-32 but 21 and 26, delay 1-9.
  CHECK(!frame_text(hover_frame(5, p)).empty());
  CHECK(frame_text(hover_frame(6, p)).empty());
  CHECK(frame_text(hover_frame(-1, p)).empty());
  CHECK(frame_text(hover_frame(0, Pick{26, 0, 0})).empty());
  CHECK(!frame_text(hover_frame(0, Pick{25, 5, 2})).empty());
  CHECK(frame_text(hover_frame(0, Pick{0, 6, 0})).empty());
  CHECK(frame_text(hover_frame(0, Pick{0, 0, 3})).empty());
  CHECK(frame_text(cpu_seat_frame(1, 0, 0)).empty());
  CHECK(frame_text(cpu_seat_frame(1, 10, 0)).empty());
  for (int stage : {1, 21, 26, 33}) CHECK(frame_text(start_frame(stage, 0, 0)).empty());
  for (int stage : {2, 3, 8, 20, 22, 25, 27, 28, 31, 32}) CHECK(!frame_text(start_frame(stage, 0, 0)).empty());
  CHECK(frame_text(start_frame(0, 0, 10)).empty());
  CHECK(frame_text(start_frame(0, 0, -1)).empty());
  Rules r;
  r.stocks = 0; CHECK(frame_text(rules_frame(r, 0)).empty());
  r.stocks = 99; r.minutes = 99; CHECK(!frame_text(rules_frame(r, 0)).empty());
  r.minutes = 100; CHECK(frame_text(rules_frame(r, 0)).empty());
  // Stock's minutes may be 0 (no timer, Rooms validMuRules 11-06); Time keeps 1-99.
  {
    Rules stock;
    stock.minutes = 0;
    CHECK(rules_ok(stock) && !frame_text(rules_frame(stock, 0)).empty());
    Rules timed;
    timed.time = true; timed.minutes = 0;
    CHECK(!rules_ok(timed) && frame_text(rules_frame(timed, 0)).empty());
    timed.minutes = 1;
    CHECK(rules_ok(timed));
    stock.minutes = -1;
    CHECK(!rules_ok(stock));
    json st = state_json();
    st["rules"]["minutes"] = 0;
    Incoming in = decode(st);
    CHECK(in.op == Op::State && in.state.rules.minutes == 0 && !in.state.rules.time);
    st["rules"]["mode"] = "time";
    CHECK(decode(st).op == Op::None);
  }
  CHECK(frame_text(rules_frame(Rules{}, -1)).empty());   // rev is a non-negative integer
  CHECK(frame_text(hello_frame("", "x")).empty());
  CHECK(frame_text(hello_frame(std::string(65, 'a'), "x")).empty());
  CHECK(!frame_text(hello_frame(std::string(64, 'a'), "x")).empty());
  CHECK(frame_text(hello_frame("a b", "x")).empty());
  CHECK(frame_text(hello_frame("a", "x/")).empty());

  // Unknown fields are refused, on every op, as Rooms refuses them (shape).
  for (json f : {hover_frame(0, p), pick_frame(0, p, nullptr), unpick_frame(0, nullptr), rules_frame(Rules{}, 0),
                 cpu_seat_frame(1, 3, 0), open_seat_frame(1, 0), start_frame(0, 0, 0), leave_frame(),
                 hello_frame("a", "b")}) {
    CHECK(valid_outgoing(f));
    f["extra"] = 1;
    CHECK(!valid_outgoing(f));
    CHECK(frame_text(f).empty());
  }
  json f = rules_frame(Rules{}, 0); f["rules"]["items"] = false; CHECK(!valid_outgoing(f));   // rules are exactly six keys
  f = rules_frame(Rules{}, 0); f["rules"].erase("pause"); CHECK(!valid_outgoing(f));
  f = open_seat_frame(1, 0); f["level"] = 3; CHECK(!valid_outgoing(f));   // open has no level
  f = hello_frame("a", "b"); f["rev"] = 1; CHECK(!valid_outgoing(f));    // hello carries no rev
  f = hover_frame(0, p); f["v"] = 2; CHECK(!valid_outgoing(f));
  f = hover_frame(0, p); f["t"] = "yg.mu.other"; CHECK(!valid_outgoing(f));
  f = hover_frame(0, p); f["op"] = "wave"; CHECK(!valid_outgoing(f));
  f = hover_frame(0, p); f.erase("team"); CHECK(valid_outgoing(f));   // team omitted = 0
  f = hover_frame(0, p); f["char"] = 2.5; CHECK(!valid_outgoing(f));

  // The 1,024-character cap: nothing over it goes out.
  f = rules_frame(Rules{}, 9007199254740991LL);
  CHECK(!frame_text(f).empty());
  CHECK(frame_text(f).size() <= kFrameMax);
  CHECK(frame_text(rules_frame(Rules{}, 9007199254740992LL)).empty());   // past Number.MAX_SAFE_INTEGER
  // A frame that passes the shape check but is too long (the cap is checked on the text itself).
  json big = hover_frame(0, p);
  CHECK(frame_text(big).size() < kFrameMax);

  // ---- incoming: state ----
  Incoming in = decode(state_json());
  CHECK(in.op == Op::State && !in.why);
  CHECK(in.state.rev == 6 && in.state.host == 0);
  CHECK(in.state.seats[0].kind == Kind::Human && in.state.seats[0].id == "human-a" && in.state.seats[0].picked);
  CHECK(in.state.seats[0].pick == (Pick{2, 1, 0}));
  CHECK(in.state.seats[1].kind == Kind::Cpu && in.state.seats[1].level == 9 && in.state.seats[1].picked);
  CHECK(in.state.seats[2].kind == Kind::Open && in.state.seats[3].kind == Kind::Open);
  CHECK(in.state.setup.is_null() && in.state.error.empty());
  CHECK(in.state.rules == Rules{});

  // Unknown incoming keys never refuse a state (Rooms may add digests, inputDelay, anything).
  json st = state_json();
  st["future"] = {1, 2};
  st["seats"][0]["settingsDigest"] = "abc";
  st["seats"][0]["cardDigest"] = "def";
  st["seats"][0]["somethingNew"] = true;
  st["rules"]["items"] = false;
  in = decode(st);
  CHECK(in.op == Op::State && !in.why);
  CHECK(in.state.seats[0].settings_digest == "abc" && in.state.seats[0].card_digest == "def");
  // ...but a known field out of range does.
  st = state_json(); st["seats"][1]["level"] = 10; CHECK(decode(st).why);
  st = state_json(); st["seats"][0]["pick"]["char"] = 26; CHECK(decode(st).why);
  st = state_json(); st["seats"].erase(3); CHECK(decode(st).why);
  st = state_json(); std::swap(st["seats"][2], st["seats"][3]); CHECK(decode(st).why);
  st = state_json(); st["host"] = nullptr; in = decode(st); CHECK(!in.why && in.state.host == -1);
  st = state_json(); st["rules"]["mode"] = "coin"; CHECK(decode(st).why);
  // A hover is not stored by Rooms, but a broadcast between hovers carries it.
  st = state_json(); st["seats"][0]["hover"] = {{"char", 9}, {"color", 0}};
  in = decode(st); CHECK(in.state.seats[0].hovering && in.state.seats[0].hover == (Pick{9, 0, 0}));

  // A refusal: the state, error, action and the refused seat, sent to the sender only.
  st = state_json(); st["error"] = "revision"; st["action"] = "seat"; st["seat"] = {{"seat", 1}, {"kind", "cpu"}, {"level", 9}};
  in = decode(st);
  CHECK(in.op == Op::State && in.state.error == "revision" && in.state.action == "seat");
  CHECK(in.state.has_refused_seat && in.state.refused_seat.seat == 1 && in.state.refused_seat.kind == Kind::Cpu);
  // A room without the lobby: {t, v, op:'state', error:'unsupported'}, no seats.
  in = decode(json{{"t", "yg.mu.lobby"}, {"v", 1}, {"op", "state"}, {"error", "unsupported"}});
  CHECK(in.op == Op::State && in.state.bare && in.state.error == "unsupported");

  // Not a lobby frame at all: ignored, no complaint.
  in = decode(json{{"t", "welcome"}, {"me", "c1"}});
  CHECK(in.op == Op::None && !in.why);
  in = decode(json{{"t", "yg.mu.lobby"}, {"v", 2}, {"op", "state"}});
  CHECK(in.op == Op::None && in.why);

  // ---- incoming: setup ----
  in = decode(setup_json());
  CHECK(in.op == Op::Setup && !in.why);
  CHECK(in.setup.match_id == "match-example" && in.setup.seed == 7 && in.setup.stage == 3 && in.setup.rev == 6);
  CHECK(in.setup.seats.size() == 2);
  CHECK(in.setup.seats[1].kind == Kind::Cpu && in.setup.seats[1].id == "cpu:1" && in.setup.seats[1].level == 9);
  CHECK(in.setup.input_delay == 0);
  // The frozen setup is the frame minus its envelope and matchId: exactly what game-report echoes.
  const json frozen = in.setup.frozen;
  CHECK(!frozen.count("t") && !frozen.count("v") && !frozen.count("op") && !frozen.count("rev") && !frozen.count("matchId"));
  CHECK(frozen.count("seed") && frozen.count("seats") && frozen.count("rules") && frozen.count("stage"));
  CHECK(frozen.size() == 4);
  // Future keys (digests, inputDelay, anything) are kept in the echo.
  json sj = setup_json();
  sj["inputDelay"] = 3;
  sj["seats"][0]["settingsDigest"] = "s1";
  sj["seats"][0]["cardDigest"] = "c1";
  sj["newThing"] = "kept";
  in = decode(sj);
  CHECK(in.op == Op::Setup && !in.why && in.setup.input_delay == 3);
  CHECK(in.setup.seats[0].settings_digest == "s1" && in.setup.seats[0].card_digest == "c1");
  CHECK(in.setup.frozen["newThing"] == "kept" && in.setup.frozen["inputDelay"] == 3);
  CHECK(in.setup.frozen["seats"][0]["settingsDigest"] == "s1");
  sj = setup_json(); sj["stage"] = 21; CHECK(decode(sj).why);
  sj = setup_json(); sj["stage"] = "random"; CHECK(decode(sj).why);   // Rooms draws random before it freezes
  sj = setup_json(); sj["inputDelay"] = 0; CHECK(decode(sj).why);
  sj = setup_json(); sj["seats"][1].erase("level"); CHECK(decode(sj).why);
  sj = setup_json(); sj["seats"][0]["level"] = 3; CHECK(decode(sj).why);
  sj = setup_json(); sj.erase("matchId"); CHECK(decode(sj).why);
  sj = setup_json(); sj["seed"] = 4294967296LL; CHECK(decode(sj).why);
  sj = setup_json(); sj["seed"] = 4294967295LL; CHECK(!decode(sj).why);
  // A state during a match carries the frozen setup (no matchId).
  st = state_json(); st["setup"] = frozen;
  in = decode(st);
  CHECK(!in.why && in.state.setup == frozen);

  // ---- the held room and its invites (desktop mu-friends.ts friendsLobbyTicket) ----
  using namespace yougame::friends;
  CHECK(lobby_compatibility("friends-lobby-1:mu-0.9.105-r12345678"));
  CHECK(lobby_compatibility("mu:friends-lobby-1:fixture"));
  CHECK(lobby_compatibility("mu-x:friends-lobby-1"));
  CHECK(!lobby_compatibility("mu-0.9.105"));
  CHECK(!lobby_compatibility("xfriends-lobby-1:mu"));
  CHECK(!lobby_compatibility("friends-lobby-12:mu"));
  CHECK(!lobby_compatibility("friends-lobby-1:" + std::string(49, 'a')));   // 65 characters
  CHECK(lobby_compatibility("friends-lobby-1:" + std::string(48, 'a')));
  CHECK(!lobby_compatibility("friends-lobby-1:a b"));
  CHECK(lobby_hold("friends-lobby-1:mu") ==
        json({{"type", "mpFriend"}, {"hold", true}, {"players", 4}, {"compatibility", "friends-lobby-1:mu"}}));
  CHECK(lobby_invite("abcdefghjkmn") ==
        json({{"type", "mpFriend"}, {"room", "abcdefghjkmn"}, {"players", 4}, {"multi", true}}));
  const json t = lobby_ticket("friends-lobby-1:mu", "abcdefghjkmn");   // held codes are 12 characters
  CHECK(t["type"] == "mpTicket" && t["players"] == 4 && t["room"] == "abcdefghjkmn" && t["ranked"] == false);
  CHECK(t["lobby"]["compatibility"] == "friends-lobby-1:mu" && t["lobby"]["minPlayers"] == 2);
  bool threw = false;
  try { lobby_hold("mu-0.9.105"); } catch (const std::exception&) { threw = true; }
  CHECK(threw);
  threw = false;
  try { lobby_ticket("friends-lobby-1:mu", "ab"); } catch (const std::exception&) { threw = true; }
  CHECK(threw);
  // ---- six seats (Triples): a six-seat state and setup, and the asks for six ----
  {
    json six = state_json();
    six["seats"].push_back({{"seat", 4}, {"kind", "human"}, {"id", "human-e"}, {"name", "Player E"}});
    six["seats"].push_back({{"seat", 5}, {"kind", "open"}});
    Incoming i6 = decode(six);
    CHECK(i6.op == Op::State && !i6.why && i6.state.size == kSeatsMax);
    CHECK(i6.state.seats[4].kind == Kind::Human && i6.state.seats[4].id == "human-e" && i6.state.seats[5].seat == 5);
    six["host"] = 5;
    CHECK(!decode(six).why);                                   // any of the six may be the host
    CHECK(decode(six, Profile::Doubles).why);                  // a Doubles home is never six
    json five = state_json();
    five["seats"].push_back({{"seat", 4}, {"kind", "open"}});
    CHECK(decode(five).why);                                   // five seats: no room has that shape
    Incoming i4 = decode(state_json());
    CHECK(i4.state.size == kSeats && i4.state.seats[4].kind == Kind::Open && i4.state.seats[5].seat == 5);
    json four_host = state_json();
    four_host["host"] = 4;
    CHECK(decode(four_host).why);                              // a four-seat room has no seat 4
    json s6 = setup_json();
    s6["seats"] = json::array();
    for (int k = 0; k < 6; ++k)
      s6["seats"].push_back({{"seat", k}, {"port", k}, {"kind", "human"}, {"id", "h" + std::to_string(k)}, {"name", "P"},
                             {"char", k}, {"color", 0}, {"team", 0}});
    Incoming is = decode(s6);
    CHECK(is.op == Op::Setup && !is.why && is.setup.seats.size() == 6 && is.setup.seats[5].port == 5);
    s6["seats"].push_back({{"seat", 5}, {"port", 6}, {"kind", "human"}, {"id", "h6"}, {"name", "P"}, {"char", 6}, {"color", 0}, {"team", 0}});
    CHECK(decode(s6).why);                                     // seven: never
  }
  {
    using namespace yougame::friends;
    CHECK(lobby_hold("friends-lobby-1:mu", true)["seats"] == 6 && lobby_hold("friends-lobby-1:mu", true)["players"] == 4);
    CHECK(!lobby_hold("friends-lobby-1:mu").count("seats"));
    const json t6 = lobby_ticket("friends-lobby-1:mu", "abcdefghjkmn", true);
    CHECK(t6["lobby"]["seats"] == 6 && t6["players"] == 4 && !t6.count("seats"));
    CHECK(!lobby_ticket("friends-lobby-1:mu", "abcdefghjkmn")["lobby"].count("seats"));
    CHECK(lobby_invite("abcdefghjkmn", 6)["players"] == 6);
    CHECK(lobby_invite("abcdefghjkmn", 5)["players"] == 4);    // only four or six
  }
  std::printf("ok\n");
  return 0;
}
