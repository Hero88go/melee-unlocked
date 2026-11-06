// The Melee integrator, first stage (rtx_melee_integrator.cpp): every G-buffer surface lit by the
// game's own lights with ray-traced soft shadows and GGX highlights, and its ambient light shadowed
// by ray-traced ambient occlusion. One sample per pixel per frame; DLSS (and later Ray
// Reconstruction) gathers them over time. Surfaces the G-buffer does not hold (the sky) keep the
// raster picture, and what the raster drew over a surface (the HUD, glows, smoke, blended leaves)
// goes back on top through the overlay: lit * transmittance + overlay.
// SPDX-License-Identifier: GPL-2.0-or-later

struct GeometryRecord {    // rtx_scene.h RtxGeometryRecord
  uint first_vertex;
  uint triangle_count;
  uint draw_index;
  int material;
  uint texture_set;
  uint flags;
  uint specular;
  float roughness;
  uint ambient;            // GX ambient register of channel 0, RGBA8 (r low)
  uint channel;            // 1 lit, 2 ambient from vertex, 4 material from vertex
  uint light_first;        // the draw's own light set in Lights
  uint light_count;
};

struct Light {             // rtx_lights.h RtxLightRecord
  float3 position;
  uint type;               // 0 ambient, 1 directional, 2 point, 3 spot
  float3 direction;
  float cos_cutoff;
  float3 radiance;
  float radius;
  float3 attenuation;
  uint flags;
};

RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<Light> Lights : register(t1);
StructuredBuffer<uint> DrawRecords : register(t2);        // frame draw index -> record, ~0u none
StructuredBuffer<GeometryRecord> Records : register(t3);
Texture2D<float4> GAlbedo : register(t4);
Texture2D<float4> GNormal : register(t5);                 // view normal xyz, linear view depth w
Texture2D<uint> GId : register(t6);                       // draw index + 1
Texture2D<float4> Raster : register(t7);                  // the picture as GX drew it
Texture2D<float4> Overlay : register(t8);                 // what the raster drew over the surface
Texture2D<float4> Trans : register(t9);                   // and how much of the surface shows through it
RWTexture2D<float4> Output : register(u0);
RWTexture2D<float4> GuideDiffuse : register(u1);
RWTexture2D<float4> GuideSpecular : register(u2);
RWTexture2D<float4> GuideNormal : register(u3);
RWTexture2D<float4> Accum : register(u4);                 // photo mode: the picture so far while the scene is still

cbuffer Frame : register(b0) {
  float4 view_to_world[3];
  float4 clip_to_view[4];
  float4 viewport;           // the scene's x, y, w, h in EFB pixels
  uint2 size;
  uint frame_index;
  uint light_count;
  uint draw_count;
  uint orthographic;
  float ao_radius;           // world units the ambient occlusion rays reach
  float light_scale;         // the game's light colours to radiance
  uint main_light_count;     // the main light set: Lights[0 .. main_light_count)
  float look;                // 0 Faithful (GX's lighting with ray-traced visibility) .. 1 Remastered
  uint switches;             // 1 shadows, 2 ambient occlusion, 4 fighters' shadows on baked stages
  uint still_frames;         // photo mode: presents the scene has stood still (0 moving or off)
  float reflection_strength; // Video > Ray tracing strengths, 1 = the Remastered look
  float floor_strength;
  float shadow_strength;
  float bounce_strength;
};
static const uint kShadows = 1u, kAmbientOcclusion = 2u, kFighterShadows = 4u, kBounce = 8u, kReflections = 16u, kSkin = 32u,
                  kStageShade = 64u, kGlossyFloors = 128u;

static const float kPi = 3.14159265;
static const uint kNone = 0xFFFFFFFFu;

