// YouGame friends protocol and presentation state, shared with window-free tests.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <map>
#include <set>
#include "yougame_console_menu.h"

namespace yougame::friends {
using json = nlohmann::json;
// Copy (variant B): the game only knows the versions differ, and during a
// Windows-first rollout "update" alone is wrong for Mac and Linux. The status line does not wrap: short line.
inline constexpr const char* kVersion = "Different Melee Unlocked versions: update from YouGame, or play someone on the same version.";
// The 6 s invite toast wraps at 380 px, so it carries the full next step.
inline constexpr const char* kVersionDetail = "You and your friend are on different Melee Unlocked versions. Whoever is behind: close the game and press Play on YouGame to update. A new version can reach Windows before Mac and Linux, so until then play Friends with someone on the same version.";
inline constexpr const char* kJoined = "Joined";
inline constexpr const char* kFailed = "Couldn't reach your friend";
// A friend room with every seat taken. The invite route answers 409 code "room_full" with its own words;
// a guest's socket upgrade is refused with "the rooms server answered 409: Room is full".
inline constexpr const char* kRoomFull = "This room is full.";
inline constexpr const char* kRequestFailed = "YouGame request failed";   // bridge()'s text when the site sent none
// A friend's Doubles home that nobody is in any more (it expired, or they left): joining it by code would open a
// fresh home under their old code with only this player in it, so the guest leaves and says this instead.
inline constexpr const char* kRoomGone = "Your friend's room is gone. Ask them for a new invite.";
// A friend's room that refused this player's socket after a good ticket (the upgrade refused: 1006, a 403 on the
// room GET, no open within 10 s, or every kind tried), or that opened but never said welcome (kHelloMs). The join
// ends here and the game goes back to the menu: never a quiet wait or another kind of room.
inline constexpr const char* kJoinRefused = "Couldn't join your friend's game. Try again from their invite.";
inline constexpr int kHelloMs = 10000;   // an opened friend's room says welcome at once; 10 s without one ends the join
inline std::string room_full_line(const std::string& host) { return host.empty() ? kRoomFull : host + "'s room is full."; }
inline bool says_room_full(std::string text) {
  for (auto& ch : text) if (ch >= 'A' && ch <= 'Z') ch = char(ch + ('a' - 'A'));
  return text.find("room is full") != std::string::npos;
}
// The line for an invite the site refused because the room is full, empty for any other refusal. The
// code wins when the app forwarded one ("refused" = it sent none); the site's own words show as they are.
inline std::string invite_room_full(const std::string& code, const std::string& error) {
  const bool uncoded = code.empty() || code == "refused";
  if (code != "room_full" && !(uncoded && says_room_full(error))) return {};
  return error.empty() || error == kRequestFailed ? std::string(kRoomFull) : error.substr(0, 120);   // toast-sized
}
// The picker's refusal for a full room, carrying the line to show.
struct RoomFull : std::runtime_error { using std::runtime_error::runtime_error; };
// Accept the explicit rooms refusal/notice reason; never guess from free-form text.
inline bool version_reason(const json& value) {
  const auto reason = value.find("reason");
  return value.is_object() && reason != value.end() && reason->is_string() && *reason == "version";
}
struct InviteEdge {
  uint16_t previous = 0;
  bool need_release = false;
  // connected = the pad is present AND the game has the keyboard/pads. After it was not (the page's
  // friend picker had focus, a hot-plug), an A or START still held must be let go before a press counts:
  // otherwise a held button reopened the picker every time focus came back (seen 10-30).
  bool poll_buttons(bool connected, uint16_t buttons, uint16_t mask) {
    if (!connected) { previous = 0; need_release = true; return false; }
    if (need_release) {
      if (!(buttons & 0x1100)) need_release = false;
      previous = buttons;
      return false;
    }
    const uint16_t pressed = buttons & ~previous;
    previous = buttons;
    return (pressed & mask) != 0;
  }
  bool poll(bool connected, uint16_t buttons, bool entry) {
    return poll_buttons(connected, buttons, entry ? 0x1100 : 0);
  }
  // Compatibility for the protected console entry's START-only ready/lock-in edge.
  // Direct code entry never calls this overload or owns an Events picker context.
  bool poll(bool connected, uint16_t buttons, bool start_only, bool enabled, bool entry) {
    return poll_buttons(connected, buttons, entry ? 0x1100 : start_only && enabled ? 0x1000 : 0);
  }

};
inline constexpr const char* kWaiting = "Waiting for your friend...";
inline constexpr const char* kTeamWaiting = "Waiting for your team...";
inline constexpr const char* kTeamCodeError = "Team codes need 2 to 9 letters or numbers";
inline constexpr int kPickerTimeout = 600000;

// The game's Direct code field is Shift-JIS: ASCII letters and digits with the # as the full-width
// 0x81 0x94 (convert_connect_code_for_game), and the on-screen keyboard can also give full-width
// letters, digits or spaces. YouGame's search is UTF-8 JSON, so the code goes in as plain ASCII
// ("LAIN#504"): the raw bytes made the request's dump() throw type_error.316, shown on CSS.
// Anything outside that set returns false so the search fails with a readable message.
inline bool direct_code_from_game(const std::string& sj, std::string* out) {
  std::string code;
  for (size_t i = 0; i < sj.size(); ++i) {
    const auto c = static_cast<unsigned char>(sj[i]);
    if (c == 0) break;
    if (c < 0x80) {
      if (c >= 'a' && c <= 'z') code += static_cast<char>(c - 'a' + 'A');
      else if (c != ' ') code += static_cast<char>(c);
      continue;
    }
    if (i + 1 >= sj.size()) return false;
    const auto d = static_cast<unsigned char>(sj[++i]);
    if (c == 0x81 && d == 0x94) code += '#';
    else if (c == 0x81 && d == 0x40) continue;
    else if (c == 0x82 && d >= 0x4F && d <= 0x58) code += static_cast<char>('0' + d - 0x4F);
    else if (c == 0x82 && d >= 0x60 && d <= 0x79) code += static_cast<char>('A' + d - 0x60);
    else if (c == 0x82 && d >= 0x81 && d <= 0x9A) code += static_cast<char>('A' + d - 0x81);
    else return false;
  }
  for (const char ch : code)
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '#')) return false;
  if (out) *out = code;
  return true;
}

