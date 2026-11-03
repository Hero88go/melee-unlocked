// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_yougame.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#ifdef _WIN32
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#endif
#include <bcrypt.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#endif

using json = nlohmann::json;

namespace launcher::yougame {
namespace {

constexpr const char* kSlug = "melee-unlocked";
constexpr size_t kMaxRequest = 1u << 20;

// The two YouGame sites. The public keys are the ones each site's own page sends to its sign-in
// service (anonymous role; they identify the project, they are not secrets).
const Env kProduction{"production", "https://yougame.co", "https://auth.yougame.co",
                      "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImhwbWlwcmhhdXRud2NobXBoenN4Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODg1NDc1ODgsImV4cCI6MjEwNDEyMzU4OH0.bB3FxRSroYA9CV5snhfptc57EtQlbKZdBRzJ1pJ87_A",
                      "sb-hpmiprhautnwchmphzsx-auth-token"};
const Env kStaging{"staging", "https://staging.yougame.co", "https://ibhaehthpikxpoappztk.supabase.co",
                   "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImliaGFlaHRocGlreHBvYXBwenRrIiwicm9sZSI6ImFub24iLCJpYXQiOjE3OTEwNjUwODQsImV4cCI6MjEwNjY0MTA4NH0.JtMaUvaE8ijc2ntwg6Ji-WBoxjZLjUWJg6UK_6_lAKw",
                   "sb-ibhaehthpikxpoappztk-auth-token"};

struct Session {
  std::string access, refresh, user_id, email, name, code;
  int64_t expires_at = 0;   // seconds since 1970
};

std::mutex g_mutex;
Env g_env = kProduction;
Session g_session;
std::string g_build_json = "{}";
std::vector<Allowed> g_allowed;     // the builds YouGame allows, as last read
Allowed g_behind;                    // the release to offer, version empty when none
std::function<std::string(const Allowed&, const std::string&)> g_restart_older;
std::deque<std::string> g_tokens;   // newest first; a few, so a game still running keeps its own
uint16_t g_port = 0;
std::atomic<bool> g_bridge_up{false};

int64_t now_s() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::wstring wide(const std::string& s) {
  if (s.empty()) return {};
  std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), (int)w.size());
  return w;
}

// Tests only (MELEE_LAUNCHER_TEST): the site and sign-in service may be a local stand-in.
Env effective_env(Env e) {
  if (!std::getenv("MELEE_LAUNCHER_TEST")) return e;
  if (const char* v = std::getenv("MELEE_YOUGAME_SITE")) e.site = v;
  if (const char* v = std::getenv("MELEE_YOUGAME_AUTH")) e.auth = v;
  return e;
}

// ---- base64 / DPAPI for the saved session

