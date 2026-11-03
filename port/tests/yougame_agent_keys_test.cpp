// Window-free key-script parsing, presentation epoch, and shared overlay ownership.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_agent_keys.h"
#include "yougame_parity.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_test.h"

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame::agent_test;
using namespace yougame::parity;
using namespace std::chrono_literals;
static std::vector<std::string> lines;
static void write_line(const char* line) { lines.emplace_back(line); }

static std::vector<ScriptedKey> parse(const std::string& script) {
  std::string why;
  const auto keys = parse_key_script(script, why);
  CHECK(why.empty());
  return keys;
}

static void parsing(const std::filesystem::path& directory) {
  const auto keys = parse("overlay@20,escape@30,overlay@45");
  CHECK(keys.size() == 3 && keys[0].overlay && !keys[1].overlay && keys[2].overlay);
  CHECK(keys[0].seconds == 20 && keys[1].seconds == 30 && keys[2].seconds == 45);
  const auto sorted = parse("overlay@600,escape@0.01,overlay@20,escape@20");
  CHECK(sorted[0].seconds == 0.01 && sorted[3].seconds == 600);
  CHECK(sorted[1].overlay && !sorted[2].overlay);  // Stable ties.
  std::string sixteen;
  for (int i = 0; i < 16; ++i) sixteen += (i ? "," : "") + std::string("overlay@1");
  CHECK(parse(sixteen).size() == 16);
  for (const auto& script : {std::string(), sixteen + ",escape@1", std::string("menu@20"),
       std::string("Overlay@20"), std::string("overlay"), std::string("overlay@1@2"),
       std::string("overlay@1,"), std::string(",overlay@1"), std::string("overlay@1,,escape@2"),
       std::string("overlay@0"), std::string("overlay@-1"), std::string("overlay@601"),
       std::string("overlay@600.0000000000000000001"), std::string("overlay@nan"),
       std::string("overlay@inf"), std::string("overlay@1e2"), std::string("overlay@+1"),
       std::string("overlay@1s"), std::string("overlay@ 1"), std::string("overlay@1 "),
       std::string("overlay@.5"), std::string("overlay@1."), std::string("overlay@"),
       std::string("overlay@99999999999999999999"), std::string("overlay@1\0", 10),
       std::string("overlay@1\nescape@2"), std::string(4097, 'x')}) {
    // A valid prefix never survives a bad suffix.
    const auto settings = map_keys({{"keys", "escape@1," + script}});
    CHECK(settings.key_script.empty());
    CHECK(settings.log.find("agent-test: keys ignored: ") == 0);
    CHECK(settings.log.find('\n') == std::string::npos);
  }
  for (const auto& value : {nlohmann::json(nullptr), nlohmann::json(true), nlohmann::json(20),
                           nlohmann::json::array(), nlohmann::json::object()}) {
    const auto settings = map_keys({{"keys", value}});
    CHECK(settings.key_script.empty());
    CHECK(settings.log == "agent-test: keys ignored: expected a string");
  }
  CHECK(map_keys({{"keys", ""}}).log == "agent-test: keys ignored: empty script");
  const auto mixed = map_keys({{"keys", "bad@1"}, {"mute", true}});
  CHECK(mixed.arguments == (std::vector<std::string>{"--volume", "0"}));
  CHECK(mixed.log == "agent-test.json applied default keys: mute\nagent-test: keys ignored: unknown key name");
  std::filesystem::create_directories(directory);
  { std::ofstream file(directory / "agent-test.json");
    file << R"({"mute":true,"keys":"overlay@20,escape@30,overlay@45"})";
    CHECK(file.good()); }
  for (const auto engine : {Engine::Source, Engine::Static}) {
    const auto settings = load(true, directory.u8string().c_str(), engine);
    CHECK(settings.key_script.size() == 3);
    CHECK(settings.log == "agent-test.json applied default keys: keys, mute");
    const auto standalone = load(false, directory.u8string().c_str(), engine);
    CHECK(standalone.key_script.empty() && standalone.log.empty());
  }
}

struct Fixture {
  Signals signals;
  OverlayKey overlay;
  KeyScript script;
  std::vector<std::string> logs;
  const KeyScript::Clock::time_point epoch{1000s};
  Fixture(const std::string& keys, bool stdio = true) {
    lines.clear();
    signals.start(stdio, write_line);
    script.arm(stdio, parse(keys));
  }
  void pump(KeyScript::Clock::time_point now, bool stdio = true) {
    script.pump(stdio, now, signals, overlay,
      [] { write_line("yg {\"key\":\"escape\"}"); },
      [&](const std::string& line) { logs.push_back(line); });
  }
  void real(bool down, bool shift = true, bool repeat = false) {
    signals.emit(overlay.tab(true, down, shift, repeat).line);
  }
};

