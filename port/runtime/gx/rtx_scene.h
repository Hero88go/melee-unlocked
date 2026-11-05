// GPU scene for the path tracer: every scene draw the raster pass executed this present, as
// world-space triangles in bottom-level acceleration structures (one per instance class) under one
// top-level structure, built on the GPU each presented frame from the raster's own uploads.
//
// The raster's vertex transform runs again in a compute pass (shaders/rtx_transform.hlsl) on the
// exact matrices, vertices and indices the raster used, so the ray-traced geometry matches the
// picture to the float, sub-frame poses included. Nothing is transformed or uploaded per vertex on
// the CPU: the old pass (gx_dxr_scene.cpp) did that and dropped matches to a few frames a second.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gx {

// Instance classes, one bottom-level structure each.
enum RtxClass : uint8_t {
  kRtxOpaque = 0,        // opaque stage surfaces
  kRtxAlphaTested = 1,   // cut-out surfaces (foliage, fences): any-hit alpha test
  kRtxTranslucent = 2,   // blended surfaces and particles: lit and emissive, never block shadow rays
  kRtxFighter = 3,       // opaque surfaces a player owns (fighters, their items): the moving shadow casters
  kRtxClassCount = 4,
};
// TraceRay instance inclusion masks. Shadow rays take opaque | alpha-tested | fighter; on a stage
// whose light is baked in, only fighter, so the stage's own baked shadows are not doubled.
constexpr uint8_t kRtxMaskOpaque = 0x01, kRtxMaskAlphaTested = 0x02, kRtxMaskTranslucent = 0x04, kRtxMaskFighter = 0x08;
// Ray types (TraceRay RayContributionToHitGroupIndex): 0 surface (closest hit), 1 visibility. The
// hit group of a hit is class * kRtxRayTypes + ray type (the instance contributes class * types).
constexpr uint32_t kRtxRayTypes = 2;
// Geometry record flags.
enum : uint32_t {
  kRtxGeometrySkinned = 1u << 0,    // a fighter's model
  kRtxGeometryFighter = 1u << 1,    // any draw a player owns
  kRtxGeometryParticle = 1u << 2,   // the particle system's
  kRtxGeometryAdditive = 1u << 3,   // blended additively: light, not surface
  kRtxGeometryNoNormals = 1u << 4,  // shade with the triangle's own normal
  kRtxGeometryTintedReflection = 1u << 5,   // its colour comes from the game's environment map: its
                                            // traced reflection takes that colour, as a metal's does
};
// RtxGeometryRecord::channel: the draw's GX colour channel 0 (XF 0x100E).
enum : uint32_t {
  kRtxChannelLit = 1u << 0,         // GX lighting on: the integrators relight the surface
  kRtxChannelAmbientVertex = 1u << 1,  // ambient from the vertex colour, not the register
  kRtxChannelMaterialVertex = 1u << 2, // material colour from the vertex colour
  // Bits 8-15: the channel's GX light mask (lights 0-7); bit 16 set when the mask is known.
  kRtxChannelMaskKnown = 1u << 16,
  kRtxChannelDiffuseShift = 17,     // bits 17-18: GX's diffuse function (0 none: no N.L, 1 sign, 2 clamp)
};

// One raster draw handed over by execute_draw.
struct RtxDrawInput {
  D3D12_GPU_VIRTUAL_ADDRESS vertices = 0;      // the draw's raw gx::Vertex array
  D3D12_GPU_VIRTUAL_ADDRESS indices = 0;       // triangle-list indices relative to `vertices`
  D3D12_GPU_VIRTUAL_ADDRESS vs_constants = 0;  // the draw's VSConstants upload (256-byte aligned)
  uint32_t index_count = 0;
  uint32_t components = 0;
  uint32_t num_texgens = 0, dual_tex = 0, chan_color0 = 0, chan_alpha0 = 0;
  uint32_t texgen[4]{}, post[4]{};
  uint32_t draw_index = 0;         // into Frame::draws
  int32_t material = -1;           // into Frame::rtx.materials
  uint32_t texture_set = 0;        // first of the draw's 8 texture SRVs in the renderer's shader-visible heap
  uint32_t texture_heap = 0;       // which heap that index is in (the renderer replaces a full heap)
  uint32_t flags = 0;              // kRtxGeometry*
  uint32_t specular = 0x0A0A0AFFu; // rtx_material_params: specular colour RGBA8
  float roughness = 0.85f;
  uint32_t ambient = 0;            // the GX ambient register of channel 0, RGBA8 (r in the low byte)
  uint32_t channel = 0;            // kRtxChannel*
  uint32_t light_first = 0, light_count = 0;   // the draw's own light set in the frame's light table
  uint8_t cls = kRtxOpaque;
};
// RtxGeometryRecord::texture_set when the draw's textures cannot be read (its heap was replaced).
constexpr uint32_t kRtxNoTextures = 0xFFFFFFFFu;

