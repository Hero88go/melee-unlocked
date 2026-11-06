// Vulkan renderer for the shared GX command decoder: one backend for Windows and Linux.
// The generated GX shaders (gx_shader.cpp, HLSL) stay the single TEV/lighting implementation:
// Windows compiles them to SPIR-V with the DirectX Shader Compiler the game already ships
// (dxcompiler.dll), Linux with glslangValidator as the OpenGL renderer does. The loader (vulkan-1 /
// libvulkan.so.1) is opened at run time, so a machine without Vulkan still starts on another backend.
// What it draws follows the Direct3D 12 renderer: the EFB at the internal scale, EFB copies into
// textures keyed by their address, and the shared present pass (present_shader.h: box filter,
// sharpening, colour, ambient occlusion, the CRT model) onto the window. Pacing follows it too: two
// frames in flight, the presentation loop's deadline, and the display's own vertical blanks for the
// G-Sync / FreeSync verdict (vrr_detect.h, vblank_clock.h); a present wait (VK_KHR_present_wait) is
// there for measurement, off by default. AMD FSR upscales on Vulkan too (gx_fsr_vk.cpp); NVIDIA
// DLSS, Reflex and XeSS stay Direct3D 12 features.
// SPDX-License-Identifier: GPL-2.0-or-later
#define VK_NO_PROTOTYPES
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <dxgi.h>
#include <dxcapi.h>
#include <wrl/client.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif
#include <vulkan/vulkan.h>
#include "gx_vulkan.h"
#include "gx_shader.h"
#include "gx_texture.h"
#include "gx_retro.h"
#include "crt_shader.h"
#include "present_shader.h"
#include "user_shader.h"
#include "vulkan_recipes.h"
#include "temporal_history.h"
#ifdef _WIN32
#include "gx_fsr.h"
#include "gx_streamline.h"   // the jitter sequence the Direct3D 12 upscalers use
#endif
#include "host.h"
#include "exi_slippi.h"
#include "window.h"
#include "sim_work.h"
#include "fullscreen_state.h"
#include "vrr_detect.h"
#include "gpu_readings.h"
#include "video_background.h"
#include "video_background_learning.h"
#include "gx_backend.h"   // solver_pair_counter, for the "gpu:" line
#ifdef _WIN32
#include "vblank_clock.h"
#endif
#ifdef GX_PC_SETTINGS
#include "pc_settings.h"
#include "pc_settings_shared.h"
#include "texture_pack.h"
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <vector>

namespace gx {
namespace {

// ---- Loader -------------------------------------------------------------------------------------

#define MU_VK_GLOBAL(X) X(vkCreateInstance) X(vkEnumerateInstanceExtensionProperties) X(vkEnumerateInstanceVersion)
// Extension functions load as null when the extension is not enabled; every use checks first.
#ifdef _WIN32
#define MU_VK_SURFACE_PLATFORM(X) X(vkCreateWin32SurfaceKHR) X(vkGetPhysicalDeviceSurfacePresentModes2EXT)
#define MU_VK_DEVICE_PLATFORM(X) X(vkAcquireFullScreenExclusiveModeEXT) X(vkReleaseFullScreenExclusiveModeEXT)
#else
#define MU_VK_SURFACE_PLATFORM(X)
#define MU_VK_DEVICE_PLATFORM(X) X(vkGetRefreshCycleDurationGOOGLE) X(vkGetPastPresentationTimingGOOGLE)
#endif
#define MU_VK_INSTANCE(X) X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
  X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceFeatures) \
  X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkDestroySurfaceKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR) \
  X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
  X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkEnumerateDeviceExtensionProperties) X(vkGetPhysicalDeviceFeatures2) \
  X(vkGetPhysicalDeviceMemoryProperties2) X(vkCreateDebugUtilsMessengerEXT) X(vkDestroyDebugUtilsMessengerEXT) MU_VK_SURFACE_PLATFORM(X)
#define MU_VK_DEVICE(X) X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) \
  X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) X(vkQueueSubmit) X(vkQueueWaitIdle) \
  X(vkDeviceWaitIdle) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkResetCommandPool) \
  X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) \
  X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) \
  X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkCreateImage) \
  X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) \
  X(vkCreateSampler) X(vkDestroySampler) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreatePipelineLayout) \
  X(vkDestroyPipelineLayout) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool) \
  X(vkDestroyDescriptorPool) X(vkResetDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
  X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreatePipelineCache) X(vkDestroyPipelineCache) \
  X(vkGetPipelineCacheData) X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkCmdPipelineBarrier) X(vkCmdBindPipeline) \
  X(vkCmdBindDescriptorSets) X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer) X(vkCmdSetViewport) X(vkCmdSetScissor) \
  X(vkCmdDrawIndexed) X(vkCmdDraw) X(vkCmdClearAttachments) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdCopyImage) \
  X(vkCmdPushConstants) X(vkWaitForPresentKHR) X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkCmdResetQueryPool) \
  X(vkCmdWriteTimestamp) X(vkGetQueryPoolResults) X(vkGetFenceStatus) X(vkCmdPushDescriptorSetKHR) MU_VK_DEVICE_PLATFORM(X)

#define MU_VK_DECLARE(name) PFN_##name name = nullptr;
MU_VK_GLOBAL(MU_VK_DECLARE)
MU_VK_INSTANCE(MU_VK_DECLARE)
MU_VK_DEVICE(MU_VK_DECLARE)
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

bool load_loader() {
  static const bool loaded = [] {
#ifdef _WIN32
    HMODULE library = LoadLibraryW(L"vulkan-1.dll");
    if (!library) return false;
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)(void*)GetProcAddress(library, "vkGetInstanceProcAddr");
#else
#ifdef __APPLE__
    // The Vulkan loader the app bundles in Frameworks (it finds MoltenVK beside it), else MoltenVK on
    // its own, which is a whole Vulkan implementation without a loader.
    void* library = dlopen("libvulkan.1.dylib", RTLD_NOW | RTLD_LOCAL);
    if (!library) library = dlopen("libMoltenVK.dylib", RTLD_NOW | RTLD_LOCAL);
#else
    void* library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library) library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
    if (!library) return false;
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
#endif
    if (!vkGetInstanceProcAddr) return false;
#define MU_VK_LOAD_GLOBAL(name) name = (PFN_##name)vkGetInstanceProcAddr(nullptr, #name);
    MU_VK_GLOBAL(MU_VK_LOAD_GLOBAL)
    return vkCreateInstance && vkEnumerateInstanceExtensionProperties;
  }();
  return loaded;
}
void load_instance(VkInstance instance) {
#define MU_VK_LOAD_INSTANCE(name) name = (PFN_##name)vkGetInstanceProcAddr(instance, #name);
  MU_VK_INSTANCE(MU_VK_LOAD_INSTANCE)
}
void load_device(VkDevice device) {
#define MU_VK_LOAD_DEVICE(name) name = (PFN_##name)vkGetDeviceProcAddr(device, #name);
  MU_VK_DEVICE(MU_VK_LOAD_DEVICE)
}

void check(VkResult r, const char* what) {
  if (r != VK_SUCCESS) throw std::runtime_error(std::string("vulkan: ") + what + " failed (" + std::to_string((int)r) + ")");
}

// ---- Shaders: HLSL to SPIR-V ----------------------------------------------------------------------

// Bindings shared by every GX program: b0 VSBlock and b1 PSBlock (dynamic uniform buffers), the
// eight textures as one array at 2 and the eight samplers at 3 (register shifts below).
constexpr uint32_t kBindingVS = 0, kBindingPS = 1, kBindingTex = 2, kBindingSamp = 3;

// The Vulkan namespace under the shader cache folder (--shader-cache, "shadercache" by default, as the
// Direct3D renderers use it). It was always "shadercache" beside the working folder, which the Mac app
// (it passes its own folder, outside the read-only bundle) and the tests could not move.
std::mutex g_cache_mutex;
std::filesystem::path g_cache_root = "shadercache";
std::filesystem::path shader_cache_dir() {
  std::lock_guard<std::mutex> lock(g_cache_mutex);   // the render thread and the pipeline worker both ask
  static std::filesystem::path made;
  const std::filesystem::path p = g_cache_root / "vulkan-v1";
  if (p != made) { std::error_code ec; std::filesystem::create_directories(p, ec); made = p; }
  return p;
}

std::vector<uint32_t> read_spirv(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::string bytes((std::istreambuf_iterator<char>(f)), {});
  if (bytes.size() < 20 || bytes.size() % 4) return {};
  std::vector<uint32_t> words(bytes.size() / 4);
  std::memcpy(words.data(), bytes.data(), bytes.size());
  return words[0] == 0x07230203u ? words : std::vector<uint32_t>{};
}

#ifdef _WIN32
struct Dxc {
  Microsoft::WRL::ComPtr<IDxcUtils> utils;
  Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
};
Dxc* dxc() {
  static Dxc* instance = [] () -> Dxc* {
    HMODULE module = LoadLibraryW(L"dxcompiler.dll");
    if (!module) { host::log("vulkan: dxcompiler.dll is missing beside the game"); return nullptr; }
    auto create = (DxcCreateInstanceProc)(void*)GetProcAddress(module, "DxcCreateInstance");
    auto* d = new Dxc;
    if (!create || FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&d->utils))) || FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&d->compiler)))) {
      host::log("vulkan: the shader compiler did not start"); delete d; return nullptr;
    }
    return d;
  }();
  return instance;
}
// present: the shared present pass (present_shader.h): b0 at 0, t0..t3 at 1..4, s0 at 5, and the
// vertex shader's Direct3D clip space flipped for Vulkan.
std::vector<uint32_t> compile_hlsl(const std::string& hlsl, bool vertex, std::string* error, const char* entry = "main", bool present = false) {
  Dxc* d = dxc();
  if (!d) { *error = "no shader compiler"; return {}; }
  DxcBuffer source{hlsl.data(), hlsl.size(), DXC_CP_UTF8};
  const std::wstring wentry(entry, entry + std::strlen(entry));
  std::vector<const wchar_t*> args = {L"-spirv", L"-E", wentry.c_str(), L"-T", vertex ? L"vs_6_0" : L"ps_6_0", L"-fspv-target-env=vulkan1.3",
                                      L"-fvk-b-shift", L"0", L"0", L"-fvk-t-shift", present ? L"1" : L"2", L"0",
                                      L"-fvk-s-shift", present ? L"5" : L"3", L"0", L"-O3", L"-Wno-ignored-attributes"};
  if (present && vertex) args.push_back(L"-fvk-invert-y");
  Microsoft::WRL::ComPtr<IDxcResult> result;
  if (FAILED(d->compiler->Compile(&source, args.data(), (UINT32)args.size(), nullptr, IID_PPV_ARGS(&result)))) { *error = "compile call failed"; return {}; }
  HRESULT status = E_FAIL; result->GetStatus(&status);
  if (FAILED(status)) {
    Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    *error = errors && errors->GetStringLength() ? std::string(errors->GetStringPointer(), errors->GetStringLength()) : "unknown error";
    return {};
  }
  Microsoft::WRL::ComPtr<IDxcBlob> object;
  result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
  if (!object || object->GetBufferSize() % 4) { *error = "no SPIR-V output"; return {}; }
  std::vector<uint32_t> words(object->GetBufferSize() / 4);
  std::memcpy(words.data(), object->GetBufferPointer(), object->GetBufferSize());
  return words;
}
#else
std::vector<uint32_t> compile_hlsl(const std::string& hlsl, bool vertex, std::string* error, const char* entry = "main", bool present = false) {
  const auto dir = shader_cache_dir();
  const std::string stem = (dir / ("tmp-" + std::to_string(getpid()))).string();
  const std::string in = stem + ".hlsl", out = stem + ".spv", log = stem + ".log";
  { std::ofstream f(in, std::ios::binary); f << hlsl; }
  // The release ships glslangValidator in bin/ beside the program (as the OpenGL renderer uses it);
  // a system copy is the fallback. Run by name alone, a system without glslang could not start Vulkan.
  static const std::string tool = [] {
    std::error_code ec;
#ifdef __APPLE__
    char path[4096]; uint32_t size = sizeof path;
    const std::filesystem::path exe = _NSGetExecutablePath(path, &size) == 0 ? std::filesystem::path(path) : std::filesystem::path();
    if (exe.empty()) ec = std::make_error_code(std::errc::no_such_file_or_directory);
#else
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
#endif
    std::filesystem::path shipped;
    if (!ec)   // bin/ beside the program (Linux), or the app bundle's Resources/bin (Mac)
      for (const auto& candidate : {exe.parent_path() / "bin" / "glslangValidator", exe.parent_path().parent_path() / "Resources" / "bin" / "glslangValidator"})
        if (std::filesystem::is_regular_file(candidate, ec)) { shipped = candidate; break; }
    return !shipped.empty() ? "'" + shipped.string() + "'" : std::string("glslangValidator");
  }();
  const std::string command = tool + " -D -V -e " + entry + " -S " + (vertex ? "vert" : "frag") +
      " --target-env vulkan1.3 --auto-map-locations --shift-UBO-binding 0 --shift-texture-binding " + (present ? "1" : "2") +
      " --shift-sampler-binding " + (present ? "5" : "3") + (present && vertex ? " --invert-y" : "") + " -o '" +
      out + "' '" + in + "' > '" + log + "' 2>&1";
  const int code = std::system(command.c_str());
  std::vector<uint32_t> words = code == 0 ? read_spirv(out) : std::vector<uint32_t>{};
  if (words.empty()) { std::ifstream f(log); *error = std::string((std::istreambuf_iterator<char>(f)), {}); if (error->empty()) *error = "glslangValidator is not installed"; }
  std::error_code ec; std::filesystem::remove(in, ec); std::filesystem::remove(out, ec); std::filesystem::remove(log, ec);
  return words;
}
#endif

// SPIR-V for one HLSL source, from the disk cache when it was compiled before.
uint64_t spirv_hash(const std::string& hlsl, bool vertex, const char* entry = "main", bool present = false) {
  uint64_t hash = hash_bytes(hlsl.data(), hlsl.size()) ^ (vertex ? 0x5653ull : 0x5053ull);
  return hash ^ (hash_bytes(entry, std::strlen(entry)) * 31 + (present ? 0x7071ull : 0));
}
std::filesystem::path spirv_path(uint64_t hash, bool vertex) {
  char name[40]; std::snprintf(name, sizeof name, "%s-%016llx.spv", vertex ? "vs" : "ps", (unsigned long long)hash);
  return shader_cache_dir() / name;
}
std::vector<uint32_t> spirv_for(const std::string& hlsl, bool vertex, const char* entry = "main", bool present = false) {
  const auto path = spirv_path(spirv_hash(hlsl, vertex, entry, present), vertex);
  std::vector<uint32_t> words = read_spirv(path);
  if (!words.empty()) return words;
  // One compile at a time: the render thread and the pipeline worker both come here, the shader
  // compiler is one instance (Windows) or works through files named after the process (Linux), and
  // two compiles of the same shader would write the same cache file.
  static std::mutex compile_mutex;
  std::lock_guard<std::mutex> lock(compile_mutex);
  words = read_spirv(path);
  if (!words.empty()) return words;
  std::string error;
  words = compile_hlsl(hlsl, vertex, &error, entry, present);
  if (words.empty()) throw std::runtime_error("vulkan: shader compile failed: " + error.substr(0, 600));
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(words.data()), (std::streamsize)(words.size() * 4));
  return words;
}

// The fixed passes: an EFB region into a copy texture, and the frame onto the window.
const char* kBlitShader = R"(
[[vk::push_constant]] struct PC { float4 rect; float4 box; int4 kind; } pc;
[[vk::binding(0)]] Texture2D src;
[[vk::binding(1)]] SamplerState samp;
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
#ifdef VERTEX
V main(uint id : SV_VertexID) {
  float2 p = float2((id << 1) & 2, id & 2);
  V o; o.pos = float4(p * 2 - 1, 0, 1); o.uv = pc.rect.zw + p * pc.rect.xy; return o;
}
#else
float4 main(V i) : SV_Target {
  if (pc.kind.x == 3) {   // the window: the frame region averaged over each output pixel
    int nx = pc.kind.y, ny = pc.kind.z;
    if (nx <= 1 && ny <= 1) return float4(src.SampleLevel(samp, i.uv, 0).rgb, 1);
    float2 foot = pc.box.xy;
    float4 acc = 0;
    for (int y = 0; y < ny; ++y)
      for (int x = 0; x < nx; ++x)
        acc += src.SampleLevel(samp, i.uv + (float2(x, y) + 0.5) / float2(nx, ny) * foot - 0.5 * foot, 0);
    return float4(acc.rgb / (nx * ny), 1);
  }
  float4 c = src.SampleLevel(samp, i.uv, 0);
  if (pc.kind.x == 1) c.a = 1;
  if (pc.kind.x == 2) {
    uint z = min(uint((1 - c.r) * 16777216.0), 0xFFFFFFu);
    c = float4(float3((z >> 16) & 255u, (z >> 8) & 255u, z & 255u) / 255.0, 1);
  }
  return c;
}
#endif
)";
struct BlitConstants { float rect[4]; float box[4]; int32_t kind[4]; };

// ---- Memory -------------------------------------------------------------------------------------

// Images share large blocks (Melee keeps thousands of textures alive, more than a driver's limit of
// separate allocations). A block is handed back when its last image is gone.
struct Allocation { VkDeviceMemory memory = VK_NULL_HANDLE; VkDeviceSize offset = 0; int block = -1; VkDeviceSize size = 0; };

class Heap {
 public:
  void init(VkDevice device, VkPhysicalDevice physical) {
    device_ = device; vkGetPhysicalDeviceMemoryProperties(physical, &props_);
  }
  uint32_t type(uint32_t bits, VkMemoryPropertyFlags want) const {
    for (uint32_t i = 0; i < props_.memoryTypeCount; ++i)
      if ((bits & (1u << i)) && (props_.memoryTypes[i].propertyFlags & want) == want) return i;
    throw std::runtime_error("vulkan: no suitable memory type");
  }
  Allocation allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags want) {
    const uint32_t t = type(req.memoryTypeBits, want);
    constexpr VkDeviceSize kBlock = 64ull << 20;
    if (req.size > kBlock / 2) {
      VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, t};
      Allocation a; check(vkAllocateMemory(device_, &info, nullptr, &a.memory), "dedicated allocation");
      a.size = req.size; held_ += req.size; return a;
    }
    for (size_t i = 0; i < blocks_.size(); ++i) {
      Block& b = blocks_[i];
      if (!b.memory || b.type != t) continue;
      const VkDeviceSize at = (b.used + req.alignment - 1) / req.alignment * req.alignment;
      if (at + req.size > kBlock) continue;
      b.used = at + req.size; ++b.live;
      return {b.memory, at, (int)i};
    }
    Block b; b.type = t;
    VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, kBlock, t};
    check(vkAllocateMemory(device_, &info, nullptr, &b.memory), "memory block");
    held_ += kBlock;
    b.used = req.size; b.live = 1;
    for (size_t i = 0; i < blocks_.size(); ++i) if (!blocks_[i].memory) { blocks_[i] = b; return {b.memory, 0, (int)i}; }
    blocks_.push_back(b);
    return {b.memory, 0, (int)blocks_.size() - 1};
  }
  void release(const Allocation& a) {
    if (a.block < 0) { if (a.memory) { vkFreeMemory(device_, a.memory, nullptr); held_ -= std::min(held_, a.size); } return; }
    Block& b = blocks_[a.block];
    if (--b.live == 0) { vkFreeMemory(device_, b.memory, nullptr); b = Block{}; held_ -= std::min<VkDeviceSize>(held_, 64ull << 20); }
  }
  void destroy() { for (auto& b : blocks_) if (b.memory) vkFreeMemory(device_, b.memory, nullptr); blocks_.clear(); }
  // Device memory this heap holds: whole blocks plus dedicated allocations.
  VkDeviceSize bytes() const { return held_; }
 private:
  struct Block { VkDeviceMemory memory = VK_NULL_HANDLE; VkDeviceSize used = 0; uint32_t type = 0; int live = 0; };
  VkDevice device_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties props_{};
  std::vector<Block> blocks_;
  VkDeviceSize held_ = 0;
};

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  Allocation memory;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  uint32_t width = 0, height = 0, levels = 1;
  uint64_t used = 0, bytes = 0;
  uint64_t charged = 0;   // a pack replacement's share of the replacement budget, given back when it goes
};

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  uint8_t* mapped = nullptr;
  VkDeviceSize size = 0, used = 0;
};

// ---- Pipelines ----------------------------------------------------------------------------------

struct PipelineKey {
  uint64_t vs = 0, ps = 0;
  uint32_t state = 0;   // blend, depth, cull, topology, fill (see pipeline_state)
  bool operator==(const PipelineKey& o) const { return vs == o.vs && ps == o.ps && state == o.state; }
};
struct PipelineKeyHash { size_t operator()(const PipelineKey& k) const { return size_t(k.vs ^ (k.ps * 31) ^ (uint64_t(k.state) << 7)); } };

// Set while a Vulkan renderer runs: the settings panel then needs no probe instance.
bool g_vulkan_running = false;

// Frames in flight. Two, as the Direct3D 12 renderer (its FRAME_SLOTS): with three the CPU could run
// two whole GPU frames ahead, and when the GPU was the bottleneck those two frames sat in the queue
// as input delay for the whole session. The slot fence only ever waits when the GPU is already more
// than a frame behind. The swapchain keeps three images so acquiring one never waits on the display
// (the present wait below holds the queue instead, after the frame is recorded and submitted).
constexpr int kFrames = 2;
// Pipeline state bit: the draw also writes motion vectors and the HUD mask (attachments 1 and 2),
// for the upscaler. Bits 19 to 22 hold the sample count.
constexpr uint32_t kStateUpscaleOutputs = 1u << 26;
inline VkSampleCountFlagBits state_samples(uint32_t state) { return VkSampleCountFlagBits((state >> 19) & 0xF); }
constexpr VkDeviceSize kRingBytes = 96ull << 20;   // vertices, indices, constants and uploads of one frame

