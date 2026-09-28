// Runs the production resize/resolve shaders on WARP, without an ISO or NVIDIA runtime.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "dlss5_test_gpu.h"
#include <cmath>

// Independent double-precision reference, in byte units before the final UNORM conversion.
static double reference_sample(const Bytes& pixels,int w,int h,double x,double y,int c,UINT filter) {
  const auto at=[&](int px,int py) { return (double)pixels[(std::clamp(py,0,h-1)*w+std::clamp(px,0,w-1))*4+c]; };
  if(filter==2) return at((int)floor(x+0.5),(int)floor(y+0.5));
  const int ix=(int)floor(x), iy=(int)floor(y); const double fx=x-ix, fy=y-iy;
  if(filter==0) return (at(ix,iy)*(1-fx)+at(ix+1,iy)*fx)*(1-fy)+(at(ix,iy+1)*(1-fx)+at(ix+1,iy+1)*fx)*fy;
  const auto weight=[](double t) {
    t=abs(t);
    return t<1 ? 1-2.5*t*t+1.5*t*t*t : t<2 ? 2-4*t+2.5*t*t-0.5*t*t*t : 0;
  };
  double sum=0;
  for(int py=iy-1;py<=iy+2;++py) for(int px=ix-1;px<=ix+2;++px)
    sum+=at(px,py)*weight(x-px)*weight(y-py);
  return sum;
}
static void reference_check(const Bytes& actual,const Bytes& source,int sw,int sh,int dw,int dh,UINT filter,
                            const Bytes* baseline=nullptr,const Bytes* original=nullptr) {
  for(int y=0;y<dh;++y) for(int x=0;x<dw;++x) for(int c=0;c<4;++c) {
    const size_t index=(y*dw+x)*4+c;
    if(original && c==3) { require(actual[index]==(*original)[index],"reference original alpha"); continue; }
    const double sx=(x+0.5)*sw/dw-0.5, sy=(y+0.5)*sh/dh-0.5;
    double expected=reference_sample(source,sw,sh,sx,sy,c,filter);
    if(baseline) expected+=(*original)[index]-reference_sample(*baseline,sw,sh,sx,sy,c,filter);
    const int byte=(int)std::lround(std::clamp(expected,0.0,255.0));
    if(abs((int)actual[index]-byte)>1) {
      fprintf(stderr,"reference mismatch at %d,%d channel %d filter %u: %u vs %d\n",x,y,c,filter,actual[index],byte);
      require(false,"GPU filter must match CPU reference within one UNORM byte");
    }
  }
}
int main() {
  try {
    GPU gpu;
    Bytes pattern(13*9*4);
    for(size_t i=0;i<pattern.size();++i) pattern[i]=(unsigned char)((i*53+17)%256);
    // Non-integer scale, all filters, same command list for downsample and reconstruction.
    for(UINT down=0;down<3;++down) for(UINT up=0;up<3;++up) {
      auto original=gpu.texture(13,9,&pattern), reduced_input=gpu.texture(7,5), output=gpu.texture(13,9);
      gpu.run(original.Get(),original.Get(),original.Get(),reduced_input.Get(),0,down);
      gpu.barrier(reduced_input.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      gpu.run(reduced_input.Get(),reduced_input.Get(),original.Get(),output.Get(),2,up);
      require(gpu.read(output.Get())==pattern,"identity residual must preserve all original bytes");
    }
    // A negative and positive neural edit must remain signed, for each enlargement filter.
    Bytes base(3*2*4,128), model(3*2*4,128), original(11*7*4,128);
    for(size_t i=0;i<model.size();i+=4) { model[i]=96; model[i+1]=160; model[i+3]=0; }
    for(size_t i=3;i<original.size();i+=4) original[i]=(unsigned char)(i%255);
    for(UINT up=0;up<3;++up) for(UINT mode=1;mode<=2;++mode) {
      auto b=gpu.texture(3,2,&base), m=gpu.texture(3,2,&model), o=gpu.texture(11,7,&original), dst=gpu.texture(11,7);
      gpu.run(m.Get(),b.Get(),o.Get(),dst.Get(),mode,up); const auto result=gpu.read(dst.Get());
      for(size_t i=0;i<result.size();i+=4) {
        require(abs((int)result[i]-96)<=1 && abs((int)result[i+1]-160)<=1 && abs((int)result[i+2]-128)<=1,"signed residual / constant enlargement");
        require(result[i+3]==original[i+3],"resolve must preserve original alpha");
      }
    }
    Bytes checker(8*8*4,255);
    for(int y=0;y<8;++y) for(int x=0;x<8;++x) for(int c=0;c<3;++c) checker[(y*8+x)*4+c]=((x+y)%2)*255;
    auto src=gpu.texture(8,8,&checker), dst=gpu.texture(2,2);
    gpu.run(src.Get(),src.Get(),src.Get(),dst.Get(),0,0); auto averaged=gpu.read(dst.Get());
    for(size_t i=0;i<averaged.size();++i)
      require(i%4==3 ? averaged[i]==255 : abs((int)averaged[i]-128)<=1,"area filter must average checkerboard energy within UNORM rounding");
    Bytes samples[3];
    for(UINT f=0;f<3;++f) {
      auto s=gpu.texture(8,8,&checker), d=gpu.texture(5,3);
      gpu.run(s.Get(),s.Get(),s.Get(),d.Get(),0,f); samples[f]=gpu.read(d.Get());
      if(f>0) reference_check(samples[f],checker,8,8,5,3,f==1?0:2);
    }
    require(samples[0]!=samples[1] && samples[1]!=samples[2],"downsample choices must perform different filtering");
    // Varied signed edits expose kernel differences and clipping, beyond constant/identity cases.
    Bytes low(7*5*4), edited(low.size()), full(13*9*4);
    for(size_t i=0;i<low.size();++i) { low[i]=(unsigned char)((i*37+11)%256); edited[i]=(unsigned char)((i*73+89)%256); }
    for(size_t i=0;i<full.size();++i) full[i]=(unsigned char)((i*29+67)%256);
    Bytes enlarged[3];
    for(UINT f=0;f<3;++f) for(UINT mode=1;mode<=2;++mode) {
      auto b=gpu.texture(7,5,&low), m=gpu.texture(7,5,&edited), o=gpu.texture(13,9,&full), d=gpu.texture(13,9);
      gpu.run(m.Get(),b.Get(),o.Get(),d.Get(),mode,f); auto actual=gpu.read(d.Get());
      reference_check(actual,edited,7,5,13,9,f,mode==2?&low:nullptr,&full);
      if(mode==1) enlarged[f]=actual;
    }
    require(enlarged[0]!=enlarged[1] && enlarged[1]!=enlarged[2],"enlargement choices must perform different filtering");
    gpu.validate(); gpu.scaling.shutdown();
    printf("DLSS 5 scaling: identity, signed edits, alpha, area averaging, filters and descriptors passed on %s\n",gpu.backend);
    return 0;
  } catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
