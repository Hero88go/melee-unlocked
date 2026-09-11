// Guest execution remains authoritative. These hooks own host metadata only.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "render_observer.h"
#include "ppc.h"
#include "gx_texture.h"
#include "authored_pose.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>
namespace gx {
namespace {
std::unordered_map<uint32_t, uint64_t> joints;
std::unordered_map<uint64_t, uint32_t> passes;
uint64_t next_generation = 1, current_generation = 0, current_pass = 0;
uint32_t current_draw = 0, current_joint = 0;
uint8_t* current_memory = nullptr;
bool rigid = false, authored_enabled = false;
std::shared_ptr<const AuthoredPose> current_pose;
struct Reader {
  uint8_t* memory; bool valid = true;
  bool span(uint32_t a,uint32_t n) { if(a<ppc::RAM_BASE||a-ppc::RAM_BASE>ppc::RAM_SIZE||n>ppc::RAM_SIZE-(a-ppc::RAM_BASE))valid=false;return valid; }
  uint32_t word(uint32_t a) { if(!span(a,4))return 0;const auto* p=memory+a-ppc::RAM_BASE;return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
  float real(uint32_t a) { uint32_t v=word(a);float f;std::memcpy(&f,&v,4);return f; }
};
}
RenderObserver::RenderObserver(ppc::Context& cpu, Observe kind, uint8_t* memory) : cpu_(cpu), kind_(kind) {
  if (kind == Observe::ReleaseJoint) joints.erase(cpu.r[3]);
  if (kind == Observe::RigidMatrix) rigid = true;
  if (kind == Observe::OtherMatrix) rigid = false;
  if (kind != Observe::DisplayJoint) return;
  saved_joint_ = current_joint; current_joint = cpu.r[3];
  saved_memory_ = current_memory; current_memory = memory;
  saved_rigid_ = rigid; rigid = false;
  saved_pose_ = std::move(current_pose); current_pose.reset();
  saved_generation_ = current_generation; saved_pass_ = current_pass; saved_draw_ = current_draw;
  auto it = joints.find(cpu.r[3]);
  current_generation = it == joints.end() ? 0 : it->second;
  const uint64_t key[] = {current_generation, cpu.r[5], cpu.r[6]};
  uint64_t pass_key = hash_bytes(key, sizeof key);
  const uint64_t pass[] = {pass_key, passes[pass_key]++};
  current_pass = hash_bytes(pass, sizeof pass); current_draw = 0;
}
RenderObserver::~RenderObserver() {
  if (kind_ == Observe::AllocateJoint && cpu_.r[3]) joints[cpu_.r[3]] = next_generation++;
  if (kind_ == Observe::DisplayJoint) {
    current_joint = saved_joint_; current_memory = saved_memory_; rigid = saved_rigid_; current_pose = std::move(saved_pose_);
    current_generation = saved_generation_; current_pass = saved_pass_; current_draw = saved_draw_;
  }
}
uint64_t observed_draw_identity(uint64_t fallback, uint64_t& generation) {
  generation = current_generation;
  if (!generation) return fallback;
  const uint64_t key[] = {generation, current_pass, current_draw++};
  return hash_bytes(key, sizeof key);
}
void set_authored_capture(bool enabled) { authored_enabled = enabled; }
std::shared_ptr<const AuthoredPose> capture_authored_pose() {
  if(!authored_enabled||!rigid||!current_generation||!current_memory){ ++authored_stats().capture[1]; return {}; }
  if(current_pose)return current_pose;
  Reader r{current_memory};
  auto pose=std::make_shared<AuthoredPose>();
  uint32_t address=current_joint;
  size_t byte_count=0;
  while(address && pose->joints.size()<128) {
    if(!r.span(address,0x88)){ ++authored_stats().capture[2]; return {}; }
    AuthoredJoint j;
    auto g=joints.find(address); if(g==joints.end()){ ++authored_stats().capture[3]; return {}; } j.generation=g->second;
    j.flags=r.word(address+0x14)&~0x40u;
    // Billboards, instances, constraints, quaternion/IK and independent matrices
    // require their own authored evaluators, so retain exact captured draws.
    if((j.flags & (0x2E00u|0x1000u|0x20000u|0x600000u|0x3800000u))||r.word(address+0x80)){ ++authored_stats().capture[4]; return {}; }
    for(int k=0;k<3;++k) {j.rotation[k]=r.real(address+0x1C+k*4);j.scale[k]=r.real(address+0x2C+k*4);j.translation[k]=r.real(address+0x38+k*4);}
    for(int k=0;k<12;++k)j.world[k]=r.real(address+0x44+k*4);
    uint32_t aobj=r.word(address+0x7C);
    if(aobj) {
      if(!r.span(aobj,28)||r.word(aobj+24)){ ++authored_stats().capture[5]; return {}; }
      uint32_t flags=r.word(aobj);
      j.frame=r.real(aobj+4); j.end=r.real(aobj+12);
      if(!(flags&0x50000000u)) {
        j.rate=r.real(aobj+16); uint32_t fobj=r.word(aobj+20);
        while(fobj && j.tracks.size()<32) {
          if(!r.span(fobj,48)){ ++authored_stats().capture[6]; return {}; }
          NativeMelee::PackedTrack t;
          uint32_t data=r.word(fobj+8), length=r.word(fobj+12), packed=r.word(fobj+16), formats=r.word(fobj+20);
          if(length>65535||byte_count+length>1024*1024||!r.span(data,length)){ ++authored_stats().capture[7]; return {}; }
          t.start_frame=(int16_t)(r.word(fobj+24)>>16); t.channel=packed&255;
          if(!((t.channel>=1&&t.channel<=3)||(t.channel>=5&&t.channel<=10))){ ++authored_stats().capture[8]; return {}; }
          t.value_format=formats>>24; t.slope_format=(formats>>16)&255;
          t.bytes.assign(current_memory+data-ppc::RAM_BASE,current_memory+data-ppc::RAM_BASE+length);
          byte_count+=length; j.tracks.push_back(std::move(t)); fobj=r.word(fobj);
        }
        if(fobj){ ++authored_stats().capture[9]; return {}; }
      }
    }
    pose->joints.push_back(std::move(j)); address=r.word(address+12);
  }
  if(address||!r.valid){ ++authored_stats().capture[10]; return {}; }
  std::reverse(pose->joints.begin(),pose->joints.end()); ++authored_stats().captured; current_pose=pose; return pose;
}
void finish_observed_frame() { passes.clear(); }
}
