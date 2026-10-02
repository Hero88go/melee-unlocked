// SPDX-License-Identifier: GPL-2.0-or-later
#include "exi_slippi.h"
#include "jukebox.h"
#include "slippi_playback.h"
#include "slippi_online.h"
#include "native_practice.h"
#include "gecko_data.h"
#include "host.h"
#include "vcdiff.h"
#include "lab_view.h"
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <atomic>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <thread>
#include <map>
#include <unordered_map>
#include <vector>

// See gecko_data.h: the fallback for a translation that predates the PAL stock icons flag.
namespace gecko { bool option_pal_stock_icons_default = false; }
#pragma comment(linker, "/alternatename:?option_pal_stock_icons@gecko@@3_NA=?option_pal_stock_icons_default@gecko@@3_NA")
namespace gecko { bool option_no_screen_shake_default = false; }
#pragma comment(linker, "/alternatename:?option_no_screen_shake@gecko@@3_NA=?option_no_screen_shake_default@gecko@@3_NA")
// Same for the Lagless FoD flag and the optional-code table (an older prebuilt playback guest has neither).
namespace gecko { bool option_lagless_fod_default = false; extern const OptionalCode optional_codes_default[1] = {}; extern const size_t optional_codes_count_default = 0; }
#pragma comment(linker, "/alternatename:?option_lagless_fod@gecko@@3_NA=?option_lagless_fod_default@gecko@@3_NA")
#pragma comment(linker, "/alternatename:?optional_codes@gecko@@3QBUOptionalCode@1@B=?optional_codes_default@gecko@@3QBUOptionalCode@1@B")
#pragma comment(linker, "/alternatename:?optional_codes_count@gecko@@3_KB=?optional_codes_count_default@gecko@@3_KB")
// And the start of the port's private suffix (zero: no suffix known, the replay code list is left as sent).
namespace gecko { extern const uint32_t port_gct_offset_default = 0; }
#pragma comment(linker, "/alternatename:?port_gct_offset@gecko@@3IB=?port_gct_offset_default@gecko@@3IB")

