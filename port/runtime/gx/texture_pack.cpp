// Dolphin-compatible custom texture packs.
//
// Every naming and hashing decision here is copied from the Ishiiruka/Slippi Dolphin tree kept in
// slippi/ for reference, so packs written for Dolphin drop in unchanged:
//   slippi/Source/Core/VideoCommon/HiresTextures.cpp:323 GenBaseName (the filename)
//   slippi/Source/Core/VideoCommon/HiresTextures.cpp:388 XXH64 over the raw GX bytes (the hash)
//   slippi/Source/Core/VideoCommon/HiresTextures.cpp:172  recursive scan of Load/Textures/<GameID>
//   slippi/Source/Core/VideoCommon/HiresTextures.cpp:222  the _mipN suffix
//   slippi/Source/Core/VideoCommon/TextureCacheBase.cpp:706 what Dolphin feeds GenBaseName
//   slippi/Externals/xxhash/xxhash.cpp:410 XXH64 with samples 0 is plain XXH64, seed 0
//
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>

#include "texture_pack.h"

#include <algorithm>
#include <thread>
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <system_error>
#include <unordered_map>

#include "gx_core.h"
#include "gx_texture.h"
#include "host.h"
#include "texture_snapshot.h"

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

namespace gx::texpack {
namespace {

// ---------------- XXH64 ----------------
// Dolphin hashes custom textures with XXH64, seed 0 (HiresTextures.cpp:388 calls the Ishiiruka
// XXH64(input, len) whose extra "samples" argument defaults to 0; with samples 0 the loop in
// slippi/Externals/xxhash/xxhash.cpp:410 keeps blockdist 8 and bEnd at input+len, which is the
// stock algorithm). Reproduced here rather than pulled in as a dependency: it is 40 lines and the
// bit pattern is the whole point of the feature.
constexpr uint64_t P1 = 0x9E3779B185EBCA87ull, P2 = 0xC2B2AE3D27D4EB4Full,
                   P3 = 0x165667B19E3779F9ull, P4 = 0x85EBCA77C2B2AE63ull, P5 = 0x27D4EB2F165667C5ull;
inline uint64_t rotl64(uint64_t v, int r) { return (v << r) | (v >> (64 - r)); }
inline uint64_t read64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }   // x86: little endian
inline uint32_t read32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline uint64_t round64(uint64_t acc, uint64_t input) { return rotl64(acc + input * P2, 31) * P1; }
inline uint64_t merge64(uint64_t h, uint64_t v) { return (h ^ (rotl64(v * P2, 31) * P1)) * P1 + P4; }

uint64_t xxh64(const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  const uint8_t* const end = p + len;
  uint64_t h;
  if (len >= 32) {
    const uint8_t* const limit = end - 32;
    uint64_t v1 = P1 + P2, v2 = P2, v3 = 0, v4 = 0 - P1;
    do {
      v1 = round64(v1, read64(p)); p += 8;
      v2 = round64(v2, read64(p)); p += 8;
      v3 = round64(v3, read64(p)); p += 8;
      v4 = round64(v4, read64(p)); p += 8;
    } while (p <= limit);
    h = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
    h = merge64(h, v1); h = merge64(h, v2); h = merge64(h, v3); h = merge64(h, v4);
  } else {
    h = P5;
  }
  h += (uint64_t)len;
  while (p + 8 <= end) { h ^= rotl64(read64(p) * P2, 31) * P1; h = rotl64(h, 27) * P1 + P4; p += 8; }
  if (p + 4 <= end) { h ^= (uint64_t)read32(p) * P1; h = rotl64(h, 23) * P2 + P3; p += 4; }
  while (p < end) { h ^= (uint64_t)(*p) * P5; h = rotl64(h, 11) * P1; ++p; }
  h ^= h >> 33; h *= P2; h ^= h >> 29; h *= P3; h ^= h >> 32;
  return h;
}

// ---------------- pack index ----------------
// One entry per base name. Paths are kept as strings and the pixels are decoded on demand: a Melee
// HD pack can be several hundred megabytes on disk while a match only ever draws a few hundred
// textures, so prefetching the way Dolphin does (HiresTextures.cpp:279) would spend far more memory
// than the machines this port targets have.
struct Entry {
  std::vector<std::string> levels;   // levels[n] is the file for mip n, empty when absent
  int pack = 0;                      // index into State::packs, so one pack can be switched off
};

// Packs the player switched off, by name. Remembered rather than the enabled set so a pack dropped
// in later is on by default: installing one and seeing nothing change would be the wrong surprise.
std::vector<std::string> g_disabled;
constexpr const char* kLoosePackName = "(loose files)";
bool pack_enabled_default(const std::string& name) {
  for (const auto& off : g_disabled) if (off == name) return false;
  return true;
}

// One installed pack: a folder the player dropped in. Anything loose in a root counts as one
// unnamed pack, so a plain Dolphin pack still works without being moved into a subfolder.
struct Pack {
  std::string name;
  bool enabled = true;
  uint64_t files = 0;
};

struct State {
  bool on = false, dump = false;
  std::filesystem::path root, dump_root;
  std::unordered_map<std::string, Entry> index;
  std::vector<Pack> packs;
  std::vector<std::filesystem::path> roots;   // Load/Textures/GALE01 and TexturePacks
  int indexing_pack = 0;
  uint64_t files_indexed = 0;      // usable tex1_ PNG files
  uint64_t dds_skipped = 0;        // DDS files in the pack (not decoded yet)
  uint64_t legacy_skipped = 0;     // pre-4.0 "GALE01_<hash>_<fmt>" names
  uint64_t material_skipped = 0;   // .nrm/.mat/.bump/.spec/.lum companions
  uint64_t other_skipped = 0;
  uint64_t lookups = 0, matched = 0, decoded = 0, decode_failed = 0;
  uint64_t mips_ignored = 0;       // packs that supplied mips we did not use
  bool reported_budget = false;
  std::unordered_map<std::string, bool> dumped;
  // When the last look found nothing, and whether "no pack folder" has been said already.
  std::chrono::steady_clock::time_point last_empty_scan{};
  bool scanned_empty = false, reported_no_folder = false;
};
State g;

// A single decoded image is capped so a hostile or broken PNG cannot ask for gigabytes. 4096x4096
// RGBA is 64 MB, far above anything a Melee pack needs (the largest native texture is 1024x1024).
constexpr uint32_t MAX_DIM = 4096;
constexpr uint64_t MAX_IMAGE_BYTES = 64ull * 1024 * 1024;
constexpr uint64_t MAX_INDEX_FILES = 500000;

std::filesystem::path exe_directory() {
  wchar_t buffer[32768];
  DWORD n = GetModuleFileNameW(nullptr, buffer, (DWORD)std::size(buffer));
  if (!n || n >= std::size(buffer)) return std::filesystem::path(".");
  return std::filesystem::path(std::wstring(buffer, n)).parent_path();
}

// Dolphin looks in Load/Textures/<GameID> and falls back to the three character region free ID
// (HiresTextures.cpp:123 GetTextureDirectory). Packs are shipped both ways, so honour both.
std::filesystem::path find_root(const std::filesystem::path& base) {
  std::error_code ec;
  std::filesystem::path full = base / "Load" / "Textures" / "GALE01";
  if (std::filesystem::is_directory(full, ec)) return full;
  std::filesystem::path region_free = base / "Load" / "Textures" / "GAL";
  if (std::filesystem::is_directory(region_free, ec)) return region_free;
  return {};
}

// Every place a pack may be installed: Dolphin's own path, so existing packs work unchanged, and a
// plain TexturePacks folder beside the game, which is where someone would think to put one.
std::vector<std::filesystem::path> find_roots(const std::filesystem::path& base) {
  std::vector<std::filesystem::path> out;
  std::error_code ec;
  if (auto dolphin = find_root(base); !dolphin.empty()) out.push_back(dolphin);
  std::filesystem::path plain = base / "TexturePacks";
  if (std::filesystem::is_directory(plain, ec)) out.push_back(plain);
  return out;
}

bool ends_with(const std::string& s, const char* suffix) {
  size_t n = std::strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string lowered_extension(const std::filesystem::path& p) {
  std::string e = p.extension().string();
  for (char& c : e) c = (char)std::tolower((unsigned char)c);
  return e;
}

// Mirrors the classification loop at HiresTextures.cpp:177-256.
void index_file(const std::filesystem::path& file) {
  const std::string extension = lowered_extension(file);
  if (extension == ".dds") { ++g.dds_skipped; return; }
  if (extension != ".png") { ++g.other_skipped; return; }

  std::string stem = file.stem().string();    // Dolphin's SplitPath drops only the last extension
  if (stem.rfind("tex1_", 0) != 0) {
    // "GALE01_<hash>_<format>" is Dolphin's pre-4.0 naming. Its hash is a sampled Murmur variant
    // whose value depends on the user's Safe Texture Cache slider (Common/Hash.cpp:533
    // GetHashHiresTexture), so it cannot be reproduced from the texture alone. Dolphin's own
    // answer is to rename such packs; count them and say so rather than pretend to support them.
    if (stem.rfind("GALE01_", 0) == 0 || stem.rfind("GAL_", 0) == 0) ++g.legacy_skipped;
    else ++g.other_skipped;
    return;
  }
  // Material map companions (HiresTextures.cpp:58 s_maps_tags). We replace colour only.
  for (const char* tag : {".mat", ".nrm", ".bump", ".spec", ".lum"})
    if (ends_with(stem, tag)) { ++g.material_skipped; return; }
  if (ends_with(stem, "_lum")) { ++g.material_skipped; return; }

  // "_mipN" trailing tag (HiresTextures.cpp:222).
  uint32_t level = 0;
  size_t underscore = stem.find_last_of('_');
  if (underscore != std::string::npos && stem.compare(underscore + 1, 3, "mip") == 0) {
    long parsed = std::strtol(stem.c_str() + underscore + 4, nullptr, 10);
    if (parsed < 0 || parsed > 15) { ++g.other_skipped; return; }
    level = (uint32_t)parsed;
    stem.resize(underscore);
  }
  Entry& entry = g.index[stem];
  entry.pack = g.indexing_pack;
  if (entry.levels.size() <= level) entry.levels.resize(level + 1);
  entry.levels[level] = file.string();
  ++g.files_indexed;
}

void build_index() {
  std::filesystem::path base = exe_directory();
  g.root = find_root(base);
  g.roots = find_roots(base);
  if (g.roots.empty()) {
    // Fall back to the working directory, which is not the executable's folder when the game is
    // started from a shortcut or from the launcher's own directory.
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec) { g.root = find_root(cwd); g.roots = find_roots(cwd); }
  }
  // Only give up when there is nowhere at all to look. This used to test the Dolphin-style
  // Load/Textures/GALE01 folder alone, so a player who put a pack in TexturePacks and had never
  // created a Load folder got no scan at all and an empty list, which is the one case the
  // TexturePacks folder exists to serve.
  if (g.roots.empty()) {
    if (!g.reported_no_folder)
      host::log("textures: no pack folder; create %s and drop a pack in it",
                (base / "TexturePacks").string().c_str());
    g.reported_no_folder = true;
    return;
  }
  std::error_code ec;
  // Each immediate subdirectory of a root is one pack, so they can be switched on and off
  // individually; anything loose in the root is gathered into a single unnamed pack so a plain
  // Dolphin pack still works exactly as it does in Dolphin.
  uint64_t seen_total = 0;
  auto scan_dir = [&](const std::filesystem::path& dir, int pack) {
    g.indexing_pack = pack;
    std::error_code walk_ec;
    std::filesystem::recursive_directory_iterator w(dir, std::filesystem::directory_options::skip_permission_denied, walk_ec);
    if (walk_ec) return;
    const uint64_t before = g.files_indexed;
    for (std::filesystem::recursive_directory_iterator end; w != end; w.increment(walk_ec)) {
      if (walk_ec) { walk_ec.clear(); continue; }
      if (++seen_total > MAX_INDEX_FILES) { host::log("textures: stopped after %llu files", (unsigned long long)MAX_INDEX_FILES); return; }
      std::error_code file_ec;
      if (!w->is_regular_file(file_ec) || file_ec) continue;
      index_file(w->path());
    }
    if (pack >= 0 && pack < (int)g.packs.size()) g.packs[(size_t)pack].files += g.files_indexed - before;
  };
  for (const auto& root : g.roots) {
    std::error_code dir_ec;
    bool loose = false;
    std::filesystem::directory_iterator top(root, std::filesystem::directory_options::skip_permission_denied, dir_ec);
    if (dir_ec) continue;
    for (std::filesystem::directory_iterator end; top != end; top.increment(dir_ec)) {
      if (dir_ec) { dir_ec.clear(); continue; }
      std::error_code kind_ec;
      if (top->is_directory(kind_ec) && !kind_ec) {
        const std::string name = top->path().filename().string();
        int index = -1;
        for (size_t k = 0; k < g.packs.size(); ++k) if (g.packs[k].name == name) index = (int)k;
        if (index < 0) { g.packs.push_back(Pack{name, pack_enabled_default(name), 0}); index = (int)g.packs.size() - 1; }
        scan_dir(top->path(), index);
      } else if (!kind_ec) {
        loose = true;
      }
    }
    if (loose) {
      int index = -1;
      for (size_t k = 0; k < g.packs.size(); ++k) if (g.packs[k].name == kLoosePackName) index = (int)k;
      if (index < 0) { g.packs.push_back(Pack{kLoosePackName, pack_enabled_default(kLoosePackName), 0}); index = (int)g.packs.size() - 1; }
      g.indexing_pack = index;
      const uint64_t before = g.files_indexed;
      std::error_code file_ec;
      std::filesystem::directory_iterator files(root, std::filesystem::directory_options::skip_permission_denied, file_ec);
      for (std::filesystem::directory_iterator end; !file_ec && files != end; files.increment(file_ec)) {
        std::error_code kind_ec;
        if (files->is_regular_file(kind_ec) && !kind_ec) index_file(files->path());
      }
      g.packs[(size_t)index].files += g.files_indexed - before;
    }
  }
  std::string where;
  for (const auto& root : g.roots) { if (!where.empty()) where += " and "; where += root.string(); }
  host::log("textures: %llu replacements in %llu files under %s",
            (unsigned long long)g.index.size(), (unsigned long long)g.files_indexed, where.c_str());
  if (g.dds_skipped)
    host::log("textures: %llu DDS files ignored (only PNG is decoded; re-save them as PNG)", (unsigned long long)g.dds_skipped);
  if (g.legacy_skipped)
    host::log("textures: %llu files use Dolphin's pre-4.0 \"GALE01_...\" names, which cannot be matched; "
              "load the pack once in Dolphin with \"Prefetch/Convert\" to rename them to tex1_...",
              (unsigned long long)g.legacy_skipped);
  if (g.material_skipped)
    host::log("textures: %llu material/normal/emissive maps ignored (colour replacement only)", (unsigned long long)g.material_skipped);
  if (g.index.empty() && g.other_skipped)
    host::log("textures: %llu files did not look like Dolphin texture names", (unsigned long long)g.other_skipped);
}

std::vector<uint8_t> read_file(const std::string& path, bool* too_big) {
  *too_big = false;
  std::vector<uint8_t> bytes;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return bytes;
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size <= 0 || (uint64_t)size > MAX_IMAGE_BYTES) { *too_big = size > 0; std::fclose(f); return bytes; }
  bytes.resize((size_t)size);
  if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) bytes.clear();
  std::fclose(f);
  return bytes;
}

