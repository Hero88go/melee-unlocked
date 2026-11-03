// Physical path checks without Windows DOS volume-name lookup.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace host::sysgen {
namespace path_detail {
inline std::filesystem::path resolve_existing(const std::filesystem::path& path, std::error_code& ec) {
#ifndef _WIN32
  return std::filesystem::canonical(path, ec);
#else
  // MSVC canonical() requests VOLUME_NAME_DOS, denied by the AppContainer's
  // mount-manager policy. NONE needs only this accessible ancestor's handle.
  const auto drive = path.root_name().native();
  if (drive.size() != 2 || drive[1] != L':') {
    ec = std::make_error_code(std::errc::invalid_argument); return {};
  }
  HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) { ec = {int(GetLastError()), std::system_category()}; return {}; }
  std::vector<wchar_t> name(32768);
  const DWORD size = GetFinalPathNameByHandleW(handle, name.data(), DWORD(name.size()),
                                             FILE_NAME_NORMALIZED | VOLUME_NAME_NONE);
  const DWORD error = size ? ERROR_SUCCESS : GetLastError();
  CloseHandle(handle);
  if (!size || size >= name.size()) {
    ec = {int(size ? ERROR_BUFFER_OVERFLOW : error), std::system_category()}; return {};
  }
  if (name[0] != L'\\' || (size > 1 && name[1] == L'\\')) {
    ec = std::make_error_code(std::errc::invalid_argument); return {};
  }
  const std::filesystem::path resolved(drive + std::wstring(name.data(), size));
  // Prove the reconstructed drive + NONE name names the same object. This also
  // refuses cross-volume aliases that cannot safely be reconstructed this way.
  if (!std::filesystem::equivalent(path, resolved, ec)) {
    if (!ec) ec = std::make_error_code(std::errc::invalid_argument);
    return {};
  }
  return resolved;
#endif
}
inline bool same_component(const std::filesystem::path& a, const std::filesystem::path& b) {
#ifdef _WIN32
  return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
  return a == b;
#endif
}
inline bool missing(const std::error_code& ec) {
  return ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory;
}
} // namespace path_detail

// Resolve the deepest accessible existing ancestor, then normalize its missing
// suffix. Parents outside the game's filesystem grants need not be inspected.
inline std::filesystem::path resolve_ancestors(const std::filesystem::path& input,
                                               std::error_code& ec) {
  namespace fs = std::filesystem;
  ec.clear();
  if (input.empty()) { ec = std::make_error_code(std::errc::invalid_argument); return {}; }
  auto ancestor = fs::absolute(input, ec);
  if (ec) return {};
  std::vector<fs::path> suffix;
  for (;;) {
    const auto status = fs::status(ancestor, ec);
    if (ec && !path_detail::missing(ec)) return {};
    if (!ec && fs::exists(status)) break;
    ec.clear();
    const auto link = fs::symlink_status(ancestor, ec);
    if (ec && !path_detail::missing(ec)) return {};
    if (fs::is_symlink(link)) {
      ec = std::make_error_code(std::errc::no_such_file_or_directory); return {};
    }
    ec.clear();
    const auto parent = ancestor.parent_path();
    if (parent == ancestor || parent.empty()) {
      ec = std::make_error_code(std::errc::no_such_file_or_directory); return {};
    }
    suffix.push_back(ancestor.filename()); ancestor = parent;
  }
  auto resolved = path_detail::resolve_existing(ancestor, ec);
  if (ec) return {};
  for (auto i = suffix.rbegin(); i != suffix.rend(); ++i) resolved /= *i;
  return resolved.lexically_normal();
}

// Physical containment: compare resolved components, then check existing prefix
// identity (including drive/case aliases). Lookup failures never grant a write.
inline bool outside_build(const std::filesystem::path& build,
                          const std::filesystem::path& destination, std::error_code& ec) {
  namespace fs = std::filesystem;
  const auto root = resolve_ancestors(build, ec);
  if (ec) return false;
  if (!fs::is_directory(root, ec)) {
    if (!ec) ec = std::make_error_code(std::errc::not_a_directory);
    return false;
  }
  const auto path = resolve_ancestors(destination, ec);
  if (ec) return false;
  auto prefix = path.root_path();
  const auto relative = path.relative_path();
  auto part = relative.begin();
  for (const auto& component : root.relative_path()) {
    if (part == relative.end() || !path_detail::same_component(component, *part)) return true;
    prefix /= *part++;
  }
  const auto status = fs::status(prefix, ec);
  if (path_detail::missing(ec)) ec.clear();
  if (ec) return false;
  if (!fs::exists(status)) return true;
  const bool same = fs::equivalent(root, prefix, ec);
  return !ec && !same;
}
} // namespace host::sysgen
