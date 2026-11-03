// Window-free Static harness mapping, digest adapter, and EXI/report event sequence.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_agent_static.h"
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_test.h"
#include "yougame_transport.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame::agent_test;

static std::string environment(const Settings& s, const char* key) {
  for (const auto& [name, value] : s.environment) if (name == key) return value;
  return {};
}

static void mapping(const char* scratch) {
  const nlohmann::json keys = {{"mute", true}, {"online", "unranked"}, {"timer", 15},
                               {"quitAfter", 2}, {"padBack", 30}, {"fight", true}};
  const auto s = map_keys(keys, Engine::Static);
  CHECK(s.arguments == (std::vector<std::string>{"--volume", "0"}));
  CHECK(s.environment.size() == 3);
  CHECK(environment(s, "MELEE_PRACTICE_AUTOSTART") == "unranked");
  CHECK(environment(s, "MELEE_ONLINE_TEST_TIMER") == "15");
  CHECK(environment(s, "MELEE_ONLINE_TEST_QUIT_AFTER") == "2");
  CHECK(s.static_online_test && s.fight && s.pad_back_seconds == 30);
  CHECK(s.unsupported_online.empty());
  CHECK(s.log == "agent-test.json applied default keys: fight, mute, padBack, quitAfter, timer, online");
  char exe[] = "melee_port.exe", flag[] = "--volume", volume[] = "50";
  char* argv[] = {exe, flag, volume};
  CHECK(s.prepend(3, argv) == (std::vector<std::string>{exe, "--volume", "0", flag, volume}));
  const auto teams = map_keys({{"online", "teams"}, {"timer", 15}, {"quitAfter", 2}}, Engine::Static);
  CHECK(teams.arguments.empty());
  CHECK(teams.environment == (std::vector<std::pair<std::string, std::string>>{
      {"MELEE_ONLINE_TEST_QUIT_AFTER", "2"}, {"MELEE_ONLINE_TEST_TIMER", "15"},
      {"MELEE_PRACTICE_AUTOSTART", "teams"}}));
  CHECK(teams.static_online_test && teams.unsupported_online.empty());
  CHECK(teams.log == "agent-test.json applied default keys: quitAfter, timer, online");
  for (const char* mode : {"ranked", "direct", "Teams", "teams:2", "teamsRanked", "ranked-teams", "UNRANKED", ""}) {
    const auto ignored = map_keys({{"online", mode}, {"mute", true}}, Engine::Static);
    CHECK(!ignored.static_online_test && ignored.environment.empty());
    CHECK(ignored.arguments == (std::vector<std::string>{"--volume", "0"}));
    CHECK(ignored.unsupported_online == std::string("agent test: online ") + mode + " not supported on the Static Recomp");
    CHECK(ignored.log == "agent-test.json applied default keys: mute");
  }
  const auto empty = map_keys(nlohmann::json::object(), Engine::Static);
  CHECK(empty.arguments.empty() && empty.environment.empty() && empty.log.empty());
  CHECK(!empty.static_online_test && !empty.fight && !empty.pad_back_seconds);
  CHECK(map_keys({{"fight", false}}, Engine::Static).log.empty());
  const auto invalid = map_keys({{"timer", 4}, {"quitAfter", 0}, {"padBack", 901},
                                 {"fight", 1}, {"online", 1}, {"extra", true}}, Engine::Static);
  CHECK(invalid.environment.empty() && invalid.arguments.empty() && !invalid.fight);
  CHECK(!invalid.pad_back_seconds && !invalid.static_online_test && invalid.unsupported_online.empty());
  CHECK(invalid.log == "agent-test.json; invalid/unknown or inapplicable keys ignored");
  for (const auto engine : {Engine::Static, Engine::Source}) {
    for (int frame : {60, 36000}) {
      const auto stop = map_keys({{"stopAt", frame}}, engine);
      CHECK(stop.stop_at_frame == frame && stop.arguments.empty() && stop.environment.empty());
      CHECK(stop.log == "agent-test.json applied default keys: stopAt");
    }
    for (const nlohmann::json& value : {nlohmann::json(59), nlohmann::json(36001), nlohmann::json("600"),
                                        nlohmann::json(600.5), nlohmann::json(true)})
      CHECK(map_keys({{"stopAt", value}}, engine).stop_at_frame == 0);
  }
  for (int timer : {5, 600})
    CHECK(environment(map_keys({{"timer", timer}}, Engine::Static), "MELEE_ONLINE_TEST_TIMER") == std::to_string(timer));
  for (int quit : {1, 3})
    CHECK(environment(map_keys({{"quitAfter", quit}}, Engine::Static), "MELEE_ONLINE_TEST_QUIT_AFTER") == std::to_string(quit));
  for (int delay : {10, 900}) CHECK(map_keys({{"padBack", delay}}, Engine::Static).pad_back_seconds == delay);
  const auto unsupported = map_keys({{"online", "unranked"}, {"character", 22}, {"code", "TEST#1"}}, Engine::Static);
  CHECK(unsupported.environment.size() == 1 && unsupported.arguments.empty());
  CHECK(unsupported.log == "agent-test.json applied default keys: online; invalid/unknown or inapplicable keys ignored");
  const auto source = map_keys({{"online", "direct"}, {"character", 22}, {"code", "TEST#1"}});
  CHECK(source.arguments == (std::vector<std::string>{"--online-test", "direct:22"}));
  CHECK(environment(source, "MELEE_ONLINE_TEST_CODE") == "TEST#1");
  CHECK(!source.static_online_test && source.unsupported_online.empty());
  const auto directory = std::filesystem::u8path(scratch) / "profile";
  std::filesystem::create_directories(directory);
  { std::ofstream file(directory / "agent-test.json"); file << keys.dump(); CHECK(file.good()); }
  const auto loaded = load(true, directory.string().c_str(), Engine::Static);
  CHECK(loaded.arguments == s.arguments && loaded.environment == s.environment && loaded.log == s.log);
  CHECK(loaded.fight && loaded.static_online_test && loaded.pad_back_seconds == 30);
  CHECK(load(false, directory.string().c_str(), Engine::Static).log.empty());
  CHECK(load(true, nullptr, Engine::Static).log.empty());
  CHECK(load(true, "", Engine::Static).log.empty());
  { std::ofstream file(directory / "agent-test.json"); file << R"({"online":"teams"})"; CHECK(file.good()); }
  const auto loaded_teams = load(true, directory.string().c_str(), Engine::Static);
  CHECK(loaded_teams.arguments.empty() && loaded_teams.environment.size() == 1);
  CHECK(environment(loaded_teams, "MELEE_PRACTICE_AUTOSTART") == "teams");
  CHECK(loaded_teams.static_online_test && loaded_teams.unsupported_online.empty());
  const auto inactive_teams = load(false, directory.string().c_str(), Engine::Static);
  CHECK(inactive_teams.arguments.empty() && inactive_teams.environment.empty() && !inactive_teams.static_online_test);
  std::filesystem::remove_all(directory);
}

