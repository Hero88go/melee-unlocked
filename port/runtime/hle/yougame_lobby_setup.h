// The 4-player Friends lobby's match, built from the setup the room froze at Start: every machine reads
// the same setup frame and writes the same game-info block (StartMeleeData: rules 0x00-0x5F, then a
// 0x24-byte PlayerInitData per port at 0x60), then the setup hash compares them before frame 1.
//
//  - ports: the setup's own, 0-based, humans first, CPUs after (Rooms freezeMuSetup). This machine
//    plays the port its own id holds; the netplay session numbers the humans 0..humans-1 the same way.
//  - seed: the setup's, as the RNG offset; stage: the setup's (Rooms already drew "random").
//  - teams, friendly fire, stocks or time, minutes, pause: the setup's rules. Items stay off.
//  - CPUs: slot type 1, their level, the default CPU kind (4, the shipped block's).
//  - every port that plays, human or CPU, gets the same writes on every machine, whatever the block
//    held from an earlier match (it is kept between matches, and the setup hash covers it).
// Plain checks, no host state: the same setup gives the same block everywhere.
//
// Doubles (G2): the match room's setup (Rooms placeTeams, then freezeMuSetup) always has 4 seats, team i
// on seats 2i and 2i+1 with pick.team i (matchmaking assigns the real team, whatever the home picked),
// humans first then CPUs by port, CPUs "cpu:<seat>" named "CPU n", the fixed rules, input delay 2, and
// the stage kDoublesStages[seed % 5] (or Rooms' old pair until it moves: friendly fire off with
// kRandomStages[seed % 6]). plan(..., Profile::Doubles) holds a setup to exactly that.
#pragma once
#include "yougame_lobby_wire.h"
#if MU_SQUAD
#include "../abi/mu_squad_wire.h"
#endif
#include "../abi/mu_setup_hash.h"
#include "../abi/mu_lobby_wire.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace yougame::lobby {

// What the search shows and says (phase 2 maps the room's refusal codes to hints of their own).
inline constexpr const char* kWaiting = "Friends lobby";
inline constexpr const char* kUnsupported = "This room can't hold the Friends lobby. Everyone needs the latest Melee Unlocked.";
inline constexpr const char* kSetupUnusable = "The Friends lobby started a match this game can't play. Press B to leave.";
inline constexpr const char* kCpuOnly = "Playing online with only CPUs isn't ready yet. Invite a friend, then Start.";
inline constexpr const char* kDoublesWaiting = "Doubles";
inline constexpr const char* kDoublesUnusable = "Matchmaking found a Doubles match this game can't play. Press START to search again.";
// hello's card digest. Native Melee Unlocked has no controller card to digest yet: every native game
// sends this one, so it never refuses a match (phase 2 replaces it with the real card's digest).
inline constexpr const char* kCardDigestNative = "mu-native-card-0";
// The one search mode a lobby search runs in: Direct (Matchmaking::DIRECT, slippi_net.h). Unranked
// results, the Direct build and pick checks (on the frozen setup's picks), no ranked or Teams rules.
// find_match refuses a lobby search in any other mode or with a connect code or invite room.
inline constexpr int kSearchMode = 2;
inline constexpr const char* kSearchRefused = "The Friends lobby can't start from this menu. Press B and open it again.";

// hello's settings digest: this build's room key, hashed. Players in one room share the key (the room
// admits only it), so every seat's digest matches; a different digest means a different build.
inline std::string settings_digest(const std::string& compatibility) {
  static const char* kHex = "0123456789abcdef";
  const uint64_t h = mu_fnv64_bytes(MU_FNV64_OFFSET, compatibility.data(), compatibility.size());
  std::string out = "mu-s1-";
  for (int i = 15; i >= 0; --i) out += kHex[(h >> (4 * i)) & 0xF];
  return out;
}

inline constexpr uint8_t kCpuKindDefault = 4;   // PlayerInitData.cpu_kind in the shipped block
inline constexpr uint8_t kCpuLevelIdle = 1;     // a human port's cpu_level in the shipped block

