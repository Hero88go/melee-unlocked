// Match requests between two peer lobbies in one process, end to end over loopback UDP: discovery,
// delivery, acceptance, refusals with their reasons on both sides, crossed requests, one-way packet
// loss, clock skew, friends by code while hidden, the 0.8.5 mod fields (badges, "open to", custom
// ISOs) and an Akaneia match's disc from the Mods folder scan. Private chat too: request, accept,
// decline, block, timeout, messages reaching only the room's other player, launchers from before
// private chat, and the message limits. No DHT (each lobby is told the other's address), no window,
// no focus: ctest runs it.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <winsock2.h>
#include "launcher_lobby_p2p.h"
#include "launcher_lang.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#ifdef MELEE_NO_SLIPPI
#include <monocypher.h>   // the peer-to-peer cases check the seed: BLAKE2b over the request id
#include <cstdio>
#include <vector>
#endif

using Json = nlohmann::json;
using launcher::lobby::PeerLobby;
namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
  std::cout << (ok ? "ok    " : "FAIL  ") << what << std::endl;
  if (!ok) ++failures;
}

Json profile(const char* name, const char* code, const char* build, int main, bool ready = true) {
  return {{"name", name}, {"code", code}, {"location", ""}, {"mains", Json::array({main})}, {"build", build}, {"ready", ready}};
}

struct Pair {
  fs::path dir;
  std::unique_ptr<PeerLobby> a, b;
};

// A listens first; B is given A's address, sends it a hello, and A answers with its own.
Pair make_pair(const std::string& name, const Json& pa, const Json& pb, bool public_lobby = true, long long b_clock = 0) {
  Pair pair;
  pair.dir = fs::temp_directory_path() / ("mu-p2p-" + std::to_string(GetCurrentProcessId()) + "-" + name);
  std::error_code ec; fs::remove_all(pair.dir, ec); fs::create_directories(pair.dir, ec);
  launcher::lobby::PeerTestOptions options; options.no_dht = true;
  pair.a = std::make_unique<PeerLobby>((pair.dir / "a").u8string(), "", 0, options);
  launcher::lobby::PeerTestOptions b_options = options; b_options.clock_offset = b_clock;
  pair.b = std::make_unique<PeerLobby>((pair.dir / "b").u8string(), "127.0.0.1:" + std::to_string(pair.a->port()), 0, b_options);
  if (public_lobby) { pair.a->join(pa); pair.b->join(pb); }
  else { pair.a->update_profile(pa); pair.b->update_profile(pb); }
  return pair;
}

// Ticks both lobbies (as the launcher's worker does, a little faster) until done() or the time is up.
bool pump(PeerLobby& a, PeerLobby& b, int ms, const std::function<bool()>& done = {}) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    a.tick(); b.tick();
    if (done && done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return done ? done() : true;
}

Json notice(const PeerLobby& p, const std::string& key) {
  const Json state = p.state();   // keep the snapshot alive: a range-for over a temporary's member dangles
  for (const auto& n : state["notices"]) if (n.value("key", std::string()) == key) return n;
  return Json();
}
bool has_notice(const PeerLobby& p, const std::string& key) { return !notice(p, key).is_null(); }

bool see_each_other(Pair& pair) {
  return pump(*pair.a, *pair.b, 5000, [&] {
    return pair.a->state()["players"].size() == 1 && pair.b->state()["players"].size() == 1;
  });
}
// Hidden players do not list each other; they only know each other as peers.
bool know_each_other(Pair& pair) {
  const bool known = pump(*pair.a, *pair.b, 5000, [&] {
    return pair.a->state()["network"]["peers"] == 1 && pair.b->state()["network"]["peers"] == 1;
  });
  pump(*pair.a, *pair.b, 300);   // their first presence messages
  return known;
}

std::string error_of(const std::function<void()>& action) {
  try { action(); } catch (const std::exception& ex) { return ex.what(); }
  return {};
}

void accept_flow() {
  std::cout << "-- A requests B, B accepts" << std::endl;
  auto pair = make_pair("accept", profile("Alpha", "ALPH#101", "t:recomp", 20), profile("Beta", "BETA#202", "t:recomp", 9));
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "both players see each other in the public lobby");
  a.command("request", {{"target", b.id()}});
  pump(a, b, 3000, [&] { return b.state()["requests"].size() == 1 && has_notice(a, "lobby.delivered"); });
  pump(a, b, 1200);   // resends must not add a second request
  const Json requests = b.state()["requests"];
  check(requests.size() == 1 && requests[0]["from"] == a.id() && requests[0]["to"] == b.id() &&
        requests[0]["state"] == "pending" && requests[0]["mode"] == "vanilla", "B holds exactly one pending request from A");
  check(requests.size() == 1 && requests[0]["peer"]["name"] == "Alpha", "the request shows who sent it");
  check(has_notice(a, "lobby.delivered"), "A hears that the request was delivered");
  check(a.state()["requests"].size() == 1 && a.state()["requests"][0].value("delivered", false), "A's request is marked delivered");
  if (requests.size() != 1) return;
  b.command("accept", {{"request", requests[0]["id"]}});
  Json la, lb; bool got_a = false, got_b = false;
  pump(a, b, 5000, [&] {
    if (!got_a) got_a = a.take_launch(la);
    if (!got_b) got_b = b.take_launch(lb);
    return got_a && got_b;
  });
  check(got_a && la["code"] == "BETA#202" && la["character"] == 20 && la["build"] == "t:recomp" && la["mode"] == "vanilla" &&
        la["name"] == "Beta", "A launches once: Beta's code, A's own main, A's build");
  check(got_b && lb["code"] == "ALPH#101" && lb["character"] == 9 && lb["build"] == "t:recomp" && lb["mode"] == "vanilla" &&
        lb["name"] == "Alpha", "B launches once: Alpha's code, B's own main, B's build");
  check(got_a && got_b && la["request"] == lb["request"], "both launches are the same match");
  pump(a, b, 1500);
  Json extra;
  check(!a.take_launch(extra) && !b.take_launch(extra), "no second launch on either side");
}

// The two Game Builds play each other: the same version on Source Port and on Static Recomp is a match.
void cross_build_flow() {
  std::cout << "-- A (Source Port) requests B (Static Recomp), same version" << std::endl;
  auto pair = make_pair("cross-build", profile("Alpha", "ALPH#101", "0.8.5:source", 20), profile("Beta", "BETA#202", "0.8.5:recomp", 9));
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "cross build: both players see each other");
  const std::string sent = error_of([&] { a.command("request", {{"target", b.id()}}); });
  check(sent.empty(), "cross build: the request is sent: " + sent);
  pump(a, b, 3000, [&] { return b.state()["requests"].size() == 1; });
  const Json requests = b.state()["requests"];
  check(requests.size() == 1 && requests[0]["state"] == "pending", "cross build: B holds the request, not a refusal");
  check(!has_notice(a, "lobby.declined.build") && !has_notice(b, "lobby.refused.build"), "cross build: nobody is told to change Game Build");
  if (requests.size() != 1) return;
  b.command("accept", {{"request", requests[0]["id"]}});
  Json la, lb; bool got_a = false, got_b = false;
  pump(a, b, 5000, [&] {
    if (!got_a) got_a = a.take_launch(la);
    if (!got_b) got_b = b.take_launch(lb);
    return got_a && got_b;
  });
  check(got_a && la["build"] == "0.8.5:source" && la["code"] == "BETA#202", "cross build: A launches its own build against Beta");
  check(got_b && lb["build"] == "0.8.5:recomp" && lb["code"] == "ALPH#101", "cross build: B launches its own build against Alpha");
}

// A sends while it still sees B as able to play; B changes before the request arrives. Both sides
// must then say why, each from its own side.
void refusal(const std::string& name, const std::function<void(PeerLobby&, Json&)>& spoil,
             const std::string& declined_key, const std::string& refused_key) {
  std::cout << "-- refused: " << name << std::endl;
  Json pa = profile("Alpha", "ALPH#101", "0.8.5:recomp", 2), pb = profile("Beta", "BETA#202", "0.8.5:recomp", 2);
  auto pair = make_pair("refuse-" + name, pa, pb);
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), name + ": discovery");
  const std::string sent = error_of([&] { a.command("request", {{"target", b.id()}}); });
  check(sent.empty(), name + ": A sends (it still sees B as ready): " + sent);
  spoil(b, pb);
  const bool told = pump(a, b, 5000, [&] { return has_notice(a, declined_key) && has_notice(b, refused_key); });
  check(told, name + ": the sender gets " + declined_key + " and the receiver gets " + refused_key);
  if (told) {
    std::cout << "      A: " << notice(a, declined_key)["text"].get<std::string>() << std::endl;
    std::cout << "      B: " << notice(b, refused_key)["text"].get<std::string>() << std::endl;
  }
  pump(a, b, 300);
  check(a.state()["requests"].empty() && b.state()["requests"].empty(), name + ": no request is left behind");
}