std::string b64(const std::string& in, bool url) {
  static const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, bits = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c; bits += 8;
    while (bits >= 0) { out.push_back(a[(val >> bits) & 0x3F]); bits -= 6; }
  }
  if (bits > -6) out.push_back(a[((val << 8) >> (bits + 8)) & 0x3F]);
  if (url) {
    for (char& c : out) c = c == '+' ? '-' : c == '/' ? '_' : c;
  } else {
    while (out.size() % 4) out.push_back('=');
  }
  return out;
}
std::string unb64(const std::string& in) {
  std::string out;
  int val = 0, bits = -8;
  for (char c : in) {
    int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 :
            c == '+' || c == '-' ? 62 : c == '/' || c == '_' ? 63 : -1;
    if (d < 0) continue;
    val = (val << 6) + d; bits += 6;
    if (bits >= 0) { out.push_back(char((val >> bits) & 0xFF)); bits -= 8; }
  }
  return out;
}
#ifndef _WIN32
// Linux has no per-user encryption service here: the session is kept as text in launcher.ini, which
// the launcher writes readable by its owner only (0600).
std::string protect(const std::string& plain) { return "p:" + b64(plain, false); }
std::string unprotect(const std::string& text) { return text.rfind("p:", 0) == 0 ? unb64(text.substr(2)) : std::string(); }
#else
std::string protect(const std::string& plain) {
  DATA_BLOB in{(DWORD)plain.size(), (BYTE*)plain.data()}, out{};
  if (!CryptProtectData(&in, L"Melee Unlocked YouGame", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
  std::string s((const char*)out.pbData, out.cbData);
  LocalFree(out.pbData);
  return b64(s, false);
}
std::string unprotect(const std::string& text) {
  const std::string raw = unb64(text);
  DATA_BLOB in{(DWORD)raw.size(), (BYTE*)raw.data()}, out{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
  std::string s((const char*)out.pbData, out.cbData);
  SecureZeroMemory(out.pbData, out.cbData);
  LocalFree(out.pbData);
  return s;
}
#endif

// ---- HTTP client (WinHTTP)

struct Reply { int status = 0; std::string body; };

Reply http(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
           const std::string& body, int timeout_ms = 15000) {
#ifdef _WIN32
  URL_COMPONENTSW c{};
#else
  URL_COMPONENTS c{};   // the Linux WinHTTP stand-in's name
#endif
  c.dwStructSize = sizeof c;
  c.dwHostNameLength = c.dwUrlPathLength = c.dwExtraInfoLength = (DWORD)-1;
  std::wstring w = wide(url);
  if (!WinHttpCrackUrl(w.c_str(), 0, 0, &c)) throw std::runtime_error("|YouGame's address is invalid");
  const bool secure = c.nScheme == INTERNET_SCHEME_HTTPS;
  const std::wstring host(c.lpszHostName, c.dwHostNameLength);
  // Plain http only to this computer (the tests' stand-in).
  if (!secure && host != L"127.0.0.1" && host != L"localhost") throw std::runtime_error("|YouGame needs a secure connection");
  const std::wstring path = std::wstring(c.lpszUrlPath, c.dwUrlPathLength) + std::wstring(c.lpszExtraInfo ? c.lpszExtraInfo : L"", c.dwExtraInfoLength);
  HINTERNET session = WinHttpOpen(L"MeleeUnlockedLauncher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) throw std::runtime_error("|Networking is unavailable");
  HINTERNET conn = WinHttpConnect(session, host.c_str(), c.nPort, 0);
  HINTERNET req = conn ? WinHttpOpenRequest(conn, wide(method).c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0) : nullptr;
  Reply r;
  bool ok = false;
  if (req) {
    WinHttpSetTimeouts(req, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    std::string h;
    for (const auto& line : headers) h += line + "\r\n";
    const std::wstring wh = wide(h);
    ok = WinHttpSendRequest(req, wh.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wh.c_str(), wh.empty() ? 0 : (DWORD)-1,
                            (LPVOID)(body.empty() ? nullptr : body.data()), (DWORD)body.size(), (DWORD)body.size(), 0) &&
         WinHttpReceiveResponse(req, nullptr);
    if (ok) {
      DWORD status = 0, size = sizeof status;
      WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
      r.status = (int)status;
      char buf[8192];
      DWORD got = 0;
      while (WinHttpReadData(req, buf, sizeof buf, &got) && got) {
        r.body.append(buf, got);
        if (r.body.size() > kMaxRequest) { ok = false; break; }
      }
    }
    WinHttpCloseHandle(req);
  }
  if (conn) WinHttpCloseHandle(conn);
  WinHttpCloseHandle(session);
  if (!ok) throw std::runtime_error("|YouGame is not answering. Check your connection.");
  return r;
}

json parse_or_empty(const std::string& s) {
  json j = json::parse(s, nullptr, false);
  return j.is_discarded() ? json::object() : j;
}

// The site's error as "code|message" for the bridge (ticket_error reads the code).
[[noreturn]] void site_error(const Reply& r, const char* fallback) {
  const json j = parse_or_empty(r.body);
  const std::string code = j.is_object() && j.count("code") && j["code"].is_string() ? j["code"].get<std::string>() : "";
  std::string message = j.is_object() && j.count("error") && j["error"].is_string() ? j["error"].get<std::string>() : fallback;
  if (j.is_object() && j.count("error_description") && j["error_description"].is_string()) message = j["error_description"];
  throw std::runtime_error(code + "|" + message);
}

// ---- the account

std::string auth_headers_json(const Env& e) { return "apikey: " + e.anon_key; }

void apply_tokens(Session& s, const json& j) {
  s.access = j.value("access_token", "");
  s.refresh = j.value("refresh_token", s.refresh);
  const int64_t expires_in = j.value("expires_in", 3600);
  s.expires_at = j.count("expires_at") && j["expires_at"].is_number() ? j["expires_at"].get<int64_t>() : now_s() + expires_in;
  if (j.count("user") && j["user"].is_object()) {
    const json& u = j["user"];
    s.user_id = u.value("id", s.user_id);
    s.email = u.value("email", s.email);
    if (u.count("user_metadata") && u["user_metadata"].is_object()) {
      const json& m = u["user_metadata"];
      for (const char* key : {"display_name", "name", "user_name", "full_name"})
        if (m.count(key) && m[key].is_string() && !m[key].get<std::string>().empty()) { s.name = m[key]; break; }
    }
  }
}

std::function<void()> g_on_change;   // launcher.cpp: save launcher.ini (tokens rotate on every refresh)
void changed() { if (g_on_change) g_on_change(); }

// A fresh access token when the saved one is near its end. False: signed out (refresh refused).
bool ensure_fresh(Session& s, const Env& e) {
  if (s.refresh.empty()) return false;
  if (!s.access.empty() && s.expires_at - now_s() > 90) return true;
  const Reply r = http("POST", e.auth + "/auth/v1/token?grant_type=refresh_token",
                       {auth_headers_json(e), "Content-Type: application/json"}, json{{"refresh_token", s.refresh}}.dump());
  if (r.status != 200) return false;
  apply_tokens(s, parse_or_empty(r.body));
  return !s.access.empty();
}

// The site's session cookie, as its own page holds it (base64 JSON, split past 3180 characters).
std::string session_cookie(const Session& s, const Env& e) {
  const json session{{"access_token", s.access}, {"token_type", "bearer"}, {"expires_at", s.expires_at},
                     {"expires_in", std::max<int64_t>(0, s.expires_at - now_s())}, {"refresh_token", s.refresh},
                     {"user", {{"id", s.user_id}, {"email", s.email}}}};
  const std::string value = "base64-" + b64(session.dump(), true);
  constexpr size_t kChunk = 3180;
  if (value.size() <= kChunk) return e.cookie + "=" + value;
  std::string out;
  for (size_t i = 0, n = 0; i < value.size(); i += kChunk, ++n)
    out += (out.empty() ? "" : "; ") + e.cookie + "." + std::to_string(n) + "=" + value.substr(i, kChunk);
  return out;
}

Reply site(const std::string& method, const std::string& path, const std::string& body) {
  Session s;
  Env e;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    s = g_session;
    e = effective_env(g_env);
  }
  if (!ensure_fresh(s, e)) throw std::runtime_error("signed_out|Connect YouGame in the launcher to play YouGame modes.");
  bool rotated = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_session.user_id == s.user_id && g_session.refresh != s.refresh) { g_session = s; rotated = true; }
    else if (g_session.user_id == s.user_id) g_session.access = s.access, g_session.expires_at = s.expires_at;
  }
  if (rotated) changed();
  std::vector<std::string> headers{"Authorization: Bearer " + s.access, "Cookie: " + session_cookie(s, e),
                                   "Origin: " + e.site, "Referer: " + e.site + "/g/" + kSlug, "Accept: application/json"};
  if (!body.empty()) headers.push_back("Content-Type: application/json");
  return http(method, e.site + path, headers, body);
}

std::string fetch_code() {
  const Reply r = site("GET", "/api/multiplayer/code", "");
  const json j = parse_or_empty(r.body);
  if (r.status != 200 || !j.count("code") || !j["code"].is_string()) site_error(r, "YouGame did not give a connect code");
  return j["code"];
}

std::string hex(const uint8_t* p, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
  return s;
}
std::string sha256_file(const std::wstring& path) {
  std::ifstream f(std::filesystem::path(path), std::ios::binary);
  if (!f) return {};
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE h = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
  std::string out;
  if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
    std::vector<char> buf(1 << 20);
    while (f) {
      f.read(buf.data(), buf.size());
      if (f.gcount() > 0) BCryptHashData(h, (PUCHAR)buf.data(), (ULONG)f.gcount(), 0);
    }
    uint8_t digest[32];
    if (BCryptFinishHash(h, digest, sizeof digest, 0) == 0) out = hex(digest, sizeof digest);
    BCryptDestroyHash(h);
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return out;
}

// "1.0.12" > "1.0.9": compared number by number.
bool newer(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    long x = 0, y = 0;
    while (i < a.size() && a[i] != '.') x = x * 10 + (std::isdigit((unsigned char)a[i]) ? a[i] - '0' : 0), ++i;
    while (j < b.size() && b[j] != '.') y = y * 10 + (std::isdigit((unsigned char)b[j]) ? b[j] - '0' : 0), ++j;
    if (x != y) return x > y;
    ++i; ++j;
  }
  return false;
}

Backend real_backend() {
  Backend b;
  b.behind = [] { return behind(); };
  b.restart_older = [](const std::string& mode) {
    Allowed target;
    std::function<std::string(const Allowed&, const std::string&)> handler;
    { std::lock_guard<std::mutex> lock(g_mutex); target = g_behind; handler = g_restart_older; }
    if (target.version.empty()) return std::string("YouGame allows this version now. Search again.");
    return handler ? handler(target, mode) : std::string("This launcher cannot switch versions.");
  };
  b.account = [] { return account(); };
  b.player_id = [] { std::lock_guard<std::mutex> lock(g_mutex); return g_session.user_id; };
  b.code = [] {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_session.code.empty()) return json{{"code", g_session.code}}.dump();
    }
    const std::string code = fetch_code();
    { std::lock_guard<std::mutex> lock(g_mutex); g_session.code = code; }
    changed();
    return json{{"code", code}}.dump();
  };
  b.rank = [](const std::string& mode) {
    const Reply r = site("GET", std::string("/api/multiplayer/rank?slug=") + kSlug + "&mode=" + mode + "&self=1", "");
    if (r.status != 200) site_error(r, "Could not load your YouGame rank");
    return r.body;
  };
  b.ticket = [](const std::string& body) {
    const Reply r = site("POST", "/api/multiplayer/ticket", body);
    const json j = parse_or_empty(r.body);
    if (r.status != 200 || (j.is_object() && j.count("error"))) site_error(r, "YouGame could not find a match");
    return r.body;
  };
  return b;
}

