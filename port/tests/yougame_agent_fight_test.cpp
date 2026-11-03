// Window-free policy, native-state gating and startup parsing checks.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_agent_fight.h"
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_test.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <tuple>

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame::agent_test;

static auto fields(const MuPadStatus& p) {
  return std::make_tuple(p.button, p.stick_x, p.stick_y, p.sub_x, p.sub_y,
                        p.trigger_l, p.trigger_r, p.analog_a, p.analog_b, p.err);
}
static uint32_t bits(float f) {
  uint32_t value;
  std::memcpy(&value, &f, sizeof value);
  return value;
}

static void policy() {
  FightState s{};
  s.opponent_x = 80;
  CHECK(fight_input(s).stick_x == 80);
  CHECK(fight_input(s).button == 0);
  s.self_x = 80;
  s.opponent_x = 0;
  CHECK(fight_input(s).stick_x == -80);
  s.self_x = 0;
  s.opponent_x = 10;
  s.facing = -1;
  CHECK(fight_input(s).stick_x == 40 && fight_input(s).button == 0);
  s.opponent_x = -10;
  s.facing = 1;
  CHECK(fight_input(s).stick_x == -40 && fight_input(s).button == 0);
  s.opponent_x = 10;
  for (uint32_t base : {0u, 240u, 480u}) {
    s.frame = base;
    CHECK(fight_input(s).button == 0x0100 && fight_input(s).stick_x == 0); // jab
    s.frame = base + 2;
    CHECK(fight_input(s).button == 0);
    s.frame = base + 54;
    CHECK(fight_input(s).button == 0x0100 && fight_input(s).stick_x == 40); // tilt
    s.frame = base + 56;
    CHECK(fight_input(s).button == 0);
    s.frame = base + 101;
    CHECK(fight_input(s).button == 0 && fight_input(s).stick_y == 0);
    for (uint32_t f = 102; f < 140; ++f) {
      s.frame = base + f;
      const auto pad = fight_input(s);
      CHECK(pad.button == 0x0100); // charge lasts 38 frames, with a 2-frame up flick
      CHECK(pad.stick_y == (f < 104 ? 80 : 0));
    }
    s.frame = base + 140;
    CHECK(fight_input(s).button == 0 && fight_input(s).stick_y == 0);
    s.frame = base + 174;
    CHECK(fight_input(s).button == 0x0100 && fight_input(s).stick_y == -80); // down smash
    s.frame = base + 176;
    CHECK(fight_input(s).button == 0 && fight_input(s).stick_y == 0);
  }
  s.frame = 54;
  s.opponent_x = -10;
  s.facing = -1;
  CHECK(fight_input(s).button == 0x0100 && fight_input(s).stick_x == -40);
  s.airborne = true;
  s.self_x = 90;
  s.frame = 0;
  CHECK(fight_input(s).stick_x == -60 && fight_input(s).stick_y == 80 && fight_input(s).button == 0x0200);
  s.self_x = -90;
  CHECK(fight_input(s).stick_x == 60 && fight_input(s).button == 0x0200);
  s.self_x = 0;
  s.self_y = -6;
  CHECK(fight_input(s).stick_x == 0 && fight_input(s).stick_y == 80 && fight_input(s).button == 0x0200);
  s.frame = 2;
  CHECK(fight_input(s).button == 0 && fight_input(s).stick_y == 80);
  s.frame = 12;
  CHECK(fight_input(s).button == 0x0200); // a new special edge
  s.airborne = false;
  CHECK(fight_input(s).button != 0x0200); // grounded outside the edge is not recovery
  s.airborne = true;
  s.self_y = 0;
  CHECK(fight_input(s).button == 0x0100 && fight_input(s).stick_x == -40); // onstage aerial
  s.stage_edge = fight_stage_edge(8);
  s.self_x = 60;
  CHECK(fight_input(s).button == 0x0200); // Yoshi's smaller edge
  s.stage_edge = fight_stage_edge(32);
  s.opponent_x = 65;
  CHECK(fight_input(s).button == 0x0100); // same position over FD is onstage

  // The whole 180-second policy horizon, mirrored ground/air/recovery cases. Repeat the call
  // after an unrelated state so accidental mutable counters cannot pass the equality check.
  for (uint32_t f = 0; f < 10800; ++f) {
    for (float x : {-95.0f, -10.0f, 10.0f, 95.0f}) {
      for (bool air : {false, true}) {
        s = {f, x, -6, x < 0 ? 1.0f : -1.0f, air, 14, 120, -x, 0, 80};
        const auto pad = fight_input(s);
        (void)fight_input(FightState{});
        CHECK(fields(pad) == fields(fight_input(s)));
        CHECK((pad.button & ~0x0300) == 0); // no Start, D-pad, shoulders or menu chord
        CHECK(pad.sub_x == 0 && pad.sub_y == 0 && pad.trigger_l == 0 && pad.trigger_r == 0 && pad.err == 0);
      }
    }
  }
}

