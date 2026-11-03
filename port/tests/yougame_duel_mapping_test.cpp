// Offline duel contract: production seat mapping, room dispatch, ranked receipts and casual outbox.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_room.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame::transport;

struct Socket {
  bool broker = true;
  std::deque<json> messages;
  std::function<void()> observe;
  std::vector<json> sent;
  bool brokered() const { return broker; }
  bool closed() const { return false; }
  bool recoverable() const { return false; }
  bool open_session(const std::string&, const std::string&, const std::string&, std::string*, int) { return false; }
  bool next(json& m, int) {
    if (observe) observe();
    if (messages.empty()) return false;
    m = messages.front(); messages.pop_front(); return true;
  }
  bool send(const json& m) { sent.push_back(m); return true; }
};

static json roster(bool reverse, int ada_team) {
  json players = json::array({
    {{"id","ada"},{"connectionId","conn-ada"},{"slot",0},{"localIndex",0},{"team",ada_team}},
    {{"id","bo"},{"connectionId","conn-bo"},{"slot",1},{"localIndex",0},{"team",1-ada_team}}});
  if (reverse) std::swap(players[0], players[1]);
  return players;
}

static void mapping(const json& participants, const std::string& me, int local) {
  // Independent expected values from 1c97e20: room slot n -> port n+1 -> participant n.
  CHECK(connection_seat(participants, me) == local);
  CHECK(connection_seat(participants, "absent") == -1);
  CHECK(port_participants(participants, 2) == std::vector<std::string>({"ada","bo"}));
  auto without_teams = participants;
  for (auto& p : without_teams) p.erase("team");
  CHECK(connection_seat(participants, me) == connection_seat(without_teams, me));
  CHECK(port_participants(participants, 2) == port_participants(without_teams, 2));
}

static void room_messages() {
  for (bool broker : {false, true}) for (int local : {0, 1}) {
    RoomState<Socket> r;
    r.socket = std::make_unique<Socket>(); r.socket->broker = broker;
    r.me = local == 0 ? "conn-ada" : "conn-bo";
    r.port_ids = port_participants(roster(true, 1), 2);
    r.set.configure(connection_seat(roster(true, 1), r.me), r.port_ids);
    CHECK(r.set.local_participant() == (local == 0 ? "ada" : "bo"));
    // Welcome/lobby/start change array order and team assignment without changing slots.
    r.socket->messages = {
      {{"t","welcome"},{"me",r.me},{"participants",roster(true, 1)}},
      {{"t","lobby"},{"participants",roster(false, 0)}},
      {{"t","start"},{"matchId","duel"},{"participants",roster(true, 0)}}};
    int seen = 0;
    r.socket->observe = [&] {
      CHECK(r.port_ids == std::vector<std::string>({"ada","bo"}));
      CHECK(r.set.local_participant() == (local == 0 ? "ada" : "bo"));
      for (int port = -1; port <= 4; ++port) CHECK(r.seat_team(port) == -1);
      if (seen == 3) { CHECK(r.match_id == "duel"); r.stop = true; }
      ++seen;
    };
    std::mutex mutex; RoomIO io; io.clock = [] { return 1; };
    pump_room(&r, mutex, io);
    CHECK(seen == 4);
  }
  // localIndex identifies the connection's first controller, not a global port.
  auto controllers = roster(true, 1);
  controllers.push_back({{"id","extra"},{"connectionId","conn-ada"},{"slot",3},{"localIndex",1},{"team",0}});
  CHECK(connection_seat(controllers, "conn-ada") == 0);
}

static void agent_roster() {
  RoomState<Socket> r;
  auto players = roster(false, 0);
  players[0]["hn"] = true; players[1]["hn"] = "true";
  read_roster(r, {{"participants", players}});
  CHECK(r.participants == players);
  CHECK(r.participants[0]["hn"].is_boolean() && r.participants[0]["hn"] == true);
  CHECK(r.participants[1]["hn"].is_string());
  read_roster(r, {{"t", "ping"}}); CHECK(r.participants == players);
  players[0].erase("hn"); players[1]["hn"] = false;
  read_roster(r, {{"participants", players}}); CHECK(r.participants == players);
  read_roster(r, {{"participants", json::object()}}); CHECK(r.participants.empty());
  read_roster(r, {{"participants", json::array()}}); CHECK(r.participants.empty());
  read_roster(r, {{"participants", players}});
  read_roster(r, {{"t", "join"}}); CHECK(r.participants.empty());
}

