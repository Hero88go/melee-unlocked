// Native cosmetic catalog, import, and file-aware disc overrides.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include "cosmetic_mods.h"

#include "host.h"

#include <windows.h>
#include <commdlg.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "nlohmann/json.hpp"

namespace host::cosmetics {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr uint32_t kCatalogSchema = 1;
constexpr uint32_t kProfileSchema = 1;
constexpr uint32_t kStateSchema = 1;
constexpr uint64_t kMaxCentralDirectoryBytes = 16ull * 1024 * 1024;
constexpr uint32_t kMaxDatCandidates = 32;
constexpr const char* kVanillaSelection = "@vanilla";

struct AssetRecord {
  struct Companion {
    std::string kind;
    std::string stored_path;
    std::string sha256;
    std::string source_member;
  };
  AssetInfo info;
  std::string stored_path;
  std::string source_kind;
  std::string source_name;
  std::string source_member;
  std::string source_id;
  std::vector<Companion> companions;
};

struct Profile {
  bool enabled = true;
  uint64_t generation = 1;
  std::map<std::string, std::string> selections;
};

struct RuntimeAsset {
  std::string id;
  std::string target_path;
  std::shared_ptr<const std::vector<uint8_t>> bytes;
  uint32_t vanilla_size = 0;
  bool online_allowed = true;
};

struct RuntimeState {
  bool initialized = false;
  uint64_t generation = 0;
  std::string fingerprint;
  std::unordered_map<uint32_t, RuntimeAsset> by_start;
  std::vector<CompanionOverride> companions;
};

struct ZipEntry {
  std::string name;
  uint16_t flags = 0;
  uint16_t method = 0;
  uint32_t crc = 0;
  uint64_t compressed = 0;
  uint64_t uncompressed = 0;
  uint32_t external_attributes = 0;
  uint32_t local_header_offset = 0;
};

std::mutex g_mutex;
fs::path g_root;
std::vector<AssetRecord> g_assets;
Profile g_profile;
bool g_configured = false;
bool g_catalog_valid = true;
bool g_profile_valid = true;
std::string g_message;
std::shared_ptr<const RuntimeState> g_runtime = std::make_shared<RuntimeState>();
std::atomic<uint32_t> g_online_freezes{0};

uint16_t le16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
uint32_t le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
uint32_t be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
         (uint32_t)p[3];
}
uint16_t be16(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
void put_be32(uint8_t* p, uint32_t value) {
  p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
  p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

std::string lower(std::string value) {
  for (char& c : value) c = (char)std::tolower((unsigned char)c);
  return value;
}

std::string selection_key(const AssetRecord& asset);

std::string wide_to_utf8(const std::wstring& value) {
  if (value.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), (int)value.size(),
                              nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string out((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), (int)value.size(), out.data(), n,
                      nullptr, nullptr);
  return out;
}

std::wstring bytes_to_wide(const std::string& value, bool utf8) {
  if (value.empty()) return {};
  UINT cp = utf8 ? CP_UTF8 : CP_ACP;
  DWORD flags = utf8 ? MB_ERR_INVALID_CHARS : 0;
  int n = MultiByteToWideChar(cp, flags, value.data(), (int)value.size(), nullptr, 0);
  if (n <= 0 && !utf8)
    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0), cp = CP_UTF8, flags = MB_ERR_INVALID_CHARS;
  if (n <= 0) return {};
  std::wstring out((size_t)n, L'\0');
  MultiByteToWideChar(cp, flags, value.data(), (int)value.size(), out.data(), n);
  return out;
}

std::string path_filename_utf8(const fs::path& path) {
  return wide_to_utf8(path.filename().wstring());
}

bool safe_relative_path(const std::string& raw) {
  if (raw.empty() || raw.size() > 1024 || raw.front() == '/' || raw.front() == '\\') return false;
  if (raw.find('\\') != std::string::npos || raw.find(':') != std::string::npos ||
      raw.find('"') != std::string::npos || raw.find('*') != std::string::npos ||
      raw.find('?') != std::string::npos) return false;
  std::string component;
  for (size_t i = 0; i <= raw.size(); ++i) {
    char c = i == raw.size() ? '/' : raw[i];
    if ((unsigned char)c < 0x20) return false;
    if (c == '/') {
      // A single trailing slash is accepted for a directory entry. Empty interior components,
      // dot components, and parent traversal are rejected on every platform.
      if (component.empty()) return i == raw.size() && i > 0 && raw[i - 1] == '/';
      if (component == "." || component == "..") return false;
      component.clear();
    } else component += c;
  }
  return true;
}

bool read_bounded(const fs::path& path, uint64_t limit, std::vector<uint8_t>* out,
                  std::string* error) {
  std::error_code ec;
  uint64_t size = fs::file_size(path, ec);
  if (ec) { *error = "Cannot read " + path_filename_utf8(path) + "."; return false; }
  if (!size) { *error = "The selected file is empty."; return false; }
  if (size > limit) {
    *error = "The selected resource is larger than the " + std::to_string(limit / (1024 * 1024)) + " MB safety limit.";
    return false;
  }
  out->resize((size_t)size);
  std::ifstream file(path, std::ios::binary);
  if (!file || !file.read((char*)out->data(), (std::streamsize)out->size())) {
    out->clear(); *error = "The selected file could not be read completely."; return false;
  }
  return true;
}

uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

std::string sha256(const uint8_t* data, size_t size) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  uint8_t digest[32]{};
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
  NTSTATUS result = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(data), (ULONG)size,
                               digest, sizeof digest);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (result < 0) return {};
  std::ostringstream text;
  text << std::hex << std::setfill('0');
  for (uint8_t byte : digest) text << std::setw(2) << (unsigned)byte;
  return text.str();
}

std::string sha256(const std::vector<uint8_t>& data) { return sha256(data.data(), data.size()); }
std::string sha256_text(const std::string& text) {
  return sha256((const uint8_t*)text.data(), text.size());
}

bool write_atomic(const fs::path& path, const uint8_t* data, size_t size, std::string* error) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) { *error = "Cannot create the cosmetic catalog folder."; return false; }
  fs::path temporary = path;
  temporary += L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) { *error = "Cannot create a temporary catalog file."; return false; }
  bool ok = true;
  size_t written_total = 0;
  while (written_total < size) {
    DWORD chunk = (DWORD)std::min<size_t>(size - written_total, 1u << 20);
    DWORD written = 0;
    if (!WriteFile(file, data + written_total, chunk, &written, nullptr) || written != chunk) {
      ok = false; break;
    }
    written_total += written;
  }
  if (ok) ok = FlushFileBuffers(file) != FALSE;
  CloseHandle(file);
  if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
  if (!ok) {
    DeleteFileW(temporary.c_str());
    *error = "The cosmetic catalog could not be committed atomically.";
  }
  return ok;
}

bool write_atomic(const fs::path& path, const std::string& text, std::string* error) {
  return write_atomic(path, (const uint8_t*)text.data(), text.size(), error);
}

json string_array(const std::vector<std::string>& values) {
  json out = json::array();
  for (const auto& value : values) out.push_back(value);
  return out;
}

std::vector<std::string> get_string_array(const json& object, const char* key) {
  std::vector<std::string> out;
  auto it = object.find(key);
  if (it == object.end() || !it->is_array()) return out;
  for (const auto& value : *it) if (value.is_string()) out.push_back(value.get<std::string>());
  return out;
}

void load_companions(const json& item, AssetRecord* asset) {
  auto found = item.find("companions");
  if (found == item.end()) return;
  if (!found->is_array()) throw std::runtime_error("companions");
  for (const auto& value : *found) {
    AssetRecord::Companion companion;
    companion.kind = value.at("kind").get<std::string>();
    companion.stored_path = value.at("stored_path").get<std::string>();
    companion.sha256 = value.at("sha256").get<std::string>();
    companion.source_member = value.value("source_member", std::string());
    if ((companion.kind != "csp" && companion.kind != "stock" && companion.kind != "preview") ||
        !safe_relative_path(companion.stored_path) || companion.sha256.size() != 64)
      throw std::runtime_error("companion");
    asset->companions.push_back(std::move(companion));
  }
}

json catalog_json_locked() {
  json root;
  root["schema_version"] = kCatalogSchema;
  root["assets"] = json::array();
  for (const auto& asset : g_assets) {
    json item;
    item["id"] = asset.info.id;
    item["name"] = asset.info.name;
    item["kind"] = asset.info.kind;
    item["target_path"] = asset.info.target_path;
    item["character"] = asset.info.character;
    item["costume"] = asset.info.costume;
    item["sha256"] = asset.info.sha256;
    item["stored_path"] = asset.stored_path;
    item["roots"] = string_array(asset.info.roots);
    item["dependencies"] = string_array(asset.info.dependencies);
    item["unsupported_companions"] = string_array(asset.info.unsupported_companions);
    item["source"] = {{"kind", asset.source_kind}, {"name", asset.source_name},
                       {"member", asset.source_member}, {"id", asset.source_id}};
    item["companions"] = json::array();
    for (const auto& companion : asset.companions) {
      item["companions"].push_back({{"kind", companion.kind},
                                      {"stored_path", companion.stored_path},
                                      {"sha256", companion.sha256},
                                      {"source_member", companion.source_member},
                                      {"status", "native_texture_override"}});
    }
    root["assets"].push_back(std::move(item));
  }
  return root;
}

json profile_json_locked() {
  json root;
  root["schema_version"] = kProfileSchema;
  root["name"] = "Default";
  root["enabled"] = g_profile.enabled;
  root["generation"] = g_profile.generation;
  root["selections"] = json::object();
  for (const auto& pick : g_profile.selections) root["selections"][pick.first] = pick.second;
  return root;
}

bool save_state_locked(std::string* error) {
  json state;
  state["schema_version"] = kStateSchema;
  state["catalog"] = catalog_json_locked();
  state["profile"] = profile_json_locked();
  if (!write_atomic(g_root / L"state.json", state.dump(2) + "\n", error)) return false;
  // These readable mirrors preserve compatibility with older builds and diagnostics. state.json is
  // authoritative because it publishes catalog and profile together in one atomic replacement.
  std::string ignored;
  write_atomic(g_root / L"catalog.json", catalog_json_locked().dump(2) + "\n", &ignored);
  write_atomic(g_root / L"profile.json", profile_json_locked().dump(2) + "\n", &ignored);
  return true;
}
bool save_catalog_locked(std::string* error) { return save_state_locked(error); }
bool save_profile_locked(std::string* error) { return save_state_locked(error); }

std::string desired_fingerprint_locked() {
  std::string source = g_profile.enabled ? "enabled\n" : "disabled\n";
  for (const auto& pick : g_profile.selections) {
    if (pick.second == kVanillaSelection) {
      source += pick.first + "=" + kVanillaSelection + "\n";
      continue;
    }
    auto found = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& a) {
      return a.info.id == pick.second && selection_key(a) == pick.first;
    });
    if (found != g_assets.end()) source += pick.first + "=" + pick.second + "=" + found->info.sha256 + "\n";
  }
  return sha256_text(source);
}

bool load_json_file(const fs::path& path, json* out, bool* exists, std::string* error) {
  std::error_code ec;
  *exists = fs::exists(path, ec);
  if (ec || !*exists) return !ec;
  uint64_t size = fs::file_size(path, ec);
  if (ec || size > 16ull * 1024 * 1024) {
    *error = path_filename_utf8(path) + " exceeds the 16 MB metadata safety limit."; return false;
  }
  std::ifstream file(path);
  if (!file) { *error = "Cannot open " + path_filename_utf8(path) + "."; return false; }
  try { file >> *out; }
  catch (...) { *error = path_filename_utf8(path) + " is not valid JSON."; return false; }
  return true;
}

void load_catalog_locked() {
  g_assets.clear(); g_catalog_valid = true;
  json root; bool exists = false; std::string error;
  if (!load_json_file(g_root / L"catalog.json", &root, &exists, &error)) {
    g_catalog_valid = false; g_message = error; return;
  }
  if (!exists) return;
  if (!root.is_object() || root.value("schema_version", 0u) != kCatalogSchema ||
      !root["assets"].is_array()) {
    g_catalog_valid = false; g_message = "catalog.json has an unsupported or malformed schema."; return;
  }
  try {
    for (const auto& item : root["assets"]) {
      AssetRecord asset;
      asset.info.id = item.at("id").get<std::string>();
      asset.info.name = item.at("name").get<std::string>();
      asset.info.kind = item.at("kind").get<std::string>();
      asset.info.target_path = item.at("target_path").get<std::string>();
      asset.info.character = item.value("character", std::string());
      asset.info.costume = item.value("costume", std::string());
      asset.info.sha256 = item.at("sha256").get<std::string>();
      asset.stored_path = item.at("stored_path").get<std::string>();
      asset.info.roots = get_string_array(item, "roots");
      asset.info.dependencies = get_string_array(item, "dependencies");
      asset.info.unsupported_companions = get_string_array(item, "unsupported_companions");
      load_companions(item, &asset);
      if (item.find("source") != item.end() && item["source"].is_object()) {
        asset.source_kind = item["source"].value("kind", std::string());
        asset.source_name = item["source"].value("name", std::string());
        asset.source_member = item["source"].value("member", std::string());
        asset.source_id = item["source"].value("id", std::string());
      }
      if (asset.info.id.empty() || asset.info.name.empty() || asset.info.sha256.size() != 64 ||
          !safe_relative_path(asset.stored_path) || asset.info.target_path.find('/') != std::string::npos ||
          asset.info.target_path.find('\\') != std::string::npos)
        throw std::runtime_error("invalid asset");
      g_assets.push_back(std::move(asset));
    }
  } catch (...) {
    g_assets.clear(); g_catalog_valid = false;
    g_message = "catalog.json contains an invalid asset record; it was not modified.";
  }
}

void load_profile_locked() {
  g_profile = Profile{}; g_profile_valid = true;
  json root; bool exists = false; std::string error;
  if (!load_json_file(g_root / L"profile.json", &root, &exists, &error)) {
    g_profile_valid = false; g_message = error; return;
  }
  if (!exists) return;
  try {
    if (!root.is_object() || root.value("schema_version", 0u) != kProfileSchema ||
        !root["selections"].is_object()) throw std::runtime_error("schema");
    g_profile.enabled = root.value("enabled", true);
    g_profile.generation = std::max<uint64_t>(1, root.value("generation", 1ull));
    for (auto it = root["selections"].begin(); it != root["selections"].end(); ++it) {
      if (!it.value().is_string() || it.key().find('/') != std::string::npos ||
          it.key().find('\\') != std::string::npos) throw std::runtime_error("selection");
      g_profile.selections[it.key()] = it.value().get<std::string>();
    }
  } catch (...) {
    g_profile = Profile{}; g_profile_valid = false;
    g_message = "profile.json has an unsupported or malformed schema; it was not modified.";
  }
}

bool ready_locked(std::string* error) {
  if (!g_configured) { *error = "The cosmetic catalog has not been configured."; return false; }
  if (!g_catalog_valid || !g_profile_valid) { *error = g_message; return false; }
  return true;
}

