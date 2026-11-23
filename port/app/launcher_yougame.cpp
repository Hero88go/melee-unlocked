// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_yougame.h"
#include "launcher_yougame_app.h"
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
#include <iterator>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <unistd.h>   // gethostname
#endif

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

const Env kProduction{"production", "https://yougame.co"};
const Env kStaging{"staging", "https://staging.yougame.co"};

struct Session {
  // Signed in by code (YouGame's game sign-in, sign_in_code): the game's client id it was made with, the
  // refresh token (kept in launcher.ini, sealed) and the access token (memory only) with when it ends.
  bool token = false;
  std::string client, refresh, access;
  int64_t access_until = 0;   // seconds since the epoch
  // Signed in through the YouGame app (sign_in_app): who the app's player is, as the app said. The session
  // is the app's; this launcher can make no request with it (the game asks the app itself).
  bool app = false;
  std::string user_id, name, code;
};
std::map<std::string, std::string> g_clients;   // the game's sign-in client id per environment, as YouGame said
// launcher.ini yougame_app: 1 this launcher connected through the YouGame app (it then starts the app quietly
// when it starts), 0 Disconnect from an account the app gave, -1 neither.
int g_app_choice = -1;

std::mutex g_mutex;
// Production (owner 11-20: YouGame moved Melee Unlocked to production; staging had no test build of a new
// release, so its searches said "Test build not available"). launcher.ini yougame_env=staging switches back.
Env g_env = kProduction;
bool g_env_named = false;   // launcher.ini has a yougame_env line: the player's choice stands
Session g_session;
std::string g_build_json = "{}";
// The first GitHub release whose YouGame rows play in YouGame's pools: none older can be restarted into.
const char* const kFirstYouGameRelease = "0.9.212";
std::vector<Allowed> g_allowed;     // the builds YouGame allows, as last read
Allowed g_behind;                    // the release to offer, version empty when none
std::function<std::string(const Allowed&, const std::string&)> g_restart_older;
std::function<void()> g_open_connect;   // muOpenConnect (set_open_connect)
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

// Tests only (MELEE_LAUNCHER_TEST): the site may be a local stand-in.
Env effective_env(Env e) {
  if (!std::getenv("MELEE_LAUNCHER_TEST")) return e;
  if (const char* v = std::getenv("MELEE_YOUGAME_SITE")) e.site = v;
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

struct Reply { int status = 0; std::string body; int retry_after = 0; };   // retry_after: seconds, 0 when not said

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
#ifdef WINHTTP_QUERY_RETRY_AFTER
      DWORD retry = 0;
      size = sizeof retry;
      if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RETRY_AFTER | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &retry, &size,
                              WINHTTP_NO_HEADER_INDEX))
        r.retry_after = (int)std::min<DWORD>(retry, 3600);
#endif
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

// A string field of a site answer, or "" when it is missing or not a string.
std::string string_or_empty(const json& j, const char* key) {
  return j.is_object() && j.count(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
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

std::function<void()> g_on_change;   // launcher.cpp: save launcher.ini
std::atomic<bool> g_expired{false};   // YouGame ended the saved sign-in: the launcher asks for a new one
// launcher.ini still held a session of an earlier sign-in (the password sign-in's tokens, or the browser view's
// site session): not used, and saved again without it; the player signs in again by code.
bool g_old_session_line = false;
void changed() { if (g_on_change) g_on_change(); }

std::function<void()> g_save_now;    // launcher.cpp: write launcher.ini now (a new refresh token)
void save_now() {
  if (g_save_now) g_save_now();
  else changed();
}

[[noreturn]] void throw_signed_out() {
  throw std::runtime_error("signed_out|Connect YouGame in the launcher to play YouGame modes.");
}

// ---- YouGame's game sign-in (OAuth 2.0 device flow, RFC 8628; sdk.md "sign-in by code")
// Every request is the launcher's own (WinHTTP: no Origin or Sec-Fetch-Site header, as YouGame requires of a
// native program). Never logged: the device code, the access token or the refresh token.

std::string form(const std::string& v) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : v) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

Reply post_form(const std::string& url, const std::string& body) {
  return http("POST", url, {"Content-Type: application/x-www-form-urlencoded", "Accept: application/json"}, body, 15000);
}

bool valid_client_id(const std::string& id) {
  return !id.empty() && id.size() <= 64 && id.find_first_not_of("0123456789abcdefABCDEF-") == std::string::npos;
}

// The game's client id on this YouGame site: launcher.ini's, else YouGame's answer for melee-unlocked (then
// kept in launcher.ini). refetch: ask again (YouGame refused the kept one).
std::string client_id(const Env& e, bool refetch) {
  if (!refetch) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_clients.find(e.id);
    if (it != g_clients.end() && valid_client_id(it->second)) return it->second;
  }
  Reply r;
  try {
    r = http("GET", e.site + "/oauth/game-client?game=" + kSlug, {"Accept: application/json"}, "", 10000);
  } catch (const std::exception&) {
    throw std::runtime_error("|YouGame is not answering. Check your connection.");
  }
  const std::string id = string_or_empty(parse_or_empty(r.body), "client_id");
  if (r.status != 200 || !valid_client_id(id))
    throw std::runtime_error("|YouGame does not offer sign-in for Melee Unlocked here yet.");
  { std::lock_guard<std::mutex> lock(g_mutex); g_clients[e.id] = id; }
  changed();
  return id;
}

void revoke(const Env& e, const std::string& client, const std::string& refresh) {
  if (client.empty() || refresh.empty()) return;
  try { post_form(e.site + "/oauth/revoke", "token=" + form(refresh) + "&client_id=" + form(client)); } catch (const std::exception&) {}
}

// YouGame ended the saved sign-in: it goes, and the launcher asks for a new one (take_expired).
void drop_session(const std::string& refresh) {
  bool was;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    was = g_session.token && g_session.refresh == refresh;
    if (was) g_session = Session{};
  }
  if (was) { g_expired = true; changed(); }
}

std::mutex g_refresh_mutex;   // one refresh at a time

