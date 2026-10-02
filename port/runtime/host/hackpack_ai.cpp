// "20XX CPUs" on the Static Recomp: see hackpack_ai.h.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Execution, for the record: the redirected Fighter_procInput is interpreted from RAM; its `b base`
// at the entry site lands in the block, which is marked as RAM code so the interpreter follows it;
// the block's `bl` calls (HSD_Randi and friends) run their compiled twins; its last word, `b` back to
// the word after the entry site, is inside the redirected function, so interpretation resumes there;
// Slippi's recording cave later in the function captures the inputs the block chose, so a replay of
// such a match plays back exactly with the option off.
#include "hackpack_ai.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include "exi_slippi.h"
#include "host.h"
#include "mod_scan.h"
#include "slippi_online.h"
#include "slippi_playback.h"

namespace host::hackpack_ai {
namespace {
using namespace rules;

std::mutex g_mutex;
std::vector<uint8_t> g_blob;   // the verified block, or empty
Status g_status;
bool g_looked_after_scan = false;   // the look happened after the Mods folder scan had finished

// Per scene: the match's copy of the block and what this loader wrote at the entry site.
uint32_t g_base = 0;        // this scene's copy, 0 = none
uint32_t g_our_word = 0;    // the `b base` standing at the entry site, 0 = the game's own word
bool g_redirected_by_us = false;   // procInput runs from RAM because of this loader (not a mod's doing)
bool g_hooked = false;
uint32_t g_gct_lo = 0, g_gct_hi = 0;   // Slippi's code table, followed in RAM while procInput runs from RAM

bool forced_by_env() {
  static const bool on = [] { const char* v = std::getenv("MELEE_TEST_20XX_AI"); return v && *v && *v != '0'; }();
  return on;
}

// The Mods folder result, then (tests only) MELEE_TEST_20XX_AI_DISC=<disc image> for runs that skip the scan.
void look_for_disc() {
  namespace mods = source_port::mods;
  Status s;
  s.looked = true;
  std::vector<uint8_t> blob;
  std::vector<std::filesystem::path> candidates;
  const mods::PanelView view = mods::panel_view();
  for (const auto& d : view.result.items)
    if (d.kind == mods::Kind::HackPack) candidates.push_back(d.path);
  if (candidates.empty())
    if (const char* test_disc = std::getenv("MELEE_TEST_20XX_AI_DISC"); test_disc && *test_disc)
      candidates.push_back(std::filesystem::u8path(test_disc));
  for (const auto& path : candidates) {
    s.disc_found = true;
    s.disc_path = path.u8string();
    std::vector<uint8_t> bytes;
    std::string why;
    if (!mods::read_disc_file(path, "/ai_engine.bin", &bytes)) { s.message = "The 20XX Hack Pack disc under Mods has no readable AI block."; continue; }
    if (!verify_blob(bytes, &why)) { s.message = "The 20XX Hack Pack disc under Mods has an unknown AI block (" + why + ")."; continue; }
    blob = std::move(bytes);
    s.blob_ok = true;
    s.message.clear();
    break;
  }
  if (!s.disc_found) s.message = "Install the 20XX Hack Pack under Mods";
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_blob = std::move(blob);
    g_status = s;
    g_looked_after_scan = view.scanned;
  }
  if (s.blob_ok) log("20XX CPUs: AI block read from %s (%u bytes, hash verified)", s.disc_path.c_str(), kBlobSize);
  else if (s.disc_found) log("20XX CPUs: %s; CPUs play as usual", s.message.c_str());
  else log("20XX CPUs: 20XX Hack Pack disc not under Mods; CPUs play as usual");
}

bool wanted() { return g_cpu_20xx.load(std::memory_order_relaxed) || forced_by_env(); }

bool offline_session() {
  if (slippi::playback::enabled()) return false;                 // a replay plays back its recorded inputs
  if (slippi::online::session_mode() >= 0) return false;         // matchmaking, the online character select, the match
  if (slippi::online::is_online_match() || slippi::online::in_online_menus()) return false;
  if (mod_disc_active()) return false;                           // a mod boot disc runs its own code
  return true;
}

std::vector<uint8_t> blob_copy() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_blob;
}

// The match's copy from the last scene is gone with that scene's heap: forget it, and put the game's
// own word back at the entry site so nothing points into freed memory even for a moment (procInput
// may keep running from RAM: the retail word interprets exactly as it compiles).
void forget_last_copy() {
  if (g_our_word && rd32(kEntrySite) == g_our_word) wr32(kEntrySite, kEntryWord);
  g_our_word = 0;
  if (!g_base) return;
  ppc::restore_dispatch_range(g_base, g_base + kBlobSize);
  ppc::remove_ram_code_range(g_base, g_base + kBlobSize);
  g_base = 0;
}

// The retail word back at the entry site, and procInput compiled again when this loader sent it to RAM.
void restore_entry_site() {
  if (g_our_word && rd32(kEntrySite) == g_our_word) wr32(kEntrySite, kEntryWord);
  g_our_word = 0;
  if (g_redirected_by_us) {
    ppc::undo_redirect(kProcInput);
    g_redirected_by_us = false;
  }
  if (g_gct_hi) ppc::remove_ram_code_range(g_gct_lo, g_gct_hi);
  g_gct_lo = g_gct_hi = 0;
}

