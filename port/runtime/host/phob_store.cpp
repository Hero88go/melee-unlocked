// The Phob settings of each adapter port, kept in PhobGCC\port<N>.ini beside port-settings.ini, and
// named setups in PhobGCC\Setups\<name>.ini that a player can copy to a friend.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "host.h"
#include "controller_profiles.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace host {
namespace {
std::mutex g_store_mutex;
std::filesystem::path g_folder;

std::filesystem::path port_file(int port) { return g_folder / ("port" + std::to_string(port + 1) + ".ini"); }
std::filesystem::path setup_folder() { return g_folder / "Setups"; }
std::filesystem::path backup_folder() { return g_folder / "Backups"; }
constexpr size_t kBackupsKept = 20;

bool read_text(const std::filesystem::path& path, std::string& text) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream s;
  s << in.rdbuf();
  text = s.str();
  return true;
}
bool write_text(const std::filesystem::path& path, const std::string& text) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    if (!out) return false;
  }
  std::filesystem::rename(temporary, path, ec);
  return !ec;
}
// This port's backups, oldest first (the names carry the time, so they sort by it).
std::vector<std::filesystem::path> backups_of(int port) {
  std::vector<std::filesystem::path> out;
  const std::string prefix = "port" + std::to_string(port + 1) + "-";
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(backup_folder(), ec))
    if (e.path().extension() == ".ini" && e.path().filename().u8string().rfind(prefix, 0) == 0) out.push_back(e.path());
  std::sort(out.begin(), out.end());
  return out;
}

// Player reports 11-26: a controller setup lost to a reset or a setup load could not be brought back. The port's
// file as it is now goes to Backups, unless the newest backup already holds the same text. Caller holds the lock.
void backup_locked(int port) {
  if (g_folder.empty() || port < 0 || port > 3) return;
  std::string text;
  if (!read_text(port_file(port), text) || text.empty()) return;
  auto list = backups_of(port);
  std::string newest;
  if (!list.empty() && read_text(list.back(), newest) && newest == text) return;
  char stamp[32];
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
  std::filesystem::path path = backup_folder() / ("port" + std::to_string(port + 1) + "-" + stamp + ".ini");
  for (int n = 2; std::filesystem::exists(path) && n < 100; ++n)
    path = backup_folder() / ("port" + std::to_string(port + 1) + "-" + stamp + "-" + std::to_string(n) + ".ini");
  if (!write_text(path, text)) return;
  list = backups_of(port);
  std::error_code ec;
  while (list.size() > kBackupsKept) { std::filesystem::remove(list.front(), ec); list.erase(list.begin()); }
}
}  // namespace

void phob_backup(int port) {
  std::lock_guard<std::mutex> lock(g_store_mutex);
  backup_locked(port);
}

bool phob_backup_restore(int port, phob::Settings& s) {
  std::lock_guard<std::mutex> lock(g_store_mutex);
  if (g_folder.empty() || port < 0 || port > 3) return false;
  const std::string now = phob::serialize(s);
  auto list = backups_of(port);
  for (auto it = list.rbegin(); it != list.rend(); ++it) {
    std::string text;
    if (!read_text(*it, text)) continue;
    phob::Settings back = s;
    phob::parse(text, back);
    if (phob::serialize(back) == now) continue;
    const bool hardware = s.phob_hardware;
    s = back;
    s.phob_hardware = hardware;   // the controller is what it is
    return true;
  }
  return false;
}

void phob_store_load(const std::string& settings_path) {
  std::lock_guard<std::mutex> lock(g_store_mutex);
  g_folder = std::filesystem::u8path(settings_path).parent_path() / "PhobGCC";
  for (int port = 0; port < 4; ++port) {
    phob::Settings s;
    std::string text;
    if (read_text(port_file(port), text)) phob::parse(text, s);
    gcadapter_phob_set(port, s);
    backup_locked(port);   // the settings this session started with
  }
}

std::string phob_store_folder() {
  std::lock_guard<std::mutex> lock(g_store_mutex);
  return g_folder.u8string();
}

bool phob_store_save(int port, const phob::Settings& s) {
  gcadapter_phob_set(port, s);
  std::lock_guard<std::mutex> lock(g_store_mutex);
  if (g_folder.empty() || port < 0 || port > 3) return false;
  return write_text(port_file(port), phob::serialize(s));
}

bool phob_setup_delete(const std::string& name) {
  const std::string clean = profile_clean_name(name);
  if (clean.empty()) return false;
  std::lock_guard<std::mutex> lock(g_store_mutex);
  std::error_code ec;
  return std::filesystem::remove(setup_folder() / (clean + ".ini"), ec) && !ec;
}

std::vector<std::string> phob_setup_list() {
  std::lock_guard<std::mutex> lock(g_store_mutex);
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(setup_folder(), ec))
    if (e.path().extension() == ".ini") names.push_back(e.path().stem().u8string());
  return names;
}

bool phob_setup_save(const std::string& name, const phob::Settings& s, bool with_calibration) {
  const std::string clean = profile_clean_name(name);
  if (clean.empty()) return false;
  phob::Settings copy = s;
  if (!with_calibration) { copy.a_cal = phob::StickCal{}; copy.c_cal = phob::StickCal{}; }
  std::lock_guard<std::mutex> lock(g_store_mutex);
  return write_text(setup_folder() / (clean + ".ini"), phob::serialize(copy));
}

// Loads a setup on top of `s`: a setup saved without calibration keeps this controller's own. The
// controller stays what it is (a PhobGCC, or a stock controller with the settings on), so one setup
// works on either.
bool phob_setup_load(const std::string& name, phob::Settings& s) {
  const std::string clean = profile_clean_name(name);
  if (clean.empty()) return false;
  std::string text;
  std::lock_guard<std::mutex> lock(g_store_mutex);
  if (!read_text(setup_folder() / (clean + ".ini"), text)) return false;
  const bool hardware = s.phob_hardware;
  phob::parse(text, s);
  s.phob_hardware = hardware;
  s.enabled = !hardware;
  return true;
}

}  // namespace host
