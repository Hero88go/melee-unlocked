#include "PackedAnimation.h"
#include "Assets.h"
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <cstring>
using namespace NativeMelee;
static void Check(bool value) {if(!value)throw std::runtime_error("Packed animation regression");}
static float Read(const PackedTrack& track,float frame) {float value=0;Check(SamplePacked(track,frame,value));return value;}
static size_t pairs = 0;
static void CheckPair(const PackedTrack& track, float first, float second) {
  PackedSampleCursor cursor;
  float initial=0, expected=123, actual=123;
  SamplePacked(track,first,initial,&cursor);
  const bool reference=SamplePacked(track,second,expected);
  const bool resumed=SamplePackedFromCursor(track,second,actual,cursor);
  Check(reference==resumed && !std::memcmp(&expected,&actual,sizeof expected));
  ++pairs;
}
int main(int argc,char** argv) {
  try {
    PackedTrack linear;linear.channel=5;linear.value_format=128;linear.bytes={0x12,0,2,10};
    Check(std::abs(Read(linear,.25f)-1.25f)<1e-6f);
    Check(std::abs(Read(linear,1.5f)-7.5f)<1e-6f);
    PackedTrack curved;curved.channel=1;curved.value_format=curved.slope_format=96;
    curved.bytes={0x14,0,4,1,0,252};
    Check(std::abs(Read(curved,.5f)-1.f)<1e-6f);
    Check(std::abs(Read(curved,.25f)-.75f)<1e-6f);
    const auto bytes=curved.bytes;
    Read(curved,.9f);Read(curved,.1f);Read(curved,.75f);
    Check(curved.bytes==bytes);
    bool rejected=false;
    try {PackedTrack broken=curved;broken.bytes={0x14};Read(broken,0);}
    catch(const std::runtime_error&) {rejected=true;}
    Check(rejected);
    // Ordinary fractional samples, backwards interpolation, key boundaries,
    // inactive/finished curves, negative starts and large animation clocks.
    for (int op : {1,2,3,4,6}) {
      PackedTrack t; t.channel=5; t.value_format=128; t.slope_format=96;
      t.bytes.push_back(uint8_t(0x70|op));
      for (int key=0; key<8; ++key) {
        t.bytes.push_back(uint8_t(key*19));
        if (op==4) t.bytes.push_back(uint8_t(key%2 ? -2 : 3));
        if (key!=7) t.bytes.push_back(uint8_t(key%3+1));
      }
      for (int start : {-1000,-3,0,7}) {
        t.start_frame=int16_t(start);
        for (float first : {-8.f,-.25f,0.f,.75f,1.f,2.25f,5.5f,12.f,1001.f,1048576.f,2097152.f})
          for (float delta : {-4.f,-.75f,0.f,.125f,.731f,1.f,8.f}) CheckPair(t,first,first+delta);
      }
    }
    PackedSampleCursor mismatch;
    float first=0, actual=0;
    SamplePacked(linear,.5f,first,&mismatch);
    auto other=linear; other.bytes.back()=20;
    Check(SamplePackedFromCursor(other,.75f,actual,mismatch) && actual==Read(other,.75f));
    // Optional real retail animation corpus, extracted locally from the ISO.
    if (argc==2) for (const char* name : {"PlFxAJ.dat","PlMsAJ.dat","PlFcAJ.dat"}) {
      Archive archive(std::string(argv[1])+"/"+name);
      const auto animations=LoadAnimations(archive);
      for (const auto& animation:animations) for (const auto& node:animation.nodes) for (const auto& t:node)
        for (float f : {0.f,.25f,1.f,3.f,7.f,animation.frames*.5f,animation.frames-1,animation.frames})
          for (float delta : {-.75f,0.f,.25f,.731f,1.f,2.f}) CheckPair(t,f,f+delta);
      std::cout<<name<<": "<<animations.size()<<" animations checked\n";
    }
    std::cout<<pairs<<" cursor samples bit-exact against a fresh decode\n";
    std::cout<<"Bounded native HSD packed-track decoding passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
