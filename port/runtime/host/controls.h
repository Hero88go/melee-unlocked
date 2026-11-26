// Tap jump off and tilt stick: the player's settings, the agreement with online opponents, and the
// per-match word the game takes as each match starts (MuHostApi.control_flags, mu_controls.h).
//
// Offline every person playing on this PC plays with these settings. Online they run only where
// controls_policy.h allows them (YouGame Unranked, Friends, the Friends lobby and Doubles; tilt stick
// in YouGame Ranked) and only when every player agrees for the match (controls_wire.h). Without that
// agreement everyone plays the default controls. The same exchange carries the opponent's OK for
// automatic L-cancel in YouGame Unranked (lcancel.cpp asks lcancel_agreed()).
//
// Answers are remembered per opponent (a file beside the settings), so the same question is never
// asked twice; this player's own options count as a yes for the same options, with a notice and a
// "not this match" button. Game thread for the session calls; the view and the answers are safe from
// the settings window's thread.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace controls {

// ---- settings (saved in the settings file: tapjumpoff, tiltstick, tiltdash, tiltitem)
struct Settings {
  bool tapjump_off = false;
  bool tilt_stick = false;
  bool tilt_dash_attack = true;        // tilt stick in a dash: dash attack (as Ultimate), else forward smash
  bool tilt_item_tilt_throw = false;   // tilt stick with an item: light throw, else Melee's smash throw
};
Settings settings();
void set_settings(const Settings& s);
// This PC's MU_CONTROL_* game bits from the settings, normalized.
unsigned own_bits();

// Only the Source Port runs these decisions (they live in the game's own functions). The Static
// Recomp never sets this: it neither applies them offline nor offers them online.
void set_engine_supported(bool on);
bool engine_supported();
// This game takes part in online agreements at all (sent in the build message).
bool advertise();
// Hidden or headless test runs only (main.cpp): lets MELEE_TEST_CONTROLS_YOUGAME make a local-peer pair
// count as a YouGame match. Never set for a player's game, so no Slippi match can ever run these.
void set_test_run(bool on);
bool test_run();

// ---- remembered answers (one line per opponent: key, allowed bits, declined bits)
void set_memory_path(const std::string& path);
size_t remembered_count();
void forget_all();

// ---- the word the game takes as a match starts: offline this PC's bits on every port (the game keeps
// them to human ports), online the agreed word frozen for this match, 0 otherwise.
uint32_t match_word();

// Automatic L-cancel was agreed for this player in the running YouGame Unranked match.
bool lcancel_agreed();

// ---- the online exchange (slippi_online.cpp drives it; game thread)
struct Player {
  int port = -1;
  std::string key;    // what answers are remembered under (account id, else connect code, else name)
  std::string name;   // shown on the character select
};
// A connection where an exchange runs (controls_policy::exchange, every peer announced it).
// ready: this player is already locked in (the exchange can start after the lock-in).
void session_begin(int local_port, const std::vector<Player>& others, int mode, bool yougame, bool ready);
void session_end();
bool session_active();
// Messages to send now (and forgets them), and one from the opponent (its sender port checked by the
// netplay client against the connection that sent it).
std::vector<std::vector<uint8_t>> take_outgoing();
void receive(const uint8_t* data, size_t size);
// Every character select frame: a changed setting goes out as a new offer while not locked in.
void tick();
// Just before each selection message: ready is the lock-in. Locking in with a question unanswered
// answers it no (for this match only). Returns the messages to send ahead of the selection.
std::vector<std::vector<uint8_t>> before_selection(bool ready);
// The setup hash inputs with the decision folded in (controls_wire::fold_inputs).
uint64_t fold_inputs(uint64_t inputs);
// Every message that decides the match has arrived (controls_wire::settled). The match waits for it.
bool settled();
// The setup hashes agreed: this match plays this decision. owner[i]: whose offer in-game port i plays
// with (the netplay port of its person's machine; -1 for a CPU or a Pro AI seat).
void freeze(const int owner[4]);
// The online game's first frame: answers now count from the next match.
void match_started();
void match_ended();

// ---- the character select and the settings panel
enum class Answer { None, Ask, Yes, YesOwn, No, NoUnused, NoLocked, NoThisMatch };
// The short pause before a match (owner 11-25), only when both sides use the options: a question asked of
// a locked-in player waits this long for an answer (then it is no, this match only), and an own-options
// yes waits this long for "Press Y for defaults" before it is sent.
constexpr int kAskSeconds = 8, kOwnSeconds = 3;
struct PeerView {
  int port = -1;
  std::string name;
  unsigned bits = 0;          // what they offer
  Answer mine = Answer::None; // this player's answer to it
  bool they_agree = false;    // they said yes to this player's offer (or there was nothing to ask)
};
struct View {
  bool active = false;
  int mode = -1;
  unsigned mine = 0;          // what this player offers
  bool ready = false;
  std::vector<PeerView> peers;
  uint32_t word = 0;          // the decision as it stands now
};
View view();
// The prompt's buttons: remembered for that opponent.
void answer(int port, bool allow);
// The own-options notice's button, and the settings panel's: no for this match only.
void decline_this_match(int port);
// The online character select's checklist line (sourceport/game/shim/mu_slippi_css.c, command 0xC9).
enum class Css : uint8_t { None, Asked, AllowedOwn, Waiting, Agreed, Defaults };
struct CssLine {
  Css state = Css::None;
  int port = -1;         // the opponent it is about
  int seconds = 0;       // left to answer (Asked) or to say "defaults" (AllowedOwn); 0 none
  unsigned theirs = 0;   // their main options (Asked, AllowedOwn), everyone's (Agreed)
  unsigned mine = 0;     // this player's main options
};
CssLine css_line();
// Tests only: the clock the pause is measured on (milliseconds); null restores the steady clock.
void set_test_clock(uint64_t (*now_ms)());
// "Tap jump off, Tilt stick (dash attack, light throws), Auto L-cancel"; "default controls" for 0.
std::string describe(unsigned bits);

}  // namespace controls
