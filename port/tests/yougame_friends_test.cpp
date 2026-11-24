// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_friends.h"
#include "yougame_parity.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <regex>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main() {
  using namespace yougame::friends;
  const json expected = {
    {"type", "mpTicket"}, {"players", 2}, {"mode", "singles"},
    {"ranked", false}, {"room", "abcd2345"},
    {"lobby", {{"version", 1}, {"minPlayers", 2}, {"maxLocalPlayers", 1}, {"compatibility", "mu-build"}}}
  };
  CHECK(ticket("mu-build", "abcd2345") == expected);
  CHECK(!ticket("mu-build", "abcd2345").count("queue"));
  CHECK(!ticket("mu-build", "abcd2345").count("direct"));
  CHECK(!ticket("mu-build", "abcd2345").count("afterMatch"));
  CHECK(picker() == json({{"type", "mpFriend"}, {"mode", "singles"}, {"players", 2}}));
  CHECK(!picker().count("username"));
  CHECK(version_request(picker()));
  CHECK(version_request(expected));
  for (const auto& ordinary : {
      json{{"type", "mpTicket"}, {"mode", "singles"}, {"players", 2}, {"ranked", false}},
      json{{"type", "mpTicket"}, {"mode", "singles"}, {"players", 2}, {"ranked", true}},
      json{{"type", "mpTicket"}, {"mode", "singles"}, {"players", 2}, {"direct", "ABC#1"}},
      team_ticket("mu-build", "mutabc123"), json{{"type", "mpRank"}}})
    CHECK(!version_request(ordinary));
  CHECK(expire_joined(kJoined, 100, 100) == kJoined);
  CHECK(expire_joined(kJoined, 100, 10099) == kJoined);
  CHECK(expire_joined(kJoined, 100, 10100).empty());
  CHECK(expire_joined(kJoined, 100, 50000).empty());
  for (const char* status : {kWaiting, kFailed, kVersion, "offline", "playing opponent"})
    CHECK(expire_joined(status, 100, 50000) == status);
  CHECK(kPickerTimeout == 600000);
  {
    std::string code;
    // What the game's field holds for LAIN#504: ASCII with the full-width # (0x81 0x94).
    CHECK(direct_code_from_game(std::string("LAIN\x81\x94" "504\0\0\0", 10), &code) && code == "LAIN#504");
    // Full-width letters, digits, # and spaces from the on-screen keyboard; lower case folds up.
    CHECK(direct_code_from_game("\x82\x6B\x82\x60\x82\x68\x82\x6D\x81\x94\x82\x54\x82\x4F\x82\x53", &code) && code == "LAIN#504");
    CHECK(direct_code_from_game("lain\x81\x40#504", &code) && code == "LAIN#504");
    CHECK(direct_code_from_game("\x82\x81\x82\x9A#1", &code) && code == "AZ#1");
    CHECK(direct_code_from_game("", &code) && code.empty());
    // The raw field is what broke the request; the folded code serializes.
    bool threw = false;
    try { (void)json({{"direct", std::string("ABC\x81\x94" "1")}}).dump(); } catch (const json::type_error&) { threw = true; }
    CHECK(threw);
    CHECK(direct_code_from_game("ABC\x81\x94" "1", &code) && json({{"direct", code}}).dump() == "{\"direct\":\"ABC#1\"}");
    // Other Shift-JIS (hiragana), a cut-off pair, or punctuation: refused, never sent.
    code = "keep";
    CHECK(!direct_code_from_game("\x82\xA0#1", &code) && code == "keep");
    CHECK(!direct_code_from_game("ABC\x81", &code));
    CHECK(!direct_code_from_game("AB-C#1", &code));
  }
#if defined(_WIN32) || defined(__linux__)
  CHECK(std::string(yougame::parity::kCaps) == "yg {\"caps\":[\"ready\",\"overlay\",\"join\",\"notice\",\"pads\",\"rumble\",\"chat\",\"settings\",\"view\",\"volume\",\"match\",\"fullscreen\"]}");
#else
  CHECK(std::string(yougame::parity::kCaps) == "yg {\"caps\":[\"ready\",\"overlay\",\"join\",\"notice\",\"pads\",\"rumble\",\"chat\",\"settings\",\"match\",\"fullscreen\"]}");
