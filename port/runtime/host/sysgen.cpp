// SPDX-License-Identifier: GPL-2.0-or-later
#include "sysgen.h"
#include "sysgen_paths.h"
#include "vcdiff.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace host::sysgen {
namespace {
constexpr size_t max_asset = 256u * 1024 * 1024;
const std::array<std::string, 2> required = {"GameSetup_gui.dat", "slpCSS.dat"};
bool fail(std::string& error, const std::string& message) { error = message; return false; }
bool component(const std::string& s) {
  return !s.empty() && s != "." && s != ".." &&
      std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
      });
}
std::string hash_field(const nlohmann::json& j, const char* field) {
  std::string s = j.at(field).get<std::string>();
  if (s.size() != 40) throw std::runtime_error("SHA-1 must have 40 hex digits");
  for (char& c : s) {
    if (c >= 'A' && c <= 'F') c += 'a' - 'A';
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      throw std::runtime_error("SHA-1 contains non-hex digits");
  }
  return s;
}
size_t size_field(const nlohmann::json& j, const char* field) {
  const auto& n = j.at(field);
  if (!n.is_number_unsigned() || n.get<uint64_t>() > max_asset)
    throw std::runtime_error("asset size is invalid or exceeds 256 MiB");
  return n.get<size_t>();
}
std::filesystem::path normalized(std::filesystem::path path) {
  path = path.lexically_normal();
  if (path.filename().empty()) path = path.parent_path();
  return path;
}
std::string sys_name(const std::filesystem::path& path) {
  const auto name = normalized(path).filename().u8string();
  if (name == "slippi_sys_playback" || name == "SysPlayback") return "SysPlayback";
  if (name == "slippi_sys_general") return name;
  return "Sys";  // --sys-dir may give a custom name to the primary Sys directory.
}
std::string key(const std::filesystem::path& sys, const std::string& target) {
  return normalized(std::filesystem::absolute(sys)).generic_u8string() + "/" + target;
}
bool read(const std::filesystem::path& path, size_t cap, std::vector<uint8_t>& bytes) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return false;
  const auto n = f.tellg();
  if (n < 0 || uint64_t(n) > cap) return false;
  bytes.resize(size_t(n));
  f.seekg(0);
  return bytes.empty() || bool(f.read(reinterpret_cast<char*>(bytes.data()), bytes.size()));
}
bool matches(const std::vector<uint8_t>& bytes, size_t size, const std::string& hash) {
  return bytes.size() == size && sha1(bytes) == hash;
}
std::filesystem::path shipped(const std::filesystem::path& sys, const std::string& target) {
  // Current builds store these under GameFiles; also accept the manifest's flat Sys layout.
  for (const auto& path : {sys / "GameFiles" / "GALE01" / target, sys / target})
    if (std::filesystem::exists(path)) return path;
  return {};
}
bool atomic_write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) return false;
  static std::atomic<uint64_t> sequence{0};
#ifdef _WIN32
  const auto pid = GetCurrentProcessId();
#else
  const auto pid = getpid();
#endif
  auto temp = path;
  temp += ".tmp-" + std::to_string(pid) + "-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
      std::to_string(sequence++);
  bool ok = false;
  {
    std::ofstream f(temp, std::ios::binary | std::ios::trunc);
    if (f) {
      f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      f.close();
      ok = bool(f);
    }
  }
  if (ok) {
#ifdef _WIN32
    ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temp, path, ec);
    ok = !ec;
#endif
  }
  if (!ok) std::filesystem::remove(temp, ec);
  return ok;
}
}  // namespace

