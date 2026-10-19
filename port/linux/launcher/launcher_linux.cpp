// Melee Unlocked Launcher for Linux: the same pages as the Windows launcher (Play, Replays with Watch
// live, Mods, Online lobby, Help) drawn with Dear ImGui on SDL2 and OpenGL, over the same logic files
// (replay data and stats, the Slippi account lookup, the updater, the P2P lobby, the crash report zip).
// It starts the Source Port with the arguments the Windows launcher passes.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <SDL.h>
#include <GL/glew.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <nlohmann/json.hpp>
#include "launcher_replay_data.h"
#include "launcher_crash_text.h"
#include "launcher_lobby_p2p.h"
#include "slippi_account.h"
#include "updater.h"
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern char** environ;
namespace fs = std::filesystem;

#ifndef MELEE_PORT_VERSION
#define MELEE_PORT_VERSION "dev"
#endif

namespace host {
// The updater and the shared helpers log through these; the launcher keeps its own log file.
static FILE* g_log = nullptr;
static std::atomic<bool> g_exit_requested{false};
void log(const char* fmt, ...) {
  char buf[2048];
  va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  if (g_log) { std::fprintf(g_log, "%s\n", buf); std::fflush(g_log); }
}
void request_exit(int) { g_exit_requested.store(true); }
}  // namespace host

namespace {
// ---------------------------------------------------------------------------- state
std::string g_dir;                 // the folder the launcher and the game are in
std::string g_iso;
bool g_save_replays = true;
bool g_replay_as_played = false;
int g_replay_sort = 0;
bool g_replay_hide_short = false;
std::string g_status;
std::mutex g_status_mutex;
void status(const std::string& s) { std::lock_guard<std::mutex> lock(g_status_mutex); g_status = s; host::log("%s", s.c_str()); }
std::string status_text() { std::lock_guard<std::mutex> lock(g_status_mutex); return g_status; }

std::string exe_dir() {
  std::error_code ec;
  const fs::path self = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::current_path().string() : self.parent_path().string();
}
std::string game_exe() { return g_dir + "/melee_" "source"; }
std::string ini_path() { return g_dir + "/launcher.ini"; }
bool file_exists(const std::string& p) { std::error_code ec; return fs::is_regular_file(p, ec); }

void load_ini() {
  std::ifstream f(ini_path());
  for (std::string line; std::getline(f, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("iso=", 0) == 0) g_iso = line.substr(4);
    else if (line == "savereplays=0") g_save_replays = false;
    else if (line == "replayasplayed=1") g_replay_as_played = true;
    else if (line == "replayhideshort=1") g_replay_hide_short = true;
    else if (line.rfind("replaysort=", 0) == 0) g_replay_sort = std::clamp(std::atoi(line.c_str() + 11), 0, 3);
  }
  if (!g_iso.empty() && !file_exists(g_iso)) g_iso.clear();
  if (g_iso.empty() && file_exists(g_dir + "/melee.iso")) g_iso = g_dir + "/melee.iso";
}
void save_ini() {
  std::ofstream f(ini_path(), std::ios::trunc);
  f << "iso=" << g_iso << "\n";
  if (!g_save_replays) f << "savereplays=0\n";
  if (g_replay_as_played) f << "replayasplayed=1\n";
  if (g_replay_hide_short) f << "replayhideshort=1\n";
  if (g_replay_sort) f << "replaysort=" << g_replay_sort << "\n";
}

// The same check as the Windows launcher's: Melee NTSC 1.02 and a full image (not an NKit one).
bool verify_iso(const std::string& path, std::string* why) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { *why = "cannot open the file"; return false; }
  char hdr[8]{}; f.read(hdr, 8);
  if (std::string(hdr, 6) != "GALE01") { *why = "not a Melee NTSC disc (game id " + std::string(hdr, 6) + ")"; return false; }
  if (hdr[7] != 2) { *why = "Melee NTSC 1.0" + std::to_string((int)hdr[7]) + "; version 1.02 is required"; return false; }
  char tag[4]{}; f.seekg(0x200); f.read(tag, 4);
  if (!std::memcmp(tag, "NKIT", 4)) { *why = "an NKit image: restore it with NKit's Recover to ISO first"; return false; }
  f.seekg(0, std::ios::end);
  const auto size = (unsigned long long)f.tellg();
  if (size < 1000000000ull) { *why = "too small to be a full disc image (" + std::to_string(size / 1000000) + " MB)"; return false; }
  return true;
}

