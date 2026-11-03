// Staging-only file capabilities. All policy publication happens before worker startup.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef YOUGAME_AGENT_TEST_HOOK
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#include <cstddef>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace yougame::agent_paths {
namespace fs = std::filesystem;
constexpr size_t path_limit = 4096, script_limit = 64 * 1024;
#ifdef _WIN32
constexpr size_t absolute_limit = 32767;
#else
constexpr size_t absolute_limit = 4096;
#endif
inline std::string slashes(std::string s) {
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
}
inline bool relative_valid(const std::string& input) {
  const auto s = slashes(input);
  if (s.empty() || s.size() > path_limit || s.front() == '/' || s.back() == '/' ||
      s.find(':') != s.npos || s.find("..") != s.npos) return false;
  if (std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; })) return false;
  for (const auto& p : fs::u8path(s)) {
    const auto part = p.u8string();
    if (part.empty()) return false;
#ifdef _WIN32
    // Reject Win32 device aliases before opening, including aliases with extensions.
    auto stem = part.substr(0, part.find('.'));
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) stem.pop_back();
    for (char& c : stem) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" || stem == "CONOUT$" ||
        (stem.size() == 4 && (stem.substr(0, 3) == "COM" || stem.substr(0, 3) == "LPT") && stem[3] >= '1' && stem[3] <= '9')) return false;
#endif
  }
  return true;
}
// One root capability per load/session. Only load's identity check reopens by name.
// Windows-only behavior is UNPROVEN until the native pc2 SDK/runtime review.
#ifdef _WIN32
inline void win_error(DWORD error = GetLastError()) {
  errno = (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) ? EEXIST : EACCES;
}
#ifdef YOUGAME_AGENT_ROOT_WIN_TEST
// Substitutes only the volume-relative result, never the identity checks.
inline DWORD (WINAPI* root_final_path)(HANDLE, LPWSTR, DWORD, DWORD) = GetFinalPathNameByHandleW;
#endif
inline bool regular(HANDLE h, bool write = false) {
  BY_HANDLE_FILE_INFORMATION info{};
  return GetFileType(h) == FILE_TYPE_DISK && GetFileInformationByHandle(h, &info) &&
      !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) &&
      (!write || info.nNumberOfLinks == 1);
}
// Resolve only one component relative to a retained, checked directory handle.
// Numeric flags are NT FILE_OPEN/CREATE/OPEN_IF and FILE_* options from ntifs.h.
inline HANDLE relative_handle(HANDLE parent, const fs::path& leaf, ACCESS_MASK access,
                              ULONG disposition, bool directory) {
  const auto name = leaf.native();
  if (name.empty() || name.size() > 32766 || name.find_first_of(L"/\\:") != name.npos) {
    errno = EINVAL; return INVALID_HANDLE_VALUE;
  }
  using Create = NTSTATUS (NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
      PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
  using Error = ULONG (WINAPI*)(NTSTATUS);
  const auto module = GetModuleHandleW(L"ntdll.dll");
  const auto create = reinterpret_cast<Create>(GetProcAddress(module, "NtCreateFile"));
  const auto error = reinterpret_cast<Error>(GetProcAddress(module, "RtlNtStatusToDosError"));
  if (!create || !error) { errno = EACCES; return INVALID_HANDLE_VALUE; }
  UNICODE_STRING unicode{};
  unicode.Buffer = const_cast<PWSTR>(name.data());
  unicode.Length = unicode.MaximumLength = static_cast<USHORT>(name.size() * sizeof(wchar_t));
  OBJECT_ATTRIBUTES attributes{};
  attributes.Length = sizeof(attributes); attributes.RootDirectory = parent;
  attributes.ObjectName = &unicode; attributes.Attributes = 0x40; // OBJ_CASE_INSENSITIVE
  IO_STATUS_BLOCK status{}; HANDLE h = INVALID_HANDLE_VALUE;
  const NTSTATUS result = create(&h, access | SYNCHRONIZE | FILE_READ_ATTRIBUTES, &attributes,
      &status, nullptr, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
      disposition, 0x00200000 | 0x00000020 | (directory ? 0x1 : 0x40), nullptr, 0);
  // FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT | DIRECTORY/NON_DIRECTORY_FILE
  if (result < 0) { win_error(error(result)); return INVALID_HANDLE_VALUE; }
  return h;
}
#endif
struct Root {
  fs::path path;
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
  void reset() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); handle = INVALID_HANDLE_VALUE; path.clear(); }
