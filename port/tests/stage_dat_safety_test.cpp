// SPDX-License-Identifier: GPL-2.0-or-later
#include "stage_dat_safety.h"
#include "stage_graft.h"
#include <cstdio>
#include <fstream>
#include <iterator>

static void put(std::vector<uint8_t>& b, size_t at, uint32_t n) {
  b[at] = n >> 24; b[at+1] = n >> 16; b[at+2] = n >> 8; b[at+3] = n;
}
// Three public gameplay roots; one model, one joint and an arbitrary render mesh.
static std::vector<uint8_t> fixture(uint32_t shift, uint8_t mesh) {
  const uint32_t size = 384 + shift;
  const std::vector<uint32_t> reloc{8+shift, 48+shift, 96+16+shift};
  const std::vector<std::pair<std::string,uint32_t>> roots{{"map_head",shift},{"coll_data",256+shift},{"grGroundParam",288+shift}};
  const size_t table = 32 + size + reloc.size()*4, strings = table + roots.size()*8;
  std::vector<uint8_t> b(strings,0);
  for (const auto& r:roots) b.insert(b.end(),r.first.c_str(),r.first.c_str()+r.first.size()+1);
  put(b,0,(uint32_t)b.size()); put(b,4,size); put(b,8,(uint32_t)reloc.size()); put(b,12,(uint32_t)roots.size());
  put(b,32+shift+8,48+shift); put(b,32+shift+12,1);
  put(b,32+shift+48,96+shift); put(b,32+shift+96+16,192+shift);
  b[32+192+shift]=mesh;
  for(size_t i=0;i<reloc.size();++i) put(b,32+size+4*i,reloc[i]);
  uint32_t name=0;
  for(size_t i=0;i<roots.size();++i) { put(b,table+8*i,roots[i].second); put(b,table+8*i+4,name); name+=(uint32_t)roots[i].first.size()+1; }
  return b;
}
int main(int argc,char** argv) {
  using host::cosmetics::stage_safety::matches;
  std::string reason;
  // <original> <skin>: the skin as installed, then its online form (restore_gameplay).
  // <original> --exporter-flags <out>: the original with CLASSICAL_SCALE (0x8) and the root draw bits
  // (0x30000000) cleared on every joint, as some exporters write a stage, for a check on a real file.
  if(argc==3 || argc==4) {
    std::ifstream a(argv[1],std::ios::binary);
    const std::vector<uint8_t> clean{std::istreambuf_iterator<char>(a),{}};
    std::vector<uint8_t> skin;
    if(argc==4) {
      if(std::string(argv[2])!="--exporter-flags") return 2;
      host::cosmetics::stage_safety::Archive parsed{clean};
      if(!parsed.parse()) { std::printf("FAIL: original does not parse\n"); return 1; }
      skin=clean;
      for(uint32_t j:parsed.joint_offsets) put(skin,32ull+j+4,parsed.get(j+4)&~0x30000008u);
      std::ofstream(argv[3],std::ios::binary).write((const char*)skin.data(),(std::streamsize)skin.size());
      std::printf("%zu joints flagged\n",parsed.joint_offsets.size());
    } else {
      std::ifstream b(argv[2],std::ios::binary);
      skin.assign(std::istreambuf_iterator<char>(b),{});
    }
    const bool raw=matches(clean,skin,&reason);
    std::printf("as installed: %s: %s\n",raw?"PASS":"FAIL",reason.c_str());
    if(raw) return 0;
    std::vector<uint8_t> online;
    const bool ok=host::cosmetics::stage_safety::restore_gameplay(clean,skin,&online,&reason);
    std::printf("online form: %s: %s\n",ok?"PASS":"FAIL",reason.c_str());
    if(ok) return 0;
    // Then drawn over the original, as the game tries next: from the repaired copy, then the skin,
    // with the models the skin's file name (its stage) allows whole.
    const auto whole=host::cosmetics::stage_safety::visual_only_models(argc==3?argv[2]:argv[1]);
    for(const std::vector<uint8_t>* base:{&online,&skin}) {
      if(base->empty()) continue;
      std::vector<uint8_t> out; std::string note;
      const bool grafted=host::cosmetics::stage_safety::graft_visuals(clean,*base,&out,&note,whole);
      std::printf("drawn over the original (%s): %s: %s\n",base==&online?"repaired":"skin",grafted?"PASS":"FAIL",note.c_str());
      if(grafted) return 0;
    }
    return 1;
  }
  int failed=0;
  const auto check=[&](bool ok,const char* name) { if(!ok) { std::fprintf(stderr,"FAIL %s: %s\n",name,reason.c_str()); ++failed; } };
  auto a=fixture(0,1), b=fixture(32,2);
  check(matches(a,b,&reason),"relocated visual mesh");
  {
    // Append one draw-only sibling without moving any original joint or gameplay object.
    auto scenery = a;
    const uint32_t size = 384, nr = 3;
    scenery.insert(scenery.begin() + 32 + size, 64, 0);
    scenery.insert(scenery.begin() + 32 + size + 64 + nr * 4, 8, 0);
    put(scenery, 0, (uint32_t)scenery.size()); put(scenery, 4, size + 64); put(scenery, 8, nr + 2);
    put(scenery, 32 + 96 + 12, size);
    put(scenery, 32 + size + 16, 192);
    put(scenery, 32 + size + 64 + nr * 4, 96 + 12);
    put(scenery, 32 + size + 64 + (nr + 1) * 4, size + 16);
    check(matches(a, scenery, &reason), "trailing scenery keeps original joint indices");
    auto dangerous = scenery; put(dangerous, 32 + size + 4, 0x4000);
    check(!matches(a, dangerous, &reason), "added spline is rejected");
    dangerous = scenery; put(dangerous, 32 + size + 16, 256);
    check(!matches(a, dangerous, &reason), "added scenery cannot reach collision data");
    dangerous = scenery; put(dangerous, 32 + size + 16, size);
    check(!matches(a, dangerous, &reason), "added scenery cannot reach itself");
    std::vector<uint8_t> grafted; std::string note;
    check(host::cosmetics::stage_safety::graft_visuals(a, scenery, &grafted, &note), "graft retains trailing scenery");
    check(matches(a, grafted, &reason), "grafted scenery retains gameplay proof");
  }
  auto changed=b; changed[32+256+32]=1;
  check(!matches(a,changed,&reason),"changed collision");
  changed=b; changed[32+288+32]=1;
  check(!matches(a,changed,&reason),"changed stage parameters");
  changed=b; changed[32+96+32+44]=1;
  check(!matches(a,changed,&reason),"changed platform transform");
  changed=b; put(changed,32+96+32+4,0x4000);
  check(!matches(a,changed,&reason),"spline behavior flag");
  changed=b; put(changed,32+96+32+4,0x10);
  check(!matches(a,changed,&reason),"hidden collision joint");
  // Stage exporters can change CLASSICAL_SCALE (0x8) on every joint. Online must
  // restore the original flag while keeping the skin's render mesh, rather than
  // either accepting changed transforms or discarding most meshes during grafting.
  {
    using host::cosmetics::stage_safety::restore_gameplay;
    auto exported = b;
    put(exported, 32 + 96 + 32 + 4, 0x30000008u);
    check(!matches(a, exported, &reason), "raw classical-scale change remains unsafe");
    std::vector<uint8_t> repaired;
    check(restore_gameplay(a, exported, &repaired, &reason), "restore exporter transform flag for online");
    check(matches(a, repaired, &reason), "repaired joint passes full gameplay comparison");
    check(repaired.size() == exported.size() && repaired[32 + 192 + 32] == 2,
          "repair retains the skin mesh and archive layout");
    check(host::cosmetics::stage_safety::word(repaired.data() + 32 + 96 + 32 + 4) == 0x30000000u,
          "repair retains visual flags and restores the original gameplay flags");
    auto moved = exported;
    moved[32 + 96 + 32 + 44] = 1;
    check(!restore_gameplay(a, moved, &repaired, &reason), "changed joint position still fails gameplay proof");
  }
  changed=b; put(changed,32+32+8,0xfffffffc);
  check(!matches(a,changed,&reason),"out of bounds pointer");
  changed=b; put(changed,16,1);
  check(!matches(a,changed,&reason),"external reference");
  changed=b; changed.resize(16);
  check(!matches(a,changed,&reason),"truncation");
  // A skin drawn over the original stage: the original's gameplay, the skin's mesh.
  {
    using host::cosmetics::stage_safety::graft_visuals;
    std::vector<uint8_t> out; std::string note;
    const bool grafted = graft_visuals(a, b, &out, &note);
    reason = note;
    check(grafted, "graft of a mesh-only skin");
    check(grafted && matches(a, out, &reason), "the grafted stage matches the original's gameplay");
    // The joint's mesh now points at the skin's mesh (its first byte is the skin's 2).
    const auto word = [&](size_t at) { return uint32_t(out[at]) << 24 | uint32_t(out[at+1]) << 16 | uint32_t(out[at+2]) << 8 | out[at+3]; };
    check(grafted && out.size() > a.size() && out[32 + word(32 + 96 + 16)] == 2, "the grafted joint draws the skin's mesh");
    auto moved = b; moved[32 + 256 + 32 + 0x10] = 1;   // a collision line range of the skin
    check(!graft_visuals(a, moved, &out, &note), "a skin that changes the collision is not grafted");
  }
  if(!failed) std::puts("stage gameplay safety tests passed");
  return failed?1:0;
}
