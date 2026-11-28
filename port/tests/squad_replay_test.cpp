// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
#if MU_SQUAD
#include "mu_squad_wire.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
static int writes, logs, spectator_logs;
static bool g_replay;
static uint32_t g_online_games;
static int g_online_test_mode = -1;
static struct { uint64_t game_end_ms; bool reentry_asked; } g_ranked_harness;
static uint64_t GetTickCount64() { return 1; }
static bool g_retail_view;
static std::string g_view_alias, g_mod_fingerprint;
static const uint8_t native_online_recording_codes[] = {0};
static const uint8_t native_recording_codes[] = {0};
static unsigned te_words_mask1() { return 0; }
static unsigned h_game_options2() { return 0; }
static unsigned h_game_options3() { return 0; }
namespace gx { struct RenderOptions { static unsigned live_te_options() { return 0; } }; }
namespace host {
static struct { bool vanilla_game = true; } options;
namespace gameplay { static bool vanilla_only() { return true; } }
static void publish_lobby_result(int, int, int, const uint8_t*, int = 4) {}
template<class... T> void log(const char* line, T...) {
  if (std::strcmp(line, "replay: not kept: Squad Strike swaps cannot be played back yet") == 0) ++logs;
  if (std::strcmp(line, "spectate: not streamed: Squad Strike swaps cannot be watched yet") == 0) ++spectator_logs;
}
}
namespace slippi::spectate {
static int starts, writes, ends;
static std::string order;
static void start_game() { ++starts; order += 'S'; }
static void write(const uint8_t*, uint32_t) { ++writes; order += 'W'; }
static void end_game() { ++ends; order += 'E'; }
}
static struct {
  bool on = false, done = false, fail = false;
  void (*sink)(const uint8_t*, uint32_t) = nullptr;
  bool active() const { return on; }
  bool ended() const { return done; }
  std::string error() const { return "fixture"; }
  void set_event_sink(void (*value)(const uint8_t*, uint32_t)) { sink = value; }
  void set_mod_profile(const std::string&) {}
  void set_feature_options(unsigned) {}
  void set_feature_options2(unsigned) {}
  void set_feature_options3(unsigned) {}
  bool begin_slippi(const std::vector<uint8_t>&, const std::vector<uint8_t>&, int64_t) {
    on = !fail; done = false;
    if (fail) return false;
    // Production begin_slippi sends the command table and Game Start through this sink.
    if (sink) { sink(nullptr, 1); sink(nullptr, 1); }
    return true;
  }
  bool append(uint8_t command, const uint8_t* payload, uint32_t size) {
    if (sink) sink(payload, size);
    if (command == 0x39) done = true;
    return true;
  }
} g_replay_stream;
static struct {
  int32_t (*slippi_command)(uint8_t, const uint8_t*, uint32_t, uint8_t*, uint32_t, uint32_t*);
} fixture_host{};
static const auto* mu_host = &fixture_host;
// Generated from the production interception, writer guard and FULL replay event handler.
#include "squad_replay_fixture.h"

static void announce(uint8_t squad) {
  const uint8_t payload[] = {MU_SQUAD_OP_REPLAY_MATCH, squad};
  uint32_t size = 99;
  assert(mu_slippi_command(MU_SQUAD_COMMAND, payload, 2, nullptr, 0, &size) == 0);
  assert(size == 0);
}
static void event(uint8_t command) {
  uint8_t payload[0x2F8]{};
  h_replay_event(command, payload, sizeof payload);
}
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
int main() {
  using namespace slippi::spectate;
  fixture_host.slippi_command = h_slippi_command;
  announce(1); event(0x36); event(0x38); event(0x39);
  assert(g_replay_squad && !g_squad_replay_pending && !g_replay_stream.sink);
  assert(starts == 0 && slippi::spectate::writes == 0 && ends == 0 && spectator_logs == 1);
  write_replay_recording(); write_replay_recording();
  assert(::writes == 0 && logs == 1);

  // A normal match after a squad streams, and writes at ITS OWN Game End.
  event(0x36); event(0x38);
  assert(starts == 1 && slippi::spectate::writes > 0 && ends == 0 && ::writes == 0);
  event(0x39);
  assert(::writes == 1); // Explicitly before any subsequent Game Start.
  assert(ends == 1 && order.front() == 'S' && order.back() == 'E');
  const auto previous = order;
  announce(1); event(0x36); event(0x38); event(0x39);
  assert(order == previous && ends == 1 && spectator_logs == 2 && logs == 2);

  // Existing interrupted-match behavior: next start flushes the recording but sends no synthetic end.
  event(0x36); event(0x38);
  assert(starts == 2 && ends == 1);
  const auto disconnected = order;
  announce(1); event(0x36); event(0x38); event(0x39);
  assert(order == disconnected && ends == 1 && !g_replay_stream.sink);
  assert(::writes == 2 && spectator_logs == 3 && logs == 3);
  event(0x36); event(0x39);
  assert(starts == 3 && ends == 2 && ::writes == 3);

  // Begin failure must not send an end for a squad, even after a normal sink existed.
  g_replay_stream.fail = true;
  announce(1); event(0x36);
  assert(starts == 3 && ends == 2 && !g_replay_stream.sink && spectator_logs == 4);
  event(0x36);
  assert(starts == 4 && ends == 3); // normal failure retains today's closing meta-event
}
#endif
