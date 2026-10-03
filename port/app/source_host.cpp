// The source port inside the full application: the same window, renderers (D3D12 with DLSS, D3D11),
// sub-frame presentation, input, settings panel and pacing as the recompiled build, with the game
// itself coming from melee_game.dll (the decompiled sources built natively, see sourceport/game)
// instead of the translated guest. main.cpp calls reserve_memory() first thing and run() where the
// recompiled build would enter __start.
//
// What still differs from the recompiled build is listed where it happens: no memory card yet
// (M10), Slippi's game side not present (M12).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_host.h"
#include <windows.h>
#include <algorithm>
#include <deque>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "host.h"
#include "audio.h"
#include "audio_core.h"
#include "ax_ucode.h"
#include "gx_core.h"
#include "gx_shader.h"
#include "render_options.h"
#include "training_overlay.h"
#include "lcancel.h"
#include "user_gecko.h"
#include "mu_host.h"
#include "ppc.h"
#include "render_observer.h"
#include "native_state_layout.h"
#include "native_savestate.h"
#ifdef MELEE_NO_SLIPPI
#include "netplay_state.h"   // the same names, answered from the neutral netplay state
#else
#include "native_practice.h"
#include "jukebox.h"
#include "slippi_playback.h"
#include "slippi_online.h"
#include "exi_slippi.h"
#include "native_slippi_bridge.h"
#include "native_replay_stream.h"
#include "slippi_net.h"
#include "native_recording_codes.h"
#include "slippilib/SlippiGame.h"
#endif
#include "source_mod_overlay.h"
#include "mod_profile.h"
#include "net_trace.h"
#include "replay_control.h"
#include "mod_scan.h"
#include "hackpack_source.h"
#include "pack_skin_rule.h"
#include "disc_skin_scan.h"
#include "cosmetic_mods.h"
#include "texture_pack.h"
#include "window.h"

// The settings panel's port-code options (pc_settings.cpp); the native game reads them through
// MuHostApi.game_options.
namespace gecko { extern bool option_no_screen_shake; extern bool option_pal_stock_icons; extern bool option_widescreen; }

