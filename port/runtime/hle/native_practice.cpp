// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_practice.h"

#include "host.h"
#include "practice_cpu.h"
#include "slippi_online.h"
#include "slippi_playback.h"
#include "../gx/render_options.h"

#include <array>
#include <atomic>
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

enum class CommandKind { StartUnranked, StartDirect, StartTeams, Cancel, AcknowledgeFailure, TrainAgainst };
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
// After an online session (the other player left, the connection dropped, the player left): back
// into a Training match with the same character, then "search again?". g_enter_* presses through
// Training's character and stage select for the player; g_offer_* is the prompt.
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
       OP_TRAINING_PICK, OP_TRAINING_STAGE, OP_UNLOCKED };
std::atomic<uint32_t> g_unlocked{0xFFFFFFFFu};   // tick(): the card's unlocked characters, read in Training

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

// Training's character select starts from its saved match (Source Port; the Static Recomp keeps the
// player's last Training picks). slot 0: the player, 1: the CPU.
void set_training_pick(int slot, int character, int costume) {
  if (!g_bridge || character < 0 || character >= 26) return;
  int32_t a[3] = {slot, character, costume};
  g_bridge(OP_TRAINING_PICK, a, 3);
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
      if (axis == 0 && std::getenv("MELEE_TEST_FRAME_ADVANCE")) host::log("practice: CPU moved to x %.1f", v);
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
  static const bool test = std::getenv("MELEE_TEST_FRAME_ADVANCE") != nullptr;
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
      const std::string v = std::getenv("MELEE_TEST_FRAME_ADVANCE");
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

void process_command(Command command) {
  switch (command.kind) {
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
      if (!normalize_direct_code(command.text, &code, &error)) { fail(error, false); return; }
      start_search(MatchMode::Teams, code);
      break;
    }
    case CommandKind::Cancel: {
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
      if (!g_bridge || !is_training() || current_minor() != 2 || g_lifecycle.phase() != Phase::Idle) return;
      const PracticeConfig now = capture_practice();
      if (!now.valid) return;
      for (const SavedPlayer& p : now.players)
        if (p.active && p.player_kind == 1 && (int)p.character == character) return;   // already that character
      const SavedPlayer& me = now.players[now.local_slot];
      g_practice = now;
      g_offer_shown = false;   // a prompt from an online session goes with the match it was shown in
      g_offer_mode = MatchMode::None;
      set_training_pick(0, (int)me.character, me.costume);
      set_training_pick(1, character, 0);
      g_enter_training = true;
      g_enter_port = now.controller_port;
      g_enter_ticks = 0;
  g_enter_seen_select = false;
      request_major(kTrainingMajor);
      host::log("native practice: Training again with the CPU as character %d", character);
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
void enter_training_after_session(MatchMode mode, int test_character = -1) {
  uint8_t character = 0, costume = 0;
  const PracticeConfig css = capture_practice();
  bool picked = slippi::online::local_css_selection(&character, &costume);
  if (!picked && test_character >= 0) { character = (uint8_t)test_character; picked = true; }
  if (!picked && css.valid && css.players[css.local_slot].character < 26) {
    character = (uint8_t)css.players[css.local_slot].character;
    costume = css.players[css.local_slot].costume;
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
  }
  g_offer_mode = mode == MatchMode::Unranked || mode == MatchMode::Direct || mode == MatchMode::Teams ? mode : MatchMode::None;
  g_offer_code.clear();
  if (g_offer_mode == MatchMode::Direct || g_offer_mode == MatchMode::Teams) {
    std::string error;
    if (!normalize_direct_code(slippi::online::direct_code_for_practice(), &g_offer_code, &error)) g_offer_mode = MatchMode::None;
  }
  g_offer_reason.clear();
  g_offer_shown = false;
  g_enter_training = picked;
  g_enter_port = g_practice.controller_port;
  g_enter_ticks = 0;
  g_enter_seen_select = false;
  g_origin_major = kTrainingMajor;
  g_restore_training = false;
  host::log("native practice: online session over; into Training%s, then offer a %s search", picked ? "" : " (no character known)",
            g_offer_mode == MatchMode::None ? "no" : match_mode_name(g_offer_mode));
  run_decision(g_lifecycle.return_after_session());
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

void submit_train_against(int character) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CommandKind::TrainAgainst, std::to_string(character)});
}

void submit_cancel() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CommandKind::Cancel, {}});
}

void submit_acknowledge_failure() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_commands.push_back({CommandKind::AcknowledgeFailure, {}});
}

