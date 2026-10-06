// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include "cosmetic_mods.h"
#include "../runtime/gx/companion_texture_match.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string g_disc_target;
static std::vector<uint8_t> g_disc_bytes;
// A test disc with several files: name -> (offset, size) inside g_disc_bytes. Looked up first.
static std::map<std::string, std::pair<uint32_t, uint32_t>> g_disc_table;

// cosmetic_mods.cpp only needs runtime logging; keep this unit target independent of generated
// guest code and the rest of the Windows renderer/runtime.
namespace host {
void log(const char* format, ...) {
  va_list args; va_start(args, format); std::vfprintf(stdout, format, args); va_end(args);
  std::fputc('\n', stdout);
}
bool disc_find_file(const std::string& path, uint32_t* offset, uint32_t* size) {
  const auto listed = g_disc_table.find(path);
  if (listed != g_disc_table.end()) { *offset = listed->second.first; *size = listed->second.second; return true; }
  if (path != g_disc_target || g_disc_bytes.empty()) return false;
  *offset = 0; *size = (uint32_t)g_disc_bytes.size(); return true;
}
bool disc_read(uint32_t offset, void* out, uint32_t size) {
  if ((uint64_t)offset + size > g_disc_bytes.size()) return false;
  std::memcpy(out, g_disc_bytes.data() + offset, size); return true;
}
}

namespace {
int failures = 0;
void check(bool condition, const char* message) {
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
void be32(std::vector<uint8_t>& out, size_t offset, uint32_t value) {
  out[offset] = (uint8_t)(value >> 24); out[offset + 1] = (uint8_t)(value >> 16);
  out[offset + 2] = (uint8_t)(value >> 8); out[offset + 3] = (uint8_t)value;
}
void le16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back((uint8_t)value); out.push_back((uint8_t)(value >> 8));
}
void le32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back((uint8_t)value); out.push_back((uint8_t)(value >> 8));
  out.push_back((uint8_t)(value >> 16)); out.push_back((uint8_t)(value >> 24));
}
uint32_t crc32(const std::vector<uint8_t>& bytes) {
  uint32_t crc = 0xffffffffu;
  for (uint8_t byte : bytes) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
std::vector<uint8_t> rooted_dat(const std::vector<std::string>& roots) {
  constexpr uint32_t data_size = 0x20;
  const size_t root_table = 0x20 + data_size, strings = root_table + roots.size() * 8;
  size_t string_bytes = 0;
  for (const auto& root : roots) string_bytes += root.size() + 1;
  std::vector<uint8_t> bytes(strings + string_bytes, 0);
  be32(bytes, 0, (uint32_t)bytes.size()); be32(bytes, 4, data_size);
  be32(bytes, 8, 0); be32(bytes, 12, (uint32_t)roots.size()); be32(bytes, 16, 0);
  size_t name_offset = 0;
  for (size_t i = 0; i < roots.size(); ++i) {
    be32(bytes, root_table + i * 8, (uint32_t)(i * 4));
    be32(bytes, root_table + i * 8 + 4, (uint32_t)name_offset);
    std::memcpy(bytes.data() + strings + name_offset, roots[i].c_str(), roots[i].size() + 1);
    name_offset += roots[i].size() + 1;
  }
  return bytes;
}
std::vector<uint8_t> visual_dat() {
  constexpr uint32_t data_size = 0x400;
  constexpr uint32_t image_pointer = 0x50, palette_pointer = 0x54;
  constexpr uint32_t image_descriptor = 0x100, palette_descriptor = 0x340;
  const std::string root = "map_head";
  const std::vector<uint32_t> relocations{
      image_pointer, palette_pointer, image_descriptor, palette_descriptor};
  const size_t root_table = 0x20 + data_size + relocations.size() * 4, strings = root_table + 8;
  std::vector<uint8_t> bytes(strings + root.size() + 1, 0);
  be32(bytes, 0, (uint32_t)bytes.size()); be32(bytes, 4, data_size);
  be32(bytes, 8, (uint32_t)relocations.size()); be32(bytes, 12, 1);
  // A bounded TObj-like pair points to independently relocated image and palette descriptors.
  be32(bytes, 0x20 + image_pointer, image_descriptor);
  be32(bytes, 0x20 + palette_pointer, palette_descriptor);
  be32(bytes, 0x20 + image_descriptor, 0x200);
  bytes[0x20 + image_descriptor + 4] = 0; bytes[0x20 + image_descriptor + 5] = 8;
  bytes[0x20 + image_descriptor + 6] = 0; bytes[0x20 + image_descriptor + 7] = 8;
  be32(bytes, 0x20 + image_descriptor + 8, 6); // RGBA8, 8x8 = 256 bytes
  be32(bytes, 0x20 + palette_descriptor, 0x380);
  be32(bytes, 0x20 + palette_descriptor + 4, 1);
  bytes[0x20 + palette_descriptor + 12] = 0;
  bytes[0x20 + palette_descriptor + 13] = 16;
  for (size_t i = 0; i < relocations.size(); ++i)
    be32(bytes, 0x20 + data_size + i * 4, relocations[i]);
  be32(bytes, root_table, 0); be32(bytes, root_table + 4, 0);
  std::memcpy(bytes.data() + strings, root.c_str(), root.size() + 1);
  return bytes;
}
std::vector<uint8_t> effect_dat(const std::string& root, uint16_t color = 0xfc00,
                                uint32_t data_size = 0xc00,
                                std::array<uint32_t, 3> streams = {0x100, 0x200, 0x300},
                                uint8_t scalar = 0) {
  static constexpr std::array<std::array<uint16_t, 23>, 3> signatures{{
      {{0x0323, 0x0521, 0x0523, 0x0525, 0x0523, 0x0526, 0x0522, 0x0520,
        0x0522, 0x051f, 0x0521, 0x0524, 0x0525, 0x0527, 0x0526, 0x0527,
        0x0520, 0x051e, 0x051f, 0x051e, 0x0524, 0x0527, 0x0500}},
      {{0x1516, 0x151a, 0x1515, 0x151a, 0x151b, 0x151a, 0x151d, 0x1514,
        0x1517, 0x1516, 0x1518, 0x1515, 0x1519, 0x151b, 0x1519, 0x151c,
        0x1518, 0x151c, 0x1517, 0x151c, 0x151d, 0x151b, 0x1500}},
      {{0x2f0b, 0x2f10, 0x2f0d, 0x2f10, 0x2f11, 0x2f10, 0x2f13, 0x2f0a,
        0x2f0c, 0x2f0b, 0x2f0e, 0x2f0d, 0x2f0f, 0x2f11, 0x2f0f, 0x2f12,
        0x2f0e, 0x2f12, 0x2f0c, 0x2f12, 0x2f13, 0x2f11, 0x2f00}},
  }};
  constexpr uint32_t descriptor = 0x40;
  uint32_t image = data_size == 0xc00 ? 0x800 : 0x900;
  const size_t root_table = 0x20 + data_size + 4, strings = root_table + 8;
  std::vector<uint8_t> bytes(strings + root.size() + 1, 0);
  be32(bytes, 0, (uint32_t)bytes.size()); be32(bytes, 4, data_size);
  be32(bytes, 8, 1); be32(bytes, 12, 1);
  be32(bytes, 0x20 + descriptor, image);
  bytes[0x20 + descriptor + 4] = 0; bytes[0x20 + descriptor + 5] = 8;
  bytes[0x20 + descriptor + 6] = 0; bytes[0x20 + descriptor + 7] = 8;
  be32(bytes, 0x20 + descriptor + 8, 6);
  be32(bytes, 0x20 + data_size, descriptor);
  be32(bytes, root_table, 0); be32(bytes, root_table + 4, 0);
  std::memcpy(bytes.data() + strings, root.c_str(), root.size() + 1);
  for (size_t stream = 0; stream < signatures.size(); ++stream)
    for (size_t record = 0; record < signatures[stream].size(); ++record)
      be32(bytes, 0x20 + streams[stream] + record * 4,
           ((uint32_t)color << 16) | signatures[stream][record]);
  const uint8_t side_color[] = {0x00, 0x99, 0xff, 0xff, 0xcc, 0xe6};
  std::memcpy(bytes.data() + 0x20 + 0x500, side_color, sizeof side_color);
  bytes[0x20 + 0x700] = scalar;
  return bytes;
}
std::vector<uint8_t> costume_dat(const std::string& root, bool material_animation = true) {
  std::vector<std::string> roots{root + "_Share_joint"};
  if (material_animation) roots.push_back(root + "_Share_matanim_joint");
  return rooted_dat(roots);
}
std::vector<uint8_t> fox_dat() { return costume_dat("PlyFox5KGr"); }
void write_file(const fs::path& path, const std::vector<uint8_t>& bytes) {
  std::ofstream file(path, std::ios::binary); file.write((const char*)bytes.data(), bytes.size());
}
std::vector<uint8_t> read_file(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}
std::vector<uint8_t> one_file_zip(const std::string& name, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> zip;
  uint32_t crc = crc32(payload);
  le32(zip, 0x04034b50); le16(zip, 20); le16(zip, 0); le16(zip, 0); le16(zip, 0); le16(zip, 0);
  le32(zip, crc); le32(zip, (uint32_t)payload.size()); le32(zip, (uint32_t)payload.size());
  le16(zip, (uint16_t)name.size()); le16(zip, 0);
  zip.insert(zip.end(), name.begin(), name.end()); zip.insert(zip.end(), payload.begin(), payload.end());
  uint32_t central_offset = (uint32_t)zip.size();
  le32(zip, 0x02014b50); le16(zip, 20); le16(zip, 20); le16(zip, 0); le16(zip, 0);
  le16(zip, 0); le16(zip, 0); le32(zip, crc); le32(zip, (uint32_t)payload.size());
  le32(zip, (uint32_t)payload.size()); le16(zip, (uint16_t)name.size()); le16(zip, 0); le16(zip, 0);
  le16(zip, 0); le16(zip, 0); le32(zip, 0); le32(zip, 0);
  zip.insert(zip.end(), name.begin(), name.end());
  uint32_t central_size = (uint32_t)zip.size() - central_offset;
  le32(zip, 0x06054b50); le16(zip, 0); le16(zip, 0); le16(zip, 1); le16(zip, 1);
  le32(zip, central_size); le32(zip, central_offset); le16(zip, 0);
  return zip;
}
std::vector<uint8_t> stored_zip(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
  struct Central { std::string name; uint32_t crc, size, offset; };
  std::vector<uint8_t> zip; std::vector<Central> central;
  for (const auto& file : files) {
    Central record{file.first, crc32(file.second), (uint32_t)file.second.size(), (uint32_t)zip.size()};
    le32(zip, 0x04034b50); le16(zip, 20); le16(zip, 0); le16(zip, 0); le16(zip, 0); le16(zip, 0);
    le32(zip, record.crc); le32(zip, record.size); le32(zip, record.size);
    le16(zip, (uint16_t)record.name.size()); le16(zip, 0);
    zip.insert(zip.end(), record.name.begin(), record.name.end());
    zip.insert(zip.end(), file.second.begin(), file.second.end()); central.push_back(record);
  }
  uint32_t central_offset = (uint32_t)zip.size();
  for (const auto& record : central) {
    le32(zip, 0x02014b50); le16(zip, 20); le16(zip, 20); le16(zip, 0); le16(zip, 0);
    le16(zip, 0); le16(zip, 0); le32(zip, record.crc); le32(zip, record.size); le32(zip, record.size);
    le16(zip, (uint16_t)record.name.size()); le16(zip, 0); le16(zip, 0); le16(zip, 0); le16(zip, 0);
    le32(zip, 0); le32(zip, record.offset); zip.insert(zip.end(), record.name.begin(), record.name.end());
  }
  uint32_t central_size = (uint32_t)zip.size() - central_offset;
  le32(zip, 0x06054b50); le16(zip, 0); le16(zip, 0); le16(zip, (uint16_t)central.size());
  le16(zip, (uint16_t)central.size()); le32(zip, central_size); le32(zip, central_offset); le16(zip, 0);
  return zip;
}
std::vector<uint8_t> png(uint32_t width, uint32_t height) {
  std::vector<uint8_t> bytes = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
                                0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 0, 0, 0, 0, 0};
  be32(bytes, 16, width); be32(bytes, 20, height); return bytes;
}
std::vector<uint8_t> vault_zip(const std::vector<uint8_t>& data, bool bad_second_target = false) {
  auto first = one_file_zip("PlFxGr.dat", data);
  auto second = one_file_zip("PlFxGr.dat", data);
  auto stage_dat = visual_dat(), effect_dat = visual_dat();
  stage_dat.push_back(0); be32(stage_dat, 0, (uint32_t)stage_dat.size());
  stage_dat[0x20 + 0x20] = 2;
  effect_dat[0x20 + 0x20] = 1;
  auto stage = one_file_zip("GrNBa.dat", stage_dat);
  std::string second_target = bad_second_target ? "PlFxOr" : "PlFxGr";
  std::string metadata =
      "{\"characters\":{\"Fox\":{\"skins\":["
      "{\"id\":\"tom-nook\",\"color\":\"Tom Nook\",\"costume_code\":\"PlFxGr\","
      "\"filename\":\"tom-nook.zip\",\"has_csp\":true,\"has_stock\":true},"
      "{\"id\":\"tom-nook-alt\",\"color\":\"Tom Nook\",\"costume_code\":\"" + second_target +
      "\",\"filename\":\"tom-nook-alt.zip\",\"has_csp\":true,\"has_stock\":true,"
      "\"paired_popo_id\":\"tom-nook\"}],"
      "\"extras\":{\"shine\":[{\"id\":\"purple\",\"name\":\"Purple Shine\","
      "\"model_file\":\"effects/purple.dat\"}]}}},"
      "\"stages\":{\"battlefield\":{\"variants\":[{\"id\":\"night\","
      "\"name\":\"Night Battlefield\",\"filename\":\"night.zip\"}]}}}";
  return stored_zip({
      {"metadata.json", std::vector<uint8_t>(metadata.begin(), metadata.end())},
      {"Fox/tom-nook.zip", first}, {"Fox/tom-nook-alt.zip", second},
      {"Fox/tom-nook_csp.png", png(136, 188)}, {"Fox/tom-nook_stc.png", png(32, 32)},
      {"Fox/tom-nook-alt_csp.png", png(136, 188)}, {"Fox/tom-nook-alt_stc.png", png(32, 32)},
      {"Fox/effects/purple.dat", effect_dat}, {"das/battlefield/night.zip", stage},
  });
}
std::vector<uint8_t> malformed_metadata_vault(const std::vector<uint8_t>& data) {
  auto nested = one_file_zip("PlFxGr.dat", data);
  std::string metadata =
      "{\"characters\":{\"Fox\":{\"skins\":[{\"id\":\"bad-type\","
      "\"color\":\"Bad Type\",\"costume_code\":\"PlFxGr\",\"filename\":\"bad.zip\","
      "\"has_csp\":\"yes\"}]}}}";
  return stored_zip({{"metadata.json", std::vector<uint8_t>(metadata.begin(), metadata.end())},
                     {"Fox/bad.zip", nested}});
}
std::vector<uint8_t> one_file_fst(uint32_t start, uint32_t original_size,
                                  const std::string& name = "PlFxGr.dat") {
  std::vector<uint8_t> fst(24 + name.size() + 1, 0);
  be32(fst, 0, 0x01000000); be32(fst, 4, 0); be32(fst, 8, 2);
  be32(fst, 12, 0); be32(fst, 16, start); be32(fst, 20, original_size);
  std::memcpy(fst.data() + 24, name.c_str(), name.size() + 1);
  return fst;
}
uint32_t read_be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
// Two files in the root directory. Their length fields are at offsets 20 and 32.
std::vector<uint8_t> two_file_fst(const std::string& first, uint32_t first_start, uint32_t first_size,
                                  const std::string& second, uint32_t second_start, uint32_t second_size) {
  std::vector<uint8_t> fst(36 + first.size() + 1 + second.size() + 1, 0);
  be32(fst, 0, 0x01000000); be32(fst, 4, 0); be32(fst, 8, 3);
  be32(fst, 12, 0); be32(fst, 16, first_start); be32(fst, 20, first_size);
  be32(fst, 24, (uint32_t)first.size() + 1); be32(fst, 28, second_start); be32(fst, 32, second_size);
  std::memcpy(fst.data() + 36, first.c_str(), first.size() + 1);
  std::memcpy(fst.data() + 36 + first.size() + 1, second.c_str(), second.size() + 1);
  return fst;
}
}

// A three-joint skeleton (root, child, child's sibling) under a _Share_joint root. Every pointer
// has its relocation entry, as in a real archive: that entry, not the value, makes it a pointer.
// child_at_zero stores the child at data offset 0 (the root moves to 0x40), so the root's child
// pointer has the value 0. sibling_as_child hangs joint 2 under joint 1 instead of beside it: the
// same number of joints in a different shape. drop_child_relocation leaves the root's child
// pointer out of the relocation table, which makes it no pointer at all.
std::vector<uint8_t> skeleton_dat(uint32_t root_flags, float child_y, bool extra_joint,
                                  const std::string& name = "PlyFox5K_Share_joint",
                                  bool child_at_zero = false, bool sibling_as_child = false,
                                  bool drop_child_relocation = false) {
  const uint32_t joints = extra_joint ? 4 : 3, data = joints * 0x40;
  const uint32_t at[4] = {child_at_zero ? 0x40u : 0u, child_at_zero ? 0u : 0x40u, 0x80u, 0xC0u};
  std::vector<uint32_t> relocations;
  std::vector<uint8_t> out(0x20 + data, 0);
  auto joint = [&](uint32_t index) { return (size_t)0x20 + at[index]; };
  auto point = [&](uint32_t from, uint32_t field, uint32_t to, bool relocate = true) {
    be32(out, joint(from) + field, at[to]);
    if (relocate) relocations.push_back(at[from] + field);
  };
  be32(out, joint(0) + 4, root_flags);
  point(0, 8, 1, !drop_child_relocation);            // child: joint 1
  point(1, sibling_as_child ? 8 : 12, 2);            // next (or child): joint 2
  if (extra_joint) point(2, 8, 3);                   // joint 2 gains a child
  uint32_t bits; std::memcpy(&bits, &child_y, 4); be32(out, joint(1) + 0x30, bits);
  for (uint32_t i = 0; i < joints; ++i) { float one = 1.0f; std::memcpy(&bits, &one, 4);
    be32(out, joint(i) + 0x20, bits); be32(out, joint(i) + 0x24, bits); be32(out, joint(i) + 0x28, bits); }
  const size_t table = out.size(), root = table + relocations.size() * 4;
  out.resize(root + 8 + name.size() + 1, 0);
  for (size_t i = 0; i < relocations.size(); ++i) be32(out, table + i * 4, relocations[i]);
  be32(out, 0, (uint32_t)out.size()); be32(out, 4, data); be32(out, 8, (uint32_t)relocations.size()); be32(out, 12, 1);
  be32(out, root, at[0]); be32(out, root + 4, 0);
  std::memcpy(out.data() + root + 8, name.c_str(), name.size() + 1);
  return out;
}

