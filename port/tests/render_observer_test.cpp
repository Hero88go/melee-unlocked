#include "render_observer.h"
#include "ppc.h"
#include <cstring>
#include <stdexcept>
#include <cstdio>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() {
  ppc::Context c{}; c.r[3]=0x80001000;
  { gx::RenderObserver scope(c,gx::Observe::AllocateJoint); }
  auto saved=c; uint64_t gen=0, first=0;
  { gx::RenderObserver scope(c,gx::Observe::DisplayJoint);
    first=gx::observed_draw_identity(7,gen); check(gen!=0,"allocated joint has a generation");
    uint64_t g2; check(first!=gx::observed_draw_identity(7,g2),"draw ordinals are unique");
  }
  check(!std::memcmp(&c,&saved,sizeof c),"observer does not change guest CPU state");
  gx::finish_observed_frame();
  { gx::RenderObserver scope(c,gx::Observe::DisplayJoint);
    uint64_t g2; check(first==gx::observed_draw_identity(7,g2)&&gen==g2,"same lifetime pairs across frames");
    auto other=c; other.r[3]=0x80002000;
    { gx::RenderObserver scope2(other,gx::Observe::DisplayJoint);
      check(gx::observed_draw_identity(99,g2)==99&&g2==0,"unobserved nested joint uses fallback"); }
    gx::observed_draw_identity(7,g2); check(g2==gen,"nested display restores parent identity");
  }
  { gx::RenderObserver scope(c,gx::Observe::ReleaseJoint); }
  { gx::RenderObserver scope(c,gx::Observe::AllocateJoint); }
  gx::finish_observed_frame();
  { gx::RenderObserver scope(c,gx::Observe::DisplayJoint);
    uint64_t g2; check(first!=gx::observed_draw_identity(7,g2)&&gen!=g2,"reused address has a fresh lifetime"); }
  puts("joint generation, nested scope, and guest isolation checks passed");
}
