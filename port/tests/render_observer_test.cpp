#include "render_observer.h"
#include "ppc.h"
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include <vector>
#include "authored_pose.h"
#include "Geometry.h"
static void check(bool ok, const char* why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); throw std::runtime_error(why); } }
namespace gx { bool authored_capture_wanted() { return true; } }   // gx_core.cpp, not linked here
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
  // ftDrawCommon adds the hit/grab model shake to the view, not the fighter's
  // position. It must stay at its 60 Hz offset while real knockback advances.
  {
    std::vector<uint8_t> memory(ppc::RAM_SIZE);
    auto word = [&](uint32_t a, uint32_t v) {
      auto* p = memory.data() + a - ppc::RAM_BASE;
      p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
    };
    auto real = [&](uint32_t a, float v) { uint32_t u; std::memcpy(&u, &v, 4); word(a, u); };
    constexpr uint32_t camera = 0x80003000, view = 0x80004000;
    word(0x804D765C, camera); // HSD_CObjGetCurrent
    for (int k = 0; k < 12; ++k) real(camera + 0x54 + k * 4, k == 0 || k == 5 || k == 10 ? 1.f : 0.f);
    c.r[3] = 0x80001000; c.r[4] = view;
    gx::set_authored_capture(true);
    auto capture = [&](float position, float shake, float camera_x = 0.f) {
      gx::finish_observed_frame();
      real(camera + 0x54 + 3 * 4, camera_x);
      for (int k = 0; k < 3; ++k) real(c.r[3] + 0x2C + k * 4, 1);
      real(c.r[3] + 0x38, position);
      for (int k = 0; k < 12; ++k) {
        const float identity = k == 0 || k == 5 || k == 10 ? 1.f : 0.f;
        real(c.r[3] + 0x44 + k * 4, k == 3 ? position : identity);
        real(view + k * 4, k == 3 ? camera_x + shake : identity);
      }
      gx::RenderObserver display(c, gx::Observe::DisplayJoint, memory.data());
      gx::RenderObserver matrix(c, gx::Observe::RigidMatrix, memory.data());
      return gx::capture_authored_pose();
    };
    auto previous = capture(0, 2), current = capture(4, -2);
    check(previous && current, "fighter hit-shake poses captured");
    float held[256]{}, normals[96]{}, result[256]{}, result_normals[96]{};
    auto identity = NativeMelee::Identity();
    std::memcpy(held, identity.data(), 48); held[3] = 2; // current position 4 plus shake -2
    check(!gx::carry_camera(*previous, *current, .5, *current, held, normals, 1, result, result_normals),
          "hit shake alone never predicts a phantom camera step");
    float sampled[12], sampled_normals[9], identity_normals[9]{1,0,0,0,1,0,0,0,1};
    check(gx::sample_authored(*previous, *current, .5, held, sampled, sampled_normals, identity_normals) &&
          std::abs(sampled[3] - 4.f) < .0001f,
          "real knockback advances from 4 to 6 while hit shake remains -2");
    current = capture(8, 0);
    check(!gx::carry_camera(*previous, *current, .5, *current, held, normals, 1, result, result_normals),
          "ending a hit shake does not predict its return");
    // The model's offset can cancel a camera pan exactly in the raw view. The
    // underlying camera still has to move; HUD views without model shake hold.
    current = capture(4, -2, 4);
    held[3] = 6; // fighter x4 + camera x4 + model shake -2
    check(gx::sample_authored(*previous, *current, .5, held, sampled, sampled_normals, identity_normals) &&
          std::abs(sampled[3] - 10.f) < .0001f,
          "camera pan keeps moving when hit shake cancels its captured view step");
  }
  puts("joint generation, nested scope, guest isolation and hit-shake checks passed");
}
