// Offline Teams contract: real room roster/pump, pure end-record result, real casual outbox.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_room.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame::transport;
using yougame::ranked::team_winner;

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

static json roster() {
  // Deliberately shuffled: array index and connection id have no bearing on the port.
  return json::array({{{"id","c"},{"connectionId","conn-c"},{"slot",2},{"localIndex",0},{"team",0}},
                      {{"id","a"},{"connectionId","conn-a"},{"slot",0},{"localIndex",0},{"team",1}},
                      {{"id","d"},{"connectionId","conn-d"},{"slot",3},{"localIndex",0},{"team",1}},
                      {{"id","b"},{"connectionId","conn-b"},{"slot",1},{"localIndex",0},{"team",0}}});
}

static void room_teams() {
  RoomState<Socket> r; r.size = 4;
  for (int i = -1; i <= 4; ++i) CHECK(r.seat_team(i) == -1);
  for (const char* type : {"welcome", "lobby", "start"}) {
    read_roster(r, {{"t",type},{"participants",roster()}});
    CHECK(r.seat_team(0) == 1 && r.seat_team(1) == 0 && r.seat_team(2) == 0 && r.seat_team(3) == 1);
    auto missing = roster(); missing[0].erase("team");
    read_roster(r, {{"t",type},{"participants",missing}});
    CHECK(r.seat_team(2) == -1 && r.seat_team(0) == 1);
    read_roster(r, {{"t", "ping"}}); CHECK(r.seat_team(0) == 1); // no roster retains known teams
  }
  for (const json invalid : {json(-1), json(2), json("0"), json(0.0), json(true), json(nullptr)}) {
    auto bad = roster(); bad[0]["team"] = invalid;
    read_roster(r, {{"participants",bad}}); CHECK(r.seat_team(2) == -1);
  }
  read_roster(r, {{"participants",json::array()}});
  for (int i = 0; i < 4; ++i) CHECK(r.seat_team(i) == -1);
  auto bad = roster(); bad.push_back({{"slot",-1},{"team",0}}); bad.push_back({{"slot",4},{"team",1}});
  bad.push_back({{"slot","0"},{"team",0}});
  read_roster(r, {{"participants",bad}});
  CHECK(r.seat_team(0) == 1 && r.seat_team(4) == -1);

  // Exercise actual welcome/lobby/start dispatch for app-broker and legacy adapters.
  for (bool broker : {false, true}) {
    RoomState<Socket> room; room.size = 4; room.socket = std::make_unique<Socket>(); room.socket->broker = broker;
    auto lobby = roster(); lobby[0]["team"] = 1;
    auto start = roster(); start[0].erase("team");
    room.socket->messages = {{{"t","welcome"},{"participants",roster()}},
                             {{"t","lobby"},{"participants",lobby}},
                             {{"t","start"},{"matchId","teams-1"},{"participants",start}}};
    int seen = 0;
    room.socket->observe = [&] {
      if (seen == 1) CHECK(room.seat_team(2) == 0);
      if (seen == 2) CHECK(room.seat_team(2) == 1);
      if (seen == 3) { CHECK(room.seat_team(2) == -1 && room.match_id == "teams-1"); room.stop = true; }
      ++seen;
    };
    std::mutex mutex; RoomIO io; io.clock = [] { return 1; };
    pump_room(&room, mutex, io); CHECK(seen == 4);
  }
}