struct PortSpec {
  Kind kind = Kind::Open;
  int seat = -1;
  std::string id, name, connection_id;
  int local_index = 0;
  bool connection_explicit = false;
  Pick pick;
  std::vector<Pick> lineup;
  int level = 0;
  int pro = -1;                          // a Pro AI seat (local_index 1): which pro, -1 a person
  std::string settings_digest, card_digest;
};
struct Plan {
  bool ok = false;
  std::string why;                      // for the log when !ok
  std::string match_id;
  uint32_t seed = 0;
  int stage = 0;
  Rules rules;
  int ports = 0, humans = 0, machines = 0;            // ports in play (humans + CPUs)
  std::vector<int> local_ports;
  int second_local_port = -1;
  int local_port = -1;                  // 0-based
  int input_delay = kInputDelayDefault; // the setup's, else the default
  std::array<PortSpec, kSeatsMax> port{};
  json frozen;                          // echoed in game-report and finish
  std::vector<std::string> ids;         // result ids by port (CPUs cpu:<seat>)
  std::string squad_budget;
  bool doubles = false;                 // a Doubles match (finish names the winning team)
  // A match made from connect codes (an Arena room, a Direct game with a Pro AI): the people pick on
  // the select as usual (their own selections); only the Pro AI seats take the setup's pick.
  bool code_match = false;
  // The match has 20XX CPUs (an Arena room's 20XX bots, owner 11-24): every machine plays its CPU ports with
  // the 20XX AI on, so they stay in step (the game asks once a match, CMD_PROAI_SEAT).
  bool cpu_20xx = false;
};

inline Plan refuse(const char* why) { Plan p; p.why = why; return p; }

