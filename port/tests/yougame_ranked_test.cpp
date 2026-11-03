// Run on any host: clang++ -std=c++20 -Iport/runtime/hle -Iport/third_party
//   port/tests/yougame_ranked_test.cpp -o /tmp/yougame_ranked_test
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The ranked set (yougame_ranked.h) against fakes: unit checks, then two clients playing whole
// sets against a model of the rooms Worker's rules for a ranked duel (rooms/src/game-lobby.ts:
// game groups that settle only on both reports, a final refused while a game is pending, a lone
// final with both players present voiding at its grace, and the leave rule awarding the player
// who stayed once the other has been gone for MATCH_RECONNECT_MS).
#include "yougame_ranked.h"
#include "online_test_quit.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <regex>
#include <set>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame::ranked;

// ------------------------------------------------------------------ unit checks
struct FakeRoom {
  Set set;
  std::vector<json> sent;
  uint64_t now = 1;
  bool host = true;
  FakeRoom(int local = 0) { set.configure(local, {"participant-a", "participant-b"}); }
  std::vector<json> tick() {
    auto out = set.due(now, host, 7);
    for (const auto& message : out) sent.push_back(json::parse(message.dump()));
    return out;
  }
  void start() {
    set.start_game();
    auto out = tick();
    if (host) CHECK(out.size() == 1 && out[0] == json({{"t", "begin"}, {"revision", 7}}));
    else CHECK(out.empty());   // only the room's host asks for the match
    set.receive({{"t", "start"}, {"matchId", "set-1"}});
  }
  Game game(int winner, bool forfeit = false) {
    Game game;
    set.start_game();   // a no-op while start() already began game 1 and nothing reported it
    game.index = set.games_started(); game.winner = winner; game.forfeit = forfeit;
    const int chars[2] = {2, 20};
    game.detail = game_detail(forfeit ? 7 : 2, forfeit ? 1 - winner : -1, 31, chars, 1800);
    set.report_game(game);
    return game;
  }
  // The room's settled outcome for a game (both players reported `winner`, or it voided).
  void receipt(const std::string& id, int winner) {
    json m = {{"t", "game-result"}, {"matchId", "set-1"}, {"id", id}};
    if (winner < 0) m["void"] = true, m["ranking"] = {"participant-a", "participant-b"};
    else m["ranking"] = winner == 0 ? json{"participant-a", "participant-b"} : json{"participant-b", "participant-a"};
    set.receive(m);
  }
};

int count_type(const std::vector<json>& sent, const char* type) {
  return (int)std::count_if(sent.begin(), sent.end(), [&](const json& m) { return m["t"] == type; });
}

void winners_from_the_record() {
  CHECK(game_winner(7, 1, -1, nullptr) == 0);   // a No Contest naming port 2: port 1 wins
  CHECK(game_winner(7, 0, 0, nullptr) == 1);
  CHECK(game_winner(7, -1, 0, nullptr) == -1);  // a No Contest naming nobody
  const int win[4] = {0, 1, 255, 255}, tie[4] = {0, 0, 255, 255}, loss[4] = {1, 0, 255, 255};
  CHECK(game_winner(2, -1, -1, win) == 0);
  CHECK(game_winner(1, -1, -1, loss) == 1);     // a time-out with a leader
  CHECK(game_winner(1, -1, -1, tie) == -1);     // a tied time-out: no winner, the game counts nothing
  CHECK(game_winner(2, -1, 1, nullptr) == 1);   // the Static Recomp's report: Slippi's winnerIdx
  CHECK(game_winner(0, -1, 5, nullptr) == -1);
  // A duel's time-out waits for the game's report: the record placed two tied time-outs (4 stocks
  // and 0% each) port 1 first on 10-28, and ranks any equal-stock time-out by kills, falls, port.
  CHECK(time_out_waits_for_report(1, true, 2));
  CHECK(!time_out_waits_for_report(1, false, 2));   // the report itself decides
  CHECK(!time_out_waits_for_report(2, true, 2));    // GAME!: the record is sent once confirmed
  CHECK(!time_out_waits_for_report(7, true, 2));
  CHECK(!time_out_waits_for_report(1, true, 4));    // Teams has no report: the record decides
  CHECK(game_winner(1, -1, -1, nullptr) == -1);     // the report of a tie: nobody wins
  CHECK(game_winner(1, -1, 1, nullptr) == 1);       // port 2 ahead (stocks, then damage)
  CHECK(game_winner(1, -1, -2, nullptr) == -1);     // a desync
  CHECK(casual_winner(1, -1, -1, nullptr, 2) == -1 && casual_winner(1, -1, 0, nullptr, 2) == 0);
  CHECK(game_key(0x1a2b, 32) == "r1a2b-s32" && game_key(-1, 32).empty() && game_key(5, -1).empty());
  // Every detail the game can send passes the room's cleanDetail rule (rooms/src/game-lobby.ts).
  const std::regex key("^[a-z]{1,4}$");
  const int chars[2] = {25, 300};
  for (const json& d : {game_detail(7, 1, 0x20, chars, 1ll << 40), game_detail(2, -1, -5, nullptr, -3)}) {
    CHECK(d.size() <= 8);
    for (auto it = d.begin(); it != d.end(); ++it) {
      const std::string k = it.key(); const json& v = it.value();
      CHECK(std::regex_match(k, key));
      const auto ok = [](const json& n) { return n.is_number_integer() && n.get<int64_t>() >= -1 && n.get<int64_t>() < (1ll << 31); };
      CHECK(ok(v) || (v.is_array() && v.size() <= 4 && std::all_of(v.begin(), v.end(), ok)));
    }
  }
  std::puts("yougame_ranked: game winners from the end record and room-valid details ok");
}

void casual_winners_from_the_record() {
  // The game encodes the stock/percent comparison in placements; winnerIdx can be absent.
  const int stock_lead[4] = {0, 1, 255, 255};   // timeout: port 1 has more stocks
  const int percent_lead[4] = {1, 0, 255, 255}; // timeout: equal stocks, port 2 has less damage
  const int tie[4] = {0, 0, 255, 255};
  CHECK(casual_winner(1, -1, -1, stock_lead, 2) == 0);
  CHECK(casual_winner(1, -1, -1, percent_lead, 2) == 1);
  CHECK(casual_winner(1, -1, 1, stock_lead, 2) == 0); // placements take precedence
  CHECK(casual_winner(1, -1, 0, tie, 2) == -1);
  CHECK(casual_winner(2, -1, -1, percent_lead, 2) == 1); // KO win
  CHECK(casual_winner(2, -1, 0, nullptr, 2) == 0);       // report without placements
  CHECK(casual_winner(1, -1, -1, nullptr, 2) == -1);
  for (int lras : {-1, 0, 1}) {
    CHECK(casual_winner(7, lras, 0, stock_lead, 2) == -1);
    CHECK(casual_winner(7, lras, 1, nullptr, 2) == -1);
  }
  for (int players : {0, 1, 3, 4}) {
    CHECK(casual_winner(1, -1, -1, stock_lead, players) == -1);
    CHECK(casual_winner(2, -1, 0, nullptr, players) == -1);
  }
  std::puts("yougame_ranked: casual/Direct timeout and KO winners, No Contest and multiplayer exclusions ok");
}

