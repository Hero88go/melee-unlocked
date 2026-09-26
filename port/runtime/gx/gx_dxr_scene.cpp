// Frame-local geometry extraction for DXR. This is the input side of the ray scene; GPU BLAS/TLAS
// construction and traced shading are separate steps and must not be inferred from this module.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_dxr_scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gx {
namespace {

bool finite_vertex(const DxrSceneVertex& v) {
  for (float x : v.position) if (!std::isfinite(x)) return false;
  for (float x : v.normal) if (!std::isfinite(x)) return false;
  for (float x : v.color) if (!std::isfinite(x)) return false;
  for (float x : v.uv) if (!std::isfinite(x)) return false;
  return true;
}

bool same_camera(const DrawCall& a, const DrawCall& b) {
  return std::memcmp(&a.xf_regs[0x20], &b.xf_regs[0x20], 7 * sizeof(uint32_t)) == 0;
}

bool append_draw_indices(const Frame& frame, const DrawCall& draw,
                         std::vector<uint32_t>& indices) {
  const size_t before = indices.size();
  if (draw.segment_count != 0) {
    if (draw.first_segment > frame.segments.size() ||
        draw.segment_count > frame.segments.size() - draw.first_segment)
      return false;
    for (uint32_t i = 0; i < draw.segment_count; ++i) {
      const DrawSegment& segment = frame.segments[draw.first_segment + i];
      if (segment.first_vertex < draw.first_vertex ||
          segment.first_vertex - draw.first_vertex > draw.vertex_count ||
          segment.vertex_count > draw.vertex_count - (segment.first_vertex - draw.first_vertex))
        return false;
      if (append_segment_indices(indices, segment.primitive, segment.vertex_count,
                                 segment.first_vertex - draw.first_vertex) != DrawTopology::Triangles)
        return false;
    }
  } else if (append_segment_indices(indices, draw.primitive, draw.vertex_count, 0) !=
             DrawTopology::Triangles) {
    return false;
  }
  if (indices.size() == before || (indices.size() - before) % 3 != 0) return false;
  for (size_t i = before; i < indices.size(); ++i)
    if (indices[i] >= draw.vertex_count) return false;
  return true;
}

bool transform_draw_vertex(const DrawCall& draw, const Vertex& source,
                           const DrawMatrices* override_matrices,
                           DxrSceneVertex& target) {
  const float* positions = override_matrices ? override_matrices->pos : draw.posMatrices;
  const float* normals = override_matrices ? override_matrices->nrm : draw.normalMatrices;
  const uint32_t matrix = source.posmtx;
  // The raster shader indexes three float4 position rows at posmtx and three float3 normal rows
  // at (posmtx >= 32 ? posmtx - 32 : posmtx). Check the same bounds before touching either array.
  if (matrix > 61) return false;
  const float* p = positions + matrix * 4;
  for (int row = 0; row < 3; ++row)
    target.position[row] = p[row * 4] * source.pos[0] + p[row * 4 + 1] * source.pos[1] +
                           p[row * 4 + 2] * source.pos[2] + p[row * 4 + 3];
  if (draw.components & VB_HAS_NRM0) {
    const uint32_t normal_matrix = matrix >= 32 ? matrix - 32 : matrix;
    if (normal_matrix > 29) return false;
    const float* n = normals + normal_matrix * 3;
    for (int row = 0; row < 3; ++row)
      target.normal[row] = n[row * 3] * source.nrm[0] + n[row * 3 + 1] * source.nrm[1] +
                           n[row * 3 + 2] * source.nrm[2];
    const float length2 = target.normal[0] * target.normal[0] +
                          target.normal[1] * target.normal[1] +
                          target.normal[2] * target.normal[2];
    if (length2 > 1.0e-20f && std::isfinite(length2)) {
      const float scale = 1.0f / std::sqrt(length2);
      for (float& x : target.normal) x *= scale;
    } else {
      target.normal[0] = target.normal[1] = target.normal[2] = 0.0f;
    }
  }
  for (int c = 0; c < 4; ++c)
    target.color[c] = (draw.components & VB_HAS_COL0) ? source.col0[c] / 255.0f : 1.0f;
  target.uv[0] = (draw.components & VB_HAS_UV0) ? source.uv[0][0] : 0.0f;
  target.uv[1] = (draw.components & VB_HAS_UV0) ? source.uv[0][1] : 0.0f;
  return finite_vertex(target);
}

}  // namespace

void DxrScene::clear() {
  vertices.clear();
  indices.clear();
  geometries.clear();
  std::memset(gx_projection, 0, sizeof(gx_projection));
  projection_type = UINT32_MAX;
  camera_draw = UINT32_MAX;
}

bool build_dxr_scene(const Frame& frame, DxrScene& out,
                     const DrawMatrices* overrides) {
  out.clear();
  if (!frame_in_match(frame)) return false;

  const DrawCall* camera = nullptr;
  for (const FrameCommand& command : frame.commands) {
    if (command.kind != FrameCommand::Draw || command.index >= frame.draws.size()) continue;
    const DrawCall& draw = frame.draws[command.index];
    if (draw.xf_regs[0x26] == 0) {
      camera = &draw;
      out.camera_draw = command.index;
      break;
    }
  }
  if (!camera) return false;
  std::memcpy(out.gx_projection, &camera->xf_regs[0x20], sizeof(out.gx_projection));
  out.projection_type = camera->xf_regs[0x26];

  for (const FrameCommand& command : frame.commands) {
    if (command.kind != FrameCommand::Draw || command.index >= frame.draws.size()) continue;
    const DrawCall& draw = frame.draws[command.index];
    if (!same_camera(draw, *camera) || draw.vertex_count < 3 ||
        draw.first_vertex > frame.vertices.size() ||
        draw.vertex_count > frame.vertices.size() - draw.first_vertex)
      continue;

    std::vector<uint32_t> local_indices;
    local_indices.reserve((size_t)draw.vertex_count * 3);
    if (!append_draw_indices(frame, draw, local_indices)) continue;

    std::vector<DxrSceneVertex> local_vertices;
    local_vertices.resize(draw.vertex_count);
    const DrawMatrices* matrix_override = overrides ? overrides + command.index : nullptr;
    const Vertex* source = matrix_override && matrix_override->vertices ?
        matrix_override->vertices : frame.vertices.data() + draw.first_vertex;
    bool valid = true;
    for (uint32_t i = 0; i < draw.vertex_count; ++i) {
      if (!transform_draw_vertex(draw, source[i], matrix_override, local_vertices[i])) {
        valid = false;
        break;
      }
    }
    if (!valid) continue;

    DxrSceneGeometry geometry;
    geometry.draw_index = command.index;
    geometry.first_vertex = (uint32_t)out.vertices.size();
    geometry.vertex_count = draw.vertex_count;
    geometry.first_index = (uint32_t)out.indices.size();
    geometry.index_count = (uint32_t)local_indices.size();
    out.vertices.insert(out.vertices.end(), local_vertices.begin(), local_vertices.end());
    for (uint32_t index : local_indices) out.indices.push_back(geometry.first_vertex + index);
    out.geometries.push_back(geometry);
  }
  if (out.geometries.empty()) {
    out.clear();
    return false;
  }
  return true;
}

}  // namespace gx