inline bool search_code(int mode, const std::string& raw, std::string* out) {
  std::string code;
  const bool folded = direct_code_from_game(raw, &code);
  if (!folded && (mode == 2 || (mode == 3 && !raw.empty()))) return false;
  if (out) *out = code;
  return true;
}

inline bool team_room_code(const std::string& folded, std::string* out) {
  std::string code;
  for (char c : folded) {
    if (c == '#') continue;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    code += c;
  }
  if (code.size() < 2 || code.size() > 9) return false;
  // A two-letter code ("EC") pads to the six-character room every longer code already makes.
  if (code.size() == 2) code += '0';
  if (out) *out = "mut" + code;
  return true;
}
inline json team_ticket(const std::string& compatibility, const std::string& code) {
  if (code.size() < 6 || code.size() > 12 || code.compare(0, 3, "mut") != 0)
    throw std::runtime_error(kTeamCodeError);
  for (char c : code)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) throw std::runtime_error(kTeamCodeError);
  return {{"type", "mpTicket"}, {"players", 4}, {"mode", "teams"},
          {"room", code}, {"ranked", false},
          {"lobby", {{"version", 1}, {"minPlayers", 4}, {"maxLocalPlayers", 1}, {"compatibility", compatibility}}}};
}

inline std::string text(const json& value, const char* key, size_t max) {
  if (!value.is_object()) return {};
  const auto it = value.find(key);
  if (it == value.end() || !it->is_string()) return {};
  const auto& s = it->get_ref<const std::string&>();
  if (s.empty() || s.size() > max * 4) return {};
  size_t count = 0;
  for (size_t i = 0; i < s.size();) {
    const auto lead = static_cast<unsigned char>(s[i++]);
    uint32_t cp = lead;
    int tails = 0;
    if (lead >= 0xC2 && lead <= 0xDF) { cp &= 0x1F; tails = 1; }
    else if (lead >= 0xE0 && lead <= 0xEF) { cp &= 0x0F; tails = 2; }
    else if (lead >= 0xF0 && lead <= 0xF4) { cp &= 0x07; tails = 3; }
    else if (lead >= 0x80) return {};
    if (i + tails > s.size()) return {};
    for (int n = 0; n < tails; ++n) {
      const auto c = static_cast<unsigned char>(s[i++]);
      if ((c & 0xC0) != 0x80) return {};
      cp = (cp << 6) | (c & 0x3F);
    }
    if ((tails == 1 && cp < 0x80) || (tails == 2 && cp < 0x800) ||
        (tails == 3 && cp < 0x10000) || cp > 0x10FFFF ||
        (cp >= 0xD800 && cp <= 0xDFFF) || cp < 32 || (cp >= 127 && cp <= 159) ||
        ++count > max) return {};
  }
  return s;
}
inline std::string room_code(const json& value, const char* key) {
  auto s = text(value, key, 16);
  if (s.size() < 4 || s.size() > 16) return {};
  for (unsigned char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return {};
  return s;
}
inline std::string room(const json& value) { return room_code(value, "room"); }
inline std::string picker_room(const json& value) { return room_code(value, "code"); }
// The picker supplies the host code; invitees repeat it in the `room` field.
inline json ticket(const std::string& compatibility, const std::string& code) {
  if (room(json{{"room", code}}).empty()) throw std::runtime_error("Invalid YouGame friends room");
  return {{"type", "mpTicket"}, {"players", 2}, {"mode", "singles"},
          {"room", code}, {"ranked", false},
          {"lobby", {{"version", 1}, {"minPlayers", 2}, {"maxLocalPlayers", 1}, {"compatibility", compatibility}}}};
}
// The 4-player Friends lobby (yougame_lobby.h). The host holds a room (the desktop app turns this into
// the room's ticket, with a 12-character code), reports it with mpRoom, then invites up to three
// friends at once; a guest joins the code its invite carries with lobby_ticket. The compatibility
// key carries friends-lobby-1 (yougame_lobby_setup.h compatibility()), at most 64 characters.
inline bool compatibility_chars(const std::string& c) {
  if (c.empty() || c.size() > 64) return false;
  for (unsigned char ch : c)
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' ||
          ch == '_' || ch == ':' || ch == '-')) return false;
  return true;
}
inline bool lobby_compatibility(const std::string& c) {
  if (!compatibility_chars(c)) return false;
  const std::string tag = "friends-lobby-1";
  for (size_t at = c.find(tag); at != std::string::npos; at = c.find(tag, at + 1))
    if ((at == 0 || c[at - 1] == ':') && (at + tag.size() == c.size() || c[at + tag.size()] == ':')) return true;
  return false;
}
// The Doubles home (G2, rooms MU-DOUBLES-R2): every call's compatibility STARTS with doubles-lobby-1:
// (the held adapter refuses a tail-only marker), the same key for the home, the queue and the match.
inline bool doubles_compatibility(const std::string& c) {
  static const std::string tag = "doubles-lobby-1:";
  return compatibility_chars(c) && c.size() > tag.size() && c.compare(0, tag.size(), tag) == 0;
}
inline json doubles_lobby(const std::string& compatibility, int min_players) {
  return {{"version", 1}, {"minPlayers", min_players}, {"maxLocalPlayers", 1}, {"compatibility", compatibility}};
}
// Hold this player's own 2-seat home (the desktop app makes it a private teams/teamsLobby:1 ticket).
inline json doubles_hold(const std::string& compatibility) {
  if (!doubles_compatibility(compatibility)) throw std::runtime_error("Invalid Doubles compatibility");
  return {{"type", "mpFriend"}, {"hold", true}, {"players", 2}, {"compatibility", compatibility}};
}
// Invite one partner (Teams invites are single-friend: no multi).
inline json doubles_invite(const std::string& code) {
  if (room(json{{"room", code}}).empty()) throw std::runtime_error("Invalid YouGame friends room");
  return {{"type", "mpFriend"}, {"mode", "teams"}, {"teamsLobby", 1}, {"room", code}, {"players", 2}};
}
// A partner joining the home its invite names (or this player's home again after a match).
inline json doubles_home_ticket(const std::string& compatibility, const std::string& code) {
  if (room(json{{"room", code}}).empty()) throw std::runtime_error("Invalid YouGame friends room");
  if (!doubles_compatibility(compatibility)) throw std::runtime_error("Invalid Doubles compatibility");
  return {{"type", "mpTicket"}, {"mode", "teams"}, {"teamsLobby", 1}, {"players", 2}, {"room", code},
          {"ranked", false}, {"lobby", doubles_lobby(compatibility, 2)}};
}
// The home's search, one per human: the generation lobby.party.queueing names (partySearch), and for a
// friend pair (n 2) the party that keeps them on one team. A CPU partner is never a local player: the
// server reads that seat from the frozen home.
inline constexpr int kLocalPartnerCapability = 1;
inline json doubles_queue_ticket(const std::string& compatibility, const std::string& home, const std::string& generation,
                                 int humans) {
  if (room(json{{"room", home}}).empty()) throw std::runtime_error("Invalid YouGame friends room");
  if (!doubles_compatibility(compatibility)) throw std::runtime_error("Invalid Doubles compatibility");
  if (generation.empty() || generation.size() > 128) throw std::runtime_error("Invalid Doubles search");
  json t = {{"type", "mpTicket"}, {"mode", "teams"}, {"teamsLobby", 1}, {"ranked", false}, {"players", 4},
            {"party_room", home}, {"partySearch", generation}, {"queue", "casual"},
            {"local_partner", kLocalPartnerCapability}, {"lobby", doubles_lobby(compatibility, 4)}};
  if (humans == 2) t["party"] = {{"id", generation}, {"n", 2}};
  return t;
}
// The match room's admission, and its renewals (rooms MU-DOUBLES-R2-REPORT: "a freshly minted match-admission
// ticket can also use normal Teams mode/teamsLobby, players 4, casual and the same min4/maxLocal1 lobby, with
// no room or party fields"). The queue's own ticket names the home (party_room, partySearch, party), which a
// renewal in the match room must not.
inline json doubles_match_ticket(const std::string& compatibility) {
  if (!doubles_compatibility(compatibility)) throw std::runtime_error("Invalid Doubles compatibility");
  return {{"type", "mpTicket"}, {"mode", "teams"}, {"teamsLobby", 1}, {"players", 4}, {"ranked", false},
          {"local_partner", kLocalPartnerCapability}, {"lobby", doubles_lobby(compatibility, 4)}};
}
// Room kinds (Events::room_kind): what a joined room turned out to be. kRoomArena is only ever an invite's
// own word (inviteMode "arena"): the arena's 8-seat room (slippi_arena.cpp under YouGame) is joined as itself,
// never tried as another kind and never reached by a retry of one.
inline constexpr int kRoom1v1 = 0, kRoomFriends4 = 1, kRoomDoubles = 2, kRoomArena = 3;
// The launch invite the page hands over for a Teams invite (Player.tsx hello, /api/multiplayer/teams):
// nativeTeamsInvite {v, room, mode:"teams", md?}. Without md it is a Teams home this build's Doubles select
// may be (tried first; the room decides); md:1 is the legacy console Doubles path (MELEE_DOUBLES).
inline bool doubles_hint(const json& invite) {
  return invite.is_object() && invite.value("mode", json()) == "teams" && !invite.count("md");
}
// The kind an accepted invite opens, from the page's inviteMode (Player.tsx hello reply and join push, PR 2280):
// "singles" (MU's 1v1 picker) kRoom1v1, "default" (an invite with no mode: MU's 4-player Friends lobby)
// kRoomFriends4, "teams" (a Teams invite; its push also has teamsLobby:1) kRoomDoubles, "arena" (an invite into an
// arena's room, players 8) kRoomArena. -1 when absent (not from an invite) or anything else (unknown): the older
// hints (players, compatibility, Teams marks) then decide as before.
inline int invite_mode_kind(const json& value) {
  if (!value.is_object() || !value.count("inviteMode") || !value["inviteMode"].is_string()) return -1;
  const auto& mode = value["inviteMode"].get_ref<const std::string&>();
  return mode == "singles" ? kRoom1v1 : mode == "default" ? kRoomFriends4 : mode == "teams" ? kRoomDoubles
       : mode == "arena" ? kRoomArena : -1;
}
// The arena's room under YouGame: one private room of kArenaSeats for the whole arena (Hero's room limit), keyed
// by this build's own key plus ":arena-1" (lobby::arena_compatibility), so the Friends lobby's 4-seat rules and its
// console match never apply. The host opens it with a room code of its own; everyone else joins that code. A plain
// ticket, never the app's hold: the hold only takes 2 or 4 seats.
inline constexpr int kArenaSeats = 8;
inline json arena_ticket(const std::string& compatibility, const std::string& code) {
  if (room(json{{"room", code}}).empty()) throw std::runtime_error("Invalid YouGame arena room");
  if (compatibility.empty() || compatibility.size() > 64) throw std::runtime_error("Invalid arena compatibility");
  return {{"type", "mpTicket"}, {"queue", "private"}, {"room", code}, {"players", kArenaSeats},
          {"lobby", {{"version", 1}, {"minPlayers", 2}, {"maxLocalPlayers", 1}, {"compatibility", compatibility}}}};
}
// The app's invite picker for the arena's room. `multi` asks for its several-friends picker, which takes more than
// 4 seats only from the app build after this one; without it the picker invites one friend at a time.
inline json arena_invite(const std::string& code, bool multi) {
  if (room(json{{"room", code}}).empty()) throw std::runtime_error("Invalid YouGame arena room");
  json q = {{"type", "mpFriend"}, {"room", code}, {"players", kArenaSeats}, {"mode", "arena"}};
  if (multi) q["multi"] = true;
  return q;
}
// The arena panel's lines for a room that did not open or an invite that did not go: the arena's own plain words,
// never the app's or the socket's reason ("Test build not available", "This game sent a match request YouGame
// can't read."), which goes to the log. Reasons that already speak to the player show as they are: a full room in
// the site's words (invite_room_full), an update, YouGame not open.
inline constexpr const char* kArenaOpenFailed = "Couldn't open the arena. Try again.";
inline constexpr const char* kArenaInviteFailed = "Couldn't send the invite. Press Z to try again.";
// The ticket route's refusals of a Doubles home ticket that mean "not a Doubles home for this build", not "gone"
// (web/src/app/api/multiplayer/ticket/route.ts): teams_lobby_off 403 (Teams is off on this site), bad_teams_lobby
// 400/403 (no Teams for this listing or room), teams_lobby_branch 409 (a test build without Teams). Since build 94
// no ticket refusal tries another kind: find_match shows these words and goes back to the
// menu like any other refusal; only the room's own kind refusal over the socket may reroute (mismatch_next).
// teams_home 409 is never one: the route also answers it for a friend's home that is gone (teams_invite:1 with no
// home), busy, full, blocked or on another remix, and a 1v1 try after it would make rooms open a fresh, empty
// 1v1 room under the dead code. Its words show at once. An expired or missing room is never one either: rooms
// answers those over the socket (host_gone 410, teams_home_expired / teams_invite_expired 409), and a Doubles
// guest that lands alone in a home leaves (kRoomGone).
inline bool doubles_kind_refusal(const std::string& code) {
  return code == "teams_lobby_off" || code == "bad_teams_lobby" || code == "teams_lobby_branch";
}
inline json lobby_hold(const std::string& compatibility) {
  if (!lobby_compatibility(compatibility)) throw std::runtime_error("Invalid Friends lobby compatibility");
  return {{"type", "mpFriend"}, {"hold", true}, {"players", 4}, {"compatibility", compatibility}};
}
inline json lobby_invite(const std::string& code) {
  if (room(json{{"room", code}}).empty()) throw std::runtime_error("Invalid YouGame friends room");
  return {{"type", "mpFriend"}, {"room", code}, {"players", 4}, {"multi", true}};
}
inline json lobby_ticket(const std::string& compatibility, const std::string& code) {
  if (room(json{{"room", code}}).empty()) throw std::runtime_error("Invalid YouGame friends room");
  if (!lobby_compatibility(compatibility)) throw std::runtime_error("Invalid Friends lobby compatibility");
  return {{"type", "mpTicket"}, {"players", 4}, {"mode", "singles"},
          {"room", code}, {"ranked", false},
          {"lobby", {{"version", 1}, {"minPlayers", 1}, {"maxLocalPlayers", 1}, {"compatibility", compatibility}}}};
}
inline bool native_ready(bool yougame, int, const std::string&, const std::string& invite) {
  return !(yougame && !invite.empty());
}
// The ordinary Direct winner path locks in on START without another code/stage screen.
// A random first stage is already supplied by native_start_match, as for other searches.
inline constexpr int kFirstCssWinner = 1;
inline json picker(const std::string& held = {}) {
  json q = {{"type", "mpFriend"}, {"mode", "singles"}, {"players", 2}};
  if (!held.empty()) q["room"] = held;
  return q;
}
inline bool version_request(const json& request) {
  return request.value("type", "") == "mpFriend" ||
      (request.value("type", "") == "mpTicket" && request.value("mode", "") == "singles" &&
       request.value("players", 0) == 2 && !request.value("ranked", false) &&
       !request.count("direct") && !room(request).empty());
}
inline std::string expire_joined(const std::string& status, uint64_t since, uint64_t now) {
  return status == kJoined && now - since >= 10000 ? std::string() : status;
}
// Shared request path: no ticket can be sent before a valid picker answer (or invite).
template <class Send, class Cancelled>
std::string resolve_room(const std::string& invite, Send send, Cancelled cancelled) {
  if (cancelled()) throw std::runtime_error("Cancelled");
  const auto code = invite.empty() ? picker_room(send(picker(), kPickerTimeout))
                                   : room(json{{"room", invite}});
  if (cancelled()) throw std::runtime_error("Cancelled");
  if (code.empty()) throw std::runtime_error("Invalid YouGame friends room");
  return code;
}
template <class Send, class Cancelled, class Starting>
json request_ticket(const std::string& compatibility, const std::string& invite,
                    json& request, Send send, Cancelled cancelled, Starting starting) {
  const auto code = resolve_room(invite, send, cancelled);
  request = ticket(compatibility, code);
  starting(code);  // track our room before the page can push its own URL's join
  return send(request, 30000);
}
// Restrict scene changes to the initialized main menu or online CSS/stage/results menus.
// Every match (including local play), replay and other scene remains untouched.
inline bool menu_safe(int major, int minor) { return major == 1 || (major == 8 && (minor == 0 || minor == 1 || minor == 3)) || console::menu_select(major,minor); }

