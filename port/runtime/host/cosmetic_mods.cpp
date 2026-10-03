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
#include <cfloat>
#include <cmath>
#include <cstdio>
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
  // A skin that lives inside a mod disc or a files pack (source_kind "disc"): no copy is kept. The
  // bytes are read from disc_path at disc_offset when the skin is applied, and checked against the
  // catalog's SHA-256 then. disc_size and disc_mtime are the container's size and time at the scan,
  // for a cheap "is it still the same file" check at startup. source_id is the scanned disc or folder.
  std::string disc_path;
  uint64_t disc_offset = 0, disc_length = 0, disc_size = 0;
  int64_t disc_mtime = 0;
  // The skeleton verdict taken when the scan had both files in hand, so the list can say "stays on
  // online" before the skin was ever applied. Imports have none until they are applied.
  bool online_known = false, online_ok = false;
  std::string online_note;
  // The fighter sound bank this skin brings (docs/voice-mods.md): a file of the catalog, checked
  // against its SHA-256 when it is applied. `bank` is the disc file it replaces ("fox.ssm"). Empty
  // stored_path: the skin has none.
  struct Voice { std::string stored_path, sha256, source_member, bank; } voice;
};

// One line per scanned disc: scanning reads every costume file, so it runs once per disc and again
// only when the file's size or time changes, or when the scan itself learned something new (rules).
struct DiscScan { std::string path; uint64_t size = 0; int64_t mtime = 0; uint32_t rules = 0; };
// 1: .dat and .usd costumes. 2: alternate costumes under other extensions (.lat, .rat) and the online
// verdict taken at scan time. 3: the pack's own portraits, from its character select file. 4: the
// skeleton check reads the relocation table (a joint at data offset 0 is a joint) and its reason
// names the bone. 5: a costume whose blended mesh names a bone with no bind matrix is marked, since
// the game stops when it draws one. A disc scanned under an older number is scanned again at the next boot.
constexpr uint32_t kScanRules = 5;
constexpr const char* kImportPack = "import";
constexpr const char* kDiscSource = "disc";
constexpr const char* kDiscChanged = "disc file missing or changed";

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
  std::string kind;            // the catalog kind, for the counts the notice shows
  std::string online_reason;   // short text for the Mods tab; empty when there is no online rule (effects)
  // A voice bank file only (kind kVoiceKind): the length the file table carries for it. `bytes` is
  // the chosen skin's bank padded to that length, or null while the disc's own bank is served.
  uint32_t length = 0;
};

constexpr const char* kVoiceKind = "voice_bank";

// One disc bank file ("audio/us/fox.ssm") that at least one installed skin has a fitting bank for.
// The candidates are fixed at startup; plan_match_voices picks among them.
struct VoiceFile {
  uint32_t start = 0, disc_size = 0, length = 0;
  std::string bank, path;   // "fox.ssm", and the disc path for the log
  int bank_index = -1;      // the game's bank number
  std::string chosen;       // the skin serving it now, "" the disc's own bank
  std::map<std::string, std::shared_ptr<const std::vector<uint8_t>>> candidates;   // skin id -> served bytes
};

struct RuntimeState {
  bool initialized = false;
  uint64_t generation = 0;
  std::string fingerprint;
  std::unordered_map<uint32_t, RuntimeAsset> by_start;
  uint32_t assets = 0;   // selected assets applied (an asset with an English twin has two files in by_start)
  std::vector<CompanionOverride> companions;
  std::vector<VoiceFile> voice_files;
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
std::vector<DiscScan> g_disc_scans;
Profile g_profile;
bool g_configured = false;
bool g_catalog_valid = true;
bool g_profile_valid = true;
std::string g_message;
std::shared_ptr<const RuntimeState> g_runtime = std::make_shared<RuntimeState>();
std::atomic<uint32_t> g_online_freezes{0};
// The mod disc this session serves as its own files (set_session_pack), spelled as a scan's key.
std::string g_session_pack;

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

// A DAT archive is named .dat, or .usd when it is the English variant of a file the disc keeps
// per language (Captain Falcon's red costume, Pokemon Stadium). A mod ships under either name.
bool dat_extension(const std::string& name) {
  const std::string extension = lower(fs::path(name).extension().string());
  return extension == ".dat" || extension == ".usd";
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

bool file_stamp(const fs::path& path, uint64_t* size, int64_t* mtime) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec) || ec) return false;
  *size = fs::file_size(path, ec);
  if (ec) return false;
  *mtime = (int64_t)fs::last_write_time(path, ec).time_since_epoch().count();
  return !ec;
}

