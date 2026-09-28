// Byte layouts of the Slippi commands the online menus send (payloads exclude the command byte,
// console byte order) and of the replies they read. Reference codecs shared by the host tests and
// tools; the native game's shim builds the same bytes.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace slippi::menu {

// B3 match state reply (MSRB).
enum : uint32_t {
  MSRB_CONNECTION_STATE = 0, MSRB_LOCAL_READY = 1, MSRB_REMOTE_READY = 2, MSRB_LOCAL_INDEX = 3,
  MSRB_REMOTE_INDEX = 4, MSRB_RNG_OFFSET = 5, MSRB_DELAY = 9, MSRB_USER_CHAT = 10, MSRB_OPP_CHAT = 11,
  MSRB_CHAT_PLAYER = 12, MSRB_USER_RANK = 13, MSRB_OPP_RANK = 14, MSRB_LOCAL_NAME = 15,
  MSRB_PLAYER_NAMES = 46, MSRB_OPP_NAME = 170, MSRB_CONNECT_CODES = 201, MSRB_UIDS = 241,
  MSRB_ERROR = 357, MSRB_GAME_INFO = 598, MSRB_MATCH_ID = 910, MSRB_ALT_STAGE = 961, MSRB_SIZE = 962,
  NAME_LEN = 31, CODE_LEN = 10, UID_LEN = 29, ERROR_LEN = 241, GAME_INFO_LEN = 312, MATCH_ID_LEN = 51,
};
// B9 online status reply, B5 selections, B4 find opponent, BB chat, BE suggestion (request, reply).
enum : uint32_t { B9_SIZE = 42, B5_SIZE = 9, B4_SIZE = 19, BB_SIZE = 2, BE_SIZE = 31, BE_REPLY_SIZE = 30 };

inline uint32_t rd_be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
inline void wr_be32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
inline std::string cstr(const uint8_t* p, uint32_t max) { uint32_t n = 0; while (n < max && p[n]) ++n; return std::string((const char*)p, n); }

struct MatchState {
  uint8_t state, local_ready, remote_ready, local_index, remote_index, delay;
  uint8_t user_chat, opp_chat, chat_player, alt_stage;
  uint32_t rng_offset;
  std::string local_name, names[4], opp_name, codes[4], uids[4], error, match_id;
  const uint8_t* game_info;   // 312 raw bytes, console layout
};
inline bool parse_match_state(const uint8_t* r, uint32_t n, MatchState* m) {
  if (n != MSRB_SIZE) return false;
  m->state = r[MSRB_CONNECTION_STATE]; m->local_ready = r[MSRB_LOCAL_READY]; m->remote_ready = r[MSRB_REMOTE_READY];
  m->local_index = r[MSRB_LOCAL_INDEX]; m->remote_index = r[MSRB_REMOTE_INDEX];
  m->rng_offset = rd_be32(r + MSRB_RNG_OFFSET); m->delay = r[MSRB_DELAY];
  m->user_chat = r[MSRB_USER_CHAT]; m->opp_chat = r[MSRB_OPP_CHAT]; m->chat_player = r[MSRB_CHAT_PLAYER];
  m->local_name = cstr(r + MSRB_LOCAL_NAME, NAME_LEN);
  for (int i = 0; i < 4; ++i) {
    m->names[i] = cstr(r + MSRB_PLAYER_NAMES + i * NAME_LEN, NAME_LEN);
    m->codes[i] = cstr(r + MSRB_CONNECT_CODES + i * CODE_LEN, CODE_LEN);
    m->uids[i] = cstr(r + MSRB_UIDS + i * UID_LEN, UID_LEN);
  }
  m->opp_name = cstr(r + MSRB_OPP_NAME, NAME_LEN);
  m->error = cstr(r + MSRB_ERROR, ERROR_LEN);
  m->game_info = r + MSRB_GAME_INFO;
  m->match_id = cstr(r + MSRB_MATCH_ID, MATCH_ID_LEN);
  m->alt_stage = r[MSRB_ALT_STAGE];
  return true;
}

struct OnlineStatus { uint8_t app_state; std::string name, code; };
inline bool parse_online_status(const uint8_t* r, uint32_t n, OnlineStatus* s) {
  if (n != B9_SIZE) return false;
  s->app_state = r[0]; s->name = cstr(r + 1, NAME_LEN); s->code = cstr(r + 1 + NAME_LEN, CODE_LEN);
  return true;
}

// stage_opt: 0 unset, 1 pick, 3 random. team: Teams index - 1, else 0.
inline void encode_selections(uint8_t out[B5_SIZE], uint8_t team, uint8_t character, uint8_t color,
                              uint16_t stage, uint8_t stage_opt, uint8_t mode, uint8_t alt_stage) {
  out[0] = team; out[1] = character; out[2] = color; out[3] = 1;
  out[4] = (uint8_t)(stage >> 8); out[5] = (uint8_t)stage; out[6] = stage_opt; out[7] = mode; out[8] = alt_stage;
}
// name_text: the name-entry buffer, 3 bytes per letter (2 Shift-JIS bytes + 1); 9 letters fill 18 bytes.
inline void encode_find_opponent(uint8_t out[B4_SIZE], uint8_t mode, const uint8_t* name_text, uint32_t letters) {
  std::memset(out, 0, B4_SIZE);
  out[0] = mode;
  for (uint32_t i = 0; i < letters && i < 9; ++i) { out[1 + i * 2] = name_text[i * 3]; out[2 + i * 2] = name_text[i * 3 + 1]; }
}
inline void encode_chat(uint8_t out[BB_SIZE], uint8_t message_id) { out[0] = message_id; out[1] = 0; }
// scroll: 1 older, 2 newer, 3 reset.
inline void encode_suggestion(uint8_t out[BE_SIZE], const uint8_t input[24], uint8_t len, uint32_t index,
                              uint8_t scroll, uint8_t mode) {
  std::memcpy(out, input, 24); out[24] = len; wr_be32(out + 25, index); out[29] = scroll; out[30] = mode;
}
struct Suggestion { uint8_t found; uint8_t text[24]; uint8_t len; uint32_t index; };
inline bool parse_suggestion(const uint8_t* r, uint32_t n, Suggestion* s) {
  if (n != BE_REPLY_SIZE) return false;
  s->found = r[0]; std::memcpy(s->text, r + 1, 24); s->len = r[25]; s->index = rd_be32(r + 26);
  return true;
}

}  // namespace slippi::menu