namespace slippi {
namespace {

enum Cmd : uint8_t {
  CMD_RECEIVE_COMMANDS = 0x35, CMD_RECEIVE_GAME_INFO = 0x36, CMD_RECEIVE_POST_FRAME_UPDATE = 0x38, CMD_RECEIVE_GAME_END = 0x39,
  CMD_RECEIVE_INITIAL_RNG = 0x3A, CMD_RECEIVE_ITEM = 0x3B, CMD_FRAME_BOOKEND = 0x3C, CMD_GECKO_LIST = 0x3D, CMD_MENU_FRAME = 0x3E,
  CMD_RECEIVE_FOD_INFO = 0x3F, CMD_RECEIVE_DL_INFO = 0x40, CMD_RECEIVE_PS_INFO = 0x41, CMD_RECEIVE_BONES = 0x60,
  CMD_PREPARE_REPLAY = 0x75, CMD_READ_FRAME = 0x76, CMD_GET_LOCATION = 0x77, CMD_IS_FILE_READY = 0x88, CMD_IS_STOCK_STEAL = 0x89, CMD_GET_GECKO_CODES = 0x8A,
  CMD_ONLINE_INPUTS = 0xB0, CMD_CAPTURE_SAVESTATE = 0xB1, CMD_LOAD_SAVESTATE = 0xB2, CMD_GET_MATCH_STATE = 0xB3, CMD_FIND_OPPONENT = 0xB4,
  CMD_SET_MATCH_SELECTIONS = 0xB5, CMD_OPEN_LOGIN = 0xB6, CMD_LOGOUT = 0xB7, CMD_UPDATE = 0xB8, CMD_GET_ONLINE_STATUS = 0xB9,
  CMD_CLEANUP_CONNECTION = 0xBA, CMD_SEND_CHAT_MESSAGE = 0xBB, CMD_GET_NEW_SEED = 0xBC, CMD_REPORT_GAME = 0xBD, CMD_FETCH_CODE_SUGGESTION = 0xBE,
  CMD_OVERWRITE_SELECTIONS = 0xBF, CMD_GP_COMPLETE_STEP = 0xC0, CMD_GP_FETCH_STEP = 0xC1, CMD_REPORT_SET_COMPLETE = 0xC2,
  CMD_GET_PLAYER_SETTINGS = 0xC3, CMD_REPORT_MATCH_STATUS_UPDATE = 0xC4,
  CMD_LOG_MESSAGE = 0xD0, CMD_FILE_LENGTH = 0xD1, CMD_FILE_LOAD = 0xD2, CMD_GCT_LENGTH = 0xD3, CMD_GCT_LOAD = 0xD4, CMD_GET_DELAY = 0xD5,
  CMD_PLAY_MUSIC = 0xD6, CMD_STOP_MUSIC = 0xD7, CMD_CHANGE_MUSIC_VOLUME = 0xD8, CMD_PREMADE_TEXT_LENGTH = 0xE1, CMD_PREMADE_TEXT_LOAD = 0xE2,
  CMD_GET_RANK = 0xE3, CMD_FETCH_RANK = 0xE4, CMD_GET_RANK_VISIBILITY = 0xE5,
};

// Fixed payload sizes (bytes after the command byte), from CEXISlippi::payloadSizes.
std::unordered_map<uint8_t, uint32_t> g_payload_sizes = {
    {CMD_RECEIVE_COMMANDS, 1}, {CMD_PREPARE_REPLAY, 0xFFFF}, {CMD_READ_FRAME, 4}, {CMD_IS_STOCK_STEAL, 5}, {CMD_GET_LOCATION, 6},
    {CMD_IS_FILE_READY, 0}, {CMD_GET_GECKO_CODES, 0}, {CMD_ONLINE_INPUTS, 25}, {CMD_CAPTURE_SAVESTATE, 32}, {CMD_LOAD_SAVESTATE, 32},
    {CMD_GET_MATCH_STATE, 0}, {CMD_FIND_OPPONENT, 19}, {CMD_SET_MATCH_SELECTIONS, 9}, {CMD_SEND_CHAT_MESSAGE, 2}, {CMD_OPEN_LOGIN, 0},
    {CMD_LOGOUT, 0}, {CMD_UPDATE, 0}, {CMD_GET_ONLINE_STATUS, 0}, {CMD_CLEANUP_CONNECTION, 0}, {CMD_GET_NEW_SEED, 0},
    {CMD_REPORT_GAME, 368}, {CMD_FETCH_CODE_SUGGESTION, 31}, {CMD_OVERWRITE_SELECTIONS, 2 + 12},
    {CMD_GP_COMPLETE_STEP, 5}, {CMD_GP_FETCH_STEP, 1}, {CMD_REPORT_SET_COMPLETE, 1}, {CMD_GET_PLAYER_SETTINGS, 0}, {CMD_REPORT_MATCH_STATUS_UPDATE, 1},
    {CMD_LOG_MESSAGE, 0xFFFF}, {CMD_FILE_LENGTH, 0x40}, {CMD_FILE_LOAD, 0x40}, {CMD_GCT_LENGTH, 0}, {CMD_GCT_LOAD, 4}, {CMD_GET_DELAY, 0},
    {CMD_PLAY_MUSIC, 8}, {CMD_STOP_MUSIC, 0}, {CMD_CHANGE_MUSIC_VOLUME, 1}, {CMD_PREMADE_TEXT_LENGTH, 2}, {CMD_PREMADE_TEXT_LOAD, 2},
    {CMD_GET_RANK, 0}, {CMD_FETCH_RANK, 0}, {CMD_GET_RANK_VISIBILITY, 0},
};
// Sizes of the recording commands are configured by CMD_RECEIVE_COMMANDS at game start.
std::unordered_map<uint8_t, uint32_t> g_record_sizes;

std::vector<uint8_t> g_read_queue;
uint32_t g_gct_address = 0;
bool g_gecko_list_pending = false;   // next DMA read fetches the replay code list (playback)
uint64_t g_commands = 0;
std::string g_replay_dir = "replays";
uint8_t g_frame_delay = 2;   // Slippi Online input delay setting (frames)

inline void append_u32(std::vector<uint8_t>& q, uint32_t v) { q.push_back((uint8_t)(v >> 24)); q.push_back((uint8_t)(v >> 16)); q.push_back((uint8_t)(v >> 8)); q.push_back((uint8_t)v); }
inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

// ---- .slp replay recording (UBJSON container, exactly as CEXISlippi::writeToFile) ----
FILE* g_file = nullptr;
uint32_t g_written = 0;
int32_t g_last_frame = -123;
time_t g_start_time = 0;
std::map<uint8_t, std::map<uint8_t, uint32_t>> g_char_usage;   // player index -> internal character -> frames
std::string g_replay_path;
uint64_t g_replays_written = 0;

std::vector<uint8_t> generate_metadata() {
  std::vector<uint8_t> m({'U', 8, 'm', 'e', 't', 'a', 'd', 'a', 't', 'a', '{'});
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&g_start_time));
  std::string date(stamp);
  m.insert(m.end(), {'U', 7, 's', 't', 'a', 'r', 't', 'A', 't', 'S', 'U', (uint8_t)date.size()});
  m.insert(m.end(), date.begin(), date.end());
  m.insert(m.end(), {'U', 9, 'l', 'a', 's', 't', 'F', 'r', 'a', 'm', 'e', 'l'});
  append_u32(m, (uint32_t)g_last_frame);
  m.insert(m.end(), {'U', 7, 'p', 'l', 'a', 'y', 'e', 'r', 's', '{'});
  for (auto& [player, usage] : g_char_usage) {
    std::string idx = std::to_string(player);
    m.push_back('U'); m.push_back((uint8_t)idx.size()); m.insert(m.end(), idx.begin(), idx.end()); m.push_back('{');
    m.insert(m.end(), {'U', 5, 'n', 'a', 'm', 'e', 's', '{', '}'});
    m.insert(m.end(), {'U', 10, 'c', 'h', 'a', 'r', 'a', 'c', 't', 'e', 'r', 's', '{'});
    for (auto& [character, frames] : usage) {
      std::string cid = std::to_string(character);
      m.push_back('U'); m.push_back((uint8_t)cid.size()); m.insert(m.end(), cid.begin(), cid.end());
      m.push_back('l'); append_u32(m, frames);
    }
    m.push_back('}'); m.push_back('}');
  }
  m.push_back('}');
  m.insert(m.end(), {'U', 8, 'p', 'l', 'a', 'y', 'e', 'd', 'O', 'n', 'S', 'U', 7, 'd', 'o', 'l', 'p', 'h', 'i', 'n'});
  m.push_back('}');
  return m;
}

bool g_discard_replay = false;   // this match cannot be played back from its file (see discard_current_replay)
const char* g_discard_reason = nullptr;   // why, for the log; null is the 20XX CPUs

void close_file() {
  if (!g_file) return;
  std::fclose(g_file); g_file = nullptr;
  if (g_discard_replay) {
    g_discard_replay = false;
    std::remove(g_replay_path.c_str());
    host::log("slippi: replay not kept: %s (%s)", g_replay_path.c_str(),
              g_discard_reason ? g_discard_reason : "20XX CPUs were on, and a replay cannot hold what they do");
    g_discard_reason = nullptr;
    return;
  }
  ++g_replays_written;
  host::log("slippi: replay written: %s (%u raw bytes, last frame %d)", g_replay_path.c_str(), g_written, g_last_frame);
}

