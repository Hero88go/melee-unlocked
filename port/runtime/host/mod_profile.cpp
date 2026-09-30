// SPDX-License-Identifier: GPL-2.0-or-later
#include "mod_profile.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <commdlg.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

#pragma comment(lib, "bcrypt.lib")

namespace source_port::mods {
namespace fs = std::filesystem;

namespace {
std::string trim(std::string s) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return s;
}
}  // namespace

fs::path profile_path(const std::string& name_or_file) {
  fs::path p = fs::u8path(name_or_file);
  if (lower(p.extension().u8string()) == ".ini") return p;
  return fs::path("Mods") / "Profiles" / fs::u8path(name_or_file + ".ini");
}

std::string safe_name(const std::string& name) {
  std::string out;
  for (unsigned char c : name)
    out += (std::isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.') ? char(c) : '_';
  out = trim(out);
  while (!out.empty() && out.back() == '.') out.pop_back();   // Windows drops trailing dots
  return out.empty() ? std::string("profile") : out;
}

bool parse_profile(const fs::path& file, Profile* out, std::string* error) {
  std::ifstream in(file);
  if (!in) { *error = "cannot open mod profile " + file.u8string(); return false; }
  Profile profile;
  profile.name = file.stem().u8string();
  std::string line;
  int number = 0;
  while (std::getline(in, line)) {
    ++number;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (number == 1 && line.size() >= 3 && (uint8_t)line[0] == 0xEF && (uint8_t)line[1] == 0xBB && (uint8_t)line[2] == 0xBF)
      line.erase(0, 3);
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      *error = file.u8string() + " line " + std::to_string(number) + ": expected key = value"; return false;
    }
    const std::string key = lower(trim(line.substr(0, eq)));
    std::string value = trim(line.substr(eq + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
    if (key == "name") { profile.name = value; continue; }
    LayerKind kind;
    if (key == "iso") kind = LayerKind::Iso;
    else if (key == "dir") kind = LayerKind::Dir;
    else if (key == "gci") kind = LayerKind::Gci;
    else { *error = file.u8string() + " line " + std::to_string(number) + ": unknown key '" + key + "'"; return false; }
    fs::path path = fs::u8path(value);
    if (path.is_relative()) {
      const fs::path beside = file.parent_path() / path;
      std::error_code ec;
      if (fs::exists(beside, ec)) path = beside;
    }
    profile.layers.push_back({kind, path});
  }
  if (profile.layers.empty()) { *error = "mod profile " + file.u8string() + " lists no layers"; return false; }
  *out = std::move(profile);
  return true;
}

Sha256::Sha256() {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return;
  DWORD object_size = 0, got = 0;
  if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&object_size, sizeof object_size, &got, 0) < 0) {
    BCryptCloseAlgorithmProvider(algorithm, 0); return;
  }
  object_.resize(object_size);
  BCRYPT_HASH_HANDLE hash = nullptr;
  if (BCryptCreateHash(algorithm, &hash, object_.data(), object_size, nullptr, 0, 0) < 0) {
    BCryptCloseAlgorithmProvider(algorithm, 0); return;
  }
  algorithm_ = algorithm; hash_ = hash;
}
Sha256::~Sha256() {
  if (hash_) BCryptDestroyHash((BCRYPT_HASH_HANDLE)hash_);
  if (algorithm_) BCryptCloseAlgorithmProvider((BCRYPT_ALG_HANDLE)algorithm_, 0);
}
void Sha256::update(const void* data, size_t size) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  while (hash_ && size) {
    const ULONG n = (ULONG)std::min<size_t>(size, 1u << 30);
    if (BCryptHashData((BCRYPT_HASH_HANDLE)hash_, const_cast<PUCHAR>(p), n, 0) < 0) {
      BCryptDestroyHash((BCRYPT_HASH_HANDLE)hash_); hash_ = nullptr; return;
    }
    p += n; size -= n;
  }
}
std::string Sha256::hex() {
  if (!hash_) return {};
  uint8_t digest[32];
  const bool ok = BCryptFinishHash((BCRYPT_HASH_HANDLE)hash_, digest, sizeof digest, 0) >= 0;
  BCryptDestroyHash((BCRYPT_HASH_HANDLE)hash_); hash_ = nullptr;
  if (!ok) return {};
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (uint8_t b : digest) { out += digits[b >> 4]; out += digits[b & 15]; }
  return out;
}

