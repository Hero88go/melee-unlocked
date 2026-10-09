// GX command processor: decodes the FIFO stream into register state and captured frames.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
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
  void clear() { vertices.clear(); segments.clear(); draws.clear(); copies.clear(); commands.clear(); hud_players = {}; player_names = {}; sequence = 0; time = 0.0; tick = {}; discontinuous = false; }
};

// The panel publishes visual-only HUD controls to the simulation thread.
void set_hud_scales(int stocks_percent, int damage_percent, bool pal_stocks);
// The same sizes for the native game (MuHostApi.hud_scales): stock | damage << 8, percent.
uint32_t hud_scales_packed();
// "Low poly fighters" (RenderOptions::low_poly_fighters), published the same way: the native game
// reads it through MuHostApi.display_options, the Static Recomp's parts hook (host.cpp) reads it live.
void set_low_poly_fighters(bool on);
bool low_poly_fighters_active();
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
  const bool match_mode = major == 0x02 || major == 0x03 || major == 0x04 || major == 0x05 ||
                          major == 0x0F || (major >= 0x10 && major <= 0x13) || major == 0x1B || major == 0x1C;
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

// Marks the next finished frame as discontinuous. Called on the simulation thread when the game's
// state is replaced wholesale (a rollback's savestate load), which only the host knows about.
void mark_discontinuity();

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
