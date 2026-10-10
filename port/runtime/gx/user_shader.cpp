// Player-chosen shader presets through librashader. See user_shader.h.
#include "user_shader.h"
#include "../host/host.h"
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <d3d12.h>
#define LIBRA_RUNTIME_D3D11
#define LIBRA_RUNTIME_D3D12
#include "../../third_party/librashader/librashader_ld.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>

namespace gx::user_shader {
namespace {

std::filesystem::path shader_root() {
  wchar_t path[32768]{};
  const DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)std::size(path));
  const std::filesystem::path exe = (!n || n >= std::size(path)) ? std::filesystem::current_path() : std::filesystem::path(path).parent_path();
  return exe / "Shaders";
}

libra_instance_t& library() {
  static libra_instance_t instance = [] {
    libra_instance_t loaded = librashader_load_instance();
    if (loaded.instance_loaded) host::log("shaders: librashader loaded (API %zu)", loaded.instance_api_version());
    return loaded;
  }();
  return instance;
}

std::mutex g_status_mutex;
std::string g_status;
void set_status(const std::string& text) {
  std::lock_guard<std::mutex> lock(g_status_mutex);
  g_status = text;
}

// The text of a librashader error, which is freed here. Empty when there was none.
std::string take_error(libra_error_t error) {
  if (!error) return {};
  std::string text = "the shader library reported an error";
  char* written = nullptr;
  if (library().error_write(error, &written) == 0 && written) {
    text = written;
    library().error_free_string(&written);
  }
  library().error_free(&error);
  return text;
}

// The choice a backend last tried, so that a preset that failed is not rebuilt every frame.
struct Choice {
  std::string preset;
  bool failed = false;
};

bool load_preset(const std::string& preset, libra_shader_preset_t* out) {
  const std::filesystem::path file = shader_root() / std::filesystem::u8path(preset);
  std::error_code ec;
  if (!std::filesystem::is_regular_file(file, ec)) {
    set_status(preset + " is not in the Shaders folder");
    return false;
  }
  const std::string error = take_error(library().preset_create(file.u8string().c_str(), out));
  if (error.empty()) return true;
  set_status(error);
  return false;
}

void report(const std::string& preset, bool ok) {
  if (ok) { set_status({}); host::log("shaders: %s is running", preset.c_str()); }
  else host::log("shaders: %s is off: %s", preset.c_str(), status().c_str());
}

libra_d3d12_filter_chain_t g_chain12 = nullptr;
Choice g_choice12;
libra_d3d11_filter_chain_t g_chain11 = nullptr;
Choice g_choice11;

}  // namespace

bool available() { return library().instance_loaded; }

std::vector<std::string> presets() {
  static std::vector<std::string> found;
  static std::chrono::steady_clock::time_point listed{};
  const auto now = std::chrono::steady_clock::now();
  if (listed != std::chrono::steady_clock::time_point{} && now - listed < std::chrono::seconds(3)) return found;
  listed = now;
  found.clear();
  const std::filesystem::path root = shader_root();
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (it.depth() > 6) { it.disable_recursion_pending(); continue; }
    std::error_code kind;
    if (!it->is_regular_file(kind) || it->path().extension() != ".slangp") continue;
    found.push_back(it->path().lexically_relative(root).generic_u8string());
    if (found.size() >= 4000) break;
  }
  std::sort(found.begin(), found.end());
  return found;
}

