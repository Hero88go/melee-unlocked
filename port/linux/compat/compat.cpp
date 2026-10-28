// Linux build: POSIX implementations behind port/linux/compat's Win32 names.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "windows.h"
#include "winsock2.h"
#include "iphlpapi.h"
#include "bcrypt.h"
#include "commdlg.h"
#include "winhttp.h"
#include <curl/curl.h>
#include <openssl/evp.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <iconv.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern char** environ;
namespace fs = std::filesystem;

namespace {
thread_local DWORD g_last_error = 0;

std::string narrow(const wchar_t* w, size_t n = std::string::npos) {
  std::string out;
  if (!w) return out;
  if (n == std::string::npos) n = std::wcslen(w);
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    uint32_t c = (uint32_t)w[i];
    if (c < 0x80) out += (char)c;
    else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
    else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
  }
  return out;
}

std::wstring widen(const char* s, size_t n = std::string::npos) {
  std::wstring out;
  if (!s) return out;
  if (n == std::string::npos) n = std::strlen(s);
  for (size_t i = 0; i < n;) {
    const uint8_t b = (uint8_t)s[i];
    uint32_t c; int extra;
    if (b < 0x80) { c = b; extra = 0; }
    else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; extra = 1; }
    else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; extra = 2; }
    else if ((b & 0xF8) == 0xF0) { c = b & 0x07; extra = 3; }
    else { out += (wchar_t)0xFFFD; ++i; continue; }
    if (i + extra >= n + (extra ? 0 : 1) && extra) { out += (wchar_t)0xFFFD; break; }
    bool ok = true;
    for (int k = 1; k <= extra; ++k) {
      const uint8_t cont = (uint8_t)s[i + k];
      if ((cont & 0xC0) != 0x80) { ok = false; break; }
      c = (c << 6) | (cont & 0x3F);
    }
    if (!ok) { out += (wchar_t)0xFFFD; ++i; continue; }
    out += (wchar_t)c;
    i += extra + 1;
  }
  return out;
}

DWORD errno_to_win(int e) {
  switch (e) {
    case 0: return ERROR_SUCCESS;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES: case EPERM: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_ALREADY_EXISTS;
    case EBUSY: return ERROR_SHARING_VIOLATION;
    default: return (DWORD)e + 10000u;
  }
}
BOOL fail_errno() { g_last_error = errno_to_win(errno); return FALSE; }

// Handles: a file descriptor or a child process. Small integers would collide with nullptr/-1 tests,
// so each handle is a heap object.
struct Handle {
  enum Kind { File, Process, Pseudo, Snap, Timer } kind; int fd = -1; pid_t pid = 0; bool exited = false; int status = 0;
  bool foreign = false;      // a process this one did not start (OpenProcess): waited on through /proc
  timespec due{};            // a waitable timer's deadline on CLOCK_MONOTONIC
  bool armed = false;
  explicit Handle(Kind k) : kind(k) {}
  virtual ~Handle() = default;
};
Handle g_current_process{Handle::Pseudo}, g_current_thread{Handle::Pseudo};

// Splits a Windows command line the way CommandLineToArgvW does (quotes, backslashes before quotes).
std::vector<std::string> split_command_line(const std::string& line) {
  std::vector<std::string> args;
  std::string cur; bool in_quotes = false, have = false;
  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (c == '\\') {
      size_t n = 0; while (i < line.size() && line[i] == '\\') { ++n; ++i; }
      if (i < line.size() && line[i] == '"') { cur.append(n / 2, '\\'); if (n % 2) { cur += '"'; } else { in_quotes = !in_quotes; } have = true; }
      else { cur.append(n, '\\'); --i; have = true; }
    } else if (c == '"') { in_quotes = !in_quotes; have = true; }
    else if ((c == ' ' || c == '\t') && !in_quotes) { if (have) { args.push_back(cur); cur.clear(); have = false; } }
    else { cur += c; have = true; }
  }
  if (have) args.push_back(cur);
  return args;
}

// A Windows program name (MeleeUnlocked.exe, melee_source.exe) launched from the shared code: the
// Linux build ships the same programs without the extension, beside this one.
std::string linux_program(std::string path) {
  for (char& c : path) if (c == '\\') c = '/';
  if (path.size() > 4) {
    std::string tail = path.substr(path.size() - 4);
    for (char& c : tail) c = (char)std::tolower((unsigned char)c);
    if (tail == ".exe") path.resize(path.size() - 4);
  }
  if (path.find('/') == std::string::npos) {
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec && fs::exists(self.parent_path() / path)) return (self.parent_path() / path).string();
  }
  return path;
}

bool run_capture(const std::vector<std::string>& argv, std::string* out, int* status) {
  int pipefd[2];
  if (pipe(pipefd) != 0) return false;
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pipefd[1], 1);
  posix_spawn_file_actions_addclose(&actions, pipefd[0]);
  std::vector<char*> args;
  for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(pipefd[1]);
  if (rc != 0) { close(pipefd[0]); return false; }
  char buf[4096]; ssize_t n;
  while ((n = read(pipefd[0], buf, sizeof buf)) > 0) if (out) out->append(buf, (size_t)n);
  close(pipefd[0]);
  int st = 0; waitpid(pid, &st, 0);
  if (status) *status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
  return true;
}

bool have_program(const char* name) {
  const char* path = std::getenv("PATH");
  if (!path) return false;
  std::stringstream ss(path); std::string dir;
  while (std::getline(ss, dir, ':')) if (!dir.empty() && access((dir + "/" + name).c_str(), X_OK) == 0) return true;
  return false;
}

void spawn_detached(const std::vector<std::string>& argv) {
  std::vector<char*> args;
  for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  pid_t pid = 0;
  if (posix_spawnp(&pid, args[0], nullptr, nullptr, args.data(), environ) == 0) {
    std::thread([pid] { int st; waitpid(pid, &st, 0); }).detach();
  }
}
}  // namespace

// ---- time ----
void Sleep(DWORD ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
ULONGLONG GetTickCount64() {
  timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ULONGLONG)ts.tv_sec * 1000u + (ULONGLONG)ts.tv_nsec / 1000000u;
}
DWORD GetTickCount() { return (DWORD)GetTickCount64(); }
BOOL QueryPerformanceCounter(LARGE_INTEGER* out) {
  timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  out->QuadPart = (LONGLONG)ts.tv_sec * 1000000000LL + ts.tv_nsec;
  return TRUE;
}
BOOL QueryPerformanceFrequency(LARGE_INTEGER* out) { out->QuadPart = 1000000000LL; return TRUE; }