bool load_state_locked(bool* exists) {
  g_assets.clear(); g_profile = Profile{}; g_catalog_valid = g_profile_valid = true;
  json state; std::string error;
  if (!load_json_file(g_root / L"state.json", &state, exists, &error)) {
    g_assets.clear(); g_profile = Profile{}; g_catalog_valid = g_profile_valid = false;
    g_message = error; return false;
  }
  if (!*exists) return true;
  try {
    if (!state.is_object() || state.value("schema_version", 0u) != kStateSchema ||
        !state.at("catalog").is_object() || !state.at("profile").is_object())
      throw std::runtime_error("schema");
    const json& catalog = state.at("catalog");
    const json& profile = state.at("profile");
    if (catalog.value("schema_version", 0u) != kCatalogSchema ||
        !catalog.at("assets").is_array() ||
        profile.value("schema_version", 0u) != kProfileSchema ||
        !profile.at("selections").is_object())
      throw std::runtime_error("nested schema");
    std::vector<AssetRecord> assets;
    for (const auto& item : catalog.at("assets")) {
      AssetRecord asset;
      asset.info.id = item.at("id").get<std::string>();
      asset.info.name = item.at("name").get<std::string>();
      asset.info.kind = item.at("kind").get<std::string>();
      asset.info.target_path = item.at("target_path").get<std::string>();
      asset.info.character = item.value("character", std::string());
      asset.info.costume = item.value("costume", std::string());
      asset.info.sha256 = item.at("sha256").get<std::string>();
      asset.stored_path = item.at("stored_path").get<std::string>();
      asset.info.roots = get_string_array(item, "roots");
      asset.info.dependencies = get_string_array(item, "dependencies");
      asset.info.unsupported_companions = get_string_array(item, "unsupported_companions");
      load_companions(item, &asset);
      if (item.find("source") != item.end() && item["source"].is_object()) {
        asset.source_kind = item["source"].value("kind", std::string());
        asset.source_name = item["source"].value("name", std::string());
        asset.source_member = item["source"].value("member", std::string());
        asset.source_id = item["source"].value("id", std::string());
      }
      if (asset.info.id.empty() || asset.info.name.empty() || asset.info.sha256.size() != 64 ||
          !safe_relative_path(asset.stored_path) || asset.info.target_path.find('/') != std::string::npos ||
          asset.info.target_path.find('\\') != std::string::npos)
        throw std::runtime_error("asset");
      assets.push_back(std::move(asset));
    }
    Profile next;
    next.enabled = profile.value("enabled", true);
    next.generation = std::max<uint64_t>(1, profile.value("generation", 1ull));
    for (auto it = profile.at("selections").begin(); it != profile.at("selections").end(); ++it) {
      if (!it.value().is_string() || it.key().find('/') != std::string::npos ||
          it.key().find('\\') != std::string::npos) throw std::runtime_error("selection");
      next.selections[it.key()] = it.value().get<std::string>();
    }
    g_assets = std::move(assets); g_profile = std::move(next);
    g_catalog_valid = g_profile_valid = true;
    return true;
  } catch (...) {
    g_assets.clear(); g_profile = Profile{}; g_catalog_valid = g_profile_valid = false;
    g_message = "state.json has an unsupported or malformed schema; it was not modified.";
    return false;
  }
}

bool mutable_profile_locked(std::string* error) {
  if (!ready_locked(error)) return false;
  if (g_online_freezes.load(std::memory_order_relaxed)) {
    *error = "The cosmetic profile is frozen while an online session is queued or active.";
    return false;
  }
  return true;
}

bool parse_zip(const fs::path& path, std::vector<ZipEntry>* entries, std::string* error) {
  entries->clear();
  std::error_code ec;
  uint64_t file_size = fs::file_size(path, ec);
  if (ec) { *error = "The ZIP archive could not be opened."; return false; }
  if (!file_size || file_size > kMaxArchiveBytes) {
    *error = "The ZIP archive is empty or larger than the 512 MB safety limit."; return false;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) { *error = "The ZIP archive could not be opened."; return false; }
  uint64_t tail_size = std::min<uint64_t>(file_size, 65557);
  std::vector<uint8_t> tail((size_t)tail_size);
  file.seekg((std::streamoff)(file_size - tail_size));
  if (!file.read((char*)tail.data(), (std::streamsize)tail.size())) {
    *error = "The ZIP end record could not be read."; return false;
  }
  size_t eocd = std::numeric_limits<size_t>::max();
  for (size_t i = tail.size() >= 22 ? tail.size() - 22 : 0;;) {
    if (i + 4 <= tail.size() && le32(&tail[i]) == 0x06054b50u) { eocd = i; break; }
    if (i == 0) break;
    --i;
  }
  if (eocd == std::numeric_limits<size_t>::max() || eocd + 22 > tail.size()) {
    *error = "The ZIP end record is missing."; return false;
  }
  const uint8_t* end = &tail[eocd];
  uint16_t disk = le16(end + 4), cd_disk = le16(end + 6), disk_entries = le16(end + 8),
           total_entries = le16(end + 10), comment = le16(end + 20);
  uint64_t cd_size = le32(end + 12), cd_offset = le32(end + 16);
  if (disk || cd_disk || disk_entries != total_entries || total_entries == 0 ||
      total_entries == 0xffff || cd_size == 0xffffffffu || cd_offset == 0xffffffffu) {
    *error = "Multi-disk and ZIP64 archives are not supported in the first importer."; return false;
  }
  if (total_entries > kMaxArchiveEntries || cd_size > kMaxCentralDirectoryBytes ||
      cd_offset + cd_size > file_size || eocd + 22 + comment > tail.size()) {
    *error = "The ZIP central directory exceeds safety limits or points outside the archive."; return false;
  }
  std::vector<uint8_t> central((size_t)cd_size);
  file.clear(); file.seekg((std::streamoff)cd_offset);
  if (!file.read((char*)central.data(), (std::streamsize)central.size())) {
    *error = "The ZIP central directory could not be read completely."; return false;
  }
  size_t cursor = 0;
  uint64_t total_uncompressed = 0;
  std::map<std::string, bool> seen_paths;
  for (uint32_t index = 0; index < total_entries; ++index) {
    if (cursor + 46 > central.size() || le32(&central[cursor]) != 0x02014b50u) {
      *error = "The ZIP central directory is truncated or malformed."; return false;
    }
    const uint8_t* h = &central[cursor];
    uint16_t name_len = le16(h + 28), extra_len = le16(h + 30), comment_len = le16(h + 32);
    uint64_t record_size = 46ull + name_len + extra_len + comment_len;
    if (!name_len || cursor + record_size > central.size()) {
      *error = "A ZIP entry has an invalid name or record length."; return false;
    }
    ZipEntry entry;
    entry.flags = le16(h + 8); entry.method = le16(h + 10); entry.crc = le32(h + 16);
    entry.compressed = le32(h + 20); entry.uncompressed = le32(h + 24);
    entry.external_attributes = le32(h + 38);
    entry.local_header_offset = le32(h + 42);
    entry.name.assign((const char*)h + 46, name_len);
    if (!safe_relative_path(entry.name)) {
      *error = "Unsafe path in ZIP archive: " + entry.name; return false;
    }
    if (!seen_paths.emplace(lower(entry.name), true).second) {
      *error = "Duplicate case-insensitive path in ZIP archive: " + entry.name; return false;
    }
    if ((entry.flags & 1) || (entry.flags & 0x40)) {
      *error = "Encrypted ZIP entries are not supported."; return false;
    }
    if (entry.method != 0 && entry.method != 8) {
      *error = "ZIP entry uses an unsupported compression method: " + entry.name; return false;
    }
    uint32_t unix_mode = (entry.external_attributes >> 16) & 0170000;
    if (unix_mode == 0120000) { *error = "Symbolic links are not allowed in imported archives."; return false; }
    if (le16(h + 34) != 0 || entry.local_header_offset >= cd_offset) {
      *error = "A ZIP entry points outside the local-file area."; return false;
    }
    if (entry.uncompressed > kMaxAssetBytes && lower(fs::path(entry.name).extension().string()) == ".dat") {
      *error = "A DAT resource in the ZIP exceeds the 64 MB safety limit."; return false;
    }
    if (total_uncompressed > kMaxArchiveBytes - entry.uncompressed) {
      *error = "The ZIP expands beyond the 512 MB aggregate safety limit."; return false;
    }
    total_uncompressed += entry.uncompressed;
    entries->push_back(std::move(entry));
    cursor += (size_t)record_size;
  }
  if (cursor != central.size()) {
    *error = "The ZIP central directory contains unexpected trailing records."; return false;
  }
  // Match every central entry to its local header before an external extractor sees the archive.
  // This prevents a benign central path from authorizing a different local path or data range.
  for (const auto& entry : *entries) {
    uint8_t local[30];
    file.clear(); file.seekg((std::streamoff)entry.local_header_offset);
    if (!file.read((char*)local, sizeof local) || le32(local) != 0x04034b50u) {
      *error = "A ZIP local-file header is missing or truncated."; return false;
    }
    uint16_t flags = le16(local + 6), method = le16(local + 8),
             name_len = le16(local + 26), extra_len = le16(local + 28);
    if (flags != entry.flags || method != entry.method || name_len != entry.name.size()) {
      *error = "A ZIP local header disagrees with its central-directory record."; return false;
    }
    std::string local_name(name_len, '\0');
    if (!file.read(local_name.data(), name_len) || local_name != entry.name) {
      *error = "A ZIP local path disagrees with its validated central path."; return false;
    }
    uint64_t data_start = (uint64_t)entry.local_header_offset + 30ull + name_len + extra_len;
    if (data_start > cd_offset || entry.compressed > cd_offset - data_start) {
      *error = "A ZIP member's compressed data points outside the local-file area."; return false;
    }
  }
  return true;
}

std::wstring quote_windows_arg(const std::wstring& arg) {
  std::wstring out = L"\"";
  size_t slashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') { ++slashes; continue; }
    if (c == L'\"') { out.append(slashes * 2 + 1, L'\\'); out += L'\"'; slashes = 0; continue; }
    out.append(slashes, L'\\'); slashes = 0; out += c;
  }
  out.append(slashes * 2, L'\\'); out += L'\"';
  return out;
}

bool extract_zip_member(const fs::path& archive, const ZipEntry& entry,
                        std::vector<uint8_t>* bytes, std::string* error) {
  wchar_t system[MAX_PATH];
  UINT n = GetSystemDirectoryW(system, MAX_PATH);
  if (!n || n >= MAX_PATH) { *error = "Cannot locate the Windows system directory."; return false; }
  fs::path tar = fs::path(system) / L"tar.exe";
  if (!fs::exists(tar)) {
    *error = "Windows tar.exe is required to read ZIP imports (Windows 10 version 1803 or newer).";
    return false;
  }
  std::error_code ec;
  fs::create_directories(g_root / L".staging", ec);
  if (ec) { *error = "Cannot create the cosmetic import staging folder."; return false; }
  static std::atomic<uint32_t> sequence{0};
  fs::path output = g_root / L".staging" /
      (L"member-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
       std::to_wstring(sequence.fetch_add(1)) + L".tmp");
  SECURITY_ATTRIBUTES security{sizeof security, nullptr, TRUE};
  HANDLE stdout_file = CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (stdout_file == INVALID_HANDLE_VALUE) { *error = "Cannot create an import staging file."; return false; }
  HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  std::wstring member = bytes_to_wide(entry.name, (entry.flags & (1 << 11)) != 0);
  if (member.empty()) {
    CloseHandle(stdout_file); if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);
    DeleteFileW(output.c_str()); *error = "A ZIP filename could not be decoded."; return false;
  }
  std::wstring command = quote_windows_arg(tar.wstring()) + L" -xOf " +
                         quote_windows_arg(archive.wstring()) + L" -- " + quote_windows_arg(member);
  STARTUPINFOW startup{}; startup.cb = sizeof startup;
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = null_input == INVALID_HANDLE_VALUE ? nullptr : null_input;
  startup.hStdOutput = stdout_file;
  startup.hStdError = null_input == INVALID_HANDLE_VALUE ? stdout_file : null_input;
  PROCESS_INFORMATION process{};
  BOOL started = CreateProcessW(tar.c_str(), command.data(), nullptr, nullptr, TRUE,
                                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
  CloseHandle(stdout_file); if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);
  if (!started) { DeleteFileW(output.c_str()); *error = "Windows tar.exe could not be started."; return false; }
  DWORD wait = WaitForSingleObject(process.hProcess, 60000);
  if (wait == WAIT_TIMEOUT) { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 5000); }
  DWORD exit_code = 1; GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hThread); CloseHandle(process.hProcess);
  if (wait != WAIT_OBJECT_0) {
    DeleteFileW(output.c_str());
    *error = "ZIP member extraction timed out: " + entry.name + ".";
    return false;
  }
  // bsdtar may return a nonzero status after successfully writing the requested member when some
  // unrelated archive entry has a filename Windows cannot display. The central/local header pair
  // above already identified this exact member. Accept the output only when its independently
  // bounded length and CRC below match that validated entry; a missing/partial/wrong member still
  // fails closed regardless of tar's status.
  bool ok = read_bounded(output, kMaxAssetBytes, bytes, error);
  DeleteFileW(output.c_str());
  if (!ok) {
    *error = "ZIP member could not be extracted safely: " + entry.name +
             " (tar exit " + std::to_string(exit_code) + ").";
    return false;
  }
  if (bytes->size() != entry.uncompressed) {
    bytes->clear(); *error = "The extracted size does not match the ZIP central directory."; return false;
  }
  if (crc32(bytes->data(), bytes->size()) != entry.crc) {
    bytes->clear(); *error = "The extracted data does not match the ZIP CRC."; return false;
  }
  return true;
}

