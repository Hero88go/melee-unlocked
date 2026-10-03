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
  // What happens to this choice in online play, for the launch that is running. Filled only for a
  // choice that was applied at startup; online_message is empty otherwise. The message is short
  // enough for one status line: "61 joints match", "rest pose differs", "textures only".
  bool online_allowed = true;
  std::string online_message;
  // "import": a file the player added, copied into the catalog; source_name is that file's name.
  // "disc": a costume inside a mod disc or files pack, read from there (disc_path) with no copy;
  // source_name is the pack's name.
  std::string source = "import";
  std::string source_name;
  std::string disc_path;
  // Which pack this skin belongs to (packs().key) and which of its sets: "" for the costume itself,
  // "alt L" or "alt R" for a pack's alternate costumes of the same slot.
  std::string pack;
  std::string variant;
  // The fighter sound bank this skin brings with it ("fox.ssm"), or empty; voice_source is the file
  // it was imported from. See docs/voice-mods.md.
  std::string voice;
  std::string voice_source;
};

// The skins grouped by where they came from: imports as one pack, each scanned disc or folder as
// another. `variants` is sorted, "" first. online_on counts the skins known to stay on online;
// online_unchecked the imports not applied yet (their verdict comes when the game starts).
struct PackInfo {
  std::string key, name;
  bool disc = false;
  std::vector<std::string> variants;
  uint32_t slots = 0, skins = 0, online_on = 0, online_unchecked = 0, selected = 0;
};
std::vector<PackInfo> packs();
// Sets every costume slot the pack covers to its skin of that set ("" or "alt L" and so on) in one
// profile change, or with "none" puts the pack's own picks back to the standard costume. `replaced`
// counts the slots that held a skin from another pack, `replaced_from` names it. With preview true
// nothing changes: the counts are for a "replaces N skins from X" line before the player confirms.
struct PackSetResult {
  bool ok = false;
  uint32_t set = 0, cleared = 0, replaced = 0;
  std::string replaced_from, message;
};
PackSetResult apply_pack_set(const std::string& pack_key, const std::string& variant, bool preview = false);

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

// A portrait (the character select picture) or a stock icon for one costume slot, imported on its
// own with no costume file. It is a catalog entry of its own (kind character_portrait, target
// "<slot>#portrait"), switched on and off like any other choice, and it shows wherever the selected
// skin has no picture of its own. `slot` is the costume's disc file name ("PlFxGr.dat"); `kind` is
// "csp" for the portrait or "stock" for the stock icon.
ImportResult import_portrait(const std::string& png_path, const std::string& slot, const std::string& kind);
// ---- a skin's own portrait and stock icon, given to it after the import ----
// The rule the pictures follow: a selected skin that has its own picture shows it; the costume's
// added picture (the entry above) fills in only where the skin has none. These give one installed
// skin its own picture of that kind ("csp" or "stock"), replacing the one it had. A skin from a mod
// disc or a files pack works too: the picture is kept in the catalog, like the ones a scan stores,
// and stays with the skin when its disc is scanned again.
ImportResult set_skin_portrait(const std::string& skin_id, const std::string& png_path, const std::string& kind);
// The same with the picture of another catalog entry: another skin's own, or a costume's added one.
// The bytes are copied, so removing the source later changes nothing.
bool set_skin_portrait_from(const std::string& skin_id, const std::string& source_asset_id,
                            const std::string& kind, std::string* error = nullptr);
// Takes the skin's own picture away: the costume's added picture, or the game's own, shows then.
bool clear_skin_portrait(const std::string& skin_id, const std::string& kind, std::string* error = nullptr);
// Every other catalog entry of the same fighter that has a picture of that kind, for a picker.
struct PortraitChoice { std::string asset_id, label; };
std::vector<PortraitChoice> portrait_choices(const std::string& skin_id, const std::string& kind);
// Which picture of that kind shows while the skin is the selected one.
enum class PortraitSource { Standard, Own, Costume };
PortraitSource skin_portrait_source(const std::string& skin_id, const std::string& kind);
// ---- voice mods: a skin's own fighter sound bank (docs/voice-mods.md) ----
// Attaches a replacement sound bank (.ssm) to an installed skin, or replaces the one it has. The
// bank must be the skin's fighter's, with the same sounds as the game's bank (see the .cpp). The
// same checks run for a bank inside a mod zip (import_file), which calls this for its costume.
ImportResult import_voice(const std::string& ssm_path, const std::string& asset_id);
// Takes the bank away from a skin again.
bool remove_voice(const std::string& asset_id, std::string* error = nullptr);