// A bounded read of one file inside a disc image (or of a whole loose file, offset 0).
bool read_range(const fs::path& path, uint64_t offset, uint64_t length, std::vector<uint8_t>* out) {
  out->clear();
  if (!length || length > kMaxAssetBytes) return false;
  std::error_code ec;
  const uint64_t size = fs::file_size(path, ec);
  if (ec || offset > size || length > size - offset) return false;
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  file.seekg((std::streamoff)offset);
  out->resize((size_t)length);
  if (!file.read((char*)out->data(), (std::streamsize)length)) { out->clear(); return false; }
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

// A skin's sound bank is its own "voice" object, not one of "companions": a build that predates
// voice mods refuses a catalog with a companion kind it does not know, and ignores a key it does not
// know. A malformed entry is dropped (the skin stays); it never invalidates the catalog.
void load_voice(const json& item, AssetRecord* asset) {
  const auto found = item.find("voice");
  if (found == item.end() || !found->is_object()) return;
  AssetRecord::Voice voice;
  voice.stored_path = found->value("stored_path", std::string());
  voice.sha256 = found->value("sha256", std::string());
  voice.source_member = found->value("source_member", std::string());
  voice.bank = found->value("bank", std::string());
  if (!safe_relative_path(voice.stored_path) || voice.sha256.size() != 64 || voice.bank.empty() ||
      voice.bank.find('/') != std::string::npos || voice.bank.find('\\') != std::string::npos) return;
  asset->voice = std::move(voice);
  asset->info.voice = asset->voice.bank;
  asset->info.voice_source = asset->voice.source_member;
}

// The public listing says where a skin comes from; a disc-backed record also says where its bytes are.
void load_disc_fields(const json& item, AssetRecord* asset) {
  asset->info.source = asset->source_kind == kDiscSource ? "disc" : "import";
  if (asset->source_kind != kDiscSource) return;
  const json& disc = item.at("disc");
  asset->disc_path = disc.at("path").get<std::string>();
  asset->disc_offset = disc.at("offset").get<uint64_t>();
  asset->disc_length = disc.at("length").get<uint64_t>();
  asset->disc_size = disc.value("size", (uint64_t)0);
  asset->disc_mtime = disc.value("mtime", (int64_t)0);
  asset->info.source_name = asset->source_name;
  asset->info.disc_path = asset->disc_path;
  if (asset->disc_path.empty() || !asset->disc_length || asset->disc_length > kMaxAssetBytes)
    throw std::runtime_error("disc");
  asset->info.variant = item.value("variant", std::string());
  const auto online = item.find("online");
  if (online != item.end() && online->is_object()) {
    asset->online_known = true;
    asset->online_ok = online->value("ok", false);
    asset->online_note = online->value("note", std::string());
  }
}

// Which set of a pack a record belongs to ("" for the plain costume, "alt L" and "alt R" for the
// 20XX style alternates), and which pack: imports are one pack, each scanned disc or folder another.
std::string pack_key(const AssetRecord& asset) { return asset.source_kind == kDiscSource ? asset.source_id : kImportPack; }

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
    if (asset.source_kind == kDiscSource)
      item["disc"] = {{"path", asset.disc_path}, {"offset", asset.disc_offset}, {"length", asset.disc_length},
                      {"size", asset.disc_size}, {"mtime", asset.disc_mtime}};
    if (!asset.info.variant.empty()) item["variant"] = asset.info.variant;
    if (asset.online_known) item["online"] = {{"ok", asset.online_ok}, {"note", asset.online_note}};
    item["companions"] = json::array();
    for (const auto& companion : asset.companions) {
      item["companions"].push_back({{"kind", companion.kind},
                                      {"stored_path", companion.stored_path},
                                      {"sha256", companion.sha256},
                                      {"source_member", companion.source_member},
                                      {"status", "native_texture_override"}});
    }
    if (!asset.voice.stored_path.empty())
      item["voice"] = {{"stored_path", asset.voice.stored_path}, {"sha256", asset.voice.sha256},
                       {"source_member", asset.voice.source_member}, {"bank", asset.voice.bank}};
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
  state["disc_scans"] = json::array();
  for (const auto& scan : g_disc_scans)
    state["disc_scans"].push_back({{"path", scan.path}, {"size", scan.size}, {"mtime", scan.mtime}, {"rules", scan.rules}});
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
    if (found != g_assets.end()) {
      source += pick.first + "=" + pick.second + "=" + found->info.sha256;
      for (const auto& companion : found->companions) source += "+" + companion.kind + ":" + companion.sha256;
      if (!found->voice.stored_path.empty()) source += "+voice:" + found->voice.sha256;
      source += "\n";
    }
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
      load_voice(item, &asset);
      if (item.find("source") != item.end() && item["source"].is_object()) {
        asset.source_kind = item["source"].value("kind", std::string());
        asset.source_name = item["source"].value("name", std::string());
        asset.source_member = item["source"].value("member", std::string());
        asset.source_id = item["source"].value("id", std::string());
      }
      load_disc_fields(item, &asset);
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
  g_assets.clear(); g_disc_scans.clear(); g_profile = Profile{}; g_catalog_valid = g_profile_valid = true;
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
      load_voice(item, &asset);
      if (item.find("source") != item.end() && item["source"].is_object()) {
        asset.source_kind = item["source"].value("kind", std::string());
        asset.source_name = item["source"].value("name", std::string());
        asset.source_member = item["source"].value("member", std::string());
        asset.source_id = item["source"].value("id", std::string());
      }
      load_disc_fields(item, &asset);
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
    std::vector<DiscScan> scans;
    const auto scan_list = state.find("disc_scans");
    if (scan_list != state.end() && scan_list->is_array())
      for (const auto& scan : *scan_list)
        scans.push_back({scan.at("path").get<std::string>(), scan.value("size", (uint64_t)0),
                         scan.value("mtime", (int64_t)0), scan.value("rules", 1u)});
    g_assets = std::move(assets); g_profile = std::move(next); g_disc_scans = std::move(scans);
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
    if (entry.uncompressed > kMaxAssetBytes && dat_extension(entry.name)) {
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

struct FighterFamily {
  const char* file_code;
  const char* root_name;
  const char* display_name;
  const char* colors;
};
// These are the existing NTSC 1.02 costume files only. Defaults use an un-suffixed 5K root even
// though their disc filename ends in Nr. Bosses, wireframes, Sandbag, extra CSS slots, and base
// fighter data archives are intentionally absent. Colors are in the game's costume order (the
// order the character select screen cycles through), which is not the order of the file names.
constexpr FighterFamily families[] = {
    {"Ca", "Captain", "Captain Falcon", "Nr Gy Re Wh Gr Bu"},
    {"Cl", "Clink", "Young Link", "Nr Re Bu Wh Bk"},
    {"Dk", "Donkey", "Donkey Kong", "Nr Bk Re Bu Gr"},
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
    {"Pp", "Popo", "Ice Climbers (Popo)", "Nr Gr Or Re"},
    {"Pr", "Purin", "Jigglypuff", "Nr Re Bu Gr Ye"},
    {"Sk", "Seak", "Sheik", "Nr Re Bu Gr Wh"},
    {"Ss", "Samus", "Samus", "Nr Pi Bk Gr La"},
    {"Ys", "Yoshi", "Yoshi", "Nr Re Bu Ye Pi Aq"},
    {"Zd", "Zelda", "Zelda", "Nr Re Bu Gr Wh"},
};
struct ColorName { const char* code; const char* label; };
constexpr ColorName color_names[] = {
    {"Nr", "Default"}, {"Re", "Red"}, {"Bu", "Blue"}, {"Gr", "Green"},
    {"Wh", "White"}, {"Bk", "Black"}, {"Ye", "Yellow"}, {"Or", "Orange"},
    {"La", "Lavender"}, {"Pi", "Pink"}, {"Aq", "Aqua"}, {"Gy", "Gray"},
};
bool family_has_color(const FighterFamily& family, const char* code) {
  return (std::string(" ") + family.colors + " ").find(std::string(" ") + code + " ") != std::string::npos;
}

// ---- portraits without a costume file ----
// A portrait (the character select picture) or stock icon the player gives a costume slot on its
// own. It is a catalog entry of its own kind whose target is the slot plus "#portrait": it is
// chosen independently of the slot's skin, and a build that does not know the kind finds no disc
// file by that name and leaves it alone.
constexpr const char* kPortraitKind = "character_portrait";
constexpr const char* kPortraitSuffix = "#portrait";

// "PlFxGr.dat" from "PlFxGr.dat#portrait"
std::string portrait_slot(const std::string& target) {
  const size_t at = target.rfind(kPortraitSuffix);
  return at == std::string::npos ? target : target.substr(0, at);
}

struct SlotName { const FighterFamily* family = nullptr; const ColorName* color = nullptr; };
std::string slot_file(const SlotName& slot) {
  return std::string("Pl") + slot.family->file_code + slot.color->code + ".dat";
}
bool find_slot(const std::string& target_path, SlotName* out) {
  const std::string wanted = lower(target_path);
  for (const auto& family : families)
    for (const auto& color : color_names) {
      if (!family_has_color(family, color.code)) continue;
      const SlotName slot{&family, &color};
      if (lower(slot_file(slot)) == wanted) { *out = slot; return true; }
    }
  return false;
}

// ---- voice mods: fighter sound banks (docs/voice-mods.md) ----
// A fighter's voice and move sounds are one bank file on the disc, audio/us/<bank>.ssm for English
// and audio/<bank>.ssm for Japanese. The file is four big-endian words (size of the sound table,
// size of the sample data, number of sounds, id of the first sound), the sound table (per sound:
// channel count, sample rate, then 0x40 bytes per channel: loop flag, format, the loop, end and
// start addresses in nibbles from the start of the sample data, and the ADPCM state), padding to
// 32 bytes, and the samples. The game plays a sound by id, so a replacement has to keep the count
// and the first id; the loader reads the file in three parts and budgets the samples from a fixed
// table, so it also has to keep the table's shape and stay inside that budget.
//
// The rows below: the disc file, the game's bank number (its place in the loader's file list), the
// costume file codes of the fighters that use the bank, the sample bytes the loader budgets for it,
// and for the English and the Japanese file the number of sounds, the id of the first one, and
// which sounds have two channels. NTSC 1.02. The disc's own files are compared again at launch.
struct BankIdentity { uint32_t count, base; const char* stereo; };
struct VoiceBank { const char* file; int index; const char* fighters; uint32_t budget; BankIdentity language[2]; };
constexpr const char* kBankLanguages[2] = {"English", "Japanese"};
constexpr VoiceBank voice_banks[] = {
    {"captain.ssm", 6, "Ca", 443328, {{28, 336, ""}, {28, 336, "0"}}},
    {"clink.ssm", 7, "Cl", 298400, {{40, 364, ""}, {40, 364, ""}}},
    {"dk.ssm", 8, "Dk", 206368, {{23, 404, ""}, {23, 404, ""}}},
    {"drmario.ssm", 9, "Dr", 430560, {{36, 427, ""}, {37, 427, "0"}}},
    {"falco.ssm", 10, "Fc", 599712, {{53, 463, "2 33 34"}, {55, 464, "0 2 35 36"}}},
    {"fox.ssm", 11, "Fx", 573216, {{50, 516, "30 31"}, {54, 519, "0 34 35"}}},
    {"ice.ssm", 13, "Pp Nn", 477600, {{44, 589, ""}, {44, 596, "0"}}},
    {"kirby.ssm", 14, "Kb", 586208, {{52, 633, ""}, {52, 640, "0"}}},
    {"koopa.ssm", 15, "Kp", 526752, {{26, 685, "4 5 6 7 8 9 10 11 12 13 14 15 16"}, {26, 692, "0 4 5 6 7 8 9 10 11 12 13 14 15 16"}}},
    {"link.ssm", 16, "Lk", 328672, {{37, 711, ""}, {37, 718, "0"}}},
    {"luigi.ssm", 17, "Lg", 372992, {{35, 748, ""}, {35, 755, ""}}},
    {"mario.ssm", 18, "Mr", 372736, {{32, 783, ""}, {32, 790, ""}}},
    {"mars.ssm", 19, "Ms", 513088, {{46, 815, ""}, {46, 822, "0"}}},
    {"mewtwo.ssm", 20, "Mt", 562912, {{32, 861, "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20"}, {35, 868, "0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23"}}},
    {"ness.ssm", 21, "Ns", 509024, {{35, 893, "26"}, {35, 903, "26"}}},
    {"peach.ssm", 22, "Pe", 416736, {{30, 928, ""}, {30, 938, "0"}}},
    {"pichu.ssm", 23, "Pc", 580128, {{32, 958, "25 30"}, {32, 968, "0 25 30"}}},
    {"pikachu.ssm", 24, "Pk", 613088, {{36, 990, "30 35"}, {36, 1000, "0 30 35"}}},
    {"purin.ssm", 25, "Pr", 334528, {{23, 1026, ""}, {26, 1036, "0"}}},
    {"samus.ssm", 26, "Ss", 319040, {{25, 1049, "14"}, {25, 1062, "0 14"}}},
    {"zs.ssm", 27, "Zd Sk", 590176, {{62, 1074, ""}, {62, 1087, "0 1"}}},
    {"yoshi.ssm", 28, "Ys", 321472, {{33, 1136, ""}, {33, 1149, "0"}}},
    {"gw.ssm", 29, "Gw", 165344, {{21, 1169, ""}, {21, 1182, "0"}}},
    {"ganon.ssm", 30, "Gn", 427872, {{28, 1190, ""}, {28, 1203, "0"}}},
    {"emblem.ssm", 31, "Fe", 495520, {{45, 1218, ""}, {45, 1231, "0"}}},
};

const VoiceBank* family_bank(const FighterFamily& family) {
  for (const auto& bank : voice_banks)
    if ((std::string(" ") + bank.fighters + " ").find(std::string(" ") + family.file_code + " ") != std::string::npos)
      return &bank;
  return nullptr;
}
const VoiceBank* bank_by_file(const std::string& name) {
  const std::string wanted = lower(name);
  for (const auto& bank : voice_banks) if (wanted == bank.file) return &bank;
  return nullptr;
}
// "Fox", "Zelda and Sheik": whose bank it is, for a message.
std::string bank_fighters(const VoiceBank& bank) {
  std::string out;
  for (const auto& family : families)
    if (family_bank(family) == &bank) out += (out.empty() ? "" : " and ") + std::string(family.display_name);
  return out;
}

struct BankLayout {
  uint32_t table_size = 0, data_size = 0, count = 0, base = 0, data_offset = 0;
  std::vector<uint8_t> channels;   // per sound: 1 or 2
};

// Reads a bank's header and sound table the way the game's loader does, and refuses anything the
// loader would misread: a table that does not end where the header says, a sound with a channel
// count the game does not play, a format other than ADPCM, an address outside the sample data.
bool parse_bank(const std::vector<uint8_t>& bytes, BankLayout* out, std::string* error) {
  *out = BankLayout{};
  if (bytes.size() < 0x20) { *error = "The sound bank is too short to have a header."; return false; }
  out->table_size = be32(bytes.data()); out->data_size = be32(bytes.data() + 4);
  out->count = be32(bytes.data() + 8); out->base = be32(bytes.data() + 12);
  if (!out->count || out->count > 4096 || out->table_size < 0x10 || out->table_size > bytes.size() - 0x10) {
    *error = "The file does not start with a sound bank header."; return false;
  }
  const uint64_t table_end = 0x10ull + out->table_size;
  out->data_offset = (uint32_t)((table_end + 31) & ~31ull);
  if (!out->data_size || (uint64_t)out->data_offset + out->data_size > bytes.size()) {
    *error = "The sound bank's sample data runs past the end of the file."; return false;
  }
  uint64_t at = 0x10;
  for (uint32_t sound = 0; sound < out->count; ++sound) {
    if (at + 8 > table_end) { *error = "The sound table ends before sound " + std::to_string(sound) + "."; return false; }
    const uint32_t channels = be32(bytes.data() + at);
    if (channels != 1 && channels != 2) {
      *error = "Sound " + std::to_string(sound) + " has " + std::to_string(channels) + " channels; the game plays 1 or 2.";
      return false;
    }
    if (at + 8 + (uint64_t)channels * 0x40 > table_end) {
      *error = "The sound table ends inside sound " + std::to_string(sound) + "."; return false;
    }
    for (uint32_t channel = 0; channel < channels; ++channel) {
      const uint8_t* voice = bytes.data() + at + 8 + (size_t)channel * 0x40;
      const uint32_t format = be16(voice + 2), loop = be32(voice + 4), end = be32(voice + 8), start = be32(voice + 12);
      if (format != 0) { *error = "Sound " + std::to_string(sound) + " is not ADPCM; the game's banks are."; return false; }
      // Nibble addresses from the start of the sample data. The loader moves all three by the
      // bank's place in audio memory, the loop address also for a sound that does not loop.
      if (start > end || loop > end || (uint64_t)end >= (uint64_t)out->data_size * 2) {
        *error = "Sound " + std::to_string(sound) + " points outside the bank's sample data."; return false;
      }
    }
    out->channels.push_back((uint8_t)channels);
    at += 8 + (uint64_t)channels * 0x40;
  }
  if (at != table_end) {
    *error = "The sound table is " + std::to_string(out->table_size) + " bytes; its " + std::to_string(out->count) +
             " sounds take " + std::to_string(at - 0x10) + "."; return false;
  }
  return true;
}

std::vector<uint8_t> identity_channels(const BankIdentity& identity) {
  std::vector<uint8_t> channels(identity.count, 1);
  std::istringstream list(identity.stereo);
  for (uint32_t sound; list >> sound;) if (sound < channels.size()) channels[sound] = 2;
  return channels;
}

// The first sound whose channel count differs, or -1 when the two tables have the same shape.
int first_channel_difference(const std::vector<uint8_t>& game, const std::vector<uint8_t>& candidate) {
  if (game.size() != candidate.size()) return 0;
  for (size_t i = 0; i < game.size(); ++i) if (game[i] != candidate[i]) return (int)i;
  return -1;
}

// The room the loader takes for a bank's samples: a whole number of 32 byte blocks.
uint32_t bank_sample_room(const BankLayout& layout) { return (layout.data_size + 31) & ~31u; }

// Which of the game's fighter banks a parsed bank is, and which language's (0 English, 1 Japanese),
// from its own header: the same number of sounds, the same first id, the same channels per sound,
// and samples that fit the room the game keeps for the bank. `name_hint` (a file name) only makes
// the refusal say more. Null with the reason otherwise.
const VoiceBank* match_bank(const BankLayout& layout, const std::string& name_hint, int* language, std::string* error) {
  const VoiceBank* near_bank = nullptr; int near_language = 0, near_sound = 0;
  for (const auto& bank : voice_banks)
    for (int which = 0; which < 2; ++which) {
      const BankIdentity& identity = bank.language[which];
      if (identity.count != layout.count || identity.base != layout.base) continue;
      const int differs = first_channel_difference(identity_channels(identity), layout.channels);
      if (differs >= 0) { if (!near_bank) { near_bank = &bank; near_language = which; near_sound = differs; } continue; }
      if (bank_sample_room(layout) > bank.budget) {
        *error = std::string(bank.file) + " holds " + std::to_string(bank_sample_room(layout) / 1024) +
                 " KB of samples; the game keeps room for " + std::to_string(bank.budget / 1024) +
                 " KB for this bank. Shorten or resample the longest sounds.";
        return nullptr;
      }
      *language = which;
      return &bank;
    }
  if (near_bank) {
    const uint32_t have = (size_t)near_sound < layout.channels.size() ? layout.channels[(size_t)near_sound] : 0;
    *error = std::string(near_bank->file) + ": sound " + std::to_string(near_sound) + " has " + std::to_string(have) +
             (have == 1 ? " channel" : " channels") + "; the game's (" + kBankLanguages[near_language] + ") has " +
             std::to_string(have == 1 ? 2 : 1) + ". A voice bank keeps the channel count of every sound.";
    return nullptr;
  }
  const size_t slash = name_hint.find_last_of("/\\");
  if (const VoiceBank* named = bank_by_file(slash == std::string::npos ? name_hint : name_hint.substr(slash + 1))) {
    *error = std::string(named->file) + " here has " + std::to_string(layout.count) + " sounds from id " +
             std::to_string(layout.base) + "; the game's has " + std::to_string(named->language[0].count) + " from id " +
             std::to_string(named->language[0].base) + " (English) or " + std::to_string(named->language[1].count) +
             " from id " + std::to_string(named->language[1].base) +
             " (Japanese). A voice bank keeps every sound of the bank it replaces, so each sound id means the same sound.";
    return nullptr;
  }
  *error = "This is not one of the game's fighter sound banks (" + std::to_string(layout.count) + " sounds from id " +
           std::to_string(layout.base) + ").";
  return nullptr;
}

// The bank as it is served: the file up to the end of its samples, the samples filled to a whole
// 32 byte block (the header says the filled size), then zeros up to `length`, the one length the
// file table carries for this disc file.
std::vector<uint8_t> served_bank(const std::vector<uint8_t>& bytes, const BankLayout& layout, uint32_t length) {
  std::vector<uint8_t> out(bytes.begin(), bytes.begin() + layout.data_offset + layout.data_size);
  out.resize((size_t)layout.data_offset + bank_sample_room(layout), 0);
  put_be32(out.data() + 4, bank_sample_room(layout));
  if (out.size() < length) out.resize(length, 0);
  return out;
}

// The slot a picture's name identifies. First the costume's own file code ("plfxgr"), which is
// what packs made beside a skin use; otherwise one fighter name and one color word. A name that
// fits two costumes, or a color the fighter does not have, identifies nothing.
bool portrait_slot_from_name_impl(const std::string& raw, std::string* slot, std::string* kind) {
  std::string name = lower(raw);
  const size_t dot = name.find_last_of('.');
  const size_t slash = name.find_last_of("/\\");
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) name.resize(dot);
  auto word_at = [&](size_t at, size_t length) {
    return (at == 0 || !std::isalnum((unsigned char)name[at - 1])) &&
           (at + length >= name.size() || !std::isalnum((unsigned char)name[at + length]));
  };
  *kind = name.find("stock") != std::string::npos ? "stock" : "csp";
  std::vector<SlotName> found;
  auto add = [&](const SlotName& candidate) {
    for (const auto& have : found)
      if (have.family == candidate.family && have.color == candidate.color) return;
    found.push_back(candidate);
  };
  for (size_t at = name.find("pl"); at != std::string::npos; at = name.find("pl", at + 1)) {
    if (at + 6 > name.size() || !word_at(at, 6)) continue;
    for (const auto& family : families) {
      if (lower(family.file_code) != name.substr(at + 2, 2)) continue;
      for (const auto& color : color_names)
        if (lower(color.code) == name.substr(at + 4, 2) && family_has_color(family, color.code))
          add({&family, &color});
    }
  }
  if (found.size() == 1) { *slot = slot_file(found[0]); return true; }
  if (found.size() > 1) return false;

  struct Alias { const char* text; const char* code; };
  // Longest first: a match is blanked out, so "falcon" never also reads as "falco", nor "dr mario"
  // as "mario".
  static constexpr Alias fighters[] = {
      {"mr. game & watch", "Gw"}, {"mr game and watch", "Gw"}, {"game and watch", "Gw"}, {"game & watch", "Gw"},
      {"captain falcon", "Ca"}, {"ice climbers", "Pp"}, {"iceclimbers", "Pp"}, {"donkey kong", "Dk"},
      {"young link", "Cl"}, {"gameandwatch", "Gw"}, {"jigglypuff", "Pr"}, {"younglink", "Cl"},
      {"ganondorf", "Gn"}, {"gamewatch", "Gw"}, {"dr. mario", "Dr"}, {"dr mario", "Dr"}, {"c.falcon", "Ca"},
      {"cfalcon", "Ca"}, {"drmario", "Dr"}, {"pikachu", "Pk"}, {"captain", "Ca"}, {"bowser", "Kp"},
      {"falcon", "Ca"}, {"mewtwo", "Mt"}, {"donkey", "Dk"}, {"jiggly", "Pr"}, {"ylink", "Cl"}, {"clink", "Cl"},
      {"ganon", "Gn"}, {"kirby", "Kb"}, {"luigi", "Lg"}, {"mario", "Mr"}, {"marth", "Ms"}, {"pichu", "Pc"},
      {"peach", "Pe"}, {"purin", "Pr"}, {"sheik", "Sk"}, {"samus", "Ss"}, {"yoshi", "Ys"}, {"zelda", "Zd"},
      {"falco", "Fc"}, {"koopa", "Kp"}, {"link", "Lk"}, {"ness", "Ns"}, {"nana", "Nn"}, {"popo", "Pp"},
      {"puff", "Pr"}, {"g&w", "Gw"}, {"gnw", "Gw"}, {"roy", "Fe"}, {"fox", "Fx"}, {"doc", "Dr"}, {"ics", "Pp"},
      {"dk", "Dk"},
  };
  static constexpr Alias colors[] = {
      {"lavender", "La"}, {"default", "Nr"}, {"neutral", "Nr"}, {"original", "Nr"}, {"vanilla", "Nr"},
      {"normal", "Nr"}, {"orange", "Or"}, {"yellow", "Ye"}, {"purple", "La"}, {"black", "Bk"}, {"white", "Wh"},
      {"green", "Gr"}, {"blue", "Bu"}, {"pink", "Pi"}, {"aqua", "Aq"}, {"cyan", "Aq"}, {"gray", "Gy"},
      {"grey", "Gy"}, {"red", "Re"},
  };
  auto scan = [&](const Alias* aliases, size_t count, std::vector<std::string>* codes) {
    for (size_t i = 0; i < count; ++i) {
      const std::string text = aliases[i].text;
      for (size_t at = name.find(text); at != std::string::npos; at = name.find(text, at + 1)) {
        if (!word_at(at, text.size())) continue;
        if (std::find(codes->begin(), codes->end(), aliases[i].code) == codes->end()) codes->push_back(aliases[i].code);
        name.replace(at, text.size(), text.size(), ' ');
      }
    }
  };
  std::vector<std::string> fighter_codes, color_codes;
  scan(fighters, std::size(fighters), &fighter_codes);
  scan(colors, std::size(colors), &color_codes);
  if (fighter_codes.size() != 1 || color_codes.size() != 1) return false;
  for (const auto& family : families) {
    if (fighter_codes[0] != family.file_code) continue;
    for (const auto& color : color_names)
      if (color_codes[0] == color.code && family_has_color(family, color.code)) {
        *slot = slot_file({&family, &color}); return true;
      }
  }
  return false;
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

// A costume's skeleton places the fighter's hurtboxes and hitboxes, so online it must match the
// vanilla slot exactly: same joints, same hierarchy, same rest transforms, same transform flags.
// Flags that only affect drawing (hidden, lighting, texgen, specular, opaque/translucent classes)
// may differ. Anything else and the costume stays offline; online the vanilla model is loaded.
namespace skeleton {
// 0x1, 0x2 and 0x4 (skeleton, skeleton root, envelope model) only say how the mesh is skinned for
// drawing. Model tools rewrite them on export, which kept every re-exported skin off online.
// 0x8 (classical scaling) is not the costume's to decide: every time an animation is put on a
// fighter joint the game sets or clears that bit from the animation itself (lbanim.c), so the value
// in the file does not last. Model tools set it on every joint when they export, and that alone
// kept nine of the ten rejected costumes on a widely used training pack off online.
constexpr uint32_t kVisualFlags = 0x1u | 0x2u | 0x4u | 0x8u | 0x10u | 0x20u | 0x40u | 0x80u | 0x100u | 0x10000u |
                                  0x40000u | 0x80000u | 0x100000u | 0x70000000u;
// A skin exported from a model tool carries the same rest pose with different rounding in the last
// digits, and a bit-for-bit comparison refused it. The nine values (rotation, scale, translation)
// are compared as numbers with a small tolerance; a real change of a joint is far larger.
// Returns which parts of the rest pose differ (0 when none do), so the reason can say what changed.
constexpr uint32_t kRestRotated = 1, kRestResized = 2, kRestMoved = 4;
inline uint32_t rest_pose_difference(const uint8_t* p, const uint8_t* q) {
  uint32_t differs = 0;
  for (int i = 0; i < 9; ++i) {
    uint32_t ua = be32(p + i * 4), ub = be32(q + i * 4);
    float fa, fb; std::memcpy(&fa, &ua, 4); std::memcpy(&fb, &ub, 4);
    if (ua == ub) continue;
    const uint32_t part = i < 3 ? kRestRotated : i < 6 ? kRestResized : kRestMoved;
    if (!std::isfinite(fa) || !std::isfinite(fb)) { differs |= part; continue; }
    const float scale = std::max(1.0f, std::max(std::fabs(fa), std::fabs(fb)));
    // The first three values are the rest rotation, which a fighter's animations replace on every
    // frame, so an exporter's rounding there gets more room than in scale and translation.
    const float tolerance = i < 3 ? 1e-2f : 1e-3f;
    if (std::fabs(fa - fb) > tolerance * scale) differs |= part;
  }
  return differs;
}
// `relocated` receives the data offsets the relocation table lists, sorted. A pointer in this format
// is an offset into the data block, so a joint stored at data offset 0 is pointed to by the value 0:
// only the relocation table tells that pointer from "no joint".
bool share_joint_root(const std::vector<uint8_t>& bytes, uint32_t* data_size, uint32_t* root,
                      std::vector<uint32_t>* relocated, std::string* error) {
  if (bytes.size() < 0x20) { *error = "DAT header is truncated."; return false; }
  const uint64_t dsize = be32(bytes.data() + 4), relocations = be32(bytes.data() + 8),
                 roots = be32(bytes.data() + 12), references = be32(bytes.data() + 16);
  const uint64_t root_table = 0x20ull + dsize + relocations * 4ull;
  const uint64_t strings = root_table + (roots + references) * 8ull;
  if (roots > 1024 || references > 1024 || strings > bytes.size()) { *error = "DAT tables point outside the file."; return false; }
  relocated->resize((size_t)relocations);
  for (uint64_t i = 0; i < relocations; ++i) (*relocated)[(size_t)i] = be32(&bytes[(size_t)(0x20ull + dsize + i * 4ull)]);
  std::sort(relocated->begin(), relocated->end());
  for (uint64_t i = 0; i < roots; ++i) {
    const uint8_t* record = &bytes[(size_t)(root_table + i * 8)];
    const uint64_t name = strings + be32(record + 4);
    if (name >= bytes.size()) continue;
    const char* s = (const char*)&bytes[(size_t)name];
    const size_t n = strnlen(s, bytes.size() - (size_t)name);
    static constexpr char suffix[] = "_Share_joint";
    if (n >= sizeof suffix - 1 && std::memcmp(s + n - (sizeof suffix - 1), suffix, sizeof suffix - 1) == 0) {
      *data_size = (uint32_t)dsize; *root = be32(record);
      if (*root + 0x40ull > dsize) { *error = "Skeleton root points outside the data block."; return false; }
      return true;
    }
  }
  *error = "No _Share_joint skeleton root."; return false;
}
inline bool is_pointer(const std::vector<uint32_t>& relocated, uint32_t slot) {
  return std::binary_search(relocated.begin(), relocated.end(), slot);
}
// The number of joints in one tree, stopping past 1024 (a cycle or a broken file). Only used to
// word the reason once the two trees are known to differ in shape.
uint32_t count_joints(const std::vector<uint8_t>& bytes, uint32_t size, const std::vector<uint32_t>& relocated,
                      uint32_t root) {
  uint32_t count = 0;
  std::vector<uint32_t> stack{root};
  while (!stack.empty() && count <= 1024) {
    const uint32_t x = stack.back(); stack.pop_back();
    if (x + 0x40ull > size) continue;
    ++count;
    const uint8_t* p = &bytes[0x20ull + x];
    if (is_pointer(relocated, x + 0x0C)) stack.push_back(be32(p + 0x0C));
    if (is_pointer(relocated, x + 0x08)) stack.push_back(be32(p + 0x08));
  }
  return count;
}
// `a` is the standard file, `b` the costume. Joints are numbered depth first (child, then next),
// from 0 at the root, and every refusal names the joint so the player can find it in a model tool.
bool same_tree(const std::vector<uint8_t>& a, uint32_t asize, uint32_t ja, const std::vector<uint32_t>& arel,
               const std::vector<uint8_t>& b, uint32_t bsize, uint32_t jb, const std::vector<uint32_t>& brel,
               uint32_t* joints, std::string* error) {
  // Iterative walk of both trees in lockstep (child first, then next), bounded against cycles.
  // A child or next slot holds a joint only when the relocation table lists the slot: the value 0
  // is a real pointer to a joint stored at data offset 0, and reading it as "no joint" called a
  // matching skeleton a different shape.
  struct Pair { uint32_t x, y; bool has_x, has_y; };
  std::vector<Pair> stack{{ja, jb, true, true}};
  while (!stack.empty()) {
    const Pair top = stack.back(); stack.pop_back();
    const uint32_t x = top.x, y = top.y;
    if (!top.has_x || !top.has_y) {
      if (top.has_x || top.has_y) {
        const uint32_t standard = count_joints(a, asize, arel, ja), costume = count_joints(b, bsize, brel, jb);
        if (standard != costume)
          *error = "Skeleton joint count differs from the vanilla costume (" + std::to_string(costume) +
                   " instead of " + std::to_string(standard) + ").";
        else
          *error = "Skeleton joint " + std::to_string(*joints) + " hierarchy differs from the vanilla costume.";
        return false;
      }
      continue;
    }
    if (x + 0x40ull > asize || y + 0x40ull > bsize) { *error = "Skeleton joint points outside the data block."; return false; }
    if (++*joints > 1024) { *error = "Skeleton has more than 1024 joints."; return false; }
    const uint8_t* p = &a[0x20ull + x];
    const uint8_t* q = &b[0x20ull + y];
    if ((be32(p + 4) & ~kVisualFlags) != (be32(q + 4) & ~kVisualFlags)) {
      *error = "Skeleton joint " + std::to_string(*joints - 1) + " flags differ from the vanilla costume."; return false;
    }
    if (const uint32_t differs = rest_pose_difference(p + 0x14, q + 0x14)) {
      std::string what;
      if (differs & kRestRotated) what += "rotated";
      if (differs & kRestResized) what += what.empty() ? "resized" : ", resized";
      if (differs & kRestMoved) what += what.empty() ? "moved" : ", moved";
      *error = "Skeleton joint " + std::to_string(*joints - 1) + " rest pose differs from the vanilla costume (" + what + ").";
      return false;
    }
    stack.push_back({be32(p + 0x0C), be32(q + 0x0C), is_pointer(arel, x + 0x0C), is_pointer(brel, y + 0x0C)});   // next sibling
    stack.push_back({be32(p + 0x08), be32(q + 0x08), is_pointer(arel, x + 0x08), is_pointer(brel, y + 0x08)});   // first child
  }
  return true;
}
// The game stops (pobj.c, assertion "jp->envelopemtx") when it draws an envelope mesh whose blended
// matrix names a joint that has no inverse bind matrix: the joint description's pointer at 0x38 is
// what the loader copies into the joint, and a blend of two or more joints multiplies by it for
// every joint in the list. A console stops the same way, so such a costume is never handed to the
// game. A list whose first weight is 1 takes the game's unchecked path and is left alone here.
// Only a certain defect refuses: anything that points outside the data block is skipped, since the
// other checks own malformed files.
bool envelopes_bound(const std::vector<uint8_t>& bytes, uint32_t size, const std::vector<uint32_t>& relocated,
                     uint32_t root, std::string* error) {
  constexpr uint32_t kNoMeshFlags = 0x20u | 0x4000u;   // particle and spline joints keep other data in the mesh slot
  constexpr size_t kMaxWalk = 65536;                   // against cycles in a broken file
  std::vector<uint32_t> order;                         // joints depth first (child, then next), as same_tree numbers them
  std::vector<uint32_t> stack{root};
  while (!stack.empty() && order.size() <= 1024) {
    const uint32_t x = stack.back(); stack.pop_back();
    if (x + 0x40ull > size) continue;
    order.push_back(x);
    const uint8_t* p = &bytes[0x20ull + x];
    if (is_pointer(relocated, x + 0x0C)) stack.push_back(be32(p + 0x0C));
    if (is_pointer(relocated, x + 0x08)) stack.push_back(be32(p + 0x08));
  }
  if (order.size() > 1024) return true;
  size_t walked = 0;
  for (const uint32_t x : order) {
    const uint8_t* p = &bytes[0x20ull + x];
    if ((be32(p + 4) & kNoMeshFlags) || !is_pointer(relocated, x + 0x10)) continue;
    for (uint32_t d = be32(p + 0x10);; d = be32(&bytes[0x20ull + d + 0x04])) {          // display objects
      if (d + 0x10ull > size || ++walked > kMaxWalk) break;
      if (is_pointer(relocated, d + 0x0C))
        for (uint32_t o = be32(&bytes[0x20ull + d + 0x0C]);; o = be32(&bytes[0x20ull + o + 0x04])) {   // polygon objects
          if (o + 0x18ull > size || ++walked > kMaxWalk) break;
          const uint8_t* po = &bytes[0x20ull + o];
          const uint32_t type = (((uint32_t)po[0x0C] << 8) | po[0x0D]) & 0x3000u;
          if (type == 0x2000u && is_pointer(relocated, o + 0x14)) {
            // The envelope list: pointers to arrays of (joint, weight), each ended by a slot that is no pointer.
            for (uint64_t slot = be32(po + 0x14); slot + 4 <= size && is_pointer(relocated, (uint32_t)slot); slot += 4) {
              if (++walked > kMaxWalk) break;
              const uint64_t first = be32(&bytes[(size_t)(0x20ull + slot)]);
              if (first + 8 > size || !is_pointer(relocated, (uint32_t)first)) continue;
              const uint32_t bits = be32(&bytes[(size_t)(0x20ull + first + 4)]);
              float weight; std::memcpy(&weight, &bits, 4);
              if (weight >= 1.0f - FLT_EPSILON) continue;
              for (uint64_t e = first; e + 8 <= size && is_pointer(relocated, (uint32_t)e); e += 8) {
                if (++walked > kMaxWalk) break;
                const uint32_t joint = be32(&bytes[(size_t)(0x20ull + e)]);
                if (joint + 0x40ull > size || is_pointer(relocated, joint + 0x38)) continue;
                const auto found = std::find(order.begin(), order.end(), joint);
                *error = "A mesh is skinned to bone " +
                         (found == order.end() ? "at " + std::to_string(joint) : std::to_string(found - order.begin())) +
                         ", which has no bind matrix: the game would stop when it is drawn.";
                return false;
              }
            }
          }
          if (!is_pointer(relocated, o + 0x04)) break;
        }
      if (!is_pointer(relocated, d + 0x04)) break;
    }
  }
  return true;
}
}  // namespace skeleton

}  // namespace

// Public: whether the game can draw this costume at all. Offline and online alike, unlike the
// skeleton comparison below: a file that fails here stops the game on any machine.
bool costume_draw_safe(const std::vector<uint8_t>& candidate, std::string* error) {
  std::string local; if (!error) error = &local;
  uint32_t size = 0, root = 0;
  std::vector<uint32_t> relocated;
  std::string unused;
  // No skeleton root or a truncated file is some other check's finding, not a drawing defect.
  if (!skeleton::share_joint_root(candidate, &size, &root, &relocated, &unused) || size + 0x20ull > candidate.size()) return true;
  return skeleton::envelopes_bound(candidate, size, relocated, root, error);
}

// Public: the Source Port asks the same question about a live pack's costume files.
bool costume_skeleton_matches(const std::vector<uint8_t>& clean, const std::vector<uint8_t>& candidate,
                              std::string* error) {
  std::string local; if (!error) error = &local;
  uint32_t asize = 0, aroot = 0, bsize = 0, broot = 0, joints = 0;
  std::vector<uint32_t> arel, brel;
  if (!skeleton::share_joint_root(clean, &asize, &aroot, &arel, error) ||
      !skeleton::share_joint_root(candidate, &bsize, &broot, &brel, error)) return false;
  if (asize + 0x20ull > clean.size() || bsize + 0x20ull > candidate.size()) { *error = "DAT data block is truncated."; return false; }
  if (!skeleton::same_tree(clean, asize, aroot, arel, candidate, bsize, broot, brel, &joints, error)) return false;
  *error = std::to_string(joints) + " joints match";
  return true;
}

// The player reads this beside "Online: standard costume", so it only has to name the difference.
// The full sentence stays in the log.
std::string online_reason_short(const std::string& detail) {
  if (detail.empty()) return "the standard file could not be read";
  if (detail.find("joints match") != std::string::npos) return detail;
  // "Skeleton joint 12 ... (moved)." names the bone, so a player knows which one to put back, and
  // the bracket (what changed, or the two joint counts) is carried over as written.
  const size_t open = detail.find('('), close = detail.find(')');
  const std::string bracket = open != std::string::npos && close != std::string::npos && close > open
                                  ? " " + detail.substr(open, close - open + 1) : std::string();
  static constexpr char kJoint[] = "Skeleton joint ";
  constexpr size_t kJointLength = sizeof kJoint - 1;
  if (detail.find("joint count differs") != std::string::npos) return "bone count differs" + bracket;
  if (detail.compare(0, kJointLength, kJoint) == 0) {
    size_t end = kJointLength;
    while (end < detail.size() && std::isdigit((unsigned char)detail[end])) ++end;
    if (end > kJointLength) {
      const std::string bone = "bone " + detail.substr(kJointLength, end - kJointLength) + ": ";
      if (detail.find("rest pose differs") != std::string::npos) return bone + "rest pose differs" + bracket;
      if (detail.find("flags differ") != std::string::npos) return bone + "settings differ";
      if (detail.find("hierarchy differs") != std::string::npos) return bone + "shape differs";
    }
  }
  if (detail.find("more than 1024 joints") != std::string::npos) return "skeleton too large";
  if (detail.find("No _Share_joint") != std::string::npos) return "no skeleton found";
  if (detail.find("non-texture data") != std::string::npos) return "changes more than textures";
  if (detail.find("changes layout") != std::string::npos) return "changes the file layout";
  if (detail.find("identical to the clean resource") != std::string::npos) return "same as the standard file";
  std::string text = detail;
  while (!text.empty() && (text.back() == '.' || text.back() == ' ')) text.pop_back();
  return text;
}

namespace {

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

// A picture the player gave a skin afterwards (set_skin_portrait) is stored under a name with its
// hash in it ("csp-0123456789abcdef.png"); the ones an import or a scan brings are csp.png and
// stock.png. So a scan that writes its csp.png again never touches the player's picture.
std::string player_picture_file(const std::string& kind, const std::string& digest) {
  return kind + "-" + digest.substr(0, 16) + ".png";
}
bool player_picture(const AssetRecord::Companion& companion) {
  if (companion.kind != "csp" && companion.kind != "stock") return false;
  const size_t slash = companion.stored_path.find_last_of('/');
  return companion.stored_path.substr(slash == std::string::npos ? 0 : slash + 1) != companion.kind + ".png";
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
    if (!dat_extension(entry.name)) continue;
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
    if (!dat_extension(entry.name)) continue;
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
  if (name.size() < 7 || !dat_extension(name)) return false;
  const std::string stem = name.substr(0, name.size() - 4);
  static constexpr struct { const char* token; const char* path; const char* label; } resources[] = {
      {"grps1", "GrPs1.dat", "Pokémon Stadium: Fire"},
      {"grps2", "GrPs2.dat", "Pokémon Stadium: Water"},
      {"grps3", "GrPs3.dat", "Pokémon Stadium: Rock"},
      {"grps4", "GrPs4.dat", "Pokémon Stadium: Grass"},
      {"grps", "GrPs.dat", "Pokémon Stadium: Base"},
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
    // A disc-backed record has no stored copy to refresh: an import of the same file is its own entry.
    return a.info.sha256 == digest && a.info.target_path == dat.target_path && a.source_kind != kDiscSource;
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
  g_message = asset.info.name + " imported as " + dat.character + ": " + dat.costume +
              " (" + dat.target_path + ")" +
              (auto_selected ? " and automatically selected." :
                               "; the existing selection was preserved.");
  result.ok = true; result.asset_id = id; result.message = g_message;
  return result;
}

// One entry per costume slot holds its portrait and its stock icon. Setting a picture again
// replaces that picture and keeps the other.
ImportResult install_portrait_locked(const std::string& slot_target, const std::string& kind,
                                     const std::vector<uint8_t>& png, const std::string& source_name) {
  ImportResult result;
  std::string error;
  if (!mutable_profile_locked(&error)) { result.message = error; return result; }
  SlotName slot;
  if (!find_slot(slot_target, &slot)) {
    result.message = "That is not a costume of the game: " + slot_target; return result;
  }
  if (kind != "csp" && kind != "stock") { result.message = "A picture is a portrait or a stock icon."; return result; }
  if (!png_companion(png, &error)) { result.message = error; return result; }
  const std::string digest = sha256(png);
  if (digest.empty()) { result.message = "The picture could not be hashed."; return result; }
  const std::string file = slot_file(slot);
  const std::string id = "portrait-" + file.substr(0, file.size() - 4);
  const std::string target = file + kPortraitSuffix;
  AssetRecord::Companion picture;
  picture.kind = kind;
  picture.stored_path = (fs::path(L"assets") / fs::u8path(id) / L"companions" /
                         (kind == "csp" ? L"csp.png" : L"stock.png")).generic_u8string();
  picture.sha256 = digest;
  picture.source_member = source_name;

  auto existing = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& a) { return a.info.id == id; });
  const bool had = existing != g_assets.end();
  AssetRecord asset = had ? *existing : AssetRecord{};
  bool same_picture = false;
  for (const auto& companion : asset.companions) same_picture |= companion.kind == kind && companion.sha256 == digest;
  asset.companions.erase(std::remove_if(asset.companions.begin(), asset.companions.end(),
      [&](const AssetRecord::Companion& c) { return c.kind == kind; }), asset.companions.end());
  asset.companions.push_back(picture);
  std::stable_sort(asset.companions.begin(), asset.companions.end(),
      [](const AssetRecord::Companion& a, const AssetRecord::Companion& b) { return a.kind == "csp" && b.kind != "csp"; });
  asset.info.id = id;
  asset.info.kind = kPortraitKind;
  asset.info.target_path = target;
  asset.info.character = slot.family->display_name;
  asset.info.costume = slot.color->label;
  if (asset.info.name.empty()) asset.info.name = std::string(slot.family->display_name) + ", " + slot.color->label + " pictures";
  asset.info.available = true;
  asset.info.availability_message.clear();
  asset.info.roots.clear();
  asset.info.unsupported_companions.clear();
  for (const auto& companion : asset.companions)
    asset.info.unsupported_companions.push_back(std::string(companion.kind == "csp" ? "Portrait: " : "Stock icon: ") +
                                               companion.source_member);
  // The entry's own file is its first picture (the portrait when it has one): what the catalog
  // checks when it lists the entry.
  asset.stored_path = asset.companions.front().stored_path;
  asset.info.sha256 = asset.companions.front().sha256;
  asset.source_kind = "png";
  asset.source_name = source_name;

  if (!write_atomic(g_root / fs::u8path(picture.stored_path), png.data(), png.size(), &error)) {
    result.message = error; return result;
  }
  const AssetRecord previous = had ? *existing : AssetRecord{};
  if (had) *existing = asset; else g_assets.push_back(asset);
  if (!save_catalog_locked(&error)) {
    if (had) *std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& a) { return a.info.id == id; }) = previous;
    else g_assets.pop_back();
    result.message = error; return result;
  }
  const auto current = g_profile.selections.find(target);
  if (current == g_profile.selections.end() || current->second != id) {
    Profile before = g_profile;
    g_profile.selections[target] = id;
    ++g_profile.generation;
    if (!save_profile_locked(&error)) {
      g_profile = std::move(before);
      result.message = "The picture was saved, but its selection could not be: " + error; return result;
    }
  } else if (!same_picture) {
    ++g_profile.generation;   // the same entry, a different picture
    save_profile_locked(&error);
  }
  g_message = std::string(kind == "csp" ? "Portrait" : "Stock icon") + " for " + slot.family->display_name + ", " +
              slot.color->label + " set from " + source_name + ".";
  result.ok = true; result.already_present = same_picture; result.asset_id = id; result.message = g_message;
  return result;
}