void refusals() {
  refusal("not ready", [](PeerLobby& b, Json& pb) { pb["ready"] = false; b.update_profile(pb); },
          "lobby.declined.not_ready", "lobby.refused.not_ready");
  refusal("busy", [](PeerLobby& b, Json&) { b.presence({{"status", "In game"}, {"stocks", Json::array()}}); },
          "lobby.declined.busy", "lobby.refused.busy");
  refusal("older version", [](PeerLobby& b, Json& pb) { pb["build"] = "0.8.1:recomp"; b.update_profile(pb); },
          "lobby.declined.update_them", "lobby.refused.update_you");

  std::cout << "-- version skew is named before sending" << std::endl;
  auto pair = make_pair("skew-version", profile("Alpha", "ALPH#101", "0.8.5:recomp", 2), profile("Beta", "BETA#202", "0.8.1:recomp", 2));
  check(see_each_other(pair), "an 0.8.1 profile is still discovered");
  const auto error = error_of([&] { pair.a->command("request", {{"target", pair.b->id()}}); });
  check(error.find("0.8.5") != std::string::npos && error.find("Beta") != std::string::npos,
        "sending to an older version says who needs to update: " + error);
  const auto reverse = error_of([&] { pair.b->command("request", {{"target", pair.a->id()}}); });
  check(reverse.find("0.8.5") != std::string::npos, "the older side is told to update: " + reverse);
}

void crossed_requests() {
  std::cout << "-- both players send at once" << std::endl;
  auto pair = make_pair("crossed", profile("Alpha", "ALPH#101", "t:recomp", 2), profile("Beta", "BETA#202", "t:recomp", 20));
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "discovery");
  const auto ea = error_of([&] { a.command("request", {{"target", b.id()}}); });
  const auto eb = error_of([&] { b.command("request", {{"target", a.id()}}); });
  check(ea.empty() && eb.empty(), "both requests leave");
  pump(a, b, 3000);
  const Json ra = a.state()["requests"], rb = b.state()["requests"];
  const std::string higher = a.id() < b.id() ? b.id() : a.id();
  check(ra.size() == 1 && rb.size() == 1 && ra[0]["id"] == rb[0]["id"], "exactly one request survives, the same on both sides");
  check(ra.size() == 1 && ra[0]["from"] == higher && ra[0]["state"] == "pending", "the surviving request is the one both agree on");
  if (ra.size() != 1 || rb.size() != 1) return;
  PeerLobby& receiver = ra[0]["to"] == a.id() ? a : b;
  receiver.command("accept", {{"request", ra[0]["id"]}});
  Json la, lb; bool got_a = false, got_b = false;
  pump(a, b, 5000, [&] {
    if (!got_a) got_a = a.take_launch(la);
    if (!got_b) got_b = b.take_launch(lb);
    return got_a && got_b;
  });
  check(got_a && got_b && la["request"] == lb["request"], "it is accepted and both launch the same match");
}

void one_way_loss(bool lose_requests) {
  std::cout << (lose_requests ? "-- A's messages never reach B" : "-- B's answers never reach A") << std::endl;
  auto pair = make_pair(lose_requests ? "loss-out" : "loss-in", profile("Alpha", "ALPH#101", "t:recomp", 2),
                        profile("Beta", "BETA#202", "t:recomp", 2));
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "discovery");
  if (lose_requests) a.test_drop(false, true); else a.test_drop(true, false);
  const auto sent_at = std::chrono::steady_clock::now();
  const auto error = error_of([&] { a.command("request", {{"target", b.id()}}); });
  check(error.empty(), "the request is sent");
  const bool told = pump(a, b, 14000, [&] { return has_notice(a, "lobby.unreachable"); });
  const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - sent_at).count();
  check(told && seconds >= 9.0 && seconds <= 12.5, "A is told it cannot reach B after about 10 s (" + std::to_string(seconds) + " s)");
  const Json n = notice(a, "lobby.unreachable");
  check(!n.is_null() && n["args"]["code"] == "BETA#202", "the notice carries B's code for Direct");
  if (!n.is_null()) std::cout << "      A: " << n["text"].get<std::string>() << std::endl;
  if (lose_requests) check(b.state()["requests"].empty(), "B never saw a request");
  else check(b.state()["requests"].size() == 1, "B did get the request (only its answer is lost)");
}

void clock_skew() {
  std::cout << "-- clocks five minutes apart" << std::endl;
  auto pair = make_pair("clock", profile("Alpha", "ALPH#101", "t:recomp", 2), profile("Beta", "BETA#202", "t:recomp", 2), true, 300);
  auto& a = *pair.a; auto& b = *pair.b;
  const bool reported = pump(a, b, 5000, [&] { return has_notice(a, "lobby.clock_skew") && has_notice(b, "lobby.clock_skew"); });
  check(reported, "both players are told their clocks differ");
  check(a.state()["players"].empty() && b.state()["players"].empty(), "and they are (still) not listed to each other");
  const Json n = notice(a, "lobby.clock_skew");
  check(!n.is_null() && n["args"]["minutes"] == "5" && n["args"]["name"] == "Beta", "the notice names the player and the difference");
  if (!n.is_null()) std::cout << "      A: " << n["text"].get<std::string>() << std::endl;
}

void friends_by_code() {
  std::cout << "-- friends by Slippi code, both hidden" << std::endl;
  auto pair = make_pair("friends", profile("Alpha", "ALPH#101", "t:recomp", 2), profile("Beta", "BETA#202", "t:recomp", 2), false);
  auto& a = *pair.a; auto& b = *pair.b;
  check(know_each_other(pair), "hidden players still exchange introductions");
  check(a.state()["players"].empty() && b.state()["players"].empty(), "neither is listed in a public roster");
  check(!error_of([&] { a.command("friend_code", {{"code", "ALPH#101"}}); }).empty(), "adding your own code is refused");
  check(!error_of([&] { a.command("friend_code", {{"code", "not a code"}}); }).empty(), "a malformed code is refused");
  const auto error = error_of([&] { a.command("friend_code", {{"code", " beta#202"}}); });
  check(error.empty(), "A asks for beta#202 (typed in lower case) " + error);
  pump(a, b, 3000, [&] { return b.state()["friend_requests"].size() == 1; });
  const Json incoming = b.state()["friend_requests"];
  check(incoming.size() == 1 && incoming[0]["id"] == a.id() && incoming[0]["code"] == "ALPH#101", "B gets A's friend request, with A's code");
  check(has_notice(b, "lobby.friend.incoming"), "B is told about it");
  b.command("friend_accept", {{"target", a.id()}});
  pump(a, b, 3000, [&] { return a.state()["friends"].size() == 1 && a.state()["friends"][0].value("online", false); });
  const Json friends = a.state()["friends"];
  check(friends.size() == 1 && friends[0]["id"] == b.id() && friends[0]["code"] == "BETA#202", "A stores B as a friend with B's code");
  check(b.state()["friends"].size() == 1 && b.state()["friends"][0]["code"] == "ALPH#101", "B stores A with A's code");
  pump(a, b, 2000);   // friend presence
  const auto sent = error_of([&] { a.command("request", {{"target", b.id()}}); });
  check(sent.empty(), "friends can send a match request while both are hidden: " + sent);
  pump(a, b, 3000, [&] { return b.state()["requests"].size() == 1; });
  const Json requests = b.state()["requests"];
  check(requests.size() == 1, "B gets it");
  if (requests.size() == 1) {
    b.command("accept", {{"request", requests[0]["id"]}});
    Json la, lb; bool got_a = false, got_b = false;
    pump(a, b, 5000, [&] {
      if (!got_a) got_a = a.take_launch(la);
      if (!got_b) got_b = b.take_launch(lb);
      return got_a && got_b;
    });
    check(got_a && got_b && la["code"] == "BETA#202" && lb["code"] == "ALPH#101", "and the match starts on both sides");
  }
  const auto looking = error_of([&] { a.command("friend_code", {{"code", "NOPE#77"}}); });
  check(looking.empty() && a.state()["lookups"].size() == 1 && a.state()["lookups"][0] == "NOPE#77" &&
        has_notice(a, "lobby.friend.looking"), "an unknown code is looked up (on the DHT in the launcher)");
}