std::string account_line(bool* ok) {
  *ok = false;
  if (auto profile = slippi::account::resolve(fs::path(g_dir) / "User" / "Slippi")) {
    auto name = slippi::account::text(profile.data, "displayName");
    if (name.empty()) name = "?";
    *ok = true;
    return "Slippi account: " + name + " (" + slippi::account::text(profile.data, "connectCode") + ")";
  }
  switch (slippi::account::why_missing(fs::path(g_dir) / "User" / "Slippi")) {
    case slippi::account::Missing::SignIn: return "Slippi Launcher found, but no sign-in: open it, log in, and start Slippi Dolphin once.";
    case slippi::account::Missing::ConnectCode: return "Your Slippi sign-in has no connect code yet: finish the account in the Slippi Launcher.";
    default: return "Slippi online needs an account: install the Slippi Launcher and log in once.";
  }
}

// ---------------------------------------------------------------------------- dialogs
bool have_program(const char* name) {
  const char* path = std::getenv("PATH");
  if (!path) return false;
  std::stringstream ss(path); std::string dir;
  while (std::getline(ss, dir, ':')) if (!dir.empty() && access((dir + "/" + name).c_str(), X_OK) == 0) return true;
  return false;
}
std::string run_capture(const std::vector<std::string>& argv) {
  int pipefd[2];
  if (pipe(pipefd) != 0) return {};
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pipefd[1], 1);
  posix_spawn_file_actions_addclose(&actions, pipefd[0]);
  std::vector<char*> args;
  for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(pipefd[1]);
  std::string out;
  if (rc == 0) {
    char buf[4096]; ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
    int st = 0; waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) out.clear();
  }
  close(pipefd[0]);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}
std::string choose_file(const std::string& title, const std::string& label, const std::string& patterns) {
  if (have_program("zenity"))
    return run_capture({"zenity", "--file-selection", "--title=" + title, "--file-filter=" + label + " | " + patterns, "--file-filter=All files | *"});
  if (have_program("kdialog")) return run_capture({"kdialog", "--title", title, "--getopenfilename", ".", patterns + "|" + label});
  status("Install zenity or kdialog to choose files, or type the path.");
  return {};
}
void open_path(const std::string& path) {
  std::vector<char*> args{const_cast<char*>("xdg-open"), const_cast<char*>(path.c_str()), nullptr};
  pid_t pid; if (posix_spawnp(&pid, "xdg-open", nullptr, nullptr, args.data(), environ) == 0) std::thread([pid] { int s; waitpid(pid, &s, 0); }).detach();
}

// ---------------------------------------------------------------------------- the game process
std::atomic<pid_t> g_game_pid{0};
std::atomic<bool> g_game_done{false};
std::atomic<int> g_game_code{0};
std::chrono::system_clock::time_point g_launch_time;
bool g_replay_active = false;

bool playing() { return g_game_pid.load() != 0; }

bool start_process(const std::vector<std::string>& argv) {
  std::vector<char*> args;
  for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addchdir_np(&actions, g_dir.c_str());
  pid_t pid = 0;
  const int rc = posix_spawn(&pid, args[0], &actions, nullptr, args.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) { status(std::string("The game could not be started: ") + std::strerror(rc)); return false; }
  g_launch_time = std::chrono::system_clock::now();
  g_game_pid.store(pid);
  g_game_done.store(false);
  std::thread([pid] {
    int st = 0; waitpid(pid, &st, 0);
    g_game_code.store(WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st));
    g_game_pid.store(0);
    g_game_done.store(true);
  }).detach();
  return true;
}