static void team_scope() {
  RoomState<Socket> r; r.size = 4;
  auto players = roster(true, 1);
  players.push_back({{"id","c"},{"connectionId","conn-c"},{"slot",2},{"localIndex",0},{"team",0}});
  players.push_back({{"id","d"},{"connectionId","conn-d"},{"slot",3},{"localIndex",0},{"team",1}});
  read_roster(r, {{"participants",players}});
  CHECK(port_participants(players, 4) == std::vector<std::string>({"ada","bo","c","d"}));
  CHECK(r.seat_team(0) == 1 && r.seat_team(1) == 0 && r.seat_team(2) == 0 && r.seat_team(3) == 1);
  r.size = 2;
  for (int port = 0; port < 4; ++port) CHECK(r.seat_team(port) == -1);
  read_roster(r, {{"participants",roster(false, 0)}});
  CHECK((r.port_teams == std::array<int,4>{-1,-1,-1,-1}));
}

static void ranked_set(const json& participants, bool split) {
  yougame::ranked::Set clients[2];
  const auto ids = port_participants(participants, 2);
  for (int local : {0, 1}) {
    clients[local].configure(connection_seat(participants, local == 0 ? "conn-ada" : "conn-bo"), ids);
    CHECK(clients[local].local_participant() == ids[local]);
    clients[local].receive({{"t","start"},{"matchId","duel-set"}});
  }
  const std::vector<int> winners = split ? std::vector<int>{0,1,0} : std::vector<int>{0,0};
  for (size_t i = 0; i < winners.size(); ++i) {
    const int winner = winners[i];
    const int placements[4] = {winner, 1-winner, 255, 255};
    for (auto& set : clients) {
      set.start_game();
      yougame::ranked::Game game;
      game.index = set.games_started();
      game.key = yougame::ranked::game_key(0x100 + i, 31);
      game.winner = yougame::ranked::game_winner(2, -1, -1, placements);
      CHECK(game.winner == winner);
      set.report_game(game);
      const auto report = set.due(i * 100 + 1, true, 7);
      CHECK(report.size() == 1 && report[0]["t"] == "game-report");
      CHECK(report[0]["winner"] == (winner == 0 ? "ada" : "bo"));
      CHECK(set.awaiting_receipt() && !set.over());
      json receipt = {{"t","game-result"},{"matchId","duel-set"},{"id",game.key},
                      {"ranking",winner == 0 ? json{"ada","bo"} : json{"bo","ada"}}};
      set.receive(receipt); set.receive(receipt); // retries count once
      CHECK(!set.awaiting_receipt());
      if (i + 1 < winners.size()) {
        CHECK(!set.over() && !set.decided());
        CHECK(set.due(i * 100 + 2, true, 7).empty()); // no finish at 1-1
      } else {
        CHECK(set.over() && set.decided());
        CHECK(set.due(i * 100 + 2, true, 7) == std::vector<json>({
          {{"t","finish"},{"matchId","duel-set"},{"winner","ada"}}}));
        CHECK(set.receive({{"t","result"},{"matchId","duel-set"},{"ranking",{"ada","bo"}}}));
        CHECK(set.settled());
      }
    }
  }
}

static void casual_reports(const json& participants) {
  for (bool broker : {false, true}) for (int winner : {0, 1}) {
    RoomState<Socket> r; r.socket = std::make_unique<Socket>(); r.socket->broker = broker;
    r.port_ids = port_participants(participants, 2); r.match_id = "casual-game";
    const int placements[4] = {winner, 1-winner, 255, 255};
    const int port = yougame::ranked::casual_winner(2, -1, -1, placements, r.size);
    CHECK(port == winner);
    if (broker) { r.games.start_game(r.match_id); r.games.report(port); }
    else r.pending_winner = port;
    CHECK(due_messages(r, 1) == std::vector<json>({
      {{"t","finish"},{"matchId","casual-game"},{"winner",winner == 0 ? "ada" : "bo"}}}));
  }
}

int main() {
  for (bool reverse : {false, true}) for (int team : {0, 1}) {
    auto participants = roster(reverse, team);
    for (int local : {0, 1}) mapping(participants, local == 0 ? "conn-ada" : "conn-bo", local);
    ranked_set(participants, true); ranked_set(participants, false);
    casual_reports(participants); // Unranked, Direct and Friends share the two-seat casual path
  }
  room_messages(); agent_roster(); team_scope();
  std::puts("yougame_duel_mapping: slot/port/participant mapping, both clients' 1-1/2-0 sets and casual reports passed");
}
