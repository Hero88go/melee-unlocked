// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_mod_catalog.h"
#include <windows.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace launcher::mod_catalog {
namespace fs = std::filesystem;
namespace {
using Json = nlohmann::json;
std::string lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
bool hex(const std::string& s, size_t count) {
  return s.size() == count && s.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}
bool text_field(const Json& j, const char* name, std::string* value) {
  const auto it = j.find(name);
  if (it == j.end()) return true;
  if (!it->is_string()) return false;
  *value = it->get<std::string>();
  return value->size() <= 4096 && value->find_first_of("\r\n\0", 0, 3) == std::string::npos;
}
bool https(const std::string& s) {
  return s.rfind("https://", 0) == 0 && s.size() > 8 && s.find_first_of("\"\\ \t") == std::string::npos;
}
bool under(const fs::path& file, const fs::path& dir) {
  const std::wstring f = file.lexically_normal().wstring(), d = dir.lexically_normal().wstring();
  return f.size() > d.size() && _wcsnicmp(f.c_str(), d.c_str(), d.size()) == 0 &&
         (f[d.size()] == L'\\' || f[d.size()] == L'/');
}
bool atomic_text(const fs::path& path, const std::string& data, std::string* error) {
  fs::path temporary = path;
  temporary += ".mods.tmp";
  { std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
    if (!f || !f.write(data.data(), data.size())) { *error = "Could not write the mod settings."; return false; } }
  if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    *error = "Could not save the mod settings."; return false;
  }
  return true;
}
}  // namespace

bool valid_sha256(const std::string& s) { return hex(s, 64); }
bool valid_id(const std::string& s) {
  return !s.empty() && s.size() <= 64 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos;
}
bool download_digest(const std::string& github, const std::string& pinned, std::string* digest, std::string* error) {
  std::string official = github;
  if (official.rfind("sha256:", 0) == 0) official = official.substr(7);
  if ((!official.empty() && !valid_sha256(official)) || (!pinned.empty() && !valid_sha256(pinned))) {
    *error = "The release has an invalid verification hash. Nothing was downloaded."; return false;
  }
  if (official.empty() && pinned.empty()) {
    *error = "No official verification hash is available. Open the official page instead."; return false;
  }
  if (!official.empty() && !pinned.empty() && lower(official) != lower(pinned)) {
    *error = "The release changed. A verified catalog update is needed before installing it."; return false;
  }
  *digest = lower(pinned.empty() ? official : pinned);
  return true;
}

bool parse(const std::string& text, std::vector<CatalogMod>* mods, std::string* error) {
  const Json j = Json::parse(text, nullptr, false);
  if (!j.is_object() || !j.count("mods") || !j["mods"].is_array() || j["mods"].size() > 128) {
    *error = "The mod catalog could not be read."; return false;
  }
  std::vector<CatalogMod> parsed;
  std::set<std::string> ids;
  for (const auto& row : j["mods"]) {
    if (!row.is_object()) { *error = "Invalid catalog entry."; return false; }
    CatalogMod m;
    bool ok = true;
    for (const auto& field : std::vector<std::pair<const char*, std::string*>>{
        {"id", &m.id}, {"name", &m.name}, {"credits", &m.credits}, {"license", &m.license},
        {"page", &m.page}, {"repo", &m.repo}, {"tag", &m.tag}, {"asset_pattern", &m.asset_pattern},
        {"kind", &m.kind}, {"sha256", &m.sha256}, {"output_md5", &m.output_md5}, {"version", &m.version},
        {"url", &m.url}, {"tool_sha256", &m.tool_sha256}, {"patch_sha256", &m.patch_sha256}, {"policy", &m.policy},
        {"output_sha256", &m.output_sha256}, {"note", &m.note}})
      ok &= text_field(row, field.first, field.second);
    if (row.count("one_click")) {
      ok &= row["one_click"].is_boolean();
      if (row["one_click"].is_boolean()) m.one_click = row["one_click"].get<bool>();
    }
    ok &= valid_id(m.id) && ids.insert(m.id).second && !m.name.empty() && https(m.page);
    ok &= m.kind == "gci" || m.kind == "xdelta_zip" || m.kind == "xdelta_7z";
    ok &= m.url.empty() || https(m.url);
    ok &= m.sha256.empty() || valid_sha256(m.sha256);
    ok &= m.tool_sha256.empty() || valid_sha256(m.tool_sha256);
    ok &= m.patch_sha256.empty() || valid_sha256(m.patch_sha256);
    ok &= m.output_sha256.empty() || valid_sha256(m.output_sha256);
    ok &= m.output_md5.empty() || hex(m.output_md5, 32);
    const size_t slash = m.repo.find('/');
    ok &= slash != std::string::npos && slash > 0 && slash + 1 < m.repo.size() &&
          m.repo.find('/', slash + 1) == std::string::npos &&
          m.repo.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_./") == std::string::npos &&
          m.repo.find("..") == std::string::npos;
    if (!ok) { *error = "The catalog contains invalid verification metadata."; return false; }
    // A catalog must record a licensing/permission basis before enabling automatic downloads.
    if (m.policy != "licensed" && m.policy != "permission_granted") m.one_click = false;
    parsed.push_back(std::move(m));
  }
  *mods = std::move(parsed);
  return true;
}

