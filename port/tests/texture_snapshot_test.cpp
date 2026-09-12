#include "texture_snapshot.h"
#include <cstdio>
#include <cstdlib>
static void check(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
  gx::TextureSnapshotCache cache;
  std::vector<uint8_t> image(96, 0), palette(512, 0);
  palette[0] = 0xf8; // RGB565 red
  auto first = cache.capture(image.data(), image.size(), palette.data(), palette.size());
  check(cache.capture(image.data(), image.size(), palette.data(), palette.size()) == first, "deduplication failed");
  palette[0] = 0x07; palette[1] = 0xe0; // green, same palette address
  auto second = cache.capture(image.data(), image.size(), palette.data(), palette.size());
  image[80] = 17; // change a later mip without changing level zero
  auto third = cache.capture(image.data(), image.size(), palette.data(), palette.size());
  check(first != second && second != third, "different palette or mip was reused");
  for (unsigned frame=0; frame<8; ++frame) {
    cache.end_frame();
    check(cache.capture(image.data(), image.size(), palette.data(), palette.size()) == third, "stable source lost ownership");
  }
  auto relocated = image;
  check(cache.capture(relocated.data(), relocated.size(), palette.data(), palette.size()) == third,
        "actively reused texture aged out of content deduplication");
  std::vector<uint8_t> red, green;
  gx::decode_texture(first->image.data(), 8, 4, 9, first->palette.data(), 1, red);
  gx::decode_texture(second->image.data(), 8, 4, 9, second->palette.data(), 1, green);
  check(red[0] == 255 && red[1] == 0 && green[0] == 0 && green[1] == 255, "deferred palette decode corrupted");
  cache.clear(); image.clear(); palette.clear();
  check(first->image[80] == 0 && third->image[80] == 17, "source mutation or cache clear changed retained draw");
  check(gx::texture_mip_count(8, 4, 17) == 4, "mip count exceeded 1x1");
  check(gx::texture_chain_bytes(8, 4, 9, 4) == 128, "mip blocks have incorrect size");
  std::puts("deferred palette, mip, ownership and deduplication checks passed");
}
