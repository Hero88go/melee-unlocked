#include "yougame_unranked.h"
#include "native_slippi_bridge.h"
#include <cstdio>
#include <cstdlib>
int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
int main() {
  using namespace yougame::unranked;
  // Searching, game over, peer left and Z all keep the same destination; only the route matters.
  for (bool active : {false, true}) for (int mode : {-1, 0, 1, 2, 3, 4}) {
    const bool expected = active && mode == 1;
    CHECK(keep_css(active, mode) == expected);
    const bool training = mode != 0 && !expected;
    CHECK(allow_training_return(active, mode) == training);
    for (bool training_setting : {false, true})
      CHECK((training_setting && allow_training_return(active, mode)) == (training_setting && training));
  }
  {
    // A ranked or Direct session: over once, only after a match, and a search that connected without
    // playing is not "over".
    SessionWatch w;
    CHECK(!w.tick(false, false, false));
    CHECK(!w.tick(true, false, false));
    CHECK(!w.tick(false, false, false));
    CHECK(!w.tick(true, false, false));
    CHECK(!w.tick(true, true, false));
    CHECK(!w.tick(true, false, false));
    CHECK(w.tick(false, false, false));
    CHECK(!w.tick(false, false, false));
    // A set: the session runs across its games and is over once, at the end.
    CHECK(!w.tick(true, true, false));
    CHECK(!w.tick(true, false, false));
    CHECK(!w.tick(true, true, false));
    CHECK(w.tick(false, false, false));
  }
  {
    // A Doubles or Friends lobby: a match, the home's open search, then the next match connecting
    // (session reads over for a tick). Never Training, and it leaves nothing behind for a later session.
    SessionWatch w;
    CHECK(!w.tick(true, false, true));
    CHECK(!w.tick(true, true, true));
    CHECK(!w.tick(true, false, true));
    CHECK(!w.tick(false, false, true));   // the second search connected, its match not begun
    CHECK(!w.tick(true, true, true));
    CHECK(!w.tick(false, false, true));   // a gap between the match's cleanup and the home's search
    CHECK(!w.tick(true, false, true));
    CHECK(!w.tick(true, false, false));   // left the lobby: disarmed on the tick before the session ends
    CHECK(!w.tick(false, false, false));
    CHECK(!w.tick(true, true, false));    // the next, ordinary session still returns
    CHECK(w.tick(false, false, false));
  }
  {
    // Leaving a lobby mid-match: disarmed before the session reads over, so only the latch can hold it.
    SessionWatch w;
    CHECK(!w.tick(true, true, true));
    CHECK(!w.tick(false, false, false));
    // A lobby armed on the tick an ordinary session ends: its select is up, so no Training over it.
    CHECK(!w.tick(true, true, false));
    CHECK(!w.tick(false, false, true));
  }
  using namespace slippi::online;
  // 2: the Friends lobby's select is back after a match; 3: the main menu came up (a kept search goes on)
  uint8_t reason = 1, next = 2, menu = 3, invalid = 4, zero = 0;
  CHECK(command_payload_valid(CMD_CLEANUP_CONNECTION, nullptr, 0));
  CHECK(command_payload_valid(CMD_CLEANUP_CONNECTION, &reason, 1));
  CHECK(command_payload_valid(CMD_CLEANUP_CONNECTION, &next, 1));
  CHECK(command_payload_valid(CMD_CLEANUP_CONNECTION, &menu, 1));
  CHECK(!command_payload_valid(CMD_CLEANUP_CONNECTION, &zero, 1));
  CHECK(!command_payload_valid(CMD_CLEANUP_CONNECTION, nullptr, 1));
  CHECK(!command_payload_valid(CMD_CLEANUP_CONNECTION, &invalid, 1));
  // The lobby select's private command: exact sizes and ranges only.
  const uint8_t enter[] = {MU_LOBBY_OP_ENTER}, read_view[] = {MU_LOBBY_OP_READ}, start[] = {MU_LOBBY_OP_START, 0};
  const uint8_t start21[] = {MU_LOBBY_OP_START, 21}, pick[] = {MU_LOBBY_OP_PICK, 25, 5, 2}, bad_pick[] = {MU_LOBBY_OP_PICK, 26, 0, 0};
  const uint8_t rules[] = {MU_LOBBY_OP_RULES, 1, 0, 0, 4, 8, 1}, bad_rules[] = {MU_LOBBY_OP_RULES, 1, 0, 0, 0, 8, 1};
  const uint8_t seat[] = {MU_LOBBY_OP_SEAT, 3, 9}, bad_seat[] = {MU_LOBBY_OP_SEAT, 6, 1}, cpu_up[] = {MU_LOBBY_OP_CPU_PICK, 2, MU_LOBBY_NONE, 0, 0};
  const uint8_t enter_long[] = {MU_LOBBY_OP_ENTER, 1}, unknown[] = {MU_LOBBY_OP_COUNT};
  CHECK(command_payload_valid(MU_LOBBY_COMMAND, enter, sizeof enter));
  CHECK(command_payload_valid(MU_LOBBY_COMMAND, read_view, sizeof read_view));
  CHECK(command_payload_valid(MU_LOBBY_COMMAND, start, sizeof start));
  CHECK(!command_payload_valid(MU_LOBBY_COMMAND, start21, sizeof start21));
  CHECK(command_payload_valid(MU_LOBBY_COMMAND, pick, sizeof pick));
  CHECK(!command_payload_valid(MU_LOBBY_COMMAND, bad_pick, sizeof bad_pick));
  CHECK(command_payload_valid(MU_LOBBY_COMMAND, rules, sizeof rules));
  CHECK(!command_payload_valid(MU_LOBBY_COMMAND, bad_rules, sizeof bad_rules));
  CHECK(command_payload_valid(MU_LOBBY_COMMAND, seat, sizeof seat));
  CHECK(!command_payload_valid(MU_LOBBY_COMMAND, bad_seat, sizeof bad_seat));
  CHECK(command_payload_valid(MU_LOBBY_COMMAND, cpu_up, sizeof cpu_up));
  CHECK(!command_payload_valid(MU_LOBBY_COMMAND, enter_long, sizeof enter_long));
  CHECK(!command_payload_valid(MU_LOBBY_COMMAND, unknown, sizeof unknown));
  CHECK(!command_payload_valid(MU_LOBBY_COMMAND, nullptr, 0));
  CHECK(!command_payload_valid(CMD_CLEANUP_CONNECTION, &reason, 2));
  std::printf("yougame_unranked: %d checks passed\n", checks);
}