std::string replay_dir() { return g_dir + "/Replays"; }
// The arguments every start passes (game_args on Windows).
std::vector<std::string> base_args() {
  std::vector<std::string> a{game_exe(), "--iso", g_iso, "--threaded-renderer", "--settings-path", g_dir + "/port-settings.ini"};
  if (file_exists(g_dir + "/Sys/codehandler.bin")) {
    a.insert(a.end(), {"--sys-dir", g_dir + "/Sys", "--user-dir", g_dir + "/User/Slippi", "--card-dir", g_dir + "/User/GC/CardA"});
    a.insert(a.end(), {"--replay-dir", g_save_replays ? replay_dir() : g_dir + "/User/UnsavedReplays"});
  }
  a.insert(a.end(), {"--lobby-status-file", g_dir + "/lobby-game-status.json"});
  return a;
}
bool check_ready() {
  if (playing()) { status("The game is already running."); return false; }
  std::string why;
  if (g_iso.empty() || !verify_iso(g_iso, &why)) { status(g_iso.empty() ? "Choose your Melee NTSC 1.02 disc image first." : "That disc image is " + why + "."); return false; }
  if (!file_exists(game_exe())) { status("The game is missing from this folder: extract the whole release archive again."); return false; }
  return true;
}
void play(const std::vector<std::string>& extra = {}) {
  if (!check_ready()) return;
  auto a = base_args();
  a.insert(a.end(), extra.begin(), extra.end());
  g_replay_active = false;
  if (start_process(a)) status("Melee Unlocked is running.");
}
void open_settings(const std::string& tab = std::string()) {
  if (!file_exists(game_exe())) { status("The game is missing from this folder."); return; }
  std::vector<std::string> a{game_exe(), "--settings-window", "--settings-path", g_dir + "/port-settings.ini"};
  if (!tab.empty()) a.insert(a.end(), {"--settings-tab", tab});
  std::vector<char*> args;
  for (const auto& s : a) args.push_back(const_cast<char*>(s.c_str()));
  args.push_back(nullptr);
  pid_t pid = 0;
  if (posix_spawn(&pid, args[0], nullptr, nullptr, args.data(), environ) == 0) std::thread([pid] { int s; waitpid(pid, &s, 0); }).detach();
}

// ---------------------------------------------------------------------------- replays
std::vector<fs::path> g_replay_files;
std::map<std::string, launcher::replay::Info> g_replay_info;
std::mutex g_replay_mutex;
std::atomic<bool> g_replay_scanning{false};
int g_replay_selected = -1;
std::vector<fs::path> g_replay_queue, g_replay_playing;
size_t g_replay_play_at = 0;
char g_replay_filter[128] = "";
bool g_live_watch = false;
std::set<std::string> g_live_seen;