// An access token for the saved sign-in: the one held while it has over a minute left (and is not `refused`, a
// token YouGame just answered 401 to), else a refresh. Both tokens are new then: the new refresh token is in
// launcher.ini (save_now) before the new access token is used. Throws "signed_out|..." when there is no sign-in
// or YouGame ended it (invalid_grant: dropped), "|..." when YouGame did not answer.
std::string access_token(const std::string& refused = {}) {
  std::lock_guard<std::mutex> one(g_refresh_mutex);
  Session s;
  Env e;
  { std::lock_guard<std::mutex> lock(g_mutex); s = g_session; e = effective_env(g_env); }
  if (!s.token || s.refresh.empty()) throw_signed_out();
  if (!s.access.empty() && s.access != refused && s.access_until > now_s() + 60) return s.access;
  const std::string body = "grant_type=refresh_token&refresh_token=" + form(s.refresh) + "&client_id=" + form(s.client);
  Reply r;
  bool retried_lost = false;
  for (int busy = 0;;) {
    try {
      r = post_form(e.site + "/oauth/token", body);
    } catch (const std::exception&) {
      // A lost answer: once more with the same refresh token (YouGame gives the same new pair within 60 s).
      if (retried_lost) throw std::runtime_error("|YouGame is not answering. Check your connection.");
      retried_lost = true;
      continue;
    }
    if ((r.status == 429 || r.status == 503) && busy++ < 2) {   // keep the token; wait as YouGame says (briefly: requests wait on this)
      std::this_thread::sleep_for(std::chrono::seconds(std::clamp(r.retry_after, 1, 10)));
      continue;
    }
    break;
  }
  const json j = parse_or_empty(r.body);
  const std::string access = string_or_empty(j, "access_token"), refresh = string_or_empty(j, "refresh_token");
  if (r.status == 200 && !access.empty() && !refresh.empty()) {
    const int64_t lasts = j.count("expires_in") && j["expires_in"].is_number_integer() ? j["expires_in"].get<int64_t>() : 3600;
    bool still;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      still = g_session.token && g_session.refresh == s.refresh;
      if (still) {
        g_session.refresh = refresh;
        g_session.access = access;
        g_session.access_until = now_s() + std::max<int64_t>(lasts, 60);
      }
    }
    if (!still) { revoke(e, s.client, refresh); throw_signed_out(); }   // signed out meanwhile: the new pair ends too
    save_now();
    return access;
  }
  const std::string error = string_or_empty(j, "error");
  if (r.status == 400 && error == "invalid_grant") { drop_session(s.refresh); throw_signed_out(); }
  // The client id is no longer YouGame's: the sign-in goes, and the next one asks /oauth/game-client again.
  if (r.status == 401 || (r.status == 400 && error == "invalid_client")) {
    { std::lock_guard<std::mutex> lock(g_mutex); g_clients.erase(e.id); }
    drop_session(s.refresh);
    throw_signed_out();
  }
  throw std::runtime_error("|YouGame is not answering. Check your connection.");
}

// A request to YouGame with the launcher's sign-in (Authorization: Bearer). A 401 refreshes once and asks again.
Reply site(const std::string& method, const std::string& path, const std::string& body) {
  Env e;
  bool token;
  { std::lock_guard<std::mutex> lock(g_mutex); token = g_session.token; e = effective_env(g_env); }
  if (!token) throw_signed_out();
  const auto ask = [&](const std::string& access) {
    std::vector<std::string> headers{"Authorization: Bearer " + access, "Accept: application/json"};
    if (!body.empty()) headers.push_back("Content-Type: application/json");
    return http(method, e.site + path, headers, body, 20000);
  };
  std::string access = access_token();
  Reply r = ask(access);
  if (r.status == 401) {
    access = access_token(access);
    r = ask(access);
  }
  return r;
}

// {"code": the connect code, "account": the account's id (the site's own page gets both)}
json fetch_identity() {
  const Reply r = site("GET", "/api/multiplayer/code", "");
  const json j = parse_or_empty(r.body);
  if (r.status != 200 || !j.count("code") || !j["code"].is_string()) site_error(r, "YouGame did not give a connect code");
  return j;
}
std::string fetch_code() { return fetch_identity()["code"]; }

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
    if (!target.restart) return std::string("That version cannot be started from here.");
    return handler ? handler(target, mode) : std::string("This launcher cannot switch versions.");
  };
  // The bridge asks YouGame with the launcher's own sign-in (by code) only: an account the YouGame app gave can make
  // no request here, so the game hears "not signed in" and says to connect in the launcher.
  b.account = [] { Account a = account(); if (a.app) a.signed_in = false; return a; };
  b.open_connect = [] {
    std::function<void()> handler;
    { std::lock_guard<std::mutex> lock(g_mutex); handler = g_open_connect; }
    if (!handler) return false;
    handler();
    return true;
  };
  b.player_id = [] { std::lock_guard<std::mutex> lock(g_mutex); return g_session.user_id; };
  b.code = [] {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (!g_session.code.empty()) return json{{"code", g_session.code}}.dump();
    }
    const json identity = fetch_identity();
    const std::string code = identity["code"];
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_session.code = code;
      if (g_session.user_id.empty()) g_session.user_id = identity.value("account", std::string());
    }
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

bool g_extension = false;
bool extension() { std::lock_guard<std::mutex> lock(g_mutex); return g_extension; }
void set_extension(bool on) { std::lock_guard<std::mutex> lock(g_mutex); g_extension = on; }

bool load_line(const std::string& line) {
  if (line.rfind("yougame_extension=", 0) == 0) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_extension = line.substr(18) == "1";
    return true;
  }
  if (line.rfind("yougame_env=", 0) == 0) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_env = line.substr(12) == "staging" ? kStaging : kProduction;
    g_env_named = true;
    return true;
  }
  if (line.rfind("yougame_app=", 0) == 0) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_app_choice = line.substr(12) == "0" ? 0 : line.substr(12) == "1" ? 1 : -1;
    return true;
  }
  if (line.rfind("yougame_client_", 0) == 0) {   // yougame_client_<env>=<the game's client id there>
    const size_t eq = line.find('=');
    const std::string id = eq == std::string::npos ? std::string() : line.substr(eq + 1);
    const std::string which = eq == std::string::npos ? std::string() : line.substr(15, eq - 15);
    std::lock_guard<std::mutex> lock(g_mutex);
    if ((which == "production" || which == "staging") && valid_client_id(id)) g_clients[which] = id;
    return true;
  }
  if (line.rfind("yougame_session=", 0) == 0) {
    const json j = parse_or_empty(unprotect(line.substr(16)));
    std::lock_guard<std::mutex> lock(g_mutex);
    // Only a sign-in by code is a session. A line saved by an earlier sign-in (the password sign-in's tokens, the
    // browser view's site session) is not used and is not written back: the next save of launcher.ini drops it.
    if (j.is_object() && j.value("kind", "") == "code" && j.value("env", "") == g_env.id &&
        valid_client_id(j.value("client", "")) && !j.value("refresh", "").empty()) {
      g_session = Session{};
      g_session.token = true;
      g_session.client = j.value("client", "");
      g_session.refresh = j.value("refresh", "");
      g_session.user_id = j.value("user", "");
      g_session.name = j.value("name", "");
      g_session.code = j.value("code", "");
    } else {
      g_old_session_line = true;
    }
    return true;
  }
  return false;
}