namespace source_port {
namespace {

constexpr uintptr_t MEM1_BASE = 0x80000000u;
constexpr uint32_t MEM1_SIZE = 40u << 20;   // the console's 24 MB, grown for 8-byte pointers; the game image follows
constexpr uintptr_t LOCKED_CACHE_BASE = 0xE0000000u;
constexpr uint32_t LOCKED_CACHE_SIZE = 16u << 10;
// Right after MEM1, so the game's own statics (the font atlas, static textures) have a physical
// address the GX texture and display-list registers can hold: 26 bits, the first 64 MB.
constexpr uintptr_t GAME_IMAGE_BASE = 0x82800000u;

MuGameApi g_game{};
std::string g_dll = "melee_game.dll";
audio_core::State g_audio;

// Native online play (host API 13). While the game re-simulates after a rollback load, the
// machine stands still: no time, alarms, audio, retraces or presentation.
native_savestate::Engine g_savestates;
bool g_resim = false;
uint64_t g_resim_phases = 0, g_resim_polls = 0, g_resim_vi_waits = 0;
// --online-test: a Slippi mode id (the harness enters the match without menus), or -1.
int g_online_test_mode = -1;
uint8_t g_online_test_character = 2, g_online_test_color = 0;
bool g_online_test_started = false;

bool trace_ax_voice_window() {
  const char* range = std::getenv("MELEE_TRACE_AX_SFX");
  unsigned first = 0, last = 0;
  const uint32_t retrace = host::retrace_count();
  return range && std::sscanf(range, "%u:%u", &first, &last) == 2 &&
         first <= last && retrace >= first && retrace <= last;
}

void trace_ax_voice(const ax::VoiceTrace& trace) {
  if (!trace_ax_voice_window()) return;
  host::log("[ax-voice-native] retrace=%u block=%llu ms=%u pb=%08X cur=%08X>%08X end=%08X ratio=%08X frac=%04X>%04X env=%04X/%04X input=%d/%d peak=%d output=%d/%d peak=%d",
            host::retrace_count(), (unsigned long long) trace.frame,
            trace.millisecond, trace.pb_addr, trace.cur_before,
            trace.cur_after, trace.end_addr, trace.ratio, trace.frac_before,
            trace.frac_after, trace.volume_before, trace.volume_delta,
            trace.input_first, trace.input_last, trace.input_peak,
            trace.output_first, trace.output_last, trace.output_peak);
}

// MELEE_TRACE_AX_VOICES (M4 diagnostic, off by default): every AX frame and every voice block the
// ucode renders, with hashes of the voice's own samples before the final mix (ax_ucode.h).
void trace_ax_frame(const char* line) {
  host::log("[ax-vtrace] retrace=%u tb=%llu vi=%llu %s", host::retrace_count(),
            (unsigned long long)(host::cpu ? host::cpu->tb : 0),
            (unsigned long long)(host::next_retrace_tb() - host::TB_PER_FRAME), line);
}

uint16_t ax_native_rd16(uint32_t addr) {
  uint16_t value;
  std::memcpy(&value, host::ptr(addr, sizeof value), sizeof value);
  return value;
}
uint32_t ax_native_rd32(uint32_t addr) {
  uint32_t value;
  std::memcpy(&value, host::ptr(addr, sizeof value), sizeof value);
  return value;
}
void ax_native_wr16(uint32_t addr, uint16_t value) {
  std::memcpy(host::ptr(addr, sizeof value), &value, sizeof value);
}
void ax_native_wr32(uint32_t addr, uint32_t value) {
  std::memcpy(host::ptr(addr, sizeof value), &value, sizeof value);
}

// ---- the disc's filesystem table, for the game's entry numbers ----
struct FstFile { uint32_t offset, length; bool dir; };
std::vector<FstFile> g_fst;
std::unordered_map<std::string, int32_t> g_paths;   // lower-case "/dir/name" -> entry number
std::vector<uint8_t> g_fst_raw;                      // the disc's table as read, for the cosmetic layer

// ---- content views ----
// A mod profile changes some of the disc's files. Every changed file also keeps an entry for its
// retail copy (a separate entry number, so nothing the game cached from the mod is reused), and a
// file only the mod has gets -1. While the retail view is on (online modes that play the retail
// game), opening a path gives the retail entry. Paths Slippi's menu layer serves are the same in
// both views.
std::unordered_map<int32_t, int32_t> g_view_alias;   // mod entry -> retail entry, or -1
bool g_retail_view = false;
// A pack's costume files that only change looks stay on in the retail view (pack_skin_rule.h): the
// same rule a skin from the Mods tab follows, so a skin behaves the same wherever it came from.
// Every costume file of the loaded packs has a verdict here, served or not, for the log and the panel.
std::vector<skins::Verdict> g_pack_skins;
std::unordered_set<int32_t> g_pack_skin_served;
slippi::online::NativeGameplayProfile g_mod_gameplay_profile = slippi::online::NativeGameplayProfile::Vanilla;
std::string g_mod_display_name;

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

bool read_fst() {
  uint8_t header[0x440];
  if (!host::disc_read(0, header, sizeof header)) return false;
  const uint32_t fst_offset = be32(header + 0x424), fst_size = be32(header + 0x428);
  // A trimmed, compressed or damaged image can have no usable table here: refuse it plainly instead
  // of reading an empty buffer (a 0.8.5 player's disc crashed at startup this way).
  if (fst_size < 12 || fst_size > (16u << 20)) return false;
  std::vector<uint8_t> fst(fst_size);
  if (!host::disc_read(fst_offset, fst.data(), fst_size)) return false;
  const uint32_t count = be32(fst.data() + 8);
  if (count < 1 || (uint64_t)count * 12 > fst_size) return false;
  const char* names = (const char*)fst.data() + count * 12;
  g_fst_raw = fst;
  g_fst.assign(count, FstFile{});
  std::vector<std::pair<uint32_t, std::string>> dirs{{count, ""}};   // (end entry, path) of open directories
  for (uint32_t i = 1; i < count; ++i) {
    while (dirs.size() > 1 && i >= dirs.back().first) dirs.pop_back();
    const uint8_t* e = fst.data() + i * 12;
    const uint32_t name_at = count * 12 + (be32(e) & 0xFFFFFFu);
    if (name_at >= fst_size) return false;
    std::string name(names + (be32(e) & 0xFFFFFFu), strnlen(names + (be32(e) & 0xFFFFFFu), fst_size - name_at));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
    const std::string full = dirs.back().second + "/" + name;
    g_fst[i] = {be32(e + 4), be32(e + 8), e[0] != 0};
    g_paths[full] = (int32_t)i;
    if (e[0]) dirs.push_back({be32(e + 8), full});
  }
  // The boot loader left the disc header at the start of MEM1, then the OS globals. The native game
  // reads those words as ordinary integers, so they are in host byte order.
  std::memcpy((void*)MEM1_BASE, header, 0x20);
  auto low = [](uint32_t offset, uint32_t value) { std::memcpy((void*)(MEM1_BASE + offset), &value, 4); };
  low(0x28, 24u << 20);      // physical memory size
  low(0xCC, 0);              // TV mode: NTSC
  low(0xF0, 24u << 20);      // simulated memory size
  low(0xF8, 162000000u);     // bus clock
  low(0xFC, 486000000u);     // core clock
  return true;
}

// ---- loose-file mods ----
// A file placed at Mods/Files/<disc path> (beside the game, same folder layout as the disc) is
// read instead of the disc's copy: costumes, textures, stages and other data-only mods, no code.
// A file the disc does not have is added. Each gets an offset range past the end of any
// GameCube disc, and reads there are served from memory, so the game's own loader is unchanged.
ModOverlay g_mod_overlay;
std::filesystem::path g_mod_root = std::filesystem::path("Mods") / "Files";
bool g_mod_root_explicit = false;
// Every mod layer in load order: the profile's (--mod-profile), then --mod-iso/--mod-dir/--mod-gci
// in command-line order, then Mods/Files when it exists. A later layer wins over an earlier one.
std::string g_mod_profile_name;
std::vector<mods::Layer> g_mod_layers;
std::vector<std::filesystem::path> g_mod_gcis;
std::string g_mod_fingerprint;                 // content identity of the enabled layers, "" = vanilla
std::filesystem::path g_profile_card;          // the profile's own card folder, "" = the ordinary card
std::vector<mods::CardImport> g_card_imports;

// Known packs, by the content fingerprint of their layer (see iso_layer_fingerprint). The 20XX TE line
// only matches the untouched official file; any 20XX TE v2d r4 save, including one Melee has saved
// records into, is recognized by its code payload (mods::check_te_file, used below and by the scan).
struct KnownPack { const char* fingerprint; const char* name; };
const KnownPack kKnownPacks[] = {
  {"9a1a48b999a2f8f4deaee2554e5f177cb265666b95676a508406db80b181246b", "Akaneia 1.0.1"},
  {"61834b65d921668ff11702c458a1ae6946172b0a4c8321a47e2c2ddff9aef51d", "20XX TE 2d r4"},
};

// An ISO layer's fingerprint covers what it changes: every changed or added file's path and
// content, the changed main.dol runs and the retail files it drops. Hashing a whole modded disc
// takes seconds, so the result is cached by path, size and modification time.
std::string iso_layer_fingerprint(const std::filesystem::path& iso, const std::string& profile) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const auto size = fs::file_size(iso, ec);
  const auto stamp = fs::last_write_time(iso, ec).time_since_epoch().count();
  const std::string key = fs::absolute(iso, ec).u8string() + "|" + std::to_string(size) + "|" + std::to_string(stamp);
  const fs::path cache = fs::path("Mods") / ".cache" / "fingerprints.txt";
  {
    std::ifstream in(cache);
    std::string line;
    while (std::getline(in, line)) {
      const auto tab = line.rfind('\t');
      if (tab == key.size() && line.compare(0, tab, key) == 0) return line.substr(tab + 1);
    }
  }
  const auto begin = std::chrono::steady_clock::now();
  mods::Sha256 total;
  std::vector<uint8_t> buffer(1 << 20);
  for (const auto& file : g_mod_overlay.files()) {
    if (file.profile != profile) continue;
    total.update(file.path + "\t" + std::to_string(file.length) + "\n");
    for (uint64_t pos = 0; pos < file.length; pos += buffer.size()) {
      const uint32_t n = (uint32_t)std::min<uint64_t>(buffer.size(), file.length - pos);
      if (g_mod_overlay.read(file.start + (uint32_t)pos, buffer.data(), n) != ModOverlay::Read::Success) return {};
      total.update(buffer.data(), n);
    }
  }
  for (const auto& report : g_mod_overlay.iso_reports()) {
    if (report.profile != profile) continue;
    for (const auto& run : report.dol_runs)
      total.update("dol " + std::to_string(run.first) + " " + std::to_string(run.second) + "\n");
    for (const auto& gone : report.deleted) total.update("deleted " + gone + "\n");
  }
  const std::string fingerprint = total.hex();
  host::log("mods: fingerprinted %s in %.1f s", iso.u8string().c_str(),
            std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
  fs::create_directories(cache.parent_path(), ec);
  std::ofstream out(cache, std::ios::app);
  out << key << '\t' << fingerprint << '\n';
  return fingerprint;
}

std::string dir_layer_fingerprint(const std::string& profile) {
  mods::Sha256 total;
  for (const auto& file : g_mod_overlay.files()) {
    if (file.profile != profile) continue;
    total.update(file.path + "\t" + std::to_string(file.length) + "\n");
    total.update(file.bytes.data(), file.bytes.size());
  }
  return total.hex();
}

// ---- the Mods folder (mod_scan.h) ----
// Normal launches only (the settings file was read): the memory card folder check, the scan of the
// Mods folder and the player's on/off choices. The packs found apply when neither a saved profile
// nor --mod-* flags chose this session's mods; the Mods page lists them either way. 20XX TE is not a
// layer here (its save stays out of the card): it switches on TE's native features. Card mods and
// code mods need the Static Recomp and are not loaded.
void apply_detected_mods() {
  if (!gx::RenderOptions::kModFeaturesAvailable || !mods::auto_detect()) return;
  mods::StartupOptions options;
  options.card_dir = std::filesystem::u8path(host::options.card_dir);
  options.base_iso = std::filesystem::u8path(host::options.iso);
  options.source_port = true;
  const mods::Startup found = mods::startup(options);
  for (const auto& line : found.log) host::log("mods: %s", line.c_str());
  // Every mod disc found, loaded here or not, offers its costumes in the skin list (the same list
  // on both engines). A skin picked there for a file a loaded pack replaces is skipped below, so a
  // loaded pack's own costume wins.
  mods::scan_detected_disc_skins(options.base_iso);
  if (!g_mod_layers.empty()) {
    const std::string chooser = g_mod_profile_name.empty() ? std::string("the command line") : "profile " + g_mod_profile_name;
    host::log("mods: %s chose this session's mods; the Mods folder's packs are only listed", chooser.c_str());
    return;
  }
  mods::status().detected = true;
  mods::status().te_owned = found.te;
  g_mod_layers.insert(g_mod_layers.end(), found.layers.begin(), found.layers.end());
}

// Decides, once at load, which of the packs' costume files stay on in the retail view. A file equal
// to the standard costume, or with the standard skeleton, cannot change what the game simulates:
// it is served from the pack in every mode. Anything else follows the retail alias like the pack's
// other files. Only runs when a pack changed a file the standard game has.
void compute_pack_skin_verdicts(const std::unordered_map<std::string, int32_t>& base_paths,
                                const std::vector<FstFile>& base_fst) {
  g_pack_skins.clear();
  g_pack_skin_served.clear();
  auto& notes = mods::status().pack_skins;
  notes.clear();
  for (const auto& file : g_mod_overlay.files()) {
    if (!skins::is_costume_file(file.path)) continue;
    const auto now = g_paths.find(file.path);
    const auto base = base_paths.find(file.path);
    if (now == g_paths.end() || base == base_paths.end() || base_fst[base->second].dir) continue;
    const auto alias = g_view_alias.find(now->second);
    if (alias == g_view_alias.end() || alias->second < 0) continue;   // a costume only the pack has
    skins::Verdict verdict;
    verdict.path = skins::display_name(file.path);
    verdict.entry = now->second;
    // The layer label is "layer N <path>": the panel shows the file or folder name.
    const size_t name_at = file.profile.find(' ', file.profile.find(' ') + 1);
    verdict.layer = name_at == std::string::npos ? file.profile
                    : std::filesystem::u8path(file.profile.substr(name_at + 1)).filename().u8string();
    const FstFile& standard = base_fst[base->second];
    std::vector<uint8_t> pack(file.length), clean(standard.length);
    if (pack.empty() || clean.empty() ||
        g_mod_overlay.read(file.start, pack.data(), file.length) != ModOverlay::Read::Success ||
        !host::disc_read(standard.offset, clean.data(), standard.length)) {
      verdict.reason = "the file could not be read";
    } else if (pack == clean) {
      verdict.identical = verdict.served = true;
      verdict.reason = "same as the standard costume";
    } else {
      std::string detail;
      verdict.served = host::cosmetics::costume_skeleton_matches(clean, pack, &detail);
      verdict.reason = host::cosmetics::online_reason_short(detail);
    }
    if (verdict.served) g_pack_skin_served.insert(verdict.entry);
    host::log("mods: skin %s %s online (%s)", verdict.path.c_str(), verdict.served ? "stays on" : "swapped",
              verdict.reason.c_str());
    notes.push_back({verdict.path, verdict.layer, verdict.reason, verdict.served, verdict.identical});
    g_pack_skins.push_back(std::move(verdict));
  }
}

void load_mod_overlay() {
  namespace fs = std::filesystem;
  std::error_code ec;
  apply_detected_mods();
  if (!g_mod_root_explicit && fs::exists(g_mod_root, ec) && !ec)
    g_mod_layers.push_back({mods::LayerKind::Dir, g_mod_root});
  if (g_mod_layers.empty()) return;
  std::string error;
  g_mod_overlay.set_layered(true);
  // Compare each imported ISO against the original disc, never against a previously
  // overlaid pack, so what a pack changes does not depend on the packs before it.
  const auto base_paths = g_paths;
  const auto base_fst = g_fst;
  auto lookup = [&](const std::string& path, ModOverlay::DiscFile* out) {
    const auto it = base_paths.find(path);
    if (it == base_paths.end() || base_fst[it->second].dir) return false;
    *out = {base_fst[it->second].offset, base_fst[it->second].length};
    return true;
  };
  auto read_base = [](uint32_t offset, void* dst, uint32_t size) {
    return host::disc_read(offset, dst, size);
  };
  {
    ModOverlay::BaseDisc base;
    for (const auto& kv : base_paths) if (!base_fst[kv.second].dir) base.files.push_back(kv.first);
    std::sort(base.files.begin(), base.files.end());
    uint8_t header[0x440];
    uint8_t dol[0x100];
    if (host::disc_read(0, header, sizeof header) && host::disc_read(be32(header + 0x420), dol, sizeof dol)) {
      base.dol_offset = be32(header + 0x420);
      uint32_t extent = sizeof dol;
      for (int i = 0; i < 18; ++i) extent = std::max(extent, be32(dol + i * 4) + be32(dol + 0x90 + i * 4));
      base.dol_size = extent;
    }
    g_mod_overlay.set_base_disc(std::move(base), read_base);
  }
  mods::Sha256 identity;
  std::string summary;
  uint32_t layer_index = 0;
  for (const auto& layer : g_mod_layers) {
    const std::string label = "layer " + std::to_string(++layer_index) + " " + layer.path.u8string();
    std::string fingerprint;
    switch (layer.kind) {
    case mods::LayerKind::Iso:
      if (!g_mod_overlay.add_iso(layer.path, label, lookup, read_base, error)) host::die("mods: %s", error.c_str());
      fingerprint = iso_layer_fingerprint(layer.path, label);
      break;
    case mods::LayerKind::Dir:
      if (!g_mod_overlay.add_directory(layer.path, label, error)) host::die("mods: %s", error.c_str());
      fingerprint = dir_layer_fingerprint(label);
      break;
    case mods::LayerKind::Gci: {
      fingerprint = mods::sha256_file(layer.path);
      std::string gci_id;
      if (fingerprint.empty() || !mods::gci_identity(layer.path, &gci_id, &error))
        host::die("mods: cannot read card file %s", layer.path.u8string().c_str());
      g_mod_gcis.push_back(layer.path);
      g_card_imports.push_back({layer.path, gci_id, fingerprint});
      break;
    }
    }
    const char* kind = layer.kind == mods::LayerKind::Iso ? "iso" : layer.kind == mods::LayerKind::Dir ? "dir" : "gci";
    identity.update(std::string(kind) + "\t" + fingerprint + "\n");
    const KnownPack* known = nullptr;
    for (const auto& pack : kKnownPacks) if (*pack.fingerprint && fingerprint == pack.fingerprint) known = &pack;
    if (known && std::strncmp(known->name, "20XX TE", 7) == 0) mods::status().te_owned = true;
    if (!known && layer.kind == mods::LayerKind::Gci) {
      const mods::TeCheck te = mods::check_te_file(layer.path);
      if (te.supported()) {
        mods::status().te_owned = true;
        host::log("mods: %s is 20XX TE %s (%u of %u payload bytes intact)", layer.path.u8string().c_str(),
                  te.version.c_str(), te.matching_bytes, te.payload_bytes);
      }
    }
    if (known && std::strncmp(known->name, "Akaneia", 7) == 0) mods::status().akaneia = true;
    host::log("mods: %s %s, content %s%s%s", kind, layer.path.u8string().c_str(), fingerprint.substr(0, 16).c_str(),
              known ? ", recognized as " : "", known ? known->name : "");
    summary += (summary.empty() ? "" : ", ") + (known ? std::string(known->name) : layer.path.filename().u8string());
  }
  g_mod_fingerprint = identity.hex();
  for (const auto& report : g_mod_overlay.iso_reports()) {
    uint64_t dol_bytes = 0;
    for (const auto& run : report.dol_runs) dol_bytes += run.second;
    host::log("mods: %s: %u files changed or added, %zu retail files missing, main.dol %u bytes (retail %u), "
              "%zu code patch runs over %llu bytes", report.profile.c_str(), report.changed_files, report.deleted.size(),
              report.dol_size, report.base_dol_size, report.dol_runs.size(), (unsigned long long)dol_bytes);
    if (!report.dol_runs.empty() || report.dol_size != report.base_dol_size)
      host::log("mods: %s patches the game's code; the Source Port runs its own source code, so only the pack's files apply",
                report.profile.c_str());
  }
  for (const auto& conflict : g_mod_overlay.conflicts())
    host::log("mods: %s is in both %s and %s; the later one is used", conflict.path.c_str(),
              conflict.earlier.c_str(), conflict.later.c_str());

  // Online policy: a single ISO carrying the m-ex tables and Akaneia's added fighters is treated
  // as Akaneia (Direct only); anything else is another mod.
  auto has = [](const char* path) {
    return std::any_of(g_mod_overlay.files().begin(), g_mod_overlay.files().end(),
        [path](const ModOverlay::File& file) { return file.path == path; });
  };
  const bool one_iso = g_mod_layers.size() == 1 && g_mod_layers[0].kind == mods::LayerKind::Iso;
  const bool akaneia = one_iso && has("/mxdt.dat") && has("/plsn.dat") && has("/plts.dat");
  if (akaneia) host::log("mods: Akaneia profile detected; its content online is Direct-only");
  // Training Mode CE: its disc's TM/ files carry the event menus and lab models; its code is
  // compiled into the native game (sourceport/game/tmce). Like the original, vanilla base only.
  if (has("/tm/eventmenu.dat")) {
    if (akaneia || mods::status().akaneia) {
      host::log("mods: Training Mode CE files found, but Training Mode CE needs the vanilla game; left off");
    } else {
      mods::status().tmce = true;
      host::log("mods: Training Mode CE files found; its events run natively");
    }
  }
  // The 20XX Hack Pack (hackpack_source.h): its stages, music and costumes load like any other
  // pack's files; its menu files and the files that carry its code do not (pack_keeps below), and
  // the game's native rewrites of its features get its data tables through command 0xFA.
  std::filesystem::path pack_iso;
  for (const auto& file : g_mod_overlay.files()) {
    if (!hackpack::is_pack_marker(file.path) || file.iso.empty() || !has("/mnslchr.0sd")) continue;
    pack_iso = file.iso;
    hackpack::attach(file.iso, file.iso_offset, file.length);
    mods::status().hackpack = true;
    host::log("mods: 20XX Hack Pack files found in %s; its content loads with the retail menus, none of its code runs",
              file.iso.filename().u8string().c_str());
  }
  const auto pack_keeps = [&](const ModOverlay::File& file) {
    return !pack_iso.empty() && file.iso == pack_iso && hackpack::keeps_retail(file.path);
  };
  // The pack's extra costumes (PlFxNr.lat, PlFxNr.rat and the other sets) become skins of their
  // costume slot, the same scan that lists a Mods folder disc's costumes (a disc scanned before and
  // unchanged is not read again), so L / R on the character select steps through them. The pack is
  // this session's disc: its plain costumes are already the standard ones, and its sets may serve
  // a costume file it replaced (load_cosmetics, republish_cosmetic_slot).
  if (!pack_iso.empty()) {
    const auto scan = host::cosmetics::scan_disc_skins(pack_iso.u8string(), pack_iso.stem().u8string() + " disc");
    if (!scan.ok) host::log("mods: 20XX Hack Pack costumes not listed as skins: %s", scan.message.c_str());
    host::cosmetics::set_session_pack(pack_iso.u8string());
  }

  // The profile's own memory card, named after the profile, or after the content for layers given
  // on the command line. The ordinary card is never written by a modded session.
  {
    const fs::path ordinary = fs::u8path(host::options.card_dir);
    const std::string folder = !g_mod_profile_name.empty() ? mods::safe_name(g_mod_profile_name)
                                                          : "mods-" + g_mod_fingerprint.substr(0, 12);
    g_profile_card = ordinary.parent_path() / "Profiles" / fs::u8path(folder);
    std::vector<std::string> notes;
    if (!mods::prepare_profile_card(ordinary, g_profile_card, g_card_imports, &notes, &error))
      host::die("mods: %s", error.c_str());
    for (const auto& note : notes) host::log("card: %s", note.c_str());
  }
  {
    auto& status = mods::status();
    status.profile = g_mod_profile_name;
    status.identity = g_mod_fingerprint;
    status.layers.clear();
    for (const auto& layer : g_mod_layers) {
      const char* kind = layer.kind == mods::LayerKind::Iso ? "Disc" : layer.kind == mods::LayerKind::Dir ? "Files" : "Save";
      status.layers.push_back(std::string(kind) + ": " + layer.path.filename().u8string());
    }
    status.notes.clear();
    for (const auto& report : g_mod_overlay.iso_reports())
      if (!report.dol_runs.empty() || report.dol_size != report.base_dol_size)
        status.notes.push_back(std::to_string(report.dol_runs.size()) +
                               " code patches in a modded disc are not applied: the Source Port runs its own game code.");
    for (const auto& conflict : g_mod_overlay.conflicts())
      status.notes.push_back(conflict.path.substr(1) + " is in two layers; the later one is used.");
    status.notes.push_back("Memory card: " + g_profile_card.u8string());
  }
  host::log("mods: profile %s, identity %s, %zu layers: %s",
            g_mod_profile_name.empty() ? "(command line)" : g_mod_profile_name.c_str(),
            g_mod_fingerprint.substr(0, 16).c_str(), g_mod_layers.size(), summary.c_str());

  uint32_t replaced = 0, added = 0, pack_kept = 0;
  for (const auto& file : g_mod_overlay.files()) {
    if (pack_keeps(file)) { ++pack_kept; continue; }   // retail menus, and no file that carries code
    auto found = g_paths.find(file.path);
    int32_t entry;
    if (found != g_paths.end()) {
      if (g_fst[found->second].dir) host::die("mods: file conflicts with disc directory %s", file.path.c_str());
      entry = found->second; ++replaced;
    } else {
      entry = (int32_t)g_fst.size(); g_fst.push_back({0, 0, false}); g_paths[file.path] = entry; ++added;
    }
    g_fst[entry] = {file.start, file.length, false};
  }
  if (replaced || added) host::log("mods: %u disc files replaced, %u added", replaced, added);
  if (pack_kept)
    host::log("mods: 20XX Hack Pack: %u of its files are not served (its menu files and the files that carry its code)", pack_kept);

  // The retail view of every file the layers changed.
  g_view_alias.clear();
  for (const auto& file : g_mod_overlay.files()) {
    if (pack_keeps(file)) continue;   // not served: the disc's own entry, the same in both views
    const auto now = g_paths.find(file.path);
    if (now == g_paths.end()) continue;
    const auto base = base_paths.find(file.path);
    if (base == base_paths.end() || base_fst[base->second].dir) { g_view_alias[now->second] = -1; continue; }
    const int32_t alias = (int32_t)g_fst.size();
    g_fst.push_back(base_fst[base->second]);
    g_view_alias[now->second] = alias;
  }
  g_mod_gameplay_profile = g_view_alias.empty() ? slippi::online::NativeGameplayProfile::Vanilla
                           : akaneia ? slippi::online::NativeGameplayProfile::Akaneia
                                     : slippi::online::NativeGameplayProfile::OtherMod;
  slippi::online::set_native_gameplay_profile(g_mod_gameplay_profile);
  g_mod_display_name = summary.empty() ? std::string("this mod") : summary;
#ifdef MELEE_NO_SLIPPI   // the same fact without the other build's mode names
  if (!g_view_alias.empty())
    host::log("mods: %zu files have a retail view: online matches play the retail game", g_view_alias.size());
#else
  if (!g_view_alias.empty())
    host::log("mods: %zu files have a retail view: Unranked, Teams and Party play the retail game", g_view_alias.size());
#endif
  // After the aliases: the verdicts only say which entries keep their own number in that view. The
  // alias table itself is left whole, because its being empty or not is what decides the gameplay
  // profile above and how replays are tagged.
  if (!g_view_alias.empty()) compute_pack_skin_verdicts(base_paths, base_fst);
}

// Serves a read inside the overlay range; false when the offset is ordinary disc.
bool read_overlay(uint32_t offset, void* dst, uint32_t size, bool* ok) {
  const auto result = g_mod_overlay.read(offset, dst, size);
  *ok = result == ModOverlay::Read::Success;
  return result != ModOverlay::Read::NotOverridden;
}

// ---- cosmetic packs (costumes, stage and effect visuals) ----
// The same catalog and rules as the static recomp (cosmetic_mods.cpp): the player's selections,
// resolved against this disc's table. Each overridden file moves to its own range past the other
// layers, so a replacement longer than the disc's copy never reads into the next file. Online, and
// in replays, anything not proven visual-only (a costume whose skeleton differs from the vanilla
// slot, a stage that changes more than textures) is served as the disc's copy. Never mod assets:
// cosmetics are local only and do not change what the game or Slippi sees.
struct CosmeticFile { uint32_t start, vanilla_start, length; bool logged; };
std::vector<CosmeticFile> g_cosmetic_files;
// Entry number -> a second entry for the same file that names the disc's copy. While the online rule
// is active, opening an override that is not proven visual-only gives the alias: a different entry
// number, so a copy the game already cached under the override's number is not reused online.
std::unordered_map<int32_t, int32_t> g_cosmetic_alias;
bool native_online_mode();
constexpr uint32_t kCosmeticBase = 0xF0000000u;
// Kept past startup for the character select's L / R skin cycling (republish_cosmetic_slot): the
// table with the lengths published so far, the next free address of the cosmetic range, the path of
// each entry of the disc's table, and the entry that serves each (disc entry, skin) pair, so a skin
// picked again reuses its entry and its range ("" is the disc's own file).
std::vector<uint8_t> g_cosmetic_fst;
uint64_t g_cosmetic_next = kCosmeticBase;
std::vector<std::string> g_raw_paths;
std::map<std::pair<uint32_t, std::string>, int32_t> g_cosmetic_live;

// A new entry for disc entry i's skin, in its own range past kCosmeticBase, and for a skin not
// proven to change looks alone the alias entry that names the disc's copy for online play. -1 when
// the range is full.
int32_t add_cosmetic_entry(uint32_t i, uint32_t vanilla_start, uint32_t length, bool online_allowed) {
  const uint64_t span = ((uint64_t)length + 0xFFFFu) & ~0xFFFFull;
  if (g_cosmetic_next + span > 0xFFFF0000ull) return -1;
  const int32_t entry = (int32_t)g_fst.size();
  g_fst.push_back({(uint32_t)g_cosmetic_next, length, false});
  g_cosmetic_files.push_back({(uint32_t)g_cosmetic_next, vanilla_start, length, false});
  g_cosmetic_next += span;
  if (!online_allowed) {
    const int32_t alias = (int32_t)g_fst.size();
    g_fst.push_back({vanilla_start, be32(g_fst_raw.data() + (size_t)i * 12 + 8), false});
    g_cosmetic_alias[entry] = alias;
  }
  return entry;
}

void load_cosmetics(bool always_vanilla_unsafe) {
  if (g_fst_raw.size() < 12) return;
  if (always_vanilla_unsafe) host::cosmetics::freeze_for_online_session();
  std::vector<uint8_t>& fst = g_cosmetic_fst;
  fst = g_fst_raw;
  host::cosmetics::apply_to_fst(fst.data(), (uint32_t)fst.size());
  const uint32_t count = be32(fst.data() + 8);
  uint64_t& next = g_cosmetic_next;
  g_raw_paths.assign(count, std::string());
  for (const auto& path : g_paths)
    if (path.second > 0 && (uint32_t)path.second < count) g_raw_paths[(size_t)path.second] = path.first;
  for (uint32_t i = 1; i < count && i < g_fst.size(); ++i) {
    const uint8_t* e = fst.data() + i * 12;
    if (e[0]) continue;
    const uint32_t vanilla_start = be32(e + 4), length = be32(e + 8);
    if (host::cosmetics::read(vanilla_start, 0, nullptr, 0) == host::cosmetics::OverrideRead::NotOverridden) continue;
    if (g_fst[i].offset != vanilla_start) {
      // A costume file the loaded pack replaced. Only that pack's own sets may serve it (the 20XX
      // Hack Pack's L / R costumes); they get an entry of their own and entry i stays the pack's
      // file, which is the standard costume of this session.
      const std::string applied = host::cosmetics::applied_asset(vanilla_start);
      if (!host::cosmetics::session_pack_skin(applied) || i >= g_raw_paths.size() || g_raw_paths[i].empty()) {
        host::log("cosmetics: entry %u is replaced by a mod pack; cosmetic selection skipped", i);
        continue;
      }
      const int32_t entry = add_cosmetic_entry(i, vanilla_start, length, host::cosmetics::online_allowed(vanilla_start));
      if (entry < 0) { host::log("cosmetics: selections exceed the cosmetic range; rest skipped"); break; }
      g_cosmetic_live[{i, applied}] = entry;
      g_paths[g_raw_paths[i]] = entry;
      continue;
    }
    const uint64_t span = ((uint64_t)length + 0xFFFFu) & ~0xFFFFull;
    if (next + span > 0xFFFF0000ull) { host::log("cosmetics: selections exceed the cosmetic range; rest skipped"); break; }
    g_cosmetic_files.push_back({(uint32_t)next, vanilla_start, length, false});
    g_fst[i] = {(uint32_t)next, length, false};
    next += span;
    g_cosmetic_live[{i, host::cosmetics::applied_asset(vanilla_start)}] = (int32_t)i;
    if (!host::cosmetics::online_allowed(vanilla_start)) {
      const int32_t alias = (int32_t)g_fst.size();
      g_fst.push_back({vanilla_start, be32(g_fst_raw.data() + i * 12 + 8), false});
      g_cosmetic_alias[(int32_t)i] = alias;
    }
  }
  host::cosmetics::set_online_probe(native_online_mode);
  const auto profile = host::cosmetics::session_profile();
  host::log("cosmetics: %zu disc files overridden (profile %s%s)", g_cosmetic_files.size(),
            profile.fingerprint.empty() ? "vanilla" : profile.fingerprint.c_str(),
            always_vanilla_unsafe ? ", visual-only rule forced" : "");
}

// Serves a read inside the cosmetic range; false when the offset is outside it.
bool read_cosmetic(uint32_t offset, void* dst, uint32_t size, bool* ok) {
  if (offset < kCosmeticBase || g_cosmetic_files.empty()) return false;
  const auto upper = std::upper_bound(g_cosmetic_files.begin(), g_cosmetic_files.end(), offset,
      [](uint32_t address, const CosmeticFile& f) { return address < f.start; });
  if (upper == g_cosmetic_files.begin()) { *ok = false; return true; }
  CosmeticFile& f = *std::prev(upper);
  *ok = host::cosmetics::read(f.vanilla_start, offset - f.start, dst, size) ==
        host::cosmetics::OverrideRead::Success;
  if (!f.logged) {
    f.logged = true;
    host::log("cosmetics: first read of the override at %08X (disc copy %08X)%s", f.start, f.vanilla_start,
              host::cosmetics::session_profile().frozen ? ", online rule active" : "");
  }
  return true;
}

// ---- Slippi system files (--slippi-menus) ----
// The online menus read Slippi's patched menu files (online rows, descriptions, frozen Stadium name,
// connect-code text) and slpCSS.dat. The static recomp gets them over EXI (D1/D2, exi_slippi.cpp);
// the native game asks for them through the ordinary disc path, so they are registered in the
// filesystem table here, built once at boot with the same code. Own offset range, never counted as
// mod assets. Any failure turns the menus off for the run instead of mixing vanilla and patched.
bool g_slippi_menus_requested = true;   // --slippi-menus on|off
bool g_slippi_menus = false;            // requested and the layer loaded
struct SystemFile { std::string path; uint32_t start; std::vector<uint8_t> data; };
std::vector<SystemFile> g_system_files;
// Between the disc (under 1.5 GB) and the mod overlay range (0xA0000000 up to the cosmetics at
// 0xF0000000). It used to sit at 0xC0000000, inside the overlay's range: a pack large enough to
// reach it (the 20XX Hack Pack's 776 files) had its files there refused, and the game waited on them.
constexpr uint32_t kSystemFileBase = 0x90000000u;
const char* const kSystemFileNames[] = {
  "MnMaAll.usd", "MnMaAll.dat", "SdMenu.usd", "SdMenu.dat", "MnSlMap.usd", "MnSlMap.dat",
  "SdSlChr.usd", "SdSlChr.dat", "MnExtAll.usd", "MnExtAll.dat", "slpCSS.dat",
};

uint32_t fnv1a(const uint8_t* p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
  return h;
}

void load_system_files(const char* forced_off_by) {
#ifdef MELEE_NO_SLIPPI
  // This build has no patched menu files to serve: the game reads the disc's own, always.
  (void)forced_off_by;
#else
  if (!g_slippi_menus_requested) { host::log("slippi menus: off (--slippi-menus off)"); return; }
  if (forced_off_by) { host::log("slippi menus: off (%s)", forced_off_by); return; }
  std::vector<SystemFile> files;
  uint32_t next = kSystemFileBase;
  for (const char* name : kSystemFileNames) {
    std::string path = std::string("/") + name;
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
    const bool modded = std::any_of(g_mod_overlay.files().begin(), g_mod_overlay.files().end(),
        [&](const ModOverlay::File& f) { return f.path == path; });
    // Slippi's online menus need Slippi's copy. A pack's own copy (Akaneia's stage select) would
    // offer stages whose code the native game does not have, so Slippi's is used in both views.
    if (modded) host::log("slippi menus: Slippi's %s replaces the pack's copy", name);
    std::vector<uint8_t> data = slippi::system_game_file(name);
    if (data.empty()) { host::log("slippi menus: off (cannot build %s)", name); return; }
    const uint32_t size = (uint32_t)data.size();
    if (uint64_t(next) + size > ModOverlay::base) { host::log("slippi menus: off (system files exceed their range)"); return; }
    files.push_back({path, next, std::move(data)});
    next += (size + 0xFFFFu) & ~0xFFFFu;
  }
  uint32_t replaced = 0, added = 0;
  for (const auto& f : files) {
    auto found = g_paths.find(f.path);
    int32_t entry;
    if (found != g_paths.end()) {
      if (g_fst[found->second].dir) { host::log("slippi menus: %s is a disc directory", f.path.c_str()); continue; }
      entry = found->second; ++replaced;
    } else {
      entry = (int32_t)g_fst.size(); g_fst.push_back({0, 0, false}); g_paths[f.path] = entry; ++added;
    }
    g_fst[entry] = {f.start, (uint32_t)f.data.size(), false};
    g_view_alias.erase(entry);   // the same file in both views
    host::log("slippi menus: %s %u bytes fnv1a %08X", f.path.c_str(), (uint32_t)f.data.size(),
              fnv1a(f.data.data(), f.data.size()));
  }
  g_system_files = std::move(files);
  g_slippi_menus = true;
  host::log("slippi menus: on, %u system files replaced, %u added", replaced, added);
#endif
}

// Serves a read inside the system-file range; false when the offset is outside it.
bool read_system_file(uint32_t offset, void* dst, uint32_t size, bool* ok) {
  if (offset < kSystemFileBase || offset >= ModOverlay::base || g_system_files.empty()) return false;
  for (const auto& f : g_system_files) {
    if (offset < f.start || offset >= f.start + (uint32_t)f.data.size()) continue;
    // The game reads in 32-byte-rounded chunks, so the last read runs a few bytes past the end,
    // into padding on a real disc. Serve that padding as zeros, up to the file's 64 KB slot.
    const uint32_t at = offset - f.start;
    const uint32_t slot = ((uint32_t)f.data.size() + 0xFFFFu) & ~0xFFFFu;
    *ok = size <= slot - at;
    if (*ok) {
      const uint32_t have = std::min<uint32_t>(size, (uint32_t)f.data.size() - at);
      std::memcpy(dst, f.data.data() + at, have);
      std::memset((uint8_t*)dst + have, 0, size - have);
    }
    return true;
  }
  *ok = false;
  return true;
}

// ---- stopping ----
// host::retrace() ends the game by throwing ExitRequested. That must not unwind through the game's
// frames (another compiler's code), so it is caught at the boundary and the process finishes here.
int g_exit_code = 0;
void (*g_shutdown)(int) = nullptr;
void write_replay_recording();
// Online games recorded since the player last searched for an opponent: the game number SendGameInfo
// takes from Slippi's game prep data (InitOnlinePlay counts every non-ranked game; a search resets it).
uint32_t g_online_games = 0;
void log_native_pose_bridge_stats() {
  const auto stats = gx::native_pose_bridge_stats();
  host::log("native pose bridge: %llu snapshots, %llu rigid captured, %llu envelope scopes, %llu envelope captured, %llu invalid; captured default-setup=%llu custom-setup=%llu; rejected kind=%llu joint=%llu depth=%llu flags=%llu constraints=%llu tracks=%llu bytes=%llu format=%llu envelope-data=%llu envelope-limit=%llu bound-animation=%llu",
            (unsigned long long)stats.snapshots,
            (unsigned long long)stats.rigid_captured,
            (unsigned long long)stats.envelope_scopes,
            (unsigned long long)stats.envelope_captured,
            (unsigned long long)stats.invalid_payloads,
            (unsigned long long)stats.captured_default_setup,
            (unsigned long long)stats.captured_custom_setup,
            (unsigned long long)stats.rejected[MU_NATIVE_POSE_REJECT_KIND],
            (unsigned long long)stats.rejected[MU_NATIVE_POSE_REJECT_NO_JOINT],
            (unsigned long long)stats.rejected[MU_NATIVE_POSE_REJECT_CHAIN_LIMIT],
            (unsigned long long)stats.rejected[MU_NATIVE_POSE_REJECT_JOINT_FLAGS],
            (unsigned long long)stats.rejected[MU_NATIVE_POSE_REJECT_CONSTRAINTS],
            (unsigned long long)stats.rejected[MU_NATIVE_POSE_REJECT_TRACK_LIMIT],
            (unsigned long long)stats.rejected[MU_NATIVE_POSE_REJECT_TRACK_BYTES],
            (unsigned long long)stats.rejected[MU_NATIVE_POSE_REJECT_TRACK_FORMAT],
            (unsigned long long)stats.rejected[MU_NATIVE_POSE_REJECT_ENVELOPE_DATA],
            (unsigned long long)stats.rejected[MU_NATIVE_POSE_REJECT_ENVELOPE_LIMIT],
            (unsigned long long)stats.rejected[MU_NATIVE_POSE_REJECT_BOUND_ANIMATION]);
}
template <class F> void guarded(F&& f) {
  try { f(); } catch (const ExitRequested& stop) {
    g_exit_code = stop.code;
    write_replay_recording();
    log_native_pose_bridge_stats();
    if (g_shutdown) g_shutdown(stop.code);
    host::end_process(stop.code);   // everything is saved: do not run the libraries' unload code
  }
}

// ---- the table the game calls ----
void h_log(const char* text) {
  std::string line = text ? text : "";
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
  // MELEE_TRACE_AX_VOICES: the game's sound-request and stream traces also get the timebase, to
  // place each on the AX frame timeline (the game's own lines carry only the retrace).
  static const bool request_tb = std::getenv("MELEE_TRACE_AX_VOICES") != nullptr;
  if (request_tb && (line.rfind("[ax-", 0) == 0 || line.rfind("[audio-start-native]", 0) == 0)) {
    host::log("[game] %s tb=%llu", line.c_str(), (unsigned long long)(host::cpu ? host::cpu->tb : 0));
    return;
  }
  if (!line.empty()) host::log("[game] %s", line.c_str());
}
void h_panic(const char* file, int32_t line, const char* message) {
  host::log("game panic at %s:%d", file ? file : "?", line);
  void* frames[32];
  const USHORT count = CaptureStackBackTrace(0, 32, frames, nullptr);
  for (USHORT i = 0; i < count; ++i) {
    const uintptr_t pc = reinterpret_cast<uintptr_t>(frames[i]);
    if (pc > GAME_IMAGE_BASE && pc < GAME_IMAGE_BASE + host::game_image_size)
      host::log("  stack: melee_game.dll+0x%llX", (unsigned long long)(pc - 1 - GAME_IMAGE_BASE));
  }
  host::die("game stopped at %s:%d: %s", file ? file : "?", line, message ? message : "");
}
uint64_t h_ticks() { return host::cpu->tb; }
// The date the game's calendar works from. It was zero, so every save and every Training Mode CE
// export was dated January 1, 2000.
uint64_t h_boot_time() { return host::console_epoch_ticks(); }

void native_audio_done(void*) {
  if (g_game.ai_dma_done) g_game.ai_dma_done();
}

void native_audio_tick() {
  audio_core::tick(g_audio, host::cpu->tb, host::TB_HZ, native_audio_done, nullptr);
}

void h_poll() {
  // A re-simulated frame never waits on the machine; count it if something does.
  if (g_resim) { ++g_resim_polls; return; }
  guarded([] {
    host::advance_time(2048);   // time flows in busy waits, as it does for the recompiled build
    native_audio_tick();
    if (host::retrace_due()) host::retrace();
    else g_game.fire_alarms(host::cpu->tb);
  });
}

void native_retrace() {
  native_audio_tick();
  g_game.fire_alarms(host::cpu->tb);
  g_game.retrace();
}

void h_gx_fifo(const uint8_t* data, uint32_t size) { guarded([&] { gx::write_fifo_bytes(data, size); }); }
void h_native_render_scope_event(const MuNativeRenderScopeEvent* event) {
  if (!event) return;
  gx::native_render_scope_event({event->sequence, event->scope_id, event->phase, event->reserved});
}
int32_t h_native_pose_capture_enabled() { return gx::native_pose_capture_enabled() ? 1 : 0; }
void h_native_pose_snapshot(const MuNativePoseSnapshot* snapshot) {
  if (!snapshot) return;
  guarded([&] { gx::native_pose_snapshot(snapshot); });
}
uint32_t h_music_volume() { return (uint32_t) slippi::jukebox::user_volume(); }
int32_t h_native_owner_tracking() { return gx::owner_tracking_enabled() ? 1 : 0; }
// Always on: the scope tokens are a few bytes per PObj, and without them a draw is known only by
// its display list and call order, which shifts whenever a menu drops or adds a draw.
int32_t h_native_draw_identity() { return 1; }
// The HUD sliders (stock icon and damage number size), display only, as the backend last set them.
uint32_t h_hud_scales() { return gx::hud_scales_packed(); }
// Display-only options (MU_DISPLAY_OPTION_*), read live by the native game's draw code. Never part
// of a replay or of the match the opponent plays: the viewer's own, as the HUD sizes are.
uint32_t h_display_options() { return gx::low_poly_fighters_active() ? MU_DISPLAY_OPTION_LOW_POLY : 0u; }
void h_hud_player(int32_t slot, int32_t present, int32_t damage, int32_t stocks, float tag_x, float tag_y,
                  int32_t tag_visible) {
  gx::set_native_hud_player(slot, present != 0, damage, stocks, tag_x, tag_y, tag_visible != 0);
}
// Replay playback: the options the replay was recorded with ("muOptions"), set by set_replay.
bool g_replaying = false;
uint32_t g_replay_feature_options = 0;
uint32_t g_replay_feature_options2 = 0;
uint32_t g_replay_feature_options3 = 0;
// "20XX CPUs" (Game tab) rides in word 2 as MU_GAME_OPTION2_TE_20XX_CPUS but is a plain option, not a
// 20XX TE feature: it goes to the game with or without a TE save, and the game gates it itself
// (mu_option2_offline: offline only, never in a TM-CE event).
uint32_t cpu_20xx_bit() { return gx::RenderOptions::live_cpu_20xx() ? MU_GAME_OPTION2_TE_20XX_CPUS : 0u; }
// The 20XX Hack Pack's debug menu (shim/mu_hp_menu.c) switches the 20XX TE features the pack also
// has, without a TE save. Then only these bits go to the game, never TE itself (MU_GAME_OPTION_TE):
// the game accepts them with the pack loaded (mu_te.c), and TE's menus and rules stay off.
constexpr uint32_t kHpTeOptions = MU_GAME_OPTION_TE_NO_STAR_KO | MU_GAME_OPTION_TE_TAUNT_CANCEL |
                                  MU_GAME_OPTION_TE_FIXED_CAMERA;
constexpr uint32_t kHpTeOptions2 = MU_GAME_OPTION2_TE_NO_SCREEN_RUMBLE | MU_GAME_OPTION2_TE_LCANCEL_FLASH |
                                   0x00780000u |   // the flash choices (mu_lcancel_flash.h)
                                   MU_GAME_OPTION2_TE_BUBBLES | MU_GAME_OPTION2_TE_INPUT_DISPLAY |
                                   MU_GAME_OPTION2_TE_COLOR_OVERLAYS;
bool hp_te_words() { return !mods::status().te_owned && hackpack::loaded(); }
uint32_t te_words_mask1() { return mods::status().te_owned ? 0x00007FF0u : hp_te_words() ? kHpTeOptions : 0u; }
uint32_t te_words_mask2() { return mods::status().te_owned ? 0xFFFFFFFFu : hp_te_words() ? kHpTeOptions2 : 0u; }
uint32_t h_game_options2() {
  if (g_replaying) return g_replay_feature_options2;
  return (gx::RenderOptions::live_te_options2() & te_words_mask2()) | cpu_20xx_bit();
}
// The 20XX Hack Pack training options for CPUs (Game tab), word 3: plain options, the game gates
// them itself (mu_options3_offline: offline only, never in a TM-CE event).
// The upper bits are the 20XX Hack Pack stage of the match (hackpack_source.h): the game reports
// them when a match is picked and they ride in the same word, so a replay records and replays them.
uint32_t h_game_options3() {
  if (g_replaying) return g_replay_feature_options3;
  return (gx::RenderOptions::live_cpu_training() & MU_GAME_OPTION3_CPU_ALL) | hackpack::stage_bits();
}
uint32_t h_game_options() {
  return (gecko::option_no_screen_shake ? MU_GAME_OPTION_NO_SCREEN_SHAKE : 0u) |
         (gecko::option_pal_stock_icons ? MU_GAME_OPTION_PAL_STOCK_ICONS : 0u) |
         (gecko::option_widescreen ? MU_GAME_OPTION_WIDESCREEN : 0u) |   // the viewer's own, also in replays
         (gx::true_widescreen_active() ? MU_GAME_OPTION_TRUE_WIDESCREEN : 0u) |   // likewise: display only
         (((uint32_t)host::g_game_language.load(std::memory_order_relaxed) << MU_GAME_OPTION_LANGUAGE_SHIFT) &
          MU_GAME_OPTION_LANGUAGE_MASK) |   // the player's language choice: text, names and voices only
         (host::options.vanilla_game ? MU_GAME_OPTION_VANILLA : 0u) |
         (g_slippi_menus ? MU_GAME_OPT_SLIPPI_MENUS : 0u) |
         (mods::status().tmce && !g_replaying ? MU_GAME_OPTION_TMCE : 0u) |
         (g_replaying ? g_replay_feature_options
                   : gx::RenderOptions::live_te_options() & te_words_mask1());
  // (Only 20XX TE's own bits, and only with its save, or the Hack Pack's share of them with the pack.
  // The Training Lab bits above 0x7FF0 are not offered any more: Training Mode CE replaces it.)
}
// Match conveniences implemented by the host follow the same gates as mu_te2 in the game.
bool te_host_feature(uint32_t feature) {
  const uint32_t options = h_game_options();
  // Without TE itself, only the Hack Pack's share (its menu), with the pack or in a replay of it.
  const bool hp = (feature & ~kHpTeOptions2) == 0 && (hp_te_words() || g_replaying);
  if ((!(options & MU_GAME_OPTION_TE) && !hp) || (options & MU_GAME_OPTION_VANILLA) ||
      !(h_game_options2() & feature) || slippi::online::session_mode() >= 0) return false;
  if ((options & MU_GAME_OPTION_TE_TOURNAMENT) &&
      !(feature & MU_GAME_OPTION2_TE_TOURNAMENT_SAFE)) return false;
  uint32_t major = 0, minor = 0, match_frame = 0;
  host::current_scene(&major, &minor, &match_frame);
  return !(mods::status().tmce && major == 0x2B);
}
uint32_t read_be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
void append_be32(std::vector<uint8_t>& q, uint32_t v) { q.push_back((uint8_t)(v >> 24)); q.push_back((uint8_t)(v >> 16)); q.push_back((uint8_t)(v >> 8)); q.push_back((uint8_t)v); }

void log_savestate_stats(const char* when) {
  const auto& s = g_savestates.stats();
  host::log("online: snapshots %s: %llu captures (avg %.2f ms, max %.2f), %llu loads (avg %.2f ms, max %.2f), "
            "%llu missing; %llu re-simulations, %llu polls and %llu VI waits inside them",
            when, (unsigned long long)s.captures, s.captures ? s.capture_ms_total / s.captures : 0.0,
            s.capture_ms_max, (unsigned long long)s.loads, s.loads ? s.load_ms_total / s.loads : 0.0,
            s.load_ms_max, (unsigned long long)s.missing_loads, (unsigned long long)g_resim_phases,
            (unsigned long long)g_resim_polls, (unsigned long long)g_resim_vi_waits);
}

void mem1_watch_begin(const std::vector<MuStateRegion>& ranges);

// The snapshot of an online match: the game's capture ranges (all of main memory and the image's
// writable sections) minus its exclusions. They are fixed within a match, so the spans are computed
// once, at online frame 1.
void begin_native_savestates(int slots = slippi::ROLLBACK_MAX_FRAMES) {   // a replay viewer keeps more (replay_keep)
  std::vector<MuStateRegion> ranges(g_game.snapshot_ranges(nullptr, 0));
  g_game.snapshot_ranges(ranges.data(), (uint32_t)ranges.size());
  std::vector<MuStateRegion> exclusions(g_game.state_exclusions(nullptr, 0));
  g_game.state_exclusions(exclusions.data(), (uint32_t)exclusions.size());
  // The sound driver's whole state (every variable of the AX files, synth and axdriver), from the
  // ranges the build wrote beside the game library: a console savestate leaves all of it out, and
  // restoring its voice tables while the voices play on cuts and restarts sounds on every rollback.
  {
    char dll_path[MAX_PATH] = {};
    HMODULE module = GetModuleHandleA("melee_game.dll");
    if (module && GetModuleFileNameA(module, dll_path, MAX_PATH)) {
      std::string path(dll_path);
      path = path.substr(0, path.size() - 4) + ".snapexcl";
      std::ifstream in(path);
      // The offsets are right only for the library they were read from: a file from another build
      // (a library copied without it) left the wrong memory out and restored the sound driver's on
      // every rollback, which crashed within seconds. Its "# dll_size" line must match this file.
      WIN32_FILE_ATTRIBUTE_DATA dll_info{};
      const unsigned long long dll_size = GetFileAttributesExA(dll_path, GetFileExInfoStandard, &dll_info)
          ? ((unsigned long long)dll_info.nFileSizeHigh << 32 | dll_info.nFileSizeLow) : 0;
      std::vector<MuStateRegion> sound;
      std::string line;
      size_t bytes = 0;
      bool other_build = false;
      while (std::getline(in, line)) {
        unsigned long long value = 0;
        if (std::sscanf(line.c_str(), "# dll_size %llu", &value) == 1) { other_build = value != dll_size; continue; }
        unsigned long long offset = 0, size = 0;
        if (std::sscanf(line.c_str(), "%llx %llx", &offset, &size) == 2 && size) {
          sound.push_back(MuStateRegion{(void*)((uintptr_t)module + offset), (uint32_t)size});
          bytes += size;
        }
      }
      if (other_build)
        host::log("online: WARNING: %s belongs to another build of melee_game.dll; sound driver state is inside snapshots", path.c_str());
      else if (sound.empty())
        host::log("online: WARNING: %s missing; sound driver state is inside snapshots", path.c_str());
      else {
        exclusions.insert(exclusions.end(), sound.begin(), sound.end());
        host::log("online: sound driver state left out of snapshots: %zu ranges, %zu bytes", sound.size(), bytes);
      }
    }
  }
  // Diagnostic (MELEE_SNAPSHOT_EXCLUDE=lo-hi[,lo-hi...], hex): leave extra ranges out, to find
  // which memory a rollback must not restore.
  if (const char* extra = std::getenv("MELEE_SNAPSHOT_EXCLUDE")) {
    for (const char* p = extra; *p;) {
      char* end = nullptr;
      const unsigned long long lo = std::strtoull(p, &end, 16);
      if (!end || *end != '-') break;
      const unsigned long long hi = std::strtoull(end + 1, &end, 16);
      if (hi > lo) {
        exclusions.push_back(MuStateRegion{(void*)(uintptr_t)lo, (uint32_t)(hi - lo)});
        host::log("online: snapshot test exclusion %08llX-%08llX", lo, hi);
      }
      p = end;
      while (*p == ',') ++p;
    }
  }
  for (const auto& r : ranges)
    host::log("online: snapshot range %08llX +%X", (unsigned long long)(uintptr_t)r.address, r.size);
  auto spans = native_savestate::subtract(ranges, exclusions);
  g_savestates.begin(std::move(spans), slots);
  mem1_watch_begin(ranges);
  host::log("online: snapshots cover %zu bytes in %zu spans (first range %u bytes, %zu exclusions)",
            g_savestates.bytes(), g_savestates.spans().size(), ranges.empty() ? 0u : ranges[0].size,
            exclusions.size());
}

// Diagnostic, off unless MELEE_ONLINE_WATCH_MEM1=1: hashes MEM1 outside the snapshot in 64 KB
// chunks at online frame 1 and every 600 captures after, and logs the ranges that changed. Game
// state that changes during a match must be inside the snapshot, or a rollback cannot restore it.
struct Mem1Watch {
  bool on = false;
  std::vector<std::pair<uintptr_t, uintptr_t>> ranges;   // [start, end) outside the snapshot
  std::vector<uint64_t> hashes;
  std::vector<std::vector<uint8_t>> bytes;               // each range as last seen
};
Mem1Watch g_mem1_watch;

bool mem1_watch_enabled() {
  static const bool enabled = [] { const char* v = std::getenv("MELEE_ONLINE_WATCH_MEM1"); return v && v[0] == '1'; }();
  return enabled;
}

// Diagnostic for the MEM1 watch: where each disc read landed, so a changed byte outside the
// snapshot can be named by the file loaded there.
struct DiscLoad { uintptr_t dst; uint32_t size, offset; };
std::vector<DiscLoad> g_disc_loads;

void note_disc_load(void* dst, uint32_t size, uint32_t offset) {
  if (!mem1_watch_enabled()) return;
  const uintptr_t a = (uintptr_t)dst;
  if (a < MEM1_BASE || a >= MEM1_BASE + MEM1_SIZE) return;
  g_disc_loads.push_back({a, size, offset});
}

std::string disc_file_at(uintptr_t address) {
  for (auto it = g_disc_loads.rbegin(); it != g_disc_loads.rend(); ++it) {
    if (address < it->dst || address >= it->dst + it->size) continue;
    const uint32_t disc = it->offset + (uint32_t)(address - it->dst);
    for (const auto& kv : g_paths) {
      const auto& f = g_fst[kv.second];
      if (!f.dir && disc >= f.offset && disc < f.offset + f.length) {
        char buf[64];
        std::snprintf(buf, sizeof buf, " +%X", disc - f.offset);
        return kv.first + buf;
      }
    }
    return "unnamed disc data";
  }
  return "no disc read";
}

uint64_t hash_chunk(const uint8_t* p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i + 8 <= n; i += 8) { uint64_t w; std::memcpy(&w, p + i, 8); h = (h ^ w) * 1099511628211ull; }
  return h;
}

void mem1_watch_begin(const std::vector<MuStateRegion>& ranges) {
  g_mem1_watch = Mem1Watch{};
  if (!mem1_watch_enabled()) return;
  g_mem1_watch.on = true;
  std::vector<MuStateRegion> inside;
  for (const auto& r : ranges)
    if ((uintptr_t)r.address >= MEM1_BASE && (uintptr_t)r.address < MEM1_BASE + MEM1_SIZE) inside.push_back(r);
  auto outside = native_savestate::subtract({MuStateRegion{(void*)MEM1_BASE, MEM1_SIZE}}, inside);
  for (const auto& s : outside) {
    for (uintptr_t a = (uintptr_t)s.address; a < (uintptr_t)s.address + s.size; a += 0x10000) {
      const uintptr_t e = std::min<uintptr_t>(a + 0x10000, (uintptr_t)s.address + s.size);
      g_mem1_watch.ranges.emplace_back(a, e);
      g_mem1_watch.hashes.push_back(hash_chunk((const uint8_t*)a, e - a));
      g_mem1_watch.bytes.emplace_back((const uint8_t*)a, (const uint8_t*)e);
    }
  }
  host::log("online: watching %zu MEM1 chunks outside the snapshot", g_mem1_watch.ranges.size());
}

void mem1_watch_check() {
  if (!g_mem1_watch.on) return;
  uintptr_t run_start = 0, run_end = 0;
  size_t changed = 0;
  auto flush = [&] {
    if (run_end > run_start) host::log("online: MEM1 outside the snapshot changed: %08llX-%08llX",
                                       (unsigned long long)run_start, (unsigned long long)run_end);
    run_start = run_end = 0;
  };
  for (size_t i = 0; i < g_mem1_watch.ranges.size(); ++i) {
    const auto& r = g_mem1_watch.ranges[i];
    const uint64_t h = hash_chunk((const uint8_t*)r.first, r.second - r.first);
    if (h == g_mem1_watch.hashes[i]) continue;
    g_mem1_watch.hashes[i] = h;
    ++changed;
    {
      // Byte ranges that changed in this chunk (merged within 16 bytes), with the file loaded there.
      auto& old = g_mem1_watch.bytes[i];
      const uint8_t* now = (const uint8_t*)r.first;
      int shown = 0;
      for (size_t k = 0; k < old.size() && shown < 12; ++k) {
        if (old[k] == now[k]) continue;
        size_t end = k + 1, last = k;
        while (end < old.size() && end <= last + 16) { if (old[end] != now[end]) last = end; ++end; }
        char was[40] = {}, is[40] = {};
        for (size_t b = 0; b < 16 && k + b <= last; ++b) {
          std::snprintf(was + 2 * b, 3, "%02X", old[k + b]);
          std::snprintf(is + 2 * b, 3, "%02X", now[k + b]);
        }
        host::log("online: MEM1 outside the snapshot: %08llX +%zu was %s now %s (%s)",
                  (unsigned long long)(r.first + k), last - k + 1, was, is, disc_file_at(r.first + k).c_str());
        ++shown;
        k = last;
      }
      std::memcpy(old.data(), now, old.size());
    }
    if (run_end == r.first) run_end = r.second;
    else { flush(); run_start = r.first; run_end = r.second; }
  }
  flush();
  host::log("online: MEM1 watch: %zu of %zu chunks outside the snapshot changed", changed, g_mem1_watch.ranges.size());
}

// Slippi's savestate commands, served natively (Slippi preserves its bookkeeping with a preserve
// list; the native game excludes it instead, so the list is always empty).
void native_capture(int32_t frame) {
  if (!g_savestates.active()) begin_native_savestates();
  g_savestates.capture(frame);
  if (g_savestates.stats().captures % 60 == 0) mem1_watch_check();
  if (g_savestates.stats().captures % 1000 == 0) log_savestate_stats("so far");
}
void native_load(int32_t frame, const uint8_t* payload) {
  if (read_be32(payload + 4) != 0) host::log("online: ignoring a savestate preserve list (native state is excluded)");
  if (!g_savestates.load(frame)) {
    host::log("online: no snapshot kept for frame %d", frame);
    return;
  }
  // The frame the game finishes next continues from this older state; the renderer must not blend
  // across the jump.
  gx::mark_discontinuity();
  slippi::online::note_rollback(frame);
  if (g_savestates.stats().loads % 200 == 0) log_savestate_stats("so far");
}

// Determinism self-test (MELEE_ONLINE_SELFTEST, game side in mu_online.c): the game keeps its state
// at the top of a frame, rolls back a few frames, re-simulates them with the same inputs and asks
// for a comparison at the same point. Any difference is non-determinism in the rollback path.
constexpr uint8_t CMD_SELFTEST_KEEP = 0xF0, CMD_SELFTEST_COMPARE = 0xF1;   // native test only

// Training Lab savestates (shim/mu_lab.c): one kept state on the rollback snapshot engine, under a
// frame number no online match uses. The engine frees its slots on a load (rollback semantics), so
// a lab load keeps the state again at once and it can be returned to as often as wanted.
constexpr uint8_t CMD_LAB_SAVE = 0xF2, CMD_LAB_LOAD = 0xF3, CMD_LAB_ADVANTAGE = 0xF4, CMD_CONTENT_MODE = 0xF5;
void apply_content_mode(int mode);
constexpr int32_t kLabFrame = -1000000;
// The replay viewer's controls (shim/mu_replay.c mu_replay_frame_begin, host/replay_control.h):
// payload s32 next replay frame (BE), u8 0 ask / 1 restore now. Defined with the replay host below.
constexpr uint8_t CMD_REPLAY_GATE = 0xFA;
uint8_t replay_gate(int32_t frame, uint8_t phase);
void lab_save() {
  if (!g_savestates.active()) begin_native_savestates();
  g_savestates.capture(kLabFrame);
  host::log("lab: state saved (%.2f ms)", g_savestates.stats().capture_ms_max);
}
void lab_load() {
  if (!g_savestates.active() || !g_savestates.load(kLabFrame)) {
    host::log("lab: no saved state");
    return;
  }
  g_savestates.capture(kLabFrame);
  gx::mark_discontinuity();
  host::log("lab: state loaded");
}
uint64_t g_selftest_checks = 0, g_selftest_failures = 0;

void report_selftest(int32_t frame, std::vector<uint8_t>& reply) {
  reply.assign(4, 0);
  ++g_selftest_checks;
  if (!g_savestates.has_reference(frame)) {
    host::log("online selftest: no reference kept for frame %d", frame);
    return;
  }
  const auto diffs = g_savestates.compare_reference();
  if (diffs.empty()) {
    if (g_selftest_checks % 20 == 0)
      host::log("online selftest: %llu checks, %llu with differences", (unsigned long long)g_selftest_checks,
                (unsigned long long)g_selftest_failures);
    return;
  }
  ++g_selftest_failures;
  size_t bytes = 0;
  for (const auto& d : diffs) bytes += d.second;
  host::log("online selftest: frame %d differs after re-simulation: %zu ranges, %zu bytes", frame, diffs.size(), bytes);
  // Reply: count, then (address, length) pairs, big-endian; the game names what lives there.
  const uint32_t listed = (uint32_t)std::min<size_t>(diffs.size(), 480);
  reply.clear();
  append_be32(reply, listed);
  for (uint32_t i = 0; i < listed; ++i) {
    append_be32(reply, (uint32_t)diffs[i].first);
    append_be32(reply, diffs[i].second);
  }
  for (size_t i = 0; i < diffs.size() && i < 24; ++i) {
    const uint8_t* ref = g_savestates.reference_bytes_at(diffs[i].first);
    const uint8_t* now = (const uint8_t*)diffs[i].first;
    char was[40] = {}, is[40] = {};
    const uint32_t n = std::min<uint32_t>(diffs[i].second, 16);
    for (uint32_t k = 0; k < n && ref; ++k) {
      std::snprintf(was + 2 * k, 3, "%02X", ref[k]);
      std::snprintf(is + 2 * k, 3, "%02X", now[k]);
    }
    host::log("online selftest:   %08llX +%u: was %s now %s", (unsigned long long)diffs[i].first, diffs[i].second, was, is);
  }
}

// ---- character select L / R skin cycling (mn/mncharsel.c, shim/mu_content.c) ----
// payload: port, character select fighter number, costume, direction (1 next, 0 previous).
// reply: 1 when the slot's skin changed (0 otherwise), then the choice's name for the door label.
constexpr uint8_t CMD_SKIN_CYCLE = 0xF9;

// The slot's disc files after the catalog published it again: each one gets the entry that serves
// it now, and the path opens that entry. A skin is a new entry number in its own range past
// kCosmeticBase, so nothing the game preloaded under the old number is reused; a skin not proven to
// change looks alone also gets the alias entry that names the disc's copy for online play, as at
// startup. False when a file could not be given an entry (the caller puts the old pick back).
bool republish_cosmetic_slot(const std::string& slot) {
  const auto result = host::cosmetics::republish_slot(g_cosmetic_fst.data(), (uint32_t)g_cosmetic_fst.size(), slot);
  if (!result.ok) return false;
  bool ok = true;
  for (const auto& file : result.files) {
    const uint32_t i = file.fst_index;
    if (i >= g_raw_paths.size() || g_raw_paths[i].empty() || (size_t)i * 12 + 12 > g_fst_raw.size()) continue;
    // A file the loaded pack replaced: entry i is the pack's file, the standard costume of this
    // session, and only the pack's own sets may serve it instead (load_cosmetics).
    const bool pack_replaced = g_fst[i].offset != file.vanilla_start && g_fst[i].offset < kCosmeticBase;
    if (pack_replaced && (!file.overridden || !host::cosmetics::session_pack_skin(file.asset_id))) {
      if (file.overridden) host::log("cosmetics: entry %u is replaced by a mod pack; cosmetic selection skipped", i);
      g_paths[g_raw_paths[i]] = (int32_t)i;
      continue;
    }
    const uint32_t vanilla_length = be32(g_fst_raw.data() + (size_t)i * 12 + 8);
    const std::pair<uint32_t, std::string> key{i, file.overridden ? file.asset_id : std::string()};
    int32_t entry;
    const auto known = g_cosmetic_live.find(key);
    if (known != g_cosmetic_live.end()) entry = known->second;
    else if (!file.overridden && g_fst[i].offset == file.vanilla_start) entry = (int32_t)i;
    else if (!file.overridden) {
      entry = (int32_t)g_fst.size();
      g_fst.push_back({file.vanilla_start, vanilla_length, false});
    } else {
      entry = add_cosmetic_entry(i, file.vanilla_start, file.length, file.online_allowed);
      if (entry < 0) {
        host::log("cosmetics: selections exceed the cosmetic range; %s keeps its previous skin", slot.c_str());
        ok = false;
        continue;
      }
    }
    g_cosmetic_live[key] = entry;
    g_paths[g_raw_paths[i]] = entry;
  }
  return ok;
}

void cycle_costume_skin(int character, int costume, int direction, std::vector<uint8_t>& reply) {
  reply.assign(1, 0);
  if (g_replaying || g_cosmetic_fst.empty()) return;
  const std::string slot = host::cosmetics::costume_slot_file(character, costume);
  if (slot.empty()) return;
  const auto pick = host::cosmetics::cycle_slot_live(slot, direction);
  if (!pick.ok || !pick.changed) {
    if (!pick.message.empty()) host::log("cosmetics: %s skin not changed (%s)", slot.c_str(), pick.message.c_str());
    return;
  }
  // The Ice Climbers: Nana's slot changed with Popo's (pick.partner_slot), and is published the same
  // way. A slot that cannot be given an entry gets its previous pick back; so does Nana's when
  // Popo's failed, so the pair never ends half changed by a failure.
  const auto put_back = [](const std::string& which, const std::string& previous_id) {
    std::string error;
    host::cosmetics::select_variant_live(which, previous_id, &error);
    republish_cosmetic_slot(which);
  };
  if (!republish_cosmetic_slot(slot)) {
    put_back(slot, pick.previous_id);
    if (!pick.partner_slot.empty()) put_back(pick.partner_slot, pick.partner_previous_id);
    return;
  }
  if (!pick.partner_slot.empty()) {
    if (republish_cosmetic_slot(pick.partner_slot))
      host::log("cosmetics: character select pairs %s with %s", pick.partner_slot.c_str(), slot.c_str());
    else put_back(pick.partner_slot, pick.partner_previous_id);
  }
  // The pictures the new choice brings (its portrait and stock icon), for the renderer's next frame.
  std::vector<gx::texpack::CosmeticCompanion> companions;
  for (const auto& item : host::cosmetics::active_companions())
    companions.push_back({item.kind, item.target_path, item.path});
  gx::texpack::refresh_cosmetic_companions(std::move(companions));
  host::log("cosmetics: character select picked %s for %s", pick.name.c_str(), slot.c_str());
  reply[0] = 1;
  reply.insert(reply.end(), pick.name.begin(), pick.name.begin() + (std::ptrdiff_t)std::min<size_t>(pick.name.size(), 63));
}

// ---- voice mods: the fighter banks of the match about to load (lb/lbaudio_ax.c, docs/voice-mods.md) ----
// payload: fighter and costume for each of the four ports (fighter 0xFF: nobody).
// reply: a count, then the game's bank number of each bank whose served content changed.
constexpr uint8_t CMD_VOICE_MATCH = 0xFA;
void plan_voice_banks(const uint8_t* p, std::vector<uint8_t>& reply) {
  reply.assign(1, 0);
  if (!host::cosmetics::voice_bank_count()) return;
  host::cosmetics::MatchFighter ports[4];
  for (int i = 0; i < 4; ++i) {
    if (p[2 * i] == 0xFF) continue;
    ports[i].character = p[2 * i];
    ports[i].costume = p[2 * i + 1];
  }
  for (const auto& bank : host::cosmetics::plan_match_voices(ports).changed) {
    if (bank.bank < 0 || bank.bank > 0xFF || reply.size() >= 32) continue;
    reply.push_back((uint8_t)bank.bank);
    ++reply[0];
  }
}

int32_t h_slippi_command(uint8_t command, const uint8_t* payload, uint32_t payload_size,
                         uint8_t* response, uint32_t response_capacity, uint32_t* response_size) {
  return slippi::online::native_command(
      command, payload, payload_size, response, response_capacity, response_size,
      MU_SLIPPI_RESPONSE_CAPACITY, true,
      [](uint8_t c, const uint8_t* p, uint32_t n, std::vector<uint8_t>& reply) {
        // Music: the game hands its songs to the jukebox as the static recomp's Slippi codes do
        // (StartSong D6, Stop D7, VolumeChange D8; shim/mu_audio.c).
        if (c == 0xD6 && n == 8) {
          // 20XX TE "Reduce Dream Land volume" (offline): its song 90/127 as loud, as TE plays it.
          const uint32_t offset = read_be32(p);
          if (te_host_feature(MU_GAME_OPTION2_TE_DL64_QUIET)) {
            const auto dl64 = g_paths.find("/audio/old_kb.hps");
            if (dl64 != g_paths.end() && g_fst[dl64->second].offset == offset)
              slippi::jukebox::set_next_song_gain(90.0f / 127.0f);
          }
          slippi::jukebox::start_song(offset, read_be32(p + 4));
          return true;
        }
        if (c == 0xD7) { slippi::jukebox::stop(); return true; }
        if (c == 0xD8 && n == 1) { slippi::jukebox::set_melee_volume(p[0]); return true; }
        if (c == slippi::online::CMD_CAPTURE_SAVESTATE) { native_capture((int32_t)read_be32(p)); return true; }
        if (c == slippi::online::CMD_FIND_OPPONENT) g_online_games = 0;   // replay game numbers restart
        if (c == slippi::online::CMD_LOAD_SAVESTATE) { native_load((int32_t)read_be32(p), p); return true; }
        if (c == CMD_SELFTEST_KEEP && n == 4) { g_savestates.keep_reference((int32_t)read_be32(p)); return true; }
        if (c == CMD_LAB_SAVE) { lab_save(); return true; }
        if (c == CMD_LAB_LOAD) { lab_load(); return true; }
        if (c == CMD_REPLAY_GATE && n == 5) { reply.push_back(replay_gate((int32_t)read_be32(p), p[4])); return true; }
        if (c == CMD_LAB_ADVANTAGE && n == 5) { training_overlay::set_advantage((int32_t)read_be32(p), p[4]); return true; }
        if (c == CMD_SKIN_CYCLE && n == 4) { cycle_costume_skin(p[1], p[2], p[3] ? 1 : -1, reply); return true; }
        if (c == CMD_VOICE_MATCH && n == 8) { plan_voice_banks(p, reply); return true; }
        if (c == 0xF6 && n == 1) {
          // 20XX TE switches from inside the game (stage select Y: Frozen Mode), for this session.
          if (p[0] == 0) {
            gx::RenderOptions::live_te_options() ^= MU_GAME_OPTION_TE_FROZEN_STAGES;
            host::log("20xx: Frozen Mode %s", (gx::RenderOptions::live_te_options() & MU_GAME_OPTION_TE_FROZEN_STAGES) ? "on" : "off");
          }
          reply.push_back((gx::RenderOptions::live_te_options() & MU_GAME_OPTION_TE_FROZEN_STAGES) ? 1 : 0);
          return true;
        }
        if (c == MU_HP_COMMAND && n >= 1 && p[0] == MU_HP_OP_CPU_WORD) {
          // The Hack Pack's menu (shim/mu_hp_menu.c): the CPU training options, the same store as
          // the Game tab's rows (option word 3, saved by the settings panel). Empty without the pack.
          if (!hackpack::loaded()) return true;
          if (n == 5 && slippi::online::session_mode() < 0 && !g_replaying) {
            gx::RenderOptions::live_cpu_training() = read_be32(p + 1) & MU_GAME_OPTION3_CPU_ALL;
            gx::RenderOptions::live_cpu_game_changes().fetch_add(1);
            host::log("20xx-hp: CPU training options from the pack's menu %08X", gx::RenderOptions::live_cpu_training());
          }
          append_be32(reply, gx::RenderOptions::live_cpu_training() & MU_GAME_OPTION3_CPU_ALL);
          return true;
        }
        if (c == MU_HP_COMMAND) {
          // 20XX Hack Pack data and choices for the game's native rewrites (hackpack_source.h).
          // Choices and the stage state change only offline, outside replay playback.
          hackpack::command(p, n, reply, slippi::online::session_mode() < 0 && !g_replaying);
          return true;
        }
        if (c == 0xF7 && n == 1) {
          // 20XX TE menu song from Sound Test (0x80 asks): TE's value, 0 none, -1 song 0.
          if (p[0] != 0x80) {
            gx::RenderOptions::live_te_menu_music().store((int8_t)p[0]);
            host::log("20xx: menu music %d", (int8_t)p[0]);
          }
          else if (gx::RenderOptions::live_te_menu_music().load() != 0) {
            host::log("20xx: menu music %d (chosen in Sound Test)", gx::RenderOptions::live_te_menu_music().load());
          }
          reply.push_back((uint8_t)(int8_t)gx::RenderOptions::live_te_menu_music().load());
          return true;
        }
        if (c == 0xF8 && (n == 0 || n == 8)) {
          // 20XX TE's in-game settings menu (shim/mu_te_debugmenu.c): both feature words, big-endian,
          // the same bits as the F1 panel. Empty asks for the current ones. With "Lock settings" on,
          // only switching the lock off goes through.
          // The 20XX Hack Pack's menu (shim/mu_hp_menu.c) uses the same command without a TE save:
          // then only its share of the bits changes (kHpTeOptions, kHpTeOptions2) and TE's lock,
          // which that menu does not show, does not apply.
          auto& w1 = gx::RenderOptions::live_te_options();
          auto& w2 = gx::RenderOptions::live_te_options2();
          if (n == 8 && hp_te_words() && slippi::online::session_mode() < 0 && !g_replaying) {
            const uint32_t next1 = read_be32(p), next2 = read_be32(p + 4);
            w1 = (w1 & ~kHpTeOptions) | (next1 & kHpTeOptions);
            w2 = (w2 & ~kHpTeOptions2) | (next2 & kHpTeOptions2);
            if (w2 & MU_GAME_OPTION2_TE_LCANCEL_FLASH) lcancel::set_indicator(false);   // one flash at a time
            gx::RenderOptions::live_te_game_changes().fetch_add(1);
            host::log("20xx-hp: settings from the pack's menu %08X %08X", w1 & kHpTeOptions, w2 & kHpTeOptions2);
          } else if (n == 8 && (h_game_options() & MU_GAME_OPTION_TE) && slippi::online::session_mode() < 0 && !g_replaying) {
            const uint32_t next1 = read_be32(p), next2 = read_be32(p + 4);
            if (w2 & MU_GAME_OPTION2_TE_LOCK_SETTINGS) {
              if (!(next2 & MU_GAME_OPTION2_TE_LOCK_SETTINGS)) w2 &= ~MU_GAME_OPTION2_TE_LOCK_SETTINGS;
            } else {
              w1 = (w1 & ~0x7FE0u) | (next1 & 0x7FE0u);   // TE itself (0x10) stays on
              w2 = next2 & ~MU_GAME_OPTION2_TE_LCANCEL_WHEELS;   // the port's Auto L-cancel does that
              if (w2 & MU_GAME_OPTION2_TE_LCANCEL_FLASH) lcancel::set_indicator(false);   // one flash at a time
            }
            gx::RenderOptions::live_te_game_changes().fetch_add(1);
            host::log("20xx: settings from the game menu %08X %08X", w1, w2);
          }
          append_be32(reply, w1);
          append_be32(reply, w2);
          return true;
        }
        if (c == CMD_CONTENT_MODE && n == 1) {
          if (p[0] != 0xFE) apply_content_mode(p[0] == 0xFF ? -1 : (int)p[0]);   // 0xFE: query only
          reply.push_back(g_retail_view ? 1 : 0);
          return true;
        }
        if (c == CMD_SELFTEST_COMPARE && n == 4) { report_selftest((int32_t)read_be32(p), reply); return true; }
        if (c == slippi::online::CMD_ONLINE_INPUTS) {
          // A new match starts at pad frame 1. The game resends frame 1 while it waits for the
          // opponent, so the snapshot pool (7 x all of main memory) is set up on the first one only.
          static int32_t last_pad_frame = 0;
          const int32_t pad_frame = (int32_t)read_be32(p);
          if (pad_frame == 1 && last_pad_frame != 1) {
            if (g_savestates.active()) log_savestate_stats("previous match");
            begin_native_savestates();
          }
          last_pad_frame = pad_frame;
        }
        return slippi::online::handle(c, p, n, reply);
      });
}

// The view for the online mode the game reports (0-4), or offline (-1): offline and Direct with the
// mod on show the mod; every other online mode shows the retail game. The online rules and the
// build the opponent is told follow it.
slippi::online::LocalBuild content_build_for_mode(int mode) {
  const bool mods = !g_view_alias.empty();
  const bool retail = mods && mode >= 0 && !(mode == 2 && gx::RenderOptions::live_mods_in_direct());
  slippi::online::LocalBuild build;
  build.mod_view = mods && !retail;
  build.fingerprint = g_mod_fingerprint;
  build.name = g_mod_display_name;
  build.allow_unverified = gx::RenderOptions::live_mods_dolphin_ok();
  return build;
}

void apply_content_mode(int mode) {
  const bool mods = !g_view_alias.empty();
  bool retail = false;
  if (mods && mode >= 0) retail = !(mode == 2 && gx::RenderOptions::live_mods_in_direct());
  if (retail != g_retail_view)
    host::log("content: %s view (%s)", retail ? "retail" : "mod",
              mode < 0 ? "offline" : mode == 2 ? "Direct" : "online mode that plays the retail game");
  g_retail_view = retail;
  mods::status().retail_view = retail;   // the panel's notice counts the pack skins swapped in this view
  slippi::online::set_local_build(content_build_for_mode(mode));
}

void prepare_practice_content(int mode, bool activate) {
  if (activate) apply_content_mode(mode);
  else slippi::online::set_local_build(content_build_for_mode(mode));
  host::log("native practice content: mode %d, advertised %s, active files %s%s", mode,
            slippi::online::local_build().mod_view ? "mod" : "retail",
            g_retail_view || g_view_alias.empty() ? "retail" : "mod",
            activate ? ", boundary activation" : "");
}

void h_resim_phase(int32_t entering) {
  g_resim = entering != 0;
  if (g_resim) ++g_resim_phases;
}

// --online-test: the harness enters an online match without menus. The first time the game asks,
// matchmaking starts (local test peering, from --local-peer); the game then waits for the match.
int32_t h_online_test_match(MuOnlineMatch* out) {
#ifdef MELEE_NO_SLIPPI
  // A session asks for a match through the neutral state (netplay_state.h). The game calls this at
  // boot, to pick its first scene, and again in that scene before it waits for the match state.
  const host::netplay::MatchRequest request = host::netplay::match_request();
  if (!request.requested) return 0;
  if (!g_online_test_started) {
    g_online_test_started = true;
    apply_content_mode(request.mode);
    host::log("netplay: the game enters a requested match (mode %d, pad port %d)", request.mode, request.local_port);
  }
  if (out) {
    std::memset(out, 0, sizeof *out);
    out->mode = (uint8_t)request.mode;
    out->local_port = (uint8_t)(request.local_port & 3);
  }
  return request.harness ? 1 : 2;
#else
  const auto& lobby = slippi::online::config();
  const bool from_lobby = !lobby.lobby_code.empty();
  if (g_online_test_mode < 0 && !from_lobby) return 0;
  const int mode = from_lobby ? 2 : g_online_test_mode;
  const uint8_t character = from_lobby ? (uint8_t)lobby.lobby_character : g_online_test_character;
  if (!g_online_test_started) {
    g_online_test_started = true;
    apply_content_mode(mode);   // a launcher or harness match skips the online menu's mode pick
    std::string error;
    if (slippi::online::native_start_match(mode, from_lobby ? lobby.lobby_code : "PEER#001", character,
                                           g_online_test_color, &error))
      host::log("online launch: searching (mode %d, character %u, color %u)", mode,
                character, g_online_test_color);
    else {
      host::log("online test: matchmaking did not start: %s", error.c_str());
      if (from_lobby) host::request_exit(2);
    }
  }
  if (out) {
    std::memset(out, 0, sizeof *out);
    out->mode = (uint8_t)mode;
    out->local_port = 0;
  }
  return from_lobby ? 2 : 1; // ABI: 1 = diagnostic harness, 2 = user-approved lobby match.
#endif
}
void h_vi_configure(uint32_t w, uint32_t h, uint32_t interlaced) { host::log("VI: %ux%u%s", w, h, interlaced ? " interlaced" : ""); }
void h_vi_set_next_framebuffer(void*) {}   // presentation follows the EFB copy, as in the recompiled build
void h_vi_flush() {}
uint32_t h_vi_retrace_count() { return host::retrace_count(); }
uint32_t h_vi_next_field() { return host::retrace_count() & 1; }
void h_vi_set_black(int32_t) {}
void h_vi_wait_retrace() {
  if (g_resim) { ++g_resim_vi_waits; return; }
  guarded([] { host::retrace(); });
}

// The idle wait before a retrace, one audio period at a time (host API 16). Plays each block at
// the real time of its AI deadline instead of three together after the retrace. Alarms and the
// retrace itself stay where they were (native_retrace), so game code runs in the same order.
// MELEE_AUDIO_PACING=0 turns it off for comparisons.
int32_t h_vi_idle_step() {
  static const bool enabled = [] { const char* v = std::getenv("MELEE_AUDIO_PACING"); return !(v && *v == '0'); }();
  // MELEE_AUDIO_PACING_TRACE=1: why the idle loop stops, counted and logged every 600 calls.
  static const bool trace = [] { const char* v = std::getenv("MELEE_AUDIO_PACING_TRACE"); return v && *v == '1'; }();
  static uint64_t calls, stop_off, stop_awaiting, stop_past_retrace, steps;
  if (trace && ++calls % 600 == 0)
    host::log("audio pacing: %llu calls, %llu steps; stops: awaiting block %llu, deadline past retrace %llu, off %llu",
              (unsigned long long)calls, (unsigned long long)steps, (unsigned long long)stop_awaiting,
              (unsigned long long)stop_past_retrace, (unsigned long long)stop_off);
  if (!enabled || g_resim || !g_audio.running || !g_audio.dma_length) { ++stop_off; return 0; }
  // The game has not handed over the next block yet: nothing to play at the next deadline.
  if (g_audio.native_pcm && g_audio.awaiting_block) { ++stop_awaiting; return 0; }
  int32_t stepped = 0;
  guarded([&] {
    const uint64_t due = g_audio.next_tb;
    if (host::cpu->tb < due && !host::wait_until_console_time(due)) { ++stop_past_retrace; return; }
    native_audio_tick();
    stepped = 1;
    ++steps;
  });
  return stepped;
}

void h_pad_read(MuPadStatus out[4]) {
  host::PadState pads[4];
  host::input_poll(pads);
  lcancel::apply(pads);   // auto L-cancel, upstream of the game exactly as in the recompiled build
  // The player's data-only Gecko codes, once per frame. Offline only: the writer in the game
  // library also refuses during an online match, whichever kind.
  if (slippi::online::session_mode() < 0 && !g_replaying) user_gecko::apply();
  if (host::audio_tracing()) {
    // Latency trace: the first read that shows a new press on port 1 (buttons or the stick leaving
    // its center), the moment the game sees it.
    static bool was_active = false;
    const bool active = pads[0].err == 0 && ((pads[0].button & 0x0F7Fu) || std::abs((int)pads[0].stick_y) >= 40 ||
                                             std::abs((int)pads[0].stick_x) >= 40);
    if (active && !was_active) host::audio_trace_event("input");
    was_active = active;
  }
  for (int i = 0; i < 4; ++i) {
    out[i].button = pads[i].button;
    out[i].stick_x = pads[i].stick_x; out[i].stick_y = pads[i].stick_y;
    out[i].sub_x = pads[i].sub_x; out[i].sub_y = pads[i].sub_y;
    out[i].trigger_l = pads[i].trig_l; out[i].trigger_r = pads[i].trig_r;
    out[i].analog_a = pads[i].analog_a; out[i].analog_b = pads[i].analog_b;
    out[i].err = pads[i].err;
  }
  // 20XX TE input display (offline matches and replays): the controllers the game read.
  training_overlay::set_input_display(te_host_feature(MU_GAME_OPTION2_TE_INPUT_DISPLAY));
  // Training Lab overlay: what the game read this frame, with the fighters' state.
  if (((gx::RenderOptions::live_te_options() & (MU_GAME_OPTION_LAB | MU_GAME_OPTION_LAB_OVERLAY)) ==
           (MU_GAME_OPTION_LAB | MU_GAME_OPTION_LAB_OVERLAY) || training_overlay::input_display()) &&
      g_game.state_snapshot) {
    MuStatePod state{};
    g_game.state_snapshot(&state);
    training_overlay::publish(state, out);
  }
  if (!host::options.input_log.empty()) {   // --input-log, same CSV as the recompiled build
    static FILE* input_log = std::fopen(host::options.input_log.c_str(), "w");
    if (input_log) {
      static bool header = (std::fputs("retrace,port,buttons,stick_x,stick_y,cstick_x,cstick_y,trigger_l,trigger_r\n", input_log), true);
      (void)header;
      for (int i = 0; i < 4; ++i)
        if (pads[i].err == 0)
          std::fprintf(input_log, "%u,%d,%04X,%d,%d,%d,%d,%u,%u\n", host::retrace_count(), i + 1, pads[i].button, pads[i].stick_x, pads[i].stick_y,
                       pads[i].sub_x, pads[i].sub_y, pads[i].trig_l, pads[i].trig_r);
      std::fflush(input_log);
    }
  }
}
// The game's motor commands go through the same routing as the Static Recomp's: the Controller
// rumble setting, the device actually feeding that port, and online only the local player's slot.
// Sending them straight to the adapter ignored the setting. A replay rumbles nobody.
void h_pad_rumble(int32_t port, int32_t on) {
  if (g_replaying) on = 0;
  if (slippi::online::is_online_match()) {
    if (port == slippi::online::local_player_slot()) host::input_rumble_local(on != 0);
    return;
  }
  host::input_rumble(port, on != 0);
}

// Slippi's online flow is game mode 8, asked of the game itself (practice bridge, scene query).
bool native_online_mode() {
  if (!g_game.practice) return false;
  int32_t scene[2] = {0, 0};
  return g_game.practice(MU_PRACTICE_SCENE, scene, 2) == 0 && scene[0] == 8;
}
int32_t h_disc_entrynum(const char* path) {
  std::string p = path ? path : "";
  std::transform(p.begin(), p.end(), p.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
  if (p.empty() || p[0] != '/') p = "/" + p;
  auto it = g_paths.find(p);
  if (it != g_paths.end() && g_retail_view) {
    // The retail copy, or -1 for a file the retail game does not have. A pack costume that only
    // changes looks keeps its own entry, so it is the same file (and the same cached copy) in both views.
    const int32_t opened = skins::resolve_open(true, it->second, g_view_alias, g_pack_skin_served);
    if (opened != it->second) return opened;
  }
  if (it != g_paths.end() && !g_cosmetic_alias.empty()) {
    const auto alias = g_cosmetic_alias.find(it->second);
    if (alias != g_cosmetic_alias.end() && host::cosmetics::online_active()) {
      host::log("cosmetics: %s opened as the disc's copy (online rule)", p.c_str());
      return alias->second;
    }
  }
  static const bool trace = std::getenv("MELEE_TRACE_DISC_OPEN") != nullptr;
  if (trace) host::log("disc: open %s -> %d at %08X", p.c_str(), it == g_paths.end() ? -1 : it->second,
                       it == g_paths.end() ? 0u : g_fst[it->second].offset);
  return it == g_paths.end() ? -1 : it->second;
}
int32_t h_disc_file(int32_t entrynum, uint32_t* start, uint32_t* length) {
  if (entrynum <= 0 || entrynum >= (int32_t)g_fst.size() || g_fst[entrynum].dir) return 0;
  *start = g_fst[entrynum].offset;
  *length = g_fst[entrynum].length;
  return 1;
}
// The jukebox reads songs from where the game would: Slippi system files, the mod overlay, the disc.
bool jukebox_disc_read(uint32_t offset, void* dst, uint32_t size) {
  bool ok = false;
  if (!read_cosmetic(offset, dst, size, &ok) && !read_system_file(offset, dst, size, &ok) &&
      !read_overlay(offset, dst, size, &ok))
    ok = host::disc_read(offset, dst, size);
  return ok;
}
void h_disc_read(uint32_t offset, void* dst, uint32_t size, MuDiscDone done, void* user) {
  bool ok = false;
  const auto began = std::chrono::steady_clock::now();
  if (!read_cosmetic(offset, dst, size, &ok) && !read_system_file(offset, dst, size, &ok) &&
      !read_overlay(offset, dst, size, &ok))
    ok = host::disc_read(offset, dst, size);
  if (!ok) host::log("disc: read %08X+%X failed", offset, size);
  // Reads are synchronous on the simulation thread: a slow one is a hitch the player feels. Logged
  // (at most 50 times) so a report of stutter can be matched against the disc.
  {
    static int slow_logged = 0;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    if (ms > 4.0 && slow_logged < 50) {
      ++slow_logged;
      uint32_t major = 0, minor = 0, match_frame = 0;
      host::current_scene(&major, &minor, &match_frame);
      host::log("disc: slow read %.1f ms (%08X+%X, scene %02X/%02X)", ms, offset, size, major, minor);
    }
  }
  note_disc_load(dst, size, offset);
  ++host::g_disc_reads;
  host::g_disc_bytes += size;
  done(ok ? (int32_t)size : -1, user);   // the game queues it and runs it as the drive interrupt
}
int32_t h_disc_status() { return 0; }
uint32_t h_disc_id(void* out, uint32_t size) { const uint32_t n = std::min<uint32_t>(size, 0x20); std::memcpy(out, (void*)MEM1_BASE, n); return n; }

// Memory card: slot A is a Dolphin-compatible folder of .gci files. The game-side shim passes
// native pointers here, so directory entries are converted explicitly instead of exposing their
// big-endian on-card representation as a host struct.
namespace {
constexpr int32_t CARD_READY = 0, CARD_NOCARD = -3, CARD_NOFILE = -4, CARD_EXIST = -7,
                  CARD_NOENT = -8, CARD_INSSPACE = -9, CARD_NOPERM = -10,
                  CARD_LIMIT = -11, CARD_NAMETOOLONG = -12, CARD_FATAL = -128;
constexpr uint32_t CARD_SECTOR = 0x2000, CARD_MAX_FILES = 127, CARD_TOTAL_BLOCKS = 2043,
                   CARD_MEM_SIZE_MBIT = 128;

struct SourceCardFile {
  uint8_t dir[64]{};
  std::vector<uint8_t> data;
  std::filesystem::path path;

  std::string name() const {
    return std::string(reinterpret_cast<const char*>(dir + 8), strnlen(reinterpret_cast<const char*>(dir + 8), 32));
  }
  uint16_t blocks() const { return (uint16_t)(((uint16_t)dir[0x38] << 8) | dir[0x39]); }
};

struct SourceCardStat {
  char fileName[32];
  uint32_t length, time;
  uint8_t gameName[4], company[2], bannerFormat;
  uint32_t iconAddr;
  uint16_t iconFormat, iconSpeed;
  uint32_t commentAddr, offsetBanner, offsetBannerTlut, offsetIcon[8], offsetIconTlut, offsetData;
};
static_assert(sizeof(SourceCardStat) == 0x6C, "native CARDStat layout");

std::vector<SourceCardFile*> g_card_files;
std::filesystem::path g_card_dir;
bool g_card_mounted = false;

uint16_t card_be16(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
uint32_t card_be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
void card_put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
void card_put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

const uint8_t* card_disk_id() { return reinterpret_cast<const uint8_t*>(MEM1_BASE); }
bool card_same_game(const SourceCardFile& file) { return std::memcmp(file.dir, card_disk_id(), 6) == 0; }
uint32_t card_used_blocks() {
  uint32_t blocks = 0;
  for (const SourceCardFile* file : g_card_files) if (file) blocks += file->blocks();
  return blocks;
}
int32_t card_find(const std::string& name) {
  for (size_t i = 0; i < g_card_files.size(); ++i) {
    if (g_card_files[i] && card_same_game(*g_card_files[i]) && g_card_files[i]->name() == name)
      return (int32_t)i;
  }
  return -1;
}
// Plain ASCII whatever the locale calls a letter: a byte above 0x7F is not valid text for the path
// conversion that follows.
std::string card_safe_name(const SourceCardFile& file) {
  std::string name;
  for (int i = 0; i < 6; ++i) { const unsigned char ch = file.dir[i]; name += (ch < 0x80 && std::isalnum(ch)) ? (char)ch : '_'; }
  name += "-";
  for (char ch : file.name()) {
    const unsigned char u = (unsigned char)ch;
    name += (u < 0x80 && (std::isalnum(u) || ch == '_' || ch == '-' || ch == '.')) ? ch : '_';
  }
  return name + ".gci";
}
// A path as text for the log. Turning a path into narrow text can fail (a folder name the system
// code page cannot hold), and on the Static Recomp that failure inside a save ended the game. The
// save path here stays wide and never ends the game either.
std::string card_shown(const std::filesystem::path& p) {
  try { const auto text = p.u8string(); return std::string(text.begin(), text.end()); }
  catch (...) { return "<name not printable>"; }
}
void card_save(SourceCardFile& file) {
  try {
    if (file.path.empty()) file.path = g_card_dir / card_safe_name(file);
    std::filesystem::path temporary = file.path; temporary += L".tmp";
    FILE* out = _wfopen(temporary.c_str(), L"wb");
    if (!out) { host::log("card: cannot write %s", card_shown(file.path).c_str()); return; }
    const bool ok = std::fwrite(file.dir, 1, sizeof file.dir, out) == sizeof file.dir &&
                    std::fwrite(file.data.data(), 1, file.data.size(), out) == file.data.size();
    std::fclose(out);
    std::error_code ec;
    // Another program (a virus scanner, the search indexer) can hold the new file or the old one
    // open for a moment right after it is written, and the replace is refused. Seen about once in
    // ten saves on one PC. Try again for a short while before giving the save up.
    DWORD code = 0;
    for (int attempt = 0; ok && attempt < 20; ++attempt) {
      if (MoveFileExW(temporary.c_str(), file.path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { code = 0; break; }
      code = GetLastError();
      if (code != ERROR_SHARING_VIOLATION && code != ERROR_ACCESS_DENIED && code != ERROR_LOCK_VIOLATION) break;
      Sleep(10);
    }
    if (!ok || code) {
      std::filesystem::remove(temporary, ec);
      host::log("card: failed to save %s (error %lu)", card_shown(file.path).c_str(), (unsigned long)code);
    }
  } catch (const std::exception& error) {
    host::log("card: failed to save %s (%s)", card_safe_name(file).c_str(), error.what());
  }
}
void card_clear_files() {
  for (SourceCardFile* file : g_card_files) delete file;
  g_card_files.clear();
}
void card_mount_files() {
  if (g_card_mounted) return;
  g_card_dir = g_profile_card.empty() ? std::filesystem::u8path(host::options.card_dir) : g_profile_card;
  std::error_code ec;
  std::filesystem::create_directories(g_card_dir, ec);
  card_clear_files();
  g_card_files.assign(CARD_MAX_FILES, nullptr);
  size_t slot = 0;
  for (const auto& entry : std::filesystem::directory_iterator(g_card_dir, ec)) {
    if (ec || entry.path().extension() != L".gci" || slot >= CARD_MAX_FILES) continue;
    FILE* in = _wfopen(entry.path().c_str(), L"rb");
    if (!in) continue;
    auto* file = new SourceCardFile;
    bool ok = std::fread(file->dir, 1, sizeof file->dir, in) == sizeof file->dir;
    if (ok) {
      file->data.resize((size_t)file->blocks() * CARD_SECTOR);
      ok = std::fread(file->data.data(), 1, file->data.size(), in) == file->data.size();
    }
    std::fclose(in);
    if (!ok || file->blocks() == 0 || file->blocks() > CARD_TOTAL_BLOCKS) { delete file; continue; }
    file->path = entry.path();
    // Two files with one save name: card_find would use whichever the folder listed first, so a save
    // dropped in beside the player's own (20XX TE has Melee's own save name) could hide it. The boot
    // check (mods::protect_card_folder) moves such files out; if one is still here, the file the game
    // itself writes (card_safe_name) is the one mounted.
    const int32_t same = card_find(file->name());
    if (same >= 0 && std::memcmp(g_card_files[same]->dir, file->dir, 6) == 0) {
      const bool keep_new = file->path.filename() == std::filesystem::u8path(card_safe_name(*file)) &&
                            g_card_files[same]->path.filename() != std::filesystem::u8path(card_safe_name(*g_card_files[same]));
      host::log("card: %s and %s have the same save name; %s is used", g_card_files[same]->path.u8string().c_str(),
                file->path.u8string().c_str(), (keep_new ? file : g_card_files[same])->path.u8string().c_str());
      if (keep_new) { delete g_card_files[same]; g_card_files[same] = file; } else delete file;
      continue;
    }
    g_card_files[slot++] = file;
  }
  // Imported GCI patches are mounted without copying or modifying the supplied
  // file. Writes create a private copy in the port's card directory; on later
  // mounts that private copy takes precedence over the original import.
  for (const auto& source : g_mod_gcis) {
    if (slot >= CARD_MAX_FILES) host::die("card: no free file slot for imported GCI");
    const auto length = std::filesystem::file_size(source, ec);
    if (ec || length < 64 || length > uint64_t(CARD_TOTAL_BLOCKS) * CARD_SECTOR + 64) {
      host::die("card: invalid imported GCI size: %s", source.string().c_str());
    }
    FILE* in = std::fopen(source.string().c_str(), "rb");
    if (!in) host::die("card: cannot open imported GCI: %s", source.string().c_str());
    auto* file = new SourceCardFile;
    const bool header_ok = std::fread(file->dir, 1, sizeof file->dir, in) == sizeof file->dir;
    const uint32_t blocks = header_ok ? file->blocks() : 0;
    if (!blocks || blocks > CARD_TOTAL_BLOCKS || length != 64ull + uint64_t(blocks) * CARD_SECTOR) {
      std::fclose(in); delete file;
      host::die("card: malformed imported GCI: %s", source.string().c_str());
    }
    file->data.resize(size_t(blocks) * CARD_SECTOR);
    const bool data_ok = std::fread(file->data.data(), 1, file->data.size(), in) == file->data.size();
    std::fclose(in);
    if (!data_ok || !card_same_game(*file)) {
      delete file;
      host::die("card: imported GCI is unreadable or belongs to another game: %s", source.string().c_str());
    }
    const int32_t previous = card_find(file->name());
    const auto private_path = g_card_dir / card_safe_name(*file);
    if (previous >= 0 && g_card_files[previous]->path == private_path) {
      host::log("card: saved private copy supersedes imported %s", source.string().c_str());
      delete file;
      continue;
    }
    if (previous >= 0) {
      const std::string name = file->name();
      delete file;
      host::die("card: imported GCI conflicts with existing card file %s", name.c_str());
    }
    file->path = private_path;
    g_card_files[slot++] = file;
    host::log("card: imported %s as %s (copy-on-write)", source.string().c_str(), file->name().c_str());
  }
  g_card_mounted = true;
  host::log("card: source slot A mounted from %s (%zu files, %u of %u blocks used)",
            card_shown(g_card_dir).c_str(), slot, card_used_blocks(), CARD_TOTAL_BLOCKS);
}
void card_reset_mount() { card_clear_files(); g_card_mounted = false; }
// Seconds since 2000-01-01 on the console clock (its date at start plus game time), as the game's
// own calendar reads it: a save file used to be stamped January 1, 2000.
uint32_t card_time_2000() { return host::cpu ? (uint32_t)((host::console_epoch_ticks() + host::cpu->tb) / host::TB_HZ) : 0; }
void card_fill_stat(const SourceCardFile& file, SourceCardStat& stat) {
  std::memset(&stat, 0, sizeof stat);
  std::memcpy(stat.fileName, file.dir + 8, 32);
  stat.length = (uint32_t)file.data.size();
  stat.time = card_be32(file.dir + 0x28);
  std::memcpy(stat.gameName, file.dir, 4);
  std::memcpy(stat.company, file.dir + 4, 2);
  stat.bannerFormat = file.dir[7];
  stat.iconAddr = card_be32(file.dir + 0x2C);
  stat.iconFormat = card_be16(file.dir + 0x30);
  stat.iconSpeed = card_be16(file.dir + 0x32);
  stat.commentAddr = card_be32(file.dir + 0x3C);
  const uint32_t none = 0xFFFFFFFFu;
  for (uint32_t& offset : stat.offsetIcon) offset = none;
  stat.offsetBanner = stat.offsetBannerTlut = stat.offsetIconTlut = none;
  if (stat.iconAddr == none) { stat.offsetData = 0; return; }
  uint32_t offset = stat.iconAddr;
  if ((stat.bannerFormat & 3) == 1) { stat.offsetBanner = offset; offset += 3072; stat.offsetBannerTlut = offset; offset += 512; }
  else if ((stat.bannerFormat & 3) == 2) { stat.offsetBanner = offset; offset += 6144; }
  bool has_tlut = false;
  for (int i = 0; i < 8; ++i) {
    const uint32_t format = (stat.iconFormat >> (2 * i)) & 3;
    if (format == 1) { stat.offsetIcon[i] = offset; offset += 1024; has_tlut = true; }
    else if (format == 2) { stat.offsetIcon[i] = offset; offset += 2048; }
  }
  if (has_tlut) { stat.offsetIconTlut = offset; offset += 512; }
  stat.offsetData = offset;
}
}  // namespace

int32_t h_card_probe(int32_t chan, int32_t* mem_size, int32_t* sector_size) {
  if (chan != 0) return CARD_NOCARD;
  if (mem_size) *mem_size = CARD_MEM_SIZE_MBIT;
  if (sector_size) *sector_size = CARD_SECTOR;
  return CARD_READY;
}
int32_t h_card_mount(int32_t chan) { if (chan != 0) return CARD_NOCARD; card_mount_files(); return CARD_READY; }
int32_t h_card_unmount(int32_t chan) { if (chan != 0) return CARD_NOCARD; card_reset_mount(); return CARD_READY; }
int32_t h_card_open(int32_t chan, const char* name, int32_t* file_no, uint32_t* length) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  const int32_t no = card_find(name ? name : "");
  if (no < 0) return CARD_NOFILE;
  if (file_no) *file_no = no;
  if (length) *length = (uint32_t)g_card_files[no]->data.size();
  return CARD_READY;
}
int32_t h_card_close(int32_t chan, int32_t) { return chan == 0 ? CARD_READY : CARD_NOCARD; }
int32_t h_card_create(int32_t chan, const char* name, uint32_t size, int32_t* file_no) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  const std::string filename = name ? name : "";
  if (filename.empty() || filename.size() > 32) return CARD_NAMETOOLONG;
  if (card_find(filename) >= 0) return CARD_EXIST;
  if (size == 0 || size % CARD_SECTOR) return CARD_FATAL;
  const uint32_t blocks = size / CARD_SECTOR;
  if (card_used_blocks() + blocks > CARD_TOTAL_BLOCKS) return CARD_INSSPACE;
  int32_t no = -1;
  for (size_t i = 0; i < g_card_files.size(); ++i) if (!g_card_files[i]) { no = (int32_t)i; break; }
  if (no < 0) return CARD_NOENT;
  auto* file = new SourceCardFile;
  std::memcpy(file->dir, card_disk_id(), 6);
  file->dir[6] = 0xFF; file->dir[7] = 0;
  std::memcpy(file->dir + 8, filename.data(), filename.size());
  card_put32(file->dir + 0x28, card_time_2000());
  card_put32(file->dir + 0x2C, 0xFFFFFFFFu);
  card_put16(file->dir + 0x30, 0); card_put16(file->dir + 0x32, 0);
  file->dir[0x34] = 0x04; file->dir[0x35] = 0;
  card_put16(file->dir + 0x36, (uint16_t)(5 + card_used_blocks()));
  card_put16(file->dir + 0x38, (uint16_t)blocks);
  file->dir[0x3A] = 0xFF; file->dir[0x3B] = 0xFF; card_put32(file->dir + 0x3C, 0xFFFFFFFFu);
  file->data.assign(size, 0xFF);
  g_card_files[no] = file;
  card_save(*file);
  if (file_no) *file_no = no;
  return CARD_READY;
}
int32_t h_card_delete(int32_t chan, const char* name) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  const int32_t no = card_find(name ? name : "");
  if (no < 0) return CARD_NOFILE;
  SourceCardFile* file = g_card_files[no];
  std::error_code ec; std::filesystem::remove(file->path, ec);
  delete file; g_card_files[no] = nullptr;
  return CARD_READY;
}
int32_t h_card_rename(int32_t chan, const char* old_name, const char* new_name) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  const std::string old_file = old_name ? old_name : "", new_file = new_name ? new_name : "";
  if (new_file.empty() || new_file.size() > 32) return CARD_NAMETOOLONG;
  const int32_t no = card_find(old_file);
  if (no < 0) return CARD_NOFILE;
  if (card_find(new_file) >= 0) return CARD_EXIST;
  SourceCardFile& file = *g_card_files[no];
  std::error_code ec; std::filesystem::remove(file.path, ec); file.path.clear();
  std::memset(file.dir + 8, 0, 32); std::memcpy(file.dir + 8, new_file.data(), new_file.size());
  card_save(file);
  return CARD_READY;
}
int32_t h_card_read(int32_t chan, int32_t file_no, void* dst, uint32_t length, uint32_t offset) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  if (file_no < 0 || file_no >= (int32_t)g_card_files.size() || !g_card_files[file_no]) return CARD_NOFILE;
  SourceCardFile& file = *g_card_files[file_no];
  if ((uint64_t)offset + length > file.data.size()) return CARD_LIMIT;
  std::memcpy(dst, file.data.data() + offset, length); return CARD_READY;
}
int32_t h_card_write(int32_t chan, int32_t file_no, const void* src, uint32_t length, uint32_t offset) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  if (file_no < 0 || file_no >= (int32_t)g_card_files.size() || !g_card_files[file_no]) return CARD_NOFILE;
  SourceCardFile& file = *g_card_files[file_no];
  if ((uint64_t)offset + length > file.data.size()) return CARD_LIMIT;
  std::memcpy(file.data.data() + offset, src, length); card_save(file); return CARD_READY;
}
int32_t h_card_stat(int32_t chan, int32_t file_no, void* stat, uint32_t stat_size) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  if (file_no < 0 || file_no >= (int32_t)g_card_files.size() || !g_card_files[file_no]) return CARD_NOFILE;
  SourceCardStat value{}; card_fill_stat(*g_card_files[file_no], value);
  std::memcpy(stat, &value, std::min<uint32_t>(stat_size, sizeof value)); return CARD_READY;
}
int32_t h_card_set_stat(int32_t chan, int32_t file_no, const void* stat, uint32_t stat_size) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  if (file_no < 0 || file_no >= (int32_t)g_card_files.size() || !g_card_files[file_no]) return CARD_NOFILE;
  if (stat_size < sizeof(SourceCardStat)) return CARD_LIMIT;
  SourceCardStat value{}; std::memcpy(&value, stat, sizeof value);
  SourceCardFile& file = *g_card_files[file_no];
  file.dir[7] = value.bannerFormat; card_put32(file.dir + 0x2C, value.iconAddr);
  card_put16(file.dir + 0x30, value.iconFormat); card_put16(file.dir + 0x32, value.iconSpeed);
  card_put32(file.dir + 0x3C, value.commentAddr); card_put32(file.dir + 0x28, card_time_2000());
  card_save(file); return CARD_READY;
}
int32_t h_card_free_blocks(int32_t chan, int32_t* bytes_unused, int32_t* files_unused) {
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  if (bytes_unused) *bytes_unused = (int32_t)((CARD_TOTAL_BLOCKS - std::min(CARD_TOTAL_BLOCKS, card_used_blocks())) * CARD_SECTOR);
  if (files_unused) *files_unused = (int32_t)(CARD_MAX_FILES - std::count_if(g_card_files.begin(), g_card_files.end(), [](const SourceCardFile* f) { return f != nullptr; }));
  return CARD_READY;
}
int32_t h_card_format(int32_t chan) {
  if (chan != 0) return CARD_NOCARD;
  card_mount_files();
  for (SourceCardFile*& file : g_card_files) {
    if (!file) continue;
    std::error_code ec; std::filesystem::remove(file->path, ec);
    delete file; file = nullptr;
  }
  return CARD_READY;
}