// ---- the bridge server

std::string http_response(const BridgeReply& r) {
  return "HTTP/1.1 " + std::to_string(r.status) + (r.status == 200 ? " OK" : " Error") +
         "\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " +
         std::to_string(r.body.size()) + "\r\n\r\n" + r.body;
}

void serve(SOCKET s) {
#ifdef _WIN32
  DWORD timeout = 5000;
#else
  timeval timeout{5, 0};
#endif
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof timeout);
  std::string data;
  HttpRequest req;
  int state = 0;
  char buf[8192];
  while (state == 0) {
    const int n = recv(s, buf, sizeof buf, 0);
    if (n <= 0) break;
    data.append(buf, n);
    state = parse_request(data, req);
  }
  BridgeReply reply;
  if (state == 1) {
    std::vector<std::string> tokens;
    std::string build;
    uint16_t port;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      tokens.assign(g_tokens.begin(), g_tokens.end());
      build = g_build_json;
      port = g_port;
    }
    reply = handle(req, tokens, port, real_backend(), build);
  } else {
    reply = {400, R"({"ok":false,"error":"Bad request"})"};
  }
  const std::string out = http_response(reply);
  send(s, out.data(), (int)out.size(), 0);
  shutdown(s, SD_SEND);
  closesocket(s);
}

