// Mods drop-in scanner: recognition by content, first-found auto-enable, choices that stick, and the
// memory card folder guard. Synthetic discs only (no game data); the official 20XX TE save is used
// when the repository's research copy is present, and those checks are skipped otherwise.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include "mod_scan.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace source_port::mods;

static int g_failures = 0;
static void check(bool ok, const std::string& what) {
  std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
  if (!ok) ++g_failures;
}

static void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
  v[at] = (uint8_t)(x >> 24); v[at + 1] = (uint8_t)(x >> 16); v[at + 2] = (uint8_t)(x >> 8); v[at + 3] = (uint8_t)x;
}

// A tiny GameCube disc image: header, a fake main.dol, a filesystem table, and the named files.
static void write_disc(const fs::path& path, const char* id, uint8_t revision, const char* title,
                       const std::vector<std::pair<std::string, std::string>>& files, uint8_t dol_fill = 0) {
  std::vector<uint8_t> disc(0x8000, 0);
  std::memcpy(disc.data(), id, 6);
  disc[7] = revision;
  put32(disc, 0x1C, 0xC2339F3Du);
  std::strncpy((char*)disc.data() + 0x20, title, 0x3D);
  put32(disc, 0x420, 0x2000);                  // main.dol: its section table says 0x100 bytes
  for (int i = 0; i < 0x100; ++i) disc[0x2000 + i] = dol_fill;
  put32(disc, 0x2000 + 0, 0x100); put32(disc, 0x2000 + 0x90, 0x100);
  // Filesystem table at 0x3000: root + files, then the name table.
  const uint32_t count = (uint32_t)files.size() + 1;
  std::vector<uint8_t> fst(count * 12, 0);
  std::string names(1, '\0');
  uint32_t data_at = 0x4000;
  fst[0] = 1; put32(fst, 8, count);
  std::vector<uint8_t> blob;
  for (size_t i = 0; i < files.size(); ++i) {
    const uint32_t name_offset = (uint32_t)names.size();
    names += files[i].first; names += '\0';
    put32(fst, (i + 1) * 12, name_offset);
    put32(fst, (i + 1) * 12 + 4, data_at + (uint32_t)blob.size());
    put32(fst, (i + 1) * 12 + 8, (uint32_t)files[i].second.size());
    blob.insert(blob.end(), files[i].second.begin(), files[i].second.end());
  }
  fst.insert(fst.end(), names.begin(), names.end());
  put32(disc, 0x424, 0x3000); put32(disc, 0x428, (uint32_t)fst.size());
  std::memcpy(disc.data() + 0x3000, fst.data(), fst.size());
  disc.insert(disc.end(), blob.begin(), blob.end());
  disc.resize(disc.size() + 0x800, 0);
  std::ofstream(path, std::ios::binary).write((const char*)disc.data(), (std::streamsize)disc.size());
}

static std::vector<uint8_t> read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Rewrites logical block `id` in every copy of the save: `edit` changes its decrypted contents.
template <class Edit> static void edit_section(std::vector<uint8_t>& gci, int id, Edit edit) {
  for (uint32_t k = 1; k < 11; ++k) {
    uint8_t* block = gci.data() + 64 + (size_t)k * 0x2000;
    std::vector<uint8_t> copy(block, block + 0x2000);
    if (!decrypt_block(copy.data(), copy.size())) continue;
    if (((copy[0x10] << 8) | copy[0x11]) != id) continue;
    edit(copy.data() + 0x20);
    encrypt_block(copy.data(), copy.size());
    std::memcpy(block, copy.data(), copy.size());
  }
}

static Detected find(const ScanResult& r, Kind kind) {
  for (const auto& d : r.items) if (d.kind == kind) return d;
  return Detected{};
}

