// GX command processor: decodes the FIFO stream into register state and captured frames.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "gx_regs.h"
#include "native_draw_audit.h"
#include "../abi/mu_native_pose.h"
#include "../host/tick_timing.h"
#include "texture_snapshot.h"

struct MuNativePoseSnapshot;

namespace gx {

constexpr int EFB_WIDTH = 640, EFB_HEIGHT = 528;

#pragma pack(push, 1)
struct Vertex {
  float pos[3];
  float nrm[3];
  uint8_t col0[4];
  uint8_t col1[4];
  float uv[8][2];
  uint8_t posmtx;
  uint8_t texmtx[8];
  uint8_t pad[3];
};
#pragma pack(pop)
static_assert(sizeof(Vertex) == 108, "vertex layout");

// Components present in the vertex stream (VB_HAS_* in Dolphin).
enum : uint32_t {
  VB_HAS_POSMTXIDX = 1u << 0,
  VB_HAS_TEXMTXIDX0 = 1u << 1,   // ..7 -> bits 1..8
  VB_HAS_NRM0 = 1u << 10, VB_HAS_NRM1 = 1u << 11, VB_HAS_NRM2 = 1u << 12,
  VB_HAS_COL0 = 1u << 13, VB_HAS_COL1 = 1u << 14,
  VB_HAS_UV0 = 1u << 15,         // ..7 -> bits 15..22
};

// Snapshot of everything a draw needs; the backend replays these.
struct TextureRef {
  uint32_t addr = 0, width = 0, height = 0, format = 0, tlut_addr = 0, tlut_format = 0;
  uint32_t mode0 = 0, mode1 = 0;   // wrap/filter/lod
  uint32_t mip_levels = 1;
  std::shared_ptr<const TextureSnapshot> data;
  bool used = false;
};

struct AuthoredPose;
struct NativePoseBridgeStats {
  uint64_t snapshots = 0;
  uint64_t rigid_captured = 0;
  uint64_t envelope_scopes = 0;
  uint64_t envelope_captured = 0;
  // Captures split by the PObj class's setup_mtx: the stock HSD path (the population the Legacy
  // observer hooks) versus a custom one (fighters' ftParts path, which Legacy never observes).
  uint64_t captured_default_setup = 0;
  uint64_t captured_custom_setup = 0;
  uint64_t invalid_payloads = 0;
  uint64_t rejected[MU_NATIVE_POSE_REJECT_COUNT]{};
};
struct DrawSegment {
  uint32_t first_vertex, vertex_count;
  uint32_t primitive;
};

enum class DrawTopology { Unsupported, Triangles, Lines };

// Appends one GX primitive's indices while preserving the boundary between adjacent commands.
// In particular, two strips must never grow a triangle across their shared batch boundary.
inline DrawTopology append_segment_indices(std::vector<uint32_t>& out, uint32_t primitive,
                                           uint32_t count, uint32_t base) {
  switch (primitive) {
    case 0x80: case 0x88:
      for (uint32_t i = 0; i + 3 < count; i += 4)
        out.insert(out.end(), {base+i, base+i+1, base+i+2, base+i, base+i+2, base+i+3});
      return DrawTopology::Triangles;
    case 0x90:
      for (uint32_t i = 0; i + 2 < count; i += 3)
        out.insert(out.end(), {base+i, base+i+1, base+i+2});
      return DrawTopology::Triangles;
    case 0x98:
      for (uint32_t i = 2; i < count; ++i) {
        if (i & 1) out.insert(out.end(), {base+i-1, base+i-2, base+i});
        else out.insert(out.end(), {base+i-2, base+i-1, base+i});
      }
      return DrawTopology::Triangles;
    case 0xA0:
      for (uint32_t i = 2; i < count; ++i)
        out.insert(out.end(), {base, base+i-1, base+i});
      return DrawTopology::Triangles;
    case 0xA8:
      for (uint32_t i = 0; i + 1 < count; i += 2)
        out.insert(out.end(), {base+i, base+i+1});
      return DrawTopology::Lines;
    case 0xB0:
      for (uint32_t i = 1; i < count; ++i)
        out.insert(out.end(), {base+i-1, base+i});
      return DrawTopology::Lines;
    default:
      return DrawTopology::Unsupported;
  }
}
struct DrawCall {
  // ~5 KB of register and matrix snapshots, recorded ~1400 times per simulation frame. Zeroing all
  // of that costs real time on the simulation thread, so record_draw (which overwrites every one of
  // those arrays) opts out with the tag below. Every other caller gets the safe zeroed default.
  struct SkipInit {};
  DrawCall() = default;
  explicit DrawCall(SkipInit) {}
  uint32_t primitive;              // GX primitive opcode & 0xF8
  uint32_t first_vertex, vertex_count;
  // Consecutive primitive commands in one display list share all render state. They are retained as
  // separate index ranges inside one backend draw instead of duplicating a 5 KB DrawCall for each.
  uint32_t first_segment = 0, segment_count = 0;
  uint32_t components;
  // Register snapshots used by shader generation and pipeline state.
  BPMemory bp;
  // XF subset needed by the vertex shader (matrices are copied in full: simplest and rollback-safe).
  float posMatrices[256];
  float normalMatrices[96];
  float postMatrices[256];
  uint8_t lights[8][64];           // raw light blocks (Light struct is 64 bytes)
  uint32_t xf_regs[0x58];          // 0x1000..0x1057
  uint32_t matrix_index_a, matrix_index_b;
  int32_t tev_colors[4][4];   // RGBA, signed 11-bit (BP E0-E7 writes with type 0)
  int32_t tev_kcolors[4][4];  // RGBA (type 1 writes)
  TextureRef textures[8] = {};     // snapshot_textures relies on `used` starting false
  int viewport_x = 0, viewport_y = 0, viewport_w = 0, viewport_h = 0;  // unused placeholders (computed by backend)
  // Stable identity of this draw across frames (display-list address + call ordinal + draw ordinal,
  // or texture/size/ordinal for immediate-mode draws). Used to pair draws for sub-frame rendering.
  uint64_t identity = 0;
  std::shared_ptr<const AuthoredPose> authored_pose;
  uint64_t object_generation = 0; // Allocated JObj lifetime; zero means unobserved.
  // Player slot (0..5) whose fighter rendered this draw, 0xFF for everything else. Display only:
  // it feeds the per-player tint, is not part of `identity` and never reaches a shader UID.
  uint8_t owner_player = 0xFF;
  // Skinned (envelope) draw: the fighter's model. Its shadow and its effects are rigid draws, so
  // this is what tells the model apart from everything else the same player renders.
  bool skinned = false;
  // Render-thread cache: the pipeline resolved for this draw (valid for the backend that set it).
  // Sub-frames re-present the same draws, so the shader UIDs are hashed once per simulation frame.
  mutable void* cached_pipeline = nullptr;
  mutable uint64_t cached_pipeline_owner = 0;
};

// Replacement transform state for one draw when a sub-frame is rendered between simulation frames.
struct DrawMatrices {
  float pos[256];
  float nrm[96];
  // Effects that rewrite their vertex stream every simulation frame (sparks, shields, hit flashes)
  // cannot be moved by a matrix. When set, the renderer draws these vertices instead of the frame's.
  const Vertex* vertices = nullptr;
  // A smoothly changing game camera (pause/unpause zoom) belongs on the same
  // presentation timeline as its model-view matrices.
  float projection[6] = {};
  bool has_projection = false;
};

struct EfbCopy {
  uint32_t dest_addr, dest_stride, src_x, src_y, src_w, src_h;
  uint32_t format;   // copy format (tp_realFormat)
  bool to_xfb, clear, intensity, half_scale, is_depth;
  bool efb_alpha;     // the EFB pixel format keeps alpha (RGBA6_Z24); otherwise copies read alpha as 1, as on hardware
  uint32_t clear_color, clear_z;  // ARGB, 24-bit z
  float y_scale;
};

struct FrameCommand {
  enum Kind { Draw, Copy } kind;
  uint32_t index;   // into draws or copies
};

struct HudPlayerSnapshot {
  int damage = 0, stocks = 0;
  float tag_x = 0, tag_y = 0; // Melee's 640x480 screen coordinates
  bool present = false, tag_visible = false;
};

struct Frame {
  std::vector<Vertex> vertices;
  std::vector<DrawSegment> segments;
  std::vector<DrawCall> draws;
  std::vector<EfbCopy> copies;
  std::vector<FrameCommand> commands;
  std::array<HudPlayerSnapshot, 4> hud_players{};
  std::array<std::string, 4> player_names{};
  uint64_t sequence = 0;
  // The game's scene controller when this frame was finished: major scene (2 VS, 0x1C training,
  // 1 the menus) and the minor scene within it (2 is in-game for the match modes; the character and
  // stage selects come before it). Read on the simulation thread; the presentation thread decides
  // from it whether there is a match on screen.
  uint8_t scene_major = 0, scene_minor = 0;
  double time = 0.0;   // host seconds of the retrace this frame belongs to (see host::frame_time)
  host::TickTiming tick;   // latency trace: the tick that produced this frame (host::tick_timing)
  // The game's timeline jumped between the previous frame and this one: an online rollback loaded
  // an older state and simulated forward again. The two frames are not neighbours in time, so
  // nothing may be blended between them (see mark_discontinuity).
  bool discontinuous = false;
  // The jump was not an online rollback (a Training Lab load, a replay seek, skipped frames): the
  // new state can be anywhere, so not even a fighter's pose may be carried across it.
  bool hard_cut = false;
  // Display latency test (set_latency_test): -1 the picture as drawn, 0 all black, 1 all white. Decided
  // from the pads the game read for this frame, so a photodiode times the same input-to-photon path.
  int8_t test_fill = -1;
  // Reserve enough storage for an ordinary match before the first large scene is decoded. DrawCall
  // is about 5 KB, so a vector growth during a Stadium transformation can otherwise copy several
  // megabytes on the simulation thread and miss a 16.7 ms frame. Recycled frames retain this memory.
  void reserve_gameplay_capacity(size_t vertex_count = 65536, size_t draw_count = 1024,
                                 size_t segment_count = 2048, size_t command_count = 2048,
                                 size_t copy_count = 16) {
    vertices.reserve(vertex_count);
    draws.reserve(draw_count);
    segments.reserve(segment_count);
    commands.reserve(command_count);
    copies.reserve(copy_count);
  }
  // sequence and time are reset too: a recycled frame is handed back to the producer as "cleared",
  // and the renderer decides when to present by comparing sequences. Leaving a stale one on a
  // buffer that is about to be refilled is only harmless while every producer remembers to assign
  // one before pushing.
  void clear() { vertices.clear(); segments.clear(); draws.clear(); copies.clear(); commands.clear(); hud_players = {}; player_names = {}; sequence = 0; time = 0.0; tick = {}; discontinuous = false; hard_cut = false; test_fill = -1; }
};

// Display latency test for photodiode testers that press X + D-pad Down (LDAT, pico lag testers;
// the Lossless adapter's test is the separate lag test launch, shim/mu_lagtest.c): while on, X + D-pad Down on any controller turns the picture all black, then switches it
// between black and white with each press; X + D-pad Up gives the picture back. Visual only: the game
// never sees it, so it is the same online. presented_test_fill is the fill of the frame a backend is
// drawing now (set in submit_frame), read by the overlay that paints it.
void set_latency_test(bool on);
void set_presented_test_fill(int fill);
int presented_test_fill();

// The panel publishes visual-only HUD controls to the simulation thread.
void set_hud_scales(int stocks_percent, int damage_percent, bool pal_stocks);
// The same sizes for the native game (MuHostApi.hud_scales): stock | damage << 8, percent.
uint32_t hud_scales_packed();
// "Low poly fighters" (RenderOptions::low_poly_fighters), published the same way: the native game
// reads it through MuHostApi.display_options, the Static Recomp's parts hook (host.cpp) reads it live.
void set_low_poly_fighters(bool on);
bool low_poly_fighters_active();
// "Fountain of Dreams reflections" (RenderOptions::fod_reflections) for the Source Port, read through
// MuHostApi.display_options when the stage loads. The Static Recomp follows the Gecko list instead.
void set_fod_reflections(bool on);
bool fod_reflections_active();
// "Character select background plays through" (RenderOptions::css_background_free), read by both engines.
void set_css_background_free(bool on);
bool css_background_free_active();
// "Always show player tags" (RenderOptions::player_tags_always), published the same way: the P1 / P2
// markers over the fighters stay up for the whole match. Display only, both engines.
void set_player_tags_always(bool on);
bool player_tags_always_active();
// The native game's players (MuHostApi.hud_player), each drawn frame: the Static Recomp reads the
// same HUD objects from guest RAM (capture_match_hud). Simulation thread.
void set_native_hud_player(int slot, bool present, int damage, int stocks, float tag_x, float tag_y,
                           bool tag_visible);

// Whether a frame certainly shows a running match. Anything else counts as a menu: the character and
// stage selects are minor scenes 0 and 1 of every mode that plays a match, the match is 2 and up.
inline bool frame_in_match(const Frame& f) {
  const uint8_t major = f.scene_major, minor = f.scene_minor;
  // Slippi online play is major scene 08: its character select is minor 0, the match minor 2, and
  // the screens around them (splash, results) other minors. Missing from the list below, an online
  // match used to be treated as a menu: sub-frame animation ran in its menu mode for whole matches.
  if (major == 0x08) return minor == 2;
  // Debug VS hosts replay playback: minor 1 is play and minor 3 is results.
  if (major == 0x0E) return minor == 1;
  // Event Match (including TM-CE) uses minor 0 for CSS, 1 for play and 2 for SSS.
  if (major == 0x2B) return minor == 1;
  // The 1P modes number their scenes their own way (the game's scene tables, gm/gmclassic.c,
  // gmadventure.c, gmallstar.c, gmmultiman.c, gmhomerun.c). With the "minor 2 and up" rule below,
  // the first fight of Classic (minor 1), Adventure (minor 1) and All-Star (minors 0 and 1) and the
  // only fight of the Stadium modes (minor 1) counted as a menu, and pausing there lost sub-frame
  // smoothing for the whole pause zoom, while their splash screens and cutscenes counted as matches.
  // Classic: 8 * stage + 1 is a fight, 8 * stage its splash, 0x68 and up the endings and the CSS.
  if (major == 0x03) return minor < 0x68 && (minor & 7) == 1;
  // Adventure: the fights by scene id (the rest are splashes, cutscenes, endings and the CSS).
  if (major == 0x04) {
    static constexpr uint8_t kFights[] = {0x01, 0x03, 0x09, 0x0A, 0x11, 0x12, 0x19, 0x1B, 0x21, 0x23, 0x25,
                                          0x29, 0x2B, 0x31, 0x3A, 0x3B, 0x41, 0x49, 0x51, 0x53, 0x59, 0x5C};
    for (uint8_t id : kFights) if (minor == id) return true;
    return false;
  }
  // All-Star: every scene below the endings is a fight or the rest area, which is one too.
  if (major == 0x05) return minor < 0x68;
  // Target Test, Home-Run Contest and Multi-Man Melee: minor 0 is the CSS, minor 1 the stage.
  if (major == 0x0F || (major >= 0x20 && major <= 0x26)) return minor == 1;
  // VS and its variants (Special Melee included): CSS 0, SSS 1, the match and what follows from 2.
  const bool match_mode = major == 0x02 || (major >= 0x10 && major <= 0x13) || major == 0x1B || major == 0x1C ||
                          (major >= 0x1D && major <= 0x1F);
  return match_mode && minor >= 2;
}

// Whether a frame's scene is presented wide when a widescreen mode is on. Every screen in the game
// is drawn through perspective cameras, which both widescreen modes widen (the Slippi code in the
// game, True 16:9 in build_projection), so every scene is, except the opening movie: a video on
// the 2D layer, which nothing widens and which would only be stretched.
// This used to be a list of the modes that play a match. The main menu was left out because its
// camera was not being widened (see is_authored_fullscreen), and the list also missed Home-Run
// Contest, Multi-Man Melee and the Special Melee modes. Every scene left out was shown squeezed
// into a 73:60 box when the Slippi code was on, since the game had widened it all the same.
inline bool frame_has_widenable_scene(const Frame& f) {
  return f.scene_major != 0x18;   // GM_OPENING_MV
}

// A mod can draw a perspective HUD before the world. Prefer a fighter's camera so that HUD
// neither receives temporal scene filtering nor follows the world's sub-frame camera motion.
inline const DrawCall* frame_scene_draw(const Frame& f) {
  const DrawCall* fallback = nullptr;
  for (const auto& d : f.draws) {
    if (d.xf_regs[0x26] != 0) continue;
    if (!fallback) fallback = &d;
    if (d.owner_player < 6 && d.skinned) return &d;
  }
  return fallback;
}

// "Visual effects" (Reduced / Minimal): whether a draw is decoration the player chose to skip. Only
// during a match, so menus are never touched: an earlier version applied everywhere and hid the
// stage select pointer and menu text, which are drawn the same way as a hit spark. Only blended
// draws of the world camera that do not write depth qualify, so fighters, the stage and the HUD
// always draw. Reduced skips additive ones (glow, sparks, flashes); Minimal also skips any that
// neither write nor test depth (screen overlays in the world). Display only: guest memory is
// untouched, so it cannot desync and two players may use different levels.
//
// "Of the world camera" is the projection the fighters are drawn with. A HUD drawn in perspective
// has a camera of its own, and text from the game's text library neither tests nor writes depth:
// without this test Minimal removed a mod's song title banner text and left its empty backing.
struct EffectsFilter {
  int level = 0;                // 0 = nothing is skipped this frame
  uint32_t projection[6] = {};  // XF 0x1020..0x1025 of the world camera
};
inline EffectsFilter effects_filter(const Frame& f, int level) {
  EffectsFilter e;
  if (level <= 0 || !frame_in_match(f)) return e;
  const DrawCall* scene = frame_scene_draw(f);
  if (!scene) return e;
  std::memcpy(e.projection, &scene->xf_regs[0x20], sizeof e.projection);
  e.level = level;
  return e;
}
inline bool skip_for_effects(const EffectsFilter& e, const DrawCall& dc) {
  if (e.level <= 0 || dc.xf_regs[0x26] != 0 || !(dc.bp.blendmode() & 1)) return false;
  const uint32_t zmode = dc.bp.zmode();
  if (zmode & 0x10) return false;                                   // writes depth: part of the scene
  if (std::memcmp(e.projection, &dc.xf_regs[0x20], sizeof e.projection) != 0) return false;   // another camera
  const bool additive = ((dc.bp.blendmode() >> 5) & 7) == 1;         // destination factor ONE
  return additive || (e.level >= 2 && !(zmode & 1));
}

// "Lightweight" (RenderOptions::lightweight, 0 to 5): one ladder of display-only savings for weak
// PCs, each level including the ones below it. 1 low poly fighters, 2 no stage backgrounds, 3 flat
// shading (no textures, Visual effects at Minimal), 4 the lowest image settings, 5 wireframe.
// Only draws of the world camera in a running match are touched, with the same gate Visual effects
// uses, so menus, the HUD and the pause screen always draw as the game made them. Guest memory is
// never written here, so online play, replays and the state digest are the same at every level.
constexpr int kLightweightLevels = 6;
void set_lightweight_level(int level);
int lightweight_level();
// What the last finished match frame drew and skipped (all backends), for the settings hint.
struct LightweightCounts { uint32_t drawn = 0, skipped = 0; };
LightweightCounts lightweight_last_counts();
void lightweight_count(bool skipped);
void lightweight_publish_counts();

struct LightweightFilter {
  int level = 0;                // 0 = nothing changes this frame
  uint32_t projection[6] = {};  // XF 0x1020..0x1025 of the world camera
  float fighter_far = 0;        // the farthest fighter's view-space z (negative, the camera looks down -z)
  bool have_fighters = false;
  bool in_match = false;        // a running match with a world camera (projection is set): looks apply to it
};
// A stage piece counts as background when all of it lies this many world units behind the
// farthest fighter. Melee plays on the z = 0 plane: platforms, ledges and walls sit within a few
// units of it, scenery is tens to thousands of units back.
constexpr float kLightweightBackgroundDepth = 60.0f;

// The view-space z of a vertex: row 2 of its position matrix (GX keeps model-view matrices).
inline float lightweight_view_z(const DrawCall& d, const Vertex& v) {
  const int row = (int)(v.posmtx & 0x3F);
  if (row + 2 >= 64) return 0;
  const float* m = &d.posMatrices[(row + 2) * 4];
  return m[0] * v.pos[0] + m[1] * v.pos[1] + m[2] * v.pos[2] + m[3];
}
// MELEE_TEST_LIGHTWEIGHT_LOG=1 (tests): every 600th match frame, the depths the background rule sees.
void lightweight_debug(const Frame& f, const LightweightFilter& e);
inline bool lightweight_world_draw(const LightweightFilter& e, const DrawCall& d) {
  return e.level > 0 && d.xf_regs[0x26] == 0 &&
         std::memcmp(e.projection, &d.xf_regs[0x20], sizeof e.projection) == 0;
}
inline LightweightFilter lightweight_filter_build(const Frame& f, int level) {
  lightweight_publish_counts();
  LightweightFilter e;
  if (!frame_in_match(f)) return e;
  const DrawCall* scene = frame_scene_draw(f);
  if (!scene) return e;
  // The world camera is known in every match, so looks (gx_retro.h) can tell fighters and stage from menus even
  // with the Lightweight level at 0.
  std::memcpy(e.projection, &scene->xf_regs[0x20], sizeof e.projection);
  e.in_match = true;
  if (level <= 0) return e;
  e.level = level;
  if (level >= 2) {
    // The play plane's depth: the nearest skinned model of the world camera, which is a fighter
    // (draw owners are only tracked for the L-cancel indicator, so they cannot be relied on here).
    // A skinned piece of scenery further back (Whispy, a crowd) only moves this nearer the camera
    // if nothing else is skinned, and then the rule skips less, never more.
    for (const auto& d : f.draws) {
      if (!d.skinned || !lightweight_world_draw(e, d)) continue;
      const int row = (int)(d.matrix_index_a & 0x3F);
      if (row + 2 >= 64) continue;
      const float z = d.posMatrices[(row + 2) * 4 + 3];
      if (!std::isfinite(z)) continue;
      if (!e.have_fighters || z > e.fighter_far) e.fighter_far = z;
      e.have_fighters = true;
    }
  }
  lightweight_debug(f, e);
  return e;
}
// The filter of the frame being drawn, for the pixel constants (gx_shader.cpp fill_ps_constants), which only
// see the draw. Set by lightweight_filter on the renderer's thread.
inline LightweightFilter& look_filter_slot() { static LightweightFilter f; return f; }   // header only: tests link gx_shader alone
inline void set_look_filter(const LightweightFilter& e) { look_filter_slot() = e; }
inline const LightweightFilter& look_filter() { return look_filter_slot(); }
inline LightweightFilter lightweight_filter(const Frame& f, int level) {
  const LightweightFilter e = lightweight_filter_build(f, level);
  set_look_filter(e);
  return e;
}
// Level 2 and up: a piece of the stage that lies wholly behind the play plane. Fighters and items
// (they have an owner or are skinned) never count, nor does anything off the world camera. With no
// fighter in view (a cut-in, the results walk-off) nothing is skipped.
inline bool lightweight_background(const LightweightFilter& e, const Frame& f, const DrawCall& d) {
  if (e.level < 2 || !e.have_fighters || d.owner_player != 0xFF || d.skinned || !lightweight_world_draw(e, d)) return false;
  // The frame's own vertices: a stage piece is rigid, so the sub-frame solver never rewrites them.
  const Vertex* v = d.first_vertex + (size_t)d.vertex_count <= f.vertices.size() ? f.vertices.data() + d.first_vertex : nullptr;
  if (!v || d.vertex_count == 0) return false;
  // Only what the camera can see counts: stage backdrops are domes and planes thousands of units
  // across that pass beside and behind the camera, so their far edges say nothing. A vertex is in
  // view when it is in front of the camera and inside a cone wider than any Melee lens.
  // Level 5 (wireframe) keeps only what touches the play plane: a band 20 units behind to 30 in front of the
  // fighters (platforms, floors, walls, ledges). Scenery further back or in front (trees, poles, building fronts,
  // foreground props) goes too, so the lines stay readable and there are fewer draws.
  const bool band = e.level >= 5;
  const float limit = e.fighter_far - (band ? 20.0f : kLightweightBackgroundDepth);
  const float front = e.fighter_far + 30.0f;
  bool seen = false;
  float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
  for (uint32_t i = 0; i < d.vertex_count; ++i) {
    const int row = (int)(v[i].posmtx & 0x3F);
    if (row + 2 >= 64) return false;
    const float* m = &d.posMatrices[row * 4];
    float p[3];
    for (int r = 0; r < 3; ++r) p[r] = m[r * 4] * v[i].pos[0] + m[r * 4 + 1] * v[i].pos[1] + m[r * 4 + 2] * v[i].pos[2] + m[r * 4 + 3];
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) return false;
    for (int r = 0; r < 3; ++r) { lo[r] = std::min(lo[r], p[r]); hi[r] = std::max(hi[r], p[r]); }
    const float z = p[2];
    if (z >= 0 || std::abs(p[0]) > -z * 1.5f || std::abs(p[1]) > -z * 1.5f) continue;   // out of view
    seen = true;
    if (z >= limit && (!band || z <= front)) return false;   // something visible is on the play plane: keep it
  }
  if (seen) return true;
  // Nothing of it in view by its corners: only a dome or backdrop plane far larger than any stage
  // floor counts as background then (a floor spanning the screen keeps drawing).
  return hi[0] - lo[0] > 3000.0f || hi[1] - lo[1] > 3000.0f || hi[2] - lo[2] > 3000.0f;
}
// The answer of lightweight_background for draw `index` of frame `f`, remembered for that game frame: an
// uncapped display presents each game frame many times, and the test reads every vertex of a stage piece, so
// it is decided once (the first present) and reused. -1: not decided yet. Renderer thread only.
int8_t& lightweight_skip_slot(const Frame& f, size_t index, int level);
// One call per draw in every backend: skip it or not, and count it for the settings hint.
inline bool skip_for_lightweight(const LightweightFilter& e, const Frame& f, const DrawCall& d) {
  if (e.level <= 0) return false;
  const size_t index = (size_t)(&d - f.draws.data());
  bool skip;
  if (index < f.draws.size()) {
    int8_t& slot = lightweight_skip_slot(f, index, e.level);
    if (slot < 0) slot = lightweight_background(e, f, d) ? 1 : 0;
    skip = slot != 0;
  } else {
    skip = lightweight_background(e, f, d);   // a copy of a draw, not the frame's own
  }
  lightweight_count(skip);
  return skip;
}
// Level 3 and up: no textures on world draws (every slot reads one white texel).
inline bool lightweight_flat(const LightweightFilter& e, const DrawCall& d) { return e.level >= 3 && lightweight_world_draw(e, d); }
// Level 5: world draws as lines.
inline bool lightweight_wire(const LightweightFilter& e, const DrawCall& d) { return e.level >= 5 && lightweight_world_draw(e, d); }
// Visual effects in force: Lightweight 3 and up holds them at Minimal.
inline int lightweight_effects_level(int effects_level, int lightweight) { return lightweight >= 3 ? std::max(effects_level, 2) : effects_level; }

