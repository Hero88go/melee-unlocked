// Ray-tracing geometry for the path tracer: the raster vertex shader's own transform (gx_shader.cpp
// gen_vertex_shader), run once per presented frame over each scene draw the raster pass executed.
// One thread per index of the draw's triangle list, so the output is a de-indexed triangle soup in
// world space: three vertices per triangle, in the draw's order, which the acceleration structures
// and the hit shaders read directly (PrimitiveIndex() * 3).
//
// Inputs are the raster's own uploads: the draw's VSConstants (same matrices the raster used this
// present, sub-frame poses included), its raw GX vertices and its triangle-list indices.
// SPDX-License-Identifier: GPL-2.0-or-later

cbuffer VSBlock : register(b0) {   // gx_shader.h VSConstants, as the raster uploaded it
  float4 projection[4];
  float4 depthparams;
  float4 viewparams;
  float4 materials[4];             // ambient 0, ambient 1, material 0, material 1 (0..255)
  float4 lights[40];
  float4 texmatrices[24];
  float4 transformmatrices[64];
  float4 normalmatrices[32];
  float4 posttransformmatrices[64];
};

cbuffer DrawBlock : register(b1) {   // root constants, one set per draw (RtxTransformConstants)
  uint index_count;
  uint output_base;                  // first output vertex of this draw
  uint components;                   // VB_HAS_*
  uint num_texgens;                  // XF 0x103F
  uint dual_tex;                     // XF 0x1012 bit 0
  uint chan_color0;                  // XF 0x100E
  uint chan_alpha0;                  // XF 0x1010
  uint draw_flags;                   // reserved
  uint4 texgen;                      // XF 0x1040..0x1043 (vectors: a cbuffer array pads each element)
  uint4 post;                        // XF 0x1050..0x1053
};

cbuffer FrameBlock : register(b2) {
  float4 view_to_world[3];           // rows of the inverse of the main camera's view matrix
};

ByteAddressBuffer Vertices : register(t0);   // gx::Vertex, 108 bytes each (gx_core.h)
ByteAddressBuffer Indices : register(t1);    // uint32 triangle-list indices, relative to Vertices

struct RtVertex {
  float3 position;   // world space
  float3 normal;     // world space, 0 when the draw has none
  uint color;        // unlit channel-0 colour (the material source GX lighting would multiply), RGBA8
  uint flags;        // bit 0: normal present
  float2 uv[4];      // texture generators 0..3, after the projective divide
};
RWStructuredBuffer<RtVertex> Output : register(u0);

static const uint kVertexStride = 108;
static const uint VB_HAS_TEXMTXIDX0 = 1u << 1;
static const uint VB_HAS_NRM0 = 1u << 10;
static const uint VB_HAS_COL0 = 1u << 13;
static const uint VB_HAS_UV0 = 1u << 15;

uint field(uint v, uint lo, uint count) { return (v >> lo) & ((1u << count) - 1u); }

uint load_byte(uint address) {
  const uint word = Vertices.Load(address & ~3u);
  return (word >> ((address & 3u) * 8u)) & 0xFFu;
}

float4 unpack_rgba8(uint word) {   // gx::Vertex colour bytes are r, g, b, a in memory order
  return float4(word & 0xFF, (word >> 8) & 0xFF, (word >> 16) & 0xFF, word >> 24) / 255.0;
}

