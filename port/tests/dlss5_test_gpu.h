// Shared D3D12 WARP fixture for DLSS 5 renderer tests.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gx_dlss5_scaling.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
using Bytes = std::vector<unsigned char>;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
static void hr(HRESULT value) { require(SUCCEEDED(value), "D3D12 operation failed"); }
struct GPU {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12InfoQueue> info;
  std::vector<ComPtr<ID3D12Resource>> uploads;
  gx::dlss5::Scaling scaling;
  HANDLE event = nullptr;
  uint64_t signal = 1;
  const char* backend = "WARP";
  GPU() {
    const char* requested=std::getenv("MELEE_DLSS5_TEST_GPU");
    const bool hardware=requested && std::strcmp(requested,"hardware")==0;
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
      debug->EnableDebugLayer();
      if(hardware) {
        ComPtr<ID3D12Debug1> validation;
        if(SUCCEEDED(debug.As(&validation))) validation->SetEnableGPUBasedValidation(TRUE);
      }
    }
    ComPtr<IDXGIFactory4> factory; hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    if(hardware) {
      for(UINT i=0;;++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if(factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{}; hr(adapter->GetDesc1(&desc));
        if(desc.VendorId!=0x10de || (desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        printf("Adapter: %ls (GPU validation enabled when available)\n",desc.Description);
        backend="NVIDIA hardware"; break;
      }
      require(device.Get()!=nullptr,"no NVIDIA hardware adapter");
    } else {
      ComPtr<IDXGIAdapter> warp; hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
      hr(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    }
    device.As(&info);
    D3D12_COMMAND_QUEUE_DESC desc{}; hr(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
    hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr); require(event!=nullptr,"event");
  }
  ~GPU() { if(event) CloseHandle(event); }
  void barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to}; list->ResourceBarrier(1,&b);
  }
  ComPtr<ID3D12Resource> buffer(UINT64 bytes,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES hp{heap}; D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=bytes; d.Height=1;
    d.DepthOrArraySize=d.MipLevels=1; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r; hr(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r))); return r;
  }
  ComPtr<ID3D12Resource> texture(UINT w,UINT h,const Bytes* pixels=nullptr) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT}; D3D12_RESOURCE_DESC d{};
    d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=w; d.Height=h; d.DepthOrArraySize=d.MipLevels=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1; d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> r;
    hr(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,pixels?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r)));
    if(pixels) {
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 size;
      device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&size);
      auto upload=buffer(size,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
      void* ptr; hr(upload->Map(0,nullptr,&ptr));
      for(UINT y=0;y<h;++y) memcpy((char*)ptr+fp.Offset+y*fp.Footprint.RowPitch,pixels->data()+y*w*4,w*4);
      upload->Unmap(0,nullptr);
      D3D12_TEXTURE_COPY_LOCATION src{upload.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT}; src.PlacedFootprint=fp;
      D3D12_TEXTURE_COPY_LOCATION dst{r.Get(),D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
      list->CopyTextureRegion(&dst,0,0,0,&src,nullptr); uploads.push_back(upload);
      barrier(r.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    return r;
  }
  void run(ID3D12Resource* src,ID3D12Resource* base,ID3D12Resource* orig,ID3D12Resource* dst,UINT mode,UINT filter) {
    require(scaling.dispatch(device.Get(),list.Get(),fence.Get(),signal,src,base,orig,dst,mode,filter),scaling.error().c_str());
  }
  Bytes read(ID3D12Resource* texture) {
    const auto d=texture->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 size;
    device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&size);
    auto rb=buffer(size,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    barrier(texture,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{texture,D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION dst{rb.Get(),D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT}; dst.PlacedFootprint=fp;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    barrier(texture,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    hr(list->Close()); ID3D12CommandList* work[]={list.Get()}; queue->ExecuteCommandLists(1,work);
    hr(queue->Signal(fence.Get(),signal)); hr(fence->SetEventOnCompletion(signal,event));
    require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"GPU timeout"); ++signal;
    Bytes out((size_t)d.Width*d.Height*4); void* ptr; hr(rb->Map(0,nullptr,&ptr));
    for(UINT y=0;y<d.Height;++y) memcpy(out.data()+y*(size_t)d.Width*4,(char*)ptr+fp.Offset+y*fp.Footprint.RowPitch,(size_t)d.Width*4);
    rb->Unmap(0,nullptr); uploads.clear(); hr(allocator->Reset()); hr(list->Reset(allocator.Get(),nullptr));
    return out;
  }
  void validate() {
    if(!info) { puts("D3D12 debug layer unavailable; pixel checks ran"); return; }
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
      SIZE_T size=0; info->GetMessage(i,nullptr,&size); std::vector<char> data(size);
      auto* msg=(D3D12_MESSAGE*)data.data(); hr(info->GetMessage(i,msg,&size));
      if(msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) { puts(msg->pDescription); throw std::runtime_error("D3D12 validation error"); }
    }
  }
};