bool card_self_test_impl(const char* directory) {
  if (!directory || !*directory) return false;
  host::options.card_dir = directory;
  card_reset_mount();
  const uint8_t id[6] = {'G', 'A', 'L', 'E', '0', '1'};
  std::memcpy(reinterpret_cast<void*>(MEM1_BASE), id, sizeof id);
  int32_t mem_size = 0, sector_size = 0;
  if (h_card_probe(0, &mem_size, &sector_size) != CARD_READY || mem_size != (int32_t)CARD_MEM_SIZE_MBIT ||
      sector_size != (int32_t)CARD_SECTOR || h_card_mount(0) != CARD_READY)
    return false;
  if (h_card_mount(1) != CARD_NOCARD || h_card_create(1, "MU_NATIVE_CARD_TEST", CARD_SECTOR, nullptr) != CARD_NOCARD ||
      h_card_open(0, "missing", nullptr, nullptr) != CARD_NOFILE)
    return false;
  int32_t file_no = -1;
  if (h_card_create(0, "MU_NATIVE_CARD_TEST", CARD_SECTOR, &file_no) != CARD_READY || file_no < 0)
    return false;
  if (h_card_create(0, "MU_NATIVE_CARD_TEST", CARD_SECTOR, nullptr) != CARD_EXIST ||
      h_card_create(0, "MU_NATIVE_CARD_BAD_SIZE", CARD_SECTOR - 1, nullptr) != CARD_FATAL ||
      h_card_read(0, -1, nullptr, 0, 0) != CARD_NOFILE ||
      h_card_write(0, -1, nullptr, 0, 0) != CARD_NOFILE)
    return false;
  std::vector<uint8_t> expected(CARD_SECTOR), actual(CARD_SECTOR);
  for (uint32_t i = 0; i < expected.size(); ++i) expected[i] = (uint8_t)((i * 37u + 11u) & 0xFFu);
  if (h_card_read(0, file_no, actual.data(), (uint32_t)actual.size(), CARD_SECTOR) != CARD_LIMIT ||
      h_card_write(0, file_no, expected.data(), (uint32_t)expected.size(), CARD_SECTOR) != CARD_LIMIT)
    return false;
  for (uint32_t pass = 0; pass < 3; ++pass) {
    for (uint32_t i = 0; i < expected.size(); ++i)
      expected[i] = (uint8_t)((i * (37u + pass * 2u) + 11u + pass) & 0xFFu);
    if (h_card_write(0, file_no, expected.data(), (uint32_t)expected.size(), 0) != CARD_READY ||
        h_card_read(0, file_no, actual.data(), (uint32_t)actual.size(), 0) != CARD_READY || actual != expected)
      return false;
  }
  SourceCardStat stat{};
  if (h_card_stat(0, file_no, &stat, sizeof stat) != CARD_READY || stat.length != CARD_SECTOR)
    return false;
  if (h_card_unmount(0) != CARD_READY || h_card_mount(0) != CARD_READY)
    return false;
  int32_t reopened = -1; uint32_t length = 0;
  if (h_card_open(0, "MU_NATIVE_CARD_TEST", &reopened, &length) != CARD_READY ||
      reopened < 0 || length != CARD_SECTOR || h_card_read(0, reopened, actual.data(), (uint32_t)actual.size(), 0) != CARD_READY ||
      actual != expected)
    return false;
  if (h_card_rename(0, "MU_NATIVE_CARD_TEST", "MU_NATIVE_CARD_RENAMED") != CARD_READY ||
      h_card_open(0, "MU_NATIVE_CARD_TEST", nullptr, nullptr) != CARD_NOFILE ||
      h_card_open(0, "MU_NATIVE_CARD_RENAMED", &reopened, &length) != CARD_READY ||
      h_card_read(0, reopened, actual.data(), (uint32_t)actual.size(), 0) != CARD_READY || actual != expected ||
      h_card_delete(0, "MU_NATIVE_CARD_RENAMED") != CARD_READY ||
      h_card_open(0, "MU_NATIVE_CARD_RENAMED", nullptr, nullptr) != CARD_NOFILE)
    return false;
  if (h_card_format(0) != CARD_READY || h_card_unmount(0) != CARD_READY)
    return false;
  host::log("card: native isolated repeated-I/O, bounds, error, rename, delete, and remount checks passed");
  return true;
}