// ---- process and threads ----
DWORD GetCurrentProcessId() { return (DWORD)getpid(); }
HANDLE GetCurrentProcess() { return &g_current_process; }
HANDLE GetCurrentThread() { return &g_current_thread; }
BOOL SetThreadPriority(HANDLE, int priority) {
  // Background mode (I/O and CPU priority lowered for a while, then restored).
  if (priority == THREAD_MODE_BACKGROUND_BEGIN) { (void)setpriority(PRIO_PROCESS, (id_t)gettid(), 10); return TRUE; }
  if (priority == THREAD_MODE_BACKGROUND_END) { (void)setpriority(PRIO_PROCESS, (id_t)gettid(), 0); return TRUE; }
  // Raising a thread's priority needs privileges on Linux; lowering never does. Ask, accept a refusal.
  const int nice_value = priority >= THREAD_PRIORITY_TIME_CRITICAL ? -10 : priority > 0 ? -priority * 2 : -priority * 2;
  if (setpriority(PRIO_PROCESS, (id_t)gettid(), nice_value) != 0 && priority > 0 && priority < THREAD_PRIORITY_HIGHEST) {
    // Refused (no privileges): the game thread asked for HIGHEST and stays at the normal level, and
    // so would every helper that asked for ABOVE_NORMAL (the sub-frame solver, the presentation
    // thread), all competing with the game thread on equal terms. Keep the order the other way
    // round: those helpers go a little below normal, which needs no privileges. A player at high
    // ping with sub-frames on saw the game fall below 60 frames a second on Linux.
    (void)setpriority(PRIO_PROCESS, (id_t)gettid(), 2);
  }
  return TRUE;
}
DWORD GetLastError() { return g_last_error; }
void SetLastError(DWORD error) { g_last_error = error; }
BOOL CloseHandle(HANDLE handle) {
  if (!handle || handle == INVALID_HANDLE_VALUE) return FALSE;
  auto* h = static_cast<Handle*>(handle);
  if (h->kind == Handle::Pseudo) return TRUE;
  if (h->kind == Handle::File && h->fd >= 0) close(h->fd);
  delete h;
  return TRUE;
}
static bool reap(Handle* h, bool block) {
  if (h->exited) return true;
  if (h->foreign) {
    for (;;) {
      if (kill(h->pid, 0) != 0 && errno == ESRCH) { h->exited = true; h->status = 0; return true; }
      if (!block) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  int st = 0;
  const pid_t r = waitpid(h->pid, &st, block ? 0 : WNOHANG);
  if (r == h->pid) { h->exited = true; h->status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st); return true; }
  if (r < 0) { h->exited = true; h->status = -1; return true; }
  return false;
}
DWORD WaitForSingleObject(HANDLE handle, DWORD ms) {
  auto* h = static_cast<Handle*>(handle);
  if (h && h != INVALID_HANDLE_VALUE && h->kind == Handle::Timer) {
    if (!h->armed) return ms == INFINITE ? WAIT_FAILED : WAIT_TIMEOUT;
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &h->due, nullptr) == EINTR) {}
    h->armed = false;
    return WAIT_OBJECT_0;
  }
  if (!h || h == INVALID_HANDLE_VALUE || h->kind != Handle::Process) return WAIT_FAILED;
  if (ms == INFINITE) { reap(h, true); return WAIT_OBJECT_0; }
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  for (;;) {
    if (reap(h, false)) return WAIT_OBJECT_0;
    if (std::chrono::steady_clock::now() >= until) return WAIT_TIMEOUT;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
BOOL GetExitCodeProcess(HANDLE handle, LPDWORD code) {
  auto* h = static_cast<Handle*>(handle);
  if (!h || h->kind != Handle::Process || !code) return FALSE;
  *code = reap(h, false) ? (DWORD)h->status : STILL_ACTIVE;
  return TRUE;
}
BOOL TerminateProcess(HANDLE handle, UINT code) {
  auto* h = static_cast<Handle*>(handle);
  if (!h) return FALSE;
  if (h->kind == Handle::Pseudo) _exit((int)code);
  if (h->kind != Handle::Process) return FALSE;
  return kill(h->pid, SIGKILL) == 0;
}
static BOOL create_process(const std::string& application, const std::string& command_line, const std::string& directory,
                           STARTUPINFOW* startup, PROCESS_INFORMATION* info) {
  std::vector<std::string> args = split_command_line(command_line);
  if (!application.empty()) { if (args.empty()) args.push_back(application); }
  if (args.empty()) { g_last_error = ERROR_FILE_NOT_FOUND; return FALSE; }
  const std::string program = linux_program(application.empty() ? args[0] : application);
  args[0] = program;
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (!directory.empty()) posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
  if (startup && (startup->dwFlags & STARTF_USESTDHANDLES)) {
    auto dup_into = [&](HANDLE handle, int target) {
      auto* h = static_cast<Handle*>(handle);
      if (h && h != INVALID_HANDLE_VALUE && h->kind == Handle::File && h->fd >= 0) posix_spawn_file_actions_adddup2(&actions, h->fd, target);
    };
    dup_into(startup->hStdInput, 0); dup_into(startup->hStdOutput, 1); dup_into(startup->hStdError, 2);
  }
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  pid_t pid = 0;
  // A bare program name (tar, xdg-open) is looked up on PATH, as Windows looks in its own folders.
  const int rc = program.find('/') == std::string::npos
      ? posix_spawnp(&pid, program.c_str(), &actions, nullptr, argv.data(), environ)
      : posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) { errno = rc; return fail_errno(); }
  if (info) {
    auto* h = new Handle{Handle::Process}; h->pid = pid;
    info->hProcess = h; info->hThread = &g_current_thread;
    info->dwProcessId = (DWORD)pid; info->dwThreadId = 0;
  }
  return TRUE;
}
static std::string path_arg(const LinuxPathArg& p) { return p.narrow ? std::string(p.narrow) : narrow(p.wide); }
BOOL CreateProcessW(LinuxPathArg application, LPWSTR command_line, SECURITY_ATTRIBUTES*, SECURITY_ATTRIBUTES*, BOOL, DWORD,
                    LPVOID, LinuxPathArg directory, STARTUPINFOW* startup, PROCESS_INFORMATION* info) {
  return create_process(path_arg(application), narrow(command_line), path_arg(directory), startup, info);
}
BOOL CreateProcessA(LPCSTR application, LPSTR command_line, SECURITY_ATTRIBUTES*, SECURITY_ATTRIBUTES*, BOOL, DWORD,
                    LPVOID, LPCSTR directory, STARTUPINFOA* startup, PROCESS_INFORMATION* info) {
  return create_process(application ? application : "", command_line ? command_line : "", directory ? directory : "", startup, info);
}
BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value) {
  return (value ? setenv(name, value, 1) : unsetenv(name)) == 0;
}
DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size) {
  const char* v = std::getenv(name);
  if (!v) { g_last_error = 203; return 0; }
  const DWORD n = (DWORD)std::strlen(v);
  if (!buffer || size <= n) return n + 1;
  std::memcpy(buffer, v, n + 1);
  return n;
}
LPWSTR GetCommandLineW() {
  static std::wstring line = [] {
    std::ifstream in("/proc/self/cmdline", std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::wstring out; size_t start = 0;
    for (size_t i = 0; i <= all.size(); ++i) {
      if (i == all.size() || all[i] == 0) {
        if (i > start) { if (!out.empty()) out += L' '; std::wstring arg = widen(all.data() + start, i - start);
          if (arg.find(L' ') != std::wstring::npos) out += L'"' + arg + L'"'; else out += arg; }
        start = i + 1;
      }
    }
    return out;
  }();
  return line.data();
}
void InitializeCriticalSection(CRITICAL_SECTION* cs) { cs->impl = new std::recursive_mutex; }
void DeleteCriticalSection(CRITICAL_SECTION* cs) { delete static_cast<std::recursive_mutex*>(cs->impl); cs->impl = nullptr; }
void EnterCriticalSection(CRITICAL_SECTION* cs) { static_cast<std::recursive_mutex*>(cs->impl)->lock(); }
void LeaveCriticalSection(CRITICAL_SECTION* cs) { static_cast<std::recursive_mutex*>(cs->impl)->unlock(); }

// ---- modules ----
DWORD GetModuleFileNameA(HMODULE module, LPSTR out, DWORD size) {
  std::string path;
  if (!module) {
    std::error_code ec; path = fs::read_symlink("/proc/self/exe", ec).string();
  } else {
    Dl_info info{};
    void* sym = dlsym(module, "mu_game_entry");
    if (sym && dladdr(sym, &info) && info.dli_fname) path = info.dli_fname;
  }
  if (!out || !size) return 0;
  const DWORD n = (DWORD)std::min<size_t>(path.size(), size - 1);
  std::memcpy(out, path.data(), n); out[n] = 0;
  return n;
}
DWORD GetModuleFileNameW(HMODULE module, LPWSTR out, DWORD size) {
  char buf[MAX_PATH];
  GetModuleFileNameA(module, buf, sizeof buf);
  const std::wstring w = widen(buf);
  if (!out || !size) return 0;
  const DWORD n = (DWORD)std::min<size_t>(w.size(), size - 1);
  std::wmemcpy(out, w.data(), n); out[n] = 0;
  return n;
}
HMODULE GetModuleHandleA(LPCSTR name) { return name ? dlopen(name, RTLD_NOLOAD | RTLD_LAZY) : dlopen(nullptr, RTLD_LAZY); }
HMODULE GetModuleHandleW(LPCWSTR name) { return name ? GetModuleHandleA(narrow(name).c_str()) : GetModuleHandleA(nullptr); }
static std::string library_name(std::string name) {
  for (char& c : name) if (c == '\\') c = '/';
  if (name.size() > 4) {
    std::string tail = name.substr(name.size() - 4);
    for (char& c : tail) c = (char)std::tolower((unsigned char)c);
    if (tail == ".dll") name = name.substr(0, name.size() - 4) + ".so";
  }
  return name;
}
HMODULE LoadLibraryA(LPCSTR name) {
  const std::string lib = library_name(name);
  // A bare name is looked for beside the program first, as Windows does.
  if (lib.find('/') == std::string::npos) {
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec && fs::exists(self.parent_path() / lib))
      if (HMODULE h = dlopen((self.parent_path() / lib).c_str(), RTLD_NOW | RTLD_LOCAL)) return h;
  }
  return dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
}
HMODULE LoadLibraryW(LPCWSTR name) { return LoadLibraryA(narrow(name).c_str()); }
FARPROC GetProcAddress(HMODULE module, LPCSTR name) { return module ? dlsym(module, name) : nullptr; }
BOOL FreeLibrary(HMODULE module) { return module && dlclose(module) == 0; }

