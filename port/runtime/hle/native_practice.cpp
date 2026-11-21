#include "test_env.h"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_practice.h"
#include "slippi_arena.h"
#include "slippi_warmup.h"

#include "host.h"
#include "audio.h"
#include "ghost_ai.h"
#include "practice_cpu.h"
#include "practice_ghosts.h"
#include "slippi_online.h"
#include "yougame_link.h"
#include "yougame_console_engine.h"
#include "yougame_console_entry.h"
#include "yougame_unranked.h"
#include "yougame_ux.h"
#include "yougame_game_state.h"
#include "yougame_friends.h"
#include "yougame_lobby_wire.h"
#include "yougame_mode_policy.h"
#include "../abi/mu_tourney.h"
#include "slippi_playback.h"
#include "../gx/render_options.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>
#include <string>

namespace slippi::native_practice {
namespace {

constexpr uint32_t kSceneState = 0x80479D30;
constexpr uint32_t kMinorSceneControl = 0x80479D58;
constexpr uint8_t kOnlineMajor = 8;
constexpr uint8_t kTrainingMajor = 0x1C;
constexpr uint8_t kMainMenuMajor = 0x01;
// OFST_R13_ONLINE_MODE is -0x5060 from Melee's r13 base (0x804DB6A0).
constexpr uint32_t kOnlineMode = 0x804D6640;
// OFST_R13_ISWINNER (-0x5037) and OFST_R13_CHOSESTAGE (-0x5036). Normal Direct
// initializes these when Start begins a search; the native search begins outside that CSS path.
constexpr uint32_t kDirectIsWinner = 0x804D6669;
constexpr uint32_t kDirectChoseStage = 0x804D666A;
constexpr uint32_t kEventBackup = 0x8045A6C0 + 0x532;
constexpr uint32_t kPlayerSlots = 0x80453080;
constexpr uint32_t kPlayerStride = 0xE90;
constexpr uint32_t kMatchStage = 0x8046B6A0 + 0x24C8 + 0x0E;
constexpr uint32_t kSearchTimeoutTicks = 60 * 90;

enum class CommandKind { StartUnranked, StartDirect, StartTeams, Cancel, AcknowledgeFailure, TrainAgainst, Warmup, WarmupReturn,
                         TestTraining };
struct Command { CommandKind kind; std::string text; };

struct SavedPlayer {
  bool active = false;
  uint32_t state = 0;
  uint32_t character = 0;
  uint32_t player_kind = 3;
  uint8_t costume = 0;
  uint8_t controller = 0;
  uint8_t cpu_level = 0;
  uint16_t damage = 0;
  uint16_t initial_damage = 0;
};

struct PracticeConfig {
  bool valid = false;
  int local_slot = 0;
  int controller_port = 0;
  uint16_t stage = 0;
  std::array<SavedPlayer, 4> players{};
};

std::mutex g_mutex;
std::deque<Command> g_commands;
Snapshot g_snapshot;
Lifecycle g_lifecycle;
PracticeConfig g_practice;
std::string g_connect_code;
std::string g_opponent;
MatchMode g_match_mode = MatchMode::None;
int g_matchmaking_state = 0;
uint32_t g_search_ticks = 0;
uint32_t g_generation = 0;
uint32_t g_return_ticks = 0;
constexpr uint32_t kReturnTimeoutTicks = 600;   // ten seconds of frames
uint32_t g_return_wait_ticks = 0;   // since the return was asked for, whatever scene the game is in
uint8_t g_origin_major = 1;
bool g_restore_training = false;
// L+R on the online character select: the search waits for the Training match, then starts.
MatchMode g_auto_search = MatchMode::None;
std::string g_auto_code;   // Direct: the code it searches for
uint32_t g_auto_ticks = 0;
Phase g_logged_phase = Phase::Idle;
bool g_lobby_started = false;
uint32_t g_lobby_boot_ticks = 0;
bool g_friend_handoff = false;
bool g_friend_css_requested = false;
bool g_shared_css_seen = false;
bool g_shared_was_active = false;
uint32_t g_friend_menu_ticks = 0;
std::future<std::string> g_friend_picker;
std::shared_ptr<std::atomic<bool>> g_friend_picker_stop;
// After an online session (the other player left, the connection dropped, the player left): back
// into a Training match with the same character, then "search again?". g_enter_* presses through
// Training's character and stage select for the player; g_offer_* is the prompt.
int g_last_opponent = -1;            // the last online opponent's character (Training's CPU after the session)
uint8_t g_last_opponent_costume = 0;
bool g_enter_training = false;
bool g_enter_seen_select = false;   // Training's character select was reached (a restart starts in the match)
int g_enter_port = 0;
uint32_t g_enter_ticks = 0;
MatchMode g_offer_mode = MatchMode::None;
std::string g_offer_code;
std::string g_offer_reason;
bool g_offer_shown = false;

// The Source Port's game answers through its practice bridge (game API 6); the static recomp's
// state is read and written at its console addresses.
NativeBridge g_bridge = nullptr;
ContentBridge g_content_bridge = nullptr;
enum { OP_SCENE, OP_GET_PLAYER, OP_SET_PLAYER, OP_GET_STAGE, OP_SET_STAGE, OP_SET_EVENT_BACKUP,
       OP_REQUEST_MAJOR, OP_SET_ONLINE_MODE, OP_GET_ONLINE_MODE, OP_DIRECT_FIRST_MATCH, OP_FRAME_PAUSE, OP_CPU_MOVE,
       OP_TRAINING_PICK, OP_TRAINING_STAGE, OP_UNLOCKED, OP_PAGE_PAUSE, OP_CSS_PICK, OP_FRIENDS_RETURN };
static_assert(OP_FRAME_PAUSE == 10 && OP_UNLOCKED == 14 && OP_PAGE_PAUSE == 15 && OP_CSS_PICK == 16 &&
              OP_FRIENDS_RETURN == 17, "practice op numbering (mu_host.h)");
std::atomic<uint32_t> g_unlocked{0xFFFFFFFFu};   // tick(): the card's unlocked characters, read in Training

// ---- warm-up room state (the room itself is below, "warm-up room") ----
warmup::Room g_warmup;
uint8_t g_warmup_mode = 0;            // the online mode byte to give back on the way home
PracticeConfig g_warmup_css;          // the player's pick on the online select (character, costume, port)
bool g_warmup_cpu_saved = false;      // the player's own practice CPU, put back when the room ends
MuPcConfig g_warmup_saved_cpu{};
bool g_warmup_cpu_applied = false;    // the sparring partner was set for this stay
int g_warmup_cpu_char = -1;
uint8_t g_warmup_cpu_costume = 0;
std::string g_warmup_sparring;
uint32_t g_warmup_match_ticks = 0;
float g_warmup_hold = 0.0f;
bool g_test_press_start = false;   // tests (MELEE_TEST_WARMUP): Start on the online select until locked in
// Everyone else locked in while the player trained: back at once, and Start on the select for the
// player as soon as it takes it (the winner locks in, the loser goes to pick the stage).
bool g_warmup_autostart = false;
uint32_t g_warmup_autostart_ticks = 0;
// Back on the select after the opponent left during the warm-up: the player asked for the select, so
// "back to Training after a session" does not send them straight back.
bool g_warmup_no_bounce = false;
// The way back through the main menu (Source Port, from a match of another mode): the select is asked
// for once the menu is up (warmup_tick). The retrace the menu was asked at, 0 none.
uint32_t g_warmup_via_menu = 0;

uint64_t steady_ms() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool warmup_offer(bool* arena, std::string* why);

uint8_t current_major() {
  if (g_bridge) { int32_t a[2] = {}; g_bridge(OP_SCENE, a, 2); return (uint8_t)a[0]; }
  return host::rd8(kSceneState);
}
uint8_t current_minor() {
  if (g_bridge) { int32_t a[2] = {}; g_bridge(OP_SCENE, a, 2); return (uint8_t)a[1]; }
  return host::rd8(kSceneState + 3);
}
void set_online_mode(uint8_t mode) {
  if (g_bridge) { int32_t a[1] = {mode}; g_bridge(OP_SET_ONLINE_MODE, a, 1); return; }
  host::wr8(kOnlineMode, mode);
}
uint8_t online_mode() {
  if (g_bridge) { int32_t a[1] = {}; g_bridge(OP_GET_ONLINE_MODE, a, 1); return (uint8_t)a[0]; }
  return host::rd8(kOnlineMode);
}

bool is_training() { return current_major() == kTrainingMajor; }
bool is_online_scene() { return current_major() == kOnlineMajor; }
// Any offline match where the player already picked a character: VS, Training, single player,
// events. The handoff then goes through Slippi's online character select.
bool is_offline_gameplay() {
  if (!is_offline_gameplay_scene(current_major(), current_minor())) return false;
  // Source Port: a match found while searching from a VS match stopped both players' games as
  // the online character select opened (mncharsel.c, its KO-star setup reads the VS match's data;
  // 0.9.56 too). Training and events hand off cleanly, so the search starts there only. The
  // Static Recomp hands off from VS matches without trouble and keeps them.
  if (g_bridge && current_major() != kTrainingMajor && current_major() != 0x2B) return false;
  return true;
}

PracticeConfig capture_practice() {
  PracticeConfig out;
  if (g_bridge) {
    int32_t st[1] = {};
    g_bridge(OP_GET_STAGE, st, 1);
    out.stage = (uint16_t)st[0];
    bool found = false;
    for (int i = 0; i < 4; ++i) {
      int32_t a[9] = {i};
      g_bridge(OP_GET_PLAYER, a, 9);
      SavedPlayer& p = out.players[i];
      p.state = (uint32_t)a[1]; p.character = (uint32_t)a[2]; p.player_kind = (uint32_t)a[3];
      p.costume = (uint8_t)a[4]; p.controller = (uint8_t)a[5]; p.cpu_level = (uint8_t)a[6];
      p.damage = (uint16_t)a[7]; p.initial_damage = (uint16_t)a[8];
      p.active = p.state == 2 || p.state == 3;
      if (!found && p.active && p.player_kind == 0) {
        found = true;
        out.local_slot = i;
        out.controller_port = p.controller < 4 ? p.controller : 0;
      }
    }
    out.valid = found;
    return out;
  }
  out.stage = host::rd16(kMatchStage);
  bool found_human = false;
  for (int i = 0; i < 4; ++i) {
    const uint32_t base = kPlayerSlots + (uint32_t)i * kPlayerStride;
    SavedPlayer& p = out.players[i];
    p.state = host::rd32(base);
    p.character = host::rd32(base + 4);
    p.player_kind = host::rd32(base + 8);
    p.active = p.state == 2 || p.state == 3;
    p.costume = host::rd8(base + 0x44);
    p.controller = host::rd8(base + 0x48);
    p.cpu_level = host::rd8(base + 0x49);
    p.damage = host::rd16(base + 0x5C);
    p.initial_damage = host::rd16(base + 0x5E);
    if (!found_human && p.active && p.player_kind == 0) {
      found_human = true;
      out.local_slot = i;
      out.controller_port = p.controller < 4 ? p.controller : 0;
    }
  }
  out.valid = found_human;
  return out;
}

// Training's character select starts from its saved match. slot 0: the player, 1: the CPU. The Source
// Port asks its game; the Static Recomp writes the same saved match in console memory: the save
// data (gmMainLib_804D3EE0, at 0x8045A6C0) holds a VsModeData (0x140 bytes) per VS mode from +0x590,
// Training is mode 6, its StartMeleeData starts 8 bytes in, and the player blocks (0x24 bytes:
// character +0, costume +3) follow the 0x60 bytes of rules, as in a Slippi replay's game info block.
void set_training_pick(int slot, int character, int costume) {
  if (character < 0 || character >= 26 || slot < 0 || slot > 1) return;
  if (g_bridge) {
    int32_t a[3] = {slot, character, costume};
    g_bridge(OP_TRAINING_PICK, a, 3);
    return;
  }
  // Costumes per character kind (external order): a costume the character does not have is the first.
  static const uint8_t kCostumes[26] = {6, 5, 4, 4, 6, 4, 5, 4, 5, 5, 4, 4, 5, 4, 4, 5, 5, 6, 5, 5, 4, 5, 5, 5, 4, 5};
  constexpr uint32_t kTrainingPlayers = 0x8045A6C0u + 0x590u + 6u * 0x140u + 8u + 0x60u;
  const uint32_t at = kTrainingPlayers + (uint32_t)slot * 0x24u;
  const uint8_t was = host::rd8(at);
  if (was > 0x21 && was != 0xFF) return;   // neither a character kind nor empty: not the layout this expects
  host::wr8(at, (uint8_t)character);
  host::wr8(at + 3, (uint8_t)(costume >= 0 && costume < kCostumes[character] ? costume : 0));
}

void write_event_backup() {
  if (!g_practice.valid) return;
  const SavedPlayer& p = g_practice.players[g_practice.local_slot];
  if (g_bridge) {
    int32_t a[3] = {(int32_t)p.character, p.costume, g_practice.controller_port};
    g_bridge(OP_SET_EVENT_BACKUP, a, 3);
    return;
  }
  host::wr8(kEventBackup + 0, (uint8_t)p.character);
  host::wr8(kEventBackup + 1, p.costume);
  host::wr8(kEventBackup + 4, (uint8_t)g_practice.controller_port);
}

void restore_practice_fields() {
  if (!g_practice.valid) return;
  if (g_bridge) {
    int32_t st[1] = {g_practice.stage};
    g_bridge(OP_SET_STAGE, st, 1);
    for (int i = 0; i < 4; ++i) {
      const SavedPlayer& p = g_practice.players[i];
      if (!p.active) continue;
      int32_t a[9] = {i, (int32_t)p.state, (int32_t)p.character, (int32_t)p.player_kind, p.costume,
                      p.controller, p.cpu_level, (int16_t)p.damage, (int16_t)p.initial_damage};
      g_bridge(OP_SET_PLAYER, a, 9);
    }
    return;
  }
  host::wr16(kMatchStage, g_practice.stage);
  for (int i = 0; i < 4; ++i) {
    const SavedPlayer& p = g_practice.players[i];
    if (!p.active) continue;
    const uint32_t base = kPlayerSlots + (uint32_t)i * kPlayerStride;
    host::wr32(base + 4, p.character);
    host::wr32(base + 8, p.player_kind);
    host::wr8(base + 0x44, p.costume);
    host::wr8(base + 0x48, p.controller);
    host::wr8(base + 0x49, p.cpu_level);
    host::wr16(base + 0x5C, p.damage);
    host::wr16(base + 0x5E, p.initial_damage);
  }
}

// The game's debug pause (gm_80479D58.unk_10.x0 bit 0) and its one-frame step (x2 bit 0).
// pause: 1 freeze, 0 run, -1 leave as is. Returns whether the game is frozen.
bool frame_pause(int pause, bool step) {
  if (g_bridge) { int32_t a[2] = {pause, step ? 1 : 0}; g_bridge(OP_FRAME_PAUSE, a, 2); return a[0] != 0; }
  const uint32_t flags = kMinorSceneControl + 0x10;
  uint8_t v = host::rd8(flags);
  if (pause > 0) v |= 1; else if (pause == 0) v &= (uint8_t)~1u;
  host::wr8(flags, v);
  if (step) host::wr8(flags + 2, (uint8_t)(host::rd8(flags + 2) | 1));
  return (v & 1) != 0;
}

// Moves the CPU fighter by (dx, dy) game units: the native game through its bridge, the Static
// Recomp in console memory (player slots at 0x80453080, 0xE90 each: kind +0x8 (1 CPU), fighter
// object +0xB0; the fighter's data at object +0x2C, its position at data +0xB0, previous +0xBC).
void cpu_move(float dx, float dy) {
  if (g_bridge) { int32_t a[2] = {(int32_t)(dx * 100.0f), (int32_t)(dy * 100.0f)}; g_bridge(OP_CPU_MOVE, a, 2); return; }
  for (uint32_t i = 0; i < 4; ++i) {
    const uint32_t slot = 0x80453080u + i * 0xE90u;
    if (host::rd32(slot + 8) != 1) continue;
    const uint32_t gobj = host::rd32(slot + 0xB0);
    if (gobj < 0x80000000u || gobj >= 0x81800000u) continue;
    const uint32_t fp = host::rd32(gobj + 0x2C);
    if (fp < 0x80000000u || fp >= 0x81800000u) continue;
    for (int axis = 0; axis < 2; ++axis) {
      uint32_t bits = host::rd32(fp + 0xB0 + axis * 4);
      float v; std::memcpy(&v, &bits, 4);
      v += axis ? dy : dx;
      std::memcpy(&bits, &v, 4);
      host::wr32(fp + 0xB0 + axis * 4, bits);
      host::wr32(fp + 0xBC + axis * 4, bits);
      if (axis == 0 && host::test_env::getenv("MELEE_TEST_FRAME_ADVANCE")) host::log("practice: CPU moved to x %.1f", v);
    }
    return;
  }
}

// Frame advance (Practice > Tools): in an offline Training match, L+R+Z freezes the game and
// unfreezes it; while frozen Z steps one frame, and held Z keeps stepping (every fourth frame
// after a short wait). Anything else (leaving Training, a search starting, the option off)
// lets the game run again.
void frame_advance_tick() {
  static bool frozen = false, combo_was = false, z_was = false;
  static uint32_t z_held = 0;
  // Tests (hidden VS matches): MELEE_TEST_FRAME_ADVANCE=1 turns the option on outside Training.
  static const bool test = host::test_env::getenv("MELEE_TEST_FRAME_ADVANCE") != nullptr;
  const bool allowed = ((practice_cpu::config().tools & MU_PC_TOOL_FRAME_ADVANCE) || test) && (is_training() || test) &&
                       (test ? is_offline_gameplay_scene(current_major(), current_minor()) : is_offline_gameplay()) && g_lifecycle.phase() == Phase::Idle && !slippi::playback::enabled() &&
                       slippi::online::session_mode() < 0;
  if (!allowed) {
    if (test) {   // tests: say which condition holds frame advance off when L+R+Z is pressed
      host::PadState pads[4];
      host::input_last_pads(pads);
      static uint32_t told = 0;
      if ((pads[0].button & 0x0070u) == 0x0070u && told++ < 3)
        host::log("practice: frame advance held off: scene %02X/%02X phase %s playback %d session %d",
                  current_major(), current_minor(), phase_name(g_lifecycle.phase()), (int)slippi::playback::enabled(),
                  slippi::online::session_mode());
    }
    if (frozen) { frame_pause(0, false); frozen = false; host::log("practice: frame advance off, the game runs"); }
    combo_was = z_was = false;
    return;
  }
  host::PadState pads[4];
  host::input_last_pads(pads);
  bool combo = false, z = false;
  for (const host::PadState& p : pads) {
    if ((p.button & 0x0070u) == 0x0070u) combo = true;           // L + R + Z
    else if ((p.button & 0x0010u) && !(p.button & 0x0060u)) z = true;
  }
  // Tests: MELEE_TEST_FRAME_ADVANCE=<freeze retrace>:<step retrace>,... presses the buttons itself.
  if (test) {
    static uint32_t freeze_at = 0; static std::vector<uint32_t> steps; static bool parsed = false;
    if (!parsed) {
      parsed = true;
      const std::string v = host::test_env::getenv("MELEE_TEST_FRAME_ADVANCE");
      freeze_at = (uint32_t)std::strtoul(v.c_str(), nullptr, 10);
      for (size_t at = v.find(':'); at != std::string::npos; at = v.find(',', at + 1))
        steps.push_back((uint32_t)std::strtoul(v.c_str() + at + 1, nullptr, 10));
    }
    const uint32_t now = host::retrace_count();
    if (now == freeze_at) combo = true;
    for (uint32_t s : steps) if (now == s) z = true;
  }
  if (combo && !combo_was) {
    frozen = !frozen;
    frame_pause(frozen ? 1 : 0, false);
    host::log("practice: frame advance %s", frozen ? "froze the game" : "let the game run");
  }
  combo_was = combo;
  if (frozen) {
    if (frame_pause(-1, false) == false) frame_pause(1, false);   // keep it frozen
    z_held = z ? z_held + 1 : 0;
    if (z && (!z_was || (z_held > 20 && z_held % 4 == 0))) frame_pause(1, true);
    // While frozen the D-pad places the CPU (a step on each press, then quickly while held).
    static uint32_t dpad_held = 0;
    uint16_t dpad = 0;
    for (const host::PadState& p : pads) dpad |= p.button & 0x000Fu;
    if (test && host::retrace_count() >= 2265 && host::retrace_count() <= 2285) dpad |= 0x2;   // tests: hold right
    dpad_held = dpad ? dpad_held + 1 : 0;
    if (dpad && (dpad_held == 1 || (dpad_held > 15 && dpad_held % 2 == 0))) {
      const float step = 2.0f;
      cpu_move((dpad & 0x2) ? step : (dpad & 0x1) ? -step : 0.0f, (dpad & 0x8) ? step : (dpad & 0x4) ? -step : 0.0f);
      frame_pause(1, true);   // one frame so the move shows (and the game settles the fighter there)
    }
  }
  z_was = z;
}

void request_major(uint8_t major) {
  // Native equivalents of Scene_SetNextMajor, Scene_ExitMajor and Scene_ExitMinor. The last write
  // makes the current minor run its decide path on the next guest frame; writing only the major
  // pending flag would leave an endless Training minor alive.
  if (g_bridge) { int32_t a[1] = {major}; g_bridge(OP_REQUEST_MAJOR, a, 1); return; }
  host::wr8(kSceneState + 1, major);
  host::wr8(kSceneState + 12, 1);
  host::wr32(kMinorSceneControl + 12, 1);
}

void run_decision(const Decision& d) {
  if (g_lifecycle.phase() == Phase::Failure && g_logged_phase != Phase::Failure) {
    ++g_generation;
    host::log("native practice: pre-match failure: %s", g_lifecycle.error().c_str());
  }
  if (d.cleanup_connection) {
    slippi::online::native_cleanup_match();
    if (g_content_bridge) g_content_bridge(-1, false);
  }
  if (d.request_online_handoff) {
    write_event_backup();
    if (mode_needs_direct_first_match_reset((uint8_t)g_match_mode)) {
      if (g_bridge) {
        int32_t none[1] = {};
        g_bridge(OP_DIRECT_FIRST_MATCH, none, 0);
      } else {
        host::wr8(kDirectIsWinner, 0xFF);  // ISWINNER_NULL: first match, not a loser rematch.
        host::wr8(kDirectChoseStage, 0);
      }
      host::log("native practice: initialized Direct first-match CSS state");
    }
    set_online_mode((uint8_t)g_match_mode);  // Consumed by the normal online scene.
    if (g_content_bridge) g_content_bridge((int)g_match_mode, true);
    request_major(kOnlineMajor);
    host::log("native practice: %s match found; handing off to normal online flow",
              match_mode_name(g_match_mode));
  }
  if (d.request_practice_return) {
    if (g_restore_training) {
      write_event_backup();
      // Seed the guest fields before Training's major-scene preparation reads them, then apply the
      // same narrow set again once the Training match minor has settled.
      restore_practice_fields();
    }
    if (g_content_bridge) g_content_bridge(-1, true);
    request_major(g_origin_major);
    g_return_ticks = 0;
    g_return_wait_ticks = 0;
    host::log("native practice: returning to offline scene %02X",
              g_origin_major);
  }
}

void fail(const std::string& reason, bool cleanup) {
  run_decision(g_lifecycle.fail(reason, cleanup));
}

void clear_search_metadata() {
  g_match_mode = MatchMode::None;
  g_connect_code.clear();
  g_opponent.clear();
  g_matchmaking_state = 0;
  g_search_ticks = 0;
}

void start_search(MatchMode mode, const std::string& connect_code) {
  if (g_lifecycle.phase() != Phase::Idle) return;
  if (slippi::playback::enabled()) { fail("Matchmaking is unavailable during replay playback", false); return; }
  if (is_online_scene()) { fail("Matchmaking is already using the online scene", false); return; }
  if (slippi::online::session_mode() >= 0) { fail("An online session is already active", false); return; }
  if (!is_offline_gameplay()) { fail("Start matchmaking during an offline match", false); return; }

  g_origin_major = current_major();
  g_restore_training = is_training();
  g_practice = capture_practice();
  if (!g_practice.valid) { fail("No active human fighter was found", false); return; }
  SavedPlayer& player = g_practice.players[g_practice.local_slot];
  if (!player.active) {
    fail("The active fighter cannot be used for online matchmaking", false);
    return;
  }
  if (player.character >= 26) {   // a mod's added fighter: picked on the online screen, in Direct
    fail("Added fighters go online from Online Play > Direct: pick them on its character select", false);
    return;
  }
  g_match_mode = mode;
  g_connect_code = connect_code;
  g_opponent.clear();
  g_matchmaking_state = 0;
  g_search_ticks = 0;
  if (!g_lifecycle.begin_search()) return;

  const int online_mode = (int)mode;
  set_online_mode((uint8_t)online_mode);
  // The build handshake starts here while the exercise remains active. Keep its file view
  // intact until both players are ready and run_decision accepts the scene handoff.
  if (g_content_bridge) g_content_bridge(online_mode, false);
  std::string error;
  if (!slippi::online::native_start_match(online_mode, connect_code, (uint8_t)player.character,
                                           player.costume, &error)) {
    fail(error.empty() ? "Unable to start matchmaking" : error, true);
    return;
  }
  ++g_generation;
  host::log("native practice: %s search started%s%s (offline scene %02X, port %d)",
            match_mode_name(mode), connect_code.empty() ? "" : " for ", connect_code.c_str(),
            g_origin_major, g_practice.controller_port + 1);
}

bool warmup_enter(int port);
bool warmup_request_back(const char* why);
int warmup_button_port();
bool wait_practice_possible(MatchMode* mode, std::string* code);
void wait_practice_go(int port, MatchMode mode, const std::string& code);
bool kept_possible();
void kept_go_training(int port, const char* why);
void kept_end(const std::string& why, bool cleanup);
bool kept_active();
int keep_ask_look();
void publish_kept(Snapshot& out);

void process_command(Command command) {
  switch (command.kind) {
    case CommandKind::Warmup: {
      // Connected (or waiting in a room): the warm-up room. Nobody connected yet in Direct or Teams:
      // Training while the search for the code goes on, as L and R held do there.
      const int port = warmup_button_port();
      MatchMode mode = MatchMode::None;
      std::string code;
      // A search running on the select (YouGame's modes, Unranked): Training, the search kept as it is.
      if (kept_possible()) kept_go_training(port, "WARM UP on the select while searching");
      else if (wait_practice_possible(&mode, &code)) wait_practice_go(port, mode, code);
      else warmup_enter(port);
      break;
    }
    case CommandKind::WarmupReturn: warmup_request_back(command.text.empty() ? "the player asked" : command.text.c_str()); break;
    case CommandKind::TestTraining: {
      const int character = std::atoi(command.text.c_str());
      if (character < 0 || character >= 26) break;
      g_practice = {};
      g_practice.valid = true;
      g_practice.players[0].active = true;
      g_practice.players[0].character = (uint32_t)character;
      write_event_backup();
      set_training_pick(0, character, 0);
      g_enter_training = true;
      g_enter_port = 0;
      g_enter_ticks = 0;
      g_enter_seen_select = false;
      request_major(kTrainingMajor);
      host::log("native practice: test: into Training as character %d", character);
      break;
    }
    case CommandKind::StartUnranked:
      start_search(MatchMode::Unranked, {});
      break;
    case CommandKind::StartDirect: {
      if (g_lifecycle.phase() != Phase::Idle) return;
      std::string code, error;
      if (!normalize_direct_code(command.text, &code, &error)) { fail(error, false); return; }
      start_search(MatchMode::Direct, code);
      break;
    }
    case CommandKind::StartTeams: {
      if (g_lifecycle.phase() != Phase::Idle) return;
      std::string code, error;
      if (!normalize_teams_code(command.text, &code, &error)) { fail(error, false); return; }
      start_search(MatchMode::Teams, code);
      break;
    }
    case CommandKind::Cancel: {
      if (kept_active()) { kept_end("the player stopped the search", true); break; }
      Decision d = g_lifecycle.cancel();
      if (!d.cleanup_connection) return;
      run_decision(d);
      clear_search_metadata();
      ++g_generation;
      host::log("native practice: search cancelled and connection cleaned up");
      break;
    }
    case CommandKind::TrainAgainst: {
      // A ghost picked in a Training match: the CPU becomes that player's character. Training starts
      // again with the same fighter and stage, pressed through its menus (Source Port).
      const int character = std::atoi(command.text.c_str());
      const size_t comma = command.text.find(',');
      int costume = comma == std::string::npos ? 0 : std::atoi(command.text.c_str() + comma + 1);
      // The offered pros use Fox (4), Marth (5), and Yoshi (6) costumes.
      const int costumes = character == 9 ? 5 : character == 17 ? 6 : 4;
      if (costume < 0 || costume >= costumes) costume = 0;
      if (!g_bridge || !is_training() || current_minor() != 2 || g_lifecycle.phase() != Phase::Idle) return;
      const PracticeConfig now = capture_practice();
      if (!now.valid) return;
      const SavedPlayer& me = now.players[now.local_slot];
      // Two players cannot wear the same colours of one character: the next costume then.
      if ((int)me.character == character && (int)me.costume == costume) costume = (costume + 1) % costumes;
      for (const SavedPlayer& p : now.players)
        if (p.active && p.player_kind == 1 && (int)p.character == character && (int)p.costume == costume) return;   // already that character, in those colours
      g_practice = now;
      g_offer_shown = false;   // a prompt from an online session goes with the match it was shown in
      g_offer_mode = MatchMode::None;
      set_training_pick(0, (int)me.character, me.costume);
      set_training_pick(1, character, costume);
      g_enter_training = true;
      g_enter_port = now.controller_port;
      g_enter_ticks = 0;
  g_enter_seen_select = false;
      request_major(kTrainingMajor);
      host::log("native practice: Training again with the CPU as character %d, costume %d", character, costume);
      break;
    }
    case CommandKind::AcknowledgeFailure:
      if (g_lifecycle.phase() != Phase::Failure) return;
      run_decision(g_lifecycle.acknowledge_failure());
      if (g_lifecycle.phase() == Phase::Idle) clear_search_metadata();
      ++g_generation;
      break;
  }
}

void publish_snapshot() {
  Snapshot out;
  out.phase = g_lifecycle.phase();
  out.mode = g_match_mode;
  out.in_practice = is_training();
  out.scene_major = current_major();
  out.scene_minor = current_minor();
  out.unlocked = g_unlocked.load(std::memory_order_relaxed);
  const bool playback = slippi::playback::enabled();
  const bool online_match = slippi::online::is_online_match() || is_online_scene();
  const bool session_active = slippi::online::session_mode() >= 0;
  const bool offline_gameplay = is_offline_gameplay();
  out.tab_available = matchmaking_tab_available(out.phase, playback, online_match, session_active,
                                                offline_gameplay);
  out.can_start = out.phase == Phase::Idle && !playback && !online_match && !session_active &&
                  offline_gameplay;
  out.cosmetic_profile_locked = out.phase == Phase::Searching || out.phase == Phase::Handoff ||
                                out.phase == Phase::OnlineFlow || out.phase == Phase::InMatch;
  out.controller_port = g_practice.valid ? g_practice.controller_port : 0;
  out.matchmaking_state = g_matchmaking_state;
  out.generation = g_generation;
  out.search_ticks = g_search_ticks;
  out.connect_code = g_connect_code;
  out.opponent = g_opponent;
  if (g_offer_shown && g_offer_mode != MatchMode::None)
    out.offer = std::string("Search ") + match_mode_name(g_offer_mode) + " again?";
  out.detail = g_lifecycle.error();
  {
    // The warm-up room, for the button, the room's strip and its banners.
    auto& w = out.warmup;
    bool arena = false;
    std::string why;
    w.enabled = warmup_offer(&arena, &why);
    const bool on_select = is_online_scene() && current_minor() == 0 && !playback && g_lifecycle.phase() == Phase::Idle;
    const slippi::online::WarmupLink link = slippi::online::warmup_link();
    // The button shows with someone connected in Direct or Teams, or in an arena room; else nothing.
    w.offered = on_select && !g_warmup.active() &&
                (w.enabled || (link.link_up && (link.mode == 2 || link.mode == 3)) || slippi::arena::in_room());
    w.why = w.enabled ? std::string() : why;
    // Nobody connected yet in Direct or Teams (searching for the code, or about to): the button goes
    // to Training and the search carries on from there (wait_practice_go), as L and R held do.
    {
      MatchMode wait_mode = MatchMode::None;
      std::string wait_code;
      if (!w.enabled && wait_practice_possible(&wait_mode, &wait_code)) {
        w.offered = true;
        w.enabled = true;
        w.why.clear();
      }
    }
    w.active = g_warmup.active();
    w.stage = (int)g_warmup.stage();
    w.arena = g_warmup.active() ? g_warmup.kind() == warmup::Kind::Arena : arena;
    w.mode = w.arena ? "Arena" : link.mode == 3 ? "Teams" : "Direct";
    if (!w.arena) {
      w.peers = link.peers;
      for (int i = 0; i < link.peers && i < 3; ++i) {
        w.peer[i].name = link.peer[i].name;
        w.peer[i].state = (int)(g_warmup.active() ? g_warmup.peer(i)
                               : link.peer[i].warmup == 1 ? warmup::PeerState::WarmingUp
                               : link.peer[i].ready ? warmup::PeerState::Ready : warmup::PeerState::Picking);
        w.peer[i].character = link.peer[i].character;
        w.peer[i].costume = link.peer[i].costume;
        w.peer[i].picked = link.peer[i].picked;
      }
    }
    const uint64_t now = steady_ms();
    w.banner = (int)g_warmup.banner();
    w.banner_age_ms = g_warmup.banner() == warmup::Banner::None ? 0u : (uint32_t)std::min<uint64_t>(now - g_warmup.banner_since_ms(), 0xFFFFFFFFu);
    w.banner_peer = g_warmup.banner_peer();
    w.countdown_ms = g_warmup.countdown_left_ms(now);
    w.hold = g_warmup_hold;
    w.sparring = g_warmup_sparring;
    if (w.arena || slippi::arena::in_room()) {
      const slippi::arena::WarmupInfo info = slippi::arena::warmup_info();
      w.arena_place = info.place;
      w.arena_versus = info.versus;
      w.score_live = info.score.live;
      w.score_frame = info.score.frame;
      for (int i = 0; i < 4; ++i) {
        const auto& p = info.score.fighter[i];
        w.score_fighter[i] = {p.present, p.character == 0xFF ? -1 : (int)p.character, (int)p.stocks, p.percent, (int)p.costume, p.name};
      }
    }
  }
  if (g_warmup.active()) {   // no searches from the warm-up room: the player is still in a session
    out.can_start = false;
  }
  publish_kept(out);
  switch (out.phase) {
    case Phase::Idle: out.status = out.in_practice ? "Practice" : "Ready"; break;
    case Phase::Searching:
      out.status = std::string("Searching ") + match_mode_name(g_match_mode);
      if (!g_connect_code.empty()) out.status += " for " + g_connect_code;
      break;
    case Phase::Handoff: out.status = "Match found"; break;
    case Phase::OnlineFlow: out.status = "Connecting..."; break;
    case Phase::InMatch: out.status = "In match"; break;
    case Phase::Failure: out.status = "Disconnected"; break;
    case Phase::ReturningToPractice: out.status = "Returning..."; break;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_snapshot = std::move(out);
}


// The online session is over (the other player left, the connection dropped, the player left with
// at least one match played): back into Training with the same character, and once its match runs,
// "search again?" for the same mode (Direct and Teams: the same code).
void enter_training_after_session(MatchMode mode, int local_index = -1, int test_character = -1) {
  uint8_t character = 0, costume = 0;
  const PracticeConfig css = capture_practice();
  bool picked = slippi::online::local_css_selection(&character, &costume);
  if (!picked && test_character >= 0) { character = (uint8_t)test_character; picked = true; }
  // The online match's own slot for this player: both slots of an online match are human, so the
  // first human one is player 1, and a player 2 came back to Training as player 1's character.
  // (The character select record is cleared when each match starts, so it is usually gone here.)
  const int slot = local_index >= 0 && local_index < 4 && css.players[local_index].active ? local_index : css.local_slot;
  if (!picked && css.valid && css.players[slot].character < 26) {
    character = (uint8_t)css.players[slot].character;
    costume = css.players[slot].costume;
    picked = true;
  }
  g_practice = {};
  if (picked) {
    g_practice.valid = true;
    g_practice.local_slot = 0;
    g_practice.controller_port = css.valid ? css.controller_port : 0;
    g_practice.players[0].active = true;
    g_practice.players[0].character = character;
    g_practice.players[0].costume = costume;
    g_practice.stage = css.stage;   // the online match's stage, kept for Training when it is a legal one
    write_event_backup();
    set_training_pick(0, character, costume);
    // The CPU is the player just faced, not whatever Training's character select was left on.
    if (g_last_opponent >= 0) set_training_pick(1, g_last_opponent, g_last_opponent_costume);
  }
  g_offer_mode = mode == MatchMode::Unranked || mode == MatchMode::Direct || mode == MatchMode::Teams ? mode : MatchMode::None;
  g_offer_code.clear();
  if (g_offer_mode == MatchMode::Direct || g_offer_mode == MatchMode::Teams) {
    std::string error;
    const std::string last = slippi::online::direct_code_for_practice();
    if (!(g_offer_mode == MatchMode::Teams ? normalize_teams_code(last, &g_offer_code, &error)
                                           : normalize_direct_code(last, &g_offer_code, &error))) g_offer_mode = MatchMode::None;
  }
  g_offer_reason.clear();
  g_offer_shown = false;
  g_enter_training = picked;
  g_enter_port = g_practice.controller_port;
  g_enter_ticks = 0;
  g_enter_seen_select = false;
  g_origin_major = kTrainingMajor;
  g_restore_training = false;
  host::log("native practice: online session over; into Training%s (character %u from slot %d), then offer a %s search",
            picked ? "" : " (no character known)", (unsigned)character, slot,
            g_offer_mode == MatchMode::None ? "no" : match_mode_name(g_offer_mode));
  run_decision(g_lifecycle.return_after_session());
}

// ---------------------------------------------------------------- warm-up room (slippi_warmup.h)
// Between matches with someone the player is connected to, or while waiting for a turn in an arena
// room: Training, with the connection kept (slippi::online::warmup_park), then back to the online
// character select. The opponent's character is the CPU, played by the pro AI of that character when
// there is one.

// The character select's number for a character name (a pro's character), or -1.
int css_kind_named(const char* name) {
  static const char* const kNames[] = {"Captain Falcon", "Donkey Kong", "Fox", "Mr. Game & Watch", "Kirby", "Bowser",
      "Link", "Luigi", "Mario", "Marth", "Mewtwo", "Ness", "Peach", "Pikachu", "Ice Climbers", "Jigglypuff",
      "Samus", "Yoshi", "Zelda", "Sheik", "Falco", "Young Link", "Dr. Mario", "Roy", "Pichu", "Ganondorf"};
  for (int i = 0; i < (int)(sizeof kNames / sizeof kNames[0]); ++i)
    if (name && std::strcmp(name, kNames[i]) == 0) return i;
  return -1;
}
const char* css_kind_name(int kind) {
  static const char* const kShort[] = {"Falcon", "DK", "Fox", "Game & Watch", "Kirby", "Bowser", "Link", "Luigi", "Mario",
      "Marth", "Mewtwo", "Ness", "Peach", "Pikachu", "Ice Climbers", "Puff", "Samus", "Yoshi", "Zelda", "Sheik",
      "Falco", "Young Link", "Dr. Mario", "Roy", "Pichu", "Ganondorf"};
  return kind >= 0 && kind < 26 ? kShort[kind] : "CPU";
}

// The pro whose AI plays this character, as Fight a pro offers them (practice_menu.cpp): on the
// Source Port the players whose own AI file loaded, elsewhere those with habit tables. -1 none.
int pro_for_character(int css_kind) {
  constexpr int n = (int)(sizeof practice_ghosts::kGhosts / sizeof practice_ghosts::kGhosts[0]);
  bool neural = false;
  for (int i = 0; i < n; ++i)
    if (practice_ghosts::kReleased[i] && ghost_ai::specialist_available((unsigned)i)) neural = true;
  for (int i = 0; i < n; ++i) {
    if (!practice_ghosts::kReleased[i]) continue;
    if (!(neural ? ghost_ai::specialist_available((unsigned)i) : practice_ghosts::kGhosts[i].games > 0)) continue;
    if (css_kind_named(practice_ghosts::kGhosts[i].character) == css_kind) return i;
  }
  return -1;
}

// What the player picked on the online character select (the select writes the pick into the
// match's player slots as it is made), with the controller that picked it.
bool online_select_pick(PracticeConfig* out) {
  *out = {};
  uint8_t character = 0, costume = 0;
  const PracticeConfig css = capture_practice();
  bool picked = slippi::online::local_css_selection(&character, &costume);
  if (!picked && css.valid && css.players[css.local_slot].character < 26) {
    character = (uint8_t)css.players[css.local_slot].character;
    costume = css.players[css.local_slot].costume;
    picked = true;
  }
  if (!picked) return false;
  out->valid = true;
  out->local_slot = 0;
  out->controller_port = css.valid ? css.controller_port : 0;
  out->stage = css.stage;
  out->players[0].active = true;
  out->players[0].character = character;
  out->players[0].costume = costume;
  return true;
}

// Direct or Teams on the online character select with nobody connected yet, outside a room: the
// player may practise while the code's player is searched for. The code is the one being searched
// for, or the last one used; with none there is nothing to search and so nothing to offer.
bool wait_practice_possible(MatchMode* mode, std::string* code) {
  if (g_lifecycle.phase() != Phase::Idle || g_auto_search != MatchMode::None || g_warmup.active() || !is_online_scene() ||
      current_minor() != 0 || slippi::playback::enabled() || slippi::online::is_online_match() || slippi::arena::in_room())
    return false;
  const MatchMode m = (MatchMode)online_mode();
  if (m != MatchMode::Direct && m != MatchMode::Teams) return false;
  if (slippi::online::warmup_link().link_up) return false;   // connected: the warm-up room's own rules
  std::string error;
  const std::string last = slippi::online::direct_code_for_practice();
  if (!(m == MatchMode::Teams ? normalize_teams_code(last, code, &error) : normalize_direct_code(last, code, &error))) return false;
  *mode = m;
  return true;
}

// From the online character select to Training, and the search (Unranked, or Direct or Teams for
// `code`) starts again by itself once the Training match is running.
void wait_practice_go(int port, MatchMode mode, const std::string& code) {
  uint8_t character = 0, costume = 0;
  bool picked = slippi::online::local_css_selection(&character, &costume);
  // Not locked in yet: the coin's own pick, as the select says it every frame (the room's portraits).
  if (!picked) picked = slippi::arena::css_pick(&character, &costume) && character < 26;
  if (!picked) {   // the character select writes the pick into the match's player slots as it is made
    const PracticeConfig css = capture_practice();
    const SavedPlayer& p = css.players[css.local_slot];
    host::log("native practice: character select slot %d: state %u character %u costume %u",
              css.local_slot, p.state, p.character, p.costume);
    if (css.valid && p.character < 26) { character = (uint8_t)p.character; costume = p.costume; picked = true; }
  }
  slippi::online::native_cleanup_match();   // the character select's own search, if one runs
  if (picked) {
    g_practice = {};
    g_practice.valid = true;
    g_practice.local_slot = 0;
    g_practice.controller_port = port;
    g_practice.players[0].active = true;
    g_practice.players[0].character = character;
    g_practice.players[0].costume = costume;
    write_event_backup();
    // Straight into a Training match with that character (the menus are pressed through, as the
    // warm-up room does; the stage is the last legal one, or Final Destination), so the search starts
    // again without the player picking anything twice. Their own input on those menus takes over.
    set_training_pick(0, character, costume);
  }
  g_enter_training = picked;   // nothing picked yet: Training's own character select, as before
  g_enter_port = port;
  g_enter_ticks = 0;
  g_enter_seen_select = false;
  g_auto_search = mode;
  g_auto_code = code;
  g_auto_ticks = 0;
  request_major(kTrainingMajor);
  host::log("native practice: from the online character select to Training, then %s search%s",
            match_mode_name(mode), picked ? "" : " (no character picked yet)");
}

// Can the player go now: a connected pair (Direct, Teams) or an arena room's wait. `why` when not.
bool warmup_offer(bool* arena, std::string* why) {
  *arena = false;
  if (g_warmup.active()) { *why = "Warming up"; return false; }
  if (g_lifecycle.phase() != Phase::Idle || slippi::playback::enabled() || !is_online_scene() || current_minor() != 0) {
    *why = "";
    return false;
  }
  std::string pair_why;
  if (slippi::online::warmup_possible(&pair_why)) return true;
  std::string arena_why;
  if (slippi::arena::warmup_ok(&arena_why)) { *arena = true; return true; }
  *why = slippi::arena::in_room() && !arena_why.empty() ? arena_why : pair_why;
  return false;
}

bool warmup_enter(int port) {
  bool arena = false;
  std::string why;
  if (!warmup_offer(&arena, &why)) {
    if (!why.empty()) host::log("native practice: warm-up not now: %s", why.c_str());
    return false;
  }
  PracticeConfig pick;
  if (!online_select_pick(&pick)) {
    // Nothing picked on the select yet: Training's own last picks then.
    pick.valid = true;
    pick.controller_port = port;
  } else {
    pick.controller_port = port;
  }
  // The CPU: whom the player is about to face again. Their pick once locked in, else the character
  // they played last game; in a room, whoever fights next is unknown, so the last opponent.
  int cpu = -1;
  uint8_t cpu_costume = 0;
  if (!arena) {
    const slippi::online::WarmupLink link = slippi::online::warmup_link();
    for (int i = 0; i < link.peers && cpu < 0; ++i)
      if (link.peer[i].character >= 0) { cpu = link.peer[i].character; cpu_costume = (uint8_t)link.peer[i].costume; }
  }
  if (cpu < 0 && g_last_opponent >= 0) { cpu = g_last_opponent; cpu_costume = g_last_opponent_costume; }
  if (cpu >= 0 && !(g_unlocked.load(std::memory_order_relaxed) & (1u << cpu))) {
    host::log("native practice: warm-up: character %d is not unlocked offline; Training keeps its own CPU", cpu);
    cpu = -1;
  }
  if (!arena && !slippi::online::warmup_park()) return false;
  if (arena) slippi::arena::set_warming(true);
  g_warmup.set_countdown_ms(gx::RenderOptions::live_warmup_turn_instant() ? 0 : 3000);
  g_warmup.enter(arena ? warmup::Kind::Arena : warmup::Kind::Pair, steady_ms());
  g_warmup_mode = online_mode();
  g_warmup_css = pick;
  g_warmup_cpu_applied = false;
  g_warmup_cpu_char = cpu;
  g_warmup_cpu_costume = cpu_costume;
  g_warmup_sparring.clear();
  g_warmup_match_ticks = 0;
  if (!g_warmup_cpu_saved) { g_warmup_saved_cpu = practice_cpu::config(); g_warmup_cpu_saved = true; }
  // Into Training the way "back to Training after a session" goes: the player's pick, the CPU picked,
  // the menus pressed through.
  g_auto_search = MatchMode::None;
  g_offer_mode = MatchMode::None;
  g_offer_shown = false;
  g_practice = pick;
  if (pick.players[0].active) {
    write_event_backup();
    set_training_pick(0, (int)pick.players[0].character, pick.players[0].costume);
    if (cpu >= 0) {
      // Two players cannot wear the same colours of one character.
      if ((int)pick.players[0].character == cpu && pick.players[0].costume == cpu_costume) cpu_costume = (uint8_t)((cpu_costume + 1) % 4);
      g_warmup_cpu_costume = cpu_costume;
      set_training_pick(1, cpu, cpu_costume);
    }
  }
  g_enter_training = pick.players[0].active;
  g_enter_port = pick.controller_port;
  g_enter_ticks = 0;
  g_enter_seen_select = false;
  request_major(kTrainingMajor);
  ++g_generation;
  host::log("native practice: warm-up: to Training (%s), character %d, CPU %d%s", arena ? "arena room" : "connection kept",
            pick.players[0].active ? (int)pick.players[0].character : -1, cpu, g_enter_training ? "" : " (Training's own picks)");
  return true;
}

// The player's own practice CPU again, once the room is over.
void warmup_restore_cpu() {
  if (!g_warmup_cpu_saved) return;
  if (g_warmup_cpu_applied) {
    practice_cpu::set_config(g_warmup_saved_cpu);
    host::log("native practice: warm-up: the player's own practice CPU is back (preset %u, %s)",
              (unsigned)g_warmup_saved_cpu.preset, g_warmup_saved_cpu.enabled ? "on" : "off");
  }
  g_warmup_cpu_saved = false;
  g_warmup_cpu_applied = false;
}

// The way back: the select again with the connection as it is.
void warmup_go_back(const char* why) {
  host::log("native practice: warm-up: back to the online character select (%s)", why);
  if (g_warmup.kind() == warmup::Kind::Pair) {
    g_warmup_no_bounce = g_warmup.anyone_left();
    slippi::online::warmup_unpark();
  } else {
    slippi::arena::set_warming(false);
  }
  warmup_restore_cpu();
  g_enter_training = false;
  frame_pause(0, false);   // never leave the game frozen by frame advance
  g_practice = g_warmup_css;
  if (g_practice.valid && g_practice.players[0].active) write_event_backup();
  set_online_mode(g_warmup_mode);
  // Source Port: from VS or another match mode (not Training or an event), the online character select
  // must not come straight after it: the select would set itself up from that mode's leftovers (a
  // search found from a VS match stopped both games in its KO star setup, mncharsel.c, 0.9.56). Through
  // the main menu first, as a player goes; the select is asked for once the menu is up.
  if (g_bridge && is_match_mode_major(current_major())) {
    host::log("native practice: warm-up: through the main menu first (scene %02X/%02X)", current_major(), current_minor());
    g_warmup_via_menu = host::retrace_count() | 1u;
    request_major(kMainMenuMajor);
  } else {
    request_major(kOnlineMajor);
  }
  ++g_generation;
}

// The room ended without going back (a scene change never landed): no connection is kept for nobody.
void warmup_end(const char* why) {
  host::log("native practice: warm-up over: %s", why);
  g_warmup_via_menu = 0;
  if (g_warmup.kind() == warmup::Kind::Pair && slippi::online::warmup_parked()) {
    slippi::online::warmup_unpark();
    slippi::online::native_cleanup_match();   // nobody is going back to that select
  }
  if (g_warmup.kind() == warmup::Kind::Arena) slippi::arena::set_warming(false);
  warmup_restore_cpu();
  ++g_generation;
}

bool warmup_request_back(const char* why) {
  const bool match_waiting = g_warmup.ready_waiting();   // everyone else is ready: back to play
  if (!g_warmup.request_return(steady_ms())) return false;
  if (match_waiting) { g_warmup_autostart = true; g_warmup_autostart_ticks = 0; }
  warmup_go_back(why);
  return true;
}

// The controller that pressed the button: the one the online select's player uses.
int warmup_button_port() {
  const PracticeConfig css = capture_practice();
  return css.valid ? css.controller_port : 0;
}

// Tests only: MELEE_TEST_WARMUP=<role>:<enter>:<stay>:<lock> plays one side of a pair, counted in
// frames on the online character select after a game (connected, not locked in):
//   warm   goes to the warm-up after <enter>, stays <stay> frames in Training, comes back, then locks
//          in after <lock> (the rematch starts when both have);
//   wait   locks in after <lock> and waits (the other side's Training shows "is ready");
//   leave  leaves the connection after <lock> (the other side's Training shows "left");
//   idle   does nothing on the select (a player who is away).
// Only the first select after a game warms up or leaves; later ones lock in after <lock>.
// Tests only: MELEE_TEST_WARMUP_AWAY=<major>[,<major>...]:<frames>: once away (in Training), every
// <frames> frames the game goes on to the next major scene of the list (1 the main menu, 2 VS mode's
// character select, ...), the way a player wanders off while the room goes on.
void warmup_away_test_tick() {
  static const char* spec = host::test_env::getenv("MELEE_TEST_WARMUP_AWAY");
  if (!spec) return;
  static std::vector<int> majors;
  static int every = 0;
  static size_t next = 0;
  static uint32_t frames = 0, last_retrace = 0;
  if (!every) {
    const char* p = spec;
    for (;;) {
      char* end = nullptr;
      const long v = std::strtol(p, &end, 0);
      if (end == p) break;
      majors.push_back((int)v);
      p = end;
      if (*p != ',') break;
      ++p;
    }
    if (*p == ':') every = std::atoi(p + 1);
    if (every <= 0 || majors.empty()) { host::log("warm-up test: bad MELEE_TEST_WARMUP_AWAY '%s'", spec); spec = nullptr; return; }
  }
  if (g_warmup.stage() != warmup::Stage::Away) { frames = 0; if (!g_warmup.active()) next = 0; return; }
  const uint32_t retrace = host::retrace_count();
  if (retrace == last_retrace) return;
  last_retrace = retrace;
  if (next >= majors.size() || ++frames < (uint32_t)every) return;
  frames = 0;
  const int major = majors[next++];
  host::log("warm-up test: away to scene %02X (from %02X/%02X)", major, current_major(), current_minor());
  request_major((uint8_t)major);
}

void warmup_test_tick() {
  warmup_away_test_tick();
  static const char* spec = host::test_env::getenv("MELEE_TEST_WARMUP");
  if (!spec) return;
  static std::string role;
  static int enter_at = 0, stay = 0, lock_at = 0;
  static bool parsed = false;
  if (!parsed) {
    parsed = true;
    char r[16] = {};
    if (std::sscanf(spec, "%15[a-z]:%d:%d:%d", r, &enter_at, &stay, &lock_at) != 4) {
      host::log("warm-up test: bad MELEE_TEST_WARMUP '%s'", spec);
      spec = nullptr;
      return;
    }
    role = r;
    host::log("warm-up test: role %s, enter %d, stay %d, lock %d", role.c_str(), enter_at, stay, lock_at);
  }
  static uint32_t ticks = 0, training_ticks = 0;
  static bool done = false, warmed = false;
  // arena: a room member who is waiting (not in the ring) goes to warm up after <enter> frames on the
  // select, up to <stay> times; the room's turn call brings them back (the countdown, or at once).
  if (role == "arena") {
    static int times = 0;
    if (g_warmup.active()) { ticks = 0; return; }
    const bool waiting = is_online_scene() && slippi::online::in_online_menus() && slippi::arena::warmup_ok(nullptr);
    ticks = waiting ? ticks + 1 : 0;
    if (ticks == (uint32_t)enter_at && times < stay) {
      ++times;
      host::log("warm-up test: arena member goes to warm up (%d)", times);
      if (!warmup_enter(0)) host::log("warm-up test: could not go");
    }
    return;
  }
  // lr: as warm, but with L and R held on the controller (host::input_test_hold) for the way in and
  // the way back, the way a player does it: the hold the game and the warm-up both read.
  static bool holding = false;
  const auto hold = [](bool on) {
    if (on == holding) return;
    holding = on;
    host::input_test_hold(on ? 0x0060u : 0u);
    host::log("warm-up test: %s L and R", on ? "holding" : "let go of");
  };
  if (g_warmup.active()) {
    ticks = 0;
    if (role == "lr") {
      if (g_warmup.stage() == warmup::Stage::Going) hold(false);   // let go once it went
      if (g_warmup.stage() == warmup::Stage::Away && is_training()) {
        ++training_ticks;
        if (training_ticks == (uint32_t)stay) hold(true);
        if (training_ticks > (uint32_t)stay + 120) hold(false);   // never longer than two seconds
      }
      if (g_warmup.stage() == warmup::Stage::Returning) hold(false);
      return;
    }
    if (g_warmup.stage() == warmup::Stage::Away && is_training() && ++training_ticks == (uint32_t)stay)
      warmup_request_back("test: the stay is over");
    return;
  }
  // Not in the warm-up: a hold that did not take it there within a second lets go.
  static uint32_t held_ticks = 0;
  held_ticks = holding ? held_ticks + 1 : 0;
  if (holding && held_ticks > 60) { hold(false); host::log("warm-up test: the hold did not go to the warm-up"); }
  training_ticks = 0;
  const slippi::online::ArenaMatchStatus st = slippi::online::arena_match_status();
  // A player who leaves may already be locked in (a loser locks in by itself in these tests).
  const bool on_select = is_online_scene() && slippi::online::in_online_menus() && !st.in_match &&
                         st.state == 4 /* CONNECTION_SUCCESS */ && (!st.local_ready || (role == "leave" && !done));
  if (!on_select) { ticks = 0; return; }
  ++ticks;
  if (!done && (role == "warm") && !warmed && ticks == (uint32_t)enter_at) {
    warmed = true;
    if (!warmup_enter(0)) host::log("warm-up test: could not go");
    return;
  }
  if (!done && role == "lr" && !warmed && ticks == (uint32_t)enter_at) {
    warmed = true;
    hold(true);   // the select's own hold check takes it from here (a third of a second)
    return;
  }
  if (!done && role == "leave" && ticks == (uint32_t)lock_at) {
    done = true;
    host::log("warm-up test: leaving the connection");
    slippi::online::native_cleanup_match();
    return;
  }
  if (role == "idle" && !done) return;
  if (ticks == (uint32_t)lock_at) {
    // As a player does in a rematch: Start on the select (apply_pads presses it). The game then
    // locks the winner of the last game in with no stage and sends the loser to the stage select
    // to counterpick (apply_pads presses A there); the match starts when both are ready.
    g_test_press_start = true;
    host::log("warm-up test: locked in (Start on the select)");
    if ((role != "warm" && role != "lr") || warmed) done = true;
  }
}

// Tests only: MELEE_TEST_GO_ONLINE=<ticks>:<route> sends the game to the online character select
// (Direct) <ticks> frames into an offline match (any is_offline_gameplay_scene), as a pull-back from
// another mode does: route "direct" requests the online scene at once, "menu" goes through the main
// menu first. The log says where it landed.
void scene_route_test_tick() {
  // MELEE_TEST_GOTO_MAJOR=<retrace>:<major>: once, that major scene requested at that retrace (the way
  // into a mode the scripted pads then play, e.g. 2 for VS mode's character select).
  {
    static const char* go = host::test_env::getenv("MELEE_TEST_GOTO_MAJOR");
    static bool gone = false;
    unsigned at = 0, major = 0;
    if (go && !gone && std::sscanf(go, "%u:%u", &at, &major) == 2 && host::retrace_count() >= at) {
      gone = true;
      host::log("scene route test: to scene %02X (from %02X/%02X)", major, current_major(), current_minor());
      request_major((uint8_t)major);
    }
  }
  static const char* spec = host::test_env::getenv("MELEE_TEST_GO_ONLINE");
  if (!spec) return;
  static int at = -1, step = 0;
  static char route[16] = {};
  static uint32_t ticks = 0, wait = 0;
  if (at < 0 && std::sscanf(spec, "%d:%15[a-z]", &at, route) != 2) { host::log("scene route test: bad '%s'", spec); spec = nullptr; return; }
  static uint8_t last_major = 0xFF, last_minor = 0xFF;
  if (current_major() != last_major || current_minor() != last_minor) {
    last_major = current_major(); last_minor = current_minor();
    host::log("scene route test: scene %02X/%02X (retrace %u)", last_major, last_minor, host::retrace_count());
  }
  if (step == 0) {
    if (!is_offline_gameplay_scene(current_major(), current_minor())) { ticks = 0; return; }
    if (++ticks < (uint32_t)at) return;
    step = 1;
    host::log("scene route test: leaving the match %02X/%02X by route %s", current_major(), current_minor(), route);
    if (std::strcmp(route, "menu") == 0) { request_major(1); return; }
    set_online_mode(2);
    request_major(kOnlineMajor);
    step = 3;
    return;
  }
  if (step == 1 && current_major() == 1 && ++wait > 30) {
    step = 3;
    host::log("scene route test: on the main menu, now the online select");
    set_online_mode(2);
    request_major(kOnlineMajor);
  }
  if (step == 3 && is_online_scene() && slippi::online::in_online_menus()) {
    step = 4;
    host::log("scene route test: the online character select is up");
  }
}

// Once a frame: what the game and the link say, and what the room answers.
void warmup_tick() {
  // L+R held: in Training toward going back (two thirds of a second: both triggers fully in for
  // that long is no accident in a match), shown on screen as it fills.
  constexpr uint32_t kBackHold = 40;
  static uint32_t back_ticks = 0;
  if (!g_warmup.active()) { back_ticks = 0; g_warmup_hold = 0.0f; return; }
  const uint64_t now = steady_ms();
  warmup::Observe o;
  o.now_ms = now;
  o.in_training = is_training();
  o.online_select = is_online_scene() && slippi::online::in_online_menus();
  // The online scene but not its select (the player picked Online from the menus): no new online game
  // while the room holds them; the way back. Not while the main menu step of a way back is pending.
  o.online_menu = is_online_scene() && !o.online_select;
  if (g_warmup.kind() == warmup::Kind::Pair) {
    const slippi::online::WarmupLink link = slippi::online::warmup_link();
    o.link_up = link.link_up;
    o.peers = link.peers;
    for (int i = 0; i < link.peers && i < warmup::kMaxPeers; ++i)
      o.peer[i] = link.peer[i].warmup == 1 ? warmup::PeerState::WarmingUp
                : link.peer[i].ready ? warmup::PeerState::Ready : warmup::PeerState::Picking;
  } else {
    o.turn = slippi::arena::turn_called();
  }
  const warmup::Stage before = g_warmup.stage();
  const warmup::Banner banner_before = g_warmup.banner();
  switch (g_warmup.tick(o)) {
    case warmup::Action::None: break;
    case warmup::Action::GoBack:
      warmup_go_back(o.online_menu ? "the player went to the online menus: back to the room" : "the room called this player's turn");
      break;
    case warmup::Action::Ready:
      host::audio_ui_sound(2);
      g_warmup_autostart = true;
      g_warmup_autostart_ticks = 0;
      host::log("native practice: warm-up: everyone else locked in (retrace %u)", host::retrace_count());
      warmup_go_back("everyone else locked in: back to start the match");
      break;
    case warmup::Action::Arrived:
      host::log("native practice: warm-up: on the online character select again (retrace %u)", host::retrace_count());
      ++g_generation;
      break;
    case warmup::Action::GiveUp:
      warmup_end(before == warmup::Stage::Returning ? "the character select never came back" : "Training never came up");
      break;
  }
  if (g_warmup.banner() != banner_before) {
    const int p = g_warmup.banner_peer();
    host::log("native practice: warm-up: %s", g_warmup.banner() == warmup::Banner::PeerReady ? "the opponent is ready"
              : g_warmup.banner() == warmup::Banner::PeerLeft ? "the opponent left"
              : g_warmup.banner() == warmup::Banner::PeerWarming ? "the opponent is warming up too"
              : g_warmup.banner() == warmup::Banner::PeerBack ? "the opponent is back on the select"
              : g_warmup.banner() == warmup::Banner::YourTurn ? "this player's turn in the room" : "banner cleared");
    (void)p;
    // A sound for what asks for the player: the opponent ready, the room calling this player's turn.
    if (g_warmup.banner() == warmup::Banner::PeerReady || g_warmup.banner() == warmup::Banner::YourTurn) host::audio_ui_sound(2);
    ++g_generation;
  }

  // The sparring partner: once the Training match runs, the pro who plays the CPU's character.
  if (g_warmup.stage() == warmup::Stage::Away && is_training() && current_minor() == 2) {
    if (++g_warmup_match_ticks == 30) {
      const PracticeConfig now_cfg = capture_practice();
      int cpu_kind = -1;
      for (const SavedPlayer& p : now_cfg.players)
        if (p.active && p.player_kind == 1 && p.character < 26) { cpu_kind = (int)p.character; break; }
      const int pro = cpu_kind >= 0 ? pro_for_character(cpu_kind) : -1;
      MuPcConfig c{};
      if (!g_warmup_cpu_applied && pro >= 0 && practice_cpu::ghost(pro, c)) {
        c.enabled = 1;
        c.tools = g_warmup_saved_cpu.tools;   // the player's own tools (frame advance, ...) stay
        practice_cpu::set_config(c);
        g_warmup_cpu_applied = true;
        g_warmup_sparring = std::string(practice_ghosts::kGhosts[pro].name) + " AI";   // the player's AI, not the player
        host::log("native practice: warm-up: %s's AI plays the CPU (%s)", practice_ghosts::kGhosts[pro].name,
                  practice_ghosts::kGhosts[pro].character);
      } else if (g_warmup_sparring.empty()) {
        const MuPcConfig live = practice_cpu::config();
        const int live_pro = (live.preset & 0x80) && live.preset != MU_PC_PRESET_AI ? (int)(live.preset & 0x7F) : -1;
        g_warmup_sparring = live_pro >= 0 && live_pro < (int)(sizeof practice_ghosts::kGhosts / sizeof practice_ghosts::kGhosts[0])
                                ? std::string(practice_ghosts::kGhosts[live_pro].name) + " AI"
                                : std::string(css_kind_name(cpu_kind)) + " CPU";
        host::log("native practice: warm-up: the CPU is %s", g_warmup_sparring.c_str());
      }
      ++g_generation;
    }
  } else {
    g_warmup_match_ticks = 0;
    // Out of Training (another mode, the menus): the player's own practice CPU again; the pro comes
    // back with the next Training match.
    if (g_warmup.stage() == warmup::Stage::Away && !is_training() && g_warmup_cpu_applied) {
      practice_cpu::set_config(g_warmup_saved_cpu);
      g_warmup_cpu_applied = false;
      g_warmup_sparring.clear();
      host::log("native practice: warm-up: out of Training (scene %02X): the player's own practice CPU is back", current_major());
      ++g_generation;
    }
  }
  // The way back through the main menu: once the menu is up (a third of a second), the select.
  if (g_warmup_via_menu) {
    if (!g_warmup.active() || is_online_scene()) {
      g_warmup_via_menu = 0;
    } else if (current_major() == kMainMenuMajor && host::retrace_count() - g_warmup_via_menu > 20) {
      g_warmup_via_menu = 0;
      host::log("native practice: warm-up: on the main menu, now the online character select");
      set_online_mode(g_warmup_mode);
      request_major(kOnlineMajor);
    }
  }

  // The hold back (anywhere the player is away: Training, another mode, the menus).
  // The hold that brought the player here may still be down when Training comes up: it counts only
  // after both triggers were let go once in Training.
  static bool armed = false;
  bool lr = false;
  if (g_warmup.stage() == warmup::Stage::Away) {
    host::PadState pads[4];
    host::input_last_pads(pads);
    for (int port = 0; port < 4; ++port)
      if (pads[port].err == 0 && (pads[port].button & 0x0060u) == 0x0060u && !(pads[port].button & (0x0100u | 0x1000u | 0x0010u))) lr = true;
    if (!lr) armed = true;
  } else {
    armed = false;
  }
  if (!armed) lr = false;
  back_ticks = lr ? back_ticks + 1 : 0;
  g_warmup_hold = lr ? std::min(1.0f, (float)back_ticks / (float)kBackHold) : 0.0f;
  if (back_ticks == kBackHold) warmup_request_back(is_training() ? "L+R held in Training" : "L+R held");
}


// ---------------------------------------------------------------- play while searching (a kept search)
// The player chose to keep a search running while they leave its character select: to Training with
// the WARM UP card or L+R held there, or out to the menus (the select asks first, or the setting
// says so). The search goes on, on its own thread, exactly as it would on the select; the game sees
// no session meanwhile (slippi::online::search_away), so anything played is plain offline play. A
// found opponent brings the player back to that select after a short countdown, at a safe moment,
// and the select carries on as if they had never left (they are locked in already). Nothing here runs
// unless the player chose it: leaving a search otherwise stops it, as it always did.
enum class KeepStage : uint8_t { Off, Away, Found, Going };
struct Kept {
  KeepStage stage = KeepStage::Off;
  uint8_t mode = 0xFF;          // the online mode byte to give back on the way home
  int service = -1;             // yougame::menu_service() when it was kept
  std::string label;            // "Unranked", "Ranked", "Friends", "Squad Strike", "YouGame and Slippi"
  bool squad = false;           // a Squad Strike search: its lineup stays on the select's side
  PracticeConfig css;           // the pick on the select (the coin on the way back) and its controller
  uint64_t since_ms = 0;        // kept since (the banner's slide in, the short form)
  uint64_t found_ms = 0, going_ms = 0;
  uint32_t via_menu = 0;        // the way back through the main menu: the retrace it was asked at
  uint64_t safe_wait_ms = 0;    // found: since the countdown ended, waiting for a safe moment
  bool left_online = false;     // the online scene was left since (the select's own scene change takes a frame or two)
  std::string note;             // found and waiting: why
};
Kept g_kept;
constexpr uint64_t kKeepCountdownMs = 3000;     // "Opponent found", then back to the match
constexpr uint64_t kKeepSafeWaitMs = 20000;     // found: the longest wait for a safe moment
constexpr uint64_t kKeepGoingMs = 15000;        // on the way back: the select must be up by then
bool g_keep_pending = false;                    // the player said yes on the select; the menu's cleanup takes it
uint64_t g_keep_pending_ms = 0;
PracticeConfig g_last_select_pick;              // the last pick seen on the online select (for the way back)
uint8_t g_prev_major = 0xFF, g_prev_minor = 0xFF;   // the scene at the end of the last tick
uint32_t g_scene_steady = 0;                    // frames since the scene last changed
float g_keep_hold = 0.0f;                       // L+R toward going back now
// The question on the select (look 1): asked as the player starts to leave while searching.
bool g_keep_ask = false;
uint64_t g_keep_ask_ms = 0;
int keep_ask_look() {
  static const int look = [] { const char* v = host::test_env::getenv("MELEE_TEST_KEEP_ASK"); const int n = v ? std::atoi(v) : 1; return n >= 1 && n <= 3 ? n : 1; }();
  return look;
}

std::string kept_label(int mode, int service, bool squad) {
  switch (mode) {
    case 0: return "Ranked";
    case 1: return squad ? "Squad Strike" : service == 2 ? "YouGame and Slippi" : "Unranked";
    case 2: return yougame::session() ? "Friends" : "Direct";
    case 3: return "Teams";
    default: return "Search";
  }
}

// A search that may be kept: going out (or being retried), nobody found yet, from a select the player
// locked in on, and not a friend's room or lobby (their select is the place to be). Party is left out.
bool kept_searching(const slippi::online::KeptSearch& s) {
  return (s.state == 1 || s.state == 2) && !s.lobby && !s.friend_room && s.mode >= 0 && s.mode <= 3 && s.locked;
}

// On the online select now, can this search be kept (the card, L+R, the question)? Slippi's own Direct
// and Teams keep 0.9.220's way (the search starts again from Training: wait_practice_go).
bool kept_possible() {
  if (g_kept.stage != KeepStage::Off || g_lifecycle.phase() != Phase::Idle || g_warmup.active() || g_auto_search != MatchMode::None ||
      slippi::playback::enabled() || !is_online_scene() || current_minor() != 0 || slippi::arena::in_room())
    return false;
  const slippi::online::KeptSearch s = slippi::online::kept_search();
  if (!kept_searching(s)) return false;
  if (!yougame::session() && (s.mode == 2 || s.mode == 3)) return false;
  return true;
}

void kept_begin(const char* why, const PracticeConfig& pick) {
  const slippi::online::KeptSearch s = slippi::online::kept_search();
  g_kept = Kept{};
  g_kept.stage = KeepStage::Away;
  g_kept.mode = (uint8_t)s.mode;
  g_kept.service = yougame::menu_service();
  g_kept.label = kept_label(s.mode, g_kept.service, s.squad);
  g_kept.squad = s.squad;
  g_kept.css = pick;
  g_kept.since_ms = steady_ms();
  g_keep_pending = false;
  g_keep_ask = false;
  slippi::online::search_away(true);
  ++g_generation;
  host::log("native practice: kept search: %s (%s, mode %d, service %d, character %d)", why, g_kept.label.c_str(), s.mode,
            g_kept.service, pick.valid && pick.players[0].active ? (int)pick.players[0].character : -1);
}

void kept_end(const std::string& why, bool cleanup) {
  host::log("native practice: kept search over: %s", why.c_str());
  const bool away = g_kept.stage == KeepStage::Away || g_kept.stage == KeepStage::Found;
  g_kept = Kept{};
  g_keep_hold = 0.0f;
  if (cleanup) slippi::online::native_cleanup_match();
  else if (away) slippi::online::search_away(false);
  ++g_generation;
}

// The select's pick as it stands (locked in: the lock-in's), with the select's controller.
PracticeConfig select_pick_now() {
  PracticeConfig pick;
  if (!online_select_pick(&pick)) pick = g_last_select_pick;
  return pick;
}

// The WARM UP card or L+R held on the select while searching: Training with the pick, the search kept.
void kept_go_training(int port, const char* why) {
  PracticeConfig pick = select_pick_now();
  pick.controller_port = port;
  kept_begin(why, pick);
  g_practice = pick;
  if (pick.valid && pick.players[0].active) {
    write_event_backup();
    set_training_pick(0, (int)pick.players[0].character, pick.players[0].costume);
  }
  g_enter_training = pick.valid && pick.players[0].active;   // straight into the match, as the warm-up goes
  g_enter_port = port;
  g_enter_ticks = 0;
  g_enter_seen_select = false;
  request_major(kTrainingMajor);
}

// Back to the select the search came from: the coin, the mode, then the scene (through the main menu
// from another mode's match on the Source Port, as the warm-up goes back).
void kept_go_back(const char* why) {
  host::log("native practice: kept search: back to the %s select (%s)", g_kept.label.c_str(), why);
  slippi::online::search_away(false);
  g_kept.stage = KeepStage::Going;
  g_kept.going_ms = steady_ms();
  g_enter_training = false;
  frame_pause(0, false);   // never leave the game frozen by frame advance
  g_practice = g_kept.css;
  if (g_practice.valid && g_practice.players[0].active) write_event_backup();
  set_online_mode(g_kept.mode);
  if (g_kept.mode == (uint8_t)MatchMode::Direct) {
    if (g_bridge) { int32_t none[1] = {}; g_bridge(OP_DIRECT_FIRST_MATCH, none, 0); }
    else { host::wr8(kDirectIsWinner, 0xFF); host::wr8(kDirectChoseStage, 0); }
  }
  if (g_bridge && is_match_mode_major(current_major())) {
    host::log("native practice: kept search: through the main menu first (scene %02X/%02X)", current_major(), current_minor());
    g_kept.via_menu = host::retrace_count() | 1u;
    request_major(kMainMenuMajor);
  } else {
    request_major(kOnlineMajor);
  }
  ++g_generation;
}

// Can the player be taken back right now? Not halfway through a scene change, not while Training's
// menus are being pressed through, not out of a replay being watched.
bool kept_safe_now(std::string* why) {
  why->clear();
  if (slippi::playback::enabled()) { *why = "Leave the replay to play"; return false; }
  if (g_enter_training) return false;
  if (g_scene_steady < 20) return false;
  return true;
}

// Tests only: MELEE_TEST_KEEP_AWAY=<major>[,<major>...]:<frames>: while the search is kept, every
// <frames> frames the game goes on to the next major scene of the list (1 the main menu, 0x1C Training's
// select, ...), the way a player wanders off while the search goes on.
void kept_away_test_tick() {
  static const char* spec = host::test_env::getenv("MELEE_TEST_KEEP_AWAY");
  if (!spec) return;
  static std::vector<int> majors;
  static int every = 0;
  static size_t next = 0;
  static uint32_t frames = 0, last_retrace = 0;
  if (!every) {
    const char* p = spec;
    for (;;) {
      char* end = nullptr;
      const long v = std::strtol(p, &end, 0);
      if (end == p) break;
      majors.push_back((int)v);
      p = end;
      if (*p != ',') break;
      ++p;
    }
    if (*p == ':') every = std::atoi(p + 1);
    if (every <= 0 || majors.empty()) { host::log("kept search test: bad MELEE_TEST_KEEP_AWAY '%s'", spec); spec = nullptr; return; }
  }
  if (g_kept.stage != KeepStage::Away) { frames = 0; return; }
  const uint32_t retrace = host::retrace_count();
  if (retrace == last_retrace) return;
  last_retrace = retrace;
  if (next >= majors.size() || ++frames < (uint32_t)every) return;
  frames = 0;
  const int major = majors[next++];
  host::log("kept search test: away to scene %02X (from %02X/%02X)", major, current_major(), current_minor());
  request_major((uint8_t)major);
}

// Once a frame: what the search is doing, and the way back when it found someone.
void kept_tick() {
  const uint8_t major = current_major(), minor = current_minor();
  g_scene_steady = major == g_prev_major && minor == g_prev_minor ? g_scene_steady + 1 : 0;
  g_prev_major = major;
  g_prev_minor = minor;
  // The pick on the select, a few times a second, for a way out that has no select under it any more.
  if (major == kOnlineMajor && minor == 0 && g_scene_steady % 10 == 5) {
    PracticeConfig pick;
    if (online_select_pick(&pick)) g_last_select_pick = pick;
  }
  if (g_keep_pending && steady_ms() - g_keep_pending_ms > 15000) g_keep_pending = false;
  kept_away_test_tick();
  if (g_kept.stage == KeepStage::Off) { g_keep_hold = 0.0f; return; }
  const uint64_t now = steady_ms();
  const slippi::online::KeptSearch s = slippi::online::kept_search();
  // Ended under it (another online mode, a room or a lobby cleaned up): nothing left to keep.
  if (g_kept.stage != KeepStage::Going && !s.away) { kept_end("the search was ended elsewhere", false); return; }
  if (g_kept.stage == KeepStage::Away || g_kept.stage == KeepStage::Found) {
    if (s.state == 5 || !s.error.empty()) { kept_end("the search stopped: " + s.error, true); return; }
    if (s.state == 0) { kept_end("the search is not running", true); return; }
    // The player went to an online screen by themselves: the same mode's select carries on with it;
    // any other online mode starts its own, so this one stops.
    if (major != kOnlineMajor) g_kept.left_online = true;
    if (major == kOnlineMajor && g_kept.left_online) {
      if (online_mode() == g_kept.mode && (g_kept.service < 0 || yougame::menu_service() == g_kept.service)) {
        host::log("native practice: kept search: the player is back on the %s select by themselves", g_kept.label.c_str());
        g_kept = Kept{};
        slippi::online::search_away(false);
        ++g_generation;
      } else {
        kept_end("another online mode was picked", true);
      }
      return;
    }
  }
  if (g_kept.stage == KeepStage::Away && s.state == 4) {
    g_kept.stage = KeepStage::Found;
    g_kept.found_ms = now;
    host::audio_ui_sound(2);
    host::log("native practice: kept search: opponent found (scene %02X/%02X, retrace %u)", major, minor, host::retrace_count());
    ++g_generation;
  }
  if (g_kept.stage == KeepStage::Found && now - g_kept.found_ms >= kKeepCountdownMs) {
    std::string why;
    if (kept_safe_now(&why)) {
      kept_go_back("opponent found");
    } else {
      if (!g_kept.safe_wait_ms) {
        g_kept.safe_wait_ms = now;
        host::log("native practice: kept search: found, waiting for a safe moment%s%s", why.empty() ? "" : ": ", why.c_str());
      }
      if (g_kept.note != why) { g_kept.note = why; ++g_generation; }
      if (now - g_kept.safe_wait_ms > kKeepSafeWaitMs) { kept_end("found, but the player could not be brought back in time", true); return; }
    }
  }
  if (g_kept.stage == KeepStage::Going) {
    if (g_kept.via_menu && major == kMainMenuMajor && host::retrace_count() - g_kept.via_menu > 20) {
      g_kept.via_menu = 0;
      host::log("native practice: kept search: on the main menu, now the online select");
      set_online_mode(g_kept.mode);
      request_major(kOnlineMajor);
    }
    if (major != kOnlineMajor && online_mode() != g_kept.mode) set_online_mode(g_kept.mode);
    if (major == kOnlineMajor && minor == 0 && slippi::online::in_online_menus()) {
      host::log("native practice: kept search: on the %s select again (retrace %u)", g_kept.label.c_str(), host::retrace_count());
      g_kept = Kept{};
      ++g_generation;
      return;
    }
    if (now - g_kept.going_ms > kKeepGoingMs) { kept_end("the select never came back", true); return; }
  }
  // L and R held (two thirds of a second, both triggers in, no A, Start or Z): back to the search now.
  // It counts only once both were let go after the way out (the hold that went to Training).
  constexpr uint32_t kHold = 40;
  static uint32_t held = 0;
  static bool armed = false;
  bool lr = false;
  if (g_kept.stage == KeepStage::Away || g_kept.stage == KeepStage::Found) {
    host::PadState pads[4];
    host::input_last_pads(pads);
    for (int port = 0; port < 4; ++port)
      if (pads[port].err == 0 && (pads[port].button & 0x0060u) == 0x0060u && !(pads[port].button & (0x0100u | 0x1000u | 0x0010u))) lr = true;
    if (!lr) armed = true;
  } else {
    armed = false;
  }
  if (!armed) lr = false;
  held = lr ? held + 1 : 0;
  g_keep_hold = lr ? std::min(1.0f, (float)held / (float)kHold) : 0.0f;
  if (held == kHold) kept_go_back(g_kept.stage == KeepStage::Found ? "L+R held: go now" : "L+R held: back to the search");
}

bool kept_active() { return g_kept.stage != KeepStage::Off; }

const char* kept_service_name(int service, int mode) {
  if (mode == 1 && service == 2) return "YouGame and Slippi";
  if (service == 0) return "Slippi";
  if (service == 1 || yougame::active()) return "YouGame";
  return yougame::session() ? "YouGame" : "Slippi";
}

void publish_kept(Snapshot& out) {
  auto& k = out.kept;
  const uint64_t now = steady_ms();
  k.ask_look = keep_ask_look();
  k.possible = gx::RenderOptions::live_keep_search() != 2 && kept_possible();
  k.asking = g_keep_ask && k.possible;
  k.ask_ms = k.asking ? (uint32_t)std::min<uint64_t>(now - g_keep_ask_ms, 0xFFFFFFFFu) : 0;
  // The search going on: the WARM UP card says so on the select (Training while it searches).
  if (k.possible && !out.warmup.enabled) {
    out.warmup.offered = true;
    out.warmup.enabled = true;
    out.warmup.why.clear();
  }
  if (g_kept.stage == KeepStage::Off) return;
  out.can_start = false;
  const slippi::online::KeptSearch s = slippi::online::kept_search();
  k.stage = g_kept.stage == KeepStage::Away ? 1 : g_kept.stage == KeepStage::Found ? 2 : 3;
  k.service = kept_service_name(g_kept.service, g_kept.mode);
  k.mode = g_kept.label;
  if (g_kept.mode == 1 && g_kept.service == 2) k.mode = "Unranked";
  k.elapsed_ms = s.started_ms && s.now_ms > s.started_ms ? (uint32_t)std::min<uint64_t>(s.now_ms - s.started_ms, 0xFFFFFFFFu) : 0;
  k.shown_ms = (uint32_t)std::min<uint64_t>(now - g_kept.since_ms, 0xFFFFFFFFu);
  if (g_kept.found_ms) {
    k.found_ms = (uint32_t)std::min<uint64_t>(now - g_kept.found_ms, 0xFFFFFFFFu);
    k.countdown_ms = g_kept.stage == KeepStage::Going ? 0
                   : now - g_kept.found_ms < kKeepCountdownMs ? (int)(kKeepCountdownMs - (now - g_kept.found_ms)) : -1;
  }
  k.hold = g_keep_hold;
  k.note = g_kept.note;
}

// The question's answers and the select's buttons (apply_pads): what the player pressed this frame.
void kept_select_pads(host::PadState* pads, bool new_frame) {
  constexpr uint16_t kA = 0x0100, kB = 0x0200, kX = 0x0400, kStart = 0x1000, kZ = 0x0010;
  static uint16_t was[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
  uint16_t fresh = 0;
  for (int i = 0; i < 4; ++i) {
    const uint16_t now = pads[i].err == 0 ? pads[i].button : 0;
    if (new_frame) { fresh |= (uint16_t)(now & ~was[i]); was[i] = now; }
  }
  const int setting = gx::RenderOptions::live_keep_search();
  const bool possible = setting != 2 && kept_possible();
  if (g_keep_ask && !possible) {
    g_keep_ask = false;
    host::log("native practice: kept search: the question went away (the search changed)");
    ++g_generation;
  }
  const auto leave = [&](bool keep, const char* why) {
    g_keep_ask = false;
    g_keep_pending = keep;
    g_keep_pending_ms = steady_ms();
    host::log("native practice: kept search: %s", why);
    request_major(kMainMenuMajor);   // the menus, where the main menu's cleanup keeps it (or stops it)
    ++g_generation;
  };
  if (g_keep_ask) {
    // Z still stops the search on the select, as it always did: the question goes and Z reaches the game.
    if (fresh & kZ) {
      g_keep_ask = false;
      for (int i = 0; i < 4; ++i) pads[i].button &= (uint16_t)~(kA | kB | kX | kStart);
      host::log("native practice: kept search: Z while asked: the question goes, Z stops the search as always");
      ++g_generation;
      return;
    }
    // While asked, the game sees none of these (B would leave, A pick, Start act on the search).
    for (int i = 0; i < 4; ++i) pads[i].button &= (uint16_t)~(kA | kB | kX | kStart | kZ);
    if (fresh & kA) leave(true, "the player keeps searching and leaves the select");
    else if (fresh & kB) leave(false, "the player stops the search and leaves the select");
    return;
  }
  if (!possible) return;
  if (setting == 0 && (fresh & kB) && keep_ask_look() == 1) {
    g_keep_ask = true;
    g_keep_ask_ms = steady_ms();
    for (int i = 0; i < 4; ++i) pads[i].button &= (uint16_t)~kB;
    host::log("native practice: kept search: B while searching: asked whether to keep searching");
    host::audio_ui_sound(2);
    ++g_generation;
    return;
  }
  if (keep_ask_look() == 2 && (fresh & kX)) {
    for (int i = 0; i < 4; ++i) pads[i].button &= (uint16_t)~kX;
    leave(true, "X on the select: leave and keep searching");
  }
}

}  // namespace


Snapshot snapshot() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_snapshot;
}

void submit_start_direct(const std::string& connect_code) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CommandKind::StartDirect, connect_code});
}

