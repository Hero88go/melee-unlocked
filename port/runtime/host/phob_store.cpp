// The Phob settings of each adapter port, kept in PhobGCC\port<N>.ini beside port-settings.ini, and
// named setups in PhobGCC\Setups\<name>.ini that a player can copy to a friend.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "host.h"
#include "controller_profiles.h"
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
}  // namespace

void phob_store_load(const std::string& settings_path) {
  std::lock_guard<std::mutex> lock(g_store_mutex);
  g_folder = std::filesystem::u8path(settings_path).parent_path() / "PhobGCC";
  for (int port = 0; port < 4; ++port) {
    phob::Settings s;
    std::string text;
    if (read_text(port_file(port), text)) phob::parse(text, s);
    gcadapter_phob_set(port, s);
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

// Loads a setup on top of `s`: a setup saved without calibration keeps this controller's own.
bool phob_setup_load(const std::string& name, phob::Settings& s) {
  const std::string clean = profile_clean_name(name);
  if (clean.empty()) return false;
  std::string text;
  std::lock_guard<std::mutex> lock(g_store_mutex);
  if (!read_text(setup_folder() / (clean + ".ini"), text)) return false;
  phob::parse(text, s);
  return true;
}

}  // namespace host
