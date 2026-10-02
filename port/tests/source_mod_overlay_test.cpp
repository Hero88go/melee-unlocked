// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_mod_overlay.h"
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
static uint32_t be32(const unsigned char* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
// Optional field check: pass a retail ISO and patched ISO to exercise the exact
// production importer against a full disc without requiring either in CI.
static int check_real_iso(const char* retail, const char* patched) {
  using Overlay = source_port::ModOverlay;
  std::ifstream base(retail, std::ios::binary);
  unsigned char header[0x440]{};
  if (!base.read((char*)header, sizeof header)) return 2;
  const uint32_t offset = be32(header + 0x424), size = be32(header + 0x428);
  if (size < 12 || size > 2u * 1024u * 1024u) return 2;
  std::vector<unsigned char> fst(size);
  base.seekg(offset);
  if (!base.read((char*)fst.data(), size)) return 2;
  const uint32_t count = be32(fst.data() + 8);
  if (count < 1 || count > 4096 || count * 12 > size) return 2;
  const char* names = (const char*)fst.data() + count * 12;
  std::vector<std::pair<uint32_t, std::string>> dirs{{count, ""}};
  std::map<std::string, Overlay::DiscFile> files;
  for (uint32_t i = 1; i < count; ++i) {
    while (dirs.size() > 1 && i >= dirs.back().first) dirs.pop_back();
    const unsigned char* e = fst.data() + i * 12;
    const uint32_t no = be32(e) & 0xFFFFFFu;
    if (no >= size - count * 12) return 2;
    const char* end = (const char*)std::memchr(names + no, 0, size - count * 12 - no);
    if (!end) return 2;
    std::string name(names + no, end);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::string path = dirs.back().second + "/" + name;
    if (e[0]) dirs.push_back({be32(e + 8), path});
    else files[path] = {be32(e + 4), be32(e + 8)};
  }
  Overlay overlay;
  std::string error;
  const bool ok = overlay.add_iso(patched, "field-iso", [&](const std::string& path, Overlay::DiscFile* out) {
    const auto it = files.find(path);
    if (it == files.end()) return false;
    *out = it->second; return true;
  }, [&](uint32_t at, void* dst, uint32_t length) {
    base.clear(); base.seekg(at);
    return bool(base.read((char*)dst, length));
  }, error);
  if (!ok) { std::fprintf(stderr, "real ISO import: %s\n", error.c_str()); return 1; }
  std::printf("real ISO import: %zu changed files; m-ex %s; Sonic %s\n",
      overlay.files().size(),
      std::any_of(overlay.files().begin(), overlay.files().end(), [](const auto& f) { return f.path == "/mxdt.dat"; }) ? "yes" : "no",
      std::any_of(overlay.files().begin(), overlay.files().end(), [](const auto& f) { return f.path == "/plsn.dat"; }) ? "yes" : "no");
  return 0;
}
int main(int argc, char** argv) {
  if (argc == 3) return check_real_iso(argv[1], argv[2]);
  namespace fs = std::filesystem;
  using Overlay = source_port::ModOverlay;
  const auto root = fs::temp_directory_path() /
      ("mu-overlay-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK(fs::create_directory(root));
  CHECK(fs::create_directory(root / "audio"));
  { std::ofstream f(root / "PlMrNr.dat", std::ios::binary); f << "costume"; }
  { std::ofstream f(root / "audio" / "test.hps", std::ios::binary); f << "music"; }
  Overlay overlay;
  std::string error;
  CHECK(overlay.load(root, error));
  CHECK(overlay.files().size() == 2);
  CHECK(overlay.files()[0].path == "/audio/test.hps");
  CHECK(overlay.files()[1].path == "/plmrnr.dat");
  unsigned char out[64]{};
  const auto start = overlay.files()[1].start;
  CHECK(overlay.read(start, out, 32) == Overlay::Read::Success);
  CHECK(out[0] == 'c' && out[6] == 'e' && out[7] == 0 && out[31] == 0);
  CHECK(overlay.read(start + 3, out, 4) == Overlay::Read::Success);
  CHECK(out[0] == 't' && out[3] == 'e');
  CHECK(overlay.read(start, out, 33) == Overlay::Read::Failed);
  CHECK(overlay.read(start + 32, out, 1) == Overlay::Read::Failed);
  CHECK(overlay.read(start + 0x7000, out, 1) == Overlay::Read::Failed);
  CHECK(overlay.read(0xFFFFFFFFu, out, 32) == Overlay::Read::Failed);
  CHECK(overlay.read(start, nullptr, 1) == Overlay::Read::Failed);
  CHECK(overlay.read(start, nullptr, 0) == Overlay::Read::Success);
  CHECK(overlay.read(123, out, 4) == Overlay::Read::NotOverridden);
  // The running session is immutable, including when its source file changes.
  { std::ofstream f(root / "PlMrNr.dat", std::ios::binary); f << "changed"; }
  CHECK(overlay.read(start, out, 7) == Overlay::Read::Success && out[1] == 'o');
  // Separately enabled presets can share identical files. A differing asset is a
  // user-visible conflict, and a failed import must leave the live table intact.
  const auto other = fs::path(root.string() + "-other");
  CHECK(fs::create_directory(other));
  { std::ofstream f(other / "PlMrNr.dat", std::ios::binary); f << "costume"; }
  CHECK(overlay.add_directory(other, "same-preset", error));
  CHECK(overlay.files().size() == 2);
  { std::ofstream f(other / "PlMrNr.dat", std::ios::binary); f << "costumX"; }
  CHECK(!overlay.add_directory(other, "other-preset", error));
  CHECK(error.find("conflicting mod file /plmrnr.dat") != std::string::npos);
  CHECK(overlay.files().size() == 2);
  // Layer order: the later pack's copy replaces the earlier one and the pair is listed.
  {
    Overlay layered;
    layered.set_layered(true);
    CHECK(layered.load(root, error));
    const auto first_start = std::find_if(layered.files().begin(), layered.files().end(),
        [](const Overlay::File& f) { return f.path == "/plmrnr.dat"; })->start;
    CHECK(layered.add_directory(other, "later-preset", error));
    CHECK(layered.files().size() == 2);
    CHECK(layered.conflicts().size() == 1 && layered.conflicts()[0].path == "/plmrnr.dat" &&
          layered.conflicts()[0].later == "later-preset");
    const auto winner = std::find_if(layered.files().begin(), layered.files().end(),
        [](const Overlay::File& f) { return f.path == "/plmrnr.dat"; });
    CHECK(winner != layered.files().end() && winner->profile == "later-preset" && winner->start != first_start);
    CHECK(layered.read(winner->start, out, 7) == Overlay::Read::Success && out[6] == 'X');
    CHECK(std::is_sorted(layered.files().begin(), layered.files().end(),
        [](const Overlay::File& a, const Overlay::File& b) { return a.start < b.start; }));
  }
  CHECK(fs::remove(other / "PlMrNr.dat"));
  CHECK(fs::remove(other));

  // A patched ISO is compared with the retail disc. Unchanged files are skipped;
  // changed files are addressed through the virtual DVD range and read on demand.
  auto put32 = [](std::vector<unsigned char>& image, size_t at, unsigned value) {
    image[at] = (unsigned char)(value >> 24);
    image[at + 1] = (unsigned char)(value >> 16);
    image[at + 2] = (unsigned char)(value >> 8);
    image[at + 3] = (unsigned char)value;
  };
  const auto iso = fs::path(root.string() + ".iso");
  std::vector<unsigned char> image(0x800);
  const unsigned char disc_id[8] = {'G', 'A', 'L', 'E', '0', '1', 0, 2};
  std::memcpy(image.data(), disc_id, sizeof disc_id);
  put32(image, 0x424, 0x500); put32(image, 0x428, 77);
  image[0x500] = 1; put32(image, 0x508, 4); // root: four entries
  put32(image, 0x50C + 4, 0x600); put32(image, 0x50C + 8, 7);
  put32(image, 0x518, 11); put32(image, 0x518 + 4, 0x700); put32(image, 0x518 + 8, 7);
  put32(image, 0x524, 19); put32(image, 0x524 + 4, 0x710);
  std::memcpy(image.data() + 0x530, "PlMrNr.dat", 11);
  std::memcpy(image.data() + 0x530 + 11, "New.dat", 8);
  std::memcpy(image.data() + 0x530 + 19, "empty.ini", 10);
  std::memcpy(image.data() + 0x600, "costume", 7);
  std::memcpy(image.data() + 0x700, "newdata", 7);
  { std::ofstream f(iso, std::ios::binary); f.write((const char*)image.data(), (std::streamsize)image.size()); }
  auto lookup = [](const std::string& path, Overlay::DiscFile* file) {
    if (path != "/plmrnr.dat") return false;
    *file = {0x100, 7}; return true;
  };
  auto base_read = [](uint32_t offset, void* dst, uint32_t size) {
    if (offset < 0x100 || offset - 0x100 + size > 7) return false;
    std::memcpy(dst, "costume" + offset - 0x100, size); return true;
  };
  { std::fstream f(iso, std::ios::binary | std::ios::in | std::ios::out);
    f.put('X'); }
  CHECK(!overlay.add_iso(iso, "wrong-game", lookup, base_read, error));
  CHECK(error.find("GALE01 revision 2") != std::string::npos);
  CHECK(overlay.files().size() == 2);
  { std::fstream f(iso, std::ios::binary | std::ios::in | std::ios::out);
    f.put('G'); }
  overlay.set_base_disc({{"/plmrnr.dat", "/gone.dat"}, 0, 0}, base_read);
  CHECK(overlay.add_iso(iso, "iso-preset", lookup, base_read, error));
  CHECK(overlay.files().size() == 4);
  CHECK(overlay.iso_reports().size() == 1 && overlay.iso_reports()[0].changed_files == 2 &&
        overlay.iso_reports()[0].deleted == std::vector<std::string>{"/gone.dat"} &&
        overlay.iso_reports()[0].dol_runs.empty());
  CHECK(overlay.files()[2].path == "/empty.ini" && overlay.files()[2].length == 0);
  CHECK(overlay.read(overlay.files()[2].start, nullptr, 0) == Overlay::Read::Success);
  CHECK(overlay.read(overlay.files()[2].start, out, 1) == Overlay::Read::Failed);
  CHECK(overlay.files()[3].path == "/new.dat");
  CHECK(overlay.read(overlay.files()[3].start, out, 7) == Overlay::Read::Success);
  CHECK(std::memcmp(out, "newdata", 7) == 0);
  // The disc's PlMrNr.dat equals the retail copy, so the pack does not carry it: nothing for the
  // skin rule to judge, and the retail file is what every view reads.
  CHECK(std::none_of(overlay.files().begin(), overlay.files().end(),
      [](const Overlay::File& f) { return f.profile == "iso-preset" && f.path == "/plmrnr.dat"; }));
  // A costume in a files pack is read whole in one call at its exact length (the skin rule
  // compares the complete file with the retail one), also when the length is not a multiple of 32.
  {
    const auto skins = fs::path(root.string() + "-skins");
    CHECK(fs::create_directory(skins));
    std::vector<unsigned char> costume(1000);
    for (size_t i = 0; i < costume.size(); ++i) costume[i] = (unsigned char)(i * 7 + 3);
    { std::ofstream f(skins / "PlFxNr.dat", std::ios::binary); f.write((const char*)costume.data(), (std::streamsize)costume.size()); }
    Overlay pack;
    CHECK(pack.add_directory(skins, "layer 1 skins", error));
    CHECK(pack.files().size() == 1 && pack.files()[0].path == "/plfxnr.dat" && pack.files()[0].length == costume.size());
    std::vector<unsigned char> whole(costume.size(), 0);
    CHECK(pack.read(pack.files()[0].start, whole.data(), (uint32_t)whole.size()) == Overlay::Read::Success);
    CHECK(whole == costume);
    CHECK(fs::remove(skins / "PlFxNr.dat"));
    CHECK(fs::remove(skins));
  }
  // Reject Akaneia by its disc files, including renamed ISOs and existing directory profiles.
  // A refused pack must not publish any of its menu or costume assets.
  const char* ak_files[] = {"MxDt.dat", "PlSn.dat", "PlTs.dat"};
  for (const char* name : ak_files) { std::ofstream f(root / name); f << "data"; }
  CHECK(!overlay.add_directory(root, "renamed-pack", error));
  CHECK(error.find("Akaneia is disabled") != std::string::npos);
  CHECK(overlay.files().size() == 4 && overlay.iso_reports().size() == 1);
  for (const char* name : ak_files) CHECK(fs::remove(root / name));
  put32(image, 0x428, 75);
  put32(image, 0x50C, 0); put32(image, 0x518, 9); put32(image, 0x524, 18);
  std::memcpy(image.data() + 0x530, "MxDt.dat\0PlSn.dat\0PlTs.dat", 27);
  { std::ofstream f(iso, std::ios::binary); f.write((const char*)image.data(), (std::streamsize)image.size()); }
  CHECK(!overlay.add_iso(iso, "renamed-pack", lookup, base_read, error));
  CHECK(error.find("Akaneia is disabled") != std::string::npos);
  CHECK(overlay.files().size() == 4 && overlay.iso_reports().size() == 1);
  // Full retail images contain movies larger than the replacement-file cap.
  // An unchanged one must be skipped, while a replacement that large is refused.
  const auto large_iso = fs::path(root.string() + "-large.iso");
  const uint32_t movie_size = 256u * 1024u * 1024u + 1;
  std::vector<unsigned char> large_image(0x1000, 0);
  std::memcpy(large_image.data(), disc_id, sizeof disc_id);
  put32(large_image, 0x424, 0x500); put32(large_image, 0x428, 34);
  large_image[0x500] = 1; put32(large_image, 0x508, 2);
  put32(large_image, 0x50C + 4, 0x1000); put32(large_image, 0x50C + 8, movie_size);
  std::memcpy(large_image.data() + 0x518, "movie.mth", 10);
  { std::ofstream f(large_iso, std::ios::binary); f.write((const char*)large_image.data(), large_image.size()); }
  fs::resize_file(large_iso, uint64_t(0x1000) + movie_size);
  auto movie_lookup = [=](const std::string& path, Overlay::DiscFile* file) {
    if (path != "/movie.mth") return false;
    *file = {0x1000, movie_size}; return true;
  };
  auto movie_read = [](uint32_t, void* dst, uint32_t size) { std::memset(dst, 0, size); return true; };
  Overlay movies;
  CHECK(movies.add_iso(large_iso, "unchanged-movie", movie_lookup, movie_read, error));
  CHECK(movies.files().empty());
  auto changed_movie = [](uint32_t, void* dst, uint32_t size) {
    std::memset(dst, 0, size); if (size) *(unsigned char*)dst = 1; return true;
  };
  CHECK(!movies.add_iso(large_iso, "changed-movie", movie_lookup, changed_movie, error));
  CHECK(error.find("replacement file is too large") != std::string::npos);
  CHECK(fs::remove(large_iso));
  CHECK(fs::remove(iso));
  { std::ofstream f(root / "empty.dat", std::ios::binary); }
  CHECK(!overlay.load(root, error));
  CHECK(overlay.files().empty());
  CHECK(fs::remove(root / "empty.dat"));
  CHECK(!overlay.load(root / "missing", error));
  CHECK(fs::remove(root / "PlMrNr.dat"));
  CHECK(fs::remove(root / "audio" / "test.hps"));
  CHECK(fs::remove(root / "audio"));
  CHECK(fs::remove(root));
  std::puts("source mod overlay tests passed");
}