uint pack_rgba8(float4 c) {
  const uint4 b = uint4(round(saturate(c) * 255.0));
  return b.x | (b.y << 8) | (b.z << 16) | (b.w << 24);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint i = id.x;
  if (i >= index_count) return;
  const uint base = Indices.Load(i * 4) * kVertexStride;

  const float3 rawpos = asfloat(Vertices.Load3(base + 0));
  const float3 rawnorm = asfloat(Vertices.Load3(base + 12));
  const uint col0 = Vertices.Load(base + 24);
  const int posmtx = (int)load_byte(base + 96);

  const float4 rawpos4 = float4(rawpos, 1.0);
  const float3 view_pos = float3(dot(transformmatrices[posmtx], rawpos4), dot(transformmatrices[posmtx + 1], rawpos4),
                                 dot(transformmatrices[posmtx + 2], rawpos4));
  float3 view_normal = float3(0, 0, 0);
  const bool has_normal = (components & VB_HAS_NRM0) != 0;
  if (has_normal) {
    const int normidx = posmtx >= 32 ? posmtx - 32 : posmtx;
    const float3 N0 = normalmatrices[normidx].xyz, N1 = normalmatrices[normidx + 1].xyz, N2 = normalmatrices[normidx + 2].xyz;
    const float3 n = float3(dot(N0, rawnorm), dot(N1, rawnorm), dot(N2, rawnorm));
    const float len2 = dot(n, n);
    view_normal = len2 > 1e-20 ? n * rsqrt(len2) : float3(0, 0, 0);
  }

  // The colour GX lighting multiplies (its material source), not the lit result: the path tracer
  // does its own lighting. Material source 1 is the vertex colour, 0 the channel's register.
  const float4 vertex_colour = (components & VB_HAS_COL0) ? unpack_rgba8(col0) : float4(1, 1, 1, 1);
  const float4 register_colour = materials[2] / 255.0;
  float4 colour;
  colour.rgb = field(chan_color0, 0, 1) ? vertex_colour.rgb : register_colour.rgb;
  colour.a = field(chan_alpha0, 0, 1) ? vertex_colour.a : register_colour.a;

  RtVertex o;
  o.position = float3(dot(view_to_world[0], float4(view_pos, 1)), dot(view_to_world[1], float4(view_pos, 1)),
                      dot(view_to_world[2], float4(view_pos, 1)));
  const float3 world_normal = float3(dot(view_to_world[0].xyz, view_normal), dot(view_to_world[1].xyz, view_normal),
                                     dot(view_to_world[2].xyz, view_normal));
  const float wn2 = dot(world_normal, world_normal);
  o.normal = wn2 > 1e-20 ? world_normal * rsqrt(wn2) : float3(0, 0, 0);
  o.color = pack_rgba8(colour);
  o.flags = has_normal ? 1u : 0u;

  // Texture generators 0..3 as the raster's vertex shader computes them (regular type only; the
  // colour-sourced and emboss generators keep their raw coordinate).
  [unroll] for (uint k = 0; k < 4; ++k) {
    float2 uv = float2(0, 0);
    if (k < num_texgens) {
      const uint info = texgen[k];
      const uint projection_q = field(info, 1, 1), inputform = field(info, 2, 1);
      const uint texgentype = field(info, 4, 3), sourcerow = field(info, 7, 5);
      float4 coord = float4(0, 0, 1, 1);
      if (sourcerow == 0) coord.xyz = rawpos;
      else if (sourcerow == 1) { if (has_normal) coord.xyz = rawnorm; }
      else if (sourcerow >= 5 && sourcerow <= 12 && (components & (VB_HAS_UV0 << (sourcerow - 5))))
        coord.xy = asfloat(Vertices.Load2(base + 32 + (sourcerow - 5) * 8));
      if (inputform == 0) coord.z = 1.0;
      float3 t;
      if (texgentype == 0) {
        if (components & (VB_HAS_TEXMTXIDX0 << k)) {
          const int tmp = (int)load_byte(base + 97 + k);
          t = float3(dot(coord, transformmatrices[tmp]), dot(coord, transformmatrices[tmp + 1]),
                     projection_q ? dot(coord, transformmatrices[tmp + 2]) : 1.0);
        } else {
          t = float3(dot(coord, texmatrices[3 * k]), dot(coord, texmatrices[3 * k + 1]),
                     projection_q ? dot(coord, texmatrices[3 * k + 2]) : 1.0);
        }
        if (projection_q && t.z == 0.0) t.xy = clamp(t.xy, -2.0, 2.0);
        if (dual_tex) {
          const uint postinfo = post[k];
          const int postidx = (int)(postinfo & 0x3F);
          if (field(postinfo, 8, 1)) t = normalize(t);
          const float4 P0 = posttransformmatrices[postidx & 63], P1 = posttransformmatrices[(postidx + 1) & 63],
                       P2 = posttransformmatrices[(postidx + 2) & 63];
          t = float3(dot(P0.xyz, t) + P0.w, dot(P1.xyz, t) + P1.w, dot(P2.xyz, t) + P2.w);
        }
      } else {
        t = float3(coord.xy, 1.0);
      }
      uv = t.xy / ((t.z == 0.0) ? 2.0 : t.z);
    }
    o.uv[k] = uv;
  }
  Output[output_base + i] = o;
}
