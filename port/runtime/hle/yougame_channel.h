// The local channel: Melee started on its own (not by the YouGame app) finds a running YouGame app
// on this computer and talks to it over the same `yg {json}\n` lines as the app's stdio bridge.
//
// Contract: local-channel-contract.md v1 (standalone notes). The game never looks when the app
// started it (YOUGAME_STDIO / YOUGAME_BRIDGE) or when Melee Unlocked's launcher did. Otherwise it
// probes once at start on a background thread, production first, then staging, and again when the
// Online menu opens (at most once every 5 s). A host counts only when it passes every check below
// and answers hello within 6 s; anything else is no host, and the game stays plain Melee Unlocked.
//
// Names:   Windows \\.\pipe\yougame-sdk-v1-<user SID>, \\.\pipe\yougame-staging-sdk-v1-<user SID>
//          Mac     /tmp/yougame-<uid>/sdk-v1.sock, /tmp/yougame-staging-<uid>/sdk-v1.sock
//          Linux   $XDG_RUNTIME_DIR/yougame/, /run/user/<uid>/yougame/, /tmp/yougame-<uid>/ (then staging)
// Checks:  Mac/Linux: the folder is a real folder, ours, mode exactly 0700 (lstat), and the peer's
//          uid is ours (getpeereid / SO_PEERCRED). Windows: the pipe's server process runs as our
//          user at medium integrity or higher, the pipe's owner is our user, the pipe's own mandatory
//          label is medium or higher (none = medium), the pipe is opened identification-only, and the
//          server's image is signed by YouGame's publisher (skipped only in YOUGAME_AGENT_TEST_HOOK
//          builds).
//
// This file holds the transport and the pure rules; yougame_link.cpp drives the requests through
// the stdio bridge's own request/answer/push handling.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace yougame {
namespace channel {

// The listing the game names in its hello (the YouGame listing of Melee Unlocked).
#ifdef MELEE_YOUGAME_LISTING_SLUG
constexpr const char* kListingSlug = MELEE_YOUGAME_LISTING_SLUG;
#else
constexpr const char* kListingSlug = "melee-unlocked";
#endif
// The host closes a connection on a line longer than 64 K (CHANNEL_LINE_LIMIT in the app). Counted
// in bytes, newline included, so a line with non-ASCII text stays under the app's character count.
constexpr size_t kLineLimit = 64 * 1024;
// Incoming lines longer than this are dropped (the host never sends one; the helper allows 3 x 64 K).
constexpr size_t kIncomingLimit = 3 * 64 * 1024;
constexpr int kHelloTimeoutMs = 6000;      // a host that has not answered hello by then is no host
constexpr int kPipeBusyWaitMs = 200;       // Windows ERROR_PIPE_BUSY: WaitNamedPipe at most this long
constexpr uint64_t kRetryEveryMs = 5000;   // Online menu retries, at most this often
constexpr int kWriteTimeoutMs = 5000;      // a host that stops reading never holds a request forever

// The Windows publisher YouGame's app is signed with (the main Electron process, YouGame.exe, serves
// the pipe; prod and staging alike): subject C=US, ST=Florida, L=Orlando, O=Fluo Inc., CN=Fluo Inc.
// Matched by name, never by thumbprint or serial: the certificates rotate.
struct Publisher {
  std::wstring simple_name, organization, locality, state, country;
};
const Publisher& windows_publisher();
bool publisher_matches(const Publisher& seen);
// WinVerifyTrust's answer, revocation read from the local cache only (no network on the probe): a
// revoked certificate fails. A revocation state that could not be read (offline, no check, or the
// check failed) is RevocationUnknown: WinVerifyTrust returns one code, and a revocation failure can
// hide another chain error behind it, so the caller checks again with revocation off and needs that
// second answer to be 0 (exactly the check from before revocation was asked at all).
enum class TrustVerdict { Trusted, Revoked, RevocationUnknown, Untrusted };
TrustVerdict trust_verdict(int32_t status);

struct Candidate {
  std::string path;       // socket path, or the pipe name
  bool staging = false;
};

// ---- names (pure)
std::string pipe_name(const std::string& sid, bool staging);
std::string mac_socket(uint32_t uid, bool staging);
// The Linux order (contract section 2): $XDG_RUNTIME_DIR (when set, absolute), /run/user/<uid>,
// /tmp; the XDG entry is left out when it names the same folder as /run/user/<uid>.
std::vector<std::string> linux_sockets(const char* xdg_runtime_dir, uint32_t uid, bool staging);
// Production first, then staging, for this computer and user. Empty (with `why`) when the user's
// identity cannot be read (Windows: the token's SID).
std::vector<Candidate> candidates(std::string* why = nullptr);

// ---- the folder check (Mac/Linux, pure part): empty when the folder may be used, else why not.
#ifndef _WIN32
std::string folder_reason(const struct stat& st, uid_t uid);
// lstat's the socket's folder. `missing` is set (and the reason empty) when there is no folder.
std::string check_folder(const std::string& socket_path, uid_t uid, bool* missing);
#endif

// ---- lines (pure)
// A hello sent on the channel names this game (contract section 4); other requests pass unchanged.
nlohmann::json hello_request(nlohmann::json body, const std::string& version, const std::string& slug = kListingSlug);
// `yg {...}` for a request, or empty when it would be longer than kLineLimit (never sent).
std::string request_line(const nlohmann::json& request);
bool line_fits(const std::string& line_without_newline);

enum class LineAction {
  Drop,           // not a bridge line, not JSON, or a push that is not an object
  Answer,         // {"re":n,...}: an answer to one of our requests
  PlayerArrived,  // {"push":{"type":"playerArrived",...}}: the player a pending hello was waiting for
  AppOnly,        // a line only a game the app started may act on (the pad mirror's handle, caps): skipped
  Push,           // any other push: the stdio handler takes the ones it knows and skips the rest
};
LineAction classify(const std::string& line);

// A not-ok answer's code, read on the reader thread before the line is handled (accountChanged must be
// known before the connection's end is). Empty for other lines.
std::string answer_code(const std::string& line);
// accountSwitching's retryAfter (seconds) as milliseconds, kept between 1 and 10 s (2 s without one).
uint64_t retry_after_ms(const nlohmann::json& answer);
// One host-supplied text for a log line: at most 120 bytes, line breaks and controls as spaces.
std::string log_text(const std::string& text);

// What the game shows for a host's refusal over the channel, when ours says it better: notYet (the
// host cannot do this yet), notHere, and a copy that is unknown or out of date. Empty: show the
// host's own words. None of them sends the player to the YouGame app: a copy the player started
// plays everything the app's copy does, through the app or the YouGame Online add-on alike.
std::string refusal_text(const nlohmann::json& answer);
// The answer's result says a player is signed in but not ready yet (playerPending, no player).
bool hello_pending(const nlohmann::json& result);
bool hello_player(const nlohmann::json& result);

// ---- the hybrid route (pure): Melee Unlocked's launcher with the YouGame extension on (MELEE_YOUGAME_MATCH_BRIDGE=1).
// The game stays on the app's channel for the extension's own features (its button and menu, gameState, matchState,
// menuButton, pause, inputKind, sign-in and update panels); the requests that say who the player is and that find or
// keep a match go to the launcher's bridge, with the launcher's own YouGame sign-in, so play never depends on the app.
// With the channel down (the app closed, not started, or gone mid-session) every request goes to the bridge.
// app_sign_in (hybrid_sign_in() said App): the launcher has no YouGame sign-in, so the match requests go over the
// app's channel with the app's sign-in, as for a game the player started. The launcher's own questions (hello: who
// is signed in to the launcher, which release it searches; muRestartOlder) stay with the launcher, so its sign-in is
// asked first every time.
inline bool to_launcher_bridge(const std::string& type, bool channel_up, bool app_sign_in = false) {
  if (!channel_up || type == "hello" || type == "muRestartOlder") return true;
  static const char* const kMatch[] = {"mpCode", "mpTicket", "mpRank", "mpFriend", "mpRoom"};
  for (const char* t : kMatch)
    if (type == t) return !app_sign_in;
  return false;
}
// The hybrid's invites. The launcher has no friends list, so an invite into a room the game already holds (the
// Doubles partner, a seat of the 4-player Friends lobby, an arena seat: mpFriend with `room` and no `hold`) and the
// room report the app's invite controls follow (mpRoom) go to the YouGame app's channel instead, when that channel
// is up and the app is signed in as the same player as the launcher. Holding the room and its tickets stay with
// the launcher. Another player in the app, or no channel: the launcher answers as before ("Enter a connect code").
inline bool invite_to_app(const std::string& type, bool has_room, bool hold, bool channel_up, bool same_player) {
  if (!channel_up || !same_player) return false;
  return type == "mpRoom" || (type == "mpFriend" && has_room && !hold);
}
// Whose YouGame sign-in the hybrid plays with: the launcher's whenever it names a player (play never depends on the
// app); with none, the app's while its channel is up and names a player; else nobody (the rows stay closed, and an
// app that is up but signed out offers its sign-in: sign_in_needed()).
enum class HybridSignIn { Launcher, App, None };
inline HybridSignIn hybrid_sign_in(bool launcher_player, bool channel_up, bool app_player) {
  if (launcher_player) return HybridSignIn::Launcher;
  return channel_up && app_player ? HybridSignIn::App : HybridSignIn::None;
}
// The hybrid offers the app's own sign-in panel for a YouGame row only when the sign-in was asked and named nobody
// (neither the launcher's nor the app's), and the app's channel is up and says nobody is signed in there.
inline bool hybrid_offers_app_sign_in(bool asked, bool signed_in, bool channel_up, bool app_signed_out) {
  return asked && !signed_in && channel_up && app_signed_out;
}

// ---- playerPending (pure): after a pending hello answer, say hello again ~2 s and ~6 s later.
// Stops at the first answer that carries a player, at the playerArrived push, or after the second
// retry. take_arrival() is true exactly once, when the player arrived after a pending answer.
class PlayerWait {
 public:
  static constexpr uint64_t kFirstRetryMs = 2000, kSecondRetryMs = 6000;
  void answered(bool pending, bool player, uint64_t now_ms);
  void pushed();
  uint64_t due_at() const;   // when to say hello again; 0 when no retry is due
  void sent();               // a retry hello went out (answered or not)
  bool take_arrival();
  bool waiting() const { return state_ == State::Waiting; }
 private:
  enum class State { Idle, Waiting, Arrived, GaveUp } state_ = State::Idle;
  uint64_t since_ = 0;
  int sent_ = 0;
  bool told_ = false;
};

// ---- Online menu retries (pure): at most one probe every kRetryEveryMs.
class RetryGate {
 public:
  bool allow(uint64_t now_ms) {
    if (tried_ && now_ms - last_ < kRetryEveryMs) return false;
    tried_ = true; last_ = now_ms; return true;
  }
 private:
  bool tried_ = false;
  uint64_t last_ = 0;
};

// ---- the transport
enum class OpenResult { Connected, Missing, Refused };

namespace detail {
// One overlapped read (Windows), as a rule the tests can drive: issue the read; when it is pending and
// close() has already set `closed`, cancel it before waiting. close() sets the flag before it cancels,
// so whichever comes second cancels: a read issued just after close()'s cancel can never wait forever.
enum class Issued { Done, Pending, Failed };
template <class Issue, class Cancel, class Wait>
bool read_once(const std::atomic<bool>& closed, const Issue& issue, const Cancel& cancel, const Wait& wait) {
  const Issued issued = issue();
  if (issued == Issued::Failed) return false;
  if (issued == Issued::Pending && closed.load()) cancel();
  return wait();
}
}  // namespace detail

class Connection {
 public:
  ~Connection();
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  // Connects and checks the host (the checks in the header). Missing: nothing listens there (no
  // folder, no socket, no pipe). Refused: something is there and failed a check (`why`, no data).
  static OpenResult open(const Candidate& where, std::unique_ptr<Connection>* out, std::string* why);
  // One whole line plus its newline, never longer than kLineLimit; false when too long or the
  // connection is gone. Safe from any thread.
  bool write_line(const std::string& line);
  // Reads until the host closes the connection or close() is called, one call per line (no
  // newline). Run on one thread only.
  void read_lines(const std::function<void(const std::string&)>& on_line);
  void close();
  bool closed() const { return closed_.load(); }
 private:
  Connection() = default;
  std::mutex write_mutex_;
  std::atomic<bool> closed_{false};
#ifdef _WIN32
  void* pipe_ = nullptr;          // HANDLE
  void* read_event_ = nullptr;    // HANDLE
  void* write_event_ = nullptr;   // HANDLE
#else
  int fd_ = -1;
#endif
};

// A probe may count `conn` as up only while it is still the current connection and still open: a host
// that closed it during hello (its reader already ended, or channel_closed already dropped it) is no host.
// Called with the owner's lock held.
inline bool may_promote(const std::shared_ptr<Connection>& current, const std::shared_ptr<Connection>& conn) {
  return conn && current == conn && !conn->closed();
}

#ifdef _WIN32
// The Authenticode check of the pipe server's image: signed, trusted, not revoked (cache only), and
// its leaf certificate's subject is windows_publisher(). Separate so a build can see what is checked.
bool verify_host_publisher(const std::wstring& image_path, std::string* why);
#endif

}  // namespace channel
}  // namespace yougame
