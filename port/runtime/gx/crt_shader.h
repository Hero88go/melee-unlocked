// CRT display model for the present pass, shared by the D3D12 and D3D11 backends.
//
// It models the tube instead of drawing lines over the picture:
//  - The picture is rebuilt from the console's own raster (640 columns, the lines the game sent to
//    the display), whatever the internal resolution: each scanline is the average of the rendered
//    rows that belong to it, band-limited along the line the way the video signal is.
//  - Each scanline is a beam with a Gaussian profile whose width grows with brightness, per colour
//    channel, and whose light is conserved (a thin beam is brighter at its centre), so mid tones keep
//    their brightness and highlights fill in between the lines. All of it is done in linear light.
//  - The beam is integrated over the height of an output pixel, and its contrast eases off when
//    the display has too few pixels per scanline to show it. That is what keeps moire out at 1080p.
//  - The phosphor mask is laid on the display's own pixels: RGB stripes when there are three or more
//    pixels per scanline, a two-pixel magenta/green mask below that. Its light loss is given back.
//  - A little of the light spreads in the glass (halation), and the tube can be curved, with a
//    soft edge and a slight fall-off toward the corners.
//
// The including shader defines, before this text:
//   rect (xy = uv scale, zw = uv offset of the picture in the source), sharp (xy = source texel size),
//   crt  (x = scanline depth 0..1, y = mask depth 0..1, z = curvature, w = scanlines in the picture),
//   crt2 (x = columns, y = output pixels per scanline, z = halation 0..1, w = raster shift in scanlines),
//   float3 crt_tap(float2 uv): the display-encoded picture at a source uv.
// 16 picture taps for the two nearest scanlines and 4 for the halation.
#pragma once