std::vector<Installed> detected(const std::string& text) {
  const Json j = Json::parse(text, nullptr, false);
  std::vector<Installed> out;
  if (!j.is_object() || !j.count("items") || !j["items"].is_array()) return out;
  for (const auto& row : j["items"]) {
    if (!row.is_object()) continue;
    Installed m;
    bool ok = true;
    for (const auto& field : std::vector<std::pair<const char*, std::string*>>{
        {"id", &m.id}, {"key", &m.key}, {"kind", &m.kind}, {"name", &m.name}, {"version", &m.version},
        {"message", &m.message}, {"path", &m.path}, {"needs_engine", &m.needs_engine},
        {"status", &m.status}, {"hash", &m.hash}}) ok &= text_field(row, field.first, field.second);
    if (row.count("enabled") && !row["enabled"].is_boolean()) ok = false;
    if (!ok || (m.needs_engine != "source" && m.needs_engine != "recomp" && m.needs_engine != "either")) continue;
    m.enabled = row.value("enabled", false);
    if (m.id.empty()) m.id = m.key;
    if (!valid_id(m.id)) continue;
    out.push_back(std::move(m));
  }
  return out;
}
bool playable(const Installed& m) {
  if (m.status != "supported" && m.status != "untested") return false;
  return m.kind == "te" || m.kind == "tmce" || m.kind == "mex" || m.kind == "hack_pack" || m.kind == "asset_mod";
}
std::string play_engine(const Installed& m, const std::string& selected) {
  if (!playable(m)) return {};
  if (m.needs_engine == "source" || m.needs_engine == "recomp") return m.needs_engine;
  return selected == "recomp" ? "recomp" : "source";
}