void submit_start_teams(const std::string& connect_code) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CommandKind::StartTeams, connect_code});
}

void submit_start_unranked() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CommandKind::StartUnranked, {}});
}

void submit_train_against(int character, int costume) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CommandKind::TrainAgainst, std::to_string(character) + "," + std::to_string(costume)});
}

void submit_cancel() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CommandKind::Cancel, {}});
}

void submit_acknowledge_failure() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CommandKind::AcknowledgeFailure, {}});
}

void submit_warmup() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CommandKind::Warmup, {}});
}

bool keep_search_squad() { return g_kept.stage != KeepStage::Off && g_kept.squad; }

bool keep_search_at_menu() {
  if (g_kept.stage != KeepStage::Off) {
    host::log("native practice: kept search: the main menu came up; the search goes on");
    return true;
  }
  const slippi::online::KeptSearch s = slippi::online::kept_search();
  const bool from_select = g_prev_major == kOnlineMajor && g_prev_minor == 0;
  const bool chose = g_keep_pending && steady_ms() - g_keep_pending_ms < 15000;
  const bool always = gx::RenderOptions::live_keep_search() == 1 && from_select;
  g_keep_pending = false;
  if (!(chose || always) || !kept_searching(s) || g_lifecycle.phase() != Phase::Idle || slippi::playback::enabled()) return false;
  kept_begin(chose ? "the player chose to keep searching and left the select" : "left the select (setting: keep searching)",
             g_last_select_pick);
  return true;
}

