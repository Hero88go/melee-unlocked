#include "ui_sources/gd_melee/motion.h"

#include <cmath>
#include <cstdlib>
#include <initializer_list>

static void expect_near(float got, float want) {
  if (std::fabs(got - want) > 0.001f) std::abort();
}

int main() {
  using gx::gd_melee_ui::slide_in;
  using gx::gd_melee_ui::slide_out;
  expect_near(slide_in(720.0f, 0.0f), 720.0f);
  expect_near(slide_in(720.0f, 12.0f), -6.0f);
  expect_near(slide_in(720.0f, 17.0f), 0.0f);
  expect_near(slide_out(0.0f, 0.0f), 0.0f);
  expect_near(slide_out(0.0f, 12.0f), -720.0f);
  // The source sequencer starts interrupted tracks from their live value.
  expect_near(slide_in(-175.0f, 0.0f), -175.0f);
  expect_near(slide_out(83.0f, 0.0f), 83.0f);
  for (float from : {-720.0f,-175.0f,-24.0f,0.0f,83.0f,350.0f}) {
    float previous=from;
    for (int frame=1;frame<=17;++frame) {
      const float value=slide_in(from,(float)frame);
      expect_near(value, std::clamp(value, std::min(from,0.0f), std::max(from,0.0f)));
      if (from<0) { if (value<previous-0.001f) std::abort(); }
      else if (value>previous+0.001f) std::abort();
      previous=value;
    }
    expect_near(slide_in(from,17.0f),0.0f);
  }
  // The source timeline is 60 Hz, but the renderer samples fractional source
  // frames. Ensure 120/144 Hz presentation can produce more than 17 updates.
  for (float hz : {120.0f, 144.0f}) {
    const float step = 60.0f / hz;
    float previous = slide_in(720.0f, 0.0f);
    int changed_samples = 0;
    for (float frame = step; frame < 17.0f; frame += step) {
      const float value = slide_in(720.0f, frame);
      if (std::fabs(value - previous) > 0.0001f) ++changed_samples;
      previous = value;
    }
    if (changed_samples <= 17) std::abort();
  }
}
