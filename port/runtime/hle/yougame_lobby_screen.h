// What the Friends select draws (4 doors; 2 for the Doubles home, mode MU_LOBBY_M_DOUBLES), from the lobby's view (yougame_lobby.h Control::View): each
// seat's door, the rules, and one short hint line, as the private MU_LOBBY_COMMAND view
// (abi/mu_lobby_wire.h). Pure, so the hint for every Start block and Rooms refusal is unit tested.
#pragma once
#include "yougame_lobby.h"
#include "../abi/mu_lobby_wire.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace yougame::lobby {

// Why Start isn't offered (the host) or what the screen waits for (everyone), as one hint.
inline unsigned block_hint(const Control::View& v, bool armed) {
  if (!armed) return MU_LOBBY_HINT_NONE;
  if (v.unsupported) return MU_LOBBY_HINT_UNSUPPORTED;
  if (!v.known || v.my_seat < 0) return MU_LOBBY_HINT_OPENING;
  if (v.started) return MU_LOBBY_HINT_STARTING;
  if (v.profile == Profile::Doubles) {
    // Either player starts the search; an open seat (seat 1 for the host, seat 0 for a guest left alone when
    // the host left) means Rooms finds the partner too.
    switch (v.start) {
      case StartBlock::None:
        return v.state.seats[0].kind == Kind::Open || v.state.seats[1].kind == Kind::Open
                   ? MU_LOBBY_HINT_START_PARTNER : MU_LOBBY_HINT_START_SEARCH;
      case StartBlock::Searching: return MU_LOBBY_HINT_SEARCHING;
      case StartBlock::MatchActive: return MU_LOBBY_HINT_IN_MATCH;
      case StartBlock::PickRequired: return MU_LOBBY_HINT_PICK;
      case StartBlock::NotReady: return MU_LOBBY_HINT_NOT_READY;
      default: return MU_LOBBY_HINT_OPENING;
    }
  }
  if (!v.host) {
    if (v.state.rules.squad && !v.squad_available) return MU_LOBBY_HINT_SQUAD_UNAVAILABLE;
    const Seat& me = v.state.seats[v.my_seat];
    return me.picked ? MU_LOBBY_HINT_WAIT_HOST : MU_LOBBY_HINT_PICK;
  }
  switch (v.start) {
    case StartBlock::SquadTwoPlayers: return MU_LOBBY_HINT_SQUAD_TWO;
    case StartBlock::SquadCapability: return MU_LOBBY_HINT_SQUAD_CAPABILITY;
    case StartBlock::SquadBudget: return MU_LOBBY_HINT_SQUAD_BUDGET;
    case StartBlock::None: return MU_LOBBY_HINT_PRESS_START;
    case StartBlock::NotSeated: return MU_LOBBY_HINT_OPENING;
    case StartBlock::NotHost: return MU_LOBBY_HINT_WAIT_HOST;
    case StartBlock::MatchActive: return MU_LOBBY_HINT_IN_MATCH;
    case StartBlock::PickRequired: return MU_LOBBY_HINT_PICK;
    case StartBlock::PlayersRequired: return MU_LOBBY_HINT_PLAYERS;
    case StartBlock::NotReady: return MU_LOBBY_HINT_NOT_READY;
    case StartBlock::TeamsRequired: return MU_LOBBY_HINT_TEAMS;
    case StartBlock::HumansRequired: return MU_LOBBY_HINT_HUMANS;
    case StartBlock::Searching: return MU_LOBBY_HINT_SEARCHING;
  }
  return MU_LOBBY_HINT_NONE;
}

