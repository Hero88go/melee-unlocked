// The path tracer's lighting: one interface, two implementations, switchable on the same frame.
//
//   * the Melee integrator: ReSTIR direct light, ReSTIR GI and a world-space radiance cache, written
//     for this project from the published papers (GPL, in this repository);
//   * a local-only module built from NVIDIA's RTXDI and RTXGI (NRC, SHaRC) SDKs. Their licence does
//     not allow them under an open-source licence, so that module lives outside this repository,
//     is compiled in only with MELEE_RTX_LOCAL_NVIDIA (local builds), and never ships.
//
// The renderer (rtx_scene.h, the G-buffer, the light and material tables) prepares one
// RtxFrameInputs per presented frame; the integrator returns the HDR radiance and the guide buffers
// DLSS Ray Reconstruction takes.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <d3d12.h>

#include <cstdint>
#include <memory>
#include <string>

namespace gx {

struct RtxLightRecord;   // rtx_lights.h

struct RtxFrameInputs {
  ID3D12Device5* device = nullptr;
  ID3D12GraphicsCommandList4* list = nullptr;
  unsigned slot = 0;                              // frame slot: per-slot resources are free to reuse
  uint64_t frame_index = 0;                       // presented frames since start (random sequences)
  bool reset = false;                             // a camera cut or rollback: drop temporal history

  // The scene (rtx_scene.h): world-space triangles and the per-geometry records.
  D3D12_GPU_VIRTUAL_ADDRESS tlas = 0;
  ID3D12Resource* vertices = nullptr;             // RtVertex, NON_PIXEL_SHADER_RESOURCE
  ID3D12Resource* records = nullptr;              // RtxGeometryRecord, upload heap
  uint32_t record_count = 0;

  // The renderer's shader-visible heap: texture SRVs at RtxGeometryRecord::texture_set, and a run of
  // free slots this present for the integrator's own views.
  ID3D12DescriptorHeap* heap = nullptr;
  UINT descriptor_size = 0;
  uint32_t free_slot = 0, free_slot_count = 0;

  // The raster pass's targets at the EFB size, NON_PIXEL_SHADER_RESOURCE while rendering.
  ID3D12Resource* raster_color = nullptr;         // the picture as GX drew it (RGBA8)
  ID3D12Resource* albedo = nullptr;               // de-lit TEV colour (RGBA8)
  ID3D12Resource* normal_depth = nullptr;         // view normal xyz, linear view depth w (RGBA16F)
  ID3D12Resource* draw_id = nullptr;              // draw index + 1, 0 none (R32_UINT)
  ID3D12Resource* motion = nullptr;               // pixel motion, previous minus current (RG16F)
  ID3D12Resource* prev_albedo = nullptr;          // the previous present's G-buffer (valid unless reset)
  ID3D12Resource* prev_normal_depth = nullptr;
  ID3D12Resource* prev_draw_id = nullptr;
  // What the raster drew over the G-buffer's surface (RGBA16F): the picture is lit * transmittance +
  // overlay wherever there is a surface.
  ID3D12Resource* overlay = nullptr;
  ID3D12Resource* transmittance = nullptr;
  uint32_t width = 0, height = 0;

  // The camera, as the raster drew this present.
  float view_to_world[12]{};
  float clip_to_view[16]{};
  float viewport[4]{};                            // the scene's x, y, w, h in EFB pixels
  bool orthographic = false;
  float prev_view_to_world[12]{};                 // last present's, for reprojection

  // Lights and materials (the renderer's tables, upload heap). light_records is the same light table
  // on the CPU, for integrators that reorder or split it.
  ID3D12Resource* lights = nullptr;
  const RtxLightRecord* light_records = nullptr;
  uint32_t light_count = 0;
  float light_scale = 1.0f;                       // overall light multiplier (exposure), 1 = the game's own
  ID3D12Resource* draws = nullptr;                // per draw index: material record for the G-buffer
  uint32_t draw_count = 0;
};

struct RtxFrameOutputs {
  ID3D12Resource* color = nullptr;                // HDR radiance, RGBA16F, PIXEL|NON_PIXEL_SHADER_RESOURCE
  ID3D12Resource* diffuse_albedo = nullptr;       // Ray Reconstruction guides, same state and size
  ID3D12Resource* specular_albedo = nullptr;
  ID3D12Resource* normal_roughness = nullptr;     // world normal xyz, roughness w
  ID3D12Resource* specular_hit_distance = nullptr;
};

class RtxIntegrator {
 public:
  virtual ~RtxIntegrator() = default;
  virtual const char* name() const = 0;
  virtual bool initialize(ID3D12Device5* device, const std::wstring& exe_dir, std::string* error) = 0;
  // Records this present's lighting into in.list. False leaves the outputs untouched.
  virtual bool render(const RtxFrameInputs& in, RtxFrameOutputs* out, std::string* error) = 0;
};

std::unique_ptr<RtxIntegrator> make_melee_integrator();
#ifdef MELEE_RTX_LOCAL_NVIDIA
std::unique_ptr<RtxIntegrator> make_nvidia_integrator();   // private module, local builds only
#endif

}  // namespace gx
