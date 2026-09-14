// Exercise the production renderer with deliberately tiny descriptor/upload pools.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include "gx_d3d12.h"
#include "gx_texture.h"
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace host {
void log(const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt,args); va_end(args); puts(""); }
[[noreturn]] void die(const char* fmt, ...) { char buf[1024]; va_list args; va_start(args,fmt); vsnprintf(buf,sizeof buf,fmt,args); va_end(args); throw std::runtime_error(buf); }
// The renderer reaches back into the window for fullscreen changes; this test owns a bare HWND.
bool window_take_fullscreen_toggle() { return false; }
void window_set_fullscreen(bool) {}
void window_set_title(const wchar_t*) {}
}
namespace slippi { void request_widescreen(bool) {} }
static void check(bool b, const char* why) { if(!b) throw std::runtime_error(why); }
static void f32(uint32_t& out, float f) { memcpy(&out,&f,4); }
static gx::TextureRef texture(uint32_t addr, uint8_t r, uint8_t b) {
  gx::TextureRef t; t.addr=addr; t.width=t.height=4; t.format=6; t.used=true;
  auto data=std::make_shared<gx::TextureSnapshot>(); data->image.resize(64);
  for(int i=0;i<16;++i) { data->image[i*2]=255; data->image[i*2+1]=r; data->image[32+i*2]=0; data->image[33+i*2]=b; }
  data->hash=gx::hash_bytes(data->image.data(),data->image.size()); t.data=data; return t;
}
int main() {
  HWND window=nullptr;
  try {
    WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"MeleeGpuResourceTest";
    RegisterClassW(&wc);
    window=CreateWindowW(wc.lpszClassName,L"GPU resource regression",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,wc.hInstance,nullptr);
    check(window!=nullptr,"hidden window creation");
    gx::D3D12Options options; options.efb_scale=1; options.capture_frame=6; options.capture_path="gpu-resource-test.ppm";
    options.frame_times="enable GPU timestamps";
    std::unique_ptr<gx::Backend> renderer(gx::create_d3d12_backend(window,640,480,options));
    gx::Frame frame; frame.sequence=1;
    // The first red quad must survive every subsequent descriptor/page rollover.
    auto red=texture(0x1000,255,0), blue=texture(0x2000,0,255);
    gx::DrawCall d{}; d.primitive=0x80; d.vertex_count=4; d.components=gx::VB_HAS_UV0;
    d.posMatrices[0]=d.posMatrices[5]=d.posMatrices[10]=1;
    d.matrix_index_a=60u<<6; // identity texture matrix
    d.xf_regs[0x26]=1; d.xf_regs[0x3F]=1; d.xf_regs[0x40]=5u<<7;
    f32(d.xf_regs[0x1A],320); f32(d.xf_regs[0x1B],-240);
    f32(d.xf_regs[0x1C],16777215); f32(d.xf_regs[0x1D],662); f32(d.xf_regs[0x1E],582); f32(d.xf_regs[0x1F],16777215);
    f32(d.xf_regs[0x20],1); f32(d.xf_regs[0x22],1); f32(d.xf_regs[0x24],1);
    d.bp.reg[gx::BP_GENMODE]=1;
    d.bp.reg[gx::BP_BLENDMODE]=24;
    d.bp.reg[gx::BP_SCISSORTL]=(342u<<12)|342u;
    d.bp.reg[gx::BP_SCISSORBR]=((342u+639)<<12)|(342u+479);
    d.bp.reg[gx::BP_SCISSOROFFSET]=(171u<<10)|171u;
    d.bp.reg[gx::BP_TREF]=64;
    d.bp.reg[gx::BP_TEV_KSEL]=4; d.bp.reg[gx::BP_TEV_KSEL+1]=14;
    d.bp.reg[gx::BP_TEV_COLOR_ENV]=0x8fff8;
    d.bp.reg[gx::BP_TEV_ALPHA_ENV]=0x8ffc0;
    d.bp.reg[gx::BP_ALPHACOMPARE]=(7u<<16)|(7u<<19);
    gx::EfbCopy clear{}; clear.src_w=640; clear.src_h=480; clear.clear=true; clear.clear_color=0xff000000; clear.clear_z=0xffffff;
    // A copy with clear initializes the EFB before the actual test draws.
    clear.dest_addr=0x3000; frame.copies.push_back(clear); frame.commands.push_back({gx::FrameCommand::Copy,0});
    for(unsigned i=0;i<40;++i) {
      d.first_vertex=(uint32_t)frame.vertices.size(); d.textures[0]=i==0?red:texture(0x4000+i*0x100,0,255);
      d.textures[0].mode1=i<<8; // distinct sampler sets force sampler rollover
      float left=i==0?-1.f:0.f, right=i==0?0.f:1.f;
      const float xy[4][2]={{left,-1},{right,-1},{right,1},{left,1}};
      for(auto& p:xy) { gx::Vertex v{}; v.pos[0]=p[0]; v.pos[1]=p[1]; v.uv[0][0]=v.uv[0][1]=0.5f; frame.vertices.push_back(v); }
      frame.draws.push_back(d); frame.commands.push_back({gx::FrameCommand::Draw,i});
    }
    // Texture generator 7 has its own ninth byte of matrix-index input. Using
    // generator 6's byte instead samples the red half rather than the blue half.
    d.first_vertex=(uint32_t)frame.vertices.size();
    d.components=gx::VB_HAS_UV0 | (gx::VB_HAS_TEXMTXIDX0 << 7);
    d.xf_regs[0x3F]=8; d.bp.reg[gx::BP_GENMODE]=8;
    for (int i=0;i<8;++i) d.xf_regs[0x40+i]=5u<<7;
    d.bp.reg[gx::BP_TREF]=64 | (7u<<3);
    d.posMatrices[12]=d.posMatrices[17]=d.posMatrices[22]=1;
    d.posMatrices[15]=3;
    d.textures[0]=texture(0xB000,255,0);
    auto split=std::make_shared<gx::TextureSnapshot>(*d.textures[0].data);
    for(int i=0;i<16;++i) if(i%4>=2) { split->image[i*2+1]=0; split->image[33+i*2]=255; }
    split->hash=gx::hash_bytes(split->image.data(),split->image.size()); d.textures[0].data=split;
    const float band[4][2]={{-1,.5f},{1,.5f},{1,1},{-1,1}};
    for(auto& p:band) { gx::Vertex v{}; v.pos[0]=p[0]; v.pos[1]=p[1]; v.texmtx[7]=3; frame.vertices.push_back(v); }
    frame.commands.push_back({gx::FrameCommand::Draw,(uint32_t)frame.draws.size()}); frame.draws.push_back(d);
    gx::EfbCopy present{}; present.to_xfb=true; present.src_w=640; present.src_h=480; present.y_scale=1;
    frame.copies.push_back(present); frame.commands.push_back({gx::FrameCommand::Copy,1});
    for (unsigned n=0; n<6; ++n) { frame.sequence=n+1; renderer->submit_frame(frame); }
    // Replay the same immutable packets through a new device/backend: their
    // cached PSO pointers must never be reused after the original owner dies.
    renderer.reset();
    options.sharpness=0.5f;
    renderer.reset(gx::create_d3d12_backend(window,640,480,options));
    uint32_t warmed = 0; gx::d3d12_stats(renderer.get(), nullptr, &warmed, nullptr);
    check(warmed > 0, "recorded pipelines prewarm before the next draw");
    for (unsigned n=0; n<6; ++n) {
      if (n==1) gx::d3d12_resize(renderer.get(),800,600);
      if (n==2) gx::d3d12_resize(renderer.get(),1280,720);
      if (n==3) gx::d3d12_resize(renderer.get(),640,480);
      frame.sequence=n+1; renderer->submit_frame(frame);
    }
    const auto timing=gx::d3d12_gpu_timing(renderer.get());
    check(timing.submission==3 && timing.simulation==3 && timing.presented && timing.milliseconds>0,
          "GPU timestamps retain the completed source identity across resizing and slot reuse");
    renderer.reset(); DestroyWindow(window); window=nullptr;
    std::ifstream file(options.capture_path,std::ios::binary); std::string magic; int w,h,max;
    file>>magic>>w>>h>>max; file.get(); check(magic=="P6"&&w==640&&h==480&&max==255,"capture header");
    std::vector<unsigned char> image(w*h*3); file.read((char*)image.data(),image.size()); check((size_t)file.gcount()==image.size(),"capture size");
    const auto* l=&image[(240*w+160)*3]; const auto* r=&image[(240*w+480)*3];
    printf("left=%u,%u,%u right=%u,%u,%u\n",l[0],l[1],l[2],r[0],r[1],r[2]);
    check(l[0]>240&&l[2]<10,"earlier red draw survives descriptor/upload rollover");
    check(r[2]>240&&r[0]<10,"later blue draw has independent descriptors");
    const auto* top=&image[(60*w+320)*3];
    check(top[2]>240&&top[0]<10,"texture generator 7 uses its own matrix index");
    puts("D3D12 descriptor and upload lifetime regression passed"); return 0;
  } catch(const std::exception& e) { if(window)DestroyWindow(window); fprintf(stderr,"%s\n",e.what()); return 1; }
}