// ---- memory ----
static int prot_of(DWORD protect) {
  switch (protect & 0xFF) {
    case PAGE_NOACCESS: return PROT_NONE;
    case PAGE_READONLY: return PROT_READ;
    case PAGE_READWRITE: return PROT_READ | PROT_WRITE;
    case PAGE_EXECUTE: return PROT_EXEC;
    case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
    case PAGE_EXECUTE_READWRITE: return PROT_READ | PROT_WRITE | PROT_EXEC;
    default: return PROT_READ | PROT_WRITE;
  }
}
LPVOID VirtualAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protect) {
  const int prot = (type & MEM_COMMIT) ? prot_of(protect) : PROT_NONE;
  if (address && !(type & MEM_RESERVE)) {   // commit inside an earlier reservation
    return mprotect(address, size, prot) == 0 ? address : nullptr;
  }
  int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
  void* p = mmap(address, size, prot, address ? flags | MAP_FIXED_NOREPLACE : flags, -1, 0);
  // WSL1 and kernels before 4.17 reject or ignore NOREPLACE: a plain hint, whose result is checked
  // below, can never replace a live mapping either.
  if (address && p == MAP_FAILED && (errno == EINVAL || errno == ENOTSUP || errno == ENOSYS))
    p = mmap(address, size, prot, flags, -1, 0);
  if (p == MAP_FAILED) { fail_errno(); return nullptr; }
  if (address && p != address) { munmap(p, size); return nullptr; }
  return p;
}
// Windows releases a whole reservation from its base with size 0; the size of each reservation is
// remembered for that.
BOOL VirtualFree(LPVOID address, SIZE_T size, DWORD type) {
  if (!address) return FALSE;
  if (type & MEM_RELEASE) {
    if (!size) {
      // Find the mapping's extent in /proc/self/maps.
      std::ifstream maps("/proc/self/maps"); std::string line;
      const uintptr_t a = (uintptr_t)address;
      while (std::getline(maps, line)) {
        uintptr_t lo = 0, hi = 0;
        if (std::sscanf(line.c_str(), "%lx-%lx", &lo, &hi) == 2 && lo == a) { size = hi - lo; break; }
      }
      if (!size) return FALSE;
    }
    return munmap(address, size) == 0;
  }
  return mprotect(address, size, PROT_NONE) == 0;   // decommit
}
BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD protect, DWORD* old_protect) {
  const uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
  const uintptr_t lo = (uintptr_t)address & ~(page - 1);
  const uintptr_t hi = ((uintptr_t)address + size + page - 1) & ~(page - 1);
  if (old_protect) *old_protect = PAGE_EXECUTE_READWRITE;
  return mprotect((void*)lo, hi - lo, prot_of(protect)) == 0 ? TRUE : fail_errno();
}
BOOL FlushInstructionCache(HANDLE, LPCVOID address, SIZE_T size) {
  __builtin___clear_cache((char*)address, (char*)address + size);
  return TRUE;
}
void GetSystemInfo(SYSTEM_INFO* info) {
  std::memset(info, 0, sizeof *info);
  info->dwPageSize = (DWORD)sysconf(_SC_PAGESIZE);
  info->dwAllocationGranularity = 65536;
  info->dwNumberOfProcessors = (DWORD)std::max(1L, sysconf(_SC_NPROCESSORS_ONLN));
}
void GetCurrentThreadStackLimits(ULONG_PTR* low, ULONG_PTR* high) {
  pthread_attr_t attr; void* base = nullptr; size_t size = 0;
  pthread_getattr_np(pthread_self(), &attr);
  pthread_attr_getstack(&attr, &base, &size);
  pthread_attr_destroy(&attr);
  if (low) *low = (ULONG_PTR)base;
  if (high) *high = (ULONG_PTR)base + size;
}

// ---- text ----
// Code page 932 (Shift-JIS) is what the game uses for names and connect codes. Every other code
// page is treated as UTF-8. Shift-JIS goes through iconv; without its tables a built-in map covers
// ASCII, the full-width letters and digits and the symbols connect codes use.
namespace {
constexpr UINT kShiftJis = 932;

// Full-width symbols of the game's text, the same pairs slippi_net.cpp writes.
constexpr struct { wchar_t wide; uint16_t sjis; } kSjisSymbols[] = {
    {L'　', 0x8140}, {L'！', 0x8149}, {L'”', 0x8168}, {L'＃', 0x8194}, {L'＄', 0x8190},
    {L'％', 0x8193}, {L'＆', 0x8195}, {L'’', 0x8166}, {L'（', 0x8169}, {L'）', 0x816A},
    {L'＊', 0x8196}, {L'＋', 0x817B}, {L'，', 0x8143}, {L'－', 0x817C}, {L'．', 0x8144},
    {L'／', 0x815E}, {L'：', 0x8146}, {L'；', 0x8147}, {L'＜', 0x8183}, {L'＝', 0x8181},
    {L'＞', 0x8184}, {L'？', 0x8148}, {L'＠', 0x8197}, {L'［', 0x816D}, {L'＼', 0x815F},
    {L'］', 0x816E}, {L'＾', 0x814F}, {L'＿', 0x8151}, {L'｀', 0x814D}, {L'｛', 0x816F},
    {L'｜', 0x8162}, {L'｝', 0x8170}, {L'～', 0x8160},
};

wchar_t sjis_pair_to_wide(uint16_t code) {
  if (code >= 0x824F && code <= 0x8258) return (wchar_t)(0xFF10 + (code - 0x824F));   // digits
  if (code >= 0x8260 && code <= 0x8279) return (wchar_t)(0xFF21 + (code - 0x8260));   // A-Z
  if (code >= 0x8281 && code <= 0x829A) return (wchar_t)(0xFF41 + (code - 0x8281));   // a-z
  for (const auto& s : kSjisSymbols) if (s.sjis == code) return s.wide;
  return L'�';
}
int wide_to_sjis_pair(wchar_t c) {
  if (c >= 0xFF10 && c <= 0xFF19) return 0x824F + (c - 0xFF10);
  if (c >= 0xFF21 && c <= 0xFF3A) return 0x8260 + (c - 0xFF21);
  if (c >= 0xFF41 && c <= 0xFF5A) return 0x8281 + (c - 0xFF41);
  for (const auto& s : kSjisSymbols) if (s.wide == c) return s.sjis;
  return -1;
}

iconv_t open_sjis(bool to_wide) {
  for (const char* name : {"CP932", "SHIFT_JIS", "SJIS"}) {
    iconv_t cd = to_wide ? iconv_open("WCHAR_T", name) : iconv_open(name, "WCHAR_T");
    if (cd != (iconv_t)-1) return cd;
  }
  return (iconv_t)-1;
}

std::wstring sjis_widen(const char* s, size_t n) {
  std::wstring out;
  iconv_t cd = open_sjis(true);
  if (cd != (iconv_t)-1) {
    char* src = const_cast<char*>(s);
    size_t left = n;
    while (left > 0) {
      wchar_t buf[256];
      char* dst = (char*)buf;
      size_t room = sizeof buf;
      const size_t r = iconv(cd, &src, &left, &dst, &room);
      out.append(buf, (wchar_t*)dst - buf);
      if (r == (size_t)-1 && errno != E2BIG) {   // a byte iconv cannot read: one replacement, go on
        out.push_back(L'�');
        ++src; --left;
        iconv(cd, nullptr, nullptr, nullptr, nullptr);
      }
    }
    iconv_close(cd);
    return out;
  }
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = (unsigned char)s[i];
    if (c < 0x80) { out.push_back((wchar_t)c); continue; }
    if (c >= 0xA1 && c <= 0xDF) { out.push_back((wchar_t)(0xFF61 + (c - 0xA1))); continue; }   // half-width kana
    if (i + 1 < n) { out.push_back(sjis_pair_to_wide((uint16_t)((c << 8) | (unsigned char)s[i + 1]))); ++i; }
    else out.push_back(L'�');
  }
  return out;
}

