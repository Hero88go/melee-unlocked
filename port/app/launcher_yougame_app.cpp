// The installed YouGame app as the launcher's sign-in (launcher_yougame_app.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_yougame_app.h"

namespace launcher::yougame::app {

using json = nlohmann::json;

Answer read_hello(const json& answer, bool* pending) {
  if (pending) *pending = false;
  Answer a;
  if (!answer.is_object()) { a.state = Answer::kNoAnswer; a.why = "no hello"; return a; }
  if (!answer.value("ok", false)) {
    a.state = Answer::kRefused;
    const std::string code = answer.count("code") && answer["code"].is_string() ? answer["code"].get<std::string>() : "";
    a.why = "hello refused" + (code.empty() || code.size() > 24 ? std::string() : " " + code);
    return a;
  }
  const json result = answer.count("result") && answer["result"].is_object() ? answer["result"] : json::object();
  const json player = result.count("player") && result["player"].is_object() ? result["player"] : json::object();
  const std::string id = player.count("id") && player["id"].is_string() ? player["id"].get<std::string>() : "";
  if (!id.empty()) {
    a.state = Answer::kSignedIn;
    a.player_id = id;
    // Until the player shares their name with this game the app calls them "Player": the caller uses the
    // tag of their connect code then (as the game does).
    if (result.value("nameShared", false) && player.count("name") && player["name"].is_string()) a.name = player["name"];
    return a;
  }
  a.state = Answer::kSignedOut;
  if (result.value("playerPending", false) == true && pending) *pending = true;
  return a;
}

}  // namespace launcher::yougame::app

#if defined(_WIN32) && defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include "yougame_channel.h"
// YouGame's start hook, the same header the game carries (MIT, beside it in third_party/yougame_start). The
// launcher is its own program, so this file is the one place in it that holds the hook's code.
#define YOUGAME_START_IMPLEMENTATION
#include "yougame_start/yougame_start.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace launcher::yougame::app {
namespace {

namespace channel = ::yougame::channel;

bool find(bool staging, channel::Candidate* out, std::string* why) {
  for (const auto& c : channel::candidates(why))
    if (c.staging == staging) { *out = c; return true; }
  return false;
}

// What the app's channel sent: answers by request id, and whether the waited-for player arrived.
struct Inbox {
  std::mutex m;
  std::condition_variable cv;
  std::map<int64_t, json> answers;
  bool arrived = false, eof = false;
};

}  // namespace

bool installed(bool staging) {
  const std::string scheme = staging ? "yougame-staging" : "yougame";
  const std::string user = "Software\\Classes\\" + scheme + "\\shell\\open\\command", root = scheme + "\\shell\\open\\command";
  HKEY h = nullptr;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, user.c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS ||
      RegOpenKeyExA(HKEY_CLASSES_ROOT, root.c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS) {
    RegCloseKey(h);
    return true;
  }
  return false;
}

bool running(bool staging) {
  channel::Candidate c;
  if (!find(staging, &c, nullptr)) return false;
  std::wstring name(c.path.begin(), c.path.end());   // \\.\pipe\yougame-...-<SID>: ASCII
  // A 1 ms wait never connects: the app sees nothing. Busy (every instance taken) still means running.
  const BOOL free_instance = WaitNamedPipeW(name.c_str(), 1);
  const DWORD error = GetLastError();
  return free_instance || error == ERROR_SEM_TIMEOUT || error == ERROR_PIPE_BUSY;
}

bool start(bool staging, int wait_ms) {
  if (running(staging)) return true;
  if (!installed(staging)) return false;
  // The app's own quiet-start link (the one YouGame's start hook, yougame_start.h, starts it with): it comes up
  // in the tray with no window. Started through the registered link handler, so nothing of the app is read here.
  const wchar_t* link = staging ? L"yougame-staging://tray" : L"yougame://tray";
  if ((INT_PTR)ShellExecuteW(nullptr, L"open", link, nullptr, nullptr, SW_SHOWNORMAL) <= 32) return false;
  for (int waited = 0; waited < wait_ms; waited += 500) {
    Sleep(500);
    if (running(staging)) return true;
  }
  return false;
}

bool elevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elevation{};
  DWORD size = 0;
  const bool raised = GetTokenInformation(token, TokenElevation, &elevation, sizeof elevation, &size) &&
                      elevation.TokenIsElevated != 0;
  CloseHandle(token);
  return raised;
}

int start_hook(bool staging) {
  // As administrator the hook starts nothing and its window only says to start the game normally: the
  // launcher's own sign-in works there, so the caller takes that instead.
  if (elevated()) return YG_START_UNAVAILABLE;
  yg_start_options o{};
  o.game = "melee-unlocked";
  o.staging = staging ? 1 : 0;
  o.ask = YG_START_ASK_ON_USE;   // the player pressed Connect: the window may show now, Play offline goes back
  o.name = "Melee Unlocked";     // "Play Melee Unlocked online"; needs stays 0 (offline play works)
  // Started quietly: up to 20 s for its channel, so YG_START_OPENED means the launcher can ask it at once
  // (and one that does not come up gets the hook's own "YouGame didn't open" window, not a silent wait).
  o.open_wait_ms = 20000;
  // The first YouGame that starts hidden in the tray, as the game names it (yougame_link.cpp tray_from).
#ifdef MELEE_YOUGAME_PROD_TRAY_FROM
  o.tray_from = staging ? "0.13.1407" : MELEE_YOUGAME_PROD_TRAY_FROM;
#else
  o.tray_from = staging ? "0.13.1407" : "0.13.25";
#endif
  return (int)yg_start(&o);
}

