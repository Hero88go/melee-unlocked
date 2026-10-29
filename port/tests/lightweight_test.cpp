// "Lightweight" ladder: which draws the stage-background rule skips, and that stepping the ladder
// up to the image tiers and back gives the player's own settings back.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_core.h"
#include "render_options.h"
#include "gx_retro.h"
#include "hle/online_frame_budget.h"

#include <cstdio>
#include <cstdlib>

namespace gx {
// The live counters live in gx_core.cpp, which this test does not link.
void lightweight_count(bool) {}
void lightweight_publish_counts() {}
void lightweight_debug(const Frame&, const LightweightFilter&) {}
int8_t& lightweight_skip_slot(const Frame&, size_t, int) { static int8_t s; s = -1; return s; }
}  // namespace gx

static int g_failures = 0;
static void check(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "lightweight test failed: %s\n", message); ++g_failures; }
}

// A camera at view-space origin looking down -z. Every draw uses position matrix row 0: identity
// rotation and a translation of (0, 0, tz), so a vertex at model z lands at view z = z + tz.
static gx::DrawCall draw(float tz, uint32_t first, uint32_t count, uint8_t owner, bool skinned, uint32_t camera_word) {
  gx::DrawCall d{};
  for (int r = 0; r < 3; ++r) d.posMatrices[r * 4 + r] = 1.0f;
  d.posMatrices[2 * 4 + 3] = tz;
  d.first_vertex = first; d.vertex_count = count;
  d.owner_player = owner; d.skinned = skinned;
  d.xf_regs[0x20] = camera_word;   // the world camera's projection word
  return d;
}