json error_json(const std::string& code_and_message) {
  const size_t bar = code_and_message.find('|');
  json j{{"ok", false}};
  if (bar == std::string::npos) { j["error"] = code_and_message; return j; }
  j["error"] = code_and_message.substr(bar + 1);
  if (bar > 0) j["code"] = code_and_message.substr(0, bar);
  return j;
}

}  // namespace

// ---------------------------------------------------------------- public

const Env& env() { static thread_local Env e; std::lock_guard<std::mutex> lock(g_mutex); e = effective_env(g_env); return e; }

void set_env(const std::string& id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const Env& next = id == "staging" ? kStaging : kProduction;
  if (next.id != g_env.id) g_session = Session{};
  g_env = next;
}

bool load_line(const std::string& line) {
  if (line.rfind("yougame_env=", 0) == 0) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_env = line.substr(12) == "staging" ? kStaging : kProduction;
    return true;
  }
  if (line.rfind("yougame_session=", 0) == 0) {
    const json j = parse_or_empty(unprotect(line.substr(16)));
    std::lock_guard<std::mutex> lock(g_mutex);
    if (j.is_object() && j.value("env", "") == g_env.id) {
      g_session.access = j.value("access", "");
      g_session.refresh = j.value("refresh", "");
      g_session.user_id = j.value("user", "");
      g_session.email = j.value("email", "");
      g_session.name = j.value("name", "");
      g_session.code = j.value("code", "");
      g_session.expires_at = j.value("expires_at", (int64_t)0);
    }
    return true;
  }
  return false;
}