// First to two of the games the room settled, sent once both clients agree; duplicated receipts.
void first_to_two() {
  for (const std::vector<int>& winners : {std::vector<int>{0, 0}, std::vector<int>{0, 1, 0}, std::vector<int>{1, 0, 1}}) {
    FakeRoom room;
    room.start();
    for (size_t i = 0; i < winners.size(); ++i) {
      room.game(winners[i]);
      const auto out = room.tick();
      CHECK(out.size() == 1 && out[0]["t"] == "game-report" && out[0]["id"] == "g" + std::to_string(i + 1) + "-t0");
      CHECK(out[0]["winner"] == (winners[i] == 0 ? "participant-a" : "participant-b"));
      CHECK(room.set.awaiting_receipt() && !room.set.decided());
      room.now += 2000;
      CHECK(room.tick() == out);   // an unchanged retry
      room.receipt(out[0]["id"], winners[i]);
      room.receipt(out[0]["id"], winners[i]);   // the room re-broadcasts a receipt: it counts once
      CHECK(!room.set.awaiting_receipt());
      CHECK(room.set.decided() == (i + 1 == winners.size()));
    }
    const int winner = winners.back();
    const auto final = room.tick();
    CHECK(final.size() == 1 && final[0] == json({{"t", "finish"}, {"matchId", "set-1"}, {"winner", winner == 0 ? "participant-a" : "participant-b"}}));
    room.set.complete(0);   // a C2 after first-to-2 adds nothing
    room.set.quit();        // nor does leaving after it (review item 1: a 2-0 winner who leaves)
    room.set.lost();
    room.now += 2000;
    CHECK(room.tick() == final);
    CHECK(room.set.needs_drain() && !room.set.leaves_at_once());
    CHECK(!room.set.receive({{"t", "result"}, {"matchId", "stale-set"}}));
    CHECK(room.set.receive({{"t", "result"}, {"matchId", "set-1"}}));
    CHECK(!room.set.receive({{"t", "result"}, {"matchId", "set-1"}}));
    CHECK(room.tick().empty() && !room.set.needs_drain());
    CHECK(count_type(room.sent, "begin") == 1);
    CHECK(std::all_of(room.sent.begin(), room.sent.end(), [&](const json& m) { return m["t"] != "finish" || m == final[0]; }));
  }
  // The game repeats frame 1 while it waits for the opponent: a game is counted once, so both
  // players' report ids match however many times each one's first frame was sent.
  {
    FakeRoom repeats; repeats.start();
    repeats.set.start_game(); repeats.set.start_game();
    CHECK(repeats.set.games_started() == 1);
    Game g; g.index = repeats.set.games_started(); g.winner = 0; repeats.set.report_game(g);
    repeats.set.start_game(); repeats.set.start_game(); repeats.set.start_game();
    CHECK(repeats.set.games_started() == 2);
  }
  // A game the room voids (reports differed) and draws do not count; nine settled games without a
  // winner end the set void.
  FakeRoom room; room.start();
  room.game(0); room.tick(); room.receipt("g1-t0", -1);
  room.game(1); room.tick(); room.receipt("g2-t0", 1);
  room.game(0); room.tick(); room.receipt("g3-t0", 0);
  CHECK(!room.set.decided());
  room.game(0); room.tick(); room.receipt("g4-t0", 0);
  CHECK(room.set.decided() && room.tick()[0]["winner"] == "participant-a");
  FakeRoom draws; draws.start();
  for (int i = 1; i <= kMaxGames; ++i) { draws.game(-1); draws.tick(); draws.receipt("g" + std::to_string(i) + "-t0", -1); }
  CHECK(draws.set.decided() && draws.tick()[0]["void"] == true);
  std::puts("yougame_ranked: first to two settled games (2-0, 2-1), duplicated receipts, voids and the game cap ok");
}

// Review item 1 (confirmed in run 1): the set ended only on C2 or abandon(), so a player who won
// 2-0 and left was reported as the loser. A winner known first makes any later leave a no-op.
void winner_who_leaves() {
  FakeRoom room(0); room.start();
  room.game(0); room.tick(); room.receipt("g1-t0", 0);
  room.game(0); room.tick(); room.receipt("g2-t0", 0);
  room.set.quit();
  CHECK(room.tick()[0]["winner"] == "participant-a" && !room.set.leaves_at_once());
  // Leaving with the deciding receipt still on its way: no concession before it arrives.
  FakeRoom early(0); early.start();
  early.game(0); early.tick(); early.receipt("g1-t0", 0);
  early.game(0); early.tick();
  early.set.quit();
  CHECK(!early.set.decided() && !early.set.leaves_at_once());
  early.receipt("g2-t0", 0);
  CHECK(early.tick()[0]["winner"] == "participant-a");
  std::puts("yougame_ranked: a winner who leaves keeps the set (review item 1) ok");
}

void forfeits_and_leaving() {
  // A No Contest naming the other player (the game ends a game that way on a disconnect or a desync,
  // naming the opponent): this client's report gives it the game and the set. Seen from A here and,
  // in the mirror case where only B's game saw it, from B naming A.
  for (int local : {0, 1}) {
    FakeRoom room(local); room.host = local == 0; room.start();
    room.game(1); room.tick(); room.receipt("g1-t0", 1);
    room.game(0, true);
    CHECK(room.set.decided() && room.set.over());
    CHECK(room.tick()[0]["t"] == "game-report");   // the final waits for the game's receipt
    room.receipt("g2-t0", 0);
    CHECK(room.tick()[0] == json({{"t", "finish"}, {"matchId", "set-1"}, {"winner", "participant-a"}}));
  }
  // The local player quits between games: concedes, and leaves at once (flush, then close).
  FakeRoom quitter(1); quitter.host = false; quitter.start();
  quitter.game(0); quitter.tick(); quitter.receipt("g1-t0", 0);
  quitter.set.quit();
  CHECK(quitter.set.leaves_at_once());
  const auto last = quitter.set.flush();
  CHECK(last.size() == 1 && last[0] == json({{"t", "finish"}, {"matchId", "set-1"}, {"winner", "participant-a"}}));
  // The other player's connection broke instead: it claims the set (never concedes it).
  FakeRoom stayer(0); stayer.start();
  stayer.game(1); stayer.tick(); stayer.receipt("g1-t0", 1);
  stayer.set.lost();
  CHECK(stayer.tick()[0]["winner"] == "participant-a" && !stayer.set.leaves_at_once());
  // Lost with its own game report still waiting: the claim waits for the receipt, which may decide.
  FakeRoom race(1); race.host = false; race.start();
  race.game(0); race.tick(); race.receipt("g1-t0", 0);
  race.game(0); race.tick();
  race.set.lost();
  CHECK(!race.set.decided());
  race.receipt("g2-t0", 0);
  CHECK(race.tick()[0]["winner"] == "participant-a");   // first-to-2, not the claim for B
  // Status 40 before any game: nothing to concede. No match started: nothing to drain.
  FakeRoom unplayed; unplayed.set.quit(); CHECK(unplayed.tick().empty() && !unplayed.set.needs_drain());
  FakeRoom nomatch; nomatch.host = false; nomatch.set.start_game(); nomatch.set.quit();
  CHECK(!nomatch.set.needs_drain() && nomatch.set.flush().empty());
  std::puts("yougame_ranked: No Contest forfeits, quit concedes at once, lost claims after its receipts ok");
}