void create_file() {
  close_file();
  CreateDirectoryA(g_replay_dir.c_str(), nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%S", std::localtime(&g_start_time));
  g_replay_path = g_replay_dir + "\\Game_" + stamp + ".slp";
  g_file = std::fopen(g_replay_path.c_str(), "wb");
  if (!g_file) { host::log("slippi: cannot create %s", g_replay_path.c_str()); return; }
  const uint8_t header[] = {'{', 'U', 3, 'r', 'a', 'w', '[', '$', 'U', '#', 'l', 0, 0, 0, 0};
  std::fwrite(header, 1, sizeof header, g_file);
  g_written = 0;
  g_char_usage.clear();
  g_last_frame = -123;
}

// Diagnostic, off unless MELEE_DUMP_FIGHTERS=<first>:<last> (Slippi frame numbers) and
// MELEE_DUMP_FIGHTERS_OUT=<file>: at each post-frame event, the console-layout Fighter (0x23EC bytes,
// big-endian) of that player, one record per fighter per frame ([s32 frame][u8 port][u8 follower]
// [u16 size][bytes], header little-endian). The Source build writes its native Fighter at the same
// point; tools/fieldmap/field_diff.py compares them field by field.
void dump_fighter(const uint8_t* payload) {
  static bool init = false;
  static int first = 0, last = -1;
  static FILE* out = nullptr;
  if (!init) {
    init = true;
    const char* range = std::getenv("MELEE_DUMP_FIGHTERS");
    const char* path = std::getenv("MELEE_DUMP_FIGHTERS_OUT");
    if (range && path && std::sscanf(range, "%d:%d", &first, &last) == 2) out = std::fopen(path, "wb");
  }
  if (!out) return;
  const int32_t frame = (int32_t)be32(payload + 1);
  if (frame < first || frame > last) return;
  const uint8_t port = payload[5], follower = payload[6];
  const uint32_t slot = 0x80453080u + port * 0xE90u + 0xB0u + 4u * follower;
  auto rd = [](uint32_t a) -> uint32_t {
    const uint32_t off = a - 0x80000000u;
    return off + 4 <= host::ram_size ? be32(host::ram + off) : 0;
  };
  const uint32_t gobj = rd(slot);
  const uint32_t fp = gobj ? rd(gobj + 0x2C) : 0;
  const uint16_t size = 0x23EC;
  if (!fp || fp - 0x80000000u + size > host::ram_size) return;
  uint8_t head[8];
  std::memcpy(head, &frame, 4);
  head[4] = port;
  head[5] = follower;
  std::memcpy(head + 6, &size, 2);
  std::fwrite(head, 1, 8, out);
  std::fwrite(host::ram + (fp - 0x80000000u), 1, size, out);
  // Bone record (port | 0x80): per part rotate, scale, translate, world matrix, 22 floats written
  // little-endian like the Source build. Parts: fp+0x5E8, 0x10 each, joint at +0; count from
  // ftPartsTable[kind]->parts_num (+8); JObj rotate +0x1C .. mtx +0x44..+0x74.
  const uint32_t parts = rd(fp + 0x5E8), table = rd(0x804D6544u);
  const uint32_t entry = table ? rd(table + 4u * rd(fp + 0x4)) : 0;
  uint32_t n = entry ? rd(entry + 8) : 0;
  if (parts && n) {
    if (n > 96) n = 96;
    std::vector<uint32_t> bones(n * 22, 0);
    for (uint32_t i = 0; i < n; ++i) {
      const uint32_t j = rd(parts + i * 0x10);
      if (!j) continue;
      for (uint32_t k = 0; k < 22; ++k) bones[i * 22 + k] = rd(j + 0x1C + 4 * k);
    }
    const uint16_t bsize = (uint16_t)(n * 22 * 4);
    head[4] = (uint8_t)(port | 0x80);
    std::memcpy(head + 6, &bsize, 2);
    std::fwrite(head, 1, 8, out);
    std::fwrite(bones.data(), 1, bsize, out);
  }
  std::fflush(out);
}

// The replay's copy of the code list is the table without its 00D0C0DE header. It is rebuilt as
// Slippi's codes plus the optional codes that are on, then FF000000 00000000, then zeros to the same
// length. Only this build's own table is rebuilt: every line of Slippi's part must have the same
// header and size as the table served (the words inside the caves are not compared: the applier
// writes their return branches, and some caves rewrite their own data while the game runs, which a
// Slippi Dolphin recording carries the same way).
std::vector<uint8_t> g_held_code_chunks;   // the list's 0x10 chunks, until the last one arrives
bool rebuild_recorded_codes(std::vector<uint8_t>& list) {
  constexpr uint32_t kHeader = 8;
  const uint32_t fixed_end = gecko::optional_gct_offset, suffix = gecko::port_gct_offset;
  if (fixed_end <= kHeader || suffix < fixed_end || suffix > gecko::slippi_gct_size || list.size() + kHeader < suffix) return false;
  const uint8_t* t = gecko::slippi_gct;
  for (uint32_t off = kHeader; off + 8 <= fixed_end;) {
    const uint32_t a = be32(t + off), b = be32(t + off + 4), type = (a >> 24) & 0xFEu;
    uint64_t span = 8;
    if (type == 0xC0 || type == 0xC2) span = 8 + (uint64_t)b * 8;
    else if (type == 0x06) span = 8 + (((uint64_t)b + 7) & ~7ull);
    else if (type == 0x08) span = 16;
    if (span > fixed_end - off) return false;
    const uint8_t* got = list.data() + (off - kHeader);
    if (be32(got) != a) return false;
    if ((type == 0xC0 || type == 0xC2 || type == 0x06 || type == 0x08) && be32(got + 4) != b) return false;
    off += (uint32_t)span;
  }
  std::vector<uint8_t> out(list.begin(), list.begin() + (fixed_end - kHeader));
  unsigned kept = 0;
  for (size_t i = 0; i < gecko::optional_codes_count; ++i) {
    const gecko::OptionalCode& code = gecko::optional_codes[i];
    if (code.size < 8 || code.size % 8 || code.offset < fixed_end || code.offset + code.size > suffix) continue;
    const uint8_t* p = list.data() + (code.offset - kHeader);
    const uint32_t first = be32(p);
    if (first == 0xE0000000u || (first >> 16) == 0x6620u) continue;   // switched off
    out.insert(out.end(), p, p + code.size);
    ++kept;
  }
  const uint8_t end_line[8] = {0xFF, 0, 0, 0, 0, 0, 0, 0};
  out.insert(out.end(), end_line, end_line + 8);
  if (out.size() > list.size()) return false;
  const size_t left_out = list.size() - out.size();
  out.resize(list.size(), 0);
  list.swap(out);
  host::log("slippi: replay code list: Slippi's codes and %u optional codes that are on; %zu bytes of switched-off and PC-only codes left out",
            kept, left_out);
  return true;
}

void write_to_file(const uint8_t* payload, uint32_t length, const char* option) {
  // Every recording event passes through here, whether or not a replay file is open, which is
  // exactly the stream the Lab view draws from. It only reads the bytes.
  // Lab view is hidden for now (see kLabViewAvailable in pc_settings.cpp), so it is not fed either:
  // no event parsing and no silhouette loading while nothing can draw it.
  constexpr bool kLabViewFeed = false;
  if (kLabViewFeed) lab::feed(payload, length);
  if (std::strcmp(option, "create") == 0) { g_held_code_chunks.clear(); create_file(); }
  if (!g_file) return;
  if (length > 0 && payload[0] == CMD_RECEIVE_POST_FRAME_UPDATE && length >= 8) {
    g_last_frame = (int32_t)be32(payload + 1);
    g_char_usage[payload[5]][payload[7]] += 1;
    dump_fighter(payload);
  }
  // The code list. The game sends its table from RAM after the applier ran, through the message
  // splitter (0x10 events carrying 0x3D, 512 bytes each) or, from other builds, as one 0x3D event. A
  // stock replay viewer runs that list, so it holds Slippi's codes and the optional codes that are on,
  // and ends before the PC-only hooks (PAL stock icons, screen shake: their gates exist only here).
  // Held chunks are written once the list is complete, each at its own size.
  constexpr uint32_t kSplitEvent = 1 + 512 + 2 + 1 + 1;   // command, data, used size, carried command, last
  if (length == kSplitEvent && payload[0] == 0x10 && payload[515] == CMD_GECKO_LIST) {
    g_held_code_chunks.insert(g_held_code_chunks.end(), payload, payload + length);
    if (!payload[516]) return;   // more chunks follow
    std::vector<uint8_t> list;
    auto used = [&](size_t e) { return std::min<uint32_t>(512u, ((uint32_t)g_held_code_chunks[e + 513] << 8) | g_held_code_chunks[e + 514]); };
    for (size_t e = 0; e + kSplitEvent <= g_held_code_chunks.size(); e += kSplitEvent)
      list.insert(list.end(), g_held_code_chunks.begin() + e + 1, g_held_code_chunks.begin() + e + 1 + used(e));
    if (rebuild_recorded_codes(list)) {
      size_t at = 0;
      for (size_t e = 0; e + kSplitEvent <= g_held_code_chunks.size(); e += kSplitEvent) {
        std::memcpy(g_held_code_chunks.data() + e + 1, list.data() + at, used(e));
        at += used(e);
      }
    }
    std::fwrite(g_held_code_chunks.data(), 1, g_held_code_chunks.size(), g_file);
    g_written += (uint32_t)g_held_code_chunks.size();
    g_held_code_chunks.clear();
    return;
  }
  std::vector<uint8_t> recorded;
  if (length > 1 && payload[0] == CMD_GECKO_LIST) {
    std::vector<uint8_t> list(payload + 1, payload + length);
    if (rebuild_recorded_codes(list)) {
      recorded.assign(payload, payload + 1);
      recorded.insert(recorded.end(), list.begin(), list.end());
      payload = recorded.data();
    }
  }
  std::fwrite(payload, 1, length, g_file);
  g_written += length;
  if (std::strcmp(option, "close") == 0) {
    std::vector<uint8_t> closing = generate_metadata();
    closing.push_back('}');
    std::fwrite(closing.data(), 1, closing.size(), g_file);
    uint8_t size_bytes[4] = {(uint8_t)(g_written >> 24), (uint8_t)(g_written >> 16), (uint8_t)(g_written >> 8), (uint8_t)g_written};
    std::fseek(g_file, 11, SEEK_SET);
    std::fwrite(size_bytes, 1, 4, g_file);
    close_file();
  }
}

void configure_commands(const uint8_t* payload, uint8_t length) {
  // payload[0] is this command's own size byte; then (command, u16 size) triples.
  for (uint32_t i = 1; i + 2 < (uint32_t)length + 1; i += 3) {
    uint8_t cmd = payload[i];
    uint32_t size = ((uint32_t)payload[i + 1] << 8) | payload[i + 2];
    g_record_sizes[cmd] = size;
  }
  host::log("slippi: recording command sizes configured (%zu commands)", g_record_sizes.size());
}

// Optional codes share the tail with the always-installed port codes. Rebuild that tail whenever
// a setting changes so each code can be enabled independently without moving the fixed base.
bool optional_enabled(const char* flag) {
  if (!flag) return false;
  if (std::strcmp(flag, "widescreen") == 0) return gecko::option_widescreen;
  if (std::strcmp(flag, "lagless_fod") == 0) return gecko::option_lagless_fod;
  return false;
}

inline void put_be32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

// Slippi's in-game applier (in its boot codes: the walker at 80002CC8 and its callback at 80002A64,
// read from their disassembly). A line's type is (word0 >> 24) & 0xFE. C0 and C2 lines span 8 + 8n
// bytes, 06 spans 8 plus its byte count rounded up to 8, 08 spans 16, a line whose top nibble is F
// with a zero second word ends the table, and any other line (a 66 goto or an E0 included) is stepped
// over as one line. Only 04 (the word), 06 (the bytes) and C2 ("b cave" at the hook, "b hook+4" as
// the cave's last word) write anything. visit(type, line offset, address, n) for each writing line.
template <class Visit>
void walk_applier(const uint8_t* table, uint32_t begin, uint32_t end, Visit&& visit) {
  for (uint32_t off = begin; off + 8 <= end;) {
    const uint32_t a = be32(table + off), b = be32(table + off + 4);
    const uint32_t type = (a >> 24) & 0xFEu;
    uint64_t span = 8;
    if (type == 0xC0 || type == 0xC2) span = 8 + (uint64_t)b * 8;
    else if (type == 0x06) span = 8 + (((uint64_t)b + 7) & ~7ull);
    else if (type == 0x08) span = 16;
    else if (type != 0x04 && (a >> 28) == 0xF && b == 0) return;
    if (span > end - off) return;
    if (type == 0x04 || type == 0x06 || type == 0xC2) visit(type, off, 0x80000000u | (a & 0x01FFFFFFu), b);
    off += (uint32_t)span;
  }
}
// The two words the applier writes for the C2 line at `off` in a table at `base`, computed as it does.
inline uint32_t c2_hook_word(uint32_t base, uint32_t off, uint32_t hook) { return 0x48000000u | (((base + off + 8) - hook) & 0x03FFFFFCu); }
inline uint32_t c2_return_word(uint32_t base, uint32_t off, uint32_t hook, uint32_t n) {
  const uint32_t last_line = base + off + n * 8;
  return ((((hook + 4) - last_line) & 0x03FFFFFCu) | 0x48000000u) - 4u;
}

// MELEE_OPTIONAL_GOTO=1: a switched-off code keeps the 0.8.1 form, a goto in its first line only.
bool optional_goto_form() {
  static const bool on = [] { const char* v = std::getenv("MELEE_OPTIONAL_GOTO"); return v && *v == '1'; }();
  return on;
}

// Optional code i in a table at `table` (guest address `base`): its own lines when on (with each C2
// cave's return branch when `returns`, as the applier leaves them in RAM), and when off one
// E0000000 00000000 per line. The applier steps over those one line at a time and writes nothing (a
// goto in the first line alone is stepped over too, and every later line of the code was still
// installed); a Gecko code handler reads them as terminators that change nothing.
void write_optional_region(uint8_t* table, uint32_t base, size_t i, bool on, bool returns) {
  const gecko::OptionalCode& code = gecko::optional_codes[i];
  uint8_t* p = table + code.offset;
  std::memcpy(p, gecko::slippi_gct + code.offset, code.size);
  if (on) {
    if (returns)
      walk_applier(gecko::slippi_gct, code.offset, code.offset + code.size, [&](uint32_t type, uint32_t off, uint32_t hook, uint32_t n) {
        if (type == 0xC2 && n) put_be32(table + off + n * 8 + 4, c2_return_word(base, off, hook, n));
      });
    return;
  }
  if (optional_goto_form()) { put_be32(p, 0x66200000u | ((code.size / 8 - 1) & 0xFFFFu)); put_be32(p + 4, 0); return; }
  for (uint32_t k = 0; k < code.size; k += 8) { put_be32(p + k, 0xE0000000u); put_be32(p + k + 4, 0); }
}

bool optional_code_valid(const gecko::OptionalCode& code) {
  return code.size >= 8 && code.size % 8 == 0 && code.offset >= gecko::optional_gct_offset && code.offset + code.size <= gecko::slippi_gct_size;
}

// Every code stays at the offset it was translated for: the recompiled caves read their constants
// from those exact guest addresses. Packing the enabled codes together moved widescreen onto Lagless
// FoD's slot whenever Lagless was off, so its caves read garbage and 16:9 smeared the whole picture.
void rebuild_optional_codes(uint8_t* table) {
  const uint32_t start = gecko::optional_gct_offset;
  if (start + 8 > gecko::slippi_gct_size) return;
  std::memcpy(table + start, gecko::slippi_gct + start, gecko::slippi_gct_size - start);
  for (size_t i = 0; i < gecko::optional_codes_count; ++i)
    if (!optional_enabled(gecko::optional_codes[i].flag) && optional_code_valid(gecko::optional_codes[i]))
      write_optional_region(table, 0, i, false, false);
}

// What each optional code installs when the applier runs it, and the words that were there before,
// taken as the game loads the table (the applier has not run yet). On a mod disc, switching a code
// during a session puts either side back, so code running from RAM follows the switch as the
// compiled code does.
struct OptionalInstall { uint32_t addr; std::vector<uint8_t> installed, original; };
std::vector<std::vector<OptionalInstall>> g_optional_installs;   // per gecko::optional_codes entry
std::vector<int8_t> g_optional_live;                             // the table in RAM: 1 on, 0 off, -1 unknown

void record_optional_installs(uint32_t base) {
  g_optional_installs.assign(gecko::optional_codes_count, {});
  g_optional_live.assign(gecko::optional_codes_count, -1);
  for (size_t i = 0; i < gecko::optional_codes_count; ++i) {
    const gecko::OptionalCode& code = gecko::optional_codes[i];
    if (!optional_code_valid(code)) continue;
    walk_applier(gecko::slippi_gct, code.offset, code.offset + code.size, [&](uint32_t type, uint32_t off, uint32_t addr, uint32_t n) {
      OptionalInstall w{addr, {}, {}};
      if (type == 0x04) w.installed.assign(gecko::slippi_gct + off + 4, gecko::slippi_gct + off + 8);
      else if (type == 0x06) w.installed.assign(gecko::slippi_gct + off + 8, gecko::slippi_gct + off + 8 + n);
      else { w.installed.resize(4); put_be32(w.installed.data(), c2_hook_word(base, off, addr)); }
      const uint8_t* now = host::try_ptr(addr, (uint32_t)w.installed.size());
      if (!now) return;
      w.original.assign(now, now + w.installed.size());
      g_optional_installs[i].push_back(std::move(w));
    });
    g_optional_live[i] = optional_enabled(code.flag) ? 1 : 0;
  }
}

std::atomic<int> g_widescreen_request{-1};
std::atomic<int> g_fod_reflections_request{-1};

void apply_optional_codes() {
  // Clean mode: the disc's code is the only code in RAM. Writing an optional code's words (or the
  // retail words it replaces) would overwrite the disc's own changes at those addresses.
  if (host::mod_clean_mode()) return;
  // Only the switched codes' lines change in RAM: the rest of the table keeps the return branches the
  // applier wrote into its caves (copying the whole table back erased them, and a cave running from
  // RAM on a mod disc then ran into a zero word).
  if (g_gct_address && g_optional_live.size() == gecko::optional_codes_count) {
    uint8_t* table = host::ptr(g_gct_address, (uint32_t)gecko::slippi_gct_size);
    const bool mirror = host::mod_disc_active() && host::mod_reference_from_table() && gecko::gct_base_used == g_gct_address;
    for (size_t i = 0; i < gecko::optional_codes_count; ++i) {
      const gecko::OptionalCode& code = gecko::optional_codes[i];
      if (!optional_code_valid(code)) continue;
      const bool on = optional_enabled(code.flag);
      if (g_optional_live[i] == (on ? 1 : 0)) continue;
      write_optional_region(table, g_gct_address, i, on, true);
      host::mark_ram_write(g_gct_address + code.offset, code.size);
      g_optional_live[i] = on ? 1 : 0;
      if (!host::mod_disc_active()) continue;   // compiled code follows the flag by itself
      // Its hooks and writes, as the applier leaves them with the code on or off. A word changed by
      // something else since (the mod's own codes) is left alone.
      for (const OptionalInstall& w : g_optional_installs[i]) {
        const std::vector<uint8_t>& from = on ? w.original : w.installed;
        const std::vector<uint8_t>& to = on ? w.installed : w.original;
        uint8_t* now = host::ptr(w.addr, (uint32_t)to.size());
        if (std::memcmp(now, from.data(), from.size()) != 0) continue;
        std::memcpy(now, to.data(), to.size());
        host::mark_ram_write(w.addr, (uint32_t)to.size());
        if (mirror) host::mod_reference_set(w.addr, to.data(), (uint32_t)to.size());
      }
    }
  }
  for (size_t i = 0; i < gecko::optional_writes_count; ++i) {
    const gecko::OptionalWrite& w = gecko::optional_writes[i];
    const uint8_t* bytes = optional_enabled(w.flag) ? w.patched : w.original;
    std::memcpy(host::ptr(w.addr, w.size), bytes, w.size);
    host::mark_ram_write(w.addr, w.size);
    if (host::mod_disc_active() && host::mod_reference_from_table()) host::mod_reference_set(w.addr, bytes, w.size);
  }
}

void apply_widescreen(bool on) {
  gecko::option_widescreen = on;
  apply_optional_codes();
  host::log("slippi: widescreen 16:9 %s", on ? "on" : "off");
}

void apply_fod_reflections(bool on) {
  gecko::option_lagless_fod = !on;
  apply_optional_codes();
  host::log("slippi: Fountain of Dreams reflections %s", on ? "on" : "off");
}

void prepare_gct_length() {
  g_read_queue.clear();
  append_u32(g_read_queue, (uint32_t)gecko::slippi_gct_size);
}

bool gct_range_local(uint32_t* lo, uint32_t* hi) {
  if (!g_gct_address) return false;
  *lo = g_gct_address;
  *hi = g_gct_address + (uint32_t)gecko::slippi_gct_size;
  return true;
}

void prepare_gct_load(const uint8_t* payload) {
  g_read_queue.clear();
  g_gct_address = be32(payload);
  host::log("slippi: game loads the GCT (%zu bytes) at %08X%s", gecko::slippi_gct_size, g_gct_address,
            gecko::gct_base_used == g_gct_address ? "" : " (recompile with --gct-base to translate C0 caves at this address)");
  // A mod moved Slippi's code table (its own memory setup shifts the heap). The translated caves were
  // made for the other address, so none of them may run: Slippi's code runs from the table in RAM
  // (interpreted), and the game functions it hooks follow the hooks written into RAM.
  if (host::mod_disc_active()) ppc::add_ram_code_range(g_gct_address, g_gct_address + (uint32_t)gecko::slippi_gct_size);
  if (host::mod_disc_active() && gecko::gct_base_used && gecko::gct_base_used != g_gct_address) {
    const uint32_t lo = std::min(gecko::gct_base_used, g_gct_address);
    const uint32_t hi = std::max(gecko::gct_base_used, g_gct_address) + (uint32_t)gecko::slippi_gct_size + 0x1000;
    ppc::disable_dispatch_range(lo, hi);
    host::log("slippi: the mod moved the code table by %d bytes; Slippi's codes run from RAM",
              (int)((int64_t)g_gct_address - (int64_t)gecko::gct_base_used));
  }
  g_read_queue.insert(g_read_queue.end(), gecko::slippi_gct, gecko::slippi_gct + gecko::slippi_gct_size);
  // Slippi's heap setup asks the disc for the size of IfAll.usd by name. A mod disc that renamed
  // that file (the 20XX Hack Pack carries it as IfAl0.usd, byte for byte the same) has no such file,
  // and the game stops three frames in. The request is given the name this disc uses.
  if (host::mod_disc_active() && !host::disc_find_file("IfAll.usd", nullptr, nullptr) &&
      host::disc_find_file("IfAl0.usd", nullptr, nullptr)) {
    static const char want[] = "IfAll.usd", have[] = "IfAl0.usd";
    uint32_t renamed = 0;
    for (size_t at = 0; at + sizeof want <= g_read_queue.size(); ++at)
      if (std::memcmp(&g_read_queue[at], want, sizeof want - 1) == 0) {
        std::memcpy(&g_read_queue[at], have, sizeof have - 1);
        ++renamed;
      }
    if (renamed) host::log("slippi: this disc has IfAl0.usd in place of IfAll.usd; %u requests renamed", renamed);
  }
  rebuild_optional_codes(g_read_queue.data());
  record_optional_installs(g_gct_address);
  // A mod disc with the table where it was translated: every word the applier is about to install from
  // it is code the compiled guest already runs, so it joins the reference image, and a function it
  // touches stays compiled unless the mod changed that function too (host.cpp). A moved table keeps
  // all of Slippi's codes running from RAM (above). MELEE_MOD_REFERENCE=boot: the 0.8.1 rule.
  if (host::mod_disc_active() && host::mod_reference_from_table() && gecko::gct_base_used == g_gct_address) {
    const uint8_t* t = g_read_queue.data();
    uint32_t writes = 0;
    // Diagnostics: MELEE_MOD_REFERENCE_LINES=<from>-<to> mirrors only the lines at those table offsets
    // (finding which code a difference between the two rules comes from, by halving the range).
    uint32_t from = 8, to = (uint32_t)gecko::slippi_gct_size;
    if (const char* v = std::getenv("MELEE_MOD_REFERENCE_LINES")) {
      char* end = nullptr;
      from = (uint32_t)std::strtoul(v, &end, 0);
      if (end && *end == '-') to = (uint32_t)std::strtoul(end + 1, nullptr, 0);
      host::log("mods: reference mirrors table offsets %u to %u only", from, to);
    }
    walk_applier(t, 8, (uint32_t)gecko::slippi_gct_size, [&](uint32_t type, uint32_t off, uint32_t addr, uint32_t n) {
      if (off < from || off >= to) return;
      if (type == 0x04) writes += host::mod_reference_set(addr, t + off + 4, 4) ? 1 : 0;
      else if (type == 0x06) writes += host::mod_reference_set(addr, t + off + 8, n) ? 1 : 0;
      else { uint8_t w[4]; put_be32(w, c2_hook_word(g_gct_address, off, addr)); writes += host::mod_reference_set(addr, w, 4) ? 1 : 0; }
    });
    host::log("mods: the reference includes Slippi's served codes (%u writes into the game's code)", writes);
  }
}

void log_message(const uint8_t* payload, uint32_t max) {
  std::string s;
  for (uint32_t i = 0; i < max && payload[i]; ++i) s += (char)payload[i];
  host::log("slippi[game]: %s", s.c_str());
}

// Game files: Sys/GameFiles/GALE01/<name> served as-is, or <name>.diff (VCDIFF) applied to the
// file of the same name from the ISO. Port of SlippiGameFileLoader::LoadFile.
std::unordered_map<std::string, std::vector<uint8_t>> g_file_cache;
std::mutex g_file_cache_mutex;   // the boot-time preload thread and the simulation thread share it

bool read_whole_file(const std::string& path, std::vector<uint8_t>& out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? (size_t)n : 0);
  bool ok = n <= 0 || std::fread(out.data(), 1, out.size(), f) == out.size();
  std::fclose(f);
  return ok;
}

