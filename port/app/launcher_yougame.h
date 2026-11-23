// YouGame in the launcher: the player's YouGame account, and the loopback bridge the game's YouGame rows use
// (YOUGAME_BRIDGE + YOUGAME_TOKEN + MELEE_YOUGAME_LAUNCHER=1). Connect takes, in this order, the sign-in saved in
// launcher.ini, the account of a running YouGame app (launcher_yougame_app.h), else YouGame's public game
// sign-in by code (OAuth 2.0 device flow, the game's own client id, scope "player"): the player enters a short
// code at yougame.co/link and the launcher gets a token for them. Requests: hello, mpCode, mpTicket, mpRank;
// mpFriend is answered here; muOpenConnect opens the launcher's own Connect (set_open_connect).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace launcher::yougame {

// The YouGame site the launcher talks to. Production unless launcher.ini says yougame_env=staging.
struct Env {
  std::string id;        // "production" or "staging"
  std::string site;      // https://yougame.co
};
const Env& env();
void set_env(const std::string& id);   // switching signs out

// launcher.ini: the environment, the game's sign-in client id per environment (read from YouGame), and the
// sign-in: the refresh token with who it is (the account's id, name and connect code), sealed for this Windows
// user (DPAPI; Linux keeps it as text in a launcher.ini only its owner reads). No password or cookie is kept.
bool load_line(const std::string& line);   // true when the line was this module's
std::string ini_lines();
// "YouGame extension": the game uses the YouGame app's channel instead of connecting itself. Off by default.
bool extension();
void set_extension(bool on);

struct Account {
  bool signed_in = false;
  bool app = false;   // signed in through the YouGame app (sign_in_app): the game plays YouGame through the app
  std::string name, code;
};
Account account();
void sign_out();
// The player is already signed in to the YouGame app for this environment (YouGame Staging on staging):
// the launcher asks the app over its own local channel (launcher_yougame_app.h) and takes that account,
// with no second sign-in. may_start: start the installed app quietly (its tray link, no window) when it is not
// running; else only a running app is asked. `version`: this build, named in the hello as the game does.
// Blocking (seconds; up to ~30 s when it starts the app): never on the window thread.
// kSignedIn: account() is the app's player (app = true). Otherwise nothing changed. *detail: every step taken.
enum class AppSignIn { kSignedIn, kSignedOut, kNoApp, kNoAnswer };
AppSignIn sign_in_app(bool may_start, const std::string& version, std::string* detail);
// Disconnect chose not to use the app's account: the launcher no longer takes it by itself when it starts
// (Connect takes it again). Saved in launcher.ini (yougame_app=0).
bool app_declined();
// This launcher connected through the YouGame app before (launcher.ini yougame_app=1): when it starts, it may
// start the installed app quietly (tray only) to take that account again.
bool app_used_before();
// "Launch with YouGame extension": the installed YouGame app (YouGame Staging on staging) is started quietly (its
// tray link, no window) when it is not running, so the game can reach its channel for the extension's features.
// Blocking up to ~25 s: never on the window thread. True when its channel is there; *detail says what happened.
bool start_app_quietly(std::string* detail);
// The extension's app is running or installed (so start_app_quietly can bring it up). Registry and pipe checks only.
bool app_expected();
// The extension's app is neither: the launcher's word to the player (it names the other environment's app when only
// that one is there); empty when the app is there. Registry and pipe checks only.
std::string extension_app_missing();
// Only the YouGame Staging app on this computer and launcher.ini names no YouGame site: the launcher (and
// the game, which reads launcher.ini) use YouGame Staging, as a tester with only that app wants (owner
// 2026-11-22). True when it switched; the caller saves launcher.ini. A yougame_env line always wins.
bool adopt_staging_app();
// This launcher's YouGame app is running (never the other environment's). Pipe check only.
bool app_running();
// YouGame's game sign-in by code. Blocking (minutes: the player has 10 minutes per code, and an expired code is
// replaced by a new one a few times); never on the window thread. on_code(code, link, open) is called (on this thread)
// each time YouGame hands out a code: link is where to enter it (https://yougame.co/link), open the same page
// with the code already in it (verification_uri_complete) for the browser. open carries the code: never log it. Test runs only:
// YOUGAME_TEST_CODE_SIGNIN, an absolute folder, gets sign-in-code.json {"link","code"} for each code, so an
// automated test can approve it. kSignedIn: account() is the player. *why (when given): the player's words when
// it ended otherwise (empty when cancelled). Neither the device code nor any token is ever logged.
enum class CodeSignIn { kSignedIn, kDenied, kExpired, kCancelled, kFailed };
CodeSignIn sign_in_code(const std::function<void(const std::string& code, const std::string& link, const std::string& open)>& on_code,
                        std::string* why);