void h_ai_init_dma(void* buffer, uint32_t length) {
  audio_core::init_dma(g_audio, buffer, length, true);
}
void h_ai_start_dma(int32_t on) {
  const bool was_running = g_audio.running;
  audio_core::start_dma(g_audio, on != 0, host::cpu->tb, host::TB_HZ);
  // Diagnostic only (M4): move the AX clock against the retrace grid by this many timebase ticks.
  static const char* shift = std::getenv("MELEE_TEST_AX_PHASE_SHIFT");
  if (!was_running && g_audio.running && shift)
    g_audio.next_tb += std::strtoull(shift, nullptr, 0);
  if (!was_running && g_audio.running)
    host::log("audio: AI clock phase retrace=%u next_tick_after_boundary=%lld", host::retrace_count(),
              (long long)(g_audio.next_tb - (host::next_retrace_tb() - host::TB_PER_FRAME)));
}
void h_ai_set_sample_rate(uint32_t) {}
void h_ai_set_stream_volume(int32_t, int32_t) {}
void h_dsp_mail(uint32_t mail) { audio_core::dsp_mail(mail); }
uint32_t h_dsp_mail_pending() { return audio_core::dsp_mail_pending(); }
void* h_aram_base() { return host::aram; }
uint32_t h_aram_size() { return 0x02000000u; }
void h_aram_dma(int32_t to_aram, void* mainmem, uint32_t aram_offset, uint32_t length) {
  if ((uint64_t)aram_offset + length > 0x02000000u) host::die("ARAM DMA out of range %08X+%X", aram_offset, length);
  if (to_aram) std::memcpy(host::aram + aram_offset, mainmem, length);
  else std::memcpy(mainmem, host::aram + aram_offset, length);
}
void* h_native_alloc(uint32_t size) { return std::malloc(size); }
void h_native_free(void* ptr) { std::free(ptr); }

