// SPDX-License-Identifier: GPL-2.0-or-later
// GD kit font adapter. Atlas, advances, kerning and glyph offsets are upstream
// data; see ORIGIN.md. Include after cosmetic_preview/ui_source_asset_path.
#include "font_metrics.h"

static const gd_font_data::Role& gd_kit_font(const char* name) {
  for (const auto& r : gd_font_data::roles)
    if (std::strcmp(r.name, name) == 0) return r;
  return gd_font_data::roles[1];
}

static unsigned gd_kit_codepoint(const char*& p) {
  unsigned c = (unsigned char)*p++;
  if (c < 128) return c;
  const int n = c >= 240 ? 3 : (c >= 224 ? 2 : 1);
  c &= n == 3 ? 7 : (n == 2 ? 15 : 31);
  for (int i = 0; i < n; ++i) {
    if (!*p || ((unsigned char)*p & 192) != 128) return '?';
    c = (c << 6) | ((unsigned char)*p++ & 63);
  }
  return c;
}

static const gd_font_data::Glyph* gd_kit_glyph(const gd_font_data::Role& r, unsigned cp) {
  if (r.caps && cp >= 'a' && cp <= 'z') cp -= 'a' - 'A';
  const auto find = [&](unsigned key) -> const gd_font_data::Glyph* {
    const auto* end = r.glyphs + r.glyph_count;
    const auto* g = std::lower_bound(r.glyphs, end, key,
        [](const gd_font_data::Glyph& glyph, unsigned value) { return glyph.cp < value; });
    return g != end && g->cp == key ? g : nullptr;
  };
  if (const auto* g = find(cp)) return g;
  return find('?');
}

static float gd_kit_kerning(const gd_font_data::Role& r, unsigned a, unsigned b) {
  const auto* end = r.kern + r.kern_count;
  const gd_font_data::Kern key{a,b,0};
  const auto* k = std::lower_bound(r.kern, end, key,
      [](const gd_font_data::Kern& lhs, const gd_font_data::Kern& rhs) {
        return lhs.a < rhs.a || (lhs.a == rhs.a && lhs.b < rhs.b);
      });
  return k != end && k->a == a && k->b == b ? k->value : 0.0f;
}

// Width is in the kit's 640x480 design units, before scaling/shear.
static float gd_kit_text_width(const char* role, const char* text) {
  const auto& r = gd_kit_font(role);
  float width = 0;
  unsigned prev = 0;
  for (const char* p = text; *p;) {
    const auto* g = gd_kit_glyph(r, gd_kit_codepoint(p));
    if (!g) continue;
    width += gd_kit_kerning(r, prev, g->cp) + g->advance;
    prev = g->cp;
  }
  return width;
}

// x/baseline/max_width are UNSHEARED kit coordinates. Origin is the screen
// position of kit (0,0). Matches upstream x' = x + (240-y)*0.25 per vertex.
static void gd_kit_text(ImDrawList* draw, const char* role, ImVec2 origin, float scale,
                        float x, float baseline, const char* text, ImU32 color,
                        float max_width = 10000.0f) {
  const auto& r = gd_kit_font(role);
  auto* atlas = cosmetic_preview(ui_source_asset_path("gd_melee", r.page));
  if (!atlas || scale <= 0 || max_width <= 0) return;
  std::vector<const gd_font_data::Glyph*> glyphs;
  float width = 0;
  unsigned prev = 0;
  const auto* ellipsis = gd_kit_glyph(r, 0x2026);
  for (const char* p = text; *p;) {
    const auto* g = gd_kit_glyph(r, gd_kit_codepoint(p));
    if (!g) continue;
    float step = gd_kit_kerning(r, prev, g->cp) + g->advance;
    if (width + step > max_width) {
      if (ellipsis) {
        while (!glyphs.empty()) {
          const unsigned before = glyphs.size() > 1 ? glyphs[glyphs.size()-2]->cp : 0;
          width -= glyphs.back()->advance + gd_kit_kerning(r, before, glyphs.back()->cp);
          glyphs.pop_back();
          const unsigned last = glyphs.empty() ? 0 : glyphs.back()->cp;
          if (width + gd_kit_kerning(r, last, ellipsis->cp) + ellipsis->advance <= max_width) break;
        }
        if (ellipsis->advance <= max_width) glyphs.push_back(ellipsis);
      }
      break;
    }
    glyphs.push_back(g); width += step; prev = g->cp;
  }
  const auto point = [&](float px, float py) {
    return ImVec2(origin.x + (px + (240.0f-py)*0.25f)*scale, origin.y + py*scale);
  };
  float pen = x;
  prev = 0;
  for (const auto* g : glyphs) {
    pen += gd_kit_kerning(r, prev, g->cp);
    if (g->w > 0 && g->h > 0) {
      const float gx = pen+g->x, gy = baseline+g->y;
      draw->AddImageQuad(atlas->GetTexRef(), point(gx,gy), point(gx+g->w,gy),
          point(gx+g->w,gy+g->h), point(gx,gy+g->h), ImVec2(g->u0,g->v0),
          ImVec2(g->u1,g->v0), ImVec2(g->u1,g->v1), ImVec2(g->u0,g->v1), color);
    }
    pen += g->advance; prev = g->cp;
  }
}
