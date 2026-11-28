// The Multiplayer Lobby page on YouGame (launcher_yglobby.h). Layout and look follow the owner's picks
// (HANDOFF-YOUGAME-LOBBIES.md 9.6): the online list on the left (friends, then everyone; rank badge per
// player, an eye left of it when they are in a match, Show Elo for the number), the LIVE bar, the three
// queues, then Public rooms | Live games.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "launcher_yglobby.h"
#include "launcher_room.h"
#include <commctrl.h>
#include "launcher_theme.h"
#include "launcher_yougame.h"
#include "launcher_yougame_app.h"
#include "launcher_lobby.h"
#include "launcher_pad.h"
#include "launcher_melee_look.h"
#include <winhttp.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <nlohmann/json.hpp>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#include <wincrypt.h>
#pragma comment(lib, "shlwapi.lib")

#include <algorithm>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#pragma comment(lib, "msimg32.lib")

namespace launcher::yglobby {
namespace {

#include "launcher_yglobby_badges.inc"

// ---------------------------------------------------------------------------------------------- data

const wchar_t* kBuildName[4] = {L"", L"Akaneia 1.0.1", L"ACE 2.0.1", L"20XX Melee 1.03"};
const COLORREF kBuildColor[4] = {RGB(200, 210, 230), RGB(255, 179, 168), RGB(168, 212, 255), RGB(214, 190, 255)};

// The page's sample data for lobbypreview=1 (the same as the owner's mockups).
Data preview_data() {
  Data d; d.known = true; d.me_known = true; d.players_known = true;
  d.online = 128; d.in_match = 96; d.in_rooms = 14;
  d.waiting[0] = 3; d.waiting[1] = 1; d.waiting[2] = 0; d.playing[0] = 38; d.playing[1] = 52; d.playing[2] = 6;
  d.me = {L"hero", L"HERO#984", 2, false, false, 1520, 0};
  d.me_matches = 24; d.me_wins = 14; d.me_losses = 10; d.me_label = L"Gold";
  d.me_history = {1440, 1452, 1447, 1466, 1471, 1460, 1478, 1490, 1486, 1499, 1493, 1505, 1512, 1504, 1511, 1520};
  d.players = {
    {L"Kirbo", L"KIRB#221", 20, true, true, 1611, 0}, {L"Lucky Duck", L"DUCK#7", 16, false, true, 1388, 1},
    {L"Sandy", L"SAND#40", 12, true, true, -1, 0}, {L"knee", L"KNEE#1", 0, false, false, 1702, 0},
    {L"Pichu4L", L"PICH#88", 24, false, false, 1204, 2}, {L"wavedash_wendy", L"WEND#31", 9, true, false, 1459, 0},
    {L"ICYBOY", L"ICY#909", 14, false, false, -1, 0}, {L"shine4days", L"SHIN#12", 2, true, false, 1533, 1},
    {L"Moonwalker", L"MOON#5", 23, false, false, 1290, 0}, {L"puffpuff", L"PUFF#64", 15, false, false, 1101, 0},
  };
  d.rooms = {
    {L"hero's room", L"hero", L"Standard", {2}, 0, 1, 2, false, 0, 0, L"k7q2md"},
    {L"Doubles warmup", L"Kirbo", L"Standard", {20, 9, 15}, 1, 3, 4, false, 0, 1, L"zp4wqx"},
    {L"falcon dittos only", L"knee", L"No items", {0}, 0, 1, 2, false, 0, 0, L"m2rt8b"},
    {L"chill ffa", L"Pichu4L", L"Items on", {24, 13}, 2, 2, 4, false, 0, 2, L"h9jd3c"},
    {L"netplay sets", L"Lucky Duck", L"Standard", {16, 5}, 0, 2, 2, true, 4, 0, L"q6wn1e"},
    {L"teams bo5", L"Sandy", L"Standard", {2, 20, 9, 12}, 1, 4, 4, true, 9, 1, L"t5bo5x"},
  };
  d.live = {
    {{{20, L"Kirbo"}}, {{9, L"wavedash_wendy"}}, 0, L"Gold vs Silver", L"Game 2 \u00B7 1-0", 12, 0, L"k7q2md"},
    {{{0, L"knee"}}, {{2, L"shine4days"}}, 3, L"Weekly #12 \u00B7 Winners semis", L"Game 3 \u00B7 2-0", 31, 0, L"zp4wqx"},
    {{{16, L"Lucky Duck"}}, {{14, L"ICYBOY"}}, 3, L"Weekly #12 \u00B7 Losers R2", L"Game 1 \u00B7 0-0", 8, 1, L"m2rt8b"},
    {{{3, L"GnW_enjoyer"}}, {{15, L"puffpuff"}}, 0, L"Silver vs Bronze", L"Game 1 \u00B7 0-0", 1, 2, L"h9jd3c"},
    {{{2, L"Sandy"}, {20, L"Jo"}}, {{9, L"Moon"}, {12, L"pk"}}, 2, L"Teams", L"Game 3 \u00B7 1-1", 9, 0, L"q6wn1e"},
    {{{24, L"Pichu4L"}}, {{23, L"Moonwalker"}}, 1, L"Unranked", L"Game 1", 2, 1, L"w3ka9p"},
  };
  // Test runs only (MELEE_LAUNCHER_TEST_LOBBY_STATE): "crowd" is a busy night with long names, "empty" a dead hour.
  if (const char* t = std::getenv("MELEE_LAUNCHER_TEST") ? std::getenv("MELEE_LAUNCHER_TEST_LOBBY_STATE") : nullptr) {
    const std::string st = t;
    if (st.find("empty") != std::string::npos) {
      d.online = 1; d.in_match = 0; d.in_rooms = 0; for (int i = 0; i < 3; ++i) d.waiting[i] = d.playing[i] = 0;
      d.players.clear(); d.rooms.clear(); d.live.clear();
    }
    if (st.find("crowd") != std::string::npos) {
      d.online = 2481; d.in_match = 1904; d.in_rooms = 233;
      for (int i = 0; i < 60; ++i) {
        Player p = d.players[i % 10];
        p.name = i % 7 == 0 ? L"TheLongestNameAnyoneCouldPossiblyPickHere" + std::to_wstring(i) : p.name + std::to_wstring(i);
        p.friend_ = i < 14; d.players.push_back(p);
      }
      for (int i = 0; i < 24; ++i) {
        Room r = d.rooms[i % 6];
        r.name = i % 5 == 0 ? L"a room name that goes on and on and never stops talking " + std::to_wstring(i) : r.name + L" " + std::to_wstring(i);
        r.code = L"crowd" + std::to_wstring(10 + i); d.rooms.push_back(r);
      }
      for (int i = 0; i < 18; ++i) d.live.push_back(d.live[i % 6]);
      d.me.name = L"MyNameIsQuiteLongToo";
    }
  }
  return d;
}

std::atomic<HWND> g_window_for_worker{nullptr}, g_view_for_worker{nullptr};
// ---------------------------------------------------------------------------------------------- live data
// Where the page's numbers come from, newest first:
//  1. YouGame's public lobby read over plain HTTPS (no app, no sign-in): counts, rooms, live matches, who is online.
//  2. The launcher's own YouGame sign-in (its web view): you (name, code), your rank, your friends.
//  3. The YouGame app's local channel, only when the app is already running (never started for this).
// At most one round every 30 seconds, only while the page is open and the window is not minimised; a source that
// refuses (not there yet, notYet, an error) is asked again only after 5 minutes. Your rank: once per opening.
using json = nlohmann::json;
constexpr const char* kSlug = "melee-unlocked";
constexpr ULONGLONG kEvery = 30000, kBackoff = 300000;
enum Source { SRC_WAIT, SRC_OK, SRC_NOT_YET, SRC_OFFLINE };
struct Snapshot {   // what the worker last learned
  Data data;
  int lobby = SRC_WAIT, friends = SRC_WAIT;
  bool rank_known = false;
};
std::mutex g_live_mx;
Snapshot g_live_state;
std::atomic<bool> g_worker_quit{false}, g_reopened{true};
std::thread g_worker;

int queue_of(const std::string& key) {   // "<mode>:<queue>" -> 0 unranked, 1 ranked, 2 2v2
  if (key.find("team") != std::string::npos || key.find("doubles") != std::string::npos) return 2;
  return key.find("ranked") != std::string::npos && key.find("unranked") == std::string::npos ? 1 : 0;
}
int build_of(const json& o) {   // a room's or player's compatibility (or label): 0 vanilla, 1 Akaneia, 2 ACE, 3 another mod
  std::string c = o.value("label", std::string()) + " " + o.value("compatibility", std::string());
  for (auto& ch : c) ch = (char)std::tolower((unsigned char)ch);
  if (c.find("akaneia") != std::string::npos) return 1;
  if (c.find("ace") != std::string::npos) return 2;
  return c.find("-mod-") != std::string::npos ? 3 : 0;
}
std::wstring wide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n); return w;
}
std::string str(const json& o, const char* k) { return o.is_object() && o.count(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); }
int num(const json& o, const char* k, int d = 0) { return o.is_object() && o.count(k) && o[k].is_number() ? (int)o[k].get<double>() : d; }

void parse_counts(const json& c, Data& d) {
  if (!c.is_object()) return;
  d.online = num(c, "online"); d.in_match = num(c, "inMatch"); d.in_rooms = num(c, "inRooms");
  for (int i = 0; i < 3; ++i) d.waiting[i] = d.playing[i] = 0;
  if (c.count("waiting") && c["waiting"].is_object()) for (auto it = c["waiting"].begin(); it != c["waiting"].end(); ++it) if (it.value().is_number()) d.waiting[queue_of(it.key())] += (int)it.value().get<double>();
  if (c.count("playing") && c["playing"].is_object()) for (auto it = c["playing"].begin(); it != c["playing"].end(); ++it) if (it.value().is_number()) d.playing[queue_of(it.key())] += (int)it.value().get<double>();
  d.known = true;
}
void parse_rooms(const json& rooms, Data& d) {
  if (!rooms.is_array()) return;
  d.rooms.clear();
  for (const auto& r : rooms) {
    if (!r.is_object() || str(r, "code").empty()) continue;
    Room x; x.code = wide(str(r, "code"));
    if (str(r, "mode") != "mu-launcher") continue;
    x.name = wide(str(r, "name")); if (x.name.empty()) x.name = L"Room " + x.code;
    x.host = wide(str(r, "host")); x.rules = wide(str(r, "rules"));
    const std::string mode = str(r, "mode"); x.size = num(r, "size", 2); x.players = num(r, "players", 1);
    x.mode = mode.find("team") != std::string::npos ? 1 : (x.size > 2 && mode.find("single") == std::string::npos) ? 2 : 0;
    x.in_game = str(r, "state") == "in_game"; x.viewers = num(r, "viewers"); x.build = build_of(r);
    const std::string code = str(r, "code");
    if (code.size() == 12 && code[0] == 'm' && code[1] >= '0' && code[1] <= '3') x.build = code[1]-'0';
    d.rooms.push_back(x);
  }
  d.known = true;
}
// ---------------------------------------------------------------------------------------------- mains
// Everyone's preferred characters, from the "player-mains" YouGame world (sdk.md "Persistent worlds"): one key per
// player, main:<username> = [up to 3 character ids], written only by that player's launcher.
const char kMainsWorld[] = "player-mains";
void trace(const char* fmt, int a = 0, int b = 0, int c = 0);
std::mutex g_mains_mx;
std::map<std::string, int> g_mains;     // username -> first main
std::string g_my_username;              // YouGame's username for you ("friend": "you")
std::atomic<bool> g_mains_busy{false}, g_mains_dirty{true};
std::atomic<ULONGLONG> g_mains_next{0};
void note_my_username(const std::string& u) { if (u.empty()) return; std::lock_guard<std::mutex> lock(g_mains_mx); if (g_my_username != u) { g_my_username = u; g_mains_dirty = true; } }
int g_my_main = -1;   // a guess from your replays, until you pick (main_picked)
std::vector<int> my_mains() { std::vector<int> v = launcher::lobby::saved_mains(); if (v.empty() && g_my_main >= 0) v.push_back(g_my_main); return v; }
std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string url_part(const std::string& s) {
  std::string o; char b[4];
  for (unsigned char c : s) { if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c; else { std::snprintf(b, sizeof b, "%%%02X", c); o += b; } }
  return o;
}
// One visit: a ticket from the YouGame app, the world's state over its WebSocket, your key set when it differs.
bool visit_mains_world(bool staging, const std::string& version) {
  const auto got = launcher::yougame::app::social(staging, version, {json{{"type", "mpTicket"}, {"world", kMainsWorld}, {"players", 2}, {"mode", "world"}}}, 10000);
  auto it = got.answers.find("mpTicket");
  trace("mains: ticket answered %d ok %d", it != got.answers.end(), it != got.answers.end() && it->second.is_object() && it->second.value("ok", false));
  if (it == got.answers.end() || !it->second.is_object() || !it->second.value("ok", false)) return false;
  const json t = it->second.value("result", json());
  const std::string url = str(t, "url"), ticket = str(t, "ticket");
  if (url.rfind("https://", 0) != 0 || ticket.empty()) return false;
  std::string host = url.substr(8); if (const size_t sl = host.find('/'); sl != std::string::npos) host.resize(sl);
  const std::wstring path = wide(std::string("/world/") + kMainsWorld + "?ticket=" + url_part(ticket));
  bool ok = false;
  HINTERNET s = WinHttpOpen(L"MeleeUnlocked launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s) return false;
  WinHttpSetTimeouts(s, 5000, 5000, 8000, 8000);
  HINTERNET c = WinHttpConnect(s, wide(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
  HINTERNET ws = nullptr;
  if (r && WinHttpSetOption(r, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
      WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr))
    ws = WinHttpWebSocketCompleteUpgrade(r, 0);
  { DWORD code = 0, len = sizeof code; if (r) WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
    trace("mains: upgrade status %d socket %d error %d", (int)code, ws != nullptr, (int)GetLastError()); }
  if (r) WinHttpCloseHandle(r);
  // The next whole message (text frames joined), or false when the link closed.
  auto receive = [&](std::string& msg) {
    msg.clear(); std::vector<char> buf(64 * 1024);
    for (;;) {
      DWORD n = 0; WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
      if (WinHttpWebSocketReceive(ws, buf.data(), (DWORD)buf.size(), &n, &type) != NO_ERROR) return false;
      if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return false;
      msg.append(buf.data(), n);
      if (msg.size() > 1024 * 1024) return false;
      if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) return true;
    }
  };
  if (ws) {
    std::string msg; json welcome;
    for (int i = 0; i < 20 && receive(msg); ++i) { const json m = json::parse(msg, nullptr, false); trace("mains: message %d bytes, welcome %d", (int)msg.size(), m.is_object() && m.value("t", std::string()) == "welcome"); if (m.is_object() && m.value("t", std::string()) == "welcome") { welcome = m; break; } }
    if (welcome.is_object()) {
      ok = true;
      std::map<std::string, int> seen;
      const json st = welcome.value("state", json::object());
      if (st.is_object()) for (auto e = st.begin(); e != st.end(); ++e) {
        if (e.key().rfind("main:", 0) != 0 || !e.value().is_array() || e.value().empty() || !e.value()[0].is_number_integer()) continue;
        const int ch = e.value()[0].get<int>(); if (ch >= 0 && ch < 26) seen[lower(e.key().substr(5))] = ch;
      }
      std::string me; { std::lock_guard<std::mutex> lock(g_mains_mx); me = lower(g_my_username); }
      const std::vector<int> mine = my_mains();
      if (!me.empty() && !mine.empty()) {
        const std::string key = "main:" + me;
        const json want = mine, have = st.is_object() && st.count(key) ? st[key] : json();
        if (have != want) {   // only your own key, only when it changed
          const std::string out = json{{"t", "set"}, {"d", {{key, want}}}, {"id", 1}}.dump();
          if (WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)out.data(), (DWORD)out.size()) == NO_ERROR)
            for (int i = 0; i < 20 && receive(msg); ++i) { const json m = json::parse(msg, nullptr, false); if (m.is_object() && m.value("t", std::string()) == "ack") break; }
        }
        seen[me] = mine.front();
      }
      trace("mains world: %d keys read, mine %d, wrote %d", (int)seen.size(), mine.empty() ? -1 : mine.front(), (int)(!me.empty() && !mine.empty()));
      std::lock_guard<std::mutex> lock(g_mains_mx); g_mains = std::move(seen);
    }
    WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    WinHttpCloseHandle(ws);
  }
  if (c) WinHttpCloseHandle(c);
  WinHttpCloseHandle(s);
  return ok;
}
void publish_view();
// From the worker, every round: a visit when due (first open, every 5 minutes, or your mains changed), never blocking it.
void maybe_visit_mains(bool staging, const std::string& version) {
  const ULONGLONG now = GetTickCount64();
  if (g_mains_busy || (!g_mains_dirty && now < g_mains_next)) return;
  if (!launcher::yougame::app::running(staging)) return;
  g_mains_busy = true; g_mains_dirty = false;
  std::thread([staging, version] {
    const bool ok = visit_mains_world(staging, version);
    trace("mains world visit ok %d", ok);
    g_mains_next = GetTickCount64() + (ok ? 5 * 60 * 1000ull : 10 * 60 * 1000ull);
    g_mains_busy = false;
    if (ok) publish_view();
  }).detach();
}

