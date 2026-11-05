// AMD FSR upscaling (AMD FSR SDK 2.4). AMD's own library picks the upscaler: FSR 4.1 (machine
// learned) on Radeon RX 6000 and newer, FidelityFX Super Resolution 3.1.6 (analytical) on every other
// Direct3D 12 GPU, NVIDIA and Intel included. Loaded at run time from amd_fidelityfx_loader_dx12.dll
// and amd_fidelityfx_upscaler_dx12.dll beside the program (a separate download, host/updater.cpp
// fsr_download), so without them the FSR modes are only not offered. It is fed what XeSS is fed:
// jittered EFB colour, reversed depth, pixel-space motion vectors (current to previous), and the HUD
// composite in the present pass.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

namespace gx {
namespace fsr {

// Upscaling-menu values that belong to FSR (after the five XeSS ones).
constexpr int kFirstMode = 11, kLastMode = 15;   // Native AA, Quality, Balanced, Performance, Ultra Performance
inline bool is_fsr_mode(int mode) { return mode >= kFirstMode && mode <= kLastMode; }
inline bool is_in_place(int mode) { return mode == kFirstMode; }   // Native AA: output = input size
const char* mode_name(int mode);

bool init(const std::wstring& exe_dir, ID3D12Device* device);
void shutdown();
bool available();
// Which upscaler AMD's library runs on this GPU ("FSR 4.1.2", "FSR 3.1.6"), once a context exists.
std::string provider();
bool optimal_size(int mode, uint32_t out_w, uint32_t out_h, uint32_t* rw, uint32_t* rh,
                  uint32_t* min_w, uint32_t* min_h, uint32_t* max_w, uint32_t* max_h);
bool set_options(int mode, uint32_t out_w, uint32_t out_h);

struct Inputs {
  ID3D12Resource* color; ID3D12Resource* depth; ID3D12Resource* mvec; ID3D12Resource* out;
  uint32_t in_left, in_top, in_w, in_h;
  float jitter_x, jitter_y;   // pixels, [-0.5, 0.5], in the convention XeSS takes
  float frame_ms;             // time since the previous upscale
  bool reset;
};
// Records the upscale. The inputs must already be in NON_PIXEL_SHADER_RESOURCE state and the output
// in UNORDERED_ACCESS (the caller owns the barriers). FSR reads its inputs from the resources' origin,
// so a region that does not start at (0, 0) is refused (false: the frame is presented natively).
bool evaluate(ID3D12GraphicsCommandList* list, const Inputs& in);

// Frame generation (amd_fidelityfx_framegeneration_dx12.dll beside the loader, the same download;
// AMD's library runs FSR Frame Generation 4.0 on Radeon RX 9000 and FidelityFX Frame Generation
// 3.1.6 on other GPUs). Used without AMD's frame interpolation swap chain: the generated picture is
// written into a texture of the renderer's, which presents it itself between two rendered frames,
// so the renderer's own pacing (the waitable swap chain, the presentation loop's deadlines) stays.
bool fg_available();
std::string fg_provider();
// A context for this picture size (the upscaled picture, before the present pass) and the largest
// render size. Made again for a new size; destroyed when generation stops. The GPU must be idle.
bool fg_create(uint32_t w, uint32_t h, uint32_t max_render_w, uint32_t max_render_h);
void fg_destroy();
bool fg_ready();
struct FrameInputs {
  ID3D12Resource* depth; ID3D12Resource* mvec;   // NON_PIXEL_SHADER_RESOURCE, as for evaluate
  ID3D12Resource* color;                          // this frame's upscaled picture, NON_PIXEL_SHADER_RESOURCE
  ID3D12Resource* out;                            // the generated picture, UNORDERED_ACCESS, the picture's size
  uint32_t render_w, render_h;
  float jitter_x, jitter_y, frame_ms;             // as for evaluate
  bool reset;                                     // the previous call's picture is not this one's predecessor
};
// Records the picture halfway between the previous call's and this one into `out`. False when
// nothing showable was made: the first frame after a reset or a pause, or a failed dispatch.
bool fg_generate(ID3D12GraphicsCommandList* list, const FrameInputs& in);

// The render size a mode asks for at an output size (AMD's per-axis ratios), for either renderer.
inline void render_size_for(int mode, uint32_t out_w, uint32_t out_h, uint32_t* rw, uint32_t* rh) {
  const float k = mode == 11 ? 1.0f : mode == 12 ? 1.5f : mode == 13 ? 1.7f : mode == 14 ? 2.0f : 3.0f;
  *rw = (uint32_t)(out_w / k + 0.5f); *rh = (uint32_t)(out_h / k + 0.5f);
}

// The Vulkan renderer's FSR (gx_fsr_vk.cpp, Windows: amd_fidelityfx_loader_vk.dll and
// amd_fidelityfx_upscaler_vk.dll beside the program). Handles pass as opaque values so this header
// needs no vulkan.h: VkDevice, VkPhysicalDevice, the loader's vkGetDeviceProcAddr, VkCommandBuffer,
// VkImage, VkFormat, VkImageUsageFlags.
struct VulkanDevice { void* device; void* physical; void* get_device_proc_addr; };
bool vk_init(const std::wstring& exe_dir, const VulkanDevice& device);
void vk_shutdown();
bool vk_available();
std::string vk_provider();
bool vk_set_options(int mode, uint32_t out_w, uint32_t out_h);
struct VulkanImage { uint64_t image; int format; uint32_t width, height, usage; };
struct VulkanInputs {
  void* command_buffer;
  VulkanImage color, depth, mvec, out;   // inputs in SHADER_READ_ONLY_OPTIMAL, the output in GENERAL
  uint32_t in_w, in_h;
  float jitter_x, jitter_y, frame_ms;
  bool reset;
};
bool vk_evaluate(const VulkanInputs& in);

}  // namespace fsr
}  // namespace gx
