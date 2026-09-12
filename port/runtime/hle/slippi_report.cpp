// SPDX-License-Identifier: GPL-2.0-or-later
#include "slippi_report.h"
#include "slippi_net.h"
#include "host.h"
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace slippi::report {
namespace {
using json = nlohmann::json;
const wchar_t* USER_AGENT = L"SlippiDolphin (b: ishiiruka) (v: 3.6.4) (o: windows)";
const char* ENDPOINT = "https://internal.slippi.gg/graphql";
constexpr int MAX_ATTEMPTS = 5;

struct StatusJob { std::string uid, play_key, match_id, status; };
struct Job { bool is_status = false; GameReport game; StatusJob status; int attempts = 0; };

std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<Job> g_queue;
std::thread g_thread;
bool g_quit = false;
std::string g_iso_path, g_cache_dir;
std::atomic<bool> g_hash_done{false};
std::string g_iso_hash;

// ---- HTTP (WinHTTP)
std::wstring widen(const std::string& s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n ? n - 1 : 0, 0); if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n); return w; }

bool http(const char* method, const std::string& url, const std::wstring& headers, const std::string& body, int* status, std::string* response) {
  std::wstring wurl = widen(url);
  URL_COMPONENTS uc{}; uc.dwStructSize = sizeof uc;
  wchar_t host[256]{}, path[2048]{};
  uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return false;
  HINTERNET session = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return false;
  WinHttpSetTimeouts(session, 5000, 5000, 15000, 15000);
  bool ok = false;
  HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
  if (conn) {
    std::wstring wmethod = widen(method);
    HINTERNET req = WinHttpOpenRequest(conn, wmethod.c_str(), path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (req) {
      if (WinHttpSendRequest(req, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), headers.empty() ? 0 : (DWORD)-1,
                             body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
          WinHttpReceiveResponse(req, nullptr)) {
        DWORD code = 0, size = sizeof code;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
        if (status) *status = (int)code;
        std::string out;
        for (;;) {
          DWORD avail = 0;
          if (!WinHttpQueryDataAvailable(req, &avail) || !avail) break;
          std::string chunk(avail, 0); DWORD got = 0;
          if (!WinHttpReadData(req, chunk.data(), avail, &got)) break;
          out.append(chunk.data(), got);
        }
        if (response) *response = out;
        ok = true;
      }
      WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(conn);
  }
  WinHttpCloseHandle(session);
  return ok;
}

// GraphQL POST; returns the `data` object or null (and logs) on failure.
json graphql(const std::string& query, const json& variables) {
  json body = {{"query", query}, {"variables", variables}};
  int status = 0; std::string response;
  if (!http("POST", ENDPOINT, L"Content-Type: application/json\r\n", body.dump(), &status, &response)) { host::log("slippi report: request failed (network)"); return nullptr; }
  json r = json::parse(response, nullptr, false);
  if (r.is_discarded()) { host::log("slippi report: bad response (HTTP %d): %s", status, response.substr(0, 200).c_str()); return nullptr; }
  if (r.count("errors") && r["errors"].is_array() && !r["errors"].empty()) { host::log("slippi report: server error: %s", r["errors"].dump().substr(0, 300).c_str()); return nullptr; }
  if (!r.count("data")) return nullptr;
  return r["data"];
}

// ---- gzip container with stored (uncompressed) deflate blocks: valid gzip, no zlib needed.
uint32_t crc32(const uint8_t* p, size_t n) {
  static uint32_t table[256]; static bool init = false;
  if (!init) { for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } init = true; }
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}
std::string gzip_stored(const std::string& in) {
  std::string out;
  const uint8_t header[10] = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 0x0B};
  out.append((const char*)header, 10);
  size_t pos = 0;
  do {
    size_t n = std::min<size_t>(65535, in.size() - pos);
    bool last = pos + n >= in.size();
    out.push_back(last ? 1 : 0);
    uint16_t len = (uint16_t)n, nlen = (uint16_t)~len;
    out.push_back((char)(len & 0xFF)); out.push_back((char)(len >> 8)); out.push_back((char)(nlen & 0xFF)); out.push_back((char)(nlen >> 8));
    out.append(in.data() + pos, n);
    pos += n;
  } while (pos < in.size());
  uint32_t crc = crc32((const uint8_t*)in.data(), in.size()), isize = (uint32_t)in.size();
  for (int i = 0; i < 4; ++i) out.push_back((char)((crc >> (8 * i)) & 0xFF));
  for (int i = 0; i < 4; ++i) out.push_back((char)((isize >> (8 * i)) & 0xFF));
  return out;
}

