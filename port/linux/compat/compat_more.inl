// Linux build: implementations for windows_more.h. Included at the end of compat.cpp, where its
// handle type and helpers are visible. SPDX-License-Identifier: GPL-2.0-or-later
#include <sys/un.h>
#include <poll.h>
#include <SDL.h>

BOOL CreatePipe(HANDLE* read_end, HANDLE* write_end, SECURITY_ATTRIBUTES* sa, DWORD) {
  int fds[2];
  // Windows inherits both ends when sa says so; the callers then clear inheritance on their own end
  // with SetHandleInformation. Close-on-exec plays that part here.
  if (pipe2(fds, (sa && sa->bInheritHandle) ? 0 : O_CLOEXEC) != 0) return fail_errno();
  auto* r = new Handle{Handle::File}; r->fd = fds[0];
  auto* w = new Handle{Handle::File}; w->fd = fds[1];
  *read_end = r; *write_end = w;
  return TRUE;
}
BOOL SetHandleInformation(HANDLE handle, DWORD mask, DWORD flags) {
  const int fd = linux_compat::fd_of_handle(handle);
  if (fd < 0) return FALSE;
  if (mask & HANDLE_FLAG_INHERIT) {
    const int f = fcntl(fd, F_GETFD);
    fcntl(fd, F_SETFD, (flags & HANDLE_FLAG_INHERIT) ? (f & ~FD_CLOEXEC) : (f | FD_CLOEXEC));
  }
  return TRUE;
}
HANDLE GetStdHandle(DWORD which) {
  static Handle in{Handle::Pseudo}, out{Handle::Pseudo}, err{Handle::Pseudo};
  in.kind = out.kind = err.kind = Handle::File;
  in.fd = 0; out.fd = 1; err.fd = 2;
  if (which == STD_INPUT_HANDLE) return &in;
  if (which == STD_OUTPUT_HANDLE) return &out;
  if (which == STD_ERROR_HANDLE) return &err;
  return INVALID_HANDLE_VALUE;
}
BOOL PeekNamedPipe(HANDLE pipe, LPVOID, DWORD, LPDWORD read, LPDWORD available, LPDWORD left) {
  const int fd = linux_compat::fd_of_handle(pipe);
  if (fd < 0) return FALSE;
  int n = 0;
  if (ioctl(fd, FIONREAD, &n) != 0) return fail_errno();
  // A socket or pipe whose other end went away polls readable with nothing in it: Windows reports a
  // broken pipe there, which the callers treat as closed.
  if (n == 0) {
    pollfd p{fd, POLLIN, 0};
    if (poll(&p, 1, 0) > 0 && (p.revents & (POLLHUP | POLLERR | POLLIN))) { g_last_error = ERROR_BROKEN_PIPE; return FALSE; }
  }
  if (read) *read = 0;
  if (available) *available = (DWORD)n;
  if (left) *left = 0;
  return TRUE;
}
BOOL CancelIoEx(HANDLE, LPVOID) { return TRUE; }
BOOL CancelIo(HANDLE) { return TRUE; }

namespace {
// \\.\pipe\discord-ipc-N is Discord's IPC socket on Linux, in the places the desktop, Flatpak and
// Snap clients create it.
int connect_discord(const std::string& pipe_name) {
  const std::string tail = pipe_name.substr(pipe_name.rfind('\\') + 1);
  std::vector<std::string> dirs;
  for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) if (const char* v = std::getenv(var)) dirs.push_back(v);
  dirs.push_back("/tmp");
  std::vector<std::string> candidates;
  for (const auto& d : dirs) {
    candidates.push_back(d + "/" + tail);
    candidates.push_back(d + "/app/com.discordapp.Discord/" + tail);
    candidates.push_back(d + "/snap.discord/" + tail);
    candidates.push_back(d + "/.flatpak/com.discordapp.Discord/xdg-run/" + tail);
  }
  for (const auto& path : candidates) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof addr.sun_path) { close(fd); continue; }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0) return fd;
    close(fd);
  }
  return -1;
}
}  // namespace
bool linux_compat_is_pipe_name(const char* path) { return path && !std::strncmp(path, "\\\\.\\pipe\\", 9); }
HANDLE linux_compat_open_pipe(const char* path) {
  const int fd = connect_discord(path);
  if (fd < 0) { g_last_error = ERROR_FILE_NOT_FOUND; return INVALID_HANDLE_VALUE; }
  auto* h = new Handle{Handle::File}; h->fd = fd;
  return h;
}

