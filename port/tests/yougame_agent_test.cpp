// Offline startup file checks; the test script supplies its own scratch directory.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_agent_test.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <tuple>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using yougame::agent_test::Settings;
using json = nlohmann::json;

struct Fixture {
  std::filesystem::path directory;
  std::string utf8_directory;
  explicit Fixture(const char* root) : directory(std::filesystem::u8path(root) / "profile-\xc3\xa9") {
    std::filesystem::create_directory(directory);
    const auto utf8 = directory.u8string();
    utf8_directory.assign(utf8.begin(), utf8.end());
  }
  ~Fixture() { std::filesystem::remove_all(directory); }
  void write(const std::string& bytes) const {
    std::ofstream file(directory / "agent-test.json", std::ios::binary);
    file.write(bytes.data(), bytes.size());
    CHECK(file.good());
  }
  Settings load(bool active = true) const {
    return yougame::agent_test::load(active, utf8_directory.c_str());
  }
  Settings load_json(const json& object) const { write(object.dump()); return load(); }
};

void inert_and_file_limits(Fixture& f) {
  auto empty = [](const Settings& s) { return s.arguments.empty() && s.environment.empty() && s.log.empty(); };
  CHECK(empty(f.load()));  // Missing file, including inside YouGame mode.
  CHECK(empty(yougame::agent_test::load(true, nullptr)));
  CHECK(empty(yougame::agent_test::load(true, "")));
  f.write(R"({"mute":true,"online":"ranked","timer":5,"code":"SAND#38","quitAfter":1})");
  CHECK(empty(f.load(false)));  // No options, env writes or logs outside YouGame.
  CHECK(empty(f.load_json(json::object())));
  f.write(std::string(4097, ' '));
  CHECK(f.load().log == "agent-test.json ignored: exceeds 4 KiB");
  CHECK(empty(f.load(false)));
  const std::string valid = R"({"mute":true})";
  f.write(valid + std::string(4096 - valid.size(), ' '));
  CHECK(f.load().arguments == std::vector<std::string>({"--volume", "0"}));
  f.write(valid + std::string(4097 - valid.size(), ' '));
  CHECK(f.load().arguments.empty() && f.load().environment.empty());
  for (const char* bad : {"", "{", "null", "[]", "true", "1", R"({"timer":1e999})", R"({"mute":true} trailing)"}) {
    f.write(bad);
    const auto s = f.load();
    CHECK(s.arguments.empty() && s.environment.empty());
    CHECK(s.log == "agent-test.json ignored: invalid JSON object");
  }
  f.write(std::string("{\"mute\":true}\0 trailing", 23));
  CHECK(f.load().arguments.empty());
  CHECK(f.load().log == "agent-test.json ignored: invalid JSON object");
  std::puts("yougame_agent: gating, missing file, 4 KiB boundary and bad JSON ok");
}

void keys_and_types(Fixture& f) {
  CHECK(f.load_json({{"mute", true}}).arguments == std::vector<std::string>({"--volume", "0"}));
  for (const char* mode : {"unranked", "ranked", "direct", "teams"}) {
    const auto s = f.load_json({{"online", mode}});
    CHECK(s.arguments == std::vector<std::string>({"--online-test", mode}));  // No suffix: existing parser's default.
    CHECK(s.environment.empty());
    CHECK(s.log == "agent-test.json applied default keys: online");
    for (int character : {0, 2, 25}) {
      CHECK(f.load_json({{"online", mode}, {"character", character}}).arguments ==
            std::vector<std::string>({"--online-test", std::string(mode) + ":" + std::to_string(character)}));
    }
  }
  for (const auto& [key, env, low, high] : {
         std::tuple{"timer", "MELEE_ONLINE_TEST_TIMER", 5, 600},
         std::tuple{"quitAfter", "MELEE_ONLINE_TEST_QUIT_AFTER", 1, 3}}) {
    for (int n : {low, high}) {
      const auto s = f.load_json({{key, n}});
      CHECK(s.environment == (std::vector<std::pair<std::string, std::string>>{{env, std::to_string(n)}}));
    }
  }
  const std::map<std::string, std::vector<json>> invalid = {
    {"mute", {false, 0, 1, "true", nullptr, json::array(), json::object()}},
    {"online", {"Teams", "teams:2", "teamsRanked", "ranked-teams", "Ranked", "ranked:2", "", 1, true, nullptr}},
    {"character", {-1, 26, 2.0, "2", true, nullptr, 18446744073709551615ull, -9223372036854775807ll}},
    {"timer", {4, 601, -1, 5.0, "5", true, nullptr, 18446744073709551615ull}},
    {"quitAfter", {0, 4, -1, 1.0, "1", false, nullptr, 18446744073709551615ull}},
    {"code", {1, true, nullptr, json::array(), json::object()}}
  };
  for (const auto& [key, values] : invalid) {
    for (const auto& value : values) {
      const auto s = f.load_json({{key, value}});
      CHECK(s.arguments.empty() && s.environment.empty());
      CHECK(s.log.find("ignored") != std::string::npos);
    }
  }
  // An invalid character falls back to the existing online parser's default.
  CHECK(f.load_json({{"online", "direct"}, {"character", 26}}).arguments ==
        std::vector<std::string>({"--online-test", "direct"}));
  const auto teams = f.load_json({{"online", "teams"}, {"timer", 15}, {"quitAfter", 2}, {"teamsRanked", true}});
  CHECK(teams.arguments == std::vector<std::string>({"--online-test", "teams"}));
  CHECK(teams.environment == (std::vector<std::pair<std::string, std::string>>{
      {"MELEE_ONLINE_TEST_QUIT_AFTER", "2"}, {"MELEE_ONLINE_TEST_TIMER", "15"}}));
  CHECK(teams.log == "agent-test.json applied default keys: quitAfter, timer, online; invalid/unknown or inapplicable keys ignored");
  CHECK(f.load_json({{"character", 2}}).arguments.empty());
  CHECK(f.load_json({{"character", 2}}).log.find("ignored") != std::string::npos);
  std::puts("yougame_agent: each key, strict types, numeric bounds and default character ok");
}

void code_and_logging(Fixture& f) {
  for (const std::string code : {"SAND#38", "A", "0123456789##", "#"}) {
    const auto s = f.load_json({{"code", code}});
    CHECK(s.environment == (std::vector<std::pair<std::string, std::string>>{{"MELEE_ONLINE_TEST_CODE", code}}));
  }
  for (const std::string code : {"", "ABCDEFGHIJKLM", "sand#38", "A B", "A-B", "A_B", "A\nB", "\xc3\x89#1", "A/B", "A\\B"}) {
    const auto s = f.load_json({{"code", code}});
    CHECK(s.environment.empty() && s.log.find("ignored") != std::string::npos);
  }
  CHECK(f.load_json({{"code", std::string("A\0B", 3)}}).environment.empty());
  const auto s = f.load_json({{"mute", true}, {"code", "SAND#38"}, {"timer", 600}, {"quitAfter", 3},
                              {"online", "direct"}, {"character", 25}, {"unknown\npath/private", true}, {"bad", false}});
  CHECK(s.log == "agent-test.json applied default keys: code, mute, quitAfter, timer, online, character; invalid/unknown or inapplicable keys ignored");
  CHECK(s.log.find('\n') == std::string::npos);
  CHECK(s.log.find(f.utf8_directory) == std::string::npos);
  CHECK(s.log.find("SAND#38") == std::string::npos);
  CHECK(s.arguments == std::vector<std::string>({"--volume", "0", "--online-test", "direct:25"}));
  CHECK(s.environment.size() == 3);
  CHECK(f.load_json({{"unknown", true}}).log == "agent-test.json; invalid/unknown or inapplicable keys ignored");
  std::puts("yougame_agent: code length/charset and single log with only known key names ok");
}

void cli_precedence(Fixture& f) {
  const auto s = f.load_json({{"mute", true}, {"online", "ranked"}, {"character", 25}, {"quitAfter", 2}});
  std::vector<std::string> original = {"melee_source", "--volume", "41", "--online-test", "direct:9:3", "--log-file", "scratch.log"};
  std::vector<char*> argv;
  for (auto& arg : original) argv.push_back(arg.data());
  const auto args = s.prepend(static_cast<int>(argv.size()), argv.data());
  // The production parser consumes in order: the explicit flags must be last, intact, and must
  // not acquire the file's character or mode. This also preserves unrelated flags and values.
  CHECK(args == std::vector<std::string>({"melee_source", "--volume", "0", "--online-test", "ranked:25",
                                        "--volume", "41", "--online-test", "direct:9:3", "--log-file", "scratch.log"}));
  CHECK(Settings{}.prepend(static_cast<int>(argv.size()), argv.data()) == original);
  CHECK(s.environment == (std::vector<std::pair<std::string, std::string>>{{"MELEE_ONLINE_TEST_QUIT_AFTER", "2"}}));
  f.write(R"({"online":"unranked","mute":true})");
  CHECK(s.prepend(static_cast<int>(argv.size()), argv.data()) == args);  // Snapshot, no watching/re-read.
  std::puts("yougame_agent: CLI precedence, unchanged explicit character/color and startup snapshot ok");
}

void pad_back_key(Fixture& f) {
  for (int seconds : {10, 900}) {
    const auto s = f.load_json({{"padBack", seconds}});
    CHECK(s.pad_back_seconds == seconds);
    CHECK(s.arguments.empty() && s.environment.empty());
    CHECK(s.log == "agent-test.json applied default keys: padBack");
    CHECK(f.load(false).pad_back_seconds == 0);
    CHECK(f.load(false).log.empty());
  }
  for (const json& value : {json(9), json(901), json("30"), json(30.5), json(true)}) {
    const auto s = f.load_json({{"padBack", value}});
    CHECK(s.pad_back_seconds == 0);
    CHECK(s.arguments.empty() && s.environment.empty());
    CHECK(s.log == "agent-test.json; invalid/unknown or inapplicable keys ignored");
    CHECK(s.log.find("padBack") == std::string::npos);
    CHECK(f.load_json({{"mute", true}, {"padBack", value}}).log ==
          "agent-test.json applied default keys: mute; invalid/unknown or inapplicable keys ignored");
  }
  CHECK(f.load_json({{"mute", true}, {"padBack", 30}}).log ==
        "agent-test.json applied default keys: mute, padBack");
  CHECK(yougame::agent_test::load(true, nullptr).pad_back_seconds == 0);
  CHECK(yougame::agent_test::load(true, "").pad_back_seconds == 0);
  f.write("{}");
  CHECK(f.load().pad_back_seconds == 0);
  std::puts("yougame_agent: padBack bounds, strict types, applied keys and active-mode gating ok");
}

void script_keys_require_hook(Fixture& f) {
  // This target deliberately compiles without YOUGAME_AGENT_TEST_HOOK.
  const auto s = f.load_json({{"script", "ab_vs_source.txt"}, {"rng_seed", 305419896}, {"time_base", 0},
      {"frames", 7200}, {"frame_times", "ft.csv"}, {"sim_times", "sim.csv"}, {"replay_dir", "replays"},
      {"record_native", true}, {"slippi_menus", false}, {"no_gc_adapter", true}, {"early_rng_seed", "1@60"},
      {"stopAt", 600}});
  CHECK(s.arguments.empty() && s.environment.empty());
  CHECK(s.log == "agent-test.json; invalid/unknown or inapplicable keys ignored");
}

void match_key_requires_hook(Fixture& f) {
  for (const auto engine : {yougame::agent_test::Engine::Source, yougame::agent_test::Engine::Static}) {
    const auto s = yougame::agent_test::map_keys({{"match", "31:2:20/c9"}}, engine);
    CHECK(s.arguments.empty() && s.environment.empty());
    CHECK(s.log == "agent-test.json; invalid/unknown or inapplicable keys ignored");
  }
  const auto s = f.load_json({{"match", "31:2:20/c9"}, {"mute", true}});
  CHECK(s.arguments == std::vector<std::string>({"--volume", "0"}));
  CHECK(s.log == "agent-test.json applied default keys: mute; invalid/unknown or inapplicable keys ignored");
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  Fixture fixture(argv[1]);
  inert_and_file_limits(fixture);
  keys_and_types(fixture);
  code_and_logging(fixture);
  cli_precedence(fixture);
  pad_back_key(fixture);
  script_keys_require_hook(fixture);
  match_key_requires_hook(fixture);
}
