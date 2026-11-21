// Sign in by code (local-channel-contract.md section 8): Melee started on its own while the YouGame app
// runs signed out (maybe only in the tray). The game asks the app for a short code over the local
// channel, draws it with a QR, and the player approves it at yougame.co/link on any device. The game
// never holds a token. "Sign in on this PC" is v1's browser sign-in, unchanged.
//
//   ask      yg {"id":n,"type":"signIn","mode":"code"}       (or "mode":"browser")
//   answer   {"ok":true,"result":{"where":"code","code":{"user_code","verify_url","qr_text","expires_in"}}}
//            an app older than v2 answers {"ok":true,"result":{"opened":true,"where":"overlay"|"browser"}}
//            YouGame Online (the small app) shows the code's sign-in in its own window and says so: the code
//            answer carries "window":true, or an ask with no provider answers {"opened":true,"where":"window"}.
//            Nothing to draw then: "Finish signing in in YouGame Online" until signedIn, signInExpired or signInFailed.
//   refusal  notYet (show its words, offer the browser), busy + retryAfter (wait, then ask again),
//            alreadySignedIn (hello again), signInFailed (its words, Try again), badRequest (the browser instead)
//   pushes   signedIn {name?}, signInExpired, signInFailed; any other push type is skipped
//
// One code per app: a code still pending is drawn again from here, never asked for again. Asks happen
// only on the player's press (open with no live code, Get a new code, Try again, Sign in on this PC)
// or once when a busy wait ends, so a redraw never asks. Pure: the caller sends the asks this returns
// and passes back the answers and pushes (yougame_link.cpp), the overlay draws view()
// (port/runtime/gx/signin_overlay.cpp). Not thread-safe: the caller holds one lock around it.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <string>