std::string ini_lines() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::string out;
  if (g_env.id != "production" || g_env_named) out += "yougame_env=" + g_env.id + "\n";   // a named site stays named
  if (g_extension) out += "yougame_extension=1\n";
  if (g_app_choice >= 0) out += "yougame_app=" + std::to_string(g_app_choice) + "\n";
  for (const auto& [which, id] : g_clients) out += "yougame_client_" + which + "=" + id + "\n";
  // An account the YouGame app gave is not kept: the launcher asks the app again when it starts.
  if (g_session.token) {
    // The refresh token and who it is, sealed (DPAPI); the access token stays in memory.
    const json j{{"env", g_env.id}, {"kind", "code"}, {"client", g_session.client}, {"refresh", g_session.refresh},
                 {"user", g_session.user_id}, {"name", g_session.name}, {"code", g_session.code}};
    const std::string sealed = protect(j.dump());
    if (!sealed.empty()) out += "yougame_session=" + sealed + "\n";
  }
  return out;
}

Account account() {
  std::lock_guard<std::mutex> lock(g_mutex);
  Account a;
  a.signed_in = g_session.token || g_session.app;
  a.app = g_session.app && !g_session.token;
  a.code = g_session.code;
  // The name opponents see: the account's own, else the tag of its connect code.
  a.name = !g_session.name.empty() ? g_session.name
         : !g_session.code.empty() ? g_session.code.substr(0, g_session.code.find('#')) : std::string("Player");
  return a;
}

bool take_expired() { return g_expired.exchange(false); }

namespace {
std::atomic<int> g_code_run{0};   // the sign-in by code that may go on; cancel_sign_in_code moves it on

// "Melee Unlocked on <this computer's name>", at most 40 bytes (YouGame's device_label).
std::string device_label() {
  std::string pc;
#ifdef _WIN32
  wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
  DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
  if (GetComputerNameW(name, &size) && size) {
    pc.resize(WideCharToMultiByte(CP_UTF8, 0, name, (int)size, nullptr, 0, nullptr, nullptr));
    WideCharToMultiByte(CP_UTF8, 0, name, (int)size, pc.data(), (int)pc.size(), nullptr, nullptr);
  }
#else
  char name[256] = {};
  if (gethostname(name, sizeof name - 1) == 0) pc = name;
  pc = pc.substr(0, pc.find('.'));
#endif
  std::string label = "Melee Unlocked";
  if (pc.empty()) return label;
  label += " on ";
  size_t room = 40 - label.size();
  if (pc.size() > room) {
    while (room > 0 && ((unsigned char)pc[room] & 0xC0) == 0x80) --room;   // never half a UTF-8 character
    pc.resize(room);
  }
  return label + pc;
}

// Test runs: YOUGAME_TEST_CODE_SIGNIN names an absolute folder; each code goes to sign-in-code.json there.
std::filesystem::path test_code_file() {
#ifdef _WIN32
  const wchar_t* dir = _wgetenv(L"YOUGAME_TEST_CODE_SIGNIN");
#else
  const char* dir = std::getenv("YOUGAME_TEST_CODE_SIGNIN");
#endif
  if (!dir || !*dir) return {};
  const std::filesystem::path folder(dir);
  if (!folder.is_absolute()) return {};
  return folder / "sign-in-code.json";
}
void write_test_code(const std::filesystem::path& file, const std::string& link, const std::string& code) {
  if (file.empty()) return;
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  std::filesystem::path tmp = file;
  tmp += ".tmp";
  { std::ofstream out(tmp, std::ios::binary | std::ios::trunc); out << json{{"link", link}, {"code", code}}.dump(); }
  std::filesystem::rename(tmp, file, ec);
}
void drop_test_code(const std::filesystem::path& file, const std::string& code) {
  if (file.empty() || code.empty()) return;
  std::string held;
  { std::ifstream in(file, std::ios::binary); held.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); }
  if (string_or_empty(parse_or_empty(held), "code") != code) return;   // a newer run's code stays
  std::error_code ec;
  std::filesystem::remove(file, ec);
}

std::string error_text(const std::exception& e) {
  const std::string w = e.what();
  const size_t bar = w.find('|');
  return bar == std::string::npos ? w : w.substr(bar + 1);
}
}  // namespace

void cancel_sign_in_code() { ++g_code_run; }