// ---- voice mods: import ----

// A sound bank checked for one costume slot before anything is written: it parses, it is one of the
// game's fighter banks, and that bank is the fighter's of `slot_target`.
struct VoiceCheck { const VoiceBank* bank = nullptr; int language = 0; BankLayout layout; };
bool check_voice(const std::vector<uint8_t>& ssm, const std::string& source_name, const std::string& slot_target,
                 VoiceCheck* out, std::string* error) {
  if (!parse_bank(ssm, &out->layout, error)) return false;
  out->bank = match_bank(out->layout, source_name, &out->language, error);
  if (!out->bank) return false;
  if (slot_target.empty()) return true;
  SlotName slot;
  if (!find_slot(slot_target, &slot) || family_bank(*slot.family) != out->bank) {
    *error = std::string(out->bank->file) + " is the sound bank of " + bank_fighters(*out->bank) + "; this skin is " +
             (find_slot(slot_target, &slot) ? std::string(slot.family->display_name) + "'s." : std::string("another fighter's."));
    return false;
  }
  return true;
}

// Gives an installed skin a sound bank, or replaces the one it has. The file is stored beside the
// skin's pictures; the record's "voice" entry names it.
ImportResult attach_voice_locked(const std::string& asset_id, const std::vector<uint8_t>& ssm,
                                 const std::string& source_name) {
  ImportResult result;
  std::string error;
  if (!mutable_profile_locked(&error)) { result.message = error; return result; }
  auto asset = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
    return item.info.id == asset_id && item.info.kind == "character_costume";
  });
  if (asset == g_assets.end()) { result.message = "A sound bank belongs to an installed skin; that skin is not in the list."; return result; }
  if (ssm.empty() || ssm.size() > kMaxAssetBytes) { result.message = "The sound bank is empty or larger than the 64 MB safety limit."; return result; }
  VoiceCheck check;
  if (!check_voice(ssm, source_name, asset->info.target_path, &check, &error)) {
    result.message = "Sound bank refused: " + error; return result;
  }
  const std::string digest = sha256(ssm);
  if (digest.empty()) { result.message = "The sound bank could not be hashed."; return result; }
  AssetRecord::Voice voice;
  voice.stored_path = (fs::path(L"assets") / fs::u8path(asset->info.id) / L"companions" / L"voice.ssm").generic_u8string();
  voice.sha256 = digest;
  voice.source_member = source_name;
  voice.bank = check.bank->file;
  const bool same = asset->voice.sha256 == digest;
  if (!write_atomic(g_root / fs::u8path(voice.stored_path), ssm.data(), ssm.size(), &error)) { result.message = error; return result; }
  const AssetRecord previous = *asset;
  const Profile previous_profile = g_profile;
  asset->voice = voice;
  asset->info.voice = voice.bank;
  asset->info.voice_source = voice.source_member;
  if (!same) ++g_profile.generation;   // the same skin, another voice
  if (!save_state_locked(&error)) { *asset = previous; g_profile = previous_profile; result.message = error; return result; }
  g_message = std::string(check.bank->file) + " (" + std::to_string(check.layout.count) + " sounds, fits the " +
              kBankLanguages[check.language] + " bank) is the voice of " + asset->info.name + ".";
  result.ok = true; result.already_present = same; result.asset_id = asset->info.id; result.message = g_message;
  return result;
}

// The installed skin a bank that came without a costume file belongs to: the selected skin of the
// costume `slot_hint` names, or with no hint the only selected skin among the bank's fighters, or
// the only installed one. Null with advice when that does not single one out.
const AssetRecord* voice_target_locked(const VoiceBank& bank, const std::string& slot_hint, std::string* error) {
  const auto selected_skin = [&](const std::string& slot) -> const AssetRecord* {
    const auto pick = g_profile.selections.find(slot);
    if (pick == g_profile.selections.end() || pick->second == kVanillaSelection) return nullptr;
    for (const auto& item : g_assets)
      if (item.info.id == pick->second && item.info.kind == "character_costume" && selection_key(item) == slot) return &item;
    return nullptr;
  };
  std::vector<const AssetRecord*> selected, installed;
  SlotName hinted;
  const bool has_hint = !slot_hint.empty() && find_slot(slot_hint, &hinted);
  if (has_hint && family_bank(*hinted.family) != &bank) {
    *error = std::string(bank.file) + " is the sound bank of " + bank_fighters(bank) + ", not of " +
             hinted.family->display_name + ".";
    return nullptr;
  }
  for (const auto& item : g_assets) {
    SlotName slot;
    if (item.info.kind != "character_costume" || !find_slot(item.info.target_path, &slot) ||
        family_bank(*slot.family) != &bank) continue;
    if (has_hint && lower(item.info.target_path) != lower(slot_file(hinted))) continue;
    installed.push_back(&item);
    if (selected_skin(item.info.target_path) == &item) selected.push_back(&item);
  }
  if (selected.size() == 1) return selected[0];
  if (selected.empty() && installed.size() == 1) return installed[0];
  const std::string who = has_hint ? std::string(hinted.family->display_name) + ", " + hinted.color->label : bank_fighters(bank);
  *error = installed.empty() ?
      std::string(bank.file) + " is a voice for " + who + ", and no skin is installed for it. A voice belongs to a skin: "
          "put the costume file in the same zip, or import the skin first." :
      std::string(bank.file) + " is a voice for " + who + ", and " + std::to_string(installed.size()) +
          " skins could take it. Name the file after the costume (for example \"Fox Green.ssm\"), or select the one skin it belongs to first.";
  return nullptr;
}

// A bank that came without a costume file. `slot_hint` is a costume slot ("PlFxGr.dat") when the
// manifest named one; otherwise the first of `names` (file names) that names a costume is the hint.
ImportResult attach_loose_voice_locked(const std::vector<uint8_t>& ssm, const std::string& source_name,
                                       std::string slot_hint, const std::vector<std::string>& names) {
  ImportResult result;
  std::string error;
  if (!mutable_profile_locked(&error)) { result.message = error; return result; }
  VoiceCheck check;
  if (!check_voice(ssm, source_name, {}, &check, &error)) { result.message = "Sound bank refused: " + error; return result; }
  for (size_t i = 0; slot_hint.empty() && i < names.size(); ++i) {
    std::string slot, kind;
    if (portrait_slot_from_name_impl(names[i], &slot, &kind)) slot_hint = slot;
  }
  const AssetRecord* target = voice_target_locked(*check.bank, slot_hint, &error);
  if (!target) { result.message = error; return result; }
  return attach_voice_locked(target->info.id, ssm, source_name);
}

