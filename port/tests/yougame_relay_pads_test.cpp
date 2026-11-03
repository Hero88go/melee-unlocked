// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_link.h"
#include "yougame_relay_pads.h"
#ifdef _WIN32
#include <windows.h>
#include <xinput.h>
#else
#include "../linux/compat/xinput.h"
#endif
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using json = nlohmann::json;
using namespace yougame::relay_pads;
json pad(unsigned slot = 0) {
  return {{"slot", slot}, {"id", "dualsense"}, {"name", "DualSense Wireless Controller"}, {"kind", "ps"},
          {"b", 65535}, {"lx", -32768}, {"ly", 32767}, {"rx", -123}, {"ry", 456}, {"lt", 0}, {"rt", 255}};
}
json frame() { return {{"seq", 42}, {"t", 100}, {"pads", json::array({pad()})}}; }
std::string line(const json& j) { return "yg-pads " + j.dump(); }

void parser() {
  Frame parsed; std::string reason;
  const auto valid = frame();
  CHECK(parse(line(valid), parsed, reason));
  CHECK(parsed.seq == 42 && parsed.t == 100 && reason.empty());
  auto bad = [&](json j) {
    CHECK(!parse(line(j), parsed, reason));
    CHECK(!reason.empty());
    CHECK(parsed.seq == 42 && parsed.pads[0].present); // never partially overwrite output
  };
  bad(json::array()); bad(nullptr); bad(7);
  CHECK(!parse("other " + valid.dump(), parsed, reason));
  CHECK(!parse("yg-pads" + valid.dump(), parsed, reason));
  CHECK(!parse("yg-pads {", parsed, reason));
  CHECK(!parse(line(valid) + " garbage", parsed, reason));
  for (const char* field : {"seq", "t", "pads"}) { auto j = valid; j.erase(field); bad(j); }
  for (const char* field : {"seq", "t"}) {
    for (const json& value : {json(-1), json(true), json("1"), json(nullptr)}) {
      auto j = valid; j[field] = value; bad(j);
    }
  }
  { auto j = valid; j["seq"] = 1.5; bad(j); }
  { auto j = valid; j["t"] = 100.25; CHECK(parse(line(j), parsed, reason)); CHECK(parsed.t == 100.25); }
  { auto j = valid; j["seq"] = uint64_t(UINT32_MAX) + 1; bad(j); }
  { auto j = valid; j["pads"] = json::array({pad(), pad(), pad(), pad(), pad()}); bad(j); }
  { auto j = valid; j["pads"].push_back(pad()); bad(j); }
  for (const json& value : {json::object(), json(nullptr), json(1)}) { auto j = valid; j["pads"] = value; bad(j); }
  { auto j = valid; j["pads"] = json::array({1}); bad(j); }
  for (const char* field : {"slot", "id", "name", "kind", "b", "lx", "ly", "rx", "ry", "lt", "rt"}) {
    auto j = valid; j["pads"][0].erase(field); bad(j);
  }
  for (const char* field : {"slot", "b", "lx", "ly", "rx", "ry", "lt", "rt"}) {
    for (const json& value : {json(1.5), json(true), json("1"), json(nullptr), json(UINT64_MAX)}) {
      auto j = valid; j["pads"][0][field] = value; bad(j);
    }
    const int lo = ((field[0] == 'l' && field[1] != 't') || (field[0] == 'r' && field[1] != 't')) ? -32768 : 0;
    const int hi = std::string(field) == "slot" ? 3 : std::string(field) == "b" ? 65535 :
                   std::string(field) == "lt" || std::string(field) == "rt" ? 255 : 32767;
    for (int value : {lo - 1, hi + 1}) { auto j = valid; j["pads"][0][field] = value; bad(j); }
  }
  for (const char* field : {"id", "name", "kind"}) { auto j = valid; j["pads"][0][field] = 12; bad(j); }
  { auto j = valid; j["pads"][0]["id"] = ""; bad(j); }
  { auto j = valid; j["pads"][0]["id"] = "has space"; bad(j); }
  { auto j = valid; j["pads"][0]["id"] = std::string(33, 'a'); bad(j); }
  { auto j = valid; j["pads"][0]["id"] = std::string(32, 'a'); CHECK(parse(line(j), parsed, reason)); }
  { auto j = valid; j["pads"][0]["id"] = "line\nbreak"; bad(j); }
  { auto j = valid; j["pads"][0]["name"] = std::string(60, 'x'); auto l = line(j); l += std::string(2048, ' '); CHECK(!parse(l, parsed, reason)); }
  { auto j = valid; j["pads"][0]["name"] = std::string(65, 'x'); bad(j); }
  { auto j = valid; j["pads"][0]["kind"] = "PS"; bad(j); }
  for (const char* kind : {"ps", "xbox", "switch", "other"}) {
    auto j = valid; j["pads"][0]["kind"] = kind;
    CHECK(parse(line(j), parsed, reason));
  }
  auto j = valid; j["seq"] = UINT32_MAX; j["t"] = UINT64_MAX;
  j["pads"] = json::array();
  for (unsigned i = 0; i < 4; ++i) j["pads"].push_back(pad(i));
  // 64 UTF-8 code points, including non-ASCII controller names.
  std::string name; for (unsigned i = 0; i < 64; ++i) name += "é";
  j["pads"][0]["name"] = name;
  CHECK(parse(line(j), parsed, reason));
  CHECK(parsed.seq == UINT32_MAX && parsed.t == UINT64_MAX && parsed.pads[3].present);
  j["pads"] = json::array();
  CHECK(parse(line(j), parsed, reason));
  for (const auto& p : parsed.pads) CHECK(!p.present);
}

