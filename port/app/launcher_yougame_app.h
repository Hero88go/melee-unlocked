// The installed YouGame app as the launcher's sign-in: when the player is already signed in to the YouGame
// app (YouGame Staging on staging), the launcher learns who they are from the app's own local channel, the
// one a game started on its own uses (runtime/hle/yougame_channel.h: the same pipe, the same checks of the
// app's process and signature, the same `hello` and `mpCode` requests), and asks for no second sign-in.
// Nothing of the app is read from disk: no files, cookies, storage or tokens; only what the app answers.
// The session stays the app's: the launcher holds no token, so YouGame's requests for a game started this
// way go through the app (the game's app-backed mode, MELEE_YOUGAME_EXTENSION=1), not the launcher's bridge.
// Windows; elsewhere the app is never asked (stubs).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace launcher::yougame::app {

struct Answer {
  enum State {
    kSignedIn,    // the app answered with its player: id, name, code below
    kSignedOut,   // the app answered: nobody is signed in
    kNotRunning,  // no app channel for this user (the app is not running)
    kRefused,     // something answered and failed a check, or refused hello (`why`)
    kNoAnswer,    // the app did not answer in time (`why`)
  } state = kNotRunning;
  std::string player_id, name, code, why;
};

// What a hello answer ({"re":n,"ok":..,"result":{..}}) says, as the game reads it (yougame_link.cpp
// fetch_identity, yougame_signin.h hello_signed_out). *pending: signed in, the player not ready yet.
Answer read_hello(const nlohmann::json& answer, bool* pending);

// YouGame's start hook (third_party/yougame_start/yougame_start.h, YouGame's public header), asked on use, as
// the game asks it from its YouGame rows (yougame_link.cpp reach_ask_on_use): a running YouGame app, nothing
// shown; installed and not running, started quietly (tray only, its identity and signature checked by the hook);
// not on this computer, the hook's own small window ("Play Melee Unlocked online": Get YouGame / Play offline).
// Returns the hook's yg_start_result. Blocks while that window is up: never on the window thread.
// YG_START_UNAVAILABLE where the hook cannot run: not Windows, or this launcher runs as administrator (the hook
// would only show its "started as administrator" window there; the launcher's own sign-in is the way then).
// elevated(): this launcher holds rights the player's own programs don't ("Run as administrator": a full elevated token).
// start_get_progress(): after YG_START_GETTING (Get YouGame pressed), the hook's background install of the
// YouGame app in this process (yg_start_get_progress: YG_START_GET_DOWNLOADING .. READY or FAILED).
#if defined(_WIN32) && defined(_MSC_VER)
int start_hook(bool staging);
bool elevated();
int start_get_progress(int* percent);
#else
inline int start_hook(bool) { return 4; }   // YG_START_UNAVAILABLE: the hook runs on Windows only here
inline bool elevated() { return false; }
inline int start_get_progress(int*) { return 0; }   // YG_START_GET_NONE
#endif

#if (defined(_WIN32) && defined(_MSC_VER)) || defined(MELEE_LINUX)
// The YouGame app for this environment is installed (its link type is registered for this user or machine).
bool installed(bool staging);
// Its local channel is there for this Windows user (the app is running). Never connects.
bool running(bool staging);
// Starts the installed app quietly (its tray link: no window, only its tray icon) and waits up to `wait_ms`
// for its channel.
bool start(bool staging, int wait_ms);
// Connects to the running app's channel, says hello as this game (`version`), asks the connect code when
// signed in, and closes. Blocking (a few seconds at most, `timeout_ms` overall); never on a window thread.
Answer ask(bool staging, const std::string& version, int timeout_ms = 12000);
#else
inline bool installed(bool) { return false; }
inline bool running(bool) { return false; }
inline bool start(bool, int) { return false; }
inline Answer ask(bool, const std::string&, int = 12000) { return {}; }
#endif

}  // namespace launcher::yougame::app
