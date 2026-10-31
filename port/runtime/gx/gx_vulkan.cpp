// Vulkan renderer for the shared GX command decoder: one backend for Windows and Linux.
// The generated GX shaders (gx_shader.cpp, HLSL) stay the single TEV/lighting implementation:
// Windows compiles them to SPIR-V with the DirectX Shader Compiler the game already ships
// (dxcompiler.dll), Linux with glslangValidator as the OpenGL renderer does. The loader (vulkan-1 /
// libvulkan.so.1) is opened at run time, so a machine without Vulkan still starts on another backend.
// What it draws follows the OpenGL renderer (port/linux/renderer_gl.cpp): the EFB at the internal
// scale, EFB copies into textures keyed by their address, and the frame region fitted to the window
// with the box filter of the Direct3D present pass. NVIDIA DLSS/Reflex, XeSS, the CRT model and
// shader presets are Direct3D or OpenGL features and are not drawn here.
// SPDX-License-Identifier: GPL-2.0-or-later
#define VK_NO_PROTOTYPES
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <dxcapi.h>
#include <wrl/client.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#include <vulkan/vulkan.h>
#include "gx_vulkan.h"
#include "gx_shader.h"
#include "gx_texture.h"
#include "gx_retro.h"
#include "host.h"
#include "exi_slippi.h"
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
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace gx {
namespace {

// ---- Loader -------------------------------------------------------------------------------------

#define MU_VK_GLOBAL(X) X(vkCreateInstance) X(vkEnumerateInstanceExtensionProperties) X(vkEnumerateInstanceVersion)
#ifdef _WIN32
#define MU_VK_SURFACE_PLATFORM(X) X(vkCreateWin32SurfaceKHR)
#else
#define MU_VK_SURFACE_PLATFORM(X)
#endif
#define MU_VK_INSTANCE(X) X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
  X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceFeatures) \
  X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkDestroySurfaceKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR) \
  X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
  X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkEnumerateDeviceExtensionProperties) MU_VK_SURFACE_PLATFORM(X)
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
  X(vkCmdDrawIndexed) X(vkCmdDraw) X(vkCmdClearAttachments) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) \
  X(vkCmdPushConstants)

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
    void* library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library) library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
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

