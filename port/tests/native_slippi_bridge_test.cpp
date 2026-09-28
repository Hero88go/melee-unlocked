// The native game's Slippi call contract: bounds, refusals and reply copying.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_slippi_bridge.h"
#include <cstdio>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

using namespace slippi::online;

namespace {
constexpr uint32_t CAPACITY = 4096;
int handler_calls = 0;
std::vector<uint8_t> next_reply;
bool fake_handler(uint8_t cmd, const uint8_t*, uint32_t, std::vector<uint8_t>& reply) {
  ++handler_calls;
  if (cmd == 0x42) return false;   // a command nobody owns
  reply = next_reply;
  return true;
}
int32_t call(uint8_t cmd, const uint8_t* payload, uint32_t size, uint8_t* response, uint32_t capacity,
             uint32_t* response_size, bool savestates = false) {
  return native_command(cmd, payload, size, response, capacity, response_size, CAPACITY, savestates,
                        fake_handler);
}
}  // namespace

int main() {
  std::vector<uint8_t> response(CAPACITY, 0xEE);
  uint32_t size = 123;
  uint8_t inputs[25] = {};

  // A missing size pointer is a bounds error before anything else happens.
  CHECK(call(CMD_ONLINE_INPUTS, inputs, 25, response.data(), CAPACITY, nullptr) == NATIVE_COMMAND_BOUNDS);

  // Savestates are refused without native support, even with a well-formed payload.
  uint8_t savestate[32] = {};
  CHECK(call(CMD_CAPTURE_SAVESTATE, savestate, 32, response.data(), CAPACITY, &size) == NATIVE_COMMAND_NEEDS_NATIVE_ROLLBACK);
  CHECK(size == 0);
  CHECK(call(CMD_LOAD_SAVESTATE, savestate, 32, response.data(), CAPACITY, &size) == NATIVE_COMMAND_NEEDS_NATIVE_ROLLBACK);
  CHECK(handler_calls == 0);

  // With native savestates the same payloads reach the handler.
  next_reply.clear();
  CHECK(call(CMD_CAPTURE_SAVESTATE, savestate, 32, response.data(), CAPACITY, &size, true) == NATIVE_COMMAND_OK);
  CHECK(call(CMD_LOAD_SAVESTATE, savestate, 32, response.data(), CAPACITY, &size, true) == NATIVE_COMMAND_OK);
  CHECK(handler_calls == 2);

  // Response buffer checks.
  CHECK(call(CMD_ONLINE_INPUTS, inputs, 25, nullptr, CAPACITY, &size) == NATIVE_COMMAND_BOUNDS);
  CHECK(call(CMD_ONLINE_INPUTS, inputs, 25, response.data(), CAPACITY - 1, &size) == NATIVE_COMMAND_BOUNDS);
  CHECK(call(CMD_ONLINE_INPUTS, nullptr, 25, response.data(), CAPACITY, &size) == NATIVE_COMMAND_BOUNDS);

  // Fixed payload sizes.
  CHECK(call(CMD_ONLINE_INPUTS, inputs, 24, response.data(), CAPACITY, &size) == NATIVE_COMMAND_BOUNDS);
  CHECK(call(CMD_GET_MATCH_STATE, inputs, 1, response.data(), CAPACITY, &size) == NATIVE_COMMAND_BOUNDS);
  CHECK(handler_calls == 2);

  // A valid call copies the handler's reply and reports its size.
  next_reply = {1, 2, 3, 4, 5};
  size = 99;
  CHECK(call(CMD_ONLINE_INPUTS, inputs, 25, response.data(), CAPACITY, &size) == NATIVE_COMMAND_OK);
  CHECK(size == 5 && response[0] == 1 && response[4] == 5 && response[5] == 0xEE);
  next_reply.clear();
  CHECK(call(CMD_GET_MATCH_STATE, nullptr, 0, response.data(), CAPACITY, &size) == NATIVE_COMMAND_OK);
  CHECK(size == 0);

  // Commands nobody owns are unknown; payload checks let them through to the dispatcher.
  CHECK(command_payload_valid(0x42, nullptr, 7));
  CHECK(call(0x42, inputs, 7, response.data(), CAPACITY, &size) == NATIVE_COMMAND_UNKNOWN);
  CHECK(size == 0);

  // A reply larger than the caller's buffer is a bounds error and copies nothing.
  next_reply.assign(CAPACITY + 1, 7);
  response.assign(CAPACITY + 1, 0xEE);
  CHECK(call(CMD_ONLINE_INPUTS, inputs, 25, response.data(), CAPACITY, &size) == NATIVE_COMMAND_BOUNDS);
  CHECK(size == 0 && response[0] == 0xEE);

  // The load preserve list must end with a zero address inside the payload.
  uint8_t load[32] = {};
  CHECK(command_payload_valid(CMD_LOAD_SAVESTATE, load, 32));
  for (int i = 4; i < 32; ++i) load[i] = 1;   // every slot nonzero: no terminator
  CHECK(!command_payload_valid(CMD_LOAD_SAVESTATE, load, 32));
  for (int i = 20; i < 24; ++i) load[i] = 0;  // terminator at the third block
  CHECK(command_payload_valid(CMD_LOAD_SAVESTATE, load, 32));
  std::printf("native slippi bridge: ok\n");
  return 0;
}