void parse_players(const json& players, Data& d) {
  if (!players.is_array()) return;
  d.players.clear();
  for (const auto& q : players) {
    if (q.is_object() && str(q, "friend") == "you") {
      note_my_username(str(q, "username"));
      if (!d.me_known) { d.me_known = true; d.me.name = wide(str(q, "name").empty() ? str(q, "username") : str(q, "name")); }
    }
    if (!q.is_object() || str(q, "friend") == "you") continue;   // you are on your own card, not in the list
    Player x; x.name = wide(str(q, "name")); x.code = wide(str(q, "username"));
    if (x.name.empty()) x.name = x.code;
    if (x.name.empty()) continue;
    x.main = -1; x.in_match = str(q, "status") == "in_match";
    const std::string f = str(q, "friend"); x.friend_ = f == "friends";
    x.link = f == "friends" ? 1 : f == "requested" ? 2 : f == "incoming" ? 3 : 0;
    x.rating = q.count("rating") && q["rating"].is_number() ? (int)q["rating"].get<double>() : -1; x.build = build_of(q);
    x.avatar = str(q, "avatar"); if (x.avatar.rfind("https://", 0) != 0) x.avatar.clear();
    if (x.avatar.empty() && std::getenv("MELEE_LAUNCHER_TEST")) if (const char* t = std::getenv("MELEE_LAUNCHER_TEST_AVATAR_URL")) x.avatar = t;   // test runs only
    d.players.push_back(x);
  }
  d.known = true; d.players_known = true;
}
void parse_live(const json& live, Data& d) {
  if (!live.is_array()) return;
  d.live.clear();
  for (const auto& m : live) {
    if (!m.is_object() || str(m, "code").empty() || !m.count("players") || !m["players"].is_array()) continue;
    Live x; x.code = wide(str(m, "code"));
    const auto& ps = m["players"]; const size_t half = (ps.size() + 1) / 2;
    for (size_t i = 0; i < ps.size(); ++i) { Side side{-1, wide(str(ps[i], "name"))}; (i < half ? x.a : x.b).push_back(side); }
    const std::string q = str(m, "queue"), mode = str(m, "mode");
    x.kind = q == "tournament" ? 3 : mode.find("team") != std::string::npos ? 2 : q == "ranked" ? 0 : 1;
    x.what = q == "tournament" ? L"Tournament" : q == "ranked" ? L"Ranked" : mode.find("team") != std::string::npos ? L"Teams" : L"Unranked";
    x.game = wide(str(m, "game")); x.viewers = num(m, "viewers"); x.build = build_of(m);
    d.live.push_back(x);
  }
}
// Your ratings as this launcher saw them (rank-history.txt beside it, one number a line, the last 40 kept): the
// card's line. YouGame gives the rating now, not its history.
std::wstring history_path() {
  wchar_t exe[MAX_PATH] = {}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring p = exe; const size_t slash = p.find_last_of(L"\\/"); return (slash == std::wstring::npos ? L"." : p.substr(0, slash)) + L"\\rank-history.txt";
}
std::vector<int> rating_history() {
  std::vector<int> v; FILE* f = _wfopen(history_path().c_str(), L"r"); if (!f) return v;
  int n = 0; while (fscanf(f, "%d", &n) == 1) v.push_back(n);
  fclose(f); return v;
}
void record_rating(int rating) {
  std::vector<int> v = rating_history();
  if (!v.empty() && v.back() == rating) return;
  v.push_back(rating); if (v.size() > 40) v.erase(v.begin(), v.end() - 40);
  if (FILE* f = _wfopen(history_path().c_str(), L"w")) { for (int n : v) fprintf(f, "%d\n", n); fclose(f); }
}
// A rank answer ({queue, mode, player, rank:{tier, division, label, placed, rating, matches}}, as the game reads mpRank).
bool parse_rank(const json& r, Data& d) {
  const json rank = r.is_object() && r.count("rank") ? r["rank"] : json();
  if (!rank.is_object()) return false;
  const bool placed = rank.value("placed", false);
  double rating = rank.count("rating") && rank["rating"].is_number() ? rank["rating"].get<double>() : -1;
  if (r.count("rating") && r["rating"].is_number()) rating = r["rating"].get<double>();
  d.me.rating = placed && rating >= 0 ? (int)(rating + 0.5) : -1;
  d.me_matches = rank.count("matches") && rank["matches"].is_number() ? (int)rank["matches"].get<double>() : -1;
  d.me_label = wide(rank.value("label", std::string()));
  {   // YouGame names the tier ("silver"): use it, never a guess from the rating (Seimy 11-24: 1015 is Silver II, not Bronze).
    static const char* kTiers[6] = {"bronze", "silver", "gold", "platinum", "diamond", "challenger"};
    const std::string tier = rank.count("tier") && rank["tier"].is_string() ? rank["tier"].get<std::string>() : std::string();
    d.me_tier = -1;
    for (int i = 0; i < 6 && placed; ++i) if (tier == kTiers[i]) d.me_tier = i;
  }
  if (r.count("wins") && r["wins"].is_number() && d.me_matches >= 0) { d.me_wins = (int)r["wins"].get<double>(); d.me_losses = std::max(0, d.me_matches - d.me_wins); }
  if (d.me.rating >= 0) record_rating(d.me.rating);
  d.me_history = rating_history();
  return true;
}

