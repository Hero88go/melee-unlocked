// "Lightweight" ladder: which draws the stage-background rule skips, and that stepping the ladder
// up to the image tiers and back gives the player's own settings back.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_core.h"
#include "render_options.h"
#include "gx_retro.h"

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

  // Settings files from before the ladder.
  gx::RenderOptions old_poly; old_poly.low_poly_fighters = 1; gx::migrate_lightweight(old_poly);
  check(old_poly.lightweight == 1 && old_poly.low_poly_fighters == 0, "lowpoly 1 becomes level 1");
  gx::RenderOptions old_spec; old_spec.low_spec = true; gx::migrate_lightweight(old_spec);
  check(old_spec.lightweight == 4, "lowspec 1 becomes level 4");

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