std::string sjis_narrow(const wchar_t* w, size_t n) {
  std::string out;
  iconv_t cd = open_sjis(false);
  if (cd != (iconv_t)-1) {
    char* src = (char*)const_cast<wchar_t*>(w);
    size_t left = n * sizeof(wchar_t);
    while (left > 0) {
      char buf[512];
      char* dst = buf;
      size_t room = sizeof buf;
      const size_t r = iconv(cd, &src, &left, &dst, &room);
      out.append(buf, dst - buf);
      if (r == (size_t)-1 && errno != E2BIG) {   // no Shift-JIS form: '?', as Windows writes
        out.push_back('?');
        src += sizeof(wchar_t); left -= sizeof(wchar_t);
        iconv(cd, nullptr, nullptr, nullptr, nullptr);
      }
    }
    iconv_close(cd);
    return out;
  }
  for (size_t i = 0; i < n; ++i) {
    const wchar_t c = w[i];
    if (c < 0x80) { out.push_back((char)c); continue; }
    if (c >= 0xFF61 && c <= 0xFF9F) { out.push_back((char)(0xA1 + (c - 0xFF61))); continue; }
    const int pair = wide_to_sjis_pair(c);
    if (pair < 0) { out.push_back('?'); continue; }
    out.push_back((char)(pair >> 8));
    out.push_back((char)(pair & 0xFF));
  }
  return out;
}
}  // namespace

int MultiByteToWideChar(UINT code_page, DWORD, LPCSTR in, int in_bytes, LPWSTR out, int out_chars) {
  if (!in) return 0;
  const bool with_null = in_bytes < 0;
  const size_t n = with_null ? std::strlen(in) : (size_t)in_bytes;
  std::wstring w = code_page == kShiftJis ? sjis_widen(in, n) : widen(in, n);
  if (with_null) w.push_back(L'\0');
  if (!out_chars) return (int)w.size();
  if ((int)w.size() > out_chars) { g_last_error = 122; return 0; }
  std::wmemcpy(out, w.data(), w.size());
  return (int)w.size();
}
int WideCharToMultiByte(UINT code_page, DWORD, LPCWSTR in, int in_chars, LPSTR out, int out_bytes, LPCSTR, BOOL* used_default) {
  if (used_default) *used_default = FALSE;
  if (!in) return 0;
  const bool with_null = in_chars < 0;
  const size_t n = with_null ? std::wcslen(in) : (size_t)in_chars;
  std::string s = code_page == kShiftJis ? sjis_narrow(in, n) : narrow(in, n);
  if (with_null) s.push_back('\0');
  if (!out_bytes) return (int)s.size();
  if ((int)s.size() > out_bytes) { g_last_error = 122; return 0; }
  std::memcpy(out, s.data(), s.size());
  return (int)s.size();
}
FILE* _wfopen(const wchar_t* path, const wchar_t* mode) { return std::fopen(narrow(path).c_str(), narrow(mode).c_str()); }
int _wremove(const wchar_t* path) { return std::remove(narrow(path).c_str()); }
int _wrename(const wchar_t* from, const wchar_t* to) { return std::rename(narrow(from).c_str(), narrow(to).c_str()); }

// ---- files ----
BOOL DeleteFileA(LPCSTR path) { return unlink(path) == 0 ? TRUE : fail_errno(); }
BOOL DeleteFileW(LPCWSTR path) { return DeleteFileA(narrow(path).c_str()); }
static BOOL copy_file(const std::string& from, const std::string& to, bool fail_if_exists) {
  std::error_code ec;
  fs::copy_file(from, to, fail_if_exists ? fs::copy_options::none : fs::copy_options::overwrite_existing, ec);
  if (ec) { errno = ec.value(); return fail_errno(); }
  return TRUE;
}
BOOL MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags) {
  if (!to) return DeleteFileA(from);
  if (!(flags & MOVEFILE_REPLACE_EXISTING) && access(to, F_OK) == 0) { g_last_error = ERROR_ALREADY_EXISTS; return FALSE; }
  if (rename(from, to) == 0) {
    if (flags & MOVEFILE_WRITE_THROUGH) { int fd = open(to, O_RDONLY); if (fd >= 0) { fsync(fd); close(fd); } }
    return TRUE;
  }
  if (errno == EXDEV && (flags & MOVEFILE_COPY_ALLOWED)) {
    if (!copy_file(from, to, false)) return FALSE;
    unlink(from);
    return TRUE;
  }
  return fail_errno();
}
BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) {
  const std::string t = narrow(to);
  return MoveFileExA(narrow(from).c_str(), to ? t.c_str() : nullptr, flags);
}
BOOL CopyFileW(LPCWSTR from, LPCWSTR to, BOOL fail_if_exists) { return copy_file(narrow(from), narrow(to), fail_if_exists); }
BOOL CopyFileExW(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE, LPVOID, BOOL* cancel, DWORD flags) {
  if (cancel && *cancel) return FALSE;
  return copy_file(narrow(from), narrow(to), (flags & 1u) != 0);   // COPY_FILE_FAIL_IF_EXISTS
}
BOOL CreateDirectoryA(LPCSTR path, SECURITY_ATTRIBUTES*) { return mkdir(path, 0755) == 0 ? TRUE : fail_errno(); }
BOOL CreateDirectoryW(LPCWSTR path, SECURITY_ATTRIBUTES* sa) { return CreateDirectoryA(narrow(path).c_str(), sa); }
static void to_filetime(const timespec& ts, FILETIME* ft) {
  const uint64_t t = ((uint64_t)ts.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)ts.tv_nsec / 100u;
  ft->dwLowDateTime = (DWORD)t; ft->dwHighDateTime = (DWORD)(t >> 32);
}
BOOL GetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS, LPVOID out) {
  struct stat st{};
  if (stat(path, &st) != 0) return fail_errno();
  auto* data = static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(out);
  std::memset(data, 0, sizeof *data);
  data->dwFileAttributes = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
  to_filetime(st.st_ctim, &data->ftCreationTime);
  to_filetime(st.st_atim, &data->ftLastAccessTime);
  to_filetime(st.st_mtim, &data->ftLastWriteTime);
  data->nFileSizeHigh = (DWORD)((uint64_t)st.st_size >> 32);
  data->nFileSizeLow = (DWORD)st.st_size;
  return TRUE;
}
BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID out) {
  return GetFileAttributesExA(narrow(path).c_str(), level, out);
}
DWORD GetFileAttributesA(LPCSTR path) {
  struct stat st{};
  if (stat(path, &st) != 0) { fail_errno(); return INVALID_FILE_ATTRIBUTES; }
  return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
DWORD GetFileAttributesW(LPCWSTR path) { return GetFileAttributesA(narrow(path).c_str()); }
bool linux_compat_is_pipe_name(const char* path);
HANDLE linux_compat_open_pipe(const char* path);
HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD, SECURITY_ATTRIBUTES*, DWORD disposition, DWORD flags, HANDLE) {
  if (linux_compat_is_pipe_name(path)) return linux_compat_open_pipe(path);
  int oflags = O_CLOEXEC;
  if ((access & GENERIC_READ) && (access & GENERIC_WRITE)) oflags |= O_RDWR;
  else if (access & GENERIC_WRITE) oflags |= O_WRONLY;
  else oflags |= O_RDONLY;
  switch (disposition) {
    case CREATE_NEW: oflags |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: oflags |= O_CREAT | O_TRUNC; break;
    case OPEN_ALWAYS: oflags |= O_CREAT; break;
    case TRUNCATE_EXISTING: oflags |= O_TRUNC; break;
    default: break;
  }
  if (flags & FILE_FLAG_WRITE_THROUGH) oflags |= O_DSYNC;
  const int fd = open(path, oflags, 0644);
  if (fd < 0) { fail_errno(); return INVALID_HANDLE_VALUE; }
  auto* h = new Handle{Handle::File}; h->fd = fd;
  return h;
}
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, SECURITY_ATTRIBUTES* sa, DWORD disposition, DWORD flags,
                   HANDLE t) {
  return CreateFileA(narrow(path).c_str(), access, share, sa, disposition, flags, t);
}
BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD bytes, LPDWORD got, LPVOID) {
  auto* h = static_cast<Handle*>(file);
  if (!h || h == INVALID_HANDLE_VALUE || h->kind != Handle::File) return FALSE;
  const ssize_t n = read(h->fd, buffer, bytes);
  if (n < 0) return fail_errno();
  if (got) *got = (DWORD)n;
  return TRUE;
}
BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD bytes, LPDWORD written, LPVOID) {
  auto* h = static_cast<Handle*>(file);
  if (!h || h == INVALID_HANDLE_VALUE || h->kind != Handle::File) return FALSE;
  size_t done = 0;
  while (done < bytes) {
    const ssize_t n = write(h->fd, (const char*)buffer + done, bytes - done);
    if (n < 0) { if (errno == EINTR) continue; return fail_errno(); }
    done += (size_t)n;
  }
  if (written) *written = (DWORD)done;
  return TRUE;
}
BOOL FlushFileBuffers(HANDLE file) {
  auto* h = static_cast<Handle*>(file);
  return h && h != INVALID_HANDLE_VALUE && h->kind == Handle::File && fsync(h->fd) == 0;
}
BOOL GetFileSizeEx(HANDLE file, LARGE_INTEGER* size) {
  auto* h = static_cast<Handle*>(file);
  struct stat st{};
  if (!h || h == INVALID_HANDLE_VALUE || h->kind != Handle::File || fstat(h->fd, &st) != 0) return FALSE;
  size->QuadPart = st.st_size;
  return TRUE;
}
UINT GetSystemDirectoryW(LPWSTR out, UINT size) {
  static const wchar_t dir[] = L"/usr/bin";
  if (size <= std::wcslen(dir)) return (UINT)std::wcslen(dir) + 1;
  std::wcscpy(out, dir);
  return (UINT)std::wcslen(dir);
}
DWORD GetTempPathW(DWORD size, LPWSTR out) {
  const char* t = std::getenv("TMPDIR");
  const std::wstring dir = widen(t && *t ? t : "/tmp") + L"/";
  if (size <= dir.size()) return (DWORD)dir.size() + 1;
  std::wcscpy(out, dir.c_str());
  return (DWORD)dir.size();
}