namespace yougame::signin {

// ---- the words (plain and short; one clear next action per screen)
constexpr const char* kTitle = "Sign in to YouGame";
constexpr const char* kAsking = "Getting a code...";
constexpr const char* kGoTo = "Go to";               // "Go to yougame.co/link and enter ABCD-EFGH"
constexpr const char* kAndEnter = "and enter";
constexpr const char* kScan = "Or scan this with your phone.";
constexpr const char* kExpiresIn = "Code expires in";  // "Code expires in 9:41"
constexpr const char* kBrowserButton = "Sign in on this PC";
constexpr const char* kBack = "Back";
constexpr const char* kNewCode = "Get a new code";
constexpr const char* kTryAgain = "Try again";
constexpr const char* kExpired = "This code ran out of time.";
constexpr const char* kFailed = "That didn't work.";
constexpr const char* kNoAnswer = "YouGame didn't answer.";
constexpr const char* kBrowser = "Finish signing in in your browser";
constexpr const char* kWindow = "Finish signing in in the YouGame window";
constexpr const char* kWindowOnline = "Finish signing in in YouGame Online";   // the small app's own sign-in window
constexpr const char* kSignedIn = "Signed in";       // "Signed in as Ada"
constexpr const char* kSignedInAs = "Signed in as";
constexpr const char* kWait = "YouGame is busy. Trying again in";   // "... in 12 s"
constexpr const char* kNotYet = "Sign in with a code works only in a game YouGame checked. Sign in with your browser instead.";
// "Play online with YouGame" (Reach below): a YouGame row picked with no YouGame to reach.
constexpr const char* kReachTitle = "Play online with YouGame";
constexpr const char* kHomeTitle = "YouGame";
constexpr const char* kFeedback = "Send feedback";
constexpr const char* kHomeOnline = "Ranked, Unranked and Friends are in Online Play, on the main menu.";
constexpr const char* kFeedbackOpened = "The feedback page is open in your browser.";   // only when the answer says opened
constexpr const char* kFeedbackAlready = "The feedback page is already open.";
constexpr const char* kFeedbackNotOpened = "Couldn't open the feedback page. Try again in a minute.";
constexpr const char* kFeedbackFailed = "Couldn't open the feedback page.";
// "Open on YouGame" (Home): the full app on the game's page, or the site's game page with no app.
constexpr const char* kOpenOnYouGame = "Open on YouGame";
constexpr const char* kOpenedApp = "Opening YouGame on Melee Unlocked's page.";
constexpr const char* kOpenedSite = "Melee Unlocked's page is open in your browser.";
constexpr const char* kOpenFailed = "Couldn't open YouGame.";
constexpr const char* kLooking = "Looking for YouGame...";
constexpr const char* kAppClosed = "YouGame isn't running. Open YouGame to play online.";
constexpr const char* kNoApp = "Online play needs the YouGame app.";
constexpr const char* kAdmin = "Melee Unlocked was started as administrator, so it can't reach YouGame. Start it normally to play online.";
constexpr const char* kOpening = "Opening YouGame...";
constexpr const char* kOpenApp = "Open YouGame";
constexpr const char* kGetApp = "Get YouGame";
constexpr const char* kPlayOffline = "Play offline";   // the panel's dismiss, as YouGame's own window says it
constexpr const char* kDidntOpen = "YouGame didn't open.";
constexpr const char* kPageOpen = "The download page is open in your browser. Install YouGame, open it, then pick this again.";
// "Update to play online" (Update below): a search refused because this copy is out of date.
constexpr const char* kUpdateTitle = "Update to play online";
constexpr const char* kOutOfDate = "This copy of Melee is out of date";   // + " (the latest is 1.2.3)" + "."
constexpr const char* kUpdateIt = "Update it to play online.";
constexpr const char* kGetLatest = "Get the latest from YouGame, then play online.";   // a copy YouGame didn't install
constexpr const char* kOpenToUpdate = "Open YouGame to update it, then play online.";  // an app with no updateGame yet
constexpr const char* kUpdating = "Updating...";                                       // "Updating... 42%"
constexpr const char* kUpdateReady = "The update is ready.";                           // no version named
constexpr const char* kRestarting = "Restarting...";
constexpr const char* kRestartFailed = "Couldn't restart. Close the game and start it again.";
constexpr const char* kUpdateFailed = "The update didn't finish.";
constexpr const char* kLatest = "You have the latest. Try again.";
constexpr const char* kNoCheck = "YouGame couldn't check for updates. Try again.";
constexpr const char* kUpdate = "Update";
constexpr const char* kCancel = "Cancel";
constexpr const char* kRestartNow = "Restart now";

inline nlohmann::json code_request() { return {{"type", "signIn"}, {"mode", "code"}}; }
inline nlohmann::json browser_request() { return {{"type", "signIn"}, {"mode", "browser"}}; }

// The address as the player types it: https:// (and http://) left off, no trailing slash.
inline std::string display_url(std::string url) {
  for (const char* scheme : {"https://", "http://"})
    if (url.rfind(scheme, 0) == 0) { url = url.substr(std::string(scheme).size()); break; }
  while (!url.empty() && url.back() == '/') url.pop_back();
  return url;
}

// Off-contract types (a null, a number where a string goes) read as missing: nlohmann's value() would throw.
inline std::string text_of(const nlohmann::json& o, const char* key) {
  return o.is_object() && o.count(key) && o[key].is_string() ? o[key].get<std::string>() : std::string();
}
inline bool true_of(const nlohmann::json& o, const char* key) {
  return o.is_object() && o.count(key) && o[key].is_boolean() && o[key].get<bool>();
}

// A hello answer that says nobody is signed in: no player, not pending, and signedIn false (or, from an
// app that does not send signedIn, no word either way).
inline bool hello_signed_out(const nlohmann::json& result) {
  if (!result.is_object()) return false;
  if (result.count("player") && result["player"].is_object() && text_of(result["player"], "id") != "") return false;
  if (true_of(result, "playerPending")) return false;
  if (!result.count("signedIn")) return true;
  return result["signedIn"].is_boolean() && !result["signedIn"].get<bool>();
}

// "9:41"
inline std::string clock(int seconds) {
  if (seconds < 0) seconds = 0;
  std::string s = std::to_string(seconds / 60) + ":";
  const int r = seconds % 60;
  if (r < 10) s += "0";
  return s + std::to_string(r);
}

enum class Screen {
  Closed,
  Asking,     // Getting a code...
  Code,       // Go to <url> and enter <code>, the code large, the QR, the countdown
  Browser,    // Finish signing in in your browser (the browser was asked, or an app older than v2)
  Window,     // Finish signing in in the YouGame window (signed in, a step still owed in the app)
  Busy,       // YouGame is busy. Trying again in N s
  Expired,    // Get a new code
  Failed,     // Try again, Sign in on this PC
  NotYet,     // the app's words; Sign in on this PC
  SignedIn,   // Signed in as Ada, briefly, then the panel closes and the menu carries on
  // Reach: no YouGame to reach yet
  Looking,    // Looking for YouGame...
  AppClosed,  // YouGame isn't running: Open YouGame, Back
  NoApp,      // Online play needs the YouGame app: Get YouGame, Back
  Admin,      // run as administrator: Back
  Opening,    // Opening YouGame...: Back
  // Update: this copy is out of date
  UpdateOffered,      // out of date: Update, Play offline
  UpdateChecking,     // Updating... (updateGame asked, or asked again after not_checked): Cancel
  UpdateDownloading,  // Updating... 42%: Cancel, Play offline (the download carries on)
  UpdateReady,        // Version X is ready.: Restart now, Play offline
  UpdateRestarting,   // Restarting... (restartGame asked)
  UpdateRestartFailed,// Couldn't restart...: Play offline
  UpdateFailed,       // The update didn't finish. (or YouGame didn't answer.): Try again, Play offline
  UpdateNotAppCopy,   // a copy YouGame didn't install: get the latest from YouGame; Play offline
  UpdateOldApp,       // an app that answers updateGame with notYet: today's "Open YouGame to update it"
  UpdateLatest,       // You have the latest. Try again.: Back
  UpdateNoCheck,      // YouGame couldn't check for updates. Try again.: Try again, Play offline
  // Home: Shift+Tab, signed in, and the app drew no menu of its own (the small start app)
  Home,               // Signed in as Ada. Online Play is in the main menu.: Feedback, Open on YouGame, Back
};
enum class Ask { None, Code, Browser };
// What the panel offers, in order. The first is where the cursor starts.
enum class Button { None, NewCode, TryAgain, Browser, Back, OpenApp, GetApp, Offline, Update, Cancel, Restart, Feedback, OpenOnYouGame };

struct View {
  Screen screen = Screen::Closed;
  std::string title;     // empty: kTitle
  std::string user_code, verify_display, qr_text;
  std::string message;   // the line under the title (notYet's words, a failure's words, Signed in as Ada)
  int seconds = 0;       // Code: the code's time left; Busy: the wait left
  Button buttons[3] = {Button::None, Button::None, Button::None};
};

class Model {
 public:
  static constexpr uint64_t kSignedInShowMs = 2000;   // "Signed in as Ada" before the menu carries on
  static constexpr int kBusyMaxSeconds = 60;           // one ask per game per minute (the app's limit)