class Events {
 private:
  // The room size an invite or launch hello names (2 or 4), 4 when it names the Friends lobby's compatibility
  // key, else 0 (unknown).
  static int hinted_players(const json& value) {
    if (value.count("players") && value["players"].is_number_integer() &&
        (value["players"] == 2 || value["players"] == 4)) return value["players"].get<int>();
    if (value.count("compatibility") && value["compatibility"].is_string() &&
        lobby_compatibility(value["compatibility"].get<std::string>())) return 4;
    return 0;
  }
 public:
  void hello(const json& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (hello_seen_) return;
    hello_seen_ = true;
    const auto code = room(value);
    if (pending_.empty() && code != current_room_ && !retired_.count(code)) {
      pending_ = code;  // a newer push wins over the launch invite
      // A Teams invite (nativeTeamsInvite, mode teams, no md) tries the Doubles home first, then 1v1, then the
      // Friends lobby (mismatch_next); anything else tries 1v1 first (then the Friends lobby, then Doubles), as
      // before. Hints only: the room decides.
      // The page's hello also names the room's size when it knows it (Player.tsx nativeRoomSeats: 2 for a
      // 1v1 friend room, 4 for the Friends lobby), as an invite push does: the Friends lobby then goes first and
      // a refusal means another build, not another kind. Without it a launch into a Friends lobby tried 1v1, then
      // the Doubles home, whose teams_home answer stops the tries before the Friends lobby (11-08).
      const auto inv = value.is_object() ? value.value("nativeTeamsInvite", json()) : json();
      pending_players_ = value.is_object() ? hinted_players(value) : 0;
      pending_hinted_ = pending_players_ != 0;
      pending_strict_ = false;
      // A Teams invite outranks the seat count: the page checked the home (/api/multiplayer/teams), while a
      // 4-seat Doubles home also reads as players 4.
      pending_first_ = doubles_hint(inv) && room(inv) == code ? kRoomDoubles
                     : pending_players_ == 4 ? kRoomFriends4 : kRoom1v1;
      // The page's inviteMode outranks both (invite_mode_kind); a legacy md:1 Teams invite keeps its own path.
      if (!(inv.is_object() && inv.value("md", json()) == 1 && room(inv) == code))
        invite_mode_locked(invite_mode_kind(value));
      pending_doubles_ = pending_first_ == kRoomDoubles;
      tried_.erase(code);
#ifdef MELEE_DOUBLES
      // The page marks a launch invite into a Doubles home; rooms still checks the profile.
      const auto invite = value.is_object() ? value.value("nativeTeamsInvite",json()) : json();
      doubles_join_ = invite.is_object() && invite.value("md",json())==1 && room(invite)==code;
#endif
    }
  }
  bool push(const json& value, uint64_t now) {
    const auto type = text(value, "type", 16);
    if (type == "join") {
      const auto code = room(value);
      if (code.empty()) return false;
      std::lock_guard<std::mutex> lock(mutex_);
      if (code == current_room_) { fresh_invite_ = false; return false; }
      // Old page pushes carry only a room code. A new invite notice grants one new
      // acceptance, including a retry of a still-live retired room. Never let that
      // old room displace a pending invite or another active session.
      if (retired_.count(code) && (!fresh_invite_ || !pending_.empty() || !current_room_.empty() ||
          (!invite_room_.empty() && invite_room_ != code))) return false;
      retired_.erase(code); fresh_invite_ = false; invite_room_.clear();
      pending_ = code;
      // The invite's room size when the page says (a 1v1 friend room 2, the Friends lobby 4); 0 unknown,
      // or 4 when the invite names the Friends lobby (compatibility friends-lobby-1). Only a hint for which
      // way to try first (and to draw the screen): the room decides (room_kind, room_decided).
      pending_players_ = hinted_players(value);
      // Whether the invite said anything about its room (a size or any compatibility key): a refusal then
      // means a different build or remix, not the other kind of room (mismatch_reroutes).
      // md:1 (or mu_doubles:1, the legacy console Doubles path) is a hint too: that join never tries another
      // kind, or a build without MELEE_DOUBLES would cycle through "another Doubles profile", teams_home and
      // the Friends lobby before showing anything.
      pending_hinted_ = pending_players_ != 0 || (value.count("compatibility") && value["compatibility"].is_string() &&
                                                  !value["compatibility"].get<std::string>().empty()) ||
                        value.value("md", json()) == 1 || value.value("mu_doubles", json()) == 1;
      pending_strict_ = value.value("md", json()) == 1 || value.value("mu_doubles", json()) == 1;
      // Which kind goes first. A push marked teamsLobby:1 (the page's Teams invite) tries the Doubles home first.
      // A push that names nothing ({type:"join", room}) tries 1v1 first, as before G2: tournament bracket pushes
      // (Player.tsx tournamentPush) and 1v1 / Friends invites whose seat lookup missed send exactly that, and so
      // may an older page's Teams invite, which then reaches Doubles after two refusals (mismatch_next: 1v1,
      // Friends lobby, Doubles). md:1 is the legacy console Doubles path (MELEE_DOUBLES), never this one.
      const bool teams_marked = value.value("teamsLobby", json()) == 1 && !value.count("md") &&
                                value.value("mu_doubles", json()) != 1;
      pending_first_ = pending_players_ == 4 ? kRoomFriends4 : teams_marked ? kRoomDoubles : kRoom1v1;
      // The page's inviteMode outranks the size and the Teams mark (invite_mode_kind). Not for md:1 /
      // mu_doubles:1: that legacy join keeps its own strict path.
      if (!pending_strict_) invite_mode_locked(invite_mode_kind(value));
      pending_doubles_ = pending_first_ == kRoomDoubles;
      tried_.erase(code);
#ifdef MELEE_DOUBLES
      doubles_join_ = value.value("md",json())==1 || value.value("mu_doubles",json())==1;
#endif
      return true;
    }
    if (type == "notice" && version_reason(value)) {
      std::lock_guard<std::mutex> lock(mutex_);
      version_notice_ = true; picker_error_ = kVersion;
      toast_ = kVersionDetail; toast_until_ = now + 6000;
      return true;
    }
    if (type == "notice" && text(value, "kind", 16) == "message") {
      // A friend's chat line or direct message (page caps these with invites at 5 a minute).
      // Display only: its own line under the invite toast, never an invite or a room.
      const auto from = text(value, "from", 40), line = text(value, "text", 140), id = text(value, "id", 32);
      if (from.empty() || line.empty()) return false;
      std::lock_guard<std::mutex> lock(mutex_);
      // Ids are a page counter that restarts with the page, so a repeat is the same id AND line.
      auto shown = from + ": " + line;
      if (!id.empty() && id == message_id_ && shown == message_) return false;
      message_id_ = id; message_ = std::move(shown);
      message_until_ = now + 6000;
      return true;
    }
    if (type != "notice" || text(value, "kind", 16) != "invite") return false;
    const auto from = text(value, "from", 40), line = text(value, "text", 80);
    if (from.empty() || line.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    fresh_invite_ = true; invite_room_ = room(value);
    inviter_ = from; inviter_room_ = invite_room_;
    toast_ = from + " " + line;
    toast_until_ = now + 6000;
    return true;
  }
  bool report_version() {
    std::lock_guard<std::mutex> lock(mutex_);
    return !std::exchange(version_logged_, true);
  }
  bool take_version() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(version_notice_, false);
  }
  // A join waiting in a menu scene: the guest's panel shows it and Z cancels it. In a match
  // it stays a toast, so a grab never cancels the invite.
#ifdef MELEE_DOUBLES
  bool doubles_join_pending() const { std::lock_guard<std::mutex> lock(mutex_); return !pending_.empty() && doubles_join_; }
#endif
  bool join_pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pending_.empty() && menu_scene_;
  }
  void menu_scene(bool safe) { std::lock_guard<std::mutex> lock(mutex_); menu_scene_ = safe; }
  std::string take_join(bool safe) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!safe) return {};
    join_players_ = std::exchange(pending_players_, 0);
    join_hinted_ = std::exchange(pending_hinted_, false);
    join_strict_ = std::exchange(pending_strict_, false);
    join_doubles_ = std::exchange(pending_doubles_, false);
    join_first_ = std::exchange(pending_first_, kRoom1v1);
    return std::exchange(pending_, {});
  }
  // The last taken join tries a Doubles home first (G2): the guest arms the 2-seat select until the room says.
  bool join_doubles() const { std::lock_guard<std::mutex> lock(mutex_); return join_doubles_; }
  // The room size the last taken join's push named (2, 4, or 0 when it named none). A hint only.
  int join_players() const { std::lock_guard<std::mutex> lock(mutex_); return join_players_; }
  // The last taken join is an invite into an arena's room (inviteMode "arena"): the arena joins it, nothing else.
  bool join_arena() const { std::lock_guard<std::mutex> lock(mutex_); return join_first_ == kRoomArena; }
  // A room that refused the ticket with lobby_mismatch is tried the other way only when nothing else
  // explains it: its kind isn't known yet and the taken join named no size and no compatibility key.
  // lobby_mismatch is also the refusal for a different build or remix (and console_mixed_builds), which
  // an invite with a size or key would otherwise turn into a pointless second try.
  // A kind refusal (rooms' teams_protocol / doubles_profile: "another Teams protocol", "another Doubles
  // profile") means a room of another kind, so it reroutes a join hinted by a size or key too: a seat count
  // can't tell a 1v1 room from a 2-seat Teams home, or a Friends lobby from a 4-seat Doubles home. Not an
  // md:1 / mu_doubles:1 join (join_strict_): there "another Doubles profile" is a build without MELEE_DOUBLES.
  bool mismatch_reroutes(const std::string& code, bool kind_refusal = false) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !code.empty() && !room_kinds_.count(code) && !blocked_locked(kind_refusal);
  }
  // The next kind to try after a refusal of `tried` (the way this join went in: a socket lobby_mismatch or
  // another-Teams-protocol refusal; never a ticket refusal), in the order the
  // taken join's hint gives (Doubles first: Doubles, 1v1, Friends lobby; Friends lobby first: Friends lobby,
  // 1v1, Doubles; otherwise 1v1, Friends lobby, Doubles); -1 when mismatch_reroutes says no or every kind was
  // tried. The kind it returns counts as tried at once, so a menu that joins another way than asked (no app
  // bridge on the static recomp, the console entry, a picker open) still gives a room three tries at most.
  // `error` is the refusal's own words: the first one is kept for when the tries run out (first_refusal).
  int mismatch_next(const std::string& code, int tried, const std::string& error = {}, bool kind_refusal = false) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (code.empty() || room_kinds_.count(code) || blocked_locked(kind_refusal) || tried < 0 || tried > 2) return -1;
    if (tried_.size() >= 32 && !tried_.count(code)) tried_.clear();
    Tries& t = tried_[code];
    if (t.first.empty() && !error.empty()) t.first = error;
    t.mask |= 1 << tried;
    // Unhinted: the Friends lobby before the Doubles home. A Friends try at a Doubles home is refused over the
    // socket (lobby_mismatch, a kind refusal: the tries go on), while a Doubles try at a Friends lobby gets the
    // route's teams_home, which ends them (11-08: a launch into a Friends lobby ended on "Your Teams home changed").
    static constexpr int kOrder[3][3] = {{kRoom1v1, kRoomFriends4, kRoomDoubles},
                                         {kRoomFriends4, kRoom1v1, kRoomDoubles},
                                         {kRoomDoubles, kRoom1v1, kRoomFriends4}};
    for (int k : kOrder[join_first_ >= 0 && join_first_ <= 2 ? join_first_ : 0])
      if (!(t.mask & (1 << k))) { t.mask |= 1 << k; return k; }
    return -1;
  }
  // The first refusal's words for a room whose tries ran out (mismatch_next -1), or empty: the error shown then,
  // since the last kind tried is usually the least likely one and its refusal the least useful.
  std::string first_refusal(const std::string& code) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = tried_.find(code);
    return it == tried_.end() ? std::string() : it->second.first;
  }
  // The words a failed try of an invited room shows. A refusal that isn't a kind refusal (`kind` false: host_gone,
  // tournament_seat, a full or expired home, ...) is a real reason and shows its own words, even after other kinds
  // were tried. A kind refusal whose tries ran out shows the first refusal's words (first_refusal), else its own.
  std::string refusal_text(const std::string& code, const std::string& error, bool kind) const {
    if (!kind) return error;
    const auto first = first_refusal(code);
    return first.empty() ? error : first;
  }
  // What a joined room said it is, which decides the way in (the invite's size, key and Teams mark are hints):
  // a Friends lobby room answers the join with welcome.muLobby (4 seats) and a yg.mu.lobby state, a Doubles
  // home the same with 2 seats and its `searching`, a 1v1 room with neither; a room of another kind refuses
  // the ticket (lobby_mismatch). kRoom1v1 0, kRoomFriends4 1, kRoomDoubles 2, -1 not heard from yet.
  int room_kind(const std::string& code) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = room_kinds_.find(code);
    return it == room_kinds_.end() ? -1 : it->second;
  }
  void room_seen(const std::string& code, int kind) {
    std::lock_guard<std::mutex> lock(mutex_);
    remember_kind(code, kind);
  }
  // The room turned out to be another kind than the way it was joined: recorded, and the join queued
  // again so the menu takes it the right way. Once only per room: its kind is known from here on.
  void room_decided(const std::string& code, int kind) {
    if (code.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    remember_kind(code, kind);
    requeue(code, kind);
    pending_hinted_ = true;
  }
  // A lobby_mismatch refusal: the join queued again as the next kind (mismatch_next), still unknown and
  // unhinted, so another refusal tries the one after it.
  void room_retry(const std::string& code, int kind) {
    if (code.empty() || kind < 0 || kind > 2) return;
    std::lock_guard<std::mutex> lock(mutex_);
    requeue(code, kind);
    pending_hinted_ = false;
    pending_first_ = join_first_;
  }
  std::string toast(uint64_t now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return now < toast_until_ ? toast_ : std::string();
  }
  std::string message(uint64_t now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return now < message_until_ ? message_ : std::string();
  }
  // Milliseconds each notice has left (0: none): the game's own notice line fades out on its last part.
  uint64_t toast_left(uint64_t now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return now < toast_until_ ? toast_until_ - now : 0;
  }
  uint64_t message_left(uint64_t now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return now < message_until_ ? message_until_ - now : 0;
  }
  void current_room(const std::string& code) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!current_room_.empty() && code != current_room_) retired_.insert(current_room_);
    current_room_ = code;
    if (!code.empty()) { fresh_invite_ = false; invite_room_.clear(); }
    if (!code.empty() && pending_ == code) pending_.clear();
  }
  void picker_error(std::string error) {
    std::lock_guard<std::mutex> lock(mutex_);
    picker_error_ = std::move(error);
  }
  std::string picker_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return picker_error_;
  }
  void online_menu(bool visible, bool selected = false) {
    std::lock_guard<std::mutex> lock(mutex_);
    online_menu_ = visible; friend_entry_ = visible && selected;
    if (!friend_entry_) host_requested_ = false;
  }
  bool online_menu() const { std::lock_guard<std::mutex> lock(mutex_); return online_menu_; }
  bool friend_entry() const { std::lock_guard<std::mutex> lock(mutex_); return friend_entry_; }
  bool picker_context() const { std::lock_guard<std::mutex> lock(mutex_); return friend_entry_; }
  // What asked for the picker last ("pad N buttons XXXX" or the keyboard/overlay path), for the entry log line.
  std::string trigger() const { std::lock_guard<std::mutex> lock(mutex_); return trigger_; }
  // connected = the pad is present. Not window focus: app-relayed pads and the GC adapter reach the
  // game while the page holds the keyboard, and the game's own menu takes that A (11-01 report: "I just
  // pressed A on friends and nothing happened"). An open picker counts as away, so the A or START that
  // closed it must be let go before it can open another.
  bool poll_pad(int port, bool connected, uint16_t buttons) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (port < 0 || port >= 4) return false;
    if (!edges_[port].poll(connected && !picker_open_, buttons, friend_entry_)) return false;
    char why[40];
    std::snprintf(why, sizeof why, "pad %d buttons %04x", port + 1, buttons);
    trigger_ = why;
    host_requested_ = true; picker_error_.clear(); version_notice_ = version_logged_ = false; return true;
  }
  void request_host() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (friend_entry_) {
      trigger_ = "Enter or the overlay button";
      host_requested_ = true; picker_error_.clear(); version_notice_ = version_logged_ = false;
    }
  }
  bool take_host() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(host_requested_, false);
  }
  void picker_open(bool open) { std::lock_guard<std::mutex> lock(mutex_); picker_open_ = open; }
  // Who invited this player into a room, from the invite notice; empty when unknown.
  std::string inviter(const std::string& code) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !code.empty() && code == inviter_room_ ? inviter_ : std::string();
  }
  // The friend room was full when this player arrived: the line shows, and the game goes back to the
  // Friends menu once (within 15 s, from the online character select).
  void room_full(std::string line, uint64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    toast_ = std::move(line); toast_until_ = now + 6000;
    full_return_until_ = now + 15000;
  }
  bool take_full_return(bool ready, uint64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!full_return_until_) return false;
    if (now >= full_return_until_) { full_return_until_ = 0; return false; }
    if (!ready) return false;
    full_return_until_ = 0;
    return true;
  }
  // One line on screen for a Friends press the host could not act on; never silence.
  void notice(std::string line, uint64_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    toast_ = std::move(line);
    toast_until_ = now + 6000;
  }
 private:
  mutable std::mutex mutex_;
  InviteEdge edges_[4];
  std::string trigger_;
  bool menu_scene_ = false;