// ---- desktop ----
HINSTANCE ShellExecuteA(HWND, LPCSTR, LPCSTR file, LPCSTR params, LPCSTR, int) {
  if (!file || !*file) return (HINSTANCE)(intptr_t)2;
  std::string target = file;
  // A program the shared code starts this way (the launcher reopening itself, the game): run it.
  const std::string program = linux_program(target);
  if (program != target || (access(program.c_str(), X_OK) == 0 && !fs::is_directory(program))) {
    std::vector<std::string> args{program};
    if (params) for (auto& a : split_command_line(params)) args.push_back(a);
    spawn_detached(args);
  } else {
    spawn_detached({"xdg-open", target});
  }
  return (HINSTANCE)(intptr_t)33;
}
HINSTANCE ShellExecuteW(HWND window, LPCWSTR verb, LPCWSTR file, LPCWSTR params, LPCWSTR directory, int show) {
  const std::string f = narrow(file), p = narrow(params);
  return ShellExecuteA(window, nullptr, f.c_str(), params ? p.c_str() : nullptr, nullptr, show);
}
int MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT type) {
  std::fprintf(stderr, "%s: %s\n", caption ? caption : "Melee Unlocked", text ? text : "");
  const bool question = (type & 0xF) == MB_YESNO || (type & 0xF) == MB_OKCANCEL;
  int status = 1;
  const std::string t = text ? text : "", c = caption ? caption : "Melee Unlocked";
  if (have_program("zenity")) {
    const char* kind = question ? "--question" : (type & 0xF0) == MB_ICONERROR ? "--error" : (type & 0xF0) == MB_ICONWARNING ? "--warning" : "--info";
    run_capture({"zenity", kind, "--no-markup", "--title=" + c, "--text=" + t}, nullptr, &status);
  } else if (have_program("kdialog")) {
    run_capture({"kdialog", "--title", c, question ? "--yesno" : (type & 0xF0) == MB_ICONERROR ? "--error" : "--msgbox", t}, nullptr, &status);
  } else {
    return question ? ((type & 0xF) == MB_YESNO ? IDNO : IDCANCEL) : IDOK;
  }
  if (!question) return IDOK;
  if ((type & 0xF) == MB_YESNO) return status == 0 ? IDYES : IDNO;
  return status == 0 ? IDOK : IDCANCEL;
}
int MessageBoxW(HWND w, LPCWSTR text, LPCWSTR caption, UINT type) {
  return MessageBoxA(w, narrow(text).c_str(), caption ? narrow(caption).c_str() : nullptr, type);
}
namespace linux_compat {
static std::atomic<bool> g_keys[256];
static std::atomic<bool> g_focused{false};
static std::atomic<HWND> g_game_window{nullptr};
void set_key_state(int vk, bool down) { if (vk >= 0 && vk < 256) g_keys[vk].store(down); }
void set_window_focused(bool focused) { g_focused.store(focused); }
bool window_focused() { return g_focused.load(); }
void set_game_window(HWND window) { g_game_window.store(window); }
}
// The game window, when it has focus. The pad read compares this with the game window itself
// (window.cpp: the keyboard counts only then), the replay keys with its process, which is ours. It
// answered a stand-in handle before, so the keyboard never reached the game on Linux.
HWND GetActiveWindow() { return nullptr; }
HWND GetForegroundWindow() {
  if (!linux_compat::window_focused()) return nullptr;
  const HWND window = linux_compat::g_game_window.load();
  return window ? window : (HWND)&g_current_process;
}
DWORD GetWindowThreadProcessId(HWND window, LPDWORD process) {
  if (process) *process = window ? (DWORD)getpid() : 0;
  return window ? 1u : 0u;
}
short GetAsyncKeyState(int key) {
  if (key < 0 || key >= 256) return 0;
  return linux_compat::g_keys[key].load() ? (short)0x8000 : 0;
}
void OutputDebugStringA(LPCSTR text) { if (std::getenv("MELEE_DEBUG_OUTPUT")) std::fputs(text, stderr); }
void OutputDebugStringW(LPCWSTR text) { OutputDebugStringA(narrow(text).c_str()); }

