// YouGame online (see yougame_link.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "test_env.h"
#include "yougame_link.h"
#include "yougame_parity.h"
#include "yougame_relay_pads.h"
#include "yougame_ux.h"
#include "yougame_friends.h"
#include "yougame_compatibility.h"
#include "slippi_online.h"
#include "yougame_exit_reason.h"
#include "yougame_ranked.h"
#include "yougame_room.h"
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_static.h"
#endif
#include <enet/yg_enet_socket.h>
#include "host.h"
#include "lcancel.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <xinput.h>
#pragma comment(lib, "winhttp.lib")
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
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

using json = nlohmann::json;

namespace yougame {
namespace {

#ifdef _WIN32
using SocketLength = int;
#else
using SocketLength = socklen_t;
#endif

uint64_t now_ms() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
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
relay_pads::State g_relay_pads([](const std::string& line) { host::log("%s", line.c_str()); }, false);
std::atomic<uintptr_t> g_pad_mirror{0};
#ifndef _WIN32
std::mutex g_pad_mirror_mutex;
pad_mirror::Attachment g_pad_mirror_attachment;
#endif
transport::Events g_net_events;
std::atomic<bool> g_broker{false}, g_direct{false};
bool g_launcher = false;                        // MELEE_YOUGAME_LAUNCHER=1: Melee Unlocked's launcher serves the bridge
std::atomic<int> g_online_mode_arg{0};          // --online-mode: 1 ranked, 2 direct, taken once
std::atomic<bool> g_launcher_session{false};    // under the launcher: the online menu's last pick was a YouGame row
std::mutex g_link_mutex;
std::map<unsigned, int> g_link_events;
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

void handle_app_line(const std::string& line) {
  if (line.rfind("yg-pads ", 0) == 0) {
    g_relay_pads.accept(line, now_ms());
    return;
  }
  if (line.rfind("yg ", 0) == 0) {
    json j = json::parse(line.substr(3), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;
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
      if (g_stdio) ux::page_pause.push(push);
      friends::events.push(push, now_ms());
      if (push.value("type", "") == "net") g_net_events.push(push);
      if (push.value("type", "") == "direct") {
        const unsigned link = push.value("link", 0u);
        const auto event = ranked::str(push, "event");
        std::lock_guard<std::mutex> lock(g_link_mutex);
        if (link && link <= 65534) g_link_events[link] = event == "up" ? 2 : event == "close" ? 3 : 0;
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
  while (in && in != INVALID_HANDLE_VALUE && ReadFile(in, buf, sizeof buf, &got, nullptr) && got) {
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
  host::log("yougame: the YouGame app closed the bridge; quitting");
  host::request_exit(0);
}

json bridge_stdio(const json& body, int timeout_ms, const std::function<bool()>& cancelled = {}) {
  if (g_io.eof) throw std::runtime_error("The YouGame app is not answering");
  int64_t id;
  {
    std::lock_guard<std::mutex> lock(g_io.mutex);
    id = g_io.next_id++;
    g_io.pending_requests.insert(id);
  }
  json request = body;
  request["id"] = id;
  write_line("yg " + request.dump());
  std::unique_lock<std::mutex> lock(g_io.mutex);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (!g_io.answers.count(id) && !g_io.eof) {
    if (cancelled && cancelled()) {
      g_io.pending_requests.erase(id);
      throw std::runtime_error("Cancelled");
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      g_io.pending_requests.erase(id);
      throw std::runtime_error("The YouGame app did not answer");
    }
    g_io.cv.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(50)));
  }
  g_io.pending_requests.erase(id);
  if (!g_io.answers.count(id)) throw std::runtime_error("The YouGame app is not answering");
  json answer = std::move(g_io.answers[id]);
  g_io.answers.erase(id);
  return answer;
}

// POST <bridge>/sdk {type, ...} -> {ok, result | error}. Only the app's loopback address is
// accepted: the token must never travel anywhere else.
json bridge_http(const json& body, int timeout_ms) {
  if (g_bridge_url.empty() || g_bridge_token.empty()) throw std::runtime_error(g_launcher ? "Start Melee Unlocked from its launcher to play YouGame modes" : "Start Melee from the YouGame app to play online");
  Url u;
  if (!parse_url(g_bridge_url + "/sdk", u) || u.secure || u.host != L"127.0.0.1") throw std::runtime_error(g_launcher ? "The launcher's address is invalid" : "The YouGame app's address is invalid");
  HINTERNET session = http_session();
  if (!session) throw std::runtime_error("Networking is unavailable");
  HINTERNET conn = WinHttpConnect(session, u.host.c_str(), u.port, 0);
  if (!conn) throw std::runtime_error(g_launcher ? "The Melee Unlocked launcher is not answering. Keep it open while you play YouGame modes." : "The YouGame app is not answering");
  HINTERNET req = WinHttpOpenRequest(conn, L"POST", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
  std::string result;
  bool sent = false;
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
    WinHttpCloseHandle(req);
  }
  WinHttpCloseHandle(conn);
  if (!sent) throw std::runtime_error(g_launcher ? "The Melee Unlocked launcher is not answering. Keep it open while you play YouGame modes." : "The YouGame app is not answering");
  json j = json::parse(result, nullptr, false);
  if (j.is_discarded() || !j.is_object()) throw std::runtime_error(g_launcher ? "The launcher sent an invalid answer" : "The YouGame app sent an invalid answer");
  return j;
}

json bridge(const json& body, int timeout_ms = 15000, const std::function<bool()>& cancelled = {}) {
  json j = g_stdio ? bridge_stdio(body, timeout_ms, cancelled) : bridge_http(body, timeout_ms);
  if (!j.value("ok", false)) {
    std::string error = j.count("error") && j["error"].is_string() ? j["error"].get<std::string>() : "YouGame request failed";
    throw std::runtime_error(body.value("ranked", false) ? ranked::ticket_error(j) : error);
  }
  return j.count("result") ? j["result"] : json::object();
}

// ------------------------------------------------------------------ WebSocket (WinHTTP)
// A reader thread queues each JSON message; next() waits on the queue, so the caller can poll its
// cancel flag. Closing the handle from another thread ends the blocking receive.
class Socket {
 public:
  ~Socket() { close(); }
  bool open_session(const std::string& url, const std::string& ticket, const std::string& room, std::string* error, int timeout = 10000) {
    if (!g_broker) return open(url + (room.empty() ? "/match" : "/room/" + url_encode(room)) + "?ticket=" + url_encode(ticket), error);
    if (!broker_) broker_ = std::make_unique<transport::Stream>(g_net_events, [](const json& q, int ms) { return bridge(q, ms); });
    try {
      if (broker_->open(ticket, room, timeout)) return true;
    } catch (...) {}
    *error = "Lost the connection to YouGame";
    return false;
  }
  int connection() const { return broker_ ? broker_->conn() : 0; }
  bool recoverable() const { return broker_ && broker_->close_code() == 1006; }
  bool brokered() const { return broker_ != nullptr; }
#ifdef YOUGAME_AGENT_TEST_HOOK
  uint64_t agent_test_first_open_at() const { return broker_ ? broker_->agent_test_first_open_at() : 0; }
  void agent_test_drop_room() { if (broker_) broker_->agent_test_drop_room(); }
#endif
  bool open(const std::string& address, std::string* error) {
    Url u;
    if (!parse_url(address, u) || (!u.secure && u.host != L"127.0.0.1" && u.host != L"localhost")) {
      *error = "Invalid YouGame rooms address";
      return false;
    }
    HINTERNET conn = WinHttpConnect(http_session(), u.host.c_str(), u.port, 0);
    if (!conn) { *error = "Could not reach YouGame"; return false; }
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       u.secure ? WINHTTP_FLAG_SECURE : 0);
    DWORD status = 0, size = sizeof status;
    // A room can be quiet for minutes (a friend who has not joined yet, a long game): the socket
    // must not time out on receive; it is kept alive by pings instead.
    DWORD forever = 0;
    if (req) WinHttpSetOption(req, WINHTTP_OPTION_RECEIVE_TIMEOUT, &forever, sizeof forever);
    bool ok = req && WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
              WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr) &&
              WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                  WINHTTP_NO_HEADER_INDEX) &&
              status == 101;
    if (ok) ws_ = WinHttpWebSocketCompleteUpgrade(req, 0);
    if (ws_) WinHttpSetOption(ws_, WINHTTP_OPTION_RECEIVE_TIMEOUT, &forever, sizeof forever);
    if (req) WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    if (!ws_) {
      *error = status == 403 ? "YouGame refused the ticket" : status ? "YouGame refused the connection (HTTP " + std::to_string(status) + ")"
                                                                  : "Could not connect to YouGame";
      return false;
    }
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
  // Up to timeout_ms for the next message; false on timeout or once the socket has closed and drained.
  bool next(json& out, int timeout_ms) {
    if (broker_) return broker_->next(out, timeout_ms);
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return !queue_.empty() || closed_; });
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }
  bool closed() const { return broker_ ? broker_->closed() : closed_.load(); }
  void close() {
    if (broker_) { broker_->close(1000, 4000); return; }
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
  void read_loop() {
    std::string data;
    std::vector<char> buf(16384);
    for (;;) {
      HINTERNET ws;
      { std::lock_guard<std::mutex> lock(send_mutex_); ws = ws_; }
      if (!ws) break;
      DWORD got = 0;
      WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
      if (WinHttpWebSocketReceive(ws, buf.data(), (DWORD)buf.size(), &got, &type) != NO_ERROR) break;
      if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
      if (type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE && type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;
      data.append(buf.data(), got);
      if (data.size() > (1u << 20)) break;
      if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;
      json j = json::parse(data, nullptr, false);
      data.clear();
      if (j.is_discarded() || !j.is_object()) continue;
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push_back(std::move(j));
      cv_.notify_all();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    cv_.notify_all();
  }
  std::unique_ptr<transport::Stream> broker_;
  HINTERNET ws_ = nullptr;
  std::thread reader_;
  std::mutex mutex_, send_mutex_;
  std::condition_variable cv_;
  std::deque<json> queue_;
  std::atomic<bool> closed_{false};
};

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
  int port = std::atoi(s.c_str() + colon + 1);
  if (port <= 0 || port > 65535 || inet_pton(AF_INET, s.substr(0, colon).c_str(), &out.sin_addr) != 1) return false;
  out.sin_port = htons((u_short)port);
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
      sendto(s, (const char*)req, sizeof req, 0, (sockaddr*)&to, sizeof to);
      uint64_t until = now_ms() + 500;
      while (now_ms() < until) {
        if (!wait_readable(s, (int)(until - now_ms()))) break;
        uint8_t buf[512];
        sockaddr_in from{};
        SocketLength fl = sizeof from;
        int n = recvfrom(s, (char*)buf, sizeof buf, 0, (sockaddr*)&from, &fl);
        if (n < 20 || buf[0] != 0x01 || buf[1] != 0x01 || std::memcmp(buf + 4, req + 4, 16) != 0) continue;
        for (int pos = 20; pos + 4 <= n;) {
          int type = (buf[pos] << 8) | buf[pos + 1], len = (buf[pos + 2] << 8) | buf[pos + 3];
          const uint8_t* v = buf + pos + 4;
          if (pos + 4 + len > n) break;
          if ((type == 0x0020 || type == 0x0001) && len >= 8 && v[1] == 0x01) {
            uint16_t port = (uint16_t)((v[2] << 8) | v[3]);
            uint8_t ip[4] = {v[4], v[5], v[6], v[7]};
            if (type == 0x0020) {
              port ^= 0x2112;
              for (int i = 0; i < 4; ++i) ip[i] ^= req[4 + i];
            }
            out = std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[2]) + "." + std::to_string(ip[3]) + ":" +
                  std::to_string(port);
            return true;
          }
          pos += 4 + ((len + 3) & ~3);
        }
      }
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
  bool found = false, acked = false;
  sockaddr_in chosen{};
};
void send_probe(SOCKET s, const sockaddr_in& to, uint8_t type, uint64_t token, int from_port) {
  uint8_t p[14] = {'Y', 'G', 'P', '2', type};
  std::memcpy(p + 5, &token, 8);
  p[13] = (uint8_t)from_port;
  sendto(s, (const char*)p, sizeof p, 0, (const sockaddr*)&to, sizeof to);
}
bool punch(SOCKET s, int my_port, std::vector<PunchPeer>& peers, const std::function<bool()>& cancelled) {
  const uint64_t start = now_ms(), deadline = start + 8000;
  uint64_t next_send = 0, done_at = 0;
  for (;;) {
    const uint64_t now = now_ms();
    bool all = std::all_of(peers.begin(), peers.end(), [](const PunchPeer& p) { return p.found && p.acked; });
    if (all && !done_at) done_at = now;
    // Keep answering for a moment after we are done: the other side may still be waiting for our ack.
    if (done_at && now - done_at > 600) return true;
    if (now > deadline || cancelled()) return false;
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
    if (n != 14 || std::memcmp(buf, "YGP2", 4) != 0) continue;
    uint64_t token;
    std::memcpy(&token, buf + 5, 8);
    for (auto& p : peers) {
      if (p.port != buf[13] || token != p.theirs) continue;
      if (!p.found) {
        p.found = true;
        p.chosen = from;
        host::log("yougame: player %d answered after %llu ms", p.port, (unsigned long long)(now - start));
      }
      if (buf[4] == kProbe) send_probe(s, from, kAck, p.mine, my_port);
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

// The room of the current online session. Stays open through the match for results.
struct Room : transport::RoomState<Socket> {
  ~Room() {
    // Also runs on cancelled/failed setup: no abandoned links occupy the three slots.
    if (!links.ids.empty()) yg_enet_enable(0);
    transport::close_links(links, [](const json& q, int ms) { return bridge(q, ms); });
  }
};
std::unique_ptr<Room> g_room;
uint64_t g_search_epoch = 0;
std::shared_future<void> g_retired; // a new search waits until the previous room has left
int g_ranked_rooms = 0;             // ranked rooms this process has played in
ranked::RankCache g_rank; // the newest ranked standing (ticket, mpRank or a set's result) for this process
// The bridge's mpRank read (docs/MELEE_UNLOCKED.md): asked when the Ranked header has no rank yet,
// and again a while after a ranked room closed without its result reaching this game.
struct RankRead {
  bool unsupported = false, in_flight = false;
  uint64_t next_at = 0;    // no ask before this (a failure or an empty answer waits)
  uint64_t stale_at = 0;   // ask again at this time (0: not stale)
};
RankRead g_rank_read;
constexpr uint64_t kDrainMs = 30000;   // a ranked room stays open this long at most for its result

void set_status(const std::string& s) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_status != s) host::log("yougame: %s", s.c_str());
  g_status = s;
}

using transport::read_roster;

transport::RoomIO room_io(Room& r) {
  transport::RoomIO io;
  io.request = [](const json& q, int ms) { return bridge(q, ms); };
  io.link_state = direct_state;
  io.disable_links = [] { yg_enet_enable(0); };
  io.log = [](const char* message) { host::log("%s", message); };
  io.result = [&r](const json& m) {
    const bool won = m.count("ranking") && m["ranking"].is_array() && !m["ranking"].empty() &&
                     m["ranking"][0] == r.set.local_participant();
    const bool none = m.count("void") && m["void"].is_boolean() && m["void"].get<bool>();
    host::log("yougame: ranked set result: %s, %s", ranked::str(m, "disposition").c_str(),
              none ? "no winner" : won ? "this player won" : "the opponent won");
    if (g_rank.result(m, r.set.local_participant())) host::log("yougame: ranked standing: %s", g_rank.header().c_str());
  };
  return io;
}
void end_links(Room& r) { transport::end_links(r, room_io(r)); }
void pump_room(Room* r) { transport::pump_room(r, g_mutex, room_io(*r)); }

void shutdown_room(std::unique_ptr<Room> r) {
  if (!r) return;
  r->stop = true;
  if (r->pump.joinable()) r->pump.join();
  end_links(*r);
  if (r->socket) {
    r->socket->send({{"t", "leave"}});
    r->socket->close();
  }
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
    ++g_search_epoch;
    friends::events.current_room({});
    r = std::move(g_room);
    if (!r) {
#ifdef YOUGAME_AGENT_TEST_HOOK
      if (agent_test::static_quit_requested.load()) host::request_exit(0);
#endif
      return g_search_epoch;
    }
#ifdef YOUGAME_AGENT_TEST_HOOK
    const bool agent_test_quit = !r->ranked && agent_test::static_quit_requested.load();
#endif
    bool at_once = true;
    if (r->ranked) {
      if (why == Leave::Quit) r->set.quit();
      else if (why == Leave::Lost) r->set.lost();
      at_once = r->set.leaves_at_once() || !r->set.needs_drain();
      if (r->set.leaves_at_once()) host::log("yougame: ranked set concession: the opponent won");
      host::log("yougame: leaving the ranked room (%s)%s", why == Leave::Quit ? "quit" : why == Leave::Lost ? "connection lost" : "set over",
                at_once ? "" : "; staying for its result");
      // Its result may not reach this game (left at once, or the room went first): read the
      // standing from the app again once the room has had time to record it.
      if (!r->set.settled()) g_rank_read.stale_at = now_ms() + 20000;
    }
    auto done = std::make_shared<std::promise<void>>();
    g_retired = done->get_future().share();
    std::thread([r = std::move(r), done, at_once
#ifdef YOUGAME_AGENT_TEST_HOOK
                 , agent_test_quit
#endif
                ]() mutable {
      // The game thread is already back in its menus. Drain this exact room, with its frozen
      // participant ids, before leaving. A replacement search waits on g_retired below.
      if (r->ranked && at_once) {
        transport::stop_and_flush(*r, g_mutex);
      } else if (r->ranked) {
        const uint64_t until = now_ms() + kDrainMs;
        for (;;) {
          bool pending;
          { std::lock_guard<std::mutex> lock(g_mutex); pending = r->set.needs_drain(); }
          if (!pending || now_ms() >= until) break;
          Sleep(50);
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        if (r->set.needs_drain()) host::log("yougame: ranked room left before its result");
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
      shutdown_room(std::move(r));
      done->set_value();
#ifdef YOUGAME_AGENT_TEST_HOOK
      if (agent_test_quit) host::request_exit(0);
#endif
    }).detach();
    return g_search_epoch;
  }
}

// What two builds must share to play: the netplay protocol and this build's game. The rooms Worker
// only matches players with the same key (lobby compatibility).
std::string compatibility(int mode) {
  const std::string v = yougame::build_version();
  const auto& build = slippi::online::local_build();
  return compatibility_key(v, build.mod_view, build.fingerprint, build.static_engine,
                           slippi::online::ranked_rules_digest(mode));
}

Match fail(const std::string& why, bool retry = false) {
  Match m;
  m.error = why;
  m.retry = retry;
  set_status("error: " + why);
  return m;
}

std::atomic<bool> g_identity_fetching{false};

Identity fetch_identity() {
  static std::once_flag said;
  std::call_once(said, [] { host::log("yougame: YouGame mode (%s%s)", g_stdio ? "stdio bridge" : "loopback bridge", g_embedded ? ", in the player box" : ""); });
  Identity id;
  try {
    json hello = bridge({{"type", "hello"}});
    friends::events.hello(hello);
#ifdef _WIN32
    const auto mode = transport::Mode::hello(hello);
    g_broker = g_stdio && mode.broker;
    g_direct = g_stdio && mode.direct;
    yg_enet_select(g_direct);
    if (g_stdio && mode.unsupported) throw std::runtime_error("This YouGame direct-link version is not supported");
#else
    const auto route = transport::posix_datagram_route(hello);
    if (route != transport::DatagramRoute::Standalone) g_broker = true;
    g_direct = g_stdio && route == transport::DatagramRoute::Ring;
    yg_enet_select(g_broker); // refuse mode also prevents ENet allocating native sockets
    if (g_broker && !g_direct) throw std::runtime_error("YouGame broker requires direct links v1 (1200-byte datagrams)");
#endif
    const json& p = hello.count("player") && hello["player"].is_object() ? hello["player"] : json::object();
    id.player_id = p.value("id", "");
    id.name = p.value("name", "");
    try {
      json code = bridge({{"type", "mpCode"}});
      id.code = code.value("code", "");
    } catch (const std::exception& e) {
      host::log("yougame: no connect code: %s", e.what());
    }
    id.ok = !id.player_id.empty();
    if (!id.ok) id.error = "Sign in to YouGame to play online";
    // Until the player shares their name with this game the SDK calls them "Player"; the tag of
    // their connect code is their own (made from their YouGame name) and reads better online.
    if (id.ok && !hello.value("nameShared", false) && !id.code.empty()) id.name = id.code.substr(0, id.code.find('#'));
  } catch (const std::exception& e) {
    id.error = e.what();
  }
  if (id.ok) host::log("yougame: signed in as %s (%s)", id.name.c_str(), id.code.empty() ? "no code" : id.code.c_str());
  else host::log("yougame: not signed in: %s", id.error.c_str());
  std::lock_guard<std::mutex> lock(g_mutex);
  g_identity = id;
  g_identity_asked = true;
  g_identity_at = GetTickCount64();
  return id;
}

}  // namespace

namespace relay_pads {
void logging_ready() {
  g_relay_pads.logging_ready();
  static std::once_flag once;
  std::call_once(once, [] { std::atexit([] { g_relay_pads.flush(); host::log_flush(); }); });
}
bool active() { return g_relay_pads.active(); }
bool read(unsigned slot, _XINPUT_STATE* state) { return g_relay_pads.read(slot, state); }
bool poll(unsigned slot, _XINPUT_STATE* state, const std::function<bool()>& native) {
  return g_relay_pads.poll(slot, state, native);
}
void rumble(const std::function<void()>& native) { g_relay_pads.rumble(native); }
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
  g_bridge_url = take("YOUGAME_BRIDGE");
  g_bridge_token = take("YOUGAME_TOKEN");
  g_stdio = take("YOUGAME_STDIO") == "1";
  g_launcher = take("MELEE_YOUGAME_LAUNCHER") == "1" && !g_stdio;
  host::test_env::yougame_session = active();
#ifdef MELEE_LINUX_X11_EMBED
  // The shared X11 helper checks this non-secret flag at window attachment.
  const char* embed = std::getenv("YOUGAME_EMBED");
  g_embedded = embed && std::strcmp(embed, "1") == 0;
#else
  g_embedded = take("YOUGAME_EMBED") == "1";
#endif
  if (g_stdio) {
    std::thread(read_stdin).detach();
    parity::signals.start(true, [](const char* line) { write_line(line); });
  }
  // Nothing is logged here: this runs before the log file is open, and an early line would send the
  // whole log to stdout, which in stdio mode is the bridge. fetch_identity says which mode this is.
}

bool active() { return g_stdio || (!g_launcher && !g_bridge_url.empty() && !g_bridge_token.empty()); }
bool launcher() { return !coming_soon() && g_launcher && !g_bridge_url.empty() && !g_bridge_token.empty(); }
bool coming_soon() {
#ifdef MELEE_YOUGAME_COMING_SOON
  static const bool soon = !host::test_env::getenv("MELEE_TEST_YOUGAME_OPEN");
  return soon;
#else
  return false;
#endif
}
bool available() { return active() || launcher(); }
bool session() { return active() || (launcher() && g_launcher_session.load()); }
std::string build_version() {
#ifdef MELEE_PORT_VERSION
  return MELEE_PORT_VERSION;
#else
  return "dev";
#endif
}

std::string behind_version_now() {
  if (!launcher()) return {};
  try {
    const json hello = bridge({{"type", "hello"}}, 2000);
    if (hello.count("behind") && hello["behind"].is_object()) return hello["behind"].value("version", "");
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

void set_online_mode_arg(int mode) { g_online_mode_arg = mode; }
int take_online_mode_arg() { return g_online_mode_arg.exchange(0); }

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

void refresh_identity() {
  if (g_identity_fetching.exchange(true)) return;
  std::thread([] {
    try {
      fetch_identity();
    } catch (const std::exception& e) {
      host::log("yougame: sign-in failed: %s", e.what());
    }
    g_identity_fetching = false;
  }).detach();
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

void relay_key(const char* key) {
  if (g_stdio) write_line(std::string("yg {\"key\":\"") + key + "\"}");
}

void pause_event(bool pausable, bool paused, const char* reason) {
  if (!g_stdio) return;
  json event = {{"type", "pause"}, {"pausable", pausable}, {"paused", paused}};
  if (reason) event["reason"] = reason;
  write_line("yg " + json{{"event", event}}.dump());
}

std::string status_line() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_status;
}

std::string pick_friend_room(const std::function<bool()>& cancelled) {
  return friends::resolve_room({},
      [&](const json& q, int ms) { return bridge(q, ms, cancelled); }, cancelled);
}

Match find_match(const Search& search, const std::function<bool()>& cancelled) {
  const std::string compat = compatibility(search.mode);
  if (compat.empty()) return fail("Could not verify this mod's content. Restart the game before playing online.");
  const uint64_t epoch = close_room(Leave::Quit);   // normally none is open: the cleanup closed it
  std::shared_future<void> retired;
  { std::lock_guard<std::mutex> lock(g_mutex); retired = g_retired; }
  while (retired.valid() && retired.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready)
    if (cancelled()) return fail("Cancelled");
  Identity me = identity();
  if (!me.ok) me = fetch_identity();   // this is the matchmaking thread: it may wait for the app
  if (!me.ok) return fail(search.mode == 0 && me.error == "Sign in to YouGame to play online"
                            ? "Sign in to YouGame to play Ranked." : me.error);
#ifndef _WIN32
  if (g_broker && !g_direct) return fail("YouGame broker requires direct links v1 (1200-byte datagrams)");
#endif
  const bool friends_mode = !search.friend_room.empty() || friends::empty_code(true, search.mode, search.direct_code);
  const bool direct = search.mode == 2;
  if (search.mode == 3 && !search.direct_code.empty()) return fail("Teams codes are not available on YouGame yet. Leave the code empty to find a team.");
  const int size = search.mode == 3 ? 4 : 2;
  const json lobby = {{"version", 1}, {"minPlayers", size}, {"maxLocalPlayers", 1}, {"compatibility", compat}};
  json request = {{"type", "mpTicket"}, {"players", size}, {"mode", size == 4 ? "teams" : "singles"}, {"lobby", lobby}, {"ranked", search.mode == 0}};
  // Unranked keeps its two players together for the next game, as Slippi's unranked does.
  if (search.mode == 1) request["afterMatch"] = "stay";
  if (!friends_mode && direct) request["direct"] = search.direct_code;

  // Clear failed/cancelled setup without clearing a newer search's room.
  struct FriendsSetup {
    uint64_t epoch;
    bool keep = false;
    ~FriendsSetup() {
      if (keep) return;
      std::lock_guard<std::mutex> lock(g_mutex);
      if (epoch == g_search_epoch) friends::events.current_room({});
    }
  } friends_setup{epoch};
  set_status(friends_mode ? friends::kWaiting : "searching");
  json ticket;
  try {
    if (friends_mode) {
      ticket = friends::request_ticket(compat, search.friend_room, request,
          [&](const json& q, int ms) { return bridge(q, ms, cancelled); }, cancelled,
          [&](const std::string& code) {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (epoch != g_search_epoch || cancelled()) throw std::runtime_error("Cancelled");
            friends::events.current_room(code);
          });
    } else {
      ticket = bridge(request, 30000);
    }
  } catch (const std::exception& e) {
    return fail(e.what());
  }
  // A ranked ticket carries the player's standing on this game's ladder (the ticket route's
  // `rating` and ranks.ts `rank`): the Ranked header shows it from the first search on.
  if (search.mode == 0 && ticket.is_object() && ticket.count("rank")) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_rank.standing(ticket["rank"], ticket.count("rating") ? ticket["rating"] : json()))
      host::log("yougame: ranked standing: %s", g_rank.header().c_str());
  }
  const std::string url = ticket.value("url", "");
  std::string pass = ticket.value("ticket", "");
  std::string room_code = ticket.count("room") && ticket["room"].is_string() ? ticket["room"].get<std::string>() : "";
  if (friends_mode && room_code.empty()) room_code = friends::room(request);
  if ((!g_broker && url.empty()) || pass.empty()) return fail("YouGame did not return a ticket");
  if (friends_mode && (room_code != friends::room(request))) return fail("YouGame returned a different friends room");
  if (cancelled()) return fail("Cancelled");
  std::string error;
  // The reopen loop retries every 250 ms; renewals go to the site, so at most one per 1.5 s.
  uint64_t last_renew = 0;
  const auto renew_ticket = [&](uint64_t until) {
    const auto now = now_ms();
    if (now >= until || cancelled() || (last_renew && now - last_renew < 1500)) return false;
    last_renew = now;
    return transport::renew_room_ticket(pass, room_code, [&] {
      return bridge(request, (int)std::min<uint64_t>(3000, until - now), cancelled);
    }, [](const char* message) { host::log("%s", message); });
  };

  // Unranked: wait in the queue until the matchmaker names a room.
  if (room_code.empty()) {
    Socket queue;
    if (!queue.open_session(url, pass, "", &error)) return fail(error);
    for (;;) {
      if (cancelled()) return fail("Cancelled");
      json m;
      if (!queue.next(m, 250)) {
        if (queue.closed()) {
          if (!queue.recoverable()) return fail("Lost the connection to YouGame matchmaking");
          const uint64_t until = now_ms() + 15000;
          bool reopened = false;
          transport::TicketRetry retry;
          while (!cancelled() && now_ms() < until) {
            if (retry.attempt([&] { return now_ms() < until && queue.open_session(url, pass, "", &error, (int)std::min<uint64_t>(3000, until - now_ms())); },
                              [&] { if (queue.closed()) renew_ticket(until); })) { reopened = true; break; }
            Sleep(250);
          }
          if (!reopened) return fail("Lost the connection to YouGame matchmaking");
        }
        continue;
      }
      const std::string t = m.value("t", "");
      if (t == "matched" && m.count("room") && m["room"].is_string()) { room_code = m["room"].get<std::string>(); break; }
      if (t == "error") return fail(m.value("error", "Matchmaking failed"));
    }
  } else {
    set_status(friends_mode ? friends::kWaiting : "waiting for " + search.direct_code);
  }

  auto room = std::make_unique<Room>();
  room->size = size;
  room->mode = search.mode;
  room->ranked = search.mode == 0;
  room->socket = std::make_unique<Socket>();
  room->url = url; room->ticket = pass; room->code = room_code; room->ticket_request = request;
  if (!room->socket->open_session(url, pass, room_code, &error)) return fail(error, !direct);

  const bool links_mode = g_direct;
  SOCKET udp = INVALID_SOCKET;
  struct Closer { SOCKET& s; ~Closer() { if (s != INVALID_SOCKET) closesocket(s); } } closer{udp};
  std::string external, lan_addr;
#ifndef _WIN32
  if (g_broker && !links_mode) return fail("YouGame broker requires direct links v1 (1200-byte datagrams)");
#endif
  if (!links_mode) {
    udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp == INVALID_SOCKET) return fail("Could not open the network port");
    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(search.udp_port);
    if (bind(udp, (sockaddr*)&bind_addr, sizeof bind_addr) != 0) return fail("The network port is in use");
    if (!stun(udp, external, cancelled)) host::log("yougame: STUN unavailable");
    const std::string lan = lan_ip();
    lan_addr = lan.empty() ? "" : lan + ":" + std::to_string(search.udp_port);
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
  int my_seat = -1;
  auto seat_of = [&](const std::string& conn) {
    return transport::connection_seat(participants, conn);
  };
  for (;;) {
    if (cancelled()) return fail("Cancelled");
    json m;
    if (room->socket->next(m, 200)) {
      const std::string t = m.value("t", "");
      if (t == "welcome") room->me = m.value("me", "");
      if (m.count("participants") && m["participants"].is_array()) participants = m["participants"];
      read_roster(*room, m);
      if (t == "error") return fail(m.value("error", m.value("message", "The room refused this player")), !direct);
      if (t == "leave" || t == "away") {
        // A player left before the match: forget what they sent (a newcomer gets a fresh exchange).
        const std::string who = transport::leave_id(m, room->socket->brokered());
        cands.erase(who);
        sent_at.erase(who);
      }
      if (t == "msg" && m.count("d") && m["d"].is_object()) {
        const json& d = m["d"];
        const std::string from = m.value("from", "");
        if (d.value("kind", "") == "mu-cand" && d.value("v", 0) == 2 && seat_of(from) >= 0) {
          if (d.value("compat", "") != compat) return fail("Your opponent is on a different Melee Unlocked version or mod build");
          // A player we had not heard from: answer at once. Ours may have gone out before they
          // joined, and once we hold every address we stop sending.
          if (!cands.count(from)) sent_at.erase(from);
          cands[from] = links_mode ? transport::candidate(true, d.value("compat", ""), d.value("code", ""), d.value("match", ""), "", "", "") : d;
        }
      }
    } else if (room->socket->closed()) {
      if (!room->socket->recoverable()) return fail("Lost the connection to the YouGame room", !direct);
      const uint64_t until = now_ms() + 15000;
      bool reopened = false;
      transport::TicketRetry retry;
      while (!cancelled() && now_ms() < until) {
        if (retry.attempt([&] { return now_ms() < until && room->socket->open_session(url, pass, room_code, &error, (int)std::min<uint64_t>(3000, until - now_ms())); },
                          [&] { if (room->socket->closed()) renew_ticket(until); })) { reopened = true; room->ticket = pass; break; }
        Sleep(250);
      }
      if (!reopened) return fail("Lost the connection to the YouGame room", !direct);
      participants = json::array(); cands.clear(); sent_at.clear(); tokens.clear();
      room->me.clear(); full_since = 0;
      continue;
    }
    my_seat = seat_of(room->me);
    const bool full = (int)participants.size() == size && my_seat >= 0;
    if (!full) full_since = 0;
    else if (!full_since) { full_since = now_ms(); set_status("connecting"); }
    // My addresses, point to point, each peer with a token of its own.
    if (full) {
      for (auto& p : participants) {
        const std::string conn = p.value("connectionId", "");
        if (conn == room->me || p.value("localIndex", 0) != 0) continue;
        if (sent_at.count(conn) && now_ms() - sent_at[conn] < 1000) continue;
        if (!links_mode && !tokens.count(conn)) tokens[conn] = random64() | 1;
        room->socket->send({{"t", "msg"}, {"to", conn},
          {"d", transport::candidate(links_mode, compat, me.code, my_match_id, lan_addr, external,
                                     links_mode ? "" : hex64(tokens[conn]))}});
        sent_at[conn] = now_ms();
      }
    }
    if (now_ms() - last_ping > 5000) {
      room->socket->send({{"t", "ping"}, {"at", now_ms()}});
      last_ping = now_ms();
    }
    if (!full) continue;
    bool all = true;
    for (auto& p : participants) {
      const std::string conn = p.value("connectionId", "");
      if (conn != room->me && (!cands.count(conn) || !sent_at.count(conn))) all = false;
    }
    if (all) break;
    // A seated opponent who never answers (crashed, stuck): give up and, in Unranked, search again.
    if (now_ms() - full_since > 15000) return fail("Your opponent did not answer", !direct);
  }

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
      if (parse_addr(c.value(key, ""), a) && std::none_of(peer.candidates.begin(), peer.candidates.end(), [&](auto& x) { return same_addr(x, a); }))
        peer.candidates.push_back(a);
    }
    if (peer.port == 1) match_id = c.value("match", "");
    peers.push_back(peer);
  }
  if (links_mode) {
    try {
      // Idempotent once per run. Mapping stays alive across matches; only the host owns ring I/O.
      if (!yg_enet_channel_ready()) {
        json h = bridge({{"type", "directAttach"}}, 10000);
#ifdef _WIN32
        if (h.value("version", 0) != 1 || yg_enet_attach_handles(h.value("section", uintptr_t(0)),
            h.value("toApp", uintptr_t(0)), h.value("toGame", uintptr_t(0))))
          throw std::runtime_error("Could not attach direct links");
#else
        transport::PosixAttachment p;
        if (!transport::posix_attachment(h, p) || yg_enet_attach_posix(p.section, p.wake, p.wait, p.size))
          throw std::runtime_error("Could not attach YouGame POSIX direct links");
#endif
      }
      std::vector<std::string> remotes;
      for (const auto& p : participants) if (p.value("connectionId", "") != room->me) remotes.push_back(p.value("connectionId", ""));
      if (!transport::open_links(room->links, room->socket->connection(), remotes,
          [](const json& q, int ms) { return bridge(q, ms); }, direct_state, now_ms, [] { Sleep(10); }, cancelled))
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
      yg_enet_enable(1);
    } catch (...) {
#ifndef _WIN32
      try { throw; }
      catch (const std::exception& e) { host::log("yougame: direct links failed: %s", e.what()); }
      catch (...) { host::log("yougame: direct links failed"); }
#endif
      return fail(cancelled() ? "Cancelled" : "Could not connect to your opponent directly", transport::retry_link_failure(search.mode));
    }
  } else if (!punch(udp, my_seat + 1, peers, cancelled)) {
    if (cancelled()) return fail("Cancelled");
    // Unranked searches again, as Slippi does when a found opponent cannot be reached.
    return fail("Could not connect to your opponent directly", !direct);
  }