std::string ini_lines() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::string out;
  if (g_env.id != "production") out += "yougame_env=" + g_env.id + "\n";
  if (!g_session.refresh.empty()) {
    const json j{{"env", g_env.id}, {"access", g_session.access}, {"refresh", g_session.refresh}, {"user", g_session.user_id},
                 {"email", g_session.email}, {"name", g_session.name}, {"code", g_session.code}, {"expires_at", g_session.expires_at}};
    const std::string sealed = protect(j.dump());
    if (!sealed.empty()) out += "yougame_session=" + sealed + "\n";
  }
  return out;
}

Account account() {
  std::lock_guard<std::mutex> lock(g_mutex);
  Account a;
  a.signed_in = !g_session.refresh.empty();
  a.email = g_session.email;
  a.code = g_session.code;
  // The name opponents see: the account's own, else the tag of its connect code. Never the email.
  a.name = !g_session.name.empty() ? g_session.name
         : !g_session.code.empty() ? g_session.code.substr(0, g_session.code.find('#')) : std::string("Player");
  return a;
}

bool sign_in(const std::string& email, const std::string& password, std::string* error) {
  try {
    Env e;
    { std::lock_guard<std::mutex> lock(g_mutex); e = effective_env(g_env); }
    const Reply r = http("POST", e.auth + "/auth/v1/token?grant_type=password", {auth_headers_json(e), "Content-Type: application/json"},
                         json{{"email", email}, {"password", password}}.dump());
    if (r.status != 200) {
      const json j = parse_or_empty(r.body);
      std::string why = j.value("error_description", j.value("msg", j.value("error", std::string())));
      if (why.empty() || r.status == 400) why = "That email and password did not sign in to YouGame.";
      if (error) *error = why;
      return false;
    }
    Session s;
    apply_tokens(s, parse_or_empty(r.body));
    if (s.access.empty() || s.user_id.empty()) { if (error) *error = "YouGame did not sign you in. Try again."; return false; }
    { std::lock_guard<std::mutex> lock(g_mutex); g_session = s; }
    try {
      const std::string code = fetch_code();
      std::lock_guard<std::mutex> lock(g_mutex);
      g_session.code = code;
    } catch (const std::exception&) {
      // Signed in; the connect code comes with the first hello or ticket instead.
    }
    changed();
    return true;
  } catch (const std::exception& ex) {
    const std::string m = ex.what();
    if (error) *error = m.substr(m.find('|') == std::string::npos ? 0 : m.find('|') + 1);
    return false;
  }
}