testing::DatInspection inspect_dat_impl(const std::vector<uint8_t>& bytes) {
  testing::DatInspection result;
  if (bytes.size() < 0x20) { result.error = "DAT header is truncated."; return result; }
  uint64_t file_size = be32(bytes.data()), data_size = be32(bytes.data() + 4),
           relocations = be32(bytes.data() + 8), roots = be32(bytes.data() + 12),
           references = be32(bytes.data() + 16);
  if (file_size != bytes.size()) { result.error = "DAT header size does not match the file length."; return result; }
  if (!roots || roots > 1024 || references > 1024 || relocations > (bytes.size() / 4)) {
    result.error = "DAT relocation/root counts are outside supported bounds."; return result;
  }
  uint64_t root_table = 0x20ull + data_size + relocations * 4ull;
  uint64_t strings = root_table + (roots + references) * 8ull;
  if (root_table > bytes.size() || strings > bytes.size()) {
    result.error = "DAT tables point outside the file."; return result;
  }
  // Every relocation identifies an aligned pointer field in the data block. The pointer itself is
  // either null or another data-block-relative offset. Real Melee DAT relocation lists are not
  // necessarily sorted, so ordering is deliberately not part of the contract.
  for (uint64_t i = 0; i < relocations; ++i) {
    uint64_t relocation_offset = be32(&bytes[(size_t)(0x20ull + data_size + i * 4ull)]);
    if ((relocation_offset & 3) || relocation_offset + 4 > data_size) {
      result.error = "DAT relocation entry is unaligned or points outside the data block.";
      return result;
    }
    uint64_t target = be32(&bytes[(size_t)(0x20ull + relocation_offset)]);
    if (target && target >= data_size) {
      result.error = "DAT relocation target points outside the data block."; return result;
    }
  }
  std::vector<std::string> symbols;
  symbols.reserve((size_t)(roots + references));
  for (uint64_t i = 0; i < roots + references; ++i) {
    uint64_t record = root_table + i * 8;
    uint64_t object_offset = be32(&bytes[(size_t)record]);
    uint64_t name_offset = be32(&bytes[(size_t)record + 4]);
    if (object_offset >= data_size || name_offset >= bytes.size() - strings) {
      result.error = "DAT root entry points outside its data/string table."; return result;
    }
    size_t begin = (size_t)(strings + name_offset), end = begin;
    while (end < bytes.size() && bytes[end]) {
      if (bytes[end] < 0x20 || bytes[end] > 0x7e || end - begin > 255) {
        result.error = "DAT root symbol is not a bounded printable string."; return result;
      }
      ++end;
    }
    if (end == bytes.size()) { result.error = "DAT root symbol is not terminated."; return result; }
    symbols.emplace_back((const char*)&bytes[begin], end - begin);
    if (i < roots) result.roots.push_back(symbols.back());
  }

  struct FighterFamily {
    const char* file_code;
    const char* root_name;
    const char* display_name;
    const char* colors;
  };
  // These are the existing NTSC 1.02 costume files only. Defaults use an un-suffixed 5K root even
  // though their disc filename ends in Nr. Bosses, wireframes, Sandbag, extra CSS slots, and base
  // fighter data archives are intentionally absent.
  static constexpr FighterFamily families[] = {
      {"Ca", "Captain", "Captain Falcon", "Nr Re Wh Gr Bu Gy"},
      {"Cl", "Clink", "Young Link", "Nr Re Wh Bk Bu"},
      {"Dk", "Donkey", "Donkey Kong", "Nr Re Bu Gr Bk"},
      {"Dr", "Drmario", "Dr. Mario", "Nr Re Bu Gr Bk"},
      {"Fc", "Falco", "Falco", "Nr Re Bu Gr"},
      {"Fe", "Emblem", "Roy", "Nr Re Bu Gr Ye"},
      {"Fx", "Fox", "Fox", "Nr Or La Gr"},
      {"Gn", "Ganon", "Ganondorf", "Nr Re Bu Gr La"},
      {"Gw", "Gamewatch", "Mr. Game & Watch", "Nr"},
      {"Kb", "Kirby", "Kirby", "Nr Ye Bu Re Gr Wh"},
      {"Kp", "Koopa", "Bowser", "Nr Re Bu Bk"},
      {"Lg", "Luigi", "Luigi", "Nr Wh Aq Pi"},
      {"Lk", "Link", "Link", "Nr Re Bu Bk Wh"},
      {"Mr", "Mario", "Mario", "Nr Ye Bk Bu Gr"},
      {"Ms", "Mars", "Marth", "Nr Re Gr Bk Wh"},
      {"Mt", "Mewtwo", "Mewtwo", "Nr Re Bu Gr"},
      {"Nn", "Nana", "Ice Climbers (Nana)", "Nr Ye Aq Wh"},
      {"Ns", "Ness", "Ness", "Nr Ye Bu Gr"},
      {"Pc", "Pichu", "Pichu", "Nr Re Bu Gr"},
      {"Pe", "Peach", "Peach", "Nr Ye Wh Bu Gr"},
      {"Pk", "Pikachu", "Pikachu", "Nr Re Bu Gr"},
      {"Pp", "Popo", "Ice Climbers (Popo)", "Nr Re Gr Or"},
      {"Pr", "Purin", "Jigglypuff", "Nr Re Bu Gr Ye"},
      {"Sk", "Seak", "Sheik", "Nr Re Bu Gr Wh"},
      {"Ss", "Samus", "Samus", "Nr Pi Bk Gr La"},
      {"Ys", "Yoshi", "Yoshi", "Nr Re Bu Ye Pi Aq"},
      {"Zd", "Zelda", "Zelda", "Nr Re Bu Gr Wh"},
  };
  struct ColorName { const char* code; const char* label; };
  static constexpr ColorName color_names[] = {
      {"Nr", "Default"}, {"Re", "Red"}, {"Bu", "Blue"}, {"Gr", "Green"},
      {"Wh", "White"}, {"Bk", "Black"}, {"Ye", "Yellow"}, {"Or", "Orange"},
      {"La", "Lavender"}, {"Pi", "Pink"}, {"Aq", "Aqua"}, {"Gy", "Gray"},
  };
  struct Match { const FighterFamily* family; const ColorName* color; };
  std::vector<Match> matches;
  for (const auto& family : families) {
    std::string allowed = std::string(" ") + family.colors + " ";
    for (const auto& color : color_names) {
      if (allowed.find(std::string(" ") + color.code + " ") == std::string::npos) continue;
      std::string identity = std::string("Ply") + family.root_name + "5K" +
                             (std::strcmp(color.code, "Nr") ? color.code : "") +
                             "_Share_joint";
      if (std::find(result.roots.begin(), result.roots.end(), identity) != result.roots.end())
        matches.push_back({&family, &color});
    }
  }
  if (matches.size() == 1) {
    result.ok = true;
    result.character = matches[0].family->display_name;
    result.costume = matches[0].color->label;
    result.target_path = std::string("Pl") + matches[0].family->file_code +
                         matches[0].color->code + ".dat";
    return result;
  }
  if (matches.size() > 1) {
    result.error = "DAT roots conflict across multiple existing costume slots."; return result;
  }
  result.error = "DAT roots do not identify a supported existing costume slot.";
  return result;
}

struct VisualImage {
  uint32_t target = 0, size = 0;
  uint16_t width = 0, height = 0;
  uint32_t format = 0, mipmap = 0, min_lod_bits = 0, max_lod_bits = 0;
  bool operator==(const VisualImage& other) const {
    return target == other.target && size == other.size && width == other.width &&
           height == other.height && format == other.format && mipmap == other.mipmap &&
           min_lod_bits == other.min_lod_bits && max_lod_bits == other.max_lod_bits;
  }
};
struct VisualPalette {
  uint32_t target = 0, size = 0, format = 0, name = 0;
  uint16_t entries = 0;
  bool operator==(const VisualPalette& other) const {
    return target == other.target && size == other.size && format == other.format &&
           name == other.name && entries == other.entries;
  }
};
struct VisualLayout {
  uint32_t data_size = 0, relocation_start = 0;
  std::vector<uint32_t> relocations;
  std::vector<std::string> roots;
  std::map<uint32_t, VisualImage> images;
  std::map<uint32_t, VisualPalette> palettes;
};

uint32_t gx_image_size(uint32_t width, uint32_t height, uint32_t format,
                       uint32_t mipmap, float max_lod) {
  uint32_t block_width = 4, block_height = 4, block_bytes = 32;
  switch (format) {
    case 0: case 8: case 14: block_width = 8; block_height = 8; break;
    case 1: case 2: case 9: block_width = 8; block_height = 4; break;
    case 6: block_bytes = 64; break;
    case 3: case 4: case 5: case 10: break;
    default: return 0;
  }
  uint32_t levels = mipmap ? (uint32_t)max_lod + 1 : 1, total = 0;
  for (uint32_t level = 0; level < levels; ++level) {
    uint64_t blocks = ((uint64_t)width + block_width - 1) / block_width *
                      (((uint64_t)height + block_height - 1) / block_height);
    if (blocks > (UINT32_MAX - total) / block_bytes) return 0;
    total += (uint32_t)blocks * block_bytes;
    width = std::max(1u, width / 2); height = std::max(1u, height / 2);
  }
  return total;
}

bool parse_visual_layout(const std::vector<uint8_t>& bytes, VisualLayout* out,
                         std::string* error) {
  out->relocations.clear(); out->roots.clear(); out->images.clear(); out->palettes.clear();
  if (bytes.size() < 0x20 || bytes.size() > kMaxAssetBytes || be32(bytes.data()) != bytes.size()) {
    *error = "Visual DAT header size does not match its bounded file length."; return false;
  }
  uint32_t data_size = be32(bytes.data() + 4), relocations = be32(bytes.data() + 8),
           roots = be32(bytes.data() + 12), references = be32(bytes.data() + 16);
  uint64_t relocation_start = 0x20ull + data_size;
  uint64_t root_start = relocation_start + (uint64_t)relocations * 4;
  uint64_t strings = root_start + (uint64_t)(roots + references) * 8;
  if (!roots || roots > 1024 || references > 1024 || relocations > bytes.size() / 4 ||
      strings > bytes.size()) {
    *error = "Visual DAT tables are outside supported bounds."; return false;
  }
  out->data_size = data_size; out->relocation_start = (uint32_t)relocation_start;
  for (uint32_t i = 0; i < relocations; ++i) {
    uint32_t offset = be32(bytes.data() + relocation_start + (uint64_t)i * 4);
    if ((offset & 3) || (uint64_t)offset + 4 > data_size) {
      *error = "Visual DAT relocation is unaligned or outside its data block."; return false;
    }
    uint32_t target = be32(bytes.data() + 0x20ull + offset);
    if (target && target >= data_size) {
      *error = "Visual DAT relocation target is outside its data block."; return false;
    }
    out->relocations.push_back(offset);
  }
  for (uint32_t i = 0; i < roots + references; ++i) {
    uint64_t record = root_start + (uint64_t)i * 8;
    uint32_t object = be32(bytes.data() + record), name = be32(bytes.data() + record + 4);
    if (object >= data_size || name >= bytes.size() - strings) {
      *error = "Visual DAT root/reference points outside its tables."; return false;
    }
    size_t cursor = (size_t)strings + name, begin = cursor, count = 0;
    while (cursor < bytes.size() && bytes[cursor] && count++ <= 255) {
      if (bytes[cursor] < 0x20 || bytes[cursor] > 0x7e) {
        *error = "Visual DAT symbol is not printable ASCII."; return false;
      }
      ++cursor;
    }
    if (cursor == bytes.size() || count > 256) {
      *error = "Visual DAT symbol is not bounded and terminated."; return false;
    }
    if (i < roots) out->roots.emplace_back((const char*)bytes.data() + begin, cursor - begin);
  }
  for (uint32_t offset : out->relocations) {
    if ((uint64_t)offset + 24 > data_size) continue;
    const uint8_t* descriptor = bytes.data() + 0x20ull + offset;
    VisualImage image;
    image.target = be32(descriptor); image.width = (uint16_t)(be32(descriptor + 4) >> 16);
    image.height = (uint16_t)be32(descriptor + 4);
    image.format = be32(descriptor + 8); image.mipmap = be32(descriptor + 12);
    image.min_lod_bits = be32(descriptor + 16); image.max_lod_bits = be32(descriptor + 20);
    float min_lod = 0, max_lod = 0;
    std::memcpy(&min_lod, &image.min_lod_bits, 4); std::memcpy(&max_lod, &image.max_lod_bits, 4);
    if (!image.target || !image.width || !image.height || image.width > 4096 || image.height > 4096 ||
        image.mipmap > 1 || !std::isfinite(min_lod) || !std::isfinite(max_lod) ||
        min_lod < 0 || max_lod < min_lod || max_lod > 12 ||
        (!image.mipmap && (min_lod != 0 || max_lod != 0))) continue;
    image.size = gx_image_size(image.width, image.height, image.format, image.mipmap, max_lod);
    if (!image.size || (uint64_t)image.target + image.size > data_size) continue;
    out->images[offset] = image;
  }
  if (out->images.empty()) { *error = "Visual DAT has no validated GX image descriptors."; return false; }
  // HSD_TObjDesc stores its image and TLUT descriptor pointers at +0x4c and +0x50. Only accept a
  // palette descriptor reached through a pointer to an already validated image descriptor.
  for (uint32_t image_pointer : out->relocations) {
    uint32_t image_descriptor = be32(bytes.data() + 0x20ull + image_pointer);
    if (image_pointer < 0x4c || out->images.find(image_descriptor) == out->images.end()) continue;
    uint32_t palette_pointer = image_pointer + 4;
    if (std::find(out->relocations.begin(), out->relocations.end(), palette_pointer) ==
        out->relocations.end()) continue;
    uint32_t descriptor = be32(bytes.data() + 0x20ull + palette_pointer);
    if (!descriptor || (uint64_t)descriptor + 16 > data_size ||
        std::find(out->relocations.begin(), out->relocations.end(), descriptor) ==
            out->relocations.end()) continue;
    const uint8_t* palette = bytes.data() + 0x20ull + descriptor;
    VisualPalette item;
    item.target = be32(palette); item.format = be32(palette + 4); item.name = be32(palette + 8);
    item.entries = (uint16_t)(be32(palette + 12) >> 16); item.size = (uint32_t)item.entries * 2;
    if ((item.format > 2) ||
        (item.entries != 16 && item.entries != 256 && item.entries != 16384) ||
        (uint64_t)item.target + item.size > data_size) continue;
    out->palettes[descriptor] = item;
  }
  return true;
}

bool visual_dat_only(const std::vector<uint8_t>& clean, const std::vector<uint8_t>& candidate,
                     std::string* error) {
  VisualLayout before, after;
  if (!parse_visual_layout(clean, &before, error) || !parse_visual_layout(candidate, &after, error))
    return false;
  if (clean.size() != candidate.size() || before.data_size != after.data_size ||
      before.relocations != after.relocations || before.images != after.images ||
      before.palettes != after.palettes ||
      before.relocation_start != after.relocation_start ||
      std::memcmp(clean.data() + before.relocation_start, candidate.data() + after.relocation_start,
                  clean.size() - before.relocation_start)) {
    *error = "Visual DAT changes layout, relocation, root, string, or GX descriptor structure.";
    return false;
  }
  std::vector<uint8_t> allowed(before.data_size, 0);
  for (const auto& pair : before.images)
    std::fill(allowed.begin() + pair.second.target,
              allowed.begin() + pair.second.target + pair.second.size, 1);
  for (const auto& pair : before.palettes)
    std::fill(allowed.begin() + pair.second.target,
              allowed.begin() + pair.second.target + pair.second.size, 1);
  size_t changed = 0;
  for (uint32_t offset = 0; offset < before.data_size; ++offset) {
    if (clean[0x20ull + offset] == candidate[0x20ull + offset]) continue;
    ++changed;
    if (!allowed[offset]) {
      std::ostringstream message;
      message << "Visual DAT changes non-texture data at data offset 0x" << std::hex << offset << ".";
      *error = message.str(); return false;
    }
  }
  if (!changed) { *error = "Visual DAT is identical to the clean resource."; return false; }
  return true;
}

constexpr std::array<std::array<uint16_t, 23>, 3> kParticleSignatures{{
    {{0x0323, 0x0521, 0x0523, 0x0525, 0x0523, 0x0526, 0x0522, 0x0520,
      0x0522, 0x051f, 0x0521, 0x0524, 0x0525, 0x0527, 0x0526, 0x0527,
      0x0520, 0x051e, 0x051f, 0x051e, 0x0524, 0x0527, 0x0500}},
    {{0x1516, 0x151a, 0x1515, 0x151a, 0x151b, 0x151a, 0x151d, 0x1514,
      0x1517, 0x1516, 0x1518, 0x1515, 0x1519, 0x151b, 0x1519, 0x151c,
      0x1518, 0x151c, 0x1517, 0x151c, 0x151d, 0x151b, 0x1500}},
    {{0x2f0b, 0x2f10, 0x2f0d, 0x2f10, 0x2f11, 0x2f10, 0x2f13, 0x2f0a,
      0x2f0c, 0x2f0b, 0x2f0e, 0x2f0d, 0x2f0f, 0x2f11, 0x2f0f, 0x2f12,
      0x2f0e, 0x2f12, 0x2f0c, 0x2f12, 0x2f13, 0x2f11, 0x2f00}},
}};
constexpr std::array<uint8_t, 6> kFoxSideColor{{0x00, 0x99, 0xff, 0xff, 0xcc, 0xe6}};

bool exact_root(const VisualLayout& layout, const char* expected, const char* label,
                std::string* error) {
  if (layout.roots.size() == 1 && layout.roots[0] == expected) return true;
  *error = std::string(label) + " DAT root is not exactly " + expected + ".";
  return false;
}