  Match out;
  out.ok = true;
  out.match_id = match_id.rfind("yg-", 0) == 0 && match_id.size() <= 32 ? match_id : my_match_id;
  out.local_index = my_seat;
  out.host = my_seat == 0;
  room->port_ids = transport::port_participants(participants, size);
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
  room->recovery.configure(room_code, room->me, participants);
  if (room->ranked) room->set.configure(my_seat, room->port_ids);
  std::sort(out.players.begin(), out.players.end(), [](const Player& a, const Player& b) { return a.port < b.port; });
  std::string names;
  for (auto& p : out.players) if (!p.local) names += (names.empty() ? "" : ", ") + p.name + (p.code.empty() ? "" : " (" + p.code + ")");
  set_status("playing " + names);

  std::lock_guard<std::mutex> lock(g_mutex);
  if (epoch != g_search_epoch || g_room || cancelled()) {
    out.ok = false;
    out.error = "Cancelled";
    return out;
  }
  Room* raw = room.get();
  if (raw->ranked) ++g_ranked_rooms;
  g_room = std::move(room);
  friends_setup.keep = true;
  raw->pump = std::thread(pump_room, raw);
  return out;
}

#ifdef YOUGAME_AGENT_TEST_HOOK
agent_test::InputState agent_test::live_input_state(int live_mode) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const uint64_t session = g_search_epoch + 1;
  if (!g_room) return {session, live_mode, ranked_mode(live_mode)};
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