void mods() {
  std::cout << "-- mods: badges, open to, custom ISO" << std::endl;
  const std::string hash = "0123456789abcdef";
  Json pa = profile("Alpha", "ALPH#101", "0.8.5:source", 2), pb = profile("Beta", "BETA#202", "0.8.5:recomp", 2);
  pa["has"] = {{"akaneia", "1.0.1"}, {"ace", "2.0"}};
  pa["open"] = Json::array({"vanilla", "akaneia", "custom"});
  pa["iso"] = {{"n", "Summit ISO"}, {"h", hash}};
  pb["has"] = {{"akaneia", "1.0.1"}};
  pb["open"] = Json::array({"vanilla", "akaneia", "ace", "custom"});
  pb["iso"] = {{"n", "My build"}, {"h", hash}};
  auto pair = make_pair("mods", pa, pb);
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "discovery with mod fields");
  const Json seen = b.state()["players"][0];
  check(seen["has"]["akaneia"] == "1.0.1" && seen["has"]["ace"] == "2.0", "installed badges reach the other player");
  check(seen["open"] == pa["open"], "what A is open to reaches B");
  check(seen["iso"]["n"] == "Summit ISO" && seen["iso"]["h"] == hash, "A's custom ISO name and hash reach B");
  const auto modes = launcher::lobby::common_modes(a.state()["self"]["profile"], b.state()["self"]["profile"]);
  // The Game Builds differ and the version is the same: vanilla and the mods both only need the version.
  const std::vector<std::string> expected{"vanilla", "akaneia", "custom:" + hash};
  check(modes == expected, "the common versions are vanilla, Akaneia and the matching custom ISO, not ACE");
  check(launcher::lobby::iso_hash(seen) == launcher::lobby::iso_hash(b.state()["self"]["profile"]), "the same ISO is recognised under two names");
  const auto ace = error_of([&] { a.command("request", {{"target", b.id()}, {"mode", "ace"}}); });
  check(!ace.empty(), "ACE is refused before sending (B does not have it): " + ace);
  const auto sent = error_of([&] { a.command("request", {{"target", b.id()}, {"mode", "akaneia"}}); });
  check(sent.empty(), "an Akaneia request leaves: " + sent);
  pump(a, b, 3000, [&] { return b.state()["requests"].size() == 1; });
  Json requests = b.state()["requests"];
  check(requests.size() == 1 && requests[0]["mode"] == "akaneia", "B receives it as an Akaneia request");
  if (requests.size() == 1) {
    b.command("accept", {{"request", requests[0]["id"]}});
    Json la, lb; bool got_a = false, got_b = false;
    pump(a, b, 5000, [&] {
      if (!got_a) got_a = a.take_launch(la);
      if (!got_b) got_b = b.take_launch(lb);
      return got_a && got_b;
    });
    check(got_a && got_b && la["mode"] == "akaneia" && lb["mode"] == "akaneia", "both launch the Akaneia match");
  }
  // Refused on both sides: B stops being open to Akaneia, then a different version, then another ISO.
  struct Case { const char* name; std::function<void(Json&)> change; std::string mode, code; };
  const std::vector<Case> cases{
    {"not open", [](Json& p) { p["open"] = Json::array({"vanilla"}); }, "akaneia", "mode_closed"},
    {"other mod version", [](Json& p) { p["has"]["akaneia"] = "1.0.2"; }, "akaneia", "mode_version"},
    {"other ISO", [](Json& p) { p["iso"]["h"] = "fedcba9876543210"; }, "custom:" + hash, "mode_hash"},
    {"missing", [](Json& p) { p.erase("has"); }, "akaneia", "mode_missing"},
  };
  for (const auto& c : cases) {
    Json changed = pb;
    auto again = make_pair("mode-" + c.code, pa, pb);
    check(see_each_other(again), std::string(c.name) + ": discovery");
    const auto error = error_of([&] { again.a->command("request", {{"target", again.b->id()}, {"mode", c.mode}}); });
    check(error.empty(), std::string(c.name) + ": the request leaves " + error);
    c.change(changed); again.b->update_profile(changed);
    const bool told = pump(*again.a, *again.b, 5000, [&] {
      return has_notice(*again.a, "lobby.declined." + c.code) && has_notice(*again.b, "lobby.refused." + c.code);
    });
    check(told, std::string(c.name) + ": both players are told why (" + c.code + ")");
    if (told) {
      std::cout << "      A: " << notice(*again.a, "lobby.declined." + c.code)["text"].get<std::string>() << std::endl;
      std::cout << "      B: " << notice(*again.b, "lobby.refused." + c.code)["text"].get<std::string>() << std::endl;
    }
  }

  // Names and garbage.
  check(launcher::lobby::valid_iso_name("Summit ISO") && launcher::lobby::valid_iso_name(std::string(32, 'x')), "a plain name up to 32 characters is fine");
  check(!launcher::lobby::valid_iso_name(std::string(33, 'x')), "33 characters are too many");
  check(!launcher::lobby::valid_iso_name(""), "an empty name is refused");
  check(!launcher::lobby::valid_iso_name("bad\x07name"), "a control character is refused");
  check(!launcher::lobby::valid_iso_name("\xE2\x80\xAEreversed"), "a bidi override is refused");
  check(!launcher::lobby::valid_iso_name("broken \xC3"), "broken UTF-8 is refused");
  std::string wide_name; for (int i = 0; i < 32; ++i) wide_name += "\xE3\x81\x82";
  check(launcher::lobby::valid_iso_name(wide_name), "32 Japanese characters (96 bytes) are fine");
  Json oversize = pa; oversize["iso"]["n"] = std::string(40, 'x');
  check(!error_of([&] { a.update_profile(oversize); }).empty(), "announcing an oversize custom ISO name is refused");
  Json garbage = {{"name", "X"}, {"has", {{"akaneia", "1.0<script>"}}}, {"open", Json::array({"vanilla", "vanilla"})},
                  {"iso", {{"n", "ok"}, {"h", "not-hex"}}}};
  const Json cleaned = launcher::lobby::clean_profile(garbage);
  check(!cleaned.count("has") && !cleaned.count("open") && !cleaned.count("iso") && cleaned["name"] == "X",
        "garbage mod fields from another player are dropped, the rest kept");
  check(launcher::lobby::open_to(Json::object(), "vanilla") && !launcher::lobby::open_to(Json::object(), "akaneia"),
        "a profile without the field (0.8.1) is open to vanilla only");

  // The largest profile a player can announce must still fit one UDP packet (1400 bytes), or the
  // other player would never see them.
  std::cout << "-- largest profile" << std::endl;
  Json big = profile("x", "ABCD#12345", "0.8.5:recomp", 2);
  big["name"] = std::string(32, '"');
  big["location"] = std::string(48, '\\');
  big["mains"] = Json::array({23, 24, 25});
  big["wins"] = 999999; big["losses"] = 999999;
  big["has"] = {{"akaneia", "1234567890abcdef"}, {"ace", "fedcba0987654321"}};
  big["open"] = Json::array({"vanilla", "akaneia", "ace", "custom"});
  big["iso"] = {{"n", wide_name}, {"h", hash}};
  auto huge = make_pair("largest", big, profile("Beta", "BETA#202", "0.8.5:recomp", 2));
  check(see_each_other(huge) && huge.b->state()["players"][0]["iso"]["n"] == wide_name,
        "a worst-case profile is still discovered intact");
}

// The launcher's half of a mod match: the Mods folder scan (detected.json) says which discs this PC
// has, the profile announces them, and an agreed Akaneia match starts from the disc the scan found.
// A disc that is gone gives the notice instead of a launch.
void mod_match_disc() {
  std::cout << "-- an Akaneia match runs the disc from the Mods folder scan" << std::endl;
  const fs::path game = fs::temp_directory_path() / ("mu-p2p-" + std::to_string(GetCurrentProcessId()) + "-game");
  std::error_code ec; fs::remove_all(game, ec); fs::create_directories(game / "Mods" / "Discs", ec);
  const fs::path disc = game / "Mods" / "Discs" / "Akaneia.iso";
  { std::ofstream out(disc, std::ios::binary); out << "stands in for the disc"; }
  // Items as mod_scan.cpp writes them: Akaneia by a relative path, an ACE disc that is gone, and
  // entries the lobby never badges (TM-CE, an unknown m-ex pack, one not supported yet).
  auto item = [](const std::string& path, const char* id, const char* kind, const char* version, const char* status) {
    return Json{{"path", path}, {"id", id}, {"kind", kind}, {"name", id}, {"version", version}, {"hash", ""},
                {"key", id}, {"enabled", true}, {"needs_engine", std::string(kind) == "tmce" ? "source" : "recomp"},
                {"status", status}, {"card", false}, {"restart", false}, {"message", ""}};
  };
  const Json scan = {{"version", 1}, {"engine", "source"}, {"notes", Json::array()}, {"items", Json::array({
      item("Mods/Discs/Akaneia.iso", "akaneia", "mex", "1.0.1", "supported"),
      item((game / "Mods" / "Discs" / "ACE.iso").u8string(), "ace", "mex", "2.0", "untested"),
      item("Mods/Discs/Akaneia.iso", "tmce", "tmce", "1.0", "supported"),
      item("Mods/Discs/Akaneia.iso", "some-pack", "mex", "1", "untested"),
      item("Mods/Discs/Akaneia.iso", "akaneia", "mex", "0.9", "not_supported_yet")})}};
  const auto discs = launcher::lobby::read_mod_scan(scan.dump(), game.u8string());
  check(discs.has == Json{{"akaneia", "1.0.1"}}, "the scan gives Akaneia 1.0.1 and nothing else: " + discs.has.dump());
  check(discs.paths.count("akaneia") && fs::equivalent(fs::u8path(discs.paths.at("akaneia")), disc, ec),
        "the Akaneia disc is found from its relative path");

  // Two players announce what the scan gives them and agree on Akaneia through the real peer code.
  Json pa = profile("Alpha", "ALPH#101", "0.8.5:source", 2), pb = profile("Beta", "BETA#202", "0.8.5:recomp", 9);
  pa["has"] = discs.has; pb["has"] = discs.has;
  pa["open"] = Json::array({"vanilla", "akaneia"}); pb["open"] = Json::array({"vanilla", "akaneia"});
  auto pair = make_pair("disc", pa, pb);
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "discovery with the scanned badge");
  const auto sent = error_of([&] { a.command("request", {{"target", b.id()}, {"mode", "akaneia"}}); });
  check(sent.empty(), "an Akaneia request leaves: " + sent);
  pump(a, b, 3000, [&] { return b.state()["requests"].size() == 1; });
  const Json requests = b.state()["requests"];
  Json la, lb; bool got_a = false, got_b = false;
  if (requests.size() == 1) {
    b.command("accept", {{"request", requests[0]["id"]}});
    pump(a, b, 5000, [&] {
      if (!got_a) got_a = a.take_launch(la);
      if (!got_b) got_b = b.take_launch(lb);
      return got_a && got_b;
    });
  }
  check(got_a && got_b && la["mode"] == "akaneia" && lb["mode"] == "akaneia", "both sides get an Akaneia launch");

  // What the launcher's lobby does with each launch (launcher_lobby.cpp): the disc from the scan.
  std::string path, name;
  for (const Json* launch : {&la, &lb}) {
    const std::string mode = launch->is_object() ? launch->value("mode", std::string()) : std::string();
    const auto problem = launcher::lobby::find_match_disc(mode, discs.paths, "", "", "", path, name);
    check(problem.empty() && name == "Akaneia" && fs::equivalent(fs::u8path(path), disc, ec),
          "the Akaneia match resolves mod_path from the scan: " + path + problem);
  }

  // Missing discs: the notice instead of a launch.
  const std::string notice_text = launcher::lang::tr("lobby.mod_disc_missing");
  const auto no_ace = launcher::lobby::find_match_disc("ace", discs.paths, "", "", "", path, name);
  check(no_ace == notice_text && path.empty(), "an ACE match with no ACE disc on this PC gives the notice: " + no_ace);
  fs::remove(disc, ec);
  const auto rescanned = launcher::lobby::read_mod_scan(scan.dump(), game.u8string());
  check(rescanned.has.empty() && rescanned.paths.empty(), "once the disc is gone the scan offers no Akaneia");
  const auto gone = launcher::lobby::find_match_disc("akaneia", rescanned.paths, "", "", "", path, name);
  check(gone == notice_text && path.empty() && gone.find("Mods") != std::string::npos,
        "the Akaneia match then gives the missing-disc notice: " + gone);

  // Vanilla needs no disc; the custom ISO runs only with the content hash both players chose.
  const std::string hash = "0123456789abcdef", iso = (game / "My build.iso").u8string();
  check(launcher::lobby::find_match_disc("vanilla", {}, "", "", "", path, name).empty() && path.empty(), "vanilla needs no mod disc");
  check(launcher::lobby::find_match_disc("custom:" + hash, {}, hash, iso, "My build", path, name).empty() && path == iso &&
        name == "My build", "the chosen custom ISO runs when its hash matches");
  check(launcher::lobby::find_match_disc("custom:fedcba9876543210", {}, hash, iso, "My build", path, name) == notice_text &&
        path.empty(), "another custom ISO gives the notice");
  check(launcher::lobby::read_mod_scan("not json", game.u8string()).paths.empty() &&
        launcher::lobby::read_mod_scan(R"({"items":[{"kind":5},{"id":"akaneia","kind":"mex","needs_engine":"recomp","path":7}]})",
                                       game.u8string()).paths.empty(),
        "a damaged scan file, or damaged entries, mean no mod discs");
  const auto card = game / "User/GC/Mods/akaneia";
  check(!launcher::lobby::has_game_save(card.u8string()), "a missing mod card is not ready for a lobby handoff");
  fs::create_directories(card);
  { std::ofstream save(card / "other.gci", std::ios::binary); save << "GZLE01unrelated save"; }
  check(!launcher::lobby::has_game_save(card.u8string()), "an unrelated game's card cannot complete mod setup");
  { std::ofstream save(card / "melee.gci", std::ios::binary); save << "GALE01fixture save"; }
  check(launcher::lobby::has_game_save(card.u8string()), "the mod's own Melee save completes the card prompt gate");
  fs::remove_all(game, ec);
}

