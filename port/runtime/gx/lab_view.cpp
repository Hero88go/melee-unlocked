// Lab view: Slippi Lab style rendering of a live match. See lab_view.h.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Drawing follows Slippi Lab (MIT, github.com/frankborden/slippilab): src/state/replayStore.tsx
// (computeRenderData), src/components/viewer/{Player,Stage,Item,Camera}.tsx. Where this file
// differs, the comment says why.
#include "lab_view.h"
#include "../abi/mu_look_snapshot.h"
#include "gx_core.h"
#include "host.h"
#include "imgui.h"
#include "earcut/mapbox/earcut.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <chrono>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lab {
namespace {

constexpr float kPi = 3.14159265358979f;

// ---- Slippi event fields (offsets from the command byte, as in the .slp spec) ----
enum : uint8_t {
  EV_GAME_START = 0x36, EV_PRE_FRAME = 0x37, EV_POST_FRAME = 0x38, EV_GAME_END = 0x39,
  EV_ITEM = 0x3B, EV_FRAME_BOOKEND = 0x3C, EV_FOD_PLATFORM = 0x3F,
};

inline uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
inline int32_t bes32(const uint8_t* p) { return (int32_t)be32(p); }
inline float bef(const uint8_t* p) { uint32_t u = be32(p); float f; std::memcpy(&f, &u, 4); return f; }

struct PlayerFrame {
  bool present = false;
  int32_t frame = 0;
  uint8_t character = 0;   // internal ID
  uint16_t action = 0;
  float x = 0, y = 0, facing = 1, percent = 0, shield = 60, counter = 0;
  uint8_t stocks = 0, lcancel = 0, hurtbox = 0;
  float joy_x = 0, joy_y = 0, trigger = 0;   // from the pre-frame update
  // Derived at commit from the frames before this one (Slippi Lab's getStartOfAction).
  float start_facing = 1, start_joy_x = 0, start_joy_y = 0, start_trigger = 0;
  uint8_t start_lcancel = 0;
  float prev_x = 0, prev_y = 0;
};

struct ItemFrame {
  uint16_t type = 0;
  uint8_t state = 0, missile_type = 0, charge_level = 0;
  int8_t owner = -1;
  float facing = 1, vx = 0, vy = 0, x = 0, y = 0;
};

struct GameInfo {
  uint16_t stage = 0;
  bool teams = false;
  struct Port { bool present = false; uint8_t external = 0, team = 0, shade = 0; } ports[4];
};

// One collision line of the stage as the game has it this frame (world units, y up).
struct StageLine {
  float x0, y0, x1, y1;
  bool floor, platform;
};

struct Frame {
  int32_t number = -1000;
  PlayerFrame players[4][2];   // [port][follower]
  std::vector<ItemFrame> items;
  float fod_left = 20.0f, fod_right = 27.44186047f;   // game platform heights (Slippi Lab constants.ts)
  // The stage's live collision, read from the game at the end of the frame. Empty if it could not
  // be read, in which case the view falls back to Slippi Lab's fixed outlines.
  std::vector<StageLine> stage_lines;
  // Each fighter's hurtbox capsules this frame (world units): the stick figure and capsule looks.
  struct Capsule { float ax, ay, bx, by, r; };
  std::vector<Capsule> capsules[4][2];
  // Each fighter's skeleton this frame: one segment from every joint to its parent (world units).
  struct Bone { float ax, ay, bx, by; };
  std::vector<Bone> bones[4][2];
};

// Simulation-thread state.
Frame g_building;
constexpr int kHistory = 256;
PlayerFrame g_history[kHistory][4][2];
float g_fod_left = 20.0f, g_fod_right = 27.44186047f;

// Shared with the render thread.
std::mutex g_mutex;
GameInfo g_info;
Frame g_shown;
bool g_in_match = false;
std::atomic<int> g_fighter_style{0}, g_stage_style{0};   // set_style
ImFont* g_hud_font = nullptr;   // set_hud_font
uint64_t g_match_serial = 0;   // bumps each game start, so the camera starts fresh
std::chrono::steady_clock::time_point g_last_commit{};

// ---- Character packs (tools/build_lab_assets.py) ----
struct Anim {
  std::string name;
  std::vector<uint16_t> frames;   // path index per frame, 0xFFFF = nothing
  bool follows_facing = false, guard = false, guard_damage = false, shine = false, fly_roll = false;
};
struct Pack {
  float scale = 1, shield_x = 0, shield_y = 0, shield_size = 10;
  std::vector<uint16_t> action_anim;
  std::vector<Anim> anims;
  std::vector<std::string> paths;
  // Render thread only: each silhouette's outline and triangles in model space, built the first
  // time that frame is drawn. Facing and rotation are applied per vertex, so one mesh serves both.
  struct Mesh { std::vector<ImVec2> outline; std::vector<uint32_t> triangles; };
  std::unordered_map<uint16_t, Mesh> meshes;
};
constexpr int kCharacters = 27;
std::mutex g_pack_mutex;
std::shared_ptr<Pack> g_packs[kCharacters];
std::atomic<int> g_pack_state[kCharacters];   // 0 not requested, 1 loading, 2 ready, 3 unavailable

std::shared_ptr<Pack> load_pack(int id) {
  const std::string path = host::options.lab_dir + "/" + std::to_string(id) + ".lab";
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) { host::log("lab: no silhouettes for character %d (%s); run tools/build_lab_assets.py", id, path.c_str()); return nullptr; }
  std::vector<uint8_t> b;
  std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  b.resize(n > 0 ? (size_t)n : 0);
  const bool ok = std::fread(b.data(), 1, b.size(), f) == b.size();
  std::fclose(f);
  size_t p = 0;
  auto need = [&](size_t k) { return ok && p + k <= b.size(); };
  auto u8 = [&]() { return need(1) ? b[p++] : (uint8_t)0; };
  auto u16 = [&]() { uint16_t v = 0; if (need(2)) { v = (uint16_t)(b[p] | (b[p + 1] << 8)); p += 2; } return v; };
  auto u32 = [&]() { uint32_t v = 0; if (need(4)) { v = b[p] | (b[p + 1] << 8) | (b[p + 2] << 16) | ((uint32_t)b[p + 3] << 24); p += 4; } return v; };
  auto f32 = [&]() { uint32_t u = u32(); float v; std::memcpy(&v, &u, 4); return v; };
  if (!need(8) || std::memcmp(b.data(), "MLAB", 4) != 0) { host::log("lab: %s is not a Lab pack", path.c_str()); return nullptr; }
  p = 4;
  if (u32() != 1) { host::log("lab: %s has an unknown version; rebuild it", path.c_str()); return nullptr; }
  auto pack = std::make_shared<Pack>();
  pack->scale = f32(); pack->shield_x = f32(); pack->shield_y = f32(); pack->shield_size = f32();
  pack->action_anim.resize(u16());
  for (auto& a : pack->action_anim) a = u16();
  pack->anims.resize(u16());
  for (auto& a : pack->anims) {
    const uint8_t len = u8();
    if (need(len)) { a.name.assign((const char*)&b[p], len); p += len; }
    a.frames.resize(u16());
    for (auto& fr : a.frames) fr = u16();
    // The same name checks Slippi Lab makes per frame, done once.
    a.follows_facing = a.name.find("Jump") != std::string::npos || a.name == "SpecialHi" || a.name == "SpecialAirHi";
    a.guard = a.name == "GuardOn" || a.name == "Guard" || a.name == "GuardReflect" || a.name == "GuardDamage";
    a.guard_damage = a.name == "GuardDamage";
    a.shine = a.name.find("SpecialLw") != std::string::npos || a.name.find("SpecialAirLw") != std::string::npos;
    a.fly_roll = a.name == "DamageFlyRoll";
  }
  pack->paths.resize(u32());
  for (auto& d : pack->paths) {
    const uint16_t len = u16();
    if (need(len)) { d.assign((const char*)&b[p], len); p += len; }
  }
  if (!need(0) || p != b.size()) { host::log("lab: %s is truncated or damaged; rebuild it", path.c_str()); return nullptr; }
  host::log("lab: loaded character %d (%zu animations, %zu silhouettes)", id, pack->anims.size(), pack->paths.size());
  return pack;
}

void request_pack(int id) {
  if (id < 0 || id >= kCharacters) return;
  int expected = 0;
  if (!g_pack_state[id].compare_exchange_strong(expected, 1)) return;
  // A pack is several megabytes; reading it on the simulation thread would stall the match start.
  std::thread([id] {
    std::shared_ptr<Pack> pack = load_pack(id);
    { std::lock_guard<std::mutex> lock(g_pack_mutex); g_packs[id] = pack; }
    g_pack_state[id].store(pack ? 2 : 3);
  }).detach();
}

std::shared_ptr<Pack> get_pack(int id) {
  if (id < 0 || id >= kCharacters) return nullptr;
  if (g_pack_state[id].load() != 2) { request_pack(id); return nullptr; }
  std::lock_guard<std::mutex> lock(g_pack_mutex);
  return g_packs[id];
}

