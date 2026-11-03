// dropRoomAfter parsing in both build modes, and the real pump against the room-test fakes.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_agent_test.h"
#include "yougame_room.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
using json = nlohmann::json;
static constexpr const char* drop_env = "MELEE_ONLINE_TEST_DROP_ROOM_AFTER";
template<class T, class = void> struct HasDropRoom : std::false_type {};
template<class T> struct HasDropRoom<T, std::void_t<decltype(std::declval<T&>().agent_test_drop_room())>> : std::true_type {};
#ifdef YOUGAME_AGENT_TEST_HOOK
static_assert(HasDropRoom<yougame::transport::Stream>::value);
#else
static_assert(!HasDropRoom<yougame::transport::Stream>::value);
#endif

static void mapping(const char* scratch) {
  using namespace yougame::agent_test;
  const auto directory = std::filesystem::u8path(scratch);
  std::filesystem::create_directories(directory);
  for (auto engine : {Engine::Source, Engine::Static}) {
    for (const json& value : {json(330), json(600), json(900), json(uint64_t(330))}) {
      const auto settings = map_keys({{"dropRoomAfter", value}}, engine);
      CHECK(settings.arguments.empty());
#ifdef YOUGAME_AGENT_TEST_HOOK
      CHECK(settings.environment == (std::vector<std::pair<std::string, std::string>>{
          {drop_env, std::to_string(value.get<int>())}}));
      CHECK(settings.log == "agent-test.json applied default keys: dropRoomAfter");
#else
      CHECK(settings.environment.empty());
      CHECK(settings.log == "agent-test.json; invalid/unknown or inapplicable keys ignored");
#endif
      { std::ofstream file(directory / "agent-test.json"); file << json{{"dropRoomAfter", value}}; CHECK(file.good()); }
      const auto loaded = load(true, directory.string().c_str(), engine);
      CHECK(loaded.environment == settings.environment && loaded.log == settings.log);
      const auto inactive = load(false, directory.string().c_str(), engine);
      CHECK(inactive.environment.empty() && inactive.arguments.empty() && inactive.log.empty());
    }
    for (const json& value : {json(329), json(901), json(-1), json(0), json(330.0), json(330.5),
         json("330"), json(true), json(false), json(nullptr), json::array(), json::object(),
         json(std::numeric_limits<uint64_t>::max()), json(std::numeric_limits<int64_t>::min())}) {
      const auto settings = map_keys({{"dropRoomAfter", value}}, engine);
      CHECK(settings.arguments.empty() && settings.environment.empty());
      CHECK(settings.log == "agent-test.json; invalid/unknown or inapplicable keys ignored");
    }
  }
  std::filesystem::remove_all(directory);
}