bool find_particle_stream(const std::vector<uint8_t>& bytes, const VisualLayout& layout,
                          const std::array<uint16_t, 23>& signature, const char* label,
                          uint32_t* result, std::string* error) {
  size_t matches = 0;
  constexpr size_t stream_size = 23 * 4;
  for (uint32_t offset = 0; (uint64_t)offset + stream_size <= layout.data_size; offset += 4) {
    bool match = true;
    for (size_t index = 0; index < signature.size(); ++index) {
      if (be16(bytes.data() + 0x20ull + offset + index * 4 + 2) != signature[index]) {
        match = false; break;
      }
    }
    if (match) { *result = offset; ++matches; }
  }
  if (matches == 1) return true;
  *error = std::string(label) + " particle-color stream must occur exactly once; found " +
           std::to_string(matches) + ".";
  return false;
}

bool uniform_stream_color(const std::vector<uint8_t>& bytes, uint32_t offset,
                          const char* label, uint16_t* color, std::string* error) {
  *color = be16(bytes.data() + 0x20ull + offset);
  for (size_t index = 1; index < 23; ++index) {
    if (be16(bytes.data() + 0x20ull + offset + index * 4) != *color) {
      *error = std::string(label) + " particle-color stream is not uniform."; return false;
    }
  }
  return true;
}

bool materialize_effect_dat_impl(const std::string& target_path,
                                 const std::vector<uint8_t>& clean,
                                 const std::vector<uint8_t>& candidate,
                                 std::vector<uint8_t>* runtime,
                                 std::string* classification,
                                 std::string* error) {
  runtime->clear(); classification->clear();
  const std::string target = lower(target_path);
  if (target != "effxdata.dat" && target != "efcodata.dat" &&
      target != "plfx.dat" && target != "plfc.dat") {
    *error = "Unsupported effect target: " + target_path + ".";
    return false;
  }
  std::string texture_error;
  if (visual_dat_only(clean, candidate, &texture_error)) {
    *runtime = candidate;
    *classification = "Texture-only effect validated against the exact clean ISO.";
    return true;
  }

  VisualLayout base, mod;
  if (!parse_visual_layout(clean, &base, error) || !parse_visual_layout(candidate, &mod, error))
    return false;
  if (target == "efcodata.dat") {
    *error = "Common effect DAT changes bytes outside validated texture payloads.";
    return false;
  }
  const char* dedicated_root = target == "effxdata.dat" ? "effFoxDataTable" : nullptr;
  if (dedicated_root) {
    if (!exact_root(base, dedicated_root, "Clean", error) ||
        !exact_root(mod, dedicated_root, "Candidate", error)) return false;
    if (clean == candidate) { *error = "Effect DAT is identical to the clean resource."; return false; }
    *runtime = candidate;
    *classification = "Dedicated effect archive validated against the exact clean ISO.";
    return true;
  }

  const char* fighter_root = target == "plfx.dat" ? "ftDataFox" :
                             target == "plfc.dat" ? "ftDataFalco" : nullptr;
  if (!fighter_root) { *error = "Unsupported effect target: " + target_path + "."; return false; }
  if (!exact_root(base, fighter_root, "Clean", error) ||
      !exact_root(mod, fighter_root, "Candidate", error)) return false;

  *runtime = clean;
  std::array<uint32_t, 3> clean_offsets{}, candidate_offsets{};
  std::array<uint16_t, 3> candidate_colors{};
  for (size_t stream = 0; stream < kParticleSignatures.size(); ++stream) {
    std::string clean_label = "Clean #" + std::to_string(stream + 1);
    std::string candidate_label = "Candidate #" + std::to_string(stream + 1);
    if (!find_particle_stream(clean, base, kParticleSignatures[stream], clean_label.c_str(),
                              &clean_offsets[stream], error) ||
        !find_particle_stream(candidate, mod, kParticleSignatures[stream], candidate_label.c_str(),
                              &candidate_offsets[stream], error)) return false;
    uint16_t clean_color = 0;
    if (!uniform_stream_color(clean, clean_offsets[stream], clean_label.c_str(),
                              &clean_color, error) ||
        !uniform_stream_color(candidate, candidate_offsets[stream], candidate_label.c_str(),
                              &candidate_colors[stream], error)) return false;
    if (clean_color != 0xfc00) {
      *error = "Clean fighter DAT does not match the exact NTSC 1.02 color stream."; return false;
    }
    for (size_t record = 0; record < 23; ++record) {
      size_t source = 0x20ull + candidate_offsets[stream] + record * 4;
      size_t destination = 0x20ull + clean_offsets[stream] + record * 4;
      (*runtime)[destination] = candidate[source];
      (*runtime)[destination + 1] = candidate[source + 1];
    }
  }
  if (candidate_colors[0] == 0xfc00 || candidate_colors[0] != candidate_colors[1] ||
      candidate_colors[0] != candidate_colors[2]) {
    *error = "Candidate particle-color streams do not contain one coherent new color."; return false;
  }

  if (target == "plfx.dat") {
    if (clean.size() != candidate.size() || base.data_size != mod.data_size ||
        base.relocations != mod.relocations || base.roots != mod.roots ||
        base.relocation_start != mod.relocation_start ||
        std::memcmp(clean.data() + base.relocation_start,
                    candidate.data() + mod.relocation_start,
                    clean.size() - base.relocation_start)) {
      *error = "Fox fighter effect DAT changes layout, relocation, roots, or strings."; return false;
    }
    auto begin = clean.begin() + 0x20;
    auto end = begin + base.data_size;
    auto side = std::search(begin, end, kFoxSideColor.begin(), kFoxSideColor.end());
    if (side == end || std::search(side + 1, end, kFoxSideColor.begin(), kFoxSideColor.end()) != end) {
      *error = "Clean Fox side-B color field is not unique."; return false;
    }
    size_t offset = (size_t)std::distance(clean.begin(), side);
    std::copy(candidate.begin() + offset, candidate.begin() + offset + kFoxSideColor.size(),
              runtime->begin() + offset);
    if (*runtime != candidate) {
      *error = "Fox fighter DAT changes bytes outside verified particle-color fields."; return false;
    }
  }
  if (*runtime == clean) { *error = "Effect DAT does not change a verified particle color."; return false; }
  *classification = "Effect colors merged into the exact clean fighter archive.";
  return true;
}

std::string companion_label(const ZipEntry& entry) {
  std::string name = lower(entry.name);
  if (lower(fs::path(name).extension().string()) != ".png") return {};
  if (name.find("stock") != std::string::npos) return entry.name + " (stock icon: native texture override)";
  if (name.find("csp") != std::string::npos || name.find("portrait") != std::string::npos)
    return entry.name + " (portrait: native texture override)";
  return {};
}

std::string display_name(const fs::path& source, const std::string& member) {
  fs::path candidate = member.empty() ? source : fs::u8path(member);
  std::string name = path_filename_utf8(candidate.stem());
  std::string member_stem = lower(name);
  if (!member.empty() && member_stem.size() >= 7 && member_stem.rfind("pl", 0) == 0 &&
      member_stem.substr(member_stem.size() - 3) == "mod")
    name = path_filename_utf8(source.stem());
  if (name.empty()) name = path_filename_utf8(source.stem());
  return name.empty() ? "Imported costume" : name;
}

struct VaultCompanionPlan {
  std::string kind;
  std::string source_member;
  std::vector<uint8_t> bytes;
};
struct VaultVariantPlan {
  std::string source_id;
  std::string display_name;
  std::string archive_member;
  std::string dat_member;
  testing::DatInspection dat;
  std::vector<uint8_t> bytes;
  std::vector<VaultCompanionPlan> companions;
  std::vector<std::string> companion_notices;
  std::vector<std::string> dependencies;
};
struct VaultResourcePlan {
  std::string source_id;
  std::string display_name;
  std::string kind;         // stage_visual or effect_visual
  std::string target_path;  // existing clean ISO DAT
  std::string group;        // stage name or character/global group
  std::string slot;         // UI slot label
  std::string source_member;
  std::vector<uint8_t> bytes;
  std::vector<VaultCompanionPlan> companions;
};
struct VaultPlan {
  std::vector<VaultVariantPlan> variants;
  std::vector<VaultResourcePlan> resources;
  std::vector<std::string> rejected_resources;
  size_t stage_records = 0;
  size_t effect_records = 0;
};

bool metadata_text(const json& object, const char* key, size_t limit, std::string* value,
                   std::string* error) {
  auto found = object.find(key);
  if (found == object.end() || !found->is_string()) {
    *error = std::string("Nucleus metadata is missing string field ") + key + "."; return false;
  }
  *value = found->get<std::string>();
  if (value->empty() || value->size() > limit ||
      std::any_of(value->begin(), value->end(), [](char c) { return (unsigned char)c < 0x20; })) {
    *error = std::string("Nucleus metadata field ") + key + " is empty or outside supported bounds.";
    return false;
  }
  return true;
}

bool png_companion(const std::vector<uint8_t>& bytes, std::string* error) {
  static constexpr uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  if (bytes.size() < 24 || bytes.size() > 16ull * 1024 * 1024 ||
      std::memcmp(bytes.data(), signature, sizeof signature) ||
      std::memcmp(bytes.data() + 12, "IHDR", 4)) {
    *error = "A promised companion is not a bounded identifiable PNG."; return false;
  }
  uint32_t width = be32(bytes.data() + 16), height = be32(bytes.data() + 20);
  if (!width || !height || width > 8192 || height > 8192) {
    *error = "A companion PNG has unsupported dimensions."; return false;
  }
  return true;
}

const ZipEntry* unique_basename(const std::vector<ZipEntry>& entries, const std::string& name,
                                std::string* error) {
  const ZipEntry* match = nullptr;
  std::string wanted = lower(name);
  for (const auto& entry : entries) {
    std::string base = entry.name.substr(entry.name.find_last_of('/') == std::string::npos ? 0 :
                                         entry.name.find_last_of('/') + 1);
    if (lower(base) != wanted) continue;
    if (match) { *error = "A Nucleus filename is ambiguous in the vault: " + name; return nullptr; }
    match = &entry;
  }
  if (!match) *error = "A Nucleus metadata file is missing from the vault: " + name;
  return match;
}

bool inspect_nested_costume(const std::vector<uint8_t>& archive_bytes,
                            testing::DatInspection* dat, std::vector<uint8_t>* dat_bytes,
                            std::string* member, std::string* error) {
  static std::atomic<uint32_t> sequence{0};
  fs::path temporary = g_root / L".staging" /
      (L"nucleus-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
       std::to_wstring(sequence.fetch_add(1)) + L".zip");
  if (!write_atomic(temporary, archive_bytes.data(), archive_bytes.size(), error)) return false;
  std::vector<ZipEntry> entries;
  bool parsed = parse_zip(temporary, &entries, error);
  if (!parsed) { DeleteFileW(temporary.c_str()); return false; }
  size_t recognized = 0;
  for (const auto& entry : entries) {
    if (lower(fs::path(entry.name).extension().string()) != ".dat") continue;
    std::vector<uint8_t> candidate;
    if (!extract_zip_member(temporary, entry, &candidate, error)) {
      DeleteFileW(temporary.c_str()); return false;
    }
    auto inspected = inspect_dat_impl(candidate);
    if (!inspected.ok) continue;
    ++recognized; *dat = std::move(inspected); *dat_bytes = std::move(candidate); *member = entry.name;
  }
  DeleteFileW(temporary.c_str());
  if (recognized != 1) {
    *error = "Each Nucleus character archive must contain exactly one supported existing costume DAT; found " +
             std::to_string(recognized) + ".";
    return false;
  }
  return true;
}

bool inspect_nested_visual(const std::vector<uint8_t>& archive_bytes,
                           std::vector<uint8_t>* dat_bytes, std::string* member,
                           std::string* error) {
  static std::atomic<uint32_t> sequence{0};
  fs::path temporary = g_root / L".staging" /
      (L"nucleus-visual-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
       std::to_wstring(sequence.fetch_add(1)) + L".zip");
  if (!write_atomic(temporary, archive_bytes.data(), archive_bytes.size(), error)) return false;
  std::vector<ZipEntry> entries;
  if (!parse_zip(temporary, &entries, error)) { DeleteFileW(temporary.c_str()); return false; }
  size_t recognized = 0;
  for (const auto& entry : entries) {
    if (lower(fs::path(entry.name).extension().string()) != ".dat") continue;
    std::vector<uint8_t> candidate;
    if (!extract_zip_member(temporary, entry, &candidate, error)) {
      DeleteFileW(temporary.c_str()); return false;
    }
    VisualLayout layout;
    if (!parse_visual_layout(candidate, &layout, error)) continue;
    ++recognized; *dat_bytes = std::move(candidate); *member = entry.name;
  }
  DeleteFileW(temporary.c_str());
  if (recognized != 1) {
    *error = "Each Nucleus stage archive must contain exactly one structurally valid visual DAT; found " +
             std::to_string(recognized) + ".";
    return false;
  }
  return true;
}

bool stage_target(const std::string& id, std::string* path, std::string* display) {
  static constexpr struct { const char* id; const char* path; const char* display; } stages[] = {
      {"battlefield", "GrNBa.dat", "Battlefield"},
      {"dreamland", "GrOp.dat", "Dream Land"},
      {"final_destination", "GrNLa.dat", "Final Destination"},
      {"fountain_of_dreams", "GrIz.dat", "Fountain of Dreams"},
      {"poke_floats", "GrPu.dat", "Poké Floats"},
      {"pokemon_stadium", "GrPs.dat", "Pokémon Stadium"},
      {"yoshis_story", "GrSt.dat", "Yoshi's Story"},
  };
  for (const auto& stage : stages) if (lower(id) == stage.id) {
    *path = stage.path; *display = stage.display; return true;
  }
  return false;
}

std::string effect_scope(const AssetRecord& asset) {
  if (asset.info.kind != "effect_visual") return {};
  const std::string& id = asset.source_id;
  if (id.rfind("effect:", 0) != 0) return lower(asset.info.costume);
  const size_t begin = id.find(':', 7);
  if (begin == std::string::npos) return lower(asset.info.costume);
  const size_t end = id.find(':', begin + 1);
  return lower(id.substr(begin + 1, end == std::string::npos ? end : end - begin - 1));
}

std::string effect_label(const std::string& scope) {
  if (scope == "upb") return "Fire Fox";
  if (scope == "shine") return "Reflector (Shine)";
  if (scope == "sideb") return "Fox Illusion";
  if (scope == "laser") return "Laser";
  if (scope == "common_shield") return "Shields";
  return scope;
}

std::string selection_key(const AssetRecord& asset) {
  return asset.info.target_path + (asset.info.kind == "effect_visual" ? "#" + effect_scope(asset) : "");
}

// A stage archive's metadata names the arena, but the DAT names the exact disc resource.
// Stadium's base and four transformations must stay separate, even when a ZIP contains all five.
bool stage_dat_target(const std::string& filename, std::string* path, std::string* display) {
  const std::string name = lower(filename.substr(filename.find_last_of("/\\") == std::string::npos ?
                                              0 : filename.find_last_of("/\\") + 1));
  if (name.size() < 7 || name.substr(name.size() - 4) != ".dat") return false;
  const std::string stem = name.substr(0, name.size() - 4);
  static constexpr struct { const char* token; const char* path; const char* label; } resources[] = {
      {"grps1", "GrPs1.dat", "Pokémon Stadium — Fire"},
      {"grps2", "GrPs2.dat", "Pokémon Stadium — Water"},
      {"grps3", "GrPs3.dat", "Pokémon Stadium — Rock"},
      {"grps4", "GrPs4.dat", "Pokémon Stadium — Grass"},
      {"grps", "GrPs.dat", "Pokémon Stadium — Base"},
      {"grop", "GrOp.dat", "Dream Land"},
      {"grnba", "GrNBa.dat", "Battlefield"},
      {"grnla", "GrNLa.dat", "Final Destination"},
      {"griz", "GrIz.dat", "Fountain of Dreams"},
      {"grst", "GrSt.dat", "Yoshi's Story"},
      {"grpu", "GrPu.dat", "Poké Floats"},
  };
  size_t match_index = std::size(resources);
  for (size_t i = 0; i < std::size(resources); ++i) {
    const auto& resource = resources[i];
    const std::string token = resource.token;
    size_t pos = stem.find(token);
    if (pos == std::string::npos) continue;
    const size_t end = pos + token.size();
    if ((pos && std::isalnum((unsigned char)stem[pos - 1])) ||
        (end < stem.size() && std::isalnum((unsigned char)stem[end]))) continue;
    if (match_index != std::size(resources)) return false; // never guess an ambiguous disc file
    match_index = i;
  }
  if (match_index == std::size(resources)) return false;
  *path = resources[match_index].path; *display = resources[match_index].label;
  return true;
}