std::vector<fs::path> replay_roots() {
  std::vector<fs::path> roots{replay_dir()};
  // The Slippi Launcher's replay folder too, as on Windows.
  for (const auto& root : slippi::account::launcher_roots()) {
    std::ifstream input(root / "Settings", std::ios::binary);
    if (!input) continue;
    const auto data = nlohmann::json::parse(input, nullptr, false);
    if (!data.is_object()) continue;
    const auto inner = data.find("settings");
    const nlohmann::json& s = inner != data.end() && inner->is_object() ? *inner : data;
    if (const auto v = s.find("rootSlpPath"); v != s.end() && v->is_string() && !v->get<std::string>().empty()) roots.push_back(fs::u8path(v->get<std::string>()));
  }
  if (const char* home = std::getenv("HOME")) roots.push_back(fs::path(home) / "Slippi");
  return roots;
}
void refresh_replays() {
  if (g_replay_scanning.exchange(true)) return;
  std::thread([] {
    std::vector<fs::path> files;
    std::set<std::string> seen;
    for (const auto& root : replay_roots()) {
      std::error_code ec;
      if (!fs::is_directory(root, ec)) continue;
      for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (it.depth() > 3) { it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(ec) || it->path().extension() != ".slp") continue;
        const auto canon = fs::weakly_canonical(it->path(), ec).string();
        if (seen.insert(canon).second) files.push_back(it->path());
      }
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
      std::error_code ec; return fs::last_write_time(a, ec) > fs::last_write_time(b, ec);
    });
    if (files.size() > 2000) files.resize(2000);
    std::map<std::string, launcher::replay::Info> info;
    for (const auto& f : files) info[f.string()] = launcher::replay::inspect(f);
    std::lock_guard<std::mutex> lock(g_replay_mutex);
    g_replay_files = std::move(files);
    g_replay_info = std::move(info);
    g_replay_scanning.store(false);
  }).detach();
}
fs::path live_spectate_folder() {
  for (const auto& root : slippi::account::launcher_roots()) {
    std::ifstream input(root / "Settings", std::ios::binary);
    if (!input) continue;
    const auto data = nlohmann::json::parse(input, nullptr, false);
    if (!data.is_object()) continue;
    const auto inner = data.find("settings");
    const nlohmann::json& s = inner != data.end() && inner->is_object() ? *inner : data;
    if (const auto v = s.find("spectateSlpPath"); v != s.end() && v->is_string() && !v->get<std::string>().empty()) return fs::u8path(v->get<std::string>());
    if (const auto v = s.find("rootSlpPath"); v != s.end() && v->is_string() && !v->get<std::string>().empty()) return fs::u8path(v->get<std::string>()) / "Spectate";
  }
  const char* home = std::getenv("HOME");
  return home ? fs::path(home) / "Slippi" / "Spectate" : fs::path();
}
enum class ReplayStart { Started, Skipped, Failed };
ReplayStart replay_start(const fs::path& file) {
  if (playing()) { status("Close the running game before opening a replay."); return ReplayStart::Failed; }
  std::string why;
  if (g_iso.empty() || !verify_iso(g_iso, &why)) { status("Choose your Melee disc image on the Play page first."); return ReplayStart::Failed; }
  std::error_code ec;
  if (!fs::is_regular_file(file, ec)) { status(file.filename().string() + " is no longer there."); return ReplayStart::Skipped; }
  const auto info = launcher::replay::inspect(file);
  if (info.valid && info.stage_id > 85) { status("This replay was recorded with a mod (stage " + std::to_string(info.stage_id) + " is not in the standard game)."); return ReplayStart::Skipped; }
  auto a = base_args();
  std::string sys = g_dir + "/SysPlayback";
  if (!file_exists(sys + "/codehandler.bin")) sys = g_dir + "/Sys";
  a.insert(a.end(), {"--sys-dir", sys, "--replay", file.string()});
  if (g_replay_as_played && file_exists(file.string() + ".trace")) a.push_back("--as-experienced");
  if (!start_process(a)) return ReplayStart::Failed;
  g_replay_active = true;
  status("Watching " + file.filename().string());
  return ReplayStart::Started;
}
bool replay_queue_next() {
  while (g_replay_play_at < g_replay_playing.size()) {
    const auto r = replay_start(g_replay_playing[g_replay_play_at++]);
    if (r == ReplayStart::Started) return true;
    if (r == ReplayStart::Failed) { g_replay_playing.clear(); return false; }
  }
  status("Queue finished.");
  g_replay_playing.clear();
  return false;
}
void live_tick() {
  if (!g_live_watch || playing()) return;
  static auto last = std::chrono::steady_clock::now();
  if (std::chrono::steady_clock::now() - last < std::chrono::seconds(1)) return;
  last = std::chrono::steady_clock::now();
  const auto folder = live_spectate_folder();
  std::error_code ec;
  if (folder.empty() || !fs::is_directory(folder, ec)) return;
  fs::path newest; fs::file_time_type newest_time{};
  for (const auto& e : fs::directory_iterator(folder, ec)) {
    if (ec || !e.is_regular_file(ec) || e.path().extension() != ".slp" || g_live_seen.count(e.path().string())) continue;
    const auto t = e.last_write_time(ec);
    if (!ec && (newest.empty() || t > newest_time)) { newest = e.path(); newest_time = t; }
  }
  if (newest.empty()) return;
  g_live_seen.insert(newest.string());
  if (replay_start(newest) == ReplayStart::Started) status("Watching live: " + newest.filename().string());
}
void live_toggle() {
  if (!g_live_watch && playing()) { status("Close the running game before watching live."); return; }
  g_live_watch = !g_live_watch;
  g_live_seen.clear();
  if (g_live_watch) {
    const auto folder = live_spectate_folder();
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    if (!folder.empty() && fs::is_directory(folder, ec))
      for (const auto& e : fs::directory_iterator(folder, ec))
        if (!ec && e.path().extension() == ".slp" && now - e.last_write_time(ec) > std::chrono::seconds(10)) g_live_seen.insert(e.path().string());
    status("Waiting for a live game. Watch a broadcast in the Slippi Launcher's Spectate tab; it plays here. Folder: " + folder.string());
  } else status("Stopped watching live.");
}