#endif
  {
    // The match state follows caps once, even when 0, then only changes reach the app.
    static std::vector<std::string> lines;
    lines.clear();
    yougame::parity::Signals signals;
    CHECK(signals.sync_match(1) == -1);   // no app yet: remembered, not written
    signals.start(true, [](const char* line) { lines.push_back(line); });
    CHECK(signals.sync_match(-1) == 1);
    CHECK(lines.size() == 2 && lines[1] == "yg {\"match\":1}");
    CHECK(signals.sync_match(1) == -1 && signals.sync_match(-1) == -1);
    CHECK(signals.sync_match(0) == 0 && lines.back() == "yg {\"match\":0}");
    yougame::parity::Signals fresh;
    fresh.start(true, [](const char* line) { lines.push_back(line); });
    CHECK(fresh.sync_match(-1) == 0 && lines.back() == "yg {\"match\":0}");   // 0 is still sent once
    CHECK(fresh.sync_match(0) == -1);
  }

  for (int mode : {0, 1, 2, 3, 4}) {
    std::string code = "stale";
    CHECK(search_code(mode, "lain#504", &code) && code == "LAIN#504");
    CHECK(search_code(mode, "", &code) && code.empty());
    code = "stale";
    const bool expected = mode != 2 && mode != 3;
    CHECK(search_code(mode, "\x83\x65", &code) == expected);
    if (expected) CHECK(code.empty());
    CHECK(search_code(mode, "AB-C#1", &code) == expected);
    if (expected) CHECK(code.empty());
  }

  {
    std::string code, room;
    CHECK(team_room_code("LAIN#504", &room) && room == "mutlain504");
    CHECK(search_code(3, "\x82\x6B\x82\x60\x82\x68\x82\x6D\x81\x94\x82\x54\x82\x4F\x82\x53", &code));
    CHECK(team_room_code(code, &room) && room == "mutlain504");
    const json expected_team = {
      {"type", "mpTicket"}, {"players", 4}, {"mode", "teams"}, {"room", room}, {"ranked", false},
      {"lobby", {{"version", 1}, {"minPlayers", 4}, {"maxLocalPlayers", 1}, {"compatibility", "mu-build"}}}
    };
    CHECK(team_ticket("mu-build", room) == expected_team);
    CHECK(team_room_code("EC", &room) && room == "mutec0");
    CHECK(team_room_code("ec#", &room) && room == "mutec0");
    for (const char* bad : {"", "A", "ABCDEFGHIJ", "ABC-123", "ABC/123", "ABC 123", "###"})
      CHECK(!team_room_code(bad, &room));
    for (int length = 3; length <= 9; ++length) {
      for (char c : std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")) {
        CHECK(team_room_code(std::string(length, c) + "#", &room));
        CHECK(std::regex_match(room, std::regex("^[a-z0-9]{6,12}$")));
        CHECK(team_ticket("mu-build", room).at("room") == room);
      }
    }
    for (const char* bad : {"", "mutab", "mutabcdefghij", "mutABC123", "mutabc/12", "xxxabc123"}) {
      try { team_ticket("mu-build", bad); CHECK(false); }
      catch (const std::runtime_error& e) { CHECK(std::string(e.what()) == kTeamCodeError); }
    }
    CHECK(std::string(kTeamWaiting) == "Waiting for your team...");
  }

  // Exercise the same picker -> ticket path used by matchmaking, with no room opened
  // by a failed picker and no ticket ever sent with an empty room.
  std::vector<json> messages;
  std::vector<int> timeouts;
  json request;
  Events own;
  const auto send = [&](const json& q, int timeout) -> json {
    messages.push_back(q); timeouts.push_back(timeout);
    if (q.at("type") == "mpFriend") return {{"code", "Ab12"}, {"url", "unused"}, {"to", "Friend"}};
    CHECK(q.at("type") == "mpTicket");
    CHECK(q.at("room") == "Ab12");
    CHECK(!q.count("queue"));
    // The page changing to our own room while the ticket is pending is a no-op.
    CHECK(!own.push({{"type", "join"}, {"room", "Ab12"}}, 1));
    return {{"room", "Ab12"}};
  };
  CHECK(request_ticket("mu-build", "", request, send, [] { return false; },
                       [&](const std::string& code) { own.current_room(code); }) == json({{"room", "Ab12"}}));
  CHECK(messages.size() == 2);
  CHECK(messages[0] == picker());
  CHECK(messages[1] == ticket("mu-build", "Ab12"));
  CHECK(timeouts == std::vector<int>({600000, 30000}));
  CHECK(own.take_join(true).empty());
  messages.clear(); timeouts.clear();
  request_ticket("mu-build", "Ab12", request, send, [] { return false; },
                 [&](const std::string& code) { own.current_room(code); });
  CHECK(messages.size() == 1 && messages[0] == ticket("mu-build", "Ab12")); // invitee: no picker
  CHECK(timeouts == std::vector<int>({30000}));
  CHECK(own.push({{"type", "join"}, {"room", "ab12"}}, 2)); // case-sensitive
  CHECK(own.take_join(true) == "ab12");
  CHECK(own.push({{"type", "join"}, {"room", "OtherRoom"}}, 3));
  CHECK(!own.push({{"type", "join"}, {"room", "Ab12"}}, 4));
  CHECK(own.take_join(false).empty());
  CHECK(own.take_join(true) == "OtherRoom"); // own push cannot displace a different invite
  own.current_room({}); // stale old-room pushes must not displace the next invite
  CHECK(!own.push({{"type", "join"}, {"room", "Ab12"}}, 5));
  CHECK(own.push({{"type", "join"}, {"room", "NewRoom"}}, 6));
  CHECK(!own.push({{"type", "join"}, {"room", "Ab12"}}, 7));
  CHECK(own.take_join(true) == "NewRoom");
  // A fresh invite can revive a retired room, but cannot replace another queued invite.
  CHECK(own.push({{"type","notice"},{"kind","invite"},{"from","Friend"},{"text","Play again"},{"room","Ab12"}},8));
  CHECK(own.push({{"type","join"},{"room","NewerRoom"}},9));
  CHECK(!own.push({{"type","join"},{"room","Ab12"}},10));
  CHECK(own.take_join(true) == "NewerRoom");
  CHECK(!own.push({{"type","join"},{"room","Ab12"}},11)); // fresh acceptance was consumed
  CHECK(own.push({{"type","notice"},{"kind","invite"},{"from","Friend"},{"text","Play again"},{"room","Ab12"}},12));
  CHECK(own.push({{"type","join"},{"room","Ab12"}},13));
  CHECK(own.take_join(true) == "Ab12");
  // A join push carrying an accepted invite's id is the acceptance itself: a new id rejoins a
  // room the player left; the same id again, a bare join, or a pending invite never does.
  own.current_room("Ab12"); own.current_room({});                 // played Ab12, then left it
  CHECK(!own.push({{"type","join"},{"room","Ab12"}},13));         // bare: still dropped
  CHECK(own.push({{"type","join"},{"room","Ab12"},{"invite","inv-1"}},13));
  CHECK(own.take_join(true) == "Ab12");
  own.current_room("Ab12"); own.current_room({});                 // left again
  CHECK(!own.push({{"type","join"},{"room","Ab12"},{"invite","inv-1"}},13)); // same id: no rejoin
  CHECK(own.push({{"type","join"},{"room","Queued"}},13));
  CHECK(!own.push({{"type","join"},{"room","Ab12"},{"invite","inv-2"}},13)); // never displaces a queued join
  CHECK(own.take_join(true) == "Queued");
  CHECK(own.push({{"type","join"},{"room","Ab12"},{"invite","inv-2"}},13));  // idle: the new id rejoins
  CHECK(own.take_join(true) == "Ab12");
  // The invite's room size (a hint for the first try): 4, or the Friends lobby's compatibility key; none
  // said is 0. The room decides (room_kind below).
  CHECK(own.push({{"type","join"},{"room","Size4Room"},{"players",4}},14));
  CHECK(own.take_join(true) == "Size4Room" && own.join_players() == 4);
  CHECK(own.push({{"type","join"},{"room","Size0Room"}},15));
  CHECK(own.take_join(true) == "Size0Room" && own.join_players() == 0 && !own.join_doubles());   // bare: 1v1 first
  CHECK(own.push({{"type","join"},{"room","CompatRoom"},{"compatibility","mu:friends-lobby-1"}},16));
  CHECK(own.take_join(true) == "CompatRoom" && own.join_players() == 4);
  CHECK(own.push({{"type","join"},{"room","OtherCompat"},{"compatibility","mu-0.9.105"}},17));
  CHECK(own.take_join(true) == "OtherCompat" && own.join_players() == 0);
  CHECK(own.push({{"type","join"},{"room","Size2Room"},{"players",2},{"compatibility","friends-lobby-1"}},18));
  CHECK(own.take_join(true) == "Size2Room" && own.join_players() == 2);   // the page's size wins
  // The room decides lobby or 1v1: nothing heard is -1; a room the search joined the wrong way queues its
  // join again with its kind (even a room retired or tracked as current), and its kind stays known.
  CHECK(own.room_kind("Size0Room") == -1);
  own.room_decided("Size0Room", true);                 // an invite with no size, into a Friends lobby room
  CHECK(own.room_kind("Size0Room") == 1);
  CHECK(own.take_join(true) == "Size0Room" && own.join_players() == 4);
  own.room_decided("Size4Room", false);                // said 4, but the room is a 1v1 friend room
  CHECK(own.take_join(true) == "Size4Room" && own.join_players() == 2 && own.room_kind("Size4Room") == 0);
  own.room_seen("Size2Room", false);                   // seen as it was joined: recorded, nothing queued
  CHECK(own.room_kind("Size2Room") == 0 && own.take_join(true).empty());
  own.room_decided("", true);                          // no room: nothing
  CHECK(own.take_join(true).empty());
  // lobby_mismatch is also a different build's refusal: a 1v1 invite that said players:2 (or carried any
  // compatibility key) fails with it and is never tried the other way; only an invite that said nothing is.
  CHECK(own.push({{"type","join"},{"room","Mm2Room"},{"players",2}},19));
  CHECK(own.take_join(true) == "Mm2Room" && !own.mismatch_reroutes("Mm2Room"));
  CHECK(own.push({{"type","join"},{"room","MmKeyRoom"},{"compatibility","mu-0.9.105"}},20));
  CHECK(own.take_join(true) == "MmKeyRoom" && own.join_players() == 0 && !own.mismatch_reroutes("MmKeyRoom"));
  CHECK(own.push({{"type","join"},{"room","Mm4Room"},{"compatibility","mu:friends-lobby-1"}},21));
  CHECK(own.take_join(true) == "Mm4Room" && !own.mismatch_reroutes("Mm4Room"));
  CHECK(own.push({{"type","join"},{"room","MmBareRoom"}},22));
  CHECK(own.take_join(true) == "MmBareRoom" && own.mismatch_reroutes("MmBareRoom"));
  own.room_decided("MmBareRoom", true);                // rerouted once: known now, and the requeue is hinted
  CHECK(own.take_join(true) == "MmBareRoom" && !own.mismatch_reroutes("MmBareRoom"));
  CHECK(!own.mismatch_reroutes(""));
  {
    yougame::friends::Events many;                     // bounded: the 33rd room starts the record over
    for (int i = 0; i < 33; ++i) many.room_seen("Room" + std::to_string(i), true);
    CHECK(many.room_kind("Room32") == 1 && many.room_kind("Room0") == -1);
  }
  own.current_room("Ab12"); // discard an own-room push queued just before ticket request
  CHECK(own.take_join(true).empty());
  own.hello({{"room", "Ab12"}});
  CHECK(own.take_join(true).empty());

  for (const auto& code : {"Ab12", "abcdefgh", "AbCd0123456789xy"})
    CHECK(picker_room({{"code", code}}) == code);
  for (const auto& bad : {json(), json::array(), json(1), json("abcd"), json{{"room", "abcdefgh"}},
       json{{"code", ""}}, json{{"code", "abc"}}, json{{"code", std::string(17, 'a')}},
       json{{"code", 42}}, json{{"code", "abcd/efg"}}, json{{"code", "abc\n2345"}},
       json{{"code", "abcd efgh"}}, json{{"code", "éabc"}}}) {
    CHECK(picker_room(bad).empty());
    int sent = 0, started = 0;
    try {
      request_ticket("mu-build", "", request,
          [&](const json& q, int ms) { CHECK(q == picker()); CHECK(ms == 600000); ++sent; return bad; },
          [] { return false; }, [&](const std::string&) { ++started; });
      CHECK(false);
    } catch (const std::runtime_error&) {}
    CHECK(sent == 1 && started == 0);
  }
  for (const auto& code : {"", "abc", "abcd/efg", "abcdefghijklmnopq"}) {
    try { ticket("mu-build", code); CHECK(false); } catch (const std::runtime_error&) {}
  }
  for (bool page_error : {false, true}) {
    int sent = 0;
    bool cancelled = false;
    try {
      request_ticket("mu-build", "", request,
          [&](const json& q, int) -> json {
            CHECK(q == picker()); ++sent;
            if (page_error) throw std::runtime_error("Picker cancelled");
            cancelled = true; return {{"code", "abcd2345"}};
          }, [&] { return cancelled; }, [&](const std::string&) { CHECK(false); });
      CHECK(false);
    } catch (const std::runtime_error&) {}
    CHECK(sent == 1);
  }

  CHECK(kFirstCssWinner == 1);
  CHECK(native_ready(true, 2, "", ""));
  CHECK(!native_ready(true, 2, "", "abcd2345"));
  CHECK(native_ready(false, 2, "", ""));
  CHECK(native_ready(true, 2, "NAME#123", ""));
  for (int mode : {0, 1, 3}) CHECK(native_ready(true, mode, "", ""));
  CHECK(menu_safe(2, 0)); CHECK(menu_safe(2, 1));
  CHECK(menu_safe(1, 0));
  CHECK(menu_safe(8, 0));
  CHECK(menu_safe(8, 1));
  CHECK(menu_safe(8, 3));
  for (int minor : {2, 4}) CHECK(!menu_safe(8, minor));
  for (int major : {0, 2, 3, 0x1C, 0x2B}) CHECK(!menu_safe(major, 2));

  Events launch;
  launch.hello({{"room", "abcd2345"}});
  CHECK(launch.take_join(false).empty()); // local match: leave it pending
  CHECK(launch.take_join(true) == "abcd2345");
  CHECK(launch.take_join(true).empty());
  launch.hello({{"room", "abcd2345"}}); // identity refresh must not replay the invite
  CHECK(launch.take_join(true).empty());
  Events absent;
  absent.hello(json::object());
  CHECK(absent.take_join(true).empty());
  absent.hello({{"room", "abcd2345"}});
  CHECK(absent.take_join(true).empty());

  const json join = {{"type", "join"}, {"room", "abcd2345"}};
  const json notice = {{"type", "notice"}, {"kind", "invite"}, {"from", "A Friend"}, {"text", "Play with me"}};
  Events running;
  CHECK(running.push(join, 100));
  running.hello({{"room", "zzzz2345"}}); // a late hello cannot supersede a running-game push
  CHECK(running.take_join(true) == "abcd2345");
  CHECK(running.take_join(true).empty());
  CHECK(running.push(join, 100));
  CHECK(running.push({{"type", "join"}, {"room", "efgh6789"}}, 101));
  CHECK(running.take_join(false).empty());
  CHECK(running.take_join(true) == "efgh6789"); // latest accepted invite wins
  CHECK(running.push(notice, 100));
  CHECK(running.toast(100) == "A Friend Play with me");
  CHECK(!running.toast(6099).empty());
  CHECK(running.toast(6100).empty());
  CHECK(running.take_join(true).empty()); // notices never accept invites

  for (const auto& bad : {json(), json::array(), json(1), json("join"),
       json{{"type", 42}}, json{{"type", "join"}}, json{{"type", "join"}, {"room", 42}},
       json{{"type", "join"}, {"room", "abc"}},
       json{{"type", "join"}, {"room", std::string(10000, 'a')}},
       json{{"type", "join"}, {"room", "abc\n2345"}},
       json{{"type", "join"}, {"room", "abc/2345"}}}) {
    Events e;
    CHECK(!e.push(bad, 100));
    e.hello(bad);
    CHECK(e.take_join(true).empty());
    CHECK(e.toast(100).empty());
  }
  for (const char* field : {"type", "kind", "from", "text"}) {
    for (const json& bad : {json(), json(1), json::array(), json::object(), json(true), json("")}) {
      auto n = notice; n[field] = bad;
      CHECK(!running.push(n, 200));
    }
    auto n = notice; n.erase(field);
    CHECK(!running.push(n, 200));
  }
  for (const char* field : {"from", "text"}) {
    auto n = notice; n[field] = std::string(10000, 'x');
    CHECK(!running.push(n, 200));
    n[field] = "bad\nline";
    CHECK(!running.push(n, 200));
  }
  CHECK(running.toast(200) == "A Friend Play with me");

  const auto repeated = [](const std::string& cp, int count) {
    std::string s; while (count-- > 0) s += cp; return s;
  };
  Events unicode;
  auto localized = notice;
  localized["from"] = repeated("界", 40);
  localized["text"] = "invited you to play. Shift+Tab to join";
  CHECK(unicode.push(localized, 1));
  CHECK(unicode.toast(1) == repeated("界", 40) + " invited you to play. Shift+Tab to join");
  localized["from"] = repeated("界", 41);
  CHECK(!unicode.push(localized, 2));
  for (const auto& cp : {"é", "界", "😀"}) {
    localized["from"] = repeated(cp, 40);
    localized["text"] = repeated(cp, 80);
    CHECK(unicode.push(localized, 3));
    CHECK(unicode.toast(3) == repeated(cp, 40) + " " + repeated(cp, 80));
    localized["text"] = repeated(cp, 81);
    CHECK(!unicode.push(localized, 4));
  }
  for (const auto& bad : {std::string("bad\tname"), std::string("bad\x7f"), std::string("bad\xc2\x85"),
       std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"),
       std::string("\xe7\x95"), std::string("\x80")}) {
    for (const char* field : {"from", "text"}) {
      auto n = notice; n[field] = bad;
      CHECK(!unicode.push(n, 5));
    }
  }
  Events presentation;
  presentation.online_menu(true, true);
  CHECK(!presentation.push({{"type", "hide"}}, 1));
  CHECK(presentation.friend_entry()); // embed hide/show never changes Friends state
  presentation.picker_error("Picker cancelled");
  CHECK(presentation.picker_error() == "Picker cancelled");
  presentation.request_host();
  CHECK(presentation.picker_error().empty());

  Events host;
  host.request_host();
  CHECK(!host.take_host());
  host.online_menu(true, false);
  host.request_host();
  CHECK(!host.take_host());
  host.online_menu(true, true);
  host.request_host();
  CHECK(host.take_host());
  CHECK(!host.take_host());
  host.request_host();
  host.online_menu(false); // Back/cancel must not host a room later
  CHECK(!host.take_host());

  // A full room. The invite route's code wins; without one (the app may drop it) only its words decide.
  const std::string site = "This room is full. Invite again when a seat opens.";
  CHECK(invite_room_full("room_full", site) == site);
  CHECK(invite_room_full("refused", site) == site && invite_room_full("", site) == site);
  CHECK(invite_room_full("room_full", "") == kRoomFull && invite_room_full("room_full", kRequestFailed) == kRoomFull);
  CHECK(invite_room_full("seat_mismatch", site).empty());   // another code: not a full room
  CHECK(invite_room_full("room_full", std::string(300, 'x')).size() == 120);
  CHECK(invite_room_full("refused", "Your friend is offline").empty() && invite_room_full("", "Cancelled").empty());
  CHECK(room_full_line("Ada") == "Ada's room is full." && room_full_line("") == "This room is full.");
  try { throw RoomFull(site); } catch (const std::runtime_error& e) { CHECK(e.what() == site); }
  Events guest;
  CHECK(guest.inviter("abcdef").empty());
  CHECK(guest.push({{"type", "notice"}, {"kind", "invite"}, {"from", "Ada"}, {"text", "invited you"}, {"room", "abcdef"}}, 1));
  guest.current_room("abcdef");   // accepting clears the invite, never who sent it
  CHECK(guest.inviter("abcdef") == "Ada" && guest.inviter("zzzzzz").empty() && guest.inviter("").empty());
  guest.room_full(room_full_line(guest.inviter("abcdef")), 1000);
  CHECK(guest.toast(1000) == "Ada's room is full." && guest.toast(7000).empty());
  CHECK(!guest.take_full_return(false, 2000));   // waits for the online character select
  CHECK(guest.take_full_return(true, 3000) && !guest.take_full_return(true, 3001));   // once
  guest.room_full(kRoomFull, 10000);
  CHECK(!guest.take_full_return(true, 25000) && !guest.take_full_return(true, 25001));   // expired, never late
  std::puts("YouGame friends passed");
}