void quit_after_config_and_receipts() {
  using yougame::online_test::QuitAfter;
  for (const char* bad : {"", "0", "-1", "+1", " 1", "1 ", "1x", "1.5", "2147483648"})
    CHECK(QuitAfter(0, bad).game() == 0);
  CHECK(QuitAfter(0, nullptr).game() == 0);
  for (int mode : {-1, 1, 2, 3}) CHECK(QuitAfter(mode, "1").game() == 0);
  CHECK(QuitAfter(0, "2").game() == 2);
  CHECK(QuitAfter(0, "2147483647").game() == 2147483647);

  FakeRoom room; room.start();
  room.game(1); room.tick();
  QuitAfter quit(0, "2");
  int closes = 0;
  const auto poll = [&] { quit.poll(room.set.games_received(), [&] { ++closes; room.set.quit(); }); };
  poll(); CHECK(closes == 0 && room.set.games_received() == 0);
  room.receipt("unrelated", 0); poll(); CHECK(closes == 0 && room.set.games_received() == 0);
  room.receipt("g1-t0", 1); room.receipt("g1-t0", 1);
  poll(); CHECK(closes == 0 && room.set.games_received() == 1);
  room.game(0); room.tick(); poll(); CHECK(closes == 0);
  room.receipt("g2-t0", -1);   // a void still has its receipt; game 2 is the requested boundary
  poll(); poll();
  CHECK(closes == 1 && quit.triggered() && room.set.games_received() == 2);
  CHECK(room.set.leaves_at_once() && room.tick().empty());   // the pump cannot race the flush
  const auto last = room.set.flush();
  CHECK(last.size() == 1 && last[0]["t"] == "finish" && last[0]["winner"] == "participant-b");
  room.set.room_closed();
  CHECK(room.set.flush().empty() && room.tick().empty());

  FakeRoom refused; refused.start(); refused.game(0); refused.tick();
  refused.set.refused({{"action", "game-report"}, {"id", "g1-t0"}, {"error", "Invalid result detail"}});
  CHECK(!refused.set.awaiting_receipt() && refused.set.games_received() == 0);
  std::puts("yougame_ranked: quit-after parsing, own unique receipts, voids, refusals and one-shot cleanup ok");
}

void c2_and_game_setup() {
  // C2 before any decision (a future flow) decides from the settled games once they are in.
  FakeRoom early; early.start();
  early.game(0); early.tick();
  early.set.complete(0);
  CHECK(!early.set.decided());
  early.receipt("g1-t0", 0);
  CHECK(early.tick()[0]["winner"] == "participant-a");
  // A GameSetup port: first-to-2 is off, C2 alone decides (here a best of 5 at 3-1).
  FakeRoom setup; setup.start(); setup.set.game_setup();
  int i = 0;
  for (int w : {0, 0, 1, 0}) { setup.game(w); setup.tick(); setup.receipt("g" + std::to_string(++i) + "-t0", w); }
  CHECK(!setup.set.decided());
  setup.set.complete(0);
  CHECK(setup.tick()[0]["winner"] == "participant-a");
  setup.set.complete(0);
  CHECK(setup.tick().empty());
  // An abnormal C2 names nobody: void.
  FakeRoom abnormal; abnormal.start(); abnormal.set.game_setup(); abnormal.game(0); abnormal.tick();
  abnormal.receipt("g1-t0", 0); abnormal.set.complete(1);
  CHECK(abnormal.tick()[0]["void"] == true);
  std::puts("yougame_ranked: C2 after first-to-2 adds no final; a GameSetup port's C2 decides ok");
}

void refusals() {
  // A game report the room refuses for good is dropped once: no resend loop, it counts nothing.
  FakeRoom room; room.start();
  room.game(0);
  const auto report = room.tick();
  room.set.refused({{"t", "command-error"}, {"action", "game-report"}, {"id", report[0]["id"]}, {"error", "Invalid result detail"}});
  CHECK(!room.set.awaiting_receipt());
  room.now += 10000;
  CHECK(room.tick().empty() && !room.set.decided());
  // A final refused while a game is pending is retried; refused for good, it stops.
  FakeRoom fin; fin.start(); fin.game(0); fin.tick(); fin.receipt("g1-t0", 0); fin.game(0); fin.tick(); fin.receipt("g2-t0", 0);
  CHECK(fin.tick()[0]["t"] == "finish");
  fin.set.refused({{"t", "command-error"}, {"action", "finish"}, {"error", "Wait for individual game reports before completing the match"}});
  fin.now += 2000;
  CHECK(fin.tick().size() == 1);
  fin.set.refused({{"t", "command-error"}, {"action", "finish"}, {"error", "A submitted result cannot be changed"}});
  fin.now += 2000;
  CHECK(fin.tick().empty() && fin.set.needs_drain());   // still waits for the room's result
  // Both reports in, the site still recording the game: the result is on its way, the fence holds
  // (review: releasing it here let a 2-0 winner who left concede before its deciding result).
  FakeRoom slow; slow.start(); slow.game(0);
  slow.tick();
  slow.set.refused({{"t", "command-error"}, {"action", "game-report"}, {"id", "g1-t0"}, {"error", "That game result is already being recorded"}});
  CHECK(slow.set.awaiting_receipt());
  slow.set.quit();
  CHECK(!slow.set.decided() && !slow.set.leaves_at_once());
  // The room recording the result: nothing more is sent, the drain waits for the result.
  FakeRoom closing; closing.start(); closing.game(0);
  closing.set.refused({{"t", "command-error"}, {"action", "game-report"}, {"id", "g1-t0"}, {"error", "The match result is being recorded"}});
  CHECK(closing.tick().empty() && closing.set.needs_drain());
  // Begin: a roster still settling is retried, at most kBeginTries times; a refusal for good stops it.
  FakeRoom begin; begin.set.start_game();
  int begins = 0;
  for (int t = 0; t < 100; ++t, begin.now += 2000) {
    begins += (int)begin.tick().size();
    begin.set.refused({{"t", "command-error"}, {"action", "begin"}, {"error", "Waiting for enough players"}});
  }
  CHECK(begins == kBeginTries && !begin.set.needs_drain());
  FakeRoom host; host.set.start_game(); CHECK(host.tick().size() == 1);
  host.set.refused({{"t", "command-error"}, {"action", "begin"}, {"error", "Only the host game can start a match"}});
  host.now += 2000; CHECK(host.tick().empty());
  // A room that never starts the match (on either side): the pair breaks up, nothing to drain.
  CHECK(!host.set.over());
  host.now += kStartWaitMs; host.tick();
  CHECK(host.set.over() && !host.set.needs_drain());
  FakeRoom guest; guest.host = false; guest.set.start_game(); guest.tick();
  guest.now += kStartWaitMs - 1; guest.tick(); CHECK(!guest.set.over());
  guest.now += 1; guest.tick(); CHECK(guest.set.over());
  guest.set.receive({{"t", "start"}, {"matchId", "late"}});   // it starts after all, before the pair broke
  CHECK(!guest.set.over() && guest.set.needs_drain());
  // Malformed room messages are ignored, never thrown on.
  room.set.receive({{"t", 5}, {"matchId", nullptr}});
  room.set.receive({{"t", "game-result"}, {"matchId", "set-1"}, {"id", 3}, {"ranking", "x"}});
  room.set.refused({{"t", "command-error"}, {"action", 1}, {"id", false}});
  std::puts("yougame_ranked: refused reports dropped, finish retried only while games settle, begin capped ok");
}