static uint32_t bits(float f) {
  uint32_t value;
  std::memcpy(&value, &f, sizeof value);
  return value;
}

static void adapter() {
  MuStatePod digest{};
  digest.scene_major = 8; // Static online scene differs from Source's Debug VS.
  digest.scene = 3;
  digest.match_frame = 54;
  auto& self = digest.player[1];
  self.present = 1; self.stocks = 3; self.action = 65;
  self.pos_x = bits(12); self.pos_y = bits(8); self.percent = bits(37); self.facing = bits(-1);
  auto& other = digest.player[0];
  other.present = 1; other.stocks = 2;
  other.pos_x = bits(2); other.pos_y = bits(4); other.percent = bits(90);
  MuLcancelView air{};
  air.port[3].present = 1; air.port[3].slot = 1; air.port[3].ground_or_air = 1;
  FightContext c{true, true, true, true, false, 1, 3, fight_stage_edge(8)};
  auto s = static_fight_state(c, digest, air);
  CHECK(s.has_value());
  CHECK(s->frame == 54 && s->self_x == 12 && s->self_y == 8 && s->facing == -1);
  CHECK(s->airborne && s->action == 65 && s->self_percent == 37);
  CHECK(s->opponent_x == 2 && s->opponent_y == 4 && s->opponent_percent == 90 && s->stage_edge == 56);
  CHECK(fight_input(*s).stick_x == -40);
  self.pos_x = bits(60);
  digest.match_frame = 12;
  s = static_fight_state(c, digest, air);
  CHECK(s && fight_input(*s).button == 0x0200 && fight_input(*s).stick_x == -60);
  c.stage_edge = fight_stage_edge(32);
  s = static_fight_state(c, digest, air);
  CHECK(s && fight_input(*s).button == 0 && fight_input(*s).stick_x == -80);
  CHECK(fight_stage_edge(-1) == fight_stage_edge(31));
  air.port[3].ground_or_air = 0;
  CHECK(!static_fight_state(c, digest, air)->airborne);
  for (bool FightContext::* gate : {&FightContext::enabled, &FightContext::yougame,
                                    &FightContext::online_test, &FightContext::in_match}) {
    c.*gate = false; CHECK(!static_fight_state(c, digest, air)); c.*gate = true;
  }
  c.replay = true; CHECK(!static_fight_state(c, digest, air)); c.replay = false;
  for (int port : {-1, 4}) { c.local_port = port; CHECK(!static_fight_state(c, digest, air)); }
  c.local_port = 3;
  for (int slot : {-1, 4}) { c.local_slot = slot; CHECK(!static_fight_state(c, digest, air)); }
  c.local_slot = 1;
  air.port[3].slot = 0; CHECK(!static_fight_state(c, digest, air)); air.port[3].slot = 1;
  air.port[3].present = 0; CHECK(!static_fight_state(c, digest, air)); air.port[3].present = 1;
  digest.match_frame = 0; CHECK(!static_fight_state(c, digest, air)); digest.match_frame = 12;
  for (auto* fighter : {&self, &other}) {
    fighter->stocks = 0; CHECK(!static_fight_state(c, digest, air)); fighter->stocks = 2;
    fighter->present = 0; CHECK(!static_fight_state(c, digest, air)); fighter->present = 1;
  }
  digest.player[2] = other; CHECK(!static_fight_state(c, digest, air)); digest.player[2] = {};
  for (auto* field : {&self.pos_x, &self.pos_y, &self.facing, &self.percent,
                     &other.pos_x, &other.pos_y, &other.percent}) {
    const auto saved = *field;
    *field = bits(std::numeric_limits<float>::quiet_NaN()); CHECK(!static_fight_state(c, digest, air));
    *field = bits(std::numeric_limits<float>::infinity()); CHECK(!static_fight_state(c, digest, air));
    *field = saved;
  }
  c.stage_edge = 0; CHECK(!static_fight_state(c, digest, air));
}