uint32_t h_mem1_size() { return MEM1_SIZE; }
uint32_t h_mod_flags() {
  return (g_mod_overlay.files().empty() ? 0 : MU_MOD_ASSETS_PRESENT) | (hackpack::loaded() ? MU_MOD_HACKPACK : 0u);
}
int32_t h_sound_mode() { return 1; }
void h_set_sound_mode(int32_t) {}
int32_t h_progressive_mode() { return 0; }
void h_set_progressive_mode(int32_t) {}
int32_t h_reset_code() { return 0; }
int32_t h_reset_switch() { return 0; }
void h_stop(int32_t reason, int32_t code) {
  host::log("game stopped: reason %d code %d", reason, code);
  host::request_exit(0);
  guarded([] { host::retrace(); });
}

// --match sets this once, before the game starts. An ordinary run never touches it, and the game
// only asks for it where a VS match's rules are finalised.
MuMatchOverride g_match{};
bool g_match_set = false;
const MuMatchOverride* h_match_override() { return g_match_set ? &g_match : nullptr; }

// --rng-seed: forced from host::options once at startup (see main.cpp's flag parsing), read by the
// game at the same first GS_VS frame callback, so a scripted run reaches identical RNG in both the source port and a
// --rng-seed recomp run from the first in-match frame.
void h_rng_seed_override(uint32_t* seed, int32_t* has_value) {
  *has_value = host::options.rng_seed_set ? 1 : 0;
  if (host::options.rng_seed_set) *seed = host::options.rng_seed;
}
// Called from the same first GS_VS frame callback unconditionally (not just when --rng-seed was given), so
// an @match-relative script section starts counting from the match's rules/players being
// finalised on an ordinary offline run too, the same instant the recomp guest's --rng-seed hook
// marks it.
void h_mark_match_start() { host::input_mark_match_start(); }