CodeSignIn sign_in_code(const std::function<void(const std::string& code, const std::string& link, const std::string& open)>& on_code,
                        std::string* why) {
  std::string local;
  if (!why) why = &local;
  why->clear();
  const int run = ++g_code_run;
  const auto live = [&] { return g_code_run.load() == run; };
  // Waits `ms`, false when this sign-in was cancelled meanwhile.
  const auto wait = [&](int64_t ms) {
    for (int64_t waited = 0; waited < ms; waited += 250) {
      if (!live()) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return live();
  };
  Env e;
  { std::lock_guard<std::mutex> lock(g_mutex); e = effective_env(g_env); }
  g_expired = false;
  const std::filesystem::path test_file = test_code_file();
  std::string shown;   // the code this run showed (and wrote for a test)
  struct Tidy { std::function<void()> f; ~Tidy() { f(); } } tidy{[&] { drop_test_code(test_file, shown); }};
  std::string client;
  try {
    client = client_id(e, false);
  } catch (const std::exception& x) {
    *why = error_text(x);
    return CodeSignIn::kFailed;
  }
  const std::string label = device_label();
  bool refetched = false;
  // An expired code is replaced by a new one, in the same box, a few times (about half an hour in all).
  for (int round = 0; round < 3;) {
    Reply start;
    try {
      start = post_form(e.site + "/oauth/device_authorization", "client_id=" + form(client) + "&scope=player&device_label=" + form(label));
    } catch (const std::exception&) {
      *why = "YouGame is not answering. Check your connection.";
      return CodeSignIn::kFailed;
    }
    if (!live()) return CodeSignIn::kCancelled;
    if (start.status == 401 && !refetched) {   // the client id launcher.ini kept is no longer YouGame's: ask again
      refetched = true;
      try { client = client_id(e, true); } catch (const std::exception& x) { *why = error_text(x); return CodeSignIn::kFailed; }
      continue;
    }
    const json j = parse_or_empty(start.body);
    const std::string device = string_or_empty(j, "device_code"), code = string_or_empty(j, "user_code");
    if (start.status != 200 || device.empty() || code.empty() || code.size() > 16) {
      *why = start.status == 429 ? "YouGame has had too many sign-ins from this network. Try again in a few minutes."
           : start.status == 401 ? "YouGame does not offer sign-in for Melee Unlocked right now."
           : "YouGame could not start a sign-in. Try again.";
      return CodeSignIn::kFailed;
    }
    // Where to enter it: YouGame's own page on this site, never another address.
    std::string link = string_or_empty(j, "verification_uri");
    if (link.rfind(e.site + "/", 0) != 0 || link.size() > 120) link = e.site + "/link";
    // The page to open in the browser: the same with the code filled in, so a player who has to sign in to the site
    // first still lands on it with the code entered. Only YouGame's own page; otherwise the bare link.
    std::string open = string_or_empty(j, "verification_uri_complete");
    if (open.rfind(e.site + "/", 0) != 0 || open.size() > 160) open = link;
    const int64_t lasts = j.count("expires_in") && j["expires_in"].is_number_integer() ? std::clamp<int64_t>(j["expires_in"].get<int64_t>(), 60, 1800) : 600;
    int64_t interval = j.count("interval") && j["interval"].is_number_integer() ? std::clamp<int64_t>(j["interval"].get<int64_t>(), 1, 60) : 5;
    drop_test_code(test_file, shown);
    shown = code;
    if (on_code) on_code(code, link, open);
    write_test_code(test_file, link, code);
    const auto ends = std::chrono::steady_clock::now() + std::chrono::seconds(lasts);
    const std::string poll = "grant_type=" + form("urn:ietf:params:oauth:grant-type:device_code") + "&device_code=" + form(device) +
                             "&client_id=" + form(client);
    int64_t extra = 0;   // a busy YouGame's retry-after, once
    bool again = false;  // the code ran out (or YouGame no longer knows it): a new one
    for (;;) {
      if (!wait((interval + extra) * 1000)) return CodeSignIn::kCancelled;
      extra = 0;
      if (std::chrono::steady_clock::now() >= ends) { again = true; break; }
      Reply r;
      try { r = post_form(e.site + "/oauth/token", poll); } catch (const std::exception&) { continue; }   // no answer: keep polling
      const json t = parse_or_empty(r.body);
      if (!live()) {   // cancelled while YouGame answered: a sign-in it gave just now ends there too
        if (r.status == 200) revoke(e, client, string_or_empty(t, "refresh_token"));
        return CodeSignIn::kCancelled;
      }
      const std::string error = string_or_empty(t, "error");
      if (r.status == 200) {
        const std::string access = string_or_empty(t, "access_token"), refresh = string_or_empty(t, "refresh_token");
        if (access.empty() || refresh.empty()) { *why = "YouGame could not finish the sign-in. Try again."; return CodeSignIn::kFailed; }
        const int64_t valid = t.count("expires_in") && t["expires_in"].is_number_integer() ? t["expires_in"].get<int64_t>() : 3600;
        Session s;
        s.token = true;
        s.client = client;
        s.refresh = refresh;
        s.access = access;
        s.access_until = now_s() + std::max<int64_t>(valid, 60);
        bool same_site;
        {
          std::lock_guard<std::mutex> lock(g_mutex);
          same_site = g_env.id == e.id;
          if (same_site) g_session = s;
        }
        if (!same_site) {   // the launcher moved to the other YouGame meanwhile: this sign-in is not kept
          revoke(e, client, refresh);
          *why = "YouGame changed meanwhile. Press Connect again.";
          return CodeSignIn::kFailed;
        }
        save_now();
        // Who it is: the account's connect code, id and gamer tag (a later request asks again when this fails).
        try {
          const json identity = fetch_identity();
          std::lock_guard<std::mutex> lock(g_mutex);
          if (g_session.token && g_session.refresh == refresh) {
            g_session.code = identity["code"];
            g_session.user_id = string_or_empty(identity, "account");
            g_session.name = string_or_empty(identity, "name");
          }
        } catch (const std::exception&) {
        }
        changed();
        return CodeSignIn::kSignedIn;
      }
      if (r.status == 503 || r.status == 429) { extra = std::clamp(r.retry_after, 1, 60); continue; }
      if (r.status >= 500) continue;   // a passing server error: keep polling until the code runs out
      if (error == "authorization_pending") continue;
      if (error == "slow_down") {
        const int64_t said = t.count("interval") && t["interval"].is_number_integer() ? t["interval"].get<int64_t>() : 0;
        interval = std::min<int64_t>(said > interval ? said : interval + 5, 120);
        continue;
      }
      if (error == "access_denied") { *why = "The sign-in was declined on YouGame. Press Connect to try again."; return CodeSignIn::kDenied; }
      if (error == "expired_token" || error == "invalid_grant") { again = true; break; }
      *why = r.status == 401 ? "YouGame does not offer sign-in for Melee Unlocked right now." : "YouGame could not finish the sign-in. Try again.";
      return CodeSignIn::kFailed;
    }
    if (again) ++round;
  }
  *why = "The sign-in code ran out. Press Connect to get a new one.";
  return CodeSignIn::kExpired;
}

void warm() {
  { std::lock_guard<std::mutex> lock(g_mutex); if (!g_session.token) return; }
  std::thread([] {
    try {
      // YouGame's answer for this sign-in: the connect code (a refresh first when the access token is old; it
      // also says when YouGame ended the sign-in).
      const json identity = fetch_identity();
      bool differs;
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_session.token) return;
        const std::string code = identity["code"];
        differs = g_session.code != code;
        g_session.code = code;
        if (identity.count("account") && identity["account"].is_string()) g_session.user_id = identity["account"];
        // The gamer tag (a site older than this field sends none: the name stays as it was).
        const std::string name = string_or_empty(identity, "name");
        if (!name.empty()) { differs = differs || g_session.name != name; g_session.name = name; }
      }
      if (differs) changed();
    } catch (const std::exception&) {
    }
  }).detach();
}

bool app_expected() {
  bool staging;
  { std::lock_guard<std::mutex> lock(g_mutex); staging = g_env.id == "staging"; }
  return app::running(staging) || app::installed(staging);
}

bool start_app_quietly(std::string* detail) {
  auto say = [&](const std::string& s) { if (detail) *detail = s; };
  if (std::getenv("MELEE_LAUNCHER_TEST") && !std::getenv("MELEE_LAUNCHER_TEST_YOUGAME_APP")) { say("test run: the YouGame app is not started"); return false; }
  bool staging;
  { std::lock_guard<std::mutex> lock(g_mutex); staging = g_env.id == "staging"; }
  const std::string name = staging ? "YouGame Staging" : "YouGame";
  if (app::running(staging)) { say("the " + name + " app is running"); return true; }
  if (!app::installed(staging)) { say("the " + name + " app is not installed, so the extension has nothing to show"); return false; }
  const auto t0 = std::chrono::steady_clock::now();
  const bool up = app::start(staging, 25000);
  const long long ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  say("the " + name + " app was started quietly (tray); its channel " + (up ? "came up after " : "did not come up in ") + std::to_string(ms) + " ms");
  return up;
}

