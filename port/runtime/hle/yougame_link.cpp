// YouGame online (see yougame_link.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "slippi_agent_lock.h"
#include "test_env.h"
#include "yougame_link.h"
#include "yougame_channel.h"
#include "yougame_signin.h"
#include "yougame_udp.h"
#include "yougame_tourney.h"
#include "yougame_tourney_queue.h"
#include "yougame_tourney_delivery.h"
#include "yougame_embed_toplevel.h"
#include "yougame_embed_fullscreen.h"
#include "opponent_display.h"
#include "yougame_parity.h"
#include "yougame_shortcuts.h"
#include "yougame_relay_pads.h"
#include "yougame_relay_rumble.h"
#include "yougame_agent_rumble.h"
#include "yougame_ux.h"
#include "yougame_friends.h"
#include "yougame_slippi_link.h"
#include "yougame_compatibility.h"
#include "yougame_env.h"
#include "slippi_online.h"
#include "yougame_exit_reason.h"
#include "yougame_ranked.h"
#include "yougame_room.h"
#include "yougame_room_relay.h"
#include "yougame_lobby.h"
#include "yougame_doubles_lobby.h"
#include "yougame_console_link.h"
#include "yougame_console_card.h"
#include "yougame_console_entry.h"
#include "yougame_doubles.h"
#include "yougame_update.h"
#include "yougame_blend.h"
#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
#include "yougame_squad.h"
#endif
#include "yougame_search.h"
#include "yougame_match_state.h"
#include "yougame_game_state.h"
#include "gameplay_policy.h"
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_static.h"
#endif
#include <enet/yg_enet_socket.h>
#include "host.h"
#include "lcancel.h"
#include "pipeline_cache_store.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <xinput.h>
// After the Windows headers: it brings <windows.h> in through input_bindings.h.
#include "yougame_controller_relay.h"
#pragma comment(lib, "winhttp.lib")
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

using json = nlohmann::json;

// An ask for YouGame (a YouGame row pressed with no YouGame connected) whose place was left before it
// was answered: the Online menu backed out of, a character select entered, a search cancelled
// (yougame::withdraw_ask). Set until the next ask starts.
static std::atomic<bool> g_yg_ask_left{false};
#ifdef _WIN32
// YouGame's small window runs its own loop on the asking thread and is topmost, so it stays up over
// whatever the game moves on to. Its window is kept here as it shows (the header's YG__START_ON_SHOWN
// hook) so leaving the place that asked can close it.
static std::atomic<void*> g_yg_start_window{nullptr};
static void yg_link_start_shown(void* hwnd);
#define YG__START_ON_SHOWN(u) yg_link_start_shown((void*)(u)->hwnd)
#endif

// YouGame's native headers (MIT, engines/native, claude/v2 0b5798f46). The start hook is enabled on
// Windows and macOS; Linux keeps its existing UNAVAILABLE behaviour.
#if defined(_WIN32) || defined(__APPLE__)
#define YOUGAME_START_IMPLEMENTATION
#endif
#include "../../third_party/yougame_start/yougame_start.h"
#include "../../third_party/yougame_start/yougame.h"
#if !defined(_WIN32) && !defined(__APPLE__)
extern "C" yg_start_result yg_start(const yg_start_options*) { return YG_START_UNAVAILABLE; }
#endif
#ifdef _WIN32
// Closes YouGame's small window as its own X does (Play offline; an ask on use never remembers it),
// Get YouGame never runs inside it (it runs on a worker after the window returns), so nothing in it is cut short.
// Only our own process's window of that class; a stale handle is refused. True when the close was sent.
static bool yg_link_start_close() {
  HWND w = (HWND)g_yg_start_window.load();
  if (!w || !IsWindow(w)) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(w, &pid);
  wchar_t cls[64] = {};
  if (pid != GetCurrentProcessId() || !GetClassNameW(w, cls, 64) || wcscmp(cls, L"YouGameStartWindow") != 0) return false;
  return PostMessageW(w, WM_CLOSE, 0, 0) != 0;
}
static void yg_link_start_shown(void* hwnd) {
  g_yg_start_window = hwnd;
  // The place that asked was left while YouGame was being looked for: close it as soon as it shows.
  if (g_yg_ask_left.load() && yg_link_start_close()) host::log("yougame: asked from a place since left: YouGame's window closed");
}
#endif

namespace yougame {
namespace {
std::timed_mutex g_tourney_mutex;
tourney::Session g_tourney;
tourney::Queue g_tourney_queue;
std::shared_future<void> g_tourney_delivery;
std::atomic<int64_t> g_tourney_delivery_until{0};
uint64_t g_tourney_generation = 0;
std::atomic<bool> g_tourney_cleanup_pending{false};

// Caller holds g_tourney_mutex. Deferred cleanup keeps scene teardown from waiting on a lock.
void release_tournament_locked() {
  if (!g_tourney_cleanup_pending.exchange(false)) return;
  ++g_tourney_generation;
  g_tourney.release();
  g_tourney_delivery = {};
}
std::atomic<bool> g_tourney_launched{false};
class BridgeNoAnswer : public std::runtime_error { public: using std::runtime_error::runtime_error; };


#ifdef _WIN32
using SocketLength = int;
#else
using SocketLength = socklen_t;
#endif

uint64_t now_ms() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void log_friend_version() {
  if (friends::events.report_version()) host::log("friends: friend on a different version");
}

uint64_t random64() {
  static std::mutex m;
  static std::mt19937_64 rng(((uint64_t)std::random_device{}() << 32) ^ std::random_device{}() ^ now_ms());
  std::lock_guard<std::mutex> lock(m);
  return rng();
}

std::string hex64(uint64_t v) {
  char buf[17];
  std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)v);
  return buf;
}

std::wstring wide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}

std::string url_encode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

struct Url {
  std::wstring host, path;
  INTERNET_PORT port = 0;
  bool secure = false;
};
bool parse_url(std::string value, Url& out) {
  if (value.rfind("wss://", 0) == 0) value.replace(0, 6, "https://");
  else if (value.rfind("ws://", 0) == 0) value.replace(0, 5, "http://");
  std::wstring w = wide(value);
  URL_COMPONENTS u{};
  u.dwStructSize = sizeof u;
  u.dwHostNameLength = u.dwUrlPathLength = u.dwExtraInfoLength = (DWORD)-1;
  if (!WinHttpCrackUrl(w.c_str(), (DWORD)w.size(), 0, &u)) return false;
  out.host.assign(u.lpszHostName, u.dwHostNameLength);
  out.path.assign(u.lpszUrlPath, u.dwUrlPathLength);
  if (u.dwExtraInfoLength) out.path.append(u.lpszExtraInfo, u.dwExtraInfoLength);
  if (out.path.empty()) out.path = L"/";
  out.port = u.nPort;
  out.secure = u.nScheme == INTERNET_SCHEME_HTTPS;
  return true;
}

HINTERNET http_session() {
  static HINTERNET session = [] {
    HINTERNET s = WinHttpOpen(L"MeleeUnlocked-YouGame/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) WinHttpSetTimeouts(s, 10000, 10000, 15000, 15000);
    return s;
  }();
  return session;
}

// ------------------------------------------------------------------ the desktop app's bridge
// Taken out of the environment once (start()), so a program the game opens never inherits the token.
std::string g_bridge_url, g_bridge_token;
bool g_stdio = false, g_embedded = false;
bool g_embed_toplevel = false;
uint32_t g_app_pid = 0;
#ifdef _WIN32
bool g_toplevel_dpi_failed = false;
DWORD g_toplevel_dpi_error = 0;
#endif

// Stdio mode: requests go out on stdout as `yg {"id":N,...}` lines and answers come back on stdin as
// `yg {"re":N,...}`; the reader thread also takes the player box's `embed ...` lines.
struct Stdio {
  std::mutex mutex;
  std::condition_variable cv;
  std::map<int64_t, json> answers;
  std::set<int64_t> pending_requests;
  int64_t next_id = 1;
  std::mutex write_mutex;
  std::function<void(const std::string&)> box;
  std::deque<std::string> box_backlog;
#ifdef MELEE_LINUX_X11_EMBED
  bool box_overflow = false;
#endif
  std::atomic<bool> eof{false};
};
Stdio g_io;
// The local channel (yougame_channel.h): a game started on its own (no YOUGAME_STDIO, YOUGAME_BRIDGE
// or launcher) talks to a running YouGame app over the stdio bridge's own lines. Requests, answers and
// pushes go through g_io and handle_app_line exactly as in stdio mode; only the writes differ.
// g_io.eof is true whenever no channel connection is open.
// MELEE_YOUGAME_NO_APP (the GitHub build): YouGame's rows are served by Melee Unlocked's launcher only.
// Nothing here looks for, starts, installs or offers the YouGame app, and no local channel is opened.
// The launcher's "YouGame extension" option (off by default) passes MELEE_YOUGAME_EXTENSION=1: then the
// app's local channel is used as in the partner's build. Read once, before anything clears the variables.
#ifdef MELEE_YOUGAME_NO_APP
const bool kNoApp = [] { const char* v = std::getenv("MELEE_YOUGAME_EXTENSION"); return !(v && v[0] == '1'); }();
#else
const bool kNoApp = false;
#endif
// The launcher found the extension's app running or installed (MELEE_YOUGAME_APP_EXPECTED=1): its channel is
// expected, so the Friends row waits for it (extension_waiting) rather than opening the connect-code search.
const bool kAppExpected = [] { const char* v = std::getenv("MELEE_YOUGAME_APP_EXPECTED"); return v && v[0] == '1'; }();
std::atomic<bool> g_channel_allowed{false};          // start(): nothing started this game
std::mutex g_channel_mutex;
std::shared_ptr<channel::Connection> g_channel;      // g_channel_mutex; set while probing and while up
std::atomic<bool> g_channel_up{false};               // verified and hello answered
std::atomic<uint64_t> g_channel_epoch{0};            // +1 each time a connection comes up (gameState resends)
uint64_t g_links_epoch = ~uint64_t(0);                // g_channel_epoch when direct links last attached (g_mutex)
std::atomic<bool> g_channel_probing{false};
std::atomic<bool> g_channel_account_changed{false};  // the host's last refusal was accountChanged
channel::RetryGate g_channel_retry;                  // g_channel_mutex
channel::PlayerWait g_player_wait;                   // g_channel_mutex
std::atomic<bool> g_player_wait_running{false};
// The channel's last hello said nobody is signed in (signin::hello_signed_out): the YouGame rows offer
// Sign in by code (yougame_signin.h) instead of staying locked.
std::atomic<bool> g_channel_signed_out{false};
void sign_in_push(const json& push);   // the sign-in panel's pushes (signedIn, signInExpired, signInFailed)
// "Update to play online" (signin::Update): a search over the local channel refused as out of date, and
// the app's gameUpdate pushes. Defined with the sign-in panel's code below. find_match offers it from the
// player's first ticket of a search only (never a renewal); Blended Unranked keeps it until the blend is over.
void update_offer(const json& refusal, uint64_t epoch, bool blend);
void update_push(const json& push);
// A match started (online_match_state): the panel closes on the next frame. Lock-free.
std::atomic<bool> g_update_match_started{false};
std::atomic<uint64_t> g_probe_started{0};           // now_ms() when the last probe began (timing lines)
uint64_t since_probe() { const uint64_t at = g_probe_started.load(); return at ? now_ms() - at : 0; }
relay_pads::State g_relay_pads([](const std::string& line) { host::log("%s", line.c_str()); }, false);
std::atomic<uintptr_t> g_pad_mirror{0};
#ifndef _WIN32
std::mutex g_pad_mirror_mutex;
pad_mirror::Attachment g_pad_mirror_attachment;
#endif
transport::Events g_net_events;
std::atomic<bool> g_broker{false}, g_direct{false};
search_policy::Generation g_search_generation;
thread_local std::function<bool()> g_search_stopped;
thread_local search_policy::BusyRetry* g_ticket_retry = nullptr;
update::Notice g_update_notice;
bool search_stopped() { return g_search_stopped && g_search_stopped(); }
bool g_launcher = false;                        // MELEE_YOUGAME_LAUNCHER=1: Melee Unlocked's launcher serves the bridge
// MELEE_YOUGAME_MATCH_BRIDGE=1 with YOUGAME_BRIDGE/YOUGAME_TOKEN: Melee Unlocked's launcher with the YouGame extension
// on (owner 11-20: play never depends on the YouGame app). The game is on the app's local channel for the
// extension's own features (its button and menu, game and match state, pause, input kind) while every request that
// says who the player is or finds and keeps a match goes to the launcher's bridge, with the launcher's own YouGame
// sign-in (channel::to_launcher_bridge). The app closed or gone: everything goes to the bridge, play goes on.
bool g_match_bridge = false;
// Hybrid, the launcher not signed in to YouGame while the app's channel names a player (channel::hybrid_sign_in
// said App, decided on every fetch_identity): the match requests go over the app's channel with the app's sign-in,
// as for a game the player started, instead of a refusal. Cleared when the channel closes.
std::atomic<bool> g_app_sign_in{false};
bool g_hybrid_env = false;                      // started with MELEE_YOUGAME_MATCH_BRIDGE=1 (put back for a restart)
// A restart (a setting the device cannot adopt, a lost graphics device, a mod's looks) starts a new copy
// from this process's environment, which start() emptied: the launcher's bridge goes back in for it, so
// the new copy is signed in like this one. The launcher keeps its last few launch tokens, so this one
// still answers.
void restore_bridge_for_restart() {
  _putenv_s("YOUGAME_BRIDGE", g_bridge_url.c_str());
  _putenv_s("YOUGAME_TOKEN", g_bridge_token.c_str());
  _putenv_s(g_hybrid_env ? "MELEE_YOUGAME_MATCH_BRIDGE" : "MELEE_YOUGAME_LAUNCHER", "1");
}
std::atomic<bool> g_app_same_player{false};   // hybrid: the app's channel names the launcher's own player (identity())
bool routed_to_bridge(const json& body) {
  if (g_match_bridge && body.is_object() &&
      channel::invite_to_app(body.value("type", std::string()), body.count("room") && body["room"].is_string(),
                             body.value("hold", json()) == true, g_channel_up.load(), g_app_same_player.load()))
    return false;
  return g_match_bridge && channel::to_launcher_bridge(body.is_object() ? body.value("type", std::string()) : std::string(),
                                                       g_channel_up.load(), g_app_sign_in.load());
}
// The player the app's channel last named in a hello (hybrid: compared with the launcher's sign-in, logged).
std::mutex g_app_player_mutex;
std::string g_app_player_id;
std::atomic<int> g_online_mode_arg{0};          // --online-mode: 1 ranked, 2 direct, taken once
std::atomic<bool> g_launcher_session{false};    // under the launcher: the online menu's last pick was a YouGame row
std::mutex g_link_mutex;
std::map<unsigned, int> g_link_events;
std::map<unsigned, std::string> g_link_routes;
std::map<unsigned, std::vector<uint64_t>> g_link_rtts;
std::map<unsigned, uint64_t> g_link_last_rtt;   // the newest round trip per link (the Friends lobby's doors)
std::atomic<bool> g_relaunch{false};
std::shared_ptr<console::RelaunchWait> g_relaunch_wait; // atomic shared_ptr publication, like handoff channels
int direct_state(unsigned id) {
  const int state = yg_enet_link_state(id);
  if (state == 2 || state == 3) return state;
  std::lock_guard<std::mutex> lock(g_link_mutex);
  const auto it = g_link_events.find(id);
  return it == g_link_events.end() ? state : it->second;
}

void write_line(const std::string& line) {
  std::lock_guard<std::mutex> lock(g_io.write_mutex);
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  std::string data = line + "\n";
  DWORD wrote = 0;
  // One WriteFile per line, so nothing else written to stdout can land inside it.
  if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, data.data(), (DWORD)data.size(), &wrote, nullptr);
}

// Deadline-limited writes use this cancellable path; ordinary SDK requests keep write_line.
bool tournament_write_line(const std::string& line, tourney::Deadline deadline) {
  std::packaged_task<bool()> task([data = line + "\n", deadline] {
    std::unique_lock<std::mutex> lock(g_io.write_mutex, std::defer_lock);
    if (!tourney::lock_until(lock, deadline) || tourney::DeliveryClock::now() >= deadline) return false;
#ifdef _WIN32
    HANDLE cancel = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &cancel,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) return false;
    std::promise<void> finished;
    auto complete = finished.get_future().share();
    try {
      std::thread([complete, cancel, deadline] {
        // Repeat cancellation closes the check/WriteFile registration race. The retained
        // thread handle cannot refer to a reused ID, and shutdown never joins this watcher.
        if (complete.wait_until(deadline) != std::future_status::ready) {
          do { CancelSynchronousIo(cancel); }
          while (complete.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready);
        }
        CloseHandle(cancel);
      }).detach();
    } catch (...) { CloseHandle(cancel); return false; }
    struct Complete { std::promise<void>& done; ~Complete() { done.set_value(); } } signal{finished};
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD wrote = 0;
    return out && out != INVALID_HANDLE_VALUE &&
           WriteFile(out, data.data(), (DWORD)data.size(), &wrote, nullptr) && wrote == data.size();
#else
    return tourney::pipe_write_until(STDOUT_FILENO, data, deadline);
#endif
  });
  auto done = task.get_future().share();
  std::thread(std::move(task)).detach();
  return done.wait_until(deadline) == std::future_status::ready && done.get();
}

std::unique_lock<std::mutex> tournament_io_lock(tourney::Deadline deadline) {
  std::unique_lock<std::mutex> lock(g_io.mutex, std::defer_lock);
  if (deadline == tourney::Deadline::max()) lock.lock();
  else if (!tourney::lock_until(lock, deadline)) throw BridgeNoAnswer("Tournament delivery deadline");
  return lock;
}

std::mutex g_relay_rumble_mutex;
relay_pads::Rumble g_relay_rumble;
#ifdef YOUGAME_AGENT_TEST_HOOK
agent_test::RumblePulse g_rumble_pulse;
#endif
void relay_rumble(host::CaptureDevice family, int index, bool on) {
  const int seat = relay_pads::seat_for(g_relay_pads.snapshot(), family, index);
  if (seat < 0) return;
  std::lock_guard<std::mutex> lock(g_relay_rumble_mutex);
  g_relay_rumble.set(seat, on, relay_pads::steady_ms());
}
void relay_rumble_writer() {
  relay_pads::Frame previous;
  for (;;) {
    g_relay_pads.timing_tick();
    const auto frame = g_relay_pads.snapshot();
    const double now = relay_pads::steady_ms();
    relay_pads::RumbleCommand command;
    bool pending = false;
    {
      std::lock_guard<std::mutex> lock(g_relay_rumble_mutex);
      for (int seat = 0; seat < 4; ++seat) {
        const auto& a = previous.pads[seat]; const auto& b = frame.pads[seat];
        if (a.present && (!b.present || a.id != b.id || a.kind != b.kind || a.family_index != b.family_index))
          g_relay_rumble.set(seat, false, now);
      }
#ifdef YOUGAME_AGENT_TEST_HOOK
      std::array<bool, 4> present{};
      for (int seat = 0; seat < 4; ++seat) present[seat] = frame.pads[seat].present;
      g_rumble_pulse.tick(now, present, [&](int seat) { g_relay_rumble.pulse(seat, now); },
          [](const char* line) { host::log("%s", line); });
#endif
      g_relay_rumble.tick(now);
      pending = g_relay_rumble.take(command);
    }
    previous = frame;
    // No queue/state mutex held across stdout: a stopped reader never stalls PADRead.
    if (pending) write_line(relay_pads::rumble_line(command));
    else std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (g_io.eof.load()) return;
  }
}
thread_local relay_pads::RouteLog g_relay_routes;
void relay_route(host::CaptureDevice family, int index, int port) {
  const auto line = g_relay_routes.changed(relay_pads::game_frame, family, index, port);
  if (!line.empty()) host::log("%s", line.c_str());
}

// The app's pads in the game's controller families (yougame_controller_relay.h), for the game's
// input threads: a copy of the latest accepted line.
host::controller_relay::Snapshot relayed_controllers() {
  if (!host::controller_relay::game_poll) return relay_pads::controller_snapshot(g_relay_pads.snapshot());
  thread_local uint64_t serial = 0;
  if (serial != host::controller_relay::poll_serial) {
    serial = host::controller_relay::poll_serial;
    relay_pads::game_frame = g_relay_pads.game_snapshot();
    g_relay_routes.begin(relay_pads::game_frame);
  }
  return relay_pads::controller_snapshot(relay_pads::game_frame);
}

void handle_app_line(const std::string& line) {
  if (g_update_notice.command(line)) {
    const auto id = g_update_notice.take_ready_log();
    if (!id.empty()) host::log("update: ready %s (waiting for a safe point)", id.c_str());
    return;
  }
  if (line.rfind("yg-pads ", 0) == 0) {
    // The provider goes in before the first line is accepted, so a reader that sees relay mode
    // already sees the families. A line that does not parse installs nothing: native pads stay.
    // Melee Unlocked reads Xbox, PlayStation, Switch Pro and HID pads itself (window.cpp), the same way as
    // when it is started on its own, so the app's relay would only add its sampling wait (about 4 ms at
    // 125 Hz). The relay is the fallback for a game that cannot open the devices: a sandboxed
    // (AppContainer) process. MELEE_YOUGAME_RELAY_PADS=1 forces it on (tests, comparisons).
    static const bool use_relay = [] {
      const char* force = std::getenv("MELEE_YOUGAME_RELAY_PADS");
      if (force && *force == '1') return true;
#ifdef _WIN32
      HANDLE token = nullptr; DWORD sandboxed = 0, bytes = 0;
      if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        if (!GetTokenInformation(token, TokenIsAppContainer, &sandboxed, sizeof sandboxed, &bytes)) sandboxed = 0;
        CloseHandle(token);
      }
      host::log("pads: %s", sandboxed ? "sandboxed: the YouGame app's relay feeds the pads"
                                      : "read by the game itself; the YouGame app's relay is not used");
      return sandboxed != 0;
#else
      return true;
#endif
    }();
    if (!use_relay) return;
    if (!host::controller_relay::active()) {
      relay_pads::Frame frame; std::string reason;
      if (relay_pads::parse(line, frame, reason)) host::controller_relay::install(relayed_controllers);
    }
    g_relay_pads.accept(line, now_ms());
    return;
  }
  if (line.rfind("yg ", 0) == 0) {
    json j = json::parse(line.substr(3), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
    if (j.count("caps")) console::read_caps(j, g_relaunch);
    if (j.count("re") && j["re"].is_number_integer()) {
      // The id first: json's operator= takes its argument by value, so moving j into it would empty
      // j before an index read from j on the left is evaluated (C++17 sequences the right side first).
      const int64_t id = j["re"].get<int64_t>();
      std::lock_guard<std::mutex> lock(g_io.mutex);
      if (g_io.pending_requests.count(id)) g_io.answers[id] = std::move(j);
      g_io.cv.notify_all();
    }
    // Pushes from the app: the pad mirror's handle ({"push":{"type":"pads","handle":N}}).
    if (j.count("push") && j["push"].is_object()) {
      const json& push = j["push"];
      if (auto wait = std::atomic_load(&g_relaunch_wait)) wait->push(push);
      if (console::launch_request(push)) parity::signals.console_boot_ready();
      if (g_stdio) ux::page_pause.push(push);
      if (push.value("cmd",json()) == "leave") {
#ifdef MELEE_DOUBLES
        doubles::ui.back_if_active();
#endif
        friends::lobby.leave(); console::entry.stop(); } // no-op outside a held room
      if (!g_stdio && g_channel_allowed) { sign_in_push(push); update_push(push); }   // sign-in's pushes, gameUpdate
      const bool taken = friends::events.push(push, now_ms());
      if (taken && friends::version_reason(push)) log_friend_version();
      // Receipt only (no names or text): proves the app's notice reached the game, shown or not.
      else if (push.value("type", "") == "notice")
        host::log("friends: notice %.16s %s", friends::text(push, "kind", 16).c_str(), taken ? "shown" : "ignored");
      if (push.value("type", "") == "net") g_net_events.push(push);
      if (push.value("type", "") == "direct") {
        const auto notice = console::direct_notice(push);
        std::lock_guard<std::mutex> lock(g_link_mutex);
        if (notice.link) {
          if (notice.state) g_link_events[notice.link] = notice.state;
          if (!notice.path.empty()) g_link_routes[notice.link] = notice.path;
          if (notice.rtt) {
            auto& samples = g_link_rtts[notice.link];
            if (samples.size() < 32) samples.push_back(*notice.rtt);
            g_link_last_rtt[notice.link] = *notice.rtt;
          }
        }
      }
      if (push.value("type", "") == "pads" && push.count("handle") && push["handle"].is_number_unsigned())
        g_pad_mirror = (uintptr_t)push["handle"].get<uint64_t>();
#ifndef _WIN32
      if (push.value("type", "") == "pads") {
        pad_mirror::Attachment info;
        if (pad_mirror::attachment(push, info)) {
          std::lock_guard<std::mutex> lock(g_pad_mirror_mutex);
          g_pad_mirror_attachment = info;
        }
      }
#endif
    }
    return;
  }
  if (line.rfind("embed ", 0) == 0) {
    if (shortcuts::commands.dispatch(line.substr(6), [](const char* text) { host::log("%s", text); })) return;
    const auto fullscreen = embed_fullscreen::state.receive(std::string_view(line).substr(6));
    if (fullscreen != embed_fullscreen::Line::Other) {
      if (fullscreen == embed_fullscreen::Line::Accepted) {
        const int on = embed_fullscreen::state.display(true);
        host::log("yougame: embed fullscreen %d (display option now %s)", on,
                  on ? "Borderless fullscreen" : "Window");
      } else {
        static bool invalid_logged = false;
        if (!invalid_logged) host::log("yougame: embed fullscreen invalid value; ignored");
        invalid_logged = true;
      }
      return;
    }
#ifdef MELEE_LINUX_X11_EMBED
    if (g_embedded) {
      // Keep command order, including during boot. Never run Xlib on this reader thread.
      if (line.size() > 159 || line.find('\0') != std::string::npos) return;
      std::lock_guard<std::mutex> lock(g_io.mutex);
      if (g_io.box_backlog.size() == 4096) g_io.box_overflow = true;
      if (!g_io.box_overflow) g_io.box_backlog.push_back(line.substr(6));
      return;
    }
#endif
    const std::string cmd = line.substr(6);
    std::function<void(const std::string&)> box;
    {
      std::lock_guard<std::mutex> lock(g_io.mutex);
      box = g_io.box;
      if (!box) {
        // Before the window exists only the latest of each kind matters (the page sends a rect on
        // every scroll while the game boots): it replaces the one it supersedes.
        auto kind = [](const std::string& c) {
#ifdef _WIN32
          if (c.rfind("view", 0) == 0) return std::string("view");
#endif
          return c.rfind("rect", 0) == 0 ? "rect" : c == "hide" || c == "show" ? "shown" : c == "focus" || c == "blur" ? "focus"
               : c.rfind("volume", 0) == 0 ? "volume" : c;
        };
        auto& backlog = g_io.box_backlog;
        backlog.erase(std::remove_if(backlog.begin(), backlog.end(), [&](const std::string& c) { return std::string(kind(c)) == kind(cmd); }), backlog.end());
        backlog.push_back(cmd);
        return;
      }
    }
    box(cmd);
  }
}

void read_stdin() {
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  std::string pending;
  char buf[4096];
  DWORD got = 0;
#ifdef MELEE_LINUX_X11_EMBED
  bool discard = false;
#endif
  // How stdin ended, logged with the quit: a launcher whose pipe never reached the game reads
  // differently from one that closed it (0 bytes, an error) or an app that left mid-run.
  unsigned long long total = 0;
  DWORD read_error = 0;
  bool read_failed = false;
  while (in && in != INVALID_HANDLE_VALUE) {
    if (!ReadFile(in, buf, sizeof buf, &got, nullptr)) { read_failed = true; read_error = GetLastError(); break; }
    if (!got) break;
    total += got;
#ifdef MELEE_LINUX_X11_EMBED
    if (g_embedded) {
      // Discard an entire oversized line through its newline; a suffix is never a command.
      for (DWORD i = 0; i < got; ++i) {
        if (buf[i] == '\n') {
          if (!discard) {
            if (!pending.empty() && pending.back() == '\r') pending.pop_back();
            try { handle_app_line(pending); }
            catch (const std::exception&) { host::log("yougame: bad line from the app"); }
          }
          pending.clear(); discard = false;
        } else if (!discard) {
          if (pending.size() == (1u << 20) || buf[i] == '\0') {
            pending.clear(); discard = true;
          } else pending.push_back(buf[i]);
        }
      }
      continue;
    }
#endif
    pending.append(buf, got);
    size_t nl;
    while ((nl = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, nl);
      pending.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      try {
        handle_app_line(line);
      } catch (const std::exception& e) {
        host::log("yougame: bad line from the app");
      }
    }
    if (pending.size() > (1u << 20)) pending.clear();
  }
  // The app is gone (it quit, or stopped this game): no answer will come, and nothing would show or
  // stop this window any more. Waiting requests fail at once and the game ends.
  g_io.eof = true;
  g_relay_pads.reset_on_eof();
  g_relay_pads.flush();
  g_io.cv.notify_all();
  if (!in || in == INVALID_HANDLE_VALUE) host::log("yougame: stdin: no handle");
  else if (read_failed) host::log("yougame: stdin: read failed (error %lu) after %llu bytes", (unsigned long)read_error, total);
  else host::log("yougame: stdin: end of file after %llu bytes", total);
  host::log("yougame: the YouGame app closed the bridge; quitting");
  host::request_exit(0);
}

json bridge_stdio(const json& body, int timeout_ms, const std::function<bool()>& cancelled = {},
                  tourney::Deadline absolute = tourney::Deadline::max()) {
  if (g_io.eof) throw BridgeNoAnswer("The YouGame app is not answering");
  int64_t id;
  {
    auto lock = tournament_io_lock(absolute);
    id = g_io.next_id++;
    g_io.pending_requests.insert(id);
  }
  struct Cleanup {
    int64_t id;
    bool tournament;
    ~Cleanup() {
      if (!tournament) return;
      std::unique_lock<std::mutex> lock(g_io.mutex, std::try_to_lock);
      if (lock.owns_lock()) { g_io.pending_requests.erase(id); g_io.answers.erase(id); }
    }
  } cleanup{id, absolute != tourney::Deadline::max()};
  json request = body;
  request["id"] = id;
  const std::string line = "yg " + request.dump();
  if (!g_stdio) {
    // The local channel: never a line the host would close the connection over.
    std::shared_ptr<channel::Connection> conn;
    { std::lock_guard<std::mutex> lock(g_channel_mutex); conn = g_channel; }
    const bool fits = channel::line_fits(line);
    if (absolute != tourney::Deadline::max() || !fits || !conn || !conn->write_line(line)) {
      std::lock_guard<std::mutex> lock(g_io.mutex);
      g_io.pending_requests.erase(id);
      if (!fits) throw std::runtime_error("This is too large to send to YouGame");
      throw BridgeNoAnswer("YouGame is not answering");
    }
  } else if (absolute == tourney::Deadline::max()) write_line(line);
  else if (!tournament_write_line(line, absolute)) throw BridgeNoAnswer("Tournament stdout deadline");
  auto lock = tournament_io_lock(absolute);
  const auto deadline = std::min(absolute, std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms));
  while (!g_io.answers.count(id) && !g_io.eof) {
    if (cancelled && cancelled()) {
      g_io.pending_requests.erase(id);
      throw std::runtime_error("Cancelled");
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      g_io.pending_requests.erase(id);
      throw BridgeNoAnswer("The YouGame app did not answer");
    }
    g_io.cv.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(50)));
  }
  g_io.pending_requests.erase(id);
  if (!g_io.answers.count(id)) throw BridgeNoAnswer("The YouGame app is not answering");
  json answer = std::move(g_io.answers[id]);
  g_io.answers.erase(id);
  return answer;
}

// ------------------------------------------------------------------ the local channel's requests
constexpr const char* kChannelClosed = "YouGame is not open. Open the YouGame app, then try again.";
void player_arrived();   // after fetch_identity, below
void player_wait_loop();
void start_player_wait();
// A request over the local channel: the stdio bridge's request and answer, with the hello naming this
// game (contract section 4). Every hello answer feeds the playerPending wait. `probing`: the probe's own
// hello, sent before the channel counts as up.
json bridge_channel(const json& body, int timeout_ms, const std::function<bool()>& cancelled = {}, bool probing = false) {
  if (!probing && !g_channel_up) throw BridgeNoAnswer(kChannelClosed);
  const bool hello = body.is_object() && body.value("type", "") == "hello";
  const uint64_t sent_at = now_ms();
  json j = bridge_stdio(hello ? channel::hello_request(body, build_version()) : body, timeout_ms, cancelled);
  if (hello && j.value("ok", false)) {
    const json result = j.count("result") ? j["result"] : json::object();
    const bool pending = channel::hello_pending(result), player = channel::hello_player(result);
    if (player) { std::lock_guard<std::mutex> lock(g_app_player_mutex); g_app_player_id = result["player"].value("id", std::string()); }
    g_channel_signed_out = signin::hello_signed_out(result);
    // This hello's own round trip. Only the probe's own hello adds the time since the probe began: a later
    // hello (a row picked, after sign-in, a reconnect) is minutes after the probe, which read as a delay.
    const char* who = player ? "player" : pending ? "pending" : "no player";
    if (probing)
      host::log("yougame channel: hello answered in %llu ms, %llu ms after the probe (%s)", (unsigned long long)(now_ms() - sent_at),
                (unsigned long long)since_probe(), who);
    else
      host::log("yougame channel: hello again answered in %llu ms (%s)", (unsigned long long)(now_ms() - sent_at), who);
    bool tell = false;
    {
      std::lock_guard<std::mutex> lock(g_channel_mutex);
      g_player_wait.answered(pending, player, now_ms());
      // The probe's own hello: the probe asks for the identity itself once the channel is up.
      if (!probing) tell = g_player_wait.take_arrival();
    }
    if (tell) {
      host::log("yougame channel: player arrived by retry %llu ms", (unsigned long long)since_probe());
      player_arrived();
    }
    if (!probing) start_player_wait();
  }
  return j;
}
// The playerPending retries run once the channel is up (the probe starts them after it counts it up).
void start_player_wait() {
  bool due;
  { std::lock_guard<std::mutex> lock(g_channel_mutex); due = g_player_wait.due_at() != 0; }
  if (due && g_channel_up && !g_player_wait_running.exchange(true)) {
    std::thread([] { player_wait_loop(); g_player_wait_running = false; }).detach();
  }
}
// playerPending: say hello again about 2 s and 6 s after the pending answer (PlayerWait), until the
// player arrives, the push comes, the second retry is answered or the connection closes.
void player_wait_loop() {
  uint64_t scheduled = 0;   // the retry this loop is waiting for, so a cancelled one is logged once
  for (;;) {
    uint64_t at;
    { std::lock_guard<std::mutex> lock(g_channel_mutex); at = g_player_wait.due_at(); }
    if (!at || !g_channel_up) {
      if (scheduled) host::log("yougame channel: retry hello at %llu ms (cancelled)", (unsigned long long)since_probe());
      return;
    }
    scheduled = at;
    const uint64_t now = now_ms();
    if (now < at) { Sleep((DWORD)std::min<uint64_t>(at - now, 250)); continue; }
    { std::lock_guard<std::mutex> lock(g_channel_mutex); g_player_wait.sent(); }
    host::log("yougame channel: retry hello at %llu ms (sent)", (unsigned long long)since_probe());
    scheduled = 0;
    try { (void)bridge_channel({{"type", "hello"}}, channel::kHelloTimeoutMs); }
    catch (const std::exception&) { }   // no answer: the next retry, if any, still goes
  }
}

// Native WinHTTP cancels synchronous requests by closing their handle. The POSIX shim polls its
// callback from libcurl/upgrade waits instead (closing its raw request concurrently is unsafe).
class PendingHttp {
 public:
  PendingHttp(HINTERNET request, const std::function<bool()>& cancelled) : request_(request) {
#ifdef _WIN32
    if (request && cancelled) watcher_ = std::thread([this, cancelled] {
      std::unique_lock<std::mutex> lock(mutex_);
      while (!done_) {
        if (cancelled()) { close(); break; }
        cv_.wait_for(lock, std::chrono::milliseconds(25), [&] { return done_; });
      }
    });
#else
    if (request) WinHttpSetCancelCallback(request, cancelled);
#endif
  }
  ~PendingHttp() {
#ifdef _WIN32
    { std::lock_guard<std::mutex> lock(mutex_); done_ = true; }
    cv_.notify_all();
    if (watcher_.joinable()) watcher_.join();
#endif
    close();
  }
 private:
  void close() { if (auto request = request_.exchange(nullptr)) WinHttpCloseHandle(request); }
  std::atomic<HINTERNET> request_;
#ifdef _WIN32
  std::mutex mutex_;
  std::condition_variable cv_;
  bool done_ = false;
  std::thread watcher_;
#endif
};

// POST <bridge>/sdk {type, ...} -> {ok, result | error}. Only the app's loopback address is
// accepted: the token must never travel anywhere else.
json bridge_http(const json& body, int timeout_ms, const std::function<bool()>& cancelled = {}) {
  if (g_bridge_url.empty() || g_bridge_token.empty()) throw std::runtime_error((g_launcher || g_match_bridge) ? "Start Melee Unlocked from its launcher to play YouGame modes" : "Start Melee from the YouGame app to play online");
  Url u;
  if (!parse_url(g_bridge_url + "/sdk", u) || u.secure || u.host != L"127.0.0.1") throw std::runtime_error((g_launcher || g_match_bridge) ? "The launcher's address is invalid" : "The YouGame app's address is invalid");
  HINTERNET session = http_session();
  if (!session) throw std::runtime_error("Networking is unavailable");
  HINTERNET conn = WinHttpConnect(session, u.host.c_str(), u.port, 0);
  if (!conn) throw BridgeNoAnswer((g_launcher || g_match_bridge) ? "The Melee Unlocked launcher is not answering. Keep it open while you play YouGame modes." : "The YouGame app is not answering");
  HINTERNET req = WinHttpOpenRequest(conn, L"POST", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
  std::string result;
  bool sent = false;
  {
    PendingHttp pending(req, cancelled);
    if (req) {
      DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
      WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof never);
      WinHttpSetTimeouts(req, 5000, 5000, timeout_ms, timeout_ms);
      std::wstring headers = wide("Authorization: Bearer " + g_bridge_token + "\r\nContent-Type: application/json\r\n");
      std::string data = body.dump();
      sent = WinHttpSendRequest(req, headers.c_str(), (DWORD)-1, data.data(), (DWORD)data.size(), (DWORD)data.size(), 0) &&
             WinHttpReceiveResponse(req, nullptr);
      char buf[4096];
      DWORD got = 0;
      while (sent && WinHttpReadData(req, buf, sizeof buf, &got) && got) {
        result.append(buf, got);
        if (result.size() > (1u << 20)) { sent = false; break; }
      }
    }
  }
  WinHttpCloseHandle(conn);
  if (!sent && cancelled && cancelled()) throw std::runtime_error("Cancelled");
  if (!sent) throw BridgeNoAnswer((g_launcher || g_match_bridge) ? "The Melee Unlocked launcher is not answering. Keep it open while you play YouGame modes." : "The YouGame app is not answering");
  json j = json::parse(result, nullptr, false);
  if (j.is_discarded() || !j.is_object()) throw std::runtime_error((g_launcher || g_match_bridge) ? "The launcher sent an invalid answer" : "The YouGame app sent an invalid answer");
  return j;
}

// The site's refusal code from this thread's last not-ok bridge() answer (letters and _ only, <= 24), empty when
// the last call threw for another reason (no answer, cancelled). For logs that must never carry the error text.
thread_local std::string g_bridge_refusal;
// The words of this thread's last update_required answer (update::error_text), empty when it carried none.
thread_local std::string g_bridge_update_text;
// This thread's last bridge() answer when it was an mpTicket refused over the local channel as out of date
// (signin::Update::out_of_date), else null. Only find_match's first ticket reads it (update_offer).
thread_local json g_ticket_out_of_date;
// The local channel: the raw code of this thread's last refusal (letters only, <= 24, for the timing
// lines), and accountSwitching's wait before asking again (0 for any other answer).
thread_local std::string g_channel_code;
// The app's raw answer, {ok:false, code} included, over whichever bridge this run has (stdio, the local
// channel of a game the player started, or the launcher's HTTP). Throws only when nothing answers.
json bridge_raw(const json& body, int timeout_ms) {
  return g_stdio ? bridge_stdio(body, timeout_ms) : routed_to_bridge(body) ? bridge_http(body, timeout_ms)
       : g_channel_allowed ? bridge_channel(body, timeout_ms) : bridge_http(body, timeout_ms);
}
thread_local uint64_t g_channel_retry_after = 0;
json bridge(const json& body, int timeout_ms = 15000, const std::function<bool()>& cancelled = {}) {
  g_bridge_refusal.clear();
  g_bridge_update_text.clear();
  g_ticket_out_of_date = json();
  g_channel_code.clear();
  g_channel_retry_after = 0;
  search_policy::BusyRetry local_retry;
  auto& retry = g_ticket_retry ? *g_ticket_retry : local_retry;
  json j;
  for (;;) {
    if (cancelled && cancelled()) throw std::runtime_error("Cancelled");
    j = g_stdio ? bridge_stdio(body, timeout_ms, cancelled)
        : routed_to_bridge(body) ? bridge_http(body, timeout_ms, cancelled)
        : g_channel_allowed ? bridge_channel(body, timeout_ms, cancelled)
        : bridge_http(body, timeout_ms, cancelled);
    // The app forwards the site's code, including HTTP 409. Text/status alone never gates a build.
    const auto& refused = update::refusal(j);
    if (update::required(refused)) {
      g_bridge_refusal = "update_required";
      g_bridge_update_text = update::listed_newer(refused, build_version()) ? update::error_text(j)
                                                                            : update::not_listed_text(build_version());
      if ((cancelled && cancelled()) || search_stopped()) throw std::runtime_error("Cancelled");
      if (!search_stopped()) {
        g_update_notice.refuse();
        host::log("yougame: update required (current %s)", update::current(refused).c_str());
      }
      throw update::Required();
    }
    // Inspect busy metadata before bridge converts a failure into display text.
    const int delay = body.value("type", "") == "mpTicket" ? retry.delay(j) : 0;
    if (!delay) break;
    // "notHere" is the local channel host's (the app has no page to ask yet), "busy" the site's.
    host::log("yougame: %s, retrying the ticket in %d s (try %d/3)",
              ranked::str(j, "code") == "notHere" ? "the app has no page to ask yet" : "site busy",
              delay, retry.tries());
    if (!search_policy::wait(uint64_t(delay) * 1000, now_ms, [](int ms) { Sleep(ms); },
        [&] { return cancelled && cancelled(); })) throw std::runtime_error("Cancelled");
  }
  // A delivered assignment still needs its scoped join/leave cleanup if cancel won the race.
  const bool assigned = body.value("type", "") == "mpTicket" && j.value("ok", false) &&
      j.count("result") && j["result"].is_object() && !ranked::str(j["result"], "room").empty();
  if (cancelled && cancelled() && !assigned) throw std::runtime_error("Cancelled");
  if (!j.value("ok", false)) {
    g_bridge_refusal = ranked::str(j, "code");
    if (!g_stdio && g_channel_allowed && !routed_to_bridge(body)) {
      // The local channel's host (v1) answers only some types; the rest say notYet. Our words, never a
      // retry: the row's search ends and the game goes back to the menu.
      if (g_bridge_refusal.size() <= 24 && g_bridge_refusal.find_first_not_of(
              "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_") == std::string::npos) g_channel_code = g_bridge_refusal;
      if (g_bridge_refusal == "accountChanged") g_channel_account_changed = true;   // the connection ends next
      // The hidden page was mid-switch (today only mpCode): the connection stays; ask again later.
      if (g_bridge_refusal == "accountSwitching") g_channel_retry_after = channel::retry_after_ms(j);
      if (g_bridge_refusal == "notYet") g_bridge_refusal = "not_yet";
      // A ticket refused as out of date (old_build, build_replaced): kept for find_match, which offers the
      // app's update for the player's first ticket only. Its words still end the search below.
      if (body.value("type", "") == "mpTicket" && signin::Update::out_of_date(j)) g_ticket_out_of_date = j;
      const std::string ours = channel::refusal_text(j);
      if (!ours.empty()) throw std::runtime_error(ours);
    }
    if (g_bridge_refusal.empty() || g_bridge_refusal.size() > 24 ||
        g_bridge_refusal.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string::npos) g_bridge_refusal = "refused";
    std::string error = j.count("error") && j["error"].is_string() ? j["error"].get<std::string>() : friends::kRequestFailed;
    throw std::runtime_error(friends::version_request(body) && friends::version_reason(j) ? friends::kVersion :
                             body.value("ranked", false) ? ranked::ticket_error(j) : error);
  }
  // Linking accepts the documented direct answer as well as the bridge result envelope.
  if (body.value("type", "") == "slippiLink") return j;
  return j.count("result") ? j["result"] : json::object();
}

// MU already owns stdio and the local channel. This narrow yg_match_state adapter uses that
// transport instead of starting yougame.h's second reader. Only the host worker calls it, with
// no callback: even update_required and every other refusal are ignored, without bridge()'s
// UI/account/update policy. No answer or SDK result is ever used by the simulation.
void yg_match_state(int open, yg_callback, void*) {
  const json body = {{"type", "matchState"}, {"open", open != 0}};
  if (g_stdio) bridge_stdio(body, 1000, {}, tourney::DeliveryClock::now() + std::chrono::seconds(1));
  else if (g_channel_allowed) {
    if (g_channel_up) bridge_channel(body, 1000);
  } else if (!g_bridge_url.empty()) bridge_http(body, 1000);
}
match_state::Worker& match_hint_worker() {
  static match_state::Worker worker;
  return worker;
}
// gameState (contract section 9) goes the same narrow way as matchState: stdio or the local channel,
// never bridge()'s refusal policy; the answer is ignored. The channel's state waits for its hello
// (g_channel_up is set only after hello answered) and every new connection hears it once.
void yg_game_state(int state) {
  const char* name = game_state::name(state);
  if (!name) return;
  const json body = {{"type", "gameState"}, {"state", name}};
  // One line per state actually sent: the worker sends only on a change (and once per new
  // connection), so a log shows every playing / paused / menu the app heard, offline modes included.
  if (g_stdio) {
    host::log("yougame: gameState %s", name);
    bridge_stdio(body, 1000, {}, tourney::DeliveryClock::now() + std::chrono::seconds(1));
  } else if (g_channel_allowed && g_channel_up) {
    host::log("yougame: gameState %s", name);
    bridge_channel(body, 1000);
  }
}
uint64_t game_state_epoch() {
  if (g_stdio) return 1;   // one app for the process's life
  return g_channel_allowed && g_channel_up ? g_channel_epoch.load() : 0;
}
game_state::Worker& game_state_worker() {
  static game_state::Worker worker;
  return worker;
}

// Every attempt and reconnect shares one deadline, including locks and stdout.
json tournament_bridge(const json& body, tourney::Deadline deadline, int answer_ms) {
  if (tourney::DeliveryClock::now() >= deadline) throw BridgeNoAnswer("Tournament delivery deadline");
  auto done = tourney::detached([body, deadline, answer_ms] {
    if (tourney::DeliveryClock::now() >= deadline) throw BridgeNoAnswer("Tournament delivery deadline");
    json j = g_stdio ? bridge_stdio(body, answer_ms, {}, deadline) : bridge_http(body, answer_ms);
    if (!j.value("ok", false)) throw std::runtime_error("Tournament result refused");
    return j;
  });
  if (done.wait_until(deadline) != std::future_status::ready) throw BridgeNoAnswer("Tournament delivery deadline");
  return done.get();
}

// ------------------------------------------------------------------ WebSocket (WinHTTP)
// A reader thread queues each JSON message; next() waits on the queue, so the caller can poll its
// cancel flag. Closing the handle from another thread ends the blocking receive.
class Socket;
bool relay_intercept(Socket* socket, const json& m);   // the room relay (below): true when m was its frame
bool arena_intercept(Socket* socket, const json& m);   // the arena's room (below): true when m was its message
void arena_forget(Socket* socket);
void relay_forget(Socket* socket);
class Socket {
 public:
  ~Socket() { relay_forget(this); arena_forget(this); close(); }
  bool open_session(const std::string& url, const std::string& ticket, const std::string& room, std::string* error, int timeout = 10000, const std::function<bool()>& cancelled = {}, const std::string& session = {}) {
    if (cancelled && cancelled()) return false;
    cancel_open_ = cancelled;
    if (!g_broker) return open(url + (room.empty() ? "/match" : "/room/" + url_encode(room)) + "?ticket=" + url_encode(ticket), error, timeout, cancelled);
    if (!broker_) broker_ = std::make_unique<transport::Stream>(g_net_events, [this](const json& q, int ms) { return bridge(q, ms, cancel_open_); });
    try {
      const bool opened = cancelled ? broker_->open(ticket, room, timeout, session, cancelled)
                                    : broker_->open(ticket, room, timeout, session);
      cancel_open_ = {};
      if (opened) return true;
    } catch (const update::Required&) { cancel_open_ = {}; throw; }
    catch (...) {}
    cancel_open_ = {};
    *error = "Lost the connection to YouGame";
    return false;
  }
  bool open_session(const std::string& url, const std::string& ticket, const std::string& room,
                    std::string* error, int timeout, const std::string& session) {
    return open_session(url, ticket, room, error, timeout, {}, session);
  }
  int connection() const { return broker_ ? broker_->conn() : 0; }
  bool recoverable() const { return broker_ && broker_->close_code() == 1006; }
  // Both close codes read 1006 from open until a close names another, so a live connection also "reads" 1006:
  // ask this before treating 1006 as a drop.
  bool connection_closed() const {
    if (broker_) return broker_->closed();
    return closed_.load();
  }
  // Our own socket's connection died: no close frame from the room (code stays 1006) and not our close().
  bool own_dropped() const { return !broker_ && !stopping_ && close_code_ == 1006; }
  // Own socket: when it last heard anything, and last heard a relay frame (a match is passing through it).
  uint64_t heard_at() const { return heard_at_.load(); }
  uint64_t relay_heard_at() const { return relay_heard_at_.load(); }
  // Own socket gone silent mid-match: close it as a drop, so the pump rejoins as after a 1006.
  void abandon() { if (broker_) return; close(); close_code_ = 1006; stopping_ = false; }
  // Friends rooms also hold the seat after Rooms' silent-socket close (4002, rooms/src/room-rules.ts
  // SILENT_CLOSE): it is not final, so the console backend rejoins as it does after a 1006 drop.
  bool rejoinable() const { return recoverable() || (broker_ && broker_->close_code() == 4002); }
  bool lobby_mismatch() const { return broker_ && broker_->lobby_mismatch(); }
  bool kind_refused() const { return broker_ && broker_->kind_refused(); }   // another Teams protocol / Doubles profile
  bool busy() const { return broker_ && broker_->busy(); }
  bool room_full() const { return broker_ && broker_->full(); }
  int close_code() const { return broker_ ? broker_->close_code() : close_code_.load(); }
  bool brokered() const { return broker_ != nullptr; }
#ifdef YOUGAME_AGENT_TEST_HOOK
  uint64_t agent_test_first_open_at() const { return broker_ ? broker_->agent_test_first_open_at() : 0; }
  void agent_test_drop_room() { if (broker_) broker_->agent_test_drop_room(); }
#endif
  // A first connection right after the game starts (or the PC wakes) can find name lookups not ready yet:
  // WinHTTP 12007 (name not resolved) or 12029 (cannot connect) on the first search, where a second try a
  // moment later works. One more try after a second, only for those two, before the player is told.
  bool open(const std::string& address, std::string* error, int timeout, const std::function<bool()>& cancelled) {
    if (open_once(address, error, timeout, cancelled)) return true;
    constexpr DWORD kNameNotResolved = 12007, kCannotConnect = 12029;   // ERROR_WINHTTP_*; the Linux shim has no names for them
    if (last_net_error_ != kNameNotResolved && last_net_error_ != kCannotConnect) return false;
    for (int waited = 0; waited < 1000; waited += 50) {
      if (cancelled && cancelled()) return false;
      Sleep(50);
    }
    host::log("yougame: socket open: trying once more after WinHTTP %lu", (unsigned long)last_net_error_);
    return open_once(address, error, timeout, cancelled);
  }
  bool open_once(const std::string& address, std::string* error, int timeout, const std::function<bool()>& cancelled) {
    last_net_error_ = 0;
    // The Unranked queue reopens on the same Socket after a drop: end the old connection and join its
    // reader first. A new std::thread assigned over a joinable one calls std::terminate, which closed
    // the game with "the game stopped itself (abort)" on the first 1006 drop of a search (121.1, 11-17).
    if (ws_ || reader_.joinable()) close();
    Url u;
    if (!parse_url(address, u) || (!u.secure && u.host != L"127.0.0.1" && u.host != L"localhost")) {
      *error = "Invalid YouGame rooms address";
      return false;
    }
    HINTERNET conn = WinHttpConnect(http_session(), u.host.c_str(), u.port, 0);
    if (!conn) {
      last_net_error_ = GetLastError();
      host::log("yougame: socket open failed: at=%s step=connect error=%lu", address.substr(0, address.find('?')).c_str(), (unsigned long)last_net_error_);
      *error = "Could not reach YouGame";
      return false;
    }
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       u.secure ? WINHTTP_FLAG_SECURE : 0);
    DWORD status = 0, size = sizeof status;
    DWORD forever = 0;
    {
      PendingHttp pending(req, cancelled);
      if (req) WinHttpSetTimeouts(req, timeout, timeout, timeout, timeout);
      // Which WinHTTP step failed, and its error, for the log (a status-0 failure says nothing on screen).
      const char* step = !req ? "open request" : nullptr;
      DWORD step_error = !req ? GetLastError() : 0;
      const auto at = [&](bool done, const char* name) {
        if (!done && !step) { step = name; step_error = GetLastError(); }
        return done;
      };
      bool ok = req && at(WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0), "upgrade option") &&
                at(WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0), "send") &&
                at(WinHttpReceiveResponse(req, nullptr), "receive") &&
                at(WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                       WINHTTP_NO_HEADER_INDEX), "status") &&
                status == 101;
      if (ok) ws_ = WinHttpWebSocketCompleteUpgrade(req, 0);
      if (ok && !ws_ && !step) { step = "complete upgrade"; step_error = GetLastError(); }
      if (!ws_ && !status) last_net_error_ = step_error;
      if (!ws_ && !(cancelled && cancelled()))
        host::log("yougame: socket open failed: at=%s step=%s error=%lu status=%lu", address.substr(0, address.find('?')).c_str(),
                  step ? step : "status", (unsigned long)step_error, (unsigned long)status);
      if (ws_) WinHttpSetOption(ws_, WINHTTP_OPTION_RECEIVE_TIMEOUT, &forever, sizeof forever);
    }
    WinHttpCloseHandle(conn);
    if (cancelled && cancelled()) { if (ws_) { WinHttpCloseHandle(ws_); ws_ = nullptr; } return false; }
    if (!ws_) {
      *error = status == 403 ? "YouGame refused the ticket" : status ? "YouGame refused the connection (HTTP " + std::to_string(status) + ")"
                                                                  : "Could not connect to YouGame";
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.clear();
      closed_ = false;
    }
    close_code_ = 1006; stopping_ = false; heard_at_ = now_ms();
    reader_ = std::thread([this] { read_loop(); });
    return true;
  }
  bool send(const json& j) {
    if (broker_) return broker_->send(j);
    std::lock_guard<std::mutex> lock(send_mutex_);
    if (!ws_ || closed_) return false;
    std::string data = j.dump();
    return WinHttpWebSocketSend(ws_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, data.data(), (DWORD)data.size()) == NO_ERROR;
  }
  // The room relay's sender: never waits behind next()'s blocking read (transport::Stream::send_now).
  // The relay's sends: no open's cancel (open_session reassigns cancel_open_ on the pump thread).
  bool send_now(const json& j) {
    return broker_ ? broker_->send_now(j, [](const json& q, int ms) { return bridge(q, ms); }) : send(j);
  }
  // Up to timeout_ms for the next message; false on timeout or once the socket has closed and drained.
  // Room relay frames (mu.relay) go to the relay here, so no caller ever sees one.
  bool next(json& out, int timeout_ms) {
    const uint64_t until = now_ms() + (uint64_t)std::max(0, timeout_ms);
    for (;;) {
      const uint64_t now = now_ms();
      if (!next_raw(out, now < until ? (int)(until - now) : 0)) return false;
      if (!relay_intercept(this, out) && !arena_intercept(this, out)) return true;
      if (now_ms() >= until) return false;
    }
  }
  bool next_raw(json& out, int timeout_ms) {
    if (broker_) return broker_->next(out, timeout_ms);
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }
  bool closed() const { return broker_ ? broker_->closed() : closed_.load(); }
  void close(int code = 4000) {
    if (broker_) { broker_->close(1000, code); return; }
    stopping_ = true;
    HINTERNET ws = nullptr;
    {
      std::lock_guard<std::mutex> lock(send_mutex_);
      ws = ws_;
      ws_ = nullptr;
    }
    if (ws) {
      WinHttpWebSocketShutdown(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
      WinHttpCloseHandle(ws);
    }
    if (reader_.joinable() && reader_.get_id() != std::this_thread::get_id()) reader_.join();
    closed_ = true;
    cv_.notify_all();
  }

 private:
  std::function<bool()> cancel_open_;
  void read_loop() {
    std::string data;
    std::vector<char> buf(16384);
    for (;;) {
      HINTERNET ws;
      { std::lock_guard<std::mutex> lock(send_mutex_); ws = ws_; }
      if (!ws) break;
      DWORD got = 0;
      WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
      const DWORD rc = WinHttpWebSocketReceive(ws, buf.data(), (DWORD)buf.size(), &got, &type);
      if (rc == NO_ERROR) heard_at_ = now_ms();
      if (rc != NO_ERROR) {
        // Not a close the server sent: a dropped connection (or our own close from another thread).
        if (!stopping_) host::log("yougame: room socket dropped (receive error %lu)", (unsigned long)rc);
        break;
      }
      if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
        // The room's own close: its code says why (4002 silent, 1008 too much, 4004 over, 4011 reset).
        USHORT status = 0; DWORD reason_len = 0; char reason[124] = {};
        if (WinHttpWebSocketQueryCloseStatus(ws, &status, reason, sizeof reason - 1, &reason_len) == NO_ERROR) {
          close_code_ = status;
          std::string why(reason, std::min<DWORD>(reason_len, sizeof reason - 1));
          for (char& c : why) if (c < 0x20 || c > 0x7e) c = '?';
          host::log("yougame: room socket closed by the room (code %u, %s)", (unsigned)status, why.empty() ? "no reason" : why.c_str());
        }
        break;
      }
      if (type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE && type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;
      data.append(buf.data(), got);
      if (data.size() > (1u << 20)) { host::log("yougame: room socket closed: a message over 1 MiB"); break; }
      if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;
      json j = json::parse(data, nullptr, false);
      data.clear();
      if (j.is_discarded() || !j.is_object()) continue;
      if (room_relay::is_frame(j)) relay_heard_at_ = now_ms();
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push_back(std::move(j));
      cv_.notify_all();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    cv_.notify_all();
  }
  std::unique_ptr<transport::Stream> broker_;
  DWORD last_net_error_ = 0;            // own socket: the WinHTTP error of the last failed open with no HTTP status
  std::atomic<int> close_code_{1006};   // own socket: the room's close code, 1006 when the connection just dropped
  std::atomic<bool> stopping_{false};   // our own close(): its receive error is not a drop
  std::atomic<uint64_t> heard_at_{0}, relay_heard_at_{0};
  HINTERNET ws_ = nullptr;
  std::thread reader_;
  std::mutex mutex_, send_mutex_;
  std::condition_variable cv_;
  std::deque<json> queue_;
  std::atomic<bool> closed_{false};
};

// ------------------------------------------------------------------ room relay (yougame_room_relay.h)
// One relay at a time: the match (or console session) that started it owns it and stops it wherever its
// links would close. Its sends go through the room socket it started on; a socket that goes away leaves
// the relay idle until the next socket of the room hears a relay frame (a room reconnect).
std::mutex g_relay_mutex;
std::shared_ptr<room_relay::Relay> g_relay;     // g_relay_mutex
const void* g_relay_owner = nullptr;            // g_relay_mutex
Socket* g_relay_socket = nullptr;               // g_relay_mutex
std::atomic<bool> g_relay_heard{false};         // a relay frame arrived while none ran: a peer relays
// After an explicit no_direct (the app says its links helper is missing), searches skip the attach and
// relay until this steady-clock time: a cooldown, so a helper the app starts again is used once it is
// up. No answer or any other refusal relays only that search or session and leaves this alone; a later
// attach that succeeds clears it.
constexpr uint64_t kNoHelperCooldownMs = 60000;
std::atomic<uint64_t> g_no_helper_until{0};
uint64_t relay_clock() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
void no_helper_cooldown() { g_no_helper_until = relay_clock() + kNoHelperCooldownMs; }
bool no_helper_cooling() { return relay_clock() < g_no_helper_until.load(); }
void helper_attached() { g_no_helper_until = 0; }
void relay_datagram(unsigned link, const void* data, size_t n) {   // yg_enet_socket.c's callback
  std::shared_ptr<room_relay::Relay> r;
  { std::lock_guard<std::mutex> lock(g_relay_mutex); r = g_relay; }
  if (r) r->queue(link, data, n);
}
room_relay::Relay::Send relay_sender(Socket* socket) {
  return [socket](const json& m) { return socket->send_now(m); };
}
void relay_stop(const void* owner) {
  std::shared_ptr<room_relay::Relay> r;
  {
    std::lock_guard<std::mutex> lock(g_relay_mutex);
    if (!g_relay || (owner && g_relay_owner != owner)) return;
    r = std::move(g_relay);
    g_relay_owner = nullptr;
    g_relay_socket = nullptr;
  }
  r->stop();
  yg_enet_relay_end();
  host::log("yougame: room relay ended (%llu sent, %llu received, %llu dropped)", (unsigned long long)r->sent(),
            (unsigned long long)r->received(), (unsigned long long)(r->dropped() + yg_enet_relay_dropped()));
}
// Starts this seat's relay to the other seats (in-game ports). Link ids by port, or empty when it can't.
std::map<int, unsigned> relay_start(const void* owner, Socket* socket, int me, const std::vector<int>& ports) {
  auto links = room_relay::links_for(me, ports);
  if (!socket || links.empty() || me < 1 || me > 4) return {};
  relay_stop(nullptr);
  auto r = std::make_shared<room_relay::Relay>(me, links, relay_sender(socket),
      [](unsigned link, const std::string& bytes) { yg_enet_relay_in(link, bytes.data(), bytes.size()); });
  yg_enet_relay_callback(relay_datagram);
  if (yg_enet_relay_begin((unsigned)links.size())) return {};
  {
    std::lock_guard<std::mutex> lock(g_relay_mutex);
    g_relay = r;
    g_relay_owner = owner;
    g_relay_socket = socket;
    g_relay_heard = false;
  }
  {
    std::lock_guard<std::mutex> lock(g_link_mutex);
    for (const auto& kv : links) g_link_routes[kv.second] = "relay";
  }
  r->start();
  host::log("yougame: room relay on: seat %d, %d peer%s", me, (int)links.size(), links.size() == 1 ? "" : "s");
  return links;
}
bool relay_running(const void* owner = nullptr) {
  std::lock_guard<std::mutex> lock(g_relay_mutex);
  return g_relay && (!owner || g_relay_owner == owner);
}
bool relay_intercept(Socket* socket, const json& m) {
  if (!room_relay::is_frame(m)) return false;
  std::shared_ptr<room_relay::Relay> r;
  {
    std::lock_guard<std::mutex> lock(g_relay_mutex);
    r = g_relay;
    if (!r) { g_relay_heard = true; return true; }
    if (!g_relay_socket) { g_relay_socket = socket; r->rebind(relay_sender(socket)); }
  }
  r->receive(m);
  return true;
}
void relay_forget(Socket* socket) {
  std::lock_guard<std::mutex> lock(g_relay_mutex);
  if (!g_relay || g_relay_socket != socket) return;
  g_relay_socket = nullptr;
  g_relay->rebind({});
}
// The answer that sends this seat to the relay: the app's links helper is missing (no_direct) or the app
// never answered the attach. Read right after the failed request (any later bridge call clears it).
bool helper_refused() { return g_bridge_refusal == "no_direct"; }
// Any refusal with a code (no_direct, ended, ...): the app answered and won't attach.
bool attach_refused() { return !g_bridge_refusal.empty() && g_bridge_refusal != "update_required"; }
// A relay still on from an earlier match (a parked room, or a room whose shutdown has not run yet)
// would take every datagram of a match on links: stop it before links start.
void relay_clear_stale() {
  if (!yg_enet_relay_active()) return;
  host::log("yougame: a room relay was still on from an earlier match: ending it before links");
  relay_stop(nullptr);
  if (yg_enet_relay_active()) yg_enet_relay_end();
}

// ------------------------------------------------------------------ addresses
std::string addr_string(const sockaddr_in& a) {
  char ip[INET_ADDRSTRLEN] = {};
  inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
  return std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
}
bool parse_addr(const std::string& s, sockaddr_in& out) {
  size_t colon = s.rfind(':');
  if (colon == std::string::npos) return false;
  std::memset(&out, 0, sizeof out);
  out.sin_family = AF_INET;
  const auto port = udp::port(s.c_str() + colon + 1);
  if (!port.valid || inet_pton(AF_INET, s.substr(0, colon).c_str(), &out.sin_addr) != 1) return false;
  out.sin_port = htons(port.value);
  return true;
}
bool same_addr(const sockaddr_in& a, const sockaddr_in& b) { return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port; }

// The address this machine uses toward the internet (no packet is sent: connect() on UDP only picks a route).
std::string lan_ip() {
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) return "";
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(53);
  inet_pton(AF_INET, "1.1.1.1", &to.sin_addr);
  std::string out;
  sockaddr_in me{};
  SocketLength len = sizeof me;
  if (connect(s, (sockaddr*)&to, sizeof to) == 0 && getsockname(s, (sockaddr*)&me, &len) == 0) {
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &me.sin_addr, ip, sizeof ip);
    out = ip;
  }
  closesocket(s);
  return out;
}

bool wait_readable(SOCKET s, int timeout_ms) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(s, &set);
  timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
#if defined(__linux__) || defined(__APPLE__)
  return select(s + 1, &set, nullptr, nullptr, &tv) > 0;
#else
  return select(0, &set, nullptr, nullptr, &tv) > 0;
#endif
}

// The external address of `s` as a STUN server sees it (RFC 5389 binding request,
// XOR-MAPPED-ADDRESS). This is the address the opponent's packets must be sent to.
bool stun(SOCKET s, std::string& out, const std::function<bool()>& cancelled) {
  udp::StunIngress ingress(&udp::own_udp_drops);
  static const char* servers[][2] = {{"stun.cloudflare.com", "3478"}, {"stun.l.google.com", "19302"}};
  for (auto& server : servers) {
    if (cancelled()) return false;
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(server[0], server[1], &hints, &res) != 0 || !res) continue;
    sockaddr_in to = *(sockaddr_in*)res->ai_addr;
    freeaddrinfo(res);
    uint8_t req[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
    uint64_t t1 = random64(), t2 = random64();
    std::memcpy(req + 8, &t1, 8);
    std::memcpy(req + 16, &t2, 4);
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (sendto(s, (const char*)req, sizeof req, 0, (sockaddr*)&to, sizeof to) != sizeof req) continue;
      uint64_t until = now_ms() + 500;
      ingress.open({to.sin_addr.s_addr, ntohs(to.sin_port)}, req, until);
      while (now_ms() < until) {
        if (!wait_readable(s, (int)(until - now_ms()))) break;
        uint8_t buf[512];
        sockaddr_in from{};
        SocketLength fl = sizeof from;
        int n = recvfrom(s, (char*)buf, sizeof buf, 0, (sockaddr*)&from, &fl);
        if (n < 0) continue;
        if (!ingress.accept({from.sin_addr.s_addr, ntohs(from.sin_port)}, buf, size_t(n), now_ms(),
            [](const char* message) { host::log("%s", message); })) continue;
        if (udp::stun_reply(buf, size_t(n), req, out)) { ingress.close(); return true; }
      }
      ingress.close();
    }
  }
  return false;
}

// ------------------------------------------------------------------ the punch
// Every player sends small probes from the netplay port to every address a peer published (its LAN
// address and its STUN address) and answers each probe it receives. The first address a peer's
// packet arrives from is the one to use: it works in that direction, and having sent to it opens our
// NAT for the reply. A peer is reachable once its ack shows it got ours too. Each pair of players
// shares secrets passed point to point through the room: a probe carries the token its sender handed
// the receiver, so nobody else (another player in a Teams room included) can answer for a peer.
// ENet drops a late probe as malformed.
constexpr uint8_t kProbe = 0, kAck = 1;
struct PunchPeer {
  int port = 0;                       // 1-based
  uint64_t mine = 0;                  // the token I gave this peer: my probes to it carry it
  uint64_t theirs = 0;                // the token this peer gave me: its probes to me carry it
  std::vector<sockaddr_in> candidates;
  std::vector<const char*> kinds;     // "lan" or "ext", one per candidate (logged, never the address)
  bool found = false, acked = false;
  sockaddr_in chosen{};
};
void send_probe(SOCKET s, const sockaddr_in& to, uint8_t type, uint64_t token, int from_port) {
  uint8_t p[14] = {'Y', 'G', 'P', '2', type};
  std::memcpy(p + 5, &token, 8);
  p[13] = (uint8_t)from_port;
  sendto(s, (const char*)p, sizeof p, 0, (const sockaddr*)&to, sizeof to);
}
// Which published address a reply came from: "other" means the peer's NAT used a port its STUN
// answer did not show (a symmetric NAT); logs name the kind, never the address.
const char* answered_via(const PunchPeer& p, const sockaddr_in& from) {
  for (size_t i = 0; i < p.candidates.size() && i < p.kinds.size(); ++i)
    if (same_addr(p.candidates[i], from)) return p.kinds[i];
  return "other";
}
bool punch(SOCKET s, int my_port, std::vector<PunchPeer>& peers, udp::Ingress& ingress, const std::function<bool()>& cancelled) {
  const uint64_t start = now_ms(), deadline = start + 8000;
  ingress.clear(); // STUN's solicited endpoint is never a gameplay/punch peer.
  for (const auto& peer : peers) for (const auto& a : peer.candidates)
    ingress.allow({a.sin_addr.s_addr, ntohs(a.sin_port)});
  uint64_t next_send = 0, done_at = 0;
  for (;;) {
    const uint64_t now = now_ms();
    bool all = std::all_of(peers.begin(), peers.end(), [](const PunchPeer& p) { return p.found && p.acked; });
    if (all && !done_at) done_at = now;
    // Keep answering for a moment after we are done: the other side may still be waiting for our ack.
    if (done_at && now - done_at > 600) return true;
    if (cancelled()) return false;
    // Not while answering after success: a peer whose ack lands in the last 600 ms must not fail on one side only.
    if (now > deadline && !done_at) {
      // Every failed own-UDP connect leaves one line per peer (10-30: strict-NAT pairs have no fallback yet).
      for (const auto& p : peers) {
        std::string tried;
        for (const char* k : p.kinds) tried += (tried.empty() ? "" : "+") + std::string(k);
        host::log("yougame: connect failed: player %d after %llu ms, tried %s, heard %s, acked %s", p.port,
                  (unsigned long long)(now - start), tried.empty() ? "nothing" : tried.c_str(),
                  p.found ? "yes" : "no", p.acked ? "yes" : "no");
      }
      return false;
    }
    if (now >= next_send) {
      for (auto& p : peers) {
        if (p.found) send_probe(s, p.chosen, kProbe, p.mine, my_port);
        else for (auto& c : p.candidates) send_probe(s, c, kProbe, p.mine, my_port);
      }
      next_send = now + 100;
    }
    if (!wait_readable(s, 20)) continue;
    uint8_t buf[64];
    sockaddr_in from{};
    SocketLength fl = sizeof from;
    int n = recvfrom(s, (char*)buf, sizeof buf, 0, (sockaddr*)&from, &fl);
    if (n < 0) continue;
    // SECURITY: source allowlist precedes every read of the datagram payload.
    if (!ingress.accept({from.sin_addr.s_addr, ntohs(from.sin_port)}, now,
        [](const char* message) { host::log("%s", message); })) continue;
    udp::Probe probe;
    if (!udp::probe(buf, size_t(n), probe)) continue;
    for (auto& p : peers) {
      if (p.port != probe.player || probe.token != p.theirs ||
          std::none_of(p.candidates.begin(), p.candidates.end(), [&](const auto& a) { return same_addr(a, from); })) continue;
      if (!p.found) {
        p.found = true;
        p.chosen = from;
        host::log("yougame: player %d answered after %llu ms via %s", p.port, (unsigned long long)(now - start), answered_via(p, from));
      }
      if (probe.type == kProbe) send_probe(s, from, kAck, p.mine, my_port);
      else p.acked = true;
    }
  }
}

// ------------------------------------------------------------------ session state
std::mutex g_mutex;
Identity g_identity;
bool g_identity_asked = false;
uint64_t g_identity_at = 0;   // GetTickCount64 of the last answer
std::string g_status = "offline";
uint64_t g_status_since = 0;
search_policy::LinkFailures g_direct_link_failures; // g_mutex; survives replacement searches
uint64_t g_firewall_note_until = 0;  // g_mutex; kFirewallNote shows until then (0: not yet this run)
bool g_own_udp_blocked = false;
uint64_t g_last_link_failure = 0;  // g_mutex; when a connect last failed (kRetryLineMs)
bool g_firewall_bound = false;       // g_mutex; this run has bound its own UDP port once (Windows asks then)  // g_mutex; own UDP failed twice in a row (kOwnUdpRetry); cleared on any connect

// The room of the current online session. Stays open through the match for results.
struct Room : transport::RoomState<Socket> {
  bool friends = false;
  // The Friends lobby (yougame_lobby.h) once its match started: its session, this player's id, the screen's
  // control and each friend's link by connection. The room keeps them through the match and, parked between
  // matches (close_room(Leave::Next)), until the lobby's next search adopts the room (find_match).
  std::unique_ptr<lobby::Session> lobby_session;
  std::shared_ptr<lobby::Control> lobby_control;
  std::map<std::string, unsigned> lobby_links;
  std::string lobby_me;
  std::atomic<bool> keep_links{false};   // stopping the pump hands the links to the adopting search
  // The Doubles home's placed match (G2): this room is the match, its home (doubles_home) stays parked in
  // g_parked_lobby. Back on the select (close_room Next/Lost) only this room closes; the home stays.
  bool doubles_match = false;
  std::string doubles_home;
  // The arena's room (arena_room): between games parked in g_arena, adopted by each game's search, handed
  // back when the game ends (close_room). Never begins or reports a match: arena games are unrated.
  bool arena = false;
  ~Room() {
    relay_stop(this);   // before the socket goes: the relay sends through it
    // Also runs on cancelled/failed setup: no abandoned links occupy the three slots.
    transport::close_links(links, [](const json& q, int ms) { return bridge(q, ms); });
  }
};
std::unique_ptr<Room> g_room;
// The Friends lobby's room between matches (g_mutex): its pump keeps the lobby current and runs the screen's
// intents, its links stay up. Only the lobby's next search takes it; any other close leaves it.
std::unique_ptr<Room> g_parked_lobby;
// The arena's room between games (g_mutex), with its own pump. g_arena_open: the arena still wants it (a game's
// room handed back after close() shuts down instead); g_arena_parking: a game's room is on its way back.
std::unique_ptr<Room> g_arena;
bool g_arena_open = false, g_arena_parking = false;
// The arena's side of its room, under its own mutex (the socket's reader and the arena's thread both use it):
// what the room says about its seats, the arena's messages in and out, and the batching sender.
struct ArenaLink {
  std::mutex mutex;
  uint64_t generation = 0;     // each open and close: a late open of an older arena keeps out
  int state = 0;               // arena_room::state
  std::string error, code, me, host;
  json roster = json::array();
  Socket* socket = nullptr;    // the room's socket while the arena has it (its Room moves, the Socket doesn't)
  std::deque<std::pair<std::string, std::vector<uint8_t>>> inbox;
  size_t inbox_bytes = 0;
  std::map<std::string, std::string> outbox;   // connection ("" everyone) -> framed messages waiting
  size_t outbox_bytes = 0;
  int invite = 0;
  std::string invite_error;
  bool inviting = false;
  std::thread sender;
  std::atomic<bool> stop{false};
  std::mutex send_gate;        // held by the sender while it uses `socket`: a closing Socket waits for it
} g_arena_link;
void arena_forget(Socket* socket) {
  std::lock_guard<std::mutex> gate(g_arena_link.send_gate);
  std::lock_guard<std::mutex> lock(g_arena_link.mutex);
  if (g_arena_link.socket == socket) g_arena_link.socket = nullptr;
}
// One arena message in a room msg is [u16 length][bytes], several back to back, base64 in `b`.
constexpr size_t kArenaChunk = 10500;            // raw bytes a room message carries (16 KB once in base64 and JSON)
constexpr size_t kArenaQueueMax = 1u << 20;      // bytes waiting either way before the oldest are dropped
bool arena_intercept(Socket* socket, const json& m) {
  if (!socket) return false;
  std::lock_guard<std::mutex> lock(g_arena_link.mutex);
  ArenaLink& a = g_arena_link;
  if (socket != a.socket) return false;
  const std::string t = ranked::str(m, "t");
  if (t == "welcome" && m.count("me") && m["me"].is_string()) a.me = m["me"].get<std::string>();
  if (m.count("participants") && m["participants"].is_array()) {
    a.roster = m["participants"];
    // The host is the room's creator: Rooms' word for it the first time, else the first seat. It never moves
    // (the host leaving closes the arena), so a later word for another seat is not followed.
    if (a.host.empty() && m.count("host") && m["host"].is_string()) a.host = m["host"].get<std::string>();
    if (a.host.empty())
      for (const auto& p : a.roster)
        if (p.is_object() && p.value("slot", -1) == 0 && p.value("localIndex", 0) == 0) a.host = p.value("connectionId", "");
  }
  if (t != "msg" || !m.count("d") || !m["d"].is_object()) return false;
  const json& d = m["d"];
  if (ranked::str(d, "kind") != "mu-arena") return false;
  std::string bytes;
  if (!d.count("b") || !d["b"].is_string() || !room_relay::unb64(d["b"].get<std::string>(), bytes, kArenaChunk + 64)) return true;
  const std::string from = ranked::str(m, "from");
  for (size_t at = 0; at + 2 <= bytes.size();) {
    const size_t n = (size_t)(uint8_t)bytes[at] << 8 | (uint8_t)bytes[at + 1];
    at += 2;
    if (n == 0 || at + n > bytes.size()) break;
    a.inbox.emplace_back(from, std::vector<uint8_t>(bytes.begin() + (long)at, bytes.begin() + (long)(at + n)));
    a.inbox_bytes += n;
    at += n;
  }
  while (a.inbox_bytes > kArenaQueueMax && !a.inbox.empty()) { a.inbox_bytes -= a.inbox.front().second.size(); a.inbox.pop_front(); }
  return true;
}
std::atomic<unsigned> g_update_searches{0};
bool g_slippi_match = false;  // g_mutex; identity refresh must not switch a live Slippi match's ENet
// g_mutex; this YouGame match plays over own UDP although the hello gave links: a seat has none (links:false,
// a standalone copy). Set when such a match is found, cleared when a search starts or the session ends, so
// an identity refresh mid-match never moves its ENet back onto links.
bool g_own_udp_match = false;
// g_mutex; this match's own UDP failed and every seat agreed to the room relay (mu-relay-ok): its ENet takes
// YouGame sockets carried by the room, also on a copy with no links. Cleared with g_own_udp_match.
bool g_relay_own = false;

void select_match_transport(bool slippi) {
  g_slippi_match = slippi;
#ifdef _WIN32
  yg_enet_select(!slippi && ((g_direct && !g_own_udp_match) || g_relay_own));
#else
  yg_enet_select(!slippi && ((g_broker && !g_own_udp_match) || g_relay_own));
#endif
}
#ifdef YOUGAME_AGENT_TEST_HOOK
bool g_pending_input = false;
struct PendingInputRoster {
  int mode = -2;
  bool ranked = false;
  json participants = json::array();
  std::string me;
} g_pending_input_roster;
#endif
std::shared_future<void> g_retired; // a new search waits until the previous room has left
int g_ranked_rooms = 0;             // ranked rooms this process has played in
ranked::RankCache g_rank; // the newest ranked standing (ticket, mpRank or a set's result) for this process
ranked::RankRead g_rank_read;
ranked::ResultsPanel g_rank_results;
bool g_rank_result_visible = false;
std::string g_rank_opponent, g_rank_score;
// The last ranked room's opponent standing: its DISCONNECTED and result screens still draw the
// opponent's badge after close_room has dropped the room (build 42b run 2).
ranked::RankCache g_rank_opponent_rank;
int g_rank_local = 0;
constexpr uint64_t kDrainMs = 30000;   // a ranked room stays open this long at most for its result

std::string g_blend_reason; // guarded by g_mutex; reset for each search
// The room's own line for a search with nobody to meet yet (rooms "searching".paused, e.g. a lone
// standalone seat or a test build's pool). Guarded by g_mutex; reset for each search.
std::string g_search_paused;
void set_status(const std::string& s) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (search_stopped()) return;
  const std::string shown = !g_blend_reason.empty() && s == blend::kSearchBoth ? blend::kSearchYouGameOnly
      : g_own_udp_blocked && s == search_policy::kLinkRetry ? std::string(search_policy::kOwnUdpRetry) : s;
  if (g_status != shown) host::log("yougame: %s", shown.c_str());
  g_status = shown;
  g_status_since = now_ms();
}

using transport::read_roster;

// Each other human's door: the route and newest round trip of their link, once it is up (nothing before).
std::array<lobby::LinkInfo, lobby::kSeats> lobby_link_info(const lobby::Session& s, const json& roster,
                                                           const std::map<std::string, unsigned>& by_conn) {
  std::array<lobby::LinkInfo, lobby::kSeats> out{};
  const int mine = s.my_seat();
  for (const auto& seat : s.state().seats) {
    if (seat.kind != lobby::Kind::Human || seat.seat == mine || seat.seat < 0 || seat.seat >= lobby::kSeats) continue;
    std::string conn;
    if (roster.is_array())
      for (const auto& p : roster)
        if (p.is_object() && p.value("id", "") == seat.id && p.value("localIndex", 0) == 0) conn = p.value("connectionId", "");
    const auto it = conn.empty() ? by_conn.end() : by_conn.find(conn);
    if (it == by_conn.end() || direct_state(it->second) != 2) continue;
    std::lock_guard<std::mutex> lock(g_link_mutex);
    const auto route = g_link_routes.find(it->second);
    out[seat.seat].route = route == g_link_routes.end() || route->second.empty() ? std::string("unknown") : route->second;
    const auto rtt = g_link_last_rtt.find(it->second);
    out[seat.seat].rtt = rtt == g_link_last_rtt.end() ? -1 : (int)std::min<uint64_t>(rtt->second, 99999);
  }
  return out;
}

transport::RoomIO room_io(Room& r) {
  transport::RoomIO io;
  // A Friends lobby room keeps its lobby through the match and while parked between matches.
  io.message = [&r](const json& m) {
    if (!r.lobby_session) return;
    r.lobby_session->receive(m);
    if (const char* bad = r.lobby_session->take_bad_frame()) host::log("friends lobby: unusable room frame (%s)", bad);
    // sessionEnded (host_ended: the host called off a match that never began) ends only that match: the
    // room stays parked for the lobby (the pump's stop list never includes it).
    if (ranked::str(m, "t") == "sessionEnded")
      host::log("friends lobby: the room ended the match (%s); the room stays", ranked::log_text(ranked::str(m, "reason"), 32).c_str());
    // Rooms dropped the match before the splash and the session let its setup go: so does the room, so
    // the game never plays the old plan and the next Start plans a fresh setup.
    if (r.lobby && !r.lobby_session->has_setup()) {
      host::log("friends lobby: the room dropped the match before it began; its setup is released");
      r.lobby = nullptr;
    }
  };
  io.tick = [&r](std::vector<json>& out) {
    if (!r.lobby_session) return;
    if (r.lobby_control) r.lobby_control->run(*r.lobby_session, lobby_link_info(*r.lobby_session, r.participants, r.lobby_links), r.code);
    for (auto& f : r.lobby_session->due(now_ms())) out.push_back(std::move(f));
  };
  io.keep_links = [&r] { return r.keep_links.load(); };
  io.own_drop = [&r] { return r.socket && r.socket->own_dropped(); };
  io.own_heard = [&r] { return r.socket ? r.socket->heard_at() : 0; };
  io.own_relay_heard = [&r] { return r.socket ? r.socket->relay_heard_at() : 0; };
  io.own_abandon = [&r] { if (r.socket) r.socket->abandon(); };
  io.request = [&r](const json& q, int ms) {
    return bridge(q, ms, [&] { return q.value("type", "") == "mpTicket" && r.stop; });
  };
  io.link_state = direct_state;
  io.disable_links = [] { yg_enet_enable(0); };
  io.end_relay = [&r] { if (relay_running(&r)) { yg_enet_enable(0); relay_stop(&r); } };
  io.log = [](const char* message) { host::log("%s", message); };
  io.started = [](const std::string& id) { g_rank_results.pending(id); };
#ifdef MELEE_DOUBLES
  io.settled = [code = r.code] { doubles::ui.settle_teams(code); };
#endif
  io.result = [&r](const json& m) {
    if (!g_rank_results.receive(m, r.set.local_participant())) g_rank_results.result_error();
    g_rank_result_visible = true;
    g_rank_score = r.set.score_text();   // "Final score": a leave-rule result arrives after close_room
    g_rank_read.recovery_at = 0;
    const bool won = m.count("ranking") && m["ranking"].is_array() && !m["ranking"].empty() &&
                     m["ranking"][0] == r.set.local_participant();
    const bool none = m.count("void") && m["void"].is_boolean() && m["void"].get<bool>();
    host::log("yougame: ranked set result: %s, %s", ranked::str(m, "disposition").c_str(),
              none ? "no winner" : won ? "this player won" : "the opponent won");
    // Whether YouGame rated the set, and its reason when it did not ("Room marked this set unrated" on screen
    // for any reason but pack or void): the player report 11-21 had three unrated sets and no reason anywhere.
    if (m.count("ratingDisposition") && m["ratingDisposition"].is_object()) {
      const json& d = m["ratingDisposition"];
      const bool rated = d.count("rated") && d["rated"].is_boolean() && d["rated"].get<bool>();
      host::log("yougame: ranked set %s%s%s", rated ? "rated" : "unrated by the room",
                rated ? "" : ", reason ", rated ? "" : ranked::log_text(ranked::str(d, "reason"), 48).c_str());
    }
    if (g_rank.result(m, r.set.local_participant())) host::log("yougame: ranked standing: %s", g_rank.header().c_str());
    // No standing in the final (unrated, or no ratings for this seat): the CSS read it before the room
    // recorded the set, so the rank line reads it again now.
    else g_rank_read.again();
  };
  return io;
}
void end_links(Room& r) { transport::end_links(r, room_io(r)); }
void pump_room(Room* r) { transport::pump_room(r, g_mutex, room_io(*r)); }

// The four-seat Friends lobby the page was last told it holds. Every ending tells the page it is gone, or
// its two-seat Friends picker keeps refusing with "This Friends room has 4 seats". Only the room the page
// still holds is cleared, so a slow ending can never wipe the report of a lobby opened after it.
std::mutex g_lobby_report_mutex;
std::string g_lobby_reported;
void report_lobby(const std::string& code, const json& report) {
  std::lock_guard<std::mutex> lock(g_lobby_report_mutex);
  g_lobby_reported = code;
  bridge(report, 1000);
}
// A 2-seat or Doubles room took over the page's report: nothing an older lobby ends may retract it.
void lobby_report_replaced() {
  std::lock_guard<std::mutex> lock(g_lobby_report_mutex);
  g_lobby_reported.clear();
}
void report_lobby_gone(const std::string& code) {
  std::lock_guard<std::mutex> lock(g_lobby_report_mutex);
  if (code.empty() || g_lobby_reported != code) return;
  g_lobby_reported.clear();
  host::log("friends lobby: told the page the room is gone");
  try { bridge({{"type", "mpRoom"}, {"code", nullptr}, {"players", json::array()}}, 1000); } catch (...) {}
}
void shutdown_room(std::unique_ptr<Room> r) {
  if (!r) return;
  r->stop = true;
  if (r->pump.joinable()) r->pump.join();
  if (r->lobby_session) report_lobby_gone(r->code);
  if (r->lobby_session && r->socket && !r->doubles_match) {   // the lobby's own leave frees the seat at once
    r->lobby_session->leave();
    for (const auto& f : r->lobby_session->due(now_ms())) if (f.value("t", "") != "leave") r->socket->send(f);
  }
  end_links(*r);
  if (r->socket) {
    r->socket->send({{"t", "leave"}});
    r->socket->close();
  }
}
// The arena's room back between games (off the game thread): the game's links close, and the socket goes on in
// a fresh Room with only the room's own facts, so nothing of the game (its match, links, recovery) is carried
// to the next one. Parked in g_arena with its own pump, or shut down when the arena closed meanwhile.
void park_arena(std::unique_ptr<Room> old) {
  old->stop = true;
  if (old->pump.joinable()) old->pump.join();
  end_links(*old);
  auto fresh = std::make_unique<Room>();
  fresh->arena = true;
  fresh->size = friends::kArenaSeats;
  fresh->socket = std::move(old->socket);
  fresh->url = old->url; fresh->ticket = old->ticket; fresh->code = old->code; fresh->ticket_request = old->ticket_request;
  fresh->me = old->me; fresh->host_connection = old->host_connection; fresh->participants = old->participants;
  old.reset();
  bool keep;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_arena_parking = false;
    keep = g_arena_open && !g_arena && fresh->socket && !fresh->socket->closed();
    if (keep) {
      Room* raw = fresh.get();
      raw->pump = std::thread(pump_room, raw);
      g_arena = std::move(fresh);
    }
  }
  if (!keep) shutdown_room(std::move(fresh));
}
// Takes the room out of the session at once and leaves it on another thread: the online cleanup
// runs on the game thread, and a search that starts right after must not find the old room.
// Ranked (yougame_ranked.h Set): a quitter concedes; a player whose opponent's connection broke
// claims the set once its own games are settled; a decided set sends its final. A quitter whose
// concession is decided sends what it owes and closes at once, so the room's leave rule can award
// the opponent; everyone else stays for the room's result, kDrainMs at most.
uint64_t close_room(Leave why) {
  std::unique_ptr<Room> r;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    // An arena game is over, however it ended: the arena's room stays for the next game (park_arena). The
    // arena itself (slippi_arena.cpp) decides when the room closes (arena_room::close).
    if (g_room && g_room->arena) {
      g_search_generation.begin();
      friends::events.current_room({});
      g_arena_parking = true;
      host::log("arena: the game is over; the arena's room stays");
      std::thread([old = std::move(g_room)]() mutable { park_arena(std::move(old)); }).detach();
      return g_search_generation.value();
    }
    // The Friends lobby back on its select: its room stays up, parked, for the lobby's next search. A
    // friend's dropped game (Lost) brings everyone back to the select too; only leaving (Quit) closes it.
    // The Doubles home's match back on the select: only the match room closes; its home stays parked (the
    // next search takes it back with its picks, no auto-search).
    if ((why == Leave::Next || why == Leave::Lost) && g_room && g_room->doubles_match) {
      g_search_generation.begin();
      friends::events.current_room({});
      host::log("doubles: back to the home%s; the match room closes", why == Leave::Lost ? " (a connection dropped)" : "");
      std::thread([old = std::move(g_room)]() mutable { shutdown_room(std::move(old)); }).detach();
      return g_search_generation.value();
    }
    if ((why == Leave::Next || why == Leave::Lost) && g_room && g_room->lobby_session) {
      g_search_generation.begin();
      friends::events.current_room({});
      if (g_parked_lobby) std::thread([old = std::move(g_parked_lobby)]() mutable { shutdown_room(std::move(old)); }).detach();
      g_parked_lobby = std::move(g_room);
      host::log("friends lobby: back to the select%s; the room stays", why == Leave::Lost ? " (a connection dropped)" : "");
      return g_search_generation.value();
    }
    if (why == Leave::Next && !g_room) {   // the lobby's own next search: the parked room is its
      g_search_generation.begin();
      return g_search_generation.value();
    }
    if (g_parked_lobby) {
      host::log("friends lobby: left the lobby");
      std::thread([old = std::move(g_parked_lobby)]() mutable { shutdown_room(std::move(old)); }).detach();
    }
    g_search_generation.begin();
#ifdef YOUGAME_AGENT_TEST_HOOK
    g_pending_input = false;
    g_pending_input_roster = {};
#endif
    friends::events.current_room({});
    r = std::move(g_room);
    if (!r) {
#ifdef YOUGAME_AGENT_TEST_HOOK
      if (agent_test::static_quit_requested.load()) host::request_exit(0);
#endif
      return g_search_generation.value();
    }
#ifdef YOUGAME_AGENT_TEST_HOOK
    const bool agent_test_quit = !r->ranked && agent_test::static_quit_requested.load();
#endif
    if (r->friends && why == Leave::Quit && !r->in_game) host::log("friends: cancelled");
    if (r->lobby_session && why != Leave::Quit) host::log("friends lobby: the room closes (%s)", why == Leave::Lost ? "connection lost" : "over");
    bool at_once = true;
    if (r->ranked) {
      if (why == Leave::Quit) r->set.quit();
      else if (why == Leave::Lost) r->set.lost(now_ms());
      at_once = r->set.leaves_at_once() || !r->set.needs_drain();
      if (r->set.leaves_at_once()) host::log("yougame: ranked set concession: the opponent won");
      host::log("yougame: leaving the ranked room (%s)%s", why == Leave::Quit ? "quit" : why == Leave::Lost ? "connection lost" : "set over",
                at_once ? "" : "; staying for its result");
      // Keep the last set visible while its exact room drains its authoritative result.
      g_rank_result_visible = r->set.started();
      g_rank_score = r->set.score_text();
    }
    auto done = std::make_shared<std::promise<void>>();
    g_retired = done->get_future().share();
    const auto result_match = g_rank_results.match();
    std::thread([r = std::move(r), done, at_once, result_match
#ifdef YOUGAME_AGENT_TEST_HOOK
                 , agent_test_quit
#endif
                ]() mutable {
      // The game thread is already back in its menus. Drain this exact room, with its frozen
      // participant ids, before leaving. A replacement search waits on g_retired below.
      if (r->ranked && at_once) {
        transport::stop_and_flush(*r, g_mutex);
      } else if (r->ranked) {
        if (!transport::drain_ranked_room(*r, g_mutex, room_io(*r), kDrainMs))
          host::log("yougame: ranked room left before its result");
      }
#ifdef YOUGAME_AGENT_TEST_HOOK
      if (agent_test_quit) {
        const bool settled = agent_test::drain_static_results([&] {
          std::lock_guard<std::mutex> lock(g_mutex);
          return r->socket->brokered() ? r->games.agent_test_pending()
                                      : r->pending_begin || r->pending_winner != -2;
        }, now_ms, [](int ms) { Sleep(ms); });
        if (!settled) host::log("agent test: quitAfter room left before its result");
      }
#endif
      const bool ranked_room = r->ranked;
      shutdown_room(std::move(r));
      if (ranked_room) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_rank_results.match() == result_match && g_rank_results.state() == ranked::ResultsPanel::State::Pending) {
          if (g_rank_read.unsupported) g_rank_results.recovered_standing(result_match, ranked::RankCache(), true);
          else g_rank_read.recover(now_ms());
        }
      }
      done->set_value();
#ifdef YOUGAME_AGENT_TEST_HOOK
      if (agent_test_quit) host::request_exit(0);
#endif
    }).detach();
    return g_search_generation.value();
  }
}

// The version part of every room key. Every release from 0.9.990 on shares one key (pool_version). While
// YouGame still hands out one of its own older builds, the launcher names that build's version in its
// hello ("pool") and this game searches that build's rooms instead, so a new release plays with whoever
// is on YouGame's current build.
std::mutex g_pool_mutex;
std::string g_pool_listed;
void note_pool(const json& hello) {
  std::string v = hello.is_object() && hello.count("pool") && hello["pool"].is_string() ? hello["pool"].get<std::string>() : std::string();
  if (v.size() > 48 || v.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-") != std::string::npos) v.clear();
  std::lock_guard<std::mutex> lock(g_pool_mutex);
  if (v != g_pool_listed) host::log("yougame: rooms searched: %s", v.empty() ? "every release (shared key)" : v.c_str());
  g_pool_listed = v;
}
// Started app-backed by the launcher (no bridge, so no hello "pool"): the launcher passes the same answer
// in MELEE_YOUGAME_POOL, read once.
std::string launcher_env_pool() {
  static const std::string v = [] {
    const char* e = std::getenv("MELEE_YOUGAME_POOL");
    std::string s = e ? e : "";
    if (s.size() > 48 || s.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-") != std::string::npos) s.clear();
    if (!s.empty()) host::log("yougame: rooms searched: %s (from the launcher, app-backed)", s.c_str());
    return s;
  }();
  return v;
}
std::string room_version() {
  std::lock_guard<std::mutex> lock(g_pool_mutex);
  if (g_pool_listed.empty() && !launcher_env_pool().empty()) return online_version(launcher_env_pool());
  return g_pool_listed.empty() ? pool_version() : online_version(g_pool_listed);
}

// What two builds must share to play: the netplay protocol and this build's game. The rooms Worker
// only matches players with the same key (lobby compatibility).
std::string compatibility(int mode
#if MU_SQUAD
    // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    , bool squad_row = false, const std::string& squad_budget = std::string()
#endif
) {
  const std::string v = room_version();
  const auto& build = slippi::online::local_build();
#if MU_SQUAD
  return squad::compatibility(v, build.mod_view, build.fingerprint, build.static_engine,
      slippi::online::ranked_rules_digest(mode),
      compat_tag_with_test_timer(compat_tag, std::getenv("MELEE_ONLINE_TEST_TIMER")),
      mode, squad_row, squad_budget);
#else
  return compatibility_key(v, build.mod_view, build.fingerprint, build.static_engine,
                           slippi::online::ranked_rules_digest(mode),
                           compat_tag_with_test_timer(compat_tag, std::getenv("MELEE_ONLINE_TEST_TIMER")));
#endif
}

// The lobby's room key: friends-lobby-1 (the Friends lobby) or doubles-lobby-1 (the Doubles home, its
// queue and its match: the same key on every call) ahead of this build's own key.
std::string lobby_room_key(lobby::Profile profile = lobby::Profile::Friends4) {
  const auto& build = slippi::online::local_build();
  const std::string tag = compat_tag_with_test_timer(compat_tag, std::getenv("MELEE_ONLINE_TEST_TIMER"));
  return lobby::compatibility(room_version(), [&](const std::string& version) {
    return compatibility_key(version, build.mod_view, build.fingerprint, build.static_engine,
                             slippi::online::ranked_rules_digest(1), tag);
  }, profile);
}

thread_local bool g_friend_search = false;
thread_local bool g_friend_failed = false;
// Blended Unranked's YouGame side (yougame_blend.h): cancelled because Slippi's side was first, it
// goes quiet ("offline"), not "error: Cancelled" under a Slippi match.
thread_local bool g_blend_search = false;
// The 4-player Friends lobby's search (find_match's lobby_route). Its failures never search again: a
// retry would hold a new room and leave the friends who joined the held code behind.
thread_local bool g_lobby_search = false;
// The Doubles match room this thread's Teams search joins (find_match), empty otherwise. Its transient
// failures search the room again kMaxTries times in all, then stop on the cap's line.
thread_local std::string g_doubles_search_room;
Match fail(const std::string& reason, bool retry = false, bool update_required = false) {
  Match m;
  std::string why = reason;
  m.retry = retry && !update_required;
  if (g_lobby_search) m.retry = false;   // the Friends lobby never searches again into a new room
#ifdef MELEE_DOUBLES
  if (m.retry && !g_doubles_search_room.empty() && !search_stopped()) {
    if (doubles::ui.retry_teams(g_doubles_search_room)) {
      host::log("doubles: the Teams search failed (%s); searching the match room again", why.c_str());
    } else {
      host::log("doubles: the Teams search failed (%s); not searching again after %d tries", why.c_str(), doubles::kMaxTries);
      m.retry = false; why = doubles::kTeamsCappedLine;
    }
  }
#endif
  m.error = why;
  m.update_required = update_required;
  if (search_stopped()) return m;
  if (update_required) {
    m.retry = false;
    set_status(update::kMessage);
    return m;
  }
  if (g_friend_search) {
    if (!g_friend_failed) {
      if (why == friends::kVersion) log_friend_version();
      else host::log("%s", why == "Cancelled" ? "friends: cancelled" : "friends: invite failed: connection failed");
      g_friend_failed = true;
    }
    set_status(why == friends::kVersion ? friends::kVersion : why == "Cancelled" ? "offline" : friends::kFailed);
  } else set_status(why == search_policy::kLinkRetry ? why :
                    g_blend_search && why == "Cancelled" ? "offline" : "error: " + why);
  return m;
}
// Rooms refused a friend room's seat because the room is full: final, never retried. The line ("<host>'s
// room is full.") shows on the character select and as a toast, and the game goes back to Friends.
Match fail_room_full(const std::string& room_code) {
  Match m;
  m.error = friends::room_full_line(friends::events.inviter(room_code));
  if (search_stopped()) return m;
  if (!g_friend_failed) host::log("friends: invite failed: room full");
  g_friend_failed = true;
  set_status("offline");
  friends::events.room_full(m.error, now_ms());
  return m;
}

std::atomic<bool> g_identity_fetching{false};
// The player arrived while an ask was in flight: that ask began before them (its hello was
// "pending"), so it runs once more when it ends instead of leaving "not signed in" until the
// Online menu is opened again (two-PC run 11-13 11:28Z: a fast PC's push landed mid-ask).
std::atomic<bool> g_identity_again{false};

Identity fetch_identity(const std::function<bool()>& cancelled = nullptr) {
  static std::once_flag said;
  std::call_once(said, [] { host::log("yougame: YouGame mode (%s%s)", g_stdio ? "stdio bridge" : g_match_bridge ? (g_channel_allowed ? "local channel for the extension, launcher bridge for matches" : "loopback bridge (the extension's channel is off in this run)") : g_channel_allowed ? "local channel" : "loopback bridge", g_embedded ? ", in the player box" : ""); });
  bool via_channel = !g_stdio && g_channel_allowed && !routed_to_bridge(json{{"type", "hello"}});
  Identity id;
  try {
    json hello = bridge({{"type", "hello"}}, 15000, cancelled);
    if ((cancelled && cancelled()) || search_stopped()) return id;
    std::string sign_in_used;   // hybrid: whose sign-in this identity is (logged below)
    if (g_match_bridge) {
      // Hybrid: the launcher's sign-in first, every time (hello always goes to the launcher). It names nobody and
      // the app's channel names a player: the app's sign-in, as for a game the player started, instead of a refusal.
      note_pool(hello);   // the release the launcher searches, whoever signs in
      const bool launcher_player = channel::hello_player(hello);
      // Who the app's channel names, asked only when the launcher names nobody and the channel is up. Asked even when
      // the app's last hello said signed out: after its sign-in panel this hello is what hears the new player.
      json app_hello = json::object();
      if (!launcher_player && g_channel_up.load()) {
        try {
          const json a = bridge_channel({{"type", "hello"}}, 15000, cancelled);
          if (a.value("ok", false) && a.count("result") && a["result"].is_object()) app_hello = a["result"];
        } catch (const std::exception& e) {
          host::log("yougame: the YouGame app's channel did not answer its hello: %s", channel::log_text(e.what()).c_str());
        }
        if ((cancelled && cancelled()) || search_stopped()) return id;
      }
      const bool app = channel::hybrid_sign_in(launcher_player, g_channel_up.load(), channel::hello_player(app_hello)) ==
                       channel::HybridSignIn::App;
      g_app_sign_in = app;
      via_channel = app;
      if (app) hello = std::move(app_hello);
      sign_in_used = launcher_player ? "the launcher's sign-in"
                   : app ? "the YouGame app's sign-in (the launcher is not signed in to YouGame)"
                   : "no sign-in (the launcher is not signed in to YouGame, and the YouGame app names nobody)";
    }
    { std::lock_guard<std::mutex> lock(g_mutex);
      if (search_stopped()) return id;
      friends::events.hello(hello);
      if (g_launcher) note_pool(hello);   // the hybrid noted the launcher's pool above
      console::launch_hello(hello);
      if (console::requests.pending()) parity::signals.console_boot_ready();
      if (hello.count("caps") && !via_channel) console::read_caps(hello, g_relaunch);   // caps: the app's own child only
    }
    std::string diagnostic;
    {
      std::lock_guard<std::timed_mutex> lock(g_tourney_mutex);
      release_tournament_locked();
      diagnostic = g_tourney.hello(hello);
      g_tourney_queue.hello(hello);
    }
    if (!diagnostic.empty()) host::log("%s", diagnostic.c_str());
#ifdef _WIN32
    const auto mode = transport::Mode::hello(hello);
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (search_stopped()) return id;
      g_broker = g_stdio && mode.broker;
      // A game the player started itself (local channel) takes direct links when the app's hello offers them:
      // the app has the game's process and its page puts the ticket in the links pool. Without `direct` (an old
      // app, an elevated game, a build without links) it keeps own UDP, as before. Read again on every hello.
      // Not a game the launcher started with the extension (g_match_bridge): its matches go through the launcher,
      // whose tickets say links:false, so it connects over direct UDP exactly as without the extension (owner 11-20).
      g_direct = (g_stdio || (via_channel && !g_match_bridge)) && mode.direct;
      if (via_channel) host::log("yougame channel: hello direct=%d", mode.direct ? 1 : 0);
      if (via_channel) {   // what the app's hello offered this copy (the broker's own settings: no account data)
        std::string keys;
        for (auto it = hello.begin(); it != hello.end(); ++it) keys += (keys.empty() ? "" : " ") + it.key();
        const std::string offer = hello.count("netBroker") ? hello["netBroker"].dump().substr(0, 400) : "none";
        host::log("yougame channel: hello keys [%s], netBroker %s", keys.c_str(), offer.c_str());
      }
      select_match_transport(g_slippi_match);
    }
    if (g_stdio && mode.unsupported) throw std::runtime_error("This YouGame direct-link version is not supported");
#else
    const auto route = transport::posix_datagram_route(hello);
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (search_stopped()) return id;
      // The app's broker is the app-started game's; a game on the local channel keeps its own sockets.
      if (route != transport::DatagramRoute::Standalone && !via_channel) g_broker = true;
      g_direct = g_stdio && route == transport::DatagramRoute::Ring;
      select_match_transport(g_slippi_match); // refuse mode also prevents native sockets
    }
    if (g_broker && !g_direct) throw std::runtime_error("YouGame broker requires direct links v1 (1200-byte datagrams)");
#endif
    // Which transport this session took, so a pass can tell direct links from the app's broker.
    // Booleans only: nothing from the hello's player, ticket or addresses.
    host::log("yougame: hello: direct=%d broker=%d stdio=%d", g_direct ? 1 : 0, g_broker ? 1 : 0, g_stdio ? 1 : 0);
    const json& p = hello.count("player") && hello["player"].is_object() ? hello["player"] : json::object();
    id.player_id = p.value("id", "");
    id.name = p.value("name", "");
    if (g_match_bridge) {
      // Hybrid: the launcher's own sign-in when it has one (the app's channel may be signed in as someone else);
      // else the app's, over its channel.
      std::string app_player;
      { std::lock_guard<std::mutex> lock(g_app_player_mutex); app_player = g_app_player_id; }
      g_app_same_player = !g_app_sign_in.load() && g_channel_up.load() && !app_player.empty() && app_player == id.player_id;
      if (g_app_same_player.load()) host::log("yougame: invites and the room report go to the YouGame app (the same player is signed in there)");
      host::log("yougame: matches go through %s with %s; the YouGame app's channel is %s%s",
                g_app_sign_in.load() ? "the YouGame app's channel" : "the launcher's bridge", sign_in_used.c_str(),
                g_channel_up.load() ? "up" : "not up",
                g_app_sign_in.load() || !g_channel_up.load() || app_player.empty() || id.player_id.empty() ? ""
                : app_player == id.player_id ? ", signed in as the same player"
                : ", signed in as ANOTHER player: matches use the launcher's sign-in");
    }
    // accountSwitching (local channel): the page was mid-switch; ask again after its retryAfter, twice at most.
    for (int attempt = 0;; ++attempt) {
      try {
        json code = bridge({{"type", "mpCode"}}, 15000, cancelled);
        if (via_channel) host::log("yougame channel: mpCode answered %llu ms ok", (unsigned long long)since_probe());
        if ((cancelled && cancelled()) || search_stopped()) return id;
        id.code = code.value("code", "");
      } catch (const std::exception& e) {
        if (via_channel)
          host::log("yougame channel: mpCode answered %llu ms %s", (unsigned long long)since_probe(),
                    g_channel_code.empty() ? "no answer" : g_channel_code.c_str());
        // The app said signed out: the card can open now, not on the next hello 5 s later.
        if (via_channel && g_channel_code == "signedOut") g_channel_signed_out = true;
        const uint64_t wait = via_channel && attempt < 2 ? g_channel_retry_after : 0;
        if (wait && search_policy::wait(wait, now_ms, [](int ms) { Sleep(ms); },
                                        [&] { return (cancelled && cancelled()) || !g_channel_up; })) continue;
        host::log("yougame: no connect code: %s", channel::log_text(e.what()).c_str());
      }
      break;
    }
    // Hybrid, the launcher's sign-in: the app's player id can be the game's own id for that player, not the site's,
    // so two ids that differ do not prove two accounts. The connect code is the account's: ask the app's and compare.
    if (g_match_bridge && !g_app_sign_in.load() && g_channel_up.load() && !g_app_same_player.load() && !id.code.empty()) {
      try {
        const json a = bridge_channel({{"type", "mpCode"}}, 4000, cancelled);
        const json r = a.is_object() && a.count("result") && a["result"].is_object() ? a["result"] : a;
        const std::string app_code = r.is_object() ? r.value("code", std::string()) : std::string();
        g_app_same_player = !app_code.empty() && app_code == id.code;
        host::log("yougame: the YouGame app's connect code %s the launcher's%s", app_code.empty() ? "was not given, so it is not known to be"
                  : g_app_same_player.load() ? "is" : "is not",
                  g_app_same_player.load() ? ": invites and the room report go to the YouGame app" : "");
      } catch (const std::exception&) {}
    }
    id.ok = !id.player_id.empty();
    if (!id.ok) id.error = "Sign in to YouGame to play online";
    // Until the player shares their name with this game the SDK calls them "Player"; the tag of
    // their connect code is their own (made from their YouGame name) and reads better online.
    if (id.ok && !hello.value("nameShared", false) && !id.code.empty()) id.name = id.code.substr(0, id.code.find('#'));
  } catch (const std::exception& e) {
    id.error = e.what();
  }
  if ((cancelled && cancelled()) || search_stopped()) return id;
  if (id.ok) host::log("yougame: signed in as %s (%s)", channel::log_text(id.name).c_str(), id.code.empty() ? "no code" : channel::log_text(id.code).c_str());
  else host::log("yougame: not signed in: %s", channel::log_text(id.error).c_str());
  std::lock_guard<std::mutex> lock(g_mutex);
  if (search_stopped()) return id;
  g_identity = id;
  g_identity_asked = true;
  g_identity_at = GetTickCount64();
  return id;
}

// ------------------------------------------------------------------ the local channel's connection
// The identity is asked again now, on this thread (the probe's, never the game's).
// The caller holds g_identity_fetching. Asks until no player arrived during the last ask, then
// lets it go; a player who arrives after the release starts a fresh ask through refresh_identity().
// Only the holder clears g_identity_again (at the top): a thread that loses the flag after its
// release leaves it set for the ask that holds it now.
void run_identity_asks() {
  do {
    g_identity_again = false;
    try { fetch_identity(); } catch (const std::exception& e) { host::log("yougame: sign-in failed: %s", channel::log_text(e.what()).c_str()); }
    g_identity_fetching = false;
  } while (g_identity_again.load() && !g_identity_fetching.exchange(true));
}
void fetch_identity_now() {
  if (g_identity_fetching.exchange(true)) return;
  run_identity_asks();
}
// The player a pending hello waited for is here (told once per connection by PlayerWait).
void player_arrived() {   // logged by the caller (by push or by retry)
  { std::lock_guard<std::mutex> lock(g_mutex); g_identity_at = 0; }   // identity() asks again at once
  g_identity_again = true;   // an ask in flight began before them: it runs once more (run_identity_asks)
  refresh_identity();
}
// A line from the host: the stdio handler takes answers and the pushes it knows, as for the app's
// stdio. Not bridge lines, the pad mirror's handle, and lines that do not parse are skipped.
void channel_line(const std::string& line) {
  std::string handled = line;
  switch (channel::classify(line)) {
    case channel::LineAction::Drop: {
      static std::atomic<int> said{0};
      if (said.fetch_add(1) < 5) host::log("yougame channel: unparsed line (%zu bytes)", line.size());
      return;
    }
    case channel::LineAction::AppOnly:
      return;
    case channel::LineAction::PlayerArrived: {
      bool tell;
      { std::lock_guard<std::mutex> lock(g_channel_mutex); g_player_wait.pushed(); tell = g_player_wait.take_arrival(); }
      if (tell) {
        host::log("yougame channel: player arrived by push %llu ms", (unsigned long long)since_probe());
        // Before the probe counts the channel up, its own identity ask covers the player.
        if (g_channel_up) player_arrived();
      }
      return;
    }
    case channel::LineAction::Answer: {
      // accountChanged is the last answer before the host ends the connection: known before its end.
      if (channel::answer_code(line) == "accountChanged") g_channel_account_changed = true;
      // An answer never carries the app's caps here (they are the app-started game's own).
      if (line.find("\"caps\"") != std::string::npos) {
        json j = json::parse(line.substr(3), nullptr, false);
        if (!j.is_object()) return;
        j.erase("caps");
        handled = "yg " + j.dump();
      }
      break;
    }
    case channel::LineAction::Push:
      if (!g_channel_up) return;   // nothing from a host before it answered hello and passed every check
      break;
  }
  try { handle_app_line(handled); }
  catch (const std::exception&) {
    static std::atomic<bool> said{false};
    if (!said.exchange(true)) host::log("yougame channel: bad line from YouGame");
  }
}
bool start_channel_probe(bool force, bool quiet = false);
// The host closed the connection (it quit, the player signed out or another account signed in), or
// the probe gave up on it. Waiting requests fail at once; the YouGame rows go back to Coming soon.
void channel_closed(const std::shared_ptr<channel::Connection>& conn) {
  bool was_up;
  {
    // One critical section (g_channel_mutex, then g_io.mutex): a stale reader never touches the next
    // candidate's eof.
    std::lock_guard<std::mutex> lock(g_channel_mutex);
    if (g_channel != conn) return;   // a later connection replaced it
    g_channel.reset();
    was_up = g_channel_up.exchange(false);
    g_channel_signed_out = false;
    g_app_sign_in = false;   // hybrid: back to the launcher's bridge (and its sign-in) for everything
    std::lock_guard<std::mutex> io(g_io.mutex);
    g_io.eof = true;
  }
  g_io.cv.notify_all();
  // Taken on every close, up or not: an accountChanged answer to the probe's own hello never stays
  // behind to force a later, unrelated re-probe past the 5 s gate.
  const bool account_changed = g_channel_account_changed.exchange(false);
  if (!was_up) return;
  host::log("yougame channel: closed");
  { std::lock_guard<std::mutex> lock(g_mutex); g_identity = Identity{}; g_identity_asked = false; }
  // accountChanged: connect again (and hello) to hear the new player.
  if (account_changed) { start_channel_probe(true); return; }
  // Otherwise YouGame may be coming back (a tray update quits and restarts it in ~45 s, a slow Windows
  // update longer): look every 5 s for five minutes, as yougame.h's yg_update does (YouGame PR 2355), so
  // the player gets YouGame back without leaving the menu (A2 walk, 11-17).
  static std::atomic<bool> looking{false};
  if (looking.exchange(true)) return;
  std::thread([] {
    for (int i = 0; i < 60 && !g_channel_up.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      start_channel_probe(true);
    }
    if (g_channel_up.load()) host::log("yougame channel: back after close");
    looking = false;
  }).detach();
}
// Which YouGame this game uses (its app, its channel, its app link, its Get YouGame page, its site):
// production (yougame.co) unless the launcher's launcher.ini says yougame_env=staging (yougame_env.h),
// the same setting the launcher itself follows. Read once per run. A build made with
// MELEE_YOUGAME_START_STAGING (a staging test build) always uses staging.
bool start_staging() {
#ifdef MELEE_YOUGAME_START_STAGING
  return true;
#else
  static const bool staging = [] {
    wchar_t module[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, module, MAX_PATH)) return false;
    const bool yes = env::chosen_staging(std::filesystem::path(module).parent_path());
    if (yes) host::log("yougame: YouGame Staging (launcher.ini yougame_env=staging)");
    return yes;
  }();
  return staging;
#endif
}
// Owner 11-20: everything is on YouGame's production now (staging has no test build of a new release, and
// searches there said "Test build not available"), so the GitHub build never falls back to YouGame Staging.
constexpr bool kStagingFallback = false;
// One probe: the YouGame this game uses, production unless staging was chosen (Linux: three folders
// each). The first that connects, passes the checks and answers hello is kept. One line in the log per probe.
// quiet: the background re-look (start_channel's watch) logs only a host it finds, not each empty look.
bool probe_channel(bool quiet = false) {
  g_probe_started = now_ms();
  std::string why;
  const auto where = channel::candidates(&why);
  if (where.empty()) { host::log("yougame channel: refused %s", why.c_str()); return false; }
  std::string refused;
  for (const auto& candidate : where) {
    // Only the YouGame this game uses (start_staging); never the other one (kStagingFallback is off).
    if (candidate.staging != start_staging() && !kStagingFallback) continue;
    std::unique_ptr<channel::Connection> opened;
    why.clear();
    const auto opened_as = channel::Connection::open(candidate, &opened, &why);
    if (opened_as == channel::OpenResult::Missing) continue;
    if (opened_as == channel::OpenResult::Refused) { if (refused.empty()) refused = why; continue; }
    std::shared_ptr<channel::Connection> conn(std::move(opened));
    {
      std::lock_guard<std::mutex> lock(g_channel_mutex);
      g_channel = conn;
      g_player_wait = channel::PlayerWait{};
      std::lock_guard<std::mutex> io(g_io.mutex);
      g_io.eof = false;
    }
    std::thread([conn] { conn->read_lines(channel_line); channel_closed(conn); }).detach();
    json hello;
    try { hello = bridge_channel({{"type", "hello"}}, channel::kHelloTimeoutMs, {}, true); }
    catch (const std::exception&) { hello = json(); }
    if (!hello.is_object() || !hello.value("ok", false)) {
      if (refused.empty()) {
        const std::string code = hello.is_object() ? ranked::str(hello, "code") : std::string();
        refused = !hello.is_object() ? "no hello" : "hello refused" + (code.size() <= 24 && !code.empty() ? " " + code : std::string());
      }
      conn->close();   // its reader ends too
      channel_closed(conn);
      continue;
    }
    const json result = hello.count("result") ? hello["result"] : json::object();
    bool up;
    {
      // Up only while this is still the connection and still open: a host that closed it during
      // hello (its reader may already have dropped it) is no host, never a channel stuck up.
      std::lock_guard<std::mutex> lock(g_channel_mutex);
      up = channel::may_promote(g_channel, conn);
      if (up) { g_channel_epoch.fetch_add(1); g_channel_up = true; g_channel_probing = false; }   // in the same section: a close
      // right after promotion still finds nothing probing, so its forced re-probe is never lost
    }
    if (!up) {
      if (refused.empty()) refused = "closed during hello";
      conn->close();
      channel_closed(conn);
      continue;
    }
    host::log("yougame channel: connected %s (verified %d)", candidate.staging ? "staging" : "production",
              result.is_object() && result.value("verified", false) == true ? 1 : 0);
    { std::lock_guard<std::mutex> lock(g_mutex); g_identity = Identity{}; g_identity_asked = false; }
    start_player_wait();
    fetch_identity_now();   // on this thread, so the Online menu opens the rows once it is in
    return true;
  }
  if (refused.empty()) { if (!quiet) host::log("yougame channel: none"); }
  else host::log("yougame channel: refused %s", channel::log_text(refused).c_str());
  return false;
}
// Starts a probe on its own thread unless one is running or a channel is up. Without `force`, at most
// one every 5 s (the Online menu's retries).
bool start_channel_probe(bool force, bool quiet) {
  if (!g_channel_allowed || g_channel_up) return false;
  {
    std::lock_guard<std::mutex> lock(g_channel_mutex);
    if (!g_channel_retry.allow(now_ms()) && !force) return false;
  }
  if (g_channel_probing.exchange(true)) return false;
  // A probe that brought the channel up cleared the flag itself, and a later probe may own it now.
  std::thread([quiet] { if (!probe_channel(quiet)) g_channel_probing = false; }).detach();
  return true;
}

}  // namespace

// The room worker owns Socket after open. SessionLink only queues sends and polls messages;
// it never waits for a bridge answer from pump(wait_ms).
namespace {
console::FriendsRetire g_console_retired;
void wait_console_retired(const std::function<bool()>& stop = {}) {
  if (!g_console_retired.wait(5000,stop) && !(stop && stop())) host::log("console: previous Friends room still closing");
}
struct ConsoleBackend {
#ifdef MELEE_DOUBLES
  bool doubles_room = false;
  std::function<void(const json&)> observe;
#endif
  std::unique_ptr<Socket> room;
  std::thread worker;
  std::atomic<bool> stop{false}, closed{false};
  std::atomic<bool> rejoining{false};
  std::atomic<bool> retiring{false};
  std::atomic<bool> helper_missing{false}; // the app answered no_direct: its links helper is missing or won't start
  std::shared_future<void> preparation_finished;
  std::vector<json> retired_links; // mutex; SessionLink's destructor queues directClose here
  std::mutex mutex, close_mutex;
  std::condition_variable cv;
  std::deque<json> incoming, outgoing;
  size_t sending = 0; // guarded by mutex; outgoing may be empty while the worker is sending
  ENetSocket datagrams = ENET_SOCKET_NULL;
  std::atomic<int> conn{0};
  int players = 2;
  bool private_queue = true; // the relaunched shared session's ticket carries no queue
  std::atomic<bool> peers_present{true}, handoff_pending{false}, end_previous_on_welcome{false};
  std::string session;
  std::atomic<bool> session_live{false}, interrupted{false}, waiting_previous{false};
  json last_roster;
  std::function<std::string(int)> renew_ticket;
  ~ConsoleBackend() { close(); }
  bool open(const std::string& ticket, const std::string& code, int ms) {
    room = std::make_unique<Socket>(); std::string error;
    bool opened = room->open_session("",ticket,code,&error,ms,session);
    // A cold Rooms isolate can refuse one seat while it fetches the build list. The ticket is still
    // good, so try once more rather than dissolve the whole admission.
    if (!opened && room->busy()) {
      host::log("console: room open refused (busy); retrying once");
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      opened = room->open_session("",ticket,code,&error,ms,session);
    }
    if (!opened) {
      host::log("console: room open refused (close %d, %s)", room->close_code(),
                room->lobby_mismatch() ? "lobby_mismatch" : room->busy() ? "busy" : room->room_full() ? "room_full" : "unclassified");
      return false;
    }
    conn = room->connection();
    worker = std::thread([this,code] {
      // Nothing may leave this thread as an exception: that is std::terminate, which ends the
      // game (exit 3). A room message this build cannot use closes the room instead.
      try {
      uint64_t ping = now_ms(), next_card = 0;
#ifdef YOUGAME_AGENT_TEST_HOOK
      const bool drop_once = transport::agent_drop_room_once_enabled(std::getenv("MELEE_ONLINE_TEST_DROP_ROOM_ONCE"));
#endif
      for (;;) {
#ifdef YOUGAME_AGENT_TEST_HOOK
        if (drop_once && !transport::agent_drop_room_once_attempted && console::console_css_frames() >= 300 &&
            room->brokered() && !room->closed()) {
          bool due = false;
          { std::lock_guard<std::mutex> lock(mutex);
            due = transport::agent_drop_room_once_due(true,console::console_css_frames(),last_roster,
                [](const char* s) { host::log("%s",s); }); }
          if (due) room->agent_test_drop_room(); // 1006 locally: the held-seat rejoin below takes it
        }
#endif
        if (host::exit_requested()) {
          if (!handoff_pending) room->send({{"t","leave"}});
          break; // an intentional shared-session relaunch retains its seat
        }
        json m;
        if (!stop && room->next(m,10)) {
          const auto type = ranked::str(m,"t");
#ifdef MELEE_DOUBLES
          if (observe) observe(m);
          if (!doubles_room) {
#endif
          if (session_live && !handoff_pending && (type == "back" || type == "join" || type == "welcome")) {
            // The live Link validates frozen slots/accounts and new connection ids.
            // Lobby's old host id must not turn a held host's new id into a Leave;
            // leave/lobby rosters still pass, so a released host closes the guest.
            auto control = m; control.erase("players"); control.erase("participants");
            friends::lobby.observe(control); // final room closure still applies immediately
          } else friends::lobby.observe(m);
          if (friends::lobby.cancelled()) console::entry.stop();
#ifdef MELEE_DOUBLES
          }
#endif
          bool ended_session = false;
          if (type == "sessionEnded") {
            std::lock_guard<std::mutex> lock(mutex);
            ended_session = ranked::str(m,"sessionId") == session;
            if (ended_session) { session_live = waiting_previous = false; interrupted = true; }
          }
          else if (m.count("console") && m["console"].is_object()) session_live = true;
          else if (m.count("room") && m["room"].is_object() && m["room"].count("console") && m["room"]["console"].is_object()) session_live = true;
          else if (type == "welcome" || type == "lobby") {
            if (session_live.exchange(false)) interrupted = true;
            waiting_previous = false; // reconnect after expiry can miss sessionEnded
          }
          std::lock_guard<std::mutex> lock(mutex);
          if (type == "welcome") last_roster = m;
          else if (!last_roster.is_null()) {
            for (const auto* key : {"players","participants","host","revision"})
              if (m.count(key)) last_roster[key] = m[key];
            if (type == "leave" && m.count("player") && last_roster.count("participants") && last_roster["participants"].is_array()) {
              auto& seats = last_roster["participants"];
              const auto id = ranked::str(m["player"],"id");
              seats.erase(std::remove_if(seats.begin(),seats.end(),[&](const json& p) {
                return ranked::str(p,"connectionId") == id || ranked::str(p,"id") == id;
              }),seats.end());
            }
          }
          // Preserve a new Start that beats the simulation's return tick, including its save chunks.
          const json* frozen = m.count("console") && m["console"].is_object() ? &m["console"] :
            m.count("room") && m["room"].is_object() && m["room"].count("console") && m["room"]["console"].is_object() ? &m["room"]["console"] : nullptr;
          if (!last_roster.is_null()) {
            if (frozen) {
              last_roster["console"] = *frozen;
              last_roster["matchId"] = m.value("matchId",frozen->value("sessionId",json()));
            } else if (ended_session || type == "welcome" || type == "lobby") {
              last_roster.erase("console"); last_roster.erase("matchId");
              if (last_roster.count("room") && last_roster["room"].is_object()) last_roster["room"].erase("console");
            }
          }
          if (type == "welcome" && end_previous_on_welcome.exchange(false) && frozen &&
              ranked::str(*frozen,"sessionId") == session) {
            waiting_previous = true;
            outgoing.push_back({{"t","end-session"},{"sessionId",session}});
          }
          if (m.count("players") && m["players"].is_array()) {
            const auto me = ranked::str(last_roster,"me");
            peers_present = m["players"].size() == size_t(players) &&
              std::none_of(m["players"].begin(),m["players"].end(),[&](const json& p) {
                return ranked::str(p,"id") != me && p.value("away",json()) == true;
              });
          }
          if (incoming.size() >= 1024) { closed = true; break; }
          incoming.push_back(std::move(m)); cv.notify_all();
          if (type == "welcome") rejoining = false; // publish the fresh roster before allowing opens
        }
        const auto quit = [this] { return stop.load() ||
#ifdef MELEE_DOUBLES
            (!doubles_room && friends::lobby.cancelled()) ||
#else
            friends::lobby.cancelled() ||
#endif
            host::exit_requested(); };
        if (room->closed() && room->rejoinable() && !quit()) {
          rejoining = true;
          // Rooms holds a dropped seat for 90 s (AWAY_MS); 60 s rides out a Rooms deploy's ~30 s reset.
          const auto until = now_ms()+60000;
          bool recovered = false;
          uint64_t backoff = 250; // a refused ticket must not become a request storm
          host::log("console: room transport dropped (%d); trying held-seat rejoin",room->close_code());
          while (!quit() && now_ms() < until && room->rejoinable() && !room->room_full()) {   // full: the seat is gone
            const auto budget = int(std::min<uint64_t>(2000,until-now_ms()));
            try {
              const auto ticket = renew_ticket ? renew_ticket(budget) : std::string();
              std::string current_session;
              { std::lock_guard<std::mutex> lock(mutex); current_session = session; }
              std::string error;
              if (!ticket.empty() && now_ms() < until && room->open_session("",ticket,code,&error,
                  int(std::min<uint64_t>(2000,until-now_ms())),quit,current_session)) {
                conn = room->connection(); recovered = true; break;
              }
            } catch (...) { /* bounded retry, never log a ticket or bridge exception */ }
            const auto left = until > now_ms() ? until-now_ms() : 0;
            { // close() wakes this, so a Leave never waits out a backoff
              std::unique_lock<std::mutex> lock(mutex);
              cv.wait_for(lock,std::chrono::milliseconds(std::min(backoff,left)),[this] { return stop.load(); });
            }
            backoff = std::min<uint64_t>(backoff*2,4000);
          }
          if (recovered) { host::log("console: held-seat rejoin connected"); continue; }
        }
        if (room->closed()) {
#ifdef MELEE_DOUBLES
          if (!doubles_room) {
#endif
          if (room->close_code() == 4012) friends::lobby.closed(4012); // 1006 keeps the held seat
          if (friends::lobby.cancelled()) console::entry.stop();
#ifdef MELEE_DOUBLES
          }
#endif
          closed = true; cv.notify_all(); break;
        }
        std::deque<json> send;
        { std::lock_guard<std::mutex> lock(mutex); send.swap(outgoing); sending = send.size(); }
        if (now_ms()-ping >= 5000) { send.push_back({{"t","ping"},{"at",now_ms()}}); ping = now_ms(); }
        for (size_t k = 0; k < send.size(); ++k) {
          const auto& value = send[k];
          // Pace actual Room sends too: a delayed worker must not burst a queued save over 120/s.
          if (value.count("d") && value["d"].is_object() && ranked::str(value["d"],"kind") == "mu-card") {
            const auto now = now_ms();
            if (now < next_card) std::this_thread::sleep_for(std::chrono::milliseconds(next_card-now));
            next_card = now_ms()+12;
          }
          if (!room->send(value)) {
            if (!room->closed()) continue; // a timeout loses one message, not the stream
            // Dropped socket: keep the rest for after the held-seat rejoin (or the final close).
            std::lock_guard<std::mutex> lock(mutex); outgoing.insert(outgoing.begin(),send.begin()+k,send.end()); break;
          }
        }
        { std::lock_guard<std::mutex> lock(mutex); sending = 0; cv.notify_all(); }
        if (closed || stop) break; // flush queued session-end notices before closing the stream
      }
      } catch (const json::exception& e) {
        host::log("console: room worker stopped on a room message it could not read (json %d)", e.id);
        { std::lock_guard<std::mutex> lock(mutex); closed = true; sending = 0; } cv.notify_all();
      } catch (...) { // never print exception text: bridge and ticket errors can carry secrets
        host::log("console: room worker stopped on an internal error");
        { std::lock_guard<std::mutex> lock(mutex); closed = true; sending = 0; } cv.notify_all();
      }
    });
    return true;
  }
  bool resume_room(bool host) {
    std::lock_guard<std::mutex> lock(mutex);
    if (closed || !last_roster.is_object()) return false;
    auto strip_previous = [this](json& m) {
      const json* frozen = m.count("console") && m["console"].is_object() ? &m["console"] :
        m.count("room") && m["room"].is_object() && m["room"].count("console") && m["room"]["console"].is_object() ? &m["room"]["console"] : nullptr;
      if (frozen && ranked::str(*frozen,"sessionId") == session) {
        m.erase("console"); m.erase("matchId");
        if (m.count("room") && m["room"].is_object()) m["room"].erase("console");
      }
    };
    auto roster = last_roster;
    waiting_previous = session_live && (!roster.count("console") || ranked::str(roster["console"],"sessionId") == session);
    strip_previous(roster);
    for (auto& m : incoming) strip_previous(m);
    incoming.push_front(std::move(roster));
    interrupted = false;
    // A failed peer link cannot resume the old simulation at its last frame.
    // End only that session; the host keeps the room for the next shared boot.
    if (host && waiting_previous) outgoing.push_back({{"t","end-session"},{"sessionId",session}});
    return true;
  }
  bool next(json& m, int ms) {
    std::unique_lock<std::mutex> lock(mutex);
    if (ms > 0) cv.wait_for(lock,std::chrono::milliseconds(ms),[&] { return !incoming.empty() || closed; });
    if (incoming.empty()) return false;
    m = std::move(incoming.front()); incoming.pop_front(); return true;
  }
  bool send(const json& m) {
    std::lock_guard<std::mutex> lock(mutex);
    if (closed || outgoing.size() >= 256) return false;
    outgoing.push_back(m); return true;
  }
  bool flush(int ms) {
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock,std::chrono::milliseconds(ms),[&] {
      return closed || (outgoing.empty() && sending == 0);
    }) && !closed;
  }
  // The room relay (yougame_room_relay.h) instead of links: the app's links helper refused the attach
  // (no_direct, or any other refusal) or never answered, and this session has a room to carry datagrams.
  std::atomic<bool> relay{false};
  bool relay_ready() const { return room && !room->closed(); }
  bool use_relay(const char* why) {
    if (!relay_ready()) return false;
    relay = true;
    host::log("%s (%s)", room_relay::kLog, why);
    return true;
  }
  // Starts this seat's relay once the session knows its seats; each peer port's link, or empty.
  std::map<int,unsigned> relay_open(int me, const std::vector<int>& ports) {
    if (!relay_ready()) return {};
    auto links = relay_start(this, room.get(), me, ports);
    if (links.empty()) return {};
    relay = true; helper_missing = false;
    yg_enet_select(1); yg_enet_enable(1);
    if (datagrams == ENET_SOCKET_NULL) datagrams = yg_enet_create();
    if (datagrams == ENET_SOCKET_NULL) { relay_stop(this); return {}; }
    return links;
  }
  bool attach(int ms) {
    helper_missing = false; // only a no_direct refusal below sets it again
    if (retiring) return false; // a cancelled preparing entry must not wait on its own retire
    wait_console_retired([this] { return retiring.load(); }); // every console-session attach shares the process-global channel
    if (ms <= 0) return false;
    if (retiring) return false;
    if (relay) return true;   // this session already relays (a peer asked, or an earlier attach)
    if (!yg_enet_channel_ready()) {
      if (no_helper_cooling() && use_relay("the links helper refused a moment ago")) return true;
      // Like directOpen: a refused attach answers {ok:false, code} (no_direct when the links helper is
      // missing or won't start). Log the code (letters and _ only) instead of throwing it away.
      json raw;
      try { raw = bridge_raw({{"type","directAttach"}},ms); }
      catch (const BridgeNoAnswer&) {
        host::log("console: link helper attach got no answer");
        if (use_relay("no answer")) return true;
        throw;
      }
      if (!raw.value("ok",false)) {
        std::string code = ranked::str(raw,"code");
        if (code.empty() || code.size() > 24 || code.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string::npos) code = "unknown";
        host::log("console: link helper attach refused (%s)", code.c_str());
        helper_missing = code == "no_direct";
        if (helper_missing) no_helper_cooldown();   // only no_direct is remembered, and only for a while
        if (use_relay(code.c_str())) return true;
        return false;
      }
      const auto h = raw.count("result") ? raw["result"] : json::object();
#ifdef _WIN32
      if (h.value("version",0) != 1 || yg_enet_attach_handles(h.value("section",uintptr_t(0)),
          h.value("toApp",uintptr_t(0)),h.value("toGame",uintptr_t(0)))) return false;
#else
      transport::PosixAttachment p;
      if (!transport::posix_attachment(h,p) || yg_enet_attach_posix(p.section,p.wake,p.wait,p.size)) return false;
#endif
      helper_attached();
    }
    relay_clear_stale();
    yg_enet_select(1); yg_enet_enable(1);
    datagrams = yg_enet_create(); // explicitly virtual: never falls back to an OS UDP socket
    return datagrams != ENET_SOCKET_NULL;
  }
  void close() {
    std::lock_guard<std::mutex> closing(close_mutex);
    { std::lock_guard<std::mutex> lock(mutex); stop = true; }
    cv.notify_all();
    if (worker.joinable()) worker.join();
    closed = true; cv.notify_all();
    relay_stop(this); relay = false;   // before the room: the relay sends through its socket
    // A clean close preserves Rooms' relaunch hold. Never send an explicit leave.
    if (room) { room->close(1000); room.reset(); }
    if (datagrams != ENET_SOCKET_NULL) { yg_enet_destroy(datagrams); datagrams = ENET_SOCKET_NULL; yg_enet_enable(0); }
  }
};
}

namespace console {
namespace {
std::mutex friends_backend_mutex;
std::mutex friends_report_mutex;
std::shared_ptr<ConsoleBackend> friends_backend;
void track_friends_backend(const std::shared_ptr<ConsoleBackend>& b) {
  std::shared_ptr<ConsoleBackend> old;
  {
    std::lock_guard<std::mutex> lock(friends_backend_mutex);
    if (b && b->retiring) return; // a Leave already took it; never reinstall a retiring backend
    old = std::exchange(friends_backend,b);
  }
  // old's destructor joins its worker here, outside the lock the simulation thread also takes
}
std::string current_build() {
  static const auto build = [] {
    wchar_t exe[32768]{};
    if (!GetModuleFileNameW(nullptr,exe,32768)) return std::string();
    return build_fingerprint(std::filesystem::path(exe));
  }();
  return build;
}
int64_t epoch_now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
SessionIO console_io(const std::shared_ptr<ConsoleBackend>& b) {
  SessionIO io;
  // Console boots before the ordinary mod/card scan. Use native Direct's canonical
  // content key in both the waiting game and the fresh boot (no ranked rules).
  // The suffix is the console session protocol: console4 adds game-local save choices and rejoin agreement
  // to the shared host save and straight-to-VS boot; console5 unlocks every fighter and stage instead of
  // taking the roster from the loaded card. Other protocols must never share a session.
  io.compatibility = compatibility_key(room_version(),false,"",false) + "-console5";
#if MU_SQUAD
  // A squad build's heaps, ARAM and Friends costume rule differ from a plain build's (as online,
  // where its keys carry "sq"): a Friends session never mixes the two. Plain keys are unchanged.
  io.compatibility += "-" + yougame::squad::build_tag("");
#endif
  io.host_save = [] {
    const auto read = yougame::console::host_save_reader.load(std::memory_order_acquire);
    return read ? read() : Bytes{};
  };
  io.persist_card = [](const SessionConfig& c) {
    const char* data = std::getenv("YOUGAME_DATA");
    return data && stage_card(std::filesystem::u8path(data),c);
  };
  io.discard_card = [](const SessionConfig& c) {
    const char* data = std::getenv("YOUGAME_DATA");
    if (data && !discard_staged_card(std::filesystem::u8path(data),c))
      host::log("console: couldn't remove staged save; default launch ignores it");
  };
  io.restore_card = [](SessionConfig& c) {
    const char* data = std::getenv("YOUGAME_DATA");
    return data && take_card_choice(std::filesystem::u8path(data),c) && load_card(std::filesystem::u8path(data),c);
  };
  io.exclusive = true;
  io.background_service = true;
  io.random = random64;
  io.request = [b](const json& q,int ms) {
    // A room relay link is this game's own: nothing for the app to close.
    if (ranked::str(q,"type") == "directClose" && q.count("link") && q["link"].is_number_unsigned() &&
        q["link"].get<unsigned>() >= room_relay::kLink0 && q["link"].get<unsigned>() < room_relay::kLink0 + room_relay::kMaxLinks)
      return json::object();
    if (ranked::str(q,"type") == "directClose" && b->retiring) {
      std::lock_guard<std::mutex> lock(b->mutex); b->retired_links.push_back(q);
      return json::object();
    }
    // A refused directOpen answers {ok:false, code} (no_direct, not_room, not_peer, exists, rate_links, ended).
    // bridge() would throw that away; hand the code (letters and _ only) to the link log instead.
    if (ranked::str(q,"type") == "directOpen") {
      json j = bridge_raw(q,ms);
      if (j.value("ok",false)) return j.count("result") ? j["result"] : json::object();
      std::string code = ranked::str(j,"code");
      if (code.empty() || code.size() > 24 || code.find_first_not_of("abcdefghijklmnopqrstuvwxyz_") != std::string::npos) code = "unknown";
      return json{{"link",0},{"refused",code}};
    }
    // A refused console room ticket: log the site's code (never its text) before start_session's catch,
    // so "couldn't connect to your friend" before the friend has even accepted names a reason.
    if (ranked::str(q,"type") == "mpTicket") {
      try { return bridge(q,ms); }
      catch (const update::Required&) { throw; }
      catch (const std::runtime_error&) {
        host::log("console: room ticket refused (%s)", g_bridge_refusal.empty() ? "no answer" : g_bridge_refusal.c_str());
        // bridge() reports an update refusal as "Cancelled" when no search is running (Friends has none). The
        // player needs the update notice, not "couldn't connect" (seen 11-01: a newer build row went up mid-run).
        if (g_bridge_refusal == "update_required") { g_update_notice.refuse(); throw update::Required(); }
        throw;
      }
    }
    return bridge(q,ms);
  };
  io.open_room = [b,compatibility = io.compatibility](const std::string& ticket,const std::string& room,int ms) {
    b->renew_ticket = [room,compatibility,players = b->players,private_queue = b->private_queue](int budget) {
      // Same shape as the session's first ticket (yougame_console_link.cpp), so Rooms sees the same lobby.
      json q = {{"type","mpTicket"},{"room",room},{"players",players},{"mode","console"},{"ranked",false},
        {"lobby",{{"version",1},{"minPlayers",players},{"maxLocalPlayers",1},{"compatibility",compatibility}}}};
      if (private_queue) q["queue"] = "private";
      const auto reply = bridge(q,budget);
      return ranked::str(reply,"ticket");
    };
    return b->open(ticket,room,ms);
  };
  io.room_mismatch = [b] { return b->room && b->room->lobby_mismatch(); };
  io.next_room = [b](json& m,int ms) { return b->next(m,ms); };
  io.send_room = [b](const json& m) { return b->send(m); };
  io.flush_room = [b](int ms) { return b->flush(ms); };
  io.room_closed = [b] { return b->closed.load(); };
  io.room_rejoining = [b] { return b->rejoining.load(); };
  io.rebound = [](const std::string& old_id,const std::string& new_id) { friends::lobby.rebound(old_id,new_id); };
  io.connection = [b] { return b->conn.load(); };
  io.attach = [b](int ms) { return b->attach(ms); };
  io.helper_missing = [b] { return b->helper_missing.load(); };
  io.relay = [b] { return b->relay.load(); };
  io.relay_open = [b](int me,const std::vector<int>& ports) { return b->relay_open(me,ports); };
  io.state = direct_state;
  io.route = [](unsigned id) { std::lock_guard<std::mutex> lock(g_link_mutex); return g_link_routes[id]; };
  io.rtt = [](unsigned id) {
    std::lock_guard<std::mutex> lock(g_link_mutex);
    return std::exchange(g_link_rtts[id],{});
  };
  io.send = [b](unsigned id,const Bytes& bytes) {
    ENetAddress address; if (yg_enet_link_address(id,&address)) return;
    ENetBuffer buffer; buffer.data = const_cast<uint8_t*>(bytes.data()); buffer.dataLength = bytes.size();
    yg_enet_send(b->datagrams,&address,&buffer,1);
  };
  io.receive = [b](unsigned& id,Bytes& bytes) {
    bytes.resize(1200); ENetAddress address{}; ENetBuffer buffer;
    buffer.data = bytes.data(); buffer.dataLength = bytes.size();
    const int n = yg_enet_receive(b->datagrams,&address,&buffer,1);
    if (n <= 0) { if (n < 0) b->closed = true; return false; }
    id = ENET_NET_TO_HOST_32(address.host) & 65535; bytes.resize(size_t(n)); return true;
  };
  io.wait = [b](int ms) { enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE; if (yg_enet_wait(b->datagrams,&condition,ms) < 0) b->closed = true; };
  io.close = [b] {
    if (b->retiring) return; // Leave's retire owns flush/close after Engine::end joins its worker
    if (friends::lobby.cancelled() || host::exit_requested()) { b->send({{"t","leave"}}); b->flush(1000); }
    // Keep receiving authoritative room departure after a peer link ends the game.
    if (!friends::lobby.held() || host::exit_requested()) { b->close(); friends::events.current_room({}); }
  };
  io.log = [](const std::string& line) { host::log("%s",line.c_str()); };
  return io;
}
} // namespace
std::unique_ptr<SessionLink> start_session(const json& request, SessionConfig& config, std::string& error) {
  const char* data = std::getenv("YOUGAME_DATA");
  if (friends::lobby.cancelled() || (data && session_left(std::filesystem::u8path(data),session_id(request)))) {
    error = "left"; return nullptr; // a page Leave during the load must never rejoin
  }
  bool busy;
  { std::lock_guard<std::mutex> lock(g_mutex); busy = bool(g_room) || g_slippi_match; }
  friends::events.current_room(friends::room(request));
  if (!active() || !wait_identity(10000).ok || !g_broker || !g_direct || busy) {
    error = "console sessions need the YouGame link helper";
    host::log("console: start failed: %s",error.c_str()); friends::events.current_room({}); return nullptr;
  }
  if (request.value("players",2) == 2) {
    friends::lobby.begin(false); // welcome supplies the host identity, including migrated ports
    friends::lobby.opened(friends::room(request));
  }
  auto b = std::make_shared<ConsoleBackend>();
  b->session = friends::text(request,"sessionId",64);
  b->players = request.value("players",2);
  b->private_queue = false;
  auto io = console_io(b);
  track_friends_backend(b);
  io.cancelled = [] { return friends::lobby.cancelled() || host::exit_requested(); };
  return start_session(request,config,error,std::move(io));
}
void take_pending_launch() {
  const char* data = std::getenv("YOUGAME_DATA");
  if (!data || !active()) return;
  pending_take(std::filesystem::u8path(data),epoch_now(),current_build(),
      [](const json& q,const auto& consume) { return requests.push(q,true,consume); },
      [](const char* text) { host::log("%s",text); });
}
std::function<void()> leave_friends_room() {
  entry.stop();
  std::shared_ptr<ConsoleBackend> b;
  std::shared_future<void> preparation;
  { std::lock_guard<std::mutex> lock(friends_backend_mutex); b = std::exchange(friends_backend,{}); }
  if (b) {
    b->retiring = true;
    std::lock_guard<std::mutex> lock(b->mutex); preparation = b->preparation_finished;
  }
  requests.clear();
  const char* data = std::getenv("YOUGAME_DATA");
  if (data) { std::error_code ec; std::filesystem::remove(std::filesystem::u8path(data) / "console" / "pending.json",ec); }
  friends::events.current_room({});
  return g_console_retired.begin([b,preparation] {
    const auto began = std::chrono::steady_clock::now();
    host::log("console: Friends leave closing in background");
    // An entry may still be opening this exact room. Let it finish using the backend
    // before closing it; never consult the mutable Entry or replacement backend.
    if (preparation.valid() && preparation.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
      host::log("console: Friends leave waiting for the room to finish opening");
      preparation.wait();
    }
    if (b) {
      // Read the session only now: an opening entry may have committed one meanwhile.
      // FIFO: end the frozen session (if any), then leave lobby membership, then close.
      std::string session;
      { std::lock_guard<std::mutex> lock(b->mutex); session = b->session; }
      const char* data = std::getenv("YOUGAME_DATA");
      if (data && !session.empty() && !mark_session_left(std::filesystem::u8path(data),session))
        host::log("console: couldn't record cancelled launch");
      if (!session.empty()) b->send({{"t","end-session"},{"sessionId",session}});
      b->send({{"t","leave"}});
      b->flush(1000);
    }
    {
      std::lock_guard<std::mutex> report_lock(friends_report_mutex);
      try { bridge({{"type","mpRoom"},{"code",nullptr},{"players",json::array()}},1000); } catch (...) {}
    }
    if (b) {
      for (const auto& q : b->retired_links) { try { bridge(q,1000); } catch (...) {} }
      b->close();
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count();
    host::log("console: Friends leave closed in %lld ms",static_cast<long long>(ms));
  });
}
void close_friends_room(std::function<void()> release) {
  // Engine::end has joined its datagram worker; only the detached retire may close now.
  release();
}
bool wait_friends_retired(int ms) { return g_console_retired.wait(ms); }
void friends_entry(const std::string& room, bool host_reenter, bool held_reenter) {
  if (!entry.begin()) return;
  if (held_reenter) {
    if (!friends::lobby.resume()) { entry.finished(); return; }
  } else friends::lobby.begin(room == "new" || host_reenter, room == "new");
  if (host_reenter) friends::lobby.opened(room);
  friends::events.current_room(room == "new" ? std::string() : room);
  host::log("console: Friends opening room");
  std::thread([room,host_reenter,held_reenter] {
    wait_console_retired(); // also order the new room/page reports after the old empty report
    if (entry.cancelled() || friends::lobby.cancelled()) { entry.finished(); return; }
    auto done = std::make_shared<std::promise<void>>();
    struct Finished {
      std::shared_ptr<std::promise<void>> done;
      ~Finished() { done->set_value(); }
    } finished{done};
    std::shared_ptr<ConsoleBackend> b;
    const auto preparing = done->get_future().share();
    if (host_reenter || held_reenter) {
      // Publish this entry on the reused backend in the same step that reads it, so a Leave that
      // takes it next waits for this entry before its retire closes it.
      std::lock_guard<std::mutex> lock(friends_backend_mutex); b = friends_backend;
      if (b) { std::lock_guard<std::mutex> backend_lock(b->mutex); b->preparation_finished = preparing; }
    }
    if (held_reenter && !host_reenter && b && b->closed) {
      friends::lobby.session_stopped(false); entry.finished(); return;
    }
    const bool reuse = b && !b->closed;
    if (!reuse) {
      std::string previous;
      if (b) { std::lock_guard<std::mutex> lock(b->mutex); previous = b->session; }
      b = std::make_shared<ConsoleBackend>(); b->session = std::move(previous);
      b->end_previous_on_welcome = host_reenter && !b->session.empty();
    }
    { std::lock_guard<std::mutex> lock(b->mutex); b->preparation_finished = preparing; }
    track_friends_backend(b);
    auto io = console_io(b);
    if (reuse) {
      io.open_room = [b,host_reenter](const std::string&,const std::string&,int) {
        return b->resume_room(host_reenter);
      };
    }
    io.previous_session_active = [b] { return b->waiting_previous.load(); };
    io.friends_lobby = true;
    io.handoff_join = friends_handoff_join_enabled();
    if (io.handoff_join) {
      const auto info = std::atomic_load_explicit(&handoff_join::published_info, std::memory_order_acquire);
      if (info) io.handoff_info = *info;
    }
    io.css_scene = [] { return css_join::scene.load(std::memory_order_acquire); };
    io.css_lock = [](bool on) {
      const bool before = css_join::locked.exchange(on,std::memory_order_acq_rel);
      if (before != on) host::log("css-join lock: %s",on ? "on" : "off");
    };
    io.css_hold = [](const std::string& name) {
      const auto line = (name.empty() ? "Your friend" : name)+" joins when this match ends";
      host::log("%s",line.c_str()); entry.status(line);
      friends::events.notice(line,transport::now_ms());
    };
    if (css_join::available.load(std::memory_order_acquire)) io.css_begin = [] {
      auto channel = std::make_shared<css_join::Channel>();
      std::atomic_store(&css_join::pending,channel); return channel;
    };
    io.handoff_css = [] { return handoff_join::host_css.load(std::memory_order_acquire); };
    io.handoff_begin = [] {
      auto channel = std::make_shared<handoff_join::Channel>();
      std::atomic_store_explicit(&handoff_join::pending_channel,channel,std::memory_order_release);
      return channel;
    };
    io.held_reenter = host_reenter || held_reenter;
    io.committed = [b](const SessionConfig& c) {
      std::lock_guard<std::mutex> lock(b->mutex); b->session = c.session_id; b->interrupted = false;
    };
    io.opened = [](const std::string& code) {
      friends::events.current_room(code); friends::lobby.opened(code);
      // A relaunch may follow: the caches start writing now, so the flush at the handoff usually
      // has nothing left to write and the handoff is not delayed.
      if (g_stdio && g_relaunch) ::gx::cache_store::request_flush_all("relaunch");
    };
    auto last_room = std::make_shared<json>();
    io.roster = [last_room](const std::string& code,const json& participants,const std::string& me,int revision) {
      std::lock_guard<std::mutex> report_lock(friends_report_mutex);
      if (friends::lobby.cancelled() || entry.cancelled()) return;
      std::string player;
      for (const auto& p : participants) if (ranked::str(p,"connectionId") == me) player = ranked::str(p,"id");
      // This report completes the page's held-room contract before mpFriend.room is sent.
      *last_room = {{"type","mpRoom"},{"code",code},{"queue","private"},{"size",2},{"me",player},
          {"players",participants},{"participants",participants},{"revision",revision}};
      lobby_report_replaced();
      bridge(*last_room,1000);
      friends::lobby.report_ready();
    };
    // The guest's wait for the host's state, as the latest room report plus loading (0-100), so the page
    // can show the same progress as the game's waiting row.
    io.progress = [last_room](int percent) {
      std::lock_guard<std::mutex> report_lock(friends_report_mutex);
      if (friends::lobby.cancelled() || entry.cancelled() || last_room->empty()) return;
      auto q = *last_room; q["loading"] = percent;
      try { bridge(q,1000); } catch (...) {}
    };
    io.css_ready = [b] { return friends::lobby.ready() && b->peers_present.load(); };
    io.freeze_pick = [](const std::string& name) { return friends::lobby.freeze(name); };
    io.cancelled = [] { return friends::lobby.cancelled() || entry.cancelled() || host::exit_requested(); };
    io.confirm_start = [] { return entry.start(); };
    std::string last;
    io.waiting = [&last](const std::string& text,bool ready) {
      entry.status(text,ready);
      if (last != text) { host::log("console: %s",text.c_str()); last = text; }
    };
    bool prepared = false; std::string prepared_session; int64_t handed_at = 0;
    std::shared_ptr<RelaunchWait> relaunch_wait;
    const bool relaunching = g_stdio && g_relaunch;
    const auto css_lock = io.css_lock;
    HandoffIO handoff_io; handoff_io.clock = io.clock; handoff_io.cancelled = io.cancelled;
    io.prepared = [&,handoff_io](const SessionConfig& c) {
      if (friends::lobby.cancelled() || entry.cancelled() || host::exit_requested()) return;
      prepared_session = c.session_id;
      { std::lock_guard<std::mutex> lock(b->mutex); b->session = c.session_id; }
      handed_at = epoch_now();
      if (relaunching) {
        relaunch_wait = std::make_shared<RelaunchWait>(prepared_session,handed_at);
        std::atomic_store(&g_relaunch_wait,relaunch_wait);
        // The app closes this game and starts the session in a new one, which reads the shader and
        // pipeline caches as it starts: write them before asking (bounded; this is not a frame).
        ::gx::cache_store::flush_all("relaunch",std::chrono::milliseconds(3000));
      }
      const char* data = std::getenv("YOUGAME_DATA");
      b->handoff_pending = true;
      prepared = handoff(c,relaunching,data ? std::filesystem::u8path(data) : std::filesystem::path(),
          epoch_now(),current_build(), [](const json& q) { write_line("yg " + q.dump()); },
          [](const std::string& line) { host::log("%s",line.c_str()); },
          [](const char* text) { entry.status(text); },handoff_io);
      if (prepared && relaunching) {
        css_join::relaunching.store(true,std::memory_order_release);
        css_lock(true);
      }
      if (!prepared) b->handoff_pending = false;
    };
    // Keep the frozen room alive while the user closes/relaunches the game. Rooms owns the gap.
    io.close = [] {};
#ifdef MELEE_DOUBLES
    auto doubles_home = std::make_shared<std::string>(); // the adopted Link keeps a copy of io
    io.doubles_home = [doubles_home](const std::string& code) { *doubles_home=code; };
#endif
    bool css_adopted = false;
    io.css_adopted = [&] { css_adopted = true; };
    SessionConfig c; std::string error;
    try {
      if (!g_broker || !g_direct) {
        error = "console sessions need the YouGame link helper";
        host::log("console: start failed: %s",error.c_str());
      } else {
        auto connected = start_session({{"room",room},{"players",2},{"inputDelay",2}},c,error,std::move(io));
#ifdef MELEE_DOUBLES
        if (!doubles_home->empty()) {
          // The welcome path already holds a Friends seat in the home; free it before Doubles rejoins.
          b->send({{"t","leave"}}); b->flush(1000); b->close();
          friends::events.current_room({}); friends::lobby.leave(""); entry.finished();
          doubles::entry(*doubles_home); return;
        }
#endif
        if (css_adopted) {
          b->handoff_pending = false; b->session_live = true;
          entry.finished(); return; // Both engines acknowledged installation before this return.
        }
      }
      if (!prepared && !error.empty() && !entry.cancelled())
        entry.status(error == update::kMessage ? std::string(update::kMessage) :
                     session_notice(error) ? session_notice(error) : "Couldn't start playing together: " + error);
      while (prepared && !friends::lobby.cancelled() && !entry.cancelled() && !host::exit_requested() && !b->closed) {
        json m; b->next(m,50);
        if (relaunch_wait && relaunch_wait->reason()) {
          host::log("console: relaunch dropped by the app: %s",relaunch_wait->reason());
          entry.status(kRelaunchDropped); break;
        }
        // The app closes this game and starts the session; still here long after means it didn't.
        if (relaunch_wait && relaunch_wait->timed_out(epoch_now())) {
          host::log("console: no relaunch 45 s after the request; the app didn't start the session");
          entry.status("Couldn't start playing together. Close the game and try again."); break;
        }
        if (ranked::str(m,"t") == "sessionEnded" && ranked::str(m,"sessionId") == prepared_session) {
          const auto reason = friends::text(m,"reason",160);
          host::log("console: session ended: %s",reason.c_str());
          entry.status(session_notice(reason) ? session_notice(reason) : "Lost connection to your friend."); break;
        }
      }
    } catch (...) {
      host::log("console: start failed: couldn't start playing together");
      entry.status("Couldn't start playing together");
    }
    css_join::relaunching.store(false,std::memory_order_release);
    if (css_join::locked.exchange(false,std::memory_order_acq_rel)) host::log("css-join lock: off");
    std::atomic_store(&g_relaunch_wait,std::shared_ptr<RelaunchWait>{});
    if ((b->interrupted || (b->closed && !friends::lobby.host())) && !friends::lobby.cancelled() && !entry.cancelled()) {
      friends::lobby.session_stopped(!b->closed);
      entry.finished();
      return; // same room/socket; host's simulation tick reenters local CSS
    }
    if (!b->retiring && (!prepared || friends::lobby.cancelled() || entry.cancelled())) {
      std::string session;
      { std::lock_guard<std::mutex> lock(b->mutex); session = b->session; }
      const char* data = std::getenv("YOUGAME_DATA");
      if (data && !session.empty()) mark_session_left(std::filesystem::u8path(data),session);
      if (!session.empty()) b->send({{"t","end-session"},{"sessionId",session}});
      b->send({{"t","leave"}}); b->flush(1000);
      if (!friends::lobby.cancelled()) friends::lobby.leave(entry.status());
    }
    if (!b->retiring) b->close();
    friends::events.current_room({});
    entry.finished();
  }).detach();
}
} // namespace console
#ifdef MELEE_DOUBLES
#include "yougame_doubles_runtime.inc"
#endif

namespace relay_pads {
void logging_ready() {
  g_relay_pads.logging_ready();
  static std::once_flag once;
  std::call_once(once, [] {
    if (yougame::active() && *compat_tag)
      host::log("yougame: branch build \"%s\": matches only the same branch", compat_tag);
#ifdef _WIN32
    if (g_embed_toplevel && g_toplevel_dpi_failed)
      host::log("embed: toplevel DPI awareness failed err=%lu", (unsigned long)g_toplevel_dpi_error);
#endif
    std::atexit([] { g_console_retired.wait(2000); g_relay_pads.flush(); host::log_flush(); });
  });
}
bool active() { return g_relay_pads.active(); }
bool read(unsigned slot, _XINPUT_STATE* state) { return g_relay_pads.read(slot, state); }
bool poll(unsigned slot, _XINPUT_STATE* state, const std::function<bool()>& native) {
  return g_relay_pads.poll(slot, state, native);
}
void rumble(const std::function<void()>& native) {
  g_relay_pads.rumble(native, host::controller_relay::rumble_writer.load(std::memory_order_acquire) != nullptr);
}
bool owns_hid() { return g_relay_pads.owns_hid(); }
#ifdef YOUGAME_AGENT_TEST_HOOK
void arm_rumble_test(double boot_ms) {
  std::lock_guard<std::mutex> lock(g_relay_rumble_mutex);
  g_rumble_pulse.arm(boot_ms);
}
#endif
}

void start() {
  static bool started = false;
  if (started) return;
  started = true;
  auto take = [](const char* name) {
    const char* v = std::getenv(name);
    std::string out = v ? v : "";
    if (v) _putenv_s(name, "");
    return out;
  };
  // The app or the launcher started this game: never the local channel (set at all, even empty).
  // MELEE_STARTED_BY_APP (no secret) carries that into a game this one restarts (settings, an older
  // release), which no longer sees the app's variables once take() has cleared them.
  const char* match_bridge = std::getenv("MELEE_YOUGAME_MATCH_BRIDGE");
  const bool hybrid = match_bridge && std::strcmp(match_bridge, "1") == 0;
  g_channel_allowed = !kNoApp && !std::getenv("YOUGAME_STDIO") && (hybrid || !std::getenv("YOUGAME_BRIDGE")) &&
                      !std::getenv("MELEE_YOUGAME_LAUNCHER") && !std::getenv("MELEE_STARTED_BY_APP");
  if (!g_channel_allowed) _putenv_s("MELEE_STARTED_BY_APP", "1");
  g_bridge_url = take("YOUGAME_BRIDGE");
  g_bridge_token = take("YOUGAME_TOKEN");
  g_stdio = take("YOUGAME_STDIO") == "1";
  slippi_linking::presentation.enabled = take("YOUGAME_SLIPPI_LINKABLE") == "1";
  g_launcher = take("MELEE_YOUGAME_LAUNCHER") == "1" && !g_stdio;
  take("MELEE_YOUGAME_MATCH_BRIDGE");
  g_hybrid_env = hybrid;
  if (!g_stdio && !g_bridge_url.empty() && !g_bridge_token.empty()) host::set_before_restart(restore_bridge_for_restart);
  // The hybrid with the channel allowed; without it (no extension) it is the launcher's bridge alone.
  g_match_bridge = hybrid && !g_stdio && g_channel_allowed && !g_bridge_url.empty() && !g_bridge_token.empty();
  if (hybrid && !g_stdio && !g_match_bridge && !g_bridge_url.empty()) g_launcher = true;
  host::test_env::yougame_session = active();
#ifdef MELEE_LINUX_X11_EMBED
  // The shared X11 helper checks this non-secret flag at window attachment.
  const char* embed = std::getenv("YOUGAME_EMBED");
  g_embedded = embed && std::strcmp(embed, "1") == 0;
#else
  g_embedded = take("YOUGAME_EMBED") == "1";
#endif
#ifdef _WIN32
  g_embed_toplevel = embed_toplevel_policy::enabled(g_embedded, take("YOUGAME_EMBED_TOPLEVEL"));
  g_app_pid = embed_toplevel_policy::app_pid(take("YOUGAME_APP_PID"));
  if (g_embed_toplevel) {
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
      g_toplevel_dpi_error = GetLastError();
      g_toplevel_dpi_failed = true;
    }
  }
#endif
  if (g_channel_allowed) g_io.eof = true;   // until a channel connection is open (start_channel)
  if (g_stdio) {
    std::thread(read_stdin).detach();
    // Toplevel caps wait for successful creation with the requested styles.
    if (!g_embed_toplevel) parity::signals.start(true, [](const char* line) { write_line(line); });
    parity::signals.sync_match(-1); // no log: the log file isn't open yet (see below)
    host::controller_relay::install_rumble(relay_rumble);
    host::controller_relay::install_route(relay_route);
    std::thread(relay_rumble_writer).detach();
  }
  // Nothing is logged here: this runs before the log file is open, and an early line would send the
  // whole log to stdout, which in stdio mode is the bridge. fetch_identity says which mode this is.
  try { match_hint_worker().start([](int open) { yg_match_state(open, nullptr, nullptr); }); }
  catch (...) {} // A host without resources for hints still starts the game.
  try { game_state_worker().start(game_state_epoch, yg_game_state); }
  catch (...) {} // Without it the button keeps its old fade; the game still starts.
}

void online_match_state(bool open) {
  match_hint_worker().set(open);
  if (open) g_update_match_started.store(true);   // the Update panel never sits over a match
}

static std::atomic<int> g_reported_game_state{game_state::kNone};
void report_game_state(int state) {
  g_reported_game_state.store(state, std::memory_order_relaxed);
  game_state_worker().set(state);
}
bool app_button_shown() {
  return !g_stdio && g_channel_allowed && g_channel_up.load() &&
         g_reported_game_state.load(std::memory_order_relaxed) != game_state::kPlaying;
}

bool active() { return g_stdio || (!g_launcher && !g_match_bridge && !g_bridge_url.empty() && !g_bridge_token.empty()); }
bool launcher() {
  return !coming_soon() && (((g_launcher || g_match_bridge) && !g_bridge_url.empty() && !g_bridge_token.empty()) || g_channel_up);
}
bool channel_connected() { return g_channel_up; }
static std::atomic<bool> g_automated_run{false};
void start_channel(bool automated) {
  if (automated) g_automated_run = true;
  static std::once_flag once;
  std::call_once(once, [automated] {
    if (!g_channel_allowed) return;
    // Bare test runs never reach a YouGame app on the machine they run on, unless the run asks.
    if (host::test_env::getenv("MELEE_NO_YOUGAME_CHANNEL")) { g_channel_allowed = false; return; }
    if (automated && !host::test_env::getenv("MELEE_TEST_YOUGAME_CHANNEL")) { g_channel_allowed = false; return; }
    start_channel_probe(true);
    // Hidden test runs only: MELEE_TEST_YG_MENU_KEY_MS=N presses Shift+Tab N ms from now (a hidden window gets no keys).
    if (const char* ms = host::test_env::getenv("MELEE_TEST_YG_MENU_KEY_MS"))
      std::thread([wait = std::atoi(ms)] {
        std::this_thread::sleep_for(std::chrono::milliseconds(wait));
        const bool taken = channel_menu_key("keys");
        host::log("yougame: test Shift+Tab: %s, channel %s", taken ? "taken" : "left to the game", g_channel_up.load() ? "up" : "down");
      }).detach();
    // A YouGame app started after the game (Get YouGame in the start window, or opened by hand) is found
    // without a restart: look again every 4 s while no channel is up. A look is a loopback connect that
    // fails at once when nothing listens; start_channel_probe refuses overlap and keeps its 5 s gate.
    std::thread([] {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(4));
        if (!g_channel_allowed) return;
        if (!g_channel_up.load() && !g_channel_probing.load()) start_channel_probe(false, true);
      }
    }).detach();
  });
}
namespace {
// Hidden and headless runs (agent copies, tests) never open a browser or an app on the machine they run on.
// So do labelled AGENT TEST copies and runs with the channel off (MELEE_NO_YOUGAME_CHANNEL, the app started it).
bool may_open_outside(const char* what) {
  const bool test = g_automated_run.load() || host::test_env::agent_copy() || !g_channel_allowed;
  if (!test || host::test_env::getenv("MELEE_TEST_YOUGAME_GET_PAGE")) return true;
  host::log((std::string("yougame: ") + what + " (test run: not opened)").c_str());
  return false;
}
// A fixed address handed to the system's opener (the browser for https, the YouGame app for its link type).
bool open_outside(const char* target) {
#ifdef _WIN32
  // Always on a thread of its own: the shell wants COM set up there (link types can go through shell extensions).
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const bool ok = (INT_PTR)ShellExecuteA(nullptr, "open", target, nullptr, nullptr, SW_SHOWNORMAL) > 32;
  if (SUCCEEDED(com)) CoUninitialize();
  return ok;
#else
#ifdef __APPLE__
  const char* tool = "open";
#else
  const char* tool = "xdg-open";
#endif
  char* argv[] = {const_cast<char*>(tool), const_cast<char*>(target), nullptr};
  pid_t pid = 0;
  if (posix_spawnp(&pid, tool, nullptr, nullptr, argv, environ) != 0) return false;
  std::thread([pid] { int st = 0; waitpid(pid, &st, 0); }).detach();   // reap it
  return true;
#endif
}
// The installed app registers this link type; the one link shape it accepts
// (desktop/src/security-policy.ts deepLinkUrl) brings it up on the game's page.
const char* app_link(bool staging) { return staging ? "yougame-staging://g/melee-unlocked" : "yougame://g/melee-unlocked"; }
std::atomic<int> g_app_installed{-1};       // -1 not looked yet, 0 no, 1 yes (app_installed_now)
std::atomic<bool> g_app_staging{false};   // the installed app app_installed_now found is staging (start_staging() is read too)
std::atomic<bool> g_app_opening{false};     // open_app's 25 s wait for the app's channel
std::atomic<bool> g_app_auto_opened{false}; // the Online menu opens the app by itself once per run
std::atomic<int64_t> g_app_opened_at{0};
std::atomic<bool> g_app_stale{false};       // the registered app did not open or answer: Get YouGame for this run
// Off the game's frame: the registry (Windows) or xdg-mime (Linux) answer whether YouGame is installed.
bool app_registered(bool staging) {
  const std::string scheme = staging ? "yougame-staging" : "yougame";
#ifdef _WIN32
  HKEY h = nullptr;
  const std::string user = "Software\\Classes\\" + scheme + "\\shell\\open\\command", root = scheme + "\\shell\\open\\command";
  if (RegOpenKeyExA(HKEY_CURRENT_USER, user.c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS ||
      RegOpenKeyExA(HKEY_CLASSES_ROOT, root.c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS) {
    RegCloseKey(h);
    return true;
  }
  return false;
#elif defined(__APPLE__)
  (void)scheme;
  return false;   // Mac: the Get YouGame row until the LaunchServices lookup lands
#else
  bool yes = false;
  if (FILE* f = popen(("xdg-mime query default x-scheme-handler/" + scheme + " 2>/dev/null").c_str(), "r")) {
    char line[256] = {};
    yes = fgets(line, sizeof line, f) && line[0] && line[0] != '\n';
    pclose(f);
  }
  return yes;
#endif
}
bool app_installed_now() {
  if (g_app_stale.load()) { g_app_installed = 0; return false; }
  bool yes = app_registered(start_staging()), staging = start_staging();
  if (kStagingFallback && !yes && !start_staging() && app_registered(true)) yes = staging = true;   // a tester with only YouGame Staging
  g_app_staging = staging;
  g_app_installed = yes ? 1 : 0;
  return yes;
}
// Opens the installed app, then looks for its channel every second for 25 s ("Opening YouGame...").
// Returns false when it did not ask the app (already opening, opened under 30 s ago, a test run, or the open failed).
bool open_app(const char* why) {
  if (kNoApp) return false;
  const int64_t now = (int64_t)now_ms(), last = g_app_opened_at.load();
  if (last != 0 && now - last < 30000) return false;
  bool idle = false;
  if (!g_app_opening.compare_exchange_strong(idle, true)) return false;   // one opener at a time
  if (!may_open_outside("YouGame app is installed")) { g_app_opening = false; return false; }
  g_app_opened_at = now;
  const bool ok = open_outside(app_link(start_staging() || g_app_staging.load()));
  host::log((std::string("yougame: opening the YouGame app (") + why + "): " + (ok ? "asked" : "could not be opened")).c_str());
  if (!ok) { g_app_stale = true; g_app_installed = 0; g_app_opening = false; return false; }
  std::thread([] {
    for (int i = 0; i < 25 && !g_channel_up.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      start_channel_probe(true);
    }
    for (int i = 0; i < 6 && g_channel_probing.load() && !g_channel_up.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (g_channel_up.load()) host::log("yougame: the YouGame app answered");
    else {   // a stale registration or an app that never came up: the row offers the download page again
      host::log("yougame: the YouGame app did not answer in 25 s: Get YouGame for this run");
      g_app_stale = true;
      g_app_installed = 0;
    }
    g_app_opening = false;
  }).detach();
  return true;
}
}  // namespace

namespace {
// Agent, hidden and test runs never show YouGame's window or open anything (YOUGAME_START_QUIET=1 too).
// MELEE_TEST_YOUGAME_START=1: a test that wants the real window.
bool start_hook_quiet(bool automated) {
  if (host::test_env::getenv("MELEE_TEST_YOUGAME_START")) return false;
  return automated || g_automated_run.load() || host::test_env::agent_copy() || !g_channel_allowed ||
         host::test_env::getenv("MELEE_NO_GC_ADAPTER") != nullptr;
}
const char* start_result_name(int r) {
  switch (r) {
    case YG_START_RUNNING: return "running";
    case YG_START_OPENED: return "opened";
    case YG_START_OFFERED: return "offered, Get YouGame pressed";
    case YG_START_DECLINED: return "offered, Play offline";
    case YG_START_UNAVAILABLE: return "unavailable";
    case YG_START_INSTALLED: return "installed, not running";
    case YG_START_MISSING: return "not on this computer";
    case YG_START_UPDATING: return "YouGame is updating (treated as unavailable; it restarts by itself)";
    default: return "?";
  }
}
// YouGame's start hook (yougame_start.h): YouGame running, nothing; not on this computer, YouGame's own
// small window (Get YouGame / Play offline). Asked on first use: at game start only
// YG_START_ASK_LATER (an installed YouGame starts quietly in the tray, no window ever), and the window
// with YG_START_ASK_ON_USE when the player presses a YouGame row with no YouGame connected. Installed but not running: a verified, recent YouGame starts
// hidden in the tray/menu bar (the header checks identity, signature and the minimum OS version);
// an older one gets the small window saying it needs an update (a standalone game never
// brings up YouGame's client window). Blocks while YouGame's window is up, so it never runs on the
// game's frame.
// The first version of each app that starts hidden in the tray (production and staging number their
// releases apart). NULL = never start that app; its small window says it needs an update.
const char* tray_from(bool staging) {
#ifdef __APPLE__
  // The Windows floor does not imply Mac argv support. Supply the first shipped Mac build.
  if (staging) {
#ifdef MELEE_YOUGAME_MAC_TRAY_FROM
    return MELEE_YOUGAME_MAC_TRAY_FROM;
#endif
  } else {
#ifdef MELEE_YOUGAME_MAC_PROD_TRAY_FROM
    return MELEE_YOUGAME_MAC_PROD_TRAY_FROM;
#else
    return "0.13.25";   // the first Mac YouGame that starts hidden
#endif
  }
  return nullptr;
#else
  if (staging) return "0.13.1407";   // the first Windows YouGame Staging that starts hidden in the tray
#ifdef MELEE_YOUGAME_PROD_TRAY_FROM
  return MELEE_YOUGAME_PROD_TRAY_FROM;
#else
  return "0.13.25";   // the first Windows YouGame that starts hidden in the tray
#endif
#endif
}
int start_once(bool staging, bool quiet, int ask = YG_START_ASK_AT_START) {
  yg_start_options o{};
  o.game = "melee-unlocked";
  o.staging = staging ? 1 : 0;
  o.quiet = quiet ? 1 : 0;
  o.ask = ask;
  o.name = "Melee Unlocked";   // YouGame's window says "Play Melee Unlocked online"; needs stays 0 (offline play works)
  o.tray_from = tray_from(staging);
  return yg_start(&o);
}
// Only looks (quiet): which YouGame is there and whether it runs.
int look_for_app(bool& staging) {
  staging = start_staging();
  int r = start_once(staging, true);
  if (kStagingFallback && !start_staging() && (r == YG_START_MISSING || r == YG_START_INSTALLED)) {
    // YouGame is not running. A running (or updating) YouGame Staging is the tester's app: use it.
    // With no YouGame at all, an installed YouGame Staging is used too.
    const int rs = start_once(true, true);
    if (rs == YG_START_RUNNING || rs == YG_START_UPDATING || (r == YG_START_MISSING && rs == YG_START_INSTALLED)) {
      staging = true;
      r = rs;
    }
  }
  return r;
}
int run_start_hook(const char* why, bool automated, int ask) {
  if (kNoApp) return YG_START_UNAVAILABLE;
  bool staging = start_staging();
  int r = look_for_app(staging);   // first only look
  g_app_staging = staging;
  const bool quiet = start_hook_quiet(automated);
  if ((r == YG_START_MISSING || r == YG_START_INSTALLED) && !quiet) r = start_once(staging, false, ask);
  host::log("yougame: start hook (%s, %s): %s%s", why, staging ? "staging" : "production", start_result_name(r),
            quiet ? " (quiet run)" : "");
  return r;
}
}  // namespace

namespace { void reach_open(); void reach_ask_on_use(); }

void start_hook_at_boot(bool automated) {
  // The app started this game (its rows are open from the start), or a build without the hook.
  if (active() || g_launcher || g_match_bridge || !coming_soon_build()) return;
  // Never a window at launch: an installed YouGame starts quietly in the tray; the ask waits for a press.
  run_start_hook("game start", automated, YG_START_ASK_LATER);
}

void online_menu_opened() {
  // Never waits: this runs on the game's frame. A host found now opens the rows on the next visit.
  start_channel_probe(false);
  if (!coming_soon()) return;
#if defined(_WIN32) || defined(__APPLE__)
  return;   // the start hook ran at game start; the client window is never opened from here
#endif
  // YouGame installed but not running: open it once per run by itself (the first row offers it again).
  std::thread([] {
    if (!app_installed_now() || g_app_auto_opened.load()) return;
    // A running app answers the probe within its hello timeout (6 s): wait it out before opening anything.
    for (int i = 0; i < 18 && g_channel_probing.load() && !g_channel_up.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (g_channel_up.load() || g_app_auto_opened.exchange(true)) return;   // it was running after all
    open_app("Online menu");
  }).detach();
}
void open_get_page() {
  if (kNoApp) return;
  // A fixed https address, opened in the player's own browser; the game downloads and starts nothing.
  const char* kUrl = start_staging() ? "https://staging.yougame.co/desktop" : "https://yougame.co/desktop";
  static std::atomic<int64_t> last{0};
  const int64_t now = (int64_t)now_ms();
  if (now - last.load() < 3000) return;   // one page per press, not one per repeated confirm
  last = now;
  if (!may_open_outside("Get YouGame picked, page")) return;
  const bool ok = open_outside(kUrl);
  host::log(ok ? "yougame: Get YouGame picked: download page opened" : "yougame: Get YouGame picked: download page could not be opened");
}
void first_row_picked() {
  if (kNoApp) return;
  // Ranked, Unranked or Friends before YouGame was connected. Windows and Mac: YouGame's own small window
  // asks (reach_ask_on_use: Get YouGame / Open YouGame / Play offline), once per press, never remembered.
  // Linux, where the hook has no window: the "Play online with YouGame" panel (reach_open). The menu
  // still continues into the row once YouGame answers.
  if (g_channel_up.load()) return;
  // Started by our launcher (extension ticked or not): YouGame's sign-in is the launcher's, and the menu says
  // so; the app opens only from an Open YouGame button, never from a row (owner 11-20).
  if (g_launcher || g_match_bridge) return;
#if defined(_WIN32) || defined(__APPLE__)
  reach_ask_on_use();
#else
  reach_open();
#endif
}
const char* first_row_text() {
  if (g_app_opening.load()) return "Opening YouGame...";
  return g_app_installed.load() == 1 ? "Open YouGame" : "Get YouGame";
}
bool coming_soon() {
#ifdef MELEE_YOUGAME_COMING_SOON
  // Only the GitHub build's own rows wait: a game the YouGame app started (active(), known from the
  // first line of main) plays YouGame as always, rows open and labels not greyed. So does one that
  // reached a running YouGame app over the local channel (yougame_channel.h), while it is connected.
  return coming_soon_build() && !g_channel_up;
#else
  return false;
#endif
}
bool coming_soon_build() {
#ifdef MELEE_YOUGAME_COMING_SOON
  static const bool soon = !host::test_env::getenv("MELEE_TEST_YOUGAME_OPEN");
  return soon && !active();
#else
  return false;
#endif
}
bool available() { return active() || launcher(); }
bool session() { return active() || (launcher() && g_launcher_session.load()); }
bool friends_flow() { return active() || (launcher() && g_channel_up.load()); }
bool extension_waiting() { return kAppExpected && !kNoApp && g_match_bridge && g_channel_allowed.load() && !g_channel_up.load(); }
std::string build_version() {
#ifdef MELEE_PORT_VERSION
  return MELEE_PORT_VERSION;
#else
  return "dev";
#endif
}

std::string behind_version_now(bool* can_restart) {
  if (can_restart) *can_restart = false;
  if (!launcher()) return {};
  try {
    const json hello = bridge({{"type", "hello"}}, 2000);
    if (hello.count("behind") && hello["behind"].is_object()) {
      if (can_restart) *can_restart = hello["behind"].value("restart", true);
      return hello["behind"].value("version", "");
    }
  } catch (const std::exception& e) {
    host::log("yougame: the launcher did not say which release YouGame is on: %s", e.what());
  }
  return {};
}

bool restart_older(const std::string& mode, std::string* error) {
  if (!launcher()) { if (error) *error = "Only the launcher can switch versions."; return false; }
  try {
    bridge({{"type", "muRestartOlder"}, {"mode", mode}}, 5000);
    host::log("yougame: restarting in the release YouGame is on (%s)", mode.c_str());
    return true;
  } catch (const std::exception& e) {
    if (error) *error = e.what();
    return false;
  }
}

bool open_launcher_connect() {
  // Straight to the launcher's bridge (never the app's channel): Connect is the launcher's own sign-in. The bridge
  // answers it with only the launch token, signed in or not.
  if (!(g_launcher || g_match_bridge) || g_bridge_url.empty() || g_bridge_token.empty()) return false;
  std::thread([] {
#ifdef _WIN32
    // The game is the foreground process: let the launcher's sign-in window come to the front.
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    try {
      const json answer = bridge_http({{"type", "muOpenConnect"}}, 5000);
      const json result = answer.is_object() && answer.count("result") && answer["result"].is_object() ? answer["result"] : json::object();
      host::log("yougame: the launcher's Connect %s", !answer.is_object() || !answer.value("ok", false) ? "was refused"
                : result.value("signedIn", false) ? "was not needed (the launcher is signed in)"
                : result.value("opening", false) ? "is opening" : "could not open");
    } catch (const std::exception& e) {
      host::log("yougame: the launcher's Connect: no answer (%s)", channel::log_text(e.what()).c_str());
    }
  }).detach();
  return true;
}

void set_online_mode_arg(int mode) { g_online_mode_arg = mode; }
int take_online_mode_arg() { return g_online_mode_arg.exchange(0); }

static std::atomic<int> g_menu_service{-1};
void set_menu_service(int service) { g_menu_service = service; }
int menu_service() { return g_menu_service.load(); }
void set_launcher_session(bool yougame) {
  if (!launcher()) return;
  if (g_launcher_session.exchange(yougame) != yougame)
    host::log("yougame: %s row chosen", yougame ? "a YouGame" : "a Slippi");
}
uintptr_t pad_mirror_handle() { return g_pad_mirror.load(); }
#ifndef _WIN32
pad_mirror::Attachment pad_mirror_attachment() {
  std::lock_guard<std::mutex> lock(g_pad_mirror_mutex);
  return g_pad_mirror_attachment;
}
#endif
bool stdio_bridge() { return g_stdio; }
bool embedded() { return g_embedded; }
bool embed_toplevel() { return g_embed_toplevel; }
uint32_t app_pid() { return g_app_pid; }
void console_boot_ready() { parity::signals.console_boot_ready(); }

void embed_window_ready(uintptr_t hwnd) {
  if (g_embed_toplevel) {
    parity::signals.start(g_stdio, [](const char* line) { write_line(line); }, true);
    // How long after launch the app got its caps (it waits 30 s on a first run, 11-15).
    static std::once_flag caps_timed;
    if (g_stdio) std::call_once(caps_timed, [] { host::log("startup: toplevel caps sent%s", host::startup_mark().c_str()); });
    if (const int sent = parity::signals.sync_match(-1); sent >= 0) host::log("yougame: match %d", sent);
    // After the caps, once per created window (again if it is ever recreated).
    if (hwnd) parity::signals.emit(embed_toplevel_policy::hwnd_line(hwnd).c_str());
  }
}

void notify_mod_refused(bool explicit_layer, mode_policy::ModRefusal refusal,
                        const std::string& mod, const std::string& version) {
  const auto request = mode_policy::mod_exit_request(active(), stdio_bridge(), explicit_layer, refusal, mod, version);
  if (request.is_null()) return;
  static std::atomic<bool> sent{false};
  if (sent.exchange(true)) return;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  try {
    // WriteFile and the stdout mutex can stall too. Keep them off the exiting thread so the
    // entire notification, including writing the request, has the same bounded grace period.
    std::packaged_task<void()> send([request] {
      try { (void)bridge_stdio(request, 500); } catch (...) { }
    });
    auto done = send.get_future();
    std::thread(std::move(send)).detach();
    done.wait_until(deadline);
  } catch (...) { }  // A failed bridge must never replace the original fatal exit.
}

void set_box_handler(std::function<void(const std::string&)> handler) {
  std::deque<std::string> backlog;
  {
    std::lock_guard<std::mutex> lock(g_io.mutex);
    g_io.box = handler;
    backlog.swap(g_io.box_backlog);
  }
  if (handler) for (auto& line : backlog) handler(line);
}

#ifdef MELEE_LINUX_X11_EMBED
std::vector<std::string> take_box_commands() {
  std::lock_guard<std::mutex> lock(g_io.mutex);
  if (g_io.box_overflow) {
    g_io.box_backlog.clear();
    return {"quit"};  // An overloaded bridge cannot leave an uncontrollable child behind.
  }
  std::vector<std::string> commands;
  commands.reserve(g_io.box_backlog.size());
  while (!g_io.box_backlog.empty()) {
    commands.push_back(std::move(g_io.box_backlog.front()));
    g_io.box_backlog.pop_front();
  }
  return commands;
}
#endif

// ------------------------------------------------------------------ sign in by code (yougame_signin.h)
// The local channel's app is signed out: a YouGame row picked opens the panel (signin_overlay.cpp),
// which asks the app for a code (contract section 8). Asks run on their own threads, never the frame.
namespace {
std::mutex g_signin_mutex;
signin::Model g_signin;                       // g_signin_mutex
std::atomic<bool> g_signin_rehello{false};    // the hello after signedIn is on its way
const char* ask_name(signin::Ask a) { return a == signin::Ask::Code ? "code" : "browser"; }
// signedIn or alreadySignedIn: say hello again (the identity ask), then tell the panel who came back.
void sign_in_rehello() {
  if (g_signin_rehello.exchange(true)) return;
  std::thread([] {
    // The app can push signedIn a moment before its hello carries the player: ask again each second for
    // up to 8 s, so the panel says "Signed in as <name>" and the picked row carries on without a second press.
    Identity id;
    for (int attempt = 0; attempt < 8; ++attempt) {
      if (attempt) Sleep(1000);
      player_arrived();   // the identity is asked again now; an ask in flight runs once more
      for (int i = 0; i < 400 && g_identity_fetching.load(); ++i) Sleep(50);
      { std::lock_guard<std::mutex> lock(g_mutex); id = g_identity; }
      if (id.ok) break;
    }
    host::log("yougame sign-in: hello again: %s", id.ok ? "player" : "no player yet");
    { std::lock_guard<std::mutex> lock(g_signin_mutex); g_signin.rehello_result(id.ok, id.name, now_ms()); }
    g_signin_rehello = false;
  }).detach();
}
// One ask, then whatever its answer asks next (badRequest: the browser). The user code never goes in the log.
void sign_in_send(signin::Ask ask) {
  if (ask == signin::Ask::None) return;
  std::thread([ask] {
    for (signin::Ask next = ask; next != signin::Ask::None;) {
      const signin::Ask asked = next;
      json answer;
#ifdef _WIN32
      // The game is the foreground process: let the browser the app opens come to the front instead of
      // only flashing in the taskbar behind the game (YouGame efdafa604).
      AllowSetForegroundWindow(ASFW_ANY);
#endif
      try { answer = bridge_channel(asked == signin::Ask::Code ? signin::code_request() : signin::browser_request(), 15000); }
      catch (const std::exception&) { answer = json(); }
      if (!answer.is_object()) host::log("yougame sign-in: %s asked: no answer", ask_name(asked));
      else if (signin::true_of(answer, "ok")) {
        const json result = answer.count("result") && answer["result"].is_object() ? answer["result"] : json::object();
        host::log("yougame sign-in: %s asked: ok, where %.16s%s", ask_name(asked), channel::log_text(signin::text_of(result, "where")).c_str(),
                  result.count("code") && result["code"].is_object() ? ", code shown" : "");
      } else {
        const std::string code = ranked::str(answer, "code");
        host::log("yougame sign-in: %s asked: refused %.24s", ask_name(asked), channel::log_text(code.empty() ? "no code" : code).c_str());
      }
      bool rehello = false;
      {
        std::lock_guard<std::mutex> lock(g_signin_mutex);
        try { next = g_signin.answered(asked, answer, now_ms()); }
        catch (const std::exception&) { next = g_signin.answered(asked, json(), now_ms()); }   // read as no answer
        rehello = g_signin.take_rehello();
      }
      if (rehello) sign_in_rehello();
    }
  }).detach();
}
void sign_in_push(const json& push) {
  bool took, rehello;
  {
    std::lock_guard<std::mutex> lock(g_signin_mutex);
    try { took = g_signin.push(push, now_ms()); } catch (const std::exception&) { took = false; }
    rehello = g_signin.take_rehello();
  }
  if (took) host::log("yougame sign-in: push %.16s", channel::log_text(signin::text_of(push, "type")).c_str());
  if (rehello) sign_in_rehello();
}
}  // namespace

// ------------------------------------------------------------------ "Play online with YouGame" (signin::Reach)
// Drawn by the sign-in panel: sign_in_showing, sign_in_view and sign_in_press serve it first.
namespace {
std::mutex g_reach_mutex;
signin::Reach g_reach;                        // g_reach_mutex
std::atomic<bool> g_reach_busy{false};        // a look or an Open in flight
// Looks for YouGame's channel every second; true once it answered.
bool reach_wait_for_channel(int seconds) {
  for (int i = 0; i < seconds && !g_channel_up.load(); ++i) {
    start_channel_probe(true);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  for (int i = 0; i < 6 && g_channel_probing.load() && !g_channel_up.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(500));
  return g_channel_up.load();
}
void reach_open() {
  if (kNoApp) return;
  bool admin = false;
#ifdef _WIN32
  admin = yg__start_raised() != 0;   // YouGame is never started from a game run as administrator
#endif
  {
    std::lock_guard<std::mutex> lock(g_reach_mutex);
    if (g_reach.is_open()) return;
    g_reach.open(admin);
  }
  host::log("yougame: online row picked, no YouGame connected%s", admin ? ": run as administrator" : "");
  if (admin) return;
  bool idle = false;
  if (!g_reach_busy.compare_exchange_strong(idle, true)) return;
  std::thread([] {
    bool staging = start_staging();
    const int r = look_for_app(staging);
    const bool known = r != YG_START_UNAVAILABLE;
    const bool running = r == YG_START_RUNNING || r == YG_START_UPDATING;
    const bool installed = known ? r == YG_START_INSTALLED : app_installed_now();   // Linux: the link type
    if (known) g_app_staging = staging;
    { std::lock_guard<std::mutex> lock(g_reach_mutex); g_reach.looked(running, installed); }
    host::log("yougame: looked for YouGame: %s", running ? "running" : installed ? "installed, not running" : "not on this computer");
    if (running && !reach_wait_for_channel(25)) {
      std::lock_guard<std::mutex> lock(g_reach_mutex);
      g_reach.open_failed(signin::kNoAnswer);
    }
    g_reach_busy = false;
  }).detach();
}
// Windows and Mac, a YouGame row pressed with no YouGame connected: the start hook with ASK_ON_USE, on its
// own thread (it blocks while YouGame's window is up; presses meanwhile are ignored). Running or opened:
// our panel's "Opening YouGame..." until the channel answers. Play offline, or Get YouGame (the page is
// open): nothing more, the player is back in the game, and the next press asks again. The hook unable to
// run (UNAVAILABLE), or a quiet run that showed no window: the panel as before (reach_open), with its own
// administrator screen; otherwise the hook's administrator window says it.
void reach_ask_on_use() {
  if (kNoApp) return;
  bool idle = false;
  if (!g_reach_busy.compare_exchange_strong(idle, true)) return;
  { std::lock_guard<std::mutex> lock(g_reach_mutex); if (g_reach.is_open()) { g_reach_busy = false; return; } }
  g_yg_ask_left = false;   // a new ask: its place is where the player is now
  host::log("yougame: online row picked, no YouGame connected: asking (on use)");
  std::thread([] {
    const int r = run_start_hook("YouGame row pressed", false, YG_START_ASK_ON_USE);
    if (r == YG_START_RUNNING || r == YG_START_OPENED || r == YG_START_UPDATING) {
      if (g_yg_ask_left.load()) {
        // The player left the place that asked (withdraw_ask): YouGame carries on starting, its rows open
        // on the next visit, and no "Opening YouGame..." panel covers the menu they went to.
        host::log("yougame: YouGame row: the place that asked was left; no panel");
        g_reach_busy = false;
        return;
      }
      {
        std::lock_guard<std::mutex> lock(g_reach_mutex);
        g_reach.open(false);
        g_reach.opening();
      }
      g_app_opening = true;
      const bool up = reach_wait_for_channel(25);
      g_app_opening = false;
      host::log(up ? "yougame: YouGame row: YouGame answered" : "yougame: YouGame row: YouGame did not answer");
      std::lock_guard<std::mutex> lock(g_reach_mutex);
      if (up) g_reach.close();
      else g_reach.open_failed(signin::kNoAnswer);
      g_reach_busy = false;
      return;
    }
    g_reach_busy = false;
    if (r == YG_START_DECLINED || r == YG_START_OFFERED) return;   // the player chose; never remembered
    reach_open();   // UNAVAILABLE, or a quiet run that showed nothing: today's panel
  }).detach();
}
// Open YouGame: the start hook's own open (the verified app, hidden in the tray, or its small window),
// the app's link on Linux; then 25 s for its channel.
void reach_open_app() {
  if (kNoApp) return;
  bool idle = false;
  if (!g_reach_busy.compare_exchange_strong(idle, true)) return;
  { std::lock_guard<std::mutex> lock(g_reach_mutex); g_reach.opening(); }
  g_yg_ask_left = false;   // asked from the panel, which holds the controllers: nothing to leave
  std::thread([] {
#if defined(_WIN32) || defined(__APPLE__)
    g_app_opening = true;
    const int r = run_start_hook("Open YouGame pressed", false, YG_START_ASK_ON_USE);
    const bool asked = r == YG_START_OPENED || r == YG_START_RUNNING;
    if (r == YG_START_DECLINED || r == YG_START_OFFERED) {   // its window asked; Play offline or Get YouGame
      g_app_opening = false;
      { std::lock_guard<std::mutex> lock(g_reach_mutex); g_reach.close(); }
      g_reach_busy = false;
      return;
    }
#else
    const bool asked = open_app("Open YouGame pressed") || g_app_opening.load();
#endif
    const bool up = asked && reach_wait_for_channel(25);
#if defined(_WIN32) || defined(__APPLE__)
    g_app_opening = false;
#endif
    host::log(up ? "yougame: Open YouGame: YouGame answered" : "yougame: Open YouGame: YouGame did not answer");
    {
      std::lock_guard<std::mutex> lock(g_reach_mutex);
      if (up) g_reach.close();
      else g_reach.open_failed(signin::kDidntOpen);
    }
    g_reach_busy = false;
  }).detach();
}
void reach_press(signin::Button button) {
  switch (button) {
    case signin::Button::OpenApp: reach_open_app(); break;
    case signin::Button::GetApp: {
      open_get_page();   // https://yougame.co/desktop (staging chosen: staging's page)
      std::lock_guard<std::mutex> lock(g_reach_mutex);
      g_reach.page_opened();
      break;
    }
    case signin::Button::Back:      // B on a pad
    case signin::Button::Offline: {
      std::lock_guard<std::mutex> lock(g_reach_mutex);
      g_reach.close();
      host::log("yougame: Play online with YouGame: Play offline");
      break;
    }
    default: break;
  }
}
bool reach_showing() {
  std::lock_guard<std::mutex> lock(g_reach_mutex);
  return g_reach.is_open();
}
}  // namespace

void withdraw_ask(const char* why) {
  // Only an ask in flight (YouGame's window up, or still being looked for) has anything to withdraw.
  if (!g_reach_busy.load()) return;
  g_yg_ask_left = true;
#ifdef _WIN32
  if (yg_link_start_close()) host::log("yougame: %s: YouGame's window closed", why ? why : "asked from a place since left");
#endif
}

namespace {
// The panel's view, or false when it is closed (closing it once YouGame answered).
bool reach_view(signin::View& out) {
  if (!reach_showing()) return false;
  // A YouGame the player opens by hand is found too: a look every 2 s while the panel is up (never waits).
  static std::atomic<uint64_t> looked_at{0};
  const uint64_t now = now_ms();
  if (now - looked_at.load() > 2000) { looked_at = now; start_channel_probe(true); }
  std::lock_guard<std::mutex> lock(g_reach_mutex);
  if (!g_reach.is_open()) return false;
  if (g_channel_up.load()) { g_reach.close(); return false; }
  out = g_reach.view();
  return true;
}
}  // namespace

// ------------------------------------------------------------------ "Update to play online" (signin::Update)
// A copy started on its own, connected over the local channel, whose search was refused as out of date. The
// app (not this game) decides whether it can update this copy (not_app_copy: one it didn't install, a GitHub
// download). Asks run on their own threads; the sign-in panel draws it (sign_in_showing/view/press).
namespace {
std::mutex g_update_mutex;
signin::Update g_update;                      // g_update_mutex
uint64_t g_update_epoch = 0;                  // g_update_mutex: the search whose refusal opened the panel
uint64_t g_update_closed_epoch = 0;           // g_update_mutex: the player closed the panel for this search
json g_update_deferred;                       // g_update_mutex: a blend's refusal, offered when the blend is over
uint64_t g_update_deferred_epoch = 0;         // g_update_mutex
// Opens the panel for this search's refusal, unless the player already closed it for this search.
void update_open(const json& refusal, uint64_t epoch) {
  if (g_stdio || !g_channel_allowed || !g_channel_up.load()) return;
  if (!update::listed_newer(refusal, build_version())) {
    host::log("yougame update: YouGame lists no newer version than %s (it names %s): no Update offered",
              build_version().c_str(), update::current(refusal).c_str());
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_update_mutex);
    if (epoch == g_update_closed_epoch) return;
    g_update_match_started.store(false);        // only a match from now on closes this panel
    try { g_update.offer(refusal); } catch (const std::exception&) { return; }
    g_update_epoch = epoch;
  }
  host::log("yougame update: out of date (%.24s), Update offered", channel::log_text(ranked::str(refusal, "code")).c_str());
}
const char* update_step_name(signin::Update::Step s) {
  return s == signin::Update::Step::Cancel ? "cancelUpdate" : s == signin::Update::Step::Restart ? "restartGame" : "updateGame";
}
void update_send(signin::Update::Step step) {
  using Step = signin::Update::Step;
  if (step == Step::None) return;
  std::thread([step] {
    for (Step next = step; next != Step::None;) {
      if (next == Step::Exit) {
        host::log("yougame update: restarting into the new build");
        host::request_exit(0);
        return;
      }
      const Step asked = next;
      json answer;
      try { answer = bridge_channel(signin::Update::request(asked), 15000); }
      catch (const std::exception&) { answer = json(); }
      const std::string code = ranked::str(answer, "code");
      host::log("yougame update: %s: %s", update_step_name(asked),
                !answer.is_object() ? "no answer" : signin::true_of(answer, "ok") ? "ok" : channel::log_text(code.empty() ? "refused" : code).c_str());
      std::lock_guard<std::mutex> lock(g_update_mutex);
      try { next = g_update.answered(asked, answer, now_ms()); }
      catch (const std::exception&) { next = g_update.answered(asked, json(), now_ms()); }   // read as no answer
    }
  }).detach();
}
bool update_showing() {
  std::lock_guard<std::mutex> lock(g_update_mutex);
  if (g_update_match_started.exchange(false) && g_update.is_open()) {
    g_update.close();
    g_update_closed_epoch = g_update_epoch;
    host::log("yougame update: panel closed, a match started");
  }
  return g_update.is_open();
}
bool update_view(signin::View& out) {
  signin::Update::Step step;
  {
    std::lock_guard<std::mutex> lock(g_update_mutex);
    if (!g_update.is_open()) return false;
    step = g_update.tick(now_ms());
    out = g_update.view();
  }
  update_send(step);
  return true;
}
void update_press(signin::Button button) {
  signin::Update::Step step;
  bool closed = false;
  {
    std::lock_guard<std::mutex> lock(g_update_mutex);
    step = g_update.press(button, now_ms());
    // Closed by the player: not again for this search (its renewals never offer; a new search may).
    if (!g_update.is_open()) { closed = true; g_update_closed_epoch = g_update_epoch; }
  }
  if (closed) host::log("yougame update: panel closed");
  update_send(step);
}

// Only a copy nothing started, while its local channel is up: the app updates a copy it launched itself.
// A blend's YouGame side failing leaves Slippi's searching (a match may still start): kept for blend_over.
void update_offer(const json& refusal, uint64_t epoch, bool blend) {
  if (g_stdio || !g_channel_allowed || !g_channel_up.load() || !g_search_generation.current(epoch)) return;
  if (blend) {
    std::lock_guard<std::mutex> lock(g_update_mutex);
    g_update_deferred = refusal;
    g_update_deferred_epoch = epoch;
    return;
  }
  update_open(refusal, epoch);
}
void update_push(const json& push) {
  bool took = false;
  signin::Screen screen = signin::Screen::Closed;
  {
    std::lock_guard<std::mutex> lock(g_update_mutex);
    try { took = g_update.push(push); } catch (const std::exception&) { took = false; }
    screen = g_update.screen();
  }
  // Every state but each percent step: one line per change of screen.
  static signin::Screen logged = signin::Screen::Closed;
  if (took && screen != logged) {
    logged = screen;
    host::log("yougame update: push gameUpdate %.16s", channel::log_text(signin::text_of(push, "state")).c_str());
  }
}
}  // namespace

void blend_over(uint64_t epoch) {
  json refusal;
  {
    std::lock_guard<std::mutex> lock(g_update_mutex);
    if (g_update_deferred_epoch != epoch || g_update_deferred.is_null()) return;
    refusal.swap(g_update_deferred);
    g_update_deferred_epoch = 0;
  }
  if (g_search_generation.current(epoch)) update_open(refusal, epoch);
}

namespace {
std::mutex g_home_mutex;
signin::Home g_home;   // g_home_mutex
bool home_showing() { std::lock_guard<std::mutex> lock(g_home_mutex); return g_home.is_open(); }
bool home_view(signin::View& out) {
  std::lock_guard<std::mutex> lock(g_home_mutex);
  if (!g_home.is_open()) return false;
  out = g_home.view();
  return true;
}
// Open on YouGame: the installed app's link (yougame://g/melee-unlocked, the app on the game's page, as
// open_app), or with no app the site's game page in the browser. Test runs open nothing (may_open_outside).
void home_open_on_yougame() {
  static std::atomic<int64_t> last{0};
  const int64_t now = (int64_t)now_ms();
  if (now - last.load() < 3000) return;   // one open per press, not one per repeated confirm
  last = now;
  std::thread([] {
    signin::Home::Opened how = signin::Home::Opened::Failed;
    if (app_installed_now()) {
      // Asked now, still opening, or asked under 30 s ago (open_app's own wait): the app has it.
      const int64_t asked_at = g_app_opened_at.load();
      if (open_app("Open on YouGame pressed") || g_app_opening.load() ||
          (asked_at != 0 && (int64_t)now_ms() - asked_at < 30000 && !g_app_stale.load()))
        how = signin::Home::Opened::App;
    } else if (may_open_outside("Open on YouGame picked, page")) {
      const bool staging = start_staging() || g_app_staging.load();
      if (open_outside(staging ? "https://staging.yougame.co/g/melee-unlocked" : "https://yougame.co/g/melee-unlocked"))
        how = signin::Home::Opened::Site;
    }
    host::log("yougame: home box, Open on YouGame: %s",
              how == signin::Home::Opened::App ? "app asked" : how == signin::Home::Opened::Site ? "game page opened" : "not opened");
    std::lock_guard<std::mutex> lock(g_home_mutex);
    g_home.opened(how);
  }).detach();
}
void home_press(signin::Button button) {
  if (button == signin::Button::Feedback) {
    host::log("yougame: home box, Send feedback");
    std::thread([] {
      json answer;   // null: no answer
      try { answer = bridge_channel({{"type", "openFeedback"}}, 3000); } catch (...) {}
      const json result = answer.is_object() && answer.count("result") && answer["result"].is_object() ? answer["result"] : json::object();
      host::log("yougame: openFeedback %s", !answer.is_object() ? "not answered" : !answer.value("ok", false) ? "refused"
                                            : result.value("opened", false) ? "opened" : "answered, not opened");
      std::lock_guard<std::mutex> lock(g_home_mutex);
      g_home.feedback_answer(answer);
    }).detach();
    return;
  }
  if (button == signin::Button::OpenOnYouGame) { host::log("yougame: home box, Open on YouGame"); home_open_on_yougame(); return; }
  if (button == signin::Button::Back) {
    host::log("yougame: home box, Back");
    std::lock_guard<std::mutex> lock(g_home_mutex);
    g_home.close();
  }
}
}  // namespace
void home_open() {
  std::string name;
  { std::lock_guard<std::mutex> lock(g_mutex); if (g_identity.ok) name = g_identity.name; }
  { std::lock_guard<std::mutex> lock(g_home_mutex); g_home.open(name); }
  host::log("yougame: home box opened");
}
bool sign_in_needed() {
  if (g_stdio || !g_channel_allowed || !g_channel_up.load() || !g_channel_signed_out.load()) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  // Hybrid: the launcher's sign-in wins, so the app's signed-out state holds a row back only once the launcher was
  // asked and named nobody. Then a YouGame row opens the app's Sign in to YouGame panel instead of a dead end.
  if (g_match_bridge)
    return channel::hybrid_offers_app_sign_in(g_identity_asked, g_identity.ok, g_channel_up.load(), g_channel_signed_out.load());
  return !g_identity.ok;
}
void sign_in_open() {
  signin::Ask ask;
  { std::lock_guard<std::mutex> lock(g_signin_mutex); ask = g_signin.open(now_ms()); }
  host::log("yougame sign-in: panel opened%s", ask == signin::Ask::None ? "" : ", asking for a code");
  sign_in_send(ask);
}
bool sign_in_showing() {
  if (reach_showing() || update_showing() || home_showing()) return true;
  std::lock_guard<std::mutex> lock(g_signin_mutex);
  return g_signin.is_open();
}
void sign_in_view(signin::View& out) {
  if (reach_view(out) || update_view(out) || home_view(out)) return;
  Identity id;
  { std::lock_guard<std::mutex> lock(g_mutex); id = g_identity; }
  signin::Ask ask;
  {
    std::lock_guard<std::mutex> lock(g_signin_mutex);
    const uint64_t now = now_ms();
    if (id.ok) g_signin.player_known(id.name, now);
    ask = g_signin.tick(now);
    out = g_signin.view(now);
  }
  sign_in_send(ask);
}
void sign_in_press(signin::Button button) {
  if (reach_showing()) { reach_press(button); return; }
  if (update_showing()) { update_press(button); return; }
  if (home_showing()) { home_press(button); return; }
  signin::Ask ask;
  { std::lock_guard<std::mutex> lock(g_signin_mutex); ask = g_signin.press(button, now_ms()); }
  if (button == signin::Button::Back) host::log("yougame sign-in: Back");
  sign_in_send(ask);
}
namespace {
std::atomic<bool> g_signin_drawn{false};
}  // namespace
void sign_in_drawn(bool drawn) { g_signin_drawn.store(drawn); }
bool sign_in_escape() {
  if (!g_signin_drawn.load() || !sign_in_showing()) return false;
  g_signin_drawn.store(false);   // drawn again by the next frame if another box is still up
  host::log("yougame: Esc, box Back");
  sign_in_press(signin::Button::Back);
  return true;
}

void refresh_identity() {
  if (g_identity_fetching.exchange(true)) return;
  std::thread(run_identity_asks).detach();
}

Identity identity() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_identity_asked) {
      // The page may not have been ready, the player signed in since, or the connect code did not
      // come: ask again, now and then.
      if ((!g_identity.ok || g_identity.code.empty()) && GetTickCount64() - g_identity_at > 5000) refresh_identity();
      return g_identity;
    }
  }
  refresh_identity();
  Identity pending;
  pending.error = "Signing in to YouGame...";
  return pending;
}

Identity wait_identity(int ms) {
  static const uint64_t until = GetTickCount64() + (uint64_t)ms;   // one budget for the whole process
  Identity id = identity();
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_identity_asked) return g_identity;
    }
    if (GetTickCount64() >= until) return id;
    Sleep(20);
  }
}

// Native game's private CPU-bracket command. The site's SDK remains the only request path.
bool tournament_active() { return g_tourney_launched.load(); }
// All SDK traffic runs off the simulation thread. The policy reserves each request
// under the same mutex before starting work, including a leave queued during join.
void tournament_queue_tick() {
  std::string kind;
  {
    std::lock_guard<std::timed_mutex> lock(g_tourney_mutex);
    release_tournament_locked();
    g_tourney_queue.tick(now_ms());
    kind = g_tourney_queue.request(now_ms());
  }
  if (kind.empty()) return;
  std::thread([kind] {
    json state, launch;
    bool failed = false;
    try {
      state = bridge({{"type", kind}}, 5000);
      if (kind == "tourney.state" && state.is_object() && state.count("phase") &&
          (state["phase"] == "ready" || state["phase"] == "playing"))
        launch = bridge({{"type", "hello"}}, 5000);
    }
    catch (const std::exception&) { failed = true; }
    std::lock_guard<std::timed_mutex> lock(g_tourney_mutex);
    release_tournament_locked();
    g_tourney_queue.answer(kind, state, now_ms(), failed);
    const auto offered = g_tourney_queue.assignment(launch);
    if (!offered.is_null()) g_tourney.assignment(offered);
  }).detach();
}
bool tournament_pending() {
  tournament_queue_tick();
  bool refresh = false, pending = false;
  {
    std::lock_guard<std::timed_mutex> lock(g_tourney_mutex);
    release_tournament_locked();
    pending = g_tourney.pending();
    // Only a previously completed tournament polls for another assignment, from the safe menu.
    static auto next = tourney::DeliveryClock::time_point{};
    const auto now = tourney::DeliveryClock::now();
    if (!g_tourney_queue.visible() && g_tourney.awaiting() && now >= next) { refresh = true; next = now + std::chrono::seconds(5); }
  }
  if (refresh) refresh_identity();
  return pending;
}

bool tournament_command(const uint8_t* p, uint32_t n, std::vector<uint8_t>& reply) {
  if (!active() || !p || !n) return true;
  if (n == 1 && (p[0] == MU_TOURNEY_MENU || p[0] == MU_TOURNEY_JOIN || p[0] == MU_TOURNEY_LEAVE)) {
    {
      std::lock_guard<std::timed_mutex> lock(g_tourney_mutex);
      if (p[0] == MU_TOURNEY_JOIN) g_tourney_queue.join();
      if (p[0] == MU_TOURNEY_LEAVE) {
        g_tourney_queue.leave();
        if (!tournament_active() && g_tourney.pending()) g_tourney.release();
      }
      g_tourney_queue.tick(now_ms());
      reply.assign(2 + 128, 0);
      reply[0] = g_tourney_queue.available(); reply[1] = g_tourney_queue.visible();
      const auto text = g_tourney_queue.text();
      std::copy_n(text.begin(), std::min<size_t>(text.size(), 127), reply.begin() + 2);
    }
    tournament_queue_tick();
    return true;
  }
  if (p[0] == MU_TOURNEY_LAUNCH && n == 1) {
    std::lock_guard<std::timed_mutex> lock(g_tourney_mutex);
    // The first hello arrives on the identity worker. Never start/wait for identity here:
    // a pending tournament can also enter from the main menu after that answer arrives.
    if (const auto m = g_tourney.launch()) {
      g_tourney_launched = true;
      host::set_emulation_speed(1.0);
      reply = {1, (uint8_t)(m->stage >> 8), (uint8_t)m->stage, (uint8_t)m->character, (uint8_t)m->level};
    } else if (g_tourney.expired()) host::log("tournament: past deadline");
    return true;
  }
  if (!tournament_active()) return true;
  if (p[0] == MU_TOURNEY_STATUS && n == 1) {
    std::lock_guard<std::timed_mutex> lock(g_tourney_mutex);
    reply = {uint8_t(g_tourney.expired() ? 0 : g_tourney.finished() ? 2 : 1)};
    return true;
  }
  if (p[0] == MU_TOURNEY_RESULT && n == 1 + MU_TOURNEY_RESULT_SIZE) {
    const auto deadline = tourney::delivery_deadline();
    std::unique_lock<std::timed_mutex> lock(g_tourney_mutex, std::defer_lock);
    if (!lock.try_lock_until(deadline)) return true;
    const auto report = g_tourney.finish(p + 1, n - 1);
    if (report.is_null()) return true;
    g_tourney_delivery_until = deadline.time_since_epoch().count();
    // Detached packaged work has no joining future destructor, even if the page stops reading.
    const auto generation = g_tourney_generation;
    g_tourney_delivery = tourney::detached([report, deadline, generation] {
      auto request = [deadline](const json& body) {
        try { (void)tournament_bridge(body, deadline, 1500); host::log("tourney: result answered"); }
        catch (const BridgeNoAnswer&) { return tourney::Reply::NoAnswer; }
        catch (const std::exception&) { host::log("tourney: result refused"); }
        return tourney::Reply::Answer;
      };
      const int attempts = tourney::deliver(report, request, [deadline] {
        try { (void)tournament_bridge({{"type", "hello"}}, deadline, 1000); return true; }
        catch (const std::exception&) { return false; }
      });
      {
        std::unique_lock<std::timed_mutex> lock(g_tourney_mutex, std::defer_lock);
        if (lock.try_lock_until(deadline) && generation == g_tourney_generation) g_tourney.answered();
      }
      host::log("tourney: result delivery finished (%d attempt%s)", attempts, attempts == 1 ? "" : "s");
    });
    return true;
  }
  if (p[0] == MU_TOURNEY_EXIT && n == 1) {
    // Called only after the scene and major mode have unloaded. The process stays open.
    tournament_drain();
    g_tourney_cleanup_pending = true;
    g_tourney_launched = false;
    std::unique_lock<std::timed_mutex> lock(g_tourney_mutex, std::try_to_lock);
    if (lock.owns_lock()) release_tournament_locked();
  }
  return true;
}

void tournament_drain() {
  const tourney::Deadline deadline{tourney::DeliveryClock::duration(g_tourney_delivery_until.load())};
  if (deadline <= tourney::DeliveryClock::now()) return;
  std::shared_future<void> delivery;
  {
    std::unique_lock<std::timed_mutex> lock(g_tourney_mutex, std::defer_lock);
    if (!lock.try_lock_until(deadline)) return;
    delivery = g_tourney_delivery;
  }
  if (delivery.valid()) delivery.wait_until(deadline);
}

void relay_key(const char* key) {
  if (g_stdio) write_line(std::string("yg {\"key\":\"") + key + "\"}");
}

void input_kind(const char* kind) {
  static std::mutex m;
  static std::string last, sent;            // m: the latest kind seen; the latest kind sent
  static uint64_t sent_epoch = 0;           // m
  static bool sending = false;              // m: one sender thread at a time, always the latest value
  if (g_stdio || !g_channel_allowed || !g_channel_up.load() || !kind) return;
  const uint64_t epoch = g_channel_epoch.load();
  {
    std::lock_guard<std::mutex> lock(m);
    if (last == kind && sent == kind && sent_epoch == epoch) return;   // once per change, once per connection
    last = kind;
    if (sending) return;                    // the running sender picks the latest up
    sending = true;
  }
  std::thread([] {
    for (;;) {
      std::string k;
      uint64_t e;
      {
        std::lock_guard<std::mutex> lock(m);
        e = g_channel_epoch.load();
        if (last == sent && sent_epoch == e) { sending = false; return; }
        k = last;
        sent = k;
        sent_epoch = e;
      }
      try { (void)bridge_channel({{"type", "inputKind"}, {"kind", k}}, 2000); } catch (...) {}
    }
  }).detach();
}
bool channel_menu_key(const char* kind) {
  if (g_stdio || !g_channel_allowed) return false;
  const bool pad = kind && std::string(kind) != "keys";
  // Start with Down held stays the game's (a crouch pause) when no YouGame is there to ask.
  if (pad && !g_channel_up.load()) return false;
  static std::atomic<int64_t> last{0};
  const int64_t now = (int64_t)now_ms();
  if (now - last.load() < 300) return true;   // the channel takes one menuButton every 300 ms
  last = now;
  // Pressed again with one of the game's YouGame boxes up: closes it (Back).
  if (sign_in_showing()) { sign_in_press(signin::Button::Back); return true; }
#ifdef _WIN32
  // Pressed again with YouGame's own small window up (the ask below): closes it, as it does the game's boxes.
  if (!g_channel_up.load() && yg_link_start_close()) { host::log("yougame: Shift+Tab: YouGame's window closed"); return true; }
#endif
  // No YouGame connected (Shift+Tab only): the same ask as a YouGame row picked first.
  if (!g_channel_up.load()) { host::log("yougame: Shift+Tab, no YouGame connected"); first_row_picked(); return true; }
  if (sign_in_needed()) { host::log("yougame: Shift+Tab, signed out"); sign_in_open(); return true; }
  const std::string device = kind ? kind : "keys";
  input_kind(device.c_str());
  std::thread([device] {
    bool opened = false, fallback = false;
    std::string code = "no answer";
    try {
      const json j = bridge_channel({{"type", "menuButton"}, {"kind", device}}, 2000);
      const json r = j.count("result") && j["result"].is_object() ? j["result"] : j;
      if (j.value("ok", false)) {
        opened = r.value("opened", false);
        code = opened ? "opened" : "not opened";
        fallback = !opened;   // an app with no overlay of its own (the small start app)
      } else {
        code = j.value("code", std::string("refused"));
        // busy (300 ms / rate limit), notYet (still connecting) and closed mean try later: nothing
        // opens. Anything else is an app that doesn't know menuButton: the game's box.
        fallback = code != "busy" && code != "notYet" && code != "closed";
      }
    } catch (...) {}
    host::log("yougame: Shift+Tab, app menu: %s", code.c_str());
    if (fallback) { if (sign_in_needed()) sign_in_open(); else home_open(); }
  }).detach();
  return true;
}

void pause_event(bool pausable, bool paused, const char* reason) {
  if (!g_stdio) return;
  json event = {{"type", "pause"}, {"pausable", pausable}, {"paused", paused}};
  if (reason) event["reason"] = reason;
  write_line("yg " + json{{"event", event}}.dump());
}

bool update_safe_point(bool idle_scene, bool online_busy, bool ranked_set, bool online_screen) {
  // Opening Online asks the app to check this game for a newer build now (its timer runs every ~30 min), so
  // an update lands before matching, not as a lobby error (11-10). Once per opening; the app debounces.
  static bool was_online_screen = false;
  if (online_screen && !was_online_screen && g_stdio) write_line(update::kCheckLine);
  was_online_screen = online_screen;
  if (g_update_notice.visible()) return true;
  if (g_update_notice.pending().empty()) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  const bool retiring = g_retired.valid() &&
      g_retired.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
  const update::SafePoint state{idle_scene,
      online_busy || g_update_searches.load() != 0,
      bool(g_room) || g_slippi_match || retiring, ranked_set,
      g_room && g_room->friends, online_screen};
  if (!g_update_notice.begin(state)) return false;
  host::log("update: safe point, handing back to YouGame");
  return true;
}
bool update_handoff_visible() { return g_update_notice.visible(); }
void update_notice_presented() {
  const auto line = g_update_notice.presented(now_ms());
  if (line.empty()) return;
  write_line(line); // host::log alone goes to stderr in app stdio mode, NOT the app's stdout.
  host::log("%s", line.c_str());
  host::set_replay_viewing(false); // update acceptance always exits 0, including a finished replay
  host::request_user_exit();
}

void blend_slippi_stopped(uint64_t epoch, const std::string& reason) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_search_generation.current(epoch)) return;
  g_blend_reason = blend::slippi_stop_reason(reason);
  if (g_status == blend::kSearchBoth || g_status == blend::kSearchYouGameOnly) {
    g_status = blend::kSearchYouGameOnly;
    g_status_since = now_ms();
  }
}

std::string status_line() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status = friends::expire_joined(g_status, g_status_since, now_ms());
  return g_update_notice.available() ? update::kMessage : g_status;
}

// One row over the character select: printable text only, "…" as "..." (the overlay font has no
// ellipsis), at most kPausedMax bytes cut on a UTF-8 boundary. An empty or missing paused clears it.
std::string paused_line(const std::string& raw) {
  constexpr size_t kPausedMax = 72;
  std::string out;
  for (size_t i = 0; i < raw.size(); ++i) {
    const unsigned char c = (unsigned char)raw[i];
    if (c == 0xE2 && i + 2 < raw.size() && (unsigned char)raw[i + 1] == 0x80 && (unsigned char)raw[i + 2] == 0xA6) {
      out += "..."; i += 2;
    } else if (c >= 0x20 && c != 0x7F) out += (char)c;
  }
  if (out.size() > kPausedMax) {
    size_t cut = kPausedMax;
    while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80) --cut;
    out.resize(cut);
  }
  return out;
}

void set_search_paused(const std::string& raw) {
  const auto line = paused_line(raw);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (search_stopped() || line == g_search_paused) return;
  host::log("yougame: search paused line: %s", line.empty() ? "(none)" : line.c_str());
  g_search_paused = line;
}

std::string search_paused_line() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_search_paused.empty()) return g_search_paused;
  // The firewall note takes the search line's place while the run's first own UDP port is new.
  return g_firewall_note_until && now_ms() < g_firewall_note_until && !search_stopped()
      ? std::string(search_policy::kFirewallNote) : std::string();
}

std::string slippi_link_status(const std::string& status) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (status == blend::kSearchYouGameOnly && !g_blend_reason.empty()) return status + "\n" + g_blend_reason;
  return status == slippi_linking::kSearching
      ? slippi_linking::presentation.line(slippi_linking::Screen::Search) : status;
}

nlohmann::json request_slippi_link(const nlohmann::json& request, int timeout, const std::function<bool()>& cancelled) {
  if (slippi::agent_lock::blocked(slippi::agent_lock::Operation::AccountLink)) return {{"ok", false}};
  return bridge(request, timeout, cancelled);
}

std::string pick_friend_room(const std::function<bool()>& cancelled, const std::string& held
#ifdef MELEE_DOUBLES
                             , bool doubles, std::string* invited
#endif
                             ) {
  host::log("friends: picker requested");
#ifdef MELEE_DOUBLES
  auto picker_request=friends::picker(held);
  if(doubles) {
    picker_request["mode"]="teams"; picker_request["players"]=4;
    picker_request["mu_doubles"]=1; picker_request["teamsLobby"]=1;
  }
#endif
  try {
    auto code = held.empty() ? friends::resolve_room({},
        [&](const json& q, int ms) { return bridge(q, ms, cancelled); }, cancelled)
#ifdef MELEE_DOUBLES
        : [&] {
            const auto picked=bridge(picker_request,friends::kPickerTimeout,cancelled);
            if (invited) *invited=doubles::invited(picked);
            return friends::picker_room(picked);
          }();
#else
        : friends::picker_room(bridge(friends::picker(held),friends::kPickerTimeout,cancelled));
#endif
    if (cancelled() || (!held.empty() && code != held)) throw std::runtime_error("Cancelled");
    host::log("friends: invite sent");
    return code;
  } catch (const std::exception& e) {
    const auto full = cancelled() ? std::string() : friends::invite_room_full(g_bridge_refusal, e.what());
    if (std::string(e.what()) == friends::kVersion) log_friend_version();
    else if (cancelled() || std::string(e.what()) == "Cancelled") host::log("friends: cancelled");
    else if (!full.empty()) {
      host::log("friends: invite failed: room full");
      throw friends::RoomFull(full);
    }
    // The page's refusal names the failed check (a held room it no longer recognises, a seat mismatch).
    else host::log("friends: invite failed: picker failed (%s room): %.120s", held.empty() ? "new" : "held", e.what());
    throw;
  }
}

void lobby_invite(const std::string& room, const std::function<bool()>& cancelled) {
  host::log("friends lobby: invite picker requested");
  try {
    const json answer = bridge(friends::lobby_invite(room), friends::kPickerTimeout, cancelled);
    const std::string code = answer.is_object() && answer.count("code") ? friends::picker_room(answer) : std::string();
    if (cancelled() || (!code.empty() && code != room)) throw std::runtime_error("Cancelled");
    host::log("friends lobby: invites sent");
  } catch (const std::exception& e) {
    const auto full = cancelled() ? std::string() : friends::invite_room_full(g_bridge_refusal, e.what());
    if (cancelled() || std::string(e.what()) == "Cancelled") host::log("friends lobby: invite cancelled");
    else if (!full.empty()) { host::log("friends lobby: invite failed: room full"); throw friends::RoomFull(full); }
    else host::log("friends lobby: invite failed: %.120s", e.what());
    throw;
  }
}

void doubles_invite(const std::string& room, const std::function<bool()>& cancelled) {
  host::log("doubles: partner invite requested");
  try {
    const json answer = bridge(friends::doubles_invite(room), friends::kPickerTimeout, cancelled);
    const std::string code = answer.is_object() && answer.count("code") ? friends::picker_room(answer) : std::string();
    if (cancelled() || (!code.empty() && code != room)) throw std::runtime_error("Cancelled");
    host::log("doubles: partner invite sent");
  } catch (const std::exception& e) {
    const auto full = cancelled() ? std::string() : friends::invite_room_full(g_bridge_refusal, e.what());
    if (cancelled() || std::string(e.what()) == "Cancelled") host::log("doubles: partner invite cancelled");
    else if (!full.empty()) { host::log("doubles: partner invite failed: room full"); throw friends::RoomFull(full); }
    else host::log("doubles: partner invite failed: %.120s", e.what());
    throw;
  }
}

void with_native_udp(const std::function<void()>& create) {
  // g_mutex: no transport selection (a search starting, an identity refresh) lands in between.
  std::lock_guard<std::mutex> lock(g_mutex);
  const int was = yg_enet_selected();
  yg_enet_select(0);
  try {
    create();
  } catch (...) {
    yg_enet_select(was);
    throw;
  }
  yg_enet_select(was);
}

void slippi_link(bool linked, const std::string& tag) {
  slippi_linking::presentation.linked = linked;
  const std::string line = blend::slippi_link_line(linked, tag);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    static std::string sent;   // the last line the app has: only a change goes out again
    if (line == sent) return;
    sent = line;
  }
  host::log("yougame: Slippi %s", linked ? "linked" : "not linked");
  if (g_stdio) write_line(line);
}

bool lobby_parked() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_parked_lobby != nullptr;
}

uint64_t begin_search(bool keep_lobby) {
  const auto epoch = close_room(keep_lobby ? Leave::Next : Leave::Quit);
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_search_generation.current(epoch)) { g_blend_reason.clear(); g_search_paused.clear(); }
  return epoch;
}

bool guard_search(uint64_t epoch, const std::function<void()>& commit) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_search_generation.current(epoch)) return false;
  commit();
  return true;
}

bool prepare_slippi_match(const std::function<bool()>& cancelled, uint64_t epoch) {
  // A blend reserved/retired its room before launching the helper. Do not close a newer room.
  const bool blend = epoch != 0;
  if (!epoch) epoch = close_room(Leave::Quit);
  std::shared_future<void> retired;
  { std::lock_guard<std::mutex> lock(g_mutex); retired = g_retired; }
  // A blend has no published YouGame room; older room teardown owns only its old links.
  // Its completion must not consume the Slippi opponent's connection window either.
  while (!blend && retired.valid() && retired.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready)
    if (cancelled()) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_search_generation.current(epoch) || cancelled()) return false;
  g_own_udp_match = false;
  g_relay_own = false;
  select_match_transport(true);
  yg_enet_enable(0);
#ifdef YOUGAME_AGENT_TEST_HOOK
  g_pending_input = false;
  g_pending_input_roster = {};
#endif
  return true;
}

Match find_match(const Search& search, const std::function<bool()>& cancel_requested) {
  // Reserve before blocking, or use the generation reserved before the helper was launched.
  if (cancel_requested()) { Match out; out.error = "Cancelled"; return out; }
  const uint64_t epoch = search.epoch ? search.epoch : begin_search(search.friends_lobby);
  const auto cancelled = [&] { return !g_search_generation.current(epoch) || cancel_requested(); };
  struct SearchScope {
    search_policy::BusyRetry retry;
    explicit SearchScope(const std::function<bool()>& stop) { ++g_update_searches; g_search_stopped = stop; g_ticket_retry = &retry; }
    ~SearchScope() { g_search_stopped = {}; g_ticket_retry = nullptr; --g_update_searches; }
  } scope(cancelled);
  if (cancelled()) return fail("Cancelled");
  // The Friends lobby runs in one mode (lobby::kSearchMode, Direct) with no connect code or invite
  // room: anything else is refused here, not half-run as another kind of search.
  if (search.friends_lobby && (search.mode != lobby::kSearchMode || !search.direct_code.empty() || !search.friend_room.empty())) {
    host::log("friends lobby: search refused (mode %d, code %s, invite %s)", (int)search.mode,
              search.direct_code.empty() ? "none" : "set", search.friend_room.empty() ? "none" : "set");
    return fail(lobby::kSearchRefused);
  }
  // An arena game: 2 or 4 different fighters, found in the arena's own room, never a code, invite or lobby.
  const bool arena = !search.arena_fighters.empty();
  if (arena) {
    const auto& f = search.arena_fighters;
    const bool distinct = std::set<std::string>(f.begin(), f.end()).size() == f.size() &&
                          std::none_of(f.begin(), f.end(), [](const std::string& id) { return id.empty(); });
    if ((f.size() != 2 && f.size() != 4) || !distinct || search.friends_lobby || !search.friend_room.empty() ||
        !search.direct_code.empty() || search.mode == 0) {
      host::log("arena: search refused (%zu fighters, mode %d)", f.size(), (int)search.mode);
      return fail("The arena could not start this game");
    }
  }
  try {
  struct FriendSearch {
    explicit FriendSearch(bool active) {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (search_stopped()) return;
      friends::events.take_version();  // a notice from an earlier search cannot refuse this invite
      g_friend_search = active; g_friend_failed = false;
    }
    ~FriendSearch() { g_friend_search = false; }
  } friend_search(!search.friend_room.empty());
  // Blended Unranked: the queue reports to the coordinator and asks it before taking a room.
  const bool blend = static_cast<bool>(search.claim);
  struct BlendSearch {
    explicit BlendSearch(bool active) { g_blend_search = active; }
    ~BlendSearch() { g_blend_search = false; }
  } blend_search(blend);
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  const bool squad_row = !search.friends_lobby && squad::selected(search.mode, search.squad, !search.friend_room.empty());
  const std::string compat = compatibility(search.mode, squad_row, search.squad_budget);
  if (squad_row && compat.empty()) {
    host::log("squad: no compatibility key (budget %zu chars)", search.squad_budget.size());
    return fail("Squad Strike could not start. Restart the game and try again.");
  }
#else
  const std::string compat = compatibility(search.mode);
#endif
  if (cancelled()) return fail("Cancelled");
  if (compat.empty()) return fail("Could not verify this mod's content. Restart the game before playing online.");
  // The Doubles home (G2): the Friends lobby's runtime on its 2-seat profile (the lobby's Control says
  // which). Its search runs in the home until matchmaking places it, then joins the match room in a
  // nested find_match (doubles_room) while the home stays parked.
  const bool doubles_room = !search.doubles_match.room.empty();
  const lobby::Profile lobby_profile = doubles_room ? lobby::Profile::Doubles
                                       : search.lobby_control ? search.lobby_control->profile() : lobby::Profile::Friends4;
  const bool doubles_home = search.friends_lobby && lobby_profile == lobby::Profile::Doubles;
  // The 4-player Friends lobby's room is keyed by its own key (friends-lobby-1, yougame_lobby_setup.h); the
  // Doubles home, its queue and its match by doubles-lobby-1.
  const std::string lobby_key = search.friends_lobby || doubles_room ? lobby_room_key(lobby_profile) : std::string();
  if ((search.friends_lobby || doubles_room) && lobby_key.empty())
    return fail("Could not verify this mod's content. Restart the game before playing online.");
#ifdef YOUGAME_AGENT_TEST_HOOK
  struct PendingInput {
    uint64_t epoch;
    void clear() const {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_search_generation.current(epoch) || search_stopped()) return;
      g_pending_input = false;
      g_pending_input_roster = {};
    }
    void publish(const Room& room, const json& participants) const {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_search_generation.current(epoch) || search_stopped()) return;
      g_pending_input_roster = {room.mode, room.ranked, participants, room.me};
      g_pending_input = true;
    }
    ~PendingInput() { clear(); }
  } pending_input{epoch};
#endif
  std::shared_future<void> retired;
  { std::lock_guard<std::mutex> lock(g_mutex); retired = g_retired; }
  while (retired.valid() && retired.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready)
    if (cancelled()) return fail("Cancelled");
  if (cancelled()) return fail("Cancelled");
  Identity me;
  { std::lock_guard<std::mutex> lock(g_mutex); me = g_identity; }
  if (!me.ok) me = fetch_identity(cancelled);
  if (cancelled()) return fail("Cancelled");
  if (!me.ok) return fail(search.mode == 0 && me.error == "Sign in to YouGame to play online"
                            ? "Sign in to YouGame to play Ranked." : me.error);
  { std::lock_guard<std::mutex> lock(g_mutex);
    if (cancelled()) { Match out; out.error = "Cancelled"; return out; }
    g_own_udp_match = false;
    g_relay_own = false;
    select_match_transport(false);
  }
#ifndef _WIN32
  if (g_broker && !g_direct) return fail("YouGame broker requires direct links v1 (1200-byte datagrams)");
#endif
#ifdef MELEE_DOUBLES
  // The Doubles home found this match room (yougame_doubles_runtime.inc): join it as a 4-seat Teams room
  // with the home's own admission profile, and play it on the 4-peer rollback client.
  const bool doubles_match = doubles_teams::kFourPeer &&
      doubles_teams::match_search(search.mode, !search.friend_room.empty());
  doubles::Consent consent;
  doubles_teams::Setup setup;          // the match Rooms froze at the host's begin (the start frame)
  uint64_t begin_sent = 0;             // the host's last begin
  std::string last_refusal;            // the room's last refusal, logged once (begin is resent every 2 s)
  struct DoublesSearch {
    explicit DoublesSearch(const std::string& room) { g_doubles_search_room = room; }
    ~DoublesSearch() { g_doubles_search_room.clear(); }
  } doubles_search(doubles_match ? search.friend_room : std::string());
#else
  const bool doubles_match = false;
  const doubles_teams::Setup setup;    // never known without Doubles
#endif
  const bool team_code = !arena && ((search.mode == 3 && !search.direct_code.empty()) || doubles_match);
  std::string team_room = doubles_match ? search.friend_room : std::string();
  if (team_code && !doubles_match && !friends::team_room_code(search.direct_code, &team_room)) return fail(friends::kTeamCodeError);
  // The 4-player Friends lobby (yougame_lobby.h): a held room of up to four, picks and Start in the room.
  const bool lobby_route = search.friends_lobby;   // never a team code or invite: refused at the top
  // A room whose match is a lobby setup: the Friends lobby's, or the Doubles home's placed match.
  const bool lobby_match = lobby_route || doubles_room;
  struct LobbySearch {
    explicit LobbySearch(bool active) { g_lobby_search = active; }
    ~LobbySearch() { g_lobby_search = false; }
  } lobby_search(lobby_match);
  const bool friends_mode = !team_code && !lobby_match && (!search.friend_room.empty());
  const bool direct = search.mode == 2 && !lobby_match && !arena;
  const int size = arena ? (int)search.arena_fighters.size() : lobby_match || search.mode == 3 ? 4 : 2;
  const json lobby = {{"version", 1}, {"minPlayers", size}, {"maxLocalPlayers", 1}, {"compatibility", compat}};
  json request = {{"type", "mpTicket"}, {"players", size}, {"mode", size == 4 ? "teams" : "singles"}, {"lobby", lobby}, {"ranked", search.mode == 0}};
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  if (squad::selected(search.mode, search.squad, !search.friend_room.empty()))
    request["mode"] = squad::ticket_mode(search.mode, true, "singles");
#endif
  if (search.mode == 0) {
    ranked::set_contract(request);
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_rank_opponent.clear(); g_rank_score.clear(); g_rank_opponent_rank = {};
      g_rank_results = {}; g_rank_result_visible = false;
      g_rank_read.recovery_at = 0;
      if (!g_rank_read.enabled) g_rank_read.enter(); // the agent harness bypasses the CSS
    }
    ranked_header();
    // The CSS may already have the read in flight. The ticket must follow it, including when
    // a hook enters Ranked without visiting the menu/CSS first.
    for (;;) {
      { std::lock_guard<std::mutex> lock(g_mutex); if (!g_rank_read.in_flight) break; }
      if (cancelled()) return fail("Cancelled");
      Sleep(20);
    }
  }
  // Unranked keeps its two players together for the next game, as Slippi's unranked does.
  if (search.mode == 1) request["afterMatch"] = "stay";
  if (!friends_mode && direct) request["direct"] = search.direct_code;
  if (lobby_route) request = json::object();   // set with the ticket below (lobby_hold or lobby_ticket)
  // The queue's ticket admits this player to the match room; a renewal there asks for a match-admission
  // ticket (Teams, teamsLobby 1, 4 players, casual, the same key; no room, party or partySearch).
  if (doubles_room) request = friends::doubles_match_ticket(lobby_key);
#ifdef MELEE_DOUBLES
  if (doubles_match) request = doubles::ticket(compat, team_room, 4);
  else
#endif
  if (team_code) request = friends::team_ticket(compat, team_room);

  // Clear failed/cancelled setup without clearing a newer search's room.
  struct FriendsSetup {
    uint64_t epoch;
    bool keep = false;
    ~FriendsSetup() {
      if (keep) return;
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_search_generation.current(epoch) && !search_stopped()) friends::events.current_room({});
    }
  } friends_setup{epoch};
  // The base listing's Unranked says which pools it searches (yougame_blend.h kSearch*).
  const std::string searching = search.status.empty() ? std::string("searching") : search.status;
  bool repeated_link_failure;
  { std::lock_guard<std::mutex> lock(g_mutex);
    // Only the automatic re-search right after a failure, and never Direct (it never looks for another).
    repeated_link_failure = !direct && g_direct_link_failures.repeated() &&
        now_ms() - g_last_link_failure < search_policy::kRetryLineMs; }
  set_status(doubles_home ? std::string(lobby::kDoublesWaiting) : lobby_match ? std::string(lobby::kWaiting) : team_code ? friends::kTeamWaiting : friends_mode ? friends::kWaiting :
             repeated_link_failure ? search_policy::kLinkRetry : searching);
  // The lobby's next match: the room parked after the last one (close_room(Leave::Next)), with its socket,
  // lobby and links. Its pump stops here with the links handed on, and anything it still owed (the last
  // match's finish) goes out first. A parked room whose socket closed asks for the same room again.
  std::unique_ptr<Room> parked;
  std::string lobby_room = search.lobby_room;
  lobby::Session::CallOff carried_call_off;   // a closed parked room's setup that never began (Options::call_off_id)
  if (doubles_home && search.lobby_control) search.lobby_control->set_matched(false);   // back in the home
  if (lobby_route) {
    std::unique_ptr<Room> other;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_parked_lobby && (lobby_room.empty() || g_parked_lobby->code == lobby_room)) parked = std::move(g_parked_lobby);
      else other = std::move(g_parked_lobby);
    }
    if (other) std::thread([old = std::move(other)]() mutable { shutdown_room(std::move(old)); }).detach();
    if (parked) {
      parked->keep_links = true;
      parked->stop = true;
      if (parked->pump.joinable()) parked->pump.join();
      parked->stop = false;
      parked->keep_links = false;
      std::vector<json> owed;
      { std::lock_guard<std::mutex> lock(g_mutex); if (!parked->socket->closed()) owed = transport::due_messages(*parked, now_ms()); }
      for (const auto& f : owed) parked->socket->send(f);
      if (parked->socket->closed() || !parked->lobby_session) {
        host::log("friends lobby: the room's connection closed between matches; joining it again");
        // A setup that never began is still the room's match: the new session calls it off (as host).
        if (parked->lobby_session) carried_call_off = parked->lobby_session->call_off();
        if (!carried_call_off.id.empty()) host::log("friends lobby: the match that never began is called off after the re-join");
        lobby_room = parked->code;
        std::thread([old = std::move(parked)]() mutable { shutdown_room(std::move(old)); }).detach();
      } else host::log("friends lobby: next match in the same room");
    }
  }
  // An arena game takes the arena's room as it is (a game's room on its way back first, briefly): its pump
  // stops here and the search reads the room itself until the game is found (the arena's own messages still go
  // to the arena: arena_intercept). Whatever happens, the room goes back between games (ArenaKeep, below).
  if (arena) {
    for (const uint64_t until = now_ms() + 5000;;) {
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_arena) { parked = std::move(g_arena); break; }
        if (!g_arena_parking || !g_arena_open) break;
      }
      if (cancelled() || now_ms() >= until) break;
      Sleep(20);
    }
    if (!parked) return fail(cancelled() ? std::string("Cancelled") : std::string("The arena's room is closed"));
    parked->stop = true;
    if (parked->pump.joinable()) parked->pump.join();
    parked->stop = false;
  }
  struct ArenaKeep {
    std::unique_ptr<Room>* room = nullptr;
    ~ArenaKeep() {
      if (!room || !*room || !(*room)->arena) return;
      std::unique_ptr<Room> back = std::move(*room);
      back->size = friends::kArenaSeats; back->mode = -2;
      // A search that failed after opening links: close them here, or the next game's links hit the cap of three.
      transport::close_links(back->links, [](const json& q, int ms) { return bridge(q, ms); });
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_arena_open && !g_arena && back->socket && !back->socket->closed()) {
        Room* raw = back.get();
        raw->pump = std::thread(pump_room, raw);
        g_arena = std::move(back);
      } else std::thread([old = std::move(back)]() mutable { shutdown_room(std::move(old)); }).detach();
    }
  } arena_keep;
  if (arena) arena_keep.room = &parked;
  // Every ticket and room hold this search asks for says whether this copy can share a match with a links:false
  // seat (transport::with_links_flag): decide_transport below is that path. Other requests go out as they are.
  const auto ticket_bridge = [&](json q, int ms) { return bridge(transport::with_links_flag(q), ms, cancelled); };
  json ticket;
  g_bridge_refusal.clear();   // read in the catch below: only this ticket's refusal counts
  try {
    if (arena) {
      ticket = {{"url", parked->url}, {"ticket", parked->ticket}, {"room", parked->code}};
      request = parked->ticket_request;
      if (cancelled()) throw std::runtime_error("Cancelled");
    } else if (lobby_route && parked) {
      ticket = {{"url", parked->url}, {"ticket", parked->ticket}, {"room", parked->code}};
      request = parked->ticket_request;
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_search_generation.current(epoch) || cancelled()) throw std::runtime_error("Cancelled");
      friends::events.current_room(parked->code);
    } else if (lobby_route) {
      // The host holds a room (the app picks its 12-character code); a guest joins its invite's code.
      // Renewals ask for the same room again, never a new hold.
      // The Doubles home holds a 2-seat teams home (doubles_hold) and its partner joins by code.
      if (lobby_room.empty()) {
        ticket = ticket_bridge(doubles_home ? friends::doubles_hold(lobby_key) : friends::lobby_hold(lobby_key), 30000);
        const std::string held = ticket.is_object() && ticket.count("room") && ticket["room"].is_string()
                                     ? ticket["room"].get<std::string>() : std::string();
        if (held.empty()) throw std::runtime_error("YouGame did not return a Friends lobby room");
        request = doubles_home ? friends::doubles_home_ticket(lobby_key, held) : friends::lobby_ticket(lobby_key, held);
      } else {
        request = doubles_home ? friends::doubles_home_ticket(lobby_key, lobby_room) : friends::lobby_ticket(lobby_key, lobby_room);
        ticket = ticket_bridge(request, 30000);
      }
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_search_generation.current(epoch) || cancelled()) throw std::runtime_error("Cancelled");
      friends::events.current_room(friends::room(request));
    } else if (doubles_room) {
      ticket = {{"url", search.doubles_match.url}, {"ticket", search.doubles_match.ticket}, {"room", search.doubles_match.room}};
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_search_generation.current(epoch) || cancelled()) throw std::runtime_error("Cancelled");
    } else if (team_code) {
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_search_generation.current(epoch) || cancelled()) throw std::runtime_error("Cancelled");
        friends::events.current_room(team_room);
      }
      ticket = ticket_bridge(request, 30000);
    } else if (friends_mode) {
      ticket = friends::request_ticket(compat, search.friend_room, request,
          [&](const json& q, int ms) {
            if (q.value("type", "") == "mpFriend") host::log("friends: picker requested");
            auto answer = ticket_bridge(q, ms);
            if (q.value("type", "") == "mpFriend") host::log("friends: invite sent");
            return answer;
          }, cancelled,
          [&](const std::string& code) {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_search_generation.current(epoch) || cancelled()) throw std::runtime_error("Cancelled");
            friends::events.current_room(code);
          });
    } else {
      // The blend cancels this side when Slippi's is first: the ticket wait must not hold it up.
      ticket = ticket_bridge(request, 30000);
    }
  } catch (const update::Required&) {
    // update_required: the answer's own words as the app gave them (it may say it is getting the update), else ours.
    return fail(g_bridge_update_text.empty() ? std::string(update::kMessage) : g_bridge_update_text, false, true);
  } catch (const std::exception& e) {
    // Any refusal of the ticket (ok:false: build_mismatch, friend_update_required, teams_home, teams_lobby_off, ...)
    // or no answer in time shows its own words and the game goes back to the menu. A ticket error never tries the
    // room as another kind (1v1, Friends lobby, Doubles home, Teams): only the room's own kind refusal over the
    // socket may, below, within the room's tries.
    if (!cancelled()) host::log("yougame: ticket refused (%s)", g_bridge_refusal.empty() ? "no code" : g_bridge_refusal.c_str());
    // The player's own search was refused as out of date: offer the app's update (a blend: once it is over).
    if (!cancelled() && !g_ticket_out_of_date.is_null()) update_offer(g_ticket_out_of_date, epoch, blend);
    return fail(e.what());
  }
  if (search.mode == 0 && !ranked::set_contract_matches(ticket))
    return fail("YouGame did not return the Ranked best-of-three rules. Please try again later.");
  // A ranked ticket carries the player's standing on this game's ladder (the ticket route's
  // `rating` and ranks.ts `rank`): the Ranked header shows it from the first search on.
  if (!cancelled() && search.mode == 0 && ticket.is_object() && ticket.count("rank")) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (cancelled()) { Match out; out.error = "Cancelled"; return out; }
    if (g_rank.standing(ticket["rank"], ticket.count("rating") ? ticket["rating"] : json()))
      host::log("yougame: ranked standing: %s", g_rank.header().c_str());
  }
  const std::string url = ticket.value("url", "");
  std::string pass = ticket.value("ticket", "");
  uint64_t pass_at = now_ms();   // a ticket lives 5 minutes at the rooms edge (TICKET_TTL_MS)
  std::string room_code = ticket.count("room") && ticket["room"].is_string() ? ticket["room"].get<std::string>() : "";
  if ((friends_mode || team_code || lobby_route) && room_code.empty()) room_code = friends::room(request);
  std::string error;
  const auto decline = [&] {
    if (!g_search_generation.current(epoch)) return;
    Socket late;
    // A Slippi commit cancels normal I/O; this bounded cleanup owns only the assigned room.
    const auto stale = [&] { return !g_search_generation.current(epoch); };
    search_policy::decline_room(
        [&] { return late.open_session(url, pass, room_code, &error, 1000, stale); },
        [&] { late.send({{"t", "leave"}}); }); // Leave::Quit's casual protocol; no result
  };

  if (cancelled()) {
    if (blend && !room_code.empty()) decline();
    return fail("Cancelled");
  }
  if ((!g_broker && url.empty()) || pass.empty()) return fail("YouGame did not return a ticket");
  if ((friends_mode || team_code || lobby_route) && (room_code != friends::room(request))) return fail("YouGame returned a different friends room");
  // The reopen loop retries every 250 ms; renewals go to the site, so at most one per 1.5 s.
  uint64_t last_renew = 0;
  bool update_refused = false;
  const auto renew_ticket = [&](uint64_t until) {
    const auto now = now_ms();
    if (now >= until || cancelled() || (last_renew && now - last_renew < 1500)) return false;
    last_renew = now;
    const bool renewed = transport::renew_room_ticket(pass, room_code, [&] {
      try { return ticket_bridge(request, (int)std::min<uint64_t>(3000, until - now)); }
      catch (const update::Required&) { update_refused = true; throw; }
    }, [](const char* message) { host::log("%s", message); });
    if (renewed) pass_at = now_ms();
    return renewed;
  };

  // Unranked: wait in the queue until the matchmaker names a room.
  const bool queued = room_code.empty();
  if (queued) {
    Socket queue;
    if (!queue.open_session(url, pass, "", &error, 10000, cancelled)) return fail(error);
    for (;;) {
      if (cancelled()) {
        if (blend && g_search_generation.current(epoch)) {
          queue.send({{"t", "cancel"}});
          json late;
          while (queue.next(late, 0)) {
            if (late.value("t", "") == "matched" && late.count("room") && late["room"].is_string()) {
              room_code = late["room"].get<std::string>();
              decline();
              break;
            }
          }
        }
        return fail("Cancelled");
      }
      json m;
      if (!queue.next(m, 250)) {
        if (queue.closed()) {
          // Only a close that says the search itself is over ends it (another search took over, a
          // build or version refusal, a flood close). Any other close (a drop, a restarted or reset
          // matchmaker, a silent-socket close) reopens the queue: the player only pressed search once.
          const int qcode = queue.close_code();
          host::log("yougame: matchmaking connection closed (code %d)", qcode);
          // 4000 "replaced": Rooms keeps one search per player and a newer one took this slot. A newer
          // search of this game drops this one quietly; otherwise it came from somewhere else.
          if (qcode == 4000) {
            if (!g_search_generation.current(epoch)) return fail("Cancelled");
            return fail("This account started a search somewhere else, so this one stopped");
          }
          if (qcode == 4003 || qcode == 4403 || qcode == 1008)
            return fail("Lost the connection to YouGame matchmaking");
          const uint64_t until = now_ms() + 15000;
          bool reopened = false;
          // A ticket near its 5-minute life would only earn a 401 first: renew before reopening.
          if (now_ms() - pass_at > 240000) renew_ticket(until);
          transport::TicketRetry retry;
          while (!cancelled() && !update_refused && now_ms() < until) {
            if (retry.attempt([&] { return !update_refused && now_ms() < until && queue.open_session(url, pass, "", &error, (int)std::min<uint64_t>(3000, until - now_ms()), cancelled); },
                              [&] { if (queue.closed()) renew_ticket(until); })) { reopened = true; break; }
            Sleep(250);
          }
          if (update_refused) return fail(update::kMessage, false, true);
          if (!reopened) return fail("Lost the connection to YouGame matchmaking");
        }
        continue;
      }
      if (cancelled()) {
        // next() may already have consumed the assignment when Slippi won. Release that
        // room through the ordinary decline path before discarding the queue message.
        if (blend && m.value("t", "") == "matched" && m.count("room") && m["room"].is_string()) {
          room_code = m["room"].get<std::string>();
          decline();
        }
        return fail("Cancelled");
      }
      const auto& refused = update::refusal(m);
      if (update::required(refused)) {
        g_update_notice.refuse();
        host::log("yougame: update required (current %s)", update::current(refused).c_str());
        return fail(update::kMessage, false, true);
      }
      const std::string t = m.value("t", "");
      if (!cancelled() && t == "searching" && search.on_searching && m.count("waiting") && m["waiting"].is_number_integer())
        search.on_searching((int)std::clamp<int64_t>(m["waiting"].get<int64_t>(), 0, 1000000));
      if (!cancelled() && t == "searching")
        set_search_paused(m.count("paused") && m["paused"].is_string() ? m["paused"].get<std::string>() : std::string());
      if (t == "matched" && m.count("room") && m["room"].is_string()) {
        room_code = m["room"].get<std::string>();
        if (blend && !search.claim()) { decline(); return fail("Cancelled"); }
        // The match room takes the queue's ticket; after a long search it has expired and the room refuses
        // it (401 "Bad or expired ticket"), losing the match. Past 4 minutes, a fresh one first.
        // Rooms can renew it itself: "matched" then carries a room ticket (good for this room join only).
        if (m.count("ticket") && m["ticket"].is_string() && !m["ticket"].get<std::string>().empty()) {
          pass = m["ticket"].get<std::string>();
          pass_at = now_ms();
          host::log("yougame: the match came with a fresh room ticket");
        }
        if (now_ms() - pass_at > 240000) {
          host::log("yougame: queue ticket is %llu s old: renewing it for the match room", (unsigned long long)((now_ms() - pass_at) / 1000));
          renew_ticket(now_ms() + 5000);
          if (update_refused) return fail(update::kMessage, false, true);
        }
        break;
      }
      if (t == "error") return fail(m.value("error", "Matchmaking failed"));
    }
  } else {
    if (blend && !search.claim()) { decline(); return fail("Cancelled"); }   // ticket assignment
    if (friends_mode) host::log("friends: waiting");
    if (lobby_route && !parked) host::log(doubles_home ? "doubles: in the home" : "friends lobby: in the room");
    if (doubles_room) host::log("doubles: joining the match room");
    if (doubles_home) set_status(lobby::kDoublesWaiting);
    else if (arena) set_status("connecting");
    else if (lobby_route) set_status(lobby::kWaiting);
    else if (doubles_room) set_status("connecting");
    else set_status(team_code ? friends::kTeamWaiting : friends_mode ? friends::kWaiting : "waiting for " + search.direct_code);
  }

  // An adopted lobby room keeps its socket, roster and lobby; only the last match's state is dropped. Its
  // links still up go back to the lobby (early, below); the others close.
  const bool adopted = parked != nullptr;
  std::map<std::string, unsigned> adopted_links;
  auto room = adopted ? std::move(parked) : std::make_unique<Room>();
  ArenaKeep arena_keep_room;   // after `room`, so it runs before `room` would close the arena's room
  if (arena) arena_keep_room.room = &room;
  // A parked room kept its links (keep_links: end_links never ran), and with them a relay it ran: the
  // next match decides relay or links again, so this one ends here, outside g_mutex (it joins a thread).
  if (adopted && relay_running(room.get())) { yg_enet_enable(0); relay_stop(room.get()); }
  if (adopted) {
    std::lock_guard<std::mutex> lock(g_mutex);
    room->games = transport::Games{};
    room->in_game = false; room->pending_winner = -2; room->pending_team_result = nullptr;
    room->pending_begin = false; room->begin_at = 0; room->match_id.clear(); room->lobby = nullptr;
    room->port_ids.clear(); room->recovery = transport::Recovery{}; room->ticket_retry = {};
    if (room->arena) { room->size = size; room->mode = search.mode; room->ranked = false; }
    for (const auto& kv : room->lobby_links)
      if (std::find(room->links.ids.begin(), room->links.ids.end(), kv.second) != room->links.ids.end() &&
          direct_state(kv.second) != 3) adopted_links.insert(kv);
  }
  if (adopted) {
    std::vector<unsigned> stale;
    for (unsigned id : room->links.ids)
      if (std::none_of(adopted_links.begin(), adopted_links.end(), [&](const auto& kv) { return kv.second == id; })) stale.push_back(id);
    room->links.ids.clear();
    room->lobby_links.clear();
    for (unsigned id : stale) { try { bridge({{"type", "directClose"}, {"link", id}}, 1000); } catch (...) {} }
  } else {
    room->friends = friends_mode;
    room->size = size;
    room->mode = search.mode;
    room->ranked = search.mode == 0;
    room->socket = std::make_unique<Socket>();
    room->url = url; room->ticket = pass; room->code = room_code; room->ticket_request = request;
    room->doubles_match = doubles_room;
    room->doubles_home = search.doubles_match.home;
  }
#ifdef YOUGAME_AGENT_TEST_HOOK
  pending_input.publish(*room, adopted ? room->participants : json::array());
#endif
  // An invited room decides 1v1, the Friends lobby or the Doubles home (friends::Events::room_kind), not the
  // invite: a room of another kind refuses this ticket (lobby_mismatch: its lobby key or size differs), or
  // once joined its first frames say (welcome.muLobby or a yg.mu.lobby state: a lobby, 2 seats or a
  // `searching` key the Doubles home; neither: a 1v1 room). A refusal tries the next kind (mismatch_next);
  // frames of another kind leave the room and queue the join again that way (room_decided), once per room.
  const bool invited = (lobby_route && !search.lobby_room.empty() && !adopted) || friends_mode;
  const int joined_kind = doubles_home ? friends::kRoomDoubles : lobby_route ? friends::kRoomFriends4 : friends::kRoom1v1;
  bool kind_heard = !invited || friends::events.room_kind(room_code) >= 0;
  uint64_t welcome_at = 0;
  uint64_t hello_by = 0;   // an invited room's welcome deadline, from the loop's start (below)
  bool hello_rearmed = false;   // a reopen re-arms it once: a room that keeps dropping before its welcome ends on a line
  bool home_checked = false;   // the first welcome of an invited Doubles home was checked for a gone home
  const auto reroute = [&](int kind, bool known, const char* why) {
    const char* name = kind == friends::kRoomDoubles ? "a Doubles home"
                     : kind == friends::kRoomFriends4 ? "a Friends lobby" : "a 1v1 friend room";
    host::log("friends: room %s %s %s (%s); joining it that way", room_code.c_str(), known ? "is" : "may be", name, why);
    if (known) friends::events.room_decided(room_code, kind);
    else friends::events.room_retry(room_code, kind);
    return fail("Cancelled");   // quiet: the menu takes the queued join at once
  };
  if (!adopted && !room->socket->open_session(url, pass, room_code, &error, 10000, cancelled)) {
    // A full Friends lobby or Doubles home an invite named says so too, never "try again from their invite".
    if ((friends_mode || invited) && !cancelled() && room->socket->room_full()) return fail_room_full(room_code);
    // lobby_mismatch is also a different build's or remix's refusal: only an invite that named no size or
    // key is tried another way (friends::Events::mismatch_reroutes), each kind once; otherwise this error
    // stands, and when every kind was tried, the first refusal's. Rooms' "another Teams protocol" / "another
    // Doubles profile" refusal (or its code, teams_protocol / doubles_profile) is a room of another kind too. A
    // gone room (host_gone 410, an expired home 409) is neither: its own error stands, even on the last try
    // (friends::Events::refusal_text).
    // A kind refusal reroutes even a hinted join (it never means another build); a lobby_mismatch only an unhinted one.
    const bool kind_refusal = room->socket->kind_refused();
    if (!kind_heard && !cancelled() && (room->socket->lobby_mismatch() || kind_refusal) &&
        friends::events.mismatch_reroutes(room_code, kind_refusal)) {
      const int next = friends::events.mismatch_next(room_code, joined_kind, error, kind_refusal);
      if (next >= 0) return reroute(next, false, "the room refused this kind");
      host::log("friends: room %s refused every kind; not joining", room_code.c_str());
      return fail(friends::kJoinRefused);
    }
    // A friend's room refused the socket after a good ticket (the upgrade refused, 1006, a 403 on the room GET, or
    // no open in 10 s): one plain line, back to the menu, no other kind and no search again.
    if (invited && !cancelled()) {
      host::log("friends: room %s refused the connection; not joining", room_code.c_str());
      return fail(friends::kJoinRefused);
    }
    return fail(friends::events.refusal_text(room_code, error, false), !direct);
  }
  if (cancelled()) {
    if (adopted && room->lobby_session) {
      report_lobby_gone(room->code);
      room->lobby_session->leave();
      for (const auto& f : room->lobby_session->due(now_ms())) room->socket->send(f);
    } else if (!arena) room->socket->send({{"t", "leave"}});   // the arena's room stays (ArenaKeep)
    for (const auto& kv : adopted_links) { try { bridge({{"type", "directClose"}, {"link", kv.second}}, 1000); } catch (...) {} }
    return fail("Cancelled");
  }

  // Direct links when the hello gave them, unless a seat in the room has none (links:false, a standalone copy):
  // then the whole match plays over own UDP + STUN, every seat alike (the pool merge, 11-17). Decided again
  // whenever the roster changes; the own socket opens only when a match needs it.
  // Tests only (MELEE_TEST_YOUGAME_LINKS_SEAT): this copy opens as a seat on YouGame links (as the app's copy
  // does) with no app behind it, so a hidden pair proves a links seat moves to own UDP for a seat without links.
  const bool test_links_seat = host::test_env::getenv("MELEE_TEST_YOUGAME_LINKS_SEAT") != nullptr;
  const bool links_hello = g_direct || test_links_seat;
  bool links_mode = links_hello;
  // Own sockets: always on Windows (an app-started seat's sandbox may still have no network: then the punch
  // fails and Unranked searches again); never under a Unix broker, whose sandbox has no sockets at all.
#ifdef _WIN32
  const bool own_udp_allowed = true;
#else
  const bool own_udp_allowed = !g_broker;
#endif
  uint16_t bound_port = search.udp_port;
  udp::Ingress ingress(&udp::own_udp_drops);
  SOCKET udp = INVALID_SOCKET;
  struct Closer { SOCKET& s; ~Closer() { if (s != INVALID_SOCKET) closesocket(s); } } closer{udp};
  std::string external, lan_addr;
#ifndef _WIN32
  if (g_broker && !links_mode) return fail("YouGame broker requires direct links v1 (1200-byte datagrams)");
#endif
  // Binds the own UDP port, then STUN (up to ~3 s) and the LAN address. Empty on success, else the message.
  const auto open_own_udp = [&]() -> std::string {
    const auto requested_port = udp::port(std::getenv("YOUGAME_UDP_PORT"));
    if (!requested_port.valid) {
      host::log("%s", udp::port_error(requested_port, "invalid decimal port").c_str());
      return "The own UDP port is unavailable";
    }
    // A socket a match never adopted (parked below, its ENet never made) closes.
    if (const ENetSocket left = yg_native_park(ENET_SOCKET_NULL, 0); left != ENET_SOCKET_NULL) closesocket((SOCKET)left);
    // No port asked for (no YOUGAME_UDP_PORT, no fixed Slippi port): the first send binds one, on all
    // interfaces at a port the system picks. Windows Firewall asks nothing for a socket bound that way; an
    // explicit bind() made it ask "allow public and private networks" over the first match, taking the
    // keyboard. STUN's first request is that send. A fixed port is a deliberate listener: bound as before.
    const bool implicit_bind = !requested_port.set && !search.udp_port_fixed;
#ifdef _WIN32
    if (!implicit_bind) { std::lock_guard<std::mutex> lock(g_mutex);   // once per run: the first bind is when Windows asks
      // The note shows where the search line can carry it (plain or YouGame-only search); a blend search,
      // Friends or a lobby shows its own line, so their first bind passes without it.
      if (!g_firewall_bound) {
        g_firewall_bound = true;
        if (g_status == "searching" || g_status == blend::kSearchYouGameOnly) {
          g_firewall_note_until = now_ms() + search_policy::kFirewallNoteMs;
          host::log("yougame: first own UDP port this run: showing the firewall note");
        } else host::log("yougame: first own UDP port this run: no firewall note under \"%s\"", g_status.c_str());
      } }
#endif
    udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp == INVALID_SOCKET) {
      if (requested_port.set) host::log("%s", udp::port_error(requested_port, "socket creation failed").c_str());
      return "Could not open the network port";
    }
    sockaddr_in bind_addr{};
    SocketLength length = sizeof bind_addr;
    bool stunned = false, bound = false;
    if (implicit_bind) {
      stunned = stun(udp, external, cancelled);
      bound = getsockname(udp, (sockaddr*)&bind_addr, &length) == 0 && bind_addr.sin_port != 0;
      if (bound) host::log("yougame: own UDP: port bound by the first send");
      else host::log("yougame: own UDP: no send bound a port; binding one");   // no STUN server resolved
    }
    if (!bound) {
      bind_addr = sockaddr_in{};
      bind_addr.sin_family = AF_INET;
      if (!udp::bind_port(requested_port, search.udp_port, [&](uint16_t p) {
        bind_addr.sin_port = htons(p); // zero-initialized INADDR_ANY: all interfaces
        return bind(udp, (sockaddr*)&bind_addr, sizeof bind_addr) == 0;
      }, [](const char* message) { host::log("%s", message); }))
        return "The network port is in use";
      length = sizeof bind_addr;
      if (getsockname(udp, (sockaddr*)&bind_addr, &length) != 0 ||
          (requested_port.set && ntohs(bind_addr.sin_port) != requested_port.value)) {
        if (requested_port.set) host::log("%s", udp::port_error(requested_port, "bound port check failed").c_str());
        return "The own UDP port is unavailable";
      }
    }
    bound_port = ntohs(bind_addr.sin_port);
    if (!stunned && (bound || !stun(udp, external, cancelled))) host::log("yougame: STUN unavailable");
    lan_addr = udp::lan_candidate(std::getenv("YOUGAME_HOST_LAN_IP"), bound_port, lan_ip,
        [](const char* message) { host::log("%s", message); });
    return "";
  };
  if (!links_mode) {
    const std::string why = open_own_udp();
    if (!why.empty()) return fail(why);
  }
  if (cancelled()) return fail("Cancelled");
  // Seat 0 names the match: Slippi's match id slot and replays get this, never the room's code.
  const std::string my_match_id = "yg-" + hex64(random64());

  // Seats come from the room: the room numbers the players 0..size-1 (participants), seat n plays
  // in-game port n+1 and seat 0 decides. A peer's addresses are keyed by its connection, as the room
  // reports the sender, never by a seat the peer claims.
  json participants = json::array();
  std::map<std::string, json> cands;          // connection id -> the addresses it sent me
  std::map<std::string, uint64_t> tokens;     // connection id -> the token I gave it
  std::map<std::string, uint64_t> sent_at;    // connection id -> when I last sent it mine
  uint64_t last_ping = now_ms(), full_since = 0;
  // Only base-listing plain Unranked can meet a blending client.
  const uint64_t arrive_by = search_policy::no_show_deadline(host::gameplay::vanilla_only(), queued,
      search.mode, friends_mode || team_code, now_ms());   // a lobby has a room code: never queued
  bool arrived = false;
  int my_seat = -1;
  auto seat_of = [&](const std::string& conn) {
    return transport::connection_seat(participants, conn);
  };
  // The Friends lobby: its state model (made once the room names this player), the links opened as each
  // friend takes a seat, and the match plan once the host's Start froze the setup. lobby_view: the setup's
  // humans from the room's roster, each with its setup port as its slot (the session's player index).
  const auto request_app = [&](const json& q, int ms) { return bridge(q, ms, cancelled); };
  // Closing a link goes out even once the search is cancelled (leaving the lobby is a cancel), as
  // Room's destructor closes its links: request_app would throw before sending.
  const auto close_app = [](const json& q, int ms) { return bridge(q, ms); };
  std::unique_ptr<lobby::Session> lobby_session;
  std::shared_ptr<const lobby::Plan> lobby_plan;
  json lobby_view = json::array();
  json arena_view = json::array();   // an arena game's fighters from the room's roster, slot = ring slot
  std::string lobby_me;
  bool lobby_me_missing_logged = false, lobby_seat_missing_logged = false;
  int lobby_reported = -1;
  // Every machine of one build has the same key; the card digest has no native source yet (phase 2).
  const std::string lobby_settings = lobby::settings_digest(lobby_key), lobby_card = lobby::kCardDigestNative;
  struct EarlyScope {
    lobby::EarlyLinks links;
    std::function<json(const json&, int)> request;
    bool keep = false;
    ~EarlyScope() { if (!keep) links.forget_all(request); }
  } early;
  early.request = close_app;
  const auto connection_of = [&](const std::string& id) {
    for (const auto& p : participants)
      if (p.value("id", "") == id && p.value("localIndex", 0) == 0) return p.value("connectionId", "");
    return std::string();
  };
  // Leaving the search any way but into the match (a cancel, B on the select, a failure) leaves the lobby
  // too, so the seat frees at once and nobody waits on this player.
  // The Doubles match room has no lobby of its own (only its setup): leaving it is the room's leave.
  struct LobbyLeave {
    std::unique_ptr<lobby::Session>& session;
    Room& room;
    bool room_only;
    const std::string& code;
    bool handed_off = false;   // the session went on with the room; a reconnect's empty session did not
    ~LobbyLeave() {
      if (!handed_off && !room_only) report_lobby_gone(code);
      if (!session || !room.socket || room.socket->closed()) return;
      if (room_only) { room.socket->send({{"t", "leave"}}); return; }
      session->leave();
      for (const auto& f : session->due(now_ms())) room.socket->send(f);
    }
  } lobby_leave{lobby_session, *room, doubles_room, room_code};
  // The Doubles home's search (G2): the generation Rooms names once Start went through, this player's queue
  // socket for it, and the match room matchmaking names. The match room: when this search reached it, and
  // its room reports.
  lobby::DoublesParty party;
  std::unique_ptr<Socket> queue_socket;
  std::string queue_generation, queue_url, queue_pass, matched_room;
  const uint64_t doubles_since = now_ms();
  uint64_t doubles_epoch = 0;
  bool doubles_playing_reported = false;   // the match room's report said playing (its setup is planned)
  bool reopen_seated = false;   // an adopted lobby opens each seated friend's link again if it closed
  std::string seats_logged;     // the last "yougame: seats" line
  // Seats whose own word said links:false (a copy the launcher started: no YouGame links). Rooms marks such a
  // seat in the roster where it knows; the seat's word says it everywhere else. Either way the whole match
  // plays over own UDP, every seat alike (decide_transport).
  std::set<std::string> no_links;
  if (adopted && !arena) {
    lobby_session = std::move(room->lobby_session);
    lobby_me = std::move(room->lobby_me);
    participants = room->participants;
    lobby_session->next_match();   // a match that never began: the host calls it off (Session::next_match)
    lobby_session->take_seated();
    early.links.by_peer = std::move(adopted_links);
    reopen_seated = true;
  }
  // Once per app connection. The app refuses every directOpen until the game has attached. A copy the
  // player started itself can outlive the app (a tray update, a crash): the old section may still read
  // live, so a new channel connection (g_channel_epoch) attaches again to the new app's section.
  const auto attach_links = [&] {
    // A copy the app launched (stdio) shares the app's life, so its one attach stays good.
    const uint64_t epoch = g_channel_epoch.load();
    bool same_app;
    { std::lock_guard<std::mutex> lock(g_mutex); same_app = g_stdio || g_links_epoch == epoch; }
    if (same_app && yg_enet_channel_ready()) return;
    json h = bridge({{"type", "directAttach"}}, 10000, cancelled);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (cancelled()) throw std::runtime_error("Cancelled");
#ifdef _WIN32
    if (h.value("version", 0) != 1 || yg_enet_attach_handles(h.value("section", uintptr_t(0)),
        h.value("toApp", uintptr_t(0)), h.value("toGame", uintptr_t(0))))
      throw std::runtime_error("Could not attach direct links");
#else
    transport::PosixAttachment p;
    if (!transport::posix_attachment(h, p) || yg_enet_attach_posix(p.section, p.wake, p.wait, p.size))
      throw std::runtime_error("Could not attach YouGame POSIX direct links");
#endif
    g_links_epoch = epoch;
  };
  // The room relay (yougame_room_relay.h): this seat's app has no links helper (directAttach refused
  // no_direct, or never answered). Known before the first candidate goes out, which says so (relay:true),
  // so every seat of the match relays. No answer or another refusal relays this search only; no_direct
  // also skips the attach for kNoHelperCooldownMs; an attach that succeeds clears that.
  bool relay_self = false;
  const auto probe_relay = [&](const char* when) {
    if (!links_mode || relay_self || test_links_seat) return;
    const char* why = "the links helper refused a moment ago";
    if (!no_helper_cooling()) {
      try { attach_links(); helper_attached(); return; }
      catch (const update::Required&) { throw; }
      catch (const BridgeNoAnswer& e) { why = "the app did not answer the attach"; host::log("yougame: direct attach %s failed: %s", when, e.what()); }
      catch (const std::exception& e) {
        const bool no_direct = helper_refused();
        if (!no_direct && !attach_refused()) { host::log("yougame: direct attach %s failed: %s", when, e.what()); return; }
        if (no_direct) { no_helper_cooldown(); why = "no_direct"; }
        else { why = "the app refused the attach"; host::log("yougame: direct attach %s refused: %s", when, e.what()); }
      }
      if (cancelled()) return;
    }
    relay_self = true;
    host::log("%s (%s)", room_relay::kLog, why);
  };
  const auto relay_flag = [](const json& c) { return c.is_object() && c.count("relay") && c["relay"].is_boolean() && c["relay"].get<bool>(); };
  // The room's roster decides links or own UDP for every seat alike (links_hello seats only; a seat without
  // links never had a choice). A change restarts the address exchange in the new shape; a lobby back on
  // links attaches and opens its seated friends' links again.
  const auto decide_transport = [&] {
    if (!links_hello) return;
    // An arena game: only its fighters' seats count (the others watch and open no links).
    const bool want = !transport::any_seat_without_links(arena ? transport::arena_roster(participants, search.arena_fighters)
                                                               : participants) && no_links.empty();
    if (want == links_mode) return;
    links_mode = want;
    cands.clear();
    sent_at.clear();
    if (!links_mode) {
      host::log("yougame: a seat has no YouGame links: this match plays over own UDP%s",
                own_udp_allowed ? "" : ", which this copy can't open");
      return;
    }
    host::log("yougame: every seat has YouGame links again: back to direct links");
    if (lobby_route) {
      reopen_seated = true;
      probe_relay("in the lobby");
    }
  };
  decide_transport();
  // A lobby opens each friend's link when they take a seat, so attach first: a game whose first online
  // mode this run is the lobby was refused no_direct there while a host already attached waited on it.
  // Every links search attaches here too, before its first candidate: a missing helper means the relay.
  if (links_mode) probe_relay(lobby_route ? "before the lobby" : "before the match");
  // An invited room says welcome as soon as it opens: without one in kHelloMs the join ends on a line, never a
  // silent wait (the loop's hello_by check).
  if (invited && !adopted) hello_by = now_ms() + friends::kHelloMs;
  for (;;) {
    if (cancelled()) return fail("Cancelled");
    if (friends_mode && friends::events.take_version()) return fail(friends::kVersion);
    json m;
    const bool received = room->socket->next(m, 200);
    if (cancelled()) return fail("Cancelled");
    if (received) {
      const auto& refused = update::refusal(m);
      if (update::required(refused)) {
        g_update_notice.refuse();
        host::log("yougame: update required (current %s)", update::current(refused).c_str());
        return fail(update::kMessage, false, true);
      }
      const std::string t = m.value("t", "");
      if (t == "welcome") { room->me = m.value("me", ""); hello_by = 0; }
      // An invited Doubles guest alone in the home it joined: the friend's home expired or emptied and this ticket
      // opened a fresh one under its code. Leave it and say the room is gone (no retry: a gone room is never
      // another kind). Rooms opened that home before any welcome; leaving at once means the game never keeps it,
      // reports it to the page or invites into it, so a friend's dead code never becomes this player's home.
      if (t == "welcome" && invited && doubles_home && !home_checked) {
        home_checked = true;
        if (lobby::lone_welcome(m)) {
          host::log("friends: room %s is an empty Doubles home (the friend's home is gone); leaving", room_code.c_str());
          room->socket->send({{"t", "leave"}});
          return fail(friends::kRoomGone);
        }
      }
      if (!kind_heard) {
        const bool lobby_frame = (t == "welcome" && m.count("muLobby") && m["muLobby"].is_object()) ||
                                 (t == lobby::kType && ranked::str(m, "op") == "state");
        if (lobby_frame) {
          kind_heard = true;
          const int kind = lobby::doubles_frame(t == "welcome" ? m["muLobby"] : m) ? friends::kRoomDoubles : friends::kRoomFriends4;
          if (kind != joined_kind) {
            room->socket->send({{"t", "leave"}});
            return reroute(kind, true, "its first frames carry that lobby");
          }
          friends::events.room_seen(room_code, kind);
        } else if (t == "welcome" && !welcome_at) welcome_at = std::max<uint64_t>(now_ms(), 1);
      }
      if (m.count("participants") && m["participants"].is_array()) {
        participants = m["participants"];
        // How Rooms marks each seat's links (the key decides links or own UDP for the whole match): logged once
        // per change, mine first. No ids, names or addresses.
        std::string seats;
        for (const auto& p : participants) {
          if (!p.is_object()) continue;
          const auto l = p.find("links");
          const char* v = l == p.end() ? "absent" : l->is_boolean() ? (l->get<bool>() ? "true" : "false") : "other";
          seats += std::string(seats.empty() ? "" : ", ") + (p.value("connectionId", "") == room->me ? "me " : "") + "links " + v;
        }
        if (seats != seats_logged) { seats_logged = seats; host::log("yougame: seats: %s (my hello links %d)", seats.c_str(), links_hello ? 1 : 0); }
        decide_transport();
      }
      read_roster(*room, m);
      if (doubles_home && (t == "lobby" || t == "welcome") && party.observe(m) && !party.generation.empty() &&
          party.generation != queue_generation)
        host::log("doubles: the home's search is on (%d %s)", party.humans, party.humans == 1 ? "human" : "humans");
      if (doubles_room && !lobby_plan && lobby::doubles_match_ended(m)) {
        host::log("doubles: the match ended before it started (%s)", ranked::log_text(t, 32).c_str());
        return fail(lobby::kDoublesEnded);
      }
      if (lobby_match) {
        if (!lobby_session && !room->me.empty()) {
          for (const auto& p : participants)
            if (p.value("connectionId", "") == room->me && p.value("localIndex", 0) == 0) lobby_me = p.value("id", "");
          if (lobby_me.empty()) {
            if (!lobby_me_missing_logged) {
              lobby_me_missing_logged = true;
              host::log("friends lobby: this connection (%s) has no participant row yet (%zu rows)",
                        ranked::log_text(room->me, 12).c_str(), participants.size());
              for (const auto& p : participants)
                host::log("friends lobby:   participant %s connection %s local %d",
                          ranked::log_text(p.value("id", ""), 12).c_str(),
                          ranked::log_text(p.value("connectionId", ""), 12).c_str(), p.value("localIndex", 0));
            }
          } else {
            host::log("friends lobby: the session is %s on connection %s",
                      ranked::log_text(lobby_me, 12).c_str(), ranked::log_text(room->me, 12).c_str());
            lobby::Session::Options o;
            o.me = lobby_me; o.connection_id = room->me; o.profile = lobby_profile;
            // The Doubles match room takes only the setup: no hello (the home sent it), nothing to call off.
            if (!doubles_room) {
              o.settings_digest = lobby_settings; o.card_digest = lobby_card;
              o.call_off_id = carried_call_off.id; o.call_off_setup = carried_call_off.setup;
            }
            lobby_session = std::make_unique<lobby::Session>(o);
          }
        }
        if (lobby_session) {
          lobby_session->receive(m);
          if (const char* bad = lobby_session->take_bad_frame()) host::log("friends lobby: unusable room frame (%s)", bad);
          if (lobby_session->known() && lobby_session->my_seat() < 0 && !lobby_seat_missing_logged) {
            lobby_seat_missing_logged = true;
            host::log("friends lobby: no seat is %s's", ranked::log_text(lobby_me, 12).c_str());
            for (const auto& seat : lobby_session->state().seats)
              host::log("friends lobby:   seat %d kind %d id %s connection %s", seat.seat, (int) seat.kind,
                        ranked::log_text(seat.id, 12).c_str(), ranked::log_text(seat.connection_id, 12).c_str());
          }
          if (lobby_session->unsupported() && lobby_route) {
            if (!kind_heard) { kind_heard = true; return reroute(friends::kRoom1v1, true, "the room has no Friends lobby"); }
            return fail(lobby::kUnsupported);
          }
        }
        if (t == "leave" || t == "away") early.links.forget(transport::leave_id(m, room->socket->brokered()), close_app);
        // The host's call-off of a match that never began (Session::call_off_setup): Rooms ends that match
        // (sessionEnded, reason host_ended) and sends the lobby without a setup. Only the match ended: the
        // room, its seats, picks and rules stay, and this search goes on (never a close or a failure).
        if (t == "sessionEnded")
          host::log("friends lobby: the room ended the match that never began (%s); the lobby stays",
                    ranked::log_text(ranked::str(m, "reason"), 32).c_str());
        if (t == "command-error" && ranked::str(m, "action") == "end-session")
          host::log("friends lobby: the room kept the match: %s", ranked::log_text(ranked::str(m, "error"), 160).c_str());
      }
#ifdef MELEE_DOUBLES
      if (doubles_match) {
        consent.observe(m);
        if (doubles::terminal(m)) return fail("The Doubles match ended before it started");
        // A begin before all four are ready is refused and sent again: expected, so each refusal is logged once.
        if (t == "command-error" && !setup.known) {
          const std::string refusal = ranked::log_text(m.value("action", ""), 32) + ": " + ranked::log_text(m.value("error", ""), 160);
          if (refusal != last_refusal) host::log("doubles: the room refused %s", refusal.c_str());
          last_refusal = refusal;
        }
        if (!setup.known && doubles_teams::start_console(m)) {
          // The host's begin went through: the start names the session, its seed, and each seat's port and team.
          doubles_teams::Setup started;
          if (const char* why = doubles_teams::read_start(m, doubles::digest(), started)) {
            host::log("doubles: the room's start is unusable (%s)", why);
            return fail("The Doubles room started a match this game can't play. Press B to leave");
          }
          if (!doubles_teams::seated(started, room->me)) {
            host::log("doubles: the room's start does not seat this player");
            return fail("The Doubles room started a match without you. Press B to leave");
          }
          setup = started; room->match_id = setup.match;
          host::log("doubles: the room started the match (teams %d%d%d%d)", setup.team[0], setup.team[1], setup.team[2], setup.team[3]);
          doubles::ui.teams_start(team_room);
        }
      }
#endif
#ifdef YOUGAME_AGENT_TEST_HOOK
      pending_input.publish(*room, room->participants);
#endif
      if (friends_mode && friends::version_reason(m) && (t == "error" || t == "notice" || t == "refused"))
        return fail(friends::kVersion);
      if (t == "error") return fail(m.value("error", m.value("message", "The room refused this player")), !direct);
      if (friends_mode && t == "command-error" && m.value("action", "") == "join" &&
          friends::says_room_full(ranked::str(m, "error"))) return fail_room_full(room_code);
      if (t == "leave" || t == "away") {
        // A player left before the match: forget what they sent (a newcomer gets a fresh exchange).
        const std::string who = transport::leave_id(m, room->socket->brokered());
        cands.erase(who);
        sent_at.erase(who);
        if (no_links.erase(who)) decide_transport();
      }
      if (t == "msg" && m.count("d") && m["d"].is_object()) {
        const json& d = m["d"];
        const std::string from = m.value("from", "");
        if (d.value("kind", "") == "mu-cand" && d.value("v", 0) == 2 && seat_of(from) >= 0) {
          if (d.value("compat", "") != compat) return fail("Your opponent is on a different Melee Unlocked version or mod build");
          // A player we had not heard from: answer at once. Ours may have gone out before they
          // joined, and once we hold every address we stop sending.
          if (!cands.count(from)) sent_at.erase(from);
          // A seat with no YouGame links says so in its word (links:false): a links seat then plays this match
          // over own UDP, as when Rooms marks the seat. Its exchange starts again in that shape.
          if (links_hello && transport::seat_without_links(d) && no_links.insert(from).second) decide_transport();
          // Over own UDP only a word with addresses counts. A links seat's first word has none: it sends its
          // addresses once this copy's word has moved it to own UDP.
          if (links_mode || d.count("token")) {
            cands[from] = links_mode ? transport::candidate(true, d.value("compat", ""), d.value("code", ""), d.value("match", ""), "", "", "") : d;
            if (links_mode && relay_flag(d)) cands[from]["relay"] = true;   // that seat relays: so does the match
          }
        }
      }
    } else if (room->socket->closed()) {
      const auto seats_full = [&] { return friends_mode && room->socket->room_full(); };
      if (seats_full()) return fail_room_full(room_code);
      if (!room->socket->recoverable()) return fail("Lost the connection to the YouGame room", !direct);
      const uint64_t until = now_ms() + 15000;
      bool reopened = false;
      if (now_ms() - pass_at > 240000) renew_ticket(until);   // as the queue: no 401 on an old ticket first
      transport::TicketRetry retry;
      while (!cancelled() && !update_refused && now_ms() < until) {
        if (retry.attempt([&] { return !update_refused && now_ms() < until && room->socket->open_session(url, pass, room_code, &error, (int)std::min<uint64_t>(3000, until - now_ms()), cancelled); },
                          [&] { if (room->socket->closed()) renew_ticket(until); })) { reopened = true; room->ticket = pass; break; }
        if (seats_full()) break;   // final: never another try
        Sleep(250);
      }
      if (update_refused) return fail(update::kMessage, false, true);
      if (!reopened && !cancelled() && seats_full()) return fail_room_full(room_code);
      if (!reopened) return fail("Lost the connection to the YouGame room", !direct);
      participants = json::array(); cands.clear(); sent_at.clear(); tokens.clear(); no_links.clear();
      room->me.clear(); room->participants = json::array(); full_since = 0;
      lobby_reported = -1;   // the page dropped the report with the old ticket: report again, even if nothing moved
      if (hello_by && !hello_rearmed) { hello_by = now_ms() + friends::kHelloMs; hello_rearmed = true; }   // once
      if (lobby_match) {
        // A new connection: the room's next welcome carries the lobby again (Rooms holds the seat, the
        // pick and the host role for 5 s). Links to the old connection are gone; a fresh session
        // re-sends hello and opens every seated friend's link again.
        lobby_session.reset();
        early.links.forget_all(close_app);
      }
#ifdef YOUGAME_AGENT_TEST_HOOK
      pending_input.publish(*room, room->participants);
#endif
      continue;
    }
    if (hello_by && now_ms() >= hello_by) {
      host::log("friends: room %s opened but sent no welcome in %d s; not joining", room_code.c_str(), friends::kHelloMs / 1000);
      room->socket->send({{"t", "leave"}});
      return fail(friends::kJoinRefused);
    }
    my_seat = seat_of(room->me);
    // A welcome without the lobby, and no lobby state within a second of it: a 1v1 friend room.
    if (!kind_heard && welcome_at && now_ms() - welcome_at >= 1000) {
      kind_heard = true;
      if (joined_kind != friends::kRoom1v1) {
        room->socket->send({{"t", "leave"}});
        return reroute(friends::kRoom1v1, true, "its first frames carry no lobby");
      }
      friends::events.room_seen(room_code, friends::kRoom1v1);
    }
    if (lobby_match && lobby_session && !lobby_route) {   // the Doubles match room: its setup, pings, report
      for (auto& f : lobby_session->due(now_ms())) room->socket->send(f);
      // The match's report (queue casual): assembling until the setup is planned, then once more as playing.
      if ((room->revision != lobby_reported || (lobby_plan && !doubles_playing_reported)) && !lobby_me.empty()) {
        lobby_reported = room->revision;
        doubles_playing_reported = lobby_plan != nullptr;
        if (!doubles_epoch) doubles_epoch = lobby::next_room_epoch();
        try {
          bridge(lobby::doubles_room_report(room_code, search.doubles_match.home, true, doubles_epoch, room->participants,
                                            lobby_me, room->revision, lobby_plan != nullptr), 1000);
        } catch (const update::Required&) { throw; } catch (...) {}
      }
      if (!lobby_plan && now_ms() - doubles_since > lobby::kDoublesSetupMs) {
        // Past Rooms' own arrival and late-player hold: the room ends the match itself. Closed without a
        // leave (LobbyLeave skips a closed socket): a leave in a placed match is a player walking out.
        host::log("doubles: the match room sent no setup in %d s", (int)(lobby::kDoublesSetupMs / 1000));
        room->socket->close();
        return fail(lobby::kDoublesNoSetup);
      }
    }
    if (lobby_route && lobby_session) {
      if (search.lobby_control)
        search.lobby_control->run(*lobby_session, lobby_link_info(*lobby_session, participants, early.links.by_peer), room_code);
      for (auto& f : lobby_session->due(now_ms())) room->socket->send(f);
      for (const auto& seated : lobby_session->take_seated())
        if (links_mode && !relay_self) early.links.open<update::Required>(room->socket->connection(), connection_of(seated.id), request_app);
      if (reopen_seated && links_mode && !relay_self && lobby_session->known()) {
        reopen_seated = false;
        for (const auto& seat : lobby_session->state().seats)
          if (seat.kind == lobby::Kind::Human && seat.id != lobby_me)
            early.links.open<update::Required>(room->socket->connection(), connection_of(seat.id), request_app);
      }
      // The app's held-room contract: the room's roster before any invite, again when it changes. The
      // Doubles home reports the Teams summary (kind party), with a newer roomEpoch each time it is back.
      if (room->revision != lobby_reported && !lobby_me.empty()) {
        lobby_reported = room->revision;
        if (doubles_home && !doubles_epoch) doubles_epoch = lobby::next_room_epoch();
        try {
          if (doubles_home) {
            lobby_report_replaced();
            bridge(lobby::doubles_room_report(room_code, room_code, false, doubles_epoch, room->participants, lobby_me,
                                              room->revision), 1000);
          } else
            report_lobby(room_code, json{{"type", "mpRoom"}, {"code", room_code}, {"queue", "private"}, {"size", 4}, {"me", lobby_me},
                                         {"players", room->participants}, {"participants", room->participants},
                                         {"revision", room->revision}});
        } catch (const update::Required&) { throw; } catch (...) {}
      }
      if (doubles_home) {
        // Start went through (Rooms: searching, and the party's generation): this player's queue ticket for
        // it, one socket polled here. Searching off (cancel, a cross-team mismatch, the home released) and
        // not placed: the socket closes. matched: the home parks and the match room is joined below.
        if (lobby_session->searching() && !queue_socket && !party.generation.empty() && party.generation != queue_generation) {
          queue_generation = party.generation;
          try {
            const json queue_request = friends::doubles_queue_ticket(lobby_key, room_code, queue_generation, lobby_session->owns_local() ? 1 : party.humans);
            const json qt = ticket_bridge(queue_request, 15000);
            queue_url = qt.value("url", "");
            queue_pass = qt.value("ticket", "");
            auto qs = std::make_unique<Socket>();
            std::string queue_error;
            if ((!g_broker && queue_url.empty()) || queue_pass.empty())
              throw std::runtime_error("YouGame did not return a Doubles search ticket");
            if (!qs->open_session(queue_url, queue_pass, "", &queue_error, 10000, cancelled)) throw std::runtime_error(queue_error);
            queue_socket = std::move(qs);
            host::log("doubles: searching for %s", party.humans == 2 ? "opponents" : "a team");
          } catch (const update::Required&) { throw; }
          catch (const std::exception& e) {
            if (cancelled()) return fail("Cancelled");
            host::log("doubles: the search could not open (%s); stopping it", ranked::log_text(e.what(), 120).c_str());
            lobby_session->cancel();
          }
        }
        while (queue_socket && matched_room.empty()) {
          json qm;
          if (!queue_socket->next(qm, 0)) break;
          const auto& refused = update::refusal(qm);
          if (update::required(refused)) {
            g_update_notice.refuse();
            return fail(update::kMessage, false, true);
          }
          const lobby::QueueMessage news = lobby::queue_message(qm);
          if (news.news == lobby::QueueNews::Matched) matched_room = news.room;
          else if (news.news == lobby::QueueNews::Searching && search.on_searching && news.waiting >= 0) search.on_searching(news.waiting);
          else if (news.news == lobby::QueueNews::Refused) {
            // The home hears it too (a state with the refusal, searching off): the screen says why. Cancel the
            // home's search as well, so it never stays Searching on a refusal the home did not hear (a
            // cancel when it is already off is a no-op in Rooms).
            host::log("doubles: the search stopped (%s)", ranked::log_text(news.code, 32).c_str());
            queue_socket->close();
            queue_socket.reset();
            lobby_session->cancel();
          }
        }
        if (queue_socket && matched_room.empty() && queue_socket->closed()) {
          host::log("doubles: the search's connection closed; stopping the search");
          queue_socket.reset();
          lobby_session->cancel();
        }
        if (queue_socket && matched_room.empty() && !lobby_session->searching() && !party.playing) {
          queue_socket->send({{"t", "cancel"}});
          queue_socket->close();
          queue_socket.reset();
          host::log("doubles: the search stopped");
        }
        if (!matched_room.empty()) {
          // The match: park the home (its pump keeps it current and runs the screen; its links close, as
          // the three link slots go to the match), then join the match room with the queue's ticket.
          host::log("doubles: matchmaking placed the home; joining the match");
          if (queue_socket) { queue_socket->close(); queue_socket.reset(); }
          // The teammate's link can't carry over: a link is the app's directOpen {conn, peer} on the home's
          // connection, and in the match room both players have new connections (and the match's links come
          // from its own candidate exchange), so a kept one would hold one of the three slots for nothing.
          early.links.forget_all(close_app);
          Search next = search;
          next.friends_lobby = false;
          next.lobby_room.clear();
          next.epoch = epoch;
          next.doubles_match.url = queue_url;
          next.doubles_match.ticket = queue_pass;
          next.doubles_match.room = matched_room;
          next.doubles_match.home = room_code;
          lobby_leave.handed_off = true;
          room->lobby_session = std::move(lobby_session);
          room->lobby_me = lobby_me;
          room->lobby_control = search.lobby_control;
          room->lobby_links.clear();
          {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_search_generation.current(epoch) || search_stopped()) {
              Match out; out.error = "Cancelled"; return out;   // the room's destructor closes it
            }
            if (g_parked_lobby) std::thread([old = std::move(g_parked_lobby)]() mutable { shutdown_room(std::move(old)); }).detach();
            Room* raw = room.get();
            g_parked_lobby = std::move(room);
            raw->pump = std::thread(pump_room, raw);
          }
          friends_setup.keep = true;   // the home is still this player's room
          return find_match(next, cancel_requested);
        }
      }
      if (lobby_plan && !lobby_session->has_setup()) {   // Rooms dropped it before the splash
        host::log("friends lobby: the room dropped the match before it began; back to the select");
        lobby_plan.reset();
      }
    }
    if (lobby_match && lobby_session) {
      if (!lobby_plan && lobby_session->has_setup()) {
        auto built = std::make_shared<lobby::Plan>(lobby::plan(lobby_session->setup(), lobby_me, lobby_settings, lobby_card, lobby_profile, lobby_session->squad_budget()));
        if (!built->ok) {
          host::log("%s: the room's setup is unusable (%s)", doubles_room ? "doubles" : "friends lobby", built->why.c_str());
          return fail(doubles_room ? lobby::kDoublesUnusable : lobby::kSetupUnusable);
        }
        if (built->machines < 2) return fail(lobby::kCpuOnly);
        if (built->match_id.empty()) return fail(doubles_room ? lobby::kDoublesUnusable : lobby::kSetupUnusable);
        host::log("%s (%d players, %d CPUs, stage %d)", doubles_room ? "doubles: the match room froze the setup" : "friends lobby: the host started the match",
                  built->humans, built->ports - built->humans, built->stage);
        lobby_plan = built;
        // The select shows Starting (the home's pump publishes it; the home itself never gets a setup).
        if (doubles_room && search.lobby_control) search.lobby_control->set_matched(true);
      }
    }
    if (lobby_match) lobby_view = lobby_plan ? lobby::match_roster(participants, *lobby_plan) : json::array();
    if (arena) arena_view = transport::arena_roster(participants, search.arena_fighters);
    const json& active = lobby_match ? lobby_view : arena ? arena_view : participants;
    const bool full = lobby_match ? (!lobby_view.empty() && transport::connection_seat(lobby_view, room->me) >= 0)
                    : arena ? (!arena_view.empty() && transport::connection_seat(arena_view, room->me) >= 0)
                            : (int)participants.size() == size && my_seat >= 0;
    if (full) arrived = true;
    else if (search_policy::no_show_expired(arrive_by, arrived, now_ms())) {
      host::log("yougame: unranked: opponent never joined the room after 30 s; searching again");
      room->socket->send({{"t", "leave"}});
      return fail("Your opponent did not arrive", true);
    }
    if (!full) full_since = 0;
    else if (!full_since) { full_since = now_ms(); set_status("connecting"); }
#ifdef MELEE_DOUBLES
    // The match room holds the four until each says ready for this roster (as the console admission did).
    if (doubles_match) if (auto ready = consent.ready(full)) room->socket->send(*ready);
    // Then the host freezes the match (Rooms' console begin) at the revision and epoch the four said
    // ready at; a begin before the others' ready is refused and sent again.
    if (doubles_match && full && !setup.known && room->me == room->host_connection && consent.have_epoch &&
        consent.revision >= 0 && consent.sent_revision == consent.revision && now_ms() - begin_sent >= 2000) {
      room->socket->send(consent.begin(3));
      begin_sent = now_ms();
    }
#endif
    // A links seat in a match with a seat that has none: its own socket, STUN and LAN address, once.
    if (full && !links_mode && udp == INVALID_SOCKET) {
      if (!own_udp_allowed) {
        host::log("yougame: a seat has no YouGame links and this copy has no sockets of its own (the app's broker)");
        if (!lobby_session) room->socket->send({{"t", "leave"}});
        return fail("This opponent's game can't connect through the YouGame app yet", !direct);
      }
      const std::string why = open_own_udp();
      if (!why.empty()) return fail(why, !direct);
      if (cancelled()) return fail("Cancelled");
    }
    // My addresses, point to point, each peer with a token of its own.
    if (full) {
      for (auto& p : active) {
        const std::string conn = p.value("connectionId", "");
        if (conn == room->me || p.value("localIndex", 0) != 0) continue;
        if (sent_at.count(conn) && now_ms() - sent_at[conn] < 1000) continue;
        if (!links_mode && !tokens.count(conn)) tokens[conn] = random64() | 1;
        json cand = transport::candidate(links_mode, compat, me.code, my_match_id, lan_addr, external,
                                         links_mode ? "" : hex64(tokens[conn]));
        if (links_mode && relay_self) cand["relay"] = true;
        if (!links_hello) cand["links"] = false;   // no YouGame links here: every seat of this match takes own UDP
        room->socket->send({{"t", "msg"}, {"to", conn}, {"d", std::move(cand)}});
        sent_at[conn] = now_ms();
      }
    }
    if (!lobby_match && now_ms() - last_ping > 5000) {   // the lobby's session keeps its own (yougame_lobby.h)
      room->socket->send({{"t", "ping"}, {"at", now_ms()}});
      last_ping = now_ms();
    }
    if (!full) continue;
    bool all = true;
    for (auto& p : active) {
      const std::string conn = p.value("connectionId", "");
      if (conn != room->me && (!cands.count(conn) || !sent_at.count(conn))) all = false;
    }
    // A Doubles match plays only the match Rooms started (its seed, ports and teams).
    if (all && (!doubles_match || setup.known)) break;
    if (all && now_ms() - full_since > 20000) return fail("The Doubles match didn't start", true);
    if (all) continue;
    // A seated opponent who never answers (crashed, stuck): give up and, in Unranked, search again.
    if (now_ms() - full_since > 15000) return fail("Your opponent did not answer", !direct);
  }

  // The Friends lobby plays its setup's humans, numbered by setup port; the room's own roster stays for
  // the room's recovery.
  const json room_roster = participants;
  if (lobby_match) { participants = lobby_view; my_seat = lobby_plan->local_port; }
  if (arena) { participants = arena_view; my_seat = seat_of(room->me); }
  // Punch to every other seat.
  std::vector<PunchPeer> peers;
  std::string match_id = my_seat == 0 ? my_match_id : "";
  for (auto& p : participants) {
    const std::string conn = p.value("connectionId", "");
    if (conn == room->me) continue;
    const json& c = cands[conn];
    PunchPeer peer;
    peer.port = p.value("slot", 0) + 1;
    if (!links_mode) peer.mine = tokens[conn];
    if (!links_mode) peer.theirs = std::strtoull(c.value("token", "").c_str(), nullptr, 16);
    if (!links_mode) for (const char* key : {"lan", "ext"}) {
      sockaddr_in a;
      if (parse_addr(c.value(key, ""), a) && std::none_of(peer.candidates.begin(), peer.candidates.end(), [&](auto& x) { return same_addr(x, a); })) {
        peer.candidates.push_back(a);
        peer.kinds.push_back(key);
      }
    }
    if (peer.port == 1) match_id = c.value("match", "");
    peers.push_back(peer);
  }
  const uint64_t connect_start = now_ms();   // linkReport's ms
  // Any seat without a links helper (its candidate said relay, or this one) moves the whole match onto
  // the room relay: the same datagrams, carried by the room's msg relay.
  bool relay_match = links_mode && relay_self;
  if (links_mode) for (const auto& kv : cands) if (relay_flag(kv.second)) relay_match = true;
  // Own UDP found no way through (both seats behind NAT, a firewall): before giving up on this opponent, each
  // seat says so to the room (mu-relay-ok) and waits a few seconds for every other seat's word. Only when all
  // agree does the match move onto the room relay, slower but playable; a copy without this never sends the
  // word, so a mixed room fails as before. A seat whose punch worked never waits, and the others time out.
  constexpr int kRelayAgreeMs = 5000;
  const auto own_udp_relay = [&]() -> bool {
    host::log("yougame: connect failed (own UDP): my STUN %s, my LAN %s", external.empty() ? "none" : "ok", lan_addr.empty() ? "none" : "ok");
    if (cancelled() || peers.empty() || !room->socket || room->socket->closed()) return false;
    room->socket->send({{"t", "msg"}, {"d", {{"kind", "mu-relay-ok"}, {"v", 1}, {"compat", compat}}}});
    std::set<std::string> want, agreed;
    for (const auto& p : participants) if (p.value("connectionId", "") != room->me) want.insert(p.value("connectionId", ""));
    const uint64_t until = now_ms() + kRelayAgreeMs;
    while (agreed.size() < want.size() && now_ms() < until && !cancelled()) {
      json m;
      if (!room->socket->next(m, 100)) { if (room->socket->closed()) return false; continue; }
      const std::string t = m.value("t", "");
      if ((t == "leave" || t == "away") && want.count(transport::leave_id(m, room->socket->brokered()))) return false;
      if (t != "msg" || !m.count("d") || !m["d"].is_object()) continue;
      const json& d = m["d"];
      const std::string from = m.value("from", "");
      if (d.value("kind", "") == "mu-relay-ok" && d.value("v", 0) == 1 && d.value("compat", "") == compat && want.count(from))
        agreed.insert(from);
    }
    if (cancelled() || agreed.size() < want.size()) {
      host::log("yougame: room relay not agreed (%d of %d seats)", (int)agreed.size(), (int)want.size());
      return false;
    }
    std::vector<int> ports;
    for (const auto& peer : peers) ports.push_back(peer.port);
    const auto links = relay_start(room.get(), room->socket.get(), my_seat + 1, ports);
    if (links.empty()) { host::log("yougame: the room relay could not start"); return false; }
    for (auto& peer : peers) {
      const auto it = links.find(peer.port);
      if (it == links.end()) continue;
      peer.chosen.sin_family = AF_INET;
      peer.chosen.sin_addr.s_addr = htonl(0x0aff0000u | it->second);
      peer.chosen.sin_port = htons(7777);
    }
    { std::lock_guard<std::mutex> lock(g_mutex);
      if (cancelled()) { relay_stop(room.get()); return false; }
      g_relay_own = true;
      select_match_transport(g_slippi_match);
      yg_enet_enable(1);
    }
    relay_match = true;
    host::log("yougame: own UDP failed: every seat agreed, this match uses the room relay");
    return true;
  };
  if (relay_match) {
    if (!relay_self) host::log("yougame: a seat has no direct links helper: this match uses the room relay");
    if (lobby_route) early.links.forget_all(close_app);   // no ring links wait for anyone
    std::vector<int> ports;
    for (const auto& peer : peers) ports.push_back(peer.port);
    const auto links = relay_start(room.get(), room->socket.get(), my_seat + 1, ports);
    if (links.empty()) {
      host::log("yougame: the room relay could not start");
      return fail(console::kHelperMissing, false);
    }
    for (auto& peer : peers) {
      const auto it = links.find(peer.port);
      if (it == links.end()) continue;
      peer.chosen.sin_family = AF_INET;
      peer.chosen.sin_addr.s_addr = htonl(0x0aff0000u | it->second);
      peer.chosen.sin_port = htons(7777);
    }
    if (lobby_route) {
      room->lobby_links.clear();
      for (const auto& p : participants) {
        const std::string conn = p.value("connectionId", "");
        const auto it = links.find(p.value("slot", -1) + 1);
        if (conn != room->me && it != links.end()) room->lobby_links[conn] = it->second;
      }
    }
    { std::lock_guard<std::mutex> lock(g_mutex);
      if (cancelled()) { relay_stop(room.get()); return fail("Cancelled"); }
      yg_enet_enable(1);
    }
  } else if (links_mode) {
    relay_clear_stale();
    try {
      // Idempotent once per run. Mapping stays alive across matches; only the host owns ring I/O.
      attach_links();
      helper_attached();
      std::vector<std::string> remotes;
      for (const auto& p : participants) if (p.value("connectionId", "") != room->me) remotes.push_back(p.value("connectionId", ""));
      if (lobby_route) {
        // The lobby opened each friend's link when they took a seat: links to anyone not in the match
        // close, any still missing open, and the match's pass to the room (it closes them), then wait.
        early.links.forget_dead(direct_state, close_app);
        room->links.begin(now_ms());
        room->links.ids = early.links.hand_over<update::Required>(room->socket->connection(), remotes, request_app, close_app);
        if (room->links.ids.size() != remotes.size()) throw std::runtime_error("Direct link failed");
        room->lobby_links.clear();
        for (size_t i = 0; i < remotes.size(); ++i) room->lobby_links[remotes[i]] = room->links.ids[i];
        while (!room->links.up(now_ms(), direct_state)) {
          if (cancelled() || room->links.failed(now_ms(), direct_state)) throw std::runtime_error("Direct link failed");
          Sleep(10);
        }
      } else if (!transport::open_links(room->links, room->socket->connection(), remotes,
          [&](const json& q, int ms) { return bridge(q, ms, cancelled); }, direct_state, now_ms, [] { Sleep(10); }, cancelled))
        throw std::runtime_error("Direct link failed");
      size_t index = 0;
      for (const auto& p : participants) {
        if (p.value("connectionId", "") == room->me) continue;
        const unsigned link = room->links.ids[index++];
        for (auto& peer : peers) if (peer.port == p.value("slot", -1) + 1) {
          peer.chosen.sin_family = AF_INET;
          peer.chosen.sin_addr.s_addr = htonl(0x0aff0000u | link);
          peer.chosen.sin_port = htons(7777);
        }
      }
      for (unsigned link : room->links.ids) {
        ENetAddress a;
        if (yg_enet_link_address(link, &a)) throw std::runtime_error("Invalid direct address");
      }
      { std::lock_guard<std::mutex> lock(g_mutex);
        if (cancelled()) throw std::runtime_error("Cancelled");
        yg_enet_enable(1);
      }
    } catch (const update::Required&) { throw; }
    catch (...) {
      // Read first: any later bridge call clears it. no_direct is this computer's own helper: the helper
      // went away after this search attached. The other seats are already waiting on links, so this
      // attempt fails as a link failure; the next search on this app goes straight to the room relay.
      const bool helper_missing = helper_refused();
      try { throw; }
      catch (const std::exception& e) { host::log("yougame: direct links failed: %s", e.what()); }
      catch (...) { host::log("yougame: direct links failed"); }
      if (cancelled()) return fail("Cancelled");
      if (helper_missing) {
        no_helper_cooldown();
        host::log("yougame: the links helper refused (no_direct): the next search uses the room relay");
      }
      // Count only peers whose link failed, never a teammate who connected.
      // Even a partial successful link resets the previous failure history.
      json failed_participants = json::array();
      bool linked = false;
      size_t peer_index = 0;
      for (const auto& p : participants) {
        if (p.value("connectionId", "") == room->me) continue;
        const bool up = peer_index < room->links.ids.size() && direct_state(room->links.ids[peer_index]) == 2;
        linked |= up;
        if (!up) failed_participants.push_back(p);
        ++peer_index;
      }
      bool repeated;
      { std::lock_guard<std::mutex> lock(g_mutex);
        if (cancelled()) { Match out; out.error = "Cancelled"; return out; }
        if (linked) g_direct_link_failures.successful();
        g_own_udp_blocked = false;   // links relay: this failure is not the player's firewall
        g_last_link_failure = now_ms();
        repeated = g_direct_link_failures.failed(failed_participants, room->me);
      }
      room->socket->send(transport::link_report(false, "links", now_ms() - connect_start, false));
      // The lobby never looks for another opponent (g_lobby_search: no retry), so it never says it does.
      return fail(repeated && !lobby_match ? search_policy::kLinkRetry : search_policy::kLinkFailed,
                  transport::retry_link_failure(search.mode));
    }
  } else if (!punch(udp, my_seat + 1, peers, ingress, cancelled) && !own_udp_relay()) {
    if (cancelled()) return fail("Cancelled");
    // Unranked searches again, as Slippi does when a found opponent cannot be reached. The second failure in
    // a row with the same opponent says so on screen, with what to try (own UDP has no relay to fall back on).
    bool repeated;
    { std::lock_guard<std::mutex> lock(g_mutex);
      if (cancelled()) { Match out; out.error = "Cancelled"; return out; }
      // Only the seats that never answered count, as on the links path.
      json unreached = json::array();
      for (const auto& p : participants) {
        if (p.value("connectionId", "") == room->me) continue;
        for (const auto& peer : peers)
          if (peer.port == p.value("slot", -1) + 1 && !(peer.found && peer.acked)) unreached.push_back(p);
      }
      g_last_link_failure = now_ms();
      repeated = g_direct_link_failures.failed(unreached, room->me);
      if (repeated) g_own_udp_blocked = true;
    }
    room->socket->send(transport::link_report(false, "none", now_ms() - connect_start, true));
    if (repeated) host::log("yougame: own UDP failed twice with this opponent, no room relay: showing the firewall line");
    return fail(repeated && !direct && !lobby_match ? search_policy::kLinkRetry : search_policy::kLinkFailed, !direct);
  }

  if (cancelled()) return fail("Cancelled");
  { std::lock_guard<std::mutex> lock(g_mutex);
    if (!cancelled()) g_direct_link_failures.successful();
    if (!cancelled()) g_own_udp_blocked = false;
    // The match's ENet takes native sockets (slippi_net: own_udp is !yg_enet_selected()) until the next
    // search or the session's end, whatever an identity refresh selects meanwhile.
    if (!cancelled() && links_hello && !links_mode && !relay_match) { g_own_udp_match = true; select_match_transport(g_slippi_match); }
  }
  // The relay reports via links, path relay (rooms keeps any path it does not know as "bad").
  room->socket->send(transport::link_report(true, links_mode ? std::string("links")
      : peers.empty() ? std::string("none") : std::string(answered_via(peers.front(), peers.front().chosen)),
      now_ms() - connect_start, !links_mode, relay_match ? std::string("relay") : links_mode ? transport::links_path(room->links.ids, [](unsigned id) {
        std::lock_guard<std::mutex> lock(g_link_mutex);
        const auto it = g_link_routes.find(id);
        return it == g_link_routes.end() ? std::string() : it->second;
      }) : std::string()));
  host::log("yougame: transport %s", relay_match ? "the YouGame room relay" : links_mode ? "YouGame direct links"
            : links_hello ? "own UDP (a seat has no YouGame links)" : "own UDP");
  Match out;
  out.ok = true;
  out.udp_port = links_mode || relay_match ? 0 : bound_port;
  // The match's ENet takes this very socket (slippi_net: yg_native_adopt_next): the NAT mapping the punch
  // opened stays, and nothing binds the port a second time.
  if (out.udp_port && udp != INVALID_SOCKET) {
    if (const ENetSocket left = yg_native_park((ENetSocket)udp, bound_port); left != ENET_SOCKET_NULL) closesocket((SOCKET)left);
    udp = INVALID_SOCKET;
  }
  out.match_id = match_id.rfind("yg-", 0) == 0 && match_id.size() <= 32 ? match_id : my_match_id;
  out.local_index = my_seat;
  out.host = my_seat == 0;
  room->port_ids = transport::port_participants(participants, size);
  if (lobby_match) {
    // Results by setup port, CPUs as cpu:<seat>; the room's match is the setup's (no begin is sent).
    room->port_ids = lobby_plan->ids;
    room->match_id = lobby_plan->match_id;
    room->lobby = lobby_plan;
  }
  // The start's seats: its result ids are the session's participants, by port.
  if (setup.known) {
    for (int slot = 0; slot < 4 && slot < (int)room->port_ids.size(); ++slot) room->port_ids[slot] = setup.id[slot];
    room->setup = setup;
  }
  for (auto& p : participants) {
    int seat = p.value("slot", -1);
    if (seat < 0 || seat >= size) continue;
    const std::string conn = p.value("connectionId", "");
    Player player;
    player.port = seat + 1;
    player.local = seat == my_seat;
    player.id = p.value("id", "");
    player.name = player.local ? me.name : p.value("name", "Player");
    // In Direct the room admits only the account behind the code we typed: that code is the
    // opponent's. Elsewhere the code is what the opponent sent, shown as theirs.
    player.code = player.local ? me.code : direct && !friends_mode ? search.direct_code : cands[conn].value("code", "");
    for (auto& peer : peers) if (peer.port == player.port) player.address = addr_string(peer.chosen);
    out.players.push_back(player);
  }
  room->recovery.configure(room_code, room->me, lobby_match ? room_roster : participants);
  if (setup.known && room->socket->brokered()) room->recovery.start(room->match_id);
  if (room->ranked) room->set.configure(my_seat, room->port_ids);
  std::sort(out.players.begin(), out.players.end(), [](const Player& a, const Player& b) { return a.port < b.port; });
  std::string names;
  for (auto& p : out.players) if (!p.local) names += (names.empty() ? "" : ", ") + p.name + (p.code.empty() ? "" : " (" + p.code + ")");
  if (friends_mode) { host::log("friends: friend joined"); set_status(friends::kJoined); }
  else set_status("playing " + slippi::display::opponent_label(slippi::display::Provider::YouGame, names));

  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_search_generation.current(epoch) || g_room || cancelled()) {
    out.ok = false;
    out.error = "Cancelled";
    return out;
  }
  Room* raw = room.get();
  g_rank_opponent_rank = {};
  if (raw->ranked) {
    ++g_ranked_rooms;
    g_rank_local = my_seat;
    const auto local = ranked::roster_rank(participants, my_seat);
    if (local.known()) g_rank.update(local);
    const auto opponent = ranked::roster_rank(participants, 1 - my_seat);
    g_rank_opponent = opponent.known() ? opponent.header().substr(9) : "Unknown";
    g_rank_opponent_rank = opponent;
    g_rank_score = raw->set.score_text();
    g_rank_results = {};
    g_rank_read.recovery_at = 0;
    g_rank_result_visible = false;
  }
  if (lobby_route) {
    // The lobby goes on with the room: through the match, and parked on the select after it.
    lobby_leave.handed_off = true;
    raw->lobby_session = std::move(lobby_session);
    raw->lobby_me = lobby_me;
    raw->lobby_control = search.lobby_control;
  } else if (doubles_room) {
    // The Doubles match keeps its setup session (a rejoin's welcome brings the same setup); the screen's
    // control stays with the parked home's pump, which alone runs it.
    raw->lobby_session = std::move(lobby_session);
    raw->lobby_me = lobby_me;
  }
  g_room = std::move(room);
#ifdef YOUGAME_AGENT_TEST_HOOK
  g_pending_input = false;
  g_pending_input_roster = {};
#endif
  friends_setup.keep = true;
  // The match's links now belong to the room. An own UDP match closes any a lobby opened before a seat
  // without links came in (three slots: none may linger).
  early.keep = links_mode;
  raw->pump = std::thread(pump_room, raw);
  return out;
  } catch (const update::Required&) {
    return fail(update::kMessage, false, true);
  }
}

#ifdef YOUGAME_AGENT_TEST_HOOK
agent_test::InputState agent_test::live_input_state(int live_mode) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const uint64_t session = g_search_generation.value() + 1;
  if (!g_room) {
    if (!g_pending_input) return pre_room_input_state(session, live_mode);
    const int mode = live_mode == -1 ? g_pending_input_roster.mode : live_mode;
    return room_input_state(session, mode, g_pending_input_roster.ranked,
                            g_pending_input_roster.participants, g_pending_input_roster.me, true);
  }
  // A room remains online even during a transition in the game's session flags.
  const int mode = live_mode == -1 ? g_room->mode : live_mode;
  if (g_room->stop || g_room->socket->closed())
    return {session, mode, g_room->ranked || ranked_mode(mode)};
  return room_input_state(session, mode, g_room->ranked, g_room->participants, g_room->me);
}
#endif

int seat_team(int player_idx) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_room ? g_room->seat_team(player_idx) : -1;
}
DoublesRoom doubles_room() {
  std::lock_guard<std::mutex> lock(g_mutex);
  DoublesRoom out;
  if (!g_room || g_room->size != 4 || g_room->mode != doubles_teams::kTeamsMode) return out;
  const auto plan = doubles_teams::plan(g_room->setup, g_room->doubles, g_room->code);
  out.open = true; out.refusal = plan.refusal;
  if (plan.refusal) return out;
  out.team = plan.team; out.seed = plan.seed;
  out.consistent = doubles_teams::consistent(plan.seats, g_room->participants);
  return out;
}

std::string friends_lobby_compatibility() { return lobby_room_key(); }

namespace arena_room {
namespace {
ArenaLink& a = g_arena_link;
// The arena's room key: this build's own key (as the Friends lobby's) with ":arena-1" after it.
std::string arena_key() {
  const auto& build = slippi::online::local_build();
  const std::string tag = compat_tag_with_test_timer(compat_tag, std::getenv("MELEE_ONLINE_TEST_TIMER"));
  return lobby::arena_compatibility(room_version(), [&](const std::string& version) {
    return compatibility_key(version, build.mod_view, build.fingerprint, build.static_engine,
                             slippi::online::ranked_rules_digest(1), tag);
  });
}
// Rooms closes a socket that sends more than 120 messages or 256 KB a second (16 KB a message). The sender
// stays well inside that: about every 50 ms each connection with something waiting gets one message, from a
// budget of 60 messages and 160 KB (raw) a second, taken in turn so a busy feed never starves the others.
constexpr double kMsgsPerMs = 0.06, kBytesPerMs = 160.0;
void run_sender() {
  double msgs = 3, bytes = 8000;
  uint64_t last = now_ms();
  size_t turn = 0;
  while (!a.stop) {
    Sleep(50);
    const uint64_t now = now_ms();
    msgs = std::min(6.0, msgs + (double)(now - last) * kMsgsPerMs);
    bytes = std::min(32000.0, bytes + (double)(now - last) * kBytesPerMs);
    last = now;
    std::lock_guard<std::mutex> gate(a.send_gate);
    std::vector<std::pair<std::string, std::string>> batch;
    Socket* socket = nullptr;
    {
      std::lock_guard<std::mutex> lock(a.mutex);
      socket = a.socket;
      if (!socket || a.outbox.empty()) continue;
      std::vector<std::string> keys;
      for (const auto& kv : a.outbox) keys.push_back(kv.first);
      for (size_t k = 0; k < keys.size() && msgs >= 1 && bytes >= 1; ++k) {
        auto it = a.outbox.find(keys[(turn + k) % keys.size()]);
        std::string& q = it->second;
        size_t take = 0;
        while (take + 2 <= q.size()) {
          const size_t n = (size_t)(uint8_t)q[take] << 8 | (uint8_t)q[take + 1];
          if (take + 2 + n > kArenaChunk || (take && (double)(take + 2 + n) > bytes)) break;
          take += 2 + n;
        }
        if (!take) continue;
        batch.emplace_back(it->first, q.substr(0, take));
        q.erase(0, take);
        a.outbox_bytes -= take;
        msgs -= 1;
        bytes -= (double)take;
        if (q.empty()) a.outbox.erase(it);
      }
      ++turn;
    }
    for (const auto& [to, raw] : batch) {
      json m = {{"t", "msg"}, {"d", {{"kind", "mu-arena"}, {"b", room_relay::b64(raw.data(), raw.size())}}}};
      if (!to.empty()) m["to"] = to;
      socket->send_now(m);
    }
  }
}
void open_thread(const std::string& asked, uint64_t generation) {
  const auto stale = [generation] { std::lock_guard<std::mutex> lock(a.mutex); return a.generation != generation; };
  // `why` goes to the log; the player sees `shown` (the arena's own line), or `why` itself when null.
  const auto failed = [&](const std::string& why, const char* shown = friends::kArenaOpenFailed) {
    std::lock_guard<std::mutex> lock(a.mutex);
    if (a.generation != generation) return;
    a.state = -1;
    a.error = shown ? std::string(shown) : why;
    host::log("arena: the room could not open: %.120s", why.c_str());
  };
  try {
    const std::string key = arena_key();
    if (key.empty()) return failed("Could not verify this mod's content. Restart the game before playing online.", nullptr);
    std::string code = asked;
    if (code.empty()) {   // the host's own room code: 10 of [a-z0-9], within the ticket route's 6 to 12
      static const char* kChars = "abcdefghijklmnopqrstuvwxyz0123456789";
      for (int i = 0; i < 10; ++i) code += kChars[random64() % 36];
    }
    const json request = friends::arena_ticket(key, code);
    const json ticket = bridge(request, 30000, stale);
    auto room = std::make_unique<Room>();
    room->arena = true;
    room->size = friends::kArenaSeats;
    room->socket = std::make_unique<Socket>();
    room->url = ticket.value("url", ""); room->ticket = ticket.value("ticket", ""); room->code = code;
    room->ticket_request = request;
    if (ticket.count("room") && ticket["room"].is_string() && ticket["room"].get<std::string>() != code)
      return failed("YouGame returned a different arena room");
    if ((!g_broker && room->url.empty()) || room->ticket.empty()) return failed("YouGame did not return a ticket");
    {
      std::lock_guard<std::mutex> lock(a.mutex);
      if (a.generation != generation) return;
      a.socket = room->socket.get();   // from its first frame: the welcome names this player's connection
      a.code = code;
    }
    std::string error;
    if (!room->socket->open_session(room->url, room->ticket, code, &error, 10000, stale)) {
      if (stale()) return;
      if (room->socket->room_full()) return failed("That arena is full", nullptr);
      return failed(error.empty() ? std::string("Could not join the arena's room") : error);
    }
    bool kept = false;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      std::lock_guard<std::mutex> arena_lock(a.mutex);
      if (a.generation == generation && !g_arena && !g_arena_parking) {
        g_arena_open = true;
        Room* raw = room.get();
        raw->pump = std::thread(pump_room, raw);
        g_arena = std::move(room);
        a.state = 2;
        a.stop = false;
        a.sender = std::thread(run_sender);
        kept = true;
      }
    }
    if (!kept) { shutdown_room(std::move(room)); return; }
    host::log("arena: the room is open (%s)", asked.empty() ? "hosting" : "joined");
  } catch (const update::Required&) {
    failed(g_bridge_update_text.empty() ? std::string(update::kMessage) : g_bridge_update_text, nullptr);
  } catch (const std::exception& e) {
    if (!stale()) failed(e.what(), std::string(e.what()) == kChannelClosed ? nullptr : friends::kArenaOpenFailed);
  }
}
}  // namespace

void open(const std::string& code) {
  close();   // one arena room at a time
  uint64_t generation;
  {
    std::lock_guard<std::mutex> lock(a.mutex);
    generation = ++a.generation;
    a.state = 1; a.error.clear(); a.code = code; a.me.clear(); a.host.clear(); a.roster = json::array();
  }
  std::thread([code, generation] { open_thread(code, generation); }).detach();
}

int state(std::string* error) {
  bool closed_socket = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const Room* r = g_arena ? g_arena.get() : g_room && g_room->arena ? g_room.get() : nullptr;
    closed_socket = r && r->socket && r->socket->closed();
  }
  std::lock_guard<std::mutex> lock(a.mutex);
  // Open, but its socket closed or went away (a drop the room did not take back, a search that lost it).
  if (a.state == 2 && (closed_socket || !a.socket)) {
    a.state = -1;
    a.error = "Lost the connection to the arena's room";
    host::log("arena: lost the room's connection");
  }
  if (error) *error = a.error;
  return a.state;
}

void close() {
  std::thread sender;
  {
    std::lock_guard<std::mutex> lock(a.mutex);
    ++a.generation;
    a.state = 0;
    a.stop = true;
    sender = std::move(a.sender);
    a.inbox.clear(); a.inbox_bytes = 0;
    a.outbox.clear(); a.outbox_bytes = 0;
    a.invite = 0; a.invite_error.clear();
  }
  if (sender.joinable()) sender.join();
  std::unique_ptr<Room> r;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_arena_open = false;   // a game's room still out (in a game, or on its way back) shuts down when it returns
    r = std::move(g_arena);
  }
  {
    std::lock_guard<std::mutex> lock(a.mutex);
    a.socket = nullptr; a.me.clear(); a.host.clear(); a.roster = json::array(); a.code.clear();
  }
  if (r) std::thread([old = std::move(r)]() mutable { shutdown_room(std::move(old)); }).detach();
}

std::string code() { std::lock_guard<std::mutex> lock(a.mutex); return a.code; }
std::string me() { std::lock_guard<std::mutex> lock(a.mutex); return a.me; }
std::string host() { std::lock_guard<std::mutex> lock(a.mutex); return a.host; }

std::vector<Seat> roster() {
  std::lock_guard<std::mutex> lock(a.mutex);
  std::vector<Seat> out;
  for (const auto& p : a.roster) {
    if (!p.is_object() || p.value("localIndex", 0) != 0) continue;
    out.push_back({ranked::str(p, "id"), ranked::str(p, "name"), ranked::str(p, "connectionId")});
  }
  return out;
}

void send(const std::string& conn, const std::vector<uint8_t>& bytes) {
  if (bytes.empty()) return;
  std::lock_guard<std::mutex> lock(a.mutex);
  if (a.state != 2) return;
  if (bytes.size() + 2 > kArenaChunk) { host::log("arena: a %zu-byte room message is too big to send", bytes.size()); return; }
  if (a.outbox_bytes + bytes.size() + 2 > kArenaQueueMax) {
    static uint64_t logged = 0;
    if (now_ms() - logged > 5000) { logged = now_ms(); host::log("arena: the room's sends are behind; dropping"); }
    return;
  }
  std::string& q = a.outbox[conn];
  q.push_back((char)(bytes.size() >> 8));
  q.push_back((char)(bytes.size() & 0xFF));
  q.append((const char*)bytes.data(), bytes.size());
  a.outbox_bytes += bytes.size() + 2;
}

bool pop(std::string* from, std::vector<uint8_t>* bytes) {
  std::lock_guard<std::mutex> lock(a.mutex);
  if (a.inbox.empty()) return false;
  *from = std::move(a.inbox.front().first);
  *bytes = std::move(a.inbox.front().second);
  a.inbox_bytes -= bytes->size();
  a.inbox.pop_front();
  return true;
}

void invite() {
  std::string code;
  uint64_t generation;
  {
    std::lock_guard<std::mutex> lock(a.mutex);
    if (a.state != 2 || a.inviting || a.code.empty()) return;
    a.inviting = true; a.invite = 1; a.invite_error.clear();
    code = a.code;
    generation = a.generation;
  }
  std::thread([code, generation] {
    const auto stale = [generation] { std::lock_guard<std::mutex> lock(a.mutex); return a.generation != generation; };
    int result = 2;
    std::string why;
    // The several-friends picker first; an app whose picker takes only 4 seats refuses it for the arena's 8, and
    // then the one-friend picker (no multi) is asked once, which takes any room size.
    for (int attempt = 0; attempt < 2; ++attempt) {
      host::log(attempt ? "arena: invite picker requested (one friend)" : "arena: invite picker requested");
      try {
        bridge(friends::arena_invite(code, attempt == 0), friends::kPickerTimeout, stale);
        host::log("arena: invites sent");
        result = 2; why.clear();
        break;
      } catch (const update::Required&) {
        result = -1; why = update::kMessage;
        break;
      } catch (const std::exception& e) {
        const std::string what = e.what();
        const auto lower = [](std::string v) { for (auto& c : v) c = (char)std::tolower((unsigned char)c); return v; };
        if (stale() || lower(what).find("cancel") != std::string::npos || lower(g_bridge_refusal).find("cancel") != std::string::npos) {
          host::log("arena: invite cancelled");
          result = 0; why.clear();
          break;
        }
        const auto full = friends::invite_room_full(g_bridge_refusal, what);
        host::log("arena: invite failed: %.120s", (full.empty() ? what : full).c_str());
        // The panel shows the site's words for a full room, or YouGame not open; else the arena's own line.
        result = -1; why = !full.empty() ? full : what == kChannelClosed ? what : std::string(friends::kArenaInviteFailed);
        if (!full.empty()) break;
      }
    }
    std::lock_guard<std::mutex> lock(a.mutex);
    if (a.generation != generation) return;
    a.inviting = false; a.invite = result; a.invite_error = why;
  }).detach();
}

int invite_state(std::string* error) {
  std::lock_guard<std::mutex> lock(a.mutex);
  if (error) *error = a.invite_error;
  return a.invite;
}
}  // namespace arena_room

FriendsLobby friends_lobby() {
  std::lock_guard<std::mutex> lock(g_mutex);
  FriendsLobby out;
  if (!g_room || !g_room->lobby) return out;
  out.open = true;
  out.plan = g_room->lobby;
  return out;
}

void lobby_refuse_setup() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_room && g_room->lobby_session) g_room->lobby_session->refuse_setup();
}

void game_started(int64_t rng, int stage) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_room) return;
  // Frame 1 is retried while waiting for peers; emit once per game, including later games of a set.
  if (g_room->ranked && !g_room->in_game)
    host::log("%s", lcancel::policy::ranked_start_log(lcancel::automatic_enabled()));
  // A Doubles match room plays one match: a game after its result belongs to no match (Rooms closes it).
  if (g_room->setup.known && g_room->settled) return;
  g_room->in_game = true;
  if (g_room->arena) return;   // unrated: no begin (the arena counts its own games)
  // The Friends lobby's match began: from here a state without the setup no longer means it fell through.
  if (g_room->lobby_session) g_room->lobby_session->match_began();
  // Called on every frame 1 the game sends while it waits for the opponent: the set counts a game once.
  if (g_room->ranked) { g_room->set.start_game(ranked::game_key(rng, stage)); return; }
  if (g_room->socket->brokered()) { g_room->games.start_game(g_room->match_id); return; }
  // Only the room's host may start a match; everyone reports its result.
  g_room->pending_begin = g_room->me == g_room->host_connection;
  g_room->begin_at = 0;
  // A result still waiting from a game the room never started belongs to no match: drop it, or it
  // would be filed under this game's match.
  g_room->pending_winner = -2;
  g_room->pending_team_result = nullptr;
}

void game_ended(const GameEnd& end) {
  online_match_state(false); // End presence now, even when a ranked result waits for its report.
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_room || !g_room->in_game) return;   // one report per game, however many places say it ended
  if (ranked::no_contest_waits_for_report(end.end_method, end.has_placements, g_room->ranked)) {
    host::log("yougame: ranked No Contest: waiting for finalized desync/disconnect report");
    return;
  }
  if (ranked::time_out_waits_for_report(end.end_method, end.has_placements, g_room->size)) {
    host::log("yougame: time-out: the game's report names the winner");
    return;
  }
  g_room->in_game = false;
  if (g_room->arena) return;   // unrated: no result is reported
  if (g_room->ranked) {
    // Only the finalized report can distinguish a replay from a disconnect forfeit.
    ranked::Game game = ranked::game_outcome(end.end_method, end.lras, end.winner,
                                            end.has_placements ? end.placements : nullptr);
    game.key = ranked::game_key(end.rng, end.stage);   // an id both players' games share
    game.index = (uint32_t)g_room->set.games_started();
    game.detail = ranked::game_detail(end.end_method, end.lras, end.stage, end.characters, end.frames);
    g_room->set.report_game(game);
    host::log("yougame: ranked game %u (%s): %s%s", game.index, game.key.c_str(),
              game.winner >= 0 ? ("port " + std::to_string(game.winner + 1) + " won").c_str() : "no winner",
              game.forfeit ? "; a No Contest: the set goes to the other player" : "");
    return;
  }
  if (!g_room->socket->brokered()) g_room->pending_begin = false;
  if (g_room->lobby) {
    // A Friends lobby match: the full ranking by setup id, CPUs included, a player the netcode dropped
    // (or who quit) last, never left out; the frozen setup echoed (Rooms compares it).
    lobby::Outcome o;
    o.has_placements = end.has_placements;
    for (int i = 0; i < 4; ++i) o.placements[i] = end.placements[i];
    o.winner = end.winner; o.end_method = end.end_method; o.lras = end.lras; o.gone = end.gone;
    json result = lobby::finish_report(*g_room->lobby, o);
    const auto ranking = result.find("ranking");   // never result["ranking"]: that would add a null one
    host::log("yougame: friends lobby result: %s, %zu ranked",
              result.value("void", false) ? "void" : result.value("draw", false) ? "draw" : "complete",
              ranking != result.end() ? ranking->size() : (size_t)0);
    if (g_room->socket->brokered()) g_room->games.report_team_result(std::move(result));
    else g_room->pending_team_result = std::move(result);
    return;
  }
  if (g_room->size == 4) {
    int teams[4];
    const int* room_teams = g_room->result_teams();
    transport::outcome_teams(end.teams, room_teams, g_room->ranked, teams);
    const int winner = ranked::team_winner(end.end_method, end.lras,
                                           end.has_placements ? end.placements : nullptr, teams);
    const json result = transport::room_team_outcome(winner, g_room->port_ids, teams,
                                                     room_teams, g_room->ranked);
    if (g_room->socket->brokered()) g_room->games.report_team_result(result);
    else g_room->pending_team_result = result;
    return;
  }
  const int winner = ranked::casual_winner(end.end_method, end.lras, end.winner,
                                          end.has_placements ? end.placements : nullptr, g_room->size);
  if (g_room->socket->brokered()) g_room->games.report(winner);
  else g_room->pending_winner = winner;
}

void ranked_set_complete(uint8_t end_mode) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_room && g_room->ranked) g_room->set.complete(end_mode);
}

void ranked_match_status(uint8_t status) {
  std::lock_guard<std::mutex> lock(g_mutex);
  // Slippi's status 40 explicitly means this client abandoned the set. Setup and start
  // statuses are telemetry; neither changes the game's rules or completes a set.
  if (g_room && g_room->ranked && status == 40) g_room->set.quit();
}

void ranked_game_setup() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_room && g_room->ranked) g_room->set.game_setup();
}

bool ranked_hold() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_room && g_room->ranked && (g_room->set.awaiting_receipt() || g_room->set.over());
}

bool ranked_set_over() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_room && g_room->ranked && g_room->set.release_pair();
}

int ranked_games_received() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_room && g_room->ranked ? g_room->set.games_received() : 0;
}

bool ranked_room_retired() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_ranked_rooms > 0 && !g_room &&
         (!g_retired.valid() || g_retired.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
}

bool own_room_dropped() {
  std::lock_guard<std::mutex> lock(g_mutex);
  // Only a CLOSED connection with 1006 is our own drop: an open one reads 1006 too, and counting it
  // turned every Unranked opponent's mid-match quit into "we dropped" (no Opponent left note).
  return g_room && g_room->socket &&
         g_room->socket->connection_closed() &&
         (g_room->socket->own_dropped() || g_room->socket->recoverable());
}

void ranked_screen_enter() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_rank_read.enter(g_room && g_room->ranked && g_room->set.started());
}

std::string ranked_header() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_slippi_match) return g_rank.header();
  const auto now = now_ms();
  const bool live_set = g_room && g_room->ranked && g_room->set.started();
  const bool recovery = !live_set && g_rank_results.state() == ranked::ResultsPanel::State::Pending && g_rank_read.recovery_due(now);
  if (g_identity.ok && !live_set && (recovery || !g_rank_read.recovery_at) && g_rank_read.begin(now)) {
    const uint64_t asked = g_rank.version();
    const auto match = g_rank_results.match();
    std::thread([asked, recovery, match] {
      json result;
      bool failed = false, unsupported = false;
      try {
        result = bridge({{"type", "mpRank"}, {"mode", "singles"}}, 10000);
      } catch (const std::exception& e) {
        failed = true;
        unsupported = ranked::unknown_request(e.what());
        // Bridge errors may contain untrusted text. The throttle applies to 429 and all failures.
        host::log("yougame: mpRank unavailable; keeping cached standing and backing off");
      }
      std::lock_guard<std::mutex> lock(g_mutex);
      const bool applied = g_rank_read.answer(g_rank, result, asked, now_ms(), failed, unsupported);
      if (recovery) {
        ranked::RankCache unknown;
        g_rank_results.recovered_standing(match, applied ? g_rank : unknown, g_rank_read.recovery_tries >= 2 || unsupported);
        if (g_rank_results.match() == match && g_rank_results.state() != ranked::ResultsPanel::State::Pending)
          g_rank_read.recovery_at = 0;
      }
    }).detach();
  }
  return g_rank.header();
}

std::array<std::string, 6> ranked_lines() {
  const auto header = ranked_header();
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_room && g_room->ranked) {
    g_rank_score = g_room->set.score_text();
    if (g_room->set.over()) g_rank_result_visible = true;
  }
  return {header, g_rank_opponent.empty() ? "Opponent: Unknown" : "Opponent: " + g_rank_opponent,
          // Line 3 stays empty: the splash that read it follows the setup screen, so "Stage
          // striking comes next" was always wrong there (build 25, 11-01).
          g_rank_score, "",
          g_rank_result_visible ? g_rank_results.rating_text() : "",
          g_rank_result_visible ? g_rank_results.change_text() : ""};
}
uint8_t opponent_rank() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_room && g_room->ranked) return ranked::roster_rank_byte(g_room->participants, 1 - g_rank_local);
  return g_rank_opponent_rank.known() ? g_rank_opponent_rank.rank() : 0xFF;
}

std::vector<uint8_t> rank_record_response() {
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto opponent = g_room && g_room->ranked ? ranked::roster_rank(g_room->participants, 1 - g_rank_local) : g_rank_opponent_rank;
  return ranked::rank_record_response(g_rank.record(), opponent.record(), g_rank_results.record());
}

std::vector<uint8_t> rank_response() {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto reply = g_rank.response();
  const auto status = g_rank_results.record().status;
  if (status == MU_YG_RESULT_RECORDING) reply[1] = 0;
  if (status == MU_YG_RESULT_ERROR) reply[1] = 2;
  return reply;
}
uint8_t rank_visibility() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_rank.visibility();
}
uint8_t local_rank() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_rank.rank();
}

void end_session(Leave why) {
  online_match_state(false);
  close_room(why);
  { std::lock_guard<std::mutex> lock(g_mutex); g_own_udp_match = false; g_relay_own = false; select_match_transport(false); }
  set_status("offline");
}

}  // namespace yougame