// ---- private chat: request, accept, decline, block, timeout, routing, older launchers, limits ----

Pair make_pair_with(const std::string& name, const Json& pa, const Json& pb,
                    launcher::lobby::PeerTestOptions a_options, launcher::lobby::PeerTestOptions b_options) {
  Pair pair;
  pair.dir = fs::temp_directory_path() / ("mu-p2p-" + std::to_string(GetCurrentProcessId()) + "-" + name);
  std::error_code ec; fs::remove_all(pair.dir, ec); fs::create_directories(pair.dir, ec);
  a_options.no_dht = true; b_options.no_dht = true;
  pair.a = std::make_unique<PeerLobby>((pair.dir / "a").u8string(), "", 0, a_options);
  pair.b = std::make_unique<PeerLobby>((pair.dir / "b").u8string(), "127.0.0.1:" + std::to_string(pair.a->port()), 0, b_options);
  pair.a->join(pa); pair.b->join(pb);
  return pair;
}
// The private room `p` holds with `other`, or an empty object.
Json room_of(const PeerLobby& p, const std::string& other) {
  const Json state = p.state();
  for (const auto& room : state["private"]) if (room.value("peer", std::string()) == other) return room;
  return Json::object();
}
std::string room_state(const PeerLobby& p, const std::string& other) { return room_of(p, other).value("state", std::string()); }
Json room_messages(const PeerLobby& p, const std::string& other) { return room_of(p, other).value("messages", Json::array()); }
size_t room_count(const PeerLobby& p) { return p.state()["private"].size(); }
// A asks, B accepts: the room both then hold.
std::string open_room(PeerLobby& a, PeerLobby& b) {
  const auto error = error_of([&] { a.command("pm_request", {{"target", b.id()}}); });
  if (!error.empty()) return {};
  if (!pump(a, b, 3000, [&] { return room_state(b, a.id()) == "incoming"; })) return {};
  const std::string room = room_of(b, a.id()).value("id", std::string());
  b.command("pm_accept", {{"room", room}});
  if (!pump(a, b, 3000, [&] { return room_state(a, b.id()) == "open" && room_state(b, a.id()) == "open"; })) return {};
  return room;
}

void private_accept_flow() {
  std::cout << "-- private chat: A asks, B accepts, both talk, A closes" << std::endl;
  auto pair = make_pair("pm-accept", profile("Alpha", "ALPH#101", "t:recomp", 2), profile("Beta", "BETA#202", "t:recomp", 2));
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "discovery");
  check(a.state()["players"][0].value("protocol", 0) >= launcher::lobby::private_protocol, "the other launcher announces a protocol with private chat");
  const auto sent = error_of([&] { a.command("pm_request", {{"target", b.id()}}); });
  check(sent.empty() && has_notice(a, "lobby.pm.sent"), "the request leaves: " + sent);
  check(error_of([&] { a.command("pm_request", {{"target", b.id()}}); }) == launcher::lang::tr("lobby.pm.pending", {{"name", "Beta"}}),
        "a second request to the same player is refused while the first waits");
  pump(a, b, 3000, [&] { return room_state(b, a.id()) == "incoming"; });
  const Json asked = room_of(b, a.id()), asking = room_of(a, b.id());
  check(asked.value("state", std::string()) == "incoming" && asked.value("name", std::string()) == "Alpha" &&
        has_notice(b, "lobby.pm.incoming"), "B sees a private chat request from Alpha");
  check(asking.value("state", std::string()) == "outgoing" && asking.value("id", std::string("a")) == asked.value("id", std::string("b")),
        "A holds the same room, waiting for the answer");
  check(asked.value("messages", Json::array()).empty() && room_count(a) == 1 && room_count(b) == 1, "nothing is in the room before it is accepted");
  const std::string room = asked.value("id", std::string());
  check(!error_of([&] { a.command("pm", {{"room", room}, {"text", "too early"}}); }).empty(), "A cannot write before B accepts");
  b.command("pm_accept", {{"room", room}});
  const bool open = pump(a, b, 3000, [&] { return room_state(a, b.id()) == "open" && room_state(b, a.id()) == "open"; });
  check(open && has_notice(a, "lobby.pm.opened") && has_notice(b, "lobby.pm.opened"), "the room opens on both sides");
  check(error_of([&] { a.command("pm_request", {{"target", b.id()}}); }) == launcher::lang::tr("lobby.pm.already", {{"name", "Beta"}}),
        "asking again with the room open is refused");
  if (!open) return;

  const auto first = error_of([&] { a.command("pm", {{"room", room}, {"text", "first line"}}); });
  check(first.empty(), "A writes: " + first);
  pump(a, b, 3000, [&] { return room_messages(b, a.id()).size() == 1; });
  pump(a, b, 1200);   // resends must not add a second copy
  Json at_b = room_messages(b, a.id());
  check(at_b.size() == 1 && at_b[0]["text"] == "first line" && at_b[0]["sender"] == a.id() && at_b[0]["name"] == "Alpha",
        "B gets it once, from Alpha");
  check(room_messages(a, b.id()).size() == 1, "A keeps its own copy");
  const auto reply = error_of([&] { b.command("pm", {{"room", room}, {"text", "second line"}}); });
  check(reply.empty(), "B answers: " + reply);
  pump(a, b, 3000, [&] { return room_messages(a, b.id()).size() == 2; });
  const Json at_a = room_messages(a, b.id());
  check(at_a.size() == 2 && at_a[0]["text"] == "first line" && at_a[1]["text"] == "second line" && at_a[1]["sender"] == b.id(),
        "A has both lines in order");
  check(a.state()["messages"].empty() && b.state()["messages"].empty(), "nothing went to the public chat");

  // The public chat keeps working next to it, and stays separate.
  a.command("chat", {{"text", "public line"}});
  pump(a, b, 3000, [&] { return b.state()["messages"].size() == 1; });
  check(b.state()["messages"].size() == 1 && b.state()["messages"][0]["text"] == "public line", "a public line reaches the lobby chat");
  check(room_messages(a, b.id()).size() == 2 && room_messages(b, a.id()).size() == 2, "and is not added to the private room");

  a.command("pm_close", {{"room", room}});
  const bool closed = pump(a, b, 3000, [&] { return room_state(b, a.id()) == "closed"; });
  check(closed && room_count(a) == 0, "A closes: its room is gone, B's is marked closed");
  check(room_messages(b, a.id()).size() == 2, "B can still read what was said");
  check(error_of([&] { b.command("pm", {{"room", room}, {"text", "anyone?"}}); }) == launcher::lang::tr("lobby.pm.closed"),
        "B cannot write into the closed room");
  b.command("pm_close", {{"room", room}});
  check(room_count(b) == 0, "B closes it too");
}

