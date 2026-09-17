// PC settings panel on the D3D11 backend. Dear ImGui ships no D3D11 renderer in this checkout,
// so this file carries a small one (the panel needs textured, scissored, alpha-blended triangles
// and nothing else). The panel itself lives in pc_settings.cpp and is shared with D3D12.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "pc_settings.h"
#include "pc_settings_shared.h"
#include "host.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace gx {
namespace {

struct ImGuiD3D11 {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11PixelShader> ps;
  ComPtr<ID3D11InputLayout> layout;
  ComPtr<ID3D11Buffer> constants, vertices, indices;
  ComPtr<ID3D11BlendState> blend;
  ComPtr<ID3D11DepthStencilState> depth;
  ComPtr<ID3D11RasterizerState> raster;
  ComPtr<ID3D11SamplerState> sampler;
  int vertex_capacity = 0, index_capacity = 0;
};

const char* kShaderSource = R"(
cbuffer C : register(b0) { float4x4 projection; };
struct VS_INPUT { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct PS_INPUT { float4 pos : SV_Position; float4 col : COLOR0; float2 uv : TEXCOORD0; };
PS_INPUT VS(VS_INPUT i) { PS_INPUT o; o.pos = mul(projection, float4(i.pos.xy, 0.0, 1.0)); o.col = i.col; o.uv = i.uv; return o; }
Texture2D tex0 : register(t0); SamplerState samp0 : register(s0);
float4 PS(PS_INPUT i) : SV_Target { return i.col * tex0.Sample(samp0, i.uv); })";

bool create_device_objects(ImGuiD3D11& r) {
  ComPtr<ID3DBlob> vsb, psb, err;
  if (FAILED(D3DCompile(kShaderSource, strlen(kShaderSource), "imgui", nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vsb, &err)) ||
      FAILED(D3DCompile(kShaderSource, strlen(kShaderSource), "imgui", nullptr, nullptr, "PS", "ps_4_0", 0, 0, &psb, &err))) {
    host::log("d3d11: settings UI shader compile failed: %s", err ? (const char*)err->GetBufferPointer() : "?");
    return false;
  }
  if (FAILED(r.device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &r.vs))) return false;
  if (FAILED(r.device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &r.ps))) return false;
  const D3D11_INPUT_ELEMENT_DESC elements[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(ImDrawVert, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(ImDrawVert, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(ImDrawVert, col), D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  if (FAILED(r.device->CreateInputLayout(elements, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &r.layout))) return false;
  D3D11_BUFFER_DESC bd{};
  bd.ByteWidth = sizeof(float) * 16; bd.Usage = D3D11_USAGE_DYNAMIC;
  bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(r.device->CreateBuffer(&bd, nullptr, &r.constants))) return false;
  D3D11_BLEND_DESC blend{};
  blend.RenderTarget[0].BlendEnable = TRUE;
  blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  if (FAILED(r.device->CreateBlendState(&blend, &r.blend))) return false;
  D3D11_DEPTH_STENCIL_DESC dd{};
  dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
  if (FAILED(r.device->CreateDepthStencilState(&dd, &r.depth))) return false;
  D3D11_RASTERIZER_DESC rd{};
  rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.ScissorEnable = TRUE; rd.DepthClipEnable = TRUE;
  if (FAILED(r.device->CreateRasterizerState(&rd, &r.raster))) return false;
  D3D11_SAMPLER_DESC sd{};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS; sd.MaxLOD = D3D11_FLOAT32_MAX; sd.MaxAnisotropy = 1;
  if (FAILED(r.device->CreateSamplerState(&sd, &r.sampler))) return false;
  return true;
}

// ImGui 1.92 hands textures to the backend through ImTextureData requests.
void update_texture(ImGuiD3D11& r, ImTextureData* tex) {
  if (tex->Status == ImTextureStatus_WantCreate) {
    if (tex->Format != ImTextureFormat_RGBA32) { host::log("d3d11: settings UI expects an RGBA32 font atlas"); tex->SetStatus(ImTextureStatus_OK); return; }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = tex->Width; td.Height = tex->Height; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{tex->GetPixels(), (UINT)tex->GetPitch(), 0};
    ID3D11Texture2D* texture = nullptr;
    if (FAILED(r.device->CreateTexture2D(&td, &initial, &texture))) return;
    ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(r.device->CreateShaderResourceView(texture, nullptr, &srv))) { texture->Release(); return; }
    tex->BackendUserData = texture;
    tex->SetTexID((ImTextureID)(uintptr_t)srv);
    tex->SetStatus(ImTextureStatus_OK);
    return;
  }
  if (tex->Status == ImTextureStatus_WantUpdates) {
    auto* texture = (ID3D11Texture2D*)tex->BackendUserData;
    if (texture) {
      const ImTextureRect& u = tex->UpdateRect;
      D3D11_BOX box{u.x, u.y, 0, (UINT)(u.x + u.w), (UINT)(u.y + u.h), 1};
      r.context->UpdateSubresource(texture, 0, &box, tex->GetPixelsAt(u.x, u.y), (UINT)tex->GetPitch(), 0);
    }
    tex->SetStatus(ImTextureStatus_OK);
    return;
  }
  if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
    if (auto* srv = (ID3D11ShaderResourceView*)(uintptr_t)tex->GetTexID()) srv->Release();
    if (auto* texture = (ID3D11Texture2D*)tex->BackendUserData) texture->Release();
    tex->BackendUserData = nullptr;
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);
  }
}

