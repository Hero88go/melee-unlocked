// SPDX-License-Identifier: GPL-2.0-or-later
#include "practice_cpu.h"

#include "host.h"
#include "ppc.h"
#ifndef MELEE_NO_SLIPPI
#include "slippi_online.h"
#include "slippi_playback.h"
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

// The decision model, compiled here for the Static Recomp (and for the menu's presets).
#include "../abi/mu_practice_cpu_impl.inl"
#include "practice_ghosts.h"
#include "ghost_ai.h"

namespace practice_cpu {
namespace {
std::mutex g_mutex;
MuPcConfig g_config = [] {
  MuPcConfig c;
  mu_pc_defaults(&c);
  // MELEE_TEST_PRACTICE_CPU=<preset> (tests only): on, with that preset, without a settings file.
  if (const char* v = std::getenv("MELEE_TEST_PRACTICE_CPU")) {
    mu_pc_preset(std::atoi(v), &c);
    c.enabled = 1;
  }
  // MELEE_TEST_PRACTICE_GHOST=<index> (tests only): on, playing that habit ghost.
  if (const char* v = std::getenv("MELEE_TEST_PRACTICE_GHOST")) {
    if (practice_cpu::ghost(std::atoi(v), c)) c.enabled = 1;
  }
  return c;
}();
std::atomic<uint32_t> g_generation{1};
std::atomic<bool> g_driving{false};
std::atomic<int> g_plan{0};
std::atomic<uint32_t> g_status_frame{0};
}  // namespace

static_assert(MU_PC_MOVE_COUNT == 10 && MU_PC_ATK_COUNT == 20 && MU_PC_DEF_COUNT == 6 && MU_PC_TECH_COUNT == 4 &&
                  MU_PC_GETUP_COUNT == 5 && MU_PC_LEDGE_COUNT == 6 && MU_PC_OOS_COUNT == 10 && MU_PC_DI_COUNT == 6 &&
                  MU_PC_AFTER_COUNT == 5,
              "practice_ghosts.h (tools/ghost_build.py) is made for these option counts");

bool ghost(int index, MuPcConfig& c) {
  if (index < 0 || index >= (int)(sizeof practice_ghosts::kGhosts / sizeof practice_ghosts::kGhosts[0])) return false;
  const practice_ghosts::Ghost& gh = practice_ghosts::kGhosts[index];
  const uint8_t tools = c.tools;
  mu_pc_defaults(&c);
  const auto take = [](uint8_t* dst, const uint8_t* src, int n) {   // a group with no data keeps the default
    int sum = 0;
    for (int i = 0; i < n; ++i) sum += src[i];
    if (sum) std::memcpy(dst, src, (size_t)n);
  };
  take(c.move_odds, gh.move, MU_PC_MOVE_COUNT);
  take(c.attack_odds, gh.attack, MU_PC_ATK_COUNT);
  take(c.defend_odds, gh.defend, MU_PC_DEF_COUNT);
  take(c.oos_odds, gh.oos, MU_PC_OOS_COUNT);
  take(c.di_odds, gh.di, MU_PC_DI_COUNT);
  take(c.tech_odds, gh.tech, MU_PC_TECH_COUNT);
  take(c.getup_odds, gh.getup, MU_PC_GETUP_COUNT);
  take(c.ledge_odds, gh.ledge, MU_PC_LEDGE_COUNT);
  take(c.after_odds, gh.after, MU_PC_AFTER_COUNT);
  if (gh.l_cancel) c.l_cancel = gh.l_cancel;
  if (gh.aggression) c.aggression = gh.aggression;
  c.tools = tools;
  // With the pro AI file beside the game, that pro's network plays; the habits above stay as the fallback.
    c.preset = ghost_ai::specialist_available((unsigned)index) ? (uint8_t)MU_PC_PRESET_PRO(index) : 0;
  return true;
}

MuPcConfig config() { std::lock_guard<std::mutex> lk(g_mutex); return g_config; }
void initialize_source_test_ghost() {
  if (const char* v = std::getenv("MELEE_TEST_PRACTICE_GHOST")) {
    MuPcConfig c = config();
    if (ghost(std::atoi(v), c)) {
      c.enabled = 1;
      set_config(c);
      host::log("practice test: ghost %d selected after Source initialization (preset %u)", std::atoi(v), c.preset);
    }
  }
}
void set_config(const MuPcConfig& c) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (std::memcmp(&g_config, &c, sizeof c) == 0) return;
  g_config = c;
  g_config.version = (uint8_t)MU_PC_CONFIG_VERSION;
  ++g_generation;
}
uint32_t generation() { return g_generation.load(); }