int https_req(const wchar_t* method, const std::wstring& host, const std::wstring& path, const std::string& send, const std::wstring& headers, std::string* body, int* retry_after);
int https_get(const std::wstring& host, const std::wstring& path, std::string* body);
// Plain HTTPS GET (no sign-in): the status (0: no answer) and the body.
int https_get(const std::wstring& host, const std::wstring& path, std::string* body) { return https_req(L"GET", host, path, std::string(), std::wstring(), body, nullptr); }
int https_req(const wchar_t* method, const std::wstring& host, const std::wstring& path, const std::string& send, const std::wstring& headers, std::string* body, int* retry_after) {
  int status = 0;
  HINTERNET s = WinHttpOpen(L"MeleeUnlocked launcher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s) return 0;
  WinHttpSetTimeouts(s, 5000, 5000, 8000, 8000);
  if (HINTERNET c = WinHttpConnect(s, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)) {
    if (HINTERNET r = WinHttpOpenRequest(c, method, path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)) {
      const std::wstring head = L"Accept: application/json\r\n" + (send.empty() ? std::wstring() : std::wstring(send[0] == '{' ? L"Content-Type: application/json\r\n" : L"Content-Type: application/x-www-form-urlencoded\r\n")) + headers;
      if (WinHttpSendRequest(r, head.c_str(), (DWORD)-1L, send.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)send.data(), (DWORD)send.size(), (DWORD)send.size(), 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD code = 0, len = sizeof code;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        status = (int)code;
        if (retry_after) { DWORD ra = 0, rl = sizeof ra; *retry_after = WinHttpQueryHeaders(r, WINHTTP_QUERY_RETRY_AFTER | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &ra, &rl, WINHTTP_NO_HEADER_INDEX) ? (int)ra : 0; }
        std::string out; DWORD avail = 0;
        while (WinHttpQueryDataAvailable(r, &avail) && avail && out.size() < 4 * 1024 * 1024) {
          std::string chunk(avail, '\0'); DWORD got = 0;
          if (!WinHttpReadData(r, chunk.data(), avail, &got) || !got) break;
          out.append(chunk.data(), got);
        }
        if (body) *body = std::move(out);
      }
      WinHttpCloseHandle(r);
    }
    WinHttpCloseHandle(c);
  }
  WinHttpCloseHandle(s);
  return status;
}

// ---------------------------------------------------------------------------------------------- sign-in by code
// The device flow (sdk.md, "Signing in a game started on its own"). The client id is the game's id (production's is
// built in; another site's is asked once). The refresh token is kept encrypted for this Windows user (DPAPI).
std::string narrow_ascii(const std::wstring& w) { std::string s; for (wchar_t c : w) s += c < 128 ? (char)c : '?'; return s; }
namespace device {
constexpr const char* kProductionClient = "880b2eeb-cbba-4b73-8e53-eae3f6964031";   // GET /oauth/game-client?game=melee-unlocked
std::mutex g_mx, g_refresh_mx;
std::string g_access, g_refresh; ULONGLONG g_access_until = 0;
launcher::yglobby::CodeSignIn g_state = launcher::yglobby::CodeSignIn::kOff;
std::wstring g_user_code, g_reason; std::string g_link;   // shown, never logged
std::atomic<unsigned> g_attempt{0};
bool g_loaded = false;
std::wstring host() {
  std::wstring site = wide(launcher::yougame::site_url()); if (site.empty()) site = L"https://yougame.co";
  std::wstring h = site.substr(site.find(L"//") == std::wstring::npos ? 0 : site.find(L"//") + 2);
  if (const size_t slash = h.find(L'/'); slash != std::wstring::npos) h.resize(slash);
  return h;
}
bool production() { return host() == L"yougame.co"; }
std::filesystem::path file() {
  wchar_t* base = nullptr; std::filesystem::path p;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) p = std::filesystem::path(base) / L"Melee Unlocked" / (production() ? L"yougame-signin.bin" : L"yougame-signin-other.bin");
  CoTaskMemFree(base); return p;
}
void store(const std::string& refresh) {   // encrypted for this Windows user only; empty deletes it
  const auto p = file(); std::error_code ec;
  if (refresh.empty()) { std::filesystem::remove(p, ec); return; }
  std::filesystem::create_directories(p.parent_path(), ec);
  DATA_BLOB in{(DWORD)refresh.size(), (BYTE*)refresh.data()}, out{};
  if (!CryptProtectData(&in, L"YouGame sign-in", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return;
  { std::ofstream f(p, std::ios::binary | std::ios::trunc); f.write((const char*)out.pbData, out.cbData); }
  LocalFree(out.pbData);
}
std::string stored() {
  std::ifstream f(file(), std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (bytes.empty()) return {};
  DATA_BLOB in{(DWORD)bytes.size(), (BYTE*)bytes.data()}, out{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
  std::string s((const char*)out.pbData, out.cbData); LocalFree(out.pbData); return s;
}
void load() {   // once: a sign-in kept from before
  std::lock_guard<std::mutex> lock(g_mx);
  if (g_loaded) return; g_loaded = true;
  g_refresh = stored(); if (!g_refresh.empty()) g_state = launcher::yglobby::CodeSignIn::kSignedIn;
}
std::string form_part(const std::string& s) {
  std::string o; char b[4];
  for (unsigned char c : s) { if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c; else { std::snprintf(b, sizeof b, "%%%02X", c); o += b; } }
  return o;
}
std::string client_id() {
  if (production()) return kProductionClient;
  static std::string other; if (!other.empty()) return other;
  std::string body; if (https_get(host(), L"/oauth/game-client?game=melee-unlocked", &body) == 200) other = json::parse(body, nullptr, false).value("client_id", std::string());
  return other;
}
int post(const wchar_t* path, const std::string& form, json* answer, int* retry_after = nullptr) {
  std::string body; const int st = https_req(L"POST", host(), path, form, std::wstring(), &body, retry_after);
  if (answer) *answer = json::parse(body, nullptr, false);
  return st;
}
void notify() { if (HWND v = g_view_for_worker.load()) PostMessageW(v, WM_APP + 1, 0, 0); if (HWND w = g_window_for_worker.load()) PostMessageW(w, WM_APP + 1, 0, 0); g_reopened.store(true); }
void take(const json& t) {   // a token answer: the new refresh token is stored before the access token is used
  const std::string access = t.value("access_token", std::string()), refresh = t.value("refresh_token", std::string());
  if (!refresh.empty()) store(refresh);
  std::lock_guard<std::mutex> lock(g_mx);
  if (!refresh.empty()) g_refresh = refresh;
  g_access = access; g_access_until = GetTickCount64() + (ULONGLONG)std::max(60, t.value("expires_in", 3600)) * 1000;
  g_state = launcher::yglobby::CodeSignIn::kSignedIn; g_user_code.clear(); g_link.clear();
}
void fail(const std::wstring& why) { std::lock_guard<std::mutex> lock(g_mx); g_state = launcher::yglobby::CodeSignIn::kFailed; g_reason = why; g_user_code.clear(); g_link.clear(); }
void open_link() {
  std::string link; { std::lock_guard<std::mutex> lock(g_mx); link = g_link; }
  if (link.rfind("https://", 0) != 0) return;
  if (std::getenv("MELEE_LAUNCHER_TEST") && std::getenv("MELEE_LAUNCHER_TEST_SCRIPT")) { if (FILE* f = std::fopen("launcher-actions.log", "ab")) { std::fprintf(f, "open the sign-in link\n"); std::fclose(f); } return; }
  ShellExecuteW(nullptr, L"open", wide(link).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
void run(unsigned attempt) {   // start, then poll until approved, refused or the code ran out
  const std::string id = client_id();
  if (id.empty()) { fail(L"YouGame didn't answer. Try again."); notify(); return; }
  json a; const int st = post(L"/oauth/device_authorization", "client_id=" + form_part(id) + "&scope=player&device_label=" + form_part("Melee Unlocked launcher"), &a);
  trace("sign-in by code: start status %d", st);
  if (st != 200 || !a.is_object()) { fail(st == 429 ? L"Too many tries. Wait a minute, then try again." : L"YouGame didn't answer. Try again."); notify(); return; }
  const std::string device_code = a.value("device_code", std::string());
  int interval = std::max(1, a.value("interval", 5));
  const ULONGLONG until = GetTickCount64() + (ULONGLONG)std::max(60, a.value("expires_in", 600)) * 1000;
  { std::lock_guard<std::mutex> lock(g_mx); g_state = launcher::yglobby::CodeSignIn::kWaiting;
    g_user_code = wide(a.value("user_code", std::string())); g_link = a.value("verification_uri_complete", a.value("verification_uri", std::string())); }
  notify(); open_link();
  while (GetTickCount64() < until && g_attempt.load() == attempt) {
    for (int i = 0; i < interval * 10 && g_attempt.load() == attempt; ++i) Sleep(100);
    if (g_attempt.load() != attempt) return;   // cancelled, or a new code asked for
    json t; int retry = 0;
    const int ts = post(L"/oauth/token", "grant_type=" + form_part("urn:ietf:params:oauth:grant-type:device_code") + "&device_code=" + form_part(device_code) + "&client_id=" + form_part(id), &t, &retry);
    if (ts == 200 && t.is_object() && t.count("access_token")) { take(t); trace("sign-in by code: signed in"); notify(); return; }
    const std::string err = t.is_object() ? t.value("error", std::string()) : std::string();
    if (err == "authorization_pending") continue;
    if (err == "slow_down") { interval = std::max(interval + 5, t.value("interval", interval + 5)); continue; }
    if (ts == 503 || ts == 0) { for (int i = 0; i < std::max(1, retry) * 10 && g_attempt.load() == attempt; ++i) Sleep(100); continue; }
    trace(err == "access_denied" ? "sign-in by code: declined" : "sign-in by code: ended");
    fail(err == "access_denied" ? L"Sign-in was declined. Try again." : L"The code ran out. Try again."); notify(); return;
  }
  if (g_attempt.load() == attempt) { fail(L"The code ran out. Try again."); notify(); }
}
// The access token for a call (refreshed when it is about to end; one refresh at a time). Empty: not signed in.
std::string token() {
  load();
  std::lock_guard<std::mutex> one(g_refresh_mx);
  std::string refresh;
  { std::lock_guard<std::mutex> lock(g_mx); if (!g_access.empty() && GetTickCount64() + 60000 < g_access_until) return g_access; refresh = g_refresh; }
  if (refresh.empty()) return {};
  const std::string id = client_id(); if (id.empty()) return {};
  json t; int retry = 0;
  const int st = post(L"/oauth/token", "grant_type=refresh_token&refresh_token=" + form_part(refresh) + "&client_id=" + form_part(id), &t, &retry);
  trace("sign-in by code: refresh status %d", st);
  if (st == 200 && t.is_object() && t.count("access_token")) { take(t); std::lock_guard<std::mutex> lock(g_mx); return g_access; }
  if (st == 400 && t.is_object() && t.value("error", std::string()) == "invalid_grant") {   // ended: sign in again
    store(std::string()); std::lock_guard<std::mutex> lock(g_mx); g_refresh.clear(); g_access.clear(); g_state = launcher::yglobby::CodeSignIn::kOff;
  }
  return {};   // 503 / 429 / no answer: kept, asked again next round
}
void expire() { std::lock_guard<std::mutex> lock(g_mx); g_access_until = 0; }   // a call answered 401: refresh next time
}  // namespace device

// ---------------------------------------------------------------------------------------------- pictures
std::mutex g_pic_mx;
std::map<std::string, std::shared_ptr<Gdiplus::Bitmap>> g_pics;   // url -> a round 64 px picture
std::set<std::string> g_pic_tried;
std::filesystem::path pic_dir() {
  wchar_t* base = nullptr; std::filesystem::path p;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) p = std::filesystem::path(base) / L"Melee Unlocked" / L"avatars";
  CoTaskMemFree(base); return p;
}
std::shared_ptr<Gdiplus::Bitmap> round_picture(const std::string& bytes) {
  if (bytes.empty()) return nullptr;
  IStream* st = SHCreateMemStream((const BYTE*)bytes.data(), (UINT)bytes.size()); if (!st) return nullptr;
  std::unique_ptr<Gdiplus::Bitmap> src(Gdiplus::Bitmap::FromStream(st)); st->Release();
  if (!src || src->GetLastStatus() != Gdiplus::Ok || !src->GetWidth()) return nullptr;
  auto out = std::make_shared<Gdiplus::Bitmap>(64, 64, PixelFormat32bppPARGB);
  Gdiplus::Graphics g(out.get()); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  g.Clear(Gdiplus::Color(0, 0, 0, 0));
  Gdiplus::GraphicsPath circle; circle.AddEllipse(0, 0, 64, 64);
  Gdiplus::TextureBrush tb(src.get()); const float k = 64.0f / std::min(src->GetWidth(), src->GetHeight());
  tb.ScaleTransform(k, k); g.FillPath(&tb, &circle);
  return out;
}
// Loads up to `budget` pictures not tried yet: from the disk copy when under 7 days old, else from the web.
bool fetch_pictures(const std::vector<Player>& players, int budget) {
  bool any = false; const auto dir = pic_dir(); std::error_code ec;
  for (const auto& p : players) {
    if (budget <= 0) break;
    if (p.avatar.empty()) continue;
    { std::lock_guard<std::mutex> lock(g_pic_mx); if (!g_pic_tried.insert(p.avatar).second) continue; }
    const auto file = dir.empty() ? std::filesystem::path() : dir / (std::to_wstring(std::hash<std::string>{}(p.avatar)) + L".img");
    std::string bytes;
    if (!file.empty() && std::filesystem::exists(file, ec) &&
        std::filesystem::file_time_type::clock::now() - std::filesystem::last_write_time(file, ec) < std::chrono::hours(24 * 7)) {
      std::ifstream in(file, std::ios::binary); bytes.assign(std::istreambuf_iterator<char>(in), {});
    } else {
      --budget;
      const std::string rest = p.avatar.substr(8); const size_t slash = rest.find('/');
      const std::wstring host = wide(rest.substr(0, slash)), path = slash == std::string::npos ? L"/" : wide(rest.substr(slash));
      if (https_get(host, path, &bytes) != 200 || bytes.size() > 1024 * 1024) bytes.clear();
      if (!bytes.empty() && !file.empty()) { std::filesystem::create_directories(dir, ec); std::ofstream(file, std::ios::binary).write(bytes.data(), (std::streamsize)bytes.size()); }
    }
    if (auto pic = round_picture(bytes)) { std::lock_guard<std::mutex> lock(g_pic_mx); g_pics[p.avatar] = pic; any = true; }
  }
  return any;
}

void publish_view() {
  if (HWND w = g_window_for_worker.load()) PostMessageW(w, WM_APP + 1, 0, 0);
  if (HWND v = g_view_for_worker.load()) PostMessageW(v, WM_APP + 1, 0, 0);
}
void publish(const Snapshot& l) {
  { std::lock_guard<std::mutex> lock(g_live_mx); g_live_state = l; }
  if (HWND w = g_window_for_worker.load()) PostMessageW(w, WM_APP + 1, 0, 0);
  if (HWND v = g_view_for_worker.load()) PostMessageW(v, WM_APP + 1, 0, 0);
}

void work(std::string version) {
  ULONGLONG next_round = 0, next_lobby = 0, next_friends = 0, next_app = 0, next_board = 0;
  std::map<std::string, std::string> board;   // the public rank board: username -> picture
  while (!g_worker_quit.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    HWND w = g_window_for_worker.load(), v = g_view_for_worker.load();
    const bool page = w && IsWindowVisible(w), view = v && IsWindowVisible(v);
    if ((!page && !view) || IsIconic(GetAncestor(page ? w : v, GA_ROOT))) continue;   // only while a view of the lobby is in view
    const ULONGLONG now = GetTickCount64();
    if (g_reopened.exchange(false)) next_round = 0;   // the page was just opened: ask now
    if (now < next_round) continue;
    next_round = now + kEvery;
    Snapshot l; { std::lock_guard<std::mutex> lock(g_live_mx); l = g_live_state; }
    const bool rank_wanted = !l.rank_known;
    trace("round: start");
    // 1. the public lobby read
    if (now >= next_lobby) {
      std::wstring site = wide(launcher::yougame::site_url()); if (site.empty()) site = L"https://yougame.co";
      std::wstring host = site.substr(site.find(L"//") == std::wstring::npos ? 0 : site.find(L"//") + 2);
      if (const size_t slash = host.find(L'/'); slash != std::wstring::npos) host.resize(slash);
      const std::wstring base = L"/api/games/" + wide(kSlug) + L"/social?part=";
      std::string body; const int st = https_get(host, base + L"counts", &body);
      trace("round: public counts status %d", st);
      const json c = st == 200 ? json::parse(body, nullptr, false) : json();
      if (c.is_object()) {
        parse_counts(c, l.data); l.data.known = true; l.lobby = SRC_OK;
        std::string rb; const int rs = https_get(host, base + L"rooms", &rb);
        trace("round: public rooms status %d", rs);
        if (const json r = rs == 200 ? json::parse(rb, nullptr, false) : json(); r.is_object()) parse_rooms(r.value("rooms", json()), l.data);
      } else { l.lobby = st == 0 ? SRC_OFFLINE : SRC_NOT_YET; next_lobby = now + kBackoff; }
    }
    // 2. you, your rank and your friends, with the launcher's own sign-in
    trace("round: account?");
    const auto acc = launcher::yougame::account();
    trace("round: account signed in %d app %d", acc.signed_in, acc.app);
    if (acc.signed_in) {
      l.data.me_known = true; l.data.me.name = wide(acc.name); l.data.me.code = wide(acc.code);
      if (!acc.app) {
        std::string body;
        if (rank_wanted && launcher::yougame::site_get(std::string("/api/multiplayer/rank?slug=") + kSlug + "&mode=singles&self=1", &body) == 200)
          l.rank_known = parse_rank(json::parse(body, nullptr, false), l.data);
        if (now >= next_friends) {   // who's online (friends marked), as the signed-in player sees it
          const int st = launcher::yougame::site_get(std::string("/api/games/") + kSlug + "/social?part=players", &body);
          trace("round: signed-in players status %d", st);
          const json j = st == 200 ? json::parse(body, nullptr, false) : json();
          if (j.is_object() && j.count("players")) { parse_players(j["players"], l.data); l.friends = SRC_OK; }
          else { l.friends = SRC_NOT_YET; next_friends = now + kBackoff; }
        }
      }
    } else l.data.me_known = false;
    // 2b. sign-in by code: who's online (friends marked, you named) and your rank, the app closed or not
    if (l.friends != SRC_OK || !acc.signed_in) {
      if (const std::string token = device::token(); !token.empty()) {
        const std::wstring bearer = L"Authorization: Bearer " + wide(token) + L"\r\n";
        std::string body; const int st = https_req(L"GET", device::host(), L"/api/games/" + wide(kSlug) + L"/social?part=players", std::string(), bearer, &body, nullptr);
        trace("round: players by code status %d", st);
        const json j = st == 200 ? json::parse(body, nullptr, false) : json();
        if (j.is_object() && j.count("players")) { parse_players(j["players"], l.data); l.friends = SRC_OK; }
        if (st == 401) device::expire();
        if (rank_wanted) {
          std::string rb; const int rs = https_req(L"GET", device::host(), L"/api/multiplayer/rank?slug=" + wide(kSlug) + L"&mode=singles", std::string(), bearer, &rb, nullptr);
          const json r = rs == 200 ? json::parse(rb, nullptr, false) : json();
          if (r.is_object() && r.count("me") && r["me"].is_object()) {
            l.rank_known = parse_rank(r["me"], l.data);
            if (!l.data.me_known && !str(r["me"], "name").empty()) { l.data.me_known = true; l.data.me.name = wide(str(r["me"], "name")); }
          }
        }
      }
    }
    // the public rank board: its top players' pictures (a main, when known, still shows first)
    if (now >= next_board) {
      next_board = now + kBackoff;
      std::string bb; if (https_get(device::host(), L"/api/multiplayer/rank?slug=" + wide(kSlug), &bb) == 200) {
        const json b = json::parse(bb, nullptr, false);
        if (b.is_object() && b.count("top") && b["top"].is_array()) {
          board.clear();
          for (const auto& t : b["top"]) { const std::string u = str(t, "username"), a = str(t, "avatar_url"); if (!u.empty() && a.rfind("https://", 0) == 0) board[u] = a; }
        }
      }
    }
    for (auto& pl : l.data.players) if (pl.avatar.empty()) { auto it = board.find(narrow_ascii(pl.code)); if (it != board.end()) pl.avatar = it->second; }
    // 3. the YouGame app, only when it is already running and the public read gave nothing
    const bool staging = launcher::yougame::staging_env();
    const bool players_ok = l.friends == SRC_OK;   // who's online came over HTTPS with the launcher's sign-in
    if ((l.lobby != SRC_OK || !players_ok || (acc.signed_in && acc.app && rank_wanted)) && now >= next_app && launcher::yougame::app::running(staging)) {
      std::vector<json> asks;
      if (l.lobby != SRC_OK) asks = {{{"type", "socialCounts"}}, {{"type", "socialRooms"}}};
      if (!players_ok) asks.push_back({{"type", "socialPlayers"}, {"friends", false}});
      if (rank_wanted) asks.push_back({{"type", "mpRank"}, {"mode", "singles"}});
      const auto got = launcher::yougame::app::social(staging, version, asks);
      bool any = false;
      auto result = [&](const char* type) -> json {
        auto it = got.answers.find(type);
        if (it == got.answers.end() || !it->second.is_object() || !it->second.value("ok", false)) return json();
        any = true; return it->second.value("result", json());
      };
      if (const json c = result("socialCounts"); c.is_object()) parse_counts(c, l.data);
      if (const json r = result("socialRooms"); r.is_object()) parse_rooms(r.value("rooms", json()), l.data);
      if (const json q = result("socialPlayers"); q.is_object()) parse_players(q.value("players", json()), l.data);
      if (const json k = result("mpRank"); k.is_object()) l.rank_known = parse_rank(k, l.data);
      if (got.hello.state == launcher::yougame::app::Answer::kSignedIn && !l.data.me_known) {
        l.data.me_known = true; l.data.me.name = wide(got.hello.name); l.data.me.code = wide(got.hello.code);
      }
      // notHere (the app has not checked this copy yet) says when to ask again; any other refusal waits 5 minutes.
      int retry_s = 0;
      for (const auto& kv : got.answers) if (kv.second.is_object() && kv.second.value("code", std::string()) == "notHere") retry_s = std::max(retry_s, std::max(2, num(kv.second, "retryAfter", 2)));
      if (!any) next_app = now + (retry_s ? (ULONGLONG)retry_s * 1000 : kBackoff);
      if (!any && retry_s) next_round = std::min(next_round, next_app);
    }
    trace("round: publish lobby %d", l.lobby);
    publish(l);
    maybe_visit_mains(staging, version);
    if (fetch_pictures(l.data.players, 8)) publish(l);   // pictures that just loaded
  }
}

// ---------------------------------------------------------------------------------------------- state
HWND g_owner = nullptr, g_window = nullptr, g_code = nullptr;
Callbacks g_cb;
bool g_preview = false, g_elo = false;
// A list the wheel scrolls by whole rows: the players, and the rooms or live games.
struct Scroll { int skip = 0, total = 0, fit = 0; RECT area{}; };
Scroll g_players_scroll, g_list_scroll;
void scrollbar(HDC dc, const Scroll& s, int x, int top, int h);
int g_list_tab = 0;     // 0 online, 1 friends
int g_rooms_tab = 0;    // 0 public rooms, 1 live games
int g_filter = 0;       // live games: 0 all, 1 ranked, 2 unranked, 3 2v2, 4 tournament
bool g_public = true;
std::wstring g_toast; ULONGLONG g_toast_until = 0;
int g_hot = -1; int g_hot_act = -1, g_hot_arg = 0;   // the hovered item (index in the last paint's list, and which item it is)
bool g_tracking = false;

enum Act { A_TAB_ONLINE, A_TAB_FRIENDS, A_ELO, A_LIVE_WATCH, A_PLAY, A_TAB_ROOMS, A_TAB_LIVE, A_FILTER,
           A_PUBLIC, A_PRIVATE, A_CREATE, A_CODE_JOIN, A_ROOM, A_LIVE_ROW, A_PLAYER_WATCH, A_MY_MAIN, A_SIGN_IN,
           A_ROOM_START, A_ROOM_INVITE, A_ROOM_LEAVE, A_ROOM_COPY };
struct Hit { RECT r; int act, arg; };
std::vector<Hit> g_hits;

Data data_raw() {
  if (g_preview) { Data d = preview_data(); const std::vector<int> mine = my_mains(); if (!mine.empty()) d.me.main = mine.front(); return d; }
  Data d; { std::lock_guard<std::mutex> lock(g_live_mx); d = g_live_state.data; }
  const std::vector<int> mine = my_mains(); d.me.main = mine.empty() ? -1 : mine.front();
  std::lock_guard<std::mutex> lock(g_mains_mx);
  for (auto& p : d.players) if (p.main < 0) { std::string u; for (wchar_t c : p.code) u += (char)std::tolower((int)(c < 128 ? c : '?')); auto it = g_mains.find(u); if (it != g_mains.end()) p.main = it->second; }
  return d;
}
// Live games most watched first everywhere: the featured match (LIVE bar, Spectate) is the one most people watch.
Data data() {
  Data d = data_raw();
  std::stable_sort(d.live.begin(), d.live.end(), [](const Live& a, const Live& b) { return a.viewers > b.viewers; });
  return d;
}
int lobby_state() { if (g_preview) return SRC_OK; std::lock_guard<std::mutex> lock(g_live_mx); return g_live_state.lobby; }

// ---------------------------------------------------------------------------------------------- drawing
// The page's scale: the window's DPI, made smaller when the page is narrower or shorter than its layout needs (a small
// screen at a high Windows scale), so nothing is cut off. Set at the start of each paint. g_exact_px: draw_badge for
// the skin, which passes real pixels.
int g_fit_dpi = 0; bool g_exact_px = false;
int dpi() { return g_exact_px ? 96 : g_fit_dpi ? g_fit_dpi : g_window ? GetDpiForWindow(g_window) : 96; }
int S(int n) { return MulDiv(n, dpi(), 96); }
RECT rc(int x, int y, int w, int h) { return RECT{x, y, x + w, y + h}; }

const COLORREF C_BG_TOP = RGB(0x17, 0x1F, 0x30), C_BG_BOT = RGB(0x11, 0x18, 0x25), C_PANEL = RGB(0x19, 0x21, 0x31),
               C_SEP = RGB(0x22, 0x2B, 0x42), C_TEXT = RGB(0xE7, 0xEC, 0xF5), C_DIM = RGB(0x8D, 0x9B, 0xB5),
               C_FAINT = RGB(0x66, 0x74, 0x8E), C_BTN = RGB(0x21, 0x2B, 0x42), C_BTN_BORDER = RGB(0x35, 0x41, 0x5F),
               C_BTN_HOT = RGB(0x2A, 0x36, 0x52), C_FIELD = RGB(0x0E, 0x15, 0x22), C_FIELD_BORDER = RGB(0x2B, 0x36, 0x52),
               C_OK = RGB(0x5A, 0xC8, 0x8A), C_AMBER = RGB(0xE5, 0xA8, 0x4A), C_CHIP = RGB(0x26, 0x30, 0x4A),
               C_CHIP_TEXT = RGB(0xC8, 0xD2, 0xE6), C_WATCH = RGB(0xCD, 0xB8, 0xFF), C_LIVE_BG = RGB(0x5A, 0x1F, 0x26),
               C_LIVE = RGB(0xFF, 0x8F, 0x8F), C_FRIEND = RGB(0x9F, 0x71, 0xF1);
const COLORREF kTierColor[6] = {RGB(0xD9, 0x90, 0x63), RGB(0xC9, 0xD2, 0xDC), RGB(0xF0, 0xC2, 0x4A),
                                RGB(0x5F, 0xD8, 0xC8), RGB(0x77, 0xBA, 0xF2), RGB(0xFF, 0xC8, 0x57)};
const wchar_t* kTierName[6] = {L"Bronze", L"Silver", L"Gold", L"Platinum", L"Diamond", L"Challenger"};
// YouGame's tiers by rating (sdk.md: Bronze under 900, Silver 900+, Gold 1100+, Platinum 1300+, Diamond 1500+;
// Challenger is a Diamond in the game's top 100, which a rating alone cannot tell). For players YouGame sends only a rating for.
int tier_of(int rating) { return rating < 0 ? -1 : rating >= 1500 ? 4 : rating >= 1300 ? 3 : rating >= 1100 ? 2 : rating >= 900 ? 1 : 0; }
int my_tier_of(const Data& d) { return d.me_tier >= 0 ? d.me_tier : tier_of(d.me.rating); }

struct Fonts { HFONT title, body, bold, sm, tiny, icon, icon_small, btn, name; int dpi = 0; } g_f;
HFONT make_font(int px, int weight, const wchar_t* face = L"Segoe UI") {
  LOGFONTW lf{}; lf.lfHeight = -S(px); lf.lfWeight = weight; lf.lfQuality = CLEARTYPE_QUALITY; wcscpy_s(lf.lfFaceName, face);
  return CreateFontIndirectW(&lf);
}
void fonts() {
  if (g_f.dpi == dpi()) return;
  for (HFONT f : {g_f.title, g_f.body, g_f.bold, g_f.sm, g_f.tiny, g_f.icon, g_f.icon_small, g_f.btn, g_f.name}) if (f) DeleteObject(f);
  g_f.dpi = dpi();
  g_f.title = make_font(21, FW_SEMIBOLD); g_f.body = make_font(12, FW_NORMAL); g_f.bold = make_font(13, FW_SEMIBOLD);
  g_f.sm = make_font(11, FW_NORMAL); g_f.name = make_font(12, FW_SEMIBOLD); g_f.tiny = make_font(10, FW_BOLD); g_f.btn = make_font(12, FW_SEMIBOLD);
  g_f.icon = make_font(15, FW_NORMAL, L"Segoe MDL2 Assets"); g_f.icon_small = make_font(12, FW_NORMAL, L"Segoe MDL2 Assets");
  if (g_code) SendMessageW(g_code, WM_SETFONT, (WPARAM)g_f.body, FALSE);   // never a deleted font
}
const wchar_t kEye[] = L"\uE890", kFriend[] = L"\uE8FA", kRules[] = L"\uE8A5", kLock[] = L"\uE72E", kPeople[] = L"\uE716";

// Rounded shapes are anti-aliased (GDI+): a rounded rectangle path in pixels.
void round_path(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
  const float d = std::min({r * 2, w, h});
  if (d <= 0.5f) { path.AddRectangle(Gdiplus::RectF(x, y, w, h)); return; }
  path.AddArc(x, y, d, d, 180, 90); path.AddArc(x + w - d, y, d, d, 270, 90);
  path.AddArc(x + w - d, y + h - d, d, d, 0, 90); path.AddArc(x, y + h - d, d, d, 90, 90); path.CloseFigure();
}
Gdiplus::Color gcolor(COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }
void fill(HDC dc, RECT r, COLORREF c, int radius = 0) {
  if (!radius) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b); return; }
  Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  Gdiplus::GraphicsPath path; round_path(path, (float)r.left, (float)r.top, (float)(r.right - r.left), (float)(r.bottom - r.top), (float)S(radius));
  Gdiplus::SolidBrush b(gcolor(c)); g.FillPath(&b, &path);
}
void frame(HDC dc, RECT r, COLORREF fillc, COLORREF border, int radius) {
  Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  Gdiplus::GraphicsPath path; round_path(path, r.left + 0.5f, r.top + 0.5f, (float)(r.right - r.left) - 1, (float)(r.bottom - r.top) - 1, (float)S(radius));
  Gdiplus::SolidBrush b(gcolor(fillc)); g.FillPath(&b, &path);
  Gdiplus::Pen pen(gcolor(border), 1.0f); g.DrawPath(&pen, &path);
}
int width_of(HDC dc, HFONT f, const std::wstring& s) {
  if (const int w = launcher::melee_look::text_width(s, f); w >= 0) return w;
  HGDIOBJ o = SelectObject(dc, f); SIZE z{}; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &z); SelectObject(dc, o); return z.cx;
}
void text(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF c, UINT fl = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX) {
  if (launcher::melee_look::text(dc, s, r, f, c, fl)) return;
  HGDIOBJ o = SelectObject(dc, f); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, c);
  DrawTextW(dc, s.c_str(), -1, &r, fl | DT_NOPREFIX); SelectObject(dc, o);
}
// Runs of text on one line, each its own font and colour; returns the x after the last run.
struct Run { std::wstring s; HFONT f; COLORREF c; };
int runs(HDC dc, const std::vector<Run>& rs, int x, int y, int h, int right) {
  for (const auto& r : rs) { if (x >= right) break; int w = width_of(dc, r.f, r.s); text(dc, r.s, RECT{x, y, std::min(right, x + w + 1), y + h}, r.f, r.c); x += w; }
  return x;
}

HICON stock_icon(int character, int px) {
  static std::map<int, HICON> cache;   // character * 1000 + size in pixels
  if (character < 0 || character > 25 || px <= 0) return nullptr;
  HICON& icon = cache[character * 1000 + px];
  if (!icon) icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(100 + character), IMAGE_ICON, px, px, LR_DEFAULTCOLOR);
  return icon;
}
void stock(HDC dc, int character, int x, int y, int px) {
  if (character < 0) {   // YouGame does not say who a player mains: a plain avatar
    fill(dc, rc(x, y, S(px), S(px)), RGB(0x2B, 0x36, 0x52), px / 2);
    HFONT f = make_font(std::max(9, px * 2 / 3), FW_NORMAL, L"Segoe MDL2 Assets");
    text(dc, L"\uE77B", rc(x, y, S(px), S(px)), f, RGB(0x8D, 0x9B, 0xB5), DT_CENTER | DT_VCENTER | DT_SINGLELINE); DeleteObject(f);
    return;
  }
  if (HICON i = stock_icon(character, S(px))) DrawIconEx(dc, x, y, i, S(px), S(px), 0, nullptr, DI_NORMAL);
}

