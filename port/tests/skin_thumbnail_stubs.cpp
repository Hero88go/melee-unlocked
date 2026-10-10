// What skin_thumbnail.cpp needs from the rest of the program, for its own test.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdint>
#include <string>
#include <vector>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
namespace host {
void log(const char*, ...) {}
bool disc_find_file(const std::string&, uint32_t*, uint32_t*) { return false; }
bool disc_read(uint32_t, void*, uint32_t) { return false; }
namespace cosmetics {
bool costume_file(const std::string&, std::vector<uint8_t>*, std::string*) { return false; }
std::string costume_thumbnail_path(const std::string&) { return {}; }
}  // namespace cosmetics
}  // namespace host