// The optional manifest of a mod zip (mod.json): the mod's name, the costume it is for when the zip
// has no costume file, and which bank is the voice when the zip has several.
struct ModManifest { std::string name, slot, voice; };
bool parse_mod_manifest(const std::vector<uint8_t>& bytes, ModManifest* out, std::string* error) {
  json root = json::parse(bytes.begin(), bytes.end(), nullptr, false);
  if (!root.is_object()) { *error = "mod.json is not a JSON object."; return false; }
  const auto text = [&](const char* key, std::string* value) {
    const auto found = root.find(key);
    if (found == root.end()) return true;
    if (!found->is_string()) return false;
    *value = found->get<std::string>();
    return value->size() <= 256 && std::none_of(value->begin(), value->end(), [](char c) { return (unsigned char)c < 0x20; });
  };
  if (!text("name", &out->name) || !text("slot", &out->slot) || !text("voice", &out->voice)) {
    *error = "mod.json: \"name\", \"slot\" and \"voice\" are short texts."; return false;
  }
  if (out->name.size() > 96) out->name.resize(96);
  if (!out->slot.empty()) {
    // A costume by file code ("PlFxGr") or by fighter and color ("Fox Green"). The suffix keeps a
    // dot inside a name ("Dr. Mario Red") from being read as a file extension.
    std::string slot, kind;
    if (!portrait_slot_from_name_impl(out->slot + ".slot", &slot, &kind)) {
      *error = "mod.json: \"slot\" does not name one costume (write it like \"Fox Green\" or \"PlFxGr\")."; return false;
    }
    out->slot = slot;
  }
  return true;
}

// The manifest and the voice bank of a mod zip, when it has them. The bank is the one the manifest
// names, or the only .ssm in the zip.
bool read_mod_extras(const fs::path& path, const std::vector<ZipEntry>& entries, ModManifest* manifest,
                     std::vector<uint8_t>* voice, std::string* voice_member, std::string* error) {
  const auto base_name = [](const std::string& name) {
    const size_t slash = name.find_last_of('/');
    return lower(slash == std::string::npos ? name : name.substr(slash + 1));
  };
  std::vector<const ZipEntry*> banks;
  const ZipEntry* manifest_entry = nullptr;
  for (const auto& entry : entries) {
    if (entry.name.empty() || entry.name.back() == '/') continue;
    if (lower(fs::path(entry.name).extension().string()) == ".ssm") banks.push_back(&entry);
    else if (base_name(entry.name) == "mod.json" && !manifest_entry) manifest_entry = &entry;
  }
  if (manifest_entry) {
    std::vector<uint8_t> text;
    if (manifest_entry->uncompressed > 64 * 1024) { *error = "mod.json is larger than 64 KB."; return false; }
    if (!extract_zip_member(path, *manifest_entry, &text, error) || !parse_mod_manifest(text, manifest, error)) return false;
  }
  if (banks.empty()) {
    if (!manifest->voice.empty()) { *error = "mod.json names a voice bank the zip does not have: " + manifest->voice; return false; }
    return true;
  }
  const ZipEntry* chosen = nullptr;
  if (!manifest->voice.empty()) {
    for (const ZipEntry* bank : banks)
      if (lower(bank->name) == lower(manifest->voice) || base_name(bank->name) == base_name(manifest->voice)) { chosen = bank; break; }
    if (!chosen) { *error = "mod.json names a voice bank the zip does not have: " + manifest->voice; return false; }
  } else if (banks.size() == 1) chosen = banks[0];
  else { *error = "The zip has " + std::to_string(banks.size()) + " sound banks. A mod has one voice: keep one, or name it in mod.json (\"voice\")."; return false; }
  if (chosen->uncompressed > kMaxAssetBytes) { *error = "The sound bank in the zip exceeds the 64 MB safety limit."; return false; }
  if (!extract_zip_member(path, *chosen, voice, error)) return false;
  *voice_member = chosen->name;
  return true;
}

// After a mod zip's costume was installed: the manifest's name for a new entry, and the voice.
void finish_mod_locked(ImportResult* result, const ModManifest& manifest, const std::string& name_suffix,
                       const std::vector<uint8_t>& voice, const std::string& voice_member) {
  if (!result->ok) return;
  if (!manifest.name.empty() && !result->already_present) {
    const auto asset = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
      return item.info.id == result->asset_id;
    });
    std::string ignored;
    if (asset != g_assets.end()) { asset->info.name = manifest.name + name_suffix; save_catalog_locked(&ignored); }
  }
  if (voice.empty()) return;
  const std::string costume_message = result->message;
  const ImportResult attached = attach_voice_locked(result->asset_id, voice, voice_member);
  result->message = costume_message + " " + attached.message;
  g_message = result->message;
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
  if (asset.source_kind == kDiscSource) {
    // Read from the disc itself, and never trust it unseen: the hash is what the scan recorded.
    if (!read_range(fs::u8path(asset.disc_path), asset.disc_offset, asset.disc_length, &bytes) ||
        sha256(bytes) != asset.info.sha256) {
      *error = kDiscChanged; return {};
    }
  } else {
    if (!safe_relative_path(asset.stored_path) ||
        !read_bounded(g_root / fs::u8path(asset.stored_path), kMaxAssetBytes, &bytes, error)) return {};
    if (sha256(bytes) != asset.info.sha256) {
      *error = asset.info.name + ": stored DAT hash no longer matches the catalog."; return {};
    }
  }
  if (asset.info.kind == "character_costume") {
    auto dat = inspect_dat_impl(bytes);
    if (!dat.ok || dat.target_path != asset.info.target_path) {
      *error = asset.info.name + ": stored DAT identity no longer matches its catalog target."; return {};
    }
    // Every way a costume reaches the game passes here, so one that the game cannot draw is
    // refused offline too and the slot keeps the standard costume.
    std::string defect;
    if (!costume_draw_safe(bytes, &defect)) { *error = asset.info.name + ": " + defect; return {}; }
  } else if (asset.info.kind == "stage_visual" || asset.info.kind == "effect_visual") {
    VisualLayout layout;
    if (!parse_visual_layout(bytes, &layout, error)) {
      *error = asset.info.name + ": " + *error; return {};
    }
  } else if (asset.info.kind == kPortraitKind) {
    if (!png_companion(bytes, error)) { *error = asset.info.name + ": " + *error; return {}; }
  } else {
    *error = asset.info.name + ": unsupported catalog resource kind."; return {};
  }
  return bytes;
}

bool refresh_assets_locked(bool prune_invalid_selections, std::string* error) {
  std::vector<std::string> invalid_selected;
  for (auto& asset : g_assets) {
    std::string validation_error;
    if (asset.source_kind == kDiscSource) {
      // Startup only asks whether the disc is still the file that was scanned. Hashing every skin of
      // every disc here would read tens of megabytes on each launch; the hash is checked when a
      // skin is selected and when it is applied.
      uint64_t size = 0; int64_t mtime = 0;
      asset.info.available = file_stamp(fs::u8path(asset.disc_path), &size, &mtime) &&
                             size == asset.disc_size && mtime == asset.disc_mtime;
      validation_error = kDiscChanged;
    } else {
      asset.info.available = !load_runtime_asset_locked(asset, &validation_error).empty();
    }
    asset.info.availability_message = asset.info.available ?
        (asset.info.kind == "character_costume" || asset.info.kind == kPortraitKind ? std::string() :
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

// ---- skins inside a mod disc or a files pack ----
struct DiscCandidate { std::string member; fs::path file; uint64_t offset = 0, length = 0; };

// A file name that holds a costume of a slot of the game: "PlFxGr.dat", the English "PlCaRe.usd", or
// a pack's alternate costume for the slot under another extension ending in "at" ("PlFxGr.lat" and
// "PlFxGr.rat" are the 20XX style L and R alternates). *variant names the set: "" for the costume
// itself, "alt L", "alt R", or "alt <EXT>" for any other such extension. The name decides here; the
// content is checked by the caller (it must be a costume of that slot).
bool disc_skin_member(const std::string& raw, std::string* variant) {
  const std::string name = lower(raw);
  SlotName slot;
  if (name.size() != 10 || name[6] != '.' || !find_slot(name.substr(0, 6) + ".dat", &slot)) return false;
  const std::string extension = name.substr(7);
  if (extension == "dat" || extension == "usd") { variant->clear(); return true; }
  if (extension.size() != 3 || extension.compare(1, 2, "at") != 0) return false;
  if (extension == "lat") *variant = "alt L";
  else if (extension == "rat") *variant = "alt R";
  else *variant = std::string("alt ") + (char)std::toupper((unsigned char)extension[0]) + "AT";
  return true;
}

// A pack's character select file, where its portraits are: the English MnSlChr.usd when the pack
// has one (it is the one this game loads), otherwise MnSlChr.dat.
void note_select_screen(const std::string& name, const fs::path& file, uint64_t offset, uint64_t length,
                        DiscCandidate* screen) {
  const std::string wanted = lower(name);
  if (wanted == "mnslchr.usd" || (wanted == "mnslchr.dat" && screen->member.empty()))
    *screen = {name, file, offset, length};
}

// Every file in a disc image whose name is a costume of a slot of the game (disc_skin_member).
// *screen gets the disc's character select file when it has one.
bool disc_costume_files(const fs::path& iso, std::vector<DiscCandidate>* out, DiscCandidate* screen,
                        std::string* error) {
  std::vector<uint8_t> header;
  if (!read_range(iso, 0, 0x440, &header)) { *error = "The disc image could not be read."; return false; }
  const uint64_t fst_offset = be32(header.data() + 0x424), fst_size = be32(header.data() + 0x428);
  std::vector<uint8_t> fst;
  if (fst_size < 12 || fst_size > 2u * 1024u * 1024u || !read_range(iso, fst_offset, fst_size, &fst)) {
    *error = "The disc image has no readable file table."; return false;
  }
  std::vector<FstFile> files;
  if (!parse_fst(fst.data(), (uint32_t)fst.size(), &files, error)) return false;
  for (const auto& file : files) {
    const size_t slash = file.path.find_last_of('/');
    const std::string name = slash == std::string::npos ? file.path : file.path.substr(slash + 1);
    std::string variant;
    note_select_screen(name, iso, file.start, file.size, screen);
    if (!disc_skin_member(name, &variant)) continue;
    out->push_back({name, iso, file.start, file.size});
  }
  return true;
}

void folder_costume_files(const fs::path& root, std::vector<DiscCandidate>* out, DiscCandidate* screen) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const std::string name = path_filename_utf8(it->path());
    std::string variant;
    const uint64_t length = it->file_size(ec);
    if (ec) continue;
    note_select_screen(name, it->path(), 0, length, screen);
    if (!disc_skin_member(name, &variant)) continue;
    out->push_back({name, it->path(), 0, length});
  }
}

// ---- a pack's own portraits ----
// The character select file keeps every portrait in one texture animation: an image table, a
// palette table, and a track of keys that says which table entry each frame shows. The game shows
// a costume's portrait by setting that animation to frame "fighter column + 30 * costume"
// (mn/mncharsel.c), so the keys, not the order of the table, name the picture of a slot. This is
// the reading tools/companion_texture_diagnostic.py does to make companion_texture_map.h.
constexpr uint32_t kPortraitWidth = 136, kPortraitHeight = 188, kPortraitFormat = 9;   // 8 bit palette indices
constexpr uint32_t kPortraitBytes = (kPortraitWidth / 8) * (kPortraitHeight / 4) * 32;
struct SelectPortrait { uint32_t pixels = 0, colors = 0, palette_format = 0, palette_entries = 0; };

// Frame -> value of one animation track that holds whole-number keys. `block` is the archive's data
// block; every read stays inside it.
bool texture_track_keys(const uint8_t* block, size_t size, uint32_t track, std::map<int, int>* keys) {
  keys->clear();
  if ((uint64_t)track + 0x14 > size) return false;
  const uint32_t length = be32(block + track + 4);
  const uint8_t value_format = block[track + 0xD], slope_format = block[track + 0xE];
  size_t at = be32(block + track + 0x10);
  if (at > size || length > size - at) return false;
  const size_t end = at + length;
  static constexpr uint32_t sizes[] = {4, 2, 2, 1, 1};   // float, s16, u16, s8, u8
  const uint32_t kind = value_format >> 5, slope_kind = slope_format >> 5;
  if (kind > 4) return false;
  const double scale = (double)(1u << (value_format & 31));
  uint64_t frame = 0;
  while (at < end) {
    uint8_t byte = block[at++];
    const uint32_t opcode = byte & 0xF;
    uint32_t count = (byte >> 4) & 7, shift = 3;
    while (byte & 0x80) {
      if (at >= size || shift > 24) return false;
      byte = block[at++];
      count |= (uint32_t)(byte & 0x7F) << shift; shift += 7;
    }
    for (uint32_t i = 0; i <= count; ++i) {
      bool has_value = false;
      double value = 0;
      if (opcode == 1 || opcode == 2 || opcode == 3 || opcode == 4 || opcode == 6) {
        if (size - at < sizes[kind]) return false;
        const uint8_t* raw = block + at;
        at += sizes[kind];
        switch (kind) {
          case 0: { const uint32_t bits = le32(raw); float real = 0; std::memcpy(&real, &bits, 4); value = real; break; }
          case 1: value = (int16_t)le16(raw) / scale; break;
          case 2: value = le16(raw) / scale; break;
          case 3: value = (int8_t)raw[0] / scale; break;
          default: value = raw[0] / scale; break;
        }
        has_value = true;
      }
      if (opcode == 4 || opcode == 5) {
        if (slope_kind > 4 || size - at < sizes[slope_kind]) return false;
        at += sizes[slope_kind];
      }
      uint32_t wait = 0, wait_shift = 0;
      for (;;) {
        if (at >= size || wait_shift > 28) return false;
        byte = block[at++];
        wait |= (uint32_t)(byte & 0x7F) << wait_shift; wait_shift += 7;
        if (!(byte & 0x80)) break;
      }
      if (has_value) {
        if (!std::isfinite(value) || value < 0 || value > 65535 || frame > 65535 || keys->size() >= 4096) return false;
        (*keys)[(int)frame] = (int)value;
      }
      frame += wait;
    }
  }
  return !keys->empty();
}

// Frame -> portrait of a character select file, from the first texture animation whose tables hold
// portraits. False when the file has no such animation (not a character select file, or one built
// another way): then no portrait is taken from it.
bool select_portraits(const std::vector<uint8_t>& dat, std::map<int, SelectPortrait>* frames) {
  frames->clear();
  if (dat.size() < 0x20) return false;
  const uint64_t data_size = be32(dat.data() + 4), relocations = be32(dat.data() + 8);
  if (0x20ull + data_size + relocations * 4ull > dat.size()) return false;
  const uint8_t* block = dat.data() + 0x20;
  std::vector<uint32_t> offsets((size_t)relocations);
  for (size_t i = 0; i < offsets.size(); ++i) offsets[i] = be32(block + data_size + i * 4);
  std::vector<uint32_t> sorted = offsets;
  std::sort(sorted.begin(), sorted.end());
  auto relocated = [&](uint32_t at) { return std::binary_search(sorted.begin(), sorted.end(), at); };
  auto word = [&](uint64_t at) { return be32(block + at); };
  for (const uint32_t pointer : offsets) {
    // pointer is the image table field of a texture animation:
    // {next, id, animation, image table, palette table, images (16 bit), palettes (16 bit)}
    if (pointer < 0xC || (uint64_t)pointer + 12 > data_size) continue;
    const uint32_t anim = pointer - 0xC;
    if (!relocated(pointer + 4) || !relocated(anim + 8)) continue;
    const uint32_t images = word(pointer), palettes = word(pointer + 4);
    const uint32_t image_count = be16(block + pointer + 8), palette_count = be16(block + pointer + 10);
    if (image_count < 20 || image_count > 400 || image_count != palette_count) continue;
    if ((uint64_t)images + 4ull * image_count > data_size || (uint64_t)palettes + 4ull * image_count > data_size) continue;
    std::vector<SelectPortrait> entries(image_count);
    std::vector<bool> valid(image_count, false);
    size_t good = 0;
    bool broken = false;
    for (uint32_t i = 0; i < image_count; ++i) {
      const uint32_t image = word(images + 4ull * i), palette = word(palettes + 4ull * i);
      if ((uint64_t)image + 24 > data_size || (uint64_t)palette + 14 > data_size) { broken = true; break; }
      SelectPortrait entry;
      entry.pixels = word(image);
      if (be16(block + image + 4) != kPortraitWidth || be16(block + image + 6) != kPortraitHeight ||
          word(image + 8) != kPortraitFormat || (uint64_t)entry.pixels + kPortraitBytes > data_size) continue;
      entry.colors = word(palette);
      entry.palette_format = word(palette + 4);
      entry.palette_entries = be16(block + palette + 12);
      if (entry.palette_format > 2 || !entry.palette_entries || entry.palette_entries > 256 ||
          (uint64_t)entry.colors + 2ull * entry.palette_entries > data_size) continue;
      entries[i] = entry; valid[i] = true; ++good;
    }
    if (broken || good * 5 < (size_t)image_count * 4) continue;
    // animation -> its first track; the image index track is type 1
    const uint32_t animation = word(anim + 8);
    if ((uint64_t)animation + 12 > data_size) continue;
    std::map<int, int> keys;
    bool have_keys = false;
    uint32_t track = word(animation + 8);
    for (int guard = 0; track && guard < 64; ++guard) {
      if ((uint64_t)track + 0x14 > data_size) { have_keys = false; break; }
      if (block[track + 0xC] == 1) have_keys = texture_track_keys(block, (size_t)data_size, track, &keys);
      track = word(track);
    }
    if (!have_keys) continue;
    bool in_table = true;
    for (const auto& key : keys) in_table &= key.second >= 0 && (uint32_t)key.second < image_count;
    if (!in_table) continue;
    for (const auto& key : keys)
      if (valid[(size_t)key.second]) (*frames)[key.first] = entries[(size_t)key.second];
    return !frames->empty();
  }
  return false;
}

// The frame that shows a costume slot's portrait, or -1 for a slot with no cell of its own: Nana
// (the Ice Climbers' cell is Popo's), Sheik (Zelda's cell), and Mr. Game & Watch (four cells, one
// costume file).
int portrait_frame(const SlotName& slot) {
  static constexpr const char* columns[] = {"Ca", "Dk", "Fx", "Gw", "Kb", "Kp", "Lk", "Lg", "Mr", "Ms", "Mt", "Ns", "Pe",
                                            "Pk", "Pp", "Pr", "Ss", "Ys", "Zd", "Fc", "Cl", "Dr", "Fe", "Pc", "Gn"};
  const std::string code = slot.family->file_code;
  if (code == "Gw") return -1;
  const std::string colors = slot.family->colors;   // the game's costume order
  const size_t at = (std::string(" ") + colors).find(std::string(" ") + slot.color->code);
  if (at == std::string::npos) return -1;
  for (size_t column = 0; column < std::size(columns); ++column)
    if (code == columns[column]) return (int)column + (int)(at / 3) * 30;
  return -1;
}

// One portrait as 8 bit RGBA, top row first. The image is rows of 8x4 texel blocks of palette
// indices; the palette is 16 bit colors in one of the console's three palette formats.
std::vector<uint8_t> decode_portrait(const uint8_t* block, const SelectPortrait& portrait) {
  std::vector<uint8_t> rgba((size_t)kPortraitWidth * kPortraitHeight * 4, 0);
  const uint8_t* source = block + portrait.pixels;
  for (uint32_t block_y = 0; block_y < kPortraitHeight; block_y += 4)
    for (uint32_t block_x = 0; block_x < kPortraitWidth; block_x += 8)
      for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 0; x < 8; ++x) {
          const uint32_t index = *source++;
          if (index >= portrait.palette_entries) continue;   // no such color: left transparent
          const uint32_t color = be16(block + portrait.colors + 2 * index);
          uint32_t r, g, b, a = 255;
          if (portrait.palette_format == 0) {          // intensity and alpha
            r = g = b = color & 0xFF; a = color >> 8;
          } else if (portrait.palette_format == 1) {   // 5:6:5
            r = (color >> 11) & 31; g = (color >> 5) & 63; b = color & 31;
            r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
          } else if (color & 0x8000) {                 // 5:5:5, opaque
            r = (color >> 10) & 31; g = (color >> 5) & 31; b = color & 31;
            r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
          } else {                                     // 4:4:4 with 3 bits of alpha
            a = (color >> 12) & 7; r = (color >> 8) & 15; g = (color >> 4) & 15; b = color & 15;
            a = (a << 5) | (a << 2) | (a >> 1); r *= 17; g *= 17; b *= 17;
          }
          uint8_t* out = rgba.data() + ((size_t)(block_y + y) * kPortraitWidth + block_x + x) * 4;
          out[0] = (uint8_t)r; out[1] = (uint8_t)g; out[2] = (uint8_t)b; out[3] = (uint8_t)a;
        }
  return rgba;
}