void ranks() {
  CHECK(ticket_error({{"code", "ranked_signed_out"}}) == "Sign in to YouGame to play Ranked.");
  CHECK(ticket_error({{"code", "ranked_off"}}) == "Ranked is off for this game.");
  CHECK(ticket_error({{"code", 3}, {"error", false}}) == "YouGame request failed");
  RankCache cache;
  CHECK(cache.header() == "Ranked Mode" && cache.visibility() == 0 && cache.response().size() == 16);
  // A ranked ticket for a player with no result yet (rankFor(1000, 0)): still plain.
  CHECK(!cache.standing({{"tier", "unranked"}, {"division", nullptr}, {"label", "Placements 0/5"}, {"placed", false}, {"matches", 0}, {"rating", 1000}}, 1000));
  CHECK(cache.header() == "Ranked Mode");
  CHECK(cache.standing({{"tier", "unranked"}, {"division", nullptr}, {"label", "Placements 2/5"}, {"placed", false}, {"matches", 2}, {"rating", 1016}}, 1016));
  CHECK(cache.header() == "Ranked - Placements 2/5" && cache.rank() == 0);
  json rank = {{"placed", true}, {"tier", "gold"}, {"division", 2}, {"label", "Gold II"}, {"matches", 5}, {"rating", 1240}};
  json event = {{"t", "result"}, {"queue", "ranked"}, {"ratings", {{"participant-a", {
    {"before", 1220}, {"after", 1240}, {"matches", 5}, {"rank", rank},
    {"rankBefore", {{"placed", false}, {"tier", "unranked"}, {"division", nullptr}}}
  }}}}};
  CHECK(!cache.result(event, "connection-a")); // participant, not connection id
  CHECK(cache.result(event, "participant-a"));
  CHECK(cache.header() == "Ranked - Gold II - 1240");
  CHECK(cache.response() == (std::vector<uint8_t>{1, 1, 8, 0x44, 0x9b, 0, 0, 0, 0, 0, 5, 0x41, 0xa0, 0, 0, 0}));
  auto game = event; game["t"] = "game-result"; game["ratings"]["participant-a"]["after"] = 1500;
  CHECK(!cache.result(game, "participant-a") && cache.rank() == 8);
  auto invalid = event; invalid["ratings"]["participant-a"]["after"] = "bad";
  CHECK(!cache.result(invalid, "participant-a") && cache.rank() == 8);
  // YouGame's label wins; without one the header is built from ranks.ts's names.
  CHECK(cache.standing({{"placed", true}, {"tier", "challenger"}, {"division", nullptr}, {"label", "Challenger"}, {"matches", 40}}, 1834.4));
  CHECK(cache.header() == "Ranked - Challenger - 1834" && cache.rank() == 19);
  CHECK(cache.standing({{"placed", true}, {"tier", "platinum"}, {"division", 3}, {"matches", 9}}, 1374));
  CHECK(cache.header() == "Ranked - Platinum III - 1374");
  CHECK(cache.standing({{"placed", true}, {"tier", "diamond"}, {"division", 1}, {"label", "Diamond I · x"}, {"matches", 9}}, 1660));
  CHECK(cache.header() == "Ranked - Diamond I  x - 1660");   // only plain ASCII reaches the game
  CHECK(cache.standing({{"placed", false}, {"tier", "unranked"}, {"matches", 3}, {"placementTotal", 5}}, 1000));
  CHECK(cache.header() == "Ranked - Placements 3/5");
  int tier_index = 0;
  for (const char* tier : {"bronze", "silver", "gold", "platinum", "diamond"}) {
    for (int division = 1; division <= 4; ++division) {
      rank["tier"] = tier; rank["division"] = division;
      CHECK(rank_enum(rank) == 1 + 3 * tier_index + (division == 1 ? 2 : division == 2 ? 1 : 0));
    }
    ++tier_index;
  }
  rank["tier"] = "challenger"; rank["division"] = nullptr; CHECK(rank_enum(rank) == 19);
  rank["placed"] = false; CHECK(rank_enum(rank) == 0);
  rank["placed"] = "yes"; CHECK(rank_enum(rank) == 0);
  // mpRank: applied only when nothing newer arrived since it was asked; "Unknown request" stops it.
  RankCache fresh;
  const uint64_t asked = fresh.version();
  json answer = {{"mode", "singles"}, {"rating", 1140}, {"matches", 5}, {"place", nullptr},
                 {"rank", {{"tier", "gold"}, {"division", 4}, {"label", "Gold IV"}, {"order", 9}, {"rating", 1140}, {"matches", 5}, {"placed", true}}}};
  CHECK(apply_rank_answer(fresh, answer, asked) && fresh.header() == "Ranked - Gold IV - 1140");
  const uint64_t stale = fresh.version();
  CHECK(fresh.result(event, "participant-a"));
  CHECK(!apply_rank_answer(fresh, answer, stale) && fresh.header() == "Ranked - Gold II - 1240");
  CHECK(!apply_rank_answer(fresh, {{"mode", "singles"}, {"rank", nullptr}}, fresh.version()));
  CHECK(unknown_request("Unknown request") && unknown_request("Unknown request mpRank") && !unknown_request("the YouGame window did not answer"));
  std::puts("yougame_ranked: ticket errors, rank cache, header strings, tier divisions, mpRank freshness ok");
}

