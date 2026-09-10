#include "VideoBackends/DX11/UnlockedPresenter.h"
#include <iostream>
#include <vector>

using namespace DX11::ExperimentalPresentation;
static void Require(HRESULT result, const char* operation)
{
  if (FAILED(result)) { std::cerr << operation << " failed: " << std::hex << result << '\n'; std::exit(1); }
}
int main()
{
  ComPtr<ID3DBlob> vs, ps, cs, errors;
  const auto compile = [&](const char* entry, const char* profile, ID3DBlob** blob) {
    const auto hr = D3DCompile(ShaderSource, std::strlen(ShaderSource), nullptr, nullptr, nullptr,
        entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, blob, &errors);
    if (FAILED(hr) && errors) std::cerr << static_cast<char*>(errors->GetBufferPointer());
    Require(hr, entry);
  };
  compile("Vertex", "vs_5_0", &vs); compile("Pixel", "ps_5_0", &ps); compile("Flow", "cs_5_0", &cs);
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL level;
  Require(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
      D3D11_SDK_VERSION, &device, &level, &context), "WARP device");
  ComPtr<ID3D11VertexShader> vertex;
  ComPtr<ID3D11PixelShader> pixel;
  ComPtr<ID3D11ComputeShader> flow;
  Require(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex), "vertex");
  Require(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel), "pixel");
  Require(device->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &flow), "compute");
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = desc.Height = 64; desc.MipLevels = desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  std::vector<uint32_t> previous(64*64), current(64*64);
  for (unsigned y=0; y<64; ++y) for (unsigned x=0; x<64; ++x)
  {
    const unsigned a = x*4;
    const unsigned b = x > 8 ? (x-8)*4 : 0;
    previous[y*64+x] = 0xff000000 | a | (a<<8) | (a<<16);
    current[y*64+x] = 0xff000000 | b | (b<<8) | (b<<16);
  }
  ComPtr<ID3D11Texture2D> a, b, motion, result, staging;
  D3D11_SUBRESOURCE_DATA initial = {previous.data(), 64*4, 0};
  Require(device->CreateTexture2D(&desc, &initial, &a), "previous texture");
  initial.pSysMem = current.data();
  Require(device->CreateTexture2D(&desc, &initial, &b), "current texture");
  ComPtr<ID3D11ShaderResourceView> av, bv, mv;
  Require(device->CreateShaderResourceView(a.Get(), nullptr, &av), "previous view");
  Require(device->CreateShaderResourceView(b.Get(), nullptr, &bv), "current view");
  auto md = desc; md.Width = md.Height = 4;
  md.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  md.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
  Require(device->CreateTexture2D(&md, nullptr, &motion), "motion texture");
  Require(device->CreateShaderResourceView(motion.Get(), nullptr, &mv), "motion view");
  ComPtr<ID3D11UnorderedAccessView> output;
  Require(device->CreateUnorderedAccessView(motion.Get(), nullptr, &output), "motion output");
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  Require(device->CreateTexture2D(&desc, nullptr, &result), "result texture");
  ComPtr<ID3D11RenderTargetView> target;
  Require(device->CreateRenderTargetView(result.Get(), nullptr, &target), "target");
  desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  Require(device->CreateTexture2D(&desc, nullptr, &staging), "readback");
  D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 16; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  float params[4] = {64,64,0.5f,1};
  initial = {params,0,0};
  ComPtr<ID3D11Buffer> constants;
  Require(device->CreateBuffer(&bd, &initial, &constants), "constants");
  D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> sampler;
  Require(device->CreateSamplerState(&sd, &sampler), "sampler");
  auto* cb = constants.Get(); auto* sp = sampler.Get(); auto* op = output.Get();
  ID3D11ShaderResourceView* inputs[3] = {av.Get(),bv.Get(),mv.Get()};
  context->CSSetShader(flow.Get(),nullptr,0);
  context->CSSetConstantBuffers(0,1,&cb); context->CSSetSamplers(0,1,&sp);
  context->CSSetShaderResources(0,2,inputs); context->CSSetUnorderedAccessViews(0,1,&op,nullptr);
  context->Dispatch(1,1,1);
  op = nullptr; context->CSSetUnorderedAccessViews(0,1,&op,nullptr);
  ID3D11ShaderResourceView* empty[3] = {};
  context->CSSetShaderResources(0,3,empty);
  context->VSSetShader(vertex.Get(),nullptr,0); context->PSSetShader(pixel.Get(),nullptr,0);
  context->PSSetConstantBuffers(0,1,&cb); context->PSSetSamplers(0,1,&sp);
  context->PSSetShaderResources(0,3,inputs);
  auto* rtv = target.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
  D3D11_VIEWPORT vp = {0,0,64,64,0,1}; context->RSSetViewports(1,&vp);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->Draw(3,0);
  context->CopyResource(staging.Get(),result.Get());
  D3D11_MAPPED_SUBRESOURCE mapped;
  Require(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped), "map");
  const int value = static_cast<unsigned char*>(mapped.pData)[32*mapped.RowPitch+32*4];
  context->Unmap(staging.Get(),0);
  if (std::abs(value-112) > 5)
  { std::cerr << "Expected translated midpoint 112, got " << value << '\n'; return 1; }
  std::cout << "HLSL compilation and WARP midpoint rendering passed (pixel=" << value
            << "). This does not validate Melee visual quality or netplay.\n";
}