std::string sha256_file(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return {};
  Sha256 hash;
  std::vector<char> buffer(1 << 20);
  while (in) {
    in.read(buffer.data(), (std::streamsize)buffer.size());
    if (in.gcount() > 0) hash.update(buffer.data(), (size_t)in.gcount());
  }
  if (!in.eof()) return {};
  return hash.hex();
}

bool gci_identity(const fs::path& file, std::string* identity, std::string* error) {
  std::ifstream in(file, std::ios::binary);
  uint8_t header[64]{};
  if (!in || !in.read((char*)header, sizeof header)) { *error = "cannot read card file header " + file.u8string(); return false; }
  *identity = std::string((const char*)header, 6) + "/" +
              std::string((const char*)header + 8, strnlen((const char*)header + 8, 32));
  return true;
}

bool prepare_profile_card(const fs::path& ordinary_card, const fs::path& profile_card,
                          const std::vector<CardImport>& imports,
                          std::vector<std::string>* notes, std::string* error) {
  std::error_code ec;
  const bool fresh = !fs::exists(profile_card, ec);
  fs::create_directories(profile_card, ec);
  if (ec) { *error = "cannot create profile card folder " + profile_card.u8string(); return false; }
  auto imported = [&](const std::string& identity) {
    return std::any_of(imports.begin(), imports.end(), [&](const CardImport& i) { return i.identity == identity; });
  };
  if (fresh) {
    uint32_t copied = 0;
    for (const auto& entry : fs::directory_iterator(ordinary_card, ec)) {
      if (ec || lower(entry.path().extension().u8string()) != ".gci") continue;
      std::string identity, why;
      if (!gci_identity(entry.path(), &identity, &why) || imported(identity)) continue;
      fs::copy_file(entry.path(), profile_card / entry.path().filename(), fs::copy_options::skip_existing, ec);
      if (!ec) ++copied;
      ec.clear();
    }
    notes->push_back("new profile card, " + std::to_string(copied) + " files copied from the ordinary card");
  }
  // imports.txt: "<identity>\t<sha256>" of the import each saved copy came from.
  const fs::path record = profile_card / "imports.txt";
  std::map<std::string, std::string> seen;
  {
    std::ifstream in(record);
    std::string line;
    while (std::getline(in, line)) {
      const auto tab = line.find('\t');
      if (tab != std::string::npos) seen[line.substr(0, tab)] = line.substr(tab + 1);
    }
  }
  bool changed = false;
  for (const auto& import : imports) {
    auto it = seen.find(import.identity);
    if (it != seen.end() && it->second == import.sha256) continue;
    if (it != seen.end()) {
      // A different version of this import: the profile's saved copy belongs to the old one.
      for (const auto& entry : fs::directory_iterator(profile_card, ec)) {
        if (ec || lower(entry.path().extension().u8string()) != ".gci") continue;
        std::string identity, why;
        if (!gci_identity(entry.path(), &identity, &why) || identity != import.identity) continue;
        fs::path bak = entry.path(); bak += ".bak";
        fs::remove(bak, ec); ec.clear();
        fs::rename(entry.path(), bak, ec);
        notes->push_back("imported " + import.source.filename().u8string() + " changed; previous saved copy kept as " +
                         bak.filename().u8string());
        ec.clear();
      }
    }
    seen[import.identity] = import.sha256;
    changed = true;
  }
  if (changed) {
    std::ofstream out(record, std::ios::trunc);
    for (const auto& kv : seen) out << kv.first << '\t' << kv.second << '\n';
  }
  return true;
}

Status& status() {
  static Status value;
  return value;
}

std::vector<std::string> list_profiles() {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(fs::path("Mods") / "Profiles", ec)) {
    if (ec) break;
    if (entry.is_regular_file(ec) && lower(entry.path().extension().u8string()) == ".ini")
      names.push_back(entry.path().stem().u8string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::string choose_mod_file() {
  wchar_t file[32768]{};
  OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof dialog;
  dialog.hwndOwner = GetActiveWindow();
  // The file's content decides what it is; the last entry lets a renamed file be picked too.
  dialog.lpstrFilter = L"Mods (*.iso;*.gcm;*.gci)\0*.iso;*.gcm;*.gci\0Modded disc (*.iso;*.gcm)\0*.iso;*.gcm\0"
                       L"Memory card file (*.gci)\0*.gci\0All files\0*.*\0";
  dialog.lpstrFile = file; dialog.nMaxFile = (DWORD)(sizeof file / sizeof file[0]);
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&dialog)) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
  std::string out(n > 0 ? (size_t)(n - 1) : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, file, -1, out.data(), n, nullptr, nullptr);
  return out;
}

}  // namespace source_port::mods
