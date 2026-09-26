// Frame-local triangle data for the D3D12 ray-tracing backend.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "gx_core.h"

namespace gx {

struct DxrSceneVertex {
  float position[3];       // GX position-matrix result, in the main camera's view space
  float normal[3];         // GX normal-matrix result
  float color[4];          // Vertex color 0, normalized to [0, 1]
  float uv[2];             // Texture coordinate 0
};

struct DxrSceneGeometry {
  uint32_t draw_index = 0;
  uint32_t first_vertex = 0, vertex_count = 0;
  uint32_t first_index = 0, index_count = 0;
};

struct DxrScene {
  std::vector<DxrSceneVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<DxrSceneGeometry> geometries;
  float gx_projection[6]{}; // left, right, top, bottom, near, far as authored by GX
  uint32_t projection_type = UINT32_MAX;
  uint32_t camera_draw = UINT32_MAX;
  void clear();
};

// Extract opaque-agnostic triangle geometry from the current in-match GX frame. Positions and
// normals use the same per-vertex GX matrices as the raster vertex shader. Draw ordering and strip
// boundaries are preserved. Lines, overlays from other cameras, and malformed ranges are skipped.
// `overrides`, when present, are the render-thread sub-frame matrices/vertex streams for this draw.
bool build_dxr_scene(const Frame& frame, DxrScene& out,
                     const DrawMatrices* overrides = nullptr);

}  // namespace gx