void state() {
  std::vector<std::string> logs;
  State state([&](const std::string& s) { logs.push_back(s); });
  XINPUT_STATE xs{};
  CHECK(!state.active()); CHECK(!state.read(0, &xs));
  int native_reads = 0, native_rumbles = 0;
  CHECK(state.poll(0, &xs, [&] { ++native_reads; return true; }));
  state.rumble([&] { ++native_rumbles; });
  CHECK(native_reads == 1 && native_rumbles == 1);
  CHECK(!state.accept("yg-pads []", 900)); CHECK(!state.active());
  CHECK(!state.accept("yg-pads null", 901)); CHECK(logs.size() == 1);
  CHECK(state.accept(line(frame()), 1000)); CHECK(state.active());
  CHECK(state.read(0, &xs));
  CHECK(xs.dwPacketNumber == 42 && xs.Gamepad.wButtons == 65535);
  CHECK(xs.Gamepad.sThumbLX == -32768 && xs.Gamepad.sThumbLY == 32767);
  CHECK(xs.Gamepad.sThumbRX == -123 && xs.Gamepad.sThumbRY == 456);
  CHECK(xs.Gamepad.bLeftTrigger == 0 && xs.Gamepad.bRightTrigger == 255);
  CHECK(!state.read(1, &xs)); CHECK(xs.dwPacketNumber == 0 && xs.Gamepad.wButtons == 0);
  CHECK(!state.read(4, &xs)); CHECK(!state.read(0, static_cast<XINPUT_STATE*>(nullptr)));
  auto j = frame(); j["seq"] = 41; j["pads"][0]["b"] = XINPUT_GAMEPAD_A;
  CHECK(state.accept(line(j), 1001));
  CHECK(state.poll(0, &xs, [&] { ++native_reads; return false; }));
  CHECK(xs.dwPacketNumber == 41 && xs.Gamepad.wButtons == XINPUT_GAMEPAD_A);
  j["pads"].push_back(pad());
  CHECK(!state.accept(line(j), 1002)); CHECK(state.read(0, &xs)); CHECK(xs.dwPacketNumber == 41);
  j["pads"] = json::array({pad(3)}); j["seq"] = 0;
  CHECK(state.accept(line(j), 1003)); CHECK(!state.read(0, &xs)); CHECK(state.read(3, &xs));
  CHECK(xs.dwPacketNumber == 0);
  j["pads"][0]["id"] = "second";
  CHECK(state.accept(line(j), 1004));
  j["pads"] = json::array();
  CHECK(state.accept(line(j), 1005)); CHECK(state.active());
  CHECK(!state.poll(3, &xs, [&] { ++native_reads; return true; }));
  state.rumble([&] { ++native_rumbles; }); state.rumble([&] { ++native_rumbles; });
  CHECK(native_reads == 1 && native_rumbles == 1);
  state.flush();
  CHECK(logs == std::vector<std::string>({
    "pads: rejected relay line: expected object",
    "pads: relay mode (app): non-adapter pads come from the YouGame app",
    "pads: relay connect slot 0 dualsense ps",
    "pads: relay disconnect slot 0 dualsense",
    "pads: relay connect slot 3 dualsense ps",
    "pads: relay disconnect slot 3 dualsense",
    "pads: relay connect slot 3 second ps",
    "pads: relay disconnect slot 3 second",
    "pads: relayed pads have no rumble yet",
    "pads: relay lag p50 3 ms p95 5 ms over 5 lines"}));
  state.flush(); CHECK(logs.size() == 10);
  // The pad's name never reaches the log: only the app's opaque id and the kind (pack-platform).
  for (const auto& l : logs) CHECK(l.find("DualSense Wireless Controller") == std::string::npos);
  State empty([](const std::string&) {});
  CHECK(empty.accept(line(j), 1)); CHECK(empty.active()); // initial empty snapshot latches too
  CHECK(!empty.accept("yg-pads false", 2)); CHECK(empty.active());
}