#ifdef YOUGAME_AGENT_TEST_HOOK
using namespace yougame::transport;
// Reuse the existing room test's offline Broker/Stream/Fixture design; no live socket or game.
static json roster() {
  return json::array({{{"id","a"},{"connectionId","a"},{"slot",0},{"localIndex",0},{"user","account-a"}},
                      {{"id","b"},{"connectionId","b"},{"slot",1},{"localIndex",0},{"user","account-b"}}});
}
static json welcome(const std::string& match = "set-1") {
  // Lobby players deliberately have no seat: no dependency on the app's former relabel bug.
  return {{"t","welcome"},{"me","a"},{"room",{{"code","abcdef"}}},{"matchId",match},
          {"participants",roster()},{"players",json::array({{{"id","a"},{"user","account-a"}},{{"id","b"},{"user","account-b"}}})}};
}
struct Broker {
  uint64_t now = 1;
  struct Action { uint64_t at; std::function<void()> run; };
  std::deque<Action> actions;
  Events events{[this](int ms) { wait(ms); }};
  std::vector<json> requests, sent;
  std::vector<int> waits;
  int conn = 0, link_state = 2, disabled = 0;
  bool fail_send = false, fail_renew = false;
  std::function<void(const json&)> on_close;
  std::function<void(int)> on_open;
  std::function<void(const json&)> on_send;
  std::function<json(int)> on_ticket;
  void wait(int ms) {
    CHECK(ms >= 0); waits.push_back(ms);
    const auto until = now + ms;
    if (!actions.empty() && actions.front().at <= until) {
      auto action = std::move(actions.front()); actions.pop_front();
      now = std::max(now, action.at); action.run();
    } else now = until; // an empty fake wait consumes precisely the caller's budget
  }
  void at(uint64_t time, std::function<void()> action) { actions.push_back({time,std::move(action)}); }
  void push(const json& m) { events.push({{"conn",conn},{"event","message"},{"data",m.dump()}}); }
  void opened(int id) { events.push({{"conn",id},{"event","open"}}); }
  void closed(int code = 1006) { events.push({{"conn",conn},{"event","close"},{"code",code}}); }
  json request(const json& q, int ms) {
    CHECK(ms > 0); requests.push_back(q);
    const auto type = str(q,"type");
    if (type == "netOpen") {
      ++conn;
      if (on_open) on_open(conn); else opened(conn);
      return {{"conn",conn}};
    }
    if (type == "netClose" && on_close) on_close(q);
    if (type == "netSend") {
      if (fail_send) throw std::runtime_error("backed_up");
      auto m = json::parse(q["data"].get<std::string>()); sent.push_back(m);
      if (on_send) on_send(m);
    }
    if (type == "mpTicket") {
      if (on_ticket) return on_ticket(ms);
      if (fail_renew) throw std::runtime_error("temporary ticket failure");
      return {{"ticket","ticket-new"},{"room","abcdef"}};
    }
    if (type == "directOpen") return {{"link",257}};
    return json::object();
  }
  int count(const char* type) const {
    return (int)std::count_if(requests.begin(),requests.end(),[&](const json& q){return str(q,"type")==type;});
  }
  int messages(const char* type) const {
    return (int)std::count_if(sent.begin(),sent.end(),[&](const json& q){return str(q,"t")==type;});
  }
};
struct FakeSocket {
  Broker& broker;
  Stream stream;
  bool broker_mode = true;
  std::function<void()> on_drop;
  explicit FakeSocket(Broker& b) : broker(b), stream(b.events,[&b](const json& q,int ms){return b.request(q,ms);},[&b]{return b.now;}) {}
  bool open_session(const std::string&,const std::string& ticket,const std::string& room,std::string*,int ms=10000) { return stream.open(ticket,room,ms); }
  bool next(json& m,int ms) { return stream.next(m,ms); }
  bool closed() const { return stream.closed(); }
  bool recoverable() const { return broker_mode && stream.close_code()==1006; }
  bool brokered() const { return broker_mode; }
  uint64_t agent_test_first_open_at() const { return stream.agent_test_first_open_at(); }
  void agent_test_drop_room() { stream.agent_test_drop_room(); if (on_drop) on_drop(); }
  bool send(const json& m) { return stream.send(m); }
};
struct Fixture {
  Broker b;
  RoomState<FakeSocket> r;
  std::mutex mutex;
  RoomIO io;
  std::vector<std::string> logs;
  Fixture() {
    r.socket=std::make_unique<FakeSocket>(b);
    CHECK(r.socket->stream.open("ticket-old","abcdef",100));
    r.me=r.host_connection="a"; r.code="abcdef"; r.ticket="ticket-old";
    r.ticket_request={{"type","mpTicket"}}; r.port_ids={"a","b"}; r.revision=7;
    r.recovery.configure(r.code,r.me,roster());
    io.request=[this](const json& q,int ms){return b.request(q,ms);};
    io.clock=[this]{return b.now;}; io.pause=[this](int ms){b.wait(ms);}; io.link_state=[this](unsigned){return b.link_state;};
    io.disable_links=[this]{++b.disabled;};
    io.log=[this](const char* message){logs.emplace_back(message);};
  }
  void ranked() {
    r.ranked=true; r.set.configure(0,{"a","b"}); r.set.start_game();
    r.match_id="set-1"; r.recovery.start(r.match_id);
    r.set.receive({{"t","start"},{"matchId","set-1"}});
  }
  void report(int index=1) {
    yougame::ranked::Game g; g.index=index; g.winner=0; r.set.report_game(g);
  }
  void tick() { b.push({{"t","test-tick"}}); }
  void stop_at(uint64_t at) { b.at(at,[this]{r.stop=true;tick();}); }
  void run() { pump_room(&r,mutex,io); CHECK(b.actions.empty()); }
};

