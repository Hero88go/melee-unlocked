#include "AnimationTrack.h"
#include <cstdlib>
#include <iostream>
#include <limits>

static void Check(bool value) { if (!value) std::abort(); }
static bool Near(float a, float b) { return std::abs(a-b) < 0.00001f; }
int main() {
  using namespace NativeMelee;
  // An authored curved track with identical endpoints still moves between them.
  // Blending endpoint snapshots would incorrectly produce zero everywhere.
  const AnimationTrack curve({{0, 1, 0, 0, 4, -4, Curve::Hermite}});
  Check(Near(curve.Sample(0), 0));
  Check(Near(curve.Sample(0.25), 0.75));
  Check(Near(curve.Sample(0.5), 1));
  Check(Near(curve.Sample(0.75), 0.75));
  Check(Near(curve.Sample(1), 0));
  // Slope units must remain per frame when segments last multiple frames.
  const AnimationTrack long_curve({{0, 2, 0, 0, 2, -2, Curve::Hermite}});
  Check(Near(long_curve.Sample(1), 1));
  const AnimationTrack step({{0, 2, 3, 9, 0, 0, Curve::Constant},
                             {2, 4, 9, 13, 0, 0, Curve::Linear}});
  Check(Near(step.Sample(1.999), 3));
  Check(Near(step.Sample(2), 9));
  Check(Near(step.Sample(3), 11));
  Check(Near(step.Sample(-1), 3));
  Check(Near(step.Sample(100), 13));
  // Out-of-order render requests do not advance or modify the animation track.
  for (int fps : {120, 144, 165, 240, 360}) {
    for (int n = fps; n >= 0; --n) {
      const double frame = static_cast<double>(n)/fps;
      Check(Near(curve.Sample(frame), static_cast<float>(4*frame*(1-frame))));
    }
  }
  bool rejected = false;
  try { curve.Sample(std::numeric_limits<double>::quiet_NaN()); }
  catch (const std::invalid_argument&) { rejected = true; }
  Check(rejected);
  std::cout << "Authored native animation curve sampling passed (not an integrated game renderer).\n";
}
