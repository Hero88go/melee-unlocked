// Window-free parsing and routing/precedence checks for app-launched input scripts.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_agent_script.h"
#include "yougame_agent_fight.h"
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_test.h"
#include <cstdio>
#include <cstdlib>

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame::agent_test;

// Exercise the policy without a window, devices, a game, or network services.
static void ranked_input_policy() {
  using json = nlohmann::json;
  const InputKey keys[] = {InputKey::Script, InputKey::Fight, InputKey::PadBack, InputKey::Keys};
  const json agents = json::array({{{"connectionId", "self"}, {"hn", true}},
                                    {{"connectionId", "other"}, {"hn", true}}});
  std::vector<json> rosters{agents};
  auto bad = agents; bad[0]["hn"] = false; rosters.push_back(bad); // local is included
  bad = agents; bad[1]["hn"] = false; rosters.push_back(bad);
  bad = agents; bad[1].erase("hn"); rosters.push_back(bad);
  bad = agents; bad[1]["hn"] = "true"; rosters.push_back(bad);
  bad = agents; bad[1]["hn"] = 1; rosters.push_back(bad);
  rosters.push_back(json::array());
  rosters.push_back(json::object());
  bad = agents; bad.push_back({{"connectionId", "newcomer"}}); rosters.push_back(bad);
  for (int mode : {-2, -1, 0, 1, 2, 3, 4, 5}) {
    for (size_t r = 0; r < rosters.size(); ++r) {
      InputPolicy policy;
      std::vector<std::string> reported;
      auto log = [&](const char* line) { reported.emplace_back(line); };
      const auto state = room_input_state(1, mode, false, rosters[r], "self");
      const bool expected = mode == -1 || (mode >= 1 && mode <= 4 && r == 0);
      for (auto key : keys) {
        for (int repeat = 0; repeat < 3; ++repeat)
          CHECK(policy.allowed(state, key, log) == expected);
      }
      CHECK(reported.size() == (mode == -1 ? 0u : 4u));
      if (mode != -1) for (int k = 0; k < 4; ++k) {
        const std::string suffix = mode == 0 ? " refused in ranked" : expected ?
            " allowed: agent-only room (2 agent players)" : " refused: not an agent-only room";
        CHECK(reported[k] == std::string("agent-test: ") + input_key_name(keys[k]) + suffix);
      }
    }
    // Queue policy ignores the room: unranked/direct may search before a roster exists.
    for (int bits = 0; bits < 16; ++bits) {
      const bool local = bits & 1, server = bits & 2, script = bits & 4, active = bits & 8;
      const bool expected = script ? mode != 0 && active && !local : local || server;
      CHECK(matchmaking_allowed(local, server, script, active, mode) == expected);
    }
  }
  InputPolicy live;
  std::vector<std::string> reported;
  auto log = [&](const char* line) { reported.emplace_back(line); };
  const FightSample sample{2, fight_input({1, 0, 0, 1, false, 0, 0, 50})};
  for (const auto& roster : {agents, rosters.back(), agents, rosters[3], agents}) {
    const auto state = room_input_state(42, 2, false, roster, "self");
    const bool expected = roster == agents;
    for (auto key : keys) CHECK(live.allowed(state, key, log) == expected);
    MuPadStatus pads[4]{};
    for (auto& pad : pads) { pad.button = 0x1000; pad.stick_y = 15; pad.err = -1; }
    CHECK(apply_fight(pads, sample, state, log) == expected);
    for (int port = 0; port < 4; ++port) {
      if (expected && port == sample.port)
        CHECK(pads[port].stick_x == 80 && pads[port].button == 0 && pads[port].err == 0);
      else
        CHECK(pads[port].button == 0x1000 && pads[port].stick_y == 15 && pads[port].err == -1);
    }
  }
  CHECK(reported.size() == 10); // Four keys, two outcomes each, plus apply_fight's two global diagnostics.
  CHECK(!live.allowed(room_input_state(42, 3, true, agents, "self"), InputKey::Script, log));
  CHECK(reported.back() == "agent-test: script refused in ranked"); // ranked teams fact
  CHECK(!live.allowed(room_input_state(42, 2, false, agents, "absent"), InputKey::Script, log));
  CHECK(live.allowed(room_input_state(43, 2, false, agents, "self"), InputKey::Script, log));
  CHECK(reported.back() == "agent-test: script allowed: agent-only room (2 agent players)");
}