// Decodes one PNG to RGBA8. Dimensions are checked before the pixels are expanded, so a bad or
// enormous file costs a header read rather than an allocation.
bool decode_png(const std::string& path, std::vector<uint8_t>& out, uint32_t* width, uint32_t* height) {
  bool too_big = false;
  std::vector<uint8_t> file = read_file(path, &too_big);
  if (file.empty()) {
    host::log("textures: %s (%s)", path.c_str(), too_big ? "larger than the 64 MB limit" : "unreadable or empty");
    return false;
  }
  int w = 0, h = 0, channels = 0;
  if (!stbi_info_from_memory(file.data(), (int)file.size(), &w, &h, &channels)) {
    host::log("textures: %s is not a readable PNG (%s)", path.c_str(), stbi_failure_reason());
    return false;
  }
  if (w <= 0 || h <= 0 || (uint32_t)w > MAX_DIM || (uint32_t)h > MAX_DIM ||
      (uint64_t)w * (uint64_t)h * 4 > MAX_IMAGE_BYTES) {
    host::log("textures: %s is %dx%d, outside the supported range (1..%u per side)", path.c_str(), w, h, MAX_DIM);
    return false;
  }
  stbi_uc* pixels = stbi_load_from_memory(file.data(), (int)file.size(), &w, &h, &channels, 4);
  if (!pixels) { host::log("textures: %s failed to decode (%s)", path.c_str(), stbi_failure_reason()); return false; }
  out.assign(pixels, pixels + (size_t)w * h * 4);
  stbi_image_free(pixels);
  *width = (uint32_t)w; *height = (uint32_t)h;
  return true;
}

}  // namespace