// This launcher's YouGame app is running (its channel's pipe is there); never the other environment's.
bool app_running() {
  bool staging;
  { std::lock_guard<std::mutex> lock(g_mutex); staging = g_env.id == "staging"; }
  return app::running(staging);
}

// The extension's app for this launcher's YouGame is neither installed nor running: what the launcher tells the
// player (empty when the app is there). Player report 11-21: a computer with only the YouGame Staging app got no
// extension and no word why; since 0.9.993 the launcher and game use YouGame (production) only, so the other
// environment's app is never used for the extension.
std::string extension_app_missing() {
  bool staging;
  { std::lock_guard<std::mutex> lock(g_mutex); staging = g_env.id == "staging"; }
  if (app::installed(staging) || app::running(staging)) return {};
  const std::string name = staging ? "YouGame Staging" : "YouGame";
  const bool other = app::installed(!staging) || app::running(!staging);
  return "Launch with YouGame extension needs the " + name + " app" +
         (other ? std::string(". This computer has only ") + (staging ? "YouGame" : "YouGame Staging") + ", which this version does not use."
                : ", which is not installed.") +
         " YouGame matches work without it.\n\nOpen the page where the " + name + " app downloads?";
}

bool adopt_staging_app() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_env_named || g_env.id != "production") return false;
  }
  // Test runs only (MELEE_LAUNCHER_TEST): MELEE_LAUNCHER_TEST_YG_APPS=staging|production|both|none says which
  // YouGame apps this computer has, instead of the registry.
  const char* apps = std::getenv("MELEE_LAUNCHER_TEST") ? std::getenv("MELEE_LAUNCHER_TEST_YG_APPS") : nullptr;
  const std::string a = apps ? apps : "";
  const bool production = apps ? (a == "production" || a == "both") : (app::installed(false) || app::running(false));
  const bool staging = apps ? (a == "staging" || a == "both") : (app::installed(true) || app::running(true));
  if (production || !staging) return false;
  set_env("staging");
  std::lock_guard<std::mutex> lock(g_mutex);
  g_env_named = true;
  return true;
}

bool app_declined() { std::lock_guard<std::mutex> lock(g_mutex); return g_app_choice == 0; }
bool app_used_before() { std::lock_guard<std::mutex> lock(g_mutex); return g_app_choice == 1; }

AppSignIn sign_in_app(bool may_start, const std::string& version, std::string* detail) {
  // Every step goes into *detail ("; " between), for the launcher's log.
  std::string steps;
  auto step = [&](const std::string& s) { steps += (steps.empty() ? "" : "; ") + s; if (detail) *detail = steps; };
  Env e;
  { std::lock_guard<std::mutex> lock(g_mutex); e = g_env; }
  // Test runs (MELEE_LAUNCHER_TEST) never ask the player's own app unless MELEE_LAUNCHER_TEST_YOUGAME_APP=1.
  if (std::getenv("MELEE_LAUNCHER_TEST") && !std::getenv("MELEE_LAUNCHER_TEST_YOUGAME_APP")) { step("test run"); return AppSignIn::kNoApp; }
  const bool staging = e.id == "staging";
  const char* app_name = staging ? "YouGame Staging" : "YouGame";
  if (app::running(staging)) {
    step(std::string(app_name) + " app running");
  } else {
    if (!app::installed(staging)) { step(std::string(app_name) + " app not installed"); return AppSignIn::kNoApp; }
    if (!may_start) { step(std::string(app_name) + " app installed, not running (not started)"); return AppSignIn::kNoApp; }
    step(std::string(app_name) + " app installed, not running: starting it quietly (tray)");
    const auto t0 = std::chrono::steady_clock::now();
    const bool up = app::start(staging, 25000);
    const long long ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (!up) { step("its channel did not come up in " + std::to_string(ms) + " ms"); return AppSignIn::kNoAnswer; }
    step("its channel came up after " + std::to_string(ms) + " ms");
  }
  const app::Answer a = app::ask(staging, version);
  switch (a.state) {
    case app::Answer::kSignedIn: {
      Session s;
      s.app = true;
      s.user_id = a.player_id;
      s.name = a.name;
      s.code = a.code;
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_session.token || g_env.id != e.id) { step("signed in on this launcher meanwhile"); return AppSignIn::kSignedIn; }
        g_session = s;
        g_app_choice = 1;   // the launcher starts the app quietly next time, to take this account again
      }
      changed();
      step(std::string("the app says signed in") + (a.code.empty() ? " (no connect code)" : ""));
      return AppSignIn::kSignedIn;
    }
    case app::Answer::kSignedOut: step("the app says signed out"); return AppSignIn::kSignedOut;
    case app::Answer::kNotRunning: step("the app's channel is gone"); return AppSignIn::kNoApp;
    case app::Answer::kRefused: step("the app refused (" + a.why + ")"); return AppSignIn::kNoAnswer;
    case app::Answer::kNoAnswer: break;
  }
  step("the app did not answer (" + a.why + ")");
  return AppSignIn::kNoAnswer;
}

void sign_out() {
  Session was;
  Env e;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    was = g_session;
    e = effective_env(g_env);
    // An account the app gave: the launcher stops taking it by itself (Connect takes it again).
    if (g_session.app && !g_session.token) g_app_choice = 0;
    g_session = Session{};
  }
  changed();
  // The app keeps its own sign-in (this launcher held nothing of it); a sign-in by code ends on YouGame too.
  if (was.token) revoke(e, was.client, was.refresh);
}

void set_save_now(std::function<void()> callback) { g_save_now = std::move(callback); }

void set_on_session_change(std::function<void()> callback) {
  g_on_change = std::move(callback);
  // launcher.ini is written again at once when it still held an earlier sign-in's session (dropped).
  bool old;
  { std::lock_guard<std::mutex> lock(g_mutex); old = g_old_session_line; g_old_session_line = false; }
  if (old) changed();
}

std::string game_page() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_env.site + "/g/" + kSlug;   // always the real site, never a test stand-in
}

