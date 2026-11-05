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
};

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

// True when nothing opaque lies between `origin` and `origin + dir * length`.
bool visible(float3 origin, float3 dir, float length) {
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = dir;
  ray.TMin = 0.0;
  ray.TMax = length;
  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> q;
  q.TraceRayInline(Scene, RAY_FLAG_NONE, 0x03, ray);   // opaque and cut-out surfaces cast shadows
  q.Proceed();
  return q.CommittedStatus() == COMMITTED_NOTHING;
}

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

// Out to the presenter, which takes linear light (it encodes for the display).
void emit(uint2 px, float3 lit) {
  // What the raster drew over the surface goes back on top, composed as GX blended it (encoded).
  const float3 shown = linear_to_srgb(lit) * Trans[px].rgb + Overlay[px].rgb;
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
  const float3 n = normalize(to_world_direction(nd.xyz));
  const float3 v = -normalize(to_world_direction(view_dir));
  const float3 albedo = srgb_to_linear(GAlbedo[px].rgb);

  // The scene's ambient light and sun, for surfaces with no record of their own.
  float3 ambient_light = 0, sun = 0;
  for (uint j = 0; j < light_count; ++j) {
    if (Lights[j].type == 0u) ambient_light += Lights[j].radiance;
    if (Lights[j].type == 1u) sun += Lights[j].radiance;
  }

  float roughness = 0.85;
  float3 f0 = 0.04;
  float3 ambient = ambient_light;
  bool lit_by_gx = true;
  const uint record = draw - 1u < draw_count ? DrawRecords[draw - 1u] : kNone;
  if (record != kNone) {
    const GeometryRecord r = Records[record];
    roughness = r.roughness;
    f0 = unpack_rgb8(r.specular);
    if ((r.channel & 2u) == 0u) ambient = srgb_to_linear(unpack_rgb8(r.ambient));   // the draw's own GX ambient
    lit_by_gx = (r.channel & 1u) != 0u;
  }
  if (!lit_by_gx) {
    // GX does not light this draw (its light is baked in): it stays as drawn.
    emit(px, albedo);
    return;
  }

  uint rng = hash32(px.x + px.y * 4099u + frame_index * 0x9e3779b9u);
  const float3 origin = p + n * (0.02 + nd.w * 0.0004);   // off the surface, more so far away
  // Ambient: the light from everywhere, shadowed by what is close around the surface.
  float3 lit = albedo * ambient * (visible(origin, cosine_direction(n, rng), ao_radius) ? 1.0 : 0.0);
  for (uint i = 0; i < light_count; ++i) {
    const Light l = Lights[i];
    if (l.type == 0u) continue;
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
    const float nl = dot(n, to_light);
    if (nl <= 0.0 || falloff <= 0.0) continue;
    if (!visible(origin, to_light, distance)) continue;
    // GX lights a surface with colour * N.L, a Lambert surface under an irradiance of pi * colour;
    // the highlight gets the same irradiance.
    lit += l.radiance * falloff * (albedo * nl + kPi * ggx_specular(n, v, to_light, roughness, f0));
  }
  // GX clamps ambient plus lights at 1 per channel, so a surface facing the sun shows exactly its
  // material colour however far past 1 they add up. Scaling by that sum keeps the game's brightness
  // there; shade and shadow come out darker than GX's flat ambient, as light really falls off.
  const float3 total = ambient + sun;
  lit *= light_scale / max(1.0, max(total.r, max(total.g, total.b)));
  emit(px, lit);
}