// The setup this machine plays, or why it can't. `me` is this player's room id. A digest the setup
// froze for this machine's own seat must be the one this machine has (Rooms compared every seat's
// with each other; this compares them with what this game actually runs).
inline Plan plan(const Setup& s, const std::string& me, const std::string& settings_digest = {},
                 const std::string& card_digest = {}, Profile profile = Profile::Friends4, const std::string& squad_budget = {}) {
  if (s.rules.squad) {
#if !MU_SQUAD
    return refuse("squad_capability");
#else
    if (profile != Profile::Friends4 || s.seats.size() != 2) return refuse("squad_two_players");
    if (s.rules.teams || s.rules.time || s.rules.stocks != 4) return refuse("squad_rules");
    if (s.squad_version != 3 || s.squad_budget.empty() || squad_budget.empty()) return refuse("squad_capability");
    if (s.squad_budget != squad_budget) return refuse("squad_budget");
    int prior = -1;
    for (const auto& x : s.seats) {
      if (x.kind != Kind::Human || x.local_index != 0) return refuse("squad_two_players");
      if (x.seat <= prior) return refuse("squad seat order");
      prior = x.seat;
      // Frozen capability is setup-wide. If a newer room also echoes seat capabilities, check them.
      if (x.squad_version && x.squad_version != 3) return refuse("squad_capability");
      if (!x.squad_budget.empty() && x.squad_budget != s.squad_budget) return refuse("squad_budget");
      if (x.lineup.size() != 4 || !read_lineup(lineup_json(x.lineup), nullptr)) return refuse("pick_required");
      if (x.pick.ch != x.lineup.front().ch || x.pick.color != x.lineup.front().color) return refuse("squad first fighter");
    }
#endif
  }
  // A Friends room seats up to six (Triples); Doubles' match room always four.
  const int seat_max = profile == Profile::Friends4 ? kSeatsMax : kSeats;
  if (s.seats.size() > size_t(seat_max)) return refuse("too many ports");
  if (s.seats.size() < 2) return refuse("fewer than 2 players");
  if (profile == Profile::Doubles) {
    if (s.seats.size() != (size_t)kSeats) return refuse("doubles needs 4 seats");
    const bool new_rules = s.rules == doubles_rules();
    if (!new_rules && s.rules != doubles_rules_old()) return refuse("doubles rules");
    if (s.input_delay && s.input_delay != kInputDelayDefault) return refuse("doubles input delay");
    if (s.stage != (new_rules ? kDoublesStages[s.seed % kDoublesStages.size()] : kRandomStages[s.seed % kRandomStages.size()]))
      return refuse("doubles stage");
    for (const auto& x : s.seats)
      if (x.seat < 0 || x.seat >= kSeats || x.pick.team != x.seat / 2) return refuse("doubles team");
  }
  if (!stage_ok(s.stage)) return refuse("stage");
  if (!rules_ok(s.rules)) return refuse("rules");
  Plan p;
  std::string own_connection = me;
  for (const auto& x : s.seats)
    if (x.kind == Kind::Human && x.local_index == 0 && x.id == me && !x.connection_id.empty()) own_connection = x.connection_id;
  unsigned seen = 0;
  bool cpus = false;
  for (size_t i = 0; i < s.seats.size(); ++i) {
    const SetupSeat& x = s.seats[i];
    if (profile == Profile::Doubles && i && x.kind == s.seats[i-1].kind && x.seat < s.seats[i-1].seat)
      return refuse("doubles seat order");
    if (x.port != (int)i) return refuse("ports not in order");
    if (x.seat < 0 || x.seat >= seat_max || (seen & (1u << x.seat))) return refuse("seat repeated");
    seen |= 1u << x.seat;
    if (x.kind == Kind::Cpu) {
      if (x.local_index != 0 || !x.connection_id.empty()) return refuse("cpu connection");
      cpus = true;
      if (x.id != "cpu:" + std::to_string(x.seat)) return refuse("cpu id");
      if (x.level < kLevelMin || x.level > kLevelMax) return refuse("cpu level");
    } else if (x.kind == Kind::Human) {
      if (cpus) return refuse("a human after a cpu");
      if (x.id.empty() || x.id.rfind("cpu:", 0) == 0) return refuse("human id");
      if (x.local_index < 0 || x.local_index > 1) return refuse("local index");
      if (x.local_index == 1 && x.pro >= 0) {
        // A Pro AI seat: the model plays it on its owner's machine (the port before it), any team, any mode.
        const auto& owner = s.seats[i - (i ? 1 : 0)];
        if (i == 0 || x.connection_id.empty() || x.pro > 0x7F || owner.kind != Kind::Human || owner.local_index != 0 ||
            owner.connection_id != x.connection_id || x.id != "proai:" + x.connection_id)
          return refuse("pro ai owner");
      } else if (x.local_index == 1) {
        if (profile != Profile::Doubles || i == 0 || x.connection_id.empty() || x.settings_digest.empty() || x.card_digest.empty()) return refuse("local outside doubles or no owner");
        const auto& owner = s.seats[i - 1];
        if (owner.kind != Kind::Human || owner.local_index != 0 || owner.connection_id != x.connection_id ||
            x.seat != owner.seat + 1 || owner.seat % 2 || owner.pick.team != x.pick.team ||
            x.id != "local:" + x.connection_id + ":1" ||
            x.settings_digest != owner.settings_digest || x.card_digest != owner.card_digest)
          return refuse("local owner or team");
      } else {
        if (x.id.rfind("local:", 0) == 0) return refuse("local metadata missing");
        const auto connection = x.connection_id.empty() ? x.id : x.connection_id;
        for (size_t j = 0; j < i; ++j)
          if (s.seats[j].kind == Kind::Human && (s.seats[j].connection_id.empty() ? s.seats[j].id : s.seats[j].connection_id) == connection)
            return refuse("connection repeated");
        ++p.machines;
      }
      ++p.humans;
    } else return refuse("seat kind");
    if (x.pick.ch < 0 || x.pick.ch > kCharMax || x.pick.color < 0 || x.pick.color > kColorMax ||
        x.pick.team < 0 || x.pick.team > kTeamMax) return refuse("pick");
    if (!s.rules.teams && x.pick.team != 0) return refuse("team without teams");
    for (size_t j = 0; j < i; ++j) if (s.seats[j].id == x.id) return refuse("id repeated");
    if (x.kind == Kind::Human && (x.id == me || x.connection_id == own_connection)) {
      p.local_ports.push_back((int)i);
      if (x.local_index == 0) p.local_port = (int)i;
      else p.second_local_port = (int)i;
    }
    PortSpec& out = p.port[i];
    out.connection_id = x.connection_id.empty() ? x.id : x.connection_id; out.local_index = x.local_index;
    out.connection_explicit = !x.connection_id.empty();
    out.kind = x.kind; out.seat = x.seat; out.id = x.id; out.name = x.name; out.pick = x.pick; out.level = x.level; out.pro = x.pro;
    out.lineup = x.lineup;
    out.settings_digest = x.settings_digest; out.card_digest = x.card_digest;
    p.ids.push_back(x.id);
  }
  if (profile == Profile::Doubles && p.machines < 2) return refuse("doubles needs 2 machines");
  if (p.humans < 1) return refuse("no human");
  if (p.local_port < 0) return refuse("this player is not in the setup");
  if (s.rules.teams) {
    bool two = false;
    for (const auto& x : s.seats) if (x.pick.team != s.seats[0].pick.team) two = true;
    if (!two) return refuse("teams with one team");
  }
  const PortSpec& mine = p.port[p.local_port];
  if (!mine.settings_digest.empty() && !settings_digest.empty() && mine.settings_digest != settings_digest)
    return refuse("this game's settings are not the setup's");
  if (!mine.card_digest.empty() && !card_digest.empty() && mine.card_digest != card_digest)
    return refuse("this game's card is not the setup's");
  p.squad_budget = s.squad_budget;
  p.ok = true;
  p.match_id = s.match_id;
  p.seed = s.seed;
  p.stage = s.stage;
  p.rules = s.rules;
  p.ports = (int)s.seats.size();
  p.input_delay = s.input_delay ? s.input_delay : kInputDelayDefault;
  p.frozen = s.frozen;
  p.doubles = profile == Profile::Doubles;
  return p;
}

