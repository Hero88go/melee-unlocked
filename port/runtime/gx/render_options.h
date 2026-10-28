// Render options shared by every backend, the presentation thread and the settings panel.
// Nothing here belongs to one graphics API: a backend reads what applies to it and ignores the rest.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../host/gameplay_policy.h"
#include <atomic>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#ifdef GX_DLSS5
#include "gx_dlss5.h"
#endif

namespace gx {

// Authored = predict ahead from the latest game frame (no delay); AuthoredInterpolate = exact
// in-betweens of the last two game frames (one frame of display delay, no overshoot).
enum class SubFrameMode { Off, Extrapolate, Interpolate, Authored, AuthoredInterpolate };

// Which graphics API renders the game. D3D12 is the default and the only one with DLSS; D3D11 is
// for machines whose driver cannot start D3D12. Switching takes effect at the next launch.
enum class RenderApi { D3D12, D3D11 };

// How the 640x480 image is fitted to the window. Presentation only: it changes nothing the game
// computes, so it cannot desync and two players in one match may pick different values.
//   Auto    73:60 normally, 16:9 with the Slippi widescreen code on (see presented_aspect).
//   Native  73:60, the aspect Melee's own camera asks for (Slippi Dolphin: "Force 73:60 (Melee)").
//   Stretch fills the window edge to edge with no bars, the "stretched res" some players prefer.
enum class AspectMode { Auto = 0, Native = 1, Force4_3 = 2, Force16_9 = 3, Stretch = 4 };
// True widescreen's target shape (saved as "truewsaspect" 0..3).
enum class TrueAspect { Wide16_9 = 0, Wide16_10 = 1, Custom = 2, FitWindow = 3 };
// The widest and narrowest shapes True widescreen widens to. Wider windows get side bars. 21:9
// (2560x1080) was the widest clean shape in hidden captures of the menus and 11 stages; at 32:9 the
// character select backdrop and several stage backgrounds end before the screen edge.
constexpr float kTrueAspectMax = 64.0f / 27.0f;
constexpr float kTrueAspectMin = 4.0f / 3.0f;

struct RenderOptions {
  // The decompiled Source Port shares the host UI and renderers with Legacy, but it does not run
  // the translated guest's Slippi/Gecko patch hooks. Keep that distinction explicit in the UI and
  // in presentation defaults without changing the user's saved Legacy settings.
  bool native_source = false;
  RenderApi api = RenderApi::D3D12;   // --backend d3d11|d3d12, or "backend" in port-settings.ini
  int audio_mode = 0;              // 0 Auto (adaptive, remembered per device), 1 Low latency (fixed), 2 Exclusive, 3 ASIO
  std::string audio_asio_driver;   // ASIO driver name (HKLM\SOFTWARE\ASIO); used in mode 3 only
  int audio_asio_buffer = 0;       // ASIO buffer in frames; 0 = the driver's preferred size
  std::string audio_device;        // output endpoint id; empty = Windows default
  int audio_buffer_ms = 40;        // software output buffer; independent of the game mixer
  // Presentation timeline (threaded renderer only). fps_cap 0 = uncapped, which the presentation
  // loop holds at twice the display's refresh rate (MELEE_PRESENT_UNCAPPED=1 removes that limit for
  // tests). With a SubFrameMode other than Off the renderer presents new sub-frames between 60 Hz
  // simulation frames.
  double fps_cap = 60; // -1 follows the active monitor
  bool fullscreen = false;
  bool exclusive_fullscreen = false;
  // Off by default: Slippi's Lagless FoD, as the Source Port always runs it. A player's report found
  // the reflection pass costly on the Static Recomp (0.8.99), and Slippi players expect the code on.
  bool fod_reflections = false;
  // The character select background plays its animation through instead of starting over every 200
  // frames (fn_80263354). Menu visuals only, so it is the viewer's own in every mode.
  bool css_background_free = false;
  // DLSS Frame Generation and NVIDIA Reflex are presentation-only controls. They are ignored when
  // the selected backend or hardware cannot provide them.
  int frame_generation_mode = 0;
  // What the driver last reported for frame generation (inserted frames, 0 = never queried), kept
  // so the settings can offer the right modes in a session where the plugin is not loaded.
  int fg_cached_max = 0;
  bool fg_cached_dynamic = false;
  int reflex_mode = 0;
  bool reflex_stats = false;
  bool reflex_flash = false;
  bool latency_test = false;
  bool lossless_lag_test = false;   // the Lossless adapter's lag test at the next launch (main.cpp, shim/mu_lagtest.c)   // gx_core.h set_latency_test: X + D-pad Down paints the picture black or white
  int dlss_mode = 0;              // gx::DlssMode: 0 native, 1 DLAA, 2 quality, 3 balanced, 4 performance, 5 ultra performance
#ifdef GX_DLSS5
  bool dlss5 = false;
  dlss5::Tuning dlss5_tuning;
#endif
  float dlss_jitter_sign = -1.0f; // calibrated 2026-09-11: -1 reconstructs sharp text, +1 blurs (see PORT_COMPLETION.md)
  bool pc_settings = false, settings_open = false, performance_overlay = false;
  bool show_vram = false;
  bool show_fps = false, show_ping = true;
  // The "Settings: F1" reminder in the corner. On for a new player, off for anyone who knows the
  // key and does not want it in a recording.
  bool settings_hint = false;   // the corner reminder is off by default; Overlays can turn it on
  bool legacy_menu_enabled = false; // F11 and launcher Settings use the selected legacy presentation
  int legacy_menu_style = 7; // 7: Legacy Old (v0.6.6), 6: Legacy New
  bool settings_menu_sounds = false;
  bool settings_custom_color_enabled = false;
  std::array<float, 3> settings_custom_color{0.96f, 0.15f, 0.52f};
  int overlay_style = 0;          // 0: Clean side, 1: Icon tiles, 2: GD Melee, 3: Radial, 4: Wide tabs, 5: Simple, 6: Classic (legacy)
  std::array<int, 7> overlay_palettes{}; // independent palette (0 classic, 1-3 alternate) for each appearance
  int settings_transparency = 0;  // percent of the settings layer's alpha removed, 0..65
  int stock_hud_scale = 100;       // percent of the original stock HUD root scale
  int damage_hud_scale = 100;      // percent of the original damage HUD root scale
  bool show_player_nicknames = false; // screen-space names anchored to Melee's player tags
  // The small "Matchmaking: Tab" reminder. Matchmaking remains available when it is hidden.
  bool matchmaking_hint = true;
  // The Practice menu's look in Training (practice_menu.h: 0 Main Menu bars, 1 side rails, 2 slots,
  // 3 strip, 4 the full window).
  int practice_style = 0;
  bool input_overlay = false;     // on-screen controller display, for streaming
  // Lab view: draw matches as Slippi Lab draws replays (flat silhouettes on a plain stage) over
  // the game image. Display only, so it is safe online. F3 toggles it. See lab_view.h.
  bool lab_view = false;
  // While the Lab view covers the window, skip replaying the game's own draws: the scene would be
  // drawn only to be painted over. Saves the GPU (and draw submission) its whole frame in a match.
  bool lab_skip_scene = true;
  int input_overlay_ports = 1;    // bitmask of the controller ports it shows (bit 0 = port 1)
  bool input_overlay_values = false;
  int input_overlay_stick = 5;
  bool input_overlay_hide_border = false;  // draw the pads with no panel background or resize grip
  // Drop in-world translucent effects to buy frame rate on weak machines: 0 everything, 1 skips
  // effects that do not write depth (sparks, glow, smoke), 2 skips translucent world geometry too.
  // Purely presentational, so unlike a Gecko code it cannot desync and both players may differ.
  int effects_level = 0;
  // "Low poly fighters": 0 the game decides, 1 the fighter draw always shows the game's own far
  // (low-polygon) model, the one it draws in reflections, shadows and the magnifier bubble. Display
  // only: it changes DObj hidden flags and nothing the simulation or a replay reads, so it is safe
  // online and each player may differ. Both engines (ftdrawcommon.c; host.cpp parts_show_low_poly).
  int low_poly_fighters = 0;
  // "Lightweight" 0 to 5 (gx_core.h, kLightweightLevels): the one ladder of savings shown in PC
  // settings. Level 4 and up hold the Low spec values below (low_spec, low_spec_previous), so
  // stepping back under 4 puts the player's own image settings back.
  int lightweight = 0;
  // "Retro (64 style)": textures shrunk to the Nintendo 64's 64 x 64 and the game's low poly fighters (gx_retro.h).
  // Display only. Independent of the Lightweight ladder; the two combine.
  bool retro = false;
  // The look (gx_retro.h): how fighters, the stage and the menus are textured (gx::Surface: 0 textured, 1 retro 64,
  // 2 flat colour, 3 white, 4 posterised, 5 wire), nearest-texel sampling, fighter colours (0 original, 1 port
  // colours), the background (0 the stage's own, 1 black, 2 plain colour). Display only. Presets set them together.
  int look_fighter = 0, look_stage = 0, look_menus = 0;
  bool look_unfiltered = false;
  // Our own renderer version of a look even when the exact look's pack is built (the "64 Style" preset).
  bool look_own = false;
  int look_colours = 0, look_background = 0;
  uint32_t look_background_rgb = 0x2A2A40;
  // Lab view looks (lab_view.h set_style): fighters 0 silhouettes, 1 stick figures, 2 capsules; stage 0 as
  // drawn, 1 outline, 2 blocks. Used while lab_view is on.
  int look_lab_fighters = 0, look_lab_stage = 0;
  // "Freeze Pokemon Stadium (offline only)" (Game tab, Stages): it changes the match, so never online.
  // Slippi's Frozen Pokemon Stadium, on the Source Port (MU_GAME_OPTION2_STADIUM_FROZEN); a replay
  // records it as its isFrozenPS, so it plays back frozen here and in Slippi's player.
  bool stadium_frozen = false;
  static bool& live_stadium_frozen() { static bool value = false; return value; }
  // The P1 / P2 markers over the fighters stay up all match (the game shows them only at the start,
  // off screen, or with a name tag). Display only, so it is each player's own online. Both engines
  // (if/ifnametag.c; host.cpp apply_player_tags_always).
  int player_tags_always = 0;
  // "Low spec": one switch that puts every setting which costs frames at its cheapest, for
  // integrated graphics and older laptops. Turning it off must give the player their own settings
  // back rather than a hardcoded default, so what they had is kept here while it is on. Both the
  // switch and the kept values are saved in port-settings.ini, so the state survives a restart.
  struct LowSpecPrevious {
    RenderApi api = RenderApi::D3D12;
    double fps_cap = 60;
    int efb_scale = 0, ssaa = 1, anisotropy = 16, effects_level = 0, dlss_mode = 0;
    SubFrameMode subframe = SubFrameMode::AuthoredInterpolate;
  };
  bool low_spec = false;
  LowSpecPrevious low_spec_previous;
  // Discord Rich Presence, off by default: it tells the player's Discord friends what they are
  // playing. The application id is Melee Unlocked's own, registered once for the whole game rather
  // than per player, and it is not a secret (Rich Presence needs no token). A player can override it
  // in the settings to point the presence at an application of their own.
  bool discord_presence = false;
  std::string discord_app_id = "1549608280949792790";
  // Source Port mod profile (Mods/Profiles/<name>.ini) loaded at start; empty = the retail game.
  // Command-line --mod-* flags take precedence. The static recomp ignores it.
  std::string mod_profile;
  // Packs found in the Mods folder (mod_scan.h): the player's on/off choice per pack, by key ("te",
  // "tmce", or a content key). A pack with no entry has not been decided: it is turned on the first
  // time it is found. Saved as mod_te_enabled, mod_tmce_enabled and "mod_enabled <key> <0|1>".
  std::map<std::string, int> mod_choices;
  static uint32_t& live_te_options() { static uint32_t value = 0x10u; return value; }
  // Source Port mod profiles and offline 20XX TE options are available under Mods in 0.8.5.
  static constexpr bool kModFeaturesAvailable = true;
  static bool mod_features_available() { return kModFeaturesAvailable && !host::gameplay::vanilla_only(); }
  // DXR path tracing and Ray Reconstruction: not offered in 0.8. They rebuild the whole stage's
  // ray tracing geometry every frame, which dropped the picture to a few frames per second in
  // matches. Hidden and forced off (saved values ignored) until that is reworked.
  static constexpr bool kPathTracingAvailable = false;
  // Content views (source_host.cpp): with a mod profile, Direct uses the mod when this is on (the
  // default), the retail game otherwise; Unranked, Teams and Party always use the retail game.
  bool mods_in_direct = true;
  static bool& live_mods_in_direct() { static bool value = true; return value; }
  // For this session only: a Direct opponent who sends no build (Slippi Dolphin) is accepted as
  // being on the same mod, at the player's word. Never saved.
  static bool& live_mods_dolphin_ok() { static bool value = false; return value; }
  // Source Port 20XX Tournament Edition features (MU_GAME_OPTION_TE_* bits); offline only.
  uint32_t te_options = 0x10u;      // enables TE when its recognized save is loaded; an explicit 0 disables it
  // The second 20XX TE word (MU_GAME_OPTION2_TE_*): the features added with host API 14.
  uint32_t te_options2 = 0;
  static uint32_t& live_te_options2() { static uint32_t value = 0; return value; }
  // "20XX CPUs" (Game tab, ini key cpu_20xx): CPUs play with the 20XX Hack Pack's AI, offline only.
  // One plain option on both engines, not a 20XX TE feature and no TE save needed: the Source Port
  // plays its native version (shim/mu_20xx_ai.c, carried to the game as MU_GAME_OPTION2_TE_20XX_CPUS),
  // the Static Recomp runs the pack's own AI block from its disc under Mods (host/hackpack_ai.cpp).
  bool cpu_20xx = false;
  static bool& live_cpu_20xx() { static bool value = false; return value; }
  // "Freeze Final Destination" (Game tab, ini key freeze_fd, both engines): the background stays on
  // its first form, with no transformations or white flash. The player's own, online too.
  bool freeze_fd = false;
  static bool& live_freeze_fd() { static bool value = false; return value; }
  // "Unlock everything" (Game tab, ini key unlock_all, on unless the file says 0): Slippi's "Unlock
  // All Characters and Stages". Off, the save file decides what is unlocked, offline only: a network
  // session always has everything. Static Recomp: gecko::option_unlock_all (host.cpp); Source Port:
  // MU_GAME_OPTION3_LOCKED_CONTENT (shim/mu_gecko.c mu_unlock_all).
  bool unlock_all = true;
  static bool& live_unlock_all() { static bool value = true; return value; }
  // "Show results screen" (Game tab, ini key results_screen, on unless the file says 0). Off, an
  // offline match goes straight back to character select, as online play does. Static Recomp:
  // gecko::option_offline_results (host.cpp); Source Port: MU_GAME_OPTION3_SKIP_RESULTS.
  bool results_screen = true;
  static bool& live_results_screen() { static bool value = true; return value; }
  // Faster sound effects (Audio): each new sound starts in the sound frame mixed now, 5 ms sooner.
  // Offline only (the host gives the game 0 online and in replays).
  static bool& live_audio_early_keyon() { static bool value = false; return value; }
  bool offline_delay = false;
  // Early inputs (Online, ini key early_inputs, on unless the file says 0): controller changes go
  // to the other player the moment they happen (host/early_input.h). Applied with
  // host::early_input::set_enabled when the file is read and when the toggle changes.
  bool early_inputs = true;
  // Slippi Dolphin's spectator server (Game tab, Online; ini keys slippi_spectator, on unless the
  // file says 0, and slippi_spectator_port, 51441): the game's recording events on a local ENet
  // port for the Slippi Launcher's Broadcast tab and stream overlays (hle/slippi_spectate.cpp).
  // Read once when the game starts, as Dolphin creates its server once.
  bool slippi_spectator = true;
  static bool& live_slippi_spectator() { static bool value = true; return value; }
  // After an online session ends (the opponent left, Z held to leave, the connection dropped) with at
  // least one match played, go to Training mode's character select (Game tab, Online; ini key
  // online_return_training, off by default). native_practice.cpp does it, on both engines.
  bool online_return_training = false;
  static bool& live_online_return_training() { static bool value = false; return value; }
  // After an online session: straight into a Training match, then "search again?" (native_practice.cpp).
  bool online_back_to_training = true;
  static bool& live_online_back_to_training() { static bool value = true; return value; }
  int slippi_spectator_port = 51441;
  static int& live_slippi_spectator_port() { static int value = 51441; return value; }
  static bool& live_offline_delay() { static bool value = false; return value; }
  // 20XX Hack Pack training options for CPUs (Game tab, Source Port only, offline only), native in
  // shim/mu_20xx_cpu.c. Plain options like "20XX CPUs": no pack or save needed. They go to the game
  // packed in option word 3 (MU_GAME_OPTION3_CPU_*, host API 17) and into a replay as "muOptions3".
  // Ini keys: cpu_tech, cpu_getup, cpu_di, cpu_sdi, cpu_notaunt, cpu_lcancel, cpu_nojab,
  // cpu_notransform.
  int cpu_tech = 0;     // 0 off, 1 in place, 2 roll forward, 3 roll back, 4 miss, 5 random
  int cpu_getup = 0;    // 0 off, 1 stand, 2 roll forward, 3 roll back, 4 getup attack, 5 random
  int cpu_di = 0;       // 0 off, 1 none, 2 random, 3 survival
  int cpu_sdi = 0;      // 0 off, 1 none, 2 random, 3 with the hit, 4 against it, 5 up, 6 down
  bool cpu_no_taunt = false;
  bool cpu_lcancel = false;
  bool cpu_no_rapid_jab = false;
  bool cpu_no_transform = false;
  // The packed word (the bit layout of MU_GAME_OPTION3_CPU_* in mu_host.h; out of range values
  // count as off).
  uint32_t cpu_training_word() const {
    const auto field = [](int value, int max, int shift) {
      return value >= 1 && value <= max ? (uint32_t)value << shift : 0u;
    };
    return field(cpu_tech, 5, 0) | field(cpu_getup, 5, 3) | field(cpu_di, 3, 6) | field(cpu_sdi, 6, 8) |
           (cpu_no_taunt ? 0x800u : 0u) | (cpu_lcancel ? 0x1000u : 0u) | (cpu_no_rapid_jab ? 0x2000u : 0u) |
           (cpu_no_transform ? 0x4000u : 0u);
  }
  // What the game is given: set from cpu_training_word() when the settings load and when a row
  // changes (it takes effect on the next frame, like "20XX CPUs").
  static uint32_t& live_cpu_training() { static uint32_t value = 0; return value; }
  // Bumped when the game sets that word from the 20XX Hack Pack's debug menu (host command 0xFA,
  // MU_HP_OP_CPU_WORD); the settings panel then takes the rows from it and saves them.
  static std::atomic<uint32_t>& live_cpu_game_changes() { static std::atomic<uint32_t> value{0}; return value; }
  // Bumped when the game changes both TE words from 20XX TE's in-game settings menu (host command
  // 0xF8); the settings panel then saves them, so the file and the menu agree.
  static std::atomic<uint32_t>& live_te_game_changes() { static std::atomic<uint32_t> value{0}; return value; }
  // 20XX TE menu song chosen in Sound Test (TE's value: 0 none, -1 song 0, else the song). The game
  // sets it through a host command; the settings panel saves it as soon as it changes.
  int te_menu_music = 0;
  static std::atomic<int>& live_te_menu_music() { static std::atomic<int> value{0}; return value; }
  // Gecko codes switched on by the player: Slippi's own switchable codes by id, and codes the
  // player supplied in the GeckoCodes folder. Both default to off, so a list holds what is on.
  // Widescreen is not in here: it has its own setting and its own control under Video.
  std::vector<std::string> gecko_enabled;
  std::vector<std::string> user_gecko_enabled;
  // Watches every presented frame for a single frame that differs from the one before and the one
  // after it while those two agree, which is what a one-frame visual glitch looks like and what no
  // human can catch with a screenshot key. Diagnostic only: it costs a small downsample and readback
  // per presented frame, so it is off unless --flicker-scan asks for it.
  bool flicker_scan = false;
  double pin_phase = -1;
  std::string settings_path = "port-settings.ini";
  std::string frame_times; // optional buffered CSV of CPU presentation timing
  SubFrameMode subframe = SubFrameMode::Off;
  // Online: protect match speed (slippi_online.cpp budget_watch). On by default.
  bool online_frame_protection = true;
  static std::atomic<bool>& live_online_frame_protection() { static std::atomic<bool> value{true}; return value; }
  int efb_scale = 0;          // internal resolution multiplier; 0 = auto (integer scale covering the window, like Dolphin "Auto (Window Size)")
  int window_w = 1280, window_h = 960;  // initial client size
  // The player picked a fixed window size ("window WxH" in the ini, or --window): the panel keeps
  // the window at it. Off = the window is whatever size it has been dragged to.
  bool window_pinned = false;
  AspectMode aspect = AspectMode::Auto;   // --aspect, or "aspect" in port-settings.ini
  bool vsync = false;
  bool widescreen = false;    // Slippi Widescreen 16:9 code on (present at 16:9 and tell the game)
  // EXPERIMENTAL: widen the frustum in the renderer rather than running the Gecko code, so the HUD
  // and the 2D layer keep their authored size and nothing is written to guest memory. Mutually
  // exclusive with `widescreen`: both together would widen twice. See gx_shader.h.
  bool true_widescreen = false;
  // The aspect True widescreen widens to: 16:9, 16:10, the player's own W:H, or the window's shape.
  // Presentation only. "truewsaspect" and "truewscustom W H" in the ini; files without them keep 16:9.
  TrueAspect true_aspect = TrueAspect::Wide16_9;
  int true_custom_w = 21, true_custom_h = 9;
  // Custom texture packs: replace game textures with PNGs from Load/Textures/GALE01, using
  // Dolphin's filenames and hashes so existing packs work unchanged. Off by default, and while it
  // is off no pack directory is opened or scanned. Display only, so it is safe online and the two
  // players may differ. dump_textures writes what the game drew, with the names a pack must use.
  bool custom_textures = false;
  bool dump_textures = false;
  // Looping host-decoded MP4s on the CSS and stage-select backdrop textures. The game still draws
  // the menus and owns their timing; only the pixels sampled by the learned backdrop are replaced.
  bool video_backgrounds = true;
  // Decode every replacement when the game starts rather than the first time each texture appears,
  // as Dolphin's "Prefetch Custom Textures" does. A large pack costs about half a minute once here
  // instead of a stutter each time a new texture comes on screen.
  bool prefetch_textures = true;
  std::string shader_preset;  // .slangp under the Shaders folder run over the picture (user_shader.h); empty = none
  int border_edge = 1;        // border art: 0 = cut off sharp at the picture, 1 = the picture casts a soft shadow on it
  std::string border_art;     // file in the Borders folder shown behind the picture (video_background.h); empty = black bars
  int crt_filter = 0;         // CRT display model in the present pass: 0 off, 1 studio monitor, 2 home television (crt_shader.h)
  float crt_curve = 1.0f;     // the home television's screen curve, 1 as designed, 0 flat to 3 (crt_look)
  float sharpness = 0.0f;     // 0..1 contrast-adaptive sharpening in the present pass (works with or without DLSS)
  float screen_space_ao = 0.0f; // 0..1 depth-based contact shading; display-only, not ray traced
  bool path_tracing = false;       // D3D12 DXR diffuse indirect bounce, display-only and off by default
  bool ray_reconstruction = false; // DLSS-RR denoising of the path-traced HDR input; needs NVIDIA runtime
  float brightness = 1.0f, contrast = 1.0f, vibrance = 1.0f;
  int anisotropy = 4;         // texture anisotropic filtering 1..16 (4: the High preset, the default)
  int ssaa = 1;               // supersampling factor: 1 off, 2 = 4x SSAA (EFB rendered at 2x the chosen scale, box filtered)
  std::string capture_path;   // write a PPM of the presented image at capture_frame
  uint32_t capture_frame = 0;
  uint32_t capture_every = 0;  // if set, capture every N presented frames as <capture_path>_<frame>.ppm
  uint32_t capture_burst = 0;  // if set, capture this many consecutive presented frames from capture_frame
  uint64_t capture_sim_frame = 0;  // if set, the burst starts at the first presented frame whose simulation sequence >= this
  std::string shader_cache = "shadercache";   // directory for compiled shader blobs and the D3D12 pipeline library
  std::string dump_path;      // write a text dump of draw state + shaders at dump_frame
  uint32_t dump_frame = 0;
};

// Compatibility name retained for tests and backend-facing callers from before the shared options
// header existed. It is the same object type, not a second settings layout.
using D3D12Options = RenderOptions;

// Sub-frame animation only means anything when the display shows more frames than the simulation
// produces. At a cap of 60 or below there is one presented frame per 60 Hz tick either way, so
// re-posing buys no smoothness at all and costs three things: the solver's time, a frame of display
// delay in the Interpolate modes, and poses taken at an arbitrary fraction of a tick instead of the
// exact ones the game computed. That last one is visible: players running a 60 cap with sub-frame
// animation on reported stage geometry glitching on Yoshi's Story, Dream Land and Fountain of
// Dreams, and switching sub-frame animation off fixed it. So the two are not allowed to combine.
//
// A cap of 0 is uncapped and -1 follows the monitor, and both of those can exceed 60.
inline bool subframe_useful(double fps_cap) { return fps_cap <= 0.0 || fps_cap > 60.0; }

// Aspect the presented image is letterboxed to, for these options and this client size.
//
// Melee does not render 4:3. Every camera in the game asks C_MTXPerspective for aspect
// 1.2173333f (melee/src/melee/cm/camera.c, written 913.0f/750.0f in melee/src/melee/ty/toy.c),
// which the captured XF projection registers confirm at runtime: 4.51071/3.70743 = 1.216668 on
// the menus and 5.67128/4.65877 = 1.217334 in a match, both 73:60 to within a rounding error.
// The console agrees: the VI paints Melee's 640 framebuffer columns into 640 of the 720 BT.601
// samples of an NTSC active line, which is why Dolphin's VI derived aspect comes out at 1.2154
// and why Slippi Dolphin ships "Force 73:60 (Melee)" as its default (VideoConfig.cpp).
// The Slippi widescreen Gecko code multiplies that camera aspect by 320/219, and 73/60 * 320/219
// is exactly 16/9, so with the code on the correct presentation is 16:9. Some scenes keep the
// authored 4:3 projection even when the renderer has a widescreen presentation option; those pass
// widenable_scene=false so the window is letterboxed instead of stretching that scene.
// The aspect True widescreen widens the world to, clamped to [kTrueAspectMin, kTrueAspectMax].
// 16:9 is returned as the exact constant so the classic mode is unchanged.
inline float true_widescreen_aspect(const RenderOptions& options, int client_w, int client_h) {
  float a = 16.0f / 9.0f;
  switch (options.true_aspect) {
    case TrueAspect::Wide16_9:  return a;
    case TrueAspect::Wide16_10: a = 16.0f / 10.0f; break;
    case TrueAspect::Custom:
      a = options.true_custom_h > 0 ? (float)options.true_custom_w / (float)options.true_custom_h : a; break;
    case TrueAspect::FitWindow:
      a = client_w > 0 && client_h > 0 ? (float)client_w / (float)client_h : a; break;
  }
  return a < kTrueAspectMin ? kTrueAspectMin : a > kTrueAspectMax ? kTrueAspectMax : a;
}

inline float presented_aspect(const RenderOptions& options, int client_w, int client_h,
                              bool widenable_scene = true) {
  switch (options.aspect) {
    case AspectMode::Native:    return 73.0f / 60.0f;
    case AspectMode::Force4_3:  return 4.0f / 3.0f;
    case AspectMode::Force16_9: return 16.0f / 9.0f;
    // No bars at all: claiming the window's own aspect makes the letterbox maths fill it exactly.
    case AspectMode::Stretch:   return (float)(client_w > 0 ? client_w : 1) / (float)(client_h > 0 ? client_h : 1);
    default:
      if (!widenable_scene) return 73.0f / 60.0f;
      if (options.widescreen) return 16.0f / 9.0f;
      return options.true_widescreen ? true_widescreen_aspect(options, client_w, client_h) : 73.0f / 60.0f;
  }
}

// Ask the renderer to write the next `frames` presented frames to capture/blink_<n>.ppm. Bound to
// F2 so a defect that only appears in a real session can be caught by the person watching it:
// four scripted captures of the stage blinking reproduced nothing, because a headless run never
// falls behind and never sees the conditions it needs.
void request_frame_capture(unsigned frames);
// The pending count, shared by both backends so F2 behaves identically on each.
unsigned gx_capture_request();
void gx_capture_request_set(unsigned frames);

// The "Low spec" switch (pc_settings.cpp). Turning it on keeps what the player had and puts every
// setting that costs frames at its cheapest; turning it off puts exactly the kept values back.
inline void apply_low_spec(RenderOptions& o, bool d3d11_available) {
  o.low_spec_previous = {o.api, o.fps_cap, o.efb_scale, o.ssaa, o.anisotropy, o.effects_level, o.dlss_mode, o.subframe};
  if (d3d11_available) o.api = RenderApi::D3D11;   // the better exercised driver path on old integrated GPUs
  o.fps_cap = 60;
  o.efb_scale = 1;                  // native 640x528, the floor
  o.ssaa = 1;                       // no supersampling
  o.anisotropy = 1;                 // no anisotropic filtering
  o.dlss_mode = 0;                  // NVIDIA and Direct3D 12 only
  o.subframe = SubFrameMode::Off;   // the sub-frame solver is the largest CPU cost here
  o.low_spec = true;
}
inline void restore_low_spec(RenderOptions& o) {
  const auto& p = o.low_spec_previous;
  o.api = p.api; o.fps_cap = p.fps_cap; o.efb_scale = p.efb_scale;
  o.ssaa = p.ssaa; o.anisotropy = p.anisotropy; o.effects_level = p.effects_level;
  o.dlss_mode = p.dlss_mode; o.subframe = p.subframe;
  o.low_spec = false;
}

// Moves the "Lightweight" ladder to `level`. Levels 1 to 3 are read live by the renderers and the
// fighter draw; 4 and 5 also hold the image at its cheapest through the Low spec pair above, kept
// and given back exactly. 5 lifts the frame cap: a wireframe frame costs almost nothing, so the
// game shows as many as the machine can make. Leaving 5 puts the 60 cap Low spec uses back.
inline void apply_lightweight(RenderOptions& o, int level, bool d3d11_available) {
  level = level < 0 ? 0 : level > 5 ? 5 : level;
  if (level >= 4 && !o.low_spec) apply_low_spec(o, d3d11_available);
  if (level < 4 && o.low_spec) restore_low_spec(o);
  // 4 and 5 lower the image only: the frame cap and the in-between frames (sub-frames) stay the player's own, since
  // with sub-frames off the game presents one frame per 60 Hz tick whatever the cap (measured 10-25: Low spec's
  // 60 cap and sub-frames off held both levels at 60 fps). 5 also lifts the cap.
  if (level >= 4) { o.fps_cap = o.low_spec_previous.fps_cap; o.subframe = o.low_spec_previous.subframe; }
  if (level == 5) { o.fps_cap = 0; if (o.subframe == SubFrameMode::Off) o.subframe = SubFrameMode::Authored; }
  o.lightweight = level;
}
// Settings files from before the ladder: "lowpoly 1" was level 1, "lowspec 1" level 4.
// The looks players pick by name. Each sets the Lightweight level and every look control; changing any control
// afterwards makes the row read "Custom" (look_preset_of finds no match).
struct LookPreset {
  const char* name; const char* tip;
  int lightweight, fighter, stage, menus; bool unfiltered; int colours, background, effects;
  int lab = 0, lab_stage = 0;   // 0: the 3D scene; 1 stick figures, 2 capsules in the Lab view (stage: 1 outline, 2 blocks)
  bool unlocked = false;        // lifts the frame cap
  bool own = false;             // our renderer version, never the exact look's pack
};
inline const LookPreset* look_presets(int* count) {
  static const LookPreset p[] = {
    {"Off", "The game as it is.", 0, 0, 0, 0, false, 0, 0, 0},
    {"Light", "Low poly fighters: the models the game itself uses in reflections and shadows.", 1, 0, 0, 0, false, 0, 0, 0},
    {"Lighter", "Low poly fighters, and the stage scenery behind the fighting area is not drawn.", 2, 0, 0, 0, false, 0, 0, 0},
    {"Diet Classic (mod)", "Uses Diet Classic artwork from your own patch after setup and restart.", 2, 0, 0, 0, false, 0, 1, 1},
    {"Diet 64 (mod)", "Uses Diet 64 artwork from your own patch after setup and restart.", 1, 1, 1, 1, true, 0, 0, 1},
    {"Crystal (original colors)", "Crystal mod artwork in its original colors. Requires your own patch and a restart.", 2, 2, 2, 4, false, 0, 1, 2},
    {"Crystal (player colors)", "The same Crystal artwork with fighters recolored by player: P1 red, P2 blue, P3 yellow, P4 green.", 2, 2, 2, 4, false, 1, 1, 2},
    {"Wireframe", "Everything as lines on black, frame cap lifted.", 5, 5, 5, 0, false, 0, 1, 2},
    {"Potato", "White fighters in port colours on a white fighting area, black background, lowest image, cap lifted.", 4, 3, 3, 0, false, 1, 1, 2, 0, 0, true},
    {"Stick Figures", "Every fighter as a stick figure in its port colour, the stage as its outline. The 3D scene is not drawn.", 2, 0, 0, 0, false, 0, 0, 2, 1, 1},
    {"Capsules", "Every fighter as rounded capsules (its hurtboxes) in its port colour, the stage as solid blocks. No 3D scene.", 2, 0, 0, 0, false, 0, 0, 2, 2, 2},
    {"Minimal", "Stick figures on the stage outline with the frame cap lifted: the least work any look does.", 2, 0, 0, 0, false, 0, 0, 2, 1, 1, true},
    {"64 Style (built-in)", "Our N64-inspired rendering, no patch needed: low poly fighters and sharp 64 x 64 textures. This is different from Diet 64's mod artwork.", 1, 1, 1, 1, true, 0, 0, 1, 0, 0, false, true},
    {"Silhouettes", "Every fighter as its real model in its port colour, so its true height and shape read, on white platforms over a plain background. No scenery.", 2, 2, 3, 0, false, 1, 2, 2},
    {"Classic Style (built-in)", "A built-in Diet Classic-inspired look. No patch needed; it does not use the mod's artwork.", 2, 0, 0, 0, false, 0, 1, 1, 0, 0, false, true},
  };
  *count = (int)(sizeof p / sizeof p[0]);
  return p;
}
inline void apply_look_preset(RenderOptions& o, int index, bool d3d11_available) {
  int n = 0; const LookPreset* p = look_presets(&n);
  if (index < 0 || index >= n) return;
  const LookPreset& l = p[index];
  apply_lightweight(o, l.lightweight, d3d11_available);
  if (l.unlocked) {
    o.fps_cap = 0;
    if (o.subframe == SubFrameMode::Off) o.subframe = SubFrameMode::Authored;
  }
  o.lab_view = l.lab != 0; o.look_lab_fighters = l.lab; o.look_lab_stage = l.lab_stage;
  o.look_fighter = l.fighter; o.look_stage = l.stage; o.look_menus = l.menus; o.look_unfiltered = l.unfiltered;
  o.look_colours = l.colours; o.look_background = l.background; o.effects_level = l.effects; o.look_own = l.own;
}
inline int look_preset_of(const RenderOptions& o);
// The exact look a preset stands for, served from the player's own pack when it is built (cosmetic_mods.h
// build_look_packs), or "" for the others.
inline const char* exact_look_of(const RenderOptions& o) {
  if (o.look_own) return "";
  switch (look_preset_of(o)) {
    case 3: return "Diet Classic";
    case 4: return "Diet 64";
    case 5: case 6: return "Crystal";
    default: return "";
  }
}
// The preset the current settings match, or -1 (Custom).
inline int look_preset_of(const RenderOptions& o) {
  int n = 0; const LookPreset* p = look_presets(&n);
  auto same = [&](const LookPreset& l) {
    return o.lightweight == l.lightweight && o.look_fighter == l.fighter && o.look_stage == l.stage && o.look_menus == l.menus &&
           o.look_unfiltered == l.unfiltered && o.look_colours == l.colours && o.look_background == l.background &&
           o.effects_level == l.effects && o.lab_view == (l.lab != 0) &&
           (l.lab == 0 || (o.look_lab_fighters == l.lab && o.look_lab_stage == l.lab_stage)) &&
           (!l.unlocked || o.fps_cap == 0) && o.look_own == l.own;
  };
  // A preset that lifts the cap first (it is the more specific one when the cap is lifted), then the rest.
  for (int i = 0; i < n; ++i) if (p[i].unlocked && same(p[i])) return i;
  for (int i = 0; i < n; ++i) if (!p[i].unlocked && same(p[i])) return i;
  return -1;
}

// The old "Low spec" switch lowered the image only: it stays on (with the settings it keeps for the player) and
// the look stays the game's own. 0.9.60 moved it to level 4 instead, which also drops textures and stage
// backgrounds (white fighters on black); a file it wrote (no "lowspec_look") with level 4 and no look of its own
// goes back to level 0. Picking a look below 4 later gives the kept image settings back (apply_lightweight).
inline void migrate_lightweight(RenderOptions& o, bool lowspec_look = false) {
  if (!lowspec_look && o.low_spec && o.lightweight == 4 && o.look_fighter == 0 && o.look_stage == 0 &&
      o.look_menus == 0 && !o.look_unfiltered && o.look_colours == 0 && o.look_background == 0 && !o.lab_view &&
      !o.look_own)
    o.lightweight = 0;
  if (o.low_poly_fighters && o.lightweight < 1) o.lightweight = 1;
  o.low_poly_fighters = 0;   // the ladder decides from here on
}

}  // namespace gx