// The same three joints with a mesh on the root: one display object, one envelope polygon object,
// one envelope that blends joint 1 and joint 2 with `weight` each. Joint 1 always has an inverse
// bind matrix; joint 2 has one only when `bound`. The game asserts on a blended joint without one.
std::vector<uint8_t> envelope_dat(bool bound, float weight = 0.5f) {
  const std::string name = "PlyFox5K_Share_joint";
  const uint32_t data = 0x138, dobj = 0xC0, pobj = 0xD0, list = 0xE8, descs = 0xF0, matrix = 0x108;
  std::vector<uint32_t> relocations;
  std::vector<uint8_t> out(0x20 + data, 0);
  auto point = [&](uint32_t slot, uint32_t to) { be32(out, (size_t)0x20 + slot, to); relocations.push_back(slot); };
  uint32_t bits; std::memcpy(&bits, &weight, 4);
  be32(out, 0x20 + 4, 0x2);
  point(0x08, 0x40); point(0x40 + 0x0C, 0x80);       // root's child, the child's sibling
  point(0x10, dobj); point(dobj + 0x0C, pobj);       // root's mesh
  out[0x20 + pobj + 0x0C] = 0x20;                    // polygon flags: envelope type
  point(pobj + 0x14, list); point(list, descs);      // one envelope; the slot after it is no pointer
  point(descs, 0x40); be32(out, 0x20 + descs + 4, bits);
  point(descs + 8, 0x80); be32(out, 0x20 + descs + 12, bits);
  point(0x40 + 0x38, matrix);
  if (bound) point(0x80 + 0x38, matrix);
  const size_t table = out.size(), root = table + relocations.size() * 4;
  out.resize(root + 8 + name.size() + 1, 0);
  for (size_t i = 0; i < relocations.size(); ++i) be32(out, table + i * 4, relocations[i]);
  be32(out, 0, (uint32_t)out.size()); be32(out, 4, data); be32(out, 8, (uint32_t)relocations.size()); be32(out, 12, 1);
  be32(out, root, 0); be32(out, root + 4, 0);
  std::memcpy(out.data() + root + 8, name.c_str(), name.size() + 1);
  return out;
}

// A fighter sound bank as the disc keeps it: the header (size of the sound table, size of the
// sample data, number of sounds, id of the first), per sound its channel count and sample rate and
// 0x40 bytes per channel, padding to 32 bytes, then the samples. Every channel gets `channel_bytes`
// of samples filled with `fill`; the sounds listed in `stereo` have two channels.
std::vector<uint8_t> sound_bank(uint32_t count, uint32_t base, const std::vector<uint32_t>& stereo, uint8_t fill,
                                uint32_t channel_bytes = 32, uint16_t format = 0) {
  std::vector<uint8_t> table;
  uint32_t cursor = 0;
  auto word = [&](uint32_t value) { table.resize(table.size() + 4); be32(table, table.size() - 4, value); };
  for (uint32_t sound = 0; sound < count; ++sound) {
    const uint32_t channels = std::find(stereo.begin(), stereo.end(), sound) != stereo.end() ? 2 : 1;
    word(channels); word(16000);
    for (uint32_t channel = 0; channel < channels; ++channel) {
      const size_t voice = table.size();
      table.resize(voice + 0x40, 0);
      table[voice + 3] = (uint8_t)format;
      be32(table, voice + 4, cursor * 2 + 2);                       // loop address, in nibbles
      be32(table, voice + 8, (cursor + channel_bytes) * 2 - 1);     // end address
      be32(table, voice + 12, cursor * 2 + 2);                      // start address
      cursor += channel_bytes;
    }
  }
  std::vector<uint8_t> out(0x10, 0);
  be32(out, 0, (uint32_t)table.size()); be32(out, 4, cursor); be32(out, 8, count); be32(out, 12, base);
  out.insert(out.end(), table.begin(), table.end());
  out.resize((out.size() + 31) & ~(size_t)31, 0);
  out.resize(out.size() + cursor, fill);
  return out;
}
// Where a bank's samples start, and its first sample byte.
size_t bank_samples_at(const std::vector<uint8_t>& bank) { return ((size_t)0x10 + read_be32(bank.data()) + 31) & ~(size_t)31; }

// A disc table with the sound folders: audio/<bank> (Japanese), audio/us/<bank> (English), then
// files in the root. Entry numbers: 2 the Japanese bank, 4 the English one, 5 and up the root files.
struct TableFile { std::string name; uint32_t start, size; };
std::vector<uint8_t> audio_fst(const std::string& bank, uint32_t jp_start, uint32_t jp_size,
                               uint32_t us_start, uint32_t us_size, const std::vector<TableFile>& root_files) {
  const uint32_t entries = 5 + (uint32_t)root_files.size();
  std::string names;
  auto name = [&](const std::string& text) { const uint32_t at = (uint32_t)names.size(); names += text; names.push_back('\0'); return at; };
  std::vector<uint8_t> fst(entries * 12, 0);
  be32(fst, 0, 0x01000000); be32(fst, 8, entries);
  be32(fst, 12, 0x01000000 | name("audio")); be32(fst, 16, 0); be32(fst, 20, 5);
  const uint32_t bank_name = name(bank);
  be32(fst, 24, bank_name); be32(fst, 28, jp_start); be32(fst, 32, jp_size);
  be32(fst, 36, 0x01000000 | name("us")); be32(fst, 40, 1); be32(fst, 44, 5);
  be32(fst, 48, bank_name); be32(fst, 52, us_start); be32(fst, 56, us_size);
  for (size_t i = 0; i < root_files.size(); ++i) {
    const size_t at = 60 + i * 12;
    be32(fst, at, name(root_files[i].name)); be32(fst, at + 4, root_files[i].start); be32(fst, at + 8, root_files[i].size);
  }
  fst.insert(fst.end(), names.begin(), names.end());
  return fst;
}