bool effect_target(const std::string& character, const std::string& scope,
                   std::string* path, std::string* group, std::string* slot) {
  const std::string who = lower(character), what = lower(scope);
  *group = character;
  if (who == "fox" && (what == "upb" || what == "shine")) {
    *path = "EfFxData.dat"; *slot = effect_label(what); return true;
  }
  if (who == "fox" && (what == "laser" || what == "sideb")) {
    *path = "PlFx.dat"; *slot = effect_label(what); return true;
  }
  if (who == "falco" && what == "laser") {
    *path = "PlFc.dat"; *slot = "Laser effects"; return true;
  }
  if (what == "common_shield") {
    *path = "EfCoData.dat"; *group = "Global"; *slot = "Shields"; return true;
  }
  return false;
}

bool parse_nucleus_vault(const fs::path& path, const std::vector<ZipEntry>& entries,
                         const ZipEntry& metadata_entry, VaultPlan* plan, std::string* error) {
  plan->variants.clear(); plan->resources.clear(); plan->rejected_resources.clear();
  plan->stage_records = plan->effect_records = 0;
  if (metadata_entry.uncompressed > 16ull * 1024 * 1024) {
    *error = "Nucleus metadata.json exceeds the 16 MB safety limit."; return false;
  }
  std::vector<uint8_t> metadata_bytes;
  if (!extract_zip_member(path, metadata_entry, &metadata_bytes, error)) return false;
  json metadata;
  try { metadata = json::parse(metadata_bytes.begin(), metadata_bytes.end()); }
  catch (...) { *error = "Nucleus metadata.json is not valid JSON."; return false; }
  auto characters = metadata.find("characters");
  if (!metadata.is_object() || characters == metadata.end() || !characters->is_object()) {
    *error = "Nucleus metadata.json does not contain a character catalog."; return false;
  }
  std::map<std::string, bool> source_ids;
  for (auto group = characters->begin(); group != characters->end(); ++group) {
    if (!group.value().is_object()) { *error = "A Nucleus character group is malformed."; return false; }
    auto skins = group.value().find("skins");
    if (skins != group.value().end() && !skins->is_array()) {
      *error = "A Nucleus character skin list is malformed."; return false;
    }
    if (skins != group.value().end()) for (const auto& skin : *skins) {
      if (!skin.is_object()) { *error = "A Nucleus skin record is malformed."; return false; }
      VaultVariantPlan variant;
      std::string filename, costume_code;
      if (!metadata_text(skin, "id", 256, &variant.source_id, error) ||
          !metadata_text(skin, "color", 256, &variant.display_name, error) ||
          !metadata_text(skin, "filename", 512, &filename, error) ||
          !metadata_text(skin, "costume_code", 16, &costume_code, error)) return false;
      if (!source_ids.emplace(lower(variant.source_id), true).second) {
        *error = "Nucleus metadata contains a duplicate skin id: " + variant.source_id; return false;
      }
      for (const char* dependency_key : {"paired_nana_id", "paired_popo_id"}) {
        auto dependency = skin.find(dependency_key);
        if (dependency == skin.end()) continue;
        std::string dependency_id;
        if (!metadata_text(skin, dependency_key, 256, &dependency_id, error)) return false;
        variant.dependencies.push_back(std::move(dependency_id));
      }
      if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos ||
          lower(fs::path(filename).extension().string()) != ".zip") {
        *error = "A Nucleus skin filename is not a safe ZIP basename: " + filename; return false;
      }
      const ZipEntry* archive = unique_basename(entries, filename, error);
      if (!archive) return false;
      variant.archive_member = archive->name;
      std::vector<uint8_t> archive_bytes;
      if (!extract_zip_member(path, *archive, &archive_bytes, error) ||
          !inspect_nested_costume(archive_bytes, &variant.dat, &variant.bytes,
                                  &variant.dat_member, error)) return false;
      if (lower(variant.dat.target_path) != lower(costume_code + ".dat")) {
        *error = "Nucleus metadata target " + costume_code +
                 " conflicts with validated DAT target " + variant.dat.target_path + ".";
        return false;
      }
      size_t slash = archive->name.find_last_of('/');
      std::string parent = slash == std::string::npos ? "" : archive->name.substr(0, slash + 1);
      std::string stem = filename.substr(0, filename.size() - 4);
      for (const auto& spec : {std::pair<const char*, const char*>{"csp", "_csp.png"},
                               {"stock", "_stc.png"}}) {
        bool promised = skin.value(std::string("has_") + spec.first, false);
        std::string wanted = lower(parent + stem + spec.second);
        auto companion = std::find_if(entries.begin(), entries.end(), [&](const ZipEntry& item) {
          return lower(item.name) == wanted;
        });
        if (promised && companion == entries.end()) {
          variant.companion_notices.push_back(
              (std::string(spec.first) == "csp" ? "CSP" : "Stock icon") +
              std::string(" missing; vanilla UI fallback will be used."));
          continue;
        }
        if (companion != entries.end()) {
          VaultCompanionPlan planned{spec.first, companion->name, {}};
          if (!extract_zip_member(path, *companion, &planned.bytes, error) ||
              !png_companion(planned.bytes, error)) return false;
          variant.companions.push_back(std::move(planned));
        }
      }
      plan->variants.push_back(std::move(variant));
    }
    auto extras = group.value().find("extras");
    if (extras != group.value().end()) {
      if (!extras->is_object()) { *error = "A Nucleus effects catalog is malformed."; return false; }
      for (auto scope = extras->begin(); scope != extras->end(); ++scope) {
        if (!scope.value().is_array()) { *error = "A Nucleus effect list is malformed."; return false; }
        plan->effect_records += scope.value().size();
        for (const auto& effect : scope.value()) {
          if (!effect.is_object()) { *error = "A Nucleus effect record is malformed."; return false; }
          VaultResourcePlan resource;
          std::string effect_id, model_file;
          std::string record_error;
          if (!metadata_text(effect, "id", 256, &effect_id, &record_error)) {
            plan->rejected_resources.push_back("effect:" + group.key() + ":" + scope.key() +
                                               " has incomplete metadata.");
            continue;
          }
          resource.source_id = "effect:" + group.key() + ":" + scope.key() + ":" + effect_id;
          resource.kind = "effect_visual";
          if (!metadata_text(effect, "name", 256, &resource.display_name, &record_error) ||
              !metadata_text(effect, "model_file", 512, &model_file, &record_error)) {
            plan->rejected_resources.push_back(resource.source_id + " has incomplete metadata.");
            continue;
          }
          if (!effect_target(group.key(), scope.key(), &resource.target_path,
                             &resource.group, &resource.slot)) {
            plan->rejected_resources.push_back(resource.source_id + " has an unsupported effect scope.");
            continue;
          }
          const ZipEntry* model = nullptr;
          const std::string suffix = "/" + lower(model_file);
          for (const auto& entry : entries) {
            std::string candidate = lower(entry.name);
            if (candidate.size() < suffix.size() ||
                candidate.compare(candidate.size() - suffix.size(), suffix.size(), suffix)) continue;
            if (model) { model = nullptr; break; }
            model = &entry;
          }
          if (!model) {
            plan->rejected_resources.push_back(resource.source_id + " has a missing or ambiguous model file.");
            continue;
          }
          resource.source_member = model->name;
          if (!extract_zip_member(path, *model, &resource.bytes, &record_error)) {
            plan->rejected_resources.push_back(resource.source_id + " rejected: " + record_error);
            continue;
          }
          VisualLayout layout;
          std::string visual_error;
          if (!parse_visual_layout(resource.bytes, &layout, &visual_error)) {
            plan->rejected_resources.push_back(resource.source_id + " rejected: " + visual_error);
            continue;
          }
          plan->resources.push_back(std::move(resource));
        }
      }
    }
  }
  for (const auto& variant : plan->variants) for (const auto& dependency : variant.dependencies) {
    if (source_ids.find(lower(dependency)) == source_ids.end()) {
      *error = "Nucleus skin " + variant.source_id + " references missing dependency " + dependency + ".";
      return false;
    }
  }
  auto stages = metadata.find("stages");
  if (stages != metadata.end()) {
    if (!stages->is_object()) { *error = "The Nucleus stage catalog is malformed."; return false; }
    for (auto stage = stages->begin(); stage != stages->end(); ++stage) {
      if (!stage.value().is_object()) { *error = "A Nucleus stage group is malformed."; return false; }
      auto variants = stage.value().find("variants");
      if (variants != stage.value().end()) {
        if (!variants->is_array()) { *error = "A Nucleus stage variant list is malformed."; return false; }
        plan->stage_records += variants->size();
        for (const auto& variant : *variants) {
          if (!variant.is_object()) { *error = "A Nucleus stage record is malformed."; return false; }
          VaultResourcePlan resource;
          std::string variant_id, filename;
          std::string record_error;
          if (!metadata_text(variant, "id", 256, &variant_id, &record_error)) {
            plan->rejected_resources.push_back("stage:" + stage.key() +
                                               " has incomplete metadata.");
            continue;
          }
          resource.source_id = "stage:" + stage.key() + ":" + variant_id;
          resource.kind = "stage_visual";
          if (!metadata_text(variant, "name", 256, &resource.display_name, &record_error)) {
            plan->rejected_resources.push_back(resource.source_id + " has incomplete metadata.");
            continue;
          }
          if (!stage_target(stage.key(), &resource.target_path, &resource.slot)) {
            plan->rejected_resources.push_back(resource.source_id + " targets an unsupported stage.");
            continue;
          }
          resource.group = "Stages";
          auto filename_value = variant.find("filename");
          if (filename_value == variant.end() || !filename_value->is_string() ||
              filename_value->get<std::string>().empty()) {
            plan->rejected_resources.push_back(resource.source_id + " contains metadata only.");
            continue;
          }
          filename = filename_value->get<std::string>();
          if (filename.size() > 512 || filename.find('/') != std::string::npos ||
              filename.find('\\') != std::string::npos ||
              lower(fs::path(filename).extension().string()) != ".zip") {
            plan->rejected_resources.push_back(resource.source_id + " has an unsafe stage ZIP name.");
            continue;
          }
          const std::string wanted = lower("das/" + stage.key() + "/" + filename);
          auto archive = std::find_if(entries.begin(), entries.end(), [&](const ZipEntry& entry) {
            return lower(entry.name) == wanted;
          });
          if (archive == entries.end()) {
            plan->rejected_resources.push_back(resource.source_id + " is missing its stage ZIP.");
            continue;
          }
          std::vector<uint8_t> archive_bytes;
          if (!extract_zip_member(path, *archive, &archive_bytes, &record_error)) {
            plan->rejected_resources.push_back(resource.source_id + " rejected: " + record_error);
            continue;
          }
          std::string visual_error, dat_member;
          if (!inspect_nested_visual(archive_bytes, &resource.bytes, &dat_member, &visual_error)) {
            plan->rejected_resources.push_back(resource.source_id + " rejected: " + visual_error);
            continue;
          }
          if (lower(stage.key()) == "pokemon_stadium") {
            std::string actual, actual_slot;
            if (!stage_dat_target(dat_member, &actual, &actual_slot) ||
                (actual != "GrPs.dat" && actual != "GrPs1.dat" && actual != "GrPs2.dat" &&
                 actual != "GrPs3.dat" && actual != "GrPs4.dat")) {
              plan->rejected_resources.push_back(resource.source_id +
                                                 " has no identifiable Pokémon Stadium disc DAT.");
              continue;
            }
            resource.target_path = actual;
            resource.slot = actual_slot;
          }
          resource.source_member = archive->name + "::" + dat_member;
          plan->resources.push_back(std::move(resource));
        }
      }
    }
  }
  if (plan->variants.empty() && plan->resources.empty()) {
    *error = "The Nucleus project contains no supported cosmetic resources."; return false;
  }
  return true;
}

std::vector<uint8_t> load_runtime_asset_locked(const AssetRecord& asset, std::string* error);

ImportResult install_asset_locked(const fs::path& source, const std::string& source_kind,
                                  const std::string& member, std::vector<uint8_t> bytes,
                                  const testing::DatInspection& dat,
                                  std::vector<VaultCompanionPlan> companions) {
  ImportResult result;
  std::string error;
  if (!mutable_profile_locked(&error)) { result.message = error; return result; }
  std::string digest = sha256(bytes);
  if (digest.empty()) { result.message = "SHA-256 validation could not be initialized."; return result; }
  // Use the full digest in the storage identity. Truncating it would make a rare prefix collision
  // overwrite a different immutable asset even though duplicate detection compares the full hash.
  std::string id = "costume-" + digest;
  auto install_companions = [&](AssetRecord* asset) -> bool {
    if (companions.empty()) return true;
    std::vector<AssetRecord::Companion> records;
    std::vector<std::string> notices;
    for (const auto& companion : companions) {
      std::string companion_digest = sha256(companion.bytes);
      if (companion_digest.empty()) { error = "A companion PNG could not be hashed."; return false; }
      const std::string filename = companion.kind == "csp" ? "csp.png" : "stock.png";
      AssetRecord::Companion record;
      record.kind = companion.kind;
      record.stored_path = (fs::path(L"assets") / fs::u8path(asset->info.id) / L"companions" /
                            fs::u8path(filename)).generic_u8string();
      record.sha256 = companion_digest;
      record.source_member = companion.source_member;
      if (!write_atomic(g_root / fs::u8path(record.stored_path), companion.bytes.data(),
                        companion.bytes.size(), &error)) return false;
      notices.push_back((companion.kind == "csp" ? "CSP" : "Stock icon") +
                        std::string(" mapped to the native selector texture with vanilla fallback: ") +
                        companion.source_member);
      records.push_back(std::move(record));
    }
    asset->companions = std::move(records);
    asset->info.unsupported_companions = std::move(notices);
    return true;
  };
  auto duplicate = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& a) {
    return a.info.sha256 == digest && a.info.target_path == dat.target_path;
  });
  if (duplicate != g_assets.end()) {
    std::string validation_error;
    if (load_runtime_asset_locked(*duplicate, &validation_error).empty()) {
      fs::path destination = g_root / fs::u8path(duplicate->stored_path);
      if (!write_atomic(destination, bytes.data(), bytes.size(), &error)) {
        result.message = error; return result;
      }
    }
    duplicate->info.available = true;
    duplicate->info.availability_message.clear();
    AssetRecord previous = *duplicate;
    if (!install_companions(&*duplicate) || !save_catalog_locked(&error)) {
      *duplicate = std::move(previous); result.message = error; return result;
    }
    size_t variants = (size_t)std::count_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& a) {
      return a.info.target_path == dat.target_path;
    });
    auto current = g_profile.selections.find(dat.target_path);
    bool has_explicit_choice = current != g_profile.selections.end();
    bool selected = has_explicit_choice && current->second == duplicate->info.id;
    if (!has_explicit_choice && variants == 1) {
      Profile previous = g_profile;
      g_profile.selections[dat.target_path] = duplicate->info.id;
      ++g_profile.generation;
      if (!save_profile_locked(&error)) {
        g_profile = std::move(previous); result.message = error; return result;
      }
      selected = true; has_explicit_choice = true;
    }
    g_message = duplicate->info.name + " refreshed for " + dat.target_path +
                (selected ? "." : "; the current selection was preserved.");
    result.ok = true; result.already_present = true; result.asset_id = duplicate->info.id;
    result.message = g_message; return result;
  }
  fs::path relative = fs::path(L"assets") / fs::u8path(id) / fs::u8path(dat.target_path);
  fs::path destination = g_root / relative;
  if (!write_atomic(destination, bytes.data(), bytes.size(), &error)) { result.message = error; return result; }

  AssetRecord asset;
  asset.info.id = id;
  asset.info.name = display_name(source, member);
  asset.info.kind = "character_costume";
  asset.info.target_path = dat.target_path;
  asset.info.character = dat.character;
  asset.info.costume = dat.costume;
  asset.info.sha256 = digest;
  asset.info.roots = dat.roots;
  asset.info.available = true;
  asset.stored_path = relative.generic_u8string();
  asset.source_kind = source_kind;
  asset.source_name = path_filename_utf8(source);
  asset.source_member = member;
  if (!install_companions(&asset)) { DeleteFileW(destination.c_str()); result.message = error; return result; }
  size_t existing_variants = (size_t)std::count_if(
      g_assets.begin(), g_assets.end(), [&](const AssetRecord& a) {
        return a.info.target_path == dat.target_path;
      });
  g_assets.push_back(asset);
  if (!save_catalog_locked(&error)) {
    g_assets.pop_back(); DeleteFileW(destination.c_str()); result.message = error; return result;
  }
  bool auto_selected = existing_variants == 0 &&
                       g_profile.selections.find(dat.target_path) == g_profile.selections.end();
  if (auto_selected) {
    Profile previous = g_profile;
    g_profile.selections[dat.target_path] = id;
    ++g_profile.generation;
    if (!save_profile_locked(&error)) {
      g_profile = std::move(previous);
      result.message = "The asset was cataloged, but selection could not be saved: " + error; return result;
    }
  }
  g_message = asset.info.name + " imported as " + dat.character + " — " + dat.costume +
              " (" + dat.target_path + ")" +
              (auto_selected ? " and automatically selected." :
                               "; the existing selection was preserved.");
  result.ok = true; result.asset_id = id; result.message = g_message;
  return result;
}