// ---------------------------------------------------------------------------- crash reports
// The same relay and zip as the Windows launcher's "Send logs".
constexpr const char* kCrashRelayUrl = "https://melee-crash-relay.firescribe-share-worker.workers.dev/report";
std::atomic<bool> g_sending{false};
std::string g_crash_offer;   // non-empty: a crash report is waiting for the player's answer
bool crash_newer_than_launch() {
  std::error_code ec;
  const auto path = fs::path(g_dir) / "melee_port_crash.txt";
  if (!fs::exists(path, ec)) return false;
  const auto t = fs::last_write_time(path, ec);
  const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  return sys > g_launch_time;
}
void send_report(const std::string& note) {
  if (g_sending.exchange(true)) return;
  std::thread([note] {
    auto files = launcher::crash::collect(g_dir, g_dir);
    if (!note.empty()) files.emplace_back("note.txt", std::vector<uint8_t>(note.begin(), note.end()));
    const auto zip = launcher::crash::capped_zip(files);
    std::string body;
    // libcurl through the command-line tool keeps this file free of another library.
    const std::string tmp = g_dir + "/melee_crash_report.zip";
    launcher::crash::save_report(tmp, (const char*)zip.data(), zip.size());
    const std::string answer = run_capture({"curl", "-sS", "-o", "/dev/null", "-w", "%{http_code}", "-X", "POST",
        "-H", "Content-Type: application/zip", "-H", std::string("X-MU-Version: ") + MELEE_PORT_VERSION, "-H", "X-MU-Platform: linux",
        "--data-binary", "@" + tmp, kCrashRelayUrl});
    if (answer == "200") status("Report sent. Thank you.");
    else if (answer == "429") status("A report was sent from this PC a moment ago; try again in two minutes.");
    else status("The report could not be sent (" + (answer.empty() ? std::string("no connection") : "HTTP " + answer) + "). It is saved as melee_crash_report.zip in the game folder.");
    g_sending.store(false);
  }).detach();
}

// ---------------------------------------------------------------------------- mods
struct ModDisc { fs::path path; std::string kind; };
std::vector<ModDisc> g_mods;
void refresh_mods() {
  g_mods.clear();
  std::error_code ec;
  for (const auto& [sub, kind] : {std::pair<const char*, const char*>{"Mods/Discs", "disc"}, {"Mods/Saves", "te"}}) {
    const fs::path dir = fs::path(g_dir) / sub;
    fs::create_directories(dir, ec);
    for (const auto& e : fs::directory_iterator(dir, ec)) {
      if (ec || !e.is_regular_file(ec)) continue;
      std::string ext = e.path().extension().string();
      for (char& c : ext) c = (char)std::tolower((unsigned char)c);
      if ((std::string(kind) == "disc" && (ext == ".iso" || ext == ".gcm")) || (std::string(kind) == "te" && ext == ".gci"))
        g_mods.push_back({e.path(), kind});
    }
  }
}
void play_mod(const ModDisc& mod) {
  play({mod.kind == "te" ? "--mod-gci" : "--mod-iso", mod.path.string(), "--card-dir", g_dir + "/User/GC/Mods/" + mod.path.stem().string()});
}

// ---------------------------------------------------------------------------- the window
void style() {
  ImGui::StyleColorsDark();
  auto& s = ImGui::GetStyle();
  s.WindowRounding = 0; s.FrameRounding = 6; s.ChildRounding = 8; s.GrabRounding = 6;
  s.FramePadding = ImVec2(12, 8); s.ItemSpacing = ImVec2(10, 10);
  auto* c = s.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.063f, 0.14f, 1);
  c[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.08f, 0.19f, 0.8f);
  c[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.16f, 0.30f, 1);
  c[ImGuiCol_Button] = ImVec4(0.32f, 0.20f, 0.52f, 1);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.28f, 0.68f, 1);
  c[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.35f, 0.80f, 1);
  c[ImGuiCol_Header] = ImVec4(0.25f, 0.18f, 0.42f, 1);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.24f, 0.56f, 1);
  c[ImGuiCol_CheckMark] = ImVec4(0.85f, 0.57f, 0.98f, 1);
}

