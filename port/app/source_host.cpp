// The source port inside the full application: the same window, renderers (D3D12 with DLSS, D3D11),
// sub-frame presentation, input, settings panel and pacing as the recompiled build, with the game
// itself coming from melee_game.dll (the decompiled sources built natively, see sourceport/game)
// instead of the translated guest. main.cpp calls reserve_memory() first thing and run() where the
// recompiled build would enter __start.
//
// What still differs from the recompiled build is listed where it happens: no memory card yet
// (M10), Slippi's game side not present (M12).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "test_env.h"
#include "layer_fingerprint.h"
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_paths.h"
#include "yougame_agent_test.h"
#include "handoff_state.h"
#endif
#include "source_host.h"
#include "yougame_update.h"
#include "yougame_console_card.h"
#include "friends_handoff_join.h"
#include "friends_handoff_transfer.h"
#include "friends_handoff_capture.h"
#include "handoff_state.h"
#include <windows.h>
#ifdef _WIN32
#include <shellapi.h>   // CommandLineToArgvW: an arena viewer's options
#endif
#ifndef _WIN32
#include <dlfcn.h>
#ifdef __APPLE__
#include "../mac/elf_loader.h"
#include "../mac/platform_paths.h"
#include "../mac/context_registers.h"
#include <csetjmp>
#else
#include <link.h>
#endif
#include <csignal>
#endif
#include <algorithm>
#include <atomic>
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
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "host.h"
#include "sim_work.h"
#include "rollback_perf.h"
#include "yougame_net_test.h"
#include "audio.h"
#include "console_sound_mode.h"
#include "audio_core.h"
#include "ax_ucode.h"
#include "gx_core.h"
#include "gx_retro.h"
#include "gx_shader.h"
#include "render_options.h"
#include "training_overlay.h"
#include "ghost_ai.h"
#include "practice_cpu.h"
#include "lcancel.h"
#include "offline_input_delay.h"
#include "disc_archive.h"
#include "user_gecko.h"
#include "mu_host.h"
#include "mu_host_aram.h"
#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
#include "mu_squad_wire.h"
#include "squad_costume_rule.h"
#endif
#include "../runtime/abi/mu_tourney.h"
#include "ppc.h"
#include "render_observer.h"
#include "native_state_layout.h"
#include "native_savestate.h"
#include "yougame_css_origin.h"
#include "yougame_css_join.h"
#include "lab_view.h"
#include "walter_egg.h"
#include "gecko_session.h"
#include "../runtime/abi/mu_look_snapshot.h"
// Both builds: the neutral netplay state. In the build without the Slippi layer it also answers the
// Slippi names; in the normal build a peer-to-peer session (--p2p-*) is found through it.
#include "netplay_state.h"
#include "yougame_link.h"
#include "online_rows.h"
#include "yougame_mode_policy.h"
#include "yougame_memcard.h"
#include "yougame_console_engine.h"
#include "yougame_console_entry.h"
#include "yougame_doubles.h"
#include "yougame_friends_lobby.h"
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_fight.h"
#endif
#include "yougame_exit_reason.h"
#ifndef MELEE_NO_SLIPPI
#include "native_practice.h"
#include "jukebox.h"
#include "slippi_playback.h"
#include "slippi_online.h"
#include "online_test_quit.h"
#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
#include "squad_test_spec.h"
#endif
#include "source_p2p.h"
#include "exi_slippi.h"
#include "native_slippi_bridge.h"
#if defined(YOUGAME_AGENT_TEST_HOOK) || (defined(YOUGAME_RANKED_BRANCH) && YOUGAME_RANKED_BRANCH)
#include "yougame_ranked_cpu.h"
#endif
#include "native_replay_stream.h"
#include "slippi_spectate.h"
#include "slippi_arena.h"
#include "slippi_net.h"
#include "native_recording_codes.h"
#include "slippilib/SlippiGame.h"
#endif
#include "replay_bar.h"     // both builds: the on-screen labels
#include "card_backup.h"    // both builds: save backups
#include "source_mod_overlay.h"
#include "mod_profile.h"
#include "net_trace.h"
#include "replay_control.h"
#include "replay_coach.h"
#include "replay_scan.h"
#include "mod_scan.h"
#include "hackpack_source.h"
#include "maker_source.h"
#include "pack_skin_rule.h"
#include "disc_skin_scan.h"
#include "cosmetic_mods.h"
#include "stage_dat_safety.h"
#include "pack_audio_safety.h"
#include "texture_pack.h"
#include "window.h"
#include "console_input.h"

// The settings panel's port-code options (pc_settings.cpp); the native game reads them through
// MuHostApi.game_options.
namespace gecko { extern int option_screen_shake_percent; extern bool option_no_screen_shake; extern bool option_pal_stock_icons; extern bool option_widescreen; }

