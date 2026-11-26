// Dolphin's .rvz and .wia disc images back to the full disc image. See disc_rvz.h.
//
// Layout (all big-endian), from Dolphin's docs/WiaAndRvz.md:
//   file head (0x48):  magic, versions, size of the disc struct, SHA-1, ISO size, file size, SHA-1
//   disc struct (0xDC): disc type, compression, level, chunk size, the disc's first 0x80 bytes,
//                       partition table (Wii only), raw data table, group table, compressor data
//   raw data entries:  regions of the disc stored as groups of chunk_size bytes (GameCube: one region)
//   groups:            where each chunk's data is in the file and how big it is
// RVZ adds a packed form inside a group: literal runs plus "junk" runs, the padding Nintendo's
// mastering tools filled the disc with, regenerated from a 68-byte seed with a lagged Fibonacci
// generator instead of being stored.
#include "disc_rvz.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <system_error>
#include <vector>

#if defined(_WIN32) && !defined(MELEE_LINUX)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#ifndef BZ_EXPORT
#define BZ_EXPORT   // bzlib.h: link the library statically instead of loading a DLL
#endif
extern "C" {
#include "bzip2/bzlib.h"
#include "lzma/Lzma2Dec.h"
#include "lzma/LzmaDec.h"
}
#include "zstd/zstd.h"