static void injection() {
  FightContext c{true, true, true, true, false, 1, 0};
  MuStatePod state{};
  state.scene_major = 0x0E;
  state.scene = 1;
  state.match_frame = 54;
  for (int i = 0; i < 2; ++i) {
    auto& p = state.player[i];
    p.present = 1;
    p.stocks = 4;
    p.facing = bits(1);
    p.pos_x = bits(i ? 0.0f : 10.0f);
    p.percent = bits(30.0f * i);
  }
  MuLcancelView air{};
  air.port[0].present = 1;
  air.port[0].slot = 1; // local slot 1 is driven by physical port 0
  auto sample = fight_sample(c, state, air);
  CHECK(sample.port == 0 && sample.pad.stick_x == 40 && sample.pad.button == 0x0100);
  std::array<MuPadStatus, 4> pads{};
  for (auto& pad : pads) { pad.button = 0x1000; pad.stick_y = 15; pad.err = -1; }
  const auto original = pads;
  CHECK(apply_fight(pads.data(), sample, InputState{0, -1}, [](const char*) {}));
  CHECK(fields(pads[0]) == fields(sample.pad));
  for (int i = 1; i < 4; ++i) CHECK(fields(pads[i]) == fields(original[i]));
  c.local_port = 3;
  CHECK(fight_sample(c, state, air).port == 3);
  c.local_port = 0;
  auto inert = [&] {
    pads = original;
    CHECK(!apply_fight(pads.data(), fight_sample(c, state, air), InputState{0, -1}, [](const char*) {}));
    for (int i = 0; i < 4; ++i) CHECK(fields(pads[i]) == fields(original[i]));
  };
  for (bool FightContext::* gate : {&FightContext::enabled, &FightContext::yougame,
                                    &FightContext::online_test, &FightContext::in_match}) {
    c.*gate = false; inert(); c.*gate = true;
  }
  c.replay = true; inert(); c.replay = false;
  for (int port : {-1, 4}) { c.local_port = port; inert(); }
  c.local_port = 0;
  for (int slot : {-1, 4}) { c.local_slot = slot; inert(); }
  c.local_slot = 1;
  state.scene_major = 2; inert(); state.scene_major = 0x0E;
  state.scene = 0; inert(); state.scene = 1;
  state.match_frame = 0; inert(); state.match_frame = 54;
  state.player[1].present = 0; inert(); state.player[1].present = 1;
  state.player[1].stocks = 0; inert(); state.player[1].stocks = 4;
  state.player[0].present = 0; inert(); state.player[0].present = 1;
  state.player[0].stocks = 0; inert(); state.player[0].stocks = 4;
  air.port[0].present = 0; inert(); air.port[0].present = 1;
  state.player[2] = state.player[0]; inert(); state.player[2] = {};
  state.player[1].pos_x = bits(std::numeric_limits<float>::quiet_NaN()); inert();
  state.player[1].pos_x = bits(0);
  // Airborne and stage bounds really travel through the snapshot adapter.
  state.player[1].pos_x = bits(90);
  state.match_frame = 12;
  air.port[0].ground_or_air = 1;
  CHECK(fight_sample(c, state, air).pad.button == 0x0200);
  CHECK(fight_sample(c, state, air).pad.stick_x == -60);
  CHECK(current_fight_sample.port == -1); // non-game consumers start inactive
}

static void parsing(const char* scratch) {
  // The agent root accepts only a plain drive path in Windows form (C:\...); CMake passes "/".
  const auto directory = (std::filesystem::u8path(scratch) / "fight-profile").make_preferred();
  std::filesystem::create_directory(directory);
  const auto path = directory / "agent-test.json";
  auto load = [&](const nlohmann::json& object, bool active = true) {
    { std::ofstream file(path); file << object.dump(); CHECK(file.good()); }
    return yougame::agent_test::load(active, directory.string().c_str());
  };
  CHECK(!load(nlohmann::json::object()).fight);
  const auto enabled = load({{"fight", true}});
  CHECK(enabled.fight);
  CHECK(enabled.arguments.empty() && enabled.environment.empty());
  CHECK(enabled.log == "agent-test.json applied default keys: fight");
  CHECK(!load({{"fight", false}}).fight);
  CHECK(load({{"fight", false}}).log.empty());
  for (const auto& invalid : {nlohmann::json(0), nlohmann::json(1), nlohmann::json(1.0),
                            nlohmann::json("true"), nlohmann::json(nullptr),
                            nlohmann::json::array(), nlohmann::json::object()}) {
    const auto s = load({{"fight", invalid}});
    CHECK(!s.fight && s.arguments.empty() && s.environment.empty());
    CHECK(s.log == "agent-test.json; invalid/unknown or inapplicable keys ignored");
  }
  CHECK(load({{"fight", true}, {"mute", true}}).log == "agent-test.json applied default keys: fight, mute");
  CHECK(!load({{"fight", true}}, false).fight);
  CHECK(load({{"fight", true}}, false).log.empty());
  CHECK(!yougame::agent_test::load(true, nullptr).fight);
  CHECK(!yougame::agent_test::load(true, "").fight);
  std::filesystem::remove_all(directory);
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  policy();
  injection();
  parsing(argv[1]);
  std::printf("yougame_agent_fight: %u checks passed (policy, 180 s determinism, gating, local port, parsing)\n", checks);
}
#else
// This target compiles the same header without the hook. Its binary must contain no fight symbols.
int main() { return 0; }
#endif