// A PNG of 8 bit RGBA pixels. The picture is small and written once per scan, so the data is
// stored without compression: no encoder to depend on, and any PNG reader accepts it.
std::vector<uint8_t> rgba_png(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height) {
  std::vector<uint8_t> raw;
  raw.reserve(((size_t)width * 4 + 1) * height);
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);   // filter: none
    raw.insert(raw.end(), rgba.begin() + (size_t)y * width * 4, rgba.begin() + (size_t)(y + 1) * width * 4);
  }
  std::vector<uint8_t> packed{0x78, 0x01};
  uint32_t sum_a = 1, sum_b = 0;
  for (const uint8_t byte : raw) { sum_a = (sum_a + byte) % 65521u; sum_b = (sum_b + sum_a) % 65521u; }
  for (size_t at = 0; at < raw.size();) {
    const size_t chunk = std::min<size_t>(raw.size() - at, 65535);
    packed.push_back(at + chunk == raw.size() ? 1 : 0);
    packed.push_back((uint8_t)chunk); packed.push_back((uint8_t)(chunk >> 8));
    packed.push_back((uint8_t)~chunk); packed.push_back((uint8_t)(~chunk >> 8));
    packed.insert(packed.end(), raw.begin() + at, raw.begin() + at + chunk);
    at += chunk;
  }
  const uint32_t checksum = (sum_b << 16) | sum_a;
  for (int shift = 24; shift >= 0; shift -= 8) packed.push_back((uint8_t)(checksum >> shift));
  std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    uint8_t word[4];
    put_be32(word, (uint32_t)data.size()); png.insert(png.end(), word, word + 4);
    png.insert(png.end(), body.begin(), body.end());
    put_be32(word, crc32(body.data(), body.size())); png.insert(png.end(), word, word + 4);
  };
  std::vector<uint8_t> header(13, 0);
  put_be32(header.data(), width); put_be32(header.data() + 4, height);
  header[8] = 8; header[9] = 6;   // 8 bits a channel, RGBA
  chunk("IHDR", header);
  chunk("IDAT", packed);
  chunk("IEND", {});
  return png;
}

// Gives each plain costume of a scan the portrait the pack's character select file shows for its
// slot, when that picture differs from the game's own. The picture is written beside the catalog as
// the skin's "csp" companion. Alternate sets are skipped: the file has one cell per slot, and it is
// the plain costume's. Returns how many portraits were stored. The game disc's English file is the
// one compared against, since its portraits are the ones the renderer replaces.
size_t pack_portraits_locked(const DiscCandidate& screen, std::map<std::string, AssetRecord>* fresh) {
  if (screen.member.empty()) return 0;
  bool wanted = false;
  for (const auto& item : *fresh) wanted |= item.second.info.variant.empty();
  if (!wanted) return 0;
  uint32_t retail_offset = 0, retail_size = 0;
  std::vector<uint8_t> pack, retail;
  if (!host::disc_find_file("MnSlChr.usd", &retail_offset, &retail_size) || !retail_size || retail_size > kMaxAssetBytes ||
      !read_range(screen.file, screen.offset, screen.length, &pack)) return 0;
  retail.resize(retail_size);
  if (!host::disc_read(retail_offset, retail.data(), retail_size) || pack == retail) return 0;
  std::map<int, SelectPortrait> ours, theirs;
  if (!select_portraits(retail, &ours) || !select_portraits(pack, &theirs)) return 0;
  size_t stored = 0;
  for (auto& item : *fresh) {
    AssetRecord& asset = item.second;
    SlotName slot;
    if (!asset.info.variant.empty() || !find_slot(asset.info.target_path, &slot)) continue;
    const int frame = portrait_frame(slot);
    const auto standard = ours.find(frame), picture = theirs.find(frame);
    if (frame < 0 || standard == ours.end() || picture == theirs.end()) continue;
    const std::vector<uint8_t> rgba = decode_portrait(pack.data() + 0x20, picture->second);
    if (rgba == decode_portrait(retail.data() + 0x20, standard->second)) continue;   // the game's own picture
    const std::vector<uint8_t> png = rgba_png(rgba, kPortraitWidth, kPortraitHeight);
    AssetRecord::Companion companion;
    companion.kind = "csp";
    companion.sha256 = sha256(png);
    companion.source_member = screen.member;
    companion.stored_path = (fs::path(L"assets") / fs::u8path(asset.info.id) / L"companions" / L"csp.png").generic_u8string();
    std::string write_error;
    if (companion.sha256.empty() ||
        !write_atomic(g_root / fs::u8path(companion.stored_path), png.data(), png.size(), &write_error)) continue;
    asset.companions.push_back(std::move(companion));
    ++stored;
  }
  return stored;
}

}  // namespace

ImportResult scan_disc_skins(const std::string& iso_path, const std::string& pack_name, std::string* error) {
  std::string local; if (!error) error = &local;
  ImportResult result;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!mutable_profile_locked(error)) { result.message = *error; return result; }
  std::error_code ec;
  const fs::path root = fs::absolute(fs::u8path(iso_path), ec);
  const std::string key = root.u8string();
  const bool folder = fs::is_directory(root, ec);
  uint64_t root_size = 0; int64_t root_mtime = 0;
  if (!folder && !file_stamp(root, &root_size, &root_mtime)) {
    *error = result.message = "The disc image could not be opened."; return result;
  }
  // A disc scanned before, unchanged since: nothing to read. A folder is small and has no single
  // time to compare, so it is read every time.
  if (!folder)
    for (const auto& scan : g_disc_scans)
      if (scan.path == key && scan.size == root_size && scan.mtime == root_mtime && scan.rules >= kScanRules) {
        result.ok = result.already_present = true;
        result.message = "Skins of " + pack_name + " are already in the skin list.";
        return result;
      }
  std::vector<DiscCandidate> candidates;
  DiscCandidate select_screen;   // the pack's character select file, when it has one
  if (folder) folder_costume_files(root, &candidates, &select_screen);
  else if (!disc_costume_files(root, &candidates, &select_screen, error)) { result.message = *error; return result; }
  // The plain .dat of a slot first, then its English .usd: when both differ from the game's and from
  // each other, the English one is the one this game loads, so it is the one listed.
  std::stable_sort(candidates.begin(), candidates.end(), [](const DiscCandidate& a, const DiscCandidate& b) {
    return lower(a.member) < lower(b.member);
  });
  std::map<std::string, AssetRecord> fresh;   // by target slot and variant
  size_t same = 0, skipped = 0;
  for (const auto& candidate : candidates) {
    uint32_t retail_offset = 0, retail_size = 0;
    std::vector<uint8_t> bytes, retail;
    std::string variant;
    if (!disc_skin_member(candidate.member, &variant)) { ++skipped; continue; }
    // An alternate costume is compared with the slot's own costume on the game disc: that is the
    // file it replaces when chosen, and the skeleton it must keep.
    const std::string retail_name = variant.empty() ? candidate.member : candidate.member.substr(0, 6) + ".dat";
    if (!host::disc_find_file(retail_name, &retail_offset, &retail_size) || !retail_size ||
        retail_size > kMaxAssetBytes || !read_range(candidate.file, candidate.offset, candidate.length, &bytes)) {
      ++skipped; continue;
    }
    retail.resize(retail_size);
    if (!host::disc_read(retail_offset, retail.data(), retail_size)) { ++skipped; continue; }
    if (bytes == retail) { ++same; continue; }
    const auto dat = inspect_dat_impl(bytes);
    const std::string slot_name = candidate.member.substr(0, 6) + ".dat";
    if (!dat.ok || lower(dat.target_path) != lower(slot_name)) { ++skipped; continue; }
    const std::string digest = sha256(bytes);
    if (digest.empty()) { ++skipped; continue; }
    const std::string fresh_key = dat.target_path + "\n" + variant;
    const auto have = fresh.find(fresh_key);
    if (have != fresh.end() && have->second.info.sha256 == digest) continue;   // .dat and .usd carry the same skin
    AssetRecord asset;
    // The identity is where the skin is, not what it is: a newer build of the same disc keeps the
    // player's selection.
    asset.info.id = "disc-" + sha256_text(key + "\n" + lower(candidate.member));
    asset.info.name = dat.character + " " + dat.costume + ": from " + pack_name + (variant.empty() ? "" : " (" + variant + ")");
    asset.info.variant = variant;
    {
      std::string detail;
      asset.online_known = true;
      asset.online_ok = costume_skeleton_matches(retail, bytes, &detail);
      asset.online_note = online_reason_short(detail);
      // A costume the game cannot draw stays listed with its reason, and the load refuses it
      // everywhere (load_runtime_asset_locked); the online verdict carries the reason to the list.
      if (!costume_draw_safe(bytes, &detail)) { asset.online_ok = false; asset.online_note = online_reason_short(detail); }
    }
    asset.info.kind = "character_costume";
    asset.info.target_path = dat.target_path;
    asset.info.character = dat.character;
    asset.info.costume = dat.costume;
    asset.info.sha256 = digest;
    asset.info.roots = dat.roots;
    asset.info.available = true;
    asset.info.source = "disc";
    asset.info.source_name = pack_name;
    asset.info.disc_path = candidate.file.u8string();
    // Never written: a build that does not know disc-backed skins finds no file here and lists the
    // entry as unavailable, instead of refusing the whole catalog.
    asset.stored_path = "disc/" + asset.info.id + "/" + dat.target_path;
    asset.source_kind = kDiscSource;
    asset.source_name = pack_name;
    asset.source_member = candidate.member;
    asset.source_id = key;
    asset.disc_path = asset.info.disc_path;
    asset.disc_offset = candidate.offset;
    asset.disc_length = candidate.length;
    if (!file_stamp(candidate.file, &asset.disc_size, &asset.disc_mtime)) { ++skipped; continue; }
    fresh[fresh_key] = std::move(asset);
  }
  const size_t portraits = pack_portraits_locked(select_screen, &fresh);
  // Replace this disc's earlier records: files that no longer differ go away, changed ones are
  // updated, and a name the player gave an entry is kept. Records of a disc that no longer exists
  // go too; they hold no data and a new scan brings them back.
  auto previous_assets = g_assets;
  auto previous_scans = g_disc_scans;
  std::map<std::string, std::string> names;
  std::map<std::string, AssetRecord::Voice> voices;   // a voice bank the player gave a pack's skin stays with it
  std::map<std::string, std::vector<AssetRecord::Companion>> pictures;   // and so does a picture (set_skin_portrait)
  g_assets.erase(std::remove_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
    if (item.source_kind != kDiscSource) return false;
    if (item.source_id == key) {
      if (item.info.name.find(": from ") == std::string::npos) names[item.info.id] = item.info.name;
      if (!item.voice.stored_path.empty()) voices[item.info.id] = item.voice;
      for (const auto& companion : item.companions)
        if (player_picture(companion)) pictures[item.info.id].push_back(companion);
      return true;
    }
    std::error_code gone;
    return !fs::exists(fs::u8path(item.disc_path), gone);
  }), g_assets.end());
  for (auto& item : fresh) {
    const auto renamed = names.find(item.second.info.id);
    if (renamed != names.end()) item.second.info.name = renamed->second;
    const auto voiced = voices.find(item.second.info.id);
    if (voiced != voices.end()) {
      item.second.voice = voiced->second;
      item.second.info.voice = voiced->second.bank;
      item.second.info.voice_source = voiced->second.source_member;
    }
    const auto pictured = pictures.find(item.second.info.id);
    if (pictured != pictures.end())
      for (const auto& kept : pictured->second) {
        auto& have = item.second.companions;
        have.erase(std::remove_if(have.begin(), have.end(),
            [&](const AssetRecord::Companion& c) { return c.kind == kept.kind; }), have.end());
        have.push_back(kept);
      }
    g_assets.push_back(std::move(item.second));
  }
  g_disc_scans.erase(std::remove_if(g_disc_scans.begin(), g_disc_scans.end(),
      [&](const DiscScan& scan) { return scan.path == key; }), g_disc_scans.end());
  if (!folder) g_disc_scans.push_back({key, root_size, root_mtime, kScanRules});
  if (!save_state_locked(error)) {
    g_assets = std::move(previous_assets); g_disc_scans = std::move(previous_scans);
    result.message = *error; return result;
  }
  result.ok = true;
  result.message = std::to_string(fresh.size()) + " skins from " + pack_name + " are in the skin list (" +
                   std::to_string(same) + " costume files are the standard ones, " + std::to_string(skipped) +
                   " not usable).";
  host::log("cosmetics: scanned %s: %zu skins listed, %zu standard, %zu not usable", key.c_str(), fresh.size(), same, skipped);
  if (!select_screen.member.empty())
    host::log("cosmetics: %zu portraits taken from the %s of %s", portraits, select_screen.member.c_str(), key.c_str());
  return result;
}

uint32_t disc_skin_count(const std::string& iso_path) {
  std::error_code ec;
  const std::string key = fs::absolute(fs::u8path(iso_path), ec).u8string();
  std::lock_guard<std::mutex> lock(g_mutex);
  return (uint32_t)std::count_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
    return item.source_kind == kDiscSource && item.source_id == key;
  });
}

std::vector<PackInfo> packs() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<PackInfo> out;
  std::map<std::string, size_t> index;
  std::map<std::string, std::map<std::string, bool>> slots;   // pack key -> covered slots
  for (const auto& asset : g_assets) {
    if (asset.info.kind != "character_costume") continue;
    const std::string key = pack_key(asset);
    auto at = index.find(key);
    if (at == index.end()) {
      PackInfo pack;
      pack.key = key;
      pack.disc = asset.source_kind == kDiscSource;
      pack.name = pack.disc ? asset.source_name : "Imported skins";
      out.push_back(std::move(pack));
      at = index.emplace(key, out.size() - 1).first;
    }
    PackInfo& pack = out[at->second];
    ++pack.skins;
    slots[key][asset.info.target_path] = true;
    if (std::find(pack.variants.begin(), pack.variants.end(), asset.info.variant) == pack.variants.end())
      pack.variants.push_back(asset.info.variant);
    if (asset.online_known) pack.online_on += asset.online_ok ? 1 : 0;
    else ++pack.online_unchecked;
    const auto picked = g_profile.selections.find(asset.info.target_path);
    if (picked != g_profile.selections.end() && picked->second == asset.info.id) ++pack.selected;
  }
  for (auto& pack : out) {
    pack.slots = (uint32_t)slots[pack.key].size();
    std::sort(pack.variants.begin(), pack.variants.end());   // "" first, then "alt L", "alt R"
  }
  // Imports first, then the packs by name.
  std::stable_sort(out.begin(), out.end(), [](const PackInfo& a, const PackInfo& b) {
    if (a.disc != b.disc) return !a.disc;
    return lower(a.name) < lower(b.name);
  });
  return out;
}