namespace source_port {
namespace {
bool g_console_boot = false; // retain pad/card isolation after a session ends
std::filesystem::path g_console_card;
int g_console_host_pad = -1; // physical host pad carried from the solo Friends CSS
yougame::console::Pads console_hardware() {
  host::PadState raw[4]{};
  host::input_read_console_devices(raw); // publishes UI from this one read, before local capture
  // The relaunched process still uses console device routing after returning to local CSS.
  // Its UI snapshot skips ordinary Friends input; restore Z/B only after lockstep ends.
  if (!yougame::console::console_active()) for (int port = 0; port < 4; ++port) {
    if (
#ifdef MELEE_DOUBLES
        yougame::doubles::ui.poll(port,raw[port].err == 0 && !host::window_input_captured(),raw[port].button) ||
#endif
        yougame::console::entry.poll(port,raw[port].err == 0 && !host::window_input_captured(),raw[port].button)) {
      const auto err = raw[port].err; raw[port] = {}; raw[port].err = err;
    }
#ifdef MELEE_DOUBLES
    else raw[port].button &= static_cast<uint16_t>(~yougame::doubles::ui.carried_over(port));
#endif
  }
#ifdef YOUGAME_AGENT_TEST_HOOK
  host::input_console_script(raw,yougame::console::console_engine().config().my_port);
#endif
  if (g_console_host_pad >= 0) {
    // Engine::first maps this physical controller onto the host's frozen virtual port.
    for (int port = 0; port < 4; ++port) if (port != g_console_host_pad) raw[port].err = -1;
  }
  static host::console_input::Capture capture;
  capture.apply(raw, host::window_input_captured());
  yougame::console::Pads pads{};
  for (int i = 0; i < 4; ++i) {
    pads[i].button = raw[i].button;
    pads[i].stick_x = raw[i].stick_x; pads[i].stick_y = raw[i].stick_y;
    pads[i].sub_x = raw[i].sub_x; pads[i].sub_y = raw[i].sub_y;
    pads[i].trigger_l = raw[i].trig_l; pads[i].trigger_r = raw[i].trig_r;
    pads[i].analog_a = raw[i].analog_a; pads[i].analog_b = raw[i].analog_b;
    pads[i].err = raw[i].err;
  }
  return pads;
}

// The console's addresses. On Apple silicon the whole low 4 GB is off limits to a native process, so
// they sit at a 4 GB-aligned base there (port/mac/elf_loader.cpp, sourceport/game/include/mu_window.h);
// everywhere else, Rosetta included, the base is 0.
#if defined(__APPLE__) && defined(__aarch64__)
const uintptr_t CONSOLE_BASE = mac_elf::window_base();
#else
constexpr uintptr_t CONSOLE_BASE = 0;
#endif
const uintptr_t MEM1_BASE = CONSOLE_BASE + 0x80000000u;
constexpr uint32_t MEM1_SIZE = 40u << 20;   // the console's 24 MB, grown for 8-byte pointers; the game image follows
const uintptr_t LOCKED_CACHE_BASE = CONSOLE_BASE + 0xE0000000u;
constexpr uint32_t LOCKED_CACHE_SIZE = 16u << 10;
// Right after MEM1, so the game's own statics (the font atlas, static textures) have a physical
// address the GX texture and display-list registers can hold: 26 bits, the first 64 MB.
const uintptr_t GAME_IMAGE_BASE = CONSOLE_BASE + 0x82800000u;
// The end of what GX can address: the game image must fit below it. The range itself is held from
// startup by g_game_image_reservation (below); in the YouGame sandbox, with bottom-up ASLR on, the
// process's early allocations (the GPU driver's among them) otherwise land anywhere in the low 4 GB.
const uintptr_t GAME_IMAGE_HOLD_END = CONSOLE_BASE + 0x84000000u;

// The game library must load at GAME_IMAGE_BASE. Everything the host does before loading it (mod
// scans, cosmetics, the music pack index) allocates, and a heap block placed there moved the library
// elsewhere and stopped the Source Port at startup. The range is held from before main and given
// back the moment the library is loaded.
struct GameImageReservation {
  void* base = nullptr;
  GameImageReservation() {
    // Generous: the image's own size is checked again after the load.
#ifndef __APPLE__
    base = VirtualAlloc((void*)GAME_IMAGE_BASE, 0x04000000u, MEM_RESERVE, PAGE_NOACCESS);
#endif
  }
  void release() {
    if (base) VirtualFree(base, 0, MEM_RELEASE);
    base = nullptr;
  }
};
GameImageReservation g_game_image_reservation;

MuGameApi g_game{};
std::filesystem::path g_loaded_game_path;
#ifdef __APPLE__
mac_elf::Image g_mac_image;
std::jmp_buf g_mac_run_boundary;
bool g_mac_running = false;
std::string g_dll = (mac_host::resources() / "melee_game.so").string();
#else
#ifdef _WIN32
// The compatibility build (melee_source_compat.exe) loads its own SSE2 game library, as on Linux.
#ifndef MELEE_CPU_BASELINE_NAME
#define MELEE_CPU_BASELINE_NAME "AVX2" // standalone syntax/probe builds; CMake supplies the release value
#endif
std::string g_dll = std::strcmp(MELEE_CPU_BASELINE_NAME, "AVX2") == 0 ? "melee_game.dll" : "melee_game_compat.dll";
#elif defined(MELEE_GAME_LIBRARY_NAME)
std::string g_dll = MELEE_GAME_LIBRARY_NAME;   // the compatibility build loads its own game library
#else
std::string g_dll = "melee_game.so";
#endif
#endif
audio_core::State g_audio;
// A console session's End moves the timebase forward to the engine clock (EngineHooks::host_catch_up).
// End can run off the game thread, so the game thread takes the jump at its next poll or retrace; a
// session that has begun again by then runs on its own clock, and the jump is dropped.
static std::atomic<uint64_t> g_console_catch_up{0};
static void console_host_catch_up(uint64_t ticks) { g_console_catch_up.fetch_add(ticks, std::memory_order_acq_rel); }
static void take_console_catch_up() {
  if (!g_console_catch_up.load(std::memory_order_relaxed)) return;
  const auto ticks = g_console_catch_up.exchange(0, std::memory_order_acq_rel);
  if (!ticks || !host::cpu) return;
  if (yougame::console::console_active()) {
    host::log("console: host clock jump of %llu ms dropped (a new session began)", (unsigned long long)(ticks / 40500));
    return;
  }
  host::shift_time(ticks);
  if (g_audio.running) g_audio.next_tb += ticks;
  if (g_audio.awaiting_block) g_audio.awaiting_since_tb += ticks;
  host::log("console: host clock moved forward %llu ms to the session's alarm clock", (unsigned long long)(ticks / 40500));
}

// Native online play (host API 13). While the game re-simulates after a rollback load, the
// machine stands still: no time, alarms, audio, retraces or presentation.
native_savestate::Engine g_savestates;
// Origin storage is host-owned; never inside a captured game span or a wire message.
struct CssOriginRuntime {
  native_savestate::Snapshot boot, excluded, aram, allocations;
  std::vector<MuStateRegion> regions;
  std::vector<native_savestate::Span> spans, excluded_spans;
  std::function<void()> restore_gpu, restore_clock;
  audio_core::State audio;
  std::map<void*, uint32_t> live_allocations, boot_allocations;
  std::vector<uint8_t> card, pending_card;
  uint64_t frame = 0;
  int resume = 0;
  bool armed = false, pending = false, catching_up = false, requested_css = false;
  bool join_boot = false;
  double reset_ms = 0;
  std::chrono::steady_clock::time_point catchup_at;
#ifdef YOUGAME_AGENT_TEST_HOOK
  bool state_proof = false;
  MuCssState proof_state{};
  uint64_t state_hash = 0;
  bool proof = false;
  int proof_stage = 0;
  unsigned retry_bodies = 0;
  bool proof_menu_seen = false, proof_css_return_seen = false;
  uint64_t fresh_hash = 0;
#endif
} g_css_origin;
#ifdef MELEE_DOUBLES
CssOriginRuntime g_doubles_origin;
#endif
bool g_origin_card_readonly = false;
void origin_mount_card(const std::vector<uint8_t>& bytes);
int css_origin_tick();
#ifdef MELEE_DOUBLES
int doubles_tick();
int doubles_checkpoint();
std::shared_ptr<yougame::doubles::Launch> g_doubles_launch;
#endif
int css_join_tick();
int32_t h_console_origin(int32_t operation);
unsigned g_handoff_disc_depth = 0;
unsigned (*g_handoff_callbacks_pending)() = nullptr;
// The game's CPU fighters' inputs as controllers (shim/mu_lcancel.c mu_cpu_pads_v1), for the input display.
int (*g_cpu_pads)(int out[4][8]) = nullptr;
void friends_handoff_tick();
struct FriendsHandoffRuntime {
  handoff_state::Identity identity;
  std::vector<native_savestate::Span> spans;
  std::shared_ptr<yougame::console::handoff_join::Channel> channel;
  uint64_t frame = 0, request_at = 0, css_bodies = 0, waited_bodies = 0;
  unsigned serial = 0;
  bool ready = false, css_requested = false;
} g_friends_handoff;
#ifdef YOUGAME_AGENT_TEST_HOOK
// Gate 1 owns a full MEM1 + image data/bss capture, independent of rollback's exclusions.
// The raw dump must include process-local words so identical-input runs can discover them.
struct HandoffRuntime {
  bool ready = false, failed = false, boot_requested = false, css_seen = false, acted = false;
  uint64_t frame = 0, css_start = 0, action_frame = 0;
  handoff_state::Identity identity;
  std::vector<native_savestate::Span> spans;
  std::vector<handoff_state::Range> keep;
  std::vector<native_savestate::Span> compared;
} g_handoff;
void handoff_tick();
// agent-test bootMatch (shim/mu_boot_match.c): one ask, once the main menu has settled.
struct BootMatchRun { uint32_t menu_bodies = 0; bool asked = false, requested = false, started = false; } g_boot_match;
void boot_match_tick();
#endif
bool g_resim = false;
bool gl_costs_enabled() {
  // First use follows session-policy setup; global initialization precedes it.
  static const bool on = [] { const char* v = host::test_env::getenv("MELEE_GL_COSTS"); return v && *v == '1'; }();
  return on;
}
uint64_t g_resim_gx_bytes = 0;
uint64_t g_resim_phases = 0, g_resim_polls = 0, g_resim_vi_waits = 0;
// --online-test: a Slippi mode id (the harness enters the match without menus), or -1.
int g_online_test_mode = -1;
uint8_t g_online_test_character = 2, g_online_test_color = 0;
bool g_online_test_started = false;
#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
MuSquadSide g_online_test_squad{};
bool g_online_test_squad_cancel = false;
#endif
#ifdef YOUGAME_AGENT_TEST_HOOK
bool g_agent_fight_match = false; // bounded by the native online Game Start / Game End events
#endif
// --online-test ranked plays one whole best-of-3 set (YouGame mode): after each game the harness
// locks the same character in again, and the run ends once the set's room is done.
struct RankedHarness {
  yougame::online_test::QuitAfter quit;
  uint64_t game_end_ms = 0;      // the last game's Game End record (0: in the match scene since)
  bool reentry_asked = false;    // asked the game back into Debug VS's match scene for the next game
  uint64_t done_ms = 0;          // the set's room closed
  bool exiting = false;
};
RankedHarness g_ranked_harness;
constexpr int32_t kGameModeDebugVs = 0x0E;   // GM_DEBUG_VS: the harness's (and Slippi playback's) major

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
std::unordered_map<uint32_t, std::string> g_music_paths;  // immutable HPS offset -> disc path
std::vector<uint8_t> g_fst_raw;                      // the disc's table as read, for the cosmetic layer
#if MU_SQUAD
std::unordered_map<std::string, int32_t> g_disc_paths;  // the disc's own table: path -> its entry there
#endif

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
// A pack costume with the standard skeleton (a look-only pack such as a one-click mod look) also takes
// the player's own Mods-tab skin: that skin was checked against the standard costume, which this file
// matches. A pack that changes the skeleton keeps its costume, as before.
bool pack_costume_takes_skins(uint32_t entry) { return g_pack_skin_served.count((int32_t)entry) != 0; }
// A pack stage rebuilt around the same stage (Animelee CE's legal stages): online it is served as the
// original stage file with the pack's meshes drawn over it (stage_online_form, stage_graft.h), which
// the gameplay comparison proves plays the same. Pack entry -> the entry serving that grafted copy,
// kept in memory in the top of the system-file range.
std::unordered_map<int32_t, int32_t> g_pack_online_entry;
struct PackOnlineFile { uint32_t start; std::vector<uint8_t> data; };
std::vector<PackOnlineFile> g_pack_online_files;
constexpr uint32_t kPackOnlineBase = 0x6C000000u;   // up to the overlay at 0x70000000; system files stay below
uint32_t g_pack_online_next = kPackOnlineBase;
bool read_pack_online(uint32_t offset, void* dst, uint32_t size, bool* ok) {
  if (offset < kPackOnlineBase || offset >= 0x70000000u || g_pack_online_files.empty()) return false;
  for (const auto& f : g_pack_online_files) {
    if (offset < f.start || offset >= f.start + (uint32_t)f.data.size()) continue;
    // Reads come in 32-byte rounded chunks: the tail past the file reads as zeros, within its slot.
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
  return false;
}
// Buttons held on any controller at the last pad read, for the stage pick of a disc with alternate
// stages (host::cosmetics::set_stage_button_probe).
std::atomic<uint32_t> g_held_buttons{0};
uint32_t held_buttons() { return g_held_buttons.load(std::memory_order_relaxed); }
// True for a stage file that is no stage on its own: it has a stage map and no collision data. An
// m-ex stage stub is one (its code, in the file, loads the real stage from a folder of alternates).
// The Source Port runs no code from a file, so serving it gave a stage nothing stands on.
bool stage_file_is_stub(const std::vector<uint8_t>& b) {
  auto word = [&](size_t at) { return (uint32_t)b[at] << 24 | (uint32_t)b[at + 1] << 16 | (uint32_t)b[at + 2] << 8 | b[at + 3]; };
  if (b.size() < 0x20 || word(0) != b.size()) return false;
  const uint64_t data = word(4), relocations = word(8), roots = word(12), references = word(16);
  const uint64_t table = 0x20 + data + relocations * 4, strings = table + (roots + references) * 8;
  if (!roots || roots > 1024 || references > 1024 || strings > b.size()) return false;
  bool map = false, collision = false;
  for (uint64_t i = 0; i < roots; ++i) {
    const uint64_t name = strings + word((size_t)(table + i * 8 + 4));
    if (name >= b.size()) return false;
    const char* text = (const char*)b.data() + name;
    const size_t length = strnlen(text, b.size() - (size_t)name);
    const std::string symbol(text, length);
    map |= symbol == "map_head";
    collision |= symbol == "coll_data";
  }
  return map && !collision;
}
// The map parts of a stage file (map_head's table of 0x34-byte entries): for each, whether its model
// joint is there (a pointer is there when the archive relocates it). False when the file has no
// readable map_head.
bool stage_map_parts(const std::vector<uint8_t>& b, std::vector<bool>* parts) {
  auto word = [&](uint64_t at) { return (uint32_t)b[at] << 24 | (uint32_t)b[at + 1] << 16 | (uint32_t)b[at + 2] << 8 | b[at + 3]; };
  parts->clear();
  if (b.size() < 0x20 || word(0) != b.size()) return false;
  const uint64_t data = word(4), relocations = word(8), roots = word(12), references = word(16);
  const uint64_t table = 0x20 + data + relocations * 4, strings = table + (roots + references) * 8;
  if (!roots || roots > 1024 || references > 1024 || strings > b.size()) return false;
  uint64_t head = UINT64_MAX;
  for (uint64_t i = 0; i < roots; ++i) {
    const uint64_t name = strings + word(table + i * 8 + 4);
    if (name >= b.size()) return false;
    const char* text = (const char*)b.data() + name;
    if (std::string(text, strnlen(text, b.size() - (size_t)name)) == "map_head") head = word(table + i * 8);
  }
  if (head == UINT64_MAX || 0x20 + head + 0x10 > 0x20 + data) return false;
  const uint64_t entries = word(0x20 + head + 8), count = word(0x20 + head + 0x0C);
  if (count > 256 || 0x20 + entries + count * 0x34 > 0x20 + data) return false;
  std::unordered_set<uint32_t> relocated;
  relocated.reserve((size_t)relocations);
  for (uint64_t i = 0; i < relocations; ++i) relocated.insert(word(0x20 + data + i * 4));
  for (uint64_t i = 0; i < count; ++i) parts->push_back(relocated.count((uint32_t)(entries + i * 0x34)) != 0);
  return true;
}
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
#if MU_SQUAD
  g_disc_paths = g_paths;
#endif
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
std::vector<mods::Layer> g_explicit_mod_layers;
std::vector<std::filesystem::path> g_mod_gcis;
std::string g_mod_fingerprint;                 // content identity of the enabled layers, "" = vanilla
std::string g_direct_fingerprint;              // what Direct compares when not the content identity (Melee Maker)
bool g_cosmetic_pack = false;                  // a verified official look; standard gameplay online
bool g_training_only = false;                  // only training mods on: online plays the standard game
std::filesystem::path g_profile_card;          // the profile's own card folder, "" = the ordinary card
std::vector<mods::CardImport> g_card_imports;

// Known packs, by the content fingerprint of their layer (see iso_layer_fingerprint). The 20XX TE line
// only matches the untouched official file; any 20XX TE v2d r4 save, including one Melee has saved
// records into, is recognized by its code payload (mods::check_te_file, used below and by the scan).
struct KnownPack { const char* fingerprint; const char* name; };
const KnownPack kKnownPacks[] = {
  {"9a1a48b999a2f8f4deaee2554e5f177cb265666b95676a508406db80b181246b", "Akaneia 1.0.1"},
  {"61834b65d921668ff11702c458a1ae6946172b0a4c8321a47e2c2ddff9aef51d", "20XX TE 2d r4"},
  // An m-ex build with its own camera, hitbox and HUD codes (run-source/arcade-0980/STUDY.md): its
  // files load as they are; its codes are native (shim/mu_arcade.c and the Arcade look switches).
  {"23859e58d0f6153bbc9a813f4eac2b3ae1b69e90d5e5a0ef1f57829b1ed588ff", "2D-style build 2.0"},
  // Its later demo (the camera code and stage reskins published with it): the same main.dol and
  // code list, except three draw-only additions (ledge snap boxes and a few move-specific hitbox
  // shapes) in place of the five always-on hitbox switches, which the Arcade look's switches cover.
  {"59933964cb939eb7e32c05f4b9cdfb332dd12043a3c82ccb9d1f204c7a314891", "2D-style build demo"},
};
constexpr const char kArcadePackName[] = "2D-style build";
bool arcade_pack_name(const char* name) { return std::strncmp(name, kArcadePackName, sizeof kArcadePackName - 1) == 0; }

// Session identity must be derived from this launch's loaded layer bytes.
std::string iso_layer_fingerprint(const std::filesystem::path& iso, const std::string& profile) {
  const auto begin = std::chrono::steady_clock::now();
  const auto fingerprint = layer_fingerprint(g_mod_overlay, profile, [] { return mods::Sha256{}; });
  host::log("mods: fingerprinted %s in %.1f s", iso.u8string().c_str(),
            std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
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
  if (yougame::console::console_active()) return; // startup() scans/protects the ordinary card
  if (!gx::RenderOptions::mod_features_available() || !mods::auto_detect()) return;
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

// Validate local cosmetics against the original disc before keeping them in the retail view.
// Gameplay files and unverified replacements continue to use their original copies online.
void compute_pack_skin_verdicts(const std::unordered_map<std::string, int32_t>& base_paths,
                                const std::vector<FstFile>& base_fst) {
  g_pack_skins.clear();
  g_pack_skin_served.clear();
  g_pack_online_entry.clear();
  g_pack_online_files.clear();
  g_pack_online_next = kPackOnlineBase;
  auto& notes = mods::status().pack_skins;
  notes.clear();
  for (const auto& file : g_mod_overlay.files()) {
    const bool costume = skins::is_costume_file(file.path);
    const bool stage = skins::is_stage_file(file.path);
    const bool music = skins::is_music_file(file.path);
    // Recoloured items, effects or menu pictures in any other archive (a pack's Peach and Sheik files,
    // its stage select): served online when only their textures changed.
    const bool textures = !costume && !stage && !music && skins::is_archive_file(file.path);
    if (!costume && !stage && !music && !textures) continue;
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
    constexpr uint32_t limit = 64u * 1024 * 1024;
    if (file.length > limit || standard.length > limit) {
      verdict.reason = "replacement exceeds 64 MB";
      notes.push_back({verdict.path, verdict.layer, verdict.reason, false, false});
      g_pack_skins.push_back(std::move(verdict));
      continue;
    }
    std::vector<uint8_t> pack(file.length), clean(standard.length);
    if (pack.empty() || clean.empty() ||
        g_mod_overlay.read(file.start, pack.data(), file.length) != ModOverlay::Read::Success ||
        !host::disc_read(standard.offset, clean.data(), standard.length)) {
      verdict.reason = "the file could not be read";
    } else if (pack == clean) {
      verdict.identical = verdict.served = true;
      verdict.reason = "same as the original file";
    } else {
      std::string detail;
      verdict.served = costume ? host::cosmetics::costume_skeleton_matches(clean, pack, &detail)
                     : stage ? host::cosmetics::stage_safety::matches(clean, pack, &detail)
                     : textures ? host::cosmetics::file_textures_only(clean, pack, &detail)
                             : skins::music_is_valid(pack, &detail);
      verdict.reason = host::cosmetics::online_reason_short(detail);
      // A stage that is not the same file may still be the same stage: the pack's look drawn over
      // the original's gameplay, as an installed stage skin gets. Served from memory in that view.
      std::vector<uint8_t> online;
      std::string graft_detail;
      if (stage && !verdict.served &&
          host::cosmetics::stage_online_form(clean, pack, &online, &graft_detail, file.path) && !online.empty()) {
        const uint32_t span = ((uint32_t)online.size() + 0xFFFFu) & ~0xFFFFu;
        if (online.size() < 64u * 1024 * 1024 && (uint64_t)g_pack_online_next + span <= 0x70000000ull) {
          const int32_t grafted = (int32_t)g_fst.size();
          g_fst.push_back({g_pack_online_next, (uint32_t)online.size(), false});
          g_pack_online_files.push_back({g_pack_online_next, std::move(online)});
          g_pack_online_next += span;
          g_pack_online_entry[verdict.entry] = grafted;
          verdict.served = true;
          verdict.reason = "its look drawn over the standard stage's gameplay";
          host::log("mods: cosmetic %s online as %s", verdict.path.c_str(), graft_detail.c_str());
        }
      }
    }
    if (verdict.served && !g_pack_online_entry.count(verdict.entry)) g_pack_skin_served.insert(verdict.entry);
    host::log("mods: cosmetic %s %s online (%s)", verdict.path.c_str(), verdict.served ? "stays on" : "swapped",
              verdict.reason.c_str());
    notes.push_back({verdict.path, verdict.layer, verdict.reason, verdict.served, verdict.identical});
    g_pack_skins.push_back(std::move(verdict));
  }
}

void load_mod_overlay() {
  if (host::gameplay::vanilla_only()) return;
  namespace fs = std::filesystem;
  std::error_code ec;
  apply_detected_mods();
  if (!host::gameplay::mod_autodetect_disabled && !g_mod_root_explicit) {
    if (!yougame::mode_policy::mod_sources(yougame::active()).folders)
      host::log("mods: implicit Mods/Files overlay ignored in YouGame mode; mods come from the listing");
    else if (fs::exists(g_mod_root, ec) && !ec)
      g_mod_layers.push_back({mods::LayerKind::Dir, g_mod_root});
  }
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
    const bool explicit_layer = std::any_of(g_explicit_mod_layers.begin(), g_explicit_mod_layers.end(),
        [&](const mods::Layer& selected) { return selected.kind == layer.kind && selected.path == layer.path; });
    mods::Detected detected;
    const auto overlay_failed = [&] {
      if (g_mod_overlay.native_fighters_refused())
        yougame::notify_mod_refused(explicit_layer, yougame::mode_policy::ModRefusal::NativeFighters,
                                   detected.name, detected.version);
      host::die("mods: %s", error.c_str());
    };
    std::string fingerprint;
    switch (layer.kind) {
    case mods::LayerKind::Iso:
      if (!g_mod_overlay.add_iso(layer.path, label, lookup, read_base, error)) overlay_failed();
      fingerprint = iso_layer_fingerprint(layer.path, label);
      break;
    case mods::LayerKind::Dir:
      if (!g_mod_overlay.add_directory(layer.path, label, error)) overlay_failed();
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
    if (known && arcade_pack_name(known->name)) mods::status().arcade = true;
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
  // Training mods (Training Mode CE's disc) only feed their own training events, never a match, so
  // they are left out of what decides how this session plays online (owner 11-17): Animelee with
  // Training Mode CE on is still a look, and Training Mode CE alone plays anyone.
  auto training_disc = [](const mods::Layer& layer) {
    if (layer.training) return true;   // marked by the Mods folder scan
    if (layer.kind != mods::LayerKind::Iso) return false;
    return std::any_of(g_mod_overlay.files().begin(), g_mod_overlay.files().end(), [&](const ModOverlay::File& f) {
      if (f.path != "/tm/eventmenu.dat") return false;
      std::error_code ec;
      return f.iso == layer.path || std::filesystem::equivalent(f.iso, layer.path, ec);
    });
  };
  std::vector<const mods::Layer*> play_layers;
  for (const auto& layer : g_mod_layers) if (!training_disc(layer)) play_layers.push_back(&layer);
  g_training_only = !g_mod_layers.empty() && play_layers.empty();
  if (play_layers.size() != g_mod_layers.size())
    host::log("mods: %s; online matches leave training mods out", g_training_only ? "only training mods are on" : "training mods are on with other mods");
  const bool one_iso = play_layers.size() == 1 && play_layers[0]->kind == mods::LayerKind::Iso;
  g_cosmetic_pack = one_iso && host::cosmetics::official_cosmetic_disc(play_layers[0]->path.u8string());
  if (g_cosmetic_pack)
    host::log("mods: verified Animelee look; online uses standard gameplay with verified costumes and music");
  const bool akaneia = one_iso && has("/mxdt.dat") && has("/plsn.dat") && has("/plts.dat");
  if (akaneia) host::log("mods: Akaneia profile detected; its content online is Direct-only");
  // The 2D-style build: its hitbox code is the Arcade look's hitbox switches, on for this profile.
  // Its camera code runs in the game (MU_GAME_OPTION2_ARCADE_PACK, offline); online, where the game
  // leaves it off, the renderer's 2D camera gives the same flat look.
  if (mods::status().arcade) {
    // Not our grid: the build's own stages carry theirs, and on other stages ours covered the scenery.
    gx::set_arcade_display(gx::ArcadeSource::Pack, (gx::kArcadeGameBits & ~gx::kArcadeGrid) | gx::kArcadeCamera2d);
    host::log("mods: %s found; its codes run natively", kArcadePackName);
  }
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
  g_direct_fingerprint.clear();
  // rogueMelee: its disc is the 1.02 game under the id GALERM. Its menu files load like any pack's;
  // its code is compiled into the native game (sourceport/game/rogue). Single player, offline.
  if (one_iso) {
    char id[6] = {};
    std::ifstream disc(play_layers[0]->path, std::ios::binary);
    if (disc.read(id, sizeof id) && std::memcmp(id, "GALERM", sizeof id) == 0) {
      mods::status().rogue = true;
      host::log("mods: rogueMelee disc found; Rogue Mode runs natively (1-P Mode > Regular Match)");
    }
    // Melee Maker: the 1.02 game's id; its main.dol carries its build id and the bone tables the
    // native engine (sourceport/game/maker) reads through command 0xEE (maker_source.h).
    if (!mods::status().rogue && maker::attach(play_layers[0]->path)) {
      mods::status().maker = true;
      host::log("mods: Melee Maker %s disc found; its builds run natively", maker::kBuild);
      // Direct: the fingerprint the Static Recomp sends for this disc too (host.cpp), so players on
      // either engine can play each other.
      const std::string shared = maker::fingerprint(mods::sha256_file(play_layers[0]->path));
      g_direct_fingerprint = shared;
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
  if (!g_console_boot) {
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
      if (status.arcade)   // every run and code of this build has a native equivalent (run-source/arcade-0980/STUDY.md)
        status.notes.push_back(std::string(kArcadePackName) + ": its code patches run natively, and its hitbox, camera "
                               "and HUD looks are on for this profile.");
      else if (!report.dol_runs.empty() || report.dol_size != report.base_dol_size)
        status.notes.push_back(std::to_string(report.dol_runs.size()) +
                               " code patches in a modded disc are not applied: the Source Port runs its own game code.");
    for (const auto& conflict : g_mod_overlay.conflicts())
      status.notes.push_back(conflict.path.substr(1) + " is in two layers; the later one is used.");
    status.notes.push_back("Memory card: " + g_profile_card.u8string());
  }
  host::log("mods: profile %s, identity %s, %zu layers: %s",
            g_mod_profile_name.empty() ? "(command line)" : g_mod_profile_name.c_str(),
            g_mod_fingerprint.substr(0, 16).c_str(), g_mod_layers.size(), summary.c_str());

  // A stage stub of a dynamic alternate stages disc is not served: the game's own stage file stays,
  // and the disc's alternates of that stage ("GrSt/GrSt-surfshop(L).dat") are picked each match the
  // way the stub's code picks them (held L, R, Z, B, X or Y names one, otherwise any of them).
  std::unordered_set<std::string> stage_stubs;
  std::filesystem::path stub_disc;
  for (const auto& file : g_mod_overlay.files()) {
    if (!skins::is_stage_file(file.path) || !file.length || file.length > 1024u * 1024u) continue;
    // Only a file in place of one of the game's own stages is a stub: a stage file the disc adds has
    // no game stage to keep, and leaving it out lost it (ACE's Smashville sunset map GrAcSs.dat: every
    // evening match there stopped the game, 0.9.201 report).
    if (g_paths.find(file.path) == g_paths.end()) continue;
    std::vector<uint8_t> bytes(file.length);
    if (g_mod_overlay.read(file.start, bytes.data(), file.length) != ModOverlay::Read::Success ||
        !stage_file_is_stub(bytes)) continue;
    stage_stubs.insert(file.path);
    if (!file.iso.empty()) stub_disc = file.iso;
    host::log("mods: %s is a stage stub (its stage code is not run here); the game's stage stays, with the disc's alternates",
              skins::display_name(file.path).c_str());
  }
  if (!stub_disc.empty()) {
    std::string name = host::cosmetics::disc_pack_name(stub_disc.u8string());
    if (name.empty()) name = stub_disc.stem().u8string() + " disc";
    const auto scan = host::cosmetics::scan_disc_skins(stub_disc.u8string(), name);
    if (!scan.ok) host::log("mods: alternate stages of %s not listed: %s", stub_disc.filename().u8string().c_str(), scan.message.c_str());
    host::cosmetics::set_session_stage_pack(stub_disc.u8string());
  }
  // A disc file in place of one of the game's archives (.dat/.usd) whose own header names another size
  // can never be loaded: HSD_ArchiveParse stops the game (a rebuilt disc's zeroed NtMemAc.usd ended every
  // boot that showed the memory card notice, 1.0.0 reports). The game's own file stays for it.
  std::unordered_set<std::string> broken_archives;
  for (const auto& file : g_mod_overlay.files()) {
    const auto found = g_paths.find(file.path);
    if (found == g_paths.end() || g_fst[found->second].dir || stage_stubs.count(file.path)) continue;
    const std::string& path = file.path;
    if (path.size() < 4 || (path.compare(path.size() - 4, 4, ".dat") != 0 && path.compare(path.size() - 4, 4, ".usd") != 0)) continue;
    const FstFile& retail = g_fst[found->second];
    uint8_t own[4] = {}, game[4] = {};
    if (retail.length < 0x20 || !host::disc_read(retail.offset, game, 4) || be32(game) != retail.length) continue;
    if (file.length >= 4 && g_mod_overlay.read(file.start, own, 4) == ModOverlay::Read::Success && be32(own) == file.length) continue;
    broken_archives.insert(path);
    host::log("mods: %s in the disc is not a whole archive (header %08X, file %u bytes): the game's own file stays",
              path.c_str(), be32(own), file.length);
  }
  // A disc stage file in place of one of the game's stages that lacks a map part the game's own file
  // has: the stage's code makes an object for each of its parts by number, and one that is not there
  // was a read through a null archive (a custom disc's Pokemon Stadium ended the match as it started,
  // 1.0.7 report B-1192). The game's own stage stays for it.
  std::unordered_set<std::string> stages_missing_parts;
  for (const auto& file : g_mod_overlay.files()) {
    const auto found = g_paths.find(file.path);
    // The .dat and its English twin (.usd), which English games load in its place.
    const std::string& path = file.path;
    const bool stage_file = path.rfind("/gr", 0) == 0 && path.find('/', 1) == std::string::npos && path.size() > 7 &&
                            (path.compare(path.size() - 4, 4, ".dat") == 0 || path.compare(path.size() - 4, 4, ".usd") == 0);
    if (found == g_paths.end() || g_fst[found->second].dir || stage_stubs.count(file.path) ||
        broken_archives.count(file.path) || !stage_file)
      continue;
    const FstFile& retail = g_fst[found->second];
    if (retail.length < 0x20 || retail.length > 64u * 1024u * 1024u || file.length < 0x20 ||
        file.length > 64u * 1024u * 1024u)
      continue;
    std::vector<uint8_t> game(retail.length), own(file.length);
    if (!host::disc_read(retail.offset, game.data(), retail.length) ||
        g_mod_overlay.read(file.start, own.data(), file.length) != ModOverlay::Read::Success)
      continue;
    std::vector<bool> game_parts, own_parts;
    if (!stage_map_parts(game, &game_parts)) continue;
    const bool own_readable = stage_map_parts(own, &own_parts);
    size_t missing = 0;
    for (size_t i = 0; i < game_parts.size(); ++i)
      if (game_parts[i] && (!own_readable || i >= own_parts.size() || !own_parts[i])) ++missing;
    if (!missing) continue;
    stages_missing_parts.insert(file.path);
    host::log("mods: %s in the disc lacks %zu of the %zu map parts the game's own stage has: the game's own stage stays",
              file.path.c_str(), missing, game_parts.size());
  }
  uint32_t replaced = 0, added = 0, pack_kept = 0;
  for (const auto& file : g_mod_overlay.files()) {
    if (stage_stubs.count(file.path) || broken_archives.count(file.path) || stages_missing_parts.count(file.path)) continue;
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
    if (pack_keeps(file) || stage_stubs.count(file.path) || broken_archives.count(file.path)) continue;   // not served: the disc's own entry, the same in both views
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
// A stage file the mod disc replaced still takes the player's stage skins (the random installed stage skin
// each match, or a chosen one): they are checked against the game's own stage, and online play uses the
// standard stage anyway. Animelee CE's Battlefield refused every pick, so the same stage came every match
// (an 11-10 report). Costumes keep the stricter rule above.
bool pack_stage_takes_skins(uint32_t entry) {
  return entry < g_raw_paths.size() && !g_raw_paths[entry].empty() && skins::is_stage_file(g_raw_paths[entry]);
}
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
      if ((!host::cosmetics::session_pack_skin(applied) && !pack_costume_takes_skins(i) && !pack_stage_takes_skins(i)) || i >= g_raw_paths.size() ||
          g_raw_paths[i].empty()) {
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
  host::cosmetics::set_lockstep_probe(yougame::console::console_active);
  host::cosmetics::set_stage_button_probe(held_buttons);
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
// connect-code text) and its CSS file. The static recomp gets them over EXI (D1/D2, exi_slippi.cpp);
// the native game asks for them through the ordinary disc path, so they are registered in the
// filesystem table here, built once at boot with the same code. Own offset range, never counted as
// mod assets. Any failure turns the menus off for the run instead of mixing vanilla and patched.
bool g_resume_online = false;
bool g_slippi_menus_requested = true;   // --slippi-menus on|off
bool g_slippi_menus = false;            // requested and the layer loaded
// `open`: the same online menu with YouGame's rows not greyed, served once they open while the game runs
// (a running YouGame app found over the local channel, yougame_channel.h). Same size as `data`.
// `serving`: the copy this load of the file reads, picked at its first byte and kept to its end, so a
// channel coming up or going down mid-load never mixes the two.
struct SystemFile {
  std::string path; uint32_t start; std::vector<uint8_t> data; std::vector<uint8_t> open;
  const std::vector<uint8_t>* serving = nullptr;
};
std::vector<SystemFile> g_system_files;
// Between the disc (under 1.5 GB) and the mod overlay range (ModOverlay::base up to the cosmetics at
// 0xF0000000). It used to sit at 0xC0000000, inside the overlay's range: a pack large enough to
// reach it (the 20XX Hack Pack's 776 files) had its files there refused, and the game waited on them.
// It moved from 0x90000000 to 0x60000000 so the overlay could start lower and hold 2 GiB: a large
// m-ex build (Chaos Eternal) changes more than the 1.25 GiB the old range held.
constexpr uint32_t kSystemFileBase = 0x60000000u;
static_assert(kSystemFileBase >= 0x58000000u && kSystemFileBase + 0x10000000u <= ModOverlay::base,
              "Slippi system files sit past the largest disc and below the mod overlay");
const char* const kSystemFileNames[] = {
  "MnMaAll.usd", "MnMaAll.dat", "SdMenu.usd", "SdMenu.dat", "MnSlMap.usd", "MnSlMap.dat",
  "SdSlChr.usd", "SdSlChr.dat", "MnExtAll.usd", "MnExtAll.dat", "slpCSS.dat", "GameSetup_gui.dat",
  "SlippiCSS_gui.dat",
};
// Slippi 3.7.0's name for slpCSS.dat, built from links into the player's disc (exi_slippi.cpp). The
// native CSS asks for it first and falls back to slpCSS.dat, so when it cannot be built (a disc that
// lacks what it links to) it is left out rather than turning the menus off.
bool optional_system_file(const char* name) { return std::strcmp(name, "SlippiCSS_gui.dat") == 0; }

uint32_t fnv1a(const uint8_t* p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
  return h;
}

bool read_overlay(uint32_t offset, void* dst, uint32_t size, bool* ok);
// True when the served file (a mod disc's stage select) is an m-ex one: its symbol table names
// mexMapData, the stage pages the native m-ex layer reads (mnstagesel.c, mexsel_map).
bool mex_stage_select(const FstFile& file) {
  if (file.dir || file.length < 32 || file.length > 64u * 1024 * 1024) return false;
  std::vector<uint8_t> bytes(file.length);
  bool ok = false;
  if (!read_overlay(file.offset, bytes.data(), file.length, &ok)) ok = host::disc_read(file.offset, bytes.data(), file.length);
  if (!ok) return false;
  static const char kName[] = "mexMapData";
  return std::search(bytes.begin(), bytes.end(), kName, kName + sizeof kName) != bytes.end();   // with its 0
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
    if (data.empty() && optional_system_file(name)) { host::log("slippi menus: %s left out (cannot build it)", name); continue; }
    if (data.empty()) { host::log("slippi menus: off (cannot build %s)", name); return; }
    // The GitHub build's online menu: YouGame Ranked and YouGame Direct above Slippi's rows. The
    // YouGame app keeps Slippi's labels (its rows are all YouGame). A file that is not the expected
    // menu is left as it is, and the menu then keeps the old layout too (online_rows::applied).
    std::vector<uint8_t> open;
    if (!yougame::active() && (std::strcmp(name, "MnMaAll.usd") == 0 || std::strcmp(name, "MnMaAll.dat") == 0)) {
      std::string why;
      // Both copies from the run's rule, never from the channel's state at this moment.
      // Since build 98.2 the rows are never greyed: one copy, the same connected or not.
      const bool soon = false;
      if (soon) {
        open = data;
        if (!online_rows::apply(open, nullptr, false)) open.clear();
      }
      if (online_rows::apply(data, &why, soon)) {
        if (open.size() != data.size()) open.clear();
        if (std::strcmp(name, "MnMaAll.usd") == 0) online_rows::set_applied(true);
        host::log("online menu: %s shows the YouGame and Slippi rows", name);
      } else {
        host::log("online menu: %s keeps its rows (%s)", name, why.c_str());
      }
    }
    const uint32_t size = (uint32_t)data.size();
    if (uint64_t(next) + size > kPackOnlineBase) { host::log("slippi menus: off (system files exceed their range)"); return; }
    files.push_back({path, next, std::move(data), std::move(open), nullptr});
    next += (size + 0xFFFFu) & ~0xFFFFu;
  }
  uint32_t replaced = 0, added = 0;
  for (const auto& f : files) {
    auto found = g_paths.find(f.path);
    int32_t entry;
    if (found != g_paths.end()) {
      if (g_fst[found->second].dir) { host::log("slippi menus: %s is a disc directory", f.path.c_str()); continue; }
      entry = found->second;
      // An m-ex disc's own stage select (ACE: its stage pages, mexMapData) stays in the mod view:
      // Slippi's copy, built from the retail file, left only the retail stages on the screen
      // (0.9.97 report "ACE maps don't show up"). The native m-ex layer locks every added stage
      // it has no code for. The retail view (online against the standard game) gets Slippi's.
      const auto alias = g_view_alias.find(entry);
      if ((f.path == "/mnslmap.usd" || f.path == "/mnslmap.dat") && alias != g_view_alias.end() &&
          mex_stage_select(g_fst[entry])) {
        const int32_t slippi_entry = (int32_t)g_fst.size();
        g_fst.push_back({f.start, (uint32_t)f.data.size(), false});
        alias->second = slippi_entry;
        host::log("slippi menus: %s keeps the disc's m-ex stage select; Slippi's copy is its retail view", f.path.c_str());
        continue;
      }
      ++replaced;
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
  for (auto& f : g_system_files) {
    if (offset < f.start || offset >= f.start + (uint32_t)f.data.size()) continue;
    // The game reads in 32-byte-rounded chunks, so the last read runs a few bytes past the end,
    // into padding on a real disc. Serve that padding as zeros, up to the file's 64 KB slot.
    const uint32_t at = offset - f.start;
    const uint32_t slot = ((uint32_t)f.data.size() + 0xFFFFu) & ~0xFFFFu;
    *ok = size <= slot - at;
    if (*ok) {
      if (at == 0 || !f.serving) f.serving = !f.open.empty() && !yougame::coming_soon() ? &f.open : &f.data;
      const auto& bytes = *f.serving;
      const uint32_t have = std::min<uint32_t>(size, (uint32_t)bytes.size() - at);
      std::memcpy(dst, bytes.data() + at, have);
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
void write_replay_recording(bool in_background = false);
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
// The body counter is already exposed by the read-only page-pause query (a[0]=-1).
// Unlike PADRead / VI waits, the clock callback also runs in every resim body
// (HSD_PerfSetStartTime). Only query it while catch-up is active.
uint32_t rollback_engine_frame() {
  int32_t state[3] = {-1, 0, 0};
  return g_game.practice && g_game.practice(MU_PRACTICE_PAGE_PAUSE, state, 3) == 0 && state[2] >= 0
      ? static_cast<uint32_t>(state[2]) : 0;
}
void rollback_log(bool exiting = false) {
  using namespace host::rollback;
  auto& a = accumulator;
  if (!a.online) return;
  const auto ns = now_ns();
  if (exiting) a.end(ns, rollback_engine_frame(), presentation);
  if (!exiting && !a.due(ns)) return;
  const auto counts = presentation.snapshot();
  auto text = line(exiting ? "match" : "interval", ns - (exiting ? a.started_ns : a.window_ns),
                   exiting ? a.match : a.interval, counts - (exiting ? a.match_present : a.interval_present));
#ifdef YOUGAME_AGENT_TEST_HOOK
  const auto& config = yougame::net_test::config();
  if (config.enabled()) text += " [AGENT TEST net=+" + std::to_string(config.delay_ms) +
      " ms jitter 0.." + std::to_string(config.jitter_ms) + "]";
#endif
  host::log("%s", text.c_str());
  if (exiting) a.stop(presentation);
  else a.next_window(ns, counts);
}
void rollback_frame_callback() {
  using namespace host::rollback;
  if (!accumulator.catching_up) return;
  const auto frame = rollback_engine_frame();
  if (frame > accumulator.last_frame()) {
    accumulator.frame(now_ns(), frame);
    rollback_log();
  }
}
// Both normal returns and a guarded ExitRequested must finish before shutdown closes the SDK.
void finish_tournament_before_exit() {
  if (!yougame::tournament_active()) return;
  if (g_game.practice) g_game.practice(MU_TOURNEY_QUIT_OP, nullptr, 0);
  yougame::tournament_drain();
}

template <class F> void guarded(F&& f) {
  try { f(); } catch (const ExitRequested& stop) {
    g_exit_code = stop.code;
    finish_tournament_before_exit();
    write_replay_recording();
#ifndef MELEE_NO_SLIPPI
    slippi::spectate::stop();
    slippi::arena::shutdown();
#endif
    rollback_log(true);
    log_native_pose_bridge_stats();
#ifdef __APPLE__
    if (g_mac_running) std::longjmp(g_mac_run_boundary, 1);
#endif
    if (g_shutdown) g_shutdown(stop.code);
    host::end_process(stop.code);   // everything is saved: do not run the libraries' unload code
  }
}

// ---- the table the game calls ----
#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
#ifdef YOUGAME_AGENT_TEST_HOOK
uint32_t g_squad_test_rollback_from = 0;
std::unordered_set<uint64_t> g_squad_test_seen_swaps;
#endif
#endif
void h_log(const char* text) {
  std::string line = text ? text : "";
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (yougame::net_test::config().squad_local_test) {
    if (line.rfind("online: match starts,", 0) == 0) g_squad_test_seen_swaps.clear();
    if (line.rfind("squad: swap P", 0) == 0) {
      unsigned port, kind, color, frame;
      if (std::sscanf(line.c_str(), "squad: swap P%u -> %u:%u at frame %u", &port, &kind, &color, &frame) == 4) {
        // Require a previous execution of this port's swap, excluding a new frontier
        // frame or two different ports swapping in the same resimulation body.
        const bool repeated = !g_squad_test_seen_swaps.insert((uint64_t(frame) << 32) | port).second;
        if (g_resim && repeated)
          host::log("squad: swap resimulated at frame %u (rollback from %u)", frame, g_squad_test_rollback_from);
      }
    }
  }
#endif
#endif
  if (line.rfind("online: match starts,", 0) == 0) {
    using namespace host::rollback;
    rollback_log(true);
    accumulator.start(now_ns(), presentation);
#ifdef YOUGAME_AGENT_TEST_HOOK
    const auto& config = yougame::net_test::config();
#if MU_SQUAD
    // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    if (config.squad_local_test)
      host::log("squad: test net delay: outgoing input +%u ms, jitter 0..%u ms (loopback harness)",
                config.delay_ms, config.jitter_ms);
    else
#endif
    if (config.enabled()) host::log("AGENT TEST net delay: outgoing input +%u ms, jitter 0..%u ms (Direct)",
                                   config.delay_ms, config.jitter_ms);
#endif
  }
  if (line.rfind("online: match exit:", 0) == 0) rollback_log(true);
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (line.rfind("online: rollback time:", 0) == 0)
    line.replace(0, sizeof("online: rollback time:") - 1, "online: rollback time (emulated clock):");
#endif
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
#ifdef __APPLE__
  host::log("game panic: %s", message ? message : "");
  host::end_process(1);
#endif
  host::die("game stopped at %s:%d: %s", file ? file : "?", line, message ? message : "");
}
uint64_t h_ticks() {
  if (yougame::console::console_active()) return yougame::console::console_engine().ticks(0);
  take_console_catch_up();   // End mid-frame: the game's OS time never goes back to the old host clock
  if (g_resim) rollback_frame_callback();
  return host::cpu->tb;
}
// The date the game's calendar works from. It was zero, so every save and every Training Mode CE
// export was dated January 1, 2000.
uint64_t h_boot_time() {
  if (yougame::console::console_active()) return yougame::console::console_engine().boot_ticks(0);
  return host::console_epoch_ticks();
}
int32_t h_console_seed(uint32_t* seed) {
  if (!yougame::console::console_active()) {
    auto ch = std::atomic_load(&yougame::console::css_join::pending);
    auto q = ch ? std::atomic_load(&ch->request) : nullptr;
    if (!q || ch->cancelled.load() || q->operation == yougame::console::css_join::Operation::Capture) return 0;
    *seed = static_cast<uint32_t>(q->seed); return 1;
  }
  *seed = static_cast<uint32_t>(yougame::console::console_engine().config().seed);
  return 1;
}
int32_t h_console_has_save() {
  if (g_css_origin.join_boot) return !g_css_origin.card.empty();
  return g_console_boot && yougame::console::console_active() &&
      !yougame::console::console_engine().config().card_digest.empty();
}
int32_t h_console_frame_begin() {
  try {
#ifdef MELEE_DOUBLES
    if (doubles_tick()==-51) return -51;
#endif
    if (css_join_tick() == -50) return -50;
    if (css_origin_tick() == -50) return -50; // return to C before discarding its old scene stack
    if (yougame::console::friends_handoff_join_enabled()) {
      int32_t scene[2]{};
      const bool css = g_game.practice && g_game.practice(MU_PRACTICE_SCENE,scene,2) == 0 &&
          scene[0] == 2 && scene[1] == 0;
      yougame::console::handoff_join::host_css.store(css,std::memory_order_release);
      friends_handoff_tick();
    }
#ifdef YOUGAME_AGENT_TEST_HOOK
    handoff_tick(); // unconditional engine-body hook; never count PADRead or VI callbacks as bodies
    boot_match_tick();
#endif
    if (!g_console_boot) return 0;
    auto& engine = yougame::console::console_engine();
    if (engine.active()) {
      // Match VI counts are logical and rewindable. Pacing uses the host's physical count.
      static uint32_t presented = 0;
      if (engine.match_active() && host::retrace_count() == presented) host::retrace();
      presented = host::retrace_count();
      MuStatePod state{};
      if (g_game.state_snapshot) g_game.state_snapshot(&state);
      // One hardware read per forward body, even when its target was already sampled (the bodies
      // after a rollback clock restore): the menus and the held-on-close latch follow live input.
      // Replays never come through here, and an already-sampled target sends nothing new.
      const bool sample = engine.needs_sample();
      const yougame::console::Pads hardware = console_hardware();
      engine.advance(sample ? hardware : yougame::console::Pads{}, state);
    }
    return 1; // also replace any queued remote sample after a loss
  } catch (const ExitRequested& stop) {
    // Finish inside the host: a partially restored game must not resume its old C stack.
    guarded([&] { throw stop; });
    return 0;
  } catch (const std::exception& error) {
    host::die("console frame begin: %s", error.what());
  } catch (...) {
    host::die("console frame begin: unknown exception");
  }
}

void native_audio_done(void*) {
  if (g_game.ai_dma_done) g_game.ai_dma_done();
}

void native_audio_tick() {
  audio_core::tick(g_audio, host::cpu->tb, host::TB_HZ, native_audio_done, nullptr);
}

bool g_console_in_retrace = false;
struct ConsoleServiceScope {
  yougame::console::Engine& engine = yougame::console::console_engine();
  ConsoleServiceScope() { engine.service_clock(true); }
  ~ConsoleServiceScope() { engine.service_clock(false); }
};
void h_poll() {
  if (yougame::console::console_engine().rollback_enabled()) {
    auto& engine = yougame::console::console_engine();
    engine.poll(); // replay executes the same game-clock step as first execution
    if (engine.body_active() && !engine.servicing()) return;
    guarded([&] {
      host::advance_time(2048);
      const bool logical_due = !engine.match_active() && !engine.servicing() && engine.retrace_due();
      const bool service_due = (engine.match_active() || engine.servicing()) && engine.service_retrace_due();
      if (logical_due) engine.logical_retrace();
      ConsoleServiceScope service;
      native_audio_tick();
      if ((logical_due || service_due) && !g_console_in_retrace) host::retrace();
      else g_game.fire_alarms(engine.alarm_ticks());
    });
    return;
  }
  // A re-simulated frame never waits on the machine; count it if something does.
  if (g_resim) { ++g_resim_polls; rollback_frame_callback(); return; }
  if (host::tick_redo_active()) return;   // a tick being run again: no time passes, nothing is due
  guarded([] {
    take_console_catch_up();
    host::advance_time(2048);   // time flows in busy waits, as it does for the recompiled build
    native_audio_tick();
    if (yougame::console::console_active()) {
      auto& engine = yougame::console::console_engine();
      engine.poll();
      if (engine.retrace_due() && !g_console_in_retrace) host::retrace();
      else g_game.fire_alarms(engine.clock_ticks());
      return;
    }
    if (host::retrace_due()) {
      if (!host::tick_redo_presleep()) return;   // the controller changed: back to the game's gate, the retrace comes after
      host::retrace();
    }
    else g_game.fire_alarms(host::cpu->tb);
  });
}

static int (*g_update_ranked_busy)() = nullptr;
static void (*g_look_snapshot)(MuLookSnapshot*) = nullptr;
static void (*g_look_items)(MuLookItems*) = nullptr;
static void (*g_look_extras)(MuLookExtras*) = nullptr;
void native_retrace() {
  ConsoleServiceScope service;
  struct ConsoleRetraceScope {
    bool previous = g_console_in_retrace;
    ConsoleRetraceScope() { g_console_in_retrace = true; }
    ~ConsoleRetraceScope() { g_console_in_retrace = previous; }
  } console_retrace_scope;
  rollback_log();
  take_console_catch_up();
  native_audio_tick();
  if (yougame::console::console_active()) {
    auto& engine = yougame::console::console_engine();
    if (engine.rollback_enabled()) engine.service_retrace();
    else engine.retrace();
    g_game.fire_alarms(engine.alarm_ticks());
  } else g_game.fire_alarms(host::cpu->tb);
  g_game.retrace();
  if (g_look_snapshot && lab::active()) {
    MuLookSnapshot snapshot{};
    g_look_snapshot(&snapshot);
    MuLookItems items{};
    if (g_look_items) g_look_items(&items);
    MuLookExtras extras{};
    if (g_look_extras) g_look_extras(&extras);
    const std::array<std::string, 4> names = slippi::online::player_names_for_overlay();
    lab::native_frame(snapshot, g_look_items ? &items : nullptr, g_look_extras ? &extras : nullptr, names.data());
  }
}

void h_gx_fifo(const uint8_t* data, uint32_t size) {
  if (gl_costs_enabled() && g_resim) g_resim_gx_bytes += size;
  guarded([&] { gx::write_fifo_bytes(data, size); });
}
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
// Render semantics for the ray-traced renderer (tokens 0xF3-0xF6): only while it asks for them.
int32_t h_native_rtx_semantics() { return gx::rtx_semantics_enabled() ? 1 : 0; }
// The HUD sliders (stock icon and damage number size), display only, as the backend last set them.
uint32_t h_hud_scales() { return gx::hud_scales_packed(); }
static_assert(gx::kArcadeHitboxes == 0x20u && gx::kArcadeProjectileHitboxes == 0x40u && gx::kArcadeHitboxesBehind == 0x80u,
              "user_gecko::arcade_bits_of_codes");
static_assert(gx::kArcadeHitboxes == MU_DISPLAY_OPTION_HITBOXES &&
              gx::kArcadeProjectileHitboxes == MU_DISPLAY_OPTION_PROJECTILE_HITBOXES &&
              gx::kArcadeHitboxesBehind == MU_DISPLAY_OPTION_HITBOXES_BEHIND && gx::kArcadeGrid == MU_DISPLAY_OPTION_GRID,
              "Arcade look bits");
// "Show Training mode's HUD" off. MELEE_TEST_TRAINING_HUD=0|1 (tests) overrides the setting.
bool training_hud_hidden() {
  static const int env = [] { const char* v = host::test_env::getenv("MELEE_TEST_TRAINING_HUD"); return v && *v ? (*v == '0' ? 0 : 1) : -1; }();
  return env >= 0 ? env == 0 : !gx::RenderOptions::live_training_hud();
}
// Display-only options (MU_DISPLAY_OPTION_*), read live by the native game's draw code. Never part
// of a replay or of the match the opponent plays: the viewer's own, as the HUD sizes are.
uint32_t h_display_options() {
  if (yougame::console::console_active()) return 0;
  return (gx::low_poly_fighters_active() ? MU_DISPLAY_OPTION_LOW_POLY : 0u) |
         (training_hud_hidden() ? MU_DISPLAY_OPTION_NO_TRAINING_HUD : 0u) |
         (slippi::online::port_colour_bits() & MU_DISPLAY_OPTION_PORT_COLOURS) |   // online 1v1 port colours
         (gx::player_tags_always_active() ? MU_DISPLAY_OPTION_PLAYER_TAGS : 0u) |
         (gx::fod_reflections_active() ? MU_DISPLAY_OPTION_FOD_REFLECTIONS : 0u) |
         (gx::css_background_free_active() ? MU_DISPLAY_OPTION_CSS_BACKGROUND_FREE : 0u) |
         (gx::RenderOptions::live_freeze_fd() ? MU_DISPLAY_OPTION_FREEZE_FD : 0u) |
         (host::g_hide_ko_explosions.load(std::memory_order_relaxed) ? MU_DISPLAY_OPTION_NO_KO_EXPLOSIONS : 0u) |
         (host::g_hide_hud_emblems.load(std::memory_order_relaxed) ? MU_DISPLAY_OPTION_NO_HUD_EMBLEMS : 0u) |
         (host::g_community_codes.load(std::memory_order_relaxed) & MU_DISPLAY_OPTION_CODES_ALL) |
         // The offline community codes (menu timing, the pause): never in a network match.
         (slippi::online::session_mode() >= 0 || source_p2p::requested()
              ? 0u : host::g_community_codes.load(std::memory_order_relaxed) & MU_DISPLAY_OPTION_OFFLINE_CODES) |
         // The Arcade look's hitboxes: never in a network match (the game also checks). Its grid is a
         // picture like the rest of the background, so it stays.
         (slippi::online::session_mode() >= 0 || source_p2p::requested() ? 0u : gx::arcade_display() & gx::kArcadeGameBits) |
         // Its 2D camera, offline: the game runs the 2D-style build's camera code; online the renderer's
         // flat projection stands in (gx_shader.cpp build_projection).
         (slippi::online::session_mode() >= 0 || source_p2p::requested() || !(gx::arcade_display() & gx::kArcadeCamera2d)
              ? 0u : MU_DISPLAY_OPTION_ARCADE_CAMERA) |
         (gx::arcade_display() & gx::kArcadeGrid);
}
uint32_t h_shield_color(int32_t slot) { return gx::shield_color(slot); }
float h_screen_shake_scale() { return gecko::option_screen_shake_percent / 100.0f; }
uint32_t h_grid_color() { return gx::look_live().arcade_grid_rgb.load(std::memory_order_relaxed) & 0xFFFFFFu; }
void h_hud_combo(int32_t slot, int32_t link_slot, float percent, int32_t stunned) {
  gx::set_native_hud_combo(slot, link_slot, percent, stunned != 0);
}
void h_hud_extras(int32_t slot, int32_t nana_damage, int32_t team) { gx::set_native_hud_extras(slot, nana_damage, team); }
void h_hud_pro_tag(int32_t slot, float dx, float width) { gx::set_native_hud_pro_tag(slot, dx, width); }
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
// What pc_settings.cpp apply_practice_tools sets in the options word for the Training tools.
constexpr uint32_t kPracticeLabBits = MU_GAME_OPTION_LAB | MU_GAME_OPTION_LAB_BUBBLES | MU_GAME_OPTION_LAB_BUBBLES_ONLY |
                                      MU_GAME_OPTION_LAB_OVERLAY | MU_GAME_OPTION_LAB_LOOP;
uint32_t te_words_mask2() { return mods::status().te_owned ? 0xFFFFFFFFu : hp_te_words() ? kHpTeOptions2 : 0u; }
uint32_t h_game_options2() {
  if (yougame::console::console_active()) return 0;
  if (yougame::tournament_active()) return 0; // Ranked-style play, no offline CPU/TE modifiers
  if (host::gameplay::vanilla_only()) return 0;
  if (g_replaying) return g_replay_feature_options2;
  return (gx::RenderOptions::live_te_options2() & te_words_mask2() & ~(MU_GAME_OPTION2_STADIUM_FROZEN | MU_GAME_OPTION2_ARCADE_PACK)) |
         cpu_20xx_bit() |
         (gx::RenderOptions::live_stadium_frozen() ? MU_GAME_OPTION2_STADIUM_FROZEN : 0u) |   // a plain option, like 20XX CPUs
         (mods::status().arcade ? MU_GAME_OPTION2_ARCADE_PACK : 0u);   // the recognised 2D-style build's codes
}
// The 20XX Hack Pack training options for CPUs (Game tab), word 3: plain options, the game gates
// them itself (mu_options3_offline: offline only, never in a TM-CE event).
// The upper bits are the 20XX Hack Pack stage of the match (hackpack_source.h): the game reports
// them when a match is picked and they ride in the same word, so a replay records and replays them.
// Faster sound effects (MU_AUDIO_OPTION_EARLY_KEYON): MELEE_AUDIO_EARLY_KEYON=0|1 over the setting;
// never during an online match or a replay (see mu_host.h).
float h_picture_widen() {
  if (yougame::console::console_active()) return 1.0f;
  return gx::true_widescreen_active() ? 1.0f / gx::true_widescreen_factor() : 1.0f;
}
uint32_t h_audio_options() {
  // Mute character voices: everywhere (online and replays too); it changes no timing.
  static const int mute_env = [] { const char* v = host::test_env::getenv("MELEE_TEST_MUTE_VOICES"); return v && *v ? (*v == '1' ? 1 : 0) : -1; }();
  const bool mute = mute_env >= 0 ? mute_env == 1 : gx::RenderOptions::live_mute_voices();
  static int mute_logged = -1;
  if ((int)mute != mute_logged) { host::log("audio: character voices %s", mute ? "muted" : "on"); mute_logged = mute; }
  const uint32_t voices = mute ? MU_AUDIO_OPTION_MUTE_VOICES : 0u;
  if (yougame::console::console_active()) return voices;
  if (g_replaying || slippi::online::session_mode() >= 0 || source_p2p::requested()) return voices;
  static const int env = [] { const char* v = std::getenv("MELEE_AUDIO_EARLY_KEYON"); return v && *v ? (*v == '1' ? 1 : 0) : -1; }();
  const bool on = env >= 0 ? env == 1 : gx::RenderOptions::live_audio_early_keyon();
  static int logged = -1;
  if ((int)on != logged) { host::log("audio: faster sound effects %s", on ? "on" : "off"); logged = on; }
  return (on ? MU_AUDIO_OPTION_EARLY_KEYON : 0u) | voices;
}

uint32_t h_game_options3() {
  // A console session unlocks every fighter and stage for both players, as a network
  // session does; a locked word would hand the roster to whichever card is loaded.
  if (yougame::console::console_active()) return 0;
  if (yougame::tournament_active()) return 0;
  if (g_replaying) return g_replay_feature_options3;
  return (host::gameplay::vanilla_only() ? 0u :
          (gx::RenderOptions::live_cpu_training() & MU_GAME_OPTION3_CPU_ALL) | hackpack::stage_bits()) |
         (gx::RenderOptions::live_unlock_all() ? 0u : MU_GAME_OPTION3_LOCKED_CONTENT) |
         (gx::RenderOptions::live_results_screen() ? 0u : MU_GAME_OPTION3_SKIP_RESULTS);
}
// A switched-on PAL Stock Icons Gecko code (user_gecko::hand_ports_of_codes), set each frame.
std::atomic<bool> g_code_pal_stock{false};
uint32_t h_game_options() {
  if (yougame::console::console_active()) return 0; // canonical normal menus and language
  return (gecko::option_no_screen_shake ? MU_GAME_OPTION_NO_SCREEN_SHAKE : 0u) |
         (gecko::option_pal_stock_icons || g_code_pal_stock.load(std::memory_order_relaxed) ? MU_GAME_OPTION_PAL_STOCK_ICONS : 0u) |
         (gecko::option_widescreen ? MU_GAME_OPTION_WIDESCREEN : 0u) |   // the viewer's own, also in replays
         (gx::true_widescreen_active() ? MU_GAME_OPTION_TRUE_WIDESCREEN : 0u) |   // likewise: display only
         (((uint32_t)host::g_game_language.load(std::memory_order_relaxed) << MU_GAME_OPTION_LANGUAGE_SHIFT) &
          MU_GAME_OPTION_LANGUAGE_MASK) |   // the player's language choice: text, names and voices only
         (host::options.vanilla_game ? MU_GAME_OPTION_VANILLA : 0u) |
         (g_slippi_menus ? MU_GAME_OPT_SLIPPI_MENUS : 0u) |
         (!host::gameplay::vanilla_only() && !yougame::tournament_active() && mods::status().tmce && !g_replaying ? MU_GAME_OPTION_TMCE : 0u) |
         ((host::gameplay::vanilla_only() || yougame::tournament_active()) ? 0u : g_replaying ? g_replay_feature_options
                   : gx::RenderOptions::live_te_options() & te_words_mask1()) |
         // The Practice panel's Training tools (D-pad save states and recording, hitboxes, the loop)
         // are the native lab's bits (shim/mu_lab.c), set from the practice settings by
         // apply_practice_tools. The mask above passes only 20XX TE's own bits, so since the panel
         // came in these switches never reached the game. The lab itself acts only in an offline
         // Training match outside a replay; never sent in a network session or during playback.
         (host::gameplay::vanilla_only() || yougame::tournament_active() || g_replaying || slippi::online::session_mode() >= 0 || source_p2p::requested()
              ? 0u : gx::RenderOptions::live_te_options() & kPracticeLabBits);
  // (Only 20XX TE's own bits, and only with its save, or the Hack Pack's share of them with the pack.
  // The old Training Lab's own rows (dummy DI, reactions, tech) are not offered any more: Training
  // Mode CE and the practice CPU replace them.)
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

// The sound driver's whole state (every variable of the AX files, synth and axdriver), from the
// ranges the build wrote beside the game library: a console savestate leaves all of it out, and
// restoring its voice tables while the voices play on cuts and restarts sounds on every rollback.
void append_sound_driver_exclusions(std::vector<MuStateRegion>& exclusions) {
  if (!g_loaded_game_path.empty() && host::game_image) {
    auto path = g_loaded_game_path; path.replace_extension(".snapexcl");
    std::ifstream in(path);
    std::error_code size_error;
    const auto dll_size = std::filesystem::file_size(g_loaded_game_path,size_error);
    std::vector<MuStateRegion> sound;
    std::string line;
    size_t bytes = 0;
    bool other_build = bool(size_error), size_seen = false;
    while (std::getline(in, line)) {
      unsigned long long value = 0;
      if (std::sscanf(line.c_str(), "# dll_size %llu", &value) == 1) { size_seen = true; other_build = other_build || value != dll_size; continue; }
      unsigned long long offset = 0, size = 0;
      if (std::sscanf(line.c_str(), "%llx %llx", &offset, &size) == 2 && size) {
        sound.push_back(MuStateRegion{(void*)((uintptr_t)host::game_image + offset), (uint32_t)size});
        bytes += size;
      }
    }
    if (!in.is_open() || sound.empty())
      host::log("online: no snapexcl applied: file missing or empty (%s)",path.string().c_str());
    else if (other_build || !size_seen)
      host::log("online: no snapexcl applied: size mismatch (%s)",path.string().c_str());
    else {
      exclusions.insert(exclusions.end(), sound.begin(), sound.end());
      host::log("online: sound driver state left out of snapshots: %zu ranges, %zu bytes", sound.size(), bytes);
    }
  } else host::log("online: no snapexcl applied: library not found");
}

// Immutable identity only; never capture or read span contents on the room worker.
void publish_handoff_join_info(const MuStateRegion* regions, uint32_t count,
                               std::vector<MuStateRegion> exclusions, const MuHostApi* api) {
  using namespace yougame::console;
  if (!friends_handoff_join_enabled()) return;
  try {
    handoff_state::Identity identity;
    std::string why;
    if (!handoff_state::library_identity(g_loaded_game_path,identity,why)) return;
    append_sound_driver_exclusions(exclusions);
    // Keep the process's host ABI binding OUT of the transmitted span table, rather than
    // importing A's pointer and excluding it only from the final comparison.
    const uintptr_t binding = reinterpret_cast<uintptr_t>(api);
    size_t bindings = 0;
    for (uint32_t j = 0; j < count; ++j) {
      auto* p = static_cast<uint8_t*>(regions[j].address);
      for (size_t k = 0; k+sizeof binding <= regions[j].size; ++k) {
        if (std::memcmp(p+k,&binding,sizeof binding)) continue;
        exclusions.push_back({p+k,sizeof binding}); ++bindings;
      }
    }
    if (!bindings || !g_handoff_callbacks_pending) return;
    const auto spans = native_savestate::subtract(
        std::vector<MuStateRegion>(regions,regions+count),exclusions);
    if (spans.empty()) return;
    g_friends_handoff.identity = identity; g_friends_handoff.spans = spans;
    g_friends_handoff.ready = true;
    // Same little-endian (u64 address,u32 size) table as MUHS; no native padding or pointers on wire.
    std::vector<uint8_t> table;
    for (const auto& span : spans) {
      const uint64_t address = reinterpret_cast<uintptr_t>(span.address);
      for (unsigned i = 0; i < 8; ++i) table.push_back(uint8_t(address >> (8*i)));
      for (unsigned i = 0; i < 4; ++i) table.push_back(uint8_t(span.size >> (8*i)));
    }
    auto info = std::make_shared<handoff_join::Info>();
    info->library_size = identity.size;
    for (const auto& span : spans) info->capture_bytes += span.size;
    constexpr char hex[] = "0123456789abcdef";
    for (const auto b : identity.sha256) {
      info->library_sha256 += hex[b >> 4]; info->library_sha256 += hex[b & 15];
    }
    info->spans_digest = host::hackpack_ai::rules::sha256_hex(table.data(),table.size());
#ifdef _WIN32
    info->os = "windows";
#elif defined(__APPLE__)
    info->os = "macos";
#else
    info->os = "linux";
#endif
    std::shared_ptr<const handoff_join::Info> immutable = std::move(info);
    std::atomic_store_explicit(&handoff_join::published_info,std::move(immutable),std::memory_order_release);
  } catch (...) { /* missing metadata refuses live join; ordinary entry remains available */ }
}

void friends_handoff_tick() {
  using namespace yougame::console::handoff_join;
  auto& run = g_friends_handoff;
  ++run.frame;
  auto channel = std::atomic_exchange_explicit(&pending_channel,std::shared_ptr<Channel>{},std::memory_order_acq_rel);
  if (channel) { run.channel = std::move(channel); run.serial = 0; run.css_requested = false; }
  channel = run.channel;
  if (!channel) return;
  channel->working.store(true,std::memory_order_release);
  struct Working { std::shared_ptr<Channel> c; ~Working() { c->working.store(false,std::memory_order_release); } } working{channel};
  if (channel->cancelled.load(std::memory_order_acquire)) return;
  const auto q = std::atomic_load_explicit(&channel->request,std::memory_order_acquire);
  if (!q) return;
  const auto now = yougame::transport::now_ms();
  if (q->serial != run.serial) {
    run.serial = q->serial; run.request_at = now; run.css_bodies = run.waited_bodies = 0;
  } else {
    const auto old = std::atomic_load_explicit(&channel->result,std::memory_order_acquire);
    if (old && old->serial == q->serial) return;
  }
  auto result = std::make_shared<Result>(); result->serial = q->serial;
  auto publish = [&] {
    std::shared_ptr<const Result> immutable = result;
    std::atomic_store_explicit(&channel->result,std::move(immutable),std::memory_order_release);
  };
  int32_t scene[2]{};
  if (!run.ready || !g_game.practice || g_game.practice(MU_PRACTICE_SCENE,scene,2) != 0 ||
      g_console_boot || yougame::console::console_active() || g_replaying || g_online_test_mode >= 0 ||
      slippi::online::session_mode() >= 0) { result->reason = "live-state-unavailable"; publish(); return; }
  if (q->operation == Operation::GuestCSS && !run.css_requested) {
    run.css_requested = true;
    if (scene[0] != 2 || scene[1] != 0) {
      int32_t major = 2;
      if (g_game.practice(MU_PRACTICE_REQUEST_MAJOR,&major,1) != 0) {
        result->reason = "css-request-failed"; publish();
      }
      return;
    }
  }
  const bool css = scene[0] == 2 && scene[1] == 0;
  const bool quiet = g_handoff_callbacks_pending && quiescent(g_handoff_disc_depth,g_handoff_callbacks_pending());
  ++run.waited_bodies;
  if (q->operation == Operation::GuestCSS) {
    run.css_bodies = css && quiet ? run.css_bodies+1 : 0;
    if (run.css_bodies >= 30) publish();
    else if (now-run.request_at >= 15000) { result->reason = "at-css-timeout"; publish(); }
    return;
  }
  if (!css) { result->reason = "host-not-vs-css"; publish(); return; }
  if (!quiet) {
    if (now-run.request_at >= 5000) { result->reason = "not-quiescent"; publish(); }
    return; // keep ordinary bodies running while any excluded callback remains
  }
  host::log("handoff-join: quiescent after %llu bodies",(unsigned long long)run.waited_bodies);
  handoff_state::State state{run.frame,2,0,{}};
  handoff_state::Sizes sizes;
  std::string why;
  auto bytes = std::make_shared<Buffer>();
  const auto began = yougame::transport::now_ms();
  if (q->operation == Operation::Base) {
    try {
      if (!capture_base(run.identity,run.spans,state,*result,why,q->storage)) result->reason = "base-capture-failed";
    } catch (...) { result->reason = "base-capture-failed"; }
    publish(); return;
  }
  // No game poll, callback, retrace or scene continuation can run in this scope. All work is
  // on this thread. The temporary guest load is undone before leaving this exact native frame.
  channel->paused_at.store(began,std::memory_order_release);
  channel->paused.store(true,std::memory_order_release);
  struct Resume {
    std::shared_ptr<Channel> channel;
    const std::vector<native_savestate::Span>& spans;
    Buffer backup;
    bool restore = false;
    ~Resume() {
      size_t at = 0;
      if (restore) for (const auto& s : spans) { std::memcpy(s.address,backup.data()+at,s.size); at += s.size; }
      channel->paused.store(false,std::memory_order_release);
    }
  } resume{channel,run.spans,{}};
  try {
    if (q->operation == Operation::PauseDelta) {
      Json record{{"v",1},{"frame",run.frame},{"ticks",host::cpu->tb},{"epoch",host::console_epoch_ticks()},
          {"retraces",host::retrace_count()},{"nextRetrace",host::next_retrace_tb()},
          {"seed",q->seed},{"seedPolicy","reseed-at-N"}};
      const auto text = record.dump(); state.host.assign(text.begin(),text.end());
      host::log("handoff-join: paused at frame %llu",(unsigned long long)run.frame);
      if (!q->base || !handoff_state::encode_delta(*q->base,run.identity,run.spans,state,*bytes,result->pages,why)) {
        result->reason = "delta-capture-failed";
      } else {
        result->bytes = bytes; result->frame = run.frame;
        result->hash = frame_hash(native_savestate::hash_spans(run.spans));
        host::log("handoff-join: delta pages %zu stored %zu bytes ms %llu",result->pages,bytes->size(),
            (unsigned long long)(yougame::transport::now_ms()-began));
      }
    } else {
      // Retain B's bytes before touching live memory. Restoring is allocation-free and cannot fail.
      for (const auto& s : run.spans) resume.backup.insert(resume.backup.end(),s.address,s.address+s.size);
      handoff_state::State decoded; std::vector<uint8_t> raw;
      bool ok = q->base && q->delta && handoff_state::apply_delta(*q->base,*q->delta,run.identity,run.spans,*bytes,why,&result->pages) &&
          handoff_state::inspect(*bytes,run.identity,run.spans,decoded,sizes,raw,why);
      Json record;
      if (ok) {
        record = Json::parse(decoded.host.begin(),decoded.host.end(),nullptr,false);
        ok = record.is_object() && record.value("v",Json()) == 1 && record.value("frame",Json()) == decoded.frame &&
            record.value("seed",Json()) == q->seed && record.value("seedPolicy",Json()) == "reseed-at-N" &&
            decoded.scene_major == 2 && decoded.scene_minor == 0;
        for (const char* field : {"ticks","epoch","retraces","nextRetrace"})
          ok = ok && record.count(field) && record[field].is_number_unsigned();
      }
      if (ok) { resume.restore = true; ok = handoff_state::load(*bytes,run.identity,run.spans,state,sizes,why); }
      if (!ok) result->reason = "load-validation-failed";
      else { result->frame = state.frame; result->hash = frame_hash(native_savestate::hash_spans(run.spans)); }
      host::log("handoff-join: load %s",ok ? "ok" : "refused load-validation-failed");
    }
    if (result->reason.empty()) host::log("handoff-join: hash %s at frame %llu",result->hash.c_str(),(unsigned long long)result->frame);
    host::log("handoff-join: audio not reset"); // no imported body is run in this milestone
    result->ms = yougame::transport::now_ms()-began; publish();
    wait_paused(q->operation,*result,*channel,began,yougame::transport::now_ms,host::exit_requested,[] {
      host::window_pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    if (q->operation == Operation::PauseDelta) host::log("handoff-join: paused %llu ms",
        (unsigned long long)(yougame::transport::now_ms()-began));
  } catch (...) {
    result = std::make_shared<Result>(); result->serial = q->serial;
    result->reason = "capture-failed"; publish();
  }
}

// The snapshot of an online match: the game's capture ranges (all of main memory and the image's
// writable sections) minus its exclusions. They are fixed within a match, so the spans are computed
// once, at online frame 1.
void begin_native_savestates(int slots = slippi::ROLLBACK_MAX_FRAMES) {   // a replay viewer keeps more (replay_keep)
  std::vector<MuStateRegion> ranges(g_game.snapshot_ranges(nullptr, 0));
  g_game.snapshot_ranges(ranges.data(), (uint32_t)ranges.size());
  std::vector<MuStateRegion> exclusions(g_game.state_exclusions(nullptr, 0));
  g_game.state_exclusions(exclusions.data(), (uint32_t)exclusions.size());
  append_sound_driver_exclusions(exclusions);
  // Diagnostic (MELEE_SNAPSHOT_EXCLUDE=lo-hi[,lo-hi...], hex): leave extra ranges out, to find
  // which memory a rollback must not restore.
  if (const char* extra = host::test_env::getenv("MELEE_SNAPSHOT_EXCLUDE")) {
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
  gx::mark_discontinuity(true);
  if (!yougame::console::console_engine().match_active()) slippi::online::note_rollback(frame);
  if (g_savestates.stats().loads % 200 == 0) log_savestate_stats("so far");
}

int32_t h_console_rollback(int32_t operation, uint32_t body, MuConsoleInputs* inputs) {
  auto& engine = yougame::console::console_engine();
  // Loss may already have ended input ownership before the shim observes the failure.
  if (g_console_boot && operation == MU_CONSOLE_FAIL) {
    g_savestates.end();
    engine.end("console rollback failed at body " + std::to_string(body));
    return 1;
  }
  if (!engine.rollback_enabled()) return 0;
  switch (operation) {
  case MU_CONSOLE_QUERY: return 1;
  case MU_CONSOLE_SERVICE: engine.service_clock(body != 0); return 1;
  case MU_CONSOLE_MATCH_BEGIN:
    if (!engine.begin_match()) return 0;
    // Loading and VS on_enter have finished. Never depend on Slippi's first B0.
    begin_native_savestates();
    if (!g_savestates.active()) { engine.end("console rollback snapshot initialization failed"); return 0; }
    if (inputs) { inputs->epoch = engine.match_epoch(); inputs->local_port = engine.config().my_port - 1; }
    return 1;
  case MU_CONSOLE_FORWARD:
    if (!engine.match_active() || !h_console_frame_begin()) return 0;
    if (engine.frame() != engine.match_origin() + body) {
      engine.end("console rollback body/session frame mismatch"); return 0;
    }
    return inputs && engine.inputs(body, true, *inputs);
  case MU_CONSOLE_REPLAY:
    if (!engine.replay(body)) return 0;
    return inputs && engine.inputs(body, false, *inputs);
  case MU_CONSOLE_INPUTS: return inputs && engine.inputs(body, false, *inputs);
  case MU_CONSOLE_VALIDATED: return engine.validated(body);
  case MU_CONSOLE_BOUNDARY: {
    MuStatePod state{};
    if (g_game.state_snapshot) g_game.state_snapshot(&state);
    return engine.boundary(body, state);
  }
  case MU_CONSOLE_BODY_END: return engine.finish_body(body);
  case MU_CONSOLE_CONFIRM:
    return engine.confirm(body) && inputs && engine.inputs(body, false, *inputs);
  case MU_CONSOLE_COMMIT: {
    MuStatePod state{};
    if (g_game.state_snapshot) g_game.state_snapshot(&state);
    if (!engine.commit_match(body, state)) return 0;
    g_savestates.end();
    return 1;
  }
  case MU_CONSOLE_MATCH_END:
    engine.end_match();
    g_savestates.end();
    return 1;
  default: return 0;
  }
}

int32_t h_console_alarm_clock(uint64_t* simulation, uint64_t* hardware) {
  auto& engine = yougame::console::console_engine();
  if (!engine.rollback_enabled()) return 0;
  *simulation = engine.servicing() ? engine.alarm_ticks() : engine.clock_ticks();
  *hardware = engine.alarm_ticks();
  return 1;
}

// Determinism self-test (MELEE_ONLINE_SELFTEST, game side in mu_online.c): the game keeps its state
// at the top of a frame, rolls back a few frames, re-simulates them with the same inputs and asks
// for a comparison at the same point. Any difference is non-determinism in the rollback path.
constexpr uint8_t CMD_SELFTEST_KEEP = 0xF0, CMD_SELFTEST_COMPARE = 0xF1;   // native test only

// Training Lab savestates (shim/mu_lab.c): one kept state on the rollback snapshot engine, under a
// frame number no online match uses. The engine frees its slots on a load (rollback semantics), so
// a lab load keeps the state again at once and it can be returned to as often as wanted.
constexpr uint8_t CMD_LAB_SAVE = 0xF2, CMD_LAB_LOAD = 0xF3, CMD_LAB_ADVANTAGE = 0xF4, CMD_CONTENT_MODE = 0xF5;
constexpr uint8_t CMD_PRACTICE_CPU = 0xF6;   // the practice CPU's settings (port/runtime/hle/practice_cpu.h)
constexpr uint8_t CMD_VS_PRO = 0xC8;   // VS Mode: the pro each port's CPU plays as (shim/mu_practice_cpu.c, practice_cpu::vs_pro)
constexpr uint8_t CMD_FRAME_EVENT = 0xE8;   // replay frame data for the coach (shim/mu_framedata.c, host/replay_coach.h)
constexpr uint8_t CMD_REPLAY_DEFENSE = 0xE9;   // a takeover's opponents once the game leaves the replay (shim/mu_replay_defend.c)
constexpr uint8_t CMD_ZELDA_AS_SHEIK = 0xDE;  // a human's Zelda starts as Sheik (shim/mu_slippi_css.c): offline only; upstream 0xE6, CMD_YOUGAME_FRIENDS_MENU in this build
constexpr uint8_t CMD_CSS_FRAME = 0xDA;  // every character select frame (shim/mu_slippi_css.c): the easter egg; upstream 0xEC, MU_SQUAD_COMMAND in this build
constexpr uint8_t CMD_ARENA_HAND = 0xDB;  // the online CSS hand (shim/mu_slippi_css.c): an arena room's side panels; upstream 0xEF, CMD_YOUGAME_MENU in this build
constexpr uint8_t CMD_ARENA_PICK = 0xDC;  // the online CSS pick (shim/mu_slippi_sss.c): an arena room's portraits; upstream 0xEE, MU_MAKER_COMMAND in this build
constexpr uint8_t CMD_ARENA_PAIRING = 0xDD;  // an arena room's pairing count (shim/mu_slippi_css.c, hle/slippi_arena.h); upstream 0xED, MU_PICK_PREVIEW_COMMAND in this build
constexpr uint8_t CMD_BUTTON_LABEL = 0xDF;  // a prompt's key name for a keyboard player (shim/mu_slippi_css.c mu_host_button_label): payload port, PAD bit (BE16); reply the name, empty for none
constexpr uint8_t CMD_GHOST_AI = 0xEA;         // a pro AI's controller for this frame (hle/ghost_ai.h)
constexpr uint8_t CMD_STAGE_SONGS = 0xEB;      // a stage's main and alternate songs (gr/ground.c, hle/jukebox.h)
void apply_content_mode(int mode);
constexpr int32_t kLabFrame = -1000000;
// The replay viewer's controls (shim/mu_replay.c mu_replay_frame_begin, host/replay_control.h):
// payload s32 next replay frame (BE), u8 0 ask / 1 restore now. Defined with the replay host below.
constexpr uint8_t CMD_REPLAY_GATE = 0xFA;
uint8_t replay_gate(int32_t frame, uint8_t phase);
// Tick re-run (host.h tick_redo_*, shim/mu_redo.c): op 1 after a drawn frame (keep the state, arm when a
// match is running), 0 not possible now, 2 the gate's question, 3 go back to the state the current tick
// started from. Two states matter: the one taken after the previous tick's frame (what the current tick
// started from, the target of a re-run) and the one taken after the current tick's frame (the next
// tick's start). Going back drops the second: the re-run's own frame replaces it.
constexpr uint8_t CMD_TICK_REDO = 0xC7;
constexpr int32_t kRedoBase = -900000;   // above the Lab's state, below any match frame
uint8_t tick_redo(uint8_t op) {
  static int32_t next_id = kRedoBase, start_id = 0, end_id = 0;   // 0: none kept
  static bool have_start = false, have_end = false;
  if (!host::tick_redo_enabled()) return 0;
  if (op == 2) return host::tick_redo_wanted() && have_start ? 1 : 0;
  if (op == 3) {
    if (!have_start || !g_savestates.active() || !g_savestates.load_keep(start_id)) {
      host::log("tick re-run: the tick's starting state is gone; this body runs as a new tick");
      have_start = have_end = false;
      host::tick_redo_arm(false);
      return 0;
    }
    have_end = false;   // dropped by the load
    gx::mark_discontinuity();
    host::tick_redo_begin();
    return 1;
  }
  if (op == 1) {
    uint32_t major = 0, minor = 0, match_frame = 0;
    host::current_scene(&major, &minor, &match_frame);
    static uint32_t last_match_frame = 0;
    const bool rerun = host::tick_redo_active();   // this frame is a tick drawn a second time
    // Not while the match is being recorded or streamed to spectators: a tick run twice would be written twice.
    static const bool recording = std::getenv("MELEE_SOURCE_RECORD") != nullptr;
    const bool playing = !recording && major == 2 && minor == 2 && match_frame > 60 && (rerun || match_frame != last_match_frame);
    last_match_frame = match_frame;
    if (!playing) { have_start = have_end = false; host::tick_redo_arm(false); return 0; }
    if (!g_savestates.active()) begin_native_savestates();
    if (next_id >= -1000) next_id = kRedoBase;   // hours of ticks later
    // A new tick's frame: its start is what the last frame ended with. A re-run's frame: the start stays.
    if (!rerun) { have_start = have_end; start_id = end_id; }
    end_id = next_id++;
    g_savestates.capture(end_id);
    have_end = true;
    static uint64_t captures = 0;
    if (++captures % 3600 == 1)
      host::log("tick re-run: state kept each tick, %.2f ms on average, %.2f at most", g_savestates.stats().capture_ms_total / std::max<uint64_t>(1, g_savestates.stats().captures), g_savestates.stats().capture_ms_max);
    host::tick_redo_arm(have_start);
    return have_start ? 1 : 0;
  }
  have_start = have_end = false;
  host::tick_redo_arm(false);
  return 0;
}
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
#ifdef YOUGAME_AGENT_TEST_HOOK
void handoff_refused(const std::string& reason) {
  host::log("handoff: refused: %s", reason.c_str());
  g_handoff.failed = true;
}
using HandoffFile = std::unique_ptr<FILE, decltype(&std::fclose)>;
HandoffFile handoff_open(const std::string& name, yougame::agent_paths::Mode mode) {
  return HandoffFile(yougame::agent_paths::open(yougame::agent_paths::session.path, name, mode), &std::fclose);
}
void handoff_start(const MuStateRegion* regions, uint32_t count, std::vector<MuStateRegion> exclusions,
                   const MuHostApi* api) {
  const auto& config = yougame::agent_test::handoff;
  if (!config.enabled()) return;
  if (!config.error.empty()) { handoff_refused(config.error); return; }
  auto& run = g_handoff;
  std::string why;
  if (!handoff_state::library_identity(g_loaded_game_path, run.identity, why)) { handoff_refused(why); return; }
  // What a console savestate skips (audio driver and mixer, renderer bridge, host plumbing, the
  // sound driver ranges beside the library) stays B's own: it is host state, not game state.
  append_sound_driver_exclusions(exclusions);
  const auto whole = native_savestate::subtract(std::vector<MuStateRegion>(regions, regions+count), {});
  run.spans = native_savestate::subtract(std::vector<MuStateRegion>(regions, regions+count), exclusions);
  size_t excluded = 0;
  for (const auto& e : exclusions) excluded += e.size;
  host::log("handoff: %zu exclusions (%zu bytes) stay this process's own", exclusions.size(), excluded);
  run.keep = config.keep;
  // mu_game_entry stores exactly one host-owned binding: mu_host = api. It is private in
  // ELF builds, so locate ALL copies of the bound value in validated writable spans rather
  // than relying on exports or a build-specific address. No other startup API/table is passed.
  size_t bindings = 0;
  const uintptr_t value = reinterpret_cast<uintptr_t>(api);
  for (const auto& s : whole) for (size_t i = 0; i+sizeof value <= s.size; ++i) {
    if (std::memcmp(s.address+i, &value, sizeof value)) continue;
    const auto address = reinterpret_cast<uintptr_t>(s.address+i);
    ++bindings;
    bool captured = false;
    for (const auto& c : run.spans)
      captured = captured || (s.address+i >= c.address && s.address+i+sizeof value <= c.address+c.size);
    if (!captured) { host::log("handoff: host API word 0x%llX +%zu already excluded", (unsigned long long)address, sizeof value); continue; }
    run.keep.push_back({address, sizeof value});
    host::log("handoff: keep host API word 0x%llX +%zu", (unsigned long long)address, sizeof value);
  }
  if (!bindings) { handoff_refused("mu_host binding not found in snapshot spans"); return; }
  // Bounds: the game's own regions. A keep inside an exclusion is redundant, not wrong.
  if (!handoff_state::validate_keep(run.keep, whole, why)) { handoff_refused(why); return; }
  {
    std::vector<handoff_state::Range> clipped;   // only the captured parts need preserving
    for (const auto& r : run.keep) for (const auto& c : run.spans) {
      const auto lo = std::max<uintptr_t>(r.address, reinterpret_cast<uintptr_t>(c.address));
      const auto hi = std::min<uintptr_t>(r.address+r.size, reinterpret_cast<uintptr_t>(c.address)+c.size);
      if (lo < hi) clipped.push_back({lo, static_cast<uint32_t>(hi-lo)});
    }
    run.keep = std::move(clipped);
  }
  std::vector<MuStateRegion> kept;
  for (const auto& r : run.keep) kept.push_back({reinterpret_cast<void*>(r.address), r.size});
  std::vector<MuStateRegion> captured;
  for (const auto& s : run.spans) captured.push_back({s.address, s.size});
  run.compared = native_savestate::subtract(captured, kept);
  host::log("handoff: guest event queue unavailable (mu_events/mu_event_count/mu_delivering are static; no host ABI accessor)");
  run.ready = true;
}
void boot_match_tick() {
  auto& run = g_boot_match;
  if (!g_game.practice || !yougame::agent_test::boot_match.load(std::memory_order_acquire)) return;
  int32_t scene[2]{};
  if (run.asked) {
    // Asked, VS not reached yet: anything but the menu (1) or VS (2) means the boot was lost, and a
    // later VS (the title's attract demo, 11-09) is not ours, so "match started" must not claim it.
    if (run.requested && !run.started && g_game.practice(MU_PRACTICE_SCENE, scene, 2) == 0 &&
        scene[0] != 1 && scene[0] != 2) {
      run.requested = false;
      host::log("agent-test: bootMatch failed: the menu left for scene %02d/%02d, not VS", (int)scene[0], (int)scene[1]);
    }
    return;
  }
  if (g_game.practice(MU_PRACTICE_SCENE, scene, 2) != 0) return;
  if (scene[0] != 1) { run.menu_bodies = 0; return; }
  if (++run.menu_bodies < 120) return;   // the menu's own setup first, as the CPU bracket waits
  run.asked = true;
  if (g_console_boot || yougame::console::console_active() || g_replaying || g_online_test_mode >= 0 ||
      slippi::online::session_mode() >= 0) {
    host::log("agent-test: bootMatch refused (not an offline menu)");
    return;
  }
  const int32_t stage = g_game.practice(MU_PRACTICE_BOOT_MATCH, nullptr, 0);
  if (stage < 0) { host::log("agent-test: bootMatch refused by the game (no match, or an older game library)"); return; }
  run.requested = true;
  host::log("agent-test: bootMatch requesting VS (stage %d)", stage);
}
void handoff_tick() {
  auto& run = g_handoff;
  const auto& config = yougame::agent_test::handoff;
  if (!run.ready || run.failed) return;
  ++run.frame;
  int32_t scene[2]{};
  if (g_game.practice(MU_PRACTICE_SCENE, scene, 2) != 0) { handoff_refused("scene query failed"); return; }
  if (g_console_boot || yougame::console::console_active() || g_replaying || g_online_test_mode >= 0 ||
      slippi::online::session_mode() >= 0) { handoff_refused("requires offline local CSS"); return; }
  // Identical automatic CSS entry in A and B; neither needs boot/menu controller input.
  if (!run.boot_requested) {
    run.boot_requested = true;
    if (scene[0] != 2 || scene[1] != 0) {
      int32_t major = 2;
      if (g_game.practice(MU_PRACTICE_REQUEST_MAJOR, &major, 1) != 0) handoff_refused("CSS boot request failed");
      return;
    }
  }
  if (!run.css_seen && scene[0] == 2 && scene[1] == 0) {
    run.css_seen = true; run.css_start = run.frame;
    host::log("handoff: CSS first frame %llu, script frame %u", (unsigned long long)run.frame, host::retrace_count());
  }
  if (!run.css_seen) return;
  const auto relative = run.frame-run.css_start;
  if (!run.acted && relative == config.at) {
    run.acted = true;
    if (scene[0] != 2 || scene[1] != 0) { handoff_refused("not in local VS CSS at handoffAt"); return; }
    if (g_handoff_disc_depth) { handoff_refused("disc read/callback pending"); return; }
    handoff_state::State state{run.frame, uint32_t(scene[0]), uint32_t(scene[1]), {}};
    handoff_state::Sizes sizes;
    std::vector<uint8_t> file;
    std::string why;
    const bool saving = !config.save.empty();
    if (saving) {
      if (!handoff_state::encode(run.identity, run.spans, state, file, sizes, why)) { handoff_refused(why); return; }
      auto out = handoff_open(config.save, yougame::agent_paths::Mode::Truncate);
      if (!handoff_state::write_file(out.get(), file, why)) { handoff_refused(why); return; }
    } else {
      auto in = handoff_open(config.load, yougame::agent_paths::Mode::Read);
      const handoff_state::Gate gate{run.frame, uint32_t(scene[0]), uint32_t(scene[1]), g_handoff_disc_depth != 0, false};
      if (!handoff_state::read_file(in.get(), run.spans, file, why) ||
          !handoff_state::load(file, run.identity, run.spans, state, sizes, why, &gate, run.keep)) {
        handoff_refused(why); return;
      }
      gx::mark_discontinuity();
    }
    run.action_frame = run.frame;
    host::log("handoff: %s %llu bytes, %zu spans, hash %016llX, frame %llu, scene %u/%u, tb %llu, retraces %u, next_retrace_tb %llu, script frame %u, body frame %llu, css frame %llu, epoch %llu",
        saving ? "saved" : "loaded", (unsigned long long)sizes.file, run.spans.size(),
        (unsigned long long)native_savestate::hash_spans(run.compared), (unsigned long long)state.frame,
        state.scene_major, state.scene_minor, (unsigned long long)host::cpu->tb, host::retrace_count(),
        (unsigned long long)host::next_retrace_tb(), host::retrace_count(), (unsigned long long)run.frame,
        (unsigned long long)relative, (unsigned long long)host::console_epoch_ticks());
    if (!config.diff_dump.empty()) {
      auto out = handoff_open(config.diff_dump, yougame::agent_paths::Mode::Truncate);
      if (!handoff_state::dump(out.get(), run.spans, why)) { handoff_refused(why); return; }
    }
    if (!config.hash_log.empty()) {
      auto out = handoff_open(config.hash_log, yougame::agent_paths::Mode::Truncate);
      if (!out) { handoff_refused("cannot open hash log"); return; }
    }
  }
  if (!run.action_frame || run.frame <= run.action_frame) return;
  const auto after = run.frame-run.action_frame;
  if (after > 600 || after % 60 || config.hash_log.empty()) return;
  auto out = handoff_open(config.hash_log, yougame::agent_paths::Mode::Append);
  if (!out) { handoff_refused("cannot append hash log"); return; }
  for (size_t i = 0; i < run.spans.size(); ++i) {
    const auto& s = run.spans[i];
    const auto lo = reinterpret_cast<uintptr_t>(s.address);
    std::vector<native_savestate::Span> parts;
    for (const auto& p : run.compared)
      if (reinterpret_cast<uintptr_t>(p.address) >= lo && reinterpret_cast<uintptr_t>(p.address)-lo < s.size) parts.push_back(p);
    std::fprintf(out.get(), "after %llu frame %llu span %zu address 0x%llX bytes %u hash %016llX\n",
        (unsigned long long)after, (unsigned long long)run.frame, i, (unsigned long long)lo, s.size,
        (unsigned long long)native_savestate::hash_spans(parts));
  }
  std::fprintf(out.get(), "after %llu frame %llu total %016llX\n", (unsigned long long)after,
      (unsigned long long)run.frame, (unsigned long long)native_savestate::hash_spans(run.compared));
  if (std::ferror(out.get()) || std::fflush(out.get())) { handoff_refused("hash log write failed"); return; }
  if (after == 600) host::log("handoff: hashlog done");
}
#endif
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
// payload: port, fighter, costume, direction; optional fifth byte is the allowed offline colors.
// reply: changed, optional new costume for a five-byte request, then the choice's name.
constexpr uint8_t CMD_SKIN_CYCLE = 0xF9;

// The slot's disc files after the catalog published it again: each one gets the entry that serves
// it now, and the path opens that entry. A skin is a new entry number in its own range past
// kCosmeticBase, so nothing the game preloaded under the old number is reused; a skin not proven to
// change looks alone also gets the alias entry that names the disc's copy for online play, as at
// startup. False when a file could not be given an entry (the caller puts the old pick back).
bool publish_cosmetic_files(const std::string& slot, const host::cosmetics::RepublishResult& result) {
  if (!result.ok) return false;
  bool ok = true;
  for (const auto& file : result.files) {
    const uint32_t i = file.fst_index;
    if (i >= g_raw_paths.size() || g_raw_paths[i].empty() || (size_t)i * 12 + 12 > g_fst_raw.size()) continue;
    // A file the loaded pack replaced: entry i is the pack's file, the standard costume of this
    // session, and only the pack's own sets may serve it instead (load_cosmetics).
    const bool pack_replaced = g_fst[i].offset != file.vanilla_start && g_fst[i].offset < kCosmeticBase;
    if (pack_replaced && (!file.overridden || (!host::cosmetics::session_pack_skin(file.asset_id) && !pack_costume_takes_skins(i) && !pack_stage_takes_skins(i)))) {
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

bool republish_cosmetic_slot(const std::string& slot, bool stage = false) {
  return publish_cosmetic_files(slot, stage
      ? host::cosmetics::republish_stage(g_cosmetic_fst.data(), (uint32_t)g_cosmetic_fst.size(), slot)
      : host::cosmetics::republish_slot(g_cosmetic_fst.data(), (uint32_t)g_cosmetic_fst.size(), slot));
}

// One command at the new match scene boundary, before any stage DVD preload.
constexpr uint8_t CMD_STAGE_SKIN_MATCH = 0xFD;
void plan_random_stage_skin(const std::string& file, std::vector<uint8_t>& reply) {
  reply.assign(1, 0);
  if (g_replaying || g_cosmetic_fst.empty()) return;
  static uint64_t match_token = 0;
  const auto result = host::cosmetics::plan_stage_skin(g_cosmetic_fst.data(),
      (uint32_t)g_cosmetic_fst.size(), file, ++match_token);
  if (result.ok && publish_cosmetic_files(file, result)) reply[0] = 1;
}

// ---- stage select skin cycling (mn/mnstagesel.c, shim/mu_content.c) ----
// payload: direction (1 next, 0 previous), then the stage's file name. reply: 1 when it changed.
constexpr uint8_t CMD_STAGE_SKIN_CYCLE = 0xFB;
// Title demo memory check (shim/mu_content.c mu_title_demo_stage): reply is four bytes, big endian,
// the sum of the four largest growths among the installed skins that are not stage files.
constexpr uint8_t CMD_SKIN_GROWTH = 0xFC;
void cycle_stage_skin(const std::string& file, int direction, std::vector<uint8_t>& reply) {
  reply.assign(1, 0);
  if (g_replaying || g_cosmetic_fst.empty() || slippi::online::session_mode() >= 0) return;
  const auto pick = host::cosmetics::cycle_stage_live(file, direction);
  if (!pick.ok || !pick.changed) {
    if (!pick.message.empty()) host::log("cosmetics: %s skin not changed (%s)", file.c_str(), pick.message.c_str());
    return;
  }
  if (!republish_cosmetic_slot(file, true)) {
    // No entry for it: the previous pick goes back, saved and published.
    host::cosmetics::cycle_stage_live(file, -direction);
    republish_cosmetic_slot(file, true);
    return;
  }
  screen_label::show(("Stage skin: " + pick.name).c_str(), 2.5);
  host::log("cosmetics: stage select picked %s for %s", pick.name.c_str(), file.c_str());
  reply[0] = 1;
}

void cycle_costume_skin(int character, int costume, int direction, std::vector<uint8_t>& reply,
                        uint32_t allowed_costumes = 0) {
  reply.assign(1, 0);
  if (g_replaying || g_cosmetic_fst.empty()) return;
  std::string slot = host::cosmetics::costume_slot_file(character, costume);
  // An m-ex screen may currently show an added color. The offline fighter-wide request can
  // return to a native slot; the original per-color command still requires a native slot.
  if (slot.empty() && !allowed_costumes) return;
  host::cosmetics::LiveCycle pick;
  if (allowed_costumes) {
    const auto cycle = host::cosmetics::cycle_character_live(character, costume, direction, allowed_costumes);
    pick = cycle.pick;
    if (pick.changed) { slot = cycle.target; costume = cycle.costume; }
  } else pick = host::cosmetics::cycle_slot_live(slot, direction);
  if (!pick.ok || !pick.changed) {
    if (!pick.message.empty()) host::log("cosmetics: %s skin not changed (%s)", slot.c_str(), pick.message.c_str());
    // Say why a press did nothing, except on a costume with no skins at all.
    if (!pick.message.empty() && pick.message.rfind("No other skin", 0) != 0) screen_label::show(pick.message.c_str(), 3.0);
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
  screen_label::show(("Skin: " + pick.name).c_str(), 2.5);
  reply[0] = 1;
  if (allowed_costumes) reply.push_back((uint8_t)costume);
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

#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
// Match setup precedes 0x36. Keep this separate from the recording still being saved.
bool g_squad_replay_pending = false;
#endif

int32_t h_slippi_command(uint8_t command, const uint8_t* payload, uint32_t payload_size,
                         uint8_t* response, uint32_t response_capacity, uint32_t* response_size) {
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  // Handle locally even in MU_NO_SLIPPI; no online command validation or network I/O.
  if (command == MU_SQUAD_COMMAND && payload && payload_size == 2 &&
      payload[0] == MU_SQUAD_OP_REPLAY_MATCH && payload[1] <= 1) {
    g_squad_replay_pending = payload[1] != 0;
    if (response_size) *response_size = 0;
    return 0;
  }
  // The squadLineup agent-test key, for the online squad CSS. Builds without the hook answer
  // nothing, so the CSS stays a player's.
  if (command == MU_SQUAD_COMMAND && payload && payload_size == 1 &&
      payload[0] == MU_SQUAD_OP_AGENT_LINEUP) {
    if (response_size) *response_size = 0;
#ifdef YOUGAME_AGENT_TEST_HOOK
    uint8_t lineup[2 * MU_SQUAD_LINEUP];
    if (response && response_capacity >= sizeof lineup &&
        yougame::agent_test::squad_lineup_bytes(lineup)) {
      std::memcpy(response, lineup, sizeof lineup);
      if (response_size) *response_size = sizeof lineup;
    }
#endif
    return 0;
  }
#endif
  // Console capture/load bypass Slippi dispatch altogether. A failed load never resumes as if
  // it succeeded; the shim exits this session with an explicit epoch/body diagnostic.
  auto& console = yougame::console::console_engine();
#ifdef MELEE_DOUBLES
  if (command==0xE6 && payload_size==1 && payload[0]==11) {
    if(response_size) *response_size=0;
    if(!console.active() || !console.config().doubles || response_capacity<10) return 0;
    const yougame::doubles::Rules rules;
    response[0]=uint8_t(rules.stocks); response[1]=uint8_t(rules.minutes);
    response[2]=uint8_t(rules.items); response[3]=uint8_t(rules.friendly_fire);
    response[4]=uint8_t(console.config().doubles_stage>>8); response[5]=uint8_t(console.config().doubles_stage);
    for(int i=0;i<4;++i) response[6+i]=uint8_t(console.config().doubles_teams[i]);
    if(response_size) *response_size=10;
    return 0;
  }
#endif
  if (command == 0xE6 && payload_size == 1 && payload[0] == 10) {
    if (response_size) *response_size = 0;
    const auto pick = yougame::friends::css_pick(console.config().settings_digest);
    if (!console.active() || !pick || response_capacity < 4) return 0;
    response[0] = uint8_t(pick->character); response[1] = uint8_t(pick->costume);
    response[2] = uint8_t(pick->pad); response[3] = 1; // native Friends host is frozen port 1
    if (response_size) *response_size = 4;
    return 0;
  }
  if (console.match_active() && (command == 0xB1 || command == 0xB2)) {
    if (response_size) *response_size = 0;
    if (payload_size != 32 || !g_savestates.active()) return -1;
    const int32_t frame = (int32_t)read_be32(payload);
    if (command == 0xB2 && !g_savestates.has(frame)) {
      host::log("console: required snapshot missing: epoch %u body %d", console.match_epoch(), frame);
      g_savestates.end();
      console.end("required console rollback snapshot missing");
      return -1;
    }
    if (command == 0xB1) {
      native_capture(frame);
      console.capture_clock(frame);
    } else {
      if (!console.restore_clock(frame)) {
        console.end("required console rollback clock missing");
        return -1;
      }
      native_load(frame, payload);
    }
    return 0;
  }
  if (yougame::console::console_active() &&
      (command == CMD_LAB_SAVE || command == CMD_LAB_LOAD || command == CMD_GHOST_AI ||
       command == CMD_PRACTICE_CPU || command == CMD_SKIN_CYCLE || command == CMD_STAGE_SKIN_CYCLE ||
       command == CMD_ZELDA_AS_SHEIK || command == CMD_CSS_FRAME ||
       command == CMD_BUTTON_LABEL)) {   // the same on every console machine
    if (response_size) *response_size = 0;
    return 0;
  }
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  // Read-only harness samples are consumed here, never validated/sent as Slippi commands.
  if (command == MU_SQUAD_COMMAND && payload && payload_size == 9 &&
      (payload[0] == MU_SQUAD_OP_DIGEST_FRAME || payload[0] == MU_SQUAD_OP_DIGEST_END)) {
    if (g_online_test_squad.count && slippi::Matchmaking::local_peer.enabled)
      host::squad_digest_observe((int32_t)read_be32(payload + 1), (int32_t)read_be32(payload + 5),
                                 payload[0] == MU_SQUAD_OP_DIGEST_END);
    if (response_size) *response_size = 0;
    return 0;
  }
#endif
  return slippi::online::native_command(
      command, payload, payload_size, response, response_capacity, response_size,
      MU_SLIPPI_RESPONSE_CAPACITY, true,
      [](uint8_t c, const uint8_t* p, uint32_t n, std::vector<uint8_t>& reply) {
#if defined(YOUGAME_AGENT_TEST_HOOK) || (defined(YOUGAME_RANKED_BRANCH) && YOUGAME_RANKED_BRANCH)
        // CPU prep uses the ordinary B3/B5/BF/C0/C1 bridge, entirely locally.
        if (yougame::ranked_cpu::intercept(c, p, n, reply, yougame::active())) return true;
#endif
        // CMD_STAGE_SONGS shares 0xEB with MU_TOURNEY_COMMAND; the tournament never sends 16 bytes.
        if (c == MU_TOURNEY_COMMAND && !(c == CMD_STAGE_SONGS && n == 16)) return yougame::tournament_command(p, n, reply);
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
        if (c == CMD_TICK_REDO && n == 1) { reply.push_back(tick_redo(p[0])); return true; }
        if (c == CMD_LAB_SAVE) { lab_save(); return true; }
        if (c == CMD_LAB_LOAD) { lab_load(); return true; }
        if (c == CMD_REPLAY_GATE && n == 5) { reply.push_back(replay_gate((int32_t)read_be32(p), p[4])); return true; }
        if (c == CMD_REPLAY_DEFENSE) {
          // The Opponent setting (gx/practice_menu.h): smart defense, no DI or techs, the Practice CPU's own
          // settings, or nothing (follow the recording).
          const int defense = replay_control::defense();
          if (defense == 3) return true;
          MuPcConfig cfg = practice_cpu::config();
          if (defense == 0) {
            // Smart: every defensive option (shield, dodges, out of shield, DI and SDI, techs, getups,
            // escaping after hitstun, the ledge), and it never starts an attack in neutral.
            mu_pc_defaults(&cfg);
            cfg.aggression = 0;
            for (auto& v : cfg.move_odds) v = 0;
            cfg.move_odds[MU_PC_MOVE_STAND] = 100;
            cfg.sdi_amount = 2;
            // With the pro AI beside the game, a fighter whose character a pro plays is played by
            // that AI instead (shim/mu_replay_defend.c picks the pro): it fights back, as a player would.
            if (ghost_ai::available()) cfg.preset = MU_PC_PRESET_AI;
          } else if (defense == 1) {
            mu_pc_preset(1, &cfg);   // stands still and only reacts to being hit
            for (auto& v : cfg.di_odds) v = 0;
            cfg.di_odds[MU_PC_DI_NONE] = 100;
            for (auto& v : cfg.tech_odds) v = 0;
            cfg.tech_odds[MU_PC_TECH_MISS] = 100;
          }
          cfg.enabled = 1;
          {   // asked every frame: logged when it changes (a session logged it 10,338 times, 11-24)
            static int last[6] = {-1, -1, -1, -1, -1, -1};
            const int now[6] = {defense, cfg.aggression, cfg.tech_odds[0], cfg.tech_odds[1], cfg.tech_odds[2], cfg.tech_odds[3]};
            if (!std::equal(now, now + 6, last)) {
              std::copy(now, now + 6, last);
              host::log("replay: the opponent defends (setting %d, aggression %d, techs %d %d %d %d)", defense, cfg.aggression,
                        cfg.tech_odds[0], cfg.tech_odds[1], cfg.tech_odds[2], cfg.tech_odds[3]);
            }
          }
          const auto* bytes = reinterpret_cast<const uint8_t*>(&cfg);
          reply.assign(bytes, bytes + sizeof cfg);
          return true;
        }
        if (c == CMD_FRAME_EVENT) {
          replay_coach::Event e;
          if (replay_coach::parse(p, n, e)) {
            replay_coach::add(e);
            static const bool log_events = host::test_env::getenv("MELEE_TEST_COACH_LOG") != nullptr;   // tests
            if (log_events) {
              const replay_coach::Readout r = replay_coach::readout(e);
              host::log("coach: frame %d P%d %s %s %s | %s (move %d)", e.frame, e.actor + 1, r.head.c_str(), r.value.c_str(),
                        r.detail.c_str(), replay_coach::advice(e).c_str(), e.move);
            }
          }
          return true;
        }
        if (c == CMD_STAGE_SONGS && n == 16) {
          slippi::jukebox::note_stage_songs((int32_t)read_be32(p), (int32_t)read_be32(p + 4));
          slippi::jukebox::note_stage_songs((int32_t)read_be32(p + 8), (int32_t)read_be32(p + 12));
          return true;
        }
        if (c == CMD_LAB_ADVANTAGE && n == 5) { training_overlay::set_advantage((int32_t)read_be32(p), p[4]); return true; }
        if (c == CMD_GHOST_AI && n == sizeof(MuGhostObs)) {   // shim/mu_practice_cpu.c: a pro AI's controller
          MuGhostObs obs;
          std::memcpy(&obs, p, sizeof obs);
          MuGhostAct act;
          if (ghost_ai::act(obs, act)) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&act);
            reply.assign(bytes, bytes + sizeof act);
          }
          return true;
        }
        if (c == CMD_CSS_FRAME) slippi::online::gecko_pump();   // Gecko codes for the match (gecko_session.h)
        if (c == CMD_CSS_FRAME && n == 0) { walter_egg::set_camera(nullptr); reply.push_back(walter_egg::css_frame()); return true; }
        if (c == CMD_CSS_FRAME && n == 13 * sizeof(float)) {   // with the menu camera (shim mu_menu_camera): the C-stick's turn
          float camera[13];
          std::memcpy(camera, p, sizeof camera);
          walter_egg::set_camera(camera);
          reply.push_back(walter_egg::css_frame());
          return true;
        }
        if (c == CMD_CSS_FRAME && n == 5) {   // with five bytes: a hand moved (the port, then x and y in tenths)
          walter_egg::set_hand(p[0], (int16_t)(p[1] << 8 | p[2]), (int16_t)(p[3] << 8 | p[4]));
          return true;
        }
        if (c == CMD_ZELDA_AS_SHEIK && n == 0) {
          static const int test = [] { const char* v = host::test_env::getenv("MELEE_TEST_ZELDA_SHEIK"); return v && *v ? (*v == '1' ? 1 : 0) : -1; }();
          const bool want = test >= 0 ? test == 1 : gx::RenderOptions::live_zelda_as_sheik();
          const bool on = want && !g_replaying && slippi::online::session_mode() < 0 && !source_p2p::requested() &&
                          !host::gameplay::vanilla_only();
          if (on) host::log("game: Zelda starts as Sheik (setting)");
          reply.push_back(on ? 1 : 0);
          return true;
        }
        if (c == CMD_BUTTON_LABEL && n == 3) {   // shim/mu_slippi_css.c: the key bound to a button, keyboard ports only
          char name[16];
          if (host::input_keyboard_button_label(p[0], (uint16_t)(p[1] << 8 | p[2]), name, sizeof name))
            for (const char* q = name; *q; ++q) reply.push_back((uint8_t)*q);
          return true;
        }
        if (c == CMD_ARENA_HAND && (n == 4 || n == 5)) {   // shim/mu_slippi_css.c: the CSS hand, in tenths, and the select's mode
          slippi::arena::set_css_hand((int16_t)(p[0] << 8 | p[1]), (int16_t)(p[2] << 8 | p[3]));
          slippi::arena::set_css_mode(n == 5 ? p[4] : -1);
          return true;
        }
        if (c == CMD_ARENA_PICK && n == 3) {   // shim/mu_slippi_sss.c: the character select's pick
          slippi::arena::set_css_pick(p[2] ? p[0] : 0xFF, p[1]);
          return true;
        }
        if (c == CMD_ARENA_PAIRING && n == 0) {   // shim/mu_slippi_css.c: an arena room's new pairing
          append_be32(reply, slippi::online::arena_pairing());
          // bit 0: the room locks in a random stage itself (tests); bit 1: this player is in a room now;
          // bit 2: the room's next game is a free-for-all (no team toggle, Start locks in a random stage)
          reply.push_back((slippi::arena::locks_in_itself() ? 1 : 0) | (slippi::arena::in_room() ? 2 : 0) |
                          (slippi::arena::css_ffa() ? 4 : 0));
          return true;
        }
        if (c == CMD_VS_PRO) {   // shim/mu_practice_cpu.c: one byte per port, 0xFF for the game's own CPU
          // Never in an online match or a replay, whatever the game library asks.
          const bool offline = !g_replaying && !slippi::online::is_online_match();
          for (int port = 0; port < 4; ++port) {
            const int pro = offline ? practice_cpu::vs_pro(port) : -1;
            reply.push_back(pro < 0 ? 0xFF : (uint8_t)pro);
          }
          return true;
        }
        if (c == CMD_PRACTICE_CPU) {   // shim/mu_practice_cpu.c: the settings, or what it is doing
          if (n == 2 || n == 3) { practice_cpu::note_status(p[0] != 0, p[1], 0, n == 3 ? p[2] : -1); return true; }
          const MuPcConfig cfg = practice_cpu::config();
          const auto* bytes = reinterpret_cast<const uint8_t*>(&cfg);
          reply.assign(bytes, bytes + sizeof cfg);
          return true;
        }
        if (c == CMD_SKIN_CYCLE && (n == 4 || (n == 5 && p[4] != 0))) {
          cycle_costume_skin(p[1], p[2], p[3] ? 1 : -1, reply, n == 5 ? p[4] : 0);
          return true;
        }
        if (c == CMD_STAGE_SKIN_MATCH && n >= 2 && n <= 64 && p[n - 1] == 0) {
          plan_random_stage_skin(std::string((const char*)p, strnlen((const char*)p, n)), reply);
          return true;
        }
        if (c == CMD_SKIN_GROWTH && n == 0) {
          uint32_t growth = host::cosmetics::largest_fighter_growth();
          // MELEE_TEST_DEMO_GROWTH=<bytes>: test runs only, stands in for installed skins that much larger.
          static const uint32_t test_growth = [] {
            const char* v = host::test_env::getenv("MELEE_TEST_DEMO_GROWTH");
            return v && host::options.no_gc_adapter ? (uint32_t)std::strtoul(v, nullptr, 0) : 0u;
          }();
          growth += test_growth;
          reply = {(uint8_t)(growth >> 24), (uint8_t)(growth >> 16), (uint8_t)(growth >> 8), (uint8_t)growth};
          return true;
        }
        if (c == CMD_STAGE_SKIN_CYCLE && n >= 2 && n <= 65) {
          cycle_stage_skin(std::string((const char*)p + 1, strnlen((const char*)p + 1, n - 1)), p[0] ? 1 : -1, reply);
          return true;
        }
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
        if (c == slippi::online::CMD_YOUGAME_FRIENDS_MENU && n == 1)
          return slippi::online::handle(c, p, n, reply);
        if (c == slippi::online::CMD_YOUGAME_RANKED && n == 0) {
          // This fork's Ranked query (native_slippi_bridge.h, shim/mu_slippi_menu.c) shares 0xFA with
          // the Hack Pack, replay gate and voice commands, which always carry a payload. An empty 0xFA
          // is the Ranked query: Slippi's handler answers it, or the online menu never shows Ranked.
          return slippi::online::handle(c, p, n, reply);
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
        if (c == MU_MAKER_COMMAND && n == 1 && p[0] == MU_MAKER_OP_CACHE) {
          // Melee Maker's loading cache: 32 MB right after the 64 MB GX can address, which then grows
          // to cover it (host::try_ptr), so donor models in it draw. Mapped once, only for its disc.
          static uint32_t cache_size = 0;
          static bool tried = false;
          if (!tried && maker::loaded()) {
            tried = true;
            const uintptr_t base = MEM1_BASE + (64u << 20);
            if (VirtualAlloc((void*)base, 32u << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) == (void*)base) {
              cache_size = 32u << 20;
              host::ram_size = (64u << 20) + cache_size;
              host::log("mods: Melee Maker cache, %u MB at %08llX", cache_size >> 20, (unsigned long long)base);
            } else {
              host::log("mods: Melee Maker cache could not be mapped; its moves load into the match heap");
            }
          }
          if (cache_size) { append_be32(reply, uint32_t(MEM1_BASE + (64u << 20))); append_be32(reply, cache_size); }
          return true;
        }
        if (c == MU_MAKER_COMMAND) {
          maker::command(p, n, reply);   // Melee Maker's data for its native engine (maker_source.h)
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
#ifndef MELEE_NO_SLIPPI
        // A peer-to-peer session (--p2p-*, source_p2p.cpp) installed its handler: it owns the
        // session commands for this run, and Slippi's session code is never entered. The handler is
        // only ever installed after such a session started, so every other run takes the line below.
        if (host::netplay::has_command_handler()) return host::netplay::handle(c, p, n, reply);
#endif
        return slippi::online::handle(c, p, n, reply);
      });
}

// The view for the online mode the game reports (0-4), or offline (-1): offline and Direct with the
// mod on show the mod; YouGame Ranked/Unranked also shows a mod with a verified fingerprint. Other
// online modes show the retail game. The online rules and the build the opponent is told follow it.
// A Direct opponent on Slippi Dolphin (no build message): that match plays the retail game. Set and
// cleared by slippi_online.cpp (set_plain_opponent_handler); g_content_mode is the mode last applied.
bool g_plain_opponent = false;
int g_content_mode = -1;
slippi::online::LocalBuild content_build_for_mode(int mode) {
  const bool mods = !g_view_alias.empty();
  slippi::online::LocalBuild build;
  build.fingerprint = g_direct_fingerprint.empty() ? g_mod_fingerprint : g_direct_fingerprint;
  build.mod_view = slippi::online::native_source_mod_view(mods, mode, yougame::active(), g_plain_opponent,
                                                         gx::RenderOptions::live_mods_in_direct(), build.fingerprint,
                                                         g_cosmetic_pack || g_training_only);
  // Akaneia and ACE: their added fighters are compiled into this game library (an Akaneia disc is
  // refused without them), so Direct and Teams may pick them, as on the Static Recomp. Before this
  // only Static mod boots said so, and the Source Port refused every added fighter online.
  build.extended_content = build.mod_view && g_mod_gameplay_profile == slippi::online::NativeGameplayProfile::Akaneia;
  build.name = g_mod_display_name;
  build.allow_unverified = gx::RenderOptions::live_mods_dolphin_ok();
  return build;
}

void apply_content_mode(int mode) {
  const bool mods = !g_view_alias.empty();
  const auto build = content_build_for_mode(mode);
  const bool retail = mods && !build.mod_view;
  g_content_mode = mode;
  if (retail != g_retail_view)
    host::log("content: %s view (%s)", retail ? "retail" : "mod",
              mode < 0 ? "offline" : g_cosmetic_pack ? "online cosmetic look" :
              mode == 2 && retail ? "Direct, the opponent is on Slippi Dolphin" :
              mode == 2 ? "Direct" : mode == 0 && build.mod_view ? "YouGame Ranked, same verified build" :
              mode == 1 && build.mod_view ? "YouGame Unranked, same verified build" :
              "online mode that plays the retail game");
  g_retail_view = retail;
  mods::status().retail_view = retail;   // the panel's notice counts the pack skins swapped in this view
  slippi::online::set_local_build(build);
}

// slippi_online.cpp's word on the Direct opponent (see g_plain_opponent). The view follows at once:
// nothing of the match is loaded yet when the build wait ends.
void plain_opponent(bool plain, const char* why) {
  if (plain == g_plain_opponent) return;
  g_plain_opponent = plain;
  if (!g_view_alias.empty()) apply_content_mode(g_content_mode);
  if (plain && why) screen_label::show(why, 6.0);
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
  if (gl_costs_enabled()) {
    if (entering) g_resim_gx_bytes = 0;
    else host::log("rollback GX: resim_fifo_bytes=%llu", (unsigned long long)g_resim_gx_bytes);
  }
  // The existing read-only pause query exposes the engine body counter. Resimulation
  // loops bypass VI waits entirely, so counting those waits would miss real frames.
  if (auto* timer = host::perf::active) {
    int32_t state[3] = {-1, 0, 0};
    const bool have_frame = g_game.practice &&
        g_game.practice(MU_PRACTICE_PAGE_PAUSE, state, 3) == 0 && state[2] >= 0;
    timer->set_resim(entering != 0, static_cast<uint32_t>(state[2]), have_frame);
  }
  using namespace host::rollback;
  const auto ns = now_ns();
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (entering && yougame::net_test::config().squad_local_test)
    g_squad_test_rollback_from = rollback_engine_frame();
#endif
#endif
  if (entering) accumulator.begin(ns, rollback_engine_frame(), presentation);
  else accumulator.end(ns, rollback_engine_frame(), presentation);
  rollback_log();
  g_resim = entering != 0;
  if (g_resim) ++g_resim_phases;
}

// --online-test: the harness enters an online match without menus. The first time the game asks,
// matchmaking starts (local test peering, from --local-peer); the game then waits for the match.
void ranked_harness_tick();
int32_t h_online_test_match(MuOnlineMatch* out) {
  if (yougame::console::console_active()) return 0;
#if defined(YOUGAME_AGENT_TEST_HOOK) && !defined(MELEE_NO_SLIPPI)
  if (yougame::ranked_cpu::enabled) {
    if (out) { std::memset(out, 0, sizeof *out); out->mode = 0; }
    return 3; // boots through the online menus, never starts real matchmaking
  }
#endif
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
  // A peer-to-peer session asked for the match (--p2p-*, source_p2p.cpp): the same answer as the
  // build without the Slippi layer gives, and Slippi's matchmaking is never started. No such
  // request (every run without --p2p-*): the Slippi path below, unchanged.
  const host::netplay::MatchRequest request = host::netplay::match_request();
  if (request.requested) {
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
  }
  const auto& lobby = slippi::online::config();
  const bool from_lobby = !lobby.lobby_code.empty();
  if (g_online_test_mode < 0 && !from_lobby) return 0;
  const int mode = from_lobby ? 2 : g_online_test_mode;
  const uint8_t character = from_lobby ? (uint8_t)lobby.lobby_character : g_online_test_character;
  if (!g_online_test_started) {
    g_online_test_started = true;
    apply_content_mode(mode);   // a launcher or harness match skips the online menu's mode pick
    std::string error;
    // Tests: MELEE_ONLINE_TEST_CODE names the opponent of a harness Direct match (YouGame mode).
    const char* test_code = std::getenv("MELEE_ONLINE_TEST_CODE");
    // Teams solo queue requires an empty code, including when a Direct test code is set.
    const std::string connect_code = from_lobby ? lobby.lobby_code : mode == 3 ? "" : test_code ? test_code : "PEER#001";
#if MU_SQUAD
    // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    // Replace only the menu/CSS actions. Game-side selection sends the real lineup command
    // and computes the real disc budget before native_start_match uses the normal search.
    if (!from_lobby && g_online_test_squad.count) {
      int32_t picks[9] = {g_online_test_squad.count};
      for (unsigned i = 0; i < g_online_test_squad.count; ++i) {
        picks[1 + 2*i] = g_online_test_squad.members[i].kind;
        picks[2 + 2*i] = g_online_test_squad.members[i].color;
      }
      if (!g_game.practice || g_game.practice(MU_SQUAD_TEST_SETUP, picks,
                                             1 + 2*g_online_test_squad.count) != 0) {
        host::log("squad: cancel: harness lineup/budget preparation failed");
        host::request_exit(2);
        return 0;
      }
    }
#endif
    if (slippi::online::native_start_match(mode, connect_code, character,
                                           g_online_test_color, &error))
      host::log("online launch: searching (mode %d, character %u, color %u)", mode,
                character, g_online_test_color);
    else {
      host::log("online test: matchmaking did not start: %s", error.c_str());
      if (from_lobby) host::request_exit(2);
    }
#if MU_SQUAD
    // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    // Negative fixture: submit an invalid second lineup through the SAME game/host command.
    // Search keeps the valid squad ticket; lock-in then cancels on the production protocol path.
    if (!from_lobby && g_online_test_squad.count && g_online_test_squad_cancel) {
      const uint8_t invalid[] = {1, 1, 26, 0}; // invalid external character
      uint8_t response[MU_SLIPPI_RESPONSE_CAPACITY];
      uint32_t got = 0;
      h_slippi_command(MU_SQUAD_COMMAND, invalid, sizeof invalid, response, sizeof response, &got);
    }
#endif
  } else if (mode == 0 && !from_lobby) {
    ranked_harness_tick();   // a receipt-triggered quit takes priority over another lock-in
    if (g_ranked_harness.quit.triggered()) return 0;
    // Ranked: the match scene again after a game of the set; the next game needs a lock-in.
    g_ranked_harness.game_end_ms = 0;
    g_ranked_harness.reentry_asked = false;
    if (slippi::online::native_ranked_next_game(character, g_online_test_color))
      host::log("online test: ranked, locked in for the set's next game");
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
uint32_t h_vi_retrace_count() {
  if (yougame::console::console_active()) return yougame::console::console_engine().retrace_count();
  return host::retrace_count();
}
uint32_t h_vi_next_field() {
  if (yougame::console::console_active()) return yougame::console::console_engine().retrace_count() & 1;
  return host::retrace_count() & 1;
}
void h_vi_set_black(int32_t) {}
void h_vi_wait_retrace() {
  auto& console = yougame::console::console_engine();
  if (console.rollback_enabled()) {
    console.logical_retrace();
    // An explicit game VI wait inside a body has the same logical effect during replay.
    if (console.body_active() && !console.servicing()) return;
    guarded([] { host::retrace(); });
    return;
  }
  if (g_resim) { ++g_resim_vi_waits; rollback_frame_callback(); return; }
  guarded([] {
    host::retrace();
    uint32_t major = 0, minor = 0, frame = 0;
    host::current_scene(&major, &minor, &frame);
    // Stay inside this VI wait: no scene bodies or new menu choices can run during the notice.
    // Retraces keep exit handling, window messages and the renderer alive, with the last picture.
    while (yougame::update_safe_point(yougame::update::idle_scene(major, minor),
                                     slippi::online::session_mode() >= 0,
                                     !g_update_ranked_busy || g_update_ranked_busy() != 0,
                                     slippi::online::online_status_visible()))
      host::retrace();
  });
}

// The idle wait before a retrace, one audio period at a time (host API 16). Plays each block at
// the real time of its AI deadline instead of three together after the retrace. Alarms and the
// retrace itself stay where they were (native_retrace), so game code runs in the same order.
// MELEE_AUDIO_PACING=0 turns it off for comparisons.
int32_t h_vi_idle_step() {
  if (yougame::console::console_active()) return 0; // wall-time audio pacing cannot schedule game work
  if (host::tick_redo_active() || host::tick_redo_wanted()) return 0;   // the gate comes first
  take_console_catch_up();
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

// --online-test ranked, once a frame (the game reads its pads every frame, in every scene). A game
// that ended and is not back in Debug VS's match scene within 5 s (a results screen waiting for a
// press) is asked back into it for the set's next game; once the set's room has closed the run ends.
void ranked_harness_tick() {
  if (g_online_test_mode != 0 || !g_online_test_started) return;
  RankedHarness& h = g_ranked_harness;
  h.quit.poll(yougame::ranked_games_received(), [&] {
    host::log("yougame: test quit after game %d", h.quit.game());
    slippi::online::native_cleanup_match();   // same path as the player leaving the online menus
  });
  if (h.quit.triggered()) {
    // close_room flushes the concession and leaves on its worker; never exit ahead of it.
    if (!h.exiting && yougame::ranked_room_retired()) {
      h.exiting = true;
      host::request_exit(0);
    }
    return;
  }
  const uint64_t now = GetTickCount64();
  if (yougame::ranked_room_retired()) {
    if (!h.done_ms) {
      h.done_ms = now;
      host::log("online test: the ranked set is over and its room has closed; ending the run");
    } else if (!h.exiting && now - h.done_ms > 2000) {
      h.exiting = true;
      host::request_exit(0);
    }
    return;
  }
  if (!h.game_end_ms || h.reentry_asked || now - h.game_end_ms < 5000 || !g_game.practice) return;
  h.reentry_asked = true;
  int32_t scene[2] = {0, 0};
  if (g_game.practice(MU_PRACTICE_SCENE, scene, 2) != 0 || (scene[0] == kGameModeDebugVs && scene[1] == 1)) return;
  int32_t major[1] = {kGameModeDebugVs};
  g_game.practice(MU_PRACTICE_REQUEST_MAJOR, major, 1);
  host::log("online test: ranked, back to the match scene for the next game (from major %d state %d)", scene[0], scene[1]);
}

void h_pad_read(MuPadStatus out[4]) {
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (g_css_origin.proof) {
    for (int i = 0; i < 4; ++i) { out[i] = {}; out[i].err = i ? -1 : 0; }
    if (!g_css_origin.state_proof && g_css_origin.proof_stage == 1) {
      const auto frame = g_css_origin.frame;
      if (frame >= 70 && frame < 78) out[0].button = 0x0200; // B
      if (frame >= 130 && frame < 138) out[0].stick_y = -127;
      if (frame >= 160 && frame < 168) out[0].button = 0x0100; // A
    }
    return;
  }
#endif
  if (yougame::console::css_join::locked.load(std::memory_order_acquire) &&
      yougame::console::css_join::on_css.load(std::memory_order_acquire)) {
    for (int i = 0; i < 4; ++i) { out[i] = {}; out[i].err = i ? -1 : 0; }
    return;
  }
  if (g_css_origin.catching_up) {
    for (int i = 0; i < 4; ++i) { out[i] = {}; out[i].err = i ? -1 : 0; }
    return;
  }
  if (g_console_boot) {
    yougame::console::Pads composed{};
    auto& engine = yougame::console::console_engine();
    engine.read(engine.active() ? yougame::console::Pads{} : console_hardware(), composed);
    std::copy(composed.begin(), composed.end(), out);
    return;
  }
  if (g_online_test_mode == 0) ranked_harness_tick();
#ifndef MELEE_NO_SLIPPI
  if (!g_resim && !g_replaying) slippi::arena::tick();   // an arena room, once a frame (nothing without one)
#endif
  host::PadState pads[4];
#ifdef YOUGAME_AGENT_TEST_HOOK
  using namespace yougame::agent_test;
  current_fight_sample = {};
  if (fight_enabled && yougame::active() && g_online_test_mode >= 0 && g_agent_fight_match &&
      !g_replaying && slippi::online::is_online_match() && g_game.state_snapshot && g_game.lcancel_view) {
    MuStatePod state{};
    MuLcancelView air{};
    g_game.state_snapshot(&state);
    g_game.lcancel_view(&air);
    // The harness reads controller port 0, even when matchmaking puts us in player slot 1.
    // A neutral netplay request can explicitly choose a different physical controller port.
    const auto request = host::netplay::match_request();
    int32_t stage = 31;
    if (g_game.practice) g_game.practice(MU_PRACTICE_GET_STAGE, &stage, 1);
    current_fight_sample = fight_sample(
        {fight_enabled, true, true, true, false, slippi::online::local_player_slot(),
         request.requested ? request.local_port : 0, fight_stage_edge(stage)}, state, air);
  }
#endif
  host::input_poll(pads);
#ifdef YOUGAME_AGENT_TEST_HOOK
  current_fight_sample = {};
#endif
  lcancel::apply(pads, !yougame::tournament_active()); // Ranked brackets keep display-only feedback
  if (!yougame::tournament_active())
    slippi::native_practice::apply_pads(pads);   // Training after an online session, the requeue prompt
  bool offline_gameplay = false;
  if (gx::RenderOptions::live_offline_delay() && !g_replaying &&
      slippi::online::session_mode() < 0 && g_game.lcancel_view) {
    MuLcancelView view{};
    g_game.lcancel_view(&view);
    for (const auto& fighter : view.port) offline_gameplay |= fighter.present != 0;
  }
  host::offline_delay::apply(pads, offline_gameplay);
  // The player's data-only Gecko codes, once per frame. Offline only: the writer in the game
  // library also refuses during an online match, whichever kind.
  if (slippi::online::session_mode() < 0 && !g_replaying && !yougame::tournament_active()) user_gecko::apply();
  else {
    user_gecko::suspend_function_patches();   // a code's rewritten functions go back online, in replays and in YouGame tournaments
    // Online: only the codes both players agreed to run (gecko_session.h), on both games alike.
    if (!g_replaying && !yougame::tournament_active() && slippi::online::is_online_match()) user_gecko::run_agreed(gecko_session::match_codes());
  }
  // A pasted always-display hitbox code turns on the Arcade look switches its lines stand for.
  gx::set_arcade_display(gx::ArcadeSource::Codes, user_gecko::arcade_bits_of_codes());
  // Hand-ported codes (user_gecko.h): a local player tag code and PAL Stock Icons run as the game's own
  // features while the player has them on. Picture only, so online too.
  {
    const user_gecko::HandPorts hand = user_gecko::hand_ports_of_codes();
    gx::set_code_ally_tag(hand.local_tag, hand.local_tag_text, hand.local_tag_arrow);
    gx::set_code_pal_stock(hand.pal_stock_icons);
    g_code_pal_stock.store(hand.pal_stock_icons, std::memory_order_relaxed);
  }
  // Hidden test hook: MELEE_TEST_INSTALL_LOOK=<0 Diet, 1 Animelee, 2 Animelee x DBZ> starts that one-click install once.
  {
    static const int test_install = [] { const char* v = host::test_env::getenv("MELEE_TEST_INSTALL_LOOK"); return v && *v ? std::atoi(v) : -1; }();
    static int frames = 0;
    if (test_install >= 0 && !yougame::active() && ++frames == 120) {   // never a download in the YouGame app
      wchar_t exe[MAX_PATH] = {};
      std::string tool;
      if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) tool = (std::filesystem::path(exe).parent_path() / L"tools" / L"xdelta3.exe").u8string();
      host::cosmetics::install_official_look(test_install, host::options.iso, tool, source_port::mods::mods_folder().u8string());
    }
  }
  if (host::audio_tracing()) {
    // Latency trace: the first read that shows a new press on port 1 (buttons or the stick leaving
    // its center), the moment the game sees it.
    static bool was_active = false;
    const bool active = pads[0].err == 0 && ((pads[0].button & 0x0F7Fu) || std::abs((int)pads[0].stick_y) >= 40 ||
                                             std::abs((int)pads[0].stick_x) >= 40);
    if (active && !was_active) host::audio_trace_event("input");
    was_active = active;
  }
  g_held_buttons.store((uint32_t)(pads[0].button | pads[1].button | pads[2].button | pads[3].button), std::memory_order_relaxed);
  for (int i = 0; i < 4; ++i) {
    out[i].button = pads[i].button;
    out[i].stick_x = pads[i].stick_x; out[i].stick_y = pads[i].stick_y;
    out[i].sub_x = pads[i].sub_x; out[i].sub_y = pads[i].sub_y;
    out[i].trigger_l = pads[i].trig_l; out[i].trigger_r = pads[i].trig_r;
    out[i].analog_a = pads[i].analog_a; out[i].analog_b = pads[i].analog_b;
    out[i].err = pads[i].err;
  }
  // MELEE_TRACE_CPU_PADS=1: what the game says its CPU fighters are fed, once a second, for a hidden test.
  static const bool trace_cpu_pads = [] { const char* v = std::getenv("MELEE_TRACE_CPU_PADS"); return v && *v == '1'; }();
  if (trace_cpu_pads && g_cpu_pads) {
    static unsigned calls = 0; static int moved[4] = {};
    int cpu[4][8];
    if (g_cpu_pads(cpu) > 0) {
      for (int i = 0; i < 4; ++i) if (cpu[i][0] && (cpu[i][1] || cpu[i][2] || cpu[i][6])) ++moved[i];
      if (++calls % 60 == 0)
        for (int i = 0; i < 4; ++i) if (cpu[i][0]) host::log("cpu pad: slot %d stick %d,%d c %d,%d trigger %d buttons %04X (frames with input so far %d)", i + 1, cpu[i][1], cpu[i][2], cpu[i][3], cpu[i][4], cpu[i][5], (unsigned)cpu[i][6], moved[i]);
    }
  }
  // 20XX TE input display (offline matches and replays): the controllers the game read.
  training_overlay::set_input_display(te_host_feature(MU_GAME_OPTION2_TE_INPUT_DISPLAY));
  // Training Lab overlay: what the game read this frame, with the fighters' state.
  if (((gx::RenderOptions::live_te_options() & (MU_GAME_OPTION_LAB | MU_GAME_OPTION_LAB_OVERLAY)) ==
           (MU_GAME_OPTION_LAB | MU_GAME_OPTION_LAB_OVERLAY) || training_overlay::input_display()) &&
      g_game.state_snapshot) {
    MuStatePod state{};
    g_game.state_snapshot(&state);
    // A CPU has no controller: its slot shows what its fighter is being fed (last frame's, as the fighters'
    // state beside it is).
    MuPadStatus shown[4];
    std::memcpy(shown, out, sizeof shown);
    int cpu[4][8];
    if (g_cpu_pads && g_cpu_pads(cpu) > 0)
      for (int i = 0; i < 4; ++i) {
        if (!cpu[i][0]) continue;
        shown[i] = {};
        shown[i].stick_x = (int8_t)cpu[i][1]; shown[i].stick_y = (int8_t)cpu[i][2];
        shown[i].sub_x = (int8_t)cpu[i][3]; shown[i].sub_y = (int8_t)cpu[i][4];
        shown[i].trigger_l = shown[i].trigger_r = 0;
        shown[i].trigger_r = (uint8_t)cpu[i][5];
        shown[i].button = (uint16_t)cpu[i][6];
      }
    training_overlay::publish(state, shown);
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
  if (g_console_boot) {
    if (port + 1 == yougame::console::console_engine().config().my_port) host::input_rumble_local(on != 0);
    return;
  }
  if (g_replaying) on = 0;
  // Online the game already keeps only the local player's motor (mu_online_motor_port), and what
  // it passes is the controller port that player's input comes from, not their slot in the match.
  // Comparing that with the slot dropped every rumble whenever the two differ (a player who is
  // not player 1 online: no rumble at all on the Source Port, while the Static Recomp rumbled).
  if (slippi::online::is_online_match()) {
    host::input_rumble_local(on != 0);
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
#if MU_SQUAD && !defined(MELEE_NO_SLIPPI)
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
// The session's standard copy of a disc path: the disc's own file, or in the mod view the mod
// profile's file for it (both peers of a mod-view match have the same one). Its own entry number,
// made once per path, so nothing the game cached under a skin's number is reused. The disc's own
// file is always readable: its offset is below every layer's range, so h_disc_read takes it straight
// from the disc. Only a path the disc lacks has no standard copy: {-1, 0}. The paths asked are the
// disc names the game opens (squad_costume::opened_names): Captain Falcon's red costume, "PlCaRe."
// in the game's table, is asked as PlCaRe.usd and PlCaRe.dat, both on the vanilla disc.
std::unordered_map<std::string, int32_t> g_squad_standard;
slippi::squad_costume::Standard squad_standard_copy(const std::string& path) {
  const auto disc = g_disc_paths.find(path);
  if (disc == g_disc_paths.end() || (size_t)disc->second * 12 + 12 > g_fst_raw.size()) return {};
  const uint8_t* raw = g_fst_raw.data() + (size_t)disc->second * 12;
  if (raw[0]) return {};
  const FstFile own{be32(raw + 4), be32(raw + 8), false};
  FstFile copy = own;
  const FstFile& now = g_fst[(size_t)disc->second];
  if (!g_retail_view && now.offset < kCosmeticBase) copy = now;   // a mod profile's file, or the disc's own
  const bool mod = copy.offset != own.offset || copy.length != own.length;
  if (copy.offset == now.offset && copy.length == now.length) return {{disc->second, copy.length}, mod};
  auto& entry = g_squad_standard[path];   // only the disc's own file needs an entry of its own
  if (!entry) {
    entry = (int32_t)g_fst.size();
    g_fst.push_back(copy);
  }
  return {{entry, g_fst[(size_t)entry].length}, mod};
}
#endif
int32_t resolve_disc_entrynum(const std::string& p);
int32_t h_disc_entrynum(const char* path) {
  std::string p = path ? path : "";
  std::transform(p.begin(), p.end(), p.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
  if (p.empty() || p[0] != '/') p = "/" + p;
  const int32_t opened = resolve_disc_entrynum(p);
#if MU_SQUAD && !defined(MELEE_NO_SLIPPI)
  // Online Squad Strike, and Friends' synced select while its squads are in play, trim every costume
  // and Kirby hat at its fighter's fixed size, so a local file over it (or one that cannot be
  // opened) loads the standard copy this match was checked with, and an English twin the disc
  // lacks is not there, so the game opens the .dat.
  slippi::online::SquadCostume costume;
  if (slippi::online::squad_costume_rule(p, &costume, yougame::console::console_active())) {
    namespace rule = slippi::squad_costume;
    const rule::File file{opened, opened > 0 && opened < (int32_t)g_fst.size() ? g_fst[(size_t)opened].length : 0};
    const rule::Pick picked = rule::pick(costume.budget, file, costume.standard.file);
    if (picked.choice == rule::Choice::Over) {
      // Not reached: op 5 armed this match only after every standard copy held. Should it be,
      // the match is cancelled and the lookup stays as it was without the rule.
      slippi::online::squad_costume_refuse(p, costume.standard.mod);
      return opened;
    }
    if (picked.choice != rule::Choice::Opened && picked.file.entry != opened) {
      if (slippi::online::squad_costume_first_fallback(p))
        host::log("squad: skin %s over its budget, vanilla used this match", p.c_str());
      return picked.file.entry;
    }
  }
#endif
  return opened;
}
int32_t resolve_disc_entrynum(const std::string& p) {
  auto it = g_paths.find(p);
  if (it != g_paths.end() && g_retail_view) {
    // The retail copy, or -1 for a file the retail game does not have. A pack costume that only
    // changes looks keeps its own entry, so it is the same file (and the same cached copy) in both views.
    const auto grafted = g_pack_online_entry.find(it->second);
    if (grafted != g_pack_online_entry.end()) return grafted->second;
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
// Music packs mirror the disc's /audio tree. A file with the original name replaces that track;
// a sibling folder with the original stem is a random HPS playlist for that track.
void index_music_paths() {
  g_music_paths.clear();
  for (const auto& path : g_paths) {
    const int32_t index = path.second;
    if (index <= 0 || index >= (int32_t)g_fst.size()) continue;
    const FstFile& file = g_fst[(size_t)index];
    if (file.dir || path.first.rfind("/audio/", 0) != 0 ||
        _stricmp(std::filesystem::u8path(path.first).extension().string().c_str(), ".hps") != 0)
      continue;
    const std::string relative = path.first.substr(1);
    g_music_paths.try_emplace(file.offset, relative);
    const auto alias = g_view_alias.find(index);
    if (alias != g_view_alias.end() && alias->second > 0 && alias->second < (int32_t)g_fst.size())
      g_music_paths.try_emplace(g_fst[(size_t)alias->second].offset, relative);
  }
#ifdef __APPLE__
  // macOS: the program sits inside a signed, read-only .app bundle (the YouGame sandbox denies writes there and a
  // write would break the code seal), so music packs live in the player's user folder (--user-dir) instead.
  const auto& user = slippi::online::config().user_dir;
  if (user.empty()) return;
  const std::filesystem::path root = std::filesystem::u8path(user) / "MusicPacks";
#else
  wchar_t module[32768];
  const DWORD n = GetModuleFileNameW(nullptr, module, (DWORD)_countof(module));
  if (!n || n >= _countof(module)) return;
  const std::filesystem::path root = std::filesystem::path(std::wstring(module, n)).parent_path() / L"MusicPacks";
#endif
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (ec) return;
  std::vector<std::string> paths;
  paths.reserve(g_music_paths.size());
  for (const auto& item : g_music_paths) paths.push_back(item.second);
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  std::ofstream list(root / L"Tracks.txt", std::ios::trunc);
  if (!list) return;
  list << "Menu and stage music files on this game disc:\n";
  for (const auto& path : paths) list << "/" << path << "\n";
}

bool jukebox_music_pack_read(uint32_t offset, std::filesystem::path* out) {
  if (!out) return false;
  const auto source = g_music_paths.find(offset);
  if (source == g_music_paths.end()) return false;
  return slippi::jukebox::resolve_music_pack_path(source->second, out);
}

// The jukebox reads songs from where the game would: Slippi system files, the mod overlay, the disc.
bool jukebox_disc_read(uint32_t offset, void* dst, uint32_t size) {
  bool ok = false;
  if (!read_cosmetic(offset, dst, size, &ok) && !read_pack_online(offset, dst, size, &ok) &&
      !read_system_file(offset, dst, size, &ok) &&
      !read_overlay(offset, dst, size, &ok))
    ok = host::disc_read(offset, dst, size);
  return ok;
}
// Disc offsets where a stage file starts, and whether a mod replaced that file. A read starting there is
// the game loading that stage (host::g_stage_file_modded).
std::unordered_map<uint32_t, bool> g_stage_file_starts;
std::unordered_set<uint32_t> g_water_stage_starts;   // Fountain of Dreams (GrIz.dat): its floor is water
bool g_stage_file_starts_built = false;
void note_stage_file_read(uint32_t offset) {
  if (!g_stage_file_starts_built) {
    g_stage_file_starts_built = true;
    std::unordered_set<std::string> modded;
    for (const auto& file : g_mod_overlay.files()) modded.insert(file.path);
    for (const auto& [path, entry] : g_paths) {
      if (entry <= 0 || (size_t)entry >= g_fst.size() || g_fst[(size_t)entry].dir) continue;
      const bool stage = path.rfind("/gr", 0) == 0 && path.find('/', 1) == std::string::npos && path.size() > 7 &&
                         (path.compare(path.size() - 4, 4, ".dat") == 0 || path.compare(path.size() - 4, 4, ".usd") == 0);
      if (stage) g_stage_file_starts[g_fst[(size_t)entry].offset] = modded.count(path) != 0;
      if (stage && (path == "/griz.dat" || path == "/griz.usd")) g_water_stage_starts.insert(g_fst[(size_t)entry].offset);
    }
  }
  const auto it = g_stage_file_starts.find(offset);
  if (it != g_stage_file_starts.end()) {
    host::g_stage_file_modded.store(it->second, std::memory_order_relaxed);
    host::g_stage_water.store(g_water_stage_starts.count(offset) != 0, std::memory_order_relaxed);
  }
}
void h_disc_read(uint32_t offset, void* dst, uint32_t size, MuDiscDone done, void* user) {
  struct DiscScope { DiscScope() { ++g_handoff_disc_depth; } ~DiscScope() { --g_handoff_disc_depth; } } disc_scope;
  bool ok = false;
  note_stage_file_read(offset);
  const auto began = std::chrono::steady_clock::now();
  if (!read_cosmetic(offset, dst, size, &ok) && !read_pack_online(offset, dst, size, &ok) &&
      !read_system_file(offset, dst, size, &ok) &&
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
std::shared_ptr<const std::vector<uint8_t>> g_console_host_snapshot;

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
void card_snapshot(const SourceCardFile* file) {
  std::shared_ptr<const std::vector<uint8_t>> snapshot = std::make_shared<const std::vector<uint8_t>>();
  if (file && card_same_game(*file) && file->name() == "SuperSmashBros0110290334") {
    auto bytes = std::make_shared<std::vector<uint8_t>>(file->dir,file->dir+sizeof file->dir);
    bytes->insert(bytes->end(),file->data.begin(),file->data.end());
    if (yougame::console::melee_gci(*bytes)) snapshot = bytes;
  }
  std::atomic_store(&g_console_host_snapshot,std::move(snapshot));
}
void card_refresh_snapshot() {
  const auto melee = card_find("SuperSmashBros0110290334");
  card_snapshot(melee < 0 ? nullptr : g_card_files[melee]);
}
void card_save(SourceCardFile& file) {
  // The session card is a disposable copy. Guests never persist progress; the host's real card
  // is never touched either. Keep identical in-memory CARD results on all seats for lockstep.
  if (g_console_boot) return;
  if (g_origin_card_readonly) return;
  if (file.name() == "SuperSmashBros0110290334") card_snapshot(&file);
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
  if (g_origin_card_readonly) { origin_mount_card(g_css_origin.card); return; }
  g_card_dir = g_console_boot ? g_console_card :
      g_profile_card.empty() ? std::filesystem::u8path(host::options.card_dir) : g_profile_card;
  std::error_code ec;
  std::filesystem::create_directories(g_card_dir, ec);
  if (!g_console_boot) host::backup_card_folder(g_card_dir);
  card_clear_files();
  g_card_files.assign(CARD_MAX_FILES, nullptr);
  size_t slot = 0;
  for (const auto& entry : std::filesystem::directory_iterator(g_card_dir, ec)) {
    if (ec || entry.path().extension() != L".gci" || slot >= CARD_MAX_FILES) continue;
    if (!yougame::mode_policy::mod_sources(yougame::active()).folders && mods::check_te_file(entry.path()).te) {
      std::string identity, error;
      const bool explicit_import = mods::gci_identity(entry.path(), &identity, &error) &&
          std::any_of(g_card_imports.begin(), g_card_imports.end(),
                      [&](const mods::CardImport& item) { return item.identity == identity; });
      if (!yougame::mode_policy::card_save_allowed(true, true, explicit_import)) continue;
    }
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
    if (g_console_boot) break; // no save imports into a session card
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
  card_refresh_snapshot();
  host::log("card: source slot A mounted from %s (%zu files, %u of %u blocks used)",
            card_shown(g_card_dir).c_str(), slot, card_used_blocks(), CARD_TOTAL_BLOCKS);
}
void card_reset_mount() {
  // Keep the last mounted save's snapshot: Melee unmounts its card after every access, and Friends
  // reads the host's save at START, long after. The next mount, a save, delete, rename or format refreshes it.
  card_clear_files(); g_card_mounted = false;
}
// Seconds since 2000-01-01 on the console clock (its date at start plus game time), as the game's
// own calendar reads it: a save file used to be stamped January 1, 2000.
uint32_t card_time_2000() {
  if (yougame::console::console_active()) return (uint32_t)((h_boot_time() + h_ticks()) / host::TB_HZ);
  return host::cpu ? (uint32_t)((host::console_epoch_ticks() + host::cpu->tb) / host::TB_HZ) : 0;
}
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

// The Lossless adapter's lag test (host/lag_test_codes.h): its app boots Dolphin with no memory card,
// and the "Skip Memcard Prompt" code it relies on skips only that case, so slot A is empty too.
static bool card_slot_missing(int32_t chan) {
  if (g_console_boot || g_origin_card_readonly) return chan != 0;
  static const bool lag_test = std::getenv("MELEE_LAG_TEST") != nullptr && !std::getenv("MELEE_LAG_TEST_KEEP_CARD");
  return chan != 0 || lag_test;
}
void origin_mount_card(const std::vector<uint8_t>& bytes) {
  // Validated by the request before any restore; empty means the canonical default card.
  card_reset_mount();
  g_card_files.assign(CARD_MAX_FILES, nullptr);
  if (!bytes.empty()) {
    auto file = std::make_unique<SourceCardFile>();
    std::memcpy(file->dir, bytes.data(), sizeof file->dir);
    file->data.assign(bytes.begin()+64, bytes.end());
    g_card_files[0] = file.release();
  }
  g_card_mounted = true;
  g_origin_card_readonly = true;
}

void origin_end_card_session() {
  card_reset_mount();
  g_origin_card_readonly = false;
}

void css_origin_disarm() {
  // A failed capture/bookkeeping operation must also release logically freed boot blocks.
  for (const auto& a : g_css_origin.boot_allocations)
    if (!g_css_origin.live_allocations.count(a.first)
#ifdef MELEE_DOUBLES
        && !g_doubles_origin.boot_allocations.count(a.first)
#endif
       ) std::free(a.first);
  g_css_origin = {};
  origin_end_card_session();
}

int32_t h_console_origin(int32_t operation) {
  auto& run = g_css_origin;
#ifdef MELEE_DOUBLES
  if (operation==64) return doubles_checkpoint();
#endif
  if (operation == 16) return yougame::console::css_join::locked.load(std::memory_order_acquire);
  if (operation == 32) return run.join_boot;
  if (operation == 0) {
    if (run.join_boot) {
      try { origin_mount_card(run.card); }
      catch (...) { host::die("css-origin: cannot mount Friends boot card"); }
      return 1;
    }
#ifdef YOUGAME_AGENT_TEST_HOOK
    if (run.proof) {
      try { origin_mount_card({}); }
      catch (...) { host::die("css-origin: cannot mount proof card"); }
      return 1;
    }
#endif
    return 0;
  }
  if (operation == 8) {
#ifdef YOUGAME_AGENT_TEST_HOOK
    if (run.proof && (run.proof_stage == 0 || run.state_proof)) return 1;
#endif
    return run.catching_up;
  }
  if (operation != 1 && operation != 2 && operation != 4) return 0;
  const bool consume = (run.resume & operation) != 0;
  run.resume &= ~operation;
  return consume;
}

bool queue_css_origin_reset(const std::vector<uint8_t>& card_bytes);
int css_join_tick() {
  using namespace yougame::console;
  namespace cj = yougame::console::css_join;
  static std::shared_ptr<cj::Channel> channel;
  static unsigned handled = 0;
  static bool origin_wait = false, applied = false, provisional_engine = false;
  int32_t scene[2]{};
  const bool queried = g_game.practice && g_game.practice(MU_PRACTICE_SCENE,scene,2) == 0;
  const bool css = queried && scene[0] == 2 && scene[1] == 0;
  cj::on_css.store(css,std::memory_order_release);
  MuCssState probe{};
  const int capture = css && g_game.css_state ? g_game.css_state(MU_CSS_CAPTURE,&probe,sizeof probe) : MU_CSS_UNSTABLE;
  const bool stable = capture == 0 || capture == MU_CSS_NOT_TRANSFERABLE;
  cj::scene.store(css ? (stable ? 1 : 0) : queried && scene[0] == 2 ? 2 : 0,std::memory_order_release);
  cj::available.store(g_css_origin.armed && g_css_origin.boot.ready() && g_game.css_state &&
      !g_console_boot && !g_replaying && g_online_test_mode < 0 && !source_p2p::requested(),std::memory_order_release);
  const auto next = std::atomic_load(&cj::pending);
  if (next != channel) { channel = next; handled = 0; origin_wait = applied = false; }
  if (!channel) return 0;
  std::lock_guard<std::recursive_mutex> ownership(channel->main_mutex);
  auto unlock = [] {
    if (cj::locked.exchange(false,std::memory_order_acq_rel)) host::log("css-join lock: off");
  };
  channel->working.store(true,std::memory_order_release);
  if (channel->cancelled.load(std::memory_order_acquire) || host::exit_requested()) {
    if (provisional_engine) { console_engine().end("css-join abort"); provisional_engine = false; g_console_boot = false; }
    unlock(); channel->working.store(false,std::memory_order_release);
    std::atomic_store(&cj::pending,std::shared_ptr<cj::Channel>{}); return 0;
  }
  auto q = std::atomic_load(&channel->request);
  if (!q) return 0;
  // Both canonical preludes must see the same CSS input/scene gate, including the guest.
  if (!cj::locked.exchange(true,std::memory_order_acq_rel)) host::log("css-join lock: on");
  auto publish = [&](bool ok, MuCssState state = {}, std::string hash = {}) {
    auto result = std::make_shared<cj::Result>(); result->serial = q->serial;
    result->ok = ok; result->frame = static_cast<uint32_t>(g_css_origin.frame);
    result->state = state; result->hash = std::move(hash); handled = q->serial;
    std::atomic_store(&channel->result,std::shared_ptr<const cj::Result>(result));
  };
  auto deliver_room = [&] {
    auto launch = std::atomic_load(&cj::launch);
    for (const auto& m : channel->take_room()) {
      if (provisional_engine) console_engine().css_join_message(m);
      else if (launch && launch->link) launch->link->css_join_message(m);
    }
  };
  deliver_room();
  if (q->serial != handled) {
    if (q->operation == cj::Operation::Capture) {
      MuCssState state{};
      if (q->host && !css) return 0; // Fight/results keep running, including on a late scene race.
      if (!g_game.css_state) { publish(false); return 0; }
      const int rc = g_game.css_state(q->host ? MU_CSS_CAPTURE_CHECKED : MU_CSS_IDENTITY,&state,sizeof state);
      if (rc == MU_CSS_NOT_TRANSFERABLE) {
        MuCssError error{};
        if (!mu_css_validate(&state,&error)) host::log("%s",cj::invalid_line(error).c_str());
        publish(false); return 0;
      }
      if (rc != 0) return 0; // name/rules modal or CSS entry still loading
      int32_t pick[3]{};
      const int physical = css && g_game.practice(MU_PRACTICE_CSS_PICK,pick,3) == 0 ? pick[2] : 0;
      if (q->host && physical != 0) { publish(false); return 0; } // preserve the host's original door ownership
      if (!q->host && physical >= 0 && physical < 4 && q->port >= 0 && q->port < 4)
        state.ports[q->port] = state.ports[physical];
      publish(true,state);
    } else if (q->operation == cj::Operation::Origin) {
      if (!origin_wait) {
        // Same-card admission remains mandatory. A changed guest card takes relaunch, which
        // already boots the agreed save. Never import another process's snapshot or stack.
        if (!queue_css_origin_reset(q->card)) {
          host::log("css-join origin: refused (%s); relaunch",
                    q->card != g_css_origin.card ? "guest save differs" : "reset unavailable");
          publish(false); return 0;
        }
        origin_wait = true; return 0;
      }
      if (!g_css_origin.pending && !g_css_origin.catching_up && css) publish(true);
    } else if (q->operation == cj::Operation::Prepare) {
      const auto launch = std::atomic_load(&cj::launch);
      publish(applied && launch && launch->link && !launch->link->lost() &&
          console_engine().can_begin(q->config));
    } else if (q->operation == cj::Operation::Release) {
      // Activate under the Link mutex before publishing; setup never touches it again.
      const bool released = provisional_engine && console_engine().css_join_release();
      publish(released);
      if (released) {
        provisional_engine = false;
        unlock(); channel->working.store(false,std::memory_order_release);
        std::atomic_store(&cj::pending,std::shared_ptr<cj::Channel>{}); return 0;
      }
    } else if (q->operation == cj::Operation::Apply) {
      if (!css || g_css_origin.frame > q->frame) { publish(false); return 0; }
      if (g_css_origin.frame < q->frame) return 0;
      MuCssState state = q->state;
      if (!g_game.css_state || g_game.css_state(MU_CSS_APPLY,&state,sizeof state) != 0) { publish(false); return 0; }
      applied = true;
      publish(true,state,handoff_join::frame_hash(native_savestate::hash_spans(g_css_origin.spans)));
    }
  }
  // Pause at stable origin and at K while the preparation thread compares both full hashes.
  // This loop pumps only host events; it advances no VI, pad, scene, or game timer.
  if ((origin_wait && handled == q->serial && q->operation == cj::Operation::Origin) || applied) {
    const auto until = GetTickCount64()+35000;
    while (!channel->cancelled.load(std::memory_order_acquire) && !host::exit_requested() && GetTickCount64()<until) {
      if (q->operation == cj::Operation::Plug && q->serial != handled) {
        auto launch = std::atomic_exchange(&cj::launch,std::shared_ptr<cj::Launch>{});
        if (!applied || !launch || !launch->link || !console_engine().can_begin(launch->config)) {
          publish(false); continue;
        }
        EngineHooks hooks;
        hooks.log = [](const std::string& line) { host::log("%s",line.c_str()); };
        hooks.notice = [](const char* text) { screen_label::show(text,8.0); };
        hooks.host_ticks = [] { return host::cpu ? host::cpu->tb : uint64_t(0); };
        hooks.host_catch_up = console_host_catch_up;
#if MU_SQUAD
        hooks.session_edge = slippi::online::squad_console_reset;
#endif
        hooks.responsive = [] {
          host::window_pump();
          if (host::exit_requested()) yougame::console::console_engine().end("window closed");
        };
        try {
          if (!console_engine().begin(std::move(launch->link),launch->config,std::move(hooks),kConsoleRollbackDefault)) {
            publish(false); continue;
          }
          provisional_engine = true;
          if (!console_engine().adopt_css_clock(host::cpu->tb,host::retrace_count())) {
            publish(false); continue;
          }
          g_console_host_pad = 0;
          g_console_boot = true; // The origin reset already mounted the agreed read-only card.
          publish(true);
        } catch (...) {
          provisional_engine = console_engine().active();
          publish(false); // Preparation still owns abort delivery and the relaunch fallback.
        }
      }
      deliver_room();
      const auto newest = std::atomic_load(&channel->request);
      if (newest && newest->serial != handled) return css_join_tick();
      host::window_pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    channel->cancelled.store(true,std::memory_order_release);
    if (provisional_engine) {
      console_engine().end("css-join abort"); provisional_engine = false; g_console_boot = false;
    }
    unlock();
    // Retire this channel before the link thread can see working == false: its relaunch fallback
    // takes the lock again for the relaunch wait, and a later tick must not find this cancelled
    // channel still pending and clear that lock through the cancel path above.
    auto self = channel;
    std::atomic_compare_exchange_strong(&cj::pending,&self,std::shared_ptr<cj::Channel>{});
    channel->working.store(false,std::memory_order_release);
  }
  return 0;
}

bool queue_css_origin_reset(const std::vector<uint8_t>& card_bytes) {
  auto& run = g_css_origin;
  // This phase owns offline resets only. Session/link clocks are phase-2 work; never reset a
  // match or Slippi state from this API. Existing relaunch remains the production fallback.
  if (!run.armed || !run.boot.ready() || run.pending || g_console_boot || g_replaying ||
      g_online_test_mode >= 0 || source_p2p::requested() || slippi::online::session_mode() >= 0 ||
      (!card_bytes.empty() && !yougame::console::melee_gci(card_bytes)) ||
      card_bytes != run.card || !gx::supports_game_reset()) return false;
  run.pending_card = card_bytes;
  run.pending = true;
  return true;
}

int css_origin_tick() {
  auto& run = g_css_origin;
  if (!run.armed) return 0;
  if (g_replaying || g_online_test_mode >= 0 || source_p2p::requested() ||
      (!g_console_boot && slippi::online::session_mode() >= 0)) {
    run.pending = false; // origin work never enters a Slippi/replay/P2P launch or match
    return 0;
  }
  if (!run.boot.ready()) {
    // Same pre-input boundary at which Engine::advance publishes the console frame-0 hash.
    // Arming must precede game startup so native boot allocations are recorded too.
    try {
      MuStateRegion game_regions[3]{};
      const auto count = g_game.state_regions(game_regions, 3);
      run.regions.assign(game_regions, game_regions + count);
      std::vector<MuStateRegion> exclusions(g_game.state_exclusions(nullptr, 0));
      g_game.state_exclusions(exclusions.data(), static_cast<uint32_t>(exclusions.size()));
      append_sound_driver_exclusions(exclusions);
      run.spans = native_savestate::subtract(run.regions, exclusions);
      std::vector<MuStateRegion> included;
      for (const auto& span : run.spans) included.push_back({span.address,span.size});
      run.excluded_spans = native_savestate::subtract(run.regions, included);
      if (!g_origin_card_readonly) {
        auto card = std::atomic_load(&g_console_host_snapshot);
        if (card) run.card = *card;
      }
      run.boot.capture(run.spans);
      run.excluded.capture(run.excluded_spans);
      run.aram.capture({{host::aram, MU_HOST_ARAM_BYTES}});
      run.boot_allocations = run.live_allocations;
      std::vector<native_savestate::Span> allocations;
      for (const auto& a : run.boot_allocations) allocations.push_back({static_cast<uint8_t*>(a.first),a.second});
      run.allocations.capture(std::move(allocations));
      run.audio = g_audio;
      run.restore_gpu = gx::capture_boot_state();
      run.restore_clock = host::capture_native_boot_clock();
      host::log("css-origin snapshot_bytes=%zu capture_ms=%.3f excluded_bytes=%zu aram_bytes=%zu allocation_bytes=%zu",
          run.boot.bytes(), run.boot.capture_ms(), run.excluded.bytes(), run.aram.bytes(),run.allocations.bytes());
    } catch (...) {
      // Optional checkpoint failure is nonfatal; disarming prevents retries and logs once.
      host::log("css-origin: capture failed; reset disabled");
      css_origin_disarm();
      return 0;
    }
  }
  if (run.pending) {
    yougame::css_origin::Services services;
    services.disc_quiet = [] {
      // Same disc + queued/in-dispatch callback rule as handoff_state::validate_gate.
      return !g_handoff_disc_depth && g_handoff_callbacks_pending && !g_handoff_callbacks_pending();
    };
    services.restore_excluded = [&] {
      run.excluded.restore(); run.aram.restore();
      for (const auto& a : run.live_allocations)
        if (!run.boot_allocations.count(a.first)) std::free(a.first);
      run.live_allocations = run.boot_allocations;
      run.allocations.restore();
    };
    services.flush_gpu = run.restore_gpu;
    services.reset_audio = [&] {
      audio_core::reset(g_audio);
      g_audio = run.audio;
      host::audio_reset_stream();
      for (int port = 0; port < 4; ++port) host::input_rumble(port, false);
    };
    services.mount_card = [&] { run.card = run.pending_card; origin_mount_card(run.card); };
    services.reset_host = [&] {
      run.restore_clock(); g_console_catch_up.store(0, std::memory_order_release); g_resim = false; g_console_in_retrace = false; g_disc_loads.clear();
      g_savestates.end(); g_mem1_watch = {};
      run.frame = 0; run.requested_css = false; run.catching_up = true;
      run.join_boot = true; // Only the in-process reset requests the canonical VS origin.
      run.resume = 1 | 2 | 4;
#if MU_SQUAD
      slippi::online::squad_console_reset();   // the rebooted game arms Friends' costume rule again
#endif
    };
    if (yougame::css_origin::reset_to_boot(run.boot,services,run.reset_ms) != yougame::css_origin::Reset::restored)
    {
#ifdef YOUGAME_AGENT_TEST_HOOK
      if (run.proof && ++run.retry_bodies > 600) {
        host::log("css-origin: failed: disc/callback gate did not become quiet");
        host::request_exit(1);
      }
#endif
      return 0; // retain request; retry next engine body, before ANY restore
    }
    run.pending = false;
    run.catchup_at = std::chrono::steady_clock::now();
    return -50;
  }
  int32_t scene[2]{};
  const bool css = g_game.practice && g_game.practice(MU_PRACTICE_SCENE,scene,2) == 0 && scene[0] == 2 && scene[1] == 0;
  if (run.catching_up && !css && !run.requested_css) {
    int32_t major = 2;
    run.requested_css = g_game.practice(MU_PRACTICE_REQUEST_MAJOR,&major,1) == 0;
  }
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (run.state_proof && css && run.frame == 30 && run.proof_stage == 0) {
    if (!g_game.css_state || g_game.css_state(MU_CSS_CAPTURE,&run.proof_state,sizeof run.proof_state)) {
      host::log("css-state: failed to capture live CSS"); host::request_exit(1); return 0;
    }
    auto& s = run.proof_state;
    s.ports[0].fighter = 2; s.ports[0].costume = 1; s.ports[0].kind = 0;
    s.ports[1].fighter = 9; s.ports[1].costume = 2; s.ports[1].kind = 1; s.ports[1].cpu = 7;
    s.rules[2] = 1; s.rules[4] = 4; s.rules[8] = 8; s.rules[9] = 1;
    s.rules[10] = 1; s.rules[11] = 1; s.item_frequency = 0;
    mu_css_write_le(s.item_mask,0x1234,8); mu_css_write_le(s.stage_mask,0xe70000b0,4);
    const unsigned char tag[8] = {'J','O','I','N',0,0,0,0};
    std::copy(tag,tag+8,s.ports[0].tag); s.ports[0].tag_index = 0;
    s.ports[0].flags = (s.ports[0].flags & ~7) | 4; // tag selected; BOTH rumble settings off
    if (g_game.css_state(MU_CSS_APPLY,&s,sizeof s) || g_game.css_state(MU_CSS_CAPTURE,&s,sizeof s)) {
      host::log("css-state: failed to set proof state"); host::request_exit(1); return 0;
    }
    run.state_hash = native_savestate::hash_spans(run.spans);
    const auto bytes = yougame::console::css_join::bytes(s);
    host::log("css-state bytes=%u data=%s",unsigned(bytes.size()),yougame::console::card_encode(bytes.data(),bytes.size()).c_str());
    if (!queue_css_origin_reset(run.card)) {
      host::log("css-state: failed reset admission"); host::request_exit(1);
    } else run.proof_stage = 2;
  } else if (run.state_proof && css && run.frame == 30 && run.proof_stage == 2) {
    auto s = run.proof_state;
    if (g_game.css_state(MU_CSS_APPLY,&s,sizeof s)) {
      host::log("css-state: failed to apply roundtrip state"); host::request_exit(1); return 0;
    }
    const auto after = native_savestate::hash_spans(run.spans);
    host::log("css-state roundtrip hash_before=%016llx hash_after=%016llx",
        (unsigned long long)run.state_hash,(unsigned long long)after);
    run.proof_stage = 3; host::request_exit(after == run.state_hash ? 0 : 1);
  }
  if (!run.state_proof && run.proof && run.proof_stage == 1) {
    run.proof_menu_seen |= scene[0] == 1;
    run.proof_css_return_seen |= run.proof_menu_seen && css;
  }
  if (!run.state_proof && run.proof && run.proof_stage == 0 && css) {
    run.fresh_hash = native_savestate::hash_spans(run.spans);
    host::log("css-origin hash=%016llx frame=%llu path=fresh",(unsigned long long)run.fresh_hash,(unsigned long long)run.frame);
    run.proof_stage = 1;
  } else if (!run.state_proof && run.proof && run.proof_stage == 1) {
    // Inputs above plus normal major-mode unload/load exercise both menu and CSS lifetimes.
    if (run.frame == 60 || run.frame == 200) {
      int32_t major = run.frame == 60 ? 1 : 2;
      g_game.practice(MU_PRACTICE_REQUEST_MAJOR,&major,1);
    }
    if (run.frame >= 360) {
      if (!run.proof_menu_seen || !run.proof_css_return_seen || !queue_css_origin_reset(run.card)) {
        host::log("css-origin: failed: scripted mode excursion or reset admission");
        host::request_exit(1);
      } else run.proof_stage = 2;
    }
  } else if (!run.state_proof && run.proof && run.proof_stage == 2 && run.catching_up && css) {
    const auto hash = native_savestate::hash_spans(run.spans);
    host::log("css-origin hash=%016llx frame=%llu path=reset",(unsigned long long)hash,(unsigned long long)run.frame);
    const double catchup = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-run.catchup_at).count();
    host::log("css-origin reset_ms=%.3f catchup_ms=%.3f",run.reset_ms,catchup);
    if (hash != run.fresh_hash) {
      for (const auto& d : run.boot.differences(16)) {
        const auto& span = run.spans[d.span];
        const auto address = reinterpret_cast<uintptr_t>(span.address) + d.offset;
        for (size_t r = 0; r < run.regions.size(); ++r) {
          const auto base = reinterpret_cast<uintptr_t>(run.regions[r].address);
          if (address >= base && address-base < run.regions[r].size)
            host::log("css-origin diff region=%zu offset=0x%llx size=%u first=%02x/%02x",r,
                (unsigned long long)(address-base),d.size,unsigned(d.expected),unsigned(d.actual));
        }
      }
    }
    run.proof_stage = 3;
    host::request_exit(hash == run.fresh_hash ? 0 : 1);
  }
#endif
  if (run.catching_up && css) {
    run.catching_up = false;
#ifdef YOUGAME_AGENT_TEST_HOOK
    if (!run.proof)
#endif
      origin_end_card_session(); // next CARD mount loads the real writable card again
  }
  ++run.frame;
  return 0;
}

#ifdef MELEE_DOUBLES
#include "doubles_boot.inc"
#endif

int32_t h_card_probe(int32_t chan, int32_t* mem_size, int32_t* sector_size) {
  if (card_slot_missing(chan)) return CARD_NOCARD;
  if (mem_size) *mem_size = CARD_MEM_SIZE_MBIT;
  if (sector_size) *sector_size = CARD_SECTOR;
  return CARD_READY;
}
int32_t h_card_mount(int32_t chan) { if (card_slot_missing(chan)) return CARD_NOCARD; card_mount_files(); return CARD_READY; }
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
  if (g_origin_card_readonly) return CARD_NOPERM;
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
  if (g_origin_card_readonly) return CARD_NOPERM;
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  const int32_t no = card_find(name ? name : "");
  if (no < 0) return CARD_NOFILE;
  SourceCardFile* file = g_card_files[no];
  std::error_code ec; if (!g_console_boot) std::filesystem::remove(file->path, ec);
  delete file; g_card_files[no] = nullptr;
  card_refresh_snapshot();
  return CARD_READY;
}
int32_t h_card_rename(int32_t chan, const char* old_name, const char* new_name) {
  if (g_origin_card_readonly) return CARD_NOPERM;
  if (chan != 0 || !g_card_mounted) return CARD_NOCARD;
  const std::string old_file = old_name ? old_name : "", new_file = new_name ? new_name : "";
  if (new_file.empty() || new_file.size() > 32) return CARD_NAMETOOLONG;
  const int32_t no = card_find(old_file);
  if (no < 0) return CARD_NOFILE;
  if (card_find(new_file) >= 0) return CARD_EXIST;
  SourceCardFile& file = *g_card_files[no];
  std::error_code ec; if (!g_console_boot) std::filesystem::remove(file.path, ec); file.path.clear();
  std::memset(file.dir + 8, 0, 32); std::memcpy(file.dir + 8, new_file.data(), new_file.size());
  card_save(file);
  card_refresh_snapshot();
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
  if (g_origin_card_readonly) return CARD_NOPERM;
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
  if (g_origin_card_readonly) return CARD_NOPERM;
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
  if (g_origin_card_readonly) return CARD_NOPERM;
  if (chan != 0) return CARD_NOCARD;
  card_mount_files();
  for (SourceCardFile*& file : g_card_files) {
    if (!file) continue;
    std::error_code ec; if (!g_console_boot) std::filesystem::remove(file->path, ec);
    delete file; file = nullptr;
  }
  card_refresh_snapshot();
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
  static const char* shift = host::test_env::getenv("MELEE_TEST_AX_PHASE_SHIFT");
  if (!was_running && g_audio.running && shift)
    g_audio.next_tb += std::strtoull(shift, nullptr, 0);
  // MELEE_AUDIO_PHASE_US=N (measurement, 0 to 5000): start the 5 ms AX clock N microseconds later
  // against the retrace, so the first sound frame after the game's frame comes sooner after it.
  static const long phase_us = [] { const char* v = std::getenv("MELEE_AUDIO_PHASE_US"); const long n = v ? std::atol(v) : 0; return n > 0 && n < 5000 ? n : 0L; }();
  if (!was_running && g_audio.running && phase_us)
    g_audio.next_tb += (uint64_t)phase_us * host::TB_HZ / 1000000u;
  if (!was_running && g_audio.running)
    host::log("audio: AI clock phase retrace=%u next_tick_after_boundary=%lld", host::retrace_count(),
              (long long)(g_audio.next_tb - (host::next_retrace_tb() - host::TB_PER_FRAME)));
}
void h_ai_set_sample_rate(uint32_t) {}
void h_ai_set_stream_volume(int32_t, int32_t) {}
void h_dsp_mail(uint32_t mail) { audio_core::dsp_mail(mail); }
uint32_t h_dsp_mail_pending() { return audio_core::dsp_mail_pending(); }
void* h_aram_base() { return host::aram; }
uint32_t h_aram_size() { return MU_HOST_ARAM_BYTES; }
void h_aram_dma(int32_t to_aram, void* mainmem, uint32_t aram_offset, uint32_t length) {
  if ((uint64_t)aram_offset + length > MU_HOST_ARAM_BYTES) host::die("ARAM DMA out of range %08X+%X", aram_offset, length);
  if (to_aram) std::memcpy(host::aram + aram_offset, mainmem, length);
  else std::memcpy(mainmem, host::aram + aram_offset, length);
}
void* h_native_alloc(uint32_t size) {
  auto* ptr = std::malloc(size);
  if (ptr && g_css_origin.armed) {
    try { g_css_origin.live_allocations[ptr] = size; }
    catch (...) {
      host::log("css-origin: allocation tracking failed; reset disabled");
      css_origin_disarm();
    }
  }
  return ptr;
}
void h_native_free(void* ptr) {
#ifdef MELEE_DOUBLES
  if(g_doubles_origin.boot_allocations.count(ptr)) {
    g_css_origin.live_allocations.erase(ptr); return;
  }
#endif
  if (!g_css_origin.armed) { std::free(ptr); return; }
  g_css_origin.live_allocations.erase(ptr);
  if (!g_css_origin.boot_allocations.count(ptr)) std::free(ptr);
}

uint32_t h_mem1_size() { return MEM1_SIZE; }
uint32_t h_mod_flags() {
  if (yougame::console::console_active()) return 0;
  return (g_mod_overlay.files().empty() ? 0 : MU_MOD_ASSETS_PRESENT) | (hackpack::loaded() ? MU_MOD_HACKPACK : 0u) |
         (mods::status().rogue ? MU_MOD_ROGUE : 0u) | (mods::status().maker ? MU_MOD_MAKER : 0u);
}
host::ConsoleSoundMode& console_sound_mode() {
  static host::ConsoleSoundMode mode(std::filesystem::u8path(host::options.card_dir));
  return mode;
}
// The game copies this into its own memory at boot (HSD_Synth_804D7754 = OSGetSoundMode()) and pans
// voices with it, so a Mono player and a Stereo player would hold different game memory and fail every
// cross-peer state hash (Friends frame 0, console lockstep, full-state export, the CSS join). YouGame
// builds report Stereo, as before 0.9.95; the player's choice is still saved for when it becomes a
// per-player field outside the hashed state.
// Stereo in the YouGame app and in a game our launcher started with YouGame connected (its YouGame
// rows hash the same memory; the bridge is read in main() before the game boots). Otherwise the
// player's own choice, as in 0.9.211 (Mono players keep Mono).
int32_t h_sound_mode() { return (yougame::active() || yougame::launcher()) ? 1 : console_sound_mode().get(); }
void h_set_sound_mode(int32_t mode) {
  if (!console_sound_mode().set(mode)) host::log("audio: could not save the console sound mode");
}
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
const MuMatchOverride* h_match_override() {
  if (yougame::console::console_active()) return nullptr;
  return g_match_set ? &g_match : nullptr;
}

// --rng-seed: forced from host::options once at startup (see main.cpp's flag parsing), read by the
// game at the same first GS_VS frame callback, so a scripted run reaches identical RNG in both the source port and a
// --rng-seed recomp run from the first in-match frame.
void h_rng_seed_override(uint32_t* seed, int32_t* has_value) {
  if (yougame::console::console_active()) { *has_value = 0; return; }
  *has_value = host::options.rng_seed_set ? 1 : 0;
  if (host::options.rng_seed_set) *seed = host::options.rng_seed;
}
// Called from the same first GS_VS frame callback unconditionally (not just when --rng-seed was given), so
// an @match-relative script section starts counting from the match's rules/players being
// finalised on an ordinary offline run too, the same instant the recomp guest's --rng-seed hook
// marks it.
void h_mark_match_start() {
  host::input_mark_match_start();
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (g_boot_match.requested && !g_boot_match.started) {
    g_boot_match.started = true;
    host::log("agent-test: bootMatch match started (frame %u)", host::retrace_count());
  }
#endif
}

#if !defined(MELEE_NO_SLIPPI) && defined(_WIN32)
// An arena room's viewer window (hle/slippi_arena.h): this same program playing the live recording,
// with the options that set how the game looks and runs taken from this run's own command line.
void* launch_arena_viewer(const std::string& path) {
  wchar_t exe[MAX_PATH];
  if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return nullptr;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  auto quote = [](const std::wstring& a) {
    std::wstring q = L"\"";
    for (wchar_t c : a) { if (c == L'"') q += L'\\'; q += c; }
    return q + L"\"";
  };
  std::wstring cmd = quote(exe);
  bool hidden = false;
  // Kept: the disc, the system files, the settings, how it draws and sounds. Dropped: anything that
  // would make the viewer play online, run a script or join the room.
  static const wchar_t* const with_value[] = {L"--iso", L"--sys-dir", L"--settings-path", L"--shader-cache", L"--backend",
                                              L"--volume", L"--mod-base-iso", L"--look-iso", L"--user-dir"};
  static const wchar_t* const flags[] = {L"--load-settings", L"--hidden", L"--headless", L"--no-music"};
  for (int i = 1; argv && i < argc; ++i) {
    const std::wstring a = argv[i];
    bool kept = false;
    for (const wchar_t* f : flags) if (a == f) { cmd += L" " + a; kept = true; if (a != L"--load-settings" && a != L"--no-music") hidden = true; }
    for (const wchar_t* w : with_value)
      if (!kept && a == w && i + 1 < argc) { cmd += L" " + a + L" " + quote(argv[++i]); kept = true; }
  }
  if (argv) LocalFree(argv);
  const std::filesystem::path live = std::filesystem::u8path(path);
  cmd += L" --replay " + quote(live.wstring());
  // How it shows the match (its corner switches it; this game keeps the choice, so it says where).
  cmd += L" --spectate-view " + std::to_wstring(std::clamp(gx::RenderOptions::live_spectate_view(), 0, 2));
  wchar_t owner[32];
  swprintf(owner, 32, L"%llx", (unsigned long long)(uintptr_t)host::window_native());
  cmd += L" --spectate-owner " + std::wstring(owner);
  // Its own recording of what it showed goes beside the live one (tests compare the two).
  cmd += L" --replay-dir " + quote((live.parent_path() / L"viewer").wstring());
  if (hidden) cmd += L" --log-file " + quote((live.parent_path() / (live.stem().wstring() + L".log")).wstring());
  // Test: MELEE_TEST_VIEWER_SHOTS=1 gives the viewer its overlay (the view selector) and pictures.
  if (host::test_env::getenv("MELEE_TEST_VIEWER_SHOTS")) {
    std::error_code ec;
    std::filesystem::create_directories(live.parent_path() / L"viewer", ec);
    cmd += L" --pc-settings --window 1280x720 --capture-every 60 --capture " +
           quote((live.parent_path() / L"viewer" / (live.stem().wstring() + L"-shot.ppm")).wstring());
  }
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> line(cmd.begin(), cmd.end());
  line.push_back(0);
  if (!CreateProcessW(exe, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    host::log("arena: viewer did not start (error %lu)", GetLastError());
    return nullptr;
  }
  CloseHandle(pi.hThread);
  host::log("arena: viewer pid %lu", pi.dwProcessId);
  return pi.hProcess;
}
#elif !defined(MELEE_NO_SLIPPI) && defined(__linux__)
// The same viewer on Linux (it was a stub: "viewer did not open", Linux report 11-24): this program again,
// its path from /proc/self/exe and its arguments from /proc/self/cmdline, started through the compat
// CreateProcessA (posix_spawn), whose handle the room's viewer_running / close_viewer already read.
void* launch_arena_viewer(const std::string& path) {
  std::error_code ec;
  const std::string exe = std::filesystem::read_symlink("/proc/self/exe", ec).string();
  if (ec || exe.empty()) { host::log("arena: viewer did not start (no /proc/self/exe)"); return nullptr; }
  std::vector<std::string> argv;
  {
    std::ifstream f("/proc/self/cmdline", std::ios::binary);
    std::string a;
    while (std::getline(f, a, '\0')) argv.push_back(a);
  }
  auto quote = [](const std::string& a) {
    std::string q = "\"";
    for (char c : a) { if (c == '"') q += '\\'; q += c; }
    return q + "\"";
  };
  std::string cmd = quote(exe);
  bool hidden = false;
  // Kept and dropped as on Windows: how it looks and runs, never online, a script or the room.
  static const char* const with_value[] = {"--iso", "--sys-dir", "--settings-path", "--shader-cache", "--backend",
                                           "--volume", "--mod-base-iso", "--look-iso", "--user-dir"};
  static const char* const flags[] = {"--load-settings", "--hidden", "--headless", "--no-music"};
  for (size_t i = 1; i < argv.size(); ++i) {
    const std::string& a = argv[i];
    bool kept = false;
    for (const char* f : flags) if (a == f) { cmd += " " + a; kept = true; if (a != "--load-settings" && a != "--no-music") hidden = true; }
    for (const char* w : with_value)
      if (!kept && a == w && i + 1 < argv.size()) { cmd += " " + a + " " + quote(argv[++i]); kept = true; }
  }
  const std::filesystem::path live = std::filesystem::u8path(path);
  cmd += " --replay " + quote(live.u8string());
  cmd += " --spectate-view " + std::to_string(std::clamp(gx::RenderOptions::live_spectate_view(), 0, 2));
  cmd += " --replay-dir " + quote((live.parent_path() / "viewer").u8string());
  if (hidden) cmd += " --log-file " + quote((live.parent_path() / (live.stem().u8string() + ".log")).u8string());
  STARTUPINFOA si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  std::vector<char> line(cmd.begin(), cmd.end());
  line.push_back(0);
  if (!CreateProcessA(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    host::log("arena: viewer did not start (error %lu)", (unsigned long)GetLastError());
    return nullptr;
  }
  if (pi.hThread) CloseHandle(pi.hThread);
  host::log("arena: viewer pid %lu", (unsigned long)pi.dwProcessId);
  return pi.hProcess;
}
#elif !defined(MELEE_NO_SLIPPI)
void* launch_arena_viewer(const std::string&) { return nullptr; }
#endif

#ifdef MELEE_NO_SLIPPI
// No replay playback and no recording in this build. The game is told there is no replay; the
// callbacks stay in the host table so the same game interface version is served.
constexpr bool g_replay = false;
uint8_t replay_gate(int32_t, uint8_t) { return 0; }
void write_replay_recording(bool) {}
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
#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
// Applies only to the recording begun by the last Game Start.
bool g_replay_squad = false;
#endif
// The current recording goes to the spectator server only (an offline match with MELEE_SOURCE_RECORD
// "2"): write_replay_recording leaves it unwritten.
bool g_replay_spectator_only = false;

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
static void replay_hold_for_live_frame(int32_t frame);

uint8_t replay_gate(int32_t frame, uint8_t phase) {
  if (!g_replay) return 0;
  if (phase == 0) {
    replay_hold_for_live_frame(frame);
    // A recording still being written plays as close to live as it can: a viewer that opened late
    // (an arena watcher's window starts after the game does) runs unpaced and unheard until it is
    // within a frame of the newest one, then plays at normal speed again.
    static bool catching_up = false;
    if (!g_replay->IsProcessingComplete() && !replay_control::status().seeking) {
      const int32_t behind = g_replay->GetLatestIndex() - frame;
      // On the clock the players' games log their frames with ("online frame N wall T"): a test
      // compares the two to see how far behind the match a watcher's window shows it.
      if (frame % 30 == 0 && !catching_up) host::log("replay: live frame %d wall %.3f s", frame, host::now_seconds());
      const bool was = catching_up;
      if (behind > 3) catching_up = true;
      else if (behind <= 1) catching_up = false;
      if (catching_up != was) {
        host::options.fast = catching_up;
        host::audio_set_muted(catching_up);
        replay_control::set_catching_up(catching_up);   // the way there is not drawn: the window joins live
        if (!catching_up) host::log("replay: caught up with the live game at frame %d", frame);
      }
    } else if (catching_up) {
      catching_up = false;
      host::options.fast = false;
      host::audio_set_muted(false);
      replay_control::set_catching_up(false);
    }
    return replay_control::gate(frame) == replay_control::Gate::GoBack ? 1 : 0;
  }
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
  // UCF 0.8 (Slippi's list until it moved to 0.84, so replays from 2023 and before) patches the same
  // three places as 0.84's dashback, shield drop and tumble, with different code. These say "a UCF"
  // here; which one is settled after the loop, from the 0.84-only codes and the code sizes.
  constexpr uint32_t kUcfShared = 0x80000000u;
  static const Known known[] = {
      {0x800D65EC, MU_REPLAY_CODE_UCF084, "UCF 0.84 DBOOC SquatRv fix"},
      {0x800C9A44, kUcfShared, "UCF dashback"}, {0x8006B460, MU_REPLAY_CODE_UCF084, "UCF pad buffer"},
      {0x8008E54C, MU_REPLAY_CODE_UCF084, "UCF SDI"}, {0x8009A0B8, MU_REPLAY_CODE_UCF084, "UCF shield drop extended"},
      {0x800998A4, kUcfShared, "UCF shield drop"}, {0x80093294, MU_REPLAY_CODE_UCF084, "UCF shield SDI"},
      {0x800908F4, kUcfShared, "UCF tumble"},
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
      // The older online lists (netplay-old1, 2023): Stadium always frozen, a jump from the start of
      // grStadium_801D4548 to its exit with no toggle of its own.
      {0x801D4578, MU_REPLAY_CODE_PS_FROZEN_CHECK, "Stadium frozen (older online lists)"},
      // Preload Stadium Transformations (Slippi's 2023 core list): the next transformation is rolled
      // and loaded as the countdown starts, not when it ends (grpstadium.c, MU_RC_PS_PRELOAD).
      {0x801D14C8, MU_REPLAY_CODE_PS_PRELOAD, "Stadium preload: init isLoaded"},
      {0x801D45EC, MU_REPLAY_CODE_PS_PRELOAD, "Stadium preload: load transformation"},
      {0x801D460C, MU_REPLAY_CODE_PS_PRELOAD, "Stadium preload: preloaded transition"},
      {0x801D4610, MU_REPLAY_CODE_PS_PRELOAD, "Stadium preload: skip normal decision"},
      {0x801D4724, MU_REPLAY_CODE_PS_PRELOAD, "Stadium preload: skip normal load"},
      {0x801D4F14, MU_REPLAY_CODE_PS_PRELOAD, "Stadium preload: reset isLoaded"},
      {0x800165AC, MU_REPLAY_CODE_PS_FILE_LOAD, "Stadium StadiumFileLoad"},
      // Slippi 3.7.0: two stage states that carried over from the last game are reset at stage init.
      {0x801CD37C, MU_REPLAY_CODE_CASTLE_WIND_RESET, "Reset Castle Wind"},
      {0x801D69B0, MU_REPLAY_CODE_KONGO_LOG_RESET, "Reset Kongo Log Bridge"},
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
  size_t ucf_dashback_bytes = 0, ucf_shield_drop_bytes = 0;
  for (const auto& c : kept) {
    const Known* k = nullptr;
    for (const auto& e : known) if (e.address == c.address) { k = &e; break; }
    if (!k) {
      ++unknown;
      host::log("replay: code %08X (type %02X, %zu bytes) is not implemented natively; this replay may diverge",
                c.address, c.type, c.bytes.size());
      continue;
    }
    codes |= k->code;
    if (c.address == 0x800C9A44) ucf_dashback_bytes = c.bytes.size();
    if (c.address == 0x800998A4) ucf_shield_drop_bytes = c.bytes.size();
    // IngameCheckIfFrozen keeps its toggle in its own code: b; blrl; .byte toggle.
    if (c.address == 0x801D457C && c.bytes.size() >= 8 + 12) *ps_frozen_toggle = c.bytes[8 + 8];
    // That jump (32 bytes: fmr f31,f1; lis/ori r12,801D4FD8; mtctr; bctr): no toggle, always frozen.
    if (c.address == 0x801D4578 && c.bytes.size() == 32 && c.bytes[0x18] == 0x4E && c.bytes[0x1B] == 0x20) *ps_frozen_toggle = 1;
  }
  // Which UCF the shared sites belong to. 0.84 brings codes of its own (pad buffer, SDI, ...); 0.8's
  // dashback and shield drop are 352 and 312 bytes (External/UCF 0.8, Slippi's list_console_UCF).
  // Anything else is a UCF the native game does not have: said, and the replay plays without one.
  if (codes & kUcfShared) {
    codes &= ~kUcfShared;
    if (codes & MU_REPLAY_CODE_UCF084) {
      // 0.84: the shared sites are its own.
    } else if (ucf_dashback_bytes == 352 && ucf_shield_drop_bytes == 312) {
      codes |= MU_REPLAY_CODE_UCF08;
      host::log("replay: recorded with UCF 0.8 (dashback, shield drop, tumble); played with UCF 0.8");
    } else {
      codes |= MU_REPLAY_CODE_UCF_OTHER;
      ++unknown;
      host::log("replay: recorded with a UCF other than 0.84 or 0.8 (dashback %zu bytes, shield drop %zu bytes); "
                "this replay may diverge", ucf_dashback_bytes, ucf_shield_drop_bytes);
    }
  }
  host::log("replay: code list %zu bytes, %zu gameplay codes kept (%zu denylisted by %s), %zu not native; codes mask %08X",
            s.geckoCodes.size(), kept.size(), dropped, sys.c_str(), unknown, codes);
  return codes;
}

// in_background (the match's end, at GAME!): the file is written on a thread of its own, so the game
// never waits on the disk there. A 4 to 8 MB replay took players 120 ms to 8.5 s to save (the file
// checked by antivirus as it closes), and an online opponent's game gave up on the stalled one.
// Everything else (quitting, a crash, a recording not written yet) writes at once, after any write still
// going (host::wait_file_writes; end_process waits for it too).
void write_replay_recording(bool in_background) {
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  if (g_replay_squad && !g_replay_written && g_replay_stream.active()) {
    g_replay_written = true; // handled: later flushes must not log this recording again
    host::log("replay: not kept: Squad Strike swaps cannot be played back yet");
    return;
  }
#endif
  // Written already (the menu after GAME!, quitting): nothing to wait for here. end_process waits.
  if (g_replay_written || !g_replay_stream.active() || g_replay_spectator_only) return;
  // The replay viewer saves only a game the player took part in (a takeover); a replay only watched is
  // already on disk, and saving it again left a copy per watch (replay_control::took_over).
  if (g_replaying && !replay_control::took_over()) {
    g_replay_written = true;
    host::log("replay: watched only, no copy saved");
    return;
  }
  host::wait_file_writes();   // an earlier match's file is on disk before this one is named
  std::vector<uint8_t> file = g_replay_stream.encode();
  if (file.empty()) {
    host::log("replay: recording failed: %s", g_replay_stream.error().c_str());
    return;
  }
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (yougame::agent_paths::replay((std::filesystem::u8path(host::options.replay_dir) / "Game.slp").u8string())) {
    std::time_t now = g_replay_stream.start_time() ? (std::time_t)g_replay_stream.start_time() : std::time(nullptr);
    std::tm tm{}; localtime_s(&tm, &now);
    char name[64]; std::strftime(name, sizeof name, "Game_%Y%m%dT%H%M%S", &tm);
    for (unsigned suffix = 0; suffix < 10000; ++suffix) {
      const auto out = std::filesystem::u8path(host::options.replay_dir) /
          (std::string(name) + (suffix ? "_" + std::to_string(suffix) : "") + ".slp");
      FILE* f = yougame::agent_paths::output(out.u8string(), true, true);
      if (!f) { if (errno == EEXIST) continue; host::log("replay: cannot create recording inside data directory"); return; }
      const bool written = std::fwrite(file.data(), 1, file.size(), f) == file.size();
      const bool closed = std::fclose(f) == 0;
      g_replay_written = written && closed;
      host::log("replay: %s %s (%zu bytes)", g_replay_written ? "recording written to" : "write failed for", out.u8string().c_str(), file.size());
      if (g_replay_written) net_trace::replay_saved(out.u8string().c_str());
      return;
    }
    host::log("replay: recording name collision limit reached");
    return;
  }
#endif
  // Named by the game's start, as Dolphin names the file when the game creates it.
  std::time_t now = g_replay_stream.start_time() ? (std::time_t)g_replay_stream.start_time() : std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &now);
  char name[64];
  std::strftime(name, sizeof name, "Game_%Y%m%dT%H%M%S.slp", &tm);
  // This recording is taken from here: a later call (the menu, quitting) does not write it again.
  g_replay_written = true;
  auto write = [file = std::move(file), dir = host::options.replay_dir, name = std::string(name)]() {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // Exclusive creation prevents quick successive matches (or two clients) from
    // overwriting a recording with the same second-resolution timestamp.
    std::filesystem::path out = std::filesystem::path(dir) / name;
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (unsigned suffix = 0; suffix < 10000; ++suffix) {
      if (suffix) out = std::filesystem::path(dir) / (name.substr(0, name.size() - 4) + "_" + std::to_string(suffix) + ".slp");
      handle = CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
    }
    if (handle != INVALID_HANDLE_VALUE) {
      const auto began = std::chrono::steady_clock::now();
      // Tests: MELEE_TEST_REPLAY_WRITE_DELAY_MS=N makes the disk this slow (a match must not wait on it).
      if (const char* slow = host::test_env::getenv("MELEE_TEST_REPLAY_WRITE_DELAY_MS"))
        std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(slow)));
      DWORD written = 0;
      const bool ok = WriteFile(handle, file.data(), (DWORD)file.size(), &written, nullptr) && written == file.size();
      const bool closed = CloseHandle(handle) != 0;
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
      host::log("replay: %s %s (%zu bytes, %.0f ms)", ok && closed ? "recording written to" : "write failed for",
                out.string().c_str(), file.size(), ms);
      if (ok && closed) net_trace::replay_saved(out.string().c_str());
    } else {
      host::log("replay: cannot write %s (error %lu)", out.string().c_str(), GetLastError());
    }
  };
  if (!in_background) { write(); return; }
  host::file_write_begin();
  std::thread([write = std::move(write)]() mutable {
    write();
    host::file_write_end();
  }).detach();
}

const MuReplayStart* h_replay_start() { return g_replay ? &g_replay_start : nullptr; }

// A recording that was never finished (the game or Dolphin closed in the middle of the match, so the
// file has no Game End and no metadata) gets no more frames. The game does not stop on a frame it
// cannot fetch: the match went on with nobody pressing anything until its timer ran out, or for good
// in a match without one. Such a file counts as over when it has not changed for a few seconds (its
// write time on the first look: a file from an earlier session ends at its last frame at once) and
// no other program holds it open for writing. A live broadcast keeps growing, and its writer keeps
// it open even while the match is paused, so it is still waited for.
static bool replay_recording_abandoned() {
  static std::string for_path;
  static bool abandoned = false;
  static std::uintmax_t last_size = 0;
  static double last_growth = 0.0, last_look = -1.0;
  const double now = host::now_seconds();
  if (for_path != g_replay_path) {   // a new file: forget the last one's observations
    for_path = g_replay_path; abandoned = false; last_size = ~std::uintmax_t{0}; last_look = -1.0;
  }
  if (abandoned) return true;
  if (last_look >= 0.0 && now - last_look < 0.5) return false;   // asked again every retrace while waiting
  last_look = now;
  std::error_code ec;
  const std::filesystem::path path = std::filesystem::u8path(g_replay_path);
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec) return false;
  if (size != last_size) {
    const bool first = last_size == ~std::uintmax_t{0};
    last_size = size;
    last_growth = now;
    if (!first) return false;                      // it grew while we watched: still being written
    const auto written = std::filesystem::last_write_time(path, ec);
    if (!ec) {
      const double age = std::chrono::duration<double>(std::filesystem::file_time_type::clock::now() - written).count();
      if (age > 0.0) last_growth = now - age;
    }
  }
  if (now - last_growth < 3.0) return false;
#ifdef _WIN32
  // Opening it for writing while only allowing others to read fails with a sharing violation exactly
  // when another handle can write it. Nothing is written; the handle is closed at once.
  HANDLE h = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return abandoned = true; }
  if (GetLastError() == ERROR_SHARING_VIOLATION) return false;
#endif
  return abandoned = now - last_growth >= 30.0;   // no way to ask who writes it (read-only file, other systems)
}

// WAIT for a frame that has not arrived yet, unless the recording was abandoned (above).
static int32_t replay_wait_or_end(int32_t frame) {
  if (!replay_recording_abandoned()) return MU_REPLAY_WAIT;
  host::log("replay: %s stops at frame %d with no Game End and nothing writing it any more; playback ends there",
            g_replay_path.c_str(), frame - 1);
  return MU_REPLAY_TERMINATE;
}

// A frame of a recording still being written that can be played: there, all of its inputs in, and
// final. A finished recording answers yes for every frame (the fetch then finds it or ends).
static bool replay_frame_ready(int32_t frame) {
  const bool exists = g_replay->DoesFrameExist(frame);
  if (g_replay->IsProcessingComplete()) return true;
  if (!exists || !g_replay->GetFrame(frame)->inputsFullyFetched) return false;
  const auto v = g_replay->GetVersion();
  const bool has_finalized = v[0] > 3 || (v[0] == 3 && v[1] >= 7);
  return !has_finalized || g_replay->GetLastFinalizedFrame() >= frame;
}

// Top of a frame, where a pause holds (replay_gate): a live recording's next frame is waited for
// here, the way Dolphin's mirror mode stops the game until the frame arrives. The fetch would
// otherwise answer WAIT and the match would run that frame with no one's inputs, and every frame
// after it would be one behind (an arena watcher's viewer that caught up with the live game).
static void replay_hold_for_live_frame(int32_t frame) {
  if (replay_frame_ready(frame)) return;
  const double started = host::now_seconds();
  bool told = false;
  while (!host::exit_requested() && !replay_frame_ready(frame) && !replay_recording_abandoned()) {
    if (!told && host::now_seconds() - started > 1.0) {
      std::error_code ec;
      const auto size = std::filesystem::file_size(std::filesystem::u8path(g_replay_path), ec);
      host::log("replay: waiting for frame %d of %s (read to frame %d, %llu bytes on disk)", frame,
                g_replay_path.c_str(), g_replay->GetLatestIndex(), ec ? 0ull : (unsigned long long)size);
      told = true;
    }
    if (host::g_has_window) host::window_pump();   // the simulation thread owns the window only without the render thread
    { host::perf::Scope wait_cost(host::perf::Stage::Wait); Sleep(4); }
  }
  if (told) host::log("replay: frame %d came after %.1f s", frame, host::now_seconds() - started);
}

void h_replay_frame(int32_t frame, MuReplayFrame* out) {
  *out = MuReplayFrame{};
  if (!g_replay) { out->result = MU_REPLAY_TERMINATE; return; }
  // A replay still being written (a live broadcast the Slippi Launcher saves as it arrives) has no
  // Game End yet: a frame that is not there, not fully received or not yet finalized is waited for,
  // as Dolphin's mirror mode does. Once Game End is read, a missing frame ends playback.
  const bool complete = g_replay->IsProcessingComplete();
  if (!g_replay->DoesFrameExist(frame)) { out->result = complete ? MU_REPLAY_TERMINATE : replay_wait_or_end(frame); return; }
  Slippi::FrameData* f = g_replay->GetFrame(frame);
  if (!complete) {
    const auto v = g_replay->GetVersion();
    const bool has_finalized = v[0] > 3 || (v[0] == 3 && v[1] >= 7);
    if (!f->inputsFullyFetched || (has_finalized && g_replay->GetLastFinalizedFrame() < frame)) {
      out->result = replay_wait_or_end(frame);
      return;
    }
  }
  out->result = MU_REPLAY_CONTINUE;
  {
    const int live = replay_control::takeover_port();
    out->live_ports = live >= 0 && live < 4 ? 1u << live : 0u;
  }
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
#ifdef MELEE_DOUBLES
  if (command==0x39 && !g_replaying) {
    const auto& c=yougame::console::console_engine().config();
    static std::string reported;
    if (c.doubles && reported!=c.session_id) {
      reported=c.session_id;
      int best[2]={255,255};
      if(size>=6) for(int port=0;port<4;++port)
        best[c.doubles_teams[port]]=std::min(best[c.doubles_teams[port]],int(payload[2+port]));
      // Time, elimination or (every Doubles stock win) team elimination; anything else is no contest.
      const bool invalid=size<6 || payload[0]<1 || payload[0]>3;
      const bool draw=!invalid && best[0]==best[1];
      yougame::doubles::report(c,best[1]<best[0]?1:0,draw,invalid);
    }
  }
#endif
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (command == 0x36)
    g_agent_fight_match = !g_replaying && size >= 0x2F8 && (payload[0x1A3] == 0x08 || payload[0x2BD] != 0);
  if (command == 0x39) g_agent_fight_match = false;
#endif
  if (!g_replay && command == 0x36) {
    write_replay_recording();
#if MU_SQUAD
    // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    // Save/skip the PREVIOUS recording before replacing its policy with this match's.
    g_replay_squad = g_squad_replay_pending;
    g_squad_replay_pending = false;
#endif
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
    {
      const char* record = std::getenv("MELEE_SOURCE_RECORD");
      g_replay_spectator_only = !online && record && std::string(record) == "2";
    }
    // Only layers that change the disc's files make a replay need them (a save file alone does not).
    g_replay_stream.set_mod_profile(g_retail_view || g_view_alias.empty() ? std::string() : g_mod_fingerprint);
    // The 20XX TE features in effect (offline only; online matches never carry them).
    // (Or the Hack Pack's share of them, with the pack and no TE save: te_words_mask1/2.)
    // What RAN, not what is saved: a --vanilla-only session plays with none of them whatever the
    // Game tab holds, and its replay must say so, or playback would switch "20XX CPUs" back on and
    // a Vanilla launch would refuse the recording as modded.
    g_replay_stream.set_feature_options(online || host::gameplay::vanilla_only() ? 0u
                                            : (gx::RenderOptions::live_te_options() & te_words_mask1()));
    g_replay_stream.set_feature_options2(online ? 0u : h_game_options2());   // "20XX CPUs" too: playback replays it
    g_replay_stream.set_feature_options3(online ? 0u : h_game_options3());   // CPU training options, likewise
    // The spectator server (Slippi Dolphin's): start_game, then the command table and every event
    // as recorded, then end_game after Game End, the order Dolphin's DMAWrite sends them.
#if MU_SQUAD
    // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    if (g_replay_squad) {
      g_replay_stream.set_event_sink(nullptr);
      host::log("spectate: not streamed: Squad Strike swaps cannot be watched yet");
    } else {
#endif
    slippi::spectate::start_game();
    g_replay_stream.set_event_sink([](const uint8_t* data, uint32_t size) {
      slippi::spectate::write(data, size);
      slippi::arena::recording_event(data, size);   // an arena room's live feed and game reports
    });
#if MU_SQUAD
    // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    }
#endif
    if (!g_replay_stream.begin_slippi(start, codes, (int64_t)std::time(nullptr))) {
      host::log("replay: cannot start native recording: %s", g_replay_stream.error().c_str());
#if MU_SQUAD
      // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
      if (!g_replay_squad)
#endif
      slippi::spectate::end_game();
    }
    else if (online)
      host::log("replay: recording online game %u", g_online_games);
    return;
  }
  // Dolphin closes the file at the Game End event; what the game still sends is not recorded.
  if (!g_replay && (!g_replay_stream.active() || g_replay_stream.ended())) return;
  if (!g_replay_stream.append(command, payload, size))
    host::log("replay: cannot record event %02X: %s", command, g_replay_stream.error().c_str());
  if (!g_replay && command == 0x39) {
#if MU_SQUAD
    // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    if (!g_replay_squad)
#endif
    slippi::spectate::end_game();
    // The native game's stable Game End record has the end method and four placements.
    // A unique zero placement is a winner; ambiguous endings stay incomplete.
    int winner = -1;
    if (size >= 6 && payload[0] == 2) {
      for (int slot = 0; slot < 4; ++slot) if (payload[2 + slot] == 0) {
        if (winner >= 0) { winner = -1; break; }
        winner = slot;
      }
    }
    host::publish_lobby_result(winner, size ? payload[0] : 0, size >= 2 ? (int)(int8_t)payload[1] : -1,
                               size >= 6 ? payload + 2 : nullptr);
    if (g_online_test_mode == 0) { g_ranked_harness.game_end_ms = GetTickCount64(); g_ranked_harness.reentry_asked = false; }
    write_replay_recording(true);   // at GAME!: the disk is not waited on
  }
}

void h_replay_finished() {
#ifdef YOUGAME_AGENT_TEST_HOOK
  g_agent_fight_match = false;
#endif
  if (!g_replay) {
    write_replay_recording();
    return; // An ordinary VS match returns to the menu, ready to record the next match.
  }
  host::log("replay: playback finished");
  if (g_savestates.active() && !g_replay_marks.empty())
    host::log("replay: %zu states were kept for jumping back (%zu MB)", g_savestates.kept(), g_savestates.pool_bytes() >> 20);
  replay_control::end();
  if (g_replay_game_end.empty()) g_replay_game_end = read_replay_event(g_replay_path, 0x39);   // a live file gets it last
  if (!g_replay_game_end.empty() && g_replay_stream.last_frame() >= g_replay->GetLatestIndex())
    h_replay_event(0x39, g_replay_game_end.data(), (uint32_t)g_replay_game_end.size());
  write_replay_recording();
  host::set_replay_viewing(false);   // played to its end: the launcher may start the next queued replay
  host::request_exit(0);
}
#endif

int32_t h_card_boot_confirm(uint32_t channel, uint32_t decision) {
  if (yougame::console::console_active()) return yougame::memcard::confirm(true, channel, decision);
#ifdef __APPLE__
  // The isolated Mac memory-card check records the two automatic boot confirmations.
  const bool answer = yougame::memcard::confirm(yougame::active(), channel, decision);
  static unsigned logged = 0;
  if (answer && !(logged & (1u << decision))) {
    logged |= 1u << decision;
    host::log("yougame: memory-card boot auto-confirm decision %u (slot A)", decision);
  }
#endif
  return yougame::memcard::confirm(yougame::active(), channel, decision);
}

MuHostApi make_host() {
  MuHostApi h{};
  h.version = MU_HOST_API_VERSION;
  h.test_getenv = [](const char* name) -> const char* {
    return yougame::console::console_active() ? nullptr : host::test_env::getenv(name);
  };
  h.console_frame_begin = h_console_frame_begin;
  h.console_seed = h_console_seed;
  h.console_has_save = h_console_has_save;
  h.console_rollback = h_console_rollback;
  h.console_alarm_clock = h_console_alarm_clock;
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
#if defined(__APPLE__) && defined(__aarch64__)
  // The game passes these a byte the AArch64 Linux way, which leaves the register's upper bits
  // unspecified; Apple's convention has the caller extend it, and clang's code relies on that. These
  // take the whole register and keep the byte.
  h.replay_event = reinterpret_cast<decltype(h.replay_event)>(
      +[](uint32_t command, const uint8_t* payload, uint32_t size) { h_replay_event(uint8_t(command), payload, size); });
#endif
  h.replay_finished = h_replay_finished;
  const char* draw_audit = std::getenv("MELEE_NATIVE_DRAW_AUDIT");
  const bool audit_enabled = draw_audit && std::strcmp(draw_audit, "1") == 0;
  gx::set_native_draw_audit(audit_enabled);
  if (audit_enabled) h.native_render_scope_event = h_native_render_scope_event;
  h.native_draw_identity = h_native_draw_identity;
  h.mod_flags = h_mod_flags;
  h.slippi_command = h_slippi_command;
#if defined(__APPLE__) && defined(__aarch64__)
  h.slippi_command = reinterpret_cast<decltype(h.slippi_command)>(
      +[](uint32_t command, const uint8_t* payload, uint32_t payload_size, uint8_t* response, uint32_t response_capacity,
          uint32_t* response_size) {
        return h_slippi_command(uint8_t(command), payload, payload_size, response, response_capacity, response_size);
      });
#endif
  h.resim_phase = h_resim_phase;
  h.online_test_match = h_online_test_match;
  h.game_options2 = h_game_options2;
  h.hud_scales = h_hud_scales;
  h.hud_player = h_hud_player;
  h.vi_idle_step = h_vi_idle_step;
  h.game_options3 = h_game_options3;
  h.display_options = h_display_options;
  h.audio_options = h_audio_options;
  h.card_boot_confirm = h_card_boot_confirm;
  h.console_origin = h_console_origin;
  h.picture_widen = h_picture_widen;
  h.hud_combo = h_hud_combo;
  h.shield_color = h_shield_color;
  h.grid_color = h_grid_color;
  h.screen_shake_scale = h_screen_shake_scale;
  h.hud_extras = h_hud_extras;
  h.native_rtx_semantics = h_native_rtx_semantics;
  h.hud_pro_tag = h_hud_pro_tag;
  return h;
}

// A crash inside the game: the faulting address and the game's return addresses on the stack, as
// offsets into melee_game.dll (resolve with addr2line -e melee_game.dbg). The application's own
// crash report still runs afterwards.
#ifndef _WIN32
// Linux: a fault inside the game logs its offset in melee_game.so (addr2line -e melee_game.dbg) and
// keeps a --replay run's recording, then hands the signal on to the application's crash report.
struct sigaction g_previous_crash_action[32];
void on_game_signal(int sig, siginfo_t* si, void* uc_void) {
  const ucontext_t* uc = static_cast<const ucontext_t*>(uc_void);
  const uint64_t base = GAME_IMAGE_BASE, end = base + host::game_image_size;
#ifdef __APPLE__
  const uint64_t rip = uc ? mac_or_linux_rip(uc) : 0;
#else
  const uint64_t rip = uc ? (uint64_t)uc->uc_mcontext.gregs[REG_RIP] : 0;
#endif
  if (rip >= base && rip < end) {
    host::mark_dying();
    write_replay_recording();
    host::log("game crash signal %d at melee_game.so+0x%llX, address %p", sig, (unsigned long long)(rip - base), si ? si->si_addr : nullptr);
#ifndef __APPLE__
    host::log("  rax %016llX rbx %016llX rcx %016llX rdx %016llX", (unsigned long long)uc->uc_mcontext.gregs[REG_RAX],
              (unsigned long long)uc->uc_mcontext.gregs[REG_RBX], (unsigned long long)uc->uc_mcontext.gregs[REG_RCX],
              (unsigned long long)uc->uc_mcontext.gregs[REG_RDX]);
    // The game's frames on its stack (GCC keeps frame pointers out, so this is the return addresses
    // that point into the game, as the Windows leaf fallback reads them).
    const uint64_t* sp = (const uint64_t*)uc->uc_mcontext.gregs[REG_RSP];
#else
    const uint64_t* sp = (const uint64_t*)mac_or_linux_sp(uc);
#endif
    for (int i = 0, shown = 0; i < 512 && shown < 24; ++i) {
      const uint64_t v = sp[i];
      if (v > base && v < end) { host::log("  stack: melee_game.so+0x%llX", (unsigned long long)(v - 1 - base)); ++shown; }
    }
  }
  struct sigaction& previous = g_previous_crash_action[sig & 31];
  if (previous.sa_flags & SA_SIGINFO) { if (previous.sa_sigaction) previous.sa_sigaction(sig, si, uc_void); }
  else if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN && previous.sa_handler) previous.sa_handler(sig);
  else { signal(sig, SIG_DFL); raise(sig); }
}
void install_game_signal_handler() {
  struct sigaction sa{};
  sa.sa_sigaction = on_game_signal;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE}) sigaction(sig, &sa, &g_previous_crash_action[sig & 31]);
}
#else
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
      uint64_t caller = 0;
      SIZE_T read = 0;
      if (ReadProcessMemory(GetCurrentProcess(), (const void*)info->ContextRecord->Rsp,
                            &caller, sizeof caller, &read) && read == sizeof caller) {
        called_by_game = caller >= base && caller < end;
      }
    }
    if (!called_by_game) return EXCEPTION_CONTINUE_SEARCH;
  }
  host::mark_dying();
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
#ifdef _MSC_VER
    __try {
      if (fn) {
        void* handler_data = nullptr; DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx.Rip, fn, &ctx, &handler_data, &frame, nullptr);
      } else {
        ctx.Rip = *(const DWORD64*)ctx.Rsp;   // a leaf: the return address is on top
        ctx.Rsp += 8;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
#else
    // MinGW cannot protect RtlVirtualUnwind with MSVC SEH. Read a leaf's return address
    // through the checked self-process API; stop at a non-leaf rather than risking a second fault.
    if (fn) break;
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (const void*)ctx.Rsp, &ctx.Rip, sizeof ctx.Rip, &read) ||
        read != sizeof ctx.Rip) break;
    ctx.Rsp += 8;
#endif
    if (ctx.Rip == 0) break;
    if (ctx.Rip >= base && ctx.Rip < end) host::log("  stack: melee_game.dll+0x%llX", (unsigned long long)(ctx.Rip - 1 - base));
    else { host::log("  (leaves the game at %016llX)", (unsigned long long)ctx.Rip); break; }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

}  // namespace

void css_origin_arm() { g_css_origin.armed = true; }
bool request_css_origin_reset(const std::vector<uint8_t>& bytes) { return queue_css_origin_reset(bytes); }
bool card_self_test(const char* directory) { return card_self_test_impl(directory); }

std::vector<uint8_t> console_host_save() {
  // Snapshot published by the simulation thread using the mounted card's exact save selection.
  const auto snapshot = std::atomic_load(&g_console_host_snapshot);
  if (snapshot) {
    if (snapshot->empty()) host::log("card: Friends host save: the mounted card has no usable Melee save");
    return *snapshot;
  }
  // A direct console launch can precede source_port::run and its card mount. Read only the
  // canonical Melee save in the configured ordinary card (including VanillaOnly), never a
  // different save or an imported/profile file. Human Friends uses the mounted snapshot above.
  const auto path = std::filesystem::u8path(host::options.card_dir) / "GALE01-SuperSmashBros0110290334.gci";
  std::error_code ec;
  if (std::filesystem::is_symlink(path,ec) || !std::filesystem::is_regular_file(path,ec)) {
    host::log("card: Friends host save: %s %s (card not mounted)", std::filesystem::is_symlink(path,ec) ? "a link, not read:" : "no save file at",
              card_shown(path).c_str());
    return {};
  }
  const auto size = std::filesystem::file_size(path,ec);
  if (ec || size < 64 || size > yougame::console::kCardLimit) {
    host::log("card: Friends host save: unusable size %llu (card not mounted)", (unsigned long long)(ec ? 0 : size)); return {};
  }
  std::ifstream in(path,std::ios::binary); std::vector<uint8_t> bytes(size);
  in.read(reinterpret_cast<char*>(bytes.data()),size);
  if (in && yougame::console::melee_gci(bytes)) return bytes;
  host::log("card: Friends host save: %s (%llu bytes, card not mounted)", in ? "not a Melee save" : "couldn't read the save", (unsigned long long)size);
  return {};
}

bool begin_console(std::unique_ptr<yougame::console::SessionLink> link,
                   const yougame::console::SessionConfig& config) {
  if (g_console_boot || g_game.run || g_replaying || g_online_test_mode >= 0 || source_p2p::requested() ||
      !link || !yougame::console::Engine::valid(config)) return false;
  const char* data = std::getenv("YOUGAME_DATA"); // path only; never reads bridge credentials
  std::filesystem::path card;
  if (!data || !yougame::console::prepare_card(std::filesystem::u8path(data), config, card)) {
    host::log("console: cannot prepare fresh session card"); return false;
  }
  yougame::console::EngineHooks hooks;
  hooks.log = [](const std::string& line) { host::log("%s", line.c_str()); };
  hooks.notice = [](const char* text) { screen_label::show(text, 8.0); };
  hooks.host_ticks = [] { return host::cpu ? host::cpu->tb : uint64_t(0); };
  hooks.host_catch_up = console_host_catch_up;
#if MU_SQUAD
  hooks.session_edge = slippi::online::squad_console_reset;   // Friends' costume rule is per session
#endif
  hooks.responsive = [] {
    host::window_pump(); // same message pump as host::retrace, without time, alarms, or game callbacks
    if (host::exit_requested()) yougame::console::console_engine().end("window closed");
  };
  const bool rollback = yougame::console::rollback_setting(std::getenv("MELEE_CONSOLE_ROLLBACK"));
  if (!yougame::console::console_engine().begin(std::move(link), config, std::move(hooks), rollback)) return false;
  // Friends (2 seats) whose host couldn't share its save: same fresh card on every seat, say so.
  if (config.ports.size() == 2 && config.card_digest.empty()) {
    host::log("console: %s", yougame::console::kDefaultCard);
    screen_label::show(yougame::console::kDefaultCard, 12.0);
  }
  const auto pick = yougame::friends::css_pick(config.settings_digest);
  g_console_host_pad = config.my_port == 1 && pick ? pick->pad : -1;
  g_console_card = std::move(card);
  g_console_boot = true;
  return true;
}

int32_t console_practice_bridge(int32_t op, int32_t* args, int32_t count) {
  if (yougame::console::console_active() && op != MU_PRACTICE_SCENE &&
      op != MU_PRACTICE_FRIENDS_RETURN && op != MU_PRACTICE_GET_PLAYER && op != MU_PRACTICE_GET_STAGE &&
      op != MU_PRACTICE_GET_ONLINE_MODE && op != MU_PRACTICE_UNLOCKED &&
      !(op == MU_PRACTICE_PAGE_PAUSE && count >= 3 && args[0] == -1)) return -1;
  return g_game.practice ? g_game.practice(op, args, count) : -1;
}

// --match <stage>:<p1>[:<p2>...], each player <kind>[/c<level>][/x<costume>], all numbers decimal
// or 0x-hex. For example "0x14:9:12/c9" is Onett, one human, one level 9 CPU.
bool set_online_test(const char* spec) {
  std::string s = spec ? spec : "";
  std::string mode = s.substr(0, s.find(':'));
  int id = mode == "ranked" ? 0 : mode == "unranked" ? 1 : mode == "direct" ? 2 : mode == "teams" ? 3 : -1;
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  if (mode == "squad") {
    MuSquadSide lineup{};
    if (s.size() < 7 || !slippi::squad_test::parse(s.c_str() + 6, &lineup)) return false;
    g_online_test_squad = lineup;
    g_online_test_mode = 1; // Unranked, exactly the Squad row's mode
    g_online_test_character = lineup.members[0].kind;
    g_online_test_color = lineup.members[0].color;
    return true;
  }
  g_online_test_squad = {};
#endif
  if (id < 0) return false;
  int character = 2, color = 0;
  size_t colon = s.find(':');
  if (colon != std::string::npos) {
    char* end = nullptr;
    character = (int)std::strtol(s.c_str() + colon + 1, &end, 10);
    if (end && *end == ':') color = (int)std::strtol(end + 1, &end, 10);
    if (!end || *end != '\0' || character < 0 || character > 255 || color < 0 || color > 5) return false;
  }
  g_ranked_harness.quit = yougame::online_test::QuitAfter(id, std::getenv("MELEE_ONLINE_TEST_QUIT_AFTER"));
  g_online_test_mode = id;
  g_online_test_character = (uint8_t)character;
  g_online_test_color = (uint8_t)color;
  return true;
}

#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
bool check_squad_test(bool local_peer, bool invalid_lineup, bool net_delay) {
  if ((g_online_test_squad.count || invalid_lineup || net_delay) && !local_peer) return false;
  if ((invalid_lineup || net_delay) && !g_online_test_squad.count) return false;
  g_online_test_squad_cancel = invalid_lineup;
  return true;
}
#endif

void set_slippi_menus(bool on) { g_slippi_menus_requested = on; }
void set_resume_online() { g_resume_online = true; }

void set_mod_directory(const char* path) {
  g_mod_layers.push_back({mods::LayerKind::Dir, std::filesystem::u8path(path)});
  g_explicit_mod_layers.push_back(g_mod_layers.back());
  g_mod_root_explicit = true;
}

void set_mod_iso(const char* path) {
  g_mod_layers.push_back({mods::LayerKind::Iso, std::filesystem::u8path(path)});
  g_explicit_mod_layers.push_back(g_mod_layers.back());
}

void set_mod_gci(const char* path) {
  g_mod_layers.push_back({mods::LayerKind::Gci, std::filesystem::u8path(path)});
  g_explicit_mod_layers.push_back(g_mod_layers.back());
}

bool set_mod_profile(const char* name, bool explicit_layer) {
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
  if (explicit_layer)
    g_explicit_mod_layers.insert(g_explicit_mod_layers.end(), profile.layers.begin(), profile.layers.end());
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
  if (host::gameplay::vanilla_only() &&
      (!g_replay_mod_profile.empty() || g_replay_feature_options || g_replay_feature_options2 ||
       (g_replay_feature_options3 & MU_GAME_OPTION3_CPU_ALL))) {
    std::fprintf(stderr, "--vanilla-only cannot play a replay recorded with gameplay mods; open it in a normal launch\n");
    return false;
  }
  g_replaying = true;
  if (g_replay_feature_options) host::log("replay: recorded with game options %08X", g_replay_feature_options);
  g_replay = Slippi::SlippiGame::FromFile(path);
  replay_coach::clear();
  if (!g_replay || !g_replay->AreSettingsLoaded()) {
    host::log("replay: cannot read %s", path);
    g_replay.reset();
    return false;
  }
  g_replay_path = path;
  g_replay_game_start = read_replay_event(path, 0x36);
  {
    // The coach's tips from the file itself, before anything plays (host/replay_scan.h).
    const double started = host::now_seconds();
    const std::vector<replay_coach::Event> found = replay_scan::scan(*g_replay);
    replay_coach::add_scanned(found);
    host::log("replay: %zu frame data events read from the file (%.0f ms)", found.size(), (host::now_seconds() - started) * 1000.0);
    if (host::test_env::getenv("MELEE_TEST_COACH_LOG")) {   // tests: the file's readings, to compare with the game's
      for (const replay_coach::Event& e : found) {
        if (e.kind != replay_coach::Kind::Shield && e.kind != replay_coach::Kind::Hit) continue;
        const replay_coach::Readout r = replay_coach::readout(e);
        host::log("scan: frame %d P%d %s %s %s (move %d)", e.frame, e.actor + 1, r.head.c_str(), r.value.c_str(), r.detail.c_str(), e.move);
      }
    }
  }
  g_replay_game_end = read_replay_event(path, 0x39);
  if (g_replay_game_start.empty()) { host::log("replay: %s has no Game Start event", path); g_replay.reset(); return false; }
  Slippi::GameSettings* s = g_replay->GetSettings();
  g_replay_stream.set_event_sink(nullptr);   // the viewer's own recording is not broadcast
#if !defined(MELEE_NO_SLIPPI)
  // An arena room's match window: its own frames tell the game that opened it the stocks and percent it
  // shows (slippi_arena.cpp viewer_score_event), so that game's warm-up panel matches it frame for frame.
  if (host::options.spectate)
    g_replay_stream.set_event_sink([](const uint8_t* data, uint32_t size) { slippi::arena::recording_event(data, size); });
#endif
  replay_control::forget_takeovers();
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
  {
    // The players, for the takeover menu: "P1 Falco".
    static const char* const kNames[26] = {"Captain Falcon", "Donkey Kong", "Fox", "Mr. Game & Watch", "Kirby", "Bowser",
        "Link", "Luigi", "Mario", "Marth", "Mewtwo", "Ness", "Peach", "Pikachu", "Ice Climbers", "Jigglypuff", "Samus",
        "Yoshi", "Zelda", "Sheik", "Falco", "Young Link", "Dr. Mario", "Roy", "Pichu", "Ganondorf"};
    std::string names[4];
    uint32_t ports = 0;
    for (int i = 0; i < 4; ++i) {
      if (!g_replay->DoesPlayerExist((int8_t)i)) continue;
      ports |= 1u << i;
      const uint8_t c = s->players[(uint8_t)i].characterId;
      names[i] = "P" + std::to_string(i + 1) + " " + (c < 26 ? kNames[c] : "Fighter");
    }
    replay_control::set_players(ports, names);
    // The Lab view's HUD names the players as Slippi Lab does: the display name, else the connect code.
    std::string lab_names[4];
    for (int i = 0; i < 4; ++i) {
      if (!(ports & (1u << i))) continue;
      const auto& p = s->players[(uint8_t)i];
      lab_names[i] = lab::name_from_sjis(p.displayName.data(), p.displayName.size());
      if (lab_names[i].empty()) lab_names[i] = lab::name_from_sjis(p.connectCode.data(), p.connectCode.size());
    }
    lab::set_replay_names(lab_names);
  }
  replay_control::set_replay_path(path);
  replay_control::begin(Slippi::GAME_FIRST_FRAME, g_replay->GetLatestIndex(), engine);
  host::set_replay_viewing(true);
  return true;
}
#endif

#ifdef _WIN32
// Inside an AppContainer (the YouGame desktop app's sandbox) Windows relocates every image it loads,
// even one built for a fixed base with ASLR off, and the game image works only at GAME_IMAGE_BASE (its
// 32-bit pointers sit beside MEM1). There the host maps the image itself, the way the loader would:
// sections at their addresses, imports bound, page protections, the exception tables registered, the
// TLS callbacks and the entry point run. The image uses no implicit TLS (its .tls holds only the
// runtime's callbacks), so there is no TLS slot to set up. Everywhere else LoadLibrary does it.
static bool in_app_container() {
  HANDLE token = nullptr;
  DWORD is = 0, len = 0;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  const bool ok = GetTokenInformation(token, TokenIsAppContainer, &is, sizeof is, &len);
  CloseHandle(token);
  return ok && is != 0;
}

// Found as LoadLibrary would: a bare name sits beside the program (the working directory is the
// sandbox's data folder).
static std::filesystem::path game_library_file(const std::string& path) {
  std::filesystem::path file_path = std::filesystem::u8path(path);
  if (file_path.is_relative()) {
    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) file_path = std::filesystem::path(exe).parent_path() / file_path;
  }
  return file_path;
}
static HMODULE map_game_image(const std::string& path, std::string* error) {
  const std::filesystem::path file_path = game_library_file(path);
  std::ifstream in(file_path, std::ios::binary);
  std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto* dos = (const IMAGE_DOS_HEADER*)file.data();
  if (file.size() < sizeof(IMAGE_DOS_HEADER) || dos->e_magic != IMAGE_DOS_SIGNATURE || (size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS) > file.size()) {
    *error = "not a Windows image";
    return nullptr;
  }
  const auto* nt = (const IMAGE_NT_HEADERS*)(file.data() + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt->OptionalHeader.ImageBase != GAME_IMAGE_BASE) {
    *error = "not the x64 game image for this base";
    return nullptr;
  }
  const size_t size = nt->OptionalHeader.SizeOfImage;
  // Committed inside the range held since startup (g_game_image_reservation), so nothing can take it in
  // between. GAME_IMAGE_HOLD_END bounds the image to what GX can address.
  if (!g_game_image_reservation.base || size > GAME_IMAGE_HOLD_END - GAME_IMAGE_BASE) { *error = "its address range is not held"; return nullptr; }
  auto* base = (uint8_t*)VirtualAlloc((void*)GAME_IMAGE_BASE, size, MEM_COMMIT, PAGE_READWRITE);
  if (base != (uint8_t*)GAME_IMAGE_BASE) {
    *error = "its address range could not be committed";
    return nullptr;
  }
  std::memcpy(base, file.data(), std::min<size_t>(nt->OptionalHeader.SizeOfHeaders, file.size()));
  const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
  for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    const size_t raw = std::min<size_t>(sec[i].SizeOfRawData, sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData);
    if (sec[i].VirtualAddress + raw > size || sec[i].PointerToRawData + raw > file.size()) { *error = "a section is out of bounds"; VirtualFree(base, size, MEM_DECOMMIT); return nullptr; }
    std::memcpy(base + sec[i].VirtualAddress, file.data() + sec[i].PointerToRawData, raw);
  }
  // Imports: the system's runtime and kernel32, bound by name or ordinal.
  const auto& imp = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  for (auto* d = (IMAGE_IMPORT_DESCRIPTOR*)(base + imp.VirtualAddress); imp.Size && d->Name; ++d) {
    const char* dll = (const char*)(base + d->Name);
    HMODULE lib = LoadLibraryA(dll);
    if (!lib) { *error = std::string("cannot load ") + dll; VirtualFree(base, size, MEM_DECOMMIT); return nullptr; }
    auto* names = (IMAGE_THUNK_DATA64*)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
    auto* slots = (IMAGE_THUNK_DATA64*)(base + d->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++slots) {
      FARPROC fn = IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)
                       ? GetProcAddress(lib, (LPCSTR)IMAGE_ORDINAL64(names->u1.Ordinal))
                       : GetProcAddress(lib, ((const IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name);
      if (!fn) { *error = std::string("missing import from ") + dll; VirtualFree(base, size, MEM_DECOMMIT); return nullptr; }
      slots->u1.Function = (ULONGLONG)fn;
    }
  }
  // Page protections from the section flags, the headers read-only.
  DWORD old = 0;
  VirtualProtect(base, nt->OptionalHeader.SizeOfHeaders, PAGE_READONLY, &old);
  for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    const DWORD c = sec[i].Characteristics;
    const size_t len = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
    if (!len) continue;
    const bool x = c & IMAGE_SCN_MEM_EXECUTE, w = c & IMAGE_SCN_MEM_WRITE;
    VirtualProtect(base + sec[i].VirtualAddress, len, x ? (w ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ) : (w ? PAGE_READWRITE : PAGE_READONLY), &old);
  }
  FlushInstructionCache(GetCurrentProcess(), base, size);
  // Unwinding (C++ exceptions, the crash handler's stack walk) needs the image's function table.
  const auto& pdata = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
  if (pdata.Size) RtlAddFunctionTable((PRUNTIME_FUNCTION)(base + pdata.VirtualAddress), pdata.Size / sizeof(RUNTIME_FUNCTION), (DWORD64)base);
  using Init = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
  const auto& tls = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
  if (tls.Size) {
    const auto* dir = (const IMAGE_TLS_DIRECTORY64*)(base + tls.VirtualAddress);
    for (auto* cb = (PIMAGE_TLS_CALLBACK*)dir->AddressOfCallBacks; cb && *cb; ++cb) (*cb)((PVOID)base, DLL_PROCESS_ATTACH, nullptr);
  }
  if (nt->OptionalHeader.AddressOfEntryPoint && !((Init)(base + nt->OptionalHeader.AddressOfEntryPoint))((HINSTANCE)base, DLL_PROCESS_ATTACH, nullptr)) {
    *error = "its initialisation failed";
    return nullptr;
  }
  return (HMODULE)base;
}

// GetProcAddress for an image the loader never saw (map_game_image).
static FARPROC image_export(HMODULE module, const char* name) {
  auto* base = (const uint8_t*)module;
  const auto* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
  const auto& ed = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (!ed.Size) return nullptr;
  const auto* dir = (const IMAGE_EXPORT_DIRECTORY*)(base + ed.VirtualAddress);
  const auto* names = (const DWORD*)(base + dir->AddressOfNames);
  const auto* ordinals = (const WORD*)(base + dir->AddressOfNameOrdinals);
  const auto* functions = (const DWORD*)(base + dir->AddressOfFunctions);
  for (DWORD i = 0; i < dir->NumberOfNames; ++i)
    if (std::strcmp((const char*)(base + names[i]), name) == 0) return (FARPROC)(base + functions[ordinals[i]]);
  return nullptr;
}

#endif  // _WIN32: AppContainer and PE image loading

bool reserve_memory() {
  // First, before anything else in the process can take the range: the game's 32-bit disc pointers
  // expect its memory where the console had it.
#ifdef __APPLE__
  std::string error;
  void* mem1 = mac_elf::activate_mem1(&error) ? (void*)MEM1_BASE : nullptr;
  if (!mem1) host::log("Mac memory: %s", error.c_str());
#else
  void* mem1 = VirtualAlloc((void*)MEM1_BASE, MEM1_SIZE, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
#endif
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
  if (!host::aram) host::aram = (uint8_t*)std::calloc(MU_HOST_ARAM_BYTES, 1);
  if (!host::cpu) host::cpu = new ppc::Context();   // only its timebase is used: the clock both builds share
  ax::set_memory({ax_native_rd16, ax_native_rd32, ax_native_wr16, ax_native_wr32,
                  host::aram, MU_HOST_ARAM_BYTES});
  ax::set_voice_trace(std::getenv("MELEE_TRACE_AX_SFX") ? trace_ax_voice
                                                        : nullptr);
  ax::set_frame_trace(std::getenv("MELEE_TRACE_AX_VOICES") ? trace_ax_frame : nullptr);
  audio_core::reset(g_audio);
  slippi::jukebox::set_disc_reader(jukebox_disc_read);
  mods::status().source_port = true;
  // Tests only: 20XX TE's features without mounting its save (its menu memory changes scripted runs).
  if (yougame::mode_policy::mod_sources(yougame::active()).folders && host::test_env::getenv("MELEE_TEST_TE_OWNED"))
    mods::status().te_owned = true;
  if (!read_fst()) host::die("this disc image has no readable file table. Use a clean, uncompressed Melee NTSC 1.02 ISO (a trimmed or compressed image will not work)");
#if MU_SQUAD && !defined(MELEE_NO_SLIPPI)
  slippi::online::set_squad_standard_copy(squad_standard_copy);
#endif
  // A readable FST and retail DOL do not prove that the disc's assets exist. A damaged image can
  // have zero-filled file extents, which otherwise reach lbArchive_InitializeDAT during boot.
  // Check the first required archive on the disc before building menus or entering the game DLL.
  {
    const auto rumble = g_paths.find("/lbrb.dat");
    if (rumble == g_paths.end() || g_fst[rumble->second].dir)
      host::die("The game ISO is missing LbRb.dat. Select a fresh, uncompressed Melee NTSC 1.02 ISO.");
    const auto& file = g_fst[rumble->second];
    uint8_t header[0x20]{};
    std::string error;
    if (!host::disc_read(file.offset, header, sizeof header))
      host::die("The game ISO cannot read LbRb.dat. Select a fresh, uncompressed Melee NTSC 1.02 ISO.");
    if (!host::disc_archive_header(header, sizeof header, file.length, &error))
      host::die("The game ISO contains damaged LbRb.dat: %s. Select a fresh, uncompressed Melee NTSC 1.02 ISO; rebuild a modified ISO from a working base image.", error.c_str());
  }
  // A game library built with the native Akaneia fighters exports this name; one built without them
  // does not, and an Akaneia disc stays refused. Looked up without running any of its code.
  {
    const char* named = yougame::mode_policy::mod_sources(yougame::active()).folders
                            ? std::getenv("MELEE_GAME_DLL") : nullptr;
#ifndef _WIN32
    // The library cannot be loaded twice at its fixed address, so its exported names are read from
    // the file: the symbol name sits in the dynamic string table only when it is exported.
    {
      std::filesystem::path library = named ? named : g_dll;
#ifndef __APPLE__
      // Read the file the loader will map: LoadLibraryA (port/linux/compat) takes a bare name from
      // beside the program first, while the app's Linux jail may start the game in its data folder.
      if (library.u8string().find('/') == std::string::npos) {
        std::error_code ec;
        const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec && std::filesystem::exists(self.parent_path() / library, ec)) library = self.parent_path() / library;
      }
#endif
      std::ifstream file(library, std::ios::binary);
      if (!file) host::log("mods: cannot read %s to check for native Akaneia fighters", library.u8string().c_str());
      const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      static const char kName[] = "mu_ak_native_build";
      if (bytes.find(std::string(kName, sizeof kName)) != std::string::npos) {
        allow_native_akaneia(true);
        host::log("mods: game library with native Akaneia fighters; Akaneia discs are accepted");
      }
    }
#else
    if (HMODULE probe = LoadLibraryExA(named ? named : g_dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES)) {
      const bool native_akaneia = GetProcAddress(probe, "mu_ak_native_build") != nullptr;
      FreeLibrary(probe);
      if (native_akaneia) {
        allow_native_akaneia(true);
        host::log("mods: game library with native Akaneia fighters; Akaneia discs are accepted");
      }
    }
#endif
  }
  load_mod_overlay();
  index_music_paths();
  slippi::jukebox::set_music_pack_reader(jukebox_music_pack_read);
  check_replay_content();
  load_cosmetics(g_replay || g_online_test_mode >= 0);
  {
    std::vector<gx::texpack::CosmeticCompanion> companions;
    for (const auto& item : host::cosmetics::active_companions())
      companions.push_back({item.kind, item.target_path, item.path});
    gx::texpack::set_cosmetic_companions(std::move(companions));
  }
  // rogueMelee is played offline on its own disc, where Slippi Dolphin applies none of Slippi's codes
  // (its disc id is not GALE01): its menu files carry the Rogue Mode label, and Slippi's would hide it.
  load_system_files(g_replay ? "--replay"
                    : g_online_test_mode >= 0 ? "--online-test"
                    : host::options.vanilla_game ? "--vanilla-game"
                    : mods::status().rogue ? "rogueMelee disc, played offline with its own menus" : nullptr);
  if (g_resume_online)
    host::log(g_slippi_menus ? "resume: online (existing boot-to-Online menu; no search)"
                            : "resume: online ignored (Online boot menu unavailable)");
  apply_content_mode(-1);   // offline: the mod's files, if a profile is loaded
#ifndef MELEE_NO_SLIPPI
  // A YouGame start boots on the main menu unless the app asked for Online (build 103).
  if (g_resume_online) slippi::online::request_boot_online();
  slippi::online::load_ranked_rules();
#endif
  // The native game reads the retail DOL's data and the retail file formats; a modded disc (patched
  // DOL, m-ex) is refused here with a message instead of failing somewhere inside the game.
  if (!host::disc_has_vanilla_dol()) {
    yougame::notify_mod_refused(!g_explicit_mod_layers.empty(), yougame::mode_policy::ModRefusal::ModdedDol);
    // The player's disc, not a crash: said once, no crash report (main.cpp does the same for a disc problem).
    static const char* kModdedDisc =
        "This ISO is a modded Melee disc. The Source Port plays only the retail Super Smash Bros. Melee NTSC 1.02 disc.\n\n"
        "Choose your retail ISO in the launcher, or put the modded disc in the Mods folder and play it from launcher > Mods.";
    host::log("ISO DOL does not match vanilla Melee NTSC 1.02; the Source engine runs only the retail game (modded discs are not supported)");
    std::fprintf(stderr, "%s\n", kModdedDisc);
    host::log_flush();
#ifdef _WIN32
    if (!host::options.no_gc_adapter) MessageBoxA(nullptr, kModdedDisc, "Melee Unlocked", MB_ICONINFORMATION | MB_OK);
    ExitProcess(2);
#else
    std::_Exit(2);
#endif
  }
  // Diagnostic builds of the game (for example the M0 maths audit) without replacing the shipped DLL.
  if (yougame::mode_policy::mod_sources(yougame::active()).folders)
    if (const char* dll = std::getenv("MELEE_GAME_DLL")) g_dll = dll;
#ifndef MELEE_NO_SLIPPI
  // Offline VS matches are recorded and saved as .slp like online ones, as Slippi Dolphin saves every game it
  // records (carboncitylights 11-24: "replays from vs mode aren't appearing in the replays list"); the
  // spectator server broadcasts them as before. Set before the game library loads, so its C runtime sees it.
  // Not with the experimental tick re-run (off unless MELEE_TICK_REDO=1): it never runs while a match is
  // recorded, so it would never run.
  if (!std::getenv("MELEE_SOURCE_RECORD") && !host::tick_redo_enabled()) {
    _putenv_s("MELEE_SOURCE_RECORD", "1");
    SetEnvironmentVariableA("MELEE_SOURCE_RECORD", "1");
  }
#endif
  ghost_ai::enable();   // the pro AI needs the game library's fighters: Source Port only
  practice_cpu::initialize_source_test_ghost();
#ifdef __APPLE__
  std::string mac_error;
  if (!g_mac_image.load(g_dll, &mac_error)) host::die("%s", mac_error.c_str());
  HMODULE module = (HMODULE)GAME_IMAGE_BASE;
  host::game_image = (uint8_t*)GAME_IMAGE_BASE;
  host::game_image_size = g_mac_image.size();
  auto game_proc = [&](const char* name) { return g_mac_image.symbol(name); };
#else
#ifndef _WIN32
  g_game_image_reservation.release();
  HMODULE module = LoadLibraryA(g_dll.c_str());
  if (!module) host::die("cannot load %s (%s)", g_dll.c_str(), dlerror());
  // The ELF's load segments: the game must start at GAME_IMAGE_BASE, its 32-bit disc pointers and
  // GX's physical window depend on it (sourceport/cmake/game-linux-layout.ld places it there).
  {
    struct Scan { uint64_t lo = ~0ull, hi = 0; } scan;
    dl_iterate_phdr([](dl_phdr_info* info, size_t, void* user) {
      auto& sc = *static_cast<Scan*>(user);
      bool ours = false;
      for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i)
        if (info->dlpi_phdr[i].p_type == PT_LOAD && info->dlpi_addr + info->dlpi_phdr[i].p_vaddr == GAME_IMAGE_BASE) ours = true;
      if (!ours) return 0;
      for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        const auto& p = info->dlpi_phdr[i];
        if (p.p_type != PT_LOAD) continue;
        sc.lo = std::min<uint64_t>(sc.lo, info->dlpi_addr + p.p_vaddr);
        sc.hi = std::max<uint64_t>(sc.hi, info->dlpi_addr + p.p_vaddr + p.p_memsz);
      }
      return 1;
    }, &scan);
    if (scan.lo != GAME_IMAGE_BASE) host::die("%s did not load at %llX; it must be the Linux build made with the fixed-address linker script", g_dll.c_str(), (unsigned long long)GAME_IMAGE_BASE);
    host::game_image = (uint8_t*)GAME_IMAGE_BASE;
    host::game_image_size = (uint32_t)(scan.hi - scan.lo);
  }
#else
  // In the YouGame sandbox (an AppContainer) the host maps the image itself into the range held
  // since startup (g_game_image_reservation) and keeps the hold; elsewhere the loader needs it free.
  const bool sandboxed = in_app_container();
  // MELEE_TEST_RELOCATED_GAME_IMAGE=1 (tests): the range stays held, so Windows places the library
  // elsewhere, as a relocation policy does, and the mapping below is what loads it.
  const bool test_relocated = host::test_env::getenv("MELEE_TEST_RELOCATED_GAME_IMAGE") != nullptr;
  if (!sandboxed && !test_relocated) g_game_image_reservation.release();
  std::string map_error;
  HMODULE module = sandboxed ? map_game_image(g_dll, &map_error) : LoadLibraryA(g_dll.c_str());
  if (!module && sandboxed) host::die("cannot map %s: %s", g_dll.c_str(), map_error.c_str());
  if (!module) {
    const DWORD error = GetLastError();
    // 4551 (ERROR_SYSTEM_INTEGRITY_POLICY_VIOLATION): Windows' app control (Smart App Control on Windows 11)
    // refused the game library itself (two 1.0.22 reports, 11-24). Say what it is and where it is decided.
    if (error == 4551)
      host::die("Windows blocked %s (Smart App Control or an app control policy, error 4551), so the game cannot "
                "start. Windows Security > App & browser control > Smart App Control decides this; on a managed "
                "PC your administrator does.", g_dll.c_str());
    host::die("cannot load %s (error %lu)", g_dll.c_str(), error);
  }
  if (sandboxed) host::log("source port: %s mapped at its base by the host (sandboxed)", g_dll.c_str());
  // Windows can put the library somewhere else although its range is free: Exploit protection's
  // "Force randomization for images" (Mandatory ASLR) relocates every image, and it landed at a new
  // low address each start (1.0.2 reports: 0x70000, 0xE0000, 0x580000). Then the host maps it at its
  // base itself, as it does in the YouGame sandbox, in the range taken back for it.
  bool mapped = sandboxed;
  if (!sandboxed && (uintptr_t)module != GAME_IMAGE_BASE) {
    host::log("source port: Windows loaded %s at %p, not at its base (a relocation policy); mapping it there instead",
              g_dll.c_str(), (void*)module);
    FreeLibrary(module);
    if (!g_game_image_reservation.base)
      g_game_image_reservation.base = VirtualAlloc((void*)GAME_IMAGE_BASE, 0x04000000u, MEM_RESERVE, PAGE_NOACCESS);
    module = g_game_image_reservation.base ? map_game_image(g_dll, &map_error) : nullptr;
    if (!module)
      host::die("%s could not be placed at %llX (%s)", g_dll.c_str(), (unsigned long long)GAME_IMAGE_BASE,
                g_game_image_reservation.base ? map_error.c_str() : "its address range is in use");
    mapped = true;
    host::log("source port: %s mapped at its base by the host", g_dll.c_str());
  }
  if ((uintptr_t)module != GAME_IMAGE_BASE) host::die("%s loaded at %p, not at %llX", g_dll.c_str(), (void*)module, (unsigned long long)GAME_IMAGE_BASE);
  const auto* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)module + ((const IMAGE_DOS_HEADER*)module)->e_lfanew);
  host::game_image = (uint8_t*)module;
  host::game_image_size = nt->OptionalHeader.SizeOfImage;
#endif
#ifdef _WIN32
  auto game_proc = [&](const char* name) { return mapped ? image_export(module, name) : GetProcAddress(module, name); };
#else
  auto game_proc = [&](const char* name) { return GetProcAddress(module, name); };
#endif
#endif
  auto entry = (MuGameEntry)game_proc("mu_game_entry");
  g_handoff_callbacks_pending = (unsigned (*)())game_proc("mu_handoff_callbacks_pending_v1");
  g_cpu_pads = (int (*)(int[4][8]))game_proc("mu_cpu_pads_v1");
#ifdef __APPLE__
  g_loaded_game_path = g_dll; // The custom ELF mapper owns this exact resources path.
#else
#ifdef _WIN32
  char loaded_path[MAX_PATH]{};
  if (GetModuleFileNameA(module,loaded_path,MAX_PATH)) g_loaded_game_path = loaded_path;
  // The sandbox's custom mapper is not in the loader list, and the working directory is the data
  // folder: the handoff identity and the snapexcl must read the file the mapper read.
  else g_loaded_game_path = game_library_file(g_dll);
#else
  Dl_info game_info{};
  if (entry && dladdr(reinterpret_cast<void*>(entry), &game_info) && game_info.dli_fname)
    g_loaded_game_path = game_info.dli_fname;
#endif
#endif
  g_update_ranked_busy = (int (*)())game_proc("mu_update_ranked_busy_v1");
  g_look_snapshot = (void (*)(MuLookSnapshot*))game_proc("mu_look_snapshot_v1");
  g_look_items = (void (*)(MuLookItems*))game_proc("mu_look_items_v1");
  g_look_extras = (void (*)(MuLookExtras*))game_proc("mu_look_extras_v1");
  user_gecko::set_native_writer((user_gecko::NativeWrite)game_proc("mu_user_gecko_write"));
  user_gecko::set_native_reader((user_gecko::NativeRead)game_proc("mu_user_gecko_read"));
  user_gecko::set_agreed_writer((user_gecko::NativeWrite)game_proc("mu_user_gecko_write_agreed"),
                                (user_gecko::NativeRead)game_proc("mu_user_gecko_read_agreed"));
  {
    // Functions a code may make return at once (shim/mu_gecko_functions.c); an older library has none.
    using Functions = uint32_t (*)(const user_gecko::NativeFunction**);
    const auto functions = (Functions)game_proc("mu_user_gecko_functions");
    const user_gecko::NativeFunction* table = nullptr;
    const uint32_t count = functions ? functions(&table) : 0;
    user_gecko::set_native_functions(table, count, host::dol_data_read);
  }
  if (!entry) host::die("%s has no mu_game_entry", g_dll.c_str());
  static MuHostApi api = make_host();
  // The game library and this program come from the same release and refuse each other otherwise.
  // A mismatch is a partly updated install (one report: a 0.8.95 program with an older library
  // left in place), so the stop says what to do; the start of the line is kept for grouping.
  if ((yougame::console::friends_handoff_join_enabled()
#ifdef MELEE_DOUBLES
       || true
#endif
      ) && !g_console_boot && !g_replaying &&
      g_online_test_mode < 0 && !source_p2p::requested() && slippi::online::session_mode() < 0) {
    g_css_origin.card = console_host_save(); css_origin_arm();
  }
  if (entry(&api, &g_game) != 0)
    host::die("%s refused host API version %u: it is from another version of Melee Unlocked than %s. "
              "Close the game and the launcher, then extract the whole release zip over the folder again", g_dll.c_str(), api.version, MELEE_PORT_VERSION);
  if (g_game.version != MU_GAME_API_VERSION || !g_game.state_snapshot || !g_game.state_regions ||
      !g_game.state_exclusions || !g_game.snapshot_ranges || !g_game.practice || !g_game.css_state)
    host::die("%s has game API version %u, expected %u: it is from another version of Melee Unlocked than %s. "
              "Close the game and the launcher, then extract the whole release zip over the folder again", g_dll.c_str(), g_game.version, MU_GAME_API_VERSION, MELEE_PORT_VERSION);
  {
    // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    // Squad Strike is in both the program and the library or in neither, at the same version.
    auto squad_build = (int (*)())game_proc("mu_squad_build");
#if MU_SQUAD
    if (!squad_build || squad_build() != MU_SQUAD_VERSION)
#else
    if (squad_build)
#endif
      host::die("%s and %s disagree on Squad Strike: it is from another build of Melee Unlocked. "
                "Close the game and the launcher, then extract the whole release zip over the folder again", g_dll.c_str(), MELEE_PORT_VERSION);
  }
  slippi::native_practice::set_native_bridge(console_practice_bridge);
  slippi::native_practice::set_content_bridge(prepare_practice_content);
  slippi::online::set_plain_opponent_handler(plain_opponent);
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
#ifdef YOUGAME_AGENT_TEST_HOOK
  const char* origin_test = std::getenv("YOUGAME_TEST_CSS_ORIGIN");
  g_css_origin.proof = origin_test && std::strcmp(origin_test,"1") == 0;
  const char* state_test = std::getenv("YOUGAME_TEST_CSS_STATE");
  g_css_origin.state_proof = state_test && std::strcmp(state_test,"1") == 0;
  g_css_origin.proof |= g_css_origin.state_proof;
  if (g_css_origin.proof && (g_console_boot || g_replaying || g_online_test_mode >= 0 ||
      source_p2p::requested() || slippi::online::session_mode() >= 0))
    host::die("css-origin: proof requires an offline launch");
  if (g_css_origin.proof) { css_origin_arm(); host::audio_set_muted(true); }
#endif
  host::native_retrace = native_retrace;
  publish_handoff_join_info(game_regions,region_count,exclusions,&api);
#ifdef YOUGAME_AGENT_TEST_HOOK
  handoff_start(game_regions, region_count, exclusions, &api);
#endif
  host::native_state_snapshot = g_game.state_snapshot;
#ifdef _WIN32
  AddVectoredExceptionHandler(1, on_game_exception);
#else
  install_game_signal_handler();
#endif
  gx::set_native_owner_source(true);
  // The L-cancel helper reads the native game's fighters through the game, not guest memory.
  lcancel::set_native_view([](MuLcancelView* out) { if (g_game.lcancel_view) g_game.lcancel_view(out); });
  host::log("source port: %s at %p, MEM1 %u MB at %08llX", g_dll.c_str(), (void*)module, MEM1_SIZE >> 20, (unsigned long long)MEM1_BASE);
  int code = 0;
#ifndef MELEE_NO_SLIPPI
  slippi::spectate::start();   // Slippi Dolphin's spectator server (settings: Spectator server)
  slippi::arena::set_recording_codes(std::vector<uint8_t>(std::begin(native_online_recording_codes), std::end(native_online_recording_codes)));
  slippi::arena::set_viewer_launcher(launch_arena_viewer);
  // Tests only (hidden runs): MELEE_TEST_ARENA_VIEWER=<live .slp> opens the room's viewer on it at start,
  // so the viewer launch is proven on each platform without a room (Linux had a stub, report 11-24).
  if (const char* v = host::test_env::getenv("MELEE_TEST_ARENA_VIEWER"); v && *v) {
    void* viewer = launch_arena_viewer(v);
    host::log("arena test: viewer launch on %s %s", v, viewer ? "started" : "failed");
  }
  slippi::arena::set_friends_exit([] { slippi::native_practice::submit_arena_from_friends(); });
#endif
#ifdef __APPLE__
  g_mac_running = true;
  if (setjmp(g_mac_run_boundary) == 0) guarded([&] { code = g_game.run(); });
  else code = g_exit_code;
  g_mac_running = false;
#else
  guarded([&] { code = g_game.run(); });
#endif
  // An orderly window close before Game End is a quit, sampled by the game before unloading.
  finish_tournament_before_exit();
  rollback_log(true);
  log_native_pose_bridge_stats();
  gx::set_native_pose_capture_enabled(false);
  host::log("game returned %d", code);
  return code;
}

}  // namespace source_port

namespace yougame::console {
// Before main: Friends' save read lives in the runtime library, built without MELEE_SOURCE_PORT.
[[maybe_unused]] static const bool host_save_installed =
    (host_save_reader.store(&source_port::console_host_save, std::memory_order_release), true);
bool console_begin(std::unique_ptr<SessionLink> link, const SessionConfig& config) {
  return source_port::begin_console(std::move(link), config);
}
} // namespace yougame::console
