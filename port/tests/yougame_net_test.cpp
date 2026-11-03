// Window-free fake-clock scheduling, startup parsing and Direct-only policy checks.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_net_test.h"
#include "yougame_agent_test.h"
#ifdef YOUGAME_AGENT_TEST_HOOK
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace yougame;
using nlohmann::json;
void parsing() {
  using agent_test::map_keys;
  using agent_test::Engine;
  auto defaults = map_keys(json::object());
  CHECK(!defaults.net.enabled() && defaults.net.jitter_ms == 0);
  for (int delay : {0, 1, 90, 250}) {
    auto s = map_keys({{"netDelayMs", delay}});
    CHECK(s.net.delay_ms == unsigned(delay));
    CHECK(s.arguments.empty() && s.environment.empty());
    CHECK(s.log.find("ignored") == std::string::npos);
  }
  for (int jitter : {0, 1, 20, 100}) {
    auto s = map_keys({{"netDelayMs", 90}, {"netJitterMs", jitter}});
    CHECK(s.net.delay_ms == 90 && s.net.jitter_ms == unsigned(jitter));
  }
  for (const char* key : {"netDelayMs", "netJitterMs"}) {
    for (const auto& bad : {json(-1), json(251), json(1.0), json(true), json(false), json("90"),
         json(nullptr), json::array(), json::object(), json(UINT64_MAX)}) {
      auto object = json{{"netDelayMs", 90}};
      object[key] = bad;
      auto s = map_keys(object);
      CHECK(s.log.find(std::string("agent-test: ") + key + " invalid value; ignored") != std::string::npos);
      CHECK(key == std::string("netDelayMs") ? !s.net.enabled() : s.net.jitter_ms == 0);
    }
    auto s = map_keys({{key, 20}}, Engine::Static);
    CHECK(!s.net.enabled() && s.net.jitter_ms == 0);
    CHECK(s.log == std::string("agent-test: ") + key + " not supported on the Static Recomp; ignored");
  }
  CHECK(map_keys({{"netDelayMs", 90}, {"netJitterMs", 101}}).net.jitter_ms == 0);
  for (const auto& object : {json{{"netJitterMs", 0}}, json{{"netJitterMs", 20}},
       json{{"netDelayMs", 0}, {"netJitterMs", 20}},
       json{{"netDelayMs", -1}, {"netJitterMs", 20}}}) {
    auto s = map_keys(object);
    CHECK(!s.net.enabled() && s.net.jitter_ms == 0);
    CHECK(s.log.find("agent-test: netJitterMs requires netDelayMs > 0; ignored") != std::string::npos);
  }
  auto conflict = map_keys({{"netDelayMs", 90}, {"netJitterMs", 20}}, Engine::Source, {}, true);
  CHECK(!conflict.net.enabled() && conflict.net.jitter_ms == 0);
  CHECK(conflict.log == "agent-test: net delay keys conflict with receive-side injector; ignored");
  for (int mode = -1; mode < 6; ++mode) {
    CHECK(net_test::mode_allowed({}, mode));
    CHECK(net_test::mode_allowed({90, 20}, mode) == (mode == 2));
  }
  CHECK(std::string(net_test::mode_name(0)) == "ranked");
  CHECK(std::string(net_test::mode_name(1)) == "unranked");
  CHECK(std::string(net_test::mode_name(3)) == "teams");
  CHECK(std::string(net_test::mode_name(4)) == "party");
  net_test::install({90, 20}); net_test::install({120, 40});
  CHECK(net_test::config().delay_ms == 90 && net_test::config().jitter_ms == 20);
}
void queue() {
  using Q = net_test::DelayQueue<std::unique_ptr<int>>;
  auto value = [](int x) { return std::make_unique<int>(x); };
  std::vector<int> sent;
  auto send = [&](const auto& p) { sent.push_back(*p); };
  Q off({});
  auto p = value(1);
  CHECK(!off.push(0x80, 100, 0, std::move(p)) && p && off.size() == 0);
  CHECK(off.wait_ms(100, 250) == 250);
  Q q({60, 0});
  for (unsigned id = 0; id < 256; ++id) {
    auto packet = value(id);
    CHECK(q.push(id, 100, 0, std::move(packet)) == (id == 0x80 || id == 0xE1));
    if (!net_test::input_message(id)) CHECK(packet);
  }
  CHECK(q.size() == 2 && q.wait_ms(100, 250) == 60);
  CHECK(q.wait_ms(100, 1) == 1);
  CHECK(!q.drain(159, send) && sent.empty());
  CHECK(q.wait_ms(160, 250) == 0);
  CHECK(q.drain(160, send) && sent == (std::vector<int>{0x80, 0xE1}));
  CHECK(q.wait_ms(160, 250) == 250);
  CHECK(q.push(0x80, 170, 0, value(1)));
  q.reset(1); // StartSlippiGame: pending and not-yet-drained async inputs both expire.
  CHECK(q.size() == 0);
  CHECK(q.push(0xE1, 170, 0, value(2)) && q.size() == 0);
  CHECK(q.push(0x80, 170, 1, value(3)));
  q.reset(1); CHECK(q.size() == 1); // Repeated generation observations cannot discard inputs.
  CHECK(q.drain(230, send) && sent.back() == 3);
  CHECK(q.push(0x80, 240, 1, value(4)));
  q.reset(2); // Disconnect: do not send anything left from this peer/session.
  CHECK(!q.drain(9999, send) && sent.back() == 3);

  Q jitter({90, 100}), repeat({90, 100});
  std::vector<int> order, order2;
  uint64_t previous_deadline = 0;
  for (int i = 0; i < 500; ++i) {
    const uint64_t enqueued = 1000 + i;
    CHECK(jitter.push(i & 1 ? 0x80 : 0xE1, enqueued, 0, value(i)));
    CHECK(repeat.push(i & 1 ? 0x80 : 0xE1, enqueued, 0, value(i)));
  }
  for (uint64_t t = 1000; t <= 1690; ++t) {
    jitter.drain(t, [&](const auto& packet) {
      const uint64_t enqueued = 1000 + *packet;
      CHECK(t >= enqueued + 90 && t <= enqueued + 190);
      CHECK(t >= previous_deadline); previous_deadline = t;
      order.push_back(*packet);
    });
    repeat.drain(t, [&](const auto& packet) { order2.push_back(*packet); });
    CHECK(order == order2); // Private fixed seed, independent of gameplay/global RNG.
  }
  CHECK(order.size() == 500 && jitter.size() == 0);
  for (int i = 0; i < 500; ++i) CHECK(order[i] == i);
}
int main() { parsing(); queue(); std::puts("net test: parsing, modes and fake-clock queue passed"); }
#else
int main() { return 0; }
#endif
