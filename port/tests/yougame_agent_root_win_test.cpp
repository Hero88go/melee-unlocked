// Windows root identity checks; --prepare plants junctions before entering the jail.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_agent_paths.h"
#include <winioctl.h>
#include <cstdlib>

namespace ap = yougame::agent_paths;
namespace fs = std::filesystem;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "line %d: %s (errno=%d, Win32=%lu)\n", \
  __LINE__, #x, errno, GetLastError()); std::exit(1); } } while (0)

static std::wstring substituted;
static DWORD WINAPI substitute_none(HANDLE h, LPWSTR name, DWORD capacity, DWORD flags) {
  CHECK(h != INVALID_HANDLE_VALUE);
  CHECK(flags == (FILE_NAME_NORMALIZED | VOLUME_NAME_NONE));
  CHECK(substituted.size() + 1 <= capacity);
  std::memcpy(name, substituted.c_str(), (substituted.size() + 1) * sizeof(wchar_t));
  return static_cast<DWORD>(substituted.size());
}

// Independent real API call: no canonical() ancestor traversal or DOS mount lookup.
static fs::path real_path(const fs::path& original) {
  HANDLE h = CreateFileW(original.c_str(), FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  CHECK(h != INVALID_HANDLE_VALUE);
  std::vector<wchar_t> name(ap::absolute_limit + 1);
  const DWORD n = GetFinalPathNameByHandleW(h, name.data(), static_cast<DWORD>(name.size()),
      FILE_NAME_NORMALIZED | VOLUME_NAME_NONE);
  CHECK(CloseHandle(h) != FALSE);
  CHECK(n > 0 && n < name.size());
  CHECK(original.native().size() >= 3 && original.native()[1] == L':');
  return fs::path(original.native().substr(0, 2) + std::wstring(name.data(), n));
}

static void mkdir(const fs::path& path) { CHECK(CreateDirectoryW(path.c_str(), nullptr) != FALSE); }

static void junction(const fs::path& link, const fs::path& target) {
  mkdir(link);
  const auto absolute = real_path(target).native();
  const std::wstring substitute = L"\\??\\" + absolute;
  // Documented mount-point wire layout, without the ntifs.h SDK dependency.
  struct MountPoint {
    DWORD tag;
    WORD data_length, reserved;
    WORD substitute_offset, substitute_length, print_offset, print_length;
    WCHAR names[1];
  };
  static_assert(offsetof(MountPoint, names) == 16, "mount point wire layout");
  const size_t bytes = offsetof(MountPoint, names) +
      (substitute.size() + 1 + absolute.size() + 1) * sizeof(WCHAR);
  CHECK(bytes <= MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
  auto storage = std::make_unique<unsigned char[]>(bytes);
  std::memset(storage.get(), 0, bytes);
  auto* data = reinterpret_cast<MountPoint*>(storage.get());
  data->tag = IO_REPARSE_TAG_MOUNT_POINT;
  data->data_length = static_cast<WORD>(bytes - 8);
  data->substitute_length = static_cast<WORD>(substitute.size() * sizeof(WCHAR));
  data->print_offset = static_cast<WORD>((substitute.size() + 1) * sizeof(WCHAR));
  data->print_length = static_cast<WORD>(absolute.size() * sizeof(WCHAR));
  std::memcpy(data->names, substitute.c_str(), (substitute.size() + 1) * sizeof(WCHAR));
  std::memcpy(reinterpret_cast<unsigned char*>(data->names) + data->print_offset,
      absolute.c_str(), (absolute.size() + 1) * sizeof(WCHAR));
  HANDLE h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  CHECK(h != INVALID_HANDLE_VALUE);
  DWORD returned = 0;
  const BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, data, static_cast<DWORD>(bytes),
      nullptr, 0, &returned, nullptr);
  CHECK(CloseHandle(h) != FALSE);
  CHECK(ok != FALSE);
}

static bool appcontainer() {
  HANDLE token = nullptr;
  CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE);
  DWORD value = 0, size = 0;
  const BOOL ok = GetTokenInformation(token, TokenIsAppContainer, &value, sizeof(value), &size);
  CHECK(CloseHandle(token) != FALSE);
  CHECK(ok != FALSE && size == sizeof(value));
  return value != 0;
}