// On the simulation thread, after the game's own StartMelee: the scene's object heap exists.
void apply_for_match(ppc::Context& c, uint8_t* m) {
  forget_last_copy();
  if (!wanted() || !offline_session()) {
    if (g_redirected_by_us) {
      restore_entry_site();
      log("20XX CPUs: not in effect for this match (%s); the game's own CPUs", wanted() ? "online or replay" : "off");
    }
    return;
  }
  std::vector<uint8_t> blob = blob_copy();
  if (blob.empty()) {
    static bool said = false;
    if (!said) { said = true; log("20XX CPUs: on, but no AI block is loaded; CPUs play as usual"); }
    return;
  }
  const uint32_t word = rd32(kEntrySite);
  if (!entry_site_free(word, g_our_word)) {
    log("20XX CPUs: another code owns %08X (%08X); CPUs play as usual", kEntrySite, word);
    return;
  }
  // The block's home for this match, from the scene's object heap, as the pack itself does.
  const ppc::Context saved = c;
  c.r[3] = kBlobSize;
  c.lr = 0;
  ppc::call(c, m, kMemAlloc);
  const uint32_t base = c.r[3];
  const uint64_t tb = c.tb;
  c = saved;
  c.tb = tb;
  if (!base || (base & 3) || !try_ptr(base, kBlobSize)) {
    log("20XX CPUs: no memory for the AI block this match (%08X); CPUs play as usual", base);
    return;
  }
  const uint32_t branch = encode_branch(kEntrySite, base);
  if (!branch || !patch_return_branch(blob, base)) {
    log("20XX CPUs: the AI block at %08X is out of branch range; CPUs play as usual", base);
    return;
  }
  std::memcpy(ptr(base, kBlobSize), blob.data(), kBlobSize);
  mark_ram_write(base, kBlobSize);
  // The block is RAM code: the interpreter follows it, and a compiled twin at the same address (a
  // Slippi cave translated for a table that a mod moved here) is never taken instead.
  ppc::disable_dispatch_range(base, base + kBlobSize);
  ppc::add_ram_code_range(base, base + kBlobSize);
  g_base = base;
  for (const auto& setting : kDefaults) wr32(setting.address, setting.value);
  wr32(kEntrySite, branch);
  g_our_word = branch;
  if (!ppc::runs_from_ram(kProcInput)) {
    if (ppc::redirect_function_at(kProcInput)) {
      g_redirected_by_us = true;
      // Slippi's codes inside procInput run from the table in RAM too, as in a mod session: their
      // compiled twins would finish the function compiled and return through a link register that
      // is not the function's own, which never unwinds.
      if (!g_gct_hi && slippi::gct_range(&g_gct_lo, &g_gct_hi)) ppc::add_ram_code_range(g_gct_lo, g_gct_hi);
    } else {
      wr32(kEntrySite, kEntryWord);
      g_our_word = 0;
      log("20XX CPUs: Fighter_procInput cannot run from RAM; CPUs play as usual");
      return;
    }
  }
  slippi::discard_current_replay();
  log("20XX CPUs: AI block loaded at %08X for this match", base);
}

// The game calls its match start (fn_8016E730) directly from three scene entries, and those are only
// ever called through the scene tables: the dispatch table sees every one of them. Each runs the
// game's own entry first, compiled as always.
constexpr uint32_t kSceneEntries[] = {0x8016E934u, 0x8016EBC0u, 0x8016EC28u};   // VS, Sudden Death, Training
ppc::Fn g_scene_entry[3] = {};
// Tests only (MELEE_TEST_20XX_AI_CPUS=1, hidden runs): every human in the match about to start becomes
// a level 9 CPU, so a script that picks two characters gives a CPU match.
void test_make_cpus(uint32_t start_melee) {
  static const bool on = [] { const char* v = std::getenv("MELEE_TEST_20XX_AI_CPUS"); return v && *v == '1'; }();
  if (!on || !try_ptr(start_melee, 0x60 + 4 * 0x24)) return;
  for (uint32_t i = 0; i < 4; ++i) {
    const uint32_t player = start_melee + 0x60 + i * 0x24;   // StartMeleeData.players[i]
    if (rd8(player + 1) != 0) continue;                       // slot_type: human only
    wr8(player + 1, 1);
    wr8(player + 0xF, 9);                                     // cpu_level
  }
}

template <int N> void scene_entry_hooked(ppc::Context& c, uint8_t* m) {
  test_make_cpus(c.r[3]);
  g_scene_entry[N](c, m);
  apply_for_match(c, m);
}
}  // namespace

void boot() {
  look_for_disc();
  if (g_hooked) return;
  const ppc::Fn hooks[3] = {scene_entry_hooked<0>, scene_entry_hooked<1>, scene_entry_hooked<2>};
  for (int i = 0; i < 3; ++i) {
    ppc::Fn previous = ppc::set_hook(kSceneEntries[i], hooks[i]);
    if (!previous) {
      ppc::set_hook(kSceneEntries[i], nullptr);   // never leave a hook with nothing to call behind it
      log("20XX CPUs: match-start hook not installed at %08X (not in the dispatch table)", kSceneEntries[i]);
      continue;
    }
    g_scene_entry[i] = previous;
  }
  g_hooked = true;
}

void reload() { look_for_disc(); }

Status status() {
  bool looked = false, before_scan = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    looked = g_status.looked;
    before_scan = !g_status.blob_ok && !g_looked_after_scan;
  }
  // The first ask (the standalone settings window never boots the game), or the Mods folder scan
  // finished after the last look: look again, once.
  if (!looked || (before_scan && source_port::mods::panel_view().scanned)) look_for_disc();
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_status;
}

bool effective() {
  if (!wanted() || !offline_session()) return false;
  std::lock_guard<std::mutex> lock(g_mutex);
  return !g_blob.empty();
}

}  // namespace host::hackpack_ai
