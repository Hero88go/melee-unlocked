// .slpz replays: Slippi replays with their events stored column by column and zstd compressed,
// 8 to 12 times smaller than .slp. Byte for byte the format of the slpz tool
// (https://github.com/AlexanderHarrison/slpz, MIT or Apache-2.0, version 0 of the format).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace slpz {

// True for the first bytes of a .slp ({U\x03raw[$U#l) and of a .slpz (format version 0).
bool is_slp(const uint8_t* data, size_t size);
bool is_slpz(const uint8_t* data, size_t size);

// The .slpz of a finished .slp (level 1 to 19, the tool's default is 3). False when `slp` is not a
// complete replay (a recording still being written has no length yet).
bool compress(const std::vector<uint8_t>& slp, std::vector<uint8_t>& out, int level = 3);
// The .slp a .slpz holds. False when it is damaged or from a newer format.
bool decompress(const std::vector<uint8_t>& slpz, std::vector<uint8_t>& out);

// A replay file's .slp bytes, whichever of the two it is. False when it cannot be read or is neither.
bool read_replay(const std::string& utf8_path, std::vector<uint8_t>& slp);

// Compresses a finished .slp beside itself as .slpz, checks that it decompresses to the same bytes,
// then removes the .slp. Returns the new path, or empty (and leaves the .slp alone) on any failure.
std::string compress_file(const std::string& utf8_slp_path, int level = 3);

}  // namespace slpz