void private_decline_and_block() {
  std::cout << "-- private chat: decline, block, and a request that runs out" << std::endl;
  launcher::lobby::PeerTestOptions quick; quick.private_request_ms = 2500;
  auto pair = make_pair_with("pm-decline", profile("Alpha", "ALPH#101", "t:recomp", 2), profile("Beta", "BETA#202", "t:recomp", 2), quick, quick);
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "discovery");
  a.command("pm_request", {{"target", b.id()}});
  pump(a, b, 2000, [&] { return room_state(b, a.id()) == "incoming"; });
  std::string room = room_of(b, a.id()).value("id", std::string());
  check(!room.empty(), "B gets the request");
  b.command("pm_decline", {{"room", room}});
  check(pump(a, b, 3000, [&] { return has_notice(a, "lobby.pm.declined"); }), "A is told that B declined");
  check(room_count(a) == 0 && room_count(b) == 0, "no room is left on either side");
  check(error_of([&] { a.command("pm_request", {{"target", b.id()}}); }) == launcher::lang::tr("lobby.pm.wait"),
        "asking again at once is held back");
  check(!error_of([&] { b.command("pm_accept", {{"room", room}}); }).empty(), "a declined request cannot be accepted afterwards");

  pump(a, b, 3200);
  check(error_of([&] { a.command("pm_request", {{"target", b.id()}}); }).empty(), "a few seconds later A may ask again");
  pump(a, b, 2000, [&] { return room_state(b, a.id()) == "incoming"; });
  room = room_of(b, a.id()).value("id", std::string());
  check(!room.empty(), "B gets the second request");
  b.command("pm_decline", {{"room", room}, {"block", true}});
  pump(a, b, 3000, [&] { return room_count(a) == 0; });
  const Json blocked = b.state()["private_blocked"];
  check(blocked.size() == 1 && blocked[0] == a.id() && has_notice(b, "lobby.pm.blocked"), "B blocks A for the session");

  pump(a, b, 3200);
  const auto asked_at = std::chrono::steady_clock::now();
  check(error_of([&] { a.command("pm_request", {{"target", b.id()}}); }).empty(), "A asks a third time");
  bool shown = false;
  const bool ended = pump(a, b, 6000, [&] {
    if (room_count(b)) shown = true;
    return has_notice(a, "lobby.pm.no_answer");
  });
  const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - asked_at).count();
  check(ended && seconds >= 2.0 && seconds <= 4.5, "the request ends by itself, with no answer (" + std::to_string(seconds) + " s)");
  check(!shown && room_count(a) == 0, "B never saw a request from the blocked player, and A's room is gone");
  pump(a, b, 1500);
  check(room_count(b) == 0, "later copies of the request do not get through either");

  // Asking the blocked player unblocks them, and their Private Chat button then answers the request.
  check(error_of([&] { b.command("pm_request", {{"target", a.id()}}); }).empty(), "B asks A");
  check(b.state()["private_blocked"].empty(), "which lifts the block");
  pump(a, b, 2000, [&] { return room_state(a, b.id()) == "incoming"; });
  check(room_state(a, b.id()) == "incoming", "A gets B's request");
  check(error_of([&] { a.command("pm_request", {{"target", b.id()}}); }).empty(), "A pressing Private Chat on B accepts it");
  check(pump(a, b, 3000, [&] { return room_state(a, b.id()) == "open" && room_state(b, a.id()) == "open"; }), "and the room opens");
}

void private_timeout() {
  std::cout << "-- private chat: a request nobody answers" << std::endl;
  launcher::lobby::PeerTestOptions quick; quick.private_request_ms = 2500;
  auto pair = make_pair_with("pm-timeout", profile("Alpha", "ALPH#101", "t:recomp", 2), profile("Beta", "BETA#202", "t:recomp", 2), quick, quick);
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "discovery");
  a.command("pm_request", {{"target", b.id()}});
  check(pump(a, b, 2000, [&] { return room_state(b, a.id()) == "incoming"; }), "B has the request");
  check(room_of(b, a.id()).value("remaining", -1) >= 0 && room_of(b, a.id()).value("remaining", 99) <= 3, "with the seconds left to answer");
  const bool ended = pump(a, b, 6000, [&] { return has_notice(a, "lobby.pm.expired") && has_notice(b, "lobby.pm.missed"); });
  check(ended, "it runs out: A is told there was no answer, B that a request was missed");
  check(room_count(a) == 0 && room_count(b) == 0, "and no room is left behind");
  check(!has_notice(a, "lobby.pm.no_answer"), "a delivered request is not reported as unreachable");
}

void private_old_versions() {
  std::cout << "-- private chat: a launcher from before private chat" << std::endl;
  {
    // It announces lobby protocol 2: the request is not sent and the player is told why at once.
    launcher::lobby::PeerTestOptions current, old; old.protocol = 2;
    auto pair = make_pair_with("pm-old", profile("Alpha", "ALPH#101", "t:recomp", 2), profile("Beta", "BETA#202", "t:recomp", 9), current, old);
    auto& a = *pair.a; auto& b = *pair.b;
    check(see_each_other(pair), "an older launcher is still discovered");
    check(a.state()["players"][0].value("protocol", 0) == 2, "and is known to be older");
    const auto error = error_of([&] { a.command("pm_request", {{"target", b.id()}}); });
    check(error == launcher::lang::tr("lobby.pm.unsupported", {{"name", "Beta"}}), "asking says their version has no private chat: " + error);
    check(room_count(a) == 0, "no room waits for an answer that cannot come");
    pump(a, b, 500);
    check(room_count(b) == 0, "nothing was sent to the older launcher");
  }
  {
    // Its version is not known (it claims the current protocol) and it treats the new messages as
    // any launcher treats an action it does not know: no answer, no acknowledgement. The request
    // must end by itself, and everything the older launcher does know must keep working.
    launcher::lobby::PeerTestOptions quick, old; quick.private_request_ms = 2500; old.no_private = true;
    auto pair = make_pair_with("pm-unknown", profile("Alpha", "ALPH#101", "t:recomp", 20), profile("Beta", "BETA#202", "t:recomp", 9), quick, old);
    auto& a = *pair.a; auto& b = *pair.b;
    check(see_each_other(pair), "discovery");
    const auto asked_at = std::chrono::steady_clock::now();
    check(error_of([&] { a.command("pm_request", {{"target", b.id()}}); }).empty(), "the request is sent");
    const bool ended = pump(a, b, 6000, [&] { return has_notice(a, "lobby.pm.no_answer"); });
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - asked_at).count();
    check(ended && seconds <= 4.5 && room_count(a) == 0, "it ends without an answer instead of hanging (" + std::to_string(seconds) + " s)");
    check(room_count(b) == 0 && b.state()["notices"].empty(), "the older launcher ignored it: no room, no message for its player");
    a.command("chat", {{"text", "still here"}});
    check(pump(a, b, 3000, [&] { return b.state()["messages"].size() == 1; }), "public chat still reaches the older launcher");
    check(error_of([&] { a.command("request", {{"target", b.id()}}); }).empty(), "a match request still leaves");
    pump(a, b, 3000, [&] { return b.state()["requests"].size() == 1; });
    const Json requests = b.state()["requests"];
    check(requests.size() == 1, "and arrives");
    if (requests.size() == 1) {
      b.command("accept", {{"request", requests[0]["id"]}});
      Json la, lb; bool got_a = false, got_b = false;
      pump(a, b, 5000, [&] {
        if (!got_a) got_a = a.take_launch(la);
        if (!got_b) got_b = b.take_launch(lb);
        return got_a && got_b;
      });
      check(got_a && got_b, "and the match starts on both sides");
    }
  }
}