void game_started() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_room) return;
  // Frame 1 is retried while waiting for peers; emit once per game, including later games of a set.
  if (g_room->ranked && !g_room->in_game)
    host::log("%s", lcancel::policy::ranked_start_log(lcancel::automatic_enabled()));
  g_room->in_game = true;
  // Called on every frame 1 the game sends while it waits for the opponent: the set counts a game once.
  if (g_room->ranked) { g_room->set.start_game(); return; }
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
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_room || !g_room->in_game) return;   // one report per game, however many places say it ended
  if (ranked::time_out_waits_for_report(end.end_method, end.has_placements, g_room->size)) {
    host::log("yougame: time-out: the game's report names the winner");
    return;
  }
  g_room->in_game = false;
  if (g_room->ranked) {
    // The game's own end record decides the game (yougame_ranked.h game_winner); a No Contest
    // naming a player (they disconnected or the games desynced: Ranked has no LRA-Start) also
    // ends the set for them.
    ranked::Game game;
    game.key = ranked::game_key(end.rng, end.stage);   // an id both players' games share
    game.index = (uint32_t)g_room->set.games_started();
    game.winner = ranked::game_winner(end.end_method, end.lras, end.winner, end.has_placements ? end.placements : nullptr);
    game.forfeit = end.end_method == 7 && game.winner >= 0;
    game.detail = ranked::game_detail(end.end_method, end.lras, end.stage, end.characters, end.frames);
    g_room->set.report_game(game);
    host::log("yougame: ranked game %u (%s): %s%s", game.index, game.key.c_str(),
              game.winner >= 0 ? ("port " + std::to_string(game.winner + 1) + " won").c_str() : "no winner",
              game.forfeit ? "; a No Contest: the set goes to the other player" : "");
    return;
  }
  if (!g_room->socket->brokered()) g_room->pending_begin = false;
  if (g_room->size == 4) {
    int winner = ranked::team_winner(end.end_method, end.lras,
                                     end.has_placements ? end.placements : nullptr, end.teams);
    // A CSS fallback played with other teams cannot claim the room's team result.
    for (int port = 0; port < 4; ++port) if (end.teams[port] != g_room->seat_team(port)) winner = -1;
    if (g_room->socket->brokered()) g_room->games.report_team(winner, g_room->port_ids, g_room->port_teams.data());
    else g_room->pending_team_result = transport::team_outcome(winner, g_room->port_ids, g_room->port_teams.data());
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
  return g_room && g_room->ranked && g_room->set.over();
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

std::string ranked_header() {
  std::lock_guard<std::mutex> lock(g_mutex);
  const uint64_t now = now_ms();
  RankRead& read = g_rank_read;
  const bool want = !g_rank.known() || (read.stale_at && now >= read.stale_at && !g_room);
  if (want && !read.unsupported && !read.in_flight && now >= read.next_at && g_identity.ok) {
    read.in_flight = true;
    read.stale_at = 0;
    const uint64_t asked = g_rank.version();
    std::thread([asked] {
      json result;
      std::string error;
      try {
        result = bridge({{"type", "mpRank"}, {"mode", "singles"}}, 10000);
      } catch (const std::exception& e) {
        error = e.what();
      }
      std::lock_guard<std::mutex> lock(g_mutex);
      g_rank_read.in_flight = false;
      if (!error.empty()) {
        if (ranked::unknown_request(error)) {
          g_rank_read.unsupported = true;   // an app without mpRank: the rank shows after a ticket or a result
          host::log("yougame: the app does not answer mpRank; the ranked standing comes with tickets and results");
        } else {
          g_rank_read.next_at = now_ms() + 60000;
          host::log("yougame: mpRank failed: %s", error.c_str());
        }
        return;
      }
      if (ranked::apply_rank_answer(g_rank, result, asked)) host::log("yougame: ranked standing: %s", g_rank.header().c_str());
      else g_rank_read.next_at = now_ms() + 5 * 60000;   // no ranked result yet, or a newer one arrived meanwhile
    }).detach();
  }
  return g_rank.header();
}

std::vector<uint8_t> rank_response() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_rank.response();
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
  close_room(why);
  set_status("offline");
}

}  // namespace yougame