static json agent_roster() {
  auto participants = roster();
  for (auto& p : participants) p["hn"] = true;
  return participants;
}
static void live(Fixture& f) {
  f.r.mode = 1; f.r.in_game = true; f.r.match_id = "game-1";
  f.r.participants = agent_roster();
  f.r.recovery.start(f.r.match_id);
}
static size_t drop_logs(const Fixture& f) {
  return std::count_if(f.logs.begin(), f.logs.end(), [](const std::string& line) {
    return line.find("agent-test:") == 0;
  });
}
static void drop_and_renew() {
  agent_drop_room_attempted = false;
  CHECK(setenv(drop_env, "330", 1) == 0);
  Fixture f; live(f);
  CHECK(f.r.socket->agent_test_first_open_at() == 1);
  // The pump starts long after the socket opened: elapsed time belongs to the first open.
  f.b.now = 330000;
  f.b.on_close = [&](const json& q) {
    CHECK(f.b.now >= 330001 && q["conn"] == 1 && q["code"] == 4000);
    CHECK(f.r.socket->closed());
  };
  f.r.socket->on_drop = [&] {
    CHECK(f.b.count("netClose") == 1 && f.r.socket->closed() && f.r.socket->recoverable());
    CHECK(f.r.socket->stream.close_code() == 1006);
  };
  f.b.on_open = [&](int id) {
    if (id == 2) {
      CHECK(f.b.count("mpTicket") == 0 && f.r.socket->closed());
      throw std::runtime_error("HTTP 401");
    }
    CHECK(id == 3 && f.b.count("mpTicket") == 1);
    CHECK(f.b.requests.back()["ticket"] == "ticket-new");
    f.b.opened(id);
    auto m = welcome("game-1"); m["participants"] = agent_roster(); f.b.push(m);
  };
  f.b.at(330001, [&] { CHECK(f.b.count("netClose") == 0); f.tick(); });
  f.b.at(331501, [&] {
    CHECK(!f.r.recovery.pending() && f.r.ticket == "ticket-new");
    CHECK(f.b.count("netClose") == 1 && f.b.count("netOpen") == 3 && f.b.count("mpTicket") == 1);
    f.tick();
  });
  f.stop_at(700001); f.run();
  CHECK(f.r.socket->agent_test_first_open_at() == 1);
  f.b.on_close = {};
  CHECK(f.b.count("netClose") == 1 && f.b.count("netOpen") == 3 && drop_logs(f) == 1);
  CHECK(f.logs == std::vector<std::string>({
      "agent-test: dropped the room socket (dropRoomAfter 330 s)", "yougame: room ticket renewed"}));
  // A second room in the same process cannot consume the hook again.
  Fixture later; live(later); later.b.now = 900001; later.stop_at(900101); later.run();
  CHECK(later.b.count("netClose") == 0 && later.b.count("netOpen") == 1 && later.logs.empty());
}
static void refuses_unsafe_rooms() {
  const auto agents = agent_roster();
  std::vector<json> rosters;
  auto bad = agents; bad[0]["hn"] = false; rosters.push_back(bad);
  bad = agents; bad[1]["hn"] = false; rosters.push_back(bad);
  bad = agents; bad[1].erase("hn"); rosters.push_back(bad);
  bad = agents; bad[1]["hn"] = "true"; rosters.push_back(bad);
  bad = agents; bad[0]["hn"] = 1; rosters.push_back(bad);
  bad = agents; bad[0]["connectionId"] = "absent-self"; rosters.push_back(bad);
  bad = agents; bad.push_back({{"connectionId", "spectator"}}); rosters.push_back(bad);
  rosters.push_back(json::array());
  rosters.push_back(agents); // ranked by mode
  rosters.push_back(agents); // ranked Teams fact, regardless of mode
  for (size_t i = 0; i < rosters.size(); ++i) {
    agent_drop_room_attempted = false;
    Fixture f; live(f); f.r.participants = rosters[i];
    if (i == rosters.size() - 2) f.r.mode = 0;
    if (i == rosters.size() - 1) { f.r.mode = 3; f.r.ranked = true; }
    f.b.now = 330001;
    f.b.at(330101, [&] { f.r.mode = 1; f.r.ranked = false; f.r.participants = agents; f.tick(); });
    f.stop_at(900001); f.run();
    CHECK(f.b.count("netClose") == 0 && f.b.count("netOpen") == 1);
    CHECK(f.logs == std::vector<std::string>({"agent-test: dropRoomAfter refused: not an agent-only room"}));
  }
}
static void waits_for_live_match() {
  agent_drop_room_attempted = false;
  Fixture f; live(f); f.r.in_game = false; f.b.now = 340001;
  f.b.on_open = [&](int id) {
    f.b.opened(id); auto m = welcome("game-1"); m["participants"] = agent_roster(); f.b.push(m);
  };
  f.b.at(340101, [&] { CHECK(f.b.count("netClose") == 0); f.r.in_game = true; f.r.match_id.clear(); f.tick(); });
  f.b.at(340201, [&] { CHECK(f.b.count("netClose") == 0); f.r.match_id = "game-1"; f.tick(); });
  f.stop_at(340401); f.run();
  CHECK(f.b.count("netClose") == 1 && drop_logs(f) == 1);
}
static void first_open_survives_reconnect() {
  for (int seconds : {330, 900}) {
    agent_drop_room_attempted = false;
    Fixture f; live(f);
    f.b.closed(); json m;
    CHECK(!f.r.socket->next(m, 0) && f.r.socket->closed());
    f.b.now = 100001;
    CHECK(f.r.socket->stream.open("ticket-new", "abcdef", 100));
    CHECK(f.r.socket->agent_test_first_open_at() == 1);
    f.b.now = uint64_t(seconds) * 1000;
    CHECK(!agent_drop_room_due(f.r, f.io, seconds) && f.logs.empty());
    ++f.b.now;
    CHECK(agent_drop_room_due(f.r, f.io, seconds));
    CHECK(!agent_drop_room_due(f.r, f.io, seconds) && drop_logs(f) == 1);
  }
  // Steady-clock epoch zero is a valid first open time, not an uninitialized sentinel.
  Broker b; b.now = 0; FakeSocket socket(b);
  CHECK(socket.stream.open("ticket-old", "abcdef", 100));
  b.closed(); json m; CHECK(!socket.next(m, 0)); b.now = 5000;
  CHECK(socket.stream.open("ticket-new", "abcdef", 100));
  CHECK(socket.agent_test_first_open_at() == 0);
}
static void environment_bounds() {
  CHECK(agent_drop_room_seconds(nullptr) == 0);
  for (const char* bad : {"", "329", "901", "-330", "+330", "330.0", "330s", " 330", "330 ",
                        "999999999999999999999999999999999999999999999"}) {
    CHECK(agent_drop_room_seconds(bad) == 0);
    agent_drop_room_attempted = false;
    CHECK(setenv(drop_env, bad, 1) == 0);
    Fixture f; live(f); f.b.now = 900001; f.stop_at(900101); f.run();
    CHECK(f.b.count("netClose") == 0 && f.logs.empty() && !agent_drop_room_attempted);
  }
  for (const char* value : {"330", "600", "900"})
    CHECK(agent_drop_room_seconds(value) == std::atoi(value));
  CHECK(unsetenv(drop_env) == 0);
  agent_drop_room_attempted = false;
  Fixture f; live(f); f.b.now = 900001; f.stop_at(900101); f.run();
  CHECK(f.b.count("netClose") == 0 && f.logs.empty() && !agent_drop_room_attempted);
}
#endif
int main(int argc, char** argv) {
  CHECK(argc == 2);
  mapping(argv[1]);
#ifdef YOUGAME_AGENT_TEST_HOOK
  environment_bounds();
  drop_and_renew();
  refuses_unsafe_rooms();
  waits_for_live_match();
  first_open_survives_reconnect();
  CHECK(unsetenv(drop_env) == 0);
#endif
  std::puts("yougame_drop_room: mapping, build gating, elapsed time, once per process, agent-only gate and recovery passed");
}