std::filesystem::path shader_cache_dir() {
  static const std::filesystem::path dir = [] {
    std::filesystem::path p = std::filesystem::path("shadercache") / "vulkan-v1";
    std::error_code ec; std::filesystem::create_directories(p, ec);
    return p;
  }();
  return dir;
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
std::vector<uint32_t> compile_hlsl(const std::string& hlsl, bool vertex, std::string* error) {
  Dxc* d = dxc();
  if (!d) { *error = "no shader compiler"; return {}; }
  DxcBuffer source{hlsl.data(), hlsl.size(), DXC_CP_UTF8};
  const wchar_t* args[] = {L"-spirv", L"-E", L"main", L"-T", vertex ? L"vs_6_0" : L"ps_6_0", L"-fspv-target-env=vulkan1.3",
                           L"-fvk-b-shift", L"0", L"0", L"-fvk-t-shift", L"2", L"0", L"-fvk-s-shift", L"3", L"0", L"-O3",
                           L"-Wno-ignored-attributes"};
  Microsoft::WRL::ComPtr<IDxcResult> result;
  if (FAILED(d->compiler->Compile(&source, args, (UINT32)(sizeof args / sizeof *args), nullptr, IID_PPV_ARGS(&result)))) { *error = "compile call failed"; return {}; }
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
std::vector<uint32_t> compile_hlsl(const std::string& hlsl, bool vertex, std::string* error) {
  const auto dir = shader_cache_dir();
  const std::string stem = (dir / ("tmp-" + std::to_string(getpid()))).string();
  const std::string in = stem + ".hlsl", out = stem + ".spv", log = stem + ".log";
  { std::ofstream f(in, std::ios::binary); f << hlsl; }
  const std::string command = std::string("glslangValidator -D -V -e main -S ") + (vertex ? "vert" : "frag") +
      " --target-env vulkan1.3 --auto-map-locations --shift-UBO-binding 0 --shift-texture-binding 2 --shift-sampler-binding 3 -o '" +
      out + "' '" + in + "' > '" + log + "' 2>&1";
  const int code = std::system(command.c_str());
  std::vector<uint32_t> words = code == 0 ? read_spirv(out) : std::vector<uint32_t>{};
  if (words.empty()) { std::ifstream f(log); *error = std::string((std::istreambuf_iterator<char>(f)), {}); if (error->empty()) *error = "glslangValidator is not installed"; }
  std::error_code ec; std::filesystem::remove(in, ec); std::filesystem::remove(out, ec); std::filesystem::remove(log, ec);
  return words;
}
#endif

// SPIR-V for one HLSL source, from the disk cache when it was compiled before.
std::vector<uint32_t> spirv_for(const std::string& hlsl, bool vertex) {
  const uint64_t hash = hash_bytes(hlsl.data(), hlsl.size()) ^ (vertex ? 0x5653ull : 0x5053ull);
  char name[40]; std::snprintf(name, sizeof name, "%s-%016llx.spv", vertex ? "vs" : "ps", (unsigned long long)hash);
  const auto path = shader_cache_dir() / name;
  std::vector<uint32_t> words = read_spirv(path);
  if (!words.empty()) return words;
  std::string error;
  words = compile_hlsl(hlsl, vertex, &error);
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
struct Allocation { VkDeviceMemory memory = VK_NULL_HANDLE; VkDeviceSize offset = 0; int block = -1; };

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
      Allocation a; check(vkAllocateMemory(device_, &info, nullptr, &a.memory), "dedicated allocation"); return a;
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
    b.used = req.size; b.live = 1;
    for (size_t i = 0; i < blocks_.size(); ++i) if (!blocks_[i].memory) { blocks_[i] = b; return {b.memory, 0, (int)i}; }
    blocks_.push_back(b);
    return {b.memory, 0, (int)blocks_.size() - 1};
  }
  void release(const Allocation& a) {
    if (a.block < 0) { if (a.memory) vkFreeMemory(device_, a.memory, nullptr); return; }
    Block& b = blocks_[a.block];
    if (--b.live == 0) { vkFreeMemory(device_, b.memory, nullptr); b = Block{}; }
  }
  void destroy() { for (auto& b : blocks_) if (b.memory) vkFreeMemory(device_, b.memory, nullptr); blocks_.clear(); }
 private:
  struct Block { VkDeviceMemory memory = VK_NULL_HANDLE; VkDeviceSize used = 0; uint32_t type = 0; int live = 0; };
  VkDevice device_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties props_{};
  std::vector<Block> blocks_;
};

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  Allocation memory;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  uint32_t width = 0, height = 0, levels = 1;
  uint64_t used = 0, bytes = 0;
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

constexpr int kFrames = 3;
constexpr VkDeviceSize kRingBytes = 96ull << 20;   // vertices, indices, constants and uploads of one frame

class VulkanBackend final : public Backend {
 public:
  VulkanBackend(const VulkanWindow& window, int client_w, int client_h, const RenderOptions& options)
      : window_(window), opts_(options), client_w_(std::max(client_w, 1)), client_h_(std::max(client_h, 1)) {
    create_instance();
    create_device();
    heap_.init(device_, physical_);
    create_layouts();
    create_frames();
    create_swapchain();
    scale_ = pick_scale();
    create_efb();
    blit_vs_ = module(spirv_for(std::string("#define VERTEX 1\n") + kBlitShader, true));
    blit_ps_ = module(spirv_for(kBlitShader, false));
    copy_pipeline_ = blit_pipeline(VK_FORMAT_R8G8B8A8_UNORM);
    present_pipeline_ = blit_pipeline(swap_format_);
    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.magFilter = s.minFilter = VK_FILTER_LINEAR; s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.maxLod = 0;
    check(vkCreateSampler(device_, &s, nullptr, &linear_), "sampler");
    s.magFilter = s.minFilter = VK_FILTER_NEAREST;
    check(vkCreateSampler(device_, &s, nullptr, &nearest_), "sampler");
    create_white();
    g_vulkan_running = true;
#ifdef GX_PC_SETTINGS
    if (opts_.pc_settings) create_settings_ui();
#endif
    host::log("vulkan: %s, internal resolution EFB x%d (window %dx%d), %s", device_name_.c_str(), scale_, client_w_, client_h_,
              present_mode_ == VK_PRESENT_MODE_FIFO_KHR ? "vsync" : present_mode_ == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "immediate");
  }

  ~VulkanBackend() override {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
#ifdef GX_PC_SETTINGS
    destroy_settings_ui();
#endif
    for (auto& p : pipelines_) vkDestroyPipeline(device_, p.second, nullptr);
    for (auto& m : modules_) vkDestroyShaderModule(device_, m.second, nullptr);
    for (auto& t : textures_) destroy(t.second);
    for (auto& c : copies_) destroy(c.second);
    for (auto& f : frames_) { for (auto& img : f.dead) destroy(img); f.dead.clear(); }
    destroy(efb_color_); destroy(efb_depth_); destroy(msaa_color_); destroy(msaa_depth_); destroy(white_);
    for (auto& s : samplers_) vkDestroySampler(device_, s.second, nullptr);
    vkDestroySampler(device_, linear_, nullptr); vkDestroySampler(device_, nearest_, nullptr);
    vkDestroyPipeline(device_, copy_pipeline_, nullptr); vkDestroyPipeline(device_, present_pipeline_, nullptr);
    vkDestroyShaderModule(device_, blit_vs_, nullptr); vkDestroyShaderModule(device_, blit_ps_, nullptr);
    destroy_swapchain();
    for (auto& f : frames_) {
      vkDestroyFence(device_, f.fence, nullptr);
      vkDestroySemaphore(device_, f.acquired, nullptr);
      vkDestroyDescriptorPool(device_, f.pool, nullptr);
      vkDestroyCommandPool(device_, f.commands, nullptr);
      vkDestroyBuffer(device_, f.ring.buffer, nullptr); vkFreeMemory(device_, f.ring.memory, nullptr);
    }
    vkDestroyPipelineLayout(device_, layout_, nullptr); vkDestroyPipelineLayout(device_, blit_layout_, nullptr);
    vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr); vkDestroyDescriptorSetLayout(device_, blit_set_layout_, nullptr);
    if (pipeline_cache_) { save_pipeline_cache(); vkDestroyPipelineCache(device_, pipeline_cache_, nullptr); }
    heap_.destroy();
    vkDestroyDevice(device_, nullptr);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
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
  void submit_frame(const Frame& frame) override { submit_frame(frame, nullptr); }
  void submit_frame(const Frame& frame, const DrawMatrices* overrides) override {
    begin_frame(frame);
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
    set_css_scene(frame.scene_major == 1 && frame.scene_minor == 0);
    bool presented = false;
    for (const auto& command : frame.commands) {
      if (command.kind == FrameCommand::Draw) {
        draw(frame, frame.draws.at(command.index), overrides ? overrides + command.index : nullptr, vertex_offsets_[command.index]);
      } else {
        const auto& c = frame.copies.at(command.index);
        if (c.to_xfb) { if (!skip_present_ && !presented) { present(c, frame.test_fill); presented = true; } }
        else copy(c);
        if (c.clear) clear(c);
      }
    }
    end_frame(presented);
    trim();
  }

 private:
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
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Melee Unlocked"; app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app; info.enabledExtensionCount = (uint32_t)extensions.size(); info.ppEnabledExtensionNames = extensions.data();
    check(vkCreateInstance(&info, nullptr, &instance_), "instance");
    load_instance(instance_);
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
        if (score > best) { best = score; physical_ = d; queue_family_ = q; device_name_ = p.deviceName; limits_ = p.limits; }
        break;
      }
    }
    if (!physical_) throw std::runtime_error("vulkan: no Vulkan 1.3 GPU that can draw to this window");
    VkPhysicalDeviceFeatures available{}; vkGetPhysicalDeviceFeatures(physical_, &available);
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = available.samplerAnisotropy; features.fillModeNonSolid = available.fillModeNonSolid;
    anisotropy_supported_ = available.samplerAnisotropy; wire_supported_ = available.fillModeNonSolid;
    VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    v13.dynamicRendering = VK_TRUE;
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = queue_family_; queue.queueCount = 1; queue.pQueuePriorities = &priority;
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &v13; info.queueCreateInfoCount = 1; info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = 1; info.ppEnabledExtensionNames = extensions; info.pEnabledFeatures = &features;
    check(vkCreateDevice(physical_, &info, nullptr, &device_), "device");
    load_device(device_);
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
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
    VkDescriptorSetLayoutBinding b[4]{};
    b[0] = {kBindingVS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[1] = {kBindingPS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[2] = {kBindingTex, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[3] = {kBindingSamp, VK_DESCRIPTOR_TYPE_SAMPLER, 8, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 4; li.pBindings = b;
    check(vkCreateDescriptorSetLayout(device_, &li, nullptr, &set_layout_), "descriptor layout");
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
      VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      bi.size = kRingBytes;
      bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      check(vkCreateBuffer(device_, &bi, nullptr, &f.ring.buffer), "ring buffer");
      VkMemoryRequirements req; vkGetBufferMemoryRequirements(device_, f.ring.buffer, &req);
      VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size,
                              heap_.type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
      check(vkAllocateMemory(device_, &mi, nullptr, &f.ring.memory), "ring memory");
      check(vkBindBufferMemory(device_, f.ring.buffer, f.ring.memory, 0), "ring bind");
      void* p = nullptr; check(vkMapMemory(device_, f.ring.memory, 0, VK_WHOLE_SIZE, 0, &p), "ring map");
      f.ring.mapped = (uint8_t*)p; f.ring.size = kRingBytes;
    }
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
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n); vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &n, modes.data());
    auto has = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
    present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    if (!opts_.vsync) present_mode_ = has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR : has(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
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
    destroy_swapchain();
    swapchain_ = created; swap_extent_ = extent;
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
  }
  void destroy_swapchain() {
    for (VkImageView v : swap_views_) vkDestroyImageView(device_, v, nullptr);
    for (VkSemaphore s : swap_done_) vkDestroySemaphore(device_, s, nullptr);
    swap_views_.clear(); swap_done_.clear(); swap_images_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }

  int pick_scale() const {
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
    samples_ = supported_samples(opts_.msaa);
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
  };

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
    vkWaitForFences(device_, 1, &f.fence, VK_TRUE, UINT64_MAX);
    for (auto& img : f.dead) destroy(img);
    f.dead.clear();
    for (auto& b : f.dead_buffers) { vkDestroyBuffer(device_, b.first, nullptr); vkFreeMemory(device_, b.second, nullptr); }
    f.dead_buffers.clear();
    vkResetCommandPool(device_, f.commands, 0);
    vkResetDescriptorPool(device_, f.pool, 0);
    f.ring.used = 0; f.uploads = false;
    ++sequence_;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(f.upload, &begin), "begin upload");
    check(vkBeginCommandBuffer(f.cmd, &begin), "begin frame");
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
      const int old_scale = scale_;
      const bool old_vsync = opts_.vsync;
      if (settings_frame(settings_state_, opts_)) {
        if (pick_scale() != old_scale || opts_.msaa != msaa_applied_) rebuild_efb();
        if (opts_.vsync != old_vsync) swapchain_dirty_ = true;
      }
    }
