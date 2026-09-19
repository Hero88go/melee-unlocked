// Dolphin-compatible custom texture packs: Load/Textures/GALE01, "tex1_" filenames, XXH64 hashes.
// Presentation only. Nothing here is visible to the simulation, so two players may run different
// packs (or none) and still stay in sync.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gx {
struct TextureRef;
struct TextureSnapshot;

namespace texpack {

// One decoded replacement, ready to upload: RGBA8, level 0 first, mips tightly packed after it.
struct Replacement {
  uint32_t width = 0, height = 0, levels = 1;
  std::vector<uint8_t> pixels;
  std::vector<uint32_t> level_offset, level_width, level_height;
  uint64_t bytes() const { return pixels.size(); }
};

// Turns pack loading on or off. With `enabled` false this returns immediately and touches no files.
// Returns true when the state changed, meaning the caller must drop textures it already uploaded so
// they are rebuilt with (or without) their replacements.
bool configure(bool enabled, bool dump);

// Background loading, so a replacement is never decoded on the render thread in the middle of a
// frame (a 100 ms freeze at match start when the stage's textures were not prefetched yet).
// has(): the pack has an enabled replacement under this name. ready(): it is decoded and waiting, so
// load() returns at once. request(): decode it on the loader thread; ready() turns true when done.
bool has(const std::string& base);
bool ready(const std::string& base);
void request(const std::string& base);
bool enabled();
bool dumping();

// Dolphin's base name for a texture, from HiresTextures.cpp GenBaseName:
//   tex1_<w>x<h>[_m]_<tex_hash>[_<tlut_hash>]_<format>
std::string base_name(const TextureRef& t, const TextureSnapshot& s);

// Decodes the replacement for `base`, or returns nullptr when the pack has none, the file is
// unreadable, or it would cost more than `budget_bytes` of texture memory.
std::unique_ptr<Replacement> load(const std::string& base, uint64_t budget_bytes);

// One installed pack, for the settings list. Each folder dropped into Load/Textures/GALE01 or
// TexturePacks is a pack; files loose in either are grouped as one.
struct PackInfo {
  std::string name;
  bool enabled = true;
  uint64_t files = 0;   // usable replacements this pack contributes
};
// Scans for installed packs without turning replacement on, so the list can be shown to someone
// who has not enabled anything yet. Cheap: filenames only, no PNG is decoded.
void refresh_packs();
// Creates TexturePacks if it is missing and shows it in Explorer, so "+ Add" means "here is where
// they go" rather than a file dialog that copies gigabytes of PNGs to a second place on disk.
void open_packs_folder();
std::vector<PackInfo> packs();
// Switching a pack changes what the next texture lookup returns; it needs no rescan. Returns true
// when the state changed, so the caller can drop textures it has already uploaded.
bool set_pack_enabled(const std::string& name, bool enabled);
// The packs the player switched off, saved and restored with the rest of the settings. Kept as the
// disabled set so a pack installed later is on by default.
std::vector<std::string> disabled_packs();
void set_disabled_packs(std::vector<std::string> names);

// Prefetch: decode every replacement up front instead of on the first draw that needs it, which is
// what Dolphin's "Prefetch Custom Textures" does. Without it a big pack pays for each texture the
// first time it appears, which is a stutter spread through the first minutes of play; with it the
// wait happens once, before the game starts, and the window title says so while it runs.
void prefetch_begin();
bool prefetching();
// Decoded so far and the total to decode, for the progress the title bar shows.
void prefetch_progress(uint64_t* done, uint64_t* total);

// Counters behind the "installed a pack and nothing changed" report.
void note_lookup(bool matched);
void report();

// Writes one texture the game used to Dump/Textures/GALE01/<base>[_mipN].png so pack authors get
// the exact filenames they need. `level_rgba` is the decoded level, `width`/`height` its size.
void dump_level(const std::string& base, uint32_t level, const uint8_t* level_rgba,
                uint32_t width, uint32_t height);

}  // namespace texpack
}  // namespace gx
