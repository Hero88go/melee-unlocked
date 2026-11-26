// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_room.h"
#include "launcher_yougame.h"
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace launcher::room {
namespace {
Callbacks callbacks;
std::mutex mutex;
State state;
std::thread worker;
std::atomic<bool> stopping{false};
std::deque<int> commands; // 1 start, 2 invite
std::atomic<HINTERNET> live_socket{nullptr};
std::string ready_file;
struct Internet {
  HINTERNET handle = nullptr;
  ~Internet() { if (handle) WinHttpCloseHandle(handle); }
};
class RoomReader {
 public:
  explicit RoomReader(HINTERNET socket) : thread_([this,socket] {
    std::string fragments;
    for (;;) {
      char buffer[16384]; DWORD count = 0; WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;
      const DWORD result = WinHttpWebSocketReceive(socket,buffer,sizeof buffer,&count,&kind);
      if (result || kind == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
      if (kind != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE &&
          kind != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;
      fragments.append(buffer,count);
      if (fragments.size()>65536) break;
      if (kind != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) continue;
      const json frame = json::parse(fragments,nullptr,false);
      fragments.clear();
      if (!frame.is_object()) continue;
      std::lock_guard<std::mutex> lock(mutex_);
      if (frames_.size()>=1024) break;
      frames_.push_back(frame);
      changed_.notify_all();
    }
    { std::lock_guard<std::mutex> lock(mutex_); closed_=true; }
    changed_.notify_all();
  }) {}
  ~RoomReader() {
    if (HINTERNET socket=live_socket.exchange(nullptr)) WinHttpCloseHandle(socket);
    if (thread_.joinable()) thread_.join();
  }
  bool next(json& frame) {
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait_for(lock,std::chrono::milliseconds(100),[this] { return closed_ || !frames_.empty(); });
    if (frames_.empty()) {
      if (closed_ && !stopping) throw std::runtime_error("The YouGame room connection ended. Create or join the room again.");
      return false;
    }
    frame=std::move(frames_.front()); frames_.pop_front();
    return true;
  }
 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<json> frames_;
  bool closed_=false;
  std::thread thread_;
};
std::wstring wide(const std::string& s) {
  const int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring result(size, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), result.data(), size);
  return result;
}
void notify() { if (callbacks.changed) callbacks.changed(); }
void failure(const std::string& reason) {
  { std::lock_guard<std::mutex> lock(mutex); state.error = reason; state.connected = false; }
  notify();
}
void flush(HINTERNET socket) {
  std::vector<json> frames;
  { std::lock_guard<std::mutex> lock(mutex); frames.swap(state.out); }
  for (const auto& frame : frames) {
    const std::string text = frame.dump();
    if (WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                            (void*)text.data(), (DWORD)text.size()) != NO_ERROR)
      throw std::runtime_error("The YouGame room connection ended.");
  }
}
void run(bool public_room, bool creating) {
  try {
    const State initial = current();
    json request{{"type", "mpTicket"}, {"room", initial.code}, {"players", 8}, {"mode", "mu-launcher"},
                 {"ranked", false},
                 {"lobby", {{"version", 1}, {"minPlayers", 2}, {"maxLocalPlayers", 1},
                            {"compatibility", "mu-launcher-room-1"}}}};
    if (creating && public_room) request["public"] = true;
    const json ticket = json::parse(yougame::sdk_request(request.dump(), callbacks.version));
    if (stopping) return;
    std::string endpoint = ticket.value("url", std::string());
    if (endpoint.compare(0, 8, "https://") == 0) endpoint.replace(0, 8, "wss://");
    if (endpoint.compare(0, 6, "wss://") != 0) throw std::runtime_error("YouGame returned an invalid room address.");
    // Tickets are opaque URL query values. Encode every character outside RFC 3986's unreserved set.
    std::string encoded;
    for (unsigned char c : ticket.value("ticket", std::string())) {
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') encoded += (char)c;
      else { char hex[4]; std::snprintf(hex, sizeof hex, "%%%02X", c); encoded += hex; }
    }
    if (encoded.empty()) throw std::runtime_error("YouGame returned no room ticket.");
    const auto slash = endpoint.find('/', 6);
    const std::string host = endpoint.substr(6, slash == std::string::npos ? slash : slash - 6);
    const std::wstring path = wide("/room/" + initial.code + "?ticket=" + encoded);
    Internet session{WinHttpOpen(L"MeleeUnlocked rooms", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.handle) throw std::runtime_error("Could not open the YouGame room connection.");
    WinHttpSetTimeouts(session.handle, 5000, 5000, 5000, 1000);
    Internet connection{WinHttpConnect(session.handle, wide(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
    Internet upgrade{connection.handle ? WinHttpOpenRequest(connection.handle, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr};
    if (!upgrade.handle || !WinHttpSetOption(upgrade.handle, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
        !WinHttpSendRequest(upgrade.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(upgrade.handle, nullptr))
      throw std::runtime_error("Could not join the YouGame room.");
    HINTERNET socket = WinHttpWebSocketCompleteUpgrade(upgrade.handle, 0);
    if (!socket) throw std::runtime_error("YouGame refused the room connection.");
    live_socket = socket;
    RoomReader reader(socket);
    uint64_t launch_at = 0;
    while (!stopping) {
      std::deque<int> actions;
      { std::lock_guard<std::mutex> lock(mutex); actions.swap(commands); }
      for (int action : actions) {
        if (action == 1) {
          std::lock_guard<std::mutex> lock(mutex);
          if (state.start()) launch_at = GetTickCount64();
        } else if (action == 2) {
          const State view = current();
          // The picker may wait for a person. Keep receiving room probes while it is open.
          std::thread([code=view.code, version=callbacks.version] {
            try {
              yougame::sdk_request(json{{"type", "mpFriend"}, {"room", code}, {"players", 8},
                                        {"mode", "mu-launcher"}, {"multi", true}}.dump(), version, 60000);
            } catch (const std::exception& e) {
              { std::lock_guard<std::mutex> lock(mutex); if (state.code == code) state.error=e.what(); }
              notify();
            }
          }).detach();
        }
      }
      bool launch = false, hosting = false;
      State plan;
      {
        std::lock_guard<std::mutex> lock(mutex);
        launch = state.launch_due; state.launch_due = false;
        plan = state; hosting = state.hosting();
      }
      if (launch) {
        const std::string reason = callbacks.available(plan.variant);
        if (!reason.empty()) throw std::runtime_error(reason);
        callbacks.launch(plan.variant, plan.arena, hosting, hosting ? ready_file : std::string());
      }
      if (launch_at) {
        std::ifstream file(std::filesystem::u8path(ready_file));
        std::string code; file >> code;
        if (code == plan.arena) {
          { std::lock_guard<std::mutex> lock(mutex); state.host_ready(); }
          launch_at = 0; notify();
        } else if (GetTickCount64() - launch_at > 90000)
          throw std::runtime_error("The host game could not open the arena. Close it and create the room again.");
      }
      flush(socket);
      json frame;
      if (!reader.next(frame)) continue;
      bool configuration = false;
      { std::lock_guard<std::mutex> lock(mutex); configuration = state.receive(frame); }
      if (configuration) {
        const std::string reason = callbacks.available(current().variant);
        std::lock_guard<std::mutex> lock(mutex); state.ready(reason.empty(), reason);
        if (!reason.empty()) state.error = reason;
      }
      if (!current().connected) break;
      notify();
    }
  } catch (const std::exception& error) { if (!stopping) failure(error.what()); }
  if (HINTERNET socket = live_socket.exchange(nullptr)) WinHttpCloseHandle(socket);
}
}
void init(Callbacks cb) { callbacks = std::move(cb); }
State current() { std::lock_guard<std::mutex> lock(mutex); return state; }
void leave() {
  stopping = true;
  if (HINTERNET socket = live_socket.exchange(nullptr)) WinHttpCloseHandle(socket);
  if (worker.joinable()) worker.join();
  { std::lock_guard<std::mutex> lock(mutex); state = {}; commands.clear(); }
  notify();
}
void open(const std::string& code, int variant, bool public_room) {
  if (!code.empty() && !valid_code(code)) { failure("A room code has 6 to 12 letters or numbers."); return; }
  leave();
  if (code.empty()) {
    const std::string reason = callbacks.available(variant);
    if (!reason.empty()) { failure(reason); return; }
  }
  const bool creating = code.empty();
  const std::string selected = creating ? "m" + std::to_string(variant) + yougame::new_room_code().substr(2) : code;
  ready_file = callbacks.directory + "/arena-ready-" + selected + ".txt";
  std::error_code ignored; std::filesystem::remove(std::filesystem::u8path(ready_file), ignored);
  {
    std::lock_guard<std::mutex> lock(mutex);
    state.code = selected; state.variant = variant;
    state.name = yougame::account().name + "'s room";
    state.arena = yougame::new_room_code();
  }
  stopping = false;
  worker = std::thread(run, public_room, creating);
  notify();
}
void start() { std::lock_guard<std::mutex> lock(mutex); commands.push_back(1); }
void invite() { std::lock_guard<std::mutex> lock(mutex); commands.push_back(2); }
void shutdown() { leave(); }
}
