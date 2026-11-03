// Rebuild disc-derived Slippi assets in writable user data, before either engine boots.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace host::sysgen {
struct Source { std::string disc, sha1; size_t size = 0; };
struct File {
  std::string target, sha1, delta;
  size_t size = 0;
  std::vector<std::string> sys;
  std::vector<Source> sources;
};
bool parse_manifest(const std::string& json, std::vector<File>& files, std::string& error);
std::string sha1(const std::vector<uint8_t>& bytes);
struct Config {
  std::filesystem::path build_dir, sys_dir, data_dir;
};
// These callbacks use the existing boot disc/FST reader. Size is checked before allocation/read.
struct Disc {
  std::function<bool()> verify_retail;
  std::function<bool(const std::string&, size_t, std::vector<uint8_t>&)> read;
};
class Assets {
 public:
  bool prepare(const Config& config, const Disc& disc, std::string& error);
  const std::string& status_message() const { return status_message_; }
  std::filesystem::path lookup(const std::filesystem::path& sys_dir, const std::string& target) const;
  std::vector<std::string> generated_names(const std::filesystem::path& sys_dir) const;
 private:
  std::string status_message_;
  std::map<std::string, std::filesystem::path> redirects_;
};
}  // namespace host::sysgen