std::string sha256_file(const fs::path& file) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
  std::string out;
  if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
    std::ifstream f(file, std::ios::binary);
    std::vector<char> buf(1 << 20);
    bool ok = (bool)f;
    while (ok && f) {
      f.read(buf.data(), buf.size());
      if (f.gcount() && BCryptHashData(hash, (PUCHAR)buf.data(), (ULONG)f.gcount(), 0) < 0) ok = false;
    }
    unsigned char bytes[32];
    if (ok && f.eof() && BCryptFinishHash(hash, bytes, sizeof bytes, 0) >= 0) {
      for (auto b : bytes) { out += "0123456789abcdef"[b >> 4]; out += "0123456789abcdef"[b & 15]; }
    }
    BCryptDestroyHash(hash);
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return out;
}
const CatalogMod* matching_archive(const std::vector<CatalogMod>& mods, const std::string& hash) {
  if (!valid_sha256(hash)) return nullptr;
  for (const auto& m : mods) if (!m.sha256.empty() && lower(m.sha256) == lower(hash)) return &m;
  return nullptr;
}
fs::path pinned_xdelta(const fs::path& folder, const std::string& hash) {
  if (!valid_sha256(hash)) return {};
  std::error_code ec;
  fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    if (it->is_symlink(ec)) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
    const std::string name = lower(it->path().filename().u8string());
    if (it->is_regular_file(ec) && name.rfind("xdelta", 0) == 0 && lower(it->path().extension().u8string()) == ".exe" &&
        sha256_file(it->path()) == lower(hash)) return it->path();
  }
  return {};
}
fs::path shipped_tool(const fs::path& dir, const char* name, const std::string& hash) {
  if (!valid_sha256(hash)) return {};
  const fs::path tool = dir / "tools" / name;
  std::error_code ec;
  if (!fs::is_regular_file(tool, ec) || sha256_file(tool) != lower(hash)) return {};
  return tool;
}
bool copy_pack(const fs::path& from, const fs::path& to, bool replace, std::string* error, const std::atomic<bool>* cancel) {
  auto cancelled = [&] { return cancel && cancel->load(); };
  if (cancelled()) { *error = "Cancelled. Nothing was installed."; return false; }
  std::error_code ec;
  if (fs::equivalent(from, to, ec) && !ec) return true;
  ec.clear();
  fs::create_directories(to.parent_path(), ec);
  if (ec) { *error = ec.message(); return false; }
  fs::path temporary = to; temporary += ".installing";
  {
    std::ifstream input(from, std::ios::binary);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    std::vector<char> block(1 << 20);
    bool ok = input && output;
    while (ok && !cancelled() && input) {
      input.read(block.data(), block.size());
      if (input.gcount()) output.write(block.data(), input.gcount());
      ok = !input.bad() && (bool)output;
    }
    output.close();
    ok &= !output.fail();
    if (!ok || cancelled()) {
      input.close(); fs::remove(temporary, ec);
      *error = cancelled() ? "Cancelled. Nothing was installed." : "Could not copy the mod file. Your installed pack was kept.";
      return false;
    }
  }
  DWORD flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
  if (!MoveFileExW(temporary.c_str(), to.c_str(), flags)) {
    *error = "Could not install the copied file; the destination may already exist.";
    fs::remove(temporary, ec); return false;
  }
  return true;
}
bool remove_pack(const fs::path& game_dir, const fs::path& pack, std::string* error) {
  std::error_code ec;
  const fs::path base = fs::weakly_canonical(game_dir / "Mods", ec);
  if (ec) { *error = ec.message(); return false; }
  const fs::path path = fs::weakly_canonical(pack.is_absolute() ? pack : game_dir / pack, ec);
  if (ec) { *error = "Could not resolve the installed file."; return false; }
  if (!under(path, base / "Discs") && !under(path, base / "Saves")) {
    // Removing a link never removes the player's original disc.
    std::ifstream links(base / "links.txt");
    if (!links) { *error = "This file is outside managed Mods folders. Your original file was kept."; return false; }
    std::string line, kept; bool removed = false;
    while (std::getline(links, line)) {
      std::string value = line;
      const auto first = value.find_first_not_of(" \t\r");
      const auto last = value.find_last_not_of(" \t\r");
      value = first == std::string::npos ? "" : value.substr(first, last - first + 1);
      if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
      fs::path candidate = fs::u8path(value);
      if (candidate.is_relative()) candidate = base / candidate;
      ec.clear();
      if (!value.empty() && value[0] != '#' && fs::weakly_canonical(candidate, ec) == path && !ec) removed = true;
      else kept += line + '\n';
    }
    if (links.bad() || !removed) { *error = "No managed link was found. Your original file was kept."; return false; }
    links.close();
    if (!atomic_text(base / "links.txt", kept, error)) return false;
  } else if (!fs::is_regular_file(path, ec) || !fs::remove(path, ec) || ec) {
    *error = "Could not remove the installed pack."; return false;
  }
  fs::remove(base / ".cache/scan.txt", ec);
  fs::remove(base / ".cache/detected.json", ec);
  return true;
}
bool enable_pack(const fs::path& settings, const std::string& key, bool on, std::string* error) {
  if (key != "te" && key != "tmce" && (!valid_id(key) || key.find_first_not_of("0123456789abcdef") != std::string::npos)) {
    *error = "Invalid mod choice."; return false;
  }
  std::ifstream f(settings, std::ios::binary);
  std::error_code ec;
  if (!f && (fs::exists(settings, ec) || ec)) {
    *error = "Could not read your existing settings."; return false;
  }
  std::string contents, line;
  const std::string special = key == "te" ? "mod_te_enabled" : key == "tmce" ? "mod_tmce_enabled" : "mod_enabled";
  while (std::getline(f, line)) {
    std::istringstream row(line); std::string field, value;
    row >> field >> value;
    if (field == special && (field != "mod_enabled" || value == key)) continue;
    contents += line + '\n';
  }
  if (f.bad()) { *error = "Could not read your existing settings."; return false; }
  f.close();
  contents += special + " " + (special == "mod_enabled" ? key + " " : "") + (on ? "1\n" : "0\n");
  return atomic_text(settings, contents, error);
}
bool link_pack(const fs::path& game_dir, const fs::path& pack, std::string* error) {
  std::error_code ec;
  const auto path = fs::weakly_canonical(pack, ec);
  if (ec || !fs::is_regular_file(path, ec)) { *error = "The selected file could not be read."; return false; }
  const std::string value = path.u8string();
  if (value.find_first_of("\r\n\"") != std::string::npos) { *error = "This path cannot be added to the Mods list."; return false; }
  const auto file = game_dir / "Mods/links.txt";
  fs::create_directories(file.parent_path(), ec);
  if (ec) { *error = "Could not create the Mods folder."; return false; }
  std::ifstream in(file, std::ios::binary);
  if (!in && fs::exists(file, ec)) { *error = "Could not read your existing Mods list."; return false; }
  std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) { *error = "Could not read your existing Mods list."; return false; }
  in.close();
  if (contents.find(value + '\n') == std::string::npos) {
    if (!contents.empty() && contents.back() != '\n') contents += '\n';
    contents += value + '\n';
  }
  return atomic_text(file, contents, error);
}
bool apply_delta(const fs::path& tool, const std::string& tool_hash, const fs::path& patch,
                 const std::string& patch_hash, const fs::path& source, const fs::path& output,
                 std::string* error, const std::atomic<bool>* cancel) {
  if (cancel && cancel->load()) { *error = "Cancelled. Nothing was installed."; return false; }
  if (!valid_sha256(tool_hash) || !valid_sha256(patch_hash) ||
      sha256_file(tool) != lower(tool_hash) || sha256_file(patch) != lower(patch_hash)) {
    *error = "The patch or its tool did not match the verified catalog. Nothing was installed."; return false;
  }
  std::error_code ec;
  if (cancel && cancel->load()) { *error = "Cancelled. Nothing was installed."; return false; }
  if (fs::exists(output, ec) || ec) { *error = "The patch output already exists."; return false; }
  auto quote = [](const fs::path& p) { return L"\"" + fs::absolute(p).wstring() + L"\""; };
  std::wstring cmd = quote(tool) + L" -d -s " + quote(source) + L" " + quote(patch) + L" " + quote(output);
  STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  const fs::path absolute_tool = fs::absolute(tool);
  if (!CreateProcessW(absolute_tool.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      absolute_tool.parent_path().c_str(), &si, &pi)) {
    *error = "The verified patcher could not start."; return false;
  }
  CloseHandle(pi.hThread);
  const ULONGLONG deadline = GetTickCount64() + 30ull * 60 * 1000;
  DWORD wait = WAIT_TIMEOUT;
  while (wait == WAIT_TIMEOUT && GetTickCount64() < deadline && !(cancel && cancel->load()))
    wait = WaitForSingleObject(pi.hProcess, 200);
  DWORD code = 1;
  if (wait == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
  else { TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 5000); }
  CloseHandle(pi.hProcess);
  if (code != 0 || !fs::is_regular_file(output, ec) || ec) {
    fs::remove(output, ec);
    *error = cancel && cancel->load() ? "Cancelled. Nothing was installed." : "The mod patcher failed. Your installed disc was kept.";
    return false;
  }
  return true;
}
}  // namespace launcher::mod_catalog
