// Scripted controller actions must remain aligned as native boot timing changes.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "input_script.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) {
  try {
    if (argc!=3) throw std::runtime_error("usage: input_script_test NATIVE_VS_SCRIPT SCRATCH_DIRECTORY");
    auto expect=[](bool condition,const char* message) { if (!condition) throw std::runtime_error(message); };
    linux_host::InputScript script;
    std::string error;
    expect(script.load(argv[1],&error),"load production native_vs script");
    MuPadStatus pads[4]{};
    MuStatePod state{};
    expect(script.apply(359,state,pads) && !pads[0].button && pads[0].err==0 && pads[1].err==0 && pads[2].err==-1,"script ports connected before first action");
    script.apply(360,state,pads); expect(pads[0].button==0x100,"absolute A press");
    script.apply(368,state,pads); expect(!pads[0].button,"explicit button release");
    script.apply(1500,state,pads); expect(pads[0].stick_x==61 && pads[0].stick_y==127 && pads[1].stick_y==127,"simultaneous separate port actions");
    script.mark_match_start(2000);
    script.apply(2400,state,pads); expect(pads[0].stick_x==80,"absolute inputs continue after match begins");
    const auto root=std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(root);
    const auto file=root/"input-script-phases.txt";
    {
      std::ofstream out(file);
      out<<"0 L+A # digital trigger must bottom out\n5\n@scene 1:0\n0 sy=-127\n10 A\n11\n@match\n0 p=2 X\n2 p=2 r=128\n@loop 10\n";
    }
    expect(script.load(file.string(),&error),"load phase-aware controller script");
    script.apply(0,state,pads); expect(pads[0].button==0x140 && pads[0].trigger_l==255,"button chords and analog trigger");
    state.scene_major=1; state.scene=1;
    script.apply(100,state,pads); expect(pads[0].stick_y==0,"scene minor wait");
    state.scene=0;
    script.apply(200,state,pads); expect(pads[0].stick_y==-127,"scene-relative activation");
    script.apply(210,state,pads); expect(pads[0].button==0x100 && !pads[1].button,"menu action before match");
    state.match_frame=1;
    script.apply(240,state,pads); expect(pads[1].button==0x400,"match-relative action follows simulation frame");
    state.match_frame=2;
    script.apply(241,state,pads); expect(!pads[1].button && pads[1].trigger_r==128,"light shield analog script");
    state.match_frame=11;
    script.apply(250,state,pads); expect(pads[1].button==0x400,"match action looping");
    { std::ofstream out(file); out<<"0 A\n@release 3\n"; }
    expect(script.load(file.string(),&error),"load script with release");
    state.match_frame=2;
    expect(script.apply(20,state,pads) && script.apply(22,state,pads) && !script.apply(23,state,pads),"script releases physical controls on schedule");
    { std::ofstream out(file); out<<"0 p=5 A\n"; }
    expect(!script.load(file.string(),&error) && !script.active() && error.find(":1:")!=std::string::npos,"invalid port refuses script with line diagnostic");
    { std::ofstream out(file); out<<"0 NOT_A_BUTTON\n"; }
    expect(!script.load(file.string(),&error),"unknown tokens never silently dropped");
    std::filesystem::remove(file);
    std::puts("native Linux input scripts: PASS");
    return 0;
  } catch(const std::exception& error) { std::fprintf(stderr,"native Linux input scripts: FAIL: %s\n",error.what()); return 1; }
}
