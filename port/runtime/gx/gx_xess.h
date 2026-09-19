// Intel XeSS Super Resolution (XeSS SDK 3.0). Loaded at run time from libxess.dll next to the
// executable, so a missing or unsupported library only means the XeSS modes are not offered. It works
// on any GPU with Shader Model 6.4 (Intel, NVIDIA, AMD). It shares everything the DLSS path already
// produces: jittered EFB colour, inverted depth, pixel-space motion vectors (current to previous) and
// the HUD composite in the present blit.
#pragma once
#include <cstdint>
#include <string>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

namespace gx {
namespace xess {

// Upscaling-menu values that belong to XeSS (after the six DLSS ones).
constexpr int kFirstMode = 6, kLastMode = 10;   // AA, Ultra Quality, Quality, Balanced, Performance
inline bool is_xess_mode(int mode) { return mode >= kFirstMode && mode <= kLastMode; }
inline bool is_in_place(int mode) { return mode == kFirstMode; }   // XeSS AA: output = input size
const char* mode_name(int mode);

bool init(const std::wstring& exe_dir, ID3D12Device* device);
void shutdown();
bool available();
bool optimal_size(int mode, uint32_t out_w, uint32_t out_h, uint32_t* rw, uint32_t* rh,
                  uint32_t* min_w, uint32_t* min_h, uint32_t* max_w, uint32_t* max_h);
bool set_options(int mode, uint32_t out_w, uint32_t out_h);

struct Inputs {
  ID3D12Resource* color; ID3D12Resource* depth; ID3D12Resource* mvec; ID3D12Resource* out;
  uint32_t in_left, in_top, in_w, in_h;
  float jitter_x, jitter_y;   // pixels, [-0.5, 0.5]
  bool reset;
};
// Records the upscale. The inputs must already be in NON_PIXEL_SHADER_RESOURCE state and the output
// in UNORDERED_ACCESS (the caller owns the barriers).
bool evaluate(ID3D12GraphicsCommandList* list, const Inputs& in);

}  // namespace xess
}  // namespace gx