void private_routing() {
  std::cout << "-- private chat: three players, two rooms" << std::endl;
  const fs::path dir = fs::temp_directory_path() / ("mu-p2p-" + std::to_string(GetCurrentProcessId()) + "-pm-three");
  std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
  launcher::lobby::PeerTestOptions options; options.no_dht = true;
  PeerLobby a((dir / "a").u8string(), "", 0, options);
  const std::string seed = "127.0.0.1:" + std::to_string(a.port());
  PeerLobby b((dir / "b").u8string(), seed, 0, options), c((dir / "c").u8string(), seed, 0, options);
  c.add_address("127.0.0.1:" + std::to_string(b.port()));
  a.join(profile("Alpha", "ALPH#101", "t:recomp", 2)); b.join(profile("Beta", "BETA#202", "t:recomp", 2));
  c.join(profile("Gamma", "GAMM#303", "t:recomp", 2));
  auto pump3 = [&](int ms, const std::function<bool()>& done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
      a.tick(); b.tick(); c.tick();
      if (done && done()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done ? done() : true;
  };
  check(pump3(8000, [&] { return a.state()["players"].size() == 2 && b.state()["players"].size() == 2 && c.state()["players"].size() == 2; }),
        "all three players see each other");
  // A and B open a room, then C and B.
  a.command("pm_request", {{"target", b.id()}});
  c.command("pm_request", {{"target", b.id()}});
  pump3(3000, [&] { return room_state(b, a.id()) == "incoming" && room_state(b, c.id()) == "incoming"; });
  const std::string with_a = room_of(b, a.id()).value("id", std::string()), with_c = room_of(b, c.id()).value("id", std::string());
  check(!with_a.empty() && !with_c.empty() && with_a != with_c, "B has a request from each, in separate rooms");
  if (with_a.empty() || with_c.empty()) return;
  b.command("pm_accept", {{"room", with_a}}); b.command("pm_accept", {{"room", with_c}});
  check(pump3(3000, [&] { return room_state(a, b.id()) == "open" && room_state(c, b.id()) == "open"; }), "both rooms open: several can be open at once");

  a.command("pm", {{"room", with_a}, {"text", "for Beta only"}});
  pump3(3000, [&] { return room_messages(b, a.id()).size() == 1; });
  pump3(1200, {});
  check(room_messages(b, a.id()).size() == 1 && room_messages(b, a.id())[0]["text"] == "for Beta only", "B gets A's line in the room with A");
  check(room_messages(b, c.id()).empty(), "not in the room with C");
  check(room_messages(c, b.id()).empty() && room_of(c, a.id()).empty() && room_count(c) == 1, "C gets nothing: it only holds its own room with B");
  check(a.state()["messages"].empty() && b.state()["messages"].empty() && c.state()["messages"].empty(), "and nothing is in anyone's public chat");
  check(c.state().dump().find("for Beta only") == std::string::npos, "the line is nowhere in C's lobby state");

  c.command("pm", {{"room", with_c}, {"text", "from Gamma"}});
  pump3(3000, [&] { return room_messages(b, c.id()).size() == 1; });
  check(room_messages(b, c.id()).size() == 1 && room_messages(b, c.id())[0]["sender"] == c.id(), "C's line lands in B's room with C");
  check(room_messages(b, a.id()).size() == 1 && room_messages(a, b.id()).size() == 1, "and not in the room between A and B, on either side");
  check(error_of([&] { c.command("pm", {{"room", with_a}, {"text", "let me in"}}); }) == launcher::lang::tr("lobby.pm.closed"),
        "C cannot write into the room between A and B");

  // Closing one room leaves the other alone.
  b.command("pm_close", {{"room", with_c}});
  check(pump3(3000, [&] { return room_state(c, b.id()) == "closed"; }), "B closes the room with C");
  check(room_state(b, a.id()) == "open" && room_state(a, b.id()) == "open", "the room with A stays open");
}

void private_crossed() {
  std::cout << "-- private chat: both players ask at once" << std::endl;
  auto pair = make_pair("pm-crossed", profile("Alpha", "ALPH#101", "t:recomp", 2), profile("Beta", "BETA#202", "t:recomp", 2));
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "discovery");
  const auto ea = error_of([&] { a.command("pm_request", {{"target", b.id()}}); });
  const auto eb = error_of([&] { b.command("pm_request", {{"target", a.id()}}); });
  check(ea.empty() && eb.empty(), "both requests leave");
  const bool open = pump(a, b, 4000, [&] { return room_state(a, b.id()) == "open" && room_state(b, a.id()) == "open"; });
  pump(a, b, 1200);
  check(open && room_count(a) == 1 && room_count(b) == 1, "one room opens, without either having to accept");
  check(room_of(a, b.id()).value("id", std::string("a")) == room_of(b, a.id()).value("id", std::string("b")), "the same room on both sides");
  const std::string room = room_of(a, b.id()).value("id", std::string());
  check(error_of([&] { a.command("pm", {{"room", room}, {"text", "crossed"}}); }).empty() &&
        pump(a, b, 3000, [&] { return room_messages(b, a.id()).size() == 1; }), "and it carries messages");
}

void private_limits() {
  std::cout << "-- private chat: limits and plain text" << std::endl;
  using launcher::lobby::clean_chat_text;
  check(clean_chat_text("plain") == "plain", "plain text is kept");
  check(clean_chat_text("a\x07" "b\tc\r\nd") == "abcd", "control characters are removed");
  check(clean_chat_text("\xE2\x80\xAE" "abc" "\xE2\x81\xA6") == "abc", "text direction overrides are removed");
  check(clean_chat_text("ok \xF0\x9F\x91\x8B \xE3\x81\x82") == "ok \xF0\x9F\x91\x8B \xE3\x81\x82", "emoji and other scripts are kept");
  check(clean_chat_text("broken \xC3").empty() && clean_chat_text("\xC0\xAF").empty(), "text that is not valid UTF-8 is refused whole");
  check(clean_chat_text("<b>bold</b> [url]") == "<b>bold</b> [url]", "markup is only ever text");

  auto pair = make_pair("pm-limits", profile("Alpha", "ALPH#101", "t:recomp", 2), profile("Beta", "BETA#202", "t:recomp", 2));
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "discovery");
  const std::string chat_error = launcher::lang::tr("lobby.error.chat");
  check(!error_of([&] { a.command("pm_request", {{"target", a.id()}}); }).empty(), "a player cannot ask themselves");
  check(!error_of([&] { a.command("pm_request", {{"target", std::string(64, '0')}}); }).empty(), "or a player who is not there");
  check(error_of([&] { a.command("pm", {{"room", std::string(32, '0')}, {"text", "hello"}}); }) == launcher::lang::tr("lobby.pm.closed"),
        "a message needs an open room");
  const std::string room = open_room(a, b);
  check(!room.empty(), "a room opens");
  if (room.empty()) return;

  check(error_of([&] { a.command("pm", {{"room", room}, {"text", ""}}); }) == chat_error, "an empty message is refused");
  check(error_of([&] { a.command("pm", {{"room", room}, {"text", "\x01\x02\n"}}); }) == chat_error, "so is one made of control characters only");
  check(error_of([&] { a.command("pm", {{"room", room}, {"text", std::string(301, 'x')}}); }) == chat_error, "301 bytes are too long");
  check(error_of([&] { a.command("pm", {{"room", room}, {"text", std::string(300, '"')}}); }) == chat_error,
        "a message too large for one packet is refused, not lost");
  check(room_messages(a, b.id()).empty(), "a refused message is not kept");

  check(error_of([&] { a.command("pm", {{"room", room}, {"text", "he\x01llo\nthere\x7F"}}); }).empty(), "a line with control characters is sent");
  check(error_of([&] { a.command("pm", {{"room", room}, {"text", "again"}}); }) == chat_error, "a second message within a second is refused");
  pump(a, b, 3000, [&] { return room_messages(b, a.id()).size() == 1; });
  Json at_b = room_messages(b, a.id());
  check(at_b.size() == 1 && at_b[0]["text"] == "hellothere", "and arrives without them");

  pump(a, b, 1100);
  check(error_of([&] { a.command("pm", {{"room", room}, {"text", std::string(300, 'x')}}); }).empty(), "300 bytes are fine");
  pump(a, b, 3000, [&] { return room_messages(b, a.id()).size() == 2; });
  at_b = room_messages(b, a.id());
  check(at_b.size() == 2 && at_b[1]["text"] == std::string(300, 'x'), "and arrive whole");

  // Several lines, one a second: each arrives once and in order.
  for (int i = 0; i < 3; ++i) {
    pump(a, b, 1100);
    a.command("pm", {{"room", room}, {"text", "line " + std::to_string(i)}});
  }
  pump(a, b, 4000, [&] { return room_messages(b, a.id()).size() == 5; });
  at_b = room_messages(b, a.id());
  check(at_b.size() == 5 && at_b[2]["text"] == "line 0" && at_b[3]["text"] == "line 1" && at_b[4]["text"] == "line 2",
        "several lines arrive once each, in order");

  // B leaves the public lobby: the open room keeps working, a new request from a stranger does not.
  b.command("leave");
  pump(a, b, 1500);
  check(error_of([&] { b.command("pm", {{"room", room}, {"text", "still talking"}}); }).empty() &&
        pump(a, b, 3000, [&] { return room_messages(a, b.id()).size() == 6; }), "an open room outlives leaving the public lobby");
  a.command("pm_close", {{"room", room}});
  pump(a, b, 500);
  check(!error_of([&] { a.command("pm_request", {{"target", b.id()}}); }).empty(), "a player outside the public lobby cannot be asked by a stranger");
}

#ifdef MELEE_NO_SLIPPI
// ---- peer-to-peer matches: the launcher built without the Slippi layer (MELEE_NO_SLIPPI) ----
bool searching(const PeerLobby& p) { return p.state()["self"].value("searching", false); }
bool has_kind(const PeerLobby& p, const std::string& kind) {
  const Json state = p.state();
  for (const auto& n : state["notices"]) if (n.value("kind", std::string()) == kind) return true;
  return false;
}

