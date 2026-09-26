// Adjacent GX primitives may share one backend draw, but their topology boundaries stay exact.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_core.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

static void check(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "draw batch test failed: %s\n", message);
    std::exit(1);
  }
}

int main() {
  std::vector<uint32_t> indices;
  check(gx::append_segment_indices(indices, 0x98, 4, 0) == gx::DrawTopology::Triangles,
        "triangle strip topology");
  check(gx::append_segment_indices(indices, 0x98, 4, 4) == gx::DrawTopology::Triangles,
        "second triangle strip topology");
  const std::vector<uint32_t> strips = {0,1,2, 2,1,3, 4,5,6, 6,5,7};
  check(indices == strips, "triangle strips crossed their command boundary");

  indices.clear();
  check(gx::append_segment_indices(indices, 0xA0, 4, 10) == gx::DrawTopology::Triangles,
        "triangle fan topology");
  const std::vector<uint32_t> fan = {10,11,12, 10,12,13};
  check(indices == fan, "triangle fan indices");

  indices.clear();
  check(gx::append_segment_indices(indices, 0xB0, 3, 20) == gx::DrawTopology::Lines,
        "line strip topology");
  const std::vector<uint32_t> lines = {20,21, 21,22};
  check(indices == lines, "line strip indices");

  std::puts("draw batch topology tests passed");
  return 0;
}
