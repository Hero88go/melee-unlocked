// SPDX-License-Identifier: GPL-2.0-or-later
#include "source_mod_overlay.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>

namespace source_port {
namespace {
namespace fs = std::filesystem;
uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
bool read_at(std::ifstream& in, uint64_t offset, void* dst, size_t size) {
  if (offset > uint64_t(std::numeric_limits<std::streamoff>::max()) ||
      size > size_t(std::numeric_limits<std::streamsize>::max())) return false;
  in.clear(); in.seekg(std::streamoff(offset));
  return in.good() && bool(in.read(static_cast<char*>(dst), std::streamsize(size)));
}
bool read_file(const ModOverlay::File& file, uint64_t offset, void* dst, size_t size) {
  if (offset > file.length || size > file.length - offset) return false;
  if (file.iso.empty()) {
    if (file.bytes.size() != file.length) return false;
    std::memcpy(dst, file.bytes.data() + offset, size);
    return true;
  }
  std::ifstream in(file.iso, std::ios::binary);
  return in && read_at(in, file.iso_offset + offset, dst, size);
}
bool valid_name(const std::string& s) {
  if (s.empty() || s == "." || s == "..") return false;
  for (unsigned char c : s) if (c < 0x20 || c == '/' || c == '\\' || c == ':') return false;
  return true;
}
bool g_native_akaneia = false;   // allow_native_akaneia()
bool akaneia_disabled(const std::set<std::string>& paths, std::string& error) {
  if (g_native_akaneia) return false;
  if (!paths.count("/mxdt.dat") || !paths.count("/plsn.dat") || !paths.count("/plts.dat")) return false;
  error = "Akaneia is disabled until its native fighters and stages are complete. Use Training Mode CE or 20XX TE.";
  return true;
}
} // namespace

bool ModOverlay::load(const fs::path& root, std::string& error) {
  files_.clear();
  return add_directory(root, root.u8string(), error);
}

bool ModOverlay::add_directory(const fs::path& root, const std::string& profile, std::string& error) {
  error.clear();
  try {
    if (!fs::is_directory(root)) { error = "mod directory does not exist: " + root.u8string(); return false; }
    std::vector<std::pair<std::string, fs::path>> paths;
    std::set<std::string> names;
    uint64_t total = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      const auto status = entry.symlink_status();
      if (fs::is_symlink(status)) { error = "mod pack contains a symbolic link"; return false; }
      if (!fs::is_regular_file(status)) continue;
      const auto relpath = entry.path().lexically_relative(root);
      for (const auto& part : relpath)
        if (!valid_name(part.u8string())) { error = "invalid disc path in mod pack"; return false; }
      std::string rel = relpath.generic_u8string();
      std::transform(rel.begin(), rel.end(), rel.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      if (!names.insert(rel).second) { error = "duplicate disc path: " + rel; return false; }
      const auto size = entry.file_size();
      if (!size || size > 64u * 1024u * 1024u) { error = "mod file must be 1 byte to 64 MiB: " + rel; return false; }
      total += size;
      if (total > 512u * 1024u * 1024u || paths.size() >= 4096) {
        error = "mod pack exceeds 512 MiB or 4096 files"; return false;
      }
      paths.emplace_back("/" + rel, entry.path());
    }
    std::sort(paths.begin(), paths.end());
    std::set<std::string> disc_paths;
    for (const auto& path : paths) disc_paths.insert(path.first);
    if (akaneia_disabled(disc_paths, error)) return false;
    std::vector<File> loaded;
    for (const auto& path : paths) {
      const auto size = fs::file_size(path.second);
      if (!size || size > 64u * 1024u * 1024u) { error = "mod pack changed during import"; return false; }
      std::ifstream in(path.second, std::ios::binary);
      File file;
      file.path = path.first; file.length = uint32_t(size); file.profile = profile;
      file.bytes.resize(size);
      if (!in.read(reinterpret_cast<char*>(file.bytes.data()), std::streamsize(size)) ||
          in.peek() != std::char_traits<char>::eof()) {
        error = "cannot read complete mod file: " + path.first; return false;
      }
      loaded.push_back(std::move(file));
    }
    return add_files(std::move(loaded), error);
  } catch (const fs::filesystem_error& e) { error = e.what(); return false; }
}

bool ModOverlay::add_iso(const fs::path& iso, const std::string& profile,
                         const DiscLookup& lookup, const DiscRead& base_read, std::string& error) {
  error.clear();
  try {
    const uint64_t image_size = fs::file_size(iso);
    if (image_size < 0x440 || image_size > 16ull * 1024 * 1024 * 1024) {
      error = "invalid mod ISO size"; return false;
    }
    std::ifstream in(iso, std::ios::binary);
    std::array<uint8_t, 0x440> header{};
    if (!in || !read_at(in, 0, header.data(), header.size())) { error = "cannot read mod ISO header"; return false; }
    static constexpr uint8_t melee_102_id[8] = {'G', 'A', 'L', 'E', '0', '1', 0, 2};
    // Training Mode CE's disc: the same 1.02 game under its own id.
    static constexpr uint8_t tmce_id[8] = {'G', 'T', 'M', 'E', '0', '1', 0, 2};
    if (std::memcmp(header.data(), melee_102_id, sizeof(melee_102_id)) &&
        std::memcmp(header.data(), tmce_id, sizeof(tmce_id))) {
      error = "mod ISO must be a patched NTSC 1.02 Melee disc (GALE01 revision 2)";
      return false;
    }
    const uint64_t fst_offset = be32(header.data() + 0x424), fst_size = be32(header.data() + 0x428);
    if (fst_size < 12 || fst_size > 2u * 1024u * 1024u || fst_offset > image_size ||
        fst_size > image_size - fst_offset) { error = "invalid mod ISO filesystem table"; return false; }
    std::vector<uint8_t> fst(size_t(fst_size), uint8_t{0});
    if (!read_at(in, fst_offset, fst.data(), fst.size())) { error = "cannot read mod ISO filesystem table"; return false; }
    const uint32_t count = be32(fst.data() + 8);
    if (count < 1 || count > 4096 || uint64_t(count) * 12 > fst_size || fst[0] == 0) {
      error = "invalid mod ISO filesystem entries"; return false;
    }
    const size_t names_start = size_t(count) * 12;
    std::vector<std::pair<uint32_t, std::string>> dirs{{count, ""}};
    std::set<std::string> names;
    std::vector<File> changed;
    std::array<uint8_t, 65536> mod_chunk{}, base_chunk{};
    for (uint32_t i = 1; i < count; ++i) {
      while (dirs.size() > 1 && i >= dirs.back().first) dirs.pop_back();
      const uint8_t* entry = fst.data() + size_t(i) * 12;
      const uint32_t name_offset = be32(entry) & 0xFFFFFFu;
      if (name_offset >= fst_size - names_start) { error = "invalid mod ISO filename offset"; return false; }
      const char* start = reinterpret_cast<const char*>(fst.data() + names_start + name_offset);
      const char* end = static_cast<const char*>(std::memchr(start, 0, size_t(fst_size) - names_start - name_offset));
      if (!end) { error = "unterminated mod ISO filename"; return false; }
      std::string name(start, end);
      if (!valid_name(name)) { error = "invalid mod ISO filename"; return false; }
      std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      std::string path = dirs.back().second + "/" + name;
      if (!names.insert(path).second) { error = "duplicate mod ISO path: " + path; return false; }
      if (entry[0]) {
        const uint32_t next = be32(entry + 8);
        if (next <= i || next > dirs.back().first) { error = "invalid mod ISO directory range"; return false; }
        dirs.push_back({next, path});
        continue;
      }
      const uint64_t offset = be32(entry + 4), length = be32(entry + 8);
      if (offset > image_size || length > image_size - offset) {
        error = "invalid mod ISO file: " + path; return false;
      }
      DiscFile original{};
      bool identical = false;
      if (lookup(path, &original) && original.length == length) {
        identical = true;
        for (uint64_t pos = 0; pos < length; pos += mod_chunk.size()) {
          const uint32_t n = uint32_t(std::min<uint64_t>(mod_chunk.size(), length - pos));
          if (!read_at(in, offset + pos, mod_chunk.data(), n) ||
              !base_read(original.offset + uint32_t(pos), base_chunk.data(), n)) {
            error = "cannot compare mod ISO file: " + path; return false;
          }
          if (std::memcmp(mod_chunk.data(), base_chunk.data(), n)) { identical = false; break; }
        }
      }
      if (identical) continue;
      // Retail movies can exceed the replacement-file limit. They are safe to
      // leave on the original disc; apply this limit only to changed content.
      if (length > 256u * 1024u * 1024u) {
        error = "mod ISO replacement file is too large: " + path; return false;
      }
      File file;
      file.path = std::move(path); file.length = uint32_t(length);
      file.iso = iso; file.iso_offset = offset; file.profile = profile;
      changed.push_back(std::move(file));
    }
    IsoReport report;
    report.profile = profile;
    report.changed_files = uint32_t(changed.size());
    for (const auto& base_file : base_.files)
      if (!names.count(base_file)) report.deleted.push_back(base_file);
    // main.dol: its extent is the furthest text or data section end in its header.
    const uint64_t dol_offset = be32(header.data() + 0x420);
    uint8_t dol_header[0x100]{};
    if (base_.dol_size && base_read_ && dol_offset + sizeof dol_header <= image_size &&
        read_at(in, dol_offset, dol_header, sizeof dol_header)) {
      uint64_t extent = sizeof dol_header;
      for (int i = 0; i < 18; ++i)
        extent = std::max<uint64_t>(extent, uint64_t(be32(dol_header + i * 4)) + be32(dol_header + 0x90 + i * 4));
      if (dol_offset + extent <= image_size && extent <= 64u * 1024u * 1024u) {
        report.dol_size = uint32_t(extent);
        report.base_dol_size = base_.dol_size;
        const uint64_t common = std::min<uint64_t>(extent, base_.dol_size);
        int64_t run_start = -1;
        uint64_t last_diff = 0;
        for (uint64_t pos = 0; pos < common; pos += mod_chunk.size()) {
          const uint32_t n = uint32_t(std::min<uint64_t>(mod_chunk.size(), common - pos));
          if (!read_at(in, dol_offset + pos, mod_chunk.data(), n) ||
              !base_read_(base_.dol_offset + uint32_t(pos), base_chunk.data(), n)) break;
          for (uint32_t k = 0; k < n; ++k) {
            if (mod_chunk[k] == base_chunk[k]) continue;
            const uint64_t at = pos + k;
            if (run_start >= 0 && at - last_diff > 32) {   // runs closer than 32 bytes merge
              report.dol_runs.push_back({uint32_t(run_start), uint32_t(last_diff + 1 - run_start)});
              run_start = -1;
            }
            if (run_start < 0) run_start = int64_t(at);
            last_diff = at;
          }
        }
        if (run_start >= 0) report.dol_runs.push_back({uint32_t(run_start), uint32_t(last_diff + 1 - run_start)});
      }
    }
    if (akaneia_disabled(names, error)) return false;
    if (!add_files(std::move(changed), error)) return false;
    reports_.push_back(std::move(report));
    return true;
  } catch (const fs::filesystem_error& e) { error = e.what(); return false; }
}

bool ModOverlay::same_contents(const File& a, const File& b) {
  if (a.length != b.length) return false;
  std::array<uint8_t, 65536> x{}, y{};
  for (uint64_t pos = 0; pos < a.length; pos += x.size()) {
    const size_t n = size_t(std::min<uint64_t>(x.size(), a.length - pos));
    if (!read_file(a, pos, x.data(), n) || !read_file(b, pos, y.data(), n) ||
        std::memcmp(x.data(), y.data(), n)) return false;
  }
  return true;
}

bool ModOverlay::add_files(std::vector<File>&& files, std::string& error) {
  std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.path < b.path; });
  std::vector<File> accepted;
  std::vector<Conflict> pending_conflicts;
  std::vector<std::string> superseded;
  uint64_t next = files_.empty() ? base :
      uint64_t(files_.back().start) +
      std::max<uint64_t>(0x8000u, (uint64_t(files_.back().length) + 0x7FFFu) & ~uint64_t(0x7FFFu));
  for (auto& file : files) {
    if (!accepted.empty() && accepted.back().path == file.path) { error = "duplicate mod path: " + file.path; return false; }
    auto previous = std::find_if(files_.begin(), files_.end(), [&](const File& item) { return item.path == file.path; });
    if (previous != files_.end()) {
      if (same_contents(*previous, file)) continue;
      if (!layered_) {
        error = "conflicting mod file " + file.path + " in " + previous->profile + " and " + file.profile;
        return false;
      }
      pending_conflicts.push_back({file.path, previous->profile, file.profile});
      superseded.push_back(file.path);
    }
    const uint64_t span = std::max<uint64_t>(0x8000u, (uint64_t(file.length) + 0x7FFFu) & ~uint64_t(0x7FFFu));
    if (next + span > 0xF0000000ull || files_.size() + accepted.size() >= 4096) {
      error = "enabled mod packs exceed the virtual disc range or 4096 files"; return false;
    }
    file.start = uint32_t(next); next += span;
    accepted.push_back(std::move(file));
  }
  // Only now, with the whole pack accepted, do its replacements take effect: a failed import leaves
  // the live table as it was.
  files_.erase(std::remove_if(files_.begin(), files_.end(), [&](const File& item) {
    return std::find(superseded.begin(), superseded.end(), item.path) != superseded.end();
  }), files_.end());
  files_.insert(files_.end(), std::make_move_iterator(accepted.begin()), std::make_move_iterator(accepted.end()));
  conflicts_.insert(conflicts_.end(), pending_conflicts.begin(), pending_conflicts.end());
  return true;
}

void allow_native_akaneia(bool allow) { g_native_akaneia = allow; }

ModOverlay::Read ModOverlay::read(uint32_t offset, void* dst, uint32_t size) const {
  if (offset < base) return Read::NotOverridden;
  if (files_.empty()) return Read::Failed;
  const auto upper = std::upper_bound(files_.begin(), files_.end(), offset,
      [](uint32_t address, const File& file) { return address < file.start; });
  if (upper == files_.begin()) return Read::Failed;
  const File& file = *std::prev(upper);
  const uint64_t local = uint64_t(offset) - file.start;
  const uint64_t aligned = (uint64_t(file.length) + 31u) & ~uint64_t(31u);
  if ((size && !dst) || local > aligned || size > aligned - local) return Read::Failed;
  if (!size) return Read::Success;
  std::memset(dst, 0, size);
  if (local < file.length) {
    const size_t copy = size_t(std::min<uint64_t>(size, file.length - local));
    if (!read_file(file, local, dst, copy)) return Read::Failed;
  }
  return Read::Success;
}
} // namespace source_port