class VulkanBackend final : public Backend {
 public:
  VulkanBackend(const VulkanWindow& window, int client_w, int client_h, const RenderOptions& options)
      : window_(window), opts_(options), client_w_(std::max(client_w, 1)), client_h_(std::max(client_h, 1)) {
    create_instance();
    create_device();
    heap_.init(device_, physical_);
    {
      // Pack replacements get a quarter of the graphics card's own memory (at least 256 MB, at most
      // 2 GB), as on Direct3D 12, instead of a fixed 512 MB.
      VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(physical_, &mp);
      VkDeviceSize local = 0;
      for (uint32_t i = 0; i < mp.memoryHeapCount; ++i)
        if (mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) local = std::max(local, mp.memoryHeaps[i].size);
      replacement_budget_ = std::clamp<uint64_t>(local / 4, 256ull << 20, 2048ull << 20);
    }
    create_layouts();
    create_frames();
    apply_fullscreen_mode();   // the window over the whole monitor first when fullscreen or exclusive is saved
    create_swapchain();
#ifdef _WIN32
    track_vblank();
#endif
    scale_ = pick_scale();
    create_efb();
    prewarm_pipelines();
    blit_vs_ = module(spirv_for(std::string("#define VERTEX 1\n") + kBlitShader, true));
    blit_ps_ = module(spirv_for(kBlitShader, false));
    copy_pipeline_ = blit_pipeline(VK_FORMAT_R8G8B8A8_UNORM);
    present_pipeline_ = blit_pipeline(swap_format_);
    create_present_pass();
    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.magFilter = s.minFilter = VK_FILTER_LINEAR; s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.maxLod = 0;
    check(vkCreateSampler(device_, &s, nullptr, &linear_), "sampler");
    s.magFilter = s.minFilter = VK_FILTER_NEAREST;
    check(vkCreateSampler(device_, &s, nullptr, &nearest_), "sampler");
    create_white();
    g_vulkan_running = true;
    { std::error_code ec; std::filesystem::remove(shader_cache_dir() / "startup.pending", ec); }
#ifdef GX_PC_SETTINGS
    if (opts_.pc_settings) create_settings_ui();
#endif
    host::log("vulkan: %s, internal resolution EFB x%d (window %dx%d), %s", device_name_.c_str(), scale_, client_w_, client_h_,
              present_mode_ == VK_PRESENT_MODE_FIFO_KHR ? "vsync" : present_mode_ == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "immediate");
    start_pipeline_worker();   // last: a constructor that throws after this would leave the thread joinable
  }

