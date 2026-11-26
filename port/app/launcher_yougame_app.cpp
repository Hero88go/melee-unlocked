// The installed YouGame app as the launcher's sign-in (launcher_yougame_app.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdlib>
#include <cwchar>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "launcher_yougame_app.h"
#ifdef _WIN32
#include <windows.h>
#endif

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

// Windows (its named pipe) and Linux (its socket in the session's runtime folder): the app's channel is
// yougame_channel.h's either way; only finding and starting the app differ.
#if (defined(_WIN32) && defined(_MSC_VER)) || defined(MELEE_LINUX)
#if defined(_WIN32) && defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
extern char** environ;
#endif
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include "yougame_channel.h"

#if defined(_WIN32) && defined(_MSC_VER)
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

namespace launcher::yougame::app {
namespace {

namespace channel = ::yougame::channel;

bool find(bool staging, channel::Candidate* out, std::string* why) {
  // Test runs only (MELEE_LAUNCHER_TEST_NO_APP): as if the YouGame app were closed.
  if (std::getenv("MELEE_LAUNCHER_TEST") && std::getenv("MELEE_LAUNCHER_TEST_NO_APP")) { if (why) *why = "test run: no app"; return false; }
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

#if defined(_WIN32) && defined(_MSC_VER)
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
#else
// Linux: the app registers its link type with the desktop (x-scheme-handler/yougame, as xdg-open uses).
std::string run_text(const std::vector<std::string>& argv) {
  int out[2];
  if (pipe(out) != 0) return {};
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, out[1], 1);
  posix_spawn_file_actions_addclose(&fa, out[0]);
  std::vector<char*> args;
  for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, args[0], &fa, nullptr, args.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  close(out[1]);
  std::string text;
  if (rc == 0) {
    char buf[512];
    for (ssize_t n; (n = read(out[0], buf, sizeof buf)) > 0;) text.append(buf, (size_t)n);
    int status = 0;
    waitpid(pid, &status, 0);
  }
  close(out[0]);
  return text;
}
bool installed(bool staging) {
  const std::string handler = run_text({"xdg-mime", "query", "default", std::string("x-scheme-handler/") + (staging ? "yougame-staging" : "yougame")});
  return handler.find(".desktop") != std::string::npos;
}
bool running(bool staging) {
  channel::Candidate c;
  if (!find(staging, &c, nullptr)) return false;
  // Only whether the app listens there: connect and close at once (the app sees a client that left).
  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return false;
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (c.path.size() >= sizeof address.sun_path) { close(fd); return false; }
  std::memcpy(address.sun_path, c.path.c_str(), c.path.size() + 1);
  const bool up = connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0;
  close(fd);
  return up;
}
bool start(bool staging, int wait_ms) {
  if (running(staging)) return true;
  if (!installed(staging)) return false;
  // The app's own quiet-start link, through the desktop's link handler (tray only, no window).
  run_text({"xdg-open", staging ? "yougame-staging://tray" : "yougame://tray"});
  for (int waited = 0; waited < wait_ms; waited += 500) {
    usleep(500 * 1000);
    if (running(staging)) return true;
  }
  return false;
}
#endif

// Opens the app's channel, says hello as this game, then (signed in or not) lets `more` make its own requests on the
// same connection before it closes. ask() and social() are its two uses.
using Request = std::function<json(json, int)>;
static Answer run(bool staging, const std::string& version, int timeout_ms, const std::function<void(const Request&, Answer&)>& more) {
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
    const auto until = (std::min)(end, std::chrono::steady_clock::now() + std::chrono::milliseconds(ms));
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
      box->cv.wait_until(lock, (std::min)(end, std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms)),
                         [&] { return box->arrived || box->eof; });
      box->arrived = false;
    }
    hello = request(hello_body, channel::kHelloTimeoutMs);
    out = read_hello(hello, &pending);
  }
  if (pending) { out.state = Answer::kNoAnswer; out.why = "the app's player did not arrive"; }
  if (out.state == Answer::kNoAnswer && out.why.empty()) out.why = "no hello";
  if (more) more(request, out);
  conn->close();
  reader.join();
  return out;
}