bool materialize_effect_with_open_disc(const std::string& target_path,
                                       const std::vector<uint8_t>& candidate,
                                       std::vector<uint8_t>* runtime,
                                       std::string* classification,
                                       std::string* error) {
  uint32_t offset = 0, size = 0;
  if (!host::disc_find_file(target_path, &offset, &size)) {
    *error = "The exact clean ISO resource is unavailable; validation is deferred until launch.";
    return false;
  }
  if (!size || size > kMaxAssetBytes) {
    *error = "The clean ISO resource is outside supported bounds."; return false;
  }
  std::vector<uint8_t> clean(size);
  if (!host::disc_read(offset, clean.data(), size)) {
    *error = "The clean ISO resource could not be read for visual-only validation."; return false;
  }
  return materialize_effect_dat_impl(target_path, clean, candidate, runtime, classification, error);
}

std::vector<const AssetRecord*> selected_effects_locked(const Profile& profile,
                                                         const std::string& target) {
  std::vector<const AssetRecord*> selected;
  for (const auto& pick : profile.selections) {
    if (pick.second == kVanillaSelection) continue;
    auto asset = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
      return item.info.id == pick.second && item.info.kind == "effect_visual" &&
             item.info.target_path == target && selection_key(item) == pick.first;
    });
    if (asset != g_assets.end()) selected.push_back(&*asset);
  }
  return selected;
}

bool compose_effects_locked(const std::string& target,
                            const std::vector<const AssetRecord*>& choices,
                            const std::vector<uint8_t>& clean,
                            std::vector<uint8_t>* combined, std::string* error) {
  *combined = clean;
  std::vector<const AssetRecord*> owners(clean.size(), nullptr);
  for (const AssetRecord* choice : choices) {
    std::vector<uint8_t> candidate = load_runtime_asset_locked(*choice, error);
    if (candidate.empty()) return false;
    std::vector<uint8_t> materialized;
    std::string classification;
    if (!materialize_effect_dat_impl(target, clean, candidate, &materialized,
                                     &classification, error)) return false;
    if (materialized.size() != clean.size()) {
      *error = "Effect materialization changed the clean resource size."; return false;
    }
    for (size_t offset = 0; offset < clean.size(); ++offset) {
      if (materialized[offset] == clean[offset]) continue;
      if (owners[offset] && (*combined)[offset] != materialized[offset]) {
        std::ostringstream message;
        message << "Conflict: " << owners[offset]->info.name << " and " << choice->info.name
                << " both alter " << target << " at 0x" << std::hex << offset << ".";
        *error = message.str(); return false;
      }
      (*combined)[offset] = materialized[offset];
      owners[offset] = choice;
    }
  }
  return true;
}

bool compose_effects_with_open_disc_locked(const std::string& target,
                                          const Profile& profile, std::string* error) {
  uint32_t offset = 0, size = 0;
  if (!host::disc_find_file(target, &offset, &size) || !size || size > kMaxAssetBytes) {
    *error = "The exact clean ISO effect resource is unavailable."; return false;
  }
  std::vector<uint8_t> clean(size), combined;
  if (!host::disc_read(offset, clean.data(), size)) {
    *error = "The clean ISO effect resource could not be read."; return false;
  }
  return compose_effects_locked(target, selected_effects_locked(profile, target),
                                clean, &combined, error);
}

ImportResult install_vault_locked(const fs::path& source, VaultPlan plan,
                                  const std::string& resource_source_kind = "nucleus_project") {
  ImportResult result;
  std::string error;
  if (!mutable_profile_locked(&error)) { result.message = error; return result; }
  std::vector<AssetRecord> next_assets = g_assets;
  Profile next_profile = g_profile;
  std::vector<fs::path> newly_created;
  std::map<std::string, bool> touched_targets;
  size_t added = 0, refreshed = 0;

  struct PendingWrite { fs::path path; const std::vector<uint8_t>* bytes; };
  std::vector<PendingWrite> writes;
  for (auto& variant : plan.variants) {
    std::string digest = sha256(variant.bytes);
    if (digest.empty()) { result.message = "SHA-256 validation could not be initialized."; return result; }
    std::string identity = sha256_text(variant.source_id + "\n" + variant.dat.target_path + "\n" + digest);
    if (identity.empty()) { result.message = "Nucleus identity hashing could not be initialized."; return result; }
    std::string id = "nucleus-" + identity;
    auto existing = std::find_if(next_assets.begin(), next_assets.end(), [&](const AssetRecord& item) {
      return item.source_kind == "nucleus_vault" && item.source_id == variant.source_id;
    });
    if (existing != next_assets.end()) {
      if (existing->info.sha256 != digest || existing->info.target_path != variant.dat.target_path) {
        result.message = "Nucleus skin id conflicts with an existing catalog asset: " + variant.source_id;
        return result;
      }
      writes.push_back({g_root / fs::u8path(existing->stored_path), &variant.bytes});
      ++refreshed; touched_targets[variant.dat.target_path] = true;
      if (result.asset_id.empty()) result.asset_id = existing->info.id;
      continue;
    }
    if (std::any_of(next_assets.begin(), next_assets.end(), [&](const AssetRecord& item) {
          return item.info.id == id;
        })) {
      result.message = "A generated Nucleus asset identity conflicts with the existing catalog.";
      return result;
    }
    AssetRecord asset;
    asset.info.id = id;
    asset.info.name = variant.display_name;
    if (std::any_of(next_assets.begin(), next_assets.end(), [&](const AssetRecord& item) {
          return item.info.target_path == variant.dat.target_path && item.info.name == asset.info.name;
        })) asset.info.name += " (" + variant.source_id + ")";
    asset.info.kind = "character_costume";
    asset.info.target_path = variant.dat.target_path;
    asset.info.character = variant.dat.character;
    asset.info.costume = variant.dat.costume;
    asset.info.sha256 = digest;
    asset.info.roots = variant.dat.roots;
    asset.info.dependencies = variant.dependencies;
    asset.info.unsupported_companions = variant.companion_notices;
    asset.info.available = true;
    asset.stored_path = (fs::path(L"assets") / fs::u8path(id) /
                         fs::u8path(variant.dat.target_path)).generic_u8string();
    asset.source_kind = "nucleus_vault";
    asset.source_name = path_filename_utf8(source);
    asset.source_member = variant.archive_member + "::" + variant.dat_member;
    asset.source_id = variant.source_id;
    writes.push_back({g_root / fs::u8path(asset.stored_path), &variant.bytes});
    for (auto& companion : variant.companions) {
      std::string companion_digest = sha256(companion.bytes);
      if (companion_digest.empty()) {
        result.message = "A Nucleus companion could not be hashed."; return result;
      }
      std::string filename = companion.kind == "csp" ? "csp.png" : "stock.png";
      AssetRecord::Companion record;
      record.kind = companion.kind;
      record.stored_path = (fs::path(L"assets") / fs::u8path(id) / L"companions" /
                            fs::u8path(filename)).generic_u8string();
      record.sha256 = companion_digest;
      record.source_member = companion.source_member;
      asset.info.unsupported_companions.push_back(
          (companion.kind == "csp" ? "CSP" : "Stock icon") +
          std::string(" mapped to the native selector texture with vanilla fallback: ") +
          companion.source_member);
      asset.companions.push_back(record);
      writes.push_back({g_root / fs::u8path(record.stored_path), &companion.bytes});
    }
    next_assets.push_back(std::move(asset));
    touched_targets[variant.dat.target_path] = true;
    if (result.asset_id.empty()) result.asset_id = id;
    ++added;
  }

  size_t resource_added = 0, resource_refreshed = 0;
  for (auto& resource : plan.resources) {
    std::string digest = sha256(resource.bytes);
    if (digest.empty()) { result.message = "A Nucleus resource could not be hashed."; return result; }
    std::string identity = sha256_text(resource.source_id + "\n" + resource.target_path + "\n" + digest);
    if (identity.empty()) { result.message = "A Nucleus resource identity could not be hashed."; return result; }
    std::string id = (resource_source_kind == "nucleus_project" ? "nucleus-resource-" : "stage-") + identity;
    auto existing = std::find_if(next_assets.begin(), next_assets.end(), [&](const AssetRecord& item) {
      return item.source_kind == resource_source_kind && item.source_id == resource.source_id;
    });
    if (existing != next_assets.end()) {
      if (existing->info.sha256 != digest || existing->info.target_path != resource.target_path ||
          existing->info.kind != resource.kind) {
        result.message = "Nucleus resource id conflicts with an existing catalog asset: " + resource.source_id;
        return result;
      }
      writes.push_back({g_root / fs::u8path(existing->stored_path), &resource.bytes});
      ++resource_refreshed;
    } else {
      AssetRecord asset;
      asset.info.id = id; asset.info.name = resource.display_name; asset.info.kind = resource.kind;
      asset.info.target_path = resource.target_path; asset.info.character = resource.group;
      asset.info.costume = resource.slot; asset.info.sha256 = digest; asset.info.available = true;
      asset.info.availability_message = resource.kind == "stage_visual" ?
          "Exact-ISO visual validation controls online use; full replacements are offline only." :
          "Validated against the exact clean ISO when selected or launched.";
      asset.stored_path = (fs::path(L"assets") / fs::u8path(id) /
                           fs::u8path(resource.target_path)).generic_u8string();
      asset.source_kind = resource_source_kind; asset.source_name = path_filename_utf8(source);
      asset.source_member = resource.source_member; asset.source_id = resource.source_id;
      writes.push_back({g_root / fs::u8path(asset.stored_path), &resource.bytes});
      next_assets.push_back(std::move(asset));
      ++resource_added;
    }
    auto recorded = std::find_if(next_assets.begin(), next_assets.end(),
                                 [&](const AssetRecord& item) { return item.info.id == id; });
    for (auto& companion : resource.companions) {
      AssetRecord::Companion record;
      record.kind = companion.kind;
      record.sha256 = sha256(companion.bytes);
      record.source_member = companion.source_member;
      record.stored_path = (fs::path(L"assets") / fs::u8path(id) / L"companions" /
                            fs::u8path(companion.kind + ".png")).generic_u8string();
      auto previous = std::find_if(recorded->companions.begin(), recorded->companions.end(),
                                   [&](const AssetRecord::Companion& item) {
                                     return item.kind == record.kind;
                                   });
      if (previous == recorded->companions.end()) recorded->companions.push_back(record);
      else *previous = record;
      writes.push_back({g_root / fs::u8path(record.stored_path), &companion.bytes});
    }
    std::string visual_error, classification;
    std::vector<uint8_t> effect_runtime;
    bool validated_now = resource.kind == "stage_visual" ||
        materialize_effect_with_open_disc(resource.target_path, resource.bytes, &effect_runtime,
                                          &classification, &visual_error);
    const std::string key = selection_key(*recorded);
    touched_targets[key] = touched_targets[key] || validated_now;
  }

  bool profile_changed = false;
  for (const auto& touched : touched_targets) {
    if (!touched.second) continue;
    if (next_profile.selections.find(touched.first) != next_profile.selections.end()) continue;
    auto matches = std::count_if(next_assets.begin(), next_assets.end(), [&](const AssetRecord& item) {
      return selection_key(item) == touched.first;
    });
    if (matches == 1) {
      auto only = std::find_if(next_assets.begin(), next_assets.end(), [&](const AssetRecord& item) {
        return selection_key(item) == touched.first;
      });
      next_profile.selections[touched.first] = only->info.id;
      profile_changed = true;
    }
  }
  if (profile_changed) ++next_profile.generation;

  for (const auto& pending : writes) {
    std::error_code ec;
    bool existed = fs::exists(pending.path, ec);
    if (ec || !write_atomic(pending.path, pending.bytes->data(), pending.bytes->size(), &error)) {
      for (const auto& created : newly_created) DeleteFileW(created.c_str());
      result.message = ec ? "A Nucleus asset destination could not be inspected." : error;
      return result;
    }
    if (!existed) newly_created.push_back(pending.path);
  }
  auto previous_assets = std::move(g_assets);
  Profile previous_profile = std::move(g_profile);
  g_assets = std::move(next_assets); g_profile = std::move(next_profile);
  if (!save_state_locked(&error)) {
    g_assets = std::move(previous_assets); g_profile = std::move(previous_profile);
    for (const auto& created : newly_created) DeleteFileW(created.c_str());
    result.message = error; return result;
  }
  result.ok = true; result.already_present = added == 0;
  result.already_present = result.already_present && resource_added == 0;
  result.message = (resource_source_kind == "nucleus_project" ?
                    "Nucleus project committed atomically: " : "Stage import committed atomically: ") +
                   std::to_string(added) +
                   " new / " + std::to_string(refreshed) + " existing character variants, " +
                   std::to_string(resource_added) + " new / " + std::to_string(resource_refreshed) +
                   " existing stage/effect resources; " +
                   std::to_string(plan.rejected_resources.size()) +
                   " structurally unsupported records quarantined. Effects require exact-ISO materialization; stage visuals are checked before online use.";
  return result;
}