#else
  int fd = -1;
  void reset() { if (fd >= 0) ::close(fd); fd = -1; path.clear(); }
#endif
  ~Root() { reset(); }
  Root() = default;
  Root(const Root&) = delete;
  Root& operator=(const Root&) = delete;
  bool load(fs::path original) {
    reset();
    if (original.empty()) { errno = EACCES; return false; }
#ifdef _WIN32
    // Only a plain DOS drive supplies a trustworthy prefix for VOLUME_NAME_NONE.
    // Reject UNC, device/extended namespaces and drive-relative inputs before I/O.
    const auto& input = original.native();
    if (input.size() < 3 || !((input[0] >= L'A' && input[0] <= L'Z') ||
        (input[0] >= L'a' && input[0] <= L'z')) || input[1] != L':' || input[2] != L'\\') {
      errno = EACCES; return false;
    }
    const std::wstring drive = input.substr(0, 2);
#endif
    // A trailing slash or /. must not let the OS follow a final root symlink.
    while (original.has_relative_path() && !original.parent_path().empty() && (original.filename().empty() || original.filename() == "."))
      original = original.parent_path();
#ifdef _WIN32
    constexpr DWORD access = FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
    handle = CreateFileW(original.c_str(), access,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle == INVALID_HANDLE_VALUE || GetFileType(handle) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { reset(); errno = EACCES; return false; }
    std::vector<wchar_t> name(absolute_limit + 1);
#ifdef YOUGAME_AGENT_ROOT_WIN_TEST
    const DWORD length = root_final_path(handle, name.data(), static_cast<DWORD>(name.size()),
                                         FILE_NAME_NORMALIZED | VOLUME_NAME_NONE);
#else
    const DWORD length = GetFinalPathNameByHandleW(handle, name.data(), static_cast<DWORD>(name.size()),
                                                  FILE_NAME_NORMALIZED | VOLUME_NAME_NONE);
#endif
    if (!length || length >= name.size() || name[0] != L'\\' ||
        (length > 1 && name[1] == L'\\')) { reset(); errno = EACCES; return false; }
    const std::wstring resolved = drive + std::wstring(name.data(), length);
    // DOS volume-name resolution needs the mount manager, unavailable in the
    // AppContainer. Prove that our drive + normalized NONE path names this root.
    HANDLE check = CreateFileW(resolved.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (check == INVALID_HANDLE_VALUE) { reset(); errno = EACCES; return false; }
    BY_HANDLE_FILE_INFORMATION checked{};
    FILE_ID_INFO held_id{}, checked_id{};
    const bool same_root = GetFileType(check) == FILE_TYPE_DISK &&
        GetFileInformationByHandle(check, &checked) &&
        !(checked.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        (checked.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        GetFileInformationByHandleEx(handle, FileIdInfo, &held_id, sizeof(held_id)) &&
        GetFileInformationByHandleEx(check, FileIdInfo, &checked_id, sizeof(checked_id)) &&
        held_id.VolumeSerialNumber == checked_id.VolumeSerialNumber &&
        std::memcmp(held_id.FileId.Identifier, checked_id.FileId.Identifier,
                    sizeof(held_id.FileId.Identifier)) == 0;
    const bool closed = CloseHandle(check) != FALSE;
    if (!same_root || !closed) { reset(); errno = EACCES; return false; }
    path = fs::path(resolved);
#else
    fd = ::open(original.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    char name[absolute_limit + 1]{};
#ifdef __APPLE__
    if (::fcntl(fd, F_GETPATH, name) < 0) { reset(); return false; }
#elif defined(__linux__)
    const auto link = "/proc/self/fd/" + std::to_string(fd);
    const auto length = ::readlink(link.c_str(), name, sizeof(name) - 1);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(name) - 1) { reset(); errno = EACCES; return false; }
    name[length] = '\0';
#else
    reset(); errno = ENOTSUP; return false;
#endif
    path = fs::path(name);
#endif
    if (!path.is_absolute() || path.u8string().size() > absolute_limit) { reset(); errno = EACCES; return false; }
    return true;
  }
};
inline fs::path data_root(const fs::path& data) {
  Root root;
  return root.load(data) ? root.path : fs::path{};
}
inline Root session;
struct Directories {
#ifdef _WIN32
  std::vector<HANDLE> handles;
  ~Directories() { for (auto h : handles) CloseHandle(h); }
  bool start(const fs::path& root, const Root& capability = session) {
    if (root.empty() || root != capability.path || capability.handle == INVALID_HANDLE_VALUE) { errno = EACCES; return false; }
    HANDLE h;
    if (!DuplicateHandle(GetCurrentProcess(), capability.handle, GetCurrentProcess(), &h, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) { win_error(); return false; }
    handles.push_back(h); return true;
  }
  bool descend(const fs::path& part, bool create) {
    HANDLE h = relative_handle(handles.back(), part, FILE_TRAVERSE, create ? 3 : 1, true);
    if (h == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(h) != FILE_TYPE_DISK || !GetFileInformationByHandle(h, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      CloseHandle(h); errno = EACCES; return false;
    }
    handles.push_back(h); return true;
  }
#else
  int fd = -1;
  ~Directories() { if (fd >= 0) ::close(fd); }
  bool descend(const fs::path& part, bool create) {
    if (create && ::mkdirat(fd, part.c_str(), 0700) && errno != EEXIST) return false;
    int next = ::openat(fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0) return false;
    ::close(fd); fd = next; return true;
  }
  bool start(const fs::path& root, const Root& capability = session) {
    if (root.empty() || root != capability.path || capability.fd < 0) { errno = EACCES; return false; }
    fd = ::fcntl(capability.fd, F_DUPFD_CLOEXEC, 0);
    return fd >= 0;
  }
#endif
  Directories() = default;
  Directories(const Directories&) = delete;
  Directories& operator=(const Directories&) = delete;
};
inline bool directory(const fs::path& root, const std::string& relative, bool create,
                      const Root& capability = session) {
  if (root.empty() || !relative_valid(relative) || (root / fs::u8path(slashes(relative))).u8string().size() > absolute_limit) return false;
  Directories dirs;
  if (!dirs.start(root, capability)) return false;
  for (const auto& part : fs::u8path(slashes(relative))) if (!dirs.descend(part, create)) return false;
  return true;
}
enum class Mode { Read, Truncate, Exclusive };
inline FILE* open(const fs::path& root, const std::string& relative, Mode mode, bool parents = false,
                  const Root& capability = session) {
  if (root.empty() || !relative_valid(relative) || (root / fs::u8path(slashes(relative))).u8string().size() > absolute_limit) {
    errno = EINVAL; return nullptr;
  }
  const auto rel = fs::u8path(slashes(relative));
  Directories dirs;
  if (!dirs.start(root, capability)) { errno = EACCES; return nullptr; }
  for (const auto& part : rel.parent_path()) if (!dirs.descend(part, parents)) { errno = EACCES; return nullptr; }
  const bool write = mode != Mode::Read;
#ifdef _WIN32
  // FILE_CREATE is exclusive; truncation opens existing, inspects, then truncates.
  HANDLE h = relative_handle(dirs.handles.back(), rel.filename(), write ? GENERIC_WRITE : GENERIC_READ,
                             write ? 2 : 1, false);
  if (h == INVALID_HANDLE_VALUE && mode == Mode::Truncate && errno == EEXIST)
    h = relative_handle(dirs.handles.back(), rel.filename(), GENERIC_WRITE, 1, false);
  if (h == INVALID_HANDLE_VALUE) return nullptr;
  if (!regular(h, write)) { CloseHandle(h); errno = EINVAL; return nullptr; }
  if (mode == Mode::Truncate && !SetEndOfFile(h)) { CloseHandle(h); errno = EACCES; return nullptr; }
  const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h), _O_BINARY | (write ? _O_WRONLY : _O_RDONLY));
  if (fd < 0) { CloseHandle(h); return nullptr; }
  FILE* file = _fdopen(fd, write ? "wb" : "rb");
  if (!file) _close(fd);
#else
  const int flags = O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
  int fd = write ? ::openat(dirs.fd, rel.filename().c_str(), O_WRONLY | O_CREAT | O_EXCL | flags, 0600)
                 : ::openat(dirs.fd, rel.filename().c_str(), O_RDONLY | flags);
  if (fd < 0 && errno == EEXIST && mode == Mode::Truncate)
    fd = ::openat(dirs.fd, rel.filename().c_str(), O_WRONLY | flags);
  if (fd < 0) return nullptr;
  struct stat st{};
  if (::fstat(fd, &st) || !S_ISREG(st.st_mode) || (write && st.st_nlink != 1)) { ::close(fd); errno = EINVAL; return nullptr; }
  if (mode == Mode::Truncate && ::ftruncate(fd, 0)) { ::close(fd); return nullptr; }
  FILE* file = ::fdopen(fd, write ? "wb" : "rb");
  if (!file) ::close(fd);
#endif
  return file;
}
// Cap actual bytes read, including concurrent growth. A consumer parses this snapshot.
inline bool read(const fs::path& root, const std::string& relative, size_t limit, std::string& bytes,
                 const Root& capability = session) {
  bytes.clear();
  FILE* f = open(root, relative, Mode::Read, false, capability);
  if (!f) return false;
  bytes.resize(limit + 1);
  const size_t n = std::fread(bytes.data(), 1, bytes.size(), f);
  const bool ok = !std::ferror(f) && n <= limit;
  std::fclose(f);
  if (!ok) { bytes.clear(); errno = EFBIG; return false; }
  bytes.resize(n); return true;
}
struct Policy {
  fs::path root;
  std::string script, frame_times, sim_times, capture_directory, replay_directory;
};
inline Policy policy; // Publish once on main, before render/network workers start.
inline bool same(const std::string& a, const std::string& b) { return !b.empty() && slashes(a) == slashes(b); }
inline bool under(const std::string& path, const std::string& directory) {
  auto base = slashes(directory);
  while (!base.empty() && base.back() == '/') base.pop_back();
  const auto s = slashes(path);
  return !base.empty() && s.size() > base.size() && s.compare(0, base.size(), base) == 0 && s[base.size()] == '/';
}
inline bool capture(const std::string& path) { return under(path, policy.capture_directory); }
inline bool replay(const std::string& path) { return under(path, policy.replay_directory); }
inline std::string beneath(const std::string& path) {
  std::error_code ec;
  auto absolute = fs::absolute(fs::u8path(slashes(path)), ec).lexically_normal();
  if (ec) return {};
  // Component comparison (not a string prefix); never canonicalize an untrusted leaf.
  auto a = absolute.begin();
  for (auto b = policy.root.begin(); b != policy.root.end(); ++b, ++a)
    if (a == absolute.end() || *a != *b) return {};
  fs::path rel;
  for (; a != absolute.end(); ++a) rel /= *a;
  return relative_valid(rel.u8string()) ? rel.u8string() : std::string();
}
inline FILE* output(const std::string& path, bool replay = false, bool exclusive = false,
                    const char* fallback_mode = "wb") {
  if (!(same(path, policy.frame_times) || same(path, policy.sim_times) || capture(path) || (replay && agent_paths::replay(path))))
    return std::fopen(path.c_str(), exclusive ? "wbx" : fallback_mode);
  return open(policy.root, beneath(path), exclusive ? Mode::Exclusive : Mode::Truncate, replay);
}
inline bool script_bytes(const std::string& path, std::string& bytes) {
  return read(policy.root, beneath(path), script_limit, bytes);
}
// Directory-confined cleanup and exclusive publication for replay sidecars.
inline bool parent(const std::string& path, Directories& dirs, fs::path& leaf) {
  const auto rel = fs::u8path(beneath(path));
  if (rel.empty() || !dirs.start(policy.root)) { errno = EACCES; return false; }
  leaf = rel.filename();
  for (const auto& part : rel.parent_path()) if (!dirs.descend(part, false)) return false;
  return true;
}
inline int remove_output(const std::string& path) {
  if (!replay(path)) return std::remove(path.c_str());
  Directories dirs; fs::path leaf;
  if (!parent(path, dirs, leaf)) return -1;
#ifdef _WIN32
  HANDLE h = relative_handle(dirs.handles.back(), leaf, DELETE, 1, false);
  if (h == INVALID_HANDLE_VALUE) return -1;
  if (!regular(h)) { CloseHandle(h); errno = EACCES; return -1; }
  FILE_DISPOSITION_INFO info{}; info.DeleteFile = TRUE;
  const bool ok = SetFileInformationByHandle(h, FileDispositionInfo, &info, sizeof(info)) != 0;
  if (!ok) win_error();
  CloseHandle(h); return ok ? 0 : -1;
#else
  return ::unlinkat(dirs.fd, leaf.c_str(), 0);
#endif
}
inline void rename_output(const std::string& from, const std::string& to, std::error_code& ec) {
  if (!replay(from) && !replay(to)) { fs::rename(fs::u8path(from), fs::u8path(to), ec); return; }
  if (!replay(from) || !replay(to)) { ec = std::make_error_code(std::errc::permission_denied); return; }
  Directories src, dst; fs::path a, b;
  if (!parent(from, src, a) || !parent(to, dst, b)) { ec = std::make_error_code(std::errc::permission_denied); return; }
#ifdef _WIN32
  HANDLE h = relative_handle(src.handles.back(), a, DELETE, 1, false);
  if (h == INVALID_HANDLE_VALUE) { ec = std::make_error_code(std::errc::permission_denied); return; }
  if (!regular(h)) { CloseHandle(h); ec = std::make_error_code(std::errc::permission_denied); return; }
  // Win32 FileRenameInfo requires a null RootDirectory. Use the native operation
  // so publication stays relative to the held destination directory.
  using SetInformation = NTSTATUS (NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
  using Error = ULONG (WINAPI*)(NTSTATUS);
  const auto module = GetModuleHandleW(L"ntdll.dll");
  const auto set = reinterpret_cast<SetInformation>(GetProcAddress(module, "NtSetInformationFile"));
  const auto error = reinterpret_cast<Error>(GetProcAddress(module, "RtlNtStatusToDosError"));
  if (!set || !error) { CloseHandle(h); ec = std::make_error_code(std::errc::permission_denied); return; }
  struct NativeRenameInformation {
    BOOLEAN ReplaceIfExists;
    HANDLE RootDirectory;
    ULONG FileNameLength;
    WCHAR FileName[1];
  };
  const auto name = b.native();
  const size_t size = offsetof(NativeRenameInformation, FileName) + name.size() * sizeof(wchar_t);
  // operator new storage is suitably aligned for NativeRenameInformation.
  auto storage = std::make_unique<unsigned char[]>(size);
  auto* info = reinterpret_cast<NativeRenameInformation*>(storage.get());
  info->ReplaceIfExists = FALSE; info->RootDirectory = dst.handles.back();
  info->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
  std::memcpy(info->FileName, name.data(), info->FileNameLength);
  IO_STATUS_BLOCK status{};
  const NTSTATUS result = set(h, &status, info, static_cast<ULONG>(size),
                             static_cast<FILE_INFORMATION_CLASS>(10)); // FileRenameInformation
  if (result < 0) {
    ec = std::error_code(error(result), std::system_category()); CloseHandle(h); return;
  }
  CloseHandle(h);
#else
  struct stat before{}, after{};
  if (::fstatat(src.fd, a.c_str(), &before, AT_SYMLINK_NOFOLLOW) || !S_ISREG(before.st_mode) ||
      ::linkat(src.fd, a.c_str(), dst.fd, b.c_str(), 0)) { ec = std::make_error_code(std::errc::permission_denied); return; }
  // linkat does not follow the source symlink. Reject a replaced source as well.
  if (::fstatat(dst.fd, b.c_str(), &after, AT_SYMLINK_NOFOLLOW) || !S_ISREG(after.st_mode) ||
      before.st_dev != after.st_dev || before.st_ino != after.st_ino) {
    ::unlinkat(dst.fd, b.c_str(), 0); ec = std::make_error_code(std::errc::permission_denied); return;
  }
  if (::unlinkat(src.fd, a.c_str(), 0)) { ec = std::error_code(errno, std::generic_category()); return; }
#endif
  ec.clear();
}
// Keep the parser's original fgets(256) chunking, including long lines.
inline bool line(const std::string& bytes, size_t& offset, char* out, size_t capacity) {
  if (offset == bytes.size()) return false;
  size_t n = 0;
  while (offset < bytes.size() && n + 1 < capacity) { char c = bytes[offset++]; out[n++] = c; if (c == '\n') break; }
  out[n] = '\0'; return true;
}
inline bool ppm(const std::string& path, unsigned w, unsigned h, const void* data, size_t pitch) {
  FILE* f = output(path);
  if (!f) return false;
  bool ok = std::fprintf(f, "P6\n%u %u\n255\n", w, h) > 0;
  const auto* pixels = static_cast<const unsigned char*>(data);
  for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x)
    if (std::fwrite(pixels + y * pitch + x * 4, 1, 3, f) != 3) ok = false;
  return std::fclose(f) == 0 && ok;
}
} // namespace yougame::agent_paths
#endif