// Ends a sign_in_code that is waiting (Back on the sign-in box): it returns kCancelled. Any thread.
void cancel_sign_in_code();
// True once after YouGame said the saved sign-in ended (the player ended it on YouGame, or it ran out): the
// launcher then asks for a new sign-in. The session-change callback fires when this becomes true.
bool take_expired();
// A game is about to start: checks the saved sign-in with YouGame (off this thread; a fresh access token and the
// account's connect code), so the game's first search does not wait for it. Nothing without a sign-in by code.
void warm();
// Called (from any thread) when the saved session changed: signed in or out, a refreshed token,
// a connect code. The launcher saves launcher.ini then.
void set_on_session_change(std::function<void()> callback);
// Called (from any thread) when YouGame handed out a new refresh token: it returns once launcher.ini holds it, so
// the new token is never used before it is saved (the old one stops working). Unset: set_on_session_change's.
void set_save_now(std::function<void()> callback);

// The Melee Unlocked page on the YouGame site (Open YouGame).
std::string game_page();
// The rooms this build searches on YouGame: the base version of the build YouGame lists while it is older
// than 0.9.990, "" for the shared key. Cached, refreshed in the background.
std::string pool_now();
// YouGame lists this build or a newer one (its game page was read and pool_now() is the shared key). Only then
// does a game started app-backed (the YouGame app's account) get its searches through: the app's tickets for a
// build YouGame does not list come back update_required. False before the page was read.
bool listed_current();
bool listing_known();   // YouGame's game page was read at least once (listed_current() means something)
// What Open YouGame opens: the installed YouGame app's link (yougame-staging:// on staging), else the
// site's download page (<site>/desktop).
std::string open_target();
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
// restart: this launcher can fetch that release and restart in it (it has YouGame, and is older than this one).
struct Allowed { std::string version, exe_sha256, game_sha256; bool restart = true; };
std::vector<Allowed> allowed_builds();   // empty: YouGame did not say (then nothing is offered)
void refresh_allowed(const std::string& current_version, const std::wstring& exe, const std::wstring& game_library);
// The version whose rooms the game searches, from YouGame's game page (its build records): the newest
// Windows build's version while that is older than 0.9.990, else "" (every release's shared key).
std::string pool_from_listing(const std::string& page);
// YouGame's build records on the game page, and the native_build a ticket names (launcher_yougame.cpp).
struct ListedBuild { std::string id, version, platform; };
std::vector<ListedBuild> listed_builds(const std::string& page);
std::string native_build_from_listing(const std::string& page, const std::string& version, const char* platform);
Allowed behind();                         // version empty: this build is allowed, or nothing is known
// The game asked to restart in the older release for a YouGame mode ("ranked" or "direct"). The
// launcher answers "" when it will start that release once the game has closed, else why not yet.
std::string file_sha256(const std::wstring& path);   // lowercase hex, "" when unreadable
void set_restart_older(std::function<std::string(const Allowed&, const std::string& mode)> handler);
// A YouGame row pressed in the game while the launcher is not signed in to YouGame (muOpenConnect): the launcher's
// Connect starts, or its sign-in window comes to the front. Called on the bridge's thread: post to the window.
void set_open_connect(std::function<void()> handler);

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
  std::function<std::string()> room_code;   // tests: the code of a held room; empty = a random one
  std::function<std::string(const std::string& mode)> restart_older;   // "" accepted, else why not
  std::function<bool()> open_connect;   // muOpenConnect: true when Connect was asked to open; may be empty
};
struct BridgeReply { int status = 200; std::string body; };
// One request to the bridge: token check, then hello / mpCode / mpTicket / mpRank / mpFriend / muOpenConnect.
std::string new_room_code();   // 12 characters, as YouGame's app names a held room
BridgeReply handle(const HttpRequest& request, const std::vector<std::string>& tokens, uint16_t port,
                   const Backend& backend, const std::string& build_json);

}  // namespace launcher::yougame