// ---- file dialogs ----
static std::vector<std::string> filter_patterns(LPCWSTR filter, std::vector<std::string>* labels) {
  std::vector<std::string> patterns;
  if (!filter) return patterns;
  for (const wchar_t* p = filter; *p;) {
    const std::wstring label = p; p += label.size() + 1;
    if (!*p) break;
    const std::wstring spec = p; p += spec.size() + 1;
    std::string s = narrow(spec.c_str());
    for (char& c : s) if (c == ';') c = ' ';
    patterns.push_back(s);
    if (labels) labels->push_back(narrow(label.c_str()));
  }
  return patterns;
}
static BOOL file_dialog(OPENFILENAMEW* dialog, bool save) {
  std::vector<std::string> labels;
  const auto patterns = filter_patterns(dialog->lpstrFilter, &labels);
  const bool multi = !save && (dialog->Flags & OFN_ALLOWMULTISELECT);
  const std::string title = dialog->lpstrTitle ? narrow(dialog->lpstrTitle) : save ? "Save" : "Open";
  std::string out; int status = 1;
  if (have_program("zenity")) {
    std::vector<std::string> argv{"zenity", "--file-selection", "--title=" + title, "--separator=\n"};
    if (multi) argv.push_back("--multiple");
    if (save) { argv.push_back("--save"); argv.push_back("--confirm-overwrite"); }
    for (size_t i = 0; i < patterns.size(); ++i) argv.push_back("--file-filter=" + labels[i] + " | " + patterns[i]);
    if (!run_capture(argv, &out, &status)) return FALSE;
  } else if (have_program("kdialog")) {
    std::string filter;
    for (size_t i = 0; i < patterns.size(); ++i) { if (i) filter += "\n"; filter += patterns[i] + "|" + labels[i]; }
    std::vector<std::string> argv{"kdialog", "--title", title, save ? "--getsavefilename" : "--getopenfilename", ".", filter};
    if (multi) { argv.push_back("--multiple"); argv.push_back("--separate-output"); }
    if (!run_capture(argv, &out, &status)) return FALSE;
  } else {
    MessageBoxA(nullptr, "Install zenity or kdialog to choose files.", "Melee Unlocked", MB_ICONWARNING);
    return FALSE;
  }
  if (status != 0) return FALSE;
  std::vector<std::string> files; std::stringstream ss(out); std::string line;
  while (std::getline(ss, line)) if (!line.empty()) files.push_back(line);
  if (files.empty()) return FALSE;
  std::wstring result;
  if (files.size() == 1) { result = widen(files[0].c_str()); result.push_back(L'\0'); }
  else {
    // Windows' multi-selection form: the folder, then each name, each ended by a null, then a null.
    const fs::path dir = fs::path(files[0]).parent_path();
    result = widen(dir.string().c_str()); result.push_back(L'\0');
    for (const auto& f : files) { result += widen(fs::path(f).filename().string().c_str()); result.push_back(L'\0'); }
  }
  result.push_back(L'\0');
  if (result.size() > dialog->nMaxFile) return FALSE;
  std::wmemcpy(dialog->lpstrFile, result.data(), result.size());
  return TRUE;
}
BOOL GetOpenFileNameW(OPENFILENAMEW* dialog) { return file_dialog(dialog, false); }
BOOL GetSaveFileNameW(OPENFILENAMEW* dialog) { return file_dialog(dialog, true); }
std::wstring linux_compat::choose_folder(const wchar_t* title) {
  std::string out; int status = 1;
  const std::string t = title ? narrow(title) : "Choose a folder";
  if (have_program("zenity")) run_capture({"zenity", "--file-selection", "--directory", "--title=" + t}, &out, &status);
  else if (have_program("kdialog")) run_capture({"kdialog", "--title", t, "--getexistingdirectory", "."}, &out, &status);
  if (status != 0) return {};
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return widen(out.c_str());
}

// ---- routing ----
DWORD GetBestRoute(DWORD, DWORD, MIB_IPFORWARDROW* row) {
  std::ifstream in("/proc/net/route"); std::string line;
  std::getline(in, line);   // header
  unsigned best_metric = ~0u; bool found = false;
  while (std::getline(in, line)) {
    char iface[64]; unsigned dest = 0, gateway = 0, flags = 0, refcnt, use, metric = 0, mask = 0;
    if (std::sscanf(line.c_str(), "%63s %x %x %x %u %u %u %x", iface, &dest, &gateway, &flags, &refcnt, &use, &metric, &mask) < 8) continue;
    if (dest != 0 || mask != 0 || !(flags & 0x2) || metric >= best_metric) continue;   // the default route via a gateway
    std::memset(row, 0, sizeof *row);
    row->dwForwardNextHop = gateway;   // /proc lists it in network byte order already
    best_metric = metric; found = true;
  }
  return found ? NO_ERROR : 1168u;   // ERROR_NOT_FOUND
}

