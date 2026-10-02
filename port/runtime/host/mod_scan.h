// Mods drop-in: the player puts mod files anywhere in the Mods folder; the game finds them,
// recognizes each one by its content (never by its file name), turns a pack on the first time it is
// found and remembers the player's on/off choice after that. No profile files are needed.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// What is recognized:
//   20XX TE        a Melee save (.gci) carrying 20XX Tournament Edition's code payload. The save's
//                  blocks are decoded and only TE's payload regions are compared (per name tag
//                  slot), so a copy Melee has saved records, settings or name tags into still matches.
//   Training Mode CE  a GTME01 disc with /TM/eventMenu.dat (its version is read from that file).
//   discs          by filesystem table and main.dol: vanilla 1.02, data-only mods, m-ex (MxDt.dat:
//                  Akaneia, ACE), the official 20XX Hack Pack builds, and other code mods.
//   card mods      any other Melee save: found, but not supported yet.
// Each item says which engine runs it and whether it is supported yet; the result is also written to
// Mods/.cache/detected.json for the launcher.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "mod_profile.h"

namespace source_port::mods {

enum class Kind { Te, TmCe, Vanilla, AssetMod, Mex, HackPack, CodeMod, CardMod, Unsupported };
enum class Engine { Source, Recomp, Either };
enum class Support { Supported, Untested, NotSupportedYet };
const char* kind_id(Kind);         // detected.json "kind": te, tmce, vanilla, asset_mod, mex, hack_pack, code_mod, card_mod, unsupported
const char* engine_id(Engine);     // "source", "recomp", "either"
const char* support_id(Support);   // "supported", "untested", "not_supported_yet"

struct Detected {
  std::filesystem::path path;      // relative to the game folder when inside it
  Kind kind = Kind::Unsupported;
  std::string name, version;       // "20XX TE" / "v2d r4"
  std::string hash;                // SHA-256 of the whole file
  std::string key;                 // the on/off choice it follows: "te", "tmce" or its content key
  std::string catalog_id;          // official ID only for a content-verified build; otherwise its key
  std::string message;             // one plain line for the player
  Engine needs = Engine::Either;
  Support status = Support::NotSupportedYet;
  bool can_enable = false;         // a pack that can be switched on (not unsupported, not a second copy)
  bool enabled = false;            // switched on (the player's choice, or turned on when first found)
  bool card = false;               // a memory card save: one at a time, a card holds one Melee save
  bool restart = false;            // switching it takes effect after a restart
};
// Card saves: 20XX TE and memory card mods share the card's single Melee save.
inline bool is_card_mod(const Detected& d) { return d.kind == Kind::Te || d.kind == Kind::CardMod; }
// Whether this engine runs it (the Mods page greys out the rest).
bool runs_on(const Detected& d, bool source_port);
// Official whole-disc MD5 fingerprints. Empty means no recognized Hack Pack build.
std::string hack_pack_version(const std::string& md5);
// One file of a disc image, by its path on the disc ("/ai_engine.bin"; case does not matter), read in
// full. False when the image or the file cannot be read.
bool read_disc_file(const std::filesystem::path& image, const char* path, std::vector<uint8_t>* out);

// ---- Melee saves (GCI) ----
// A Melee NTSC save file: 11 blocks, the first holding the comment, banner and icons; each of the
// other ten holds one copy of a logical block (1 = game data, 2-8 = the seven name tag banks),
// encrypted and checksummed, with a sequence number (the newest copy is the one in use).
struct MeleeSave {
  uint8_t header[64]{};
  std::array<std::vector<uint8_t>, 9> section;   // [1] game data 0x1790 bytes, [2..8] name tag banks 0x1F2C
  std::array<int, 9> seq{};                        // -1: no valid copy
};
bool read_melee_save(const std::vector<uint8_t>& gci, MeleeSave* out, std::string* why = nullptr);
// The game's own block cipher (HSD_Encrypt / HSD_Decrypt): a 16-byte checksum, then every byte
// encrypted against the previous one. Decrypt returns false when the checksum does not match.
void encrypt_block(uint8_t* block, size_t size);
bool decrypt_block(uint8_t* block, size_t size);

// 20XX TE, by its code payload: the 23 regions its source writes into the name tag banks (39,962
// bytes, see te_payload in mod_scan.cpp), compared per name tag slot. Melee's own saving rewrites
// the game data block and moves blocks around; a name tag the player creates or plays with rewrites
// that one slot. So a copy that played on a console or Dolphin keeps nearly all of the payload.
struct TeCheck {
  bool melee_save = false;         // a readable Melee NTSC save
  bool te = false;                 // 20XX TE's texts are in it (any version)
  std::string version;             // TE's own version text as shown on the title screen, "v2d r4"
  uint32_t payload_bytes = 0, matching_bytes = 0;   // v2d r4's payload, and how much of it is intact
  bool supported() const { return payload_bytes && matching_bytes * 10ull >= payload_bytes * 9ull; }
};
TeCheck check_te(const std::vector<uint8_t>& gci);
TeCheck check_te_file(const std::filesystem::path& gci);

// ---- scanning ----
struct ScanOptions {
  std::filesystem::path mods_dir = "Mods";
  std::filesystem::path base_iso;   // the player's retail disc, for comparing a modded disc's files; optional
  bool source_port = false;         // which engine is running (only for the messages)
  bool use_cache = true;            // Mods/.cache/scan.txt, keyed by path, size and time
};
struct ScanResult {
  std::vector<Detected> items;
  std::vector<std::string> notes;   // plain lines for the Mods page
  std::vector<std::string> log;     // for the log file
  uint32_t hashed = 0, cached = 0;  // files read in full this time, and files taken from the cache
};
// One file, by its content. A disc image or card save the game cannot use comes back as
// Kind::Unsupported with a message saying why; any other file comes back with an empty message
// (not listed).
Detected identify(const std::filesystem::path& file, const ScanOptions& options, std::string* log = nullptr);
// The whole Mods folder, recursively (Mods/.cache and the loose-file overlay Mods/Files are skipped),
// with the player's choices applied. A pack seen for the first time is turned on and the choice is
// recorded in *choices; a second card save found while one is on stays off.
ScanResult scan(const ScanOptions& options, std::map<std::string, int>* choices);
bool write_detected_json(const std::filesystem::path& file, const ScanResult& result, bool source_port);

// ---- the player's choices ----
// Key -> 1 on, 0 off; absent = not decided (turned on when first found). Saved with the settings as
// mod_te_enabled, mod_tmce_enabled and "mod_enabled <key> <0|1>".
void set_choices(const std::map<std::string, int>& choices);
std::map<std::string, int> choices();
uint32_t choices_version();        // changes whenever a choice changes (the settings panel saves then)
// Switches one pack. Turning a card save on turns the other card saves off; *notes says so.
void choose(const std::string& key, bool on, std::vector<std::string>* notes = nullptr);
bool parse_choice(const std::string& key, const std::string& value, std::map<std::string, int>* choices);
std::string choice_lines(const std::map<std::string, int>& choices);   // "\nmod_te_enabled 1..." for the settings file
// Normal launches follow the Mods folder; automated runs (no settings file read) never do.
void set_auto_detect(bool on);
bool auto_detect();

// ---- the memory card folder ----
// A save with the same internal name as the player's own hides one of them (the game uses whichever
// file the folder lists first). 20XX TE's save has Melee's own name. At boot every such duplicate is
// moved into Mods/Saves, keeping the player's own save: a file with 20XX TE in it is never the
// player's own; then the file the game itself writes (<game>-<name>.gci); then the file that was in
// the folder first (earliest creation time, then the older save time). A lone 20XX TE save in the
// folder is moved too, so the normal game gets a clean card. Nothing is deleted.
struct CardMove { std::filesystem::path from, to; bool te = false; };
std::vector<CardMove> protect_card_folder(const std::filesystem::path& card_dir, const std::filesystem::path& saves_dir,
                                          std::vector<std::string>* notes, std::vector<std::string>* log);

// ---- sessions ----
// Source Port: the enabled packs it runs natively, in load order (Training Mode CE, then data-only
// discs). 20XX TE is not a layer there: te_enabled() turns on its native features. Card mods and
// code mods are not run by the Source Port.
std::vector<Layer> source_port_layers(const ScanResult& result);
bool te_enabled(const ScanResult& result);
// Static Recomp: the enabled card save (20XX TE or a card mod) goes on its own card folder,
// <ordinary card>/../Profiles/<name>, seeded from the ordinary card minus that save's identity; the
// ordinary card is never written. Returns that folder, or empty for the ordinary card.
std::filesystem::path static_recomp_card(const ScanResult& result, const std::filesystem::path& ordinary_card,
                                         std::vector<std::string>* log);

struct StartupOptions {
  std::filesystem::path mods_dir = "Mods";
  std::filesystem::path card_dir = "User/GC/CardA";   // the ordinary card
  std::filesystem::path base_iso;
  bool source_port = false;
  bool use_profile_card = true;  // explicit session card directories take precedence
};
struct Startup {
  std::vector<Layer> layers;         // Source Port
  bool te = false;                   // Source Port: 20XX TE's native features
  std::filesystem::path card_dir;    // Static Recomp: the session's card folder, empty = the ordinary one
  std::vector<std::string> log;
};
// At boot, on either engine (normal launches only): the card folder check, the scan, the player's
// choices, Mods/.cache/detected.json. Keeps the result for the settings panel.
Startup startup(const StartupOptions& options);

// ---- the settings panel ----
struct PanelView {
  ScanResult result;
  bool scanned = false, scanning = false, importing = false;
  std::string import_message;
  std::vector<std::string> choice_notes;   // from the last switch
};
PanelView panel_view();
void rescan_async();                                   // the Mods page opened, or "Scan again"
void import_async(const std::filesystem::path& file);  // "Add a mod file...": copy into Mods, then identify
bool open_mods_folder();
bool open_official_page(Kind kind);
std::filesystem::path mods_folder();

}  // namespace source_port::mods