static void prepare(const fs::path& fixture) {
  CHECK(!appcontainer());
  mkdir(fixture);
  mkdir(fixture / L"root");
  mkdir(fixture / L"other");
  HANDLE file = CreateFileW((fixture / L"plain-file").c_str(), GENERIC_WRITE, 0,
      nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(file != INVALID_HANDLE_VALUE);
  CHECK(CloseHandle(file) != FALSE);
  // Point back at the held root: following this junction would pass identity.
  junction(fixture / L"junction", fixture / L"root");
}

static void refused(ap::Root& root, const fs::path& path) {
  errno = 0;
  CHECK(!root.load(path));
  CHECK(errno == EACCES);
  CHECK(root.handle == INVALID_HANDLE_VALUE && root.path.empty());
}

static void check_root(const fs::path& fixture, bool require_appcontainer) {
  const bool jailed = appcontainer();
  CHECK(!require_appcontainer || jailed);
  std::printf("TokenIsAppContainer=%u; junction fixtures were prepared unjailed\n", jailed ? 1u : 0u);
  const auto original = fixture / L"root";
  const auto expected = real_path(original);
  ap::Root root;
  CHECK(root.load(original));
  CHECK(root.path == expected && root.handle != INVALID_HANDLE_VALUE);
  CHECK(root.load(original / L".." / L"root"));
  CHECK(root.path == expected);

  ap::root_final_path = substitute_none;
  substituted = expected.native().substr(2);
  CHECK(root.load(original)); // Positive control for the test-only path substitution.
  CHECK(root.path == expected);
  substituted = real_path(fixture / L"other").native().substr(2);
  refused(root, original); // Valid directory on the same volume, different FileId.
  substituted = real_path(fixture).native().substr(2) + L"\\junction";
  refused(root, original); // Same target identity, but the check handle is a reparse point.
  substituted = real_path(fixture).native().substr(2) + L"\\plain-file";
  refused(root, original); // A composed path must name a directory.
  substituted = real_path(fixture).native().substr(2) + L"\\missing";
  refused(root, original); // Composed-path reopen failure.
  ap::root_final_path = GetFinalPathNameByHandleW;

  refused(root, fixture / L"junction");
  refused(root, L"\\\\server\\share\\root");
  refused(root, L"\\\\?\\" + expected.native());
  refused(root, L"\\\\.\\" + expected.native());
  refused(root, L"C:relative");
  refused(root, fixture / L"missing");
  CHECK(root.load(original)); // Rejections leave no stale state.
  CHECK(root.path == expected);
  // The runner-native.mjs count protocol lets this binary use the same launcher.
  std::printf("loader-proof-win: %u checks passed\n", checks);
}

int wmain(int argc, wchar_t** argv) {
  if (argc == 1) {
    std::vector<wchar_t> cwd(ap::absolute_limit + 1);
    const DWORD n = GetCurrentDirectoryW(static_cast<DWORD>(cwd.size()), cwd.data());
    CHECK(n > 0 && n < cwd.size());
    const auto fixture = fs::path(cwd.data()) /
        (L"yougame-agent-root-win-" + std::to_wstring(GetCurrentProcessId()));
    prepare(fixture);
    check_root(fixture, false);
    // Never recursively traverse junction fixtures during cleanup.
    CHECK(RemoveDirectoryW((fixture / L"junction").c_str()) != FALSE);
    CHECK(RemoveDirectoryW((fixture / L"root").c_str()) != FALSE);
    CHECK(RemoveDirectoryW((fixture / L"other").c_str()) != FALSE);
    CHECK(DeleteFileW((fixture / L"plain-file").c_str()) != FALSE);
    CHECK(RemoveDirectoryW(fixture.c_str()) != FALSE);
    return 0;
  }
  CHECK(argc == 3 || argc == 4);
  const std::wstring mode = argv[1];
  const fs::path fixture = argv[2];
  CHECK(fixture.is_absolute());
  if (mode == L"--prepare") {
    CHECK(argc == 3);
    prepare(fixture);
  } else {
    CHECK(mode == L"--check");
    CHECK(argc == 3 || std::wstring(argv[3]) == L"--require-appcontainer");
    check_root(fixture, argc == 4);
  }
  return 0;
}
