// Match requests between two peer lobbies in one process, end to end over loopback UDP: discovery,
// delivery, acceptance, refusals with their reasons on both sides, crossed requests, one-way packet
// loss, clock skew, friends by code while hidden, the 0.8.5 mod fields (badges, "open to", custom
// ISOs) and an Akaneia match's disc from the Mods folder scan. No DHT (each lobby is told the other's
// address), no window, no focus: ctest runs it.
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
  refusal("different Game Build", [](PeerLobby& b, Json& pb) { pb["build"] = "0.8.5:source"; b.update_profile(pb); },
          "lobby.declined.build", "lobby.refused.build");
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
  // Vanilla needs the same Game Build (these differ); a mod only needs the same version.
  const std::vector<std::string> expected{"akaneia", "custom:" + hash};
  check(modes == expected, "the common versions are Akaneia and the matching custom ISO, not vanilla or ACE");
  check(launcher::lobby::iso_hash(seen) == launcher::lobby::iso_hash(b.state()["self"]["profile"]), "the same ISO is recognised under two names");
  const auto vanilla = error_of([&] { a.command("request", {{"target", b.id()}, {"mode", "vanilla"}}); });
  check(!vanilla.empty(), "vanilla is refused before sending (different Game Build): " + vanilla);
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
}  // namespace

int main() {
  WSADATA ws{};
  if (WSAStartup(MAKEWORD(2, 2), &ws)) { std::cerr << "no Winsock\n"; return 2; }
  try {
    accept_flow();
    refusals();
    crossed_requests();
    one_way_loss(true);
    one_way_loss(false);
    clock_skew();
    friends_by_code();
    mods();
    mod_match_disc();
  } catch (const std::exception& ex) {
    std::cerr << "unexpected exception: " << ex.what() << std::endl;
    ++failures;
  }
  WSACleanup();
  std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("PASS: launcher lobby peer requests")) << std::endl;
  return failures ? 1 : 0;
}
