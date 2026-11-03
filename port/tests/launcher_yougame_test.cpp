// The launcher's YouGame bridge without a network: request parsing, the token and origin checks,
// and each request type answered as the game's loopback client expects ({ok, result} or {ok:false}).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_yougame.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

using json = nlohmann::json;
using namespace launcher::yougame;

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

namespace {
constexpr uint16_t kPort = 50123;

HttpRequest post(const std::string& body, const std::string& token = "tok", const std::string& host = "127.0.0.1:50123") {
  const std::string raw = "POST /sdk HTTP/1.1\r\nHost: " + host + "\r\nAuthorization: Bearer " + token +
                          "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  HttpRequest r;
  CHECK(parse_request(raw, r) == 1);
  return r;
}

struct Fake {
  bool signed_in = true;
  std::string last_ticket;
  Backend backend() {
    Backend b;
    b.account = [this] { Account a; a.signed_in = signed_in; a.name = "Tester"; a.code = "TEST#123"; return a; };
    b.player_id = [] { return std::string("user-1"); };
    b.code = [] { return std::string(R"({"code":"TEST#123"})"); };
    b.rank = [](const std::string& mode) { return json{{"mode", mode}, {"rank", "Gold II"}}.dump(); };
    b.ticket = [this](const std::string& body) {
      last_ticket = body;
      const json q = json::parse(body);
      if (q.value("direct", "") == "BAD#1") throw std::runtime_error("not_found|No player has that code");
      return json{{"url", "wss://rooms.example/r"}, {"ticket", "t-1"}, {"room", "abc"}, {"queue", q["queue"]},
                  {"rank", "Gold II"}, {"secret_internal", 1}}.dump();
    };
    return b;
  }
};

json call(Fake& f, const HttpRequest& r, int* status = nullptr) {
  const BridgeReply reply = handle(r, {"old", "tok"}, kPort, f.backend(), R"({"version":"1.0.0","exe_sha256":"aa","game_sha256":"bb"})");
  if (status) *status = reply.status;
  return json::parse(reply.body);
}
}  // namespace

// --serve SITE EMAIL PASSWORD VERSION EXE DLL: the real bridge for the end-to-end run
// (tools/yougame_fake/e2e.py). Signs in to the stand-in site, prints "BRIDGE <url> <token>", then
// answers until stdin closes.
int serve(int argc, char** argv) {
  if (argc < 8) return 2;
  _putenv_s("MELEE_LAUNCHER_TEST", "1");
  _putenv_s("MELEE_YOUGAME_SITE", argv[2]);
  _putenv_s("MELEE_YOUGAME_AUTH", argv[2]);
  std::string why;
  if (!sign_in(argv[3], argv[4], &why)) { std::printf("SIGNIN FAILED %s\n", why.c_str()); return 3; }
  if (!start_bridge(&why)) { std::printf("BRIDGE FAILED %s\n", why.c_str()); return 4; }
  // The launcher's restart, recorded instead of done: a line the end-to-end run reads.
  set_restart_older([](const Allowed& older, const std::string& mode) {
    std::printf("RESTART %s %s\n", older.version.c_str(), mode.c_str());
    std::fflush(stdout);
    return std::string();
  });
  const std::string token = new_launch_token();
  auto wide = [](const char* s) { std::wstring w; for (; *s; ++s) w += (wchar_t)(unsigned char)*s; return w; };
  set_build(argv[5], wide(argv[6]), wide(argv[7]));
  refresh_allowed(argv[5], wide(argv[6]), wide(argv[7]));
  const Account a = account();
  std::printf("BRIDGE %s %s %s %s\n", bridge_url().c_str(), token.c_str(), a.name.c_str(), a.code.c_str());
  std::fflush(stdout);
  while (std::getchar() != EOF) {}
  return 0;
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--serve") return serve(argc, argv);
  // Parsing: incomplete, complete, oversize, chunked refused.
  {
    HttpRequest r;
    CHECK(parse_request("POST /sdk HTTP/1.1\r\nHost: x\r\n", r) == 0);
    CHECK(parse_request("POST /sdk HTTP/1.1\r\nContent-Length: 5\r\n\r\nab", r) == 0);
    CHECK(parse_request("POST /sdk HTTP/1.1\r\nContent-Length: 2\r\n\r\nab", r) == 1 && r.body == "ab");
    CHECK(parse_request("POST /sdk HTTP/1.1\r\nContent-Length: 99999999\r\n\r\n", r) == -1);
    CHECK(parse_request("POST /sdk HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", r) == -1);
    CHECK(parse_request("garbage\r\n\r\n", r) == -1);
  }
  Fake f;
  // Refusals: wrong token, browser origin, another host name, wrong path.
  {
    int status = 0;
    CHECK(!call(f, post(R"({"type":"hello"})", "nope"), &status).value("ok", true) && status == 401);
    HttpRequest r = post(R"({"type":"hello"})");
    r.headers["origin"] = "https://evil.example";
    CHECK(!call(f, r, &status).value("ok", true) && status == 403);
    CHECK(!call(f, post(R"({"type":"hello"})", "tok", "evil.example:50123"), &status).value("ok", true) && status == 403);
    HttpRequest g;
    CHECK(parse_request("GET / HTTP/1.1\r\nHost: 127.0.0.1:50123\r\n\r\n", g) == 1);
    CHECK(call(f, g).value("bridge", "") == "yougame");
  }
  // hello: the player when signed in, nothing when not.
  {
    const json j = call(f, post(R"({"type":"hello"})"));
    CHECK(j["ok"] == true && j["result"]["player"]["id"] == "user-1" && j["result"]["player"]["name"] == "Tester");
    CHECK(!j["result"].count("netBroker"));   // the game keeps its own networking
    f.signed_in = false;
    const json out = call(f, post(R"({"type":"hello"})"));
    CHECK(out["ok"] == true && !out["result"].count("player"));
    const json t = call(f, post(R"({"type":"mpTicket","players":2,"mode":"singles","ranked":true})"));
    CHECK(t["ok"] == false && t["code"] == "signed_out");
    f.signed_in = true;
  }
  // mpCode, mpRank, mpFriend, unknown.
  {
    CHECK(call(f, post(R"({"type":"mpCode"})"))["result"]["code"] == "TEST#123");
    CHECK(call(f, post(R"({"type":"mpRank","mode":"singles"})"))["result"]["rank"] == "Gold II");
    const json fr = call(f, post(R"({"type":"mpFriend"})"));
    CHECK(fr["ok"] == false && fr["error"] == "Enter a connect code");
    const json u = call(f, post(R"({"type":"directAttach"})"));
    CHECK(u["ok"] == false && u["code"] == "unsupported");
  }
  // mpTicket: the site body (slug, player, queue, build), the answer passed through, errors kept.
  {
    const json j = call(f, post(R"({"type":"mpTicket","players":2,"mode":"singles","ranked":true,
        "lobby":{"version":1,"minPlayers":2,"maxLocalPlayers":1,"compatibility":"mu-1.0.0"}})"));
    CHECK(j["ok"] == true && j["result"]["url"] == "wss://rooms.example/r" && j["result"]["ticket"] == "t-1");
    CHECK(!j["result"].count("secret_internal"));
    const json sent = json::parse(f.last_ticket);
    CHECK(sent["slug"] == "melee-unlocked" && sent["player_id"] == "user-1" && sent["queue"] == "ranked");
    CHECK(sent["build"]["exe_sha256"] == "aa" && sent["build"]["game_sha256"] == "bb" && sent["build"]["version"] == "1.0.0");
    CHECK(sent["lobby"]["compatibility"] == "mu-1.0.0" && !sent.count("direct"));
    const json d = call(f, post(R"({"type":"mpTicket","players":2,"mode":"singles","ranked":false,"direct":"FRND#7","afterMatch":"stay"})"));
    CHECK(d["ok"] == true);
    const json sent2 = json::parse(f.last_ticket);
    CHECK(sent2["queue"] == "casual" && sent2["direct"] == "FRND#7" && sent2["after_match"] == "stay");
    const json bad = call(f, post(R"({"type":"mpTicket","players":2,"mode":"singles","direct":"BAD#1"})"));
    CHECK(bad["ok"] == false && bad["code"] == "not_found" && bad["error"] == "No player has that code");
  }
  std::printf("launcher_yougame: ok\n");
  return 0;
}