Answer ask(bool staging, const std::string& version, int timeout_ms) {
  Answer out;
  channel::Candidate where;
  std::string why;
  if (!find(staging, &where, &why)) { out.state = Answer::kRefused; out.why = why; return out; }
  std::unique_ptr<channel::Connection> opened;
  const channel::OpenResult result = channel::Connection::open(where, &opened, &why);
  if (result == channel::OpenResult::Missing) { out.state = Answer::kNotRunning; return out; }
  if (result == channel::OpenResult::Refused) { out.state = Answer::kRefused; out.why = why; return out; }
  std::shared_ptr<channel::Connection> conn(std::move(opened));
  auto box = std::make_shared<Inbox>();
  std::thread reader([conn, box] {
    conn->read_lines([box](const std::string& line) {
      const channel::LineAction action = channel::classify(line);
      if (action == channel::LineAction::PlayerArrived) {
        std::lock_guard<std::mutex> lock(box->m);
        box->arrived = true;
        box->cv.notify_all();
        return;
      }
      if (action != channel::LineAction::Answer) return;   // other pushes are the game's, not the launcher's
      json j = json::parse(line.substr(3), nullptr, false);
      if (!j.is_object()) return;
      const int64_t id = j["re"].get<int64_t>();
      std::lock_guard<std::mutex> lock(box->m);
      box->answers[id] = std::move(j);
      box->cv.notify_all();
    });
    std::lock_guard<std::mutex> lock(box->m);
    box->eof = true;
    box->cv.notify_all();
  });
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  int64_t next_id = 1;
  // One request and its answer; json() when none came (closed, too long, or past the overall time).
  auto request = [&](json body, int ms) -> json {
    const int64_t id = next_id++;
    body["id"] = id;
    const std::string line = channel::request_line(body);
    if (line.empty() || !conn->write_line(line)) return json();
    std::unique_lock<std::mutex> lock(box->m);
    const auto until = std::min(end, std::chrono::steady_clock::now() + std::chrono::milliseconds(ms));
    box->cv.wait_until(lock, until, [&] { return box->answers.count(id) || box->eof; });
    if (!box->answers.count(id)) return json();
    json j = std::move(box->answers[id]);
    box->answers.erase(id);
    return j;
  };
  const json hello_body = channel::hello_request({{"type", "hello"}}, version);
  bool pending = false;
  json hello = request(hello_body, channel::kHelloTimeoutMs);
  out = read_hello(hello, &pending);
  // playerPending (the app is still loading its player): hello again about 2 s and 6 s later, or as soon
  // as the app says the player arrived (channel::PlayerWait's schedule).
  for (const int wait_ms : {(int)channel::PlayerWait::kFirstRetryMs, (int)(channel::PlayerWait::kSecondRetryMs - channel::PlayerWait::kFirstRetryMs)}) {
    if (!pending) break;
    {
      std::unique_lock<std::mutex> lock(box->m);
      box->cv.wait_until(lock, std::min(end, std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms)),
                         [&] { return box->arrived || box->eof; });
      box->arrived = false;
    }
    hello = request(hello_body, channel::kHelloTimeoutMs);
    out = read_hello(hello, &pending);
  }
  if (pending) { out.state = Answer::kNoAnswer; out.why = "the app's player did not arrive"; }
  if (out.state == Answer::kNoAnswer && out.why.empty()) out.why = "no hello";
  if (out.state == Answer::kSignedIn) {
    // The connect code, asked as the game asks it; accountSwitching: again after its retryAfter, twice at most.
    for (int attempt = 0; attempt < 3; ++attempt) {
      const json code = request({{"type", "mpCode"}}, 8000);
      if (code.is_object() && code.value("ok", false)) {
        const json r = code.count("result") && code["result"].is_object() ? code["result"] : json::object();
        if (r.count("code") && r["code"].is_string()) out.code = r["code"];
        break;
      }
      const std::string c = code.is_object() && code.count("code") && code["code"].is_string() ? code["code"].get<std::string>() : "";
      if (c == "signedOut") { out = Answer{}; out.state = Answer::kSignedOut; break; }
      if (c != "accountSwitching") break;
      std::this_thread::sleep_for(std::chrono::milliseconds(channel::retry_after_ms(code)));
    }
    if (out.state == Answer::kSignedIn && out.name.empty())
      out.name = out.code.empty() ? std::string("Player") : out.code.substr(0, out.code.find('#'));
  }
  conn->close();
  reader.join();
  return out;
}

}  // namespace launcher::yougame::app
#endif