// Reads and patches one file. Runs without the cache lock held: the preload worker must never
// make the simulation thread wait behind a multi-megabyte read plus VCDIFF.
std::vector<uint8_t> build_game_file(const std::string& name) {
  std::vector<uint8_t> out;
  std::string base = host::options.sys_dir + "/GameFiles/GALE01/" + name;
  std::vector<uint8_t> blob;
  if (name != "MxDt.dat" && read_whole_file(base, blob)) {
    out = std::move(blob);
    host::log("slippi: served %s (%zu bytes)", name.c_str(), out.size());
    return out;
  }
  if (read_whole_file(base + ".diff", blob)) {
    uint32_t off = 0, size = 0;
    std::vector<uint8_t> source;
    if (host::disc_find_file(name, &off, &size)) {
      source.resize(size);
      if (!host::disc_read(off, source.data(), size)) source.clear();
    }
    std::string err;
    if (source.empty() || !host::vcdiff_decode(source.data(), source.size(), blob.data(), blob.size(), out, &err)) {
      host::log("slippi: cannot apply %s.diff (%s)", name.c_str(), source.empty() ? "file not on disc" : err.c_str());
      out.clear();
    } else {
      host::log("slippi: served %s (%zu bytes from ISO + %zu byte diff)", name.c_str(), out.size(), blob.size());
    }
    return out;
  }
  host::log("slippi: game file %s not found in %s", name.c_str(), host::options.sys_dir.c_str());
  return out;
}