#if MU_SQUAD
// The room already agreed the lineups; there is no second peer handshake.
inline bool squad_setup(const Plan& p, MuSquadSetup* out) {
  if (!p.ok || !p.rules.squad || p.ports != 2 || !out) return false;
  MuSquadSetup s{}; s.stocks = 4;
  for (int port = 0; port < 2; ++port) {
    if (p.port[port].lineup.size() != 4) return false;
    s.sides[port].count = 4;
    for (int i = 0; i < 4; ++i) {
      const auto& x = p.port[port].lineup[i];
      s.sides[port].members[i] = {(unsigned char)x.ch, mu_squad_color(x.ch, x.color)};
    }
  }
  if (!mu_squad_setup_valid(&s)) return false;
  *out = s; return true;
}
#endif

// A port's colour in the match: the setup's, unless the fighter has fewer costumes in this build (the
// counts Melee's X/Y cycle uses, gm_GetNumCostumesForCKind, sent by the select with COSTUMES; Melee's
// own when none came), then its last (the select's door clamps the same way, mu_friends4_css.inc
// mu_f4_color). Every machine in a room runs the same build (the room's key), so every machine writes the
// same colour before the setup hash.
inline uint8_t costume_for(int ch, int color, const uint8_t* costumes) {
  const unsigned n = ch >= 0 && ch <= kCharMax ? (costumes ? costumes[ch] : mu_lobby_costumes_melee(ch)) : 0;
  if (!n || color < 0) return 0;
  return (unsigned)color < n ? (uint8_t)color : (uint8_t)(n - 1);
}

