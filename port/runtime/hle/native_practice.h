// Native practice matchmaking coordinator. Renderers exchange snapshots/commands with it; only
// tick() touches Slippi networking or guest scene state, and tick() runs on the simulation thread.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "native_practice_model.h"

#include <cstdint>
#include <string>

namespace host { struct PadState; }

namespace slippi::native_practice {

// Values intentionally match Slippi's OnlinePlayMode (Melee Unlocked has no Ranked).
enum class MatchMode : uint8_t { None = 0xFF, Unranked = 1, Direct = 2, Teams = 3 };

struct Snapshot {
  Phase phase = Phase::Idle;
  MatchMode mode = MatchMode::None;
  bool in_practice = false;
  uint8_t scene_major = 0, scene_minor = 0;   // the game's scene as of the last tick
  bool tab_available = false;
  bool can_start = false;
  bool cosmetic_profile_locked = false;
  int controller_port = 0;
  int matchmaking_state = 0;
  uint32_t generation = 0;
  uint32_t search_ticks = 0;
  std::string connect_code;
  std::string status;
  std::string detail;
  std::string opponent;
  // After an online session: back in Training, "search again?" (D-pad Up searches, Down dismisses).
  std::string offer;          // the prompt, empty when none
  // Characters the memory card has unlocked, a bit per character kind (all set when not known, and on
  // the Static Recomp). Fight a pro offers a pro only when that pro's character can be picked.
  uint32_t unlocked = 0xFFFFFFFFu;
  // Warm-up room (slippi_warmup.h): Training while staying connected to the opponent (Direct, Teams)
  // or while waiting for a turn in an arena room.
  struct Warmup {
    // On the online character select: the WARM UP button. offered: shown at all; enabled: works now
    // (else `why`, a short reason under it, "Locked in").
    bool offered = false, enabled = false;
    std::string why;
    bool active = false;        // in the warm-up room (on the way there, in Training, on the way back)
    int stage = 0;              // slippi::warmup::Stage
    bool arena = false;         // an arena room's wait, not a parked pair
    std::string mode;           // "Direct", "Teams", "Arena"
    int peers = 0;
    // state: warmup::PeerState; character: locked in with; picked: on the select before that (our builds)
    struct Peer { std::string name; int state = 0; int character = -1; int costume = 0; int picked = -1; } peer[3];
    int banner = 0;             // slippi::warmup::Banner
    uint32_t banner_age_ms = 0;
    int banner_peer = -1;
    int countdown_ms = -1;      // the arena's "your turn" countdown, -1 none
    float hold = 0.0f;          // 0..1 while L+R is held toward going (or coming back)
    std::string sparring;       // who the CPU is ("Zain", "Marth CPU"), empty before the match
    std::string arena_place;    // "Next up", "2 ahead of you", "Watching"
    std::string arena_versus;   // the game being played in the room: "HERO vs WISE"
    bool score_live = false;    // the room's game score (slippi::arena::LiveScore)
    uint32_t score_frame = 0;
    struct Fighter {
      bool present = false; int character = -1; int stocks = 0; float percent = 0;
      int costume = 0; std::string name;   // for the stock icon; the player's name (empty: not sent)
    } score_fighter[4];
  } warmup;
  // Play while searching: a search the player chose to keep while they play elsewhere.
  struct Kept {
    int stage = 0;              // 0 none, 1 searching while away, 2 opponent found, 3 on the way back
    std::string service;        // "YouGame", "Slippi", "YouGame and Slippi"
    std::string mode;           // "Unranked", "Ranked", "Friends", "Squad Strike", "Direct", "Teams"
    uint32_t elapsed_ms = 0;    // the search's own time
    uint32_t shown_ms = 0;      // since it was kept
    uint32_t found_ms = 0;      // since the opponent was found
    int countdown_ms = -1;      // found: until the way back (0 going, -1 waiting for a safe moment)
    float hold = 0.0f;          // L+R toward going back now
    std::string note;           // found but waiting: why
    // On the online select while a search runs: whether it can be kept, and the question.
    bool possible = false;
    bool asking = false;
    uint32_t ask_ms = 0;
    int ask_look = 1;
  } kept;
};

Snapshot snapshot();
void submit_start_unranked();
void submit_start_direct(const std::string& connect_code);
// Teams: four players who all enter the same connect code.
void submit_start_teams(const std::string& connect_code);
// In a Training match: start it again with the CPU as `character` (a ghost's own character).
// Training again with the CPU as `character` in `costume` (a pro's own colours for Fight a pro).
void submit_train_against(int character, int costume = 0);
void submit_cancel();
void submit_acknowledge_failure();
// Warm-up room: go to Training from the online character select (the button), or back to the select.
void submit_warmup();
void submit_warmup_return();
// The ARENA card on the YouGame Friends lobby's select (slippi_arena FromFriends): to the online select.
void submit_arena_from_friends();
// A launcher room starts on Direct's character select, before opening its arena.
void submit_launcher_arena();
// The main menu's cleanup came (the online select was left, or the player is passing through): true
// when a search the player chose to keep goes on (slippi_online.cpp skips that cleanup then).
bool keep_search_at_menu();
// A Squad Strike search the player chose to keep: the online menu keeps the select's lineup for the way back.
bool keep_search_squad();
// Tests only (the search banner's pictures): into a Training match as `character`, the menus pressed through.
void test_request_training(int character);

// Called once per 60 Hz retrace, before the guest advances its frame.
void tick();
// Called with the freshly polled pads, before the game sees them (both engines): presses through
// Training's character and stage select after an online session, and takes the requeue prompt's D-pad.
void apply_pads(host::PadState* pads);   // four pads
void shutdown();

// Narrow integration hook for cosmetic work: do not change the active cosmetic profile from the
// start of a search through online cleanup. No cosmetic implementation is required by this module.
bool cosmetic_profile_locked();
const char* match_mode_name(MatchMode mode);

// The Source Port game's practice operations (MuGameApi::practice, MU_PRACTICE_* in mu_host.h).
// Unset, the coordinator works on the static recomp's console memory.
using NativeBridge = int32_t (*)(int32_t op, int32_t* args, int32_t count);
void set_native_bridge(NativeBridge bridge);

// Source-only host content policy, simulation thread. A search advertises the requested
// online build without replacing files under the running offline exercise. Activate only
// at the accepted scene handoff or the return to the offline scene. Unset on Static.
using ContentBridge = void (*)(int mode, bool activate);
void set_content_bridge(ContentBridge bridge);

}  // namespace slippi::native_practice