void render_draw_data(ImGuiD3D11& r, ImDrawData* draw_data) {
  if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f) return;
  if (draw_data->Textures)
    for (ImTextureData* tex : *draw_data->Textures)
      if (tex->Status != ImTextureStatus_OK) update_texture(r, tex);
  if (draw_data->TotalVtxCount == 0) return;

  if (!r.vertices || r.vertex_capacity < draw_data->TotalVtxCount) {
    r.vertices.Reset();
    r.vertex_capacity = draw_data->TotalVtxCount + 5000;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = r.vertex_capacity * sizeof(ImDrawVert); bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(r.device->CreateBuffer(&bd, nullptr, &r.vertices))) return;
  }
  if (!r.indices || r.index_capacity < draw_data->TotalIdxCount) {
    r.indices.Reset();
    r.index_capacity = draw_data->TotalIdxCount + 10000;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = r.index_capacity * sizeof(ImDrawIdx); bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(r.device->CreateBuffer(&bd, nullptr, &r.indices))) return;
  }
  D3D11_MAPPED_SUBRESOURCE vm{}, im{};
  if (FAILED(r.context->Map(r.vertices.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &vm))) return;
  if (FAILED(r.context->Map(r.indices.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &im))) { r.context->Unmap(r.vertices.Get(), 0); return; }
  auto* vtx = (ImDrawVert*)vm.pData;
  auto* idx = (ImDrawIdx*)im.pData;
  for (const ImDrawList* list : draw_data->CmdLists) {
    memcpy(vtx, list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert));
    memcpy(idx, list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx));
    vtx += list->VtxBuffer.Size; idx += list->IdxBuffer.Size;
  }
  r.context->Unmap(r.vertices.Get(), 0);
  r.context->Unmap(r.indices.Get(), 0);

  {
    const float l = draw_data->DisplayPos.x, t = draw_data->DisplayPos.y;
    const float rr = l + draw_data->DisplaySize.x, b = t + draw_data->DisplaySize.y;
    // Stored column-major (HLSL's default packing) for mul(projection, pos), as ImGui's own backends do.
    const float projection[16] = {
      2.0f / (rr - l),  0.0f,             0.0f, 0.0f,
      0.0f,             2.0f / (t - b),   0.0f, 0.0f,
      0.0f,             0.0f,             0.5f, 0.0f,
      (rr + l) / (l - rr), (t + b) / (b - t), 0.5f, 1.0f,
    };
    D3D11_MAPPED_SUBRESOURCE cm{};
    if (FAILED(r.context->Map(r.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &cm))) return;
    memcpy(cm.pData, projection, sizeof projection);
    r.context->Unmap(r.constants.Get(), 0);
  }

  D3D11_VIEWPORT vp{0, 0, draw_data->DisplaySize.x, draw_data->DisplaySize.y, 0.0f, 1.0f};
  r.context->RSSetViewports(1, &vp);
  UINT stride = sizeof(ImDrawVert), offset = 0;
  ID3D11Buffer* vb = r.vertices.Get();
  ID3D11Buffer* cb = r.constants.Get();
  ID3D11SamplerState* sampler = r.sampler.Get();
  r.context->IASetInputLayout(r.layout.Get());
  r.context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
  r.context->IASetIndexBuffer(r.indices.Get(), sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
  r.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  r.context->VSSetShader(r.vs.Get(), nullptr, 0);
  r.context->VSSetConstantBuffers(0, 1, &cb);
  r.context->PSSetShader(r.ps.Get(), nullptr, 0);
  r.context->PSSetSamplers(0, 1, &sampler);
  const float blend_factor[4] = {0, 0, 0, 0};
  r.context->OMSetBlendState(r.blend.Get(), blend_factor, 0xFFFFFFFFu);
  r.context->OMSetDepthStencilState(r.depth.Get(), 0);
  r.context->RSSetState(r.raster.Get());

  int vertex_offset = 0, index_offset = 0;
  const ImVec2 clip_off = draw_data->DisplayPos, clip_scale = draw_data->FramebufferScale;
  for (const ImDrawList* list : draw_data->CmdLists) {
    for (int i = 0; i < list->CmdBuffer.Size; ++i) {
      const ImDrawCmd& cmd = list->CmdBuffer[i];
      if (cmd.UserCallback) { cmd.UserCallback(list, &cmd); continue; }
      const float x0 = (cmd.ClipRect.x - clip_off.x) * clip_scale.x, y0 = (cmd.ClipRect.y - clip_off.y) * clip_scale.y;
      const float x1 = (cmd.ClipRect.z - clip_off.x) * clip_scale.x, y1 = (cmd.ClipRect.w - clip_off.y) * clip_scale.y;
      if (x1 <= x0 || y1 <= y0) continue;
      D3D11_RECT scissor{(LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1};
      r.context->RSSetScissorRects(1, &scissor);
      auto* srv = (ID3D11ShaderResourceView*)(uintptr_t)cmd.GetTexID();
      r.context->PSSetShaderResources(0, 1, &srv);
      r.context->DrawIndexed(cmd.ElemCount, cmd.IdxOffset + index_offset, (INT)(cmd.VtxOffset + vertex_offset));
    }
    index_offset += list->IdxBuffer.Size;
    vertex_offset += list->VtxBuffer.Size;
  }
}

}  // namespace