// Open YouGame (owner 11-19: the app, not the site): the installed YouGame app for this environment
// (YouGame Staging for staging), at the Melee Unlocked page; without that app, the same site's desktop
// page, where the app downloads (staging.yougame.co/desktop for staging, as the game's Get YouGame).
std::string open_target() {
  std::string id, site;
  { std::lock_guard<std::mutex> lock(g_mutex); id = g_env.id; site = g_env.site; }
  const std::string scheme = id == "staging" ? "yougame-staging" : "yougame";
#ifdef _WIN32
  const std::string user = "Software\\Classes\\" + scheme + "\\shell\\open\\command", root = scheme + "\\shell\\open\\command";
  HKEY h = nullptr;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, user.c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS ||
      RegOpenKeyExA(HKEY_CLASSES_ROOT, root.c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS) {
    RegCloseKey(h);
    return scheme + "://g/" + kSlug;
  }
#endif
  return site + "/desktop";   // Linux: the site's desktop page (no app registry to ask)
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

// Until the site has that route: the release YouGame's own Melee Unlocked build sits on, read from its
// public source releases (tags "v<our version>-yg<its build>", newest first). One entry, no hashes:
// the version alone says whether this build shares YouGame's pool (the pool key carries it).
// YouGame's own record of this release (its listing's buildId): the site takes a Doubles home only from a build
// it lists (native_build). Its passive sync lists every Melee Unlocked GitHub release; the public listing page
// names each record as {"buildId","version","platform"}, the version being ours or ours with a suffix
// ("0.9.217-online-test.122.24"). Empty until the listing has this release.
// The page's build records, read once: {"buildId","version","platform"} each, escaped inside its script.
std::vector<ListedBuild> listed_builds(const std::string& page) {
  std::string b;
  b.reserve(page.size());
  for (char c : page) if (c != '\\') b += c;
  std::vector<ListedBuild> out;
  static const std::string kId = "\"buildId\":\"", kVersion = "\",\"version\":\"", kPlatform = "\",\"platform\":\"";
  for (size_t at = b.find(kId); at != std::string::npos; at = b.find(kId, at + kId.size())) {
    const size_t id = at + kId.size();
    if (id + 36 + kVersion.size() > b.size()) break;
    if (b.compare(id + 36, kVersion.size(), kVersion) != 0) continue;
    const size_t v = id + 36 + kVersion.size(), v_end = b.find('"', v);
    if (v_end == std::string::npos || v_end - v > 48 || b.compare(v_end, kPlatform.size(), kPlatform) != 0) continue;
    const size_t p = v_end + kPlatform.size(), p_end = b.find('"', p);
    if (p_end == std::string::npos || p_end - p > 16) continue;
    out.push_back({b.substr(id, 36), b.substr(v, v_end - v), b.substr(p, p_end - p)});
  }
  return out;
}
// The native_build a ticket names, as the YouGame app's page names it: YouGame's record of this release for
// this platform, so app players and launcher players on one release meet in one queue. Never another
// build's record: a release YouGame does not list yet names none and meets launcher players only. "" then.
std::string native_build_from_listing(const std::string& page, const std::string& version, const char* platform) {
  std::string found;
  for (const ListedBuild& r : listed_builds(page))   // the page lists oldest first: keep the newest
    if (r.platform == platform && (r.version == version || r.version.rfind(version + "-", 0) == 0)) found = r.id;
  return found;
}
std::string listed_build_id(const std::string& version) {
  static std::mutex m;
  static std::string cached_for, cached_id;
  static uint64_t cached_at = 0;
  const uint64_t now = (uint64_t)GetTickCount64();
  {
    std::lock_guard<std::mutex> lock(m);
    if (cached_for == version && now - cached_at < 60000) return cached_id;   // read again once a minute
  }
  std::string found;
  bool read = false;
  try {
    Env e;
    { std::lock_guard<std::mutex> lock(g_mutex); e = effective_env(g_env); }
    const Reply r = http("GET", e.site + "/g/" + kSlug, {"Accept: text/html", "User-Agent: MeleeUnlocked-Launcher"}, "", 8000);
    if (r.status == 200) {
#ifdef _WIN32
      found = native_build_from_listing(r.body, version, "windows");
#else
      found = native_build_from_listing(r.body, version, "linux");
#endif
      read = true;
    }
  } catch (const std::exception&) {
  }
  std::lock_guard<std::mutex> lock(m);
  if (read || cached_for != version) { cached_id = found; }   // a failed read keeps the last answer
  cached_for = version; cached_at = now;
  return cached_id;
}

// The release whose pool this build's YouGame rows search (owner 11-19: a new release plays YouGame at once,
// with whoever is on the build YouGame hands out). YouGame's own builds before 0.9.990 key their rooms by
// their release ("mu-0.9.218-p2" for 0.9.218-online-test.122.31), so while the newest Windows build the site lists is one
// of those, this build searches under that version. From 0.9.990 on every release shares one key
// (yougame_compatibility.h pool_version): "" here, as when the page cannot be read.
// Test launchers only (MELEE_LAUNCHER_TEST): MELEE_LAUNCHER_TEST_YOUGAME_POOL names the version, "any" the shared key.
std::vector<int> version_numbers(const std::string& v) {
  std::vector<int> out;
  for (size_t i = 0; i < v.size();) {
    if (!std::isdigit((unsigned char)v[i])) { ++i; continue; }
    int n = 0;
    for (; i < v.size() && std::isdigit((unsigned char)v[i]); ++i) n = n * 10 + (v[i] - '0');
    out.push_back(n);
  }
  return out;
}
std::string pool_from_listing(const std::string& b) {
  std::string newest;
  for (size_t at = b.find("buildId"); at != std::string::npos; at = b.find("buildId", at + 7)) {
    std::string w;
    for (size_t i = at; i < b.size() && w.size() < 260; ++i) if (b[i] != '\\') w += b[i];
    auto field = [&](const char* key) {
      const std::string k = std::string("\"") + key + "\":\"";
      const size_t p = w.find(k);
      if (p == std::string::npos) return std::string();
      const size_t s = p + k.size(), e2 = w.find('"', s);
      return e2 == std::string::npos ? std::string() : w.substr(s, e2 - s);
    };
    const std::string v = field("version");
    // One pool for every platform of this build: the Windows listing names it.
    // (The window starts inside this record's "buildId" key, so only the fields after it are read.)
    if (w.find("\"version\":\"") > 60 || field("platform") != "windows" || v.empty() || v.size() > 48) continue;
    if (v.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-") != std::string::npos) continue;
    if (version_numbers(v).size() < 3) continue;
    if (newest.empty() || version_numbers(newest) < version_numbers(v)) newest = v;
  }
  if (newest.empty()) return {};
  const std::vector<int> n = version_numbers(newest);
  const std::vector<int> shared{0, 9, 990};
  if (!(std::vector<int>(n.begin(), n.begin() + 3) < shared)) return {};   // 0.9.990 on: the shared key
  // Those builds key their rooms by the release alone ("mu-0.9.218-p2"): the label after it
  // ("-online-test.122.31") names the build, not the pool.
  return newest.substr(0, newest.find('-'));
}
// Whether the last listed_pool_version() read YouGame's page (a failed read also answers "").
std::atomic<bool> g_listing_read{false};
std::string listed_pool_version() {
  g_listing_read = false;
  if (const char* forced = std::getenv("MELEE_LAUNCHER_TEST") ? std::getenv("MELEE_LAUNCHER_TEST_YOUGAME_POOL") : nullptr) { g_listing_read = true; return std::string(forced) == "any" ? std::string() : forced; }
  try {
    Env e;
    { std::lock_guard<std::mutex> lock(g_mutex); e = effective_env(g_env); }
    const Reply r = http("GET", e.site + "/g/" + kSlug, {"Accept: text/html", "User-Agent: MeleeUnlocked-Launcher"}, "", 8000);
    if (r.status != 200) return {};
    g_listing_read = true;
    return pool_from_listing(r.body);
  } catch (const std::exception&) {
    return {};
  }
}

std::vector<Allowed> allowed_from_releases() {
  std::vector<Allowed> out;
  try {
    const Reply r = http("GET", "https://api.github.com/repos/yougame-co/melee-unlocked-public/releases?per_page=5",
                         {"Accept: application/vnd.github+json", "User-Agent: MeleeUnlocked-Launcher"}, "", 8000);
    const json j = parse_or_empty(r.body);
    if (r.status != 200 || !j.is_array()) return out;
    for (const json& rel : j) {
      if (!rel.is_object() || rel.value("draft", false)) continue;
      const std::string tag = rel.value("tag_name", "");
      const size_t cut = tag.find("-yg");
      if (tag.size() < 2 || tag[0] != 'v' || cut == std::string::npos) continue;
      const std::string version = tag.substr(1, cut - 1);
      if (version.empty() || version.find_first_not_of("0123456789.") != std::string::npos) continue;
      out.push_back({version, "", ""});
      break;   // the newest one is the one YouGame is on
    }
  } catch (const std::exception&) {
  }
  return out;
}

void pool_refresh();
void refresh_allowed(const std::string& current_version, const std::wstring& exe, const std::wstring& game_library) {
  pool_refresh();   // which pool this build's YouGame rows search
  std::vector<Allowed> allowed = allowed_builds();
  if (allowed.empty()) allowed = allowed_from_releases();
  const std::string exe_hash = sha256_file(exe), game_hash = sha256_file(game_library);
  Allowed offer;
  bool current_ok = allowed.empty();   // nothing known: the search goes ahead as it is
  // A build newer than every version YouGame names searches too (owner 11-18: a new release plays at once).
  // The pool key carries the version, so it meets only players on this same release until YouGame's own
  // build catches up; nothing older is offered.
  bool newest = !allowed.empty();
  for (const Allowed& a : allowed) if (!newer(current_version, a.version)) newest = false;
  if (newest) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_allowed = allowed;
    g_behind = Allowed{};
    return;
  }
  for (Allowed a : allowed) {
    // With hashes: this exact build. Without (the release list): this version.
    if (a.exe_sha256.empty() ? a.version == current_version : (a.exe_sha256 == exe_hash && a.game_sha256 == game_hash)) current_ok = true;
    if (a.version == current_version || !(offer.version.empty() || newer(a.version, offer.version))) continue;
    // Restart only into a GitHub release this launcher can fetch that plays YouGame the same way
    // (kFirstYouGameRelease and later) and is older than this one. Otherwise the game only says
    // which version YouGame is on.
    a.restart = !newer(kFirstYouGameRelease, a.version) && newer(current_version, a.version);
    offer = a;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_allowed = allowed;
  // Owner 11-19: every release shares one YouGame pool (yougame_compatibility.h pool_version), so a
  // version YouGame does not list still plays: nothing is reported behind.
  (void)current_ok; (void)offer;
  g_behind = Allowed{};
}

// The pool the game is told (bridge hello). Read when the launcher learns its build (refresh_allowed) and
// again in the background once a minute while hellos keep coming: YouGame can list a new build while the
// launcher stays open. Never read before refresh_allowed (unit tests serve hellos with no network).
std::mutex g_pool_mutex;
std::string g_pool;
uint64_t g_pool_at = 0;
bool g_pool_on = false, g_pool_busy = false;
bool g_pool_read = false;   // the pool came from a real read of YouGame's page (not a failed one)
void pool_refresh() {
  const std::string v = listed_pool_version();
  const bool read = g_listing_read.load();
  std::lock_guard<std::mutex> lock(g_pool_mutex);
  // A failed read keeps the last real answer; without one the pool stays "" (the shared key) but unread.
  if (read || !g_pool_read) g_pool = v;
  g_pool_read = g_pool_read || read;
  g_pool_at = (uint64_t)GetTickCount64(); g_pool_on = true; g_pool_busy = false;
}
std::string pool_now() {
  std::lock_guard<std::mutex> lock(g_pool_mutex);
  if (g_pool_on && !g_pool_busy && (uint64_t)GetTickCount64() - g_pool_at >= 60000) {
    g_pool_busy = true;
    std::thread(pool_refresh).detach();
  }
  return g_pool;
}
bool listing_known() {
  std::lock_guard<std::mutex> lock(g_pool_mutex);
  return g_pool_on;
}
bool listed_current() {
  (void)pool_now();   // a stale answer is read again in the background, as for the bridge's hello
  std::lock_guard<std::mutex> lock(g_pool_mutex);
  return g_pool_on && g_pool_read && g_pool.empty();   // unread is never "listed": app-backed would be refused
}

Allowed behind() { std::lock_guard<std::mutex> lock(g_mutex); return g_behind; }
std::string file_sha256(const std::wstring& path) { return sha256_file(path); }

void set_restart_older(std::function<std::string(const Allowed&, const std::string&)> handler) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_restart_older = std::move(handler);
}
void set_open_connect(std::function<void()> handler) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_open_connect = std::move(handler);
}