void open_folder() {
  const std::filesystem::path root = shader_root();
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  ShellExecuteW(nullptr, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string status() {
  std::lock_guard<std::mutex> lock(g_status_mutex);
  return g_status;
}

bool d3d12_prepare(ID3D12Device* device, const std::string& preset, void (*wait)(void*), void* owner) {
  if (preset == g_choice12.preset) return g_chain12 != nullptr && !g_choice12.failed;
  if (g_chain12) {
    wait(owner);   // frames already submitted still use the old chain's resources
    library().d3d12_filter_chain_free(&g_chain12);
    g_chain12 = nullptr;
  }
  g_choice12 = {preset, true};
  if (preset.empty()) { set_status({}); return false; }
  if (!available()) { set_status("librashader.dll is missing from the game folder"); report(preset, false); return false; }
  // On Direct3D 12 the library compiles with the DirectX shader compiler and loads it on first use.
  // Without the file that load ends the program, so it is loaded here first and its absence is a message.
  static const bool compiler = LoadLibraryW(L"dxcompiler.dll") != nullptr;
  if (!compiler) { set_status("dxcompiler.dll is missing from the game folder (needed for shader presets on Direct3D 12)"); report(preset, false); return false; }
  libra_shader_preset_t loaded = nullptr;
  if (!load_preset(preset, &loaded)) { report(preset, false); return false; }
  filter_chain_d3d12_opt_t options{};
  options.version = LIBRASHADER_CURRENT_VERSION;
  options.frames_in_flight = 3;
  const std::string error = take_error(library().d3d12_filter_chain_create(&loaded, device, &options, &g_chain12));
  if (!error.empty() || !g_chain12) {
    g_chain12 = nullptr;
    set_status(error.empty() ? "the filter chain could not be made" : error);
    report(preset, false);
    return false;
  }
  g_choice12.failed = false;
  report(preset, true);
  return true;
}

bool d3d12_draw(ID3D12GraphicsCommandList* list, ID3D12Resource* source, size_t output_rtv, int output_format,
                uint32_t output_width, uint32_t output_height, const Target& where, size_t frame) {
  if (!g_chain12) return false;
  libra_image_d3d12_t in{};
  in.image_type = LIBRA_D3D12_IMAGE_TYPE_RESOURCE;
  in.handle.resource = source;
  libra_image_d3d12_t out{};
  out.image_type = LIBRA_D3D12_IMAGE_TYPE_OUTPUT_IMAGE;
  out.handle.output.descriptor.ptr = output_rtv;
  out.handle.output.format = (DXGI_FORMAT)output_format;
  out.handle.output.width = output_width;
  out.handle.output.height = output_height;
  const libra_viewport_t viewport{where.x, where.y, where.width, where.height};
  const std::string error = take_error(library().d3d12_filter_chain_frame(&g_chain12, list, frame, in, out, &viewport, nullptr, nullptr));
  if (error.empty()) return true;
  set_status(error);
  g_choice12.failed = true;   // the plain picture from the next frame on
  host::log("shaders: %s stopped: %s", g_choice12.preset.c_str(), error.c_str());
  return false;
}

void d3d12_release() {
  if (g_chain12) library().d3d12_filter_chain_free(&g_chain12);
  g_chain12 = nullptr;
  g_choice12 = {};
}

bool d3d11_prepare(ID3D11Device* device, const std::string& preset) {
  if (preset == g_choice11.preset) return g_chain11 != nullptr && !g_choice11.failed;
  if (g_chain11) { library().d3d11_filter_chain_free(&g_chain11); g_chain11 = nullptr; }
  g_choice11 = {preset, true};
  if (preset.empty()) { set_status({}); return false; }
  if (!available()) { set_status("librashader.dll is missing from the game folder"); report(preset, false); return false; }
  libra_shader_preset_t loaded = nullptr;
  if (!load_preset(preset, &loaded)) { report(preset, false); return false; }
  filter_chain_d3d11_opt_t options{};
  options.version = LIBRASHADER_CURRENT_VERSION;
  const std::string error = take_error(library().d3d11_filter_chain_create(&loaded, device, &options, &g_chain11));
  if (!error.empty() || !g_chain11) {
    g_chain11 = nullptr;
    set_status(error.empty() ? "the filter chain could not be made" : error);
    report(preset, false);
    return false;
  }
  g_choice11.failed = false;
  report(preset, true);
  return true;
}

bool d3d11_draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, ID3D11RenderTargetView* output,
                const Target& where, size_t frame) {
  if (!g_chain11) return false;
  const libra_viewport_t viewport{where.x, where.y, where.width, where.height};
  const std::string error = take_error(library().d3d11_filter_chain_frame(&g_chain11, context, frame, source, output, &viewport, nullptr, nullptr));
  if (error.empty()) return true;
  set_status(error);
  g_choice11.failed = true;
  host::log("shaders: %s stopped: %s", g_choice11.preset.c_str(), error.c_str());
  return false;
}

void d3d11_release() {
  if (g_chain11) library().d3d11_filter_chain_free(&g_chain11);
  g_chain11 = nullptr;
  g_choice11 = {};
}

}  // namespace gx::user_shader
