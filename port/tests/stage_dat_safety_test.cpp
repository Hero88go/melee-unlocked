// SPDX-License-Identifier: GPL-2.0-or-later
#include "stage_dat_safety.h"
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
  if(argc==3) {
    std::ifstream a(argv[1],std::ios::binary),b(argv[2],std::ios::binary);
    const bool ok=matches({std::istreambuf_iterator<char>(a),{}},{std::istreambuf_iterator<char>(b),{}},&reason);
    std::printf("%s: %s\n",ok?"PASS":"FAIL",reason.c_str()); return ok?0:1;
  }
  int failed=0;
  const auto check=[&](bool ok,const char* name) { if(!ok) { std::fprintf(stderr,"FAIL %s: %s\n",name,reason.c_str()); ++failed; } };
  auto a=fixture(0,1), b=fixture(32,2);
  check(matches(a,b,&reason),"relocated visual mesh");
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
  changed=b; put(changed,32+32+8,0xfffffffc);
  check(!matches(a,changed,&reason),"out of bounds pointer");
  changed=b; put(changed,16,1);
  check(!matches(a,changed,&reason),"external reference");
  changed=b; changed.resize(16);
  check(!matches(a,changed,&reason),"truncation");
  if(!failed) std::puts("stage gameplay safety tests passed");
  return failed?1:0;
}