static void schedule() {
  Fixture f("overlay@20,escape@30,overlay@45");
  f.pump(f.epoch + 700s);  // Process age cannot trigger the script.
  f.signals.presented(false, f.epoch - 100s);  // Loading frame.
  f.pump(f.epoch + 700s);
  CHECK(lines == (std::vector<std::string>{kCaps}) && f.logs.empty());
  f.signals.presented(true, f.epoch);
  f.signals.presented(true, f.epoch + 15s);  // Later frames cannot reset the epoch.
  f.pump(f.epoch - 1s);
  f.pump(f.epoch + 20s - 1ms);
  CHECK(lines == (std::vector<std::string>{kCaps, kReady}));
  f.pump(f.epoch + 20s);
  CHECK(lines.back() == kDown && lines.size() == 3);
  CHECK(f.logs == (std::vector<std::string>{"agent-test: key overlay at 20 s"}));
  f.pump(f.epoch + 20s + 99ms);
  CHECK(lines.size() == 3);
  f.pump(f.epoch + 20s + 100ms);
  CHECK(lines.back() == kUp && lines.size() == 4 && f.logs.size() == 1);
  f.pump(f.epoch + 30s);
  CHECK(lines.back() == "yg {\"key\":\"escape\"}" && lines.size() == 5);
  f.pump(f.epoch + 45s);
  f.pump(f.epoch + 45s + 100ms);
  f.pump(f.epoch + 900s);
  CHECK(lines == (std::vector<std::string>{kCaps, kReady, kDown, kUp,
      "yg {\"key\":\"escape\"}", kDown, kUp}));
  CHECK(f.logs == (std::vector<std::string>{"agent-test: key overlay at 20 s",
      "agent-test: key escape at 30 s", "agent-test: key overlay at 45 s"}));
}

static void holds() {
  Fixture f("overlay@1,escape@2,overlay@3,overlay@4");
  f.signals.presented(true, f.epoch);
  f.real(true);
  f.pump(f.epoch + 1s);
  CHECK(lines.size() == 3 && lines.back() == kDown);
  CHECK(f.logs.back() == "agent-test: key overlay at 1 s skipped: real Shift+Tab held");
  f.pump(f.epoch + 1100ms);
  CHECK(lines.size() == 3);  // Script must not release the real hold.
  f.pump(f.epoch + 2s);
  CHECK(lines.back() == "yg {\"key\":\"escape\"}");
  f.signals.emit(f.overlay.cancel());
  f.pump(f.epoch + 3s);  // Cancelled real hold remains reserved until release.
  CHECK(lines.back() == kCancel && f.logs.back().find("skipped") != std::string::npos);
  f.real(false, false);
  f.pump(f.epoch + 4s);
  CHECK(lines.back() == kDown);
  f.pump(f.epoch + 4100ms);
  CHECK(lines.back() == kUp && f.logs.size() == 4);

  Fixture overlap("overlay@1,overlay@1.05,escape@1.06");
  overlap.signals.presented(true, overlap.epoch);
  overlap.pump(overlap.epoch + 1s);
  overlap.real(true);
  overlap.real(true, true, true);
  overlap.real(false);
  CHECK(lines.size() == 3);  // Real edges cannot shorten the tap or duplicate down.
  overlap.pump(overlap.epoch + 1060ms);
  CHECK(lines.back() == "yg {\"key\":\"escape\"}");
  overlap.pump(overlap.epoch + 1100ms);
  CHECK(lines == (std::vector<std::string>{kCaps, kReady, kDown,
      "yg {\"key\":\"escape\"}", kUp, kDown}));
  overlap.real(true);
  overlap.pump(overlap.epoch + 1200ms);
  CHECK(lines.back() == kUp);  // Up still arrives after 100 ms with a new real hold.
  const auto count = lines.size();
  overlap.real(false);
  CHECK(lines.size() == count);  // Its release cannot emit a duplicate up.
  CHECK(overlap.logs.size() == 3);

  Fixture cancelled("overlay@1");
  cancelled.signals.presented(true, cancelled.epoch);
  cancelled.pump(cancelled.epoch + 1s);
  cancelled.signals.emit(cancelled.overlay.cancel());
  cancelled.real(true);
  cancelled.pump(cancelled.epoch + 1100ms);
  CHECK(lines == (std::vector<std::string>{kCaps, kReady, kDown, kCancel, kDown}));
  cancelled.real(false);
  CHECK(lines.back() == kUp);  // Stale script release cannot release a new real press.
}

static void gating_and_late_pump() {
  Fixture standalone("overlay@1,escape@2", false);
  standalone.signals.presented(true, standalone.epoch);
  standalone.pump(standalone.epoch + 10s, false);
  standalone.pump(standalone.epoch + 10s);
  CHECK(lines.empty() && standalone.logs.empty());
  Fixture f("overlay@1,escape@2");
  f.signals.presented(true, f.epoch);
  f.pump(f.epoch + 5s, false);
  CHECK(lines.size() == 2 && f.logs.empty());
  f.pump(f.epoch + 5s);  // A stalled pump still gives a full 100 ms tap.
  CHECK(lines == (std::vector<std::string>{kCaps, kReady, kDown, "yg {\"key\":\"escape\"}"}));
  f.pump(f.epoch + 5099ms);
  CHECK(lines.size() == 4);
  f.pump(f.epoch + 5100ms);
  CHECK(lines.back() == kUp);
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  parsing(std::filesystem::u8path(argv[1]));
  schedule();
  holds();
  gating_and_late_pump();
  std::printf("yougame_agent_keys: %u checks passed\n", checks);
}
#else
// Include the hook header in an off build, alongside the normal parity path. The
// production scanner must find no script diagnostics or hook implementation.
int main() {
  yougame::parity::OverlayKey key;
  return key.tab(false, true, true, false).swallowed ? 1 : 0;
}
#endif