// Rooms' refusal of this player's frame, as a hint. NONE for the ones the session handles by itself:
// a hello or start the room doesn't know yet (`shape`), and a shared change it sends again on the new
// rev (`revision`, except Start, which the host presses again).
inline unsigned refusal_hint(const std::string& error, const std::string& action) {
  if (error.empty() || error == "shape") return MU_LOBBY_HINT_NONE;
  if (error == "revision") return action == "start" ? MU_LOBBY_HINT_CHANGED : MU_LOBBY_HINT_NONE;
  if (error == "squad_two_players") return MU_LOBBY_HINT_SQUAD_TWO;
  if (error == "squad_capability") return MU_LOBBY_HINT_SQUAD_CAPABILITY;
  if (error == "squad_budget") return MU_LOBBY_HINT_SQUAD_BUDGET;
  if (error == "squad_rules") return MU_LOBBY_HINT_SQUAD_RULES;
  if (error == "squad_unavailable") return MU_LOBBY_HINT_SQUAD_UNAVAILABLE;
  if (error == "pick_required") return MU_LOBBY_HINT_PICK;
  if (error == "players_required") return MU_LOBBY_HINT_PLAYERS;
  if (error == "humans_required") return MU_LOBBY_HINT_HUMANS;
  if (error == "teams_required") return MU_LOBBY_HINT_TEAMS;
  if (error == "digest_missing") return MU_LOBBY_HINT_NOT_READY;
  if (error == "digest_mismatch") return MU_LOBBY_HINT_MISMATCH;
  if (error == "player_away") return MU_LOBBY_HINT_AWAY;
  if (error == "host_only") return MU_LOBBY_HINT_HOST_ONLY;
  if (error == "seat_occupied" || error == "seat_owner") return MU_LOBBY_HINT_TAKEN;
  if (error == "match_active") return MU_LOBBY_HINT_IN_MATCH;
  if (error == "unsupported") return MU_LOBBY_HINT_UNSUPPORTED;
  if (error == "searching") return MU_LOBBY_HINT_SEARCHING;
  if (error == "fixed_rules") return MU_LOBBY_HINT_FIXED_RULES;
  if (error == "unranked_only") return MU_LOBBY_HINT_UNRANKED_ONLY;
  if (error == kTeamsMismatch) return MU_LOBBY_HINT_TEAM_MISMATCH;
  if (error == "search_cancelled") return MU_LOBBY_HINT_SEARCH_STOPPED;
  return MU_LOBBY_HINT_REFUSED;   // not_seated and anything newer than this build
}

inline unsigned char route_code(const std::string& route) {
  if (route.empty()) return MU_LOBBY_ROUTE_NONE;
  if (route == "direct") return MU_LOBBY_ROUTE_DIRECT;
  if (route == "lan") return MU_LOBBY_ROUTE_LAN;
  if (route == "relay") return MU_LOBBY_ROUTE_RELAY;
  return MU_LOBBY_ROUTE_UNKNOWN;
}

// A name or note in Melee's menu font: printable ASCII, anything else as '?', at most N - 1.
template <size_t N>
inline void screen_text(const std::string& in, char (&out)[N]) {
  std::memset(out, 0, sizeof out);
  size_t at = 0;
  for (size_t i = 0; i < in.size() && at + 1 < sizeof out; ++i) {
    const unsigned char c = (unsigned char)in[i];
    if (c >= 0x80) {
      while (i + 1 < in.size() && ((unsigned char)in[i + 1] & 0xC0) == 0x80) ++i;   // one '?' per character
      out[at++] = '?';
    } else out[at++] = c >= 0x20 && c <= 0x7E ? (char)c : '?';
  }
}
inline void screen_name(const std::string& in, char (&out)[MU_LOBBY_NAME]) { screen_text(in, out); }

struct ScreenFlags {
  bool armed = false, guest = false, inviting = false;
  bool closed = false;   // the lobby's search failed or its room closed: only B (leave) is left
  bool leave = false;    // YouGame asked to leave (the app's Leave push): the select backs out as B does
  unsigned char arm = 0;   // which arming this is (slippi_online.cpp): a select from an older one never leaves it
  std::string note;        // why the last match setup fell through, until the next match starts
  unsigned char note_seq = 0;   // bumps once per drop recorded (slippi_online.cpp): each drop's note shows once
};

