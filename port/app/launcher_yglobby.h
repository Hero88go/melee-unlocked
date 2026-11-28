// The Multiplayer Lobby page on YouGame: who is online (friends first, each with their rank badge and, when
// they are in a match, an eye to watch it), the live match bar, the three queues with how many are waiting
// and playing, and the public rooms and live games. Drawn by hand like the rest of the launcher (a child
// window over the content column, composed in a memory DC).
//
// YouGame does not yet answer its Social calls for a program the app did not start (the app says notYet),
// so the page shows honest empty states then. launcher.ini lobbypreview=1 (test builds) fills it with
// sample data instead, marked "Preview" in the header, to try the page.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

namespace launcher::yglobby {

// What YouGame says (or the sample data with lobbypreview=1). build: 0 vanilla, 1 Akaneia 1.0.1, 2 ACE 2.0.1, 3 another mod.
// main: the character's index (stock icon), -1 when YouGame does not say. rating -1: unranked.
struct Player { std::wstring name, code; int main = -1; bool in_match = false, friend_ = false; int rating = -1; int build = 0; std::string avatar;
                int link = 0;      // 0 not friends, 1 friends, 2 you asked them, 3 they asked you
                bool you = false; };   // this player's own row (owner 11-27: see yourself online, game open or not)
struct Room { std::wstring name, host, rules; std::vector<int> members; int mode = 0; int players = 1, size = 2; bool in_game = false; int viewers = 0; int build = 0; std::wstring code; };
struct Side { int main; std::wstring name; };
struct Live { std::vector<Side> a, b; int kind = 0; std::wstring what, game; int viewers = 0; int build = 0; std::wstring code; };
struct Data {
  bool known = false;
  int online = 0, in_match = 0, in_rooms = 0, waiting[3] = {}, playing[3] = {};
  std::vector<Player> players;
  std::vector<Room> rooms;
  std::vector<Live> live;
  Player me;
  bool me_known = false;
  bool me_listed = false;       // YouGame's players list had you in it (your game is open): Online counts you then
  bool players_known = false;   // the list of who is online came (it needs a sign-in, or the YouGame app)
  int me_matches = -1, me_wins = -1, me_losses = -1;   // ranked matches YouGame counted (-1 unknown); wins and losses when known
  std::wstring me_label;                               // YouGame's rank label ("Placements 0/5", "Gold II")
  int me_tier = -1;                                    // YouGame's own tier for you, 0 Bronze to 5 Challenger (-1 unplaced)
  std::vector<int> me_history;                         // your ratings as this launcher saw them, oldest first
};

struct Callbacks {
  std::function<void(int what)> play;    // 0 unranked, 1 ranked, 2 2v2, 3 a public room, 4 a private room: start the game
  std::function<void()> changed;         // a setting the launcher saves (Show Elo) changed
  std::string version;                   // this launcher's version, said in hello to the YouGame app
};
void init(HWND owner, const Callbacks& callbacks);
void open();    // shows the page over the content column (the launcher sizes the window for page 2)
void hide();
bool shown();

// Settings kept in launcher.ini by the launcher.
void set_preview(bool on);   // lobbypreview=1
bool preview();
void set_elo(bool on);       // lobbyelo=1: the rating under or beside each badge
bool elo();
// A typed or pasted room code as YouGame writes it (lowercase letters and digits), or empty when it can't be one
// (YouGame's codes are 6 to 12 of them).
std::wstring room_code(const std::wstring& typed);
// True when a button that starts the game or opens YouGame was pressed under a second ago (a double click).
bool repeat_press();
void settings_changed();
// YouGame sign-in by code (the device flow): the lobby's own sign-in, the YouGame app closed or not.
enum class CodeSignIn { kOff, kStarting, kWaiting, kSignedIn, kFailed };
CodeSignIn code_sign_in(std::wstring* user_code_or_reason);   // the code to show while waiting; why, when it failed
void start_code_sign_in();    // a new code; yougame.co/link opens with it filled in
void open_code_link();        // the link again (the code already shown)
void sign_out_code();
// A friend request (or accepting theirs) sent to YouGame with the sign-in by code; `done` gets what to tell the
// player, on another thread. Not signed in: the sign-in starts.
void friend_action(const std::string& username, bool accept, std::function<void(const std::wstring&)> done);
void fit();   // the launcher window changed size
void refresh();
bool me_pending();   // the YouGame app runs and its answer for you has not come yet   // a lobby setting changed outside the page (the skin): the launcher saves it
void set_my_main(int character, bool picked = false);   // main=N in launcher.ini when picked (-1: none); else a guess from your replays
bool main_picked();
int my_main();
bool pick_main(HWND owner, POINT screen);   // the character menu; true when the pick changed

// For another view of the same lobby (the Brawl Online skin): its data, how the YouGame source is doing
// (0 loading, 1 answering, 2 not opened to the launcher yet, 3 not reachable), and its actions.
Data current();
int source_state();
void attach_view(HWND view);   // the worker also runs while this window is in view, and posts WM_APP + 1 to it
void join(const std::wstring& code);
void watch(const std::wstring& code);
void create_room(bool is_public);
void play(int queue);
void open_yougame_page();
// The page's own art for other views: a rank badge by rating (? ring when unranked), a character's stock icon (a plain
// avatar for -1). Sizes in pixels.
void draw_badge(HDC dc, int rating, int x, int y, int px);
// Your own tier (0 Bronze to 5 Challenger, -1 none): YouGame's word for it when it sent one, else from the rating.
int my_tier(const Data& d);
void draw_tier_badge(HDC dc, int tier, int x, int y, int px);
void draw_stock(HDC dc, int main, int x, int y, int px);
bool draw_avatar(HDC dc, const std::string& url, int x, int y, int px);   // a round profile picture, once it has loaded
int tier_of_rating(int rating);   // -1 unranked, 0 bronze .. 5 challenger

}  // namespace launcher::yglobby
