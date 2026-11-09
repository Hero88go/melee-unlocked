// RTX Remix bridge (local experiment, never in a release). See remix_bridge.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "remix_bridge.h"
#include "gx_core.h"
#include "gx_texture.h"
#include "rtx_lights.h"
#include "gx_fx_lights.h"
#include "texture_snapshot.h"
#include "host.h"
#include "stb/stb_image_write.h"

#if defined(_WIN32) && defined(MELEE_REMIX_SPIKE)
#define REMIX_BRIDGE_ON 1
#include <windows.h>
#include <d3d9.h>
#include <remix/remix_c.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace gx {

bool remix_requested() {
  static const bool on = [] { const char* v = std::getenv("MELEE_REMIX"); return v && v[0] == '1'; }();
  return on;
}
bool remix_visible() {
  static const bool on = [] { const char* v = std::getenv("MELEE_REMIX_SHOW"); return v && v[0] == '1'; }();
  return on;
}

#ifdef REMIX_BRIDGE_ON
namespace {

uint64_t fnv(const void* p, size_t n, uint64_t h = 1469598103934665603ull) {
  const uint8_t* b = (const uint8_t*)p;
  for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}

// An sRGB RGBA8 DDS (DX10 header), the format Remix reads albedo from.
bool write_dds(const std::filesystem::path& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba) {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  uint32_t header[32] = {};
  header[0] = 124;                                   // size
  header[1] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8;        // CAPS HEIGHT WIDTH PIXELFORMAT PITCH
  header[2] = h; header[3] = w; header[4] = w * 4;   // height, width, pitch
  header[6] = 1;                                     // mip count
  // The classic header (no DX10 block): 32-bit RGBA by channel masks. Remix 1.6.1 divided by zero on
  // the DX10 form.
  header[18] = 32;                                   // pixel format size
  header[19] = 0x40 | 0x1;                           // DDPF_RGB | DDPF_ALPHAPIXELS
  header[21] = 32;                                   // bits per pixel
  header[22] = 0x000000FF; header[23] = 0x0000FF00; header[24] = 0x00FF0000; header[25] = 0xFF000000;   // R G B A masks
  header[26] = 0x1000;                               // DDSCAPS_TEXTURE
  const uint32_t magic = 0x20534444;                 // 'DDS '
  f.write((const char*)&magic, 4);
  f.write((const char*)header, sizeof header);
  f.write((const char*)rgba.data(), (std::streamsize)rgba.size());
  return (bool)f;
}

LRESULT CALLBACK remix_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return DefWindowProcW(hwnd, msg, wp, lp); }

}  // namespace

struct RemixBridge::Impl {
  remixapi_Interface api{};
  HMODULE dll = nullptr;
  HWND hwnd = nullptr, readback_hwnd = nullptr;
  int width = 0, height = 0;
  std::filesystem::path tex_dir;
  // Meshes by content (a stage piece that does not move is the same mesh every frame), each with the
  // frame it was last drawn; materials by texture content.
  struct MeshEntry { remixapi_MeshHandle handle = nullptr; uint64_t last = 0; };
  std::unordered_map<uint64_t, MeshEntry> meshes;
  std::unordered_map<uint64_t, remixapi_MaterialHandle> materials;
  std::vector<remixapi_LightHandle> lights;
  uint64_t frame_no = 0;
  std::vector<remixapi_HardcodedVertex> verts;
  std::vector<uint32_t> indices, prim;
  std::vector<uint8_t> rgba;
  IDirect3D9Ex* d3d9 = nullptr;
  IDirect3DDevice9Ex* device = nullptr;
  // D3D9 mode: a Remix device on Remix's window presents each frame (Remix composites its
  // screen-space draws, the HUD, over the path-traced picture); else the API's own Present.
  bool d3d9_mode = false;
  IDirect3DTexture9* hud_tex = nullptr;
  int hud_w = 0, hud_h = 0;
  std::vector<uint8_t> hud;
  bool hud_new = false;
  std::string backbuffer_capture;   // D3D9 mode: save the next presented picture here
  bool child = false;                // drawing inside the game's window (the renderer setting)
  int shown = -1;                    // the window's visibility as show() last set it (-1 not yet)
  int place_x = -1, place_y = -1;    // where place() last put that window
  int want_w = 0, want_h = 0, want_frames = 0;   // a new picture size, held a few frames before the device is reset
  // This frame's bright effects (additive, carried by the layer) as lights.
  std::vector<FxLight> fx_lights;
  FxLightCollector fx;
  std::unordered_map<uint64_t, uint32_t> baked_mean;   // test logs: each baked material's mean colour
};

RemixBridge::RemixBridge() : impl_(new Impl) {}
RemixBridge::~RemixBridge() {
  if (started_ && impl_->api.Shutdown) {
    for (auto& m : impl_->meshes) impl_->api.DestroyMesh(m.second.handle);
    for (auto& m : impl_->materials) impl_->api.DestroyMaterial(m.second);
    for (auto l : impl_->lights) impl_->api.DestroyLight(l);
    if (impl_->hud_tex) impl_->hud_tex->Release();
    if (impl_->device) impl_->device->Release();
    if (impl_->d3d9) impl_->d3d9->Release();
    // D3D9 mode never called Startup: releasing the device ends Remix's frame loop.
    if (!impl_->d3d9_mode) impl_->api.Shutdown();
  }
  if (impl_->hwnd) DestroyWindow(impl_->hwnd);
  if (impl_->readback_hwnd) DestroyWindow(impl_->readback_hwnd);
  delete impl_;
}

