// The 20XX Hack Pack on the Source Port: what the host does for the pack's disc when it is loaded as
// a file overlay (--mod-iso, a profile, or Play on the launcher's Mods page).
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Source Port never runs the pack's code. The pack's features are native rewrites in the game
// (sourceport/game/shim/mu_hp.c, plan and ledgers in run-source/rel09-hackpack). The host's part:
//
//   - which of the pack's files are served. The pack's own menu files stay out (retail menus), and
//     the files that carry PowerPC code are never published at all (keeps_retail).
//   - the pack's data tables for the game. The stage swap table is a disc file. The music names,
//     the playlists and the default stage file names are data inside the pack's main.dol, so the
//     host reads those bytes from the disc image at their console address (dol_read). Bytes only:
//     nothing from that file is ever executed, and code addresses are refused.
//   - the player's choices for the pack (playlist types, stage variants, stage page), kept in the
//     settings file like 20XX TE's menu song (settings(), key hp_settings).
//   - the stage variant of the running match, carried into option word 3 for the replay.
//
// Header only, used by port/app/source_host.cpp and the settings panel (port/runtime/gx/pc_settings.cpp),
// which loads and saves the block (key hp_settings) and saves it again when the game changes it.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include "../abi/mu_host.h"   // (relative, like host.h: the settings panel includes this file too)

namespace source_port::hackpack {

// Disc paths (lower case, as the overlay stores them) that are not served from the pack.
//   Code carriers, never published: the pack's character select files hold PowerPC blocks the pack's
//   main.dol jumps into at fixed addresses, and AI_Engine.bin is PowerPC. The native game keeps
//   loading the retail MnSlChr.usd, which the pack does not have, so the retail copy is served.
//   Retail menus: the pack's main menu, extra menu, results text and event table describe features
//   only its code has (its debug menu entry, its results counters, its custom event matches).
// The pack renamed or deleted MnSlChr.usd, MnSlMap.usd, SdMenu.usd, SdSlChr.usd, IfAll.usd and
// GmTitle.usd: the native game asks for those names and gets the retail disc's copies without any
// rule here. Its MnSlMap.1sd..4sd, SdMenu.1sd..4sd, SdSlChr.1sd..4sd, IfAl0/IfAl1.usd and
// IfCom*.dat are data under names the native game never asks for.
inline bool keeps_retail(const std::string& path) {
  static const char* const kept[] = {
      "/mnslchr.0sd", "/mnslchr.1sd", "/mnslchr.2sd", "/mnslchr.3sd", "/ai_engine.bin",
      "/mnmaall.usd", "/mnextall.usd", "/sdrst.usd", "/gmevent.dat", "/opening.bnr",
  };
  for (const char* name : kept) if (path == name) return true;
  return false;
}

// The pack is recognized by its own files among a layer's: the stage swap table and the code
// carrying character select file. (The Mods page recognizes the official builds by whole-disc MD5;
// this also covers a build the player changed with the pack's own tools.)
inline bool is_pack_marker(const std::string& path) { return path == "/stageswaptable.bin"; }
inline bool is_pack_second_marker(const std::string& path) { return path == "/mnslchr.0sd"; }

struct State {
  std::mutex mutex;
  std::atomic<bool> loaded{false};
  std::filesystem::path iso;
  uint64_t table_offset = 0;        // StageSwapTable.bin inside the image
  uint32_t table_length = 0;
  bool dol_tried = false;
  std::vector<uint8_t> dol;         // the pack's main.dol, read once on first use
  uint32_t section_offset[18]{}, section_address[18]{}, section_size[18]{};
  std::atomic<uint32_t> stage_bits{0};   // MU_GAME_OPTION3_HP_STAGE_* of the match about to start
};
inline State& state() { static State s; return s; }

// The saved choices (MU_HP_SET_* in mu_host.h). All zero is the pack's own default.
struct Settings {
  std::array<std::atomic<uint8_t>, MU_HP_SETTINGS_SIZE> bytes;
  std::atomic<uint32_t> changes{0};   // bumped by a change from the game: the panel saves then
  Settings() { for (auto& b : bytes) b.store(0); }
};
inline Settings& settings() { static Settings s; return s; }

inline uint8_t clamp_setting(uint32_t index, uint8_t value) {
  if (index < MU_HP_SET_CUSTOM_SONGS) return value > 4 ? 0 : value;                 // playlist type
  if (index == MU_HP_SET_CUSTOM_SONGS) return value > 0x9E ? 0x9E : value;          // tracks 0x31..0xCE
  if (index == MU_HP_SET_GLOBAL_ON || index == MU_HP_SET_GLOBAL_MENUS) return value ? 1 : 0;
  if (index >= MU_HP_SET_LEGAL_VARIANT && index < MU_HP_SET_LEGAL_VARIANT + 6) return value > 15 ? 15 : value;
  if (index == MU_HP_SET_STAGE_PAGE) return value > 3 ? 0 : value;
  if (index == MU_HP_SET_TRAINING) return value & (MU_HP_TRAINING_ON | MU_HP_TRAINING_LOOP);
  return 0;                                                                        // spare bytes stay zero
}
// The settings file's form: 48 hex digits. Anything else leaves the defaults.
inline std::string settings_text() {
  static const char digits[] = "0123456789ABCDEF";
  std::string out;
  for (auto& b : settings().bytes) { const uint8_t v = b.load(); out += digits[v >> 4]; out += digits[v & 15]; }
  return out;
}
inline bool settings_are_default() {
  for (auto& b : settings().bytes) if (b.load()) return false;
  return true;
}
inline void settings_parse(const std::string& text) {
  if (text.size() != MU_HP_SETTINGS_SIZE * 2) return;
  auto nibble = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 :
                                    c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
  uint8_t parsed[MU_HP_SETTINGS_SIZE];
  for (uint32_t i = 0; i < MU_HP_SETTINGS_SIZE; ++i) {
    const int hi = nibble(text[i * 2]), lo = nibble(text[i * 2 + 1]);
    if (hi < 0 || lo < 0) return;
    parsed[i] = (uint8_t)(hi << 4 | lo);
  }
  for (uint32_t i = 0; i < MU_HP_SETTINGS_SIZE; ++i) settings().bytes[i].store(clamp_setting(i, parsed[i]));
}

// The loaded layer is the pack: remember where its image and its stage swap table are.
inline void attach(const std::filesystem::path& iso, uint64_t table_offset, uint32_t table_length) {
  State& s = state();
  std::lock_guard<std::mutex> lock(s.mutex);
  s.iso = iso;
  s.table_offset = table_offset;
  s.table_length = table_length;
  s.dol_tried = false;
  s.dol.clear();
  s.loaded.store(!iso.empty());
}
inline bool loaded() { return state().loaded.load(); }

namespace detail {
inline uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
inline bool read_at(std::ifstream& in, uint64_t offset, void* dst, size_t size) {
  in.clear();
  in.seekg((std::streamoff)offset);
  return in.good() && (bool)in.read((char*)dst, (std::streamsize)size);
}
// Caller holds the mutex. The whole file once (4.4 MB), then every request is a memory copy.
inline bool load_dol(State& s) {
  if (s.dol_tried) return !s.dol.empty();
  s.dol_tried = true;
  std::ifstream in(s.iso, std::ios::binary);
  uint8_t header[0x440], dol_header[0x100];
  if (!in || !read_at(in, 0, header, sizeof header)) return false;
  const uint32_t dol_offset = be32(header + 0x420);
  if (!read_at(in, dol_offset, dol_header, sizeof dol_header)) return false;
  uint32_t extent = sizeof dol_header;
  for (int i = 0; i < 18; ++i) {
    s.section_offset[i] = be32(dol_header + i * 4);
    s.section_address[i] = be32(dol_header + 0x48 + i * 4);
    s.section_size[i] = be32(dol_header + 0x90 + i * 4);
    if (s.section_size[i] > (64u << 20) || s.section_offset[i] > (64u << 20)) return false;
    extent = std::max(extent, s.section_offset[i] + s.section_size[i]);
  }
  if (extent > (64u << 20)) return false;
  s.dol.resize(extent);
  if (!read_at(in, dol_offset, s.dol.data(), extent)) { s.dol.clear(); return false; }
  return true;
}
}  // namespace detail

// Bytes of the pack's main.dol by console address, data sections only (sections 7 and up in the
// file's table): the game asks for tables and strings, and a request inside the code sections is
// refused. A range must lie inside one section.
inline bool dol_read(uint32_t address, uint8_t* dst, uint32_t size) {
  State& s = state();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.loaded.load() || !size || !detail::load_dol(s)) return false;
  for (int i = 7; i < 18; ++i) {
    const uint32_t start = s.section_address[i], length = s.section_size[i];
    if (!length || address < start || address - start >= length || size > length - (address - start)) continue;
    std::memcpy(dst, s.dol.data() + s.section_offset[i] + (address - start), size);
    return true;
  }
  return false;
}