// External (character select) ID to the internal IDs a match may use, so both halves of Zelda/
// Sheik and of the Ice Climbers load up front.
const int8_t kInternalByExternal[26][2] = {
  {2, -1}, {3, -1}, {1, -1}, {24, -1}, {4, -1}, {5, -1}, {6, -1}, {17, -1}, {0, -1}, {18, -1},
  {16, -1}, {8, -1}, {9, -1}, {12, -1}, {10, 11}, {15, -1}, {13, -1}, {14, -1}, {19, 7}, {7, 19},
  {22, -1}, {20, -1}, {21, -1}, {26, -1}, {23, -1}, {25, -1},
};
constexpr uint8_t kFox = 1, kFalco = 22;

// ---- Event intake (simulation thread) ----

void begin_frame(int32_t number) {
  if (g_building.number == number) return;
  g_building.number = number;
  for (auto& port : g_building.players) for (auto& pf : port) pf.present = false;
  g_building.items.clear();
}

PlayerFrame& history(int32_t frame, int port, int follower) {
  return g_history[((frame % kHistory) + kHistory) % kHistory][port][follower];
}

// ---- Live stage collision (read only) ----
//
// Slippi Lab draws stages from outlines typed in by hand, so anything that moves or changes shape
// is either special-cased (Randall, the Fountain platforms) or missing (Pokemon Stadium's
// transformations, every other stage). The game keeps the collision it is actually using in three
// globals of mplib.c (names and addresses from the decomp's GALE01 symbols.txt, byte-matching 1.02):
//   0x804D64B4 MapCollData*  the stage's collision: +0x4 vertex count, +0xC line count
//   0x804D64B8 CollVtx*      current vertices, 0x18 bytes each, position at +0x8 (moves with the stage)
//   0x804D64BC CollLine*     per line: +0x0 MapLine* (vertex indices u16 at +0 and +2), +0x4 flags
// A line is solid when it is enabled and neither hidden (its group switched off, which is how a
// Stadium transformation swaps terrain) nor pruned as empty. Only read here, never written, and at
// the end of the frame's simulation, so it cannot affect the match.
constexpr uint32_t kCollData = 0x804D64B4, kCollVerts = 0x804D64B8, kCollLines = 0x804D64BC;
constexpr uint32_t kRamBase = 0x80000000u, kRamSize = 24u << 20;   // 24 MB of guest RAM
constexpr uint32_t kLineKind = 0xF, kLineFloor = 1 << 0, kLineEmpty = 1 << 7, kLinePlatform = 1 << 8,
                   kLineEnabled = 1 << 16, kLineHidden = 1 << 18;

bool guest_span(uint32_t addr, uint32_t bytes) {
  return host::ram && addr >= kRamBase && bytes <= kRamSize && addr - kRamBase <= kRamSize - bytes;
}
uint32_t guest32(uint32_t addr) { return be32(host::ram + (addr - kRamBase)); }
uint16_t guest16(uint32_t addr) { return be16(host::ram + (addr - kRamBase)); }
float guestf(uint32_t addr) { return bef(host::ram + (addr - kRamBase)); }

void read_stage_lines(std::vector<StageLine>& out) {
  out.clear();
  if (!guest_span(kCollData, 12)) return;
  const uint32_t coll = guest32(kCollData), verts = guest32(kCollVerts), lines = guest32(kCollLines);
  if (!guest_span(coll, 0x10)) return;
  const uint32_t vert_count = guest32(coll + 0x4), line_count = guest32(coll + 0xC);
  // The game's own array sizes (groundCollVtx_count, groundCollLine_count in mplib.c).
  if (vert_count == 0 || vert_count > 2048 || line_count == 0 || line_count > 1536) return;
  if (!guest_span(verts, vert_count * 0x18) || !guest_span(lines, line_count * 8)) return;
  for (uint32_t i = 0; i < line_count; ++i) {
    const uint32_t line = guest32(lines + 8 * i), flags = guest32(lines + 8 * i + 4);
    if (!(flags & kLineEnabled) || (flags & (kLineHidden | kLineEmpty)) || !(flags & kLineKind)) continue;
    if (!guest_span(line, 4)) continue;
    const uint16_t a = guest16(line), b = guest16(line + 2);
    if (a >= vert_count || b >= vert_count) continue;
    const uint32_t va = verts + 0x18 * a + 8, vb = verts + 0x18 * b + 8;
    StageLine l{guestf(va), guestf(va + 4), guestf(vb), guestf(vb + 4), (flags & kLineFloor) != 0, (flags & kLinePlatform) != 0};
    if (!std::isfinite(l.x0) || !std::isfinite(l.y0) || !std::isfinite(l.x1) || !std::isfinite(l.y1)) continue;
    out.push_back(l);
  }
}

// ---- Hurtbox capsules (read only) ----
// player_slots (0x80453080, 0xE90 bytes a player): +0xB0 the fighter entity, +0xB4 the follower
// (Nana). The entity's user data (+0x2C) is the Fighter: +0x119E the capsule count, +0x11A0 the
// capsules, 0x4C bytes each: scale at +0x1C, world a_pos at +0x28, b_pos at +0x34. The game updates
// them every frame where it checks hits, so they are exactly where the fighter is.
constexpr uint32_t kPlayerSlots = 0x80453080, kPlayerStride = 0xE90;

// The fighter's joint tree (read only): the entity's root joint (+0x28), each HSD_JObj's next (+0x08),
// parent (+0x0C) and child (+0x10), and its world matrix (+0x44, translation x at +0x50, y at +0x60)
// as the game set it up for this frame's drawing. The first four levels (top, translation, the two
// rotation joints, and what hangs off them, one of which stands at the feet) would draw a line from
// the hip to the ground, so their segments are left out. Segments under 0.6 units (joints on top of
// each other, the fingers) are left out too, so a hand reads as one hand.
void read_skeleton(uint32_t entity, std::vector<Frame::Bone>& out) {
  out.clear();
  struct Item { uint32_t jobj; int depth; };
  Item stack[192];
  int top = 0, seen = 0;
  const uint32_t root = guest_span(entity, 0x30) ? guest32(entity + 0x28) : 0;
  if (guest_span(root, 0x88)) stack[top++] = {root, 0};
  while (top > 0 && seen++ < 192) {
    const Item it = stack[--top];
    const uint32_t next = guest32(it.jobj + 0x08), child = guest32(it.jobj + 0x10), parent = guest32(it.jobj + 0x0C);
    if (next && top < 192 && guest_span(next, 0x88)) stack[top++] = {next, it.depth};
    if (child && top < 192 && guest_span(child, 0x88)) stack[top++] = {child, it.depth + 1};
    static const bool dump = std::getenv("MELEE_TEST_LAB_BONES") != nullptr;   // tests: the joints of one frame
    static int dumped = 0;
    if (dump && dumped < 200 && g_building.number >= 0)
      ++dumped, host::log("lab bones: jobj %08X depth %d at %.1f %.1f parent %08X", it.jobj, it.depth,
                          guestf(it.jobj + 0x50), guestf(it.jobj + 0x60), parent);
    if (it.depth < 5 || !guest_span(parent, 0x88)) continue;
    const Frame::Bone b{guestf(parent + 0x50), guestf(parent + 0x60), guestf(it.jobj + 0x50), guestf(it.jobj + 0x60)};
    if (!std::isfinite(b.ax) || !std::isfinite(b.ay) || !std::isfinite(b.bx) || !std::isfinite(b.by)) continue;
    const float len = std::hypot(b.bx - b.ax, b.by - b.ay);
    if (len > 0.6f && len < 30.0f) out.push_back(b);
  }
}

void read_capsules(Frame& f) {
  for (int port = 0; port < 4; ++port)
    for (int fol = 0; fol < 2; ++fol) {
      auto& out = f.capsules[port][fol];
      out.clear();
      f.bones[port][fol].clear();
      const uint32_t slot = kPlayerSlots + kPlayerStride * port + 0xB0 + 4 * fol;
      if (!guest_span(slot, 4)) continue;
      const uint32_t entity = guest32(slot);
      if (!guest_span(entity, 0x30)) continue;
      const uint32_t fp = guest32(entity + 0x2C);
      if (!guest_span(fp, 0x11A0 + 15 * 0x4C)) continue;
      const uint32_t n = host::ram[fp + 0x119E - kRamBase];
      for (uint32_t i = 0; i < n && i < 15; ++i) {
        const uint32_t c = fp + 0x11A0 + 0x4C * i;
        Frame::Capsule cap{guestf(c + 0x28), guestf(c + 0x2C), guestf(c + 0x34), guestf(c + 0x38), guestf(c + 0x1C)};
        const uint32_t bone = guest32(c + 0x20);
        if (guest_span(bone, 0x88)) {
          float matrix[3][4];
          for (unsigned row = 0; row < 3; ++row) for (unsigned col = 0; col < 4; ++col)
            matrix[row][col] = guestf(bone + 0x44 + 16 * row + 4 * col);
          cap.r = mu_look_world_radius(cap.r, matrix);
        }
        if (!std::isfinite(cap.ax) || !std::isfinite(cap.ay) || !std::isfinite(cap.bx) || !std::isfinite(cap.by) ||
            !std::isfinite(cap.r) || cap.r <= 0 || cap.r > 50) continue;
        out.push_back(cap);
      }
      read_skeleton(entity, f.bones[port][fol]);
    }
}