static void team_results() {
  // Every balanced seat/team permutation and every legal set of four placement bytes.
  for (int mask = 0; mask < 16; ++mask) {
    int teams[4], count = 0;
    for (int i = 0; i < 4; ++i) count += teams[i] = (mask >> i) & 1;
    if (count != 2) continue;
    for (int encoded = 0; encoded < 256; ++encoded) {
      int placements[4], first_teams = 0;
      for (int i = 0; i < 4; ++i) {
        placements[i] = (encoded >> (2 * i)) & 3;
        if (placements[i] == 0) first_teams |= 1 << teams[i];
      }
      const int expected = first_teams == 1 ? 0 : first_teams == 2 ? 1 : -1;
      for (int method : {1, 2}) CHECK(team_winner(method, -1, placements, teams) == expected);
    }
  }
  const int teams[4] = {0,1,0,1};
  const int shared_first[4] = {0,1,0,1}, survivor[4] = {0,1,3,2}, other[4] = {2,0,3,1};
  const int tie[4] = {0,0,1,1}, no_first[4] = {1,1,2,2};
  for (int method : {1,2}) {
    CHECK(team_winner(method,-1,shared_first,teams) == 0);
    CHECK(team_winner(method,-1,survivor,teams) == 0);
    CHECK(team_winner(method,-1,other,teams) == 1);
    CHECK(team_winner(method,-1,tie,teams) == -1);
    CHECK(team_winner(method,-1,no_first,teams) == -1);
    CHECK(team_winner(method,-1,nullptr,teams) == -1);
    CHECK(team_winner(method,-1,shared_first,nullptr) == -1);
    for (int lras : {-2,0,1,2,3,4,255}) CHECK(team_winner(method,lras,shared_first,teams) == -1);
    for (int invalid : {-1,4,255}) {
      int placements[4] = {0,1,0,invalid};
      CHECK(team_winner(method,-1,placements,teams) == -1);
      int missing[4] = {0,1,0,invalid};
      CHECK(team_winner(method,-1,shared_first,missing) == -1);
    }
    const int unbalanced[4] = {0,0,0,1};
    CHECK(team_winner(method,-1,shared_first,unbalanced) == -1);
  }
  for (int method : {0,3,4,5,6,7,8,255})
    for (int lras : {-1,0,1,2,3}) CHECK(team_winner(method,lras,shared_first,teams) == -1);
}

static void team_reports() {
  std::vector<std::string> ids = {"a","b","c","d"};
  int teams[4] = {1,0,0,1};
  const json win0 = {{"ranking",{"b","c","a","d"}},{"detail",{{"tw",0}}}};
  const json win1 = {{"ranking",{"a","d","b","c"}},{"detail",{{"tw",1}}}};
  CHECK(team_outcome(0,ids,teams) == win0 && team_outcome(1,ids,teams) == win1);
  CHECK(team_outcome(-1,ids,teams) == json({{"void",true}}));
  CHECK(team_outcome(2,ids,teams) == json({{"void",true}}));
  CHECK(team_outcome(0,{"a","b"},teams) == json({{"void",true}}));
  CHECK(team_outcome(0,{"a","b","c","c"},teams) == json({{"void",true}}));
  CHECK(team_outcome(0,{"a","b","c",""},teams) == json({{"void",true}}));
  CHECK(team_outcome(0,ids,nullptr) == json({{"void",true}}));

  Games games; games.start_game(); games.report_team(0,ids,teams);
  CHECK(games.due(1,true,7,ids)[0] == json({{"t","begin"},{"revision",7}}));
  games.started("teams-1");
  json expected = win0; expected["t"] = "finish"; expected["matchId"] = "teams-1";
  CHECK(games.due(2,false,7,ids) == std::vector<json>({expected}));
  games.start_game(); games.report_team(1,ids,teams);
  ids[0] = "replacement"; teams[0] = 0; // retries and queued games retain their original roster
  CHECK(games.due(2002,false,8,ids) == std::vector<json>({expected}));
  games.receive({{"t","result"},{"matchId","wrong"}});
  CHECK(games.due(4002,false,8,ids) == std::vector<json>({expected}));
  games.receive({{"t","result"},{"matchId","teams-1"}});
  games.started("teams-2"); expected = win1; expected["t"] = "finish"; expected["matchId"] = "teams-2";
  CHECK(games.due(4003,false,8,ids) == std::vector<json>({expected}));
  games.receive({{"t","result"},{"matchId","teams-2"}});
  CHECK(games.due(4004,false,8,ids).empty());
  games.start_game("teams-void"); games.report_team(-1,ids,teams);
  CHECK(games.due(4005,false,8,ids)[0] == json({{"t","finish"},{"matchId","teams-void"},{"void",true}}));

  RoomState<Socket> legacy; legacy.socket = std::make_unique<Socket>(); legacy.socket->broker = false;
  legacy.match_id = "legacy-teams"; legacy.pending_team_result = win0;
  expected = win0; expected["t"] = "finish"; expected["matchId"] = "legacy-teams";
  CHECK(due_messages(legacy,1) == std::vector<json>({expected}));
  CHECK(due_messages(legacy,2).empty());
}

int main() {
  room_teams(); team_results(); team_reports();
  std::puts("yougame_teams: roster/pump mapping, exhaustive end-record results, stable 4-seat finish/retries/void passed");
}