  bool is_open() const { return open_; }
  Screen screen() const { return screen_; }

  // The player needs to sign in (a YouGame row picked while signed out). A code still pending is drawn
  // again; an ask still on its way is waited for; otherwise one ask for a code.
  Ask open(uint64_t now) {
    open_ = true;
    if (screen_ == Screen::SignedIn || screen_ == Screen::Window || screen_ == Screen::Browser) return Ask::None;
    if (in_flight_) { screen_ = Screen::Asking; return Ask::None; }
    if (!code_.empty() && now < expires_at_) { screen_ = Screen::Code; return Ask::None; }
    if (screen_ == Screen::Busy && now < retry_at_) return Ask::None;
    return ask(Ask::Code);
  }
  // Back: the panel closes. A pending code stays the app's (and ours, to draw again on the next open).
  void close() { open_ = false; if (screen_ != Screen::Code && screen_ != Screen::Busy) screen_ = Screen::Closed; }

  Ask press(Button b, uint64_t now) {
    if (b != Button::Back && b != Button::None && in_flight_) { code_.clear(); screen_ = Screen::Asking; return Ask::None; }
    switch (b) {
      case Button::NewCode:
      case Button::TryAgain:
        code_.clear();
        if (now < retry_at_) { screen_ = Screen::Busy; retry_ask_ = Ask::Code; return Ask::None; }
        return ask(Ask::Code);
      case Button::Browser:
        code_.clear();
        if (now < retry_at_) { screen_ = Screen::Busy; retry_ask_ = Ask::Browser; return Ask::None; }
        return ask(Ask::Browser);
      case Button::Back: close(); return Ask::None;
      case Button::OpenApp:
      case Button::GetApp:
      case Button::Offline:   // the Play online and Update panels', never this panel's
      case Button::Update:
      case Button::Cancel:
      case Button::Restart:
      case Button::Feedback:
      case Button::OpenOnYouGame:
      case Button::None: break;
    }
    return Ask::None;
  }