PackSetResult apply_pack_set(const std::string& pack, const std::string& variant, bool preview) {
  PackSetResult result;
  std::lock_guard<std::mutex> lock(g_mutex);
  std::string error;
  if (!preview && !mutable_profile_locked(&error)) { result.message = error; return result; }
  const bool none = variant == "none";
  // The pack's choice for each slot: the record of that variant, an available one when there is more
  // than one. For "none", every record of the pack counts, since any of them may be selected now.
  std::map<std::string, const AssetRecord*> choice;
  std::map<std::string, const AssetRecord*> by_id;
  for (const auto& asset : g_assets) {
    by_id[asset.info.id] = &asset;
    if (asset.info.kind != "character_costume" || pack_key(asset) != pack) continue;
    if (!none && asset.info.variant != variant) continue;
    const AssetRecord*& have = choice[asset.info.target_path];
    if (!have || (!have->info.available && asset.info.available)) have = &asset;
  }
  if (choice.empty()) { result.message = "That pack has no skins of that set."; return result; }
  Profile next = g_profile;
  std::map<std::string, uint32_t> replaced_from;
  for (const auto& slot : choice) {
    const auto current = next.selections.find(slot.first);
    const std::string current_id = current == next.selections.end() ? std::string() : current->second;
    const auto current_record = by_id.find(current_id);
    const bool current_is_skin = current_record != by_id.end() && current_id != kVanillaSelection;
    if (none) {
      // Only this pack's own picks go back to the standard costume; another pack's choice is kept.
      if (current_is_skin && pack_key(*current_record->second) == pack) {
        next.selections[slot.first] = kVanillaSelection; ++result.cleared;
      }
      continue;
    }
    if (!slot.second->info.available) continue;
    if (current_id == slot.second->info.id) continue;
    if (current_is_skin && pack_key(*current_record->second) != pack) {
      ++result.replaced;
      ++replaced_from[current_record->second->source_kind == kDiscSource ? current_record->second->source_name
                                                                          : std::string("Imported skins")];
    }
    next.selections[slot.first] = slot.second->info.id; ++result.set;
  }
  uint32_t most = 0;
  for (const auto& from : replaced_from) if (from.second > most) { most = from.second; result.replaced_from = from.first; }
  if (replaced_from.size() > 1) result.replaced_from = "other packs";
  result.ok = true;
  if (preview) return result;
  if (result.set || result.cleared) {
    Profile previous = g_profile;
    g_profile = std::move(next);
    if (!none) g_profile.enabled = true;
    ++g_profile.generation;
    if (!save_profile_locked(&error)) { g_profile = std::move(previous); result.ok = false; result.message = error; return result; }
  }
  result.message = none ? std::to_string(result.cleared) + " costumes back to standard; restart to apply."
                        : std::to_string(result.set) + " costumes set" +
                          (result.replaced ? ", " + std::to_string(result.replaced) + " replaced from " + result.replaced_from : "") +
                          "; restart to apply.";
  g_message = result.message;
  return result;
}

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
  // A mod zip's own extras (docs/voice-mods.md): its manifest and the fighter's sound bank.
  ModManifest manifest;
  std::vector<uint8_t> voice;
  std::string voice_member;
  if (extension == ".ssm") {
    // A sound bank on its own: it becomes the voice of an installed skin of its fighter.
    if (!read_bounded(path, kMaxAssetBytes, &bytes, &error)) return {false, false, {}, error};
    std::lock_guard<std::mutex> lock(g_mutex);
    ImportResult result = attach_loose_voice_locked(bytes, path_filename_utf8(path), {}, {path_filename_utf8(path)});
    if (result.ok && std::atomic_load(&g_runtime)->initialized) result.message += " Restart to apply it.";
    g_message = result.message;
    return result;
  }
  if (extension == ".png") {
    // A portrait or stock icon on its own: the file name says which costume it is for.
    std::string slot, kind;
    if (!portrait_slot_from_name_impl(path_filename_utf8(path), &slot, &kind))
      return {false, false, {}, "This picture's name does not say which costume it is for. Name it like "
                                "\"Fox Green.png\" or \"PlFxGr stock.png\", or use Add portrait and pick the costume."};
    if (!read_bounded(path, 16ull * 1024 * 1024, &bytes, &error)) return {false, false, {}, error};
    std::lock_guard<std::mutex> lock(g_mutex);
    ImportResult result = install_portrait_locked(slot, kind, bytes, path_filename_utf8(path));
    if (result.ok && std::atomic_load(&g_runtime)->initialized) result.message += " Restart to apply it.";
    g_message = result.message;
    return result;
  }
  if (extension == ".dat" || extension == ".usd") {
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
    if (!read_mod_extras(path, entries, &manifest, &voice, &voice_member, &error)) return {false, false, {}, error};
    std::vector<std::pair<ZipEntry, testing::DatInspection>> recognized;
    std::vector<std::vector<uint8_t>> recognized_bytes;
    std::vector<VaultResourcePlan> stage_resources;
    uint32_t candidates = 0;
    for (const auto& entry : entries) {
      if (!dat_extension(entry.name)) continue;
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
    if (recognized.empty() && candidates == 0 && !voice.empty()) {
      // A voice with no costume file: for the costume the manifest, the bank's name or the zip's
      // name says, else for the one skin of that fighter it can only be meant for.
      std::lock_guard<std::mutex> lock(g_mutex);
      ImportResult result = attach_loose_voice_locked(voice, voice_member, manifest.slot,
                                                      {voice_member, path_filename_utf8(path)});
      if (result.ok && std::atomic_load(&g_runtime)->initialized) result.message += " Restart to apply it.";
      g_message = result.message;
      return result;
    }
    if (recognized.empty() && candidates == 0) {
      // A pack of pictures with no costume file: each PNG whose name identifies a costume.
      size_t installed = 0, pictures = 0;
      std::string skipped, last_id, failure;
      std::lock_guard<std::mutex> lock(g_mutex);
      for (const auto& entry : entries) {
        if (lower(fs::path(entry.name).extension().string()) != ".png") continue;
        ++pictures;
        std::string slot, kind;
        std::vector<uint8_t> picture;
        if (!portrait_slot_from_name_impl(entry.name, &slot, &kind) ||
            !extract_zip_member(path, entry, &picture, &error)) {
          if (skipped.size() < 160) skipped += (skipped.empty() ? "" : ", ") + path_filename_utf8(fs::u8path(entry.name));
          continue;
        }
        ImportResult one = install_portrait_locked(slot, kind, picture, path_filename_utf8(fs::u8path(entry.name)));
        if (one.ok) { ++installed; last_id = one.asset_id; }
        else failure = one.message;
      }
      if (pictures) {
        ImportResult result;
        result.ok = installed > 0;
        result.asset_id = last_id;
        result.message = installed ? std::to_string(installed) + " of " + std::to_string(pictures) + " pictures set." :
                                     failure.empty() ? "No picture in the ZIP names a costume." : failure;
        if (!skipped.empty()) result.message += " Names that identify no costume: " + skipped + ".";
        if (result.ok && std::atomic_load(&g_runtime)->initialized) result.message += " Restart to apply them.";
        g_message = result.message;
        return result;
      }
    }
    if (recognized.empty())
      return {false, false, {}, "ZIP contains no valid DAT for a supported costume or stage resource."};
    // The same costume twice (a .dat and its .usd with the same content) is one costume.
    for (size_t i = 0; i < recognized.size(); ++i)
      for (size_t k = recognized.size(); k-- > i + 1;)
        if (lower(recognized[k].second.target_path) == lower(recognized[i].second.target_path) &&
            recognized_bytes[k] == recognized_bytes[i]) {
          recognized.erase(recognized.begin() + (std::ptrdiff_t)k);
          recognized_bytes.erase(recognized_bytes.begin() + (std::ptrdiff_t)k);
        }
    // The bank is checked before anything is written: a mod goes in whole or not at all.
    if (!voice.empty())
      for (const auto& costume : recognized) {
        VoiceCheck check;
        if (!check_voice(voice, voice_member, costume.second.target_path, &check, &error))
          return {false, false, {}, "Sound bank refused: " + error + " Nothing from this zip was imported."};
      }
    if (recognized.size() != 1) {
      // Several costumes in one mod: one entry per costume slot, each with the pictures whose
      // names say that costume, and all of them with the mod's voice.
      std::map<std::string, size_t> slots;
      for (size_t i = 0; i < recognized.size(); ++i)
        if (!slots.emplace(lower(recognized[i].second.target_path), i).second)
          return {false, false, {}, "ZIP contains two different costume files for " + recognized[i].second.target_path +
                                    "; a mod has one file per costume."};
      std::vector<std::vector<VaultCompanionPlan>> pictures(recognized.size());
      for (const auto& entry : entries) {
        if (lower(fs::path(entry.name).extension().string()) != ".png") continue;
        std::string slot, kind;
        if (!portrait_slot_from_name_impl(entry.name, &slot, &kind)) continue;
        const auto owner = slots.find(lower(slot));
        if (owner == slots.end()) continue;
        auto& mine = pictures[owner->second];
        if (std::any_of(mine.begin(), mine.end(), [&](const VaultCompanionPlan& item) { return item.kind == kind; })) continue;
        VaultCompanionPlan picture;
        picture.kind = kind;
        picture.source_member = entry.name;
        if (!extract_zip_member(path, entry, &picture.bytes, &error) || !png_companion(picture.bytes, &error))
          return {false, false, {}, error};
        mine.push_back(std::move(picture));
      }
      std::lock_guard<std::mutex> lock(g_mutex);
      ImportResult result;
      size_t installed = 0;
      for (size_t i = 0; i < recognized.size(); ++i) {
        ImportResult one = install_asset_locked(path, "zip", recognized[i].first.name, std::move(recognized_bytes[i]),
                                                recognized[i].second, std::move(pictures[i]));
        finish_mod_locked(&one, manifest, ", " + recognized[i].second.costume, voice, voice_member);
        if (!one.ok) { result.message = one.message; break; }
        ++installed;
        result.asset_id = one.asset_id;
        result.already_present = installed == 1 ? one.already_present : result.already_present && one.already_present;
      }
      result.ok = installed == recognized.size();
      if (result.ok) {
        result.message = std::to_string(installed) + " costumes imported from " + path_filename_utf8(path) +
                         (voice.empty() ? std::string(".") : ", each with the voice bank " +
                              path_filename_utf8(fs::u8path(voice_member)) + ".");
        if (std::atomic_load(&g_runtime)->initialized) result.message += " Restart to apply the staged profile safely.";
      } else if (installed) {
        result.message = std::to_string(installed) + " of " + std::to_string(recognized.size()) +
                         " costumes imported, then: " + result.message;
      }
      g_message = result.message;
      return result;
    }
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
    return {false, false, {}, "Choose a costume or stage .dat, .usd or .zip, a portrait .png, a voice bank .ssm, or a Nucleus vault .zip."};
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  ImportResult result = install_asset_locked(path, extension == ".zip" ? "zip" : "dat", member,
                                             std::move(bytes), dat, std::move(companions));
  finish_mod_locked(&result, manifest, {}, voice, voice_member);
  if (result.ok && std::atomic_load(&g_runtime)->initialized)
    result.message += " Restart to apply the staged profile safely.";
  g_message = result.message;
  return result;
}

ImportResult import_portrait(const std::string& png_path, const std::string& slot, const std::string& kind) {
  const fs::path path = fs::u8path(png_path);
  std::vector<uint8_t> bytes;
  std::string error;
  if (!read_bounded(path, 16ull * 1024 * 1024, &bytes, &error)) return {false, false, {}, error};
  std::lock_guard<std::mutex> lock(g_mutex);
  ImportResult result = install_portrait_locked(slot, kind, bytes, path_filename_utf8(path));
  if (result.ok && std::atomic_load(&g_runtime)->initialized) result.message += " Restart to apply it.";
  g_message = result.message;
  return result;
}

ImportResult import_voice(const std::string& ssm_path, const std::string& asset_id) {
  const fs::path path = fs::u8path(ssm_path);
  std::vector<uint8_t> bytes;
  std::string error;
  if (!read_bounded(path, kMaxAssetBytes, &bytes, &error)) return {false, false, {}, error};
  std::lock_guard<std::mutex> lock(g_mutex);
  ImportResult result = attach_voice_locked(asset_id, bytes, path_filename_utf8(path));
  if (result.ok && std::atomic_load(&g_runtime)->initialized) result.message += " Restart to apply it.";
  g_message = result.message;
  return result;
}

bool remove_voice(const std::string& asset_id, std::string* error) {
  std::string local; if (!error) error = &local;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!mutable_profile_locked(error)) return false;
  auto asset = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) { return item.info.id == asset_id; });
  if (asset == g_assets.end() || asset->voice.stored_path.empty()) { *error = "That skin has no voice bank."; return false; }
  const AssetRecord previous = *asset;
  const Profile previous_profile = g_profile;
  const fs::path stored = g_root / fs::u8path(asset->voice.stored_path);
  asset->voice = AssetRecord::Voice{};
  asset->info.voice.clear(); asset->info.voice_source.clear();
  ++g_profile.generation;
  if (!save_state_locked(error)) { *asset = previous; g_profile = previous_profile; return false; }
  DeleteFileW(stored.c_str());
  g_message = asset->info.name + " uses the game's own voice again" +
              (std::atomic_load(&g_runtime)->initialized ? "; restart to apply it." : ".");
  return true;
}

// ---- a skin's own portrait and stock icon ----

static const char* picture_word(const std::string& kind) { return kind == "csp" ? "portrait" : "stock icon"; }
static AssetRecord* skin_locked(const std::string& skin_id) {
  for (auto& item : g_assets)
    if (item.info.id == skin_id && item.info.kind == "character_costume") return &item;
  return nullptr;
}
static const AssetRecord::Companion* picture_of(const AssetRecord& asset, const std::string& kind) {
  for (const auto& companion : asset.companions) if (companion.kind == kind) return &companion;
  return nullptr;
}
// The fighter a skin or a costume's picture entry belongs to; null for stages and effects.
static const FighterFamily* entry_family(const AssetRecord& asset) {
  SlotName slot;
  if (asset.info.kind != "character_costume" && asset.info.kind != kPortraitKind) return nullptr;
  return find_slot(portrait_slot(asset.info.target_path), &slot) ? slot.family : nullptr;
}

// Stores `png` as the skin's own picture of that kind. The file is written under a new name first
// and the record changed after, so a failed save leaves the old picture as it was.
static ImportResult attach_picture_locked(const std::string& skin_id, const std::string& kind,
                                          const std::vector<uint8_t>& png, const std::string& source_name,
                                          const std::string& from_entry = std::string()) {
  ImportResult result;
  std::string error;
  if (!mutable_profile_locked(&error)) { result.message = error; return result; }
  if (kind != "csp" && kind != "stock") { result.message = "A picture is a portrait or a stock icon."; return result; }
  AssetRecord* asset = skin_locked(skin_id);
  if (!asset) { result.message = "A picture belongs to an installed skin; that skin is not in the list."; return result; }
  if (!png_companion(png, &error)) { result.message = error; return result; }
  const std::string digest = sha256(png);
  if (digest.empty()) { result.message = "The picture could not be hashed."; return result; }
  AssetRecord::Companion picture;
  picture.kind = kind;
  picture.stored_path = (fs::path(L"assets") / fs::u8path(asset->info.id) / L"companions" /
                         fs::u8path(player_picture_file(kind, digest))).generic_u8string();
  picture.sha256 = digest;
  picture.source_member = source_name;
  const AssetRecord::Companion* had = picture_of(*asset, kind);
  const bool same = had && had->sha256 == digest;
  const std::string old_path = had ? had->stored_path : std::string();
  const std::string old_source = had ? had->source_member : std::string();
  if (!write_atomic(g_root / fs::u8path(picture.stored_path), png.data(), png.size(), &error)) { result.message = error; return result; }
  const AssetRecord previous = *asset;
  const Profile previous_profile = g_profile;
  asset->companions.erase(std::remove_if(asset->companions.begin(), asset->companions.end(),
      [&](const AssetRecord::Companion& c) { return c.kind == kind; }), asset->companions.end());
  asset->companions.push_back(picture);
  // The portrait first: the list's preview is the first picture it finds.
  std::stable_sort(asset->companions.begin(), asset->companions.end(),
      [](const AssetRecord::Companion& a, const AssetRecord::Companion& b) { return a.kind == "csp" && b.kind != "csp"; });
  // The note about the picture this one replaces goes with it.
  if (!old_source.empty()) {
    auto& notes = asset->info.unsupported_companions;
    const std::string tail = ": " + old_source;
    notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const std::string& note) {
      return note.size() >= tail.size() && note.compare(note.size() - tail.size(), tail.size(), tail) == 0;
    }), notes.end());
  }
  if (!same) ++g_profile.generation;   // the same skin, another picture
  // A costume's picture entry for this skin's own costume, whose every picture the skin now carries,
  // has done its job: it is switched off, so the standard costume shows the game's picture again.
  bool retired = false;
  const auto entry = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
    return !from_entry.empty() && item.info.id == from_entry && item.info.kind == kPortraitKind &&
           item.info.target_path == asset->info.target_path + kPortraitSuffix;
  });
  if (entry != g_assets.end()) {
    const auto pick = g_profile.selections.find(entry->info.target_path);
    const bool carried = std::all_of(entry->companions.begin(), entry->companions.end(),
        [&](const AssetRecord::Companion& c) {
          const AssetRecord::Companion* mine = picture_of(*asset, c.kind);
          return (c.kind != "csp" && c.kind != "stock") || (mine && mine->sha256 == c.sha256);
        });
    if (pick != g_profile.selections.end() && pick->second == from_entry && carried) {
      pick->second = kVanillaSelection;
      ++g_profile.generation;
      retired = true;
    }
  }
  if (!save_state_locked(&error)) {
    *asset = previous; g_profile = previous_profile;
    if (picture.stored_path != old_path) DeleteFileW((g_root / fs::u8path(picture.stored_path)).c_str());
    result.message = error; return result;
  }
  if (!old_path.empty() && old_path != picture.stored_path) DeleteFileW((g_root / fs::u8path(old_path)).c_str());
  g_message = std::string(kind == "csp" ? "Portrait" : "Stock icon") + " of " + asset->info.name + " set from " +
              source_name + "." + (retired ? " The standard costume shows its own picture again." : "");
  result.ok = true; result.already_present = same; result.asset_id = asset->info.id; result.message = g_message;
  return result;
}

ImportResult set_skin_portrait(const std::string& skin_id, const std::string& png_path, const std::string& kind) {
  const fs::path path = fs::u8path(png_path);
  std::vector<uint8_t> bytes;
  std::string error;
  if (!read_bounded(path, 16ull * 1024 * 1024, &bytes, &error)) return {false, false, {}, error};
  std::lock_guard<std::mutex> lock(g_mutex);
  ImportResult result = attach_picture_locked(skin_id, kind, bytes, path_filename_utf8(path));
  if (result.ok && std::atomic_load(&g_runtime)->initialized) result.message += " Restart to apply it.";
  g_message = result.message;
  return result;
}

bool set_skin_portrait_from(const std::string& skin_id, const std::string& source_asset_id,
                            const std::string& kind, std::string* error) {
  std::string local; if (!error) error = &local;
  std::lock_guard<std::mutex> lock(g_mutex);
  const AssetRecord* skin = skin_locked(skin_id);
  const auto source = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
    return item.info.id == source_asset_id;
  });
  const AssetRecord::Companion* picture = source == g_assets.end() ? nullptr : picture_of(*source, kind);
  if (!skin || !picture || source_asset_id == skin_id || !entry_family(*source) ||
      entry_family(*source) != entry_family(*skin)) {
    *error = std::string("That entry has no ") + picture_word(kind) + " for this skin."; return false;
  }
  std::vector<uint8_t> bytes;
  if (!read_bounded(g_root / fs::u8path(picture->stored_path), 16ull * 1024 * 1024, &bytes, error) ||
      sha256(bytes) != picture->sha256) {
    *error = "The " + std::string(picture_word(kind)) + " of " + source->info.name + " is missing or changed."; return false;
  }
  const std::string from = source->info.name;   // attach_picture_locked changes the list's records
  ImportResult result = attach_picture_locked(skin_id, kind, bytes, from, source_asset_id);
  if (result.ok && std::atomic_load(&g_runtime)->initialized) result.message += " Restart to apply it.";
  if (result.ok) g_message = result.message; else *error = result.message;
  return result.ok;
}

bool clear_skin_portrait(const std::string& skin_id, const std::string& kind, std::string* error) {
  std::string local; if (!error) error = &local;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!mutable_profile_locked(error)) return false;
  AssetRecord* asset = skin_locked(skin_id);
  const AssetRecord::Companion* had = asset ? picture_of(*asset, kind) : nullptr;
  if (!had) { *error = std::string("That skin has no ") + picture_word(kind) + " of its own."; return false; }
  const AssetRecord previous = *asset;
  const Profile previous_profile = g_profile;
  const fs::path stored = g_root / fs::u8path(had->stored_path);
  const std::string tail = ": " + had->source_member;
  auto& notes = asset->info.unsupported_companions;
  notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const std::string& note) {
    return tail.size() > 2 && note.size() >= tail.size() && note.compare(note.size() - tail.size(), tail.size(), tail) == 0;
  }), notes.end());
  asset->companions.erase(std::remove_if(asset->companions.begin(), asset->companions.end(),
      [&](const AssetRecord::Companion& c) { return c.kind == kind; }), asset->companions.end());
  ++g_profile.generation;
  if (!save_state_locked(error)) { *asset = previous; g_profile = previous_profile; return false; }
  DeleteFileW(stored.c_str());
  g_message = asset->info.name + " has no " + picture_word(kind) + " of its own now" +
              (std::atomic_load(&g_runtime)->initialized ? "; restart to apply it." : ".");
  return true;
}

std::vector<PortraitChoice> portrait_choices(const std::string& skin_id, const std::string& kind) {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<PortraitChoice> out;
  const AssetRecord* skin = skin_locked(skin_id);
  const FighterFamily* family = skin ? entry_family(*skin) : nullptr;
  if (!family) return out;
  for (const auto& item : g_assets)
    if (item.info.id != skin_id && entry_family(item) == family && picture_of(item, kind))
      out.push_back({item.info.id, item.info.name});
  return out;
}

PortraitSource skin_portrait_source(const std::string& skin_id, const std::string& kind) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const AssetRecord* skin = skin_locked(skin_id);
  if (!skin) return PortraitSource::Standard;
  if (picture_of(*skin, kind)) return PortraitSource::Own;
  // The costume's added picture counts while its entry is switched on.
  const std::string target = skin->info.target_path + kPortraitSuffix;
  const auto pick = g_profile.selections.find(target);
  if (pick == g_profile.selections.end() || pick->second == kVanillaSelection) return PortraitSource::Standard;
  for (const auto& item : g_assets)
    if (item.info.id == pick->second && item.info.kind == kPortraitKind && item.info.target_path == target &&
        picture_of(item, kind)) return PortraitSource::Costume;
  return PortraitSource::Standard;
}

std::vector<CostumeSlot> costume_slots() {
  std::vector<CostumeSlot> out;
  for (const auto& family : families) {
    // the family's own color order, which is the game's
    std::string colors = family.colors;
    for (size_t at = 0; at < colors.size(); at += 3) {
      const std::string code = colors.substr(at, 2);
      for (const auto& color : color_names)
        if (code == color.code) out.push_back({slot_file({&family, &color}), family.display_name, color.label});
    }
  }
  return out;
}