// Both launchers build the same match from the request id and the two halves of the setup.
void p2p_setup() {
  std::cout << "-- p2p: A requests B, B accepts, both hold the same match setup" << std::endl;
  auto pair = make_pair("p2p-setup", profile("Alpha", "ALPH#101", "t:source", 2), profile("Be:ta", "BETA#202", "t:source", 2));
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "p2p: both players see each other");
  check(a.state()["self"]["protocol"] == 4 && a.state()["players"][0].value("protocol", 0) == 4, "p2p: both announce lobby protocol 4");
  check(error_of([&] { a.command("request", {{"target", b.id()}}); }).empty(), "p2p: the request is sent");
  pump(a, b, 3000, [&] { return b.state()["requests"].size() == 1; });
  const Json requests = b.state()["requests"];
  check(requests.size() == 1 && requests[0]["transport"] == "p2p", "p2p: B holds the request, marked peer-to-peer");
  if (requests.size() != 1) return;
  b.command("accept", {{"request", requests[0]["id"]}});
  Json la, lb; bool got_a = false, got_b = false;
  pump(a, b, 5000, [&] {
    if (!got_a) got_a = a.take_launch(la);
    if (!got_b) got_b = b.take_launch(lb);
    return got_a && got_b;
  });
  check(got_a && got_b && la.count("p2p") && lb.count("p2p"), "p2p: both sides launch with a match setup");
  if (!(got_a && got_b && la.count("p2p") && lb.count("p2p"))) return;
  const Json pa = la["p2p"], pb = lb["p2p"];
  check(la["request"] == lb["request"], "p2p: the same match on both sides");
  check(pa["slot"] == 0 && pb["slot"] == 1, "p2p: the requester is slot 0, the other player slot 1");
  check(pa["seed"] == pb["seed"] && pa["stage"] == pb["stage"] && pa["delay"] == pb["delay"] && pa["delay"] == 2,
        "p2p: the same seed, stage and delay");
  // The seed is the first four bytes of BLAKE2b over the request id, and it draws the stage.
  const std::string rid = la["request"].get<std::string>();
  uint8_t digest[4]{};
  crypto_blake2b(digest, sizeof digest, reinterpret_cast<const uint8_t*>(rid.data()), rid.size());
  char seed[9]{}; std::snprintf(seed, sizeof seed, "%02x%02x%02x%02x", digest[0], digest[1], digest[2], digest[3]);
  const uint32_t value = (uint32_t(digest[0]) << 24) | (uint32_t(digest[1]) << 16) | (uint32_t(digest[2]) << 8) | digest[3];
  const int legal[6] = {0x1F, 0x20, 0x1C, 0x08, 0x02, 0x03};
  check(pa["seed"] == seed && pa["stage"] == legal[value % 6], "p2p: the seed comes from the request id and picks a legal stage");
  // Both on Fox: the second player gets the next color. Slot 0's character is first.
  const Json chars = Json::array({Json::array({2, 0}), Json::array({2, 1})});
  check(pa["chars"] == chars && pb["chars"] == chars, "p2p: the same characters in slot order, the same character on two colors");
  const Json names = Json::array({"Alpha", "Beta"});
  check(pa["names"] == names && pb["names"] == names, "p2p: the same names in slot order, without the colon");
  check(pa["expect"] == b.id() && pb["expect"] == a.id(), "p2p: each expects the other's identity key");
  const int port_a = pa.value("port", 0), port_b = pb.value("port", 0);
  // The game takes the lobby's own port (the path between the two lobby sockets is the open one),
  // and the first address to dial is the one this lobby hears the other from.
  check(port_a > 0 && port_b > 0 && port_a != port_b && port_a == a.port() && port_b == b.port(), "p2p: each game takes its lobby's UDP port");
  check(pa["peers"].size() >= 1 && pa["peers"][0] == "127.0.0.1:" + std::to_string(b.port()) &&
        pb["peers"].size() >= 1 && pb["peers"][0] == "127.0.0.1:" + std::to_string(a.port()),
        "p2p: each dials the other's lobby address as observed, first");
  for (const Json* p : {&pa, &pb})
    for (size_t i = 1; i < (*p)["peers"].size(); ++i) {
      const std::string peer = (*p)["peers"][i].get<std::string>(), port = ":" + std::to_string(p == &pa ? b.port() : a.port());
      check(peer.size() > port.size() && peer.compare(peer.size() - port.size(), port.size(), port) == 0,
            "p2p: the other's own addresses carry its lobby port: " + peer);
    }
  const std::string args = launcher::lobby::p2p_arguments(pa, "C:\\mu\\lobby-peer-identity.json", "C:\\mu\\p2p-results\\r.json");
  std::cout << "      " << args << std::endl;
  check(args.find(" --p2p-port " + std::to_string(port_a) + " --p2p-peer 127.0.0.1:" + std::to_string(port_b)) == 0 &&
        args.find(" --p2p-slot 0 --p2p-chars 2/0:2/1 --p2p-stage " + std::to_string(pa["stage"].get<int>()) + " --p2p-seed " + seed + " --p2p-delay 2") != std::string::npos &&
        args.find(" --p2p-identity \"C:\\mu\\lobby-peer-identity.json\" --p2p-expect " + b.id() + " --p2p-result \"C:\\mu\\p2p-results\\r.json\" --p2p-names \"Alpha:Beta\"") != std::string::npos,
        "p2p: the game's arguments carry the whole setup");
  Json bad = pa; bad["peers"][0] = "1.2.3.4:5\" --iso x";
  check(launcher::lobby::p2p_arguments(bad, "i.json", "r.json").empty(), "p2p: an address that is not one never reaches the command line");
  bad = pa; bad["stage"] = 0x55;
  check(launcher::lobby::p2p_arguments(bad, "i.json", "r.json").empty(), "p2p: a stage outside the legal list is refused");
  bad = pa; bad["chars"][0][1] = 4;   // Fox has four colors, 0 to 3
  check(launcher::lobby::p2p_arguments(bad, "i.json", "r.json").empty(), "p2p: a color the character does not have is refused");
  bad = pa; bad["chars"][1][0] = 26;
  check(launcher::lobby::p2p_arguments(bad, "i.json", "r.json").empty(), "p2p: a character id past the last one is refused");
  bad = pa; bad["chars"] = Json::array({Json::array({0, 5}), Json::array({17, 5})});
  check(launcher::lobby::p2p_arguments(bad, "i.json", "r.json").find(" --p2p-chars 0/5:17/5 ") != std::string::npos,
        "p2p: the sixth color of a character that has six is passed on");
  check(launcher::lobby::p2p_color_count(2) == 4 && launcher::lobby::p2p_color_count(0) == 6 && launcher::lobby::p2p_color_count(25) == 5 &&
        launcher::lobby::p2p_color_count(26) == 0 && launcher::lobby::p2p_color_count(-1) == 0, "p2p: color counts by character");
  check(launcher::lobby::p2p_name("a:b\"c\\d") == "abcd" && launcher::lobby::p2p_name(std::string(40, 'x')).size() == 31 &&
        launcher::lobby::p2p_name(" : ") == "Player", "p2p: names are cut to what the game takes");
  const std::string code = launcher::lobby::derived_code("Alpha", a.id());
  check(launcher::lobby::valid_code(code) && code.rfind("ALPH#", 0) == 0 && code.size() == 8 && code == launcher::lobby::derived_code("Alpha", a.id()) &&
        launcher::lobby::valid_code(launcher::lobby::derived_code("\xE6\x97\xA5", a.id())), "p2p: the code made from the name and the key is a valid, stable code: " + code);
  pump(a, b, 1500);
  Json extra;
  check(!a.take_launch(extra) && !b.take_launch(extra), "p2p: no second launch on either side");
}

// The accept is lost on the way to A at first: the setup still arrives whole with a resend.
void p2p_setup_loss() {
  std::cout << "-- p2p: the acknowledgment carrying A's half of the setup is lost at first" << std::endl;
  auto pair = make_pair("p2p-loss", profile("Alpha", "ALPH#101", "t:source", 20), profile("Beta", "BETA#202", "t:source", 9));
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "p2p loss: discovery");
  a.command("request", {{"target", b.id()}});
  pump(a, b, 3000, [&] { return b.state()["requests"].size() == 1; });
  const Json requests = b.state()["requests"];
  if (requests.size() != 1) { check(false, "p2p loss: the request arrives"); return; }
  a.test_drop(false, true);   // A hears the accept but its acknowledgments go nowhere
  b.command("accept", {{"request", requests[0]["id"]}});
  Json la, lb; bool got_a = false, got_b = false;
  pump(a, b, 1500, [&] { if (!got_a) got_a = a.take_launch(la); if (!got_b) got_b = b.take_launch(lb); return false; });
  check(got_a && !got_b, "p2p loss: B does not launch without A's half of the setup");
  a.test_drop(false, false);
  pump(a, b, 4000, [&] { if (!got_b) got_b = b.take_launch(lb); return got_b; });
  check(got_b && lb.count("p2p") && la.count("p2p") && lb["p2p"]["seed"] == la["p2p"]["seed"] &&
        lb["p2p"]["peers"][0] == "127.0.0.1:" + std::to_string(la["p2p"].value("port", 0)), "p2p loss: a resent acknowledgment completes it");
}

void p2p_auto_two() {
  std::cout << "-- p2p: two searching players are paired, once" << std::endl;
  auto pair = make_pair("p2p-auto", profile("Alpha", "ALPH#101", "t:source", 20), profile("Beta", "BETA#202", "t:source", 9));
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "auto: discovery");
  check(error_of([&] { a.command("search", {{"on", true}}); }).empty() && error_of([&] { b.command("search", {{"on", true}}); }).empty(),
        "auto: both start searching");
  check(searching(a) && searching(b), "auto: both say so");
  std::vector<Json> launches_a, launches_b;
  auto collect = [&] { Json l; while (a.take_launch(l)) launches_a.push_back(l); while (b.take_launch(l)) launches_b.push_back(l); };
  pump(a, b, 20000, [&] { collect(); return !launches_a.empty() && !launches_b.empty(); });
  pump(a, b, 6000, [&] { collect(); return false; });
  check(launches_a.size() == 1 && launches_b.size() == 1, "auto: exactly one match, launched once on each side (" +
        std::to_string(launches_a.size()) + ", " + std::to_string(launches_b.size()) + ")");
  if (launches_a.size() != 1 || launches_b.size() != 1) return;
  const Json pa = launches_a[0]["p2p"], pb = launches_b[0]["p2p"];
  check(launches_a[0]["request"] == launches_b[0]["request"] && pa["seed"] == pb["seed"] && pa["chars"] == pb["chars"] &&
        pa["slot"] != pb["slot"] && pa.value("auto", false) && pb.value("auto", false), "auto: the same automatic match, distinct slots");
  check(!searching(a) && !searching(b), "auto: searching ends for both when the match starts");
  check(!has_kind(a, "refused") && !has_kind(b, "refused") && !has_kind(a, "declined") && !has_kind(b, "declined"),
        "auto: nobody is shown a refusal");
}

