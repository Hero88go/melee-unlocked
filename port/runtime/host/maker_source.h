// Melee Maker on the Source Port: what the host does for its disc (--mod-iso, a profile, or Play on
// the launcher's Mods page).
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Melee Maker's code is native (sourceport/game/maker); the Source Port never runs the disc's own
// code. Its borrowed moves are posed with bone tables its build generates from the player's own
// game files and compiles into the disc's main.dol. They are game data, so they are not in this
// repository: the game reads them from the player's patched disc through command 0xEE, by console
// address, as bytes. The addresses belong to one release, so the host offers the disc only when its
// build id is that release's (kBuild).
//
// Header only, used by port/app/source_host.cpp and port/runtime/host/host.cpp (the Direct fingerprint).
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include "../abi/mu_host.h"
#include "mod_profile.h"

namespace source_port::maker {

constexpr const char kBuild[] = "1.3.8-1865b7";

struct State {
  std::mutex mutex;
  std::vector<uint8_t> dol;
  uint32_t offset[18] = {}, address[18] = {}, size[18] = {};
  bool loaded = false;
};
inline State& state() { static State s; return s; }

namespace detail {
inline uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
}

// Reads the disc's main.dol; true when it is Melee Maker's supported release.
inline bool attach(const std::filesystem::path& iso) {
  State& s = state();
  std::lock_guard<std::mutex> lock(s.mutex);
  s.loaded = false;
  std::ifstream in(iso, std::ios::binary);
  uint8_t head[4]{}, hdr[0x100]{};
  if (!in.seekg(0x420) || !in.read(reinterpret_cast<char*>(head), 4)) return false;
  const uint32_t dol_offset = detail::be32(head);
  if (!in.seekg(dol_offset) || !in.read(reinterpret_cast<char*>(hdr), sizeof hdr)) return false;
  uint32_t extent = sizeof hdr;
  for (int i = 0; i < 18; ++i) {
    s.offset[i] = detail::be32(hdr + i * 4);
    s.address[i] = detail::be32(hdr + 0x48 + i * 4);
    s.size[i] = detail::be32(hdr + 0x90 + i * 4);
    if (s.size[i]) extent = std::max(extent, s.offset[i] + s.size[i]);
  }
  if (extent > 16u * 1024u * 1024u) return false;
  s.dol.resize(extent);
  if (!in.seekg(dol_offset) || !in.read(reinterpret_cast<char*>(s.dol.data()), extent)) return false;
  const std::string mark(kBuild, sizeof kBuild);   // with its terminator
  if (std::search(s.dol.begin(), s.dol.end(), mark.begin(), mark.end()) == s.dol.end()) return false;
  s.loaded = true;
  return true;
}

inline bool loaded() { std::lock_guard<std::mutex> lock(state().mutex); return state().loaded; }

// Command 0xEE (MU_MAKER_COMMAND). 0x01 addr(4) len(2): main.dol bytes at a console address, any
// section. 0x02: the build id. An empty reply means the disc is not loaded or the range is outside.
inline void command(const uint8_t* p, uint32_t n, std::vector<uint8_t>& reply) {
  State& s = state();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.loaded || n < 1) return;
  if (p[0] == MU_MAKER_OP_DOL_READ && n == 7) {
    const uint32_t address = detail::be32(p + 1), size = uint32_t(p[5]) << 8 | p[6];
    if (!size || size > MU_SLIPPI_RESPONSE_CAPACITY) return;
    for (int i = 0; i < 18; ++i) {
      if (!s.size[i] || address < s.address[i] || address - s.address[i] >= s.size[i] ||
          size > s.size[i] - (address - s.address[i])) continue;
      const uint8_t* src = s.dol.data() + s.offset[i] + (address - s.address[i]);
      reply.assign(src, src + size);
      return;
    }
  } else if (p[0] == MU_MAKER_OP_BUILD) {
    reply.assign(kBuild, kBuild + sizeof kBuild - 1);
  }
}

// The build Direct compares (slippi_online.cpp build_verdict), the same on both engines so a Source
// Port player and a Static Recomp player on this disc can play each other: the release and the whole
// disc's SHA-256. Empty when the disc could not be read.
inline std::string fingerprint(const std::string& disc_sha256) {
  if (disc_sha256.empty()) return {};
  mods::Sha256 h;
  h.update("mu-melee-maker-v1\n");
  h.update(std::string(kBuild));
  h.update("\n" + disc_sha256);
  return h.hex();
}

}  // namespace source_port::maker