void set_build(const std::string& version, const std::wstring& exe, const std::wstring& game_library) {
  const json j{{"version", version}, {"exe_sha256", sha256_file(exe)}, {"game_sha256", sha256_file(game_library)}};
  std::lock_guard<std::mutex> lock(g_mutex);
  g_build_json = j.dump();
}

// A room code as YouGame's app makes them: 12 lowercase letters and digits, none that read alike.
std::string new_room_code() {
  static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
  uint8_t bytes[12];
  BCryptGenRandom(nullptr, bytes, sizeof bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  std::string code;
  for (uint8_t b : bytes) code += alphabet[b % (sizeof alphabet - 1)];
  return code;
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
        if (!older.version.empty()) result["behind"] = {{"version", older.version}, {"restart", older.restart}};
      }
      // The version whose rooms the game searches ("" or absent: every release's shared key).
      if (const std::string pool = pool_now(); !pool.empty()) result["pool"] = pool;
      return reply(200, {{"ok", true}, {"result", result}});
    }
    // A YouGame row pressed while the launcher is not signed in: its own Connect opens (or comes to the front).
    // Needs only the launch token: the player has no YouGame sign-in here yet.
    if (type == "muOpenConnect") {
      if (a.signed_in) return reply(200, {{"ok", true}, {"result", {{"signedIn", true}}}});
      return reply(200, {{"ok", true}, {"result", {{"opening", backend.open_connect ? backend.open_connect() : false}}}});
    }
    if (!a.signed_in && (type == "mpCode" || type == "mpTicket" || type == "mpRank"))
      return reply(200, {{"ok", false}, {"error", "Connect YouGame in the launcher to play YouGame modes."}, {"code", "signed_out"}});
    if (type == "mpCode") return reply(200, {{"ok", true}, {"result", parse_or_empty(backend.code())}});
    if (type == "mpRank") {
      const std::string mode = q.count("mode") && q["mode"].is_string() ? q["mode"].get<std::string>() : "singles";
      return reply(200, {{"ok", true}, {"result", parse_or_empty(backend.rank(mode))}});
    }
    // The ticket answer's parts the game reads, and nothing else of it.
    const auto ticket_result = [](const json& answer) {
      json result = json::object();
      for (const char* key : {"url", "ticket", "room", "queue", "rating", "rank", "ice", "server", "direct", "player",
                              "bestOf", "winsRequired", "rulesVersion", "reason"})
        if (answer.is_object() && answer.count(key)) result[key] = answer[key];
#ifdef MELEE_DOUBLES
      if (answer.is_object() && answer.count("md")) result["md"] = answer["md"];
#endif
      return result;
    };
    // The game's room report is for the YouGame app's own page (who is in the room); nothing shows it here.
    if (type == "mpRoom") return reply(200, {{"ok", true}, {"result", json::object()}});
    // Hold a room of the player's own (the Friends lobby's 4 seats, the Doubles home's 2): as the YouGame
    // app does, the launcher picks the room's 12-character code and asks YouGame for its private ticket.
    if (type == "mpFriend" && q.value("hold", json()) == true) {
      const int players = q.value("players", 4);
      const std::string compatibility = q.value("compatibility", std::string());
      if ((players != 2 && players != 4) || compatibility.empty() || compatibility.size() > 64)
        return reply(200, {{"ok", false}, {"error", "Bad request"}});
      const std::string code = backend.room_code ? backend.room_code() : new_room_code();
      json body{{"slug", kSlug}, {"player_id", backend.player_id()}, {"name", a.name}, {"anonymous", false},
                {"players", players}, {"mode", players == 2 ? "teams" : "singles"}, {"queue", "private"}, {"room", code},
                {"lobby", {{"version", 1}, {"minPlayers", players == 2 ? 2 : 1}, {"maxLocalPlayers", 1}, {"compatibility", compatibility}}},
                {"build", parse_or_empty(build_json)}};
      if (const std::string nb = listed_build_id(parse_or_empty(build_json).value("version", std::string())); !nb.empty())
        body["native_build"] = nb;
      // Launcher copies have no YouGame links of their own (the app's): said on the ticket for YouGame, and by
      // the game to the other seats (its word), so every seat of its match connects over direct UDP.
      body["links"] = false;
      if (players == 2) {
        // The Doubles home is YouGame's native unranked home: a 4-seat teams room that starts with 2 and
        // takes up to 2 local players (the game's own yougame_doubles.h ticket(); a 2-seat one is refused
        // with bad_teams_lobby).
        body["players"] = 4;
        body["mode"] = "teams";
        body["teams_lobby"] = 1;
        body["mu_doubles"] = 1;
        body["lobby"] = {{"version", 1}, {"minPlayers", 2}, {"maxLocalPlayers", 2}, {"compatibility", compatibility}};
        // The site's native Doubles check (native-teams.ts, port/tests/doubles_wiring_test.py) names the build as native_build.
        const std::string version = parse_or_empty(build_json).value("version", std::string());
        body["native_build"] = listed_build_id(version);
        if (body["native_build"].get<std::string>().empty())
          return reply(200, {{"ok", false}, {"error", "YouGame Doubles opens once YouGame lists Melee Unlocked " + version +
                                                      " (usually within two hours of a release). Try again later."}});
      }
      json result = ticket_result(parse_or_empty(backend.ticket(body.dump())));
      if (!result.count("room") || !result["room"].is_string()) result["room"] = code;
      return reply(200, {{"ok", true}, {"result", result}});
    }
    // The app's friends list has no place in the launcher: friends meet by connect code (Friends), and a
    // Doubles partner by the home's code.
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
      if (const std::string nb = listed_build_id(parse_or_empty(build_json).value("version", std::string())); !nb.empty())
        body["native_build"] = nb;
      // Launcher copies have no YouGame links of their own (the app's): said on the ticket for YouGame, and by
      // the game to the other seats (its word), so every seat of its match connects over direct UDP.
      body["links"] = false;
      // A room named by its code is a private room (a friend's room, a held lobby or home), never a queue.
      if (!ranked && q.count("room") && q["room"].is_string() && !q.count("queue")) body["queue"] = "private";
      if (q.count("lobby")) body["lobby"] = q["lobby"];
      if (q.count("direct") && q["direct"].is_string()) body["direct"] = q["direct"];
      if (q.count("room") && q["room"].is_string()) body["room"] = q["room"];
      if (q.value("afterMatch", std::string()) == "stay") body["after_match"] = "stay";