int main(int argc, char** argv) {
  wchar_t temp_root[MAX_PATH]; GetTempPathW(MAX_PATH, temp_root);
  fs::path folder = fs::path(temp_root) /
      (L"melee-cosmetic-test-" + std::to_wstring(GetCurrentProcessId()));
  std::error_code ec; fs::remove_all(folder, ec); fs::create_directories(folder, ec);
  check(!ec, "create temporary directory");

  if (argc == 5 && std::string(argv[1]) == "--prepare-stage") {
    host::cosmetics::configure(argv[3]);
    const auto imported = fs::u8path(argv[2]).extension() == ".dat" ?
        host::cosmetics::import_stage_dat(argv[2], "GrNBa.dat") : host::cosmetics::import_file(argv[2]);
    bool ok = imported.ok;
    g_disc_target = "GrNBa.dat"; g_disc_bytes = read_file(fs::u8path(argv[4]));
    std::string error;
    bool selected = false;
    for (const auto& asset : host::cosmetics::assets()) {
      if (asset.kind != "stage_visual" || asset.target_path != g_disc_target) continue;
      std::printf("stage=%s available=%d target=%s\n", asset.name.c_str(), asset.available, asset.target_path.c_str());
      selected = asset.available && host::cosmetics::select_variant(asset.target_path, asset.id, &error);
      break;
    }
    ok &= selected && !g_disc_bytes.empty();
    if (ok) {
      auto fst = one_file_fst(0, (uint32_t)g_disc_bytes.size(), "GrNBa.dat");
      host::cosmetics::apply_to_fst(fst.data(), (uint32_t)fst.size());
      std::vector<uint8_t> served(read_be32(fst.data() + 20));
      ok = host::cosmetics::read(0, 0, served.data(), (uint32_t)served.size()) == host::cosmetics::OverrideRead::Success;
      std::printf("stage_runtime=%s bytes=%zu imported=%s error=%s\n", ok ? "active" : "failed", served.size(), imported.message.c_str(), error.c_str());
    } else std::printf("stage_runtime=failed imported=%s error=%s\n", imported.message.c_str(), error.c_str());
    fs::remove_all(folder, ec);
    return ok ? 0 : 1;
  }

  if (argc == 5 && std::string(argv[1]) == "--effect") {
    auto clean = read_file(fs::u8path(argv[3]));
    auto candidate = read_file(fs::u8path(argv[4]));
    std::vector<uint8_t> runtime;
    std::string classification, error;
    bool ok = !clean.empty() && !candidate.empty() &&
        host::cosmetics::testing::materialize_effect_dat(
            argv[2], clean, candidate, &runtime, &classification, &error);
    std::printf("effect_runtime=%s target=%s clean=%zu candidate=%zu runtime=%zu\n%s%s%s\n",
                ok ? "active" : "failed", argv[2], clean.size(), candidate.size(), runtime.size(),
                ok ? "classification=" : "error=", ok ? classification.c_str() : error.c_str(),
                ok ? "" : (clean.empty() || candidate.empty() ? " (input missing)" : ""));
    fs::remove_all(folder, ec);
    return ok ? 0 : 1;
  }

  if (argc == 5 && std::string(argv[1]) == "--prepare-effects") {
    host::cosmetics::configure(fs::u8path(argv[3]).string());
    auto imported = host::cosmetics::import_file(argv[2]);
    std::map<std::string, bool> selected;
    bool ok = imported.ok;
    for (const auto& asset : host::cosmetics::assets()) {
      if (asset.kind == "effect_visual") continue;
      std::string error;
      if (!host::cosmetics::disable_target(asset.target_path, &error)) {
        std::fprintf(stderr, "%s: %s\n", asset.target_path.c_str(), error.c_str());
        ok = false;
      }
    }
    for (const auto& asset : host::cosmetics::assets()) {
      if (asset.kind != "effect_visual" || selected[asset.target_path]) continue;
      g_disc_target = asset.target_path;
      g_disc_bytes = read_file(fs::u8path(argv[4]) / fs::u8path(asset.target_path));
      std::string error;
      if (g_disc_bytes.empty() ||
          !host::cosmetics::select_variant(asset.target_path, asset.id, &error)) {
        std::fprintf(stderr, "%s: %s\n", asset.target_path.c_str(),
                     g_disc_bytes.empty() ? "clean resource missing" : error.c_str());
        ok = false;
        continue;
      }
      selected[asset.target_path] = true;
      std::printf("selected %s | %s | %s\n", asset.target_path.c_str(),
                  asset.name.c_str(), asset.availability_message.c_str());
    }
    g_disc_target.clear(); g_disc_bytes.clear();
    std::printf("prepared_effect_targets=%zu catalog_assets=%zu\n",
                selected.size(), host::cosmetics::assets().size());
    fs::remove_all(folder, ec);
    return ok && selected.size() == 4 ? 0 : 1;
  }

  if (argc == 3 && (std::string(argv[1]) == "--vault" ||
                    std::string(argv[1]) == "--vault-stage")) {
    const bool stage_mode = std::string(argv[1]) == "--vault-stage";
    host::cosmetics::configure((folder / L"port-settings.ini").string());
    auto imported = host::cosmetics::import_file(argv[2]);
    auto catalog = host::cosmetics::assets();
    size_t fox = 0;
    for (const auto& asset : catalog) if (asset.character == "Fox") ++fox;
    std::printf("%s\nvariants=%zu fox=%zu\n", imported.message.c_str(), catalog.size(), fox);
    bool stage_ok = true;
    if (stage_mode && imported.ok) {
      auto stage = std::find_if(catalog.begin(), catalog.end(), [](const auto& asset) {
        return asset.kind == "stage_visual";
      });
      std::string stage_error;
      stage_ok = stage != catalog.end() &&
          host::cosmetics::select_variant(stage->target_path, stage->id, &stage_error);
      if (stage_ok) {
        auto fst = one_file_fst(0x00234000, 1, stage->target_path);
        host::cosmetics::apply_to_fst(fst.data(), (uint32_t)fst.size());
        stage_ok = read_be32(fst.data() + 20) > 1;
      }
      std::printf("stage_runtime=%s%s%s\n", stage_ok ? "active" : "failed",
                  stage_error.empty() ? "" : " error=", stage_error.c_str());
    }
    fs::remove_all(folder, ec);
    return imported.ok && stage_ok ? 0 : 1;
  }

  const auto dat = fox_dat();
  {
    const auto vanilla = skeleton_dat(0x2, 5.0f, false);
    std::string why;
    check(host::cosmetics::testing::costume_skeleton_matches(vanilla, skeleton_dat(0x2 | 0x10 | 0x40000, 5.0f, false), &why),
          "costume skeleton: drawing-only joint flags may differ");
    check(!host::cosmetics::testing::costume_skeleton_matches(vanilla, skeleton_dat(0x2, 5.5f, false), &why),
          "costume skeleton: a moved bone keeps the costume offline");
    check(host::cosmetics::testing::costume_skeleton_matches(vanilla, skeleton_dat(0x2 | 0x8, 5.0f, false), &why),
          "costume skeleton: the classical scaling flag may differ (animations set it themselves)");
    check(!host::cosmetics::testing::costume_skeleton_matches(vanilla, skeleton_dat(0x2 | 0x20000, 5.0f, false), &why),
          "costume skeleton: a transform flag change keeps the costume offline");
    check(!host::cosmetics::testing::costume_skeleton_matches(vanilla, skeleton_dat(0x2, 5.0f, true), &why),
          "costume skeleton: an extra joint keeps the costume offline");
    check(!host::cosmetics::testing::costume_skeleton_matches(vanilla, fox_dat(), &why),
          "costume skeleton: a different tree is refused");
    using host::cosmetics::online_reason_short;
    const std::string name = "PlyFox5K_Share_joint";
    // A pointer is an offset into the data block, so the value 0 points at the joint stored first.
    check(host::cosmetics::testing::costume_skeleton_matches(vanilla, skeleton_dat(0x2, 5.0f, false, name, true), &why) &&
              why == "3 joints match",
          "costume skeleton: a child joint stored at data offset 0 is a joint");
    check(!host::cosmetics::testing::costume_skeleton_matches(vanilla, skeleton_dat(0x2, 5.0f, false, name, false, true), &why) &&
              online_reason_short(why) == "bone 2: shape differs",
          "costume skeleton: a missing joint against a present one names the bone");
    check(!host::cosmetics::testing::costume_skeleton_matches(vanilla, skeleton_dat(0x2, 5.0f, false, name, true, false, true), &why) &&
              online_reason_short(why) == "bone count differs (1 instead of 3)",
          "costume skeleton: a value with no relocation entry is not a pointer");
    check(!host::cosmetics::testing::costume_skeleton_matches(vanilla, skeleton_dat(0x2, 5.5f, false), &why) &&
              online_reason_short(why) == "bone 1: rest pose differs (moved)",
          "costume skeleton: a moved bone is named and said to have moved");
    check(!host::cosmetics::testing::costume_skeleton_matches(vanilla, skeleton_dat(0x2 | 0x20000, 5.0f, false), &why) &&
              online_reason_short(why) == "bone 0: settings differ",
          "costume skeleton: a transform flag change names the bone");
    check(!host::cosmetics::testing::costume_skeleton_matches(vanilla, skeleton_dat(0x2, 5.0f, true), &why) &&
              online_reason_short(why) == "bone count differs (4 instead of 3)",
          "costume skeleton: a different joint count gives both counts");
    using host::cosmetics::costume_draw_safe;
    check(costume_draw_safe(envelope_dat(true), &why), "costume mesh: blended bones with bind matrices are drawn");
    check(!costume_draw_safe(envelope_dat(false), &why) &&
              why == "A mesh is skinned to bone 2, which has no bind matrix: the game would stop when it is drawn.",
          "costume mesh: a blended bone without a bind matrix refuses the costume and names the bone");
    check(costume_draw_safe(envelope_dat(false, 1.0f), &why),
          "costume mesh: a full-weight envelope takes the game's unchecked path and is left alone");
    check(costume_draw_safe(vanilla, &why) && costume_draw_safe(fox_dat(), &why),
          "costume mesh: a file with no envelope mesh, or no skeleton root, is not refused");
  }
  auto inspected = host::cosmetics::testing::inspect_dat(dat);
  check(inspected.ok, "synthetic Fox DAT validates");
  check(inspected.target_path == "PlFxGr.dat", "green Fox roots map to PlFxGr.dat");
  auto marth = host::cosmetics::testing::inspect_dat(costume_dat("PlyMars5KWh"));
  check(marth.ok && marth.target_path == "PlMsWh.dat" && marth.character == "Marth",
        "non-Fox stock costume identity is resolved from roots");
  auto falcon = host::cosmetics::testing::inspect_dat(costume_dat("PlyCaptain5KBu", false));
  check(falcon.ok && falcon.target_path == "PlCaBu.dat",
        "observed joint-only costume DAT is accepted without trusting its filename");
  check(!host::cosmetics::testing::inspect_dat(costume_dat("PlyFox5KRe")).ok,
        "nonexistent additional Fox costume slot is rejected");
  check(!host::cosmetics::testing::inspect_dat(rooted_dat(
            {"PlyFox5KGr_Share_joint", "PlyMars5K_Share_joint"})).ok,
        "conflicting costume identities are rejected");
  auto bad_relocation = dat;
  bad_relocation.insert(bad_relocation.begin() + 0x40, 4, 0);
  be32(bad_relocation, 0, (uint32_t)bad_relocation.size());
  be32(bad_relocation, 8, 1);
  be32(bad_relocation, 0x40, 0x21);
  check(!host::cosmetics::testing::inspect_dat(bad_relocation).ok,
        "unaligned relocation entry is rejected");
  auto clean_visual = visual_dat(), changed_visual = clean_visual, changed_palette = clean_visual,
       changed_scalar = clean_visual;
  changed_visual[0x20 + 0x200] = 0x7f;
  changed_palette[0x20 + 0x380] = 0x6e;
  changed_scalar[0x20 + 0x20] = 1;
  std::string visual_error;
  check(host::cosmetics::testing::visual_dat_only(clean_visual, changed_visual, &visual_error),
        "visual DAT accepts changes confined to validated GX image payloads");
  check(host::cosmetics::testing::visual_dat_only(clean_visual, changed_palette, &visual_error),
        "visual DAT accepts anchored and bounded GX palette payloads");
  check(!host::cosmetics::testing::visual_dat_only(clean_visual, changed_scalar, &visual_error) &&
        visual_error.find("non-texture") != std::string::npos,
        "visual DAT rejects scalar, collision, hazard, or parameter bytes");
  check(!host::cosmetics::testing::visual_dat_only(clean_visual, clean_visual, &visual_error),
        "identical DAT is not treated as a cosmetic variant");

  std::vector<uint8_t> effect_runtime;
  std::string effect_classification;
  auto dedicated_clean = effect_dat("effFoxDataTable");
  auto dedicated_candidate = effect_dat("effFoxDataTable", 0xfc00, 0xc00,
                                        {0x100, 0x200, 0x300}, 7);
  check(host::cosmetics::testing::materialize_effect_dat(
            "EfFxData.dat", dedicated_clean, dedicated_candidate, &effect_runtime,
            &effect_classification, &visual_error) && effect_runtime == dedicated_candidate,
        "dedicated Fox effect archive accepts bounded effect-animation changes");
  auto common_effect_clean = effect_dat("effCommonDataTable");
  auto common_effect_candidate = effect_dat("effCommonDataTable", 0xfc00, 0xc00,
                                            {0x100, 0x200, 0x300}, 7);
  check(!host::cosmetics::testing::materialize_effect_dat(
            "EfCoData.dat", common_effect_clean, common_effect_candidate, &effect_runtime,
            &effect_classification, &visual_error),
        "common effect archive rejects changes outside validated texture payloads");
  auto fox_effect_clean = effect_dat("ftDataFox");
  auto fox_effect_candidate = effect_dat("ftDataFox", 0xa50f);
  const uint8_t purple_side[] = {0xff, 0xff, 0xff, 0xff, 0xa1, 0x00};
  std::memcpy(fox_effect_candidate.data() + 0x20 + 0x500, purple_side, sizeof purple_side);
  check(host::cosmetics::testing::materialize_effect_dat(
            "PlFx.dat", fox_effect_clean, fox_effect_candidate, &effect_runtime,
            &effect_classification, &visual_error) && effect_runtime == fox_effect_candidate,
        "Fox fighter archive accepts only verified particle and side-B color fields");
  fox_effect_candidate[0x20 + 0x700] = 1;
  check(!host::cosmetics::testing::materialize_effect_dat(
            "PlFx.dat", fox_effect_clean, fox_effect_candidate, &effect_runtime,
            &effect_classification, &visual_error),
        "Fox fighter archive rejects changes outside verified effect-color fields");
  auto falco_effect_clean = effect_dat("ftDataFalco");
  auto falco_effect_candidate = effect_dat("ftDataFalco", 0x0f0f, 0xd00,
                                           {0x180, 0x280, 0x380}, 9);
  check(host::cosmetics::testing::materialize_effect_dat(
            "PlFc.dat", falco_effect_clean, falco_effect_candidate, &effect_runtime,
            &effect_classification, &visual_error) &&
            effect_runtime.size() == falco_effect_clean.size() &&
            effect_runtime[0x20 + 0x700] == 0 &&
            read_be32(effect_runtime.data() + 0x20 + 0x100) == 0x0f0f0323,
        "repacked Falco archive contributes only laser-color streams to clean fighter data");

  fs::path dat_path = folder / L"Tom Nook.dat"; write_file(dat_path, dat);
  host::cosmetics::configure((folder / L"port-settings.ini").string());
  auto imported = host::cosmetics::import_file(dat_path.string());
  check(imported.ok && !imported.already_present, "direct DAT imports");
  check(fs::exists(folder / L"CosmeticMods" / L"state.json"),
        "catalog and profile publish through one canonical atomic state file");
  check(host::cosmetics::assets().size() == 1 && host::cosmetics::assets()[0].selected,
        "import selects a persistent variant");
  auto repeated = host::cosmetics::import_file(dat_path.string());
  check(repeated.ok && repeated.already_present && host::cosmetics::assets().size() == 1,
        "repeated import is idempotent");
  host::cosmetics::configure((folder / L"port-settings.ini").string());
  check(host::cosmetics::assets().size() == 1 && host::cosmetics::assets()[0].selected,
        "catalog and selection reload from disk");

  const std::string first_id = host::cosmetics::assets()[0].id;
  std::string error;
  check(host::cosmetics::disable_target("PlFxGr.dat", &error),
        "per-slot Vanilla choice is saved");
  host::cosmetics::configure((folder / L"port-settings.ini").string());
  auto vanilla_refresh = host::cosmetics::import_file(dat_path.string());
  check(vanilla_refresh.ok && !host::cosmetics::assets()[0].selected,
        "re-import does not override an explicit per-slot Vanilla choice");
  check(host::cosmetics::select_variant("PlFxGr.dat", first_id, &error),
        "variant can be reselected after explicit Vanilla");
  auto alternate_dat = dat; alternate_dat[0x20] = 1;
  fs::path alternate_path = folder / L"Alternate green Fox.dat"; write_file(alternate_path, alternate_dat);
  auto alternate = host::cosmetics::import_file(alternate_path.string());
  auto variants = host::cosmetics::assets();
  check(alternate.ok && variants.size() == 2, "second variant for one slot imports");
  check(variants[0].id == first_id && variants[0].selected && !variants[1].selected,
        "multiple variants preserve the current deterministic selection");

  host::cosmetics::freeze_for_online_session();
  check(!host::cosmetics::select_variant("PlFxGr.dat", alternate.asset_id, &error) &&
        error.find("frozen") != std::string::npos,
        "profile mutation is blocked while the online-session hook is frozen");
  check(!host::cosmetics::import_file(dat_path.string()).ok,
        "imports are rejected before reading while the online profile is frozen");
  host::cosmetics::thaw_after_online_session();
  check(host::cosmetics::select_variant("PlFxGr.dat", alternate.asset_id, &error),
        "profile mutation resumes after the online-session hook thaws");

  fs::remove(folder / L"CosmeticMods" / L"assets" / fs::u8path(alternate.asset_id) /
             L"PlFxGr.dat", ec);
  auto missing_fst = one_file_fst(0x00123300, 0x10000);
  host::cosmetics::apply_to_fst(missing_fst.data(), (uint32_t)missing_fst.size());
  check(read_be32(missing_fst.data() + 20) == 0x10000 &&
        host::cosmetics::session_profile().active_assets == 0,
        "missing selected asset fails safely to the vanilla FST file");
  check(host::cosmetics::refresh_catalog(&error), "catalog refresh validates stored assets");
  variants = host::cosmetics::assets();
  check(!variants[1].available && !variants[0].selected && !variants[1].selected,
        "refresh marks the missing variant unavailable and persists Vanilla fallback");
  auto repaired = host::cosmetics::import_file(alternate_path.string());
  variants = host::cosmetics::assets();
  check(repaired.ok && repaired.already_present && variants[1].available &&
        !variants[0].selected && !variants[1].selected,
        "re-import repairs missing content while preserving Vanilla for a multi-variant slot");
  check(host::cosmetics::select_variant("PlFxGr.dat", first_id, &error),
        "a valid variant can be selected after missing-asset fallback");
  check(host::cosmetics::rename_asset(first_id, "  Green Test Variant  ", &error),
        "variant display name can be changed without changing content identity");
  host::cosmetics::configure((folder / L"port-settings.ini").string());
  variants = host::cosmetics::assets();
  check(variants[0].id == first_id && variants[0].name == "Green Test Variant" &&
        variants[0].selected,
        "renamed display name and selection persist across catalog reload");
  check(!host::cosmetics::rename_asset(first_id, "   ", &error),
        "empty display name is rejected");

  auto malformed = dat; malformed[3] ^= 1;
  fs::path bad_dat = folder / L"bad.dat"; write_file(bad_dat, malformed);
  check(!host::cosmetics::import_file(bad_dat.string()).ok, "malformed DAT is rejected");
  fs::path oversized = folder / L"oversized.dat";
  { std::ofstream file(oversized, std::ios::binary); file.seekp((std::streamoff)host::cosmetics::kMaxAssetBytes); file.put(0); }
  check(!host::cosmetics::import_file(oversized.string()).ok, "asset over size boundary is rejected before allocation");

  fs::path zip_path = folder / L"Tom Nook.zip"; write_file(zip_path, one_file_zip("Finished Tom Nook/Tom Nook.dat", dat));
  auto zip_import = host::cosmetics::import_file(zip_path.string());
  check(zip_import.ok && zip_import.already_present, "validated ZIP import reaches the same content-addressed asset");
  fs::path companion_zip = folder / L"Tom Nook with companions.zip";
  write_file(companion_zip, stored_zip({
      {"Finished Tom Nook/Tom Nook.dat", dat},
      {"Finished Tom Nook/Tom Nook CSP.png", png(136, 188)},
      {"Finished Tom Nook/TomNookStockIcon.png", png(24, 24)},
  }));
  auto companion_import = host::cosmetics::import_file(companion_zip.string());
  variants = host::cosmetics::assets();
  check(companion_import.ok && companion_import.already_present &&
        variants[0].unsupported_companions.size() == 2,
        "ordinary single-costume ZIP refresh stores CSP and stock companions");
  size_t before_vault = host::cosmetics::assets().size();
  fs::path malformed_vault = folder / L"malformed-metadata.zip";
  write_file(malformed_vault, malformed_metadata_vault(dat));
  check(!host::cosmetics::import_file(malformed_vault.string()).ok,
        "invalid Nucleus metadata field types fail closed without terminating");
  fs::path bad_vault = folder / L"bad-vault.zip"; write_file(bad_vault, vault_zip(dat, true));
  check(!host::cosmetics::import_file(bad_vault.string()).ok &&
        host::cosmetics::assets().size() == before_vault,
        "multi-asset vault validation failure leaves the previous catalog unchanged");
  fs::path vault = folder / L"vault.zip"; write_file(vault, vault_zip(dat));
  auto vault_import = host::cosmetics::import_file(vault.string());
  variants = host::cosmetics::assets();
  check(vault_import.ok && !vault_import.already_present && variants.size() == before_vault + 4,
        "saved Nucleus project preserves character, stage, and effect resources");
  size_t mapped_companions = 0;
  size_t preserved_dependencies = 0;
  std::string nucleus_id;
  for (const auto& variant : variants)
    if (variant.id.rfind("nucleus-", 0) == 0) {
      mapped_companions += variant.unsupported_companions.size();
      preserved_dependencies += variant.dependencies.size();
      if (nucleus_id.empty()) nucleus_id = variant.id;
    }
  check(mapped_companions == 4,
        "Nucleus CSP and stock companions are preserved and marked as native texture mappings");
  check(preserved_dependencies == 1, "Nucleus inter-variant dependencies are preserved");
  size_t project_visuals = 0;
  std::string stage_id, effect_id;
  for (const auto& variant : variants)
    if (variant.kind == "stage_visual" || variant.kind == "effect_visual") {
      ++project_visuals;
      if (variant.kind == "stage_visual") stage_id = variant.id;
      else effect_id = variant.id;
    }
  check(project_visuals == 2, "project-only stage and effect DATs enter the catalog");
  check(host::cosmetics::select_variant("GrNBa.dat", stage_id, &error),
        "structurally validated project stage can be selected as a full replacement");
  constexpr uint32_t stage_start = 0x00234000;
  auto stage_fst = one_file_fst(stage_start, (uint32_t)visual_dat().size(), "GrNBa.dat");
  host::cosmetics::apply_to_fst(stage_fst.data(), (uint32_t)stage_fst.size());
  auto full_stage = visual_dat();
  full_stage.push_back(0); be32(full_stage, 0, (uint32_t)full_stage.size());
  full_stage[0x20 + 0x20] = 2;
  check(read_be32(stage_fst.data() + 20) == full_stage.size(),
        "full project stage patches the FST for every game mode");
  std::vector<uint8_t> stage_read(full_stage.size());
  check(host::cosmetics::read(stage_start, 0, stage_read.data(), (uint32_t)stage_read.size()) ==
            host::cosmetics::OverrideRead::Success && stage_read == full_stage,
        "full project stage bytes are served by the native DVD override");
  g_disc_bytes.resize(stage_start + visual_dat().size());
  const auto clean_stage = visual_dat();
  std::copy(clean_stage.begin(), clean_stage.end(), g_disc_bytes.begin() + stage_start);
  host::cosmetics::freeze_for_online_session();
  std::vector<uint8_t> online_stage(clean_stage.size());
  // The game was told this file has the override's length (the FST above), and it checks an
  // archive's own length field against that: the disc's stage carries the same length.
  check(host::cosmetics::read(stage_start, 0, online_stage.data(), (uint32_t)online_stage.size()) ==
            host::cosmetics::OverrideRead::Success &&
            read_be32(online_stage.data()) == full_stage.size() &&
            std::equal(clean_stage.begin() + 4, clean_stage.end(), online_stage.begin() + 4),
        "unsafe project stage serves the disc's own stage during online play, with the file's length in its length field");
  host::cosmetics::thaw_after_online_session();
  // Random skin choices belong to one match and never replace the saved fixed selection.
  check(!host::cosmetics::random_stage_skins(), "random stage skins default off");
  check(host::cosmetics::set_random_stage_skins(true,&error), "enable random stage skins");
  auto pick=host::cosmetics::plan_stage_skin(stage_fst.data(),(uint32_t)stage_fst.size(),"GrNBa.dat",100);
  check(pick.ok && host::cosmetics::applied_asset(stage_start)==stage_id, "offline random selects installed full stage");
  check(!host::cosmetics::plan_stage_skin(stage_fst.data(),(uint32_t)stage_fst.size(),"GrNBa.dat",100).ok,
        "one match token cannot reroll a stage");
  host::cosmetics::freeze_for_online_session();
  check(!host::cosmetics::set_random_stage_skins(false,&error), "online queue freezes random setting");
  pick=host::cosmetics::plan_stage_skin(stage_fst.data(),(uint32_t)stage_fst.size(),"GrNBa.dat",101);
  check(pick.ok && host::cosmetics::applied_asset(stage_start).empty(), "online random excludes unsafe stage and serves standard");
  host::cosmetics::thaw_after_online_session();
  check(host::cosmetics::set_random_stage_skins(false,&error), "disable random stage skins");
  pick=host::cosmetics::plan_stage_skin(stage_fst.data(),(uint32_t)stage_fst.size(),"GrNBa.dat",102);
  check(pick.ok && host::cosmetics::applied_asset(stage_start)==stage_id, "disabling random restores saved fixed stage");
  g_disc_bytes.clear();
  g_disc_bytes = visual_dat();
  g_disc_target = "EfFxData.dat";
  check(!host::cosmetics::select_variant("EfFxData.dat", effect_id, &error),
        "project effect changing non-image data is unavailable and leaves Vanilla selected");
  g_disc_target.clear(); g_disc_bytes.clear();
  auto repeated_vault = host::cosmetics::import_file(vault.string());
  check(repeated_vault.ok && repeated_vault.already_present &&
        host::cosmetics::assets().size() == before_vault + 4,
        "repeated Nucleus vault import is idempotent by stable metadata identity");
  check(host::cosmetics::select_variant("PlFxGr.dat", nucleus_id, &error),
        "Nucleus variant with companions can be selected");
  fs::path unsafe_zip = folder / L"unsafe.zip"; write_file(unsafe_zip, one_file_zip("../escape.dat", dat));
  std::vector<std::string> names; std::string zip_error;
  check(!host::cosmetics::testing::inspect_zip(unsafe_zip.string(), &names, &zip_error),
        "ZIP traversal path is rejected");

  constexpr uint32_t start = 0x00123400;
  // The disc slot is shorter than the costume: a costume that is not proven skeleton-equal (this
  // test disc has no clean copy) is padded to the disc's extent for the online fallback, and a
  // shorter slot keeps the override's own length.
  auto fst = one_file_fst(start, 0x20);
  host::cosmetics::apply_to_fst(fst.data(), (uint32_t)fst.size());
  check(read_be32(fst.data() + 20) == dat.size(), "FST logical file size is patched");
  auto session = host::cosmetics::session_profile();
  check(session.active_assets == 1 && host::cosmetics::runtime_initialized(), "runtime snapshot has one active asset");
  auto active_companions = host::cosmetics::active_companions();
  check(active_companions.size() == 2 && active_companions[0].target_path == "PlFxGr.dat" &&
        active_companions[1].target_path == "PlFxGr.dat",
        "selected Nucleus CSP and stock PNGs enter the immutable runtime snapshot");
  uint8_t partial[16]{};
  check(host::cosmetics::read(start, 4, partial, sizeof partial) == host::cosmetics::OverrideRead::Success &&
        std::memcmp(partial, dat.data() + 4, sizeof partial) == 0, "partial file-relative override read");
  uint8_t tail[32]; std::memset(tail, 0xcc, sizeof tail);
  check(host::cosmetics::read(start, (uint32_t)dat.size() - 10, tail, sizeof tail) == host::cosmetics::OverrideRead::Success,
        "aligned final read succeeds");
  check(std::memcmp(tail, dat.data() + dat.size() - 10, 10) == 0, "aligned final read preserves payload");
  bool zero_tail = true; for (size_t i = 10; i < sizeof tail; ++i) zero_tail &= tail[i] == 0;
  check(zero_tail, "aligned final read zero-fills bounded padding");
  uint8_t too_far[64]{};
  check(host::cosmetics::read(start, (uint32_t)dat.size() - 10, too_far, sizeof too_far) == host::cosmetics::OverrideRead::Failed,
        "unbounded read past override fails closed");
  check(host::cosmetics::read(start + 1, 0, partial, sizeof partial) == host::cosmetics::OverrideRead::NotOverridden,
        "unrelated disc file falls back to vanilla");

  host::cosmetics::freeze_for_online_session();
  check(host::cosmetics::session_profile().frozen, "online integration hook exposes frozen session");
  host::cosmetics::thaw_after_online_session();
  check(!host::cosmetics::session_profile().frozen, "online integration hook thaws session");

  check(host::cosmetics::restore_vanilla(&error) && host::cosmetics::pending_restart(),
        "Restore Vanilla persists a staged profile change");
  auto vanilla_fst = one_file_fst(start, 0x10000);
  host::cosmetics::apply_to_fst(vanilla_fst.data(), (uint32_t)vanilla_fst.size());
  check(read_be32(vanilla_fst.data() + 20) == 0x10000 &&
        host::cosmetics::session_profile().active_assets == 0,
        "Restore Vanilla leaves FST untouched and clears runtime overrides after restart boundary");
  check(host::cosmetics::read(start, 0, partial, sizeof partial) == host::cosmetics::OverrideRead::NotOverridden,
        "Restore Vanilla clears affected read cache/snapshot");
  check(host::cosmetics::active_companions().empty(),
        "Restore Vanilla clears companion texture mappings at the restart boundary");

  fs::path bracket_folder = folder / L"bracket-stage";
  fs::create_directories(bracket_folder, ec);
  host::cosmetics::configure((bracket_folder / L"port-settings.ini").string());
  auto bracket_stage = one_file_zip("Shiny PokeFloats PkFlt_2][GrPu.dat", visual_dat());
  const std::string bracket_metadata =
      "{\"characters\":{},\"stages\":{\"poke_floats\":{\"variants\":[{"
      "\"id\":\"blue-sky\",\"name\":\"Blue Sky Poke Floats\","
      "\"filename\":\"blue-sky.zip\"}]}}}";
  auto bracket_vault = stored_zip({
      {"metadata.json", std::vector<uint8_t>(bracket_metadata.begin(), bracket_metadata.end())},
      {"das/poke_floats/blue-sky.zip", bracket_stage},
  });
  fs::path bracket_path = folder / L"bracket-stage.zip";
  write_file(bracket_path, bracket_vault);
  auto bracket_import = host::cosmetics::import_file(bracket_path.string());
  auto bracket_assets = host::cosmetics::assets();
  check(bracket_import.ok && bracket_assets.size() == 1 &&
            bracket_assets[0].kind == "stage_visual" &&
            bracket_assets[0].target_path == "GrPu.dat",
        "project Poke Floats stage accepts a literal square-bracket DAT filename");

  fs::path nucleus_download = folder / L"files.zip";
  write_file(nucleus_download, one_file_zip("GrNBa_precursor_default.dat", visual_dat()));
  auto nucleus_import = host::cosmetics::import_file(nucleus_download.string());
  auto nucleus_assets = host::cosmetics::assets();
  check(nucleus_import.ok && std::any_of(nucleus_assets.begin(), nucleus_assets.end(),
        [](const auto& asset) { return asset.target_path == "GrNBa.dat" && asset.selected &&
                                     asset.name == "GrNBa_precursor_default"; }),
        "generic Nucleus download identifies its selected Battlefield variant by the DAT name");

  auto named_stage = visual_dat(); named_stage[0x20 + 0x20] = 3;
  fs::path named_stage_path = folder / L"Any Custom Name.dat";
  write_file(named_stage_path, named_stage);
  check(!host::cosmetics::import_file(named_stage_path.string()).ok,
        "automatic import does not guess a stage from an arbitrary filename");
  check(host::cosmetics::import_stage_dat(named_stage_path.string(), "GrNBa.dat").ok,
        "a named raw DAT imports when the replacement stage is explicitly chosen");
  const auto manual_assets = host::cosmetics::assets();
  check(std::any_of(manual_assets.begin(), manual_assets.end(), [](const auto& asset) {
          return asset.name == "Any Custom Name" && asset.target_path == "GrNBa.dat" && asset.selected;
        }), "manual stage selection is persisted for the chosen target");
  check(!host::cosmetics::import_stage_dat(named_stage_path.string(), "NotAStage.dat").ok,
        "manual stage import rejects an unknown target");
  auto disguised = visual_dat();
  const auto name_at = std::search(disguised.begin(), disguised.end(), "map_head", "map_head" + 8);
  if (name_at != disguised.end()) *name_at = 'x';
  write_file(folder / "not a stage.dat", disguised);
  check(!host::cosmetics::import_stage_dat((folder / "not a stage.dat").string(), "GrNBa.dat").ok,
        "manual target selection cannot turn a non-stage DAT into a stage");
  check(host::cosmetics::stage_slots().size() == 33,
        "all 29 standard arenas and four Stadium transformations are offered");
  fs::path corneria_path = folder / L"GrCn_custom.dat";
  write_file(corneria_path, visual_dat());
  check(host::cosmetics::import_file(corneria_path.string()).ok,
        "raw stage recognition covers standard arenas beyond the tournament stages");

  fs::path stadium_path = folder / L"GrPs1.dat";
  write_file(stadium_path, visual_dat());
  auto stadium_import = host::cosmetics::import_file(stadium_path.string());
  bracket_assets = host::cosmetics::assets();
  check(stadium_import.ok && std::any_of(bracket_assets.begin(), bracket_assets.end(),
        [](const auto& asset) { return asset.target_path == "GrPs1.dat" && asset.selected; }),
        "standalone Stadium transformation imports to its own disc resource");
  constexpr uint32_t stadium_start = 0x00278000;
  auto stadium_base_fst = one_file_fst(stadium_start, (uint32_t)visual_dat().size(), "GrPs.dat");
  host::cosmetics::apply_to_fst(stadium_base_fst.data(), (uint32_t)stadium_base_fst.size());
  uint8_t stadium_probe[16]{};
  check(host::cosmetics::read(stadium_start, 0, stadium_probe, sizeof stadium_probe) ==
            host::cosmetics::OverrideRead::NotOverridden,
        "Stadium transformation cannot override base GrPs.dat");
  auto stadium_variant_fst = one_file_fst(stadium_start, (uint32_t)visual_dat().size(), "GrPs1.dat");
  host::cosmetics::apply_to_fst(stadium_variant_fst.data(), (uint32_t)stadium_variant_fst.size());
  check(host::cosmetics::read(stadium_start, 0, stadium_probe, sizeof stadium_probe) ==
            host::cosmetics::OverrideRead::Success,
        "Stadium transformation overrides only GrPs1.dat");

  // A costume file named .usd, and the English twin of its slot. The disc holds PlCaRe.dat and
  // PlCaRe.usd for Captain Falcon's red costume, and the English game loads the .usd.
  using host::cosmetics::OverrideRead;
  fs::path usd_folder = folder / L"usd";
  fs::create_directories(usd_folder, ec);
  host::cosmetics::configure((usd_folder / L"port-settings.ini").string());
  const auto falcon_red = rooted_dat({"PlyCaptain5KRe_Share_joint"});
  fs::path falcon_path = usd_folder / L"PlCaRe.usd";
  write_file(falcon_path, falcon_red);
  auto falcon_import = host::cosmetics::import_file(falcon_path.string());
  auto usd_assets = host::cosmetics::assets();
  check(falcon_import.ok && usd_assets.size() == 1 && usd_assets[0].target_path == "PlCaRe.dat",
        "a costume file named .usd imports to its slot");
  check(host::cosmetics::select_variant("PlCaRe.dat", falcon_import.asset_id, &error),
        "the imported .usd costume can be selected");
  auto usd_zip_path = usd_folder / L"falcon.zip";
  write_file(usd_zip_path, one_file_zip("Blood Falcon/PlCaRe.usd", falcon_red));
  auto usd_zip_import = host::cosmetics::import_file(usd_zip_path.string());
  check(usd_zip_import.ok && usd_zip_import.already_present,
        "a ZIP whose costume member is named .usd is read like a .dat member");
  constexpr uint32_t twin_dat_start = 0x00200000, twin_usd_start = 0x00300000, twin_usd_size = 0x100;
  auto twin_fst = two_file_fst("PlCaRe.dat", twin_dat_start, 0x40, "PlCaRe.usd", twin_usd_start, twin_usd_size);
  host::cosmetics::apply_to_fst(twin_fst.data(), (uint32_t)twin_fst.size());
  check(read_be32(twin_fst.data() + 20) == falcon_red.size() && read_be32(twin_fst.data() + 32) == twin_usd_size,
        "PlCaRe.dat and its English twin PlCaRe.usd are both replaced");
  check(host::cosmetics::session_profile().active_assets == 1,
        "one costume counts once although two disc files serve it");
  std::vector<uint8_t> served_dat(falcon_red.size()), served_usd(twin_usd_size);
  check(host::cosmetics::read(twin_dat_start, 0, served_dat.data(), (uint32_t)served_dat.size()) == OverrideRead::Success &&
            served_dat == falcon_red,
        "the .dat slot serves the costume");
  check(host::cosmetics::read(twin_usd_start, 0, served_usd.data(), twin_usd_size) == OverrideRead::Success &&
            read_be32(served_usd.data()) == twin_usd_size &&
            std::equal(falcon_red.begin() + 4, falcon_red.end(), served_usd.begin() + 4) &&
            std::all_of(served_usd.begin() + falcon_red.size(), served_usd.end(), [](uint8_t b) { return b == 0; }),
        "the English twin serves the costume, padded to its disc file's length");

  // One file length, offline and online. The game checks that an archive's own length field
  // equals the file's length and stops when they differ. A costume whose skeleton differs from
  // the disc's is used offline only, and online the disc's own file is served: both must carry
  // the length the game was given for the file.
  fs::path length_folder = folder / L"length";
  fs::create_directories(length_folder, ec);
  host::cosmetics::configure((length_folder / L"port-settings.ini").string());
  const auto three_joints = skeleton_dat(0x2, 5.0f, false), four_joints = skeleton_dat(0x2, 5.0f, true);
  const auto moved_bone = skeleton_dat(0x2, 5.5f, false);
  check(four_joints.size() > three_joints.size() && moved_bone.size() == three_joints.size(),
        "test costumes: one longer than the disc file, one of the same length");
  fs::path longer_path = length_folder / L"longer.dat";
  write_file(longer_path, four_joints);
  auto longer_import = host::cosmetics::import_file(longer_path.string());
  check(longer_import.ok && host::cosmetics::select_variant("PlFxNr.dat", longer_import.asset_id, &error),
        "a costume with an extra joint imports and can be selected");
  g_disc_bytes = three_joints;   // the disc's own PlFxNr.dat, at offset 0
  auto longer_fst = one_file_fst(0, (uint32_t)three_joints.size(), "PlFxNr.dat");
  host::cosmetics::apply_to_fst(longer_fst.data(), (uint32_t)longer_fst.size());
  uint32_t file_length = read_be32(longer_fst.data() + 20);
  check(file_length == four_joints.size() && !host::cosmetics::online_allowed(0),
        "an offline-only costume longer than the disc file gives the file the costume's length");
  {
    // The verdict is in the listing the Mods tab draws, not only in the log.
    const auto listed = host::cosmetics::assets();
    const auto longer = std::find_if(listed.begin(), listed.end(),
        [&](const auto& asset) { return asset.id == longer_import.asset_id; });
    check(longer != listed.end() && !longer->online_allowed && longer->online_message == "bone count differs (4 instead of 3)",
          "a costume with an extra joint is listed as off online, with the reason");
    uint32_t swapped_stages = 7;
    check(host::cosmetics::swapped_online_count(&swapped_stages) == 1 && swapped_stages == 0,
          "one applied costume is counted as swapped online");
  }
  std::vector<uint8_t> offline(file_length), online(file_length, 0xcc);
  check(host::cosmetics::read(0, 0, offline.data(), file_length) == OverrideRead::Success && offline == four_joints,
        "offline the longer costume is served as it is");
  host::cosmetics::freeze_for_online_session();
  check(host::cosmetics::read(0, 0, online.data(), file_length) == OverrideRead::Success &&
            read_be32(online.data()) == file_length &&
            std::equal(three_joints.begin() + 4, three_joints.end(), online.begin() + 4) &&
            std::all_of(online.begin() + three_joints.size(), online.end(), [](uint8_t b) { return b == 0; }),
        "online the disc's own file is served with the file's length in its length field");
  uint8_t middle[2] = {0xcc, 0xcc};
  check(host::cosmetics::read(0, 2, middle, 2) == OverrideRead::Success &&
            middle[0] == (uint8_t)(file_length >> 8) && middle[1] == (uint8_t)file_length,
        "a read that starts inside the length field gets the same length");
  host::cosmetics::thaw_after_online_session();

  fs::path shorter_path = length_folder / L"shorter.dat";
  write_file(shorter_path, moved_bone);
  auto shorter_import = host::cosmetics::import_file(shorter_path.string());
  check(shorter_import.ok && host::cosmetics::select_variant("PlFxNr.dat", shorter_import.asset_id, &error),
        "a costume with a moved bone imports and can be selected");
  g_disc_bytes = four_joints;    // now the disc's file is the longer one
  auto shorter_fst = one_file_fst(0, (uint32_t)four_joints.size(), "PlFxNr.dat");
  host::cosmetics::apply_to_fst(shorter_fst.data(), (uint32_t)shorter_fst.size());
  file_length = read_be32(shorter_fst.data() + 20);
  check(file_length == four_joints.size() && !host::cosmetics::online_allowed(0),
        "an offline-only costume shorter than the disc file keeps the disc file's length");
  offline.assign(file_length, 0xcc); online.assign(file_length, 0xcc);
  check(host::cosmetics::read(0, 0, offline.data(), file_length) == OverrideRead::Success &&
            read_be32(offline.data()) == file_length &&
            std::equal(moved_bone.begin() + 4, moved_bone.end(), offline.begin() + 4) &&
            std::all_of(offline.begin() + moved_bone.size(), offline.end(), [](uint8_t b) { return b == 0; }),
        "offline the shorter costume is padded and its length field says the file's length");
  host::cosmetics::freeze_for_online_session();
  check(host::cosmetics::read(0, 0, online.data(), file_length) == OverrideRead::Success && online == four_joints,
        "online the disc's own longer file is served unchanged");
  host::cosmetics::thaw_after_online_session();
  {
    const auto listed = host::cosmetics::assets();
    const auto shorter = std::find_if(listed.begin(), listed.end(),
        [&](const auto& asset) { return asset.id == shorter_import.asset_id; });
    const auto longer = std::find_if(listed.begin(), listed.end(),
        [&](const auto& asset) { return asset.id == longer_import.asset_id; });
    check(shorter != listed.end() && !shorter->online_allowed && !shorter->online_message.empty(),
          "the applied costume carries its own verdict");
    check(longer != listed.end() && longer->online_allowed && longer->online_message.empty(),
          "a costume that is not applied has no verdict");
  }
  // A costume that only changes how joints are drawn keeps the standard skeleton: it stays on.
  const auto same_skeleton = skeleton_dat(0x2 | 0x10 | 0x40000, 5.0f, false);
  fs::path matching_path = length_folder / L"matching.dat";
  write_file(matching_path, same_skeleton);
  auto matching_import = host::cosmetics::import_file(matching_path.string());
  check(matching_import.ok && host::cosmetics::select_variant("PlFxNr.dat", matching_import.asset_id, &error),
        "a costume with the standard skeleton imports and can be selected");
  g_disc_bytes = three_joints;
  auto matching_fst = one_file_fst(0, (uint32_t)three_joints.size(), "PlFxNr.dat");
  host::cosmetics::apply_to_fst(matching_fst.data(), (uint32_t)matching_fst.size());
  {
    const auto listed = host::cosmetics::assets();
    const auto matching = std::find_if(listed.begin(), listed.end(),
        [&](const auto& asset) { return asset.id == matching_import.asset_id; });
    check(matching != listed.end() && matching->online_allowed && matching->online_message == "3 joints match",
          "a costume with the standard skeleton is listed as on online, with the joint count");
    check(host::cosmetics::swapped_online_count() == 0 && host::cosmetics::online_allowed(0),
          "nothing is counted as swapped when every applied costume stays on");
  }
  check(host::cosmetics::online_reason_short("Skeleton joint 4 rest pose differs from the vanilla costume (rotated, moved).") == "bone 4: rest pose differs (rotated, moved)" &&
            host::cosmetics::online_reason_short("Skeleton joint 7 flags differ from the vanilla costume.") == "bone 7: settings differ" &&
            host::cosmetics::online_reason_short("Skeleton joint 9 hierarchy differs from the vanilla costume.") == "bone 9: shape differs" &&
            host::cosmetics::online_reason_short("Skeleton joint count differs from the vanilla costume (62 instead of 61).") == "bone count differs (62 instead of 61)" &&
            host::cosmetics::online_reason_short("61 joints match") == "61 joints match" &&
            host::cosmetics::online_reason_short("Something else.") == "Something else",
        "the short reason names the difference");
  g_disc_bytes.clear();

  // Portraits and stock icons with no costume file. The name of a picture can say its costume.
  {
    using host::cosmetics::testing::portrait_slot_from_name;
    std::string slot, kind;
    check(portrait_slot_from_name("PlFxGr stock.png", &slot, &kind) && slot == "PlFxGr.dat" && kind == "stock",
          "a picture named by the costume's file code is a stock icon for that costume");
    check(portrait_slot_from_name("Fox Green.png", &slot, &kind) && slot == "PlFxGr.dat" && kind == "csp",
          "a picture named by fighter and color is that costume's portrait");
    check(portrait_slot_from_name("captain falcon red csp.png", &slot, &kind) && slot == "PlCaRe.dat" && kind == "csp",
          "a two-word fighter name is read as one fighter");
    check(portrait_slot_from_name("Falco_Blue.png", &slot, &kind) && slot == "PlFcBu.dat",
          "Falco is not read as Falcon");
    check(portrait_slot_from_name("Dr Mario default.png", &slot, &kind) && slot == "PlDrNr.dat",
          "Dr. Mario is not read as Mario, and default is the first costume");
    check(portrait_slot_from_name("csp/Marth/White.png", &slot, &kind) && slot == "PlMsWh.dat",
          "folder names count as part of a picture's name");
    check(portrait_slot_from_name("Young Link black.png", &slot, &kind) && slot == "PlClBk.dat",
          "Young Link is not read as Link");
    check(!portrait_slot_from_name("Fox Blue.png", &slot, &kind), "a color the fighter does not have names no costume");
    check(!portrait_slot_from_name("Fox.png", &slot, &kind), "a fighter with no color names no costume");
    check(!portrait_slot_from_name("Mario and Luigi red.png", &slot, &kind), "two fighters name no costume");
    check(!portrait_slot_from_name("readme.png", &slot, &kind), "an unrelated name names no costume");
  }
  // Which retail texture a costume's picture replaces. The hashes are the retail NTSC 1.02
  // textures at frame column + 30 * costume of the portrait and stock icon animations.
  {
    namespace nc = gx::texpack::native_companions;
    auto one = [](const char* kind, const char* target) {
      auto found = nc::identities(kind, target);
      return found.size() == 1 ? found[0] : nc::Identity{};
    };
    // The portrait table is packed and its last 13 entries are not in column order.
    check(one("csp", "PlCaBu.dat").tex == 0xcbdcafb99d9a68e1ull, "Captain Falcon's blue portrait is its own texture");
    check(one("csp", "PlPrYe.dat").tex == 0x5e1e74f0a5666906ull, "Jigglypuff's fifth portrait is its own texture");
    check(one("csp", "PlCaGr.dat").tex == 0xcafa49cf41691873ull && one("csp", "PlCaNr.dat").tex == 0xd46c9af694a6fe76ull,
          "portraits the table has in column order keep their textures");
    check(one("csp", "PlMsWh.dat").tex == 0x688145fe5c90aabfull && one("csp", "PlKbWh.dat").tex == 0xdf8057c0d6820cbaull &&
              one("csp", "PlYsAq.dat").tex == 0x5bb2be668712dfc1ull,
          "the other portraits past the fourth costume are their own textures");
    // The stock table has 26 columns a costume (Sheik is the last), not the portraits' 25.
    check(one("stock", "PlCaNr.dat").tex == 0xd42ea081a61c4adcull && one("stock", "PlCaGy.dat").tex == 0x1f32ae66d4886dcbull &&
              one("stock", "PlFxGr.dat").tex == 0xf9e2c8c1f7328ccdull && one("stock", "PlCaBu.dat").tex == 0xf655b41f0359df60ull,
          "a stock icon of a later costume is its own texture");
    check(!one("stock", "PlCaGy.dat").needs_tlut, "a stock icon with an image of its own needs no palette to tell it apart");
    const auto young_red = one("stock", "PlClRe.dat"), young_blue = one("stock", "PlClBu.dat");
    check(young_red.tex == young_blue.tex && young_red.needs_tlut && young_blue.needs_tlut &&
              young_red.tlut == 0xc64209e07041e51dull && young_blue.tlut == 0x332d5996bd41fe9cull,
          "two costumes that share a stock image are told apart by palette");
    check(one("stock", "PlSkNr.dat").tex == 0x6973293830927135ull && one("stock", "PlSkNr.dat").tex != one("stock", "PlZdNr.dat").tex &&
              one("csp", "PlSkNr.dat").tex == one("csp", "PlZdNr.dat").tex && one("csp", "PlZdNr.dat").tex != 0,
          "Sheik has her own stock icon and Zelda's portrait");
    check(one("stock", "PlNnYe.dat").tex == one("stock", "PlPpGr.dat").tex && one("csp", "PlNnWh.dat").tex == one("csp", "PlPpRe.dat").tex,
          "Nana's pictures are the Ice Climbers' at the same costume");
    check(nc::identities("csp", "PlGwNr.dat").size() == 4 && nc::identities("stock", "PlGwNr.dat").size() == 4,
          "Mr. Game & Watch's one costume file covers his four selector cells");
    check(nc::identities("csp", "PlFxBu.dat").empty() && nc::identities("preview", "PlFxGr.dat").empty(),
          "an unknown costume or kind has no texture");
    bool distinct = true;
    for (const auto& a : nc::kSlots)
      for (const auto& b : nc::kSlots) {
        if (&a == &b) continue;
        if (a.csp_hash && a.csp_hash == b.csp_hash) distinct = false;
        if (a.stock_hash == b.stock_hash && a.stock_tlut_hash == b.stock_tlut_hash) distinct = false;
      }
    check(distinct && std::size(nc::kSlots) == 123, "every costume has a portrait and a stock identity of its own");

    auto name = nc::parse_texture_name("tex1_136x188_cbdcafb99d9a68e1_0123456789abcdef_9");
    check(name.ok && !name.stock && name.tex == 0xcbdcafb99d9a68e1ull && name.has_tlut && name.tlut == 0x0123456789abcdefull,
          "a portrait's texture name gives its image and palette hashes");
    name = nc::parse_texture_name("tex1_24x24_52300b4c906938c3_332d5996bd41fe9c_8");
    check(name.ok && name.stock && name.tex == young_blue.tex && name.has_tlut && name.tlut == young_blue.tlut,
          "a stock icon's texture name gives its image and palette hashes");
    name = nc::parse_texture_name("tex1_24x24_m_1f32ae66d4886dcb_2817b39e889e22ef_8");
    check(name.ok && name.stock && name.tex == 0x1f32ae66d4886dcbull && name.has_tlut,
          "the mipmapped spelling of a texture name is read too");
    name = nc::parse_texture_name("tex1_24x24_1f32ae66d4886dcb_8");
    check(name.ok && name.stock && !name.has_tlut, "a name with no palette hash still gives the image hash");
    check(!nc::parse_texture_name("tex1_32x32_1f32ae66d4886dcb_8").ok && !nc::parse_texture_name("tex1_24x24_xyz_8").ok,
          "other textures are not portraits or stock icons");
  }
  {
    // The picker lists a fighter's costumes in the order the game cycles through them.
    const auto slots = host::cosmetics::costume_slots();
    const char* falcon[] = {"PlCaNr.dat", "PlCaGy.dat", "PlCaRe.dat", "PlCaWh.dat", "PlCaGr.dat", "PlCaBu.dat"};
    bool in_order = slots.size() >= 6;
    for (size_t i = 0; in_order && i < 6; ++i) in_order = slots[i].target_path == falcon[i];
    check(in_order, "Captain Falcon's costumes are listed in the game's order");
  }
  fs::path picture_folder = folder / L"pictures";
  fs::create_directories(picture_folder, ec);
  host::cosmetics::configure((picture_folder / L"port-settings.ini").string());
  check(host::cosmetics::costume_slots().size() == 124 && host::cosmetics::costume_slots()[0].target_path == "PlCaNr.dat",
        "the slot picker lists every costume of the game");
  fs::path portrait_path = picture_folder / L"anything.png";
  write_file(portrait_path, png(136, 188));
  auto portrait_import = host::cosmetics::import_portrait(portrait_path.string(), "PlFxGr.dat", "csp");
  auto picture_assets = host::cosmetics::assets();
  check(portrait_import.ok && picture_assets.size() == 1 && picture_assets[0].kind == "character_portrait" &&
            picture_assets[0].target_path == "PlFxGr.dat#portrait" && picture_assets[0].selected &&
            picture_assets[0].character == "Fox" && picture_assets[0].costume == "Green",
        "a portrait for a chosen costume becomes its own selected entry");
  check(!host::cosmetics::import_portrait(portrait_path.string(), "PlFxBu.dat", "csp").ok,
        "a costume the game does not have is refused");
  constexpr uint32_t picture_start = 0x00400000;
  auto picture_fst = one_file_fst(picture_start, 0x40, "PlFxGr.dat");
  host::cosmetics::apply_to_fst(picture_fst.data(), (uint32_t)picture_fst.size());
  uint8_t picture_probe[4]{};
  auto pictures_active = host::cosmetics::active_companions();
  check(read_be32(picture_fst.data() + 20) == 0x40 &&
            host::cosmetics::read(picture_start, 0, picture_probe, sizeof picture_probe) == OverrideRead::NotOverridden,
        "a portrait entry replaces no disc file");
  check(pictures_active.size() == 1 && pictures_active[0].kind == "csp" && pictures_active[0].target_path == "PlFxGr.dat" &&
            host::cosmetics::session_profile().active_assets == 1,
        "the portrait reaches the renderer for its costume");
  // The stock icon joins the same entry, here through its file name.
  fs::path stock_path = picture_folder / L"Fox Green stock.png";
  write_file(stock_path, png(24, 24));
  auto stock_import = host::cosmetics::import_file(stock_path.string());
  check(stock_import.ok && stock_import.asset_id == portrait_import.asset_id && host::cosmetics::assets().size() == 1,
        "a stock icon named for the costume joins the costume's entry");
  host::cosmetics::apply_to_fst(picture_fst.data(), (uint32_t)picture_fst.size());
  pictures_active = host::cosmetics::active_companions();
  check(pictures_active.size() == 2, "portrait and stock icon are both active");
  // A skin with its own portrait: the skin's picture shows; the slot's added picture fills in only
  // where a skin brings none.
  fs::path skin_zip = picture_folder / L"skin.zip";
  write_file(skin_zip, stored_zip({{"PlFxGr.dat", dat}, {"skin csp.png", png(136, 188)}}));
  auto skin_import = host::cosmetics::import_file(skin_zip.string());
  check(skin_import.ok && host::cosmetics::select_variant("PlFxGr.dat", skin_import.asset_id, &error),
        "a skin with its own portrait imports beside the costume's portrait entry");
  host::cosmetics::apply_to_fst(picture_fst.data(), (uint32_t)picture_fst.size());
  pictures_active = host::cosmetics::active_companions();
  size_t fox_portraits = 0; bool own_wins = false;
  for (const auto& item : pictures_active)
    if (item.kind == "csp" && item.target_path == "PlFxGr.dat") {
      ++fox_portraits; own_wins = item.path.find("portrait-PlFxGr") != std::string::npos;
    }
  check(fox_portraits == 1 && !own_wins, "a skin that brings its own portrait shows it over the slot's added one");
  check(host::cosmetics::disable_target("PlFxGr.dat#portrait", &error), "the portrait entry can be switched off");
  host::cosmetics::apply_to_fst(picture_fst.data(), (uint32_t)picture_fst.size());
  pictures_active = host::cosmetics::active_companions();
  fox_portraits = 0; own_wins = false;
  for (const auto& item : pictures_active)
    if (item.kind == "csp" && item.target_path == "PlFxGr.dat") {
      ++fox_portraits; own_wins = item.path.find("portrait-PlFxGr") != std::string::npos;
    }
  check(fox_portraits == 1 && !own_wins, "with the portrait entry off the skin's own portrait is used again");
  // A ZIP of pictures and no costume file.
  fs::path pack_path = picture_folder / L"pack.zip";
  write_file(pack_path, stored_zip({{"Fox Orange.png", png(136, 188)}, {"csp/PlMsWh csp.png", png(136, 188)},
                                    {"readme.png", png(8, 8)}}));
  const size_t before_pack = host::cosmetics::assets().size();
  auto pack_import = host::cosmetics::import_file(pack_path.string());
  check(pack_import.ok && host::cosmetics::assets().size() == before_pack + 2 &&
            pack_import.message.find("2 of 3") != std::string::npos,
        "a ZIP of pictures sets each one whose name identifies a costume and reports the rest");
  fs::path nameless = picture_folder / L"cool.png";
  write_file(nameless, png(136, 188));
  check(!host::cosmetics::import_file(nameless.string()).ok, "a picture whose name names no costume is refused with advice");

  // Skins inside a mod disc. The game disc has Fox's default and green costumes; the mod disc has
  // a changed default costume (same skeleton) and the same green one.
  {
    fs::path disc_folder = folder / L"disc-skins";
    fs::create_directories(disc_folder, ec);
    host::cosmetics::configure((disc_folder / L"port-settings.ini").string());
    const auto retail_default = skeleton_dat(0x2, 5.0f, false), retail_green = fox_dat();
    const auto mod_default = skeleton_dat(0x2 | 0x10, 5.0f, false);
    constexpr uint32_t retail_default_at = 0x100, retail_green_at = 0x400;
    g_disc_bytes.assign(0x800, 0);
    std::copy(retail_default.begin(), retail_default.end(), g_disc_bytes.begin() + retail_default_at);
    std::copy(retail_green.begin(), retail_green.end(), g_disc_bytes.begin() + retail_green_at);
    g_disc_table = {{"PlFxNr.dat", {retail_default_at, (uint32_t)retail_default.size()}},
                    {"PlFxGr.dat", {retail_green_at, (uint32_t)retail_green.size()}}};
    constexpr uint32_t fst_at = 0x500, mod_default_at = 0x800, mod_green_at = 0xC00;
    const auto mod_fst = two_file_fst("PlFxNr.dat", mod_default_at, (uint32_t)mod_default.size(),
                                      "PlFxGr.dat", mod_green_at, (uint32_t)retail_green.size());
    std::vector<uint8_t> image(0x1000, 0);
    be32(image, 0x424, fst_at); be32(image, 0x428, (uint32_t)mod_fst.size());
    std::copy(mod_fst.begin(), mod_fst.end(), image.begin() + fst_at);
    std::copy(mod_default.begin(), mod_default.end(), image.begin() + mod_default_at);
    std::copy(retail_green.begin(), retail_green.end(), image.begin() + mod_green_at);
    fs::path iso_path = disc_folder / L"mod.iso";
    write_file(iso_path, image);

    auto scan = host::cosmetics::scan_disc_skins(iso_path.string(), "Test Pack", &error);
    auto listed = host::cosmetics::assets();
    check(scan.ok && !scan.already_present && listed.size() == 1, "one costume of the mod disc differs and is listed");
    check(listed.size() == 1 && listed[0].source == "disc" && listed[0].source_name == "Test Pack" &&
              listed[0].target_path == "PlFxNr.dat" && listed[0].name == "Fox Default: from Test Pack" &&
              !listed[0].selected && listed[0].available && !listed[0].disc_path.empty(),
          "the disc skin is a choice for its costume, named after the pack, and not selected");
    check(host::cosmetics::disc_skin_count(iso_path.string()) == 1, "the disc's skin count is known");
    std::error_code walk;
    size_t dat_copies = 0;
    for (fs::recursive_directory_iterator it(disc_folder / L"CosmeticMods", walk), end; !walk && it != end; it.increment(walk))
      if (it->path().extension() == ".dat") ++dat_copies;
    check(dat_copies == 0, "no copy of the disc's costume is stored");
    auto again = host::cosmetics::scan_disc_skins(iso_path.string(), "Test Pack", &error);
    check(again.ok && again.already_present && host::cosmetics::assets().size() == 1, "an unchanged disc is not scanned twice");
    host::cosmetics::configure((disc_folder / L"port-settings.ini").string());
    listed = host::cosmetics::assets();
    check(listed.size() == 1 && listed[0].source == "disc" && listed[0].available, "the disc skin reloads from the catalog");
    const std::string disc_id = listed.empty() ? std::string() : listed[0].id;
    check(host::cosmetics::select_variant("PlFxNr.dat", disc_id, &error), "the disc skin can be selected");
    auto disc_fst = one_file_fst(retail_default_at, (uint32_t)retail_default.size(), "PlFxNr.dat");
    host::cosmetics::apply_to_fst(disc_fst.data(), (uint32_t)disc_fst.size());
    std::vector<uint8_t> served(mod_default.size());
    check(host::cosmetics::read(retail_default_at, 0, served.data(), (uint32_t)served.size()) ==
              host::cosmetics::OverrideRead::Success && served == mod_default,
          "the disc skin is served from the mod disc's bytes");
    listed = host::cosmetics::assets();
    check(listed.size() == 1 && listed[0].online_allowed && listed[0].online_message == "3 joints match",
          "the disc skin gets the same online verdict as an imported one");
    // The disc is replaced by another file: the entry says so and the costume goes back to standard.
    image[mod_default_at + 0x20 + 0x30] ^= 0x01;
    image.push_back(0);
    write_file(iso_path, image);
    check(host::cosmetics::refresh_catalog(&error), "the catalog refreshes after the disc changed");
    listed = host::cosmetics::assets();
    check(listed.size() == 1 && !listed[0].available && !listed[0].selected &&
              listed[0].availability_message == "disc file missing or changed",
          "a changed disc makes its skin unavailable, with the reason");
    auto gone_fst = one_file_fst(retail_default_at, (uint32_t)retail_default.size(), "PlFxNr.dat");
    host::cosmetics::apply_to_fst(gone_fst.data(), (uint32_t)gone_fst.size());
    check(host::cosmetics::read(retail_default_at, 0, served.data(), 4) == host::cosmetics::OverrideRead::NotOverridden,
          "an unavailable disc skin overrides nothing");
    // Scanning the changed disc brings the entry back under the same identity.
    auto rescan = host::cosmetics::scan_disc_skins(iso_path.string(), "Test Pack", &error);
    listed = host::cosmetics::assets();
    check(rescan.ok && !rescan.already_present && listed.size() == 1 && listed[0].id == disc_id && listed[0].available,
          "a changed disc is scanned again and keeps its entry's identity");
    // A files pack: the same costume as a loose file.
    fs::path pack_folder = disc_folder / L"pack";
    fs::create_directories(pack_folder, ec);
    write_file(pack_folder / L"PlFxNr.dat", mod_default);
    write_file(pack_folder / L"PlFxGr.dat", retail_green);
    write_file(pack_folder / L"PlFxAJ.dat", mod_default);
    auto folder_scan = host::cosmetics::scan_disc_skins(pack_folder.string(), "My Files", &error);
    check(folder_scan.ok && host::cosmetics::disc_skin_count(pack_folder.string()) == 1 &&
              host::cosmetics::assets().size() == 2,
          "a files pack lists its changed costume and nothing else");
    // The mod disc is deleted: its entry leaves the list at the next scan of anything.
    fs::remove(iso_path, ec);
    host::cosmetics::scan_disc_skins(pack_folder.string(), "My Files", &error);
    check(host::cosmetics::assets().size() == 1 && host::cosmetics::assets()[0].source_name == "My Files",
          "entries of a disc that no longer exists are dropped");
    g_disc_table.clear(); g_disc_bytes.clear();
  }

  // A pack's alternate costumes: PlFxNr.lat and PlFxNr.rat beside PlFxNr.dat, the way the 20XX
  // Hack Pack keeps its L and R sets. Each differing one is its own choice for the slot.
  {
    fs::path alt_folder = folder / L"alt-skins";
    fs::create_directories(alt_folder, ec);
    host::cosmetics::configure((alt_folder / L"port-settings.ini").string());
    const auto retail_default = skeleton_dat(0x2, 5.0f, false);
    const auto alt_l = skeleton_dat(0x2 | 0x10, 5.0f, false);   // drawing flag only: same skeleton
    const auto alt_r = retail_default;                            // identical to the game's: not a skin
    const auto moved = skeleton_dat(0x2, 5.5f, false);           // a moved bone: off online
    constexpr uint32_t retail_default_at = 0x100;
    g_disc_bytes.assign(0x400, 0);
    std::copy(retail_default.begin(), retail_default.end(), g_disc_bytes.begin() + retail_default_at);
    g_disc_table = {{"PlFxNr.dat", {retail_default_at, (uint32_t)retail_default.size()}}};
    // Four root files: the plain costume (a moved bone), the L and R alternates, and an animation
    // bank named like a costume, which is never a skin.
    struct Member { const char* name; uint32_t at; const std::vector<uint8_t>* bytes; };
    const Member members[] = {{"PlFxNr.dat", 0x800, &moved}, {"PlFxNr.lat", 0xC00, &alt_l},
                              {"PlFxNr.rat", 0x1000, &alt_r}, {"PlFxAJ.dat", 0x1400, &alt_l}};
    std::vector<uint8_t> fst(12 * 5, 0);
    be32(fst, 0, 0x01000000); be32(fst, 8, 5);
    std::string names;
    for (size_t i = 0; i < 4; ++i) {
      be32(fst, 12 * (i + 1), (uint32_t)names.size());
      be32(fst, 12 * (i + 1) + 4, members[i].at);
      be32(fst, 12 * (i + 1) + 8, (uint32_t)members[i].bytes->size());
      names += members[i].name; names.push_back('\0');
    }
    fst.insert(fst.end(), names.begin(), names.end());
    constexpr uint32_t fst_at = 0x500;
    std::vector<uint8_t> image(0x1800, 0);
    be32(image, 0x424, fst_at); be32(image, 0x428, (uint32_t)fst.size());
    std::copy(fst.begin(), fst.end(), image.begin() + fst_at);
    for (const auto& member : members) std::copy(member.bytes->begin(), member.bytes->end(), image.begin() + member.at);
    fs::path iso_path = alt_folder / L"hack.iso";
    write_file(iso_path, image);

    auto scan = host::cosmetics::scan_disc_skins(iso_path.string(), "Hack disc", &error);
    auto listed = host::cosmetics::assets();
    check(scan.ok && listed.size() == 2, "the plain costume and the L alternate are listed; the identical R one is not");
    const host::cosmetics::AssetInfo* plain = nullptr; const host::cosmetics::AssetInfo* left = nullptr;
    for (const auto& item : listed) (item.variant.empty() ? plain : left) = &item;
    check(plain && left && plain->id != left->id && plain->target_path == "PlFxNr.dat" && left->target_path == "PlFxNr.dat",
          "both are choices for the same slot with different identities");
    check(plain && plain->name == "Fox Default: from Hack disc" && left && left->name == "Fox Default: from Hack disc (alt L)" &&
              left->variant == "alt L",
          "the alternate is labelled with its set");
    check(plain && !plain->online_allowed && plain->online_message == "bone 1: rest pose differs (moved)" &&
              left && left->online_allowed && left->online_message == "3 joints match",
          "the online verdict is known from the scan, before any skin is applied");
    // The pack view and its set buttons.
    auto packs = host::cosmetics::packs();
    check(packs.size() == 1 && packs[0].name == "Hack disc" && packs[0].skins == 2 && packs[0].slots == 1 &&
              packs[0].variants == std::vector<std::string>{"", "alt L"} && packs[0].online_on == 1 && packs[0].selected == 0,
          "the pack lists its sets, slots and online count");
    // An imported skin on the same slot, selected: the pack's set replaces it after saying so.
    fs::path import_path = alt_folder / L"mine.dat";
    write_file(import_path, skeleton_dat(0x2 | 0x40, 5.0f, false));
    auto mine = host::cosmetics::import_file(import_path.string());
    check(mine.ok && host::cosmetics::select_variant("PlFxNr.dat", mine.asset_id, &error), "an imported skin holds the slot");
    packs = host::cosmetics::packs();
    check(packs.size() == 2 && packs[0].name == "Imported skins" && packs[0].selected == 1 && packs[0].online_unchecked == 1,
          "imports are a pack of their own, listed first, with the verdict still to come");
    auto preview = host::cosmetics::apply_pack_set(packs[1].key, "alt L", true);
    check(preview.ok && preview.set == 1 && preview.replaced == 1 && preview.replaced_from == "Imported skins" &&
              host::cosmetics::assets().size() == 3,
          "the preview counts the replaced skin and changes nothing");
    bool still_mine = false;
    for (const auto& item : host::cosmetics::assets()) if (item.id == mine.asset_id) still_mine = item.selected;
    check(still_mine, "a preview leaves the selection alone");
    auto applied = host::cosmetics::apply_pack_set(packs[1].key, "alt L", false);
    bool left_selected = false;
    for (const auto& item : host::cosmetics::assets()) if (item.id == left->id) left_selected = item.selected;
    check(applied.ok && applied.set == 1 && applied.replaced == 1 && left_selected, "the set is applied in one change");
    auto disc_fst = one_file_fst(retail_default_at, (uint32_t)retail_default.size(), "PlFxNr.dat");
    host::cosmetics::apply_to_fst(disc_fst.data(), (uint32_t)disc_fst.size());
    std::vector<uint8_t> served(alt_l.size());
    check(host::cosmetics::read(retail_default_at, 0, served.data(), (uint32_t)served.size()) == OverrideRead::Success &&
              served == alt_l,
          "the slot serves the alternate's own bytes");
    auto none = host::cosmetics::apply_pack_set(packs[1].key, "none", false);
    bool any_selected = false;
    for (const auto& item : host::cosmetics::assets()) any_selected |= item.selected;
    check(none.ok && none.cleared == 1 && !any_selected, "the pack's picks go back to the standard costume");
    check(!host::cosmetics::apply_pack_set(packs[1].key, "alt R", true).ok, "a set the pack does not have is refused");
    // A disc scanned under the old rules is scanned again, so players get the alternates without doing anything.
    {
      const fs::path state = alt_folder / L"CosmeticMods" / L"state.json";
      std::ifstream in(state, std::ios::binary);
      std::string text((std::istreambuf_iterator<char>(in)), {});
      in.close();
      const size_t at = text.find("\"rules\": 5");
      check(at != std::string::npos, "the scan records the rules it used");
      if (at != std::string::npos) text.replace(at, 10, "\"rules\": 4");
      std::ofstream out(state, std::ios::binary); out << text;
    }
    host::cosmetics::configure((alt_folder / L"port-settings.ini").string());
    auto rescan = host::cosmetics::scan_disc_skins(iso_path.string(), "Hack disc", &error);
    check(rescan.ok && !rescan.already_present, "a disc scanned under older rules is scanned again");
    check(host::cosmetics::scan_disc_skins(iso_path.string(), "Hack disc", &error).already_present,
          "and then not a third time");
    // A files pack with an alternate.
    fs::path pack_folder = alt_folder / L"pack";
    fs::create_directories(pack_folder, ec);
    write_file(pack_folder / L"PlFxNr.lat", alt_l);
    auto folder_scan = host::cosmetics::scan_disc_skins(pack_folder.string(), "Loose", &error);
    bool loose_alt = false;
    for (const auto& item : host::cosmetics::assets()) loose_alt |= item.source_name == "Loose" && item.variant == "alt L";
    check(folder_scan.ok && loose_alt, "a loose .lat file is an alt L skin too");
    g_disc_table.clear(); g_disc_bytes.clear();
  }

  // Character select L / R: a skin is picked and published while the game runs, with no restart.
  {
    using host::cosmetics::OverrideRead;
    host::cosmetics::thaw_after_online_session();
    host::cosmetics::set_online_probe(nullptr);
    fs::path live_folder = folder / L"live-skins";
    fs::create_directories(live_folder, ec);
    host::cosmetics::configure((live_folder / L"port-settings.ini").string());
    const auto retail_default = skeleton_dat(0x2, 5.0f, false);
    const auto alt_l = skeleton_dat(0x2 | 0x10, 5.0f, false);   // drawing flag only: stays on online
    const auto moved = skeleton_dat(0x2, 5.5f, false);           // a moved bone: off online
    const auto longer = skeleton_dat(0x2, 5.0f, true);           // one more joint: off online, and a longer file
    constexpr uint32_t retail_default_at = 0x100;
    g_disc_bytes.assign(0x400, 0);
    std::copy(retail_default.begin(), retail_default.end(), g_disc_bytes.begin() + retail_default_at);
    g_disc_table = {{"PlFxNr.dat", {retail_default_at, (uint32_t)retail_default.size()}}};
    constexpr uint32_t fst_at = 0x500, moved_at = 0x800, alt_at = 0xC00;
    const auto pack_fst = two_file_fst("PlFxNr.dat", moved_at, (uint32_t)moved.size(),
                                       "PlFxNr.lat", alt_at, (uint32_t)alt_l.size());
    std::vector<uint8_t> image(0x1000, 0);
    be32(image, 0x424, fst_at); be32(image, 0x428, (uint32_t)pack_fst.size());
    std::copy(pack_fst.begin(), pack_fst.end(), image.begin() + fst_at);
    std::copy(moved.begin(), moved.end(), image.begin() + moved_at);
    std::copy(alt_l.begin(), alt_l.end(), image.begin() + alt_at);
    fs::path iso_path = live_folder / L"pack.iso";
    write_file(iso_path, image);
    auto scan = host::cosmetics::scan_disc_skins(iso_path.string(), "Pack", &error);
    fs::path import_path = live_folder / L"mine.dat";
    write_file(import_path, longer);
    auto mine = host::cosmetics::import_file(import_path.string());
    std::string left_id, moved_id;
    for (const auto& item : host::cosmetics::assets()) {
      if (item.source == "disc" && item.variant == "alt L") left_id = item.id;
      if (item.source == "disc" && item.variant.empty()) moved_id = item.id;
    }
    check(scan.ok && mine.ok && !left_id.empty() && !moved_id.empty() && longer.size() > retail_default.size(),
          "the slot has three skins: two from a pack and an import");

    // The game starts with the standard costume.
    auto fst = one_file_fst(retail_default_at, (uint32_t)retail_default.size(), "PlFxNr.dat");
    host::cosmetics::apply_to_fst(fst.data(), (uint32_t)fst.size());
    std::vector<uint8_t> served(longer.size());
    check(host::cosmetics::read(retail_default_at, 0, served.data(), 4) == OverrideRead::NotOverridden,
          "nothing is overridden before a skin is picked");

    // A live pick: saved, no restart asked, and served after the slot is published again.
    check(host::cosmetics::select_variant_live("PlFxNr.dat", left_id, &error), "a skin can be picked live");
    check(host::cosmetics::last_message().find("restart") == std::string::npos, "a live pick asks for no restart");
    auto published = host::cosmetics::republish_slot(fst.data(), (uint32_t)fst.size(), "PlFxNr.dat");
    check(published.ok && published.files.size() == 1 && published.files[0].fst_index == 1 &&
              published.files[0].vanilla_start == retail_default_at && published.files[0].overridden &&
              published.files[0].online_allowed && published.files[0].asset_id == left_id &&
              published.files[0].length == alt_l.size(),
          "publishing the slot again reports its file as served by the picked skin");
    served.assign(alt_l.size(), 0);
    check(host::cosmetics::read(retail_default_at, 0, served.data(), (uint32_t)served.size()) == OverrideRead::Success &&
              served == alt_l && host::cosmetics::applied_asset(retail_default_at) == left_id,
          "the slot serves the picked skin's bytes");
    check(!host::cosmetics::pending_restart(), "nothing waits for a restart after a live pick");

    // Another skin, longer than the disc's file: the bytes switch and the table gets the new length.
    check(host::cosmetics::select_variant_live("PlFxNr.dat", mine.asset_id, &error), "another skin can be picked live");
    published = host::cosmetics::republish_slot(fst.data(), (uint32_t)fst.size(), "PlFxNr.dat");
    served.assign(longer.size(), 0);
    check(published.ok && published.files.size() == 1 && published.files[0].asset_id == mine.asset_id &&
              published.files[0].length == longer.size() && read_be32(fst.data() + 20) == longer.size() &&
              host::cosmetics::read(retail_default_at, 0, served.data(), (uint32_t)served.size()) == OverrideRead::Success &&
              served == longer,
          "the served bytes switch to the new skin and the table carries its length");
    // It is not proven to change looks alone: the caller gives it the alias entry, and online the
    // disc's own costume is what the slot serves.
    check(published.files.size() == 1 && !published.files[0].online_allowed && !host::cosmetics::online_allowed(retail_default_at),
          "a skin with another skeleton is reported as off online, for the caller's alias entry");
    host::cosmetics::set_online_probe([] { return true; });
    served.assign(retail_default.size(), 0);
    check(host::cosmetics::read(retail_default_at, 0, served.data(), (uint32_t)served.size()) == OverrideRead::Success &&
              std::equal(served.begin() + 4, served.end(), retail_default.begin() + 4),
          "online the slot serves the disc's own costume");

    // The online rule for live picks: only a skin proven to change looks alone, or the standard costume.
    check(!host::cosmetics::select_variant_live("PlFxNr.dat", moved_id, &error),
          "online, a pack skin scanned as off online cannot be picked");
    check(host::cosmetics::select_variant_live("PlFxNr.dat", left_id, &error),
          "online, a skin that stays on online can be picked");
    check(!host::cosmetics::select_variant_live("PlFxNr.dat", mine.asset_id, &error),
          "online, an import with another skeleton cannot be picked");
    check(host::cosmetics::select_variant_live("PlFxNr.dat", "", &error), "online, the standard costume can be picked");
    host::cosmetics::freeze_for_online_session();
    check(!host::cosmetics::select_variant_live("PlFxNr.dat", left_id, &error),
          "no live pick while an online match is queued or running");
    host::cosmetics::thaw_after_online_session();

    // Back to the standard costume: the disc's file and its length again.
    published = host::cosmetics::republish_slot(fst.data(), (uint32_t)fst.size(), "PlFxNr.dat");
    check(published.ok && published.files.size() == 1 && !published.files[0].overridden &&
              published.files[0].length == retail_default.size() && read_be32(fst.data() + 20) == retail_default.size() &&
              host::cosmetics::read(retail_default_at, 0, served.data(), 4) == OverrideRead::NotOverridden,
          "the standard costume puts the disc's file and length back");

    // Cycling online steps over the skins that are off online: standard, the alt L skin, standard.
    auto step = host::cosmetics::cycle_slot_live("PlFxNr.dat", 1);
    check(step.ok && step.changed && step.asset_id == left_id && step.previous_id.empty() && step.name == "Pack (alt L)",
          "online, L / R goes from the standard costume to the one skin that stays on");
    step = host::cosmetics::cycle_slot_live("PlFxNr.dat", 1);
    check(step.ok && step.changed && step.asset_id.empty() && step.previous_id == left_id && step.name == "Standard",
          "and then back to the standard costume");
    // Offline every skin is a step, and both directions come back around.
    host::cosmetics::set_online_probe(nullptr);
    std::map<std::string, bool> seen;
    for (int i = 0; i < 4; ++i) {
      step = host::cosmetics::cycle_slot_live("PlFxNr.dat", 1);
      check(step.ok && step.changed, "offline, every press picks another choice");
      seen[step.asset_id] = true;
    }
    check(seen.size() == 4 && step.asset_id.empty(), "four presses visit the three skins and return to the standard costume");
    step = host::cosmetics::cycle_slot_live("PlFxNr.dat", -1);
    const std::string last = step.asset_id;
    step = host::cosmetics::cycle_slot_live("PlFxNr.dat", 1);
    check(!last.empty() && step.asset_id.empty(), "the other direction steps back the same way");
    check(!host::cosmetics::cycle_slot_live("PlFxGr.dat", 1).changed, "a costume with no skins has nothing to cycle");

    // The character select's fighter numbers and costume order.
    check(host::cosmetics::costume_slot_file(2, 0) == "PlFxNr.dat" && host::cosmetics::costume_slot_file(2, 3) == "PlFxGr.dat" &&
              host::cosmetics::costume_slot_file(2, 4).empty() && host::cosmetics::costume_slot_file(14, 1) == "PlPpGr.dat" &&
              host::cosmetics::costume_slot_file(0, 1) == "PlCaGy.dat" && host::cosmetics::costume_slot_file(26, 0).empty(),
          "a fighter number and costume index name the costume's file");
    g_disc_table.clear(); g_disc_bytes.clear();
  }

  // Character select L / R on the Ice Climbers: the screen names Popo's slot, and Nana's follows
  // where the same pack and set has a skin for her.
  {
    using host::cosmetics::OverrideRead;
    host::cosmetics::thaw_after_online_session();
    host::cosmetics::set_online_probe(nullptr);
    fs::path pair_folder = folder / L"climbers";
    fs::create_directories(pair_folder, ec);
    host::cosmetics::configure((pair_folder / L"port-settings.ini").string());
    const auto popo_retail = costume_dat("PlyPopo5K"), nana_retail = costume_dat("PlyNana5K");
    const auto popo_l = costume_dat("PlyPopo5K", false), nana_l = costume_dat("PlyNana5K", false);
    const auto popo_r = rooted_dat({"PlyPopo5K_Share_joint", "PlyPopo5K_Share_matanim_joint", "extra"});
    const auto nana_own = rooted_dat({"PlyNana5K_Share_joint", "own"});
    constexpr uint32_t popo_at = 0x100, nana_at = 0x200;
    g_disc_bytes.assign(0x400, 0);
    std::copy(popo_retail.begin(), popo_retail.end(), g_disc_bytes.begin() + popo_at);
    std::copy(nana_retail.begin(), nana_retail.end(), g_disc_bytes.begin() + nana_at);
    g_disc_table = {{"PlPpNr.dat", {popo_at, (uint32_t)popo_retail.size()}},
                    {"PlNnNr.dat", {nana_at, (uint32_t)nana_retail.size()}}};
    // The pack has an L set for both climbers and an R set for Popo alone.
    fs::path pack_folder = pair_folder / L"pack";
    fs::create_directories(pack_folder, ec);
    write_file(pack_folder / L"PlPpNr.lat", popo_l);
    write_file(pack_folder / L"PlNnNr.lat", nana_l);
    write_file(pack_folder / L"PlPpNr.rat", popo_r);
    auto scan = host::cosmetics::scan_disc_skins(pack_folder.string(), "Pair", &error);
    std::string popo_l_id, popo_r_id, nana_l_id;
    for (const auto& item : host::cosmetics::assets()) {
      if (item.target_path == "PlPpNr.dat" && item.variant == "alt L") popo_l_id = item.id;
      if (item.target_path == "PlPpNr.dat" && item.variant == "alt R") popo_r_id = item.id;
      if (item.target_path == "PlNnNr.dat" && item.variant == "alt L") nana_l_id = item.id;
    }
    fs::path own_path = pair_folder / L"nana-own.dat";
    write_file(own_path, nana_own);
    auto own = host::cosmetics::import_file(own_path.string());
    check(scan.ok && own.ok && !popo_l_id.empty() && !popo_r_id.empty() && !nana_l_id.empty(),
          "the pack has an L set for both climbers, an R set for Popo, and Nana has an import of her own");
    auto fst = two_file_fst("PlPpNr.dat", popo_at, (uint32_t)popo_retail.size(),
                            "PlNnNr.dat", nana_at, (uint32_t)nana_retail.size());
    host::cosmetics::apply_to_fst(fst.data(), (uint32_t)fst.size());
    auto selected = [](const std::string& id) {
      for (const auto& item : host::cosmetics::assets()) if (item.id == id) return item.selected;
      return false;
    };

    auto step = host::cosmetics::cycle_slot_live("PlPpNr.dat", 1);
    check(step.ok && step.changed && step.asset_id == popo_l_id && step.partner_slot == "PlNnNr.dat" &&
              step.partner_previous_id.empty() && selected(nana_l_id),
          "Popo's L skin brings Nana's L skin with it");
    auto popo_published = host::cosmetics::republish_slot(fst.data(), (uint32_t)fst.size(), "PlPpNr.dat");
    auto nana_published = host::cosmetics::republish_slot(fst.data(), (uint32_t)fst.size(), step.partner_slot);
    // These fixture costumes have no skeleton, so they are off online: the served file is padded to
    // the disc file's length and its own length field (the first four bytes) says that length.
    std::vector<uint8_t> served(nana_l.size());
    check(popo_published.ok && nana_published.ok && nana_published.files.size() == 1 &&
              nana_published.files[0].asset_id == nana_l_id &&
              nana_published.files[0].length == std::max(nana_l.size(), nana_retail.size()) &&
              host::cosmetics::read(nana_at, 0, served.data(), (uint32_t)served.size()) == OverrideRead::Success &&
              std::equal(served.begin() + 4, served.end(), nana_l.begin() + 4) &&
              host::cosmetics::applied_asset(popo_at) == popo_l_id,
          "both slots serve the pair's skins once they are published again");
    check(!host::cosmetics::pending_restart(), "the pair leaves nothing waiting for a restart");

    step = host::cosmetics::cycle_slot_live("PlPpNr.dat", 1);
    check(step.changed && step.asset_id == popo_r_id && step.partner_slot == "PlNnNr.dat" &&
              step.partner_previous_id == nana_l_id && !selected(nana_l_id),
          "a set with no skin for Nana puts her back in the standard costume, not in the old pair's");
    step = host::cosmetics::cycle_slot_live("PlPpNr.dat", 1);
    check(step.changed && step.asset_id.empty() && step.partner_slot.empty(),
          "back to the standard costume: Nana is already there and is not touched");

    // A pick of her own is left alone, whatever Popo wears.
    check(host::cosmetics::select_variant_live("PlNnNr.dat", own.asset_id, &error), "Nana can have a skin of her own");
    step = host::cosmetics::cycle_slot_live("PlPpNr.dat", 1);
    check(step.changed && step.asset_id == popo_l_id && step.partner_slot.empty() && selected(own.asset_id) &&
              !selected(nana_l_id),
          "Nana's own pick stays when Popo's skin changes");
    step = host::cosmetics::cycle_slot_live("PlNnNr.dat", 1);
    check(step.changed && step.partner_slot.empty(), "Nana's slot leads no pair");

    // Online, a partner skin that is off online stays out: Popo changes alone.
    check(host::cosmetics::select_variant_live("PlNnNr.dat", "", &error) &&
              host::cosmetics::select_variant_live("PlPpNr.dat", "", &error),
          "both climbers back in the standard costume");
    bool popo_l_online = false, nana_l_online = false;
    for (const auto& item : host::cosmetics::assets()) {
      if (item.id == popo_l_id) popo_l_online = item.online_allowed;
      if (item.id == nana_l_id) nana_l_online = item.online_allowed;
    }
    host::cosmetics::set_online_probe([] { return true; });
    step = host::cosmetics::cycle_slot_live("PlPpNr.dat", 1);
    check(!step.changed || step.partner_slot.empty() || (popo_l_online && nana_l_online),
          "online, Nana follows only with a skin that stays on online");
    check(selected(nana_l_id) == (step.changed && step.asset_id == popo_l_id && nana_l_online),
          "online, the pair's saved picks match what was allowed");
    host::cosmetics::set_online_probe(nullptr);
    g_disc_table.clear(); g_disc_bytes.clear();
  }

  // The session's own pack (the 20XX Hack Pack as the Source Port's overlay): its plain costume is
  // the standard one already, so L / R goes standard, its L set, its R set, then the other skins.
  {
    host::cosmetics::thaw_after_online_session();
    host::cosmetics::set_online_probe(nullptr);
    fs::path session_folder = folder / L"session-pack";
    fs::create_directories(session_folder, ec);
    host::cosmetics::configure((session_folder / L"port-settings.ini").string());
    const auto retail_default = skeleton_dat(0x2, 5.0f, false);
    const auto plain = skeleton_dat(0x2 | 0x40000, 5.0f, false);   // the pack's own plain costume
    const auto alt_l = skeleton_dat(0x2 | 0x10, 5.0f, false);      // drawing flag only: stays on online
    const auto alt_r = skeleton_dat(0x2, 5.5f, false);             // a moved bone: off online
    const auto longer = skeleton_dat(0x2, 5.0f, true);
    constexpr uint32_t retail_default_at = 0x100;
    g_disc_bytes.assign(0x400, 0);
    std::copy(retail_default.begin(), retail_default.end(), g_disc_bytes.begin() + retail_default_at);
    g_disc_table = {{"PlFxNr.dat", {retail_default_at, (uint32_t)retail_default.size()}}};
    fs::path pack_folder = session_folder / L"pack";
    fs::create_directories(pack_folder, ec);
    write_file(pack_folder / L"PlFxNr.dat", plain);
    write_file(pack_folder / L"PlFxNr.lat", alt_l);
    write_file(pack_folder / L"PlFxNr.rat", alt_r);
    auto scan = host::cosmetics::scan_disc_skins(pack_folder.string(), "20XX", &error);
    fs::path import_path = session_folder / L"mine.dat";
    write_file(import_path, longer);
    auto mine = host::cosmetics::import_file(import_path.string());
    std::string plain_id, left_id, right_id;
    for (const auto& item : host::cosmetics::assets()) {
      if (item.source != "disc" || item.target_path != "PlFxNr.dat") continue;
      if (item.variant.empty()) plain_id = item.id;
      if (item.variant == "alt L") left_id = item.id;
      if (item.variant == "alt R") right_id = item.id;
    }
    check(scan.ok && mine.ok && !plain_id.empty() && !left_id.empty() && !right_id.empty(),
          "the pack lists its plain costume and its L and R sets as skins of the slot");
    auto fst = one_file_fst(retail_default_at, (uint32_t)retail_default.size(), "PlFxNr.dat");
    host::cosmetics::apply_to_fst(fst.data(), (uint32_t)fst.size());

    // No session pack: the pack is any other disc, and its plain costume is a step too.
    check(!host::cosmetics::session_pack_skin(left_id) && !host::cosmetics::session_pack_skin(plain_id),
          "with no session pack no skin is the session pack's");
    std::map<std::string, bool> seen;
    for (int i = 0; i < 5; ++i) seen[host::cosmetics::cycle_slot_live("PlFxNr.dat", 1).asset_id] = true;
    check(seen.size() == 5 && seen.count(plain_id) && host::cosmetics::cycle_slot_live("PlFxNr.dat", 1).changed,
          "with no session pack, five presses visit the standard costume, the pack's three and the import");
    check(host::cosmetics::select_variant_live("PlFxNr.dat", "", &error), "back to the standard costume");

    // The pack is the session's disc.
    host::cosmetics::set_session_pack(pack_folder.string());
    check(host::cosmetics::session_pack_skin(plain_id) && host::cosmetics::session_pack_skin(left_id) &&
              host::cosmetics::session_pack_skin(right_id) && !host::cosmetics::session_pack_skin(mine.asset_id) &&
              !host::cosmetics::session_pack_skin(""),
          "the session pack's skins are known as its own, the import and the standard costume are not");
    auto step = host::cosmetics::cycle_slot_live("PlFxNr.dat", 1);
    check(step.changed && step.asset_id == left_id && step.previous_id.empty() && step.name == "20XX (alt L)",
          "the first press goes from the standard costume to the pack's L set");
    auto published = host::cosmetics::republish_slot(fst.data(), (uint32_t)fst.size(), "PlFxNr.dat");
    std::vector<uint8_t> served(alt_l.size());
    check(published.ok && published.files.size() == 1 && published.files[0].overridden &&
              published.files[0].asset_id == left_id && published.files[0].online_allowed &&
              host::cosmetics::read(retail_default_at, 0, served.data(), (uint32_t)served.size()) ==
                  host::cosmetics::OverrideRead::Success && served == alt_l,
          "the slot serves the pack's L costume once it is published again");
    step = host::cosmetics::cycle_slot_live("PlFxNr.dat", 1);
    check(step.changed && step.asset_id == right_id && step.name == "20XX (alt R)", "then its R set");
    published = host::cosmetics::republish_slot(fst.data(), (uint32_t)fst.size(), "PlFxNr.dat");
    check(published.ok && published.files.size() == 1 && published.files[0].asset_id == right_id &&
              !published.files[0].online_allowed,
          "a set with another skeleton is reported as off online, for the caller's alias entry");
    step = host::cosmetics::cycle_slot_live("PlFxNr.dat", 1);
    check(step.changed && step.asset_id == mine.asset_id, "then the other skins of the slot");
    step = host::cosmetics::cycle_slot_live("PlFxNr.dat", 1);
    check(step.changed && step.asset_id.empty() && step.name == "Standard",
          "and back to the standard costume, never through the pack's plain costume");
    step = host::cosmetics::cycle_slot_live("PlFxNr.dat", -1);
    check(step.changed && step.asset_id == mine.asset_id, "the other direction steps back the same way");
    check(host::cosmetics::select_variant_live("PlFxNr.dat", "", &error), "back to the standard costume again");

    // Online the skeleton rule decides, as for every skin: only the L set stays a step.
    host::cosmetics::set_online_probe([] { return true; });
    step = host::cosmetics::cycle_slot_live("PlFxNr.dat", 1);
    check(step.changed && step.asset_id == left_id, "online, the pack's L set (same skeleton) is a step");
    step = host::cosmetics::cycle_slot_live("PlFxNr.dat", 1);
    check(step.changed && step.asset_id.empty(), "online, the R set and the import are stepped over");
    host::cosmetics::set_online_probe(nullptr);

    host::cosmetics::set_session_pack("");
    check(!host::cosmetics::session_pack_skin(left_id), "clearing the session pack forgets its skins");
    g_disc_table.clear(); g_disc_bytes.clear();
  }

  // A pack's own portraits: its character select file keeps one picture per costume, shown through
  // the keys of a texture animation. A plain costume of the pack gets the picture of its slot when
  // it differs from the game's; an alternate set gets none.
  {
    host::cosmetics::thaw_after_online_session();
    host::cosmetics::set_online_probe(nullptr);
    fs::path portrait_folder = folder / L"pack-portraits";
    fs::create_directories(portrait_folder, ec);
    host::cosmetics::configure((portrait_folder / L"port-settings.ini").string());
    // Twenty 136x188 pictures of palette indices. Frame f of the animation shows picture 19 - f, so
    // Captain Falcon's standard costume (frame 0) is picture 19 and Fox's (frame 2) is picture 17.
    // Every picture's palette is opaque blues, except the ones given another color here.
    auto select_screen = [](uint16_t fox_color, uint16_t falcon_color) {
      constexpr uint32_t pictures = 20, data_size = 0x80000;
      std::vector<uint8_t> out(0x20 + data_size + 12, 0);
      be32(out, 0, (uint32_t)out.size()); be32(out, 4, data_size); be32(out, 8, 3);
      auto at = [](uint32_t offset) { return (size_t)0x20 + offset; };
      be32(out, at(0x08), 0x20); be32(out, at(0x0C), 0x100); be32(out, at(0x10), 0x180);
      out[at(0x15)] = pictures; out[at(0x17)] = pictures;
      be32(out, at(0x28), 0x30);       // the animation's first track
      be32(out, at(0x34), 42);         // 42 bytes of keys
      out[at(0x3C)] = 1;               // the image index track
      out[at(0x3D)] = 0x80;            // values are unsigned bytes
      be32(out, at(0x40), 0x50);
      out[at(0x50)] = 0xB1; out[at(0x51)] = 2;   // twenty constant keys
      for (uint32_t frame = 0; frame < pictures; ++frame) {
        out[at(0x52 + 2 * frame)] = (uint8_t)(pictures - 1 - frame);
        out[at(0x53 + 2 * frame)] = 1;           // one frame to the next key
      }
      for (uint32_t i = 0; i < pictures; ++i) {
        const uint32_t image = 0x200 + i * 0x18, palette = 0x400 + i * 0x10, colors = 0x600 + i * 0x200,
                       pixels = 0x3000 + i * 0x6400;
        be32(out, at(0x100 + 4 * i), image); be32(out, at(0x180 + 4 * i), palette);
        be32(out, at(image), pixels); out[at(image + 5)] = 136; out[at(image + 7)] = 188; be32(out, at(image + 8), 9);
        be32(out, at(palette), colors); be32(out, at(palette + 4), 2); out[at(palette + 12)] = 1;   // 256 colors
        const uint16_t special = i == 17 ? fox_color : i == 19 ? falcon_color : 0;
        for (uint32_t k = 0; k < 256; ++k) {
          const uint16_t color = special ? special : (uint16_t)(0x8000 | (k & 31));
          out[at(colors + 2 * k)] = (uint8_t)(color >> 8); out[at(colors + 2 * k + 1)] = (uint8_t)color;
        }
        std::fill(out.begin() + at(pixels), out.begin() + at(pixels) + 17 * 47 * 32, (uint8_t)i);
      }
      be32(out, 0x20 + data_size, 0x0C); be32(out, 0x20 + data_size + 4, 0x10); be32(out, 0x20 + data_size + 8, 0x08);
      return out;
    };
    const auto retail_default = skeleton_dat(0x2, 5.0f, false);
    const auto plain = skeleton_dat(0x2 | 0x10, 5.0f, false);
    const auto alt_l = skeleton_dat(0x2 | 0x20, 5.0f, false);
    const auto retail_screen = select_screen(0, 0);
    constexpr uint32_t costume_at = 0x100, screen_at = 0x400;
    g_disc_bytes.assign(screen_at + retail_screen.size(), 0);
    std::copy(retail_default.begin(), retail_default.end(), g_disc_bytes.begin() + costume_at);
    std::copy(retail_screen.begin(), retail_screen.end(), g_disc_bytes.begin() + screen_at);
    g_disc_table = {{"PlFxNr.dat", {costume_at, (uint32_t)retail_default.size()}},
                    {"MnSlChr.usd", {screen_at, (uint32_t)retail_screen.size()}}};

    // Fox's picture in this pack is opaque red.
    fs::path pack_folder = portrait_folder / L"pack";
    fs::create_directories(pack_folder, ec);
    write_file(pack_folder / L"PlFxNr.dat", plain);
    write_file(pack_folder / L"PlFxNr.lat", alt_l);
    write_file(pack_folder / L"MnSlChr.usd", select_screen(0xFC00, 0));
    auto scan = host::cosmetics::scan_disc_skins(pack_folder.string(), "Pictures", &error);
    std::string plain_id, alt_id, plain_picture, alt_picture;
    for (const auto& item : host::cosmetics::assets()) {
      if (item.variant.empty()) { plain_id = item.id; plain_picture = item.preview_path; }
      else { alt_id = item.id; alt_picture = item.preview_path; }
    }
    check(scan.ok && !plain_id.empty() && !alt_id.empty(), "the pack's plain costume and its L alternate are listed");
    check(!plain_picture.empty() && alt_picture.empty(),
          "the plain costume gets the pack's portrait of its slot; the alternate, with no cell of its own, gets none");
    const auto picture = read_file(fs::path(plain_picture));
    static constexpr uint8_t png_signature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    check(picture.size() > 57 && !std::memcmp(picture.data(), png_signature, 8) && !std::memcmp(picture.data() + 12, "IHDR", 4) &&
              read_be32(picture.data() + 16) == 136 && read_be32(picture.data() + 20) == 188 && picture[24] == 8 && picture[25] == 6,
          "the portrait is stored as a 136x188 RGBA PNG");
    // signature 8, header chunk 25, data chunk header 8, stream header 2, block header 5, row filter 1
    check(picture.size() > 57 && picture[49] == 0xFF && picture[50] == 0 && picture[51] == 0 && picture[52] == 0xFF,
          "its pixels are the pack's picture for that frame, through the palette");
    // Scanning again changes nothing.
    auto again = host::cosmetics::scan_disc_skins(pack_folder.string(), "Pictures", &error);
    std::string picture_again;
    for (const auto& item : host::cosmetics::assets()) if (item.id == plain_id) picture_again = item.preview_path;
    check(again.ok && host::cosmetics::assets().size() == 2 && picture_again == plain_picture &&
              read_file(fs::path(picture_again)) == picture,
          "a second scan keeps one portrait, the same one");
    // The picture is live with the skin, and gone with the alternate.
    check(host::cosmetics::select_variant("PlFxNr.dat", plain_id, &error), "the plain costume is selected");
    auto fst = one_file_fst(costume_at, (uint32_t)retail_default.size(), "PlFxNr.dat");
    host::cosmetics::apply_to_fst(fst.data(), (uint32_t)fst.size());
    auto companions = host::cosmetics::active_companions();
    check(companions.size() == 1 && companions[0].kind == "csp" && companions[0].target_path == "PlFxNr.dat",
          "the applied skin brings its portrait");
    check(host::cosmetics::select_variant_live("PlFxNr.dat", alt_id, &error) &&
              host::cosmetics::republish_slot(fst.data(), (uint32_t)fst.size(), "PlFxNr.dat").ok &&
              host::cosmetics::active_companions().empty(),
          "the alternate shows the game's own portrait");

    // A pack whose file changes another fighter's picture only, and one whose file is no character
    // select file at all: no portrait for Fox from either.
    fs::path other_folder = portrait_folder / L"other";
    fs::create_directories(other_folder, ec);
    write_file(other_folder / L"PlFxNr.dat", plain);
    write_file(other_folder / L"MnSlChr.usd", select_screen(0, 0xFC00));
    fs::path odd_folder = portrait_folder / L"odd";
    fs::create_directories(odd_folder, ec);
    write_file(odd_folder / L"PlFxNr.dat", plain);
    write_file(odd_folder / L"MnSlChr.usd", visual_dat());
    auto other_scan = host::cosmetics::scan_disc_skins(other_folder.string(), "Other", &error);
    auto odd_scan = host::cosmetics::scan_disc_skins(odd_folder.string(), "Odd", &error);
    bool other_listed = false, odd_listed = false, extra_picture = false;
    for (const auto& item : host::cosmetics::assets()) {
      if (item.source_name != "Other" && item.source_name != "Odd") continue;
      (item.source_name == "Other" ? other_listed : odd_listed) = true;
      extra_picture |= !item.preview_path.empty();
    }
    check(other_scan.ok && odd_scan.ok && other_listed && odd_listed && !extra_picture,
          "a picture equal to the game's is not stored, and a file that is not a character select file gives none");
    g_disc_table.clear(); g_disc_bytes.clear();
  }

  // Voice mods: a skin's own fighter sound bank, one zip per mod (docs/voice-mods.md).
  {
    using host::cosmetics::MatchFighter;
    using host::cosmetics::OverrideRead;
    host::cosmetics::thaw_after_online_session();
    host::cosmetics::set_online_probe(nullptr);
    fs::path voice_folder = folder / L"voice-mods";
    fs::create_directories(voice_folder, ec);
    host::cosmetics::configure((voice_folder / L"port-settings.ini").string());
    auto text_bytes = [](const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); };
    auto listed = [](const std::string& id) {
      for (const auto& item : host::cosmetics::assets()) if (item.id == id) return item;
      return host::cosmetics::AssetInfo{};
    };

    // The disc: Fox's English bank (50 sounds from id 516), his Japanese bank (54 from id 519) and
    // two of his costumes.
    const std::vector<uint32_t> us_stereo{30, 31}, jp_stereo{0, 34, 35};
    const auto disc_us = sound_bank(50, 516, us_stereo, 0x11), disc_jp = sound_bank(54, 519, jp_stereo, 0x12);
    const auto green_retail = skeleton_dat(0x2, 5.0f, false, "PlyFox5KGr_Share_joint");
    const auto default_retail = skeleton_dat(0x2, 5.0f, false);
    constexpr uint32_t us_at = 0x1000, jp_at = 0x4000, green_at = 0x7000, default_at = 0x7800;
    g_disc_bytes.assign(0x8000, 0);
    std::copy(disc_us.begin(), disc_us.end(), g_disc_bytes.begin() + us_at);
    std::copy(disc_jp.begin(), disc_jp.end(), g_disc_bytes.begin() + jp_at);
    std::copy(green_retail.begin(), green_retail.end(), g_disc_bytes.begin() + green_at);
    std::copy(default_retail.begin(), default_retail.end(), g_disc_bytes.begin() + default_at);
    g_disc_table = {{"PlFxGr.dat", {green_at, (uint32_t)green_retail.size()}},
                    {"PlFxNr.dat", {default_at, (uint32_t)default_retail.size()}}};
    check(disc_us.size() < 0x3000 && disc_jp.size() < 0x3000, "the fixture banks fit their place on the test disc");

    // A bank's own header says which fighter bank it is, and what is wrong with it.
    std::string bank_name, language, why;
    check(host::cosmetics::testing::inspect_bank(disc_us, &bank_name, &language, &why) && bank_name == "fox.ssm" &&
              language == "English",
          "a bank with Fox's English sounds is recognised as fox.ssm");
    check(host::cosmetics::testing::inspect_bank(disc_jp, &bank_name, &language, &why) && bank_name == "fox.ssm" &&
              language == "Japanese",
          "and the Japanese one too");
    check(!host::cosmetics::testing::inspect_bank(sound_bank(49, 516, us_stereo, 0), &bank_name, &language, &why),
          "a bank with one sound fewer is refused");
    check(!host::cosmetics::testing::inspect_bank(sound_bank(50, 516, {30}, 0), &bank_name, &language, &why) &&
              why.find("channel") != std::string::npos,
          "a sound with another channel count is refused, and the message says so");
    check(!host::cosmetics::testing::inspect_bank(sound_bank(50, 516, us_stereo, 0, 32, 0x0A), &bank_name, &language, &why) &&
              why.find("ADPCM") != std::string::npos,
          "a sound that is not ADPCM is refused");
    check(!host::cosmetics::testing::inspect_bank(sound_bank(50, 516, us_stereo, 0, 12000), &bank_name, &language, &why) &&
              why.find("room") != std::string::npos,
          "samples larger than the room the game keeps for the bank are refused");
    {
      auto cut = disc_us; cut.resize(cut.size() - 40);
      check(!host::cosmetics::testing::inspect_bank(cut, &bank_name, &language, &why), "a bank cut short is refused");
      auto outside = disc_us; be32(outside, 0x10 + 8 + 8, 0x00FFFFFF);   // the first sound's end address
      check(!host::cosmetics::testing::inspect_bank(outside, &bank_name, &language, &why) &&
                why.find("outside") != std::string::npos,
            "a sound that points outside the sample data is refused");
    }

    // One zip per mod: the costume, the bank and a manifest with the mod's name.
    const auto wolf_costume = skeleton_dat(0x2 | 0x10, 5.0f, false, "PlyFox5KGr_Share_joint");   // looks only
    const auto wolf_bank = sound_bank(50, 516, us_stereo, 0xAA);
    fs::path wolf_zip = voice_folder / L"wolf.zip";
    write_file(wolf_zip, stored_zip({{"Wolf/PlFxGr.dat", wolf_costume}, {"Wolf/sound/wolf.ssm", wolf_bank},
                                     {"Wolf/mod.json", text_bytes("{\"name\": \"Wolf\"}")}}));
    auto wolf = host::cosmetics::import_file(wolf_zip.string());
    check(wolf.ok && listed(wolf.asset_id).name == "Wolf" && listed(wolf.asset_id).voice == "fox.ssm" &&
              listed(wolf.asset_id).target_path == "PlFxGr.dat" && listed(wolf.asset_id).selected,
          "a zip with a costume and a bank imports one skin that carries the voice");
    check(read_file(voice_folder / L"CosmeticMods" / L"assets" / fs::u8path(wolf.asset_id) / L"companions" / L"voice.ssm") == wolf_bank,
          "the bank is stored beside the skin's pictures");

    // A bank that does not keep the game's sounds: the whole zip is refused, the costume too.
    fs::path short_zip = voice_folder / L"short.zip";
    write_file(short_zip, stored_zip({{"PlFxNr.dat", skeleton_dat(0x2 | 0x10, 5.0f, false)},
                                      {"fox.ssm", sound_bank(49, 516, us_stereo, 0xCC)}}));
    auto refused = host::cosmetics::import_file(short_zip.string());
    check(!refused.ok && refused.message.find("49 sounds") != std::string::npos &&
              refused.message.find("50 from id 516") != std::string::npos && host::cosmetics::assets().size() == 1,
          "a bank with a different sound count is refused with both counts, and nothing of the zip is imported");
    fs::path falco_zip = voice_folder / L"falco.zip";
    write_file(falco_zip, stored_zip({{"PlFxNr.dat", skeleton_dat(0x2 | 0x10, 5.0f, false)},
                                      {"voice.ssm", sound_bank(53, 463, {2, 33, 34}, 0xCC)}}));
    refused = host::cosmetics::import_file(falco_zip.string());
    check(!refused.ok && refused.message.find("Falco") != std::string::npos && host::cosmetics::assets().size() == 1,
          "another fighter's bank is refused for this skin");

    // A second skin, for the default costume, with a moved bone (off online); then a bank on its own.
    fs::path kitsune_path = voice_folder / L"kitsune.dat";
    write_file(kitsune_path, skeleton_dat(0x2, 5.5f, false));
    auto kitsune = host::cosmetics::import_file(kitsune_path.string());
    const auto kitsune_bank = sound_bank(50, 516, us_stereo, 0xBB, 64);   // longer than the disc's bank
    fs::path loose_bank = voice_folder / L"fox.ssm";
    write_file(loose_bank, kitsune_bank);
    auto loose = host::cosmetics::import_file(loose_bank.string());
    check(kitsune.ok && !loose.ok && loose.message.find("2 skins") != std::string::npos,
          "a bank alone is refused with advice when two skins could take it");
    fs::path named_bank = voice_folder / L"Fox Default.ssm";
    write_file(named_bank, kitsune_bank);
    loose = host::cosmetics::import_file(named_bank.string());
    check(loose.ok && loose.asset_id == kitsune.asset_id && listed(kitsune.asset_id).voice == "fox.ssm" &&
              listed(kitsune.asset_id).voice_source == "Fox Default.ssm",
          "a bank named after a costume becomes the voice of that costume's skin");
    check(host::cosmetics::import_voice(named_bank.string(), kitsune.asset_id).already_present,
          "giving a skin the same bank again changes nothing");
    check(!host::cosmetics::import_voice(named_bank.string(), "no-such-skin").ok, "a bank needs an installed skin");

    // The catalog on disk keeps the voices.
    host::cosmetics::configure((voice_folder / L"port-settings.ini").string());
    check(listed(wolf.asset_id).voice == "fox.ssm" && listed(kitsune.asset_id).voice == "fox.ssm" &&
              listed(wolf.asset_id).selected && listed(kitsune.asset_id).selected,
          "the voices are still there after the catalog is loaded again");

    // The game starts. The English bank file has two candidate voices; the Japanese one fits none.
    auto fst = audio_fst("fox.ssm", jp_at, (uint32_t)disc_jp.size(), us_at, (uint32_t)disc_us.size(),
                         {{"PlFxGr.dat", green_at, (uint32_t)green_retail.size()},
                          {"PlFxNr.dat", default_at, (uint32_t)default_retail.size()}});
    host::cosmetics::apply_to_fst(fst.data(), (uint32_t)fst.size());
    const uint32_t bank_length = (uint32_t)kitsune_bank.size();
    check(host::cosmetics::voice_bank_count() == 1 && read_be32(fst.data() + 56) == bank_length &&
              read_be32(fst.data() + 32) == disc_jp.size() && bank_length > disc_us.size(),
          "the English bank file gets the longest candidate's length for the session; the Japanese file is untouched");
    check(host::cosmetics::applied_asset(green_at) == wolf.asset_id && host::cosmetics::applied_asset(default_at) == kitsune.asset_id,
          "both skins are applied");
    const size_t samples_at = bank_samples_at(disc_us);
    auto serves = [&](const std::vector<uint8_t>& bank) {
      std::vector<uint8_t> got(bank_length, 0x77);
      if (host::cosmetics::read(us_at, 0, got.data(), bank_length) != OverrideRead::Success) return false;
      return std::equal(bank.begin(), bank.end(), got.begin()) &&
             std::all_of(got.begin() + (std::ptrdiff_t)bank.size(), got.end(), [](uint8_t byte) { return byte == 0; });
    };
    uint8_t probe[4];
    check(serves(disc_us) && host::cosmetics::read(jp_at, 0, probe, 4) == OverrideRead::NotOverridden,
          "before a match the bank file is the disc's own, with zeros up to the session length");
    check(host::cosmetics::read(us_at, bank_length - 8, probe, 4) == OverrideRead::Success &&
              host::cosmetics::read(us_at, bank_length + 64, probe, 4) == OverrideRead::Failed,
          "reads stay inside the session length");

    // A match with green Fox on port 1: the skin's bank is served, and named for the engine to reload.
    MatchFighter ports[4];
    ports[0] = {2, 3};
    auto plan = host::cosmetics::plan_match_voices(ports);
    check(plan.changed.size() == 1 && plan.changed[0].file == "fox.ssm" && plan.changed[0].bank == 11 && serves(wolf_bank),
          "the bank is served when its skin is the costume in the match");
    {
      std::vector<uint8_t> header(0x20), samples(32);
      check(host::cosmetics::read(us_at, 0, header.data(), 0x20) == OverrideRead::Success &&
                host::cosmetics::read(us_at, (uint32_t)samples_at, samples.data(), 32) == OverrideRead::Success &&
                read_be32(header.data() + 8) == 50 && read_be32(header.data() + 12) == 516 && samples[0] == 0xAA,
            "the loader's own reads (header, then samples) see the skin's bank");
    }
    check(host::cosmetics::plan_match_voices(ports).changed.empty() && serves(wolf_bank),
          "the same match again changes nothing, so nothing is reloaded");
    ports[0] = {2, 1};   // orange Fox: the standard costume
    plan = host::cosmetics::plan_match_voices(ports);
    check(plan.changed.size() == 1 && serves(disc_us), "the disc's bank is served when the costume in the match has no skin");
    ports[0] = {20, 0};   // Falco alone
    check(host::cosmetics::plan_match_voices(ports).changed.empty() && serves(disc_us),
          "a match without the bank's fighter leaves the bank as it is");

    // Two Foxes with different voices: one bank per fighter, the lowest port decides.
    auto noted = [](const host::cosmetics::VoicePlan& made, const char* text) {
      for (const auto& note : made.notes) if (note.find(text) != std::string::npos) return true;
      return false;
    };
    ports[0] = {2, 0}; ports[1] = {2, 3};
    plan = host::cosmetics::plan_match_voices(ports);
    check(plan.changed.size() == 1 && serves(kitsune_bank) && noted(plan, "port 2's Wolf is not used: port 1 decides fox.ssm"),
          "two Foxes with different voices: port 1's is served and port 2's is reported");
    ports[0] = {2, 3}; ports[1] = {2, 0};
    plan = host::cosmetics::plan_match_voices(ports);
    check(plan.changed.size() == 1 && serves(wolf_bank) && noted(plan, "port 2's") && noted(plan, "port 1 decides"),
          "with the ports swapped the other voice wins");
    ports[0] = {2, 1}; ports[1] = {2, 3};
    plan = host::cosmetics::plan_match_voices(ports);
    check(plan.changed.size() == 1 && serves(disc_us) && noted(plan, "port 2's Wolf is not used"),
          "the lowest port decides also when its choice is the game's own sounds");
    ports[0] = {-1, 0}; ports[1] = {2, 3}; ports[3] = {2, 0};
    plan = host::cosmetics::plan_match_voices(ports);
    check(serves(wolf_bank) && noted(plan, "port 4's") && noted(plan, "port 2 decides"),
          "an empty port decides nothing: the lowest port that plays the fighter does");
    ports[1] = {}; ports[3] = {};

    // Online: sound data only, so a voice stays on. A skin that online play shows as the standard
    // costume keeps the game's voice too.
    host::cosmetics::set_online_probe([] { return true; });
    host::cosmetics::freeze_for_online_session();
    ports[0] = {2, 3};
    plan = host::cosmetics::plan_match_voices(ports);
    check(host::cosmetics::online_allowed(green_at) && serves(wolf_bank), "online, a skin that stays on keeps its voice");
    ports[0] = {2, 0};
    plan = host::cosmetics::plan_match_voices(ports);
    check(!host::cosmetics::online_allowed(default_at) && plan.changed.size() == 1 && serves(disc_us),
          "online, a skin shown as the standard costume has the standard voice");
    host::cosmetics::thaw_after_online_session();
    host::cosmetics::set_online_probe(nullptr);
    plan = host::cosmetics::plan_match_voices(ports);
    check(plan.changed.size() == 1 && serves(kitsune_bank), "offline again, that skin has its voice");

    // The character select's L / R: the bank follows the skin the slot serves at the next match load.
    ports[0] = {2, 3};
    plan = host::cosmetics::plan_match_voices(ports);
    check(serves(wolf_bank), "green Fox has the skin's voice");
    check(host::cosmetics::select_variant_live("PlFxGr.dat", "", &error) &&
              host::cosmetics::republish_slot(fst.data(), (uint32_t)fst.size(), "PlFxGr.dat").ok &&
              host::cosmetics::voice_bank_count() == 1 && serves(wolf_bank),
          "picking the standard costume publishes the slot again and leaves the bank until the next match load");
    plan = host::cosmetics::plan_match_voices(ports);
    check(plan.changed.size() == 1 && serves(disc_us), "the next match load serves the disc's bank for the standard costume");
    check(host::cosmetics::select_variant_live("PlFxGr.dat", wolf.asset_id, &error) &&
              host::cosmetics::republish_slot(fst.data(), (uint32_t)fst.size(), "PlFxGr.dat").ok,
          "the skin is picked again");
    plan = host::cosmetics::plan_match_voices(ports);
    check(plan.changed.size() == 1 && serves(wolf_bank) && read_be32(fst.data() + 56) == bank_length,
          "and its voice is back at the next match load, under the same file length");

    // Several costumes of one fighter in one mod, and a voice for a costume named by the manifest.
    fs::path pair_zip = voice_folder / L"star.zip";
    write_file(pair_zip, stored_zip({{"PlFxOr.dat", costume_dat("PlyFox5KOr")}, {"PlFxLa.dat", costume_dat("PlyFox5KLa")},
                                     {"star.ssm", wolf_bank}, {"mod.json", text_bytes("{\"name\":\"Star\"}")},
                                     {"PlFxOr csp.png", png(136, 188)}}));
    auto pair = host::cosmetics::import_file(pair_zip.string());
    size_t star_voices = 0, star_pictures = 0;
    for (const auto& item : host::cosmetics::assets()) {
      if (item.name != "Star, Orange" && item.name != "Star, Lavender") continue;
      star_voices += item.voice == "fox.ssm";
      star_pictures += !item.preview_path.empty();
    }
    check(pair.ok && star_voices == 2 && star_pictures == 1,
          "a zip with two costumes of one fighter gives both the voice, and each the picture named for it");
    std::string orange_id;
    for (const auto& item : host::cosmetics::assets()) if (item.name == "Star, Orange") orange_id = item.id;
    check(host::cosmetics::remove_voice(orange_id, &error) && listed(orange_id).voice.empty(), "a voice can be taken away again");
    fs::path voice_zip = voice_folder / L"voice only.zip";
    write_file(voice_zip, stored_zip({{"sounds/new.ssm", kitsune_bank},
                                      {"mod.json", text_bytes("{\"slot\": \"Fox Orange\"}")}}));
    auto voice_only = host::cosmetics::import_file(voice_zip.string());
    check(voice_only.ok && voice_only.asset_id == orange_id && listed(orange_id).voice == "fox.ssm",
          "a zip with only a bank goes to the skin of the costume its manifest names");
    fs::path two_zip = voice_folder / L"two banks.zip";
    write_file(two_zip, stored_zip({{"a.ssm", wolf_bank}, {"b.ssm", kitsune_bank}}));
    check(!host::cosmetics::import_file(two_zip.string()).ok, "a zip with two banks and no manifest is refused");

    // The applied snapshot is this launch's: the voices added since wait for a restart.
    check(host::cosmetics::pending_restart(), "a voice added while the game runs waits for a restart");
    g_disc_table.clear(); g_disc_bytes.clear();
  }

  // A picture given to one skin afterwards: the skin's own shows, the costume's added one fills in
  // where a skin has none.
  {
    using host::cosmetics::PortraitSource;
    host::cosmetics::thaw_after_online_session();
    host::cosmetics::set_online_probe(nullptr);
    fs::path own_folder = folder / L"skin-pictures";
    fs::create_directories(own_folder, ec);
    host::cosmetics::configure((own_folder / L"port-settings.ini").string());
    const auto retail_default = skeleton_dat(0x2, 5.0f, false);
    const auto pack_skin = skeleton_dat(0x2 | 0x10, 5.0f, false);
    const auto imported = skeleton_dat(0x2, 5.0f, true);
    constexpr uint32_t retail_default_at = 0x100;
    g_disc_bytes.assign(0x400, 0);
    std::copy(retail_default.begin(), retail_default.end(), g_disc_bytes.begin() + retail_default_at);
    g_disc_table = {{"PlFxNr.dat", {retail_default_at, (uint32_t)retail_default.size()}}};
    fs::path pack_folder = own_folder / L"pack";
    fs::create_directories(pack_folder, ec);
    write_file(pack_folder / L"PlFxNr.dat", pack_skin);
    auto scan = host::cosmetics::scan_disc_skins(pack_folder.string(), "Pack", &error);
    fs::path import_path = own_folder / L"mine.dat";
    write_file(import_path, imported);
    auto mine = host::cosmetics::import_file(import_path.string());
    std::string disc_id;
    for (const auto& item : host::cosmetics::assets()) if (item.source == "disc") disc_id = item.id;
    check(scan.ok && mine.ok && !disc_id.empty(), "the slot has a pack skin and an import");
    bool import_named = false;
    for (const auto& item : host::cosmetics::assets()) import_named |= item.id == mine.asset_id && item.source_name == "mine.dat";
    check(import_named, "an import's listing names the file it came from");

    auto csp_path_of = [](const std::string& slot) {
      std::string path; size_t count = 0;
      for (const auto& item : host::cosmetics::active_companions())
        if (item.kind == "csp" && item.target_path == slot) { path = item.path; ++count; }
      return count == 1 ? path : std::string();
    };
    auto fst = one_file_fst(retail_default_at, (uint32_t)retail_default.size(), "PlFxNr.dat");
    auto apply = [&] { host::cosmetics::apply_to_fst(fst.data(), (uint32_t)fst.size()); };

    // From a PNG.
    fs::path first_png = own_folder / L"first.png", bad_png = own_folder / L"bad.png";
    write_file(first_png, png(136, 188));
    write_file(bad_png, {1, 2, 3, 4});
    check(host::cosmetics::skin_portrait_source(mine.asset_id, "csp") == PortraitSource::Standard,
          "a skin with no picture and no added one shows the standard picture");
    check(!host::cosmetics::set_skin_portrait(mine.asset_id, bad_png.string(), "csp").ok &&
              !host::cosmetics::set_skin_portrait("no-such-skin", first_png.string(), "csp").ok &&
              !host::cosmetics::set_skin_portrait(mine.asset_id, first_png.string(), "banner").ok,
          "a file that is no PNG, an unknown skin and an unknown kind are refused");
    auto set = host::cosmetics::set_skin_portrait(mine.asset_id, first_png.string(), "csp");
    check(set.ok && set.asset_id == mine.asset_id && host::cosmetics::skin_portrait_source(mine.asset_id, "csp") == PortraitSource::Own,
          "a PNG becomes the skin's own portrait");
    // The costume's added picture, and one for another costume and another fighter.
    fs::path slot_png = own_folder / L"slot.png";
    write_file(slot_png, png(136, 189));
    check(host::cosmetics::import_portrait(slot_png.string(), "PlFxNr.dat", "csp").ok &&
              host::cosmetics::import_portrait(slot_png.string(), "PlFxGr.dat", "csp").ok &&
              host::cosmetics::import_portrait(slot_png.string(), "PlCaNr.dat", "csp").ok,
          "costumes get added pictures");
    check(host::cosmetics::skin_portrait_source(disc_id, "csp") == PortraitSource::Costume &&
              host::cosmetics::skin_portrait_source(mine.asset_id, "csp") == PortraitSource::Own &&
              host::cosmetics::skin_portrait_source(mine.asset_id, "stock") == PortraitSource::Standard,
          "the list says which picture each skin shows");
    check(host::cosmetics::select_variant("PlFxNr.dat", mine.asset_id, &error), "the import is selected");
    apply();
    check(csp_path_of("PlFxNr.dat").find(mine.asset_id) != std::string::npos,
          "the selected skin's own portrait is the one shown");
    // Another skin of the same slot, with none of its own: the costume's added picture, also live.
    check(host::cosmetics::select_variant_live("PlFxNr.dat", disc_id, &error) &&
              host::cosmetics::republish_slot(fst.data(), (uint32_t)fst.size(), "PlFxNr.dat").ok &&
              csp_path_of("PlFxNr.dat").find("portrait-PlFxNr") != std::string::npos,
          "a skin without its own portrait shows the costume's added one");
    check(host::cosmetics::select_variant_live("PlFxNr.dat", mine.asset_id, &error) &&
              host::cosmetics::republish_slot(fst.data(), (uint32_t)fst.size(), "PlFxNr.dat").ok &&
              csp_path_of("PlFxNr.dat").find(mine.asset_id) != std::string::npos,
          "stepping back to the skin with its own portrait shows that one again");

    // Copied from another entry, onto the pack's skin (which has no stored file of its own).
    auto choices = host::cosmetics::portrait_choices(disc_id, "csp");
    bool offers_import = false, offers_slot = false, offers_green = false, offers_other = false;
    for (const auto& choice : choices) {
      offers_import |= choice.asset_id == mine.asset_id && !choice.label.empty();
      offers_slot |= choice.asset_id == "portrait-PlFxNr";
      offers_green |= choice.asset_id == "portrait-PlFxGr";
      offers_other |= choice.asset_id == "portrait-PlCaNr" || choice.asset_id == disc_id;
    }
    check(choices.size() == 3 && offers_import && offers_slot && offers_green && !offers_other,
          "the choices are the fighter's other entries that have a portrait");
    check(host::cosmetics::portrait_choices(disc_id, "stock").empty(), "no entry has a stock icon to offer");
    check(!host::cosmetics::set_skin_portrait_from(disc_id, "portrait-PlCaNr", "csp", &error) &&
              !host::cosmetics::set_skin_portrait_from(disc_id, mine.asset_id, "stock", &error),
          "another fighter's picture, or a picture the source does not have, is refused");
    check(host::cosmetics::set_skin_portrait_from(disc_id, mine.asset_id, "csp", &error) &&
              host::cosmetics::skin_portrait_source(disc_id, "csp") == PortraitSource::Own,
          "a pack skin takes a copy of another skin's portrait");
    check(host::cosmetics::select_variant("PlFxNr.dat", disc_id, &error), "the pack skin is selected");
    apply();
    const std::string disc_picture = csp_path_of("PlFxNr.dat");
    check(disc_picture.find(disc_id) != std::string::npos && read_file(fs::path(disc_picture)) == png(136, 188),
          "the pack skin shows its copy, kept in the catalog");

    // Taken away from the source: the copy stays, and the source falls back to the costume's picture.
    check(host::cosmetics::clear_skin_portrait(mine.asset_id, "csp", &error) &&
              !host::cosmetics::clear_skin_portrait(mine.asset_id, "csp", &error) &&
              host::cosmetics::skin_portrait_source(mine.asset_id, "csp") == PortraitSource::Costume,
          "a skin's own portrait can be removed, once");
    apply();
    check(csp_path_of("PlFxNr.dat") == disc_picture && read_file(fs::path(disc_picture)) == png(136, 188),
          "removing the source's picture leaves the copy in place");
    check(host::cosmetics::select_variant("PlFxNr.dat", mine.asset_id, &error), "the import is selected again");
    apply();
    check(csp_path_of("PlFxNr.dat").find("portrait-PlFxNr") != std::string::npos,
          "the skin whose portrait was removed shows the costume's added one");
    // A stock icon, replaced by another: one picture of the kind stays.
    fs::path stock_png = own_folder / L"stock.png", stock_two = own_folder / L"stock two.png";
    write_file(stock_png, png(24, 24));
    write_file(stock_two, png(24, 25));
    check(host::cosmetics::set_skin_portrait(mine.asset_id, stock_png.string(), "stock").ok &&
              host::cosmetics::set_skin_portrait(mine.asset_id, stock_two.string(), "stock").ok,
          "a stock icon is set and replaced");
    apply();
    size_t stocks = 0, new_stocks = 0;
    for (const auto& item : host::cosmetics::active_companions())
      if (item.kind == "stock" && item.target_path == "PlFxNr.dat") {
        ++stocks; new_stocks += read_file(fs::path(item.path)) == png(24, 25);
      }
    check(stocks == 1 && new_stocks == 1, "the replaced stock icon is the one shown");

    // The catalog is read again, and the pack is scanned again: the pictures stay with their skins.
    host::cosmetics::configure((own_folder / L"port-settings.ini").string());
    check(host::cosmetics::skin_portrait_source(disc_id, "csp") == PortraitSource::Own &&
              host::cosmetics::skin_portrait_source(mine.asset_id, "stock") == PortraitSource::Own &&
              host::cosmetics::skin_portrait_source(mine.asset_id, "csp") == PortraitSource::Costume,
          "the skins' own pictures are there after the catalog is read again");
    auto rescan = host::cosmetics::scan_disc_skins(pack_folder.string(), "Pack", &error);
    check(rescan.ok && host::cosmetics::skin_portrait_source(disc_id, "csp") == PortraitSource::Own &&
              host::cosmetics::select_variant("PlFxNr.dat", disc_id, &error),
          "scanning the pack again keeps the picture the player gave its skin");
    apply();
    check(csp_path_of("PlFxNr.dat") == disc_picture, "and it is still the one shown");

    // A costume's picture entry given to a skin of that costume switches itself off, so the standard
    // costume shows the game's picture again; another costume's entry stays on.
    check(host::cosmetics::set_skin_portrait_from(mine.asset_id, "portrait-PlFxGr", "csp", &error) &&
              host::cosmetics::clear_skin_portrait(mine.asset_id, "csp", &error) &&
              host::cosmetics::skin_portrait_source(mine.asset_id, "csp") == PortraitSource::Costume,
          "taking another costume's picture leaves this costume's entry on");
    check(host::cosmetics::set_skin_portrait_from(mine.asset_id, "portrait-PlFxNr", "csp", &error) &&
              host::cosmetics::skin_portrait_source(mine.asset_id, "csp") == PortraitSource::Own &&
              host::cosmetics::clear_skin_portrait(mine.asset_id, "csp", &error) &&
              host::cosmetics::skin_portrait_source(mine.asset_id, "csp") == PortraitSource::Standard,
          "a skin that takes its costume's picture entry turns that entry off");
    g_disc_table.clear(); g_disc_bytes.clear();
  }

  fs::remove_all(folder, ec);
  if (failures) std::fprintf(stderr, "%d cosmetic mod test(s) failed\n", failures);
  else std::puts("cosmetic mod tests passed");
  return failures ? 1 : 0;
}