inline bool table_read(std::vector<uint8_t>* out) {
  State& s = state();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.loaded.load() || !s.table_length || s.table_length > MU_SLIPPI_RESPONSE_CAPACITY) return false;
  std::ifstream in(s.iso, std::ios::binary);
  out->resize(s.table_length);
  if (!in || !detail::read_at(in, s.table_offset, out->data(), out->size())) { out->clear(); return false; }
  return true;
}

// Option word 3's stage bits for the host to carry (zero unless the game reported a pack stage).
inline uint32_t stage_bits() { return loaded() ? state().stage_bits.load() : 0u; }

// Command 0xFA (MU_HP_COMMAND). `can_change`: offline and not a replay; otherwise the settings and
// the stage state cannot be written. An empty reply tells the game the pack is not there.
inline void command(const uint8_t* p, uint32_t n, std::vector<uint8_t>& reply, bool can_change) {
  if (!loaded() || n < 1) return;
  auto append_settings = [&] { for (auto& b : settings().bytes) reply.push_back(b.load()); };
  switch (p[0]) {
  case MU_HP_OP_DOL_READ:
    if (n == 7) {
      const uint32_t address = detail::be32(p + 1), size = (uint32_t)p[5] << 8 | p[6];
      if (size && size <= MU_SLIPPI_RESPONSE_CAPACITY) {
        reply.resize(size);
        if (!dol_read(address, reply.data(), size)) reply.clear();
      }
    }
    break;
  case MU_HP_OP_GET_SETTINGS:
    append_settings();
    break;
  case MU_HP_OP_SET_SETTING:
    if (n == 3 && p[1] < MU_HP_SETTINGS_SIZE && can_change) {
      const uint8_t value = clamp_setting(p[1], p[2]);
      if (settings().bytes[p[1]].exchange(value) != value) settings().changes.fetch_add(1);
    }
    append_settings();
    break;
  case MU_HP_OP_STAGE_STATE:
    if (n == 5) {
      state().stage_bits.store(can_change ? detail::be32(p + 1) & MU_GAME_OPTION3_HP_STAGE_ALL : 0u);
      reply.push_back(1);
    }
    break;
  case MU_HP_OP_STAGE_TABLE:
    if (!table_read(&reply)) reply.clear();
    break;
  default:
    break;
  }
}

}  // namespace source_port::hackpack