void sign_out() {
  { std::lock_guard<std::mutex> lock(g_mutex); g_session = Session{}; }
  changed();
}

void set_on_session_change(std::function<void()> callback) { g_on_change = std::move(callback); }

std::string game_page() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_env.site + "/g/" + kSlug;   // always the real site, never a test stand-in
}

bool start_bridge(std::string* error) {
  if (g_bridge_up) return true;
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { if (error) *error = "networking unavailable"; return false; }
  SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == INVALID_SOCKET) { if (error) *error = "no socket"; return false; }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
#ifdef _WIN32
  BOOL exclusive = TRUE;
  setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof exclusive);
  int len = sizeof addr;
#else
  socklen_t len = sizeof addr;
#endif
  if (bind(listener, (sockaddr*)&addr, sizeof addr) != 0 || listen(listener, 16) != 0 ||
      getsockname(listener, (sockaddr*)&addr, &len) != 0) {
    closesocket(listener);
    if (error) *error = "cannot listen on this computer";
    return false;
  }
  { std::lock_guard<std::mutex> lock(g_mutex); g_port = ntohs(addr.sin_port); }
  g_bridge_up = true;
  std::thread([listener] {
    for (;;) {
      SOCKET s = accept(listener, nullptr, nullptr);
      if (s == INVALID_SOCKET) continue;
      std::thread(serve, s).detach();
    }
  }).detach();
  return true;
}

std::string bridge_url() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_port ? "http://127.0.0.1:" + std::to_string(g_port) : std::string();
}