// Marks the next finished frame as discontinuous. Called on the simulation thread when the game's
// state is replaced wholesale (a rollback's savestate load), which only the host knows about.
// rollback: an online rollback, a few frames back on the same match, so fighters may still be
// smoothed through it. Anything else is a hard cut.
void mark_discontinuity(bool rollback = false);
// Online, when the game thread keeps running over its frame budget with sub-frames on, they are
// turned off until the match ends so the game holds 60 frames a second (slippi_online.cpp). The
// presentation loop reports whether sub-frames are on; it honours the suspension.
void set_subframe_suspended(bool suspended);
bool subframe_suspended();
void set_subframes_active(bool active);
bool subframes_active();

// Renderer interface shared by D3D12, D3D11, and future backends. The host calls
// these on the renderer's owning thread. This is internal C++, not the game ABI.
struct RenderOptions;
struct Backend {
  virtual ~Backend() = default;
  virtual const RenderOptions* presentation_options() const { return nullptr; }
  virtual void resize(int, int) {}
  virtual void presentation_stats(uint32_t* frames, uint32_t* pipelines, uint32_t* textures) const {
    if (frames) *frames = 0;
    if (pipelines) *pipelines = 0;
    if (textures) *textures = 0;
  }
  virtual std::string profile_line() const { return {}; }
  virtual void set_present_deadline(double) {}
  virtual double presentation_wait_seconds() const { return 0; }
  // NVIDIA Reflex pacing for the threaded renderer: reflex_pacing(cap_hz) applies Reflex's own frame
  // limit (cap_hz, 0 = none) and says whether Reflex holds the frame rate; reflex_sleep() is then
  // called once at the start of each presented frame, before its moment in time is chosen.
  virtual bool reflex_pacing(double) { return false; }
  virtual void reflex_sleep() {}
  // What the display side reported since the last call (D3D12: DXGI frame statistics), for the
  // "pacing:" line. Called on the render thread.
  virtual std::string display_timing_line() { return "display statistics not available"; }
  // Whether the display follows the frames (G-Sync, FreeSync): 1 it does, 0 it refreshes at a fixed
  // rate, -1 not known (Windows composes the window, or nothing measured yet). Judged from the
  // display's own counts while frames come slower than the refresh: a fixed display still refreshes
  // at its full rate and repeats frames, a variable one refreshes once per frame. Render thread.
  virtual int variable_refresh() const { return -1; }
  // NVIDIA frame generation running now with a fixed multiplier (2 to 6): the presentation loop
  // renders that many times fewer frames, and generation fills the rest up to the target rate.
  // 1 when it is off or in Dynamic mode (NVIDIA picks the multiplier itself).
  virtual int frame_generation_multiplier() const { return 1; }
  // The display's counts could be read at the last look (false while Windows composes the window).
  virtual bool display_counts_readable() const { return false; }
  // From the presentation loop, every frame: the monitor's nominal refresh rate (what the verdict
  // above is judged against), and whether to present synced even with V-Sync off. The loop asks for
  // that on a display that follows the frames, where it costs no delay while the rate stays under
  // the refresh, and stops every tear.
  virtual void set_display_refresh(double /*hz*/, bool /*tear_free*/) {}
  // Scanline sync on a fixed-refresh display (vblank_clock.h): the first vertical blank at or after
  // `after` (now_seconds clock) and the refresh period, or false with the reason in `why`. Render thread.
  virtual bool vblank_next(double /*after*/, double /*nominal_hz*/, double* /*when*/, double* /*period*/, std::string* why) {
    *why = "this renderer does not read the vertical blank";
    return false;
  }
  virtual void submit_frame(const Frame& frame) = 0;   // called at XFB copy
  // Queueing backends take the frame's buffers and hand back recycled ones (no copy, no
  // per-frame reallocation on the simulation thread); `frame` comes back cleared either way.
  virtual void submit_and_recycle(Frame& frame) { submit_frame(frame); frame.clear(); }
  // Render `frame` with per-draw transform overrides (one entry per frame.draws element); the
  // default ignores the overrides. Used by the sub-frame presenter for unlocked frame rates.
  virtual void submit_frame(const Frame& frame, const DrawMatrices* overrides) { (void)overrides; submit_frame(frame); }
  // Drain mode: execute the frame's commands (copies, clears, uploads) but do not blit or present it.
  virtual void set_skip_present(bool) {}
};