void tick() {
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
  // Test only (MELEE_PRACTICE_AUTOSTART=unranked or direct:CODE): start one search after two
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
        else if (spec.rfind("teams:", 0) == 0) submit_start_teams(spec.substr(6));
        else submit_start_unranked();
      }
    }
  }
  // L+R held on Slippi's online character select in Unranked (not with A or Start, so L+R+A+Start
  // still resets): go to Training to warm up, and the Unranked search starts again by itself once
  // the Training match is running. A third of a second of holding, so brushing both triggers does
  // nothing. Leaving Training for anywhere else first drops the pending search.
  {
    static uint32_t lr_ticks = 0;
    bool lr = false;
    static int lr_port = 0;
    static MatchMode lr_mode = MatchMode::None;
    if (g_lifecycle.phase() == Phase::Idle && g_auto_search == MatchMode::None && is_online_scene() &&
        current_minor() == 0 &&
        (online_mode() == (uint8_t)MatchMode::Unranked || online_mode() == (uint8_t)MatchMode::Direct ||
         online_mode() == (uint8_t)MatchMode::Teams) &&
        !slippi::playback::enabled() && !slippi::online::is_online_match()) {
      lr_mode = (MatchMode)online_mode();
      host::PadState pads[4];
      host::input_last_pads(pads);
      for (int port = 0; port < 4; ++port)
        if ((pads[port].button & 0x0060u) == 0x0060u && !(pads[port].button & (0x0100u | 0x1000u))) { lr = true; lr_port = port; }
    }
    lr_ticks = lr ? lr_ticks + 1 : 0;
    // Direct: practice while waiting for the code's player, with the code being searched for (or
    // the last one used). No code at all: nothing to search, so L+R does nothing there.
    std::string lr_code;
    if (lr_ticks == 20 && (lr_mode == MatchMode::Direct || lr_mode == MatchMode::Teams)) {
      std::string error;
      if (!normalize_direct_code(slippi::online::direct_code_for_practice(), &lr_code, &error)) {
        host::log("native practice: L+R on the Direct character select with no code to search for");
        lr_ticks = 21;
      }
    }
    if (lr_ticks == 20) {
      uint8_t character = 0, costume = 0;
      bool picked = slippi::online::local_css_selection(&character, &costume);
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
        g_practice.controller_port = lr_port;
        g_practice.players[0].active = true;
        g_practice.players[0].character = character;
        g_practice.players[0].costume = costume;
        write_event_backup();
      }
      g_auto_search = lr_mode;
      g_auto_code = lr_code;
      g_auto_ticks = 0;
      request_major(kTrainingMajor);
      host::log("native practice: L+R on the online character select; Training, then %s search%s",
                match_mode_name(lr_mode), picked ? "" : " (no character picked yet)");
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
    static const int test_at = [] { const char* v = std::getenv("MELEE_TEST_AFTER_SESSION"); return v ? std::atoi(v) : -1; }();
    static int ticks = 0;
    if (test_at >= 0 && ++ticks == test_at && g_lifecycle.phase() == Phase::Idle) enter_training_after_session(MatchMode::Unranked, [] { const char* c = std::getenv("MELEE_TEST_P1_CHAR"); return c ? std::atoi(c) : 2; }());   // Fox, or MELEE_TEST_P1_CHAR
    // MELEE_TEST_TRAIN_AGAINST=<tick>,<character> (tests only): a ghost's character picked at that tick.
    static int against_at = -1, against = -1;
    static const bool parsed = [] { if (const char* v = std::getenv("MELEE_TEST_TRAIN_AGAINST")) std::sscanf(v, "%d,%d", &against_at, &against); return true; }();
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

  // "After an online session ends, go back to Training mode" (Game tab, Online): when the session
  // ends with at least one match played (the other player left, Z held to leave, the connection
  // dropped) and the player is still on the online screens, Training's character select. Only once
  // the session is over, so nothing changes during a match or between a pair's games.
  {
    static bool session_seen = false, played = false;
    static int session_mode_seen = -1;
    static std::string session_code;
    const bool session = slippi::online::session_mode() >= 0;
    if (session) {
      session_seen = true;
      session_mode_seen = slippi::online::session_mode();
      if (slippi::online::is_online_match()) played = true;
    } else if (session_seen) {
      session_seen = false;
      if (played && gx::RenderOptions::live_online_back_to_training() && g_lifecycle.phase() == Phase::Idle &&
          is_online_scene() && !slippi::playback::enabled()) {
        enter_training_after_session((MatchMode)session_mode_seen);
      } else if (played && gx::RenderOptions::live_online_return_training() && g_lifecycle.phase() == Phase::Idle &&
          is_online_scene()) {
        g_origin_major = kTrainingMajor;
        g_restore_training = false;
        host::log("native practice: online session over; going to Training (setting on)");
        run_decision(g_lifecycle.return_after_session());
      }
      played = false;
    }
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
    if (g_search_ticks > kSearchTimeoutTicks) {
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
  if (g_enter_training) {
    const uint8_t major = current_major(), minor = current_minor();
    ++g_enter_ticks;
    if (major == kTrainingMajor && minor < 2) g_enter_seen_select = true;
    if (major == kTrainingMajor && minor >= 2 && g_enter_seen_select) {
      g_enter_training = false;
    } else if (g_enter_ticks > 60 * 15 || slippi::online::is_online_match() ||
               (major != kTrainingMajor && major != kOnlineMajor && g_enter_ticks > 180)) {
      g_enter_training = false;
      host::log("native practice: stopped pressing through Training's menus (scene %02X:%02X)", major, minor);
    } else if (major == kTrainingMajor && minor == 1 && g_bridge) {
      neutral_pads(pads);
      // Stage select: the same stage as before (Final Destination when there was none), picked at once.
      const int legal[] = {2, 3, 8, 28, 31, 32};
      int stage = 32;
      for (int s : legal) if ((int)g_practice.stage == s) stage = s;
      int32_t a[1] = {stage};
      g_bridge(OP_TRAINING_STAGE, a, 1);
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