uint hash32(uint x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return x;
}
float rand01(inout uint state) {
  state = hash32(state + 0x9e3779b9u);
  return (state & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

float3 srgb_to_linear(float3 c) {
  return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
}
float3 linear_to_srgb(float3 c) {
  c = max(c, 0.0);
  return lerp(c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055, step(0.0031308, c));
}

float3 unproject(float2 ndc, float z) {
  const float4 c = float4(ndc, z, 1);
  const float4 v = float4(dot(clip_to_view[0], c), dot(clip_to_view[1], c), dot(clip_to_view[2], c), dot(clip_to_view[3], c));
  return v.xyz / v.w;
}
float3 to_world_point(float3 p) {
  return float3(dot(view_to_world[0], float4(p, 1)), dot(view_to_world[1], float4(p, 1)), dot(view_to_world[2], float4(p, 1)));
}
float3 to_world_direction(float3 d) {
  return float3(dot(view_to_world[0].xyz, d), dot(view_to_world[1].xyz, d), dot(view_to_world[2].xyz, d));
}

// An orthonormal basis around n.
void basis(float3 n, out float3 t, out float3 b) {
  const float3 helper = abs(n.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
  t = normalize(cross(helper, n));
  b = cross(n, t);
}

float3 cosine_direction(float3 n, inout uint rng) {
  const float u = rand01(rng), v = rand01(rng);
  const float phi = 2.0 * kPi * u, r = sqrt(v);
  float3 t, b;
  basis(n, t, b);
  return normalize(t * (cos(phi) * r) + b * (sin(phi) * r) + n * sqrt(max(0.0, 1.0 - v)));
}

// A direction within `angle` radians of d, uniform over the cap.
float3 cone_direction(float3 d, float angle, inout uint rng) {
  const float cos_max = cos(angle);
  const float u = rand01(rng), v = rand01(rng);
  const float cos_t = lerp(1.0, cos_max, u), sin_t = sqrt(max(0.0, 1.0 - cos_t * cos_t));
  const float phi = 2.0 * kPi * v;
  float3 t, b;
  basis(d, t, b);
  return normalize(t * (cos(phi) * sin_t) + b * (sin(phi) * sin_t) + d * cos_t);
}

// True when nothing in `mask` lies between `origin` and `origin + dir * length`. Shadow casters are
// opaque, cut-out and fighter surfaces (0x0B); fighters alone are 0x08.
bool visible(float3 origin, float3 dir, float length, uint mask) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = dir;
  ray.TMin = 0.0;
  ray.TMax = length;
  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> q;
  q.TraceRayInline(Scene, RAY_FLAG_NONE, mask, ray);
  q.Proceed();
  return q.CommittedStatus() == COMMITTED_NOTHING;
}
static const uint kCasters = 0x0Bu, kFighterCasters = 0x08u;
static const float kBakedShadowReach = 250.0;   // painted surfaces take shadows from this close only
static const float kShadowFill = 0.2;           // what a blocked light still gives (sky and bounce)
// Shadow strength: how much of the light a shadow takes away (1 as designed, more is deeper).
// Above 1 the shadow deepens but keeps at least half of what it was designed to keep, so a
// character in shadow never goes black.
float darken(float kept) { return saturate(max(1.0 - (1.0 - kept) * shadow_strength, 0.5 * kept)); }
float3 darken3(float3 kept) { return saturate(max(1.0 - (1.0 - kept) * shadow_strength, 0.5 * kept)); }


float3 ggx_specular(float3 n, float3 v, float3 l, float roughness, float3 f0) {
  const float3 h = normalize(v + l);
  const float nl = saturate(dot(n, l)), nv = saturate(dot(n, v)), nh = saturate(dot(n, h)), vh = saturate(dot(v, h));
  if (nl <= 0.0 || nv <= 0.0) return 0;
  const float a = max(roughness * roughness, 0.002), a2 = a * a;
  const float d = a2 / (kPi * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0));
  const float k = a * 0.5;
  const float g = (nl / (nl * (1.0 - k) + k)) * (nv / (nv * (1.0 - k) + k));
  const float3 f = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
  return d * g * f / max(4.0 * nl * nv, 1e-4) * nl;
}

float3 unpack_rgb8(uint c) { return float3(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF) / 255.0; }
// Whether GX lit the draw with this light: its slot in the channel's light mask (rtx_scene.h channel
// bits 8-16, rtx_lights.h flags bits 24-31).
bool in_light_mask(uint channel, Light l) {
  if ((channel & 0x10000u) == 0u) return true;
  const uint slot = l.flags >> 24;
  return slot < 8u && ((channel >> (8u + slot)) & 1u) != 0u;
}

// ---------------------------------------------------------------- surfaces rays hit
struct RtVertex {          // shaders/rtx_transform.hlsl
  float3 position;
  float3 normal;
  uint color;
  uint flags;
  float2 uv[4];
};
StructuredBuffer<RtVertex> Vertices : register(t10);
Texture2D<float4> Textures[] : register(t0, space1);   // the renderer's whole shader-visible heap
SamplerState LinearWrap : register(s0);
static const uint kNoTextures = 0xFFFFFFFFu;
static const uint kGeometryNoNormals = 1u << 4;
static const uint kGeometryTintedReflection = 1u << 5;   // the env map's colour tints its reflection

struct Hit {
  bool found;
  float3 position;
  float3 normal;           // facing the ray
  float3 albedo;           // linear: texture 0 times the vertex colour (as GX drew it for baked draws)
  bool lit;                // GX lights it (else its light is baked into the albedo)
  float3 ambient;          // its GX ambient (linear)
};

float4 unpack_rgba8(uint word) { return float4(word & 0xFFu, (word >> 8) & 0xFFu, (word >> 16) & 0xFFu, word >> 24) / 255.0; }

Hit trace_surface(float3 origin, float3 dir, float tmax) {
  Hit h = (Hit)0;
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = dir;
  ray.TMin = 0.0;
  ray.TMax = tmax;
  RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
  q.TraceRayInline(Scene, RAY_FLAG_NONE, kCasters, ray);
  q.Proceed();
  if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return h;
  const GeometryRecord r = Records[q.CommittedInstanceID() + q.CommittedGeometryIndex()];
  const uint first = r.first_vertex + q.CommittedPrimitiveIndex() * 3;
  const RtVertex v0 = Vertices[first], v1 = Vertices[first + 1], v2 = Vertices[first + 2];
  const float2 bc = q.CommittedTriangleBarycentrics();
  const float3 w = float3(1.0 - bc.x - bc.y, bc.x, bc.y);
  float3 nrm = v0.normal * w.x + v1.normal * w.y + v2.normal * w.z;
  if ((r.flags & kGeometryNoNormals) != 0u || dot(nrm, nrm) < 1e-12)
    nrm = cross(v1.position - v0.position, v2.position - v0.position);
  nrm = normalize(nrm);
  if (dot(nrm, dir) > 0.0) nrm = -nrm;
  const float4 c = unpack_rgba8(v0.color) * w.x + unpack_rgba8(v1.color) * w.y + unpack_rgba8(v2.color) * w.z;
  const float2 uv = v0.uv[0] * w.x + v1.uv[0] * w.y + v2.uv[0] * w.z;
  float4 t = float4(1, 1, 1, 1);
  if (r.texture_set != kNoTextures) t = Textures[NonUniformResourceIndex(r.texture_set)].SampleLevel(LinearWrap, uv, 2);
  const bool vertex_colour = (r.channel & 4u) != 0u || (r.channel & 1u) == 0u;
  h.found = true;
  h.position = origin + dir * q.CommittedRayT();
  h.normal = nrm;
  h.albedo = srgb_to_linear(saturate((vertex_colour ? c.rgb : 1.0) * t.rgb));
  h.lit = (r.channel & 1u) != 0u;
  h.ambient = srgb_to_linear(unpack_rgb8(r.ambient));
  return h;
}

// The light leaving a hit toward the ray: a lit surface under the sun (one shadow ray) and its
// ambient, at the game's brightness; a baked surface as GX drew it.
float3 hit_radiance(Hit h, float3 sky, float3 sun, float3 sun_dir, float sun_radius, inout uint rng) {
  if (!h.found) return sky;
  if (!h.lit) return h.albedo;
  float3 light = h.ambient;
  const float3 to_sun = cone_direction(-sun_dir, sun_radius, rng);
  const float nl = dot(h.normal, to_sun);
  if (nl > 0.0 && any(sun > 0.0) && visible(h.position + h.normal * 0.05, to_sun, 1.0e6, kCasters)) light += sun * nl;
  const float3 total = h.ambient + sun;
  return h.albedo * light / max(1.0, max(total.r, max(total.g, total.b)));
}

// A GGX-distributed reflection direction around the mirror direction (visible normals would be
// better; this is the plain half-vector sample).
float3 glossy_direction(float3 n, float3 v, float roughness, inout uint rng) {
  const float a = max(roughness * roughness, 0.002);
  const float u = rand01(rng), w = rand01(rng);
  const float phi = 2.0 * kPi * u;
  const float cos_t = sqrt((1.0 - w) / (1.0 + (a * a - 1.0) * w)), sin_t = sqrt(max(0.0, 1.0 - cos_t * cos_t));
  float3 t, b;
  basis(n, t, b);
  const float3 h = normalize(t * (cos(phi) * sin_t) + b * (sin(phi) * sin_t) + n * cos_t);
  return reflect(-v, h);
}

// Ray Reconstruction's guides (rtx_integrator.h RtxFrameOutputs), at the render size: the surface's
// diffuse albedo, its specular albedo (the split-sum environment response), and its view-space
// normal with roughness (Streamline's packed layout; the renderer passes identity camera matrices,
// so view space is the space RR reads). Pixels with no surface get albedo 1 and no normal.
float3 specular_albedo(float3 f0, float nv, float roughness) {
  const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022), c1 = float4(1.0, 0.0425, 1.04, -0.04);
  const float4 r = roughness * c0 + c1;
  const float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
  const float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
  return saturate(f0 * ab.x + ab.y);
}
// Glossy stage floors: a polished floor (F0 0.10, roughness 0.10), the reflection up to half the
// picture at a glancing view.
static const float kFloorRoughness = 0.10;
float3 floor_fresnel(float3 n, float3 v) {
  return min(2.0 * floor_strength * specular_albedo(0.10, saturate(dot(n, v)), kFloorRoughness), min(0.5 * floor_strength, 0.9));
}
// How polished a painted floor reads from its colour (linear): 1 for dark, grey stone or metal,
// fading to 0 for bright or coloured ground (grass, felt, paper).
float polished_floor(float3 albedo) {
  const float lum = dot(albedo, float3(0.2126, 0.7152, 0.0722));
  const float sat = max(albedo.r, max(albedo.g, albedo.b)) - min(albedo.r, min(albedo.g, albedo.b));
  return saturate((0.30 - lum) / 0.15) * saturate((0.20 - sat) / 0.10);
}
// What the floor reflects: everything in the scene at any distance (fighters, the stage, effects,
// far scenery). A ray to empty sky keeps the floor's own colour (`own`): a mirror-strong floor under a
// night sky or space (Fountain of Dreams, Final Destination) would otherwise turn black.
float3 floor_reflection(float3 origin, float3 n, float3 v, float3 own, float3 ambient_light, float3 sun, float3 sun_dir, float sun_radius, inout uint rng) {
  const float3 dir = glossy_direction(n, v, kFloorRoughness, rng);
  const Hit h = trace_surface(origin, dir, 1.0e5);
  return h.found ? hit_radiance(h, ambient_light, sun, sun_dir, sun_radius, rng) : own;
}
// Fighters' shadows on the stage: from the stage's light direction lifted to about 57 degrees, so
// the shadow lands at their feet on every stage, with soft edges. 1 lit, else how much light is left.
float fighter_shadow(float3 origin, float3 n, float3 sun_dir, float look, inout uint rng) {
  const float3 d = -sun_dir;
  const float2 lean = length(d.xz) > 1e-4 ? normalize(d.xz) * 0.65 : float2(0.0, 0.0);
  const float3 dir = cone_direction(normalize(float3(lean.x, 1.0, lean.y)), 0.06, rng);
  if (dot(n, dir) <= 0.0) return 1.0;
  return visible(origin, dir, kBakedShadowReach, kFighterCasters) ? 1.0 : darken(lerp(0.6, 0.45, look));
}
void write_guides(uint2 px, float3 diffuse, float3 specular, float3 view_normal, float roughness) {
  GuideDiffuse[px] = float4(saturate(diffuse), 1);
  GuideSpecular[px] = float4(specular, 1);
  GuideNormal[px] = float4(view_normal, roughness);
}