void page_play() {
  ImGui::TextColored(ImVec4(0.85f, 0.7f, 1, 1), "MELEE UNLOCKED %s", MELEE_PORT_VERSION);
  ImGui::Separator();
  ImGui::Text("Melee NTSC 1.02 disc image");
  static char iso[1024] = "";
  static bool init = false;
  if (!init) { std::snprintf(iso, sizeof iso, "%s", g_iso.c_str()); init = true; }
  ImGui::SetNextItemWidth(-130);
  if (ImGui::InputText("##iso", iso, sizeof iso)) { g_iso = iso; save_ini(); }
  ImGui::SameLine();
  if (ImGui::Button("Browse...", ImVec2(-1, 0))) {
    const auto picked = choose_file("Choose your Melee NTSC 1.02 disc image", "Disc images", "*.iso *.gcm *.ISO *.GCM");
    if (!picked.empty()) { g_iso = picked; std::snprintf(iso, sizeof iso, "%s", g_iso.c_str()); save_ini(); }
  }
  std::string why;
  if (!g_iso.empty()) {
    if (verify_iso(g_iso, &why)) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1), "Disc image OK");
    else ImGui::TextColored(ImVec4(1, 0.55f, 0.5f, 1), "This disc image is %s.", why.c_str());
  }
  bool account = false;
  const auto line = account_line(&account);
  ImGui::TextColored(account ? ImVec4(0.75f, 0.8f, 1, 1) : ImVec4(1, 0.8f, 0.5f, 1), "%s", line.c_str());
  ImGui::Spacing();
  ImGui::BeginDisabled(playing());
  if (ImGui::Button(playing() ? "RUNNING" : "PLAY", ImVec2(260, 64))) play();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginGroup();
  if (ImGui::Button("Settings", ImVec2(180, 28))) open_settings();
  if (ImGui::Button("Choose skins", ImVec2(180, 28))) open_settings("mods");
  ImGui::EndGroup();
  ImGui::Spacing();
  if (ImGui::Checkbox("Save my replays", &g_save_replays)) save_ini();
  ImGui::Separator();
  // Updates: the same release check the game's settings panel makes.
  const auto state = host::updater::state();
  ImGui::Text("Updates: %s", host::updater::message().c_str());
  if (state == host::updater::State::UpdateAvailable) {
    if (ImGui::Button(("Update to " + host::updater::latest_version()).c_str())) { host::updater::download_and_install(); }
  } else if (state != host::updater::State::Checking && state != host::updater::State::Downloading) {
    if (ImGui::Button("Check for updates")) host::updater::check(MELEE_PORT_VERSION);
  }
}