std::string to_text(const MuPcConfig& c) {
  static const char* hex = "0123456789abcdef";
  const auto* p = reinterpret_cast<const uint8_t*>(&c);
  std::string s;
  s.reserve(sizeof c * 2);
  for (size_t i = 0; i < sizeof c; ++i) { s += hex[p[i] >> 4]; s += hex[p[i] & 15]; }
  return s;
}
bool from_text(const std::string& text, MuPcConfig& c) {
  if (text.size() != sizeof(MuPcConfig) * 2) return false;
  MuPcConfig out;
  auto* p = reinterpret_cast<uint8_t*>(&out);
  for (size_t i = 0; i < sizeof out; ++i) {
    auto nib = [](char h) -> int { return h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : -1; };
    const int hi = nib(text[i * 2]), lo = nib(text[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    p[i] = (uint8_t)(hi << 4 | lo);
  }
  if (out.version != MU_PC_CONFIG_VERSION) return false;
  c = out;
  return true;
}

namespace {
std::atomic<int> g_kind{-1};
MuPcConfig g_slots[kSlots];
bool g_slot_valid[kSlots] = {};
}  // namespace

Status status() {
  Status s;
  // No time limit: the Practice panel pauses the match, and it should still say what the CPU was
  // doing. The game reports "not driving" itself whenever it stops.
  s.driving = g_driving.load();
  s.plan = g_plan.load();
  s.frame = g_status_frame.load();
  s.kind = g_kind.load();
  return s;
}
void note_status(bool driving, int plan, uint32_t frame, int kind) {
  g_driving = driving;
  if (plan != 0) g_plan = plan;   // between two actions it shows the last one, never "none"
  g_status_frame = host::retrace_count();
  if (kind >= 0) g_kind = kind;
  (void)frame;
}

bool slot(int index, MuPcConfig& out) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (index < 0 || index >= kSlots || !g_slot_valid[index]) return false;
  out = g_slots[index];
  return true;
}
void set_slot(int index, const MuPcConfig& c) {
  std::lock_guard<std::mutex> lk(g_mutex);
  if (index < 0 || index >= kSlots) return;
  g_slots[index] = c;
  g_slot_valid[index] = true;
}

const char* special_name(int kind, int which) {
  // Neutral, side, up and down special per character (FighterKind order).
  static const char* const names[27][4] = {
      {"Fireball", "Cape", "Super Jump Punch", "Mario Tornado"},
      {"Blaster", "Fox Illusion", "Fire Fox", "Reflector (shine)"},
      {"Falcon Punch", "Raptor Boost", "Falcon Dive", "Falcon Kick"},
      {"Giant Punch", "Headbutt", "Spinning Kong", "Hand Slap"},
      {"Inhale", "Hammer", "Final Cutter", "Stone"},
      {"Fire Breath", "Koopa Klaw", "Whirling Fortress", "Bowser Bomb"},
      {"Bow", "Boomerang", "Spin Attack", "Bomb"},
      {"Needle Storm", "Chain", "Vanish", "Transform"},
      {"PK Flash", "PK Fire", "PK Thunder", "PSI Magnet"},
      {"Toad", "Peach Bomber", "Parasol", "Vegetable"},
      {"Ice Shot", "Squall Hammer", "Belay", "Blizzard"},
      {"Ice Shot", "Squall Hammer", "Belay", "Blizzard"},
      {"Thunder Jolt", "Skull Bash", "Quick Attack", "Thunder"},
      {"Charge Shot", "Missile", "Screw Attack", "Bomb"},
      {"Egg Lay", "Egg Roll", "Egg Throw", "Yoshi Bomb"},
      {"Rollout", "Pound", "Sing", "Rest"},
      {"Shadow Ball", "Confusion", "Teleport", "Disable"},
      {"Fireball", "Green Missile", "Super Jump Punch", "Luigi Cyclone"},
      {"Shield Breaker", "Dancing Blade", "Dolphin Slash", "Counter"},
      {"Nayru's Love", "Din's Fire", "Farore's Wind", "Transform"},
      {"Fire Bow", "Boomerang", "Spin Attack", "Bomb"},
      {"Megavitamin", "Super Sheet", "Super Jump Punch", "Dr. Tornado"},
      {"Blaster", "Falco Phantasm", "Fire Bird", "Reflector (shine)"},
      {"Thunder Jolt", "Skull Bash", "Agility", "Thunder"},
      {"Chef", "Judgment", "Fire", "Oil Panic"},
      {"Warlock Punch", "Gerudo Dragon", "Dark Dive", "Wizard's Foot"},
      {"Flare Blade", "Double-Edged Dance", "Blazer", "Counter"},
  };
  if (kind < 0 || kind >= 27 || which < 0 || which > 3) return nullptr;
  return names[kind][which];
}

// ---------------- Static Recomp ----------------
namespace {
constexpr uint32_t kSceneState = 0x80479D30;
constexpr uint8_t kTrainingMajor = 0x1C;
constexpr uint32_t kPlayerSlots = 0x80453080, kPlayerStride = 0xE90;
constexpr uint32_t kStageKind = 0x8049E750;   // stage_info.grkind
constexpr uint32_t kProcInput = 0x8006AD10;   // Fighter_procInput(gobj)

float rdf(uint32_t a) { const uint32_t v = host::rd32(a); float f; std::memcpy(&f, &v, 4); return f; }
uint32_t fighter_of(uint32_t gobj) { return gobj ? host::rd32(gobj + 0x2C) : 0; }

bool can_act(uint32_t fp, int motion, bool hitlag, bool hitstun) {
  if (hitlag || hitstun) return false;
  if (host::rd8(fp + 0x2218) & 0x80) return true;   // allow_interrupt
  return (motion >= 14 && motion <= 21) || (motion >= 27 && motion <= 34) || motion == 39 || motion == 40 ||
         motion == 38 || motion == 179;
}

void read_fighter(uint32_t fp, MuPcFighter& f) {
  std::memset(&f, 0, sizeof f);
  if (!fp) return;
  f.present = 1;
  f.kind = (int32_t)host::rd32(fp + 0x4);
  f.motion = (int32_t)host::rd32(fp + 0x10);
  f.facing = rdf(fp + 0x2C);
  f.x = rdf(fp + 0xB0);
  f.y = rdf(fp + 0xB4);
  f.vx = rdf(fp + 0x80) + rdf(fp + 0x8C);
  f.vy = rdf(fp + 0x84) + rdf(fp + 0x90);
  f.airborne = host::rd32(fp + 0xE0) != 0;
  f.percent = rdf(fp + 0x1830);
  f.shield = rdf(fp + 0x1998);
  f.hitlag = rdf(fp + 0x195C) > 0.0f;
  f.hitstun = (host::rd8(fp + 0x221C) & 0x02) != 0;   // x221C_b6
  const int max_jumps = (int)host::rd32(fp + 0x168);
  const int used = host::rd8(fp + 0x1968);
  f.jumps_left = (uint8_t)(max_jumps > used ? max_jumps - used : 0);
  f.can_act = can_act(fp, f.motion, f.hitlag, f.hitstun);
}

int8_t stick8(float v) {
  v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
  return (int8_t)std::lround(v > 0 ? v * 127.0f : v * 128.0f);
}

MuPcState g_state[4];
uint32_t g_state_session[4];
uint32_t g_session = 0;
uint8_t g_last_major = 0;
int g_logs = 0;

void on_proc_input(ppc::Context& c) {
#ifdef MELEE_NO_SLIPPI
  (void)c;   // only the Source Port exists in that build, and it runs the model in its game code
}
#else
  if (host::game_image) return;   // the Source Port runs the model in its game code
  const uint8_t major = host::rd8(kSceneState);
  if (major != g_last_major) { g_last_major = major; ++g_session; }
  static const bool any_mode = std::getenv("MELEE_PC_ANY_MODE") != nullptr;   // tests only: any offline match
  if (major != kTrainingMajor && !any_mode) return;
  if (slippi::online::is_online_match() || slippi::playback::enabled()) return;
  const MuPcConfig cfg = config();
  if (!cfg.enabled) return;
  const uint32_t fp = fighter_of(c.r[3]);
  if (!fp) return;
  const uint8_t idx = host::rd8(fp + 0xC);
  if (idx >= 4 || (host::rd8(fp + 0x221F) & 0x08)) return;   // a sub fighter (Nana) follows its own AI
  const uint32_t slot = kPlayerSlots + idx * kPlayerStride;
  // MELEE_PC_TEST_P2_CPU (tests only, with MELEE_PC_ANY_MODE): player 2 of a scripted two-human VS
  // match becomes a CPU, so the practice CPU can be checked without scripting the CPU badge.
  static const bool p2_cpu = any_mode && std::getenv("MELEE_PC_TEST_P2_CPU") != nullptr;
  if (p2_cpu && idx == 1 && host::rd32(slot + 0x8) == 0) host::wr32(slot + 0x8, 1);
  if (host::rd32(slot + 0x8) != 1) return;   // pkind: CPU only
  // The opponent: the first human player in the match.
  uint32_t opp = 0;
  for (uint32_t k = 0; k < 4 && !opp; ++k) {
    const uint32_t s = kPlayerSlots + k * kPlayerStride;
    if (k == idx || host::rd32(s + 0x8) != 0) continue;
    opp = fighter_of(host::rd32(s + 0xB0));
  }
  MuPcView v;
  std::memset(&v, 0, sizeof v);
  read_fighter(fp, v.cpu);
  read_fighter(opp, v.opp);
  if (!v.opp.present) v.opp = v.cpu;
  mu_pc_stage_ledge((int)host::rd32(kStageKind), &v.ledge_x, &v.ledge_y);
  v.frame = host::retrace_count();
  // No floor query here: the main floor, when it is below and the CPU is over the stage.
  v.land_frames = -1;
  if (v.cpu.airborne && v.cpu.vy < 0.0f && std::fabs(v.cpu.x) <= v.ledge_x && v.cpu.y >= v.ledge_y - 1.0f)
    v.land_frames = (int32_t)((v.cpu.y - v.ledge_y) / -v.cpu.vy);
  if (g_state_session[idx] != g_session) {
    mu_pc_reset(&g_state[idx], 0x9E3779B9u ^ (g_session * 2654435761u) ^ idx);
    g_state_session[idx] = g_session;
  }
  MuPcInput in;
  const int driven = mu_pc_frame(&cfg, &g_state[idx], &v, &in);
  note_status(driven != 0, g_state[idx].plan, v.frame, v.cpu.kind);
  if (!driven) return;
  // CpuFighter at fp+0x1A88: buttons, lstick, cstick (s8), ltrigger, rtrigger (u8). Fighter_procInput
  // reads a CPU's controller from there, so writing it at entry is what the CPU holds this frame.
  const uint32_t cpu = fp + 0x1A88;
  host::wr32(cpu + 0x0, in.buttons);
  host::wr8(cpu + 0x4, (uint8_t)stick8(in.lx));
  host::wr8(cpu + 0x5, (uint8_t)stick8(in.ly));
  host::wr8(cpu + 0x6, (uint8_t)stick8(in.cx));
  host::wr8(cpu + 0x7, (uint8_t)stick8(in.cy));
  host::wr8(cpu + 0x8, (uint8_t)std::lround(std::fmin(1.0f, std::fmax(0.0f, in.l)) * 255.0f));
  host::wr8(cpu + 0x9, (uint8_t)std::lround(std::fmin(1.0f, std::fmax(0.0f, in.r)) * 255.0f));
  if (std::getenv("MELEE_PC_LOG") && g_logs < 400 && g_state[idx].plan != g_state[idx].last_logged) {
    ++g_logs;
    g_state[idx].last_logged = g_state[idx].plan;
    host::log("practice cpu: frame %u plan %s, motion %d, x %.1f", v.frame, mu_pc_plan_name(g_state[idx].plan),
              v.cpu.motion, v.cpu.x);
  }
}
#endif
}  // namespace

void install_static() {
  if (host::game_image) return;
  ppc::add_entry_hook(kProcInput, on_proc_input);
}
}  // namespace practice_cpu