const std::vector<uint8_t>& load_game_file(const std::string& name) {
  {
    std::lock_guard<std::mutex> lock(g_file_cache_mutex);   // node-based map: element references survive later inserts
    auto it = g_file_cache.find(name);
    if (it != g_file_cache.end()) return it->second;
  }
  host::SimCostScope cost(host::SIM_EXI);
  std::vector<uint8_t> built = build_game_file(name);
  std::lock_guard<std::mutex> lock(g_file_cache_mutex);
  return g_file_cache.emplace(name, std::move(built)).first->second;   // a racing preload already inserted: keep that copy
}

void prepare_file(const uint8_t* payload, bool load) {
  g_read_queue.clear();
  std::string name((const char*)payload, strnlen((const char*)payload, 0x40));
  const std::vector<uint8_t>& data = load_game_file(name);
  if (!load) append_u32(g_read_queue, (uint32_t)data.size());
  else g_read_queue.insert(g_read_queue.end(), data.begin(), data.end());
}


}  // namespace

// Every game file the Sys folder can serve is read and patched on a worker at boot, so the first
// request from the game (menus, CSS) is a cache hit instead of a multi-megabyte read plus VCDIFF
// on the simulation thread.
static void preload_game_files() {
  if (host::mod_clean_mode()) return;   // the game never asks: Slippi's file loader is not in it
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::path(host::options.sys_dir) / "GameFiles" / "GALE01";
  std::vector<std::string> names;
  for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    std::string name = entry.path().filename().string();
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".diff") == 0) name.resize(name.size() - 5);
    names.push_back(name);
  }
  std::thread([names] { for (const auto& n : names) load_game_file(n); }).detach();
}

