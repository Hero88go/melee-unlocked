// Save backups: once per start and folder, each save file in the card folder is copied to its
// Backups folder when it differs from the newest copy kept there (kept: the ten newest per save
// and the first of each of the last thirty days). Never stops the game: a copy that fails is logged. Both engines call it when the card is
// first mounted. Header only, so the memory card unit test links without the rest of the host.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "host.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace host {
inline void backup_card_folder(const std::filesystem::path& folder) {
  // Once per folder and start (a profile's own card folder is another folder).
  static std::vector<std::wstring> done;
  const std::wstring key = folder.wstring();
  if (std::find(done.begin(), done.end(), key) != done.end()) return;
  done.push_back(key);
  namespace fs = std::filesystem;
  try {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return;
    const fs::path backups = folder / L"Backups";
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t stamp[32];
    std::wcsftime(stamp, 32, L"%Y%m%d-%H%M%S", &local);
    const auto read_all = [](const fs::path& p, std::vector<uint8_t>* out) {
      std::ifstream in(p, std::ios::binary);
      if (!in) return false;
      out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      return true;
    };
    unsigned copied = 0;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
      if (!entry.is_regular_file(ec) || entry.path().extension() != L".gci") continue;
      const std::wstring stem = entry.path().stem().wstring();
      // This save's copies, oldest first (the stamp sorts as text).
      std::vector<fs::path> kept;
      if (fs::is_directory(backups, ec))
        for (const auto& old : fs::directory_iterator(backups, ec)) {
          const std::wstring name = old.path().filename().wstring();
          if (name.size() > stem.size() + 1 && name.compare(0, stem.size() + 1, stem + L".") == 0 &&
              old.path().extension() == L".gci") kept.push_back(old.path());
        }
      std::sort(kept.begin(), kept.end());
      std::vector<uint8_t> current, newest;
      if (!read_all(entry.path(), &current) || current.empty()) continue;
      if (!kept.empty() && read_all(kept.back(), &newest) && newest == current) continue;
      fs::create_directories(backups, ec);
      const fs::path target = backups / (stem + L"." + stamp + L".gci");
      fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec);
      if (ec) { host::log("card: the save could not be backed up (%s)", ec.message().c_str()); continue; }
      ++copied;
      kept.push_back(target);
      // The game writes its save at every start, so every start leaves a copy. Kept: the ten
      // newest, and the first copy of each of the thirty most recent days that have one.
      std::vector<fs::path> keep(kept.end() - (std::ptrdiff_t)std::min<size_t>(10, kept.size()), kept.end());
      std::vector<std::wstring> days;
      for (auto it = kept.rbegin(); it != kept.rend(); ++it) {
        const std::wstring name = it->filename().wstring();
        const std::wstring day = name.size() >= stem.size() + 9 ? name.substr(stem.size() + 1, 8) : std::wstring();
        if (day.empty()) continue;
        if (days.empty() || days.back() != day) { if (days.size() == 30) break; days.push_back(day); }
      }
      for (const std::wstring& day : days)
        for (const fs::path& item : kept) {   // oldest first: the day's first copy
          const std::wstring name = item.filename().wstring();
          if (name.size() >= stem.size() + 9 && name.compare(stem.size() + 1, 8, day) == 0) { keep.push_back(item); break; }
        }
      for (const fs::path& item : kept)
        if (std::find(keep.begin(), keep.end(), item) == keep.end()) fs::remove(item, ec);
    }
    if (copied) host::log("card: %u save file%s backed up to the Backups folder beside the saves", copied, copied == 1 ? "" : "s");
  } catch (const std::exception& error) {
    host::log("card: save backup skipped (%s)", error.what());
  }
}
}  // namespace host