struct PcSettingsUID3D11::Impl {
  ImGuiD3D11 renderer;
  SettingsState state;
  bool ready = false;
};

PcSettingsUID3D11::PcSettingsUID3D11(void* window, ID3D11Device* device, ID3D11DeviceContext* context, const RenderOptions& options)
    : impl_(std::make_unique<Impl>()) {
  impl_->state.open = options.settings_open;
  impl_->state.running_d3d11 = true;   // so the panel can say a backend change needs a restart
  impl_->renderer.device = device;
  impl_->renderer.context = context;
  settings_context_create(window, options.settings_open);
  auto& io = ImGui::GetIO();
  io.BackendRendererName = "melee_d3d11";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
  impl_->ready = create_device_objects(impl_->renderer);
  if (!impl_->ready) host::log("d3d11: settings panel unavailable (renderer objects could not be created)");
}

PcSettingsUID3D11::~PcSettingsUID3D11() {
  for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
    if (tex->RefCount != 1) continue;
    if (auto* srv = (ID3D11ShaderResourceView*)(uintptr_t)tex->GetTexID()) srv->Release();
    if (auto* texture = (ID3D11Texture2D*)tex->BackendUserData) texture->Release();
    tex->BackendUserData = nullptr;
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);
  }
  ImGui::GetIO().BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
  ImGui::GetIO().BackendRendererName = nullptr;
  settings_context_destroy();
}

bool PcSettingsUID3D11::begin(RenderOptions& options) {
  if (!impl_->ready) return false;
  return settings_frame(impl_->state, options);
}

void PcSettingsUID3D11::draw() {
  if (!impl_->ready) return;
  render_draw_data(impl_->renderer, ImGui::GetDrawData());
}

}  // namespace gx