// The same bytes the EXI file commands serve, for the Source Port's system-file layer. Built
// directly (not through the Legacy cache): the native host calls it once per file at boot.
std::vector<uint8_t> system_game_file(const std::string& name) { return build_game_file(name); }

// Every place Slippi's main list writes when the game's applier installs it, with all optional codes
// on, plus the optional single writes. Clean mode uses it to find the functions that have Slippi's
// code compiled in.
void for_each_served_code_write(const std::function<void(uint32_t addr, uint32_t size)>& visit) {
  walk_applier(gecko::slippi_gct, 8, (uint32_t)gecko::slippi_gct_size, [&](uint32_t type, uint32_t, uint32_t addr, uint32_t n) {
    visit(addr, type == 0x06 ? n : 4u);
  });
  for (size_t i = 0; i < gecko::optional_writes_count; ++i) visit(gecko::optional_writes[i].addr, gecko::optional_writes[i].size);
}

void init() { g_read_queue.reserve(64 * 1024); g_replay_dir = host::options.replay_dir; preload_game_files(); online::init(); }
void request_widescreen(bool on) { g_widescreen_request.store(on ? 1 : 0); }
bool widescreen() { return gecko::option_widescreen; }
void request_fod_reflections(bool on) { g_fod_reflections_request.store(on ? 1 : 0); }
void poll_options() {
  // Everything below acts on the translated guest: the optional codes live in its code table and
  // practice matchmaking drives its scene state at console addresses. The native game keeps both
  // in its own image, so the Source Port skips the rest of the poll. Practice matchmaking runs on
  // both: natively through the game's practice bridge (set by the Source Port host).
  if (host::game_image) {
    // The native game carries Slippi widescreen as C and reads this flag as each camera loads.
    const int wide = g_widescreen_request.exchange(-1);
    if (wide >= 0 && (wide != 0) != gecko::option_widescreen) {
      gecko::option_widescreen = wide != 0;
      host::log("slippi: widescreen 16:9 %s (from the next screen)", gecko::option_widescreen ? "on" : "off");
    }
    native_practice::tick();
    return;
  }
  int r = g_widescreen_request.exchange(-1);
  if (r >= 0 && (r != 0) != gecko::option_widescreen) apply_widescreen(r != 0);
  int fod = g_fod_reflections_request.exchange(-1);
  if (fod >= 0 && (fod != 0) == gecko::option_lagless_fod) apply_fod_reflections(fod != 0);
  // One line per game mode change. Melee routes every mode through the state machine at 0x80479D30
  // (gm_1A3F.c) whose first byte is routingInfo::curr_mode. Logging it costs one read per retrace
  // and answers "what were you doing when that happened" on a report without having to ask.
  static uint8_t last_mode = 0xFF;
  if (const uint8_t now = host::rd8(0x80479D30); now != last_mode) {
    last_mode = now;
    host::log("game mode: 0x%02X", now);
  }
  native_practice::tick();
}
void shutdown() { if (g_file) { uint8_t empty[1]; write_to_file(empty, 0, "close"); } native_practice::shutdown(); online::shutdown(); }
uint64_t replays_written() { return g_replays_written; }
uint32_t gct_load_address() { return g_gct_address; }
uint64_t commands_seen() { return g_commands; }
const std::string& replay_directory() { return g_replay_dir; }
const std::string& last_replay_path() { return g_replay_path; }
bool recording() { return g_file != nullptr; }

