// CPU-side contract tests for the scene geometry that will feed D3D12 BLAS construction.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_dxr_scene.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void check(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "DXR scene test failed: %s\n", message);
    std::exit(1);
  }
}

void identity_matrices(gx::DrawCall& draw) {
  draw.posMatrices[0] = 1.0f;
  draw.posMatrices[5] = 1.0f;
  draw.posMatrices[10] = 1.0f;
  draw.normalMatrices[0] = 1.0f;
  draw.normalMatrices[4] = 1.0f;
  draw.normalMatrices[8] = 1.0f;
  draw.components = gx::VB_HAS_NRM0 | gx::VB_HAS_COL0 | gx::VB_HAS_UV0;
  auto set_float = [&](int index, float value) {
    std::memcpy(&draw.xf_regs[index], &value, sizeof(value));
  };
  set_float(0x1A, 320.0f);
  set_float(0x1B, -240.0f);
  set_float(0x20, 1.0f);
  set_float(0x22, 1.0f);
  set_float(0x24, 1.0f);
  set_float(0x25, 1.0f);
  draw.xf_regs[0x26] = 0;
}

gx::Vertex vertex(float x, float y, float z, float u, float v) {
  gx::Vertex out{};
  out.pos[0] = x; out.pos[1] = y; out.pos[2] = z;
  out.nrm[2] = 1.0f;
  out.col0[0] = 255; out.col0[1] = 128; out.col0[2] = 64; out.col0[3] = 255;
  out.uv[0][0] = u; out.uv[0][1] = v;
  return out;
}
}

int main() {
  gx::Frame frame;
  frame.scene_major = 2;
  frame.scene_minor = 2;
  frame.vertices = {vertex(-1, -1, -5, 0, 0), vertex(1, -1, -5, 1, 0),
                    vertex(0, 1, -5, 0.5f, 1), vertex(-2, -2, -4, 0, 0),
                    vertex(-1, -2, -4, 1, 0), vertex(-2, -1, -4, 0, 1),
                    vertex(0, 0, -3, 0, 0), vertex(1, 0, -3, 1, 0),
                    vertex(1, 1, -3, 1, 1)};

  gx::DrawCall camera{};
  identity_matrices(camera);
  camera.primitive = 0x90;
  camera.first_vertex = 0;
  camera.vertex_count = 3;

  gx::DrawCall other_camera{};
  identity_matrices(other_camera);
  other_camera.primitive = 0x90;
  other_camera.first_vertex = 3;
  other_camera.vertex_count = 3;
  other_camera.xf_regs[0x20] = 2.0f;

  gx::DrawCall line{};
  identity_matrices(line);
  line.primitive = 0xB0;
  line.first_vertex = 6;
  line.vertex_count = 3;

  frame.draws.push_back(camera);
  frame.draws.push_back(other_camera);
  frame.draws.push_back(line);
  frame.commands = {{gx::FrameCommand::Draw, 0}, {gx::FrameCommand::Draw, 1},
                    {gx::FrameCommand::Draw, 2}};

  gx::DxrScene scene;
  check(gx::build_dxr_scene(frame, scene), "extract a perspective match scene");
  check(scene.camera_draw == 0, "choose first perspective draw as camera");
  check(scene.geometries.size() == 1, "exclude another camera and line-only draw");
  check(scene.vertices.size() == 3 && scene.indices.size() == 3,
        "preserve triangle vertex and index counts");
  check(scene.indices[0] == 0 && scene.indices[1] == 1 && scene.indices[2] == 2,
        "use local triangle indices in the packed scene");
  check(std::fabs(scene.vertices[0].position[0] + 1.0f) < 0.0001f &&
        std::fabs(scene.vertices[0].position[2] + 5.0f) < 0.0001f,
        "apply GX position matrices to view-space positions");
  check(std::fabs(scene.vertices[1].normal[2] - 1.0f) < 0.0001f,
        "normalize GX transformed normal");
  check(std::fabs(scene.vertices[0].color[1] - 128.0f / 255.0f) < 0.0001f &&
        scene.vertices[2].uv[1] == 1.0f,
        "preserve normalized vertex color and UV0");

  std::vector<gx::DrawMatrices> overrides(frame.draws.size());
  for (auto& m : overrides) {
    m.pos[0] = 1.0f; m.pos[3] = 10.0f;
    m.pos[5] = 1.0f; m.pos[10] = 1.0f;
    m.nrm[0] = 1.0f; m.nrm[4] = 1.0f; m.nrm[8] = 1.0f;
  }
  check(gx::build_dxr_scene(frame, scene, overrides.data()), "extract an overridden sub-frame");
  check(std::fabs(scene.vertices[0].position[0] - 9.0f) < 0.0001f,
        "use render-thread sub-frame transforms");

  frame.scene_minor = 1;
  check(!gx::build_dxr_scene(frame, scene), "reject non-match menu geometry");
  check(scene.vertices.empty() && scene.geometries.empty(), "clear outputs on rejection");
  std::puts("DXR scene extraction tests passed");
  return 0;
}
