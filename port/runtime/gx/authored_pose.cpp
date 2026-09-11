// SPDX-License-Identifier: GPL-2.0-or-later
#include "authored_pose.h"
#include "Geometry.h"
#include <cmath>
#include <cstring>
namespace gx {
AuthoredStats& authored_stats() { static AuthoredStats st; return st; }
namespace {
using NativeMelee::Matrix;
bool near(float a,float b) { return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=0.002f*(1+std::abs(b)); }
bool inverse(const Matrix& m,Matrix& o) {
  double det=m[0]*(double(m[5])*m[10]-double(m[6])*m[9])-m[1]*(double(m[4])*m[10]-double(m[6])*m[8])+m[2]*(double(m[4])*m[9]-double(m[5])*m[8]);
  if (!std::isfinite(det)||std::abs(det)<1e-12) return false;
  const int rows[3][2]={{1,2},{2,0},{0,1}};
  for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
    int a=rows[j][0],b=rows[j][1],c=rows[i][0],d=rows[i][1];
    o[i*4+j]=float((double(m[a*4+c])*m[b*4+d]-double(m[a*4+d])*m[b*4+c])/det);
  }
  for(int i=0;i<3;++i)o[i*4+3]=-(o[i*4]*m[3]+o[i*4+1]*m[7]+o[i*4+2]*m[11]);
  return true;
}
bool same_track(const NativeMelee::PackedTrack& a,const NativeMelee::PackedTrack& b) {
  return a.start_frame==b.start_frame&&a.channel==b.channel&&a.value_format==b.value_format&&a.slope_format==b.slope_format&&a.bytes==b.bytes;
}
}
// The expensive part: re-sample every joint's authored tracks at the fractional frame and
// rebuild the chain's world matrix. Shared by all draws of the same object in a presented frame.
static bool sample_chain(const AuthoredPose& previous,const AuthoredPose& current,double phase,Matrix& world,Matrix& inv) {
  if(!std::isfinite(phase)||phase<0||phase>1||current.joints.empty()||previous.joints.size()!=current.joints.size()){ ++authored_stats().sample[1]; return false; }
  world=NativeMelee::Identity(); Matrix exact=world;
  NativeMelee::Vec inherited{{1,1,1}};
  bool animated=false;
  try {
    for(size_t i=0;i<current.joints.size();++i) {
      const auto& j=current.joints[i]; const auto& p=previous.joints[i];
      if(!j.generation||j.generation!=p.generation||j.flags!=p.flags||j.tracks.size()!=p.tracks.size()){ ++authored_stats().sample[2]; return false; }
      auto scale=j.scale,rot=j.rotation,pos=j.translation;
      if(!j.tracks.empty()&&(!near(j.frame-p.frame,j.rate)||j.frame-(1-phase)*j.rate<0||j.frame>j.end)){ ++authored_stats().sample[3]; return false; }
      for(size_t k=0;k<j.tracks.size();++k) {
        const auto& t=j.tracks[k]; if(!same_track(t,p.tracks[k])){ ++authored_stats().sample[4]; return false; }
        float* component=nullptr;
        if(t.channel>=1&&t.channel<=3)component=&rot[t.channel-1];
        else if(t.channel>=5&&t.channel<=7)component=&pos[t.channel-5];
        else if(t.channel>=8&&t.channel<=10)component=&scale[t.channel-8];
        else { ++authored_stats().sample[5]; return false; }
        float at_current, value;
        if(!NativeMelee::SamplePacked(t,j.frame,at_current)||!near(at_current,*component)){ ++authored_stats().sample[6]; return false; }
        if(!NativeMelee::SamplePacked(t,float(j.frame-(1-phase)*j.rate),value)){ ++authored_stats().sample[7]; return false; }
        *component=value; animated=true;
      }
      exact=NativeMelee::Multiply(exact,NativeMelee::SRT(j.scale,j.rotation,j.translation,inherited));
      for(int k=0;k<12;++k)if(!near(exact[k],j.world[k])){ ++authored_stats().sample[8]; return false; }
      // Animated scale changes affect inherited scale; do not approximate that special case yet.
      if(scale!=j.scale){ ++authored_stats().sample[9]; return false; }
      world=NativeMelee::Multiply(world,NativeMelee::SRT(scale,rot,pos,inherited));
      if(!(j.flags&8))for(int k=0;k<3;++k)inherited[k]*=j.scale[k];
    }
  } catch(const std::exception&) { { ++authored_stats().sample[10]; return false; } }
  if(!animated){ ++authored_stats().sample[11]; return false; }
  if(!inverse(current.joints.back().world,inv)){ ++authored_stats().sample[12]; return false; }
  return true;
}
bool sample_authored(const AuthoredPose& previous,const AuthoredPose& current,double phase,
                     const float matrix[12],float result[12],float normals[9],const float current_normals[9],AuthoredCache* cache) {
  Matrix world,inv;
  if(cache) {
    auto key=std::make_pair(&previous,&current);
    auto it=cache->find(key);
    if(it==cache->end()) {
      AuthoredChain chain; chain.ok=sample_chain(previous,current,phase,chain.world,chain.inverse_current);
      it=cache->emplace(key,chain).first;
    }
    if(!it->second.ok)return false;
    world=it->second.world; inv=it->second.inverse_current;
  } else if(!sample_chain(previous,current,phase,world,inv))return false;
  Matrix base; std::memcpy(base.data(),matrix,48);
  auto output=NativeMelee::Multiply(NativeMelee::Multiply(base,inv),world);
  Matrix base_inv;
  if(!inverse(base,base_inv)){ ++authored_stats().sample[13]; return false; }
  auto delta=NativeMelee::Multiply(output,base_inv);
  Matrix delta_inv; if(!inverse(delta,delta_inv)){ ++authored_stats().sample[14]; return false; }
  for(float v:output)if(!std::isfinite(v)){ ++authored_stats().sample[15]; return false; }
  std::memcpy(result,output.data(),48);
  for(int r=0;r<3;++r)for(int c=0;c<3;++c)
    normals[r*3+c]=delta_inv[r]*current_normals[c]+delta_inv[4+r]*current_normals[3+c]+delta_inv[8+r]*current_normals[6+c];
  ++authored_stats().sampled;
  return true;
}
}