void commit(bool native = false) {
  const int32_t f = g_building.number;
  for (int port = 0; port < 4; ++port) {
    for (int fol = 0; fol < 2; ++fol) {
      PlayerFrame& cur = g_building.players[port][fol];
      if (!cur.present) continue;
      cur.frame = f;
      history(f, port, fol) = cur;
      // Walk back to the first frame of this action: same action ID, and the frame counter never
      // went up going backwards. Capped at the history length (only facing and a few start-of-action
      // inputs depend on it; a four-second Guard showing its first facing is not worth more memory).
      PlayerFrame start = cur;
      for (int k = 1; k < kHistory - 1; ++k) {
        const PlayerFrame& h = history(f - k, port, fol);
        if (!h.present || h.frame != f - k || h.action != start.action || h.counter > start.counter) break;
        start = h;
      }
      cur.start_facing = start.facing;
      cur.start_joy_x = start.joy_x; cur.start_joy_y = start.joy_y;
      cur.start_trigger = start.trigger;
      cur.start_lcancel = start.lcancel;
      const PlayerFrame& prev = history(f - 1, port, fol);
      const bool have_prev = prev.present && prev.frame == f - 1;
      cur.prev_x = have_prev ? prev.x : cur.x;
      cur.prev_y = have_prev ? prev.y : cur.y;
      history(f, port, fol) = cur;
    }
  }
  g_building.fod_left = g_fod_left;
  g_building.fod_right = g_fod_right;
  if (!native) {
    read_stage_lines(g_building.stage_lines);
    read_capsules(g_building);
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_shown = g_building;
  g_last_commit = std::chrono::steady_clock::now();
}

}  // namespace

void native_frame(const MuLookSnapshot& snapshot, const MuLookItems* items) {
  gx::Frame scene;
  scene.scene_major = (uint8_t)snapshot.major;
  scene.scene_minor = (uint8_t)snapshot.minor;
  if (!gx::frame_in_match(scene) || (snapshot.major & (MU_LOOK_SCENE_PAUSED | MU_LOOK_SCENE_ENDED))) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_in_match = false;
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_in_match) {
      g_in_match = true;
      ++g_match_serial;
      for (auto& history_frame : g_history) for (auto& port : history_frame) for (auto& player : port) player.present = false;
      g_building = Frame{};
    }
    g_info.stage = (uint16_t)snapshot.stage;
    g_info.teams = snapshot.teams != 0;
    for (int port = 0; port < 4; ++port) {
      const auto& src = snapshot.fighters[port][0];
      g_info.ports[port] = {src.present != 0, (uint8_t)src.external, (uint8_t)src.team, (uint8_t)src.costume};
    }
  }
  begin_frame(snapshot.frame);
  if (items) {
    g_building.items.clear();
    for (int i = 0; i < std::clamp(items->count, 0, MU_LOOK_ITEMS); ++i) {
      const MuLookItem& src = items->items[i];
      if (!std::isfinite(src.x) || !std::isfinite(src.y) || !std::isfinite(src.vx) || !std::isfinite(src.vy)) continue;
      ItemFrame dst;
      dst.type = (uint16_t)src.type; dst.state = (uint8_t)src.state; dst.owner = (int8_t)src.owner;
      dst.missile_type = (uint8_t)src.missile_type; dst.charge_level = (uint8_t)src.charge_level;
      dst.facing = src.facing; dst.x = src.x; dst.y = src.y; dst.vx = src.vx; dst.vy = src.vy;
      g_building.items.push_back(dst);
    }
  }
  g_building.stage_lines.clear();
  for (int i = 0; i < std::clamp(snapshot.stage_line_count, 0, MU_LOOK_STAGE_LINES); ++i) {
    const MuLookStageLine& line = snapshot.stage_lines[i];
    if (std::isfinite(line.x0) && std::isfinite(line.y0) && std::isfinite(line.x1) && std::isfinite(line.y1))
      g_building.stage_lines.push_back({line.x0, line.y0, line.x1, line.y1, line.floor != 0, line.platform != 0});
  }
  for (int port = 0; port < 4; ++port) for (int follower = 0; follower < 2; ++follower) {
    const MuLookFighter& src = snapshot.fighters[port][follower];
    PlayerFrame& dst = g_building.players[port][follower];
    auto& caps = g_building.capsules[port][follower];
    auto& bones = g_building.bones[port][follower];
    caps.clear(); bones.clear();
    dst.present = src.present != 0;
    if (!dst.present) continue;
    dst.character = (uint8_t)src.kind; dst.action = (uint16_t)src.action;
    dst.x = src.x; dst.y = src.y; dst.facing = src.facing; dst.percent = src.percent;
    dst.shield = src.shield; dst.stocks = (uint8_t)src.stocks; dst.counter = src.animation;
    for (int i = 0; i < std::clamp(src.capsule_count, 0, MU_LOOK_CAPSULES); ++i) {
      const MuLookCapsule& c = src.capsules[i];
      if (std::isfinite(c.ax) && std::isfinite(c.ay) && std::isfinite(c.bx) && std::isfinite(c.by) &&
          std::isfinite(c.radius) && c.radius > 0 && c.radius <= 50)
        caps.push_back({c.ax, c.ay, c.bx, c.by, c.radius});
    }
    for (int i = 0; i < std::clamp(src.bone_count, 0, MU_LOOK_BONES); ++i) {
      const MuLookBone& b = src.bones[i];
      const float length = std::hypot(b.bx - b.ax, b.by - b.ay);
      if (std::isfinite(length) && length > 0.6f && length < 30.0f) bones.push_back({b.ax, b.ay, b.bx, b.by});
    }
  }
  commit(true);
}

void feed(const uint8_t* e, uint32_t size) {
  if (size == 0) return;
  switch (e[0]) {
    case EV_GAME_START: {
      if (size < 0x6F + 0x24 * 3) return;
      GameInfo info;
      info.stage = be16(e + 0x13);
      info.teams = e[0x0D] != 0;
      for (int i = 0; i < 4; ++i) {
        const uint8_t type = e[0x66 + 0x24 * i];
        auto& port = info.ports[i];
        port.present = type != 3;
        port.external = e[0x65 + 0x24 * i];
        port.shade = e[0x6C + 0x24 * i];
        port.team = e[0x6E + 0x24 * i];
        if (port.present && port.external < 26 && g_fighter_style.load(std::memory_order_relaxed) == 0)   // the styles need no pack
          for (int8_t id : kInternalByExternal[port.external]) if (id >= 0) request_pack(id);
      }
      for (auto& slot : g_history) for (auto& port : slot) for (auto& pf : port) pf.present = false;
      g_building = Frame{};
      g_fod_left = 20.0f; g_fod_right = 27.44186047f;
      std::lock_guard<std::mutex> lock(g_mutex);
      g_info = info;
      g_shown = Frame{};
      g_in_match = true;
      ++g_match_serial;
      break;
    }
    case EV_GAME_END: {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_in_match = false;
      break;
    }
    case EV_PRE_FRAME: {
      if (size < 0x2D) return;
      const uint8_t port = e[5], fol = e[6] ? 1 : 0;
      if (port >= 4) return;
      begin_frame(bes32(e + 1));
      PlayerFrame& pf = g_building.players[port][fol];
      pf.joy_x = bef(e + 0x19); pf.joy_y = bef(e + 0x1D); pf.trigger = bef(e + 0x29);
      break;
    }
    case EV_POST_FRAME: {
      if (size < 0x35) return;
      const uint8_t port = e[5], fol = e[6] ? 1 : 0;
      if (port >= 4) return;
      begin_frame(bes32(e + 1));
      PlayerFrame& pf = g_building.players[port][fol];
      pf.present = true;
      pf.character = e[7];
      pf.action = be16(e + 8);
      pf.x = bef(e + 0x0A); pf.y = bef(e + 0x0E); pf.facing = bef(e + 0x12);
      pf.percent = bef(e + 0x16); pf.shield = bef(e + 0x1A);
      pf.stocks = e[0x21];
      pf.counter = bef(e + 0x22);
      pf.lcancel = e[0x33];
      pf.hurtbox = e[0x34];
      break;
    }
    case EV_ITEM: {
      if (size < 0x2B) return;
      begin_frame(bes32(e + 1));
      ItemFrame it;
      it.type = be16(e + 5); it.state = e[7];
      it.facing = bef(e + 8); it.vx = bef(e + 0x0C); it.vy = bef(e + 0x10);
      it.x = bef(e + 0x14); it.y = bef(e + 0x18);
      it.missile_type = e[0x26]; it.charge_level = e[0x29]; it.owner = (int8_t)e[0x2A];
      g_building.items.push_back(it);
      break;
    }
    case EV_FOD_PLATFORM: {
      if (size < 0x0A) return;
      (e[5] == 1 ? g_fod_left : g_fod_right) = bef(e + 6);
      break;
    }
    case EV_FRAME_BOOKEND: {
      if (size < 5) return;
      begin_frame(bes32(e + 1));
      commit();
      break;
    }
    default: break;
  }
}

bool match_in_progress() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_in_match && g_shown.number > -1000 &&
         std::chrono::steady_clock::now() - g_last_commit < std::chrono::seconds(15);
}