bool parse_manifest(const std::string& text, std::vector<File>& files, std::string& error) {
  files.clear(); error.clear();
  try {
    const auto root = nlohmann::json::parse(text);
    const auto& entries = root.at("files");
    if (!entries.is_array() || entries.empty()) throw std::runtime_error("files must be a nonempty array");
    std::vector<File> parsed;
    std::set<std::string> targets;
    for (const auto& j : entries) {
      File f;
      f.target = j.at("target").get<std::string>();
      if (std::find(required.begin(), required.end(), f.target) == required.end() || !targets.insert(f.target).second)
        throw std::runtime_error("unknown or duplicate target");
      f.sha1 = hash_field(j, "target_sha1"); f.size = size_field(j, "target_size");
      f.delta = j.at("delta").get<std::string>();
      if (!component(f.delta)) throw std::runtime_error("invalid delta filename");
      f.sys = j.at("sys").get<std::vector<std::string>>();
      std::set<std::string> dirs;
      for (const auto& sys : f.sys)
        if ((sys != "Sys" && sys != "SysPlayback" && sys != "slippi_sys_general") || !dirs.insert(sys).second)
          throw std::runtime_error("unknown or duplicate Sys directory");
      const auto& sources = j.at("sources");
      if (f.sys.empty() || !sources.is_array() || sources.empty()) throw std::runtime_error("empty sys or sources");
      size_t total = 0;
      for (const auto& item : sources) {
        Source s;
        s.disc = item.at("disc").get<std::string>(); s.sha1 = hash_field(item, "sha1"); s.size = size_field(item, "size");
        if (!component(s.disc) || s.size > max_asset - total) throw std::runtime_error("invalid source filename or total size");
        total += s.size; f.sources.push_back(std::move(s));
      }
      parsed.push_back(std::move(f));
    }
    files = std::move(parsed);
    return true;
  } catch (const std::exception& e) { return fail(error, std::string("invalid manifest: ") + e.what()); }
}

// Portable SHA-1, the same retail-asset identity algorithm as the Linux bootstrap host.
std::string sha1(const std::vector<uint8_t>& input) {
  const uint64_t bits = uint64_t(input.size()) * 8;
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  const auto rol = [](uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); };
  const size_t blocks = input.size() / 64 + (input.size() % 64 < 56 ? 1 : 2);
  for (size_t block = 0; block < blocks; ++block) {
    uint32_t w[80]{};
    for (size_t i = 0; i < 64; ++i) {
      const size_t at = block * 64 + i;
      uint8_t value = 0;
      if (at < input.size()) value = input[at];
      else if (at == input.size()) value = 128;
      else if (at >= blocks * 64 - 8) value = uint8_t(bits >> ((blocks * 64 - 1 - at) * 8));
      w[i / 4] |= uint32_t(value) << (24 - (i % 4) * 8);
    }
    for (unsigned i = 16; i < 80; ++i) w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
    for (unsigned i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      const uint32_t t = rol(a, 5) + f + e + k + w[i];
      e=d; d=c; c=rol(b, 30); b=a; a=t;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
  }
  const char* digits = "0123456789abcdef";
  std::string out;
  for (unsigned i = 0; i < 20; ++i) {
    const uint8_t byte = uint8_t(h[i/4] >> (24 - (i%4)*8));
    out += digits[byte >> 4]; out += digits[byte & 15];
  }
  return out;
}