int main(int argc, char** argv) {
  // Optional field validation against private discs, never required in CI.
  if (argc == 3) {
    ScanOptions options;
    options.base_iso = fs::u8path(argv[1]);
    const auto d = identify(fs::u8path(argv[2]), options);
    std::cout << kind_id(d.kind) << " | " << d.name << " | " << engine_id(d.needs)
              << " | " << support_id(d.status) << " | " << d.hash << "\n";
    return d.kind == Kind::AssetMod && d.needs == Engine::Either &&
           d.status == Support::Supported && d.can_enable ? 0 : 1;
  }
  const fs::path root = fs::temp_directory_path() / ("mu-mod-scan-test-" + std::to_string(GetCurrentProcessId()));
  fs::remove_all(root);
  fs::create_directories(root / "Mods" / "Discs");

  // ---- 20XX TE by its payload, not the whole file ----
  const fs::path te_source = fs::path(MU_REPO_ROOT) / "run-source/20xxte-official/20XXTE-v2d-r4-USA.gci";
  if (fs::exists(te_source)) {
    const auto pristine = read_file(te_source);
    const TeCheck a = check_te(pristine);
    check(a.melee_save && a.te && a.supported(), "the official 20XX TE save is recognized (" + a.version + ")");
    check(a.version == "v2d r4", "its version reads v2d r4");

    auto played = pristine;   // Melee rewrote records and settings after some matches
    edit_section(played, 1, [](uint8_t* data) { for (int i = 0x100; i < 0x400; ++i) data[i] ^= 0x5A; });
    check(played != pristine, "the played copy differs byte for byte");
    const TeCheck b = check_te(played);
    check(b.te && b.supported(), "a played 20XX TE save (records changed) is still recognized");

    auto renamed = played;    // and the player made a name tag in the last slot of one bank
    edit_section(renamed, 8, [](uint8_t* data) { for (int i = 0; i < 0x1A4; ++i) data[0x1F2C - 0x1A4 + i] = (uint8_t)i; });
    check(check_te(renamed).supported(), "one rewritten name tag slot still leaves it recognized");

    auto normal = pristine;   // a normal Melee save: the name tag banks hold no payload
    for (int id = 2; id <= 8; ++id) edit_section(normal, id, [](uint8_t* data) { std::memset(data, 0, 0x1F2C); });
    const TeCheck c = check_te(normal);
    check(c.melee_save && !c.te, "a normal Melee save is not taken for 20XX TE");

    std::ofstream(root / "Mods" / "anything.gci", std::ios::binary).write((const char*)played.data(), (std::streamsize)played.size());
    std::ofstream(root / "Mods" / "other-hack.gci", std::ios::binary).write((const char*)normal.data(), (std::streamsize)normal.size());
  } else {
    std::cout << "skip  20XX TE checks (research copy not present)\n";
  }

  // ---- discs by filesystem and main.dol ----
  write_disc(root / "Mods" / "Discs" / "tm.iso", "GTME01", 2, "Training Mode", {{"TM", ""}, {"eventMenu.dat", "....TM-CE v1.4 d1\0"}});
  // The table above puts eventMenu.dat at the root; Training Mode CE keeps it in /TM, which the
  // scanner requires. Write the real layout instead:
  {
    // root(3 entries): TM dir (entries 1..2), eventMenu.dat inside it.
    std::vector<uint8_t> disc(0x8000, 0);
    std::memcpy(disc.data(), "GTME01", 6); disc[7] = 2; put32(disc, 0x1C, 0xC2339F3Du);
    put32(disc, 0x420, 0x2000); put32(disc, 0x2000, 0x100); put32(disc, 0x2000 + 0x90, 0x100);
    const std::string text = std::string("....TM-CE v1.4 d1") + '\0';
    std::vector<uint8_t> fst(3 * 12, 0);
    fst[0] = 1; put32(fst, 8, 3);
    put32(fst, 12, 0x01000001u); put32(fst, 12 + 4, 0); put32(fst, 12 + 8, 3);   // directory flag + name offset
    put32(fst, 24, 4); put32(fst, 24 + 4, 0x4000); put32(fst, 24 + 8, (uint32_t)text.size());
    const std::string names = std::string("\0TM\0eventMenu.dat\0", 18);
    fst.insert(fst.end(), names.begin(), names.end());
    put32(disc, 0x424, 0x3000); put32(disc, 0x428, (uint32_t)fst.size());
    std::memcpy(disc.data() + 0x3000, fst.data(), fst.size());
    std::memcpy(disc.data() + 0x4000, text.data(), text.size());
    std::ofstream(root / "Mods" / "Discs" / "tm.iso", std::ios::binary).write((const char*)disc.data(), (std::streamsize)disc.size());
  }
  write_disc(root / "Mods" / "Discs" / "akaneia-like.iso", "GALE01", 2, "Super Smash Bros Melee", {{"MxDt.dat", "mex data"}}, 0x11);
  write_disc(root / "Mods" / "Discs" / "codes.iso", "GALE01", 2, "Code Mod Build", {{"codes.gct", "gecko"}}, 0x22);
  write_disc(root / "Mods" / "Discs" / "pal.iso", "GALP01", 0, "Melee PAL", {{"a.dat", "x"}});

  ScanOptions options;
  options.mods_dir = root / "Mods";
  options.use_cache = true;
  std::map<std::string, int> choices;
  const ScanResult first = scan(options, &choices);
  for (const auto& d : first.items) std::cout << "      " << d.path.filename().u8string() << ": " << kind_id(d.kind) << " | " << d.message << "\n";

  const Detected tm = find(first, Kind::TmCe);
  check(tm.kind == Kind::TmCe && tm.version == "v1.4 d1" && tm.status == Support::Supported, "Training Mode CE is recognized with its version");
  check(tm.enabled, "Training Mode CE is turned on the first time it is found");
  check(choices.count("tmce") && choices["tmce"] == 1, "that first-found choice is recorded");
  const Detected mex = find(first, Kind::Mex);
  check(mex.kind == Kind::Mex && mex.needs == Engine::Recomp, "an m-ex disc (MxDt.dat) is recognized and needs the Static Recomp");
  check(mex.name == "akaneia-like" && mex.enabled && mex.status == Support::Untested,
        "an unknown m-ex pack has its own name, turns on, and retains its untested status");
  const Detected code = find(first, Kind::CodeMod);
  check(code.kind == Kind::CodeMod && code.needs == Engine::Recomp, "a disc with its own codes is a code mod for the Static Recomp");
  check(code.status == Support::NotSupportedYet && !code.can_enable && !code.enabled,
        "an unverified code mod is found but not offered as playable");
  check(mex.catalog_id.empty(), "a named file does not give an unknown m-ex disc an official catalog ID");
  check(tm.catalog_id == "tmce", "a content-recognized pack has its stable catalog ID");
  bool pal_refused = false;
  for (const auto& d : first.items) if (d.path.filename() == "pal.iso") pal_refused = d.kind == Kind::Unsupported && !d.message.empty();
  check(pal_refused, "another region of Melee is refused with a reason");
  if (fs::exists(te_source)) {
    const Detected te = find(first, Kind::Te);
    check(te.kind == Kind::Te && te.enabled && te.needs == Engine::Source, "20XX TE in Mods is recognized and enabled for its verified native engine");
    check(static_recomp_card(first, root / "User/GC/CardA", nullptr).empty(),
          "Static does not mount a save exploit without a verified execution path");
    const Detected card = find(first, Kind::CardMod);
    check(card.kind == Kind::CardMod && card.needs == Engine::Recomp && !card.enabled,
          "another Melee save is found and remains off");
    check(!card.can_enable && card.status == Support::NotSupportedYet,
          "unknown Melee saves are not offered as playable without a card-load guard");
  }

  // The player turns Training Mode CE off: a rescan keeps it off.
  choices["tmce"] = 0;
  const ScanResult second = scan(options, &choices);
  check(!find(second, Kind::TmCe).enabled, "turned off stays off after a rescan");
  check(second.cached >= 3 && second.hashed == 0, "an unchanged folder is not read again (cache)");
  check(parse_choice("mod_tmce_enabled", "1", &choices) && choices["tmce"] == 1, "the settings key mod_tmce_enabled parses");
  check(choice_lines(choices).find("mod_tmce_enabled 1") != std::string::npos, "and is written back");
  check(write_detected_json(root / "Mods" / ".cache" / "detected.json", second, true) &&
            fs::file_size(root / "Mods" / ".cache" / "detected.json") > 50,
        "detected.json is written for the launcher");

  // Compare game files rather than ISO layout, and distinguish data changes from code changes.
  write_disc(root / "base.iso", "GALE01", 2, "Super Smash Bros Melee", {{"a.dat", "original"}});
  write_disc(root / "Mods/Discs/assets.iso", "GALE01", 2, "Visual pack", {{"a.dat", "replacement data"}});
  write_disc(root / "Mods/Discs/retail.iso", "GALE01", 2, "Super Smash Bros Melee", {{"a.dat", "original"}});
  ScanOptions compared = options;
  compared.base_iso = root / "base.iso";
  compared.use_cache = false;
  const auto assets = identify(root / "Mods/Discs/assets.iso", compared);
  check(assets.kind == Kind::AssetMod && assets.status == Support::Supported && assets.can_enable,
        "an ISO with the same code and changed assets is a supported data-only mod");
  check(assets.needs == Engine::Either, "verified data-only ISOs are offered on both engines");
  const auto retail = identify(root / "Mods/Discs/retail.iso", compared);
  check(retail.kind == Kind::Vanilla && !retail.can_enable,
        "an ISO with identical game files is retail even without the official whole-disc hash");
  check(runs_on(tm, true) && !runs_on(tm, false), "Training Mode CE is enabled only on the Source Port");
  check(runs_on(mex, false) && !runs_on(mex, true), "m-ex mods are enabled only on the Static Recomp");
  check(hack_pack_version("d926ba5b39551f5245fd655bc1dfeb3f") == "5.0.2" &&
        hack_pack_version("17756217401a39227feea1ebb8542376") == "4.07++",
        "current and legacy official Hack Pack fingerprints are recognized");
  check(hack_pack_version("d926ba5b39551f5245fd655bc1dfeb30").empty(),
        "a near-match fingerprint is not identified as a known Hack Pack");

  write_disc(root / "external asset.iso", "GALE01", 2, "Linked visual pack", {{"a.dat", "external replacement"}});
  {
    std::ofstream links(root / "Mods/links.txt");
    links << "# Player files can stay outside Mods\n../external asset.iso\n\""
          << (root / "external asset.iso").u8string() << "\"\nDiscs/assets.iso\nmissing.iso\n";
  }
  const auto linked = scan(compared, &choices);
  int external_count = 0, inside_count = 0;
  for (const auto& d : linked.items) {
    external_count += d.path.filename() == "external asset.iso";
    inside_count += d.path.filename() == "assets.iso";
  }
  check(external_count == 1 && inside_count == 1, "links.txt scans outside packs and deduplicates existing/linked paths");
  check(!linked.notes.empty() && fs::exists(root / "external asset.iso"), "a missing link is reported while originals remain untouched");

  // ---- the memory card folder guard ----
  if (fs::exists(te_source)) {
    const fs::path card = root / "User" / "GC" / "CardA";
    fs::create_directories(card);
    auto normal = read_file(root / "Mods" / "other-hack.gci");
    std::ofstream(card / "01-GALE-SuperSmashBros0110290334.gci", std::ios::binary).write((const char*)normal.data(), (std::streamsize)normal.size());
    fs::copy_file(te_source, card / "20XXTE.gci");
    std::vector<std::string> notes, log;
    const auto moves = protect_card_folder(card, root / "Mods" / "Saves", &notes, &log);
    check(moves.size() == 1 && moves[0].te, "the 20XX TE save dropped next to the player's own is moved out");
    check(fs::exists(card / "01-GALE-SuperSmashBros0110290334.gci") && !fs::exists(card / "20XXTE.gci"),
          "the player's own save stays in the card folder");
    check(fs::exists(root / "Mods" / "Saves" / "20XXTE.gci"), "and 20XX TE lands in Mods/Saves, nothing deleted");
  }

  fs::remove_all(root);
  if (g_failures) { std::cout << "FAILED: " << g_failures << "\n"; return 1; }
  std::cout << "PASS: mods drop-in scanner\n";
  return 0;
}