namespace {

// ---- SVG path flattening (model space, 1000 x 1000, y down) ----

struct PathReader {
  const char* s; const char* end;
  void skip() { while (s < end && (*s == ' ' || *s == ',' || *s == '\n' || *s == '\t' || *s == '\r')) ++s; }
  bool number_next() { skip(); return s < end && (std::isdigit((unsigned char)*s) || *s == '-' || *s == '+' || *s == '.'); }
  // The path string is NUL terminated (std::string), so strtof can read straight out of it. It
  // also splits svgo's packed numbers correctly: "1.5.5" is 1.5 then .5, "-4-.9" is -4 then -.9.
  float number() {
    skip();
    char* next = nullptr;
    const float v = std::strtof(s, &next);
    if (next == s) { s = end; return 0; }   // not a number: stop reading this path
    s = next;
    return v;
  }
  // Arc flags are single characters and may be written with no separator ("a5 5 0 01-4 2").
  bool flag() { skip(); return s < end && *s++ == '1'; }
};

void add_cubic(std::vector<ImVec2>& out, ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3) {
  const float len = std::hypot(p1.x - p0.x, p1.y - p0.y) + std::hypot(p2.x - p1.x, p2.y - p1.y) + std::hypot(p3.x - p2.x, p3.y - p2.y);
  const int n = std::clamp((int)(len / 6.0f) + 1, 1, 12);
  for (int i = 1; i <= n; ++i) {
    const float t = (float)i / n, u = 1 - t;
    const float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    out.push_back(ImVec2(a * p0.x + b * p1.x + c * p2.x + d * p3.x, a * p0.y + b * p1.y + c * p2.y + d * p3.y));
  }
}

void add_quad(std::vector<ImVec2>& out, ImVec2 p0, ImVec2 p1, ImVec2 p2) {
  add_cubic(out, p0, ImVec2(p0.x + 2.0f / 3 * (p1.x - p0.x), p0.y + 2.0f / 3 * (p1.y - p0.y)),
            ImVec2(p2.x + 2.0f / 3 * (p1.x - p2.x), p2.y + 2.0f / 3 * (p1.y - p2.y)), p2);
}

// SVG endpoint arc to points (SVG 1.1 implementation notes, F.6.5).
void add_arc(std::vector<ImVec2>& out, ImVec2 p0, float rx, float ry, float rot_deg, bool large, bool sweep, ImVec2 p1) {
  if (rx == 0 || ry == 0) { out.push_back(p1); return; }
  rx = std::fabs(rx); ry = std::fabs(ry);
  const float phi = rot_deg * kPi / 180, cp = std::cos(phi), sp = std::sin(phi);
  const float dx = (p0.x - p1.x) / 2, dy = (p0.y - p1.y) / 2;
  const float x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
  const float lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
  if (lambda > 1) { const float s = std::sqrt(lambda); rx *= s; ry *= s; }
  const float num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
  const float den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
  float coef = den > 0 ? std::sqrt(std::max(0.0f, num / den)) : 0;
  if (large == sweep) coef = -coef;
  const float cx1 = coef * rx * y1 / ry, cy1 = -coef * ry * x1 / rx;
  const float cx = cp * cx1 - sp * cy1 + (p0.x + p1.x) / 2, cy = sp * cx1 + cp * cy1 + (p0.y + p1.y) / 2;
  auto angle = [](float ux, float uy, float vx, float vy) {
    return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  };
  const float t1 = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
  float dt = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
  if (!sweep && dt > 0) dt -= 2 * kPi;
  if (sweep && dt < 0) dt += 2 * kPi;
  const int n = std::clamp((int)(std::fabs(dt) * std::max(rx, ry) / 6.0f) + 1, 2, 24);
  for (int i = 1; i <= n; ++i) {
    const float t = t1 + dt * i / n;
    const float ex = rx * std::cos(t), ey = ry * std::sin(t);
    out.push_back(ImVec2(cp * ex - sp * ey + cx, sp * ex + cp * ey + cy));
  }
}

// Slippi Lab's silhouettes are one closed contour each (potrace output run through svgo), so one
// polygon per path is enough. A second subpath, if a future pack has one, is appended to the same
// outline rather than dropped.
std::vector<ImVec2> flatten(const std::string& d) {
  std::vector<ImVec2> out;
  PathReader r{d.data(), d.data() + d.size()};
  ImVec2 cur(0, 0), start(0, 0), ctrl(0, 0);
  char cmd = 0, prev = 0;
  while (true) {
    r.skip();
    if (r.s >= r.end) break;
    if (std::isalpha((unsigned char)*r.s)) cmd = *r.s++;
    else if (!cmd) break;
    const bool rel = std::islower((unsigned char)cmd) != 0;
    const ImVec2 base = rel ? cur : ImVec2(0, 0);
    auto pt = [&](float x, float y) { return ImVec2(base.x + x, base.y + y); };
    switch (std::tolower((unsigned char)cmd)) {
      case 'm': {
        const float x = r.number(), y = r.number();
        cur = start = pt(x, y);
        out.push_back(cur);
        cmd = rel ? 'l' : 'L';   // further pairs after a moveto are linetos
        break;
      }
      case 'l': { const float x = r.number(), y = r.number(); cur = pt(x, y); out.push_back(cur); break; }
      case 'h': { const float x = r.number(); cur = ImVec2(rel ? cur.x + x : x, cur.y); out.push_back(cur); break; }
      case 'v': { const float y = r.number(); cur = ImVec2(cur.x, rel ? cur.y + y : y); out.push_back(cur); break; }
      case 'c': {
        const float a = r.number(), b = r.number(), c = r.number(), e = r.number(), f = r.number(), g = r.number();
        const ImVec2 p1 = pt(a, b), p2 = pt(c, e), p3 = pt(f, g);
        add_cubic(out, cur, p1, p2, p3);
        ctrl = p2; cur = p3;
        break;
      }
      case 's': {
        const float c = r.number(), e = r.number(), f = r.number(), g = r.number();
        const char pl = (char)std::tolower((unsigned char)prev);
        const ImVec2 p1 = (pl == 'c' || pl == 's') ? ImVec2(2 * cur.x - ctrl.x, 2 * cur.y - ctrl.y) : cur;
        const ImVec2 p2 = pt(c, e), p3 = pt(f, g);
        add_cubic(out, cur, p1, p2, p3);
        ctrl = p2; cur = p3;
        break;
      }
      case 'q': {
        const float a = r.number(), b = r.number(), f = r.number(), g = r.number();
        const ImVec2 p1 = pt(a, b), p2 = pt(f, g);
        add_quad(out, cur, p1, p2);
        ctrl = p1; cur = p2;
        break;
      }
      case 't': {
        const float f = r.number(), g = r.number();
        const char pl = (char)std::tolower((unsigned char)prev);
        const ImVec2 p1 = (pl == 'q' || pl == 't') ? ImVec2(2 * cur.x - ctrl.x, 2 * cur.y - ctrl.y) : cur;
        const ImVec2 p2 = pt(f, g);
        add_quad(out, cur, p1, p2);
        ctrl = p1; cur = p2;
        break;
      }
      case 'a': {
        const float rx = r.number(), ry = r.number(), rot = r.number();
        const bool large = r.flag(), sweep = r.flag();
        const float x = r.number(), y = r.number();
        const ImVec2 p1 = pt(x, y);
        add_arc(out, cur, rx, ry, rot, large, sweep, p1);
        cur = p1;
        break;
      }
      case 'z': cur = start; prev = cmd; cmd = 0; continue;
      default: return out;   // unknown command: keep what was read
    }
    prev = cmd;
  }
  // Drop repeated points (the closing point especially): the ear clipper wants a simple polygon.
  std::vector<ImVec2> clean;
  clean.reserve(out.size());
  for (const ImVec2& p : out)
    if (clean.empty() || std::fabs(p.x - clean.back().x) > 0.01f || std::fabs(p.y - clean.back().y) > 0.01f) clean.push_back(p);
  while (clean.size() > 1 && std::fabs(clean.front().x - clean.back().x) <= 0.01f && std::fabs(clean.front().y - clean.back().y) <= 0.01f) clean.pop_back();
  return clean;
}

// ImGui's own concave fill is an ear clipper that gives up on the small self-intersections traced
// outlines have, and fills a hull instead. earcut (mapbox, ISC) handles those.
Pack::Mesh build_mesh(const std::string& d) {
  Pack::Mesh m;
  m.outline = flatten(d);
  if (m.outline.size() < 3) return m;
  std::vector<std::vector<std::array<float, 2>>> polygon(1);
  polygon[0].reserve(m.outline.size());
  for (const ImVec2& p : m.outline) polygon[0].push_back({p.x, p.y});
  m.triangles = mapbox::earcut<uint32_t>(polygon);
  return m;
}

// ---- Stages (Slippi Lab src/components/viewer/Stage.tsx; world units, y up) ----

struct StageShape {
  uint16_t id;
  std::vector<ImVec2> main;
  std::vector<std::array<ImVec2, 2>> platforms;
  ImVec2 bz_min, bz_max;
};

const std::vector<StageShape>& stages() {
  static const std::vector<StageShape> list = {
    {31, {{-68.4f, 0}, {68.4f, 0}, {65, -6}, {36, -19}, {39, -21}, {33, -25}, {30, -29}, {29, -35}, {10, -40}, {10, -30},
          {-10, -30}, {-10, -40}, {-29, -35}, {-30, -29}, {-33, -25}, {-39, -21}, {-36, -19}, {-65, -6}},
     {{{{-57.6f, 27.2f}, {-20, 27.2f}}}, {{{20, 27.2f}, {57.6f, 27.2f}}}, {{{-18.8f, 54.4f}, {18.8f, 54.4f}}}},
     {-224, -108.8f}, {224, 200}},
    {28, {{-76.5f, -11}, {-77.25f, 0}, {77.25f, 0}, {76.5f, -11}, {65.75f, -36}, {-65.75f, -36}},
     {{{{-61.393f, 30.142f}, {-31.725f, 30.142f}}}, {{{31.704f, 30.243f}, {63.075f, 30.243f}}}, {{{-19.018f, 51.425f}, {19.017f, 51.425f}}}},
     {-255, -123}, {255, 250}},
    {32, {{-85.6f, 0}, {85.6f, 0}, {85.6f, -10}, {65, -20}, {65, -30}, {60, -47}, {50, -55}, {45, -56}, {-45, -56},
          {-50, -55}, {-60, -47}, {-65, -30}, {-65, -20}, {-85.6f, -10}},
     {}, {-246, -140}, {246, 188}},
    {8, {{-54, -91}, {-54, -47}, {-53, -46}, {-53, -31}, {-54, -30}, {-54, -28}, {-53, -27}, {-53, -12}, {-54, -11},
         {-55, -8}, {-56, -7}, {-56, -3.5f}, {-39, 0}, {39, 0}, {56, -3.5f}, {56, -7}, {55, -8}, {54, -11}, {53, -12},
         {53, -27}, {54, -28}, {54, -30}, {53, -31}, {53, -46}, {54, -47}, {54, -91}},
     {{{{-59.5f, 23.45f}, {-28, 23.45f}}}, {{{28, 23.45f}, {59.5f, 23.45f}}}, {{{-15.75f, 42}, {15.75f, 42}}}},
     {-175.7f, -91}, {173.6f, 169}},
    // Fountain of Dreams: Slippi Lab's outline doubles back on itself at the far left
    // ("-63.35,0.62 / -63.35,-4.5 / -63.33,0.62"); that sliver is dropped so the polygon is simple.
    // The side platforms move and are placed per frame below.
    {2, {{-63.33f, 0.62f}, {-53.5f, 0.62f}, {-51, 0}, {51, 0}, {53.5f, 0.62f}, {63.33f, 0.62f}, {63.35f, -4.5f},
         {59.33f, -15}, {56.9f, -19.5f}, {55, -27}, {52, -32}, {48, -38}, {41, -42}, {19, -49.5f}, {13, -54.5f}, {10, -62},
         {8.8f, -72}, {8.8f, -150}, {-8.8f, -150}, {-8.8f, -72}, {-10, -62}, {-13, -54.5f}, {-19, -49.5f}, {-41, -42},
         {-48, -38}, {-52, -32}, {-55, -27}, {-56.9f, -19.5f}, {-59.33f, -15}, {-63.35f, -4.5f}},
     {{{{-14.25f, 42.75f}, {14.25f, 42.75f}}}},
     {-198.75f, -146.25f}, {198.75f, 202.5f}},
    {3, {{87.75f, 0}, {87.75f, -4}, {73.75f, -15}, {73.75f, -17.75f}, {60, -17.75f}, {60, -38}, {15, -60}, {15, -112},
         {-15, -112}, {-15, -60}, {-60, -38}, {-60, -17.75f}, {-73.75f, -17.75f}, {-73.75f, -15}, {-87.75f, -4}, {-87.75f, 0}},
     {{{{-55, 25}, {-25, 25}}}, {{{25, 25}, {55, 25}}}},
     {-230, -111}, {230, 180}},
  };
  return list;
}

// Randall's platform on Yoshi's Story, one lap every 1200 frames (Slippi Lab's table and speeds).
bool randall(int32_t frame_number, ImVec2& left, ImVec2& right) {
  const int lap = ((frame_number % 1200) + 1200) % 1200;
  const float width = 11.9f;
  if (lap > 476 && lap < 1016) {
    const float x = 101.235443115234f - 0.35484f * (lap - 477);
    left = ImVec2(x - width, -13.64989f); right = ImVec2(x, -13.64989f); return true;
  }
  if (lap > 1022 && lap < 1069) {
    const float y = -15.2778692245483f - 0.354839325f * (lap - 1023);
    left = ImVec2(-103.6f, y); right = ImVec2(-91.7f, y); return true;
  }
  if (lap > 1075 || lap < 416) {
    const float x = -101.850006103516f + 0.35484f * (lap + (lap < 416 ? 125 : -1076));
    left = ImVec2(x, -33.2489f); right = ImVec2(x + width, -33.2489f); return true;
  }
  if (lap > 423 && lap < 469) {
    const float y = -31.16023254394531f + 0.354839325f * (lap - 424);
    left = ImVec2(91.35f, y); right = ImVec2(103.25f, y); return true;
  }
  struct Corner { int lap; float y, x; };
  static const Corner corners[] = {
    {416, -33.184478759765625f, 89.75263977050781f}, {417, -33.04470443725586f, 90.07878112792969f},
    {418, -32.904930114746094f, 90.40492248535156f}, {419, -32.76515197753906f, 90.73107147216797f},
    {420, -32.49260711669922f, 90.92455291748047f}, {421, -32.16635513305664f, 91.06437683105469f},
    {422, -31.84010314941406f, 91.20419311523438f}, {423, -31.513851165771484f, 91.3440170288086f},
    {469, -15.1948881149292f, 91.3371353149414f}, {470, -14.868742942810059f, 91.1973648071289f},
    {471, -14.542601585388184f, 91.05758666992188f}, {472, -14.216456413269043f, 90.91781616210938f},
    {473, -13.967143058776855f, 90.71036529541016f}, {474, -13.869664192199707f, 90.36917877197266f},
    {475, -13.772183418273926f, 90.02799224853516f}, {476, -13.674698829650879f, 89.68680572509766f},
    {1016, -13.679760932922363f, -101.919677734375f}, {1017, -13.819535255432129f, -102.24581909179688f},
    {1018, -13.959305763244629f, -102.57196044921875f}, {1019, -14.099089622497559f, -102.8981018066406f},
    {1020, -14.320136070251465f, -103.1476135253906f}, {1021, -14.6375150680542f, -103.3063049316406f},
    {1022, -14.954894065856934f, -103.4649963378906f}, {1069, -31.59004211425781f, -103.554931640625f},
    {1070, -31.907413482666016f, -103.39625549316406f}, {1071, -32.22478485107422f, -103.23756408691406f},
    {1072, -32.54215621948242f, -103.07887268066406f}, {1073, -32.7216796875f, -102.77439880371094f},
    {1074, -32.89775085449219f, -102.46626281738281f}, {1075, -33.07382583618164f, -102.15814208984375f},
  };
  for (const Corner& c : corners)
    if (c.lap == lap) { left = ImVec2(c.x, c.y); right = ImVec2(c.x + width, c.y); return true; }
  return false;
}

// ---- Colors (Tailwind values Slippi Lab uses) ----

constexpr ImU32 rgb(uint32_t hex, float alpha = 1.0f) {
  return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, (int)(alpha * 255));
}
constexpr uint32_t kBackground = 0xF8FAFC;   // slate-50
constexpr uint32_t kStage = 0x1E293B;        // slate-800
constexpr uint32_t kRandall = 0x94A3B8;      // slate-400
constexpr uint32_t kGrid = 0xBFE5CB;         // slippi-50
constexpr uint32_t kItem = 0xA9A9A9;         // darkgray

