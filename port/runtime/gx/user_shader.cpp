// Player-chosen shader presets through librashader. See user_shader.h.
#include "user_shader.h"
#include "../host/host.h"
#include <windows.h>
#include <shellapi.h>
#ifdef _WIN32
#include <d3d11.h>
#include <d3d12.h>
#define LIBRA_RUNTIME_D3D11
#define LIBRA_RUNTIME_D3D12
#else
#define LIBRA_RUNTIME_OPENGL   // Linux: the presets run on the OpenGL renderer (gl_prepare/gl_draw)
#endif
#include "../../third_party/librashader/librashader_ld.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <vector>

namespace gx::user_shader {
std::vector<std::string> presets();
namespace {

std::filesystem::path shader_root() {
  wchar_t path[32768]{};
  const DWORD n = GetModuleFileNameW(nullptr, path, (DWORD)std::size(path));
  const std::filesystem::path exe = (!n || n >= std::size(path)) ? std::filesystem::current_path() : std::filesystem::path(path).parent_path();
  return exe / "Shaders";
}

// Windows refuses a path of 260 characters or more unless it is written in the long form. A shader
// pack nests deep and some of its files have very long names, so in a game folder that is itself
// deep the plain path does not open.
std::wstring long_path(const std::filesystem::path& path) {
  std::wstring text = path.lexically_normal().wstring();
  if (text.size() < 240 || text.size() < 3 || text[1] != L':' || text.rfind(L"\\\\", 0) == 0) return text;
  return L"\\\\?\\" + text;
}
bool is_file(const std::filesystem::path& path) {
  const DWORD attributes = GetFileAttributesW(long_path(path).c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
void make_folders(const std::filesystem::path& folder) {
  std::filesystem::path built;
  for (const auto& part : folder) {
    built /= part;
    if (built.wstring().size() > 3) CreateDirectoryW(long_path(built).c_str(), nullptr);
  }
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

std::string read_text(const std::filesystem::path& path) {
  std::string text;
  FILE* f = _wfopen(long_path(path).c_str(), L"rb");
  if (!f) return text;
  char chunk[4096];
  for (size_t n; text.size() < (1u << 20) && (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) text.append(chunk, n);
  std::fclose(f);
  return text;
}

std::string trimmed(const std::string& s, const char* drop = " \t\r\"") {
  const size_t a = s.find_first_not_of(drop), b = s.find_last_not_of(drop);
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// Calls `each` with every line of `text`, leading blanks removed.
template <class F> void for_lines(const std::string& text, F each) {
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    const size_t first = text.find_first_not_of(" \t\r", at);
    if (first != std::string::npos && first < end) each(text.substr(first, end - first));
    at = end + 1;
  }
}

// The first file a shader pulls in with #include (followed into the files those name) that is not
// there, as the line wrote it. `budget` bounds the files read, against a pack that includes in a ring.
std::string missing_include(const std::filesystem::path& shader, int depth, int* budget) {
  if (depth > 16 || --*budget < 0) return {};
  std::string missing;
  for_lines(read_text(shader), [&](const std::string& line) {
    if (!missing.empty() || line.rfind("#include", 0) != 0) return;
    const size_t open = line.find('"'), close = line.find('"', open == std::string::npos ? 0 : open + 1);
    if (open == std::string::npos || close == std::string::npos) return;
    const std::string named = line.substr(open + 1, close - open - 1);
    const std::filesystem::path target = (shader.parent_path() / std::filesystem::u8path(named)).lexically_normal();
    if (!is_file(target)) missing = named;
    else missing = missing_include(target, depth + 1, budget);
  });
  return missing;
}

// A preset holds no shader code: it lists shader files, the pictures they sample and sometimes
// another preset it builds on, all named relative to its own folder. A player who copies only the
// .slangp file gets the library's raw "path not found" error, which reads like a fault of the game.
// This names the first file that is not there, as the preset or shader wrote it. Empty when every
// file named exists (or the preset cannot be read).
std::string first_missing_file(const std::filesystem::path& preset, int depth = 0) {
  const std::string text = read_text(preset);
  const std::filesystem::path folder = preset.parent_path();
  std::vector<std::string> textures;   // the names its "textures" line lists; each has a line of its own
  for_lines(text, [&](const std::string& line) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos || trimmed(line.substr(0, eq)) != "textures") return;
    const std::string list = trimmed(line.substr(eq + 1));
    for (size_t at = 0; at <= list.size();) {
      size_t end = list.find(';', at);
      if (end == std::string::npos) end = list.size();
      const std::string name = trimmed(list.substr(at, end - at));
      if (!name.empty()) textures.push_back(name);
      at = end + 1;
    }
  });
  std::string missing;
  int budget = 600;
  for_lines(text, [&](const std::string& line) {
    if (!missing.empty()) return;
    std::string named;
    bool reference = false, shader = false;
    if (line.rfind("#reference", 0) == 0) { named = trimmed(line.substr(10)); reference = true; }
    else {
      const size_t eq = line.find('=');
      if (eq == std::string::npos) return;
      const std::string key = trimmed(line.substr(0, eq));
      shader = key.size() > 6 && key.rfind("shader", 0) == 0 &&
               std::all_of(key.begin() + 6, key.end(), [](char c) { return std::isdigit((unsigned char)c) != 0; });
      if (!shader && std::find(textures.begin(), textures.end(), key) == textures.end()) return;
      named = trimmed(line.substr(eq + 1));
    }
    if (named.empty()) return;
    const std::filesystem::path target = (folder / std::filesystem::u8path(named)).lexically_normal();
    if (!is_file(target)) missing = named;
    else if (reference && depth < 8) missing = first_missing_file(target, depth + 1);
    else if (shader) missing = missing_include(target, 0, &budget);
  });
  return missing;
}

void say_missing(const std::string& missing) {
  set_status("this needs the file " + missing + ", which is not in the Shaders folder. "
             "Copy the whole shader pack with its folders, not single files out of it");
}

// A single .slang file dropped straight into the Shaders folder is a whole shader of one pass. The
// library only takes presets, so one is written for it in the temporary folder, naming the file by
// its full path. Empty when it could not be written.
std::filesystem::path one_pass_preset(const std::filesystem::path& shader) {
  wchar_t temp[MAX_PATH + 1]{};
  if (!GetTempPathW(MAX_PATH, temp)) return {};
  const std::filesystem::path folder = std::filesystem::path(temp) / "MeleeUnlockedShaders";
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  const std::filesystem::path preset = folder / (shader.stem().wstring() + L".slangp");
  FILE* f = _wfopen(preset.c_str(), L"wb");
  if (!f) return {};
  const std::string text = "shaders = 1\nshader0 = \"" + shader.generic_u8string() + "\"\n";
  const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
  std::fclose(f);
  return ok ? preset : std::filesystem::path();
}

// A release carries the CRT presets as one file, Shaders\crt-shaders.pak: the pack's long file names
// inside the release archive stopped the update's copy step in a deep game folder. The first use
// after an install writes the files out under Shaders\slang-shaders and notes the pack's id there,
// so it happens once per release. Layout, little endian: "MUSHPAK1", 16 characters of id, a 32-bit
// count, then for each file a 16-bit name length, the name (relative, forward slashes), a 32-bit
// size and the bytes.
void unpack_bundle() {
  static std::once_flag once;
  std::call_once(once, [] {
    const std::filesystem::path root = shader_root(), stamp = root / "slang-shaders" / "bundle-id.txt";
    FILE* f = _wfopen((root / "crt-shaders.pak").c_str(), L"rb");
    if (!f) return;
    char head[24]{};
    uint32_t count = 0;
    bool ok = std::fread(head, 1, 24, f) == 24 && !std::memcmp(head, "MUSHPAK1", 8) && std::fread(&count, 4, 1, f) == 1 && count <= 5000;
    const std::string id(head + 8, 16);
    if (ok && trimmed(read_text(stamp)) == id) { std::fclose(f); return; }
    size_t written = 0;
    std::vector<char> bytes;
    for (uint32_t i = 0; ok && i < count; ++i) {
      uint16_t name_size = 0;
      uint32_t size = 0;
      std::string name;
      ok = std::fread(&name_size, 2, 1, f) == 1 && name_size && name_size < 400;
      if (ok) { name.resize(name_size); ok = std::fread(name.data(), 1, name_size, f) == name_size; }
      ok = ok && std::fread(&size, 4, 1, f) == 1 && size <= (16u << 20);
      // Only a plain relative name: nothing absolute, no drive, no step up.
      ok = ok && name[0] != '/' && name.find_first_of("\\:") == std::string::npos && name.find("..") == std::string::npos;
      if (!ok) break;
      bytes.resize(size);
      ok = !size || std::fread(bytes.data(), 1, size, f) == size;
      if (!ok) break;
      const std::filesystem::path target = root / "slang-shaders" / std::filesystem::u8path(name);
      make_folders(target.parent_path());
      FILE* out = _wfopen(long_path(target).c_str(), L"wb");
      ok = out && (!size || std::fwrite(bytes.data(), 1, size, out) == size);
      if (out) ok = std::fclose(out) == 0 && ok;
      if (ok) ++written;
    }
    std::fclose(f);
    if (ok) {
      FILE* out = _wfopen(long_path(stamp).c_str(), L"wb");
      if (out) { std::fwrite(id.data(), 1, id.size(), out); std::fclose(out); }
      host::log("shaders: %zu bundled preset files written to the Shaders folder", written);
    } else {
      host::log("shaders: the bundled presets could not be written out (stopped after %zu files)", written);
    }
  });
}

bool load_preset(const std::string& preset, libra_shader_preset_t* out) {
  unpack_bundle();
  std::filesystem::path file = shader_root() / std::filesystem::u8path(preset);
  if (!is_file(file)) {
    set_status(preset + " is not in the Shaders folder");
    return false;
  }
  if (file.extension() == ".slang") {
    int budget = 600;
    const std::string missing = missing_include(file, 0, &budget);
    if (!missing.empty()) { say_missing(missing); return false; }
    file = one_pass_preset(file);
    if (file.empty()) { set_status("a preset for this shader could not be written to the temporary folder"); return false; }
  } else {
    const std::string missing = first_missing_file(file);
    if (!missing.empty()) { say_missing(missing); return false; }
  }
  const std::string error = take_error(library().preset_create(file.u8string().c_str(), out));
  if (error.empty()) return true;
  set_status(error);
  return false;
}

// MELEE_TEST_SHADER_SCAN (tests only, hidden and headless runs): the missing-file check over every
// preset in the Shaders folder, to show it finds no fault with a complete pack.
void test_scan() {
  static bool done = false;
  if (done) return;
  done = true;
  if (!std::getenv("MELEE_TEST_SHADER_SCAN") || !std::getenv("MELEE_NO_GC_ADAPTER")) return;
  size_t flagged = 0;
  const std::vector<std::string> all = presets();
  for (const std::string& name : all) {
    const std::filesystem::path file = shader_root() / std::filesystem::u8path(name);
    int budget = 600;
    const std::string missing = file.extension() == ".slang" ? missing_include(file, 0, &budget) : first_missing_file(file);
    if (missing.empty()) continue;
    if (++flagged <= 12) host::log("shaders: test scan: %s lacks %s", name.c_str(), missing.c_str());
  }
  host::log("shaders: test scan: %zu presets and shaders, %zu with a missing file", all.size(), flagged);
}

void report(const std::string& preset, bool ok) {
  if (ok) { set_status({}); host::log("shaders: %s is running", preset.c_str()); }
  else host::log("shaders: %s is off: %s", preset.c_str(), status().c_str());
}

#ifndef _WIN32
libra_gl_filter_chain_t g_chain_gl = nullptr;
Choice g_choice_gl;
#endif
#ifdef _WIN32
libra_d3d12_filter_chain_t g_chain12 = nullptr;
Choice g_choice12;
libra_d3d11_filter_chain_t g_chain11 = nullptr;
Choice g_choice11;
#endif

}  // namespace

bool available() { return library().instance_loaded; }

std::vector<std::string> presets() {
  unpack_bundle();
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
    if (!it->is_regular_file(kind)) continue;
    // Presets anywhere in the folder; a lone .slang file only when it sits directly in it (the
    // thousands inside a pack are passes of its presets, not shaders to pick).
    const auto extension = it->path().extension();
    if (extension != ".slangp" && !(extension == ".slang" && it.depth() == 0)) continue;
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

#ifdef _WIN32
bool d3d12_prepare(ID3D12Device* device, const std::string& preset, void (*wait)(void*), void* owner) {
  test_scan();
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
  test_scan();
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

#endif

#ifndef _WIN32
bool gl_prepare(const std::string& preset, const void* (*loader)(const char*)) {
  test_scan();
  if (preset == g_choice_gl.preset) return g_chain_gl != nullptr && !g_choice_gl.failed;
  if (g_chain_gl) { library().gl_filter_chain_free(&g_chain_gl); g_chain_gl = nullptr; }
  g_choice_gl = {preset, true};
  if (preset.empty()) { set_status({}); return false; }
  if (!available()) { set_status("librashader.so is missing from the game's lib folder"); report(preset, false); return false; }
  libra_shader_preset_t loaded = nullptr;
  if (!load_preset(preset, &loaded)) { report(preset, false); return false; }
  filter_chain_gl_opt_t options{};
  options.version = LIBRASHADER_CURRENT_VERSION;
  options.glsl_version = 450;
  options.use_dsa = true;
  const std::string error = take_error(library().gl_filter_chain_create(&loaded, loader, &options, &g_chain_gl));
  if (!error.empty() || !g_chain_gl) {
    g_chain_gl = nullptr;
    set_status(error.empty() ? "the filter chain could not be made" : error);
    report(preset, false);
    return false;
  }
  g_choice_gl.failed = false;
  report(preset, true);
  return true;
}

bool gl_draw(uint32_t source, uint32_t source_width, uint32_t source_height, uint32_t output, uint32_t output_width,
             uint32_t output_height, const Target& where, size_t frame) {
  if (!g_chain_gl) return false;
  constexpr uint32_t kRgba8 = 0x8058;   // GL_RGBA8
  const libra_image_gl_t in{source, kRgba8, source_width, source_height};
  const libra_image_gl_t out{output, kRgba8, output_width, output_height};
  const libra_viewport_t viewport{where.x, where.y, where.width, where.height};
  const std::string error = take_error(library().gl_filter_chain_frame(&g_chain_gl, frame, in, out, &viewport, nullptr, nullptr));
  if (error.empty()) return true;
  set_status(error);
  g_choice_gl.failed = true;
  host::log("shaders: %s stopped: %s", g_choice_gl.preset.c_str(), error.c_str());
  return false;
}

void gl_release() {
  if (g_chain_gl) library().gl_filter_chain_free(&g_chain_gl);
  g_chain_gl = nullptr;
  g_choice_gl = {};
}
#endif
}  // namespace gx::user_shader
