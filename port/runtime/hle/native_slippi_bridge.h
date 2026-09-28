// Command ids, payload bounds and the call contract the native game uses to reach the Slippi host.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace slippi::online {

enum Cmd : uint8_t {
  CMD_ONLINE_INPUTS = 0xB0, CMD_CAPTURE_SAVESTATE = 0xB1, CMD_LOAD_SAVESTATE = 0xB2, CMD_GET_MATCH_STATE = 0xB3, CMD_FIND_OPPONENT = 0xB4,
  CMD_SET_MATCH_SELECTIONS = 0xB5, CMD_OPEN_LOGIN = 0xB6, CMD_LOGOUT = 0xB7, CMD_UPDATE = 0xB8, CMD_GET_ONLINE_STATUS = 0xB9,
  CMD_CLEANUP_CONNECTION = 0xBA, CMD_SEND_CHAT_MESSAGE = 0xBB, CMD_GET_NEW_SEED = 0xBC, CMD_REPORT_GAME = 0xBD, CMD_FETCH_CODE_SUGGESTION = 0xBE,
  CMD_OVERWRITE_SELECTIONS = 0xBF, CMD_GP_COMPLETE_STEP = 0xC0, CMD_GP_FETCH_STEP = 0xC1, CMD_REPORT_SET_COMPLETE = 0xC2,
  CMD_GET_PLAYER_SETTINGS = 0xC3, CMD_REPORT_MATCH_STATUS_UPDATE = 0xC4, CMD_GET_DELAY = 0xD5,
  CMD_GET_RANK = 0xE3, CMD_FETCH_RANK = 0xE4, CMD_GET_RANK_VISIBILITY = 0xE5,
};

// Results of a native command call, mirrored by the host ABI comment in mu_host.h.
enum : int32_t {
  NATIVE_COMMAND_OK = 0,
  NATIVE_COMMAND_UNKNOWN = -1,
  NATIVE_COMMAND_BOUNDS = -2,
  NATIVE_COMMAND_NEEDS_NATIVE_ROLLBACK = -3,
};

namespace bridge_detail {
inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
}  // namespace bridge_detail

// Fixed EXI payload sizes. Unknown commands pass here; the dispatcher reports them itself.
inline bool command_payload_valid(uint8_t cmd, const uint8_t* payload, uint32_t payload_len) {
  uint32_t expected = 0;
  switch (cmd) {
    case CMD_ONLINE_INPUTS: expected = 25; break;
    case CMD_CAPTURE_SAVESTATE: case CMD_LOAD_SAVESTATE: expected = 32; break;
    case CMD_FIND_OPPONENT: expected = 19; break;
    case CMD_SET_MATCH_SELECTIONS: expected = 9; break;
    case CMD_SEND_CHAT_MESSAGE: expected = 2; break;
    case CMD_REPORT_GAME: expected = 368; break;
    case CMD_FETCH_CODE_SUGGESTION: expected = 31; break;
    case CMD_OVERWRITE_SELECTIONS: expected = 14; break;
    case CMD_GP_COMPLETE_STEP: expected = 5; break;
    case CMD_GP_FETCH_STEP: case CMD_REPORT_SET_COMPLETE:
    case CMD_REPORT_MATCH_STATUS_UPDATE: expected = 1; break;
    case CMD_GET_MATCH_STATE: case CMD_OPEN_LOGIN: case CMD_LOGOUT: case CMD_UPDATE:
    case CMD_GET_ONLINE_STATUS: case CMD_CLEANUP_CONNECTION: case CMD_GET_NEW_SEED:
    case CMD_GET_PLAYER_SETTINGS: case CMD_GET_DELAY: case CMD_GET_RANK:
    case CMD_FETCH_RANK: case CMD_GET_RANK_VISIBILITY: expected = 0; break;
    default: return true;
  }
  if (payload_len != expected || (expected && !payload)) return false;
  if (cmd == CMD_LOAD_SAVESTATE) {
    // The preserve-block list ends with a zero address. The last 4-byte slot cannot hold a
    // nonzero address because there is no room for its length.
    for (uint32_t i = 4; i + 4 <= expected; i += 8) {
      if (bridge_detail::be32(payload + i) == 0) return true;
      if (i + 8 > expected) return false;
    }
    return false;
  }
  return true;
}

// The whole native call contract. `handle(cmd, payload, size, reply)` returns false for commands
// it does not own. Savestate commands are refused until the host can snapshot the native game.
template <class Handler>
int32_t native_command(uint8_t command, const uint8_t* payload, uint32_t payload_size,
                       uint8_t* response, uint32_t response_capacity, uint32_t* response_size,
                       uint32_t minimum_capacity, bool native_savestates, Handler&& handle) {
  if (!response_size) return NATIVE_COMMAND_BOUNDS;
  *response_size = 0;
  if (!native_savestates && (command == CMD_CAPTURE_SAVESTATE || command == CMD_LOAD_SAVESTATE))
    return NATIVE_COMMAND_NEEDS_NATIVE_ROLLBACK;
  if (!response || response_capacity < minimum_capacity || (payload_size && !payload))
    return NATIVE_COMMAND_BOUNDS;
  if (!command_payload_valid(command, payload, payload_size)) return NATIVE_COMMAND_BOUNDS;
  std::vector<uint8_t> reply;
  if (!handle(command, payload, payload_size, reply)) return NATIVE_COMMAND_UNKNOWN;
  if (reply.size() > response_capacity) return NATIVE_COMMAND_BOUNDS;
  if (!reply.empty()) std::memcpy(response, reply.data(), reply.size());
  *response_size = (uint32_t)reply.size();
  return NATIVE_COMMAND_OK;
}

}  // namespace slippi::online