#ifdef MELEE_NO_SLIPPI
// No replay playback and no recording in this build. The game is told there is no replay; the
// callbacks stay in the host table so the same game interface version is served.
constexpr bool g_replay = false;
uint8_t replay_gate(int32_t, uint8_t) { return 0; }
void write_replay_recording() {}
const MuReplayStart* h_replay_start() { return nullptr; }
void h_replay_frame(int32_t, MuReplayFrame* out) { *out = MuReplayFrame{}; out->result = MU_REPLAY_TERMINATE; }
int32_t h_replay_stock_steal(int32_t, int32_t) { return 0; }
void h_replay_event(uint8_t, const uint8_t*, uint32_t) {}
void h_replay_finished() {}
#else
// ---- --replay: Slippi replay playback (host API version 9) ----
// The host side of Slippi's playback: what Dolphin's CEXISlippi answers the playback codes over EXI
// (prepareGameInfo, prepareFrameData, prepareIsStockSteal), in the Legacy port's "normal" mode with
// rollback display off, so the final version of every frame plays. The game side is
// sourceport/game/shim/mu_replay.c. The played-back game is recorded again from the game's own
// pre-frame and post-frame events and written as a .slp to --replay-dir.
std::string g_replay_path;
std::unique_ptr<Slippi::SlippiGame> g_replay;
MuReplayStart g_replay_start{};
std::vector<uint8_t> g_replay_game_start;   // the original Game Start payload (command 0x36), reused
std::vector<uint8_t> g_replay_game_end;
slippi::NativeReplayStream g_replay_stream;
bool g_replay_written = false;