void page_replays() {
  ImGui::BeginDisabled(playing());
  if (ImGui::Button(g_live_watch ? "Stop watching live" : "Watch live")) live_toggle();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Refresh")) refresh_replays();
  ImGui::SameLine();
  if (ImGui::Button("Open replay...")) {
    const auto picked = choose_file("Open a replay", "Slippi replays", "*.slp");
    if (!picked.empty()) replay_start(picked);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(g_replay_queue.empty() || playing());
  if (ImGui::Button(("Watch queue (" + std::to_string(g_replay_queue.size()) + ")").c_str())) {
    g_replay_playing = g_replay_queue; g_replay_play_at = 0; replay_queue_next();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Checkbox("Show it as it was played", &g_replay_as_played)) save_ini();
  ImGui::SetNextItemWidth(320);
  ImGui::InputTextWithHint("##filter", "Search players, codes, stages", g_replay_filter, sizeof g_replay_filter);
  ImGui::SameLine();
  if (ImGui::Checkbox("Hide short games", &g_replay_hide_short)) save_ini();
  if (g_replay_scanning) { ImGui::SameLine(); ImGui::TextDisabled("reading replays..."); }
  std::lock_guard<std::mutex> lock(g_replay_mutex);
  std::string filter = g_replay_filter;
  for (char& c : filter) c = (char)std::tolower((unsigned char)c);
  ImGui::BeginChild("replays", ImVec2(0, 0), true);
  if (ImGui::BeginTable("list", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 150);
    ImGui::TableSetupColumn("Players");
    ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 170);
    ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 70);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < g_replay_files.size(); ++i) {
      const auto& file = g_replay_files[i];
      const auto it = g_replay_info.find(file.string());
      if (it == g_replay_info.end()) continue;
      const auto& info = it->second;
      const int seconds = info.last_frame > 0 ? (info.last_frame + 123) / 60 : 0;
      if (g_replay_hide_short && seconds < 30) continue;
      std::string players;
      for (size_t p = 0; p < info.players.size(); ++p) {
        if (p) players += "  vs  ";
        const auto& pl = info.players[p];
        players += std::string(launcher::replay::character_name(pl.character)) + (pl.code.empty() ? "" : " (" + pl.code + ")");
        if ((int)p == info.winner) players += " *";
      }
      if (!filter.empty()) {
        std::string hay = players + " " + info.stage + " " + file.filename().string();
        for (char& c : hay) c = (char)std::tolower((unsigned char)c);
        if (hay.find(filter) == std::string::npos) continue;
      }
      ImGui::TableNextRow();
      ImGui::PushID((int)i);
      ImGui::TableNextColumn();
      if (ImGui::Selectable(launcher::replay::display_date(info).c_str(), g_replay_selected == (int)i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
        g_replay_selected = (int)i;
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) replay_start(file);
      ImGui::TableNextColumn(); ImGui::TextUnformatted(players.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(info.stage.c_str());
      ImGui::TableNextColumn(); ImGui::Text("%d:%02d", seconds / 60, seconds % 60);
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("Watch")) replay_start(file);
      ImGui::SameLine();
      const bool queued = std::find(g_replay_queue.begin(), g_replay_queue.end(), file) != g_replay_queue.end();
      if (ImGui::SmallButton(queued ? "Unqueue" : "Queue")) {
        if (queued) g_replay_queue.erase(std::remove(g_replay_queue.begin(), g_replay_queue.end(), file), g_replay_queue.end());
        else g_replay_queue.push_back(file);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
}

void page_mods() {
  ImGui::TextWrapped("Mod discs (.iso) go in Mods/Discs and training mode saves (.gci) in Mods/Saves. Skins, stages and music are chosen in Settings.");
  if (ImGui::Button("Open the Mods folder")) open_path(g_dir + "/Mods");
  ImGui::SameLine();
  if (ImGui::Button("Add a mod...")) {
    const auto picked = choose_file("Add a mod disc or save", "Mods", "*.iso *.gcm *.gci *.ISO *.GCI");
    if (!picked.empty()) {
      const fs::path src(picked);
      std::string ext = src.extension().string();
      for (char& c : ext) c = (char)std::tolower((unsigned char)c);
      const fs::path dst = fs::path(g_dir) / "Mods" / (ext == ".gci" ? "Saves" : "Discs") / src.filename();
      std::error_code ec;
      fs::create_directories(dst.parent_path(), ec);
      fs::copy_file(src, dst, fs::copy_options::skip_existing, ec);
      status(ec ? "Could not copy " + src.filename().string() + " (" + ec.message() + ")" : "Added " + src.filename().string());
      refresh_mods();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Skins and stages")) open_settings("mods");
  ImGui::SameLine();
  if (ImGui::Button("Refresh")) refresh_mods();
  ImGui::BeginChild("mods", ImVec2(0, 0), true);
  for (size_t i = 0; i < g_mods.size(); ++i) {
    ImGui::PushID((int)i);
    ImGui::Text("%s  (%s)", g_mods[i].path.filename().string().c_str(), g_mods[i].kind == "te" ? "training save" : "mod disc");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
    ImGui::BeginDisabled(playing());
    if (ImGui::Button("Play")) play_mod(g_mods[i]);
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  if (g_mods.empty()) ImGui::TextDisabled("No mods yet.");
  ImGui::EndChild();
}

void page_online();   // the P2P lobby, below

void page_help() {
  ImGui::TextWrapped("Report a problem: your note and the game's log go to the developer. Nothing else on your PC is read.");
  static char note[2048] = "";
  ImGui::InputTextMultiline("##note", note, sizeof note, ImVec2(-1, 160));
  ImGui::BeginDisabled(g_sending.load());
  if (ImGui::Button("Send logs")) send_report(note);
  ImGui::EndDisabled();
  ImGui::Separator();
  if (ImGui::Button("Open the game folder")) open_path(g_dir);
  ImGui::SameLine();
  if (ImGui::Button("GitHub issues")) open_path("https://github.com/hero88go/melee-unlocked/issues");
  ImGui::Spacing();
  ImGui::TextWrapped("GameCube adapter: install the udev rule that comes with the game once "
                     "(sudo install -m 644 51-melee-unlocked-gc-adapter.rules /etc/udev/rules.d/ && sudo udevadm control --reload-rules), "
                     "then replug the adapter. Box controllers and PlayStation or Switch Pro pads use the same rule.");
}
}  // namespace

// ---------------------------------------------------------------------------- online (P2P lobby)
namespace {
std::unique_ptr<launcher::lobby::PeerLobby> g_lobby;
std::vector<std::string> g_lobby_chat;
nlohmann::json g_lobby_state;
void page_online_impl();
}
namespace { void page_online() { page_online_impl(); } }

#include "launcher_linux_lobby.inl"

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  g_dir = exe_dir();
  host::g_log = std::fopen((g_dir + "/launcher.log").c_str(), "w");
  load_ini();
  refresh_replays();
  refresh_mods();
  host::updater::check(MELEE_PORT_VERSION);
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_Window* window = SDL_CreateWindow("Melee Unlocked Launcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1080, 700,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window) { std::fprintf(stderr, "SDL window: %s\n", SDL_GetError()); return 1; }
  SDL_GLContext gl = SDL_GL_CreateContext(window);
  SDL_GL_SetSwapInterval(1);
  glewExperimental = GL_TRUE; glewInit(); glGetError();
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  for (const char* font : {"ui_sources/gd_melee/kit/SourceSans3-Regular.otf", "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"}) {
    const std::string path = font[0] == '/' ? font : g_dir + "/" + font;
    if (file_exists(path)) { io.Fonts->AddFontFromFileTTF(path.c_str(), 18.0f); break; }
  }
  style();
  ImGui_ImplSDL2_InitForOpenGL(window, gl);
  ImGui_ImplOpenGL3_Init("#version 330 core");
  int page = 0;
  const char* pages[] = {"Play", "Replays", "Mods", "Online", "Help"};
  bool running = true;
  while (running && !host::g_exit_requested.load()) {
    SDL_Event e;
    const bool busy = playing() || g_live_watch || g_replay_scanning || g_sending;
    if (SDL_WaitEventTimeout(&e, busy ? 50 : 250)) {
      do {
        ImGui_ImplSDL2_ProcessEvent(&e);
        if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE)) running = false;
      } while (SDL_PollEvent(&e));
    }
    // The game has ended: a queue's next replay, a crash report offer.
    if (g_game_done.exchange(false)) {
      const int code = g_game_code.load();
      if (crash_newer_than_launch()) g_crash_offer = "Melee Unlocked stopped with an error. Send the crash report to the developer?";
      if (g_replay_active) {
        g_replay_active = false;
        if (g_live_watch) {
          if (code == 4) { g_live_watch = false; status("Stopped watching live."); }
          else status("Live game over. Waiting for the next one.");
        } else if (!g_replay_playing.empty() && code == 0 && g_crash_offer.empty()) {
          replay_queue_next();
        } else {
          g_replay_playing.clear();
          status(code == 4 ? "Replay stopped." : code ? "Replay playback ended with an error. Check melee_port.log." : "Replay finished.");
        }
      } else status(code ? "The game ended with code " + std::to_string(code) + "." : "Ready.");
      SDL_RestoreWindow(window);
    }
    live_tick();
    if (g_lobby) g_lobby->tick();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    const ImVec2 size = io.DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##launcher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::BeginChild("rail", ImVec2(180, -32), true);
    ImGui::TextColored(ImVec4(0.85f, 0.6f, 1, 1), "Melee Unlocked");
    ImGui::Spacing();
    for (int i = 0; i < (int)(sizeof pages / sizeof pages[0]); ++i)
      if (ImGui::Selectable(pages[i], page == i, 0, ImVec2(0, 34))) page = i;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("page", ImVec2(0, -32), false);
    switch (page) {
      case 0: page_play(); break;
      case 1: page_replays(); break;
      case 2: page_mods(); break;
      case 3: page_online(); break;
      default: page_help(); break;
    }
    ImGui::EndChild();
    ImGui::TextDisabled("%s", status_text().c_str());
    if (!g_crash_offer.empty()) ImGui::OpenPopup("Crash report");
    if (ImGui::BeginPopupModal("Crash report", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted(g_crash_offer.c_str());
      if (ImGui::Button("Send")) { send_report(std::string()); g_crash_offer.clear(); ImGui::CloseCurrentPopup(); }
      ImGui::SameLine();
      if (ImGui::Button("Not now")) { g_crash_offer.clear(); ImGui::CloseCurrentPopup(); }
      ImGui::EndPopup();
    }
    ImGui::End();
    ImGui::Render();
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.06f, 0.12f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
  }
  if (g_lobby) g_lobby.reset();
  host::updater::shutdown();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