  // The answer to `asked`: the whole answer line's object ({ok, result | error, code, retryAfter}), or
  // anything else for no answer. Returns the next ask to send at once (badRequest: the browser).
  Ask answered(Ask asked, const nlohmann::json& answer, uint64_t now) {
    in_flight_ = false;
    if (!answer.is_object()) return fail(kNoAnswer);
    if (true_of(answer, "ok")) {
      const nlohmann::json result = answer.count("result") && answer["result"].is_object() ? answer["result"] : nlohmann::json::object();
      // YouGame Online signs the player in in its own window: no code or QR here, just wait for its push.
      online_window_ = (asked == Ask::Code && true_of(result, "window")) || text_of(result, "where") == "window";
      if (online_window_) { code_.clear(); screen_ = Screen::Window; return Ask::None; }
      if (asked == Ask::Code && text_of(result, "where") == "code" && result.count("code") && result["code"].is_object()) {
        const nlohmann::json& c = result["code"];
        const std::string user = c.count("user_code") && c["user_code"].is_string() ? c["user_code"].get<std::string>() : "";
        if (!user.empty()) {
          code_ = user;
          verify_ = display_url(c.count("verify_url") && c["verify_url"].is_string() ? c["verify_url"].get<std::string>() : "");
          qr_ = c.count("qr_text") && c["qr_text"].is_string() ? c["qr_text"].get<std::string>() : "";
          double secs = c.count("expires_in") && c["expires_in"].is_number() ? c["expires_in"].get<double>() : 600;
          if (!(secs >= 1)) secs = 1;
          if (secs > 3600) secs = 3600;
          expires_at_ = now + (uint64_t)(secs * 1000);
          screen_ = Screen::Code;
          return Ask::None;
        }
      }
      // The browser sign-in ran: asked for, or an app older than v2 that ignores the mode (no code).
      // Since YouGame efdafa604 the browser answer carries the /link page's code too: shown under the
      // line in case the browser opened behind the game.
      code_.clear();
      if (asked == Ask::Browser && result.count("code") && result["code"].is_object()) {
        const nlohmann::json& c = result["code"];
        if (c.count("user_code") && c["user_code"].is_string()) code_ = c["user_code"].get<std::string>();
      }
      screen_ = text_of(result, "where") == "overlay" ? Screen::Window : Screen::Browser;
      return Ask::None;
    }
    const std::string code = answer.count("code") && answer["code"].is_string() ? answer["code"].get<std::string>() : "";
    const std::string words = answer.count("error") && answer["error"].is_string() ? answer["error"].get<std::string>() : "";
    if (code == "alreadySignedIn") { rehello_ = true; return signed_in("", now); }
    if (code == "busy") {
      retry_at_ = now + wait_ms(answer, kBusyMaxSeconds);
      retry_ask_ = asked;
      screen_ = Screen::Busy;
      return Ask::None;
    }
    if (code == "notYet") { message_ = words.empty() ? kNotYet : words; screen_ = Screen::NotYet; return Ask::None; }
    if (code == "badRequest" && asked == Ask::Code) return ask(Ask::Browser);   // an app that knows no code mode
    if (code == "signInFailed" && answer.count("retryAfter")) retry_at_ = now + wait_ms(answer, kBusyMaxSeconds);
    // The app's own words say what went wrong (no WebView2 on this PC, say); the generic line only when it sent none.
    return fail(words.empty() ? kFailed : words);
  }

  // A push on the channel. True when it was one of ours (the caller then says hello again on signedIn).
  bool push(const nlohmann::json& push, uint64_t now) {
    if (!push.is_object()) return false;
    const std::string type = push.count("type") && push["type"].is_string() ? push["type"].get<std::string>() : "";
    if (type == "signedIn") {
      rehello_ = true;
      signed_in(push.count("name") && push["name"].is_string() ? push["name"].get<std::string>() : "", now);
      return true;
    }
    if (type == "signInExpired") {
      code_.clear();
      if (screen_ == Screen::Code || screen_ == Screen::Asking || screen_ == Screen::Closed ||
          (screen_ == Screen::Window && online_window_))
        screen_ = Screen::Expired;
      online_window_ = false;
      return true;
    }
    if (type == "signInFailed") {
      code_.clear();
      const std::string words = text_of(push, "error");
      if (screen_ != Screen::SignedIn) fail(words.empty() ? kFailed : words);
      return true;
    }
    return false;
  }
  // signedIn or alreadySignedIn: the caller says hello again once. True once per such answer.
  bool take_rehello() { const bool r = rehello_; rehello_ = false; return r; }
  // What the hello after signedIn found. No player: a step is still owed in the app's window.
  void rehello_result(bool player, const std::string& name, uint64_t now) {
    if (player) {
      if (screen_ != Screen::SignedIn) signed_in(name, now);
      else if (name_.empty() && !name.empty()) name_ = name;
    } else if (screen_ == Screen::SignedIn || screen_ == Screen::Closed) {
      online_window_ = false;   // signed in, a step still owed in the full app's window
      screen_ = Screen::Window;
    }
  }
  // The identity arrived another way (the 5 s re-ask, another sign-in) while the panel was up.
  void player_known(const std::string& name, uint64_t now) {
    if (open_ && screen_ != Screen::SignedIn) signed_in(name, now);
  }

  // Once per frame: the countdown, the busy wait's end (its one ask), the Signed in screen's end.
  Ask tick(uint64_t now) {
    if (!code_.empty() && now >= expires_at_) {
      code_.clear();
      if (screen_ == Screen::Code) screen_ = Screen::Expired;
    }
    if (screen_ == Screen::Busy && now >= retry_at_ && open_) {
      const Ask a = retry_ask_;
      retry_ask_ = Ask::None;
      return ask(a == Ask::None ? Ask::Code : a);
    }
    if (screen_ == Screen::SignedIn && now >= shown_until_) { open_ = false; screen_ = Screen::Closed; }
    return Ask::None;
  }

