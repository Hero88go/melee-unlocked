// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_dlss5_scaling.h"
#include <d3dcompiler.h>
#include <cstring>

namespace gx::dlss5 {
using Microsoft::WRL::ComPtr;
namespace {
const char* kShader = R"(
Texture2D<float4> source : register(t0);
Texture2D<float4> baseline : register(t1);
Texture2D<float4> original : register(t2);
RWTexture2D<float4> target : register(u0);
cbuffer Settings : register(b0) { uint sw, sh, dw, dh; uint mode, filter; };

float4 pixel(Texture2D<float4> tex, int2 p) {
  return tex.Load(int3(clamp(p, int2(0,0), int2(sw-1,sh-1)), 0));
}
float cubic(float x) {
  x = abs(x);
  if (x < 1) return ((1.5*x - 2.5)*x)*x + 1;
  if (x < 2) return ((-0.5*x + 2.5)*x - 4)*x + 2;
  return 0;
}
float4 resize(Texture2D<float4> tex, uint2 p, uint f) {
  float2 at = (float2(p)+0.5)*float2(sw,sh)/float2(dw,dh)-0.5;
  int2 lo = int2(floor(at)); float2 t = frac(at);
  if (f == 2) return pixel(tex, int2(floor(at+0.5)));
  if (f == 1) {
    float4 sum = 0;
    [unroll] for (int y=-1;y<=2;++y)
      [unroll] for (int x=-1;x<=2;++x)
        sum += pixel(tex, lo+int2(x,y))*cubic(x-t.x)*cubic(y-t.y);
    return sum;
  }
  return lerp(lerp(pixel(tex,lo),pixel(tex,lo+int2(1,0)),t.x),
              lerp(pixel(tex,lo+int2(0,1)),pixel(tex,lo+int2(1,1)),t.x),t.y);
}
float4 area(uint2 p) {
  float2 a=float2(p)*float2(sw,sh)/float2(dw,dh);
  float2 b=float2(p+1)*float2(sw,sh)/float2(dw,dh);
  float4 sum=0;
  for (int y=(int)floor(a.y);y<(int)ceil(b.y);++y)
    for (int x=(int)floor(a.x);x<(int)ceil(b.x);++x) {
      float2 overlap=max(0.0,min(b,float2(x+1,y+1))-max(a,float2(x,y)));
      sum += pixel(source,int2(x,y))*overlap.x*overlap.y;
    }
  return sum/((b.x-a.x)*(b.y-a.y));
}
float3 coarse(Texture2D<float4> tex, uint2 p) {
  uint cw, ch; tex.GetDimensions(cw, ch);
  float2 at=(float2(p)+0.5)*float2(cw,ch)/float2(dw,dh)-0.5;
  int2 lo=int2(floor(at)), hi=int2(cw-1,ch-1); float2 t=frac(at);
  float3 a=tex.Load(int3(clamp(lo,int2(0,0),hi),0)).rgb, b=tex.Load(int3(clamp(lo+int2(1,0),int2(0,0),hi),0)).rgb;
  float3 c=tex.Load(int3(clamp(lo+int2(0,1),int2(0,0),hi),0)).rgb, d=tex.Load(int3(clamp(lo+int2(1,1),int2(0,0),hi),0)).rgb;
  return lerp(lerp(a,b,t.x),lerp(c,d,t.x),t.y);
}
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
  if (id.x>=dw || id.y>=dh) return;
  uint2 p=id.xy;
  if (mode==3) {
    // Tone restore: the model lifts the black level on every pass. original = the average of the
    // picture before the model, baseline = the average of its output (1x1 for a whole-frame
    // correction). A lift is removed as a levels change, so black returns to black and white
    // stays white; a drop is added back. filter carries the strength, 256 = full.
    float4 out_px=source.Load(int3(p,0));
    float3 lift=(coarse(baseline,p)-coarse(original,p))*(filter/256.0);
    float3 up=max(lift,0.0);
    float3 fixed=(out_px.rgb-up)/max(1.0-up,0.25)-min(lift,0.0);
    target[p]=float4(saturate(fixed),out_px.a);
    return;
  }
  if (mode==0) {
    target[p]=filter==0 ? area(p) : resize(source,p,filter==1 ? 0 : 2);
    return;
  }
  float4 base=original.Load(int3(p,0));
  float3 result=resize(source,p,filter).rgb;
  // Subtract in signed floating point before the UNORM store. No unsigned delta texture.
  if (mode==2) result=base.rgb+(result-resize(baseline,p,filter).rgb);
  target[p]=float4(saturate(result),base.a);
}
)";
}

bool Scaling::initialise(ID3D12Device* device) {
  if (pipeline_) return true;
  ComPtr<ID3DBlob> shader, errors, signature;
  HRESULT hr=D3DCompile(kShader, std::strlen(kShader), "dlss5_scaling", nullptr, nullptr,
                       "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &errors);
  if (FAILED(hr)) { error_=errors ? std::string((char*)errors->GetBufferPointer(),errors->GetBufferSize()) : "resize shader compilation failed"; return false; }
  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0,0,0};
  ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,3};
  D3D12_ROOT_PARAMETER params[2]{};
  params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[0].DescriptorTable={2,ranges};
  params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[1].Constants={0,0,6};
  D3D12_ROOT_SIGNATURE_DESC desc{2,params,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
  hr=D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&errors);
  if (FAILED(hr) || FAILED(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root_)))) {
    error_="resize root signature failed"; return false;
  }
  D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature=root_.Get();
  pd.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
  if (FAILED(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pipeline_)))) { error_="resize pipeline failed"; return false; }
  return true;
}

bool Scaling::dispatch(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                       ID3D12Fence* fence, uint64_t signal_value,
                       ID3D12Resource* source, ID3D12Resource* baseline,
                       ID3D12Resource* original, ID3D12Resource* destination,
                       uint32_t mode, uint32_t filter) {
  if (!fence || !signal_value) { error_="resize submission has no completion fence"; return false; }
  if (!initialise(device)) return false;
  Descriptors* slot=nullptr;
  for (auto& d:descriptors_) if (d.until<=fence->GetCompletedValue()) { slot=&d; break; }
  if (!slot) {
    Descriptors next;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,4,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    if (FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&next.heap)))) { error_="resize descriptors failed"; return false; }
    descriptors_.push_back(std::move(next)); slot=&descriptors_.back();
  }
  slot->until=signal_value;
  const UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto cpu=slot->heap->GetCPUDescriptorHandleForHeapStart();
  ID3D12Resource* inputs[]={source,baseline,original};
  for (auto* input:inputs) { device->CreateShaderResourceView(input,nullptr,cpu); cpu.ptr+=stride; }
  device->CreateUnorderedAccessView(destination,nullptr,nullptr,cpu);
  ID3D12DescriptorHeap* heaps[]={slot->heap.Get()}; list->SetDescriptorHeaps(1,heaps);
  list->SetComputeRootSignature(root_.Get()); list->SetPipelineState(pipeline_.Get());
  list->SetComputeRootDescriptorTable(0,slot->heap->GetGPUDescriptorHandleForHeapStart());
  const auto src=source->GetDesc(), dst=destination->GetDesc();
  const uint32_t constants[]={(uint32_t)src.Width,src.Height,(uint32_t)dst.Width,dst.Height,mode,filter};
  list->SetComputeRoot32BitConstants(1,6,constants,0);
  list->Dispatch(((UINT)dst.Width+7)/8,(dst.Height+7)/8,1);
  return true;
}
void Scaling::shutdown() { descriptors_.clear(); pipeline_.Reset(); root_.Reset(); error_.clear(); }
}