#ifdef MELEE_DOUBLES
  bool doubles_join_ = false;
#endif
  bool online_menu_ = false, friend_entry_ = false, version_notice_ = false, version_logged_ = false;
  bool hello_seen_ = false, host_requested_ = false, picker_open_ = false;
  std::set<std::string> retired_; // Retired room pushes need a fresh invite acceptance.
  bool fresh_invite_ = false;
  std::string invite_room_, pending_, toast_, current_room_, picker_error_;
  int pending_players_ = 0, join_players_ = 0;
  bool pending_hinted_ = false, join_hinted_ = false;
  bool pending_strict_ = false, join_strict_ = false;   // md:1 / mu_doubles:1: never another kind (mismatch_reroutes)
  // Whether the taken join may not try another kind after this refusal (mutex_ held).
  bool blocked_locked(bool kind_refusal) const { return join_strict_ || (join_hinted_ && !kind_refusal); }
  bool pending_doubles_ = false, join_doubles_ = false;
  int pending_first_ = kRoom1v1, join_first_ = kRoom1v1;   // the kind the join tries first (mismatch_next)
  std::map<std::string, int> room_kinds_;   // room code -> its kind (room_kind)
  struct Tries { int mask = 0; std::string first; };   // the kinds tried (bit per kind), the first refusal's words
  std::map<std::string, Tries> tried_;      // room code -> its tries (mismatch_next)
  void remember_kind(const std::string& code, int kind) {
    if (room_kinds_.size() >= 32 && !room_kinds_.count(code)) room_kinds_.clear();
    room_kinds_[code] = kind;
    tried_.erase(code);
  }
  // A join whose invite named its mode (invite_mode_kind; -1 changes nothing): that kind goes first and the join
  // counts as hinted, so a refusal means another build or remix, not another kind, while the room's own kind
  // refusal can still reroute it (mismatch_reroutes, mismatch_next). Not strict: the room has the final word.
  // The size follows the mode, which is what the menu reads for a Friends lobby (join_players 4) or a 1v1 room;
  // a Teams invite keeps the page's size (join_doubles decides it). mutex_ held.
  // An arena invite is strict: its room is only ever joined as the arena's (never rerouted, players 8).
  void invite_mode_locked(int kind) {
    if (kind == kRoomArena) {
      pending_first_ = kRoomArena;
      pending_hinted_ = pending_strict_ = true;
      pending_players_ = kArenaSeats;
      return;
    }
    if (kind < 0 || kind > 2) return;
    pending_first_ = kind;
    pending_hinted_ = true;
    if (kind == kRoomFriends4) pending_players_ = 4;
    else if (kind == kRoom1v1) pending_players_ = 2;
  }
  void requeue(const std::string& code, int kind) {
    retired_.erase(code);
    pending_ = code;
    pending_players_ = kind == kRoomFriends4 ? 4 : 2;
    pending_doubles_ = kind == kRoomDoubles;
  }
  uint64_t toast_until_ = 0, full_return_until_ = 0;
  std::string inviter_, inviter_room_;
  std::string message_, message_id_;
  uint64_t message_until_ = 0;
};
inline Events events;
}  // namespace yougame::friends