#ifdef _WIN32
namespace {
json answer_json(const Answer& a) { return {{"state", (int)a.state}, {"player_id", a.player_id}, {"name", a.name}, {"code", a.code}, {"why", a.why}}; }
Answer answer_from(const json& j) {
  Answer a; if (!j.is_object()) { a.state = Answer::kNoAnswer; a.why = "no answer from the channel helper"; return a; }
  a.state = (Answer::State)j.value("state", (int)Answer::kNoAnswer); a.player_id = j.value("player_id", std::string());
  a.name = j.value("name", std::string()); a.code = j.value("code", std::string()); a.why = j.value("why", std::string()); return a;
}
// Runs `job` ({op, staging, version, timeout, requests}) in the hidden copy and returns what it printed (one JSON).
json in_helper(const json& job, int timeout_ms) {
  wchar_t exe[MAX_PATH] = {}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE}; HANDLE in_r = nullptr, in_w = nullptr, out_r = nullptr, out_w = nullptr;
  if (!CreatePipe(&in_r, &in_w, &sa, 0)) return {};
  if (!CreatePipe(&out_r, &out_w, &sa, 0)) { CloseHandle(in_r); CloseHandle(in_w); return {}; }
  SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si{sizeof si}; si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
  si.hStdInput = in_r; si.hStdOutput = out_w; si.hStdError = out_w;
  std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --yougame-channel";
  PROCESS_INFORMATION pi{};
  const BOOL ok = CreateProcessW(exe, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
  CloseHandle(in_r); CloseHandle(out_w);
  if (!ok) { CloseHandle(in_w); CloseHandle(out_r); return {}; }
  const std::string text = job.dump() + "\n"; DWORD wrote = 0; WriteFile(in_w, text.data(), (DWORD)text.size(), &wrote, nullptr); CloseHandle(in_w);
  std::string got; char buf[4096]; DWORD n = 0;
  const ULONGLONG until = GetTickCount64() + (ULONGLONG)timeout_ms + 5000;
  for (;;) {   // read until the copy ends or the time is up
    DWORD avail = 0;
    if (PeekNamedPipe(out_r, nullptr, 0, nullptr, &avail, nullptr) && avail) { if (ReadFile(out_r, buf, sizeof buf, &n, nullptr) && n) got.append(buf, n); continue; }
    if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) { while (ReadFile(out_r, buf, sizeof buf, &n, nullptr) && n) got.append(buf, n); break; }
    if (GetTickCount64() > until) { TerminateProcess(pi.hProcess, 1); break; }   // only that copy, by its own handle
  }
  CloseHandle(out_r); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  const size_t at = got.rfind("\nYGHELPER "); const size_t from = got.rfind("YGHELPER ", 0) == 0 ? 0 : at == std::string::npos ? std::string::npos : at + 1;
  if (from == std::string::npos) return {};
  return json::parse(got.substr(from + 9), nullptr, false);
}
bool is_helper = false;
}  // namespace
#endif
Answer ask_direct(bool staging, const std::string& version, int timeout_ms);
Social social_direct(bool staging, const std::string& version, const std::vector<json>& requests, int timeout_ms);
Answer ask(bool staging, const std::string& version, int timeout_ms) {
#ifdef _WIN32
  if (!is_helper) return answer_from(in_helper({{"op", "ask"}, {"staging", staging}, {"version", version}, {"timeout", timeout_ms}}, timeout_ms).value("hello", json()));
#endif
  return ask_direct(staging, version, timeout_ms);
}
Answer ask_direct(bool staging, const std::string& version, int timeout_ms) {
  return run(staging, version, timeout_ms, [](const Request& request, Answer& out) {
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
  });
}

Social social(bool staging, const std::string& version, const std::vector<json>& requests, int timeout_ms) {
#ifdef _WIN32
  if (!is_helper) {
    const json r = in_helper({{"op", "social"}, {"staging", staging}, {"version", version}, {"timeout", timeout_ms}, {"requests", requests}}, timeout_ms);
    Social got; got.hello = answer_from(r.value("hello", json()));
    if (r.is_object() && r.count("answers") && r["answers"].is_object()) for (auto it = r["answers"].begin(); it != r["answers"].end(); ++it) got.answers[it.key()] = it.value();
    return got;
  }
#endif
  return social_direct(staging, version, requests, timeout_ms);
}
Social social_direct(bool staging, const std::string& version, const std::vector<json>& requests, int timeout_ms) {
  Social got;
  got.hello = run(staging, version, timeout_ms, [&](const Request& request, Answer& out) {
    for (const json& body : requests) {
      const std::string type = body.value("type", std::string());
      got.answers[type] = request(body, type == "mpFriend" ? timeout_ms : 8000);
    }
    (void)out;
  });
  return got;
}

#ifdef _WIN32
bool helper_main(const wchar_t* command_line, int* code) {
  if (!command_line || !std::wcsstr(command_line, L"--yougame-channel")) return false;
  is_helper = true; *code = 0;
  std::string text; char buf[4096]; DWORD n = 0; HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  while (ReadFile(in, buf, sizeof buf, &n, nullptr) && n) { text.append(buf, n); if (text.find('\n') != std::string::npos) break; }
  const json job = json::parse(text, nullptr, false);
  json out;
  if (job.is_object()) {
    const bool staging = job.value("staging", false); const std::string version = job.value("version", std::string());
    const int timeout = job.value("timeout", 15000);
    if (job.value("op", std::string()) == "ask") out["hello"] = answer_json(ask_direct(staging, version, timeout));
    else {
      std::vector<json> requests; if (job.count("requests") && job["requests"].is_array()) for (const auto& q : job["requests"]) requests.push_back(q);
      const Social s = social_direct(staging, version, requests, timeout);
      out["hello"] = answer_json(s.hello); out["answers"] = json::object(); for (const auto& [k, v] : s.answers) out["answers"][k] = v;
    }
  }
  const std::string line = "YGHELPER " + out.dump() + "\n"; DWORD wrote = 0;
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), (DWORD)line.size(), &wrote, nullptr);
  return true;
}
#endif
}  // namespace launcher::yougame::app
#endif
