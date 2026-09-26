// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include "cosmetic_mods.h"

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

// cosmetic_mods.cpp only needs runtime logging; keep this unit target independent of generated
// guest code and the rest of the Windows renderer/runtime.
namespace host {
void log(const char* format, ...) {
  va_list args; va_start(args, format); std::vfprintf(stdout, format, args); va_end(args);
  std::fputc('\n', stdout);
}
bool disc_find_file(const std::string& path, uint32_t* offset, uint32_t* size) {
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
}

int main(int argc, char** argv) {
  wchar_t temp_root[MAX_PATH]; GetTempPathW(MAX_PATH, temp_root);
  fs::path folder = fs::path(temp_root) /
      (L"melee-cosmetic-test-" + std::to_wstring(GetCurrentProcessId()));
  std::error_code ec; fs::remove_all(folder, ec); fs::create_directories(folder, ec);
  check(!ec, "create temporary directory");

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
  check(host::cosmetics::read(stage_start, 0, online_stage.data(), (uint32_t)online_stage.size()) ==
            host::cosmetics::OverrideRead::Success && online_stage == clean_stage,
        "unsafe project stage serves clean ISO bytes during online play");
  host::cosmetics::thaw_after_online_session();
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
  auto fst = one_file_fst(start, 0x10000);
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

  fs::remove_all(folder, ec);
  if (failures) std::fprintf(stderr, "%d cosmetic mod test(s) failed\n", failures);
  else std::puts("cosmetic mod tests passed");
  return failures ? 1 : 0;
}
