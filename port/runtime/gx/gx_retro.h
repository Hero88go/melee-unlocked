// Looks: how fighters, the stage and the menus are drawn, chosen in PC settings > Video > "Low poly and
// lightweight" (presets Diet Classic, Diet 64, Crystal and the rest live in render_options.h). Everything here is
// display only: a texture is uploaded as a treated copy the way a texture-pack replacement is, samplers and the
// background clear change, nothing the game reads changes, so online play and replays are the same at every look.
// "Retro (64 style)" is the original switch: Retro 64 textures everywhere plus the game's low poly fighters.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include "gx_core.h"
#include "gx_texture.h"
#include "texture_pack.h"

namespace gx {

// How a group of draws is textured. Wire is drawn as lines (the Lightweight wire pipelines).
enum class Surface : uint8_t { Textured = 0, Retro64 = 1, Flat = 2, White = 3, Posterize = 4, Wire = 5 };
constexpr int kSurfaceCount = 6;

// The look in force, published once per frame by the renderer from its options (set_look_live) and read while
// drawing. Relaxed atomics: a frame drawn half with the old look is harmless.
struct LookLive {
  std::atomic<uint8_t> fighter{0}, stage{0}, menus{0};   // Surface per group; menus = everything outside a match's world
  std::atomic<bool> unfiltered{false};                   // nearest-texel sampling on treated groups (the 64 look)
  std::atomic<uint8_t> colours{0};                       // 0 original, 1 port colours on fighters
  std::atomic<bool> effect_owners{false};                // Visual effects at No decorative particles: effect draws are marked
  std::atomic<uint8_t> background{0};                    // 0 the stage's own, 1 black, 2 plain colour
  std::atomic<uint32_t> background_rgb{0x000000};
  std::atomic<uint32_t> arcade_rgb{0x1E2430};                // Arcade look "Background" colour (gx_core.h arcade_backdrop_slot)
  std::atomic<uint32_t> arcade_grid_rgb{0x3A4458};           // its grid lines (MU_DISPLAY_OPTION_GRID, drawn by the game)
  std::atomic<bool> retro{false};                        // the original "Retro (64 style)" switch
  std::atomic<bool> exact{false};                        // an exact look's pack is served: the game's art is theirs, no treatment
  std::atomic<bool> exact_low_poly{true};                // that look's own fighters are the low poly ones (Diet Classic, Diet 64; not Crystal)
};
inline LookLive& look_live() { static LookLive l; return l; }
inline void set_retro_textures(bool on) { look_live().retro.store(on, std::memory_order_relaxed); }
// Called by each renderer once a frame with its options (RenderOptions; a template keeps this header free of it).
template <class Options> inline void publish_look(const Options& o) {
  LookLive& l = look_live();
  l.retro.store(o.retro, std::memory_order_relaxed);
  l.fighter.store((uint8_t)o.look_fighter, std::memory_order_relaxed);
  l.stage.store((uint8_t)o.look_stage, std::memory_order_relaxed);
  l.menus.store((uint8_t)o.look_menus, std::memory_order_relaxed);
  l.unfiltered.store(o.look_unfiltered, std::memory_order_relaxed);
  l.colours.store((uint8_t)o.look_colours, std::memory_order_relaxed);
  l.effect_owners.store(o.effects_level > 0 || o.lightweight > 0 || o.look_stage != 0 || o.look_fighter != 0, std::memory_order_relaxed);
  l.background.store((uint8_t)o.look_background, std::memory_order_relaxed);
  l.background_rgb.store(o.look_background_rgb, std::memory_order_relaxed);
  arcade_backdrop_slot().store((uint8_t)o.arcade_backdrop, std::memory_order_relaxed);
  l.arcade_rgb.store(o.arcade_background_rgb, std::memory_order_relaxed);
  l.arcade_grid_rgb.store(o.arcade_grid_rgb, std::memory_order_relaxed);
  // An exact look draws its own art: the renderer's stand-in treatments step aside (port colours and the
  // Lightweight speed options still apply).
  if (l.exact.load(std::memory_order_relaxed)) {
    l.fighter.store(0, std::memory_order_relaxed); l.stage.store(0, std::memory_order_relaxed);
    l.menus.store(0, std::memory_order_relaxed); l.background.store(0, std::memory_order_relaxed);
    l.unfiltered.store(false, std::memory_order_relaxed);
  }
}
inline bool retro_textures_active() { return look_live().retro.load(std::memory_order_relaxed); }
constexpr uint32_t kRetroMaxSide = 64;

// Halves an RGBA8 image with a 2 x 2 box filter (an odd last row or column is folded into its neighbour).
inline void retro_halve(std::vector<uint8_t>& px, uint32_t& w, uint32_t& h) {
  const uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
  std::vector<uint8_t> out((size_t)nw * nh * 4);
  for (uint32_t y = 0; y < nh; ++y)
    for (uint32_t x = 0; x < nw; ++x)
      for (int c = 0; c < 4; ++c) {
        uint32_t sum = 0, n = 0;
        for (uint32_t dy = 0; dy < 2; ++dy)
          for (uint32_t dx = 0; dx < 2; ++dx) {
            const uint32_t sx = std::min(w - 1, x * 2 + dx), sy = std::min(h - 1, y * 2 + dy);
            sum += px[((size_t)sy * w + sx) * 4 + c]; ++n;
          }
        out[((size_t)y * nw + x) * 4 + c] = (uint8_t)((sum + n / 2) / n);
      }
  px.swap(out); w = nw; h = nh;
}

// One colour for a whole texture: the mean of its visible texels (weighted by alpha), alpha the mean alpha.
// A costume is a handful of textures, so a model shows a few solid colour regions (the flat colour look).
inline std::vector<uint8_t> flat_colour(const std::vector<uint8_t>& px) {
  double r = 0, g = 0, b = 0, wsum = 0, asum = 0; size_t n = px.size() / 4;
  for (size_t i = 0; i < n; ++i) {
    const double a = px[i * 4 + 3] / 255.0; asum += a;
    r += px[i * 4] * a; g += px[i * 4 + 1] * a; b += px[i * 4 + 2] * a; wsum += a;
  }
  // No alpha at all (a format whose alpha the draw does not use): the plain mean, or the texture
  // drew black wherever the draw ignores alpha.
  if (wsum <= 0) {
    for (size_t i = 0; i < n; ++i) { r += px[i * 4]; g += px[i * 4 + 1]; b += px[i * 4 + 2]; }
    const double k = 1.0 / std::max<size_t>(1, n);
    return {(uint8_t)(r * k + 0.5), (uint8_t)(g * k + 0.5), (uint8_t)(b * k + 0.5), 0};
  }
  return {(uint8_t)(r / wsum + 0.5), (uint8_t)(g / wsum + 0.5), (uint8_t)(b / wsum + 0.5), (uint8_t)(asum / std::max<size_t>(1, n) * 255.0 + 0.5)};
}

// Four levels per colour channel, alpha kept: portraits and HUD art read as flat colour shapes.
inline void posterize(std::vector<uint8_t>& px) {
  for (size_t i = 0; i < px.size(); ++i) {
    if (i % 4 == 3) continue;
    px[i] = (uint8_t)(std::min(3, px[i] / 64) * 85);
  }
}

// The surface a texture is treated with, from the draw's group. The retro switch upgrades Textured to Retro 64.
inline Surface treated(Surface s) {
  if (s == Surface::Textured && retro_textures_active()) return Surface::Retro64;
  return s;
}

// The copy of a texture to upload for a surface, or nothing (upload the game's own).
inline std::unique_ptr<texpack::Replacement> look_replacement(const TextureRef& t, Surface s) {
  if (!t.data || s == Surface::Textured || s == Surface::Wire) return nullptr;
  if (s == Surface::Retro64 && t.width <= kRetroMaxSide && t.height <= kRetroMaxSide) return nullptr;
  std::vector<uint8_t> px;
  uint32_t w = t.width, h = t.height;
  decode_texture(t.data->image.data(), t.width, t.height, t.format, t.data->palette.data(), t.tlut_format, px);
  if (px.size() < (size_t)w * h * 4) {
    if (s != Surface::White) return nullptr;
    px = {255, 255, 255, 255}; w = h = 1;   // undecodable: plain white, as before
  }
  if (s == Surface::White || s == Surface::Flat) {
    // One colour for the whole texture, its own alpha kept: a single opaque texel turned smoke, sparks
    // and cut-out shapes (leaves, fences) into solid squares, which in a match hid the fighters behind
    // them. A texture with no transparency still becomes one texel.
    const std::vector<uint8_t> flat = s == Surface::White ? std::vector<uint8_t>{255, 255, 255, 255} : flat_colour(px);
    bool opaque = true;
    for (size_t i = 3; i < px.size() && opaque; i += 4) opaque = px[i] == 255;
    if (opaque) { px = {flat[0], flat[1], flat[2], 255}; w = h = 1; }
    else for (size_t i = 0; i + 3 < px.size(); i += 4) { px[i] = flat[0]; px[i + 1] = flat[1]; px[i + 2] = flat[2]; }
  }
  else if (s == Surface::Retro64) while (w > kRetroMaxSide || h > kRetroMaxSide) retro_halve(px, w, h);
  else if (s == Surface::Posterize) posterize(px);
  auto r = std::make_unique<texpack::Replacement>();
  r->width = w; r->height = h; r->levels = 1;
  r->pixels = std::move(px);
  r->level_offset = {0}; r->level_width = {w}; r->level_height = {h};
  return r;
}
// Each surface has its own cache entries, so a live change uploads fresh copies and two groups sharing one
// texture can each have theirs.
inline uint64_t look_key(uint64_t key, Surface s) { return s == Surface::Textured ? key : key ^ (0x9E3779B97F4A7C15ull * (uint64_t)s); }

// Old entry points (Retro switch alone): kept for the call sites that only know the texture.
inline std::unique_ptr<texpack::Replacement> retro_replacement(const TextureRef& t) { return look_replacement(t, treated(Surface::Textured)); }
inline uint64_t retro_key(uint64_t key) { return look_key(key, treated(Surface::Textured)); }

// The group a draw belongs to for the look: fighters (skinned models of the match's world camera), the stage (the
// rest of that world), or menus (everything else: character and stage select, HUD, pause screen).
inline Surface look_surface(const LightweightFilter& e, const DrawCall& d) {
  if (d.owner_player == kGameplayOwner || d.owner_player == kModelEffectOwner) return Surface::Textured;
  const LookLive& l = look_live();
  if (e.in_match && d.xf_regs[0x26] == 0 && std::memcmp(e.projection, &d.xf_regs[0x20], sizeof e.projection) == 0)
    return treated((Surface)(d.skinned ? l.fighter.load(std::memory_order_relaxed) : l.stage.load(std::memory_order_relaxed)));
  return treated((Surface)l.menus.load(std::memory_order_relaxed));
}
// Lines instead of triangles: the Lightweight wireframe level, or a Wire surface for this draw's group.
inline bool look_wire(const LightweightFilter& e, const DrawCall& d) {
  return lightweight_wire(e, d) || (e.in_match && look_surface(e, d) == Surface::Wire &&
                                    d.xf_regs[0x26] == 0 && std::memcmp(e.projection, &d.xf_regs[0x20], sizeof e.projection) == 0);
}
// Nearest-texel sampling for this draw (the 64 look): only where a treatment applies.
inline bool look_unfiltered(const LightweightFilter& e, const DrawCall& d) {
  return look_live().unfiltered.load(std::memory_order_relaxed) && look_surface(e, d) != Surface::Textured;
}
// The sampler mode with linear filtering turned off (mag bit 4, min "linear" bit 7 of mode0).
inline uint32_t unfiltered_mode0(uint32_t mode0) { return mode0 & ~((1u << 4) | (1u << 7)); }
// The background clear in a match: the game's colour, black, or the plain colour (alpha kept).
inline uint32_t look_clear_colour(bool in_match, uint32_t colour) {
  const LookLive& l = look_live();
  if (in_match && arcade_backdrop_slot().load(std::memory_order_relaxed) != 0)   // the Arcade look's own colour wins
    return (colour & 0xFF000000u) | (l.arcade_rgb.load(std::memory_order_relaxed) & 0xFFFFFF);
  const uint8_t b = l.background.load(std::memory_order_relaxed);
  if (!in_match || b == 0) return colour;
  const uint32_t rgb = b == 1 ? 0 : (l.background_rgb.load(std::memory_order_relaxed) & 0xFFFFFF);
  return (colour & 0xFF000000u) | rgb;
}

}  // namespace gx
