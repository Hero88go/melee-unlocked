#include "PackedAnimation.h"
#include <cmath>
#include <stdexcept>
#include <iostream>
using namespace NativeMelee;
static void Check(bool value) {if(!value)throw std::runtime_error("Packed animation regression");}
static float Read(const PackedTrack& track,float frame) {float value=0;Check(SamplePacked(track,frame,value));return value;}
int main() {
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
    std::cout<<"Bounded native HSD packed-track decoding passed\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