bool enabled() { return g.on; }
bool dumping() { return g.dump; }

// Scanning is separate from replacing. A player who has a pack installed should see it listed
// without having to switch anything on first: the index is filenames only, and building it is a
// directory walk, not the expensive part. Decoding the PNGs is what costs, and that still happens
// only when replacement is on.
void refresh_packs() {
  if (!g.index.empty() || !g.packs.empty()) return;   // already scanned this run
  // Nothing found so far. The settings panel calls this on every frame it draws the Video tab, and
  // "found nothing" used to mean "look again", so a player with no pack (nearly everyone) had the
  // folders probed at the monitor's refresh rate, on the render thread, for as long as the panel
  // was open: the game stuttered whenever the panel was up. Look again now and then instead, which
  // still picks up a pack dropped in while the panel is open.
  const auto now = std::chrono::steady_clock::now();
  if (g.scanned_empty && now - g.last_empty_scan < std::chrono::seconds(3)) return;
  g.last_empty_scan = now;
  g.scanned_empty = true;
  build_index();
}

bool configure(bool on, bool dump) {
  if (on == g.on && dump == g.dump) return false;
  const bool was_on = g.on;
  if (was_on && !on) report();   // last word on what the pack did before its counters are dropped
  g.on = on;
  g.dump = dump;
  if (on && g.index.empty()) build_index();
  if (dump) {
    g.dump_root = exe_directory() / "Dump" / "Textures" / "GALE01";
    std::error_code ec;
    std::filesystem::create_directories(g.dump_root, ec);
    if (ec) { host::log("textures: cannot create %s (%s)", g.dump_root.string().c_str(), ec.message().c_str()); g.dump = false; }
    else host::log("textures: dumping to %s", g.dump_root.string().c_str());
  }
  return true;
}