// Doubles team colours (11-11 report: "would be nice to be able to change team color easily in doubles").
// The setup's pick.team is the room's pair (0 or 1, placeTeams); Melee's team byte is a colour (red 0,
// blue 1, green 2). Each machine's colour is the one its player chose on the select's team box: its
// 4-peer selection carries it as team_id (mu_f4_lock_in), which the lobby's selections otherwise only
// use to say "in". Every machine holds the same selections once everyone is in, so every machine maps
// the same colours. `sent`: by port, the team_id of the machine whose own (local_index 0) port that
// is, -1 for none.
//  - A team wears the colour of its lowest seat whose machine sent one. A local Player 2 or a CPU sends
//    none: it wears its team's. A team with none wears its own index (red, blue), as before colours.
//  - A clash (both teams chose one colour): team 0 keeps it, team 1 wears the first of red, blue, green
//    that team 0 isn't wearing.
inline constexpr int kTeamColors = 3;
inline std::array<uint8_t, 2> doubles_colors(const Plan& p, const std::array<int, kSeats>& sent) {
  std::array<uint8_t, 2> color{{0, 1}};
  if (!p.ok || !p.doubles || !p.rules.teams) return color;
  for (int team = 0; team < 2; ++team) {
    int best = kSeats;
    for (int port = 0; port < p.ports && port < kSeats; ++port) {
      const PortSpec& x = p.port[port];
      if (x.kind != Kind::Human || x.local_index != 0 || x.pick.team != team || x.seat >= best) continue;
      if (sent[port] < 0 || sent[port] >= kTeamColors) continue;
      best = x.seat;
      color[team] = (uint8_t)sent[port];
    }
  }
  if (color[1] == color[0])
    for (uint8_t c = 0; c < kTeamColors; ++c)
      if (c != color[0]) { color[1] = c; break; }
  return color;
}

// Writes the plan into the game-info block: every port's record, then the rules bytes. Items, and the
// rest the caller writes for every match, are left to the caller. `timer_override` (seconds, 0 none):
// the tests' MELEE_ONLINE_TEST_TIMER. `costumes`: each fighter's costume count (MU_LOBBY_CHARS), or
// null for Melee's own. `colors`: a Doubles match's team colours (doubles_colors), or null for the
// room's team numbers as they are.
inline bool apply(const Plan& p, uint8_t* block, size_t size, uint32_t timer_override = 0,
                  const uint8_t* costumes = nullptr, const std::array<uint8_t, 2>* colors = nullptr) {
  if (!p.ok || !block || size < 0x60 + 4 * 0x24 || p.ports < 1 || p.ports > kSeatsMax) return false;
  // A six-seat room's match (Triples) needs the block's six records.
  const bool six = size >= 0x60 + (size_t)kSeatsMax * 0x24;
  if (p.ports > kSeats && !six) return false;
  unsigned char kinds[4] = {3, 3, 3, 3};
  for (int i = 0; i < kSeatsMax; ++i) {
    uint8_t* r = block + 0x60 + i * 0x24;
    if (i >= p.ports) continue;
    const PortSpec& x = p.port[i];
    if (i < kSeats) kinds[i] = x.kind == Kind::Cpu ? 1 : 0;
    r[0x0] = (uint8_t)x.pick.ch;
    r[0x2] = (uint8_t)(p.rules.time ? 4 : p.rules.stocks);
    r[0x3] = costume_for(x.pick.ch, x.pick.color, costumes);
    r[0x7] = 0;
    r[0x9] = (uint8_t)(!p.rules.teams ? 0
                       : p.doubles && colors && (x.pick.team == 0 || x.pick.team == 1) ? (*colors)[x.pick.team]
                                                                                    : x.pick.team);
    r[0xE] = kCpuKindDefault;
    r[0xF] = x.kind == Kind::Cpu ? (uint8_t)x.level : kCpuLevelIdle;
    r[0x10] = 0; r[0x11] = 0;   // 0%
  }
  mu_setup_ports_kinds(block, size, kinds, p.rules.teams ? 1 : 0);
  if (six) {
    // Players 5 and 6: their slot types (a person, a CPU, or nobody past the match's ports: the block is
    // kept between matches, so a Triples match before this one would otherwise still seat them), then
    // every port's costume index over all six by mu_setup_ports_kinds' rule.
    for (int i = kSeats; i < kSeatsMax; ++i)
      block[0x61 + i * 0x24] = i < p.ports ? (p.port[i].kind == Kind::Cpu ? 1 : 0) : 3;
    if (p.ports > kSeats)
      for (int i = 0; i < kSeatsMax; ++i) {
        const uint8_t* a = block + 0x60 + i * 0x24;
        uint8_t index = 0;
        if (a[1] != 0 && a[1] != 1) continue;
        for (int j = 0; j < i; ++j) {
          const uint8_t* b = block + 0x60 + j * 0x24;
          if (b[1] != 0 && b[1] != 1) continue;
          if ((a[0] == 0x13 ? 0x12 : a[0]) == (b[0] == 0x13 ? 0x12 : b[0]) &&
              (p.rules.teams ? a[9] == b[9] : a[3] == b[3])) ++index;
        }
        block[0x67 + i * 0x24] = index;
      }
  }
  // Rules: match kind and the HUD byte as the shipped block's stock match, or as Party's timed one.
  // Stock with minutes 0 has no timer: the timer-enabled bit (0x02) clear and no time, as Melee's own
  // VS rules leave a stock match with no time limit (gm_1601.c).
  const bool timer_on = p.rules.time || p.rules.minutes > 0 || timer_override;
  block[0x0] = (uint8_t)((p.rules.time ? 0x12 : 0x32) & (timer_on ? 0xFF : ~0x02));
  block[0x3] = p.rules.time ? 0xCC : 0x4C;
  block[0x1] = p.rules.friendly_fire ? (uint8_t)(block[0x1] | 0x01) : (uint8_t)(block[0x1] & ~0x01);
  block[0x2] = p.rules.pause ? (uint8_t)(block[0x2] & ~0x08) : (uint8_t)(block[0x2] | 0x08);
  block[0x8] = p.rules.teams ? 1 : 0;
  block[0xE] = (uint8_t)(p.stage >> 8);
  block[0xF] = (uint8_t)p.stage;
  const uint32_t t = timer_override ? timer_override : (uint32_t)p.rules.minutes * 60u;
  block[0x10] = (uint8_t)(t >> 24); block[0x11] = (uint8_t)(t >> 16); block[0x12] = (uint8_t)(t >> 8); block[0x13] = (uint8_t)t;
  return true;
}