// ---- ISO MD5 (BCrypt), cached by path, size and modification time.
std::string md5_file(const std::string& path) {
  BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE h = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) < 0) return "";
  std::string result;
  if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) >= 0) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f) {
      std::vector<uint8_t> buf(4 << 20);
      size_t n;
      while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) BCryptHashData(h, buf.data(), (ULONG)n, 0);
      std::fclose(f);
      uint8_t digest[16];
      if (BCryptFinishHash(h, digest, 16, 0) >= 0) { char hex[33]; for (int i = 0; i < 16; ++i) std::snprintf(hex + 2 * i, 3, "%02x", digest[i]); result = hex; }
    }
    BCryptDestroyHash(h);
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return result;
}
void hash_iso() {
  WIN32_FILE_ATTRIBUTE_DATA fa{};
  std::string key;
  if (GetFileAttributesExA(g_iso_path.c_str(), GetFileExInfoStandard, &fa)) {
    char buf[128]; std::snprintf(buf, sizeof buf, "|%llu|%llu", ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow, ((unsigned long long)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime);
    key = g_iso_path + buf;
  }
  std::string cache = g_cache_dir + "/iso_md5_cache.txt";
  { std::ifstream in(cache); std::string line; while (std::getline(in, line)) { size_t eq = line.rfind('='); if (eq != std::string::npos && line.substr(0, eq) == key) { g_iso_hash = line.substr(eq + 1); g_hash_done = true; return; } } }
  auto t0 = std::chrono::steady_clock::now();
  g_iso_hash = md5_file(g_iso_path);
  host::log("slippi report: ISO md5 %s (%.1f s)", g_iso_hash.c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
  if (!g_iso_hash.empty() && !key.empty()) { std::ofstream out(cache, std::ios::app); out << key << "=" << g_iso_hash << "\n"; }
  g_hash_done = true;
}

bool send_status(const StatusJob& s) {
  json vars = {{"report", {{"matchId", s.match_id}, {"fbUid", s.uid}, {"playKey", s.play_key}, {"status", s.status}}}};
  json data = graphql("mutation ($report: OnlineMatchStatusReportInput!) { reportOnlineMatchStatus (report: $report) }", vars);
  bool ok = !data.is_null() && data.value("reportOnlineMatchStatus", false);
  host::log("slippi report: match status '%s' for %s: %s", s.status.c_str(), s.match_id.c_str(), ok ? "accepted" : "failed");
  return ok;
}

void upload_replay(const std::string& path, const std::string& url) {
  std::ifstream in(path, std::ios::binary);
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (contents.empty()) { host::log("slippi report: no replay to upload (%s)", path.c_str()); return; }
  std::string gz = gzip_stored(contents);
  int status = 0;
  bool ok = http("PUT", url, L"Content-Type: application/octet-stream\r\nContent-Encoding: gzip\r\nX-Goog-Content-Length-Range: 0,10000000\r\n", gz, &status, nullptr);
  host::log("slippi report: replay upload %s (HTTP %d, %zu bytes)", ok && status / 100 == 2 ? "done" : "failed", status, gz.size());
}

// Returns true when the report is finished (accepted or given up).
bool send_game(Job& job) {
  const GameReport& g = job.game;
  ++job.attempts;
  json players = json::array();
  for (auto& p : g.players)
    players.push_back({{"fbUid", p.uid}, {"slotType", p.slot_type}, {"damageDone", p.damage_done}, {"stocksRemaining", p.stocks_remaining},
                       {"characterId", p.character_id}, {"colorId", p.color_id}, {"startingStocks", p.starting_stocks}, {"startingPercent", p.starting_percent}});
  json payload = {{"fbUid", g.uid}, {"mode", g.online_mode}, {"players", players}, {"isoHash", g_iso_hash}, {"matchId", g.match_id}, {"playKey", g.play_key},
                  {"gameDurationFrames", g.duration_frames}, {"gameIndex", g.game_index}, {"tiebreakIndex", g.tiebreak_index}, {"winnerIdx", g.winner_index},
                  {"gameEndMethod", g.game_end_method}, {"lrasInitiator", g.lras_initiator}, {"stageId", g.stage_id}};
  json data = graphql("mutation ($report: OnlineGameReportInput!) { reportOnlineGame (report: $report) { success uploadUrl } }", {{"report", payload}});
  bool success = !data.is_null() && data.count("reportOnlineGame") && data["reportOnlineGame"].value("success", false);
  if (success) {
    host::log("slippi report: game %u of %s reported", g.game_index, g.match_id.c_str());
    auto& r = data["reportOnlineGame"];
    if (r.count("uploadUrl") && r["uploadUrl"].is_string() && !g.replay_path.empty()) upload_replay(g.replay_path, r["uploadUrl"].get<std::string>());
    return true;
  }
  if (job.attempts >= MAX_ATTEMPTS) { host::log("slippi report: giving up on game report for %s after %d attempts", g.match_id.c_str(), job.attempts); return true; }
  std::this_thread::sleep_for(std::chrono::milliseconds(100 * job.attempts));
  return false;
}

void worker() {
  hash_iso();
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lk(g_mutex);
      g_cv.wait(lk, [] { return g_quit || !g_queue.empty(); });
      if (g_queue.empty()) return;
      job = g_queue.front();
      if (g_quit) { g_queue.pop_front(); job.attempts = MAX_ATTEMPTS - 1; }   // one last attempt each on shutdown
    }
    bool done = job.is_status ? (send_status(job.status), true) : send_game(job);
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_queue.empty() && !g_quit) { if (done) g_queue.pop_front(); else g_queue.front().attempts = job.attempts; }
  }
}
}  // namespace

void init(const std::string& iso_path, const std::string& cache_dir) {
  g_iso_path = iso_path; g_cache_dir = cache_dir;
  g_thread = std::thread(worker);
}

void shutdown() {
  { std::lock_guard<std::mutex> lk(g_mutex); g_quit = true; }
  g_cv.notify_all();
  if (g_thread.joinable()) g_thread.join();
}

void log_game(const GameReport& report) {
  { std::lock_guard<std::mutex> lk(g_mutex); Job j; j.game = report; g_queue.push_back(std::move(j)); }
  g_cv.notify_all();
}

void match_status(const std::string& uid, const std::string& play_key, const std::string& match_id, const std::string& status, bool background) {
  StatusJob s{uid, play_key, match_id, status};
  if (!background) { send_status(s); return; }
  { std::lock_guard<std::mutex> lk(g_mutex); Job j; j.is_status = true; j.status = s; g_queue.push_back(std::move(j)); }
  g_cv.notify_all();
}
}  // namespace slippi::report
