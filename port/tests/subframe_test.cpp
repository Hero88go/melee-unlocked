// Sub-frame solver: fractional rigid deltas, cut detection, draw pairing.
#include "subframe.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static void check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); std::fflush(stdout); std::exit(1); } }
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static void rot_z(float deg, float tx, float out[12]) {
  float r = deg * 3.14159265f / 180.0f, c = std::cos(r), s = std::sin(r);
  float m[12] = {c, -s, 0, tx, s, c, 0, 0, 0, 0, 1, 0};
  std::memcpy(out, m, sizeof m);
}
int main() {
  const float ident_n[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  float prev[12], cur[12], out[12], nrm[9];
  gx::SubFrameStats st;
  // 90 degree rotation + 8 unit translation per frame: half phase gives 45 degrees and 4 units.
  rot_z(0, 0, prev); rot_z(90, 8, cur);
  gx::SubFrameSolver::fractional(prev, cur, 0.5, true, 40, 3.0f, out, nrm, ident_n, ident_n, &st);
  check(near(out[0], std::cos(3.14159265f / 4)) && near(out[4], std::sin(3.14159265f / 4)), "interpolated rotation is 45 degrees");
  // The screw axis passes through (4, 4): halfway along the arc the origin maps to (4, 4 - 4*sqrt2).
  const float s2 = std::sqrt(2.0f);
  check(near(out[3], 4.0f) && near(out[7], 4.0f - 4.0f * s2), "interpolated translation follows the screw path");
  check(near(nrm[0], out[0]) && near(nrm[3], out[4]), "normal matrix follows the rotation");
  check(st.rigid == 1, "rigid path used");
  // Extrapolation continues the same screw: t = 0.5 beyond cur gives 135 degrees at (4 + 4*sqrt2, 4).
  gx::SubFrameSolver::fractional(prev, cur, 0.5, false, 40, 3.0f, out, nrm, ident_n, ident_n, &st);
  check(near(out[0], std::cos(3 * 3.14159265f / 4)) && near(out[3], 4.0f + 4.0f * s2) && near(out[7], 4.0f), "extrapolated pose continues");
  // Pure translation halves exactly.
  float slide[12] = {1, 0, 0, 6, 0, 1, 0, -2, 0, 0, 1, 0};
  gx::SubFrameSolver::fractional(prev, slide, 0.5, true, 40, 3.0f, out, nullptr, nullptr, nullptr, &st);
  check(near(out[3], 3.0f) && near(out[7], -1.0f) && near(out[0], 1.0f), "pure translation halves");
  // Exact endpoints.
  gx::SubFrameSolver::fractional(prev, cur, 0.0, false, 40, 3.0f, out, nullptr, nullptr, nullptr, &st);
  check(std::memcmp(out, cur, sizeof out) == 0, "t = 0 extrapolation is the current pose");
  gx::SubFrameSolver::fractional(prev, cur, 1.0, true, 40, 3.0f, out, nullptr, nullptr, nullptr, &st);
  for (int i = 0; i < 12; ++i) check(near(out[i], cur[i], 1e-3f), "t = 1 interpolation reaches the current pose");
  // A camera cut (large translation) keeps the exact simulation pose.
  rot_z(0, 500, cur); st = {};
  gx::SubFrameSolver::fractional(prev, cur, 0.5, false, 40, 3.0f, out, nullptr, nullptr, nullptr, &st);
  check(std::memcmp(out, cur, sizeof out) == 0 && st.cuts == 1, "large delta treated as a cut");
  st = {};
  gx::SubFrameSolver::fractional(prev, cur, 0.5, true, 40, 3.0f, out, nullptr, nullptr, nullptr, &st);
  check(std::memcmp(out, cur, sizeof out) == 0 && st.cuts == 1, "interpolation cut retains current pose");
  // Non-rigid delta (anisotropic scale x3) retains the current pose.
  float scaled[12] = {3, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}; st = {};
  gx::SubFrameSolver::fractional(prev, scaled, 0.5, true, 40, 3.0f, out, nullptr, nullptr, nullptr, &st);
  check(st.cuts == 1 && near(out[0], 3.0f), "non-rigid delta retains current pose");
  // Draw pairing by identity through the solver.
  gx::Frame a, b; a.sequence = 1; b.sequence = 2; a.vertices.resize(3); b.vertices.resize(3);
  gx::DrawCall d{}; d.identity = 7; d.vertex_count = 3; d.primitive = 0x90; d.components = 0; d.matrix_index_a = 0;
  d.xf_regs[0x3F] = 0;
  rot_z(0, 0, d.posMatrices); std::memcpy(d.normalMatrices, ident_n, sizeof ident_n);
  a.draws.push_back(d);
  rot_z(90, 8, d.posMatrices); b.draws.push_back(d);
  gx::DrawCall unpaired = d; unpaired.identity = 9; b.draws.push_back(unpaired);
  gx::SubFrameSolver solver; std::vector<gx::DrawMatrices> mats;
  solver.max_rotation = 3.0f;
  solver.set_frames(&a, &b);
  check(solver.stats().paired == 1, "one draw paired by identity");
  solver.build(0.5, true, mats);
  std::printf("paired row0: %g %g %g %g | %g %g %g %g (paired=%u rigid=%u blended=%u cuts=%u)\n", mats[0].pos[0], mats[0].pos[1], mats[0].pos[2], mats[0].pos[3],
              mats[0].pos[4], mats[0].pos[5], mats[0].pos[6], mats[0].pos[7], solver.stats().paired, solver.stats().rigid, solver.stats().blended, solver.stats().cuts);
  check(mats.size() == 2 && near(mats[0].pos[3], 4.0f), "paired draw gets the fractional pose");
  check(std::memcmp(mats[1].pos, unpaired.posMatrices, sizeof mats[1].pos) == 0, "unpaired draw keeps its pose");
  b.sequence = 4; solver.set_frames(&a, &b);
  check(solver.stats().paired == 0, "frame gaps invalidate pairing");
  b.sequence = 2; b.vertices[0].pos[0] = 10; solver.set_frames(&a, &b);
  check(solver.stats().paired == 0, "changed geometry cannot reuse a draw identity");
  b.vertices[0].pos[0] = 0; b.draws[0].xf_regs[0x26] = 1; solver.set_frames(&a, &b);
  check(solver.stats().paired == 0, "orthographic HUD draws retain exact pose");
  float scale2[12] = {2,0,0,0, 0,1,0,0, 0,0,1,0};
  gx::SubFrameSolver::fractional(prev, scale2, 0.5, true, 40, 3.0f, out, nrm, ident_n, ident_n, &st);
  check(near(nrm[0], 1.0f / std::sqrt(2.0f)), "normal delta uses inverse transpose");
  std::puts("sub-frame rigid fractions, cuts, blends and pairing passed");
}