namespace disc_rvz {
namespace {

namespace fs = std::filesystem;

constexpr size_t kHeadSize = 0x48;
constexpr size_t kDiscSize = 0xDC;
constexpr size_t kDiscMinSize = 0xD4;          // up to and including the group table fields
constexpr size_t kRawEntrySize = 0x18;
constexpr uint64_t kBlock = 0x8000;            // a Wii disc block; RVZ junk and raw regions align to it
constexpr uint32_t kMaxChunk = 128u << 20;     // far above anything Dolphin writes (2 MiB for RVZ)
constexpr uint64_t kMaxIso = 16ull << 30;      // a dual-layer Wii disc is 8.5 GB

enum Method : uint32_t { kNone = 0, kPurge = 1, kBzip2 = 2, kLzma = 3, kLzma2 = 4, kZstd = 5 };

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint64_t be64(const uint8_t* p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

const char* method_name(uint32_t m) {
  switch (m) {
    case kNone: return "none";
    case kPurge: return "purge";
    case kBzip2: return "bzip2";
    case kLzma: return "LZMA";
    case kLzma2: return "LZMA2";
    case kZstd: return "Zstandard";
    default: return "unknown";
  }
}

struct File {
  std::FILE* f = nullptr;
  File() = default;
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  ~File() { close(); }
  bool open(const fs::path& p, bool write) {
#if defined(_WIN32) && !defined(MELEE_LINUX)
    f = _wfopen(p.c_str(), write ? L"wb" : L"rb");
#else
    f = std::fopen(p.c_str(), write ? "wb" : "rb");
#endif
    return f != nullptr;
  }
  bool seek(uint64_t offset) {
#if defined(_WIN32) && !defined(MELEE_LINUX)
    return _fseeki64(f, (long long)offset, SEEK_SET) == 0;
#else
    return fseeko(f, (off_t)offset, SEEK_SET) == 0;
#endif
  }
  bool read(void* data, size_t n) { return n == 0 || std::fread(data, 1, n, f) == n; }
  bool write(const void* data, size_t n) { return n == 0 || std::fwrite(data, 1, n, f) == n; }
  bool close() {
    if (!f) return true;
    const bool ok = std::fclose(f) == 0;
    f = nullptr;
    return ok;
  }
};

struct Image {
  Format format = Format::NotCompressed;
  uint64_t iso_size = 0, file_size = 0;
  uint32_t disc_type = 0, method = 0, chunk_size = 0;
  uint8_t disc_head[0x80] = {};
  uint32_t partitions = 0;
  uint32_t raw_count = 0, raw_bytes = 0;
  uint64_t raw_offset = 0;
  uint32_t group_count = 0, group_bytes = 0;
  uint64_t group_offset = 0;
  uint8_t compressor[7] = {};
  uint8_t compressor_size = 0;
};

bool fail(std::string* error, const std::string& text) {
  if (error) *error = text;
  return false;
}

bool read_image(File& in, const fs::path& path, Image* im, std::string* error) {
  std::error_code ec;
  im->file_size = fs::file_size(path, ec);
  if (ec) return fail(error, "The disc image could not be opened.");
  uint8_t head[kHeadSize];
  if (!in.seek(0) || !in.read(head, sizeof head)) return fail(error, "This file is too short to be a disc image.");
  if (std::memcmp(head, "RVZ\x01", 4) == 0) im->format = Format::Rvz;
  else if (std::memcmp(head, "WIA\x01", 4) == 0) im->format = Format::Wia;
  else return fail(error, "This is not an RVZ or WIA disc image.");
  const uint32_t disc_struct = be32(head + 0x0C);
  im->iso_size = be64(head + 0x24);
  if (disc_struct < kDiscMinSize || disc_struct > 0x10000) return fail(error, "This disc image's header is damaged.");
  uint8_t disc[kDiscSize] = {};
  if (!in.read(disc, std::min<size_t>(disc_struct, kDiscSize))) return fail(error, "This disc image's header is damaged.");
  im->disc_type = be32(disc + 0x00);
  im->method = be32(disc + 0x04);
  im->chunk_size = be32(disc + 0x0C);
  std::memcpy(im->disc_head, disc + 0x10, sizeof im->disc_head);
  im->partitions = be32(disc + 0x90);
  im->raw_count = be32(disc + 0xB4);
  im->raw_offset = be64(disc + 0xB8);
  im->raw_bytes = be32(disc + 0xC0);
  im->group_count = be32(disc + 0xC4);
  im->group_offset = be64(disc + 0xC8);
  im->group_bytes = be32(disc + 0xD0);
  im->compressor_size = std::min<uint8_t>(disc[0xD4], 7);
  std::memcpy(im->compressor, disc + 0xD5, 7);

  if (im->disc_type == 2) return fail(error, "This is a Wii disc image. Melee Unlocked needs the GameCube disc of Melee.");
  if (im->disc_type != 1) return fail(error, "This disc image's header is damaged.");
  if (im->method > kZstd || (im->format == Format::Rvz && im->method == kPurge))
    return fail(error, "This disc image uses a compression method Melee Unlocked does not know.");
  if (im->chunk_size == 0 || im->chunk_size % kBlock != 0 || im->chunk_size > kMaxChunk)
    return fail(error, "This disc image's header is damaged.");
  if (im->iso_size == 0 || im->iso_size > kMaxIso) return fail(error, "This disc image's header is damaged.");
  if (im->partitions != 0) return fail(error, "This disc image has Wii partitions. Melee Unlocked needs the GameCube disc of Melee.");
  const uint64_t max_groups = im->iso_size / im->chunk_size + im->raw_count + 1;
  if (im->raw_count == 0 || im->raw_count > 4096 || im->group_count > max_groups)
    return fail(error, "This disc image's header is damaged.");
  if (im->raw_offset + im->raw_bytes > im->file_size || im->group_offset + im->group_bytes > im->file_size)
    return fail(error, "This disc image is incomplete. Copy or download it again.");
  if ((im->method == kLzma && im->compressor_size < 5) || (im->method == kLzma2 && im->compressor_size < 1))
    return fail(error, "This disc image's header is damaged.");
  return true;
}

// LZMA SDK allocator.
void* lzma_alloc(ISzAllocPtr, size_t size) { return std::malloc(size); }
void lzma_free(ISzAllocPtr, void* p) { std::free(p); }
const ISzAlloc kLzmaAlloc = {lzma_alloc, lzma_free};

struct Decoder {
  const Image& im;
  ZSTD_DCtx* zstd = nullptr;
  explicit Decoder(const Image& image) : im(image) {}
  ~Decoder() { if (zstd) ZSTD_freeDCtx(zstd); }

  // Exactly out_size bytes from in, with the image's compression.
  bool decompress(uint32_t method, const uint8_t* in, size_t in_size, uint8_t* out, size_t out_size) {
    switch (method) {
      case kNone:
        if (in_size < out_size) return false;
        std::memcpy(out, in, out_size);
        return true;
      case kPurge: {
        // Runs of {offset, size, bytes} over zeros, then a SHA-1 of the stored data.
        if (in_size < 20) return false;
        std::memset(out, 0, out_size);
        const size_t end = in_size - 20;
        size_t pos = 0, filled = 0;
        while (pos < end) {
          if (end - pos < 8) return false;
          const uint64_t at = be32(in + pos), size = be32(in + pos + 4);
          pos += 8;
          if (at < filled || at + size > out_size || size > end - pos) return false;
          std::memcpy(out + at, in + pos, (size_t)size);
          pos += (size_t)size;
          filled = (size_t)(at + size);
        }
        return true;
      }
      case kBzip2: {
        unsigned int got = (unsigned int)out_size;
        if (in_size > 0xFFFFFFFFu || out_size > 0xFFFFFFFFu) return false;
        const int r = BZ2_bzBuffToBuffDecompress((char*)out, &got, (char*)in, (unsigned int)in_size, 0, 0);
        return (r == BZ_OK || r == BZ_OUTBUFF_FULL) && got == out_size;
      }
      case kLzma: {
        SizeT got = out_size, used = in_size;
        ELzmaStatus status;
        const SRes r = LzmaDecode(out, &got, in, &used, im.compressor, LZMA_PROPS_SIZE, LZMA_FINISH_ANY, &status, &kLzmaAlloc);
        return r == SZ_OK && got == out_size;
      }
      case kLzma2: {
        SizeT got = out_size, used = in_size;
        ELzmaStatus status;
        const SRes r = Lzma2Decode(out, &got, in, &used, im.compressor[0], LZMA_FINISH_ANY, &status, &kLzmaAlloc);
        return r == SZ_OK && got == out_size;
      }
      case kZstd: {
        if (!zstd && !(zstd = ZSTD_createDCtx())) return false;
        const size_t r = ZSTD_decompressDCtx(zstd, out, out_size, in, in_size);
        return !ZSTD_isError(r) && r == out_size;
      }
      default: return false;
    }
  }
};

// Dolphin's LaggedFibonacciGenerator (Source/Core/DiscIO/LaggedFibonacciGenerator.cpp): the junk
// data on Nintendo's discs, rebuilt from 17 seed words.
class Junk {
 public:
  void seed(const uint8_t* s) {
    position_ = 0;
    for (size_t i = 0; i < kSeed; ++i) buffer_[i] = be32(s + i * 4);
    for (size_t i = kSeed; i < kK; ++i) buffer_[i] = (buffer_[i - 17] << 23) ^ (buffer_[i - 16] >> 9) ^ buffer_[i - 1];
    for (uint32_t& x : buffer_) x = (x & 0xFF00FFFFu) | ((x >> 2) & 0x00FF0000u);
    for (int i = 0; i < 4; ++i) forward();
  }
  void skip(size_t count) {
    position_ += count;
    while (position_ >= kK * 4) { forward(); position_ -= kK * 4; }
  }
  void bytes(uint8_t* out, size_t count) {
    while (count > 0) {
      const size_t length = std::min(count, kK * 4 - position_);
      for (size_t i = 0; i < length; ++i) {
        const size_t at = position_ + i;
        out[i] = (uint8_t)(buffer_[at / 4] >> (24 - 8 * (at % 4)));
      }
      position_ += length; count -= length; out += length;
      if (position_ == kK * 4) { forward(); position_ = 0; }
    }
  }
  static constexpr size_t kSeed = 17;

 private:
  static constexpr size_t kK = 521, kJ = 32;
  void forward() {
    for (size_t i = 0; i < kJ; ++i) buffer_[i] ^= buffer_[i + kK - kJ];
    for (size_t i = kJ; i < kK; ++i) buffer_[i] ^= buffer_[i - kJ];
  }
  uint32_t buffer_[kK] = {};
  size_t position_ = 0;
};

// RVZ's packed form: {u32 size; bytes} literal runs and {u32 size | 0x80000000; 68-byte seed} junk runs.
// `data_offset` is where out[0] sits in its region; junk restarts its pattern every 0x8000 bytes.
bool unpack(const uint8_t* in, size_t in_size, uint8_t* out, size_t out_size, uint64_t data_offset, Junk& junk) {
  size_t pos = 0, done = 0;
  while (done < out_size) {
    if (in_size - pos < 4) return false;
    uint32_t size = be32(in + pos);
    pos += 4;
    const bool is_junk = (size & 0x80000000u) != 0;
    size &= 0x7FFFFFFFu;
    if (size > out_size - done) return false;
    if (is_junk) {
      if (in_size - pos < Junk::kSeed * 4) return false;
      junk.seed(in + pos);
      pos += Junk::kSeed * 4;
      junk.skip((size_t)((data_offset + done) % kBlock));
      junk.bytes(out + done, size);
    } else {
      if (in_size - pos < size) return false;
      std::memcpy(out + done, in + pos, size);
      pos += size;
    }
    done += size;
  }
  return true;
}

bool read_table(File& in, Decoder& dec, uint64_t offset, uint32_t stored, std::vector<uint8_t>* out) {
  std::vector<uint8_t> raw(stored);
  if (!in.seek(offset) || !in.read(raw.data(), raw.size())) return false;
  return dec.decompress(dec.im.method, raw.data(), raw.size(), out->data(), out->size());
}


// ---- NKit (Melee NTSC 1.02 only). The image keeps the disc's files, header, apploader and DOL, with its
// own file offsets and none of the padding. The recipe (tools/nkit_recipe.py, nkit_melee102.inc) says where
// each part goes on the retail disc and how to make its padding again (zero runs, generator seeds, a few
// short runs as bytes); the file table is the image's with the retail offsets put back. Every byte is then
// checked: the result must have the retail disc's MD5, or nothing is kept.
#include "nkit_melee102.inc"

// RFC 1321.
class Md5 {
 public:
  void update(const uint8_t* p, size_t n) {
    total_ += n;
    while (n) {
      const size_t take = std::min(n, (size_t)64 - fill_);
      std::memcpy(buf_ + fill_, p, take);
      fill_ += take; p += take; n -= take;
      if (fill_ == 64) { block(buf_); fill_ = 0; }
    }
  }
  void digest(uint8_t out[16]) {
    const uint64_t bits = total_ * 8;
    const uint8_t pad = 0x80, zero = 0;
    update(&pad, 1);
    while (fill_ != 56) update(&zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; ++i) len[i] = (uint8_t)(bits >> (8 * i));
    update(len, 8);
    for (int i = 0; i < 4; ++i)
      for (int k = 0; k < 4; ++k) out[i * 4 + k] = (uint8_t)(h_[i] >> (8 * k));
  }

 private:
  static uint32_t rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
  void block(const uint8_t* p) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) m[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    for (int i = 0; i < 64; ++i) {
      uint32_t f;
      int g;
      if (i < 16) { f = (b & c) | (~b & d); g = i; }
      else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
      else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
      else { f = c ^ (b | ~d); g = (7 * i) % 16; }
      const uint32_t t = d;
      d = c; c = b;
      b = b + rol(a + f + K[i] + m[g], R[i]);
      a = t;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
  }
  uint32_t h_[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  uint8_t buf_[64] = {};
  size_t fill_ = 0;
  uint64_t total_ = 0;
};

// The recipe's fixed header: "MUNK", version, the disc's size and MD5, the header fields to put back,
// header bytes 0x200..0x21F, and each file table entry's retail offset. Then its segments.
struct Recipe {
  uint64_t size = 0;
  const uint8_t* md5 = nullptr;
  uint32_t dol = 0, fst = 0, fst_size = 0, fst_max = 0;
  const uint8_t* boot200 = nullptr;
  uint32_t entries = 0;
  const uint8_t* offsets = nullptr;
  uint32_t segments = 0;
  const uint8_t* segs = nullptr;
};
bool read_recipe(Recipe* r) {
  const uint8_t* p = kMelee102Recipe;
  const size_t n = sizeof kMelee102Recipe;
  if (n < 88 || std::memcmp(p, "MUNK", 4) != 0 || be32(p + 4) != 1) return false;
  r->size = be64(p + 8);
  r->md5 = p + 16;
  r->dol = be32(p + 32); r->fst = be32(p + 36); r->fst_size = be32(p + 40); r->fst_max = be32(p + 44);
  r->boot200 = p + 48;
  r->entries = be32(p + 80);
  if (84 + (uint64_t)r->entries * 4 + 4 > n) return false;
  r->offsets = p + 84;
  r->segments = be32(p + 84 + (size_t)r->entries * 4);
  r->segs = p + 88 + (size_t)r->entries * 4;
  return true;
}

bool nkit_tag(File& in) {
  uint8_t tag[4] = {};
  const bool tagged = in.seek(0x200) && in.read(tag, 4) && std::memcmp(tag, "NKIT", 4) == 0;
  in.seek(0);   // the other readers start at the beginning
  return tagged;
}

bool nkit_probe(File& in, Info* info, std::string* error) {
  uint8_t head[8] = {};
  if (!in.seek(0) || !in.read(head, 8)) return fail(error, "The disc image could not be read.");
  Recipe r;
  if (!read_recipe(&r)) return fail(error, "This build cannot restore NKit images.");
  info->format = Format::NKit;
  info->gamecube = true;
  info->iso_size = r.size;
  info->game_id.clear();
  for (int i = 0; i < 6; ++i) info->game_id += (head[i] >= 32 && head[i] < 127) ? (char)head[i] : '?';
  info->revision = head[7];
  info->compression = "NKit";
  if (info->game_id != "GALE01" || info->revision != 2)
    return fail(error, "This NKit image is not Melee NTSC 1.02 (" + info->game_id + "). Only Melee 1.02's can be restored "
                       "here: restore it with NKit's \"Recover to ISO\" first.");
  return true;
}

bool nkit_convert(File& in, const fs::path& out_path, const Progress& progress, std::string* error) {
  Info info;
  if (!nkit_probe(in, &info, error)) return false;
  Recipe r;
  read_recipe(&r);
  const char* differs = "This NKit image differs from Melee 1.02's (it was made from a changed disc, or by another "
                        "NKit version). Restore it with NKit's \"Recover to ISO\" and pick the restored .iso.";
  uint8_t h[0x440];
  if (!in.seek(0) || !in.read(h, sizeof h)) return fail(error, "The disc image could not be read.");
  const uint32_t k_dol = be32(h + 0x420), k_fst = be32(h + 0x424), k_fst_size = be32(h + 0x428);
  if (k_fst_size != r.fst_size || k_fst_size < 12) return fail(error, differs);
  std::vector<uint8_t> fst(k_fst_size);
  if (!in.seek(k_fst) || !in.read(fst.data(), fst.size())) return fail(error, "The disc image could not be read.");
  if (be32(fst.data() + 8) != r.entries || (uint64_t)r.entries * 12 > fst.size()) return fail(error, differs);
  // The image's own file table, read before its offsets are put back: where each file is in the image.
  const std::vector<uint8_t> image_fst = fst;
  for (uint32_t i = 0; i < r.entries; ++i) {
    const uint32_t o = be32(r.offsets + (size_t)i * 4);
    if (!o) continue;
    uint8_t* e = fst.data() + (size_t)i * 12 + 4;
    e[0] = (uint8_t)(o >> 24); e[1] = (uint8_t)(o >> 16); e[2] = (uint8_t)(o >> 8); e[3] = (uint8_t)o;
  }

  const fs::path part = fs::path(out_path).concat(".part");
  bool ok = false;
  struct Remove { const fs::path& p; bool* ok; ~Remove() { if (!*ok) { std::error_code e; fs::remove(p, e); } } } cleanup{part, &ok};
  File out;
  if (!out.open(part, true)) return fail(error, "Could not write the disc image to " + out_path.parent_path().u8string() + ".");
  Md5 md5;
  uint64_t written = 0;
  const char* full = "Could not write the disc image. Is the drive full?";
  auto emit = [&](const uint8_t* p, size_t n) {
    md5.update(p, n);
    written += n;
    return out.write(p, n);
  };
  std::vector<uint8_t> buf;
  auto copy = [&](uint64_t from, uint64_t n) {
    buf.resize(1u << 20);
    if (!in.seek(from)) return false;
    while (n) {
      const size_t take = (size_t)std::min<uint64_t>(n, buf.size());
      if (!in.read(buf.data(), take) || !emit(buf.data(), take)) return false;
      n -= take;
    }
    return true;
  };
  Junk junk;
  const uint8_t* p = r.segs;
  const uint8_t* end = kMelee102Recipe + sizeof kMelee102Recipe;
  int shown_mb = -1;
  for (uint32_t s = 0; s < r.segments; ++s) {
    if (p >= end) return fail(error, differs);
    const uint8_t t = *p;
    if (t <= 5) {   // a part of the disc taken from the image
      const uint32_t length = be32(p + 1), index = be32(p + 5);
      p += 9;
      bool good = true;
      if (t == 0) {          // the header, its own fields as on the retail disc
        uint8_t b[0x440];
        if (length != sizeof b) return fail(error, differs);
        std::memcpy(b, h, sizeof b);
        std::memcpy(b + 0x200, r.boot200, 0x20);
        const uint32_t fields[4] = {r.dol, r.fst, r.fst_size, r.fst_max};
        for (int i = 0; i < 4; ++i)
          for (int k = 0; k < 4; ++k) b[0x420 + i * 4 + k] = (uint8_t)(fields[i] >> (24 - 8 * k));
        good = emit(b, sizeof b);
      } else if (t == 1) good = copy(0x440, length);
      else if (t == 2) good = copy(0x2440, length);
      else if (t == 3) good = copy(k_dol, length);
      else if (t == 4) {
        if (length != fst.size()) return fail(error, differs);
        good = emit(fst.data(), fst.size());
      } else {
        if (index >= r.entries) return fail(error, differs);
        const uint8_t* e = image_fst.data() + (size_t)index * 12;
        if (e[0] != 0 || be32(e + 8) != length) return fail(error, differs);
        good = copy(be32(e + 4), length);
      }
      if (!good) return fail(error, full);
    } else if (t == 6) {   // zeros
      const uint32_t length = be32(p + 1);
      p += 5;
      buf.assign(std::min<size_t>(length, 1u << 20), 0);
      for (uint32_t left = length; left;) {
        const uint32_t take = (uint32_t)std::min<size_t>(left, buf.size());
        if (!emit(buf.data(), take)) return fail(error, full);
        left -= take;
      }
    } else if (t == 7) {   // padding from the generator: its length, where in its 0x8000 block it starts, the seed
      const uint32_t length = be32(p + 1), start = be32(p + 5);
      junk.seed(p + 9);
      p += 9 + Junk::kSeed * 4;
      junk.skip(start);
      buf.resize(length);
      junk.bytes(buf.data(), length);
      if (!emit(buf.data(), length)) return fail(error, full);
    } else if (t == 8) {   // bytes as they are
      const uint32_t length = be32(p + 1);
      if (p + 5 + length > end) return fail(error, differs);
      if (!emit(p + 5, length)) return fail(error, full);
      p += 5 + length;
    } else {
      return fail(error, differs);
    }
    const int mb = (int)(written >> 24);
    if (progress && mb != shown_mb) {
      shown_mb = mb;
      if (!progress(written, r.size)) return fail(error, "Cancelled.");
    }
  }
  uint8_t digest[16];
  md5.digest(digest);
  if (!out.close()) return fail(error, full);
  if (written != r.size || std::memcmp(digest, r.md5, 16) != 0) return fail(error, differs);
  if (progress) progress(r.size, r.size);
  std::error_code ec;
  fs::rename(part, out_path, ec);
  if (ec) return fail(error, "Could not write the disc image to " + out_path.parent_path().u8string() + ".");
  ok = true;
  return true;
}

// ---- CISO: "CISO", the block size (little-endian), then one byte per block (1 stored, 0 all zero) up to
// 0x8000; the stored blocks follow in disc order. A GameCube disc is 1459978240 bytes.
constexpr uint32_t kCisoHeader = 0x8000, kCisoMap = kCisoHeader - 8;
constexpr uint64_t kGameCubeDisc = 1459978240ull;
bool ciso_header(File& in, uint32_t* block, std::vector<uint8_t>* map) {
  uint8_t head[8];
  if (!in.seek(0) || !in.read(head, 8) || std::memcmp(head, "CISO", 4) != 0) return false;
  *block = (uint32_t)head[4] | (uint32_t)head[5] << 8 | (uint32_t)head[6] << 16 | (uint32_t)head[7] << 24;
  map->assign(kCisoMap, 0);
  return *block >= 0x8000 && *block <= (64u << 20) && (*block & (*block - 1)) == 0 && in.read(map->data(), kCisoMap);
}
uint64_t ciso_size(uint32_t, const std::vector<uint8_t>&) {
  return kGameCubeDisc;   // every GameCube disc; the last stored block can run past its end, and trailing zero blocks are left out
}
bool ciso_probe(File& in, Info* info, std::string* error) {
  uint32_t block = 0;
  std::vector<uint8_t> map;
  if (!ciso_header(in, &block, &map)) return fail(error, "This CISO image's header is damaged. Copy or download it again.");
  uint8_t disc[8] = {};
  if (!map[0] || !in.seek(kCisoHeader) || !in.read(disc, 8)) return fail(error, "This CISO image has no disc header.");
  info->format = Format::Ciso;
  info->gamecube = true;
  info->iso_size = ciso_size(block, map);
  info->game_id.clear();
  for (int i = 0; i < 6; ++i) info->game_id += (disc[i] >= 32 && disc[i] < 127) ? (char)disc[i] : '?';
  info->revision = disc[7];
  info->compression = "none";
  return true;
}
bool ciso_convert(File& in, const fs::path& out_path, const Progress& progress, std::string* error) {
  uint32_t block = 0;
  std::vector<uint8_t> map;
  if (!ciso_header(in, &block, &map)) return fail(error, "This CISO image's header is damaged. Copy or download it again.");
  const uint64_t total = ciso_size(block, map);
  const fs::path part = fs::path(out_path).concat(".part");
  std::error_code ec;
  fs::create_directories(out_path.parent_path(), ec);
  bool ok = false;
  struct Remove { const fs::path& p; bool* ok; ~Remove() { if (!*ok) { std::error_code e; fs::remove(p, e); } } } cleanup{part, &ok};
  {
    File out;
    if (!out.open(part, true)) return fail(error, "Could not write the disc image to " + out_path.parent_path().u8string() + ".");
    if (!in.seek(kCisoHeader)) return fail(error, "This CISO image could not be read.");
    std::vector<uint8_t> buf(block), zeros(block, 0);
    uint64_t written = 0;
    for (size_t i = 0; i < map.size() && written < total; ++i) {
      const size_t n = (size_t)std::min<uint64_t>(block, total - written);
      if (map[i]) {
        if (!in.read(buf.data(), n)) return fail(error, "This CISO image is cut short. Copy or download it again.");
        if (n < block && !in.seek(kCisoHeader)) return fail(error, "This CISO image could not be read.");   // the last block
        if (!out.write(buf.data(), n)) return fail(error, "Could not write the disc image.");
      } else if (!out.write(zeros.data(), n)) return fail(error, "Could not write the disc image.");
      written += n;
      if (progress && !progress(written, total)) return fail(error, "Cancelled.");
    }
    if (!out.close()) return fail(error, "Could not write the disc image.");
  }
  fs::rename(part, out_path, ec);
  if (ec) return fail(error, "Could not write the disc image to " + out_path.parent_path().u8string() + ".");
  ok = true;
  return true;
}

}  // namespace

Format detect(const fs::path& file) {
  File in;
  uint8_t magic[4] = {};
  if (!in.open(file, false) || !in.read(magic, 4)) return Format::NotCompressed;
  if (std::memcmp(magic, "RVZ\x01", 4) == 0) return Format::Rvz;
  if (std::memcmp(magic, "WIA\x01", 4) == 0) return Format::Wia;
  if (std::memcmp(magic, "CISO", 4) == 0) return Format::Ciso;
  if (nkit_tag(in)) return Format::NKit;
  return Format::NotCompressed;
}

bool probe(const fs::path& file, Info* info, std::string* error) {
  File in;
  if (!in.open(file, false)) return fail(error, "The disc image could not be opened.");
  if (nkit_tag(in)) return nkit_probe(in, info, error);
  { uint32_t b; std::vector<uint8_t> m; if (ciso_header(in, &b, &m)) return ciso_probe(in, info, error); }
  Image im;
  if (!read_image(in, file, &im, error)) return false;
  info->format = im.format;
  info->gamecube = im.disc_type == 1;
  info->iso_size = im.iso_size;
  info->game_id.clear();
  for (int i = 0; i < 6; ++i) info->game_id += (im.disc_head[i] >= 32 && im.disc_head[i] < 127) ? (char)im.disc_head[i] : '?';
  info->revision = im.disc_head[7];
  info->compression = method_name(im.method);
  return true;
}

bool convert(const fs::path& in_path, const fs::path& out_path, const Progress& progress, std::string* error) {
  File in;
  if (!in.open(in_path, false)) return fail(error, "The disc image could not be opened.");
  if (nkit_tag(in)) return nkit_convert(in, out_path, progress, error);
  { uint32_t b; std::vector<uint8_t> m; if (ciso_header(in, &b, &m)) return ciso_convert(in, out_path, progress, error); }
  Image im;
  if (!read_image(in, in_path, &im, error)) return false;
  Decoder dec(im);
  const bool rvz = im.format == Format::Rvz;
  const size_t group_entry = rvz ? 12 : 8;

  std::vector<uint8_t> raw_table((size_t)im.raw_count * kRawEntrySize);
  std::vector<uint8_t> group_table((size_t)im.group_count * group_entry);
  if (!read_table(in, dec, im.raw_offset, im.raw_bytes, &raw_table) ||
      !read_table(in, dec, im.group_offset, im.group_bytes, &group_table))
    return fail(error, "This disc image's tables are damaged. Copy or download it again.");

  struct Region { uint64_t start, end; uint32_t first_group, groups; };
  std::vector<Region> regions;
  for (uint32_t i = 0; i < im.raw_count; ++i) {
    const uint8_t* e = raw_table.data() + (size_t)i * kRawEntrySize;
    const uint64_t offset = be64(e), size = be64(e + 8);
    const uint32_t first = be32(e + 16), count = be32(e + 20);
    // A region is stored from the 0x8000 boundary below its start (Dolphin's reader does the same).
    const uint64_t start = offset - offset % kBlock, end = offset + size;
    if (size == 0) continue;
    if (end < offset || end > im.iso_size || (uint64_t)first + count > im.group_count ||
        (uint64_t)count * im.chunk_size < end - start)
      return fail(error, "This disc image's tables are damaged. Copy or download it again.");
    regions.push_back({start, end, first, count});
  }
  std::sort(regions.begin(), regions.end(), [](const Region& a, const Region& b) { return a.start < b.start; });

  const fs::path part = fs::path(out_path).concat(".part");
  std::error_code ec;
  fs::create_directories(out_path.parent_path(), ec);
  bool ok = false;
  // Declared before the output file so the file is closed before a failed .part is removed.
  struct Remove { const fs::path& p; bool* ok; ~Remove() { if (!*ok) { std::error_code e; fs::remove(p, e); } } } cleanup{part, &ok};
  File out;
  if (!out.open(part, true)) return fail(error, "Could not write the disc image to " + out_path.parent_path().u8string() + ".");

  uint64_t written = 0;
  const std::vector<uint8_t> zeros(1u << 20, 0);
  auto zero_to = [&](uint64_t target) {
    while (written < target) {
      const size_t n = (size_t)std::min<uint64_t>(zeros.size(), target - written);
      if (!out.write(zeros.data(), n)) return false;
      written += n;
    }
    return true;
  };
  std::vector<uint8_t> stored, staged, chunk(im.chunk_size);
  Junk junk;
  const char* damaged = "This disc image is damaged: part of it does not decompress. Copy or download it again.";
  for (const Region& r : regions) {
    for (uint32_t g = 0; g < r.groups; ++g) {
      const uint64_t chunk_start = r.start + (uint64_t)g * im.chunk_size;
      if (chunk_start >= r.end) break;
      const size_t length = (size_t)std::min<uint64_t>(im.chunk_size, r.end - chunk_start);
      const uint64_t chunk_end = chunk_start + length;
      if (chunk_end <= written) continue;   // already written by an earlier region
      const uint8_t* e = group_table.data() + (size_t)(r.first_group + g) * group_entry;
      const uint64_t data_offset = (uint64_t)be32(e) << 2;
      uint32_t data_size = be32(e + 4);
      const uint32_t packed = rvz ? be32(e + 8) : 0;
      const bool compressed = rvz ? (data_size & 0x80000000u) != 0 : true;
      data_size &= rvz ? 0x7FFFFFFFu : 0xFFFFFFFFu;
      if (data_size == 0) {
        std::memset(chunk.data(), 0, length);
      } else {
        if (data_offset + data_size > im.file_size || packed > kMaxChunk * 2u) return fail(error, damaged);
        stored.resize(data_size);
        if (!in.seek(data_offset) || !in.read(stored.data(), data_size)) return fail(error, "The disc image could not be read.");
        const size_t inner = packed ? packed : length;
        const uint8_t* plain = stored.data();
        if (compressed && im.method != kNone) {
          staged.resize(inner);
          if (!dec.decompress(im.method, stored.data(), stored.size(), staged.data(), inner)) return fail(error, damaged);
          plain = staged.data();
        } else if (stored.size() < inner) {
          return fail(error, damaged);
        }
        if (packed) {
          // Junk is placed by the chunk's offset within its region, as Dolphin's reader does
          // (WIABlob.cpp ReadFromGroups: group_offset_in_data). A GameCube region starts at 0.
          if (!unpack(plain, inner, chunk.data(), length, chunk_start - r.start, junk)) return fail(error, damaged);
        } else {
          std::memcpy(chunk.data(), plain, length);
        }
      }
      if (!zero_to(chunk_start)) return fail(error, "Could not write the disc image. Is the drive full?");
      const size_t skip = (size_t)(written - chunk_start);
      if (!out.write(chunk.data() + skip, length - skip)) return fail(error, "Could not write the disc image. Is the drive full?");
      written = chunk_end;
      if (progress && !progress(written, im.iso_size)) return fail(error, "Cancelled.");
    }
  }
  if (!zero_to(im.iso_size)) return fail(error, "Could not write the disc image. Is the drive full?");
  // The first 0x80 bytes live in the disc struct; the stored chunk may hold them too, but the header copy is the authority.
  if (!out.seek(0) || !out.write(im.disc_head, sizeof im.disc_head) || !out.close())
    return fail(error, "Could not write the disc image. Is the drive full?");
  if (progress) progress(im.iso_size, im.iso_size);
  fs::rename(part, out_path, ec);
  if (ec) return fail(error, "Could not save the disc image as " + out_path.u8string() + ".");
  ok = true;
  return true;
}

}  // namespace disc_rvz