#endif
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
      vkDeviceWaitIdle(device_);
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
    end_efb();
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
    check(vkQueueSubmit(queue_, 1, &submit, f.fence), "submit");
    if (presented && acquired_) {
      VkPresentInfoKHR p{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
      p.waitSemaphoreCount = 1; p.pWaitSemaphores = &swap_done_[image_index_];
      p.swapchainCount = 1; p.pSwapchains = &swapchain_; p.pImageIndices = &image_index_;
      const VkResult r = vkQueuePresentKHR(queue_, &p);
      if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) swapchain_dirty_ = true;
      else if (r != VK_SUCCESS) check(r, "present");
      ++presented_;
      if (capture_pending_) write_capture();
    }
    acquired_ = false;
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
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, {width_, height_}}; info.layerCount = 1;
    info.colorAttachmentCount = 1; info.pColorAttachments = &color; info.pDepthAttachment = &depth;
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
    vkDeviceWaitIdle(device_);
    host::log("vulkan: internal scale %d -> %d", scale_, pick_scale());
    end_efb();
    retire(efb_color_); retire(efb_depth_); retire(msaa_color_); retire(msaa_depth_);
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
    replacement_bytes_ = 0;
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
                            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) {
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
    cb.attachmentCount = 1; cb.pAttachments = &blend;
    const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &color; rendering.depthAttachmentFormat = depth;
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
    const auto vs = make_vs_uid(dc); const auto ps = make_ps_uid(dc);
    // Bits 19 and up: the EFB's sample count, so an MSAA change builds its own pipelines.
    const PipelineKey key{vs.hash(), ps.hash(), pipeline_state(dc, topology, wire && wire_supported_) | (uint32_t(samples_) << 19)};
    if (auto i = pipelines_.find(key); i != pipelines_.end()) return i->second;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
                 cached_module(key.vs, generate_vertex_shader(vs), true), "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
                 cached_module(key.ps, generate_pixel_shader(ps), false), "main", nullptr};
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
                                  VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT, samples_);
    pipelines_.emplace(key, p);
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
      VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = bytes; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      check(vkCreateBuffer(device_, &bi, nullptr, &source), "staging buffer");
      VkMemoryRequirements req; vkGetBufferMemoryRequirements(device_, source, &req);
      VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size,
                              heap_.type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
      VkDeviceMemory memory; check(vkAllocateMemory(device_, &mi, nullptr, &memory), "staging memory");
      vkBindBufferMemory(device_, source, memory, 0);
      void* p = nullptr; vkMapMemory(device_, memory, 0, VK_WHOLE_SIZE, 0, &p); std::memcpy(p, rgba, bytes); vkUnmapMemory(device_, memory);
      f.dead_buffers.push_back({source, memory});
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
      if (texpack::has(name)) {
        constexpr uint64_t budget = 512ull << 20;
        auto r = texpack::load(name, replacement_bytes_ < budget ? budget - replacement_bytes_ : 0);
        texpack::note_lookup(r != nullptr);
        if (r) {
          const uint32_t levels = std::max(1u, std::min(r->levels, 16u));
          Image img = make_image(r->width, r->height, levels, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT);
          for (uint32_t level = 0; level < levels; ++level)
            upload_level(img, level, r->level_width[level], r->level_height[level], r->pixels.data() + r->level_offset[level]);
          finish_upload(img);
          img.used = sequence_;
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
    fill_vs_constants(dc, vc, scale_, matrices); fill_ps_constants(dc, pc, scale_);
    const VkDeviceSize align = std::max<VkDeviceSize>(limits_.minUniformBufferOffsetAlignment, 16);
    const VkDeviceSize vs_at = ring_alloc(sizeof vc, align), ps_at = ring_alloc(sizeof pc, align);
    std::memcpy(f.ring.mapped + vs_at, &vc, sizeof vc); std::memcpy(f.ring.mapped + ps_at, &pc, sizeof pc);
    const VkDeviceSize index_at = ring_alloc(index_scratch_.size() * 4, 4);
    std::memcpy(f.ring.mapped + index_at, index_scratch_.data(), index_scratch_.size() * 4);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = f.pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &set_layout_;
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(device_, &ai, &set) != VK_SUCCESS) { if (++pool_exhausted_ <= 3) host::log("vulkan: more than 8192 draws in one frame; the rest were skipped"); return; }
    VkDescriptorBufferInfo vsb{f.ring.buffer, 0, sizeof vc}, psb{f.ring.buffer, 0, sizeof pc};
    VkWriteDescriptorSet writes[4]{};
    for (auto& wr : writes) wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, wr.dstSet = set;
    writes[0].dstBinding = kBindingVS; writes[0].descriptorCount = 1; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; writes[0].pBufferInfo = &vsb;
    writes[1].dstBinding = kBindingPS; writes[1].descriptorCount = 1; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; writes[1].pBufferInfo = &psb;
    writes[2].dstBinding = kBindingTex; writes[2].descriptorCount = 8; writes[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; writes[2].pImageInfo = images;
    writes[3].dstBinding = kBindingSamp; writes[3].descriptorCount = 8; writes[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; writes[3].pImageInfo = samplers;
    vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);
    if (p != bound_pipeline_) { vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p); bound_pipeline_ = p; }
    const uint32_t offsets[] = {(uint32_t)vs_at, (uint32_t)ps_at};
    vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 2, offsets);
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
    if (swapchain_dirty_) {
      vkDeviceWaitIdle(device_);
      create_swapchain();
      if (swapchain_dirty_) return;   // minimized
    }
    const VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, f.acquired, VK_NULL_HANDLE, &image_index_);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { swapchain_dirty_ = true; return; }
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
    const VkImage target = swap_images_[image_index_];
    raw_barrier(f.cmd, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const int w = (int)swap_extent_.width, h = (int)swap_extent_.height;
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = swap_views_[image_index_]; color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const float fill = test_fill > 0 ? 1.0f : 0.0f;
    color.clearValue.color = {{fill, fill, fill, 1}};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, swap_extent_}; info.layerCount = 1; info.colorAttachmentCount = 1; info.pColorAttachments = &color;
    vkCmdBeginRendering(f.cmd, &info);
    if (test_fill < 0) {
      const double aspect = presented_aspect(opts_, w, h, frame_widenable_);
      int pw = w, ph = (int)std::lround(w / aspect);
      if (ph > h) { ph = h; pw = (int)std::lround(h * aspect); }
      const float vx = std::floor((w - pw) * 0.5f), vy = std::floor((h - ph) * 0.5f);
      VkViewport viewport{vx, vy, float(pw), float(ph), 0, 1};
      VkRect2D scissor{{0, 0}, swap_extent_};
      vkCmdSetViewport(f.cmd, 0, 1, &viewport); vkCmdSetScissor(f.cmd, 0, 1, &scissor);
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
#ifdef GX_PC_SETTINGS
    if (settings_ui_ready()) {
      VkViewport viewport{0, 0, float(w), float(h), 0, 1};
      vkCmdSetViewport(f.cmd, 0, 1, &viewport);
      ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), f.cmd);
    }
