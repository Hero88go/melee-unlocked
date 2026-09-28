// Payload and reply layouts of the Slippi commands the online menus use (B3, B4, B5, B9, BB, BE),
// checked against hand-written bytes from the design's field tables and through the bridge.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_slippi_bridge.h"
#include "slippi_menu_codec.h"
#include <cstdio>
#include <cstring>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

using namespace slippi;

namespace {
std::vector<uint8_t> g_reply;
uint8_t g_last_cmd = 0;
std::vector<uint8_t> g_last_payload;
bool fake(uint8_t cmd, const uint8_t* p, uint32_t n, std::vector<uint8_t>& reply) {
  g_last_cmd = cmd; g_last_payload.assign(p, p + n); reply = g_reply; return true;
}
int32_t call(uint8_t cmd, const uint8_t* p, uint32_t n, std::vector<uint8_t>& out) {
  out.assign(4096, 0); uint32_t size = 0;
  const int32_t r = online::native_command(cmd, p, n, out.data(), 4096, &size, 4096, true, fake);
  out.resize(size);
  return r;
}
}  // namespace

int main() {
  std::vector<uint8_t> out;

  // B5: team, char, colour, 1, stage u16 BE, stage opt, mode, alt stage.
  uint8_t b5[menu::B5_SIZE];
  menu::encode_selections(b5, 2, 0x14, 3, 0x001F, 1, 3, 1);
  const uint8_t b5_want[9] = {0x02, 0x14, 0x03, 0x01, 0x00, 0x1F, 0x01, 0x03, 0x01};
  CHECK(std::memcmp(b5, b5_want, 9) == 0);
  CHECK(call(online::CMD_SET_MATCH_SELECTIONS, b5, 9, out) == online::NATIVE_COMMAND_OK);
  CHECK(g_last_cmd == 0xB5 && g_last_payload.size() == 9);
  CHECK(call(online::CMD_SET_MATCH_SELECTIONS, b5, 8, out) == online::NATIVE_COMMAND_BOUNDS);

  // B4: mode, then 2 of every 3 name-entry bytes ("PEER#001", '#' = 81 94).
  const uint8_t text[24] = {0x82,0x6F,0, 0x82,0x64,0, 0x82,0x64,0, 0x82,0x71,0,
                            0x81,0x94,0, 0x82,0x4F,0, 0x82,0x4F,0, 0x82,0x50,0};
  uint8_t b4[menu::B4_SIZE];
  menu::encode_find_opponent(b4, 2, text, 8);
  const uint8_t b4_want[19] = {0x02, 0x82,0x6F, 0x82,0x64, 0x82,0x64, 0x82,0x71, 0x81,0x94,
                               0x82,0x4F, 0x82,0x4F, 0x82,0x50, 0x00,0x00};
  CHECK(std::memcmp(b4, b4_want, 19) == 0);
  CHECK(call(online::CMD_FIND_OPPONENT, b4, 19, out) == online::NATIVE_COMMAND_OK);
  CHECK(call(online::CMD_FIND_OPPONENT, b4, 18, out) == online::NATIVE_COMMAND_BOUNDS);

  // BB: message id and a pad byte.
  uint8_t bb[menu::BB_SIZE];
  menu::encode_chat(bb, 0x12);
  CHECK(bb[0] == 0x12 && bb[1] == 0);
  CHECK(call(online::CMD_SEND_CHAT_MESSAGE, bb, 2, out) == online::NATIVE_COMMAND_OK);
  CHECK(call(online::CMD_SEND_CHAT_MESSAGE, bb, 1, out) == online::NATIVE_COMMAND_BOUNDS);

  // BE request: 8x3 input, length, index u32 BE, scroll, mode.
  uint8_t be[menu::BE_SIZE];
  menu::encode_suggestion(be, text, 4, 0x01020304u, 3, 2);
  CHECK(std::memcmp(be, text, 24) == 0);
  CHECK(be[24] == 4 && be[25] == 1 && be[26] == 2 && be[27] == 3 && be[28] == 4 && be[29] == 3 && be[30] == 2);
  CHECK(call(online::CMD_FETCH_CODE_SUGGESTION, be, 31, out) == online::NATIVE_COMMAND_OK);
  CHECK(call(online::CMD_FETCH_CODE_SUGGESTION, be, 30, out) == online::NATIVE_COMMAND_BOUNDS);

  // BE reply: found, 8x3 suggestion, length, new index u32 BE (30 bytes).
  g_reply.assign(30, 0);
  g_reply[0] = 1; std::memcpy(&g_reply[1], text, 24); g_reply[25] = 8;
  g_reply[26] = 0; g_reply[27] = 0; g_reply[28] = 0; g_reply[29] = 5;
  CHECK(call(online::CMD_FETCH_CODE_SUGGESTION, be, 31, out) == online::NATIVE_COMMAND_OK);
  menu::Suggestion s{};
  CHECK(menu::parse_suggestion(out.data(), (uint32_t)out.size(), &s));
  CHECK(s.found == 1 && s.len == 8 && s.index == 5 && std::memcmp(s.text, text, 24) == 0);
  CHECK(!menu::parse_suggestion(out.data(), 29, &s));

  // B9 reply: app state, name[31], code[10] (42 bytes).
  g_reply.assign(42, 0);
  g_reply[0] = 1; std::memcpy(&g_reply[1], "Tester", 6); std::memcpy(&g_reply[32], "TEST\x81\x94" "001", 9);
  CHECK(call(online::CMD_GET_ONLINE_STATUS, nullptr, 0, out) == online::NATIVE_COMMAND_OK);
  menu::OnlineStatus st{};
  CHECK(menu::parse_online_status(out.data(), (uint32_t)out.size(), &st));
  CHECK(st.app_state == 1 && st.name == "Tester" && st.code == "TEST\x81\x94" "001");
  CHECK(call(online::CMD_GET_ONLINE_STATUS, b5, 1, out) == online::NATIVE_COMMAND_BOUNDS);

  // B3 reply (MSRB, 962 bytes) at the design's offsets.
  static_assert(menu::MSRB_SIZE == 962, "MSRB size");
  static_assert(menu::MSRB_GAME_INFO + menu::GAME_INFO_LEN == menu::MSRB_MATCH_ID, "game info");
  static_assert(menu::MSRB_ERROR + menu::ERROR_LEN == menu::MSRB_GAME_INFO, "error");
  g_reply.assign(962, 0);
  g_reply[0] = 4; g_reply[1] = 1; g_reply[2] = 0; g_reply[3] = 1; g_reply[4] = 0;
  g_reply[5] = 0xDE; g_reply[6] = 0xAD; g_reply[7] = 0xBE; g_reply[8] = 0xEF; g_reply[9] = 2;
  g_reply[10] = 0x11; g_reply[11] = 0x22; g_reply[12] = 1;
  std::memcpy(&g_reply[15], "Local", 5);
  std::memcpy(&g_reply[46 + 31], "Remote", 6);
  std::memcpy(&g_reply[170], "Remote", 6);
  std::memcpy(&g_reply[201 + 10], "PEER\x81\x94" "002", 9);
  std::memcpy(&g_reply[241], "uid0", 4);
  std::memcpy(&g_reply[357], "err", 3);
  g_reply[598] = 0x32; g_reply[598 + 311] = 0x7A;
  std::memcpy(&g_reply[910], "mode.direct-1", 13);
  g_reply[961] = 1;
  CHECK(call(online::CMD_GET_MATCH_STATE, nullptr, 0, out) == online::NATIVE_COMMAND_OK);
  menu::MatchState m{};
  CHECK(menu::parse_match_state(out.data(), (uint32_t)out.size(), &m));
  CHECK(m.state == 4 && m.local_ready == 1 && m.remote_ready == 0 && m.local_index == 1 && m.remote_index == 0);
  CHECK(m.rng_offset == 0xDEADBEEFu && m.delay == 2 && m.user_chat == 0x11 && m.opp_chat == 0x22 && m.chat_player == 1);
  CHECK(m.local_name == "Local" && m.names[1] == "Remote" && m.names[0].empty() && m.opp_name == "Remote");
  CHECK(m.codes[1] == "PEER\x81\x94" "002" && m.uids[0] == "uid0" && m.error == "err");
  CHECK(m.game_info[0] == 0x32 && m.game_info[311] == 0x7A && m.match_id == "mode.direct-1" && m.alt_stage == 1);
  CHECK(!menu::parse_match_state(out.data(), 961, &m));

  // BC reply: 4-byte seed; BA, B9 and B3 take no payload.
  g_reply = {0x12, 0x34, 0x56, 0x78};
  CHECK(call(online::CMD_GET_NEW_SEED, nullptr, 0, out) == online::NATIVE_COMMAND_OK);
  CHECK(out.size() == 4 && menu::rd_be32(out.data()) == 0x12345678u);
  CHECK(call(online::CMD_CLEANUP_CONNECTION, nullptr, 0, out) == online::NATIVE_COMMAND_OK);

  std::puts("native_slippi_menu_payload: ok");
  return 0;
}