// ------------------------------------------------------------------ the rooms Worker's duel rules
// A model of rooms/src/game-lobby.ts for a ranked SDK duel (queue ranked, two connections with one
// participant each, no stall claims, no Orca set rules, the site answering at once).
struct Outcome {
  std::vector<std::string> ranking;
  bool draw = false, isvoid = false;
  json detail;
  std::string key() const { return json{{"ranking", ranking}, {"draw", draw}, {"void", isvoid}}.dump(); }
  std::string full() const { return json{{"ranking", ranking}, {"draw", draw}, {"void", isvoid}, {"detail", detail}}.dump(); }
};
struct Group { std::string id; bool final = false; uint64_t at = 0; std::map<std::string, Outcome> reports; };

struct Rooms {
  static constexpr uint64_t GRACE = 15000;   // MATCH_RECONNECT_MS
  std::vector<std::string> conns = {"conn-a", "conn-b"}, pids = {"participant-a", "participant-b"};
  std::string host = "conn-a";
  int revision = 7;
  std::set<std::string> present = {"conn-a", "conn-b"};
  std::map<std::string, uint64_t> left;
  bool match = false, closing = false, disputed = false, closed = false;
  const std::string match_id = "set-1";
  std::map<std::string, Group> groups;
  std::map<std::string, Outcome> completed;
  std::map<std::string, json> receipts;
  std::vector<std::pair<std::string, json>> out;   // (connection, message) to deliver
  json result;                                      // the final result event
  uint64_t now = 0;
  // The site's callback takes this long: a settled game's receipt exists only once it answers
  // (deliver), and a retry meanwhile hears "That game result is already being recorded".
  uint64_t site_delay = 0;
  struct Delivery { uint64_t at; bool final; std::string id; Outcome outcome; std::string disposition; };
  std::deque<Delivery> outbox;

  void broadcast(const json& m) { for (const auto& c : conns) if (present.count(c)) out.push_back({c, m}); }
  std::string pid(const std::string& conn) const { return conn == conns[0] ? pids[0] : pids[1]; }
  bool abandoned(const std::string& c) const { return !present.count(c) && left.count(c) && now >= left.at(c) + GRACE; }
  std::vector<std::string> missing(const Group& g) const {
    std::vector<std::string> m;
    for (const auto& c : conns) if (!g.reports.count(c)) m.push_back(c);
    return m;
  }
  bool complete(const Group& g) const {
    const auto m = missing(g);
    if (m.empty()) return true;
    if (present.empty()) return false;
    return std::all_of(m.begin(), m.end(), [&](const std::string& c) { return abandoned(c); });
  }
  uint64_t deadline(const Group& g) const {
    uint64_t d = g.at + GRACE;
    for (const auto& c : missing(g)) if (!present.count(c) && left.count(c)) d = std::max(d, left.at(c) + GRACE);
    return d;
  }
  bool normalize(const json& msg, Outcome& o) const {
    if (msg.count("void") && msg["void"] == true) { o.ranking = pids; o.isvoid = true; return true; }
    if (msg.count("draw") && msg["draw"] == true) { o.ranking = pids; o.draw = true; return true; }
    const std::string w = str(msg, "winner");
    if (w == pids[0] || w == pids[1]) { o.ranking = {w, w == pids[0] ? pids[1] : pids[0]}; return true; }
    return false;
  }
  static bool clean_detail(const json& d) {
    if (!d.is_object() || d.size() > 8) return false;
    static const std::regex key("^[a-z]{1,4}$");
    const auto ok = [](const json& n) { return n.is_number_integer() && n.get<int64_t>() >= -1 && n.get<int64_t>() < (1ll << 31); };
    for (auto it = d.begin(); it != d.end(); ++it)
      if (!std::regex_match(it.key(), key) || !(ok(it.value()) || (it.value().is_array() && it.value().size() <= 4 && std::all_of(it.value().begin(), it.value().end(), ok)))) return false;
    return true;
  }
  void deliver(bool final, const std::string& id, const Outcome& o, const std::string& disposition) {
    json event = {{"t", final ? "result" : "game-result"}, {"id", id}, {"matchId", match_id}, {"ranking", o.ranking},
                  {"draw", o.draw}, {"void", o.isvoid}, {"disposition", disposition}, {"queue", "ranked"}, {"ratings", nullptr}};
    if (final && !o.isvoid) {
      const json won = {{"placed", false}, {"tier", "unranked"}, {"division", nullptr}, {"label", "Placements 1/5"}, {"matches", 1}};
      event["ratings"] = {{o.ranking[0], {{"before", 1000}, {"after", 1016}, {"matches", 1}, {"rank", won}}},
                          {o.ranking[1], {{"before", 1000}, {"after", 984}, {"matches", 1}, {"rank", won}}}};
    }
    receipts[match_id + "/" + id] = event;
    broadcast(event);
    if (final) { result = event; match = false; closed = true; }
  }
  void resolve(const std::string& key) {
    if (!groups.count(key)) return;
    const Group g = groups.at(key);
    std::vector<Outcome> reports;
    for (const auto& [c, o] : g.reports) reports.push_back(o);
    const bool same = !reports.empty() && !reports[0].isvoid &&
                      std::all_of(reports.begin(), reports.end(), [&](const Outcome& r) { return r.key() == reports[0].key(); });
    const bool whole = reports.size() == conns.size(), alone = !whole;
    const bool agree = same && complete(g) && whole;
    Outcome outcome;
    if (agree) outcome = reports[0];
    else { outcome.ranking = pids; outcome.isvoid = true; }
    const bool cut = alone && !g.final && groups.count("$final");
    const bool all_void = std::all_of(reports.begin(), reports.end(), [](const Outcome& r) { return r.isvoid; });
    if (!agree && !(whole && all_void) && !(alone && complete(g)) && !cut) disputed = true;
    completed[key] = outcome;
    groups.erase(key);
    if (g.final) closing = true;
    queue(g.final, g.id, outcome, outcome.isvoid ? "void" : "completed");
  }
  void queue(bool final, const std::string& id, const Outcome& o, const std::string& disposition) {
    outbox.push_back({now + site_delay, final, id, o, disposition});
    flush();
  }
  void flush() {
    while (!outbox.empty() && outbox.front().at <= now) {
      const Delivery d = outbox.front();
      outbox.pop_front();
      deliver(d.final, d.id, d.outcome, d.disposition);
    }
  }
  bool games_pending() const {
    return std::any_of(groups.begin(), groups.end(), [](const auto& kv) { return !kv.second.final; });
  }
  std::string command(const std::string& from, const json& msg) {
    const std::string t = str(msg, "t");
    if (t == "begin") {
      if (match || closed) return "A match is already active or complete";
      if (host != from) return "Only the host game can start a match";
      if (!msg.count("revision") || msg["revision"] != revision) return "The lobby changed; use its current roster revision";
      match = true;
      broadcast({{"t", "start"}, {"matchId", match_id}});
      return "";
    }
    if (t != "game-report" && t != "finish") return "Unsupported game lobby command";
    const bool final = t == "finish";
    const std::string id = final ? "$final" : str(msg, "id"), key = final ? "$final" : "game:" + id;
    if (receipts.count(str(msg, "matchId") + "/" + id)) { broadcast(receipts.at(str(msg, "matchId") + "/" + id)); return ""; }
    if (!match || str(msg, "matchId") != match_id) return "This report is not for your active match";
    if (closing) return "The match result is being recorded";
    if (!final && (groups.count("$final") || completed.size() >= 64)) return "The match is completing or has reached its report limit";
    if (msg.count("detail") && !msg["detail"].is_null() && !clean_detail(msg["detail"])) return "Invalid result detail";
    Outcome o;
    if (!normalize(msg, o)) return "Report a valid winner, full participant ranking, draw, or neutral cancellation";
    if (msg.count("detail")) o.detail = msg["detail"];
    if (final && games_pending() && !o.isvoid) return "Wait for individual game reports before completing the match";
    if (completed.count(key)) return "That game result is already being recorded";
    Group& g = groups[key];
    if (g.id.empty()) { g.id = id; g.final = final; g.at = now; }
    if (g.reports.count(from) && g.reports.at(from).full() != o.full()) return "A submitted result cannot be changed";
    g.reports[from] = o;
    if (final && !missing(g).empty() && complete(g)) { tick(); return ""; }
    if (complete(g)) resolve(key);
    return "";
  }
  void departed(const std::string& c) {
    present.erase(c);
    if (match && !closing) left[c] = now;
  }
  void tick() {
    flush();
    if (!match || closing) return;
    std::vector<std::string> survivors, gone, quit;
    for (const auto& c : conns) (present.count(c) ? survivors : gone).push_back(c);
    for (const auto& c : gone) if (abandoned(c)) quit.push_back(c);
    const std::string winner = survivors.size() == 1 ? pid(survivors[0]) : "";
    bool neutral = disputed;
    for (const auto& [k, g] : groups) {
      std::vector<Outcome> r;
      for (const auto& [c, o] : g.reports) r.push_back(o);
      if (std::any_of(r.begin(), r.end(), [](const Outcome& x) { return x.isvoid; })) neutral = true;
      if (std::any_of(r.begin(), r.end(), [&](const Outcome& x) { return x.key() != r[0].key(); })) neutral = true;
      if (g.final && std::any_of(r.begin(), r.end(), [&](const Outcome& x) { return x.draw || x.ranking[0] != winner; })) neutral = true;
    }
    if (!quit.empty() && !neutral && !winner.empty()) {
      std::vector<std::string> keys;
      for (const auto& [k, g] : groups) if (!g.final) keys.push_back(k);
      for (const auto& k : keys) resolve(k);
      Outcome o;
      o.ranking = {winner, winner == pids[0] ? pids[1] : pids[0]};
      groups.erase("$final");
      completed["$final"] = o;
      closing = true;
      queue(true, "$final", o, "forfeit");
      return;
    }
    std::vector<std::string> keys;
    for (const auto& [k, g] : groups) if (!g.final) keys.push_back(k);
    if (groups.count("$final")) keys.push_back("$final");
    for (const auto& k : keys)
      if (!closing && groups.count(k) && (complete(groups.at(k)) || now >= deadline(groups.at(k)))) resolve(k);
    if (!closing && !gone.empty() && quit.size() == gone.size() && survivors.size() < 2) {
      std::vector<std::string> games;
      for (const auto& [k, g] : groups) if (!g.final) games.push_back(k);
      for (const auto& k : games) resolve(k);
      Group& g = groups["$final"];
      g.id = "$final"; g.final = true; g.at = now;
      resolve("$final");
    }
  }
};