bool RemixBridge::start(int width, int height, bool hidden, std::string* error, void* parent) {
  Impl& m = *impl_;
  std::wstring dir;
  if (const wchar_t* v = _wgetenv(L"MELEE_REMIX_DIR")) dir = v;
  else {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    dir = std::filesystem::path(exe).parent_path() / L"Remix";
  }
  // The runtime's own DLLs (DLSS, NRD, USD) sit beside it and load as it starts and later. A Remix run
  // loads none of the game's own DLSS, XeSS or FSR (Streamline is not started), so nothing else is affected.
  SetDllDirectoryW(dir.c_str());
  // The picture defaults chosen from the 11-08 comparison: fixed exposure (Remix's automatic
  // exposure brightened dark stages and dimmed bright ones away from the game's look), and the HUD
  // quad as UI. A config file the runtime reads at start (these options are not applied later);
  // MELEE_REMIX_CONF="key = value;..." adds to it, an rtx.conf beside the game still overrides it,
  // and an existing DXVK_CONFIG_FILE wins outright.
  if (!_wgetenv(L"DXVK_CONFIG_FILE")) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    const std::filesystem::path conf = std::filesystem::path(tmp) / L"melee-remix.conf";
    std::string text = "rtx.orthographicIsUI = True\n";   // exposure: Remix's automatic one (he judged fixed worse, 11-09)
    if (const char* v = std::getenv("MELEE_REMIX_CONF"))
      for (const char* c = v; *c; ++c) text += *c == ';' ? '\n' : *c;
    if (FILE* f = _wfopen(conf.c_str(), L"wb")) {
      std::fwrite(text.data(), 1, text.size(), f);
      std::fputc('\n', f);
      std::fclose(f);
      _wputenv_s(L"DXVK_CONFIG_FILE", conf.c_str());
    }
  }
  const std::wstring dll = (std::filesystem::path(dir) / L"d3d9.dll").wstring();
  const remixapi_ErrorCode rc = remixapi_lib_loadRemixDllAndInitialize(dll.c_str(), &m.api, &m.dll);
  if (rc != REMIXAPI_ERROR_CODE_SUCCESS) { if (error) *error = "loading the Remix runtime failed (" + std::to_string((int)rc) + ")"; return false; }
  WNDCLASSW wc{};
  wc.lpfnWndProc = remix_wndproc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"MeleeUnlockedRemix";
  RegisterClassW(&wc);
  if (parent) {
    // Inside the game's window, over its picture (place() keeps it there). Disabled, so the mouse and
    // the keyboard stay with the game's window under it.
    m.hwnd = CreateWindowExW(WS_EX_NOPARENTNOTIFY, wc.lpszClassName, L"", WS_CHILD | WS_DISABLED | WS_CLIPSIBLINGS,
                             0, 0, width, height, (HWND)parent, nullptr, wc.hInstance, nullptr);
  } else {
    RECT r{0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    m.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Melee Unlocked: RTX Remix", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
  }
  if (!m.hwnd) { if (error) *error = "no window for Remix"; return false; }
  m.child = parent != nullptr;
  if (!hidden) ShowWindow(m.hwnd, SW_SHOWNOACTIVATE);
  m.width = width; m.height = height;
  // D3D9 mode (MELEE_REMIX_API_PRESENT=1 keeps the API's own presenter): the documented D3D9
  // interop, a device from the runtime registered with it, presenting on Remix's window.
  if (!_wgetenv(L"MELEE_REMIX_API_PRESENT") && m.api.dxvk_CreateD3D9 && m.api.dxvk_RegisterD3D9Device &&
      m.api.dxvk_CreateD3D9(0, &m.d3d9) == REMIXAPI_ERROR_CODE_SUCCESS && m.d3d9) {
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = m.hwnd;
    pp.BackBufferWidth = (UINT)width; pp.BackBufferHeight = (UINT)height; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT hr = m.d3d9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m.hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                                        &pp, nullptr, &m.device);
    const remixapi_ErrorCode reg = SUCCEEDED(hr) ? m.api.dxvk_RegisterD3D9Device(m.device) : REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    m.d3d9_mode = SUCCEEDED(hr) && reg == REMIXAPI_ERROR_CODE_SUCCESS;
    host::log("remix: D3D9 mode %s (device %08X, register %d)", m.d3d9_mode ? "on" : "failed", (unsigned)hr, (int)reg);
    if (!m.d3d9_mode) { if (error) *error = "Remix D3D9 device failed"; return false; }
  }
  remixapi_StartupInfo info{};
  info.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
  info.hwnd = m.hwnd;
  info.disableSrgbConversionForOutput = 0;
  info.forceNoVkSwapchain = 0;
  info.editorModeEnabled = 0;
  info.combineGuiInFinalColor = 1;
  const remixapi_ErrorCode s = m.d3d9_mode ? REMIXAPI_ERROR_CODE_SUCCESS : m.api.Startup(&info);
  if (s != REMIXAPI_ERROR_CODE_SUCCESS) { if (error) *error = "Remix startup failed (" + std::to_string((int)s) + ")"; return false; }
  // Melee's vertex colours tint surfaces (backdrops, stage pieces); they are not baked lighting to drop.
  if (m.api.SetConfigVariable) {
    const remixapi_ErrorCode a = m.api.SetConfigVariable("rtx.vertexColorIsBakedLighting", "False");
    const remixapi_ErrorCode b = m.api.SetConfigVariable("rtx.vertexColorStrength", "1.0");
    host::log("remix: vertex colours as colour (%d %d)", (int)a, (int)b);
  }
  // A D3D9 device on the runtime, only for reading the finished picture back (captures).
  // Off by default: Remix 1.6.1 divides by zero after a second swap chain appears. Captures use Remix's
  // own DXVK_DUMP_SCREENSHOT_FRAME instead.
  if (!m.d3d9_mode && _wgetenv(L"MELEE_REMIX_D3D9_READBACK") && m.api.dxvk_CreateD3D9 && m.api.dxvk_CreateD3D9(0, &m.d3d9) == REMIXAPI_ERROR_CODE_SUCCESS && m.d3d9) {
    // Its own small hidden window: a swap chain on Remix's window would take it from Remix's presenter.
    m.readback_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Melee Unlocked: Remix readback", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                                      nullptr, nullptr, wc.hInstance, nullptr);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = m.readback_hwnd;
    pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    if (!m.readback_hwnd || FAILED(m.d3d9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, m.readback_hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr, &m.device)))
      m.device = nullptr;
    else if (m.api.dxvk_RegisterD3D9Device) m.api.dxvk_RegisterD3D9Device(m.device);
  }
  wchar_t tmp[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, tmp);
  m.tex_dir = std::filesystem::path(tmp) / L"melee-remix-textures";
  if (const wchar_t* v = _wgetenv(L"MELEE_REMIX_TEXTURES")) m.tex_dir = v;
  std::error_code ec;
  std::filesystem::create_directories(m.tex_dir, ec);
  started_ = true;
  host::log("remix: runtime started (%ls), window %dx%d%s, capture %s", dll.c_str(), width, height, hidden ? " hidden" : "",
            m.device ? "ready" : "unavailable");
  return true;
}