void imm_write(uint32_t, uint32_t) {}
uint32_t imm_read(uint32_t) { return 0; }

void dma_write(uint32_t addr, uint32_t size) {
  const uint8_t* mem = host::ptr(addr, size);
  uint32_t loc = 0;
  uint8_t byte = mem[0];
  if (byte == CMD_RECEIVE_COMMANDS) {
    std::time(&g_start_time);
    uint8_t len = mem[1];
    configure_commands(&mem[1], len);
    write_to_file(&mem[0], len + 1, "create");
    loc += len + 1;
    ++g_commands;
  }
  if (byte == CMD_MENU_FRAME) { ++g_commands; return; }
  uint8_t prev = 0;
  while (loc < size) {
    byte = mem[loc];
    uint32_t payload = 0;
    auto fixed = g_payload_sizes.find(byte);
    auto rec = g_record_sizes.find(byte);
    if (fixed != g_payload_sizes.end()) payload = fixed->second;
    else if (rec != g_record_sizes.end()) payload = rec->second;
    else { host::log("slippi: invalid command byte %02X (previous %02X)", byte, prev); return; }
    ++g_commands;
    switch (byte) {
      case CMD_GCT_LENGTH: prepare_gct_length(); break;
      case CMD_GCT_LOAD: prepare_gct_load(&mem[loc + 1]); break;
      case CMD_LOG_MESSAGE: log_message(&mem[loc + 1], size - loc - 1); break;
      case CMD_FILE_LENGTH: prepare_file(&mem[loc + 1], false); break;
      case CMD_FILE_LOAD: prepare_file(&mem[loc + 1], true); break;
      case CMD_PREMADE_TEXT_LENGTH: g_read_queue.clear(); append_u32(g_read_queue, 0); break;
      case CMD_PREMADE_TEXT_LOAD: g_read_queue.clear(); break;
      case CMD_PLAY_MUSIC: jukebox::start_song(be32(&mem[loc + 1]), be32(&mem[loc + 5])); break;
      case CMD_STOP_MUSIC: jukebox::stop(); break;
      case CMD_CHANGE_MUSIC_VOLUME: jukebox::set_melee_volume(mem[loc + 1]); break;
      case CMD_RECEIVE_COMMANDS: break;   // handled above
      case CMD_RECEIVE_GAME_END: write_to_file(&mem[loc], payload + 1, "close"); break;
      case CMD_FRAME_BOOKEND: write_to_file(&mem[loc], payload + 1, ""); break;
      case CMD_PREPARE_REPLAY: playback::prepare_game_info(&mem[loc + 1], g_read_queue); break;
      case CMD_READ_FRAME: playback::prepare_frame_data(&mem[loc + 1], g_read_queue); break;
      case CMD_IS_STOCK_STEAL: playback::prepare_is_stock_steal(&mem[loc + 1], g_read_queue); break;
      case CMD_IS_FILE_READY: playback::prepare_is_file_ready(g_read_queue); break;
      case CMD_GET_GECKO_CODES: playback::prepare_gecko_codes(g_read_queue); g_gecko_list_pending = true; break;
      case CMD_ONLINE_INPUTS: case CMD_CAPTURE_SAVESTATE: case CMD_LOAD_SAVESTATE: case CMD_GET_MATCH_STATE: case CMD_FIND_OPPONENT:
      case CMD_SET_MATCH_SELECTIONS: case CMD_OPEN_LOGIN: case CMD_LOGOUT: case CMD_UPDATE: case CMD_CLEANUP_CONNECTION:
      case CMD_SEND_CHAT_MESSAGE: case CMD_REPORT_GAME: case CMD_FETCH_CODE_SUGGESTION: case CMD_OVERWRITE_SELECTIONS:
      case CMD_GP_COMPLETE_STEP: case CMD_GP_FETCH_STEP: case CMD_REPORT_SET_COMPLETE: case CMD_REPORT_MATCH_STATUS_UPDATE: case CMD_FETCH_RANK:
      case CMD_GET_DELAY: case CMD_GET_ONLINE_STATUS: case CMD_GET_NEW_SEED: case CMD_GET_PLAYER_SETTINGS: case CMD_GET_RANK: case CMD_GET_RANK_VISIBILITY:
        online::handle(byte, &mem[loc + 1], payload, g_read_queue);
        break;
      default:
        // Recording payloads (game info, frames, items, bones...) go to the replay file.
        write_to_file(&mem[loc], payload + 1, "");
        break;
    }
    prev = byte;
    loc += payload + 1;
  }
}

void dma_read(uint32_t addr, uint32_t size) {
  if (g_gecko_list_pending) { g_gecko_list_pending = false; playback::note_gecko_list_dma(addr, size); }
  if (g_read_queue.empty()) { host::log("slippi: DMA read of %u bytes with an empty response queue", size); return; }
  g_read_queue.resize(size, 0);
  std::memcpy(host::ptr(addr, size), g_read_queue.data(), size);
  host::mark_ram_write(addr, size);
}

bool gct_range(uint32_t* lo, uint32_t* hi) { return gct_range_local(lo, hi); }
void discard_current_replay(const char* reason) { if (g_file) { g_discard_replay = true; g_discard_reason = reason; } }

}  // namespace slippi
