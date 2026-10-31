// The present pass (EFB to window) shared by the Direct3D 12 and Vulkan renderers: the averaging box,
// sharpening, HUD composite, ambient occlusion, colour sliders, border-art shadow, and the CRT model's
// tap and entry point (crt_shader.h holds the model itself). Constants: cbuffer C at b0, textures t0..t3,
// sampler s0; entry points VS, PS and PS_CRT.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace gx {

inline constexpr const char* kPresentShader = R"(
Texture2D src : register(t0); Texture2D efb : register(t1); Texture2D hudmask : register(t2); Texture2D<float> efb_depth : register(t3); SamplerState samp : register(s0);
// box.z > 0: DLSS HUD composite. Pixels a flat 2D draw wrote this frame (HUD, text, player tags) come
// from this frame's own render instead of DLSS, which rebuilds from history and ghosted the ticking
// timer and moving tags; NVIDIA's guide puts UI after DLSS. hudr maps output uv to EFB uv.
cbuffer C : register(b0) { float4 rect; float4 sharp; float4 box; float4 hudr; float4 color; float4 crt; float4 crt2; };  // rect: xy = uv scale, zw = uv offset; sharp: xy = texel size, z = amount; box: xy = taps per axis; color: x = brightness gain, y = contrast gain, z = vibrance gain
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O VS(uint id : SV_VertexID) { O o; float2 p = float2((id << 1) & 2, id & 2); o.pos = float4(p * float2(2,-2) + float2(-1,1), 0, 1); o.uv = p * rect.xy + rect.zw; return o; }
// Border art (crt2.w > 0): the picture casts a soft shadow on the art beside it, darkest at the
// picture's edge. crt = the picture's rectangle in window pixels, crt2.x = how far the shadow reaches.
float3 art_frame(float2 px, float3 c) {
  if (crt2.w <= 0.0) return c;
  float2 d = max(max(crt.xy - px, px - crt.zw), 0.0);
  float near = saturate(1.0 - length(d) / max(crt2.x, 1.0));
  return c * (1.0 - 0.88 * near * near);
}
// Downsampling: average the whole footprint of one output pixel (a box of taps x taps bilinear
// samples) instead of one bilinear sample, which at 3x or more would skip most of the rendered
// pixels (shimmering, jagged edges). This is what makes a 4x or 6x internal resolution look
// supersampled on a 1080p screen.
float4 downsample(float2 uv) {
  int nx = (int)box.x, ny = (int)box.y;
  if (nx <= 1 && ny <= 1) return src.Sample(samp, uv);
  float2 foot = sharp.xy * float2(nx, ny);   // footprint of one output pixel in uv
  float4 acc = 0;
  for (int y = 0; y < ny; ++y)
    for (int x = 0; x < nx; ++x)
      acc += src.Sample(samp, uv + (float2(x, y) + 0.5) / float2(nx, ny) * foot - 0.5 * foot);
  return acc / (nx * ny);
}
float4 hud(float2 uv, float4 c) {
  if (box.z <= 0.0) return c;
  float2 e = uv * hudr.xy + hudr.zw;
  if (box.w > 0.0) return float4(hudmask.Sample(samp, e).rrr, 1);   // MELEE_DEBUG_HUDMASK
  float m = hudmask.Sample(samp, e).r;
  return m > 0.0 ? lerp(c, efb.Sample(samp, e), saturate(m)) : c;
}
// Optional screen-space ambient occlusion approximation. Melee uses reversed depth (clear = 0),
// so nearer samples have larger depth. It darkens contact edges only and leaves cleared pixels
// alone. This is a lightweight display effect, not hardware ray tracing.
float ao(float2 uv) {
  if (color.w <= 0.0) return 1.0;
  float d = efb_depth.Sample(samp, uv).r;
  if (d <= 0.00001) return 1.0;
  uint depth_w, depth_h;
  efb_depth.GetDimensions(depth_w, depth_h);
  float2 px = 2.0 / max(float2(depth_w, depth_h), 1.0);
  float2 dirs[8] = {float2(-1,-1), float2(0,-1), float2(1,-1), float2(-1,0), float2(1,0), float2(-1,1), float2(0,1), float2(1,1)};
  float occ = 0.0;
  float bias = max(0.0015, d * 0.012);
  [unroll] for (int k = 0; k < 8; ++k) {
    float nd = efb_depth.Sample(samp, uv + dirs[k] * px).r;
    occ += nd > d + bias ? 1.0 : 0.0;
  }
  return saturate(1.0 - (occ * 0.125) * color.w);
}
// Brightness/contrast/vibrance: a display adjustment over the finished picture, HUD included (a
// brightness slider that left percentages at native brightness while dimming everything else would
// read as a bug, not a feature). Neutral at (1,1,1), so this is a no-op when nobody has touched it.
float3 grade(float3 c) {
  if (color.x == 1.0 && color.y == 1.0 && color.z == 1.0) return c;
  c *= color.x;
  c = (c - 0.5) * color.y + 0.5;
  float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
  c = lerp(luma.xxx, c, color.z);
  return saturate(c);
}
float3 path_to_display(float3 c) {
  c = saturate(c);
  return lerp(12.92 * c, 1.055 * pow(c, 1.0 / 2.4) - 0.055, step(0.0031308, c));
}
float4 PS(O i) : SV_Target {
  if (color.w < -1.5) {
    uint z = min((uint)((1.0 - src.Sample(samp, i.uv).r) * 16777216.0), 0xFFFFFFu);
    return float4(float3((z >> 16) & 255u, (z >> 8) & 255u, z & 255u) / 255.0, 1.0);
  }
  float2 depth_uv = i.uv * hudr.xy + hudr.zw;
  float4 scene = downsample(i.uv);
  scene.rgb *= ao(depth_uv);
  float4 c = hud(i.uv, scene);
  // color.w < 0: an EFB copy from a frame buffer without alpha, which reads as opaque on hardware.
  if (color.w < 0.0) return float4(c.rgb, 1.0);
  if (sharp.z <= 0.0) return float4(art_frame(i.pos.xy, grade(sharp.w > 0.5 ? path_to_display(c.rgb) : c.rgb)), c.a);
  // Contrast-adaptive sharpening (AMD CAS style): sharpen where local contrast allows it,
  // over neighbours one output pixel away.
  float2 step = sharp.xy * max(box.xy, 1.0);
  float3 n = src.Sample(samp, i.uv + float2(0, -step.y)).rgb, s = src.Sample(samp, i.uv + float2(0, step.y)).rgb;
  float3 w = src.Sample(samp, i.uv + float2(-step.x, 0)).rgb, e = src.Sample(samp, i.uv + float2(step.x, 0)).rgb;
  float3 mn = min(min(min(n, s), min(w, e)), c.rgb), mx = max(max(max(n, s), max(w, e)), c.rgb);
  float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1e-4)));
  float peak = -1.0 / lerp(8.0, 5.0, saturate(sharp.z));
  float3 wgt = amp * peak;
  float3 r = (c.rgb + (n + s + w + e) * wgt) / (1.0 + 4.0 * wgt);
  float4 sharpened = hud(i.uv, float4(saturate(r), c.a));
  return float4(grade(sharp.w > 0.5 ? path_to_display(sharpened.rgb) : sharpened.rgb), sharpened.a);
})";

inline constexpr const char* kPresentCrtTap = R"(
float3 crt_tap(float2 uv) {
  float4 c = src.SampleLevel(samp, uv, 0);
  if (box.z > 0.0) {
    float2 e = uv * hudr.xy + hudr.zw;
    float m = hudmask.SampleLevel(samp, e, 0).r;
    if (m > 0.0) c = lerp(c, efb.SampleLevel(samp, e, 0), saturate(m));
  }
  return sharp.w > 0.5 ? path_to_display(c.rgb) : c.rgb;
}
)";

inline constexpr const char* kPresentCrtMain = R"(
float4 PS_CRT(O i) : SV_Target {
  float2 picture_uv;
  float3 c = crt_pixel(i.pos.xy, i.uv, picture_uv);
  c *= ao(picture_uv * hudr.xy + hudr.zw);
  return float4(grade(c), 1.0);
})";

}  // namespace gx