void RemixBridge::frame(const Frame& f, int camera, const float vw[12], const std::vector<RtxLightRecord>& lights,
                        const std::vector<uint8_t>& layer) {
  if (!started_ || camera < 0 || (size_t)camera >= f.rtx.cameras.size()) return;
  Impl& m = *impl_;
  const auto t0 = std::chrono::steady_clock::now();
  ++m.frame_no;
  stats_ = Stats{};
  const RtxCamera& cam = f.rtx.cameras[(size_t)camera];
  // Bisecting switches (tests): MELEE_REMIX_SKIP=textures,meshes,lights,camera.
  static const std::string skip = [] { const char* v = std::getenv("MELEE_REMIX_SKIP"); return std::string(v ? v : ""); }();
  const bool no_textures = skip.find("textures") != std::string::npos, no_meshes = skip.find("meshes") != std::string::npos;
  const bool no_lights = skip.find("lights") != std::string::npos, no_camera = skip.find("camera") != std::string::npos;

  // The camera: eye, the axes of view_to_world (columns), the game's field of view and planes.
  if (!no_camera) {
    remixapi_CameraInfoParameterizedEXT p{};
    p.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT;
    p.position = {vw[3], vw[7], vw[11]};
    p.right = {vw[0], vw[4], vw[8]};
    p.up = {vw[1], vw[5], vw[9]};
    p.forward = {-vw[2], -vw[6], -vw[10]};   // GX views look down -z
    // The projection the scene is drawn with (XF 0x20: x scale, x shift, y scale, ...): it carries the
    // match camera's zoom, which the camera object's nominal field of view does not.
    p.fovYInDegrees = 30.0f;
    p.aspect = (float)m.width / (float)std::max(1, m.height);
    for (const DrawCall& d : f.draws) {
      if (d.rtx.camera != camera || d.xf_regs[0x26] != 0) continue;
      const float* proj = (const float*)&d.xf_regs[0x20];
      if (proj[2] > 0.01f && proj[0] > 0.01f) {
        p.fovYInDegrees = 2.0f * std::atan(1.0f / proj[2]) * 57.2957795f;
        p.aspect = proj[2] / proj[0];
      }
      break;
    }
    p.nearPlane = std::max(cam.near_plane, 0.1f);
    p.farPlane = std::max(cam.far_plane, p.nearPlane + 10.0f);
    remixapi_CameraInfo c{};
    c.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
    c.pNext = &p;
    c.type = REMIXAPI_CAMERA_TYPE_WORLD;
    m.api.SetupCamera(&c);
  }

  m.fx_lights.clear();
  // Draws: world-space vertices from the GX matrices, one surface per draw, cached by content.
  for (const DrawCall& d : f.draws) {
    if (no_meshes || d.rtx.camera != camera || d.vertex_count < 3) continue;
    const uint32_t bm = d.bp.blendmode();
    const bool blended = bm & 1;
    const bool additive = blended && ((bm >> 5) & 7) == 1;   // destination factor ONE: glows, fire, sparks
    // Fighters: their additive pass is painted shine (an environment map over the model), not a light;
    // Remix's own specular replaces it.
    const bool fighter = d.skinned && d.owner_player < 6;
    if (additive && fighter) continue;
    // Effects and live screens reach Remix's picture through the layer the renderer composes over it,
    // exactly as the game drew them (`layer`, D3D12Backend::remix_layer_draw). The bright effects
    // (additive: fire, sparks, flashes, glows) also light the scene, at their place in their own colour.
    const size_t draw_index = (size_t)(&d - f.draws.data());
    const uint8_t in_layer = draw_index < layer.size() ? layer[draw_index] : 0;
    if (in_layer == 1) {
      if (additive && !no_lights && remix_effect_draw(d)) m.fx.add(f, d, vw, m.fx_lights);
      // Test runs (MELEE_REMIX_FXLOG=<sim frame>): each effect draw's owner and GX blend/depth state.
      static const uint64_t fx_log = [] { const char* v = std::getenv("MELEE_REMIX_FXLOG"); return v ? std::strtoull(v, nullptr, 10) : 0ull; }();
      if (fx_log && f.sequence == fx_log)
        host::log("remix fx: #%zu layer %u owner %02X pass %u bm %04X zm %02X at %06X verts %u tex %ux%u fmt %u", draw_index, (unsigned)in_layer,
                  (unsigned)d.owner_player, (unsigned)d.rtx.pass, d.bp.blendmode() & 0xFFFF, d.bp.zmode() & 0x1F,
                  d.bp.alpha_test() & 0xFFFFFF, d.vertex_count, d.textures[0].width, d.textures[0].height, d.textures[0].format);
      continue;
    }
    // Additive draws (glows, the fountain's light, sparks) come in as emissive cut-outs.
    // Blended surfaces (leaves, the sky's cut-out clouds, decals) and alpha-tested ones become cutouts.
    const uint32_t at = d.bp.alpha_test();
    const uint32_t comp0 = (at >> 16) & 7, comp1 = (at >> 19) & 7, logic = (at >> 22) & 3;
    const bool alpha_always = logic == 0 ? (comp0 == 7 && comp1 == 7) : logic == 1 ? (comp0 == 7 || comp1 == 7) : false;
    const bool cutout = blended || !alpha_always;
    // The game's own alpha test (GX and Remix number their compares alike: never, less, equal, less or
    // equal, greater, not equal, greater or equal, always); of two real compares the first. A blended
    // draw cuts at 96 at least, so its soft edges do not come out as solid halos.
    int test_type = 7;
    uint32_t test_ref = 0;
    if (!alpha_always) {
      const bool second = logic == 0 && comp0 == 7;
      test_type = (int)(second ? comp1 : comp0);
      test_ref = second ? (at >> 8) & 0xFF : at & 0xFF;
    }
    // Blended draws (leaves, clouds) as cut-outs at a low reference (MELEE_REMIX_CUTOUT_REF, 24): at 96
    // the soft edges of Dream Land's and Yoshi's Story's leaves came out as holes.
    static const uint32_t cut_ref = [] { const char* v = std::getenv("MELEE_REMIX_CUTOUT_REF"); return v ? (uint32_t)std::atoi(v) : 24u; }();
    if (blended && (test_type == 7 || ((test_type == 4 || test_type == 6) && test_ref < cut_ref))) { test_type = 4; test_ref = cut_ref; }
    const uint32_t test_key = (uint32_t)test_type << 8 | test_ref;
    // GX does not light this draw: its light is painted into its texture and vertex colours (most of a
    // stage). Its texture also emits, so Remix shows it as painted and adds its own light on top
    // (MELEE_REMIX_EMISSIVE scales it; additive glows emit more).
    const uint32_t chan = d.xf_regs[0x0E];
    const bool unlit = (d.xf_regs[0x09] & 3) == 0 || ((chan >> 1) & 1) == 0;
    static const float emissive_scale = [] { const char* v = std::getenv("MELEE_REMIX_EMISSIVE"); return v ? (float)std::atof(v) : 0.8f; }();   // 0.8: the 11-06 look he liked
    // A fighter glows a little with its own colours (MELEE_REMIX_FIGHTER_GLOW), so it reads on dark
    // stages as it does in the game, with Remix's light and shadow on top.
    static const float fighter_glow = [] { const char* v = std::getenv("MELEE_REMIX_FIGHTER_GLOW"); return v ? (float)std::atof(v) : 0.35f; }();
    // A lit stage piece whose combiner reads a GX specular channel (Final Destination's blue panels under
    // its two side lights) shows that light as its colour in the game: it glows like painted art, or
    // Remix's own lights leave it dark.
    const bool gx_specular = [&] {
      if (unlit || fighter) return false;
      for (uint32_t j = 0; j < 2 && j < (d.xf_regs[0x09] & 3); ++j) {
        const uint32_t cc = d.xf_regs[0x0E + j];
        if (!lit_enable(cc) || lit_attnfunc(cc) != 1) continue;
        for (uint32_t n = 0; n <= d.bp.numtevstages() && n < 16; ++n) {
          if ((uint32_t)d.bp.order_colorchan((int)n) != j) continue;
          const uint32_t tc = d.bp.tev_color((int)n);
          for (int sh = 0; sh < 16; sh += 4) { const uint32_t sel = (tc >> sh) & 15; if (sel == 10 || sel == 11) return true; }
        }
      }
      return false;
    }();
    // MELEE_REMIX_GX_SPECULAR_GLOW=1 (off by default since 11-08 evening: it turned FD's panels to noise).
    // FD's blue panels keep their colour with a low glow (MELEE_REMIX_GX_SPECULAR_GLOW, 0.3): at full
    // strength they lit like lamps and came out noisy.
    static const float gx_specular_glow = [] { const char* v = std::getenv("MELEE_REMIX_GX_SPECULAR_GLOW"); return v ? (float)std::atof(v) : 0.3f; }();
    const float emissive = additive ? 2.0f * emissive_scale : unlit && d.owner_player >= 6 ? emissive_scale
                           : gx_specular && d.owner_player >= 6 ? gx_specular_glow : fighter ? fighter_glow : 0.0f;
    m.prim.clear();
    if (d.segment_count) {
      for (uint32_t i = 0; i < d.segment_count; ++i) {
        const auto& seg = f.segments.at(d.first_segment + i);
        append_segment_indices(m.prim, seg.primitive, seg.vertex_count, seg.first_vertex - d.first_vertex);
      }
    } else append_segment_indices(m.prim, d.primitive, d.vertex_count, 0);
    if (m.prim.size() < 3 || d.first_vertex + (size_t)d.vertex_count > f.vertices.size()) continue;
    m.verts.resize(d.vertex_count);
    for (uint32_t i = 0; i < d.vertex_count; ++i) {
      const Vertex& v = f.vertices[d.first_vertex + i];
      const int row = (int)(v.posmtx & 0x3F);
      if (row + 2 >= 64) continue;
      const float* pm = &d.posMatrices[row * 4];
      float view[3];
      for (int k = 0; k < 3; ++k) view[k] = pm[k * 4] * v.pos[0] + pm[k * 4 + 1] * v.pos[1] + pm[k * 4 + 2] * v.pos[2] + pm[k * 4 + 3];
      remixapi_HardcodedVertex& o = m.verts[i];
      o = {};
      for (int k = 0; k < 3; ++k) o.position[k] = vw[k * 4] * view[0] + vw[k * 4 + 1] * view[1] + vw[k * 4 + 2] * view[2] + vw[k * 4 + 3];
      const float* nm = &d.normalMatrices[(row % 32) * 3];
      float nv[3];
      for (int k = 0; k < 3; ++k) nv[k] = nm[k * 3] * v.nrm[0] + nm[k * 3 + 1] * v.nrm[1] + nm[k * 3 + 2] * v.nrm[2];
      float nw[3];
      for (int k = 0; k < 3; ++k) nw[k] = vw[k * 4] * nv[0] + vw[k * 4 + 1] * nv[1] + vw[k * 4 + 2] * nv[2];
      const float len = std::sqrt(nw[0] * nw[0] + nw[1] * nw[1] + nw[2] * nw[2]);
      for (int k = 0; k < 3; ++k) o.normal[k] = len > 1e-6f ? nw[k] / len : (k == 1 ? 1.0f : 0.0f);
      o.texcoord[0] = v.uv[0][0]; o.texcoord[1] = v.uv[0][1];
      // The game's vertex colour (Melee tints backdrops and baked shading with it), D3D9 order 0xAARRGGBB.
      static const bool no_vc = std::getenv("MELEE_REMIX_NO_VERTEX_COLOR") != nullptr;
      o.color = no_vc || !(d.components & VB_HAS_COL0) ? 0xFFFFFFFFu
              : ((uint32_t)v.col0[3] << 24) | ((uint32_t)v.col0[0] << 16) | ((uint32_t)v.col0[1] << 8) | v.col0[2];
    }
    // The draw's colour source, which GX multiplies its texture by: the vertex colours (their average)
    // or the material colour register (XF 0x0C, RGBA from the high byte). Melee's textures are often
    // grey and get their colour here (Final Destination's lines, Fountain of Dreams' pastels).
    float tint[3] = {1.0f, 1.0f, 1.0f};
    {
      const bool from_vertex = (chan & 1) != 0 && (d.components & VB_HAS_COL0);
      if (from_vertex) {
        double sum[3] = {0, 0, 0};
        for (uint32_t i = 0; i < d.vertex_count; ++i)
          for (int k = 0; k < 3; ++k) sum[k] += f.vertices[d.first_vertex + i].col0[k];
        for (int k = 0; k < 3; ++k) tint[k] = (float)(sum[k] / d.vertex_count / 255.0);
      } else {
        const uint32_t mat = d.xf_regs[0x0C];
        tint[0] = ((mat >> 24) & 0xFF) / 255.0f; tint[1] = ((mat >> 16) & 0xFF) / 255.0f; tint[2] = ((mat >> 8) & 0xFF) / 255.0f;
      }
      // A lit draw: GX's colour is its ambient register plus the material colour under the lights.
      // HSD keeps a fighter's skin and cloth tone in the ambient colour, so the average lit colour
      // (light at about 0.7 across the surface) is what its combiner sees.
      if (!unlit && !fighter) {
        const uint32_t amb = d.xf_regs[0x0A];
        const float a[3] = {((amb >> 24) & 0xFF) / 255.0f, ((amb >> 16) & 0xFF) / 255.0f, ((amb >> 8) & 0xFF) / 255.0f};
        for (int k = 0; k < 3; ++k) tint[k] = std::min(1.0f, a[k] + 0.7f * tint[k]);
      }
      static const bool no_tint = std::getenv("MELEE_REMIX_NO_TINT") != nullptr;
      if (no_tint) tint[0] = tint[1] = tint[2] = 1.0f;
    }
    const uint32_t tint_q = ((uint32_t)(tint[0] * 63.0f + 0.5f) << 12) | ((uint32_t)(tint[1] * 63.0f + 0.5f) << 6) | (uint32_t)(tint[2] * 63.0f + 0.5f);
    // Its material: the first texture times that colour, written once as a DDS Remix reads.
    uint64_t mat_key = 0;
    const TextureRef& t = d.textures[0];
    if (!no_textures && t.used && t.data && t.width && t.height && t.width <= 1024 && t.height <= 1024) {
      mat_key = fnv(&t.data->hash, 8, fnv(&t.format, 4, fnv(&t.mode0, 4, fnv(&emissive, 4, fnv(&tint_q, 4, fnv(&test_key, 4, cutout ? 0x9E3779B97F4A7C15ull : 1469598103934665603ull))))));
      if (!m.materials.count(mat_key)) {
        decode_texture(t.data->image.data(), t.width, t.height, t.format, t.data->palette.empty() ? nullptr : t.data->palette.data(),
                       t.tlut_format, m.rgba);
        // Each texel through the draw's colour combiner (MELEE_REMIX_NO_TEV: the texture times its colour source).
        static const bool no_tev = std::getenv("MELEE_REMIX_NO_TEV") != nullptr;
        const int own_map = [&] { for (uint32_t n = 0; n <= d.bp.numtevstages() && n < 16; ++n) if (d.bp.order_enable((int)n)) return d.bp.order_texmap((int)n); return 0; }();
        const float ras[4] = {tint[0] * 255.0f, tint[1] * 255.0f, tint[2] * 255.0f, 255.0f};
        // A combiner that mixes several textures (Pokemon Stadium's grass over its markings) cannot be
        // baked from one: such a draw keeps its first texture times its colour, as at first.
        const bool multi_map = [&] {
          for (uint32_t n = 0; n <= d.bp.numtevstages() && n < 16; ++n)
            if (d.bp.order_enable((int)n) && d.bp.order_texmap((int)n) != own_map) return true;
          return false;
        }();
        for (size_t i = 0; i + 3 < m.rgba.size(); i += 4) {
          // Fighters: texture x material. Their later combiner stages blend toward white by a register
          // that only means something with GX lighting (baked flat it turns them pastel).
          if (no_tev || fighter) { for (int k = 0; k < 3; ++k) m.rgba[i + k] = (uint8_t)std::min(255.0f, m.rgba[i + k] * tint[k] + 0.5f); continue; }
          const float texel[4] = {(float)m.rgba[i], (float)m.rgba[i + 1], (float)m.rgba[i + 2], (float)m.rgba[i + 3]};
          float o[4];
          tev_texel(d, own_map, texel, ras, o);
          for (int k = 0; k < 4; ++k) m.rgba[i + k] = (uint8_t)(o[k] + 0.5f);
        }
        // A multi-texture combiner baked from one texture can come out blank white (the other map read
        // as white): then the first texture times its colour, as at first.
        if (multi_map && !no_tev && !fighter && !m.rgba.empty()) {
          double sum = 0.0;
          for (size_t i = 0; i + 3 < m.rgba.size(); i += 4) sum += m.rgba[i] + m.rgba[i + 1] + m.rgba[i + 2];
          if (sum / (m.rgba.size() / 4 * 3.0) > 215.0) {
            decode_texture(t.data->image.data(), t.width, t.height, t.format, t.data->palette.empty() ? nullptr : t.data->palette.data(),
                           t.tlut_format, m.rgba);
            for (size_t i = 0; i + 3 < m.rgba.size(); i += 4)
              for (int k = 0; k < 3; ++k) m.rgba[i + k] = (uint8_t)std::min(255.0f, m.rgba[i + k] * tint[k] + 0.5f);
          }
        }
        // How shiny it is, as our path tracer decides (rtx_material_params, rtx_direct.hlsl
        // polished_floor): the game's own specular and shininess; an environment-mapped (shiny) stage
        // material; or a dark, grey stage surface (Final Destination's stone, metal decks), polished.
        float rough = 0.85f;
        {
          const RtxMaterial* mat = d.rtx.material >= 0 && (size_t)d.rtx.material < f.rtx.materials.size()
                                       ? &f.rtx.materials[(size_t)d.rtx.material] : nullptr;
          uint32_t spec = 0;
          rtx_material_params(mat, &spec, &rough);
          if (!fighter && !additive) {
            bool env_mapped = false;
            for (uint32_t k = 0; mat && k < mat->tobj_count && k < 8; ++k) env_mapped = env_mapped || mat->tobjs[k].coord == 1;
            double sum[3] = {0, 0, 0};
            size_t n = 0;
            for (size_t i = 0; i + 3 < m.rgba.size(); i += 16, ++n)
              for (int k = 0; k < 3; ++k) { const float c = m.rgba[i + k] / 255.0f; sum[k] += c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
            const float a[3] = {(float)(sum[0] / std::max<size_t>(n, 1)), (float)(sum[1] / std::max<size_t>(n, 1)), (float)(sum[2] / std::max<size_t>(n, 1))};
            const float lum = 0.2126f * a[0] + 0.7152f * a[1] + 0.0722f * a[2];
            const float sat = std::max({a[0], a[1], a[2]}) - std::min({a[0], a[1], a[2]});
            const float polished = std::clamp((0.30f - lum) / 0.15f, 0.0f, 1.0f) * std::clamp((0.20f - sat) / 0.10f, 0.0f, 1.0f);
            if (env_mapped) rough = std::min(rough, 0.2f);
            else if (polished > 0.5f) rough = std::min(rough, 0.12f);
            // Stage art GX does not light shows no highlight in the game, whatever its material's
            // specular says: matte (Yoshi's Story's felt trees went pale with the sky's reflection).
            // Only cut-out foliage (MELEE_REMIX_MATTE_ALL=1: every unlit piece, the 11-08 rule that took the platforms' shine).
            else if (unlit && (cutout || std::getenv("MELEE_REMIX_MATTE_ALL"))) rough = std::max(rough, 0.85f);
          }
        }
        static const float rough_scale = [] { const char* v = std::getenv("MELEE_REMIX_ROUGHNESS"); return v ? (float)std::atof(v) : 1.0f; }();
        rough = std::clamp(rough * rough_scale, 0.02f, 1.0f);
        char name[40];
        // PNG: Remix 1.6.1's loader divided by zero on these uncompressed DDS files (MELEE_REMIX_DDS=1 keeps DDS).
        static const bool dds = std::getenv("MELEE_REMIX_DDS") != nullptr;
        std::snprintf(name, sizeof name, dds ? "%016llx.dds" : "%016llx.png", (unsigned long long)mat_key);
        const std::filesystem::path path = m.tex_dir / name;
        if (!std::filesystem::exists(path)) {
          if (dds) write_dds(path, t.width, t.height, m.rgba);
          else stbi_write_png(path.u8string().c_str(), (int)t.width, (int)t.height, 4, m.rgba.data(), (int)t.width * 4);
        }
        const std::wstring wpath = path.wstring();
        remixapi_MaterialInfoOpaqueEXT op{};
        op.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
        op.albedoConstant = {1, 1, 1};
        op.opacityConstant = 1.0f;
        op.roughnessConstant = rough;
        op.metallicConstant = 0.0f;
        op.alphaTestType = test_type;
        op.alphaReferenceValue = (uint8_t)test_ref;
        remixapi_MaterialInfo mi{};
        mi.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
        mi.pNext = &op;
        mi.hash = mat_key;
        mi.albedoTexture = wpath.c_str();
        mi.emissiveTexture = emissive > 0.0f ? wpath.c_str() : nullptr;
        mi.emissiveIntensity = emissive;
        mi.emissiveColorConstant = {1.0f, 1.0f, 1.0f};
        mi.filterMode = (uint8_t)((t.mode0 >> 4) & 1);   // the game's magnification filter (0 nearest: pixel-art stages)
        const uint32_t wrap_s = t.mode0 & 3, wrap_t = (t.mode0 >> 2) & 3;
        mi.wrapModeU = (uint8_t)(wrap_s == 0 ? 2 : wrap_s == 2 ? 1 : 0);   // GX clamp/repeat/mirror -> clamp/mirror/repeat ids
        mi.wrapModeV = (uint8_t)(wrap_t == 0 ? 2 : wrap_t == 2 ? 1 : 0);
        remixapi_MaterialHandle h = nullptr;
        {
          double sum[4] = {0, 0, 0, 0};
          const size_t n = m.rgba.size() / 4;
          for (size_t i = 0; i < n; ++i) for (int k = 0; k < 4; ++k) sum[k] += m.rgba[i * 4 + k];
          if (n) m.baked_mean[mat_key] = ((uint32_t)(sum[0] / n) << 24) | ((uint32_t)(sum[1] / n) << 16) | ((uint32_t)(sum[2] / n) << 8) | (uint32_t)(sum[3] / n);
        }
        if (m.api.CreateMaterial(&mi, &h) == REMIXAPI_ERROR_CODE_SUCCESS) m.materials[mat_key] = h;
        else m.materials[mat_key] = nullptr;
      }
    }
    // No texture: what the combiner makes of the colour source and its constant and register colours
    // (Final Destination's magenta and orange lines are a constant colour on an untextured draw), as a
    // constant. Fighters keep their colour source, as their textures do.
    if (!mat_key) {
      static const bool no_tev = std::getenv("MELEE_REMIX_NO_TEV") != nullptr;
      float col[3] = {tint[0], tint[1], tint[2]};
      if (!no_tev && !fighter) {
        const float white[4] = {255.0f, 255.0f, 255.0f, 255.0f};
        const float ras[4] = {tint[0] * 255.0f, tint[1] * 255.0f, tint[2] * 255.0f, 255.0f};
        float o[4];
        tev_texel(d, -1, white, ras, o);   // no stage reads a texture here: texture inputs read white
        for (int k = 0; k < 3; ++k) col[k] = o[k] / 255.0f;
      }
      const uint32_t col_q = ((uint32_t)(col[0] * 63.0f + 0.5f) << 12) | ((uint32_t)(col[1] * 63.0f + 0.5f) << 6) | (uint32_t)(col[2] * 63.0f + 0.5f);
      mat_key = fnv(&col_q, 4, fnv(&emissive, 4, 0x51ED270B27ull));
      if (!m.materials.count(mat_key)) {
        remixapi_MaterialInfoOpaqueEXT op{};
        op.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
        op.albedoConstant = {col[0], col[1], col[2]};
        op.opacityConstant = 1.0f;
        op.roughnessConstant = 0.55f;
        op.alphaTestType = 7;
        remixapi_MaterialInfo mi{};
        mi.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
        mi.pNext = &op;
        mi.hash = mat_key;
        mi.emissiveIntensity = emissive;
        mi.emissiveColorConstant = {col[0], col[1], col[2]};
        m.baked_mean[mat_key] = ((uint32_t)(col[0] * 255.0f) << 24) | ((uint32_t)(col[1] * 255.0f) << 16) | ((uint32_t)(col[2] * 255.0f) << 8) | 255u;
        remixapi_MaterialHandle h = nullptr;
        m.materials[mat_key] = m.api.CreateMaterial(&mi, &h) == REMIXAPI_ERROR_CODE_SUCCESS ? h : nullptr;
      }
    }
    const remixapi_MaterialHandle material = m.materials[mat_key];
    // Test runs (MELEE_REMIX_DRAWLOG=<sim frame>): each scene draw Remix gets, with its colour sources
    // and combiner and the mean of the texture it was baked to.
    {
      static const uint64_t draw_log = [] { const char* v = std::getenv("MELEE_REMIX_DRAWLOG"); return v ? std::strtoull(v, nullptr, 10) : 0ull; }();
      if (draw_log && f.sequence == draw_log) {
        const auto bm_it = m.baked_mean.find(mat_key);
        std::string tev;
        char buf[64];
        for (uint32_t n = 0; n <= d.bp.numtevstages() && n < 16; ++n) {
          std::snprintf(buf, sizeof buf, " [%06X %06X k%d/%d t%d]", d.bp.tev_color((int)n) & 0xFFFFFF, d.bp.tev_alpha((int)n) & 0xFFFFFF,
                        d.bp.ksel_kc((int)n), d.bp.ksel_ka((int)n), d.bp.order_enable((int)n) ? d.bp.order_texmap((int)n) : -1);
          tev += buf;
        }
        host::log("remix draw: #%zu layer %u zm %02X bm %04X owner %02X unlit %d add %d cut %d emis %.2f chan %08X amb %08X mat %08X vc %d tint %.2f %.2f %.2f "
                  "tex %ux%u f%u baked %08X c0 %d,%d,%d k0 %d,%d,%d%s",
                  (size_t)(&d - f.draws.data()), (unsigned)in_layer, d.bp.zmode() & 0x1F, d.bp.blendmode() & 0xFFFF, (unsigned)d.owner_player, unlit ? 1 : 0, additive ? 1 : 0, cutout ? 1 : 0, emissive,
                  d.xf_regs[0x0E], d.xf_regs[0x0A], d.xf_regs[0x0C], (d.components & VB_HAS_COL0) ? 1 : 0, tint[0], tint[1], tint[2],
                  t.width, t.height, t.format, bm_it != m.baked_mean.end() ? bm_it->second : 0u,
                  d.tev_colors[1][0], d.tev_colors[1][1], d.tev_colors[1][2], d.tev_kcolors[0][0], d.tev_kcolors[0][1], d.tev_kcolors[0][2], tev.c_str());
      }
    }
    uint64_t key = fnv(m.verts.data(), m.verts.size() * sizeof(remixapi_HardcodedVertex));
    key = fnv(m.prim.data(), m.prim.size() * 4, key);
    key = fnv(&mat_key, 8, key);
    Impl::MeshEntry& e = m.meshes[key];
    if (!e.handle) {
      remixapi_MeshInfoSurfaceTriangles s{};
      s.vertices_values = m.verts.data(); s.vertices_count = m.verts.size();
      s.indices_values = m.prim.data(); s.indices_count = m.prim.size();
      s.material = material;
      remixapi_MeshInfo mi{};
      mi.sType = REMIXAPI_STRUCT_TYPE_MESH_INFO;
      mi.hash = key;
      mi.surfaces_values = &s; mi.surfaces_count = 1;
      if (m.api.CreateMesh(&mi, &e.handle) != REMIXAPI_ERROR_CODE_SUCCESS) { m.meshes.erase(key); continue; }
      ++stats_.created;
    }
    e.last = m.frame_no;
    remixapi_InstanceInfo inst{};
    inst.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
    inst.mesh = e.handle;
    inst.transform = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};
    inst.doubleSided = 1;
    // A blended stage piece that does not write depth lies on another surface (Whispy's face, signs,
    // painted marks): a decal, so Remix draws it over that surface instead of fighting it.
    {
      const uint32_t zm = d.bp.zmode();
      const bool writes_depth = (zm & 1) && ((zm >> 4) & 1);
      if (in_layer == 2) inst.categoryFlags |= REMIXAPI_INSTANCE_CATEGORY_BIT_SKY;   // shown by the layer; Remix's sky for reflections and light
      else if (blended && !additive && !writes_depth && d.owner_player >= 6) inst.categoryFlags |= REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_STATIC;
    }
    m.api.DrawInstance(&inst);
    ++stats_.instances;
  }
  // Meshes not drawn for a few frames (fighter poses already shown) go.
  for (auto it = m.meshes.begin(); it != m.meshes.end();) {
    if (m.frame_no - it->second.last > 3) { m.api.DestroyMesh(it->second.handle); it = m.meshes.erase(it); }
    else ++it;
  }

  // Lights: the stage's sun as a distant light, its point and spot lights as spheres, scaled up
  // from GX's 0..1 colours to Remix's physical units (MELEE_REMIX_LIGHT scales them all).
  for (auto l : m.lights) m.api.DestroyLight(l);
  m.lights.clear();
  static const float scale = [] { const char* v = std::getenv("MELEE_REMIX_LIGHT"); return v ? (float)std::atof(v) : 1.0f; }();
  uint64_t n = 0;
  for (const RtxLightRecord& l : lights) {
    if (no_lights) break;
    remixapi_LightInfo li{};
    li.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
    li.hash = 0x4D454C45ull + (++n);
    remixapi_LightInfoDistantEXT dist{};
    remixapi_LightInfoSphereEXT sph{};
    remixapi_LightInfoDomeEXT dome{};
    static std::wstring white;
    if (l.type == kRtxLightAmbient) {
      // The stage's ambient light (Melee lights most of a stage with it) as a dome around the scene:
      // a one-texel white map tinted by the light's colour (MELEE_REMIX_SKY scales it).
      if (white.empty()) {
        const std::filesystem::path path = m.tex_dir / L"melee-remix-white.dds";
        if (!std::filesystem::exists(path)) write_dds(path, 1, 1, std::vector<uint8_t>{255, 255, 255, 255});
        white = path.wstring();
      }
      static const float sky = [] { const char* v = std::getenv("MELEE_REMIX_SKY"); return v ? (float)std::atof(v) : 1.5f; }();
      dome.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT;
      dome.transform = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};
      dome.colorTexture = white.c_str();
      li.pNext = &dome;
      li.radiance = {l.radiance[0] * sky * scale, l.radiance[1] * sky * scale, l.radiance[2] * sky * scale};
    } else if (l.type == kRtxLightDirectional) {
      dist.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
      dist.direction = {l.direction[0], l.direction[1], l.direction[2]};
      dist.angularDiameterDegrees = 1.5f;
      dist.volumetricRadianceScale = 1.0f;
      li.pNext = &dist;
      li.radiance = {l.radiance[0] * 6.0f * scale, l.radiance[1] * 6.0f * scale, l.radiance[2] * 6.0f * scale};
    } else if (l.type == kRtxLightPoint || l.type == kRtxLightSpot) {
      sph.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
      sph.position = {l.position[0], l.position[1], l.position[2]};
      sph.radius = std::max(l.radius, 2.0f);
      sph.volumetricRadianceScale = 1.0f;
      li.pNext = &sph;
      li.radiance = {l.radiance[0] * 400.0f * scale, l.radiance[1] * 400.0f * scale, l.radiance[2] * 400.0f * scale};
    } else continue;
    remixapi_LightHandle h = nullptr;
    if (m.api.CreateLight(&li, &h) == REMIXAPI_ERROR_CODE_SUCCESS) { m.lights.push_back(h); m.api.DrawLightInstance(h); }
  }
  // No sun in the game's lights (some stages only have ambient light): a soft key light from above.
  if (m.lights.empty() && !no_lights) {
    remixapi_LightInfoDistantEXT dist{};
    dist.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
    dist.direction = {0.3f, -0.85f, -0.43f};
    dist.angularDiameterDegrees = 2.0f;
    dist.volumetricRadianceScale = 1.0f;
    remixapi_LightInfo li{};
    li.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
    li.pNext = &dist;
    li.hash = 0x4D454C45ull;
    li.radiance = {5.0f * scale, 4.8f * scale, 4.5f * scale};
    remixapi_LightHandle h = nullptr;
    if (m.api.CreateLight(&li, &h) == REMIXAPI_ERROR_CODE_SUCCESS) { m.lights.push_back(h); m.api.DrawLightInstance(h); }
  }
  // The brightest effects as small sphere lights (MELEE_REMIX_FX_LIGHTS caps the count, 0 turns them
  // off; MELEE_REMIX_FX_LIGHT scales their strength).
  {
    static const int fx_max = [] { const char* v = std::getenv("MELEE_REMIX_FX_LIGHTS"); return v ? std::atoi(v) : 8; }();
    static const float fx_scale = [] { const char* v = std::getenv("MELEE_REMIX_FX_LIGHT"); return v ? (float)std::atof(v) : 7.0f; }();
    float focus[3];
    if (fx_focus(f, camera, vw, focus)) {
      const float cx = vw[3] - focus[0], cy = vw[7] - focus[1], cz = vw[11] - focus[2];
      fx_keep_near(m.fx_lights, focus, std::max(150.0f, 1.5f * std::sqrt(cx * cx + cy * cy + cz * cz)), (size_t)std::max(fx_max, 0));
    } else {
      m.fx_lights.clear();
    }
    for (int i = 0; i < fx_max && i < (int)m.fx_lights.size(); ++i) {
      const FxLight& l = m.fx_lights[(size_t)i];
      if (l.power <= 0.01f) break;
      remixapi_LightInfoSphereEXT sph{};
      sph.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
      sph.position = {l.pos[0], l.pos[1], l.pos[2]};
      sph.radius = l.radius;
      sph.volumetricRadianceScale = 1.0f;
      remixapi_LightInfo li{};
      li.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
      li.pNext = &sph;
      li.hash = 0x46584C00ull + (uint64_t)i;
      li.radiance = {l.rgb[0] * fx_scale * scale, l.rgb[1] * fx_scale * scale, l.rgb[2] * fx_scale * scale};
      remixapi_LightHandle h = nullptr;
      if (m.api.CreateLight(&li, &h) == REMIXAPI_ERROR_CODE_SUCCESS) { m.lights.push_back(h); m.api.DrawLightInstance(h); ++stats_.fx_lights; }
    }
  }
  if (m.d3d9_mode && m.device) {
    present_d3d9();
  } else {
    remixapi_PresentInfo pi{};
    pi.sType = REMIXAPI_STRUCT_TYPE_PRESENT_INFO;
    m.api.Present(&pi);
  }
  stats_.meshes = (uint32_t)m.meshes.size();
  stats_.materials = (uint32_t)m.materials.size();
  stats_.lights = (uint32_t)m.lights.size();
  stats_.cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void RemixBridge::show(bool on) {
  Impl& m = *impl_;
  if (!m.hwnd || m.shown == (on ? 1 : 0)) return;
  ShowWindow(m.hwnd, on ? SW_SHOWNOACTIVATE : SW_HIDE);
  m.shown = on ? 1 : 0;
}