#ifdef MELEE_DOUBLES
      if (q.value("teamsLobby",json())==1) {
        body["teams_lobby"]=1;
        body["native_build"] = listed_build_id(parse_or_empty(build_json).value("version", std::string()));
        if (q.count("partySearch") && !q["partySearch"].is_null()) body["party_search"]=q["partySearch"];
        for (const char* key : {"mu_doubles","party_room","party","local_players","local_partner","lobby","queue"})
          if (q.count(key)) body[key]=q[key];
      }
#endif
      const json answer = parse_or_empty(backend.ticket(body.dump()));
      json result = json::object();
      for (const char* key : {"url", "ticket", "room", "queue", "rating", "rank", "ice", "server", "direct", "player",
                              "bestOf", "winsRequired", "rulesVersion", "reason"})
        if (answer.is_object() && answer.count(key)) result[key] = answer[key];
#ifdef MELEE_DOUBLES
      if (answer.is_object() && answer.count("md")) result["md"]=answer["md"];
#endif
      return reply(200, {{"ok", true}, {"result", result}});
    }
  } catch (const std::exception& e) {
    return reply(200, error_json(e.what()));
  }
  return reply(200, {{"ok", false}, {"error", "Not supported by the Melee Unlocked launcher"}, {"code", "unsupported"}});
}

}  // namespace launcher::yougame