void init(Backend* backend);
void write_fifo(uint32_t value, int bytes);   // write-gather pipe byte stream
void write_fifo_bytes(const uint8_t* data, size_t bytes);   // the same stream in bulk, big-endian as the pipe carries it
void stats(uint64_t* commands, uint64_t* draws, uint64_t* vertices, uint32_t* efb_copies);
void set_native_draw_audit(bool enabled);
void native_render_scope_event(const NativeRenderScopeEvent& event);
NativeDrawAuditStats native_draw_audit_stats();
bool native_draw_audit_enabled();
void set_native_pose_capture_enabled(bool enabled);
bool native_pose_capture_enabled();
// Whether the renderer is presenting authored sub-frames right now (set every loop by the render
// thread). Pose capture feeds only that path, so at a 60 cap, or with sub-frames off, both the
// native snapshots and the translated observer's captures are skipped instead of thrown away.
void set_authored_capture_wanted(bool wanted);
bool authored_capture_wanted();
void native_pose_snapshot(const ::MuNativePoseSnapshot* snapshot);
std::shared_ptr<const AuthoredPose> active_native_authored_pose();
bool active_native_scope_is_skinned();
// The native game reports draw owners in the command stream (token 0xF1) instead of through the
// render observer.
void set_native_owner_source(bool native);
NativePoseBridgeStats native_pose_bridge_stats();
void finish_native_pose_scope(uint32_t scope_id);
const uint8_t* tmem();

}  // namespace gx
