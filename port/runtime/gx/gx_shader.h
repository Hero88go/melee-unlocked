// HLSL generation for GX vertex/pixel pipelines (transcribed from Dolphin's shader generators).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include "gx_core.h"

namespace gx {

// Constant buffer layouts shared with the generated HLSL.
struct VSConstants {
  float projection[4][4];
  float depthparams[4];
  float viewparams[4];
  float materials[4][4];
  float lights[40][4];
  float texmatrices[24][4];
  float transformmatrices[64][4];
  float normalmatrices[32][4];
  float posttransformmatrices[64][4];
};
struct PSConstants {
  int32_t colors[4][4];
  int32_t kcolors[4][4];
  int32_t alpha[4];
  float texdims[8][4];
  int32_t zbias[2][4];
  int32_t indtexscale[2][4];
  int32_t indtexmtx[6][4];
  int32_t fogcolor[4];
  int32_t fogi[4];
  float fogf[2][4];
  float zslope[4];
  int32_t flags[4];
  float efbscale[4];
};

struct VSUid {
  uint32_t components;
  uint32_t numTexGens, numColorChans;
  uint32_t xf_regs[0x58];   // channel controls, texgen infos, dual tex, post infos
  uint64_t hash() const;
  bool operator==(const VSUid& o) const;
};
struct PSUid {
  BPMemory bp;              // full BP image; hash covers the relevant registers only
  uint32_t numTexGens;
  uint64_t hash() const;
  bool operator==(const PSUid& o) const;
};

VSUid make_vs_uid(const DrawCall& dc);
PSUid make_ps_uid(const DrawCall& dc);
std::string generate_vertex_shader(const VSUid& uid);
std::string generate_pixel_shader(const PSUid& uid);

// Fill constants for a draw.
void fill_vs_constants(const DrawCall& dc, VSConstants& out, int efb_scale, const DrawMatrices* override_matrices = nullptr);
void fill_ps_constants(const DrawCall& dc, PSConstants& out, int efb_scale);

}  // namespace gx
