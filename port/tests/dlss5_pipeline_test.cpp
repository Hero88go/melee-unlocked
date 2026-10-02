// Exercises production orchestration on WARP with simulated model callbacks.
// This does not load or validate NVIDIA's neural model.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "dlss5_test_gpu.h"
#include <unordered_map>
#include <unordered_set>
// Include the implementation to inject the model boundary without a production test API.
#include "../runtime/gx/gx_dlss5.cpp"

namespace host { void log(const char*, ...) {} }
namespace gx::streamline { void* native_interface(void* p) { return p; } }

namespace {
struct Parameters {
  void** table;
  std::unordered_map<std::string, unsigned long long> integers;
  std::unordered_map<std::string, float> floats;
};
void integer(void* p, const char* key, unsigned long long value) {
  ((Parameters*)p)->integers[key]=value;
}
void number(void* p, const char* key, float value) { ((Parameters*)p)->floats[key]=value; }
void unsigned_integer(void* p, const char* key, unsigned value) { integer(p,key,value); }
void* table[]={reinterpret_cast<void*>(&integer),reinterpret_cast<void*>(&number),nullptr,reinterpret_cast<void*>(&unsigned_integer)};
Parameters parameters{table};
struct Feature { int id; };
struct Call { void* feature; ID3D12Resource* input; ID3D12Resource* output; bool reset; };
std::vector<Call> calls;
std::unordered_set<void*> alive;
int created=0, released=0, fail_at=-1;
int fail_create_after=-1;
bool alias_create=false;
ID3D12Resource* replacement=nullptr;
int __cdecl create(void*, void*, void** output) {
  if(fail_create_after==0) { *output=nullptr; return NVSDK_NGX_Result_FAIL_OutOfGPUMemory; }
  if(fail_create_after>0) --fail_create_after;
  if(alias_create && gx::dlss5::g.feature[0]) { *output=gx::dlss5::g.feature[0]; return NVSDK_NGX_Result_Success; }
  *output=new Feature{++created}; alive.insert(*output); return NVSDK_NGX_Result_Success;
}
int __cdecl release(void* feature) {
  require(alive.erase(feature)==1,"feature released twice or without creation");
  delete (Feature*)feature; ++released; return NVSDK_NGX_Result_Success;
}
int __cdecl evaluate(void* raw_list, void* feature, void*) {
  const auto resource=[](const char* key) { return (ID3D12Resource*)(uintptr_t)parameters.integers.at(key); };
  auto* input=resource("DLSSNR.Color"); auto* output=resource("DLSSNR.Output");
  require(alive.count(feature)==1,"evaluation uses a live feature");
  require(input!=output,"input and output must be distinct");
  require(output->GetDesc().Width==parameters.integers.at("DLSSNR.Width"),"working width");
  require(output->GetDesc().Height==parameters.integers.at("DLSSNR.Height"),"working height");
  require(parameters.floats.at("DLSSNR.MVecScaleX")==output->GetDesc().Width/8.0f,"motion scale X");
  require(parameters.floats.at("DLSSNR.MVecScaleY")==output->GetDesc().Height/4.0f,"motion scale Y");
  require(parameters.integers.at("DLSSNR.DepthSubrectBaseX")==3,"depth guide origin");
  require(parameters.integers.at("DLSSNR.MVecSubrectWidth")==8,"motion guide extent");
  const int pass=(int)calls.size();
  calls.push_back({feature,input,output,parameters.integers.at("DLSSNR.Reset")!=0});
  if(pass==fail_at) return NVSDK_NGX_Result_FAIL_PlatformError;
  auto* list=(ID3D12GraphicsCommandList*)raw_list;
  auto* source=(pass==0 && replacement)?replacement:input;
  gx::dlss5::barrier(list,source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
  gx::dlss5::barrier(list,output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
  list->CopyResource(output,source);
  gx::dlss5::barrier(list,source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  gx::dlss5::barrier(list,output,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  return NVSDK_NGX_Result_Success;
}
ComPtr<ID3D12Resource> guide(GPU& gpu, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
  D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT}; D3D12_RESOURCE_DESC desc{};
  desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width=16; desc.Height=12;
  desc.DepthOrArraySize=desc.MipLevels=1; desc.SampleDesc.Count=1; desc.Format=format; desc.Flags=flags;
  ComPtr<ID3D12Resource> result;
  hr(gpu.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&result)));
  return result;
}
void check_calls(int passes, bool reset) {
  require((int)calls.size()==passes,"requested pass count must execute");
  std::unordered_set<void*> histories;
  for(int p=0;p<passes;++p) {
    require(histories.insert(calls[p].feature).second,"each pass owns a distinct temporal feature");
    require(calls[p].reset==reset,"history reset policy");
    if(p) require(calls[p].input==calls[p-1].output,"passes must chain the preceding output");
  }
}
}
int main(int argc, char** argv) {
  try {
    GPU gpu;
    auto& state=gx::dlss5::g;
    if(argc==2 && std::strcmp(argv[1],"--native-parameters")==0) {
      // Check the production setters against the installed NVIDIA core without loading NR.
      require(std::strcmp(gpu.backend,"NVIDIA hardware")==0,"native parameters need a NVIDIA adapter");
      std::filesystem::create_directories("dlss5-parameter-logs");
      const std::wstring folder=std::filesystem::absolute("dlss5-parameter-logs").wstring();
      require(gx::dlss5::load_forwarder(),"optional NGX helper exports");
      auto result=static_cast<NVSDK_NGX_Result>(state.core_init(gx::dlss5::kAppId,
          folder.c_str(),gpu.device.Get(),static_cast<int>(NVSDK_NGX_Version_API)));
      require(result==NVSDK_NGX_Result_Success,"installed NVIDIA NGX core initialization");
      void* core_parameters=nullptr;
      result=static_cast<NVSDK_NGX_Result>(state.core_capabilities(&core_parameters));
      state.caps=static_cast<NVSDK_NGX_Parameter*>(core_parameters);
      require(result==NVSDK_NGX_Result_Success && state.caps,"installed NVIDIA parameter block");
      gx::dlss5::find_float_slot(); require(state.float_slot>=0,"native float setter discovered");
      gx::dlss5::Tuning tuning; tuning.intensity=0.85f; tuning.detail=1.1f;
      tuning.tone=0.9f; tuning.skin=-1; tuning.style=2; tuning.auto_mask=false;
      gx::dlss5::set_tuning(tuning);
      const auto check_float=[&](const char* name,float expected) {
        float actual=0; require(state.caps->Get(name,&actual)==NVSDK_NGX_Result_Success && actual==expected,"native float roundtrip");
      };
      const auto check_uint=[&](const char* name,unsigned expected) {
        unsigned actual=0; require(state.caps->Get(name,&actual)==NVSDK_NGX_Result_Success && actual==expected,"native unsigned roundtrip");
      };
      check_float("DLSSNR.Intensity",tuning.intensity); check_float("DLSSNR.LocalStructureStrength",tuning.detail);
      check_float("DLSSNR.LocalToneStrength",tuning.tone); check_float("DLSSNR.SkinStructureStrength",tuning.skin);
      check_uint("DLSSNR.Style",2); check_uint("DLSSNR.UseAutoMask",0); check_uint("DLSSNR.Hint.Render.Preset",0);
      auto color=gpu.texture(8,8);
      gx::dlss5::set_resource("DLSSNR.Color",color.Get());
      ID3D12Resource* actual=nullptr;
      require(state.caps->Get("DLSSNR.Color",&actual)==NVSDK_NGX_Result_Success && actual==color.Get(),"native resource pointer roundtrip");
      gx::dlss5::set_resource("DLSSNR.Color",nullptr);
      require(state.core_destroy(state.caps)==NVSDK_NGX_Result_Success,"core parameters released");
      state.caps=nullptr;
      require(state.core_shutdown(gpu.device.Get())==NVSDK_NGX_Result_Success,"core shut down");
      printf("Installed NVIDIA NGX core: artistic floats, model/preset/mask integers and resource pointers passed (float slot %d)\n",state.float_slot);
      return 0;
    }
    state.ready=true; state.device=gpu.device.Get(); state.float_slot=1;
    state.caps=reinterpret_cast<NVSDK_NGX_Parameter*>(&parameters);
    state.create=create; state.eval=evaluate; state.release=release;
    auto depth=guide(gpu,DXGI_FORMAT_D32_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto motion=guide(gpu,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET);
    Bytes original(64*32*4);
    for(size_t i=0;i<original.size();++i) original[i]=(unsigned char)((i*53+17)%256);
    gx::dlss5::Inputs in{};
    in.device=gpu.device.Get(); in.list=gpu.list.Get(); in.fence=gpu.fence.Get(); in.w=64; in.h=32;
    in.depth=depth.Get(); in.depth_state=D3D12_RESOURCE_STATE_DEPTH_WRITE;
    in.mvec=motion.Get(); in.mvec_state=D3D12_RESOURCE_STATE_RENDER_TARGET;
    in.guide_x=3; in.guide_y=2; in.guide_w=8; in.guide_h=4;
    auto frame=[&](bool expected, const Bytes& expected_pixels) {
      auto color=gpu.texture(in.w,in.h,&original);
      gpu.barrier(color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      in.color=color.Get(); in.signal_value=gpu.signal; calls.clear();
      require(gx::dlss5::evaluate(in)==expected,"evaluation result");
      require(gpu.read(color.Get())==expected_pixels,"frame output / fallback pixels");
      gpu.validate();
    };
    // Build, chain and retain all four temporal histories at full and irregular reduced sizes.
    for(int scale:{100,50,73}) for(int passes=1;passes<=4;++passes) {
      in.tuning.resolution_scale=scale; in.tuning.passes=passes;
      frame(true,original); check_calls(passes,true);
      const int before=created;
      frame(true,original); check_calls(passes,false);
      require(created==before,"unchanged settings must reuse features");
    }
    // Remove haze runs after the model: turning it up keeps the features and their history, and a
    // model that lifts nothing leaves the picture as it was, at reduced and full size.
    for(int scale:{73,100}) {
      in.tuning.resolution_scale=scale; in.tuning.tone_restore=0.0f;
      frame(true,original);
      const int before=created;
      in.tuning.tone_restore=1.0f;
      require(!gx::dlss5::needs_warmup(64,32,in.tuning),"remove haze must not ask for a warm-up");
      frame(true,original); check_calls(4,false);
      require(created==before,"remove haze must not rebuild features");
    }
    in.tuning.tone_restore=0.0f;
    // A later failure must discard even a visibly modified earlier pass.
    in.tuning.resolution_scale=100; in.tuning.passes=2;
    Bytes altered(original.size(),37); auto synthetic=gpu.texture(64,32,&altered);
    replacement=synthetic.Get(); fail_at=1;
    frame(false,original); check_calls(2,true);
    fail_at=-1;
    frame(true,altered); check_calls(2,true);
    replacement=nullptr;
    // Warm-up and zero strength leave the source intact and reset resumed history.
    in.warm_only=true; frame(false,original); check_calls(2,true); in.warm_only=false;
    frame(true,original); check_calls(2,true);
    in.tuning.intensity=0; frame(false,original); require(calls.empty(),"zero strength bypasses model");
    in.tuning.intensity=1; frame(true,original); check_calls(2,true);
    // Unsupported settings are cached; choosing a supported preset must recover without restart.
    in.tuning.passes=3; fail_create_after=1;
    frame(false,original); require(calls.empty(),"creation failure must bypass evaluations");
    require(state.failed && !gx::dlss5::needs_warmup(64,32,in.tuning),"failed settings must not retry each frame");
    const int before_failed_retry=created;
    frame(false,original); require(created==before_failed_retry,"failed creation is cached");
    fail_create_after=-1; in.tuning.passes=1;
    require(gx::dlss5::needs_warmup(64,32,in.tuning),"changed settings can warm up again");
    frame(true,original); check_calls(1,true);
    // A model that aliases pass handles cannot provide distinct histories or be released twice.
    alias_create=true; in.tuning.passes=2; frame(false,original);
    require(state.failed && calls.empty(),"aliased temporal features are rejected");
    alias_create=false; in.tuning.passes=1; frame(true,original); check_calls(1,true);
    fail_at=0;
    for(int attempt=0;attempt<30;++attempt) frame(false,original);
    require(state.failed,"persistent evaluation failure stops these settings");
    frame(false,original); require(calls.empty(),"persistent failure is cached");
    fail_at=-1; in.tuning.style=1; frame(true,original); check_calls(1,true);
    // Rebuild twice before submission: pending features/resources must survive both evaluations.
    auto pending=gpu.texture(64,32,&original);
    gpu.barrier(pending.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    in.color=pending.Get(); in.signal_value=gpu.signal; in.tuning.passes=3; calls.clear();
    require(gx::dlss5::evaluate(in),"first pending evaluation"); check_calls(3,true);
    const int before_release=released;
    in.tuning.resolution_scale=50; in.tuning.passes=4; calls.clear();
    require(gx::dlss5::evaluate(in),"second pending evaluation"); check_calls(4,true);
    require(released==before_release && !state.retired.empty(),"pending retirements must wait for the fence");
    require(gpu.read(pending.Get())==original,"multiple pending evaluations preserve identity"); gpu.validate();
    // Completion fences permit retired objects to be released; shutdown releases the rest.
    gx::dlss5::collect_retired(); require(state.retired.empty(),"completed retirements collected");
    state.caps=nullptr; gx::dlss5::shutdown();
    require(alive.empty() && released==created,"all temporal features released exactly once");
    printf("DLSS 5 pipeline: pass chaining, history, guide scaling, failure fallback, bypass and fence retirement passed with a simulated model on %s\n",gpu.backend);
    return 0;
  } catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