// ---- the replay viewer's controls (host/replay_control.h) ----
// The game calls in at the top of every engine frame of the replay's match (mu_replay_frame_begin):
// that is where a pause holds and where speed is paced. Jumping back uses the rollback snapshot
// engine: a state is kept every few seconds at that same point, and a jump restores the newest one
// at or before its target and runs forward from it, unseen and unpaced. The recording of the
// played-back game is put back to where it stood at that state, so frames played again are
// recorded once.
std::map<int32_t, slippi::NativeReplayStream::Mark> g_replay_marks;   // per kept state: the recording then

void replay_keep(int32_t frame) {
  if (!g_savestates.active()) begin_native_savestates(replay_control::kMaxKeptStates);
  g_savestates.capture(frame);
  g_replay_marks[frame] = g_replay_stream.mark();
}

int32_t replay_restore(int32_t target) {
  int32_t frame = 0;
  if (!g_savestates.active() || !g_savestates.newest_at_or_before(target, &frame)) return replay_control::kNoFrame;
  const auto mark = g_replay_marks.find(frame);
  if (mark == g_replay_marks.end() || !g_savestates.load_keep(frame)) return replay_control::kNoFrame;
  g_replay_stream.rewind(mark->second);
  g_replay_marks.erase(std::next(mark), g_replay_marks.end());
  gx::mark_discontinuity();   // the next frame shown continues from this older state
  return frame;
}

// CMD_REPLAY_GATE. Phase 0, every frame: 1 when the viewer wants to go back (the game then calls
// again with phase 1, with its timers held), else 0. Phase 1: the state is restored.
uint8_t replay_gate(int32_t frame, uint8_t phase) {
  if (!g_replay) return 0;
  if (phase == 0) return replay_control::gate(frame) == replay_control::Gate::GoBack ? 1 : 0;
  return replay_control::go_back() == replay_control::Gate::Restored ? 2 : 0;
}

// The Game Start event's payload, from the file's raw element (the recording reuses it as is).
std::vector<uint8_t> read_replay_event(const std::string& path, uint8_t wanted) {
  std::ifstream f(path, std::ios::binary);
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  static const char tag[] = "raw[$U#l";
  auto it = std::search(d.begin(), d.end(), tag, tag + 8);
  if (it == d.end()) return {};
  size_t i = (size_t)(it - d.begin()) + 8 + 4;
  if (i + 2 > d.size() || d[i] != 0x35) return {};
  if (d[i + 1] < 1 || (d[i + 1] - 1) % 3) return {};
  const size_t count = (d[i + 1] - 1) / 3;
  std::array<int32_t, 256> sizes;
  sizes.fill(-1);
  for (size_t k = 0; k < count; ++k) {
    const size_t e = i + 2 + 3 * k;
    if (e + 3 > d.size()) return {};
    sizes[d[e]] = (uint32_t)d[e + 1] << 8 | d[e + 2];
  }
  const uint32_t raw_size = be32(d.data() + i - 4);
  if (raw_size && raw_size > d.size() - i) return {};
  const size_t end = raw_size ? i + raw_size : d.size();
  for (size_t pos = i + 1 + d[i + 1]; pos < end;) {
    const uint8_t command = d[pos++];
    if (sizes[command] < 0 || (size_t)sizes[command] > end - pos) return {};
    const size_t size = (size_t)sizes[command];
    if (command == wanted) return std::vector<uint8_t>(d.begin() + pos, d.begin() + pos + size);
    pos += size;
  }
  return {};
}

// Which of the replay's gameplay codes the native game knows, by injection address.
uint32_t replay_codes(const Slippi::GameSettings& s, uint8_t* ps_frozen_toggle) {
  struct Known { uint32_t address; uint32_t code; const char* name; };
  static const Known known[] = {
      {0x800D65EC, MU_REPLAY_CODE_UCF084, "UCF 0.84 DBOOC SquatRv fix"},
      {0x800C9A44, MU_REPLAY_CODE_UCF084, "UCF dashback"}, {0x8006B460, MU_REPLAY_CODE_UCF084, "UCF pad buffer"},
      {0x8008E54C, MU_REPLAY_CODE_UCF084, "UCF SDI"}, {0x8009A0B8, MU_REPLAY_CODE_UCF084, "UCF shield drop extended"},
      {0x800998A4, MU_REPLAY_CODE_UCF084, "UCF shield drop"}, {0x80093294, MU_REPLAY_CODE_UCF084, "UCF shield SDI"},
      {0x800908F4, MU_REPLAY_CODE_UCF084, "UCF tumble"},
      {0x8016E510, MU_REPLAY_CODE_NEUTRAL_SPAWN, "NeutralSpawn"},
      {0x801239A8, MU_REPLAY_CODE_FREEZE_GLITCH, "FreezeGlitchFix"},
      {0x801C154C, MU_REPLAY_CODE_INIT_STAGE_DATA, "Init Stage Data"},
      {0x80068EEC, MU_REPLAY_CODE_INIT_PLAYER_DATA, "Init Player Data"},
      {0x8006A880, MU_REPLAY_CODE_OFFSCREEN_DAMAGE, "BrawlOffscreenDamage"},
      {0x800D4C1C, MU_REPLAY_CODE_DEAD_UP_FALL, "FreezeDeadUpFallPhysics init"},
      {0x800D4D68, MU_REPLAY_CODE_DEAD_UP_FALL, "FreezeDeadUpFallPhysics fall"},
      {0x80080E80, MU_REPLAY_CODE_DEAD_UP_FALL, "FreezeDeadUpFallPhysics model"},
      {0x8021AAE4, MU_REPLAY_CODE_FD_BG_SEED, "FD DesyncProofBGTransformations"},
      {0x801C65C8, MU_REPLAY_CODE_PS_ZERO_BUFFER, "Stadium CustomZeroBuffer"},
      {0x801D4760, MU_REPLAY_CODE_PS_IS_VALID, "Stadium GrPsxIsValid"},
      {0x801D457C, MU_REPLAY_CODE_PS_FROZEN_CHECK, "Stadium IngameCheckIfFrozen"},
      {0x800165AC, MU_REPLAY_CODE_PS_FILE_LOAD, "Stadium StadiumFileLoad"},
      {0x8008653C, MU_REPLAY_CODE_WHISPY_FIX, "WhispyBlowDirFix"},
      {0x800AC5B8, MU_REPLAY_CODE_NANA_DETERMINISM, "NanaDeterminism"},
      {0x801D24FC, MU_REPLAY_CODE_PS_MONITOR, "PSCameraIndependentMonitor"},
      {0x800DB880, MU_REPLAY_CODE_PREVENT_WOBBLING, "PreventWobbling air"},
      {0x800DBBD4, MU_REPLAY_CODE_PREVENT_WOBBLING, "PreventWobbling ground"},
      {0x8008F090, MU_REPLAY_CODE_PREVENT_WOBBLING, "PreventWobbling check"},
      // Not gameplay: results screen panels, the online static data table, the HUD stock display.
      {0x80179378, 0, nullptr}, {0x8017900C, 0, nullptr}, {0x8017A890, 0, nullptr},
      {0x8000568C, 0, nullptr}, {0x802F84C8, 0, nullptr},
  };
  // The injection lists that say which codes affect gameplay ship with the playback Sys folder.
  std::string sys = host::options.sys_dir;
  for (const char* candidate : {host::options.sys_dir.c_str(), "port/slippi_sys_playback", "SysPlayback"}) {
    if (std::filesystem::exists(std::filesystem::path(candidate) / "Slippi" / "InjectionLists")) { sys = candidate; break; }
  }
  size_t dropped = 0;
  auto kept = slippi::playback::gameplay_codes(s.geckoCodes, sys, &dropped);
  uint32_t codes = 0;
  size_t unknown = 0;
  for (const auto& c : kept) {
    const Known* k = nullptr;
    for (const auto& e : known) if (e.address == c.address) { k = &e; break; }
    if (!k) {
      ++unknown;
      host::log("replay: code %08X (type %02X, %zu bytes) is not implemented natively; this replay may diverge",
                c.address, c.type, c.bytes.size());
      if ((c.address == 0x800C9A44 || c.address == 0x800998A4) && !(codes & MU_REPLAY_CODE_UCF084)) codes |= MU_REPLAY_CODE_UCF_OTHER;
      continue;
    }
    codes |= k->code;
    // IngameCheckIfFrozen keeps its toggle in its own code: b; blrl; .byte toggle.
    if (c.address == 0x801D457C && c.bytes.size() >= 8 + 12) *ps_frozen_toggle = c.bytes[8 + 8];
  }
  host::log("replay: code list %zu bytes, %zu gameplay codes kept (%zu denylisted by %s), %zu not native; codes mask %08X",
            s.geckoCodes.size(), kept.size(), dropped, sys.c_str(), unknown, codes);
  return codes;
}

void write_replay_recording() {
  if (g_replay_written || !g_replay_stream.active()) return;
  const auto file = g_replay_stream.encode();
  if (file.empty()) {
    host::log("replay: recording failed: %s", g_replay_stream.error().c_str());
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(host::options.replay_dir, ec);
  // Named by the game's start, as Dolphin names the file when the game creates it.
  std::time_t now = g_replay_stream.start_time() ? (std::time_t)g_replay_stream.start_time() : std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &now);
  char name[64];
  std::strftime(name, sizeof name, "Game_%Y%m%dT%H%M%S.slp", &tm);
  // Exclusive creation prevents quick successive matches (or two clients) from
  // overwriting a recording with the same second-resolution timestamp.
  std::filesystem::path out = std::filesystem::path(host::options.replay_dir) / name;
  HANDLE handle = INVALID_HANDLE_VALUE;
  for (unsigned suffix = 0; suffix < 10000; ++suffix) {
    if (suffix) out = std::filesystem::path(host::options.replay_dir) /
        (std::string(name, std::strlen(name) - 4) + "_" + std::to_string(suffix) + ".slp");
    handle = CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
  }
  if (handle != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    const bool ok = WriteFile(handle, file.data(), (DWORD)file.size(), &written, nullptr) && written == file.size();
    const bool closed = CloseHandle(handle) != 0;
    g_replay_written = ok && closed;
    host::log("replay: %s %s (%zu bytes)", g_replay_written ? "recording written to" : "write failed for",
              out.string().c_str(), file.size());
    if (g_replay_written) net_trace::replay_saved(out.string().c_str());
  } else {
    host::log("replay: cannot write %s (error %lu)", out.string().c_str(), GetLastError());
  }
}

const MuReplayStart* h_replay_start() { return g_replay ? &g_replay_start : nullptr; }

void h_replay_frame(int32_t frame, MuReplayFrame* out) {
  *out = MuReplayFrame{};
  if (!g_replay) { out->result = MU_REPLAY_TERMINATE; return; }
  // The whole file is here, so a frame that is not ready never becomes ready: terminate, as
  // Dolphin does once processing is complete.
  if (!g_replay->DoesFrameExist(frame)) { out->result = MU_REPLAY_TERMINATE; return; }
  Slippi::FrameData* f = g_replay->GetFrame(frame);
  out->result = MU_REPLAY_CONTINUE;
  out->seed_exists = f->randomSeedExists ? 1 : 0;
  out->seed = f->randomSeed;
  for (uint8_t port = 0; port < 4; ++port) {
    for (int follower = 0; follower < 2; ++follower) {
      const auto& source = follower ? f->followers : f->players;
      auto it = source.find(port);
      if (it == source.end()) continue;
      const Slippi::PlayerFrameData& d = it->second;
      MuReplayCharacter& c = out->character[port][follower];
      c.present = 1;
      c.random_seed = d.randomSeed;
      c.joystick_x = d.joystickX; c.joystick_y = d.joystickY; c.cstick_x = d.cstickX; c.cstick_y = d.cstickY;
      c.trigger = d.trigger;
      c.buttons = d.buttons;
      c.x = d.locationX; c.y = d.locationY; c.facing = d.facingDirection;
      c.action_state = d.animation;
      c.percent = d.percent;
      c.joystick_x_raw = d.joystickXRaw; c.joystick_y_raw = d.joystickYRaw;
      c.cstick_x_raw = d.cstickXRaw; c.cstick_y_raw = d.cstickYRaw;
    }
  }
}

int32_t h_replay_stock_steal(int32_t frame, int32_t port) {
  if (!g_replay || !g_replay->DoesFrameExist(frame)) return 0;
  return g_replay->GetFrame(frame)->players.count((uint8_t)port) ? 1 : 0;
}

void h_replay_event(uint8_t command, const uint8_t* payload, uint32_t size) {
  if (!g_replay && command == 0x36) {
    write_replay_recording();
    // Game Start payload offsets (the payload has no command byte): 0x1A3 major scene, 0x2BD the
    // online match id, 0x2F0 the game number.
    std::vector<uint8_t> start(payload, payload + size);
    const bool online = size >= 0x2F8 && (start[0x1A3] == 0x08 || start[0x2BD] != 0);
    std::vector<uint8_t> codes;
    if (online) {
      codes.assign(std::begin(native_online_recording_codes), std::end(native_online_recording_codes));
      const uint32_t game = ++g_online_games;
      for (int b = 0; b < 4; ++b) start[0x2F0 + b] = (uint8_t)(game >> (24 - 8 * b));
    } else if (!host::options.vanilla_game) {
      codes.assign(std::begin(native_recording_codes), std::end(native_recording_codes));
    } else {
      codes = {0xFF, 0, 0, 0, 0, 0, 0, 0};
    }
    g_replay_written = false;
    // Only layers that change the disc's files make a replay need them (a save file alone does not).
    g_replay_stream.set_mod_profile(g_retail_view || g_view_alias.empty() ? std::string() : g_mod_fingerprint);
    // The 20XX TE features in effect (offline only; online matches never carry them).
    // (Or the Hack Pack's share of them, with the pack and no TE save: te_words_mask1/2.)
    g_replay_stream.set_feature_options(online ? 0u : (gx::RenderOptions::live_te_options() & te_words_mask1()));
    g_replay_stream.set_feature_options2(online ? 0u : (gx::RenderOptions::live_te_options2() & te_words_mask2()) |
                                                      cpu_20xx_bit());   // "20XX CPUs" too: playback replays it
    g_replay_stream.set_feature_options3(online ? 0u : h_game_options3());   // CPU training options, likewise
    if (!g_replay_stream.begin_slippi(start, codes, (int64_t)std::time(nullptr)))
      host::log("replay: cannot start native recording: %s", g_replay_stream.error().c_str());
    else if (online)
      host::log("replay: recording online game %u", g_online_games);
    return;
  }
  // Dolphin closes the file at the Game End event; what the game still sends is not recorded.
  if (!g_replay && (!g_replay_stream.active() || g_replay_stream.ended())) return;
  if (!g_replay_stream.append(command, payload, size))
    host::log("replay: cannot record event %02X: %s", command, g_replay_stream.error().c_str());
  if (!g_replay && command == 0x39) {
    // The native game's stable Game End record has the end method and four placements.
    // A unique zero placement is a winner; ambiguous endings stay incomplete.
    int winner = -1;
    if (size >= 6 && payload[0] == 2) {
      for (int slot = 0; slot < 4; ++slot) if (payload[2 + slot] == 0) {
        if (winner >= 0) { winner = -1; break; }
        winner = slot;
      }
    }
    host::publish_lobby_result(winner, size ? payload[0] : 0);
    write_replay_recording();
  }
}

void h_replay_finished() {
  if (!g_replay) {
    write_replay_recording();
    return; // An ordinary VS match returns to the menu, ready to record the next match.
  }
  host::log("replay: playback finished");
  if (g_savestates.active() && !g_replay_marks.empty())
    host::log("replay: %zu states were kept for jumping back (%zu MB)", g_savestates.kept(), g_savestates.pool_bytes() >> 20);
  replay_control::end();
  if (!g_replay_game_end.empty() && g_replay_stream.last_frame() >= g_replay->GetLatestIndex())
    h_replay_event(0x39, g_replay_game_end.data(), (uint32_t)g_replay_game_end.size());
  write_replay_recording();
  host::set_replay_viewing(false);   // played to its end: the launcher may start the next queued replay
  host::request_exit(0);
}
#endif

MuHostApi make_host() {
  MuHostApi h{};
  h.version = MU_HOST_API_VERSION;
  h.log = h_log; h.panic = h_panic; h.ticks = h_ticks; h.boot_time = h_boot_time; h.poll = h_poll;
  h.gx_fifo = h_gx_fifo; h.vi_configure = h_vi_configure; h.vi_set_next_framebuffer = h_vi_set_next_framebuffer;
  h.vi_flush = h_vi_flush; h.vi_retrace_count = h_vi_retrace_count; h.vi_next_field = h_vi_next_field;
  h.vi_set_black = h_vi_set_black; h.vi_wait_retrace = h_vi_wait_retrace;
  h.pad_read = h_pad_read; h.pad_rumble = h_pad_rumble;
  h.disc_entrynum = h_disc_entrynum; h.disc_file = h_disc_file; h.disc_read = h_disc_read;
  h.disc_status = h_disc_status; h.disc_id = h_disc_id;
  h.card_probe = h_card_probe; h.card_mount = h_card_mount; h.card_unmount = h_card_unmount; h.card_open = h_card_open;
  h.card_close = h_card_close; h.card_create = h_card_create; h.card_delete = h_card_delete; h.card_rename = h_card_rename;
  h.card_read = h_card_read; h.card_write = h_card_write; h.card_stat = h_card_stat; h.card_set_stat = h_card_set_stat;
  h.card_free_blocks = h_card_free_blocks; h.card_format = h_card_format;
  h.ai_init_dma = h_ai_init_dma; h.ai_start_dma = h_ai_start_dma; h.ai_set_sample_rate = h_ai_set_sample_rate;
  h.ai_set_stream_volume = h_ai_set_stream_volume; h.dsp_mail = h_dsp_mail; h.dsp_mail_pending = h_dsp_mail_pending;
  h.aram_base = h_aram_base; h.aram_size = h_aram_size; h.aram_dma = h_aram_dma;
  h.native_alloc = h_native_alloc; h.native_free = h_native_free;
  h.mem1_size = h_mem1_size; h.sound_mode = h_sound_mode; h.set_sound_mode = h_set_sound_mode;
  h.progressive_mode = h_progressive_mode; h.set_progressive_mode = h_set_progressive_mode;
  h.reset_code = h_reset_code; h.reset_switch = h_reset_switch; h.stop = h_stop;
  h.match_override = h_match_override;
  h.rng_seed_override = h_rng_seed_override; h.mark_match_start = h_mark_match_start;
  h.native_pose_capture_enabled = h_native_pose_capture_enabled;
  h.native_pose_snapshot = h_native_pose_snapshot;
  h.game_options = h_game_options;
  h.native_owner_tracking = h_native_owner_tracking;
  h.music_volume = h_music_volume;
  h.replay_start = h_replay_start; h.replay_frame = h_replay_frame;
  h.replay_stock_steal = h_replay_stock_steal; h.replay_event = h_replay_event;
  h.replay_finished = h_replay_finished;
  const char* draw_audit = std::getenv("MELEE_NATIVE_DRAW_AUDIT");
  const bool audit_enabled = draw_audit && std::strcmp(draw_audit, "1") == 0;
  gx::set_native_draw_audit(audit_enabled);
  if (audit_enabled) h.native_render_scope_event = h_native_render_scope_event;
  h.native_draw_identity = h_native_draw_identity;
  h.mod_flags = h_mod_flags;
  h.slippi_command = h_slippi_command;
  h.resim_phase = h_resim_phase;
  h.online_test_match = h_online_test_match;
  h.game_options2 = h_game_options2;
  h.hud_scales = h_hud_scales;
  h.hud_player = h_hud_player;
  h.vi_idle_step = h_vi_idle_step;
  h.game_options3 = h_game_options3;
  h.display_options = h_display_options;
  return h;
}