static void source_perf_keys(const std::filesystem::path& dir) {
  using json = nlohmann::json;
  auto parse = [&](const json& object) { return map_keys(object, Engine::Source, dir); };
  std::filesystem::remove_all(dir / "new-output");
  std::filesystem::remove_all(dir / "outputs");
  const auto child = dir / "outputs";
  std::filesystem::create_directories(child);
  for (const auto& [key, flag] : {std::pair{"frame_times", "--frame-times"},
                               std::pair{"sim_times", "MELEE_SIM_TIMES"},
                               std::pair{"replay_dir", "--replay-dir"}}) {
    const bool env = std::string(key) == "sim_times", directory = std::string(key) == "replay_dir";
    for (const char* name : {"new-output", "outputs/new-output", "outputs\\new-output"}) {
      const auto settings = parse({{key, name}});
      const auto path = std::string(name) == "new-output" ? dir / "new-output" : child / "new-output";
      if (env) CHECK(settings.environment == (std::vector<std::pair<std::string, std::string>>{{flag, path.u8string()}}));
      else CHECK(settings.arguments == (std::vector<std::string>{flag, path.u8string()}));
      CHECK(settings.log == std::string("agent-test.json applied default keys: ") + key);
      if (directory) CHECK(std::filesystem::is_directory(path));
    }
    for (const auto& bad : {json(1), json(true), json(nullptr), json::array(), json::object(), json(""),
         json("/abs"), json("C:\\out"), json("C:out"), json("\\\\server\\out"), json("../out"),
         json("outputs/../out"), json("outputs\\..\\out"), json("a..b"), json("a\nb"),
         json(std::string("a\0b", 3)), json(std::string("a\x7f", 2)), json("."), json("outputs/")}) {
      const auto settings = parse({{key, bad}});
      CHECK(settings.arguments.empty() && settings.environment.empty());
      CHECK(settings.log == std::string("agent-test: ") + key + " invalid output path; ignored");
    }
    for (const char* bad : {"linked-fixtures/new-output", "link.txt", "a/child"}) {
      const auto settings = parse({{key, bad}});
      CHECK(settings.arguments.empty() && settings.environment.empty());
    }
    CHECK(map_keys({{key, "out"}}, Engine::Source).log == std::string("agent-test: ") + key + " invalid output path; ignored");
    const auto other = map_keys({{key, "out"}}, Engine::Static, dir);
    CHECK(other.arguments.empty() && other.environment.empty());
    CHECK(other.log == std::string("agent-test: ") + key + " not supported on the Static Recomp; ignored");
    if (!directory) {
      CHECK(parse({{key, "missing-parent/out"}}).arguments.empty());
      CHECK(parse({{key, "missing-parent/out"}}).environment.empty());
      CHECK(parse({{key, "outputs"}}).log.find("ignored") != std::string::npos);
      CHECK(parse({{key, "a"}}).log.find("applied default keys") != std::string::npos); // existing regular file
    }
  }
  CHECK(std::filesystem::is_directory(dir / "new-output"));
  CHECK(parse({{"replay_dir", "created/nested/replays"}}).log == "agent-test.json applied default keys: replay_dir");
  CHECK(std::filesystem::is_directory(dir / "created/nested/replays"));
  CHECK(parse({{"replay_dir", "a"}}).log.find("ignored") != std::string::npos);
  // Existing output symlinks are never followed, even when their targets are inside the data dir.
  for (const char* key : {"frame_times", "sim_times", "replay_dir"})
    CHECK(parse({{key, "link.txt"}}).log.find("ignored") != std::string::npos);
  for (const char* key : {"record_native", "slippi_menus", "no_gc_adapter"}) {
    for (bool enabled : {false, true}) {
      const auto settings = parse({{key, enabled}});
      if (std::string(key) == "slippi_menus")
        CHECK(settings.arguments == (std::vector<std::string>{"--slippi-menus", enabled ? "on" : "off"}));
      else if (std::string(key) == "record_native")
        CHECK(settings.arguments == (enabled ? std::vector<std::string>{"--record-native"} : std::vector<std::string>{}));
      else CHECK(settings.environment == (enabled ? std::vector<std::pair<std::string, std::string>>{{"MELEE_NO_GC_ADAPTER", "1"}} :
                                                   std::vector<std::pair<std::string, std::string>>{}));
      CHECK(settings.log == (enabled || std::string(key) == "slippi_menus" ?
            std::string("agent-test.json applied default keys: ") + key : ""));
    }
    for (const auto& bad : {json(0), json(1), json("true"), json(nullptr), json::array(), json::object()}) {
      const auto settings = parse({{key, bad}});
      CHECK(settings.arguments.empty() && settings.environment.empty());
      CHECK(settings.log == std::string("agent-test: ") + key + " invalid value; ignored");
    }
    CHECK(map_keys({{key, true}}, Engine::Static, dir).log.find("not supported on the Static Recomp") != std::string::npos);
  }
  for (int n : {600, 7200, 36000}) {
    CHECK(parse({{"frames", n}}).arguments == (std::vector<std::string>{"--frames", std::to_string(n)}));
    CHECK(parse({{"frames", n}}).log == "agent-test.json applied default keys: frames");
  }
  for (const auto& bad : {json(599), json(36001), json(-1), json(UINT64_MAX), json(600.0), json("600"), json(true), json(nullptr), json::array()})
    CHECK(parse({{"frames", bad}}).log == "agent-test: frames invalid value; ignored");
  for (const char* valid : {"0@1", "4294967295@4294967295", "0x12345678@060", "000@0x10"}) {
    const auto settings = parse({{"early_rng_seed", valid}});
    CHECK(settings.environment == (std::vector<std::pair<std::string, std::string>>{{"MELEE_TEST_EARLY_RNG_SEED", valid}}));
    CHECK(settings.log == "agent-test.json applied default keys: early_rng_seed");
  }
  for (const auto& bad : {json(1), json(true), json(nullptr), json::array(), json::object(), json(""), json("1"),
       json("1@0"), json("1@"), json("@1"), json("1@-1"), json("-1@1"), json("+1@1"), json("1@+1"),
       json("1@4294967296"), json("4294967296@1"), json("18446744073709551616@1"), json("1@2@3"),
       json("1@08"), json(" 1@1"), json("1@ 1"), json("1@1 "), json(std::string("1@1\0x", 5))}) {
    const auto settings = parse({{"early_rng_seed", bad}});
    CHECK(settings.environment.empty() && settings.arguments.empty());
    CHECK(settings.log == "agent-test: early_rng_seed invalid value; ignored");
  }
  CHECK(map_keys({{"early_rng_seed", "1@1"}}, Engine::Static, dir).environment.empty());
  // All CLI-mapped defaults precede the original args; no environment key has a CLI equivalent.
  const auto settings = parse({{"frames", 7200}, {"frame_times", "ft.csv"}, {"sim_times", "sim.csv"},
      {"replay_dir", "replays"}, {"record_native", true}, {"slippi_menus", false},
      {"no_gc_adapter", true}, {"early_rng_seed", "123@60"}});
  char exe[] = "melee_source", frames[] = "--frames", count[] = "900", ft[] = "--frame-times", file[] = "cli.csv",
       replay[] = "--replay-dir", folder[] = "cli-replays", menus[] = "--slippi-menus", on[] = "on", record[] = "--record-native";
  char* argv[] = {exe, frames, count, ft, file, replay, folder, menus, on, record};
  auto expected = std::vector<std::string>{exe};
  expected.insert(expected.end(), settings.arguments.begin(), settings.arguments.end());
  expected.insert(expected.end(), {frames, count, ft, file, replay, folder, menus, on, record});
  CHECK(settings.prepend(10, argv) == expected);
  CHECK(settings.log == "agent-test.json applied default keys: early_rng_seed, frame_times, frames, no_gc_adapter, record_native, replay_dir, sim_times, slippi_menus");
  for (const char* unsupported : {"vanilla_only", "no_mod_autodetect", "vsync", "audio_pacing"}) {
    const auto ignored = parse({{unsupported, true}});
    CHECK(ignored.arguments.empty() && ignored.environment.empty());
    CHECK(ignored.log == "agent-test.json; invalid/unknown or inapplicable keys ignored");
  }
  CHECK(settings.environment.size() == 3);
  CHECK(settings.log.find(dir.u8string()) == std::string::npos);
}