namespace gx {

inline constexpr const char* kCrtShader = R"(
float3 crt_linear(float3 c) { return pow(saturate(c), 2.4); }
// Taps stay inside the picture: the frame buffer holds rows below and beside it that are not part
// of the frame (old contents), and the edge scanlines and the halation would pull them in.
float3 crt_at(float2 p) {
  float2 half_texel = 0.5 * sharp.xy / rect.xy;
  return crt_linear(crt_tap(clamp(p, half_texel, 1.0 - half_texel) * rect.xy + rect.zw));
}
// One scanline's light at column position p.x: the rows of the rendered image inside the line,
// filtered along the line with a Gaussian about one column wide.
float3 crt_scanline(float2 p, float line_y, float2 cell) {
  static const float ox[4] = {-0.9, -0.3, 0.3, 0.9};
  static const float wx[4] = {0.262, 0.862, 0.862, 0.262};
  float3 acc = 0;
  for (int k = 0; k < 4; ++k) {
    float x = p.x + ox[k] * cell.x;
    acc += wx[k] * (crt_at(float2(x, line_y - 0.25 * cell.y)) + crt_at(float2(x, line_y + 0.25 * cell.y)));
  }
  return acc / (2.0 * 2.248);
}
// The beam's light at distance d (in scanlines) from its centre. Its width follows the brightness of
// each channel; dividing by the width keeps the light of a line the same however thin it is.
float3 crt_beam(float3 c, float d, float depth, float pixel) {
  float3 level = sqrt(saturate(c));
  float3 sigma = lerp(lerp(0.62, 0.24, depth), lerp(0.62, 0.43, depth), level);
  sigma = sqrt(sigma * sigma + pixel * pixel / 12.0);   // integrated over the output pixel's height
  return c * exp(-0.5 * d * d / (sigma * sigma)) / (sigma * 2.5066);
}
float3 crt_mask(float2 pos, float depth, float per_line) {
  if (depth <= 0.0) return 1.0;
  float3 m;
  if (per_line >= 2.8) {
    int x = (int)pos.x % 3;
    m = x == 0 ? float3(1, 0, 0) : (x == 1 ? float3(0, 1, 0) : float3(0, 0, 1));
    return lerp(1.0, m, depth) / (1.0 - depth * 2.0 / 3.0);
  }
  m = ((int)pos.x & 1) ? float3(0, 1, 0) : float3(1, 0, 1);
  return lerp(1.0, m, depth) / (1.0 - depth * 0.5);
}
// pos: the output pixel (SV_Position), uv: the picture's source uv at that pixel.
// Returns display-encoded colour; edge.x is 0 outside the tube face.
float3 crt_pixel(float2 pos, float2 uv, out float2 picture_uv) {
  float2 p = (uv - rect.zw) / rect.xy;
  float2 one_pixel = fwidth(p);
  float face = 1.0;
  if (crt.z > 0.0) {
    float2 q = p * 2.0 - 1.0;
    q *= 1.0 + crt.z * float2(q.y * q.y, q.x * q.x);
    q /= 1.0 + crt.z * 0.5;
    // Soft edge about two output pixels wide, and rounded corners.
    float2 px = 4.0 * one_pixel;
    float2 e = smoothstep(0.0, 1.0, (1.0 - abs(q)) / max(px, 1e-5));
    float2 corner = max(abs(q) - (1.0 - 0.045), 0.0) / 0.045;
    face = e.x * e.y * saturate((1.0 - length(corner)) / max(max(px.x, px.y) / 0.045, 1e-5));
    face *= 1.0 - 0.35 * crt.z * dot(q, q) * 0.5;
    p = q * 0.5 + 0.5;
  }
  picture_uv = p * rect.xy + rect.zw;
  if (face <= 0.0) return 0.0;
  float lines = max(crt.w, 1.0), per_line = max(crt2.y, 0.01);
  float2 cell = 1.0 / float2(max(crt2.x, 1.0), lines);
  // Too few output pixels per scanline cannot show the gaps: ease the beam back toward a flat line.
  float depth = crt.x * saturate(per_line - 1.0);
  float y = p.y * lines - 0.5 + crt2.w;
  float upper = floor(y), d = y - upper;
  float3 a = crt_scanline(p, (upper + 0.5) * cell.y, cell);
  float3 b = crt_scanline(p, (upper + 1.5) * cell.y, cell);
  float pixel = 1.0 / per_line;
  float3 c = crt_beam(a, d, depth, pixel) + crt_beam(b, 1.0 - d, depth, pixel);
  // Halation: light scattered in the glass, from the unmodulated picture around this point.
  if (crt2.z > 0.0) {
    float2 h = 1.6 * cell;
    float3 halo = 0.25 * (crt_at(p + float2(-h.x, -h.y)) + crt_at(p + float2(h.x, -h.y)) + crt_at(p + float2(-h.x, h.y)) + crt_at(p + h));
    c = lerp(c, max(c, halo), crt2.z);
  }
  c *= crt_mask(pos, crt.y, per_line);
  return pow(saturate(c * face), 1.0 / 2.2);
}
)";

// The picture's height on the display while the model is on. A scanline pitch that is not a whole
// number of pixels beats against the pixel grid (bands that crawl when the picture moves), and below
// four pixels per scanline the beam cannot be integrated finely enough to hide it, so the pitch is
// rounded down to whole pixels there: 960 rows for 480 lines on a 1080p display. `shift` moves the
// raster half a pixel when the pitch is even, so that a pixel row lies on each beam's centre (with
// two rows per line, both would otherwise sit equally far from it and show no lines at all).
struct CrtFit { float height, shift; };
inline CrtFit crt_fit(float height, float lines) {
  if (lines < 1.0f) return {height, 0.0f};
  float per_line = height / lines;
  if (per_line >= 2.0f && per_line < 4.0f) per_line = (float)(int)(per_line + 0.01f);
  const int whole = (int)(per_line + 0.5f);
  const bool even = per_line >= 2.0f && per_line - (float)whole < 0.01f && (float)whole - per_line < 0.01f && whole % 2 == 0;
  return {per_line * lines, even ? 0.5f / per_line : 0.0f};
}

// Scanline depth, mask depth, curvature and halation of each setting. 0 is off.
struct CrtLook { float scan, mask, curve, halation; };
inline CrtLook crt_look(int mode) {
  switch (mode) {
    case 1: return {0.85f, 0.30f, 0.00f, 0.06f};   // flat studio monitor: thin beam, fine stripes
    case 2: return {0.60f, 0.45f, 0.10f, 0.14f};   // home television: softer beam, curved glass, more glow
    default: return {0.0f, 0.0f, 0.0f, 0.0f};
  }
}

}  // namespace gx
