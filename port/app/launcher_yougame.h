// YouGame in the launcher: the player's YouGame account, and the loopback bridge the game's YouGame
// rows use (YOUGAME_BRIDGE + YOUGAME_TOKEN + MELEE_YOUGAME_LAUNCHER=1). The YouGame app is never
// started: the launcher answers the game's few requests itself and passes tickets from YouGame's
// site through unchanged. Requests: hello, mpCode, mpTicket, mpRank; mpFriend is answered here.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace launcher::yougame {

// The YouGame site the launcher talks to. Staging unless launcher.ini says yougame_env=production.
struct Env {
  std::string id;        // "production" or "staging"
  std::string site;      // https://yougame.co
  std::string auth;      // YouGame's sign-in service
  std::string anon_key;  // the sign-in service's public key, as the site's own page uses it
  std::string cookie;    // the site's session cookie name
};
const Env& env();
void set_env(const std::string& id);   // switching signs out

// launcher.ini: the environment and the saved session (protected for this Windows user).
bool load_line(const std::string& line);   // true when the line was this module's
std::string ini_lines();

struct Account {
  bool signed_in = false;
  std::string name, code, email;
};
Account account();
// Blocking (call off the window thread): YouGame's own email sign-in, then the connect code.
bool sign_in(const std::string& email, const std::string& password, std::string* error);
void sign_out();
// Called (from any thread) when the saved session changed: signed in or out, a refreshed token,
// a connect code. The launcher saves launcher.ini then.
void set_on_session_change(std::function<void()> callback);

// The Melee Unlocked page on the YouGame site (Open YouGame).
std::string game_page();

// The bridge: started once, it answers until the launcher exits. A new launch token per game start;
// a token stays valid while that game may still be running.
bool start_bridge(std::string* error);
std::string bridge_url();
std::string new_launch_token();
// The files of the build about to start: their SHA-256 goes with every ticket (build check).
void set_build(const std::string& version, const std::wstring& exe, const std::wstring& game_library);

// YouGame on an older release. YouGame decides which builds its players use and may still be on an
// older release than this one. The launcher reads the builds YouGame allows (blocking; call off the
// window thread) and compares them with this installation's files. When this build is not allowed,
// behind() names the newest allowed GitHub release; the game then offers to restart in it.
struct Allowed { std::string version, exe_sha256, game_sha256; };
std::vector<Allowed> allowed_builds();   // empty: YouGame did not say (then nothing is offered)
void refresh_allowed(const std::string& current_version, const std::wstring& exe, const std::wstring& game_library);
Allowed behind();                         // version empty: this build is allowed, or nothing is known
// The game asked to restart in the older release for a YouGame mode ("ranked" or "direct"). The
// launcher answers "" when it will start that release once the game has closed, else why not yet.
std::string file_sha256(const std::wstring& path);   // lowercase hex, "" when unreadable
void set_restart_older(std::function<std::string(const Allowed&, const std::string& mode)> handler);

// ---- testable parts (port/tests/launcher_yougame_test.cpp) ----
struct HttpRequest {
  std::string method, path, body;
  std::map<std::string, std::string> headers;   // lower-case names
};
// Parses one HTTP/1.1 request. 0: incomplete, -1: refused (malformed or over 1 MB), 1: complete.
int parse_request(const std::string& data, HttpRequest& out);

// What the bridge needs from YouGame; the real one calls the site, tests pass their own.
struct Backend {
  std::function<Account()> account;
  std::function<std::string()> player_id;
  // Each returns the site's JSON answer, or throws std::runtime_error with the site's error text
  // (`code` from the answer kept as "code|message").
  std::function<std::string()> code;
  std::function<std::string(const std::string& mode)> rank;
  std::function<std::string(const std::string& body_json)> ticket;
  std::function<Allowed()> behind;   // may be empty
  std::function<std::string(const std::string& mode)> restart_older;   // "" accepted, else why not
};
struct BridgeReply { int status = 200; std::string body; };
// One request to the bridge: token check, then hello / mpCode / mpTicket / mpRank / mpFriend.
BridgeReply handle(const HttpRequest& request, const std::vector<std::string>& tokens, uint16_t port,
                   const Backend& backend, const std::string& build_json);

}  // namespace launcher::yougame