// One client: a Set, its socket, and the game around it.
struct Client {
  std::string conn, name;
  int local = 0;
  Set set;
  yougame::online_test::QuitAfter quit;
  int quit_calls = 0;
  bool open = true, draining = false, leaving_at_once = false;
  uint64_t delay = 0, drain_until = 0;
  std::deque<std::pair<uint64_t, json>> inbox;
};

struct Sim {
  Rooms rooms;
  Client a, b;
  uint64_t now = 0;
  std::vector<std::string> trace;   // the message sequence, for the report
  bool tracing = false;
  Sim() {
    a.conn = "conn-a"; a.name = "A"; a.local = 0;
    b.conn = "conn-b"; b.name = "B"; b.local = 1;
    for (Client* c : {&a, &b}) c->set.configure(c->local, rooms.pids);
  }
  void note(const std::string& line) { if (tracing) trace.push_back(line); }
  static std::string brief(const json& m) {
    json copy = m;
    if (copy.count("ratings") && copy["ratings"].is_object()) copy["ratings"] = "{...}";
    for (const char* k : {"detail", "round", "reason", "queue", "players"}) copy.erase(k);
    return copy.dump();
  }
  void send(Client& c, const json& m) {
    note(c.name + " -> " + brief(m));
    const std::string error = rooms.command(c.conn, m);
    if (!error.empty()) c.inbox.push_back({now + c.delay, {{"t", "command-error"}, {"action", m["t"]}, {"id", m.count("id") ? m["id"] : json()}, {"error", error}}});
  }
  void flush_room() {
    for (auto& [conn, m] : rooms.out) {
      Client& c = conn == a.conn ? a : b;
      if (c.open) c.inbox.push_back({now + c.delay, m});
    }
    rooms.out.clear();
  }
  void step(uint64_t dt = 50) {
    now += dt;
    rooms.now = now;
    for (Client* c : {&a, &b}) {
      while (c->open && !c->inbox.empty() && c->inbox.front().first <= now) {
        const json m = c->inbox.front().second;
        c->inbox.pop_front();
        if (m["t"] == "command-error") { note("room -> " + c->name + " " + brief(m)); c->set.refused(m); }
        else { if (c == &a) note("room -> both " + brief(m)); c->set.receive(m); }
      }
      if (c->open) c->quit.poll(c->set.games_received(), [&] { ++c->quit_calls; close(*c, 0); });
      if (c->open) for (const auto& m : c->set.due(now, c->conn == rooms.host, rooms.revision)) send(*c, m);
      // A draining client leaves once the room's result is in (or the room closed), or at its limit.
      if (c->open && c->draining && (!c->set.needs_drain() || now >= c->drain_until)) leave(*c);
    }
    rooms.tick();
    flush_room();
    // The room closes after its final result (4004 match-over): sockets drop once delivered.
    if (rooms.closed) for (Client* c : {&a, &b}) if (c->open && c->inbox.empty()) { c->open = false; c->set.room_closed(); }
  }
  void run(uint64_t ms) { for (uint64_t t = 0; t < ms; t += 50) step(); }
  void leave(Client& c) {
    if (!c.open) return;
    note(c.name + " leaves the room");
    c.open = false;
    rooms.departed(c.conn);
  }
  // yougame_link close_room: Quit / Lost / Over.
  void close(Client& c, int why) {
    if (why == 0) c.set.quit();
    if (why == 1) c.set.lost();
    if (c.set.leaves_at_once() || !c.set.needs_drain()) {
      for (const auto& m : c.set.flush()) send(c, m);
      leave(c);
    } else {
      c.draining = true;
      c.drain_until = now + 30000;
    }
  }
  void play(int winner, bool forfeit = false) {
    for (Client* c : {&a, &b}) c->set.start_game();
    run(200);
    for (Client* c : {&a, &b}) {
      Game g;
      g.index = c->set.games_started(); g.winner = winner; g.forfeit = forfeit;
      g.key = game_key(0x100 + c->set.games_started(), 31);
      const int chars[2] = {2, 20};
      g.detail = game_detail(forfeit ? 7 : 2, forfeit ? 1 - winner : -1, 31, chars, 2400);
      c->set.report_game(g);
    }
    run(300);
  }
  // The game threads break the pair once their set is decided (slippi_online.cpp).
  void break_pair_when_over() {
    for (Client* c : {&a, &b}) if (c->open && !c->draining && c->set.over()) close(*c, 2);
  }
};