  View view(uint64_t now) const {
    View v;
    v.screen = open_ ? screen_ : Screen::Closed;
    v.message = message_;
    switch (v.screen) {
      case Screen::Code:
        v.user_code = code_;
        v.verify_display = verify_;
        v.qr_text = qr_;
        v.seconds = expires_at_ > now ? (int)((expires_at_ - now + 999) / 1000) : 0;
        v.buttons[0] = Button::Browser; v.buttons[1] = Button::Back;
        break;
      case Screen::Busy:
        v.seconds = retry_at_ > now ? (int)((retry_at_ - now + 999) / 1000) : 0;
        v.buttons[0] = Button::Back;
        break;
      case Screen::Expired: v.buttons[0] = Button::NewCode; v.buttons[1] = Button::Browser; v.buttons[2] = Button::Back; break;
      case Screen::Failed: v.buttons[0] = Button::TryAgain; v.buttons[1] = Button::Browser; v.buttons[2] = Button::Back; break;
      case Screen::NotYet: v.buttons[0] = Button::Browser; v.buttons[1] = Button::Back; break;
      case Screen::Browser: v.user_code = code_; v.buttons[0] = Button::Back; break;
      case Screen::Window:
        v.message = online_window_ ? kWindowOnline : "";   // empty: the overlay says kWindow
        v.buttons[0] = Button::Back;
        break;
      case Screen::SignedIn:
        v.message = name_.empty() ? std::string(kSignedIn) : std::string(kSignedInAs) + " " + name_;
        break;
      case Screen::Asking: v.buttons[0] = Button::Back; break;
      case Screen::Looking:
      case Screen::AppClosed:
      case Screen::NoApp:
      case Screen::Admin:
      case Screen::Opening:
      case Screen::UpdateOffered:
      case Screen::UpdateChecking:
      case Screen::UpdateDownloading:
      case Screen::UpdateReady:
      case Screen::UpdateRestarting:
      case Screen::UpdateRestartFailed:
      case Screen::UpdateFailed:
      case Screen::UpdateNotAppCopy:
      case Screen::UpdateOldApp:
      case Screen::UpdateLatest:
      case Screen::UpdateNoCheck:
      case Screen::Home:
      case Screen::Closed: break;
    }
    return v;
  }

 private:
  Ask ask(Ask a) {
    if (a == Ask::None) return a;
    in_flight_ = true;
    online_window_ = false;
    screen_ = Screen::Asking;
    return a;
  }
  Ask fail(const std::string& words) { message_ = words; online_window_ = false; screen_ = Screen::Failed; return Ask::None; }
  Ask signed_in(const std::string& name, uint64_t now) {
    code_.clear();
    online_window_ = false;
    name_ = name;
    screen_ = Screen::SignedIn;
    shown_until_ = now + kSignedInShowMs;
    return Ask::None;
  }
  static uint64_t wait_ms(const nlohmann::json& answer, int max_seconds) {
    double s = answer.count("retryAfter") && answer["retryAfter"].is_number() ? answer["retryAfter"].get<double>() : max_seconds;
    if (!(s >= 1)) s = 1;
    if (s > max_seconds) s = max_seconds;
    return (uint64_t)(s * 1000);
  }

  bool open_ = false, in_flight_ = false, rehello_ = false;
  bool online_window_ = false;   // Window: YouGame Online's own sign-in window has the code (window:true / where:"window")
  Screen screen_ = Screen::Closed;
  std::string code_, verify_, qr_, message_, name_;
  uint64_t expires_at_ = 0, retry_at_ = 0, shown_until_ = 0;
  Ask retry_ask_ = Ask::None;
};

// "Play online with YouGame": Ranked, Unranked or Friends picked while no YouGame is connected. The
// same panel as sign-in (signin_overlay.cpp) says why the row can't start and offers the one next step:
// Open YouGame (installed, not running), Get YouGame (not installed: YouGame's download page) or, run
// as administrator, starting the game normally. Pure; yougame_link.cpp looks, opens and holds the lock.
class Reach {
 public:
  bool is_open() const { return screen_ != Screen::Closed; }
  Screen screen() const { return screen_; }
  void open(bool admin) { screen_ = admin ? Screen::Admin : Screen::Looking; note_.clear(); page_ = failed_ = false; }
  // What the look found (a closed panel stays closed).
  void looked(bool running, bool installed) {
    if (screen_ == Screen::Looking) screen_ = running ? Screen::Opening : installed ? Screen::AppClosed : Screen::NoApp;
  }
  void opening() { if (is_open()) screen_ = Screen::Opening; }
  // Open YouGame (or a running YouGame's channel) did not come up: say so, offer Open again and Get.
  // Looking too: a panel reopened while that wait was still running started no look of its own.
  void open_failed(const char* why) {
    if (screen_ != Screen::Opening && screen_ != Screen::Looking) return;
    screen_ = Screen::AppClosed;
    note_ = why ? why : kDidntOpen;
    failed_ = true;
  }
  void page_opened() { if (is_open()) page_ = true; }
  void close() { screen_ = Screen::Closed; }

