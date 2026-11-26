// Dolphin's compressed disc images (.rvz and .wia) turned back into the full disc image (.iso).
//
// Both formats are lossless: every byte of the original disc comes back, so the result has the
// same hash as the ISO it was made from and is checked like any other disc. The launcher converts
// once, when a player picks or drops one, and the game, mods and online play keep reading a plain
// ISO. GameCube discs only; a Wii image is refused with a plain message.
//
// Format: Dolphin's docs/WiaAndRvz.md. Compression: none, purge (WIA), bzip2, LZMA, LZMA2, Zstandard.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace disc_rvz {

enum class Format { NotCompressed, Wia, Rvz, NKit, Ciso };

// CISO ("CISO", the compact format some loaders and tools write): a block size, a map of which blocks
// are stored, then the stored blocks in disc order; the blocks left out are all zero.
// From the file's first four bytes ("WIA\x01", "RVZ\x01" or "CISO"), or NKit's tag at 0x200; NotCompressed for
// anything else. An NKit image is a disc with the padding between its files taken out. Only Melee NTSC
// 1.02's is turned back into the disc: its layout and its padding's generator seeds are built in
// (nkit_melee102.inc), and the result must have the retail disc's MD5.
Format detect(const std::filesystem::path& file);

struct Info {
  Format format = Format::NotCompressed;
  bool gamecube = false;      // disc type 1; 2 is Wii
  uint64_t iso_size = 0;      // the full disc image's size in bytes
  std::string game_id;        // six characters from the disc header, "GALE01" for Melee NTSC
  int revision = -1;          // disc header byte 7: 2 for 1.02
  std::string compression;    // "none", "purge", "bzip2", "LZMA", "LZMA2" or "Zstandard"
};

// Reads the headers only. False with a plain sentence in *error when the file is not a usable image.
bool probe(const std::filesystem::path& file, Info* info, std::string* error);

// done and total in bytes of the output disc. Returning false cancels.
using Progress = std::function<bool(uint64_t done, uint64_t total)>;

// Writes the full disc image to `out`. It is written beside it as "<out>.part" first and renamed
// when complete, so a cancelled or failed conversion never leaves a partial .iso behind.
bool convert(const std::filesystem::path& in, const std::filesystem::path& out, const Progress& progress,
             std::string* error);

}  // namespace disc_rvz