void badge(HDC dc, int tier, int x, int y, int px) {
  const int side = S(px);
  if (tier < 0) {   // unranked: a dashed ring with a ?
    LOGBRUSH lb{BS_SOLID, RGB(0xAE, 0xB8, 0xC8), 0}; const DWORD dash[2] = {(DWORD)std::max(2, side / 6), (DWORD)std::max(2, side / 10)};
    HPEN p = ExtCreatePen(PS_GEOMETRIC | PS_USERSTYLE | PS_ENDCAP_FLAT, std::max(1, side / 11), &lb, 2, dash); HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    const int in = std::max(1, side / 10); Ellipse(dc, x + in, y + in, x + side - in, y + side - in);
    SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p);
    HFONT f = make_font(std::max(9, px * 11 / 20), FW_BOLD); text(dc, L"?", rc(x, y, side, side), f, RGB(0xD7, 0xDE, 0xEA), DT_CENTER | DT_VCENTER | DT_SINGLELINE); DeleteObject(f);
    return;
  }
  BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = kBadgeSize; bi.bmiHeader.biHeight = -kBadgeSize;
  bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
  void* bits = nullptr; HDC md = CreateCompatibleDC(dc); HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (bm && bits) {
    memcpy(bits, kBadgeBgra[std::min(tier, kBadgeCount - 1)], kBadgeSize * kBadgeSize * 4);
    Gdiplus::Bitmap art(kBadgeSize, kBadgeSize, kBadgeSize * 4, PixelFormat32bppPARGB, (BYTE*)bits);
    Gdiplus::Graphics g(dc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic); g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    if (art.GetLastStatus() != Gdiplus::Ok || g.DrawImage(&art, Gdiplus::Rect(x, y, side, side), 0, 0, kBadgeSize, kBadgeSize, Gdiplus::UnitPixel) != Gdiplus::Ok) {
      HGDIOBJ o = SelectObject(md, bm);   // no GDI+: a plain blend
      BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
      AlphaBlend(dc, x, y, side, side, md, 0, 0, kBadgeSize, kBadgeSize, bf);
      SelectObject(md, o);
    }
  }
  if (bm) DeleteObject(bm); DeleteDC(md);
}

