// Hybrid one-bounce GX path-tracing pass. The raster image remains the material-color source so
// Melee's TEV textures and authored lighting survive; DXR traces diffuse secondary transport.
// SPDX-License-Identifier: GPL-2.0-or-later
struct DxrSceneVertex {
  float3 position;
  float3 normal;
  float4 color;
  float2 uv;
};

struct RayPayload {
  float3 normal;
  float3 position;
  float t;
  float3 vertexColor;
  uint hit;
};

struct HitAttributes { float2 barycentrics; };

RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<DxrSceneVertex> Vertices : register(t1);
StructuredBuffer<uint> Indices : register(t2);
Texture2D<float4> RasterColor : register(t3);
RWTexture2D<float4> PathColor : register(u0);
RWTexture2D<float4> DiffuseGuide : register(u1);
RWTexture2D<float4> NormalGuide : register(u2);
RWTexture2D<float4> SpecularGuide : register(u3);

cbuffer DispatchInfo : register(b0) {
  uint2 outputExtent;
  uint frameIndex;
  uint samplesPerPixel;
  float frustumLeft;
  float frustumRight;
  float frustumTop;
  float frustumBottom;
  float nearPlane;
  float farPlane;
  float bounceIntensity;
  uint maxBounces;
};

uint hash32(uint x) {
  x ^= x >> 16;
  x *= 0x7feb352d;
  x ^= x >> 15;
  x *= 0x846ca68b;
  x ^= x >> 16;
  return x;
}

float random01(inout uint state) {
  state = hash32(state + 0x9e3779b9);
  return (state & 0x00ffffff) * (1.0 / 16777216.0);
}

float3 cosine_direction(float3 normal, inout uint rng) {
  float u = random01(rng);
  float v = random01(rng);
  float phi = 6.28318530718 * u;
  float r = sqrt(v);
  float z = sqrt(max(0.0, 1.0 - v));
  float3 helper = abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
  float3 tangent = normalize(cross(helper, normal));
  float3 bitangent = cross(normal, tangent);
  return normalize(tangent * (cos(phi) * r) + bitangent * (sin(phi) * r) + normal * z);
}

float3 linearize_srgb(float3 c) {
  c = saturate(c);
  return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
}

bool project_view_position(float3 p, out uint2 pixel) {
  pixel = 0;
  if (p.z >= -nearPlane * 0.5) return false;
  float inv_z = nearPlane / -p.z;
  float u = (p.x * inv_z - frustumLeft) / (frustumRight - frustumLeft);
  float v = (frustumTop - p.y * inv_z) / (frustumTop - frustumBottom);
  if (u < 0 || u >= 1 || v < 0 || v >= 1) return false;
  pixel = min(uint2(u * outputExtent.x, v * outputExtent.y), outputExtent - 1);
  return true;
}

float3 sampled_material_color(float3 view_position, float3 fallback, bool hit) {
  uint2 pixel;
  if (hit && project_view_position(view_position, pixel))
    return linearize_srgb(RasterColor.Load(int3(pixel, 0)).rgb);
  return linearize_srgb(fallback);
}

[shader("miss")]
void Miss(inout RayPayload payload) {
  payload.hit = 0;
  payload.t = 0;
  payload.normal = float3(0, 0, 0);
  payload.position = float3(0, 0, 0);
  payload.vertexColor = float3(0.18, 0.24, 0.34);
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, HitAttributes attributes) {
  uint index = PrimitiveIndex() * 3;
  uint i0 = Indices[index + 0];
  uint i1 = Indices[index + 1];
  uint i2 = Indices[index + 2];
  float w0 = 1.0 - attributes.barycentrics.x - attributes.barycentrics.y;
  float w1 = attributes.barycentrics.x;
  float w2 = attributes.barycentrics.y;
  float3 n = normalize(Vertices[i0].normal * w0 + Vertices[i1].normal * w1 + Vertices[i2].normal * w2);
  if (dot(n, -WorldRayDirection()) < 0.0) n = -n;
  payload.normal = n;
  payload.t = RayTCurrent();
  payload.position = WorldRayOrigin() + WorldRayDirection() * payload.t;
  payload.vertexColor = Vertices[i0].color.rgb * w0 + Vertices[i1].color.rgb * w1 + Vertices[i2].color.rgb * w2;
  payload.hit = 1;
}

[shader("raygeneration")]
void RayGen() {
  uint2 pixel = DispatchRaysIndex().xy;
  if (pixel.x >= outputExtent.x || pixel.y >= outputExtent.y) return;
  float4 raster = RasterColor.Load(int3(pixel, 0));
  float3 base = linearize_srgb(raster.rgb);

  float2 uv = (float2(pixel) + 0.5) / float2(outputExtent);
  float x = lerp(frustumLeft, frustumRight, uv.x);
  float y = lerp(frustumTop, frustumBottom, uv.y);
  RayDesc primary;
  primary.Origin = float3(0, 0, 0);
  primary.Direction = normalize(float3(x, y, -nearPlane));
  primary.TMin = max(nearPlane * 0.001, 0.001);
  primary.TMax = max(farPlane, nearPlane + 1.0);
  RayPayload first;
  first.hit = 0;
  TraceRay(Scene, RAY_FLAG_NONE, 0xff, 0, 1, 0, primary, first);

  if (first.hit == 0) {
    PathColor[pixel] = float4(base, 1.0);
    DiffuseGuide[pixel] = float4(base, 1.0);
    NormalGuide[pixel] = float4(0, 0, 0, 0.72);
    SpecularGuide[pixel] = float4(0, 0, 0, 1);
    return;
  }

  float3 albedo = base;
  float3 indirect = 0;
  uint seed = hash32(pixel.x + pixel.y * outputExtent.x + frameIndex * 0x9e3779b9);
  uint spp = clamp(samplesPerPixel, 1, 4);
  uint bounceCount = clamp(maxBounces, 1, 4);
  [loop] for (uint sample = 0; sample < spp; ++sample) {
    RayPayload current = first;
    float3 throughput = albedo;
    [loop] for (uint bounceIndex = 0; bounceIndex < bounceCount; ++bounceIndex) {
      float3 direction = cosine_direction(current.normal, seed);
      RayDesc bounce;
      bounce.Origin = current.position + current.normal * max(nearPlane * 0.0005, 0.002);
      bounce.Direction = direction;
      bounce.TMin = max(nearPlane * 0.0005, 0.002);
      bounce.TMax = max(farPlane, nearPlane + 1.0);
      RayPayload next;
      next.hit = 0;
      TraceRay(Scene, RAY_FLAG_NONE, 0xff, 0, 1, 0, bounce, next);
      float3 incoming = sampled_material_color(next.position, next.vertexColor, next.hit != 0);
      indirect += throughput * incoming;
      // Use bounded, energy-conserving continuation; a miss contributes environment once.
      if (next.hit == 0) break;
      throughput *= saturate(incoming) * 0.5;
      current = next;
    }
  }
  indirect /= spp;
  // The captured GX color already contains the game's textured direct-lighting result. Add a
  // restrained Monte Carlo diffuse bounce so the pass improves inter-reflection without replacing
  // Melee's material response or changing pixels that DXR cannot match to the raster camera.
  float3 traced = base + indirect * max(bounceIntensity, 0.0);
  PathColor[pixel] = float4(traced, 1.0);
  DiffuseGuide[pixel] = float4(albedo, 1.0);
  NormalGuide[pixel] = float4(first.normal, 0.72);
  SpecularGuide[pixel] = float4(0, 0, 0, 1);
}
