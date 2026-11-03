// Production scanner and overlay checks. Never boots a game.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "mod_scan.h"
#include "layer_fingerprint.h"
#include "test_env.h"
#include "sysgen_paths.h"
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace fs = std::filesystem;
using namespace source_port;
using namespace source_port::mods;
static bool& yougame_active = host::test_env::yougame_session;
// The production native C functions are generated verbatim at configure time.
#include "mu_host.h"
static const MuHostApi* mu_host = nullptr;
static bool native_online = false, native_pending = false, native_replay = false, native_option = false;
static int mu_online_active() { return native_online; }
static int mu_online_pending() { return native_pending; }
static int mu_replay_abi_active() { return native_replay; }
static int mu_option2_offline(unsigned) { return native_option && !native_online && !native_pending; }
static constexpr unsigned MU_TE2_20XX_CPUS = MU_GAME_OPTION2_TE_20XX_CPUS;
#include "mu_native_test_policy.inc"
static void set_env(const char* name, const char* value) {
#ifdef _WIN32
  CHECK(_putenv_s(name, value ? value : "") == 0);
#else
  CHECK(value ? setenv(name, value, 1) == 0 : unsetenv(name) == 0);
#endif
}
static int native_ai_test(const std::string& mode) {
  yougame_active = mode == "--native-ai-yougame";
  host::gameplay::vanilla_only_session = mode == "--native-ai-vanilla";
  MuHostApi api{}; api.version = MU_HOST_API_VERSION; api.test_getenv = host::test_env::getenv; mu_host = &api;
  set_env("MELEE_TEST_20XX_AI", "1");
  const bool restricted = yougame_active || host::gameplay::vanilla_only();
  CHECK(bool(mu_20xx_ai_enabled()) == !restricted);
  native_online = true; CHECK(!mu_20xx_ai_enabled()); native_online = false;
  native_pending = true; CHECK(!mu_20xx_ai_enabled()); native_pending = false;
  native_replay = true; CHECK(!mu_20xx_ai_enabled()); native_replay = false;
  set_env("MELEE_TEST_20XX_AI", nullptr); return 0;
}
static void write(const fs::path& path, const std::vector<uint8_t>& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary); out.write((const char*)bytes.data(), bytes.size()); CHECK(out.good());
}
static uint32_t be(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
static void put(std::vector<uint8_t>& v, size_t at, uint32_t n) {
  for (int i = 3; i >= 0; --i) { v[at+i] = uint8_t(n); n >>= 8; }
}
static std::vector<uint8_t> image() {
  std::vector<uint8_t> v(0x8000);
  std::memcpy(v.data(), "GALE01", 6); v[7] = 2; put(v, 0x1c, 0xc2339f3d);
  put(v, 0x420, 0x3000); put(v, 0x424, 0x3800); put(v, 0x428, 0x40);
  put(v, 0x3000, 0x100); put(v, 0x3090, 16);
  put(v, 0x3800, 0x01000000); put(v, 0x3808, 2);
  put(v, 0x380c, 1); put(v, 0x3810, 0x6000); put(v, 0x3814, 5);
  std::memcpy(v.data()+0x3819, "a.dat", 6); std::memcpy(v.data()+0x6000, "hello", 5);
  return v;
}
static void environment_tests() {
  set_env("MELEE_TEST_20XX_AI", "1"); set_env("MELEE_SNAPSHOT_EXCLUDE", "1-2");
  CHECK(std::strcmp(host::test_env::getenv("MELEE_TEST_20XX_AI"), "1") == 0);
  CHECK(host::test_env::getenv("MELEE_SNAPSHOT_EXCLUDE"));
  yougame_active = true;
  CHECK(!host::test_env::getenv("MELEE_TEST_20XX_AI")); CHECK(!host::test_env::getenv("MELEE_SNAPSHOT_EXCLUDE"));
  yougame_active = false; host::gameplay::vanilla_only_session = true;
  CHECK(!host::test_env::getenv("MELEE_TEST_20XX_AI")); CHECK(!host::test_env::getenv("MELEE_SNAPSHOT_EXCLUDE"));
  host::gameplay::vanilla_only_session = false;
  CHECK(host::test_env::getenv("MELEE_TEST_20XX_AI"));
  set_env("MELEE_TEST_20XX_AI", nullptr); set_env("MELEE_SNAPSHOT_EXCLUDE", nullptr);
}
static void path_tests(const fs::path& root) {
  using host::sysgen::resolve_ancestors; using host::sysgen::outside_build;
  const auto build = root / "build", data = root / "data";
  fs::create_directories(build / "sub"); fs::create_directories(data);
  std::error_code ec;
  const auto physical = resolve_ancestors(root, ec); CHECK(!ec);
  const auto relative = root.lexically_relative(fs::current_path()) / "data/../new/missing";
  CHECK(resolve_ancestors(relative, ec) == physical / "new/missing" && !ec);
  CHECK(resolve_ancestors(data / "./../build/sub/..", ec) == physical / "build" && !ec);
  CHECK(outside_build(build, data / "missing/file", ec) && !ec);
  CHECK(!outside_build(build, build / "missing/file", ec) && !ec);
  CHECK(!outside_build(build, build / "sub", ec) && !ec);
  // Same-name prefix is outside; compare components/identities, never string prefixes.
  fs::create_directories(root / "build-other");
  CHECK(outside_build(build, root / "build-other/file", ec) && !ec);
  fs::create_directory_symlink(build / "sub", data / "into-build", ec);
  // Creating links may require Developer Mode on Windows; the lead also runs these on macOS.
#ifdef _WIN32
  if (ec) { std::cout << "symlink fixtures unavailable: " << ec.message() << '\n'; return; }
#else
  CHECK(!ec);
#endif
  CHECK(resolve_ancestors(data / "into-build/new/file", ec) == physical / "build/sub/new/file" && !ec);
  CHECK(!outside_build(build, data / "into-build/new/file", ec) && !ec);
#ifdef _WIN32
  // Windows collapses ".." by the path text before a link is followed (GetFullPathNameW, and
  // CreateFileW does the same), so a write to into-build/../file really lands in data/file.
  CHECK(resolve_ancestors(data / "into-build/../file", ec) == physical / "data/file" && !ec);
#else
  CHECK(resolve_ancestors(data / "into-build/../file", ec) == physical / "build/file" && !ec);
#endif
  fs::create_directory_symlink(data, build / "out", ec); CHECK(!ec);
  CHECK(outside_build(build, build / "out/new/file", ec) && !ec);
  fs::create_directory_symlink(build, root / "build-alias", ec); CHECK(!ec);
  CHECK(!outside_build(root / "build-alias", build / "new/file", ec) && !ec);
  fs::create_directory_symlink(build / "missing", data / "dangling", ec); CHECK(!ec);
  CHECK(!outside_build(build, data / "dangling/file", ec) && bool(ec));
  fs::create_symlink("loop", data / "loop", ec); CHECK(!ec);
  CHECK(resolve_ancestors(data / "loop/file", ec).empty() && bool(ec));
}
static void overlay_tests(const fs::path& root) {
  const auto iso = root / "layer.iso"; auto v = image(); write(iso, v);
  const auto stamp = fs::last_write_time(iso);
  auto fingerprint = [&] {
    ModOverlay overlay; std::string error;
    CHECK(overlay.add_iso(iso, "test", [](const std::string&, ModOverlay::DiscFile*) { return false; },
                          [](uint32_t, void*, uint32_t) { return false; }, error));
    return layer_fingerprint(overlay, "test", [] { return Sha256{}; });
  };
  const auto first = fingerprint(); CHECK(first.size() == 64);
  const auto cache = root / "Mods/.cache/fingerprints.txt";
  fs::create_directories(cache.parent_path());
  const std::string record = fs::absolute(iso).u8string() + "|" + std::to_string(fs::file_size(iso)) + "|" +
      std::to_string((long long)stamp.time_since_epoch().count()) + "\t" + first + "\n";
  { std::ofstream out(cache); out << record; }
  v[0x6000] ^= 1; write(iso, v); fs::last_write_time(iso, stamp);
  CHECK(fingerprint() != first); // identical path, size, mtime and cached fingerprint
  std::ifstream in(cache); CHECK(std::string(std::istreambuf_iterator<char>(in), {}) == record);
  fs::remove(iso);
}
static std::string content_key(const fs::path& iso) {
  return "yougame-gc-content-2|" + fs::absolute(iso).u8string() + "|" + std::to_string(fs::file_size(iso)) + "|" +
      std::to_string((int64_t)fs::last_write_time(iso).time_since_epoch().count());
}
static double timed_verdict(const fs::path& iso, const ScanOptions& options, bool fresh) {
  const auto begin = std::chrono::steady_clock::now();
  const auto d = fresh ? identify_vanilla(iso, options) : identify(iso, options);
  CHECK(d.kind == Kind::Vanilla);
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-begin).count();
}
static std::map<std::string, ModOverlay::DiscFile> disc_files(const fs::path& iso) {
  std::ifstream in(iso, std::ios::binary); uint8_t boot[0x440];
  CHECK(bool(in.read((char*)boot, sizeof boot)));
  const uint32_t offset = be(boot+0x424), size = be(boot+0x428);
  CHECK(size >= 12 && size <= 4*1024*1024);
  std::vector<uint8_t> fst(size); in.seekg(offset); CHECK(bool(in.read((char*)fst.data(), size)));
  const uint32_t count = be(fst.data()+8); CHECK(count*12ull <= fst.size());
  std::vector<std::pair<uint32_t, std::string>> dirs{{count, ""}};
  std::map<std::string, ModOverlay::DiscFile> files;
  for (uint32_t i = 1; i < count; ++i) {
    while (dirs.size() > 1 && i >= dirs.back().first) dirs.pop_back();
    const auto* entry = fst.data()+i*12;
    const auto at = count*12 + (be(entry)&0xffffff); CHECK(at < fst.size());
    const auto end = std::find(fst.begin()+at, fst.end(), 0); CHECK(end != fst.end());
    std::string name(fst.begin()+at, end);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const auto path = dirs.back().second + "/" + name;
    if (entry[0]) dirs.emplace_back(be(entry+8), path);
    else files[path] = {be(entry+4), be(entry+8)};
  }
  return files;
}
static int real_disc(const fs::path& root) {
  const char* dir = std::getenv("MU_REAL_DISC_DIR");
  if (!dir || !*dir) { std::cout << "MU_REAL_DISC_DIR unset; skipping real disc checks\n"; return 77; }
  ScanOptions options; options.mods_dir = root / "Mods";
  const auto cache = options.mods_dir / ".cache/fingerprints.txt";
  fs::create_directories(cache.parent_path());
  unsigned count = 0;
  for (const auto& entry : fs::directory_iterator(fs::u8path(dir))) {
    if (entry.path().extension() != ".iso") continue;
    ++count; const auto iso = entry.path();
    fs::remove(cache);
    const double absent = timed_verdict(iso, options, true);
    // The pre-fix parent default options populate and then trust this cache.
    timed_verdict(iso, options, false);
    const double before = timed_verdict(iso, options, false);
    const double present = timed_verdict(iso, options, true);
    { std::ofstream out(cache); out << content_key(iso) << '\t' << std::string(40, '0') << '\n'; }
    const double forged = timed_verdict(iso, options, true);
    std::cout << iso.filename() << " A before_cached_ms=" << before << " after_present_ms=" << present
              << " added_ms=" << present-before << " absent_ms=" << absent << " forged_ms=" << forged << '\n';
    // Real-disc layer benchmark: expose every file within the importer's replacement-size limit.
    // Oversized retail movies stay on the base disc, as in an actual mod launch.
    const auto files = disc_files(iso);
    std::ifstream base(iso, std::ios::binary);
    ModOverlay layer; std::string layer_error;
    const bool loaded = layer.add_iso(iso, "real", [&](const std::string& path, ModOverlay::DiscFile* out) {
      const auto found = files.find(path); CHECK(found != files.end());
      *out = found->second; return out->length > 256u*1024u*1024u;
    }, [&](uint32_t off, void* out, uint32_t n) {
      base.clear(); base.seekg(off); return bool(base.read((char*)out, n));
    }, layer_error);
    if (!loaded) std::cerr << layer_error << '\n';
    CHECK(loaded);
    const auto started = std::chrono::steady_clock::now();
    const auto identity = layer_fingerprint(layer, "real", [] { return Sha256{}; });
    const double layer_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-started).count();
    CHECK(identity.size() == 64);
    // Time the old matching-entry return, with exactly the same cache record layout.
    const std::string layer_key = fs::absolute(iso).u8string() + "|" + std::to_string(fs::file_size(iso)) + "|" +
        std::to_string((long long)fs::last_write_time(iso).time_since_epoch().count());
    { std::ofstream out(cache); out << layer_key << '\t' << identity << '\n'; }
    const auto cache_start = std::chrono::steady_clock::now();
    std::ifstream cached(cache); std::string line, old_identity;
    while (std::getline(cached, line)) {
      const auto tab = line.rfind('\t');
      if (tab == layer_key.size() && line.compare(0, tab, layer_key) == 0) { old_identity = line.substr(tab+1); break; }
    }
    const double cached_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-cache_start).count();
    CHECK(old_identity == identity);
    uint64_t layer_bytes = 0; for (const auto& file : layer.files()) layer_bytes += file.length;
    std::cout << iso.filename() << " B before_cached_ms=" << cached_ms << " after_hash_ms=" << layer_ms
              << " added_ms=" << layer_ms-cached_ms << " bytes=" << layer_bytes << '\n';
    // A small sparse fixture uses this disc's authentic retail DOL and altered DAT bytes.
    // This reaches the exact vulnerable branch without copying another full disc.
    std::ifstream source(iso, std::ios::binary); uint8_t boot[0x440];
    CHECK(bool(source.read((char*)boot, sizeof boot)));
    std::vector<uint8_t> fixture(0x500000); auto tiny = image();
    std::copy(tiny.begin(), tiny.end(), fixture.begin());
    const uint32_t dol_at = be(boot+0x420), size = 0x4385e0;
    put(fixture, 0x420, 0x8000); source.seekg(dol_at);
    CHECK(bool(source.read((char*)fixture.data()+0x8000, size)));
    const auto modified = root / "modified.iso"; write(modified, fixture);
    const auto time = fs::last_write_time(modified);
    { std::ofstream out(cache); out << content_key(modified) << "\t4852be98b97acd8def716d11a5b781f24f9fcfab\n"; }
    CHECK(identify(modified, options).kind == Kind::Vanilla); // reproduce the previous vulnerability
    CHECK(identify_vanilla(modified, options).kind != Kind::Vanilla);
    CHECK(fs::last_write_time(modified) == time);
    fs::remove(modified);
  }
  CHECK(count > 0); std::cout << "real discs verified: " << count << '\n'; return 0;
}
int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]).find("--native-ai-") == 0) return native_ai_test(argv[1]);
  const bool real = argc == 2 && std::string(argv[1]) == "--real-disc";
  // Real-disc fixtures stay on the external volume, including the small DOL fixture.
  const auto base = real ? fs::path(MU_INTEGRITY_FIXTURE_DIR) : fs::temp_directory_path();
  const auto root = base / ("mu-integrity-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root);
  int result = 0;
  if (real) result = real_disc(root);
  else { environment_tests(); path_tests(root); overlay_tests(root); }
  fs::remove_all(root); return result;
}
