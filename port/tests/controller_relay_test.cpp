#include "controller_relay.h"
#include <cstdio>
using namespace host;
namespace {
controller_relay::Snapshot state;
controller_relay::Snapshot read() { return state; }
int failures=0, output_count=0;
CaptureDevice output_family=CaptureDevice::None;
void output(CaptureDevice family,int,bool) { ++output_count; output_family=family; }
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(0)
}
int main() {
  using namespace controller_relay;
  CHECK(!active());
  CHECK(native_buttons(0x1000,CaptureDevice::DS4Pad)==DS4_CROSS);
  CHECK(native_buttons(0x2000,CaptureDevice::DS4Pad)==DS4_CIRCLE);
  CHECK(native_buttons(0x4000,CaptureDevice::DS4Pad)==DS4_SQUARE);
  CHECK(native_buttons(0x8000,CaptureDevice::DS4Pad)==DS4_TRIANGLE);
  CHECK(native_buttons(0x1000,CaptureDevice::SwitchPro)==SWPRO_B);
  CHECK(native_buttons(0x2000,CaptureDevice::SwitchPro)==SWPRO_A);
  CHECK(native_buttons(0x4000,CaptureDevice::SwitchPro)==SWPRO_Y);
  CHECK(native_buttons(0x8000,CaptureDevice::SwitchPro)==SWPRO_X);
  CHECK(native_buttons(0xffff,CaptureDevice::XInputPad)==0xffff);
  for (auto family : {CaptureDevice::DS4Pad,CaptureDevice::SwitchPro}) {
    CHECK((native_buttons(0xf,family)&0xf)==0xf);
    CHECK((native_buttons(0,family,255,255)&0xc00)==0xc00);
    CHECK((native_buttons(0,family,0,0)&0xc00)==0);
  }
  install(read); install_rumble(output);
  const CaptureDevice families[]={CaptureDevice::XInputPad,CaptureDevice::DS4Pad,CaptureDevice::SwitchPro,CaptureDevice::HidPad};
  for(int i=0;i<4;++i) {
    auto& s=state.devices[i]; s.family=families[i]; s.index=2; s.axes[0]=32767;s.axes[1]=-32768;
    s.left=79;s.right=201;s.buttons=i==3?0x80000001u:native_buttons(0x1000,s.family);
  }
  for(int i=0;i<4;++i) {
    Sample s; CHECK(sample(families[i],2,s));CHECK(!sample(families[i],0,s));
    PadState pads[4]{};uint32_t buttons[4]{};
    CHECK(poll(families[i],pads,buttons)==4);
    CHECK(pads[2].stick_x==127 && pads[2].stick_y==-127);
    CHECK(pads[2].trig_l==79 && pads[2].trig_r==201);
    CHECK(buttons[2]==state.devices[i].buttons);
  }
  // A custom PlayStation Cross -> B profile must produce B, never the Xbox default A.
  auto bindings=default_pad_bindings()[0];
  for(auto& mask:bindings.mask)mask=0;
  bindings.mask[(size_t)BindAction::B]=DS4_CROSS;
  auto pad=raw_pad(state.devices[1]);
  ds4_apply_bindings(bindings,(uint16_t)state.devices[1].buttons,pad);
  CHECK((pad.button & 0x200)!=0);CHECK((pad.button & 0x100)==0);
  CHECK(rumble(DeviceKind::DS4Pad,2,true));CHECK(output_count==1 && output_family==CaptureDevice::DS4Pad);
  CHECK(!rumble(DeviceKind::GCAdapter,0,true));CHECK(!rumble(DeviceKind::Keyboard,0,true));
  state={};Sample s;CHECK(active());CHECK(!sample(CaptureDevice::DS4Pad,2,s));
  install(nullptr);install_rumble(nullptr);CHECK(!active());CHECK(!rumble(DeviceKind::XInputPad,0,true));
  std::printf("controller relay: %d failures\n",failures);return failures?1:0;
}
