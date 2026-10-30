// The Arcade look's 2D camera (gx_shader.cpp arcade_orthographic): on the plane the fighters stand on,
// the orthographic matrix puts every point where the game's perspective camera puts it, and depth
// keeps its order and its near and far ends.
#include "gx_shader.h"

#include <cmath>
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char* what, double a = 0, double b = 0) {
  if (!ok) { std::printf("FAIL %s (%g vs %g)\n", what, a, b); ++failures; }
}

// GX_SetProjection's perspective terms (MTXPerspective with an off-centre shift, as Melee's cameras use).
void perspective(float fovy_deg, float aspect, float n, float f, float shift_x, float shift_y, float p[6]) {
  const float cot = 1.0f / std::tan(fovy_deg * 3.14159265f / 360.0f);
  p[0] = cot / aspect; p[1] = shift_x; p[2] = cot; p[3] = shift_y;
  p[4] = -n / (f - n); p[5] = -(f * n) / (f - n);
}

// Clip -> NDC of view-space point v through a row-major 4x4 matrix.
void project(const float m[16], const float v[3], float out[3]) {
  float c[4];
  for (int r = 0; r < 4; ++r) c[r] = m[r * 4] * v[0] + m[r * 4 + 1] * v[1] + m[r * 4 + 2] * v[2] + m[r * 4 + 3];
  for (int i = 0; i < 3; ++i) out[i] = c[i] / c[3];
}

void perspective_matrix(const float p[6], float m[16]) {
  for (int i = 0; i < 16; ++i) m[i] = 0;
  m[0] = p[0]; m[2] = p[1]; m[5] = p[2]; m[6] = p[3]; m[10] = p[4]; m[11] = p[5]; m[14] = -1.0f;
}
}  // namespace

int main() {
  struct Case { float fov, aspect, n, f, sx, sy, d; } cases[] = {
      {30.0f, 73.0f / 60.0f, 10.0f, 20000.0f, 0.0f, 0.0f, 150.0f},
      {45.0f, 73.0f / 60.0f, 1.0f, 65535.0f, 0.1f, -0.05f, 420.0f},
      {18.0f, 4.0f / 3.0f, 50.0f, 5000.0f, -0.2f, 0.0f, 900.0f},
  };
  for (const auto& c : cases) {
    float p[6], persp[16], ortho[16];
    perspective(c.fov, c.aspect, c.n, c.f, c.sx, c.sy, p);
    perspective_matrix(p, persp);
    gx::arcade_orthographic(p, c.d, ortho);
    // Four sample points on the plane: same x and y on screen.
    const float pts[4][2] = {{0, 0}, {60, 40}, {-80, 25}, {35, -70}};
    for (const auto& xy : pts) {
      const float v[3] = {xy[0], xy[1], -c.d};
      float a[3], b[3];
      project(persp, v, a);
      project(ortho, v, b);
      check(std::fabs(a[0] - b[0]) < 1e-5f, "x on the plane", a[0], b[0]);
      check(std::fabs(a[1] - b[1]) < 1e-5f, "y on the plane", a[1], b[1]);
    }
    // Depth: on the plane the perspective camera's own depth (other cameras' draws sort against it);
    // near and far stay inside GX's -1..0, and nearer is always smaller.
    const float near_pt[3] = {0, 0, -c.n}, far_pt[3] = {0, 0, -c.f}, plane_pt[3] = {0, 0, -c.d};
    float a[3], b[3], pp[3], po[3];
    project(ortho, near_pt, a);
    project(ortho, far_pt, b);
    project(persp, plane_pt, pp);
    project(ortho, plane_pt, po);
    check(std::fabs(pp[2] - po[2]) < 1e-5f, "depth on the plane", po[2], pp[2]);
    check(a[2] >= -1.0f - 1e-4f, "near plane depth in range", a[2], -1);
    check(b[2] <= 1e-4f, "far plane depth in range", b[2], 0);
    float last = -2.0f;
    for (float z = c.n; z <= c.f; z *= 1.5f) {
      const float pt[3] = {0, 0, -z};
      float o[3];
      project(ortho, pt, o);
      check(o[2] > last, "depth order", o[2], last);
      last = o[2];
    }
  }
  std::printf(failures ? "projection_2d: %d failures\n" : "projection_2d: ok\n", failures);
  return failures ? 1 : 0;
}