int main() {
  constexpr float kCamera = -300.0f;   // the fighters stand 300 units in front of the camera
  gx::Frame f;
  f.scene_major = 0x02; f.scene_minor = 0x02;   // a VS match
  f.vertices.resize(12);
  for (auto& v : f.vertices) { v.pos[0] = v.pos[1] = v.pos[2] = 0; v.posmtx = 0; }
  // Two vertices of the platform reach 10 units back; the backdrop is wholly 500 units back.
  f.vertices[1].pos[2] = -10.0f;
  f.draws.push_back(draw(kCamera, 0, 3, 0, true, 0x1234));            // 0: fighter
  f.draws.push_back(draw(kCamera, 0, 3, 0xFF, false, 0x1234));        // 1: platform (stage)
  f.draws.push_back(draw(kCamera - 500.0f, 3, 3, 0xFF, false, 0x1234)); // 2: backdrop (stage)
  f.draws.push_back(draw(kCamera - 500.0f, 6, 3, 0xFF, false, 0x9999)); // 3: same depth, another camera (HUD)
  f.draws.push_back(draw(kCamera - 500.0f, 9, 3, 0, false, 0x1234));    // 4: same depth, owned by a player (item)
  f.draws.push_back(draw(kCamera - 40.0f, 0, 3, 0xFF, false, 0x1234));  // 5: 40 back: inside the margin, kept

  const gx::LightweightFilter off = gx::lightweight_filter(f, 0);
  check(!gx::skip_for_lightweight(off, f, f.draws[2]), "level 0 skips nothing");

  const gx::LightweightFilter one = gx::lightweight_filter(f, 1);
  check(!gx::skip_for_lightweight(one, f, f.draws[2]), "level 1 keeps the backdrop");

  const gx::LightweightFilter two = gx::lightweight_filter(f, 2);
  check(two.have_fighters, "level 2 finds the fighter");
  check(!gx::skip_for_lightweight(two, f, f.draws[0]), "the fighter is kept");
  check(!gx::skip_for_lightweight(two, f, f.draws[1]), "the platform is kept");
  check(gx::skip_for_lightweight(two, f, f.draws[2]), "the backdrop is skipped");
  check(!gx::skip_for_lightweight(two, f, f.draws[3]), "a draw of another camera (the HUD) is kept");
  check(!gx::skip_for_lightweight(two, f, f.draws[4]), "a draw a player owns is kept");
  check(!gx::skip_for_lightweight(two, f, f.draws[5]), "scenery near the play plane is kept");

  const gx::LightweightFilter five = gx::lightweight_filter(f, 5);
  check(!gx::skip_for_lightweight(five, f, f.draws[1]), "level 5 keeps the platform");
  check(gx::skip_for_lightweight(five, f, f.draws[5]), "level 5 drops scenery 40 units behind the play plane");
  check(!gx::lightweight_flat(two, f.draws[1]) && gx::lightweight_flat(gx::lightweight_filter(f, 3), f.draws[1]), "flat from level 3");
  check(!gx::lightweight_flat(gx::lightweight_filter(f, 3), f.draws[3]), "flat never reaches the HUD");
  check(gx::lightweight_wire(gx::lightweight_filter(f, 5), f.draws[0]) && !gx::lightweight_wire(gx::lightweight_filter(f, 4), f.draws[0]), "wire at level 5 only");
  check(!gx::lightweight_wire(gx::lightweight_filter(f, 5), f.draws[3]), "wire never reaches the HUD");
  check(gx::lightweight_effects_level(0, 3) == 2 && gx::lightweight_effects_level(1, 2) == 1, "effects held at Minimal from level 3");

  // The optional particle tier removes only the effect objects' draws: fighters with their shields,
  // items, the stage and the HUD stay.
  {
    gx::DrawCall smoke = f.draws[2];
    smoke.bp.reg[gx::BP_BLENDMODE] = 1 | (5 << 5); // ordinary alpha blend
    smoke.bp.reg[gx::BP_ZMODE] = 1;               // depth test, no depth writes
    smoke.owner_player = gx::kEffectOwner;
    check(!gx::skip_for_effects(gx::effects_filter(f, 2), smoke), "Minimal keeps alpha smoke");
    check(gx::skip_for_effects(gx::effects_filter(f, 3), smoke), "particle tier removes effect smoke");
    gx::DrawCall shield = smoke; shield.owner_player = 0;
    check(!gx::skip_for_effects(gx::effects_filter(f, 3), shield), "particle tier keeps a fighter's shield");
    gx::DrawCall item = smoke; item.owner_player = 0xFF;
    check(!gx::skip_for_effects(gx::effects_filter(f, 3), item), "particle tier keeps items and the stage");
    gx::DrawCall projectile = smoke; projectile.owner_player = gx::kGameplayOwner;
    projectile.bp.reg[gx::BP_BLENDMODE] = 1 | (1 << 5); // additive projectile or reflector
    projectile.bp.reg[gx::BP_ZMODE] = 0;
    for (int level = 1; level <= 3; ++level)
      check(!gx::skip_for_effects(gx::effects_filter(f, level), projectile), "all effect levels keep gameplay visuals");
    check(!gx::lightweight_flat(five, projectile) && !gx::lightweight_wire(five, projectile), "lowest levels keep projectile shape");
    check(gx::look_surface(five, projectile) == gx::Surface::Textured, "gameplay visuals keep their original textures");
    gx::DrawCall model_effect = smoke; model_effect.owner_player = gx::kModelEffectOwner;
    for (int level = 1; level <= 3; ++level)
      check(!gx::skip_for_effects(gx::effects_filter(f, level), model_effect), "model effects retain current visual-effects behavior");
    check(!gx::lightweight_flat(five, model_effect) && !gx::lightweight_wire(five, model_effect), "model effects retain current look behavior");
    check(gx::look_surface(five, model_effect) == gx::Surface::Textured, "model effects retain their current textures");
    check(!gx::lightweight_flat(five, smoke), "a blended particle keeps its texture's shape");
    smoke.xf_regs[0x20] = 0x9999;
    check(!gx::skip_for_effects(gx::effects_filter(f, 3), smoke), "particle tier keeps another camera's HUD");
    gx::RenderOptions particles; particles.effects_level = 3; gx::publish_look(particles);
    check(gx::look_live().effect_owners.load(), "the particle tier turns effect owners on");
    gx::publish_look(gx::RenderOptions{});
    check(!gx::look_live().effect_owners.load(), "and off again below it");
    gx::RenderOptions white; white.look_stage = 3; gx::publish_look(white);
    smoke.xf_regs[0x20] = 0x1234;
    check(gx::look_surface(two, smoke) == gx::Surface::White, "the chosen white look applies to smoke too");
  }
  // White and Flat recolour a texture but keep its alpha: one opaque texel made smoke, sparks and
  // cut-out shapes solid squares over the fighters.
  {
    auto snap = std::make_shared<gx::TextureSnapshot>();
    snap->image.resize(32);   // IA8 4x4 (one tile): alpha then intensity per texel
    for (int i = 0; i < 16; ++i) { snap->image[i * 2] = i < 8 ? 0 : 255; snap->image[i * 2 + 1] = 100; }
    gx::TextureRef smoke_tex; smoke_tex.width = smoke_tex.height = 4; smoke_tex.format = 3; smoke_tex.data = snap;
    for (gx::Surface s : {gx::Surface::White, gx::Surface::Flat}) {
      const auto r = gx::look_replacement(smoke_tex, s);
      const uint8_t want = s == gx::Surface::White ? 255 : 100;
      bool shaped = r && r->width == 4 && r->height == 4 && r->pixels.size() == 64;
      for (int i = 0; shaped && i < 16; ++i)
        shaped = r->pixels[i * 4] == want && r->pixels[i * 4 + 1] == want && r->pixels[i * 4 + 3] == (i < 8 ? 0 : 255);
      check(shaped, s == gx::Surface::White ? "the white look keeps a texture's alpha" : "the flat look keeps a texture's alpha");
    }
    for (int i = 0; i < 16; ++i) snap->image[i * 2] = 255;
    const auto opaque = gx::look_replacement(smoke_tex, gx::Surface::White);
    check(opaque && opaque->width == 1 && opaque->pixels == std::vector<uint8_t>({255, 255, 255, 255}), "an opaque texture is still one white texel");
  }

  // Online match speed protection judges the frame pacer's deadlines, never single slow frames.
  {
    using Budget = slippi::online::FrameBudget;
    const unsigned W = Budget::kWindow;
    // A computer that keeps up, with a late frame after every rollback or hitch: never touched.
    Budget keeps;
    for (unsigned i = 0; i < 60 * 60 * 8; ++i) keeps.update(true, true, true, i % 4 == 0 || i % 7 == 0);
    check(!keeps.suspended(), "rollback and hitch frames never pause sub-frames on a computer that keeps up");
    Budget burst;
    for (unsigned i = 0; i < 60 * 60; ++i) burst.update(true, true, true, (i % 600) < 120);
    check(!burst.suspended(), "two seconds of lateness (a background task) does not pause sub-frames");
    // Falling behind with sub-frames on, keeping up without them: paused for the rest of the match.
    Budget slow;
    Budget::Change c = Budget::Change::None;
    for (unsigned i = 0; i < W - 1; ++i) c = slow.update(true, true, true, true);
    check(c == Budget::Change::None && !slow.suspended(), "a window not yet full never pauses");
    check(slow.update(true, true, true, true) == Budget::Change::Suspend, "a full window of late frames pauses sub-frames");
    for (unsigned i = 0; i < 60 * 60 * 5; ++i) c = slow.update(true, true, false, i < 30);
    check(slow.suspended() && slow.state() == Budget::State::Kept, "pausing fixed it: held for the match, no switching back and forth");
    // Still behind without sub-frames: they were not the cause and come back on, once, for good.
    Budget other;
    for (unsigned i = 0; i < W; ++i) other.update(true, true, true, true);
    unsigned resumes = 0, suspends = 0;
    for (unsigned i = 0; i < 60 * 60 * 5; ++i) {
      const bool sub = !other.suspended();
      const auto r = other.update(true, true, sub, true);
      resumes += r == Budget::Change::Resume; suspends += r == Budget::Change::Suspend;
    }
    check(resumes == 1 && suspends == 0 && !other.suspended() && other.state() == Budget::State::Done,
          "pausing did not help: sub-frames return and stay on for the match");
    // Off in settings, or the match ends: sub-frames are back at once.
    Budget off;
    for (unsigned i = 0; i < W; ++i) off.update(true, true, true, true);
    check(off.update(false, true, false, true) == Budget::Change::Resume && !off.suspended(), "turning the setting off gives sub-frames back");
    for (unsigned i = 0; i < W * 4; ++i) off.update(false, true, true, true);
    check(!off.suspended(), "with the setting off nothing is ever paused");
    Budget ends;
    for (unsigned i = 0; i < W; ++i) ends.update(true, true, true, true);
    check(ends.update(true, false, false, false) == Budget::Change::Resume && ends.state() == Budget::State::Watching, "the next match starts fresh");
    Budget menus;
    for (unsigned i = 0; i < W * 4; ++i) menus.update(true, false, true, true);
    check(!menus.suspended(), "menus and results never pause sub-frames");
    Budget capped;
    for (unsigned i = 0; i < W * 4; ++i) capped.update(true, true, false, true);
    check(!capped.suspended(), "with sub-frames already off (a 60 cap) there is nothing to pause");
  }

  // Outside a match (the character select) nothing changes at any level.
  gx::Frame menu = f; menu.scene_minor = 0x00;
  check(!gx::skip_for_lightweight(gx::lightweight_filter(menu, 5), menu, menu.draws[2]), "menus are never touched");

  // With no fighter in view nothing is skipped (there is no play plane to measure from).
  gx::Frame empty = f; empty.draws.erase(empty.draws.begin());
  check(!gx::skip_for_lightweight(gx::lightweight_filter(empty, 2), empty, empty.draws[1]), "no fighter, no skipping");

  // The ladder: up to 4 holds the cheapest image, back down gives the player's settings back.
  gx::RenderOptions o;
  o.api = gx::RenderApi::D3D12; o.fps_cap = 144; o.efb_scale = 3; o.ssaa = 2; o.anisotropy = 16; o.dlss_mode = 1;
  o.subframe = gx::SubFrameMode::AuthoredInterpolate;
  gx::apply_lightweight(o, 2, true);
  check(o.lightweight == 2 && o.efb_scale == 3 && o.fps_cap == 144, "level 2 leaves the image settings alone");
  gx::apply_lightweight(o, 4, false);
  check(o.low_spec && o.efb_scale == 1 && o.ssaa == 1 && o.fps_cap == 144 && o.subframe == gx::SubFrameMode::AuthoredInterpolate, "level 4 lowers the image, keeps the cap and sub-frames");
  gx::apply_lightweight(o, 5, false);
  check(o.fps_cap == 0 && o.efb_scale == 1 && o.subframe != gx::SubFrameMode::Off, "level 5 lifts the cap with sub-frames on");
  gx::apply_lightweight(o, 4, false);
  check(o.fps_cap == 144 && o.subframe == gx::SubFrameMode::AuthoredInterpolate, "back to 4 puts the player's cap back");
  gx::apply_lightweight(o, 0, false);
  check(!o.low_spec && o.efb_scale == 3 && o.ssaa == 2 && o.fps_cap == 144 && o.dlss_mode == 1 &&
        o.subframe == gx::SubFrameMode::AuthoredInterpolate && o.api == gx::RenderApi::D3D12, "level 0 restores the player's settings");
  gx::apply_lightweight(o, 9, false);
  check(o.lightweight == 5, "levels are clamped");
  gx::RenderOptions capped_white;
  capped_white.fps_cap = 144; capped_white.subframe = gx::SubFrameMode::Off;
  gx::apply_look_preset(capped_white, 8, false);
  check(capped_white.fps_cap == 0 && capped_white.subframe != gx::SubFrameMode::Off,
        "the cap-lifted white look enables its promised in-between frames");

  // Settings files from before the ladder.
  gx::RenderOptions old_poly; old_poly.low_poly_fighters = 1; gx::migrate_lightweight(old_poly);
  check(old_poly.lightweight == 1 && old_poly.low_poly_fighters == 0, "lowpoly 1 becomes level 1");
  gx::RenderOptions old_spec; old_spec.low_spec = true; gx::migrate_lightweight(old_spec);
  check(old_spec.lightweight == 0 && old_spec.low_spec, "lowspec 1 keeps the game's look and its kept settings");
  gx::RenderOptions moved; moved.low_spec = true; moved.lightweight = 4; gx::migrate_lightweight(moved);
  check(moved.lightweight == 0 && moved.low_spec, "a 0.9.60 file moved to level 4 gets its look back");
  gx::RenderOptions potato; potato.low_spec = true; potato.lightweight = 4; potato.look_fighter = 3; gx::migrate_lightweight(potato);
  check(potato.lightweight == 4 && potato.low_spec, "a picked look stays");
  gx::RenderOptions picked; picked.low_spec = true; picked.lightweight = 4; gx::migrate_lightweight(picked, true);
  check(picked.lightweight == 4 && picked.low_spec, "level 4 picked in a fixed build stays");

  // Which scenes are matches (frame_in_match), from the game's own scene tables. The 1P modes'
  // first fights used to count as menus (B-526: pausing there lost the smooth pause zoom).
  auto in_match = [](uint8_t major, uint8_t minor) { gx::Frame s; s.scene_major = major; s.scene_minor = minor; return gx::frame_in_match(s); };
  check(in_match(0x02, 0x02) && !in_match(0x02, 0x00) && !in_match(0x02, 0x01), "VS: match from minor 2");
  check(in_match(0x03, 0x01) && in_match(0x03, 0x09) && in_match(0x03, 0x51), "Classic: every fight, the first included");
  check(!in_match(0x03, 0x00) && !in_match(0x03, 0x08) && !in_match(0x03, 0x70) && !in_match(0x03, 0x69), "Classic: splashes, game over and the CSS are not");
  check(in_match(0x04, 0x01) && in_match(0x04, 0x53) && in_match(0x04, 0x5C), "Adventure: fights, the first included");
  check(!in_match(0x04, 0x00) && !in_match(0x04, 0x02) && !in_match(0x04, 0x52) && !in_match(0x04, 0x70), "Adventure: intros, cutscenes and the CSS are not");
  check(in_match(0x05, 0x00) && in_match(0x05, 0x01) && in_match(0x05, 0x60) && !in_match(0x05, 0x70), "All-Star: rest area and fights, not the CSS");
  check(in_match(0x0F, 0x01) && !in_match(0x0F, 0x00), "Target Test: the stage, not the CSS");
  check(in_match(0x21, 0x01) && in_match(0x26, 0x01) && in_match(0x20, 0x01) && !in_match(0x21, 0x00), "Multi-Man and Home-Run: the stage");
  check(in_match(0x1F, 0x02) && !in_match(0x1F, 0x01), "Special Melee: match from minor 2");
  check(in_match(0x08, 0x02) && in_match(0x2B, 0x01) && !in_match(0x01, 0x00), "online, events and menus as before");

  // Looks: texture treatments and presets.
  {
    const std::vector<uint8_t> two = {255, 0, 0, 255,  0, 0, 255, 255,  0, 255, 0, 0,  0, 255, 0, 0};   // red, blue, 2 clear
    const std::vector<uint8_t> mean = gx::flat_colour(two);
    check(mean.size() == 4 && mean[0] == 128 && mean[1] == 0 && mean[2] == 128 && mean[3] == 128, "flat colour: alpha-weighted mean, mean alpha");
    std::vector<uint8_t> px = {10, 100, 200, 77};
    gx::posterize(px);
    check(px[0] == 0 && px[1] == 85 && px[2] == 255 && px[3] == 77, "posterise: four levels, alpha kept");
    check(gx::look_key(42, gx::Surface::Textured) == 42 && gx::look_key(42, gx::Surface::Flat) != gx::look_key(42, gx::Surface::Retro64),
          "each treatment has its own cache key");
    int n = 0; const gx::LookPreset* presets = gx::look_presets(&n);
    bool all = n >= 8;
    for (int i = 0; i < n; ++i) {
      gx::RenderOptions o; o.fps_cap = 144;
      gx::apply_look_preset(o, i, false);
      if (gx::look_preset_of(o) != i) { std::fprintf(stderr, "preset %s reads back as %d (custom)", presets[i].name, gx::look_preset_of(o)); std::fputc(10, stderr); all = false; }
    }
    check(all, "every preset reads back as itself");
    gx::RenderOptions custom; gx::apply_look_preset(custom, 5, false); custom.look_background = 2;
    check(gx::look_preset_of(custom) == -1, "a changed control reads as Custom");
  }

  if (g_failures) return 1;
  std::puts("lightweight test ok");
  return 0;
}
