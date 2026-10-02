// Render options shared by every backend, the presentation thread and the settings panel.
// Nothing here belongs to one graphics API: a backend reads what applies to it and ignores the rest.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
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
  bool fod_reflections = true;  // keep Fountain of Dreams water/reflection effects on by default
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
  float sharpness = 0.0f;     // 0..1 contrast-adaptive sharpening in the present pass (works with or without DLSS)
  float screen_space_ao = 0.0f; // 0..1 depth-based contact shading; display-only, not ray traced
  bool path_tracing = false;       // D3D12 DXR diffuse indirect bounce, display-only and off by default
  bool ray_reconstruction = false; // DLSS-RR denoising of the path-traced HDR input; needs NVIDIA runtime
  float brightness = 1.0f, contrast = 1.0f, vibrance = 1.0f;
  int anisotropy = 16;        // texture anisotropic filtering 1..16
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
inline float presented_aspect(const RenderOptions& options, int client_w, int client_h,
                              bool widenable_scene = true) {
  switch (options.aspect) {
    case AspectMode::Native:    return 73.0f / 60.0f;
    case AspectMode::Force4_3:  return 4.0f / 3.0f;
    case AspectMode::Force16_9: return 16.0f / 9.0f;
    // No bars at all: claiming the window's own aspect makes the letterbox maths fill it exactly.
    case AspectMode::Stretch:   return (float)(client_w > 0 ? client_w : 1) / (float)(client_h > 0 ? client_h : 1);
    default:                    return (options.widescreen || options.true_widescreen) && widenable_scene
                                      ? 16.0f / 9.0f : 73.0f / 60.0f;
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

}  // namespace gx