// The setup hash's two halves for a lobby match (mu_setup_hash.h). Inputs: which match (its id), so a
// peer still on an earlier match's hash waits. Setup side: the frozen setup itself, canonical (keys
// sorted), so two machines holding different setups for one match refuse it rather than wait.
inline uint64_t inputs_hash(const Plan& p) {
  uint64_t h = mu_fnv64_bytes(MU_FNV64_OFFSET, "mu-lobby-inputs-1", 17);
  return mu_fnv64_bytes(h, p.match_id.data(), p.match_id.size());
}
inline uint64_t setup_digest(const Plan& p) {
  const std::string text = p.frozen.dump();   // nlohmann objects keep keys sorted: canonical
  uint64_t h = mu_fnv64_bytes(MU_FNV64_OFFSET, "mu-lobby-setup-1", 16);
  h = mu_fnv64_bytes(h, text.data(), text.size());
  return mu_fnv64_u8(h, (unsigned)p.ports);
}

// The room's participants who play this setup, each with its setup port as `slot` (the netplay session's
// player index, port + 1 in game): the humans, in port order. Empty when one of them is not in the room
// (yet, or any more), or a participant appears twice. CPUs are no participants: they run on every machine.
inline json match_roster(const json& participants, const Plan& p) {
  json out = json::array();
  if (!p.ok || !participants.is_array()) return out;
  for (int port = 0; port < p.humans; ++port) {
    if (p.port[port].local_index != 0) continue;
    const json* found = nullptr;
    for (const auto& x : participants) {
      if (!x.is_object() || x.find("id") == x.end() || !x["id"].is_string() || x["id"].get<std::string>() != p.port[port].id) continue;
      if (x.value("localIndex", 0) != 0) continue;
      if (found) return json::array();
      found = &x;
    }
    if (!found || found->find("connectionId") == found->end() || !(*found)["connectionId"].is_string()) return json::array();
    if (p.port[port].connection_explicit && (*found)["connectionId"].get<std::string>() != p.port[port].connection_id) return json::array();
    json seat = *found;
    seat["slot"] = port;
    out.push_back(std::move(seat));
  }
  return out;
}