// A crash inside the game: the faulting address and the game's return addresses on the stack, as
// offsets into melee_game.dll (resolve with addr2line -e melee_game.dbg). The application's own
// crash report still runs afterwards.
LONG CALLBACK on_game_exception(EXCEPTION_POINTERS* info) {
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  if (code < 0x80000000u || code == 0xE06D7363u /* C++ exception */) return EXCEPTION_CONTINUE_SEARCH;
  const uint64_t base = GAME_IMAGE_BASE, end = base + host::game_image_size;
  const uint64_t rip = info->ContextRecord->Rip;
  const bool in_game = rip >= base && rip < end;
  if (!in_game) {
    // A bad indirect call has already left the DLL. Its return address still
    // identifies the game caller, and the unwind below starts from that frame.
    bool called_by_game = false;
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->NumberParameters >= 2 &&
        info->ExceptionRecord->ExceptionInformation[0] == 8) {
      __try {
        const uint64_t caller = *(const uint64_t*)info->ContextRecord->Rsp;
        called_by_game = caller >= base && caller < end;
      } __except (EXCEPTION_EXECUTE_HANDLER) { }
    }
    if (!called_by_game) return EXCEPTION_CONTINUE_SEARCH;
  }
  write_replay_recording();   // a --replay run keeps what it played up to the crash
  if (in_game)
    host::log("game crash %08lX at melee_game.dll+0x%llX", code, (unsigned long long)(rip - base));
  else
    host::log("game crash %08lX calling %016llX", code, (unsigned long long)rip);
  const CONTEXT& r = *info->ContextRecord;
  if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
    host::log("  %s address %016llX", info->ExceptionRecord->ExceptionInformation[0] == 8 ? "executing" :
              (info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading"),
              (unsigned long long)info->ExceptionRecord->ExceptionInformation[1]);
  host::log("  rax %016llX rbx %016llX rcx %016llX rdx %016llX", r.Rax, r.Rbx, r.Rcx, r.Rdx);
  host::log("  rsi %016llX rdi %016llX r8  %016llX r9  %016llX", r.Rsi, r.Rdi, r.R8, r.R9);
  // The real call chain, from the unwind tables GCC writes into the DLL (.pdata), so frames are
  // callers rather than whatever return-address-looking values happen to sit on the stack.
  CONTEXT ctx = r;
  for (int depth = 0; depth < 24; ++depth) {
    DWORD64 image = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image, nullptr);
    __try {
      if (fn) {
        void* handler_data = nullptr; DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx.Rip, fn, &ctx, &handler_data, &frame, nullptr);
      } else {
        ctx.Rip = *(const DWORD64*)ctx.Rsp;   // a leaf: the return address is on top
        ctx.Rsp += 8;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
    if (ctx.Rip == 0) break;
    if (ctx.Rip >= base && ctx.Rip < end) host::log("  stack: melee_game.dll+0x%llX", (unsigned long long)(ctx.Rip - 1 - base));
    else { host::log("  (leaves the game at %016llX)", (unsigned long long)ctx.Rip); break; }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

bool card_self_test(const char* directory) { return card_self_test_impl(directory); }

// --match <stage>:<p1>[:<p2>...], each player <kind>[/c<level>][/x<costume>], all numbers decimal
// or 0x-hex. For example "0x14:9:12/c9" is Onett, one human, one level 9 CPU.
bool set_online_test(const char* spec) {
  std::string s = spec ? spec : "";
  std::string mode = s.substr(0, s.find(':'));
  int id = mode == "unranked" ? 1 : mode == "direct" ? 2 : mode == "teams" ? 3 : -1;
  if (id < 0) return false;
  int character = 2, color = 0;
  size_t colon = s.find(':');
  if (colon != std::string::npos) {
    char* end = nullptr;
    character = (int)std::strtol(s.c_str() + colon + 1, &end, 10);
    if (end && *end == ':') color = (int)std::strtol(end + 1, &end, 10);
    if (!end || *end != '\0' || character < 0 || character > 25 || color < 0 || color > 5) return false;
  }
  g_online_test_mode = id;
  g_online_test_character = (uint8_t)character;
  g_online_test_color = (uint8_t)color;
  return true;
}

void set_slippi_menus(bool on) { g_slippi_menus_requested = on; }

void set_mod_directory(const char* path) {
  g_mod_layers.push_back({mods::LayerKind::Dir, std::filesystem::u8path(path)});
  g_mod_root_explicit = true;
}

void set_mod_iso(const char* path) {
  g_mod_layers.push_back({mods::LayerKind::Iso, std::filesystem::u8path(path)});
}

void set_mod_gci(const char* path) {
  g_mod_layers.push_back({mods::LayerKind::Gci, std::filesystem::u8path(path)});
}

bool set_mod_profile(const char* name) {
  mods::Profile profile;
  std::string error;
  if (!name || !*name || !mods::parse_profile(mods::profile_path(name), &profile, &error)) {
    std::fprintf(stderr, "%s\n", error.empty() ? "--mod-profile needs a name" : error.c_str());
    return false;
  }
  if (!g_mod_profile_name.empty()) { std::fprintf(stderr, "only one --mod-profile per run\n"); return false; }
  g_mod_profile_name = profile.name;
  mods::status().requested = name;
  // The profile's layers come first; command-line layers, wherever they were given, load after it.
  g_mod_layers.insert(g_mod_layers.begin(), profile.layers.begin(), profile.layers.end());
  return true;
}

const std::string& mod_fingerprint() { return g_mod_fingerprint; }
bool has_mod_layers() { return !g_mod_layers.empty(); }

bool set_match(const char* spec) {
  if (!spec || !*spec) return false;
  MuMatchOverride m{};
  for (auto& p : m.players) p.kind = -1;

  const char* p = spec;
  char* end = nullptr;
  const long stage = std::strtol(p, &end, 0);
  if (end == p || stage < 0) return false;
  m.stage = (int32_t)stage;
  p = end;

  size_t slot = 0;
  while (*p == ':') {
    ++p;
    if (slot >= sizeof m.players / sizeof m.players[0]) return false;
    const long kind = std::strtol(p, &end, 0);
    if (end == p || kind < 0) return false;
    m.players[slot].kind = (int32_t)kind;
    p = end;
    // Each player's options, in any order: /c<level> makes it a CPU, /x<costume> picks a costume.
    while (*p == '/') {
      const char opt = p[1];
      if (opt != 'c' && opt != 'x') return false;
      p += 2;
      const long v = std::strtol(p, &end, 0);
      if (end == p || v < 0) return false;
      if (opt == 'c') { m.players[slot].cpu = 1; m.players[slot].cpu_level = (int32_t)v; }
      else { m.players[slot].costume = (int32_t)v; }
      p = end;
    }
    ++slot;
  }
  if (*p != '\0' || slot == 0) return false;   // trailing junk, or no players at all

  g_match = m;
  g_match_set = true;
  return true;
}

#ifdef MELEE_NO_SLIPPI
void check_replay_content() {}
bool set_replay(const char*) { return false; }   // main refuses --replay before it gets here
#else
// --replay <file.slp>: play a Slippi replay and record it again (see h_replay_start above).
// The "modProfile" metadata a native recording made with mod layers carries (see
// NativeReplayStream::set_mod_profile); empty for the retail game and for other recorders.
std::string g_replay_mod_profile;
uint32_t read_replay_feature_options(const char* path, int word = 0) {   // 0 "muOptions", 1 "muOptions2", 2 "muOptions3"
  std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  static const char key1[] = "U\x09muOptionsl";
  static const char key2[] = "U\x0AmuOptions2l";
  static const char key3[] = "U\x0AmuOptions3l";
  const char* key = word == 2 ? key3 : word == 1 ? key2 : key1;
  const size_t key_len = word == 2 ? sizeof key3 - 1 : word == 1 ? sizeof key2 - 1 : sizeof key1 - 1;
  const auto at = std::search(bytes.begin(), bytes.end(), key, key + key_len);
  if (at == bytes.end() || bytes.end() - at < (std::ptrdiff_t)(key_len + 4)) return 0;
  const uint8_t* p = (const uint8_t*)&*(at + key_len);
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
std::string read_replay_mod_profile(const char* path) {
  std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  static const char key[] = "U\x0amodProfileSU\x40";
  const auto at = std::search(bytes.begin(), bytes.end(), key, key + sizeof key - 1);
  if (at == bytes.end() || bytes.end() - at < (std::ptrdiff_t)(sizeof key - 1 + 64)) return {};
  return std::string(at + (sizeof key - 1), at + (sizeof key - 1) + 64);
}

// A replay must play with the content it was recorded with: different fighter or stage files
// play a different game from the same inputs.
void check_replay_content() {
  if (!g_replay) return;
  if (!g_replay_mod_profile.empty() && g_replay_mod_profile != g_mod_fingerprint)
    host::die("replay: %s was recorded with mod content %s, but this run has %s; enable the same mods to play it",
              g_replay_path.c_str(), g_replay_mod_profile.substr(0, 16).c_str(),
              g_mod_fingerprint.empty() ? "the retail game" : g_mod_fingerprint.substr(0, 16).c_str());
  if (g_replay_mod_profile.empty() && !g_mod_fingerprint.empty())
    host::log("replay: %s records no mod content but mods are enabled; it may not play back as recorded",
              g_replay_path.c_str());
}

bool set_replay(const char* path) {
  g_replay_mod_profile = read_replay_mod_profile(path);
  g_replay_feature_options = read_replay_feature_options(path);
  g_replay_feature_options2 = read_replay_feature_options(path, 1);
  g_replay_feature_options3 = read_replay_feature_options(path, 2);
  g_replaying = true;
  if (g_replay_feature_options) host::log("replay: recorded with game options %08X", g_replay_feature_options);
  g_replay = Slippi::SlippiGame::FromFile(path);
  if (!g_replay || !g_replay->AreSettingsLoaded()) {
    host::log("replay: cannot read %s", path);
    g_replay.reset();
    return false;
  }
  g_replay_path = path;
  g_replay_game_start = read_replay_event(path, 0x36);
  g_replay_game_end = read_replay_event(path, 0x39);
  if (g_replay_game_start.empty()) { host::log("replay: %s has no Game Start event", path); g_replay.reset(); return false; }
  Slippi::GameSettings* s = g_replay->GetSettings();
  if (!g_replay_stream.begin(g_replay_game_start, s->geckoCodes)) {
    host::log("replay: cannot start recording: %s", g_replay_stream.error().c_str());
    g_replay.reset();
    return false;
  }
  g_replay_written = false;
  g_replay_stream.set_mod_profile(g_replay_mod_profile);
  std::array<uint32_t, Slippi::GAME_INFO_HEADER_SIZE> header = s->header;
  // Dolphin's prepareGameInfo: a player who is Sheik or Zelda gets that character in the header.
  for (int i = 0; i < 4; ++i) {
    if (!g_replay->DoesPlayerExist((int8_t)i)) continue;
    const uint8_t ext = s->players[(uint8_t)i].characterId;
    if (ext != 0x12 && ext != 0x13) continue;
    header[24 + 9 * i] = (header[24 + 9 * i] & 0x00FFFFFFu) | (uint32_t)ext << 24;
  }
  for (int i = 0; i < Slippi::GAME_INFO_HEADER_SIZE; ++i)
    for (int b = 0; b < 4; ++b) g_replay_start.game_info[4 * i + b] = (uint8_t)(header[i] >> (24 - 8 * b));
  g_replay_start.random_seed = s->randomSeed;
  g_replay_start.is_pal = s->isPAL;
  auto version = g_replay->GetVersion();
  g_replay_start.preload_ps = version[0] > 1 || (version[0] == 1 && version[1] > 2);
  g_replay_start.frozen_ps = s->isFrozenPS;
  g_replay_start.resync = std::getenv("MELEE_REPLAY_RESYNC") != nullptr;   // Legacy plays without resync too
  g_replay_start.codes = replay_codes(*s, &g_replay_start.ps_frozen_toggle);
  host::log("replay: %s, Slippi %s, frames %d..%d, stage %u, seed %08X, frozen PS %u (code toggle %u)%s",
            path, g_replay->GetVersionString().c_str(), Slippi::GAME_FIRST_FRAME, g_replay->GetLatestIndex(),
            (unsigned)s->stage, s->randomSeed, (unsigned)s->isFrozenPS, (unsigned)g_replay_start.ps_frozen_toggle,
            g_replay_start.resync ? ", resync on" : "");
  // The viewer's controls: this engine can keep states of the game and return to them.
  replay_control::Engine engine;
  engine.keep = replay_keep;
  engine.restore = replay_restore;
  replay_control::set_replay_path(path);
  replay_control::begin(Slippi::GAME_FIRST_FRAME, g_replay->GetLatestIndex(), engine);
  host::set_replay_viewing(true);
  return true;
}
#endif

bool reserve_memory() {
  // First, before anything else in the process can take the range: the game's 32-bit disc pointers
  // expect its memory where the console had it.
  void* mem1 = VirtualAlloc((void*)MEM1_BASE, MEM1_SIZE, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
  void* lc = VirtualAlloc((void*)LOCKED_CACHE_BASE, LOCKED_CACHE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (mem1 != (void*)MEM1_BASE || lc != (void*)LOCKED_CACHE_BASE) {
    char msg[200];
    std::snprintf(msg, sizeof msg, "Could not reserve the game's memory at 0x80000000 (%p) or 0xE0000000 (%p).", mem1, lc);
    MessageBoxA(nullptr, msg, "Melee Unlocked", MB_ICONERROR | MB_OK);
    return false;
  }
  host::ram = (uint8_t*)MEM1_BASE;
  host::ram_size = 64u << 20;   // MEM1 and the game image after it: everything GX can address
  return true;
}

int run(void (*shutdown)(int)) {
  g_shutdown = shutdown;
  gx::set_native_pose_capture_enabled(gx::authored_capture_enabled());
  host::init_state_digest();
  if (!host::aram) host::aram = (uint8_t*)std::calloc(0x02000000, 1);
  if (!host::cpu) host::cpu = new ppc::Context();   // only its timebase is used: the clock both builds share
  ax::set_memory({ax_native_rd16, ax_native_rd32, ax_native_wr16, ax_native_wr32,
                  host::aram, 0x02000000});
  ax::set_voice_trace(std::getenv("MELEE_TRACE_AX_SFX") ? trace_ax_voice
                                                        : nullptr);
  ax::set_frame_trace(std::getenv("MELEE_TRACE_AX_VOICES") ? trace_ax_frame : nullptr);
  audio_core::reset(g_audio);
  slippi::jukebox::set_disc_reader(jukebox_disc_read);
  mods::status().source_port = true;
  // Tests only: 20XX TE's features without mounting its save (its menu memory changes scripted runs).
  if (std::getenv("MELEE_TEST_TE_OWNED")) mods::status().te_owned = true;
  if (!read_fst()) host::die("this disc image has no readable file table. Use a clean, uncompressed Melee NTSC 1.02 ISO (a trimmed or compressed image will not work)");
  // A development game library built with the native Akaneia fighters exports this name; the shipped
  // one does not, and an Akaneia disc stays refused. Looked up without running any of its code.
  {
    const char* named = std::getenv("MELEE_GAME_DLL");
    if (HMODULE probe = LoadLibraryExA(named ? named : g_dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES)) {
      const bool native_akaneia = GetProcAddress(probe, "mu_ak_native_build") != nullptr;
      FreeLibrary(probe);
      if (native_akaneia) {
        allow_native_akaneia(true);
        host::log("mods: development game library with native Akaneia fighters; Akaneia discs are accepted");
      }
    }
  }
  load_mod_overlay();
  check_replay_content();
  load_cosmetics(g_replay || g_online_test_mode >= 0);
  {
    std::vector<gx::texpack::CosmeticCompanion> companions;
    for (const auto& item : host::cosmetics::active_companions())
      companions.push_back({item.kind, item.target_path, item.path});
    gx::texpack::set_cosmetic_companions(std::move(companions));
  }
  load_system_files(g_replay ? "--replay"
                    : g_online_test_mode >= 0 ? "--online-test"
                    : host::options.vanilla_game ? "--vanilla-game" : nullptr);
  apply_content_mode(-1);   // offline: the mod's files, if a profile is loaded
  // The native game reads the retail DOL's data and the retail file formats; a modded disc (patched
  // DOL, m-ex) is refused here with a message instead of failing somewhere inside the game.
  if (!host::disc_has_vanilla_dol())
    host::die("ISO DOL does not match vanilla Melee NTSC 1.02; the Source engine runs only the retail game (modded discs are not supported)");
  // Diagnostic builds of the game (for example the M0 maths audit) without replacing the shipped DLL.
  if (const char* dll = std::getenv("MELEE_GAME_DLL")) g_dll = dll;
  HMODULE module = LoadLibraryA(g_dll.c_str());
  if (!module) host::die("cannot load %s (error %lu)", g_dll.c_str(), GetLastError());
  if ((uintptr_t)module != GAME_IMAGE_BASE) host::die("%s loaded at %p, not at %llX", g_dll.c_str(), (void*)module, (unsigned long long)GAME_IMAGE_BASE);
  const auto* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)module + ((const IMAGE_DOS_HEADER*)module)->e_lfanew);
  host::game_image = (uint8_t*)module;
  host::game_image_size = nt->OptionalHeader.SizeOfImage;
  auto entry = (MuGameEntry)GetProcAddress(module, "mu_game_entry");
  user_gecko::set_native_writer((user_gecko::NativeWrite)GetProcAddress(module, "mu_user_gecko_write"));
  if (!entry) host::die("%s has no mu_game_entry", g_dll.c_str());
  static MuHostApi api = make_host();
  if (entry(&api, &g_game) != 0) host::die("%s refused host API version %u", g_dll.c_str(), api.version);
  if (g_game.version != MU_GAME_API_VERSION || !g_game.state_snapshot || !g_game.state_regions ||
      !g_game.state_exclusions || !g_game.snapshot_ranges || !g_game.practice)
    host::die("%s has game API version %u, expected %u", g_dll.c_str(), g_game.version, MU_GAME_API_VERSION);
  slippi::native_practice::set_native_bridge(g_game.practice);
  slippi::native_practice::set_content_bridge(prepare_practice_content);
  MuStateRegion game_regions[3]{};
  std::string region_error;
  const uint32_t region_count = g_game.state_regions(game_regions, 3);
  if (!validate_state_regions(game_regions, region_count, MEM1_BASE, MEM1_SIZE, &region_error))
    host::die("%s reported invalid native state regions: %s", g_dll.c_str(), region_error.c_str());
  host::log("source port: native state regions MEM1 %u, data %u, bss %u bytes",
            game_regions[0].size, game_regions[1].size, game_regions[2].size);
  std::vector<MuStateRegion> exclusions(g_game.state_exclusions(nullptr, 0));
  const uint32_t exclusion_count = g_game.state_exclusions(exclusions.data(), (uint32_t)exclusions.size());
  if (exclusion_count != exclusions.size() ||
      !validate_state_exclusions(game_regions, 3, exclusions.data(), exclusion_count, &region_error))
    host::die("%s reported invalid native state exclusions: %s", g_dll.c_str(),
              exclusion_count != exclusions.size() ? "count changed" : region_error.c_str());
  uint64_t excluded_bytes = 0;
  for (const auto& e : exclusions) excluded_bytes += e.size;
  host::log("source port: native state exclusions %u (%llu bytes)", exclusion_count,
            (unsigned long long)excluded_bytes);
  host::native_retrace = native_retrace;
  host::native_state_snapshot = g_game.state_snapshot;
  AddVectoredExceptionHandler(1, on_game_exception);
  gx::set_native_owner_source(true);
  // The L-cancel helper reads the native game's fighters through the game, not guest memory.
  lcancel::set_native_view([](MuLcancelView* out) { if (g_game.lcancel_view) g_game.lcancel_view(out); });
  host::log("source port: %s at %p, MEM1 %u MB at %08llX", g_dll.c_str(), (void*)module, MEM1_SIZE >> 20, (unsigned long long)MEM1_BASE);
  int code = 0;
  guarded([&] { code = g_game.run(); });
  log_native_pose_bridge_stats();
  gx::set_native_pose_capture_enabled(false);
  host::log("game returned %d", code);
  return code;
}

}  // namespace source_port