static void source_match_key(const std::filesystem::path& dir) {
  using json = nlohmann::json;
  const std::string match = "31:2:20/c9"; // Battlefield, human Fox versus level-9 CPU Falco.
  for (const std::string valid : {match, std::string("0x1f:0x2/x0:024/c011/x1"),
       std::string("31:2/x1/c9:20/c9/x0"), std::string("31:0:1:2:3:4:5"),
       std::string("31:2"), std::string("31:") + std::string(124, '0') + "2"}) {
    const auto s = map_keys({{"match", valid}});
    CHECK(s.arguments == (std::vector<std::string>{"--match", valid}));
    CHECK(s.environment.empty());
    CHECK(s.log == "agent-test.json applied default keys: match");
  }
  for (const auto& invalid : {json(1), json(1.0), json(true), json(nullptr), json::array(), json::object(),
       json(""), json("31"), json(":2"), json("31:"), json("31::2"), json("31:2:"),
       json("31:0:1:2:3:4:5:6"), json("31:2/"), json("31:2/c"), json("31:2/x"),
       json("31:2/z1"), json("31:2/c9junk"), json("31:2_c9"), json("battlefield:fox"),
       json("31:08"), json("0x:2"), json("31:2/c08"), json("31:2/x0x"),
       json("-1:2"), json("31:-2"), json("31:2/c-1"), json("31:2/x+1"), json("+31:2"),
       json("31: 2"), json("31:2\n"), json("0X1F:2"), json("31:2;quit"), json("31:2\\c9"),
       json(std::string("31:2\0:9", 7)), json("31:2\xc3\xa9"), json("2147483648:2"),
       json("31:999999999999999999999999999999999"),
       json(std::string("31:") + std::string(125, '0') + "2"), json(std::string(129, 'a'))}) {
    const auto s = map_keys({{"match", invalid}});
    CHECK(s.arguments.empty() && s.environment.empty());
    CHECK(s.log == "agent-test: match invalid value; ignored");
    const auto muted = map_keys({{"match", invalid}, {"mute", true}});
    CHECK(muted.arguments == (std::vector<std::string>{"--volume", "0"}));
    CHECK(muted.log == "agent-test.json applied default keys: mute\nagent-test: match invalid value; ignored");
  }
  // Presence of online refuses match even when the online value itself is invalid.
  for (const auto& online : {json("unranked"), json("ranked"), json("direct"), json("teams"),
       json(""), json("invalid"), json(false), json(nullptr), json(1), json::object()}) {
    const auto s = map_keys({{"match", match}, {"online", online}});
    CHECK(std::find(s.arguments.begin(), s.arguments.end(), "--match") == s.arguments.end());
    CHECK(s.environment.empty());
    CHECK(s.log.find("agent-test: match conflicts with online (offline only); ignored") != std::string::npos);
    CHECK(s.log.find("applied default keys: match") == std::string::npos);
    if (online.is_string() && (online == "unranked" || online == "ranked" || online == "direct" || online == "teams"))
      CHECK(s.arguments == (std::vector<std::string>{"--online-test", online.get<std::string>()}));
    else CHECK(s.arguments.empty());
  }
  const auto other = map_keys({{"match", match}}, Engine::Static);
  CHECK(other.arguments.empty() && other.environment.empty());
  CHECK(other.log == "agent-test: match not supported on the Static Recomp; ignored");
  { std::ofstream file(dir / "agent-test.json"); file << json({{"match", match}}).dump(); CHECK(file.good()); }
  const auto loaded = load(true, dir.u8string().c_str(), Engine::Source);
  CHECK(loaded.arguments == (std::vector<std::string>{"--match", match}));
  CHECK(loaded.log == "agent-test.json applied default keys: match");
  CHECK(load(false, dir.u8string().c_str()).arguments.empty());
  char exe[] = "melee_source", flag[] = "--match", explicit_match[] = "32:9:2/c9";
  char* argv[] = {exe, flag, explicit_match};
  CHECK(loaded.prepend(3, argv) == (std::vector<std::string>{exe, "--match", match, flag, explicit_match}));
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  const auto directory = std::filesystem::u8path(argv[1]);
  std::filesystem::create_directories(directory);
  auto parse = [&](const nlohmann::json& object, Engine engine = Engine::Static) {
    { std::ofstream file(directory / "agent-test.json"); file << object.dump(); CHECK(file.good()); }
    return load(true, directory.u8string().c_str(), engine);
  };
  for (const std::string name : {std::string("ace-0.9_Online.txt"), std::string("a"), std::string(64, 'a'), std::string(".menu")}) {
    { std::ofstream file(directory / name); file << "0 0 0\n"; }
    const auto s = parse({{"script", name}});
    CHECK(s.agent_script);
    CHECK(s.arguments == (std::vector<std::string>{"--script", (directory / name).u8string()}));
    CHECK(s.log == "agent-test.json applied default keys: script");
  }
  for (const std::string name : {std::string(), std::string(".."), std::string("a..txt"), std::string("../out"),
       std::string("/tmp/out"), std::string("C:\\out"), std::string("a/b"), std::string("a\\b"),
       std::string("two words"), std::string(65, 'a'), std::string("a\0b", 3), std::string("caf\xc3\xa9")}) {
    const auto s = parse({{"script", name}, {"mute", true}});
    CHECK(!s.agent_script && s.arguments == (std::vector<std::string>{"--volume", "0"}));
    CHECK(s.log.find("agent-test: script invalid name; ignored") != std::string::npos);
  }
  for (const auto& value : {nlohmann::json(nullptr), nlohmann::json(1), nlohmann::json(true), nlohmann::json::array()}) {
    const auto s = parse({{"script", value}});
    CHECK(!s.agent_script && s.arguments.empty());
    CHECK(s.log == "agent-test: script invalid name; ignored");
  }
  CHECK(parse({{"script", "missing.txt"}}).log == "agent-test: script missing or not a regular file; ignored");
  std::filesystem::create_directories(directory / "folder");
  CHECK(!parse({{"script", "folder"}}).agent_script);
  { std::ofstream file(directory / "limit.txt"); file << std::string(65536, '#'); }
  CHECK(parse({{"script", "limit.txt"}}).agent_script);
  { std::ofstream file(directory / "limit.txt", std::ios::app); file << '#'; }
  CHECK(parse({{"script", "limit.txt"}}).log == "agent-test: script exceeds 64 KiB; ignored");
  // A name in this directory cannot follow a symlink to some other file.
  std::filesystem::remove(directory / "link.txt");
  std::filesystem::create_symlink(directory / "a", directory / "link.txt");
  CHECK(!parse({{"script", "link.txt"}}).agent_script);
  for (int every : {30, 600}) {
    const auto s = parse({{"captureEvery", every}});
    CHECK(s.arguments == (std::vector<std::string>{"--capture", (directory / "agent-captures" / "cap.ppm").u8string(),
                                                  "--capture-every", std::to_string(every)}));
    CHECK(std::filesystem::is_directory(directory / "agent-captures"));
    CHECK(s.log == "agent-test.json applied default keys: captureEvery");
  }
  for (int frames : {600, 36000}) {
    const auto s = parse({{"frames", frames}});
    CHECK(s.arguments == (std::vector<std::string>{"--frames", std::to_string(frames)}));
    CHECK(s.log == "agent-test.json applied default keys: frames");
  }
  for (const char* key : {"captureEvery", "frames"}) {
    const bool capture = std::string(key) == "captureEvery";
    for (const auto& value : {nlohmann::json(capture ? 29 : 599), nlohmann::json(capture ? 601 : 36001),
         nlohmann::json(-1), nlohmann::json(0), nlohmann::json(600.0), nlohmann::json("600"),
         nlohmann::json(true), nlohmann::json(nullptr), nlohmann::json(UINT64_MAX)}) {
      const auto s = parse({{key, value}});
      CHECK(s.arguments.empty());
      CHECK(s.log == std::string("agent test: ") + key + " invalid value; ignored");
    }
  }
  const auto source = parse({{"script", "a"}, {"captureEvery", 30}, {"frames", 600}}, Engine::Source);
  CHECK(source.arguments == (std::vector<std::string>{"--frames", "600", "--script", (directory / "a").u8string()}));
  CHECK(source.agent_script);
  CHECK(source.log.find("agent test: captureEvery not supported on the Source Port; ignored") != std::string::npos);
  std::filesystem::create_directories(directory / "fixtures");
  { std::ofstream file(directory / "fixtures" / "ab_vs_source.txt"); file << "0 START\n"; }
  for (const char* name : {"a", "fixtures/ab_vs_source.txt", "fixtures\\ab_vs_source.txt"}) {
    const auto s = parse({{"script", name}}, Engine::Source);
    const auto expected = std::string(name) == "a" ? directory / "a" : directory / "fixtures" / "ab_vs_source.txt";
    CHECK(s.agent_script && s.arguments == (std::vector<std::string>{"--script", expected.u8string()}));
    CHECK(s.log == "agent-test.json applied default keys: script");
  }
  for (const auto& value : {nlohmann::json("/tmp/out"), nlohmann::json("C:\\out"), nlohmann::json("C:out"),
       nlohmann::json("\\\\server\\out"), nlohmann::json("../out"), nlohmann::json("fixtures/../a"),
       nlohmann::json("fixtures\\..\\a"), nlohmann::json("a..txt"), nlohmann::json(""),
       nlohmann::json(std::string("a\0b", 3)), nlohmann::json(1), nlohmann::json(nullptr),
       nlohmann::json(true), nlohmann::json::array(), nlohmann::json::object()}) {
    const auto s = parse({{"script", value}}, Engine::Source);
    CHECK(s.arguments.empty() && !s.agent_script);
    CHECK(s.log == "agent-test: script invalid name; ignored");
  }
  for (const char* name : {"missing.txt", "folder", "link.txt"}) {
    const auto s = parse({{"script", name}}, Engine::Source);
    CHECK(s.arguments.empty() && !s.agent_script);
    CHECK(s.log == "agent-test: script missing or not a regular file; ignored");
  }
  std::filesystem::remove(directory / "linked-fixtures");
  std::filesystem::create_directory_symlink(directory / "fixtures", directory / "linked-fixtures");
  CHECK(!parse({{"script", "linked-fixtures/ab_vs_source.txt"}}, Engine::Source).agent_script);
  CHECK(parse({{"script", "limit.txt"}}, Engine::Source).log == "agent-test: script exceeds 64 KiB; ignored");
  CHECK(map_keys({{"script", "a"}}, Engine::Source).log == "agent-test: script missing data directory; ignored");
  // Both engines use the existing CLI fields: uint32 seed and uint64 time base, including zero.
  for (const auto engine : {Engine::Source, Engine::Static}) {
    for (const char* key : {"rng_seed", "time_base"}) {
      const bool seed = std::string(key) == "rng_seed";
      for (uint64_t number : {uint64_t(0), uint64_t(305419896), uint64_t(UINT32_MAX), seed ? uint64_t(UINT32_MAX) : UINT64_MAX}) {
        const auto s = parse({{key, number}}, engine);
        CHECK(s.arguments == (std::vector<std::string>{seed ? "--rng-seed" : "--time-base", std::to_string(number)}));
        CHECK(s.log == std::string("agent-test.json applied default keys: ") + key);
      }
      for (const auto& value : {nlohmann::json(-1), nlohmann::json(INT64_MIN), nlohmann::json(0.0),
           nlohmann::json(1e30), nlohmann::json("0"), nlohmann::json(true), nlohmann::json(nullptr),
           nlohmann::json::array(), nlohmann::json::object()}) {
        const auto s = parse({{key, value}}, engine);
        CHECK(s.arguments.empty());
        CHECK(s.log == std::string("agent-test: ") + key + " invalid value; ignored");
      }
    }
    for (uint64_t number : {uint64_t(UINT32_MAX) + 1, UINT64_MAX}) {
      const auto s = parse({{"rng_seed", number}}, engine);
      CHECK(s.arguments.empty() && s.log == "agent-test: rng_seed invalid value; ignored");
    }
  }
  const auto seeded = parse({{"mute", true}, {"script", "fixtures/ab_vs_source.txt"},
                             {"rng_seed", 305419896}, {"time_base", 0}}, Engine::Source);
  CHECK(seeded.arguments == (std::vector<std::string>{"--volume", "0", "--rng-seed", "305419896", "--script",
        (directory / "fixtures" / "ab_vs_source.txt").u8string(), "--time-base", "0"}));
  char source_exe[] = "melee_source", script_flag[] = "--script", cli_script[] = "cli.txt",
       seed_flag[] = "--rng-seed", cli_seed[] = "42", time_flag[] = "--time-base", cli_time[] = "123";
  char* source_args[] = {source_exe, script_flag, cli_script, seed_flag, cli_seed, time_flag, cli_time};
  auto expected_args = std::vector<std::string>{source_exe};
  expected_args.insert(expected_args.end(), seeded.arguments.begin(), seeded.arguments.end());
  expected_args.insert(expected_args.end(), {script_flag, cli_script, seed_flag, cli_seed, time_flag, cli_time});
  CHECK(seeded.prepend(7, source_args) == expected_args);
  const auto inactive_source = load(false, directory.u8string().c_str(), Engine::Source);
  CHECK(inactive_source.arguments.empty() && inactive_source.log.empty() && !inactive_source.agent_script);
  std::filesystem::remove(directory / "agent-test.json");
  const auto missing_source = load(true, directory.u8string().c_str(), Engine::Source);
  CHECK(missing_source.arguments.empty() && missing_source.log.empty() && !missing_source.agent_script);
  CHECK(!load(false, directory.u8string().c_str(), Engine::Static).agent_script);
  CHECK(map_keys({{"captureEvery", 30}}, Engine::Static).arguments.empty());
  // Existing CLI order is preserved: an explicit flag follows the defaults and wins.
  const auto combined = parse({{"script", "a"}, {"captureEvery", 60}, {"frames", 900}});
  char exe[] = "melee_port", frames[] = "--frames", count[] = "1200";
  char* args[] = {exe, frames, count};
  const auto prepended = combined.prepend(3, args);
  CHECK(combined.agent_script && prepended[prepended.size() - 2] == "--frames" && prepended.back() == "1200");
  CHECK(combined.log == "agent-test.json applied default keys: captureEvery, frames, script");
  const auto inactive = load(false, directory.u8string().c_str(), Engine::Static);
  CHECK(inactive.arguments.empty() && inactive.log.empty() && !inactive.agent_script);
  const auto blocked = directory / "capture-blocked";
  { std::ofstream file(blocked); file << "not a directory"; }
  const auto failed_capture = map_keys({{"captureEvery", 30}, {"frames", 600}}, Engine::Static, blocked);
  CHECK(failed_capture.arguments == (std::vector<std::string>{"--frames", "600"}));
  CHECK(failed_capture.log.find("agent test: captureEvery cannot create directory; ignored") != std::string::npos);
  source_perf_keys(directory);
  source_match_key(directory);
  ranked_input_policy();
  constexpr bool allowed[16] = {false, true, true, true, false, false, false, false,
                               false, true, true, true, true, false, true, false};
  for (int bits = 0; bits < 16; ++bits) {
    const bool local = bits & 1, server = bits & 2, agent = bits & 4, active = bits & 8;
    CHECK(matchmaking_allowed(local, server, agent, active, 1) == allowed[bits]);
  }
  CHECK(!matchmaking_allowed(false, false, false, true, 1));  // CLI script stays blocked.
  CHECK(matchmaking_allowed(false, false, true, true, 1));  // App script uses YouGame.
  CHECK(!matchmaking_allowed(false, true, true, false, 1)); // --allow-matchmaking cannot leak to Slippi.
  CHECK(input_owner(false, false) == InputOwner::Devices);
  CHECK(input_owner(true, false) == InputOwner::Script);
  CHECK(input_owner(false, true) == InputOwner::Fight);
  CHECK(input_owner(true, true) == InputOwner::Fight);
  // A prepared online sample replaces the local script port, leaving the others alone.
  MuPadStatus pads[4]{};
  for (auto& pad : pads) pad.button = 0x1000;
  CHECK(apply_fight(pads, FightSample{2, fight_input({1, 0, 0, 1, false, 0, 0, 50})}, InputState{0, -1}, [](const char*) {}));
  CHECK(pads[2].button == 0 && pads[2].stick_x == 80);
  CHECK(pads[0].button == 0x1000 && pads[1].button == 0x1000 && pads[3].button == 0x1000);
  CHECK(!apply_fight(pads, FightSample{}, InputState{0, -1}, [](const char*) {}));
  std::printf("yougame_agent_script: %u checks passed\n", checks);
}
#else
int main() { return 0; }
#endif
