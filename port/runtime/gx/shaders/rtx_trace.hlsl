// Ray-traced passes of the path tracer (rtx_tracer.cpp). This first pass is the scene debug view of
// milestone F2: primary rays through the raster's own projection and viewport, coloured by draw,
// by surface normal or by textured vertex colour, to prove the ray-traced scene (rtx_scene.cpp)
// lines up with the picture. Selected with MELEE_RTX_DEBUG=1|2|3.
// SPDX-License-Identifier: GPL-2.0-or-later

struct RtVertex {          // shaders/rtx_transform.hlsl
  float3 position;
  float3 normal;
  uint color;
  uint flags;
  float2 uv[4];
};

struct GeometryRecord {    // rtx_scene.h RtxGeometryRecord
  uint first_vertex;
  uint triangle_count;
  uint draw_index;
  int material;
  uint texture_set;
  uint flags;
  uint specular;   // RGBA8
  float roughness;
  uint ambient;    // GX ambient register, RGBA8
  uint channel;    // kRtxChannel*
  uint light_first;
  uint light_count;
};

RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<RtVertex> Vertices : register(t1);
StructuredBuffer<GeometryRecord> Records : register(t2);
Texture2D<float4> Textures[] : register(t0, space1);   // the renderer's whole shader-visible heap
SamplerState LinearWrap : register(s0);
RWTexture2D<float4> Output : register(u0);
Texture2D<float4> GAlbedo : register(t3);   // the raster pass's G-buffer (gx_shader.cpp, G-buffer variant)
Texture2D<float4> GNormal : register(t4);   // view normal, linear depth
Texture2D<uint> GId : register(t5);         // draw index + 1

cbuffer FrameConstants : register(b0) {
  float4 view_to_world[3];   // rows
  float4 clip_to_view[4];    // inverse of the raster's projection, rows
  float4 viewport;           // x, y, width, height of the scene in output pixels
  uint2 output_size;
  uint mode;                 // 1 draw, 2 normal, 3 textured vertex colour; 4 G-buffer albedo, 5 normal, 6 draw
  uint orthographic;
};

static const uint kNoTextures = 0xFFFFFFFFu;
static const uint kGeometryNoNormals = 1u << 4;

struct Payload {
  float t;
  uint record;
  uint primitive;
  float2 barycentrics;
};

struct Attributes {
  float2 barycentrics;
};

float3 hash_colour(uint x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return float3(x & 255u, (x >> 8) & 255u, (x >> 16) & 255u) / 255.0;
}

float4 unpack_rgba8(uint word) {
  return float4(word & 0xFFu, (word >> 8) & 0xFFu, (word >> 16) & 0xFFu, word >> 24) / 255.0;
}

float3 to_world_point(float3 p) {
  return float3(dot(view_to_world[0], float4(p, 1)), dot(view_to_world[1], float4(p, 1)), dot(view_to_world[2], float4(p, 1)));
}

float3 to_world_direction(float3 d) {
  return float3(dot(view_to_world[0].xyz, d), dot(view_to_world[1].xyz, d), dot(view_to_world[2].xyz, d));
}

float3 unproject(float2 ndc, float z) {
  const float4 c = float4(ndc, z, 1);
  const float4 v = float4(dot(clip_to_view[0], c), dot(clip_to_view[1], c), dot(clip_to_view[2], c), dot(clip_to_view[3], c));
  return v.xyz / v.w;
}

[shader("miss")]
void Miss(inout Payload p) {
  p.t = -1.0;
}

[shader("closesthit")]
void ClosestHit(inout Payload p, Attributes a) {
  p.t = RayTCurrent();
  p.record = InstanceID() + GeometryIndex();
  p.primitive = PrimitiveIndex();
  p.barycentrics = a.barycentrics;
}

[shader("raygeneration")]
void RayGen() {
  const uint2 px = DispatchRaysIndex().xy;
  if (px.x >= output_size.x || px.y >= output_size.y) return;
  if (mode >= 4) {
    float3 g = float3(0, 0, 0);
    if (mode == 4) g = GAlbedo[px].rgb;
    else if (mode == 5) { const float4 n = GNormal[px]; if (n.w > 0.0) g = n.xyz * 0.5 + 0.5; }
    else if (mode == 7) { const uint id = GId[px]; g = float3((id & 0xFFu) / 255.0, ((id >> 8) & 0xFFu) / 255.0, 0.0); }   // raw draw id
    else { const uint id = GId[px]; if (id != 0u) g = hash_colour((id - 1u) * 2654435761u + 1u); }
    Output[px] = float4(g, 1);
    return;
  }
  const float2 ndc = float2(((px.x + 0.5) - viewport.x) / viewport.z * 2.0 - 1.0,
                            1.0 - ((px.y + 0.5) - viewport.y) / viewport.w * 2.0);
  const float3 a = unproject(ndc, 0.25), b = unproject(ndc, 0.75);
  RayDesc ray;
  if (orthographic != 0) {
    const float3 origin_view = a.z > b.z ? a : b;   // the nearer sample: the camera looks down -z
    ray.Origin = to_world_point(origin_view);
    ray.Direction = normalize(to_world_direction(float3(0, 0, -1)));
  } else {
    // Any point unprojected from this pixel lies on its ray through the eye, but depending on the
    // projection's depth range it can be behind the camera: the view looks down -z, so face that way.
    float3 d = normalize(a);
    if (d.z > 0.0) d = -d;
    ray.Origin = to_world_point(float3(0, 0, 0));
    ray.Direction = normalize(to_world_direction(d));
  }
  ray.TMin = 0.0;
  ray.TMax = 1.0e30;
  Payload p;
  p.t = -1.0;
  p.record = 0;
  p.primitive = 0;
  p.barycentrics = float2(0, 0);
  // Opaque, cut-out and fighter surfaces; ray type 0; the geometry index does not pick the hit group.
  TraceRay(Scene, RAY_FLAG_NONE, 0x0B, 0, 0, 0, ray, p);
  if (p.t < 0.0) {
    Output[px] = float4(0.04, 0.04, 0.07, 1);
    return;
  }
  const GeometryRecord r = Records[p.record];
  const uint first = r.first_vertex + p.primitive * 3;
  const RtVertex v0 = Vertices[first], v1 = Vertices[first + 1], v2 = Vertices[first + 2];
  const float3 w = float3(1.0 - p.barycentrics.x - p.barycentrics.y, p.barycentrics.x, p.barycentrics.y);
  float3 colour;
  if (mode == 1) {
    colour = hash_colour(r.draw_index * 2654435761u + 1u);
  } else if (mode == 2) {
    float3 n = v0.normal * w.x + v1.normal * w.y + v2.normal * w.z;
    if ((r.flags & kGeometryNoNormals) != 0 || dot(n, n) < 1e-12)
      n = cross(v1.position - v0.position, v2.position - v0.position);
    n = normalize(n);
    if (dot(n, ray.Direction) > 0) n = -n;
    colour = n * 0.5 + 0.5;
  } else {
    const float4 c = unpack_rgba8(v0.color) * w.x + unpack_rgba8(v1.color) * w.y + unpack_rgba8(v2.color) * w.z;
    const float2 uv = v0.uv[0] * w.x + v1.uv[0] * w.y + v2.uv[0] * w.z;
    float4 t = float4(1, 1, 1, 1);
    if (r.texture_set != kNoTextures) t = Textures[NonUniformResourceIndex(r.texture_set)].SampleLevel(LinearWrap, uv, 0);
    colour = c.rgb * t.rgb;
  }
  Output[px] = float4(colour, 1);
}