  View view() const {
    View v;
    v.screen = screen_;
    v.title = kReachTitle;
    switch (screen_) {
      case Screen::Looking: v.message = kLooking; v.buttons[0] = Button::Offline; break;
      case Screen::Opening: v.message = kOpening; v.buttons[0] = Button::Offline; break;
      case Screen::Admin: v.message = kAdmin; v.buttons[0] = Button::Offline; break;
      case Screen::AppClosed:
        v.message = failed_ ? note_ + " Open YouGame to try again, or get the latest YouGame." : std::string(kAppClosed);
        v.buttons[0] = Button::OpenApp;
        if (failed_) { v.buttons[1] = Button::GetApp; v.buttons[2] = Button::Offline; }
        else v.buttons[1] = Button::Offline;
        break;
      case Screen::NoApp:
        v.message = kNoApp;
        v.buttons[0] = Button::GetApp; v.buttons[1] = Button::Offline;
        break;
      default: v.screen = Screen::Closed; break;
    }
    if (page_ && v.screen != Screen::Closed) v.message += std::string("\n\n") + kPageOpen;
    return v;
  }

 private:
  Screen screen_ = Screen::Closed;
  std::string note_;
  bool page_ = false, failed_ = false;
};

// The YouGame box for Shift+Tab when the player is signed in and the app drew no menu over the game
// (the small start app has no overlay): who is signed in, where online play starts, Send feedback
// (the app opens the game's feedback page), Open on YouGame and Back. Esc and the pad's B are Back.
// Pure; yougame_link.cpp holds the lock.
class Home {
 public:
  enum class Opened { App, Site, Failed };
  bool is_open() const { return open_; }
  void open(const std::string& name) { open_ = true; name_ = name; note_.clear(); }
  // openFeedback's whole answer line ({ok, result: {opened, alreadyOpen?}} | {ok:false, error, code}), or
  // anything else for no answer. "Open in your browser" only when the answer says it opened.
  void feedback_answer(const nlohmann::json& answer) {
    if (!answer.is_object()) { note_ = kFeedbackFailed; return; }
    const nlohmann::json result = answer.count("result") && answer["result"].is_object() ? answer["result"] : nlohmann::json::object();
    if (true_of(answer, "ok")) note_ = true_of(result, "opened") ? kFeedbackOpened : true_of(result, "alreadyOpen") ? kFeedbackAlready : kFeedbackNotOpened;
    else note_ = short_words(text_of(answer, "error"), kFeedbackFailed);
  }
  void opened(Opened how) { note_ = how == Opened::App ? kOpenedApp : how == Opened::Site ? kOpenedSite : kOpenFailed; }
  void close() { open_ = false; }
  View view() const {
    View v;
    if (!open_) return v;
    v.screen = Screen::Home;
    v.title = kHomeTitle;
    v.message = (name_.empty() ? std::string("Signed in to YouGame.") : "Signed in as " + name_ + ".") + "\n" + kHomeOnline;
    if (!note_.empty()) v.message += "\n\n" + note_;
    v.buttons[0] = Button::Feedback; v.buttons[1] = Button::OpenOnYouGame; v.buttons[2] = Button::Back;
    return v;
  }
 private:
  // The app's own words for one line: printable ASCII and under 100 bytes, else `fallback`.
  static std::string short_words(const std::string& words, const char* fallback) {
    if (words.empty() || words.size() >= 100) return fallback;
    for (const char c : words) if (c < 0x20 || c > 0x7E) return fallback;
    return words;
  }
  bool open_ = false;
  std::string name_, note_;
};

// "Update to play online": a copy of Melee started on its own, connected to YouGame over the local channel,
// whose search was refused as out of date (old_build or build_replaced; never unknown_build, which follows an
// upload for a moment). The same panel offers Update; the app downloads the latest build for a copy it
// installed and relaunches it in its own window after Restart now (no website, no app window).
//
//   ask      updateGame   -> ok {state:"ready",version} | ok {state:"downloading"}
//                            | not_app_copy | up_to_date | not_checked (+retryAfter: ask again, twice at most)
//   ask      cancelUpdate -> ok {}
//   ask      restartGame  -> ok {version} (then this game quits cleanly) | not_ready | unavailable (stay)
//   push     gameUpdate {state:"downloading",percent} | {state:"ready",version} | {state:"failed",error}
//
// Pure, like Model: the caller sends the steps this returns from its own thread, passes back answers and
// pushes, and quits on Step::Exit. Not thread-safe: the caller holds one lock around it.
class Update {
 public:
  enum class Step { None, Update, Cancel, Restart, Exit };
  static constexpr int kNotCheckedRetries = 2;