static void events() {
  using E = StaticEvent;
  for (int n : {1, 2, 3}) {
    const auto after = std::to_string(n);
    StaticMatch match(true, after.c_str());
    int leaves = 0;
    auto event = [&](E e, bool eligible = true) { match.event(e, eligible, [&] { ++leaves; }); };
    event(E::Report); event(E::GameEnd); event(E::Report);
    CHECK(!match.in_match() && match.finished() == 0 && leaves == 0);
    event(E::GameStart, false); event(E::GameEnd, false); event(E::Report, false);
    CHECK(match.finished() == 0 && !match.in_match());
    event(E::GameStart); CHECK(match.in_match());
    event(E::Report); CHECK(match.finished() == 0);
    event(E::Cancel); event(E::GameEnd); event(E::Report);
    CHECK(match.finished() == 0 && !match.in_match());
    for (int i = 1; i <= n; ++i) {
      event(E::GameStart); event(E::GameStart); CHECK(match.in_match());
      event(E::GameEnd); event(E::GameEnd); CHECK(!match.in_match());
      CHECK(match.finished() == i - 1 && leaves == 0);
      event(E::Report); event(E::Report);
      CHECK(match.finished() == i && leaves == (i == n ? 1 : 0));
    }
    CHECK(match.quitting());
    event(E::GameStart); event(E::GameEnd); event(E::Report);
    CHECK(!match.in_match() && match.finished() == n && leaves == 1);
  }
  StaticMatch off(false, "1");
  int leaves = 0;
  for (auto e : {E::GameStart, E::GameEnd, E::Report}) off.event(e, true, [&] { ++leaves; });
  CHECK(!off.armed() && !off.in_match() && !off.finished() && !leaves);
  for (const char* value : {static_cast<const char*>(nullptr), "0", "bad", "-1"}) {
    StaticMatch match(true, value);
    for (int game = 0; game < 4; ++game)
      for (auto e : {E::GameStart, E::GameEnd, E::Report}) match.event(e, true, [&] { ++leaves; });
    CHECK(match.finished() == 4 && !match.quitting() && leaves == 0);
  }
}

static void drain() {
  // Reporting a local end is insufficient: a delayed begin and the room receipt still owe IO.
  yougame::transport::Games games;
  games.start_game();
  games.report(0);
  uint64_t now = 100;
  int polls = 0;
  const bool settled = drain_static_results([&] { return games.agent_test_pending(); }, [&] { return now; },
      [&](int ms) {
        CHECK(ms == 50);
        now += ms;
        ++polls;
        if (polls == 1) {
          const auto due = games.due(now, true, 1, {"a", "b"});
          CHECK(due.size() == 1 && due[0]["t"] == "begin");
          games.started("game-1");
        } else if (polls == 2) {
          const auto due = games.due(now, true, 1, {"a", "b"});
          CHECK(due.size() == 1 && due[0]["t"] == "finish" && due[0]["winner"] == "a");
          games.receive({{"t", "result"}, {"matchId", "other-game"}});
          CHECK(games.agent_test_pending());
        } else if (polls == 3) {
          games.receive({{"t", "result"}, {"matchId", "game-1"}});
        }
      });
  CHECK(settled && polls == 3 && !games.agent_test_pending());
  games.start_game("never-receipted");
  games.report(1);
  const uint64_t start = now;
  CHECK(!drain_static_results([&] { return games.agent_test_pending(); }, [&] { return now; },
                              [&](int ms) { now += ms; }));
  CHECK(now - start == 30000 && games.agent_test_pending());
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  mapping(argv[1]);
  adapter();
  events();
  drain();
  std::printf("yougame_agent_static: %u checks passed (mapping, digest adapter, Static event counting, result drain)\n", checks);
}
#else
int main() { return 0; }
#endif