// HiresTextures.cpp:323 GenBaseName, new-format branch (line 351 onward). Dolphin feeds it the raw
// GX bytes of level 0 only (TextureCacheBase.cpp:525 texture_size), the TLUT slice at texTlut, the
// BP width/height before block expansion, the GX format, and whether the sampler asks for mipmaps.
std::string base_name(const TextureRef& t, const TextureSnapshot& s) {
  const uint32_t texture_size = texture_level_bytes(t.width, t.height, t.format);
  if (s.image.size() < texture_size) return {};
  const uint8_t* texture = s.image.data();

  // Paletted formats hash only the range of palette entries the texture actually indexes
  // (HiresTextures.cpp:355-387), so two textures sharing a big TLUT still get distinct names.
  const uint8_t* tlut = s.palette.data();
  size_t tlut_size = s.palette.size();
  uint32_t low = 0xffff, high = 0;
  switch (tlut_size) {
    case 0: break;
    case 16 * 2:
      for (size_t i = 0; i < texture_size; i++) {
        low = std::min<uint32_t>(low, texture[i] & 0xf);
        low = std::min<uint32_t>(low, texture[i] >> 4);
        high = std::max<uint32_t>(high, texture[i] & 0xf);
        high = std::max<uint32_t>(high, texture[i] >> 4);
      }
      break;
    case 256 * 2:
      for (size_t i = 0; i < texture_size; i++) {
        low = std::min<uint32_t>(low, texture[i]);
        high = std::max<uint32_t>(high, texture[i]);
      }
      break;
    case 16384 * 2:
      for (size_t i = 0; i < texture_size / 2; i++) {
        uint32_t v = (uint32_t)(((uint32_t)texture[i * 2] << 8) | texture[i * 2 + 1]) & 0x3fff;
        low = std::min(low, v);
        high = std::max(high, v);
      }
      break;
    default: tlut_size = 0; break;   // not a GameCube palette size; behave as unpaletted
  }
  if (tlut_size > 0) {
    tlut_size = 2 * (size_t)(high + 1 - low);
    tlut += 2 * low;
    if ((size_t)(tlut - s.palette.data()) + tlut_size > s.palette.size()) return {};
  }

  const uint64_t tex_hash = xxh64(texture, texture_size);
  const uint64_t tlut_hash = tlut_size ? xxh64(tlut, tlut_size) : 0;
  // SamplerCommon.h IsBpTexMode0MipmapsEnabled: min_filter is TexMode0 bits 5..7, and any of its
  // low two bits selects a mip filter. Deliberately not "mip_levels > 1": Dolphin passes the raw
  // sampler predicate (TextureCacheBase.cpp:493,711), so a texture too small for a mip chain still
  // gets the _m tag.
  const bool has_mipmaps = (((t.mode0 >> 5) & 7) & 3) != 0;

  char name[128];
  int n = std::snprintf(name, sizeof name, "tex1_%ux%u%s_%016llx", t.width, t.height,
                        has_mipmaps ? "_m" : "", (unsigned long long)tex_hash);
  if (tlut_size)
    n += std::snprintf(name + n, sizeof name - n, "_%016llx", (unsigned long long)tlut_hash);
  std::snprintf(name + n, sizeof name - n, "_%u", t.format);
  return name;
}