bool Assets::prepare(const Config& config, const Disc& disc, std::string& error) {
  const auto started = std::chrono::steady_clock::now();
  redirects_.clear(); error.clear(); status_message_.clear();
  try {
    std::vector<uint8_t> manifest;
    std::vector<File> files;
    const auto manifest_path = config.build_dir / "deltas" / "manifest.json";
    const bool has_manifest = std::filesystem::exists(manifest_path);
    if (has_manifest) {
      if (!read(manifest_path, 1024 * 1024, manifest)) return fail(error, "cannot read deltas/manifest.json");
      if (!parse_manifest(std::string(manifest.begin(), manifest.end()), files, error)) return false;
    }
    const auto active = sys_name(config.sys_dir);
    // Missing metadata may never silently restore the old optional-menu fallback.
    for (const auto& name : required) {
      if (!shipped(config.sys_dir, name).empty()) continue;
      if (std::none_of(files.begin(), files.end(), [&](const File& f) {
            return f.target == name && std::find(f.sys.begin(), f.sys.end(), active) != f.sys.end();
          })) return fail(error, name + ": missing delta manifest entry for active Sys directory");
    }
    std::map<std::string, std::filesystem::path> pending;
    bool verified = false;
    std::vector<std::string> built_targets;
    std::set<std::filesystem::path> built_folders;
    size_t verified_files = 0;
    for (const auto& file : files) {
      std::vector<std::pair<std::filesystem::path, std::filesystem::path>> needed;
      for (const auto& sys : file.sys) {
        auto folder = sys == active ? config.sys_dir : config.build_dir / sys;
        // Source-tree staging uses these aliases for Sys and SysPlayback.
        if (!std::filesystem::exists(folder) && sys == "Sys" &&
            std::filesystem::exists(config.build_dir / "slippi_sys")) folder = config.build_dir / "slippi_sys";
        if (!std::filesystem::exists(folder) && sys == "SysPlayback" &&
            std::filesystem::exists(config.build_dir / "slippi_sys_playback")) folder = config.build_dir / "slippi_sys_playback";
        if (!shipped(folder, file.target).empty()) continue;
        if (config.data_dir.empty()) return fail(error, file.target + ": generation needs --user-dir or --sysgen-data-dir");
        const auto generated = config.data_dir / "SysGenerated" / sys / file.target;
        std::error_code path_error;
        if (!outside_build(config.build_dir, generated, path_error))
          return fail(error, file.target + ": generated data must be outside the build directory" +
                      (path_error ? ": " + path_error.message() : ""));
        needed.push_back({folder, generated});
      }
      if (needed.empty()) continue;
      std::vector<uint8_t> output;
      bool rebuild = false;
      for (const auto& item : needed) {
        if (std::filesystem::exists(item.second)) {
          if (!read(item.second, file.size, output) || !matches(output, file.size, file.sha1))
            return fail(error, file.target + ": generated file size/SHA-1 mismatch");
        } else rebuild = true;
      }
      if (rebuild) {
        if (!verified) {
          if (!disc.verify_retail || !disc.verify_retail()) return fail(error, file.target + ": disc is not verified retail Melee NTSC 1.02");
          verified = true;
        }
        std::vector<uint8_t> source, chunk, delta;
        for (const auto& s : file.sources) {
          if (!disc.read || !disc.read(s.disc, s.size, chunk)) return fail(error, file.target + ": missing, unreadable or wrong-sized disc source " + s.disc);
          if (!matches(chunk, s.size, s.sha1)) return fail(error, file.target + ": disc source size/SHA-1 mismatch: " + s.disc);
          source.insert(source.end(), chunk.begin(), chunk.end());
        }
        if (!read(config.build_dir / "deltas" / file.delta, max_asset, delta)) return fail(error, file.target + ": cannot read delta");
        std::string why;
        if (!vcdiff_decode(source.data(), source.size(), delta.data(), delta.size(), output, &why))
          return fail(error, file.target + ": invalid delta: " + why);
        if (output.size() != file.size) return fail(error, file.target + ": rebuilt target size/SHA-1 mismatch");
        if (sha1(output) != file.sha1) return fail(error, file.target + ": rebuilt target size/SHA-1 mismatch");
        bool built = false;
        for (const auto& item : needed) {
          if (std::filesystem::exists(item.second)) continue;
          if (!atomic_write(item.second, output))
            return fail(error, file.target + ": cannot atomically write generated file");
          built = true;
          built_folders.insert(item.second.parent_path());
        }
        if (built) built_targets.push_back(file.target);
      }
      for (const auto& item : needed) {
        // Recheck final on-disk bytes, including a file another boot may have just published.
        if (!read(item.second, file.size, output) || !matches(output, file.size, file.sha1))
          return fail(error, file.target + ": generated file size/SHA-1 mismatch");
        ++verified_files;
        pending[key(item.first, file.target)] = item.second;
      }
    }
    if (has_manifest) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count();
      if (!built_targets.empty()) {
        status_message_ = "sysgen: built ";
        for (const auto& target : built_targets) {
          if (target != built_targets.front()) status_message_ += ", ";
          status_message_ += target;
        }
        status_message_ += " into " + (config.data_dir / "SysGenerated").u8string() +
            " in " + std::to_string(elapsed) + " ms (" +
            std::to_string(built_folders.size()) + " Sys folders)";
      } else if (verified_files) {
        status_message_ = "sysgen: already built (" + std::to_string(verified_files) +
            " files verified in " + std::to_string(elapsed) + " ms)";
      } else {
        status_message_ = "sysgen: shipped files present; nothing to build";
      }
    }
    redirects_ = std::move(pending);  // Publish only after every required asset passed.
    return true;
  } catch (const std::exception&) { return fail(error, "cannot prepare generated Sys assets (I/O or allocation failure)"); }
}

std::filesystem::path Assets::lookup(const std::filesystem::path& sys, const std::string& target) const {
  const auto found = redirects_.find(key(sys, target));
  if (found != redirects_.end()) return found->second;
  const auto existing = shipped(sys, target);
  return existing.empty() ? sys / "GameFiles" / "GALE01" / target : existing;
}
std::vector<std::string> Assets::generated_names(const std::filesystem::path& sys) const {
  std::vector<std::string> names;
  for (const auto& name : required) if (redirects_.count(key(sys, name))) names.push_back(name);
  return names;
}
}  // namespace host::sysgen