std::vector<AssetInfo> assets() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<AssetInfo> out;
  out.reserve(g_assets.size());
  // The online verdict of each applied choice, from the snapshot this launch runs with. A costume
  // with an English twin has two disc files: it is off online when either one is.
  const auto runtime = std::atomic_load(&g_runtime);
  std::map<std::string, const RuntimeAsset*> verdicts;
  for (const auto& entry : runtime->by_start) {
    if (entry.second.kind == kVoiceKind) continue;   // a bank file carries the skin's id and no verdict
    const RuntimeAsset*& have = verdicts[entry.second.id];
    if (!have || (have->online_allowed && !entry.second.online_allowed)) have = &entry.second;
  }
  for (const auto& record : g_assets) {
    AssetInfo info = record.info;
    info.pack = pack_key(record);
    if (info.source_name.empty()) info.source_name = record.source_name;   // an import: the file it came from
    const auto verdict = verdicts.find(info.id);
    if (verdict != verdicts.end()) {
      info.online_allowed = verdict->second->online_allowed;
      info.online_message = verdict->second->online_reason;
    } else if (record.online_known) {
      // A disc skin was judged when it was scanned, against the same standard file.
      info.online_allowed = record.online_ok;
      info.online_message = record.online_note;
    }
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

// True when the running snapshot matched the saved profile before the live picks not published yet
// changed it, so publishing them leaves nothing waiting for a restart.
static bool g_live_in_step = false, g_live_unpublished = false;

// Voice banks, at startup only (docs/voice-mods.md). Every disc bank file that an installed skin has
// a fitting bank for becomes a served file: it starts as the disc's own bank, and plan_match_voices
// switches it between the disc's and the skins' at each match load. Every installed skin counts,
// selected or not, so a skin picked later on the character select screen brings its voice. The
// file table gets one length per bank file for the whole session, the longest of the disc's and
// the candidates', and everything served is padded to it: the entry never changes again, and a
// read that started on one bank and ends on another never runs past the file.
static void publish_voices_locked(uint8_t* fst, const std::vector<FstFile>& files, RuntimeState* next) {
  struct Loaded {
    const AssetRecord* asset = nullptr; const VoiceBank* bank = nullptr;
    std::vector<uint8_t> bytes; BankLayout layout; bool used = false;
  };
  std::vector<Loaded> voices;
  for (const auto& asset : g_assets) {
    if (asset.info.kind != "character_costume" || !asset.info.available || asset.voice.stored_path.empty()) continue;
    Loaded item;
    item.asset = &asset; item.bank = bank_by_file(asset.voice.bank);
    std::string error;
    SlotName slot;
    if (!item.bank || !find_slot(asset.info.target_path, &slot) || family_bank(*slot.family) != item.bank ||
        !safe_relative_path(asset.voice.stored_path) ||
        !read_bounded(g_root / fs::u8path(asset.voice.stored_path), kMaxAssetBytes, &item.bytes, &error) ||
        sha256(item.bytes) != asset.voice.sha256 || !parse_bank(item.bytes, &item.layout, &error)) {
      host::log("cosmetics: the voice bank of %s is missing or changed; the game's own sounds are used",
                asset.info.name.c_str());
      continue;
    }
    voices.push_back(std::move(item));
  }
  if (voices.empty()) return;
  for (const auto& file : files) {
    const std::string path = lower(file.path);
    const size_t slash = path.find_last_of('/');
    const VoiceBank* bank = bank_by_file(slash == std::string::npos ? path : path.substr(slash + 1));
    if (!bank || path.rfind("audio/", 0) != 0 || next->by_start.count(file.start)) continue;
    if (std::none_of(voices.begin(), voices.end(), [&](const Loaded& voice) { return voice.bank == bank; })) continue;
    if (file.size < 0x20 || file.size > kMaxAssetBytes) continue;
    std::vector<uint8_t> disc(file.size);
    BankLayout disc_layout;
    std::string error;
    if (!host::disc_read(file.start, disc.data(), file.size) || !parse_bank(disc, &disc_layout, &error)) {
      host::log("cosmetics: %s on this disc is not a bank the voice mods can replace (%s)", file.path.c_str(), error.c_str());
      continue;
    }
    VoiceFile served;
    served.start = file.start; served.disc_size = file.size; served.length = file.size;
    served.bank = bank->file; served.path = file.path; served.bank_index = bank->index;
    std::vector<Loaded*> fitting;
    for (auto& voice : voices) {
      // The same sounds as this disc file: count, first id and channels per sound, and samples
      // inside the room the game's loader budgets for the bank.
      if (voice.bank != bank || voice.layout.count != disc_layout.count || voice.layout.base != disc_layout.base ||
          first_channel_difference(disc_layout.channels, voice.layout.channels) >= 0 ||
          bank_sample_room(voice.layout) > bank->budget) continue;
      fitting.push_back(&voice);
      served.length = std::max<uint32_t>(served.length, voice.layout.data_offset + bank_sample_room(voice.layout));
    }
    if (fitting.empty()) continue;
    for (Loaded* voice : fitting) {
      served.candidates[voice->asset->info.id] =
          std::make_shared<const std::vector<uint8_t>>(served_bank(voice->bytes, voice->layout, served.length));
      voice->used = true;
      host::log("cosmetics: voice bank of %s fits %s (%u sounds from id %u)", voice->asset->info.name.c_str(),
                file.path.c_str(), disc_layout.count, disc_layout.base);
    }
    if (served.length != file.size) put_be32(fst + (size_t)file.index * 12 + 8, served.length);
    next->by_start[file.start] = RuntimeAsset{std::string(), bank->file, nullptr, file.size, true, kVoiceKind,
                                              std::string(), served.length};
    next->voice_files.push_back(std::move(served));
  }
  for (const auto& voice : voices)
    if (!voice.used)
      host::log("cosmetics: the voice bank of %s fits no %s on this disc (%u sounds from id %u); the game's own sounds are used",
                voice.asset->info.name.c_str(), voice.bank->file, voice.layout.count, voice.layout.base);
  if (!next->voice_files.empty())
    host::log("cosmetics: %zu sound bank files can be served by a skin's voice", next->voice_files.size());
}

// The whole profile at startup (only_slot null), or one costume slot again while the game runs
// (republish_slot): the snapshot is then a copy of the running one with that slot's files replaced.
static void publish_locked(uint8_t* fst, uint32_t fst_size, const std::string* only_slot, RepublishResult* republished) {
  const auto current = std::atomic_load(&g_runtime);
  auto next = only_slot ? std::make_shared<RuntimeState>(*current) : std::make_shared<RuntimeState>();
  next->initialized = true; next->generation = g_profile.generation;
  if (!only_slot || g_live_in_step) next->fingerprint = desired_fingerprint_locked();
  if (only_slot && (!g_configured || !g_catalog_valid || !g_profile_valid || !current->initialized)) {
    republished->message = "The cosmetic catalog is not running."; return;
  }
  if (!only_slot && (!g_configured || !g_catalog_valid || !g_profile_valid || !g_profile.enabled)) {
    std::atomic_store(&g_runtime, std::shared_ptr<const RuntimeState>(next));
    host::log("cosmetics: vanilla profile (%s)", !g_profile.enabled ? "disabled" : "catalog unavailable");
    return;
  }
  std::vector<FstFile> files; std::string error;
  if (!parse_fst(fst, fst_size, &files, &error)) {
    g_message = error; host::log("cosmetics: %s", error.c_str());
    if (only_slot) { republished->message = error; return; }
    std::atomic_store(&g_runtime, std::shared_ptr<const RuntimeState>(next)); return;
  }
  std::vector<FstFile> slot_files;   // the slot's disc files, with the disc's own lengths
  if (only_slot) {
    // The table carries the lengths published so far: every overridden file gets the disc's own
    // length back in the list, which is what the checks below compare against.
    for (auto& file : files) {
      const auto served = current->by_start.find(file.start);
      if (served != current->by_start.end()) file.size = served->second.vanilla_size;
    }
    const std::string wanted = lower(*only_slot);
    const std::string english = wanted.size() > 4 && wanted.compare(wanted.size() - 4, 4, ".dat") == 0 ?
        wanted.substr(0, wanted.size() - 4) + ".usd" : std::string();
    for (const auto& file : files) {
      const std::string path = lower(file.path);
      const size_t slash = path.find_last_of('/');
      const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
      if (base == wanted || (!english.empty() && base == english)) slot_files.push_back(file);
    }
    if (slot_files.empty()) { republished->message = "This disc has no " + *only_slot + "."; return; }
    // Back to the disc's own files first; the slot's pick, if it has one, is applied by the loop.
    bool was_served = false;
    for (const auto& file : slot_files) {
      was_served |= next->by_start.erase(file.start) != 0;
      put_be32(fst + (size_t)file.index * 12 + 8, file.size);
    }
    if (was_served && next->assets) --next->assets;
    next->companions.erase(std::remove_if(next->companions.begin(), next->companions.end(),
        [&](const CompanionOverride& have) { return lower(have.target_path) == wanted; }), next->companions.end());
  }
  std::map<std::string, bool> effect_done;
  std::vector<std::string> runtime_errors;
  std::vector<const AssetRecord*> portraits;
  for (const auto& pick : g_profile.selections) {
    if (pick.second == kVanillaSelection) continue;
    // One slot: its skin and its own portrait entry, and nothing while the profile is switched off.
    if (only_slot && (!g_profile.enabled || (lower(pick.first) != lower(*only_slot) &&
                                              lower(pick.first) != lower(*only_slot + kPortraitSuffix)))) continue;
    auto asset = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
      return item.info.id == pick.second && selection_key(item) == pick.first;
    });
    if (asset == g_assets.end()) { host::log("cosmetics: missing catalog asset %s", pick.second.c_str()); continue; }
    if (asset->info.kind == kPortraitKind) { portraits.push_back(&*asset); continue; }
    if (asset->info.kind == "effect_visual" && effect_done[asset->info.target_path]) continue;
    if (asset->info.kind == "effect_visual") effect_done[asset->info.target_path] = true;
    std::vector<uint8_t> bytes = load_runtime_asset_locked(*asset, &error);
    if (bytes.empty()) { host::log("cosmetics: %s", error.c_str()); continue; }
    std::string wanted = lower(asset->info.target_path);
    // The English twin of a per-language file. The disc holds PlCaRe.usd and GrPs.usd beside the
    // .dat, and the game loads the .usd when its language is English, so a costume or stage for
    // the .dat slot replaces both: replacing only the .dat changed nothing for English players.
    std::string english;
    if (asset->info.kind != "effect_visual" && wanted.size() > 4 &&
        wanted.compare(wanted.size() - 4, 4, ".dat") == 0)
      english = wanted.substr(0, wanted.size() - 4) + ".usd";
    std::vector<FstFile*> matches, english_matches;
    for (auto& file : files) {
      std::string path = lower(file.path);
      size_t slash = path.find_last_of('/');
      std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
      const bool by_name = wanted.find('/') == std::string::npos;
      if (path == wanted || (by_name && base == wanted)) matches.push_back(&file);
      else if (!english.empty() && (path == english || (by_name && base == english))) english_matches.push_back(&file);
    }
    if (matches.size() != 1 || english_matches.size() > 1) {
      host::log("cosmetics: %s resolved to %zu ISO files (%zu English); override not applied", pick.first.c_str(),
                matches.size(), english_matches.size());
      continue;
    }
    std::vector<FstFile*> targets{matches[0]};
    if (!english_matches.empty()) targets.push_back(english_matches[0]);
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
    bool applied = false;
    for (FstFile* target : targets) {
      FstFile& disc_file = *target;
      std::vector<uint8_t> served = bytes;   // per disc file: the online rule and the length below are per file
      bool online_allowed = true;
      std::string online_reason;   // kept for the Mods tab: the verdict is never only in the log
      if (asset->info.kind == "stage_visual") {
        std::vector<uint8_t> clean(disc_file.size);
        std::string validation_error;
        online_allowed = host::disc_read(disc_file.start, clean.data(), disc_file.size) &&
                         visual_dat_only(clean, served, &validation_error);
        host::log("cosmetics: stage %s %s online (%s)", disc_file.path.c_str(),
                  online_allowed ? "allowed" : "uses vanilla", validation_error.c_str());
        online_reason = online_allowed ? std::string("textures only") : online_reason_short(validation_error);
      }
      if (asset->info.kind == "character_costume") {
        std::vector<uint8_t> clean(disc_file.size);
        std::string validation_error;
        online_allowed = host::disc_read(disc_file.start, clean.data(), disc_file.size) &&
                         costume_skeleton_matches(clean, served, &validation_error);
        host::log("cosmetics: costume %s %s online (%s)", disc_file.path.c_str(),
                  online_allowed ? "allowed" : "uses vanilla", validation_error.c_str());
        online_reason = online_reason_short(validation_error);
      }
      if (!online_allowed) {
        // This file is the override offline and the disc's own file online, and the game sees one
        // length for it (the FST is patched once). The game also checks that an archive's own
        // length field equals the file's length (HSD_ArchiveParse) and stops when they differ.
        // So the file's length is the longer of the two, the shorter one is padded with zeros,
        // and the length field of whichever is served says that length (the disc's: see read()).
        const size_t extent = std::max<size_t>(served.size(), disc_file.size);
        if (extent > std::numeric_limits<uint32_t>::max() || served.size() < 4) continue;
        served.resize(extent, 0);
        put_be32(served.data(), (uint32_t)extent);
      }
      if (served.size() > std::numeric_limits<uint32_t>::max()) continue;
      put_be32(fst + (size_t)disc_file.index * 12 + 8, (uint32_t)served.size());
      RuntimeAsset active{asset->info.id, asset->info.target_path,
                          std::make_shared<const std::vector<uint8_t>>(std::move(served)),
                          disc_file.size, online_allowed, asset->info.kind, std::move(online_reason)};
      next->by_start[disc_file.start] = std::move(active);
      host::log("cosmetics: %s -> %s at %08X (%u vanilla bytes, %zu override bytes)",
                asset->info.name.c_str(), disc_file.path.c_str(), disc_file.start, disc_file.size,
                next->by_start[disc_file.start].bytes->size());
      applied = true;
    }
    if (!applied) continue;
    ++next->assets;
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
  // A slot's own portrait and stock icon, after the skins: they show where the selected skin brought
  // no picture of that kind.
  for (const AssetRecord* asset : portraits) {
    const std::string slot = portrait_slot(asset->info.target_path);
    bool any = false;
    for (const auto& companion : asset->companions) {
      if (companion.kind != "csp" && companion.kind != "stock") continue;
      std::vector<uint8_t> picture;
      const fs::path path = g_root / fs::u8path(companion.stored_path);
      std::string picture_error;
      if (!read_bounded(path, kMaxAssetBytes, &picture, &picture_error) || sha256(picture) != companion.sha256) {
        host::log("cosmetics: %s for %s is missing or changed; using vanilla",
                  companion.kind == "csp" ? "portrait" : "stock icon", slot.c_str());
        continue;
      }
      // One rule for the player: a skin that brings its own picture shows it; a picture added to
      // the slot on its own fills in wherever there is none (the standard costume, or a skin
      // without one). So stepping through skins on the character select always changes the picture
      // with the skin, and taking the skin off brings the added picture back.
      const bool skin_has_own = std::any_of(next->companions.begin(), next->companions.end(),
          [&](const CompanionOverride& have) { return have.kind == companion.kind && have.target_path == slot; });
      if (skin_has_own) continue;
      next->companions.push_back({companion.kind, slot, path.string()});
      host::log("cosmetics: %s for %s from %s", companion.kind == "csp" ? "portrait" : "stock icon", slot.c_str(),
                companion.source_member.c_str());
      any = true;
    }
    if (any && !only_slot) ++next->assets;   // one slot again: its portrait entry was counted at startup
  }
  // One slot again keeps the bank files as they are (they came with the copy of the snapshot): which
  // bank a file serves is decided at match load, from the skins the slots serve then.
  if (!only_slot) publish_voices_locked(fst, files, next.get());
  std::atomic_store(&g_runtime, std::shared_ptr<const RuntimeState>(next));
  if (only_slot) {
    bool served = false;
    for (const auto& file : slot_files) {
      RepublishedFile out;
      out.fst_index = file.index; out.vanilla_start = file.start; out.length = file.size;
      const auto now = next->by_start.find(file.start);
      if (now != next->by_start.end()) {
        out.overridden = served = true;
        out.length = (uint32_t)now->second.bytes->size();
        out.online_allowed = now->second.online_allowed;
        out.asset_id = now->second.id;
      }
      republished->files.push_back(std::move(out));
    }
    if (!served) host::log("cosmetics: %s is the standard costume again", only_slot->c_str());
    g_live_unpublished = false;
    republished->ok = true;
    return;
  }
  g_message = next->assets == 0 ? "No selected cosmetic matched this ISO." :
              std::to_string(next->assets) + " cosmetic override(s) active for this launch.";
  for (const auto& issue : runtime_errors) g_message += " " + issue;
  // The game loads every stage and fighter file whole into heaps of a fixed size, laid out for the
  // disc's own files (lbheap.c: 5.0 MB and 6.3 MB of main memory), and stops with
  // `assertion "memp_kouho"` in lbmemory.c when a file finds no room there. That includes the
  // title screen, which preloads a random demo stage and four fighters. Nothing is changed here:
  // a file that grew by more than this is named, so the stop has a cause the player can act on.
  constexpr size_t kLargeGrowth = 1u << 20;
  size_t large = 0, growth = 0;
  std::string largest; size_t largest_growth = 0;
  for (const auto& served : next->by_start) {
    const RuntimeAsset& item = served.second;
    if (!item.bytes || item.bytes->size() <= (size_t)item.vanilla_size + kLargeGrowth) continue;
    const size_t grew = item.bytes->size() - item.vanilla_size;
    ++large; growth += grew;
    if (grew > largest_growth) { largest_growth = grew; largest = item.target_path; }
    host::log("cosmetics: %s is %.1f MB larger than the game's file; the game's memory is laid out for the original",
              item.target_path.c_str(), grew / 1048576.0);
  }
  if (large) {
    char text[256];
    std::snprintf(text, sizeof text, " %zu mod file(s) are much larger than the game's own (%.1f MB more in all, the most in %s). "
                  "The game can stop with a memory error when it loads them; if it does, turn the largest ones off.",
                  large, growth / 1048576.0, largest.c_str());
    g_message += text;
    host::log("cosmetics: %zu oversized override(s), %.1f MB over the originals; a lbmemory.c stop on load means one did not fit",
              large, growth / 1048576.0);
  }
}

void apply_to_fst(uint8_t* fst, uint32_t fst_size) {
  std::lock_guard<std::mutex> lock(g_mutex);
  publish_locked(fst, fst_size, nullptr, nullptr);
}

RepublishResult republish_slot(uint8_t* fst, uint32_t fst_size, const std::string& slot) {
  RepublishResult result;
  std::lock_guard<std::mutex> lock(g_mutex);
  SlotName known;
  if (!fst || !find_slot(slot, &known)) { result.message = "That is not a costume slot."; return result; }
  const std::string target = slot_file(known);
  publish_locked(fst, fst_size, &target, &result);
  if (!result.ok) host::log("cosmetics: %s was not published again (%s)", target.c_str(), result.message.c_str());
  return result;
}

std::string applied_asset(uint32_t vanilla_file_start) {
  const auto runtime = std::atomic_load(&g_runtime);
  const auto found = runtime->by_start.find(vanilla_file_start);
  return found == runtime->by_start.end() ? std::string() : found->second.id;
}

