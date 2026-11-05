// AMD FSR upscaling for the Vulkan renderer, through AMD's signed Vulkan DLLs (see gx_fsr.h). The same
// inputs and conventions as the Direct3D 12 path (gx_fsr.cpp); AMD's Vulkan build is at feature parity.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_fsr.h"
#include "host.h"

#include <windows.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <cstdlib>
#include <string>
#include "../../third_party/fsr/api/include/ffx_api.h"
#include "../../third_party/fsr/api/include/vk/ffx_api_vk.h"
#include "../../third_party/fsr/upscalers/include/ffx_upscale.h"

namespace gx {
namespace fsr {
namespace {

HMODULE g_module = nullptr;
PfnFfxCreateContext p_create = nullptr;
PfnFfxDestroyContext p_destroy = nullptr;
PfnFfxQuery p_query = nullptr;
PfnFfxDispatch p_dispatch = nullptr;
VulkanDevice g_device{};
ffxContext g_ctx = nullptr;
bool g_ok = false;
uint32_t g_out_w = 0, g_out_h = 0;
std::string g_provider;

void destroy_context() {
  if (g_ctx && p_destroy) p_destroy(&g_ctx, nullptr);
  g_ctx = nullptr;
}

bool create_context(uint32_t out_w, uint32_t out_h) {
  destroy_context();
  ffxCreateBackendVKDesc backend{};
  backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
  backend.deviceContext.vkDevice = (VkDevice)g_device.device;
  backend.deviceContext.vkPhysicalDevice = (VkPhysicalDevice)g_device.physical;
  backend.deviceContext.vkDeviceProcAddr = (PFN_vkGetDeviceProcAddr)g_device.get_device_proc_addr;
  ffxCreateContextDescUpscale desc{};
  desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
  desc.header.pNext = &backend.header;
  desc.flags = FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
  desc.maxRenderSize = {out_w, out_h};
  desc.maxUpscaleSize = {out_w, out_h};
  const ffxReturnCode_t r = p_create(&g_ctx, &desc.header, nullptr);
  if (r != FFX_API_RETURN_OK) { host::log("fsr (vulkan): context for %ux%u refused (%u)", out_w, out_h, (unsigned)r); g_ctx = nullptr; return false; }
  ffxQueryGetProviderVersion version{};
  version.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
  if (p_query(&g_ctx, &version.header) == FFX_API_RETURN_OK && version.versionName) g_provider = version.versionName;
  return true;
}

FfxApiResource resource(const VulkanImage& img, uint32_t state) {
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType = VK_IMAGE_TYPE_2D; info.format = (VkFormat)img.format; info.extent = {img.width, img.height, 1};
  info.mipLevels = 1; info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT; info.usage = (VkImageUsageFlags)img.usage;
  const VkImage image = (VkImage)(uintptr_t)img.image;
  return ffxApiGetResourceVK((void*)image, ffxApiGetImageResourceDescriptionVK(image, info, 0), state);
}

}  // namespace

bool vk_init(const std::wstring& exe_dir, const VulkanDevice& device) {
  const std::wstring loader = exe_dir + L"\\amd_fidelityfx_loader_vk.dll", upscaler = exe_dir + L"\\amd_fidelityfx_upscaler_vk.dll";
  if (GetFileAttributesW(loader.c_str()) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW(upscaler.c_str()) == INVALID_FILE_ATTRIBUTES) {
    host::log("fsr (vulkan): AMD FSR for Vulkan is not installed (PC settings > Video has the download)");
    return false;
  }
  g_module = LoadLibraryW(loader.c_str());
  if (!g_module) { host::log("fsr (vulkan): cannot load amd_fidelityfx_loader_vk.dll (%lu)", GetLastError()); return false; }
  p_create = (PfnFfxCreateContext)(void*)GetProcAddress(g_module, "ffxCreateContext");
  p_destroy = (PfnFfxDestroyContext)(void*)GetProcAddress(g_module, "ffxDestroyContext");
  p_query = (PfnFfxQuery)(void*)GetProcAddress(g_module, "ffxQuery");
  p_dispatch = (PfnFfxDispatch)(void*)GetProcAddress(g_module, "ffxDispatch");
  if (!p_create || !p_destroy || !p_query || !p_dispatch) { host::log("fsr (vulkan): the FSR loader is missing entry points"); return false; }
  g_device = device;
  if (!create_context(1280, 720)) { host::log("fsr (vulkan): not supported on this GPU and driver"); return false; }
  destroy_context();
  g_ok = true;
  host::log("fsr (vulkan): available (%s)", g_provider.empty() ? "provider not reported" : g_provider.c_str());
  return true;
}

void vk_shutdown() {
  destroy_context();
  g_ok = false; g_out_w = g_out_h = 0;
}

bool vk_available() { return g_ok; }
std::string vk_provider() { return g_provider; }

bool vk_set_options(int mode, uint32_t out_w, uint32_t out_h) {
  if (!g_ok) return false;
  if (g_ctx && g_out_w == out_w && g_out_h == out_h) return true;
  if (!create_context(out_w, out_h)) return false;
  g_out_w = out_w; g_out_h = out_h;
  host::log("fsr (vulkan): %s (%s) -> output %ux%u", mode_name(mode), g_provider.empty() ? "AMD FSR" : g_provider.c_str(), out_w, out_h);
  return true;
}

bool vk_evaluate(const VulkanInputs& in) {
  if (!g_ok || !g_ctx) return false;
  static const float mv_scale = [] { const char* v = std::getenv("MELEE_FSR_MV_SCALE"); return v && *v ? (float)std::atof(v) : 1.0f; }();
  ffxDispatchDescUpscale d{};
  d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
  d.commandList = in.command_buffer;
  d.color = resource(in.color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
  d.depth = resource(in.depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
  d.motionVectors = resource(in.mvec, FFX_API_RESOURCE_STATE_COMPUTE_READ);
  d.output = resource(in.out, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
  d.jitterOffset = {in.jitter_x, in.jitter_y};
  d.motionVectorScale = {mv_scale, mv_scale};
  d.renderSize = {in.in_w, in.in_h};
  d.upscaleSize = {g_out_w, g_out_h};
  d.enableSharpening = false;
  d.frameTimeDelta = in.frame_ms > 0.0f && in.frame_ms < 100.0f ? in.frame_ms : 1000.0f / 60.0f;
  d.preExposure = 1.0f;
  d.reset = in.reset;
  d.cameraNear = 1.0f;   // as gx_fsr.cpp: Melee's match cameras, roughly
  d.cameraFar = 20000.0f;
  d.cameraFovAngleVertical = 0.5f;
  d.viewSpaceToMetersFactor = 1.0f;
  d.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
  const ffxReturnCode_t r = p_dispatch(&g_ctx, &d.header);
  if (r != FFX_API_RETURN_OK) {
    static int logged = 0;
    if (logged++ < 5) host::log("fsr (vulkan): dispatch failed (%u)", (unsigned)r);
    return false;
  }
  return true;
}

}  // namespace fsr
}  // namespace gx