void eof_reset() {
  std::vector<std::string> logs;
  State state([&](const std::string& s) { logs.push_back(s); });
  state.reset_on_eof();
  CHECK(!state.active()); CHECK(logs.empty());
  auto j = frame(); j["pads"] = json::array();
  for (unsigned slot = 0; slot < 4; ++slot) j["pads"].push_back(pad(slot));
  CHECK(state.accept(line(j), 1000));
  XINPUT_STATE xs{};
  for (unsigned slot = 0; slot < 4; ++slot) CHECK(state.read(slot, &xs));
  logs.clear();
  state.reset_on_eof();
  CHECK(state.active());
  for (unsigned slot = 0; slot < 4; ++slot) {
    xs.dwPacketNumber = 42; xs.Gamepad.wButtons = 65535;
    CHECK(!state.read(slot, &xs));
    CHECK(xs.dwPacketNumber == 0 && xs.Gamepad.wButtons == 0 &&
          xs.Gamepad.sThumbLX == 0 && xs.Gamepad.sThumbLY == 0 &&
          xs.Gamepad.sThumbRX == 0 && xs.Gamepad.sThumbRY == 0 &&
          xs.Gamepad.bLeftTrigger == 0 && xs.Gamepad.bRightTrigger == 0);
    CHECK(!state.poll(slot, &xs, [] { CHECK(false); return true; }));
  }
  state.rumble([] { CHECK(false); });
  CHECK(logs == std::vector<std::string>({
    "pads: relay disconnect slot 0 dualsense",
    "pads: relay disconnect slot 1 dualsense",
    "pads: relay disconnect slot 2 dualsense",
    "pads: relay disconnect slot 3 dualsense",
    "pads: relay ended (stdin closed): all relayed pads released",
    "pads: relayed pads have no rumble yet"}));
  state.reset_on_eof(); CHECK(logs.size() == 6); CHECK(state.active());

  logs.clear();
  State empty([&](const std::string& s) { logs.push_back(s); }, false);
  j["pads"] = json::array();
  CHECK(empty.accept(line(j), 1000));
  empty.reset_on_eof(); empty.reset_on_eof();
  CHECK(empty.active()); CHECK(!empty.read(0, &xs)); CHECK(logs.empty());
  empty.logging_ready();
  CHECK(logs == std::vector<std::string>({
    "pads: relay mode (app): non-adapter pads come from the YouGame app",
    "pads: relay ended (stdin closed): all relayed pads released"}));
}

void lag() {
  std::vector<std::string> logs;
  State state([&](const std::string& s) { logs.push_back(s); });
  auto j = frame(); j["pads"] = json::array();
  for (unsigned i = 0; i < 601; ++i) {
    j["seq"] = i; j["t"] = 100 + i * 10;
    CHECK(state.accept(line(j), 1000 + i * 10 + i));
  }
  CHECK(logs.size() == 2);
  CHECK(logs[1] == "pads: relay lag p50 299 ms p95 569 ms over 600 lines");
  state.flush();
  CHECK(logs.back() == "pads: relay lag p50 600 ms p95 600 ms over 1 lines");
}

void startup_logging() {
  std::vector<std::string> logs;
  State state([&](const std::string& s) { logs.push_back(s); }, false);
  CHECK(state.accept(line(frame()), 10)); CHECK(state.active()); CHECK(logs.empty());
  XINPUT_STATE xs{}; CHECK(state.read(0, &xs)); CHECK(xs.dwPacketNumber == 42);
  state.logging_ready(); CHECK(logs.size() == 2);
  state.logging_ready(); CHECK(logs.size() == 2);
  CHECK(state.accept(line(frame()), 11)); CHECK(logs.size() == 2);
  state.flush(); CHECK(logs.size() == 3);
}

void concurrent_activation() {
  State state([](const std::string&) {});
  std::mutex mutex; std::condition_variable cv;
  bool native_entered = false, release = false;
  XINPUT_STATE xs{};
  std::thread native([&] { state.poll(0, &xs, [&] {
    std::unique_lock<std::mutex> lock(mutex); native_entered = true; cv.notify_all();
    cv.wait(lock, [&] { return release; }); return true;
  }); });
  { std::unique_lock<std::mutex> lock(mutex); cv.wait(lock, [&] { return native_entered; }); }
  CHECK(!state.active()); // a slow native worker cannot hold the game thread's state lock
  XINPUT_STATE other{};
  CHECK(state.poll(1, &other, [] { return true; })); // native calls can run concurrently
  std::thread app([&] { state.accept(line(frame()), 1000); });
  { std::lock_guard<std::mutex> lock(mutex); release = true; } cv.notify_all();
  native.join(); app.join();
  bool called = false;
  CHECK(state.poll(0, &xs, [&] { called = true; return false; }));
  state.rumble([&] { called = true; });
  CHECK(!called && xs.dwPacketNumber == 42);
}
}
int main() {
  parser(); state(); eof_reset(); lag(); startup_logging(); concurrent_activation();
  std::printf("yougame relay pads: %d/%d checks passed\n", checks, checks);
}