void test_request_training(int character) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CommandKind::TestTraining, std::to_string(character)});
}

void submit_warmup_return() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CommandKind::WarmupReturn, "the player chose Back to match"});
}

// Playing, paused or a menu, for the YouGame button (yougame_game_state.h). Every retrace, before
// any early return below, so online, friends and console sessions report too. Only reads: the
// page-pause op's query (-1) changes nothing; the worker sends on a change.
static void report_game_state() {
  bool match = false, paused = false;
  if (g_bridge) {
    int32_t a[5] = {-1, 0, 0, 0, 0};
    if (g_bridge(OP_PAGE_PAUSE, a, 5) == 0) { match = a[4] != 0; paused = a[0] > 0 || a[3] != 0; }
  } else {
    const uint32_t info = host::rd32(0x804D6720u);   // Static Recomp: the scene kind and the pause bits
    const int kind = info && host::try_ptr(info, 1) ? host::rd8(info) : -1;
    match = kind == 2 || kind == 3 || kind == 4 || kind == 0x2C;
    // gm_80479D58.unk_10.x0 (gm_GetDbPauseFlag): bit 0 the scene freeze, bits 1 and 2 Start's pause. The source
    // port's answer counts the same three bits (mu_yougame_pause.h).
    paused = match && host::try_ptr(0x80479D68u, 1) && (host::rd8(0x80479D68u) & 0x7) != 0;
  }
  yougame::report_game_state(yougame::game_state::classify(
      match, paused, yougame::game_state::overlay_menu_open.load(std::memory_order_relaxed)));
}

