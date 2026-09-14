#include "authored_pose.h"
#include "Geometry.h"
#include "gx_shader.h"
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
  gx::DrawCall shader_draw{};
  auto uid = gx::make_vs_uid(shader_draw);
  shader_draw.xf_regs[0x0A] = 0xff00ff00;
  shader_draw.xf_regs[0x0C] = 0x12345678;
  auto recolored = gx::make_vs_uid(shader_draw);
  check(uid == recolored && uid.hash() == recolored.hash(), "color constants reuse the vertex shader");
  check(gx::generate_vertex_shader(uid) == gx::generate_vertex_shader(recolored), "recolored shader source is unchanged");
  shader_draw.xf_regs[0x0E] = 1;
  check(!(uid == gx::make_vs_uid(shader_draw)), "lighting controls remain in shader identity");
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
  gx::DrawCall d{}; d.identity = 7; d.object_generation = 1; d.vertex_count = 3; d.primitive = 0x90; d.components = 0; d.matrix_index_a = 0;
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
  solver.build(0.5, false, mats, true);
  check(std::memcmp(mats[0].pos, b.draws[0].posMatrices, sizeof mats[0].pos) == 0, "unsupported authored draw holds latest pose without delay");
  b.draws[0].bp.reg[gx::BP_PE_TOKEN_ID] = 123;
  b.draws[0].bp.reg[gx::BP_EFB_ADDR] = 0x12345;
  b.draws[0].bp.reg[gx::BP_TX_SETIMAGE1] = 0x100;
  b.draws[0].bp.reg[0xB4] = 0x200; // unit 4 storage address, same captured texture content
  solver.set_frames(&a, &b);
  check(solver.stats().paired == 1, "FIFO token and XFB destination do not change draw identity");
  b.draws[0].bp.reg[gx::BP_BLENDMODE] = 1;
  solver.set_frames(&a, &b);
  check(solver.stats().paired == 0, "material blend changes invalidate pairing");
  b.draws[0].bp.reg[gx::BP_BLENDMODE] = 0;
  b.sequence = 4; solver.set_frames(&a, &b);
  check(solver.stats().paired == 0, "frame gaps invalidate pairing");
  b.sequence = 2; b.vertices[0].pos[0] = 10; solver.set_frames(&a, &b);
  check(solver.stats().paired == 1, "matching rewritten primitive can pair");
  solver.build(0.5, true, mats, true);
  check(mats[0].vertices && near(mats[0].vertices[0].pos[0], 5), "rewritten geometry samples between known positions");
  a.draws[0].object_generation = b.draws[0].object_generation = 0;
  solver.set_frames(&a, &b);
  solver.build(.5, false, mats, true);
  check(solver.stats().paired == 0 && !mats[0].vertices &&
        std::memcmp(mats[0].pos, b.draws[0].posMatrices, sizeof mats[0].pos) == 0,
        "unidentified rewritten glyph/primitive cannot pair by submission ordinal alone");
  a.draws[0].object_generation = b.draws[0].object_generation = 1;
  solver.set_frames(&a, &b);
  solver.build(0.5, true, mats, false);
  check(!mats[0].vertices, "switching away from authored mode clears the vertex override");
  b.vertices[0].posmtx = 3; solver.set_frames(&a, &b);
  solver.build(0.5, true, mats, true);
  check(!mats[0].vertices && std::memcmp(mats[0].pos, b.draws[0].posMatrices, sizeof mats[0].pos) == 0,
        "changed matrix binding holds the complete current draw");
  b.vertices[0].posmtx = 0; b.vertices[0].pos[0] = solver.max_translation + 1; solver.set_frames(&a, &b);
  solver.build(0.5, true, mats, true);
  check(!mats[0].vertices && std::memcmp(mats[0].pos, b.draws[0].posMatrices, sizeof mats[0].pos) == 0,
        "geometry discontinuity holds current vertices and matrices together");
  b.vertices[0].pos[0] = 0; b.draws[0].xf_regs[0x26] = 1; solver.set_frames(&a, &b);
  check(solver.stats().paired == 0, "orthographic HUD draws retain exact pose");
  // Matrix selectors are discrete even when the vertex stream is unchanged.
  gx::Frame texture_previous, texture_current;
  texture_previous.sequence = 1; texture_current.sequence = 2;
  texture_previous.vertices.resize(3); texture_current.vertices.resize(3);
  gx::DrawCall texture_draw{}; texture_draw.identity = 15; texture_draw.vertex_count = 3;
  texture_draw.primitive = 0x90; texture_draw.xf_regs[0x3F] = 1;
  texture_draw.matrix_index_a = 6u << 6;
  rot_z(0, 0, texture_draw.posMatrices);
  rot_z(0, 0, texture_draw.posMatrices + 24);
  texture_previous.draws.push_back(texture_draw); texture_current.draws.push_back(texture_draw);
  texture_current.draws[0].matrix_index_a |= 3;
  solver.set_frames(&texture_previous, &texture_current);
  check(solver.stats().paired == 0, "changed CP position binding invalidates subframe history");
  texture_current.draws[0].matrix_index_a = 9u << 6;
  solver.set_frames(&texture_previous, &texture_current);
  check(solver.stats().paired == 0, "changed active CP texture binding invalidates subframe history");
  texture_current.draws[0].matrix_index_a = texture_draw.matrix_index_a;
  texture_current.draws[0].matrix_index_b = 12; // inactive texture generator
  solver.set_frames(&texture_previous, &texture_current);
  check(solver.stats().paired == 1, "inactive texture bindings do not reject a draw");
  texture_current.draws[0].xf_regs[0x40] = 2;
  solver.set_frames(&texture_previous, &texture_current);
  check(solver.stats().paired == 0, "changed texture projection invalidates subframe history");
  texture_current.draws[0].xf_regs[0x40] = 0;
  texture_current.draws[0].matrix_index_b = 0;
  // All eight per-vertex selectors must drive their own texture matrices. CP
  // defaults point elsewhere, so accidentally using them is observable.
  for (unsigned generator = 0; generator < 8; ++generator) {
    texture_previous.draws[0].xf_regs[0x3F] = texture_current.draws[0].xf_regs[0x3F] = generator + 1;
    texture_previous.draws[0].components = texture_current.draws[0].components = gx::VB_HAS_TEXMTXIDX0 << generator;
    for (auto& v : texture_previous.vertices) v.texmtx[generator] = 12;
    for (auto& v : texture_current.vertices) v.texmtx[generator] = 12;
    rot_z(0, 0, texture_previous.draws[0].posMatrices + 48);
    rot_z(0, 4, texture_current.draws[0].posMatrices + 48);
    solver.set_frames(&texture_previous, &texture_current);
    solver.build(.5, false, mats, true);
    check(near(mats[0].pos[51], 6), "per-vertex texture selector advances the matrix actually used by the shader");
    solver.build(.5, true, mats, true);
    check(near(mats[0].pos[51], 2), "per-vertex texture transform shares interpolation timeline");
  }
  // A large change to one coefficient rejects the complete texture transform.
  texture_current.draws[0].posMatrices[48] = 30;
  solver.set_frames(&texture_previous, &texture_current);
  solver.build(.5, false, mats, true);
  check(std::memcmp(mats[0].pos + 48, texture_current.draws[0].posMatrices + 48, 12 * sizeof(float)) == 0,
        "texture discontinuity holds all coefficients together");
  solver.build(.5, true, mats, true);
  check(std::memcmp(mats[0].pos + 48, texture_current.draws[0].posMatrices + 48, 12 * sizeof(float)) == 0,
        "texture interpolation cut also holds the complete current transform");
  texture_previous.draws[0].components = texture_current.draws[0].components = 0;
  texture_previous.draws[0].xf_regs[0x3F] = texture_current.draws[0].xf_regs[0x3F] = 1;
  texture_previous.draws[0].matrix_index_a = texture_current.draws[0].matrix_index_a = 1u << 6;
  texture_current.draws[0].posMatrices[7] = 4;
  solver.set_frames(&texture_previous, &texture_current);
  solver.build(.5, false, mats, true);
  check(near(mats[0].pos[7], 4), "partially overlapping texture rows do not overwrite a held position matrix");
  float scale2[12] = {2,0,0,0, 0,1,0,0, 0,0,1,0};
  gx::SubFrameSolver::fractional(prev, scale2, 0.5, true, 40, 3.0f, out, nrm, ident_n, ident_n, &st);
  check(near(nrm[0], 1.0f / std::sqrt(2.0f)), "normal delta uses inverse transpose");
  // A linear authored track: forward sampling must start at the current pose,
  // and refuse to predict across its animation boundary.
  gx::set_authored_interpolate(false);
  gx::AuthoredJoint joint;
  joint.generation = 1; joint.scale = {1,1,1}; joint.frame = 0; joint.rate = 1; joint.end = 2;
  joint.world = NativeMelee::Identity();
  NativeMelee::PackedTrack track; track.channel = 5; track.value_format = 128;
  track.bytes = {0x12,0,2,10}; joint.tracks.push_back(track);
  gx::AuthoredPose previous_pose, current_pose; previous_pose.joints.push_back(joint);
  joint.frame = 1; joint.translation[0] = 5; joint.world[3] = 5;
  current_pose.joints.push_back(joint);
  check(gx::sample_authored(previous_pose, current_pose, 0.5, joint.world.data(), out, nrm, ident_n), "valid forward authored track");
  check(near(out[3], 7.5f), "authored sampling advances beyond current rather than previous frame");
  // Animation-clock continuity cannot use the world-position tolerance: at
  // frame 1001 that tolerance admits a whole paused tick as forward motion.
  {
    auto paused_previous = current_pose, paused_current = current_pose;
    for (auto* pose : {&paused_previous, &paused_current}) {
      auto& j = pose->joints[0];
      j.frame = 1001; j.end = 1002; j.tracks[0].start_frame = -1000;
    }
    check(!gx::sample_authored(paused_previous, paused_current, .5, joint.world.data(), out, nrm, ident_n),
          "paused long-running animation does not predict another half tick");
    paused_previous.joints[0].frame = 1000;
    check(gx::sample_authored(paused_previous, paused_current, .5, joint.world.data(), out, nrm, ident_n) && near(out[3], 7.5f),
          "advancing long-running animation still samples at its declared rate");
    paused_previous.joints[0].rate = .5f;
    check(!gx::sample_authored(paused_previous, paused_current, .5, joint.world.data(), out, nrm, ident_n),
          "retimed animation waits for a continuous pair before predicting");
  }
  current_pose.joints[0].end = 1;
  check(!gx::sample_authored(previous_pose, current_pose, 0.5, joint.world.data(), out, nrm, ident_n), "authored sampling holds at animation boundary");
  current_pose.joints[0].end = 2;
  previous_pose.joints[0].tracks[0].channel = current_pose.joints[0].tracks[0].channel = 11;
  gx::set_authored_interpolate(true);
  for (double phase : {0.0, 0.5, 1.0}) {
    check(gx::sample_authored(previous_pose, current_pose, phase, joint.world.data(), out, nrm, ident_n), "unsupported local track can use continuous captured SRT");
    check(near(out[3], float(5*phase)), "held local joint shares interpolation endpoints and timeline");
  }
  gx::set_authored_interpolate(false);
  // A late envelope rejection must not leave earlier slots advanced. This
  // happens on a stationary camera when a later bone cannot be reconstructed.
  previous_pose.joints[0].tracks[0].channel = current_pose.joints[0].tracks[0].channel = 5;
  gx::AuthoredPose previous_skin, current_skin;
  previous_skin.envelope = current_skin.envelope = true;
  previous_skin.has_view = current_skin.has_view = true;
  previous_skin.view = current_skin.view = NativeMelee::Identity();
  gx::AuthoredBone previous_bone, current_bone;
  previous_bone.chain = std::make_shared<gx::AuthoredPose>(previous_pose);
  current_bone.chain = std::make_shared<gx::AuthoredPose>(current_pose);
  previous_bone.envelope = current_bone.envelope = NativeMelee::Identity();
  previous_skin.slots = {{{previous_bone}}, {{previous_bone}}};
  current_skin.slots = {{{current_bone}}, {{current_bone}}};
  current_skin.slots[1].bones[0].weight = 0.5f;
  float skin_pos[256]{}, skin_nrm[96]{}, sampled_pos[256], sampled_nrm[96];
  std::memcpy(skin_pos, current_pose.joints[0].world.data(), 12*sizeof(float));
  for (float& value : sampled_pos) value = -7;
  for (float& value : sampled_nrm) value = -9;
  check(!gx::sample_authored_envelope(previous_skin, current_skin, .5, skin_pos, skin_nrm,
                                    sampled_pos, sampled_nrm, nullptr), "later skin slot rejects changed weights");
  for (float value : sampled_pos) check(value == -7, "failed envelope leaves all position slots unchanged");
  for (float value : sampled_nrm) check(value == -9, "failed envelope leaves all normal slots unchanged");
  current_skin.slots[1].bones[0].weight = 1;
  std::memcpy(skin_pos + 12, current_pose.joints[0].world.data(), 12*sizeof(float));
  check(gx::sample_authored_envelope(previous_skin, current_skin, .5, skin_pos, skin_nrm,
                                   sampled_pos, sampled_nrm, nullptr), "valid skin publishes all sampled slots");
  check(near(sampled_pos[3], 7.5f) && near(sampled_pos[15], 7.5f), "both skin slots advance together");
  // Skin publication copies the complete matrix array; disjoint UV animation
  // must survive it and finish on the same presentation phase.
  texture_previous.draws[0].authored_pose = std::make_shared<gx::AuthoredPose>(previous_skin);
  texture_current.draws[0].authored_pose = std::make_shared<gx::AuthoredPose>(current_skin);
  texture_previous.draws[0].matrix_index_a = texture_current.draws[0].matrix_index_a = 12u << 6;
  std::memcpy(texture_current.draws[0].posMatrices, skin_pos, sizeof skin_pos);
  std::memcpy(texture_current.draws[0].normalMatrices, skin_nrm, sizeof skin_nrm);
  rot_z(0, 0, texture_previous.draws[0].posMatrices + 48);
  rot_z(0, 4, texture_current.draws[0].posMatrices + 48);
  solver.set_frames(&texture_previous, &texture_current);
  solver.build(.5, false, mats, true);
  check(solver.stats().authored == 1 && near(mats[0].pos[3], 7.5f) && near(mats[0].pos[51], 6),
        "skin and independent texture animation both survive complete pose publication");
  std::puts("sub-frame rigid fractions, cuts, blends and pairing passed");
}