#endif
    vkCmdEndRendering(f.cmd);
    capture_pending_ = test_fill < 0 && wants_capture();
    if (capture_pending_) record_capture(target);
    raw_barrier(f.cmd, target, capture_pending_ ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    barrier(f.cmd, efb_color_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
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
      if (capture_buffer_.buffer) { vkDeviceWaitIdle(device_); vkDestroyBuffer(device_, capture_buffer_.buffer, nullptr); vkFreeMemory(device_, capture_buffer_.memory, nullptr); }
      VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = bytes; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      check(vkCreateBuffer(device_, &bi, nullptr, &capture_buffer_.buffer), "capture buffer");
      VkMemoryRequirements req; vkGetBufferMemoryRequirements(device_, capture_buffer_.buffer, &req);
      VkMemoryAllocateInfo mi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, req.size,
                              heap_.type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
      check(vkAllocateMemory(device_, &mi, nullptr, &capture_buffer_.memory), "capture memory");
      vkBindBufferMemory(device_, capture_buffer_.buffer, capture_buffer_.memory, 0);
      void* p = nullptr; vkMapMemory(device_, capture_buffer_.memory, 0, VK_WHOLE_SIZE, 0, &p);
      capture_buffer_.mapped = (uint8_t*)p; capture_buffer_.size = bytes;
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
      if (sequence_ - it->second.used > 1800) { retire(it->second); it = textures_.erase(it); } else ++it;
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
  std::unordered_map<uint64_t, VkShaderModule> modules_;
  std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelines_;
  std::unordered_map<uint64_t, Image> textures_, copies_;
  std::unordered_map<uint64_t, VkSampler> samplers_;
  std::vector<uint8_t> decoded_;
  std::vector<uint32_t> index_scratch_;
  std::vector<size_t> vertex_offsets_;
  VkDeviceSize vertex_base_ = 0;
  uint64_t sequence_ = 0, replacement_bytes_ = 0;
  uint32_t presented_ = 0, capture_frame_ = 0;
  int anisotropy_applied_ = 0;
  int pipeline_failures_ = 0, pool_exhausted_ = 0;
  EffectsFilter effects_filter_{};
  LightweightFilter lightweight_filter_{};
  Surface look_surface_ = Surface::Textured;
  bool frame_widenable_ = false, widescreen_sent_ = false, fod_sent_ = false;
  Buffer capture_buffer_;
  bool capture_pending_ = false;
  std::string capture_file_;
};

}  // namespace

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
    bool ok = false;
    for (VkPhysicalDevice d : devices) { VkPhysicalDeviceProperties p; properties(d, &p); ok |= p.apiVersion >= VK_API_VERSION_1_3; }
    destroy(instance, nullptr);
    return ok;
  }();
  return available;
}

Backend* create_vulkan_backend(const VulkanWindow& window, int client_w, int client_h, const RenderOptions& options) {
  try {
    return new VulkanBackend(window, client_w, client_h, options);
  } catch (const std::exception& e) {
    host::log("%s", e.what());
    return nullptr;
  }
}

}  // namespace gx