std::vector<uint8_t> load_runtime_asset_locked(const AssetRecord& asset, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!safe_relative_path(asset.stored_path) ||
      !read_bounded(g_root / fs::u8path(asset.stored_path), kMaxAssetBytes, &bytes, error)) return {};
  if (sha256(bytes) != asset.info.sha256) {
    *error = asset.info.name + ": stored DAT hash no longer matches the catalog."; return {};
  }
  if (asset.info.kind == "character_costume") {
    auto dat = inspect_dat_impl(bytes);
    if (!dat.ok || dat.target_path != asset.info.target_path) {
      *error = asset.info.name + ": stored DAT identity no longer matches its catalog target."; return {};
    }
  } else if (asset.info.kind == "stage_visual" || asset.info.kind == "effect_visual") {
    VisualLayout layout;
    if (!parse_visual_layout(bytes, &layout, error)) {
      *error = asset.info.name + ": " + *error; return {};
    }
  } else {
    *error = asset.info.name + ": unsupported catalog resource kind."; return {};
  }
  return bytes;
}

bool refresh_assets_locked(bool prune_invalid_selections, std::string* error) {
  std::vector<std::string> invalid_selected;
  for (auto& asset : g_assets) {
    std::string validation_error;
    asset.info.available = !load_runtime_asset_locked(asset, &validation_error).empty();
    asset.info.availability_message = asset.info.available ?
        (asset.info.kind == "character_costume" ? std::string() :
         asset.info.kind == "stage_visual" ?
             "Stage replacement is offline unless exact-ISO validation proves texture-only changes." :
             "Exact clean-ISO effect validation and safe materialization are required when selected and repeated at every launch.") :
        validation_error;
    auto selected = g_profile.selections.find(selection_key(asset));
    if (!asset.info.available && selected != g_profile.selections.end() &&
        selected->second == asset.info.id)
      invalid_selected.push_back(selection_key(asset));
  }
  for (const auto& selected : g_profile.selections) {
    if (selected.second == kVanillaSelection) continue;
    auto asset = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
      return item.info.id == selected.second && selection_key(item) == selected.first;
    });
    if (asset == g_assets.end()) invalid_selected.push_back(selected.first);
  }
  if (!prune_invalid_selections || invalid_selected.empty()) return true;
  Profile previous = g_profile;
  std::sort(invalid_selected.begin(), invalid_selected.end());
  invalid_selected.erase(std::unique(invalid_selected.begin(), invalid_selected.end()),
                         invalid_selected.end());
  for (const auto& target : invalid_selected) g_profile.selections[target] = kVanillaSelection;
  ++g_profile.generation;
  if (!save_profile_locked(error)) { g_profile = std::move(previous); return false; }
  g_message = std::to_string(invalid_selected.size()) +
              " invalid selection(s) fell back to Vanilla.";
  return true;
}

struct FstFile { uint32_t index = 0, start = 0, size = 0; std::string path; };
bool parse_fst(uint8_t* fst, uint32_t fst_size, std::vector<FstFile>* files, std::string* error) {
  files->clear();
  if (!fst || fst_size < 12) { *error = "ISO FST is missing or truncated."; return false; }
  uint32_t entries = be32(fst + 8);
  uint64_t strings = (uint64_t)entries * 12;
  if (!entries || strings > fst_size) { *error = "ISO FST entry table is invalid."; return false; }
  struct Dir { std::string path; uint32_t end; };
  std::vector<Dir> stack{{"", entries}};
  for (uint32_t i = 1; i < entries; ++i) {
    while (stack.size() > 1 && i >= stack.back().end) stack.pop_back();
    uint8_t* record = fst + (size_t)i * 12;
    uint32_t type_name = be32(record), name_offset = type_name & 0x00ffffffu;
    if (strings + name_offset >= fst_size) { *error = "ISO FST filename points outside the string table."; return false; }
    const char* text = (const char*)fst + strings + name_offset;
    size_t available = fst_size - (size_t)(strings + name_offset), length = 0;
    while (length < available && text[length]) ++length;
    if (length == available) { *error = "ISO FST filename is not terminated."; return false; }
    std::string name(text, length);
    std::string path = stack.back().path.empty() ? name : stack.back().path + "/" + name;
    if (type_name >> 24) {
      uint32_t end = be32(record + 8);
      if (end <= i || end > entries) { *error = "ISO FST directory range is invalid."; return false; }
      stack.push_back({path, end});
    } else files->push_back({i, be32(record + 4), be32(record + 8), path});
  }
  return true;
}

}  // namespace

void configure(const std::string& settings_path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  fs::path settings = fs::u8path(settings_path);
  fs::path parent = settings.parent_path();
  if (parent.empty()) parent = fs::current_path();
  fs::path root = parent / L"CosmeticMods";
  g_root = std::move(root); g_configured = true; g_message.clear();
  bool state_exists = false;
  load_state_locked(&state_exists);
  if (!state_exists && g_catalog_valid && g_profile_valid) {
    load_catalog_locked(); load_profile_locked();
    if (g_catalog_valid && g_profile_valid) {
      std::string migration_error;
      if (!save_state_locked(&migration_error)) {
        g_catalog_valid = g_profile_valid = false; g_message = migration_error;
      }
    }
  }
  if (g_catalog_valid && g_profile_valid) {
    // Older profiles keyed all effects by their DAT. Preserve the selected move
    // when moving to independent scope keys; other moves stay vanilla.
    bool migrated_effects = false;
    for (const auto& asset : g_assets) {
      if (asset.info.kind != "effect_visual") continue;
      auto old = g_profile.selections.find(asset.info.target_path);
      if (old == g_profile.selections.end() || old->second != asset.info.id) continue;
      g_profile.selections[selection_key(asset)] = old->second;
      g_profile.selections.erase(old);
      migrated_effects = true;
    }
    if (migrated_effects) {
      ++g_profile.generation;
      std::string migration_error;
      if (!save_state_locked(&migration_error)) {
        g_catalog_valid = g_profile_valid = false;
        g_message = migration_error;
      }
    }
  }
  if (g_catalog_valid && g_profile_valid) {
    std::string error;
    if (refresh_assets_locked(true, &error)) {
      if (g_message.empty())
        g_message = g_assets.empty() ? "No cosmetic mods imported." : "Cosmetic catalog loaded.";
    } else g_message = error;
  }
}

bool make_standalone_stage(const fs::path& source, const std::string& member,
                           std::vector<uint8_t> bytes, VaultResourcePlan* resource,
                           std::string* error) {
  const std::string filename = member.empty() ? path_filename_utf8(source) : member;
  if (!stage_dat_target(filename, &resource->target_path, &resource->slot)) {
    *error = "The stage DAT name does not identify a supported disc resource."; return false;
  }
  VisualLayout layout;
  if (!parse_visual_layout(bytes, &layout, error)) return false;
  const std::string digest = sha256(bytes);
  if (digest.empty()) { *error = "The stage DAT could not be hashed."; return false; }
  resource->source_id = "standalone-stage:" + resource->target_path + ":" + digest;
  resource->kind = "stage_visual";
  resource->display_name = path_filename_utf8(source.stem());
  if (resource->display_name.empty() || lower(resource->display_name) == lower(resource->target_path))
    resource->display_name = resource->slot + " import";
  resource->group = "Stages";
  resource->source_member = member;
  resource->bytes = std::move(bytes);
  return true;
}

ImportResult import_file(const std::string& path_text) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string readiness;
    if (!mutable_profile_locked(&readiness)) return {false, false, {}, readiness};
  }
  fs::path path = fs::u8path(path_text);
  std::string extension = lower(path.extension().string());
  std::vector<uint8_t> bytes;
  testing::DatInspection dat;
  std::vector<VaultCompanionPlan> companions;
  std::string member, error;
  if (extension == ".dat") {
    if (!read_bounded(path, kMaxAssetBytes, &bytes, &error)) return {false, false, {}, error};
    dat = inspect_dat_impl(bytes);
    if (!dat.ok) {
      VaultResourcePlan stage;
      if (!make_standalone_stage(path, {}, std::move(bytes), &stage, &error))
        return {false, false, {}, error};
      VaultPlan plan;
      plan.resources.push_back(std::move(stage)); plan.stage_records = 1;
      std::lock_guard<std::mutex> lock(g_mutex);
      ImportResult result = install_vault_locked(path, std::move(plan), "standalone_stage");
      if (result.ok && std::atomic_load(&g_runtime)->initialized)
        result.message += " Restart to apply the staged profile safely.";
      g_message = result.message;
      return result;
    }
  } else if (extension == ".zip") {
    std::vector<ZipEntry> entries;
    if (!parse_zip(path, &entries, &error)) return {false, false, {}, error};
    auto metadata = std::find_if(entries.begin(), entries.end(), [](const ZipEntry& entry) {
      return lower(entry.name) == "metadata.json";
    });
    if (metadata != entries.end()) {
      VaultPlan plan;
      try {
        if (!parse_nucleus_vault(path, entries, *metadata, &plan, &error))
          return {false, false, {}, error};
      } catch (const std::exception&) {
        return {false, false, {}, "Nucleus metadata contains an invalid field type or structure."};
      }
      std::lock_guard<std::mutex> lock(g_mutex);
      ImportResult result = install_vault_locked(path, std::move(plan));
      if (result.ok && std::atomic_load(&g_runtime)->initialized)
        result.message += " Restart to apply the staged profile safely.";
      g_message = result.message;
      return result;
    }
    std::vector<std::pair<ZipEntry, testing::DatInspection>> recognized;
    std::vector<std::vector<uint8_t>> recognized_bytes;
    std::vector<VaultResourcePlan> stage_resources;
    uint32_t candidates = 0;
    for (const auto& entry : entries) {
      std::string ext = lower(fs::path(entry.name).extension().string());
      if (ext != ".dat") continue;
      if (++candidates > kMaxDatCandidates)
        return {false, false, {}, "ZIP contains too many DAT candidates for a bounded import."};
      std::vector<uint8_t> candidate;
      if (!extract_zip_member(path, entry, &candidate, &error)) return {false, false, {}, error};
      auto inspected = inspect_dat_impl(candidate);
      if (inspected.ok) {
        recognized.push_back({entry, inspected});
        recognized_bytes.push_back(std::move(candidate));
      } else {
        std::string target, slot;
        if (stage_dat_target(entry.name, &target, &slot)) {
          VaultResourcePlan stage;
          if (!make_standalone_stage(path, entry.name, std::move(candidate), &stage, &error))
            return {false, false, {}, "Stage " + entry.name + " rejected: " + error};
          stage_resources.push_back(std::move(stage));
        }
      }
    }
    if (!stage_resources.empty()) {
      if (!recognized.empty())
        return {false, false, {}, "ZIP mixes costumes and stages; import separate archives."};
      VaultPlan plan;
      const ZipEntry* screenshot = nullptr;
      for (const auto& entry : entries) {
        if (lower(fs::path(entry.name).extension().string()) != ".png") continue;
        if (!screenshot || lower(entry.name).find("screenshot_0") != std::string::npos)
          screenshot = &entry;
      }
      if (screenshot) {
        std::vector<uint8_t> preview;
        std::string preview_error;
        if (extract_zip_member(path, *screenshot, &preview, &preview_error) &&
            png_companion(preview, &preview_error)) {
          for (auto& stage : stage_resources)
            stage.companions.push_back({"preview", screenshot->name, preview});
        }
      }
      plan.stage_records = stage_resources.size();
      plan.resources = std::move(stage_resources);
      std::lock_guard<std::mutex> lock(g_mutex);
      ImportResult result = install_vault_locked(path, std::move(plan), "standalone_stage");
      if (result.ok && std::atomic_load(&g_runtime)->initialized)
        result.message += " Restart to apply the staged profile safely.";
      g_message = result.message;
      return result;
    }
    if (recognized.empty())
      return {false, false, {}, "ZIP contains no valid DAT for a supported costume or stage resource."};
    if (recognized.size() != 1)
      return {false, false, {}, "ZIP contains multiple supported DATs; multi-asset bundle import is not enabled in this milestone."};
    member = recognized[0].first.name;
    dat = std::move(recognized[0].second);
    bytes = std::move(recognized_bytes[0]);
    for (const auto& entry : entries) {
      std::string label = companion_label(entry);
      if (label.empty()) continue;
      VaultCompanionPlan companion;
      companion.kind = lower(entry.name).find("stock") != std::string::npos ? "stock" : "csp";
      companion.source_member = entry.name;
      if (std::any_of(companions.begin(), companions.end(), [&](const VaultCompanionPlan& item) {
            return item.kind == companion.kind;
          }))
        return {false, false, {}, "ZIP contains multiple " + companion.kind + " companion PNGs."};
      if (!extract_zip_member(path, entry, &companion.bytes, &error) ||
          !png_companion(companion.bytes, &error))
        return {false, false, {}, error};
      companions.push_back(std::move(companion));
    }
  } else {
    return {false, false, {}, "Choose a costume or stage .dat/.zip, or a Nucleus vault .zip."};
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  ImportResult result = install_asset_locked(path, extension == ".zip" ? "zip" : "dat", member,
                                             std::move(bytes), dat, std::move(companions));
  if (result.ok && std::atomic_load(&g_runtime)->initialized)
    result.message += " Restart to apply the staged profile safely.";
  g_message = result.message;
  return result;
}

std::vector<AssetInfo> assets() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<AssetInfo> out;
  out.reserve(g_assets.size());
  for (const auto& record : g_assets) {
    AssetInfo info = record.info;
    for (const auto& companion : record.companions) {
      if (companion.kind != "csp" && companion.kind != "preview" && companion.kind != "stock") continue;
      const fs::path preview = g_root / fs::u8path(companion.stored_path);
      std::error_code ec;
      if (fs::is_regular_file(preview, ec)) {
        info.preview_path = preview.string();
        if (companion.kind == "csp" || companion.kind == "preview") break;
      }
    }
    if (info.kind == "effect_visual") {
      info.scope = effect_scope(record);
      info.costume = effect_label(info.scope);
    }
    auto selected = g_profile.selections.find(selection_key(record));
    info.selected = selected != g_profile.selections.end() && selected->second == info.id;
    out.push_back(std::move(info));
  }
  return out;
}

bool refresh_catalog(std::string* error) {
  std::string local; if (!error) error = &local;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!mutable_profile_locked(error)) return false;
  uint64_t previous_generation = g_profile.generation;
  if (!refresh_assets_locked(true, error)) return false;
  if (g_profile.generation == previous_generation)
    g_message = "Cosmetic catalog refreshed; selections are valid.";
  return true;
}

bool rename_asset(const std::string& asset_id, const std::string& display_name,
                  std::string* error) {
  std::string local; if (!error) error = &local;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!mutable_profile_locked(error)) return false;
  size_t begin = 0, end = display_name.size();
  while (begin < end && std::isspace((unsigned char)display_name[begin])) ++begin;
  while (end > begin && std::isspace((unsigned char)display_name[end - 1])) --end;
  std::string name = display_name.substr(begin, end - begin);
  if (name.empty() || name.size() > 96 || std::any_of(name.begin(), name.end(), [](char c) {
        return (unsigned char)c < 0x20 || c == 0x7f;
      })) {
    *error = "Display names must contain 1-96 printable characters."; return false;
  }
  auto asset = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
    return item.info.id == asset_id;
  });
  if (asset == g_assets.end()) { *error = "The asset is no longer in the catalog."; return false; }
  std::string previous = asset->info.name;
  asset->info.name = name;
  if (!save_catalog_locked(error)) { asset->info.name = std::move(previous); return false; }
  g_message = "Renamed cosmetic variant to " + name + ".";
  return true;
}