  static nlohmann::json update_request() { return {{"type", "updateGame"}}; }
  static nlohmann::json cancel_request() { return {{"type", "cancelUpdate"}}; }
  static nlohmann::json restart_request() { return {{"type", "restartGame"}}; }
  static nlohmann::json request(Step s) {
    return s == Step::Cancel ? cancel_request() : s == Step::Restart ? restart_request() : update_request();
  }
  // A refusal this panel answers: the copy is known and out of date. unknown_build is not one.
  static bool out_of_date(const nlohmann::json& answer) {
    const std::string code = text_of(answer, "code");
    return answer.is_object() && !true_of(answer, "ok") && (code == "old_build" || code == "build_replaced");
  }
  // A version as the app or the site names it, for one line: printable, no spaces, under 40 bytes.
  static std::string version_text(const nlohmann::json& o, const char* key) {
    std::string out;
    for (const char c : text_of(o, key))
      if (c > 0x20 && c < 0x7F && out.size() < 40) out += c;
    return out;
  }

  bool is_open() const { return open_; }
  Screen screen() const { return screen_; }

  // The refused search's answer. An update already on its way is shown again as it stands.
  void offer(const nlohmann::json& refusal) {
    open_ = true;
    const std::string latest = version_text(refusal, "current");
    if (!latest.empty()) latest_ = latest;
    if (in_progress()) return;
    // The app already said it didn't install this copy: it won't this run either, so no Update to press.
    screen_ = not_app_copy_ ? Screen::UpdateNotAppCopy : Screen::UpdateOffered;
  }
  void close() { open_ = false; }

  Step press(Button b, uint64_t now) {
    switch (b) {
      case Button::Update:
      case Button::TryAgain:
        if (screen_ != Screen::UpdateOffered && screen_ != Screen::UpdateFailed && screen_ != Screen::UpdateNoCheck) break;
        retries_ = 0;
        return ask(now);
      case Button::Cancel:
        if (screen_ != Screen::UpdateChecking && screen_ != Screen::UpdateDownloading) break;
        started_ = false;
        retry_at_ = 0;
        screen_ = Screen::UpdateOffered;
        return Step::Cancel;
      case Button::Restart:
        if (screen_ != Screen::UpdateReady) break;
        screen_ = Screen::UpdateRestarting;
        return Step::Restart;
      case Button::Back:
      case Button::Offline: close(); break;
      default: break;
    }
    return Step::None;
  }

  // The answer to `asked`: the whole answer object, or anything else for no answer.
  Step answered(Step asked, const nlohmann::json& answer, uint64_t now) {
    const bool ok = answer.is_object() && true_of(answer, "ok");
    const std::string code = text_of(answer, "code");
    const nlohmann::json result = ok && answer.count("result") && answer["result"].is_object() ? answer["result"] : nlohmann::json::object();
    if (asked == Step::Cancel) return Step::None;   // the panel went back to Update already
    if (asked == Step::Restart) {
      if (screen_ != Screen::UpdateRestarting) return Step::None;
      if (ok) return Step::Exit;                    // the app starts the new build once this game is gone
      screen_ = Screen::UpdateRestartFailed;        // not_ready, unavailable, no answer: stay
      return Step::None;
    }
    if (asked != Step::Update || screen_ != Screen::UpdateChecking) return Step::None;   // cancelled meanwhile
    if (!answer.is_object()) return fail(kNoAnswer);
    if (ok) {
      const std::string state = text_of(result, "state");
      if (state == "ready") { ready(version_text(result, "version")); return Step::None; }
      if (state == "downloading") { screen_ = Screen::UpdateDownloading; return Step::None; }
      return fail(kUpdateFailed);
    }
    if (code == "not_checked") {
      if (retries_ >= kNotCheckedRetries) { started_ = false; screen_ = Screen::UpdateNoCheck; return Step::None; }
      ++retries_;
      retry_at_ = now + wait_ms(answer);
      return Step::None;                            // tick() asks again when the wait ends
    }
    started_ = false;
    if (code == "not_app_copy") { not_app_copy_ = true; screen_ = Screen::UpdateNotAppCopy; return Step::None; }
    if (code == "up_to_date") { screen_ = Screen::UpdateLatest; return Step::None; }
    if (code == "notYet") { screen_ = Screen::UpdateOldApp; return Step::None; }   // an app with no updateGame
    return fail(kUpdateFailed);
  }

