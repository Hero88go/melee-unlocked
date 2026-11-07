// RTX Remix bridge (local experiment, never in a release). See remix_bridge.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "remix_bridge.h"
#include "gx_core.h"
#include "gx_texture.h"
#include "rtx_lights.h"
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

// The draw's TEV (colour combiner) for one texel on the CPU, the way Melee composes its colours from
// textures, the rasterized colour and its constant and register colours: what the surface shows under
// neutral light. Remix gets that as its texture, so constant-coloured lines and tinted stages keep their
// colours. Inputs follow GX: only the texmap `own_map` is the given texel (other textures read white),
// the rasterized colour is `ras` (0..255), comparison modes are passed as plain adds.
void tev_texel(const DrawCall& d, int own_map, const float texel[4], const float ras[4], float out[4]) {
  const BPMemory& bp = d.bp;
  float reg[4][4];
  for (int r = 0; r < 4; ++r)
    for (int k = 0; k < 4; ++k) reg[r][k] = (float)d.tev_colors[r][k];
  const uint32_t stages = bp.numtevstages() + 1;
  static const float kconst[8] = {255.0f, 223.0f, 191.0f, 159.0f, 128.0f, 96.0f, 64.0f, 32.0f};
  for (uint32_t n = 0; n < stages && n < 16; ++n) {
    float tex[4] = {255, 255, 255, 255};
    if (bp.order_enable((int)n) && bp.order_texmap((int)n) == own_map)
      for (int k = 0; k < 4; ++k) tex[k] = texel[k];
    float konst[4];
    const int kc = bp.ksel_kc((int)n), ka = bp.ksel_ka((int)n);
    auto kval = [&](int sel, int ch) -> float {
      if (sel < 8) return kconst[sel];
      if (sel >= 12 && sel < 16) return (float)d.tev_kcolors[sel - 12][ch];
      if (sel >= 16 && sel < 32) return (float)d.tev_kcolors[(sel - 16) & 3][(sel - 16) >> 2];
      return 0.0f;
    };
    for (int k = 0; k < 3; ++k) konst[k] = kval(kc, k);
    konst[3] = kval(ka < 12 ? ka : (ka >= 28 ? ka : ka), 3);
    if (ka >= 16 && ka < 32) konst[3] = (float)d.tev_kcolors[(ka - 16) & 3][(ka - 16) >> 2];
    const uint32_t cc = bp.tev_color((int)n), ac = bp.tev_alpha((int)n);
    auto cin = [&](uint32_t sel, int ch) -> float {
      switch (sel) {
        case 0: return reg[0][ch]; case 1: return reg[0][3]; case 2: return reg[1][ch]; case 3: return reg[1][3];
        case 4: return reg[2][ch]; case 5: return reg[2][3]; case 6: return reg[3][ch]; case 7: return reg[3][3];
        case 8: return tex[ch]; case 9: return tex[3]; case 10: return ras[ch]; case 11: return ras[3];
        case 12: return 255.0f; case 13: return 128.0f; case 14: return konst[ch]; default: return 0.0f;
      }
    };
    auto ain = [&](uint32_t sel) -> float {
      switch (sel) {
        case 0: return reg[0][3]; case 1: return reg[1][3]; case 2: return reg[2][3]; case 3: return reg[3][3];
        case 4: return tex[3]; case 5: return ras[3]; case 6: return konst[3]; default: return 0.0f;
      }
    };
    static const float shifts[4] = {1.0f, 2.0f, 4.0f, 0.5f}, biases[3] = {0.0f, 128.0f, -128.0f};
    float result[4];
    for (int ch = 0; ch < 3; ++ch) {
      const float a = cin(bits(cc, 12, 4), ch), b = cin(bits(cc, 8, 4), ch), c = cin(bits(cc, 4, 4), ch), dd = cin(bits(cc, 0, 4), ch);
      const float lerp_v = (a * (255.0f - c) + b * c) / 255.0f;
      const uint32_t bias = bits(cc, 16, 2);
      float v = dd + (bits(cc, 18, 1) ? -lerp_v : lerp_v) + (bias < 3 ? biases[bias] : 0.0f);
      v *= shifts[bits(cc, 20, 2)];
      result[ch] = bits(cc, 19, 1) ? std::clamp(v, 0.0f, 255.0f) : std::clamp(v, -1024.0f, 1023.0f);
    }
    {
      const float a = ain(bits(ac, 13, 3)), b = ain(bits(ac, 10, 3)), c = ain(bits(ac, 7, 3)), dd = ain(bits(ac, 4, 3));
      const float lerp_v = (a * (255.0f - c) + b * c) / 255.0f;
      const uint32_t bias = bits(ac, 16, 2);
      float v = dd + (bits(ac, 18, 1) ? -lerp_v : lerp_v) + (bias < 3 ? biases[bias] : 0.0f);
      v *= shifts[bits(ac, 20, 2)];
      result[3] = bits(ac, 19, 1) ? std::clamp(v, 0.0f, 255.0f) : std::clamp(v, -1024.0f, 1023.0f);
    }
    const uint32_t cdest = bits(cc, 22, 2), adest = bits(ac, 22, 2);
    for (int ch = 0; ch < 3; ++ch) reg[cdest][ch] = result[ch];
    reg[adest][3] = result[3];
    if (n + 1 == stages) for (int k = 0; k < 4; ++k) out[k] = std::clamp(k < 3 ? reg[cdest][k] : reg[adest][3], 0.0f, 255.0f);
  }
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
};