// One port of the match that is about to load: the game's own fighter number (0 Captain Falcon to
// 25 Ganondorf, the numbers costume_slot_file takes) and costume index, or character -1 for an
// empty port.
struct MatchFighter { int character = -1; int costume = 0; };
// A fighter bank whose served content changed: its file name ("fox.ssm") and the game's bank number
// (11). The engine lets the copy the game holds in audio memory go, so the loader reads it again.
struct VoiceChange { std::string file; int bank = -1; };
struct VoicePlan {
  std::vector<VoiceChange> changed;
  std::vector<std::string> notes;   // one line per bank decided and per conflict; also logged
};
// Decides every fighter bank for the match about to load and swaps the served banks as a whole.
// Per bank: the lowest port that plays the bank's fighter decides. Its skin (the saved selection of
// its costume slot) brings its bank, or the game's own sounds when it has none; a higher port that
// would have chosen otherwise is named in `notes`. A bank no port uses keeps what it serves. Changes
// no selection and no file table entry; safe while the profile is frozen for an online session.
VoicePlan plan_match_voices(const MatchFighter ports[4]);
// How many disc bank files have a usable voice mod this launch. 0: nothing to plan at match load.
uint32_t voice_bank_count();

// Lists the costumes of a mod disc (.iso) or a files pack (a folder) as skins, without copying them.
// Each costume file that differs from the open game disc's becomes a choice named
// "<fighter> <color>: from <pack_name>" in that costume's list; nothing is selected. Needs the game
// disc open (host::disc_open) to compare against. Scanning the same unchanged disc again does
// nothing; a changed disc updates its entries and drops the ones that no longer differ.
// A pack that carries its own character select file (MnSlChr.usd or .dat) also gives each of its
// plain costumes the portrait that file shows for the slot, when it differs from the game's: it is
// stored as the skin's "csp" picture, like the portrait an imported skin brings. A pack's
// alternate sets have no cell of their own in that file and get none.
ImportResult scan_disc_skins(const std::string& iso_path, const std::string& pack_name,
                             std::string* error = nullptr);
// How many skins the catalog lists from that disc or folder.
uint32_t disc_skin_count(const std::string& iso_path);
// The mod disc (or files pack) this session loads as its own files, as scan_disc_skins was given
// it: the 20XX Hack Pack as the Source Port's overlay. Its plain costumes are what the session
// already serves as the standard ones, so L / R leaves them out (cycle_slot_live), and its other
// sets ("alt L", "alt R") step after the standard costume like any skin. Empty: no such disc
// (the default, and always on the Static Recomp, where a pack disc runs its own L / R).
void set_session_pack(const std::string& iso_path);
// True for a costume skin scanned from that disc. The Source Port lets these serve a costume file
// the pack itself replaced; a skin from anywhere else leaves the pack's own file in place.
bool session_pack_skin(const std::string& asset_id);
// Every costume slot of the retail game, in the game's own order, for a slot picker.
struct CostumeSlot { std::string target_path, character, costume; };
std::vector<CostumeSlot> costume_slots();

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

// ---- character select L / R skin cycling (both engines) ----
// A skin picked on the character select screen is saved like a Mods tab choice and applied at once,
// with no restart: the slot's disc file is published again before the match opens it.