// ---- the clipboard ----
BOOL OpenClipboard(HWND) { return TRUE; }
BOOL EmptyClipboard() { return TRUE; }
BOOL CloseClipboard() { return TRUE; }
HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes) { return (flags & GMEM_ZEROINIT) ? std::calloc(1, bytes) : std::malloc(bytes); }
LPVOID GlobalLock(HGLOBAL mem) { return mem; }
BOOL GlobalUnlock(HGLOBAL) { return TRUE; }
HGLOBAL GlobalFree(HGLOBAL mem) { std::free(mem); return nullptr; }
HANDLE SetClipboardData(UINT format, HANDLE data) {
  if (!data) return nullptr;
  const std::string text = format == CF_UNICODETEXT ? narrow(static_cast<const wchar_t*>(data)) : std::string(static_cast<const char*>(data));
  std::free(data);   // the clipboard owns the memory once this succeeds, as on Windows
  if (SDL_WasInit(SDL_INIT_VIDEO) && SDL_SetClipboardText(text.c_str()) == 0) return (HANDLE)1;
  return nullptr;
}

// ---- narrow-path forms of the wide file functions ----
BOOL DeleteFileW(const char* path) { return DeleteFileA(path); }
BOOL MoveFileExW(const char* from, const char* to, DWORD flags) { return MoveFileExA(from, to, flags); }
BOOL CopyFileW(const char* from, const char* to, BOOL fail_if_exists) { return copy_file(from, to, fail_if_exists); }
BOOL CopyFileExW(const char* from, const char* to, LPPROGRESS_ROUTINE, LPVOID, BOOL* cancel, DWORD flags) {
  if (cancel && *cancel) return FALSE;
  return copy_file(from, to, (flags & 1u) != 0);
}
BOOL CreateDirectoryW(const char* path, SECURITY_ATTRIBUTES* sa) { return CreateDirectoryA(path, sa); }
BOOL GetFileAttributesExW(const char* path, GET_FILEEX_INFO_LEVELS level, LPVOID out) { return GetFileAttributesExA(path, level, out); }
DWORD GetFileAttributesW(const char* path) { return GetFileAttributesA(path); }
HANDLE CreateFileW(const char* path, DWORD access, DWORD share, SECURITY_ATTRIBUTES* sa, DWORD disposition, DWORD flags, HANDLE t) {
  return CreateFileA(path, access, share, sa, disposition, flags, t);
}
FILE* _wfopen(const char* path, const wchar_t* mode) { return std::fopen(path, narrow(mode).c_str()); }
int _wremove(const char* path) { return std::remove(path); }
int _wrename(const char* from, const char* to) { return std::rename(from, to); }
HINSTANCE ShellExecuteW(HWND window, LPCWSTR, const char* file, LPCWSTR params, LPCWSTR, int show) {
  const std::string p = narrow(params);
  return ShellExecuteA(window, nullptr, file, params ? p.c_str() : nullptr, nullptr, show);
}