std::vector<PackInfo> packs() {
  std::vector<PackInfo> out;
  out.reserve(g.packs.size());
  for (const auto& p : g.packs) out.push_back(PackInfo{p.name, p.enabled, p.files});
  return out;
}

bool set_pack_enabled(const std::string& name, bool enabled) {
  bool changed = false;
  for (auto& p : g.packs)
    if (p.name == name && p.enabled != enabled) { p.enabled = enabled; changed = true; }
  // Mirrored into the remembered set so the choice survives a restart, and so a pack whose folder
  // appears after launch still comes back switched off.
  auto at = std::find(g_disabled.begin(), g_disabled.end(), name);
  if (enabled && at != g_disabled.end()) { g_disabled.erase(at); changed = true; }
  if (!enabled && at == g_disabled.end()) { g_disabled.push_back(name); changed = true; }
  return changed;
}

std::vector<std::string> disabled_packs() { return g_disabled; }

void set_disabled_packs(std::vector<std::string> names) {
  g_disabled = std::move(names);
  for (auto& p : g.packs) p.enabled = pack_enabled_default(p.name);
}



// Prefetch runs on its own thread: the decode is pure CPU work on files, touches no device object,
// and the only thing it shares is the cache below, so the game keeps booting while it works.
std::thread g_prefetch;
std::atomic<bool> g_prefetching{false};
std::atomic<uint64_t> g_prefetch_done{0}, g_prefetch_total{0};