// The character select's fighter numbers, in its own order, as the families' file codes.
std::string costume_slot_file(int css_character, int costume) {
  static constexpr const char* codes[] = {"Ca", "Dk", "Fx", "Gw", "Kb", "Kp", "Lk", "Lg", "Mr", "Ms", "Mt", "Ns", "Pe",
                                          "Pk", "Pp", "Pr", "Ss", "Ys", "Zd", "Sk", "Fc", "Cl", "Dr", "Fe", "Pc", "Gn"};
  if (css_character < 0 || css_character >= (int)std::size(codes) || costume < 0) return {};
  for (const auto& family : families) {
    if (std::string(family.file_code) != codes[css_character]) continue;
    const std::string colors = family.colors;   // "Nr Or La Gr": the game's costume order
    const size_t at = (size_t)costume * 3;
    if (at + 2 > colors.size()) return {};
    return std::string("Pl") + family.file_code + colors.substr(at, 2) + ".dat";
  }
  return {};
}

// The online verdict of a skin that is about to be picked live. A disc skin was judged when it was
// scanned; an import is compared with the standard costume here, the way startup does it.
static bool live_online_ok_locked(const AssetRecord& asset, const std::vector<uint8_t>& candidate) {
  if (asset.online_known) return asset.online_ok;
  uint32_t offset = 0, size = 0;
  if (!host::disc_find_file(asset.info.target_path, &offset, &size) || !size || size > kMaxAssetBytes) return false;
  std::vector<uint8_t> clean(size);
  std::string detail;
  return host::disc_read(offset, clean.data(), size) && costume_skeleton_matches(clean, candidate, &detail);
}

static bool select_live_locked(const std::string& slot, const std::string& asset_id, std::string* error) {
  if (!ready_locked(error)) return false;
  if (g_online_freezes.load(std::memory_order_relaxed)) {
    *error = "Skins cannot change while an online match is queued or running."; return false;
  }
  SlotName known;
  if (!find_slot(slot, &known)) { *error = "That is not a costume slot."; return false; }
  const std::string target = slot_file(known);
  const bool standard = asset_id.empty() || asset_id == kVanillaSelection;
  std::string name = "the standard costume";
  if (!standard) {
    auto asset = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
      return item.info.id == asset_id && item.info.kind == "character_costume" && selection_key(item) == target;
    });
    if (asset == g_assets.end()) { *error = "The selected variant does not belong to that slot."; return false; }
    const bool online = online_active();
    // A scanned skin known to be off online is refused before its file is read.
    if (online && asset->online_known && !asset->online_ok) {
      *error = "That skin is off online (" + asset->online_note + ")."; return false;
    }
    std::string validation_error;
    const std::vector<uint8_t> candidate = load_runtime_asset_locked(*asset, &validation_error);
    asset->info.available = !candidate.empty();
    if (!asset->info.available) {
      asset->info.availability_message = validation_error;
      *error = "The selected variant is unavailable or invalid; the costume is unchanged."; return false;
    }
    if (online && !live_online_ok_locked(*asset, candidate)) {
      *error = "That skin changes more than looks, so it cannot be picked online."; return false;
    }
    name = asset->info.name;
  }
  const auto current = g_profile.selections.find(target);
  const std::string now = current == g_profile.selections.end() ? std::string(kVanillaSelection) : current->second;
  if (standard ? now == kVanillaSelection : (now == asset_id && g_profile.enabled)) return true;
  if (!g_live_unpublished) {
    const auto runtime = std::atomic_load(&g_runtime);
    g_live_in_step = runtime->initialized && runtime->fingerprint == desired_fingerprint_locked();
  }
  Profile previous = g_profile;
  if (standard) g_profile.selections[target] = kVanillaSelection;
  else { g_profile.enabled = true; g_profile.selections[target] = asset_id; }
  ++g_profile.generation;
  if (!save_profile_locked(error)) { g_profile = std::move(previous); return false; }
  g_live_unpublished = true;
  g_message = name + " selected for " + target + ".";
  return true;
}

bool select_variant_live(const std::string& slot, const std::string& asset_id, std::string* error) {
  std::string local; if (!error) error = &local;
  std::lock_guard<std::mutex> lock(g_mutex);
  return select_live_locked(slot, asset_id, error);
}

// Nana's costume slot for one of Popo's (the same place in each one's costume order), or empty.
static std::string climber_partner(const std::string& target) {
  static constexpr const char* popo[] = {"PlPpNr.dat", "PlPpGr.dat", "PlPpOr.dat", "PlPpRe.dat"};
  static constexpr const char* nana[] = {"PlNnNr.dat", "PlNnYe.dat", "PlNnAq.dat", "PlNnWh.dat"};
  for (size_t i = 0; i < std::size(popo); ++i) if (lower(target) == lower(popo[i])) return nana[i];
  return {};
}

// The skin for `partner_target` that belongs with `skin`: the same pack and the same set of it, or
// for a vault import the skin the vault itself names as the pair. Null when there is none.
static const AssetRecord* partner_skin_locked(const std::string& skin_id, const std::string& partner_target) {
  if (skin_id.empty()) return nullptr;
  const auto skin = std::find_if(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
    return item.info.id == skin_id && item.info.kind == "character_costume";
  });
  if (skin == g_assets.end()) return nullptr;
  for (const auto& other : g_assets) {
    if (other.info.kind != "character_costume" || !other.info.available || selection_key(other) != partner_target ||
        other.source_kind != skin->source_kind) continue;
    if (skin->source_kind == kDiscSource) {
      if (other.source_id == skin->source_id && other.info.variant == skin->info.variant) return &other;
      continue;
    }
    if (other.source_name != skin->source_name || other.source_id.empty() || skin->source_id.empty()) continue;
    const auto names = [](const AssetRecord& a, const AssetRecord& b) {
      return std::find(a.info.dependencies.begin(), a.info.dependencies.end(), b.source_id) != a.info.dependencies.end();
    };
    if (names(*skin, other) || names(other, *skin)) return &other;
  }
  return nullptr;
}

// After Popo's slot went from previous_id to asset_id: Nana's follows (see LiveCycle).
static void pair_climbers_locked(const std::string& target, LiveCycle* result) {
  const std::string partner = climber_partner(target);
  if (partner.empty()) return;
  const auto picked = g_profile.selections.find(partner);
  const std::string wears = picked == g_profile.selections.end() || picked->second == kVanillaSelection ?
      std::string() : picked->second;
  const AssetRecord* before = partner_skin_locked(result->previous_id, partner);
  const AssetRecord* after = partner_skin_locked(result->asset_id, partner);
  // She follows only from where Popo's previous pick put her: its partner, or the standard costume
  // when it had none. Anything else is a pick of her own.
  if (wears != (before ? before->info.id : std::string())) return;
  const std::string message = g_message;
  std::string wanted = after ? after->info.id : std::string(), error;
  bool changed = wanted != wears && select_live_locked(partner, wanted, &error);
  // Her partner skin cannot be picked (online, or its file is gone): not left in the old pair's skin.
  if (!changed && !wanted.empty() && !wears.empty()) { wanted.clear(); changed = select_live_locked(partner, wanted, &error); }
  g_message = message;
  if (!changed) return;
  result->partner_slot = partner;
  result->partner_previous_id = wears;
}

// A skin scanned from the disc this session serves as its own files.
static bool session_pack_record(const AssetRecord& asset) {
  return !g_session_pack.empty() && asset.source_kind == kDiscSource && asset.source_id == g_session_pack;
}

void set_session_pack(const std::string& iso_path) {
  std::error_code ec;
  const std::string key = iso_path.empty() ? std::string() : fs::absolute(fs::u8path(iso_path), ec).u8string();
  std::lock_guard<std::mutex> lock(g_mutex);
  g_session_pack = key;
}

bool session_pack_skin(const std::string& asset_id) {
  if (asset_id.empty()) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  return std::any_of(g_assets.begin(), g_assets.end(), [&](const AssetRecord& item) {
    return item.info.id == asset_id && item.info.kind == "character_costume" && session_pack_record(item);
  });
}

LiveCycle cycle_slot_live(const std::string& slot, int direction) {
  LiveCycle result;
  std::lock_guard<std::mutex> lock(g_mutex);
  SlotName known;
  if (!ready_locked(&result.message)) return result;
  if (!find_slot(slot, &known)) { result.message = "That is not a costume slot."; return result; }
  const std::string target = slot_file(known);
  // The standard costume, then the slot's skins in catalog order. The session pack's plain costume
  // is left out: this session already serves it as the standard costume (set_session_pack).
  std::vector<std::string> ids{std::string()}, names{"Standard"};
  for (const auto& asset : g_assets) {
    if (asset.info.kind != "character_costume" || !asset.info.available || selection_key(asset) != target) continue;
    if (asset.info.variant.empty() && session_pack_record(asset)) continue;
    ids.push_back(asset.info.id);
    // A pack's skin is labelled by the pack and its set; an import by the name the player gave it.
    names.push_back(asset.source_kind == kDiscSource ?
        asset.source_name + (asset.info.variant.empty() ? "" : " (" + asset.info.variant + ")") : asset.info.name);
  }
  size_t at = 0;
  const auto picked = g_profile.selections.find(target);
  if (g_profile.enabled && picked != g_profile.selections.end())
    for (size_t i = 1; i < ids.size(); ++i) if (ids[i] == picked->second) at = i;
  result.previous_id = ids[at];
  result.asset_id = ids[at]; result.name = names[at];
  result.ok = true;
  const size_t count = ids.size();
  for (size_t step = 1; step < count; ++step) {
    const size_t next = direction > 0 ? (at + step) % count : (at + count - step) % count;
    std::string error;
    if (!select_live_locked(target, ids[next], &error)) { result.message = error; continue; }
    result.changed = true; result.asset_id = ids[next]; result.name = names[next]; result.message.clear();
    break;
  }
  if (!result.changed && result.message.empty()) result.message = "No other skin is installed for this costume.";
  if (result.changed) pair_climbers_locked(target, &result);
  return result;
}

// ---- voice mods: which bank a match hears (docs/voice-mods.md) ----

// The skin a costume slot serves right now, from the running snapshot: what the match will load for
// that costume. Empty for the standard costume, and for a skin that online play shows as the
// standard costume (its voice goes with it).
static std::string served_skin(const RuntimeState& runtime, const std::string& slot) {
  const std::string wanted = lower(slot);
  std::string id;
  for (const auto& entry : runtime.by_start) {
    const RuntimeAsset& file = entry.second;
    if (file.kind != "character_costume" || lower(file.target_path) != wanted) continue;
    if (!file.online_allowed && online_active()) return {};
    id = file.id;
  }
  return id;
}

// The other costume file a port's fighter also wears: Nana's for Popo's, and for Zelda and Sheik
// each other's of the same color (the two share a bank and a port).
static std::string voice_partner(const std::string& slot) {
  const std::string nana = climber_partner(slot);
  if (!nana.empty()) return nana;
  const std::string name = lower(slot);
  if (name.size() == 10 && name.compare(0, 4, "plzd") == 0) return "PlSk" + slot.substr(4);
  if (name.size() == 10 && name.compare(0, 4, "plsk") == 0) return "PlZd" + slot.substr(4);
  return {};
}

VoicePlan plan_match_voices(const MatchFighter ports[4]) {
  VoicePlan plan;
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto current = std::atomic_load(&g_runtime);
  if (!ports || !current->initialized || current->voice_files.empty()) return plan;
  auto next = std::make_shared<RuntimeState>(*current);
  const auto skin_name = [&](const std::string& id) {
    if (id.empty()) return std::string("the game's own sounds");
    for (const auto& item : g_assets) if (item.info.id == id) return item.info.name;
    return id;
  };
  bool any = false;
  for (auto& file : next->voice_files) {
    // The voice a port's costume asks for on this bank file: its skin's bank, else its partner
    // costume's, else the game's own (also when the skin's bank fits the other language only).
    const auto wanted_by = [&](const std::string& slot) {
      const std::string own = served_skin(*next, slot);
      if (!own.empty() && file.candidates.count(own)) return own;
      const std::string partner = voice_partner(slot);
      const std::string other = partner.empty() ? std::string() : served_skin(*next, partner);
      return !other.empty() && file.candidates.count(other) ? other : std::string();
    };
    int decider = -1;
    std::string wanted;
    for (int port = 0; port < 4; ++port) {
      const std::string slot = costume_slot_file(ports[port].character, ports[port].costume);
      SlotName known;
      if (slot.empty() || !find_slot(slot, &known)) continue;
      const VoiceBank* bank = family_bank(*known.family);
      if (!bank || file.bank != bank->file) continue;
      const std::string asks = wanted_by(slot);
      if (decider < 0) { decider = port; wanted = asks; continue; }
      if (asks == wanted) continue;
      // One bank per fighter file: the lowest port decided, this one is told.
      plan.notes.push_back(asks.empty() ?
          "voice: port " + std::to_string(port + 1) + "'s costume gets port " + std::to_string(decider + 1) + "'s voice (" +
              skin_name(wanted) + "): the game loads one " + file.bank :
          "voice: port " + std::to_string(port + 1) + "'s " + skin_name(asks) + " is not used: port " +
              std::to_string(decider + 1) + " decides " + file.bank + " (" + skin_name(wanted) + ")");
    }
    if (decider < 0) continue;   // no fighter of this bank in the match: it keeps what it serves
    if (wanted == file.chosen) continue;
    plan.notes.push_back("voice: " + file.path + " is " + skin_name(wanted) + " for this match (port " +
                         std::to_string(decider + 1) + ")");
    file.chosen = wanted;
    RuntimeAsset& served = next->by_start[file.start];
    served.id = wanted;
    served.bytes = wanted.empty() ? nullptr : file.candidates[wanted];
    any = true;
    if (std::none_of(plan.changed.begin(), plan.changed.end(), [&](const VoiceChange& have) { return have.file == file.bank; }))
      plan.changed.push_back({file.bank, file.bank_index});
  }
  if (any) std::atomic_store(&g_runtime, std::shared_ptr<const RuntimeState>(next));
  for (const auto& note : plan.notes) host::log("cosmetics: %s", note.c_str());
  return plan;
}

uint32_t voice_bank_count() { return (uint32_t)std::atomic_load(&g_runtime)->voice_files.size(); }

OverrideRead read(uint32_t vanilla_file_start, uint32_t file_offset, void* dst, uint32_t size) {
  auto runtime = std::atomic_load(&g_runtime);
  auto found = runtime->by_start.find(vanilla_file_start);
  if (found == runtime->by_start.end()) return OverrideRead::NotOverridden;
  if (found->second.kind == kVoiceKind && !found->second.bytes) {
    // A bank file while no skin's voice is chosen for it: the disc's own bank, under the one length
    // the file table carries for this file, with zeros past the disc file's end.
    const uint32_t vanilla_size = found->second.vanilla_size;
    const uint64_t exposed_size = found->second.length;
    if (file_offset > exposed_size || (uint64_t)file_offset + size > exposed_size + 31) return OverrideRead::Failed;
    const uint32_t disc_size = file_offset < vanilla_size ? std::min(size, vanilla_size - file_offset) : 0;
    if (disc_size && !host::disc_read(vanilla_file_start + file_offset, dst, disc_size)) return OverrideRead::Failed;
    if (disc_size < size) std::memset((uint8_t*)dst + disc_size, 0, size - disc_size);
    return OverrideRead::Success;
  }
  if (!found->second.online_allowed && online_active()) {
    const uint32_t vanilla_size = found->second.vanilla_size;
    const uint64_t exposed_size = std::max<size_t>(vanilla_size, found->second.bytes->size());
    if (file_offset > exposed_size || (uint64_t)file_offset + size > exposed_size + 31)
      return OverrideRead::Failed;
    const uint32_t disc_size = file_offset < vanilla_size ? std::min(size, vanilla_size - file_offset) : 0;
    if (disc_size && !host::disc_read(vanilla_file_start + file_offset, dst, disc_size))
      return OverrideRead::Failed;
    if (disc_size < size) std::memset((uint8_t*)dst + disc_size, 0, size - disc_size);
    // The archive's length field (its first four bytes) says the length the game was given for
    // this file, which is the override's when that is the longer one (apply_to_fst).
    uint8_t length_field[4]; put_be32(length_field, (uint32_t)exposed_size);
    for (uint32_t at = file_offset; at < 4 && at - file_offset < size; ++at)
      ((uint8_t*)dst)[at - file_offset] = length_field[at];
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

std::atomic<OnlineProbe> g_online_probe{nullptr};
void set_online_probe(OnlineProbe probe) { g_online_probe.store(probe); }
bool online_active() {
  if (g_online_freezes.load(std::memory_order_relaxed)) return true;
  const OnlineProbe probe = g_online_probe.load();
  return probe && probe();
}
bool online_allowed(uint32_t vanilla_file_start) {
  auto runtime = std::atomic_load(&g_runtime);
  auto found = runtime->by_start.find(vanilla_file_start);
  return found == runtime->by_start.end() || found->second.online_allowed;
}
uint32_t swapped_online_count(uint32_t* stages) {
  auto runtime = std::atomic_load(&g_runtime);
  std::map<std::string, bool> costumes, stage_ids;
  for (const auto& entry : runtime->by_start) {
    if (entry.second.online_allowed) continue;
    (entry.second.kind == "stage_visual" ? stage_ids : costumes)[entry.second.id] = true;
  }
  if (stages) *stages = (uint32_t)stage_ids.size();
  return (uint32_t)costumes.size();
}
void freeze_for_online_session() { g_online_freezes.store(1, std::memory_order_relaxed); }
void thaw_after_online_session() { g_online_freezes.store(0, std::memory_order_relaxed); }
SessionProfile session_profile() {
  auto runtime = std::atomic_load(&g_runtime);
  return {runtime->generation, runtime->fingerprint, runtime->assets,
          g_online_freezes.load(std::memory_order_relaxed) != 0};
}

std::string choose_import_file() {
  wchar_t file[32768]{};
  OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof dialog;
  dialog.hwndOwner = GetActiveWindow();
  dialog.lpstrFilter = L"Cosmetic imports (*.zip;*.dat;*.usd;*.png;*.ssm)\0*.zip;*.dat;*.usd;*.png;*.ssm\0Costume or stage file (*.dat;*.usd)\0*.dat;*.usd\0ZIP archive (*.zip)\0*.zip\0Portrait or stock icon (*.png)\0*.png\0Voice bank (*.ssm)\0*.ssm\0";
  dialog.lpstrFile = file; dialog.nMaxFile = (DWORD)std::size(file);
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  return GetOpenFileNameW(&dialog) ? wide_to_utf8(file) : std::string();
}

std::string choose_portrait_file() {
  wchar_t file[32768]{};
  OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof dialog;
  dialog.hwndOwner = GetActiveWindow();
  dialog.lpstrFilter = L"Portrait or stock icon (*.png)\0*.png\0";
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
bool costume_skeleton_matches(const std::vector<uint8_t>& clean, const std::vector<uint8_t>& candidate,
                              std::string* error) {
  std::string local; if (!error) error = &local;
  return host::cosmetics::costume_skeleton_matches(clean, candidate, error);
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
bool portrait_slot_from_name(const std::string& name, std::string* slot, std::string* kind) {
  return portrait_slot_from_name_impl(name, slot, kind);
}
bool inspect_bank(const std::vector<uint8_t>& bytes, std::string* bank, std::string* language, std::string* error) {
  std::string local; if (!error) error = &local;
  VoiceCheck check;
  if (!check_voice(bytes, {}, {}, &check, error)) return false;
  if (bank) *bank = check.bank->file;
  if (language) *language = kBankLanguages[check.language];
  return true;
}
bool inspect_zip(const std::string& path, std::vector<std::string>* names, std::string* error) {
  std::vector<ZipEntry> entries;
  if (!parse_zip(fs::u8path(path), &entries, error)) return false;
  names->clear(); for (const auto& entry : entries) names->push_back(entry.name);
  return true;
}
}  // namespace testing

}  // namespace host::cosmetics
