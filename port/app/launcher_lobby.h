// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace launcher::lobby {
// An accepted match. mode is "vanilla", "akaneia", "ace" or "custom:<hash>"; a mod match runs on
// Static Recomp with mod_path (the mod's ISO from the Mods folder scan).
struct Match {
  std::string id, code; int character = 2; std::string build, opponent;
  std::string mode = "vanilla", mod_path, mod_name;
  // A peer-to-peer match (the build without the Slippi layer): the game's --p2p-* arguments, checked
  // and complete, and the result file the game writes for it. Empty for a Slippi Direct match.
  std::string p2p_args, p2p_result;
  // The game binds the lobby's own UDP port: the lobby has to be stopped before the game starts.
  bool p2p_lobby_port = false;
};
// A peer-to-peer match on the lobby's port. stop_for_match says "In game" to the players known,
// closes the lobby's socket and returns once it is closed (the DHT and the lobby are quiet for the
// length of the match; friends, profile and history stay). restart_after_match opens the lobby
// again on the same port and announces the player as before. Both do nothing for any other match.
void stop_for_match(const Match& match);
void restart_after_match();
// The build without the Slippi layer: the player's lobby name and the code made from it and the
// identity key, as the Play page shows them. Empty name: none chosen yet.
void p2p_player(std::string& name, std::string& code);
void set_account(const std::string& name, const std::string& code);
void init(HWND owner, const std::string& directory);
void open(const std::string& build, bool can_play);
void hide();
bool take_match(Match& match);
void game_running(bool running);
void shutdown();
void refresh_theme();

// The launcher calls this whenever it may have changed (a timer is fine): the Game Build, whether
// this PC can start a lobby match, and if not, a line saying what is missing. Changes are announced
// to other players at once, public lobby or not.
void set_game_state(const std::string& build, bool ready, const std::string& setup_hint);
// Reads the Mods folder scan (Mods/.cache/detected.json) from the first path that exists, when it
// changed. A missing file means no mods: nothing is shown and nothing fails.
void refresh_mods(const std::vector<std::string>& detected_json_paths, bool static_engine_ready);
// What the player is open to and their custom ISO, kept in launcher.ini by the launcher.
struct Prefs { std::string open_to = "vanilla", custom_iso, custom_name; };
void set_prefs(const Prefs& prefs);
Prefs prefs();
void on_prefs_changed(std::function<void()> callback);
// Main window timer: match and friend requests, notices and banners, also before the Lobby page
// was ever opened (friends can send requests to a player who never opened it this session).
void tick_ui();
void owner_resized();
void refresh_language();

// A banner across the top of the launcher window, over whichever page is showing: a title, a line
// or two of detail and up to three buttons. on_click gets the button's index; the banner closes
// first unless keep_open is set. Showing an id that is already up replaces its text in place.
struct BannerButton { std::string label; bool primary = false; };
void show_banner(const std::string& id, const std::string& title, const std::string& detail,
                 const std::vector<BannerButton>& buttons, std::function<void(int)> on_click,
                 int priority = 0, bool keep_open = false);
void close_banner(const std::string& id);
bool banner_shown(const std::string& id);
// Raises the launcher (restoring it from the taskbar) without stealing keyboard focus when Windows
// does not allow that, and flashes its taskbar button.
void bring_to_front();
// A line in lobby.log (the launcher folder). Never a raw IP address.
void log(const std::string& line);
}