void open_packs_folder() {
  const std::filesystem::path folder = exe_directory() / "TexturePacks";
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  // A pack is a folder of PNGs and can be gigabytes; copying one into place would duplicate it for
  // no reason. Opening the folder lets the player move or link their own copy where it belongs.
  ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  g.packs.clear();
  g.index.clear();
  build_index();   // pick up anything already there, so the list is right the moment it opens
}

void prefetch_begin() {
  if (g_prefetching.load(std::memory_order_relaxed)) return;
  refresh_packs();
  if (!g.on || g.index.empty()) return;
  if (g_prefetch.joinable()) g_prefetch.join();
  g_prefetch_done.store(0, std::memory_order_relaxed);
  g_prefetch_total.store(g.index.size(), std::memory_order_relaxed);
  g_prefetching.store(true, std::memory_order_release);
  g_prefetch = std::thread([] {
    std::vector<std::string> names;
    names.reserve(g.index.size());
    for (const auto& kv : g.index) names.push_back(kv.first);
    for (const auto& name : names) {
      if (!g.on) break;   // switched off mid-run: stop rather than finish work nobody wants
      load(name, ~0ull);  // decoded into the cache; the draw path then finds it ready
      g_prefetch_done.fetch_add(1, std::memory_order_relaxed);
    }
    host::log("textures: prefetched %llu of %llu replacements",
              (unsigned long long)g_prefetch_done.load(), (unsigned long long)g_prefetch_total.load());
    g_prefetching.store(false, std::memory_order_release);
  });
}

bool prefetching() { return g_prefetching.load(std::memory_order_relaxed); }

void prefetch_progress(uint64_t* done, uint64_t* total) {
  if (done) *done = g_prefetch_done.load(std::memory_order_relaxed);
  if (total) *total = g_prefetch_total.load(std::memory_order_relaxed);
}

void note_lookup(bool was_matched) {
  ++g.lookups;
  if (was_matched) ++g.matched;
}

