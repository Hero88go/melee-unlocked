// Launcher-only accent palette. Kept separate from the game's video settings.
#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace launcher::theme {
inline COLORREF accent = RGB(142, 84, 239);
inline COLORREF mix(COLORREF a, COLORREF b, double amount) {
  amount = std::clamp(amount, 0.0, 1.0);
  auto channel = [amount](int x, int y) { return int(std::lround(x + (y - x) * amount)); };
  return RGB(channel(GetRValue(a), GetRValue(b)), channel(GetGValue(a), GetGValue(b)),
             channel(GetBValue(a), GetBValue(b)));
}
inline COLORREF top() { return mix(accent, RGB(255,255,255), .19); }
inline COLORREF bottom() { return mix(accent, RGB(0,0,0), .16); }
inline COLORREF pressed() { return mix(accent, RGB(0,0,0), .34); }
inline COLORREF glow() { return mix(accent, RGB(255,255,255), .36); }
inline COLORREF on_accent() {
  const double luminance = .2126 * GetRValue(accent) + .7152 * GetGValue(accent) + .0722 * GetBValue(accent);
  return luminance > 169 ? RGB(16,23,38) : RGB(255,255,255);
}
inline COLORREF hsv(double hue, double saturation, double value) {
  hue = std::fmod(hue + 360.0, 360.0); saturation = std::clamp(saturation, 0.0, 1.0);
  value = std::clamp(value, 0.0, 1.0);
  double c = value * saturation, x = c * (1 - std::abs(std::fmod(hue / 60.0, 2.0) - 1)), m = value - c;
  double r=0,g=0,b=0;
  if(hue<60) {r=c;g=x;} else if(hue<120) {r=x;g=c;} else if(hue<180) {g=c;b=x;}
  else if(hue<240) {g=x;b=c;} else if(hue<300) {r=x;b=c;} else {r=c;b=x;}
  return RGB(int((r+m)*255+.5),int((g+m)*255+.5),int((b+m)*255+.5));
}
inline void to_hsv(COLORREF rgb, double& hue, double& saturation, double& value) {
  double r=GetRValue(rgb)/255.0,g=GetGValue(rgb)/255.0,b=GetBValue(rgb)/255.0;
  double hi=std::max({r,g,b}),lo=std::min({r,g,b}),delta=hi-lo;
  value=hi; saturation=hi==0?0:delta/hi; hue=0;
  if(delta>0) {
    if(hi==r) hue=60*std::fmod((g-b)/delta+6,6.0);
    else if(hi==g) hue=60*((b-r)/delta+2);
    else hue=60*((r-g)/delta+4);
  }
}
}
