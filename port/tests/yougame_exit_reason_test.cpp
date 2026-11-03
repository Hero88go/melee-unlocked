// Window-free compatibility policy and overlay refusal checks; no game or real disc required.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_exit_reason.h"
#include "source_mod_overlay.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main() {
  using namespace yougame::mode_policy;
  using namespace source_port::mods;
  // All refusal categories need all three launch gates. None includes ordinary file/disc errors.
  for (auto refusal : {ModRefusal::None, ModRefusal::UnsupportedMod, ModRefusal::UnsupportedVersion,
                       ModRefusal::ModdedDol, ModRefusal::NativeFighters}) {
    for (bool active : {false, true}) for (bool stdio : {false, true}) for (bool explicit_layer : {false, true}) {
      const auto request = mod_exit_request(active, stdio, explicit_layer, refusal);
      CHECK(request.is_object() == (active && stdio && explicit_layer && refusal != ModRefusal::None));
      if (!request.is_null()) {
        CHECK(request.size() == 2);
        CHECK(request.at("type") == "exitReason" && request.at("reason") == "mod_outdated");
        CHECK(!request.count("mod") && !request.count("modVersion"));
      }
    }
  }
  // Every scanner kind: a supported Source/Either mod passes; another engine or unknown version
  // is a compatibility refusal. Bad, missing, wrong-region and retail discs never become updates.
  for (auto kind : {Kind::Te, Kind::TmCe, Kind::Vanilla, Kind::AssetMod, Kind::Mex, Kind::HackPack,
                    Kind::CodeMod, Kind::CardMod, Kind::Unsupported}) {
    for (auto support : {Support::Supported, Support::Untested, Support::NotSupportedYet}) {
      for (auto engine : {Engine::Source, Engine::Recomp, Engine::Either}) {
        Detected mod;
        mod.kind = kind; mod.status = support; mod.needs = engine;
        const bool is_mod = kind != Kind::Unsupported && kind != Kind::Vanilla;
        CHECK((mod_refusal(mod) != ModRefusal::None) ==
              (is_mod && (support != Support::Supported || engine == Engine::Recomp)));
      }
    }
  }
  Detected te;
  te.kind = Kind::Te; te.name = "20XX TE"; te.version = "v2d r3";
  CHECK(mod_refusal(te) == ModRefusal::UnsupportedVersion);
  Detected tmce;
  tmce.kind = Kind::TmCe; tmce.name = "Training Mode CE"; tmce.version = "v1.3";
  CHECK(mod_refusal(tmce) == ModRefusal::UnsupportedVersion);
  Detected maker;
  maker.kind = Kind::CodeMod; maker.name = "Melee Maker"; maker.version = "1.3.7";
  maker.status = Support::Supported; maker.needs = Engine::Recomp;
  CHECK(mod_refusal(maker) == ModRefusal::UnsupportedMod);
  auto request = mod_exit_request(true, true, true, mod_refusal(te), te.name, te.version);
  CHECK(request.at("mod") == "20XX TE" && request.at("modVersion") == "v2d r3");
  CHECK(nlohmann::json::parse(request.dump()) == request);
  request = mod_exit_request(true, true, true, ModRefusal::NativeFighters, "ACE");
  CHECK(request.at("mod") == "ACE" && !request.count("modVersion"));
  request = mod_exit_request(true, true, true, ModRefusal::ModdedDol, "", "1.0");
  CHECK(!request.count("mod") && request.at("modVersion") == "1.0");

  CHECK(exit_reason_field(std::string(60, 'a')) == std::string(60, 'a'));
  CHECK(exit_reason_field(std::string(61, 'a')) == std::string(60, 'a'));
  CHECK(exit_reason_field(std::string(500, 'a')).size() == 60);
  CHECK(exit_reason_field("  Akaneia  ") == "Akaneia");
  CHECK(exit_reason_field(" \n\t\r\x7f\x80\xff ").empty());
  // Exercise all byte values, including embedded NUL, DEL and UTF-8/non-ASCII bytes.
  for (unsigned c = 0; c <= 255; ++c) {
    const std::string value = std::string("A") + char(c) + "B";
    CHECK(exit_reason_field(value) == (c >= 32 && c <= 126 ? value : "AB"));
  }
  for (const auto* link : {"https://bad", "www.bad", "WWW.bad", "w\nw\tw.bad", "x:\r//bad"}) {
    request = mod_exit_request(true, true, true, ModRefusal::UnsupportedMod, link, link);
    CHECK(!request.count("mod") && !request.count("modVersion"));
  }
  CHECK(exit_reason_field(std::string(60, 'a') + "www.bad").empty());
  request = mod_exit_request(true, true, true, ModRefusal::UnsupportedMod, "A\"B\\C", "v1\nv2");
  CHECK(nlohmann::json::parse(request.dump()).at("mod") == "A\"B\\C");
  CHECK(request.at("modVersion") == "v1v2");

  // Real importer calls distinguish native-fighter availability from I/O failures, for both
  // explicit directories and ISOs (including profile layers). No matching on human error prose.
  namespace fs = std::filesystem;
  using Overlay = source_port::ModOverlay;
  const auto root = fs::temp_directory_path() /
      ("mu-exit-reason-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK(fs::create_directory(root));
  for (const char* name : {"mxdt.dat", "plsn.dat", "plts.dat"}) {
    std::ofstream file(root / name, std::ios::binary); file << 'x';
  }
  Overlay overlay;
  std::string error;
  source_port::allow_native_akaneia(false);
  CHECK(!overlay.add_directory(root, "explicit", error));
  CHECK(overlay.native_fighters_refused());
  CHECK(!overlay.add_directory(root / "missing", "missing", error));
  CHECK(!overlay.native_fighters_refused());
  source_port::allow_native_akaneia(true);
  CHECK(overlay.add_directory(root, "supported", error));
  CHECK(!overlay.native_fighters_refused());

  std::vector<unsigned char> image(0x800);
  const unsigned char disc_id[8] = {'G', 'A', 'L', 'E', '0', '1', 0, 2};
  std::memcpy(image.data(), disc_id, sizeof disc_id);
  const auto put32 = [&](size_t at, unsigned value) {
    for (int i = 3; i >= 0; --i) { image[at + i] = (unsigned char)value; value >>= 8; }
  };
  put32(0x424, 0x500); put32(0x428, 75);
  image[0x500] = 1; put32(0x508, 4);
  for (unsigned i = 0; i < 3; ++i) {
    put32(0x50c + i * 12, i * 9);
    put32(0x510 + i * 12, 0x600 + i);
    put32(0x514 + i * 12, 1);
  }
  std::memcpy(image.data() + 0x530, "mxdt.dat\0plsn.dat\0plts.dat", 27);
  const auto iso = root / "test.iso";
  { std::ofstream file(iso, std::ios::binary); file.write((const char*)image.data(), image.size()); }
  const auto lookup = [](const std::string&, Overlay::DiscFile*) { return false; };
  const auto read = [](uint32_t, void*, uint32_t) { return false; };
  Overlay iso_overlay;
  source_port::allow_native_akaneia(false);
  CHECK(!iso_overlay.add_iso(iso, "explicit", lookup, read, error));
  CHECK(iso_overlay.native_fighters_refused());
  CHECK(!iso_overlay.add_iso(root / "missing.iso", "missing", lookup, read, error));
  CHECK(!iso_overlay.native_fighters_refused());
  source_port::allow_native_akaneia(true);
  CHECK(iso_overlay.add_iso(iso, "supported", lookup, read, error));
  CHECK(!iso_overlay.native_fighters_refused());
  source_port::allow_native_akaneia(false);
  fs::remove_all(root);
  std::puts("yougame_exit_reason: policy, sanitized fields and typed overlay refusals ok");
}