  ~VulkanBackend() override {
    if (!device_) return;
    stop_pipeline_worker();   // before anything it uses goes; what it finished joins the maps emptied below
#ifdef _WIN32
    fg_presenter_wait(); fg_presenter_stop();
#endif
    vkDeviceWaitIdle(device_);
#ifdef _WIN32
    vblank_.stop();
    if (present_timer_) { CloseHandle(present_timer_); present_timer_ = nullptr; }
#endif
    // Quitting: keep the caches and leave the device to Windows. NVIDIA's driver crashed inside
    // vkDestroyDevice as the game closed (RTX 3060, 0.9.96 reports), after everything was saved.
    if (host::process_ending()) {
      save_pipeline_recipes();
      if (pipeline_cache_) save_pipeline_cache();
      return;
    }
#ifdef GX_PC_SETTINGS
    destroy_settings_ui();
#endif
    for (auto& p : pipelines_) vkDestroyPipeline(device_, p.second, nullptr);
    for (auto& m : modules_) vkDestroyShaderModule(device_, m.second, nullptr);
    for (auto& t : textures_) destroy(t.second);
    for (auto& c : copies_) destroy(c.second);
    for (auto& f : frames_) { for (auto& img : f.dead) destroy(img); f.dead.clear(); }
    destroy(efb_color_); destroy(efb_depth_); destroy(msaa_color_); destroy(msaa_depth_); destroy(white_);
    destroy(mvec_img_); destroy(hud_img_); destroy(upscale_out_);
#ifdef _WIN32
    destroy(fg_out_); destroy(fg_real_);
    for (VkFence fence : fg_fences_) if (fence) vkDestroyFence(device_, fence, nullptr);
    for (VkSemaphore sem : fg_acquired_) if (sem) vkDestroySemaphore(device_, sem, nullptr);
    if (fg_pool_) vkDestroyCommandPool(device_, fg_pool_, nullptr);
    fsr::vk_shutdown();
#endif
    for (auto& v : video_images_) destroy(v);
    if (efb_blit_) vkDestroyPipeline(device_, efb_blit_, nullptr);
    for (auto& s : samplers_) vkDestroySampler(device_, s.second, nullptr);
    vkDestroySampler(device_, linear_, nullptr); vkDestroySampler(device_, nearest_, nullptr);
    vkDestroyPipeline(device_, copy_pipeline_, nullptr); vkDestroyPipeline(device_, present_pipeline_, nullptr);
    if (present_plain_) vkDestroyPipeline(device_, present_plain_, nullptr);
    if (present_plain_rgba_) vkDestroyPipeline(device_, present_plain_rgba_, nullptr);
    destroy(shader_source_);
    user_shader::vk_release();   // the GPU is idle: the preset's resources go before the device
    if (present_crt_) vkDestroyPipeline(device_, present_crt_, nullptr);
    for (VkShaderModule m : {present_vs_, present_ps_, present_crt_ps_}) if (m) vkDestroyShaderModule(device_, m, nullptr);
    if (present_layout_) vkDestroyPipelineLayout(device_, present_layout_, nullptr);
    if (present_set_layout_) vkDestroyDescriptorSetLayout(device_, present_set_layout_, nullptr);
    vkDestroyShaderModule(device_, blit_vs_, nullptr); vkDestroyShaderModule(device_, blit_ps_, nullptr);
    release_exclusive();
    destroy_swapchain();
    if (capture_buffer_.buffer) { vkDestroyBuffer(device_, capture_buffer_.buffer, nullptr); vkFreeMemory(device_, capture_buffer_.memory, nullptr); }
    for (auto& f : frames_) {
      vkDestroyFence(device_, f.fence, nullptr);
      vkDestroySemaphore(device_, f.acquired, nullptr);
      vkDestroyDescriptorPool(device_, f.pool, nullptr);
      if (f.timers) vkDestroyQueryPool(device_, f.timers, nullptr);
      vkDestroyCommandPool(device_, f.commands, nullptr);
      vkDestroyBuffer(device_, f.ring.buffer, nullptr); vkFreeMemory(device_, f.ring.memory, nullptr);
    }
    vkDestroyPipelineLayout(device_, layout_, nullptr); vkDestroyPipelineLayout(device_, blit_layout_, nullptr);
    vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr); vkDestroyDescriptorSetLayout(device_, blit_set_layout_, nullptr);
    save_pipeline_recipes();
    if (pipeline_cache_) { save_pipeline_cache(); vkDestroyPipelineCache(device_, pipeline_cache_, nullptr); }
    heap_.destroy();
    vkDestroyDevice(device_, nullptr);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (messenger_ && vkDestroyDebugUtilsMessengerEXT) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    vkDestroyInstance(instance_, nullptr);
  }

  const RenderOptions* presentation_options() const override { return &opts_; }
  void set_skip_present(bool skip) override { skip_present_ = skip; }
  void resize(int w, int h) override { if (w > 0 && h > 0 && (w != client_w_ || h != client_h_)) { client_w_ = w; client_h_ = h; swapchain_dirty_ = true; } }
  void presentation_stats(uint32_t* frames, uint32_t* pipes, uint32_t* textures) const override {
    if (frames) *frames = presented_;
    if (pipes) *pipes = (uint32_t)pipelines_.size();
    if (textures) *textures = (uint32_t)(textures_.size() + copies_.size());
  }
  // The pacing contract the Direct3D renderers keep with the presentation loop (threaded_backend.cpp):
  // the deadline for this present, how long the frame waited for the display, and the display's own
  // verdict (follows the frames, or refreshes at a fixed rate) from counts it cannot fake.
  void set_present_deadline(double deadline) override { present_deadline_ = deadline; }
  double presentation_wait_seconds() const override { return present_wait_; }
  std::string profile_line() const override {
    char line[320];
    const double f = (double)std::max<uint64_t>(1, stat_frames_);
    std::snprintf(line, sizeof line, "vulkan per presented frame: acquire %.3f, present wait %.3f, deadline %.3f, present %.3f ms | %zu pipelines, %zu shader modules, %s",
                  1e3 * stat_acquire_ / f, 1e3 * stat_wait_ / f, 1e3 * stat_spin_ / f, 1e3 * stat_present_ / f, pipelines_.size(), modules_.size(),
                  present_mode_ == VK_PRESENT_MODE_FIFO_KHR ? "synced" : present_mode_ == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate" : "mailbox");
    stat_acquire_ = stat_wait_ = stat_spin_ = stat_present_ = 0; stat_frames_ = 0;
    return line;
  }
  std::string display_timing_line() override {
    char line[256];
    const char* mode = fullscreen_state_.exclusive_active ? "exclusive fullscreen" : "windowed or borderless";
#ifdef _WIN32
    if (!vblank_.healthy()) {
      timing_shown_ = presented_; timing_blanks_ = vblank_.wakes();
      std::snprintf(line, sizeof line, "%s, no vertical blank readings from this output", mode);
      return line;
    }
    const uint64_t shown = presented_ - timing_shown_, blanks = vblank_.wakes() - timing_blanks_;
    timing_shown_ = presented_; timing_blanks_ = vblank_.wakes();
    std::snprintf(line, sizeof line, "%s, %s: %llu frames presented over %llu vertical blanks (%llu blanks repeated a frame)", mode,
                  present_wait_active() ? "synced, one frame in line for the display" : present_synced_ ? "synced, the driver's own present queue" : "not synced",
                  (unsigned long long)shown, (unsigned long long)blanks, (unsigned long long)(blanks > shown ? blanks - shown : 0));
#else
    if (!display_timing_on_) { std::snprintf(line, sizeof line, "%s, no display timing from this driver", mode); return line; }
    std::snprintf(line, sizeof line, "%s, %s: %u frames presented, %u of %u present intervals on the refresh grid", mode,
                  present_wait_active() ? "synced, one frame in line for the display" : present_synced_ ? "synced, the driver's own present queue" : "not synced",
                  presented_ - timing_shown_, timing_line_on_grid_, timing_line_intervals_);
    timing_shown_ = presented_; timing_line_on_grid_ = timing_line_intervals_ = 0;
#endif
    return line;
  }
  int variable_refresh() const override {
#ifdef _WIN32
    return vrr_.state();
#else
    return timing_state_;
#endif
  }
  bool display_counts_readable() const override { return vrr_readable_; }
  int frame_generation_multiplier() const override { return fg_multiplier_.load(std::memory_order_relaxed); }
  void set_display_refresh(double hz, bool tear_free) override { vrr_nominal_hz_ = hz; tear_free_ = tear_free; }
  bool vblank_next(double after, double nominal_hz, double* when, double* period, std::string* why) override {
#ifdef _WIN32
    if (!vblank_tracked_) track_vblank();
    return vblank_.next_after(after, nominal_hz, when, period, why);
#else
    (void)after; (void)nominal_hz; (void)when; (void)period;
    *why = "no vertical blank clock on this renderer";
    return false;
#endif
  }
  void submit_frame(const Frame& frame) override { submit_frame(frame, nullptr); }
  void submit_frame(const Frame& frame, const DrawMatrices* overrides) override {
    begin_frame(frame);
    exact_frame_ = !frame_in_match(frame) &&
                   std::any_of(frame.copies.begin(), frame.copies.end(), [](const EfbCopy& c) { return !c.to_xfb; });
    const size_t original = frame.vertices.size();
    vertex_offsets_.assign(frame.draws.size(), 0);
    size_t total = original;
    if (overrides) for (size_t i = 0; i < frame.draws.size(); ++i)
      if (overrides[i].vertices) { vertex_offsets_[i] = total; total += frame.draws[i].vertex_count; }
    // Every vertex of the frame in one piece of the ring, the override poses after them.
    vertex_base_ = 0;
    if (total) {
      const VkDeviceSize at = ring_alloc(total * sizeof(Vertex), 16);
      vertex_base_ = at;
      Frame_& f = frames_[slot_];
      if (original) std::memcpy(f.ring.mapped + at, frame.vertices.data(), original * sizeof(Vertex));
      if (overrides) for (size_t i = 0; i < frame.draws.size(); ++i)
        if (overrides[i].vertices) std::memcpy(f.ring.mapped + at + vertex_offsets_[i] * sizeof(Vertex), overrides[i].vertices, frame.draws[i].vertex_count * sizeof(Vertex));
    }
    set_main_projection(frame_scene_draw(frame));
    // Menu video backgrounds and the character select's True widescreen bands, decided as the
    // Direct3D renderers do (this read major 1 minor 0, the main menu, so the CSS bands never stretched).
    video_bg::set_enabled(opts_.video_backgrounds);
    video_bg::begin_frame(frame.scene_major, frame.scene_minor);
    set_css_scene(video_bg::learning::scene_slot(frame.scene_major, frame.scene_minor) == 0);
    bool presented = false;
    // Menu video backgrounds (video_background.h): on the character select the decoded frame goes in
    // between Melee's hardcoded animated backdrop (its leading untextured draws) and the textured
    // UI; on the stage select it is composited right after the learned backdrop draw.
    const EfbCopy* screen = nullptr;
    for (const EfbCopy& c : frame.copies) if (c.to_xfb) screen = &c;
    int fullscreen_slot = -1;
    const std::shared_ptr<const video_bg::Frame> fullscreen = video_bg::fullscreen_frame(&fullscreen_slot);
    const bool css_fullscreen = fullscreen && fullscreen_slot == 0 && screen;
    unsigned leading_untextured = 0;
    bool background_drawn = false;
    try {
      for (const auto& command : frame.commands) {
        if (command.kind == FrameCommand::Draw) {
          const DrawCall& dc = frame.draws.at(command.index);
          if (css_fullscreen && !background_drawn) {
            bool textured = false;
            for (const TextureRef& texture : dc.textures) textured |= texture.used;
            if (!textured) ++leading_untextured;
            else if (video_bg::learning::should_insert_fullscreen_layer(fullscreen_slot, leading_untextured, textured)) {
              draw_video_background(fullscreen, fullscreen_slot, *screen);
              background_drawn = true;
              if (!video_layer_logged_[0]) {
                host::log("video backgrounds: CSS full-screen layer inserted after %u hardcoded background draws (Vulkan)", leading_untextured);
                video_layer_logged_[0] = true;
              }
            }
          }
          draw_video_slot_ = -1;
          draw(frame, dc, overrides ? overrides + command.index : nullptr, vertex_offsets_[command.index]);
          if (fullscreen && screen && fullscreen_slot == 1 && draw_video_slot_ == 1 && !background_drawn) {
            draw_video_background(fullscreen, fullscreen_slot, *screen);
            background_drawn = true;
            if (!video_layer_logged_[1]) {
              host::log("video backgrounds: SSS full-screen layer composited after learned backdrop draw (Vulkan)");
              video_layer_logged_[1] = true;
            }
          }
        } else {
          const auto& c = frame.copies.at(command.index);
          if (c.to_xfb) { if (!skip_present_ && !presented) { present(c, frame.test_fill); presented = true; } }
          else copy(c);
          if (c.clear) clear(c);
        }
      }
    } catch (const std::runtime_error& e) {
      // The per-frame buffer ran out (a small one on a driver that could not map the full size):
      // the rest of this frame is not drawn; what was recorded is still submitted.
      if (++ring_overflows_ <= 3) host::log("%s; the rest of this frame was skipped", e.what());
    }
    end_frame(presented);
    trim();
  }

 private:
  // ---- Pacing, the display, fullscreen ----
  bool synced_wanted() const { return opts_.vsync || tear_free_; }

  // Blocks until the previous present reached the display (VK_KHR_present_wait), so at most one frame
  // is ever in line for it, as the Direct3D 12 frame latency waitable object with a latency of one.
  // Called after this frame is recorded and submitted, never before: waiting before recording made
  // every frame take CPU plus GPU time under a heavy GPU load. A wait that fails or keeps timing out
  // gives up for the session rather than slowing the game to the timeout, and says so once.
  // Synced presents (FIFO) only: only there do frames queue for a vertical blank, which is the delay
  // this wait removes. Immediate and mailbox presents never wait for a blank, and the two frame
  // slots already hold the GPU to a frame behind; there NVIDIA's driver also never completed the
  // wait for a hidden window (three 100 ms timeouts, 2026-11-04 hidden runs, V-Sync off).
  // Off unless MELEE_VULKAN_PRESENT_WAIT=1 (A/B measurement only): at a locked 60 Hz the wait took 8 to
  // 15 ms of every frame and frames missed their slot (31 to 56 fps where the shipped renderer held
  // 57 to 60, 2026-11-05), and its latency gain was never measured. Two frames in flight stay.
  static bool present_wait_wanted() { static const bool on = [] { const char* v = std::getenv("MELEE_VULKAN_PRESENT_WAIT"); return v && *v == '1'; }(); return on; }
  bool present_wait_active() const { return present_wait_on_ && present_wait_wanted() && present_mode_ == VK_PRESENT_MODE_FIFO_KHR; }
  void wait_previous_present() {
    if (!present_wait_active() || !last_present_id_) return;
    host::perf::Scope wait_cost(host::perf::Stage::Wait);
    const VkResult r = vkWaitForPresentKHR(device_, swapchain_, last_present_id_, 100ull * 1000 * 1000);   // 100 ms, as the Direct3D 12 wait
    if (r == VK_SUCCESS) { present_wait_timeouts_ = 0; return; }
    if (r == VK_SUBOPTIMAL_KHR) { swapchain_dirty_ = true; present_wait_timeouts_ = 0; return; }
    if (r == VK_ERROR_OUT_OF_DATE_KHR || exclusive_lost(r)) { swapchain_dirty_ = true; return; }
    if (r == VK_TIMEOUT) {
      if (++present_wait_timeouts_ < 3) return;
      host::log("vulkan: the present wait timed out three frames in a row; presenting with the driver's own queue from now on");
    } else {
      host::log("vulkan: the present wait failed (%d); presenting with the driver's own queue from now on", (int)r);
    }
    present_wait_on_ = false;
  }

  // The presentation loop's deadline for this present (set_present_deadline): a high resolution
  // timer until shortly before it, then a spin, as the Direct3D 12 renderer does. 0 = no deadline.
  void spin_to_deadline() {
    if (present_deadline_ <= 0) return;
    host::perf::Scope wait_cost(host::perf::Stage::Wait);
    for (;;) {
      const double remaining = present_deadline_ - host::now_seconds();
      if (remaining <= 0) break;
#ifdef _WIN32
      if (present_timer_ && remaining > 0.0004) {
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((remaining - 0.0002) * 1e7);
        if (SetWaitableTimer(present_timer_, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(present_timer_, INFINITE);
        else SwitchToThread();
      } else YieldProcessor();
#else
      if (remaining > 0.0004) std::this_thread::sleep_for(std::chrono::duration<double>(remaining - 0.0002));
      else std::this_thread::yield();
#endif
    }
  }

  // The window over the whole monitor when fullscreen or exclusive fullscreen is wanted, as the
  // Direct3D renderers (apply_fullscreen_mode there); exclusive itself is the swapchain's (Windows).
  void apply_fullscreen_mode() {
#ifdef _WIN32
    fg_presenter_wait();
#endif
    const bool want_window = opts_.fullscreen || opts_.exclusive_fullscreen;
    if (want_window != host::window_is_fullscreen()) host::window_set_fullscreen(want_window);
#ifdef _WIN32
    if (opts_.exclusive_fullscreen && !fse_supported_ && !fse_logged_) {
      fse_logged_ = true;
      host::log("vulkan: exclusive fullscreen is not offered by this driver; borderless fullscreen instead");
    }
    const bool want_exclusive = opts_.exclusive_fullscreen && fse_supported_ && !fse_failed_;
    if (want_exclusive != want_exclusive_) { want_exclusive_ = want_exclusive; swapchain_dirty_ = true; }   // made again with or without the chain
#endif
  }
#ifdef _WIN32
  void acquire_exclusive() {
    if (!vkAcquireFullScreenExclusiveModeEXT || !swapchain_ || fullscreen_state_.exclusive_active) return;
    VkResult r = VK_ERROR_INITIALIZATION_FAILED;
    fullscreen_state_.enter_exclusive([&] { r = vkAcquireFullScreenExclusiveModeEXT(device_, swapchain_); return r == VK_SUCCESS; });
    if (r == VK_SUCCESS) { host::log("vulkan: exclusive fullscreen"); return; }
    host::log("vulkan: exclusive fullscreen unavailable (%d); using borderless fullscreen", (int)r);
    fse_failed_ = true; want_exclusive_ = false; swapchain_dirty_ = true;   // made again without the chain
  }
  void release_exclusive() {
    if (!fullscreen_state_.exclusive_active || !vkReleaseFullScreenExclusiveModeEXT || !swapchain_) return;
    vkReleaseFullScreenExclusiveModeEXT(device_, swapchain_);
    fullscreen_state_.exclusive_active = false;
  }
  bool exclusive_lost(VkResult r) {
    if (r != VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT) return false;
    fullscreen_state_.exclusive_active = false;
    host::log("vulkan: exclusive fullscreen was taken away (another window came up); it is entered again");
    return true;
  }
  void track_vblank() {
    vblank_.track(MonitorFromWindow((HWND)window_.window, MONITOR_DEFAULTTONEAREST));
    vblank_tracked_ = true;
  }
  // See Backend::variable_refresh and vrr_detect.h. Called every 32 presents; a window is a second.
  // The refreshes come from the output's own vertical blanks (vblank_clock.h) since Vulkan has no
  // DXGI frame statistics: a display that follows the frames blanks once per presented frame under
  // its refresh, a fixed one at its full rate whatever the frame rate.
  // No verdict on Windows: the output's vertical blank wait keeps ticking at the nominal rate under
  // G-Sync too (his 200 Hz G-Sync screen, 2026-11-05: Direct3D 12's frame statistics said variable
  // refresh, the blank count said "fixed, 201 a second"), so it cannot tell the two apart. Unknown
  // is the honest answer, as before; the blank clock still serves fixed-refresh pacing (vblank_next).
  void update_variable_refresh() {
    vrr_readable_ = false;
    vrr_.restart();
    if (!vrr_unknown_logged_) {
      vrr_unknown_logged_ = true;
      host::log("display: Vulkan cannot tell variable refresh (G-Sync, FreeSync) from a fixed refresh on Windows; treated as unknown");
    }
  }
  bool vrr_unknown_logged_ = false;
  void timing_reset() {}
#else
  void release_exclusive() {}
  bool exclusive_lost(VkResult) { return false; }
  void timing_reset() { last_actual_present_ = 0; timing_intervals_ = timing_on_grid_ = 0; timing_window_start_ = 0; }
  // Linux: VK_GOOGLE_display_timing says when each frame really reached the screen. On a display
  // that refreshes at a fixed rate those times sit on its refresh grid (intervals are whole
  // refreshes); on one that follows the frames they follow the frame times. Verdict after two
  // one-second windows agree, as vrr::Detector does. Not run on real Linux hardware yet.
  void update_variable_refresh() {
    if (!display_timing_on_ || !vkGetPastPresentationTimingGOOGLE || !vkGetRefreshCycleDurationGOOGLE || !swapchain_) { vrr_readable_ = false; return; }
    uint32_t n = 0;
    if (vkGetPastPresentationTimingGOOGLE(device_, swapchain_, &n, nullptr) != VK_SUCCESS) { vrr_readable_ = false; return; }
    std::vector<VkPastPresentationTimingGOOGLE> t(n);
    if (n) vkGetPastPresentationTimingGOOGLE(device_, swapchain_, &n, t.data());
    VkRefreshCycleDurationGOOGLE cycle{};
    if (vkGetRefreshCycleDurationGOOGLE(device_, swapchain_, &cycle) != VK_SUCCESS || !cycle.refreshDuration) { vrr_readable_ = false; return; }
    vrr_readable_ = true;
    for (uint32_t i = 0; i < n; ++i) {
      if (last_actual_present_ && t[i].actualPresentTime > last_actual_present_) {
        const double k = double(t[i].actualPresentTime - last_actual_present_) / double(cycle.refreshDuration);
        ++timing_intervals_; ++timing_line_intervals_;
        if (std::fabs(k - std::round(k)) < 0.05) { ++timing_on_grid_; ++timing_line_on_grid_; }
      }
      last_actual_present_ = std::max(last_actual_present_, t[i].actualPresentTime);
    }
    const double now = host::now_seconds();
    if (timing_window_start_ <= 0) { timing_window_start_ = now; timing_window_presents_ = presented_; return; }
    if (now - timing_window_start_ < 1.0) return;
    const double shown = double(presented_ - timing_window_presents_) / (now - timing_window_start_);
    timing_window_start_ = now; timing_window_presents_ = presented_;
    int verdict = -1;
    if (timing_intervals_ >= 30 && vrr_nominal_hz_ >= 75.0 && shown >= 30.0 && shown <= 0.96 * vrr_nominal_hz_) {
      const double on_grid = double(timing_on_grid_) / timing_intervals_;
      verdict = on_grid > 0.9 ? 0 : on_grid < 0.5 ? 1 : -1;
    }
    timing_intervals_ = timing_on_grid_ = 0;
    if (verdict < 0) { timing_pending_ = -1; timing_votes_ = 0; return; }
    if (verdict != timing_pending_) { timing_pending_ = verdict; timing_votes_ = 1; return; }
    if (++timing_votes_ < 2 || verdict == timing_state_) return;
    timing_state_ = verdict;
    host::log(verdict == 1 ? "display: variable refresh (present times follow the frames) on a %.0f Hz display; frames are presented synced under %.0f a second"
                           : "display: fixed refresh (present times sit on the refresh grid) on a %.0f Hz display", vrr_nominal_hz_, vrr::under_refresh(vrr_nominal_hz_));
  }
#endif

  // ---- GPU timers and video memory ----
  // This slot's previous frame is finished (its fence passed), so its timestamps are ready: read
  // without waiting. A frame that did not write a pair contributes nothing.
  void read_gpu_timers(int slot) {
    Frame_& f = frames_[slot];
    if (!f.timers || !(f.timer_mask & 1)) return;
    uint64_t ts[kTimers]{};
    if (vkGetQueryPoolResults(device_, f.timers, 0, kTimers, sizeof ts, ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
    const uint64_t mask = timestamp_bits_ >= 64 ? ~0ull : ((1ull << timestamp_bits_) - 1);
    auto ms = [&](int a, int b) { const uint64_t d = (ts[b] - ts[a]) & mask; return (double)d * limits_.timestampPeriod * 1e-6; };
    if ((f.timer_mask & 9) == 9) { const double v = ms(0, 3); if (v >= 0 && v < 1000) { gpu_frame_ms_ += v; ++gpu_frame_samples_; } }
    if ((f.timer_mask & 6) == 6) { const double v = ms(1, 2); if (v >= 0 && v < 1000) { gpu_present_ms_ += v; ++gpu_present_samples_; } }
  }
  // VRAM meter (the settings panel's "Video memory in use"): VK_EXT_memory_budget's usage of the
  // device-local heaps, against the largest of them (the card's own memory on a discrete GPU).
  void update_vram() {
    if (!vkGetPhysicalDeviceMemoryProperties2) return;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
    if (memory_budget_on_) props.pNext = &budget;
    vkGetPhysicalDeviceMemoryProperties2(physical_, &props);
    VkDeviceSize used = 0, total = 0;
    for (uint32_t i = 0; i < props.memoryProperties.memoryHeapCount; ++i) {
      if (!(props.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) continue;
      total = std::max(total, props.memoryProperties.memoryHeaps[i].size);
      if (memory_budget_on_) used += budget.heapUsage[i];
    }
    if (!memory_budget_on_) used = heap_.bytes();   // only what this renderer allocated
    gpu_readings::vram_total_gb().store((float)(total / 1073741824.0), std::memory_order_relaxed);
    gpu_readings::vram_used_gb().store((float)(used / 1073741824.0), std::memory_order_relaxed);
  }

  // ---- Setup ----
  void create_instance() {
    if (!load_loader()) throw std::runtime_error("vulkan: no Vulkan loader on this system");
    uint32_t version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&version);
    if (version < VK_API_VERSION_1_3) throw std::runtime_error("vulkan: the loader is older than Vulkan 1.3");
    std::vector<const char*> extensions{VK_KHR_SURFACE_EXTENSION_NAME};
#ifdef _WIN32
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
    if (window_.instance_extensions) extensions = window_.instance_extensions();
#endif
    // Optional instance extensions, by what the loader offers: surface capabilities 2 (exclusive
    // fullscreen needs it) and portability enumeration (MoltenVK behind the Vulkan loader on macOS
    // shows its device only with it).
    std::unordered_set<std::string> offered;
    {
      uint32_t n = 0; vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
      std::vector<VkExtensionProperties> props(n); vkEnumerateInstanceExtensionProperties(nullptr, &n, props.data());
      for (uint32_t i = 0; i < n; ++i) offered.insert(props[i].extensionName);
    }
    VkInstanceCreateFlags flags = 0;
    if (offered.count(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)) { extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME); surface_caps2_on_ = true; }
    if (offered.count(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) { extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME); flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR; }
    // MELEE_VULKAN_VALIDATION=1 (tests and reports): Khronos' validation layer, its messages in the
    // log. Only when the layer is installed (the Vulkan SDK); a player never has it on by accident.
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    const bool validation = [] { const char* v = std::getenv("MELEE_VULKAN_VALIDATION"); return v && *v == '1'; }() &&
                            offered.count(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (validation) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Melee Unlocked"; app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.flags = flags;
    info.pApplicationInfo = &app; info.enabledExtensionCount = (uint32_t)extensions.size(); info.ppEnabledExtensionNames = extensions.data();
    if (validation) { info.enabledLayerCount = 1; info.ppEnabledLayerNames = layers; }
    VkResult created = vkCreateInstance(&info, nullptr, &instance_);
    if (created == VK_ERROR_LAYER_NOT_PRESENT && validation) {
      host::log("vulkan: MELEE_VULKAN_VALIDATION=1 but the validation layer is not installed; running without it");
      info.enabledLayerCount = 0; extensions.pop_back(); info.enabledExtensionCount = (uint32_t)extensions.size();
      created = vkCreateInstance(&info, nullptr, &instance_);
    } else if (validation) validation_on_ = true;
    check(created, "instance");
    load_instance(instance_);
    if (validation_on_ && vkCreateDebugUtilsMessengerEXT) {
      VkDebugUtilsMessengerCreateInfoEXT m{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
      m.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      m.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      m.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) -> VkBool32 {
        // Each distinct message once (by its id), so a per-draw complaint does not flood the log.
        static std::mutex mutex; static std::unordered_set<int32_t> seen;
        std::lock_guard<std::mutex> lock(mutex);
        if (seen.insert(data->messageIdNumber).second && seen.size() <= 200)
          host::log("vulkan validation %s: %s", severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "ERROR" : "warning",
                    data->pMessage ? std::string(data->pMessage).substr(0, 700).c_str() : "");
        return VK_FALSE;
      };
      if (vkCreateDebugUtilsMessengerEXT(instance_, &m, nullptr, &messenger_) == VK_SUCCESS) host::log("vulkan: validation layer on");
    }
#ifdef _WIN32
    VkWin32SurfaceCreateInfoKHR s{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    s.hinstance = window_.display ? (HINSTANCE)window_.display : GetModuleHandleW(nullptr); s.hwnd = (HWND)window_.window;
    check(vkCreateWin32SurfaceKHR(instance_, &s, nullptr, &surface_), "window surface");
#else
    if (!window_.create_surface || !window_.create_surface(instance_, &surface_)) throw std::runtime_error("vulkan: the window surface could not be created");
#endif
  }

  void create_device() {
    uint32_t count = 0; vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count); vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    int best = -1;
    for (VkPhysicalDevice d : devices) {
      VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(d, &p);
      if (p.apiVersion < VK_API_VERSION_1_3) continue;
      uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
      std::vector<VkQueueFamilyProperties> qs(qn); vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qs.data());
      for (uint32_t q = 0; q < qn; ++q) {
        VkBool32 present = VK_FALSE; vkGetPhysicalDeviceSurfaceSupportKHR(d, q, surface_, &present);
        if (!(qs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
        const int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        if (score > best) { best = score; physical_ = d; queue_family_ = q; device_name_ = p.deviceName; limits_ = p.limits; timestamp_bits_ = qs[q].timestampValidBits; }
        break;
      }
    }
    if (!physical_) throw std::runtime_error("vulkan: no Vulkan 1.3 GPU that can draw to this window");
    VkPhysicalDeviceFeatures available{}; vkGetPhysicalDeviceFeatures(physical_, &available);
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = available.samplerAnisotropy; features.fillModeNonSolid = available.fillModeNonSolid;
    anisotropy_supported_ = available.samplerAnisotropy; wire_supported_ = available.fillModeNonSolid;
    // Reads past the end of a buffer return zeros instead of faulting: on AMD's Linux driver such a
    // read lost the device ("GPUVM fault", then "submit failed") as a Training stage loaded.
    features.robustBufferAccess = available.robustBufferAccess;
    // What AMD FSR's compute shaders use (gx_fsr_vk.cpp), where the GPU has it: typed storage-image
    // access without a declared format, 16-bit types, image gathers, subgroup size control. The game's
    // own drawing uses none of it, so enabling it changes nothing for the picture.
    features.shaderStorageImageReadWithoutFormat = available.shaderStorageImageReadWithoutFormat;
    features.shaderStorageImageWriteWithoutFormat = available.shaderStorageImageWriteWithoutFormat;
    features.shaderInt16 = available.shaderInt16;
    features.shaderImageGatherExtended = available.shaderImageGatherExtended;
    // Different blending per attachment: the upscaler's pipelines blend the picture and write motion
    // vectors and the HUD mask unblended (as Direct3D 12's IndependentBlendEnable).
    features.independentBlend = available.independentBlend;
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    if (vkGetPhysicalDeviceFeatures2) {
      VkPhysicalDeviceVulkan12Features have12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
      VkPhysicalDeviceVulkan13Features have13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
      have12.pNext = &have13;
      VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      f2.pNext = &have12;
      vkGetPhysicalDeviceFeatures2(physical_, &f2);
      v12.shaderFloat16 = have12.shaderFloat16;
      v13.subgroupSizeControl = have13.subgroupSizeControl;
      v13.computeFullSubgroups = have13.computeFullSubgroups;
      // The shader compiler turns HLSL's discard into demote-to-helper for Vulkan 1.3 (DXC), which
      // needs this feature; drivers drew it without, but the shaders were invalid.
      v13.shaderDemoteToHelperInvocation = have13.shaderDemoteToHelperInvocation;
    }
    v12.pNext = &v13;
    v13.dynamicRendering = VK_TRUE;
    // Optional device extensions, by what the driver offers. Present id + present wait hold the
    // display queue to one frame (the Direct3D 12 frame latency waitable object's job); exclusive
    // fullscreen is Windows; display timing (Linux) reports when frames really reached the screen;
    // the memory budget feeds the VRAM meter; portability subset must be enabled where it exists (MoltenVK).
    std::unordered_set<std::string> offered;
    {
      uint32_t n = 0; vkEnumerateDeviceExtensionProperties(physical_, nullptr, &n, nullptr);
      std::vector<VkExtensionProperties> props(n); vkEnumerateDeviceExtensionProperties(physical_, nullptr, &n, props.data());
      for (uint32_t i = 0; i < n; ++i) offered.insert(props[i].extensionName);
    }
    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDevicePresentIdFeaturesKHR present_id_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR present_wait_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    if (offered.count(VK_KHR_PRESENT_ID_EXTENSION_NAME) && offered.count(VK_KHR_PRESENT_WAIT_EXTENSION_NAME) && vkGetPhysicalDeviceFeatures2) {
      VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      f2.pNext = &present_id_features; present_id_features.pNext = &present_wait_features;
      vkGetPhysicalDeviceFeatures2(physical_, &f2);
      present_id_features.pNext = nullptr; present_wait_features.pNext = nullptr;
      if (present_id_features.presentId && present_wait_features.presentWait) {
        extensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
        extensions.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
        present_id_features.pNext = &present_wait_features;
        v13.pNext = &present_id_features;
        present_id_on_ = present_wait_on_ = true;
      }
    }
#ifdef _WIN32
    if (surface_caps2_on_ && offered.count(VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME)) { extensions.push_back(VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME); fse_supported_ = true; }
#else
    if (offered.count(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME)) { extensions.push_back(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME); display_timing_on_ = true; }
#endif
    if (offered.count(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) { extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME); memory_budget_on_ = true; }
    // Each draw's constants, textures and samplers written straight into the command buffer
    // (vkCmdPushDescriptorSetKHR) instead of a descriptor set allocated and written for every draw:
    // less work per draw, and no limit on draws a frame. MELEE_VULKAN_PUSH_DESCRIPTORS=0 for A/B.
    static const bool push_wanted = [] { const char* v = std::getenv("MELEE_VULKAN_PUSH_DESCRIPTORS"); return !(v && *v == '0'); }();
    if (push_wanted && offered.count(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) { extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME); push_descriptors_on_ = true; }
    // AMD FSR's compute shaders use compute-shader derivatives (KHR, or NVIDIA's earlier name for it).
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
    const char* derivatives_ext = offered.count(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME) ? VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME
                                : offered.count(VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME) ? VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME : nullptr;
    if (derivatives_ext && vkGetPhysicalDeviceFeatures2) {
      VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      f2.pNext = &derivatives;
      vkGetPhysicalDeviceFeatures2(physical_, &f2);
      derivatives.pNext = nullptr;
      if (derivatives.computeDerivativeGroupLinear || derivatives.computeDerivativeGroupQuads) {
        extensions.push_back(derivatives_ext);
        derivatives.pNext = v13.pNext;   // in front of what the chain already holds (present id and wait)
        v13.pNext = &derivatives;
      }
    }
    if (offered.count("VK_KHR_portability_subset")) extensions.push_back("VK_KHR_portability_subset");
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = queue_family_; queue.queueCount = 1; queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &v12; info.queueCreateInfoCount = 1; info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = (uint32_t)extensions.size(); info.ppEnabledExtensionNames = extensions.data(); info.pEnabledFeatures = &features;
    check(vkCreateDevice(physical_, &info, nullptr, &device_), "device");
    load_device(device_);
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    if (!vkWaitForPresentKHR) present_wait_on_ = false;
    if (!vkCmdPushDescriptorSetKHR) push_descriptors_on_ = false;
    host::log("vulkan: draw bindings %s", push_descriptors_on_ ? "pushed with each draw" : "in a descriptor set per draw");
    host::log("vulkan: pacing: %s; exclusive fullscreen %s; display timing %s",
              present_wait_on_ && present_wait_wanted() ? "synced presents hold one frame in line for the display (present wait, MELEE_VULKAN_PRESENT_WAIT=1)"
              : present_wait_on_ ? "two frames in flight, the driver's own present queue (present wait available, off)" : "two frames in flight, the driver's own present queue",
              fse_supported_ ? "available" : "not offered", display_timing_on_ ? "available" : "not offered");
    // A start that died while building pipelines (a 0.9.86 Intel Mesa player segfaulted inside
    // vkCreateGraphicsPipelines, every start) left this marker behind: the cached pipelines, recipes
    // and SPIR-V are then cleared and built anew, so a bad cache costs one crash, not every start.
    // The marker counts the unfinished starts: at two, main.cpp starts on the default renderer.
    {
      std::error_code ec;
      const auto marker = shader_cache_dir() / "startup.pending";
      const int failed = vulkan_failed_starts();
      if (failed > 0) {
        host::log("vulkan: the last start did not finish; clearing the Vulkan shader cache");
        for (const auto& entry : std::filesystem::directory_iterator(shader_cache_dir(), ec))
          std::filesystem::remove(entry.path(), ec);
      }
      std::ofstream(marker, std::ios::binary) << (failed + 1);
    }
    VkPipelineCacheCreateInfo cache{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    std::ifstream f(shader_cache_dir() / "pipelines.bin", std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(f)), {});
    cache.initialDataSize = data.size(); cache.pInitialData = data.empty() ? nullptr : data.data();
    if (vkCreatePipelineCache(device_, &cache, nullptr, &pipeline_cache_) != VK_SUCCESS) {
      cache.initialDataSize = 0; cache.pInitialData = nullptr;
      vkCreatePipelineCache(device_, &cache, nullptr, &pipeline_cache_);
    }
  }

  void save_pipeline_cache() {
    size_t size = 0;
    if (vkGetPipelineCacheData(device_, pipeline_cache_, &size, nullptr) != VK_SUCCESS || !size) return;
    std::string data(size, '\0');
    if (vkGetPipelineCacheData(device_, pipeline_cache_, &size, data.data()) != VK_SUCCESS) return;
    std::ofstream f(shader_cache_dir() / "pipelines.bin", std::ios::binary);
    f.write(data.data(), (std::streamsize)size);
  }

  void create_layouts() {
    // Pushed bindings cannot be dynamic uniform buffers: the constants' place in the ring then goes
    // in each write instead of a dynamic offset. The shaders are the same either way.
    const VkDescriptorType constants = push_descriptors_on_ ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    VkDescriptorSetLayoutBinding b[4]{};
    b[0] = {kBindingVS, constants, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[1] = {kBindingPS, constants, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[2] = {kBindingTex, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[3] = {kBindingSamp, VK_DESCRIPTOR_TYPE_SAMPLER, 8, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 4; li.pBindings = b;
    if (push_descriptors_on_) li.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    check(vkCreateDescriptorSetLayout(device_, &li, nullptr, &set_layout_), "descriptor layout");
    li.flags = 0;
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &set_layout_;
    check(vkCreatePipelineLayout(device_, &pl, nullptr, &layout_), "pipeline layout");
    VkDescriptorSetLayoutBinding bb[2]{};
    bb[0] = {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bb[1] = {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    li.bindingCount = 2; li.pBindings = bb;
    check(vkCreateDescriptorSetLayout(device_, &li, nullptr, &blit_set_layout_), "descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BlitConstants)};
    pl.pSetLayouts = &blit_set_layout_; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(device_, &pl, nullptr, &blit_layout_), "pipeline layout");
  }

  void create_frames() {
    for (auto& f : frames_) {
      VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
      pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; pool.queueFamilyIndex = queue_family_;
      check(vkCreateCommandPool(device_, &pool, nullptr, &f.commands), "command pool");
      VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
      alloc.commandPool = f.commands; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 2;
      VkCommandBuffer buffers[2];
      check(vkAllocateCommandBuffers(device_, &alloc, buffers), "command buffers");
      f.upload = buffers[0]; f.cmd = buffers[1];
      VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
      check(vkCreateFence(device_, &fence, nullptr, &f.fence), "fence");
      VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      check(vkCreateSemaphore(device_, &sem, nullptr, &f.acquired), "semaphore");
      VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2 * 8192}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8 * 8192 + 64},
                                      {VK_DESCRIPTOR_TYPE_SAMPLER, 8 * 8192 + 64}};
      VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
      dp.maxSets = 8192 + 64; dp.poolSizeCount = 3; dp.pPoolSizes = sizes;
      check(vkCreateDescriptorPool(device_, &dp, nullptr, &f.pool), "descriptor pool");
      f.ring = mapped_buffer(kRingBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
      // GPU timers: frame start, present pass start and end, frame end (read a slot later, never waited on).
      if (timestamp_bits_ && limits_.timestampPeriod > 0 && vkCreateQueryPool) {
        VkQueryPoolCreateInfo q{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        q.queryType = VK_QUERY_TYPE_TIMESTAMP; q.queryCount = kTimers;
        if (vkCreateQueryPool(device_, &q, nullptr, &f.timers) != VK_SUCCESS) f.timers = VK_NULL_HANDLE;
      }
    }
  }

  // A host-visible buffer the CPU writes through. Some drivers (Mesa's lavapipe under WSL) report a
  // successful vkMapMemory and hand back MAP_FAILED for a large allocation, so the pointer is checked,
  // other host-visible memory types are tried, and the per-frame ring may shrink (to 24 MB) before
  // giving up, which makes the game fall back to another renderer instead of writing through it.
  Buffer mapped_buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool may_shrink) {
    VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(physical_, &props);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (VkDeviceSize bytes = size; bytes >= (may_shrink ? (24ull << 20) : size); bytes /= 2) {
      Buffer b; b.size = bytes;
      VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = bytes; bi.usage = usage;
      check(vkCreateBuffer(device_, &bi, nullptr, &b.buffer), "buffer");
      VkMemoryRequirements req; vkGetBufferMemoryRequirements(device_, b.buffer, &req);
      for (uint32_t t = 0; t < props.memoryTypeCount; ++t) {
        if (!(req.memoryTypeBits & (1u << t)) || (props.memoryTypes[t].propertyFlags & want) != want) continue;
        VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size, t};
        if (vkAllocateMemory(device_, &mi, nullptr, &b.memory) != VK_SUCCESS) continue;
        void* p = nullptr;
        if (vkBindBufferMemory(device_, b.buffer, b.memory, 0) == VK_SUCCESS &&
            vkMapMemory(device_, b.memory, 0, req.size, 0, &p) == VK_SUCCESS && p && p != reinterpret_cast<void*>(~uintptr_t(0))) {
          b.mapped = static_cast<uint8_t*>(p);
          if (bytes != size) host::log("vulkan: per-frame buffer %llu MB (the driver could not map %llu MB)", (unsigned long long)(bytes >> 20), (unsigned long long)(size >> 20));
          return b;
        }
        vkFreeMemory(device_, b.memory, nullptr); b.memory = VK_NULL_HANDLE;
      }
      vkDestroyBuffer(device_, b.buffer, nullptr);
      if (!may_shrink) break;
    }
    throw std::runtime_error("vulkan: the driver could not map a host-visible buffer");
  }

  void create_swapchain() {
    if (window_.drawable_size) window_.drawable_size(&client_w_, &client_h_);
    VkSurfaceCapabilitiesKHR caps; check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps), "surface capabilities");
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) extent = {(uint32_t)client_w_, (uint32_t)client_h_};
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    if (!extent.width || !extent.height) { swapchain_dirty_ = true; return; }   // minimized: try again later
    uint32_t n = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n); vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, formats.data());
    VkSurfaceFormatKHR format = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
    for (const auto& f : formats)
      if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { format = f; break; }
    if (swap_format_ != VK_FORMAT_UNDEFINED && format.format != swap_format_) format.format = swap_format_;   // the present pipeline was built for it
    swap_format_ = format.format;
    // Exclusive fullscreen (Windows, VK_EXT_full_screen_exclusive): the swapchain is made with the
    // application-controlled chain and the mode acquired after it; a refusal falls back to borderless
    // fullscreen for the session, as the Direct3D renderers do.
    const bool exclusive = want_exclusive_;
    std::vector<VkPresentModeKHR> modes;
#ifdef _WIN32
    VkSurfaceFullScreenExclusiveInfoEXT fse{VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT};
    VkSurfaceFullScreenExclusiveWin32InfoEXT fse_win32{VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT};
    fse.fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;
    fse_win32.hmonitor = MonitorFromWindow((HWND)window_.window, MONITOR_DEFAULTTONEAREST);
    fse.pNext = &fse_win32;
    if (exclusive && vkGetPhysicalDeviceSurfacePresentModes2EXT) {
      VkPhysicalDeviceSurfaceInfo2KHR si{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR, &fse, surface_};
      if (vkGetPhysicalDeviceSurfacePresentModes2EXT(physical_, &si, &n, nullptr) == VK_SUCCESS && n) {
        modes.resize(n); vkGetPhysicalDeviceSurfacePresentModes2EXT(physical_, &si, &n, modes.data());
      }
    }
#endif
    if (modes.empty()) {
      vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &n, nullptr);
      modes.resize(n); vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &n, modes.data());
    }
    auto has = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
    // Synced (FIFO) with V-Sync on, and on a display that follows the frames (tear_free_, from the
    // presentation loop): there a frame starts its own refresh, so syncing costs nothing under the
    // refresh and a frame that comes a little early waits instead of tearing. Otherwise IMMEDIATE,
    // which is what DXGI_PRESENT_ALLOW_TEARING gives the Direct3D renderers; MAILBOX only where
    // IMMEDIATE is not offered. A change of either asks for the swapchain again (present()).
    present_synced_ = synced_wanted();
    present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    if (!present_synced_) present_mode_ = has(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR : has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
#ifdef _WIN32
    if (exclusive) info.pNext = &fse;
#endif
    info.surface = surface_;
    info.minImageCount = std::max(caps.minImageCount, 3u);
    if (caps.maxImageCount) info.minImageCount = std::min(info.minImageCount, caps.maxImageCount);
    info.imageFormat = format.format; info.imageColorSpace = format.colorSpace; info.imageExtent = extent;
    info.imageArrayLayers = 1; info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; info.presentMode = present_mode_; info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_;
    VkSwapchainKHR created = VK_NULL_HANDLE;
    check(vkCreateSwapchainKHR(device_, &info, nullptr, &created), "swapchain");
    release_exclusive();   // the old swapchain's, before it goes
    destroy_swapchain();
    swapchain_ = created; swap_extent_ = extent;
    present_id_ = last_present_id_ = 0; present_wait_timeouts_ = 0;   // present ids count per swapchain
    timing_reset();
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
    swap_images_.resize(n); vkGetSwapchainImagesKHR(device_, swapchain_, &n, swap_images_.data());
    for (VkImage image : swap_images_) {
      VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      v.image = image; v.viewType = VK_IMAGE_VIEW_TYPE_2D; v.format = swap_format_;
      v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      VkImageView view; check(vkCreateImageView(device_, &v, nullptr, &view), "swapchain view");
      swap_views_.push_back(view);
      VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
      VkSemaphore done; check(vkCreateSemaphore(device_, &sem, nullptr, &done), "semaphore");
      swap_done_.push_back(done);
    }
    swapchain_dirty_ = false;
#ifdef GX_PC_SETTINGS
    if (settings_ready_) ImGui_ImplVulkan_SetMinImageCount(info.minImageCount);
#endif
#ifdef _WIN32
    if (exclusive) acquire_exclusive();   // last: a refusal marks the swapchain dirty, to be made again plain
#endif
  }
  void destroy_swapchain() {
    for (VkImageView v : swap_views_) vkDestroyImageView(device_, v, nullptr);
    for (VkSemaphore s : swap_done_) vkDestroySemaphore(device_, s, nullptr);
    swap_views_.clear(); swap_done_.clear(); swap_images_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }

  int pick_scale() const {
    if (forced_scale_ > 0) return forced_scale_;   // the upscaler's render size (configure_upscaler)
    const int ssaa = std::clamp(opts_.ssaa, 1, 4);
    const int max_scale = 16;
    if (opts_.efb_scale > 0) return std::clamp(opts_.efb_scale * ssaa, 1, max_scale);
    const float aspect = 73.0f / 60.0f;
    float vw = (float)client_w_, vh = vw / aspect;
    if (vh > client_h_) { vh = (float)client_h_; vw = vh * aspect; }
    const int s = std::max((int)std::ceil(vw / (480.0f * aspect)), (int)std::ceil(vh / 480.0f));
    return std::clamp(s * ssaa, 1, max_scale);
  }

  VkSampleCountFlagBits supported_samples(int wanted) const {
    const VkSampleCountFlags ok = limits_.framebufferColorSampleCounts & limits_.framebufferDepthSampleCounts;
    for (int n = std::clamp(wanted, 1, 8); n > 1; n /= 2)
      if (ok & VkSampleCountFlags(n)) return VkSampleCountFlagBits(n);
    return VK_SAMPLE_COUNT_1_BIT;
  }

  // ---- Images ----
  Image make_image(uint32_t w, uint32_t h, uint32_t levels, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
                   VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) {
    Image img; img.width = w; img.height = h; img.levels = levels; img.format = format;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D; info.format = format; info.extent = {w, h, 1}; info.mipLevels = levels; info.arrayLayers = 1;
    info.samples = samples; info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE; info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device_, &info, nullptr, &img.image), "image");
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device_, img.image, &req);
    img.memory = heap_.allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkBindImageMemory(device_, img.image, img.memory.memory, img.memory.offset), "image bind");
    VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    v.image = img.image; v.viewType = VK_IMAGE_VIEW_TYPE_2D; v.format = format; v.subresourceRange = {aspect, 0, levels, 0, 1};
    check(vkCreateImageView(device_, &v, nullptr, &img.view), "image view");
    img.bytes = req.size;
    return img;
  }
  void destroy(Image& img) {
    if (img.view) vkDestroyImageView(device_, img.view, nullptr);
    if (img.image) vkDestroyImage(device_, img.image, nullptr);
    if (img.image) heap_.release(img.memory);
    img = Image{};
  }
  // An image the GPU may still read this frame or the frames in flight goes when this slot comes round again.
  // The GPU idle, with the frame generation presenter's present out first (it uses the queue).
  void idle() {
#ifdef _WIN32
    fg_presenter_wait();
#endif
    vkDeviceWaitIdle(device_);
  }
  void retire(Image& img) { if (img.image) frames_[slot_].dead.push_back(img); img = Image{}; }

  static void barrier(VkCommandBuffer cmd, Image& img, VkImageLayout to, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    if (img.layout == to) return;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = img.layout; b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img.image; b.subresourceRange = {aspect, 0, img.levels, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    img.layout = to;
  }
  static void raw_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from; b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
  }

  void create_efb() {
    width_ = EFB_WIDTH * scale_; height_ = EFB_HEIGHT * scale_;
    efb_color_ = make_image(width_, height_, 1, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    efb_depth_ = make_image(width_, height_, 1, VK_FORMAT_D32_SFLOAT,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    // MSAA: the draws go to multisampled copies, resolved into the images above (colour averaged,
    // depth from sample 0) whenever an EFB pass ends, which is before every copy and present.
    samples_ = upscaling_ ? VK_SAMPLE_COUNT_1_BIT : supported_samples(opts_.msaa);   // the upscaler reads single-sample targets
    if (upscaling_) {
      mvec_img_ = make_image(width_, height_, 1, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
      hud_img_ = make_image(width_, height_, 1, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
      aux_clear_pending_ = true;
    }
    if (samples_ != VK_SAMPLE_COUNT_1_BIT) {
      msaa_color_ = make_image(width_, height_, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, samples_);
      msaa_depth_ = make_image(width_, height_, 1, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, samples_);
    }
    msaa_applied_ = opts_.msaa;
    if (samples_ != VK_SAMPLE_COUNT_1_BIT) host::log("vulkan: %dx MSAA", (int)samples_);
    efb_cleared_ = false;
  }

  void create_white() {
    white_ = make_image(1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    pending_white_ = true;
  }

  // ---- Frame ----
  struct Frame_ {
    VkCommandPool commands = VK_NULL_HANDLE;
    VkCommandBuffer upload = VK_NULL_HANDLE, cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    Buffer ring;
    std::vector<Image> dead;
    std::vector<std::pair<VkBuffer, VkDeviceMemory>> dead_buffers;
    bool uploads = false;
    VkQueryPool timers = VK_NULL_HANDLE;
    uint32_t timer_mask = 0;   // which timestamps this slot's frame wrote (bit i: query i)
  };
  static constexpr uint32_t kTimers = 4;

  VkDeviceSize ring_alloc(VkDeviceSize bytes, VkDeviceSize align) {
    Buffer& r = frames_[slot_].ring;
    const VkDeviceSize at = (r.used + align - 1) / align * align;
    if (at + bytes > r.size) throw std::runtime_error("vulkan: a frame needed more than the per-frame buffer");
    r.used = at + bytes;
    return at;
  }

  void begin_frame(const Frame& frame) {
    slot_ = (slot_ + 1) % kFrames;
    Frame_& f = frames_[slot_];
    present_wait_ = 0; acquire_wait_ = 0;   // a frame that presents nothing waited for nothing
    // The frames still running on the GPU when this one starts: the queue depth the player feels.
    unsigned queued = 0;
    for (const auto& other : frames_) if (vkGetFenceStatus && vkGetFenceStatus(device_, other.fence) == VK_NOT_READY) ++queued;
    vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX);
    read_gpu_timers(slot_);
    if (++frame_counter_ % 300 == 0) {
      // As Direct3D 12's line: what a "gets laggier the longer I play" report needs.
      host::log("gpu: %.2f ms/frame (present pass %.2f), %u frames queued, solver pairs %zu, textures %zu, pipelines %zu",
                gpu_frame_samples_ ? gpu_frame_ms_ / gpu_frame_samples_ : 0.0, gpu_present_samples_ ? gpu_present_ms_ / gpu_present_samples_ : 0.0,
                queued, solver_pair_counter().load(std::memory_order_relaxed), textures_.size() + copies_.size(), pipelines_.size());
      gpu_frame_ms_ = gpu_present_ms_ = 0; gpu_frame_samples_ = gpu_present_samples_ = 0;
    }
    if (frame_counter_ % 30 == 1) update_vram();
    for (auto& img : f.dead) destroy(img);
    f.dead.clear();
    for (auto& b : f.dead_buffers) { vkDestroyBuffer(device_, b.first, nullptr); vkFreeMemory(device_, b.second, nullptr); }
    f.dead_buffers.clear();
    vkResetCommandPool(device_, f.commands, 0);
    vkResetDescriptorPool(device_, f.pool, 0);
    f.ring.used = 0; f.uploads = false;
    ++sequence_;
    swap_in_ready_replacements();
    if (async_pipelines_) integrate_pipelines();
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(f.upload, &begin), "begin upload");
    check(vkBeginCommandBuffer(f.cmd, &begin), "begin frame");
    f.timer_mask = 0;
    if (f.timers) {
      vkCmdResetQueryPool(f.upload, f.timers, 0, kTimers);
      vkCmdWriteTimestamp(f.upload, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, f.timers, 0);
      f.timer_mask |= 1;
    }
    rendering_ = false;
    if (pending_white_) {
      const uint32_t white = 0xFFFFFFFFu;
      upload_level(white_, 0, 1, 1, &white);
      finish_upload(white_);
      pending_white_ = false;
    }
#ifdef GX_PC_SETTINGS
    if (settings_ui_ready()) {
      if (settings_textures_dirty()) drop_textures();
      settings_set_game_aspect(presented_aspect(opts_, client_w_, client_h_, frame_widenable_));
      settings_set_hud_snapshot(frame);
      const bool old_vsync = opts_.vsync;
      if (settings_frame(settings_state_, opts_)) {
        apply_fullscreen_mode();
        if (opts_.vsync != old_vsync) swapchain_dirty_ = true;
      }
    }
#endif
    // Alt+Enter, and coming back from Alt+Tab in exclusive fullscreen (Windows left it; it is
    // entered again), as the Direct3D renderers handle them.
    if (host::window_take_fullscreen_toggle()) {
      if (opts_.exclusive_fullscreen) opts_.exclusive_fullscreen = false;
      else opts_.fullscreen = !host::window_is_fullscreen();
      apply_fullscreen_mode();
    }
    if (host::window_take_activation() && opts_.exclusive_fullscreen) apply_fullscreen_mode();
    configure_upscaler();
    aux_clear_pending_ = true;
    in_match_ = frame_in_match(frame);
    if (in_match_ != was_in_match_) { upscale_reset_ = true; was_in_match_ = in_match_; }
#ifdef _WIN32
    {
      bool ui_drawn = false;   // the settings panel open: frames are shown as rendered (no generated panel)
#ifdef GX_PC_SETTINGS
      if (settings_ui_ready()) if (const ImDrawData* d = ImGui::GetDrawData()) ui_drawn = d->TotalVtxCount > 0;
#endif
      update_fsr_frame_generation(ui_drawn);
    }
#endif
#ifdef _WIN32
    if (upscaling_) streamline::jitter(presented_, &jitter_x_, &jitter_y_);
    else jitter_x_ = jitter_y_ = 0.0f;
#endif
    // The internal scale follows the window (auto scale) and the MSAA setting, whoever changed them:
    // the panel, Alt+Enter, or a drag of the window edge (resize()).
    if (pick_scale() != scale_ || opts_.msaa != msaa_applied_) rebuild_efb();
    const int lightweight = opts_.lightweight;
    set_lightweight_level(lightweight);
    publish_look(opts_);
    frame_widenable_ = frame_has_widenable_scene(frame);
    effects_filter_ = effects_filter(frame, lightweight_effects_level(opts_.effects_level, lightweight));
    lightweight_filter_ = lightweight_filter(frame, lightweight);
#ifdef GX_PC_SETTINGS
    if (texpack::configure(opts_.custom_textures, opts_.dump_textures) || texpack::take_cosmetics_changed()) drop_textures();
#endif
    if (opts_.anisotropy != anisotropy_applied_) {
      anisotropy_applied_ = opts_.anisotropy;
      idle();
      for (auto& s : samplers_) vkDestroySampler(device_, s.second, nullptr);
      samplers_.clear();
    }
    if (!efb_cleared_) {
      barrier(f.cmd, efb_color_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      barrier(f.cmd, efb_depth_, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
      begin_efb(true);
      efb_cleared_ = true;
    }
  }

  void end_frame(bool presented) {
    Frame_& f = frames_[slot_];
#ifdef _WIN32
    fg_presenter_wait();
#endif
    end_efb();
    if (f.timers) { vkCmdWriteTimestamp(f.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f.timers, 3); f.timer_mask |= 8; }
    check(vkEndCommandBuffer(f.upload), "end upload");
    check(vkEndCommandBuffer(f.cmd), "end frame");
    VkCommandBuffer buffers[] = {f.upload, f.cmd};
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 2; submit.pCommandBuffers = buffers;
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (presented && acquired_) {
      submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &f.acquired; submit.pWaitDstStageMask = &wait_stage;
      submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &swap_done_[image_index_];
    }
    vkResetFences(device_, 1, &f.fence);
    const VkResult submitted = vkQueueSubmit(queue_, 1, &submit, f.fence);
    if (submitted == VK_ERROR_DEVICE_LOST) {
      // The driver lost the GPU (a Linux AMD player: "GPUVM fault" as every Training stage loaded, on
      // 0.9.93 and 0.9.95; OpenGL drew it). Counted as two failed starts: the next launch starts on the
      // default renderer (main.cpp) instead of losing the device again.
      std::ofstream(shader_cache_dir() / "startup.pending", std::ios::binary) << 2;
      host::log("vulkan: the GPU was lost; the next start uses the default renderer (pick Vulkan in the settings to try it again)");
    }
    check(submitted, "submit");
    if (presented && acquired_) {
      // As Direct3D 12: a shown frame waits for its place in the display queue after it is recorded
      // and submitted (the CPU records this frame while the GPU runs the last one), then for the
      // presentation loop's deadline; both are this frame's wait (presentation_wait_seconds).
      const double wait_start = host::now_seconds();
      wait_previous_present();
      const double spin_start = host::now_seconds();
      spin_to_deadline();
      const double present_call = host::now_seconds();
      VkPresentInfoKHR p{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
      p.waitSemaphoreCount = 1; p.pWaitSemaphores = &swap_done_[image_index_];
      p.swapchainCount = 1; p.pSwapchains = &swapchain_; p.pImageIndices = &image_index_;
      const uint64_t id = present_id_ + 1;
      const void** tail = &p.pNext;
      VkPresentIdKHR present_id{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
      if (present_id_on_) { present_id.swapchainCount = 1; present_id.pPresentIds = &id; *tail = &present_id; tail = &present_id.pNext; }
#ifndef _WIN32
      VkPresentTimeGOOGLE present_time{(uint32_t)id, 0};
      VkPresentTimesInfoGOOGLE present_times{VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE};
      if (display_timing_on_) { present_times.swapchainCount = 1; present_times.pTimes = &present_time; *tail = &present_times; tail = &present_times.pNext; }
#endif
      (void)tail;
      const VkResult r = [&] { host::perf::Scope wait_cost(host::perf::Stage::Wait); return vkQueuePresentKHR(queue_, &p); }();
      const double present_end = host::now_seconds();
      present_wait_ = acquire_wait_ + (present_end - wait_start);
      stat_acquire_ += acquire_wait_; stat_wait_ += spin_start - wait_start; stat_spin_ += present_call - spin_start;
      stat_present_ += present_end - present_call; ++stat_frames_;
      if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) { present_id_ = id; if (present_id_on_) last_present_id_ = id; }
      if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || exclusive_lost(r)) swapchain_dirty_ = true;
      else if (r != VK_SUCCESS) check(r, "present");
      ++presented_;
      if ((presented_ & 31) == 0) update_variable_refresh();
#ifdef _WIN32
      if ((presented_ & 127) == 0) track_vblank();   // a move to another monitor restarts the clock there
#endif
      if (capture_pending_) write_capture();
#ifdef _WIN32
      if (fsr_fg_generated_ && !fg_show_generated_only()) post_rendered_frame();
#endif
    }
    acquired_ = false;
#ifdef _WIN32
    fsr_fg_generated_ = false;
#endif
  }

  void begin_efb(bool clear_all = false) {
    if (rendering_) return;
    Frame_& f = frames_[slot_];
    barrier(f.cmd, efb_color_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    barrier(f.cmd, efb_depth_, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    const bool msaa = samples_ != VK_SAMPLE_COUNT_1_BIT;
    if (msaa) {
      barrier(f.cmd, msaa_color_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      barrier(f.cmd, msaa_depth_, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
    }
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = msaa ? msaa_color_.view : efb_color_.view; color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = clear_all ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0, 0, 0, 1}};
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = msaa ? msaa_depth_.view : efb_depth_.view; depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = color.loadOp; depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE; depth.clearValue.depthStencil = {0.0f, 0};
    if (msaa) {
      color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT; color.resolveImageView = efb_color_.view;
      color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      depth.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT; depth.resolveImageView = efb_depth_.view;
      depth.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    }
    VkRenderingAttachmentInfo colors[3] = {color};
    uint32_t color_count = 1;
    if (upscaling_ && mvec_img_.image && hud_img_.image) {
      // Motion vectors and the HUD mask (attachments 1 and 2), cleared once a frame as on Direct3D 12.
      barrier(f.cmd, mvec_img_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      barrier(f.cmd, hud_img_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      for (int i = 1; i <= 2; ++i) {
        colors[i] = VkRenderingAttachmentInfo{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colors[i].imageView = i == 1 ? mvec_img_.view : hud_img_.view; colors[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colors[i].loadOp = clear_all || aux_clear_pending_ ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE; colors[i].clearValue.color = {{0, 0, 0, 0}};
      }
      aux_clear_pending_ = false;
      color_count = 3;
    }
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, {width_, height_}}; info.layerCount = 1;
    info.colorAttachmentCount = color_count; info.pColorAttachments = colors; info.pDepthAttachment = &depth;
    vkCmdBeginRendering(f.cmd, &info);
    rendering_ = true;
    bound_pipeline_ = VK_NULL_HANDLE;
  }
  void end_efb() {
    if (!rendering_) return;
    vkCmdEndRendering(frames_[slot_].cmd);
    rendering_ = false;
  }

  void rebuild_efb() {
    idle();
    host::log("vulkan: internal scale %d -> %d", scale_, pick_scale());
    end_efb();
    retire(efb_color_); retire(efb_depth_); retire(msaa_color_); retire(msaa_depth_); retire(mvec_img_); retire(hud_img_);
    for (auto& c : copies_) retire(c.second);
    copies_.clear();
    scale_ = pick_scale();
    create_efb();
    Frame_& f = frames_[slot_];
    begin_efb(true);
    (void)f;
    efb_cleared_ = true;
  }

  void drop_textures() {
    for (auto& t : textures_) retire(t.second);
    textures_.clear();
    pending_hd_.clear();
    replacement_bytes_ = 0;
  }

  // A pack replacement that finished decoding in the background: the original drawn meanwhile goes,
  // so the next use builds the texture from the replacement (load() then returns it at once). The
  // retired image lives until this slot comes round again, past every frame that may still read it.
  void swap_in_ready_replacements() {
    if (pending_hd_.empty() || (sequence_ & 1)) return;
    // A few a frame: the background decoder finishes them in batches, and swapping a whole batch in
    // at once uploaded every one of its pictures in the same frame (a 0.3 s hitch in the middle of a
    // match with a large pack).
    int swapped = 0;
    for (auto it = pending_hd_.begin(); it != pending_hd_.end() && swapped < 6;) {
      auto tex = textures_.find(it->first);
      if (tex == textures_.end()) { it = pending_hd_.erase(it); continue; }
      if (!texpack::ready(it->second)) { ++it; continue; }
      ++swapped;
      replacement_bytes_ -= std::min(replacement_bytes_, tex->second.charged);
      retire(tex->second);
      textures_.erase(tex);
      it = pending_hd_.erase(it);
    }
  }

  // ---- Shaders and pipelines ----
  VkShaderModule module(const std::vector<uint32_t>& words) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = words.size() * 4; info.pCode = words.data();
    VkShaderModule m; check(vkCreateShaderModule(device_, &info, nullptr, &m), "shader module");
    return m;
  }
  VkShaderModule cached_module(uint64_t key, const std::string& hlsl, bool vertex) {
    const uint64_t k = key ^ (vertex ? 0xA5A5000000000000ull : 0x5A5A000000000000ull);
    if (auto i = modules_.find(k); i != modules_.end()) return i->second;
    VkShaderModule m = module(spirv_for(hlsl, vertex));
    modules_.emplace(k, m);
    return m;
  }

  VkPipeline blit_pipeline(VkFormat format) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, blit_vs_, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, blit_ps_, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask = 0xF;
    return build_pipeline(stages, vi, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, blend,
                          false, false, VK_COMPARE_OP_ALWAYS, blit_layout_, format, VK_FORMAT_UNDEFINED);
  }

  VkPipeline build_pipeline(const VkPipelineShaderStageCreateInfo* stages, const VkPipelineVertexInputStateCreateInfo& vi,
                            VkPrimitiveTopology topology, VkPolygonMode fill, VkCullModeFlags cull,
                            const VkPipelineColorBlendAttachmentState& blend, bool depth_test, bool depth_write, VkCompareOp compare,
                            VkPipelineLayout layout, VkFormat color, VkFormat depth,
                            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                            bool upscale_outputs = false, VkColorComponentFlags mvec_write = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT,
                            VkColorComponentFlags hud_write = VK_COLOR_COMPONENT_R_BIT) {
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = topology;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = fill; rs.cullMode = cull; rs.frontFace = VK_FRONT_FACE_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = samples;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = depth_test; ds.depthWriteEnable = depth_write; ds.depthCompareOp = compare;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    // With the upscaler's outputs: motion vectors (attachment 1) and the HUD mask (2), never blended.
    VkPipelineColorBlendAttachmentState blends[3] = {blend, {}, {}};
    blends[1].colorWriteMask = mvec_write; blends[2].colorWriteMask = hud_write;
    const VkFormat formats[3] = {color, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R8_UNORM};
    const uint32_t outputs = upscale_outputs ? 3 : 1;
    cb.attachmentCount = outputs; cb.pAttachments = blends;
    const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = outputs; rendering.pColorAttachmentFormats = formats; rendering.depthAttachmentFormat = depth;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering; info.stageCount = 2; info.pStages = stages; info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia; info.pViewportState = &vp; info.pRasterizationState = &rs; info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds; info.pColorBlendState = &cb; info.pDynamicState = &dy; info.layout = layout;
    VkPipeline p; check(vkCreateGraphicsPipelines(device_, pipeline_cache_, 1, &info, nullptr, &p), "pipeline");
    return p;
  }

  // The GX blend, depth and cull state of a draw, packed into the pipeline key.
  static uint32_t pipeline_state(const DrawCall& dc, DrawTopology topology, bool wire) {
    const uint32_t blend = dc.bp.blendmode(), zmode = dc.bp.zmode(), cull = dc.bp.cullmode();
    uint32_t sf = bits(blend, 8, 3), df = bits(blend, 5, 3);
    if ((dc.bp.zcontrol() & 7) != 1) {   // no destination alpha in this EFB format: it reads as 1
      if (sf == 6) sf = 1; if (sf == 7) sf = 0; if (df == 6) df = 1; if (df == 7) df = 0;
    }
    uint32_t s = 0;
    s |= (blend & 1);                       // 0: blending on
    s |= sf << 1;                           // 1..3
    s |= df << 4;                           // 4..6
    s |= ((blend >> 11) & 1) << 7;          // 7: reverse subtract
    s |= ((blend >> 3) & 1) << 8;           // 8: colour write
    s |= ((blend >> 4) & 1) << 9;           // 9: alpha write
    s |= (zmode & 1) << 10;                 // 10: depth test
    s |= (((zmode & 1) && (zmode & 16)) ? 1u : 0u) << 11;   // 11: depth write
    s |= bits(zmode, 1, 3) << 12;           // 12..14: compare
    s |= (cull & 3) << 15;                  // 15..16
    s |= (topology == DrawTopology::Lines ? 1u : 0u) << 17;
    s |= (wire ? 1u : 0u) << 18;
    return s;
  }

  VkPipeline pipeline(const DrawCall& dc, DrawTopology topology, bool wire) {
    auto vs = make_vs_uid(dc); auto ps = make_ps_uid(dc);
    if (upscaling_) vs.motion_vectors = ps.motion_vectors = 1;   // the shaders also write motion and the HUD mask
    // Bits 19 and up: the EFB's sample count, so an MSAA change builds its own pipelines.
    const PipelineKey key{vs.hash(), ps.hash(), pipeline_state(dc, topology, wire && wire_supported_) | (uint32_t(samples_) << 19) |
                                                (upscaling_ ? kStateUpscaleOutputs : 0u)};
    if (auto i = pipelines_.find(key); i != pipelines_.end()) return i->second;
    const std::string vs_hlsl = generate_vertex_shader(vs), ps_hlsl = generate_pixel_shader(ps);
    // A pipeline not built yet goes to the worker and this draw is skipped until it is there (a frame
    // or two), as Dolphin's "skip drawing" does: compiling it here held the whole frame, a hitch at
    // every new scene, effect and stage piece in a first session. A frame that copies the picture
    // into a texture outside a match (a portrait, a stage screen) and a depth-texture draw still wait
    // for theirs, since what they draw lasts in that texture for the whole scene.
    if (async_pipelines_ && !exact_frame_ && !(dc.bp.ztex2() & 12) && !worker_failed_.count(key)) {
      if (pending_pipelines_.insert(key).second) {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        jobs_.push_back({key, vs_hlsl, ps_hlsl});
        jobs_cv_.notify_one();
      }
      return VK_NULL_HANDLE;
    }
    VkPipeline p = game_pipeline(key, cached_module(key.vs, vs_hlsl, true), cached_module(key.ps, ps_hlsl, false));
    if (recipes_.size() < kMaxRecipes) recipes_.push_back({key.vs, key.ps, spirv_hash(vs_hlsl, true), spirv_hash(ps_hlsl, false), key.state});
    return p;
  }

  // Pipelines of earlier sessions, built before the first frame. A pipeline first needed mid-match
  // compiled on the render thread and held that frame (a stutter at each new effect or stage piece),
  // as on Direct3D 12 before its own prewarm. Each one the game builds is recorded with the SPIR-V
  // files of its two shaders; at the next start those are read back and built through the pipeline
  // cache. A recipe whose SPIR-V is gone (a cleared cache) is dropped.
  using Recipe = vk_recipes::Recipe;   // the file format (vulkan_recipes.h, unit-tested)
  static constexpr size_t kMaxRecipes = vk_recipes::kMax;
  std::vector<Recipe> recipes_;

  void prewarm_pipelines() {
    // The player's own recipes, then the ones the release ships (vk_recipes.shipped.bin, from a warmed
    // run: tools/package_release.py). SPIR-V is the same on every GPU and driver, so a first session on
    // Vulkan builds the common pipelines before its first frame instead of compiling them mid-match.
    // The shipped file has its own name so an update never overwrites what the player's sessions saved.
    const std::vector<Recipe> saved = vk_recipes::merge({shader_cache_dir() / "vk_recipes.bin", shader_cache_dir() / "vk_recipes.shipped.bin"});
    if (saved.empty()) return;
    const auto start = std::chrono::steady_clock::now();
    size_t built = 0;
    for (const Recipe& r : saved) {
      const PipelineKey key{r.vs, r.ps, r.state};
      // Another MSAA sample count than this session's draws to other images: kept for when it is used.
      if (state_samples(r.state) != samples_ || ((r.state & kStateUpscaleOutputs) != 0) != upscaling_) { recipes_.push_back(r); continue; }
      if (pipelines_.count(key)) continue;
      // Wireframe needs fillModeNonSolid, which a recipe saved on another GPU may have had.
      if (((r.state >> 18) & 1) && !wire_supported_) continue;
      const uint64_t vk = key.vs ^ 0xA5A5000000000000ull, pk = key.ps ^ 0x5A5A000000000000ull;
      VkShaderModule vm = VK_NULL_HANDLE, pm = VK_NULL_HANDLE;
      if (auto i = modules_.find(vk); i != modules_.end()) vm = i->second;
      else if (auto w = read_spirv(spirv_path(r.vs_spv, true)); !w.empty()) { vm = module(w); modules_.emplace(vk, vm); }
      if (auto i = modules_.find(pk); i != modules_.end()) pm = i->second;
      else if (auto w = read_spirv(spirv_path(r.ps_spv, false)); !w.empty()) { pm = module(w); modules_.emplace(pk, pm); }
      if (!vm || !pm) continue;
      game_pipeline(key, vm, pm);
      recipes_.push_back(r);
      ++built;
    }
    host::log("vulkan: prewarmed %zu pipelines from %zu recipes in %.1f ms", built, saved.size(),
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
  }

  void save_pipeline_recipes() {
    if (recipes_.empty()) return;
    if (!vk_recipes::write(shader_cache_dir() / "vk_recipes.bin", recipes_)) host::log("vulkan: the pipeline recipes could not be saved");
  }

  // ---- The pipeline worker ----
  // One thread builds the pipelines draws ask for (compile, shader modules, pipeline) and hands them
  // back; the render thread files them at the start of a frame (integrate_pipelines), so only it ever
  // touches the maps. MELEE_VULKAN_SYNC_PIPELINES=1 (tests): every pipeline built on the render thread.
  struct PipelineJob { PipelineKey key; std::string vs_hlsl, ps_hlsl; };
  struct PipelineDone {
    PipelineKey key{};
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule vs = VK_NULL_HANDLE, ps = VK_NULL_HANDLE;
    uint64_t vs_spv = 0, ps_spv = 0;
  };
  std::mutex jobs_mutex_;
  std::condition_variable jobs_cv_;
  std::deque<PipelineJob> jobs_;
  std::vector<PipelineDone> done_;
  bool worker_stop_ = false;
  std::thread worker_;
  std::unordered_set<PipelineKey, PipelineKeyHash> pending_pipelines_, worker_failed_;   // render thread only
  bool async_pipelines_ = false, exact_frame_ = false;

  void start_pipeline_worker() {
    const char* sync = std::getenv("MELEE_VULKAN_SYNC_PIPELINES");
    if (sync && *sync == '1') return;
    worker_ = std::thread([this] { pipeline_worker(); });
    async_pipelines_ = true;
  }
  void stop_pipeline_worker() {
    if (!worker_.joinable()) return;
    {
      std::lock_guard<std::mutex> lock(jobs_mutex_);
      worker_stop_ = true;
      jobs_.clear();
    }
    jobs_cv_.notify_all();
    worker_.join();
    async_pipelines_ = false;
    integrate_pipelines();
  }
  void pipeline_worker() {
    for (;;) {
      PipelineJob job;
      {
        std::unique_lock<std::mutex> lock(jobs_mutex_);
        jobs_cv_.wait(lock, [this] { return worker_stop_ || !jobs_.empty(); });
        if (worker_stop_) return;
        job = std::move(jobs_.front());
        jobs_.pop_front();
      }
      PipelineDone d;
      d.key = job.key;
      try {
        d.vs = module(spirv_for(job.vs_hlsl, true));
        d.ps = module(spirv_for(job.ps_hlsl, false));
        d.pipeline = make_game_pipeline(job.key, d.vs, d.ps);
        d.vs_spv = spirv_hash(job.vs_hlsl, true);
        d.ps_spv = spirv_hash(job.ps_hlsl, false);
      } catch (const std::exception& e) {
        host::log("vulkan: pipeline worker: %s", e.what());
      }
      std::lock_guard<std::mutex> lock(jobs_mutex_);
      done_.push_back(d);
    }
  }
  void integrate_pipelines() {
    std::vector<PipelineDone> done;
    {
      std::lock_guard<std::mutex> lock(jobs_mutex_);
      done.swap(done_);
    }
    for (const PipelineDone& d : done) {
      pending_pipelines_.erase(d.key);
      // A module already kept (the same shader in another pipeline) stays; the new copy goes, which
      // is allowed once a pipeline is built from it.
      const uint64_t vk = d.key.vs ^ 0xA5A5000000000000ull, pk = d.key.ps ^ 0x5A5A000000000000ull;
      if (d.vs && !modules_.emplace(vk, d.vs).second) vkDestroyShaderModule(device_, d.vs, nullptr);
      if (d.ps && !modules_.emplace(pk, d.ps).second) vkDestroyShaderModule(device_, d.ps, nullptr);
      if (!d.pipeline) { worker_failed_.insert(d.key); continue; }   // the next draw builds it here and says why
      if (!pipelines_.emplace(d.key, d.pipeline).second) { vkDestroyPipeline(device_, d.pipeline, nullptr); continue; }
      if (recipes_.size() < kMaxRecipes) recipes_.push_back({d.key.vs, d.key.ps, d.vs_spv, d.ps_spv, d.key.state, 0});
    }
  }

  VkPipeline game_pipeline(const PipelineKey& key, VkShaderModule vs_module, VkShaderModule ps_module) {
    VkPipeline p = make_game_pipeline(key, vs_module, ps_module);
    pipelines_.emplace(key, p);
    return p;
  }

  // The pipeline itself, kept nowhere: the pipeline worker builds through this too, so it reads only
  // what never changes after start (the layout) and the key (its sample count is the key's bits 19 up).
  VkPipeline make_game_pipeline(const PipelineKey& key, VkShaderModule vs_module, VkShaderModule ps_module) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs_module, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps_module, "main", nullptr};
    // gx::Vertex in declaration order of the generated vertex shader's inputs (locations 0..14).
    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> attrs;
    attrs.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0});
    attrs.push_back({1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12});
    attrs.push_back({2, 0, VK_FORMAT_R8G8B8A8_UNORM, 24});
    attrs.push_back({3, 0, VK_FORMAT_R8G8B8A8_UNORM, 28});
    for (uint32_t i = 0; i < 8; ++i) attrs.push_back({4 + i, 0, VK_FORMAT_R32G32_SFLOAT, 32 + 8 * i});
    attrs.push_back({12, 0, VK_FORMAT_R8G8B8A8_UINT, 96});
    attrs.push_back({13, 0, VK_FORMAT_R8G8B8A8_UINT, 100});
    attrs.push_back({14, 0, VK_FORMAT_R8_UINT, 104});
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();
    static const VkBlendFactor src[] = {VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
                                        VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA};
    static const VkBlendFactor dst[] = {VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
                                        VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA};
    static const VkCompareOp compare[] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_GREATER, VK_COMPARE_OP_EQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL,
                                          VK_COMPARE_OP_LESS, VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_ALWAYS};
    const uint32_t s = key.state;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = s & 1;
    blend.srcColorBlendFactor = src[bits(s, 1, 3)]; blend.dstColorBlendFactor = dst[bits(s, 4, 3)];
    blend.colorBlendOp = bits(s, 7, 1) ? VK_BLEND_OP_REVERSE_SUBTRACT : VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = (bits(s, 8, 1) ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT) : 0) |
                           (bits(s, 9, 1) ? VK_COLOR_COMPONENT_A_BIT : 0);
    static const VkCullModeFlags culls[] = {VK_CULL_MODE_NONE, VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_FRONT_BIT, VK_CULL_MODE_BACK_BIT};
    const bool wire_on = bits(s, 18, 1);
    VkPipeline p = build_pipeline(stages, vi, bits(s, 17, 1) ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                  wire_on ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL, wire_on ? VK_CULL_MODE_NONE : culls[bits(s, 15, 2)],
                                  blend, bits(s, 10, 1), bits(s, 11, 1), compare[bits(s, 12, 3)], layout_,
                                  VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT, state_samples(s), (s & kStateUpscaleOutputs) != 0,
                                  // Motion vectors belong to the surface in the depth buffer: only draws that write depth write
                                  // them, so a translucent layer over a moving fighter does not replace its motion (gx_d3d12.cpp).
                                  bits(s, 11, 1) ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT) : 0);
    return p;
  }

  // ---- Textures ----
  void upload_level(Image& img, uint32_t level, uint32_t w, uint32_t h, const void* rgba) {
    Frame_& f = frames_[slot_];
    const VkDeviceSize bytes = VkDeviceSize(w) * h * 4;
    VkBuffer source = f.ring.buffer; VkDeviceSize offset = 0;
    if (frames_[slot_].ring.used + bytes + 16 <= frames_[slot_].ring.size) {
      offset = ring_alloc(bytes, 16);
      std::memcpy(f.ring.mapped + offset, rgba, bytes);
    } else {   // a texture pack's large picture: its own staging buffer, freed with this frame
      Buffer staging = mapped_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
      std::memcpy(staging.mapped, rgba, bytes);
      source = staging.buffer;
      f.dead_buffers.push_back({staging.buffer, staging.memory});
    }
    barrier(f.upload, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy region{};
    region.bufferOffset = offset; region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1}; region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(f.upload, source, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    f.uploads = true;
  }
  void finish_upload(Image& img) { barrier(frames_[slot_].upload, img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); }

  Image* texture(const TextureRef& t) {
    if (auto c = copies_.find(t.addr & 0x3FFFFFFFu); c != copies_.end()) {
      c->second.used = sequence_;
      return &c->second;
    }
    if (!t.data || !t.width || !t.height) return nullptr;
#ifdef GX_PC_SETTINGS
    // Checked before the texture cache: a target may be learned after the guest texture was cached,
    // and the next frames must switch at once (as gx_d3d11.cpp get_texture).
    if (video_bg::wants_texture_names()) {
      int slot = -1;
      const auto video = video_bg::lookup(texpack::base_name(t, *t.data), t.width, t.height, &slot);
      if (video && slot >= 0 && slot < 2 && !video->bgra.empty())
        if (Image* img = update_video_image(video, slot)) { draw_video_slot_ = slot; return img; }
    }
#endif
    const uint64_t desc[] = {t.data->hash, t.width, t.height, t.format, t.tlut_format, t.mip_levels};
    const uint64_t key = look_key(hash_bytes(desc, sizeof desc), look_surface_);
    if (auto i = textures_.find(key); i != textures_.end()) { i->second.used = sequence_; return &i->second; }
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (auto treated = look_replacement(t, look_surface_)) {   // the look's treated copy, one level (gx_retro.h)
      Image img = make_image(treated->width, treated->height, 1, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT);
      upload_level(img, 0, treated->width, treated->height, treated->pixels.data());
      finish_upload(img);
      img.used = sequence_;
      return &textures_.emplace(key, img).first->second;
    }
#ifdef GX_PC_SETTINGS
    if (texpack::enabled() || texpack::cosmetics_enabled()) {
      const std::string name = texpack::base_name(t, *t.data);
      if (texpack::has(name) && !texpack::ready(name)) {
        // Not decoded yet: the original now, the replacement from the background (as on Direct3D 12).
        // Decoding the PNG here held the frame, a stutter at every new stage, effect and menu.
        texpack::request(name);
        pending_hd_[key] = name;
      } else if (texpack::has(name)) {
        auto r = texpack::load(name, replacement_bytes_ < replacement_budget_ ? replacement_budget_ - replacement_bytes_ : 0);
        texpack::note_lookup(r != nullptr);
        if (r) {
          const uint32_t levels = std::max(1u, std::min(r->levels, 16u));
          Image img = make_image(r->width, r->height, levels, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT);
          for (uint32_t level = 0; level < levels; ++level)
            upload_level(img, level, r->level_width[level], r->level_height[level], r->pixels.data() + r->level_offset[level]);
          finish_upload(img);
          img.used = sequence_;
          img.charged = img.bytes;
          replacement_bytes_ += img.bytes;
          return &textures_.emplace(key, img).first->second;
        }
      }
    }
#endif
    const uint32_t levels = texture_mip_count(t.width, t.height, t.mip_levels);
    Image img = make_image(t.width, t.height, levels, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT);
    const uint8_t* data = t.data->image.data(); size_t remaining = t.data->image.size();
    uint32_t w = t.width, h = t.height;
    for (uint32_t level = 0; level < levels; ++level) {
      const uint32_t bytes = texture_level_bytes(w, h, t.format);
      if (bytes > remaining) { destroy(img); throw std::runtime_error("GX texture mip exceeded snapshot bytes"); }
      decode_texture(data, w, h, t.format, t.data->palette.data(), t.tlut_format, decoded_);
      upload_level(img, level, w, h, decoded_.data());
      data += bytes; remaining -= bytes;
      w = std::max(1u, w / 2); h = std::max(1u, h / 2);
    }
    finish_upload(img);
    img.used = sequence_;
    return &textures_.emplace(key, img).first->second;
  }

  VkSampler sampler(uint32_t mode0, uint32_t mode1) {
    const uint64_t key = (uint64_t(mode0) << 32) | mode1;
    if (auto i = samplers_.find(key); i != samplers_.end()) return i->second;
    static const VkSamplerAddressMode wraps[] = {VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                 VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT};
    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.addressModeU = wraps[bits(mode0, 0, 2)]; s.addressModeV = wraps[bits(mode0, 2, 2)]; s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    const uint32_t minf = bits(mode0, 5, 3), mip = minf & 3; const bool linear = minf & 4;
    s.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    s.magFilter = bits(mode0, 4, 1) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    s.mipmapMode = mip == 2 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    s.mipLodBias = float(sbits(mode0, 9, 8)) / 32;
    s.minLod = bits(mode1, 0, 8) / 16.0f;
    s.maxLod = mip ? bits(mode1, 8, 8) / 16.0f : 0.0f;
    if (anisotropy_applied_ > 1 && anisotropy_supported_ && linear && bits(mode0, 4, 1)) {
      s.anisotropyEnable = VK_TRUE; s.maxAnisotropy = float(std::clamp(anisotropy_applied_, 1, 16));
    }
    VkSampler out; check(vkCreateSampler(device_, &s, nullptr, &out), "sampler");
    samplers_.emplace(key, out);
    return out;
  }

  // ---- Draws ----
  void draw(const Frame& frame, const DrawCall& dc, const DrawMatrices* matrices, size_t vertex_override) {
    if (skip_for_effects(effects_filter_, dc)) return;
    if (skip_for_lightweight(lightweight_filter_, frame, dc)) return;
    const float* vp = reinterpret_cast<const float*>(&dc.xf_regs[0x1A]);
    float x = (vp[3] - vp[0] - 342) * scale_, y = (vp[4] + vp[1] - 342) * scale_;
    float w = vp[0] * 2 * scale_, h = -vp[1] * 2 * scale_;
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    float znear = std::clamp(1 - vp[5] / 16777216.0f, 0.0f, 1.0f), zfar = std::clamp(1 - (vp[5] - vp[2]) / 16777216.0f, 0.0f, 1.0f);
    if (zfar < znear) std::swap(zfar, znear);
    const auto tl = dc.bp.reg[BP_SCISSORTL], br = dc.bp.reg[BP_SCISSORBR], so = dc.bp.reg[BP_SCISSOROFFSET];
    const int ox = bits(so, 0, 10) * 2 - 342, oy = bits(so, 10, 10) * 2 - 342;
    const int l = std::clamp(int(bits(tl, 12, 12)) - ox - 342, 0, EFB_WIDTH), t = std::clamp(int(bits(tl, 0, 12)) - oy - 342, 0, EFB_HEIGHT);
    const int r = std::clamp(int(bits(br, 12, 12)) - ox - 341, 0, EFB_WIDTH), b = std::clamp(int(bits(br, 0, 12)) - oy - 341, 0, EFB_HEIGHT);
    if (r <= l || b <= t) return;
    index_scratch_.clear();
    DrawTopology topology = DrawTopology::Unsupported;
    if (dc.segment_count) {
      for (uint32_t i = 0; i < dc.segment_count; ++i) {
        const auto& seg = frame.segments.at(dc.first_segment + i);
        const uint32_t base = vertex_override ? uint32_t(vertex_override + seg.first_vertex - dc.first_vertex) : seg.first_vertex;
        topology = append_segment_indices(index_scratch_, seg.primitive, seg.vertex_count, base);
      }
    } else topology = append_segment_indices(index_scratch_, dc.primitive, dc.vertex_count, vertex_override ? uint32_t(vertex_override) : dc.first_vertex);
    if (index_scratch_.empty()) return;
    const bool wire = look_wire(lightweight_filter_, dc);
    VkPipeline p;
    try { p = pipeline(dc, topology, wire); }
    catch (const std::exception& e) {
      if (++pipeline_failures_ <= 5) host::log("%s", e.what());
      return;
    }
    if (p == VK_NULL_HANDLE) return;   // its pipeline is still being built: drawn from the next frame it is ready
    Frame_& f = frames_[slot_];
    begin_efb();
    const bool flat = lightweight_flat(lightweight_filter_, dc);
    look_surface_ = look_surface(lightweight_filter_, dc);
    const bool unfiltered = look_unfiltered(lightweight_filter_, dc);
    // Textures first: an upload goes to the upload buffer, which runs before this frame's draws.
    VkDescriptorImageInfo images[8]{}, samplers[8]{};
    for (int unit = 0; unit < 8; ++unit) {
      Image* img = nullptr;
      if (dc.textures[unit].used) img = flat ? &white_ : texture(dc.textures[unit]);
      if (!img) img = &white_;
      if (img->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {   // an EFB copy drawn this frame
        end_efb(); barrier(f.cmd, *img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); begin_efb();
      }
      images[unit] = {VK_NULL_HANDLE, img->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      samplers[unit] = {sampler(unfiltered ? unfiltered_mode0(dc.textures[unit].mode0) : dc.textures[unit].mode0, dc.textures[unit].mode1),
                        VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    }
    VSConstants vc{}; PSConstants pc{};
    if (upscaling_) {
      // As Direct3D 12: the jittered projection and last presented frame's pose of this draw, for its motion.
      MotionInfo motion; motion.jitter_x = jitter_x_; motion.jitter_y = jitter_y_;
      const uint64_t presentation = (uint64_t)presented_ + 1;
      const auto* previous = last_poses_.previous(dc.identity, presentation, !skip_present_);
      if (previous) { motion.prev_pos = previous->position.data(); motion.prev_proj = previous->projection.data(); }
      fill_vs_constants(dc, vc, scale_, matrices, &motion);
      last_poses_.record(dc.identity, presentation, !skip_present_, matrices ? matrices->pos : dc.posMatrices, &vc.unjittered_projection[0][0]);
    } else {
      fill_vs_constants(dc, vc, scale_, matrices);
    }
    fill_ps_constants(dc, pc, scale_);
    const VkDeviceSize align = std::max<VkDeviceSize>(limits_.minUniformBufferOffsetAlignment, 16);
    const VkDeviceSize vs_at = ring_alloc(sizeof vc, align), ps_at = ring_alloc(sizeof pc, align);
    std::memcpy(f.ring.mapped + vs_at, &vc, sizeof vc); std::memcpy(f.ring.mapped + ps_at, &pc, sizeof pc);
    const VkDeviceSize index_at = ring_alloc(index_scratch_.size() * 4, 4);
    std::memcpy(f.ring.mapped + index_at, index_scratch_.data(), index_scratch_.size() * 4);
    // Pushed (push_descriptors_on_): the constants' place in the ring is in the write. Otherwise a set
    // from this frame's pool, with the places as dynamic offsets.
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!push_descriptors_on_) {
      VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      ai.descriptorPool = f.pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &set_layout_;
      if (vkAllocateDescriptorSets(device_, &ai, &set) != VK_SUCCESS) { if (++pool_exhausted_ <= 3) host::log("vulkan: more than 8192 draws in one frame; the rest were skipped"); return; }
    }
    const VkDescriptorType constants = push_descriptors_on_ ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    VkDescriptorBufferInfo vsb{f.ring.buffer, push_descriptors_on_ ? vs_at : 0, sizeof vc}, psb{f.ring.buffer, push_descriptors_on_ ? ps_at : 0, sizeof pc};
    VkWriteDescriptorSet writes[4]{};
    for (auto& wr : writes) wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, wr.dstSet = set;
    writes[0].dstBinding = kBindingVS; writes[0].descriptorCount = 1; writes[0].descriptorType = constants; writes[0].pBufferInfo = &vsb;
    writes[1].dstBinding = kBindingPS; writes[1].descriptorCount = 1; writes[1].descriptorType = constants; writes[1].pBufferInfo = &psb;
    writes[2].dstBinding = kBindingTex; writes[2].descriptorCount = 8; writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; writes[2].pImageInfo = images;
    writes[3].dstBinding = kBindingSamp; writes[3].descriptorCount = 8; writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; writes[3].pImageInfo = samplers;
    if (!push_descriptors_on_) vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);
    if (p != bound_pipeline_) { vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p); bound_pipeline_ = p; }
    if (push_descriptors_on_) {
      vkCmdPushDescriptorSetKHR(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 4, writes);
    } else {
      const uint32_t offsets[] = {(uint32_t)vs_at, (uint32_t)ps_at};
      vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 2, offsets);
    }
    // Melee's viewport is top-down like Direct3D's: a negative height gives the same mapping.
    VkViewport viewport{x, y + std::max(h, 1.0f), std::max(w, 1.0f), -std::max(h, 1.0f), znear, zfar};
    vkCmdSetViewport(f.cmd, 0, 1, &viewport);
    VkRect2D scissor{{l * scale_, t * scale_}, {uint32_t((r - l) * scale_), uint32_t((b - t) * scale_)}};
    vkCmdSetScissor(f.cmd, 0, 1, &scissor);
    const VkDeviceSize vb = vertex_base_;
    vkCmdBindVertexBuffers(f.cmd, 0, 1, &f.ring.buffer, &vb);
    vkCmdBindIndexBuffer(f.cmd, f.ring.buffer, index_at, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(f.cmd, (uint32_t)index_scratch_.size(), 1, 0, 0, 0);
  }

  void clear(const EfbCopy& c) {
    begin_efb();
    const uint32_t colour = look_clear_colour(lightweight_filter_.in_match, c.clear_color);
    VkClearAttachment att[2]{};
    att[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; att[0].colorAttachment = 0;
    att[0].clearValue.color = {{((colour >> 16) & 255) / 255.0f, ((colour >> 8) & 255) / 255.0f, (colour & 255) / 255.0f, (colour >> 24) / 255.0f}};
    att[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; att[1].clearValue.depthStencil = {float(1 - c.clear_z / 16777215.0), 0};
    const uint32_t x0 = std::min(c.src_x * scale_, width_), y0 = std::min(c.src_y * scale_, height_);
    const uint32_t w = std::min(c.src_w * scale_, width_ - x0), h = std::min(c.src_h * scale_, height_ - y0);
    if (!w || !h) return;
    VkClearRect rect{{{(int32_t)x0, (int32_t)y0}, {w, h}}, 0, 1};
    vkCmdClearAttachments(frames_[slot_].cmd, 2, att, 1, &rect);
  }

  void blit(VkCommandBuffer cmd, VkPipeline p, const Image& source, VkSampler s, const BlitConstants& constants) {
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = frames_[slot_].pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &blit_set_layout_;
    VkDescriptorSet set; check(vkAllocateDescriptorSets(device_, &ai, &set), "blit descriptors");
    VkDescriptorImageInfo image{VK_NULL_HANDLE, source.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, smp{s, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet w[2]{};
    w[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &image, nullptr, nullptr};
    w[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &smp, nullptr, nullptr};
    vkUpdateDescriptorSets(device_, 2, w, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blit_layout_, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, blit_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof constants, &constants);
    vkCmdDraw(cmd, 3, 1, 0, 0);
  }

  void copy(const EfbCopy& c) {
    if (!c.src_w || !c.src_h) return;
    const uint32_t w = std::max(1u, c.src_w / (c.half_scale ? 2 : 1)) * scale_, h = std::max(1u, c.src_h / (c.half_scale ? 2 : 1)) * scale_;
    Image& e = copies_[c.dest_addr & 0x3FFFFFFFu];
    if (!e.image || e.width != w || e.height != h) {
      retire(e);
      e = make_image(w, h, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    e.used = sequence_;
    Frame_& f = frames_[slot_];
    end_efb();
    Image& source = c.is_depth ? efb_depth_ : efb_color_;
    barrier(f.cmd, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, c.is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
    barrier(f.cmd, e, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = e.view; color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, {w, h}}; info.layerCount = 1; info.colorAttachmentCount = 1; info.pColorAttachments = &color;
    vkCmdBeginRendering(f.cmd, &info);
    VkViewport viewport{0, 0, float(w), float(h), 0, 1}; VkRect2D scissor{{0, 0}, {w, h}};
    vkCmdSetViewport(f.cmd, 0, 1, &viewport); vkCmdSetScissor(f.cmd, 0, 1, &scissor);
    BlitConstants k{};
    k.rect[0] = c.src_w / float(EFB_WIDTH); k.rect[1] = c.src_h / float(EFB_HEIGHT);
    k.rect[2] = c.src_x / float(EFB_WIDTH); k.rect[3] = c.src_y / float(EFB_HEIGHT);
    k.kind[0] = c.is_depth ? 2 : (c.efb_alpha ? 0 : 1);
    blit(f.cmd, copy_pipeline_, source, c.is_depth ? nearest_ : linear_, k);
    vkCmdEndRendering(f.cmd);
    barrier(f.cmd, e, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(f.cmd, source, c.is_depth ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            c.is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
  }

  void present(const EfbCopy& c, int test_fill) {
    Frame_& f = frames_[slot_];
#ifdef _WIN32
    fg_presenter_wait();   // the previous frame's second present (FSR frame generation) is out first
#endif
    // A V-Sync change, or the presentation loop's verdict on the display (tear_free_), asks for
    // another present mode: the swapchain is made again for it.
    if (present_synced_ != synced_wanted()) swapchain_dirty_ = true;
    if (swapchain_dirty_) {
      idle();
      create_swapchain();
      if (swapchain_dirty_) return;   // minimized, or exclusive fullscreen refused (made again plain next frame)
    }
    // With three swapchain images and one frame in line this never waits on the display; the time
    // is counted all the same (presentation_wait_seconds), so a driver that does queue here shows up.
    const double acquire_start = host::now_seconds();
    const VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &image_index_);
    acquire_wait_ = host::now_seconds() - acquire_start;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || exclusive_lost(r)) { swapchain_dirty_ = true; return; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) check(r, "acquire");
    if (r == VK_SUBOPTIMAL_KHR) swapchain_dirty_ = true;
    acquired_ = true;
    // As the Direct3D renderers: the widescreen setting also drives the Slippi code, and FoD
    // reflections and True widescreen follow the settings every frame (game changes, not drawing).
    if (opts_.widescreen != widescreen_sent_) { widescreen_sent_ = opts_.widescreen; slippi::request_widescreen(opts_.widescreen); }
    if (opts_.fod_reflections != fod_sent_) { fod_sent_ = opts_.fod_reflections; slippi::request_fod_reflections(opts_.fod_reflections); }
    set_true_widescreen(opts_.true_widescreen && !opts_.widescreen, true_widescreen_aspect(opts_, client_w_, client_h_));
    end_efb();
    barrier(f.cmd, efb_color_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(f.cmd, efb_depth_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);   // ambient occlusion reads it
    upscale(c, test_fill);
    capture_pending_ = false;
#ifdef _WIN32
    generate_frame();
    if (fsr_fg_generated_) {
      // AMD FSR frame generation: the generated picture (between the last frame and this one) is
      // this present's; the rendered frame is drawn into fg_real_ and presented half a frame later
      // (post_rendered_frame). Captures take the rendered frames unless MELEE_FSR_FG_SHOW_GENERATED.
      std::swap(upscale_out_, fg_out_);
      draw_window(c, test_fill, swap_images_[image_index_], swap_views_[image_index_], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, true, false, fg_show_generated_only());
      std::swap(upscale_out_, fg_out_);
      draw_window(c, test_fill, fg_real_.image, fg_real_.view, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, false, true, !fg_show_generated_only());
      fg_real_.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    } else
#endif
    draw_window(c, test_fill, swap_images_[image_index_], swap_views_[image_index_], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, true, true, true);
    barrier(f.cmd, efb_color_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    barrier(f.cmd, efb_depth_, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
  }

  // The window's picture into `target` (a swapchain image, or fg_real_): border art, the picture or a
  // shader preset, the settings panel when `ui`; GPU timers when `timers`, a capture when `capture`.
  void draw_window(const EfbCopy& c, int test_fill, VkImage target, VkImageView view, VkImageLayout final_layout, bool timers, bool ui, bool capture) {
    Frame_& f = frames_[slot_];
    raw_barrier(f.cmd, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const int w = (int)swap_extent_.width, h = (int)swap_extent_.height;
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = view; color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const float fill = test_fill > 0 ? 1.0f : 0.0f;
    color.clearValue.color = {{fill, fill, fill, 1}};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, swap_extent_}; info.layerCount = 1; info.colorAttachmentCount = 1; info.pColorAttachments = &color;
    if (timers && f.timers) { vkCmdWriteTimestamp(f.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, f.timers, 1); f.timer_mask |= 2; }
    // A shader preset (user_shader.h): the plain present pass is drawn into a texture the size of the
    // console's picture (640 columns by the lines sent to the display), and the preset's passes draw
    // it onto the window after the bars and border art. Presets are written for that picture.
    const bool shader_on = test_fill < 0 && prepare_shader(c);
    if (shader_on) draw_shader_source(c, w, h);
    vkCmdBeginRendering(f.cmd, &info);
    if (test_fill < 0) {
      // Border art fills the window first; the picture is drawn over it (as the Direct3D renderers).
      video_bg::set_border(opts_.border_art);
      bool art_reversed = false;
      if (present_plain_)
        if (const auto art = video_bg::border_frame(&art_reversed))
          if (Image* texture = update_video_image(art, video_bg::kBorderSlot)) present_border(*texture, *art, art_reversed, w, h);
      const double aspect = presented_aspect(opts_, w, h, frame_widenable_);
      int pw = w, ph = (int)std::lround(w / aspect);
      if (ph > h) { ph = h; pw = (int)std::lround(h * aspect); }
      const float vx = std::floor((w - pw) * 0.5f), vy = std::floor((h - ph) * 0.5f);
      VkViewport viewport{vx, vy, float(pw), float(ph), 0, 1};
      VkRect2D scissor{{0, 0}, swap_extent_};
      vkCmdSetViewport(f.cmd, 0, 1, &viewport); vkCmdSetScissor(f.cmd, 0, 1, &scissor);
      if (shader_on) {}   // drawn by the preset below, outside this rendering
      else if (present_plain_) present_picture(c, w, h);
      else {
        BlitConstants k{};
        const uint32_t sw = std::max(c.src_w, 1u), sh = std::max(c.src_h, 1u);
        k.rect[0] = sw / float(EFB_WIDTH); k.rect[1] = sh / float(EFB_HEIGHT);
        k.rect[2] = c.src_x / float(EFB_WIDTH); k.rect[3] = c.src_y / float(EFB_HEIGHT);
        k.box[0] = k.rect[0] / float(pw); k.box[1] = k.rect[1] / float(ph);
        k.kind[0] = 3;
        k.kind[1] = std::clamp((int)std::ceil(float(sw * scale_) / float(pw)), 1, 8);
        k.kind[2] = std::clamp((int)std::ceil(float(sh * scale_) / float(ph)), 1, 8);
        blit(f.cmd, present_pipeline_, efb_color_, linear_, k);
      }
    }
    if (shader_on) {
      vkCmdEndRendering(f.cmd);
      const double aspect = presented_aspect(opts_, w, h, frame_widenable_);
      float vw = (float)w, vh = (float)(w / aspect);
      if (vh > h) { vh = (float)h; vw = (float)(h * aspect); }
      const user_shader::Target where{std::floor((w - vw) * 0.5f), std::floor((h - vh) * 0.5f), (uint32_t)std::max(vw, 1.0f), (uint32_t)std::max(vh, 1.0f)};
      user_shader::vk_draw(f.cmd, (uint64_t)(uintptr_t)shader_source_.image, (int)VK_FORMAT_R8G8B8A8_UNORM, shader_source_.width, shader_source_.height,
                           (uint64_t)(uintptr_t)target, (int)swap_format_, (uint32_t)w, (uint32_t)h, where, presented_);
      color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;   // the settings panel goes over what the preset drew
      vkCmdBeginRendering(f.cmd, &info);
    }
#ifdef GX_PC_SETTINGS
    if (ui && settings_ui_ready()) {
      VkViewport viewport{0, 0, float(w), float(h), 0, 1};
      vkCmdSetViewport(f.cmd, 0, 1, &viewport);
      ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), f.cmd);
    }
#endif
    vkCmdEndRendering(f.cmd);
    if (timers && f.timers) { vkCmdWriteTimestamp(f.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f.timers, 2); f.timer_mask |= 4; }
    const bool captured = capture && test_fill < 0 && wants_capture();
    if (captured) { capture_pending_ = true; record_capture(target); }
    raw_barrier(f.cmd, target, captured ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, final_layout);
  }

#ifdef _WIN32
  // ---- AMD FSR frame generation (fsr::vk_fg_generate), as Direct3D 12's (gx_d3d12.cpp) ----
  // No AMD swap chain: each rendered frame in a match makes one generated picture, presented first,
  // and the rendered frame follows half a rendered frame later from the presenter thread, so the
  // presentation loop's pacing stays the game's. The loop renders at half the rate (the multiplier).
  static bool fg_show_generated_only() {
    // MELEE_FSR_FG_SHOW_GENERATED=1 (tests): only the generated pictures are shown and captured.
    static const bool on = [] { const char* v = std::getenv("MELEE_FSR_FG_SHOW_GENERATED"); return v && *v == '1'; }();
    return on;
  }
  bool fsr_fg_wanted() const {
    return opts_.frame_generation_mode > 0 && upscaling_ && upscale_out_.image && fsr::vk_fg_available() && !fsr_fg_failed_;
  }
  void update_fsr_frame_generation(bool ui_drawn) {
    if (!fsr_fg_wanted() || !swap_extent_.width || !swap_extent_.height) {
      if (fsr_fg_running_ && !fsr_fg_wanted()) release_fsr_frame_generation();
      fsr_fg_on_ = false;
      fg_multiplier_.store(1, std::memory_order_relaxed);
      return;
    }
    const bool sized = fg_out_.image && fg_real_.image && fsr::vk_fg_ready() && fg_out_.width == upscale_w_ && fg_out_.height == upscale_h_ &&
                       fg_real_.width == swap_extent_.width && fg_real_.height == swap_extent_.height && fg_real_.format == swap_format_;
    if (!sized) {
      idle();
      destroy(fg_out_); destroy(fg_real_);
      bool made = true;
      try {
        fg_out_ = make_image(upscale_w_, upscale_h_, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        fg_real_ = make_image(swap_extent_.width, swap_extent_.height, 1, swap_format_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!fg_pool_) {
          VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
          pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex = queue_family_;
          check(vkCreateCommandPool(device_, &pool, nullptr, &fg_pool_), "frame generation pool");
          VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
          ai.commandPool = fg_pool_; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = kFrames;
          check(vkAllocateCommandBuffers(device_, &ai, fg_cmds_.data()), "frame generation commands");
          for (int i = 0; i < kFrames; ++i) {
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            check(vkCreateFence(device_, &fence, nullptr, &fg_fences_[i]), "frame generation fence");
            VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            check(vkCreateSemaphore(device_, &sem, nullptr, &fg_acquired_[i]), "frame generation semaphore");
          }
        }
      } catch (const std::exception& e) {
        host::log("fsr (vulkan): frame generation images could not be made (%s)", e.what());
        made = false;
      }
      if (!made || !fsr::vk_fg_create(upscale_w_, upscale_h_, width_, height_)) {
        host::log("fsr (vulkan): frame generation could not start; it stays off this session");
        fsr_fg_failed_ = true;
        release_fsr_frame_generation();
        return;
      }
      fsr_fg_prev_frame_ = ~0ull; fsr_fg_interval_ = 0; fsr_fg_last_present_ = 0;
      if (!fsr_fg_running_)
        host::log("fsr (vulkan): frame generation 2x (%s) for a %ux%u picture in a %ux%u window",
                  fsr::vk_fg_provider().empty() ? "AMD FSR" : fsr::vk_fg_provider().c_str(), upscale_w_, upscale_h_, swap_extent_.width, swap_extent_.height);
      fsr_fg_running_ = true;
    }
    // Menus are shown as rendered (the upscaler runs in matches only); so are frames with the
    // settings panel or a shader preset over them.
    fsr_fg_on_ = in_match_ && !ui_drawn && opts_.shader_preset.empty();
    fg_multiplier_.store(fsr_fg_on_ ? 2 : 1, std::memory_order_relaxed);
  }
  void release_fsr_frame_generation() {
    fg_presenter_wait(); fg_presenter_stop();
    vkDeviceWaitIdle(device_);
    fsr::vk_fg_destroy();
    destroy(fg_out_); destroy(fg_real_);
    if (fsr_fg_running_) host::log("fsr (vulkan): frame generation off");
    fsr_fg_running_ = fsr_fg_on_ = fsr_fg_generated_ = false;
    fsr_fg_prev_frame_ = ~0ull;
    fg_multiplier_.store(1, std::memory_order_relaxed);
  }
  // After this frame's upscale: the picture between the previous upscaled picture and this one.
  void generate_frame() {
    fsr_fg_generated_ = false;
    if (!upscaled_now_ || !fsr_fg_on_ || !fg_out_.image || !fg_real_.image) return;
    if (fg_real_.width != swap_extent_.width || fg_real_.height != swap_extent_.height || fg_real_.format != swap_format_) return;   // a new swapchain: sized next frame
    Frame_& f = frames_[slot_];
    barrier(f.cmd, fg_out_, VK_IMAGE_LAYOUT_GENERAL);
    auto image = [](const Image& i, uint32_t usage) { return fsr::VulkanImage{(uint64_t)(uintptr_t)i.image, (int)i.format, i.width, i.height, usage}; };
    fsr::VulkanFrameInputs in{};
    in.command_buffer = f.cmd;
    in.depth = image(efb_depth_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    in.mvec = image(mvec_img_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    in.color = image(upscale_out_, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    in.out = image(fg_out_, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    in.render_w = fsr_in_w_; in.render_h = fsr_in_h_;
    in.jitter_x = fsr_jitter_x_; in.jitter_y = fsr_jitter_y_; in.frame_ms = fsr_frame_ms_;
    in.reset = fsr_fg_prev_frame_ + 1 != (uint64_t)presented_;   // not the frame right after the last generating one
    fsr_fg_generated_ = fsr::vk_fg_generate(in);
    fsr_fg_prev_frame_ = presented_;
    barrier(f.cmd, fg_out_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  // The generated picture is presented: the rendered one follows halfway to the next (the cadence
  // measured at these presents, at most 50 ms apart); with V-Sync, half a refresh early, so it is
  // shown at the blank nearest halfway.
  void post_rendered_frame() {
    const double shown = host::now_seconds();
    if (fsr_fg_last_present_ > 0) {
      const double gap = shown - fsr_fg_last_present_;
      if (gap > 0 && gap < 0.1) fsr_fg_interval_ = fsr_fg_interval_ > 0 ? fsr_fg_interval_ * 0.9 + gap * 0.1 : gap;
    }
    fsr_fg_last_present_ = shown;
    const double halfway = shown + 0.5 * std::min(fsr_fg_interval_, 0.05);
    const double hz = vrr_nominal_hz_ > 0 ? vrr_nominal_hz_ : 60.0;
    fg_presenter_post(present_mode_ == VK_PRESENT_MODE_FIFO_KHR ? halfway - 0.5 / std::max(hz, 30.0) : halfway, slot_);
    ++fsr_fg_shown_;
    if (fsr_fg_shown_ % 600 == 0)
      host::log("fsr (vulkan): frame generation, %llu generated frames shown, rendered frames %.2f ms apart",
                (unsigned long long)fsr_fg_shown_, fsr_fg_interval_ * 1000.0);
  }
  // The presenter thread: the only queue and swapchain user between fg_presenter_post and the end of
  // its present; the game thread waits for it (fg_presenter_wait) before using either.
  void fg_presenter_wait() {
    if (!fg_presenter_.joinable()) return;
    std::unique_lock<std::mutex> lock(fg_mutex_);
    fg_cv_.wait(lock, [&] { return !fg_pending_; });
    if (fg_dirty_) { fg_dirty_ = false; swapchain_dirty_ = true; }
  }
  void fg_presenter_post(double due, int slot) {
    if (!fg_presenter_.joinable()) { fg_quit_ = false; fg_presenter_ = std::thread([this] { fg_presenter_run(); }); }
    { std::lock_guard<std::mutex> lock(fg_mutex_); fg_due_ = due; fg_slot_ = slot; fg_pending_ = true; }
    fg_cv_.notify_all();
  }
  void fg_presenter_stop() {
    if (!fg_presenter_.joinable()) return;
    { std::lock_guard<std::mutex> lock(fg_mutex_); fg_quit_ = true; }
    fg_cv_.notify_all();
    fg_presenter_.join();
  }
  void fg_presenter_run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    for (;;) {
      double due; int slot;
      {
        std::unique_lock<std::mutex> lock(fg_mutex_);
        fg_cv_.wait(lock, [&] { return fg_pending_ || fg_quit_; });
        if (!fg_pending_) break;   // asked to stop with nothing left to present
        due = fg_due_; slot = fg_slot_;
      }
      const bool dirty = !present_rendered(slot, due, timer);
      {
        std::lock_guard<std::mutex> lock(fg_mutex_);
        if (dirty) fg_dirty_ = true;
        fg_pending_ = false;
      }
      fg_cv_.notify_all();
    }
    if (timer) CloseHandle(timer);
  }
  // fg_real_ into the next swapchain image, presented at `due`. False: the swapchain needs making again.
  bool present_rendered(int slot, double due, HANDLE timer) {
    uint32_t index = 0;
    VkResult r = vkAcquireNextImageKHR(device_, swapchain_, 100000000ull, fg_acquired_[slot], VK_NULL_HANDLE, &index);
    if (r == VK_TIMEOUT || r == VK_NOT_READY) return true;   // no image in time: this rendered frame is not shown
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;
    vkWaitForFences(device_, 1, &fg_fences_[slot], VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &fg_fences_[slot]);
    const VkCommandBuffer cmd = fg_cmds_[slot];
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) return false;
    raw_barrier(cmd, swap_images_[index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; region.dstSubresource = region.srcSubresource;
    region.extent = {fg_real_.width, fg_real_.height, 1};
    vkCmdCopyImage(cmd, fg_real_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swap_images_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    raw_barrier(cmd, swap_images_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) return false;
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &fg_acquired_[slot]; submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &swap_done_[index];
    if (vkQueueSubmit(queue_, 1, &submit, fg_fences_[slot]) != VK_SUCCESS) return false;
    for (;;) {
      const double remaining = due - host::now_seconds();
      if (remaining <= 0) break;
      if (timer && remaining > 0.0004) {
        LARGE_INTEGER t; t.QuadPart = -(LONGLONG)((remaining - 0.0002) * 1e7);
        if (SetWaitableTimer(timer, &t, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
        else SwitchToThread();
      } else YieldProcessor();
    }
    VkPresentInfoKHR p{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    p.waitSemaphoreCount = 1; p.pWaitSemaphores = &swap_done_[index];
    p.swapchainCount = 1; p.pSwapchains = &swapchain_; p.pImageIndices = &index;
    r = vkQueuePresentKHR(queue_, &p);
    return r == VK_SUCCESS;   // anything else (out of date, suboptimal, exclusive fullscreen lost): made again by the game thread
  }
#endif

  // ---- AMD FSR (Windows: AMD's Vulkan DLLs, gx_fsr_vk.cpp) ----
  // Follows the Upscaling setting each frame, as configure_dlss does on Direct3D 12: the render scale is
  // the smallest EFB multiple whose 640x480 picture reaches the mode's render size for the picture's
  // size in the window; Native AA keeps the player's own scale and anti-aliases the EFB in place.
  void configure_upscaler() {
#ifdef _WIN32
    if (!fsr_tried_ && fsr::is_fsr_mode(opts_.dlss_mode)) {
      fsr_tried_ = true;
      wchar_t module[MAX_PATH]{};
      const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
      if (length > 0 && length < MAX_PATH) {
        const std::wstring dir = std::filesystem::path(module).parent_path().wstring();
        const fsr::VulkanDevice d{device_, physical_, (void*)vkGetDeviceProcAddr};
        fsr::vk_init(dir, d);
      }
    }
    const int mode = opts_.dlss_mode;
    const bool want = fsr::is_fsr_mode(mode) && fsr::vk_available();
    if (!want) {
      if (upscaling_) {
        idle();
        upscaling_ = false; forced_scale_ = 0; upscaled_now_ = false;
        retire(upscale_out_);
        rebuild_efb();
        host::log("fsr (vulkan): off (native rendering at EFB x%d)", scale_);
      }
      return;
    }
    const float aspect = presented_aspect(opts_, client_w_, client_h_, true);
    int out_w = client_w_, out_h = (int)std::lround(client_w_ / aspect);
    if (out_h > client_h_) { out_h = client_h_; out_w = (int)std::lround(client_h_ * aspect); }
    const bool in_place = fsr::is_in_place(mode);
    int scale;
    if (in_place) {
      const int saved = forced_scale_;
      forced_scale_ = 0; scale = pick_scale(); forced_scale_ = saved;
      out_w = EFB_WIDTH * scale; out_h = EFB_HEIGHT * scale;
    } else {
      uint32_t rw = 0, rh = 0;
      fsr::render_size_for(mode, (uint32_t)out_w, (uint32_t)out_h, &rw, &rh);
      scale = std::max(1, (int)std::ceil(std::max(rw / 640.0, rh / 480.0)));
      while (scale > 1 && (640 * scale > out_w || 480 * scale > out_h)) --scale;
      if (640 * scale > out_w || 480 * scale > out_h) {
        if (!upscale_logged_) { upscale_logged_ = true; host::log("fsr (vulkan): the window is smaller than the native picture; staying native"); }
        return;
      }
    }
    if (upscaling_ && upscale_mode_ == mode && (int)upscale_w_ == out_w && (int)upscale_h_ == out_h && forced_scale_ == scale) return;
    idle();
    if (!fsr::vk_set_options(mode, (uint32_t)out_w, (uint32_t)out_h)) { opts_.dlss_mode = 0; return; }
    retire(upscale_out_);
    try {
      upscale_out_ = make_image((uint32_t)out_w, (uint32_t)out_h, 1, VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    } catch (const std::exception& e) {
      host::log("fsr (vulkan): the output image could not be made (%s); staying native", e.what());
      upscale_out_ = Image{}; opts_.dlss_mode = 0; return;
    }
    upscaling_ = true; upscale_in_place_ = in_place; upscale_mode_ = mode; upscale_w_ = (uint32_t)out_w; upscale_h_ = (uint32_t)out_h;
    forced_scale_ = scale; upscale_reset_ = true; last_poses_.clear();
    rebuild_efb();   // the render scale, single-sample targets, and the motion and HUD targets
    host::log("fsr (vulkan): %s, render %dx%d (EFB x%d) -> output %dx%d", fsr::mode_name(mode),
              in_place ? out_w : 640 * scale, in_place ? out_h : 480 * scale, scale, out_w, out_h);
#endif
  }
  // In a match the upscaler rebuilds the picture from the jittered EFB, depth and motion; menus are
  // presented from the EFB as rendered (as on Direct3D 12).
  void upscale(const EfbCopy& c, int test_fill) {
    upscaled_now_ = false;
#ifdef _WIN32
    if (!upscaling_ || !in_match_ || test_fill >= 0 || !upscale_out_.image || !mvec_img_.image) return;
    if (!upscale_in_place_ && (c.src_x || c.src_y)) return;   // FSR reads from the image's origin
    Frame_& f = frames_[slot_];
    barrier(f.cmd, mvec_img_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(f.cmd, hud_img_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(f.cmd, upscale_out_, VK_IMAGE_LAYOUT_GENERAL);
    const double now = host::now_seconds();
    const float frame_ms = fsr_last_time_ > 0 ? (float)((now - fsr_last_time_) * 1000.0) : 0.0f;
    fsr_last_time_ = now;
    // MELEE_FSR_JITTER_SIGN (tests): the jitter convention, as on Direct3D 12 (gx_d3d12.cpp).
    static const float jitter_sign = [] { const char* v = std::getenv("MELEE_FSR_JITTER_SIGN"); return v && *v ? (float)std::atof(v) : -1.0f; }();
    auto image = [](const Image& i, uint32_t usage) { return fsr::VulkanImage{(uint64_t)(uintptr_t)i.image, (int)i.format, i.width, i.height, usage}; };
    fsr::VulkanInputs in{};
    in.command_buffer = f.cmd;
    in.color = image(efb_color_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    in.depth = image(efb_depth_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    in.mvec = image(mvec_img_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    in.out = image(upscale_out_, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    in.in_w = upscale_in_place_ ? width_ : std::min<uint32_t>(c.src_w * scale_, width_);
    in.in_h = upscale_in_place_ ? height_ : std::min<uint32_t>(c.src_h * scale_, height_);
    in.jitter_x = jitter_sign * jitter_x_ * opts_.dlss_jitter_sign;
    in.jitter_y = jitter_sign * jitter_y_ * opts_.dlss_jitter_sign;
    in.frame_ms = frame_ms;
    in.reset = upscale_reset_;
    upscale_reset_ = false;
    upscaled_now_ = fsr::vk_evaluate(in);
    fsr_in_w_ = in.in_w; fsr_in_h_ = in.in_h; fsr_jitter_x_ = in.jitter_x; fsr_jitter_y_ = in.jitter_y; fsr_frame_ms_ = frame_ms;
    barrier(f.cmd, upscale_out_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#else
    (void)c; (void)test_fill;
#endif
  }

  // ---- Shader presets (user_shader.h) ----
  // The preset's filter chain for the chosen preset (made, swapped or let go by user_shader), and the
  // source image at the picture's size. False: no preset, or it cannot run (its status says why).
  bool prepare_shader(const EfbCopy& c) {
    const user_shader::VulkanDevice device{instance_, physical_, device_, queue_, (void*)vkGetInstanceProcAddr};
    const bool on = user_shader::vk_prepare(device, opts_.shader_preset,
                                            [](void* self) { static_cast<VulkanBackend*>(self)->idle(); }, this);
    if (!on || !present_plain_rgba_) return false;
    const uint32_t sw = std::max(c.src_w, 1u), sh = (uint32_t)std::max(std::lround((double)c.src_h * c.y_scale), 1L);
    if (!shader_source_.image || shader_source_.width != sw || shader_source_.height != sh) {
      retire(shader_source_);
      try {
        shader_source_ = make_image(sw, sh, 1, VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                    VK_IMAGE_ASPECT_COLOR_BIT);
      } catch (const std::exception& e) {
        host::log("shaders: the picture texture could not be made (%s); the preset is off this frame", e.what());
        shader_source_ = Image{};
        return false;
      }
    }
    return true;
  }
  void draw_shader_source(const EfbCopy& c, int w, int h) {
    Frame_& f = frames_[slot_];
    barrier(f.cmd, shader_source_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    a.imageView = shader_source_.view; a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE; a.clearValue.color = {{0, 0, 0, 1}};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {shader_source_.width, shader_source_.height}}; ri.layerCount = 1; ri.colorAttachmentCount = 1; ri.pColorAttachments = &a;
    vkCmdBeginRendering(f.cmd, &ri);
    const VkRect2D sc{{0, 0}, {shader_source_.width, shader_source_.height}};
    vkCmdSetScissor(f.cmd, 0, 1, &sc);
    const VkExtent2D into{shader_source_.width, shader_source_.height};
    present_picture(c, w, h, &into);
    vkCmdEndRendering(f.cmd);
    barrier(f.cmd, shader_source_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  // ---- Menu video backgrounds and border art (video_background.h) ----
  // The slot's image holds the newest published frame (BGRA, as the decoder writes it); a new size
  // makes a new image (the old one goes when this frame slot comes round again).
  Image* update_video_image(const std::shared_ptr<const video_bg::Frame>& frame, int slot) {
    if (!frame || slot < 0 || slot >= video_bg::kVideoSlots || frame->bgra.empty() || !frame->width || !frame->height) return nullptr;
    if ((uint64_t)frame->width * frame->height * 4 > frame->bgra.size()) return nullptr;
    Image& img = video_images_[slot];
    if (!img.image || img.width != frame->width || img.height != frame->height) {
      retire(img);
      try {
        img = make_image(frame->width, frame->height, 1, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
      } catch (const std::exception& e) {
        video_bg::report_backend_failure(slot, std::string("Vulkan video texture creation failed: ") + e.what());
        img = Image{};
        return nullptr;
      }
      video_frame_[slot] = nullptr;
    }
    // A new frame object or a new serial: the decoder published another picture.
    if (video_frame_[slot] != frame.get() || video_serial_[slot] != frame->serial) {
      upload_level(img, 0, frame->width, frame->height, frame->bgra.data());
      finish_upload(img);
      video_frame_[slot] = frame.get();
      video_serial_[slot] = frame->serial;
      video_keep_[slot] = frame;   // keeps the address from being reused by a later frame
    }
    img.used = sequence_;
    return &img;
  }
  // The blit pipeline for drawing into the EFB itself (its colour and depth formats and sample count).
  VkPipeline efb_blit_pipeline() {
    if (efb_blit_ && efb_blit_samples_ == samples_ && efb_blit_upscale_ == upscaling_) return efb_blit_;
    if (efb_blit_) { idle(); vkDestroyPipeline(device_, efb_blit_, nullptr); efb_blit_ = VK_NULL_HANDLE; }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, blit_vs_, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, blit_ps_, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask = 0xF;
    try {
      efb_blit_ = build_pipeline(stages, vi, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, blend,
                                 false, false, VK_COMPARE_OP_ALWAYS, blit_layout_, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT, samples_,
                                 upscaling_, 0, 0);
      efb_blit_samples_ = samples_; efb_blit_upscale_ = upscaling_;
    } catch (const std::exception& e) {
      host::log("%s (video background layer)", e.what());
      efb_blit_ = VK_NULL_HANDLE;
    }
    return efb_blit_;
  }
  // The decoded menu frame over the screen region of the EFB. The decoder's rows run the other way
  // from this blit's V, so V is flipped, as on Direct3D (the same constants: both blits put the
  // first vertex row at the top).
  void draw_video_background(const std::shared_ptr<const video_bg::Frame>& frame, int slot, const EfbCopy& screen) {
    Image* video = update_video_image(frame, slot);
    if (!video) return;
    VkPipeline p = efb_blit_pipeline();
    if (!p) return;
    begin_efb();
    Frame_& f = frames_[slot_];
    VkViewport vp{float(screen.src_x * scale_), float(screen.src_y * scale_), float(screen.src_w * scale_), float(screen.src_h * scale_), 0, 1};
    VkRect2D sc{{int32_t(screen.src_x * scale_), int32_t(screen.src_y * scale_)}, {screen.src_w * scale_, screen.src_h * scale_}};
    vkCmdSetViewport(f.cmd, 0, 1, &vp);
    vkCmdSetScissor(f.cmd, 0, 1, &sc);
    BlitConstants k{};
    k.rect[0] = 1; k.rect[1] = -1; k.rect[2] = 0; k.rect[3] = 1;
    k.kind[0] = 1;   // opaque: the backdrop is the bottom layer
    blit(f.cmd, p, *video, linear_, k);
    bound_pipeline_ = VK_NULL_HANDLE;   // the next game draw binds its own
  }
  // Border art across the window through the shared present shader (its art_frame: the picture's
  // rectangle casts a soft shadow on the art), with the Direct3D renderers' constants.
  void present_border(const Image& texture, const video_bg::Frame& art, bool reversed, int w, int h) {
    Frame_& f = frames_[slot_];
    const float window = (float)w / std::max((float)h, 1.0f), shape = (float)art.width / std::max((float)art.height, 1.0f);
    const float sx = shape > window ? window / shape : 1.0f, sy = shape > window ? 1.0f : shape / window;
    const float aspect = presented_aspect(opts_, w, h, frame_widenable_);
    float pw = (float)w, ph = pw / aspect;
    if (ph > (float)h) { ph = (float)h; pw = ph * aspect; }
    const float px = ((float)w - pw) * 0.5f, py = ((float)h - ph) * 0.5f;
    const float k[28] = {sx, reversed ? -sy : sy, (1.0f - sx) * 0.5f, reversed ? 1.0f - (1.0f - sy) * 0.5f : (1.0f - sy) * 0.5f,
                         1.0f / art.width, 1.0f / art.height, 0.0f, 0.0f,
                         1.0f, 1.0f, 0.0f, 0.0f,
                         1.0f, 1.0f, 0.0f, 0.0f,
                         1.0f, 1.0f, 1.0f, 0.0f,
                         px, py, px + pw, py + ph,
                         std::clamp(std::max(px, py) * 0.6f, 12.0f, 160.0f), 0.0f, 0.0f, opts_.border_edge ? 1.0f : 0.0f};
    VkViewport vp{0, 0, (float)w, (float)h, 0, 1};
    VkRect2D sc{{0, 0}, swap_extent_};
    vkCmdSetViewport(f.cmd, 0, 1, &vp);
    vkCmdSetScissor(f.cmd, 0, 1, &sc);
    const VkDeviceSize align = std::max<VkDeviceSize>(limits_.minUniformBufferOffsetAlignment, 16);
    const VkDeviceSize at = ring_alloc(sizeof k, align);
    std::memcpy(f.ring.mapped + at, k, sizeof k);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = f.pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &present_set_layout_;
    VkDescriptorSet set; check(vkAllocateDescriptorSets(device_, &ai, &set), "border descriptors");
    VkDescriptorBufferInfo ub{f.ring.buffer, 0, sizeof k};
    VkDescriptorImageInfo img[4] = {{VK_NULL_HANDLE, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                    {VK_NULL_HANDLE, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                    {VK_NULL_HANDLE, white_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                    {VK_NULL_HANDLE, efb_depth_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkDescriptorImageInfo smp{linear_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet wr[6]{};
    for (auto& x : wr) x.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, x.dstSet = set, x.descriptorCount = 1;
    wr[0].dstBinding = 0; wr[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; wr[0].pBufferInfo = &ub;
    for (uint32_t i = 0; i < 4; ++i) { wr[1 + i].dstBinding = 1 + i; wr[1 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; wr[1 + i].pImageInfo = &img[i]; }
    wr[5].dstBinding = 5; wr[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; wr[5].pImageInfo = &smp;
    vkUpdateDescriptorSets(device_, 6, wr, 0, nullptr);
    vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, present_plain_);
    const uint32_t offset = (uint32_t)at;
    vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, present_layout_, 0, 1, &set, 1, &offset);
    vkCmdDraw(f.cmd, 3, 1, 0, 0);
  }

  // ---- The shared present pass: averaging box, sharpening, colour sliders, ambient occlusion and
  // the CRT model, the same shader and constants as the Direct3D 12 renderer (present_shader.h). ----
  void create_present_pass() {
    try {
      const std::string plain = kPresentShader;
      const std::string crt = plain + kPresentCrtTap + kCrtShader + kPresentCrtMain;
      present_vs_ = module(spirv_for(plain, true, "VS", true));
      present_ps_ = module(spirv_for(plain, false, "PS", true));
      present_crt_ps_ = module(spirv_for(crt, false, "PS_CRT", true));
    } catch (const std::exception& e) {
      host::log("vulkan: the present shader did not compile (%s); plain present only", e.what());
      return;
    }
    VkDescriptorSetLayoutBinding b[6]{};
    b[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    for (uint32_t i = 1; i <= 4; ++i) b[i] = {i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[5] = {5, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 6; li.pBindings = b;
    check(vkCreateDescriptorSetLayout(device_, &li, nullptr, &present_set_layout_), "present layout");
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1; pl.pSetLayouts = &present_set_layout_;
    check(vkCreatePipelineLayout(device_, &pl, nullptr, &present_layout_), "present pipeline layout");
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask = 0xF;
    for (int crt = 0; crt < 2; ++crt) {
      VkPipelineShaderStageCreateInfo st[2]{};
      st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, present_vs_, "VS", nullptr};
      st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, crt ? present_crt_ps_ : present_ps_,
               crt ? "PS_CRT" : "PS", nullptr};
      (crt ? present_crt_ : present_plain_) = build_pipeline(st, vi, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
                                                             blend, false, false, VK_COMPARE_OP_ALWAYS, present_layout_, swap_format_, VK_FORMAT_UNDEFINED);
      if (!crt)   // the shader preset's source image
        present_plain_rgba_ = build_pipeline(st, vi, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
                                             blend, false, false, VK_COMPARE_OP_ALWAYS, present_layout_, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED);
    }
  }
  // `into`: drawn into the shader preset's source image instead (its own size, no CRT model: a preset
  // takes the CRT model's place, as on Direct3D 12).
  void present_picture(const EfbCopy& c, int w, int h, const VkExtent2D* into = nullptr) {
    Frame_& f = frames_[slot_];
    const float src_h_lines = (float)c.src_h * c.y_scale;
    const float aspect = presented_aspect(opts_, w, h, frame_widenable_);
    float vw = (float)w, vh = (float)w / aspect;
    if (vh > h) { vh = (float)h; vw = h * aspect; }
    if (into) { vw = (float)into->width; vh = (float)into->height; }
    const bool crt_on = opts_.crt_filter != 0 && !into;
    const CrtLook crt = crt_look(crt_on ? opts_.crt_filter : 0, opts_.crt_curve);
    CrtFit fit{vh, 0.0f, src_h_lines};
    if (crt_on) fit = crt_fit(vh, src_h_lines);
    VkViewport vp{into ? 0.0f : (w - vw) * 0.5f, into ? 0.0f : (h - vh) * 0.5f, vw, vh, 0, 1};
    if (crt_on) { vp.x = std::floor(vp.x); vp.y = std::floor(vp.y); }
    vkCmdSetViewport(f.cmd, 0, 1, &vp);
    // Upscaled: the upscaler's image is the picture region alone (or, in place, the whole EFB), and pixels a
    // flat 2D draw wrote this frame (HUD, text, tags) come from this frame's own render (box.z), mapped
    // from output uv to EFB uv (hudr), as the Direct3D 12 present pass does.
    const bool up = upscaled_now_ && upscale_out_.image;
    const bool fills = up && !upscale_in_place_;
    const Image& source = up ? upscale_out_ : efb_color_;
    float k[28] = {fills ? 1.0f : (float)c.src_w / EFB_WIDTH, fills ? 1.0f : (float)c.src_h / EFB_HEIGHT,
                   fills ? 0.0f : (float)c.src_x / EFB_WIDTH, fills ? 0.0f : (float)c.src_y / EFB_HEIGHT,
                   1.0f / std::max((float)source.width, 1.0f), 1.0f / std::max((float)source.height, 1.0f), std::clamp(opts_.sharpness, 0.0f, 1.0f), 0.0f,
                   1.0f, 1.0f, up ? 1.0f : 0.0f, 0.0f,
                   fills ? (float)c.src_w / EFB_WIDTH : 1.0f, fills ? (float)c.src_h / EFB_HEIGHT : 1.0f,
                   fills ? (float)c.src_x / EFB_WIDTH : 0.0f, fills ? (float)c.src_y / EFB_HEIGHT : 0.0f,
                   opts_.brightness, opts_.contrast, opts_.vibrance, opts_.screen_space_ao,
                   crt.scan, crt.mask, crt.curve, std::max(fit.lines, 1.0f),
                   (float)c.src_w, vh / std::max(fit.lines, 1.0f), crt.halation, fit.shift};
    k[8] = (float)std::clamp((int)std::lround((fills ? (double)source.width : (double)c.src_w * scale_) / std::max(vw, 1.0f)), 1, 4);
    k[9] = (float)std::clamp((int)std::lround((fills ? (double)source.height : (double)c.src_h * scale_) / std::max(vh, 1.0f)), 1, 4);
    const VkDeviceSize align = std::max<VkDeviceSize>(limits_.minUniformBufferOffsetAlignment, 16);
    const VkDeviceSize at = ring_alloc(sizeof k, align);
    std::memcpy(f.ring.mapped + at, k, sizeof k);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = f.pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &present_set_layout_;
    VkDescriptorSet set; check(vkAllocateDescriptorSets(device_, &ai, &set), "present descriptors");
    VkDescriptorBufferInfo ub{f.ring.buffer, 0, sizeof k};
    VkDescriptorImageInfo img[4] = {{VK_NULL_HANDLE, source.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                    {VK_NULL_HANDLE, efb_color_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                    {VK_NULL_HANDLE, up ? hud_img_.view : white_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                    {VK_NULL_HANDLE, efb_depth_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkDescriptorImageInfo smp{linear_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet wr[6]{};
    for (auto& x : wr) x.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, x.dstSet = set, x.descriptorCount = 1;
    wr[0].dstBinding = 0; wr[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; wr[0].pBufferInfo = &ub;
    for (uint32_t i = 0; i < 4; ++i) { wr[1 + i].dstBinding = 1 + i; wr[1 + i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; wr[1 + i].pImageInfo = &img[i]; }
    wr[5].dstBinding = 5; wr[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; wr[5].pImageInfo = &smp;
    vkUpdateDescriptorSets(device_, 6, wr, 0, nullptr);
    vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, crt_on ? present_crt_ : into ? present_plain_rgba_ : present_plain_);
    const uint32_t offset = (uint32_t)at;
    vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, present_layout_, 0, 1, &set, 1, &offset);
    vkCmdDraw(f.cmd, 3, 1, 0, 0);
  }

  // ---- Captures (--capture-frame and friends, for hidden checks) ----
  bool wants_capture() {
    const RenderOptions& o = opts_;
    if (o.capture_path.empty()) return false;
    const uint32_t n = presented_ + 1;
    if (o.capture_sim_frame && !capture_frame_ && sequence_ >= o.capture_sim_frame) capture_frame_ = n;
    const uint32_t at = capture_frame_ ? capture_frame_ : o.capture_frame;
    const bool burst = o.capture_burst && at && n >= at && n < at + o.capture_burst;
    if (n == at && !burst) { capture_file_ = o.capture_path; return true; }
    if (burst || (o.capture_every && n % o.capture_every == 0)) {
      const std::string& base = o.capture_path;
      char suffix[48]; std::snprintf(suffix, sizeof suffix, "_%05u.ppm", n);
      capture_file_ = base.substr(0, base.size() > 4 && base.compare(base.size() - 4, 4, ".ppm") == 0 ? base.size() - 4 : base.size()) + suffix;
      return true;
    }
    return false;
  }
  void record_capture(VkImage target) {
    Frame_& f = frames_[slot_];
    const VkDeviceSize bytes = VkDeviceSize(swap_extent_.width) * swap_extent_.height * 4;
    if (capture_buffer_.size < bytes) {
      if (capture_buffer_.buffer) { idle(); vkDestroyBuffer(device_, capture_buffer_.buffer, nullptr); vkFreeMemory(device_, capture_buffer_.memory, nullptr); }
      capture_buffer_ = mapped_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    }
    raw_barrier(f.cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy region{}; region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; region.imageExtent = {swap_extent_.width, swap_extent_.height, 1};
    vkCmdCopyImageToBuffer(f.cmd, target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, capture_buffer_.buffer, 1, &region);
  }
  void write_capture() {
    capture_pending_ = false;
    vkWaitForFences(device_, 1, &frames_[slot_].fence, VK_TRUE, UINT64_MAX);
    const uint32_t w = swap_extent_.width, h = swap_extent_.height;
    const bool bgra = swap_format_ == VK_FORMAT_B8G8R8A8_UNORM;
    std::ofstream file(capture_file_, std::ios::binary);
    file << "P6\n" << w << " " << h << "\n255\n";
    std::vector<uint8_t> row(size_t(w) * 3);
    for (uint32_t y = 0; y < h; ++y) {
      const uint8_t* p = capture_buffer_.mapped + size_t(y) * w * 4;
      for (uint32_t x = 0; x < w; ++x) {
        row[x * 3 + 0] = p[x * 4 + (bgra ? 2 : 0)]; row[x * 3 + 1] = p[x * 4 + 1]; row[x * 3 + 2] = p[x * 4 + (bgra ? 0 : 2)];
      }
      file.write(reinterpret_cast<const char*>(row.data()), (std::streamsize)row.size());
    }
    host::log("vulkan: frame %u written to %s", presented_, capture_file_.c_str());
  }

  // Textures and EFB copies nothing drew for a while go; the GPU is done with them by then.
  void trim() {
    if (sequence_ % 600) return;
    for (auto it = textures_.begin(); it != textures_.end();) {
      if (sequence_ - it->second.used > 1800) {
        // Its share of the replacement budget comes back. It used to stay charged, so after enough
        // stages and menus the budget read as spent and every pack texture fell back to the original.
        replacement_bytes_ -= std::min(replacement_bytes_, it->second.charged);
        retire(it->second); it = textures_.erase(it);
      } else ++it;
    }
    for (auto it = copies_.begin(); it != copies_.end();) {
      if (sequence_ - it->second.used > 1800) { retire(it->second); it = copies_.erase(it); } else ++it;
    }
  }

#ifdef GX_PC_SETTINGS
  // ---- Settings panel (the shared panel drawn through Dear ImGui's Vulkan renderer) ----
  bool settings_ui_ready() const { return settings_ready_; }
  void create_settings_ui() {
    const bool open = opts_.settings_open || settings_fills_window();
    settings_state_.open = open;
    settings_state_.panel_anim_target_open = open;
    settings_state_.panel_anim_initialized = true;
    settings_state_.panel_anim_frame = open ? 17.0f : 12.0f;
    settings_state_.panel_slide_x = open ? 0.0f : -720.0f;
    settings_state_.panel_slide_start_x = settings_state_.panel_slide_x;
    settings_state_.running_vulkan = true;
    settings_context_create(window_.window, opts_.settings_open);
    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, [](const char* name, void* user) {
      auto* self = static_cast<VulkanBackend*>(user);
      PFN_vkVoidFunction fn = vkGetDeviceProcAddr(self->device_, name);
      return fn ? fn : vkGetInstanceProcAddr(self->instance_, name);
    }, this);
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3; info.Instance = instance_; info.PhysicalDevice = physical_; info.Device = device_;
    info.QueueFamily = queue_family_; info.Queue = queue_; info.DescriptorPoolSize = 64;
    info.MinImageCount = 2; info.ImageCount = (uint32_t)std::max<size_t>(swap_images_.size(), 2);
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swap_format_;
    settings_ready_ = ImGui_ImplVulkan_Init(&info);
    if (!settings_ready_) { host::log("vulkan: settings panel unavailable"); settings_context_destroy(); }
  }
  void destroy_settings_ui() {
    if (!settings_ready_) return;
    ImGui_ImplVulkan_Shutdown();
    settings_context_destroy();
    settings_ready_ = false;
  }
  SettingsState settings_state_;
  bool settings_ready_ = false;
#endif

  VulkanWindow window_;
  RenderOptions opts_;
  int client_w_, client_h_;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkPhysicalDeviceLimits limits_{};
  std::string device_name_;
  uint32_t queue_family_ = 0;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
  bool anisotropy_supported_ = false, wire_supported_ = false;
  Heap heap_;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE, blit_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE, blit_layout_ = VK_NULL_HANDLE;
  std::array<Frame_, kFrames> frames_{};
  int slot_ = 0;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat swap_format_ = VK_FORMAT_UNDEFINED;
  VkExtent2D swap_extent_{};
  VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
  std::vector<VkImage> swap_images_;
  std::vector<VkImageView> swap_views_;
  std::vector<VkSemaphore> swap_done_;
  bool swapchain_dirty_ = false, acquired_ = false;
  uint32_t image_index_ = 0;
  int scale_ = 1;
  uint32_t width_ = 0, height_ = 0;
  Image efb_color_, efb_depth_, white_, msaa_color_, msaa_depth_;
  VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
  int msaa_applied_ = 1;
  bool efb_cleared_ = false, pending_white_ = false, rendering_ = false, skip_present_ = false;
  VkShaderModule blit_vs_ = VK_NULL_HANDLE, blit_ps_ = VK_NULL_HANDLE;
  VkPipeline copy_pipeline_ = VK_NULL_HANDLE, present_pipeline_ = VK_NULL_HANDLE, bound_pipeline_ = VK_NULL_HANDLE;
  VkSampler linear_ = VK_NULL_HANDLE, nearest_ = VK_NULL_HANDLE;
  VkShaderModule present_vs_ = VK_NULL_HANDLE, present_ps_ = VK_NULL_HANDLE, present_crt_ps_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout present_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout present_layout_ = VK_NULL_HANDLE;
  VkPipeline present_plain_ = VK_NULL_HANDLE, present_crt_ = VK_NULL_HANDLE, present_plain_rgba_ = VK_NULL_HANDLE;
  Image shader_source_;   // the picture at the console's own size, for a shader preset (user_shader.h)
  std::unordered_map<uint64_t, VkShaderModule> modules_;
  std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelines_;
  std::unordered_map<uint64_t, Image> textures_, copies_;
  std::unordered_map<uint64_t, VkSampler> samplers_;
  std::vector<uint8_t> decoded_;
  std::vector<uint32_t> index_scratch_;
  std::vector<size_t> vertex_offsets_;
  VkDeviceSize vertex_base_ = 0;
  uint64_t sequence_ = 0, replacement_bytes_ = 0, replacement_budget_ = 512ull << 20;
  std::unordered_map<uint64_t, std::string> pending_hd_;   // textures drawn as the original while their replacement decodes
  uint32_t presented_ = 0, capture_frame_ = 0;
  int anisotropy_applied_ = 0;
  int pipeline_failures_ = 0, pool_exhausted_ = 0, ring_overflows_ = 0;
  EffectsFilter effects_filter_{};
  LightweightFilter lightweight_filter_{};
  Surface look_surface_ = Surface::Textured;
  bool frame_widenable_ = false, widescreen_sent_ = false, fod_sent_ = false;
  Buffer capture_buffer_;
  bool capture_pending_ = false;
  std::string capture_file_;
  // ---- AMD FSR ----
  bool upscaling_ = false, upscale_in_place_ = false, upscaled_now_ = false, upscale_reset_ = true, upscale_logged_ = false;
  bool aux_clear_pending_ = false, in_match_ = false, was_in_match_ = false, fsr_tried_ = false, efb_blit_upscale_ = false;
  int upscale_mode_ = 0, forced_scale_ = 0;
  uint32_t upscale_w_ = 0, upscale_h_ = 0;
  Image mvec_img_, hud_img_, upscale_out_;
  float jitter_x_ = 0, jitter_y_ = 0;
  double fsr_last_time_ = 0;
  uint32_t fsr_in_w_ = 0, fsr_in_h_ = 0;   // this frame's upscale inputs, reused by frame generation
  float fsr_jitter_x_ = 0, fsr_jitter_y_ = 0, fsr_frame_ms_ = 0;
  std::atomic<int> fg_multiplier_{1};   // frame_generation_multiplier(): 2 while FSR frame generation runs
#ifdef _WIN32
  Image fg_out_;    // the generated picture, the upscaled picture's size (storage)
  Image fg_real_;   // the rendered frame's window picture, copied into the next swapchain image
  VkCommandPool fg_pool_ = VK_NULL_HANDLE;
  std::array<VkCommandBuffer, kFrames> fg_cmds_{};
  std::array<VkFence, kFrames> fg_fences_{};
  std::array<VkSemaphore, kFrames> fg_acquired_{};
  bool fsr_fg_on_ = false, fsr_fg_generated_ = false, fsr_fg_running_ = false, fsr_fg_failed_ = false;
  uint64_t fsr_fg_prev_frame_ = ~0ull, fsr_fg_shown_ = 0;
  double fsr_fg_last_present_ = 0, fsr_fg_interval_ = 0;
  std::thread fg_presenter_;
  std::mutex fg_mutex_;
  std::condition_variable fg_cv_;
  bool fg_pending_ = false, fg_quit_ = false, fg_dirty_ = false;
  double fg_due_ = 0;
  int fg_slot_ = 0;
#endif
  TemporalMotionHistory last_poses_;
  // ---- Menu video backgrounds, border art ----
  Image video_images_[video_bg::kVideoSlots];
  uint64_t video_serial_[video_bg::kVideoSlots]{};
  const void* video_frame_[video_bg::kVideoSlots]{};
  std::shared_ptr<const video_bg::Frame> video_keep_[video_bg::kVideoSlots];
  bool video_layer_logged_[2]{};
  int draw_video_slot_ = -1;
  VkPipeline efb_blit_ = VK_NULL_HANDLE;
  VkSampleCountFlagBits efb_blit_samples_ = VK_SAMPLE_COUNT_1_BIT;
  // ---- GPU timers, validation ----
  uint32_t timestamp_bits_ = 0;
  uint64_t frame_counter_ = 0;
  double gpu_frame_ms_ = 0, gpu_present_ms_ = 0;
  uint32_t gpu_frame_samples_ = 0, gpu_present_samples_ = 0;
  bool validation_on_ = false;
  VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
  // ---- Pacing and the display (see the Backend overrides above) ----
  bool surface_caps2_on_ = false, present_id_on_ = false, present_wait_on_ = false, memory_budget_on_ = false, display_timing_on_ = false;
  bool push_descriptors_on_ = false;   // VK_KHR_push_descriptor: draws push their bindings (create_layouts, draw)
  bool fse_supported_ = false, fse_failed_ = false, fse_logged_ = false, want_exclusive_ = false;
  FullscreenState fullscreen_state_;
  uint64_t present_id_ = 0, last_present_id_ = 0;
  int present_wait_timeouts_ = 0;
  double present_deadline_ = 0, present_wait_ = 0, acquire_wait_ = 0;
  bool present_synced_ = true, tear_free_ = false, vrr_readable_ = false;
  double vrr_nominal_hz_ = 0;
  mutable double stat_acquire_ = 0, stat_wait_ = 0, stat_spin_ = 0, stat_present_ = 0;
  mutable uint64_t stat_frames_ = 0;
  uint32_t timing_shown_ = 0;
  uint64_t timing_blanks_ = 0;
#ifdef _WIN32
  HANDLE present_timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* high resolution */, TIMER_ALL_ACCESS);
  VBlankClock vblank_;
  bool vblank_tracked_ = false;
  vrr::Detector vrr_;
#else
  uint64_t last_actual_present_ = 0;
  uint32_t timing_intervals_ = 0, timing_on_grid_ = 0, timing_line_intervals_ = 0, timing_line_on_grid_ = 0;
  double timing_window_start_ = 0;
  uint32_t timing_window_presents_ = 0;
  int timing_state_ = -1, timing_pending_ = -1, timing_votes_ = 0;
#endif
};

}  // namespace

int vulkan_failed_starts() {
  std::ifstream f(shader_cache_dir() / "startup.pending", std::ios::binary);
  if (!f) return 0;
  int n = 0;
  return (f >> n) && n > 0 ? n : 1;
}

void vulkan_forget_failed_starts() {
  std::error_code ec;
  std::filesystem::remove(shader_cache_dir() / "startup.pending", ec);
}

bool vulkan_available() {
  if (g_vulkan_running) return true;
  static const bool available = [] {
    if (!load_loader()) return false;
    uint32_t version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&version);
    if (version < VK_API_VERSION_1_3) return false;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS) return false;
    auto destroy = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(instance, "vkDestroyInstance");
    auto enumerate = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
    auto properties = (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");
    uint32_t count = 0; enumerate(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count); enumerate(instance, &count, devices.data());
    // A Vulkan 1.3 device that can also map memory: some drivers (Mesa's lavapipe under WSL) report
    // success and return MAP_FAILED, which no renderer can draw with. One device and a 1 MB mapping
    // are tried, then let go.
    auto get = [&](const char* name) { return vkGetInstanceProcAddr(instance, name); };
    auto create_device = (PFN_vkCreateDevice)get("vkCreateDevice");
    auto device_proc = (PFN_vkGetDeviceProcAddr)get("vkGetDeviceProcAddr");
    auto memory_props = (PFN_vkGetPhysicalDeviceMemoryProperties)get("vkGetPhysicalDeviceMemoryProperties");
    bool ok = false;
    for (VkPhysicalDevice d : devices) {
      VkPhysicalDeviceProperties p; properties(d, &p);
      if (p.apiVersion < VK_API_VERSION_1_3) continue;
      const float priority = 1.0f;
      VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; q.queueCount = 1; q.pQueuePriorities = &priority;
      VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &q;
      VkDevice device = VK_NULL_HANDLE;
      if (create_device(d, &di, nullptr, &device) != VK_SUCCESS) continue;
      auto alloc = (PFN_vkAllocateMemory)device_proc(device, "vkAllocateMemory");
      auto map = (PFN_vkMapMemory)device_proc(device, "vkMapMemory");
      auto free_memory = (PFN_vkFreeMemory)device_proc(device, "vkFreeMemory");
      auto destroy_device = (PFN_vkDestroyDevice)device_proc(device, "vkDestroyDevice");
      VkPhysicalDeviceMemoryProperties mp; memory_props(d, &mp);
      const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      for (uint32_t t = 0; t < mp.memoryTypeCount && !ok; ++t) {
        if ((mp.memoryTypes[t].propertyFlags & want) != want) continue;
        VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, 1u << 20, t};
        VkDeviceMemory memory = VK_NULL_HANDLE; void* ptr = nullptr;
        if (alloc(device, &mi, nullptr, &memory) != VK_SUCCESS) continue;
        ok = map(device, memory, 0, 1u << 20, 0, &ptr) == VK_SUCCESS && ptr && ptr != reinterpret_cast<void*>(~uintptr_t(0));
        free_memory(device, memory, nullptr);
      }
      destroy_device(device, nullptr);
      if (ok) break;
      host::log("vulkan: %s cannot map memory; not offered", p.deviceName);
    }
    destroy(instance, nullptr);
    return ok;
  }();
  return available;
}

void vulkan_set_shader_cache(const std::string& folder) {
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_cache_root = folder.empty() ? std::filesystem::path("shadercache") : std::filesystem::u8path(folder);
}

Backend* create_vulkan_backend(const VulkanWindow& window, int client_w, int client_h, const RenderOptions& options) {
  vulkan_set_shader_cache(options.shader_cache);
  try {
    return new VulkanBackend(window, client_w, client_h, options);
  } catch (const std::exception& e) {
    host::log("%s", e.what());
    return nullptr;
  }
}

}  // namespace gx