void tick() {
  report_game_state();
  if (g_bridge && yougame::tournament_active()) g_bridge(MU_TOURNEY_TICK_OP, nullptr, 0);
#ifdef MELEE_DOUBLES
  if (g_bridge && current_major()==1) {
    std::string room; bool search=false; int humans=1;
    { std::lock_guard<std::mutex> lock(yougame::doubles::ui.mutex);
      room=std::exchange(yougame::doubles::ui.pending_room,{});
      search=yougame::doubles::ui.auto_search; humans=yougame::doubles::ui.auto_humans;
    }
    if (room.size()>0) { yougame::doubles::entry(room,search,humans); return; }
  }
#endif
  // Room and picker workers publish state; only this simulation thread touches scenes/CSS.
  auto& friends_lobby = yougame::friends::lobby;
  const bool shared_active = yougame::console::console_active();
  if (g_shared_was_active && !shared_active && friends_lobby.held()) {
    friends_lobby.session_stopped();
    g_shared_css_seen = false;
  }
  g_shared_was_active = shared_active;
  if (yougame::mode_policy::friends_shared_menus && !shared_active && yougame::console::entry.available()) {
    const auto held_room = friends_lobby.take_reenter();
    if (!held_room.empty()) {
      g_friend_css_requested = g_shared_css_seen = false;
      yougame::console::friends_entry(held_room,friends_lobby.host(),true);
    }
  }
  if (shared_active && current_major() == 2) g_shared_css_seen = true;
  if (g_shared_css_seen && current_major() != 2 && current_major() == 1) {
    friends_lobby.leave(); g_shared_css_seen = false;
  }
  if (friends_lobby.host() && !friends_lobby.room().empty() && !g_friend_css_requested) {
    // A room/invite never interrupts VS stage select, fight, sudden death or results.
    // The join worker holds the guest until the ordinary scene path returns to CSS.
    if (current_major() == 2) {
      if (current_minor() == 0) g_friend_css_requested = true;
    } else {
      g_friend_css_requested = true;
      request_major(2); // room FIRST, ordinary local VS CSS SECOND
    }
  }
  if (friends_lobby.loading() && g_friend_picker_stop) *g_friend_picker_stop = true;
  if (friends_lobby.host() && current_major() == 2 && current_minor() == 0 && g_bridge) {
    int32_t a[3] = {};
    if (g_bridge(OP_CSS_PICK,a,3) == 0) friends_lobby.shown({a[0],a[1],a[2]});
    if (friends_lobby.reported() && !g_friend_picker.valid() && friends_lobby.take_invite()) {
      g_friend_picker_stop = std::make_shared<std::atomic<bool>>(false);
      const auto room = friends_lobby.room();
      std::packaged_task<std::string()> task([stop = g_friend_picker_stop,room] {
        return yougame::pick_friend_room([stop] { return stop->load() || yougame::friends::lobby.cancelled(); },room);
      });
      g_friend_picker = task.get_future(); std::thread(std::move(task)).detach();
    }
    if (g_friend_picker.valid() && g_friend_picker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      try { g_friend_picker.get(); }
      catch (const yougame::friends::RoomFull& e) {   // the site's own words, or "This room is full."
        yougame::friends::events.notice(e.what(),
          uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
      }
      catch (const std::exception& e) {
        // Cancel only closes the picker: the room and local CSS stay open. Z invites again.
        if (std::string(e.what()) != "Cancelled") yougame::friends::events.notice("Couldn't open your friends list. Press Z to try again.",
          uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
      }
    }
    yougame::friends::events.picker_open(g_friend_picker.valid());
  }
  if (friends_lobby.host() && friends_lobby.shown() && current_major() != 2) friends_lobby.leave();
  std::string leave_line;
  if (friends_lobby.take_return(leave_line)) {
    if (g_friend_picker_stop) *g_friend_picker_stop = true;
    g_friend_picker = {}; yougame::friends::events.picker_open(false);
    auto retire = yougame::console::leave_friends_room(); // queue end-session + leave before teardown
    yougame::console::console_engine().end(yougame::console::kSelfLeft);
    yougame::console::close_friends_room(std::move(retire));
    g_friend_css_requested = g_shared_css_seen = false;
    if (g_bridge) { int32_t a[1] = {}; g_bridge(OP_FRIENDS_RETURN,a,1); }
    else request_major(1);
    if (!leave_line.empty() && !yougame::friends::page_lobby_lines.load(std::memory_order_acquire)) yougame::friends::events.notice(leave_line,
      uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
  }
  // The friend room filled before this player got a seat (a final refusal): its line is on screen,
  // and the online character select goes back to the Friends menu.
  if (yougame::friends::events.take_full_return(current_major() == kOnlineMajor,
      uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()))) {
    host::log("friends: room full; back to the Friends menu");
    slippi::online::native_cleanup_match();
    if (g_bridge) { int32_t a[1] = {}; g_bridge(OP_FRIENDS_RETURN,a,1); }
    else request_major(1);
  }
  // The Doubles row's press (menu value 16) is taken every tick, before any early return. One made while
  // signing in, or anywhere the home can't open, is dropped and never opens one later. The one wait: the
  // menu counts as safe only 120 ticks after it was entered, and a press in that window was dropped, so a
  // quick player had to press again (report a405af23, 11-17). That press is kept for up to 3 s while the
  // Online menu stays up, and used when the menu is safe.
  static bool doubles_pending = false;
  static std::chrono::steady_clock::time_point doubles_pressed_at;
  if (slippi::online::take_doubles_lobby_request()) {
    doubles_pending = true;
    doubles_pressed_at = std::chrono::steady_clock::now();
  }
  if (doubles_pending && (current_major() != 1 || std::chrono::steady_clock::now() - doubles_pressed_at > std::chrono::seconds(3)))
    doubles_pending = false;
  bool& doubles_press = doubles_pending;
  if (yougame::console::console_active()) { doubles_pending = false; return; }
  if (friends_lobby.held()) { doubles_pending = false; return; } // local CSS remains interactive; the room worker owns preparation
  // This tick runs at retrace even while the scene is frozen. No game state is
  // accessed by the stdin reader; online sessions never touch these pause flags.
  if (yougame::stdio_bridge()) {
    const bool online = slippi::online::session_mode() >= 0 || yougame::tournament_active();
    int32_t a[3] = {-1, 0, 0};
    if (!online && !slippi::playback::enabled()) {
      if (g_bridge) g_bridge(OP_PAGE_PAUSE, a, 3);
      else {
        const uint32_t info = host::rd32(0x804D6720u);
        const int kind = info && host::try_ptr(info, 1) ? host::rd8(info) : -1;
        a[0] = host::rd8(kMinorSceneControl + 0x10) & 1;
        a[1] = (kind == 2 || kind == 3 || kind == 4 || kind == 0x2C) &&
               host::rd8(0x8046B6A0u) == 0;
        a[2] = host::rd32(kMinorSceneControl);
      }
    }
    const uint32_t scene = (uint32_t(current_major()) << 8) | current_minor();
    const auto pause_result = yougame::ux::page_pause.tick(true, online, a[1] != 0, scene, uint32_t(a[2]),
        a[0] > 0, [&](bool paused) {
          if (g_bridge) { int32_t args[3] = {paused ? 1 : 0, 0, 0}; g_bridge(OP_PAGE_PAUSE, args, 3); }
          // Same scheduler API as Source/Start pause, context 0 skips the pause menu.
          else {
            // Retrace interrupts arbitrary guest code; preserve its complete CPU context.
            struct Restore {
              ppc::Context saved = *host::cpu;
              ~Restore() { *host::cpu = saved; ppc::update_mxcsr(*host::cpu); }
            } restore;
            host::call_guest(paused ? 0x801A4634u : 0x801A4674u, 0);
          }
        });
    if (pause_result.released_scene) host::log("pause: released (scene changed)");
    if (pause_result.event) {   // the page greys or restores its Pause button from this
      yougame::pause_event(pause_result.pausable, pause_result.paused, pause_result.reason);
      host::log("pause: told the page pausable %d paused %d (%s)", pause_result.pausable ? 1 : 0,
                pause_result.paused ? 1 : 0, pause_result.reason ? pause_result.reason : "-");
    }
    using PauseOutcome = yougame::ux::PagePause::Outcome;
    switch (pause_result.outcome) {
      case PauseOutcome::None: break;
      case PauseOutcome::Applied: host::log("pause: applied (page)"); break;
      case PauseOutcome::Cleared: host::log("pause: cleared (page)"); break;
      case PauseOutcome::IgnoredOnline: host::log("pause: ignored (online)"); break;
      case PauseOutcome::IgnoredNotInMatch: host::log("pause: ignored (not in a match)"); break;
      case PauseOutcome::IgnoredAlreadyPaused: host::log("pause: ignored (already paused)"); break;
      case PauseOutcome::IgnoredUnownedResume:
        host::log("pause: ignored (resume for a pause the page didn't set)"); break;
    }
  }

  const auto major = current_major(), minor = current_minor();
  slippi::online::slippi_link_tick(major, minor, online_mode());
  if (major != 1) yougame::friends::events.online_menu(false);
  yougame::friends::events.menu_scene(yougame::friends::menu_safe(major, minor));
  // Under Melee Unlocked's launcher (no YouGame app): the Doubles row holds the same 2-seat Doubles home,
  // its tickets through the launcher's bridge. Friends there is a search by connect code, so nothing
  // else of the app's menu flow below runs, unless the local channel to a running YouGame app is up
  // (friends_flow(), below).
  if (!yougame::active() && yougame::launcher() && !slippi::playback::enabled()) {
    if (major == 1) ++g_friend_menu_ticks;
    else g_friend_menu_ticks = 0;
    if (doubles_press && major == 1 && g_bridge && g_friend_menu_ticks >= 120 && yougame::friends::menu_safe(major, minor)) {
      doubles_press = false;
      const auto id = yougame::identity();
      if (!id.ok) {
        host::log("doubles: entry refused (%s)", id.error.empty() ? "not signed in" : id.error.c_str());
        yougame::friends::events.notice(id.error.empty() ? "Signing in to YouGame..." : id.error,
          (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
      } else {
        host::log("doubles: entry opened (the launcher's Doubles row)");
        yougame::friends::events.online_menu(false);
        slippi::online::native_cleanup_match();
        g_lifecycle = Lifecycle{};
        clear_search_metadata();
        set_online_mode(2);
        g_practice = {};
        g_practice.valid = true;
        g_practice.players[0].active = true;
        g_practice.players[0].character = 0;
        write_event_backup();
        if (g_content_bridge) g_content_bridge(2, true);
        slippi::online::arm_friends_lobby({}, yougame::lobby::Profile::Doubles);
        request_major(kOnlineMajor);   // the online major opens the 4-door select for an armed lobby
        return;
      }
    }
  }
  // friends_flow(): the app (active(), as before), or a game started on its own with the local channel up,
  // whose Friends row and accepted invites take this same path. The YouGame tournament stays the app's
  // (below).
  const bool via_channel = !yougame::active();
  if (yougame::friends_flow() && !slippi::playback::enabled()) {
    if (major == kOnlineMajor) g_friend_handoff = false;
    // Wait for the main menu's initialization/cleanup. Never interrupt a local or online match.
    // Over the local channel the launcher's block above has counted this tick already.
    if (!via_channel) {
      if (major == 1) ++g_friend_menu_ticks;
      else g_friend_menu_ticks = 0;
    }
    const bool safe = yougame::friends::menu_safe(major, minor) &&
                      (major != 1 || g_friend_menu_ticks >= 120);
    // The ordinary menu path requests identity as before. Only its validated first hello
    // can schedule a bracket game; boot never waits or consumes the identity wait budget.
    if (!via_channel && g_bridge && major == 1 && safe && yougame::tournament_pending()) {
      g_bridge(MU_TOURNEY_BOOT_OP, nullptr, 0);
      return;
    }
    // Only the Online Friends row owns the page picker.
    const bool picker_scene = major == 1;
    if (g_friend_picker.valid() && (!picker_scene || !safe || !yougame::friends::events.picker_context())) {
      *g_friend_picker_stop = true;
      g_friend_picker = {};
      slippi::online::squad_friend_picker_closed();
    }
    yougame::friends::events.picker_open(g_friend_picker.valid());
    const uint64_t friends_now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (safe && picker_scene && yougame::friends::events.friend_entry()) {
      // A Friends press the host can't act on yet says why on screen; it used to do nothing.
      const auto id = yougame::identity();
      if (!id.ok && yougame::friends::events.take_host()) {
        slippi::online::squad_friend_picker_closed();
        host::log("friends: entry refused (%s)", id.error.empty() ? "not signed in" : id.error.c_str());
        yougame::friends::events.notice(id.error.empty() ? "Signing in to YouGame..." : id.error, friends_now);
      }
    }
    if (doubles_press && picker_scene) {   // the Doubles row says the same as the Friends row
      const auto id = yougame::identity();
      if (!id.ok) {
        host::log("doubles: entry refused (%s)", id.error.empty() ? "not signed in" : id.error.c_str());
        yougame::friends::events.notice(id.error.empty() ? "Signing in to YouGame..." : id.error, friends_now);
        doubles_press = false;
      }
    }
    if (safe && yougame::identity().ok && yougame::console::entry.available()) {
#ifdef MELEE_DOUBLES
      // A join the page marked md:1 opens the Doubles home directly; a plain Friends ticket would be refused.
      if (yougame::friends::events.doubles_join_pending()) {
        auto room=yougame::friends::events.take_join(true);
        if (room.size()>0) { yougame::doubles::entry(room); return; }
      }
#endif
      auto room = yougame::friends::events.take_join(true);
      // An invite into an arena (inviteMode "arena"): the arena joins its YouGame room and the player
      // waits on the online character select (YouGame Friends, the arena's home under YouGame; the game's
      // own mode there is Direct's), where the arena starts each game it puts them in.
      if (!room.empty() && yougame::friends::events.join_arena()) {
        host::log("arena: invite accepted (%s)", yougame::friends::events.trigger().c_str());
        yougame::friends::events.online_menu(false);
        slippi::online::disarm_friends_lobby();
        slippi::online::native_cleanup_match();
        g_lifecycle = Lifecycle{};
        clear_search_metadata();
        set_online_mode(2);
        g_practice = {};
        g_practice.valid = true;
        g_practice.players[0].active = true;
        g_practice.players[0].character = 0;
        write_event_backup();
        if (g_content_bridge) g_content_bridge(2, true);
        slippi::arena::act(slippi::arena::Act::JoinYouGame, 0, room);
        request_major(kOnlineMajor);
        return;
      }
      // A room from an invite (in the menu, the character select or the results screen) is never
      // the "Play a friend" room: only the picker below can return that one.
      bool picked = false;
      const bool host_friend = yougame::friends::events.take_host();
      // Shared menus are off for Friends (mode_policy::friends_shared_menus): the host's press opens
      // the page picker below, and both players meet as known peers from their own CSS.
      if (yougame::mode_policy::friends_shared_menus && g_bridge && host_friend && picker_scene &&
          !yougame::console::entry.active()) {
        slippi::online::native_cleanup_match();
        g_friend_css_requested = false;
        yougame::friends::events.online_menu(false);
        yougame::console::friends_entry("new");
        return;
      }
      // The 4-player Friends lobby (mode_policy::friends_lobby4): the Friends row opens Melee's 4-door select
      // and holds a room there (the select's Invite doors open the page's picker); an accepted invite joins
      // that select. Squad Strike's "Play a friend", an invite into a 1v1 room and the static recomp (no
      // 4-door select) keep the friend room below.
      if (yougame::mode_policy::friends_lobby4 && g_bridge && !yougame::console::entry.active() &&
          !slippi::online::squad_friend_intent() && !g_friend_picker.valid()) {
        // The Doubles home (G2) is the same lobby on its 2-seat profile: the Doubles row (menu value 16) holds
        // a new home; an invite into a Doubles home joins one.
        const bool doubles_host = picker_scene && room.empty() && std::exchange(doubles_press, false);
        // The room decides (friends::Events::room_kind): once joined, a lobby room's first frames carry
        // welcome.muLobby or a yg.mu.lobby state (2 seats and `searching`: the Doubles home) and a 1v1 room's
        // neither; a room of another kind refuses the ticket. The search that joined it the wrong way queues
        // the join again the next way (room_retry, room_decided). Until a room has said, the invite picks the
        // first try: a Teams invite (no md) or a push naming nothing the Doubles home, a size or compatibility
        // key the Friends lobby or 1v1.
        const int kind = room.empty() ? -1 : yougame::friends::events.room_kind(room);
        const bool doubles_guest = !room.empty() && (kind >= 0 ? kind == 2 : yougame::friends::events.join_doubles());
        const bool lobby_host = !doubles_host && picker_scene && host_friend && room.empty();
        const bool lobby_guest = !doubles_guest && !room.empty() && (kind >= 0 ? kind == 1 : yougame::friends::events.join_players() == 4);
        const bool doubles = doubles_host || doubles_guest;
        if (doubles || lobby_host || lobby_guest) {
          host::log("%s: %s (%s)", doubles ? "doubles" : "friends lobby",
                    doubles_host || lobby_host ? "entry opened" : "invite accepted", yougame::friends::events.trigger().c_str());
          // Over the local channel the lobby is a YouGame search like a YouGame row's (session()); a no-op in the app.
          if (via_channel) yougame::set_launcher_session(true);
          yougame::friends::events.online_menu(false);
          slippi::online::native_cleanup_match();
          g_lifecycle = Lifecycle{};
          clear_search_metadata();
          set_online_mode(2);
          g_practice = {};
          g_practice.valid = true;
          g_practice.players[0].active = true;
          g_practice.players[0].character = 0;
          write_event_backup();
          if (g_content_bridge) g_content_bridge(2, true);
          slippi::online::arm_friends_lobby(room, doubles ? yougame::lobby::Profile::Doubles : yougame::lobby::Profile::Friends4);
          request_major(kOnlineMajor);   // the online major opens the 4-door select for an armed lobby
          return;
        }
      }
      // Local menus: the page picks or makes the room; static recomp uses the same entry.
      if (picker_scene && host_friend && !g_friend_picker.valid() && room.empty() && !yougame::console::entry.active()) {
        host::log("friends: entry opened (%s)", yougame::friends::events.trigger().c_str());
        g_friend_picker_stop = std::make_shared<std::atomic<bool>>(false);
        std::packaged_task<std::string()> task([stop = g_friend_picker_stop] {
          return yougame::pick_friend_room([stop] { return stop->load(); });
        });
        g_friend_picker = task.get_future();
        std::thread(std::move(task)).detach();
      }
      if (g_friend_picker.valid()) {
        if (!room.empty() || !yougame::friends::events.picker_context()) {
          *g_friend_picker_stop = true;
          g_friend_picker = {};
          if (room.empty()) slippi::online::squad_friend_picker_closed();
        } else if (g_friend_picker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
          try { room = g_friend_picker.get(); picked = !room.empty(); }
          catch (const yougame::friends::RoomFull& e) {   // the site's own words, or "This room is full."
            slippi::online::squad_friend_picker_closed();
            yougame::friends::events.picker_error(e.what());
            yougame::friends::events.notice(e.what(), friends_now);
          }
          catch (const std::exception& e) {
            slippi::online::squad_friend_picker_closed();   // cancelled or failed: Squad Strike off
            yougame::friends::events.picker_error(e.what());
            const std::string why = e.what();
            if (why != "Cancelled" && why != yougame::friends::kVersion)  // a version refusal has its own toast
              yougame::friends::events.notice("Couldn't open your friends list. Press A to try again.", friends_now);
          }
        }
      }
      yougame::friends::events.picker_open(g_friend_picker.valid());
      if (!room.empty()) {
        slippi::online::squad_friend_room(room, picked);
        if (via_channel) yougame::set_launcher_session(true);   // the friend room is a YouGame search (session())
        yougame::friends::events.online_menu(false);
        // An armed Friends lobby (this invite accepted on its select) is let go first: its select's unload
        // then sends no LEAVE into the new 1v1 connection (mu_slippi_menu.c lobby_close, view.arm).
        slippi::online::disarm_friends_lobby();
        slippi::online::native_cleanup_match();
        g_lifecycle = Lifecycle{};
        clear_search_metadata();
        if (yougame::mode_policy::friends_shared_menus && g_bridge) {
          yougame::console::friends_entry(room);
          room.clear();
        } else {
          // Known-peer rollback in the friend room: each player keeps their own CSS, picks go as
          // 0x82 (a Squad lineup as 0xE3 just before it), and the Direct winner/loser flow runs the
          // rematch in the same room. Both start as winners, so game 1 takes a random stage.
          set_online_mode(2);
          g_practice = {};
          g_practice.valid = true;
          g_practice.players[0].active = true;
          g_practice.players[0].character = 0;
          write_event_backup();
          if (g_bridge) { int32_t a[1] = {yougame::friends::kFirstCssWinner}; g_bridge(OP_DIRECT_FIRST_MATCH, a, 1); }
          else {
            host::wr8(kDirectIsWinner, yougame::friends::kFirstCssWinner);
            host::wr8(kDirectChoseStage, 0);
            // Keep a menu's ordinary decide callback from replacing the accepted destination.
            static bool hooks = false;
            if (!hooks) {
              hooks = true;
              const auto redirect = [](ppc::Context& context) {
                if (g_friend_handoff) context.r[3] = kOnlineMajor;
              };
              ppc::add_entry_hook(0x801A42E8u, redirect);
              ppc::add_entry_hook(0x801A42F8u, redirect);
            }
          }
          if (g_content_bridge) g_content_bridge(2, true);
          std::string error;
          const bool started = slippi::online::native_start_match(2, {}, 0, 0, &error, room);
          g_friend_handoff = !g_bridge;
          request_major(kOnlineMajor);  // the CSS also shows any policy/start error
          if (!started) host::log("yougame: could not join friends: %s", error.c_str());
        }
      }
    }
  }
  // Launcher matches start only once the title/main-menu scene is initialized. Both players have
  // explicitly agreed already; the first main supplies an initial fighter to Slippi's normal CSS.
  // This coordinator runs only for static recomp; native uses its game-side online scene bridge.
  const auto& lobby = slippi::online::config();
  // (The Source Port enters lobby matches through its own online scene bridge instead.)
  if (!g_bridge && !lobby.lobby_code.empty() && !g_lobby_started && !slippi::playback::enabled()) {
    const uint8_t major = host::rd8(kSceneState);
    if ((major == 0 || major == 1) && ++g_lobby_boot_ticks >= 120) {
      g_lobby_started = true;
      // Boot/main-menu decide callbacks normally replace the pending destination. During this
      // single accepted handoff, redirect their scene setter through the ordinary online major.
      // These hooks stop changing arguments as soon as Slippi owns the online scene.
      const auto redirect = [](ppc::Context& context) {
        if (g_lobby_started && g_lifecycle.phase() == Phase::Handoff)
          context.r[3] = kOnlineMajor;
      };
      ppc::add_entry_hook(0x801A42E8u, redirect); // gm_SetPendingGameMode
      ppc::add_entry_hook(0x801A42F8u, redirect); // gm_ChangeGameModeAfterCurrentScene
      g_origin_major = 1;
      g_restore_training = false;
      g_practice = {};
      g_practice.valid = true;
      g_practice.players[0].active = true;
      g_practice.players[0].character = (uint32_t)lobby.lobby_character;
      g_match_mode = MatchMode::Direct;
      g_connect_code = lobby.lobby_code;
      g_search_ticks = 0;
      if (g_lifecycle.begin_search()) {
        host::wr8(kOnlineMode, 2);
        std::string error;
        if (!slippi::online::native_start_match(2, g_connect_code, (uint8_t)lobby.lobby_character, 0, &error))
          fail(error.empty() ? "Could not start the lobby match" : error, true);
        ++g_generation;
      }
    }
  }
  // Test only (MELEE_PRACTICE_AUTOSTART=unranked, teams or direct:CODE): start one search after two
  // seconds of offline gameplay, as pressing Tab and choosing would. Hidden test runs have no Tab.
  {
    static const char* autostart = std::getenv("MELEE_PRACTICE_AUTOSTART");
    static uint32_t offline_ticks = 0;
    static bool fired = false;
    if (autostart && !fired && g_lifecycle.phase() == Phase::Idle && is_offline_gameplay()) {
      if (++offline_ticks >= 120) {
        fired = true;
        const std::string spec = autostart;
        host::log("native practice: test autostart %s", spec.c_str());
        if (spec.rfind("direct:", 0) == 0) submit_start_direct(spec.substr(7));
        else if (spec == "teams") start_search(MatchMode::Teams, {});
        else if (spec.rfind("teams:", 0) == 0) submit_start_teams(spec.substr(6));
        else submit_start_unranked();
      }
    }
  }
  // Tests: MELEE_TEST_WAIT_PRACTICE=<frames>: the WARM UP button pressed once (its command), that many
  // frames after it was first offered with nobody connected (Direct, Teams).
  {
    static const char* spec = host::test_env::getenv("MELEE_TEST_WAIT_PRACTICE");
    static uint32_t ticks = 0;
    static bool done = false;
    if (spec && !done) {
      MatchMode mode = MatchMode::None;
      std::string code;
      if (wait_practice_possible(&mode, &code) && ++ticks >= (uint32_t)std::atoi(spec)) {
        done = true;
        host::log("wait-practice test: WARM UP pressed (%s, code %s)", match_mode_name(mode), code.c_str());
        process_command({CommandKind::Warmup, {}});
      }
    }
  }
  // L+R held on Slippi's online character select in Unranked (not with A or Start, so L+R+A+Start
  // still resets): go to Training to warm up, and the Unranked search starts again by itself once
  // the Training match is running. A third of a second of holding, so brushing both triggers does
  // nothing. Leaving Training for anywhere else first drops the pending search.
  // With someone connected (Direct, Teams), or waiting for a turn in an arena room, the same hold
  // goes to the warm-up room instead: Training with the connection kept (warmup_enter).
  {
    static uint32_t lr_ticks = 0;
    bool lr = false;
    static int lr_port = 0;
    static MatchMode lr_mode = MatchMode::None;
    bool warm = false, keep = false;
    if (g_lifecycle.phase() == Phase::Idle && g_auto_search == MatchMode::None && !g_warmup.active() && is_online_scene() &&
        current_minor() == 0 && !slippi::playback::enabled()) {
      bool arena = false;
      std::string why;
      warm = warmup_offer(&arena, &why);
      // A Direct or Teams pair that is connected but locked in stays: the hold does nothing then
      // (the button says "Locked in"). Before a pair connects the hold works as it always did.
      const bool connected_pair = !warm && slippi::online::warmup_link().link_up &&
                                  (online_mode() == (uint8_t)MatchMode::Direct || online_mode() == (uint8_t)MatchMode::Teams);
      const bool plain = !connected_pair &&
          (online_mode() == (uint8_t)MatchMode::Unranked || online_mode() == (uint8_t)MatchMode::Direct ||
           online_mode() == (uint8_t)MatchMode::Teams) && !slippi::online::is_online_match();
      // A search running that can be kept (any YouGame mode, Ranked too, and Unranked): Training with
      // the search kept as it is, never started again.
      keep = !warm && kept_possible();
      if (warm || plain || keep) {
        lr_mode = (MatchMode)online_mode();
        host::PadState pads[4];
        host::input_last_pads(pads);
        for (int port = 0; port < 4; ++port)
          if ((pads[port].button & 0x0060u) == 0x0060u && !(pads[port].button & (0x0100u | 0x1000u))) { lr = true; lr_port = port; }
      }
    }
    lr_ticks = lr ? lr_ticks + 1 : 0;
    g_warmup_hold = warm && lr ? std::min(1.0f, (float)lr_ticks / 20.0f) : (g_warmup.active() ? g_warmup_hold : 0.0f);
    if (warm && lr_ticks == 20) {
      host::log("native practice: L+R on the online character select; warm-up room");
      warmup_enter(lr_port);
      lr_ticks = 21;
    }
    if (warm) lr_ticks = std::min<uint32_t>(lr_ticks, 21);
    if (keep && lr_ticks == 20) {
      host::log("native practice: L+R on the online character select while searching");
      kept_go_training(lr_port, "L+R held on the select while searching");
      lr_ticks = 21;
    }
    if (keep) lr_ticks = std::min<uint32_t>(lr_ticks, 21);
    // Direct: practice while waiting for the code's player, with the code being searched for (or
    // the last one used). No code at all: nothing to search, so L+R does nothing there.
    std::string lr_code;
    if (lr_ticks == 20 && (lr_mode == MatchMode::Direct || lr_mode == MatchMode::Teams)) {
      std::string error;
      const std::string last = slippi::online::direct_code_for_practice();
      if (!(lr_mode == MatchMode::Teams ? normalize_teams_code(last, &lr_code, &error) : normalize_direct_code(last, &lr_code, &error))) {
        host::log("native practice: L+R on the Direct character select with no code to search for");
        lr_ticks = 21;
      }
    }
    if (lr_ticks == 20) {
      host::log("native practice: L+R on the online character select");
      wait_practice_go(lr_port, lr_mode, lr_code);
    }
  }
  if (g_auto_search != MatchMode::None) {
    if (g_lifecycle.phase() != Phase::Idle) {
      g_auto_search = MatchMode::None;
    } else if (is_offline_gameplay()) {
      if (++g_auto_ticks >= 30) {
        const MatchMode mode = g_auto_search;
        g_auto_search = MatchMode::None;
        host::log("native practice: Training match running; starting the %s search", match_mode_name(mode));
        start_search(mode, mode == MatchMode::Unranked ? std::string() : g_auto_code);
      }
    } else if (!is_training() && !is_online_scene()) {
      host::log("native practice: left Training before its match; the pending search is dropped");
      g_auto_search = MatchMode::None;
    }
  }

  frame_advance_tick();

  // Fight a pro: which characters the memory card has, once a second in Training (a game library
  // without the question leaves it unknown, which offers every pro as before).
  {
    static int unlocked_ticks = 0;
    if (g_bridge && is_training() && ++unlocked_ticks >= 60) {
      unlocked_ticks = 0;
      int32_t a[1] = {0};
      if (g_bridge(OP_UNLOCKED, a, 1) == 0) g_unlocked.store((uint32_t)a[0], std::memory_order_relaxed);
    }
  }

  // MELEE_TEST_AFTER_SESSION=<tick> (tests only): at that tick on the main menu, the step an ended
  // online session takes (Unranked), so the way into Training and the prompt can run without a peer.
  {
    static const int test_at = [] { const char* v = host::test_env::getenv("MELEE_TEST_AFTER_SESSION"); return v ? std::atoi(v) : -1; }();
    static int ticks = 0;
    if (test_at >= 0 && ++ticks == test_at && g_lifecycle.phase() == Phase::Idle) enter_training_after_session(MatchMode::Unranked, -1, [] { const char* c = host::test_env::getenv("MELEE_TEST_P1_CHAR"); return c ? std::atoi(c) : 2; }());   // Fox, or MELEE_TEST_P1_CHAR
    // MELEE_TEST_TRAIN_AGAINST=<tick>,<character> (tests only): a ghost's character picked at that tick.
    static int against_at = -1, against = -1;
    static const bool parsed = [] { if (const char* v = host::test_env::getenv("MELEE_TEST_TRAIN_AGAINST")) std::sscanf(v, "%d,%d", &against_at, &against); return true; }();
    (void)parsed;
    if (against_at >= 0 && ticks == against_at) submit_train_against(against);
  }

  // The requeue prompt shows once the Training match runs, and goes when the player leaves Training
  // or a search starts.
  if (g_offer_mode != MatchMode::None) {
    if (!g_offer_shown && is_training() && current_minor() == 2 && g_lifecycle.phase() == Phase::Idle) {
      g_offer_shown = true;
      ++g_generation;
      host::log("native practice: Training is running; asking whether to search %s again", match_mode_name(g_offer_mode));
    } else if (g_offer_shown && (!is_training() || current_minor() != 2 || g_lifecycle.phase() != Phase::Idle)) {
      g_offer_shown = false;
      g_offer_mode = MatchMode::None;
      ++g_generation;
    }
  }

  std::deque<Command> commands;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    commands.swap(g_commands);
  }
  for (Command& command : commands) process_command(std::move(command));

  warmup_tick();
  warmup_test_tick();
  kept_tick();
  scene_route_test_tick();
  // This player's pick on the online character select, for an opponent on our build to see
  // ("picked Falco") before any lock-in. A few times a second; sent only when it changes.
  {
    static uint32_t pick_ticks = 0;
    if (!g_warmup.active() && is_online_scene() && current_minor() == 0 && ++pick_ticks % 6 == 0) {
      uint8_t character = 0xFF, costume = 0;
      if (!slippi::arena::css_pick(&character, &costume)) {
        const PracticeConfig css = capture_practice();
        if (css.valid && css.players[css.local_slot].character < 26) {
          character = (uint8_t)css.players[css.local_slot].character;
          costume = css.players[css.local_slot].costume;
        }
      }
      slippi::online::warmup_note_pick(character < 26 ? character : -1, costume);
    }
  }

  // "After an online session ends, go back to Training mode" (Game tab, Online): when the session
  // ends with at least one match played (the other player left, Z held to leave, the connection
  // dropped) and the player is still on the online screens, Training's character select. Only once
  // the session is over, so nothing changes during a match or between a pair's games.
  {
    static yougame::unranked::SessionWatch session_watch;
    static int session_mode_seen = -1, local_index_seen = -1;
    static std::string session_code;
    // A parked session (the warm-up room) is still the same session: nothing has ended.
    const bool session = slippi::online::session_mode_or_parked() >= 0;
    const bool match_running = slippi::online::is_online_match();
    // A session that was an arena room's (in the room, or its room just ended) is never a finished
    // online session: the room's last game stays "in a match" on the select until its connection is
    // cleaned up, which can be long after the room closed (0.9.201 test: the host closed the room).
    static bool from_room = false, session_was = false;
    if (slippi::arena::in_room() || slippi::arena::room_just_ended()) from_room = true;
    else if (!session && !session_was) from_room = false;   // no session left over: the next one is plain
    const bool session_ended = session_was && !session;
    session_was = session;
    if (session) {
      session_mode_seen = slippi::online::session_mode_or_parked();
      if (match_running) {
        local_index_seen = slippi::online::local_player_index();
        // The opponent's character, for Training's CPU afterwards (once a second; the slots are
        // cleared when the next match starts).
        static uint32_t seen_ticks = 0;
        if (++seen_ticks % 60 == 0) {
          const PracticeConfig match = capture_practice();
          for (int i = 0; i < 4; ++i)
            if (i != local_index_seen && match.players[i].active && match.players[i].character < 26) {
              g_last_opponent = (int)match.players[i].character;
              g_last_opponent_costume = match.players[i].costume;
              break;
            }
        }
      }
    }
    if (session_watch.tick(session, match_running, slippi::online::friends_lobby_armed())) {
      // The guest mode also covers local Ranked practice, which has no real
      // matchmaking session and must not inherit an earlier session's return.
      const bool allow_training = yougame::unranked::allow_training_return(yougame::active(), session_mode_seen) &&
                                  online_mode() != 0;
      // Unranked ends its session after every game, so there it would leave the online screens after
      // each one (two reports); the player stays on Unranked's character select, as on Slippi.
      const bool unranked = session_mode_seen == (int)MatchMode::Unranked;
      // In an arena room a pair ending is the rotation: the room starts the next match from here.
      // Leaving a room, or the room closing, ends the pair too: the player stays on the select.
      const bool arena_room = slippi::arena::in_room() || from_room;
      from_room = false;
      // The opponent left while this player warmed up: the player is back on the select, and stays there.
      const bool warmup_left = g_warmup_no_bounce;
      if (warmup_left) host::log("native practice: the opponent left during the warm-up; the player stays on the select");
      if (!warmup_left && allow_training && !arena_room && gx::RenderOptions::live_online_back_to_training() && !unranked && g_lifecycle.phase() == Phase::Idle &&
          is_online_scene() && !slippi::playback::enabled()) {
        enter_training_after_session((MatchMode)session_mode_seen, local_index_seen);
      } else if (!warmup_left && allow_training && !arena_room && gx::RenderOptions::live_online_return_training() && g_lifecycle.phase() == Phase::Idle &&
          is_online_scene()) {
        g_origin_major = kTrainingMajor;
        g_restore_training = false;
        host::log("native practice: online session over; going to Training (setting on)");
        run_decision(g_lifecycle.return_after_session());
      }
    }
    if (session_ended) g_warmup_no_bounce = false;   // read once, when that session ended
  }

  // Entering major 8 directly skips the title-menu option handler that normally owns this byte.
  // Keep the native selection authoritative during the scene transition, then leave every later
  // CSS decision to Slippi's normal online flow.
  if (phase_owns_online_mode(g_lifecycle.phase()) && g_match_mode != MatchMode::None) {
    const uint8_t expected = (uint8_t)g_match_mode;
    const uint8_t actual = online_mode();
    if (actual != expected) {
      host::log("native practice: correcting guest online mode %u -> %u during handoff",
                actual, expected);
      set_online_mode(expected);
    }
  }

  if (g_lifecycle.phase() == Phase::Searching) {
    ++g_search_ticks;
    // Teams waits for three other players: ten minutes, where Unranked and Direct give up after 90 s.
    if (g_search_ticks > (g_match_mode == MatchMode::Teams ? 60u * 600u : kSearchTimeoutTicks)) {
      fail(std::string("The ") + match_mode_name(g_match_mode) + " search timed out", true);
    } else {
      const slippi::online::NativeMatchPoll poll = slippi::online::native_poll_match();
      g_matchmaking_state = poll.process_state;
      if (!poll.opponent.empty()) g_opponent = poll.opponent;
      if (poll.error.empty() && slippi::online::session_mode() < 0)
        fail("The connection ended before the match started", true);
      else
        run_decision(g_lifecycle.matchmaking_result(poll.connection_success, poll.local_ready,
                                                    poll.remote_ready, poll.error));
    }
  } else if (g_lifecycle.phase() == Phase::Handoff ||
             g_lifecycle.phase() == Phase::OnlineFlow ||
             g_lifecycle.phase() == Phase::InMatch) {
    const Phase before = g_lifecycle.phase();
    run_decision(g_lifecycle.observe_session(is_online_scene(),
                                             slippi::online::session_mode() >= 0,
                                             slippi::online::is_online_match()));
    if (before == Phase::InMatch && g_lifecycle.phase() == Phase::Idle)
      clear_search_metadata();
  } else if (g_lifecycle.phase() == Phase::ReturningToPractice) {
    const bool origin_ready = current_major() == g_origin_major &&
                              (!g_restore_training || current_minor() == 2);
    if (origin_ready) ++g_return_ticks;
    else g_return_ticks = 0;
    if (g_return_ticks >= 3) {
      if (g_restore_training) restore_practice_fields();
      g_lifecycle.practice_restored();
      clear_search_metadata();
      ++g_generation;
      host::log("native practice: offline scene %02X restored%s", g_origin_major,
                g_restore_training ? " with Training configuration" : "");
    }
    // The return is a request to the game, and the player can leave for somewhere else before it
    // lands (or it never lands). Without this the phase stayed for the rest of the session, with
    // its notice on screen in every later scene, online matches included.
    // Time spent in the origin scene itself (Training's character select) is not counted: the
    // fields are restored when its match starts, however long the player takes there.
    else if (slippi::online::is_online_match() ||
             (current_major() != g_origin_major && ++g_return_wait_ticks > kReturnTimeoutTicks)) {
      host::log("native practice: return to offline scene %02X given up (%s)", g_origin_major,
                slippi::online::is_online_match() ? "an online match started" : "timed out");
      g_lifecycle.practice_restored();
      clear_search_metadata();
      ++g_generation;
    }
  }

  if (g_lifecycle.phase() != g_logged_phase) {
    g_logged_phase = g_lifecycle.phase();
    host::log("native practice: phase %s", phase_name(g_logged_phase));
  }
  publish_snapshot();
}


// Every controller as if let go (buttons up, sticks and triggers at rest); disconnected ones stay so.
static void neutral_pads(host::PadState* pads) {
  for (int i = 0; i < 4; ++i) {
    host::PadState& p = pads[i];
    p.button = 0;
    p.stick_x = p.stick_y = p.sub_x = p.sub_y = 0;
    p.trig_l = p.trig_r = p.analog_a = p.analog_b = 0;
  }
}

void apply_pads(host::PadState* pads) {
  constexpr uint16_t kDown = 0x0004, kUp = 0x0008, kA = 0x0100, kStart = 0x1000;
  // Counted in frames, not calls: the Static Recomp's game reads the pads many times a frame on some
  // menus (about 24 on Training's character select), which ran the 15-second limit out in 38 frames.
  static uint32_t last_retrace = 0;
  const uint32_t retrace = host::retrace_count();
  const bool new_frame = retrace != last_retrace;
  last_retrace = retrace;
  kept_select_pads(pads, new_frame);
  if (g_warmup_autostart) {
    // Back from the warm-up because everyone else is ready: Start on the select, every few frames
    // until the game takes it (it ignores input for a moment after it comes up). Never on a select
    // that lost the connection (Start would open the code entry there).
    const uint8_t major = current_major(), minor = current_minor();
    if (major == kOnlineMajor && minor == 0) {
      const slippi::online::ArenaMatchStatus st = slippi::online::arena_match_status();
      if (st.state != 4 /* CONNECTION_SUCCESS */ || st.local_ready) {
        g_warmup_autostart = false;
        host::log("native practice: warm-up: %s (retrace %u)", st.local_ready ? "locked in for the match" : "no connection to start the match on",
                  host::retrace_count());
      } else {
        if (new_frame) ++g_warmup_autostart_ticks;
        if (g_warmup_autostart_ticks % 6 == 1) pads[g_warmup_css.controller_port & 3].button |= kStart;
        if (g_warmup_autostart_ticks > 300) { g_warmup_autostart = false; host::log("native practice: warm-up: the select never took Start"); }
      }
    } else if (major == kOnlineMajor && minor != 0) {
      g_warmup_autostart = false;   // the stage select (the loser picks) or the match itself
      host::log("native practice: warm-up: on to the %s", minor == 1 ? "stage select" : "match");
    } else if (major != kOnlineMajor && !g_warmup.active()) {
      g_warmup_autostart = false;   // the way back ended somewhere else (it is kept through the main menu step)
    }
  }
  if (g_enter_training) {
    const uint8_t major = current_major(), minor = current_minor();
    if (new_frame) ++g_enter_ticks;
    if (major == kTrainingMajor && minor < 2) g_enter_seen_select = true;
    // The player's own input on Training's menus hands them back at once: the automatic picks below
    // hold every controller neutral, and when they did not take, nothing answered for 15 seconds.
    // Only a change from what was held when the menus came up counts (an A still held is no press).
    static uint16_t held_at_entry[4];
    static bool entry_seen = false;
    bool touched = false;
    if (major == kTrainingMajor && minor == 0) {   // the character select (the stage is picked at once)
      for (int i = 0; i < 4; ++i) {
        const host::PadState& p = pads[i];
        if (p.err != 0) continue;
        if (!entry_seen) held_at_entry[i] = p.button;
        const uint16_t fresh = (uint16_t)(p.button & ~held_at_entry[i]);
        held_at_entry[i] &= p.button;   // a released button counts again when pressed
        if (entry_seen && (fresh || std::abs((int)p.stick_x) > 60 || std::abs((int)p.stick_y) > 60)) touched = true;
      }
      entry_seen = true;
    } else {
      entry_seen = false;
    }
    // Tests (MELEE_TEST_WARMUP): the bots' match inputs are no player taking over Training's menus.
    static const bool warmup_test = host::test_env::getenv("MELEE_TEST_WARMUP") != nullptr;
    if (warmup_test) touched = false;
    if (touched && g_enter_ticks > 20) {
      g_enter_training = false;
      entry_seen = false;
      host::log("native practice: the player took over Training's menus (scene %02X:%02X)", major, minor);
    } else if (major == kTrainingMajor && minor >= 2 && g_enter_seen_select) {
      g_enter_training = false;
    } else if (g_enter_ticks > 60 * 15 || slippi::online::is_online_match() ||
               (major != kTrainingMajor && major != kOnlineMajor && g_enter_ticks > 180)) {
      g_enter_training = false;
      host::log("native practice: stopped pressing through Training's menus (scene %02X:%02X, %u frames, online match %d)",
                major, minor, g_enter_ticks, slippi::online::is_online_match() ? 1 : 0);
    } else if (major == kTrainingMajor && minor == 1) {
      neutral_pads(pads);
      // Stage select: the same stage as before (Final Destination when there was none), picked at once.
      // Both engines run the same stage select: it takes SSSData::force_stage_id as a pick (its enter
      // clears it, so it is set while the select runs). The Static Recomp's is Training's own SSSData,
      // a static at 0x8048E378 (gmtrainingmode.c), the field at +3.
      const int legal[] = {2, 3, 8, 28, 31, 32};
      int stage = 32;
      for (int s : legal) if ((int)g_practice.stage == s) stage = s;
      if (g_bridge) {
        int32_t a[1] = {stage};
        g_bridge(OP_TRAINING_STAGE, a, 1);
      } else {
        host::wr8(0x8048E378u + 3u, (uint8_t)stage);
      }
    } else if (major == kTrainingMajor) {
      // Character select: only Start, which confirms the remembered pick (the Static Recomp also takes A
      // on the stage). Every controller is neutral meanwhile: an A still held from the Practice menu, or a
      // nudged stick, picked whatever character was under the cursor instead.
      neutral_pads(pads);
      if (g_enter_ticks % 40 < 3) pads[g_enter_port & 3].button |= minor == 0 ? kStart : kA;
    } else {
      neutral_pads(pads);   // on the way into Training
    }
  }
  // Tests (MELEE_TEST_WARMUP): the online stage select (a rematch's loser counterpicks) takes the
  // stage under the cursor, as a player pressing A would.
  {
    static const bool test = host::test_env::getenv("MELEE_TEST_WARMUP") != nullptr;
    static uint32_t sss_ticks = 0;
    if (test && is_online_scene() && current_minor() == 1) {
      // The cursor starts at the left edge, off the icons: right onto the grid first, then A.
      const uint32_t t = new_frame ? ++sss_ticks : sss_ticks;
      if (t > 20 && t <= 44) pads[0].stick_x = 100;
      if (t > 60 && t % 40 == 20) { pads[0].button |= kA; if (new_frame) host::log("warm-up test: A on the stage select"); }
      if (t > 60 && t % 40 == 0) pads[0].stick_x = 80;   // nudge on if that was no stage
    } else {
      sss_ticks = 0;
    }
    static uint32_t start_ticks = 0;
    if (g_test_press_start && is_online_scene() && current_minor() == 0 && !slippi::online::arena_match_status().local_ready) {
      if (new_frame) ++start_ticks;
      if (start_ticks % 40 == 1) pads[0].button |= kStart;
    } else {
      if (g_test_press_start && (!is_online_scene() || current_minor() != 0 || slippi::online::arena_match_status().local_ready))
        g_test_press_start = false;
      start_ticks = 0;
    }
  }
  if (g_offer_shown) {
    static uint16_t held = 0xFFFF;   // nothing held when the prompt appears counts as a press
    host::PadState& p = pads[g_practice.controller_port & 3];
    // Up or Down on its own: Start + D-pad Up + Z (the Practice menu) and other combinations pass through.
    const bool alone = p.err == 0 && (p.button & (uint16_t)~(kUp | kDown)) == 0;
    const uint16_t now = alone ? (uint16_t)(p.button & (kUp | kDown)) : 0;
    const uint16_t fresh = (uint16_t)(now & ~held);
    held = p.err == 0 ? (uint16_t)(p.button & (kUp | kDown)) : 0;
    if (alone) p.button &= (uint16_t)~(kUp | kDown);   // the prompt's buttons never reach the game
    if (fresh & kUp) {
      const MatchMode mode = g_offer_mode;
      g_offer_shown = false;
      g_offer_mode = MatchMode::None;
      held = 0xFFFF;
      host::log("native practice: search again (%s) from the prompt", match_mode_name(mode));
      if (mode == MatchMode::Direct) submit_start_direct(g_offer_code);
      else if (mode == MatchMode::Teams) submit_start_teams(g_offer_code);
      else submit_start_unranked();
    } else if (fresh & kDown) {
      g_offer_shown = false;
      g_offer_mode = MatchMode::None;
      held = 0xFFFF;
      host::log("native practice: the search-again prompt dismissed");
    }
  }
}

void shutdown() {
  if (g_lifecycle.phase() != Phase::Idle && g_lifecycle.phase() != Phase::InMatch)
    slippi::online::native_cleanup_match();
  if (g_content_bridge) {
    g_content_bridge(-1, true);
    g_content_bridge = nullptr;
  }
  g_lifecycle.reset();
  g_practice = {};
  g_restore_training = false;
  clear_search_metadata();
  // The launcher's settings window shuts Slippi down without ever loading the game, so there is no
  // guest RAM to read the scene from; publishing then crashed every close of that window.
  if (host::ram) publish_snapshot();
}

bool cosmetic_profile_locked() { return snapshot().cosmetic_profile_locked; }

void set_native_bridge(NativeBridge bridge) { g_bridge = bridge; }
void set_content_bridge(ContentBridge bridge) { g_content_bridge = bridge; }

const char* match_mode_name(MatchMode mode) {
  switch (mode) {
    case MatchMode::Unranked: return "Unranked";
    case MatchMode::Direct: return "Direct";
    case MatchMode::Teams: return "Teams";
    case MatchMode::None: return "matchmaking";
  }
  return "matchmaking";
}

}  // namespace slippi::native_practice
