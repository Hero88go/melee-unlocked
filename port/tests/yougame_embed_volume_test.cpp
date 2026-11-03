// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_embed_volume.h"
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main() {
  // What NativeEmbed.tsx sends: toFixed(3) of a gain in [0, 1].
  CHECK(yg_embed_volume("volume 0.000") == 0);
  CHECK(yg_embed_volume("volume 0.500") == 50);
  CHECK(yg_embed_volume("volume 0.704") == 70);
  CHECK(yg_embed_volume("volume 0.705") == 71);   // rounded, not truncated
  CHECK(yg_embed_volume("volume 0.995") == 100);
  CHECK(yg_embed_volume("volume 1.000") == 100);
  // Other plain decimals in range.
  CHECK(yg_embed_volume("volume 1") == 100);
  CHECK(yg_embed_volume("volume 0") == 0);
  CHECK(yg_embed_volume("volume .25") == 25);
  CHECK(yg_embed_volume("volume 1.") == 100);
  CHECK(yg_embed_volume("volume 0.30000000000000004") == 30);
  // Out of range, malformed, or another command: ignored.
  for (const char* bad : {"volume 1.001", "volume 1.0000001", "volume 2", "volume 10", "volume -0.5", "volume .",
                          "volume ", "volume", "volume 0,5", "volume 0.5 ", "volume 0.5x", "volume 1e-1", "volume nan",
                          "volume inf", "volume 0..5", " volume 0.5", "Volume 0.5", "rect 0 0 640 480", ""})
    CHECK(yg_embed_volume(bad) == -1);
  // A comma-decimal locale changes nothing (the page always sends a point).
  if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") || std::setlocale(LC_NUMERIC, "es_ES.UTF-8"))
    CHECK(yg_embed_volume("volume 0.500") == 50);
  std::puts("yougame_embed_volume: ok");
  return 0;
}
