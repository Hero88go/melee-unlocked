// A third backend must work without registration or a Direct3D dependency.
#include "gx_backend.h"
#include <cstdio>

struct TestBackend : gx::Backend {
  gx::RenderOptions opts{};
  int width=0, height=0;
  void submit_frame(const gx::Frame&) override {}
  const gx::RenderOptions* presentation_options() const override { return &opts; }
  void resize(int w,int h) override { width=w; height=h; }
  void presentation_stats(uint32_t* frames,uint32_t* pipelines,uint32_t* textures) const override {
    if(frames) *frames=13;
    if(pipelines) *pipelines=7;
    if(textures) *textures=5;
  }
  std::string profile_line() const override { return "third backend"; }
};
struct NullBackend : gx::Backend { void submit_frame(const gx::Frame&) override {} };
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main() {
  TestBackend first, second;
  gx::Backend* backend=&first;
  CHECK(&gx::render_options(backend)==&first.opts);
  gx::render_resize(backend,1920,1080);
  CHECK(first.width==1920 && first.height==1080 && second.width==0);
  uint32_t f=0,p=0,t=0;
  gx::render_stats(backend,&f,&p,&t);
  CHECK(f==13 && p==7 && t==5);
  gx::render_stats(backend,nullptr,&p,nullptr);
  CHECK(gx::render_profile_line(backend)=="third backend");
  NullBackend null;
  gx::render_stats(&null,&f,&p,&t);
  CHECK(f==0 && p==0 && t==0);
  CHECK(gx::render_profile_line(&null).empty());
  CHECK(&gx::render_options(&null)==&gx::render_options(nullptr));
  gx::render_resize(nullptr,1,2);
  f=p=t=99;
  gx::render_stats(nullptr,&f,&p,&t);
  CHECK(f==0 && p==0 && t==0 && gx::render_profile_line(nullptr).empty());
  return 0;
}