RemixBridge::RemixBridge() : impl_(new Impl) {}
RemixBridge::~RemixBridge() {
  if (started_ && impl_->api.Shutdown) {
    for (auto& m : impl_->meshes) impl_->api.DestroyMesh(m.second.handle);
    for (auto& m : impl_->materials) impl_->api.DestroyMaterial(m.second);
    for (auto l : impl_->lights) impl_->api.DestroyLight(l);
    if (impl_->device) impl_->device->Release();
    if (impl_->d3d9) impl_->d3d9->Release();
    impl_->api.Shutdown();
  }
  if (impl_->hwnd) DestroyWindow(impl_->hwnd);
  if (impl_->readback_hwnd) DestroyWindow(impl_->readback_hwnd);
  delete impl_;
}

bool RemixBridge::start(int width, int height, bool hidden, std::string* error) {
  Impl& m = *impl_;
  std::wstring dir;
  if (const wchar_t* v = _wgetenv(L"MELEE_REMIX_DIR")) dir = v;
  else {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    dir = std::filesystem::path(exe).parent_path() / L"Remix";
  }
  SetDllDirectoryW(dir.c_str());   // the runtime's own DLLs (DLSS, NRD, USD) sit beside it
  const std::wstring dll = (std::filesystem::path(dir) / L"d3d9.dll").wstring();
  const remixapi_ErrorCode rc = remixapi_lib_loadRemixDllAndInitialize(dll.c_str(), &m.api, &m.dll);
  if (rc != REMIXAPI_ERROR_CODE_SUCCESS) { if (error) *error = "loading the Remix runtime failed (" + std::to_string((int)rc) + ")"; return false; }
  WNDCLASSW wc{};
  wc.lpfnWndProc = remix_wndproc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"MeleeUnlockedRemix";
  RegisterClassW(&wc);
  RECT r{0, 0, width, height};
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  m.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Melee Unlocked: RTX Remix", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
  if (!m.hwnd) { if (error) *error = "no window for Remix"; return false; }
  if (!hidden) ShowWindow(m.hwnd, SW_SHOWNOACTIVATE);
  m.width = width; m.height = height;
  remixapi_StartupInfo info{};
  info.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
  info.hwnd = m.hwnd;
  info.disableSrgbConversionForOutput = 0;
  info.forceNoVkSwapchain = 0;
  info.editorModeEnabled = 0;
  info.combineGuiInFinalColor = 1;
  const remixapi_ErrorCode s = m.api.Startup(&info);
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
  if (_wgetenv(L"MELEE_REMIX_D3D9_READBACK") && m.api.dxvk_CreateD3D9 && m.api.dxvk_CreateD3D9(0, &m.d3d9) == REMIXAPI_ERROR_CODE_SUCCESS && m.d3d9) {
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

void RemixBridge::frame(const Frame& f, int camera, const float vw[12], const std::vector<RtxLightRecord>& lights) {
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

  // Draws: world-space vertices from the GX matrices, one surface per draw, cached by content.
  for (const DrawCall& d : f.draws) {
    if (no_meshes || d.rtx.camera != camera || d.vertex_count < 3) continue;
    const uint32_t bm = d.bp.blendmode();
    const bool blended = bm & 1;
    const bool additive = blended && ((bm >> 5) & 7) == 1;   // destination factor ONE: glows, fire, sparks
    // Additive draws (glows, the fountain's light, sparks) come in as emissive cut-outs.
    // Blended surfaces (leaves, the sky's cut-out clouds, decals) and alpha-tested ones become cutouts.
    const uint32_t at = d.bp.alpha_test();
    const uint32_t comp0 = (at >> 16) & 7, comp1 = (at >> 19) & 7, logic = (at >> 22) & 3;
    const bool alpha_always = logic == 0 ? (comp0 == 7 && comp1 == 7) : logic == 1 ? (comp0 == 7 || comp1 == 7) : false;
    const bool cutout = blended || !alpha_always;
    // GX does not light this draw: its light is painted into its texture and vertex colours (most of a
    // stage). Its texture also emits, so Remix shows it as painted and adds its own light on top
    // (MELEE_REMIX_EMISSIVE scales it; additive glows emit more).
    const uint32_t chan = d.xf_regs[0x0E];
    const bool unlit = (d.xf_regs[0x09] & 3) == 0 || ((chan >> 1) & 1) == 0;
    static const float emissive_scale = [] { const char* v = std::getenv("MELEE_REMIX_EMISSIVE"); return v ? (float)std::atof(v) : 0.8f; }();
    const float emissive = additive ? 2.0f * emissive_scale : unlit && d.owner_player >= 6 ? emissive_scale : 0.0f;
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
      static const bool no_tint = std::getenv("MELEE_REMIX_NO_TINT") != nullptr;
      if (no_tint) tint[0] = tint[1] = tint[2] = 1.0f;
    }
    const uint32_t tint_q = ((uint32_t)(tint[0] * 63.0f + 0.5f) << 12) | ((uint32_t)(tint[1] * 63.0f + 0.5f) << 6) | (uint32_t)(tint[2] * 63.0f + 0.5f);
    // Its material: the first texture times that colour, written once as a DDS Remix reads.
    uint64_t mat_key = 0;
    const TextureRef& t = d.textures[0];
    if (!no_textures && t.used && t.data && t.width && t.height && t.width <= 1024 && t.height <= 1024) {
      mat_key = fnv(&t.data->hash, 8, fnv(&t.format, 4, fnv(&t.mode0, 4, fnv(&emissive, 4, fnv(&tint_q, 4, cutout ? 0x9E3779B97F4A7C15ull : 1469598103934665603ull)))));
      if (!m.materials.count(mat_key)) {
        decode_texture(t.data->image.data(), t.width, t.height, t.format, t.data->palette.empty() ? nullptr : t.data->palette.data(),
                       t.tlut_format, m.rgba);
        // Each texel through the draw's colour combiner (MELEE_REMIX_NO_TEV: the texture times its colour source).
        static const bool no_tev = std::getenv("MELEE_REMIX_NO_TEV") != nullptr;
        const int own_map = [&] { for (uint32_t n = 0; n <= d.bp.numtevstages() && n < 16; ++n) if (d.bp.order_enable((int)n)) return d.bp.order_texmap((int)n); return 0; }();
        const float ras[4] = {tint[0] * 255.0f, tint[1] * 255.0f, tint[2] * 255.0f, 255.0f};
        for (size_t i = 0; i + 3 < m.rgba.size(); i += 4) {
          if (no_tev) { for (int k = 0; k < 3; ++k) m.rgba[i + k] = (uint8_t)std::min(255.0f, m.rgba[i + k] * tint[k] + 0.5f); continue; }
          const float texel[4] = {(float)m.rgba[i], (float)m.rgba[i + 1], (float)m.rgba[i + 2], (float)m.rgba[i + 3]};
          float o[4];
          tev_texel(d, own_map, texel, ras, o);
          for (int k = 0; k < 4; ++k) m.rgba[i + k] = (uint8_t)(o[k] + 0.5f);
        }
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
        op.roughnessConstant = 0.55f;
        op.metallicConstant = 0.0f;
        op.alphaTestType = cutout ? 4 : 7;   // greater (cut-outs) or always
        op.alphaReferenceValue = cutout ? 96 : 0;
        remixapi_MaterialInfo mi{};
        mi.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
        mi.pNext = &op;
        mi.hash = mat_key;
        mi.albedoTexture = wpath.c_str();
        mi.emissiveTexture = emissive > 0.0f ? wpath.c_str() : nullptr;
        mi.emissiveIntensity = emissive;
        mi.emissiveColorConstant = {1.0f, 1.0f, 1.0f};
        mi.filterMode = 1;
        const uint32_t wrap_s = t.mode0 & 3, wrap_t = (t.mode0 >> 2) & 3;
        mi.wrapModeU = (uint8_t)(wrap_s == 0 ? 2 : wrap_s == 2 ? 1 : 0);   // GX clamp/repeat/mirror -> clamp/mirror/repeat ids
        mi.wrapModeV = (uint8_t)(wrap_t == 0 ? 2 : wrap_t == 2 ? 1 : 0);
        remixapi_MaterialHandle h = nullptr;
        if (m.api.CreateMaterial(&mi, &h) == REMIXAPI_ERROR_CODE_SUCCESS) m.materials[mat_key] = h;
        else m.materials[mat_key] = nullptr;
      }
    }
    // No texture: the colour source alone, as a constant.
    if (!mat_key) {
      mat_key = fnv(&tint_q, 4, fnv(&emissive, 4, 0x51ED270B27ull));
      if (!m.materials.count(mat_key)) {
        remixapi_MaterialInfoOpaqueEXT op{};
        op.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
        op.albedoConstant = {tint[0], tint[1], tint[2]};
        op.opacityConstant = 1.0f;
        op.roughnessConstant = 0.55f;
        op.alphaTestType = 7;
        remixapi_MaterialInfo mi{};
        mi.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
        mi.pNext = &op;
        mi.hash = mat_key;
        mi.emissiveIntensity = emissive;
        mi.emissiveColorConstant = {tint[0], tint[1], tint[2]};
        remixapi_MaterialHandle h = nullptr;
        m.materials[mat_key] = m.api.CreateMaterial(&mi, &h) == REMIXAPI_ERROR_CODE_SUCCESS ? h : nullptr;
      }
    }
    const remixapi_MaterialHandle material = m.materials[mat_key];
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
  remixapi_PresentInfo pi{};
  pi.sType = REMIXAPI_STRUCT_TYPE_PRESENT_INFO;
  m.api.Present(&pi);
  stats_.meshes = (uint32_t)m.meshes.size();
  stats_.materials = (uint32_t)m.materials.size();
  stats_.lights = (uint32_t)m.lights.size();
  stats_.cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

bool RemixBridge::capture(const std::string& path) {
  Impl& m = *impl_;
  if (!started_ || !m.device || !m.api.dxvk_CopyRenderingOutput) return false;
  IDirect3DSurface9* rt = nullptr;
  if (FAILED(m.device->CreateRenderTarget((UINT)m.width, (UINT)m.height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)))
    return false;
  bool ok = m.api.dxvk_CopyRenderingOutput(rt, REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_FINAL_COLOR) == REMIXAPI_ERROR_CODE_SUCCESS;
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
bool RemixBridge::start(int, int, bool, std::string* error) { if (error) *error = "Windows only"; return false; }
void RemixBridge::frame(const Frame&, int, const float*, const std::vector<RtxLightRecord>&) {}
bool RemixBridge::capture(const std::string&) { return false; }
#endif

}  // namespace gx