bool profile_enabled() { std::lock_guard<std::mutex> lock(g_mutex); return g_profile.enabled; }

bool set_profile_enabled(bool enabled, std::string* error) {
  std::string local;
  if (!error) error = &local;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!mutable_profile_locked(error)) return false;
  if (g_profile.enabled == enabled) return true;
  Profile previous = g_profile;
  g_profile.enabled = enabled; ++g_profile.generation;
  if (!save_profile_locked(error)) { g_profile = std::move(previous); return false; }
  g_message = enabled ? "Cosmetic profile enabled; restart to apply it." :
                        "Cosmetic profile disabled; restart to restore vanilla assets.";
  return true;
}

bool enable_project_effects(std::string* error) {
  std::string local; if (!error) error = &local;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!mutable_profile_locked(error)) return false;

  // Select one validated variant for each move, then ensure the selected moves
  // compose without writing different values to the same clean-resource byte.
  std::map<std::string, bool> targets;
  std::map<std::string, bool> disc_targets;
  std::map<std::string, std::string> selections;
  std::map<std::string, std::string> failures;
  for (auto& asset : g_assets) {
    if (asset.info.kind != "effect_visual") continue;
    const std::string& target = asset.info.target_path;
    const std::string key = selection_key(asset);
    targets[key] = true;
    disc_targets[target] = true;
    if (selections.find(key) != selections.end()) continue;

    std::string validation_error;
    std::vector<uint8_t> candidate = load_runtime_asset_locked(asset, &validation_error);
    asset.info.available = !candidate.empty();
    if (asset.info.available) {
      std::vector<uint8_t> runtime;
      std::string classification;
      asset.info.available = materialize_effect_with_open_disc(
          target, candidate, &runtime, &classification, &validation_error);
      if (asset.info.available) {
        asset.info.availability_message = classification;
        selections[key] = asset.info.id;
        failures.erase(key);
        continue;
      }
    }
    asset.info.availability_message = validation_error;
    failures[key] = validation_error;
  }
  if (targets.empty()) {
    *error = "No project effects are installed.";
    return false;
  }
  if (selections.size() != targets.size()) {
    std::ostringstream message;
    message << "Project effects were not changed because no safe variant validated for ";
    bool first = true;
    for (const auto& target : targets) {
      if (selections.find(target.first) != selections.end()) continue;
      if (!first) message << ", ";
      first = false;
      message << target.first;
      auto reason = failures.find(target.first);
      if (reason != failures.end() && !reason->second.empty()) message << " (" << reason->second << ")";
    }
    *error = message.str();
    return false;
  }

  Profile previous = g_profile;
  for (auto it = g_profile.selections.begin(); it != g_profile.selections.end();) {
    if (it->first.find('#') != std::string::npos) it = g_profile.selections.erase(it);
    else ++it;
  }
  for (const auto& selection : selections)
    g_profile.selections[selection.first] = selection.second;
  for (const auto& target : disc_targets) {
    if (!compose_effects_with_open_disc_locked(target.first, g_profile, error)) {
      g_profile = std::move(previous);
      return false;
    }
  }
  g_profile.enabled = true;
  ++g_profile.generation;
  if (!save_profile_locked(error)) { g_profile = std::move(previous); return false; }
  g_message = std::to_string(selections.size()) +
              " project effect move slots selected; restart to apply them.";
  return true;
}

bool select_variant(const std::string& target_path, const std::string& asset_id, std::string* error) {
  std::string local; if (!error) error = &local;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!mutable_profile_locked(error)) return false;
  auto asset = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
    return item.info.id == asset_id && selection_key(item) == target_path;
  });
  if (asset == g_assets.end()) { *error = "The selected variant does not belong to that slot."; return false; }
  std::string validation_error;
  std::vector<uint8_t> candidate = load_runtime_asset_locked(*asset, &validation_error);
  asset->info.available = !candidate.empty();
  if (asset->info.available && asset->info.kind == "effect_visual") {
    std::vector<uint8_t> runtime;
    std::string classification;
    asset->info.available = materialize_effect_with_open_disc(
        asset->info.target_path, candidate, &runtime, &classification, &validation_error);
    if (asset->info.available) asset->info.availability_message = classification;
  }
  if (asset->info.available && asset->info.kind == "stage_visual")
    asset->info.availability_message =
        "Stage replacement is offline unless exact-ISO validation proves texture-only changes.";
  else if (!asset->info.available)
    asset->info.availability_message = validation_error;
  if (!asset->info.available) {
    *error = "The selected variant is unavailable or invalid; Vanilla remains selected."; return false;
  }
  Profile previous = g_profile;
  g_profile.enabled = true; g_profile.selections[target_path] = asset_id; ++g_profile.generation;
  if (asset->info.kind == "effect_visual" &&
      !compose_effects_with_open_disc_locked(asset->info.target_path, g_profile, error)) {
    g_profile = std::move(previous);
    return false;
  }
  if (!save_profile_locked(error)) { g_profile = std::move(previous); return false; }
  g_message = asset->info.name + " selected for " + target_path + "; restart to apply it.";
  return true;
}

bool disable_target(const std::string& target_path, std::string* error) {
  std::string local; if (!error) error = &local;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!mutable_profile_locked(error)) return false;
  Profile previous = g_profile;
  auto current = g_profile.selections.find(target_path);
  if (current != g_profile.selections.end() && current->second == kVanillaSelection) return true;
  g_profile.selections[target_path] = kVanillaSelection;
  ++g_profile.generation;
  if (!save_profile_locked(error)) { g_profile = std::move(previous); return false; }
  g_message = target_path + " disabled; restart to restore its vanilla asset.";
  return true;
}

bool restore_vanilla(std::string* error) {
  std::string local; if (!error) error = &local;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!mutable_profile_locked(error)) return false;
  Profile previous = g_profile;
  g_profile.enabled = false; g_profile.selections.clear(); ++g_profile.generation;
  if (!save_profile_locked(error)) { g_profile = std::move(previous); return false; }
  g_message = "Vanilla profile staged. Restart clears guest/render caches and uses the clean ISO.";
  return true;
}

std::string last_message() { std::lock_guard<std::mutex> lock(g_mutex); return g_message; }

bool pending_restart() {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto runtime = std::atomic_load(&g_runtime);
  return runtime->initialized && runtime->fingerprint != desired_fingerprint_locked();
}
bool runtime_initialized() { return std::atomic_load(&g_runtime)->initialized; }

std::vector<CompanionOverride> active_companions() {
  return std::atomic_load(&g_runtime)->companions;
}

void apply_to_fst(uint8_t* fst, uint32_t fst_size) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto next = std::make_shared<RuntimeState>();
  next->initialized = true; next->generation = g_profile.generation;
  next->fingerprint = desired_fingerprint_locked();
  if (!g_configured || !g_catalog_valid || !g_profile_valid || !g_profile.enabled) {
    std::atomic_store(&g_runtime, std::shared_ptr<const RuntimeState>(next));
    host::log("cosmetics: vanilla profile (%s)", !g_profile.enabled ? "disabled" : "catalog unavailable");
    return;
  }
  std::vector<FstFile> files; std::string error;
  if (!parse_fst(fst, fst_size, &files, &error)) {
    g_message = error; host::log("cosmetics: %s", error.c_str());
    std::atomic_store(&g_runtime, std::shared_ptr<const RuntimeState>(next)); return;
  }
  std::map<std::string, bool> effect_done;
  std::vector<std::string> runtime_errors;
  for (const auto& pick : g_profile.selections) {
    if (pick.second == kVanillaSelection) continue;
    auto asset = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
      return item.info.id == pick.second && selection_key(item) == pick.first;
    });
    if (asset == g_assets.end()) { host::log("cosmetics: missing catalog asset %s", pick.second.c_str()); continue; }
    if (asset->info.kind == "effect_visual" && effect_done[asset->info.target_path]) continue;
    if (asset->info.kind == "effect_visual") effect_done[asset->info.target_path] = true;
    std::vector<uint8_t> bytes = load_runtime_asset_locked(*asset, &error);
    if (bytes.empty()) { host::log("cosmetics: %s", error.c_str()); continue; }
    std::string wanted = lower(asset->info.target_path);
    std::vector<FstFile*> matches;
    for (auto& file : files) {
      std::string path = lower(file.path);
      size_t slash = path.find_last_of('/');
      std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
      if (path == wanted || (wanted.find('/') == std::string::npos && base == wanted)) matches.push_back(&file);
    }
    if (matches.size() != 1) {
      host::log("cosmetics: %s resolved to %zu ISO files; override not applied", pick.first.c_str(), matches.size());
      continue;
    }
    FstFile& file = *matches[0];
    if (asset->info.kind == "effect_visual") {
      std::vector<uint8_t> clean(file.size);
      std::vector<uint8_t> materialized;
      std::string visual_error;
      if (!host::disc_read(file.start, clean.data(), file.size) ||
          !compose_effects_locked(asset->info.target_path,
                                  selected_effects_locked(g_profile, asset->info.target_path),
                                  clean, &materialized, &visual_error)) {
        host::log("cosmetics: effect selection rejected for %s; vanilla fallback (%s)",
                  file.path.c_str(), visual_error.c_str());
        runtime_errors.push_back(visual_error);
        continue;
      }
      bytes = std::move(materialized);
      host::log("cosmetics: composed %zu effect move(s) for %s",
                selected_effects_locked(g_profile, asset->info.target_path).size(), file.path.c_str());
    }
    bool online_allowed = true;
    if (asset->info.kind == "stage_visual") {
      std::vector<uint8_t> clean(file.size);
      std::string validation_error;
      online_allowed = host::disc_read(file.start, clean.data(), file.size) &&
                       visual_dat_only(clean, bytes, &validation_error);
      host::log("cosmetics: stage %s %s online (%s)", file.path.c_str(),
                online_allowed ? "allowed" : "uses vanilla", validation_error.c_str());
      // The offline DAT may be shorter than the disc resource. Keep the disc's
      // original extent available for aligned DVD reads during online fallback.
      if (!online_allowed && bytes.size() < file.size) bytes.resize(file.size, 0);
    }
    if (bytes.size() > std::numeric_limits<uint32_t>::max()) continue;
    put_be32(fst + (size_t)file.index * 12 + 8, (uint32_t)bytes.size());
    RuntimeAsset active{asset->info.id, asset->info.target_path,
                        std::make_shared<const std::vector<uint8_t>>(std::move(bytes)),
                        file.size, online_allowed};
    next->by_start[file.start] = std::move(active);
    host::log("cosmetics: %s -> %s at %08X (%u vanilla bytes, %zu override bytes)",
              asset->info.name.c_str(), file.path.c_str(), file.start, file.size,
              next->by_start[file.start].bytes->size());
    for (const auto& companion : asset->companions) {
      if (companion.kind == "preview") continue;
      std::vector<uint8_t> companion_bytes;
      const fs::path path = g_root / fs::u8path(companion.stored_path);
      std::string companion_error;
      if (!read_bounded(path, kMaxAssetBytes, &companion_bytes, &companion_error) ||
          sha256(companion_bytes) != companion.sha256) {
        host::log("cosmetics: %s companion for %s is missing or changed; using vanilla",
                  companion.kind.c_str(), asset->info.target_path.c_str());
        continue;
      }
      next->companions.push_back({companion.kind, asset->info.target_path, path.string()});
    }
  }
  std::atomic_store(&g_runtime, std::shared_ptr<const RuntimeState>(next));
  g_message = next->by_start.empty() ? "No selected cosmetic matched this ISO." :
              std::to_string(next->by_start.size()) + " cosmetic override(s) active for this launch.";
  for (const auto& issue : runtime_errors) g_message += " " + issue;
}

OverrideRead read(uint32_t vanilla_file_start, uint32_t file_offset, void* dst, uint32_t size) {
  auto runtime = std::atomic_load(&g_runtime);
  auto found = runtime->by_start.find(vanilla_file_start);
  if (found == runtime->by_start.end()) return OverrideRead::NotOverridden;
  if (g_online_freezes.load(std::memory_order_relaxed) && !found->second.online_allowed) {
    const uint32_t vanilla_size = found->second.vanilla_size;
    const uint64_t exposed_size = std::max<size_t>(vanilla_size, found->second.bytes->size());
    if (file_offset > exposed_size || (uint64_t)file_offset + size > exposed_size + 31)
      return OverrideRead::Failed;
    const uint32_t disc_size = file_offset < vanilla_size ? std::min(size, vanilla_size - file_offset) : 0;
    if (disc_size && !host::disc_read(vanilla_file_start + file_offset, dst, disc_size))
      return OverrideRead::Failed;
    if (disc_size < size) std::memset((uint8_t*)dst + disc_size, 0, size - disc_size);
    return OverrideRead::Success;
  }
  const auto& bytes = *found->second.bytes;
  uint64_t begin = file_offset, end = begin + size;
  if (end < begin || begin > bytes.size()) return OverrideRead::Failed;
  if (end > bytes.size() && end - bytes.size() > 31) return OverrideRead::Failed;
  size_t available = begin < bytes.size() ? bytes.size() - (size_t)begin : 0;
  size_t copy = std::min<size_t>(size, available);
  if (copy) std::memcpy(dst, bytes.data() + (size_t)begin, copy);
  if (copy < size) {
    // DVD callers commonly align the final transfer to 32 bytes. Only that bounded tail is legal.
    std::memset((uint8_t*)dst + copy, 0, size - copy);
  }
  return OverrideRead::Success;
}

void freeze_for_online_session() { g_online_freezes.store(1, std::memory_order_relaxed); }
void thaw_after_online_session() { g_online_freezes.store(0, std::memory_order_relaxed); }
SessionProfile session_profile() {
  auto runtime = std::atomic_load(&g_runtime);
  return {runtime->generation, runtime->fingerprint, (uint32_t)runtime->by_start.size(),
          g_online_freezes.load(std::memory_order_relaxed) != 0};
}

std::string choose_import_file() {
  wchar_t file[32768]{};
  OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof dialog;
  dialog.hwndOwner = GetActiveWindow();
  dialog.lpstrFilter = L"Cosmetic imports (*.zip;*.dat)\0*.zip;*.dat\0DAT costume (*.dat)\0*.dat\0ZIP archive (*.zip)\0*.zip\0";
  dialog.lpstrFile = file; dialog.nMaxFile = (DWORD)std::size(file);
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  return GetOpenFileNameW(&dialog) ? wide_to_utf8(file) : std::string();
}

namespace testing {
DatInspection inspect_dat(const std::vector<uint8_t>& bytes) { return inspect_dat_impl(bytes); }
bool visual_dat_only(const std::vector<uint8_t>& clean, const std::vector<uint8_t>& candidate,
                     std::string* error) {
  std::string local; if (!error) error = &local;
  return host::cosmetics::visual_dat_only(clean, candidate, error);
}
bool materialize_effect_dat(const std::string& target_path,
                            const std::vector<uint8_t>& clean,
                            const std::vector<uint8_t>& candidate,
                            std::vector<uint8_t>* runtime,
                            std::string* classification,
                            std::string* error) {
  std::string local; if (!error) error = &local;
  return materialize_effect_dat_impl(target_path, clean, candidate, runtime, classification, error);
}
bool inspect_zip(const std::string& path, std::vector<std::string>* names, std::string* error) {
  std::vector<ZipEntry> entries;
  if (!parse_zip(fs::u8path(path), &entries, error)) return false;
  names->clear(); for (const auto& entry : entries) names->push_back(entry.name);
  return true;
}
}  // namespace testing

}  // namespace host::cosmetics
