// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_ux.h"
#include "yougame_parity.h"
#include "settings_chord.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

namespace {
std::vector<std::string> lines;
void write_line(const char* line) { lines.emplace_back(line); }
void pause_cases() {
  using nlohmann::json;
  using Outcome = yougame::ux::PagePause::Outcome;
  yougame::ux::PagePause pause;
  bool frozen = false;
  std::vector<bool> writes;
  auto set = [&](bool value) { frozen = value; writes.push_back(value); };
  auto tick = [&](bool stdio = true, bool online = false, bool match = true,
                  uint32_t scene = 0x202, uint32_t frame = 20) {
    return pause.tick(stdio, online, match, scene, frame, frozen, set);
  };
  auto check = [](yougame::ux::PagePause::Result result, Outcome expected,
                  bool released_scene = false) {
    CHECK(result.outcome == expected);
    CHECK(result.released_scene == released_scene);
  };
  auto push = [&](bool value) { CHECK(pause.push(json{{"type", "pause"}, {"paused", value}})); };
  check(tick(), Outcome::None);
  // Same JSON object handed to the push branch by handle_app_line.
  std::thread stdin_reader([&] { CHECK(pause.push(json::parse(
      R"({"push":{"type":"pause","paused":true}})")["push"])); });
  stdin_reader.join();
  CHECK(writes.empty());  // Stdin cannot touch simulation state.
  check(tick(), Outcome::Applied); CHECK(frozen && writes == std::vector<bool>{true});
  check(tick(), Outcome::None); CHECK(writes.size() == 1);
  push(true); check(tick(), Outcome::IgnoredAlreadyPaused); CHECK(writes.size() == 1);
  for (const char* text : {"null", "[]", "true", "{}", R"({"type":"pause"})",
       R"({"type":"pause","paused":null})", R"({"type":"pause","paused":1})",
       R"({"type":"pause","paused":0})", R"({"type":"pause","paused":"false"})",
       R"({"type":"pause","paused":[]})", R"({"type":"pause","paused":{}})",
       R"({"type":1,"paused":true})", R"({"type":"notice","paused":false})"}) {
    CHECK(!pause.push(json::parse(text))); check(tick(), Outcome::None);
    CHECK(frozen && writes.size() == 1);
  }
  push(false); check(tick(), Outcome::Cleared);
  CHECK(!frozen && writes == (std::vector<bool>{true, false}));
  push(false); check(tick(), Outcome::IgnoredUnownedResume); CHECK(writes.size() == 2);
  // Both request values are consumed in standalone/online/non-match states, never deferred.
  for (bool desired : {true, false}) {
    push(desired); check(tick(false), Outcome::IgnoredNotInMatch);
    check(tick(), Outcome::None);
    push(desired); check(tick(true, true), Outcome::IgnoredOnline);
    check(tick(true, true), Outcome::None);
    push(desired); check(tick(true, true, false), Outcome::IgnoredOnline);
    check(tick(), Outcome::None);
    push(desired); check(tick(true, false, false), Outcome::IgnoredNotInMatch);
    check(tick(true, false, false), Outcome::None);
  }
  CHECK(!frozen && writes.size() == 2);
  // Preserve a preexisting engine pause on page resume.
  frozen = true;
  push(true); check(tick(), Outcome::IgnoredAlreadyPaused);
  push(false); check(tick(), Outcome::IgnoredUnownedResume);
  CHECK(frozen && writes.size() == 2);
  frozen = false;
  push(true); push(false); check(tick(), Outcome::IgnoredUnownedResume);
  CHECK(!frozen && writes.size() == 2);  // Latest desired state wins before retrace.
  push(true); check(tick(), Outcome::Applied); CHECK(frozen);
  // A new scene owns its flags; stale resume must not clear them. Both events are reported.
  push(false); check(tick(true, false, true, 0x1C02), Outcome::IgnoredUnownedResume, true);
  CHECK(frozen && writes.size() == 3);
  check(tick(true, false, true, 0x1C02), Outcome::None);  // Release is reported only once.
  frozen = false; push(true);
  check(tick(true, false, true, 0x1C02, 30), Outcome::Applied); CHECK(frozen);
  push(false); check(tick(true, false, true, 0x1C02, 0), Outcome::IgnoredUnownedResume, true);
  CHECK(frozen && writes.size() == 4);
  frozen = false; push(true); check(tick(), Outcome::Applied); CHECK(frozen);
  push(false); check(tick(true, true), Outcome::IgnoredOnline);
  CHECK(frozen && writes.size() == 5);
  push(false); check(tick(), Outcome::IgnoredUnownedResume); CHECK(frozen);

  // Scene/frame changes without a request report a release once, without a setter call.
  for (bool scene_change : {true, false}) {
    frozen = false; push(true); check(tick(), Outcome::Applied);
    const auto count = writes.size();
    const uint32_t scene = scene_change ? 0x1C02 : 0x202;
    const uint32_t frame = scene_change ? 21 : 0;
    check(tick(true, false, true, scene, frame), Outcome::None, true);
    check(tick(true, false, true, scene, frame + 1), Outcome::None);
    push(false);
    check(tick(true, false, true, scene, frame + 2), Outcome::IgnoredUnownedResume);
    CHECK(frozen && writes.size() == count);
  }
  // A scene transition may also consume a fresh pause and acquire ownership again.
  frozen = false; push(true); check(tick(), Outcome::Applied);
  frozen = false; push(true);
  check(tick(true, false, true, 0x1C02), Outcome::Applied, true);
  push(false); check(tick(true, false, true, 0x1C02), Outcome::Cleared);
  CHECK(!frozen);
  // Non-match and standalone transitions still discard ownership without changing flags.
  for (bool stdio : {true, false}) {
    push(true); check(tick(), Outcome::Applied);
    const auto count = writes.size();
    check(tick(stdio, false, false), Outcome::None);
    push(false); check(tick(), Outcome::IgnoredUnownedResume);
    CHECK(frozen && writes.size() == count);
    frozen = false;
  }
}

// What the page hears (yg {"event":{"type":"pause",...}}): every change, and an answer to every request.
void pause_event_cases() {
  using nlohmann::json;
  using Result = yougame::ux::PagePause::Result;
  yougame::ux::PagePause pause;
  bool frozen = false;
  auto set = [&](bool value) { frozen = value; };
  auto tick = [&](bool online = false, bool match = true, uint32_t scene = 0x202, uint32_t frame = 20,
                  bool stdio = true) {
    return pause.tick(stdio, online, match, scene, frame, frozen, set);
  };
  auto event = [](const Result& r, bool pausable, bool paused, const char* reason) {
    CHECK(r.event && r.pausable == pausable && r.paused == paused);
    CHECK(reason ? (r.reason && std::string(r.reason) == reason) : r.reason == nullptr);
  };
  auto push = [&](bool value) { CHECK(pause.push(json{{"type", "pause"}, {"paused", value}})); };
  // The first tick tells the page where it stands; then nothing until something changes.
  event(tick(false, false, 0x101), false, false, "not-in-match");   // a menu
  CHECK(!tick(false, false, 0x101).event);
  push(true); event(tick(false, false, 0x101), false, false, "not-in-match");   // answered, not applied
  CHECK(!frozen);
  event(tick(), true, false, nullptr);   // a local match: pausable
  CHECK(!tick().event);
  push(true); event(tick(), true, true, nullptr); CHECK(frozen);     // applied
  push(true); event(tick(), true, true, nullptr);                    // already paused: still answered
  push(false); event(tick(), true, false, nullptr); CHECK(!frozen);  // cleared
  push(false); event(tick(), true, false, nullptr);                  // a resume nobody asked for: answered
  push(true); CHECK(tick().event && frozen);
  event(tick(false, true, 0x1C02), true, false, "scene-changed");    // a new scene drops the page's pause
  CHECK(!tick(false, true, 0x1C02).event);
  event(tick(true, true, 0x1C02), false, false, "online");           // online: never pausable
  push(true); event(tick(true, true, 0x1C02), false, false, "online");
  CHECK(!tick(true, true, 0x1C02).event);
  // A Start pause is the player's own: the page's request is answered paused:false.
  frozen = true; event(tick(false, true, 0x1C02), true, false, nullptr);
  push(true); event(tick(false, true, 0x1C02), true, false, nullptr);
  frozen = false;
  // Without the stdio bridge there is nobody to tell.
  push(true); CHECK(!tick(false, true, 0x1C02, 20, false).event);
}

void pad_cases() {
  using namespace yougame::parity;
  Signals signals;
  signals.start(true, write_line);
  lines.clear();
  OverlayPad pad;
  bool legacy[4]{};
  auto sample = [&](uint16_t raw, uint64_t time, bool stdio = true, int port = 0, bool connected = true) {
    signals.emit(pad.tick(time));
    uint16_t gameplay = raw;
    host::settings_shortcut::poll_z_start(connected, gameplay, legacy[port]);
    signals.emit(pad.poll(stdio, port, connected, raw, time, legacy[port]));
    if (!(raw & 0x1000)) gameplay &= static_cast<uint16_t>(~0x1000);
    return gameplay;
  };
  CHECK(sample(0x1000, 0) == 0x1000); CHECK(lines.empty());  // Start alone.
  CHECK(sample(0x1004, 1) == 0x1004); CHECK(lines.empty());  // Down added after Start.
  sample(0, 2);
  CHECK(sample(0x0004, 3) == 0x0004);
  CHECK(sample(0x1104, 4) == 0x0104); // Down held, Start edge; preserve A and Down.
  CHECK(lines == std::vector<std::string>{kDown});
  CHECK(sample(0x1104, 50) == 0x0104); CHECK(lines.size() == 1); // Repeats.
  CHECK(sample(0x1100, 103) == 0x0100); CHECK(lines.size() == 1); // Down released first.
  CHECK(sample(0x1100, 104) == 0x0100);
  CHECK(lines == (std::vector<std::string>{kDown, kUp}));
  sample(0x1004, 200); CHECK(lines.size() == 2);  // Held after expiry still cannot repeat.
  sample(0, 201); CHECK(sample(0x1000, 202) == 0x1000); // Plain Start restored after release.
  sample(0, 203);
  sample(0x1004, 204); sample(0, 205); sample(0x1004, 206); // Rapid tap coalesces.
  sample(0x1004, 304); CHECK(lines == (std::vector<std::string>{kDown, kUp, kDown, kUp}));
  sample(0, 305); sample(0x1004, 306, true, 3); sample(0, 406, true, 3);
  CHECK(lines.size() == 6 && lines[4] == kDown && lines[5] == kUp); // Every port.
  sample(0, 407); CHECK(sample(0x1114, 408) == 0x0100); // Legacy 3-button settings still wins.
  CHECK(lines.size() == 6);
  sample(0, 409); CHECK(sample(0x1104, 410, false) == 0x1104); CHECK(lines.size() == 6);
  sample(0, 411); sample(0x1004, 412, true, 0, false); CHECK(lines.size() == 6);
  sample(0x1004, 413); CHECK(lines.size() == 7);
  sample(0, 414, true, 0, false); sample(0, 513); CHECK(lines.size() == 8 && lines.back() == kUp);
}
} // namespace
int main() {
  pause_cases(); pause_event_cases(); pad_cases();
  CHECK(std::string(yougame::parity::kMenuHint) == "Escape / YouGame menu (D-pad + Start)");
  std::puts("YouGame page pause and controller menu passed");
}