// The game's picture moved or changed size (window resized, 4:3 menus and widescreen matches): the
// window follows at once; the device's picture follows when the new size has held for a few frames
// (a reset rebuilds Remix's targets, so a drag does not reset on every step).
void RemixBridge::place(int x, int y, int w, int h) {
  Impl& m = *impl_;
  if (!m.child || !m.hwnd || w < 16 || h < 16) return;
  if (x != m.place_x || y != m.place_y || w != m.want_w || h != m.want_h) {
    SetWindowPos(m.hwnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
    m.place_x = x; m.place_y = y;
    if (w != m.want_w || h != m.want_h) { m.want_w = w; m.want_h = h; m.want_frames = 0; }
  }
  if ((w != m.width || h != m.height) && m.d3d9_mode && m.device && ++m.want_frames >= 10) {
    if (m.hud_tex) { m.hud_tex->Release(); m.hud_tex = nullptr; }
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = m.hwnd;
    pp.BackBufferWidth = (UINT)w; pp.BackBufferHeight = (UINT)h; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    const HRESULT hr = m.device->ResetEx(&pp, nullptr);
    host::log("remix: picture %dx%d -> %dx%d (%08X)", m.width, m.height, w, h, (unsigned)hr);
    if (SUCCEEDED(hr)) { m.width = w; m.height = h; m.hud_new = !m.hud.empty(); }
    m.want_frames = 0;
  }
}

void RemixBridge::present_d3d9() {
  Impl& m = *impl_;
    // The HUD: one screen-space textured quad over the whole picture (Remix treats pre-transformed
    // draws as UI and composites them after path tracing).
    IDirect3DDevice9Ex* dev = m.device;
    dev->BeginScene();
    if (m.hud_new && m.hud_w > 0 && m.hud_h > 0) {
      if (m.hud_tex) {
        D3DSURFACE_DESC desc{};
        m.hud_tex->GetLevelDesc(0, &desc);
        if ((int)desc.Width != m.hud_w || (int)desc.Height != m.hud_h) { m.hud_tex->Release(); m.hud_tex = nullptr; }
      }
      if (!m.hud_tex) dev->CreateTexture((UINT)m.hud_w, (UINT)m.hud_h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m.hud_tex, nullptr);
      D3DLOCKED_RECT lr{};
      if (m.hud_tex && SUCCEEDED(m.hud_tex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD))) {
        for (int y = 0; y < m.hud_h; ++y) std::memcpy((uint8_t*)lr.pBits + (size_t)y * lr.Pitch, m.hud.data() + (size_t)y * m.hud_w * 4, (size_t)m.hud_w * 4);
        m.hud_tex->UnlockRect(0);
      }
      m.hud_new = false;
    }
    if (m.hud_tex) {
      struct V { float x, y, z, rhw, u, v; };
      const float w = (float)m.width, h = (float)m.height;
      const V quad[4] = {{-0.5f, -0.5f, 0, 1, 0, 0}, {w - 0.5f, -0.5f, 0, 1, 1, 0}, {-0.5f, h - 0.5f, 0, 1, 0, 1}, {w - 0.5f, h - 0.5f, 0, 1, 1, 1}};
      dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
      dev->SetTexture(0, m.hud_tex);
      dev->SetRenderState(D3DRS_ZENABLE, FALSE);
      dev->SetRenderState(D3DRS_LIGHTING, FALSE);
      dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
      dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
      dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);   // premultiplied: the HUD over the effects (gx_d3d12.cpp)
      dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
      dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
      dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
      dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
      dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
      dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
      dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
      const HRESULT drew = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(V));
      static int drawn_logged = 0;
      if (drawn_logged++ < 2) host::log("remix: HUD layer drawn (%08X)", (unsigned)drew);
    }
    dev->EndScene();
    if (!m.backbuffer_capture.empty()) {
      // The layer this present laid over the picture (<capture>.layer.png, premultiplied RGBA).
      if (m.hud_w > 0 && m.hud_h > 0 && m.hud.size() >= (size_t)m.hud_w * m.hud_h * 4) {
        std::vector<uint8_t> rgba(m.hud.begin(), m.hud.begin() + (size_t)m.hud_w * m.hud_h * 4);
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) std::swap(rgba[i], rgba[i + 2]);
        stbi_write_png((m.backbuffer_capture + ".layer.png").c_str(), m.hud_w, m.hud_h, 4, rgba.data(), m.hud_w * 4);
      }
      // What goes to the screen: the back buffer as Remix leaves it before present.
      IDirect3DSurface9* bb = nullptr;
      IDirect3DSurface9* sys = nullptr;
      D3DSURFACE_DESC desc{};
      bool ok = SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && SUCCEEDED(bb->GetDesc(&desc)) &&
                SUCCEEDED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
                SUCCEEDED(dev->GetRenderTargetData(bb, sys));
      D3DLOCKED_RECT lr{};
      if (ok) ok = SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY));
      if (ok) {
        std::ofstream out(m.backbuffer_capture, std::ios::binary);
        out << "P6\n" << desc.Width << " " << desc.Height << "\n255\n";
        std::vector<uint8_t> row(desc.Width * 3);
        for (UINT y = 0; y < desc.Height; ++y) {
          const uint8_t* p = (const uint8_t*)lr.pBits + (size_t)y * lr.Pitch;
          for (UINT x = 0; x < desc.Width; ++x) { row[x * 3] = p[x * 4 + 2]; row[x * 3 + 1] = p[x * 4 + 1]; row[x * 3 + 2] = p[x * 4]; }
          out.write((const char*)row.data(), (std::streamsize)row.size());
        }
        sys->UnlockRect();
      }
      host::log("remix: back buffer %s %s (format %d)", m.backbuffer_capture.c_str(), ok ? "saved" : "failed", (int)desc.Format);
      if (sys) sys->Release();
      if (bb) bb->Release();
      m.backbuffer_capture.clear();
    }
    dev->PresentEx(nullptr, nullptr, nullptr, nullptr, 0);
}