uint32_t player_color(const GameInfo& info, int port, bool nana) {
  static const uint32_t solo[4][2] = {
    {0xB91C1C, 0xDC2626}, {0x1D4ED8, 0x2563EB}, {0xEAB308, 0xFACC15}, {0x15803D, 0x16A34A},
  };
  static const uint32_t teams[3][2] = {{0x991B1B, 0xDC2626}, {0x166534, 0x16A34A}, {0x1E40AF, 0x2563EB}};
  if (info.teams) {
    const auto& p = info.ports[port];
    return teams[std::min<int>(p.team, 2)][nana ? 1 : std::min<int>(p.shade, 1)];
  }
  return solo[port][nana ? 1 : 0];
}

// ---- Camera (Slippi Lab Camera.tsx: follow the players, eased 4% per game frame) ----

struct Camera {
  bool valid = false;
  float cx = 0, cy = 0, scale = 5;
  int32_t frame = -1000;
  uint64_t serial = 0;
};
Camera g_camera;   // render thread only

void update_camera(const Frame& f, uint64_t serial) {
  if (g_camera.serial != serial) { g_camera = Camera{}; g_camera.serial = serial; }
  if (f.number == g_camera.frame) return;   // eased once per game frame, not per presented frame
  g_camera.frame = f.number;
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  bool any = false;
  for (const auto& port : f.players) {
    if (!port[0].present) continue;
    any = true;
    x0 = std::min(x0, port[0].x); x1 = std::max(x1, port[0].x);
    y0 = std::min(y0, port[0].y); y1 = std::max(y1, port[0].y);
  }
  if (!any) return;
  x0 -= 25; x1 += 25; y0 -= 25; y1 += 25;
  const float ncx = (x0 + x1) / 2, ncy = (y0 + y1) / 2;
  const float scale = std::min(640.0f / std::max(x1 - x0, 100.0f), 480.0f / std::max(y1 - y0, 100.0f));
  if (!g_camera.valid) { g_camera.cx = ncx; g_camera.cy = ncy; g_camera.valid = true; }
  g_camera.cx += (ncx - g_camera.cx) * 0.04f;
  g_camera.cy += (ncy - g_camera.cy) * 0.04f;
  g_camera.scale += (scale - g_camera.scale) * 0.04f;
}

