// Immutable data-file overlay for the native game's DVD boundary.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace source_port {
class ModOverlay {
 public:
  static constexpr uint32_t base = 0xA0000000u;
  struct File {
    std::string path;
    uint32_t start = 0;
    uint32_t length = 0;
    std::vector<uint8_t> bytes;
    std::filesystem::path iso;
    uint64_t iso_offset = 0;
    std::string profile;
  };
  struct DiscFile { uint32_t offset = 0, length = 0; };
  using DiscLookup = std::function<bool(const std::string&, DiscFile*)>;
  using DiscRead = std::function<bool(uint32_t, void*, uint32_t)>;
  // root is the disc filesystem root, not its parent. Failure leaves no files
  // published. Limits apply before reading data into memory.
  bool load(const std::filesystem::path& root, std::string& error);
  // Add independent presets. Identical files may be shared; differing replacements
  // conflict instead of silently changing the result according to load order.
  bool add_directory(const std::filesystem::path& root, const std::string& profile, std::string& error);
  // A patched ISO is compared with the retail disc. Only changed files occupy the
  // virtual overlay address range, and their contents are read from the ISO on demand.
  bool add_iso(const std::filesystem::path& iso, const std::string& profile,
               const DiscLookup& lookup, const DiscRead& base_read, std::string& error);
  enum class Read { NotOverridden, Success, Failed };
  Read read(uint32_t offset, void* destination, uint32_t size) const;
  const std::vector<File>& files() const { return files_; }

  // Layer order: a later pack's differing copy of a file replaces the earlier one, and the pair is
  // recorded in conflicts() for the player to see. Off (the default), a differing copy is an error.
  void set_layered(bool layered) { layered_ = layered; }
  struct Conflict { std::string path, earlier, later; };
  const std::vector<Conflict>& conflicts() const { return conflicts_; }

  // What an imported ISO changes beyond its files. The retail disc's file list and main.dol extent
  // come from set_base_disc(); without them the report stays empty.
  struct BaseDisc { std::vector<std::string> files; uint32_t dol_offset = 0, dol_size = 0; };
  void set_base_disc(BaseDisc base, DiscRead base_read) { base_ = std::move(base); base_read_ = std::move(base_read); }
  struct IsoReport {
    std::string profile;
    uint32_t changed_files = 0;
    std::vector<std::string> deleted;                        // retail files the ISO does not have
    std::vector<std::pair<uint32_t, uint32_t>> dol_runs;     // (offset, length) of changed main.dol bytes
    uint32_t dol_size = 0, base_dol_size = 0;
  };
  const std::vector<IsoReport>& iso_reports() const { return reports_; }
 private:
  bool add_files(std::vector<File>&& files, std::string& error);
  static bool same_contents(const File& a, const File& b);
  std::vector<File> files_;
  bool layered_ = false;
  std::vector<Conflict> conflicts_;
  BaseDisc base_;
  DiscRead base_read_;
  std::vector<IsoReport> reports_;
};
// An Akaneia disc is refused while its fighters and stages are not native. A game library built with
// them (development builds only) says so, and the host then lets the disc through for that library.
void allow_native_akaneia(bool allow);
} // namespace source_port