// ---- other processes, waitable timers ----
HANDLE OpenProcess(DWORD, BOOL, DWORD pid) {
  if (!pid || kill((pid_t)pid, 0) != 0) { g_last_error = ERROR_ACCESS_DENIED; return nullptr; }
  auto* h = new Handle{Handle::Process}; h->pid = (pid_t)pid; h->foreign = true;
  return h;
}
BOOL QueryFullProcessImageNameW(HANDLE process, DWORD, LPWSTR name, LPDWORD size) {
  auto* h = static_cast<Handle*>(process);
  if (!h || h->kind != Handle::Process || !name || !size) return FALSE;
  std::error_code ec;
  const fs::path exe = fs::read_symlink("/proc/" + std::to_string(h->pid) + "/exe", ec);
  if (ec) return fail_errno();
  const std::wstring w = widen(exe.string().c_str());
  if (w.size() + 1 > *size) { g_last_error = 122; return FALSE; }
  std::wmemcpy(name, w.c_str(), w.size() + 1);
  *size = (DWORD)w.size();
  return TRUE;
}
HANDLE CreateWaitableTimerExW(SECURITY_ATTRIBUTES*, LPCWSTR, DWORD, DWORD) { return new Handle{Handle::Timer}; }
BOOL SetWaitableTimer(HANDLE timer, const LARGE_INTEGER* due, LONG, PTIMERAPCROUTINE, LPVOID, BOOL) {
  auto* h = static_cast<Handle*>(timer);
  if (!h || h->kind != Handle::Timer || !due) return FALSE;
  timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
  // Negative: relative, in 100 ns units. Positive (absolute FILETIME) is not used by the host.
  const int64_t ns = due->QuadPart < 0 ? -due->QuadPart * 100 : 0;
  int64_t total = (int64_t)now.tv_nsec + ns;
  h->due.tv_sec = now.tv_sec + (time_t)(total / 1000000000LL);
  h->due.tv_nsec = (long)(total % 1000000000LL);
  h->armed = true;
  return TRUE;
}
BOOL CancelWaitableTimer(HANDLE timer) {
  auto* h = static_cast<Handle*>(timer);
  if (!h || h->kind != Handle::Timer) return FALSE;
  h->armed = false;
  return TRUE;
}

// ---- stack and thread time ----
#include <execinfo.h>
#include <x86intrin.h>
void* NtCurrentTeb() {
  thread_local NT_TIB tib{};
  pthread_attr_t attr; void* base = nullptr; size_t size = 0;
  if (pthread_getattr_np(pthread_self(), &attr) == 0) {
    pthread_attr_getstack(&attr, &base, &size);
    pthread_attr_destroy(&attr);
  }
  // Windows' StackLimit is the lowest page committed so far; the nearest Linux has is where the
  // stack pointer is now, which the callers use the same way (how deep the thread has gone).
  volatile char here = 0;
  tib.StackBase = (char*)base + size;
  const uintptr_t sp_page = (uintptr_t)&here & ~(uintptr_t)4095;
  if (!tib.StackLimit || sp_page < (uintptr_t)tib.StackLimit) tib.StackLimit = (void*)sp_page;
  return &tib;
}
USHORT RtlCaptureStackBackTrace(ULONG skip, ULONG count, void** frames, ULONG* hash) {
  std::vector<void*> all(count + skip + 1);
  const int n = backtrace(all.data(), (int)all.size());
  const int start = std::min<int>(n, (int)skip + 1);   // leave this function out, as Windows does
  const int got = std::min<int>(n - start, (int)count);
  for (int i = 0; i < got; ++i) frames[i] = all[start + i];
  if (hash) *hash = 0;
  return (USHORT)std::max(got, 0);
}
BOOL QueryThreadCycleTime(HANDLE, ULONG64* cycles) {
  static const double tsc_per_ns = [] {
    timespec a{}, b{}; clock_gettime(CLOCK_MONOTONIC, &a);
    const uint64_t t0 = __rdtsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const uint64_t t1 = __rdtsc(); clock_gettime(CLOCK_MONOTONIC, &b);
    const double ns = (b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec);
    return ns > 0 ? (double)(t1 - t0) / ns : 3.0;
  }();
  timespec t{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) return FALSE;
  *cycles = (ULONG64)(((double)t.tv_sec * 1e9 + t.tv_nsec) * tsc_per_ns);
  return TRUE;
}

DWORD SetThreadExecutionState(DWORD flags) {
  if (SDL_WasInit(SDL_INIT_VIDEO)) {
    if (flags & ES_DISPLAY_REQUIRED) SDL_DisableScreenSaver(); else SDL_EnableScreenSaver();
  }
  return ES_CONTINUOUS;
}
DWORD GetFullPathNameA(LPCSTR path, DWORD size, LPSTR out, LPSTR* file_part) {
  std::error_code ec;
  const std::string full = fs::absolute(path ? path : "", ec).lexically_normal().string();
  if (ec) return 0;
  if (full.size() + 1 > size) return (DWORD)full.size() + 1;
  std::memcpy(out, full.c_str(), full.size() + 1);
  if (file_part) { char* slash = std::strrchr(out, '/'); *file_part = slash ? slash + 1 : out; }
  return (DWORD)full.size();
}