// ---- BCrypt over OpenSSL ----
namespace {
struct Algorithm { const EVP_MD* md = nullptr; bool rng = false; };
struct Hash { EVP_MD_CTX* ctx = nullptr; const EVP_MD* md = nullptr; };
}
NTSTATUS BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE* algorithm, LPCWSTR id, LPCWSTR, ULONG) {
  const std::string name = narrow(id);
  auto* a = new Algorithm;
  if (name == "MD5") a->md = EVP_md5();
  else if (name == "SHA1") a->md = EVP_sha1();
  else if (name == "SHA256") a->md = EVP_sha256();
  else if (name == "RNG") a->rng = true;
  else { delete a; return STATUS_UNSUCCESSFUL; }
  *algorithm = a;
  return STATUS_SUCCESS;
}
NTSTATUS BCryptCloseAlgorithmProvider(BCRYPT_ALG_HANDLE algorithm, ULONG) { delete static_cast<Algorithm*>(algorithm); return STATUS_SUCCESS; }
NTSTATUS BCryptGetProperty(void* object, LPCWSTR property, PUCHAR output, ULONG size, ULONG* result, ULONG) {
  auto* a = static_cast<Algorithm*>(object);
  if (!a || !a->md || size < sizeof(DWORD)) return STATUS_UNSUCCESSFUL;
  const std::string p = narrow(property);
  DWORD value = p == "HashDigestLength" ? (DWORD)EVP_MD_get_size(a->md) : p == "ObjectLength" ? 64u : 0u;
  if (!value) return STATUS_UNSUCCESSFUL;
  std::memcpy(output, &value, sizeof value);
  if (result) *result = sizeof value;
  return STATUS_SUCCESS;
}
NTSTATUS BCryptCreateHash(BCRYPT_ALG_HANDLE algorithm, BCRYPT_HASH_HANDLE* hash, PUCHAR, ULONG, PUCHAR, ULONG, ULONG) {
  auto* a = static_cast<Algorithm*>(algorithm);
  if (!a || !a->md) return STATUS_UNSUCCESSFUL;
  auto* h = new Hash{EVP_MD_CTX_new(), a->md};
  if (!h->ctx || EVP_DigestInit_ex(h->ctx, h->md, nullptr) != 1) { EVP_MD_CTX_free(h->ctx); delete h; return STATUS_UNSUCCESSFUL; }
  *hash = h;
  return STATUS_SUCCESS;
}
NTSTATUS BCryptHashData(BCRYPT_HASH_HANDLE hash, PUCHAR input, ULONG size, ULONG) {
  auto* h = static_cast<Hash*>(hash);
  return h && EVP_DigestUpdate(h->ctx, input, size) == 1 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
NTSTATUS BCryptFinishHash(BCRYPT_HASH_HANDLE hash, PUCHAR output, ULONG size, ULONG) {
  auto* h = static_cast<Hash*>(hash);
  if (!h || size < (ULONG)EVP_MD_get_size(h->md)) return STATUS_UNSUCCESSFUL;
  unsigned n = 0;
  return EVP_DigestFinal_ex(h->ctx, output, &n) == 1 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
NTSTATUS BCryptDestroyHash(BCRYPT_HASH_HANDLE hash) {
  auto* h = static_cast<Hash*>(hash);
  if (h) { EVP_MD_CTX_free(h->ctx); delete h; }
  return STATUS_SUCCESS;
}
NTSTATUS BCryptHash(BCRYPT_ALG_HANDLE algorithm, PUCHAR, ULONG, PUCHAR input, ULONG input_size, PUCHAR output, ULONG output_size) {
  BCRYPT_HASH_HANDLE h = nullptr;
  NTSTATUS s = BCryptCreateHash(algorithm, &h, nullptr, 0, nullptr, 0, 0);
  if (s != STATUS_SUCCESS) return s;
  s = BCryptHashData(h, input, input_size, 0);
  if (s == STATUS_SUCCESS) s = BCryptFinishHash(h, output, output_size, 0);
  BCryptDestroyHash(h);
  return s;
}
NTSTATUS BCryptGenRandom(BCRYPT_ALG_HANDLE, PUCHAR buffer, ULONG size, ULONG) {
  size_t done = 0;
  while (done < size) {
    const ssize_t n = getrandom(buffer + done, size - done, 0);
    if (n < 0) { if (errno == EINTR) continue; return STATUS_UNSUCCESSFUL; }
    done += (size_t)n;
  }
  return STATUS_SUCCESS;
}

// ---- WinHTTP over libcurl ----
namespace {
struct HttpSession { std::string agent; int connect_ms = 60000, receive_ms = 30000; bool follow = true; };
struct HttpConnect { HttpSession* session; std::string host; INTERNET_PORT port; };
struct HttpRequest {
  HttpConnect* connect; std::string verb, path; bool secure; std::vector<std::string> headers;
  std::string body, response; size_t read_at = 0; long status = 0; bool done = false; bool follow;
  int connect_ms, receive_ms;
};
enum class Kind { Session, Connect, Request };
struct Any { Kind kind; void* p; };
size_t curl_write(char* data, size_t size, size_t count, void* user) {
  static_cast<std::string*>(user)->append(data, size * count);
  return size * count;
}
void add_header_lines(HttpRequest* r, const std::string& text) {
  std::stringstream ss(text); std::string line;
  while (std::getline(ss, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (!line.empty()) r->headers.push_back(line);
  }
}
}
HINTERNET WinHttpOpen(LPCWSTR agent, DWORD, LPCWSTR, LPCWSTR, DWORD) {
  static std::once_flag once; std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
  auto* s = new HttpSession; s->agent = narrow(agent);
  return new Any{Kind::Session, s};
}
HINTERNET WinHttpConnect(HINTERNET session, LPCWSTR host, INTERNET_PORT port, DWORD) {
  auto* a = static_cast<Any*>(session);
  if (!a || a->kind != Kind::Session) return nullptr;
  return new Any{Kind::Connect, new HttpConnect{static_cast<HttpSession*>(a->p), narrow(host), port}};
}
HINTERNET WinHttpOpenRequest(HINTERNET connect, LPCWSTR verb, LPCWSTR path, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD flags) {
  auto* a = static_cast<Any*>(connect);
  if (!a || a->kind != Kind::Connect) return nullptr;
  auto* c = static_cast<HttpConnect*>(a->p);
  auto* r = new HttpRequest{c, verb ? narrow(verb) : "GET", narrow(path), (flags & WINHTTP_FLAG_SECURE) != 0};
  r->follow = c->session->follow; r->connect_ms = c->session->connect_ms; r->receive_ms = c->session->receive_ms;
  return new Any{Kind::Request, r};
}
BOOL WinHttpSetTimeouts(HINTERNET handle, int, int connect, int, int receive) {
  auto* a = static_cast<Any*>(handle);
  if (!a) return FALSE;
  if (a->kind == Kind::Session) { auto* s = static_cast<HttpSession*>(a->p); s->connect_ms = connect; s->receive_ms = receive; }
  if (a->kind == Kind::Request) { auto* r = static_cast<HttpRequest*>(a->p); r->connect_ms = connect; r->receive_ms = receive; }
  return TRUE;
}
BOOL WinHttpSetOption(HINTERNET handle, DWORD option, LPVOID buffer, DWORD) {
  auto* a = static_cast<Any*>(handle);
  if (!a || option != WINHTTP_OPTION_REDIRECT_POLICY || !buffer) return TRUE;
  const bool follow = *static_cast<DWORD*>(buffer) != WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  if (a->kind == Kind::Session) static_cast<HttpSession*>(a->p)->follow = follow;
  if (a->kind == Kind::Request) static_cast<HttpRequest*>(a->p)->follow = follow;
  return TRUE;
}
BOOL WinHttpAddRequestHeaders(HINTERNET request, LPCWSTR headers, DWORD length, DWORD) {
  auto* a = static_cast<Any*>(request);
  if (!a || a->kind != Kind::Request || !headers) return FALSE;
  add_header_lines(static_cast<HttpRequest*>(a->p), narrow(headers, length == (DWORD)-1 ? std::string::npos : length));
  return TRUE;
}
BOOL WinHttpSendRequest(HINTERNET request, LPCWSTR headers, DWORD headers_length, LPVOID optional, DWORD optional_length,
                        DWORD, DWORD_PTR) {
  auto* a = static_cast<Any*>(request);
  if (!a || a->kind != Kind::Request) return FALSE;
  auto* r = static_cast<HttpRequest*>(a->p);
  if (headers) add_header_lines(r, narrow(headers, headers_length == (DWORD)-1 ? std::string::npos : headers_length));
  if (optional && optional_length) r->body.assign(static_cast<const char*>(optional), optional_length);
  return TRUE;
}
BOOL WinHttpReceiveResponse(HINTERNET request, LPVOID) {
  auto* a = static_cast<Any*>(request);
  if (!a || a->kind != Kind::Request) return FALSE;
  auto* r = static_cast<HttpRequest*>(a->p);
  if (r->done) return TRUE;
  CURL* curl = curl_easy_init();
  if (!curl) return FALSE;
  const bool default_port = r->connect->port == 0 || r->connect->port == (r->secure ? 443 : 80);
  std::string url = std::string(r->secure ? "https://" : "http://") + r->connect->host +
                    (default_port ? "" : ":" + std::to_string(r->connect->port)) + r->path;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, r->verb.c_str());
  if (r->verb == "HEAD") curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  if (!r->body.empty() || r->verb == "POST" || r->verb == "PUT") {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, r->body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)r->body.size());
  }
  curl_slist* list = nullptr;
  for (const auto& h : r->headers) list = curl_slist_append(list, h.c_str());
  if (list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
  if (!r->connect->session->agent.empty()) curl_easy_setopt(curl, CURLOPT_USERAGENT, r->connect->session->agent.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, r->follow ? 1L : 0L);
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https,http");
  if (r->connect_ms > 0) curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)r->connect_ms);
  if (r->receive_ms > 0) { curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, std::max(1L, (long)r->receive_ms / 1000)); curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L); }
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r->response);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  const CURLcode rc = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r->status);
  curl_slist_free_all(list);
  curl_easy_cleanup(curl);
  if (rc != CURLE_OK) { g_last_error = 12002u; return FALSE; }   // ERROR_WINHTTP_TIMEOUT-like
  r->done = true;
  return TRUE;
}
BOOL WinHttpQueryHeaders(HINTERNET request, DWORD level, LPCWSTR, LPVOID buffer, LPDWORD length, LPDWORD) {
  auto* a = static_cast<Any*>(request);
  if (!a || a->kind != Kind::Request || !buffer || !length) return FALSE;
  auto* r = static_cast<HttpRequest*>(a->p);
  const DWORD what = level & 0xFFFF;
  DWORD value = 0;
  if (what == WINHTTP_QUERY_STATUS_CODE) value = (DWORD)r->status;
  else if (what == WINHTTP_QUERY_CONTENT_LENGTH) value = (DWORD)r->response.size();
  else return FALSE;
  if (level & WINHTTP_QUERY_FLAG_NUMBER) {
    if (*length < sizeof value) return FALSE;
    std::memcpy(buffer, &value, sizeof value); *length = sizeof value;
  } else {
    const std::wstring text = std::to_wstring(value);
    const DWORD bytes = (DWORD)((text.size() + 1) * sizeof(wchar_t));
    if (*length < bytes) { *length = bytes; return FALSE; }
    std::memcpy(buffer, text.c_str(), bytes); *length = bytes - sizeof(wchar_t);
  }
  return TRUE;
}
BOOL WinHttpQueryDataAvailable(HINTERNET request, LPDWORD available) {
  auto* a = static_cast<Any*>(request);
  if (!a || a->kind != Kind::Request || !available) return FALSE;
  auto* r = static_cast<HttpRequest*>(a->p);
  *available = (DWORD)(r->response.size() - r->read_at);
  return TRUE;
}
BOOL WinHttpReadData(HINTERNET request, LPVOID buffer, DWORD size, LPDWORD got) {
  auto* a = static_cast<Any*>(request);
  if (!a || a->kind != Kind::Request) return FALSE;
  auto* r = static_cast<HttpRequest*>(a->p);
  const size_t n = std::min<size_t>(size, r->response.size() - r->read_at);
  std::memcpy(buffer, r->response.data() + r->read_at, n);
  r->read_at += n;
  if (got) *got = (DWORD)n;
  return TRUE;
}
BOOL WinHttpCloseHandle(HINTERNET handle) {
  auto* a = static_cast<Any*>(handle);
  if (!a) return FALSE;
  if (a->kind == Kind::Session) delete static_cast<HttpSession*>(a->p);
  else if (a->kind == Kind::Connect) delete static_cast<HttpConnect*>(a->p);
  else delete static_cast<HttpRequest*>(a->p);
  delete a;
  return TRUE;
}
BOOL WinHttpCrackUrl(LPCWSTR url, DWORD length, DWORD, URL_COMPONENTS* parts) {
  if (!url || !parts) return FALSE;
  const std::wstring u = length ? std::wstring(url, length) : std::wstring(url);
  const size_t scheme_end = u.find(L"://");
  if (scheme_end == std::wstring::npos) return FALSE;
  const std::wstring scheme = u.substr(0, scheme_end);
  const bool https = _wcsicmp(scheme.c_str(), L"https") == 0;
  size_t host_start = scheme_end + 3;
  size_t path_start = u.find(L'/', host_start);
  if (path_start == std::wstring::npos) path_start = u.size();
  std::wstring hostport = u.substr(host_start, path_start - host_start);
  INTERNET_PORT port = https ? 443 : 80;
  const size_t colon = hostport.find(L':');
  if (colon != std::wstring::npos) { port = (INTERNET_PORT)std::wcstoul(hostport.c_str() + colon + 1, nullptr, 10); hostport.resize(colon); }
  size_t extra_start = u.find(L'?', path_start);
  if (extra_start == std::wstring::npos) extra_start = u.size();
  parts->nScheme = https ? INTERNET_SCHEME_HTTPS : INTERNET_SCHEME_HTTP;
  parts->nPort = port;
  // Windows hands back pointers into the URL with lengths when the caller passed a length of 1 and
  // no buffer, or copies into the caller's buffers otherwise.
  auto fill = [&](LPWSTR& ptr, DWORD& len, size_t start, size_t count) {
    if (!len && !ptr) return;
    if (!ptr) { ptr = const_cast<wchar_t*>(url) + start; len = (DWORD)count; return; }
    if (len <= count) return;
    std::wmemcpy(ptr, u.c_str() + start, count); ptr[count] = 0; len = (DWORD)count;
  };
  fill(parts->lpszScheme, parts->dwSchemeLength, 0, scheme_end);
  fill(parts->lpszHostName, parts->dwHostNameLength, host_start, hostport.size());
  fill(parts->lpszUrlPath, parts->dwUrlPathLength, path_start, extra_start - path_start);
  fill(parts->lpszExtraInfo, parts->dwExtraInfoLength, extra_start, u.size() - extra_start);
  return TRUE;
}