// The costume slot ("PlFxGr.dat") of a character select fighter number (0 Captain Falcon to 25
// Ganondorf) and a costume index in the game's order; empty when the game has no such costume. The
// Ice Climbers give Popo's slot.
std::string costume_slot_file(int css_character, int costume);
// Like select_variant for a costume slot, without the restart: an empty asset_id is the standard
// costume. Refused while an online match is queued or running. Inside the online flow only a skin
// proven to change looks alone (verdict online_allowed) can be picked; the standard costume always can.
bool select_variant_live(const std::string& slot, const std::string& asset_id, std::string* error = nullptr);
// One step through the slot's choices: the standard costume, then each installed skin in catalog
// order. direction > 0 is the next one. Choices select_variant_live refuses are stepped over.
// `name` is short enough for a label ("Standard", "My skin", "Pack name (alt L)").
// The Ice Climbers are two costume files, and the character select names Popo's. When Popo's slot
// changes, Nana's follows where the same pack (and the same set of it) has a skin for her, and goes
// back to the standard costume when she wore the partner of Popo's previous skin and the new one
// has none. A pick of her own (another pack, an import) is left alone. partner_slot is her slot
// when it changed with this step (the caller publishes it again too), partner_previous_id what it
// held before ("" the standard costume).
struct LiveCycle {
  bool ok = false, changed = false;
  std::string asset_id, previous_id, name, message;
  std::string partner_slot, partner_previous_id;
};
LiveCycle cycle_slot_live(const std::string& slot, int direction);
// Publishes one costume slot again from the saved profile: the same work apply_to_fst does for it,
// on the table apply_to_fst patched at startup. The slot's entries get their new lengths and the
// read snapshot is swapped as a whole, so a reader sees the old file or the new one, never a mix.
// `files` lists the slot's disc files (the .dat and its English twin) as they are served now.
struct RepublishedFile {
  uint32_t fst_index = 0, vanilla_start = 0, length = 0;
  bool overridden = false, online_allowed = true;
  std::string asset_id;
};
struct RepublishResult {
  bool ok = false;
  std::string message;
  std::vector<RepublishedFile> files;
};
RepublishResult republish_slot(uint8_t* fst, uint32_t fst_size, const std::string& slot);
// The skin serving this disc file now, or empty for the disc's own file.
std::string applied_asset(uint32_t vanilla_file_start);

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
// The online rule also applies whenever this probe says the game is in its online flow (the online
// menus and character select come before any search, and the character select already loads the
// fighter files). Each engine installs its own: the game's current mode is Slippi's online mode.
using OnlineProbe = bool (*)();
void set_online_probe(OnlineProbe probe);
// Frozen for a search or match, or inside the online flow.
bool online_active();
// False when this disc file has an override that must not be used online (not proven visual-only).
bool online_allowed(uint32_t vanilla_file_start);
// How many applied costumes show the standard costume online (each costume counts once, also when
// two disc files serve it). *stages gets the same count for stage choices.
uint32_t swapped_online_count(uint32_t* stages = nullptr);

// Online rule for costumes: the _Share_joint skeleton equals the standard costume's (same joints,
// same hierarchy, same rest pose; see the .cpp). On success *detail says how many joints matched;
// on failure it says what differs.
bool costume_skeleton_matches(const std::vector<uint8_t>& clean, const std::vector<uint8_t>& candidate,
                              std::string* detail);
// The same verdict in a few words for a status line: "rest pose differs", "61 joints match".
std::string online_reason_short(const std::string& detail);

// Native Windows picker used by the ImGui Mods tab. An empty string means the player cancelled.
std::string choose_import_file();
// The same for one PNG (a portrait or stock icon).
std::string choose_portrait_file();

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
// The public costume_skeleton_matches, kept under its old name for the tests.
bool costume_skeleton_matches(const std::vector<uint8_t>& clean, const std::vector<uint8_t>& candidate,
                              std::string* error);
bool materialize_effect_dat(const std::string& target_path,
                            const std::vector<uint8_t>& clean,
                            const std::vector<uint8_t>& candidate,
                            std::vector<uint8_t>* runtime,
                            std::string* classification,
                            std::string* error);
// The costume slot and kind ("csp" or "stock") a picture's file name identifies: the costume's
// file code ("PlFxGr stock.png") or a fighter and a color ("Fox Green.png"). False when the name
// does not identify exactly one costume.
bool portrait_slot_from_name(const std::string& name, std::string* slot, std::string* kind);
// Validates ZIP central-directory structure, paths, flags, methods, and resource sizes without
// extracting. Returned entries use '/' separators exactly as the archive records them.
bool inspect_zip(const std::string& path, std::vector<std::string>* entries, std::string* error);
// The fighter bank a sound bank file is ("fox.ssm") and which of the game's two it fits ("English"
// or "Japanese"), from its own header. False, with the reason, for anything the importer refuses.
bool inspect_bank(const std::vector<uint8_t>& bytes, std::string* bank, std::string* language, std::string* error);
}

}  // namespace host::cosmetics