inline MuLobbyView screen_view(const Control::View& v, const ScreenFlags& f) {
  MuLobbyView out;
  std::memset(&out, 0, sizeof out);
  out.flags = (f.armed ? MU_LOBBY_F_ARMED : 0) | (v.known ? MU_LOBBY_F_KNOWN : 0) | (v.host ? MU_LOBBY_F_HOST : 0) |
              (v.started ? MU_LOBBY_F_STARTED : 0) | (v.unsupported ? MU_LOBBY_F_UNSUPPORTED : 0) |
              (f.inviting ? MU_LOBBY_F_INVITING : 0) | (f.guest ? MU_LOBBY_F_GUEST : 0) |
              (f.armed && f.leave ? MU_LOBBY_F_LEAVE : 0);
  const bool doubles = v.profile == Profile::Doubles;
  // A closed room's last state can't keep it searching either: Rooms never sends the search's end once the
  // room is gone, and the select takes the player's whole pad while it searches (mu_lobby_pads, its B a
  // cancel the closed lobby refuses), so "Lobby closed. Hold B to exit" could not be left (11-18).
  out.mode = (unsigned char)((doubles ? MU_LOBBY_M_DOUBLES : 0) | (doubles && v.searching && !f.closed ? MU_LOBBY_M_SEARCHING : 0) |
                            (!doubles && f.armed && !f.closed && v.state.stage_select ? MU_LOBBY_M_STAGE_SELECT : 0));
#if MU_SQUAD
  if (!doubles && v.state.rules.squad) out.mode |= MU_LOBBY_M_SQUAD;
  if (!doubles && v.squad_available) out.mode |= MU_LOBBY_M_SQUAD_AVAILABLE;
#endif
  out.my_seat = v.my_seat >= 0 && v.my_seat < kSeats ? (unsigned char)v.my_seat : MU_LOBBY_NONE;
  out.host_seat = v.state.host >= 0 && v.state.host < kSeats ? (unsigned char)v.state.host : MU_LOBBY_NONE;
  out.block_hint = (unsigned char)(f.armed && f.closed ? MU_LOBBY_HINT_CLOSED : block_hint(v, f.armed));
  out.refusal_hint = (unsigned char)refusal_hint(v.refusal, v.refusal_action);
  if (v.state.rules.squad && v.refusal_action == "start" && v.refusal == "shape")
    out.refusal_hint = MU_LOBBY_HINT_REFUSED;
  out.refusal_seq = (unsigned char)(v.refusal_count & 0xFF);
  out.arm = f.arm;
  out.note_seq = f.note_seq;
  if (f.armed && !v.started) screen_text(f.note, out.note);
  if (doubles && v.owns_local) out.mode |= MU_LOBBY_M_LOCAL_OWNER;
  const Rules& r = v.state.rules;
  out.rules = {(unsigned char)r.teams, (unsigned char)r.friendly_fire, (unsigned char)r.time,
               (unsigned char)r.stocks, (unsigned char)r.minutes, (unsigned char)r.pause};
  for (int i = 0; i < kSeats; ++i) {
    const Seat& s = v.state.seats[i];
    MuLobbySeat& d = out.seats[i];
    if (doubles && i >= kDoublesSeats) { d.hover_ch = d.pick_ch = MU_LOBBY_NONE; continue; }   // doors 3-4 stay shut
    d.kind = s.kind == Kind::Local ? MU_LOBBY_KIND_LOCAL : s.kind == Kind::Human ? MU_LOBBY_KIND_HUMAN : s.kind == Kind::Cpu ? MU_LOBBY_KIND_CPU : MU_LOBBY_KIND_OPEN;
    d.lineup_count = (unsigned char)s.lineup.size();
    for (size_t j = 0; j < s.lineup.size() && j < 4; ++j) {
      d.lineup[j][0] = (unsigned char)s.lineup[j].ch; d.lineup[j][1] = (unsigned char)s.lineup[j].color;
    }
    d.level = s.kind == Kind::Cpu ? (unsigned char)s.level : 0;
    d.flags = (s.hovering ? MU_LOBBY_S_HOVERING : 0) | (s.picked ? MU_LOBBY_S_PICKED : 0) | (s.ready ? MU_LOBBY_S_READY : 0);
    d.hover_ch = s.hovering ? (unsigned char)s.hover.ch : MU_LOBBY_NONE;
    d.hover_color = s.hovering ? (unsigned char)s.hover.color : 0;
    d.hover_team = s.hovering ? (unsigned char)s.hover.team : 0;
    d.pick_ch = s.picked ? (unsigned char)s.pick.ch : MU_LOBBY_NONE;
    d.pick_color = s.picked ? (unsigned char)s.pick.color : 0;
    d.pick_team = s.picked ? (unsigned char)s.pick.team : 0;
    // Another player's link, only once it is up: nothing before (the spec's "show nothing before that").
    const bool other = s.kind == Kind::Human && i != v.my_seat;
    d.route = other ? route_code(v.links[i].route) : MU_LOBBY_ROUTE_NONE;
    d.rtt_ms = d.route != MU_LOBBY_ROUTE_NONE && v.links[i].rtt >= 0 ? (unsigned short)std::min(v.links[i].rtt, 9999) : 0;
    if (s.kind == Kind::Human || s.kind == Kind::Local) screen_name(s.name, d.name);
  }
  return out;
}

}  // namespace yougame::lobby