void RemixBridge::present_2d() {
  if (!started_ || !impl_->d3d9_mode || !impl_->device) return;
  present_d3d9();
}

void RemixBridge::set_hud(const uint8_t* bgra, int w, int h) {
  Impl& m = *impl_;
  if (!bgra || w <= 0 || h <= 0) return;
  m.hud.assign(bgra, bgra + (size_t)w * h * 4);
  m.hud_w = w; m.hud_h = h; m.hud_new = true;
  static int logged = 0;
  if (logged < 3) {
    ++logged;
    size_t covered = 0;
    for (size_t i = 3; i < m.hud.size(); i += 4) covered += m.hud[i] > 8;
    host::log("remix: HUD %dx%d, %zu covered pixels", w, h, covered);
  }
}

bool RemixBridge::capture(const std::string& path) {
  // The final picture, and the layer laid over it (<path>.layer.png, premultiplied RGBA): Remix's
  // composite of that layer happens at present, after both.
  Impl& m = *impl_;
  if (m.d3d9_mode) m.backbuffer_capture = path + ".bb.ppm";
  const bool ok = capture_output(path, (int)REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_FINAL_COLOR);
  return ok;
}
bool RemixBridge::capture_output(const std::string& path, int type) {
  Impl& m = *impl_;
  if (!started_ || !m.device || !m.api.dxvk_CopyRenderingOutput) return false;
  IDirect3DSurface9* rt = nullptr;
  if (FAILED(m.device->CreateRenderTarget((UINT)m.width, (UINT)m.height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)))
    return false;
  bool ok = m.api.dxvk_CopyRenderingOutput(rt, (remixapi_dxvk_CopyRenderingOutputType)type) == REMIXAPI_ERROR_CODE_SUCCESS;
  IDirect3DSurface9* sys = nullptr;
  if (ok) ok = SUCCEEDED(m.device->CreateOffscreenPlainSurface((UINT)m.width, (UINT)m.height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
               SUCCEEDED(m.device->GetRenderTargetData(rt, sys));
  D3DLOCKED_RECT lr{};
  if (ok) ok = SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY));
  if (ok) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << m.width << " " << m.height << "\n255\n";
    std::vector<uint8_t> row((size_t)m.width * 3);
    for (int y = 0; y < m.height; ++y) {
      const uint8_t* src = (const uint8_t*)lr.pBits + (size_t)y * lr.Pitch;
      for (int x = 0; x < m.width; ++x) { row[x * 3] = src[x * 4 + 2]; row[x * 3 + 1] = src[x * 4 + 1]; row[x * 3 + 2] = src[x * 4]; }
      f.write((const char*)row.data(), (std::streamsize)row.size());
    }
    sys->UnlockRect();
    ok = (bool)f;
  }
  if (sys) sys->Release();
  rt->Release();
  host::log("remix: capture %s %s", path.c_str(), ok ? "saved" : "failed");
  return ok;
}

#else
struct RemixBridge::Impl {};
RemixBridge::RemixBridge() {}
RemixBridge::~RemixBridge() {}
bool RemixBridge::start(int, int, bool, std::string* error, void*) { if (error) *error = "Windows only"; return false; }
void RemixBridge::show(bool) {}
void RemixBridge::place(int, int, int, int) {}
void RemixBridge::frame(const Frame&, int, const float*, const std::vector<RtxLightRecord>&, const std::vector<uint8_t>&) {}
bool RemixBridge::capture(const std::string&) { return false; }
bool RemixBridge::capture_output(const std::string&, int) { return false; }
void RemixBridge::present_2d() {}
void RemixBridge::present_d3d9() {}
void RemixBridge::set_hud(const uint8_t*, int, int) {}
#endif

}  // namespace gx