// ---- Drawing ----

struct View {
  float ox, oy, px;   // screen origin (window center) and pixels per world unit
  float cx, cy;
  ImVec2 operator()(float wx, float wy) const { return ImVec2(ox + (wx - cx) * px, oy - (wy - cy) * px); }
  ImVec2 operator()(ImVec2 w) const { return (*this)(w.x, w.y); }
};

void draw_circle(ImDrawList* dl, const View& v, float x, float y, float r, ImU32 col) {
  dl->AddCircleFilled(v(x, y), std::max(1.0f, r * v.px), col, 0);
}

// The game's live collision lines: exactly the floors the characters are standing on this frame,
// moving platforms and Stadium transformations included.
void draw_stage_lines(ImDrawList* dl, const View& v, const Frame& f) {
  const ImU32 stage = rgb(kStage);
  const float floor_w = std::max(1.0f, 1.0f * v.px), edge_w = std::max(1.0f, 0.5f * v.px);
  for (const StageLine& l : f.stage_lines)
    dl->AddLine(v(l.x0, l.y0), v(l.x1, l.y1), stage, (l.floor || l.platform) ? floor_w : edge_w);
}

void draw_stage(ImDrawList* dl, const View& v, const GameInfo& info, const Frame& f) {
  const StageShape* shape = nullptr;
  for (const StageShape& s : stages()) if (s.id == info.stage) shape = &s;
  const bool live = !f.stage_lines.empty();
  if (!shape) {   // not one of the six Slippi Lab draws: the game's own outline, if we have it
    if (live) draw_stage_lines(dl, v, f);
    return;
  }
  // Grid every 5 units inside the blast zones.
  const ImU32 grid = rgb(kGrid);
  const float grid_w = std::max(1.0f, 0.1f * v.px);
  for (float x = shape->bz_min.x; x < shape->bz_max.x; x += 5)
    dl->AddLine(v(x, shape->bz_min.y), v(x, shape->bz_max.y), grid, grid_w);
  for (float y = shape->bz_min.y; y < shape->bz_max.y; y += 5)
    dl->AddLine(v(shape->bz_min.x, y), v(shape->bz_max.x, y), grid, grid_w);
  // Main platform.
  std::vector<ImVec2> pts;
  pts.reserve(shape->main.size());
  for (const ImVec2& p : shape->main) pts.push_back(v(p));
  const ImU32 stage = rgb(kStage);
  dl->AddConcavePolyFilled(pts.data(), (int)pts.size(), stage);
  dl->AddPolyline(pts.data(), (int)pts.size(), stage, ImDrawFlags_Closed, std::max(1.0f, 0.5f * v.px));
  const float line_w = std::max(1.0f, 1.0f * v.px);   // SVG's default stroke, one world unit
  if (live) {
    // Platforms, Randall, the Fountain's moving platforms and whatever a Stadium transformation
    // puts down, all from the game itself. The filled body above is Slippi Lab's outline of the
    // part that never changes.
    draw_stage_lines(dl, v, f);
  } else {
    for (const auto& pl : shape->platforms) dl->AddLine(v(pl[0]), v(pl[1]), stage, line_w);
    if (info.stage == 2) {   // Fountain of Dreams side platforms
      constexpr float k = 0.80625f;   // game platform height to drawn height (Slippi Lab)
      dl->AddLine(v(-49.5f, f.fod_left * k), v(-21, f.fod_left * k), stage, line_w);
      dl->AddLine(v(21, f.fod_right * k), v(49.5f, f.fod_right * k), stage, line_w);
    }
    ImVec2 l, r;
    if (info.stage == 8 && randall(f.number, l, r)) dl->AddLine(v(l), v(r), rgb(kRandall), line_w);
  }
  dl->AddRect(v(shape->bz_min.x, shape->bz_max.y), v(shape->bz_max.x, shape->bz_min.y), stage, 0, 0, std::max(1.0f, v.px));
}

// Stick figure: a thick line through each capsule with round ends, so limbs meet at the joints the
// way they do in the game's hurtboxes; a capsule that is a ball (both ends together: the head, hands)
// becomes a filled dot, the head the largest, so the head reads as a head.
void draw_capsule_sticks(ImDrawList* dl, const View& v, const std::vector<Frame::Capsule>& caps, ImU32 col) {
  for (const auto& c : caps) {
    const float len = std::hypot(c.bx - c.ax, c.by - c.ay);
    const float half = std::max(1.5f, c.r * 0.4f * v.px);
    if (len < 0.5f) {
      dl->AddCircleFilled(v(c.ax, c.ay), std::max(2.5f, c.r * 0.85f * v.px), col, 0);
      continue;
    }
    dl->AddLine(v(c.ax, c.ay), v(c.bx, c.by), col, 2 * half);
    dl->AddCircleFilled(v(c.ax, c.ay), half, col, 0);
    dl->AddCircleFilled(v(c.bx, c.by), half, col, 0);
  }
}

// Stick figure with its body: the hurtboxes filled in a pale tint of the port colour (the size and shape
// the game hits), the skeleton on top in the full colour, and the head (the largest ball hurtbox) as a
// ring. Without a skeleton (it could not be read), the hurtboxes as sticks.
ImU32 mix_with(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int s) { return (uint32_t)(((a >> s) & 0xFF) * (1 - t) + ((b >> s) & 0xFF) * t + 0.5f) << s; };
  return rgb(ch(16) | ch(8) | ch(0));
}

void draw_stick_figure(ImDrawList* dl, const View& v, const std::vector<Frame::Capsule>& caps,
                       const std::vector<Frame::Bone>& bones, uint32_t colour) {
  const ImU32 col = rgb(colour), body = mix_with(colour, kBackground, 0.78f);
  if (bones.empty()) { draw_capsule_sticks(dl, v, caps, col); return; }
  for (const auto& c : caps) {
    const float r = std::max(1.0f, c.r * v.px);
    dl->AddCircleFilled(v(c.ax, c.ay), r, body, 0);
    if (std::hypot(c.bx - c.ax, c.by - c.ay) >= 0.5f) {
      dl->AddCircleFilled(v(c.bx, c.by), r, body, 0);
      dl->AddLine(v(c.ax, c.ay), v(c.bx, c.by), body, 2 * r);
    }
  }
  const float half = std::max(1.25f, 0.32f * v.px);
  const Frame::Capsule* head = nullptr;
  for (const auto& c : caps)
    if (std::hypot(c.bx - c.ax, c.by - c.ay) < 0.5f && (!head || c.r > head->r)) head = &c;
  for (const auto& b : bones) {
    // Joints hanging off the head (a cap, a nose) start inside it: their lines crossed the face.
    if (head && std::hypot(b.ax - head->ax, b.ay - head->ay) < head->r) continue;
    dl->AddLine(v(b.ax, b.ay), v(b.bx, b.by), col, 2 * half);
    dl->AddCircleFilled(v(b.bx, b.by), half, col, 8);
  }
  if (head) {
    const float r = std::max(3.0f, head->r * 0.8f * v.px);
    dl->AddCircleFilled(v(head->ax, head->ay), r, body, 0);
    dl->AddCircle(v(head->ax, head->ay), r, col, 0, 2 * half);
  }
}

// Keep each capsule's contour readable where large hurtboxes overlap (notably Bowser's shell).
void draw_capsule_fighter(ImDrawList* dl, const View& v, const std::vector<Frame::Capsule>& caps,
                          const std::vector<Frame::Bone>& bones, ImU32 col) {
  const ImU32 fill = (col & 0x00FFFFFFu) | 0xD0000000u;
  const ImU32 edge = (col & 0x00FFFFFFu) | 0xFF000000u;
  for (const auto& c : caps) {
    const float r = std::max(1.0f, c.r * v.px);
    const ImVec2 a = v(c.ax, c.ay), b = v(c.bx, c.by);
    if (std::hypot(c.bx - c.ax, c.by - c.ay) < 0.5f) {
      dl->AddCircleFilled(a, r, fill);
      dl->AddCircle(a, r, edge, 0, 1.0f);
      continue;
    }
    // One convex shape avoids accumulating alpha at the line/circle joins.
    const float angle = std::atan2(b.y - a.y, b.x - a.x);
    ImVec2 contour[26];
    for (int i = 0; i <= 12; ++i) {
      const float t = angle + 1.570796327f + i * 3.141592654f / 12;
      contour[i] = ImVec2(a.x + r * std::cos(t), a.y + r * std::sin(t));
      contour[13 + i] = ImVec2(b.x - r * std::cos(t), b.y - r * std::sin(t));
    }
    dl->AddConvexPolyFilled(contour, 26, fill);
    dl->AddPolyline(contour, 26, edge, ImDrawFlags_Closed, 1.0f);
  }
  // Weapons have skeleton joints but no hurtboxes. Preserve their visible extent,
  // including Marth/Roy's swords, instead of dropping everything outside the body.
  for (const auto& b : bones)
    dl->AddLine(v(b.ax, b.ay), v(b.bx, b.by), col, std::max(1.5f, 0.35f * v.px));
}