// How a lobby game ended for each port, as the game and the netcode decided it (host.cpp
// publish_lobby_result fills it from the game's result record and the netplay session).
struct Outcome {
  bool has_placements = false;
  int placements[kSeatsMax] = {-1, -1, -1, -1, -1, -1};   // 0 first; anything else (0xFF) none
  int winner = -1;                             // the single winner's port when there are no placements
  int end_method = 0;                          // Slippi's: 1 TIME!, 2 GAME!, 7 No Contest
  int lras = -1;                               // the port a No Contest names (LRA-Start)
  unsigned gone = 0;                           // ports whose game the netcode dropped (disconnected)
  bool draw = false;
};

// The match's `finish` for Rooms (game-lobby.ts normalizeOutcome), with the frozen setup echoed exactly.
// Every machine still in the match sends the same one, so it is built only from what they all hold:
// - The game decided an order (placements, or a single winner): that order, CPUs as cpu:<seat>, with a
//   player the netcode dropped or who quit with LRA-Start ranked last (a forfeit, as Rooms ranks a
//   leaver), never left out. In Teams the leaver's whole team goes last.
// - It decided none, and a forfeit left exactly one side (one port, or one team in Teams): that side
//   wins, the rest after it.
// - It called a draw: draw.
// - Anything else (a No Contest after a disconnect in a bigger match, a desync, no result at all):
//   void, with no ranking (Rooms refuses an empty one and settles a void neutral). Never an invented order.
inline json finish_report(const Plan& p, const Outcome& o) {
  const json head = {{"t", "finish"}, {"matchId", p.match_id}, {"setup", p.frozen}};
  json out = head;
  out["void"] = true;
  if (!p.ok || (int)p.ids.size() != p.ports || p.ports < 1 || p.ports > kSeatsMax) return out;
  // A port's side: its team in Teams, itself otherwise.
  const auto side = [&](int port) { return p.rules.teams ? p.port[port].pick.team : kSeatsMax + port; };
  unsigned forfeit = 0;
  for (int port = 0; port < p.ports; ++port) {
    if (p.port[port].kind != Kind::Human) continue;   // a CPU runs on every machine: never dropped
    if ((o.gone >> port) & 1u) forfeit |= 1u << port;
    if (o.end_method == 7 && o.lras == port) forfeit |= 1u << port;
  }
  if (p.rules.teams) {   // the leaver's whole team forfeits with them
    unsigned team = forfeit;
    for (int a = 0; a < p.ports; ++a)
      if ((forfeit >> a) & 1u)
        for (int b = 0; b < p.ports; ++b) if (side(b) == side(a)) team |= 1u << b;
    forfeit = team;
  }
  const auto placed = [&](int port) {
    return o.has_placements && o.placements[port] >= 0 && o.placements[port] < kSeatsMax ? o.placements[port] : kSeatsMax;
  };
  bool placements = false;
  for (int port = 0; port < p.ports; ++port) placements = placements || placed(port) < kSeatsMax;
  const bool winner = !placements && o.winner >= 0 && o.winner < p.ports;
  const auto ranked = [&](std::vector<int> order) {
    for (int port = 0; port < p.ports; ++port) if ((forfeit >> port) & 1u) order.push_back(port);
    json ranking = json::array();
    for (int port : order) ranking.push_back(p.ids[port]);
    json r = head;
    r["ranking"] = ranking;
    // Doubles: the winning team, by its own field (Rooms teamWinner reads detail.tw first: a CPU at the
    // top of the ranking has no connection to look up).
    if (p.doubles && !order.empty() && !((forfeit >> order[0]) & 1u)) r["detail"] = {{"tw", p.port[order[0]].pick.team}};
    return r;
  };
  std::vector<int> left;
  for (int port = 0; port < p.ports; ++port) if (!((forfeit >> port) & 1u)) left.push_back(port);
  if (forfeit == 0 && o.end_method == 7) return out;   // a No Contest that names nobody (a desync): void
  if (placements || winner) {
    std::stable_sort(left.begin(), left.end(), [&](int a, int b) {
      if (placements) return placed(a) < placed(b);
      return side(a) == side(o.winner) && side(b) != side(o.winner);   // in Teams the winner's whole team first
    });
    return ranked(left);
  }
  if (forfeit && !left.empty()) {
    bool one_side = true;
    for (int port : left) one_side = one_side && side(port) == side(left[0]);
    if (one_side) return ranked(left);
  }
  if (o.draw && forfeit == 0) {
    json r = head;
    r["draw"] = true;
    return r;
  }
  return out;
}