void sim_series(std::vector<int> winners, bool print) {
  Sim sim;
  sim.tracing = print;
  for (int w : winners) { sim.play(w); sim.break_pair_when_over(); }
  sim.run(3000);
  CHECK(sim.rooms.result.is_object() && sim.rooms.result["disposition"] == "completed");
  CHECK(sim.rooms.result["ranking"][0] == (winners.back() == 0 ? "participant-a" : "participant-b"));
  CHECK(sim.a.set.settled() && sim.b.set.settled() && !sim.a.open && !sim.b.open);
  CHECK(!sim.rooms.disputed);
  if (print) {
    std::puts("yougame_ranked: rooms message sequence for a 2-1 set:");
    for (const auto& line : sim.trace) std::printf("  %s\n", line.c_str());
  }
}

void sim_quit_after() {
  using yougame::online_test::QuitAfter;
  // Either instance can leave after game 1 or a split game 2, whether it won game 1 or lost it.
  // Slow receipts keep the harness in the room; repeated polls never send another concession.
  for (int local : {0, 1}) for (int n : {1, 2}) for (int first : {0, 1}) {
    Sim sim; sim.tracing = true; sim.rooms.site_delay = 1000;
    Client& quitter = local == 0 ? sim.a : sim.b;
    Client& stayer = local == 0 ? sim.b : sim.a;
    quitter.quit = QuitAfter(0, n == 1 ? "1" : "2");
    for (int game = 1; game <= n; ++game) {
      sim.play(game == 1 ? first : 1 - first);
      CHECK(quitter.open && quitter.quit_calls == 0);   // reported, but no room receipt yet
      sim.run(1200);
      if (game < n) CHECK(quitter.open && quitter.set.games_received() == game);
    }
    CHECK(quitter.quit.triggered() && quitter.quit_calls == 1 && !quitter.open && !quitter.draining);
    CHECK(quitter.set.leaves_at_once() && quitter.set.games_received() == n);
    CHECK(stayer.open && !stayer.set.settled());
    const auto concessions = [&] {
      return std::count_if(sim.trace.begin(), sim.trace.end(), [&](const std::string& line) {
        return line.starts_with(quitter.name + " -> ") && line.find("\"t\":\"finish\"") != std::string::npos;
      });
    };
    CHECK(concessions() == 1);
    CHECK(quitter.set.final_message()["winner"] == sim.rooms.pids[1 - local]);
    sim.run(13000); CHECK(!stayer.set.settled());   // before the leave-rule grace
    sim.run(5000);   // the stayer's game sends no finish: the leave rule alone awards it
    CHECK(sim.rooms.result["disposition"] == "forfeit" && sim.rooms.result["void"] == false);
    CHECK(sim.rooms.result["ranking"][0] == sim.rooms.pids[1 - local]);
    CHECK(stayer.set.settled() && !stayer.open && !quitter.set.settled());
    CHECK(quitter.quit_calls == 1 && concessions() == 1 && !sim.rooms.disputed);
  }
  // Quitting at the deciding receipt preserves the already decided result, as a real quit does.
  {
    Sim sim; sim.a.quit = QuitAfter(0, "2");
    sim.play(0); sim.play(0); sim.run(3000);
    CHECK(sim.a.quit_calls == 1 && sim.a.set.settled() && sim.b.set.settled());
    CHECK(!sim.a.set.leaves_at_once() && sim.rooms.result["ranking"][0] == "participant-a");
    CHECK(sim.rooms.result["disposition"] == "completed");
  }
  std::puts("yougame_ranked: harness quits after receipt 1/2 once, closes, stayer receives leave-rule win ok");
}