// Blocks: floors and platforms as solid slabs hanging below their line (platforms thin, floors deep),
// walls as thick lines. The live collision, so moving platforms and Stadium's forms come along.
void draw_stage_blocks(ImDrawList* dl, const View& v, const Frame& f) {
  const ImU32 stage = rgb(kStage), edge = (stage & 0x00FFFFFFu) | 0x80000000u;
  // Not every stage flags its pass-through platforms, so a floor above the lowest one (the main
  // stage's top) is drawn as a thin ledge too.
  float lowest = std::numeric_limits<float>::max();
  for (const StageLine& l : f.stage_lines)
    if (l.floor && !l.platform) lowest = std::min(lowest, std::min(l.y0, l.y1));
  for (const StageLine& l : f.stage_lines) {
    if (l.platform || l.floor) {
      const bool ledge = l.platform || std::min(l.y0, l.y1) > lowest + 4.0f;
      const float depth = ledge ? 2.5f : 12.0f;
      const ImVec2 q[4] = {v(l.x0, l.y0), v(l.x1, l.y1), v(l.x1, l.y1 - depth), v(l.x0, l.y0 - depth)};
      dl->AddConvexPolyFilled(q, 4, stage);
    } else {
      dl->AddLine(v(l.x0, l.y0), v(l.x1, l.y1), edge, std::max(1.0f, 1.5f * v.px));
    }
  }
}

void draw_hexagon_ring(ImDrawList* dl, const View& v, float x, float y, float r, float hole, ImU32 col) {
  const float sx = std::sin(2 * kPi / 6);
  const ImVec2 off[6] = {{0, 1}, {sx, 0.5f}, {sx, -0.5f}, {0, -1}, {-sx, -0.5f}, {-sx, 0.5f}};
  for (int i = 0; i < 6; ++i) {
    const ImVec2 a = off[i], b = off[(i + 1) % 6];
    const ImVec2 q[4] = {v(x + a.x * r, y + a.y * r), v(x + b.x * r, y + b.y * r),
                         v(x + b.x * r * hole, y + b.y * r * hole), v(x + a.x * r * hole, y + a.y * r * hole)};
    dl->AddConvexPolyFilled(q, 4, col);
  }
}

// The last silhouette each player was drawn with this match (render thread only), shown again for
// actions Slippi Lab has no drawing for.
struct LastPose { uint64_t serial = 0; uint8_t character = 0; uint16_t path = 0xFFFF; float facing = 1; };
LastPose g_last_pose[4][2];
// Actions 0-11 are the KOs (DeadDown ... DeadUpFallHitCameraIce) and Sleep, the state between a
// lost stock and the respawn platform: nothing on screen to keep drawing.
constexpr uint16_t kLastDeadAction = 11;

void draw_player(ImDrawList* dl, const View& v, const GameInfo& info, const PlayerFrame& p, int port, bool nana) {
  const ImU32 inner = rgb(player_color(info, port, nana));
  std::shared_ptr<Pack> pack = get_pack(p.character);
  if (!pack) {
    // No silhouettes for this character (packs not built, or still loading): a plain marker.
    draw_circle(dl, v, p.x, p.y + 8, 6, inner);
    return;
  }
  const Anim* anim = nullptr;
  if (p.action < pack->action_anim.size() && pack->action_anim[p.action] < pack->anims.size())
    anim = &pack->anims[pack->action_anim[p.action]];
  // Which silhouette, facing and rotation this frame. Slippi Lab has no drawing for some actions
  // (being thrown, a few specials); for those the character keeps the last pose it was drawn in,
  // at its current position, rather than vanishing mid-move.
  uint16_t path = 0xFFFF;
  float facing = p.facing, rotation = 0;
  if (anim && !anim->frames.empty()) {
    // Floor, clamp -1 to 0, and loop (Entry, Guard...), as Slippi Lab does.
    const int index = (int)std::floor(std::max(0.0f, p.counter)) % (int)anim->frames.size();
    path = anim->frames[index];
    if (anim->fly_roll) {
      rotation = std::atan2(p.y - p.prev_y, p.x - p.prev_x) * 180 / kPi - 90;
    } else if ((p.character == kFox || p.character == kFalco) && (p.action == 355 || p.action == 356)) {
      const float joy = (p.start_joy_x == 0 && p.start_joy_y == 0) ? 90.0f : std::atan2(p.start_joy_y, p.start_joy_x) * 180 / kPi;
      rotation = joy - (p.start_facing == -1 ? 180.0f : 0.0f);
    }
    facing = anim->follows_facing ? p.facing : p.start_facing;
  }
  LastPose& last = g_last_pose[port][nana ? 1 : 0];
  if (last.serial != g_camera.serial) last = LastPose{};
  if (path < pack->paths.size()) {
    last = LastPose{g_camera.serial, p.character, path, facing};
  } else if (p.action <= kLastDeadAction) {
    return;   // KO'd or between stocks: off screen, nothing to hold
  } else if (last.path != 0xFFFF && last.character == p.character) {
    path = last.path;
    facing = last.facing;
  }
  if (path < pack->paths.size()) {
    auto it = pack->meshes.find(path);
    if (it == pack->meshes.end()) it = pack->meshes.emplace(path, build_mesh(pack->paths[path])).first;
    const Pack::Mesh& mesh = it->second;
    const std::vector<ImVec2>& model = mesh.outline;
    const float rad = rotation * kPi / 180, cr = std::cos(rad), sr = std::sin(rad);
    const float s = pack->scale;
    // Slippi Lab's transform, point first: translate(-500 -500), scale(.1 -.1), scale(facing 1),
    // scale(character), rotate about (0, 8), translate to the character's position.
    thread_local std::vector<ImVec2> screen;
    screen.resize(model.size());
    for (size_t i = 0; i < model.size(); ++i) {
      float x = (model[i].x - 500) * 0.1f * facing * s;
      float y = (model[i].y - 500) * -0.1f * s - 8;
      const float rx = x * cr - y * sr, ry = x * sr + y * cr + 8;
      screen[i] = v(p.x + rx, p.y + ry);
    }
    const ImU32 outer = p.start_lcancel == 2 ? rgb(0xFF0000) : p.hurtbox != 0 ? rgb(0x0000FF) : rgb(0x000000);
    if (screen.size() >= 3) {
      if (!mesh.triangles.empty()) {
        const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
        dl->PrimReserve((int)mesh.triangles.size(), (int)screen.size());
        const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
        for (uint32_t i : mesh.triangles) dl->PrimWriteIdx((ImDrawIdx)(base + i));
        for (const ImVec2& q : screen) dl->PrimWriteVtx(q, uv, inner);
      }
      dl->AddPolyline(screen.data(), (int)screen.size(), outer, ImDrawFlags_Closed, std::max(1.0f, 0.2f * s * v.px));
    }
  } else {
    // Never drawn yet this match (an action with no silhouette on the very first frames).
    draw_circle(dl, v, p.x, p.y + 8, 6, inner);
  }
}

// Gameplay signals are shared by silhouettes, sticks and capsules, including when no pack exists.
void draw_player_signals(ImDrawList* dl, const View& v, const GameInfo& info, const PlayerFrame& p,
                         const std::vector<Frame::Capsule>& caps, int port, bool nana) {
  const ImU32 inner = rgb(player_color(info, port, nana));
  const auto pack = g_fighter_style.load(std::memory_order_relaxed) == 0 ? get_pack(p.character) : nullptr;
  const Anim* anim = pack && p.action < pack->action_anim.size() && pack->action_anim[p.action] < pack->anims.size()
                         ? &pack->anims[pack->action_anim[p.action]] : nullptr;
  float x = p.x, y = p.y + 8, radius = 10;
  if (pack) { x += pack->shield_x * p.facing; y = p.y + pack->shield_y; radius = pack->shield_size; }
  else if (!caps.empty()) {
    float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
    for (const auto& c : caps) {
      lo_x = std::min(lo_x, std::min(c.ax, c.bx) - c.r); hi_x = std::max(hi_x, std::max(c.ax, c.bx) + c.r);
      lo_y = std::min(lo_y, std::min(c.ay, c.by) - c.r); hi_y = std::max(hi_y, std::max(c.ay, c.by) + c.r);
    }
    x = (lo_x + hi_x) * 0.5f; y = (lo_y + hi_y) * 0.5f;
    radius = std::clamp(std::max(hi_x - lo_x, hi_y - lo_y) * 0.9f, 4.0f, 50.0f);
  }
  if ((p.action >= 178 && p.action <= 182) || (anim && anim->guard)) {
    // Shield size from shield health and trigger strength (ssbwiki Shield statistics), with the
    // trigger read at the start of GuardDamage, since that action ignores trigger changes.
    float trigger = (p.action == 181 || (anim && anim->guard_damage)) ? p.start_trigger : (p.trigger == 0 ? 1.0f : p.trigger);
    trigger = std::clamp(trigger, 0.3f, 1.0f);
    const float trig_mult = 1 - (0.5f * (trigger - 0.3f)) / 0.7f;
    const float size_mult = ((std::clamp(p.shield, 0.0f, 60.0f) * trig_mult) / 60) * 0.85f + 0.15f;
    draw_circle(dl, v, x, y, radius * size_mult, (inner & 0x00FFFFFF) | ((ImU32)(0.35f * 255) << IM_COL32_A_SHIFT));
    dl->AddCircle(v(x, y), radius * size_mult * v.px, inner, 0, std::max(1.5f, v.px * 0.35f));
  }
  if ((p.character == kFox || p.character == kFalco) &&
      ((p.action >= 360 && p.action <= 369) || (anim && anim->shine)))
    draw_hexagon_ring(dl, v, x, y, 6, 0.6f, rgb(0x8ABCE9));
}