// What the hit shaders read per geometry: StructuredBuffer index InstanceID() + GeometryIndex().
struct RtxGeometryRecord {
  uint32_t first_vertex;     // into the world-space vertex soup (three per triangle)
  uint32_t triangle_count;
  uint32_t draw_index;
  int32_t material;
  uint32_t texture_set;
  uint32_t flags;
  uint32_t specular;         // RGBA8 specular colour
  float roughness;           // perceptual
  // GX's own lighting of the draw: the ambient its channel adds (HSD's material ambient times the
  // scene's ambient light, RGBA8, r in the low byte) and kRtxChannel*. With GX's clamp they give the
  // brightness the integrators keep.
  uint32_t ambient;
  uint32_t channel;
  // The lights GX lit this draw with (its HSD light set), a run of the frame's light table: fighters
  // keep their own lights, the stage its own.
  uint32_t light_first;
  uint32_t light_count;
};
static_assert(sizeof(RtxGeometryRecord) == 48, "hit shader record layout");

// The compute pass writes this per vertex (shaders/rtx_transform.hlsl RtVertex).
constexpr uint32_t kRtxVertexStride = 64;

class RtxScene {
 public:
  static constexpr unsigned kFrameSlots = 3;

  // Loads rtx_transform.dxil from beside the executable and creates its pipeline.
  bool initialize(ID3D12Device5* device, const std::wstring& exe_dir, std::string* error);
  bool ready() const { return transform_pso_ != nullptr; }

  void begin_frame() { draws_.clear(); }
  void add(const RtxDrawInput& draw) { draws_.push_back(draw); }
  size_t pending_draws() const { return draws_.size(); }

  // Records the transform dispatches and the acceleration-structure builds for this present into
  // `list` (compute state; the caller restores its own pipeline and descriptor heaps afterwards).
  // `view_to_world` is the inverse of the main camera's view matrix, row major 3x4; draws whose
  // texture_heap is not `heap_generation` lose their textures. Returns false when there was
  // nothing to build or a resource could not be made; `error` says which.
  // `frame_draws` sizes the draw table (frame draw index -> record), read through the G-buffer's ids.
  bool build(ID3D12GraphicsCommandList4* list, unsigned slot, const float view_to_world[12],
             uint32_t heap_generation, uint32_t frame_draws, std::string* error);

  D3D12_GPU_VIRTUAL_ADDRESS tlas(unsigned slot) const;
  ID3D12Resource* vertices(unsigned slot) const;
  ID3D12Resource* geometry_records(unsigned slot) const;
  uint32_t vertex_count(unsigned slot) const;
  uint32_t geometry_count(unsigned slot) const;
  ID3D12Resource* draw_table(unsigned slot) const;   // uint per frame draw: record index, ~0u none
  uint32_t draw_table_size(unsigned slot) const;

  struct Stats { uint64_t builds = 0, draws = 0, triangles = 0; double record_ms = 0; };
  const Stats& stats() const { return stats_; }

 private:
  struct Slot {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertices;           // RtVertex soup, UAV then AS input
    D3D12_RESOURCE_STATES vertices_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    Microsoft::WRL::ComPtr<ID3D12Resource> records;            // RtxGeometryRecord, upload heap
    Microsoft::WRL::ComPtr<ID3D12Resource> frame_constants;    // view_to_world, upload heap
    Microsoft::WRL::ComPtr<ID3D12Resource> instances;          // D3D12_RAYTRACING_INSTANCE_DESC, upload heap
    Microsoft::WRL::ComPtr<ID3D12Resource> blas[kRtxClassCount], blas_scratch[kRtxClassCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> tlas, tlas_scratch;
    Microsoft::WRL::ComPtr<ID3D12Resource> draw_table;         // upload heap
    uint8_t* draw_table_cpu = nullptr;
    uint32_t draw_table_size = 0;
    uint8_t* records_cpu = nullptr;
    uint8_t* constants_cpu = nullptr;
    uint8_t* instances_cpu = nullptr;
    uint32_t vertex_count = 0, geometry_count = 0;
    bool built = false;
  };

  bool ensure_buffer(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, uint64_t bytes, D3D12_HEAP_TYPE heap,
                     D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags, uint8_t** mapped,
                     std::string* error);

  Microsoft::WRL::ComPtr<ID3D12Device5> device_;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> transform_root_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> transform_pso_;
  std::vector<RtxDrawInput> draws_;
  std::vector<uint32_t> order_;                                   // draws_ sorted by class
  std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometry_descs_;
  Slot slots_[kFrameSlots];
  Stats stats_;
};

// Inverse of a row-major 3x4 affine matrix (a camera's world -> view). False when singular.
bool rtx_invert_affine(const float m[12], float out[12]);

}  // namespace gx