std::unique_ptr<Replacement> load(const std::string& base, uint64_t budget_bytes) {
  if (!g.on || base.empty()) return nullptr;
  auto found = g.index.find(base);
  if (found == g.index.end()) return nullptr;
  const Entry& entry = found->second;
  // A pack switched off in the panel contributes nothing, without a rescan.
  if (entry.pack >= 0 && entry.pack < (int)g.packs.size() && !g.packs[(size_t)entry.pack].enabled) return nullptr;
  if (entry.levels.empty() || entry.levels[0].empty()) {
    host::log("textures: %s has mips but no level 0, ignored", base.c_str());
    return nullptr;
  }

  auto out = std::make_unique<Replacement>();
  std::vector<uint8_t> level;
  uint32_t w = 0, h = 0;
  if (!decode_png(entry.levels[0], level, &w, &h)) { ++g.decode_failed; return nullptr; }

  // Refuse before allocating the mip chain rather than after: the budget exists so a huge pack
  // degrades into "some textures replaced" instead of exhausting video memory.
  if ((uint64_t)w * h * 4 > budget_bytes) {
    if (!g.reported_budget) {
      g.reported_budget = true;
      host::log("textures: replacement budget reached; the rest of the pack stays at native resolution");
    }
    return nullptr;
  }

  out->width = w; out->height = h; out->levels = 1;
  out->level_offset.push_back(0);
  out->level_width.push_back(w);
  out->level_height.push_back(h);
  out->pixels = std::move(level);

  // Extra levels are used only while they form the chain D3D expects. A pack that supplies a
  // wrongly sized mip loses that level and everything past it, not the whole texture.
  for (uint32_t i = 1; i < entry.levels.size(); ++i) {
    const uint32_t want_w = std::max(1u, w >> i), want_h = std::max(1u, h >> i);
    if (entry.levels[i].empty()) { ++g.mips_ignored; break; }
    std::vector<uint8_t> mip;
    uint32_t mw = 0, mh = 0;
    if (!decode_png(entry.levels[i], mip, &mw, &mh)) { ++g.decode_failed; break; }
    if (mw != want_w || mh != want_h) {
      host::log("textures: %s_mip%u is %ux%u but level %u of a %ux%u texture must be %ux%u, ignored from here",
                base.c_str(), i, mw, mh, i, w, h, want_w, want_h);
      ++g.mips_ignored;
      break;
    }
    if (out->pixels.size() + mip.size() > budget_bytes) { ++g.mips_ignored; break; }
    out->level_offset.push_back((uint32_t)out->pixels.size());
    out->level_width.push_back(mw);
    out->level_height.push_back(mh);
    out->pixels.insert(out->pixels.end(), mip.begin(), mip.end());
    ++out->levels;
  }
  ++g.decoded;
  return out;
}

void dump_level(const std::string& base, uint32_t level, const uint8_t* level_rgba,
                uint32_t width, uint32_t height) {
  if (!g.dump || base.empty() || !level_rgba || !width || !height) return;
  char suffix[16] = "";
  if (level) std::snprintf(suffix, sizeof suffix, "_mip%u", level);
  std::string name = base + suffix + ".png";
  // Dolphin only writes a dump that is not already there (TextureCacheBase.cpp:456). The in-memory
  // set keeps us from stat-ing the same file once per texture upload.
  if (!g.dumped.emplace(name, true).second) return;
  std::filesystem::path path = g.dump_root / name;
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) return;
  if (!stbi_write_png(path.string().c_str(), (int)width, (int)height, 4, level_rgba, (int)width * 4))
    host::log("textures: could not write %s", path.string().c_str());
}

void report() {
  if (!g.on) return;
  host::log("textures: %llu of %llu textures the game drew had a replacement (%llu decoded, %llu failed, %llu pack entries unused)",
            (unsigned long long)g.matched, (unsigned long long)g.lookups, (unsigned long long)g.decoded,
            (unsigned long long)g.decode_failed,
            (unsigned long long)(g.index.size() > g.matched ? g.index.size() - g.matched : 0));
  if (g.mips_ignored)
    host::log("textures: %llu mip levels from the pack were not used", (unsigned long long)g.mips_ignored);
  if (g.lookups && !g.matched)
    host::log("textures: nothing matched. Check that the files sit under %s and are named tex1_<w>x<h>_<hash>_<format>.png; "
              "run with --dump-textures to write the exact names this build looks for.",
              g.root.empty() ? "Load/Textures/GALE01" : g.root.string().c_str());
}

}  // namespace gx::texpack