void draw_items(ImDrawList* dl, const View& v, const Frame& f) {
  const ImU32 gray = rgb(kItem), gray_half = rgb(kItem, 0.5f), red = rgb(0xFF0000);
  auto owner_pos = [&](const ItemFrame& it, float& x, float& y) {
    if (it.owner >= 0 && it.owner < 4 && f.players[it.owner][0].present) { x = f.players[it.owner][0].x; y = f.players[it.owner][0].y + 8; return; }
    x = it.x; y = it.y;
  };
  auto laser = [&](const ItemFrame& it, std::initializer_list<float> offsets, float facing) {
    const float dir = std::atan2(it.vy, it.vx), c = std::cos(dir), s = std::sin(dir);
    const float first = *offsets.begin() / 256, last = *(offsets.end() - 1) / 256;
    dl->AddLine(v(it.x + first * facing * c, it.y + first * facing * s), v(it.x + last * facing * c, it.y + last * facing * s), red, std::max(1.0f, v.px));
    for (float o : offsets) draw_circle(dl, v, it.x + o / 256 * facing * c, it.y + o / 256 * facing * s, 300.0f / 256, red);
  };
  for (const ItemFrame& it : f.items) {
    switch (it.type) {
      case 54: laser(it, {-200, -933, -1666}, it.facing); break;         // Fox's laser
      case 55: laser(it, {-200, -933, -1666, -2400}, 1.0f); break;       // Falco's laser (no facing, as in Slippi Lab)
      case 79: draw_circle(dl, v, it.x, it.y, 500.0f / 256, gray); break;   // Sheik's needle
      case 48: draw_circle(dl, v, it.x, it.y, 600.0f / 256, gray); break;   // Mario's fireball
      case 105: draw_circle(dl, v, it.x, it.y, 500.0f / 256, gray); break;  // Luigi's fireball
      case 69: draw_circle(dl, v, it.x, it.y, 2.5f, rgb(0x38BDF8)); break; // PK Thunder head
      case 70: case 71: case 72: case 73:
        draw_circle(dl, v, it.x, it.y, 1.2f, rgb(0x38BDF8)); break;       // PK Thunder trail
      case 95: draw_circle(dl, v, it.x, it.y, (it.missile_type == 0 ? 500.0f : 600.0f) / 256, gray); break;
      case 93: draw_circle(dl, v, it.x, it.y, (it.state == 3 ? 1536.0f : 500.0f) / 256, gray); break;   // Samus's bomb
      case 94: {   // Samus's charge shot, charge levels 0 to 7
        static const float r[8] = {300, 400, 500, 600, 700, 800, 900, 1200};
        draw_circle(dl, v, it.x, it.y, r[std::min<int>(it.charge_level, 7)] / 256, gray);
        break;
      }
      case 86: {   // Yoshi's egg: 0 held, 1 thrown, 2 exploded
        float x, y;
        if (it.state == 0) owner_pos(it, x, y); else { x = it.x; y = it.y; }
        draw_circle(dl, v, x, y, (it.state == 2 ? 2500.0f : 1000.0f) / 256, it.state == 1 ? gray : gray_half);
        break;
      }
      case 99: {   // Peach's turnip: 0 held
        float x, y;
        if (it.state == 0) owner_pos(it, x, y); else { x = it.x; y = it.y; }
        draw_circle(dl, v, x, y, 600.0f / 256, it.state == 0 ? gray_half : gray);
        break;
      }
      case 210: draw_circle(dl, v, it.x, it.y, 5 * 0.85f, rgb(0xAA0000)); break;   // Fly Guy
      default: draw_circle(dl, v, it.x, it.y, 2.0f, gray); break;   // unknown items remain visible
    }
  }
}

// Percent and stocks per port along the bottom. Slippi Lab's HUD is laid out for a replay viewer
// (timeline, names); this keeps only what a player needs mid-match.
void draw_hud(ImDrawList* dl, const GameInfo& info, const Frame& f, float w, float h) {
  int ports[4], count = 0;
  for (int i = 0; i < 4; ++i) if (info.ports[i].present && f.players[i][0].present) ports[count++] = i;
  if (!count) return;
  // Its own strip along the bottom, so the stage's lines never run through it: one card a player,
  // the percent large in a heavy face with the "%" smaller beside it (as the game sets it), the
  // stocks as dots under it.
  ImFont* font = g_hud_font ? g_hud_font : ImGui::GetFont();
  const float size = std::clamp(h * 0.095f, 26.0f, 140.0f), strip = size * 1.6f;
  dl->AddRectFilled(ImVec2(0, h - strip), ImVec2(w, h), rgb(0xE2E8F0));
  dl->AddLine(ImVec2(0, h - strip), ImVec2(w, h - strip), rgb(0xCBD5E1), std::max(1.0f, size * 0.03f));
  for (int k = 0; k < count; ++k) {
    const int port = ports[k];
    const PlayerFrame& p = f.players[port][0];
    const uint32_t colour = player_color(info, port, false);
    const ImU32 col = rgb(colour), dark = rgb(0x0F172A, 0.22f);
    const float cx = w * (k + 1) / (count + 1);
    char number[16];
    std::snprintf(number, sizeof number, "%d", (int)std::floor(std::max(0.0f, p.percent)));
    const float small = size * 0.55f;
    const ImVec2 ns = font->CalcTextSizeA(size, FLT_MAX, 0, number), ps = font->CalcTextSizeA(small, FLT_MAX, 0, "%");
    const float top = h - strip + size * 0.08f, left = cx - (ns.x + ps.x) / 2;
    const float shadow = std::max(1.0f, size * 0.025f);
    dl->AddText(font, size, ImVec2(left + shadow, top + shadow), dark, number);
    dl->AddText(font, size, ImVec2(left, top), col, number);
    const ImVec2 pct(left + ns.x + size * 0.03f, top + ns.y - ps.y - size * 0.06f);
    dl->AddText(font, small, ImVec2(pct.x + shadow * 0.7f, pct.y + shadow * 0.7f), dark, "%");
    dl->AddText(font, small, pct, col, "%");
    const float r = size * 0.09f, gap = r * 2.7f;
    const float sx = cx - gap * (p.stocks - 1) / 2.0f, sy = top + ns.y + r * 1.4f;
    for (int s = 0; s < p.stocks && s < 8; ++s) dl->AddCircleFilled(ImVec2(sx + gap * s, sy), r, col);
  }
}

std::atomic<bool> g_covering{false};

}  // namespace

bool covering() { return g_covering.load(std::memory_order_relaxed); }

void draw(bool enabled, float width, float height) {
  g_covering.store(false, std::memory_order_relaxed);
  if (!enabled || width <= 0 || height <= 0 || !match_in_progress()) return;
  GameInfo info;
  Frame f;
  uint64_t serial;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    info = g_info;
    f = g_shown;
    serial = g_match_serial;
  }
  update_camera(f, serial);
  if (!g_camera.valid) return;
  g_covering.store(true, std::memory_order_relaxed);

  // Slippi Lab's viewBox is 730 x 600 world-scaled units around the camera center; here that box
  // is fitted to the window, and a wider window simply shows more of the stage to the sides.
  const float fit = std::min(width / 730.0f, height / 600.0f);
  const View v{width / 2, height / 2, g_camera.scale * fit, g_camera.cx, g_camera.cy};

  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  // Covers the game image entirely; the game still renders underneath (and the other player's
  // game is untouched), this only decides what this window shows.
  dl->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), rgb(kBackground));
  const int stage_style = g_stage_style.load(std::memory_order_relaxed);
  const int fighter_style = g_fighter_style.load(std::memory_order_relaxed);
  if (stage_style == 1 && !f.stage_lines.empty()) draw_stage_lines(dl, v, f);
  else if (stage_style == 2 && !f.stage_lines.empty()) draw_stage_blocks(dl, v, f);
  else draw_stage(dl, v, info, f);
  for (int port = 0; port < 4; ++port)
    for (int fol = 0; fol < 2; ++fol) {
      const PlayerFrame& p = f.players[port][fol];
      if (!p.present) continue;
      const auto& caps = f.capsules[port][fol];
      if (fighter_style != 0 && !caps.empty()) {
        if (fighter_style == 1) draw_stick_figure(dl, v, caps, f.bones[port][fol], player_color(info, port, fol == 1));
        else draw_capsule_fighter(dl, v, caps, f.bones[port][fol], rgb(player_color(info, port, fol == 1)));
      } else {
        draw_player(dl, v, info, p, port, fol == 1);
      }
      draw_player_signals(dl, v, info, p, caps, port, fol == 1);
    }
  draw_items(dl, v, f);
  draw_hud(dl, info, f, width, height);
}

void set_style(int fighters, int stage) {
  g_fighter_style.store(fighters, std::memory_order_relaxed);
  g_stage_style.store(stage, std::memory_order_relaxed);
}

void set_hud_font(ImFont* font) { g_hud_font = font; }

bool styles_on() { return g_fighter_style.load(std::memory_order_relaxed) != 0; }

}  // namespace lab
