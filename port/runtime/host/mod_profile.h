// Mod profiles for the Source Port: named, ordered import sets with a content identity.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// A profile is a text file, Mods/Profiles/<name>.ini, one layer per line in load order:
//
//     iso = C:\Games\SomeMod.iso      patched NTSC 1.02 disc, compared with the retail disc
//     dir = Mods\Packs\Recolors       loose disc files, same layout as the disc
//     gci = C:\Games\SomeMod.gci      memory card file (settings saves, codes)
//
// Later layers win over earlier ones when both change the same disc file; the pair is listed.
// Each profile gets its own memory card folder so an imported save is never hidden behind the
// player's ordinary Melee save, and the ordinary card is never written by a modded session.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace source_port::mods {

enum class LayerKind { Iso, Dir, Gci };
struct Layer { LayerKind kind; std::filesystem::path path; };
struct Profile {
  std::string name;               // empty for layers given on the command line
  std::vector<Layer> layers;
};

// Reads a profile file. Relative paths resolve against the profile file's folder first, then the
// working directory. Unknown keys are errors, so a typo never silently drops a layer.
bool parse_profile(const std::filesystem::path& file, Profile* out, std::string* error);
// "name" -> Mods/Profiles/name.ini; a path ending in .ini is used as given.
std::filesystem::path profile_path(const std::string& name_or_file);
// Profile names become folder names: letters, digits, space, '-', '_' and '.' only.
std::string safe_name(const std::string& name);

// SHA-256, lower-case hex. Incremental for streamed content.
class Sha256 {
 public:
  Sha256();
  ~Sha256();
  Sha256(const Sha256&) = delete;
  Sha256& operator=(const Sha256&) = delete;
  void update(const void* data, size_t size);
  void update(const std::string& text) { update(text.data(), text.size()); }
  std::string hex();   // finishes the hash
 private:
  void* algorithm_ = nullptr;
  void* hash_ = nullptr;
  std::vector<uint8_t> object_;
};
std::string sha256_file(const std::filesystem::path& file);   // empty on error

// The identity of a memory card file: game code (6 bytes) and file name (32 bytes) from its header.
bool gci_identity(const std::filesystem::path& file, std::string* identity, std::string* error);

// The profile's own card folder. On first use it is seeded with every .gci from the ordinary card
// except the ones an imported file replaces (same identity), so unlocks and records carry over.
// When an imported file's content changes, the profile's saved copy of it is set aside as
// <file>.bak so the new import takes effect. Returns false only when the folder cannot be made.
struct CardImport { std::filesystem::path source; std::string identity, sha256; };
bool prepare_profile_card(const std::filesystem::path& ordinary_card,
                          const std::filesystem::path& profile_card,
                          const std::vector<CardImport>& imports,
                          std::vector<std::string>* notes, std::string* error);

// What the running game loaded, for the settings panel. The Source Port fills it at boot; the
// static recomp leaves source_port false and the panel shows no profile section.
struct Status {
  bool source_port = false;
  std::string profile;               // active profile name; empty for command-line layers or none
  std::string requested;             // the saved setting or flag that chose it (name or .ini path)
  std::string identity;              // content fingerprint of the enabled layers; empty = retail game
  std::vector<std::string> layers;   // one line per layer, in load order
  std::vector<std::string> notes;    // code patches not applied, file conflicts, card folder
  // Packs recognized by content among the loaded layers: their native features are offered only
  // while the player's own file is loaded.
  bool te_owned = false;             // the 20XX Tournament Edition save
  bool akaneia = false;              // the Akaneia 1.0.1 disc
  bool tmce = false;                 // Training Mode CE's disc files (TM/), on the vanilla game
  // The 20XX Hack Pack's disc files (StageSwapTable.bin and its stage, music and costume files) are
  // loaded as an overlay. None of its code runs: the retail menu files stay, and its features are
  // native rewrites (run-source/rel09-hackpack/PLAN.md section 3), offline only.
  bool hackpack = false;
  // The session's mods came from the Mods folder (mod_scan.h), not a profile or --mod-* flags; then
  // switching 20XX TE on the Mods page takes effect at once.
  bool detected = false;
  // Costume files of the loaded packs and what online modes that play the standard game do with
  // each: `served` ones stay on there, the rest show the standard costume. Empty when no pack
  // changes a costume, and then the panel draws exactly what it drew before.
  struct SkinNote {
    std::string path, layer, reason;   // "PlFxGr.dat", the layer's file name, "61 joints match"
    bool served = false, identical = false;
  };
  std::vector<SkinNote> pack_skins;
  // The game is showing the standard files right now (an online mode that plays the standard game).
  bool retail_view = false;
};
Status& status();
// Profile names found in Mods/Profiles (*.ini), sorted.
std::vector<std::string> list_profiles();

// Windows open dialog for discs and memory card files; empty when cancelled. "Add a mod file..."
// copies the pick into the Mods folder (mod_scan.h import_async).
std::string choose_mod_file();

}  // namespace source_port::mods
