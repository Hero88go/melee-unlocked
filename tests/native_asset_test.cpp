#include "Assets.h"
#include "Geometry.h"
#include <iostream>
#include <algorithm>
int main(int argc,char** argv) {
  try {
    if (argc!=2) throw std::runtime_error("Expected asset directory");
    NativeMelee::Archive model(std::string(argv[1])+"/PlFcNr.dat");
    NativeMelee::Rig rig(model);
    const auto mesh=NativeMelee::LoadMesh(model,rig);
    std::cout << mesh.polygons << " polygon objects, " << mesh.triangles.size()/3 << " triangles\n";
    NativeMelee::Archive bundle(std::string(argv[1])+"/PlFcAJ.dat");
    auto animations=NativeMelee::LoadAnimations(bundle);
    size_t sampled=0, tracks=0;
    for (const auto& animation:animations) {
      for (const auto& node:animation.nodes) for (const auto& track:node) {
        ++tracks;
        for (float frame : {0.f,0.25f,0.5f,1.f,animation.frames*0.5f,animation.frames}) {
          float value=0;
          if (NativeMelee::SamplePacked(track,frame,value)) {
            if (!std::isfinite(value)) throw std::runtime_error("Nonfinite sampled pose");
            ++sampled;
          }
        }
      }
    }
    std::cout << rig.joints.size() << " joints, " << animations.size() << " animations, "
              << tracks << " packed tracks, " << sampled << " finite native samples\n";
    std::cout << "First animation nodes=" << animations.front().nodes.size() << " duration=" << animations.front().frames << "\n";
    if (animations.size()!=222 || rig.joints.empty() || sampled<1000) return 1;
  } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
