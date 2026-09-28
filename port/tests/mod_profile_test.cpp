// SPDX-License-Identifier: GPL-2.0-or-later
#include "mod_profile.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace fs = std::filesystem;
using namespace source_port::mods;

static void write_gci(const fs::path& path, const char* code, const char* name, char fill) {
  std::vector<char> bytes(64 + 8192, fill);
  std::memset(bytes.data(), 0, 64);
  std::memcpy(bytes.data(), code, 6);
  std::strncpy(bytes.data() + 8, name, 32);
  bytes[0x39] = 1;   // one block
  std::ofstream(path, std::ios::binary).write(bytes.data(), (std::streamsize)bytes.size());
}

int main() {
  const fs::path root = fs::temp_directory_path() /
      ("mu-profile-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK(fs::create_directories(root / "Profiles"));

  // SHA-256 known answer.
  { Sha256 h; h.update("abc"); CHECK(h.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"); }
  { Sha256 h; CHECK(h.hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"); }

  // Profile parsing: order kept, relative paths beside the file, comments, unknown keys refused.
  { std::ofstream f(root / "Profiles" / "Test.ini");
    f << "\xEF\xBB\xBF# a profile\nname = My Pack\niso = \"C:\\Games\\a.iso\"\n\ndir = pack\ngci=save.gci\n"; }
  CHECK(fs::create_directory(root / "Profiles" / "pack"));
  Profile profile; std::string error;
  CHECK(parse_profile(root / "Profiles" / "Test.ini", &profile, &error));
  CHECK(profile.name == "My Pack" && profile.layers.size() == 3);
  CHECK(profile.layers[0].kind == LayerKind::Iso && profile.layers[0].path == fs::path("C:\\Games\\a.iso"));
  CHECK(profile.layers[1].kind == LayerKind::Dir && profile.layers[1].path == root / "Profiles" / "pack");
  CHECK(profile.layers[2].kind == LayerKind::Gci && profile.layers[2].path == fs::path("save.gci"));
  { std::ofstream f(root / "Profiles" / "Bad.ini"); f << "isoo = x\n"; }
  CHECK(!parse_profile(root / "Profiles" / "Bad.ini", &profile, &error) && error.find("unknown key") != std::string::npos);
  CHECK(profile_path("20XX") == fs::path("Mods") / "Profiles" / "20XX.ini");
  CHECK(safe_name("a/b:c. ") == "a_b_c");

  // Profile card: seeded from the ordinary card except the imported file's identity; a changed
  // import sets the profile's saved copy aside.
  const fs::path ordinary = root / "CardA", card = root / "Profiles" / "cardP";
  CHECK(fs::create_directories(ordinary));
  write_gci(ordinary / "GALE01-SuperSmashBros0110290334.gci", "GALE01", "SuperSmashBros0110290334", 'v');
  write_gci(ordinary / "GALE01-other.gci", "GALE01", "other", 'o');
  write_gci(root / "import.gci", "GALE01", "SuperSmashBros0110290334", 'm');
  std::string identity;
  CHECK(gci_identity(root / "import.gci", &identity, &error) && identity == "GALE01/SuperSmashBros0110290334");
  std::vector<CardImport> imports{{root / "import.gci", identity, sha256_file(root / "import.gci")}};
  CHECK(imports[0].sha256.size() == 64);
  std::vector<std::string> notes;
  CHECK(prepare_profile_card(ordinary, card, imports, &notes, &error));
  CHECK(fs::exists(card / "GALE01-other.gci"));
  CHECK(!fs::exists(card / "GALE01-SuperSmashBros0110290334.gci"));   // the import replaces it
  // The game saves: its copy lives in the profile card.
  write_gci(card / "GALE01-SuperSmashBros0110290334.gci", "GALE01", "SuperSmashBros0110290334", 's');
  notes.clear();
  CHECK(prepare_profile_card(ordinary, card, imports, &notes, &error));
  CHECK(fs::exists(card / "GALE01-SuperSmashBros0110290334.gci") && notes.empty());
  // A new version of the import: the saved copy is set aside.
  write_gci(root / "import.gci", "GALE01", "SuperSmashBros0110290334", 'n');
  imports[0].sha256 = sha256_file(root / "import.gci");
  CHECK(prepare_profile_card(ordinary, card, imports, &notes, &error));
  CHECK(!fs::exists(card / "GALE01-SuperSmashBros0110290334.gci"));
  CHECK(fs::exists(card / "GALE01-SuperSmashBros0110290334.gci.bak"));
  // The ordinary card is untouched throughout.
  CHECK(fs::exists(ordinary / "GALE01-SuperSmashBros0110290334.gci"));

  fs::remove_all(root);
  std::puts("mod profile tests passed");
}
