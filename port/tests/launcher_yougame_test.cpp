// The launcher's YouGame bridge without a network: request parsing, the token and origin checks,
// and each request type answered as the game's loopback client expects ({ok, result} or {ok:false}).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_yougame.h"
#include "launcher_yougame_app.h"
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
  bool has_open_connect = true;
  int open_connects = 0;
  std::string last_ticket;
  Backend backend() {
    Backend b;
    b.account = [this] { Account a; a.signed_in = signed_in; a.name = "Tester"; a.code = "TEST#123"; return a; };
    b.player_id = [] { return std::string("user-1"); };
    if (has_open_connect) b.open_connect = [this] { ++open_connects; return true; };
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

// --serve-web FOLDER VERSION EXE DLL [SCRIPT]: the real bridge over the launcher's browser view, on
// YouGame staging. FOLDER holds the view's session and session.ini (what the launcher keeps in
// launcher.ini): a session saved there is used as it is, as a launcher started again does; otherwise
// YouGame's sign-in page opens (run it on a desktop nobody sees) and SCRIPT, a file, types a test
// account into it. Prints "BRIDGE <url> <token> <name> <code>", then answers until stdin closes.
int serve_web(int argc, char** argv) {
  if (argc < 6) return 2;
  auto wide = [](const char* s) { std::wstring w; for (; *s; ++s) w += (wchar_t)(unsigned char)*s; return w; };
  const std::string folder = argv[2], ini = folder + "/session.ini";
  set_web_folder(wide(argv[2]));
  set_on_session_change([ini] {
    if (FILE* f = std::fopen(ini.c_str(), "wb")) { const std::string lines = ini_lines(); std::fwrite(lines.data(), 1, lines.size(), f); std::fclose(f); }
  });
  if (FILE* f = std::fopen(ini.c_str(), "rb")) {
    char line[8192];
    while (std::fgets(line, sizeof line, f)) {
      std::string l = line;
      while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
      load_line(l);
    }
    std::fclose(f);
  }
  std::string why;
  if (account().signed_in) {
    std::printf("REMEMBERED %s\n", account().code.c_str());
  } else {
    std::string script;
    if (argc > 6)
      if (FILE* f = std::fopen(argv[6], "rb")) { char buf[4096]; size_t n; while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) script.append(buf, n); std::fclose(f); }
    if (!web_sign_in_available()) { std::printf("SIGNIN FAILED no browser view on this computer\n"); return 3; }
    if (!sign_in_web(nullptr, &why, script)) { std::printf("SIGNIN FAILED %s\n", why.c_str()); return 3; }
    std::printf("SIGNED IN %s\n", account().code.c_str());
  }
  std::fflush(stdout);
  if (!start_bridge(&why)) { std::printf("BRIDGE FAILED %s\n", why.c_str()); return 4; }
  const std::string token = new_launch_token();
  set_build(argv[3], wide(argv[4]), wide(argv[5]));
  refresh_allowed(argv[3], wide(argv[4]), wide(argv[5]));
  const Account a = account();
  std::printf("BRIDGE %s %s %s %s\n", bridge_url().c_str(), token.c_str(), a.name.c_str(), a.code.c_str());
  std::fflush(stdout);
  while (std::getchar() != EOF) {}
  return 0;
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--serve-web") return serve_web(argc, argv);
  // --ask-app staging|production VERSION: asks the running YouGame app over its channel, as Connect does
  // (never opens it). Prints the state and which parts of the account came back, not the account itself.
  if (argc > 3 && std::string(argv[1]) == "--ask-app") {
    namespace app = launcher::yougame::app;
    const bool staging = std::string(argv[2]) == "staging";
    std::printf("installed %d running %d\n", app::installed(staging) ? 1 : 0, app::running(staging) ? 1 : 0);
    const app::Answer a = app::ask(staging, argv[3]);
    static const char* names[] = {"signed in", "signed out", "not running", "refused", "no answer"};
    std::printf("APP %s id %s name %s code %s why '%s'\n", names[a.state], a.player_id.empty() ? "no" : "yes",
                a.name.empty() ? "no" : "yes", a.code.empty() ? "no" : "yes", a.why.c_str());
    return a.state == app::Answer::kSignedIn ? 0 : 5;
  }
  if (argc > 2 && std::string(argv[1]) == "--sign-out-web") {   // the launcher's Disconnect: the folder goes
    auto wide = [](const char* s) { std::wstring w; for (; *s; ++s) w += (wchar_t)(unsigned char)*s; return w; };
    set_web_folder(wide(argv[2]));
    sign_out();
    return 0;
  }
  // The listing reads (native_build, pool) never reach a real site from these tests: nothing answers here.
#ifdef _WIN32
  _putenv_s("MELEE_LAUNCHER_TEST", "1");
  _putenv_s("MELEE_YOUGAME_SITE", "http://127.0.0.1:9");
#else
  setenv("MELEE_LAUNCHER_TEST", "1", 1);
  setenv("MELEE_YOUGAME_SITE", "http://127.0.0.1:9", 1);
#endif
  // Which rooms a release searches, from YouGame's game page: its newest Windows build's version while that
  // is one of YouGame's own builds from before 0.9.990, the shared key ("") from 0.9.990 on or when unknown.
  {
    auto rec = [](const char* id, const char* version, const char* platform) {
      return std::string("{\\\"buildId\\\":\\\"") + id + "\\\",\\\"version\\\":\\\"" + version + "\\\",\\\"platform\\\":\\\"" + platform + "\\\"}";
    };
    const char* a = "880b2eeb-0d00-4a00-8000-0000000e0131";
    const char* b = "ef5cf674-1b68-447f-9144-794abfd4eb1e";
    CHECK(pool_from_listing("") == "");
    CHECK(pool_from_listing("no builds here") == "");
    CHECK(pool_from_listing(rec(b, "0.9.218-online-test.122.30", "windows") + rec(a, "0.9.218-online-test.122.31", "windows") +
                            rec(a, "0.9.218-online-test.122.32", "linux")) == "0.9.218");   // Windows names the pool
    CHECK(pool_from_listing(rec(a, "0.9.218-online-test.122.31", "windows") + rec(b, "0.9.218-online-test.122.9", "windows")) ==
          "0.9.218");                                                                     // 31 is newer than 9
    CHECK(pool_from_listing(rec(a, "0.9.218-online-test.122.31", "windows") + rec(b, "0.9.990", "windows")) == "");          // our release listed
    CHECK(pool_from_listing(rec(a, "0.9.990-online-test.123.1", "windows")) == "");                                         // a build of 0.9.990
    CHECK(pool_from_listing(rec(a, "0.9.1001", "windows")) == "");
    CHECK(pool_from_listing(rec(a, "0.9.218 bad\"version", "windows")) == "");
    CHECK(pool_from_listing(rec(a, "0.9.218-online-test.122.31", "linux")) == "");
    // native_build: YouGame's record of this release for this platform, and never another build's.
    const char* app_w = "880b2eeb-0d00-4a00-8000-0000000e0135";
    const char* app_l = "880b2eeb-0d00-4a00-8000-0000000c0189";
    const std::string page = rec(a, "0.9.218-online-test.122.31", "windows") + rec(app_w, "0.9.220-online-test.122.39", "windows") +
                             rec(app_l, "0.9.220-online-test.122.39", "linux");
    CHECK(listed_builds(page).size() == 3 && listed_builds(page)[1].id == app_w && listed_builds(page)[2].platform == "linux");
    CHECK(native_build_from_listing(page, "0.9.990", "windows") == "");                 // not listed: no other build's record
    CHECK(native_build_from_listing(page, "0.9.220", "windows") == app_w);               // a build of this release
    CHECK(native_build_from_listing(page, "0.9.220", "linux") == app_l);
    CHECK(native_build_from_listing(page + rec(b, "0.9.990", "windows"), "0.9.990", "windows") == b);
    CHECK(native_build_from_listing(page + rec(b, "0.9.990-online-test.123.1", "windows"), "0.9.990", "windows") == b);
    CHECK(native_build_from_listing("", "0.9.990", "windows") == "");
    // A test build of the release is its own build: never the release's record, in either order on the page.
    const char* test_w = "880b2eeb-0d00-4a00-8000-0000000e0777";
    const std::string release = rec(b, "1.0.8", "windows"), test = rec(test_w, "1.0.8-test.4900950", "windows");
    CHECK(native_build_from_listing(release + test, "1.0.8", "windows") == b);
    CHECK(native_build_from_listing(test + release, "1.0.8", "windows") == b);
    CHECK(native_build_from_listing(test, "1.0.8", "windows") == "");                       // release not listed yet
    CHECK(native_build_from_listing(release + test, "1.0.8-test.4900950", "windows") == test_w);
    CHECK(native_build_from_listing(test + release, "1.0.8-test.4900950", "windows") == test_w);
  }
  // The YouGame app's hello answer, read as the game reads it (launcher_yougame_app.h): signed in with a
  // player id, pending while the app loads its player, signed out otherwise, refused when not ok.
  {
    namespace app = launcher::yougame::app;
    bool pending = true;
    app::Answer a = app::read_hello(json::parse(R"({"re":1,"ok":true,"result":{"player":{"id":"u-9","name":"Ann"},"nameShared":true}})"), &pending);
    CHECK(a.state == app::Answer::kSignedIn && a.player_id == "u-9" && a.name == "Ann" && !pending);
    a = app::read_hello(json::parse(R"({"re":1,"ok":true,"result":{"player":{"id":"u-9","name":"Player"}}})"), &pending);
    CHECK(a.state == app::Answer::kSignedIn && a.name.empty());   // name not shared: the code's tag is used
    a = app::read_hello(json::parse(R"({"re":1,"ok":true,"result":{"playerPending":true,"signedIn":true}})"), &pending);
    CHECK(a.state == app::Answer::kSignedOut && pending);
    a = app::read_hello(json::parse(R"({"re":1,"ok":true,"result":{"signedIn":false}})"), &pending);
    CHECK(a.state == app::Answer::kSignedOut && !pending);
    a = app::read_hello(json::parse(R"({"re":1,"ok":false,"code":"accountChanged"})"), &pending);
    CHECK(a.state == app::Answer::kRefused && a.why == "hello refused accountChanged");
    CHECK(app::read_hello(json(), &pending).state == app::Answer::kNoAnswer);
    // launcher.ini remembers whether this launcher connected through the app (1) or Disconnect declined it (0);
    // nothing of the app's account is kept.
    CHECK(load_line("yougame_app=0") && app_declined() && !app_used_before());
    CHECK(ini_lines().find("yougame_app=0") != std::string::npos);
    CHECK(load_line("yougame_app=1") && !app_declined() && app_used_before());
    CHECK(ini_lines().find("yougame_app=1") != std::string::npos);
    CHECK(load_line("yougame_app=x") && !app_declined() && !app_used_before());
    CHECK(ini_lines().find("yougame_app") == std::string::npos);
    CHECK(!account().signed_in && ini_lines().find("yougame_session") == std::string::npos);
  }
  // The YouGame site: production by default; launcher.ini yougame_env=staging is the only way to staging, and
  // production writes no yougame_env line until launcher.ini names a site.
  {
    CHECK(env().id == "production");
    CHECK(ini_lines().find("yougame_env") == std::string::npos);
    CHECK(load_line("yougame_env=staging") && env().id == "staging");
    CHECK(ini_lines().find("yougame_env=staging\n") != std::string::npos);
    CHECK(load_line("yougame_env=production") && env().id == "production");
    // A site named in launcher.ini stays named, so a computer with only the Staging app does not switch a player who chose production.
    CHECK(ini_lines().find("yougame_env=production\n") != std::string::npos);
    CHECK(load_line("yougame_env=staging") && load_line("yougame_env=Staging") && env().id == "production");
    CHECK(load_line("yougame_env=") && env().id == "production");
    set_env("staging");
    CHECK(env().id == "staging" && ini_lines().find("yougame_env=staging\n") != std::string::npos);
    set_env("anything else");
    CHECK(env().id == "production" && ini_lines().find("yougame_env=production\n") != std::string::npos);
  }
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
  // An older launcher may refuse best-effort match hints. Neither edge consumes a ticket.
  for (bool open : {true, false, true, false}) {
    const json hint = call(f, post(json{{"type", "matchState"}, {"open", open}}.dump()));
    CHECK(hint["ok"] == false && hint["code"] == "unsupported");
    CHECK(f.last_ticket.empty());
  }
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
  // muOpenConnect: a YouGame row pressed in the game while the launcher is not signed in opens the launcher's own
  // Connect. It needs only the launch token; a signed-in launcher has nothing to open.
  {
    int status = 0;
    CHECK(!call(f, post(R"({"type":"muOpenConnect"})", "nope"), &status).value("ok", true) && status == 401);
    CHECK(f.open_connects == 0);
    f.signed_in = false;
    const json opened = call(f, post(R"({"type":"muOpenConnect"})"));
    CHECK(opened["ok"] == true && opened["result"]["opening"] == true && f.open_connects == 1);
    f.has_open_connect = false;   // a launcher with no handler set: answered, nothing opens
    const json none = call(f, post(R"({"type":"muOpenConnect"})"));
    CHECK(none["ok"] == true && none["result"]["opening"] == false && f.open_connects == 1);
    f.has_open_connect = true;
    f.signed_in = true;
    const json already = call(f, post(R"({"type":"muOpenConnect"})"));
    CHECK(already["ok"] == true && already["result"]["signedIn"] == true && f.open_connects == 1);
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
  // A held room (the Doubles home, the Friends lobby): the launcher picks the 12-character code and asks
  // for the room's private ticket, as the YouGame app does. Room reports are accepted and go nowhere.
  {
    CHECK(new_room_code().size() == 12 && new_room_code() != new_room_code());
    for (char c : new_room_code()) CHECK((c >= 'a' && c <= 'z') || (c >= '2' && c <= '9'));
    // The Doubles home needs YouGame's own record of this release (its listing's buildId, native_build). The
    // test site lists nothing: the launcher says so and sends no ticket.
    const std::string before = f.last_ticket;
    const json home = call(f, post(R"({"type":"mpFriend","hold":true,"players":2,"compatibility":"doubles-lobby-1:mu"})"));
    CHECK(home["ok"] == false && home["error"].get<std::string>().find("opens once YouGame lists") != std::string::npos);
    CHECK(f.last_ticket == before);
    json sent;
    const json lobby = call(f, post(R"({"type":"mpFriend","hold":true,"players":4,"compatibility":"friends-lobby-1:mu"})"));
    CHECK(lobby["ok"] == true);
    sent = json::parse(f.last_ticket);
    CHECK(sent["mode"] == "singles" && !sent.count("teams_lobby") && sent["players"] == 4 && sent["queue"] == "private" &&
          sent["lobby"]["minPlayers"] == 1);
    CHECK(call(f, post(R"({"type":"mpFriend","hold":true,"players":3,"compatibility":"x"})"))["ok"] == false);
    CHECK(call(f, post(R"({"type":"mpRoom","code":"abcdefghjkmn","players":[]})"))["ok"] == true);
    // A ticket for a room named by its code is private; a search stays in its queue.
    CHECK(call(f, post(R"({"type":"mpTicket","players":2,"mode":"singles","ranked":false,"room":"abcdefghjkmn"})"))["ok"] == true);
    CHECK(json::parse(f.last_ticket)["queue"] == "private");
    CHECK(call(f, post(R"({"type":"mpTicket","players":2,"mode":"singles","ranked":false})"))["ok"] == true);
    CHECK(json::parse(f.last_ticket)["queue"] == "casual");
  }
  std::printf("launcher_yougame: ok\n");
  return 0;
}