  // A push on the channel. True when it was ours (gameUpdate).
  bool push(const nlohmann::json& p) {
    if (text_of(p, "type") != "gameUpdate") return false;
    if (!started_) return true;                     // none asked for, or cancelled: nothing to show
    const std::string state = text_of(p, "state");
    if (state == "downloading") {
      if (screen_ == Screen::UpdateChecking || screen_ == Screen::UpdateDownloading) {
        screen_ = Screen::UpdateDownloading;
        retry_at_ = 0;
        if (p.count("percent") && p["percent"].is_number()) {
          const double v = p["percent"].get<double>();
          percent_ = v >= 0 && v <= 100 ? (int)v : v > 100 ? 100 : 0;   // NaN reads as 0
        }
      }
    } else if (state == "ready") {
      if (screen_ != Screen::UpdateRestarting) ready(version_text(p, "version"));
    } else if (state == "failed") {
      if (screen_ != Screen::UpdateRestarting && screen_ != Screen::UpdateReady) fail(kUpdateFailed);
    }
    return true;
  }

  // Once per frame while open: a not_checked wait that ended asks again.
  Step tick(uint64_t now) {
    if (screen_ == Screen::UpdateChecking && retry_at_ && now >= retry_at_) { retry_at_ = 0; return Step::Update; }
    return Step::None;
  }

  View view() const {
    View v;
    v.title = kUpdateTitle;
    v.screen = open_ ? screen_ : Screen::Closed;
    const std::string out_of_date = std::string(kOutOfDate) + (latest_.empty() ? "" : " (the latest is " + latest_ + ")") + ".";
    switch (v.screen) {
      case Screen::UpdateOffered:
        v.message = out_of_date + " " + kUpdateIt;
        v.buttons[0] = Button::Update; v.buttons[1] = Button::Offline;
        break;
      case Screen::UpdateChecking: v.message = kUpdating; v.buttons[0] = Button::Cancel; break;
      case Screen::UpdateDownloading:
        v.message = percent_ >= 0 ? std::string(kUpdating) + " " + std::to_string(percent_) + "%" : std::string(kUpdating);
        v.buttons[0] = Button::Cancel; v.buttons[1] = Button::Offline;
        break;
      case Screen::UpdateReady:
        v.message = version_.empty() ? std::string(kUpdateReady) : "Version " + version_ + " is ready.";
        v.buttons[0] = Button::Restart; v.buttons[1] = Button::Offline;
        break;
      case Screen::UpdateRestarting: v.message = kRestarting; break;
      case Screen::UpdateRestartFailed: v.message = kRestartFailed; v.buttons[0] = Button::Offline; break;
      case Screen::UpdateFailed:
        v.message = message_;
        v.buttons[0] = Button::TryAgain; v.buttons[1] = Button::Offline;
        break;
      case Screen::UpdateNotAppCopy: v.message = out_of_date + " " + kGetLatest; v.buttons[0] = Button::Offline; break;
      case Screen::UpdateOldApp: v.message = out_of_date + " " + kOpenToUpdate; v.buttons[0] = Button::Offline; break;
      case Screen::UpdateLatest: v.message = kLatest; v.buttons[0] = Button::Back; break;
      case Screen::UpdateNoCheck:
        v.message = kNoCheck;
        v.buttons[0] = Button::TryAgain; v.buttons[1] = Button::Offline;
        break;
      default: v.screen = Screen::Closed; break;
    }
    return v;
  }

 private:
  bool in_progress() const {
    return screen_ == Screen::UpdateChecking || screen_ == Screen::UpdateDownloading ||
           screen_ == Screen::UpdateReady || screen_ == Screen::UpdateRestarting;
  }
  Step ask(uint64_t) {
    started_ = true;
    percent_ = -1;
    retry_at_ = 0;
    screen_ = Screen::UpdateChecking;
    return Step::Update;
  }
  void ready(const std::string& version) {
    if (!version.empty()) version_ = version;
    retry_at_ = 0;
    screen_ = Screen::UpdateReady;
  }
  Step fail(const char* words) {
    started_ = false;
    retry_at_ = 0;
    message_ = words;
    screen_ = Screen::UpdateFailed;
    return Step::None;
  }
  static uint64_t wait_ms(const nlohmann::json& answer) {
    double s = answer.count("retryAfter") && answer["retryAfter"].is_number() ? answer["retryAfter"].get<double>() : 2;
    if (!(s >= 1)) s = 1;
    if (s > 10) s = 10;
    return (uint64_t)(s * 1000);
  }

  bool open_ = false, started_ = false, not_app_copy_ = false;
  Screen screen_ = Screen::Closed;
  std::string latest_, version_, message_;
  int percent_ = -1, retries_ = 0;
  uint64_t retry_at_ = 0;
};

}  // namespace yougame::signin