// The room's compatibility key for the lobby: "friends-lobby-1:" (the Doubles home: "doubles-lobby-1:",
// the same length) ahead of the game's own key, inside the
// 64 characters the desktop app and the ticket route take. The game's key keeps its build identity at
// its end (engine, mod, rules, tag) and truncates the version first; when the tag would push it past 64,
// the version is replaced by a hash of itself (never cut: two versions that differ only at the end must
// not share a room), and if even that is too long, the whole key by its hash. `make(version)` builds the
// game's key for a version; empty when it can't.
inline constexpr const char* kFriendsTag = "friends-lobby-1:";
inline constexpr const char* kDoublesTag = "doubles-lobby-1:";
inline const char* tag_of(Profile p) { return p == Profile::Doubles ? kDoublesTag : kFriendsTag; }
template <class Make>
std::string compatibility(const std::string& version, Make make, Profile profile = Profile::Friends4) {
  const std::string kTag = tag_of(profile);
  std::string key = make(version);
  if (key.empty()) return {};
  if (kTag.size() + key.size() <= 64) return kTag + key;
  const uint64_t h = mu_fnv64_bytes(MU_FNV64_OFFSET, version.data(), version.size());
  const auto hex = [](uint64_t v, int digits) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < digits; ++i) out += kHex[(v >> (4 * (digits - 1 - i))) & 0xF];
    return out;
  };
  const std::string shorter = make("h" + hex(h >> 16, 12));
  if (!shorter.empty() && kTag.size() + shorter.size() <= 64) return kTag + shorter;
  // An identity too long even then: one hash of the whole key, still one room per build.
  return kTag + "mu-k" + hex(mu_fnv64_bytes(MU_FNV64_OFFSET, key.data(), key.size()), 16);
}
// The arena's room key: the game's own key with ":arena-1" after it, so no Friends lobby or Doubles home rule
// (their keys start with their tags) ever applies to the arena's 8-seat room. Shortened like compatibility().
inline constexpr const char* kArenaTag = ":arena-1";
template <class Make>
std::string arena_compatibility(const std::string& version, Make make) {
  const std::string kTag = kArenaTag;
  std::string key = make(version);
  if (key.empty()) return {};
  if (key.size() + kTag.size() <= 64) return key + kTag;
  const uint64_t h = mu_fnv64_bytes(MU_FNV64_OFFSET, version.data(), version.size());
  const auto hex = [](uint64_t v, int digits) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < digits; ++i) out += kHex[(v >> (4 * (digits - 1 - i))) & 0xF];
    return out;
  };
  const std::string shorter = make("h" + hex(h >> 16, 12));
  if (!shorter.empty() && shorter.size() + kTag.size() <= 64) return shorter + kTag;
  return "mu-k" + hex(mu_fnv64_bytes(MU_FNV64_OFFSET, key.data(), key.size()), 16) + kTag;
}
}  // namespace yougame::lobby
