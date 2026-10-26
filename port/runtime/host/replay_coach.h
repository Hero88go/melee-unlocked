// The replay coach: frame data from the game (sourceport/game/shim/mu_framedata.c) while a replay
// plays, turned into a short readout per hit and a line of advice from fixed rules.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace replay_coach {

enum class Kind : uint8_t { Hit = 1, Shield = 2, Landing = 3, Tech = 4, Grab = 5 };

struct Event {
  int32_t frame = 0;          // replay frame (the hit's frame for hits)
  Kind kind = Kind::Hit;
  int actor = 0;              // attacker, lander, the player teching, grabber (port 0..3)
  int target = -1;            // defender, or -1
  int advantage = 0;          // hits: the attacker's frame advantage; techs: 1 teched, 0 missed
  int move = 0;               // the actor's motion state (Slippi action state id)
  int lcancel = 0;            // 0 none or auto cancel, 1 in time, 2 missed
  int to_land = -1;           // aerials: frames from the hit to the landing, -1 none
  int late = -1;              // aerials: frames after the hit its hitbox stayed out in the air, -1 none
  int free_at = -1;           // hits: frames from the hit until the defender could act, -1 none
  int tilt = 0;               // shield hits: the shield's tilt, -100 down .. 100 up
  int hitlag = 0;
  int actor_percent = 0, target_percent = 0;
  int actor_character = -1, target_character = -1;   // the game's internal fighter kind
  bool from_file = false;     // read from the replay file (the recording), not measured by the game
};

// The game's 22-byte event (command 0xE8). False when it is not one.
bool parse(const uint8_t* p, uint32_t n, Event& out);

// Game thread: one more event. The same event seen again (after a jump back, or first read from the
// file and then measured by the game) is kept once, the newest reading winning.
void add(const Event& e);
// The events read from the replay file at load (host/replay_scan.h): the tips before anything plays.
void add_scanned(const std::vector<Event>& events);
void clear();                       // a new replay
// A takeover attempt starts again at `frame`: what the game measured from there on is dropped (the
// recording's own events read from the file come back).
void forget_from(int32_t frame);
std::vector<Event> events();        // all so far, by frame
// The newest hit or shield hit and how many seconds ago it came in; false when there is none.
bool latest(Event& e, double& age_seconds);

// "Dair", "Forward smash", "Grab": the move's common name, "Special" for character moves.
std::string move_name(int move);
// "Falco": the character's name for the internal fighter kind.
std::string character_name(int kind);

// "Dair on shield", "-3", "L-cancelled.  Hit 4 frames before landing."
struct Readout {
  std::string head, value, detail;
  int sign = 0;                     // -1 minus, 0 even, 1 plus (colors the value)
};
Readout readout(const Event& e);
// One line of advice for the event, empty when there is nothing to say.
std::string advice(const Event& e);

// A moment worth practicing for one player: an unsafe hit on shield (and whether it was punished),
// a dropped combo, a missed L-cancel, a missed tech. Built from the events by fixed rules, in frame order.
struct Moment {
  int32_t frame = 0;                // where it happened (the hit's frame)
  std::string what;                 // "Dair on shield", "Missed tech"
  std::string value;                // "-3", or empty
  int sign = 0;
  std::string advice;
  bool punished = false;            // the other player hit or grabbed them right after
  enum Type { Unsafe, LCancel, Combo, Tech } type = Unsafe;   // the timeline marker's color
};
std::vector<Moment> moments(const std::vector<Event>& events, int player);
// "1:23": a replay frame as match time (the match starts at frame 0).
std::string clock(int32_t frame);

}  // namespace replay_coach