// ---- memory status, process lists ----
#include "tlhelp32.h"
#include "psapi.h"
#include <sched.h>
#include <dirent.h>
#include <sys/sysinfo.h>
BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX* status) {
  struct sysinfo si{};
  if (sysinfo(&si) != 0) return FALSE;
  const uint64_t unit = si.mem_unit ? si.mem_unit : 1;
  status->ullTotalPhys = (uint64_t)si.totalram * unit;
  status->ullAvailPhys = ((uint64_t)si.freeram + si.bufferram) * unit;
  status->ullTotalPageFile = ((uint64_t)si.totalram + si.totalswap) * unit;
  status->ullAvailPageFile = ((uint64_t)si.freeram + si.freeswap) * unit;
  status->ullTotalVirtual = 1ull << 47;
  status->ullAvailVirtual = 1ull << 46;
  status->ullAvailExtendedVirtual = 0;
  status->dwMemoryLoad = status->ullTotalPhys ? (DWORD)(100 - status->ullAvailPhys * 100 / status->ullTotalPhys) : 0;
  return TRUE;
}
BOOL SwitchToThread() { return sched_yield() == 0; }
BOOL K32GetProcessMemoryInfo(HANDLE, PROCESS_MEMORY_COUNTERS* counters, DWORD size) {
  std::ifstream in("/proc/self/status"); std::string line;
  uint64_t rss = 0, peak = 0, vm = 0;
  while (std::getline(in, line)) {
    unsigned long long kb = 0;
    if (std::sscanf(line.c_str(), "VmRSS: %llu kB", &kb) == 1) rss = kb * 1024;
    else if (std::sscanf(line.c_str(), "VmHWM: %llu kB", &kb) == 1) peak = kb * 1024;
    else if (std::sscanf(line.c_str(), "VmData: %llu kB", &kb) == 1) vm = kb * 1024;
  }
  std::memset(counters, 0, size);
  counters->cb = size;
  counters->WorkingSetSize = rss; counters->PeakWorkingSetSize = peak;
  counters->PagefileUsage = vm; counters->PeakPagefileUsage = vm;
  if (size >= sizeof(PROCESS_MEMORY_COUNTERS_EX)) reinterpret_cast<PROCESS_MEMORY_COUNTERS_EX*>(counters)->PrivateUsage = vm;
  return TRUE;
}
namespace {
struct Snapshot : Handle { Snapshot() : Handle(Snap) {} std::vector<PROCESSENTRY32W> processes; std::vector<THREADENTRY32> threads; size_t at = 0; };
}
HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD) {
  auto* s = new Snapshot;
  DIR* proc = opendir("/proc");
  if (!proc) { delete s; return INVALID_HANDLE_VALUE; }
  while (dirent* e = readdir(proc)) {
    char* end = nullptr; const long pid = std::strtol(e->d_name, &end, 10);
    if (!end || *end || pid <= 0) continue;
    if (flags & TH32CS_SNAPPROCESS) {
      PROCESSENTRY32W p{}; p.dwSize = sizeof p; p.th32ProcessID = (DWORD)pid;
      std::ifstream comm("/proc/" + std::string(e->d_name) + "/comm"); std::string name; std::getline(comm, name);
      std::error_code ec; const fs::path exe = fs::read_symlink("/proc/" + std::string(e->d_name) + "/exe", ec);
      if (!ec) name = exe.filename().string();
      const std::wstring w = widen(name.c_str());
      wcsncpy_s(p.szExeFile, MAX_PATH, w.c_str(), _TRUNCATE);
      std::ifstream stat("/proc/" + std::string(e->d_name) + "/stat"); std::string st; std::getline(stat, st);
      const size_t close_paren = st.rfind(')');
      if (close_paren != std::string::npos) { char state; int ppid = 0; std::sscanf(st.c_str() + close_paren + 1, " %c %d", &state, &ppid); p.th32ParentProcessID = (DWORD)ppid; }
      s->processes.push_back(p);
    }
    if ((flags & TH32CS_SNAPTHREAD) && pid == getpid()) {
      DIR* tasks = opendir(("/proc/" + std::string(e->d_name) + "/task").c_str());
      if (tasks) {
        while (dirent* t = readdir(tasks)) {
          const long tid = std::strtol(t->d_name, &end, 10);
          if (!end || *end || tid <= 0) continue;
          THREADENTRY32 te{}; te.dwSize = sizeof te; te.th32ThreadID = (DWORD)tid; te.th32OwnerProcessID = (DWORD)pid;
          s->threads.push_back(te);
        }
        closedir(tasks);
      }
    }
  }
  closedir(proc);
  return s;
}
BOOL Process32FirstW(HANDLE snapshot, PROCESSENTRY32W* entry) {
  auto* s = static_cast<Snapshot*>(snapshot); s->at = 0;
  return Process32NextW(snapshot, entry);
}
BOOL Process32NextW(HANDLE snapshot, PROCESSENTRY32W* entry) {
  auto* s = static_cast<Snapshot*>(snapshot);
  if (s->at >= s->processes.size()) return FALSE;
  *entry = s->processes[s->at++];
  return TRUE;
}
BOOL Thread32First(HANDLE snapshot, THREADENTRY32* entry) {
  auto* s = static_cast<Snapshot*>(snapshot); s->at = 0;
  return Thread32Next(snapshot, entry);
}
BOOL Thread32Next(HANDLE snapshot, THREADENTRY32* entry) {
  auto* s = static_cast<Snapshot*>(snapshot);
  if (s->at >= s->threads.size()) return FALSE;
  *entry = s->threads[s->at++];
  return TRUE;
}

namespace linux_compat {
int fd_of_handle(HANDLE file) {
  auto* h = static_cast<Handle*>(file);
  if (!h || h == INVALID_HANDLE_VALUE || h->kind != Handle::File) return -1;
  return h->fd;
}
}

#include "compat_more.inl"
