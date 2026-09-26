// Native cosmetic catalog, import, and file-aware disc overrides.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace host::cosmetics {

// The first importer intentionally accepts bounded, structurally validated resources. These caps
// are part of the file format contract, not merely UI advice.
constexpr uint64_t kMaxAssetBytes = 64ull * 1024 * 1024;
constexpr uint64_t kMaxArchiveBytes = 512ull * 1024 * 1024;
constexpr uint32_t kMaxArchiveEntries = 4096;

struct AssetInfo {
  std::string id;
  std::string name;
  std::string kind;              // character_costume, stage_visual, or effect_visual
  std::string target_path;       // logical GALE01 FST path, e.g. PlFxGr.dat
  std::string character;
  std::string costume;
  std::string scope;             // effect move key; empty for costumes and stages
  std::string preview_path;      // validated imported PNG, when available
  std::string sha256;
  std::vector<std::string> roots;
  std::vector<std::string> dependencies;
  std::vector<std::string> unsupported_companions;
  bool available = true;
  std::string availability_message;
  bool selected = false;
};

struct ImportResult {
  bool ok = false;
  bool already_present = false;
  std::string asset_id;
  std::string message;
};

struct SessionProfile {
  uint64_t generation = 0;
  std::string fingerprint;
  uint32_t active_assets = 0;
  bool frozen = false;
};

struct CompanionOverride {
  std::string kind;         // csp or stock
  std::string target_path;  // costume slot whose native selector texture is replaced
  std::string path;         // validated absolute PNG path for this immutable launch
};

// Stores the catalog beside port-settings.ini and loads the current desired profile. Safe to call
// more than once. The running profile is not changed by configure/import/toggle operations.
void configure(const std::string& settings_path);

// DAT files, bounded single-costume ZIPs, and saved Nucleus vault ZIPs are supported. Vault
// character variants commit as one transaction; supported project stages/effects remain subject to
// their fail-closed runtime validation policies.
ImportResult import_file(const std::string& path);

std::vector<AssetInfo> assets();
bool refresh_catalog(std::string* error = nullptr);
bool rename_asset(const std::string& asset_id, const std::string& display_name,
                  std::string* error = nullptr);
bool profile_enabled();
bool set_profile_enabled(bool enabled, std::string* error = nullptr);
// Selects the first exact-ISO-validated project effect for every effect target in one profile
// transaction. Character and stage selections are preserved.
bool enable_project_effects(std::string* error = nullptr);
bool select_variant(const std::string& target_path, const std::string& asset_id,
                    std::string* error = nullptr);
bool disable_target(const std::string& target_path, std::string* error = nullptr);
bool restore_vanilla(std::string* error = nullptr);
std::string last_message();

// True when the desired on-disk profile differs from the immutable profile captured for this
// process. The settings UI uses this to say exactly when a restart is required.
bool pending_restart();
bool runtime_initialized();

// Validated companion PNGs belonging to the immutable profile captured by apply_to_fst(). Missing
// or changed files are omitted so the renderer naturally falls back to the vanilla texture.
std::vector<CompanionOverride> active_companions();
// Called after the vanilla FST has been copied into guest RAM. It resolves selected logical paths
// against that exact ISO, patches their FST lengths, and publishes an immutable read snapshot.
void apply_to_fst(uint8_t* fst, uint32_t fst_size);

enum class OverrideRead { NotOverridden, Success, Failed };
// Handles a file-relative read. The final aligned DVD read may extend up to 31 bytes past the
// logical asset length; that padding is zeroed. Arbitrary out-of-range reads fail closed.
OverrideRead read(uint32_t vanilla_file_start, uint32_t file_offset, void* dst, uint32_t size);

// Integration hook for the practice/matchmaking branch. The runtime asset snapshot is already
// immutable for the entire process; these calls additionally expose queue/match freeze state to
// diagnostics without coupling this module to matchmaking internals.
void freeze_for_online_session();
void thaw_after_online_session();
SessionProfile session_profile();

// Native Windows picker used by the ImGui Mods tab. An empty string means the player cancelled.
std::string choose_import_file();

namespace testing {
struct DatInspection {
  bool ok = false;
  std::string error;
  std::string target_path;
  std::string character;
  std::string costume;
  std::vector<std::string> roots;
};
DatInspection inspect_dat(const std::vector<uint8_t>& bytes);
bool visual_dat_only(const std::vector<uint8_t>& clean, const std::vector<uint8_t>& candidate,
                     std::string* error);
bool materialize_effect_dat(const std::string& target_path,
                            const std::vector<uint8_t>& clean,
                            const std::vector<uint8_t>& candidate,
                            std::vector<uint8_t>* runtime,
                            std::string* classification,
                            std::string* error);
// Validates ZIP central-directory structure, paths, flags, methods, and resource sizes without
// extracting. Returned entries use '/' separators exactly as the archive records them.
bool inspect_zip(const std::string& path, std::vector<std::string>* entries, std::string* error);
}

}  // namespace host::cosmetics