void p2p_auto_three() {
  std::cout << "-- p2p: three searching players, one match, one still searching" << std::endl;
  const fs::path dir = fs::temp_directory_path() / ("mu-p2p-" + std::to_string(GetCurrentProcessId()) + "-p2p-three");
  std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
  launcher::lobby::PeerTestOptions options; options.no_dht = true;
  PeerLobby a((dir / "a").u8string(), "", 0, options);
  const std::string seed = "127.0.0.1:" + std::to_string(a.port());
  PeerLobby b((dir / "b").u8string(), seed, 0, options), c((dir / "c").u8string(), seed, 0, options);
  c.add_address("127.0.0.1:" + std::to_string(b.port()));
  a.join(profile("Alpha", "ALPH#101", "t:source", 2)); b.join(profile("Beta", "BETA#202", "t:source", 9));
  c.join(profile("Gamma", "GAMM#303", "t:source", 20));
  PeerLobby* all[3] = {&a, &b, &c};
  std::vector<Json> launches[3];
  auto pump3 = [&](int ms, const std::function<bool()>& done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
      for (int i = 0; i < 3; ++i) { all[i]->tick(); Json l; while (all[i]->take_launch(l)) launches[i].push_back(l); }
      if (done && done()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done ? done() : true;
  };
  check(pump3(8000, [&] { return a.state()["players"].size() == 2 && b.state()["players"].size() == 2 && c.state()["players"].size() == 2; }),
        "three: all three players see each other");
  for (auto* p : all) p->command("search", {{"on", true}});
  auto total = [&] { return launches[0].size() + launches[1].size() + launches[2].size(); };
  pump3(25000, [&] { return total() >= 2; });
  pump3(12000, {});   // long enough for the player left over to ask the other two again
  check(total() == 2, "three: one match, launched once on each of its two sides (" + std::to_string(total()) + " launches)");
  int matched = 0, still = 0; std::string request;
  bool same = true;
  for (int i = 0; i < 3; ++i) {
    if (launches[i].size() == 1) {
      ++matched;
      const std::string id = launches[i][0].value("request", std::string());
      if (request.empty()) request = id; else same = same && request == id;
      check(!searching(*all[i]), "three: a matched player stopped searching");
    } else if (launches[i].empty() && searching(*all[i])) ++still;
  }
  check(matched == 2 && same, "three: two players hold the same match");
  check(still == 1, "three: the third is still searching");
}

void p2p_block() {
  std::cout << "-- p2p: a blocked player is never paired and never answered" << std::endl;
  auto pair = make_pair("p2p-block", profile("Alpha", "ALPH#101", "t:source", 20), profile("Beta", "BETA#202", "t:source", 9));
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "block: discovery");
  check(error_of([&] { a.command("block", {{"target", b.id()}}); }).empty(), "block: A blocks B");
  const Json blocked = a.state()["blocked"];
  check(blocked.size() == 1 && blocked[0] == b.id() && a.state()["players"][0].value("blocked", false), "block: A's list holds B");
  check(!error_of([&] { a.command("request", {{"target", b.id()}}); }).empty(), "block: A cannot ask a player it blocked");
  // B asks by hand: refused without a reason B could read anything into, and A is told nothing.
  check(error_of([&] { b.command("request", {{"target", a.id()}}); }).empty(), "block: B's request leaves");
  check(pump(a, b, 4000, [&] { return has_notice(b, "lobby.declined.other"); }), "block: B is told A cannot play right now");
  check(a.state()["requests"].empty() && !has_kind(a, "refused"), "block: A sees no request and no notice about it");
  // Both search: no match, however long.
  pump(a, b, 3200);
  a.command("search", {{"on", true}}); b.command("search", {{"on", true}});
  Json launch; bool any = false;
  pump(a, b, 9000, [&] { any = any || a.take_launch(launch) || b.take_launch(launch); return any; });
  check(!any && searching(a) && searching(b), "block: two searching players, one blocked, are never paired");
  // The list is kept with the friends: a new lobby on the same folder still has it.
  const std::string folder = (pair.dir / "a").u8string();
  const std::string b_id = b.id();
  pair.a.reset();
  launcher::lobby::PeerTestOptions options; options.no_dht = true;
  PeerLobby again(folder, "", 0, options);
  again.update_profile(profile("Alpha", "ALPH#101", "t:source", 20));
  const Json kept = again.state()["blocked"];
  check(kept.size() == 1 && kept[0] == b_id, "block: the list survives a restart");
  check(error_of([&] { again.command("unblock", {{"target", b_id}}); }).empty() && again.state()["blocked"].empty(), "block: and can be undone");
}

void p2p_old_protocol() {
  std::cout << "-- p2p: a launcher on an older lobby protocol is refused, with the reason" << std::endl;
  launcher::lobby::PeerTestOptions current, old; old.protocol = 3;
  auto pair = make_pair_with("p2p-old", profile("Alpha", "ALPH#101", "t:source", 20), profile("Beta", "BETA#202", "t:source", 9), current, old);
  auto& a = *pair.a; auto& b = *pair.b;
  check(see_each_other(pair), "old: an older launcher is still discovered");
  check(a.state()["players"][0].value("protocol", 0) == 3, "old: and is known to be older");
  const auto error = error_of([&] { a.command("request", {{"target", b.id()}}); });
  check(error == launcher::lang::tr("lobby.declined.p2p_version", {{"name", "Beta"}}), "old: asking them says why at once: " + error);
  check(a.state()["requests"].empty(), "old: nothing was sent");
  // The older launcher asks: it gets a refusal it can show, and this player is told too.
  check(error_of([&] { b.command("request", {{"target", a.id()}}); }).empty(), "old: their request leaves");
  check(pump(a, b, 4000, [&] { return has_notice(a, "lobby.refused.p2p_version") && b.state()["requests"].empty(); }),
        "old: it is refused, and this player hears why");
  const Json refused = notice(b, "lobby.declined.p2p_version");
  check(!refused.is_null() && a.state()["requests"].empty(), "old: the older launcher is told the reason");
  Json launch;
  pump(a, b, 1000);
  check(!a.take_launch(launch) && !b.take_launch(launch), "old: no match starts");
  // An older launcher that is searching is never asked by automatic pairing.
  pump(a, b, 3200);
  a.command("search", {{"on", true}});
  bool any = false;
  pump(a, b, 6000, [&] { any = any || a.take_launch(launch) || b.take_launch(launch); return any; });
  check(!any && searching(a) && a.state()["requests"].empty(), "old: automatic pairing leaves them alone");
}

// Connect by address: B never heard of A (no bootstrap); A's invite line is all it gets.
void p2p_invite() {
  std::cout << "-- p2p: connect by address with an invite line" << std::endl;
  const fs::path dir = fs::temp_directory_path() / ("mu-p2p-" + std::to_string(GetCurrentProcessId()) + "-p2p-invite");
  std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
  launcher::lobby::PeerTestOptions options; options.no_dht = true;
  PeerLobby a((dir / "a").u8string(), "", 0, options), b((dir / "b").u8string(), "", 0, options);
  a.join(profile("Alpha", "ALPH#101", "t:source", 20)); b.join(profile("Beta", "BETA#202", "t:source", 9));
  pump(a, b, 300);
  const std::string own = a.invite();
  check(own.rfind("MUINVITE1|" + a.id() + "|", 0) == 0 && own.size() > 5 && own.substr(own.size() - 6) == "|Alpha", "invite: the line holds the key and the name");
  // This PC may have no network address but loopback, so the test writes the address itself.
  const std::string line = "MUINVITE1|" + a.id() + "|127.0.0.1:" + std::to_string(a.port()) + "|Alpha";
  check(!error_of([&] { b.command("connect", {{"invite", "hello"}}); }).empty(), "invite: text that is not an invite is refused");
  check(!error_of([&] { a.command("connect", {{"invite", line}}); }).empty(), "invite: a player's own invite is refused");
  check(error_of([&] { b.command("connect", {{"invite", "  " + line + "\r\n"}}); }).empty(), "invite: a pasted line is taken");
  check(pump(a, b, 6000, [&] { return has_notice(b, "lobby.invite.found"); }), "invite: the two launchers find each other");
  const Json found = b.state()["invite"];
  check(found.value("found", std::string()) == a.id() && found.value("seq", 0) == 1 && b.state()["players"].size() == 1,
        "invite: the player is named for the list to select");
}
#endif
}  // namespace

int main() {
  WSADATA ws{};
  if (WSAStartup(MAKEWORD(2, 2), &ws)) { std::cerr << "no Winsock\n"; return 2; }
  try {
#ifdef MELEE_NO_SLIPPI
    // The launcher built without the Slippi layer: its accepted matches are peer-to-peer and it
    // refuses the older flow, so only the cases written for it run here (the rest of this file is
    // the normal build's port_launcher_lobby_p2p).
    p2p_setup();
    p2p_setup_loss();
    p2p_auto_two();
    p2p_auto_three();
    p2p_block();
    p2p_old_protocol();
    p2p_invite();
    WSACleanup();
    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("PASS: launcher lobby peer-to-peer matches")) << std::endl;
    return failures ? 1 : 0;
#endif
    accept_flow();
    cross_build_flow();
    refusals();
    crossed_requests();
    one_way_loss(true);
    one_way_loss(false);
    clock_skew();
    friends_by_code();
    mods();
    mod_match_disc();
    private_accept_flow();
    private_decline_and_block();
    private_timeout();
    private_old_versions();
    private_routing();
    private_crossed();
    private_limits();
  } catch (const std::exception& ex) {
    std::cerr << "unexpected exception: " << ex.what() << std::endl;
    ++failures;
  }
  WSACleanup();
  std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("PASS: launcher lobby peer requests")) << std::endl;
  return failures ? 1 : 0;
}