void hit(RECT r, int act, int arg = 0) { g_hits.push_back({r, act, arg}); }
bool is_hot(int act, int arg = 0) { return g_hot >= 0 && g_hot_act == act && g_hot_arg == arg; }   // any order: the list is rebuilt while painting

// A launcher button: accent for the main action, quiet otherwise. Registers its hit box.
void button(HDC dc, RECT r, const std::wstring& label, bool accent, int act, int arg = 0, const wchar_t* icon = nullptr, bool watch = false) {
  const bool hot = (hit(r, act, arg), is_hot(act, arg));
  if (launcher::melee_look::button(dc, r, accent ? (hot ? launcher::theme::glow() : launcher::theme::top()) : (hot ? C_BTN_HOT : C_BTN),
                                  accent ? launcher::theme::bottom() : (hot ? C_BTN_HOT : C_BTN),
                                  accent ? RGB(0xC7, 0xAE, 0xFC) : watch ? RGB(0x5B, 0x4A, 0x8A) : RGB(0x4D, 0x5B, 0x80))) {
  } else if (accent) {
    TRIVERTEX v[2] = {{r.left, r.top, 0, 0, 0, 0xff00}, {r.right, r.bottom, 0, 0, 0, 0xff00}};
    COLORREF t = hot ? launcher::theme::glow() : launcher::theme::top(), b = launcher::theme::bottom();
    v[0].Red = GetRValue(t) << 8; v[0].Green = GetGValue(t) << 8; v[0].Blue = GetBValue(t) << 8;
    v[1].Red = GetRValue(b) << 8; v[1].Green = GetGValue(b) << 8; v[1].Blue = GetBValue(b) << 8;
    (void)v;   // the accent's gradient, top to bottom, in an anti-aliased rounded box
    Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::GraphicsPath path; round_path(path, (float)r.left, (float)r.top, (float)(r.right - r.left), (float)(r.bottom - r.top), (float)S(7));
    Gdiplus::LinearGradientBrush lb(Gdiplus::PointF(0, (float)r.top - 1), Gdiplus::PointF(0, (float)r.bottom + 1), gcolor(t), gcolor(b));
    g.FillPath(&lb, &path);
  } else frame(dc, r, hot ? C_BTN_HOT : C_BTN, watch ? RGB(0x5B, 0x4A, 0x8A) : C_BTN_BORDER, 7);
  const COLORREF fg = accent ? launcher::theme::on_accent() : watch ? C_WATCH : C_TEXT;
  const int iw = icon ? width_of(dc, g_f.icon_small, icon) + S(6) : 0, tw = width_of(dc, g_f.btn, label);
  int x = r.left + (r.right - r.left - iw - tw) / 2;
  if (icon) { text(dc, icon, rc(x, r.top, iw, r.bottom - r.top), g_f.icon_small, fg); x += iw; }
  text(dc, label, rc(x, r.top, tw + 2, r.bottom - r.top), g_f.btn, fg);
}
void chip(HDC dc, RECT r, const std::wstring& s, COLORREF bg, COLORREF fg, HFONT f) { fill(dc, r, bg, (r.bottom - r.top) / 2 * 96 / dpi()); text(dc, s, r, f, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
int chip_w(HDC dc, const std::wstring& s, HFONT f) { return width_of(dc, f, s) + S(14); }

// ---------------------------------------------------------------------------------------------- the page
std::wstring status_line(const std::wstring& what) {
  switch (lobby_state()) {
    case SRC_WAIT: return L"Loading " + what + L" from YouGame...";
    case SRC_OFFLINE: return L"Can't reach YouGame right now. Trying again shortly.";
    default: return L"YouGame hasn't opened " + what + L" to the launcher yet. This fills in by itself once it does.";
  }
}
void header(HDC dc, const Data& d, int W) {
  text(dc, L"Multiplayer Lobby", rc(S(22), S(20), S(260), S(36)), g_f.title, RGB(255, 255, 255));
  int x = S(22) + width_of(dc, g_f.title, L"Multiplayer Lobby") + S(12);
  if (g_preview) { chip(dc, rc(x, S(29), chip_w(dc, L"Preview", g_f.tiny), S(18)), L"Preview", C_CHIP, C_AMBER, g_f.tiny); x += chip_w(dc, L"Preview", g_f.tiny); }
  const int left = x + S(16);   // nothing on the right may reach the title
  int right = W - S(22);
  if (d.me_known) {   // you, top right: character, name and code, badge
    const int bw = S(30), nw = std::min(S(150), std::max(width_of(dc, g_f.bold, d.me.name), width_of(dc, g_f.sm, d.me.code)));
    badge(dc, my_tier_of(d), right - bw, S(23), 30); right -= bw + S(8);
    text(dc, d.me.name, rc(right - nw, d.me.code.empty() ? S(29) : S(21), nw, S(18)), g_f.bold, RGB(255, 255, 255));
    text(dc, d.me.code, rc(right - nw, S(38), nw, S(14)), g_f.sm, C_DIM); right -= nw + S(6);
    stock(dc, d.me.main, right - S(22), S(27), 22); hit(rc(right - S(24), S(25), S(26), S(26)), A_MY_MAIN); right -= S(22) + S(12);
    fill(dc, rc(right, S(24), 1, S(28)), C_SEP); right -= S(12);
  }
  if (d.known) {   // 128 online \u00B7 96 in game \u00B7 14 in rooms
    const std::wstring parts[3] = {std::to_wstring(d.online) + L" online", std::to_wstring(d.in_match) + L" in game", std::to_wstring(d.in_rooms) + L" in rooms"};
    int all = S(13); for (int i = 0; i < 3; ++i) all += width_of(dc, g_f.body, parts[i]) + (i ? S(14) : 0);
    const int first = right - all >= left ? 2 : right - S(13) - width_of(dc, g_f.body, parts[0]) >= left ? 0 : -1;   // narrow: only who's online, or nothing
    for (int i = first; i >= 0; --i) {
      const int w = width_of(dc, g_f.body, parts[i]); right -= w; text(dc, parts[i], rc(right, S(28), w + 1, S(18)), g_f.body, C_DIM);
      if (i == 0) { right -= S(13); fill(dc, rc(right, S(34), S(7), S(7)), C_OK, 4); } else right -= S(14);
    }
  }
}

void online_list(HDC dc, const Data& d, RECT p) {
  frame(dc, p, C_PANEL, C_SEP, 10);
  const int x0 = p.left + S(12), x1 = p.right - S(12);
  // Tabs and the Show Elo switch
  int x = x0; const int ty = p.top + S(8);
  const std::wstring tabs[2] = {L"Online" + (d.known ? L" " + std::to_wstring(d.online) : L""), L"Friends" + (d.known ? L" " + std::to_wstring(std::count_if(d.players.begin(), d.players.end(), [](const Player& q) { return q.friend_; })) : L"")};
  for (int i = 0; i < 2; ++i) {
    const int w = width_of(dc, g_f.bold, tabs[i]); RECT r = rc(x, ty, w, S(26));
    text(dc, tabs[i], r, g_f.bold, g_list_tab == i ? RGB(255, 255, 255) : C_DIM);
    if (g_list_tab == i) fill(dc, rc(x, ty + S(27), w, S(2)), launcher::theme::accent);
    hit(RECT{r.left - S(4), r.top, r.right + S(4), r.bottom + S(4)}, i == 0 ? A_TAB_ONLINE : A_TAB_FRIENDS); x += w + S(16);
  }
  {
    const int sw = S(28), sh = S(16);
    const wchar_t* label = x1 - sw - S(6) - width_of(dc, g_f.sm, L"Show Elo") < x ? L"Elo" : L"Show Elo";
    const int lw = width_of(dc, g_f.sm, label);
    RECT s = rc(x1 - sw, ty + S(5), sw, sh); fill(dc, s, g_elo ? launcher::theme::accent : C_FIELD_BORDER, 8);
    fill(dc, rc(g_elo ? s.right - S(14) : s.left + S(2), s.top + S(2), S(12), S(12)), RGB(255, 255, 255), 6);
    text(dc, label, rc(s.left - S(6) - lw, ty, lw + 1, S(26)), g_f.sm, g_elo ? C_TEXT : C_DIM);
    hit(RECT{s.left - S(6) - lw, ty, s.right, ty + S(26)}, A_ELO);
  }
  fill(dc, rc(x0, ty + S(29), x1 - x0, 1), C_SEP);
  int y = ty + S(36);
  if (!d.known) {
    text(dc, kPeople, rc(x0, y + S(40), x1 - x0, S(30)), g_f.icon, C_FAINT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT m = rc(x0 + S(8), y + S(76), x1 - x0 - S(16), S(60));
    text(dc, status_line(L"who's online"), m, g_f.body, C_DIM, DT_CENTER | DT_WORDBREAK);
    return;
  }
  auto section = [&](const std::wstring& name, int n) {
    text(dc, name, rc(x0 + S(2), y, S(120), S(20)), g_f.tiny, C_FAINT);
    text(dc, std::to_wstring(n), rc(x1 - S(60), y, S(58), S(20)), g_f.tiny, C_FAINT, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += S(22);
  };
  auto row = [&](const Player& q, int index) {
    const int lines = q.build ? 3 : 2, h = S(lines == 3 ? 56 : 42);
    if (y + h > p.bottom - S(6)) return false;
    const int eloW = g_elo ? S(30) + S(6) : 0;
    const int badgeX = x1 - eloW - S(24), eyeX = badgeX - S(8) - S(20), nameR = eyeX - S(8);
    const int midY = y + (h - S(24)) / 2;
    if (q.main >= 0 || !draw_avatar(dc, q.avatar, x0 + S(4), midY, S(24))) stock(dc, q.main, x0 + S(4), midY, 24);
    int nx = x0 + S(38), ny = y + (lines == 3 ? S(4) : S(4));
    if (q.friend_) { text(dc, kFriend, rc(nx, ny, S(16), S(18)), g_f.icon_small, C_FRIEND); nx += S(18); }
    text(dc, q.name, RECT{nx, ny, nameR, ny + S(18)}, g_f.name, C_TEXT);
    const int t = tier_of(q.rating);
    runs(dc, {{q.in_match ? L"In match" : L"Online", g_f.sm, q.in_match ? C_AMBER : C_OK}, {L" \u00B7 ", g_f.sm, C_DIM},
              {t >= 0 ? kTierName[t] : L"No rank yet", g_f.sm, t >= 0 ? kTierColor[t] : RGB(0xAE, 0xB8, 0xC8)}}, x0 + S(38), ny + S(18), S(16), nameR);
    if (q.build) text(dc, kBuildName[q.build], RECT{x0 + S(38), ny + S(34), nameR, ny + S(50)}, g_f.sm, kBuildColor[q.build]);
    if (q.in_match) {
      RECT e = rc(eyeX, midY, S(20), S(24)); text(dc, kEye, e, g_f.icon, C_WATCH, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      hit(e, A_PLAYER_WATCH, index);
    }
    badge(dc, t, badgeX + S(1), midY + S(1), 22);
    if (g_elo) text(dc, q.rating >= 0 ? std::to_wstring(q.rating) : L"\u2013", rc(x1 - S(30), midY, S(30), S(24)), g_f.btn, t >= 0 ? kTierColor[t] : C_FAINT, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += h + S(2);
    return true;
  };
  int friends = 0; for (const auto& q : d.players) friends += q.friend_;
  std::vector<int> list;   // friends first, then everyone (Online tab)
  for (int i = 0; i < (int)d.players.size(); ++i) if (d.players[i].friend_) list.push_back(i);
  if (g_list_tab == 0) for (int i = 0; i < (int)d.players.size(); ++i) if (!d.players[i].friend_) list.push_back(i);
  if (list.empty()) {
    if (!d.players_known) hit(rc(x0, y + S(30), x1 - x0, S(40)), A_SIGN_IN);   // signs in by code
    text(dc, !d.players_known ? L"Sign in to see who's online." : g_list_tab == 1 ? L"None of your friends are online." : L"Nobody else is online right now.", rc(x0, y + S(30), x1 - x0, S(40)), g_f.body, C_DIM, DT_CENTER | DT_WORDBREAK);
    return;
  }
  Scroll& sc = g_players_scroll; sc.area = RECT{p.left, y, p.right, p.bottom}; sc.total = (int)list.size();
  sc.skip = std::clamp(sc.skip, 0, std::max(0, sc.total - 1)); sc.fit = 0;
  const int top = y; int last = -1;
  for (int k = sc.skip; k < sc.total; ++k) {
    const int sec = d.players[list[k]].friend_ ? 0 : 1;
    if (sec != last) {
      if (y + S(22 + 42) > p.bottom - S(6)) break;
      if (last == 0) y += S(4);
      if (sec == 0) section(L"FRIENDS", friends); else section(L"EVERYONE", std::max(0, d.online - friends));
      last = sec;
    }
    if (!row(d.players[list[k]], list[k])) break;
    ++sc.fit;
  }
  scrollbar(dc, sc, p.right - S(6), top + S(2), p.bottom - top - S(10));
}
void scrollbar(HDC dc, const Scroll& s, int x, int top, int h) {   // a thin thumb, only when rows are hidden
  if (s.total <= s.fit || s.total <= 0 || h < S(20)) return;
  const int th = std::max(S(24), h * s.fit / s.total), ty = top + (h - th) * s.skip / std::max(1, s.total - s.fit);
  fill(dc, rc(x, top, S(4), h), C_SEP, 2);
  fill(dc, rc(x, std::min(ty, top + h - th), S(4), th), C_DIM, 2);
}

void live_bar(HDC dc, const Data& d, RECT p) {
  frame(dc, p, C_PANEL, C_SEP, 10);
  const Live& m = d.live.front();
  int x = p.left + S(12); const int cy = p.top + (p.bottom - p.top) / 2;
  const int cw = chip_w(dc, L"\u25CF LIVE", g_f.tiny);
  chip(dc, rc(x, cy - S(10), cw, S(20)), L"\u25CF LIVE", C_LIVE_BG, C_LIVE, g_f.tiny); x += cw + S(10);
  RECT w = rc(p.right - S(12) - S(82), cy - S(14), S(82), S(28));
  const std::wstring views = std::to_wstring(m.viewers); const int vw = width_of(dc, g_f.body, views);
  const int vx = w.left - S(14) - vw;
  text(dc, views, rc(vx, cy - S(9), vw + 1, S(18)), g_f.body, C_DIM);
  text(dc, kEye, rc(vx - S(20), cy - S(9), S(16), S(18)), g_f.icon_small, C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  const int namesR = vx - S(30);
  auto side = [&](const std::vector<Side>& s) { for (const auto& q : s) { stock(dc, q.main, x, cy - S(10), 20); x += S(25); const int nw = width_of(dc, g_f.bold, q.name); text(dc, q.name, RECT{x, cy - S(10), std::min(namesR, x + nw + 1), cy + S(10)}, g_f.bold, C_TEXT); x += nw + S(8); } };
  side(m.a); text(dc, L"vs", rc(x, cy - S(9), S(18), S(18)), g_f.sm, C_DIM); x += S(22); side(m.b);
  button(dc, w, L"Watch", true, A_LIVE_WATCH, 0, kEye);
}

void queues(HDC dc, const Data& d, RECT p) {
  frame(dc, p, C_PANEL, C_SEP, 10);
  const wchar_t* names[3] = {L"Play unranked", L"Play ranked", L"Play 2v2"};
  const int gap = S(8), w = (p.right - p.left - S(28) - gap * 2) / 3;
  for (int i = 0; i < 3; ++i) {
    RECT b = rc(p.left + S(14) + i * (w + gap), p.top + S(12), w, S(32));
    button(dc, b, names[i], i == 1, A_PLAY, i);
    if (d.known) text(dc, std::to_wstring(d.waiting[i]) + L" in queue \u00B7 " + std::to_wstring(d.playing[i]) + L" in game",
                      rc(b.left, b.bottom + S(4), w, S(16)), g_f.sm, C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void place_code(RECT r) {
  if (!g_code) return;
  RECT cur{}; GetWindowRect(g_code, &cur); MapWindowPoints(nullptr, g_window, (POINT*)&cur, 2);
  if (!EqualRect(&cur, &r)) MoveWindow(g_code, r.left, r.top, r.right - r.left, r.bottom - r.top, FALSE);
  if (!IsWindowVisible(g_code)) ShowWindow(g_code, SW_SHOWNA);
}

void rooms_panel(HDC dc, const Data& d, RECT p) {
  frame(dc, p, C_PANEL, C_SEP, 10);
  const int x0 = p.left + S(14), x1 = p.right - S(14);
  const auto room = launcher::room::current();
  if (!room.code.empty() || !room.error.empty()) {
    if (g_code) ShowWindow(g_code, SW_HIDE);
    int y = p.top + S(14);
    text(dc, wide(room.name.empty() ? "Room" : room.name), rc(x0, y, x1-x0, S(26)), g_f.bold, C_TEXT);
    y += S(32);
    text(dc, wide(std::string(launcher::room::variant_name(room.variant)) + "  " + room.code),
         rc(x0, y, x1-x0, S(24)), g_f.body, C_DIM);
    y += S(36);
    for (const auto& member : room.members) {
      text(dc, wide(member.second.name + (member.first == room.host ? " (host)" : "")),
           rc(x0, y, x1-x0-S(70), S(24)), g_f.body, C_TEXT);
      text(dc, member.second.ready ? L"Ready" : L"Checking", rc(x1-S(70), y, S(70), S(24)),
           g_f.sm, member.second.ready ? C_OK : C_DIM);
      y += S(30);
    }
    const std::string status = !room.error.empty() ? room.error : room.started ? "Arena open. Players can join using this room code."
        : room.launching ? "Opening the host arena..." : room.connected ? "Waiting for players" : "Connecting to YouGame...";
    text(dc, wide(status), rc(x0, y+S(10), x1-x0, S(50)), g_f.body, room.error.empty() ? C_DIM : C_AMBER, DT_WORDBREAK);
    y += S(74);
    int x = x0;
    if (room.hosting() && !room.started) {
      button(dc, rc(x,y,S(90),S(30)), L"Start", room.can_start(), room.can_start() ? A_ROOM_START : -1);
      x += S(100);
    }
    button(dc, rc(x,y,S(90),S(30)), L"Invite", false, A_ROOM_INVITE); x += S(100);
    button(dc, rc(x,y,S(90),S(30)), L"Copy code", false, A_ROOM_COPY);
    button(dc, rc(x0,y+S(40),S(90),S(30)), L"Leave", false, A_ROOM_LEAVE);
    return;
  }
  int x = x0; const int ty = p.top + S(10);
  const std::wstring tabs[2] = {L"Public rooms" + (d.known ? L" " + std::to_wstring(d.rooms.size()) : L""), L"Live games" + (d.known ? L" " + std::to_wstring(d.live.size()) : L"")};
  for (int i = 0; i < 2; ++i) {
    const int iw = i == 1 ? width_of(dc, g_f.icon_small, kEye) + S(7) : 0, w = iw + width_of(dc, g_f.bold, tabs[i]);
    const COLORREF c = g_rooms_tab == i ? RGB(255, 255, 255) : C_DIM;
    if (iw) text(dc, kEye, rc(x, ty + S(1), iw, S(26)), g_f.icon_small, c);
    text(dc, tabs[i], rc(x + iw, ty, w - iw + 1, S(26)), g_f.bold, c);
    if (g_rooms_tab == i) fill(dc, rc(x, ty + S(27), w, S(2)), launcher::theme::accent);
    hit(RECT{x - S(4), ty, x + w + S(4), ty + S(30)}, i == 0 ? A_TAB_ROOMS : A_TAB_LIVE); x += w + S(20);
  }
  int y = ty + S(40);
  if (g_rooms_tab == 0) {
    // Standard \u00B7 [Room code  Join] \u00B7 [Public][Private], then Create room
    x = x0;
    text(dc, kRules, rc(x, y, S(16), S(30)), g_f.icon_small, C_DIM); x += S(20);
    text(dc, L"Standard", rc(x, y, S(60), S(30)), g_f.body, C_DIM); x += width_of(dc, g_f.body, L"Standard") + S(12);
    const int segW = S(178), codeW = std::max(S(150), x1 - segW - S(10) - x);
    RECT box = rc(x, y, codeW, S(30)); frame(dc, box, C_FIELD, C_FIELD_BORDER, 7);
    RECT join = rc(box.right - S(4) - S(44), y + S(4), S(44), S(22)); button(dc, join, L"Join", false, A_CODE_JOIN);
    place_code(RECT{box.left + S(9), y + S(7), join.left - S(6), y + S(25)});
    RECT pub = rc(x1 - segW, y, (segW - S(6)) / 2, S(30)), pri = rc(pub.right + S(6), y, (segW - S(6)) / 2, S(30));
    button(dc, pub, L"Public", g_public, A_PUBLIC); button(dc, pri, L"Private", !g_public, A_PRIVATE, 0, kLock);
    y += S(38);
    button(dc, rc(x0, y, S(108), S(30)), L"Create room", true, A_CREATE);
    y += S(40);
    if (!d.known) {
      text(dc, status_line(L"public rooms"), rc(x0, y + S(20), x1 - x0, S(60)), g_f.body, C_DIM, DT_CENTER | DT_WORDBREAK);
      return;
    }
    if (d.rooms.empty()) { text(dc, L"No public rooms right now. Create one!", rc(x0, y + S(20), x1 - x0, S(40)), g_f.body, C_DIM, DT_CENTER | DT_WORDBREAK); return; }
    Scroll& sc = g_list_scroll; sc.area = RECT{p.left, y, p.right, p.bottom}; sc.total = (int)d.rooms.size();
    sc.skip = std::clamp(sc.skip, 0, std::max(0, sc.total - 1)); sc.fit = 0;
    const int list_top = y;
    for (int i = sc.skip; i < (int)d.rooms.size(); ++i) {
      const Room& r = d.rooms[i]; const int h = S(48);
      if (y + h > p.bottom - S(8)) break;
      ++sc.fit;
      if (i > sc.skip) fill(dc, rc(x0, y, x1 - x0, 1), C_SEP);
      const int cy = y + h / 2;
      // columns: characters 56 | name 1fr | mode 56 | status 66 | button 60
      const int bx = x1 - S(60), sx = bx - S(8) - S(6) - S(66), mx = sx - S(8) - S(56), nx = x0 + S(56) + S(8);
      int ix = x0; for (int c : r.members) { stock(dc, c, ix, cy - S(7), 14); ix += S(14); }
      text(dc, r.name, RECT{nx, cy - S(17), mx - S(8), cy}, g_f.bold, C_TEXT);
      std::vector<Run> sub = {{r.host, g_f.sm, C_DIM}};
      if (r.build) { sub.push_back({L" \u00B7 ", g_f.sm, C_DIM}); sub.push_back({kBuildName[r.build], g_f.sm, kBuildColor[r.build]}); }
      runs(dc, sub, nx, cy + S(1), S(16), mx - S(8));
      const wchar_t* mode = r.mode == 0 ? L"1v1" : r.mode == 1 ? L"Teams" : L"FFA";
      const int mw = chip_w(dc, mode, g_f.tiny); chip(dc, rc(mx + (S(56) - mw) / 2, cy - S(17), mw, S(17)), mode, C_CHIP, C_CHIP_TEXT, g_f.tiny);
      text(dc, r.rules, rc(mx - S(4), cy + S(2), S(64), S(15)), g_f.sm, C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      text(dc, r.in_game ? L"In game" : L"Waiting", rc(sx, cy - S(17), S(66), S(16)), g_f.body, r.in_game ? C_AMBER : C_OK);
      text(dc, r.in_game ? std::to_wstring(r.viewers) + L" watching" : std::to_wstring(r.players) + L"/" + std::to_wstring(r.size) + L" players", rc(sx, cy + S(1), S(70), S(15)), g_f.sm, C_DIM);
      RECT b = rc(bx, cy - S(13), S(60), S(26));
      if (r.in_game) button(dc, b, L"Watch", false, A_ROOM, i, nullptr, true);
      else if (r.players >= r.size) { frame(dc, b, C_BTN, C_BTN_BORDER, 7); text(dc, L"Full", b, g_f.btn, C_FAINT, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
      else button(dc, b, L"Join", true, A_ROOM, i);
      y += h;
    }
    scrollbar(dc, sc, p.right - S(7), list_top + S(2), p.bottom - list_top - S(12));
  } else {
    if (g_code) ShowWindow(g_code, SW_HIDE);
    const wchar_t* filters[5] = {L"All", L"Ranked", L"Unranked", L"2v2", L"Tournament sets"};
    x = x0;
    for (int i = 0; i < 5; ++i) {
      const int w = chip_w(dc, filters[i], g_f.tiny); RECT r = rc(x, y, w, S(20));
      chip(dc, r, filters[i], g_filter == i ? RGB(0x3B, 0x2A, 0x66) : C_CHIP, g_filter == i ? RGB(255, 255, 255) : C_CHIP_TEXT, g_f.tiny);
      hit(r, A_FILTER, i); x += w + S(6);
    }
    text(dc, L"Most watched first", rc(x1 - S(120), y, S(120), S(20)), g_f.sm, C_DIM, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += S(30);
    if (!d.known || d.live.empty()) {
      text(dc, d.known ? (d.in_match > 0 ? std::to_wstring(d.in_match) + (d.in_match == 1 ? L" player is" : L" players are") + L" in a match. YouGame doesn't list matches to watch yet; they show here once it does." : std::wstring(L"No matches being played right now.")) : status_line(L"matches being played"), rc(x0, y + S(20), x1 - x0, S(60)), g_f.body, C_DIM, DT_CENTER | DT_WORDBREAK);
      return;
    }
    const int want[5] = {-1, 0, 1, 2, 3};
    std::vector<int> order;
    for (int i = 0; i < (int)d.live.size(); ++i) if (want[g_filter] < 0 || d.live[i].kind == want[g_filter]) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return d.live[a].viewers > d.live[b].viewers; });
    Scroll& sc = g_list_scroll; sc.area = RECT{p.left, y, p.right, p.bottom}; sc.total = (int)order.size();
    sc.skip = std::clamp(sc.skip, 0, std::max(0, sc.total - 1)); sc.fit = 0;
    const int list_top = y;
    bool first = true;
    for (int k = sc.skip; k < (int)order.size(); ++k) {
      const int i = order[k];
      const Live& m = d.live[i]; const int h = S(56);
      if (y + h > p.bottom - S(8)) break;
      ++sc.fit;
      if (!first) fill(dc, rc(x0, y, x1 - x0, 1), C_SEP); first = false;
      const int bx = x1 - S(76), vx = bx - S(10) - S(36);
      const wchar_t* kinds[4] = {L"Ranked", L"Unranked", L"2v2", L"Tournament"};
      const int kw = chip_w(dc, kinds[m.kind], g_f.tiny), kx = vx - S(10) - kw;
      int lx = x0; const int ly = y + S(9);
      auto side = [&](const std::vector<Side>& s) { for (const auto& q : s) { stock(dc, q.main, lx, ly, 18); lx += S(22); const int nw = width_of(dc, g_f.bold, q.name); text(dc, q.name, RECT{lx, ly, std::min(kx - S(8), lx + nw + 1), ly + S(18)}, g_f.bold, C_TEXT); lx += nw + S(8); } };
      side(m.a); text(dc, L"vs", rc(lx, ly, S(16), S(18)), g_f.sm, C_DIM); lx += S(20); side(m.b);
      int dx = x0; const int dy = y + S(33);
      if (m.build) { const int bw = chip_w(dc, kBuildName[m.build], g_f.tiny); chip(dc, rc(dx, dy, bw, S(16)), kBuildName[m.build], RGB(0x3A, 0x26, 0x2A), kBuildColor[m.build], g_f.tiny); dx += bw + S(6); }
      text(dc, m.what + L" \u00B7 " + m.game, RECT{dx, dy, kx - S(8), dy + S(16)}, g_f.sm, C_DIM);
      const COLORREF kbg = m.kind == 3 ? RGB(0x3A, 0x2D, 0x10) : m.kind == 0 ? RGB(0x2B, 0x23, 0x50) : C_CHIP;
      const COLORREF kfg = m.kind == 3 ? RGB(0xF0, 0xC2, 0x4A) : m.kind == 0 ? C_WATCH : C_CHIP_TEXT;
      chip(dc, rc(kx, y + (h - S(18)) / 2, kw, S(18)), kinds[m.kind], kbg, kfg, g_f.tiny);
      text(dc, kEye, rc(vx, y, S(16), h), g_f.icon_small, C_DIM, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      text(dc, std::to_wstring(m.viewers), rc(vx + S(18), y, S(22), h), g_f.sm, C_DIM);
      button(dc, rc(bx, y + (h - S(26)) / 2, S(76), S(26)), L"Watch", false, A_LIVE_ROW, i, kEye, true);
      y += h;
    }
    scrollbar(dc, sc, p.right - S(7), list_top + S(2), p.bottom - list_top - S(12));
  }
}

void trace(const char* fmt, int a, int b, int c) {   // test runs only: what the page did, next to the launcher
  if (!std::getenv("MELEE_LAUNCHER_TEST")) return;
  if (FILE* f = std::fopen("yglobby-trace.log", "a")) { std::fprintf(f, fmt, a, b, c); std::fputc(10, f); std::fclose(f); }
}
void paint(HDC target) {
  RECT client{}; GetClientRect(g_window, &client); const int W = client.right, H = client.bottom;
  { const int real = GetDpiForWindow(g_window);
    const double f = std::min({1.0, W / (790.0 * real / 96), H / (600.0 * real / 96)});
    g_fit_dpi = std::max(48, (int)std::floor(real * f)); }
  trace("paint %d x %d visible %d", W, H, IsWindowVisible(g_window));
  HDC dc = CreateCompatibleDC(target); HBITMAP bm = CreateCompatibleBitmap(target, W, H); HGDIOBJ old = SelectObject(dc, bm);
  fonts(); g_hits.clear();
  TRIVERTEX v[2] = {{0, 0, (COLOR16)(GetRValue(C_BG_TOP) << 8), (COLOR16)(GetGValue(C_BG_TOP) << 8), (COLOR16)(GetBValue(C_BG_TOP) << 8), 0},
                    {W, H, (COLOR16)(GetRValue(C_BG_BOT) << 8), (COLOR16)(GetGValue(C_BG_BOT) << 8), (COLOR16)(GetBValue(C_BG_BOT) << 8), 0}};
  GRADIENT_RECT g{0, 1}; GradientFill(dc, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
  const Data d = data();
  header(dc, d, W);
  const int top = S(68), bottom = H - S(18), leftW = S(276), gap = S(14);
  const int sidebar = W - S(22) - leftW;
  online_list(dc, d, RECT{sidebar, top, W-S(22), bottom});
  const int rx0 = S(22), rx1 = sidebar - gap;
  int y = top;
  if (d.known && !d.live.empty()) { live_bar(dc, d, RECT{rx0, y, rx1, y + S(50)}); y += S(50) + gap; }
  queues(dc, d, RECT{rx0, y, rx1, y + (d.known ? S(72) : S(56))}); y += (d.known ? S(72) : S(56)) + gap;
  rooms_panel(dc, d, RECT{rx0, y, rx1, bottom});
  if (!g_toast.empty() && GetTickCount64() < g_toast_until) {
    const int tw = width_of(dc, g_f.body, g_toast) + S(28); RECT t = rc((W - tw) / 2, H - S(56), tw, S(32));
    frame(dc, t, RGB(0x26, 0x30, 0x4A), C_BTN_BORDER, 8); text(dc, g_toast, t, g_f.body, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  BitBlt(target, 0, 0, W, H, dc, 0, 0, SRCCOPY);
  SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc);
}

void open_yougame(const std::wstring& path) {   // production unless the launcher is set to staging
  if (std::getenv("MELEE_LAUNCHER_TEST") && std::getenv("MELEE_LAUNCHER_TEST_SCRIPT")) {   // scripted test runs: logged, not opened
    if (FILE* f = std::fopen("launcher-actions.log", "ab")) { std::fprintf(f, "open %ls\n", path.c_str()); std::fclose(f); }
    return;
  }
  std::wstring site = wide(launcher::yougame::site_url()); if (site.empty()) site = L"https://yougame.co";
  ShellExecuteW(nullptr, L"open", (site + path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
void toast(const std::wstring& s) {
  g_toast = s; g_toast_until = GetTickCount64() + 4000; SetTimer(g_window, 1, 4100, nullptr); InvalidateRect(g_window, nullptr, FALSE);
}
void act(int a, int arg) {
  const Data d = data();
  switch (a) {
    case A_TAB_ONLINE: g_list_tab = 0; g_players_scroll.skip = 0; break;
    case A_TAB_FRIENDS: g_list_tab = 1; g_players_scroll.skip = 0; break;
    case A_ELO: g_elo = !g_elo; if (g_cb.changed) g_cb.changed(); break;
    case A_TAB_ROOMS: g_rooms_tab = 0; g_list_scroll.skip = 0; break;
    case A_TAB_LIVE: g_rooms_tab = 1; g_list_scroll.skip = 0; break;
    case A_FILTER: g_filter = arg; g_list_scroll.skip = 0; break;
    case A_PUBLIC: g_public = true; break;
    case A_PRIVATE: g_public = false; break;
    case A_PLAY: if (repeat_press()) return; if (g_cb.play) g_cb.play(arg); return;
    case A_ROOM_START: launcher::room::start(); return;
    case A_ROOM_INVITE: launcher::room::invite(); return;
    case A_ROOM_LEAVE: launcher::room::leave(); return;
    case A_ROOM_COPY: {
      const auto room = launcher::room::current();
      const std::wstring value = wide(room.code);
      if (OpenClipboard(g_window)) {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (value.size()+1)*sizeof(wchar_t));
        if (memory) { void* data = GlobalLock(memory); memcpy(data,value.c_str(),(value.size()+1)*sizeof(wchar_t));
          GlobalUnlock(memory); EmptyClipboard(); if (!SetClipboardData(CF_UNICODETEXT,memory)) GlobalFree(memory); }
        CloseClipboard();
      }
      return;
    }
    case A_CREATE:
      if (repeat_press()) return;
      create_room(g_public); return;
    case A_CODE_JOIN: case A_ROOM: {   // YouGame's own invite link: YouGame starts the game into that room
      std::wstring code;
      if (a == A_ROOM && arg >= 0 && arg < (int)d.rooms.size()) code = d.rooms[arg].code;
      if (a == A_CODE_JOIN && g_code) { wchar_t b[32] = {}; GetWindowTextW(g_code, b, 31); code = b; }
      if (code.empty()) { toast(L"Type a room code first."); return; }
      code = room_code(code);
      if (code.empty()) { toast(L"A room code is 6 to 12 letters or numbers."); return; }
      if (repeat_press()) return;
      join(code); return;
    }
    case A_LIVE_WATCH: case A_LIVE_ROW: {   // YouGame's own watch page for that match
      if (repeat_press()) return;
      const int i = a == A_LIVE_WATCH ? 0 : arg;
      if (i >= 0 && i < (int)d.live.size() && !d.live[i].code.empty()) { open_yougame(L"/g/" + wide(kSlug) + L"/watch/" + d.live[i].code); toast(L"Opening the match in YouGame..."); }
      return;
    }
    case A_PLAYER_WATCH: toast(L"Watching in the game needs YouGame's match list and watch tickets. Not out yet."); InvalidateRect(g_window, nullptr, FALSE); return;
    case A_MY_MAIN: { POINT p{}; GetCursorPos(&p); pick_main(g_window, p); InvalidateRect(g_window, nullptr, FALSE); return; }
    case A_SIGN_IN: if (code_sign_in(nullptr) == CodeSignIn::kWaiting) open_code_link(); else start_code_sign_in(); break;
  }
  InvalidateRect(g_window, nullptr, FALSE);
}

LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
    case WM_MOUSEWHEEL: {   // three rows a notch, never past the last row
      POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)}; ScreenToClient(w, &pt);
      for (Scroll* sc : {&g_players_scroll, &g_list_scroll}) if (PtInRect(&sc->area, pt)) {
        static int acc = 0; acc += GET_WHEEL_DELTA_WPARAM(wp);
        const int rows = acc * 3 / WHEEL_DELTA; acc -= rows * WHEEL_DELTA / 3;
        const int before = sc->skip; sc->skip = std::clamp(sc->skip - rows, 0, std::max(0, sc->total - sc->fit));
        if (sc->skip != before) InvalidateRect(w, nullptr, FALSE);
        break;
      }
      return 0;
    }
    case WM_DROPFILES: return SendMessageW(GetParent(w), WM_DROPFILES, wp, lp);   // the launcher's own handler
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(w, &ps); paint(dc); EndPaint(w, &ps); return 0; }
    case WM_PRINT: case WM_PRINTCLIENT: paint((HDC)wp); if (m == WM_PRINT && g_code && IsWindowVisible(g_code)) {
        RECT r{}; GetWindowRect(g_code, &r); MapWindowPoints(nullptr, w, (POINT*)&r, 2); const int s = SaveDC((HDC)wp);
        POINT o{}; GetViewportOrgEx((HDC)wp, &o); SetViewportOrgEx((HDC)wp, o.x + r.left, o.y + r.top, nullptr);
        SendMessageW(g_code, WM_PRINT, wp, PRF_CLIENT | PRF_ERASEBKGND); RestoreDC((HDC)wp, s); }
      return 0;
    case WM_TIMER: KillTimer(w, wp); InvalidateRect(w, nullptr, FALSE); return 0;
    case WM_APP + 1: InvalidateRect(w, nullptr, FALSE); return 0;   // the worker learned something
    case WM_MOUSEMOVE: {
      // Scripted hidden test runs: only the script's moves (marked 0x4000) count; the hidden desktop's own pointer
      // sends moves of its own that would clear the hover the script set.
      if (static const bool script_run = std::getenv("MELEE_LAUNCHER_TEST") && std::getenv("MELEE_LAUNCHER_TEST_SCRIPT"); script_run && !(wp & 0x4000)) return 0;
      POINT p{(short)LOWORD(lp), (short)HIWORD(lp)}; int hot = -1;
      for (int i = 0; i < (int)g_hits.size(); ++i) if (PtInRect(&g_hits[i].r, p)) hot = i;
      if (hot != g_hot) { g_hot = hot; g_hot_act = hot >= 0 ? g_hits[hot].act : -1; g_hot_arg = hot >= 0 ? g_hits[hot].arg : 0; InvalidateRect(w, nullptr, FALSE); }
      // Scripted hidden test runs have no real pointer over the window (leave would come at once): hover stays.
      static const bool scripted = std::getenv("MELEE_LAUNCHER_TEST") && std::getenv("MELEE_LAUNCHER_TEST_SCRIPT");
      if (!g_tracking && !scripted) { TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, w, 0}; TrackMouseEvent(&t); g_tracking = true; }
      return 0;
    }
    case WM_MOUSELEAVE: g_tracking = false; if (g_hot >= 0) { g_hot = -1; g_hot_act = -1; InvalidateRect(w, nullptr, FALSE); } return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        const HCURSOR h = launcher::melee_look::hand_now(g_hot >= 0, HIWORD(lp));
        if (launcher::pad::hand_shown()) { SetCursor(nullptr); return TRUE; }   // one hand: the controller's
        if (h) { SetCursor(h); return TRUE; }
      }
      if (LOWORD(lp) == HTCLIENT && g_hot >= 0) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; } break;
    case WM_LBUTTONDOWN: {
      POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
      for (int i = (int)g_hits.size() - 1; i >= 0; --i) if (PtInRect(&g_hits[i].r, p)) { act(g_hits[i].act, g_hits[i].arg); break; }
      return 0;
    }
    case WM_CTLCOLOREDIT: { HDC dc = (HDC)wp; SetTextColor(dc, C_TEXT); SetBkColor(dc, C_FIELD); static HBRUSH b = CreateSolidBrush(C_FIELD); return (LRESULT)b; }
    case WM_SIZE: InvalidateRect(w, nullptr, FALSE); return 0;
  }
  return DefWindowProcW(w, m, wp, lp);
}

}  // namespace

void init(HWND owner, const Callbacks& callbacks) {
  g_owner = owner; g_cb = callbacks;
  if (!g_worker.joinable()) { g_worker = std::thread(work, callbacks.version); g_worker.detach(); }
  if (const char* t = std::getenv("MELEE_LAUNCHER_TEST") ? std::getenv("MELEE_LAUNCHER_TEST_LOBBY_STATE") : nullptr) {
    const std::string st = t;
    if (st.find("elo") != std::string::npos) g_elo = true;
    if (st.find("live") != std::string::npos) g_rooms_tab = 1;
    if (st.find("friends") != std::string::npos) g_list_tab = 1;
    if (st.find("private") != std::string::npos) g_public = false;
  }
  WNDCLASSW wc{}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"MeleeUnlockedYouGameLobby";
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
}
void open() {
  if (!g_owner) return;
  RECT client{}; GetClientRect(g_owner, &client);
  const int rail = MulDiv(190, GetDpiForWindow(g_owner), 96);
  if (!g_window) {
    g_window = CreateWindowExW(WS_EX_ACCEPTFILES, L"MeleeUnlockedYouGameLobby", L"", WS_CHILD | WS_CLIPCHILDREN, rail, 0, client.right - rail, client.bottom,
                               g_owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_code = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, g_window, nullptr, GetModuleHandleW(nullptr), nullptr);
    fonts(); SendMessageW(g_code, WM_SETFONT, (WPARAM)g_f.body, FALSE);
    SendMessageW(g_code, EM_SETCUEBANNER, TRUE, (LPARAM)L"Room code");
    SendMessageW(g_code, EM_LIMITTEXT, 16, 0);
    SetWindowSubclass(g_code, [](HWND e, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) -> LRESULT {
      // the launcher's dialog loop keeps Enter for itself unless the box asks for it
      if (m == WM_GETDLGCODE && lp && ((MSG*)lp)->message == WM_KEYDOWN && ((MSG*)lp)->wParam == VK_RETURN) return DLGC_WANTALLKEYS;
      if (m == WM_KEYDOWN && wp == VK_RETURN) { act(A_CODE_JOIN, 0); return 0; }
      if (m == WM_CHAR && (wp == L'\r' || wp == L'\n')) return 0;   // no beep
      return DefSubclassProc(e, m, wp, lp);
    }, 1, 0);
  }
  MoveWindow(g_window, rail, 0, client.right - rail, client.bottom, FALSE);
  trace("open client %d x %d rail %d", client.right, client.bottom, rail);
  ShowWindow(g_window, SW_SHOW); InvalidateRect(g_window, nullptr, FALSE);
  g_window_for_worker.store(g_window); g_reopened.store(true);
  { std::lock_guard<std::mutex> lock(g_live_mx); g_live_state.rank_known = false; }   // your rank: once per opening
}
void hide() { if (g_window) ShowWindow(g_window, SW_HIDE); }
void fit() {   // the launcher window changed size: the page fills the column beside the rail again
  if (!g_window || !IsWindowVisible(g_window)) return;
  RECT client{}; GetClientRect(g_owner, &client); const int rail = MulDiv(190, GetDpiForWindow(g_owner), 96);
  MoveWindow(g_window, rail, 0, client.right - rail, client.bottom, FALSE); InvalidateRect(g_window, nullptr, FALSE);
}
bool shown() { return g_window && IsWindowVisible(g_window); }
Data current() { return data(); }
void draw_badge(HDC dc, int rating, int x, int y, int px) { g_exact_px = true; badge(dc, tier_of(rating), x, y, px); g_exact_px = false; }   // px: real pixels
bool draw_avatar(HDC dc, const std::string& url, int x, int y, int px) {
  if (url.empty()) return false;
  std::shared_ptr<Gdiplus::Bitmap> pic; { std::lock_guard<std::mutex> lock(g_pic_mx); auto it = g_pics.find(url); if (it != g_pics.end()) pic = it->second; }
  if (!pic) return false;
  Gdiplus::Graphics g(dc); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic); g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  return g.DrawImage(pic.get(), Gdiplus::Rect(x, y, px, px), 0, 0, 64, 64, Gdiplus::UnitPixel) == Gdiplus::Ok;
}
void draw_stock(HDC dc, int main, int x, int y, int px) {
  if (main < 0) { HBRUSH b = CreateSolidBrush(RGB(0x2B, 0x36, 0x52)); HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, GetStockObject(NULL_PEN)); Ellipse(dc, x, y, x + px, y + px); SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b); return; }
  if (HICON i = stock_icon(main, px)) DrawIconEx(dc, x, y, i, px, px, 0, nullptr, DI_NORMAL);
}
int tier_of_rating(int rating) { return tier_of(rating); }
int my_tier(const Data& d) { return my_tier_of(d); }
void draw_tier_badge(HDC dc, int tier, int x, int y, int px) { g_exact_px = true; badge(dc, tier, x, y, px); g_exact_px = false; }
int source_state() { return lobby_state(); }
void attach_view(HWND view) { g_view_for_worker.store(view); g_reopened.store(true); { std::lock_guard<std::mutex> lock(g_live_mx); g_live_state.rank_known = false; } }
std::wstring room_code(const std::wstring& typed) {
  std::wstring code;
  for (wchar_t c : typed) { if (c == L'-' || c == L' ' || c == L'\t') continue; if (c > 127 || !iswalnum(c)) return {}; code += (wchar_t)towlower(c); }
  return code.size() >= 6 && code.size() <= 12 ? code : std::wstring();
}
bool repeat_press() {
  static ULONGLONG last = 0; const ULONGLONG now = GetTickCount64();
  const bool repeat = last && now - last < 1000; if (!repeat) last = now; return repeat;
}
void refresh() { publish_view(); }
void join(const std::wstring& code_in) {
  const std::wstring code = room_code(code_in);
  if (!code.empty()) {
    launcher::room::open(std::string(code.begin(), code.end()), 0, false);
    PostMessageW(g_owner, WM_APP+70, 0, 0);
    open();
  }
}
void watch(const std::wstring& code) { if (!code.empty()) open_yougame(L"/g/" + wide(kSlug) + L"/watch/" + code); }
void create_room(bool is_public) {
  HMENU menu = CreatePopupMenu();
  for (int variant=0; variant<4; ++variant)
    AppendMenuW(menu, MF_STRING, variant+1, wide(launcher::room::variant_name(variant)).c_str());
  POINT point{}; GetCursorPos(&point);
  const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, g_owner, nullptr);
  DestroyMenu(menu);
  if (!choice) return;
  launcher::room::open({}, choice-1, is_public);
  PostMessageW(g_owner, WM_APP+70, 0, 0);
  open();
}
void play(int queue) { if (g_cb.play) g_cb.play(queue); }
void open_yougame_page() { open_yougame(L"/g/" + wide(kSlug)); }
void set_preview(bool on) { g_preview = on; if (g_window) InvalidateRect(g_window, nullptr, FALSE); }
bool preview() { return g_preview; }
void set_elo(bool on) { g_elo = on; if (g_window) InvalidateRect(g_window, nullptr, FALSE); }
void set_my_main(int character, bool picked) {
  if (picked) { launcher::lobby::save_mains(character >= 0 && character < 26 ? std::vector<int>{character} : std::vector<int>{}); g_mains_dirty = true; }
  else g_my_main = character >= 0 && character < 26 ? character : -1;
  if (g_window) InvalidateRect(g_window, nullptr, FALSE);
}
bool main_picked() { return !launcher::lobby::saved_mains().empty(); }
int my_main() { const auto v = my_mains(); return v.empty() ? -1 : v.front(); }
bool pick_main(HWND owner, POINT at) {
  static const wchar_t* names[26] = {L"Captain Falcon", L"Donkey Kong", L"Fox", L"Mr. Game & Watch", L"Kirby", L"Bowser", L"Link", L"Luigi",
    L"Mario", L"Marth", L"Mewtwo", L"Ness", L"Peach", L"Pikachu", L"Ice Climbers", L"Jigglypuff", L"Samus", L"Yoshi", L"Zelda", L"Sheik",
    L"Falco", L"Young Link", L"Dr. Mario", L"Roy", L"Pichu", L"Ganondorf"};
  std::vector<int> mains = launcher::lobby::saved_mains();
  int order[26]; for (int i = 0; i < 26; ++i) order[i] = i;
  std::sort(order, order + 26, [](int a, int b) { return wcscmp(names[a], names[b]) < 0; });
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"Your mains (up to 3; the one you click is your picture)");
  for (int k = 0; k < 26; ++k) {
    const int i = order[k]; const auto at_ = std::find(mains.begin(), mains.end(), i);
    std::wstring label = names[i];
    if (at_ == mains.begin() && !mains.empty()) label += L"   (picture)";
    AppendMenuW(m, MF_STRING | (k && k % 13 == 0 ? MF_MENUBARBREAK : 0) | (at_ != mains.end() ? MF_CHECKED : 0), 100 + i, label.c_str());
  }
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING | (mains.empty() ? MF_GRAYED : 0), 99, L"Clear my mains");
  const int pick = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, at.x, at.y, 0, owner, nullptr);
  DestroyMenu(m);
  if (!pick) return false;
  if (pick == 99) mains.clear();
  else {
    const int ch = pick - 100; const auto it = std::find(mains.begin(), mains.end(), ch);
    if (it != mains.end()) mains.erase(it);                       // one of yours: off
    else { mains.insert(mains.begin(), ch); if (mains.size() > 3) mains.resize(3); }   // new: your picture
  }
  launcher::lobby::save_mains(mains); g_mains_dirty = true;
  if (g_window) InvalidateRect(g_window, nullptr, FALSE);
  if (g_cb.changed) g_cb.changed();
  return true;
}
bool elo() { return g_elo; }
void settings_changed() { if (g_cb.changed) g_cb.changed(); }
CodeSignIn code_sign_in(std::wstring* text) {
  device::load(); std::lock_guard<std::mutex> lock(device::g_mx);
  if (text) *text = device::g_state == CodeSignIn::kWaiting ? device::g_user_code : device::g_reason;
  return device::g_state;
}
void start_code_sign_in() {
  const unsigned attempt = ++device::g_attempt;
  { std::lock_guard<std::mutex> lock(device::g_mx); device::g_state = CodeSignIn::kStarting; device::g_reason.clear(); }
  std::thread([attempt] { device::run(attempt); }).detach();
}
void open_code_link() { device::open_link(); }
void friend_action(const std::string& username, bool accept, std::function<void(const std::wstring&)> done) {
  std::thread([username, accept, done] {
    const std::string token = device::token();
    if (token.empty()) { start_code_sign_in(); done(L"Sign in with a YouGame code first: your browser is opening it."); return; }
    const json body{{"action", accept ? "accept" : "request"}, {"username", username}};
    std::string out; const int st = https_req(L"POST", device::host(), L"/api/friends", body.dump(), L"Authorization: Bearer " + wide(token) + L"\r\n", &out, nullptr);
    trace("friends: status %d", st);
    const json a = json::parse(out, nullptr, false); const std::wstring err = a.is_object() ? wide(a.value("error", std::string())) : std::wstring();
    if (st >= 200 && st < 300) { g_reopened.store(true); done(accept ? L"You're friends now." : L"Friend request sent."); }
    else if (st == 401) { device::expire(); done(L"YouGame wants you to sign in again."); }
    else done(err.empty() ? std::wstring(L"YouGame didn't take that. Try again.") : err);
  }).detach();
}
void sign_out_code() {
  ++device::g_attempt;
  std::string refresh; { std::lock_guard<std::mutex> lock(device::g_mx); refresh = device::g_refresh; device::g_refresh.clear(); device::g_access.clear(); device::g_state = CodeSignIn::kOff; }
  device::store(std::string());
  if (!refresh.empty()) std::thread([refresh] { const std::string id = device::client_id(); if (!id.empty()) device::post(L"/oauth/revoke", "token=" + device::form_part(refresh) + "&client_id=" + device::form_part(id), nullptr); }).detach();
}
bool me_pending() { return !g_preview && !data().me_known && launcher::yougame::app::running(launcher::yougame::staging_env()); }

}  // namespace launcher::yglobby
