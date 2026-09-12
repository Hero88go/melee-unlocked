// SPDX-License-Identifier: GPL-2.0-or-later
#include "authored_pose.h"
#include "Geometry.h"
#include "subframe.h"
#include <atomic>
#include <cmath>
#include <cstring>
namespace gx {
AuthoredStats& authored_stats() { static AuthoredStats st; return st; }
static std::atomic<bool> g_interpolate{false};
void set_authored_interpolate(bool on) { g_interpolate.store(on, std::memory_order_relaxed); }
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
// Inverse-transpose of the 3x3 part (the GX normal matrix), row-major 3x3 output.
bool normal_matrix(const Matrix& m, float out[9]) {
  Matrix inv; if(!inverse(m,inv))return false;
  for(int r=0;r<3;++r)for(int c=0;c<3;++c)out[r*3+c]=inv[c*4+r];
  return true;
}
Matrix from12(const float* p) { Matrix m; std::memcpy(m.data(),p,48); return m; }
const AuthoredPose& chain_of(const AuthoredPose& p) { return p.chain ? *p.chain : p; }
// Camera: the view matrix advanced `phase` frames by screw extrapolation of its last change. Returns
// the sampled view and the transform that carries a current view-space matrix to it.
bool camera_motion(const AuthoredPose& previous,const AuthoredPose& current,double phase,Matrix& view_new,Matrix& carry,bool& moved) {
  moved=false;
  if(!current.has_view){ view_new=NativeMelee::Identity(); carry=view_new; return false; }
  Matrix cur=from12(current.view.data());
  view_new=cur; carry=NativeMelee::Identity();
  if(!previous.has_view||previous.view==current.view) return true;
  Matrix prev=from12(previous.view.data());
  if(g_interpolate.load(std::memory_order_relaxed)) SubFrameSolver::interpolate_matrix(prev.data(),cur.data(),phase,view_new.data());
  else SubFrameSolver::extrapolate_matrix(prev.data(),cur.data(),phase,view_new.data());
  Matrix inv_cur; if(!inverse(cur,inv_cur)){ view_new=cur; return true; }
  carry=NativeMelee::Multiply(view_new,inv_cur);
  moved=view_new!=cur;
  return true;
}
}
// The expensive part: re-sample every joint's authored tracks at the fractional frame and
// rebuild the chain's world matrix. Shared by all draws of the same object in a presented frame.
// `allow_static` accepts chains with no animated track (their world matrix simply holds), which
// skinned models need for bones that are not moving this frame.
static bool sample_chain(const AuthoredPose& previous,const AuthoredPose& current,double phase,Matrix& world,Matrix& inv,bool allow_static) {
  if(!std::isfinite(phase)||phase<0||phase>1||current.joints.empty()||previous.joints.size()!=current.joints.size()){ ++authored_stats().sample[1]; return false; }
  world=NativeMelee::Identity(); Matrix exact=world;
  NativeMelee::Vec inherited{{1,1,1}};
  bool animated=false;
  try {
    for(size_t i=0;i<current.joints.size();++i) {
      const auto& j=current.joints[i]; const auto& p=previous.joints[i];
      if(!j.generation||j.generation!=p.generation||j.flags!=p.flags||j.tracks.size()!=p.tracks.size()){ ++authored_stats().sample[2]; return false; }
      const bool interp=g_interpolate.load(std::memory_order_relaxed);
      // Predict: sample `phase` frames past the current frame. Interpolate: sample `phase` frames
      // past the previous frame (exact at both ends, shown one frame late).
      const float sample_frame=interp?float(p.frame+phase*j.rate):float(j.frame+phase*j.rate);
      auto scale=j.scale,rot=j.rotation,pos=j.translation;
      bool driven[3]={false,false,false};
      for(const auto& t:j.tracks)if(t.channel>=5&&t.channel<=7)driven[t.channel-5]=true;
      if(!j.tracks.empty()&&(!near(j.frame-p.frame,j.rate)||sample_frame<0||sample_frame>j.end)){ ++authored_stats().sample[3]; return false; }
      for(size_t k=0;k<j.tracks.size();++k) {
        const auto& t=j.tracks[k]; if(!same_track(t,p.tracks[k])){ ++authored_stats().sample[4]; return false; }
        float* component=nullptr;
        if(t.channel>=1&&t.channel<=3)component=&rot[t.channel-1];
        else if(t.channel>=5&&t.channel<=7)component=&pos[t.channel-5];
        else if(t.channel>=8&&t.channel<=10)component=&scale[t.channel-8];
        else { ++authored_stats().sample[5]; return false; }
        float at_current, value;
        if(!NativeMelee::SamplePacked(t,j.frame,at_current)||!near(at_current,*component)){ ++authored_stats().sample[6]; return false; }
        if(!NativeMelee::SamplePacked(t,sample_frame,value)){ ++authored_stats().sample[7]; return false; }
        *component=value; animated=true;
      }
      // Game-driven motion (fighter positions, items, knockback) has no track: predict it forward by
      // the last simulated per-frame delta, bounded so teleports and respawns hold instead.
      for(int k=0;k<3;++k){
        if(driven[k])continue;
        float delta=j.translation[k]-p.translation[k];
        if(delta!=0.0f&&std::isfinite(delta)&&std::abs(delta)<=30.0f){ pos[k]=interp?p.translation[k]+float(phase)*delta:j.translation[k]+float(phase)*delta; animated=true; }
      }
      exact=NativeMelee::Multiply(exact,NativeMelee::SRT(j.scale,j.rotation,j.translation,inherited));
      for(int k=0;k<12;++k)if(!near(exact[k],j.world[k])){ ++authored_stats().sample[8]; return false; }
      // Sampled scale feeds the children's inherited scale exactly as the current scale does in
      // the validated reconstruction above.
      world=NativeMelee::Multiply(world,NativeMelee::SRT(scale,rot,pos,inherited));
      if(!(j.flags&8))for(int k=0;k<3;++k)inherited[k]*=scale[k];
    }
  } catch(const std::exception&) { { ++authored_stats().sample[10]; return false; } }
  if(!animated&&!allow_static){ ++authored_stats().sample[11]; return false; }
  if(!inverse(current.joints.back().world,inv)){ ++authored_stats().sample[12]; return false; }
  return true;
}
static bool cached_chain(const AuthoredPose& previous,const AuthoredPose& current,double phase,Matrix& world,Matrix& inv,AuthoredCache* cache,bool allow_static) {
  if(!cache)return sample_chain(previous,current,phase,world,inv,allow_static);
  auto key=std::make_pair(&previous,&current);
  auto it=cache->find(key);
  if(it==cache->end()) {
    AuthoredChain chain; chain.ok=sample_chain(previous,current,phase,chain.world,chain.inverse_current,allow_static);
    it=cache->emplace(key,chain).first;
  }
  if(!it->second.ok)return false;
  world=it->second.world; inv=it->second.inverse_current;
  return true;
}
bool sample_authored(const AuthoredPose& previous,const AuthoredPose& current,double phase,
                     const float matrix[12],float result[12],float normals[9],const float current_normals[9],AuthoredCache* cache) {
  if(current.envelope||previous.envelope)return false;
  Matrix view_new,carry; bool camera_moved=false;
  camera_motion(previous,current,phase,view_new,carry,camera_moved);
  Matrix world,inv;
  if(!cached_chain(chain_of(previous),chain_of(current),phase,world,inv,cache,camera_moved))return false;
  Matrix base; std::memcpy(base.data(),matrix,48);
  auto output=NativeMelee::Multiply(carry,NativeMelee::Multiply(NativeMelee::Multiply(base,inv),world));
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

// Skinned draws (SetupEnvelopeModelMtx): slot = view * (sum_k weight_k * world_k * envelope_k) [* right].
// Computed twice per slot: with the current worlds (must match the draw's matrices, which proves the
// reconstruction) and with the sampled worlds at the fractional frame (the output).
bool sample_authored_envelope(const AuthoredPose& previous,const AuthoredPose& current,double phase,
                              const float current_pos[256],const float current_nrm[96],float out_pos[256],float out_nrm[96],AuthoredCache* cache) {
  if(!current.envelope||!previous.envelope||current.slots.empty()||current.slots.size()!=previous.slots.size()||current.slots.size()>10){ ++authored_stats().sample[16]; return false; }
  if(current.right_kind!=previous.right_kind){ ++authored_stats().sample[16]; return false; }
  Matrix view_new,carry; bool camera_moved=false;
  camera_motion(previous,current,phase,view_new,carry,camera_moved);
  const Matrix view=from12(current.view.data());
  // The skeleton-root transform, current and sampled.
  Matrix right_cur=NativeMelee::Identity(), right_new=NativeMelee::Identity();
  bool has_right=current.right_kind!=0;
  if(has_right) {
    if(!current.right_chain_m||!current.right_chain_x||!previous.right_chain_m||!previous.right_chain_x){ ++authored_stats().sample[17]; return false; }
    Matrix wm_new,wm_inv,wx_new,wx_inv;
    if(!cached_chain(*previous.right_chain_m,*current.right_chain_m,phase,wm_new,wm_inv,cache,true)||
       !cached_chain(*previous.right_chain_x,*current.right_chain_x,phase,wx_new,wx_inv,cache,true)){ ++authored_stats().sample[17]; return false; }
    const Matrix wm_cur=current.right_chain_m->joints.back().world, wx_cur=current.right_chain_x->joints.back().world;
    const Matrix xenv=from12(current.right_envelope.data());
    auto make=[&](const Matrix& wm,const Matrix& wx,Matrix& out)->bool{
      if(current.right_kind==1){ return inverse(xenv,out); }
      if(current.right_kind==2){ Matrix inv; if(!inverse(wx,inv))return false; out=NativeMelee::Multiply(inv,wm); return true; }
      Matrix n=NativeMelee::Multiply(wx,xenv), inv; if(!inverse(n,inv))return false; out=NativeMelee::Multiply(inv,wm); return true;
    };
    if(!make(wm_cur,wx_cur,right_cur)||!make(wm_new,wx_new,right_new)){ ++authored_stats().sample[17]; return false; }
  }
  std::memcpy(out_pos,current_pos,256*sizeof(float));
  std::memcpy(out_nrm,current_nrm,96*sizeof(float));
  for(size_t s=0;s<current.slots.size();++s) {
    const auto& slot=current.slots[s]; const auto& pslot=previous.slots[s];
    if(slot.bones.size()!=pslot.bones.size()||slot.bones.empty()){ ++authored_stats().sample[18]; return false; }
    Matrix blend_cur{}, blend_new{};
    for(size_t b=0;b<slot.bones.size();++b) {
      const auto& bone=slot.bones[b]; const auto& pbone=pslot.bones[b];
      if(!bone.chain||!pbone.chain||!near(bone.weight,pbone.weight)||bone.envelope!=pbone.envelope){ ++authored_stats().sample[18]; return false; }
      Matrix w_new,w_inv;
      if(!cached_chain(*pbone.chain,*bone.chain,phase,w_new,w_inv,cache,true)){ ++authored_stats().sample[19]; return false; }
      const Matrix w_cur=bone.chain->joints.back().world;
      const Matrix env=from12(bone.envelope.data());
      // SetupEnvelopeModelMtx: a single full-weight bone without a skeleton-root transform uses the
      // joint matrix alone; every other case multiplies by the inverse-bind (envelope) matrix.
      bool bare = slot.bones.size()==1 && bone.weight>=1.0f-1.1920929e-7f && !has_right;
      Matrix c=bare?w_cur:NativeMelee::Multiply(w_cur,env), n=bare?w_new:NativeMelee::Multiply(w_new,env);
      for(int k=0;k<12;++k){ blend_cur[k]+=bone.weight*c[k]; blend_new[k]+=bone.weight*n[k]; }
    }
    if(has_right){ blend_cur=NativeMelee::Multiply(blend_cur,right_cur); blend_new=NativeMelee::Multiply(blend_new,right_new); }
    Matrix m_cur=NativeMelee::Multiply(view,blend_cur), m_new=NativeMelee::Multiply(view_new,blend_new);
    // Proof: the reconstruction of the current pose must reproduce the matrix the game loaded.
    const float* actual=current_pos+12*s;
    for(int k=0;k<12;++k)if(!near(m_cur[k],actual[k])){ ++authored_stats().sample[20]; return false; }
    for(float v:m_new)if(!std::isfinite(v)){ ++authored_stats().sample[21]; return false; }
    float nrm[9];
    if(!normal_matrix(m_new,nrm)){ ++authored_stats().sample[22]; return false; }
    std::memcpy(out_pos+12*s,m_new.data(),48);
    if(9*s+9<=96)std::memcpy(out_nrm+9*s,nrm,sizeof nrm);
  }
  ++authored_stats().sampled;
  return true;
}
}