std::string new_launch_token() {
  uint8_t bytes[24];
  BCryptGenRandom(nullptr, bytes, sizeof bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  const std::string token = b64(std::string((const char*)bytes, sizeof bytes), true);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_tokens.push_front(token);
  while (g_tokens.size() > 4) g_tokens.pop_back();
  return token;
}

// The builds YouGame allows. Assumed route until YouGame names its own: GET /api/native/builds
// ?slug=melee-unlocked -> {"allowed":[{"version","exe_sha256","game_sha256"}]}. Anything else
// (no route, an error, no list) means nothing is known, and the game offers no other version.
std::vector<Allowed> allowed_builds() {
  std::vector<Allowed> out;
  try {
    Env e;
    { std::lock_guard<std::mutex> lock(g_mutex); e = effective_env(g_env); }
    const Reply r = http("GET", e.site + "/api/native/builds?slug=" + kSlug, {"Accept: application/json"}, "", 8000);
    const json j = parse_or_empty(r.body);
    if (r.status != 200 || !j.count("allowed") || !j["allowed"].is_array()) return out;
    for (const json& a : j["allowed"])
      if (a.is_object() && a.count("version") && a["version"].is_string())
        out.push_back({a["version"], a.value("exe_sha256", ""), a.value("game_sha256", "")});
  } catch (const std::exception&) {
  }
  return out;
}

void refresh_allowed(const std::string& current_version, const std::wstring& exe, const std::wstring& game_library) {
  const std::vector<Allowed> allowed = allowed_builds();
  const std::string exe_hash = sha256_file(exe), game_hash = sha256_file(game_library);
  Allowed offer;
  bool current_ok = allowed.empty();
  for (const Allowed& a : allowed) {
    if (a.exe_sha256 == exe_hash && a.game_sha256 == game_hash) current_ok = true;
    // Only a GitHub release this launcher can fetch: 1.0 and later, never this version itself.
    if (a.version != current_version && !newer("1.0.0", a.version) && (offer.version.empty() || newer(a.version, offer.version)))
      offer = a;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_allowed = allowed;
  g_behind = current_ok ? Allowed{} : offer;
}

Allowed behind() { std::lock_guard<std::mutex> lock(g_mutex); return g_behind; }
std::string file_sha256(const std::wstring& path) { return sha256_file(path); }

void set_restart_older(std::function<std::string(const Allowed&, const std::string&)> handler) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_restart_older = std::move(handler);
}

void set_build(const std::string& version, const std::wstring& exe, const std::wstring& game_library) {
  const json j{{"version", version}, {"exe_sha256", sha256_file(exe)}, {"game_sha256", sha256_file(game_library)}};
  std::lock_guard<std::mutex> lock(g_mutex);
  g_build_json = j.dump();
}

// ---------------------------------------------------------------- testable parts

int parse_request(const std::string& data, HttpRequest& out) {
  const size_t end = data.find("\r\n\r\n");
  if (end == std::string::npos) return data.size() > 16384 ? -1 : 0;
  out = HttpRequest{};
  size_t line_end = data.find("\r\n");
  const std::string first = data.substr(0, line_end);
  const size_t sp1 = first.find(' '), sp2 = first.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos || first.compare(sp2 + 1, 5, "HTTP/") != 0) return -1;
  out.method = first.substr(0, sp1);
  out.path = first.substr(sp1 + 1, sp2 - sp1 - 1);
  size_t pos = line_end + 2;
  while (pos < end) {
    const size_t e = data.find("\r\n", pos);
    const std::string h = data.substr(pos, e - pos);
    const size_t colon = h.find(':');
    if (colon == std::string::npos) return -1;
    std::string name = h.substr(0, colon), value = h.substr(colon + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    value.erase(0, value.find_first_not_of(" \t"));
    out.headers[name] = value;
    pos = e + 2;
  }
  size_t length = 0;
  if (out.headers.count("content-length")) {
    const std::string& v = out.headers["content-length"];
    if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos || v.size() > 8) return -1;
    length = std::stoul(v);
  }
  if (out.headers.count("transfer-encoding")) return -1;
  if (length > kMaxRequest) return -1;
  if (data.size() - (end + 4) < length) return 0;
  out.body = data.substr(end + 4, length);
  return 1;
}