void sim_leaving() {
  // Review item 1 end to end: A wins 2-0 and leaves at once after the deciding receipt (holds Z):
  // its set is decided, so it stays for the result, and the result is A's.
  {
    Sim sim;
    sim.play(0); sim.play(0);
    sim.close(sim.a, 0);   // quit after the decision
    sim.run(2000);
    sim.break_pair_when_over();
    sim.run(3000);
    CHECK(sim.rooms.result["ranking"][0] == "participant-a" && sim.rooms.result["disposition"] == "completed");
  }
  // Addendum A/B: B quits between games (concedes, closes at once); A's game sees the pair break
  // and claims. Both finals agree: completed for A at once, not void.
  {
    Sim sim;
    sim.play(1);
    sim.close(sim.b, 0);
    sim.run(500);
    sim.close(sim.a, 1);
    sim.run(3000);
    CHECK(sim.rooms.result["ranking"][0] == "participant-a" && sim.rooms.result["disposition"] == "completed");
    CHECK(sim.now < 20000);
  }
  // B quits but A's game says nothing (it froze): the leave rule gives A the set after 15 s.
  {
    Sim sim;
    sim.play(1);
    sim.close(sim.b, 0);
    sim.run(20000);
    CHECK(sim.rooms.result["ranking"][0] == "participant-a" && sim.rooms.result["disposition"] == "forfeit");
  }
  // B's game vanishes between games (no message at all); A claims: the leave rule gives A the set.
  {
    Sim sim;
    sim.play(1);
    sim.leave(sim.b);
    sim.close(sim.a, 1);
    sim.run(25000);
    CHECK(sim.rooms.result["ranking"][0] == "participant-a" && sim.rooms.result["disposition"] == "forfeit");
    CHECK(sim.a.set.settled());
  }
  // Run 1's behaviour, for contrast: the player who stayed conceded too. The two finals differ: void.
  {
    Sim sim;
    sim.play(1);
    sim.close(sim.b, 0);
    sim.close(sim.a, 0);
    sim.run(20000);
    CHECK(sim.rooms.result["void"] == true);
  }
  // The link between the games breaks while both stay in the room: both claim, the set is void.
  {
    Sim sim;
    sim.play(0);
    sim.close(sim.a, 1); sim.close(sim.b, 1);
    sim.run(20000);
    CHECK(sim.rooms.result["void"] == true);
  }
  // The link dies mid-game 2 with both still in the room: each game ends it as a No Contest naming
  // its opponent (mu_online.c end_game), so each claims the game and the set: void, never inverted.
  {
    Sim sim;
    sim.play(1);
    for (Client* c : {&sim.a, &sim.b}) c->set.start_game();
    sim.run(200);
    for (Client* c : {&sim.a, &sim.b}) {
      Game g; g.index = 2; g.key = game_key(0x202, 32); g.winner = c->local; g.forfeit = true;
      c->set.report_game(g);
    }
    sim.break_pair_when_over();
    sim.run(25000);
    CHECK(sim.rooms.result["void"] == true && sim.rooms.disputed);
  }
  // B's game is gone mid-game 2 (its socket too); A's game ends on the disconnect, a No Contest
  // naming B. A reports it and claims; the leave rule settles the game and the set for A.
  {
    Sim sim;
    sim.play(1);
    for (Client* c : {&sim.a, &sim.b}) c->set.start_game();
    sim.run(200);
    sim.leave(sim.b);
    Game g; g.index = 2; g.winner = 0; g.forfeit = true;
    sim.a.set.report_game(g);
    sim.break_pair_when_over();
    sim.run(25000);
    CHECK(sim.rooms.result["ranking"][0] == "participant-a" && sim.rooms.result["disposition"] == "forfeit");
  }
  // The race: A sees the deciding game's result first and breaks the pair; B's game sees the link
  // drop before its own receipt arrives. B's claim waits for the receipt, which decides: A's set.
  {
    Sim sim;
    sim.b.delay = 400;
    sim.play(0);
    for (Client* c : {&sim.a, &sim.b}) c->set.start_game();
    sim.run(200);
    for (Client* c : {&sim.a, &sim.b}) { Game g; g.index = 2; g.winner = 0; c->set.report_game(g); }
    while (!sim.a.set.decided()) sim.step();
    sim.close(sim.a, 2);
    CHECK(!sim.b.set.decided());
    sim.close(sim.b, 1);
    sim.run(3000);
    CHECK(sim.rooms.result["ranking"][0] == "participant-a" && sim.rooms.result["disposition"] == "completed");
  }
  // A slow site (3 s per callback): A wins 2-0 and holds Z while game 2 is still being recorded. Its
  // retry hears "already being recorded"; the fence holds, the deciding result arrives, A keeps it.
  {
    Sim sim;
    sim.rooms.site_delay = 3000;
    sim.play(0);
    sim.run(3000);
    for (Client* c : {&sim.a, &sim.b}) c->set.start_game();
    sim.run(200);
    for (Client* c : {&sim.a, &sim.b}) { Game g; g.index = 2; g.key = game_key(0x302, 31); g.winner = 0; c->set.report_game(g); }
    sim.run(2500);   // past a 2 s retry, before the site answers
    CHECK(!sim.a.set.decided());
    sim.close(sim.a, 0);
    sim.close(sim.b, 1);
    sim.run(15000);
    CHECK(sim.rooms.result["ranking"][0] == "participant-a" && sim.rooms.result["disposition"] == "completed");
  }
  // One game's end never reached B's set (no report): ids come from what both games negotiated, so
  // the next game still matches. The lone game voids; game 2 settles for A.
  {
    Sim sim;
    for (Client* c : {&sim.a, &sim.b}) c->set.start_game();
    sim.run(200);
    { Game g; g.index = 1; g.key = game_key(0x401, 31); g.winner = 0; sim.a.set.report_game(g); }
    sim.run(16000);
    for (Client* c : {&sim.a, &sim.b}) c->set.start_game();   // B's set still thinks game 1 runs
    sim.run(200);
    for (Client* c : {&sim.a, &sim.b}) { Game g; g.index = (uint32_t)c->set.games_started(); g.key = game_key(0x402, 2); g.winner = 0; c->set.report_game(g); }
    sim.run(1000);
    CHECK(sim.rooms.completed.count("game:r401-s31") && sim.rooms.completed.at("game:r401-s31").isvoid);
    CHECK(sim.rooms.completed.count("game:r402-s2") && !sim.rooms.completed.at("game:r402-s2").isvoid);
  }
  // A duplicated game receipt (the room re-broadcasts it on a retry) counts once: 1-1 goes to game 3.
  {
    Sim sim;
    sim.play(0);
    sim.rooms.broadcast(sim.rooms.receipts.at("set-1/r101-s31"));
    sim.flush_room();
    sim.run(500);
    sim.play(1);
    CHECK(!sim.a.set.decided() && !sim.b.set.decided());
    sim.play(1);
    sim.break_pair_when_over();
    sim.run(3000);
    CHECK(sim.rooms.result["ranking"][0] == "participant-b");
  }
  // C2 arriving after the first-to-2 final: still exactly one finish from each player.
  {
    Sim sim;
    sim.tracing = true;
    sim.play(0); sim.play(0);
    sim.a.set.complete(0); sim.b.set.complete(0);
    sim.break_pair_when_over();
    sim.run(3000);
    const auto finishes = std::count_if(sim.trace.begin(), sim.trace.end(), [](const std::string& l) { return l.find("\"t\":\"finish\"") != std::string::npos; });
    CHECK(finishes == 2 && sim.rooms.result["ranking"][0] == "participant-a");
  }
  std::puts("yougame_ranked: rooms model: leaving winner, quit/claim/leave rule, voids, mirrored No Contest, race, slow site, drifting ids, duplicate receipt, late C2 ok");
}

int main() {
  winners_from_the_record();
  casual_winners_from_the_record();
  first_to_two();
  winner_who_leaves();
  forfeits_and_leaving();
  quit_after_config_and_receipts();
  sim_quit_after();
  c2_and_game_setup();
  refusals();
  ranks();
  sim_series({0, 0}, false);
  sim_series({0, 1, 0}, true);
  sim_leaving();
  std::puts("yougame_ranked: all tests passed");
}