// Out to the presenter, which takes linear light (it encodes for the display).
void emit(uint2 px, float3 lit, float3 albedo) {
  // Photo mode: a still scene averages every present's light (up to 256 presents), so the picture
  // refines to a clean, converged image while paused. The light is averaged apart from the surface
  // colour, so textures stay as sharp as the raster under the camera's sub-pixel jitter.
  const float3 base = max(albedo, 0.02);
  // A sample that came out invalid (NaN or infinite, rare at high strengths) or far out of range must
  // not enter photo mode's running average, where it would stay until the scene moves.
  float3 light = lit / base;
  if (any(isnan(light)) || any(isinf(light))) light = 1.0;
  light = min(light, 64.0);
  const float3 kept = Accum[px].rgb;
  const bool kept_ok = !any(isnan(kept)) && !any(isinf(kept));
  if (still_frames > 0u && kept_ok) light = lerp(kept, light, 1.0 / (float)(min(still_frames, 255u) + 1u));
  Accum[px] = float4(light, 1);
  // What the raster drew over the surface goes back on top, composed as GX blended it (encoded).
  const float3 shown = linear_to_srgb(light * base) * Trans[px].rgb + Overlay[px].rgb;
  Output[px] = float4(srgb_to_linear(shown), 1);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint2 px = id.xy;
  if (px.x >= size.x || px.y >= size.y) return;
  const uint draw = GId[px];
  const float4 nd = GNormal[px];
  if (draw == 0u || nd.w <= 0.0) {
    Output[px] = float4(srgb_to_linear(Raster[px].rgb), 1);
    write_guides(px, 1.0, 0.0, 0.0, 1.0);
    return;
  }

  // The surface: its view ray scaled to the G-buffer's linear depth.
  const float2 ndc = float2(((px.x + 0.5) - viewport.x) / viewport.z * 2.0 - 1.0,
                            1.0 - ((px.y + 0.5) - viewport.y) / viewport.w * 2.0);
  float3 view_pos;
  float3 view_dir;
  if (orthographic != 0) {
    const float3 a = unproject(ndc, 0.25);
    view_pos = float3(a.xy, -nd.w);
    view_dir = float3(0, 0, -1);
  } else {
    float3 d = normalize(unproject(ndc, 0.25));
    if (d.z > 0.0) d = -d;
    view_pos = d * (nd.w / max(-d.z, 1e-6));
    view_dir = d;
  }
  const float3 p = to_world_point(view_pos);
  // GX's normal lights the surface directly (as the game does: a part whose normals face away from
  // the camera stays dark); rays leave from the side the camera sees.
  const float3 v = -normalize(to_world_direction(view_dir));
  const float3 n_gx = normalize(to_world_direction(nd.xyz));
  const float3 n = dot(n_gx, v) < 0.0 ? -n_gx : n_gx;
  const float3 albedo = srgb_to_linear(GAlbedo[px].rgb);
  const bool shadows = (switches & kShadows) != 0u;
  uint rng = hash32(px.x + px.y * 4099u + frame_index * 0x9e3779b9u);
  const float3 origin = p + n * (0.02 + nd.w * 0.0004);   // off the surface, more so far away

  // The draw's material and its own light set (fighters keep theirs, the stage its own).
  float roughness = 0.85;
  float3 f0 = 0.04;
  uint first = 0, count = min(main_light_count, light_count);
  bool lit_by_gx = true, own_ambient = false;
  uint channel = 0;
  float3 ambient_gx = 0;   // GX's ambient register, as GX uses it (encoded)
  const uint record = draw - 1u < draw_count ? DrawRecords[draw - 1u] : kNone;
  if (record != kNone) {
    const GeometryRecord r = Records[record];
    roughness = r.roughness;
    f0 = unpack_rgb8(r.specular);
    if ((r.flags & kGeometryTintedReflection) != 0u) f0 = albedo * srgb_to_linear(f0);   // its own colour, tinted
    lit_by_gx = (r.channel & 1u) != 0u;
    own_ambient = (r.channel & 2u) == 0u;
    channel = r.channel;
    ambient_gx = unpack_rgb8(r.ambient);
    if (r.light_count) { first = r.light_first; count = r.light_count; }
  }

  write_guides(px, albedo, specular_albedo(f0, saturate(dot(n, v)), roughness), nd.xyz, roughness);

  // The set's ambient light and sun.
  float3 ambient_light = 0, sun = 0;
  float3 sun_dir = float3(0, -1, 0);
  float sun_radius = 0.03;
  for (uint j = first; j < first + count; ++j) {
    const Light l = Lights[j];
    if (l.type == 0u) ambient_light += l.radiance;
    if (l.type == 1u && (in_light_mask(channel, l) || !lit_by_gx)) { sun += l.radiance; sun_dir = l.direction; sun_radius = l.radius; }
  }

  if (!lit_by_gx) {
    // GX does not light this draw: its light is baked in, and it stays as drawn, except where a
    // fighter stands between it and the stage's sun (the stage's own shadows are already baked).
    float3 baked = albedo;
    // Stage shadows and contact shade: the stage's own geometry casts the sun's shadow too, and a
    // short ray finds the corners and contacts (softened: the painted light has some already).
    const bool stage_shade = shadows && (switches & kStageShade) != 0u && nd.w < 800.0;   // not far backgrounds
    if (stage_shade && (switches & kAmbientOcclusion) != 0u &&
        !visible(origin, cosine_direction(n, rng), ao_radius, kCasters))
      baked *= darken(lerp(0.85, 0.7, look));
    if (shadows && ((switches & kFighterShadows) != 0u || stage_shade) && any(sun > 0.0)) {
      const float3 to_sun = cone_direction(-sun_dir, sun_radius, rng);
      // Fighters' shadows reach kBakedShadowReach; the stage's own only its near parts (eaves,
      // platforms, ledges), not across to a far backdrop.
      const bool by_stage = stage_shade && !visible(origin, to_sun, 100.0, kCasters);
      if (dot(n, to_sun) > 0.0 && by_stage) {
        // As dark as the stage's own shade: GX's share of ambient in its light (Faithful, softened),
        // or the physical one (Remastered).
        const float3 amb_e = linear_to_srgb(ambient_light), sun_e = linear_to_srgb(sun);
        const float3 faithful = saturate(amb_e / max(amb_e + sun_e, 1e-3) + 0.25);
        const float3 remastered = max(saturate(ambient_light / max(ambient_light + sun, 1e-3)), 0.3);
        baked *= darken3(lerp(faithful, remastered, look));
      }
    }
    if (shadows && (switches & kFighterShadows) != 0u) baked *= fighter_shadow(origin, n, sun_dir, look, rng);
    if ((switches & kReflections) != 0u && roughness <= 0.3) {
      // The game faked this surface's shine with an environment map; a traced reflection of the
      // real scene goes over it.
      const float3 dir = glossy_direction(n, v, roughness, rng);
      const Hit h = trace_surface(origin, dir, 1.0e5);
      baked += 0.5 * reflection_strength * specular_albedo(f0, saturate(dot(n, v)), roughness) *
               hit_radiance(h, ambient_light, sun, sun_dir, sun_radius, rng);
    }
    if ((switches & kGlossyFloors) != 0u && (switches & kReflections) != 0u && n.y > 0.85 && nd.w < 800.0) {
      // Glossy stage floors on painted ground: only dark, grey floors are polished (Final
      // Destination's stone, metal decks). Grass, felt and paper (Yoshi's Story, Dream Land) stay
      // matte, or the mirror shows the cut-outs and trees through the ground.
      // The game made it shiny (an environment map: FoD's water, metal decks): polished at any colour.
      const float3 fresnel = floor_fresnel(n, v) * (roughness <= 0.3 ? 1.0 : polished_floor(albedo));
      if (fresnel.x > 0.0) {
        baked = baked * (1.0 - fresnel) + fresnel * floor_reflection(origin, n, v, baked, ambient_light, sun, sun_dir, sun_radius, rng);
        write_guides(px, albedo, fresnel, nd.xyz, kFloorRoughness);
      }
    }
    emit(px, baked, albedo);
    return;
  }

  const float3 ambient = own_ambient ? srgb_to_linear(ambient_gx) : ambient_light;
  // HSD lights a material with a white channel and gives it its own ambient colour (the material's
  // ambient times the scene's ambient light: state.c HSD_SetupRenderMode), so a fighter's skin tone
  // can live there rather than in its albedo. That colour over the ambient light is how the surface
  // answers light from everywhere, sky or bounced alike.
  const float3 ambient_reflect = own_ambient && any(ambient_light > 1e-3) ? saturate(ambient / max(ambient_light, 1e-3)) : 1.0;
  // The light from everywhere. Bounce light: one cosine-weighted ray; what it hits sends its own
  // light back (a lit surface under the sun, a baked one as drawn), what it misses the ambient sky.
  // Without bounce light, ambient occlusion: the ambient light where the ray reaches open sky within
  // ao_radius.
  float ao = 1.0;
  float3 indirect = ambient;
  float3 bounce_extra = 0;
  const float3 gather = cosine_direction(n, rng);
  if ((switches & kBounce) != 0u) {
    // The game's ambient is its light from everywhere: open sky gives exactly that, a blocked
    // contact ray takes some away (ambient occlusion), and a brighter surface the ray reaches adds
    // its colour (bounce), so a dark scene never darkens what the game shows lit.
    const Hit h = trace_surface(origin, gather, 1.0e5);
    const float3 from_hit = hit_radiance(h, ambient_light, sun, sun_dir, sun_radius, rng) * ambient_reflect;
    ao = h.found && length(h.position - p) < ao_radius ? 0.0 : 1.0;
    const float3 base = (switches & kAmbientOcclusion) != 0u ? ambient * lerp(0.45, 1.0, ao) : ambient;
    // Bounce strength scales what the bounce adds over the base (contact shade stays as it is).
    indirect = h.found ? base + max(lerp(base, from_hit, 0.6) - base, 0.0) * bounce_strength : ambient;
    if (h.found) bounce_extra = max(from_hit - ambient, 0.0) * bounce_strength;
  } else if ((switches & kAmbientOcclusion) != 0u) {
    ao = visible(origin, gather, ao_radius, kCasters) ? 1.0 : 0.0;
    indirect = ambient * lerp(0.45, 1.0, ao);
  }
  // Remastered: the path tracer's light (linear: irradiance with every ray-traced shadow, contact
  // shade and bounce, GGX highlights) taken as a ratio to the same light with nothing in the way,
  // applied to GX's own unshadowed lighting, so the game's brightness stays and only what the rays
  // change shows. Faithful: GX's own sum (encoded colours, clamped per channel), with the same
  // ray-traced visibility, and bounce light on top.
  // HSD keeps a fighter's skin and cloth tone in its ambient colour, so occlusion darkens that
  // light at most by half: faces keep their colour in the folds and under a cap.
  const bool fighter = record != kNone && (Records[record].flags & 2u) != 0u;
  if (fighter && own_ambient) indirect = max(indirect, 0.5 * ambient);
  float3 irradiance = indirect, irradiance_open = ambient, highlight = 0;
  const float3 gx_ambient = own_ambient ? ambient_gx : linear_to_srgb(ambient_light);
  float3 gx_sum = gx_ambient * (fighter ? max(ao, 0.5) : ao);
  float3 gx_open = gx_ambient;
  // Soft skin: light scatters under a fighter's surface, so it wraps past the shadow line and the
  // terminator warms (wrap lighting with a subsurface tint; matte fighter surfaces only).
  const bool skin = (switches & kSkin) != 0u && record != kNone && (Records[record].flags & 1u) != 0u && roughness > 0.5;
  for (uint i = first; i < first + count; ++i) {
    const Light l = Lights[i];
    if (l.type == 0u || !in_light_mask(channel, l)) continue;
    float3 to_light;
    float distance;
    float falloff = 1.0;
    if (l.type == 1u) {
      to_light = cone_direction(-l.direction, l.radius, rng);
      distance = 1.0e6;
    } else {
      // A point on the light's sphere, so its shadows have soft edges.
      float3 t, b;
      const float3 axis = normalize(l.position - p);
      basis(axis, t, b);
      const float u = rand01(rng) * 2.0 * kPi, rr = sqrt(rand01(rng)) * l.radius;
      const float3 target = l.position + (t * cos(u) + b * sin(u)) * rr;
      const float3 delta = target - p;
      distance = length(delta);
      to_light = delta / max(distance, 1e-4);
      const float dc = length(l.position - p);
      falloff = 1.0 / max(l.attenuation.x + l.attenuation.y * dc + l.attenuation.z * dc * dc, 1e-4);
      if (l.type == 3u && l.cos_cutoff > -1.0) {
        const float c = dot(-axis, l.direction);
        falloff *= saturate((c - l.cos_cutoff) / max(1.0 - l.cos_cutoff, 1e-3));
      }
    }
    // GX's diffuse function: none adds the light whatever the normal (no N.L).
    const float nl = ((channel >> 17) & 3u) == 0u && (channel & 0x10000u) != 0u ? 1.0 : dot(n_gx, to_light);
    const float nl_diffuse = skin ? saturate((nl + 0.4) / 1.4) : nl;
    if (nl_diffuse <= 0.0 || falloff <= 0.0) continue;
    // GX lights a surface with colour * N.L, a Lambert surface under an irradiance of pi * colour;
    // the highlight gets the same irradiance.
    const float3 scatter = skin ? lerp(float3(1.0, 0.78, 0.66), 1.0, saturate(nl * 2.0)) : 1.0;
    const float3 direct = l.radiance * falloff * nl_diffuse * scatter;
    irradiance_open += direct;
    gx_open += linear_to_srgb(l.radiance) * falloff * max(nl, 0.0);
    // Fighters take shadows from everything; lit stage parts from the fighters (their lights were
    // placed for a game without shadows). A low fill light (side or below) casts none.
    const bool fill = l.type == 1u && to_light.y < 0.2;
    const float lit = shadows && !fill && !visible(origin, to_light, distance, fighter ? kCasters : kFighterCasters) ? darken(kShadowFill) : 1.0;
    irradiance += direct * lit;
    if (lit < 1.0) { gx_sum += linear_to_srgb(l.radiance) * falloff * max(nl, 0.0) * lit; continue; }
    highlight += l.radiance * falloff * kPi * ggx_specular(n_gx, v, to_light, roughness, f0);
    gx_sum += linear_to_srgb(l.radiance) * falloff * max(nl, 0.0);
  }
  // GX clamps ambient plus lights at 1 per channel, so a surface facing the sun shows exactly its
  // material colour however far past 1 they add up; the same scale keeps the game's brightness.
  float3 total = ambient;
  for (uint k = first; k < first + count; ++k)
    if (Lights[k].type != 0u && in_light_mask(channel, Lights[k])) total += Lights[k].radiance;
  const float clamp_scale = 1.0 / max(1.0, max(total.r, max(total.g, total.b)));
  const float3 gx_lit = srgb_to_linear(GAlbedo[px].rgb * saturate(gx_open));
  const float3 pbr = gx_lit * min(irradiance / max(irradiance_open, 1e-4), 2.0) + highlight * clamp_scale;
  // Reflections: a glossy surface (roughness up to 0.5) sees the real scene in its highlight.
  float3 reflection = 0;
  if ((switches & kReflections) != 0u && roughness <= 0.5) {
    const float3 dir = glossy_direction(n, v, roughness, rng);
    if (dot(dir, n) > 0.0) {
      const Hit h = trace_surface(origin, dir, 1.0e5);
      reflection = reflection_strength * specular_albedo(f0, saturate(dot(n, v)), roughness) * hit_radiance(h, ambient_light, sun, sun_dir, sun_radius, rng);
    }
  }
  float3 faithful = srgb_to_linear(GAlbedo[px].rgb * saturate(gx_sum));
  faithful += 0.5 * albedo * bounce_extra;
  float3 shaded = lerp(faithful, pbr, look) + reflection;
  if (shadows && (switches & kFighterShadows) != 0u && !fighter) shaded *= fighter_shadow(origin, n, sun_dir, look, rng);
  if ((switches & kGlossyFloors) != 0u && (switches & kReflections) != 0u && !fighter && n.y > 0.85 && nd.w < 800.0) {
    const float3 fresnel = floor_fresnel(n, v);
    shaded = shaded * (1.0 - fresnel) + fresnel * floor_reflection(origin, n, v, shaded, ambient_light, sun, sun_dir, sun_radius, rng);
    write_guides(px, albedo, fresnel, nd.xyz, kFloorRoughness);
  }
  emit(px, shaded * light_scale, albedo);
}