BridgeReply handle(const HttpRequest& request, const std::vector<std::string>& tokens, uint16_t port,
                   const Backend& backend, const std::string& build_json) {
  auto reply = [](int status, const json& j) { return BridgeReply{status, j.dump()}; };
  // Only programs on this computer reach a 127.0.0.1 listener; a web page could still try, so a
  // request with a browser's Origin, or addressed to another host name, is refused.
  if (request.headers.count("origin")) return reply(403, {{"ok", false}, {"error", "Forbidden"}});
  const auto host = request.headers.find("host");
  const std::string p = std::to_string(port);
  if (host == request.headers.end() || (host->second != "127.0.0.1:" + p && host->second != "localhost:" + p))
    return reply(403, {{"ok", false}, {"error", "Forbidden"}});
  std::string path = request.path, query;
  if (const size_t q = path.find('?'); q != std::string::npos) { query = path.substr(q + 1); path.resize(q); }
  if (request.method == "GET" && path == "/") return reply(200, {{"ok", true}, {"bridge", "yougame"}, {"version", 1}});
  if (request.method != "POST" || path != "/sdk") return reply(404, {{"ok", false}, {"error", "Not found"}});
  std::string token;
  if (const auto a = request.headers.find("authorization"); a != request.headers.end() && a->second.rfind("Bearer ", 0) == 0)
    token = a->second.substr(7);
  else if (query.rfind("token=", 0) == 0)
    token = query.substr(6);
  if (token.empty() || std::find(tokens.begin(), tokens.end(), token) == tokens.end())
    return reply(401, {{"ok", false}, {"error", "Unauthorized"}});
  const json q = json::parse(request.body, nullptr, false);
  if (!q.is_object() || !q.count("type") || !q["type"].is_string()) return reply(400, {{"ok", false}, {"error", "Bad request"}});
  const std::string type = q["type"];
  const Account a = backend.account();
  try {
    if (type == "hello") {
      json result = json::object();
      if (a.signed_in) {
        result["player"] = {{"id", backend.player_id()}, {"name", a.name}};
        result["nameShared"] = true;
      }
      // YouGame is on an older release than this one: the game offers to restart in it.
      if (backend.behind) {
        const Allowed older = backend.behind();
        if (!older.version.empty()) result["behind"] = {{"version", older.version}};
      }
      return reply(200, {{"ok", true}, {"result", result}});
    }
    if (!a.signed_in && (type == "mpCode" || type == "mpTicket" || type == "mpRank"))
      return reply(200, {{"ok", false}, {"error", "Connect YouGame in the launcher to play YouGame modes."}, {"code", "signed_out"}});
    if (type == "mpCode") return reply(200, {{"ok", true}, {"result", parse_or_empty(backend.code())}});
    if (type == "mpRank") {
      const std::string mode = q.count("mode") && q["mode"].is_string() ? q["mode"].get<std::string>() : "singles";
      return reply(200, {{"ok", true}, {"result", parse_or_empty(backend.rank(mode))}});
    }
    if (type == "mpFriend") return reply(200, {{"ok", false}, {"error", "Enter a connect code"}});
    // Launcher only, never sent on to YouGame: restart in the release YouGame is on.
    if (type == "muRestartOlder") {
      const std::string mode = q.value("mode", std::string("ranked"));
      const std::string why = backend.restart_older ? backend.restart_older(mode == "direct" ? "direct" : "ranked")
                                                    : std::string("This launcher cannot switch versions.");
      if (why.empty()) return reply(200, {{"ok", true}, {"result", json::object()}});
      return reply(200, {{"ok", false}, {"error", why}});
    }
    if (type == "mpTicket") {
      const bool ranked = q.value("ranked", false);
      json body{{"slug", kSlug}, {"player_id", backend.player_id()}, {"name", a.name}, {"anonymous", false},
                {"players", q.value("players", 2)}, {"mode", q.value("mode", std::string("singles"))},
                {"queue", ranked ? "ranked" : "casual"}, {"build", parse_or_empty(build_json)}};
      if (q.count("lobby")) body["lobby"] = q["lobby"];
      if (q.count("direct") && q["direct"].is_string()) body["direct"] = q["direct"];
      if (q.count("room") && q["room"].is_string()) body["room"] = q["room"];
      if (q.value("afterMatch", std::string()) == "stay") body["after_match"] = "stay";
      const json answer = parse_or_empty(backend.ticket(body.dump()));
      json result = json::object();
      for (const char* key : {"url", "ticket", "room", "queue", "rating", "rank", "ice", "server", "direct", "player",
                              "bestOf", "winsRequired", "rulesVersion", "reason"})
        if (answer.is_object() && answer.count(key)) result[key] = answer[key];
      return reply(200, {{"ok", true}, {"result", result}});
    }
  } catch (const std::exception& e) {
    return reply(200, error_json(e.what()));
  }
  return reply(200, {{"ok", false}, {"error", "Not supported by the Melee Unlocked launcher"}, {"code", "unsupported"}});
}

}  // namespace launcher::yougame
