// Verified mod metadata, routing and managed files. No network or process execution.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace launcher::mod_catalog {
struct CatalogMod {
  std::string id, name, credits, license, page, repo, tag, asset_pattern;
  std::string kind;
  bool one_click = false;
  std::string sha256, output_md5;
  std::string version, url, tool_sha256, patch_sha256, policy;
  // Whole-file SHA-256 of the finished disc (checked after patching; also names a detected disc
  // whose header cannot tell it apart from another build) and a one-line engine note for the card.
  std::string output_sha256, note;
};
struct Installed {
  std::string id, key, kind, name, version, message, path, needs_engine, status, hash;
  bool enabled = false;
};
bool valid_sha256(const std::string& value);
bool valid_id(const std::string& value);
bool download_digest(const std::string& github_digest, const std::string& pinned,
                     std::string* digest, std::string* error);
bool parse(const std::string& text, std::vector<CatalogMod>* mods, std::string* error);
std::vector<Installed> detected(const std::string& text);
bool playable(const Installed& mod);
std::string play_engine(const Installed& mod, const std::string& selected);
std::string sha256_file(const std::filesystem::path& file);
const CatalogMod* matching_archive(const std::vector<CatalogMod>& mods, const std::string& sha256);
std::filesystem::path pinned_xdelta(const std::filesystem::path& folder, const std::string& sha256);
// A tool shipped next to the launcher (tools\<name>), only when its SHA-256 matches the pin.
std::filesystem::path shipped_tool(const std::filesystem::path& launcher_dir, const char* name,
                                   const std::string& sha256);
bool copy_pack(const std::filesystem::path& from, const std::filesystem::path& to, bool replace,
               std::string* error, const std::atomic<bool>* cancel = nullptr);
// Deletes only managed input packs in Mods/Discs or Mods/Saves. User cards/profile saves stay.
bool remove_pack(const std::filesystem::path& game_dir, const std::filesystem::path& pack,
                 std::string* error);
bool enable_pack(const std::filesystem::path& settings, const std::string& key, bool on,
                 std::string* error);
bool link_pack(const std::filesystem::path& game_dir, const std::filesystem::path& pack,
               std::string* error);
// Executes only a verified tool and patch, with output staged away from an installed disc.
bool apply_delta(const std::filesystem::path& tool, const std::string& tool_hash,
                 const std::filesystem::path& patch, const std::string& patch_hash,
                 const std::filesystem::path& source, const std::filesystem::path& output,
                 std::string* error, const std::atomic<bool>* cancel = nullptr);
}  // namespace launcher::mod_catalog
