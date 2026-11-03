// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_friends.h"
#include "yougame_parity.h"
#include <cstdio>
#include <cstdlib>
#include <string>
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
  CHECK(std::string(yougame::parity::kCaps) == "yg {\"caps\":[\"ready\",\"overlay\",\"join\",\"notice\",\"pads\",\"view\",\"volume\"]}");
#else
  CHECK(std::string(yougame::parity::kCaps) == "yg {\"caps\":[\"ready\",\"overlay\",\"join\",\"notice\",\"pads\"]}");
#endif

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
  own.current_room({}); // cleanup permits joining that room again
  CHECK(own.push({{"type", "join"}, {"room", "Ab12"}}, 5));
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
  CHECK(!native_ready(true, 2, "", ""));
  CHECK(!native_ready(true, 2, "", "abcd2345"));
  CHECK(native_ready(false, 2, "", ""));
  CHECK(native_ready(true, 2, "NAME#123", ""));
  for (int mode : {0, 1, 3}) CHECK(native_ready(true, mode, "", ""));
  CHECK(empty_code(true, 2, ""));
  CHECK(!empty_code(false, 2, ""));
  CHECK(!empty_code(true, 2, "NAME#123"));
  for (int mode : {0, 1, 3, 4, -1}) CHECK(!empty_code(true, mode, ""));
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
  presentation.code_screen(true);
  presentation.code_empty(true);
  CHECK(!presentation.push({{"type", "hide"}}, 1));
  CHECK(presentation.code_screen()); // embed hide/show never changes Friends state
  presentation.picker_error("Picker cancelled");
  CHECK(presentation.picker_error() == "Picker cancelled");
  presentation.request_host();
  CHECK(presentation.picker_error().empty());

  Events host;
  host.request_host();
  CHECK(!host.take_host());
  host.code_screen(true);
  host.code_empty(false);
  host.request_host();
  CHECK(!host.take_host());
  host.code_empty(true);
  host.request_host();
  CHECK(host.take_host());
  CHECK(!host.take_host());
  host.request_host();
  host.code_screen(false); // Back/cancel must not host a room later
  CHECK(!host.take_host());
  std::puts("YouGame friends passed");
}
